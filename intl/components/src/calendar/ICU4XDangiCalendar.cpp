/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/calendar/ICU4XDangiCalendar.h"

namespace mozilla::intl::calendar {

ICU4XDangiCalendar::ICU4XDangiCalendar(const icu::Locale& locale,
                                       UErrorCode& success)
    : ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind_Dangi, locale,
                                success) {}

ICU4XDangiCalendar::ICU4XDangiCalendar(const icu::TimeZone& timeZone,
                                       const icu::Locale& locale,
                                       UErrorCode& success)
    : ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind_Dangi, timeZone,
                                locale, success) {}

ICU4XDangiCalendar::ICU4XDangiCalendar(const ICU4XDangiCalendar& other)
    : ICU4XChineseBasedCalendar(other) {}

ICU4XDangiCalendar::~ICU4XDangiCalendar() = default;

ICU4XDangiCalendar* ICU4XDangiCalendar::clone() const {
  return new ICU4XDangiCalendar(*this);
}

const char* ICU4XDangiCalendar::getType() const { return "dangi"; }

////////////////////////////////////////////
// ICU4XCalendar implementation overrides //
////////////////////////////////////////////

std::string_view ICU4XDangiCalendar::eraName(int32_t extendedYear) const {
  return "dangi";
}

////////////////////////////////////////////
// icu::Calendar implementation overrides //
////////////////////////////////////////////

UDate ICU4XDangiCalendar::defaultCenturyStart() const {
  return defaultCentury_.start();
}

int32_t ICU4XDangiCalendar::defaultCenturyStartYear() const {
  return defaultCentury_.startYear();
}

UBool ICU4XDangiCalendar::haveDefaultCentury() const { return true; }

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(ICU4XDangiCalendar)

}  // namespace mozilla::intl::calendar
