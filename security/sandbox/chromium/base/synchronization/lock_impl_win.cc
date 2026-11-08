// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/lock_impl.h"

#include <windows.h>

#include "base/synchronization/lock_metrics_recorder.h"

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

void LockImpl::LockInternal() {
  LockMetricsRecorder::ScopedLockAcquisitionTimer timer;
  ::EnterCriticalSection(reinterpret_cast<PCRITICAL_SECTION>(&native_handle_));
}

}  // namespace internal
}  // namespace base
