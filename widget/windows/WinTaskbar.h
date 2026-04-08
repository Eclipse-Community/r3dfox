/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WinTaskbar_h_
#define WinTaskbar_h_

#include <windows.h>
#include <shobjidl.h>
#undef LogSeverity  // SetupAPI.h #defines this as DWORD
#include "nsIWinTaskbar.h"

namespace mozilla {
namespace widget {

class WinTaskbar final : public nsIWinTaskbar {
  ~WinTaskbar();

 public:
  WinTaskbar();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWINTASKBAR

  static bool GenerateAppUserModelID(nsAString& aAppUserModelId,
                                     bool aPrivateBrowsing = false);
  // Registers the global app user model id for the instance.
  // See comments in WinTaskbar.cpp for more information.
  static bool RegisterAppUserModelID();
  static bool GetAppUserModelID(nsAString& aDefaultGroupId,
                                bool aPrivateBrowsing = false);

 private:
  bool Initialize();

  typedef HRESULT(WINAPI* SetCurrentProcessExplicitAppUserModelIDPtr)(
      PCWSTR AppID);
  ITaskbarList4* mTaskbar;
};

}  // namespace widget
}  // namespace mozilla

#endif /* WinTaskbar_h_ */
