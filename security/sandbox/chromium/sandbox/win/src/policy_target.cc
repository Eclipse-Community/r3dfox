// Copyright 2006-2008 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/win/src/policy_target.h"

#include <ntstatus.h>
#include <stddef.h>

#include "sandbox/win/src/crosscall_client.h"
#include "sandbox/win/src/ipc_tags.h"
#include "sandbox/win/src/policy_engine_processor.h"
#include "sandbox/win/src/policy_low_level.h"
#include "sandbox/win/src/policy_params.h"
#include "sandbox/win/src/sandbox_factory.h"
#include "sandbox/win/src/sandbox_nt_util.h"
#include "sandbox/win/src/sharedmem_ipc_client.h"
#include "sandbox/win/src/target_services.h"

using namespace std::literals;

namespace sandbox {

// Policy data.
extern void* volatile g_shared_policy_memory;
SANDBOX_INTERCEPT size_t g_shared_policy_size;

bool QueryBroker(IpcTag ipc_id, CountedParameterSetBase* params) {
  DCHECK_NT(static_cast<size_t>(ipc_id) < kMaxServiceCount);

  if (static_cast<size_t>(ipc_id) >= kMaxServiceCount)
    return false;

  // Policy is only sent if required.
  if (!g_shared_policy_memory) {
    CHECK_NT(g_shared_policy_size);
    return false;
  }

  PolicyGlobal* global_policy =
      reinterpret_cast<PolicyGlobal*>(g_shared_policy_memory);

  if (!global_policy->entry[static_cast<size_t>(ipc_id)])
    return false;

  PolicyBuffer* policy = reinterpret_cast<PolicyBuffer*>(
      reinterpret_cast<char*>(g_shared_policy_memory) +
      reinterpret_cast<size_t>(
          global_policy->entry[static_cast<size_t>(ipc_id)]));

  if ((reinterpret_cast<size_t>(
           global_policy->entry[static_cast<size_t>(ipc_id)]) >
       global_policy->data_size) ||
      (g_shared_policy_size < global_policy->data_size)) {
    NOTREACHED_NT();
    return false;
  }

  for (size_t i = 0; i < params->count; i++) {
    if (!params->parameters[i].IsValid()) {
      NOTREACHED_NT();
      return false;
    }
  }

  PolicyProcessor processor(policy);
  PolicyResult result =
      processor.Evaluate(kShortEval, params->parameters, params->count);
  DCHECK_NT(POLICY_ERROR != result);

  return POLICY_MATCH == result && ASK_BROKER == processor.GetAction();
}

// -----------------------------------------------------------------------

// Hooks NtImpersonateAnonymousToken so we can block until call to LowerToken.
// This means a non-retricted token behaves the same as restricted one before
// LowerToken and prevents us from being left with an anonymous logon token
// because we are blocking the RevertToSelf that would undo it.
NTSTATUS WINAPI TargetNtImpersonateAnonymousToken(
    NtImpersonateAnonymousTokenFunction orig_ImpersonateAnonymousToken,
    HANDLE thread) {
  if (!SandboxFactory::GetTargetServices()->GetState()->RevertedToSelf()) {
    return STATUS_ACCESS_DENIED;
  }

  return orig_ImpersonateAnonymousToken(thread);
}

// Split out so that it can use AddressSanitizer.
NOINLINE bool IsKnownDlls(HANDLE handle) {
  auto root_path = GetPathFromHandle(handle);
  if (!root_path) {
    return false;
  }

#if defined(_WIN64)
  constexpr auto kKnownDllsDir = LR"(\KnownDlls)"sv;
#else
  constexpr auto kKnownDllsDir = LR"(\KnownDlls32)"sv;
#endif
  return root_path->length() == kKnownDllsDir.length() &&
         _wcsnicmp(root_path->data(), kKnownDllsDir.data(),
                   kKnownDllsDir.length()) == 0;
}

// Hooks NtOpenSection when directed by the config, so that we can detect calls
// to open KnownDlls entries and always return not found. This will cause
// fall-back to the normal loading path. This means that if a config blocks
// access to the KnownDlls list, but allows read access to the actual DLLs then
// they can continue to be loaded.
// This is called too early to use AddressSanitizer.
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
SANDBOX_INTERCEPT NTSTATUS __stdcall TargetNtOpenSection(
    NtOpenSectionFunction orig_NtOpenSection, PHANDLE section_handle,
    ACCESS_MASK desired_access, POBJECT_ATTRIBUTES object_attributes) {

  NTSTATUS open_status =
      orig_NtOpenSection(section_handle, desired_access, object_attributes);
  // We're only interested in failure that might be caused by the sandbox.
  if (open_status != STATUS_ACCESS_DENIED) {
    return open_status;
  }

  // Calls for KnownDlls use a RootDirectory.
  if (!object_attributes->RootDirectory) {
    return open_status;
  }

  // Make sure IsKnownDlls isn't called too early, so that everything it uses is
  // loaded. We shouldn't get here before that for KnownDlls. 
  if (!SandboxFactory::GetTargetServices()->GetState()->InitCalled()) {
    return open_status;
  }

  if (!IsKnownDlls(object_attributes->RootDirectory)) {
    return open_status;
  }

  // This is for a KnownDll, just return not found to trigger fall-back loading.
  return STATUS_OBJECT_NAME_NOT_FOUND;
}

// Hooks NtSetInformationThread to block RevertToSelf from being
// called before the actual call to LowerToken.
NTSTATUS WINAPI TargetNtSetInformationThread(
    NtSetInformationThreadFunction orig_SetInformationThread,
    HANDLE thread,
    THREADINFOCLASS thread_info_class,
    PVOID thread_information,
    ULONG thread_information_bytes) {
  do {
    if (SandboxFactory::GetTargetServices()->GetState()->RevertedToSelf())
      break;
    if (ThreadImpersonationToken != thread_info_class)
      break;
    // This is a revert to self.
    return STATUS_SUCCESS;
  } while (false);

  return orig_SetInformationThread(
      thread, thread_info_class, thread_information, thread_information_bytes);
}

// Hooks NtOpenThreadToken to force the open_as_self parameter to be set to
// false if we are still running with the impersonation token. open_as_self set
// to true means that the token will be open using the process token instead of
// the impersonation token. This is bad because the process token does not have
// access to open the thread token.
NTSTATUS WINAPI
TargetNtOpenThreadToken(NtOpenThreadTokenFunction orig_OpenThreadToken,
                        HANDLE thread,
                        ACCESS_MASK desired_access,
                        BOOLEAN open_as_self,
                        PHANDLE token) {
  if (!SandboxFactory::GetTargetServices()->GetState()->RevertedToSelf())
    open_as_self = false;

  return orig_OpenThreadToken(thread, desired_access, open_as_self, token);
}

// See comment for TargetNtOpenThreadToken
NTSTATUS WINAPI
TargetNtOpenThreadTokenEx(NtOpenThreadTokenExFunction orig_OpenThreadTokenEx,
                          HANDLE thread,
                          ACCESS_MASK desired_access,
                          BOOLEAN open_as_self,
                          ULONG handle_attributes,
                          PHANDLE token) {
  if (!SandboxFactory::GetTargetServices()->GetState()->RevertedToSelf())
    open_as_self = false;

  return orig_OpenThreadTokenEx(thread, desired_access, open_as_self,
                                handle_attributes, token);
}

}  // namespace sandbox
