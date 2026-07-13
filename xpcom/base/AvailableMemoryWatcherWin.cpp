/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AvailableMemoryWatcher.h"
#include "mozilla/Atomics.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/TimeStamp.h"
#include "nsAppRunner.h"
#include "nsError.h"
#include "nsExceptionHandler.h"
#include "nsICrashReporter.h"
#include "nsIConsoleService.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsITimer.h"
#include "nsMemoryPressure.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"

#include <cstring>
#include <windows.h>
#include <memoryapi.h>

extern mozilla::Atomic<uint32_t, mozilla::MemoryOrdering::Relaxed>
    sNumLowPhysicalMemEvents;

namespace mozilla {

namespace {

// Physical-memory defaults used when the user has not explicitly configured
// the corresponding about:config preferences.
constexpr uint32_t kDefaultLowPhysicalMemoryThresholdMB = 1024;
constexpr uint32_t kDefaultPhysicalMemoryPollRampWindowMB = 1024;

// Polling defaults.
constexpr uint32_t kDefaultInitialPollingIntervalMs = 500;
constexpr uint32_t kDefaultMinPollingIntervalMs = 500;
constexpr uint32_t kDefaultMaxPollingIntervalMs = 3000;

// Preference guardrails.
constexpr uint32_t kMaximumMemoryThresholdMB = 1024 * 1024;
constexpr uint32_t kMaximumRampWindowMB = 1024 * 1024;
constexpr uint32_t kMinimumPollingIntervalMs = 500;
constexpr uint32_t kMaximumPollingIntervalMs = 10 * 60 * 1000;

constexpr uint64_t kBytesPerMB = 1024 * 1024;

// Available physical memory at or below this level triggers the low-memory
// response.
constexpr char kLowPhysicalMemoryThresholdMBPref[] =
    "browser.low_physical_memory_threshold_mb";

// This value is added to the physical-memory trigger threshold to determine
// where adaptive polling begins.
//
// For example:
//
//   physical threshold = 1024 MB
//   ramp window        = 1024 MB
//   ramp start         = 2048 MB
constexpr char kPhysicalMemoryPollRampWindowMBPref[] =
    "browser.low_physical_memory_poll_ramp_window_mb";

constexpr char kInitialPollingIntervalMsPref[] =
    "browser.low_memory_polling_initial_interval_ms";

constexpr char kMinPollingIntervalMsPref[] =
    "browser.low_memory_polling_min_interval_ms";

constexpr char kMaxPollingIntervalMsPref[] =
    "browser.low_memory_polling_max_interval_ms";

// Browser Console diagnostics are enabled by default.
//
// Set this Boolean pref to false to disable them:
//
//   browser.low_memory_watcher_console_logging
constexpr char kConsoleLoggingPref[] =
    "browser.low_memory_watcher_console_logging";

static_assert(kDefaultLowPhysicalMemoryThresholdMB > 0);
static_assert(kDefaultPhysicalMemoryPollRampWindowMB > 0);
static_assert(kDefaultMinPollingIntervalMs <=
              kDefaultMaxPollingIntervalMs);
static_assert(kDefaultInitialPollingIntervalMs > 0);

struct MemoryStatusSnapshot {
  uint64_t mAvailablePhysicalMB = 0;
  uint64_t mAvailableCommitMB = 0;
};

uint32_t ClampUint32(uint32_t aValue,
                     uint32_t aMinimum,
                     uint32_t aMaximum) {
  MOZ_ASSERT(aMinimum <= aMaximum);

  if (aValue < aMinimum) {
    return aMinimum;
  }

  if (aValue > aMaximum) {
    return aMaximum;
  }

  return aValue;
}

/**
 * Uses the supplied fallback when the user has not explicitly created or
 * changed the preference.
 *
 * This guarantees the requested 1024 MB physical trigger and 1024 MB ramp
 * window when no user settings are configured, even if another default-branch
 * value exists elsewhere in the tree.
 */
uint32_t GetUserUintPrefOrDefault(const char* aPrefName,
                                  uint32_t aDefaultValue) {
  if (!Preferences::HasUserValue(aPrefName)) {
    return aDefaultValue;
  }

  return Preferences::GetUint(aPrefName, aDefaultValue);
}

bool GetUserBoolPrefOrDefault(const char* aPrefName,
                              bool aDefaultValue) {
  if (!Preferences::HasUserValue(aPrefName)) {
    return aDefaultValue;
  }

  return Preferences::GetBool(aPrefName, aDefaultValue);
}

bool ConsoleLoggingEnabled() {
  return GetUserBoolPrefOrDefault(kConsoleLoggingPref, true);
}

void LogToBrowserConsole(const nsACString& aMessage) {
  if (!ConsoleLoggingEnabled()) {
    return;
  }

  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIConsoleService> consoleService =
      do_GetService(NS_CONSOLESERVICE_CONTRACTID);

  if (!consoleService) {
    return;
  }

  nsAutoString fullMessage;
  fullMessage.AssignLiteral(u"[MemoryPressureWatcher] ");
  fullMessage.Append(NS_ConvertUTF8toUTF16(aMessage));

  (void)consoleService->LogStringMessage(fullMessage.get());
}

#define MP_CONSOLE_LOG(...) \
  LogToBrowserConsole(nsPrintfCString(__VA_ARGS__))

uint32_t LowPhysicalMemoryThresholdMB() {
  return ClampUint32(
      GetUserUintPrefOrDefault(
          kLowPhysicalMemoryThresholdMBPref,
          kDefaultLowPhysicalMemoryThresholdMB),
      0,
      kMaximumMemoryThresholdMB);
}

uint32_t PhysicalMemoryPollRampWindowMB() {
  return ClampUint32(
      GetUserUintPrefOrDefault(
          kPhysicalMemoryPollRampWindowMBPref,
          kDefaultPhysicalMemoryPollRampWindowMB),
      0,
      kMaximumRampWindowMB);
}

uint32_t MinPollingIntervalMs() {
  return ClampUint32(
      GetUserUintPrefOrDefault(
          kMinPollingIntervalMsPref,
          kDefaultMinPollingIntervalMs),
      kMinimumPollingIntervalMs,
      kMaximumPollingIntervalMs);
}

uint32_t MaxPollingIntervalMs() {
  const uint32_t minimumIntervalMs =
      MinPollingIntervalMs();

  return ClampUint32(
      GetUserUintPrefOrDefault(
          kMaxPollingIntervalMsPref,
          kDefaultMaxPollingIntervalMs),
      minimumIntervalMs,
      kMaximumPollingIntervalMs);
}

uint32_t InitialPollingIntervalMs() {
  if (gIsGtest) {
    return 10;
  }

  return ClampUint32(
      GetUserUintPrefOrDefault(
          kInitialPollingIntervalMsPref,
          kDefaultInitialPollingIntervalMs),
      MinPollingIntervalMs(),
      MaxPollingIntervalMs());
}

uint64_t PhysicalMemoryPollRampStartMB() {
  return static_cast<uint64_t>(
             LowPhysicalMemoryThresholdMB()) +
         PhysicalMemoryPollRampWindowMB();
}

uint64_t CommitSpaceThresholdMB() {
  return StaticPrefs::
      browser_low_commit_space_threshold_mb();
}

void LogCurrentPreferenceValues() {
  MP_CONSOLE_LOG(
      "preferences: "
      "physicalThreshold=%u MB, "
      "physicalRampWindow=%u MB, "
      "physicalRampStart=%llu MB, "
      "commitThreshold=%llu MB, "
      "initialInterval=%u ms, "
      "minimumInterval=%u ms, "
      "maximumInterval=%u ms",
      LowPhysicalMemoryThresholdMB(),
      PhysicalMemoryPollRampWindowMB(),
      static_cast<unsigned long long>(
          PhysicalMemoryPollRampStartMB()),
      static_cast<unsigned long long>(
          CommitSpaceThresholdMB()),
      InitialPollingIntervalMs(),
      MinPollingIntervalMs(),
      MaxPollingIntervalMs());
}

bool GetMemoryStatusSnapshot(
    MemoryStatusSnapshot& aSnapshot) {
  MEMORYSTATUSEX memoryStatus = {
      sizeof(memoryStatus)};

  if (!::GlobalMemoryStatusEx(&memoryStatus)) {
    return false;
  }

  aSnapshot.mAvailablePhysicalMB =
      memoryStatus.ullAvailPhys / kBytesPerMB;

  // Remaining system commit capacity.
  aSnapshot.mAvailableCommitMB =
      memoryStatus.ullAvailPageFile / kBytesPerMB;

  return true;
}

}  // namespace

/**
 * Windows available-memory watcher.
 *
 * The low-memory response is triggered when either:
 *
 *   available physical memory <= physical-memory threshold
 *
 * or:
 *
 *   available commit space <= commit-space threshold
 *
 * Adaptive polling is based on physical memory:
 *
 *   physical >= threshold + ramp window
 *       Maximum polling interval.
 *
 *   threshold < physical < threshold + ramp window
 *       Quadratically interpolated polling interval.
 *
 *   physical <= threshold
 *       Minimum polling interval and low-memory response.
 *
 * Commit-space pressure is an immediate low-memory condition. While commit
 * space remains low, the watcher also uses the minimum polling interval.
 */
class nsAvailableMemoryWatcher final
    : public nsITimerCallback,
      public nsINamed,
      public nsAvailableMemoryWatcherBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  nsAvailableMemoryWatcher();

  nsresult Init() override;

 private:
  ~nsAvailableMemoryWatcher();

  static void RecordLowMemoryEvent();

  static bool IsPhysicalMemoryLow(
      const MemoryStatusSnapshot& aSnapshot);

  static bool IsCommitSpaceLow(
      const MemoryStatusSnapshot& aSnapshot);

  static bool IsMemoryLow(
      const MemoryStatusSnapshot& aSnapshot);

  static uint32_t GetAdaptivePollingIntervalMs(
      bool aAlreadyUnderMemoryPressure,
      const MemoryStatusSnapshot* aSnapshot);

  nsresult OnUnloadAttemptCompleted(
      nsresult aResult) override;

  void MaybeSaveMemoryReport(
      const MutexAutoLock&)
      MOZ_REQUIRES(mMutex);

  void Shutdown(
      const MutexAutoLock&)
      MOZ_REQUIRES(mMutex);

  void LogPollFired(
      const MutexAutoLock&)
      MOZ_REQUIRES(mMutex);

  void ScheduleNextPoll(
      const MutexAutoLock&,
      const MemoryStatusSnapshot* aSnapshot)
      MOZ_REQUIRES(mMutex);

  void OnLowMemory(
      const MutexAutoLock&)
      MOZ_REQUIRES(mMutex);

  void OnHighMemory(
      const MutexAutoLock&)
      MOZ_REQUIRES(mMutex);

  nsCOMPtr<nsITimer> mTimer
      MOZ_GUARDED_BY(mMutex);

  bool mUnderMemoryPressure
      MOZ_GUARDED_BY(mMutex);

  bool mSavedReport
      MOZ_GUARDED_BY(mMutex);

  bool mIsShutdown
      MOZ_GUARDED_BY(mMutex);

  uint64_t mPollNumber
      MOZ_GUARDED_BY(mMutex);

  TimeStamp mLastPollTime
      MOZ_GUARDED_BY(mMutex);
};

NS_IMPL_ISUPPORTS_INHERITED(
    nsAvailableMemoryWatcher,
    nsAvailableMemoryWatcherBase,
    nsIObserver,
    nsITimerCallback,
    nsINamed)

nsAvailableMemoryWatcher::
    nsAvailableMemoryWatcher()
    : mUnderMemoryPressure(false),
      mSavedReport(false),
      mIsShutdown(false),
      mPollNumber(0) {}

nsAvailableMemoryWatcher::
    ~nsAvailableMemoryWatcher() {
  MOZ_ASSERT(
      !mTimer,
      "The memory watcher timer should be released "
      "during shutdown");
}

nsresult nsAvailableMemoryWatcher::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  MP_CONSOLE_LOG("initialization started");

  nsresult rv =
      nsAvailableMemoryWatcherBase::Init();

  if (NS_FAILED(rv)) {
    MP_CONSOLE_LOG(
        "base initialization failed: rv=0x%08x",
        static_cast<uint32_t>(rv));

    return rv;
  }

  MutexAutoLock lock(mMutex);

  mTimer = NS_NewTimer();

  if (!mTimer) {
    MP_CONSOLE_LOG("timer allocation failed");
    return NS_ERROR_OUT_OF_MEMORY;
  }

  const uint32_t initialIntervalMs =
      InitialPollingIntervalMs();

  LogCurrentPreferenceValues();

  MP_CONSOLE_LOG(
      "scheduling initial poll in %u ms",
      initialIntervalMs);

  rv = mTimer->InitWithCallback(
      this,
      initialIntervalMs,
      nsITimer::TYPE_ONE_SHOT);

  if (NS_FAILED(rv)) {
    MP_CONSOLE_LOG(
        "initial poll scheduling failed: "
        "rv=0x%08x",
        static_cast<uint32_t>(rv));

    mTimer = nullptr;
    return rv;
  }

  static_assert(
      sizeof(sNumLowPhysicalMemEvents) ==
      sizeof(uint32_t));

  CrashReporter::RegisterAnnotationU32(
      CrashReporter::Annotation::
          LowPhysicalMemoryEvents,
      reinterpret_cast<uint32_t*>(
          &sNumLowPhysicalMemEvents));

  MP_CONSOLE_LOG(
      "watcher initialized successfully");

  return NS_OK;
}

void nsAvailableMemoryWatcher::
    RecordLowMemoryEvent() {
  sNumLowPhysicalMemEvents++;
}

void nsAvailableMemoryWatcher::Shutdown(
    const MutexAutoLock&) {
  if (mIsShutdown) {
    return;
  }

  MP_CONSOLE_LOG("shutting down");

  mIsShutdown = true;

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void nsAvailableMemoryWatcher::
    MaybeSaveMemoryReport(
        const MutexAutoLock&) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mSavedReport) {
    return;
  }

  nsCOMPtr<nsICrashReporter> crashReporter =
      do_GetService(
          "@mozilla.org/toolkit/"
          "crash-reporter;1");

  if (!crashReporter) {
    MP_CONSOLE_LOG(
        "crash reporter unavailable; "
        "memory report was not saved");

    return;
  }

  const nsresult rv =
      crashReporter->SaveMemoryReport();

  mSavedReport = NS_SUCCEEDED(rv);

  MP_CONSOLE_LOG(
      "SaveMemoryReport completed: "
      "rv=0x%08x, saved=%d",
      static_cast<uint32_t>(rv),
      static_cast<int>(mSavedReport));
}

void nsAvailableMemoryWatcher::LogPollFired(
    const MutexAutoLock&) {
  ++mPollNumber;

  const TimeStamp now =
      TimeStamp::Now();

  if (mLastPollTime.IsNull()) {
    MP_CONSOLE_LOG(
        "poll #%llu fired; initial poll",
        static_cast<unsigned long long>(
            mPollNumber));
  } else {
    const double elapsedMs =
        (now - mLastPollTime)
            .ToMilliseconds();

    MP_CONSOLE_LOG(
        "poll #%llu fired after %.1f ms; "
        "underMemoryPressure=%d",
        static_cast<unsigned long long>(
            mPollNumber),
        elapsedMs,
        static_cast<int>(
            mUnderMemoryPressure));
  }

  mLastPollTime = now;
}

void nsAvailableMemoryWatcher::
    ScheduleNextPoll(
        const MutexAutoLock&,
        const MemoryStatusSnapshot* aSnapshot) {
  if (mIsShutdown || !mTimer) {
    return;
  }

  const uint32_t intervalMs =
      GetAdaptivePollingIntervalMs(
          mUnderMemoryPressure,
          aSnapshot);

  MP_CONSOLE_LOG(
      "next poll scheduled in %u ms",
      intervalMs);

  const nsresult rv =
      mTimer->InitWithCallback(
          this,
          intervalMs,
          nsITimer::TYPE_ONE_SHOT);

  if (NS_FAILED(rv)) {
    MP_CONSOLE_LOG(
        "next poll scheduling failed: "
        "rv=0x%08x",
        static_cast<uint32_t>(rv));

    mTimer = nullptr;
  }
}

void nsAvailableMemoryWatcher::OnLowMemory(
    const MutexAutoLock& aLock) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mUnderMemoryPressure) {
    mUnderMemoryPressure = true;

    RecordLowMemoryEvent();

    MP_CONSOLE_LOG(
        "entered low-memory state");
  } else {
    MP_CONSOLE_LOG(
        "memory remains below at least "
        "one configured threshold");
  }

  MaybeSaveMemoryReport(aLock);
  UpdateLowMemoryTimeStamp();

  {
    /*
     * TabUnloader eventually calls OnUnloadAttemptCompleted().
     *
     * Do not hold mMutex while requesting the unload because the completion
     * path enters the base watcher and can acquire the same mutex.
     */
    MutexAutoUnlock unlock(mMutex);

    if (!mTabUnloader) {
      MP_CONSOLE_LOG(
          "unload not attempted: "
          "TabUnloader is unavailable");

      return;
    }

    MP_CONSOLE_LOG(
        "requesting asynchronous tab unload");

    (void)mTabUnloader->UnloadTabAsync();
  }
}

void nsAvailableMemoryWatcher::OnHighMemory(
    const MutexAutoLock& aLock) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mUnderMemoryPressure) {
    MP_CONSOLE_LOG(
        "physical memory and commit space "
        "have both recovered");

    RecordTelemetryEventOnHighMemory(aLock);

    NS_NotifyOfEventualMemoryPressure(
        MemoryPressureState::NoPressure);
  }

  mUnderMemoryPressure = false;

  // Permit another report during a future pressure episode.
  mSavedReport = false;
}

bool nsAvailableMemoryWatcher::
    IsPhysicalMemoryLow(
        const MemoryStatusSnapshot& aSnapshot) {
  const uint64_t thresholdMB =
      LowPhysicalMemoryThresholdMB();

  // Zero disables physical-memory-triggered pressure.
  if (thresholdMB == 0) {
    return false;
  }

  return aSnapshot.mAvailablePhysicalMB <=
         thresholdMB;
}

bool nsAvailableMemoryWatcher::
    IsCommitSpaceLow(
        const MemoryStatusSnapshot& aSnapshot) {
  const uint64_t thresholdMB =
      CommitSpaceThresholdMB();

  // Zero disables commit-space-triggered pressure.
  if (thresholdMB == 0) {
    return false;
  }

  return aSnapshot.mAvailableCommitMB <=
         thresholdMB;
}

bool nsAvailableMemoryWatcher::
    IsMemoryLow(
        const MemoryStatusSnapshot& aSnapshot) {
  return IsPhysicalMemoryLow(aSnapshot) ||
         IsCommitSpaceLow(aSnapshot);
}

uint32_t nsAvailableMemoryWatcher::
    GetAdaptivePollingIntervalMs(
        bool aAlreadyUnderMemoryPressure,
        const MemoryStatusSnapshot* aSnapshot) {
  if (gIsGtest) {
    return 10;
  }

  const uint32_t minimumIntervalMs =
      MinPollingIntervalMs();

  const uint32_t maximumIntervalMs =
      MaxPollingIntervalMs();

  /*
   * This includes both physical-memory pressure and commit-space pressure.
   *
   * While either condition remains active, poll at the minimum interval so
   * another unload can be attempted after the previous asynchronous request
   * completes.
   */
  if (aAlreadyUnderMemoryPressure) {
    return minimumIntervalMs;
  }

  /*
   * A failed Windows sample while not already under pressure falls back to the
   * healthy interval.
   */
  if (!aSnapshot) {
    return maximumIntervalMs;
  }

  const uint64_t unloadThresholdMB =
      LowPhysicalMemoryThresholdMB();

  /*
   * A physical threshold of zero disables the physical-memory ramp.
   *
   * Commit pressure can still trigger OnLowMemory(), at which point
   * aAlreadyUnderMemoryPressure causes minimum-interval polling.
   */
  if (unloadThresholdMB == 0) {
    return maximumIntervalMs;
  }

  const uint64_t rampWindowMB =
      PhysicalMemoryPollRampWindowMB();

  const uint64_t availablePhysicalMB =
      aSnapshot->mAvailablePhysicalMB;

  /*
   * A zero ramp window creates a direct transition:
   *
   *   physical > threshold:
   *       maximum interval
   *
   *   physical <= threshold:
   *       minimum interval
   */
  if (rampWindowMB == 0) {
    return availablePhysicalMB <=
                   unloadThresholdMB
               ? minimumIntervalMs
               : maximumIntervalMs;
  }

  const uint64_t rampStartMB =
      unloadThresholdMB +
      rampWindowMB;

  if (availablePhysicalMB >= rampStartMB) {
    return maximumIntervalMs;
  }

  if (availablePhysicalMB <=
      unloadThresholdMB) {
    return minimumIntervalMs;
  }

  /*
   * Quadratic interpolation:
   *
   *   physical == unloadThresholdMB
   *       interval == minimumIntervalMs
   *
   *   physical == rampStartMB
   *       interval == maximumIntervalMs
   *
   * Let:
   *
   *   x = availablePhysicalMB - unloadThresholdMB
   *
   * Then:
   *
   *   interval =
   *       minimum +
   *       (maximum - minimum) *
   *       x^2 / rampWindow^2
   */
  const uint64_t distanceAboveThresholdMB =
      availablePhysicalMB -
      unloadThresholdMB;

  const uint64_t intervalRangeMs =
      static_cast<uint64_t>(
          maximumIntervalMs) -
      minimumIntervalMs;

  const uint64_t distanceSquared =
      distanceAboveThresholdMB *
      distanceAboveThresholdMB;

  const uint64_t rampWindowSquared =
      rampWindowMB *
      rampWindowMB;

  const uint64_t intervalMs =
      minimumIntervalMs +
      intervalRangeMs *
          distanceSquared /
          rampWindowSquared;

  return static_cast<uint32_t>(
      intervalMs);
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::
    OnUnloadAttemptCompleted(
        nsresult aResult) {
  MOZ_ASSERT(NS_IsMainThread());

  switch (aResult) {
    case NS_OK:
      MP_CONSOLE_LOG(
          "unload result: SUCCESS - "
          "one tab was unloaded");
      break;

    case NS_ERROR_NOT_AVAILABLE:
      MP_CONSOLE_LOG(
          "unload result: NO ELIGIBLE TAB - "
          "the base watcher will issue a "
          "Gecko low-memory notification");
      break;

    case NS_ERROR_ABORT:
      MP_CONSOLE_LOG(
          "unload result: BUSY - "
          "another unload operation "
          "was already running");
      break;

    default:
      MP_CONSOLE_LOG(
          "unload result: unexpected "
          "rv=0x%08x",
          static_cast<uint32_t>(aResult));
      break;
  }

  return nsAvailableMemoryWatcherBase::
      OnUnloadAttemptCompleted(aResult);
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::Notify(
    nsITimer* aTimer) {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);

  if (mIsShutdown) {
    return NS_OK;
  }

  if (aTimer != mTimer.get()) {
    MP_CONSOLE_LOG(
        "ignored callback from "
        "an unexpected timer");

    return NS_OK;
  }

  LogPollFired(lock);

  MemoryStatusSnapshot snapshot;

  if (!GetMemoryStatusSnapshot(snapshot)) {
    MP_CONSOLE_LOG(
        "GlobalMemoryStatusEx failed; "
        "the existing pressure state "
        "was not changed");

    ScheduleNextPoll(lock, nullptr);

    return NS_OK;
  }

  const uint64_t physicalThresholdMB =
      LowPhysicalMemoryThresholdMB();

  const uint64_t physicalRampWindowMB =
      PhysicalMemoryPollRampWindowMB();

  const uint64_t physicalRampStartMB =
      physicalThresholdMB +
      physicalRampWindowMB;

  const uint64_t commitThresholdMB =
      CommitSpaceThresholdMB();

  const bool physicalMemoryLow =
      IsPhysicalMemoryLow(snapshot);

  const bool commitSpaceLow =
      IsCommitSpaceLow(snapshot);

  MP_CONSOLE_LOG(
      "memory sample: "
      "availablePhysical=%llu MB, "
      "physicalThreshold=%llu MB, "
      "physicalRampWindow=%llu MB, "
      "physicalRampStart=%llu MB, "
      "physicalLow=%d, "
      "availableCommit=%llu MB, "
      "commitThreshold=%llu MB, "
      "commitLow=%d",
      static_cast<unsigned long long>(
          snapshot.mAvailablePhysicalMB),
      static_cast<unsigned long long>(
          physicalThresholdMB),
      static_cast<unsigned long long>(
          physicalRampWindowMB),
      static_cast<unsigned long long>(
          physicalRampStartMB),
      static_cast<int>(
          physicalMemoryLow),
      static_cast<unsigned long long>(
          snapshot.mAvailableCommitMB),
      static_cast<unsigned long long>(
          commitThresholdMB),
      static_cast<int>(
          commitSpaceLow));

  if (physicalMemoryLow ||
      commitSpaceLow) {
    if (physicalMemoryLow &&
        commitSpaceLow) {
      MP_CONSOLE_LOG(
          "decision: request tab unload - "
          "physical memory and commit "
          "space are low");
    } else if (physicalMemoryLow) {
      MP_CONSOLE_LOG(
          "decision: request tab unload - "
          "physical memory is low");
    } else {
      MP_CONSOLE_LOG(
          "decision: request tab unload - "
          "commit space is low");
    }

    OnLowMemory(lock);
  } else {
    MP_CONSOLE_LOG(
        "decision: no unload - "
        "physical memory and commit "
        "space are healthy");

    OnHighMemory(lock);
  }

  /*
   * If either low-memory condition was true, OnLowMemory() set
   * mUnderMemoryPressure, so the next interval will be the minimum.
   *
   * Otherwise the next interval is calculated from the physical-memory ramp.
   */
  ScheduleNextPoll(lock, &snapshot);

  return NS_OK;
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::GetName(
    nsACString& aName) {
  aName.AssignLiteral(
      "nsAvailableMemoryWatcher");

  return NS_OK;
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::Observe(
    nsISupports* aSubject,
    const char* aTopic,
    const char16_t* aData) {
  nsresult rv =
      nsAvailableMemoryWatcherBase::
          Observe(
              aSubject,
              aTopic,
              aData);

  if (NS_FAILED(rv)) {
    MP_CONSOLE_LOG(
        "base Observe(%s) failed: "
        "rv=0x%08x",
        aTopic ? aTopic : "(null)",
        static_cast<uint32_t>(rv));

    return rv;
  }

  MutexAutoLock lock(mMutex);

  if (aTopic &&
      strcmp(
          aTopic,
          "xpcom-shutdown") == 0) {
    Shutdown(lock);
  }

  return NS_OK;
}

already_AddRefed<
    nsAvailableMemoryWatcherBase>
CreateAvailableMemoryWatcher() {
  RefPtr<nsAvailableMemoryWatcher> watcher =
      new nsAvailableMemoryWatcher();

  const nsresult rv =
      watcher->Init();

  if (NS_FAILED(rv)) {
    MP_CONSOLE_LOG(
        "watcher initialization failed: "
        "rv=0x%08x; using base fallback",
        static_cast<uint32_t>(rv));

    return do_AddRef(
        new nsAvailableMemoryWatcherBase);
  }

  return watcher.forget();
}

}  // namespace mozilla
