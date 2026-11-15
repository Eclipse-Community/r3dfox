/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFDecoderModule.h"

#include <algorithm>

#include "AOMDecoder.h"
#include "DriverCrashGuard.h"
#include "DXVA2Manager.h"
#include "DXVAVP9Manager.h"
#include "WMFVP9MFTManager.h"
#include "GfxDriverInfo.h"
#include "MFTDecoder.h"
#include "MP4Decoder.h"
#include "MediaInfo.h"
#include "PDMFactory.h"
#include "VPXDecoder.h"
#include "WMFAudioMFTManager.h"
#include "WMFMediaDataDecoder.h"
#include "WMFVideoMFTManager.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/mscom/EnsureMTA.h"
#include "nsComponentManagerUtils.h"
#include "nsIXULRuntime.h"
#include "nsIXULRuntime.h"  // for BrowserTabsRemoteAutostart
#include "nsServiceManagerUtils.h"
#include "nsWindowsHelpers.h"
#include "prsystem.h"

#define LOG(...) MOZ_LOG_FMT(sPDMLog, mozilla::LogLevel::Debug, __VA_ARGS__)

namespace mozilla {

// Helper function to add a profile marker and log at the same time.
static void MOZ_FORMAT_PRINTF(2, 3)
    WmfDecoderModuleMarkerAndLog(const ProfilerString8View& aMarkerTag,
                                 const char* aFormat, ...) {
  va_list ap;
  va_start(ap, aFormat);
  const nsVprintfCString markerString(aFormat, ap);
  va_end(ap);
  PROFILER_MARKER_TEXT(aMarkerTag, MEDIA_PLAYBACK, {}, markerString);
  LOG("{}", markerString.get());
}

static const GUID CLSID_CMSAACDecMFT = {
    0x32D186A7,
    0x218F,
    0x4C75,
    {0x88, 0x76, 0xDD, 0x77, 0x27, 0x3A, 0x89, 0x99}};

static Atomic<bool> sDXVAEnabled(false);

/* static */
already_AddRefed<PlatformDecoderModule> WMFDecoderModule::Create() {
  RefPtr<WMFDecoderModule> wmf = new WMFDecoderModule();
  return wmf.forget();
}

static bool IsRemoteAcceleratedCompositor(
    layers::KnowsCompositor* aKnowsCompositor) {
  if (!aKnowsCompositor) {
    return false;
  }

  if (aKnowsCompositor->UsingSoftwareWebRenderD3D11()) {
    return true;
  }

  layers::TextureFactoryIdentifier ident =
      aKnowsCompositor->GetTextureFactoryIdentifier();
  return !aKnowsCompositor->UsingSoftwareWebRender() &&
         ident.mParentProcessType == GeckoProcessType_GPU;
}

/* static */
void WMFDecoderModule::Init() {
  // TODO : add an assertion to prevent this from running on main thread.
  if (XRE_IsContentProcess()) {
    // If we're in the content process and the UseGPUDecoder pref is set, it
    // means that we've given up on the GPU process (it's been crashing) so we
    // should disable DXVA
    sDXVAEnabled = !StaticPrefs::media_gpu_process_decoder();
  } else if (XRE_IsGPUProcess()) {
    // Always allow DXVA in the GPU process.
    sDXVAEnabled = true;
  } else if (XRE_IsRDDProcess()) {
    // Hardware accelerated decoding is explicitly only done in the GPU process
    // to avoid copying textures whenever possible. Previously, detecting
    // whether the video bridge was set up could be done with the following:
    // sDXVAEnabled = !!DeviceManagerDx::Get()->GetImageDevice();
    // The video bridge was previously broken due to initialization order
    // issues. For more information see Bug 1763880.
    sDXVAEnabled = false;
  } else {
    // Only allow DXVA in the UI process if we aren't in e10s Firefox
    sDXVAEnabled = !mozilla::BrowserTabsRemoteAutostart();
  }

  // We have heavy logging below to help diagnose issue around hardware
  // decoding failures. Due to these failures often relating to driver level
  // problems they're hard to nail down, so we want lots of info. We may be
  // able to relax this in future if we're not seeing such problems (see bug
  // 1673007 for references to the bugs motivating this).
  bool hwVideo = gfx::gfxVars::GetCanUseHardwareVideoDecodingOrDefault();
  WmfDecoderModuleMarkerAndLog(
      "WMFInit DXVA Status",
      "sDXVAEnabled: %s, CanUseHardwareVideoDecoding: %s",
      sDXVAEnabled ? "true" : "false", hwVideo ? "true" : "false");
  sDXVAEnabled = sDXVAEnabled && hwVideo;

  mozilla::mscom::EnsureMTA([&]() {
    StaticMutexAutoLock lock(sMutex);
    // Store the supported MFT decoders.
    sSupportedTypes.clear();
    sLackOfExtensionTypes.clear();
    // i = 1 to skip Unknown.
    for (uint32_t i = 1; i < static_cast<uint32_t>(WMFStreamType::SENTINEL);
         i++) {
      WMFStreamType type = static_cast<WMFStreamType>(i);
      RefPtr<MFTDecoder> decoder = new MFTDecoder();
      HRESULT hr = CreateMFTDecoder(type, decoder);
      if (SUCCEEDED(hr)) {
        sSupportedTypes += type;
        WmfDecoderModuleMarkerAndLog("WMFInit Decoder Supported",
                                     "%s is enabled", EnumValueToString(type));
      } else if (hr != E_FAIL) {
        // E_FAIL should be logged by CreateMFTDecoder. Skipping those codes
        // will help to keep the logs readable.
        WmfDecoderModuleMarkerAndLog("WMFInit Decoder Failed",
                                     "%s failed with code 0x%lx",
                                     EnumValueToString(type), hr);
        if (hr == WINCODEC_ERR_COMPONENTNOTFOUND) {
          // Only AV1 and HEVC have a LackOfExtension entry in
          // MediaCodecsSupport; any other codec hitting this arm has lost
          // a built-in MFT and is plainly unsupported, and would also
          // trip the corresponding assertion in
          // MCSInfo::GetDecodeMediaCodecsSupported.
          MOZ_ASSERT(type == WMFStreamType::AV1 || type == WMFStreamType::HEVC);
          if (type == WMFStreamType::AV1) {
            WmfDecoderModuleMarkerAndLog("No AV1 extension",
                                         "Lacking of AV1 extension");
          } else if (type == WMFStreamType::HEVC) {
            WmfDecoderModuleMarkerAndLog("No HEVC extension",
                                         "Lacking of HEVC extension");
          }
          sLackOfExtensionTypes += type;
        }
      }
    }
  });

  {
    StaticMutexAutoLock lock(sMutex);
    sSupportedTypesInitialized = true;
  }

  WmfDecoderModuleMarkerAndLog("WMFInit Result",
                               "WMFDecoderModule::Init finishing");
}

/* static */
int WMFDecoderModule::GetNumDecoderThreads() {
  int32_t numCores = PR_GetNumberOfProcessors();

  // If we have more than 4 cores, let the decoder decide how many threads.
  // On an 8 core machine, WMF chooses 4 decoder threads.
  static const int WMF_DECODER_DEFAULT = -1;
  if (numCores > 4) {
    return WMF_DECODER_DEFAULT;
  }
  return std::max(numCores - 1, 1);
}

/* static */
HRESULT WMFDecoderModule::CreateMFTDecoder(const WMFStreamType& aType,
                                           RefPtr<MFTDecoder>& aDecoder) {
  // Do not expose any video decoder on utility process which is only for audio
  // decoding.
  if (XRE_IsUtilityProcess()) {
    switch (aType) {
      case WMFStreamType::H264:
      case WMFStreamType::VP8:
      case WMFStreamType::VP9:
      case WMFStreamType::AV1:
      case WMFStreamType::HEVC:
        return E_FAIL;
      default:
        break;
    }
  }

  switch (aType) {
    case WMFStreamType::H264: {
      if (XRE_IsGPUProcess() && !sDXVAEnabled) {
        WmfDecoderModuleMarkerAndLog("CreateMFTDecoder, H264 Failure",
                                     "SW decoder is not allowed in the GPU "
                                     "process and the HW H264 requires DXVA");
        return E_FAIL;
      }
      return aDecoder->Create(CLSID_CMSH264DecoderMFT);
    }
    case WMFStreamType::VP8:
      static const uint32_t VP8_USABLE_BUILD = 16287;
      if (!IsWindowsBuildOrLater(VP8_USABLE_BUILD)) {
        WmfDecoderModuleMarkerAndLog("CreateMFTDecoder, VP8 Failure",
                                     "VP8 MFT requires Windows build %" PRId32
                                     " or later",
                                     VP8_USABLE_BUILD);
        return E_FAIL;
      }
      if (!gfx::gfxVars::GetUseVP8HwDecodeOrDefault()) {
        WmfDecoderModuleMarkerAndLog("CreateMFTDecoder, VP8 Failure",
                                     "Gfx VP8 blocklist");
        return E_FAIL;
      }
      [[fallthrough]];
    case WMFStreamType::VP9:
      if (!sDXVAEnabled) {
        WmfDecoderModuleMarkerAndLog("CreateMFTDecoder, VPx Disabled",
                                     "%s MFT requires DXVA",
                                     EnumValueToString(aType));
        return E_FAIL;
      }

      {
        gfx::WMFVPXVideoCrashGuard guard;
        if (guard.Crashed()) {
          WmfDecoderModuleMarkerAndLog(
              "CreateMFTDecoder, VPx Failure",
              "Will not use VPx MFT due to crash guard reporting a crash");
          return E_FAIL;
        }
        return aDecoder->Create(CLSID_CMSVPXDecMFT);
      }
    case WMFStreamType::AV1:
      // If this process cannot use DXVA, the AV1 decoder will not be used.
      // Also, upon startup, init will be called both before and after
      // layers acceleration is setup. This prevents creating the AV1 decoder
      // twice.
      if (!sDXVAEnabled) {
        WmfDecoderModuleMarkerAndLog("CreateMFTDecoder AV1 Disabled",
                                     "AV1 MFT requires DXVA");
        return E_FAIL;
      }
      // TODO: MFTEnumEx is slower than creating by CLSID, it may be worth
      // investigating other ways to instantiate the AV1 decoder.
      return aDecoder->Create(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_AV1,
                              MFVideoFormat_NV12);
    case WMFStreamType::HEVC:
      if (!StaticPrefs::media_hevc_enabled() || !sDXVAEnabled) {
        return E_FAIL;
      }
      return aDecoder->Create(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_HEVC,
                              MFVideoFormat_NV12);
    case WMFStreamType::MP3:
      return aDecoder->Create(CLSID_CMP3DecMediaObject);
    case WMFStreamType::AAC:
      return aDecoder->Create(CLSID_CMSAACDecMFT);
    default:
      return E_FAIL;
  }
}

/* static */
bool WMFDecoderModule::CanCreateMFTDecoder(const WMFStreamType& aType) {
  MOZ_ASSERT(WMFStreamType::Unknown < aType && aType < WMFStreamType::SENTINEL);
  bool hasInitialized = false;
  {
    StaticMutexAutoLock lock(sMutex);
    hasInitialized = sSupportedTypesInitialized;
  }
  if (!hasInitialized) {
    Init();
  }

  // Check prefs here rather than CreateMFTDecoder so that prefs aren't baked
  // into sSupportedTypes
  switch (aType) {
    case WMFStreamType::VP9:
      if (!StaticPrefs::media_wmf_vp9_mft_enabled()) {
        return false;
      }
      [[fallthrough]];
    case WMFStreamType::VP8:
      if (!StaticPrefs::media_wmf_vp9_enabled()) {
        return false;
      }
      break;
    case WMFStreamType::AV1:
      if (!StaticPrefs::media_av1_enabled() ||
          !StaticPrefs::media_wmf_av1_enabled()) {
        return false;
      }
      break;
    case WMFStreamType::HEVC:
      if (!StaticPrefs::media_hevc_enabled()) {
        return false;
      }
      break;
    case WMFStreamType::MP3:
      // Prefer ffvpx mp3 decoder over WMF.
      if (StaticPrefs::media_ffvpx_mp3_enabled()) {
        return false;
      }
      break;
    default:
      break;
  }

  // Do not expose any video decoder on utility process which is only for audio
  // decoding.
  if (XRE_IsUtilityProcess()) {
    switch (aType) {
      case WMFStreamType::H264:
      case WMFStreamType::VP8:
      case WMFStreamType::VP9:
      case WMFStreamType::AV1:
      case WMFStreamType::HEVC:
        return false;
      default:
        break;
    }
  }
  StaticMutexAutoLock lock(sMutex);
  return sSupportedTypes.contains(aType);
}

// The direct decoder accepts only profile 0 (8-bit 4:2:0). An absent vpcC
// leaves the profile unknown until the first sample; the parser checks it then.
static bool IsVP9DXVA2ConfigSupported(const VideoInfo& aInfo) {
  if (aInfo.HasAlpha() || aInfo.mColorDepth != gfx::ColorDepth::COLOR_8) {
    return false;
  }
  // MIME-only queries have no dimensions. Use a representative size for their
  // hardware probe, but reject known unsupported dimensions immediately.
  if ((aInfo.mImage.width > 0 && aInfo.mImage.width < 16) ||
      (aInfo.mImage.height > 0 && aInfo.mImage.height < 16) ||
      aInfo.mImage.width > 8192 || aInfo.mImage.height > 8192) {
    return false;
  }
  if (aInfo.mExtraData && !aInfo.mExtraData->IsEmpty()) {
    const auto& extraData = *aInfo.mExtraData;
    // ReadVPCCBox expects a version-1 record with no initialization data.
    if (extraData.Length() < 12 || extraData[0] != 1 ||
        ((extraData[6] >> 1) & 7) > 1 || extraData[10] || extraData[11]) {
      return false;
    }
    VPXDecoder::VPXStreamInfo info;
    VPXDecoder::ReadVPCCBox(info, aInfo.mExtraData);
    return info.mProfile == 0 && info.mBitDepth == 8 && info.mSubSampling_x &&
           info.mSubSampling_y;
  }
  return true;
}

static bool ProbeVP9DXVA2(layers::KnowsCompositor* aKnowsCompositor,
                         const VideoInfo* aInfo = nullptr) {
  if (!StaticPrefs::media_wmf_enabled() ||
      !StaticPrefs::media_wmf_vp9_enabled() ||
      !StaticPrefs::media_wmf_vp9_dxva2_enabled() || !IsVistaOrLater() ||
      !gfx::gfxVars::GetCanUseHardwareVideoDecodingOrDefault() ||
      (aInfo && !IsVP9DXVA2ConfigSupported(*aInfo)) ||
      !wmf::MediaFoundationInitializer::HasInitialized()) {
    return false;
  }

  nsAutoCString failureReason;
  const GUID& guid = DXVAVP9Manager::GetVP9DecoderGUID();
  // Remote decoder capability enumeration has no KnowsCompositor yet. Probe
  // the device directly in that case; no images or textures are allocated.
  UniquePtr<DXVA2Manager> manager(
      aKnowsCompositor
          ? WMFVideoMFTManager::CreateVP9DXVA(aKnowsCompositor, failureReason,
                                            guid)
          : DXVA2Manager::CreateD3D9DXVA(nullptr, failureReason, &guid));
  if (!manager) {
    return false;
  }

  const gfx::IntSize size =
      aInfo && aInfo->mImage.width > 0 && aInfo->mImage.height > 0
          ? aInfo->mImage
          : gfx::IntSize(1280, 720);
  DXVAVP9Manager decoder;
  return decoder.Init(manager->GetDXVADeviceManager(), size.width, size.height,
                      failureReason);
}

/* static */
bool WMFDecoderModule::HasVP9MFT() {
  if (!StaticPrefs::media_wmf_enabled() ||
      !StaticPrefs::media_wmf_vp9_enabled() ||
      !StaticPrefs::media_wmf_vp9_mft_enabled() || !IsVistaOrLater() ||
      !gfx::gfxVars::GetCanUseHardwareVideoDecodingOrDefault() ||
      !wmf::MediaFoundationInitializer::HasInitialized()) {
    return false;
  }
  // Keep the guard on the calling thread: it is active on the main thread,
  // while IMFTransform must be created and released on an MTA thread.
  gfx::WMFVPXVideoCrashGuard guard;
  if (guard.Crashed()) {
    return false;
  }
  bool supported = false;
  mscom::EnsureMTA([&]() {
    RefPtr<MFTDecoder> decoder = new MFTDecoder();
    supported = SUCCEEDED(decoder->Create(CLSID_CMSVPXDecMFT));
  });
  return supported;
}

/* static */
bool WMFDecoderModule::HasVP9DXVA2(
    layers::KnowsCompositor* aKnowsCompositor) {
  return aKnowsCompositor && ProbeVP9DXVA2(aKnowsCompositor);
}

bool WMFDecoderModule::SupportsColorDepth(
    gfx::ColorDepth aColorDepth, DecoderDoctorDiagnostics* aDiagnostics) const {
  // Color depth support can be determined by creating DX decoders.
  return true;
}

media::DecodeSupportSet WMFDecoderModule::Supports(
    const SupportDecoderParams& aParams,
    DecoderDoctorDiagnostics* aDiagnostics) const {
  // This should only be supported by MFMediaEngineDecoderModule.
  if (aParams.mMediaEngineId) {
    return media::DecodeSupportSet{};
  }
  // In GPU process, only support decoding if video. This only gives a hint of
  // what the GPU decoder *may* support. The actual check will occur in
  // CreateVideoDecoder.
  const auto& trackInfo = aParams.mConfig;
  if (XRE_IsGPUProcess() && !trackInfo.GetAsVideoInfo()) {
    return media::DecodeSupportSet{};
  }

  const auto* videoInfo = trackInfo.GetAsVideoInfo();
  // Temporary - forces use of VPXDecoder when alpha is present.
  // Bug 1263836 will handle alpha scenario once implemented. It will shift
  // the check for alpha to PDMFactory but not itself remove the need for a
  // check.
  if (videoInfo && (!SupportsColorDepth(videoInfo->mColorDepth, aDiagnostics) ||
                    videoInfo->HasAlpha())) {
    return media::DecodeSupportSet{};
  }

  if (videoInfo && VPXDecoder::IsVP9(aParams.MimeType()) &&
      aParams.mOptions.contains(CreateDecoderParams::Option::LowLatency)) {
    // SVC layers are unsupported, and may be used in low latency use cases
    // (WebRTC).
    return media::DecodeSupportSet{};
  }

  WMFStreamType type = GetStreamTypeFromMimeType(aParams.MimeType());
  if (type == WMFStreamType::Unknown) {
    return media::DecodeSupportSet{};
  }

  // Keep the MFT availability cache separate from direct DXVA2 support. A
  // system without the VP9 MFT can still have a usable profile-0 DXVA2 decoder.
  const bool hasMFT = CanCreateMFTDecoder(type);
  if (type == WMFStreamType::VP9) {
    if (!sDXVAEnabled || (!XRE_IsGPUProcess() && !XRE_IsRDDProcess()) ||
        !StaticPrefs::media_wmf_vp9_enabled() ||
        !gfx::gfxVars::GetCanUseHardwareVideoDecodingOrDefault() ||
        aParams.mOptions.contains(
            CreateDecoderParams::Option::HardwareDecoderNotAllowed) ||
        (aParams.mKnowsCompositor &&
         (!aParams.mKnowsCompositor->SupportsD3D11() ||
          (XRE_IsGPUProcess() &&
           !IsRemoteAcceleratedCompositor(aParams.mKnowsCompositor))))) {
      return media::DecodeSupportSet{};
    }
    if (hasMFT || ProbeVP9DXVA2(aParams.mKnowsCompositor, videoInfo)) {
      return media::DecodeSupport::HardwareDecode;
    }
    return media::DecodeSupportSet{};
  }

  if (hasMFT) {
    if (StreamTypeIsVideo(type)) {
      return sDXVAEnabled ? media::DecodeSupport::HardwareDecode
                          : media::DecodeSupport::SoftwareDecode;
    } else {
      // Audio only supports software decode
      return media::DecodeSupport::SoftwareDecode;
    }
  }
  StaticMutexAutoLock lock(sMutex);
  return sLackOfExtensionTypes.contains(type)
             ? media::DecodeSupport::UnsureDueToLackOfExtension
             : media::DecodeSupportSet{};
}

nsresult WMFDecoderModule::Startup() {
  return wmf::MediaFoundationInitializer::HasInitialized() ? NS_OK
                                                           : NS_ERROR_FAILURE;
}

already_AddRefed<MediaDataDecoder> WMFDecoderModule::CreateVP9Decoder(
    const CreateDecoderParams& aParams) {
  const VideoInfo& config = aParams.VideoConfig();
  // CanCreateMFTDecoder also ensures that sDXVAEnabled is initialized. Disabling
  // the MFT preference must not suppress initialization of the direct path.
  const bool mftAllowed = CanCreateMFTDecoder(WMFStreamType::VP9);
  if (!sDXVAEnabled || (!XRE_IsGPUProcess() && !XRE_IsRDDProcess()) ||
      !StaticPrefs::media_wmf_vp9_enabled() ||
      !gfx::gfxVars::GetCanUseHardwareVideoDecodingOrDefault() ||
      config.HasAlpha() ||
      aParams.mOptions.contains(
          CreateDecoderParams::Option::HardwareDecoderNotAllowed) ||
      aParams.mOptions.contains(CreateDecoderParams::Option::LowLatency)) {
    return nullptr;
  }

  const bool dxva2Allowed = StaticPrefs::media_wmf_vp9_dxva2_enabled() &&
                            IsVistaOrLater() &&
                            IsVP9DXVA2ConfigSupported(config);
  const bool dxva2First = dxva2Allowed &&
                          (!mftAllowed ||
                           StaticPrefs::media_wmf_vp9_dxva2_preferred());
  MediaResult lastError(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        RESULT_DETAIL("No enabled VP9 hardware decoder"));
  auto initialize = [&](auto aManager) -> already_AddRefed<MediaDataDecoder> {
    MediaResult result = aManager->Init();
    if (NS_FAILED(result)) {
      WmfDecoderModuleMarkerAndLog(
          "WMFVP9DecoderCreation Failure", "%s failed: %s",
          aManager->GetDescriptionName().get(), result.Description().get());
      lastError = result;
      // Release hardware resources before trying the other decoder path.
      aManager->Shutdown();
      return nullptr;
    }
    WmfDecoderModuleMarkerAndLog("WMFVP9DecoderCreation Success", "%s",
                                 aManager->GetDescriptionName().get());
    RefPtr<MediaDataDecoder> decoder =
        new WMFMediaDataDecoder(aManager.release());
    return decoder.forget();
  };

  for (bool useDXVA2 : {dxva2First, !dxva2First}) {
    RefPtr<MediaDataDecoder> decoder;
    if (useDXVA2 && dxva2Allowed) {
      decoder = initialize(MakeUnique<WMFVP9MFTManager>(
          config, aParams.mKnowsCompositor, aParams.mImageContainer));
    } else if (!useDXVA2 && mftAllowed) {
      decoder = initialize(MakeUnique<WMFVideoMFTManager>(
          config, aParams.mKnowsCompositor, aParams.mImageContainer,
          aParams.mRate.mValue, aParams.mOptions, sDXVAEnabled,
          aParams.mTrackingId));
    }
    if (decoder) {
      return decoder.forget();
    }
  }
  if (aParams.mError) {
    *aParams.mError = lastError;
  }
  return nullptr;
}

already_AddRefed<MediaDataDecoder> WMFDecoderModule::CreateVideoDecoder(
    const CreateDecoderParams& aParams) {
  // In GPU process, only support decoding if an accelerated compositor is
  // known.
  if (XRE_IsGPUProcess() &&
      !IsRemoteAcceleratedCompositor(aParams.mKnowsCompositor)) {
    return nullptr;
  }

  if (VPXDecoder::IsVP9(aParams.VideoConfig().mMimeType)) {
    return CreateVP9Decoder(aParams);
  }

  UniquePtr<WMFVideoMFTManager> manager(new WMFVideoMFTManager(
      aParams.VideoConfig(), aParams.mKnowsCompositor, aParams.mImageContainer,
      aParams.mRate.mValue, aParams.mOptions, sDXVAEnabled,
      aParams.mTrackingId));

  MediaResult result = manager->Init();
  if (NS_FAILED(result)) {
    if (aParams.mError) {
      *aParams.mError = result;
    }
    WmfDecoderModuleMarkerAndLog(
        "WMFVDecoderCreation Failure",
        "WMFDecoderModule::CreateVideoDecoder failed for manager with "
        "description %s with result: %s",
        manager->GetDescriptionName().get(), result.Description().get());
    return nullptr;
  }

  nsAutoCString hwFailure;
  bool isHardwareAccelerated = manager->IsHardwareAccelerated(hwFailure);
  if (!isHardwareAccelerated) {
    // The decoder description includes whether it is using software or
    // hardware, but no information about how the hardware acceleration failed.
    WmfDecoderModuleMarkerAndLog(
        "WMFVDecoderCreation Success",
        "WMFDecoderModule::CreateVideoDecoder success for manager with "
        "description %s - DXVA failure: %s",
        manager->GetDescriptionName().get(), hwFailure.get());
  } else {
    WmfDecoderModuleMarkerAndLog(
        "WMFVDecoderCreation Success",
        "WMFDecoderModule::CreateVideoDecoder success for manager with "
        "description %s",
        manager->GetDescriptionName().get());
  }

  // Ensure that if the GPU process claims to support hardware acceleration but
  // fails to create a hardware decoder, we do not fall back to a software
  // decoder. Software decoders are intended to run only in the RDD process.
  if (XRE_IsGPUProcess() && !isHardwareAccelerated) {
    WmfDecoderModuleMarkerAndLog("WMFVDecoderCreation Blocked",
                                 "SW decoder is not allowed in the GPU "
                                 "process");
    return nullptr;
  }

  RefPtr<MediaDataDecoder> decoder = new WMFMediaDataDecoder(manager.release());
  return decoder.forget();
}

already_AddRefed<MediaDataDecoder> WMFDecoderModule::CreateAudioDecoder(
    const CreateDecoderParams& aParams) {
  if (XRE_IsGPUProcess()) {
    // Only allow video in the GPU process.
    return nullptr;
  }

  UniquePtr<WMFAudioMFTManager> manager(
      new WMFAudioMFTManager(aParams.AudioConfig()));

  if (!manager->Init()) {
    WmfDecoderModuleMarkerAndLog(
        "WMFADecoderCreation Failure",
        "WMFDecoderModule::CreateAudioDecoder failed for manager with "
        "description %s",
        manager->GetDescriptionName().get());
    return nullptr;
  }

  WmfDecoderModuleMarkerAndLog(
      "WMFADecoderCreation Success",
      "WMFDecoderModule::CreateAudioDecoder success for manager with "
      "description %s",
      manager->GetDescriptionName().get());

  RefPtr<MediaDataDecoder> decoder = new WMFMediaDataDecoder(manager.release());
  return decoder.forget();
}

media::DecodeSupportSet WMFDecoderModule::SupportsMimeType(
    const nsACString& aMimeType, DecoderDoctorDiagnostics* aDiagnostics) const {
  UniquePtr<TrackInfo> trackInfo = CreateTrackInfoWithMIMEType(aMimeType);
  if (!trackInfo) {
    return media::DecodeSupportSet{};
  }
  auto supports = Supports(SupportDecoderParams(*trackInfo), aDiagnostics);
  MOZ_LOG_FMT(sPDMLog, LogLevel::Debug, "WMF decoder {} requested type '{}'",
              !supports.isEmpty() ? "supports" : "rejects",
              PromiseFlatCString(aMimeType).get());
  return supports;
}

}  // namespace mozilla

#undef WFM_DECODER_MODULE_STATUS_MARKER
#undef LOG
