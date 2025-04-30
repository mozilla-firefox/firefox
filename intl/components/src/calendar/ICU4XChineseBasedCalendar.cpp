/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/calendar/ICU4XChineseBasedCalendar.h"

namespace mozilla::intl::calendar {

ICU4XChineseBasedCalendar::ICU4XChineseBasedCalendar(
    capi::ICU4XAnyCalendarKind kind, const icu::Locale& locale,
    UErrorCode& success)
    : ICU4XCalendar(kind, locale, success) {}

ICU4XChineseBasedCalendar::ICU4XChineseBasedCalendar(
    capi::ICU4XAnyCalendarKind kind, const icu::TimeZone& timeZone,
    const icu::Locale& locale, UErrorCode& success)
    : ICU4XCalendar(kind, timeZone, locale, success) {}

ICU4XChineseBasedCalendar::ICU4XChineseBasedCalendar(
    const ICU4XChineseBasedCalendar& other)
    : ICU4XCalendar(other) {}

ICU4XChineseBasedCalendar::~ICU4XChineseBasedCalendar() = default;

////////////////////////////////////////////
// ICU4XCalendar implementation overrides //
////////////////////////////////////////////

bool ICU4XChineseBasedCalendar::hasLeapMonths() const { return true; }

bool ICU4XChineseBasedCalendar::hasMonthCode(MonthCode monthCode) const {
  return monthCode.ordinal() <= 12;
}

bool ICU4XChineseBasedCalendar::requiresFallbackForExtendedYear(
    int32_t year) const {
  // Same limits as in js/src/builtin/temporal/Calendar.cpp.
  return std::abs(year) > 10'000;
}

bool ICU4XChineseBasedCalendar::requiresFallbackForGregorianYear(
    int32_t year) const {
  // Same limits as in js/src/builtin/temporal/Calendar.cpp.
  return std::abs(year) > 10'000;
}

////////////////////////////////////////////
// icu::Calendar implementation overrides //
////////////////////////////////////////////

bool ICU4XChineseBasedCalendar::inTemporalLeapYear(UErrorCode& status) const {
  int32_t days = getActualMaximum(UCAL_DAY_OF_YEAR, status);
  if (U_FAILURE(status)) {
    return false;
  }

  constexpr int32_t maxDaysInMonth = 30;
  constexpr int32_t monthsInNonLeapYear = 12;
  return days > (monthsInNonLeapYear * maxDaysInMonth);
}

int32_t ICU4XChineseBasedCalendar::getRelatedYear(UErrorCode& status) const {
  int32_t year = get(UCAL_EXTENDED_YEAR, status);
  if (U_FAILURE(status)) {
    return 0;
  }
  return year + relatedYearDifference();
}

void ICU4XChineseBasedCalendar::setRelatedYear(int32_t year) {
  set(UCAL_EXTENDED_YEAR, year - relatedYearDifference());
}

void ICU4XChineseBasedCalendar::handleComputeFields(int32_t julianDay,
                                                    UErrorCode& status) {
  int32_t gyear = getGregorianYear();

  // Use the fallback calendar for years outside the range supported by ICU4X.
  if (requiresFallbackForGregorianYear(gyear)) {
    handleComputeFieldsFromFallback(julianDay, status);
    return;
  }

  int32_t gmonth = getGregorianMonth() + 1;
  int32_t gday = getGregorianDayOfMonth();

  MOZ_ASSERT(1 <= gmonth && gmonth <= 12);
  MOZ_ASSERT(1 <= gday && gday <= 31);

  auto date = createICU4XDate(ISODate{gyear, gmonth, gday}, status);
  if (U_FAILURE(status)) {
    return;
  }
  MOZ_ASSERT(date);

  MonthCode monthCode = monthCodeFrom(date.get(), status);
  if (U_FAILURE(status)) {
    return;
  }

  int32_t extendedYear = capi::ICU4XDate_year_in_era(date.get());
  int32_t month = capi::ICU4XDate_ordinal_month(date.get());
  int32_t dayOfMonth = capi::ICU4XDate_day_of_month(date.get());
  int32_t dayOfYear = capi::ICU4XDate_day_of_year(date.get());

  MOZ_ASSERT(1 <= month && month <= 13);
  MOZ_ASSERT(1 <= dayOfMonth && dayOfMonth <= 30);
  MOZ_ASSERT(1 <= dayOfYear && dayOfYear <= (13 * 30));

  // Compute the cycle and year of cycle relative to the Chinese calendar, even
  // when this is the Dangi calendar.
  int32_t chineseExtendedYear =
      extendedYear + relatedYearDifference() - chineseRelatedYearDiff;
  int32_t cycle_year = chineseExtendedYear - 1;
  int32_t cycle = FloorDiv(cycle_year, 60);
  int32_t yearOfCycle = cycle_year - (cycle * 60);

  internalSet(UCAL_ERA, cycle + 1);
  internalSet(UCAL_YEAR, yearOfCycle + 1);
  internalSet(UCAL_EXTENDED_YEAR, extendedYear);
  internalSet(UCAL_MONTH, monthCode.ordinal() - 1);
  internalSet(UCAL_ORDINAL_MONTH, month - 1);
  internalSet(UCAL_IS_LEAP_MONTH, monthCode.isLeapMonth() ? 1 : 0);
  internalSet(UCAL_DAY_OF_MONTH, dayOfMonth);
  internalSet(UCAL_DAY_OF_YEAR, dayOfYear);
}

// Limits table copied from i18n/chnsecal.cpp. Licensed under:
//
// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
static const int32_t CHINESE_CALENDAR_LIMITS[UCAL_FIELD_COUNT][4] = {
    // clang-format off
    // Minimum  Greatest     Least    Maximum
    //           Minimum   Maximum
    {        1,        1,    83333,    83333}, // ERA
    {        1,        1,       60,       60}, // YEAR
    {        0,        0,       11,       11}, // MONTH
    {        1,        1,       50,       55}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,       29,       30}, // DAY_OF_MONTH
    {        1,        1,      353,      385}, // DAY_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DAY_OF_WEEK
    {       -1,       -1,        5,        5}, // DAY_OF_WEEK_IN_MONTH
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // AM_PM
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR_OF_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MINUTE
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // SECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // ZONE_OFFSET
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DST_OFFSET
    { -5000000, -5000000,  5000000,  5000000}, // YEAR_WOY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DOW_LOCAL
    { -5000000, -5000000,  5000000,  5000000}, // EXTENDED_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // JULIAN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECONDS_IN_DAY
    {        0,        0,        1,        1}, // IS_LEAP_MONTH
    {        0,        0,       11,       12}, // ORDINAL_MONTH
    // clang-format on
};

int32_t ICU4XChineseBasedCalendar::handleGetLimit(UCalendarDateFields field,
                                                  ELimitType limitType) const {
  return CHINESE_CALENDAR_LIMITS[field][limitType];
}

// Field resolution table copied from i18n/chnsecal.cpp. Licensed under:
//
// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
const icu::UFieldResolutionTable
    ICU4XChineseBasedCalendar::CHINESE_DATE_PRECEDENCE[] = {
        // clang-format off
  {
    { UCAL_DAY_OF_MONTH, kResolveSTOP },
    { UCAL_WEEK_OF_YEAR, UCAL_DAY_OF_WEEK, kResolveSTOP },
    { UCAL_WEEK_OF_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
    { UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
    { UCAL_WEEK_OF_YEAR, UCAL_DOW_LOCAL, kResolveSTOP },
    { UCAL_WEEK_OF_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
    { UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
    { UCAL_DAY_OF_YEAR, kResolveSTOP },
    { kResolveRemap | UCAL_DAY_OF_MONTH, UCAL_IS_LEAP_MONTH, kResolveSTOP },
    { kResolveSTOP }
  },
  {
    { UCAL_WEEK_OF_YEAR, kResolveSTOP },
    { UCAL_WEEK_OF_MONTH, kResolveSTOP },
    { UCAL_DAY_OF_WEEK_IN_MONTH, kResolveSTOP },
    { kResolveRemap | UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
    { kResolveRemap | UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
    { kResolveSTOP }
  },
  {{kResolveSTOP}}
        // clang-format on
};

const icu::UFieldResolutionTable*
ICU4XChineseBasedCalendar::getFieldResolutionTable() const {
  return CHINESE_DATE_PRECEDENCE;
}

}  // namespace mozilla::intl::calendar
