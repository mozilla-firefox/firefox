/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/RoundedMulDiv.h"
#include "mozilla/TimeStamp.h"
#include "prinrval.h"
#include "prthread.h"

using mozilla::TimeDuration;
using mozilla::TimeStamp;

TEST(TimeStamp, RoundedMulDiv)
{
  EXPECT_EQ(mozilla::RoundedMulDiv(1, 1, 3), 0);
  EXPECT_EQ(mozilla::RoundedMulDiv(2, 1, 3), 1);
  EXPECT_EQ(mozilla::RoundedMulDiv(1, 1, 2), 1);
  EXPECT_EQ(mozilla::RoundedMulDiv(-1, 1, 2), -1);

  // Intermediate overflow saturates in the direction of the input sign.
  EXPECT_EQ(mozilla::RoundedMulDiv(3, UINT64_MAX, 1), INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-3, UINT64_MAX, 1), INT64_MIN);
  EXPECT_EQ(mozilla::RoundedMulDiv(6, UINT64_MAX, 7), INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-6, UINT64_MAX, 7), INT64_MIN);
  EXPECT_EQ(mozilla::RoundedMulDiv(1, UINT64_MAX, 2), INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-1, UINT64_MAX, 2), INT64_MIN);
  EXPECT_EQ(mozilla::RoundedMulDiv(3, UINT64_MAX - 2, 2), INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-3, UINT64_MAX - 2, 2), INT64_MIN);
  EXPECT_EQ(mozilla::RoundedMulDiv(INT64_MAX, 2, 1), INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-INT64_MAX, 2, 1), INT64_MIN);

  // Conservative saturation: the exact rounded result 2000000000 is
  // representable in int64_t, but the intermediate r * aMultiplier overflows
  // uint64_t, so RoundedMulDiv saturates rather than computing it.
  //
  // This is unreachable via ToTicksAtRate: there aMultiplier and aDivisor are
  // aRate and the clock frequency, so the GCD reduction cancels their common
  // factor and bounds the intermediate r * aMultiplier by aRate * aDivisor.
  // Every backend's aDivisor is below 2^32 (posix 1e9, darwin 3e9 on Apple
  // Silicon, windows QPC frequency) and aRate <= UINT32_MAX, so that product
  // stays below 2^64. This case exceeds it only because its divisor
  // (1e10 + 1) is above 2^32 and is paired with a comparably large multiplier.
  // It is reachable only by calling RoundedMulDiv directly.
  EXPECT_EQ(mozilla::RoundedMulDiv(2000000000, 10000000000, 10000000001),
            INT64_MAX);
  EXPECT_EQ(mozilla::RoundedMulDiv(-2000000000, 10000000000, 10000000001),
            INT64_MIN);

  // Boundary of the conservative saturation at the maximum rate
  // aMultiplier = UINT32_MAX. The intermediate r * aMultiplier is at most
  // (aDivisor - 1) * UINT32_MAX, which crosses 2^64 at aDivisor ~= 2^32: a
  // divisor just below 2^32 still returns the exact result, one just above
  // saturates. Both divisors are primes near 2^32 coprime to UINT32_MAX, and
  // aValue = aDivisor - 1, so no GCD reduction hides the bound.
  EXPECT_EQ(mozilla::RoundedMulDiv(4294967290, UINT32_MAX, 4294967291),
            4294967294);  // aDivisor = 2^32 - 5
  EXPECT_EQ(mozilla::RoundedMulDiv(-4294967290, UINT32_MAX, 4294967291),
            -4294967294);
  EXPECT_EQ(mozilla::RoundedMulDiv(4294967310, UINT32_MAX, 4294967311),
            INT64_MAX);  // aDivisor = 2^32 + 15
  EXPECT_EQ(mozilla::RoundedMulDiv(-4294967310, UINT32_MAX, 4294967311),
            INT64_MIN);

  // Real clock divisors sit far below that 2^32 boundary, so even at
  // aMultiplier = UINT32_MAX the exact result is returned, which is the
  // guarantee ToTicksAtRate depends on. On Apple Silicon (mach timebase
  // sNumer/sDenom = 125/3) the GCD reduction is load-bearing: the raw
  // (3e9 - 1) * 125 * UINT32_MAX ~= 1.6e21 exceeds 2^64, but reduction shrinks
  // the intermediate to ~4.6e14. (Intel macOS uses 1/1, i.e. the posix case.)
  EXPECT_EQ(mozilla::RoundedMulDiv(999999999, UINT32_MAX, 1000000000),
            4294967291);  // posix: aDivisor = 1e9
  EXPECT_EQ(mozilla::RoundedMulDiv(2999999999, 125ULL * UINT32_MAX, 3000000000),
            536870911696);  // Apple Silicon (125/3 timebase)
  EXPECT_EQ(mozilla::RoundedMulDiv(9999999, UINT32_MAX, 10000000),
            4294966866);  // windows: aDivisor = 10 MHz QPC
}

TEST(TimeStamp, Main)
{
  TimeDuration td;
  EXPECT_TRUE(td.ToSeconds() == 0.0);
  EXPECT_TRUE(TimeDuration::FromSeconds(5).ToSeconds() == 5.0);
  EXPECT_TRUE(TimeDuration::FromMilliseconds(5000).ToSeconds() == 5.0);
  EXPECT_TRUE(TimeDuration::FromSeconds(1) < TimeDuration::FromSeconds(2));
  EXPECT_FALSE(TimeDuration::FromSeconds(1) < TimeDuration::FromSeconds(1));
  EXPECT_TRUE(TimeDuration::FromSeconds(2) > TimeDuration::FromSeconds(1));
  EXPECT_FALSE(TimeDuration::FromSeconds(1) > TimeDuration::FromSeconds(1));
  EXPECT_TRUE(TimeDuration::FromSeconds(1) <= TimeDuration::FromSeconds(2));
  EXPECT_TRUE(TimeDuration::FromSeconds(1) <= TimeDuration::FromSeconds(1));
  EXPECT_FALSE(TimeDuration::FromSeconds(2) <= TimeDuration::FromSeconds(1));
  EXPECT_TRUE(TimeDuration::FromSeconds(2) >= TimeDuration::FromSeconds(1));
  EXPECT_TRUE(TimeDuration::FromSeconds(1) >= TimeDuration::FromSeconds(1));
  EXPECT_FALSE(TimeDuration::FromSeconds(1) >= TimeDuration::FromSeconds(2));

  TimeStamp ts;
  EXPECT_TRUE(ts.IsNull());

  ts = TimeStamp::Now();
  EXPECT_TRUE(!ts.IsNull());
  EXPECT_TRUE((ts - ts).ToSeconds() == 0.0);

  PR_Sleep(PR_SecondsToInterval(2));

  TimeStamp ts2(TimeStamp::Now());
  EXPECT_TRUE(ts2 > ts);
  EXPECT_FALSE(ts > ts);
  EXPECT_TRUE(ts < ts2);
  EXPECT_FALSE(ts < ts);
  EXPECT_TRUE(ts <= ts2);
  EXPECT_TRUE(ts <= ts);
  EXPECT_FALSE(ts2 <= ts);
  EXPECT_TRUE(ts2 >= ts);
  EXPECT_TRUE(ts2 >= ts);
  EXPECT_FALSE(ts >= ts2);

  // We can't be sure exactly how long PR_Sleep slept for. It should have
  // slept for at least one second. We might have slept a lot longer due
  // to process scheduling, but hopefully not more than 10 seconds.
  td = ts2 - ts;
  EXPECT_TRUE(td.ToSeconds() > 1.0);
  EXPECT_TRUE(td.ToSeconds() < 20.0);
  td = ts - ts2;
  EXPECT_TRUE(td.ToSeconds() < -1.0);
  EXPECT_TRUE(td.ToSeconds() > -20.0);

  // Now() is trying to ensure the best possible precision on each platform,
  // but guarantees at least one millisecond resolution. Given the huge
  // difference between the assumed resolution of the clock on modern CPUs
  // (< 100ns, often close to 1ns as of 2025) and that guarantee, we can safely
  // test for 1ms without fearing intermittents caused by jitter.
  TimeStamp start = TimeStamp::Now();
  TimeStamp last = start;
  int updated = 0;
  int same = 0;
  while ((last - start).ToMilliseconds() < 1.0) {
    TimeStamp next = TimeStamp::Now();
    if ((next - last).ToMicroseconds() > 0.0) {
      // Only count if we saw progress in the ticks.
      ++updated;
      last = next;
    } else {
      ++same;
    }
  }
  printf("  Poll saw updated iterations in 1ms: %d\n", updated);
  printf("  Poll saw same iterations in 1ms: %d\n", same);
  // If we saw 2 updates, we can be pretty sure to be able to guarantee at
  // least 1ms resolution. In practice we see much higher numbers here (>>1K).
  EXPECT_GE(updated, 2);
}

TEST(TimeStamp, ToTicksAtRate)
{
  // Zero and the saturating sentinels.
  EXPECT_EQ(TimeDuration().ToTicksAtRate(90000), 0);
  EXPECT_EQ(TimeDuration::Forever().ToTicksAtRate(90000), INT64_MAX);
  EXPECT_EQ((-TimeDuration::Forever()).ToTicksAtRate(90000), INT64_MIN);

  const int64_t maxWholeSecondsAtMaxRate =
      INT64_MAX / static_cast<int64_t>(UINT32_MAX);
  const int64_t maxWholeSecondTicks =
      maxWholeSecondsAtMaxRate * static_cast<int64_t>(UINT32_MAX);
  const TimeDuration maxWholeSecondDuration =
      TimeDuration::FromSeconds(maxWholeSecondsAtMaxRate);
  EXPECT_EQ(maxWholeSecondDuration.ToTicksAtRate(UINT32_MAX),
            maxWholeSecondTicks);
  EXPECT_EQ((-maxWholeSecondDuration).ToTicksAtRate(UINT32_MAX),
            -maxWholeSecondTicks);

  const TimeDuration overflowingWholeSecondDuration =
      TimeDuration::FromSeconds(maxWholeSecondsAtMaxRate + 1.0);
  EXPECT_EQ(overflowingWholeSecondDuration.ToTicksAtRate(UINT32_MAX),
            INT64_MAX);
  EXPECT_EQ((-overflowingWholeSecondDuration).ToTicksAtRate(UINT32_MAX),
            INT64_MIN);

  const TimeDuration overflowingUint64WholeSecondDuration =
      TimeDuration::FromSeconds(5000000000.0);
  EXPECT_EQ(overflowingUint64WholeSecondDuration.ToTicksAtRate(UINT32_MAX),
            INT64_MAX);
  EXPECT_EQ((-overflowingUint64WholeSecondDuration).ToTicksAtRate(UINT32_MAX),
            INT64_MIN);

  // Durations that are an exact multiple of the target tick must round-trip
  // EXACTLY on every platform, regardless of the underlying clock's tick rate.
  EXPECT_EQ(TimeDuration::FromSeconds(1).ToTicksAtRate(90000), 90000);
  EXPECT_EQ(TimeDuration::FromMilliseconds(500).ToTicksAtRate(90000), 45000);
  EXPECT_EQ(TimeDuration::FromMilliseconds(125).ToTicksAtRate(90000), 11250);
  EXPECT_EQ(TimeDuration::FromSeconds(17).ToTicksAtRate(90000), 1530000);
  EXPECT_EQ(TimeDuration::FromMilliseconds(2125).ToTicksAtRate(90000), 191250);

  // Rate-agnostic: production uses the MediaTrackGraph (audio device) rate.
  EXPECT_EQ(TimeDuration::FromSeconds(1).ToTicksAtRate(48000), 48000);
  EXPECT_EQ(TimeDuration::FromMilliseconds(500).ToTicksAtRate(48000), 24000);
  EXPECT_EQ(TimeDuration::FromSeconds(1).ToTicksAtRate(44100), 44100);

  // Off-grid durations round to the NEAREST tick (not floored):
  //   11 us    = 0.99 ticks   @ 90 kHz -> 1
  //   99999 us = 8999.91 ticks @ 90 kHz -> 9000
  EXPECT_EQ(TimeDuration::FromMicroseconds(11).ToTicksAtRate(90000), 1);
  EXPECT_EQ(TimeDuration::FromMicroseconds(99999).ToTicksAtRate(90000), 9000);

  // Negative durations are symmetric.
  EXPECT_EQ((-TimeDuration::FromSeconds(1)).ToTicksAtRate(90000), -90000);
  EXPECT_EQ((-TimeDuration::FromMilliseconds(500)).ToTicksAtRate(90000),
            -45000);
}

TEST(TimeStamp, IntegerDurations)
{
  // An integral argument picks the integer overloads, which convert with
  // integer arithmetic only. ToTicksAtRate() is likewise exact, so a whole
  // number of units must round-trip EXACTLY on every platform, whatever the
  // underlying clock's tick rate. Note that the double overloads are not
  // required to agree to the tick: they round twice, so on a clock whose tick
  // is not a whole number of nanoseconds they can land a tick low.
  for (int64_t seconds : {0, 1, 2, 17, 60, 3600, 86400, 1000000}) {
    EXPECT_EQ(TimeDuration::FromSeconds(seconds).ToTicksAtRate(1), seconds);
    EXPECT_EQ(TimeDuration::FromSeconds(-seconds).ToTicksAtRate(1), -seconds);
  }
  for (int64_t ms : {0, 1, 5, 125, 500, 1000, 2125, 123456}) {
    EXPECT_EQ(TimeDuration::FromMilliseconds(ms).ToTicksAtRate(1000), ms);
    EXPECT_EQ(TimeDuration::FromMilliseconds(-ms).ToTicksAtRate(1000), -ms);
  }
  for (int64_t us : {0, 1, 11, 500, 99999, 1000000}) {
    EXPECT_EQ(TimeDuration::FromMicroseconds(us).ToTicksAtRate(1000000), us);
    EXPECT_EQ(TimeDuration::FromMicroseconds(-us).ToTicksAtRate(1000000), -us);
  }

  // The three units agree with each other.
  EXPECT_EQ(TimeDuration::FromSeconds(1).ToTicksAtRate(1000000),
            TimeDuration::FromMilliseconds(1000).ToTicksAtRate(1000000));
  EXPECT_EQ(TimeDuration::FromSeconds(1).ToTicksAtRate(1000000),
            TimeDuration::FromMicroseconds(1000000).ToTicksAtRate(1000000));

  // Every integral type is accepted, and narrow ones don't lose their sign.
  EXPECT_EQ(TimeDuration::FromMilliseconds(int8_t(-125)).ToTicksAtRate(1000),
            -125);
  EXPECT_EQ(TimeDuration::FromMilliseconds(uint8_t(250)).ToTicksAtRate(1000),
            250);
  EXPECT_EQ(TimeDuration::FromMilliseconds(int16_t(-500)).ToTicksAtRate(1000),
            -500);
  EXPECT_EQ(TimeDuration::FromMilliseconds(uint32_t(1000)).ToTicksAtRate(1000),
            1000);
  EXPECT_EQ(TimeDuration::FromMilliseconds(size_t(1000)).ToTicksAtRate(1000),
            1000);

  // Counts too long to represent saturate, as the double overloads do.
  EXPECT_EQ(TimeDuration::FromSeconds(INT64_MAX), TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromMilliseconds(INT64_MAX), TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromMicroseconds(INT64_MAX), TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromSeconds(INT64_MIN), -TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromMilliseconds(INT64_MIN),
            -TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromMicroseconds(INT64_MIN),
            -TimeDuration::Forever());

  // Unsigned counts past INT64_MAX saturate rather than wrapping negative.
  EXPECT_EQ(TimeDuration::FromSeconds(UINT64_MAX), TimeDuration::Forever());
  EXPECT_EQ(TimeDuration::FromMicroseconds(UINT64_MAX),
            TimeDuration::Forever());

  // A non-integral argument still picks the double overloads; this would not
  // compile if the integer ones took over.
  EXPECT_EQ(TimeDuration::FromMilliseconds(0.5).ToTicksAtRate(2000), 1);
  EXPECT_EQ(TimeDuration::FromSeconds(1.5).ToTicksAtRate(2), 3);
  EXPECT_EQ(TimeDuration::FromMilliseconds(1.0f).ToTicksAtRate(1000), 1);
}

TEST(TimeStamp, NanosecondDurations)
{
  // A nanosecond is finer than the tick on most platforms, so FromNanoseconds
  // rounds to the nearest tick and only whole ticks round-trip. What must hold
  // everywhere is that it agrees exactly with the coarser units, since
  // 1000 * (rate/1e9) and rate/1e6 are the same rational and so round alike.
  for (int64_t us : {0, 1, 11, 500, 99999, 1000000}) {
    EXPECT_EQ(TimeDuration::FromNanoseconds(us * 1000),
              TimeDuration::FromMicroseconds(us))
        << "nanoseconds disagree with microseconds at " << us << " us";
    EXPECT_EQ(TimeDuration::FromNanoseconds(-us * 1000),
              TimeDuration::FromMicroseconds(-us));
  }
  EXPECT_EQ(TimeDuration::FromNanoseconds(1000000000),
            TimeDuration::FromSeconds(1));
  EXPECT_EQ(TimeDuration::FromNanoseconds(0), TimeDuration());

  // Whole microseconds expressed in nanoseconds therefore round-trip exactly.
  EXPECT_EQ(TimeDuration::FromNanoseconds(500000).ToTicksAtRate(1000000), 500);
  EXPECT_EQ(TimeDuration::FromNanoseconds(-500000).ToTicksAtRate(1000000),
            -500);

  // Rounding is symmetric about zero and monotonic in between.
  TimeDuration previous = TimeDuration::FromNanoseconds(0);
  for (int64_t ns = 1; ns <= 5000; ns += 7) {
    TimeDuration current = TimeDuration::FromNanoseconds(ns);
    EXPECT_TRUE(current >= previous) << "not monotonic at " << ns << " ns";
    EXPECT_EQ(TimeDuration::FromNanoseconds(-ns), -current);
    previous = current;
  }

  // INT64_MAX nanoseconds is ~292 years, which is a representable number of
  // ticks at any rate below 1 GHz, so unlike the coarser units nanoseconds
  // don't generally saturate - on a 1 GHz tick INT64_MAX ns lands exactly on
  // the Forever() sentinel, on a 10 MHz tick it is an ordinary duration. What
  // must hold everywhere is that the ends of the range don't wrap, and that an
  // unsigned count past INT64_MAX clamps rather than going negative.
  EXPECT_TRUE(TimeDuration::FromNanoseconds(INT64_MAX) > TimeDuration());
  EXPECT_TRUE(TimeDuration::FromNanoseconds(INT64_MIN) < TimeDuration());
  EXPECT_EQ(TimeDuration::FromNanoseconds(UINT64_MAX),
            TimeDuration::FromNanoseconds(INT64_MAX));

  // The double overload still exists for symmetry with the other units.
  EXPECT_EQ(TimeDuration::FromNanoseconds(1000000.0).ToTicksAtRate(1000), 1);
}
