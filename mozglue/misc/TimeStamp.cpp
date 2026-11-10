/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the OS-independent methods of the TimeStamp class
 */

#include "mozilla/MathAlgorithms.h"
#include "mozilla/RoundedMulDiv.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Uptime.h"
#include <string.h>
#include <stdlib.h>

namespace mozilla {

namespace detail {

/**
 * How many platform ticks make up one unit of time, held two ways.
 *
 * mMul / mDiv is always the exact count, as a rational reduced to lowest terms.
 * mTicksPerUnit is that same count as a plain integer when mDiv is 1, and 0
 * when it isn't, so that a caller can branch on it and just multiply.
 *
 * The rates in current use - 1 GHz on POSIX and Intel macOS, 24 MHz on Apple
 * silicon, and typically 10 MHz on Windows x86 and x64 - divide seconds,
 * milliseconds and microseconds evenly, so mDiv is 1 and mTicksPerUnit is
 * usable for those. Nanoseconds are different: no rate below 1 GHz can divide
 * them, so there mDiv is routinely not 1 and the rational is what gets used.
 * Any other rate takes the rational path for each unit it doesn't divide - for
 * example the Arm generic timer's 19.2 MHz, or the 3,125,000 Hz that
 * Microsoft's documentation uses as an example while warning that the Windows
 * rate must not be assumed - hence keeping both forms.
 *
 * Private to this file: no caller outside it, and nothing in mozglue's ABI,
 * depends on this shape.
 */
struct TickScale {
  int64_t mTicksPerUnit;
  int64_t mMul;
  int64_t mDiv;

  /**
   * Build a TickScale for one 1/aUnitsPerSecond-th of a second, given a
   * platform tick rate of aTickRateNum / aTickRateDen ticks per second.
   *
   * constexpr so that the tick rates we know of are checked at compile time;
   * see TickScaleTests below.
   */
  static constexpr TickScale FromTickRate(uint64_t aTickRateNum,
                                          uint64_t aTickRateDen,
                                          uint32_t aUnitsPerSecond) {
    MOZ_RELEASE_ASSERT(aTickRateNum != 0 && aTickRateDen != 0,
                       "Invalid platform tick rate");
    MOZ_RELEASE_ASSERT(aUnitsPerSecond != 0);

    // One 1/aUnitsPerSecond-th of a second is
    //   aTickRateNum / (aTickRateDen * aUnitsPerSecond)
    // ticks. Reduce that to lowest terms so the scaling done per call stays
    // as small as possible: at a 19.2 MHz tick rate, for instance, a
    // microsecond is 19200000/1000000 ticks, which reduces to 96/5.
    uint64_t den;
    MOZ_RELEASE_ASSERT(SafeMul(aTickRateDen, uint64_t(aUnitsPerSecond), &den),
                       "Platform tick rate denominator is implausibly large");

    uint64_t num = aTickRateNum;
    const uint64_t g = GCD(num, den);
    num /= g;
    den /= g;

    MOZ_RELEASE_ASSERT(num <= uint64_t(INT64_MAX) &&
                       den <= uint64_t(INT64_MAX));

    return TickScale{.mTicksPerUnit = den == 1 ? int64_t(num) : 0,
                     .mMul = int64_t(num),
                     .mDiv = int64_t(den)};
  }

  constexpr bool operator==(const TickScale&) const = default;
};

#ifdef MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC

// How many ticks make up each unit, filled in by InitTickScales() below. mDiv
// starts at 1 rather than 0 so that a conversion attempted before then yields
// a zero duration instead of dividing by zero.
static TickScale sSecondScale = {0, 0, 1};
static TickScale sMillisecondScale = {0, 0, 1};
static TickScale sMicrosecondScale = {0, 0, 1};
static TickScale sNanosecondScale = {0, 0, 1};
#  ifdef DEBUG
static bool sScalesInitialized = false;
#  endif

void InitTickScales(uint64_t aTickRateNum, uint64_t aTickRateDen) {
  sSecondScale = TickScale::FromTickRate(aTickRateNum, aTickRateDen, 1);
  sMillisecondScale = TickScale::FromTickRate(aTickRateNum, aTickRateDen, 1000);
  sMicrosecondScale =
      TickScale::FromTickRate(aTickRateNum, aTickRateDen, 1000000);
  sNanosecondScale =
      TickScale::FromTickRate(aTickRateNum, aTickRateDen, 1000000000);
#  ifdef DEBUG
  sScalesInitialized = true;
#  endif
}

// Scale aCount by aScale, rounding to the nearest tick and saturating at the
// ends of int64_t.
static int64_t TicksFromCount(int64_t aCount, const TickScale& aScale) {
  MOZ_ASSERT(sScalesInitialized,
             "building a TimeDuration before TimeStamp::Startup()");

  if (aScale.mTicksPerUnit != 0) {
    // The tick rate divides this unit evenly, so one multiply is exact.
    int64_t ticks;
    if (MOZ_LIKELY(SafeMul(aCount, aScale.mTicksPerUnit, &ticks))) {
      return ticks;
    }
    // Longer than a tick count can hold; saturate by the count's sign.
    return aCount < 0 ? INT64_MIN : INT64_MAX;
  }

  // It doesn't, so scale by the exact reduced rational instead, rounding to
  // the nearest tick. Halves go away from zero, matching RoundedMulDiv.
  const int64_t half = aCount < 0 ? -(aScale.mDiv / 2) : aScale.mDiv / 2;
  int64_t scaled;
  if (MOZ_LIKELY(SafeMul(aCount, aScale.mMul, &scaled) &&
                 SafeAdd(scaled, half, &scaled))) {
    return scaled / aScale.mDiv;
  }
  return RoundedMulDiv(aCount, uint64_t(aScale.mMul), uint64_t(aScale.mDiv));
}

MFBT_API int64_t TicksFromSecondCount(int64_t aCount) {
  return TicksFromCount(aCount, sSecondScale);
}
MFBT_API int64_t TicksFromMillisecondCount(int64_t aCount) {
  return TicksFromCount(aCount, sMillisecondScale);
}
MFBT_API int64_t TicksFromMicrosecondCount(int64_t aCount) {
  return TicksFromCount(aCount, sMicrosecondScale);
}
MFBT_API int64_t TicksFromNanosecondCount(int64_t aCount) {
  return TicksFromCount(aCount, sNanosecondScale);
}

#endif  // MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC

}  // namespace detail

/**
 * Wrapper class used to initialize static data used by the TimeStamp class
 */
struct TimeStampInitialization {
  /**
   * First timestamp taken when the class static initializers are run. This
   * timestamp is used to sanitize timestamps coming from different sources.
   */
  TimeStamp mFirstTimeStamp;

  /**
   * Timestamp representing the time when the process was created. This field
   * is populated lazily the first time this information is required and is
   * replaced every time the process is restarted.
   */
  TimeStamp mProcessCreation;

  TimeStampInitialization() {
    TimeStamp::Startup();
    mFirstTimeStamp = TimeStamp::Now();
    // On Windows < 10, initializing the uptime requires `mFirstTimeStamp` to be
    // valid.
    mozilla::InitializeUptime();
  }

  ~TimeStampInitialization() { TimeStamp::Shutdown(); }
};

MOZ_RUNINIT static TimeStampInitialization sInitOnce;

MFBT_API TimeStamp TimeStamp::ProcessCreation() {
  if (sInitOnce.mProcessCreation.IsNull()) {
    char* mozAppRestart = getenv("MOZ_APP_RESTART");
    TimeStamp ts;

    /* When calling PR_SetEnv() with an empty value the existing variable may
     * be unset or set to the empty string depending on the underlying platform
     * thus we have to check if the variable is present and not empty. */
    if (mozAppRestart && (strcmp(mozAppRestart, "") != 0)) {
      /* Firefox was restarted, use the first time-stamp we've taken as the new
       * process startup time. */
      ts = sInitOnce.mFirstTimeStamp;
    } else {
      TimeStamp now = Now();
      uint64_t uptime = ComputeProcessUptime();

      ts = now - TimeDuration::FromMicroseconds(uptime);

      if ((ts > sInitOnce.mFirstTimeStamp) || (uptime == 0)) {
        ts = sInitOnce.mFirstTimeStamp;
      }
    }

    sInitOnce.mProcessCreation = ts;
  }

  return sInitOnce.mProcessCreation;
}

void TimeStamp::RecordProcessRestart() {
  sInitOnce.mProcessCreation = TimeStamp();
}

MFBT_API TimeStamp TimeStamp::FirstTimeStamp() {
  return sInitOnce.mFirstTimeStamp;
}

class TickScaleTests {
  using TickScale = detail::TickScale;

  // Reduction to lowest terms, for tick rates that do and don't divide the
  // unit. A rate that divides evenly must leave mDiv == 1 so that callers take
  // the plain-multiply path.

  // 10 MHz: typical Windows x86/x64. Divides seconds through microseconds.
  static_assert(TickScale::FromTickRate(10000000, 1, 1) ==
                TickScale{10000000, 10000000, 1});
  static_assert(TickScale::FromTickRate(10000000, 1, 1000) ==
                TickScale{10000, 10000, 1});
  static_assert(TickScale::FromTickRate(10000000, 1, 1000000) ==
                TickScale{10, 10, 1});
  // ... but not nanoseconds: 1e7/1e9 reduces to 1/100.
  static_assert(TickScale::FromTickRate(10000000, 1, 1000000000) ==
                TickScale{0, 1, 100});

  // 1 GHz: the POSIX tick, and Intel macOS (mach timebase 1/1). Exact for
  // every unit including nanoseconds, which is the identity.
  static_assert(TickScale::FromTickRate(1000000000, 1, 1) ==
                TickScale{1000000000, 1000000000, 1});
  static_assert(TickScale::FromTickRate(1000000000, 1, 1000000000) ==
                TickScale{1, 1, 1});

  // Apple silicon: mach timebase 125/3, i.e. 1e9*3/125 = 24 MHz.
  static_assert(TickScale::FromTickRate(1000000000ull * 3, 125, 1) ==
                TickScale{24000000, 24000000, 1});
  static_assert(TickScale::FromTickRate(1000000000ull * 3, 125, 1000) ==
                TickScale{24000, 24000, 1});
  static_assert(TickScale::FromTickRate(1000000000ull * 3, 125, 1000000) ==
                TickScale{24, 24, 1});
  static_assert(TickScale::FromTickRate(1000000000ull * 3, 125, 1000000000) ==
                TickScale{0, 3, 125});

  // 19.2 MHz, the classic Arm generic timer rate: divides milliseconds but not
  // microseconds, where it reduces to 96/5.
  static_assert(TickScale::FromTickRate(19200000, 1, 1000) ==
                TickScale{19200, 19200, 1});
  static_assert(TickScale::FromTickRate(19200000, 1, 1000000) ==
                TickScale{0, 96, 5});

  // 3,125,000: the value Microsoft's QPC documentation uses as an example.
  // Microseconds reduce to 25/8.
  static_assert(TickScale::FromTickRate(3125000, 1, 1000) ==
                TickScale{3125, 3125, 1});
  static_assert(TickScale::FromTickRate(3125000, 1, 1000000) ==
                TickScale{0, 25, 8});

  // The ACPI PM timer divides no sub-second unit. A whole number of ticks per
  // second is guaranteed, though, so seconds always take the multiply path.
  static_assert(TickScale::FromTickRate(3579545, 1, 1) ==
                TickScale{3579545, 3579545, 1});
  static_assert(TickScale::FromTickRate(3579545, 1, 1000) ==
                TickScale{0, 715909, 200});
};

class TimeStampTests {
  // Check that nullity is set/not set correctly.
  static_assert(TimeStamp{TimeStampValue{0}}.IsNull());
  static_assert(!TimeStamp{TimeStampValue{1}}.IsNull());

  // Check that some very basic comparisons work correctly.
  static constexpr uint64_t sMidTime = (uint64_t)1 << 63;
  static_assert(TimeStampValue{sMidTime + 5} > TimeStampValue{sMidTime - 5});
  static_assert(TimeStampValue{sMidTime + 5} >= TimeStampValue{sMidTime - 5});
  static_assert(TimeStampValue{sMidTime - 5} < TimeStampValue{sMidTime + 5});
  static_assert(TimeStampValue{sMidTime - 5} <= TimeStampValue{sMidTime + 5});
  static_assert(TimeStampValue{sMidTime} == TimeStampValue{sMidTime});
  static_assert(TimeStampValue{sMidTime} >= TimeStampValue{sMidTime});
  static_assert(TimeStampValue{sMidTime} <= TimeStampValue{sMidTime});
  static_assert(TimeStampValue{sMidTime - 5} != TimeStampValue{sMidTime + 5});

  // Check that comparisons involving very large and very small TimeStampValue's
  // work correctly. This may seem excessive, but these asserts would have
  // failed in the past due to a comparison such as "a > b" being implemented as
  // "<cast to signed 64-bit value>(a - b) > 0". When a-b didn't fit into a
  // signed 64-bit value, this would have given an incorrect result.
  static_assert(TimeStampValue{UINT64_MAX} > TimeStampValue{1});
  static_assert(TimeStampValue{1} < TimeStampValue{UINT64_MAX});

  // NOTE/TODO: It would be nice to add some additional tests here that involve
  // arithmetic between TimeStamps and TimeDurations (and verifying some of the
  // special behaviors in some cases such as not wrapping around below zero) but
  // that is not possible right now because those operators are not constexpr.
};

}  // namespace mozilla
