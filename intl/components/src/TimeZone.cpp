/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/intl/TimeZone.h"

#include "mozilla/Vector.h"

#include <algorithm>
#include <string_view>

#include "unicode/uenum.h"
#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
#  include "unicode/basictz.h"
#endif

namespace mozilla::intl {

static constexpr std::string_view LegacyPOSIXTimeZones[] = {
    "CST6CDT",
    "EST5EDT",
    "MST7MDT",
    "PST8PDT",
};

static constexpr const char* LegacyPOSIXTimeZoneCities[] = {
    "America/Chicago",
    "America/New_York",
    "America/Denver",
    "America/Los_Angeles",
};

static constexpr const char16_t* LegacyPOSIXTimeZoneCitiesUTF16[] = {
    u"America/Chicago",
    u"America/New_York",
    u"America/Denver",
    u"America/Los_Angeles",
};

template <typename C>
static Maybe<size_t> LegacyPOSIXTimeZoneIndex(Span<const C> aTimeZone) {
  for (size_t i = 0; i < std::size(LegacyPOSIXTimeZones); i++) {
    const auto& legacy = LegacyPOSIXTimeZones[i];
    if (std::equal(aTimeZone.begin(), aTimeZone.end(), legacy.begin(),
                   legacy.end())) {
      return Some(i);
    }
  }
  return Nothing();
}

/* static */
Span<const char> TimeZone::ResolveLegacyPOSIXTimeZone(
    Span<const char> aTimeZone) {
  if (auto index = LegacyPOSIXTimeZoneIndex(aTimeZone)) {
    return MakeStringSpan(LegacyPOSIXTimeZoneCities[*index]);
  }
  return aTimeZone;
}

/* static */
Span<const char16_t> TimeZone::ResolveLegacyPOSIXTimeZone(
    Span<const char16_t> aTimeZone) {
  if (auto index = LegacyPOSIXTimeZoneIndex(aTimeZone)) {
    return MakeStringSpan(LegacyPOSIXTimeZoneCitiesUTF16[*index]);
  }
  return aTimeZone;
}

/* static */
Result<Maybe<Span<const char16_t>>, ICUError>
TimeZone::ResolveLegacyPOSIXTimeZone(
    Maybe<Span<const char16_t>> aTimeZoneOverride) {
  if (aTimeZoneOverride) {
    return Some(ResolveLegacyPOSIXTimeZone(*aTimeZoneOverride));
  }

  Vector<char16_t, TimeZoneIdentifierLength> id;
  VectorToBufferAdaptor buffer(id);
  MOZ_TRY(GetDefaultTimeZone(buffer));
  if (auto index = LegacyPOSIXTimeZoneIndex(
          Span<const char16_t>(id.begin(), id.length()))) {
    return Some(MakeStringSpan(LegacyPOSIXTimeZoneCitiesUTF16[*index]));
  }
  return Maybe<Span<const char16_t>>{};
}

static Result<UniquePtr<TimeZone>, ICUError> TryCreateTimeZone(
    Maybe<Span<const char>> aTimeZoneOverride) {
  const char* zoneID = nullptr;
  int32_t zoneIDLen = 0;
  if (aTimeZoneOverride) {
    zoneIDLen = static_cast<int32_t>(aTimeZoneOverride->Length());
    zoneID = aTimeZoneOverride->Elements();
  }

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  UniquePtr<icu::TimeZone> tz;
  if (zoneID) {
    tz.reset(icu::TimeZone::createTimeZone(
        icu::UnicodeString(zoneID, zoneIDLen, icu::UnicodeString::kInvariant)));
  } else {
    tz.reset(icu::TimeZone::createDefault());
  }
  MOZ_ASSERT(tz);

  if (*tz == icu::TimeZone::getUnknown()) {
    return Err(ICUError::InternalError);
  }

  return MakeUnique<TimeZone>(std::move(tz));
#else
  const char16_t* zoneIDChar16 = nullptr;
  Vector<char16_t, TimeZone::TimeZoneIdentifierLength> zoneIDChars;
  if (zoneID) {
    if (!zoneIDChars.append(zoneID, zoneIDLen)) {
      return Err(ICUError::OutOfMemory);
    }
    zoneIDChar16 = zoneIDChars.begin();
  }

  // An empty string is used for the root locale. This is regarded as the base
  // locale of all locales, and is used as the language/country neutral locale
  // for locale sensitive operations.
  const char* rootLocale = "";

  UErrorCode status = U_ZERO_ERROR;
  UCalendar* calendar =
      ucal_open(zoneIDChar16, zoneIDLen, rootLocale, UCAL_DEFAULT, &status);

  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  // https://tc39.es/ecma262/#sec-time-values-and-time-range
  //
  // A time value supports a slightly smaller range of -8,640,000,000,000,000 to
  // 8,640,000,000,000,000 milliseconds.
  constexpr double StartOfTime = -8.64e15;

  // Ensure all computations are performed in the proleptic Gregorian calendar.
  ucal_setGregorianChange(calendar, StartOfTime, &status);

  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return MakeUnique<TimeZone>(calendar);
#endif
}

/* static */
Result<UniquePtr<TimeZone>, ICUError> TimeZone::TryCreate(
    Maybe<Span<const char>> aTimeZoneOverride) {
  if (aTimeZoneOverride) {
    return TryCreateTimeZone(
        Some(ResolveLegacyPOSIXTimeZone(*aTimeZoneOverride)));
  }

  auto result = TryCreateTimeZone(Nothing());
  if (result.isErr()) {
    return result.propagateErr();
  }
  UniquePtr<TimeZone> timeZone = result.unwrap();

  // The default time zone can also be one of the legacy POSIX time zones.
  Vector<char16_t, TimeZoneIdentifierLength> id;
  VectorToBufferAdaptor buffer(id);
  MOZ_TRY(timeZone->GetId(buffer));
  if (auto index = LegacyPOSIXTimeZoneIndex(
          Span<const char16_t>(id.begin(), id.length()))) {
    return TryCreateTimeZone(
        Some(MakeStringSpan(LegacyPOSIXTimeZoneCities[*index])));
  }
  return timeZone;
}

Result<int32_t, ICUError> TimeZone::GetRawOffsetMs() {
#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  return mTimeZone->getRawOffset();
#else
  // Reset the time in case the calendar has been modified.
  UErrorCode status = U_ZERO_ERROR;
  ucal_setMillis(mCalendar, ucal_getNow(), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  int32_t offset = ucal_get(mCalendar, UCAL_ZONE_OFFSET, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return offset;
#endif
}

Result<int32_t, ICUError> TimeZone::GetDSTOffsetMs(int64_t aUTCMilliseconds) {
  UDate date = UDate(aUTCMilliseconds);

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  constexpr bool dateIsLocalTime = false;
  int32_t rawOffset, dstOffset;
  UErrorCode status = U_ZERO_ERROR;

  mTimeZone->getOffset(date, dateIsLocalTime, rawOffset, dstOffset, status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return dstOffset;
#else
  UErrorCode status = U_ZERO_ERROR;
  ucal_setMillis(mCalendar, date, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  int32_t dstOffset = ucal_get(mCalendar, UCAL_DST_OFFSET, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return dstOffset;
#endif
}

Result<int32_t, ICUError> TimeZone::GetOffsetMs(int64_t aUTCMilliseconds) {
  UDate date = UDate(aUTCMilliseconds);

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  constexpr bool dateIsLocalTime = false;
  int32_t rawOffset, dstOffset;
  UErrorCode status = U_ZERO_ERROR;

  mTimeZone->getOffset(date, dateIsLocalTime, rawOffset, dstOffset, status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return rawOffset + dstOffset;
#else
  UErrorCode status = U_ZERO_ERROR;
  ucal_setMillis(mCalendar, date, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  int32_t rawOffset = ucal_get(mCalendar, UCAL_ZONE_OFFSET, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  int32_t dstOffset = ucal_get(mCalendar, UCAL_DST_OFFSET, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return rawOffset + dstOffset;
#endif
}

Result<int32_t, ICUError> TimeZone::GetUTCOffsetMs(int64_t aLocalMilliseconds) {
  // https://tc39.es/ecma262/#sec-local-time-zone-adjustment
  //
  // LocalTZA ( t, isUTC )
  //
  // When t_local represents local time repeating multiple times at a negative
  // time zone transition (e.g. when the daylight saving time ends or the time
  // zone offset is decreased due to a time zone rule change) or skipped local
  // time at a positive time zone transitions (e.g. when the daylight saving
  // time starts or the time zone offset is increased due to a time zone rule
  // change), t_local must be interpreted using the time zone offset before the
  // transition.
  constexpr LocalOption skippedTime = LocalOption::Former;
  constexpr LocalOption repeatedTime = LocalOption::Former;

  return GetUTCOffsetMs(aLocalMilliseconds, skippedTime, repeatedTime);
}

static UTimeZoneLocalOption ToUTimeZoneLocalOption(
    TimeZone::LocalOption aOption) {
  switch (aOption) {
    case TimeZone::LocalOption::Former:
      return UTimeZoneLocalOption::UCAL_TZ_LOCAL_FORMER;
    case TimeZone::LocalOption::Latter:
      return UTimeZoneLocalOption::UCAL_TZ_LOCAL_LATTER;
  }
  MOZ_CRASH("Unexpected TimeZone::LocalOption");
}

Result<int32_t, ICUError> TimeZone::GetUTCOffsetMs(int64_t aLocalMilliseconds,
                                                   LocalOption aSkippedTime,
                                                   LocalOption aRepeatedTime) {
  UDate date = UDate(aLocalMilliseconds);
  UTimeZoneLocalOption skippedTime = ToUTimeZoneLocalOption(aSkippedTime);
  UTimeZoneLocalOption repeatedTime = ToUTimeZoneLocalOption(aRepeatedTime);

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  int32_t rawOffset, dstOffset;
  UErrorCode status = U_ZERO_ERROR;

  // All ICU TimeZone classes derive from BasicTimeZone, so we can safely
  // perform the static_cast.
  // Once <https://unicode-org.atlassian.net/browse/ICU-13705> is fixed we
  // can remove this extra cast.
  auto* basicTz = static_cast<icu::BasicTimeZone*>(mTimeZone.get());
  basicTz->getOffsetFromLocal(date, skippedTime, repeatedTime, rawOffset,
                              dstOffset, status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return rawOffset + dstOffset;
#else
  UErrorCode status = U_ZERO_ERROR;
  ucal_setMillis(mCalendar, date, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  int32_t rawOffset, dstOffset;
  ucal_getTimeZoneOffsetFromLocal(mCalendar, skippedTime, repeatedTime,
                                  &rawOffset, &dstOffset, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return rawOffset + dstOffset;
#endif
}

Result<Maybe<int64_t>, ICUError> TimeZone::GetPreviousTransition(
    int64_t aUTCMilliseconds) {
  UDate date = UDate(aUTCMilliseconds);

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  // All ICU TimeZone classes derive from BasicTimeZone, so we can safely
  // perform the static_cast.
  auto* basicTz = static_cast<icu::BasicTimeZone*>(mTimeZone.get());

  constexpr bool inclusive = false;
  icu::TimeZoneTransition transition;
  if (!basicTz->getPreviousTransition(date, inclusive, transition)) {
    return Maybe<int64_t>();
  }
  return Some(int64_t(transition.getTime()));
#else
  UDate transition = 0;
  UErrorCode status = U_ZERO_ERROR;
  bool found = ucal_getTimeZoneTransitionDate(
      mCalendar, UCAL_TZ_TRANSITION_PREVIOUS, &transition, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  if (!found) {
    return Maybe<int64_t>();
  }
  return Some(int64_t(transition));
#endif
}

Result<Maybe<int64_t>, ICUError> TimeZone::GetNextTransition(
    int64_t aUTCMilliseconds) {
  UDate date = UDate(aUTCMilliseconds);

#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  // All ICU TimeZone classes derive from BasicTimeZone, so we can safely
  // perform the static_cast.
  auto* basicTz = static_cast<icu::BasicTimeZone*>(mTimeZone.get());

  constexpr bool inclusive = false;
  icu::TimeZoneTransition transition;
  if (!basicTz->getNextTransition(date, inclusive, transition)) {
    return Maybe<int64_t>();
  }
  return Some(int64_t(transition.getTime()));
#else
  UDate transition = 0;
  UErrorCode status = U_ZERO_ERROR;
  bool found = ucal_getTimeZoneTransitionDate(
      mCalendar, UCAL_TZ_TRANSITION_NEXT, &transition, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  if (!found) {
    return Maybe<int64_t>();
  }
  return Some(int64_t(transition));
#endif
}

using TimeZoneIdentifierVector =
    Vector<char16_t, TimeZone::TimeZoneIdentifierLength>;

#if !MOZ_INTL_USE_ICU_CPP_TIMEZONE
static bool IsUnknownTimeZone(const TimeZoneIdentifierVector& timeZone) {
  constexpr std::string_view unknownTimeZone = UCAL_UNKNOWN_ZONE_ID;

  return timeZone.length() == unknownTimeZone.length() &&
         std::equal(timeZone.begin(), timeZone.end(), unknownTimeZone.begin(),
                    unknownTimeZone.end());
}

static ICUResult SetDefaultTimeZone(TimeZoneIdentifierVector& timeZone) {
  // The string mustn't already be null-terminated.
  MOZ_ASSERT_IF(!timeZone.empty(), timeZone.end()[-1] != '\0');

  // The time zone identifier must be a null-terminated string.
  if (!timeZone.append('\0')) {
    return Err(ICUError::OutOfMemory);
  }

  UErrorCode status = U_ZERO_ERROR;
  ucal_setDefaultTimeZone(timeZone.begin(), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return Ok{};
}
#endif

Result<bool, ICUError> TimeZone::SetDefaultTimeZone(
    Span<const char> aTimeZone) {
#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  icu::UnicodeString tzid(aTimeZone.data(), aTimeZone.size(), US_INV);
  if (tzid.isBogus()) {
    return Err(ICUError::OutOfMemory);
  }

  UniquePtr<icu::TimeZone> newTimeZone(icu::TimeZone::createTimeZone(tzid));
  MOZ_ASSERT(newTimeZone);

  if (*newTimeZone != icu::TimeZone::getUnknown()) {
    // adoptDefault() takes ownership of the time zone.
    icu::TimeZone::adoptDefault(newTimeZone.release());
    return true;
  }
#else
  TimeZoneIdentifierVector tzid;
  if (!tzid.append(aTimeZone.data(), aTimeZone.size())) {
    return Err(ICUError::OutOfMemory);
  }

  // Retrieve the current default time zone in case we need to restore it.
  TimeZoneIdentifierVector defaultTimeZone;
  MOZ_TRY(FillBufferWithICUCall(defaultTimeZone, ucal_getDefaultTimeZone));

  // Try to set the new time zone.
  MOZ_TRY(mozilla::intl::SetDefaultTimeZone(tzid));

  // Check if the time zone was actually applied.
  TimeZoneIdentifierVector newTimeZone;
  MOZ_TRY(FillBufferWithICUCall(newTimeZone, ucal_getDefaultTimeZone));

  // Return if the new time zone was successfully applied.
  if (!IsUnknownTimeZone(newTimeZone)) {
    return true;
  }

  // Otherwise restore the original time zone.
  MOZ_TRY(mozilla::intl::SetDefaultTimeZone(defaultTimeZone));
#endif

  return false;
}

ICUResult TimeZone::SetDefaultTimeZoneFromHostTimeZone() {
#if MOZ_INTL_USE_ICU_CPP_TIMEZONE
  if (icu::TimeZone* defaultZone = icu::TimeZone::detectHostTimeZone()) {
    icu::TimeZone::adoptDefault(defaultZone);
  }
#else
  TimeZoneIdentifierVector hostTimeZone;
  MOZ_TRY(FillBufferWithICUCall(hostTimeZone, ucal_getHostTimeZone));

  MOZ_TRY(mozilla::intl::SetDefaultTimeZone(hostTimeZone));
#endif

  return Ok{};
}

Result<Span<const char>, ICUError> TimeZone::GetTZDataVersion() {
  UErrorCode status = U_ZERO_ERROR;
  const char* tzdataVersion = ucal_getTZDataVersion(&status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  return MakeStringSpan(tzdataVersion);
}

Result<SpanEnumeration<char>, ICUError> TimeZone::GetAvailableTimeZones(
    const RegionSubtag& aRegion) {
  auto regionSpan = aRegion.Span();
  MOZ_ASSERT(IsStructurallyValidRegionTag(regionSpan));

  // Zero-terminated region string.
  char region[LanguageTagLimits::RegionLength + 1] = {};
  std::copy_n(regionSpan.Elements(), regionSpan.Length(), region);

  // Get the time zones that are commonly used in the given region. Uses the
  // UCAL_ZONE_TYPE_CANONICAL filter so we have more fine-grained control over
  // the returned time zones. Otherwise ICU can return time zones attached to
  // other countries, for example the time zone id "Iceland" is returned when
  // requesting all times zones for "CI" (Ivory Coast).
  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* enumeration = ucal_openTimeZoneIDEnumeration(
      UCAL_ZONE_TYPE_CANONICAL, region, nullptr, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return SpanEnumeration<char>(enumeration);
}

Result<SpanEnumeration<char>, ICUError> TimeZone::GetAvailableTimeZones() {
  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* enumeration = ucal_openTimeZones(&status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  return SpanEnumeration<char>(enumeration);
}

#if !MOZ_INTL_USE_ICU_CPP_TIMEZONE
TimeZone::~TimeZone() {
  MOZ_ASSERT(mCalendar);
  ucal_close(mCalendar);
}
#endif

}  // namespace mozilla::intl
