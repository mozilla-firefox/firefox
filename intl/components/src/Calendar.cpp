/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/Calendar.h"

#include <algorithm>
#include <string_view>

#include "mozilla/intl/TimeZone.h"
#include "unicode/ucal.h"
#include "unicode/uloc.h"
#include "unicode/utypes.h"

namespace mozilla::intl {

class UndRegion {
  static constexpr std::string_view und = "und";

  // Zero-terminated locale string.
  char mLocale[und.length() + LanguageTagLimits::RegionLength + 2] = {};

 public:
  explicit UndRegion(const RegionSubtag& aRegion) {
    auto region = aRegion.Span();
    MOZ_ASSERT(IsStructurallyValidRegionTag(region));

    // Make a full locale identifier.
    auto* out = std::copy_n(und.data(), und.size(), mLocale);
    *out++ = '-';
    std::copy_n(region.data(), region.size(), out);
  }

  operator const char*() const { return mLocale; }
};

/* static */
Result<UniquePtr<Calendar>, ICUError> Calendar::TryCreate(
    const char* aLocale, Maybe<Span<const char16_t>> aTimeZoneOverride) {
  UErrorCode status = U_ZERO_ERROR;
  const UChar* zoneID = nullptr;
  int32_t zoneIDLen = 0;
  auto resolvedTimeZone =
      TimeZone::ResolveLegacyPOSIXTimeZone(aTimeZoneOverride);
  if (resolvedTimeZone.isErr()) {
    return resolvedTimeZone.propagateErr();
  }
  aTimeZoneOverride = resolvedTimeZone.unwrap();
  if (aTimeZoneOverride) {
    zoneIDLen = static_cast<int32_t>(aTimeZoneOverride->Length());
    zoneID = aTimeZoneOverride->Elements();
  }

  UCalendar* calendar =
      ucal_open(zoneID, zoneIDLen, aLocale, UCAL_DEFAULT, &status);

  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return MakeUnique<Calendar>(calendar);
}

/* static */
Result<UniquePtr<Calendar>, ICUError> Calendar::TryCreate(
    const RegionSubtag& aRegion) {
  return TryCreate(UndRegion{aRegion});
}

Result<Span<const char>, ICUError> Calendar::GetBcp47Type() {
  UErrorCode status = U_ZERO_ERROR;
  const char* oldType = ucal_getType(mCalendar, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  const char* bcp47Type = uloc_toUnicodeLocaleType("calendar", oldType);

  if (!bcp47Type) {
    return Err(ICUError::InternalError);
  }

  return MakeStringSpan(bcp47Type);
}

static Weekday WeekdayFromDaysOfWeek(UCalendarDaysOfWeek weekday) {
  switch (weekday) {
    case UCAL_MONDAY:
      return Weekday::Monday;
    case UCAL_TUESDAY:
      return Weekday::Tuesday;
    case UCAL_WEDNESDAY:
      return Weekday::Wednesday;
    case UCAL_THURSDAY:
      return Weekday::Thursday;
    case UCAL_FRIDAY:
      return Weekday::Friday;
    case UCAL_SATURDAY:
      return Weekday::Saturday;
    case UCAL_SUNDAY:
      return Weekday::Sunday;
  }
  MOZ_CRASH("unexpected weekday value");
}

Result<EnumSet<Weekday>, ICUError> Calendar::GetWeekend() {
  static_assert(static_cast<int32_t>(UCAL_SUNDAY) == 1);
  static_assert(static_cast<int32_t>(UCAL_SATURDAY) == 7);

  UErrorCode status = U_ZERO_ERROR;

  EnumSet<Weekday> weekend;
  for (int32_t i = UCAL_SUNDAY; i <= UCAL_SATURDAY; i++) {
    auto dayOfWeek = static_cast<UCalendarDaysOfWeek>(i);
    auto type = ucal_getDayOfWeekType(mCalendar, dayOfWeek, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    switch (type) {
      case UCAL_WEEKEND_ONSET:
        // Treat days which start as a weekday as weekdays.
        [[fallthrough]];
      case UCAL_WEEKDAY:
        break;

      case UCAL_WEEKEND_CEASE:
        // Treat days which start as a weekend day as weekend days.
        [[fallthrough]];
      case UCAL_WEEKEND:
        weekend += WeekdayFromDaysOfWeek(dayOfWeek);
        break;
    }
  }

  return weekend;
}

Weekday Calendar::GetFirstDayOfWeek() {
  int32_t firstDayOfWeek = ucal_getAttribute(mCalendar, UCAL_FIRST_DAY_OF_WEEK);
  MOZ_ASSERT(UCAL_SUNDAY <= firstDayOfWeek && firstDayOfWeek <= UCAL_SATURDAY);

  return WeekdayFromDaysOfWeek(
      static_cast<UCalendarDaysOfWeek>(firstDayOfWeek));
}

int32_t Calendar::GetMinimalDaysInFirstWeek() {
  int32_t minimalDays =
      ucal_getAttribute(mCalendar, UCAL_MINIMAL_DAYS_IN_FIRST_WEEK);
  MOZ_ASSERT(1 <= minimalDays && minimalDays <= 7);

  return minimalDays;
}

Result<Ok, ICUError> Calendar::SetTimeInMs(double aUnixEpoch) {
  UErrorCode status = U_ZERO_ERROR;
  ucal_setMillis(mCalendar, aUnixEpoch, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  return Ok{};
}

/* static */
SpanResult<char> Calendar::LegacyIdentifierToBcp47(const char* aIdentifier,
                                                   int32_t aLength) {
  if (aIdentifier == nullptr) {
    return Err(InternalError{});
  }
  // aLength is not needed here, as the ICU call uses the null terminated
  // string.
  return MakeStringSpan(uloc_toUnicodeLocaleType("ca", aIdentifier));
}

/* static */
Result<Calendar::Bcp47IdentifierEnumeration, ICUError>
Calendar::GetBcp47KeywordValuesForLocale(const char* aLocale,
                                         CommonlyUsed aCommonlyUsed) {
  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* enumeration = ucal_getKeywordValuesForLocale(
      "calendar", aLocale, static_cast<bool>(aCommonlyUsed), &status);

  if (U_SUCCESS(status)) {
    return Bcp47IdentifierEnumeration(enumeration);
  }

  return Err(ToICUError(status));
}

/* static */
Result<Calendar::Bcp47IdentifierEnumeration, ICUError>
Calendar::GetBcp47KeywordValuesForRegion(const RegionSubtag& aRegion,
                                         CommonlyUsed aCommonlyUsed) {
  return GetBcp47KeywordValuesForLocale(UndRegion{aRegion}, aCommonlyUsed);
}

Calendar::~Calendar() {
  MOZ_ASSERT(mCalendar);
  ucal_close(mCalendar);
}

}  // namespace mozilla::intl
