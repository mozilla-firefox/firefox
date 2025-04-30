/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ICU4XChineseCalendar_h_
#define intl_components_calendar_ICU4XChineseCalendar_h_

#include "mozilla/intl/calendar/ICU4XChineseBasedCalendar.h"

#include <stdint.h>
#include <string_view>

#include "unicode/uobject.h"

namespace mozilla::intl::calendar {

/**
 * Chinese calendar implementation.
 *
 * Overrides the same methods as icu::ChineseCalendar to ensure compatible
 * behavior even when using ICU4X as the underlying calendar implementation.
 */
class ICU4XChineseCalendar : public ICU4XChineseBasedCalendar {
 public:
  ICU4XChineseCalendar() = delete;
  ICU4XChineseCalendar(const icu::Locale& locale, UErrorCode& success);
  ICU4XChineseCalendar(const icu::TimeZone& timeZone, const icu::Locale& locale,
                       UErrorCode& success);
  ICU4XChineseCalendar(const ICU4XChineseCalendar& other);

  virtual ~ICU4XChineseCalendar();

  ICU4XChineseCalendar* clone() const override;

  const char* getType() const override;

 protected:
  std::string_view eraName(int32_t extendedYear) const override;

  int32_t relatedYearDifference() const override {
    return chineseRelatedYearDiff;
  }

 public:
  UClassID getDynamicClassID() const override;
  static UClassID U_EXPORT2 getStaticClassID();

 protected:
  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

  struct SystemDefaultCenturyLocale {
    static inline const char* identifier = "@calendar=chinese";
  };
  static inline SystemDefaultCentury<ICU4XChineseCalendar,
                                     SystemDefaultCenturyLocale>
      defaultCentury_{};
};

}  // namespace mozilla::intl::calendar

#endif
