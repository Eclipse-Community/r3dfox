/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowsTestDebug.h"
#include "nsCOMPtr.h"
#include "nsIFile.h"
#include "nsTArray.h"
#include "nsXPCOM.h"

namespace {
typedef DWORD(WINAPI* RMSTARTSESSION)(DWORD*, DWORD, WCHAR[]);
typedef DWORD(WINAPI* RMREGISTERRESOURCES)(DWORD, UINT, LPCWSTR[], UINT,
                                           RM_UNIQUE_PROCESS[], UINT,
                                           LPCWSTR[]);
typedef DWORD(WINAPI* RMGETLIST)(DWORD, UINT*, UINT*, RM_PROCESS_INFO[],
                                 LPDWORD);
typedef DWORD(WINAPI* RMENDSESSION)(DWORD);
}  // namespace

namespace mozilla::widget {

class WindowsDebugProcessData : public nsIWindowsDebugProcessData {
 public:
  NS_DECL_ISUPPORTS

  WindowsDebugProcessData(const wchar_t* aName, const wchar_t* aFilePath,
                          unsigned long aPid)
      : mName(aName), mFilePath(aFilePath), mPid(aPid) {}

  NS_IMETHODIMP GetName(nsAString& aOut) {
    aOut = mName;
    return NS_OK;
  }

  NS_IMETHODIMP GetExecutablePath(nsAString& aOut) {
    aOut = mFilePath;
    return NS_OK;
  }

  NS_IMETHODIMP GetPid(uint32_t* aOut) {
    *aOut = mPid;
    return NS_OK;
  }

 private:
  virtual ~WindowsDebugProcessData() = default;
  nsString mName;
  nsString mFilePath;
  unsigned long mPid;
};

NS_IMPL_ISUPPORTS(WindowsDebugProcessData, nsIWindowsDebugProcessData)
NS_IMPL_ISUPPORTS(WindowsTestDebug, nsIWindowsTestDebug)

WindowsTestDebug::WindowsTestDebug() {}

WindowsTestDebug::~WindowsTestDebug() {}

static nsReturnRef<HMODULE> MakeRestartManager() {
  nsModuleHandle module(::LoadLibraryW(L"Rstrtmgr.dll"));
  (void)NS_WARN_IF(!module);
  return module.out();
}

NS_IMETHODIMP
WindowsTestDebug::ProcessesThatOpenedFile(
    const nsAString& aFilename,
    nsTArray<RefPtr<nsIWindowsDebugProcessData>>& aResult) {
  nsModuleHandle module(MakeRestartManager());
  if (!module) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  auto RmStartSession = reinterpret_cast<RMSTARTSESSION>(
      ::GetProcAddress(module, "RmStartSession"));
  if (NS_WARN_IF(!RmStartSession)) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  auto RmRegisterResources = reinterpret_cast<RMREGISTERRESOURCES>(
      ::GetProcAddress(module, "RmRegisterResources"));
  if (NS_WARN_IF(!RmRegisterResources)) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  auto RmGetList =
      reinterpret_cast<RMGETLIST>(::GetProcAddress(module, "RmGetList"));
  if (NS_WARN_IF(!RmGetList)) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  auto RmEndSession =
      reinterpret_cast<RMENDSESSION>(::GetProcAddress(module, "RmEndSession"));
  if (NS_WARN_IF(!RmEndSession)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = {0};
  DWORD handle;
  if (NS_WARN_IF(FAILED(RmStartSession(&handle, 0, sessionKey)))) {
    return NS_ERROR_FAILURE;
  }

  auto endSession = MakeScopeExit(
      [&, handle]() { (void)NS_WARN_IF(FAILED(RmEndSession(handle))); });

  nsPromiseFlatString flatFilename(aFilename);
  LPCWSTR resources[] = {flatFilename.getW()};
  if (NS_WARN_IF(FAILED(
          RmRegisterResources(handle, 1, resources, 0, nullptr, 0, nullptr)))) {
    return NS_ERROR_FAILURE;
  }

  AutoTArray<RM_PROCESS_INFO, 1> info;
  UINT numEntries;
  UINT numEntriesNeeded = 1;
  DWORD error = ERROR_MORE_DATA;
  DWORD reason = RmRebootReasonNone;
  while (error == ERROR_MORE_DATA) {
    info.SetLength(numEntriesNeeded);
    numEntries = numEntriesNeeded;
    error =
        RmGetList(handle, &numEntriesNeeded, &numEntries, &info[0], &reason);
  }
  if (NS_WARN_IF(error != ERROR_SUCCESS)) {
    return NS_ERROR_FAILURE;
  }

  for (UINT i = 0; i < numEntries; ++i) {
    nsAutoHandle otherProcess(::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info[i].Process.dwProcessId));
    if (NS_WARN_IF(!otherProcess)) {
      continue;
    }
    WCHAR imageName[MAX_PATH];
    DWORD imageNameLen = MAX_PATH;
typedef BOOL(WINAPI * QUERYFULLPROCESSIMAGENAMEW)(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize);
    static QUERYFULLPROCESSIMAGENAMEW pQueryFullProcessImageNameW = NULL;

            HMODULE hKernel = LoadLibraryW( L"kernel32" );

            if( !pQueryFullProcessImageNameW )
                pQueryFullProcessImageNameW = (QUERYFULLPROCESSIMAGENAMEW)GetProcAddress( hKernel, "QueryFullProcessImageNameW" );

    if (!pQueryFullProcessImageNameW || NS_WARN_IF(FAILED(pQueryFullProcessImageNameW(
            otherProcess, 0, imageName, &imageNameLen)))) {
      continue;
    }
    aResult.AppendElement(new WindowsDebugProcessData(
        info[i].strAppName, imageName, info[i].Process.dwProcessId));
  }

  return NS_OK;
}

}  // namespace mozilla::widget
