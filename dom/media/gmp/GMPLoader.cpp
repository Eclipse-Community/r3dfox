/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPLoader.h"
#include <stdio.h>
#include "mozilla/Attributes.h"
#include "nsExceptionHandler.h"
#include "GMPLog.h"
#include "gmp-entrypoints.h"
#include "prlink.h"
#include "prenv.h"
#include "prerror.h"
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "mozilla/sandboxTarget.h"
#  include "nsWindowsHelpers.h"
#endif
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#  include "mozilla/SandboxInfo.h"
#  include "mozilla/SandboxProfilerObserver.h"
#endif

#include <string>

#ifdef XP_WIN
#  include <windows.h>
#endif

namespace mozilla::gmp {
class PassThroughGMPAdapter : public GMPAdapter {
 public:
  ~PassThroughGMPAdapter() override {
    // Ensure we're always shutdown, even if caller forgets to call
    // GMPShutdown().
    GMPShutdown();
  }

  void SetAdaptee(PRLibrary* aLib) override { mLib = aLib; }

  GMPErr GMPInit(const GMPPlatformAPI* aPlatformAPI) override {
    if (NS_WARN_IF(!mLib)) {
      MOZ_CRASH("Missing library!");
      return GMPGenericErr;
    }
    GMPInitFunc initFunc =
        reinterpret_cast<GMPInitFunc>(PR_FindFunctionSymbol(mLib, "GMPInit"));
    if (!initFunc) {
      MOZ_CRASH("Missing init method!");
      return GMPNotImplementedErr;
    }
    return initFunc(aPlatformAPI);
  }

  GMPErr GMPGetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI,
                   const nsACString& /* aKeySystem */) override {
    if (!mLib) {
      return GMPGenericErr;
    }
    GMPGetAPIFunc getapiFunc = reinterpret_cast<GMPGetAPIFunc>(
        PR_FindFunctionSymbol(mLib, "GMPGetAPI"));
    if (!getapiFunc) {
      return GMPNotImplementedErr;
    }
    return getapiFunc(aAPIName, aHostAPI, aPluginAPI);
  }

  void GMPShutdown() override {
    if (mLib) {
      GMPShutdownFunc shutdownFunc = reinterpret_cast<GMPShutdownFunc>(
          PR_FindFunctionSymbol(mLib, "GMPShutdown"));
      if (shutdownFunc) {
        shutdownFunc();
      }
      PR_UnloadLibrary(mLib);
      mLib = nullptr;
    }
  }

 private:
  PRLibrary* mLib = nullptr;
};

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
// This performs the same checks for an AppLocker policy that are performed in
// SaferpIsV2PolicyPresent from ntdll.dll, they are used to decide whether an
// AppLocker ioctl call is made.
static bool IsAppLockerPolicyPresent() {
  // RuleCount check for policy configured via Local Security Policy.
  DWORD ruleCount = 0;
  DWORD ruleCountSize = sizeof(ruleCount);
  if (RegGetValueW(HKEY_LOCAL_MACHINE,
                   LR"(SYSTEM\CurrentControlSet\Control\Srp\GP)", L"RuleCount",
                   RRF_RT_REG_DWORD, nullptr, &ruleCount,
                   &ruleCountSize) == ERROR_SUCCESS &&
      ruleCount != 0) {
    return true;
  }

  // Directory check for policy configured via Mobile Device Management.
  static constexpr wchar_t appLockerMDMPath[] = LR"(\System32\AppLocker\MDM)";
  wchar_t path[MAX_PATH + sizeof(appLockerMDMPath) / sizeof(wchar_t)];
  UINT len = GetSystemWindowsDirectoryW(path, MAX_PATH);
  if (len != 0 && len < MAX_PATH) {
    wcscpy(path + len, appLockerMDMPath);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
  }
  return false;
}

static void EnsureAppLockerCacheIsWarm(const wchar_t* aWidePath) {
  // IOCTL to \Device\SrpDevice (\\.\SrpDevice via DosDevices) that triggers
  // AppLocker to cache the allow/deny decision for the DLL, warming the NTFS
  // EA cache before the sandbox starts.
  static constexpr DWORD IOCTL_SRP_VERIFY_DLL = 0x225804;
  static constexpr wchar_t kSrpDevicePath[] = LR"(\\.\SrpDevice)";

  // Buffer layout: [HANDLE as 8 bytes][USHORT pathBytes][WCHAR path...]
  // The handle field is always 8 bytes. On x86 the handle is zero-extended.
  struct SrpIoctlBuffer {
    uint64_t handle;
    USHORT pathBytes;
    WCHAR path[1];
  };
  static constexpr DWORD kSrpHeaderSize = offsetof(SrpIoctlBuffer, path);

  UniquePtr<HANDLE, CloseHandleDeleter> fileHandle(CreateFileW(
      aWidePath, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (fileHandle.get() == INVALID_HANDLE_VALUE) {
    GMP_LOG_WARNING("EnsureAppLockerCacheIsWarm: CreateFileW failed (%lu)",
                    GetLastError());
    return;
  }

  NtPathFromDosPath ntPath(aWidePath);
  if (!ntPath.IsValid()) {
    return;
  }

  DWORD ioctlSize = kSrpHeaderSize + ntPath.LengthInBytes();
  auto buf = MakeUnique<uint8_t[]>(ioctlSize);
  auto* srp = reinterpret_cast<SrpIoctlBuffer*>(buf.get());

  // ULONG_PTR is pointer-sized (4 bytes on x86, 8 on x64). Casting to uint64_t
  // zero-extends on x86, matching the cdq zero-extension in x86 ntdll.
  srp->handle =
      static_cast<uint64_t>(reinterpret_cast<ULONG_PTR>(fileHandle.get()));
  srp->pathBytes = ntPath.LengthInBytes();
  if (!ntPath.CopyTo(
          mozilla::Span(srp->path, ntPath.LengthInBytes() / sizeof(WCHAR)))) {
    MOZ_DIAGNOSTIC_ASSERT(false, "CopyTo failed: buffer too small");
    return;
  }

  UniquePtr<HANDLE, CloseHandleDeleter> srpDevice(
      CreateFileW(kSrpDevicePath, FILE_READ_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, 0, nullptr));
  if (srpDevice.get() == INVALID_HANDLE_VALUE) {
    GMP_LOG_WARNING(
        "EnsureAppLockerCacheIsWarm: opening SrpDevice failed (%lu)",
        GetLastError());
    return;
  }

  DWORD outBuf = 0;
  DWORD bytesReturned = 0;
  if (!DeviceIoControl(srpDevice.get(), IOCTL_SRP_VERIFY_DLL, srp, ioctlSize,
                       &outBuf, sizeof(outBuf), &bytesReturned, nullptr)) {
    GMP_LOG_DEBUG(
        "EnsureAppLockerCacheIsWarm: DeviceIoControl failed (%lu), "
        "AppLocker may not be enabled",
        GetLastError());
  }
}
#endif

bool GMPLoader::Load(const char* aUTF8LibPath, uint32_t aUTF8LibPathLen,
                     const GMPPlatformAPI* aPlatformAPI, GMPAdapter* aAdapter) {
  CrashReporter::AutoRecordAnnotation autoLibPath(
      CrashReporter::Annotation::GMPLibraryPath,
      nsDependentCString(aUTF8LibPath));

  // Load the GMP.
  PRLibSpec libSpec;
#ifdef XP_WIN
  int pathLen = MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, nullptr, 0);
  if (pathLen == 0) {
    MOZ_CRASH("Cannot get path length as wide char!");
    return false;
  }

  auto widePath = MakeUnique<wchar_t[]>(pathLen);
  if (MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, widePath.get(),
                          pathLen) == 0) {
    MOZ_CRASH("Cannot convert path to wide char!");
    return false;
  }

#  if defined(MOZ_SANDBOX)
  if (IsAppLockerPolicyPresent()) {
    EnsureAppLockerCacheIsWarm(widePath.get());
  }
#  endif
#endif

  if (!getenv("MOZ_DISABLE_GMP_SANDBOX") && mSandboxStarter &&
      !mSandboxStarter->Start(aUTF8LibPath)) {
    MOZ_CRASH("Cannot start sandbox!");
    return false;
  }

#ifdef XP_WIN
  libSpec.value.pathname_u = widePath.get();
  libSpec.type = PR_LibSpec_PathnameU;
#else
  libSpec.value.pathname = aUTF8LibPath;
  libSpec.type = PR_LibSpec_Pathname;
#endif
  PRLibrary* lib = PR_LoadLibraryWithFlags(libSpec, 0);
  if (!lib) {
    MOZ_CRASH_UNSAFE_PRINTF("Cannot load plugin as library %d %d",
                            PR_GetError(), PR_GetOSError());
    return false;
  }

  mAdapter.reset((!aAdapter) ? new PassThroughGMPAdapter() : aAdapter);
  mAdapter->SetAdaptee(lib);

  if (mAdapter->GMPInit(aPlatformAPI) != GMPNoErr) {
    MOZ_CRASH("Cannot initialize plugin adapter!");
    return false;
  }

  return true;
}

GMPErr GMPLoader::GetAPI(const char* aAPIName, void* aHostAPI,
                         void** aPluginAPI, const nsACString& aKeySystem) {
  return mAdapter->GMPGetAPI(aAPIName, aHostAPI, aPluginAPI, aKeySystem);
}

void GMPLoader::Shutdown() {
  if (mAdapter) {
    mAdapter->GMPShutdown();
  }
}

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
class WinSandboxStarter : public mozilla::gmp::SandboxStarter {
 public:
  bool Start(const char* aLibPath) override {
    // Cause advapi32 to load before the sandbox is turned on, as
    // Widevine version 970 and later require it and the sandbox
    // blocks it on Win7.
    unsigned int dummy_rand;
    rand_s(&dummy_rand);

    mozilla::SandboxTarget::Instance()->StartSandbox();
    return true;
  }
};
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
namespace {
class LinuxSandboxStarter : public mozilla::gmp::SandboxStarter {
 private:
  LinuxSandboxStarter() = default;
  friend mozilla::detail::UniqueSelector<LinuxSandboxStarter>::SingleObject
  mozilla::MakeUnique<LinuxSandboxStarter>();

 public:
  static UniquePtr<SandboxStarter> Make() {
    if (mozilla::SandboxInfo::Get().CanSandboxMedia()) {
      return MakeUnique<LinuxSandboxStarter>();
    }
    // Sandboxing isn't possible, but the parent has already
    // checked that this plugin doesn't require it.  (Bug 1074561)
    return nullptr;
  }
  bool Start(const char* aLibPath) override {
    RegisterProfilerObserversForSandboxProfiler();
    mozilla::SetMediaPluginSandbox(aLibPath);
    return true;
  }
};
}  // anonymous namespace
#endif  // XP_LINUX && MOZ_SANDBOX

static UniquePtr<SandboxStarter> MakeSandboxStarter() {
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  return mozilla::MakeUnique<WinSandboxStarter>();
#elif defined(XP_LINUX) && defined(MOZ_SANDBOX)
  return LinuxSandboxStarter::Make();
#else
  return nullptr;
#endif
}

GMPLoader::GMPLoader() : mSandboxStarter(MakeSandboxStarter()) {}

bool GMPLoader::CanSandbox() const { return !!mSandboxStarter; }

}  // namespace mozilla::gmp
