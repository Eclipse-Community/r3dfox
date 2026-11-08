/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#ifdef XP_WIN
#  include <windows.h>
#else
#  include <time.h>
#  include <unistd.h>
#endif

#include "mozilla/Flow.h"

// Some infrastructure that we use for computing a per process UUID

// FNV-1a hasher
// We use this to construct a randomish UUID by hashing the PID and the
// current time.
//
// It's probably not the best choice of hasher but it works for Perfetto
// so it's good enough for us.
struct Hasher {
  static const uint64_t kOffsetBasis = 14695981039346656037ULL;
  static const uint64_t kPrime = 1099511628211ULL;

  uint64_t hash = kOffsetBasis;

  void Update(const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
      hash ^= ptr[i];
      hash *= kPrime;
    }
  }
  void Update(uint64_t value) { Update(&value, sizeof(value)); }
  void Update(uint32_t value) { Update(&value, sizeof(value)); }

  uint64_t Get() const { return hash; }
};

// Used only when GetTickCount64 is not available on the platform.
// Last result of GetTickCount call.
//
// Kept in [ms]
static DWORD sLastGTCResult = 0;

// Higher part of the 64-bit value of MozGetTickCount64,
// incremented atomically.
static DWORD sLastGTCRollover = 0;

typedef ULONGLONG (WINAPI* GetTickCount64_t)();
static GetTickCount64_t sGetTickCount64 = nullptr;

// Function protecting GetTickCount result from rolling over,
// result is in [ms]
static ULONGLONG WINAPI
MozGetTickCount64()
{
  DWORD GTC = ::GetTickCount();

  // Pull the rollover counter forward only if new value of GTC goes way
  // down under the last saved result
  if ((sLastGTCResult > GTC) && ((sLastGTCResult - GTC) > (1UL << 30))) {
    ++sLastGTCRollover;
  }

  sLastGTCResult = GTC;
  return ULONGLONG(sLastGTCRollover) << 32 | sLastGTCResult;
}

static uint64_t CurrentTime() {
#ifdef XP_WIN
  HMODULE kernelDLL = GetModuleHandleW(L"kernel32.dll");
  sGetTickCount64 = reinterpret_cast<GetTickCount64_t>(
    GetProcAddress(kernelDLL, "GetTickCount64"));
  if (!sGetTickCount64) {
    // If the platform does not support the GetTickCount64 (Windows XP doesn't),
    // then use our fallback implementation based on GetTickCount.
    sGetTickCount64 = MozGetTickCount64;
  }

  return sGetTickCount64();
#else
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static uint64_t CurrentPID() {
#ifdef XP_WIN
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

// This is inspired by the perfetto code in TrackRegistry::ComputeProcessUuid
uint64_t ComputeProcessUUID() {
  auto pid = CurrentPID();
  auto time = CurrentTime();
  Hasher hasher;
  hasher.Update(pid);
  hasher.Update(time);
  return hasher.Get();
}

MFBT_DATA uint64_t gProcessUUID;

MFBT_API void Flow::Init() { gProcessUUID = ComputeProcessUUID(); }
