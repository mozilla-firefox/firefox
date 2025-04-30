/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_DateIntervalFormat_h_
#define intl_components_DateIntervalFormat_h_

#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateTimePart.h"
#include "mozilla/intl/ICU4CGlue.h"
#include "mozilla/intl/ICUError.h"
#include "mozilla/Result.h"
#include "mozilla/Span.h"
#include "mozilla/UniquePtr.h"

#include "unicode/udateintervalformat.h"
#include "unicode/utypes.h"

namespace mozilla::intl {
class Calendar;
class DateTimeFormat;

using AutoFormattedDateInterval =
    AutoFormattedResult<UFormattedDateInterval, udtitvfmt_openResult,
                        udtitvfmt_resultAsValue, udtitvfmt_closeResult>;

/**
 * This component is a Mozilla-focused API for the date range formatting
 * provided by ICU. This DateIntervalFormat class helps to format the range
 * between two date-time values.
 *
 * https://tc39.es/ecma402/#sec-formatdatetimerange
 * https://tc39.es/ecma402/#sec-formatdatetimerangetoparts
 */
class DateIntervalFormat final {
 public:
  /**
   * Create a DateIntervalFormat object from locale, skeleton and time zone.
   * The format of skeleton can be found in [1].
   *
   * Note: Skeleton will be removed in the future.
   *
   * [1]: https://unicode.org/reports/tr35/tr35-dates.html#Date_Format_Patterns
   */
  static Result<UniquePtr<DateIntervalFormat>, ICUError> TryCreate(
      Span<const char> aLocale, Span<const char16_t> aSkeleton,
      Span<const char16_t> aTimeZone);

  ~DateIntervalFormat();

  /**
   * Format a date-time range between two Calendar objects.
   *
   * DateIntervalFormat cannot be changed to use a proleptic Gregorian
   * calendar, so use this method if the start date is before the Gregorian
   * calendar is introduced(October 15, 1582), otherwise use TryFormatDateTime
   * instead.
   *
   * The result will be stored in aFormatted, caller can use
   * AutoFormattedDateInterval::ToSpan() to get the formatted string, or pass
   * the aFormatted to TryFormattedToParts to get the parts vector.
   *
   * aPracticallyEqual will be set to true if the date times of the two
   * calendars are equal.
   */
  ICUResult TryFormatCalendar(const Calendar& aStart, const Calendar& aEnd,
                              AutoFormattedDateInterval& aFormatted,
                              bool* aPracticallyEqual) const;

  /**
   * Format a date-time range between two Unix epoch times in milliseconds.
   *
   * The result will be stored in aFormatted, caller can use
   * AutoFormattedDateInterval::ToSpan() to get the formatted string, or pass
   * the aFormatted to TryFormattedToParts to get the parts vector.
   *
   * aPracticallyEqual will be set to true if the date times of the two
   * Unix epoch times are equal.
   */
  ICUResult TryFormatDateTime(double aStart, double aEnd,
                              AutoFormattedDateInterval& aFormatted,
                              bool* aPracticallyEqual) const;

  /**
   * Format a date-time range between two Unix epoch times in milliseconds.
   *
   * The result will be stored in aFormatted, caller can use
   * AutoFormattedDateInterval::ToSpan() to get the formatted string, or pass
   * the aFormatted to TryFormattedToParts to get the parts vector.
   *
   * aPracticallyEqual will be set to true if the date times of the two
   * Unix epoch times are equal.
   */
  ICUResult TryFormatDateTime(double aStart, double aEnd,
                              const DateTimeFormat* aDateTimeFormat,
                              AutoFormattedDateInterval& aFormatted,
                              bool* aPracticallyEqual) const;

  /**
   *  Convert the formatted DateIntervalFormat into several parts.
   *
   *  The caller get the formatted result from either TryFormatCalendar, or
   *  TryFormatDateTime methods, and instantiate the DateTimePartVector. This
   *  method will generate the parts and insert them into the vector.
   *
   *  See:
   *  https://tc39.es/ecma402/#sec-formatdatetimerangetoparts
   */
  ICUResult TryFormattedToParts(const AutoFormattedDateInterval& aFormatted,
                                DateTimePartVector& aParts) const;

 private:
  DateIntervalFormat() = delete;
  explicit DateIntervalFormat(UDateIntervalFormat* aDif)
      : mDateIntervalFormat(aDif) {}
  DateIntervalFormat(const DateIntervalFormat&) = delete;
  DateIntervalFormat& operator=(const DateIntervalFormat&) = delete;

  ICUPointer<UDateIntervalFormat> mDateIntervalFormat =
      ICUPointer<UDateIntervalFormat>(nullptr);
};
}  // namespace mozilla::intl

#endif
