/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/Try.h"

#include "DateTimeFormatUtils.h"
#include "mozilla/intl/ICU4CGlue.h"

#include <cstring>

#if !MOZ_SYSTEM_ICU
#  include "calendar/ICU4XChineseCalendar.h"
#  include "calendar/ICU4XDangiCalendar.h"
#  include "unicode/datefmt.h"
#  include "unicode/gregocal.h"
#endif

namespace mozilla::intl {

DateTimePartType ConvertUFormatFieldToPartType(UDateFormatField fieldName) {
  // See intl/icu/source/i18n/unicode/udat.h for a detailed field list.  This
  // switch is deliberately exhaustive: cases might have to be added/removed
  // if this code is compiled with a different ICU with more
  // UDateFormatField enum initializers.  Please guard such cases with
  // appropriate ICU version-testing #ifdefs, should cross-version divergence
  // occur.
  switch (fieldName) {
    case UDAT_ERA_FIELD:
      return DateTimePartType::Era;

    case UDAT_YEAR_FIELD:
    case UDAT_YEAR_WOY_FIELD:
    case UDAT_EXTENDED_YEAR_FIELD:
      return DateTimePartType::Year;

    case UDAT_YEAR_NAME_FIELD:
      return DateTimePartType::YearName;

    case UDAT_MONTH_FIELD:
    case UDAT_STANDALONE_MONTH_FIELD:
      return DateTimePartType::Month;

    case UDAT_DATE_FIELD:
    case UDAT_JULIAN_DAY_FIELD:
      return DateTimePartType::Day;

    case UDAT_HOUR_OF_DAY1_FIELD:
    case UDAT_HOUR_OF_DAY0_FIELD:
    case UDAT_HOUR1_FIELD:
    case UDAT_HOUR0_FIELD:
      return DateTimePartType::Hour;

    case UDAT_MINUTE_FIELD:
      return DateTimePartType::Minute;

    case UDAT_SECOND_FIELD:
      return DateTimePartType::Second;

    case UDAT_DAY_OF_WEEK_FIELD:
    case UDAT_STANDALONE_DAY_FIELD:
    case UDAT_DOW_LOCAL_FIELD:
    case UDAT_DAY_OF_WEEK_IN_MONTH_FIELD:
      return DateTimePartType::Weekday;

    case UDAT_AM_PM_FIELD:
    case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:
      return DateTimePartType::DayPeriod;

    case UDAT_TIMEZONE_FIELD:
    case UDAT_TIMEZONE_GENERIC_FIELD:
    case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD:
      return DateTimePartType::TimeZoneName;

    case UDAT_FRACTIONAL_SECOND_FIELD:
      return DateTimePartType::FractionalSecondDigits;

#ifndef U_HIDE_INTERNAL_API
    case UDAT_RELATED_YEAR_FIELD:
      return DateTimePartType::RelatedYear;
#endif

    case UDAT_DAY_OF_YEAR_FIELD:
    case UDAT_WEEK_OF_YEAR_FIELD:
    case UDAT_WEEK_OF_MONTH_FIELD:
    case UDAT_MILLISECONDS_IN_DAY_FIELD:
    case UDAT_TIMEZONE_RFC_FIELD:
    case UDAT_QUARTER_FIELD:
    case UDAT_STANDALONE_QUARTER_FIELD:
    case UDAT_TIMEZONE_SPECIAL_FIELD:
    case UDAT_TIMEZONE_ISO_FIELD:
    case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
    case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
#ifndef U_HIDE_INTERNAL_API
    case UDAT_TIME_SEPARATOR_FIELD:
#endif
      // These fields are all unsupported.
      return DateTimePartType::Unknown;

#ifndef U_HIDE_DEPRECATED_API
    case UDAT_FIELD_COUNT:
      MOZ_ASSERT_UNREACHABLE(
          "format field sentinel value returned by "
          "iterator!");
#endif
  }

  MOZ_ASSERT_UNREACHABLE(
      "unenumerated, undocumented format field returned "
      "by iterator");
  return DateTimePartType::Unknown;
}

// Start of ECMAScript time.
static constexpr double StartOfTime = -8.64e15;

#if !MOZ_SYSTEM_ICU
static bool IsGregorianLikeCalendar(const char* type) {
  return std::strcmp(type, "gregorian") == 0 ||
         std::strcmp(type, "iso8601") == 0 ||
         std::strcmp(type, "buddhist") == 0 ||
         std::strcmp(type, "japanese") == 0 || std::strcmp(type, "roc") == 0;
}

/**
 * Set the start time of the Gregorian calendar. This is useful for
 * ensuring the consistent use of a proleptic Gregorian calendar for ECMA-402.
 * https://en.wikipedia.org/wiki/Proleptic_Gregorian_calendar
 */
static Result<Ok, ICUError> SetGregorianChangeDate(
    icu::GregorianCalendar* gregorian) {
  UErrorCode status = U_ZERO_ERROR;
  gregorian->setGregorianChange(StartOfTime, status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  return Ok{};
}

static bool IsCalendarReplacementSupported(const char* type) {
  return std::strcmp(type, "chinese") == 0 || std::strcmp(type, "dangi") == 0;
}

static Result<UniquePtr<icu::Calendar>, ICUError> CreateCalendarReplacement(
    const icu::Calendar* calendar) {
  const char* type = calendar->getType();
  MOZ_ASSERT(IsCalendarReplacementSupported(type));

  UErrorCode status = U_ZERO_ERROR;
  icu::Locale locale = calendar->getLocale(ULOC_ACTUAL_LOCALE, status);
  locale.setKeywordValue("calendar", type, status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  const icu::TimeZone& timeZone = calendar->getTimeZone();

  UniquePtr<icu::Calendar> replacement = nullptr;
  if (std::strcmp(type, "chinese") == 0) {
    replacement.reset(
        new calendar::ICU4XChineseCalendar(timeZone, locale, status));
  } else {
    MOZ_ASSERT(std::strcmp(type, "dangi") == 0);
    replacement.reset(
        new calendar::ICU4XDangiCalendar(timeZone, locale, status));
  }
  if (replacement == nullptr) {
    return Err(ICUError::OutOfMemory);
  }
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return replacement;
}
#endif

Result<Ok, ICUError> ApplyCalendarOverride(UDateFormat* aDateFormat) {
#if !MOZ_SYSTEM_ICU
  icu::DateFormat* df = reinterpret_cast<icu::DateFormat*>(aDateFormat);
  const icu::Calendar* calendar = df->getCalendar();

  const char* type = calendar->getType();

  if (IsGregorianLikeCalendar(type)) {
    auto* gregorian = static_cast<const icu::GregorianCalendar*>(calendar);
    MOZ_TRY(
        SetGregorianChangeDate(const_cast<icu::GregorianCalendar*>(gregorian)));
  }
  else if (IsCalendarReplacementSupported(type)) {
    auto replacement = CreateCalendarReplacement(calendar);
    if (replacement.isErr()) {
      return replacement.propagateErr();
    }
    df->adoptCalendar(replacement.unwrap().release());
  }
#else
  UErrorCode status = U_ZERO_ERROR;
  UCalendar* cal = const_cast<UCalendar*>(udat_getCalendar(aDateFormat));
  ucal_setGregorianChange(cal, StartOfTime, &status);
  // An error here means the calendar is not Gregorian, and can be ignored.
#endif

  return Ok{};
}

#if !MOZ_SYSTEM_ICU
Result<UniquePtr<icu::Calendar>, ICUError> CreateCalendarOverride(
    const icu::Calendar* calendar) {
  const char* type = calendar->getType();

  if (IsGregorianLikeCalendar(type)) {
    UniquePtr<icu::GregorianCalendar> gregorian(
        static_cast<const icu::GregorianCalendar*>(calendar)->clone());
    if (!gregorian) {
      return Err(ICUError::OutOfMemory);
    }

    MOZ_TRY(SetGregorianChangeDate(gregorian.get()));

    return UniquePtr<icu::Calendar>{gregorian.release()};
  }

  if (IsCalendarReplacementSupported(type)) {
    return CreateCalendarReplacement(calendar);
  }

  return UniquePtr<icu::Calendar>{};
}
#endif

}  // namespace mozilla::intl
