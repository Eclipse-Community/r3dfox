/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StunAddrsRequestChild.h"

#include "mozilla/net/NeckoChild.h"
#include "nsISerialEventTarget.h"

using namespace mozilla::ipc;

namespace mozilla::net {

// static
RefPtr<StunAddrsRequestChild> StunAddrsRequestChild::Create(
    StunAddrsListener* listener) {
  // c'tor is protected, so we can't use fancy things like MakeRefPtr
  RefPtr<StunAddrsRequestChild> result(new StunAddrsRequestChild(listener));
  gNeckoChild->SendPStunAddrsRequestConstructor(result);
  return result;
}

StunAddrsRequestChild::StunAddrsRequestChild(StunAddrsListener* listener)
    : mListener(listener) {}

mozilla::ipc::IPCResult StunAddrsRequestChild::RecvOnMDNSQueryComplete(
    const nsACString& hostname, const Maybe<nsCString>& address) {
  if (mListener) {
    mListener->OnMDNSQueryComplete(PromiseFlatCString(hostname), address);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult StunAddrsRequestChild::RecvOnStunAddrsAvailable(
    const NrIceStunAddrArray& addrs) {
  if (mListener) {
    mListener->OnStunAddrsAvailable(addrs);
  }
  return IPC_OK();
}

void StunAddrsRequestChild::Cancel() { mListener = nullptr; }

NS_IMPL_ADDREF(StunAddrsListener)
NS_IMPL_RELEASE(StunAddrsListener)

}  // namespace mozilla::net
