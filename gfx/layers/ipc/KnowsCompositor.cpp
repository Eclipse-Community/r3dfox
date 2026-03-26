/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "KnowsCompositor.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/ipc/ProtocolUtils.h"

namespace mozilla::layers {

void KnowsCompositor::IdentifyTextureHost(
    const TextureFactoryIdentifier& aIdentifier) {
  auto lock = mData.Lock();
  lock.ref().mTextureFactoryIdentifier = aIdentifier;

  lock.ref().mSyncObject =
      SyncObjectClient::CreateSyncObjectClientForContentDevice(
          aIdentifier.mSyncHandle);
}

KnowsCompositor::KnowsCompositor()
    : mData("KnowsCompositorMutex"), mSerial(++sSerialCounter) {}

KnowsCompositor::~KnowsCompositor() = default;

KnowsCompositorMediaProxy::KnowsCompositorMediaProxy(
    const TextureFactoryIdentifier& aIdentifier) {
  auto lock = mData.Lock();
  lock.ref().mTextureFactoryIdentifier = aIdentifier;
  // overwrite mSerial's value set by the parent class because we use the same
  // serial as the KnowsCompositor we are proxying.
  mThreadSafeAllocator = ImageBridgeChild::GetSingleton();
  lock.ref().mSyncObject = mThreadSafeAllocator->GetSyncObject();
}

KnowsCompositorMediaProxy::~KnowsCompositorMediaProxy() = default;

TextureForwarder* KnowsCompositorMediaProxy::GetTextureForwarder() {
  return mThreadSafeAllocator->GetTextureForwarder();
}

LayersIPCActor* KnowsCompositorMediaProxy::GetLayersIPCActor() {
  return mThreadSafeAllocator->GetLayersIPCActor();
}

void KnowsCompositorMediaProxy::SyncWithCompositor(
    const Maybe<uint64_t>& aWindowID) {
  mThreadSafeAllocator->SyncWithCompositor(aWindowID);
}

bool IsSurfaceDescriptorValid(const SurfaceDescriptor& aSurface) {
  return aSurface.type() != SurfaceDescriptor::T__None &&
         aSurface.type() != SurfaceDescriptor::Tnull_t;
}

}  // namespace mozilla::layers
