/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ICU4XCalendar_h_
#define intl_components_calendar_ICU4XCalendar_h_

#include "mozilla/intl/calendar/ICU4XUniquePtr.h"
#include "mozilla/intl/calendar/ISODate.h"
#include "mozilla/intl/calendar/MonthCode.h"

#include <memory>
#include <mutex>
#include <stdint.h>
#include <string_view>

#include "unicode/calendar.h"
#include "unicode/locid.h"
#include "unicode/timezone.h"
#include "unicode/utypes.h"

#include "ICU4XAnyCalendarKind.h"

namespace mozilla::intl::calendar {

/**
 * Abstract class to implement icu::Calendar using ICU4X.
 */
class ICU4XCalendar : public icu::Calendar {
  mutable UniqueICU4XCalendar calendar_{};
  mutable std::unique_ptr<icu::Calendar> fallback_{};
  capi::ICU4XAnyCalendarKind kind_;

 protected:
  ICU4XCalendar(capi::ICU4XAnyCalendarKind kind, const icu::Locale& locale,
                UErrorCode& success);
  ICU4XCalendar(capi::ICU4XAnyCalendarKind kind, const icu::TimeZone& timeZone,
                const icu::Locale& locale, UErrorCode& success);
  ICU4XCalendar(const ICU4XCalendar& other);

  /**
   * Get or create the underlying ICU4X calendar.
   */
  capi::ICU4XCalendar* getICU4XCalendar(UErrorCode& status) const;

  /**
   * Get or create the ICU4C fallback calendar implementation.
   */
  icu::Calendar* getFallbackCalendar(UErrorCode& status) const;

 protected:
  /**
   * Return the ICU4X era name for the given extended year.
   */
  virtual std::string_view eraName(int32_t extendedYear) const = 0;

  /**
   * Return true if this calendar contains any leap months.
   */
  virtual bool hasLeapMonths() const = 0;

  /**
   * Return true if this calendar contains the requested month code.
   */
  virtual bool hasMonthCode(MonthCode monthCode) const = 0;

  /**
   * Subclasses can request to use the ICU4C fallback calendar.
   *
   * Can be removed when <https://github.com/unicode-org/icu4x/issues/4917> is
   * fixed.
   */
  virtual bool requiresFallbackForExtendedYear(int32_t year) const = 0;
  virtual bool requiresFallbackForGregorianYear(int32_t year) const = 0;

 protected:
  static constexpr int32_t kEpochStartAsJulianDay =
      2440588;  // January 1, 1970 (Gregorian)

  /**
   * Return the month code of |date|.
   */
  static MonthCode monthCodeFrom(const capi::ICU4XDate* date,
                                 UErrorCode& status);

  /**
   * Create a new ICU4X date object from an ISO date.
   */
  UniqueICU4XDate createICU4XDate(const ISODate& date,
                                  UErrorCode& status) const;

  /**
   * Create a new ICU4X date object from a calendar date.
   */
  UniqueICU4XDate createICU4XDate(const CalendarDate& date,
                                  UErrorCode& status) const;

 public:
  ICU4XCalendar() = delete;
  virtual ~ICU4XCalendar();

  const char* getTemporalMonthCode(UErrorCode& status) const override;
  void setTemporalMonthCode(const char* code, UErrorCode& status) override;

  void add(UCalendarDateFields field, int32_t amount,
           UErrorCode& status) override;
  void add(EDateFields field, int32_t amount, UErrorCode& status) override;
  void roll(UCalendarDateFields field, int32_t amount,
            UErrorCode& status) override;
  void roll(EDateFields field, int32_t amount, UErrorCode& status) override;

 protected:
  int32_t internalGetMonth(int32_t defaultValue,
                           UErrorCode& status) const override;
  int32_t internalGetMonth(UErrorCode& status) const override;

  int64_t handleComputeMonthStart(int32_t extendedYear, int32_t month,
                                  UBool useMonth,
                                  UErrorCode& status) const override;
  int32_t handleGetMonthLength(int32_t extendedYear, int32_t month,
                               UErrorCode& status) const override;
  int32_t handleGetYearLength(int32_t extendedYear,
                              UErrorCode& status) const override;

  int32_t handleGetExtendedYear(UErrorCode& status) override;

 protected:
  /**
   * handleComputeFields implementation using the ICU4C fallback calendar.
   */
  void handleComputeFieldsFromFallback(int32_t julianDay, UErrorCode& status);
};

/**
 * `IMPL_SYSTEM_DEFAULT_CENTURY` is internal to "i18n/gregoimp.h", so we have
 * to provider our own helper class to implement default centuries.
 */
template <class Calendar, class Locale>
class SystemDefaultCentury {
  mutable UDate start_ = DBL_MIN;
  mutable int32_t startYear_ = -1;
  mutable std::once_flag init_{};

  void initialize() const {
    UErrorCode status = U_ZERO_ERROR;
    Calendar calendar(Locale::identifier, status);
    if (U_FAILURE(status)) {
      return;
    }
    calendar.setTime(icu::Calendar::getNow(), status);
    calendar.add(UCAL_EXTENDED_YEAR, -80, status);
    start_ = calendar.getTime(status);
    startYear_ = calendar.get(UCAL_YEAR, status);
  }

 public:
  UDate start() const {
    std::call_once(init_, [this] { initialize(); });
    return start_;
  }
  int32_t startYear() const {
    std::call_once(init_, [this] { initialize(); });
    return startYear_;
  }
};

}  // namespace mozilla::intl::calendar

#endif
