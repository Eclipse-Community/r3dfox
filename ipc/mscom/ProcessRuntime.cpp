/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/mscom/ProcessRuntime.h"

#include <accctrl.h>
#include <aclapi.h>
#include <objbase.h>
#include <objidl.h>

#include "mozilla/Assertions.h"
#include "mozilla/mscom/COMWrappers.h"
#include "mozilla/mscom/ProcessRuntimeShared.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "mozilla/WindowsVersion.h"

#if defined(MOZILLA_INTERNAL_API)
#  include "mozilla/mscom/EnsureMTA.h"
#  if defined(MOZ_SANDBOX)
#    include "mozilla/sandboxTarget.h"
#  endif  // defined(MOZ_SANDBOX)
#endif    // defined(MOZILLA_INTERNAL_API)

// This API from oleaut32.dll is not declared in Windows SDK headers
extern "C" void __cdecl SetOaNoCache(void);

using namespace mozilla::mscom::detail;

namespace mozilla {
namespace mscom {

#if defined(MOZILLA_INTERNAL_API)
ProcessRuntime* ProcessRuntime::sInstance = nullptr;

ProcessRuntime::ProcessRuntime() : ProcessRuntime(XRE_GetProcessType()) {}

ProcessRuntime::ProcessRuntime(const GeckoProcessType aProcessType)
    : ProcessRuntime(aProcessType == GeckoProcessType_Default
                         ? ProcessCategory::GeckoBrowserParent
                         : ProcessCategory::GeckoChild) {}
#endif  // defined(MOZILLA_INTERNAL_API)

ProcessRuntime::ProcessRuntime(const ProcessCategory aProcessCategory)
    : mInitResult(CO_E_NOTINITIALIZED), mProcessCategory(aProcessCategory) {
#if defined(MOZILLA_INTERNAL_API)
  MOZ_DIAGNOSTIC_ASSERT(!sInstance);
  sInstance = this;

  EnsureMTA();
  /**
   * From this point forward, all threads in this process are implicitly
   * members of the multi-threaded apartment, with the following exceptions:
   * 1. If any Win32 GUI APIs were called on the current thread prior to
   *    executing this constructor, then this thread has already been implicitly
   *    initialized as the process's main STA thread; or
   * 2. A thread explicitly and successfully calls CoInitialize(Ex) to specify
   *    otherwise.
   */

  const bool isCurThreadImplicitMTA = IsCurrentThreadImplicitMTA();
  // We only assert that the implicit MTA precondition holds when not running
  // as the Gecko parent process.
  MOZ_DIAGNOSTIC_ASSERT(aProcessCategory ==
                            ProcessCategory::GeckoBrowserParent ||
                        isCurThreadImplicitMTA);

#  if defined(MOZ_SANDBOX)
  const bool isLockedDownChildProcess =
      mProcessCategory == ProcessCategory::GeckoChild && IsWin32kLockedDown();
  // If our process is running under Win32k lockdown, we cannot initialize
  // COM with a single-threaded apartment. This is because STAs create a hidden
  // window, which implicitly requires user32 and Win32k, which are blocked.
  // Instead we start the multi-threaded apartment and conduct our process-wide
  // COM initialization there.
  if (isLockedDownChildProcess) {
    // Make sure we're still running with the sandbox's privileged impersonation
    // token.
    HANDLE rawCurThreadImpToken;
    if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_QUERY,
                           FALSE, &rawCurThreadImpToken)) {
      mInitResult = HRESULT_FROM_WIN32(::GetLastError());
      return;
    }
    nsAutoHandle curThreadImpToken(rawCurThreadImpToken);

    // Ensure that our current token is still an impersonation token (ie, we
    // have not yet called RevertToSelf() on this thread).
    DWORD len;
    TOKEN_TYPE tokenType;
    MOZ_RELEASE_ASSERT(
        ::GetTokenInformation(rawCurThreadImpToken, TokenType, &tokenType,
                              sizeof(tokenType), &len) &&
        len == sizeof(tokenType) && tokenType == TokenImpersonation);

    // Ideally we want our current thread to be running implicitly inside the
    // MTA, but if for some wacky reason we did not end up with that, we may
    // compensate by completing initialization via EnsureMTA's persistent
    // thread.
    if (!isCurThreadImplicitMTA) {
      InitUsingPersistentMTAThread(curThreadImpToken);
      return;
    }
  }
#  endif  // defined(MOZ_SANDBOX)
#endif    // defined(MOZILLA_INTERNAL_API)

  mAptRegion.Init(GetDesiredApartmentType(mProcessCategory));

  // It can happen that we are not the outermost COM initialization on this
  // thread. In fact it should regularly be the case that the outermost
  // initialization occurs from outside of XUL, before we show the skeleton UI,
  // at which point we still need to run some things here from within XUL.
  if (!mAptRegion.IsValidOutermost()) {
    mInitResult = mAptRegion.GetHResult();
#if defined(MOZILLA_INTERNAL_API)
    MOZ_ASSERT(mProcessCategory == ProcessCategory::GeckoBrowserParent);
    if (mProcessCategory != ProcessCategory::GeckoBrowserParent) {
      // This is unexpected unless we're GeckoBrowserParent
      return;
    }

    ProcessInitLock lock;

    // Is another instance of ProcessRuntime responsible for the outer
    // initialization?
    const bool prevInit =
        lock.GetInitState() == ProcessInitState::FullyInitialized;
    MOZ_ASSERT(prevInit);
    if (prevInit) {
      PostInit();
    }
#endif  // defined(MOZILLA_INTERNAL_API)
    return;
  }

  InitInsideApartment();
  if (FAILED(mInitResult)) {
    return;
  }

#if defined(MOZILLA_INTERNAL_API)
#  if defined(MOZ_SANDBOX)
  if (isLockedDownChildProcess) {
    // In locked-down child processes, defer PostInit until priv drop
    SandboxTarget::Instance()->RegisterSandboxStartCallback([self = this]() {
      // Ensure that we're still live and the init was successful before
      // calling PostInit()
      if (self == sInstance && SUCCEEDED(self->mInitResult)) {
        PostInit();
      }
    });
    return;
  }
#  endif  // defined(MOZ_SANDBOX)

  PostInit();
#endif  // defined(MOZILLA_INTERNAL_API)
}

#if defined(MOZILLA_INTERNAL_API)
ProcessRuntime::~ProcessRuntime() {
  MOZ_DIAGNOSTIC_ASSERT(sInstance == this);
  sInstance = nullptr;
}

#  if defined(MOZ_SANDBOX)
void ProcessRuntime::InitUsingPersistentMTAThread(
    const nsAutoHandle& aCurThreadToken) {
  // Create an impersonation token based on the current thread's token
  HANDLE rawMtaThreadImpToken = nullptr;
  if (!::DuplicateToken(aCurThreadToken, SecurityImpersonation,
                        &rawMtaThreadImpToken)) {
    mInitResult = HRESULT_FROM_WIN32(::GetLastError());
    return;
  }
  nsAutoHandle mtaThreadImpToken(rawMtaThreadImpToken);

  // Impersonate and initialize.
  bool tokenSet = false;
  EnsureMTA(
      [this, rawMtaThreadImpToken, &tokenSet]() -> void {
        if (!::SetThreadToken(nullptr, rawMtaThreadImpToken)) {
          mInitResult = HRESULT_FROM_WIN32(::GetLastError());
          return;
        }

        tokenSet = true;
        InitInsideApartment();
      },
      EnsureMTA::Option::ForceDispatchToPersistentThread);

  if (!tokenSet) {
    return;
  }

  SandboxTarget::Instance()->RegisterSandboxStartCallback(
      [self = this]() -> void {
        EnsureMTA(
            []() -> void {
              // This is a security risk if it fails, so we release assert
              MOZ_RELEASE_ASSERT(::RevertToSelf(),
                                 "mscom::ProcessRuntime RevertToSelf failed");
            },
            EnsureMTA::Option::ForceDispatchToPersistentThread);

        // Ensure that we're still live and the init was successful before
        // calling PostInit()
        if (self == sInstance && SUCCEEDED(self->mInitResult)) {
          PostInit();
        }
      });
}
#  endif  // defined(MOZ_SANDBOX)
#endif    // defined(MOZILLA_INTERNAL_API)

/* static */
COINIT ProcessRuntime::GetDesiredApartmentType(
    const ProcessRuntime::ProcessCategory aProcessCategory) {
  switch (aProcessCategory) {
    case ProcessCategory::GeckoBrowserParent:
      return COINIT_APARTMENTTHREADED;
    case ProcessCategory::GeckoChild:
      if (!IsWin32kLockedDown()) {
        // If Win32k is not locked down then we probably still need STA.
        // We disable DDE since that is not usable from child processes.
        return static_cast<COINIT>(COINIT_APARTMENTTHREADED |
                                   COINIT_DISABLE_OLE1DDE);
      }

      [[fallthrough]];
    default:
      return COINIT_MULTITHREADED;
  }
}

void ProcessRuntime::InitInsideApartment() {
  ProcessInitLock lock;
  const ProcessInitState prevInitState = lock.GetInitState();
  if (prevInitState == ProcessInitState::FullyInitialized) {
    // COM has already been initialized by a previous ProcessRuntime instance
    mInitResult = S_OK;
    return;
  }

  if (prevInitState < ProcessInitState::PartialSecurityInitialized) {
    // We are required to initialize security prior to configuring global
    // options.
    mInitResult = InitializeSecurity(mProcessCategory);
    // Downgrading from a MOZ_DIAGNOSTIC_ASSERT while investigating
    // bug 1930846.
    MOZ_ASSERT(SUCCEEDED(mInitResult));

    // Even though this isn't great, we should try to proceed even when
    // CoInitializeSecurity has previously been called: the additional settings
    // we want to change are important enough that we don't want to skip them.
    if (FAILED(mInitResult) && mInitResult != RPC_E_TOO_LATE) {
      return;
    }

    lock.SetInitState(ProcessInitState::PartialSecurityInitialized);
  }

  if (prevInitState < ProcessInitState::PartialGlobalOptions) {
    RefPtr<IGlobalOptions> globalOpts;
    mInitResult = wrapped::CoCreateInstance(
        CLSID_GlobalOptions, nullptr, CLSCTX_INPROC_SERVER, IID_IGlobalOptions,
        getter_AddRefs(globalOpts));
    MOZ_ASSERT(SUCCEEDED(mInitResult));
    if (FAILED(mInitResult)) {
      return;
    }

    // Disable COM's catch-all exception handler
    mInitResult = globalOpts->Set(COMGLB_EXCEPTION_HANDLING,
                                  COMGLB_EXCEPTION_DONOT_HANDLE_ANY);
    MOZ_ASSERT(SUCCEEDED(mInitResult));
    if (FAILED(mInitResult)) {
      return;
    }

    lock.SetInitState(ProcessInitState::PartialGlobalOptions);
  }

  // Disable the BSTR cache (as it never invalidates, thus leaking memory)
  // (This function is itself idempotent, so we do not concern ourselves with
  // tracking whether or not we've already called it.)
  ::SetOaNoCache();

  lock.SetInitState(ProcessInitState::FullyInitialized);
}

#if defined(MOZILLA_INTERNAL_API)
/**
 * Guaranteed to run *after* the COM (and possible sandboxing) initialization
 * has successfully completed and stabilized. This method MUST BE IDEMPOTENT!
 */
/* static */ void ProcessRuntime::PostInit() {
  // Currently "roughed-in" but unused.
}
#endif  // defined(MOZILLA_INTERNAL_API)

/* static */
DWORD
ProcessRuntime::GetClientThreadId() {
  DWORD callerTid;
  HRESULT hr = ::CoGetCallerTID(&callerTid);
  // Don't return callerTid unless the call succeeded and returned S_FALSE,
  // indicating that the caller originates from a different process.
  if (hr != S_FALSE) {
    return 0;
  }

  return callerTid;
}

MOZ_COLD static HRESULT DiagnosticAssertOrHResultFromLastError() {
  HRESULT hr = HRESULT_FROM_WIN32(::GetLastError());
  MOZ_DIAGNOSTIC_ASSERT(SUCCEEDED(hr));
  return hr;
}

/* static */
HRESULT
ProcessRuntime::InitializeSecurity(const ProcessCategory aProcessCategory) {
  // Reserve enough room on the stack for the ACL to hold the maximum number of
  // ACCESS_ALLOWED_ACEs we might add. The calculation is explained in the
  // Remarks section of the documentation for InitializeAcl.
  const size_t kMaxAceCount = 5;
  constexpr auto requiredACLSize =
      sizeof(ACL) + (kMaxAceCount * (sizeof(ACCESS_ALLOWED_ACE) -
                                     sizeof(ACCESS_ALLOWED_ACE::SidStart) +
                                     SECURITY_MAX_SID_SIZE));
  static_assert((requiredACLSize % sizeof(DWORD)) == 0,
                "ACL length must be DWORD aligned.");
  alignas(DWORD) BYTE aclBytes[requiredACLSize];
  auto* pAcl = reinterpret_cast<ACL*>(aclBytes);
  if (!::InitializeAcl(pAcl, requiredACLSize, ACL_REVISION)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  HANDLE rawProcessToken;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY,
                          &rawProcessToken)) {
    return DiagnosticAssertOrHResultFromLastError();
  }
  nsAutoHandle processToken(rawProcessToken);

  // Grant access to the user's SID.
  DWORD len = 0;
  SE_TOKEN_USER tokenUser;
  if (!::GetTokenInformation(processToken, TokenUser, &tokenUser,
                             sizeof(tokenUser), &len)) {
    return DiagnosticAssertOrHResultFromLastError();
  }
  if (!::AddAccessAllowedAce(pAcl, ACL_REVISION, COM_RIGHTS_EXECUTE,
                             &tokenUser.Sid)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  // Grant access to the SYSTEM SID.
  SE_SID wellKnownSid;
  DWORD wellKnownSidSize = sizeof(wellKnownSid);
  if (!::CreateWellKnownSid(WinLocalSystemSid, nullptr, &wellKnownSid,
                            &wellKnownSidSize)) {
    return DiagnosticAssertOrHResultFromLastError();
  }
  if (!::AddAccessAllowedAce(pAcl, ACL_REVISION, COM_RIGHTS_EXECUTE,
                             &wellKnownSid)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  // Grant access to the Administrator SID.
  wellKnownSidSize = sizeof(wellKnownSid);
  if (!::CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, &wellKnownSid,
                            &wellKnownSidSize)) {
    return DiagnosticAssertOrHResultFromLastError();
  }
  if (!::AddAccessAllowedAce(pAcl, ACL_REVISION, COM_RIGHTS_EXECUTE,
                             &wellKnownSid)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  // If we are the browser process, grant access to all non restricted app
  // containers.
  const bool appContainersSupported = IsWin8OrLater();
  if (aProcessCategory == ProcessCategory::GeckoBrowserParent && appContainersSupported) {
    wellKnownSidSize = sizeof(wellKnownSid);
    if (!::CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, &wellKnownSid,
                              &wellKnownSidSize)) {
      return DiagnosticAssertOrHResultFromLastError();
    }
    if (!::AddAccessAllowedAce(pAcl, ACL_REVISION, COM_RIGHTS_EXECUTE,
                               &wellKnownSid)) {
      return DiagnosticAssertOrHResultFromLastError();
    }
  }

  // If in an app container grant access to it. Don't fail if we get an error
  // retrieving the app container SID.
  alignas(TOKEN_APPCONTAINER_INFORMATION)
      BYTE appContainerInfoBuf[TOKEN_APPCONTAINER_SID_MAX_SIZE];
  auto* appContainerInfo =
      reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(appContainerInfoBuf);
  bool haveAppContainerSid =
      ::GetTokenInformation(processToken, TokenAppContainerSid,
                            appContainerInfo, sizeof(appContainerInfoBuf),
                            &len) &&
      appContainerInfo->TokenAppContainer;
  if (haveAppContainerSid) {
    if (!::AddAccessAllowedAce(pAcl, ACL_REVISION, COM_RIGHTS_EXECUTE,
                               appContainerInfo->TokenAppContainer)) {
      return DiagnosticAssertOrHResultFromLastError();
    }
  }

  alignas(TOKEN_PRIMARY_GROUP)
      BYTE primaryGroupBuf[sizeof(TOKEN_PRIMARY_GROUP) + SECURITY_MAX_SID_SIZE];
  auto* primaryGroup = reinterpret_cast<TOKEN_PRIMARY_GROUP*>(primaryGroupBuf);
  if (!::GetTokenInformation(processToken, TokenPrimaryGroup, primaryGroup,
                             sizeof(primaryGroupBuf), &len)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  SECURITY_DESCRIPTOR sd;
  if (!::InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  if (!::SetSecurityDescriptorDacl(&sd, TRUE, pAcl, FALSE)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  if (!::SetSecurityDescriptorOwner(&sd, tokenUser.User.Sid, FALSE)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  if (!::SetSecurityDescriptorGroup(&sd, primaryGroup->PrimaryGroup, FALSE)) {
    return DiagnosticAssertOrHResultFromLastError();
  }

  HRESULT hr = wrapped::CoInitializeSecurity(
      &sd, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
      RPC_C_IMP_LEVEL_IDENTIFY, nullptr, EOAC_NONE, nullptr);
  MOZ_DIAGNOSTIC_ASSERT(SUCCEEDED(hr));
  return hr;
}

}  // namespace mscom
}  // namespace mozilla
