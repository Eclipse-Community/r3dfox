/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/WindowsBCryptInitialization.h"

#include "mozilla/RandomNum.h"
#include "nsWindowsDllInterceptor.h"

#include <windows.h>

using RtlGenRandomFn = BOOLEAN(WINAPI*)(PVOID, ULONG);

namespace mozilla {

bool WindowsBCryptInitialization() {
  HMODULE h = GetModuleHandleW(L"advapi32.dll");
  if (!h) return false;
  auto fn = reinterpret_cast<RtlGenRandomFn>(
              GetProcAddress(h, "SystemFunction036"));
  if (!fn) return false;
  UCHAR buffer[32];
  return !!fn(buffer, sizeof(buffer));
}

}  // namespace mozilla
