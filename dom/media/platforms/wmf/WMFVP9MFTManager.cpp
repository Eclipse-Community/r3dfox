/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFVP9MFTManager.h"

#include "ImageContainer.h"
#include "VideoUtils.h"
#include "WMFVideoMFTManager.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "nsPrintfCString.h"
#include "nsXULAppAPI.h"

namespace mozilla {

using media::TimeUnit;

struct VP9ShownFrame {
  RefPtr<layers::Image> mImage;
  bool mKeyframe;
};

static bool IsValidVP9Region(const gfx::IntSize& aFrame,
                             const gfx::IntRect& aPicture,
                             const gfx::IntSize& aDisplay) {
  // IsValidVideoRegion checks size limits, but does not require the crop to
  // lie inside the decoded frame. StretchRect needs that additional check.
  return aPicture.x >= 0 && aPicture.y >= 0 && aPicture.width > 0 &&
         aPicture.height > 0 &&
         int64_t(aPicture.x) + aPicture.width <= aFrame.width &&
         int64_t(aPicture.y) + aPicture.height <= aFrame.height &&
         IsValidVideoRegion(aFrame, aPicture, aDisplay);
}

WMFVP9MFTManager::WMFVP9MFTManager(
    const VideoInfo& aConfig, layers::KnowsCompositor* aCompositor,
    layers::ImageContainer* aContainer)
    : mVideoInfo(aConfig),
      mKnowsCompositor(aCompositor),
      mImageContainer(aContainer),
      mLock("WMFVP9MFTManager") {}

WMFVP9MFTManager::~WMFVP9MFTManager() { Shutdown(); }

MediaResult WMFVP9MFTManager::Init() {
  MutexAutoLock lock(mLock);
  MOZ_ASSERT(!mVP9Decoder && !mDXVA2Manager);
  if (!XRE_IsRDDProcess() && !XRE_IsGPUProcess()) {
    mFailureReason.AssignLiteral("DXVA only supported in RDD or GPU process");
  } else if (mVideoInfo.HasAlpha()) {
    mFailureReason.AssignLiteral("VP9 DXVA2 cannot decode streams with alpha");
  } else if (mVideoInfo.mColorDepth != gfx::ColorDepth::COLOR_8) {
    mFailureReason.AssignLiteral("VP9 DXVA2 supports 8-bit profile 0 only");
  } else if (mVideoInfo.mImage.width < 16 || mVideoInfo.mImage.height < 16 ||
             mVideoInfo.mImage.width > 8192 || mVideoInfo.mImage.height > 8192 ||
             !IsValidVP9Region(mVideoInfo.mImage, mVideoInfo.ImageRect(),
                               mVideoInfo.mDisplay)) {
    mFailureReason = nsPrintfCString(
        "VP9 DXVA2 does not accept the video geometry (%dx%d coded size)",
        mVideoInfo.mImage.width, mVideoInfo.mImage.height);
  }
  if (!mFailureReason.IsEmpty()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, mFailureReason);
  }

  mDXVA2Manager.reset(WMFVideoMFTManager::CreateVP9DXVA(
      mKnowsCompositor, mFailureReason, DXVAVP9Manager::GetVP9DecoderGUID()));
  if (!mDXVA2Manager) {
    if (mFailureReason.IsEmpty()) {
      mFailureReason.AssignLiteral(
          "Could not create a D3D9 DXVA2 device for the VP9 profile-0 GUID");
    }
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, mFailureReason);
  }

  mVP9Decoder = MakeUnique<DXVAVP9Manager>();
  if (!mVP9Decoder->Init(mDXVA2Manager->GetDXVADeviceManager(),
                         mVideoInfo.mImage.width, mVideoInfo.mImage.height,
                         mFailureReason)) {
    mVP9Decoder = nullptr;
    mDXVA2Manager = nullptr;
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, mFailureReason);
  }

  mIsValid = true;
  mFailureReason.AssignLiteral("Using VP9 DXVA2 profile 0");
  return NS_OK;
}

HRESULT WMFVP9MFTManager::Input(MediaRawData* aSample) {
  MutexAutoLock lock(mLock);
  if (!mIsValid) {
    return E_UNEXPECTED;
  }
  if (!mPendingFrames.IsEmpty()) {
    return MF_E_NOTACCEPTING;
  }
  if (!aSample || aSample->Size() > UINT32_MAX ||
      !aSample->mTime.IsValid() || aSample->mTime.IsInfinite() ||
      !aSample->mDuration.IsValid() || aSample->mDuration.IsInfinite() ||
      aSample->mDuration.IsNegative()) {
    return E_INVALIDARG;
  }
  const TimeUnit sampleEnd = aSample->mTime + aSample->mDuration;
  if (!sampleEnd.IsValid() || sampleEnd.IsInfinite()) {
    return E_INVALIDARG;
  }
  uint32_t offsets[8], sizes[8], count;
  if (!VP9SplitSuperframe(aSample->Data(), uint32_t(aSample->Size()), offsets,
                         sizes, count)) {
    return E_INVALIDARG;
  }

  // A superframe may refresh a reference slot again after displaying it, or
  // reconfigure the decoder at a keyframe. Copy each displayed surface before
  // decoding the next frame so pending output never aliases decoder surfaces.
  AutoTArray<VP9ShownFrame, 2> shown;
  for (uint32_t i = 0; i < count; ++i) {
    RefPtr<IDirect3DSurface9> surface;
    bool display = false;
    VP9FrameHeader header;
    HRESULT hr = mVP9Decoder->DecodeFrame(
        aSample->Data() + offsets[i], sizes[i], getter_AddRefs(surface), &display,
        &header);
    if (FAILED(hr)) {
      FlushInternal();
      return hr;
    }
    if (!display || (mSeekTargetThreshold && sampleEnd < *mSeekTargetThreshold)) {
      continue;
    }
    const gfx::IntSize frameSize(header.frameWidth, header.frameHeight);
    const gfx::IntRect picture =
        mVideoInfo.ScaledImageRect(header.frameWidth, header.frameHeight);
    if (!IsValidVP9Region(frameSize, picture, mVideoInfo.mDisplay)) {
      FlushInternal();
      return MF_E_INVALIDMEDIATYPE;
    }
    RefPtr<layers::Image> image;
    hr = mDXVA2Manager->CopySurfaceToImage(surface, picture,
                                          getter_AddRefs(image));
    if (FAILED(hr) || !image) {
      FlushInternal();
      return FAILED(hr) ? hr : E_FAIL;
    }
    VP9ShownFrame* entry = shown.AppendElement();
    entry->mImage = std::move(image);
    entry->mKeyframe = !header.showExistingFrame && header.frameType == 0;
  }
  if (shown.IsEmpty()) {
    return S_OK;
  }

  const uint32_t shownCount = shown.Length();
  const TimeUnit slice = aSample->mDuration / int64_t(shownCount);
  for (uint32_t i = 0; i < shownCount; ++i) {
    const TimeUnit time = aSample->mTime + slice * i;
    const TimeUnit duration = (i + 1 == shownCount)
                                  ? aSample->mDuration - slice * i
                                  : slice;
    if (mSeekTargetThreshold && time + duration < *mSeekTargetThreshold) {
      continue;
    }
    RefPtr<VideoData> frame = VideoData::CreateFromImage(
        mVideoInfo.mDisplay, aSample->mOffset, time, duration, shown[i].mImage,
        shown[i].mKeyframe, aSample->mTimecode);
    if (!frame) {
      FlushInternal();
      return E_OUTOFMEMORY;
    }
    mPendingFrames.AppendElement(std::move(frame));
  }
  if (!mPendingFrames.IsEmpty()) {
    mSeekTargetThreshold.reset();
  }
  return S_OK;
}

HRESULT WMFVP9MFTManager::Output(int64_t, RefPtr<MediaData>& aOutput) {
  MutexAutoLock lock(mLock);
  aOutput = nullptr;
  if (mPendingFrames.IsEmpty()) {
    return MF_E_TRANSFORM_NEED_MORE_INPUT;
  }
  aOutput = std::move(mPendingFrames[0]);
  mPendingFrames.RemoveElementAt(0);
  return S_OK;
}

void WMFVP9MFTManager::FlushInternal() {
  mPendingFrames.Clear();
  mSeekTargetThreshold.reset();
  if (mVP9Decoder) {
    mVP9Decoder->Flush();
  }
}

void WMFVP9MFTManager::Flush() {
  MutexAutoLock lock(mLock);
  FlushInternal();
}

void WMFVP9MFTManager::Shutdown() {
  MutexAutoLock lock(mLock);
  mPendingFrames.Clear();
  mSeekTargetThreshold.reset();
  mVP9Decoder = nullptr;
  mDXVA2Manager = nullptr;
  mIsValid = false;
}

bool WMFVP9MFTManager::IsHardwareAccelerated(nsACString& aReason) const {
  MutexAutoLock lock(mLock);
  aReason = mFailureReason;
  return mIsValid;
}

void WMFVP9MFTManager::SetSeekThreshold(const TimeUnit& aTime) {
  MutexAutoLock lock(mLock);
  MFTManager::SetSeekThreshold(aTime);
}

bool WMFVP9MFTManager::HasSeekThreshold() const {
  MutexAutoLock lock(mLock);
  return MFTManager::HasSeekThreshold();
}

}  // namespace mozilla
