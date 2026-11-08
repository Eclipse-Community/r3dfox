/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"
#include "mozilla/NativeNt.h"
#include "mozilla/Types.h"
#include "mozilla/WindowsDllBlocklist.h"

#include "DllBlocklist.h"
#include "LoaderPrivateAPI.h"
#include "ModuleLoadFrame.h"
#include "SharedSection.h"

using mozilla::DllBlockInfoFlags;

#define DLL_BLOCKLIST_ENTRY(name, ...) \
  {MOZ_LITERAL_UNICODE_STRING(L##name), __VA_ARGS__},
#define DLL_BLOCKLIST_STRING_TYPE UNICODE_STRING

#if defined(MOZ_LAUNCHER_PROCESS) || defined(NIGHTLY_BUILD)
#  include "mozilla/WindowsDllBlocklistLauncherDefs.h"
#else
#  include "mozilla/WindowsDllBlocklistCommon.h"
DLL_BLOCKLIST_DEFINITIONS_BEGIN
DLL_BLOCKLIST_DEFINITIONS_END
#endif

using WritableBuffer = mozilla::glue::detail::WritableBuffer<1024>;

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

#define NTKERNELAPI

#ifndef FASTCALL
#define FASTCALL __fastcall
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

class MOZ_STATIC_CLASS MOZ_TRIVIAL_CTOR_DTOR NativeNtBlockSet final {
  struct NativeNtBlockSetEntry {
    NativeNtBlockSetEntry() = default;
    ~NativeNtBlockSetEntry() = default;
    NativeNtBlockSetEntry(const UNICODE_STRING& aName, uint64_t aVersion,
                          NativeNtBlockSetEntry* aNext)
        : mName(aName), mVersion(aVersion), mNext(aNext) {}
    UNICODE_STRING mName;
    uint64_t mVersion;
    NativeNtBlockSetEntry* mNext;
  };

 public:
  // Constructor and destructor MUST be trivial
  constexpr NativeNtBlockSet() : mFirstEntry(nullptr) {}
  ~NativeNtBlockSet() = default;

  void Add(const UNICODE_STRING& aName, uint64_t aVersion);
  void Write(WritableBuffer& buffer);

 private:
  static NativeNtBlockSetEntry* NewEntry(const UNICODE_STRING& aName,
                                         uint64_t aVersion,
                                         NativeNtBlockSetEntry* aNextEntry);

 private:
  NativeNtBlockSetEntry* mFirstEntry;
  mozilla::nt::SRWLock mLock;
};

NativeNtBlockSet::NativeNtBlockSetEntry* NativeNtBlockSet::NewEntry(
    const UNICODE_STRING& aName, uint64_t aVersion,
    NativeNtBlockSet::NativeNtBlockSetEntry* aNextEntry) {
  return mozilla::freestanding::RtlNew<NativeNtBlockSetEntry>(aName, aVersion,
                                                              aNextEntry);
}

void NativeNtBlockSet::Add(const UNICODE_STRING& aName, uint64_t aVersion) {
  mozilla::nt::AutoExclusiveLock lock(mLock);

  for (NativeNtBlockSetEntry* entry = mFirstEntry; entry;
       entry = entry->mNext) {
    if (::RtlEqualUnicodeString(&entry->mName, &aName, TRUE) &&
        aVersion == entry->mVersion) {
      return;
    }
  }

  // Not present, add it
  NativeNtBlockSetEntry* newEntry = NewEntry(aName, aVersion, mFirstEntry);
  if (newEntry) {
    mFirstEntry = newEntry;
  }
}

void NativeNtBlockSet::Write(WritableBuffer& aBuffer) {
  // NB: If this function is called, it is long after kernel32 is initialized,
  // so it is safe to use Win32 calls here.
  char buf[MAX_PATH];

  // It would be nicer to use RAII here. However, its destructor
  // might not run if an exception occurs, in which case we would never release
  // the lock (MSVC warns about this possibility). So we acquire and release
  // manually.
  ExAcquirePushLockExclusive((PEX_PUSH_LOCK)&mLock);

  MOZ_SEH_TRY {
    for (auto entry = mFirstEntry; entry; entry = entry->mNext) {
      int convOk = ::WideCharToMultiByte(CP_UTF8, 0, entry->mName.Buffer,
                                         entry->mName.Length / sizeof(wchar_t),
                                         buf, sizeof(buf), nullptr, nullptr);
      if (!convOk) {
        continue;
      }

      // write name[,v.v.v.v];
      aBuffer.Write(buf, convOk);

      if (entry->mVersion != DllBlockInfo::ALL_VERSIONS) {
        aBuffer.Write(",", 1);
        uint16_t parts[4];
        parts[0] = entry->mVersion >> 48;
        parts[1] = (entry->mVersion >> 32) & 0xFFFF;
        parts[2] = (entry->mVersion >> 16) & 0xFFFF;
        parts[3] = entry->mVersion & 0xFFFF;
        for (size_t p = 0; p < std::size(parts); ++p) {
          _ltoa_s(parts[p], buf, sizeof(buf), 10);
          aBuffer.Write(buf, strlen(buf));
          if (p != std::size(parts) - 1) {
            aBuffer.Write(".", 1);
          }
        }
      }
      aBuffer.Write(";", 1);
    }
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {}

  ExReleasePushLockExclusive((PEX_PUSH_LOCK)&mLock);
}

static NativeNtBlockSet gBlockSet;

extern "C" void MOZ_EXPORT NativeNtBlockSet_Write(WritableBuffer& aBuffer) {
  gBlockSet.Write(aBuffer);
}

enum class BlockAction {
  // Allow the DLL to be loaded.
  Allow,
  // Substitute in a different DLL to be loaded instead of this one?
  // This is intended to be used for Layered Service Providers, which
  // cannot be blocked in the normal way. Note that this doesn't seem
  // to be actually implemented right now, and no entries in the blocklist
  // use it.
  SubstituteLSP,
  // There was an error in determining whether we should block this DLL.
  // It will be blocked.
  Error,
  // Block the DLL from loading.
  Deny,
  // Effectively block the DLL from loading by redirecting its DllMain
  // to a stub version. This is needed for DLLs that add themselves to
  // the executable's Import Table, since failing to load would mean the
  // executable would fail to launch.
  NoOpEntryPoint,
};

static BlockAction CheckBlockInfo(const DllBlockInfo* aInfo,
                                  const mozilla::nt::PEHeaders& aHeaders,
                                  uint64_t& aVersion) {
  aVersion = DllBlockInfo::ALL_VERSIONS;

  if (aInfo->mFlags & (DllBlockInfoFlags::BLOCK_WIN8_AND_OLDER |
                       DllBlockInfoFlags::BLOCK_WIN7_AND_OLDER)) {
    RTL_OSVERSIONINFOW osv = {sizeof(osv)};
    NTSTATUS ntStatus = ::RtlGetVersion(&osv);
    if (!NT_SUCCESS(ntStatus)) {
      return BlockAction::Error;
    }

    if ((aInfo->mFlags & DllBlockInfoFlags::BLOCK_WIN8_AND_OLDER) &&
        (osv.dwMajorVersion > 6 ||
         (osv.dwMajorVersion == 6 && osv.dwMinorVersion > 2))) {
      return BlockAction::Allow;
    }

    if ((aInfo->mFlags & DllBlockInfoFlags::BLOCK_WIN7_AND_OLDER) &&
        (osv.dwMajorVersion > 6 ||
         (osv.dwMajorVersion == 6 && osv.dwMinorVersion > 1))) {
      return BlockAction::Allow;
    }
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::CHILD_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsChildProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::UTILITY_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsUtilityProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::SOCKET_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsSocketProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::GPU_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsGPUProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::BROWSER_PROCESS_ONLY) &&
      (gBlocklistInitFlags & eDllBlocklistInitFlagIsChildProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::GMPLUGIN_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsGMPluginProcess)) {
    return BlockAction::Allow;
  }

  if ((aInfo->mFlags & DllBlockInfoFlags::RDD_PROCESSES_ONLY) &&
      !(gBlocklistInitFlags & eDllBlocklistInitFlagIsRDDProcess)) {
    return BlockAction::Allow;
  }

  if (aInfo->mMaxVersion == DllBlockInfo::ALL_VERSIONS) {
    return BlockAction::Deny;
  }

  if (!aHeaders) {
    return BlockAction::Error;
  }

  if (aInfo->mFlags & DllBlockInfoFlags::USE_TIMESTAMP) {
    DWORD timestamp;
    if (!aHeaders.GetTimeStamp(timestamp)) {
      return BlockAction::Error;
    }

    if (timestamp > aInfo->mMaxVersion) {
      return BlockAction::Allow;
    }

    return BlockAction::Deny;
  }

  // Else we try to get the file version information. Note that we don't have
  // access to GetFileVersionInfo* APIs.
  if (!aHeaders.GetVersionInfo(aVersion)) {
    return BlockAction::Error;
  }

  if (aInfo->IsVersionBlocked(aVersion)) {
    return BlockAction::Deny;
  }

  return BlockAction::Allow;
}

static BOOL WINAPI NoOp_DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

// This helper function checks whether a given module is included
// in the executable's Import Table.  Because an injected module's
// DllMain may revert the Import Table to the original state, we parse
// the Import Table every time a module is loaded without creating a cache.
static bool IsInjectedDependentModule(
    const UNICODE_STRING& aModuleLeafName,
    mozilla::freestanding::Kernel32ExportsSolver& aK32Exports) {
  mozilla::nt::PEHeaders exeHeaders(aK32Exports.mGetModuleHandleW(nullptr));
  if (!exeHeaders || !exeHeaders.IsImportDirectoryTampered()) {
    // If no tampering is detected, no need to enumerate the Import Table.
    return false;
  }

  bool isDependent = false;
  exeHeaders.EnumImportChunks(
      [&isDependent, &aModuleLeafName, &exeHeaders](const char* aDepModule) {
        // If |aDepModule| is within the PE image, it's not an injected module
        // but a legitimate dependent module.
        if (isDependent || exeHeaders.IsWithinImage(aDepModule)) {
          return;
        }

        UNICODE_STRING depModuleLeafName;
        mozilla::nt::AllocatedUnicodeString depModuleName(aDepModule);
        mozilla::nt::GetLeafName(&depModuleLeafName, depModuleName);
        isDependent = (::RtlCompareUnicodeString(
                           &aModuleLeafName, &depModuleLeafName, TRUE) == 0);
      });
  return isDependent;
}

// Allowing a module to be loaded but detour the entrypoint to NoOp_DllMain
// so that the module has no chance to interact with our code.  We need this
// technique to safely block a module injected by IAT tampering because
// blocking such a module makes a process fail to launch.
static bool RedirectToNoOpEntryPoint(
    const mozilla::nt::PEHeaders& aModule,
    mozilla::freestanding::Kernel32ExportsSolver& aK32Exports) {
  mozilla::interceptor::WindowsDllEntryPointInterceptor interceptor(
      aK32Exports);
  if (!interceptor.Set(aModule, NoOp_DllMain)) {
    return false;
  }

  return true;
}

static BlockAction DetermineBlockAction(
    const UNICODE_STRING& aLeafName, void* aBaseAddress,
    mozilla::freestanding::Kernel32ExportsSolver* aK32Exports) {
  if (mozilla::nt::Contains12DigitHexString(aLeafName) ||
      mozilla::nt::IsFileNameAtLeast16HexDigits(aLeafName)) {
    return BlockAction::Deny;
  }

  mozilla::nt::PEHeaders headers(aBaseAddress);
  DWORD checksum = 0;
  DWORD timestamp = 0;
  DWORD imageSize = 0;
  uint64_t version = 0;

  // Block some malicious DLLs known for crashing our process (bug 1841751),
  // based on matching the combination of version number + timestamp + image
  // size. We further reduce the chances of collision with legit DLLs by
  // checking for a checksum of 0 and the absence of debug information, both of
  // which are unusual for production-ready DLLs.
  if (headers.GetCheckSum(checksum) && checksum == 0 && !headers.GetPdbInfo() &&
      headers.GetTimeStamp(timestamp)) {
    struct KnownMaliciousCombination {
      uint64_t mVersion;
      uint32_t mTimestamp;
      uint32_t mImageSize;
    };
    const KnownMaliciousCombination instances[]{
        // 1.0.0.26638
        {0x000100000000680e, 0x570B8A90, 0x62000},
        // 1.0.0.26793
        {0x00010000000068a9, 0x572B4CE4, 0x62000},
        // 1.0.0.27567
        {0x0001000000006baf, 0x57A725AC, 0x61000},
        // 1.0.0.29915
        {0x00010000000074db, 0x5A115D81, 0x5D000},
        // 1.0.0.31122
        {0x0001000000007992, 0x5CFF88B8, 0x5D000}};

    // We iterate over timestamps, because they are unique and it is a quick
    // field to fetch
    for (const auto& instance : instances) {
      if (instance.mTimestamp == timestamp) {
        // Only fetch other fields in case we have a match. Then, we can exit
        // the loop.
        if (headers.GetImageSize(imageSize) &&
            instance.mImageSize == imageSize &&
            headers.GetVersionInfo(version) && instance.mVersion == version) {
          return BlockAction::Deny;
        }
        break;
      }
    }
  }

  DECLARE_POINTER_TO_FIRST_DLL_BLOCKLIST_ENTRY(info);
  DECLARE_DLL_BLOCKLIST_NUM_ENTRIES(infoNumEntries);

  mozilla::freestanding::DllBlockInfoComparator comp(aLeafName);

  size_t match = LowerBound(info, 0, infoNumEntries, comp);
  bool builtinListHasLowerBound = match != infoNumEntries;
  const DllBlockInfo* entry = nullptr;
  BlockAction checkResult = BlockAction::Allow;
  if (builtinListHasLowerBound) {
    // There may be multiple entries on the list. Since LowerBound() returns
    // the first entry that matches (if there are any matches),
    // search forward from there.
    while (match < infoNumEntries && (comp(info[match]) == 0)) {
      entry = &info[match];
      checkResult = CheckBlockInfo(entry, headers, version);
      if (checkResult != BlockAction::Allow) {
        break;
      }
      ++match;
    }
  }
  mozilla::DebugOnly<bool> blockedByDynamicBlocklist = false;
  // Make sure we handle a case that older versions are blocked by the static
  // list, but the dynamic list blocks all versions.
  if (checkResult == BlockAction::Allow) {
    if (!mozilla::freestanding::gSharedSection.IsDisabled()) {
      entry = mozilla::freestanding::gSharedSection.SearchBlocklist(aLeafName);
      if (entry) {
        checkResult = CheckBlockInfo(entry, headers, version);
        blockedByDynamicBlocklist = checkResult != BlockAction::Allow;
      }
    }
  }
  if (checkResult == BlockAction::Allow) {
    return BlockAction::Allow;
  }

  gBlockSet.Add(entry->mName, version);

  if ((entry->mFlags & DllBlockInfoFlags::REDIRECT_TO_NOOP_ENTRYPOINT) &&
      aK32Exports && RedirectToNoOpEntryPoint(headers, *aK32Exports)) {
    MOZ_ASSERT(!blockedByDynamicBlocklist, "dynamic blocklist has redirect?");
    return BlockAction::NoOpEntryPoint;
  }

  return checkResult;
}

namespace mozilla {
namespace freestanding {

CrossProcessDllInterceptor::FuncHookType<LdrLoadDllPtr> stub_LdrLoadDll;

NTSTATUS NTAPI patched_LdrLoadDll(PWCHAR aDllPath, PULONG aFlags,
                                  PUNICODE_STRING aDllName,
                                  PHANDLE aOutHandle) {
  ModuleLoadFrame frame(aDllName);

  NTSTATUS ntStatus = stub_LdrLoadDll(aDllPath, aFlags, aDllName, aOutHandle);

  return frame.SetLoadStatus(ntStatus, aOutHandle);
}

CrossProcessDllInterceptor::FuncHookType<NtMapViewOfSectionPtr>
    stub_NtMapViewOfSection;

CrossProcessDllInterceptor::FuncHookType<NtCreateSectionPtr>
    stub_NtCreateSection;

// The file handle taken by patched_NtCreateSection on this thread, until
// patched_NtMapViewOfSection claims it.
static SafeThreadLocal<HANDLE> sPendingFileHandle;

static nt::AutoHandle TakePendingFileHandle() {
  HANDLE pending = sPendingFileHandle.get();
  if (pending) {
    sPendingFileHandle.set(nullptr);
  }
  return nt::AutoHandle(pending);
}

// To preserve compatibility with third-parties, this must not use checked stack
// buffers, for the same reason as patched_NtMapViewOfSection (see bug 1733532).
NTSTATUS NTAPI patched_NtCreateSection(PHANDLE aSectionHandle,
                                       ACCESS_MASK aDesiredAccess,
                                       POBJECT_ATTRIBUTES aObjectAttributes,
                                       PLARGE_INTEGER aMaximumSize,
                                       ULONG aSectionPageProtection,
                                       ULONG aAllocationAttributes,
                                       HANDLE aFileHandle) {
  NTSTATUS stubStatus = stub_NtCreateSection(
      aSectionHandle, aDesiredAccess, aObjectAttributes, aMaximumSize,
      aSectionPageProtection, aAllocationAttributes, aFileHandle);
  if (!NT_SUCCESS(stubStatus)) {
    return stubStatus;
  }

  // We only care about sections created the way the loader creates one for a
  // DLL.
  if (!(aDesiredAccess & SECTION_MAP_EXECUTE) || aObjectAttributes ||
      aMaximumSize || aSectionPageProtection != PAGE_EXECUTE ||
      aAllocationAttributes != SEC_IMAGE || !aFileHandle) {
    return stubStatus;
  }

  // Duplicate the file handle for the parent process, which only uses it to
  // identify the file.  Asking for any access the loader's handle lacks would
  // fail.
  // DuplicateHandle is in kernel32 and this intercepted function can run
  // before that is loaded, so use ntdll's equivalent.
  HANDLE duplicate = nullptr;
  if (!NT_SUCCESS(::NtDuplicateObject(nt::kCurrentProcess, aFileHandle,
                                      nt::kCurrentProcess, &duplicate,
                                      SYNCHRONIZE, 0, 0))) {
    // Not fatal: the load proceeds and the parent simply cannot evaluate
    // this module.
    return stubStatus;
  }

  // Drop any handle still pending, since its section was never mapped.
  nt::AutoHandle stale(TakePendingFileHandle());
  if (!sPendingFileHandle.set(duplicate)) {
    // This thread is not main and has no TLS yet, so we can't track this.
    ::NtClose(duplicate);
  }
  return stubStatus;
}

// All the code for patched_NtMapViewOfSection that relies on checked stack
// buffers (e.g. mbi, sectionFileName) should be put in this helper function
// (see bug 1733532).
MOZ_NEVER_INLINE NTSTATUS AfterMapViewOfExecutableSection(
    HANDLE aProcess, PVOID* aBaseAddress, NTSTATUS aStubStatus,
    nt::AutoHandle&& aFileHandle) {
  // We don't care about mappings that aren't MEM_IMAGE.
  MEMORY_BASIC_INFORMATION mbi;
  NTSTATUS ntStatus =
      ::NtQueryVirtualMemory(aProcess, *aBaseAddress, MemoryBasicInformation,
                             &mbi, sizeof(mbi), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    ::NtUnmapViewOfSection(aProcess, *aBaseAddress);
    return STATUS_ACCESS_DENIED;
  }
  if (!(mbi.Type & MEM_IMAGE)) {
    return aStubStatus;
  }

  // Get the section name
  nt::MemorySectionNameBuf sectionFileName(
      gLoaderPrivateAPI.GetSectionNameBuffer(*aBaseAddress));
  if (sectionFileName.IsEmpty()) {
    ::NtUnmapViewOfSection(aProcess, *aBaseAddress);
    return STATUS_ACCESS_DENIED;
  }

  // Find the leaf name
  UNICODE_STRING leafOnStack;
  nt::GetLeafName(&leafOnStack, sectionFileName);

  bool isInjectedDependent = false;
  const UNICODE_STRING k32Name = MOZ_LITERAL_UNICODE_STRING(L"kernel32.dll");
  Kernel32ExportsSolver* k32Exports = nullptr;
  BlockAction blockAction;
  // Trying to get the Kernel32Exports while loading kernel32.dll causes Firefox
  // to crash. (but only during a profile-guided optimization run, oddly) We
  // know we're never going to block kernel32.dll, so skip all this
  if (::RtlCompareUnicodeString(&k32Name, &leafOnStack, TRUE) == 0) {
    blockAction = BlockAction::Allow;
  } else {
    auto noSharedSectionReset{SharedSection::AutoNoReset()};
    k32Exports = gSharedSection.GetKernel32Exports();
    // Small optimization: Since loading a dependent module does not involve
    // LdrLoadDll, we know isInjectedDependent is false if we hold a top frame.
    if (k32Exports && !ModuleLoadFrame::ExistsTopFrame()) {
      // Note that if a module is dependent but not injected, this means that
      // the executable built against it, and it should be signed by Mozilla
      // or Microsoft, so we don't need to worry about adding it to the list
      // for CIG. (and users won't be able to block it) So the only special
      // case here is a dependent module that has been injected.
      isInjectedDependent = IsInjectedDependentModule(leafOnStack, *k32Exports);
    }

    if (isInjectedDependent) {
      // Add an NT dv\path to the shared section so that a sandbox process can
      // use it to bypass CIG.  In a sandbox process, this addition fails
      // because we cannot map the section to a writable region, but it's
      // ignorable because the paths have been added by the browser process.
      (void)SharedSection::AddDependentModule(sectionFileName);

      bool attemptToBlockViaRedirect;
#if defined(NIGHTLY_BUILD)
      // We enable automatic DLL blocking only in Nightly for now
      // because it caused a compat issue (bug 1682304 and 1704373).
      attemptToBlockViaRedirect = true;
      // We will set blockAction below in the if (attemptToBlockViaRedirect)
      // block, but I guess the compiler isn't smart enough to figure
      // that out and complains about an uninitialized variable :-(
      blockAction = BlockAction::NoOpEntryPoint;
#else
      // Check blocklist
      blockAction =
          DetermineBlockAction(leafOnStack, *aBaseAddress, k32Exports);
      // If we were going to block this dependent module, try redirection
      // instead of blocking it, since blocking it would cause the .exe not to
      // launch.
      // Note tht Deny and Error both end up blocking the module in a
      // straightforward way, so those are the cases in which we need
      // to redirect instead.
      attemptToBlockViaRedirect =
          blockAction == BlockAction::Deny || blockAction == BlockAction::Error;
#endif
      if (attemptToBlockViaRedirect) {
        // For a dependent module, try redirection instead of blocking it.
        // If we fail, we reluctantly allow the module for free.
        mozilla::nt::PEHeaders headers(*aBaseAddress);
        blockAction = RedirectToNoOpEntryPoint(headers, *k32Exports)
                          ? BlockAction::NoOpEntryPoint
                          : BlockAction::Allow;
      }
    } else {
      // Check blocklist
      blockAction =
          DetermineBlockAction(leafOnStack, *aBaseAddress, k32Exports);
    }
  }

  ModuleLoadInfo::Status loadStatus = ModuleLoadInfo::Status::Blocked;

  switch (blockAction) {
    case BlockAction::Allow:
      loadStatus = ModuleLoadInfo::Status::Loaded;
      break;

    case BlockAction::NoOpEntryPoint:
      loadStatus = ModuleLoadInfo::Status::Redirected;
      break;

    case BlockAction::SubstituteLSP:
      // The process heap needs to be available here because
      // NotifyLSPSubstitutionRequired below copies a given string into
      // the heap. We use a soft assert here, assuming LSP load always
      // occurs after the heap is initialized.
      MOZ_ASSERT(nt::RtlGetProcessHeap());

      // Notify patched_LdrLoadDll that it will be necessary to perform
      // a substitution before returning.
      ModuleLoadFrame::NotifyLSPSubstitutionRequired(&leafOnStack);
      break;

    default:
      break;
  }

  if (nt::RtlGetProcessHeap()) {
    ModuleLoadFrame::NotifySectionMap(
        nt::AllocatedUnicodeString(sectionFileName), *aBaseAddress, aStubStatus,
        loadStatus, isInjectedDependent, std::move(aFileHandle));
  }

  if (loadStatus == ModuleLoadInfo::Status::Loaded ||
      loadStatus == ModuleLoadInfo::Status::Redirected) {
    return aStubStatus;
  }

  ::NtUnmapViewOfSection(aProcess, *aBaseAddress);
  return STATUS_ACCESS_DENIED;
}

// To preserve compatibility with third-parties, calling into this function
// must not use checked stack buffers when reached through Thread32Next (see
// bug 1733532). Hence this function is declared as MOZ_NO_STACK_PROTECTOR.
// Ideally, all code relying on stack buffers should be put in the dedicated
// helper function AfterMapViewOfExecutableImageSection, which does not have
// the MOZ_NO_STACK_PROTECTOR attribute. The obi variable below is an
// exception to this rule, as it is required to collect the information that
// lets us decide whether we really need to go through the helper function.
NTSTATUS NTAPI patched_NtMapViewOfSection(
    HANDLE aSection, HANDLE aProcess, PVOID* aBaseAddress, ULONG_PTR aZeroBits,
    SIZE_T aCommitSize, PLARGE_INTEGER aSectionOffset, PSIZE_T aViewSize,
    SECTION_INHERIT aInheritDisposition, ULONG aAllocationType,
    ULONG aProtectionFlags) {
  // Save off the values currently in the out-pointers for later restoration if
  // we decide not to permit this mapping.
  auto const rollback =
      [
          // Avoid taking a reference to the stack frame, mostly out of
          // paranoia. (The values of `aBaseAddress` et al. may have been
          // crafted to point to our return address anyway...)
          =,
          // `NtMapViewOfSection` itself is mildly robust to invalid pointers;
          // we can't easily do that, but we can at least check for `nullptr`.
          baseAddress = aBaseAddress ? *aBaseAddress : nullptr,
          sectionOffset = aSectionOffset ? *aSectionOffset : LARGE_INTEGER{},
          viewSize = aViewSize ? *aViewSize : 0]() {
        if (aBaseAddress) *aBaseAddress = baseAddress;
        if (aSectionOffset) *aSectionOffset = sectionOffset;
        if (aViewSize) *aViewSize = viewSize;
      };

  // Claim the pending file handle on every path, so that a later, unrelated
  // mapping cannot.
  nt::AutoHandle fileHandle(TakePendingFileHandle());

  // We always map first, then we check for additional info after.
  NTSTATUS stubStatus = stub_NtMapViewOfSection(
      aSection, aProcess, aBaseAddress, aZeroBits, aCommitSize, aSectionOffset,
      aViewSize, aInheritDisposition, aAllocationType, aProtectionFlags);
  if (!NT_SUCCESS(stubStatus)) {
    return stubStatus;
  }

  if (aProcess != nt::kCurrentProcess) {
    // We're only interested in mapping for the current process.
    return stubStatus;
  }

  PUBLIC_OBJECT_BASIC_INFORMATION obi;
  NTSTATUS ntStatus = ::NtQueryObject(aSection, ObjectBasicInformation, &obi,
                                      sizeof(obi), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    ::NtUnmapViewOfSection(aProcess, *aBaseAddress);
    rollback();
    return STATUS_ACCESS_DENIED;
  }

  // We don't care about sections for which the permission to map executable
  // views was not asked at creation time. This early exit path is notably
  // taken for:
  //  - calls to LoadLibraryExW using LOAD_LIBRARY_AS_DATAFILE or
  //    LOAD_LIBRARY_AS_IMAGE_RESOURCE (bug 1842088), thus allowing us to load
  //    blocked DLLs for analysis without executing them;
  //  - calls to Thread32Next (bug 1733532), thus avoiding the helper function
  //    with stack cookie checks.
  if (!(obi.GrantedAccess & SECTION_MAP_EXECUTE)) {
    return stubStatus;
  }

  NTSTATUS rv = AfterMapViewOfExecutableSection(
      aProcess, aBaseAddress, stubStatus, std::move(fileHandle));
  if (FAILED(rv)) {
    rollback();
  }
  return rv;
}

}  // namespace freestanding
}  // namespace mozilla
