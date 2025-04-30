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

// When a refcounted object is actually owned by a cache, we may not
// want to release the object after last reference gets released. In
// this pattern, the cache may rely on the balance of increment to one
// and decrement to zero, so that it can maintain a counter for GC.
TEST(AutoRefCnt, ThreadSafeAutoRefCntBalance)
{
  static const size_t kThreadCount = 4;
  nsCOMPtr<nsIThread> threads[kThreadCount];
  for (size_t i = 0; i < kThreadCount; i++) {
    nsresult rv =
        NS_NewNamedThread("AutoRefCnt Test", getter_AddRefs(threads[i]),
                          new nsThreadSafeAutoRefCntRunner);
    EXPECT_NS_SUCCEEDED(rv);
  }
  for (size_t i = 0; i < kThreadCount; i++) {
    threads[i]->Shutdown();
  }
  EXPECT_EQ(nsThreadSafeAutoRefCntRunner::sRefCnt, nsrefcnt(0));
  EXPECT_EQ(nsThreadSafeAutoRefCntRunner::sIncToOne,
            nsThreadSafeAutoRefCntRunner::sDecToZero);
}
