/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DynamicallyLinkedFunctionPtr.h"
#include "mozilla/RoundedMulDiv.h"
#include "mozilla/TimeStamp.h"
#include <intrin.h>
#include <windows.h>

#ifndef MOZ_TIMESTAMP_TICK_RATE_IS_DYNAMIC
#  error "The QueryPerformanceCounter tick rate is only known at run time."
#endif

// Historical note: We used to sample both QueryPerformanceCounter (QPC) and
// GetTickCount (GTC) timestamps in the past, as very early implementations of
// QPC were buggy. We had heuristics to determine if QPC is unreliable and
// would have switched to GTC in case, which could cause unexpected time
// travels between QPC and GPC values when that occured.
//
// Since Windows 8 together with the then modern CPUs, QPC became both reliable
// and almost as fast as GTC timestamps and provides a much higher resolution.
// QPC in general exists long enough even on older systems than Windows 8, such
// that we can just always rely on it, as we do in rust.

// ----------------------------------------------------------------------------
// Global variables, not changing at runtime
// ----------------------------------------------------------------------------

// Result of QueryPerformanceFrequency, set only once on startup.
static double sTicksPerSecd;
static double sTicksPerMsd;
// Exact integer QueryPerformanceFrequency (ticks per second), used by
// ToTicksAtRate. This is the integer companion to the lossy double
// sTicksPerSecd.
static uint64_t sQpcFrequency = 0;

// ----------------------------------------------------------------------------
// Useful constants
// ----------------------------------------------------------------------------

static constexpr double kMsPerSecd = 1000.0;

namespace mozilla {

// Result is in ticks.
static inline ULONGLONG PerformanceCounter() {
  LARGE_INTEGER pc;
  MOZ_ALWAYS_TRUE(::QueryPerformanceCounter(&pc));
  return pc.QuadPart;
}

static void InitConstants() {
  // Query the frequency from QPC and rely on it for all values.
  // Note: The resolution used to be sampled based on a loop of QPC calls.
  // While it is true that on most systems we cannot expect to subsequently
  // sample QPC values as fast as the QPC frequency, we still will get that
  // as resolution of the sampled values, that is we have 1 tick resolution.
  LARGE_INTEGER freq;
  bool hasQPC = ::QueryPerformanceFrequency(&freq);
  MOZ_RELEASE_ASSERT(hasQPC);
  sTicksPerSecd = double(freq.QuadPart);
  sTicksPerMsd = sTicksPerSecd / kMsPerSecd;
  sQpcFrequency = static_cast<uint64_t>(freq.QuadPart);

  // The QPC frequency is a whole number of ticks per second by definition, but
  // it need not divide evenly into smaller units. It is typically 10 MHz on x86
  // and x64, which divides seconds through microseconds evenly, but Microsoft
  // documents that the value is not derived from the hardware frequency and
  // must not be assumed.
  detail::InitTickScales(sQpcFrequency, 1);
}

// ----------------------------------------------------------------------------
// TimeDuration and TimeStamp implementation
// ----------------------------------------------------------------------------

MFBT_API double BaseTimeDurationPlatformUtils::ToSeconds(int64_t aTicks) {
  return double(aTicks) / sTicksPerSecd;
}

MFBT_API int64_t
BaseTimeDurationPlatformUtils::TicksFromMilliseconds(double aMilliseconds) {
  double result = sTicksPerMsd * aMilliseconds;
  // NOTE: this MUST be a >= test, because int64_t(double(INT64_MAX))
  // overflows and gives INT64_MIN.
  if (result >= double(INT64_MAX)) {
    return INT64_MAX;
  }
  if (result <= double(INT64_MIN)) {
    return INT64_MIN;
  }

  return (int64_t)result;
}

MFBT_API int64_t BaseTimeDurationPlatformUtils::ToTicksAtRate(int64_t aTicks,
                                                              uint32_t aRate) {
  // The tick unit is a QueryPerformanceCounter tick; sQpcFrequency is the
  // integer QPC frequency, so this is an exact integer/rational conversion
  // rounded to the nearest aRate tick.
  return RoundedMulDiv(aTicks, aRate, sQpcFrequency);
}

// Note that we init early enough during startup such that we are supposed to
// not yet have started other threads which could try to use us.
static bool gInitialized = false;

MFBT_API void TimeStamp::Startup() {
  if (gInitialized) {
    return;
  }
  InitConstants();
  gInitialized = true;
}

MFBT_API void TimeStamp::Shutdown() {}

MFBT_API TimeStamp TimeStamp::Now(bool aHighResolution) {
  MOZ_ASSERT(gInitialized);
  return TimeStamp((TimeStampValue)PerformanceCounter());
}

// Computes and returns the process uptime in microseconds.
// Returns 0 if an error was encountered.

MFBT_API uint64_t TimeStamp::ComputeProcessUptime() {
  FILETIME start, foo, bar, baz;
  bool success = GetProcessTimes(GetCurrentProcess(), &start, &foo, &bar, &baz);
  if (!success) {
    return 0;
  }

  static const StaticDynamicallyLinkedFunctionPtr<void(WINAPI*)(LPFILETIME)>
      pGetSystemTimePreciseAsFileTime(L"kernel32.dll",
                                      "GetSystemTimePreciseAsFileTime");

  FILETIME now;
  if (pGetSystemTimePreciseAsFileTime) {
    pGetSystemTimePreciseAsFileTime(&now);
  } else {
    GetSystemTimeAsFileTime(&now);
  }

  ULARGE_INTEGER startUsec = {{start.dwLowDateTime, start.dwHighDateTime}};
  ULARGE_INTEGER nowUsec = {{now.dwLowDateTime, now.dwHighDateTime}};

  return (nowUsec.QuadPart - startUsec.QuadPart) / 10ULL;
}

}  // namespace mozilla
