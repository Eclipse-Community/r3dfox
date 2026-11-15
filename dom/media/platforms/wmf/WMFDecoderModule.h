/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WMFPlatformDecoderModule_h_
#define WMFPlatformDecoderModule_h_

#include "PlatformDecoderModule.h"
#include "WMF.h"
#include "WMFUtils.h"

namespace mozilla {

class MFTDecoder;

class WMFDecoderModule : public PlatformDecoderModule {
 public:
  const char* Name() const override { return "WMF"; }
  static already_AddRefed<PlatformDecoderModule> Create();

  // Initializes the module, loads required dynamic libraries, etc.
  nsresult Startup() override;

  already_AddRefed<MediaDataDecoder> CreateVideoDecoder(
      const CreateDecoderParams& aParams) override;

  already_AddRefed<MediaDataDecoder> CreateAudioDecoder(
      const CreateDecoderParams& aParams) override;

  bool SupportsColorDepth(
      gfx::ColorDepth aColorDepth,
      DecoderDoctorDiagnostics* aDiagnostics) const override;
  media::DecodeSupportSet SupportsMimeType(
      const nsACString& aMimeType,
      DecoderDoctorDiagnostics* aDiagnostics) const override;
  media::DecodeSupportSet Supports(
      const SupportDecoderParams& aParams,
      DecoderDoctorDiagnostics* aDiagnostics) const override;

  // Can be called on any thread, but avoid calling this on the main thread
  // because the initialization takes long time and we don't want to block the
  // main thread.
  static void Init();

  // Called from any thread, must call init first
  static int GetNumDecoderThreads();

  static HRESULT CreateMFTDecoder(const WMFStreamType& aType,
                                  RefPtr<MFTDecoder>& aDecoder);
  static bool CanCreateMFTDecoder(const WMFStreamType& aType);

  // Diagnostic probes, independent of the process used for playback. The MFT
  // probe checks for an installed transform, not hardware support for a stream.
  // The DXVA2 probe creates a profile-0 decoder at 1280x720. These may be called
  // on the main thread without initializing the module's supported-type cache.
  static bool HasVP9MFT();
  static bool HasVP9DXVA2(layers::KnowsCompositor* aKnowsCompositor);

 private:
  WMFDecoderModule() = default;
  virtual ~WMFDecoderModule() = default;

  already_AddRefed<MediaDataDecoder> CreateVP9Decoder(
      const CreateDecoderParams& aParams);

  static inline StaticMutex sMutex;
  static inline bool sSupportedTypesInitialized MOZ_GUARDED_BY(sMutex) = false;
  static inline EnumSet<WMFStreamType> sSupportedTypes MOZ_GUARDED_BY(sMutex);
  static inline EnumSet<WMFStreamType> sLackOfExtensionTypes
      MOZ_GUARDED_BY(sMutex);
};

}  // namespace mozilla

#endif
