/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <cstdint>
#include <deque>
#include <vector>

#include "gtest/gtest.h"
#include "mozilla/dom/DatagramCredit.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace {

// Drives the parent's credit granter and the content process's credit and
// in-flight count against a simulated neqo queue, which charges and evicts
// like neqo's, the way the two sides do:
// OutgoingDatagramStreamAlgorithms::WriteCallbackImpl(), the batch handed to
// WebTransportParent::RecvOutgoingDatagrams(), and
// WebTransportDatagramDuplexStream::OnDatagramCapacity().  Writes are made one
// at a time, each after the previous promise has resolved, as a
// WritableStream does.
class Sim {
 public:
  // What DatagramPrefixBytes() gives for the usual first session.
  static constexpr uint64_t kPrefixBytes = 1;

  explicit Sim(uint32_t aMaxBuffered, uint64_t aMaxQueuedBytes = 1 << 20,
               uint64_t aMaxDatagramSize = 1200)
      : mMax(aMaxBuffered), mMaxQueuedBytes(aMaxQueuedBytes) {
    mParent.SetMaxBufferedDatagrams(aMaxBuffered);
    mParent.SetMaxDatagramSize(aMaxDatagramSize);
    mParent.SetDatagramPrefixBytes(kPrefixBytes);
    // The parent grants once the session is ready, before any write.
    Sample();
  }

  struct Write {
    uint64_t mBytes;
    bool mResolved;
    bool mHanded;
  };

  size_t DoWrite(uint64_t aBytes) {
    bool immediate = mInFlight.Add(mMax) && mCredit.HasBytesFor(aBytes);
    if (immediate) {
      mCredit.Charge(aBytes);
    } else {
      mPending.push_back(mWrites.size());
    }
    mWrites.push_back({aBytes, immediate, false});
    return mWrites.size() - 1;
  }

  // Hands every write not yet handed over to the parent.  With aTooOld the
  // parent drops them for age instead of forwarding them to neqo.
  void Flush(bool aTooOld = false) {
    for (Write& w : mWrites) {
      if (w.mHanded) {
        continue;
      }
      w.mHanded = true;
      if (aTooOld) {
        mParent.OnExpiredBeforeForwarding(w.mBytes);
        mParentDeparted++;
        continue;
      }
      mParent.OnForwarded(w.mBytes);
      mQueueBytes += DatagramQueueCharge(w.mBytes, kPrefixBytes);
      mQueue.push_back(w.mBytes);
      while (mQueueBytes > mMaxQueuedBytes && mQueue.size() > 1) {
        Remove();
        mDropped++;
      }
    }
  }

  void DepartSent(size_t aCount = 1) {
    for (; aCount && !mQueue.empty(); aCount--) {
      Remove();
      mSent++;
    }
  }

  void DepartExpired(size_t aCount = 1) {
    for (; aCount && !mQueue.empty(); aCount--) {
      Remove();
      mExpired++;
    }
  }

  // The parent samples neqo and, if it has something to say, the content
  // process applies the message.  Returns whether one was sent.
  bool Sample() {
    DatagramQueueSample sample;
    sample.mRemainingBytes =
        mMaxQueuedBytes > mQueueBytes ? mMaxQueuedBytes - mQueueBytes : 0;
    sample.mQueuedDatagrams = mQueue.size();
    sample.mMaxQueuedBytes = mMaxQueuedBytes;
    sample.mSent = mSent;
    sample.mExpired = mExpired;
    sample.mDropped = mDropped;
    Maybe<DatagramCapacityUpdate> update = mParent.Compute(sample);
    if (!update) {
      return false;
    }
    mParent.Commit(*update);
    OnCapacity(*update);
    return true;
  }

  uint64_t Dropped() const { return mDropped; }

  size_t Departed() const {
    return mSent + mExpired + mDropped + mParentDeparted;
  }

  size_t Resolved() const {
    size_t n = 0;
    for (const Write& w : mWrites) {
      n += w.mResolved;
    }
    return n;
  }

  bool IsResolved(size_t aIndex) const { return mWrites[aIndex].mResolved; }

  OutgoingDatagramCreditGranter mParent;
  OutgoingDatagramCredit mCredit;
  OutgoingDatagramInFlight mInFlight;
  uint64_t mExpiredReported = 0;

 private:
  void Remove() {
    mQueueBytes -= DatagramQueueCharge(mQueue.front(), kPrefixBytes);
    mQueue.pop_front();
  }

  void OnCapacity(const DatagramCapacityUpdate& aUpdate) {
    mExpiredReported += aUpdate.mExpiredDelta;
    mInFlight.Retire(aUpdate.mSentDelta + aUpdate.mExpiredDelta +
                     aUpdate.mDroppedDelta);
    mCredit.SetDatagramPrefixBytes(mParent.DatagramPrefixBytes());
    mCredit.Raise(aUpdate.mCumulativeDatagrams, aUpdate.mCumulativeBytes);
    while (!mPending.empty() && mCredit.HasDatagramRoom() &&
           mCredit.HasBytesFor(mWrites[mPending.front()].mBytes)) {
      Write& w = mWrites[mPending.front()];
      mPending.pop_front();
      mCredit.Charge(w.mBytes);
      w.mResolved = true;
    }
  }

  uint32_t mMax;
  uint64_t mMaxQueuedBytes;
  std::vector<Write> mWrites;
  std::deque<size_t> mPending;
  std::deque<uint64_t> mQueue;
  uint64_t mQueueBytes = 0;
  uint64_t mSent = 0;
  uint64_t mExpired = 0;
  uint64_t mDropped = 0;
  uint64_t mParentDeparted = 0;
};

}  // namespace

// A write that resolves immediately takes one of the slots, so the one that
// fills the queue stays withheld until a datagram leaves it.
TEST(DatagramCredit, WithheldWriteWaitsForRoom)
{
  Sim sim(2);
  size_t a = sim.DoWrite(100);
  size_t b = sim.DoWrite(100);
  EXPECT_TRUE(sim.IsResolved(a));
  EXPECT_FALSE(sim.IsResolved(b));

  sim.Flush();
  sim.Sample();
  EXPECT_FALSE(sim.IsResolved(b));

  sim.DepartSent();
  EXPECT_TRUE(sim.Sample());
  EXPECT_TRUE(sim.IsResolved(b));
}

// Datagrams the parent drops for age never reach neqo, but content must still
// stop counting them as in flight.
TEST(DatagramCredit, ParentAgeDropsRetireInFlight)
{
  Sim sim(5);
  sim.DoWrite(100);
  sim.DoWrite(100);
  EXPECT_EQ(sim.mInFlight.Count(), 2u);

  sim.Flush(/* aTooOld = */ true);
  EXPECT_TRUE(sim.Sample());
  EXPECT_EQ(sim.mInFlight.Count(), 0u);
  EXPECT_EQ(sim.mExpiredReported, 2u);
  EXPECT_EQ(sim.mParent.ParentExpired(), 2u);
}

TEST(DatagramCredit, MaxBufferedOneWaitsForEveryDeparture)
{
  Sim sim(1);
  for (int i = 0; i < 3; i++) {
    size_t w = sim.DoWrite(100);
    EXPECT_FALSE(sim.IsResolved(w));
    sim.Flush();
    sim.Sample();
    EXPECT_FALSE(sim.IsResolved(w));
    sim.DepartSent();
    EXPECT_TRUE(sim.Sample());
    EXPECT_TRUE(sim.IsResolved(w));
  }
}

TEST(DatagramCredit, MaxBufferedZeroDoesNotUnderflow)
{
  Sim sim(0);
  size_t w = sim.DoWrite(100);
  EXPECT_FALSE(sim.IsResolved(w));
  sim.Flush();
  sim.Sample();
  EXPECT_FALSE(sim.IsResolved(w));
  sim.DepartSent();
  sim.Sample();
  EXPECT_TRUE(sim.IsResolved(w));
}

TEST(DatagramCredit, CreditNeverUnderflowsOrShrinks)
{
  OutgoingDatagramCredit credit;
  credit.Charge(60);
  credit.Charge(60);
  EXPECT_EQ(credit.BytesAvailable(), 0u);
  EXPECT_FALSE(credit.HasDatagramRoom());

  credit.Raise(10, 100);
  EXPECT_EQ(credit.BytesAvailable(), 0u);
  EXPECT_TRUE(credit.HasDatagramRoom());

  credit.Raise(5, 50);
  EXPECT_EQ(credit.DatagramsLimit(), 10u);
  credit.Raise(10, 2 * DatagramQueueCharge(60, 0) + 80);
  EXPECT_EQ(credit.BytesAvailable(), 80u);
}

// Charges count neqo's per-datagram overhead, so a payload that fits the
// credit byte for byte doesn't fit what it costs.
TEST(DatagramCredit, ChargeIncludesQueueOverhead)
{
  OutgoingDatagramCredit credit;
  credit.Raise(10, 1000);
  EXPECT_FALSE(credit.HasBytesFor(1000));
  EXPECT_TRUE(credit.HasBytesFor(1000 - kDatagramQueueOverhead));
  credit.Charge(100);
  EXPECT_EQ(credit.BytesAvailable(), 1000 - DatagramQueueCharge(100, 0));
}

// Writes charged before the prefix is known are charged it once it arrives.
TEST(DatagramCredit, PrefixIsChargedRetroactively)
{
  OutgoingDatagramCredit credit;
  credit.Raise(10, 1000);
  credit.Charge(100);
  credit.Charge(100);
  credit.SetDatagramPrefixBytes(2);
  EXPECT_EQ(credit.BytesAvailable(), 1000 - 2 * DatagramQueueCharge(100, 2));
  credit.Charge(100);
  EXPECT_EQ(credit.BytesAvailable(), 1000 - 3 * DatagramQueueCharge(100, 2));
}

// A whole grant's worth of small datagrams reaching neqo's queue before any of
// them is sent must fit, prefix included.
TEST(DatagramCredit, BurstBeforeAnySendFitsTheByteBudget)
{
  Sim sim(8000, 256 * 1024, 1221);
  size_t w;
  do {
    w = sim.DoWrite(15);
  } while (sim.IsResolved(w));
  sim.Flush();
  EXPECT_EQ(sim.Dropped(), 0u);
}

// An outgoingMaxBufferedDatagrams far above what neqo's byte budget holds must
// be held back by the byte credit instead of overflowing the queue, which
// would evict datagrams.
TEST(DatagramCredit, LargeMaxBufferedDoesNotOverflowTheByteBudget)
{
  constexpr uint64_t kBudget = 256 * 1024;
  constexpr uint64_t kSize = 1199;
  Sim sim(1000, kBudget, kSize);
  for (int i = 0; i < 5000; i++) {
    size_t w = sim.DoWrite(kSize);
    sim.Flush();
    if (i % 3 == 0) {
      sim.Sample();
    }
    while (!sim.IsResolved(w)) {
      sim.DepartSent(1 + i % 20);
      sim.Sample();
    }
  }
  EXPECT_EQ(sim.Dropped(), 0u);
}

TEST(DatagramCredit, GranterSendsOnlyWhenThereIsSomethingToSay)
{
  OutgoingDatagramCreditGranter granter;
  granter.SetMaxBufferedDatagrams(4);
  DatagramQueueSample sample;
  sample.mRemainingBytes = 1000;
  sample.mMaxQueuedBytes = 1000;

  Maybe<DatagramCapacityUpdate> first = granter.Compute(sample);
  ASSERT_TRUE(first.isSome());
  // Not sent yet, so it is still due.
  EXPECT_TRUE(granter.Compute(sample).isSome());

  granter.Commit(*first);
  EXPECT_TRUE(granter.Compute(sample).isNothing());

  sample.mSent = 1;
  Maybe<DatagramCapacityUpdate> second = granter.Compute(sample);
  ASSERT_TRUE(second.isSome());
  EXPECT_EQ(second->mSentDelta, 1u);
  granter.Commit(*second);
  EXPECT_TRUE(granter.Compute(sample).isNothing());
}

TEST(DatagramCredit, GranterCountsParentExpiryInExpiredDelta)
{
  OutgoingDatagramCreditGranter granter;
  granter.SetMaxBufferedDatagrams(4);
  DatagramQueueSample sample;
  sample.mRemainingBytes = 1000;
  sample.mMaxQueuedBytes = 1000;
  granter.Commit(*granter.Compute(sample));

  granter.OnExpiredBeforeForwarding(100);
  Maybe<DatagramCapacityUpdate> update = granter.Compute(sample);
  ASSERT_TRUE(update.isSome());
  EXPECT_EQ(update->mExpiredDelta, 1u);
  // It left the queue without ever being in neqo's, so it frees a slot.
  EXPECT_EQ(update->mCumulativeDatagrams, 1u + 3u);
}

// Whatever the order of writes, flushes, departures and samples, the number of
// resolved writes never gets ahead of what has left the queue plus the buffer
// size less one, and once everything has left, every write is resolved.
TEST(DatagramCredit, RandomizedSequencesKeepTheWindowAndMakeProgress)
{
  uint32_t seed = 12345;
  auto next = [&seed](uint32_t aBound) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % aBound;
  };

  for (int run = 0; run < 300; run++) {
    uint32_t max = 1 + next(6);
    Sim sim(max, 800 + next(2000), 300);
    size_t lastWrite = 0;
    bool haveWrite = false;
    for (int step = 0; step < 60; step++) {
      switch (next(6)) {
        case 0:
        case 1:
          if (!haveWrite || sim.IsResolved(lastWrite)) {
            lastWrite = sim.DoWrite(50 + next(250));
            haveWrite = true;
          }
          break;
        case 2:
          sim.Flush(next(4) == 0);
          break;
        case 3:
          sim.DepartSent(1 + next(2));
          break;
        case 4:
          sim.DepartExpired();
          break;
        default:
          sim.Sample();
          break;
      }
      ASSERT_LE(sim.Resolved(), sim.Departed() + max - 1)
          << "run " << run << " step " << step;
      ASSERT_EQ(sim.Dropped(), 0u) << "run " << run << " step " << step;
    }

    for (int i = 0; i < 4; i++) {
      sim.Flush();
      sim.DepartSent(1000);
      sim.Sample();
    }
    EXPECT_TRUE(!haveWrite || sim.IsResolved(lastWrite)) << "run " << run;
  }
}
