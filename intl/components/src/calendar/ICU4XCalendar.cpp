/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/calendar/ICU4XCalendar.h"

#include "mozilla/Assertions.h"
#include "mozilla/TextUtils.h"
#include "mozilla/intl/ICU4XGeckoDataProvider.h"

#include <cstring>
#include <mutex>
#include <stdint.h>
#include <type_traits>

#include "unicode/timezone.h"

#include "diplomat_runtime.h"
#include "ICU4XDataProvider.h"
#include "ICU4XError.h"

namespace mozilla::intl::calendar {

// Copied from js/src/util/Text.h
template <typename CharT>
static constexpr uint8_t AsciiDigitToNumber(CharT c) {
  using UnsignedCharT = std::make_unsigned_t<CharT>;
  auto uc = static_cast<UnsignedCharT>(c);
  return uc - '0';
}

static UniqueICU4XCalendar CreateICU4XCalendar(
    capi::ICU4XAnyCalendarKind kind) {
  auto result = capi::ICU4XCalendar_create_for_kind(GetDataProvider(), kind);
  if (!result.is_ok) {
    return nullptr;
  }
  return UniqueICU4XCalendar{result.ok};
}

static UniqueICU4XDate CreateICU4XDate(const ISODate& date,
                                       const capi::ICU4XCalendar* calendar) {
  auto result = capi::ICU4XDate_create_from_iso_in_calendar(
      date.year, date.month, date.day, calendar);
  if (!result.is_ok) {
    return nullptr;
  }
  return UniqueICU4XDate{result.ok};
}

static UniqueICU4XDate CreateDateFromCodes(const capi::ICU4XCalendar* calendar,
                                           std::string_view era,
                                           int32_t eraYear, MonthCode monthCode,
                                           int32_t day) {
  auto monthCodeView = std::string_view{monthCode};
  auto date = capi::ICU4XDate_create_from_codes_in_calendar(
      era.data(), era.length(), eraYear, monthCodeView.data(),
      monthCodeView.length(), day, calendar);
  if (date.is_ok) {
    return UniqueICU4XDate{date.ok};
  }
  return nullptr;
}

// Copied from js/src/builtin/temporal/Calendar.cpp
static UniqueICU4XDate CreateDateFrom(const capi::ICU4XCalendar* calendar,
                                      std::string_view era, int32_t eraYear,
                                      int32_t month, int32_t day) {
  MOZ_ASSERT(1 <= month && month <= 13);

  // Create date with month number replaced by month-code.
  auto monthCode = MonthCode{std::min(month, 12)};
  auto date = CreateDateFromCodes(calendar, era, eraYear, monthCode, day);
  if (!date) {
    return nullptr;
  }

  // If the ordinal month of |date| matches the input month, no additional
  // changes are necessary and we can directly return |date|.
  int32_t ordinal = capi::ICU4XDate_ordinal_month(date.get());
  if (ordinal == month) {
    return date;
  }

  // Otherwise we need to handle three cases:
  // 1. The input year contains a leap month and we need to adjust the
  //    month-code.
  // 2. The thirteenth month of a year without leap months was requested.
  // 3. The thirteenth month of a year with leap months was requested.
  if (ordinal > month) {
    MOZ_ASSERT(1 < month && month <= 12);

    // This case can only happen in leap years.
    MOZ_ASSERT(capi::ICU4XDate_months_in_year(date.get()) == 13);

    // Leap months can occur after any month in the Chinese calendar.
    //
    // Example when the fourth month is a leap month between M03 and M04.
    //
    // Month code:     M01  M02  M03  M03L  M04  M05  M06 ...
    // Ordinal month:  1    2    3    4     5    6    7

    // The month can be off by exactly one.
    MOZ_ASSERT((ordinal - month) == 1);

    // First try the case when the previous month isn't a leap month. This
    // case can only occur when |month > 2|, because otherwise we know that
    // "M01L" is the correct answer.
    if (month > 2) {
      auto previousMonthCode = MonthCode{month - 1};
      date =
          CreateDateFromCodes(calendar, era, eraYear, previousMonthCode, day);
      if (!date) {
        return nullptr;
      }
      int32_t ordinal = capi::ICU4XDate_ordinal_month(date.get());
      if (ordinal == month) {
        return date;
      }
    }

    // Fall-through when the previous month is a leap month.
  } else {
    MOZ_ASSERT(month == 13);
    MOZ_ASSERT(ordinal == 12);

    // Years with leap months contain thirteen months.
    if (capi::ICU4XDate_months_in_year(date.get()) != 13) {
      return nullptr;
    }

    // Fall-through to return leap month "M12L" at the end of the year.
  }

  // Finally handle the case when the previous month is a leap month.
  auto leapMonthCode = MonthCode{month - 1, /* isLeapMonth= */ true};
  return CreateDateFromCodes(calendar, era, eraYear, leapMonthCode, day);
}

static ISODate ToISODate(const capi::ICU4XDate* date) {
  UniqueICU4XIsoDate isoDate{capi::ICU4XDate_to_iso(date)};

  int32_t isoYear = capi::ICU4XIsoDate_year(isoDate.get());
  int32_t isoMonth = capi::ICU4XIsoDate_month(isoDate.get());
  int32_t isoDay = capi::ICU4XIsoDate_day_of_month(isoDate.get());

  return {isoYear, isoMonth, isoDay};
}

////////////////////////////////////////////////////////////////////////////////

ICU4XCalendar::ICU4XCalendar(capi::ICU4XAnyCalendarKind kind,
                             const icu::Locale& locale, UErrorCode& success)
    : icu::Calendar(icu::TimeZone::forLocaleOrDefault(locale), locale, success),
      kind_(kind) {}

ICU4XCalendar::ICU4XCalendar(capi::ICU4XAnyCalendarKind kind,
                             const icu::TimeZone& timeZone,
                             const icu::Locale& locale, UErrorCode& success)
    : icu::Calendar(timeZone, locale, success), kind_(kind) {}

ICU4XCalendar::ICU4XCalendar(const ICU4XCalendar& other)
    : icu::Calendar(other), kind_(other.kind_) {}

ICU4XCalendar::~ICU4XCalendar() = default;

/**
 * Get or create the underlying ICU4X calendar.
 */
capi::ICU4XCalendar* ICU4XCalendar::getICU4XCalendar(UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return nullptr;
  }
  if (!calendar_) {
    auto result = CreateICU4XCalendar(kind_);
    if (!result) {
      status = U_INTERNAL_PROGRAM_ERROR;
      return nullptr;
    }
    calendar_ = std::move(result);
  }
  return calendar_.get();
}

/**
 * Get or create the fallback ICU4C calendar. Used for dates outside the range
 * supported by ICU4X.
 */
icu::Calendar* ICU4XCalendar::getFallbackCalendar(UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return nullptr;
  }
  if (!fallback_) {
    icu::Locale locale = getLocale(ULOC_ACTUAL_LOCALE, status);
    locale.setKeywordValue("calendar", getType(), status);
    fallback_.reset(
        icu::Calendar::createInstance(getTimeZone(), locale, status));
  }
  return fallback_.get();
}

UniqueICU4XDate ICU4XCalendar::createICU4XDate(const ISODate& date,
                                               UErrorCode& status) const {
  MOZ_ASSERT(U_SUCCESS(status));

  auto* calendar = getICU4XCalendar(status);
  if (U_FAILURE(status)) {
    return nullptr;
  }

  auto dt = CreateICU4XDate(date, calendar);
  if (!dt) {
    status = U_INTERNAL_PROGRAM_ERROR;
  }
  return dt;
}

UniqueICU4XDate ICU4XCalendar::createICU4XDate(const CalendarDate& date,
                                               UErrorCode& status) const {
  MOZ_ASSERT(U_SUCCESS(status));

  auto* calendar = getICU4XCalendar(status);
  if (U_FAILURE(status)) {
    return nullptr;
  }

  auto era = eraName(date.year);

  auto dt =
      CreateDateFromCodes(calendar, era, date.year, date.monthCode, date.day);
  if (!dt) {
    status = U_INTERNAL_PROGRAM_ERROR;
  }
  return dt;
}

MonthCode ICU4XCalendar::monthCodeFrom(const capi::ICU4XDate* date,
                                       UErrorCode& status) {
  MOZ_ASSERT(U_SUCCESS(status));

  // Storage for the largest valid month code and the terminating NUL-character.
  char buf[4 + 1] = {};
  auto writable = capi::diplomat_simple_writeable(buf, std::size(buf));

  if (!capi::ICU4XDate_month_code(date, &writable).is_ok) {
    status = U_INTERNAL_PROGRAM_ERROR;
    return {};
  }

  auto view = std::string_view{writable.buf, writable.len};

  MOZ_ASSERT(view.length() >= 3);
  MOZ_ASSERT(view[0] == 'M');
  MOZ_ASSERT(mozilla::IsAsciiDigit(view[1]));
  MOZ_ASSERT(mozilla::IsAsciiDigit(view[2]));
  MOZ_ASSERT_IF(view.length() > 3, view[3] == 'L');

  int32_t ordinal =
      AsciiDigitToNumber(view[1]) * 10 + AsciiDigitToNumber(view[2]);
  bool isLeapMonth = view.length() > 3;

  return MonthCode{ordinal, isLeapMonth};
}

////////////////////////////////////////////
// icu::Calendar implementation overrides //
////////////////////////////////////////////

const char* ICU4XCalendar::getTemporalMonthCode(UErrorCode& status) const {
  int32_t month = get(UCAL_MONTH, status);
  int32_t isLeapMonth = get(UCAL_IS_LEAP_MONTH, status);
  if (U_FAILURE(status)) {
    return nullptr;
  }

  static const char* MonthCodes[] = {
      // Non-leap months.
      "M01",
      "M02",
      "M03",
      "M04",
      "M05",
      "M06",
      "M07",
      "M08",
      "M09",
      "M10",
      "M11",
      "M12",
      "M13",

      // Leap months. (Note: There's no thirteenth leap month.)
      "M01L",
      "M02L",
      "M03L",
      "M04L",
      "M05L",
      "M06L",
      "M07L",
      "M08L",
      "M09L",
      "M10L",
      "M11L",
      "M12L",
  };

  size_t index = month + (isLeapMonth ? 12 : 0);
  if (index >= std::size(MonthCodes)) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return nullptr;
  }
  return MonthCodes[index];
}

void ICU4XCalendar::setTemporalMonthCode(const char* code, UErrorCode& status) {
  if (U_FAILURE(status)) {
    return;
  }

  size_t len = std::strlen(code);
  if (len < 3 || len > 4 || code[0] != 'M' || !IsAsciiDigit(code[1]) ||
      !IsAsciiDigit(code[2]) || (len == 4 && code[3] != 'L')) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return;
  }

  int32_t month =
      AsciiDigitToNumber(code[1]) * 10 + AsciiDigitToNumber(code[2]);
  bool isLeapMonth = len == 4;

  if (month < 1 || month > 13 || (month == 13 && isLeapMonth)) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return;
  }

  // Check if this calendar supports the requested month code.
  auto monthCode = MonthCode{month, isLeapMonth};
  if (!hasMonthCode(monthCode)) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return;
  }

  set(UCAL_MONTH, monthCode.ordinal() - 1);
  set(UCAL_IS_LEAP_MONTH, int32_t(monthCode.isLeapMonth()));
}

int32_t ICU4XCalendar::internalGetMonth(int32_t defaultValue,
                                        UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return 0;
  }
  if (resolveFields(kMonthPrecedence) == UCAL_MONTH) {
    return internalGet(UCAL_MONTH, defaultValue);
  }
  if (!hasLeapMonths()) {
    return internalGet(UCAL_ORDINAL_MONTH);
  }
  return internalGetMonth(status);
}

/**
 * Return the current month, possibly by computing it from |UCAL_ORDINAL_MONTH|.
 */
int32_t ICU4XCalendar::internalGetMonth(UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return 0;
  }
  if (resolveFields(kMonthPrecedence) == UCAL_MONTH) {
    return internalGet(UCAL_MONTH);
  }
  if (!hasLeapMonths()) {
    return internalGet(UCAL_ORDINAL_MONTH);
  }

  int32_t extendedYear = internalGet(UCAL_EXTENDED_YEAR);
  int32_t ordinalMonth = internalGet(UCAL_ORDINAL_MONTH);

  int32_t month;
  int32_t isLeapMonth;
  if (requiresFallbackForExtendedYear(extendedYear)) {
    // Use the fallback calendar for years outside the range supported by ICU4X.
    auto* fallback = getFallbackCalendar(status);
    if (U_FAILURE(status)) {
      return 0;
    }
    fallback->clear();
    fallback->set(UCAL_EXTENDED_YEAR, extendedYear);
    fallback->set(UCAL_ORDINAL_MONTH, ordinalMonth);
    fallback->set(UCAL_DAY_OF_MONTH, 1);

    month = fallback->get(UCAL_MONTH, status);
    isLeapMonth = fallback->get(UCAL_IS_LEAP_MONTH, status);
    if (U_FAILURE(status)) {
      return 0;
    }
  } else {
    auto* cal = getICU4XCalendar(status);
    if (U_FAILURE(status)) {
      return 0;
    }

    UniqueICU4XDate date = CreateDateFrom(cal, eraName(extendedYear),
                                          extendedYear, ordinalMonth + 1, 1);
    if (!date) {
      status = U_INTERNAL_PROGRAM_ERROR;
      return 0;
    }

    MonthCode monthCode = monthCodeFrom(date.get(), status);
    if (U_FAILURE(status)) {
      return 0;
    }

    month = monthCode.ordinal() - 1;
    isLeapMonth = monthCode.isLeapMonth();
  }

  auto* nonConstThis = const_cast<ICU4XCalendar*>(this);
  nonConstThis->internalSet(UCAL_IS_LEAP_MONTH, isLeapMonth);
  nonConstThis->internalSet(UCAL_MONTH, month);

  return month;
}

void ICU4XCalendar::add(UCalendarDateFields field, int32_t amount,
                        UErrorCode& status) {
  switch (field) {
    case UCAL_MONTH:
    case UCAL_ORDINAL_MONTH:
      if (amount != 0) {
        // Our implementation doesn't yet support this action.
        status = U_ILLEGAL_ARGUMENT_ERROR;
        break;
      }
      break;
    default:
      Calendar::add(field, amount, status);
      break;
  }
}

void ICU4XCalendar::add(EDateFields field, int32_t amount, UErrorCode& status) {
  add(static_cast<UCalendarDateFields>(field), amount, status);
}

void ICU4XCalendar::roll(UCalendarDateFields field, int32_t amount,
                         UErrorCode& status) {
  switch (field) {
    case UCAL_MONTH:
    case UCAL_ORDINAL_MONTH:
      if (amount != 0) {
        // Our implementation doesn't yet support this action.
        status = U_ILLEGAL_ARGUMENT_ERROR;
        break;
      }
      break;
    default:
      Calendar::roll(field, amount, status);
      break;
  }
}

void ICU4XCalendar::roll(EDateFields field, int32_t amount,
                         UErrorCode& status) {
  roll(static_cast<UCalendarDateFields>(field), amount, status);
}

int32_t ICU4XCalendar::handleGetExtendedYear(UErrorCode& status) {
  if (U_FAILURE(status)) {
    return 0;
  }
  if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR) {
    return internalGet(UCAL_EXTENDED_YEAR, 1);
  }

  // We don't yet support the case when UCAL_YEAR is newer.
  status = U_UNSUPPORTED_ERROR;
  return 0;
}

int32_t ICU4XCalendar::handleGetYearLength(int32_t extendedYear,
                                           UErrorCode& status) const {
  // Use the (slower) default implementation for years outside the range
  // supported by ICU4X.
  if (requiresFallbackForExtendedYear(extendedYear)) {
    return icu::Calendar::handleGetYearLength(extendedYear, status);
  }

  auto* cal = getICU4XCalendar(status);
  if (U_FAILURE(status)) {
    return 0;
  }

  UniqueICU4XDate date =
      CreateDateFrom(cal, eraName(extendedYear), extendedYear, 1, 1);
  if (!date) {
    status = U_INTERNAL_PROGRAM_ERROR;
    return 0;
  }
  return capi::ICU4XDate_days_in_year(date.get());
}

/**
 * Return the number of days in a month.
 */
int32_t ICU4XCalendar::handleGetMonthLength(int32_t extendedYear, int32_t month,
                                            UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return 0;
  }

  // ICU4C supports wrap around. We don't support this case.
  if (month < 0 || month > 11) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return 0;
  }

  // Use the fallback calendar for years outside the range supported by ICU4X.
  if (requiresFallbackForExtendedYear(extendedYear)) {
    auto* fallback = getFallbackCalendar(status);
    if (U_FAILURE(status)) {
      return 0;
    }
    fallback->clear();
    fallback->set(UCAL_EXTENDED_YEAR, extendedYear);
    fallback->set(UCAL_MONTH, month);
    fallback->set(UCAL_DAY_OF_MONTH, 1);

    return fallback->getActualMaximum(UCAL_DAY_OF_MONTH, status);
  }

  auto* cal = getICU4XCalendar(status);
  if (U_FAILURE(status)) {
    return 0;
  }

  bool isLeapMonth = internalGet(UCAL_IS_LEAP_MONTH) != 0;
  auto monthCode = MonthCode{month + 1, isLeapMonth};
  UniqueICU4XDate date = CreateDateFromCodes(cal, eraName(extendedYear),
                                             extendedYear, monthCode, 1);
  if (!date) {
    status = U_INTERNAL_PROGRAM_ERROR;
    return 0;
  }

  return capi::ICU4XDate_days_in_month(date.get());
}

/**
 * Return the start of the month as a Julian date.
 */
int64_t ICU4XCalendar::handleComputeMonthStart(int32_t extendedYear,
                                               int32_t month, UBool useMonth,
                                               UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return 0;
  }

  // ICU4C supports wrap around. We don't support this case.
  if (month < 0 || month > 11) {
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return 0;
  }

  // Use the fallback calendar for years outside the range supported by ICU4X.
  if (requiresFallbackForExtendedYear(extendedYear)) {
    auto* fallback = getFallbackCalendar(status);
    if (U_FAILURE(status)) {
      return 0;
    }
    fallback->clear();
    fallback->set(UCAL_EXTENDED_YEAR, extendedYear);
    if (useMonth) {
      fallback->set(UCAL_MONTH, month);
      fallback->set(UCAL_IS_LEAP_MONTH, internalGet(UCAL_IS_LEAP_MONTH));
    } else {
      fallback->set(UCAL_ORDINAL_MONTH, month);
    }
    fallback->set(UCAL_DAY_OF_MONTH, 1);

    int32_t newMoon = fallback->get(UCAL_JULIAN_DAY, status);
    if (U_FAILURE(status)) {
      return 0;
    }
    return newMoon - 1;
  }

  auto* cal = getICU4XCalendar(status);
  if (U_FAILURE(status)) {
    return 0;
  }

  UniqueICU4XDate date{};
  if (useMonth) {
    bool isLeapMonth = internalGet(UCAL_IS_LEAP_MONTH) != 0;
    auto monthCode = MonthCode{month + 1, isLeapMonth};
    date = CreateDateFromCodes(cal, eraName(extendedYear), extendedYear,
                               monthCode, 1);
  } else {
    date =
        CreateDateFrom(cal, eraName(extendedYear), extendedYear, month + 1, 1);
  }
  if (!date) {
    status = U_INTERNAL_PROGRAM_ERROR;
    return 0;
  }

  auto isoDate = ToISODate(date.get());
  int32_t newMoon = MakeDay(isoDate);

  return (newMoon - 1) + kEpochStartAsJulianDay;
}

/**
 * Default implementation of handleComputeFields when using the fallback
 * calendar.
 */
void ICU4XCalendar::handleComputeFieldsFromFallback(int32_t julianDay,
                                                    UErrorCode& status) {
  auto* fallback = getFallbackCalendar(status);
  if (U_FAILURE(status)) {
    return;
  }
  fallback->clear();
  fallback->set(UCAL_JULIAN_DAY, julianDay);

  internalSet(UCAL_ERA, fallback->get(UCAL_ERA, status));
  internalSet(UCAL_YEAR, fallback->get(UCAL_YEAR, status));
  internalSet(UCAL_EXTENDED_YEAR, fallback->get(UCAL_EXTENDED_YEAR, status));
  internalSet(UCAL_MONTH, fallback->get(UCAL_MONTH, status));
  internalSet(UCAL_ORDINAL_MONTH, fallback->get(UCAL_ORDINAL_MONTH, status));
  internalSet(UCAL_IS_LEAP_MONTH, fallback->get(UCAL_IS_LEAP_MONTH, status));
  internalSet(UCAL_DAY_OF_MONTH, fallback->get(UCAL_DAY_OF_MONTH, status));
  internalSet(UCAL_DAY_OF_YEAR, fallback->get(UCAL_DAY_OF_YEAR, status));
}

}  // namespace mozilla::intl::calendar
