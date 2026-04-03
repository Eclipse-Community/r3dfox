/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "jsapi/RTCEncodedAudioFrame.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "api/frame_transformer_interface.h"

#include "jsapi/RTCEncodedFrameBase.h"
#include "jsapi/RTCRtpScriptTransform.h"
#include "mozilla/dom/RTCRtpScriptTransformer.h"
#include "mozilla/dom/RTCEncodedAudioFrameBinding.h"
#include "nsIGlobalObject.h"
#include "nsContentUtils.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Unused.h"
#include "mozilla/fallible.h"
#include "js/RootingAPI.h"

namespace mozilla::dom {

RTCEncodedAudioFrame::RTCEncodedAudioFrame(
    nsIGlobalObject* aGlobal,
    std::unique_ptr<webrtc::TransformableFrameInterface> aFrame,
    uint64_t aCounter, RTCRtpScriptTransformer* aOwner)
    : RTCEncodedFrameBase(aGlobal, std::move(aFrame), aCounter, aOwner) {
  mMetadata.mSynchronizationSource.Construct(mFrame->GetSsrc());
  mMetadata.mPayloadType.Construct(mFrame->GetPayloadType());
  // send frames are derived directly from TransformableFrameInterface, not
  // TransformableAudioFrameInterface! Right now, send frames have no csrcs
  // or sequence number
  // TODO(bug 1835076): Fix this
  if (mFrame->GetDirection() ==
      webrtc::TransformableFrameInterface::Direction::kReceiver) {
    const auto& audioFrame(
        static_cast<webrtc::TransformableAudioFrameInterface&>(*mFrame));
    mMetadata.mContributingSources.Construct();
    for (const auto csrc : audioFrame.GetContributingSources()) {
      Unused << mMetadata.mContributingSources.Value().AppendElement(csrc,
                                                                     fallible);
    }
    if (const auto optionalSeqNum = audioFrame.SequenceNumber()) {
      mMetadata.mSequenceNumber.Construct(*optionalSeqNum);
    }
  }
}

JSObject* RTCEncodedAudioFrame::WrapObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return RTCEncodedAudioFrame_Binding::Wrap(aCx, this, aGivenProto);
}

void RTCEncodedAudioFrame::GetMetadata(
    RTCEncodedAudioFrameMetadata& aMetadata) const {
  aMetadata = mMetadata;
}

bool RTCEncodedAudioFrame::CheckOwner(RTCRtpScriptTransformer* aOwner) const {
  return aOwner == mOwner;
}
}  // namespace mozilla::dom
