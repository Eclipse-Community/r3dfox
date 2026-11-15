/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/WindowsBCryptInitialization.h"

#include "nsWindowsDllInterceptor.h"

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include "mozilla/RandomNum.h"
namespace mozilla {

bool WindowsBCryptInitialization() {
  // Call RandomUint64 to pre-load bcryptPrimitives.dll while the current
  // thread still has an unrestricted impersonation token.
  LoadLibraryW(L"bcryptprimitives");
  RandomUint64OrDie();
  return true;
}

}  // namespace mozilla
