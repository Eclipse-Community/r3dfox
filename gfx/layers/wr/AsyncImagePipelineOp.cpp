/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AsyncImagePipelineOp.h"

#include "mozilla/layers/AsyncImagePipelineManager.h"

namespace mozilla {
namespace layers {

AsyncImagePipelineOp::AsyncImagePipelineOp(
    Tag aTag, AsyncImagePipelineManager* aAsyncImageManager,
    const wr::PipelineId& aPipelineId, TextureHost* aTextureHost)
    : mTag(aTag),
      mAsyncImageManager(aAsyncImageManager),
      mPipelineId(aPipelineId),
      mTextureHost(aTextureHost) {
  MOZ_ASSERT(mTag == Tag::ApplyAsyncImageForPipeline);
}

AsyncImagePipelineOp::AsyncImagePipelineOp(
    Tag aTag, AsyncImagePipelineManager* aAsyncImageManager,
    const wr::PipelineId& aPipelineId)
    : mTag(aTag),
      mAsyncImageManager(aAsyncImageManager),
      mPipelineId(aPipelineId) {
  MOZ_ASSERT(mTag == Tag::RemoveAsyncImagePipeline);
}

AsyncImagePipelineOp::~AsyncImagePipelineOp() = default;
AsyncImagePipelineOp::AsyncImagePipelineOp(AsyncImagePipelineOp&&) = default;
AsyncImagePipelineOp::AsyncImagePipelineOp(const AsyncImagePipelineOp&) =
    default;

AsyncImagePipelineOps::~AsyncImagePipelineOps() = default;

void AsyncImagePipelineOps::HandleOps(wr::TransactionBuilder& aTxn) {
  MOZ_ASSERT(!mList.empty());

  while (!mList.empty()) {
    auto& frontOp = mList.front();
    switch (frontOp.mTag) {
      case AsyncImagePipelineOp::Tag::ApplyAsyncImageForPipeline: {
        AsyncImagePipelineManager* manager = frontOp.mAsyncImageManager.get();
        const auto& pipelineId = frontOp.mPipelineId;
        const auto& textureHost = frontOp.mTextureHost;

        manager->ApplyAsyncImageForPipeline(pipelineId, textureHost, aTxn);
        break;
      }
      case AsyncImagePipelineOp::Tag::RemoveAsyncImagePipeline: {
        AsyncImagePipelineManager* manager = frontOp.mAsyncImageManager.get();
        const auto& pipelineId = frontOp.mPipelineId;
        manager->RemoveAsyncImagePipeline(pipelineId, /* aPendingOps */ nullptr,
                                          aTxn);
        break;
      }
    }
    mList.pop();
  }
}

}  // namespace layers
}  // namespace mozilla
