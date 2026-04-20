/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TCPServerSocketChild.h"
#include "TCPSocketChild.h"
#include "TCPServerSocket.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/dom/PBrowserChild.h"
#include "mozilla/dom/BrowserChild.h"
#include "nsJSUtils.h"
#include "jsfriendapi.h"

using mozilla::net::gNeckoChild;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION(TCPServerSocketChild, mServerSocket)
NS_IMPL_CYCLE_COLLECTING_ADDREF(TCPServerSocketChild)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TCPServerSocketChild)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TCPServerSocketChild)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// static
RefPtr<TCPServerSocketChild> TCPServerSocketChild::Create(
    TCPServerSocket* aServerSocket, uint16_t aLocalPort, uint16_t aBacklog,
    bool aUseArrayBuffers) {
  // c'tor is private, so we can't use fancy things like MakeRefPtr
  RefPtr<TCPServerSocketChild> result(new TCPServerSocketChild(aServerSocket));
  gNeckoChild->SendPTCPServerSocketConstructor(result, aLocalPort, aBacklog,
                                               aUseArrayBuffers);
  return result;
}

TCPServerSocketChild::TCPServerSocketChild(TCPServerSocket* aServerSocket)
    : mServerSocket(aServerSocket) {}

TCPServerSocketChild::~TCPServerSocketChild() = default;

mozilla::ipc::IPCResult TCPServerSocketChild::RecvCallbackAccept(
    mozilla::NotNull<PTCPSocketChild*> psocket) {
  RefPtr<TCPSocketChild> socket = static_cast<TCPSocketChild*>(psocket.get());
  nsresult rv = mServerSocket->AcceptChildSocket(socket);
  NS_ENSURE_SUCCESS(rv, IPC_OK());
  return IPC_OK();
}

void TCPServerSocketChild::Close() { SendClose(); }

}  // namespace mozilla::dom
