/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Implement TimeStamp::Now() with mach_absolute_time
//
// The "tick" unit for mach_absolute_time is defined using mach_timebase_info()
// which gives a conversion ratio to nanoseconds. For more information see
// Apple's QA1398.
//
// This code is inspired by Chromium's time_mac.cc. The biggest
// differences are that we explicitly initialize using
// TimeStamp::Initialize() instead of lazily in Now() and that
// we store the time value in ticks and convert when needed instead
// of storing the time value in nanoseconds.

#include <mach/mach_time.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include "mozilla/RoundedMulDiv.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Uptime.h"

static const uint64_t kUsPerSec = 1000000;
static const double kNsPerMsd = 1000000.0;
static const double kNsPerSecd = 1000000000.0;

static bool gInitialized = false;
static double sNsPerTickd;
// Exact mach timebase ratio (nanoseconds = ticks * sNumer / sDenom), kept
// alongside the precomputed double so ToTicksAtRate can avoid float rounding.
static uint32_t sNumer = 0;
static uint32_t sDenom = 0;

#ifndef MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC
#  error "The mach timebase is only known at run time."
#endif

static uint64_t ClockTime() {
  // mach_absolute_time is it when it comes to ticks on the Mac.  Other calls
  // with less precision (such as TickCount) just call through to
  // mach_absolute_time.
  //
  // At the time of writing mach_absolute_time returns the number of nanoseconds
  // since boot. This won't overflow 64bits for 500+ years so we aren't going
  // to worry about that possiblity
  return mach_absolute_time();
}

namespace mozilla {

double BaseTimeDurationPlatformUtils::ToSeconds(int64_t aTicks) {
  MOZ_ASSERT(gInitialized, "calling TimeDuration too early");
  return (aTicks * sNsPerTickd) / kNsPerSecd;
}

int64_t BaseTimeDurationPlatformUtils::TicksFromMilliseconds(
    double aMilliseconds) {
  MOZ_ASSERT(gInitialized, "calling TimeDuration too early");
  double result = (aMilliseconds * kNsPerMsd) / sNsPerTickd;
  // NOTE: this MUST be a >= test, because int64_t(double(INT64_MAX))
  // overflows and gives INT64_MIN.
  if (result >= double(INT64_MAX)) {
    return INT64_MAX;
  } else if (result <= double(INT64_MIN)) {
    return INT64_MIN;
  }

  return result;
}

int64_t BaseTimeDurationPlatformUtils::ToTicksAtRate(int64_t aTicks,
                                                     uint32_t aRate) {
  MOZ_ASSERT(gInitialized, "calling TimeDuration too early");
  // aRate ticks = aTicks mach-ticks * (sNumer/sDenom ns/tick) * (aRate / 1e9),
  // rounded to nearest with integer arithmetic (no floating point).
  return RoundedMulDiv(aTicks, static_cast<uint64_t>(sNumer) * aRate,
                       static_cast<uint64_t>(sDenom) * 1000000000u);
}

void TimeStamp::Startup() {
  if (gInitialized) {
    return;
  }

  mach_timebase_info_data_t timebaseInfo;
  // Apple's QA1398 suggests that the output from mach_timebase_info
  // will not change while a program is running, so it should be safe
  // to cache the result.
  kern_return_t kr = mach_timebase_info(&timebaseInfo);
  if (kr != KERN_SUCCESS) {
    MOZ_RELEASE_ASSERT(false, "mach_timebase_info failed");
  }

  sNsPerTickd = double(timebaseInfo.numer) / timebaseInfo.denom;
  sNumer = timebaseInfo.numer;
  sDenom = timebaseInfo.denom;

  // nanoseconds = ticks * sNumer / sDenom, so the tick rate is
  // 1e9 * sDenom / sNumer ticks per second. Both the Intel (1/1) and Apple
  // silicon (125/3) timebases divide seconds through microseconds evenly;
  // nanoseconds reduce to 1/1 and 3/125 respectively.
  detail::InitTickScales(1000000000ull * sDenom, sNumer);

  gInitialized = true;
}

void TimeStamp::Shutdown() {}

TimeStamp TimeStamp::Now(bool aHighResolution) {
  return TimeStamp(ClockTime());
}

uint64_t TimeStamp::RawMachAbsoluteTimeNanoseconds() const {
  return static_cast<uint64_t>(double(mValue) * sNsPerTickd);
}

// Computes and returns the process uptime in microseconds.
// Returns 0 if an error was encountered.
uint64_t TimeStamp::ComputeProcessUptime() {
  struct timeval tv;
  int rv = gettimeofday(&tv, nullptr);

  if (rv == -1) {
    return 0;
  }

  int mib[] = {
      CTL_KERN,
      KERN_PROC,
      KERN_PROC_PID,
      getpid(),
  };
  u_int mibLen = sizeof(mib) / sizeof(mib[0]);

  struct kinfo_proc proc;
  size_t bufferSize = sizeof(proc);
  rv = sysctl(mib, mibLen, &proc, &bufferSize, nullptr, 0);

  if (rv == -1) {
    return 0;
  }

  uint64_t startTime =
      ((uint64_t)proc.kp_proc.p_un.__p_starttime.tv_sec * kUsPerSec) +
      proc.kp_proc.p_un.__p_starttime.tv_usec;
  uint64_t now = (tv.tv_sec * kUsPerSec) + tv.tv_usec;

  if (startTime > now) {
    return 0;
  }

  return now - startTime;
}

}  // namespace mozilla
