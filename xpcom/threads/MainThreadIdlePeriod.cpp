/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MainThreadIdlePeriod.h"

#include "VRManagerChild.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_idle_period.h"
#include "mozilla/dom/Document.h"
#include "nsRefreshDriver.h"
#include "nsThreadUtils.h"

// The amount of idle time (milliseconds) reserved for a long idle period.
static const int32_t kLongIdlePeriodMS = 50;

// The minimum amount of time (milliseconds) required for an idle period to be
// scheduled on the main thread. N.B. layout.idle_period.time_limit adds
// padding at the end of the idle period, which makes the point in time that we
// expect to become busy again be:
//   now + idle_period.min + layout.idle_period.time_limit
// or during page load
//   now + idle_period.during_page_load.min + layout.idle_period.time_limit

static const uint32_t kMaxTimerThreadBound = 25;  // Number of timers to check.

namespace mozilla {

NS_IMETHODIMP
MainThreadIdlePeriod::GetIdlePeriodHint(TimeStamp* aIdleDeadline) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aIdleDeadline);

  TimeStamp now = TimeStamp::Now();
  TimeStamp currentGuess =
      now + TimeDuration::FromMilliseconds(kLongIdlePeriodMS);

  currentGuess = nsRefreshDriver::GetIdleDeadlineHint(
      currentGuess, nsRefreshDriver::IdleCheck::AllVsyncListeners);
  if (XRE_IsContentProcess()) {
    currentGuess = gfx::VRManagerChild::GetIdleDeadlineHint(currentGuess);
  }
  currentGuess = NS_GetTimerDeadlineHintOnCurrentThread(currentGuess,
                                                        kMaxTimerThreadBound);

  // If the idle period is too small, then just return a null time
  // to indicate we are busy. Otherwise return the actual deadline.
  double highRateMultiplier = nsRefreshDriver::HighRateMultiplier();
  TimeDuration minIdlePeriod = TimeDuration::FromMilliseconds(
      std::max(highRateMultiplier * StaticPrefs::idle_period_min(), 1.0));
  bool busySoon = currentGuess.IsNull() ||
                  (now >= (currentGuess - minIdlePeriod)) ||
                  currentGuess < mLastIdleDeadline;

  // During page load use higher minimum idle period.
  if (!busySoon && XRE_IsContentProcess() &&
      mozilla::dom::Document::HasRecentlyStartedForegroundLoads()) {
    TimeDuration minIdlePeriod = TimeDuration::FromMilliseconds(std::max(
        highRateMultiplier * StaticPrefs::idle_period_during_page_load_min(),
        1.0));
    busySoon = (now >= (currentGuess - minIdlePeriod));
  }

  if (!busySoon) {
    *aIdleDeadline = mLastIdleDeadline = currentGuess;
  }

  return NS_OK;
}

/* static */
int32_t MainThreadIdlePeriod::GetLongIdlePeriod() { return kLongIdlePeriodMS; }

}  // namespace mozilla
