/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_windowsA_h
#define mozilla_windowsA_h

// Include the "real" windows.h header.
//
// Also turn off deprecation warnings, as we may be wrapping deprecated fns.

#pragma GCC system_header
#include_next <windows.h>

#define LWSTDAPI_(type)   EXTERN_C DECLSPEC_IMPORT type STDAPICALLTYPE
LWSTDAPI_(LSTATUS) SHRegGetValueW(_In_ HKEY hkey, _In_opt_ LPCWSTR pszSubKey, _In_opt_ LPCWSTR pszValue,
    _In_ int srrfFlags, _Out_opt_ DWORD *pdwType, _Out_writes_bytes_to_opt_(*pcbData,*pcbData) void *pvData,
    _Inout_opt_ DWORD *pcbData);
#define RegGetValueW   SHRegGetValueW
#pragma comment (lib, "shlwapi.lib")

#endif // !defined(mozilla_windowsA_h)
