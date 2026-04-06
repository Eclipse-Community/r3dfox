/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoEncoderChild.h"
#include "GMPContentChild.h"
#include <stdio.h>
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/Unused.h"
#include "GMPPlatform.h"
#include "GMPVideoEncodedFrameImpl.h"
#include "runnable_utils.h"

namespace mozilla::gmp {

GMPVideoEncoderChild::GMPVideoEncoderChild(GMPContentChild* aPlugin)
    : mPlugin(aPlugin), mVideoEncoder(nullptr) {
  MOZ_ASSERT(mPlugin);
}

GMPVideoEncoderChild::~GMPVideoEncoderChild() {
  // Since any outstanding synchronous runnables require a strong reference to
  // ourselves, we know that when we are freed, they must have all successfully
  // dispatched. As such, it should now be safe to free the plugin and join with
  // the worker thread.
  if (mVideoEncoder) {
    mVideoEncoder->EncodingComplete();
  }
}

bool GMPVideoEncoderChild::MgrIsOnOwningThread() const {
  return !mPlugin || mPlugin->GMPMessageLoop() == MessageLoop::current();
}

void GMPVideoEncoderChild::Init(GMPVideoEncoder* aEncoder) {
  MOZ_ASSERT(aEncoder,
             "Cannot initialize video encoder child without a video encoder!");
  mVideoEncoder = aEncoder;
}

void GMPVideoEncoderChild::Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                                   const uint8_t* aCodecSpecificInfo,
                                   uint32_t aCodecSpecificInfoLength) {
  if (NS_WARN_IF(!mPlugin)) {
    aEncodedFrame->Destroy();
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  auto ef = static_cast<GMPVideoEncodedFrameImpl*>(aEncodedFrame);

  ipc::Shmem inputShmem;
  if (MgrTakeShmem(GMPSharedMemClass::Decoded, &inputShmem)) {
    Unused << SendReturnShmem(std::move(inputShmem));
  }

  nsTArray<uint8_t> codecSpecific;
  codecSpecific.AppendElements(aCodecSpecificInfo, aCodecSpecificInfoLength);

  GMPVideoEncodedFrameData frameData;
  ipc::Shmem frameShmem;
  nsTArray<uint8_t> frameArray;
  if (ef->RelinquishFrameData(frameData, frameShmem)) {
    Unused << SendEncodedShmem(frameData, std::move(frameShmem), codecSpecific);
  } else if (ef->RelinquishFrameData(frameData, frameArray)) {
    Unused << SendEncodedData(frameData, std::move(frameArray), codecSpecific);
  } else {
    MOZ_CRASH("Encoded without any frame data!");
  }

  mLatestEncodedTimestamp = frameData.mTimestamp();

  aEncodedFrame->Destroy();
}

void GMPVideoEncoderChild::MgrDecodedFrameDestroyed(
    GMPVideoi420FrameImpl* aFrame) {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  // The OpenH264 encoder destroys the input frame if it has skipped encoding
  // it. When it has encoded it, it calls the Encoded() callback before
  // destroying the frame.
  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());
  if (aFrame->Timestamp() > mLatestEncodedTimestamp) {
    (void)SendDroppedFrame(aFrame->Timestamp());
  }
}

void GMPVideoEncoderChild::Error(GMPErr aError) {
  if (NS_WARN_IF(!mPlugin)) {
    return;
  }

  MOZ_ASSERT(mPlugin->GMPMessageLoop() == MessageLoop::current());

  SendError(aError);
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvInitEncode(
    const GMPVideoCodec& aCodecSettings, nsTArray<uint8_t>&& aCodecSpecific,
    const int32_t& aNumberOfCores, const uint32_t& aMaxPayloadSize) {
  if (!mVideoEncoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoEncoder->InitEncode(aCodecSettings, aCodecSpecific.Elements(),
                            aCodecSpecific.Length(), this, aNumberOfCores,
                            aMaxPayloadSize);

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvGiveShmem(
    ipc::Shmem&& aOutputShmem) {
  MgrGiveShmem(GMPSharedMemClass::Encoded, std::move(aOutputShmem));
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvEncode(
    const GMPVideoi420FrameData& aInputFrame, ipc::Shmem&& aInputShmem,
    nsTArray<uint8_t>&& aCodecSpecificInfo,
    nsTArray<GMPVideoFrameType>&& aFrameTypes) {
  if (!mVideoEncoder) {
    DeallocShmem(aInputShmem);
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // The `this` destroyed callback outlives the frame, because `mVideoEncoder`
  // is responsible for destroying the frame, and we outlive `mVideoEncoder`.
  auto* f = new GMPVideoi420FrameImpl(aInputFrame, std::move(aInputShmem), this,
                                      HostReportPolicy::Destroyed);

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoEncoder->Encode(f, aCodecSpecificInfo.Elements(),
                        aCodecSpecificInfo.Length(), aFrameTypes.Elements(),
                        aFrameTypes.Length());

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvSetChannelParameters(
    const uint32_t& aPacketLoss, const uint32_t& aRTT) {
  if (!mVideoEncoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoEncoder->SetChannelParameters(aPacketLoss, aRTT);

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvSetRates(
    const uint32_t& aNewBitRate, const uint32_t& aFrameRate) {
  if (!mVideoEncoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoEncoder->SetRates(aNewBitRate, aFrameRate);

  return IPC_OK();
}

mozilla::ipc::IPCResult GMPVideoEncoderChild::RecvSetPeriodicKeyFrames(
    const bool& aEnable) {
  if (!mVideoEncoder) {
    return IPC_FAIL(this, "!mVideoDecoder");
  }

  // Ignore any return code. It is OK for this to fail without killing the
  // process.
  mVideoEncoder->SetPeriodicKeyFrames(aEnable);

  return IPC_OK();
}

void GMPVideoEncoderChild::ActorDestroy(ActorDestroyReason why) {
  // We don't destroy the video encoder from the plugin here because there may
  // be outstanding synchronous runnables. They hold a strong reference to
  // ourselves, so we can wait for our destructor to be called first.
  MgrPurgeShmems();
  mPlugin = nullptr;
}

}  // namespace mozilla::gmp
