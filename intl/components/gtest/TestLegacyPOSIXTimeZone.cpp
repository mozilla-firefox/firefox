/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gtest/gtest.h"

#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateIntervalFormat.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/DateTimePatternGenerator.h"
#include "mozilla/intl/TimeZone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "TestBuffer.h"

#include <string>

// The legacy POSIX time zones CST6CDT, EST5EDT, MST7MDT, and PST8PDT must
// behave like their corresponding city time zones in every API which accepts a
// time zone identifier.

namespace mozilla::intl::legacy_posix_time_zone_test {

struct LegacyPOSIXTimeZone {
  const char* legacy;
  const char16_t* legacyUTF16;
  const char* city;
  const char16_t* cityUTF16;
};

static constexpr LegacyPOSIXTimeZone LegacyPOSIXTimeZones[] = {
    {"CST6CDT", u"CST6CDT", "America/Chicago", u"America/Chicago"},
    {"EST5EDT", u"EST5EDT", "America/New_York", u"America/New_York"},
    {"MST7MDT", u"MST7MDT", "America/Denver", u"America/Denver"},
    {"PST8PDT", u"PST8PDT", "America/Los_Angeles", u"America/Los_Angeles"},
};

// Instants before and after the transition to the modern US time zone rules.
static constexpr int64_t Instants[] = {
    -3'786'825'600'000,  // 1850-01-01T00:00:00Z
    1'721'044'800'000,   // 2024-07-15T12:00:00Z
};

template <typename CharType>
static std::basic_string<CharType> ToString(Span<const CharType> aSpan) {
  return std::basic_string<CharType>(aSpan.data(), aSpan.size());
}

static std::u16string Format(const DateTimeFormat& aFormat, double aInstant) {
  TestBuffer<char16_t> buffer;
  aFormat.TryFormat(aInstant, buffer).unwrap();
  return std::u16string(buffer.get_string_view());
}

static std::u16string FormatRange(const DateIntervalFormat& aFormat,
                                  double aStart, double aEnd) {
  AutoFormattedDateInterval formatted;
  bool equal;
  aFormat.TryFormatDateTime(aStart, aEnd, formatted, &equal).unwrap();
  return ToString(formatted.ToSpan().unwrap());
}

static std::u16string FormatCalendars(const DateIntervalFormat& aFormat,
                                      const Calendar& aStart,
                                      const Calendar& aEnd) {
  AutoFormattedDateInterval formatted;
  bool equal;
  aFormat.TryFormatCalendar(aStart, aEnd, formatted, &equal).unwrap();
  return ToString(formatted.ToSpan().unwrap());
}

// Restores the default time zone when going out of scope.
class AutoRestoreDefaultTimeZone {
  std::string mTimeZone;

 public:
  AutoRestoreDefaultTimeZone() {
    TestBuffer<char16_t> buffer;
    TimeZone::GetDefaultTimeZone(buffer).unwrap();
    for (char16_t ch : buffer.get_string_view()) {
      mTimeZone.push_back(char(ch));
    }
  }

  ~AutoRestoreDefaultTimeZone() {
    TimeZone::SetDefaultTimeZone(Span<const char>(mTimeZone)).unwrap();
  }
};

TEST(IntlLegacyPOSIXTimeZone, TimeZoneTryCreate)
{
  AutoRestoreDefaultTimeZone restore;

  for (const auto& zone : LegacyPOSIXTimeZones) {
    auto legacy =
        TimeZone::TryCreate(Some(MakeStringSpan(zone.legacy))).unwrap();
    auto city = TimeZone::TryCreate(Some(MakeStringSpan(zone.city))).unwrap();
    ASSERT_TRUE(
        TimeZone::SetDefaultTimeZone(MakeStringSpan(zone.legacy)).unwrap());
    auto defaultZone = TimeZone::TryCreate().unwrap();

    for (int64_t instant : Instants) {
      auto expected = city->GetOffsetMs(instant).unwrap();
      ASSERT_EQ(legacy->GetOffsetMs(instant).unwrap(), expected);
      ASSERT_EQ(defaultZone->GetOffsetMs(instant).unwrap(), expected);
    }
  }
}

TEST(IntlLegacyPOSIXTimeZone, DateTimeFormatFromStyle)
{
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();

  DateTimeFormat::StyleBag style;
  style.date = Some(DateTimeFormat::Style::Full);
  style.time = Some(DateTimeFormat::Style::Full);

  auto create = [&](const char16_t* aTimeZone) {
    return DateTimeFormat::TryCreateFromStyle(MakeStringSpan("en-US"), style,
                                              gen.get(),
                                              Some(MakeStringSpan(aTimeZone)))
        .unwrap();
  };

  for (const auto& zone : LegacyPOSIXTimeZones) {
    auto legacy = create(zone.legacyUTF16);
    auto city = create(zone.cityUTF16);
    for (int64_t instant : Instants) {
      ASSERT_EQ(Format(*legacy, double(instant)),
                Format(*city, double(instant)));
    }
  }
}

TEST(IntlLegacyPOSIXTimeZone, DateTimeFormatDefaultTimeZone)
{
  AutoRestoreDefaultTimeZone restore;

  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();

  DateTimeFormat::StyleBag style;
  style.date = Some(DateTimeFormat::Style::Full);
  style.time = Some(DateTimeFormat::Style::Full);

  auto create = [&](const char* aDefaultTimeZone, bool aPattern) {
    MOZ_RELEASE_ASSERT(
        TimeZone::SetDefaultTimeZone(MakeStringSpan(aDefaultTimeZone))
            .unwrap());
    if (aPattern) {
      return DateTimeFormat::TryCreateFromPattern(
                 MakeStringSpan("en-US"),
                 MakeStringSpan(u"yyyy-MM-dd HH:mm:ss zzzz"))
          .unwrap();
    }
    return DateTimeFormat::TryCreateFromStyle(MakeStringSpan("en-US"), style,
                                              gen.get())
        .unwrap();
  };

  for (const auto& zone : LegacyPOSIXTimeZones) {
    for (bool pattern : {false, true}) {
      auto legacy = create(zone.legacy, pattern);
      auto city = create(zone.city, pattern);
      for (int64_t instant : Instants) {
        ASSERT_EQ(Format(*legacy, double(instant)),
                  Format(*city, double(instant)));
      }
    }
  }
}

TEST(IntlLegacyPOSIXTimeZone, DateTimeFormatFromPattern)
{
  auto create = [](const char16_t* aTimeZone) {
    return DateTimeFormat::TryCreateFromPattern(
               MakeStringSpan("en-US"),
               MakeStringSpan(u"yyyy-MM-dd HH:mm:ss zzzz"),
               Some(MakeStringSpan(aTimeZone)))
        .unwrap();
  };

  for (const auto& zone : LegacyPOSIXTimeZones) {
    auto legacy = create(zone.legacyUTF16);
    auto city = create(zone.cityUTF16);
    for (int64_t instant : Instants) {
      ASSERT_EQ(Format(*legacy, double(instant)),
                Format(*city, double(instant)));
    }
  }
}

TEST(IntlLegacyPOSIXTimeZone, DateIntervalFormat)
{
  auto create = [](const char16_t* aTimeZone) {
    return DateIntervalFormat::TryCreate(MakeStringSpan("en-US"),
                                         MakeStringSpan(u"yMMddHHmmzzzz"),
                                         MakeStringSpan(aTimeZone))
        .unwrap();
  };

  for (const auto& zone : LegacyPOSIXTimeZones) {
    auto legacy = create(zone.legacyUTF16);
    auto city = create(zone.cityUTF16);
    for (int64_t start : Instants) {
      double end = double(start) + 3'600'000;
      ASSERT_EQ(FormatRange(*legacy, double(start), end),
                FormatRange(*city, double(start), end));
    }
  }
}

TEST(IntlLegacyPOSIXTimeZone, CalendarTryCreate)
{
  AutoRestoreDefaultTimeZone restore;

  // Calendars carry their own time zone, which DateIntervalFormat uses when
  // formatting calendars.
  auto dif = DateIntervalFormat::TryCreate(MakeStringSpan("en-US"),
                                           MakeStringSpan(u"yMMddHHmmzzzz"),
                                           MakeStringSpan(u"UTC"))
                 .unwrap();

  auto create = [](const char16_t* aTimeZone, int64_t aInstant) {
    auto calendar =
        Calendar::TryCreate("en-US", Some(MakeStringSpan(aTimeZone))).unwrap();
    calendar->SetTimeInMs(double(aInstant)).unwrap();
    return calendar;
  };

  for (const auto& zone : LegacyPOSIXTimeZones) {
    ASSERT_TRUE(
        TimeZone::SetDefaultTimeZone(MakeStringSpan(zone.legacy)).unwrap());
    for (int64_t instant : Instants) {
      auto legacy = create(zone.legacyUTF16, instant);
      auto city = create(zone.cityUTF16, instant);
      auto defaultCalendar = Calendar::TryCreate("en-US").unwrap();
      defaultCalendar->SetTimeInMs(double(instant)).unwrap();
      auto expected = FormatCalendars(*dif, *city, *city);
      ASSERT_EQ(FormatCalendars(*dif, *legacy, *legacy), expected);
      ASSERT_EQ(FormatCalendars(*dif, *defaultCalendar, *defaultCalendar),
                expected);
    }
  }
}

}  // namespace mozilla::intl::legacy_posix_time_zone_test
