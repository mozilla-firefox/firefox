/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/intl/calendar/ISODate.h"

#include <array>
#include <stddef.h>
#include <stdint.h>

namespace mozilla::intl::calendar {

// Copied from js/src/builtin/temporal/Calendar.cpp

static int32_t DayFromYear(int32_t year) {
  return 365 * (year - 1970) + FloorDiv(year - 1969, 4) -
         FloorDiv(year - 1901, 100) + FloorDiv(year - 1601, 400);
}

static constexpr bool IsISOLeapYear(int32_t year) {
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static constexpr int32_t ISODaysInMonth(int32_t year, int32_t month) {
  MOZ_ASSERT(1 <= month && month <= 12);

  constexpr uint8_t daysInMonth[2][13] = {
      {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
      {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

  return daysInMonth[IsISOLeapYear(year)][month];
}

static constexpr auto FirstDayOfMonth(int32_t year) {
  // The following array contains the day of year for the first day of each
  // month, where index 0 is January, and day 0 is January 1.
  std::array<int32_t, 13> days = {};
  for (int32_t month = 1; month <= 12; ++month) {
    days[month] = days[month - 1] + ISODaysInMonth(year, month);
  }
  return days;
}

static int32_t ISODayOfYear(const ISODate& isoDate) {
  const auto& [year, month, day] = isoDate;

  // First day of month arrays for non-leap and leap years.
  constexpr decltype(FirstDayOfMonth(0)) firstDayOfMonth[2] = {
      FirstDayOfMonth(1), FirstDayOfMonth(0)};

  return firstDayOfMonth[IsISOLeapYear(year)][month - 1] + day;
}

int32_t MakeDay(const ISODate& date) {
  return DayFromYear(date.year) + ISODayOfYear(date) - 1;
}

}  // namespace mozilla::intl::calendar
