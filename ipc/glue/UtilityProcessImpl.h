/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_ipc_glue_UtilityProcessImpl_h__
#define _include_ipc_glue_UtilityProcessImpl_h__
#include "mozilla/ipc/ProcessChild.h"

#if defined(XP_WIN)
#  include "mozilla/mscom/ProcessRuntime.h"
#endif

#include "mozilla/ipc/UtilityProcessChild.h"

namespace mozilla::ipc {

// This class owns the subprocess instance of a PUtilityProcess - which in this
// case, is a UtilityProcessParent. It is instantiated as a singleton in
// XRE_InitChildProcess.
class UtilityProcessImpl final : public ipc::ProcessChild {
 public:
  using ipc::ProcessChild::ProcessChild;
  ~UtilityProcessImpl();

  bool Init(int aArgc, char* aArgv[]) override;
  void CleanUp() override;

#if defined(XP_WIN)
  static void LoadLibraryOrCrash(LPCWSTR aLib);
#endif  // defined(XP_WIN)

 private:
  RefPtr<UtilityProcessChild> mUtility = UtilityProcessChild::GetSingleton();

#if defined(XP_WIN)
  // ole32.dll is delay-loaded from xul.dll. In the Utility process the
  // mscom::ProcessRuntime member below initializes COM (and thus delay-loads
  // ole32) in its constructor, before Init() runs and before the sandbox is
  // lowered. On Windows XP the Generic Utility process had not loaded ole32
  // by that point, and the very first delay-load resolved a bogus module
  // handle (0x1B), failing GetProcAddress(CoInitializeEx) and crashing with
  // 0xC06D007F. Preload ole32.dll (a required system DLL) first so that the
  // subsequent delay-load is a no-op against the already-loaded module.
  // Mirrors WMFPreloadForSandbox's LoadLibraryOrCrash(L"ole32.dll").
  struct Ole32Preloader {
    Ole32Preloader() { ::LoadLibraryW(L"ole32.dll"); }
  };
  Ole32Preloader mOle32Preloader;
  mozilla::mscom::ProcessRuntime mCOMRuntime;
#endif
};

}  // namespace mozilla::ipc

#endif  // _include_ipc_glue_UtilityProcessImpl_h__
