/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a partial implementation of Chromium's source file
// //sandbox/win/src/sandbox_policy_diagnostic.h

#ifndef SANDBOX_WIN_SRC_SANDBOX_POLICY_DIAGNOSTIC_H_
#define SANDBOX_WIN_SRC_SANDBOX_POLICY_DIAGNOSTIC_H_

#include "sandbox/win/src/sandbox.h"

#include "mozilla/Assertions.h"

namespace sandbox {

class PolicyBase;

class PolicyDiagnostic final : public PolicyInfo {
 public:

  PolicyDiagnostic(const PolicyDiagnostic&) = delete;
  PolicyDiagnostic& operator=(const PolicyDiagnostic&) = delete;

  PolicyDiagnostic(PolicyBase*) {}
  ~PolicyDiagnostic() override = default;
  const char* JsonString() override { MOZ_CRASH(); }

};

}  // namespace sandbox

#endif  // SANDBOX_WIN_SRC_SANDBOX_POLICY_DIAGNOSTIC_H_
