/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WMFVP9MFTManager_h_
#define WMFVP9MFTManager_h_

#include "DXVA2Manager.h"
#include "DXVAVP9Manager.h"
#include "MediaInfo.h"
#include "MediaResult.h"
#include "WMFMediaDataDecoder.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "nsTArray.h"

namespace mozilla {

class WMFVP9MFTManager final : public MFTManager {
 public:
  WMFVP9MFTManager(const VideoInfo& aConfig,
                   layers::KnowsCompositor* aCompositor,
                   layers::ImageContainer* aContainer);
  ~WMFVP9MFTManager() override;

  MediaResult Init();
  HRESULT Input(MediaRawData* aSample) override;
  HRESULT Output(int64_t aStreamOffset, RefPtr<MediaData>& aOutput) override;
  void Flush() override;
  // Input submits each complete frame synchronously. Output drains the images
  // we already copied; there is no IMFTransform to send a drain command to.
  void Drain() override {}
  void Shutdown() override;
  bool IsHardwareAccelerated(nsACString& aReason) const override;
  void SetSeekThreshold(const media::TimeUnit& aTime) override;
  bool HasSeekThreshold() const override;
  TrackInfo::TrackType GetType() override { return TrackInfo::kVideoTrack; }
  nsCString GetDescriptionName() const override {
    return "VP9 DXVA2 profile 0 hardware decoder"_ns;
  }
  nsCString GetCodecName() const override { return "vp9"_ns; }

 private:
  void FlushInternal();

  const VideoInfo mVideoInfo;
  RefPtr<layers::KnowsCompositor> mKnowsCompositor;
  RefPtr<layers::ImageContainer> mImageContainer;
  UniquePtr<DXVA2Manager> mDXVA2Manager;
  UniquePtr<DXVAVP9Manager> mVP9Decoder;
  nsTArray<RefPtr<MediaData>> mPendingFrames;
  nsCString mFailureReason;
  bool mIsValid = false;
  // Decoder operations run on the decode queue, but hardware status may be
  // queried from another thread while shutdown releases the device.
  mutable Mutex mLock MOZ_UNANNOTATED;
};

}  // namespace mozilla

#endif  // WMFVP9MFTManager_h_
