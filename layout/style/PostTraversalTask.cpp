/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PostTraversalTask.h"

#include "mozilla/dom/FontFace.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/FontFaceSetImpl.h"
#include "gfxPlatformFontList.h"
#include "gfxTextRun.h"
#include "ServoStyleSet.h"
#include "nsPresContext.h"

namespace mozilla {

using namespace dom;

PostTraversalTask::~PostTraversalTask() {
  if (!mTarget) {
    return;
  }
  switch (mType) {
    case Type::DispatchLoadingEventAndReplaceReadyPromise:
      static_cast<dom::FontFaceSetImpl*>(mTarget)->Release();
      break;
    case Type::LoadFontEntry:
      static_cast<gfxUserFontEntry*>(mTarget)->Release();
      break;
  }
}

void PostTraversalTask::Run() {
  switch (mType) {
    case Type::DispatchLoadingEventAndReplaceReadyPromise:
      static_cast<dom::FontFaceSetImpl*>(mTarget)
          ->DispatchLoadingEventAndReplaceReadyPromise();
      break;
    case Type::LoadFontEntry:
      static_cast<gfxUserFontEntry*>(mTarget)->ContinueLoad();
      break;
  }
}

}  // namespace mozilla
