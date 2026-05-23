/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"

#include <float.h>
#include <intrin.h>
#include <stdlib.h>
#include <windows.h>

#include "mozilla/PlatformConditionVariable.h"
#include "mozilla/PlatformMutex.h"
#include "MutexPlatformData_windows.h"

// Some versions of the Windows SDK have a bug where some interlocked functions
// are not redefined as compiler intrinsics. Fix that for the interlocked
// functions that are used in this file.
#if defined(_MSC_VER) && !defined(InterlockedExchangeAdd)
#  define InterlockedExchangeAdd(addend, value) \
    _InterlockedExchangeAdd((volatile long*)(addend), (long)(value))
#endif

#if defined(_MSC_VER) && !defined(InterlockedIncrement)
#  define InterlockedIncrement(addend) \
    _InterlockedIncrement((volatile long*)(addend))
#endif

typedef struct {
  uint32_t waiting;
#ifdef OS2
  HEV    semaphore;
#else
  HANDLE semaphore;
#endif
} pthread_cond_t;
typedef CRITICAL_SECTION pthread_mutex_t;

int pthread_cond_init(pthread_cond_t *cond, const void *attr)
{
  cond->waiting=0;
  cond->semaphore=CreateSemaphore(NULL,0,0x7FFFFFFF,NULL);
  if (!cond->semaphore)
    return ENOMEM;
  return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	return CloseHandle(cond->semaphore) ? 0 : EINVAL;
}


int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  InterlockedIncrement(&cond->waiting);
  LeaveCriticalSection(mutex);
  WaitForSingleObject(cond->semaphore,INFINITE);
  InterlockedDecrement(&cond->waiting);
  EnterCriticalSection(mutex);
  return 0 ;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           DWORD timeout)
{
  int result;
  InterlockedIncrement(&cond->waiting);
  LeaveCriticalSection(mutex);
  result=WaitForSingleObject(cond->semaphore,timeout);
  InterlockedDecrement(&cond->waiting);
  EnterCriticalSection(mutex);

  return result == WAIT_TIMEOUT ? ETIMEDOUT : 0;
}


int pthread_cond_signal(pthread_cond_t *cond)
{
  long prev_count;
  if (cond->waiting)
    ReleaseSemaphore(cond->semaphore,1,&prev_count);
  return 0;
}


int pthread_cond_broadcast(pthread_cond_t *cond)
{
  long prev_count;
  if (cond->waiting)
    ReleaseSemaphore(cond->semaphore,cond->waiting,&prev_count);
  return 0 ;
}



// Wrapper for native condition variable APIs.
struct mozilla::detail::ConditionVariableImpl::PlatformData {
  pthread_cond_t cv_;
};

mozilla::detail::ConditionVariableImpl::ConditionVariableImpl() {
  pthread_cond_init(&platformData()->cv_, NULL);
}

void mozilla::detail::ConditionVariableImpl::notify_one() {
  pthread_cond_signal(&platformData()->cv_);
}

void mozilla::detail::ConditionVariableImpl::notify_all() {
  pthread_cond_broadcast(&platformData()->cv_);
}

void mozilla::detail::ConditionVariableImpl::wait(MutexImpl& lock) {
  CRITICAL_SECTION* cs = &lock.platformData()->criticalSection;
  bool r = !pthread_cond_wait(&platformData()->cv_, cs);
  MOZ_RELEASE_ASSERT(r);
}

mozilla::CVStatus mozilla::detail::ConditionVariableImpl::wait_for(
    MutexImpl& lock, const mozilla::TimeDuration& rel_time) {
  if (rel_time == mozilla::TimeDuration::Forever()) {
    wait(lock);
    return CVStatus::NoTimeout;
  }

  CRITICAL_SECTION* cs = &lock.platformData()->criticalSection;

  // Note that DWORD is unsigned, so we have to be careful to clamp at 0. If
  // rel_time is Forever, then ToMilliseconds is +inf, which evaluates as
  // greater than UINT32_MAX, resulting in the correct INFINITE wait. We also
  // don't want to round sub-millisecond waits to 0, as that wastes energy (see
  // bug 1437167 comment 6), so we instead round submillisecond waits to 1ms.
  double msecd = rel_time.ToMilliseconds();
  DWORD msec;
  if (msecd < 0.0) {
    msec = 0;
  } else if (msecd > UINT32_MAX) {
    msec = INFINITE;
  } else {
    msec = static_cast<DWORD>(msecd);
    // Round submillisecond waits to 1ms.
    if (msec == 0 && !rel_time.IsZero()) {
      msec = 1;
    }
  }

  BOOL r = !pthread_cond_timedwait(&platformData()->cv_, cs, msec);
  if (r) return CVStatus::NoTimeout;
//MOZ_RELEASE_ASSERT(GetLastError() == ERROR_TIMEOUT);
  return CVStatus::Timeout;
}

mozilla::detail::ConditionVariableImpl::~ConditionVariableImpl() {
  // Native condition variables don't require cleanup.
  pthread_cond_destroy(&platformData()->cv_);
}

inline mozilla::detail::ConditionVariableImpl::PlatformData*
mozilla::detail::ConditionVariableImpl::platformData() {
  static_assert(sizeof platformData_ >= sizeof(PlatformData),
                "platformData_ is too small");
  return reinterpret_cast<PlatformData*>(platformData_);
}
