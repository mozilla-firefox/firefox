/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gtest/gtest.h"

#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/DateTimePart.h"
#include "mozilla/intl/DateTimePatternGenerator.h"
#include "mozilla/Span.h"
#include "TestBuffer.h"

#include <string_view>

namespace mozilla::intl {

// Firefox 1.0 release date.
const double DATE = 1032800850000.0;

static UniquePtr<DateTimeFormat> testStyle(
    const char* aLocale, DateTimeFormat::StyleBag& aStyleBag) {
  // Always specify a time zone in the tests, otherwise it will use the system
  // time zone which can vary between test runs.
  auto timeZone = Some(MakeStringSpan(u"GMT+3"));
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();
  return DateTimeFormat::TryCreateFromStyle(MakeStringSpan(aLocale), aStyleBag,
                                            gen.get(), timeZone)
      .unwrap();
}

TEST(IntlDateTimeFormat, Style_enUS_utf8)
{
  DateTimeFormat::StyleBag style;
  style.date = Some(DateTimeFormat::Style::Medium);
  style.time = Some(DateTimeFormat::Style::Medium);

  auto dtFormat = testStyle("en-US", style);
  TestBuffer<char> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();

  ASSERT_TRUE(buffer.verboseMatches("Sep 23, 2002, 8:07:30 PM"));
}

TEST(IntlDateTimeFormat, Style_enUS_utf16)
{
  DateTimeFormat::StyleBag style;
  style.date = Some(DateTimeFormat::Style::Medium);
  style.time = Some(DateTimeFormat::Style::Medium);

  auto dtFormat = testStyle("en-US", style);
  TestBuffer<char16_t> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();

  ASSERT_TRUE(buffer.verboseMatches(u"Sep 23, 2002, 8:07:30 PM"));
}

TEST(IntlDateTimeFormat, Style_ar_utf8)
{
  DateTimeFormat::StyleBag style;
  style.time = Some(DateTimeFormat::Style::Medium);

  auto dtFormat = testStyle("ar-EG", style);
  TestBuffer<char> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();

  ASSERT_TRUE(buffer.verboseMatches("٨:٠٧:٣٠ م"));
}

TEST(IntlDateTimeFormat, Style_ar_utf16)
{
  DateTimeFormat::StyleBag style;
  style.time = Some(DateTimeFormat::Style::Medium);

  auto dtFormat = testStyle("ar-EG", style);
  TestBuffer<char16_t> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();

  ASSERT_TRUE(buffer.verboseMatches(u"٨:٠٧:٣٠ م"));
}

TEST(IntlDateTimeFormat, Style_enUS_fallback_to_default_styles)
{
  DateTimeFormat::StyleBag style;

  auto dtFormat = testStyle("en-US", style);
  TestBuffer<char> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();

  ASSERT_TRUE(buffer.verboseMatches("Sep 23, 2002, 8:07:30 PM"));
}

TEST(IntlDateTimeFormat, Time_zone_IANA_identifier)
{
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();

  DateTimeFormat::StyleBag style;
  style.date = Some(DateTimeFormat::Style::Medium);
  style.time = Some(DateTimeFormat::Style::Medium);

  auto dtFormat = DateTimeFormat::TryCreateFromStyle(
                      MakeStringSpan("en-US"), style, gen.get(),
                      Some(MakeStringSpan(u"America/Chicago")))
                      .unwrap();
  TestBuffer<char> buffer;
  dtFormat->TryFormat(DATE, buffer).unwrap();
  ASSERT_TRUE(buffer.verboseMatches("Sep 23, 2002, 12:07:30 PM"));
}

TEST(IntlDateTimeFormat, GetAllowedHourCycles)
{
  auto allowed_en_US = DateTimeFormat::GetAllowedHourCycles(
                           MakeStringSpan("en"), Some(MakeStringSpan("US")))
                           .unwrap();

  ASSERT_TRUE(allowed_en_US.length() == 2);
  ASSERT_EQ(allowed_en_US[0], DateTimeFormat::HourCycle::H12);
  ASSERT_EQ(allowed_en_US[1], DateTimeFormat::HourCycle::H23);

  auto allowed_de =
      DateTimeFormat::GetAllowedHourCycles(MakeStringSpan("de"), Nothing())
          .unwrap();

  ASSERT_TRUE(allowed_de.length() == 2);
  ASSERT_EQ(allowed_de[0], DateTimeFormat::HourCycle::H23);
  ASSERT_EQ(allowed_de[1], DateTimeFormat::HourCycle::H12);
}

TEST(IntlDateTimePatternGenerator, GetBestPattern)
{
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();
  TestBuffer<char16_t> buffer;

  gen->GetBestPattern(MakeStringSpan(u"yMd"), buffer).unwrap();
  ASSERT_TRUE(buffer.verboseMatches(u"M/d/y"));
}

TEST(IntlDateTimePatternGenerator, GetSkeleton)
{
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();
  TestBuffer<char16_t> buffer;

  DateTimePatternGenerator::GetSkeleton(MakeStringSpan(u"M/d/y"), buffer)
      .unwrap();
  ASSERT_TRUE(buffer.verboseMatches(u"yMd"));
}

TEST(IntlDateTimePatternGenerator, GetPlaceholderPattern)
{
  auto gen = DateTimePatternGenerator::TryCreate("en").unwrap();
  auto span = gen->GetPlaceholderPattern();
  // The default date-time pattern for 'en' locale is u"{1}, {0}".
  ASSERT_EQ(span, MakeStringSpan(u"{1}, {0}"));
}

// A utility function to help test the DateTimeFormat::ComponentsBag.
[[nodiscard]] bool FormatComponents(
    TestBuffer<char16_t>& aBuffer, DateTimeFormat::ComponentsBag& aComponents,
    Span<const char> aLocale = MakeStringSpan("en-US")) {
  UniquePtr<DateTimePatternGenerator> gen = nullptr;
  auto dateTimePatternGenerator =
      DateTimePatternGenerator::TryCreate(aLocale.data()).unwrap();

  auto dtFormat = DateTimeFormat::TryCreateFromComponents(
      aLocale, aComponents, dateTimePatternGenerator.get(),
      Some(MakeStringSpan(u"GMT+3")));
  if (dtFormat.isErr()) {
    fprintf(stderr, "Could not create a DateTimeFormat\n");
    return false;
  }

  auto result = dtFormat.unwrap()->TryFormat(DATE, aBuffer);
  if (result.isErr()) {
    fprintf(stderr, "Could not format a DateTimeFormat\n");
    return false;
  }

  return true;
}

TEST(IntlDateTimeFormat, Components)
{
  DateTimeFormat::ComponentsBag components{};

  components.year = Some(DateTimeFormat::Numeric::Numeric);
  components.month = Some(DateTimeFormat::Month::Numeric);
  components.day = Some(DateTimeFormat::Numeric::Numeric);

  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::TwoDigit);
  components.second = Some(DateTimeFormat::Numeric::TwoDigit);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components));
  ASSERT_TRUE(buffer.verboseMatches(u"9/23/2002, 8:07:30 PM"));
}

TEST(IntlDateTimeFormat, Components_es_ES)
{
  DateTimeFormat::ComponentsBag components{};

  components.year = Some(DateTimeFormat::Numeric::Numeric);
  components.month = Some(DateTimeFormat::Month::Numeric);
  components.day = Some(DateTimeFormat::Numeric::Numeric);

  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::TwoDigit);
  components.second = Some(DateTimeFormat::Numeric::TwoDigit);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components, MakeStringSpan("es-ES")));
  ASSERT_TRUE(buffer.verboseMatches(u"23/9/2002, 20:07:30"));
}

TEST(IntlDateTimeFormat, ComponentsAll)
{
  // Use most all of the components.
  DateTimeFormat::ComponentsBag components{};

  components.era = Some(DateTimeFormat::Text::Short);

  components.year = Some(DateTimeFormat::Numeric::Numeric);
  components.month = Some(DateTimeFormat::Month::Numeric);
  components.day = Some(DateTimeFormat::Numeric::Numeric);

  components.weekday = Some(DateTimeFormat::Text::Short);

  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::TwoDigit);
  components.second = Some(DateTimeFormat::Numeric::TwoDigit);

  components.timeZoneName = Some(DateTimeFormat::TimeZoneName::Short);
  components.hourCycle = Some(DateTimeFormat::HourCycle::H24);
  components.fractionalSecondDigits = Some(3);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components));
  ASSERT_TRUE(buffer.verboseMatches(u"Mon, 9 23, 2002 AD, 20:07:30.000 GMT+3"));
}

TEST(IntlDateTimeFormat, ComponentsHour12Default)
{
  // Assert the behavior of the default "en-US" 12 hour time with day period.
  DateTimeFormat::ComponentsBag components{};
  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::Numeric);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components));
  ASSERT_TRUE(buffer.verboseMatches(u"8:07 PM"));
}

TEST(IntlDateTimeFormat, ComponentsHour24)
{
  // Test the behavior of using 24 hour time to override the default of
  // hour 12 with a day period.
  DateTimeFormat::ComponentsBag components{};
  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::Numeric);
  components.hour12 = Some(false);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components));
  ASSERT_TRUE(buffer.verboseMatches(u"20:07"));
}

TEST(IntlDateTimeFormat, ComponentsHour12DayPeriod)
{
  // Test the behavior of specifying a specific day period.
  DateTimeFormat::ComponentsBag components{};

  components.hour = Some(DateTimeFormat::Numeric::Numeric);
  components.minute = Some(DateTimeFormat::Numeric::Numeric);
  components.dayPeriod = Some(DateTimeFormat::Text::Long);

  TestBuffer<char16_t> buffer;
  ASSERT_TRUE(FormatComponents(buffer, components));
  ASSERT_TRUE(buffer.verboseMatches(u"8:07 in the evening"));
}

const char* ToString(uint8_t b) { return "uint8_t"; }
const char* ToString(bool b) { return b ? "true" : "false"; }

template <typename T>
const char* ToString(Maybe<T> option) {
  if (option) {
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, uint8_t>) {
      return ToString(*option);
    } else {
      return DateTimeFormat::ToString(*option);
    }
  }
  return "Nothing";
}

template <typename T>
[[nodiscard]] bool VerboseEquals(T expected, T actual, const char* msg) {
  if (expected != actual) {
    fprintf(stderr, "%s\n  Actual: %s\nExpected: %s\n", msg, ToString(actual),
            ToString(expected));
    return false;
  }
  return true;
}

// A testing utility for getting nice errors when ComponentsBags don't match.
[[nodiscard]] bool VerboseEquals(DateTimeFormat::ComponentsBag& expected,
                                 DateTimeFormat::ComponentsBag& actual) {
  // clang-format off
  return
      VerboseEquals(expected.era, actual.era, "Components do not match: bag.era") &&
      VerboseEquals(expected.year, actual.year, "Components do not match: bag.year") &&
      VerboseEquals(expected.month, actual.month, "Components do not match: bag.month") &&
      VerboseEquals(expected.day, actual.day, "Components do not match: bag.day") &&
      VerboseEquals(expected.weekday, actual.weekday, "Components do not match: bag.weekday") &&
      VerboseEquals(expected.hour, actual.hour, "Components do not match: bag.hour") &&
      VerboseEquals(expected.minute, actual.minute, "Components do not match: bag.minute") &&
      VerboseEquals(expected.second, actual.second, "Components do not match: bag.second") &&
      VerboseEquals(expected.timeZoneName, actual.timeZoneName, "Components do not match: bag.timeZoneName") &&
      VerboseEquals(expected.hour12, actual.hour12, "Components do not match: bag.hour12") &&
      VerboseEquals(expected.hourCycle, actual.hourCycle, "Components do not match: bag.hourCycle") &&
      VerboseEquals(expected.dayPeriod, actual.dayPeriod, "Components do not match: bag.dayPeriod") &&
      VerboseEquals(expected.fractionalSecondDigits, actual.fractionalSecondDigits, "Components do not match: bag.fractionalSecondDigits");
  // clang-format on
}

// A utility function to help test the DateTimeFormat::ComponentsBag.
[[nodiscard]] bool ResolveComponentsBag(
    DateTimeFormat::ComponentsBag& aComponentsIn,
    DateTimeFormat::ComponentsBag* aComponentsOut,
    Span<const char> aLocale = MakeStringSpan("en-US")) {
  UniquePtr<DateTimePatternGenerator> gen = nullptr;
  auto dateTimePatternGenerator =
      DateTimePatternGenerator::TryCreate("en").unwrap();
  auto dtFormat = DateTimeFormat::TryCreateFromComponents(
      aLocale, aComponentsIn, dateTimePatternGenerator.get(),
      Some(MakeStringSpan(u"GMT+3")));
  if (dtFormat.isErr()) {
    fprintf(stderr, "Could not create a DateTimeFormat\n");
    return false;
  }

  auto result = dtFormat.unwrap()->ResolveComponents();
  if (result.isErr()) {
    fprintf(stderr, "Could not resolve the components\n");
    return false;
  }

  *aComponentsOut = result.unwrap();
  return true;
}

TEST(IntlDateTimeFormat, ResolvedComponentsDate)
{
  DateTimeFormat::ComponentsBag input{};
  {
    input.year = Some(DateTimeFormat::Numeric::Numeric);
    input.month = Some(DateTimeFormat::Month::Numeric);
    input.day = Some(DateTimeFormat::Numeric::Numeric);
  }

  DateTimeFormat::ComponentsBag expected = input;

  DateTimeFormat::ComponentsBag resolved{};
  ASSERT_TRUE(ResolveComponentsBag(input, &resolved));
  ASSERT_TRUE(VerboseEquals(expected, resolved));
}

TEST(IntlDateTimeFormat, ResolvedComponentsAll)
{
  DateTimeFormat::ComponentsBag input{};
  {
    input.era = Some(DateTimeFormat::Text::Short);

    input.year = Some(DateTimeFormat::Numeric::Numeric);
    input.month = Some(DateTimeFormat::Month::Numeric);
    input.day = Some(DateTimeFormat::Numeric::Numeric);

    input.weekday = Some(DateTimeFormat::Text::Short);

    input.hour = Some(DateTimeFormat::Numeric::Numeric);
    input.minute = Some(DateTimeFormat::Numeric::TwoDigit);
    input.second = Some(DateTimeFormat::Numeric::TwoDigit);

    input.timeZoneName = Some(DateTimeFormat::TimeZoneName::Short);
    input.hourCycle = Some(DateTimeFormat::HourCycle::H24);
    input.fractionalSecondDigits = Some(3);
  }

  DateTimeFormat::ComponentsBag expected = input;
  {
    expected.hour = Some(DateTimeFormat::Numeric::TwoDigit);
    expected.hourCycle = Some(DateTimeFormat::HourCycle::H24);
    expected.hour12 = Some(false);
  }

  DateTimeFormat::ComponentsBag resolved{};
  ASSERT_TRUE(ResolveComponentsBag(input, &resolved));
  ASSERT_TRUE(VerboseEquals(expected, resolved));
}

TEST(IntlDateTimeFormat, ResolvedComponentsHourDayPeriod)
{
  DateTimeFormat::ComponentsBag input{};
  {
    input.hour = Some(DateTimeFormat::Numeric::Numeric);
    input.minute = Some(DateTimeFormat::Numeric::Numeric);
  }

  DateTimeFormat::ComponentsBag expected = input;
  {
    expected.minute = Some(DateTimeFormat::Numeric::TwoDigit);
    expected.hourCycle = Some(DateTimeFormat::HourCycle::H12);
    expected.hour12 = Some(true);
  }

  DateTimeFormat::ComponentsBag resolved{};
  ASSERT_TRUE(ResolveComponentsBag(input, &resolved));
  ASSERT_TRUE(VerboseEquals(expected, resolved));
}

TEST(IntlDateTimeFormat, ResolvedComponentsHour12)
{
  DateTimeFormat::ComponentsBag input{};
  {
    input.hour = Some(DateTimeFormat::Numeric::Numeric);
    input.minute = Some(DateTimeFormat::Numeric::Numeric);
    input.hour12 = Some(false);
  }

  DateTimeFormat::ComponentsBag expected = input;
  {
    expected.hour = Some(DateTimeFormat::Numeric::TwoDigit);
    expected.minute = Some(DateTimeFormat::Numeric::TwoDigit);
    expected.hourCycle = Some(DateTimeFormat::HourCycle::H23);
    expected.hour12 = Some(false);
  }

  DateTimeFormat::ComponentsBag resolved{};
  ASSERT_TRUE(ResolveComponentsBag(input, &resolved));
  ASSERT_TRUE(VerboseEquals(expected, resolved));
}

TEST(IntlDateTimeFormat, GetOriginalSkeleton)
{
  // Demonstrate that the original skeleton and the resolved skeleton can
  // differ.
  DateTimeFormat::ComponentsBag components{};
  components.month = Some(DateTimeFormat::Month::Narrow);
  components.day = Some(DateTimeFormat::Numeric::TwoDigit);

  const char* locale = "zh-Hans-CN";
  auto dateTimePatternGenerator =
      DateTimePatternGenerator::TryCreate(locale).unwrap();

  auto result = DateTimeFormat::TryCreateFromComponents(
      MakeStringSpan(locale), components, dateTimePatternGenerator.get(),
      Some(MakeStringSpan(u"GMT+3")));
  ASSERT_TRUE(result.isOk());
  auto dtFormat = result.unwrap();

  TestBuffer<char16_t> originalSkeleton;
  auto originalSkeletonResult = dtFormat->GetOriginalSkeleton(originalSkeleton);
  ASSERT_TRUE(originalSkeletonResult.isOk());
  ASSERT_TRUE(originalSkeleton.verboseMatches(u"MMMMMdd"));

  TestBuffer<char16_t> pattern;
  auto patternResult = dtFormat->GetPattern(pattern);
  ASSERT_TRUE(patternResult.isOk());
  ASSERT_TRUE(pattern.verboseMatches(u"M月dd日"));

  TestBuffer<char16_t> resolvedSkeleton;
  auto resolvedSkeletonResult = DateTimePatternGenerator::GetSkeleton(
      Span(pattern.data(), pattern.length()), resolvedSkeleton);

  ASSERT_TRUE(resolvedSkeletonResult.isOk());
  ASSERT_TRUE(resolvedSkeleton.verboseMatches(u"Mdd"));
}

TEST(IntlDateTimeFormat, GetAvailableLocales)
{
  using namespace std::literals;

  int32_t english = 0;
  int32_t german = 0;
  int32_t chinese = 0;

  // Since this list is dependent on ICU, and may change between upgrades, only
  // test a subset of the available locales.
  for (const char* locale : DateTimeFormat::GetAvailableLocales()) {
    if (locale == "en"sv) {
      english++;
    } else if (locale == "de"sv) {
      german++;
    } else if (locale == "zh"sv) {
      chinese++;
    }
  }

  // Each locale should be found exactly once.
  ASSERT_EQ(english, 1);
  ASSERT_EQ(german, 1);
  ASSERT_EQ(chinese, 1);
}

TEST(IntlDateTimeFormat, TryFormatToParts)
{
  auto dateTimePatternGenerator =
      DateTimePatternGenerator::TryCreate("en").unwrap();

  DateTimeFormat::ComponentsBag components;
  components.year = Some(DateTimeFormat::Numeric::Numeric);
  components.month = Some(DateTimeFormat::Month::TwoDigit);
  components.day = Some(DateTimeFormat::Numeric::TwoDigit);
  components.hour = Some(DateTimeFormat::Numeric::TwoDigit);
  components.minute = Some(DateTimeFormat::Numeric::TwoDigit);
  components.hour12 = Some(false);

  UniquePtr<DateTimeFormat> dtFormat =
      DateTimeFormat::TryCreateFromComponents(
          MakeStringSpan("en-US"), components, dateTimePatternGenerator.get(),
          Some(MakeStringSpan(u"GMT")))
          .unwrap();

  TestBuffer<char16_t> buffer;
  mozilla::intl::DateTimePartVector parts;
  auto result = dtFormat->TryFormatToParts(DATE, buffer, parts);
  ASSERT_TRUE(result.isOk());

  std::u16string_view strView = buffer.get_string_view();
  ASSERT_EQ(strView, u"09/23/2002, 17:07");

  auto getSubStringView = [strView, &parts](size_t index) {
    size_t pos = index == 0 ? 0 : parts[index - 1].mEndIndex;
    size_t count = parts[index].mEndIndex - pos;
    return strView.substr(pos, count);
  };

  ASSERT_EQ(parts[0].mType, DateTimePartType::Month);
  ASSERT_EQ(getSubStringView(0), u"09");

  ASSERT_EQ(parts[1].mType, DateTimePartType::Literal);
  ASSERT_EQ(getSubStringView(1), u"/");

  ASSERT_EQ(parts[2].mType, DateTimePartType::Day);
  ASSERT_EQ(getSubStringView(2), u"23");

  ASSERT_EQ(parts[3].mType, DateTimePartType::Literal);
  ASSERT_EQ(getSubStringView(3), u"/");

  ASSERT_EQ(parts[4].mType, DateTimePartType::Year);
  ASSERT_EQ(getSubStringView(4), u"2002");

  ASSERT_EQ(parts[5].mType, DateTimePartType::Literal);
  ASSERT_EQ(getSubStringView(5), u", ");

  ASSERT_EQ(parts[6].mType, DateTimePartType::Hour);
  ASSERT_EQ(getSubStringView(6), u"17");

  ASSERT_EQ(parts[7].mType, DateTimePartType::Literal);
  ASSERT_EQ(getSubStringView(7), u":");

  ASSERT_EQ(parts[8].mType, DateTimePartType::Minute);
  ASSERT_EQ(getSubStringView(8), u"07");

  ASSERT_EQ(parts.length(), 9u);
}

TEST(IntlDateTimeFormat, SetStartTimeIfGregorian)
{
  using namespace std::literals;

  DateTimeFormat::StyleBag style{};
  style.date = Some(DateTimeFormat::Style::Long);

  auto timeZone = Some(MakeStringSpan(u"UTC"));

  // Gregorian change date defaults to October 15, 1582 in ICU. Test with a date
  // before the default change date, in this case January 1, 1582.
  constexpr double FirstJanuary1582 = -12244089600000.0;

  // One year expressed in milliseconds.
  constexpr double oneYear = (365 * 24 * 60 * 60) * 1000.0;

  // Test with and without explicit calendar. The start time of the calendar can
  // only be adjusted for the Gregorian and the ISO-8601 calendar.
  for (const char* locale : {
           "en-US",
           "en-US-u-ca-gregory",
           "en-US-u-ca-iso8601",
       }) {
    auto gen = DateTimePatternGenerator::TryCreate(locale).unwrap();

    auto dtFormat = DateTimeFormat::TryCreateFromStyle(
                        MakeStringSpan(locale), style, gen.get(), timeZone)
                        .unwrap();

    const char* Jan01_1582;
    const char* Jan01_1583;
    if (locale == "en-US-u-ca-iso8601"sv) {
      Jan01_1582 = "1582 January 1";
      Jan01_1583 = "1583 January 1";
    } else {
      Jan01_1582 = "January 1, 1582";
      Jan01_1583 = "January 1, 1583";
    }

    TestBuffer<char> buffer;

    // Before the default Gregorian change date, but not interpreted in the
    // Julian calendar, which is December 22, 1581. Instead interpreted in
    // proleptic Gregorian calendar at January 1, 1582.
    dtFormat->TryFormat(FirstJanuary1582, buffer).unwrap();
    ASSERT_TRUE(buffer.verboseMatches(Jan01_1582));

    // After default Gregorian change date, so January 1, 1583.
    dtFormat->TryFormat(FirstJanuary1582 + oneYear, buffer).unwrap();
    ASSERT_TRUE(buffer.verboseMatches(Jan01_1583));
  }
}

TEST(IntlDateTimeFormat, GetTimeSeparator)
{
  struct TestData {
    const char* locale;
    const char* numberingSystem;
    const char16_t* expected;
  } testData[] = {
      {"root", "latn", u":"},
      {"root", "arab", u":"},
      {"root", "thai", u":"},
      {"root", "arabext", u"٫"},

      // English uses the same data as the root locale.
      {"en", "latn", u":"},
      {"en", "arab", u":"},
      {"en", "thai", u":"},
      {"en", "arabext", u"٫"},

      // Spanish uses the same data as the root locale.
      {"es", "latn", u":"},
      {"es", "arab", u":"},
      {"es", "thai", u":"},
      {"es", "arabext", u"٫"},

      // German (Austria) uses the same data as the root locale.
      {"de-AT", "latn", u":"},
      {"de-AT", "arab", u":"},
      {"de-AT", "thai", u":"},
      {"de-AT", "arabext", u"٫"},

      // Danish has a different time separator for "latn".
      {"da", "latn", u"."},
      {"da", "arab", u":"},
      {"da", "thai", u"."},
      {"da", "arabext", u"٫"},

      // Same time separator as Danish.
      {"en-DK", "latn", u"."},
      {"en-DK", "arab", u":"},
      {"en-DK", "thai", u"."},
      {"en-DK", "arabext", u"٫"},

      // Norwegian overrides time separators for "arab" and "arabext".
      {"no", "latn", u":"},
      {"no", "arab", u"."},
      {"no", "thai", u":"},
      {"no", "arabext", u"."},

      // Parent locale of Bokmål is Norwegian.
      {"nb", "latn", u":"},
      {"nb", "arab", u"."},
      {"nb", "thai", u":"},
      {"nb", "arabext", u"."},

      // Farsi overrides the time separator for "arabext".
      {"fa", "latn", u":"},
      {"fa", "arab", u":"},
      {"fa", "thai", u":"},
      {"fa", "arabext", u":"},
  };

  for (const auto& data : testData) {
    TestBuffer<char16_t> timeSeparator;
    auto timeSeparatorResult = DateTimeFormat::GetTimeSeparator(
        MakeStringSpan(data.locale), MakeStringSpan(data.numberingSystem),
        timeSeparator);
    ASSERT_TRUE(timeSeparatorResult.isOk());
    ASSERT_TRUE(timeSeparator.verboseMatches(data.expected));
  }
}
}  // namespace mozilla::intl
