/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_mainthreadidleperiod_h
#define mozilla_dom_mainthreadidleperiod_h

#include "mozilla/TimeStamp.h"
#include "nsThreadUtils.h"

namespace mozilla {

class MainThreadIdlePeriod final : public IdlePeriod {
 public:
  NS_DECL_NSIIDLEPERIOD

  MainThreadIdlePeriod() : mLastIdleDeadline(TimeStamp::Now()) {}

  // In milliseconds.
  static int32_t GetLongIdlePeriod();

 private:
  virtual ~MainThreadIdlePeriod() = default;

  TimeStamp mLastIdleDeadline;
};

}  // namespace mozilla

#endif  // mozilla_dom_mainthreadidleperiod_h
