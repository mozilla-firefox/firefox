/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TimeStamp_h
#define mozilla_TimeStamp_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/CheckedArithmetic.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Types.h"
#include <algorithm>  // for std::min, std::max
#include <ostream>
#include <stdint.h>
#include <type_traits>

namespace IPC {
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

using TimeStampValue = uint64_t;

class TimeStamp;
class TimeStampTests;

// Whether the platform's tick rate is only known at run time. Must agree with
// the implementation picked by mozglue/misc/timestamp.mozbuild, which each
// TimeStamp_*.cpp checks.
#if defined(XP_WIN) || defined(XP_DARWIN)
#  define MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC 1
#endif

/**
 * Platform-specific implementation details of BaseTimeDuration.
 */
class BaseTimeDurationPlatformUtils {
 public:
  static MFBT_API double ToSeconds(int64_t aTicks);
  static MFBT_API int64_t TicksFromMilliseconds(double aMilliseconds);
  // Convert a platform tick count to an integer count of aRate ticks, rounded
  // to the nearest tick, using integer/rational arithmetic (no floating point).
  static MFBT_API int64_t ToTicksAtRate(int64_t aTicks, uint32_t aRate);
};

namespace detail {

/**
 * Converting an integer count of units to a tick count. How that is done
 * depends on whether the platform's tick rate is a compile-time constant.
 *
 * Where the tick rate is dynamic, nothing about it appears here: the conversion
 * lives in TimeStamp.cpp, behind one entry point per unit so that a call site
 * passes nothing but the count. That costs a call, but keeps the tick rate, and
 * the shape of the data describing it, entirely out of mozglue's ABI, and makes
 * a call site smaller than the floating-point call it replaces.
 */
#ifdef MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC

MFBT_API int64_t TicksFromSecondCount(int64_t aCount);
MFBT_API int64_t TicksFromMillisecondCount(int64_t aCount);
MFBT_API int64_t TicksFromMicrosecondCount(int64_t aCount);
MFBT_API int64_t TicksFromNanosecondCount(int64_t aCount);

/**
 * Work out the tick counts the entry points above need, from a platform tick
 * rate of aTickRateNum / aTickRateDen ticks per second. Called by the
 * TimeStamp_*.cpp backend from TimeStamp::Startup(). Internal to mozglue, so
 * deliberately not MFBT_API.
 */
void InitTickScales(uint64_t aTickRateNum, uint64_t aTickRateDen);

#else

/**
 * The tick is always a nanosecond here, which divides every unit evenly, so
 * these are constexpr versions of the out-of-line entry points above, under the
 * same names: each is at most one multiply, and a duration built from an
 * integer constant folds away entirely.
 */
constexpr int64_t SaturatingTicks(int64_t aCount, int64_t aTicksPerUnit) {
  int64_t ticks;
  if (MOZ_LIKELY(SafeMul(aCount, aTicksPerUnit, &ticks))) {
    return ticks;
  }
  return aCount < 0 ? INT64_MIN : INT64_MAX;
}
constexpr int64_t TicksFromSecondCount(int64_t aCount) {
  return SaturatingTicks(aCount, 1000000000);
}
constexpr int64_t TicksFromMillisecondCount(int64_t aCount) {
  return SaturatingTicks(aCount, 1000000);
}
constexpr int64_t TicksFromMicrosecondCount(int64_t aCount) {
  return SaturatingTicks(aCount, 1000);
}
constexpr int64_t TicksFromNanosecondCount(int64_t aCount) { return aCount; }

#endif

// Integral types that spell a character rather than a count. A duration built
// from one of these is almost always a typo, so the integer BaseTimeDuration
// factories reject them. signed char and unsigned char are deliberately absent
// since they are how int8_t and uint8_t are spelled.
template <typename T>
inline constexpr bool IsCharacterType =
    std::is_same_v<T, char> || std::is_same_v<T, char8_t> ||
    std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t> ||
    std::is_same_v<T, wchar_t>;

}  // namespace detail

/**
 * Convert a tick count held in a double to the integer tick count used
 * internally by BaseTimeDuration, saturating at the ends of the int64_t range.
 * Converting an out-of-range double to int64_t is undefined behavior, so the
 * range must be checked before the conversion rather than after.
 */
inline int64_t SaturatingTicksFromDouble(double aTicks) {
  // NOTE: this MUST be a >= test, because int64_t(double(INT64_MAX))
  // overflows and gives INT64_MIN.
  if (aTicks >= double(INT64_MAX)) {
    return INT64_MAX;
  }

  // This MUST be a <= test.
  if (aTicks <= double(INT64_MIN)) {
    return INT64_MIN;
  }

  return int64_t(aTicks);
}

/**
 * Instances of this class represent the length of an interval of time.
 * Negative durations are allowed, meaning the end is before the start.
 *
 * Internally the duration is stored as a system-dependent unit.
 *
 * The ValueCalculator template parameter determines how arithmetic
 * operations are performed on the integer count of ticks (mValue).
 */
template <typename ValueCalculator>
class BaseTimeDuration {
 public:
  // The default duration is 0.
  constexpr BaseTimeDuration() : mValue(0) {}
  // Allow construction using '0' as the initial value, for readability,
  // but no other numbers (so we don't have any implicit unit conversions).
  struct _SomethingVeryRandomHere;
  MOZ_IMPLICIT BaseTimeDuration(_SomethingVeryRandomHere* aZero) : mValue(0) {
    MOZ_ASSERT(!aZero, "Who's playing funny games here?");
  }
  // Default copy-constructor and assignment are OK

  // Converting copy-constructor and assignment operator
  template <typename E>
  explicit BaseTimeDuration(const BaseTimeDuration<E>& aOther)
      : mValue(aOther.mValue) {}

  template <typename E>
  BaseTimeDuration& operator=(const BaseTimeDuration<E>& aOther) {
    mValue = aOther.mValue;
    return *this;
  }

  // Block double multiplier (slower, imprecise if long duration) - Bug 853398.
  // If required, use MultDouble explicitly and with care.
  BaseTimeDuration operator*(const double aMultiplier) const = delete;

  // Block double divisor (for the same reason, and because dividing by
  // fractional values would otherwise invoke the int64_t variant, and rounding
  // the passed argument can then cause divide-by-zero) - Bug 1147491.
  BaseTimeDuration operator/(const double aDivisor) const = delete;

  // ToSeconds returns the (fractional) number of seconds of the duration
  // with the maximum representable precision.
  double ToSeconds() const {
    if (mValue == INT64_MAX) {
      return PositiveInfinity<double>();
    }
    if (mValue == INT64_MIN) {
      return NegativeInfinity<double>();
    }
    return BaseTimeDurationPlatformUtils::ToSeconds(mValue);
  }
  // ToMilliseconds returns the (fractional) number of milliseconds of the
  // duration with the maximum representable precision.
  double ToMilliseconds() const { return ToSeconds() * 1000.0; }
  // ToMicroseconds returns the (fractional) number of microseconds of the
  // duration with the maximum representable precision.
  double ToMicroseconds() const { return ToMilliseconds() * 1000.0; }

  // ToTicksAtRate returns the duration as an integer count of aRate ticks,
  // rounded to the nearest tick. The conversion is done with integer/rational
  // arithmetic directly from the platform tick count to avoid floating-point
  // representation error. Saturated to INT64_MAX/INT64_MIN if result does not
  // fit in int64_t.
  int64_t ToTicksAtRate(uint32_t aRate) const {
    MOZ_ASSERT(aRate > 0, "aRate must be a positive tick rate");
    if (mValue == INT64_MAX) {
      return INT64_MAX;
    }
    if (mValue == INT64_MIN) {
      return INT64_MIN;
    }
    return BaseTimeDurationPlatformUtils::ToTicksAtRate(mValue, aRate);
  }

  // Using a double here is safe enough; with 53 bits we can represent
  // durations up to over 280,000 years exactly.  If the units of
  // mValue do not allow us to represent durations of that length,
  // long durations are clamped to the max/min representable value
  // instead of overflowing.
  static inline BaseTimeDuration FromSeconds(double aSeconds) {
    return FromMilliseconds(aSeconds * 1000.0);
  }
  static BaseTimeDuration FromMilliseconds(double aMilliseconds) {
    if (aMilliseconds == PositiveInfinity<double>()) {
      return Forever();
    }
    if (aMilliseconds == NegativeInfinity<double>()) {
      return FromTicks(INT64_MIN);
    }
    return FromTicks(
        BaseTimeDurationPlatformUtils::TicksFromMilliseconds(aMilliseconds));
  }
  static inline BaseTimeDuration FromMicroseconds(double aMicroseconds) {
    return FromMilliseconds(aMicroseconds / 1000.0);
  }
  static inline BaseTimeDuration FromNanoseconds(double aNanoseconds) {
    return FromMilliseconds(aNanoseconds / 1000000.0);
  }

  // Integer overloads, preferred by overload resolution over the double ones
  // above for any integral argument. These need neither floating-point
  // arithmetic nor the range and infinity checks the double versions do, and
  // are exact wherever the tick rate divides the unit. Pass an integer whenever
  // the duration is a whole number of units.
  //
  // Durations too long to represent saturate to Forever() or its negation, the
  // same values the double versions saturate to.
  template <typename T>
    requires(std::is_integral_v<T>)
  static BaseTimeDuration FromSeconds(T aSeconds) {
    return FromIntegerUnits<1>(aSeconds);
  }
  template <typename T>
    requires(std::is_integral_v<T>)
  static BaseTimeDuration FromMilliseconds(T aMilliseconds) {
    return FromIntegerUnits<1000>(aMilliseconds);
  }
  template <typename T>
    requires(std::is_integral_v<T>)
  static BaseTimeDuration FromMicroseconds(T aMicroseconds) {
    return FromIntegerUnits<1000000>(aMicroseconds);
  }
  // Nanoseconds are the finest unit the tick count can represent, and only then
  // on platforms whose tick is a nanosecond; elsewhere this rounds to the
  // nearest tick (100ns on a 10 MHz clock, ~41.7ns on Apple silicon).
  template <typename T>
    requires(std::is_integral_v<T>)
  static BaseTimeDuration FromNanoseconds(T aNanoseconds) {
    return FromIntegerUnits<1000000000>(aNanoseconds);
  }

  static constexpr BaseTimeDuration Zero() { return BaseTimeDuration(); }
  static constexpr BaseTimeDuration Forever() { return FromTicks(INT64_MAX); }

  BaseTimeDuration operator+(const BaseTimeDuration& aOther) const {
    return FromTicks(ValueCalculator::Add(mValue, aOther.mValue));
  }
  BaseTimeDuration operator-(const BaseTimeDuration& aOther) const {
    return FromTicks(ValueCalculator::Subtract(mValue, aOther.mValue));
  }
  BaseTimeDuration& operator+=(const BaseTimeDuration& aOther) {
    mValue = ValueCalculator::Add(mValue, aOther.mValue);
    return *this;
  }
  BaseTimeDuration& operator-=(const BaseTimeDuration& aOther) {
    mValue = ValueCalculator::Subtract(mValue, aOther.mValue);
    return *this;
  }
  BaseTimeDuration operator-() const {
    // We don't just use FromTicks(ValueCalculator::Subtract(0, mValue))
    // since that won't give the correct result for -TimeDuration::Forever().
    int64_t ticks;
    if (mValue == INT64_MAX) {
      ticks = INT64_MIN;
    } else if (mValue == INT64_MIN) {
      ticks = INT64_MAX;
    } else {
      ticks = -mValue;
    }

    return FromTicks(ticks);
  }

  static BaseTimeDuration Max(const BaseTimeDuration& aA,
                              const BaseTimeDuration& aB) {
    return FromTicks(std::max(aA.mValue, aB.mValue));
  }
  static BaseTimeDuration Min(const BaseTimeDuration& aA,
                              const BaseTimeDuration& aB) {
    return FromTicks(std::min(aA.mValue, aB.mValue));
  }

 public:
  BaseTimeDuration MultDouble(double aMultiplier) const {
    return FromTicks(ValueCalculator::Multiply(mValue, aMultiplier));
  }
  BaseTimeDuration operator*(const int32_t aMultiplier) const {
    return FromTicks(ValueCalculator::Multiply(mValue, aMultiplier));
  }
  BaseTimeDuration operator*(const uint32_t aMultiplier) const {
    return FromTicks(ValueCalculator::Multiply(mValue, aMultiplier));
  }
  BaseTimeDuration operator*(const int64_t aMultiplier) const {
    return FromTicks(ValueCalculator::Multiply(mValue, aMultiplier));
  }
  BaseTimeDuration operator*(const uint64_t aMultiplier) const {
    if (aMultiplier > INT64_MAX) {
      return Forever();
    }
    return FromTicks(ValueCalculator::Multiply(mValue, aMultiplier));
  }
  BaseTimeDuration operator/(const int64_t aDivisor) const {
    MOZ_ASSERT(aDivisor != 0, "Division by zero");
    return FromTicks(ValueCalculator::Divide(mValue, aDivisor));
  }
  double operator/(const BaseTimeDuration& aOther) const {
    MOZ_ASSERT(aOther.mValue != 0, "Division by zero");
    return ValueCalculator::DivideDouble(mValue, aOther.mValue);
  }
  BaseTimeDuration operator%(const BaseTimeDuration& aOther) const {
    MOZ_ASSERT(aOther.mValue != 0, "Division by zero");
    return FromTicks(ValueCalculator::Modulo(mValue, aOther.mValue));
  }

  template <typename E>
  bool operator<(const BaseTimeDuration<E>& aOther) const {
    return mValue < aOther.mValue;
  }
  template <typename E>
  bool operator<=(const BaseTimeDuration<E>& aOther) const {
    return mValue <= aOther.mValue;
  }
  template <typename E>
  bool operator>=(const BaseTimeDuration<E>& aOther) const {
    return mValue >= aOther.mValue;
  }
  template <typename E>
  bool operator>(const BaseTimeDuration<E>& aOther) const {
    return mValue > aOther.mValue;
  }
  template <typename E>
  bool operator==(const BaseTimeDuration<E>& aOther) const {
    return mValue == aOther.mValue;
  }
  template <typename E>
  bool operator!=(const BaseTimeDuration<E>& aOther) const {
    return mValue != aOther.mValue;
  }
  bool IsZero() const { return mValue == 0; }
  explicit operator bool() const { return mValue != 0; }

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const BaseTimeDuration& aDuration) {
    return aStream << aDuration.ToMilliseconds() << " ms";
  }

  // We could define additional operators here:
  // -- convert to/from other time units
  // -- scale duration by a float
  // but let's do that on demand.
  // Comparing durations for equality will only lead to bugs on
  // platforms with high-resolution timers.

 private:
  friend class TimeStamp;
  friend struct IPC::ParamTraits<mozilla::BaseTimeDuration<ValueCalculator>>;
  template <typename>
  friend class BaseTimeDuration;

  static constexpr BaseTimeDuration FromTicks(int64_t aTicks) {
    BaseTimeDuration t;
    t.mValue = aTicks;
    return t;
  }

  static BaseTimeDuration FromTicks(double aTicks) {
    return FromTicks(SaturatingTicksFromDouble(aTicks));
  }

  template <uint32_t UnitsPerSecond, typename T>
  static BaseTimeDuration FromIntegerUnits(T aCount) {
    static_assert(!std::is_same_v<T, bool>,
                  "Building a duration out of a bool is almost certainly a "
                  "mistake; pass an integer count of units");
    static_assert(!detail::IsCharacterType<T>,
                  "Building a duration out of a character type is almost "
                  "certainly a mistake; pass an integer count of units");

    int64_t count;
    if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(int64_t)) {
      // A count that doesn't fit int64_t is longer than any representable
      // duration, so saturate it here and let the scaling below clamp it.
      count = aCount > uint64_t(INT64_MAX) ? INT64_MAX : int64_t(aCount);
    } else {
      count = int64_t(aCount);
    }

    if constexpr (UnitsPerSecond == 1) {
      return FromTicks(detail::TicksFromSecondCount(count));
    } else if constexpr (UnitsPerSecond == 1000) {
      return FromTicks(detail::TicksFromMillisecondCount(count));
    } else if constexpr (UnitsPerSecond == 1000000) {
      return FromTicks(detail::TicksFromMicrosecondCount(count));
    } else {
      static_assert(UnitsPerSecond == 1000000000, "No such unit");
      return FromTicks(detail::TicksFromNanosecondCount(count));
    }
  }

  // Duration, result is implementation-specific difference of two TimeStamps
  int64_t mValue;
};

/**
 * Perform arithmetic operations on the value of a BaseTimeDuration without
 * doing strict checks on the range of values.
 */
class TimeDurationValueCalculator {
 public:
  static int64_t Add(int64_t aA, int64_t aB) { return aA + aB; }
  static int64_t Subtract(int64_t aA, int64_t aB) { return aA - aB; }

  template <typename T>
  static int64_t Multiply(int64_t aA, T aB) {
    static_assert(std::is_integral_v<T>,
                  "Using integer multiplication routine with non-integer type."
                  " Further specialization required");
    return aA * static_cast<int64_t>(aB);
  }

  static int64_t Divide(int64_t aA, int64_t aB) { return aA / aB; }
  static double DivideDouble(int64_t aA, int64_t aB) {
    return static_cast<double>(aA) / aB;
  }
  static int64_t Modulo(int64_t aA, int64_t aB) { return aA % aB; }
};

template <>
inline int64_t TimeDurationValueCalculator::Multiply<double>(int64_t aA,
                                                             double aB) {
  return SaturatingTicksFromDouble(static_cast<double>(aA) * aB);
}

/**
 * Specialization of BaseTimeDuration that uses TimeDurationValueCalculator for
 * arithmetic on the mValue member.
 *
 * Use this class for time durations that are *not* expected to hold values of
 * Forever (or the negative equivalent) or when such time duration are *not*
 * expected to be used in arithmetic operations.
 */
typedef BaseTimeDuration<TimeDurationValueCalculator> TimeDuration;

/**
 * Instances of this class represent moments in time, or a special
 * "null" moment. We do not use the non-monotonic system clock or
 * local time, since they can be reset, causing apparent backward
 * travel in time, which can confuse algorithms. Instead we measure
 * elapsed time according to the system.  This time can never go
 * backwards (i.e. it never wraps around, at least not in less than
 * five million years of system elapsed time). It might not advance
 * while the system is sleeping. If TimeStamp::SetNow() is not called
 * at all for hours or days, we might not notice the passage of some
 * of that time.
 *
 * We deliberately do not expose a way to convert TimeStamps to some
 * particular unit. All you can do is compute a difference between two
 * TimeStamps to get a TimeDuration. You can also add a TimeDuration
 * to a TimeStamp to get a new TimeStamp. You can't do something
 * meaningless like add two TimeStamps.
 *
 * Internally this is implemented as a wrapper around high-resolution,
 * monotonic, platform-dependent system clocks.
 *
 * This class is similar to C++11's time_point, however it is
 * explicitly nullable and provides an IsNull() method. time_point
 * is initialized to the clock's epoch and provides a
 * time_since_epoch() method that functions similiarly. i.e.
 * t.IsNull() is equivalent to t.time_since_epoch() ==
 * decltype(t)::duration::zero();
 *
 * Note that, since TimeStamp objects are small, prefer to pass them by value
 * unless there is a specific reason not to do so.
 */
class TimeStamp {
 public:
  using DurationType = TimeDuration;
  /**
   * Initialize to the "null" moment
   */
  constexpr TimeStamp() : mValue(0) {}
  // Default copy-constructor and assignment are OK

  /**
   * The system timestamps are the same as the TimeStamp
   * retrieved by mozilla::TimeStamp. Since we need this for
   * vsync timestamps, we enable the creation of mozilla::TimeStamps
   * on platforms that support vsync aligned refresh drivers / compositors
   * Verified true as of Jan 31, 2015: B2G and OS X
   * False on Windows 7
   * Android's event time uses CLOCK_MONOTONIC via SystemClock.uptimeMilles.
   * So it is same value of TimeStamp posix implementation.
   * Wayland/GTK event time also uses CLOCK_MONOTONIC on Weston/Mutter
   * compositors.
   * UNTESTED ON OTHER PLATFORMS
   */
#if defined(XP_DARWIN) || defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
  static TimeStamp FromSystemTime(int64_t aSystemTime) {
    static_assert(sizeof(aSystemTime) == sizeof(TimeStampValue),
                  "System timestamp should be same units as TimeStampValue");
    return TimeStamp(aSystemTime);
  }
#endif

  /**
   * Return true if this is the "null" moment
   */
  constexpr bool IsNull() const { return mValue == 0; }

  /**
   * Return true if this is not the "null" moment, may be used in tests, e.g.:
   * |if (timestamp) { ... }|
   */
  explicit operator bool() const { return mValue != 0; }

  /**
   * Return a timestamp reflecting the current elapsed system time. This
   * is monotonically increasing (i.e., does not decrease) over the
   * lifetime of this process' XPCOM session.
   *
   * Now() is trying to ensure the best possible precision on each platform,
   * at least one millisecond.
   */
  static TimeStamp Now() { return Now(true); }

  /**
   * Return a (coarse) timestamp reflecting the current elapsed system time.
   * NowLoRes() behaves different depending on the OS:
   *
   * Windows: NowLoRes() == Now(), uses always QueryPerformanceCounter.
   * MacOS:   NowLoRes() == Now(), uses always mach_absolute_time.
   * Posix:   If the kernel supports CLOCK_MONOTONIC_COARSE use that,
   *          CLOCK_MONOTONIC otherwise.
   *
   * Used to promise better performance, which might still be true only for
   * Posix.
   */
  static TimeStamp NowLoRes() { return Now(false); }

  /**
   * Return a timestamp representing the time when the current process was
   * created which will be comparable with other timestamps taken with this
   * class.
   *
   * @returns A timestamp representing the time when the process was created
   */
  static MFBT_API TimeStamp ProcessCreation();

  /**
   * Return the very first timestamp that was taken. This can be used instead
   * of TimeStamp::ProcessCreation() by code that might not allow running the
   * complex logic required to compute the real process creation. This will
   * necessarily have been recorded sometimes after TimeStamp::ProcessCreation()
   * or at best should be equal to it.
   *
   * @returns The first tiemstamp that was taken by this process
   */
  static MFBT_API TimeStamp FirstTimeStamp();

  /**
   * Records a process restart. After this call ProcessCreation() will return
   * the time when the browser was restarted instead of the actual time when
   * the process was created.
   */
  static MFBT_API void RecordProcessRestart();

#ifdef XP_LINUX
  uint64_t RawClockMonotonicNanosecondsSinceBoot() const {
    return static_cast<uint64_t>(mValue);
  }
#endif

#ifdef XP_DARWIN
  // Returns the number of nanoseconds since the mach_absolute_time origin.
  MFBT_API uint64_t RawMachAbsoluteTimeNanoseconds() const;
#endif

#ifdef XP_WIN
  uint64_t RawQueryPerformanceCounterValue() const {
    return static_cast<uint64_t>(mValue);
  }
#endif

  /**
   * Compute the difference between two timestamps. Both must be non-null.
   */
  TimeDuration operator-(const TimeStamp& aOther) const {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    MOZ_ASSERT(!aOther.IsNull(), "Cannot compute with aOther null value");
    static_assert(-INT64_MAX > INT64_MIN, "int64_t sanity check");
    int64_t ticks = int64_t(mValue - aOther.mValue);
    // Check for overflow.
    if (mValue > aOther.mValue) {
      if (ticks < 0) {
        ticks = INT64_MAX;
      }
    } else {
      if (ticks > 0) {
        ticks = INT64_MIN;
      }
    }
    return TimeDuration::FromTicks(ticks);
  }

  TimeStamp operator+(const TimeDuration& aOther) const {
    TimeStamp result = *this;
    result += aOther;
    return result;
  }
  TimeStamp operator-(const TimeDuration& aOther) const {
    TimeStamp result = *this;
    result -= aOther;
    return result;
  }
  TimeStamp& operator+=(const TimeDuration& aOther) {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    TimeStampValue value = mValue + aOther.mValue;
    // Check for underflow.
    // (We don't check for overflow because it's not obvious what the error
    //  behavior should be in that case.)
    if (aOther.mValue < 0 && value > mValue) {
      value = 0;
    }
    mValue = value;
    return *this;
  }
  TimeStamp& operator-=(const TimeDuration& aOther) {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    TimeStampValue value = mValue - aOther.mValue;
    // Check for underflow.
    // (We don't check for overflow because it's not obvious what the error
    //  behavior should be in that case.)
    if (aOther.mValue > 0 && value > mValue) {
      value = 0;
    }
    mValue = value;
    return *this;
  }

  constexpr bool operator<(const TimeStamp& aOther) const {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    MOZ_ASSERT(!aOther.IsNull(), "Cannot compute with aOther null value");
    return mValue < aOther.mValue;
  }
  constexpr bool operator<=(const TimeStamp& aOther) const {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    MOZ_ASSERT(!aOther.IsNull(), "Cannot compute with aOther null value");
    return mValue <= aOther.mValue;
  }
  constexpr bool operator>=(const TimeStamp& aOther) const {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    MOZ_ASSERT(!aOther.IsNull(), "Cannot compute with aOther null value");
    return mValue >= aOther.mValue;
  }
  constexpr bool operator>(const TimeStamp& aOther) const {
    MOZ_ASSERT(!IsNull(), "Cannot compute with a null value");
    MOZ_ASSERT(!aOther.IsNull(), "Cannot compute with aOther null value");
    return mValue > aOther.mValue;
  }
  bool operator==(const TimeStamp& aOther) const {
    return IsNull() ? aOther.IsNull()
                    : !aOther.IsNull() && mValue == aOther.mValue;
  }
  bool operator!=(const TimeStamp& aOther) const { return !(*this == aOther); }

  // Comparing TimeStamps for equality should be discouraged. Adding
  // two TimeStamps, or scaling TimeStamps, is nonsense and must never
  // be allowed.

  static MFBT_API void Startup();
  static MFBT_API void Shutdown();

#if defined(DEBUG)
  TimeStampValue GetValue() const { return mValue; }
#endif

 private:
  friend struct IPC::ParamTraits<mozilla::TimeStamp>;
  friend struct TimeStampInitialization;
  friend class TimeStampTests;

  constexpr MOZ_IMPLICIT TimeStamp(TimeStampValue aValue) : mValue(aValue) {}

  static MFBT_API TimeStamp Now(bool aHighResolution);

  /**
   * Computes the uptime of the current process in microseconds. The result
   * is platform-dependent and needs to be checked against existing timestamps
   * for consistency.
   *
   * @returns The number of microseconds since the calling process was started
   *          or 0 if an error was encountered while computing the uptime
   */
  static MFBT_API uint64_t ComputeProcessUptime();

  /**
   * When built with PRIntervalTime, a value of 0 means this instance
   * is "null". Otherwise, the low 32 bits represent a PRIntervalTime,
   * and the high 32 bits represent a counter of the number of
   * rollovers of PRIntervalTime that we've seen. This counter starts
   * at 1 to avoid a real time colliding with the "null" value.
   *
   * PR_INTERVAL_MAX is set at 100,000 ticks per second. So the minimum
   * time to wrap around is about 2^64/100000 seconds, i.e. about
   * 5,849,424 years.
   *
   * When using a system clock, a value is system dependent.
   */
  TimeStampValue mValue;
};

}  // namespace mozilla

#endif /* mozilla_TimeStamp_h */
