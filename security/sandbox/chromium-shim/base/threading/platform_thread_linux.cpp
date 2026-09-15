/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a cut down version of Chromium source file base/threading/platform_thread_linux.h
// with only the functions required. It also has a dummy implementation of
// SetCurrentThreadTypeForPlatform, which should not be called.

#include "base/threading/platform_thread.h"

#include "base/message_loop/message_pump_type.h"
#include "base/threading/platform_thread_internal_posix.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

#include "mozilla/Assertions.h"

namespace base {
namespace internal {

const ThreadPriorityToNiceValuePairForTest
    kThreadPriorityToNiceValueMapForTest[7] = {
        {ThreadPriorityForTest::kRealtimeAudio, -10},
        {ThreadPriorityForTest::kDisplay, -8},
        {ThreadPriorityForTest::kNormal, 0},
        {ThreadPriorityForTest::kResourceEfficient, 1},
        {ThreadPriorityForTest::kUtility, 2},
        {ThreadPriorityForTest::kBackground, 10},
};

// These nice values are shared with ChromeOS platform code
// (platform_thread_cros.cc) and have to be unique as ChromeOS has a unique
// type -> nice value mapping. An exception is kCompositing and
// kDisplayCritical where aliasing is OK as they have the same scheduler
// attributes (cpusets, latency_sensitive etc) including nice value.
// The uniqueness of the nice value per-type helps to change and restore the
// scheduling params of threads when their process toggles between FG and BG.
const ThreadTypeToNiceValuePair kThreadTypeToNiceValueMap[7] = {
    {ThreadType::kBackground, 10},       {ThreadType::kUtility, 2},
    {ThreadType::kResourceEfficient, 1}, {ThreadType::kDefault, 0},
#if BUILDFLAG(IS_CHROMEOS)
    {ThreadType::kCompositing, -8},
#else
    // TODO(1329208): Experiment with bringing IS_LINUX inline with IS_CHROMEOS.
    {ThreadType::kCompositing, -1},
#endif
    {ThreadType::kDisplayCritical, -8},  {ThreadType::kRealtimeAudio, -10},
};

bool SetCurrentThreadTypeForPlatform(ThreadType thread_type,
                                     MessagePumpType pump_type_hint) {
  MOZ_CRASH();
}

absl::optional<ThreadPriorityForTest>
GetCurrentThreadPriorityForPlatformForTest() {
  int maybe_sched_rr = 0;
  struct sched_param maybe_realtime_prio = {0};
  if (pthread_getschedparam(pthread_self(), &maybe_sched_rr,
                            &maybe_realtime_prio) == 0 &&
      maybe_sched_rr == SCHED_RR &&
      maybe_realtime_prio.sched_priority ==
          PlatformThreadLinux::kRealTimeAudioPrio.sched_priority) {
    return absl::make_optional(ThreadPriorityForTest::kRealtimeAudio);
  }
  return absl::nullopt;
}

}  // namespace internal

void InitThreading() {}

void TerminateOnThread() {}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
#if !defined(THREAD_SANITIZER)
  return 0;
#else
  // ThreadSanitizer bloats the stack heavily. Evidence has been that the
  // default stack size isn't enough for some browser tests.
  return 2 * (1 << 23);  // 2 times 8192K (the default stack size on Linux).
#endif
}

}  // namespace base
