/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPVideoHost.h"
#include "mozilla/Assertions.h"
#include "GMPSharedMemManager.h"
#include "GMPVideoPlaneImpl.h"
#include "GMPVideoi420FrameImpl.h"
#include "GMPVideoEncodedFrameImpl.h"

namespace mozilla::gmp {

GMPVideoHostImpl::GMPVideoHostImpl() = default;

GMPVideoHostImpl::~GMPVideoHostImpl() = default;

GMPErr GMPVideoHostImpl::CreateFrame(GMPVideoFrameFormat aFormat,
                                     GMPVideoFrame** aFrame) {
  if (!aFrame) {
    return GMPGenericErr;
  }
  *aFrame = nullptr;

  switch (aFormat) {
    case kGMPI420VideoFrame:
      *aFrame = new GMPVideoi420FrameImpl(this);
      return GMPNoErr;
    case kGMPEncodedVideoFrame:
      *aFrame = new GMPVideoEncodedFrameImpl(this);
      return GMPNoErr;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown frame format!");
  }

  return GMPGenericErr;
}

GMPErr GMPVideoHostImpl::CreatePlane(GMPPlane** aPlane) {
  if (!aPlane) {
    return GMPGenericErr;
  }
  *aPlane = nullptr;

  auto* p = new GMPPlaneImpl();

  *aPlane = p;

  return GMPNoErr;
}

}  // namespace mozilla::gmp
