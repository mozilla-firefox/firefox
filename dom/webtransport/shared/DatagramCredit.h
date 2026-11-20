/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_SHARED_DATAGRAMCREDIT_H_
#define DOM_WEBTRANSPORT_SHARED_DATAGRAMCREDIT_H_

#include <algorithm>
#include <cstdint>

#include "mozilla/Maybe.h"
#include "mozilla/dom/DatagramQueueCharge.h"

// The arithmetic behind the DatagramCapacity message, kept free of IPC,
// promises and neqo so it can be unit tested.  The parent samples neqo's
// outgoing datagram queue and computes the message
// (OutgoingDatagramCreditGranter); the content process raises its credit from
// it and retires datagrams from its in-flight count (OutgoingDatagramCredit,
// OutgoingDatagramInFlight).

namespace mozilla::dom {

struct DatagramQueueSample {
  uint64_t mRemainingBytes = 0;
  uint64_t mQueuedDatagrams = 0;
  uint64_t mMaxQueuedBytes = 0;
  // Cumulative counters from neqo.
  uint64_t mSent = 0;
  uint64_t mExpired = 0;
  uint64_t mDropped = 0;
};

struct DatagramCapacityUpdate {
  uint64_t mCumulativeDatagrams = 0;
  uint64_t mCumulativeBytes = 0;
  uint64_t mSentDelta = 0;
  uint64_t mExpiredDelta = 0;
  uint64_t mDroppedDelta = 0;

  // What the update was computed from, for Commit().
  DatagramQueueSample mSample;
  uint64_t mParentExpired = 0;
};

// Parent side.
class OutgoingDatagramCreditGranter {
 public:
  void SetMaxBufferedDatagrams(uint32_t aMax) { mMaxBufferedDatagrams = aMax; }
  void SetMaxDatagramSize(uint64_t aSize) { mMaxDatagramSize = aSize; }
  void SetDatagramPrefixBytes(uint64_t aBytes) {
    mDatagramPrefixBytes = aBytes;
  }
  uint64_t DatagramPrefixBytes() const { return mDatagramPrefixBytes; }

  // A datagram handed to neqo's outgoing queue.
  void OnForwarded(uint64_t aBytes) { Retire(aBytes); }

  // A datagram dropped before reaching neqo for being too old on arrival.  It
  // leaves the queue like a forwarded one, and neqo's own expired counter
  // cannot see it.
  void OnExpiredBeforeForwarding(uint64_t aBytes) {
    mParentExpired++;
    Retire(aBytes);
  }

  uint64_t ParentExpired() const { return mParentExpired; }

  // Returns the message to send, if any.  Call Commit() once it has been sent.
  Maybe<DatagramCapacityUpdate> Compute(
      const DatagramQueueSample& aSample) const {
    DatagramCapacityUpdate u;
    u.mSample = aSample;
    u.mParentExpired = mParentExpired;

    // Everything handed to neqo so far plus the room currently free in its
    // queue: a MAX_DATA-style cumulative grant.  Less one maximum-sized
    // datagram, for the same reason as the count below: a write whose promise
    // is withheld has already sent its datagram without being charged for it.
    uint64_t withheld =
        DatagramQueueCharge(mMaxDatagramSize, mDatagramPrefixBytes);
    u.mCumulativeBytes =
        mBytesRetired + (aSample.mRemainingBytes > withheld
                             ? aSample.mRemainingBytes - withheld
                             : 0);
    // Clamp rather than trust the queued count across the FFI boundary.
    uint64_t departed = mDatagramsRetired -
                        std::min(aSample.mQueuedDatagrams, mDatagramsRetired);
    // One less than the buffer size, to match the rule that makes a write()
    // resolve immediately: fewer than the maximum queued counting itself.  A
    // withheld write is released under the same condition once a departure
    // makes room.
    u.mCumulativeDatagrams =
        departed + (mMaxBufferedDatagrams > 0 ? mMaxBufferedDatagrams - 1 : 0);

    u.mSentDelta = aSample.mSent - mLastSample.mSent;
    u.mExpiredDelta = (aSample.mExpired - mLastSample.mExpired) +
                      (mParentExpired - mLastParentExpired);
    u.mDroppedDelta = aSample.mDropped - mLastSample.mDropped;

    // Only ever send an increasing grant (the child ignores a non-increase
    // anyway), and dedupe like neqo's ReceiverFlowControl::should_send_update:
    // once consumption has crossed roughly half of the window since the last
    // message, or there is a delta waiting to be reported.
    bool bytesAdvancedEnough =
        u.mCumulativeBytes > mLastGrantedBytes && aSample.mMaxQueuedBytes > 0 &&
        u.mCumulativeBytes - mLastGrantedBytes >= aSample.mMaxQueuedBytes / 2;
    bool datagramsAdvancedEnough =
        u.mCumulativeDatagrams > mLastGrantedDatagrams &&
        mMaxBufferedDatagrams > 0 &&
        u.mCumulativeDatagrams - mLastGrantedDatagrams >=
            mMaxBufferedDatagrams / 2;
    bool neverGranted = mLastGrantedBytes == 0 && mLastGrantedDatagrams == 0;
    bool hasDeltaToReport =
        u.mSentDelta > 0 || u.mExpiredDelta > 0 || u.mDroppedDelta > 0;

    if (!bytesAdvancedEnough && !datagramsAdvancedEnough && !neverGranted &&
        !hasDeltaToReport) {
      return Nothing();
    }
    return Some(u);
  }

  // Whether the last grant leaves the child too little to send another
  // maximum-sized datagram.
  bool CreditExhausted() const {
    return mLastGrantedDatagrams <= mDatagramsRetired ||
           mLastGrantedBytes <
               mBytesRetired +
                   DatagramQueueCharge(mMaxDatagramSize, mDatagramPrefixBytes);
  }

  void Commit(const DatagramCapacityUpdate& aUpdate) {
    mLastGrantedDatagrams = aUpdate.mCumulativeDatagrams;
    mLastGrantedBytes = aUpdate.mCumulativeBytes;
    mLastSample = aUpdate.mSample;
    mLastParentExpired = aUpdate.mParentExpired;
  }

 private:
  void Retire(uint64_t aBytes) {
    mDatagramsRetired++;
    mBytesRetired += DatagramQueueCharge(aBytes, mDatagramPrefixBytes);
  }

  uint32_t mMaxBufferedDatagrams = 5;
  uint64_t mMaxDatagramSize = 0;
  uint64_t mDatagramPrefixBytes = 0;
  // Cumulative datagrams/charged bytes that have left
  // [[OutgoingDatagramsQueue]], whether handed to neqo or dropped before it.
  uint64_t mDatagramsRetired = 0;
  uint64_t mBytesRetired = 0;
  uint64_t mParentExpired = 0;
  uint64_t mLastGrantedDatagrams = 0;
  uint64_t mLastGrantedBytes = 0;
  DatagramQueueSample mLastSample;
  uint64_t mLastParentExpired = 0;
};

// Content side, shared by every writable of a session: the latest grant and
// how much of it write() promises have used, for each dimension.  Mirrors
// neqo-transport's SenderFlowControl.
class OutgoingDatagramCredit {
 public:
  // A decrease is ignored, so a grant that arrives late is harmless.
  void Raise(uint64_t aDatagrams, uint64_t aBytes) {
    mDatagramsLimit = std::max(mDatagramsLimit, aDatagrams);
    mBytesLimit = std::max(mBytesLimit, aBytes);
  }

  // From DatagramCapacity.  Writes charged before the first grant arrived
  // (pre-connect ones) were charged without the prefix, so make up the
  // difference for them.
  void SetDatagramPrefixBytes(uint64_t aBytes) {
    if (aBytes > mDatagramPrefixBytes) {
      mBytesUsed += (aBytes - mDatagramPrefixBytes) * mDatagramsUsed;
      mDatagramPrefixBytes = aBytes;
    }
  }
  uint64_t DatagramPrefixBytes() const { return mDatagramPrefixBytes; }

  // Charged when a write() promise resolves, immediately or not, which can be
  // before the parent has been told about the datagram.
  void Charge(uint64_t aPayloadBytes) {
    mDatagramsUsed++;
    mBytesUsed += DatagramQueueCharge(aPayloadBytes, mDatagramPrefixBytes);
  }

  bool HasDatagramRoom() const { return mDatagramsUsed < mDatagramsLimit; }
  bool HasBytesFor(uint64_t aPayloadBytes) const {
    return DatagramQueueCharge(aPayloadBytes, mDatagramPrefixBytes) <=
           BytesAvailable();
  }
  uint64_t BytesAvailable() const {
    return mBytesLimit > mBytesUsed ? mBytesLimit - mBytesUsed : 0;
  }

  uint64_t DatagramsUsed() const { return mDatagramsUsed; }
  uint64_t DatagramsLimit() const { return mDatagramsLimit; }

 private:
  uint64_t mDatagramsLimit = 0;
  uint64_t mDatagramsUsed = 0;
  uint64_t mBytesLimit = 0;
  uint64_t mBytesUsed = 0;
  uint64_t mDatagramPrefixBytes = 0;
};

// Content side, per writable: datagrams handed to the parent that have not
// been reported as having left neqo's queue.  Stands in for the length of the
// spec's [[OutgoingDatagramsQueue]], which lives in the parent.
class OutgoingDatagramInFlight {
 public:
  // Counts a datagram handed to the parent.  Returns whether its write()
  // promise resolves immediately: fewer than aMaxBuffered in flight, counting
  // itself.
  bool Add(uint32_t aMaxBuffered) {
    mCount++;
    return mCount < aMaxBuffered;
  }

  // Datagrams queued before connecting, handed over at once on connect.
  void AddQueued(size_t aCount) { mCount += aCount; }

  void Retire(uint64_t aCount) { mCount -= std::min<uint64_t>(aCount, mCount); }

  size_t Count() const { return mCount; }

 private:
  size_t mCount = 0;
};

}  // namespace mozilla::dom

#endif  // DOM_WEBTRANSPORT_SHARED_DATAGRAMCREDIT_H_
