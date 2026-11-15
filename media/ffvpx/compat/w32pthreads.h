/*
 * Copyright (C) 2010-2011 x264 project
 *
 * Authors: Steven Walters <kemuri9@gmail.com>
 *          Pegasys Inc. <http://www.pegasys-inc.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * w32threads to pthreads wrapper
 */

#ifndef COMPAT_W32PTHREADS_H
#define COMPAT_W32PTHREADS_H

/* Build up a pthread-like API using underlying Windows API. Have only static
 * methods so as to not conflict with a potentially linked in pthread-win32
 * library.
 * As most functions here are used without checking return values,
 * only implement return values as necessary. */

#include <windows.h>
#include <process.h>
#include <time.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/fftime.h"
#include "libavutil/wchar_filename.h"

typedef struct w32pthread_t {
    void *handle;
    void *(*func)(void* arg);
    void *arg;
    void *ret;
} *pthread_t;

/* use light weight mutex/condition variable API for Windows Vista and later */
typedef SRWLOCK pthread_mutex_t;
typedef intptr_t pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

#define InitializeCriticalSection(x) InitializeCriticalSectionEx(x, 0, 0)
#define WaitForSingleObject(a, b) WaitForSingleObjectEx(a, b, FALSE)

#define PTHREAD_CANCEL_ENABLE 1
#define PTHREAD_CANCEL_DISABLE 0

#if HAVE_WINRT
#define THREADFUNC_RETTYPE DWORD
#else
#define THREADFUNC_RETTYPE unsigned
#endif

av_unused static THREADFUNC_RETTYPE
__stdcall attribute_align_arg win32thread_worker(void *arg)
{
    pthread_t h = (pthread_t)arg;
    h->ret = h->func(h->arg);
    return 0;
}

av_unused static int pthread_create(pthread_t *thread, const void *unused_attr,
                                    void *(*start_routine)(void*), void *arg)
{
    pthread_t ret;

    ret = (pthread_t)av_mallocz(sizeof(*ret));
    if (!ret)
        return EAGAIN;

    ret->func   = start_routine;
    ret->arg    = arg;
#if HAVE_WINRT
    ret->handle = (void*)CreateThread(NULL, 0, win32thread_worker, ret,
                                      0, NULL);
#else
    ret->handle = (void*)_beginthreadex(NULL, 0, win32thread_worker, ret,
                                        0, NULL);
#endif

    if (!ret->handle) {
        av_free(ret);
        return EAGAIN;
    }

    *thread = ret;

    return 0;
}

av_unused static int pthread_join(pthread_t thread, void **value_ptr)
{
    DWORD ret = WaitForSingleObject(thread->handle, INFINITE);
    if (ret != WAIT_OBJECT_0) {
        if (ret == WAIT_ABANDONED)
            return EINVAL;
        else
            return EDEADLK;
    }
    if (value_ptr)
        *value_ptr = thread->ret;
    CloseHandle(thread->handle);
    av_free(thread);
    return 0;
}

/*++

Copyright (c) 2000  Microsoft Corporation

Module Name:

    pushlock.c

Abstract:

    This module houses routines that do push locking.

    Push locks are capable of being acquired in both shared and exclusive mode.

    Properties include:

    They can not be acquired recursively.
    They are small (the size of a pointer) and can be used when embeded in pagable data.
    Acquire and release is done lock free. On lock contention the waiters are chained
    through the lock and local stack space.

    This is the structure of a push lock:


    E  == Exclusive bit
    W  == Waiters present
    SC == Share count
    P  == Pointer to wait block
    +----------+---+---+
    |    SC    | E | W | E, W are single bits W == 0
    +----------+---+---+

    +--------------+---+
    |      P       | W | W == 1. Pointer is the address of a chain of stack local wait blocks
    +--------------+---+

    The non-contented acquires and releases are trivial. Interlocked operations make the following
    transformations.

    (SC=0,E=0,W=0) === Exclusive acquire ===> (SC=0,E=1,W=0)
    (SC=n,E=0,W=0) === Shared acquire    ===> (SC=n+1,E=0,W=0)

    (SC=0,E=1,W=0) === Exclusive release ===> (SC=0,E=0,W=0)
    (SC=n,E=0,W=0) === Shared release    ===> (SC=n-1,E=0,W=0) n > 0

    Contention causes the acquiring thread to produce a local stack based wait block and to
    enqueue it to the front of the list.

    (SC=n,E=e,W=0) === Exclusive acquire ===> (P=LWB(SSC=n,E=e),W=1) LWB = local wait block,
                                                                     SSC = Saved share count,
                                                                     n > 0 or e == 1.

    (SC=0,E=1,W=0) === Shared acquire    ===> (P=LWB(SSC=0,E=0),W=1) LWB = local wait block,
                                                                     SSC = Saved share count.

    After contention has causes one or more threads to queue up wait blocks releases are more
    complicated. This following rights are granted to a releasing thread (shared or exclusive).

    1) Shared release threads are allowed to search the wait list until they hit a wait block
       with a non-zero share count (this will be a wait block marked exclusive). This thread is
       allowed to use an interlocked operation to decrement this value. If this thread
       transitioned the value to zero then it obtains the rights of an exclusive release thread

    2) Exclusive threads are allowed to search the wait list until they find a continuous chain
       of shared wait blocks or they find the last wait block is an exclusive waiter. This thread
       may then break the chain at this point or update the header to show a single exclusive
       owner or multiple shared owners. Breaking the list can be done with normal assignment
       but updating the header requires interlocked exchange compare.


Author:

    Neill Clift (NeillC) 30-Sep-2000


Revision History:

--*/

// begin_ntosp

//
// Push lock definitions
//
typedef struct _EX_PUSH_LOCK {

#define EX_PUSH_LOCK_WAITING   0x1
#define EX_PUSH_LOCK_EXCLUSIVE 0x2
#define EX_PUSH_LOCK_SHARE_INC 0x4

    union {
        struct {
            ULONG_PTR Waiting : 1;
            ULONG_PTR Exclusive : 1;
            ULONG_PTR Shared : sizeof (ULONG_PTR) * 8 - 2;
        };
        ULONG_PTR Value;
        PVOID Ptr;
    };
} EX_PUSH_LOCK, *PEX_PUSH_LOCK;


//
// This is a block held on the local stack of the waiting threads.
//

typedef  struct _EX_PUSH_LOCK_WAIT_BLOCK *PEX_PUSH_LOCK_WAIT_BLOCK;

typedef struct _EX_PUSH_LOCK_WAIT_BLOCK {
    HANDLE WakeEvent;
    PEX_PUSH_LOCK_WAIT_BLOCK Next;
    PEX_PUSH_LOCK_WAIT_BLOCK Last;
    PEX_PUSH_LOCK_WAIT_BLOCK Previous;
    ULONG ShareCount;
    BOOLEAN Exclusive;
} EX_PUSH_LOCK_WAIT_BLOCK;

// end_ntosp
#define NTKERNELAPI inline

#ifndef FASTCALL
#define FASTCALL    __fastcall
#endif

/*****************************************************************************/

#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT(e)        // get rid of the assert symbol.


//
// Interrupt Request Level (IRQL)
//

typedef UCHAR KIRQL;
typedef KIRQL *PKIRQL;

#ifdef KeRaiseIrql
#undef KeRaiseIrql
#endif
#define KeRaiseIrql(NewLevel, OldLevel)
#ifdef KeLowerIrql
#undef KeLowerIrql
#endif
#define KeLowerIrql(Level)

#pragma hdrstop

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ExBlockPushLock)
#pragma alloc_text(PAGE, ExfAcquirePushLockExclusive)
#pragma alloc_text(PAGE, ExfAcquirePushLockShared)
#pragma alloc_text(PAGE, ExfUnblockPushLock)
#pragma alloc_text(PAGE, ExAllocateCacheAwarePushLock)
#pragma alloc_text(PAGE, ExFreeCacheAwarePushLock)
#pragma alloc_text(PAGE, ExAcquireCacheAwarePushLockExclusive)
#pragma alloc_text(PAGE, ExReleaseCacheAwarePushLockExclusive)
#endif


NTKERNELAPI
VOID
FASTCALL
ExfAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;

    OldValue = *PushLock;
    while (1) {
        //
        // If the lock is already held exclusively/shared or there are waiters then
        // we need to wait.
        //
        if (OldValue.Value == 0) {
            NewValue.Value = OldValue.Value + EX_PUSH_LOCK_EXCLUSIVE;
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
        } else {
            WaitBlock.WakeEvent = CreateEventW ( NULL, FALSE, FALSE, NULL );
            WaitBlock.Exclusive = TRUE;
            WaitBlock.Last = NULL;
            WaitBlock.Previous = NULL;
            //
            // Move the sharecount to our wait block if need be.
            //
            if (OldValue.Waiting) {
                WaitBlock.Next = (PEX_PUSH_LOCK_WAIT_BLOCK)
                                     (OldValue.Value - EX_PUSH_LOCK_WAITING);
                WaitBlock.ShareCount = 0;
            } else {
                WaitBlock.Next = NULL;
                WaitBlock.ShareCount = (ULONG) OldValue.Shared;
            }
            NewValue.Ptr = ((PUCHAR) &WaitBlock) + EX_PUSH_LOCK_WAITING;
            ASSERT ((NewValue.Value & EX_PUSH_LOCK_WAITING) != 0);
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                WaitForSingleObject(WaitBlock.WakeEvent, INFINITE);
                ASSERT ((WaitBlock.ShareCount == 0) && (WaitBlock.Next == NULL));
                CloseHandle(WaitBlock.WakeEvent);
                break;
            }
            CloseHandle(WaitBlock.WakeEvent);

        }
        OldValue = NewValue;
    }
}

NTKERNELAPI
VOID
FASTCALL
ExfAcquirePushLockShared (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock shared

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;

    OldValue = *PushLock;
    while (1) {
        //
        // If the lock is already held exclusively or there are waiters then we need to wait
        //
        if (OldValue.Exclusive || OldValue.Waiting) {
            WaitBlock.WakeEvent = CreateEventW ( NULL, FALSE, FALSE, NULL );
            WaitBlock.Exclusive = 0;
            WaitBlock.ShareCount = 0;
            WaitBlock.Last = NULL;
            WaitBlock.Previous = NULL;
            //
            // Chain the next block to us if there is one.
            //
            if (OldValue.Waiting) {
                WaitBlock.Next = (PEX_PUSH_LOCK_WAIT_BLOCK)
                                     (OldValue.Value - EX_PUSH_LOCK_WAITING);
            } else {
                WaitBlock.Next = NULL;
            }
            NewValue.Ptr = ((PUCHAR) &WaitBlock) + EX_PUSH_LOCK_WAITING;
            ASSERT ((NewValue.Value & EX_PUSH_LOCK_WAITING) != 0);
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                WaitForSingleObject(WaitBlock.WakeEvent, INFINITE);

                ASSERT (WaitBlock.ShareCount == 0);
                CloseHandle(WaitBlock.WakeEvent);
                break;
            }
            CloseHandle(WaitBlock.WakeEvent);

        } else {
            //
            // We only have shared accessors at the moment. We can just update the lock to include this thread.
            //
            NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
            ASSERT (!(NewValue.Waiting || NewValue.Exclusive));
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
        }
        OldValue = NewValue;
    }
}


NTKERNELAPI
VOID
FASTCALL
ExfReleasePushLock (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired exclusively or shared

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock, NextWaitBlock, ReleaseWaitList, Previous;
    PEX_PUSH_LOCK_WAIT_BLOCK LastWaitBlock, FirstWaitBlock;
    ULONG ShareCount;
    KIRQL OldIrql;

    OldValue = *PushLock;
    while (1) {
        if (!OldValue.Waiting) {
            //
            // Either we hold the lock exclusive or shared but not both.
            //
            ASSERT (OldValue.Exclusive ^ (OldValue.Shared > 0));

            //
            // We must hold the lock exclusive or shared. We make the assuption that
            // the exclusive bit is just below the share count here.
            //
            NewValue.Value = (OldValue.Value - EX_PUSH_LOCK_EXCLUSIVE) &
                             ~EX_PUSH_LOCK_EXCLUSIVE;
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
            //
            // Either we gained a new waiter or another shared owner arrived or left
            //
            ASSERT (NewValue.Waiting || (NewValue.Shared > 0 && !NewValue.Exclusive));
            OldValue = NewValue;
        } else {
            //
            // There are waiters chained to the lock. We have to release the share count,
            // last exclusive or last chain of shared waiters.
            //
            WaitBlock = (PEX_PUSH_LOCK_WAIT_BLOCK)
                           (OldValue.Value - EX_PUSH_LOCK_WAITING);

            FirstWaitBlock = WaitBlock;
            ReleaseWaitList = WaitBlock;
            Previous = NULL;
            LastWaitBlock = NULL;
            ShareCount = 0;
            do {

                if (WaitBlock->Last != NULL) {
                    LastWaitBlock = WaitBlock;
                    WaitBlock = WaitBlock->Last;
                    Previous = WaitBlock->Previous;
                    ReleaseWaitList = WaitBlock;
                    ASSERT (WaitBlock->Next == NULL);
                    ASSERT (Previous != NULL);
                    ShareCount = 0;
                }

                if (WaitBlock->Exclusive) {
                    //
                    // This is an exclusive waiter. If this was the first exclusive waited to a shared acquire
                    // then it will have the saved share count. If we acquired the lock shared then the count
                    // must contain a bias for this thread. Release that and if we are not the last shared
                    // accessor then exit. A later shared release thread will wake the exclusive
                    // waiter.
                    //
                    if (WaitBlock->ShareCount != 0) {
                        if (InterlockedDecrement ((PLONG)&WaitBlock->ShareCount) != 0) {
                            return;
                        }
                    }
                    //
                    // Reset count of share acquires waiting.
                    //
                    ShareCount = 0;
                } else {
                    //
                    // This is a shared waiter. Record the number of these to update the head or the
                    // previous exclusive waiter.
                    //
                    ShareCount++;
                }
                NextWaitBlock = WaitBlock->Next;
                if (NextWaitBlock != NULL) {

                    NextWaitBlock->Previous = WaitBlock;

                    if (NextWaitBlock->Exclusive) {
                        //
                        // The next block is exclusive. This may be the entry to free.
                        //
                        Previous = WaitBlock;
                        ReleaseWaitList = NextWaitBlock;
                    } else {
                        //
                        // The next block is shared. If the chain start is exclusive then skip to this one
                        // as the exclusive isn't the thread we will wake up.
                        //
                        if (ReleaseWaitList->Exclusive) {
                            Previous = WaitBlock;
                            ReleaseWaitList = NextWaitBlock;
                        }
                    }
                }

                WaitBlock = NextWaitBlock;
            } while (WaitBlock != NULL);

            //
            // If our release chain is everything then we have to update the header
            //
            if (Previous == NULL) {
                NewValue.Value = 0;
                NewValue.Exclusive = ReleaseWaitList->Exclusive;
                NewValue.Shared = ShareCount;
                ASSERT (((ShareCount > 0) ^ (ReleaseWaitList->Exclusive)) && !NewValue.Waiting);

                NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                                  NewValue.Ptr,
                                                                  OldValue.Ptr);
                if (NewValue.Ptr != OldValue.Ptr) {
                    //
                    // We are releasing so we could have only gained another waiter
                    //
                    ASSERT (NewValue.Waiting);
                    OldValue = NewValue;
                    continue;
                }
            } else {

                if (LastWaitBlock != NULL) {
                    LastWaitBlock->Last = NULL;
                }
                //
                // Truncate the chain at this position and save the share count for all the shared owners to
                // decrement later.
                //
                Previous->Next = NULL;
                ASSERT (Previous->ShareCount == 0);
                Previous->ShareCount = ShareCount;

                //
                // Add a pointer to make future searches faster
                //
                if (Previous->Exclusive && FirstWaitBlock != Previous) {
                    FirstWaitBlock->Last = Previous;
                    ASSERT (Previous->Previous != NULL);
                }
                //
                // We are either releasing multiple share accessors or a single exclusive
                //
                ASSERT ((ShareCount > 0) ^ ReleaseWaitList->Exclusive);
            }

            //
            // If we are waking more than one thread then raise to DPC level to prevent us
            // getting rescheduled part way through the operation
            //

            OldIrql = DISPATCH_LEVEL;
            if (ShareCount > 1) {
                KeRaiseIrql (DISPATCH_LEVEL, &OldIrql);
            }

            //
            //
            // Release the chain of threads we located.
            //
            do {
                NextWaitBlock = ReleaseWaitList->Next;
                //
                // All the chain should have the same type (Exclusive/Shared).
                //
                ASSERT (NextWaitBlock == NULL || (ReleaseWaitList->Exclusive == NextWaitBlock->Exclusive));
                ASSERT (!ReleaseWaitList->Exclusive || (ReleaseWaitList->ShareCount == 0));
                SetEvent (ReleaseWaitList->WakeEvent);
                ReleaseWaitList = NextWaitBlock;
            } while (ReleaseWaitList != NULL);

            if (OldIrql != DISPATCH_LEVEL) {
                KeLowerIrql (OldIrql);
            }


            break;
        }
    }
}
VOID
FORCEINLINE
ExInitializePushLock (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Initialize a push lock structure

Arguments:

    PushLock - Push lock to be initialized

Return Value:

    None

--*/
{
    PushLock->Value = 0;
}
// begin_ntosp

VOID
FORCEINLINE
ExAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE,
                                           NULL) != NULL) {
        ExfAcquirePushLockExclusive (PushLock);
    }
}
VOID
FORCEINLINE
ExReleasePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired exclusively

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    ASSERT (PushLock->Value & (EX_PUSH_LOCK_WAITING|EX_PUSH_LOCK_EXCLUSIVE));

    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NULL,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE) != (PVOID)EX_PUSH_LOCK_EXCLUSIVE) {
        ExfReleasePushLock (PushLock);
    }
}
static inline int pthread_mutex_init(pthread_mutex_t *m, void* attr)
{
    ExInitializePushLock((PEX_PUSH_LOCK)m);
    return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    /* Unlocked SWR locks use no resources */
    return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    ExAcquirePushLockExclusive((PEX_PUSH_LOCK)m);
    return 0;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    ExReleasePushLockExclusive((PEX_PUSH_LOCK)m);
    return 0;
}

#if 0
typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT

av_unused static int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    BOOL pending = FALSE;
    InitOnceBeginInitialize(once_control, 0, &pending, NULL);
    if (pending)
        init_routine();
    InitOnceComplete(once_control, 0, NULL);
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t *cond, const void *unused_attr)
{
    InitializeConditionVariable(cond);
    return 0;
}

/* native condition variables do not destroy */
static inline int pthread_cond_destroy(pthread_cond_t *cond)
{
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    SleepConditionVariableSRW(cond, mutex, INFINITE, 0);
    return 0;
}

static inline int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                         const struct timespec *abstime)
{
    int64_t abs_milli = abstime->tv_sec * 1000LL + abstime->tv_nsec / 1000000;
    DWORD t = av_clip64(abs_milli - av_gettime() / 1000, 0, UINT32_MAX);

    if (!SleepConditionVariableSRW(cond, mutex, t, 0)) {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT)
            return ETIMEDOUT;
        else
            return EINVAL;
    }
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(cond);
    return 0;
}

static inline int pthread_setcancelstate(int state, int *oldstate)
{
    return 0;
}

#else // _WIN32_WINNT < 0x0600

/* atomic init state of dynamically loaded functions */
static LONG w32thread_init_state = 0;
static av_unused void w32thread_init(void);

/* for pre-Windows 6.0 platforms, define INIT_ONCE struct,
 * compatible to the one used in the native API */

typedef union pthread_once_t  {
    void * Ptr;    ///< For the Windows 6.0+ native functions
    LONG state;    ///< For the pre-Windows 6.0 compat code
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0}

/* function pointers to init once API on windows 6.0+ kernels */
static BOOL (WINAPI *initonce_begin)(pthread_once_t *lpInitOnce, DWORD dwFlags, BOOL *fPending, void **lpContext);
static BOOL (WINAPI *initonce_complete)(pthread_once_t *lpInitOnce, DWORD dwFlags, void *lpContext);

/* pre-Windows 6.0 compat using a spin-lock */
static inline void w32thread_once_fallback(LONG volatile *state, void (*init_routine)(void))
{
    switch (InterlockedCompareExchange(state, 1, 0)) {
    /* Initial run */
    case 0:
        init_routine();
        InterlockedExchange(state, 2);
        break;
    /* Another thread is running init */
    case 1:
        while (1) {
            MemoryBarrier();
            if (*state == 2)
                break;
            Sleep(0);
        }
        break;
    /* Initialization complete */
    case 2:
        break;
    }
}

static av_unused int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    w32thread_once_fallback(&w32thread_init_state, w32thread_init);

    /* Use native functions on Windows 6.0+ */
    if (initonce_begin && initonce_complete) {
        BOOL pending = FALSE;
        initonce_begin(once_control, 0, &pending, NULL);
        if (pending)
            init_routine();
        initonce_complete(once_control, 0, NULL);
        return 0;
    }

    w32thread_once_fallback(&once_control->state, init_routine);
    return 0;
}

/* function pointers to conditional variable API on windows 6.0+ kernels */
static void (WINAPI *cond_broadcast)(pthread_cond_t *cond);
static void (WINAPI *cond_init)(pthread_cond_t *cond);
static void (WINAPI *cond_signal)(pthread_cond_t *cond);
static BOOL (WINAPI *cond_wait)(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                DWORD milliseconds);

/*
   Copyright (c) 2011-2016  mingw-w64 project

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

/*
 * Posix Condition Variables for Microsoft Windows.
 * 22-9-2010 Partly based on the ACE framework implementation.
 */

/*
   Copyright (c) 2011-2016  mingw-w64 project

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

#ifndef WIN_PTHREADS_COND_H
#define WIN_PTHREADS_COND_H

#define CHECK_COND(c)                                                   \
    do {                                                                \
        if (!(c) || !*c || (*c == PTHREAD_COND_INITIALIZER)             \
            || ( ((cond_t *)(*c))->valid != (unsigned int)LIFE_COND ) ) \
            return EINVAL;                                              \
    } while (0)

#define LIFE_COND 0xC0BAB1FD
#define DEAD_COND 0xC0DEADBF

#define STATIC_COND_INITIALIZER(x)                ((pthread_cond_t)(x) == ((pthread_cond_t)PTHREAD_COND_INITIALIZER))

typedef struct cond_t cond_t;
struct cond_t
{
    unsigned int valid;
    int busy;
    LONG waiters_count_; /* Number of waiting threads.  */
    LONG waiters_count_unblock_; /* Number of waiting threads whitch can be unblocked.  */
    LONG waiters_count_gone_; /* Number of waiters which are gone.  */
    CRITICAL_SECTION waiters_count_lock_; /* Serialize access to <waiters_count_>.  */
    CRITICAL_SECTION waiters_q_lock_; /* Serialize access to sema_q.  */
    LONG value_q;
    CRITICAL_SECTION waiters_b_lock_; /* Serialize access to sema_b.  */
    LONG value_b;
    HANDLE sema_q; /* Semaphore used to queue up threads waiting for the condition to
                 become signaled.  */
    HANDLE sema_b; /* Semaphore used to queue up threads waiting for the condition which
                 became signaled.  */
};

#endif
#include <malloc.h>
#include <stdio.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define WINPTHREAD_COND_DECL WINPTHREAD_API

#define __pthread_shallcancel() (0)

static int do_sema_b_wait (HANDLE sema, int nointerrupt, DWORD timeout,CRITICAL_SECTION *cs, LONG *val);
static int do_sema_b_release(HANDLE sema, LONG count,CRITICAL_SECTION *cs, LONG *val);
static void cleanup_wait(void *arg);

typedef struct sCondWaitHelper {
    cond_t *c;
    pthread_mutex_t *external_mutex;
    int *r;
} sCondWaitHelper;

static int do_sema_b_wait_intern (HANDLE sema, int nointerrupt, DWORD timeout);

static pthread_mutex_t cond_locked = PTHREAD_MUTEX_INITIALIZER;

static int
pthread_cond_init (pthread_cond_t *c, const void *unused_attr)
{
  cond_t *_c;
  int r = 0;

  if (!c)
    return EINVAL;
  if ((_c = calloc(1, sizeof(*_c))) == NULL)
    return ENOMEM;

  _c->valid  = DEAD_COND;
  _c->busy = 0;
  _c->waiters_count_ = 0;
  _c->waiters_count_gone_ = 0;
  _c->waiters_count_unblock_ = 0;

  _c->sema_q = CreateSemaphore (NULL,       /* no security */
      0,          /* initially 0 */
      0x7fffffff, /* max count */
      NULL);      /* unnamed  */
  _c->sema_b =  CreateSemaphore (NULL,       /* no security */
      0,          /* initially 0 */
      0x7fffffff, /* max count */
      NULL);
  if (_c->sema_q == NULL || _c->sema_b == NULL) {
      if (_c->sema_q != NULL)
        CloseHandle (_c->sema_q);
      if (_c->sema_b != NULL)
        CloseHandle (_c->sema_b);
      free (_c);
      r = EAGAIN;
  } else {
      InitializeCriticalSectionAndSpinCount(&_c->waiters_count_lock_, 2000);
      InitializeCriticalSectionAndSpinCount(&_c->waiters_b_lock_, 2000);
      InitializeCriticalSectionAndSpinCount(&_c->waiters_q_lock_, 2000);
      _c->value_q = 0;
      _c->value_b = 1;
  }
  if (!r)
    {
      _c->valid = LIFE_COND;
      *c = (pthread_cond_t)_c;
    }
  else
    *c = (pthread_cond_t)NULL;
  return r;
}

static int
cond_static_init (pthread_cond_t *c)
{
  int r = 0;

  pthread_mutex_lock (&cond_locked);
  if (c == NULL)
    r = EINVAL;
  else if (*c == 0)
    r = pthread_cond_init (c, NULL);
  else
    /* We assume someone was faster ... */
    r = 0;
  pthread_mutex_unlock (&cond_locked);
  return r;
}

static int
pthread_cond_destroy (pthread_cond_t *c)
{
  cond_t *_c;
  int r;
  if (!c)
    return EINVAL;
  if (*c == 0)
    {
      pthread_mutex_lock (&cond_locked);
      if (*c == 0)
      {
        *c = (pthread_cond_t)NULL;
        r = 0;
      }
      else
        r = EBUSY;
      pthread_mutex_unlock (&cond_locked);
      return r;
    }
  _c = (cond_t *) *c;
  r = do_sema_b_wait(_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
  if (r != 0)
    return r;
  if (!TryEnterCriticalSection (&_c->waiters_count_lock_))
    {
       do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
       return EBUSY;
    }
  if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
      r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (!r) r = EBUSY;
      LeaveCriticalSection(&_c->waiters_count_lock_);
      return r;
    }
  *c = (pthread_cond_t)NULL;
  do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);

  if (!CloseHandle (_c->sema_q) && !r)
    r = EINVAL;
  if (!CloseHandle (_c->sema_b) && !r)
    r = EINVAL;
  LeaveCriticalSection (&_c->waiters_count_lock_);
  DeleteCriticalSection(&_c->waiters_count_lock_);
  DeleteCriticalSection(&_c->waiters_b_lock_);
  DeleteCriticalSection(&_c->waiters_q_lock_);
  _c->valid  = DEAD_COND;
  free(_c);
  return 0;
}

static int
pthread_cond_signal (pthread_cond_t *c)
{
  cond_t *_c;
  int r;

  if (!c)
    return EINVAL;
  _c = (cond_t *)*c;
  if (_c == (cond_t *)0)
    return 0;
  else if (_c->valid != (unsigned int)LIFE_COND)
    return EINVAL;

  EnterCriticalSection (&_c->waiters_count_lock_);
  /* If there aren't any waiters, then this is a no-op.   */
  if (_c->waiters_count_unblock_ != 0)
    {
      if (_c->waiters_count_ == 0)
      {
        LeaveCriticalSection (&_c->waiters_count_lock_);
        /* pthread_testcancel(); */
        return 0;
      }
      _c->waiters_count_ -= 1;
      _c->waiters_count_unblock_ += 1;
    }
  else if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
      r = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
      if (r != 0)
      {
        LeaveCriticalSection (&_c->waiters_count_lock_);
        /* pthread_testcancel(); */
        return r;
      }
      if (_c->waiters_count_gone_ != 0)
      {
        _c->waiters_count_ -= _c->waiters_count_gone_;
        _c->waiters_count_gone_ = 0;
      }
      _c->waiters_count_ -= 1;
      _c->waiters_count_unblock_ = 1;
    }
  else
    {
      LeaveCriticalSection (&_c->waiters_count_lock_);
      /* pthread_testcancel(); */
      return 0;
    }
  LeaveCriticalSection (&_c->waiters_count_lock_);
  r = do_sema_b_release(_c->sema_q, 1,&_c->waiters_q_lock_,&_c->value_q);
  /* pthread_testcancel(); */
  return r;
}

static int
pthread_cond_broadcast (pthread_cond_t *c)
{
  cond_t *_c;
  int r;
  int relCnt = 0;

  if (!c)
    return EINVAL;
  _c = (cond_t *)*c;
  if (_c == (cond_t*)0)
    return 0;
  else if (_c->valid != (unsigned int)LIFE_COND)
    return EINVAL;

  EnterCriticalSection (&_c->waiters_count_lock_);
  /* If there aren't any waiters, then this is a no-op.   */
  if (_c->waiters_count_unblock_ != 0)
    {
      if (_c->waiters_count_ == 0)
      {
        LeaveCriticalSection (&_c->waiters_count_lock_);
        /* pthread_testcancel(); */
        return 0;
      }
      relCnt = _c->waiters_count_;
      _c->waiters_count_ = 0;
      _c->waiters_count_unblock_ += relCnt;
    }
  else if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
      r = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
      if (r != 0)
      {
        LeaveCriticalSection (&_c->waiters_count_lock_);
        /* pthread_testcancel(); */
        return r;
      }
      if (_c->waiters_count_gone_ != 0)
      {
        _c->waiters_count_ -= _c->waiters_count_gone_;
        _c->waiters_count_gone_ = 0;
      }
      relCnt = _c->waiters_count_;
      _c->waiters_count_ = 0;
      _c->waiters_count_unblock_ = relCnt;
    }
  else
    {
      LeaveCriticalSection (&_c->waiters_count_lock_);
      /* pthread_testcancel(); */
      return 0;
    }
  LeaveCriticalSection (&_c->waiters_count_lock_);
  r = do_sema_b_release(_c->sema_q, relCnt,&_c->waiters_q_lock_,&_c->value_q);
  /* pthread_testcancel(); */
  return r;
}

static int
pthread_cond_wait (pthread_cond_t *c, pthread_mutex_t *external_mutex)
{
  sCondWaitHelper ch;
  cond_t *_c;
  int r;

  /* pthread_testcancel(); */

  if (!c)
    return EINVAL;
  _c = (cond_t *)*c;
  if (*c == 0)
  {
    r = cond_static_init(c);
    if (r != 0 && r != EBUSY)
      return r;
    _c = (cond_t *) *c;
  } else if (_c->valid != (unsigned int)LIFE_COND)
    return EINVAL;

tryagain:
  r = do_sema_b_wait (_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
  if (r != 0)
    return r;

  if (!TryEnterCriticalSection (&_c->waiters_count_lock_))
  {
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return r;
    Sleep(0);
    goto tryagain;
  }

  _c->waiters_count_++;
  LeaveCriticalSection(&_c->waiters_count_lock_);
  r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
  if (r != 0)
    return r;

  ch.c = _c;
  ch.r = &r;
  ch.external_mutex = external_mutex;

  r = pthread_mutex_unlock(external_mutex);
  if (!r)
    r = do_sema_b_wait (_c->sema_q, 0, INFINITE,&_c->waiters_q_lock_,&_c->value_q);

  cleanup_wait((void *) &ch);
  return r;
}

static int
pthread_cond_timedwait_impl (pthread_cond_t *c, pthread_mutex_t *external_mutex, const struct timespec *abstime, int rel)
{
  sCondWaitHelper ch;
  DWORD dwr;
  int r;
  cond_t *_c;

  /* pthread_testcancel(); */

  if (!c)
    return EINVAL;
  _c = (cond_t *)*c;
  if (_c == (cond_t *)0)
  {
    r = cond_static_init(c);
    if (r && r != EBUSY)
      return r;
    _c = (cond_t *) *c;
  } else if ((_c)->valid != (unsigned int)LIFE_COND)
    return EINVAL;

    int64_t abs_milli = abstime->tv_sec * 1000LL + abstime->tv_nsec / 1000000;
    dwr = av_clip64(abs_milli - av_gettime() / 1000, 0, UINT32_MAX);

tryagain:
  r = do_sema_b_wait (_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
  if (r != 0)
    return r;

  if (!TryEnterCriticalSection (&_c->waiters_count_lock_))
  {
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return r;
    Sleep(0);
    goto tryagain;
  }

  _c->waiters_count_++;
  LeaveCriticalSection(&_c->waiters_count_lock_);
  r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
  if (r != 0)
    return r;

  ch.c = _c;
  ch.r = &r;
  ch.external_mutex = external_mutex;
  {

    r = pthread_mutex_unlock(external_mutex);
    if (!r)
      r = do_sema_b_wait (_c->sema_q, 0, dwr,&_c->waiters_q_lock_,&_c->value_q);

    cleanup_wait((void *) &ch);
  }
  return r;
}

static int
pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime)
{
  return pthread_cond_timedwait_impl(c, m, abstime, 0);
}

static void
cleanup_wait (void *arg)
{
  int n, r;
  sCondWaitHelper *ch = (sCondWaitHelper *) arg;
  cond_t *_c;

  _c = ch->c;
  EnterCriticalSection (&_c->waiters_count_lock_);
  n = _c->waiters_count_unblock_;
  if (n != 0)
    _c->waiters_count_unblock_ -= 1;
  else if ((INT_MAX/2) - 1 == _c->waiters_count_gone_)
  {
    _c->waiters_count_gone_ += 1;
    r = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
    {
      LeaveCriticalSection(&_c->waiters_count_lock_);
      ch->r[0] = r;
      return;
    }
    _c->waiters_count_ -= _c->waiters_count_gone_;
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
    {
      LeaveCriticalSection(&_c->waiters_count_lock_);
      ch->r[0] = r;
      return;
    }
    _c->waiters_count_gone_ = 0;
  }
  else
    _c->waiters_count_gone_ += 1;
  LeaveCriticalSection (&_c->waiters_count_lock_);

  if (n == 1)
  {
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
    {
      ch->r[0] = r;
      return;
    }
  }
  r = pthread_mutex_lock(ch->external_mutex);
  if (r != 0)
    ch->r[0] = r;
}

static int
do_sema_b_wait (HANDLE sema, int nointerrupt, DWORD timeout,CRITICAL_SECTION *cs, LONG *val)
{
  int r;
  LONG v;
  EnterCriticalSection(cs);
  InterlockedDecrement(val);
  v = val[0];
  LeaveCriticalSection(cs);
  if (v >= 0)
    return 0;
  r = do_sema_b_wait_intern (sema, nointerrupt, timeout);
  EnterCriticalSection(cs);
  if (r != 0)
    InterlockedIncrement(val);
  LeaveCriticalSection(cs);
  return r;
}

static int
do_sema_b_wait_intern (HANDLE sema, int nointerrupt, DWORD timeout)
{
  HANDLE arr[2];
  DWORD maxH = 1;
  int r = 0;
  DWORD res, dt;
  if (nointerrupt == 1)
  {
    res = WaitForSingleObject(sema, timeout);
    switch (res) {
    case WAIT_TIMEOUT:
        r = ETIMEDOUT;
        break;
    case WAIT_ABANDONED:
        r = EPERM;
        break;
    case WAIT_OBJECT_0:
        break;
    default:
        /*We can only return EINVAL though it might not be posix compliant  */
        r = EINVAL;
    }
    if (r != 0 && r != EINVAL && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
      r = 0;
    return r;
  }
  arr[0] = sema;
  arr[1] = (HANDLE) 0;//pthread_getevent ();
  if (arr[1] != NULL) maxH += 1;
  if (maxH == 2)
  {
redo:
      res = WaitForMultipleObjects(maxH, arr, 0, timeout);
      switch (res) {
      case WAIT_TIMEOUT:
          r = ETIMEDOUT;
          break;
      case (WAIT_OBJECT_0 + 1):
          ResetEvent(arr[1]);
          if (nointerrupt != 2)
            {
            /* pthread_testcancel(); */
            return EINVAL;
            }
          /* pthread_testcancel(); */
          goto redo;
      case WAIT_ABANDONED:
          r = EPERM;
          break;
      case WAIT_OBJECT_0:
          r = 0;
          break;
      default:
          /*We can only return EINVAL though it might not be posix compliant  */
          r = EINVAL;
      }
      if (r != 0 && r != EINVAL && WaitForSingleObject(arr[0], 0) == WAIT_OBJECT_0)
        r = 0;
      if (r != 0 && nointerrupt != 2 && __pthread_shallcancel ())
        return EINVAL;
      return r;
  }
  if (timeout == INFINITE)
  {
    do {
      res = WaitForSingleObject(sema, 40);
      switch (res) {
      case WAIT_TIMEOUT:
          r = ETIMEDOUT;
          break;
      case WAIT_ABANDONED:
          r = EPERM;
          break;
      case WAIT_OBJECT_0:
          r = 0;
          break;
      default:
          /*We can only return EINVAL though it might not be posix compliant  */
          r = EINVAL;
      }
      if (r != 0 && __pthread_shallcancel ())
      {
        if (nointerrupt != 2)
          /* pthread_testcancel(); */;
        return EINVAL;
      }
    } while (r == ETIMEDOUT);
    if (r != 0 && r != EINVAL && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
      r = 0;
    return r;
  }
  dt = 20;
  do {
    if (dt > timeout) dt = timeout;
    res = WaitForSingleObject(sema, dt);
    switch (res) {
    case WAIT_TIMEOUT:
        r = ETIMEDOUT;
        break;
    case WAIT_ABANDONED:
        r = EPERM;
        break;
    case WAIT_OBJECT_0:
        r = 0;
        break;
    default:
        /*We can only return EINVAL though it might not be posix compliant  */
        r = EINVAL;
    }
    timeout -= dt;
    if (timeout != 0 && r != 0 && __pthread_shallcancel ())
      return EINVAL;
  } while (r == ETIMEDOUT && timeout != 0);
  if (r != 0 && r == ETIMEDOUT && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
    r = 0;
  if (r != 0 && nointerrupt != 2)
    /* pthread_testcancel(); */;
  return r;
}

static int
do_sema_b_release(HANDLE sema, LONG count,CRITICAL_SECTION *cs, LONG *val)
{
  int wc;
  EnterCriticalSection(cs);
  if (((long long) val[0] + (long long) count) > (long long) 0x7fffffffLL)
  {
    LeaveCriticalSection(cs);
    return ERANGE;
  }
  wc = -val[0];
  InterlockedExchangeAdd(val, count);
  if (wc <= 0 || ReleaseSemaphore(sema, (wc < count ? wc : count), NULL))
  {
    LeaveCriticalSection(cs);
    return 0;
  }
  InterlockedExchangeAdd(val, -count);
  LeaveCriticalSection(cs);
  return EINVAL;
}
#endif

static av_unused void w32thread_init(void)
{
#if _WIN32_WINNT < 0x0600
    HMODULE kernel_dll = GetModuleHandleW(L"kernel32.dll");
    /* if one is available, then they should all be available */
    cond_init      = (void (WINAPI*)(pthread_cond_t *))
        GetProcAddress(kernel_dll, "InitializeConditionVariable");
    cond_broadcast = (void (WINAPI*)(pthread_cond_t *))
        GetProcAddress(kernel_dll, "WakeAllConditionVariable");
    cond_signal    = (void (WINAPI*)(pthread_cond_t *))
        GetProcAddress(kernel_dll, "WakeConditionVariable");
    cond_wait      = (BOOL (WINAPI*)(pthread_cond_t *, pthread_mutex_t *, DWORD))
        GetProcAddress(kernel_dll, "SleepConditionVariableCS");
    initonce_begin = (BOOL (WINAPI*)(pthread_once_t *, DWORD, BOOL *, void **))
        GetProcAddress(kernel_dll, "InitOnceBeginInitialize");
    initonce_complete = (BOOL (WINAPI*)(pthread_once_t *, DWORD, void *))
        GetProcAddress(kernel_dll, "InitOnceComplete");
#endif

}

static inline int win32_thread_setname(const char *name)
{
#if !HAVE_UWP
    typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);

    // Although SetThreadDescription lives in kernel32.dll, on Windows Server 2016,
    // Windows 10 LTSB 2016 and Windows 10 version 1607, it was only available in
    // kernelbase.dll. So, load it from there for maximum coverage.
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    if (!kernelbase)
        return AVERROR(ENOSYS);

    SetThreadDescriptionFn pSetThreadDescription =
        (SetThreadDescriptionFn)GetProcAddress(kernelbase, "SetThreadDescription");
    if (!pSetThreadDescription)
        return AVERROR(ENOSYS);

    wchar_t *wname;
    if (utf8towchar(name, &wname) < 0)
        return AVERROR(ENOMEM);

    HRESULT hr = pSetThreadDescription(GetCurrentThread(), wname);
    av_free(wname);
    return SUCCEEDED(hr) ? 0 : AVERROR(EINVAL);
#else
    // UWP is not supported because we cannot use LoadLibrary/GetProcAddress to
    // detect the availability of the SetThreadDescription API. There is a small
    // gap in Windows builds 1507-1607 where it was not available. UWP allows
    // querying the availability of APIs with QueryOptionalDelayLoadedAPI, but it
    // requires /DELAYLOAD:kernel32.dll during linking, and we cannot enforce that.
    return AVERROR(ENOSYS);
#endif
}

#endif /* COMPAT_W32PTHREADS_H */
