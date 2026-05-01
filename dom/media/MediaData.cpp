/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaData.h"

#include <stdint.h>

#include <functional>

#include "ImageContainer.h"
#include "MediaInfo.h"
#include "MediaResult.h"
#include "PerformanceRecorder.h"
#include "PlatformDecoderModule.h"
#include "VideoUtils.h"
#include "YCbCrUtils.h"
#include "libyuv.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/SharedRGBImage.h"

#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/layers/D3D11ShareHandleImage.h"
#  include "mozilla/layers/D3D11YCbCrImage.h"
#elif XP_MACOSX
#  include "MacIOSurfaceImage.h"
#  include "mozilla/gfx/gfxVars.h"
#endif

namespace mozilla {

extern LazyLogModule sPDMLog;

using namespace mozilla::gfx;
using layers::BufferRecycleBin;
using layers::PlanarYCbCrData;
using layers::PlanarYCbCrImage;
using media::TimeUnit;

const char* AudioData::sTypeName = "audio";
const char* VideoData::sTypeName = "video";

AudioData::AudioData(int64_t aOffset, const media::TimeUnit& aTime,
                     AlignedAudioBuffer&& aData, uint32_t aChannels,
                     uint32_t aRate, uint32_t aChannelMap)
    // Passing TimeUnit::Zero() here because we can't pass the result of an
    // arithmetic operation to the CheckedInt ctor. We set the duration in the
    // ctor body below.
    : MediaData(sType, aOffset, aTime, TimeUnit::Zero()),
      mChannels(aChannels),
      mChannelMap(aChannelMap),
      mRate(aRate),
      mOriginalTime(aTime),
      mAudioData(std::move(aData)),
      mFrames(mAudioData.Length() / aChannels) {
  MOZ_RELEASE_ASSERT(aChannels != 0,
                     "Can't create an AudioData with 0 channels.");
  MOZ_RELEASE_ASSERT(aRate != 0,
                     "Can't create an AudioData with a sample-rate of 0.");
  mDuration = TimeUnit(mFrames, aRate);
}

Span<AudioDataValue> AudioData::Data() const {
  return Span{GetAdjustedData(), mFrames * mChannels};
}

nsCString AudioData::ToString() const {
  nsCString rv;
  rv.AppendPrintf("AudioData: %s %s %" PRIu32 " frames %" PRIu32 "Hz, %" PRIu32
                  "ch",
                  mTime.ToString().get(), mDuration.ToString().get(), mFrames,
                  mRate, mChannels);
  return rv;
}

void AudioData::SetOriginalStartTime(const media::TimeUnit& aStartTime) {
  MOZ_ASSERT(mTime == mOriginalTime,
             "Do not call this if data has been trimmed!");
  mTime = aStartTime;
  mOriginalTime = aStartTime;
}

bool AudioData::AdjustForStartTime(const media::TimeUnit& aStartTime) {
  mOriginalTime -= aStartTime;
  mTime -= aStartTime;
  if (mTrimWindow) {
    *mTrimWindow -= aStartTime;
  }
  if (mTime.IsNegative()) {
    NS_WARNING("Negative audio start time after time-adjustment!");
  }
  return mTime.IsValid() && mOriginalTime.IsValid();
}

bool AudioData::SetTrimWindow(const media::TimeInterval& aTrim) {
  MOZ_DIAGNOSTIC_ASSERT(aTrim.mStart.IsValid() && aTrim.mEnd.IsValid(),
                        "An overflow occurred on the provided TimeInterval");
  if (!mAudioData) {
    // MoveableData got called. Can no longer work on it.
    return false;
  }
  if (aTrim.mStart < mOriginalTime || aTrim.mEnd > GetEndTime()) {
    return false;
  }

  auto trimBefore = aTrim.mStart - mOriginalTime;
  auto trimAfter = aTrim.mEnd - mOriginalTime;
  if (!trimBefore.IsValid() || !trimAfter.IsValid()) {
    // Overflow.
    return false;
  }
  if (!mTrimWindow && trimBefore.IsZero() && trimAfter == mDuration) {
    // Nothing to change, abort early to prevent rounding errors.
    return true;
  }

  size_t frameOffset = trimBefore.ToTicksAtRate(mRate);
  size_t dataOffset = frameOffset * mChannels;
  int64_t frameCountAfterTrim = (trimAfter - trimBefore).ToTicksAtRate(mRate);
  if (dataOffset > mAudioData.Length() || frameCountAfterTrim < 0) {
    return false;
  }
  mTrimWindow = Some(aTrim);
  mDataOffset = dataOffset;
  const size_t availFrames = (mAudioData.Length() - mDataOffset) / mChannels;
  if (frameCountAfterTrim > AssertedCast<int64_t>(availFrames)) {
    // Accept rounding error caused by an imprecise time_base in the container,
    // that can cause a mismatch but not other kind of unexpected frame count.
    MOZ_RELEASE_ASSERT(!trimBefore.IsBase(mRate));
    mFrames = 0;
  } else {
    mFrames = frameCountAfterTrim;
  }
  mTime = mOriginalTime + trimBefore;
  mDuration = TimeUnit(mFrames, mRate);

  return true;
}

AudioDataValue* AudioData::GetAdjustedData() const {
  if (!mAudioData) {
    return nullptr;
  }
  return mAudioData.Data() + mDataOffset;
}

void AudioData::EnsureAudioBuffer() {
  if (mAudioBuffer || !mAudioData) {
    return;
  }
  const AudioDataValue* srcData = GetAdjustedData();
  CheckedInt<size_t> bufferSize(sizeof(AudioDataValue));
  bufferSize *= mFrames;
  bufferSize *= mChannels;
  mAudioBuffer = SharedBuffer::Create(bufferSize);

  AudioDataValue* destData = static_cast<AudioDataValue*>(mAudioBuffer->Data());
  for (uint32_t i = 0; i < mFrames; ++i) {
    for (uint32_t j = 0; j < mChannels; ++j) {
      destData[j * mFrames + i] = srcData[i * mChannels + j];
    }
  }
}

size_t AudioData::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t size =
      aMallocSizeOf(this) + mAudioData.SizeOfExcludingThis(aMallocSizeOf);
  if (mAudioBuffer) {
    size += mAudioBuffer->SizeOfIncludingThis(aMallocSizeOf);
  }
  return size;
}

AlignedAudioBuffer AudioData::MoveableData() {
  // Trim buffer according to trimming mask.
  mAudioData.PopFront(mDataOffset);
  mAudioData.SetLength(mFrames * mChannels);
  mDataOffset = 0;
  mFrames = 0;
  mTrimWindow.reset();
  return std::move(mAudioData);
}

static bool ValidatePlane(const VideoData::YCbCrBuffer::Plane& aPlane) {
  return aPlane.mWidth <= PlanarYCbCrImage::MAX_DIMENSION &&
         aPlane.mHeight <= PlanarYCbCrImage::MAX_DIMENSION &&
         aPlane.mWidth * aPlane.mHeight < MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT &&
         aPlane.mStride > 0 && aPlane.mWidth <= aPlane.mStride;
}

static MediaResult ValidateBufferAndPicture(
    const VideoData::YCbCrBuffer& aBuffer, const IntRect& aPicture) {
  // mChromaSubsampling describes the relationship between plane sizes.
  if (aBuffer.mChromaSubsampling == ChromaSubsampling::FULL) {
    MOZ_ASSERT(aBuffer.mPlanes[1].mWidth == aBuffer.mPlanes[0].mWidth);
  } else {
    MOZ_ASSERT(aBuffer.mPlanes[1].mWidth ==
               (aBuffer.mPlanes[0].mWidth + 1) / 2);
  }
  if (aBuffer.mChromaSubsampling == ChromaSubsampling::HALF_WIDTH_AND_HEIGHT) {
    MOZ_ASSERT(aBuffer.mPlanes[1].mHeight ==
               (aBuffer.mPlanes[0].mHeight + 1) / 2);
  } else {
    MOZ_ASSERT(aBuffer.mPlanes[1].mHeight == aBuffer.mPlanes[0].mHeight);
  }
  MOZ_ASSERT(aBuffer.mPlanes[1].mWidth == aBuffer.mPlanes[2].mWidth);
  MOZ_ASSERT(aBuffer.mPlanes[1].mHeight == aBuffer.mPlanes[2].mHeight);
  // The following situations could be triggered by invalid input
  if (aPicture.width <= 0 || aPicture.height <= 0) {
    return MediaResult::Logged(NS_ERROR_INVALID_ARG,
                               RESULT_DETAIL("Empty picture rect"), sPDMLog);
  }
  if (!ValidatePlane(aBuffer.mPlanes[0]) ||
      !ValidatePlane(aBuffer.mPlanes[1]) ||
      !ValidatePlane(aBuffer.mPlanes[2])) {
    return MediaResult::Logged(NS_ERROR_INVALID_ARG,
                               RESULT_DETAIL("Invalid plane size"), sPDMLog);
  }
  // ConstructPlanarYCbCrData() and ConvertI420AlphaToARGB() assume Chroma
  // planes have equal strides.
  if (aBuffer.mPlanes[1].mStride != aBuffer.mPlanes[2].mStride) {
    return MediaResult::Logged(
        NS_ERROR_INVALID_ARG,
        RESULT_DETAIL("Chroma planes with different strides"), sPDMLog);
  }
  // Ensure the picture size specified in the headers can be extracted out of
  // the frame we've been supplied without indexing out of bounds.
  CheckedUint32 xLimit = aPicture.x + CheckedUint32(aPicture.width);
  CheckedUint32 yLimit = aPicture.y + CheckedUint32(aPicture.height);
  if (!xLimit.isValid() || xLimit.value() > aBuffer.mPlanes[0].mStride ||
      !yLimit.isValid() || yLimit.value() > aBuffer.mPlanes[0].mHeight) {
    // The specified picture dimensions can't be contained inside the video
    // frame, we'll stomp memory if we try to copy it. Fail.
    return MediaResult::Logged(NS_ERROR_INVALID_ARG,
                               RESULT_DETAIL("Overflowing picture rect"),
                               sPDMLog);
  }
  return MediaResult(NS_OK);
}

VideoData::VideoData(int64_t aOffset, const TimeUnit& aTime,
                     const TimeUnit& aDuration, bool aKeyframe,
                     const TimeUnit& aTimecode, IntSize aDisplay,
                     layers::ImageContainer::FrameID aFrameID)
    : MediaData(Type::VIDEO_DATA, aOffset, aTime, aDuration),
      mDisplay(aDisplay),
      mFrameID(aFrameID),
      mSentToCompositor(false),
      mNextKeyFrameTime(TimeUnit::Invalid()) {
  MOZ_ASSERT(!mDuration.IsNegative(), "Frame must have non-negative duration.");
  mKeyframe = aKeyframe;
  mTimecode = aTimecode;
}

VideoData::~VideoData() = default;

size_t VideoData::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t size = aMallocSizeOf(this);

  // Currently only PLANAR_YCBCR has a well defined function for determining
  // it's size, so reporting is limited to that type.
  if (mImage && mImage->GetFormat() == ImageFormat::PLANAR_YCBCR) {
    const mozilla::layers::PlanarYCbCrImage* img =
        static_cast<const mozilla::layers::PlanarYCbCrImage*>(mImage.get());
    size += img->SizeOfIncludingThis(aMallocSizeOf);
  }

  return size;
}

ColorDepth VideoData::GetColorDepth() const {
  if (!mImage) {
    return ColorDepth::COLOR_8;
  }

  return mImage->GetColorDepth();
}

void VideoData::UpdateDuration(const TimeUnit& aDuration) {
  MOZ_ASSERT(!aDuration.IsNegative());
  mDuration = aDuration;
}

void VideoData::UpdateTimestamp(const TimeUnit& aTimestamp) {
  MOZ_ASSERT(!aTimestamp.IsNegative());

  auto updatedDuration = GetEndTime() - aTimestamp;
  MOZ_ASSERT(!updatedDuration.IsNegative());

  mTime = aTimestamp;
  mDuration = updatedDuration;
}

bool VideoData::AdjustForStartTime(const media::TimeUnit& aStartTime) {
  mTime -= aStartTime;
  if (mTime.IsNegative()) {
    NS_WARNING("Negative video start time after time-adjustment!");
  }
  return mTime.IsValid();
}

PlanarYCbCrData ConstructPlanarYCbCrData(const VideoInfo& aInfo,
                                         const VideoData::YCbCrBuffer& aBuffer,
                                         const IntRect& aPicture) {
  const VideoData::YCbCrBuffer::Plane& Y = aBuffer.mPlanes[0];
  const VideoData::YCbCrBuffer::Plane& Cb = aBuffer.mPlanes[1];
  const VideoData::YCbCrBuffer::Plane& Cr = aBuffer.mPlanes[2];

  PlanarYCbCrData data;
  data.mYChannel = Y.mData;
  data.mYStride = AssertedCast<int32_t>(Y.mStride);
  data.mYSkip = AssertedCast<int32_t>(Y.mSkip);
  data.mCbChannel = Cb.mData;
  data.mCrChannel = Cr.mData;
  MOZ_ASSERT(Cb.mStride == Cr.mStride);
  data.mCbCrStride = AssertedCast<int32_t>(Cb.mStride);
  data.mCbSkip = AssertedCast<int32_t>(Cb.mSkip);
  data.mCrSkip = AssertedCast<int32_t>(Cr.mSkip);
  data.mPictureRect = aPicture;
  data.mStereoMode = aInfo.mStereoMode;
  data.mYUVColorSpace = aBuffer.mYUVColorSpace;
  data.mColorPrimaries = aBuffer.mColorPrimaries;
  data.mColorDepth = aBuffer.mColorDepth;
  if (aInfo.mTransferFunction) {
    data.mTransferFunction = *aInfo.mTransferFunction;
  }
  data.mColorRange = aBuffer.mColorRange;
  data.mChromaSubsampling = aBuffer.mChromaSubsampling;
  return data;
}

/* static */
MediaResult VideoData::SetVideoDataToImage(PlanarYCbCrImage* aVideoImage,
                                           const VideoInfo& aInfo,
                                           const YCbCrBuffer& aBuffer,
                                           const IntRect& aPicture,
                                           bool aCopyData) {
  MOZ_ASSERT(aVideoImage);

  PlanarYCbCrData data = ConstructPlanarYCbCrData(aInfo, aBuffer, aPicture);

  if (aCopyData) {
    return MediaResult::Logged(aVideoImage->CopyData(data),
                               RESULT_DETAIL("Failed to copy image data"),
                               sPDMLog);
  }
  return MediaResult::Logged(aVideoImage->AdoptData(data),
                             RESULT_DETAIL("Failed to adopt image data"),
                             sPDMLog);
}

#if XP_WIN
bool ConvertToNV12AtImageHost(const VideoData::YCbCrBuffer& aBuffer,
                              const IntRect& aPicture,
                              layers::KnowsCompositor* aAllocator) {
  if (!aAllocator) {
    return false;
  }

  // Only allow in GPU process to avoid RDD process memory leaks
  if (!XRE_IsGPUProcess()) {
    return false;
  }

  auto ident = aAllocator->GetTextureFactoryIdentifier();

  if (!StaticPrefs::gfx_video_convert_yuv_to_nv12_image_host_win() ||
      ident.mParentProcessType != GeckoProcessType_GPU ||
      !aAllocator->GetCompositorUseDComp() ||
      !gfx::DeviceManagerDx::Get()->CanUseNV12()) {
    return false;
  }

  // XXX support gfx::ColorRange::FULL
  if (aPicture.Size().width % 2 != 0 || aPicture.Size().height % 2 != 0 ||
      aBuffer.mColorDepth != gfx::ColorDepth::COLOR_8 ||
      aBuffer.mColorRange != gfx::ColorRange::LIMITED ||
      aBuffer.mChromaSubsampling !=
          gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT) {
    return false;
  }
  return true;
}
#endif

/* static */
bool VideoData::UseUseNV12ForSoftwareDecodedVideoIfPossible(
    layers::KnowsCompositor* aAllocator) {
#if XP_WIN
  if (!aAllocator) {
    return false;
  }

  if (StaticPrefs::gfx_video_convert_yuv_to_nv12_image_host_win()) {
    return false;
  }

  if (StaticPrefs::gfx_video_convert_i420_to_nv12_force_enabled_AtStartup() ||
      (gfx::DeviceManagerDx::Get()->CanUseNV12() &&
       gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin() &&
       aAllocator->GetCompositorUseDComp())) {
    return true;
  }
#endif
  return false;
}

/* static */
Result<already_AddRefed<VideoData>, MediaResult> VideoData::CreateAndCopyData(
    const VideoInfo& aInfo, ImageContainer* aContainer, int64_t aOffset,
    const TimeUnit& aTime, const TimeUnit& aDuration,
    const YCbCrBuffer& aBuffer, bool aKeyframe, const TimeUnit& aTimecode,
    const IntRect& aPicture, layers::KnowsCompositor* aAllocator) {
  if (!aContainer) {
    // Create a dummy VideoData with no image. This gives us something to
    // send to media streams if necessary.
    RefPtr<VideoData> v(new VideoData(aOffset, aTime, aDuration, aKeyframe,
                                      aTimecode, aInfo.mDisplay, 0));
    return v.forget();
  }

  if (MediaResult r = ValidateBufferAndPicture(aBuffer, aPicture);
      NS_FAILED(r)) {
    return Err(r);
  }

  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::CopyDecodedVideo,
                                                  aInfo.mImage.height);
  RefPtr<VideoData> v(new VideoData(aOffset, aTime, aDuration, aKeyframe,
                                    aTimecode, aInfo.mDisplay, 0));

  // Currently our decoder only knows how to output to ImageFormat::PLANAR_YCBCR
  // format.
#if XP_WIN
  // Copy to NV12 format D3D11ShareHandleImage when video overlay could be used
  // with the video frame
  if (UseUseNV12ForSoftwareDecodedVideoIfPossible(aAllocator)) {
    PlanarYCbCrData data = ConstructPlanarYCbCrData(aInfo, aBuffer, aPicture);
    RefPtr<layers::D3D11ShareHandleImage> d3d11Image =
        layers::D3D11ShareHandleImage::MaybeCreateNV12ImageAndSetData(
            aAllocator, aContainer, data);
    if (d3d11Image) {
      v->mImage = d3d11Image;
      perfRecorder.Record();
      return v.forget();
    }
  }

  // D3D11YCbCrImage can only handle YCbCr images using 3 non-interleaved planes
  // non-zero mSkip value indicates that one of the plane would be interleaved.
  if (XRE_IsGPUProcess() && aAllocator && aAllocator->SupportsD3D11() &&
      !ConvertToNV12AtImageHost(aBuffer, aPicture, aAllocator) &&
      aBuffer.mPlanes[0].mSkip == 0 && aBuffer.mPlanes[1].mSkip == 0 &&
      aBuffer.mPlanes[2].mSkip == 0) {
    RefPtr<layers::D3D11YCbCrImage> d3d11Image = new layers::D3D11YCbCrImage();
    PlanarYCbCrData data = ConstructPlanarYCbCrData(aInfo, aBuffer, aPicture);
    if (d3d11Image->SetData(layers::ImageBridgeChild::GetSingleton()
                                ? layers::ImageBridgeChild::GetSingleton().get()
                                : aAllocator,
                            aContainer, data)) {
      v->mImage = d3d11Image;
      perfRecorder.Record();
      return v.forget();
    }
  }
#elif XP_MACOSX
  if (aAllocator && aAllocator->GetWebRenderCompositorType() !=
                        layers::WebRenderCompositor::SOFTWARE) {
    RefPtr<layers::MacIOSurfaceImage> ioImage =
        new layers::MacIOSurfaceImage(nullptr);
    PlanarYCbCrData data = ConstructPlanarYCbCrData(aInfo, aBuffer, aPicture);
    if (ioImage->SetData(aContainer, data)) {
      v->mImage = ioImage;
      perfRecorder.Record();
      return v.forget();
    }
  }
#endif
  if (!v->mImage) {
    v->mImage = aContainer->CreatePlanarYCbCrImage();
  }

  if (!v->mImage) {
    // TODO: Should other error like NS_ERROR_UNEXPECTED be used here to
    // distinguish this error from the NS_ERROR_OUT_OF_MEMORY below?
    return Err(MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY,
        RESULT_DETAIL("Failed to create a PlanarYCbCrImage"), sPDMLog));
  }
  NS_ASSERTION(v->mImage->GetFormat() == ImageFormat::PLANAR_YCBCR,
               "Wrong format?");
  PlanarYCbCrImage* videoImage = v->mImage->AsPlanarYCbCrImage();
  MOZ_ASSERT(videoImage);
  videoImage->SetColorDepth(aBuffer.mColorDepth);

  if (MediaResult r = VideoData::SetVideoDataToImage(
          videoImage, aInfo, aBuffer, aPicture, true /* aCopyData */);
      NS_FAILED(r)) {
    return Err(r);
  }

  perfRecorder.Record();
  return v.forget();
}

/* static */
Result<already_AddRefed<VideoData>, MediaResult> VideoData::CreateAndCopyData(
    const VideoInfo& aInfo, ImageContainer* aContainer, int64_t aOffset,
    const TimeUnit& aTime, const TimeUnit& aDuration,
    const YCbCrBuffer& aBuffer, const YCbCrBuffer::Plane& aAlphaPlane,
    bool aKeyframe, const TimeUnit& aTimecode, const IntRect& aPicture) {
  if (!aContainer) {
    // Create a dummy VideoData with no image. This gives us something to
    // send to media streams if necessary.
    RefPtr<VideoData> v(new VideoData(aOffset, aTime, aDuration, aKeyframe,
                                      aTimecode, aInfo.mDisplay, 0));
    return v.forget();
  }

  if (MediaResult r = ValidateBufferAndPicture(aBuffer, aPicture);
      NS_FAILED(r)) {
    return Err(std::move(r));
  }
  if (!ValidatePlane(aAlphaPlane)) {
    return Err(MediaResult::Logged(NS_ERROR_DOM_MEDIA_RANGE_ERR,
                                   RESULT_DETAIL("Invalid alpha plane"),
                                   sPDMLog));
  }
  // The alpha plane is expected to be the same size as the luma plane.
  // See Method 1 at https://wiki.webmproject.org/alpha-channel
  if (aBuffer.mPlanes[0].mWidth != aAlphaPlane.mWidth ||
      aBuffer.mPlanes[0].mHeight != aAlphaPlane.mHeight) {
    return Err(MediaResult::Logged(NS_ERROR_DOM_MEDIA_RANGE_ERR,
                                   RESULT_DETAIL("luma and alpha sizes differ"),
                                   sPDMLog));
  }
  // ConvertI420AlphaToARGB() expects equal strides for luma and alpha
  if (aBuffer.mPlanes[0].mStride != aAlphaPlane.mStride) {
    return Err(MediaResult::Logged(
        NS_ERROR_DOM_MEDIA_RANGE_ERR,
        RESULT_DETAIL("luma and alpha strides differ"), sPDMLog));
  }

  RefPtr<VideoData> v(new VideoData(aOffset, aTime, aDuration, aKeyframe,
                                    aTimecode, aInfo.mDisplay, 0));

  // Convert from YUVA to BGRA format on the software side.
  RefPtr<layers::SharedRGBImage> videoImage =
      aContainer->CreateSharedRGBImage();
  v->mImage = videoImage;
  if (!v->mImage) {
    return Err(MediaResult::Logged(NS_ERROR_OUT_OF_MEMORY,
                                   RESULT_DETAIL("CreateSharedRGBImage failed"),
                                   sPDMLog));
  }
  if (!videoImage->Allocate(
          IntSize(aBuffer.mPlanes[0].mWidth, aBuffer.mPlanes[0].mHeight),
          SurfaceFormat::B8G8R8A8)) {
    return Err(MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY,
        RESULT_DETAIL("failed to Allocate SharedRGBImage"), sPDMLog));
  }

  RefPtr<layers::TextureClient> texture =
      videoImage->GetTextureClient(/* aKnowsCompositor */ nullptr);
  if (!texture) {
    return Err(MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY,
        RESULT_DETAIL("Failed to allocate TextureClient"), sPDMLog));
  }

  layers::TextureClientAutoLock autoLock(texture,
                                         layers::OpenMode::OPEN_WRITE_ONLY);
  if (!autoLock.Succeeded()) {
    return Err(MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY, RESULT_DETAIL("Failed to lock TextureClient"),
        sPDMLog));
  }

  layers::MappedTextureData buffer;
  if (!texture->BorrowMappedData(buffer)) {
    return Err(MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY, RESULT_DETAIL("Failed to borrow mapped data"),
        sPDMLog));
  }

  // The naming convention for libyuv and associated utils is word-order.
  // The naming convention in the gfx stack is byte-order.
  nsresult result = ConvertI420AlphaToARGB(
      aBuffer.mPlanes[0].mData, aBuffer.mPlanes[1].mData,
      aBuffer.mPlanes[2].mData, aAlphaPlane.mData,
      AssertedCast<int>(aBuffer.mPlanes[0].mStride),
      AssertedCast<int>(aBuffer.mPlanes[1].mStride), buffer.data, buffer.stride,
      buffer.size.width, buffer.size.height);
  if (NS_FAILED(result)) {
#define MSG "Failed to convert I420 YUVA into RGBA data"
    return Err(MediaResult::Logged(result, RESULT_DETAIL(MSG), sPDMLog));
#undef MSG
  }

  return v.forget();
}

/* static */
already_AddRefed<VideoData> VideoData::CreateFromImage(
    const IntSize& aDisplay, int64_t aOffset, const TimeUnit& aTime,
    const TimeUnit& aDuration, const RefPtr<Image>& aImage, bool aKeyframe,
    const TimeUnit& aTimecode) {
  RefPtr<VideoData> v(new VideoData(aOffset, aTime, aDuration, aKeyframe,
                                    aTimecode, aDisplay, 0));
  v->mImage = aImage;
  return v.forget();
}

nsCString VideoData::ToString() const {
  std::array ImageFormatStrings = {
      "PLANAR_YCBCR",
      "NV_IMAGE",
      "SHARED_RGB",
      "MOZ2D_SURFACE",
      "MAC_IOSURFACE",
      "SURFACE_TEXTURE",
      "D3D9_RGB32_TEXTURE",
      "OVERLAY_IMAGE",
      "D3D11_SHARE_HANDLE_TEXTURE",
      "D3D11_TEXTURE_ZERO_COPY",
      "TEXTURE_WRAPPER",
      "D3D11_YCBCR_IMAGE",
      "GPU_VIDEO",
      "DMABUF",
      "DCOMP_SURFACE",
  };

  nsCString rv;
  rv.AppendPrintf(
      "VideoFrame [%s,%s] [%dx%d] format: %s", mTime.ToString().get(),
      mDuration.ToString().get(), mDisplay.Width(), mDisplay.Height(),
      mImage ? ImageFormatStrings[static_cast<int>(mImage->GetFormat())]
             : "null");
  return rv;
}

MediaResult VideoData::QuantizableBuffer::To8BitPerChannel(
    BufferRecycleBin* aRecycleBin) {
  MOZ_ASSERT(!mRecycleBin, "Should not be called more than once.");
  mRecycleBin = aRecycleBin;

  // Find available converter.
  MOZ_ASSERT(mColorDepth == ColorDepth::COLOR_10 ||
             mColorDepth == ColorDepth::COLOR_12);
  using Func16To8 =  // libyuv function type.
      std::function<int(const uint16_t*, int, const uint16_t*, int,
                        const uint16_t*, int, uint8_t*, int, uint8_t*, int,
                        uint8_t*, int, int, int)>;
  auto convertFunc = [](ColorDepth aDepth,
                        ChromaSubsampling aSubsampling) -> Func16To8 {
    switch (aSubsampling) {
      case ChromaSubsampling::HALF_WIDTH_AND_HEIGHT:  // 420p
        return aDepth == ColorDepth::COLOR_10 ? libyuv::I010ToI420
                                              : libyuv::I012ToI420;
      case ChromaSubsampling::HALF_WIDTH:  // 422p
        return aDepth == ColorDepth::COLOR_10 ? libyuv::I210ToI422
                                              : libyuv::I212ToI422;
      case ChromaSubsampling::FULL:  // 444p
        return aDepth == ColorDepth::COLOR_10 ? libyuv::I410ToI444
                                              : libyuv::I412ToI444;
      default:
        return Func16To8();
    }
  }(mColorDepth, mChromaSubsampling);
  if (!convertFunc) {
    return MediaResult::Logged(
        NS_ERROR_NOT_IMPLEMENTED,
        RESULT_DETAIL("Source format (color depth=%d, subsampling=%" PRIu8
                      ") not supported",
                      BitDepthForColorDepth(mColorDepth),
                      static_cast<uint8_t>(mChromaSubsampling)),
        sPDMLog);
  }

  if (mPlanes[0].mStride % 2 != 0 ||
      mPlanes[0].mStride < mPlanes[0].mWidth * 2 ||
      mPlanes[1].mStride % 2 != 0 ||
      mPlanes[1].mStride < mPlanes[1].mWidth * 2 ||
      mPlanes[2].mStride % 2 != 0 ||
      mPlanes[2].mStride < mPlanes[2].mWidth * 2) {
    return MediaResult::Logged(
        NS_ERROR_ILLEGAL_VALUE,
        RESULT_DETAIL("width/stride don't add up: y:%" PRIu32 "/%" PRIu32
                      ", cb:%" PRIu32 "/%" PRIu32 ", cr:%" PRIu32 "/%" PRIu32,
                      mPlanes[0].mWidth, mPlanes[0].mStride, mPlanes[1].mWidth,
                      mPlanes[1].mStride, mPlanes[2].mWidth,
                      mPlanes[2].mStride),
        sPDMLog);
  }

  // libyuv functions use `int` for width/height/stride.
  CheckedInt<int> yWidth(mPlanes[0].mWidth);
  CheckedInt<int> yHeight(mPlanes[0].mHeight);
  CheckedInt<int> yStride(mPlanes[0].mStride / 2);
  CheckedInt<int> cbcrStride(mPlanes[1].mStride / 2);
  if (!yWidth.isValid() || yWidth.value() == 0 || !yHeight.isValid() ||
      yHeight.value() == 0 || !yStride.isValid() || yStride.value() == 0 ||
      !cbcrStride.isValid() || cbcrStride.value() == 0) {
    return MediaResult::Logged(
        NS_ERROR_ILLEGAL_VALUE,
        RESULT_DETAIL("Invalid plane size:%" PRIu32 "x%" PRIu32
                      ", y-stride:%" PRIu32 ", cbcr-stride:%" PRIu32,
                      mPlanes[0].mWidth, mPlanes[0].mHeight, mPlanes[0].mStride,
                      mPlanes[1].mStride),
        sPDMLog);
  }

  // Set up destination.
  CheckedUint32 yLength(yStride.toChecked<uint32_t>() * yHeight.value());
  CheckedUint32 cbcrLength(cbcrStride.toChecked<uint32_t>() *
                           mPlanes[1].mHeight);
  if (!yLength.isValid() || yLength.value() == 0 || !cbcrLength.isValid() ||
      cbcrLength.value() == 0) {
    return MediaResult::Logged(
        NS_ERROR_ILLEGAL_VALUE,
        RESULT_DETAIL("Invalid buffer size y:%ix%i, cbcr:%ix%" PRIu32,
                      yStride.value(), yHeight.value(), cbcrStride.value(),
                      mPlanes[1].mHeight),
        sPDMLog);
  }
  CheckedUint32 destLength(yLength + (cbcrLength * 2));
  if (!destLength.isValid()) {
    return MediaResult::Logged(
        NS_ERROR_ILLEGAL_VALUE,
        RESULT_DETAIL(
            "Cannot allocate 8-bit conversion buffer : invalid length"),
        sPDMLog);
  }
  AllocateRecyclableData(destLength.value());
  if (!m8bpcPlanes) {
    return MediaResult::Logged(
        NS_ERROR_OUT_OF_MEMORY,
        RESULT_DETAIL("Fail to allocate %" PRIu32 " bytes for 8-bit conversion",
                      destLength.value()),
        sPDMLog);
  }
  uint8_t* destPlanes[3]{
      m8bpcPlanes.get(), m8bpcPlanes.get() + yLength.value(),
      m8bpcPlanes.get() + yLength.value() + cbcrLength.value()};

  // Set up source.
  if (!mPlanes[0].mData || !mPlanes[1].mData || !mPlanes[2].mData) {
    return MediaResult::Logged(
        NS_ERROR_ILLEGAL_VALUE,
        RESULT_DETAIL("Invalid source buffer y:%p, cb:%p cr:%p",
                      mPlanes[0].mData, mPlanes[1].mData, mPlanes[2].mData),
        sPDMLog);
  }
  const uint16_t* srcPlanes[3]{
      reinterpret_cast<const uint16_t*>(mPlanes[0].mData),
      reinterpret_cast<const uint16_t*>(mPlanes[1].mData),
      reinterpret_cast<const uint16_t*>(mPlanes[2].mData)};

  int r = convertFunc(srcPlanes[0], yStride.value(), srcPlanes[1],
                      cbcrStride.value(), srcPlanes[2], cbcrStride.value(),
                      destPlanes[0], yStride.value(), destPlanes[1],
                      cbcrStride.value(), destPlanes[2], cbcrStride.value(),
                      yWidth.value(), yHeight.value());
  if (r != 0) {
    return MediaResult::Logged(
        NS_ERROR_DOM_MEDIA_DECODE_ERR,
        RESULT_DETAIL("Conversion to 8-bit failed. libyuv error=%d", r),
        sPDMLog);
  }
  // Update buffer info.
  mColorDepth = ColorDepth::COLOR_8;
  mPlanes[0].mData = destPlanes[0];
  mPlanes[0].mStride = yStride.value();
  mPlanes[1].mData = destPlanes[1];
  mPlanes[2].mData = destPlanes[2];
  mPlanes[1].mStride = mPlanes[2].mStride = cbcrStride.value();

  return MediaResult(NS_OK);
}

void VideoData::QuantizableBuffer::AllocateRecyclableData(uint32_t aLength) {
  MOZ_ASSERT(!m8bpcPlanes, "Should not allocate more than once.");
  MOZ_ASSERT(aLength > 0, "Zero-length allocation!");

  m8bpcPlanes = mRecycleBin->GetBuffer(aLength);
  if (m8bpcPlanes) {
    mAllocatedLength = aLength;
  }
}

VideoData::QuantizableBuffer::~QuantizableBuffer() {
  if (m8bpcPlanes) {
    mRecycleBin->RecycleBuffer(std::move(m8bpcPlanes), mAllocatedLength);
  }
}

MediaRawData::MediaRawData()
    : MediaData(Type::RAW_DATA), mCrypto(mCryptoInternal) {}

MediaRawData::MediaRawData(const uint8_t* aData, size_t aSize)
    : MediaData(Type::RAW_DATA),
      mCrypto(mCryptoInternal),
      mBuffer(aData, aSize) {}

MediaRawData::MediaRawData(const uint8_t* aData, size_t aSize,
                           const uint8_t* aAlphaData, size_t aAlphaSize)
    : MediaData(Type::RAW_DATA),
      mCrypto(mCryptoInternal),
      mBuffer(aData, aSize),
      mAlphaBuffer(aAlphaData, aAlphaSize) {}

MediaRawData::MediaRawData(AlignedByteBuffer&& aData)
    : MediaData(Type::RAW_DATA),
      mCrypto(mCryptoInternal),
      mBuffer(std::move(aData)) {}

MediaRawData::MediaRawData(AlignedByteBuffer&& aData,
                           AlignedByteBuffer&& aAlphaData)
    : MediaData(Type::RAW_DATA),
      mCrypto(mCryptoInternal),
      mBuffer(std::move(aData)),
      mAlphaBuffer(std::move(aAlphaData)) {}

already_AddRefed<MediaRawData> MediaRawData::Clone() const {
  int32_t sampleHeight = 0;
  if (mTrackInfo && mTrackInfo->GetAsVideoInfo()) {
    sampleHeight = mTrackInfo->GetAsVideoInfo()->mImage.height;
  }
  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::CopyDemuxedData,
                                                  sampleHeight);
  RefPtr<MediaRawData> s = new MediaRawData;
  s->mTimecode = mTimecode;
  s->mTime = mTime;
  s->mDuration = mDuration;
  s->mOffset = mOffset;
  s->mKeyframe = mKeyframe;
  s->mExtraData = mExtraData;
  s->mCryptoInternal = mCryptoInternal;
  s->mTrackInfo = mTrackInfo;
  s->mEOS = mEOS;
  s->mOriginalPresentationWindow = mOriginalPresentationWindow;
  if (!s->mBuffer.Append(mBuffer.Data(), mBuffer.Length())) {
    return nullptr;
  }
  if (!s->mAlphaBuffer.Append(mAlphaBuffer.Data(), mAlphaBuffer.Length())) {
    return nullptr;
  }
  perfRecorder.Record();
  return s.forget();
}

MediaRawData::~MediaRawData() = default;

size_t MediaRawData::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t size = aMallocSizeOf(this);
  size += mBuffer.SizeOfExcludingThis(aMallocSizeOf);
  return size;
}

UniquePtr<MediaRawDataWriter> MediaRawData::CreateWriter() {
  UniquePtr<MediaRawDataWriter> p(new MediaRawDataWriter(this));
  return p;
}

MediaRawDataWriter::MediaRawDataWriter(MediaRawData* aMediaRawData)
    : mCrypto(aMediaRawData->mCryptoInternal), mTarget(aMediaRawData) {}

bool MediaRawDataWriter::SetSize(size_t aSize) {
  return mTarget->mBuffer.SetLength(aSize);
}

bool MediaRawDataWriter::Prepend(const uint8_t* aData, size_t aSize) {
  return mTarget->mBuffer.Prepend(aData, aSize);
}

bool MediaRawDataWriter::Append(const uint8_t* aData, size_t aSize) {
  return mTarget->mBuffer.Append(aData, aSize);
}

bool MediaRawDataWriter::Replace(const uint8_t* aData, size_t aSize) {
  return mTarget->mBuffer.Replace(aData, aSize);
}

void MediaRawDataWriter::Clear() { mTarget->mBuffer.Clear(); }

uint8_t* MediaRawDataWriter::Data() { return mTarget->mBuffer.Data(); }

size_t MediaRawDataWriter::Size() { return mTarget->Size(); }

void MediaRawDataWriter::PopFront(size_t aSize) {
  mTarget->mBuffer.PopFront(aSize);
}

nsCString CryptoSchemeSetToString(const CryptoSchemeSet& aSchemes) {
  nsAutoCString rv;
  if (aSchemes.contains(CryptoScheme::Cenc)) {
    rv.AppendLiteral("cenc");
  }
  if (aSchemes.contains(CryptoScheme::Cbcs)) {
    if (!rv.IsEmpty()) {
      rv.AppendLiteral("/");
    }
    rv.AppendLiteral("cbcs");
  }
  if (aSchemes.contains(CryptoScheme::Cbcs_1_9)) {
    if (!rv.IsEmpty()) {
      rv.AppendLiteral("/");
    }
    rv.AppendLiteral("cbcs-1-9");
  }
  if (rv.IsEmpty()) {
    rv.AppendLiteral("none");
  }
  return std::move(rv);
}

CryptoScheme StringToCryptoScheme(const nsAString& aString) {
  if (aString.EqualsLiteral("cenc")) {
    return CryptoScheme::Cenc;
  }
  if (aString.EqualsLiteral("cbcs")) {
    return CryptoScheme::Cbcs;
  }
  if (aString.EqualsLiteral("cbcs-1-9")) {
    return CryptoScheme::Cbcs_1_9;
  }
  return CryptoScheme::None;
}

}  // namespace mozilla
