/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SystemTimeConverter_h
#define SystemTimeConverter_h

#include <cinttypes>
#include <limits>
#include <type_traits>

#include "mozilla/RWLock.h"
#include "mozilla/ThreadSafety.h"
#include "mozilla/TimeStamp.h"

namespace mozilla {

// Utility class that converts time values represented as an unsigned integral
// number of milliseconds from one time source (e.g. a native event time) to
// corresponding mozilla::TimeStamp objects.
//
// This class handles wrapping of integer values and skew between the time
// source and mozilla::TimeStamp values.
//
// It does this by using an historical reference time recorded in both time
// scales (i.e. both as a numerical time value and as a TimeStamp).
//
// For performance reasons, this class is careful to minimize calls to the
// native "current time" function (e.g. gdk_x11_server_get_time) since this can
// be slow.
template <typename Time, typename TimeStampNowProvider = TimeStamp>
class SystemTimeConverter {
 public:
  SystemTimeConverter()
      : mReferenceTime(Time(0)),
        mLastBackwardsSkewCheck(Time(0)),
        mReferenceTimeLock("SystemTimeConverter::mReferenceTimeLock"),
        kTimeRange(std::numeric_limits<Time>::max()),
        kTimeHalfRange(kTimeRange / 2),
        kBackwardsSkewCheckInterval(Time(2000)) {
    static_assert(!std::is_signed_v<Time>, "Expected Time to be unsigned");
  }

  template <typename CurrentTimeGetter>
  mozilla::TimeStamp GetTimeStampFromSystemTime(
      Time aTime, CurrentTimeGetter& aCurrentTimeGetter) {
    TimeStamp roughlyNow = TimeStampNowProvider::Now();

    // If the reference time is not set, use the current time value to fill
    // it in.
    bool referenceTimeStampIsNull;
    {
      // This is awkward. We need a read lock to check mReferenceTimeStamp,
      // but mReferenceTimeLock isn't upgradable, and per the documentation it's
      // not safe to hold a read lock while acquiring a write lock on this
      // thread.
      //
      // On the other hand, if two threads get here at the same time it's OK if
      // they both end up calling UpdateReferenceTime(); the values will be
      // coherent.
      AutoReadLock lock(mReferenceTimeLock);
      referenceTimeStampIsNull = mReferenceTimeStamp.IsNull();
    }
    if (referenceTimeStampIsNull) {
      // This sometimes happens when ::GetMessageTime returns 0 for the first
      // message on Windows.
      if (!aTime) return roughlyNow;
      UpdateReferenceTime(aTime, aCurrentTimeGetter);
    }

    // Check for skew between the source of Time values and TimeStamp values.
    // We do this by comparing two durations (both in ms):
    //
    // i.  The duration from the reference time to the passed-in time.
    //     (timeDelta in the diagram below)
    // ii. The duration from the reference timestamp to the current time
    //     based on TimeStamp::Now.
    //     (timeStampDelta in the diagram below)
    //
    // Normally, we'd expect (ii) to be slightly larger than (i) to account
    // for the time taken between generating the event and processing it.
    //
    // If (ii) - (i) is negative then the source of Time values is getting
    // "ahead" of TimeStamp. We call this "forwards" skew below.
    //
    // For the reverse case, if (ii) - (i) is positive (and greater than some
    // tolerance factor), then we may have "backwards" skew. This is often
    // the case when we have a backlog of events and by the time we process
    // them, the time given by the system is comparatively "old".
    //
    // The IsNewerThanTimestamp function computes the equivalent of |aTime| in
    // the TimeStamp scale and returns that in |timeAsTimeStamp|.
    //
    // Graphically:
    //
    //                    mReferenceTime              aTime
    // Time scale:      ........+.......................*........
    //                          |--------timeDelta------|
    //
    //                  mReferenceTimeStamp             roughlyNow
    // TimeStamp scale: ........+...........................*....
    //                          |------timeStampDelta-------|
    //
    //                                                  |---|
    //                                         roughlyNow-timeAsTimeStamp
    //
    TimeStamp timeAsTimeStamp;
    bool newer = IsTimeNewerThanTimestamp(aTime, roughlyNow, &timeAsTimeStamp);

    // Tolerance when detecting clock skew.
    static const TimeDuration kTolerance = TimeDuration::FromMilliseconds(30);

    // Check for forwards skew
    if (newer) {
      // Make aTime correspond to roughlyNow
      UpdateReferenceTime(aTime, roughlyNow);

      // We didn't have backwards skew so don't bother checking for
      // backwards skew again for a little while.
      mLastBackwardsSkewCheck = aTime;

      return roughlyNow;
    }

    if (roughlyNow - timeAsTimeStamp <= kTolerance) {
      // If the time between event times and TimeStamp values is within
      // the tolerance then assume we don't have clock skew so we can
      // avoid checking for backwards skew for a while.
      mLastBackwardsSkewCheck = aTime;
    } else if (aTime - mLastBackwardsSkewCheck > kBackwardsSkewCheckInterval) {
      aCurrentTimeGetter.GetTimeAsyncForPossibleBackwardsSkew(roughlyNow);
      mLastBackwardsSkewCheck = aTime;
    }

    // Finally, calculate the timestamp
    return timeAsTimeStamp;
  }

  void CompensateForBackwardsSkew(Time aReferenceTime,
                                  const TimeStamp& aLowerBound) {
    // Check if we actually have backwards skew. Backwards skew looks like
    // the following:
    //
    //        mReferenceTime
    // Time:      ..+...a...b...c..........................
    //
    //     mReferenceTimeStamp
    // TimeStamp: ..+.....a.....b.....c....................
    //
    // Converted
    // time:      ......a'..b'..c'.........................
    //
    // What we need to do is bring mReferenceTime "forwards".
    //
    // Suppose when we get (c), we detect possible backwards skew and trigger
    // an async request for the current time (which is passed in here as
    // aReferenceTime).
    //
    // We end up with something like the following:
    //
    //        mReferenceTime     aReferenceTime
    // Time:      ..+...a...b...c...v......................
    //
    //     mReferenceTimeStamp
    // TimeStamp: ..+.....a.....b.....c..........x.........
    //                                ^          ^
    //                          aLowerBound  TimeStamp::Now()
    //
    // If the duration (aLowerBound - mReferenceTimeStamp) is greater than
    // (aReferenceTime - mReferenceTime) then we know we have backwards skew.
    //
    // If that's not the case, then we probably just got caught behind
    // temporarily.
    if (IsTimeNewerThanTimestamp(aReferenceTime, aLowerBound, nullptr)) {
      return;
    }

    // We have backwards skew; the equivalent TimeStamp for aReferenceTime lies
    // somewhere between aLowerBound (which was the TimeStamp when we triggered
    // the async request for the current time) and TimeStamp::Now().
    //
    // If aReferenceTime was waiting in the event queue for a long time, the
    // equivalent TimeStamp might be much closer to aLowerBound than
    // TimeStamp::Now() so for now we just set it to aLowerBound. That's
    // guaranteed to be at least somewhat of an improvement.
    UpdateReferenceTime(aReferenceTime, aLowerBound);
  }

 private:
  template <typename CurrentTimeGetter>
  void UpdateReferenceTime(Time aReferenceTime,
                           const CurrentTimeGetter& aCurrentTimeGetter) {
    Time currentTime = aCurrentTimeGetter.GetCurrentTime();
    TimeStamp currentTimeStamp = TimeStampNowProvider::Now();
    Time timeSinceReference = currentTime - aReferenceTime;
    TimeStamp referenceTimeStamp =
        currentTimeStamp - TimeDuration::FromMilliseconds(timeSinceReference);
    UpdateReferenceTime(aReferenceTime, referenceTimeStamp);
  }

  void UpdateReferenceTime(Time aReferenceTime,
                           const TimeStamp& aReferenceTimeStamp) {
    // If two threads try to do this at the same time, taking either set
    // of values is fine, but we need to make sure they are coherent.
    AutoWriteLock lock(mReferenceTimeLock);
    mReferenceTime = aReferenceTime;
    mReferenceTimeStamp = aReferenceTimeStamp;
  }

  bool IsTimeNewerThanTimestamp(Time aTime, TimeStamp aTimeStamp,
                                TimeStamp* aTimeAsTimeStamp) {
    AutoReadLock lock(mReferenceTimeLock);
    if (mReferenceTimeStamp.IsNull()) {
      // This should never happen, but it seems to (see bug 1989314)
      MOZ_ASSERT_UNREACHABLE("mReferenceTimeStamp should have been set by now");
      // Do our best here. Since we have no mReferenceTimeStamp,
      // assume there is no skewing.
      if (aTimeAsTimeStamp) {
        *aTimeAsTimeStamp = aTimeStamp;
      }
      return false;
    }
    Time timeDelta = aTime - mReferenceTime;

    // Cast the result to signed 64-bit integer first since that should be
    // enough to hold the range of values returned by ToMilliseconds() and
    // the result of converting from double to an integer-type when the value
    // is outside the integer range is undefined.
    // Then we do an implicit cast to Time (typically an unsigned 32-bit
    // integer) which wraps times outside that range.
    TimeDuration timeStampDelta = (aTimeStamp - mReferenceTimeStamp);
    int64_t wholeMillis = static_cast<int64_t>(timeStampDelta.ToMilliseconds());
    Time wrappedTimeStampDelta = wholeMillis;  // truncate to unsigned
    // Half of the valid range of Time
    const Time shift = (static_cast<Time>(0) - static_cast<Time>(1)) / 2;
    // Shift/move origin (0) of the value by UINT32_MAX / 2 and shift
    // it back later in order to support negative deltas. With this
    // approach we can support deltas before shifting in the range
    // [UINT32_MAX / 2 + 1, -UINT32_MAX / 2].
    Time wrappedTimeStampDeltaShifted = wrappedTimeStampDelta + shift;

    int64_t timeToTimeStamp =
        static_cast<int64_t>(wrappedTimeStampDeltaShifted) -
        static_cast<int64_t>(timeDelta) - static_cast<int64_t>(shift);
    bool isNewer = false;
    if (timeToTimeStamp == 0) {
      // wholeMillis needs no adjustment
    } else if (timeToTimeStamp < 0) {
      isNewer = true;
      wholeMillis += (-timeToTimeStamp);
    } else {
      wholeMillis -= timeToTimeStamp;
    }
    if (aTimeAsTimeStamp) {
      *aTimeAsTimeStamp =
          mReferenceTimeStamp + TimeDuration::FromMilliseconds(wholeMillis);

      if (aTimeAsTimeStamp->IsNull()) {
        MOZ_CRASH_UNSAFE_PRINTF(
            "Failed to compute the new timestamp, aTime: %" PRIu32
            ", timeDelta: %" PRIu32 ", wholeMillis: %" PRId64
            ", timeToTimeStamp: %" PRId64,
            static_cast<uint32_t>(aTime), static_cast<uint32_t>(timeDelta),
            wholeMillis, timeToTimeStamp);
      }
    }

    return isNewer;
  }

  Time mReferenceTime MOZ_GUARDED_BY(mReferenceTimeLock);
  TimeStamp mReferenceTimeStamp MOZ_GUARDED_BY(mReferenceTimeLock);
  Time mLastBackwardsSkewCheck;
  mozilla::RWLock mReferenceTimeLock;

  const Time kTimeRange;
  const Time kTimeHalfRange;
  const Time kBackwardsSkewCheckInterval;
};

}  // namespace mozilla

#endif /* SystemTimeConverter_h */
