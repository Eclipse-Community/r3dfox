/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TCPServerSocketChild_h
#define mozilla_dom_TCPServerSocketChild_h

#include "mozilla/net/PTCPServerSocketChild.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCOMPtr.h"

#define TCPSERVERSOCKETCHILD_CID \
  {0x41a77ec8, 0xfd86, 0x409e, {0xae, 0xa9, 0xaf, 0x2c, 0xa4, 0x07, 0xef, 0x8e}}

class nsITCPServerSocketInternal;

namespace mozilla::dom {

class TCPServerSocket;

class TCPServerSocketChild : public mozilla::net::PTCPServerSocketChild,
                             public nsISupports {
 public:
  NS_DECL_CYCLE_COLLECTION_CLASS(TCPServerSocketChild)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  static RefPtr<TCPServerSocketChild> Create(TCPServerSocket* aServerSocket,
                                             uint16_t aLocalPort,
                                             uint16_t aBacklog,
                                             bool aUseArrayBuffers);

  void Close();

  mozilla::ipc::IPCResult RecvCallbackAccept(
      mozilla::NotNull<PTCPSocketChild*> socket) override;

 private:
  explicit TCPServerSocketChild(TCPServerSocket* aServerSocket);

  virtual ~TCPServerSocketChild();
  RefPtr<TCPServerSocket> mServerSocket;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_TCPServerSocketChild_h
