/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TimerThread_h_
#define TimerThread_h_

#include "nsIObserver.h"
#include "nsIRunnable.h"
#include "nsIThread.h"

#include "nsTimerImpl.h"
#include "nsThreadUtils.h"

#include "nsTArray.h"

#include "mozilla/Monitor.h"
#include "mozilla/ProfilerUtils.h"
#include "mozilla/Span.h"

#if defined(XP_WIN)
#  include <windows.h>
#endif

// Enable this to compute lots of interesting statistics and print them out when
// PrintStatistics() is called.
#define TIMER_THREAD_STATISTICS 0

class TimerThread final : public mozilla::Runnable, public nsIObserver {
 private:
#if defined(XP_WIN)
  // HiResWindowsMonitor is a simple (Windows-only) implementaton of a monitor
  // that uses a Windows waitable timer object and a Windows event object (along
  // with a mutex) as its synchronization primitives. When precise firing is
  // needed (as determined by the tolerance parameter when waiting) a special
  // high-resolution waitable timer will be used. Otherwise a regular waitable
  // timer will be used.
  // NOTE: Although it's not documented by Microsoft at this moment (as far as I
  // can tell), it seems that hi-res timers are fundamentally different under
  // the hood and don't support a lot of the features that non-hi-res timers
  // support. You cannot use names, callbacks or tolerances with them. The only
  // mention I've seen of this is https://stackoverflow.com/questions/73647588.
  class MOZ_CAPABILITY("monitor") HiResWindowsMonitor final {
   public:
    explicit HiResWindowsMonitor(const char* aName)
        : mMutex(aName), mHandles{{
            CreateWaitableTimerEx(nullptr, nullptr,
                                  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_ALL_ACCESS),
            CreateEvent(nullptr, FALSE, FALSE, nullptr),
            CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS)
          }} {
      // These are MOZ_RELEASE_ASSERT's because, if we fail to create either of
      // those objects, we have no way to continue. GetHiResTimer() is not
      // checked and could possibly be nullptr on old-enough versions of Windows
      // that don't support high-res timers. We allow this and fall back to
      // lo-res timers in that case.
      MOZ_RELEASE_ASSERT(GetEvent() != nullptr);
      MOZ_RELEASE_ASSERT(GetLoResTimer() != nullptr);
    }

    ~HiResWindowsMonitor() {
      [[maybe_unused]] const BOOL b0 = CloseHandle(GetLoResTimer());
      MOZ_ASSERT(b0 != 0);
      [[maybe_unused]] const BOOL b1 = CloseHandle(GetEvent());
      MOZ_ASSERT(b1 != 0);
      if (GetHiResTimer()) {
        [[maybe_unused]] const BOOL b2 = CloseHandle(GetHiResTimer());
        MOZ_ASSERT(b2 != 0);
      }
    }

    MOZ_ALWAYS_INLINE void Lock() MOZ_CAPABILITY_ACQUIRE() { mMutex.Lock(); }

    MOZ_ALWAYS_INLINE void Unlock() MOZ_CAPABILITY_RELEASE() {
      mMutex.Unlock();
    }

    // Sleep until notified.
    void Wait() MOZ_REQUIRES(this) {
      Unlock();
      WaitForSingleObject(GetEvent(), INFINITE);
      Lock();
    }

   private:
    void WaitHiRes(const LARGE_INTEGER* aDuration) MOZ_REQUIRES(this) {
      const BOOL b = SetWaitableTimer(GetHiResTimer(), aDuration, 0, nullptr,
                                        nullptr, 0);
      MOZ_RELEASE_ASSERT(b != 0);
      mMutex.AssertCurrentThreadOwns();
      Unlock();
      const mozilla::Span<const HANDLE, 2> handles{GetHiResHandles()};
      WaitForMultipleObjects(handles.size(), handles.data(), FALSE, INFINITE);
      Lock();
    }

    void WaitLoRes(const LARGE_INTEGER* aDuration, const uint64_t aTolerance_ms)
        MOZ_REQUIRES(this) {
typedef BOOL (WINAPI *pfnSetWaitableTimerEx)(HANDLE hTimer, const LARGE_INTEGER *lpDueTime, LONG lPeriod, PTIMERAPCROUTINE pfnCompletionRoutine, LPVOID lpArgToCompletionRoutine, PREASON_CONTEXT WakeContext, ULONG TolerableDelay);
static pfnSetWaitableTimerEx pSetWaitableTimerEx;

typedef BOOL (WINAPI *pfnSetWaitableTimer)(HANDLE hTimer, const LARGE_INTEGER *lpDueTime, LONG lPeriod, PTIMERAPCROUTINE pfnCompletionRoutine, LPVOID lpArgToCompletionRoutine, BOOL fResume);
static pfnSetWaitableTimer pSetWaitableTimer;

      HMODULE module = GetModuleHandleW(L"kernel32.dll");
      pSetWaitableTimerEx = (pfnSetWaitableTimerEx)GetProcAddress(module, "SetWaitableTimerEx"); // Windows Vista and up
      if (!pSetWaitableTimerEx) {
          pSetWaitableTimer = (pfnSetWaitableTimer)GetProcAddress(module, "SetWaitableTimer");
      }
      const BOOL b = (pSetWaitableTimerEx && pSetWaitableTimerEx(GetLoResTimer(), aDuration, 0, nullptr, nullptr, nullptr, aTolerance_ms)) || pSetWaitableTimer(GetLoResTimer(), aDuration, 0, nullptr, nullptr, 0);
      MOZ_RELEASE_ASSERT(b != 0);
      mMutex.AssertCurrentThreadOwns();
      Unlock();
      const mozilla::Span<const HANDLE, 2> handles{GetLoResHandles()};
      WaitForMultipleObjects(handles.size(), handles.data(), FALSE, INFINITE);
      Lock();
    }

   public:
    // Sleep for the specified number of microseconds or until notified.
    void Wait(const uint64_t aDuration_us, const uint64_t aTolerance_ms)
        MOZ_REQUIRES(this) {
      // duration needs to be in "hundreds of nanoseconds", negative indicates
      // value is relative rather than absolute
      const LARGE_INTEGER duration{
          .QuadPart = static_cast<int64_t>(aDuration_us) * -10LL};

      if (aTolerance_ms <= sHiResThreshold_ms && GetHiResTimer()) {
        WaitHiRes(&duration);
      } else {
        WaitLoRes(&duration, aTolerance_ms);
      }
    }

    // Sleep for the specified number of microseconds or until notified.
    // Negative waits are clamped to zero.
    MOZ_ALWAYS_INLINE void Wait(const double aDuration_us,
                                const double aTolerance_ms) MOZ_REQUIRES(this) {
      const uint64_t duration_us =
          static_cast<uint64_t>(std::max(aDuration_us, 0.0));
      const uint64_t tolerance_ms =
          static_cast<uint64_t>(std::max(aTolerance_ms, 0.0));
      Wait(duration_us, tolerance_ms);
    }

    // Sleep for the specified duration or until notified. Negative waits are
    // clamped to zero.
    void Wait(mozilla::TimeDuration aDuration, mozilla::TimeDuration aTolerance)
        MOZ_REQUIRES(this) {
      if (aDuration != TimeDuration::Forever()) {
        Wait(aDuration.ToMicroseconds(), aTolerance.ToMilliseconds());
      } else {
        Wait();
      }
    }

    // Wake one thread waiting on the monitor.
    MOZ_ALWAYS_INLINE void Notify() {
      const BOOL b = SetEvent(GetEvent());
      MOZ_RELEASE_ASSERT(b != 0);
    }

    void AssertCurrentThreadOwns() const MOZ_ASSERT_CAPABILITY(this) {
      mMutex.AssertCurrentThreadOwns();
    }

    void AssertNotCurrentThreadOwns() const MOZ_ASSERT_CAPABILITY(!this) {
      mMutex.AssertNotCurrentThreadOwns();
    }

   private:
    // Waits with a tolerance at or below this threshold will use hi-res timers.
    static constexpr uint64_t sHiResThreshold_ms = 16;

    // Convenience functions for accessing the handles and not having to
    // remember which is which.
    MOZ_ALWAYS_INLINE HANDLE GetHiResTimer() const { return mHandles[0]; }
    MOZ_ALWAYS_INLINE HANDLE GetEvent() const { return mHandles[1]; }
    MOZ_ALWAYS_INLINE HANDLE GetLoResTimer() const { return mHandles[2]; }

    // Returns a span corresponding to the HANDLEs that are needed for hi-res
    // waiting.
    MOZ_ALWAYS_INLINE mozilla::Span<const HANDLE, 2> GetHiResHandles() const {
      return mozilla::Span<const HANDLE, 3>{mHandles}.Subspan<0, 2>();
    }

    // Returns a span corresponding to the HANDLEs that are needed for lo-res
    // waiting.
    MOZ_ALWAYS_INLINE mozilla::Span<const HANDLE, 2> GetLoResHandles() const {
      return mozilla::Span<const HANDLE, 3>{mHandles}.Subspan<1, 2>();
    }

    mozilla::Mutex mMutex;
    std::array<HANDLE, 3> mHandles;
  };

  typedef HiResWindowsMonitor TimerThreadMonitor;

#else
  typedef mozilla::Monitor TimerThreadMonitor;
#endif

  using TimerThreadMonitorAutoLock =
      mozilla::MonitorAutoLockBase<TimerThreadMonitor>;
  using TimerThreadMonitorAutoUnlock =
      mozilla::MonitorAutoUnlockBase<TimerThreadMonitor>;

 public:
  typedef mozilla::MutexAutoLock MutexAutoLock;
  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;

  TimerThread();

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIOBSERVER

  nsresult Shutdown();

  nsresult AddTimer(nsTimerImpl* aTimer, const MutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(aTimer->mMutex);
  nsresult RemoveTimer(nsTimerImpl* aTimer, const MutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(aTimer->mMutex);
  // Considering only the first 'aSearchBound' timers (in firing order), returns
  // the timeout of the first non-low-priority timer, on the current thread,
  // that will fire before 'aDefault'. If no such timer exists, 'aDefault' is
  // returned.
  TimeStamp FindNextFireTimeForCurrentThread(TimeStamp aDefault,
                                             uint32_t aSearchBound);

  void DoBeforeSleep();
  void DoAfterSleep();

  bool IsOnTimerThread() const { return mThread->IsOnCurrentThread(); }

  uint32_t AllowedEarlyFiringMicroseconds();
  nsresult GetTimers(nsTArray<RefPtr<nsITimer>>& aRetVal);

 private:
  ~TimerThread();

  bool mInitialized;

  // These internal helper methods must be called while mMonitor is held.
  void AddTimerInternal(nsTimerImpl& aTimer) MOZ_REQUIRES(mMonitor);
  bool RemoveTimerInternal(nsTimerImpl& aTimer)
      MOZ_REQUIRES(mMonitor, aTimer.mMutex);
  void RemoveLeadingCanceledTimersInternal() MOZ_REQUIRES(mMonitor);
  nsresult Init() MOZ_REQUIRES(mMonitor);
  void AssertTimersSortedAndUnique() MOZ_REQUIRES(mMonitor);

  nsCOMPtr<nsIThread> mThread;
  // Lock ordering requirements:
  // (optional) ThreadWrapper::sMutex ->
  // (optional) nsTimerImpl::mMutex   ->
  // TimerThread::mMonitor
  TimerThreadMonitor mMonitor;

  bool mShutdown MOZ_GUARDED_BY(mMonitor);
  bool mWaiting MOZ_GUARDED_BY(mMonitor);
  bool mNotified MOZ_GUARDED_BY(mMonitor);
  bool mSleeping MOZ_GUARDED_BY(mMonitor);

  struct EntryKey {
    explicit EntryKey(nsTimerImpl& aTimerImpl)
        : mTimeout(aTimerImpl.mTimeout), mTimerSeq(aTimerImpl.mTimerSeq) {}

    // The comparison operators must ensure to detect equality only for
    // equal mTimerImpl except for canceled timers.
    // This is achieved through the sequence number.
    // Currently we maintain a FIFO order for timers with equal timeout.
    // Note that it might make sense to flip the sequence order to favor
    // timeouts with smaller delay as they are most likely more sensitive
    // to jitter. But we strictly test for FIFO order in our gtests.

    bool operator==(const EntryKey& aRhs) const {
      return (mTimeout == aRhs.mTimeout && mTimerSeq == aRhs.mTimerSeq);
    }

    bool operator<(const EntryKey& aRhs) const {
      if (mTimeout == aRhs.mTimeout) {
        return mTimerSeq < aRhs.mTimerSeq;
      }
      return mTimeout < aRhs.mTimeout;
    }

    TimeStamp mTimeout;
    uint64_t mTimerSeq;
  };

  struct Entry final : EntryKey {
    explicit Entry(nsTimerImpl& aTimerImpl)
        : EntryKey(aTimerImpl),
          mDelay(aTimerImpl.mDelay),
          mTimerImpl(&aTimerImpl) {}

    // No copies to not fiddle with mTimerImpl's ref-count.
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    Entry(Entry&&) = default;
    Entry& operator=(Entry&&) = default;

#ifdef DEBUG
    // While the timer is stored in the thread's list, the timeout is
    // immutable, so it should be OK to read without holding the mutex.
    // We only allow this in debug builds.
    bool IsTimerInThreadAndUnchanged() MOZ_NO_THREAD_SAFETY_ANALYSIS {
      return (mTimerImpl && mTimerImpl->IsInTimerThread() &&
              mTimerImpl->mTimeout == mTimeout);
    }
#endif

    TimeDuration mDelay;
    RefPtr<nsTimerImpl> mTimerImpl;
  };

  void PostTimerEvent(Entry& aPostMe) MOZ_REQUIRES(mMonitor);

  // WakeupTime encompasses both a point in time at which we should wake up and
  // tolerance for how much that wake-up can be delayed.
  struct WakeupTime {
    const TimeStamp mWakeupTime;
    const TimeDuration mDelayTolerance;
  };

  // Computes and returns when we should next try to wake up in order to handle
  // the triggering of the timers in mTimers.
  // If mTimers is empty, returns a null TimeStamp. If mTimers is not empty,
  // returns the timeout of the last timer that can be bundled with the first
  // timer in mTimers along with a tolerance indicating the most that we can be
  // delayed and not violate the tolerances of any of the timers in the bundle.
  WakeupTime ComputeWakeupTimeFromTimers() const MOZ_REQUIRES(mMonitor);

  // Computes how late a timer can acceptably fire.
  // timerDuration is the duration of the timer whose delay we are calculating.
  // Longer timers can tolerate longer firing delays.
  // minDelay is an amount by which any timer can be delayed.
  // This function will never return a value smaller than minDelay (unless this
  // conflicts with maxDelay). maxDelay is the upper limit on the amount by
  // which we will ever delay any timer. Takes precedence over minDelay if there
  // is a conflict. (Zero will effectively disable timer coalescing.)
  TimeDuration ComputeAcceptableFiringDelay(TimeDuration timerDuration,
                                            TimeDuration minDelay,
                                            TimeDuration maxDelay) const;

  // Fires and removes all timers in mTimers that are "due" to be fired,
  // according to the current time and the passed-in early firing tolerance.
  // Return value is the number of timers that were fired by the operation.
  uint64_t FireDueTimers(TimeDuration aAllowedEarlyFiring)
      MOZ_REQUIRES(mMonitor);

  // Suspends thread execution using mMonitor.Wait(waitFor). Also sets and
  // clears a few flags before and after.
  void Wait(TimeDuration aWaitFor, TimeDuration aTolerance)
      MOZ_REQUIRES(mMonitor);

  // mTimers is sorted by timeout, followed by a unique sequence number.
  // Some entries are for cancelled entries, but remain in sorted order based
  // on the timeout and sequence number they were originally created with.
  nsTArray<Entry> mTimers MOZ_GUARDED_BY(mMonitor);

  // Set only at the start of the thread's Run():
  uint32_t mAllowedEarlyFiringMicroseconds MOZ_GUARDED_BY(mMonitor);

  ProfilerThreadId mProfilerThreadId MOZ_GUARDED_BY(mMonitor);

  // Time at which we were intending to wake up the last time that we slept.
  // Is "null" if we have never slept or if our last sleep was "forever".
  TimeStamp mIntendedWakeupTime;

#if TIMER_THREAD_STATISTICS
  static constexpr size_t sTimersFiredPerWakeupBucketCount = 16;
  static inline constexpr std::array<size_t, sTimersFiredPerWakeupBucketCount>
      sTimersFiredPerWakeupThresholds = {
          0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 20, 30, 40, 50, 70, (size_t)(-1)};

  mutable AutoTArray<size_t, sTimersFiredPerWakeupBucketCount>
      mTimersFiredPerWakeup MOZ_GUARDED_BY(mMonitor) = {0, 0, 0, 0, 0, 0, 0, 0,
                                                        0, 0, 0, 0, 0, 0, 0, 0};
  mutable AutoTArray<size_t, sTimersFiredPerWakeupBucketCount>
      mTimersFiredPerUnnotifiedWakeup MOZ_GUARDED_BY(mMonitor) = {
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  mutable AutoTArray<size_t, sTimersFiredPerWakeupBucketCount>
      mTimersFiredPerNotifiedWakeup MOZ_GUARDED_BY(mMonitor) = {
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  mutable size_t mTotalTimersAdded MOZ_GUARDED_BY(mMonitor) = 0;
  mutable size_t mTotalTimersRemoved MOZ_GUARDED_BY(mMonitor) = 0;
  mutable size_t mTotalTimersFiredNotified MOZ_GUARDED_BY(mMonitor) = 0;
  mutable size_t mTotalTimersFiredUnnotified MOZ_GUARDED_BY(mMonitor) = 0;

  mutable size_t mTotalWakeupCount MOZ_GUARDED_BY(mMonitor) = 0;
  mutable size_t mTotalUnnotifiedWakeupCount MOZ_GUARDED_BY(mMonitor) = 0;
  mutable size_t mTotalNotifiedWakeupCount MOZ_GUARDED_BY(mMonitor) = 0;

  mutable double mTotalActualTimerFiringDelayNotified MOZ_GUARDED_BY(mMonitor) =
      0.0;
  mutable double mTotalActualTimerFiringDelayUnnotified
      MOZ_GUARDED_BY(mMonitor) = 0.0;

  mutable TimeStamp mFirstTimerAdded MOZ_GUARDED_BY(mMonitor);

  mutable size_t mEarlyWakeups MOZ_GUARDED_BY(mMonitor) = 0;
  mutable double mTotalEarlyWakeupTime MOZ_GUARDED_BY(mMonitor) = 0.0;

  void CollectTimersFiredStatistics(uint64_t timersFiredThisWakeup);

  void CollectWakeupStatistics();

  void PrintStatistics() const;
#endif
};
#endif /* TimerThread_h_ */
