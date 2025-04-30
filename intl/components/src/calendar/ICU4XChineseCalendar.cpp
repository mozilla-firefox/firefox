/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/calendar/ICU4XChineseCalendar.h"

namespace mozilla::intl::calendar {

ICU4XChineseCalendar::ICU4XChineseCalendar(const icu::Locale& locale,
                                           UErrorCode& success)
    : ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind_Chinese, locale,
                                success) {}

ICU4XChineseCalendar::ICU4XChineseCalendar(const icu::TimeZone& timeZone,
                                           const icu::Locale& locale,
                                           UErrorCode& success)
    : ICU4XChineseBasedCalendar(capi::ICU4XAnyCalendarKind_Chinese, timeZone,
                                locale, success) {}

ICU4XChineseCalendar::ICU4XChineseCalendar(const ICU4XChineseCalendar& other)
    : ICU4XChineseBasedCalendar(other) {}

ICU4XChineseCalendar::~ICU4XChineseCalendar() = default;

ICU4XChineseCalendar* ICU4XChineseCalendar::clone() const {
  return new ICU4XChineseCalendar(*this);
}

const char* ICU4XChineseCalendar::getType() const { return "chinese"; }

////////////////////////////////////////////
// ICU4XCalendar implementation overrides //
////////////////////////////////////////////

std::string_view ICU4XChineseCalendar::eraName(int32_t extendedYear) const {
  return "chinese";
}

////////////////////////////////////////////
// icu::Calendar implementation overrides //
////////////////////////////////////////////

UDate ICU4XChineseCalendar::defaultCenturyStart() const {
  return defaultCentury_.start();
}

int32_t ICU4XChineseCalendar::defaultCenturyStartYear() const {
  return defaultCentury_.startYear();
}

UBool ICU4XChineseCalendar::haveDefaultCentury() const { return true; }

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(ICU4XChineseCalendar)

}  // namespace mozilla::intl::calendar
