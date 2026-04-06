/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoDecoderChild.h"

#include "GMPContentChild.h"
#include <stdio.h>
#include "mozilla/Unused.h"
#include "GMPPlatform.h"
#include "GMPVideoEncodedFrameImpl.h"
#include "GMPVideoi420FrameImpl.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"
#include "runnable_utils.h"

namespace mozilla::gmp {

GMPVideoDecoderChild::GMPVideoDecoderChild(GMPContentChild* aPlugin)
    : mPlugin(aPlugin), mVideoDecoder(nullptr) {
  MOZ_ASSERT(mPlugin);
}

GMPVideoDecoderChild::~GMPVideoDecoderChild() {
  // Since any outstanding synchronous runnables require a strong reference to
  // ourselves, we know that when we are freed, they must have all successfully
  // dispatched. As such, it should now be safe to free the plugin and join with
  // the worker thread.
  if (mVideoDecoder) {
    mVideoDecoder->DecodingComplete();
  }
}

bool GMPVideoDecoderChild::MgrIsOnOwningThread() const {
  return !mPlugin || mPlugin->GMPMessageLoop() == MessageLoop::current();
}

void GMPVideoDecoderChild::Init(GMPVideoDecoder* aDecoder) {
  MOZ_ASSERT(aDecoder,
             "Cannot initialize video decoder child without a video decoder!");
  mVideoDecoder = aDecoder;
}

void GMPVideoDecoderChild::Decoded(GMPVideoi420Frame* aDecodedFrame) {
  if (!aDecodedFrame) {
    MOZ_CRASH("Not given a decoded frame!");
  }

  if (NS_WARN_IF(!mPlugin)) {
    aDecodedFrame->Destroy();
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  auto df = static_cast<GMPVideoi420FrameImpl*>(aDecodedFrame);

  ipc::Shmem inputShmem;
  if (MgrTakeShmem(GMPSharedMemClass::Encoded, &inputShmem)) {
    Unused << SendReturnShmem(std::move(inputShmem));
  }

  GMPVideoi420FrameData frameData;
  ipc::Shmem frameShmem;
  nsTArray<uint8_t> frameArray;

  if (df->InitFrameData(frameData, frameShmem)) {
    Unused << SendDecodedShmem(frameData, std::move(frameShmem));
  } else if (df->InitFrameData(frameData, frameArray)) {
    Unused << SendDecodedData(frameData, std::move(frameArray));
  } else {
    MOZ_CRASH("Decoded without any frame data!");
  }

  aDecodedFrame->Destroy();
}

void GMPVideoDecoderChild::ReceivedDecodedReferenceFrame(
    const uint64_t aPictureId) {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendReceivedDecodedReferenceFrame(aPictureId);
}

void GMPVideoDecoderChild::ReceivedDecodedFrame(const uint64_t aPictureId) {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendReceivedDecodedFrame(aPictureId);
}

void GMPVideoDecoderChild::InputDataExhausted() {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendInputDataExhausted();
}

void GMPVideoDecoderChild::DrainComplete() {
  if (!mDrainSelfRef) {
    MOZ_ASSERT_UNREACHABLE("DrainComplete without Drain!");
    return;
  }

  // Proxy release to ensure that any synchronous runnables from the plugin can
  // first unblock the worker thread. If we destroy the plugin once this
  // reference is freed, we won't be blocked trying to join the worker thread.
  NS_ProxyRelease("GMPVideoDecoderChild::DrainComplete",
                  GetMainThreadSerialEventTarget(), mDrainSelfRef.forget(),
                  /* aAlwaysProxy */ true);

  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendDrainComplete();
}

void GMPVideoDecoderChild::ResetComplete() {
  if (!mResetSelfRef) {
    MOZ_ASSERT_UNREACHABLE("ResetComplete without Reset!");
    return;
  }

  // Proxy release to ensure that any synchronous runnables from the plugin can
  // first unblock the worker thread. If we destroy the plugin once this
  // reference is freed, we won't be blocked trying to join the worker thread.
  NS_ProxyRelease("GMPVideoDecoderChild::ResetComplete",
                  GetMainThreadSerialEventTarget(), mResetSelfRef.forget(),
                  /* aAlwaysProxy */ true);

  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendResetComplete();
}

void GMPVideoDecoderChild::Error(GMPErr aError) {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendError(aError);
}

mozilla::ipc::IPCResult GMPVideoDecoderChild::RecvInitDecode(
    const GMPVideoCodec& aCodecSettings, nsTArray<uint8_t>&& aCodecSpecific,
    const int32_t& aCoreCount) {
  if (!mVideoDecoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoDecoder->InitDecode(aCodecSettings, aCodecSpecific.Elements(),
                            aCodecSpecific.Length(), this, aCoreCount);
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoDecoderChild::RecvGiveShmem(
    ipc::Shmem&& aOutputShmem) {
  MgrGiveShmem(GMPSharedMemClass::Decoded, std::move(aOutputShmem));
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoDecoderChild::RecvDecode(
    const GMPVideoEncodedFrameData& aInputFrame, ipc::Shmem&& aInputShmem,
    const bool& aMissingFrames, nsTArray<uint8_t>&& aCodecSpecificInfo,
    const int64_t& aRenderTimeMs) {
  if (!mVideoDecoder) {
    DeallocShmem(aInputShmem);
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  auto* f =
      new GMPVideoEncodedFrameImpl(aInputFrame, std::move(aInputShmem), this);

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoDecoder->Decode(f, aMissingFrames, aCodecSpecificInfo.Elements(),
                        aCodecSpecificInfo.Length(), aRenderTimeMs);

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoDecoderChild::RecvReset() {
  if (!mVideoDecoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  if (mResetSelfRef) {
    MOZ_ASSERT_UNREACHABLE("Already has outstanding reset!");
    return IPC_OK();
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mResetSelfRef = this;
  mVideoDecoder->Reset();

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoDecoderChild::RecvDrain() {
  if (!mVideoDecoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  if (mDrainSelfRef) {
    MOZ_ASSERT_UNREACHABLE("Already has outstanding drain!");
    return IPC_OK();
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mDrainSelfRef = this;
  mVideoDecoder->Drain();

  return IPC_OK();
}

void GMPVideoDecoderChild::ActorDestroy(ActorDestroyReason why) {
  // We don't destroy the video decoder from the plugin here because there may
  // be outstanding synchronous runnables. They hold a strong reference to
  // ourselves, so we can wait for our destructor to be called first.
  MgrPurgeShmems();
  mPlugin = nullptr;
}

}  // namespace mozilla::gmp
