/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AvailableMemoryWatcher.h"
#include "mozilla/Atomics.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_browser.h"
#include "nsAppRunner.h"
#include "nsExceptionHandler.h"
#include "nsICrashReporter.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsITimer.h"
#include "nsMemoryPressure.h"
#include "nsServiceManagerUtils.h"

#include <windows.h>
#include <memoryapi.h>
#include <algorithm>

extern mozilla::Atomic<uint32_t, mozilla::MemoryOrdering::Relaxed>
    sNumLowPhysicalMemEvents;

namespace mozilla {

class nsAvailableMemoryWatcher final : public nsITimerCallback,
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
  static void RecordLowMemoryEvent();

  static bool IsCommitSpaceLow();
  static bool IsPhysicalMemoryLow();
  static bool IsMemoryLow();

  ~nsAvailableMemoryWatcher();

  void MaybeSaveMemoryReport(const MutexAutoLock&) MOZ_REQUIRES(mMutex);
  void Shutdown(const MutexAutoLock&) MOZ_REQUIRES(mMutex);

  void OnLowMemory(const MutexAutoLock&) MOZ_REQUIRES(mMutex);
  void OnHighMemory(const MutexAutoLock&) MOZ_REQUIRES(mMutex);

  nsCOMPtr<nsITimer> mTimer MOZ_GUARDED_BY(mMutex);

  bool mUnderMemoryPressure MOZ_GUARDED_BY(mMutex);
  bool mSavedReport MOZ_GUARDED_BY(mMutex);
  bool mIsShutdown MOZ_GUARDED_BY(mMutex);

  uint32_t mPollingInterval;
};

NS_IMPL_ISUPPORTS_INHERITED(nsAvailableMemoryWatcher,
                            nsAvailableMemoryWatcherBase,
                            nsIObserver,
                            nsITimerCallback,
                            nsINamed)

nsAvailableMemoryWatcher::nsAvailableMemoryWatcher()
    : mUnderMemoryPressure(false),
      mSavedReport(false),
      mIsShutdown(false),
      mPollingInterval(1000) {}

nsresult nsAvailableMemoryWatcher::Init() {
  nsresult rv = nsAvailableMemoryWatcherBase::Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  MutexAutoLock lock(mMutex);

  mTimer = NS_NewTimer();
  if (!mTimer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  uint32_t interval =
      StaticPrefs::browser_memory_poll_interval_ms();

  interval = std::clamp(interval, 100u, 60000u);

  mPollingInterval = gIsGtest ? 10 : interval;

  rv = mTimer->InitWithCallback(
      this,
      mPollingInterval,
      nsITimer::TYPE_REPEATING_SLACK);

  if (NS_FAILED(rv)) {
    return rv;
  }

  static_assert(sizeof(sNumLowPhysicalMemEvents) == sizeof(uint32_t));

  CrashReporter::RegisterAnnotationU32(
      CrashReporter::Annotation::LowPhysicalMemoryEvents,
      reinterpret_cast<uint32_t*>(&sNumLowPhysicalMemEvents));

  return NS_OK;
}

nsAvailableMemoryWatcher::~nsAvailableMemoryWatcher() = default;

void nsAvailableMemoryWatcher::RecordLowMemoryEvent() {
  sNumLowPhysicalMemEvents++;
}

void nsAvailableMemoryWatcher::Shutdown(const MutexAutoLock&) {
  mIsShutdown = true;

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void nsAvailableMemoryWatcher::MaybeSaveMemoryReport(
    const MutexAutoLock&) {
  if (mSavedReport) {
    return;
  }

  if (nsCOMPtr<nsICrashReporter> cr =
          do_GetService("@mozilla.org/toolkit/crash-reporter;1")) {
    mSavedReport = NS_SUCCEEDED(cr->SaveMemoryReport());
  }
}

void nsAvailableMemoryWatcher::OnLowMemory(
    const MutexAutoLock& aLock) {
  if (!mUnderMemoryPressure) {
    RecordLowMemoryEvent();
  }

  mUnderMemoryPressure = true;

  if (NS_IsMainThread()) {
    MaybeSaveMemoryReport(aLock);
    UpdateLowMemoryTimeStamp();

    {
      MutexAutoUnlock unlock(mMutex);
      mTabUnloader->UnloadTabAsync();
    }
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsAvailableMemoryWatcher::OnLowMemory",
        [self = RefPtr{this}]() {
          {
            MutexAutoLock lock(self->mMutex);
            self->MaybeSaveMemoryReport(lock);
            self->UpdateLowMemoryTimeStamp();
          }

          self->mTabUnloader->UnloadTabAsync();
        }));
  }
}

void nsAvailableMemoryWatcher::OnHighMemory(
    const MutexAutoLock& aLock) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mUnderMemoryPressure) {
    RecordTelemetryEventOnHighMemory(aLock);

    NS_NotifyOfEventualMemoryPressure(
        MemoryPressureState::NoPressure);
  }

  mUnderMemoryPressure = false;
  mSavedReport = false;
}

bool nsAvailableMemoryWatcher::IsCommitSpaceLow() {
  MEMORYSTATUSEX memStatus = {sizeof(memStatus)};

  if (!::GlobalMemoryStatusEx(&memStatus)) {
    return false;
  }

  constexpr uint64_t MB = 1024 * 1024;

  uint64_t availCommitMB =
      memStatus.ullAvailPageFile / MB;

  return availCommitMB <
         StaticPrefs::browser_low_commit_space_threshold_mb();
}

bool nsAvailableMemoryWatcher::IsPhysicalMemoryLow() {
  MEMORYSTATUSEX memStatus = {sizeof(memStatus)};

  if (!::GlobalMemoryStatusEx(&memStatus)) {
    return false;
  }

  constexpr uint64_t MB = 1024 * 1024;

  uint64_t availPhysMB =
      memStatus.ullAvailPhys / MB;

  return availPhysMB <
         StaticPrefs::
             browser_low_physical_memory_threshold_mb();
}

bool nsAvailableMemoryWatcher::IsMemoryLow() {
  return IsCommitSpaceLow() ||
         IsPhysicalMemoryLow();
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::Notify(nsITimer* aTimer) {
  MutexAutoLock lock(mMutex);

  if (mIsShutdown) {
    return NS_OK;
  }

  if (IsMemoryLow()) {
    OnLowMemory(lock);
  } else {
    OnHighMemory(lock);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::GetName(nsACString& aName) {
  aName.AssignLiteral("nsAvailableMemoryWatcher");
  return NS_OK;
}

NS_IMETHODIMP
nsAvailableMemoryWatcher::Observe(nsISupports* aSubject,
                                  const char* aTopic,
                                  const char16_t* aData) {
  nsresult rv =
      nsAvailableMemoryWatcherBase::Observe(
          aSubject, aTopic, aData);

  if (NS_FAILED(rv)) {
    return rv;
  }

  MutexAutoLock lock(mMutex);

  if (strcmp(aTopic, "xpcom-shutdown") == 0) {
    Shutdown(lock);
  }

  return NS_OK;
}

already_AddRefed<nsAvailableMemoryWatcherBase>
CreateAvailableMemoryWatcher() {
  RefPtr watcher(new nsAvailableMemoryWatcher);

  if (NS_FAILED(watcher->Init())) {
    return do_AddRef(new nsAvailableMemoryWatcherBase);
  }

  return watcher.forget();
}

}  // namespace mozilla
