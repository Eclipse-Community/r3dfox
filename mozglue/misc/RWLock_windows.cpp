/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XP_WIN
#  error This file should only be compiled on Windows.
#endif

#include "mozilla/PlatformRWLock.h"

#include <windows.h>

#define NativeHandle(m) (reinterpret_cast<SRWLOCK*>(&m))

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

#define NTKERNELAPI

#ifndef FASTCALL
#define FASTCALL
#endif

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

BOOLEAN
FORCEINLINE
ExTryAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Try and acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    BOOLEAN - TRUE: Acquire was successfull, FALSE: Lock was already acquired

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE,
                                           NULL) == NULL) {
        return TRUE;
    } else {
        return FALSE;
    }
}

VOID
FORCEINLINE
ExAcquirePushLockShared (
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

    OldValue = *PushLock;
    OldValue.Value &= ~(EX_PUSH_LOCK_EXCLUSIVE | EX_PUSH_LOCK_WAITING);
    NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfAcquirePushLockShared (PushLock);
    }
}

VOID
FORCEINLINE
ExAcquirePushLockSharedAssumeNoOwner (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock shared making the assumption that its not currently owned.

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_SHARE_INC,
                                           NULL) != NULL) {
        ExfAcquirePushLockShared (PushLock);
    }
}

BOOLEAN
FORCEINLINE
ExTryConvertPushLockSharedToExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Attempts to convert a shared acquire to exclusive. If other sharers or waiters are present
    the function fails.

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    BOOLEAN - TRUE: Conversion worked ok, FALSE: The conversion could not be achieved

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr, (PVOID) EX_PUSH_LOCK_EXCLUSIVE,
                                           (PVOID) EX_PUSH_LOCK_SHARE_INC) ==
                                               (PVOID)EX_PUSH_LOCK_SHARE_INC) {
        return TRUE;
    } else {
        return FALSE;
    }
}

VOID
FORCEINLINE
ExReleasePushLock (
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

    OldValue = *PushLock;
    OldValue.Value &= ~EX_PUSH_LOCK_WAITING;
    NewValue.Value = (OldValue.Value - EX_PUSH_LOCK_EXCLUSIVE) &
                         ~EX_PUSH_LOCK_EXCLUSIVE;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfReleasePushLock (PushLock);
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

VOID
FORCEINLINE
ExReleasePushLockShared (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired shared

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;

    OldValue = *PushLock;
    ASSERT (OldValue.Waiting || !OldValue.Exclusive);
    OldValue.Value &= ~EX_PUSH_LOCK_WAITING;
    NewValue.Value = OldValue.Value - EX_PUSH_LOCK_SHARE_INC;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfReleasePushLock (PushLock);
    }
}

mozilla::detail::RWLockImpl::RWLockImpl() {
  static_assert(sizeof(SRWLOCK) <= sizeof(mRWLock), "SRWLOCK is too big!");
  ExInitializePushLock((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

mozilla::detail::RWLockImpl::~RWLockImpl() {}

bool mozilla::detail::RWLockImpl::tryReadLock() {
  ExAcquirePushLockShared((PEX_PUSH_LOCK)NativeHandle(mRWLock)); return true;
}

void mozilla::detail::RWLockImpl::readLock() {
  ExAcquirePushLockShared((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

void mozilla::detail::RWLockImpl::readUnlock() {
  ExReleasePushLockShared((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

bool mozilla::detail::RWLockImpl::tryWriteLock() {
  return ExTryAcquirePushLockExclusive((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

void mozilla::detail::RWLockImpl::writeLock() {
  ExAcquirePushLockExclusive((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

void mozilla::detail::RWLockImpl::writeUnlock() {
  ExReleasePushLockExclusive((PEX_PUSH_LOCK)NativeHandle(mRWLock));
}

#undef NativeHandle
