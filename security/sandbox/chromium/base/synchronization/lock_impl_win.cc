// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/lock_impl.h"

#include "base/debug/activity_tracker.h"

#include <windows.h>

namespace base {
namespace internal {

LockImpl::LockImpl() {
  // The second parameter is the spin count, for short-held locks it avoid the
  // contending thread from going to sleep which helps performance greatly.
  ::InitializeCriticalSectionAndSpinCount(reinterpret_cast<PCRITICAL_SECTION>(&native_handle_), 2000);
}

LockImpl::~LockImpl() {
  ::DeleteCriticalSection(reinterpret_cast<PCRITICAL_SECTION>(&native_handle_));
}

bool LockImpl::Try() {
  if (::TryEnterCriticalSection(reinterpret_cast<PCRITICAL_SECTION>(&native_handle_)) != FALSE) {
    return true;
  }
  return false;
}

void LockImpl::Lock() {
  // The ScopedLockAcquireActivity below is relatively expensive and so its
  // actions can become significant due to the very large number of locks
  // that tend to be used throughout the build. To avoid this cost in the
  // vast majority of the calls, simply "try" the lock first and only do the
  // (tracked) blocking call if that fails. Since "try" itself is a system
  // call, and thus also somewhat expensive, don't bother with it unless
  // tracking is actually enabled.
  if (base::debug::GlobalActivityTracker::IsEnabled())
    if (Try())
      return;

  base::debug::ScopedLockAcquireActivity lock_activity(this);
  ::EnterCriticalSection(reinterpret_cast<PCRITICAL_SECTION>(&native_handle_));
}

}  // namespace internal
}  // namespace base
