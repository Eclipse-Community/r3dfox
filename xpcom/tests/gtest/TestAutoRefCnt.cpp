/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsISupportsImpl.h"

#include "mozilla/Atomics.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"

#include "gtest/gtest.h"
#include "mozilla/gtest/MozAssertions.h"

using namespace mozilla;

class nsThreadSafeAutoRefCntRunner final : public Runnable {
 public:
  NS_IMETHOD Run() final {
    for (int i = 0; i < 10000; i++) {
      if (++sRefCnt == 1) {
        sIncToOne++;
      }
      if (--sRefCnt == 0) {
        sDecToZero++;
      }
    }
    return NS_OK;
  }

  static ThreadSafeAutoRefCnt sRefCnt;
  static Atomic<uint32_t, Relaxed> sIncToOne;
  static Atomic<uint32_t, Relaxed> sDecToZero;

  nsThreadSafeAutoRefCntRunner() : Runnable("nsThreadSafeAutoRefCntRunner") {}

 private:
  ~nsThreadSafeAutoRefCntRunner() = default;
};

ThreadSafeAutoRefCnt nsThreadSafeAutoRefCntRunner::sRefCnt;
Atomic<uint32_t, Relaxed> nsThreadSafeAutoRefCntRunner::sIncToOne(0);
Atomic<uint32_t, Relaxed> nsThreadSafeAutoRefCntRunner::sDecToZero(0);

class nsThreadSafeAutoRefCntDecrementWithLimitRunner final : public Runnable {
 public:
  static constexpr size_t kDecrementsPerThread = 1000;

  NS_IMETHOD Run() final {
    for (size_t i = 0; i < kDecrementsPerThread; i++) {
      auto [ok, count] = sRefCnt.DecrementWithLimit<1>();
      if (!ok) {
        sLimitHits++;
        break;
      }
    }
    return NS_OK;
  }

  static ThreadSafeAutoRefCnt sRefCnt;
  // Relaxed ordering so sLimitHits doesn't affect the ordering properties of
  // DecrementWithLimit being tested.
  static Atomic<uint32_t, Relaxed> sLimitHits;

  nsThreadSafeAutoRefCntDecrementWithLimitRunner()
      : Runnable("nsThreadSafeAutoRefCntDecrementWithLimitRunner") {}

 private:
  ~nsThreadSafeAutoRefCntDecrementWithLimitRunner() = default;
};

ThreadSafeAutoRefCnt nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt;
Atomic<uint32_t, Relaxed>
    nsThreadSafeAutoRefCntDecrementWithLimitRunner::sLimitHits(0);

// When a refcounted object is actually owned by a cache, we may not
// want to release the object after last reference gets released. In
// this pattern, the cache may rely on the balance of increment to one
// and decrement to zero, so that it can maintain a counter for GC.
TEST(AutoRefCnt, ThreadSafeAutoRefCntBalance)
{
  static const size_t kThreadCount = 4;
  nsCOMPtr<nsIThread> threads[kThreadCount];
  for (auto& thread : threads) {
    nsresult rv = NS_NewNamedThread("AutoRefCnt Test", getter_AddRefs(thread),
                                    new nsThreadSafeAutoRefCntRunner);
    EXPECT_NS_SUCCEEDED(rv);
  }
  for (const auto& thread : threads) {
    thread->Shutdown();
  }
  EXPECT_EQ(nsThreadSafeAutoRefCntRunner::sRefCnt, nsrefcnt(0));
  EXPECT_EQ(nsThreadSafeAutoRefCntRunner::sIncToOne,
            nsThreadSafeAutoRefCntRunner::sDecToZero);
}

// Spawns kThreadCount threads each attempting kDecrementsPerThread
// decrements, then verifies the final refcount is 1 and that exactly
// aExpectedLimitHits threads hit the limit.
// kInitial is derived from aExpectedLimitHits so that exactly
// (kThreadCount - aExpectedLimitHits) threads can complete all their
// decrements; the rest hit the limit.
static void RunDecrementWithLimitThreaded(uint32_t aExpectedLimitHits) {
  static const size_t kThreadCount = 4;
  const nsrefcnt kInitial =
      (kThreadCount - aExpectedLimitHits) *
          nsThreadSafeAutoRefCntDecrementWithLimitRunner::kDecrementsPerThread +
      1;
  nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt = kInitial;
  nsThreadSafeAutoRefCntDecrementWithLimitRunner::sLimitHits = 0;

  nsCOMPtr<nsIThread> threads[kThreadCount];
  for (auto& thread : threads) {
    nsresult rv =
        NS_NewNamedThread("AutoRefCnt Test", getter_AddRefs(thread),
                          new nsThreadSafeAutoRefCntDecrementWithLimitRunner);
    EXPECT_NS_SUCCEEDED(rv);
  }
  for (const auto& thread : threads) {
    thread->Shutdown();
  }
  EXPECT_EQ(nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt,
            nsrefcnt(1));
  EXPECT_EQ(nsThreadSafeAutoRefCntDecrementWithLimitRunner::sLimitHits,
            uint32_t(aExpectedLimitHits));
}

// Verify DecrementWithLimit with no lost updates when the limit is never hit.
TEST(AutoRefCnt, ThreadSafeAutoRefCntDecrementWithLimitNoHit)
{
  RunDecrementWithLimitThreaded(0);
}

// Verify DecrementWithLimit correctly enforces the limit. With kInitial == 1
// (equal to Limit), all threads hit the limit on their first attempt,
// giving a deterministic sLimitHits == kThreadCount.
TEST(AutoRefCnt, ThreadSafeAutoRefCntDecrementWithLimitHit)
{
  static const size_t kThreadCount = 4;
  RunDecrementWithLimitThreaded(kThreadCount);
}

// Verify DecrementWithLimit performs exactly kDecrementsPerThread successful
// decrements before hitting the limit. Single-threaded for determinism.
TEST(AutoRefCnt, ThreadSafeAutoRefCntDecrementWithLimitSequential)
{
  const nsrefcnt kDecrementsPerThread =
      nsThreadSafeAutoRefCntDecrementWithLimitRunner::kDecrementsPerThread;
  // Initialize so exactly kDecrementsPerThread decrements succeed before
  // hitting the limit at 1.
  nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt =
      kDecrementsPerThread + 1;

  size_t successes = 0;
  while (true) {
    auto [ok, count] = nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt
                           .DecrementWithLimit<1>();
    if (!ok) {
      break;
    }
    successes++;
  }

  EXPECT_EQ(successes, size_t(kDecrementsPerThread));
  EXPECT_EQ(nsThreadSafeAutoRefCntDecrementWithLimitRunner::sRefCnt,
            nsrefcnt(1));
}
