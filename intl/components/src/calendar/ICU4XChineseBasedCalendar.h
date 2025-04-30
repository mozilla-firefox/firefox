/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ICU4XChineseBasedCalendar_h_
#define intl_components_calendar_ICU4XChineseBasedCalendar_h_

#include "mozilla/intl/calendar/ICU4XCalendar.h"

#include <stdint.h>
#include <string_view>

namespace mozilla::intl::calendar {

/**
 * Abstract base class for Chinese-based calendars.
 *
 * Overrides the same methods as icu::ChineseCalendar to ensure compatible
 * behavior even when using ICU4X as the underlying calendar implementation.
 */
class ICU4XChineseBasedCalendar : public ICU4XCalendar {
 protected:
  ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind kind,
                            const icu::Locale& locale, UErrorCode& success);
  ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind kind,
                            const icu::TimeZone& timeZone,
                            const icu::Locale& locale, UErrorCode& success);
  ICU4XChineseBasedCalendar(const ICU4XChineseBasedCalendar& other);

 public:
  ICU4XChineseBasedCalendar() = delete;
  virtual ~ICU4XChineseBasedCalendar();

 protected:
  bool hasLeapMonths() const override;
  bool hasMonthCode(MonthCode monthCode) const override;
  bool requiresFallbackForExtendedYear(int32_t year) const override;
  bool requiresFallbackForGregorianYear(int32_t year) const override;

  /**
   * Difference to the related Gregorian year.
   */
  virtual int32_t relatedYearDifference() const = 0;

  static constexpr int32_t chineseRelatedYearDiff = -2637;

 public:
  bool inTemporalLeapYear(UErrorCode& status) const override;
  int32_t getRelatedYear(UErrorCode& status) const override;
  void setRelatedYear(int32_t year) override;

 protected:
  void handleComputeFields(int32_t julianDay, UErrorCode& status) override;
  int32_t handleGetLimit(UCalendarDateFields field,
                         ELimitType limitType) const override;
  const icu::UFieldResolutionTable* getFieldResolutionTable() const override;

 private:
  static const icu::UFieldResolutionTable CHINESE_DATE_PRECEDENCE[];
};

}  // namespace mozilla::intl::calendar

#endif
