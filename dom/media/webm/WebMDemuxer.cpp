/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebMDemuxer.h"

#include <opus/opus.h>
#include <stdint.h>

#include <algorithm>
#include <numeric>

#include "AOMDecoder.h"
#include "MediaDataDemuxer.h"
#include "MediaResource.h"
#include "NesteggPacketHolder.h"
#include "OpusDecoder.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "WebMBufferedParser.h"
#include "XiphExtradata.h"
#include "gfx2DGlue.h"
#include "gfxUtils.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Maybe.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/Sprintf.h"
#include "nsAutoRef.h"
#include "nsError.h"
#include "prprf.h"  // leaving it for PR_vsnprintf()

#define WEBM_DEBUG(arg, ...)                                          \
  DDMOZ_LOG(gMediaDemuxerLog, mozilla::LogLevel::Debug, "::%s: " arg, \
            __func__, ##__VA_ARGS__)
extern mozilla::LazyLogModule gMediaDemuxerLog;

namespace mozilla {

using namespace gfx;
using media::TimeUnit;

LazyLogModule gNesteggLog("Nestegg");

#define NSECS_PER_USEC 1000

// How far ahead will we look when searching future keyframe. In microseconds.
// This value is based on what appears to be a reasonable value as most webm
// files encountered appear to have keyframes located < 4s.
#define MAX_LOOK_AHEAD 10000000

// Functions for reading and seeking using WebMDemuxer required for
// nestegg_io. The 'user data' passed to these functions is the
// demuxer's context.
static int64_t webmdemux_read(void* aBuffer, size_t aLength, void* aUserData) {
  MOZ_ASSERT(aUserData);
  MOZ_ASSERT(aLength < UINT32_MAX);
  WebMDemuxer::NestEggContext* context =
      reinterpret_cast<WebMDemuxer::NestEggContext*>(aUserData);
  // Nestegg buffers reads internally and may request up to its IO buffer
  // size (several KiB) even when the parser only needs a few bytes.
  // SourceBufferResource returns NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA for any
  // read that extends past currently-appended data rather than serving a
  // partial read, so an unclamped read-ahead near the end of an MSE append
  // would misreport a fully-available packet as waiting for data. Clamp to
  // the cached range so short reads surface through the new nestegg
  // short-read path, and only let WAITING_FOR_DATA propagate when no bytes
  // are currently cached at the stream position.
  if (context->IsMediaSource()) {
    int64_t offset = context->GetResource()->Tell();
    int64_t cachedEnd =
        context->GetResource()->GetResource()->GetCachedDataEnd(offset);
    if (cachedEnd > offset) {
      int64_t available = cachedEnd - offset;
      if (static_cast<int64_t>(aLength) > available) {
        aLength = static_cast<size_t>(available);
      }
    }
  }
  uint32_t bytes = 0;
  context->mLastIORV = context->GetResource()->Read(static_cast<char*>(aBuffer),
                                                    aLength, &bytes);
  if (NS_FAILED(context->mLastIORV)) {
    return -1;
  }
  return bytes;
}

static int webmdemux_seek(int64_t aOffset, int aWhence, void* aUserData) {
  MOZ_ASSERT(aUserData);
  WebMDemuxer::NestEggContext* context =
      reinterpret_cast<WebMDemuxer::NestEggContext*>(aUserData);
  nsresult rv = context->GetResource()->Seek(aWhence, aOffset);
  // Don't overwrite mLastIORV on success: nestegg performs internal rewind
  // seeks after failed reads, and a successful rewind must not mask the
  // original read failure (e.g. NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA).
  if (NS_FAILED(rv)) {
    context->mLastIORV = rv;
    return -1;
  }
  return 0;
}

static int64_t webmdemux_tell(void* aUserData) {
  MOZ_ASSERT(aUserData);
  WebMDemuxer::NestEggContext* context =
      reinterpret_cast<WebMDemuxer::NestEggContext*>(aUserData);
  return context->GetResource()->Tell();
}

static void webmdemux_log(nestegg* aContext, unsigned int aSeverity,
                          char const* aFormat, ...) {
  if (!MOZ_LOG_TEST(gNesteggLog, LogLevel::Debug)) {
    return;
  }

  va_list args;
  char msg[256];
  const char* sevStr;

  switch (aSeverity) {
    case NESTEGG_LOG_DEBUG:
      sevStr = "DBG";
      break;
    case NESTEGG_LOG_INFO:
      sevStr = "INF";
      break;
    case NESTEGG_LOG_WARNING:
      sevStr = "WRN";
      break;
    case NESTEGG_LOG_ERROR:
      sevStr = "ERR";
      break;
    case NESTEGG_LOG_CRITICAL:
      sevStr = "CRT";
      break;
    default:
      sevStr = "UNK";
      break;
  }

  va_start(args, aFormat);

  SprintfLiteral(msg, "%p [Nestegg-%s] ", aContext, sevStr);
  PR_vsnprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), aFormat, args);
  MOZ_LOG_FMT(gNesteggLog, LogLevel::Debug, "{}", msg);

  va_end(args);
}

WebMDemuxer::NestEggContext::~NestEggContext() {
  if (mContext) {
    nestegg_destroy(mContext);
  }
}

int WebMDemuxer::NestEggContext::Init() {
  nestegg_io io;
  io.read = webmdemux_read;
  io.seek = webmdemux_seek;
  io.tell = webmdemux_tell;
  io.userdata = this;

  return nestegg_init(
      &mContext, io, &webmdemux_log,
      // nestegg_init() would return an error, from ne_parse(), if a resource
      // read were to fail.
      // For MediaSource, TrackBuffersManager::InitializationSegmentReceived()
      // calls WebMDemuxer::Init() while the resource has cached only the
      // bytes of the initialization segment.  max_offset is passed so that no
      // read will fail.
      mParent->IsMediaSource() ? mResource.GetResource()->GetCachedDataEnd(0)
                               : -1);
}

WebMDemuxer::WebMDemuxer(MediaResource* aResource)
    : WebMDemuxer(aResource, false) {}

WebMDemuxer::WebMDemuxer(
    MediaResource* aResource, bool aIsMediaSource,
    Maybe<media::TimeUnit> aFrameEndTimeBeforeRecreateDemuxer)
    : mVideoContext(this, aResource),
      mAudioContext(this, aResource),
      mBufferedState(nullptr),
      mInitData(nullptr),
      mVideoTrack(0),
      mAudioTrack(0),
      mSeekPreroll(0),
      mAudioCodec(-1),
      mVideoCodec(-1),
      mHasVideo(false),
      mHasAudio(false),
      mNeedReIndex(true),
      mIsMediaSource(aIsMediaSource) {
  DDLINKCHILD("resource", aResource);
  // Audio/video contexts hold a MediaResourceIndex.
  DDLINKCHILD("video context", mVideoContext.GetResource());
  DDLINKCHILD("audio context", mAudioContext.GetResource());

  MOZ_ASSERT_IF(!aIsMediaSource,
                aFrameEndTimeBeforeRecreateDemuxer.isNothing());
  if (aIsMediaSource && aFrameEndTimeBeforeRecreateDemuxer) {
    mVideoFrameEndTimeBeforeReset = aFrameEndTimeBeforeRecreateDemuxer;
    WEBM_DEBUG("Set mVideoFrameEndTimeBeforeReset=%" PRId64,
               mVideoFrameEndTimeBeforeReset->ToMicroseconds());
  }
}

WebMDemuxer::~WebMDemuxer() {
  Reset(TrackInfo::kVideoTrack);
  Reset(TrackInfo::kAudioTrack);
}

RefPtr<WebMDemuxer::InitPromise> WebMDemuxer::Init() {
  InitBufferedState();

  if (NS_FAILED(ReadMetadata())) {
    return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_METADATA_ERR,
                                        __func__);
  }

  if (!GetNumberTracks(TrackInfo::kAudioTrack) &&
      !GetNumberTracks(TrackInfo::kVideoTrack)) {
    return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_METADATA_ERR,
                                        __func__);
  }

  return InitPromise::CreateAndResolve(NS_OK, __func__);
}

void WebMDemuxer::InitBufferedState() {
  MOZ_ASSERT(!mBufferedState);
  mBufferedState = new WebMBufferedState;
}

uint32_t WebMDemuxer::GetNumberTracks(TrackInfo::TrackType aType) const {
  switch (aType) {
    case TrackInfo::kAudioTrack:
      return mHasAudio ? 1 : 0;
    case TrackInfo::kVideoTrack:
      return mHasVideo ? 1 : 0;
    default:
      return 0;
  }
}

UniquePtr<TrackInfo> WebMDemuxer::GetTrackInfo(TrackInfo::TrackType aType,
                                               size_t aTrackNumber) const {
  switch (aType) {
    case TrackInfo::kAudioTrack:
      return mInfo.mAudio.Clone();
    case TrackInfo::kVideoTrack:
      return mInfo.mVideo.Clone();
    default:
      return nullptr;
  }
}

already_AddRefed<MediaTrackDemuxer> WebMDemuxer::GetTrackDemuxer(
    TrackInfo::TrackType aType, uint32_t aTrackNumber) {
  if (GetNumberTracks(aType) <= aTrackNumber) {
    return nullptr;
  }
  RefPtr<WebMTrackDemuxer> e = new WebMTrackDemuxer(this, aType, aTrackNumber);
  DDLINKCHILD("track demuxer", e.get());
  mDemuxers.AppendElement(e);

  return e.forget();
}

void WebMDemuxer::Reset(TrackInfo::TrackType aType) {
  mProcessedDiscardPadding = false;
  if (aType == TrackInfo::kVideoTrack) {
    mVideoPackets.Reset();
  } else {
    mAudioPackets.Reset();
  }
}

int64_t WebMDemuxer::FloorDefaultDurationToTimecodeScale(
    nestegg* aContext, unsigned aTrackNumber) {
  uint64_t durationNanoSecs;
  // https://www.webmproject.org/docs/container/#DefaultDuration
  if (0 != nestegg_track_default_duration(aContext, aTrackNumber,
                                          &durationNanoSecs)) {
    return -1;
  }

  // https://www.webmproject.org/docs/container/#TimecodeScale
  uint64_t timecodeScale = 0;
  nestegg_tstamp_scale(aContext, &timecodeScale);
  if (timecodeScale == 0) {
    // Zero TimecodeScale would make timestamps all zero.
    // The Segment should have triggered an error before now, but use the
    // specified default if that has not happened.
    // https://www.ietf.org/archive/id/draft-ietf-cellar-matroska-21.html#name-timestampscale-element
    WEBM_DEBUG("Zero timecode scale");
    timecodeScale = PR_NSEC_PER_MSEC;
  }
  // Round down to nearest multiple of TimecodeScale.
  // Round down again to microseconds.
  // This avoids having block end times unintentionally overlap subsequent
  // frame start times, which would cause subsequent frames to be removed from
  // MediaSource buffers.
  return AssertedCast<int64_t>(durationNanoSecs / timecodeScale *
                               timecodeScale / NSECS_PER_USEC);
}

nsresult WebMDemuxer::SetVideoCodecInfo(nestegg* aContext, int aTrackId) {
  mVideoCodec = nestegg_track_codec_id(aContext, aTrackId);
  switch (mVideoCodec) {
    case NESTEGG_CODEC_VP8:
      mInfo.mVideo.mMimeType = "video/vp8";
      break;
    case NESTEGG_CODEC_VP9:
      mInfo.mVideo.mMimeType = "video/vp9";
      break;
    case NESTEGG_CODEC_AV1:
      mInfo.mVideo.mMimeType = "video/av1";
      break;
    default:
      NS_WARNING("Unknown WebM video codec");
      return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult WebMDemuxer::SetContainerAudioCodecInfo(
    nestegg* aContext, const nestegg_audio_params& aParams) {
  switch (mAudioCodec) {
    case NESTEGG_CODEC_VORBIS: {
      mInfo.mAudio.mCodecSpecificConfig =
          AudioCodecSpecificVariant{VorbisCodecSpecificData{}};
      mInfo.mAudio.mMimeType = "audio/vorbis";
      break;
    }
    case NESTEGG_CODEC_OPUS: {
      uint64_t codecDelayUs = aParams.codec_delay / NSECS_PER_USEC;
      mInfo.mAudio.mMimeType = "audio/opus";
      OpusCodecSpecificData opusCodecSpecificData;
      opusCodecSpecificData.mContainerCodecDelayFrames =
          AssertedCast<int64_t>(USECS_PER_S * codecDelayUs / 48000);
      WEBM_DEBUG("Preroll for Opus: %" PRIu64 " frames",
                 opusCodecSpecificData.mContainerCodecDelayFrames);
      mInfo.mAudio.mCodecSpecificConfig =
          AudioCodecSpecificVariant{std::move(opusCodecSpecificData)};
      break;
    }
    default:
      NS_WARNING("Unknown WebM audio codec");
      return NS_ERROR_DOM_MEDIA_METADATA_ERR;
  }
  return NS_OK;
}

nsresult WebMDemuxer::SetAudioCodecInfo(nestegg* aContext, int aTrackId,
                                        const nestegg_audio_params& aParams) {
  mAudioCodec = nestegg_track_codec_id(aContext, aTrackId);

  nsresult rv = SetContainerAudioCodecInfo(aContext, aParams);
  if (NS_FAILED(rv)) {
    return rv;
  }

  AutoTArray<const unsigned char*, 4> headers;
  AutoTArray<size_t, 4> headerLens;
  rv = GetCodecPrivateData(aContext, aTrackId, &headers, &headerLens);
  if (NS_FAILED(rv)) {
    WEBM_DEBUG("GetCodecPrivateData error for WebM");
    return rv;
  }

  // Vorbis has 3 headers, convert to Xiph extradata format to send them to
  // the demuxer.
  // TODO: This is already the format WebM stores them in. Would be nice
  // to avoid having libnestegg split them only for us to pack them again,
  // but libnestegg does not give us an API to access this data directly.
  RefPtr<MediaByteBuffer> audioCodecSpecificBlob =
      GetAudioCodecSpecificBlob(mInfo.mAudio.mCodecSpecificConfig);
  if (headers.Length() > 1) {
    if (!XiphHeadersToExtradata(audioCodecSpecificBlob, headers, headerLens)) {
      WEBM_DEBUG("Couldn't parse Xiph headers");
      return NS_ERROR_FAILURE;
    }
  } else {
    audioCodecSpecificBlob->AppendElements(headers[0], headerLens[0]);
  }

  return NS_OK;
}

nsresult WebMDemuxer::GetCodecPrivateData(
    nestegg* aContext, int aTrackId, nsTArray<const unsigned char*>* aHeaders,
    nsTArray<size_t>* aHeaderLens) {
  unsigned int nheaders = 0;
  int r = nestegg_track_codec_data_count(aContext, aTrackId, &nheaders);
  if (r == -1) {
    WEBM_DEBUG("nestegg_track_codec_data_count error");
    return NS_ERROR_FAILURE;
  }

  for (uint32_t header = 0; header < nheaders; ++header) {
    unsigned char* data = 0;
    size_t length = 0;
    r = nestegg_track_codec_data(aContext, aTrackId, header, &data, &length);
    if (r == -1) {
      WEBM_DEBUG("nestegg_track_codec_data error");
      return NS_ERROR_FAILURE;
    }
    aHeaders->AppendElement(data);
    aHeaderLens->AppendElement(length);
  }
  return NS_OK;
}

static Maybe<gfx::HDRMetadata> ParseWebMMasteringMetadata(
    const nestegg_video_params& aParams) {
  gfx::HDRMetadata hdr;
  if (!std::isnan(aParams.primary_r_chromacity_x)) {
    gfx::Smpte2086Metadata smpte;
    smpte.displayPrimaryRed.x =
        static_cast<float>(aParams.primary_r_chromacity_x);
    smpte.displayPrimaryRed.y =
        static_cast<float>(aParams.primary_r_chromacity_y);
    smpte.displayPrimaryGreen.x =
        static_cast<float>(aParams.primary_g_chromacity_x);
    smpte.displayPrimaryGreen.y =
        static_cast<float>(aParams.primary_g_chromacity_y);
    smpte.displayPrimaryBlue.x =
        static_cast<float>(aParams.primary_b_chromacity_x);
    smpte.displayPrimaryBlue.y =
        static_cast<float>(aParams.primary_b_chromacity_y);
    smpte.whitePoint.x = static_cast<float>(aParams.white_point_chromaticity_x);
    smpte.whitePoint.y = static_cast<float>(aParams.white_point_chromaticity_y);
    smpte.maxLuminance = static_cast<float>(aParams.luminance_max);
    smpte.minLuminance = static_cast<float>(aParams.luminance_min);
    hdr.mSmpte2086 = Some(smpte);
  }
  if (aParams.max_cll != 0 || aParams.max_fall != 0) {
    gfx::ContentLightLevel cll;
    cll.maxContentLightLevel = aParams.max_cll;
    cll.maxFrameAverageLightLevel = aParams.max_fall;
    hdr.mContentLightLevel = Some(cll);
  }
  if (!hdr.IsValid()) {
    return Nothing();
  }
  return Some(hdr);
}

nsresult WebMDemuxer::ReadMetadata() {
  int r = mVideoContext.Init();
  if (r == -1) {
    WEBM_DEBUG("mVideoContext::Init failure");
    return NS_ERROR_FAILURE;
  }
  if (mAudioContext.Init() == -1) {
    WEBM_DEBUG("mAudioContext::Init failure");
    return NS_ERROR_FAILURE;
  }

  // Both contexts have the metadata; the video context is used here.
  MediaResourceIndex& resource = Resource(TrackInfo::kVideoTrack);
  nestegg* context = Context(TrackInfo::kVideoTrack);

  {
    // Check how much data nestegg read and force feed it to BufferedState.
    RefPtr<MediaByteBuffer> buffer = resource.MediaReadAt(0, resource.Tell());
    if (!buffer) {
      WEBM_DEBUG("resource.MediaReadAt error");
      return NS_ERROR_FAILURE;
    }
    mBufferedState->NotifyDataArrived(buffer->Elements(), buffer->Length(), 0);
    if (mBufferedState->GetInitEndOffset() < 0) {
      WEBM_DEBUG("Couldn't find init end");
      return NS_ERROR_FAILURE;
    }
    MOZ_ASSERT(mBufferedState->GetInitEndOffset() <= resource.Tell());
  }
  mInitData = resource.MediaReadAt(0, mBufferedState->GetInitEndOffset());
  if (!mInitData ||
      mInitData->Length() != size_t(mBufferedState->GetInitEndOffset())) {
    WEBM_DEBUG("Couldn't read init data");
    return NS_ERROR_FAILURE;
  }

  unsigned int ntracks = 0;
  r = nestegg_track_count(context, &ntracks);
  if (r == -1) {
    WEBM_DEBUG("nestegg_track_count error");
    return NS_ERROR_FAILURE;
  }

  for (unsigned int track = 0; track < ntracks; ++track) {
    int id = nestegg_track_codec_id(context, track);
    if (id == -1) {
      WEBM_DEBUG("nestegg_track_codec_id error");
      return NS_ERROR_FAILURE;
    }

    WEBM_DEBUG("Read metadata, track %u, codec id %d", track, id);
    int type = nestegg_track_type(context, track);
    if (type == NESTEGG_TRACK_VIDEO && !mHasVideo) {
      nestegg_video_params params;
      r = nestegg_track_video_params(context, track, &params);
      if (r == -1) {
        WEBM_DEBUG("nestegg_track_video_params error");
        return NS_ERROR_FAILURE;
      }
      mVideoDefaultDuration =
          FloorDefaultDurationToTimecodeScale(context, track);
      nsresult rv = SetVideoCodecInfo(context, track);
      if (NS_FAILED(rv)) {
        WEBM_DEBUG("Set video codec info error, ignoring track");
        continue;
      }
      mInfo.mVideo.mColorPrimaries = gfxUtils::CicpToColorPrimaries(
          static_cast<gfx::CICP::ColourPrimaries>(params.primaries),
          gMediaDemuxerLog);

      // For VPX, this is our only chance to capture the transfer
      // characteristics, which we can't get from a VPX bitstream later.
      // We only need this value if the video is using the BT2020
      // colorspace, which will be determined on a per-frame basis later.
      mInfo.mVideo.mTransferFunction = gfxUtils::CicpToTransferFunction(
          static_cast<gfx::CICP::TransferCharacteristics>(
              params.transfer_characteristics));

      mInfo.mVideo.mHDRMetadata = ParseWebMMasteringMetadata(params);

      CheckedInt<uint32_t> cropH =
          CheckedInt<uint32_t>(params.crop_left) + params.crop_right;
      CheckedInt<uint32_t> cropV =
          CheckedInt<uint32_t>(params.crop_top) + params.crop_bottom;
      if (!cropH.isValid() || !cropV.isValid() ||
          cropH.value() >= params.width || cropV.value() >= params.height) {
        WEBM_DEBUG("Invalid crop values left: %u right: %u top: %u bottom: %u",
                   params.crop_left, params.crop_right, params.crop_top,
                   params.crop_bottom);
        continue;
      }

      // Validate the container-reported frame and pictureRect sizes. This
      // ensures that our video frame creation code doesn't overflow.
      gfx::IntSize displaySize(params.display_width, params.display_height);
      gfx::IntSize frameSize(params.width, params.height);
      const uint32_t croppedWidth = params.width - cropH.value();
      const uint32_t croppedHeight = params.height - cropV.value();
      gfx::IntRect pictureRect(AssertedCast<int32_t>(params.crop_left),
                               AssertedCast<int32_t>(params.crop_top),
                               AssertedCast<int32_t>(croppedWidth),
                               AssertedCast<int32_t>(croppedHeight));
      if (!IsValidVideoRegion(frameSize, pictureRect, displaySize)) {
        // Video track's frame sizes will overflow. Ignore the video track.
        continue;
      }

      mVideoTrack = track;
      mHasVideo = true;

      mInfo.mVideo.mDisplay = displaySize;
      mInfo.mVideo.mImage = frameSize;
      mInfo.mVideo.SetImageRect(pictureRect);
      mInfo.mVideo.SetAlpha(params.alpha_mode);

      switch (params.stereo_mode) {
        case NESTEGG_VIDEO_MONO:
          mInfo.mVideo.mStereoMode = StereoMode::MONO;
          break;
        case NESTEGG_VIDEO_STEREO_LEFT_RIGHT:
          mInfo.mVideo.mStereoMode = StereoMode::LEFT_RIGHT;
          break;
        case NESTEGG_VIDEO_STEREO_BOTTOM_TOP:
          mInfo.mVideo.mStereoMode = StereoMode::BOTTOM_TOP;
          break;
        case NESTEGG_VIDEO_STEREO_TOP_BOTTOM:
          mInfo.mVideo.mStereoMode = StereoMode::TOP_BOTTOM;
          break;
        case NESTEGG_VIDEO_STEREO_RIGHT_LEFT:
          mInfo.mVideo.mStereoMode = StereoMode::RIGHT_LEFT;
          break;
      }
      uint64_t duration = 0;
      r = nestegg_duration(context, &duration);
      if (!r) {
        mInfo.mVideo.mDuration = TimeUnit::FromNanoseconds(duration);
      }
      WEBM_DEBUG("stream duration: %lf\n", mInfo.mVideo.mDuration.ToSeconds());
      mInfo.mVideo.mCrypto = GetTrackCrypto(TrackInfo::kVideoTrack, track);
      if (mInfo.mVideo.mCrypto.IsEncrypted()) {
        MOZ_ASSERT(mInfo.mVideo.mCrypto.mCryptoScheme == CryptoScheme::Cenc,
                   "WebM should only use cenc scheme");
        mCrypto.AddInitData(u"webm"_ns, mInfo.mVideo.mCrypto.mKeyId);
      }
    } else if (type == NESTEGG_TRACK_AUDIO && !mHasAudio) {
      nestegg_audio_params params;
      r = nestegg_track_audio_params(context, track, &params);
      if (r == -1) {
        WEBM_DEBUG("nestegg_track_audio_params error");
        return NS_ERROR_FAILURE;
      }

      if (params.rate <= 0 || params.rate > AudioInfo::MAX_RATE ||
          params.channels == 0 ||
          params.channels > AudioConfig::ChannelLayout::MAX_CHANNELS) {
        WEBM_DEBUG("Invalid audio param rate: %lf channel count: %d",
                   params.rate, params.channels);
        return NS_ERROR_DOM_MEDIA_METADATA_ERR;
      }
      const uint32_t rate = AssertedCast<uint32_t>(params.rate);
      params.rate = rate;

      nsresult rv = SetAudioCodecInfo(context, track, params);
      if (NS_FAILED(rv)) {
        WEBM_DEBUG("Set audio codec info error, ignoring track");
        continue;
      }

      mAudioTrack = track;
      mHasAudio = true;
      mAudioDefaultDuration =
          FloorDefaultDurationToTimecodeScale(context, track);
      mSeekPreroll = params.seek_preroll;
      mInfo.mAudio.mRate = rate;
      mInfo.mAudio.mChannels = params.channels;

      uint64_t duration = 0;
      r = nestegg_duration(context, &duration);
      if (!r) {
        mInfo.mAudio.mDuration = TimeUnit::FromNanoseconds(duration);
        WEBM_DEBUG("audio track duration: %lf",
                   mInfo.mAudio.mDuration.ToSeconds());
      }
      mInfo.mAudio.mCrypto = GetTrackCrypto(TrackInfo::kAudioTrack, track);
      if (mInfo.mAudio.mCrypto.IsEncrypted()) {
        MOZ_ASSERT(mInfo.mAudio.mCrypto.mCryptoScheme == CryptoScheme::Cenc,
                   "WebM should only use cenc scheme");
        mCrypto.AddInitData(u"webm"_ns, mInfo.mAudio.mCrypto.mKeyId);
      }
    }
  }

  if (!mHasVideo && !mHasAudio) {
    WEBM_DEBUG("No supported track!");
    return NS_ERROR_DOM_MEDIA_METADATA_ERR;
  }
  WEBM_DEBUG("Read metadata OK");
  return NS_OK;
}

bool WebMDemuxer::IsSeekable() const {
  return Context(TrackInfo::kVideoTrack) &&
         nestegg_has_cues(Context(TrackInfo::kVideoTrack));
}

bool WebMDemuxer::IsSeekableOnlyInBufferedRanges() const {
  return Context(TrackInfo::kVideoTrack) &&
         !nestegg_has_cues(Context(TrackInfo::kVideoTrack));
}

void WebMDemuxer::EnsureUpToDateIndex() {
  if (!mNeedReIndex || !mInitData) {
    return;
  }
  AutoPinned<MediaResource> resource(
      Resource(TrackInfo::kVideoTrack).GetResource());
  MediaByteRangeSet byteRanges;
  nsresult rv = resource->GetCachedRanges(byteRanges);
  if (NS_FAILED(rv) || byteRanges.IsEmpty()) {
    return;
  }
  mBufferedState->UpdateIndex(byteRanges, resource);

  mNeedReIndex = false;
}

void WebMDemuxer::NotifyDataArrived() {
  WEBM_DEBUG("");
  mNeedReIndex = true;
}

void WebMDemuxer::NotifyDataRemoved() {
  mBufferedState->Reset();
  if (mInitData) {
    mBufferedState->NotifyDataArrived(mInitData->Elements(),
                                      mInitData->Length(), 0);
  }
  mNeedReIndex = true;
}

UniquePtr<EncryptionInfo> WebMDemuxer::GetCrypto() {
  return mCrypto.IsEncrypted() ? MakeUnique<EncryptionInfo>(mCrypto) : nullptr;
}

CryptoTrack WebMDemuxer::GetTrackCrypto(TrackInfo::TrackType aType,
                                        size_t aTrackNumber) {
  const int WEBM_IV_SIZE = 16;
  const unsigned char* contentEncKeyId;
  size_t contentEncKeyIdLength;
  CryptoTrack crypto;
  nestegg* context = Context(aType);

  int r = nestegg_track_content_enc_key_id(
      context, aTrackNumber, &contentEncKeyId, &contentEncKeyIdLength);

  if (r == -1) {
    WEBM_DEBUG("nestegg_track_content_enc_key_id failed r=%d", r);
    return crypto;
  }

  uint32_t i;
  nsTArray<uint8_t> initData;
  for (i = 0; i < contentEncKeyIdLength; i++) {
    initData.AppendElement(contentEncKeyId[i]);
  }

  if (!initData.IsEmpty()) {
    // Webm only uses a cenc style scheme.
    crypto.mCryptoScheme = CryptoScheme::Cenc;
    crypto.mIVSize = WEBM_IV_SIZE;
    crypto.mKeyId = std::move(initData);
  }

  return crypto;
}

bool WebMDemuxer::CheckKeyFrameByExamineByteStream(
    const MediaRawData* aSample) {
  switch (mVideoCodec) {
    case NESTEGG_CODEC_VP8:
      return VPXDecoder::IsKeyframe(*aSample, VPXDecoder::Codec::VP8);
    case NESTEGG_CODEC_VP9:
      return VPXDecoder::IsKeyframe(*aSample, VPXDecoder::Codec::VP9);
    case NESTEGG_CODEC_AV1:
      return AOMDecoder::IsKeyframe(*aSample);
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Cannot detect keyframes in unknown WebM video codec");
      return false;
  }
}

nsresult WebMDemuxer::GetNextPacket(TrackInfo::TrackType aType,
                                    MediaRawDataQueue* aSamples) {
  auto result = NextPacket(aType);
  if (result.isErr()) {
    return result.unwrapErr();
  }
  RefPtr<NesteggPacketHolder> holder = result.unwrap();

  int r = 0;
  unsigned int count = 0;
  r = nestegg_packet_count(holder->Packet(), &count);
  if (r == -1) {
    WEBM_DEBUG("nestegg_packet_count: error");
    return NS_ERROR_DOM_MEDIA_DEMUXER_ERR;
  }
  int64_t tstamp = holder->Timestamp();
  int64_t duration = holder->Duration();
  if (aType == TrackInfo::TrackType::kVideoTrack) {
    WEBM_DEBUG("video: tstamp=%" PRId64 ", duration=%" PRId64
               ", mVideoDefaultDuration=%" PRId64,
               tstamp, duration, mVideoDefaultDuration);
  }

  // The end time of this frame is the start time of the next frame.
  // Attempt to fetch the timestamp of the next packet for this track.
  result = NextPacket(aType);
  if (result.isErr()) {
    nsresult rv = result.inspectErr();
    if (rv != NS_ERROR_DOM_MEDIA_END_OF_STREAM &&
        // Gecko has historically estimated a duration for the last frame
        // available in a SourceBuffer, if possible, even though this might
        // result in a different frame duration from that which would be
        // calculated if the frame were not parsed until the next frame
        // becomes available.
        rv != NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
      WEBM_DEBUG("NextPacket: error");
      return rv;
    }
  }
  RefPtr<NesteggPacketHolder> next_holder = result.unwrapOr(nullptr);

  int64_t next_tstamp = INT64_MIN;
  auto calculateNextTimestamp = [&](auto pushPacket,
                                    Maybe<int64_t>* lastFrameTime,
                                    int64_t defaultDuration,
                                    int64_t trackEndTime) {
    MOZ_ASSERT(lastFrameTime);
    if (next_holder) {
      next_tstamp = next_holder->Timestamp();
      (this->*pushPacket)(next_holder);
    } else if (duration >= 0) {
      next_tstamp = tstamp + duration;
    } else if (defaultDuration >= 0) {
      next_tstamp = tstamp + defaultDuration;
    } else if (lastFrameTime->isSome()) {
      // This is a poor estimate, and overestimation overlaps the subsequent
      // block, which can cause cause removal of subsequent frames from
      // MediaSource buffers.
      next_tstamp = tstamp + (tstamp - lastFrameTime->value());
    } else if (mVideoFrameEndTimeBeforeReset) {
      WEBM_DEBUG("Setting next timestamp to be %" PRId64 " us",
                 mVideoFrameEndTimeBeforeReset->ToMicroseconds());
      next_tstamp = mVideoFrameEndTimeBeforeReset->ToMicroseconds();
    } else if (mIsMediaSource) {
      (this->*pushPacket)(holder);
    } else {
      // If we can't get frame's duration, it means either we need to wait for
      // more data for MSE case or this is the last frame for file resource
      // case.
      if (tstamp > trackEndTime) {
        // This shouldn't happen, but some muxers give incorrect durations to
        // segments, then have samples appear beyond those durations.
        WEBM_DEBUG("Found tstamp=%" PRIi64 " > trackEndTime=%" PRIi64
                   " while calculating next timestamp! Indicates a bad mux! "
                   "Will use tstamp value.",
                   tstamp, trackEndTime);
      }
      next_tstamp = std::max<int64_t>(tstamp, trackEndTime);
    }
    *lastFrameTime = Some(tstamp);
  };

  if (aType == TrackInfo::kAudioTrack) {
    calculateNextTimestamp(&WebMDemuxer::PushAudioPacket, &mLastAudioFrameTime,
                           mAudioDefaultDuration,
                           mInfo.mAudio.mDuration.ToMicroseconds());
  } else {
    WEBM_DEBUG("next_holder %c mLastVideoFrameTime %c", next_holder ? 'Y' : 'N',
               mLastVideoFrameTime ? 'Y' : 'N');
    calculateNextTimestamp(&WebMDemuxer::PushVideoPacket, &mLastVideoFrameTime,
                           mVideoDefaultDuration,
                           mInfo.mVideo.mDuration.ToMicroseconds());
  }

  if (mIsMediaSource && next_tstamp == INT64_MIN) {
    WEBM_DEBUG("WebM is a media source, and next timestamp computation filed.");
    return result.unwrapErr();
  }

  int64_t discardPadding = 0;
  if (aType == TrackInfo::kAudioTrack) {
    (void)nestegg_packet_discard_padding(holder->Packet(), &discardPadding);
  }

  int packetEncryption = nestegg_packet_encryption(holder->Packet());

  for (uint32_t i = 0; i < count; ++i) {
    unsigned char* data = nullptr;
    size_t length;
    r = nestegg_packet_data(holder->Packet(), i, &data, &length);
    if (r == -1) {
      WEBM_DEBUG("nestegg_packet_data failed r=%d", r);
      return NS_ERROR_DOM_MEDIA_DEMUXER_ERR;
    }
    unsigned char* alphaData = nullptr;
    size_t alphaLength = 0;
    // Check packets for alpha information if file has declared alpha frames
    // may be present.
    if (mInfo.mVideo.HasAlpha()) {
      r = nestegg_packet_additional_data(holder->Packet(), 1, &alphaData,
                                         &alphaLength);
      if (r == -1) {
        WEBM_DEBUG(
            "nestegg_packet_additional_data failed to retrieve alpha data r=%d",
            r);
      }
    }

    RefPtr<MediaRawData> sample;
    if (mInfo.mVideo.HasAlpha() && alphaData) {
      sample = new MediaRawData(data, length, alphaData, alphaLength);
      if ((length && !sample->Data()) ||
          (alphaLength && !sample->AlphaData())) {
        WEBM_DEBUG("Couldn't allocate MediaRawData: OOM");
        return NS_ERROR_OUT_OF_MEMORY;
      }
    } else {
      sample = new MediaRawData(data, length);
      if (length && !sample->Data()) {
        WEBM_DEBUG("Couldn't allocate MediaRawData: OOM");
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
    sample->mTimecode = TimeUnit::FromMicroseconds(tstamp);
    sample->mTime = TimeUnit::FromMicroseconds(tstamp);
    if (next_tstamp > tstamp) {
      sample->mDuration = TimeUnit::FromMicroseconds(next_tstamp - tstamp);
    } else {
      WEBM_DEBUG("tstamp >= next_tstamp");
    }
    sample->mOffset = holder->Offset();
    // Determine if the sample should be a key frame
    if (aType == TrackInfo::kAudioTrack) {
      sample->mKeyframe = true;
    } else {
      sample->mExtraData = mInfo.mVideo.mExtraData;
      if (packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_ENCRYPTED ||
          packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_PARTITIONED) {
        // Packet is encrypted, can't peek, use packet info
        sample->mKeyframe = nestegg_packet_has_keyframe(holder->Packet()) ==
                            NESTEGG_PACKET_HAS_KEYFRAME_TRUE;
      } else {
        MOZ_ASSERT(
            packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_UNENCRYPTED ||
                packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_FALSE,
            "Unencrypted packet expected");
        sample->mKeyframe = CheckKeyFrameByExamineByteStream(sample);
      }
    }
    WEBM_DEBUG("push sample tstamp: %" PRId64 " next_tstamp: %" PRId64
               " length: %zu kf: %d",
               tstamp, next_tstamp, length, sample->mKeyframe);

    if (discardPadding && i == count - 1) {
      sample->mOriginalPresentationWindow =
          Some(media::TimeInterval{sample->mTime, sample->GetEndTime()});
      if (discardPadding < 0) {
        // This will ensure decoding will error out, and the file is rejected.
        sample->mDuration = TimeUnit::Invalid();
      } else {
        TimeUnit padding = TimeUnit::FromNanoseconds(discardPadding);
        const int samples = opus_packet_get_nb_samples(
            sample->Data(), AssertedCast<int32_t>(sample->Size()),
            AssertedCast<int32_t>(mInfo.mAudio.mRate));
        if (samples <= 0) {
          WEBM_DEBUG(
              "Invalid number of samples, flagging packet for error (padding: "
              "%s, samples: %d, already processed: %s, error: %s)",
              padding.ToString().get(), samples,
              mProcessedDiscardPadding ? "true" : "false",
              (samples == OPUS_BAD_ARG)          ? "OPUS_BAD_ARG"
              : (samples == OPUS_INVALID_PACKET) ? "OPUS_INVALID_PACKET"
                                                 : "Undefined Error");
          sample->mDuration = TimeUnit::Invalid();
        } else {
          TimeUnit packetDuration = TimeUnit(samples, mInfo.mAudio.mRate);
          if (padding > packetDuration || mProcessedDiscardPadding) {
            WEBM_DEBUG(
                "Padding frames larger than packet size, flagging packet for "
                "error (padding: %s, duration: %s, already processed: %s)",
                padding.ToString().get(), packetDuration.ToString().get(),
                mProcessedDiscardPadding ? "true" : "false");
            sample->mDuration = TimeUnit::Invalid();
          } else {
            sample->mDuration = packetDuration - padding;
          }
        }
      }
      mProcessedDiscardPadding = true;
    }

    if (packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_ENCRYPTED ||
        packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_PARTITIONED) {
      UniquePtr<MediaRawDataWriter> writer(sample->CreateWriter());
      unsigned char const* iv;
      size_t ivLength;
      nestegg_packet_iv(holder->Packet(), &iv, &ivLength);
      writer->mCrypto.mCryptoScheme = CryptoScheme::Cenc;
      writer->mCrypto.mIVSize = ivLength;
      if (ivLength == 0) {
        // Frame is not encrypted. This shouldn't happen as it means the
        // encryption bit is set on a frame with no IV, but we gracefully
        // handle incase.
        MOZ_ASSERT_UNREACHABLE(
            "Unencrypted packets should not have the encryption bit set!");
        WEBM_DEBUG("Unencrypted packet with encryption bit set");
        writer->mCrypto.mPlainSizes.AppendElement(length);
        writer->mCrypto.mEncryptedSizes.AppendElement(0);
      } else {
        // Frame is encrypted
        writer->mCrypto.mIV.AppendElements(iv, 8);
        // Iv from a sample is 64 bits, must be padded with 64 bits more 0s
        // in compliance with spec
        for (uint32_t i = 0; i < 8; i++) {
          writer->mCrypto.mIV.AppendElement(0);
        }

        if (packetEncryption == NESTEGG_PACKET_HAS_SIGNAL_BYTE_ENCRYPTED) {
          writer->mCrypto.mPlainSizes.AppendElement(0);
          writer->mCrypto.mEncryptedSizes.AppendElement(length);
        } else if (packetEncryption ==
                   NESTEGG_PACKET_HAS_SIGNAL_BYTE_PARTITIONED) {
          uint8_t numPartitions = 0;
          const uint32_t* partitions = NULL;
          nestegg_packet_offsets(holder->Packet(), &partitions, &numPartitions);

          // WebM stores a list of 'partitions' in the data, which alternate
          // clear, encrypted. The data in the first partition is always clear.
          // So, and sample might look as follows:
          // 00|XXXX|000|XX, where | represents a partition, 0 a clear byte and
          // X an encrypted byte. If the first bytes in sample are unencrypted,
          // the first partition will be at zero |XXXX|000|XX.
          //
          // As GMP expects the lengths of the clear and encrypted chunks of
          // data, we calculate these from the difference between the last two
          // partitions.
          uint32_t lastOffset = 0;
          bool encrypted = false;

          for (uint8_t i = 0; i < numPartitions; i++) {
            uint32_t partition = partitions[i];
            if (partition > length || partition < lastOffset) {
              WEBM_DEBUG(
                  "Invalid partition offset: %u (length: %zu, lastOffset: %u)",
                  partition, length, lastOffset);
              return NS_ERROR_DOM_MEDIA_DEMUXER_ERR;
            }
            uint32_t currentLength = partition - lastOffset;

            if (encrypted) {
              writer->mCrypto.mEncryptedSizes.AppendElement(currentLength);
            } else {
              writer->mCrypto.mPlainSizes.AppendElement(currentLength);
            }

            encrypted = !encrypted;
            lastOffset = partition;
          }

          // Add the data between the last offset and the end of the data.
          // 000|XXX|000
          //        ^---^
          if (encrypted) {
            writer->mCrypto.mEncryptedSizes.AppendElement(length - lastOffset);
          } else {
            writer->mCrypto.mPlainSizes.AppendElement(length - lastOffset);
          }

          // Make sure we have an equal number of encrypted and plain sizes (GMP
          // expects this). This simple check is sufficient as there are two
          // possible cases at this point:
          // 1. The number of samples are even (so we don't need to do anything)
          // 2. There is one more clear sample than encrypted samples, so add a
          // zero length encrypted chunk.
          // There can never be more encrypted partitions than clear partitions
          // due to the alternating structure of the WebM samples and the
          // restriction that the first chunk is always clear.
          if (numPartitions % 2 == 0) {
            writer->mCrypto.mEncryptedSizes.AppendElement(0);
          }

          // Assert that the lengths of the encrypted and plain samples add to
          // the length of the data.
          MOZ_ASSERT(
              ((size_t)(std::accumulate(writer->mCrypto.mPlainSizes.begin(),
                                        writer->mCrypto.mPlainSizes.end(), 0) +
                        std::accumulate(writer->mCrypto.mEncryptedSizes.begin(),
                                        writer->mCrypto.mEncryptedSizes.end(),
                                        0)) == length));
        }
      }
    }
    aSamples->Push(sample);
  }
  return NS_OK;
}

Result<RefPtr<NesteggPacketHolder>, nsresult> WebMDemuxer::NextPacket(
    TrackInfo::TrackType aType) {
  bool isVideo = aType == TrackInfo::kVideoTrack;

  // Flag to indicate that we do need to playback these types of
  // packets.
  bool hasType = isVideo ? mHasVideo : mHasAudio;

  if (!hasType) {
    WEBM_DEBUG("No media type found");
    return Err(NS_ERROR_DOM_MEDIA_DEMUXER_ERR);
  }

  // The packet queue for the type that we are interested in.
  WebMPacketQueue& packets = isVideo ? mVideoPackets : mAudioPackets;

  if (packets.GetSize() > 0) {
    return packets.PopFront();
  }

  // Track we are interested in
  uint32_t ourTrack = isVideo ? mVideoTrack : mAudioTrack;

  do {
    auto result = DemuxPacket(aType);
    if (result.isErr()) {
      return result.propagateErr();
    }

    RefPtr<NesteggPacketHolder> holder = result.unwrap();
    if (ourTrack == holder->Track()) {
      return holder;
    }
  } while (true);
}

Result<RefPtr<NesteggPacketHolder>, nsresult> WebMDemuxer::DemuxPacket(
    TrackInfo::TrackType aType) {
  nestegg_packet* packet;
  NestEggContext& context =
      aType == TrackInfo::kVideoTrack ? mVideoContext : mAudioContext;
  // Clear any IO result carried over from a prior nestegg operation so that
  // mLastIORV only reflects failures that occur during this packet read.
  context.mLastIORV = NS_OK;
  int r = nestegg_read_packet(context.mContext, &packet);
  if (r <= 0) {
    nsresult rv = context.mLastIORV;
    nestegg_read_reset(context.mContext);
    if (r == 0) {
      WEBM_DEBUG("EOS");
      return Err(NS_ERROR_DOM_MEDIA_END_OF_STREAM);
    } else if (r < 0) {
      WEBM_DEBUG("nestegg_read_packet: error");
      return Err(NS_FAILED(rv) ? rv : NS_ERROR_DOM_MEDIA_DEMUXER_ERR);
    }
  }

  unsigned int track = 0;
  r = nestegg_packet_track(packet, &track);
  if (r == -1) {
    WEBM_DEBUG("nestegg_packet_track: error");
    return Err(NS_ERROR_DOM_MEDIA_DEMUXER_ERR);
  }

  // Use nestegg's logical end-of-packet offset rather than the resource
  // cursor: nestegg's internal buffering means the cursor may be ahead of
  // the packet's actual end in the stream.
  int64_t offset = 0;
  if (nestegg_packet_end_offset(packet, &offset) != 0) {
    WEBM_DEBUG("nestegg_packet_end_offset: error");
    return Err(NS_ERROR_DOM_MEDIA_DEMUXER_ERR);
  }
  RefPtr<NesteggPacketHolder> holder = new NesteggPacketHolder();
  if (!holder->Init(packet, offset, track, false)) {
    WEBM_DEBUG("NesteggPacketHolder::Init: error");
    return Err(NS_ERROR_DOM_MEDIA_DEMUXER_ERR);
  }

  return holder;
}

void WebMDemuxer::PushAudioPacket(NesteggPacketHolder* aItem) {
  mAudioPackets.PushFront(aItem);
}

void WebMDemuxer::PushVideoPacket(NesteggPacketHolder* aItem) {
  mVideoPackets.PushFront(aItem);
}

nsresult WebMDemuxer::SeekInternal(TrackInfo::TrackType aType,
                                   const TimeUnit& aTarget) {
  EnsureUpToDateIndex();
  uint32_t trackToSeek = mHasVideo ? mVideoTrack : mAudioTrack;
  MOZ_ASSERT(aTarget.ToNanoseconds() >= 0, "Seek time can't be negative");
  uint64_t target = static_cast<uint64_t>(aTarget.ToNanoseconds());
  WEBM_DEBUG("Seeking to %lf", aTarget.ToSeconds());

  Reset(aType);

  if (mSeekPreroll) {
    uint64_t startTime = 0;
    if (!mBufferedState->GetStartTime(&startTime)) {
      startTime = 0;
    }
    WEBM_DEBUG("Seek Target: %f",
               TimeUnit::FromNanoseconds(target).ToSeconds());
    if (target < mSeekPreroll || target - mSeekPreroll < startTime) {
      target = startTime;
    } else {
      target -= mSeekPreroll;
    }
    WEBM_DEBUG("SeekPreroll: %f StartTime: %f Adjusted Target: %f",
               TimeUnit::FromNanoseconds(mSeekPreroll).ToSeconds(),
               TimeUnit::FromNanoseconds(startTime).ToSeconds(),
               TimeUnit::FromNanoseconds(target).ToSeconds());
  }
  int r = nestegg_track_seek(Context(aType), trackToSeek, target);
  if (r == -1) {
    WEBM_DEBUG("track_seek for track %u to %f failed, r=%d", trackToSeek,
               TimeUnit::FromNanoseconds(target).ToSeconds(), r);
    // Try seeking directly based on cluster information in memory.
    int64_t offset = 0;
    bool rv = mBufferedState->GetOffsetForTime(target, &offset);
    if (!rv) {
      WEBM_DEBUG("mBufferedState->GetOffsetForTime failed too");
      return NS_ERROR_FAILURE;
    }

    if (offset < 0) {
      WEBM_DEBUG("Unknow byte offset time for seek target %" PRIu64 "ns",
                 target);
      return NS_ERROR_FAILURE;
    }

    r = nestegg_offset_seek(Context(aType), static_cast<uint64_t>(offset));
    if (r == -1) {
      WEBM_DEBUG("and nestegg_offset_seek to %" PRIu64 " failed", offset);
      return NS_ERROR_FAILURE;
    }
    WEBM_DEBUG("got offset from buffered state: %" PRIu64 "", offset);
  }

  if (aType == TrackInfo::kAudioTrack) {
    mLastAudioFrameTime.reset();
  } else {
    mLastVideoFrameTime.reset();
  }

  return NS_OK;
}

bool WebMDemuxer::IsBufferedIntervalValid(uint64_t start, uint64_t end) {
  if (start > end) {
    // Buffered ranges are clamped to the media's start time and duration. Any
    // frames with timestamps outside that range are ignored, see bug 1697641
    // for more info.
    WEBM_DEBUG("Ignoring range %" PRIu64 "-%" PRIu64
               ", due to invalid interval (start > end).",
               start, end);
    return false;
  }

  auto startTime = TimeUnit::FromNanoseconds(start);
  auto endTime = TimeUnit::FromNanoseconds(end);

  if (startTime.IsNegative() || endTime.IsNegative()) {
    // We can get timestamps that are conceptually valid, but become
    // negative due to uint64 -> int64 conversion from TimeUnit. We should
    // not get negative timestamps, so guard against them.
    WEBM_DEBUG(
        "Invalid range %f-%f, likely result of uint64 -> int64 conversion.",
        startTime.ToSeconds(), endTime.ToSeconds());
    return false;
  }

  return true;
}

media::TimeIntervals WebMDemuxer::GetBuffered() {
  EnsureUpToDateIndex();
  AutoPinned<MediaResource> resource(
      Resource(TrackInfo::kVideoTrack).GetResource());

  media::TimeIntervals buffered;

  MediaByteRangeSet ranges;
  nsresult rv = resource->GetCachedRanges(ranges);
  if (NS_FAILED(rv)) {
    return media::TimeIntervals();
  }
  uint64_t duration = 0;
  uint64_t startOffset = 0;
  if (!nestegg_duration(Context(TrackInfo::kVideoTrack), &duration)) {
    if (mBufferedState->GetStartTime(&startOffset)) {
      duration += startOffset;
    }
    WEBM_DEBUG("Duration: %f StartTime: %f",
               TimeUnit::FromNanoseconds(duration).ToSeconds(),
               TimeUnit::FromNanoseconds(startOffset).ToSeconds());
  }
  for (uint32_t index = 0; index < ranges.Length(); index++) {
    uint64_t start, end;
    bool rv = mBufferedState->CalculateBufferedForRange(
        ranges[index].mStart, ranges[index].mEnd, &start, &end);
    if (rv) {
      NS_ASSERTION(startOffset <= start,
                   "startOffset negative or larger than start time");

      if (duration && end > duration) {
        WEBM_DEBUG("limit range to duration, end: %f duration: %f",
                   TimeUnit::FromNanoseconds(end).ToSeconds(),
                   TimeUnit::FromNanoseconds(duration).ToSeconds());
        end = duration;
      }

      if (!IsBufferedIntervalValid(start, end)) {
        WEBM_DEBUG("Invalid interval, bailing");
        break;
      }

      auto startTime = TimeUnit::FromNanoseconds(start);
      auto endTime = TimeUnit::FromNanoseconds(end);

      WEBM_DEBUG("add range %f-%f", startTime.ToSeconds(), endTime.ToSeconds());
      buffered += media::TimeInterval(startTime, endTime);
    }
  }
  return buffered;
}

bool WebMDemuxer::GetOffsetForTime(uint64_t aTime, int64_t* aOffset) {
  EnsureUpToDateIndex();
  return mBufferedState && mBufferedState->GetOffsetForTime(aTime, aOffset);
}

// WebMTrackDemuxer
WebMTrackDemuxer::WebMTrackDemuxer(WebMDemuxer* aParent,
                                   TrackInfo::TrackType aType,
                                   uint32_t aTrackNumber)
    : mParent(aParent), mType(aType), mNeedKeyframe(true) {
  mInfo = mParent->GetTrackInfo(aType, aTrackNumber);
  MOZ_ASSERT(mInfo);
}

WebMTrackDemuxer::~WebMTrackDemuxer() { mSamples.Reset(); }

UniquePtr<TrackInfo> WebMTrackDemuxer::GetInfo() const {
  return mInfo->Clone();
}

RefPtr<WebMTrackDemuxer::SeekPromise> WebMTrackDemuxer::Seek(
    const TimeUnit& aTime) {
  // Seeks to aTime. Upon success, SeekPromise will be resolved with the
  // actual time seeked to. Typically the random access point time

  auto seekTime = aTime;
  bool keyframe = false;

  mNeedKeyframe = true;

  do {
    mSamples.Reset();
    mParent->SeekInternal(mType, seekTime);
    nsresult rv = mParent->GetNextPacket(mType, &mSamples);
    if (NS_FAILED(rv)) {
      if (rv == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
        // Ignore the error for now, the next GetSample will be rejected with
        // EOS.
        return SeekPromise::CreateAndResolve(TimeUnit::Zero(), __func__);
      }
      return SeekPromise::CreateAndReject(rv, __func__);
    }

    // Check what time we actually seeked to.
    if (mSamples.GetSize() == 0) {
      // We can't determine if the seek succeeded at this stage, so break the
      // loop.
      break;
    }

    for (const auto& sample : mSamples) {
      seekTime = sample->mTime;
      keyframe = sample->mKeyframe;
      if (keyframe) {
        break;
      }
    }
    if (mType == TrackInfo::kVideoTrack &&
        !mInfo->GetAsVideoInfo()->HasAlpha()) {
      // We only perform a search for a keyframe on videos with alpha layer to
      // prevent potential regression for normal video (even though invalid)
      break;
    }
    if (!keyframe) {
      // We didn't find any keyframe, attempt to seek to the previous cluster.
      seekTime = mSamples.First()->mTime - TimeUnit::FromMicroseconds(1);
    }
  } while (!keyframe && seekTime >= TimeUnit::Zero());

  SetNextKeyFrameTime();

  return SeekPromise::CreateAndResolve(seekTime, __func__);
}

nsresult WebMTrackDemuxer::NextSample(RefPtr<MediaRawData>& aData) {
  nsresult rv = NS_ERROR_DOM_MEDIA_END_OF_STREAM;
  while (mSamples.GetSize() < 1 &&
         NS_SUCCEEDED((rv = mParent->GetNextPacket(mType, &mSamples)))) {
  }
  if (mSamples.GetSize()) {
    aData = mSamples.PopFront();
    return NS_OK;
  }
  WEBM_DEBUG("WebMTrackDemuxer::NextSample: error");
  return rv;
}

RefPtr<WebMTrackDemuxer::SamplesPromise> WebMTrackDemuxer::GetSamples(
    int32_t aNumSamples) {
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  MOZ_ASSERT(aNumSamples);

  while (aNumSamples) {
    RefPtr<MediaRawData> sample;
    nsresult rv = NextSample(sample);
    if (NS_FAILED(rv)) {
      if ((rv != NS_ERROR_DOM_MEDIA_END_OF_STREAM &&
           rv != NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) ||
          samples->GetSamples().IsEmpty()) {
        return SamplesPromise::CreateAndReject(rv, __func__);
      }
      break;
    }
    // Ignore empty samples.
    if (sample->Size() == 0) {
      WEBM_DEBUG(
          "0 sized sample encountered while getting samples, skipping it");
      continue;
    }
    if (mNeedKeyframe && !sample->mKeyframe) {
      continue;
    }
    if (!sample->HasValidTime()) {
      return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                             __func__);
    }
    mNeedKeyframe = false;
    samples->AppendSample(std::move(sample));
    aNumSamples--;
  }

  UpdateSamples(samples->GetSamples());
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

void WebMTrackDemuxer::SetNextKeyFrameTime() {
  if (mType != TrackInfo::kVideoTrack || mParent->IsMediaSource()) {
    return;
  }

  auto frameTime = TimeUnit::Invalid();

  mNextKeyframeTime.reset();

  MediaRawDataQueue skipSamplesQueue;
  bool foundKeyframe = false;
  while (!foundKeyframe && mSamples.GetSize()) {
    RefPtr<MediaRawData> sample = mSamples.PopFront();
    if (sample->mKeyframe) {
      frameTime = sample->mTime;
      foundKeyframe = true;
    }
    skipSamplesQueue.Push(sample.forget());
  }
  Maybe<int64_t> startTime;
  if (skipSamplesQueue.GetSize()) {
    const RefPtr<MediaRawData>& sample = skipSamplesQueue.First();
    startTime.emplace(sample->mTimecode.ToMicroseconds());
  }
  // Demux and buffer frames until we find a keyframe.
  RefPtr<MediaRawData> sample;
  nsresult rv = NS_OK;
  while (!foundKeyframe && NS_SUCCEEDED((rv = NextSample(sample)))) {
    if (sample->mKeyframe) {
      frameTime = sample->mTime;
      foundKeyframe = true;
    }
    int64_t sampleTimecode = sample->mTimecode.ToMicroseconds();
    skipSamplesQueue.Push(sample.forget());
    if (!startTime) {
      startTime.emplace(sampleTimecode);
    } else if (!foundKeyframe &&
               sampleTimecode > startTime.ref() + MAX_LOOK_AHEAD) {
      WEBM_DEBUG("Couldn't find keyframe in a reasonable time, aborting");
      break;
    }
  }
  // We may have demuxed more than intended, so ensure that all frames are kept
  // in the right order.
  mSamples.PushFront(std::move(skipSamplesQueue));

  if (frameTime.IsValid()) {
    mNextKeyframeTime.emplace(frameTime);
    WEBM_DEBUG(
        "Next Keyframe %f (%u queued %.02fs)",
        mNextKeyframeTime.value().ToSeconds(), uint32_t(mSamples.GetSize()),
        (mSamples.Last()->mTimecode - mSamples.First()->mTimecode).ToSeconds());
  } else {
    WEBM_DEBUG("Couldn't determine next keyframe time  (%u queued)",
               uint32_t(mSamples.GetSize()));
  }
}

void WebMTrackDemuxer::Reset() {
  mSamples.Reset();
  media::TimeIntervals buffered = GetBuffered();
  mNeedKeyframe = true;
  if (!buffered.IsEmpty()) {
    WEBM_DEBUG("Seek to start point: %f", buffered.Start(0).ToSeconds());
    mParent->SeekInternal(mType, buffered.Start(0));
    SetNextKeyFrameTime();
  } else {
    mNextKeyframeTime.reset();
  }
}

void WebMTrackDemuxer::UpdateSamples(
    const nsTArray<RefPtr<MediaRawData>>& aSamples) {
  for (const auto& sample : aSamples) {
    if (sample->mCrypto.IsEncrypted()) {
      UniquePtr<MediaRawDataWriter> writer(sample->CreateWriter());
      writer->mCrypto.mIVSize = mInfo->mCrypto.mIVSize;
      writer->mCrypto.mKeyId.AppendElements(mInfo->mCrypto.mKeyId);
    }
  }
  if (mNextKeyframeTime.isNothing() ||
      aSamples.LastElement()->mTime >= mNextKeyframeTime.value()) {
    SetNextKeyFrameTime();
  }
}

nsresult WebMTrackDemuxer::GetNextRandomAccessPoint(TimeUnit* aTime) {
  if (mNextKeyframeTime.isNothing()) {
    // There's no next key frame.
    *aTime = TimeUnit::FromInfinity();
  } else {
    *aTime = mNextKeyframeTime.ref();
  }
  return NS_OK;
}

RefPtr<WebMTrackDemuxer::SkipAccessPointPromise>
WebMTrackDemuxer::SkipToNextRandomAccessPoint(const TimeUnit& aTimeThreshold) {
  uint32_t parsed = 0;
  bool found = false;
  RefPtr<MediaRawData> sample;
  nsresult rv = NS_OK;

  WEBM_DEBUG("TimeThreshold: %f", aTimeThreshold.ToSeconds());
  while (!found && NS_SUCCEEDED((rv = NextSample(sample)))) {
    parsed++;
    if (sample->mKeyframe && sample->mTime >= aTimeThreshold) {
      WEBM_DEBUG("next sample: %f (parsed: %d)", sample->mTime.ToSeconds(),
                 parsed);
      found = true;
      mSamples.Reset();
      mSamples.PushFront(sample.forget());
    }
  }
  if (NS_SUCCEEDED(rv)) {
    SetNextKeyFrameTime();
  }
  if (found) {
    return SkipAccessPointPromise::CreateAndResolve(parsed, __func__);
  } else {
    SkipFailureHolder failure(NS_ERROR_DOM_MEDIA_END_OF_STREAM, parsed);
    return SkipAccessPointPromise::CreateAndReject(std::move(failure),
                                                   __func__);
  }
}

media::TimeIntervals WebMTrackDemuxer::GetBuffered() {
  return mParent->GetBuffered();
}

void WebMTrackDemuxer::BreakCycles() { mParent = nullptr; }

int64_t WebMTrackDemuxer::GetEvictionOffset(const TimeUnit& aTime) {
  int64_t offset;
  int64_t nanos = aTime.ToNanoseconds();
  if (nanos < 0 ||
      !mParent->GetOffsetForTime(static_cast<uint64_t>(nanos), &offset)) {
    return 0;
  }

  return offset;
}
}  // namespace mozilla

#undef WEBM_DEBUG
