/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a partial implementation of Chromium's source file
// base/win/win_util.h

#ifndef BASE_WIN_WIN_UTIL_H_
#define BASE_WIN_WIN_UTIL_H_

#include <minwindef.h>
#include <string>

#include "base/base_export.h"

namespace base {
namespace win {

inline HANDLE Uint32ToHandle(uint32_t h) {
  return reinterpret_cast<HANDLE>(
      static_cast<uintptr_t>(static_cast<int32_t>(h)));
}

// Returns the name of a desktop or a window station.
BASE_EXPORT std::wstring GetWindowObjectName(HANDLE handle);

// IsAppVerifierLoaded() indicates whether Application Verifier is *already*
// loaded into the current process.
BASE_EXPORT bool IsAppVerifierLoaded();

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_WIN_UTIL_H_
