/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/PlatformMutex.h"

#include <windows.h>

#include "MutexPlatformData_windows.h"

mozilla::detail::MutexImpl::MutexImpl() {
  // This number was adopted from NSPR.
  const static DWORD LockSpinCount = 1500;
  BOOL r =
      InitializeCriticalSectionAndSpinCount(&platformData()->criticalSection, LockSpinCount);
  MOZ_RELEASE_ASSERT(r);
}

mozilla::detail::MutexImpl::~MutexImpl()
{
  DeleteCriticalSection(&platformData()->criticalSection);
}

void mozilla::detail::MutexImpl::lock() {
  EnterCriticalSection(&platformData()->criticalSection);
}

bool mozilla::detail::MutexImpl::tryLock() { return mutexTryLock(); }

bool mozilla::detail::MutexImpl::mutexTryLock() {
  return !!TryEnterCriticalSection(&platformData()->criticalSection);
}

void mozilla::detail::MutexImpl::unlock() {
  LeaveCriticalSection(&platformData()->criticalSection);
}

mozilla::detail::MutexImpl::PlatformData*
mozilla::detail::MutexImpl::platformData() {
  static_assert(sizeof(platformData_) >= sizeof(PlatformData),
                "platformData_ is too small");
  return reinterpret_cast<PlatformData*>(platformData_);
}
