/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "LoaderPrivateAPI.h"

#include "mozilla/Assertions.h"
#include "mozilla/Types.h"
#include "mozilla/Unused.h"
#include "../DllBlocklistInit.h"
#include "../ErrorHandler.h"
#include "SharedSection.h"

using GlobalInitializerFn = void(__cdecl*)(void);

// Allocation of static initialization section for the freestanding library
#pragma section(".freestd$a", read)
__declspec(allocate(".freestd$a")) static const GlobalInitializerFn
    FreeStdStart = reinterpret_cast<GlobalInitializerFn>(0);

#pragma section(".freestd$z", read)
__declspec(allocate(".freestd$z")) static const GlobalInitializerFn FreeStdEnd =
    reinterpret_cast<GlobalInitializerFn>(0);

namespace mozilla {
namespace freestanding {

static RTL_RUN_ONCE gRunOnce = RTL_RUN_ONCE_INIT;

// The contract for this callback is identical to the InitOnceCallback from
// Win32 land; we're just using ntdll-layer types instead.
static ULONG NTAPI DoOneTimeInit(PRTL_RUN_ONCE aRunOnce, PVOID aParameter,
                                 PVOID* aContext) {
  // Invoke every static initializer in the .freestd section
  const GlobalInitializerFn* cur = &FreeStdStart + 1;
  while (cur < &FreeStdEnd) {
    if (*cur) {
      (*cur)();
    }

    ++cur;
  }

  return TRUE;
}

/**
 * This observer is only used until the mozglue observer connects itself.
 * All we do here is accumulate the module loads into a vector.
 * As soon as mozglue connects, we call |Forward| on mozglue's LoaderObserver
 * to pass our vector on for further processing. This object then becomes
 * defunct.
 */
class MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS DefaultLoaderObserver final
    : public nt::LoaderObserver {
 public:
  constexpr DefaultLoaderObserver() : mModuleLoads(nullptr) {}

  void OnBeginDllLoad(void** aContext,
                      PCUNICODE_STRING aRequestedDllName) final {}
  bool SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                        PHANDLE aOutHandle) final {
    return false;
  }
  void OnEndDllLoad(void* aContext, NTSTATUS aNtStatus,
                    ModuleLoadInfo&& aModuleLoadInfo) final;
  void Forward(nt::LoaderObserver* aNext) final;
  void OnForward(ModuleLoadInfoVec&& aInfo) final {
    MOZ_ASSERT_UNREACHABLE("Not valid in freestanding::DefaultLoaderObserver");
  }

 private:
  mozilla::nt::SRWLock mLock;
  ModuleLoadInfoVec* mModuleLoads;
};

class MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS LoaderPrivateAPIImp final
    : public LoaderPrivateAPI {
 public:
  // LoaderAPI
  ModuleLoadInfo ConstructAndNotifyBeginDllLoad(
      void** aContext, PCUNICODE_STRING aRequestedDllName) final;
  bool SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                        PHANDLE aOutHandle) final;
  void NotifyEndDllLoad(void* aContext, NTSTATUS aLoadNtStatus,
                        ModuleLoadInfo&& aModuleLoadInfo) final;
  nt::AllocatedUnicodeString GetSectionName(void* aSectionAddr) final;
  nt::LoaderAPI::InitDllBlocklistOOPFnPtr GetDllBlocklistInitFn() final;
  nt::LoaderAPI::HandleLauncherErrorFnPtr GetHandleLauncherErrorFn() final;
  nt::SharedSection* GetSharedSection() final;

  // LoaderPrivateAPI
  void NotifyBeginDllLoad(void** aContext,
                          PCUNICODE_STRING aRequestedDllName) final;
  void NotifyBeginDllLoad(ModuleLoadInfo& aModuleLoadInfo, void** aContext,
                          PCUNICODE_STRING aRequestedDllName) final;
  void SetObserver(nt::LoaderObserver* aNewObserver) final;
  bool IsDefaultObserver() const final;
  nt::MemorySectionNameBuf GetSectionNameBuffer(void* aSectionAddr) final;
};

extern "C" DECLSPEC_IMPORT NTSTATUS WINAPI NtWaitForKeyedEvent(HANDLE, const void*, BOOLEAN, const LARGE_INTEGER*);
extern "C" DECLSPEC_IMPORT NTSTATUS WINAPI NtReleaseKeyedEvent(HANDLE, const void*, BOOLEAN, const LARGE_INTEGER*);
  DWORD sRunOnceComplete(INIT_ONCE* once, ULONG flags, void* context) {
    if ((ULONG_PTR)context & 3) return STATUS_INVALID_PARAMETER;

    if (flags & RTL_RUN_ONCE_INIT_FAILED) {
      if (context) return STATUS_INVALID_PARAMETER;
      if (flags & RTL_RUN_ONCE_ASYNC) return STATUS_INVALID_PARAMETER;
    }
    else context = (void *)((ULONG_PTR)context | 2);

    for (;;) {
      ULONG_PTR val = (ULONG_PTR)once->Ptr;
      switch (val & 3) {
        case 1:  /* in progress */
          if (::InterlockedCompareExchangePointer( &once->Ptr, context, (void *)val ) != (void *)val) break;
          val &= ~3;
          while (val) {
            ULONG_PTR next = *(ULONG_PTR *)val;
            NtReleaseKeyedEvent( 0, (void *)val, FALSE, NULL );
            val = next;
            }
            return STATUS_SUCCESS;

        case 3:  /* in progress, async */
          if (!(flags & RTL_RUN_ONCE_ASYNC)) return STATUS_INVALID_PARAMETER;
          if (::InterlockedCompareExchangePointer( &once->Ptr, context, (void *)val ) != (void *)val) break;
          return STATUS_SUCCESS;

        default:
          return STATUS_UNSUCCESSFUL;
      }
    }
  }


  DWORD sInitOnceExecuteOnce(INIT_ONCE* once, PINIT_ONCE_FN func, void* param, void ** context) {
    DWORD ret = 0;
    for (;;) {
for_begin:
      ULONG_PTR next, val = (ULONG_PTR)once->Ptr;

      switch (val & 3) {
        case 0:  /* first time */
          if (!::InterlockedCompareExchangePointer(&once->Ptr, (void *)1, 0 ))
            ret = STATUS_PENDING;
          break;

        case 1:  /* in progress, wait */
          next = val & ~3;
          if (::InterlockedCompareExchangePointer(&once->Ptr, (void *)((ULONG_PTR)&next | 1), (void *)val ) == (void *)val)
            NtWaitForKeyedEvent( 0, &next, FALSE, NULL );
          goto for_begin;

        case 2:  /* done */
          ret = STATUS_SUCCESS;
          break;

        case 3:  /* in progress, async */
          ret = STATUS_INVALID_PARAMETER;
          break;
       }
      break;
    }

  if (ret != STATUS_PENDING) return ret;

  if (!func(once, param, context)) {
    sRunOnceComplete(once, RTL_RUN_ONCE_INIT_FAILED, NULL);
    return STATUS_UNSUCCESSFUL;
  }

  return sRunOnceComplete(once, 0, context ? *context : NULL);
  }

static void Init() {
  DebugOnly<NTSTATUS> ntStatus =
      sInitOnceExecuteOnce((INIT_ONCE*)&gRunOnce, (PINIT_ONCE_FN)&DoOneTimeInit, nullptr, nullptr);
  MOZ_ASSERT(NT_SUCCESS(ntStatus));
}

}  // namespace freestanding
}  // namespace mozilla

static mozilla::freestanding::DefaultLoaderObserver gDefaultObserver;
static mozilla::freestanding::LoaderPrivateAPIImp gPrivateAPI;

static mozilla::nt::SRWLock gLoaderObserverLock;
static mozilla::nt::LoaderObserver* gLoaderObserver = &gDefaultObserver;

namespace mozilla {
namespace freestanding {

LoaderPrivateAPI& gLoaderPrivateAPI = gPrivateAPI;

void DefaultLoaderObserver::OnEndDllLoad(void* aContext, NTSTATUS aNtStatus,
                                         ModuleLoadInfo&& aModuleLoadInfo) {
  // If the DLL load failed, or if the DLL was loaded by a previous request
  // and thus was not mapped by this request, we do not save the ModuleLoadInfo.
  if (!NT_SUCCESS(aNtStatus) || !aModuleLoadInfo.WasMapped()) {
    return;
  }

  nt::AutoExclusiveLock lock(mLock);
  if (!mModuleLoads) {
    mModuleLoads = RtlNew<ModuleLoadInfoVec>();
    if (!mModuleLoads) {
      return;
    }
  }

  Unused << mModuleLoads->emplaceBack(
      std::forward<ModuleLoadInfo>(aModuleLoadInfo));
}

/**
 * Pass mModuleLoads's data off to |aNext| for further processing.
 */
void DefaultLoaderObserver::Forward(nt::LoaderObserver* aNext) {
  MOZ_ASSERT(aNext);
  if (!aNext) {
    return;
  }

  ModuleLoadInfoVec* moduleLoads = nullptr;

  {  // Scope for lock
    nt::AutoExclusiveLock lock(mLock);
    moduleLoads = mModuleLoads;
    mModuleLoads = nullptr;
  }

  if (!moduleLoads) {
    return;
  }

  aNext->OnForward(std::move(*moduleLoads));
  RtlDelete(moduleLoads);
}

ModuleLoadInfo LoaderPrivateAPIImp::ConstructAndNotifyBeginDllLoad(
    void** aContext, PCUNICODE_STRING aRequestedDllName) {
  ModuleLoadInfo loadInfo(aRequestedDllName);

  NotifyBeginDllLoad(loadInfo, aContext, aRequestedDllName);

  return loadInfo;
}

bool LoaderPrivateAPIImp::SubstituteForLSP(PCUNICODE_STRING aLSPLeafName,
                                           PHANDLE aOutHandle) {
  nt::AutoSharedLock lock(gLoaderObserverLock);
  return gLoaderObserver->SubstituteForLSP(aLSPLeafName, aOutHandle);
}

void LoaderPrivateAPIImp::NotifyEndDllLoad(void* aContext,
                                           NTSTATUS aLoadNtStatus,
                                           ModuleLoadInfo&& aModuleLoadInfo) {
  aModuleLoadInfo.SetEndLoadTimeStamp();

  if (NT_SUCCESS(aLoadNtStatus)) {
    aModuleLoadInfo.CaptureBacktrace();
  }

  nt::AutoSharedLock lock(gLoaderObserverLock);

  // We need to notify the observer that the DLL load has ended even when
  // |aLoadNtStatus| indicates a failure. This is to ensure that any resources
  // acquired by the observer during OnBeginDllLoad are cleaned up.
  gLoaderObserver->OnEndDllLoad(aContext, aLoadNtStatus,
                                std::move(aModuleLoadInfo));
}

nt::AllocatedUnicodeString LoaderPrivateAPIImp::GetSectionName(
    void* aSectionAddr) {
  const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);

  nt::MemorySectionNameBuf buf;
  NTSTATUS ntStatus =
      ::NtQueryVirtualMemory(kCurrentProcess, aSectionAddr, MemorySectionName,
                             &buf, sizeof(buf), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    return nt::AllocatedUnicodeString();
  }

  return nt::AllocatedUnicodeString(&buf.mSectionFileName);
}

nt::LoaderAPI::InitDllBlocklistOOPFnPtr
LoaderPrivateAPIImp::GetDllBlocklistInitFn() {
  return &InitializeDllBlocklistOOP;
}

nt::LoaderAPI::HandleLauncherErrorFnPtr
LoaderPrivateAPIImp::GetHandleLauncherErrorFn() {
  return &HandleLauncherError;
}

nt::SharedSection* LoaderPrivateAPIImp::GetSharedSection() {
  return &gSharedSection;
}

nt::MemorySectionNameBuf LoaderPrivateAPIImp::GetSectionNameBuffer(
    void* aSectionAddr) {
  const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);

  nt::MemorySectionNameBuf buf;
  NTSTATUS ntStatus =
      ::NtQueryVirtualMemory(kCurrentProcess, aSectionAddr, MemorySectionName,
                             &buf, sizeof(buf), nullptr);
  if (!NT_SUCCESS(ntStatus)) {
    return nt::MemorySectionNameBuf();
  }

  return buf;
}

void LoaderPrivateAPIImp::NotifyBeginDllLoad(
    void** aContext, PCUNICODE_STRING aRequestedDllName) {
  nt::AutoSharedLock lock(gLoaderObserverLock);
  gLoaderObserver->OnBeginDllLoad(aContext, aRequestedDllName);
}

void LoaderPrivateAPIImp::NotifyBeginDllLoad(
    ModuleLoadInfo& aModuleLoadInfo, void** aContext,
    PCUNICODE_STRING aRequestedDllName) {
  NotifyBeginDllLoad(aContext, aRequestedDllName);
  aModuleLoadInfo.SetBeginLoadTimeStamp();
}

void LoaderPrivateAPIImp::SetObserver(nt::LoaderObserver* aNewObserver) {
  nt::LoaderObserver* prevLoaderObserver = nullptr;

  nt::AutoExclusiveLock lock(gLoaderObserverLock);

  MOZ_ASSERT(aNewObserver);
  if (!aNewObserver) {
    // This is unlikely, but we always want a valid observer, so use the
    // gDefaultObserver if necessary.
    gLoaderObserver = &gDefaultObserver;
    return;
  }

  prevLoaderObserver = gLoaderObserver;
  gLoaderObserver = aNewObserver;

  MOZ_ASSERT(prevLoaderObserver);
  if (!prevLoaderObserver) {
    return;
  }

  // Now that we have a new observer, the previous observer must forward its
  // data on to the new observer for processing.
  prevLoaderObserver->Forward(aNewObserver);
}

bool LoaderPrivateAPIImp::IsDefaultObserver() const {
  nt::AutoSharedLock lock(gLoaderObserverLock);
  return gLoaderObserver == &gDefaultObserver;
}

void EnsureInitialized() { Init(); }

}  // namespace freestanding
}  // namespace mozilla
