/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dom_tabs_ContentThread_h
#define dom_tabs_ContentThread_h 1

#include "ContentChild.h"
#include "mozilla/ipc/ProcessChild.h"
#include "nsXREDirProvider.h"

#if defined(XP_WIN)
#  include "mozilla/mscom/ProcessRuntime.h"
#endif

namespace mozilla::dom {

/**
 * ContentProcess is a singleton on the content process which represents
 * the main thread where tab instances live.
 */
class ContentProcess : public mozilla::ipc::ProcessChild {
  using ProcessChild = mozilla::ipc::ProcessChild;

 public:
  ContentProcess(IPC::Channel::ChannelHandle aClientChannel,
                 ProcessId aParentPid, const nsID& aMessageChannelId);
  ~ContentProcess();

  virtual bool Init(int aArgc, char* aArgv[]) override;
  virtual void CleanUp() override;

 private:
  void InfallibleInit(int aArgc, char* aArgv[]);

  ContentChild mContent;
#if defined(XP_WIN)
  // ole32.dll is delay-loaded from xul.dll. On Windows XP, the first
  // CoInitializeEx call can otherwise fail while the COM MTA is created.
  struct Ole32Preloader {
    Ole32Preloader() { ::LoadLibraryW(L"ole32.dll"); }
  };

  Ole32Preloader mOle32Preloader;

  // This object initializes and configures COM. This must happen prior to
  // constructing mXREEmbed.
  mozilla::mscom::ProcessRuntime mCOMRuntime;
#endif
  nsXREDirProvider mDirProvider;
};

}  // namespace mozilla::dom

#endif  // ifndef dom_tabs_ContentThread_h
