/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ISODate_h_
#define intl_components_calendar_ISODate_h_

#include "mozilla/intl/calendar/MonthCode.h"

#include <stdint.h>

namespace mozilla::intl::calendar {

struct ISODate final {
  int32_t year = 0;
  int32_t month = 0;
  int32_t day = 0;
};

struct CalendarDate final {
  int32_t year = 0;
  MonthCode monthCode = {};
  int32_t day = 0;
};

inline int32_t FloorDiv(int32_t dividend, int32_t divisor) {
  int32_t quotient = dividend / divisor;
  int32_t remainder = dividend % divisor;
  if (remainder < 0) {
    quotient -= 1;
  }
  return quotient;
}

/**
 * Return the day relative to the Unix epoch, January 1 1970.
 */
int32_t MakeDay(const ISODate& date);

}  // namespace mozilla::intl::calendar

#endif
