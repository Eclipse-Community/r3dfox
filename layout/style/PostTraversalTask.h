/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_PostTraversalTask_h
#define mozilla_PostTraversalTask_h

#include "mozilla/AlreadyAddRefed.h"

/* a task to be performed immediately after a Servo traversal */

namespace mozilla {
class ServoStyleSet;
namespace dom {
class FontFaceSet;
class FontFaceSetImpl;
}  // namespace dom
namespace fontlist {
struct Family;
}  // namespace fontlist
}  // namespace mozilla
class gfxUserFontEntry;

namespace mozilla {

/**
 * A PostTraversalTask is a task to be performed immediately after a Servo
 * traversal.  There are just a few tasks we need to perform, so we use this
 * class rather than Runnables, to avoid virtual calls and some allocations.
 *
 * A PostTraversalTask is only safe to run immediately after the Servo
 * traversal, since it can hold raw pointers to DOM objects.
 */
class PostTraversalTask {
 public:
  static PostTraversalTask DispatchLoadingEventAndReplaceReadyPromise(
      already_AddRefed<dom::FontFaceSetImpl> aFontFaceSetImpl) {
    PostTraversalTask task(Type::DispatchLoadingEventAndReplaceReadyPromise);
    task.mTarget = aFontFaceSetImpl.take();
    return task;
  }

  static PostTraversalTask LoadFontEntry(
      already_AddRefed<gfxUserFontEntry> aFontEntry) {
    PostTraversalTask task(Type::LoadFontEntry);
    task.mTarget = aFontEntry.take();
    return task;
  }

  void Run();

  PostTraversalTask(const PostTraversalTask&) = delete;
  PostTraversalTask(PostTraversalTask&& aOther)
      : PostTraversalTask(aOther.mType) {
    mTarget = aOther.mTarget;
    aOther.mTarget = nullptr;
  };

  ~PostTraversalTask();

 private:
  enum class Type {
    // mTarget (FontFaceSetImpl*)
    DispatchLoadingEventAndReplaceReadyPromise,

    // mTarget (gfxUserFontEntry*)
    LoadFontEntry,
  };

  explicit PostTraversalTask(Type aType)
      : mType(aType), mTarget(nullptr) {}

  const Type mType;
  // Note that this is a strong reference of the relevant target
  void* mTarget = nullptr;
};

}  // namespace mozilla

#endif  // mozilla_PostTraversalTask_h
