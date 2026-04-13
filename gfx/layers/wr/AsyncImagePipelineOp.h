/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_AsyncImagePipelineOp_H
#define MOZILLA_GFX_AsyncImagePipelineOp_H

#include <queue>

#include "mozilla/layers/TextureHost.h"
#include "mozilla/RefPtr.h"
#include "mozilla/webrender/webrender_ffi.h"
#include "Units.h"

namespace mozilla {

namespace wr {
struct Transaction;
}  // namespace wr

namespace layers {

class AsyncImagePipelineManager;
class TextureHost;

class AsyncImagePipelineOp {
 public:
  enum class Tag {
    ApplyAsyncImageForPipeline,
    RemoveAsyncImagePipeline,
  };

  const Tag mTag;

  // Hold a strong reference: queued ops can outlive their owning
  // WebRenderBridgeParent and be processed after StopAndClearResources frees
  // the manager via a deferred remote-texture callback.
  const RefPtr<AsyncImagePipelineManager> mAsyncImageManager;
  const wr::PipelineId mPipelineId;
  const CompositableTextureHostRef mTextureHost;

  // Out-of-line so callers don't need the full AsyncImagePipelineManager type
  // to instantiate ~RefPtr<AsyncImagePipelineManager>.
  ~AsyncImagePipelineOp();
  AsyncImagePipelineOp(AsyncImagePipelineOp&&);
  AsyncImagePipelineOp(const AsyncImagePipelineOp&);

 private:
  AsyncImagePipelineOp(Tag aTag, AsyncImagePipelineManager* aAsyncImageManager,
                       const wr::PipelineId& aPipelineId,
                       TextureHost* aTextureHost);

  AsyncImagePipelineOp(Tag aTag, AsyncImagePipelineManager* aAsyncImageManager,
                       const wr::PipelineId& aPipelineId);

 public:
  static AsyncImagePipelineOp ApplyAsyncImageForPipeline(
      AsyncImagePipelineManager* aAsyncImageManager,
      const wr::PipelineId& aPipelineId, TextureHost* aTextureHost) {
    return AsyncImagePipelineOp(Tag::ApplyAsyncImageForPipeline,
                                aAsyncImageManager, aPipelineId, aTextureHost);
  }

  static AsyncImagePipelineOp RemoveAsyncImagePipeline(
      AsyncImagePipelineManager* aAsyncImageManager,
      const wr::PipelineId& aPipelineId) {
    return AsyncImagePipelineOp(Tag::RemoveAsyncImagePipeline,
                                aAsyncImageManager, aPipelineId);
  }
};

struct AsyncImagePipelineOps {
  explicit AsyncImagePipelineOps(wr::Transaction* aTransaction)
      : mTransaction(aTransaction) {}
  // Out-of-line so callers don't need the full AsyncImagePipelineManager type
  // to instantiate ~RefPtr<AsyncImagePipelineManager> for queued ops.
  ~AsyncImagePipelineOps();

  void HandleOps(wr::TransactionBuilder& aTxn);

  wr::Transaction* const mTransaction;
  std::queue<AsyncImagePipelineOp> mList;
};

}  // namespace layers
}  // namespace mozilla

#endif  // MOZILLA_GFX_AsyncImagePipelineOp_H
