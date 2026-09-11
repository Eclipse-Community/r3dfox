/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/mscom/AgileReference.h"

#include <utility>

#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DynamicallyLinkedFunctionPtr.h"
#include "mozilla/mscom/Utils.h"

#if defined(MOZILLA_INTERNAL_API)
#  include "nsDebug.h"
#  include "nsPrintfCString.h"
#endif  // defined(MOZILLA_INTERNAL_API)

#if defined(__MINGW32__)

// Declarations from Windows SDK specific to Windows 8.1.
enum AgileReferenceOptions {
  AGILEREFERENCE_DEFAULT = 0,
  AGILEREFERENCE_DELAYEDMARSHAL = 1,
};

HRESULT WINAPI RoGetAgileReference(AgileReferenceOptions options, REFIID riid,
                                   IUnknown* pUnk,
                                   IAgileReference** ppAgileReference);

#endif  // defined(__MINGW32__)

namespace mozilla::mscom::detail {

GlobalInterfaceTableCookie::GlobalInterfaceTableCookie(IUnknown* aObject,
                                                       REFIID aIid,
                                                       HRESULT& aOutHResult)
    : mCookie(0) {
  IGlobalInterfaceTable* git = ObtainGit();
  MOZ_ASSERT(git);
  if (!git) {
    aOutHResult = E_POINTER;
    return;
  }

  aOutHResult = git->RegisterInterfaceInGlobal(aObject, aIid, &mCookie);
  MOZ_ASSERT(SUCCEEDED(aOutHResult));
}

GlobalInterfaceTableCookie::~GlobalInterfaceTableCookie() {
  IGlobalInterfaceTable* git = ObtainGit();
  MOZ_ASSERT(git);
  if (!git) {
    return;
  }

  DebugOnly<HRESULT> hr = git->RevokeInterfaceFromGlobal(mCookie);
#if defined(MOZILLA_INTERNAL_API)
  NS_WARNING_ASSERTION(
      SUCCEEDED(hr),
      nsPrintfCString("IGlobalInterfaceTable::RevokeInterfaceFromGlobal failed "
                      "with HRESULT 0x%08lX",
                      ((HRESULT)hr))
          .get());
#else
  MOZ_ASSERT(SUCCEEDED(hr));
#endif  // defined(MOZILLA_INTERNAL_API)
  mCookie = 0;
}

HRESULT GlobalInterfaceTableCookie::GetInterface(REFIID aIid,
                                                 void** aOutInterface) const {
  IGlobalInterfaceTable* git = ObtainGit();
  MOZ_ASSERT(git);
  if (!git || !IsValid()) {
    return E_UNEXPECTED;
  }

  return git->GetInterfaceFromGlobal(mCookie, aIid, aOutInterface);
}

/* static */
IGlobalInterfaceTable* GlobalInterfaceTableCookie::ObtainGit() {
  // Internally to COM, the Global Interface Table is a singleton, therefore we
  // don't worry about holding onto this reference indefinitely.
  static IGlobalInterfaceTable* sGit = []() -> IGlobalInterfaceTable* {
    IGlobalInterfaceTable* result = nullptr;
    DebugOnly<HRESULT> hr = ::CoCreateInstance(
        CLSID_StdGlobalInterfaceTable, nullptr, CLSCTX_INPROC_SERVER,
        IID_IGlobalInterfaceTable, reinterpret_cast<void**>(&result));
    MOZ_ASSERT(SUCCEEDED(hr));
    return result;
  }();

  return sGit;
}

HRESULT AgileReference_CreateImpl(
    RefPtr<IAgileReference>& aAgileRef,
    RefPtr<GlobalInterfaceTableCookie>& aGitCookie, REFIID aIid,
    IUnknown* aObject) {
  MOZ_ASSERT(aObject);
  MOZ_ASSERT(IsCOMInitializedOnCurrentThread());

  // RoGetAgileReference is unavailable on Windows versions before 8.1. Keep
  // it dynamically linked so those systems can use the GIT fallback without
  // taking a static import dependency on the missing API.
  static const mozilla::StaticDynamicallyLinkedFunctionPtr<
      decltype(&::RoGetAgileReference)>
      pRoGetAgileReference(L"ole32.dll", "RoGetAgileReference");

  aAgileRef = nullptr;
  aGitCookie = nullptr;

  HRESULT hr = E_NOINTERFACE;
  if (pRoGetAgileReference) {
    hr = pRoGetAgileReference(AGILEREFERENCE_DEFAULT, aIid, aObject,
                              getter_AddRefs(aAgileRef));
    if (SUCCEEDED(hr)) {
      return hr;
    }
  }

  // RoGetAgileReference is unavailable or failed. The GIT is supported by the
  // older Windows versions for which this fallback exists.
  aGitCookie = new GlobalInterfaceTableCookie(aObject, aIid, hr);
  return hr;
}

HRESULT AgileReference_ResolveImpl(
    RefPtr<IAgileReference> const& aAgileRef,
    RefPtr<GlobalInterfaceTableCookie> const& aGitCookie, REFIID aStoredIid,
    REFIID aIid, void** aOutInterface) {
  MOZ_ASSERT(aOutInterface);
  MOZ_ASSERT((aAgileRef || aGitCookie) && !(aAgileRef && aGitCookie));
  MOZ_ASSERT(IsCOMInitializedOnCurrentThread());

  if (!aOutInterface) {
    return E_INVALIDARG;
  }

  *aOutInterface = nullptr;

  if (aAgileRef) {
    // IAgileReference lets us directly resolve the requested interface.
    return aAgileRef->Resolve(aIid, aOutInterface);
  }

  if (!aGitCookie || !aGitCookie->IsValid()) {
    return E_UNEXPECTED;
  }

  RefPtr<IUnknown> originalInterface;
  HRESULT hr = aGitCookie->GetInterface(
      aStoredIid, getter_AddRefs(originalInterface));
  if (FAILED(hr)) {
    return hr;
  }

  if (aIid == aStoredIid) {
    originalInterface.forget(aOutInterface);
    return S_OK;
  }

  // The GIT returns the interface used for registration. QueryInterface is
  // needed when the caller requests a different interface.
  return originalInterface->QueryInterface(aIid, aOutInterface);
}

}  // namespace mozilla::mscom::detail
