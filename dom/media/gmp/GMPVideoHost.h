/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPVideoHost_h_
#define GMPVideoHost_h_

#include "GMPSharedMemManager.h"
#include "gmp-video-frame.h"
#include "gmp-video-host.h"
#include "gmp-video-plane.h"
#include "nsTArray.h"

namespace mozilla::gmp {

class GMPVideoEncodedFrameImpl;
class GMPVideoi420FrameImpl;

class GMPVideoHostImpl : public GMPVideoHost, public GMPSharedMemManager {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  // GMPVideoHost
  GMPErr CreateFrame(GMPVideoFrameFormat aFormat,
                     GMPVideoFrame** aFrame) override;
  GMPErr CreatePlane(GMPPlane** aPlane) override;

 protected:
  GMPVideoHostImpl();
  virtual ~GMPVideoHostImpl();
};

}  // namespace mozilla::gmp

#endif  // GMPVideoHost_h_
