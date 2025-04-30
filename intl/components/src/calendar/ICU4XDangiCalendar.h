/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ICU4XDangiCalendar_h_
#define intl_components_calendar_ICU4XDangiCalendar_h_

#include "mozilla/intl/calendar/ICU4XChineseBasedCalendar.h"

#include <stdint.h>
#include <string_view>

#include "unicode/uobject.h"

namespace mozilla::intl::calendar {

/**
 * Dangi (traditional Korean) calendar implementation.
 *
 * Overrides the same methods as icu::DangiCalendar to ensure compatible
 * behavior even when using ICU4X as the underlying calendar implementation.
 */
class ICU4XDangiCalendar : public ICU4XChineseBasedCalendar {
 public:
  ICU4XDangiCalendar() = delete;
  ICU4XDangiCalendar(const icu::Locale& locale, UErrorCode& success);
  ICU4XDangiCalendar(const icu::TimeZone& timeZone, const icu::Locale& locale,
                     UErrorCode& success);
  ICU4XDangiCalendar(const ICU4XDangiCalendar& other);

  virtual ~ICU4XDangiCalendar();

  ICU4XDangiCalendar* clone() const override;

  const char* getType() const override;

 protected:
  std::string_view eraName(int32_t extendedYear) const override;

  static constexpr int32_t dangiRelatedYearDiff = -2333;

  int32_t relatedYearDifference() const override {
    return dangiRelatedYearDiff;
  }

 public:
  UClassID getDynamicClassID() const override;
  static UClassID U_EXPORT2 getStaticClassID();

 protected:
  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

  struct SystemDefaultCenturyLocale {
    static inline const char* identifier = "@calendar=dangi";
  };
  static inline SystemDefaultCentury<ICU4XDangiCalendar,
                                     SystemDefaultCenturyLocale>
      defaultCentury_{};
};

}  // namespace mozilla::intl::calendar

#endif
