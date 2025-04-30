/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cstring>

#include "unicode/ucal.h"
#include "unicode/udat.h"
#include "unicode/udatpg.h"
#include "unicode/ures.h"

#include "DateTimeFormatUtils.h"
#include "ScopedICUObject.h"

#include "mozilla/Buffer.h"
#include "mozilla/EnumSet.h"
#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/DateTimePatternGenerator.h"

namespace mozilla::intl {

DateTimeFormat::~DateTimeFormat() {
  MOZ_ASSERT(mDateFormat);
  udat_close(mDateFormat);
}

static UDateFormatStyle ToUDateFormatStyle(
    Maybe<DateTimeFormat::Style> aLength) {
  if (!aLength) {
    return UDAT_NONE;
  }
  switch (*aLength) {
    case DateTimeFormat::Style::Full:
      return UDAT_FULL;
    case DateTimeFormat::Style::Long:
      return UDAT_LONG;
    case DateTimeFormat::Style::Medium:
      return UDAT_MEDIUM;
    case DateTimeFormat::Style::Short:
      return UDAT_SHORT;
  }
  MOZ_ASSERT_UNREACHABLE();
  // Do not use the default: branch so that the enum is exhaustively checked.
  return UDAT_NONE;
}

/**
 * Parse a pattern according to the format specified in
 * <https://unicode.org/reports/tr35/tr35-dates.html#Date_Format_Patterns>.
 */
template <typename CharT>
class PatternIterator {
  CharT* iter;
  const CharT* const end;

 public:
  explicit PatternIterator(mozilla::Span<CharT> aPattern)
      : iter(aPattern.data()), end(aPattern.data() + aPattern.size()) {}

  CharT* next() {
    MOZ_ASSERT(iter != nullptr);

    bool inQuote = false;
    while (iter < end) {
      CharT* cur = iter++;
      if (*cur == '\'') {
        inQuote = !inQuote;
      } else if (!inQuote) {
        return cur;
      }
    }

    iter = nullptr;
    return nullptr;
  }
};

Maybe<DateTimeFormat::HourCycle> DateTimeFormat::HourCycleFromPattern(
    Span<const char16_t> aPattern) {
  PatternIterator<const char16_t> iter(aPattern);
  while (const auto* ptr = iter.next()) {
    switch (*ptr) {
      case 'K':
        return Some(DateTimeFormat::HourCycle::H11);
      case 'h':
        return Some(DateTimeFormat::HourCycle::H12);
      case 'H':
        return Some(DateTimeFormat::HourCycle::H23);
      case 'k':
        return Some(DateTimeFormat::HourCycle::H24);
    }
  }
  return Nothing();
}

static bool IsHour12(DateTimeFormat::HourCycle aHourCycle) {
  return aHourCycle == DateTimeFormat::HourCycle::H11 ||
         aHourCycle == DateTimeFormat::HourCycle::H12;
}

static char16_t HourSymbol(DateTimeFormat::HourCycle aHourCycle) {
  switch (aHourCycle) {
    case DateTimeFormat::HourCycle::H11:
      return 'K';
    case DateTimeFormat::HourCycle::H12:
      return 'h';
    case DateTimeFormat::HourCycle::H23:
      return 'H';
    case DateTimeFormat::HourCycle::H24:
      return 'k';
  }
  MOZ_CRASH("unexpected hour cycle");
}

enum class PatternField { Hour, Minute, Second, Other };

template <typename CharT>
static PatternField ToPatternField(CharT aCh) {
  if (aCh == 'K' || aCh == 'h' || aCh == 'H' || aCh == 'k' || aCh == 'j') {
    return PatternField::Hour;
  }
  if (aCh == 'm') {
    return PatternField::Minute;
  }
  if (aCh == 's') {
    return PatternField::Second;
  }
  return PatternField::Other;
}

/**
 * Replaces all hour pattern characters in |patternOrSkeleton| to use the
 * matching hour representation for |hourCycle|.
 */
/* static */
void DateTimeFormat::ReplaceHourSymbol(
    mozilla::Span<char16_t> aPatternOrSkeleton,
    DateTimeFormat::HourCycle aHourCycle) {
  char16_t replacement = HourSymbol(aHourCycle);
  PatternIterator<char16_t> iter(aPatternOrSkeleton);
  while (auto* ptr = iter.next()) {
    auto field = ToPatternField(*ptr);
    if (field == PatternField::Hour) {
      *ptr = replacement;
    }
  }
}

/**
 * Find a matching pattern using the requested hour-12 options.
 *
 * This function is needed to work around the following two issues.
 * - https://unicode-org.atlassian.net/browse/ICU-21023
 * - https://unicode-org.atlassian.net/browse/CLDR-13425
 *
 * We're currently using a relatively simple workaround, which doesn't give the
 * most accurate results. For example:
 *
 * ```
 * var dtf = new Intl.DateTimeFormat("en", {
 *   timeZone: "UTC",
 *   dateStyle: "long",
 *   timeStyle: "long",
 *   hourCycle: "h12",
 * });
 * print(dtf.format(new Date("2020-01-01T00:00Z")));
 * ```
 *
 * Returns the pattern "MMMM d, y 'at' h:mm:ss a z", but when going through
 * |DateTimePatternGenerator::GetSkeleton| and then
 * |DateTimePatternGenerator::GetBestPattern| to find an equivalent pattern for
 * "h23", we'll end up with the pattern "MMMM d, y, HH:mm:ss z", so the
 * combinator element " 'at' " was lost in the process.
 */
/* static */
ICUResult DateTimeFormat::FindPatternWithHourCycle(
    DateTimePatternGenerator& aDateTimePatternGenerator,
    DateTimeFormat::PatternVector& aPattern, bool aHour12,
    DateTimeFormat::SkeletonVector& aSkeleton) {
  MOZ_TRY(mozilla::intl::DateTimePatternGenerator::GetSkeleton(aPattern,
                                                               aSkeleton));

  // Input skeletons don't differentiate between "K" and "h" resp. "k" and "H".
  DateTimeFormat::ReplaceHourSymbol(aSkeleton,
                                    aHour12 ? DateTimeFormat::HourCycle::H12
                                            : DateTimeFormat::HourCycle::H23);

  MOZ_TRY(aDateTimePatternGenerator.GetBestPattern(aSkeleton, aPattern));

  return Ok();
}

static auto PatternMatchOptions(mozilla::Span<const char16_t> aSkeleton) {
  // Values for hour, minute, and second are:
  // - absent: 0
  // - numeric: 1
  // - 2-digit: 2
  int32_t hour = 0;
  int32_t minute = 0;
  int32_t second = 0;

  PatternIterator<const char16_t> iter(aSkeleton);
  while (const auto* ptr = iter.next()) {
    switch (ToPatternField(*ptr)) {
      case PatternField::Hour:
        MOZ_ASSERT(hour < 2);
        hour += 1;
        break;
      case PatternField::Minute:
        MOZ_ASSERT(minute < 2);
        minute += 1;
        break;
      case PatternField::Second:
        MOZ_ASSERT(second < 2);
        second += 1;
        break;
      case PatternField::Other:
        break;
    }
  }

  // Adjust the field length when the user requested '2-digit' representation.
  //
  // We can't just always adjust the field length, because
  // 1. The default value for hour, minute, and second fields is 'numeric'. If
  //    the length is always adjusted, |date.toLocaleTime()| will start to
  //    return strings like "1:5:9 AM" instead of "1:05:09 AM".
  // 2. ICU doesn't support to adjust the field length to 'numeric' in certain
  //    cases. For example when the locale is "de" (German):
  //      a. hour='numeric' and minute='2-digit' will return "1:05".
  //      b. whereas hour='numeric' and minute='numeric' will return "01:05".
  //
  // Therefore we only support adjusting the field length when the user
  // explicitly requested the '2-digit' representation.

  using PatternMatchOption =
      mozilla::intl::DateTimePatternGenerator::PatternMatchOption;
  mozilla::EnumSet<PatternMatchOption> options;
  if (hour == 2) {
    options += PatternMatchOption::HourField;
  }
  if (minute == 2) {
    options += PatternMatchOption::MinuteField;
  }
  if (second == 2) {
    options += PatternMatchOption::SecondField;
  }
  return options;
}

/* static */
Result<UniquePtr<DateTimeFormat>, ICUError> DateTimeFormat::TryCreateFromStyle(
    Span<const char> aLocale, const StyleBag& aStyleBag,
    DateTimePatternGenerator* aDateTimePatternGenerator,
    Maybe<Span<const char16_t>> aTimeZoneOverride) {
  auto dateStyle = ToUDateFormatStyle(aStyleBag.date);
  auto timeStyle = ToUDateFormatStyle(aStyleBag.time);

  if (dateStyle == UDAT_NONE && timeStyle == UDAT_NONE) {
    dateStyle = UDAT_DEFAULT;
    timeStyle = UDAT_DEFAULT;
  }

  // The time zone is optional.
  int32_t tzIDLength = -1;
  const UChar* tzID = nullptr;
  if (aTimeZoneOverride) {
    tzIDLength = static_cast<int32_t>(aTimeZoneOverride->size());
    tzID = aTimeZoneOverride->Elements();
  }

  UErrorCode status = U_ZERO_ERROR;
  UDateFormat* dateFormat =
      udat_open(timeStyle, dateStyle, IcuLocale(aLocale), tzID, tzIDLength,
                /* pattern */ nullptr, /* pattern length */ -1, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  MOZ_TRY(ApplyCalendarOverride(dateFormat));

  auto df = UniquePtr<DateTimeFormat>(new DateTimeFormat(dateFormat));

  if (aStyleBag.time && (aStyleBag.hour12 || aStyleBag.hourCycle)) {
    // Only adjust the style pattern for time if there is an override.
    // Extract the pattern and adjust it for the preferred hour cycle.
    DateTimeFormat::PatternVector pattern{};

    VectorToBufferAdaptor buffer(pattern);
    MOZ_TRY(df->GetPattern(buffer));

    Maybe<DateTimeFormat::HourCycle> hcPattern = HourCycleFromPattern(pattern);
    DateTimeFormat::SkeletonVector skeleton{};

    if (hcPattern) {
      bool wantHour12 =
          aStyleBag.hour12 ? *aStyleBag.hour12 : IsHour12(*aStyleBag.hourCycle);
      if (wantHour12 == IsHour12(*hcPattern)) {
        // Return the date-time format when its hour-cycle settings match the
        // requested options.
        if (aStyleBag.hour12 || *hcPattern == *aStyleBag.hourCycle) {
          return df;
        }
      } else {
        MOZ_ASSERT(aDateTimePatternGenerator);
        MOZ_TRY(DateTimeFormat::FindPatternWithHourCycle(
            *aDateTimePatternGenerator, pattern, wantHour12, skeleton));
      }
      // Replace the hourCycle, if present, in the pattern string. But only do
      // this if no hour12 option is present, because the latter takes
      // precedence over hourCycle.
      if (!aStyleBag.hour12) {
        DateTimeFormat::ReplaceHourSymbol(pattern, *aStyleBag.hourCycle);
      }

      auto result = DateTimeFormat::TryCreateFromPattern(aLocale, pattern,
                                                         aTimeZoneOverride);
      if (result.isErr()) {
        return Err(result.unwrapErr());
      }
      auto dateTimeFormat = result.unwrap();
      MOZ_TRY(dateTimeFormat->CacheSkeleton(skeleton));
      return dateTimeFormat;
    }
  }

  return df;
}

DateTimeFormat::DateTimeFormat(UDateFormat* aDateFormat) {
  MOZ_RELEASE_ASSERT(aDateFormat, "Expected aDateFormat to not be a nullptr.");
  mDateFormat = aDateFormat;
}

// A helper to ergonomically push a string onto a string vector.
template <typename V, size_t N>
static ICUResult PushString(V& aVec, const char16_t (&aString)[N]) {
  if (!aVec.append(aString, N - 1)) {
    return Err(ICUError::OutOfMemory);
  }
  return Ok();
}

// A helper to ergonomically push a char onto a string vector.
template <typename V>
static ICUResult PushChar(V& aVec, char16_t aCh) {
  if (!aVec.append(aCh)) {
    return Err(ICUError::OutOfMemory);
  }
  return Ok();
}

/**
 * Returns an ICU skeleton string representing the specified options.
 * http://unicode.org/reports/tr35/tr35-dates.html#Date_Field_Symbol_Table
 */
ICUResult ToICUSkeleton(const DateTimeFormat::ComponentsBag& aBag,
                        DateTimeFormat::SkeletonVector& aSkeleton) {
  // Create an ICU skeleton representing the specified aBag. See
  if (aBag.weekday) {
    switch (*aBag.weekday) {
      case DateTimeFormat::Text::Narrow:
        MOZ_TRY(PushString(aSkeleton, u"EEEEE"));
        break;
      case DateTimeFormat::Text::Short:
        MOZ_TRY(PushString(aSkeleton, u"E"));
        break;
      case DateTimeFormat::Text::Long:
        MOZ_TRY(PushString(aSkeleton, u"EEEE"));
    }
  }
  if (aBag.era) {
    switch (*aBag.era) {
      case DateTimeFormat::Text::Narrow:
        MOZ_TRY(PushString(aSkeleton, u"GGGGG"));
        break;
      case DateTimeFormat::Text::Short:
        // Use "GGG" instead of "G" to return the same results as other
        // browsers. This is exploiting the following ICU bug
        // <https://unicode-org.atlassian.net/browse/ICU-22138>. As soon as that
        // bug has been fixed, we can change this back to "G".
        //
        // In practice the bug only affects "G", so we only apply it for "G"
        // and not for other symbols like "B" or "z".
        MOZ_TRY(PushString(aSkeleton, u"GGG"));
        break;
      case DateTimeFormat::Text::Long:
        MOZ_TRY(PushString(aSkeleton, u"GGGG"));
        break;
    }
  }
  if (aBag.year) {
    switch (*aBag.year) {
      case DateTimeFormat::Numeric::TwoDigit:
        MOZ_TRY(PushString(aSkeleton, u"yy"));
        break;
      case DateTimeFormat::Numeric::Numeric:
        MOZ_TRY(PushString(aSkeleton, u"y"));
        break;
    }
  }
  if (aBag.month) {
    switch (*aBag.month) {
      case DateTimeFormat::Month::TwoDigit:
        MOZ_TRY(PushString(aSkeleton, u"MM"));
        break;
      case DateTimeFormat::Month::Numeric:
        MOZ_TRY(PushString(aSkeleton, u"M"));
        break;
      case DateTimeFormat::Month::Narrow:
        MOZ_TRY(PushString(aSkeleton, u"MMMMM"));
        break;
      case DateTimeFormat::Month::Short:
        MOZ_TRY(PushString(aSkeleton, u"MMM"));
        break;
      case DateTimeFormat::Month::Long:
        MOZ_TRY(PushString(aSkeleton, u"MMMM"));
        break;
    }
  }
  if (aBag.day) {
    switch (*aBag.day) {
      case DateTimeFormat::Numeric::TwoDigit:
        MOZ_TRY(PushString(aSkeleton, u"dd"));
        break;
      case DateTimeFormat::Numeric::Numeric:
        MOZ_TRY(PushString(aSkeleton, u"d"));
        break;
    }
  }

  // If hour12 and hourCycle are both present, hour12 takes precedence.
  char16_t hourSkeletonChar = 'j';
  if (aBag.hour12) {
    if (*aBag.hour12) {
      hourSkeletonChar = 'h';
    } else {
      hourSkeletonChar = 'H';
    }
  } else if (aBag.hourCycle) {
    switch (*aBag.hourCycle) {
      case DateTimeFormat::HourCycle::H11:
      case DateTimeFormat::HourCycle::H12:
        hourSkeletonChar = 'h';
        break;
      case DateTimeFormat::HourCycle::H23:
      case DateTimeFormat::HourCycle::H24:
        hourSkeletonChar = 'H';
        break;
    }
  }
  if (aBag.hour) {
    switch (*aBag.hour) {
      case DateTimeFormat::Numeric::TwoDigit:
        MOZ_TRY(PushChar(aSkeleton, hourSkeletonChar));
        MOZ_TRY(PushChar(aSkeleton, hourSkeletonChar));
        break;
      case DateTimeFormat::Numeric::Numeric:
        MOZ_TRY(PushChar(aSkeleton, hourSkeletonChar));
        break;
    }
  }
  // ICU requires that "B" is set after the "j" hour skeleton symbol.
  // https://unicode-org.atlassian.net/browse/ICU-20731
  if (aBag.dayPeriod) {
    switch (*aBag.dayPeriod) {
      case DateTimeFormat::Text::Narrow:
        MOZ_TRY(PushString(aSkeleton, u"BBBBB"));
        break;
      case DateTimeFormat::Text::Short:
        MOZ_TRY(PushString(aSkeleton, u"B"));
        break;
      case DateTimeFormat::Text::Long:
        MOZ_TRY(PushString(aSkeleton, u"BBBB"));
        break;
    }
  }
  if (aBag.minute) {
    switch (*aBag.minute) {
      case DateTimeFormat::Numeric::TwoDigit:
        MOZ_TRY(PushString(aSkeleton, u"mm"));
        break;
      case DateTimeFormat::Numeric::Numeric:
        MOZ_TRY(PushString(aSkeleton, u"m"));
        break;
    }
  }
  if (aBag.second) {
    switch (*aBag.second) {
      case DateTimeFormat::Numeric::TwoDigit:
        MOZ_TRY(PushString(aSkeleton, u"ss"));
        break;
      case DateTimeFormat::Numeric::Numeric:
        MOZ_TRY(PushString(aSkeleton, u"s"));
        break;
    }
  }
  if (aBag.fractionalSecondDigits) {
    switch (*aBag.fractionalSecondDigits) {
      case 1:
        MOZ_TRY(PushString(aSkeleton, u"S"));
        break;
      case 2:
        MOZ_TRY(PushString(aSkeleton, u"SS"));
        break;
      default:
        MOZ_TRY(PushString(aSkeleton, u"SSS"));
        break;
    }
  }
  if (aBag.timeZoneName) {
    switch (*aBag.timeZoneName) {
      case DateTimeFormat::TimeZoneName::Short:
        MOZ_TRY(PushString(aSkeleton, u"z"));
        break;
      case DateTimeFormat::TimeZoneName::Long:
        MOZ_TRY(PushString(aSkeleton, u"zzzz"));
        break;
      case DateTimeFormat::TimeZoneName::ShortOffset:
        MOZ_TRY(PushString(aSkeleton, u"O"));
        break;
      case DateTimeFormat::TimeZoneName::LongOffset:
        MOZ_TRY(PushString(aSkeleton, u"OOOO"));
        break;
      case DateTimeFormat::TimeZoneName::ShortGeneric:
        MOZ_TRY(PushString(aSkeleton, u"v"));
        break;
      case DateTimeFormat::TimeZoneName::LongGeneric:
        MOZ_TRY(PushString(aSkeleton, u"vvvv"));
        break;
    }
  }
  return Ok();
}

/* static */
Result<UniquePtr<DateTimeFormat>, ICUError>
DateTimeFormat::TryCreateFromComponents(
    Span<const char> aLocale, const DateTimeFormat::ComponentsBag& aBag,
    DateTimePatternGenerator* aDateTimePatternGenerator,
    Maybe<Span<const char16_t>> aTimeZoneOverride) {
  DateTimeFormat::SkeletonVector skeleton;
  MOZ_TRY(ToICUSkeleton(aBag, skeleton));
  return TryCreateFromSkeleton(aLocale, skeleton, aDateTimePatternGenerator,
                               aBag.hourCycle, aTimeZoneOverride);
}

/* static */
Result<UniquePtr<DateTimeFormat>, ICUError>
DateTimeFormat::TryCreateFromPattern(
    Span<const char> aLocale, Span<const char16_t> aPattern,
    Maybe<Span<const char16_t>> aTimeZoneOverride) {
  UErrorCode status = U_ZERO_ERROR;

  // The time zone is optional.
  int32_t tzIDLength = -1;
  const UChar* tzID = nullptr;
  if (aTimeZoneOverride) {
    tzIDLength = static_cast<int32_t>(aTimeZoneOverride->size());
    tzID = aTimeZoneOverride->data();
  }

  // Create the date formatter.
  UDateFormat* dateFormat = udat_open(
      UDAT_PATTERN, UDAT_PATTERN, IcuLocale(aLocale), tzID, tzIDLength,
      aPattern.data(), static_cast<int32_t>(aPattern.size()), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  MOZ_TRY(ApplyCalendarOverride(dateFormat));

  // The DateTimeFormat wrapper will control the life cycle of the ICU
  // dateFormat object.
  return UniquePtr<DateTimeFormat>(new DateTimeFormat(dateFormat));
}

/* static */
Result<UniquePtr<DateTimeFormat>, ICUError>
DateTimeFormat::TryCreateFromSkeleton(
    Span<const char> aLocale, Span<const char16_t> aSkeleton,
    DateTimePatternGenerator* aDateTimePatternGenerator,
    Maybe<DateTimeFormat::HourCycle> aHourCycle,
    Maybe<Span<const char16_t>> aTimeZoneOverride) {
  if (!aDateTimePatternGenerator) {
    return Err(ICUError::InternalError);
  }

  // Compute the best pattern for the skeleton.
  DateTimeFormat::PatternVector pattern;
  auto options = PatternMatchOptions(aSkeleton);
  MOZ_TRY(
      aDateTimePatternGenerator->GetBestPattern(aSkeleton, pattern, options));

  if (aHourCycle) {
    DateTimeFormat::ReplaceHourSymbol(pattern, *aHourCycle);
  }

  auto result =
      DateTimeFormat::TryCreateFromPattern(aLocale, pattern, aTimeZoneOverride);
  if (result.isErr()) {
    return Err(result.unwrapErr());
  }
  auto dateTimeFormat = result.unwrap();
  MOZ_TRY(dateTimeFormat->CacheSkeleton(aSkeleton));
  return dateTimeFormat;
}

ICUResult DateTimeFormat::CacheSkeleton(Span<const char16_t> aSkeleton) {
  if (mOriginalSkeleton.append(aSkeleton.Elements(), aSkeleton.Length())) {
    return Ok();
  }
  return Err(ICUError::OutOfMemory);
}

/* static */
Result<UniquePtr<Calendar>, ICUError> DateTimeFormat::CloneCalendar(
    double aUnixEpoch) const {
  UErrorCode status = U_ZERO_ERROR;
  UCalendar* calendarRaw = ucal_clone(udat_getCalendar(mDateFormat), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  auto calendar = MakeUnique<Calendar>(calendarRaw);

  MOZ_TRY(calendar->SetTimeInMs(aUnixEpoch));

  return calendar;
}

/**
 * ICU locale identifier consisting of a language and a region subtag.
 */
class LanguageRegionLocaleId {
  // unicode_language_subtag = alpha{2,3} | alpha{5,8} ;
  static constexpr size_t LanguageLength = 8;

  // unicode_region_subtag = (alpha{2} | digit{3}) ;
  static constexpr size_t RegionLength = 3;

  // Add +1 to account for the separator.
  static constexpr size_t LRLength = LanguageLength + RegionLength + 1;

  // Add +1 to zero terminate the string.
  char mLocale[LRLength + 1] = {};

  // Pointer to the start of the region subtag within |locale_|.
  char* mRegion = nullptr;

 public:
  LanguageRegionLocaleId(Span<const char> aLanguage,
                         Maybe<Span<const char>> aRegion);

  const char* languageRegion() const { return mLocale; }
  const char* region() const { return mRegion; }
};

LanguageRegionLocaleId::LanguageRegionLocaleId(
    Span<const char> aLanguage, Maybe<Span<const char>> aRegion) {
  MOZ_RELEASE_ASSERT(aLanguage.Length() <= LanguageLength);
  MOZ_RELEASE_ASSERT(!aRegion || aRegion->Length() <= RegionLength);

  size_t languageLength = aLanguage.Length();

  std::memcpy(mLocale, aLanguage.Elements(), languageLength);

  // ICU locale identifiers are separated by underscores.
  mLocale[languageLength] = '_';

  mRegion = mLocale + languageLength + 1;
  if (aRegion) {
    std::memcpy(mRegion, aRegion->Elements(), aRegion->Length());
  } else {
    // Use "001" (UN M.49 code for the World) as the fallback to match ICU.
    std::strcpy(mRegion, "001");
  }
}

/* static */
Result<DateTimeFormat::HourCyclesVector, ICUError>
DateTimeFormat::GetAllowedHourCycles(Span<const char> aLanguage,
                                     Maybe<Span<const char>> aRegion) {
  // ICU doesn't expose a public API to retrieve the hour cyles for a locale, so
  // we have to reconstruct |DateTimePatternGenerator::getAllowedHourFormats()|
  // using the public UResourceBundle API.
  //
  // The time data format is specified in UTS 35 at [1] and the data itself is
  // located at [2].
  //
  // [1] https://unicode.org/reports/tr35/tr35-dates.html#Time_Data
  // [2]
  // https://github.com/unicode-org/cldr/blob/master/common/supplemental/supplementalData.xml

  HourCyclesVector result;

  // Reserve space for the maximum number of hour cycles. This call always
  // succeeds because it matches the inline capacity. We can now infallibly
  // append all hour cycles to the vector.
  MOZ_ALWAYS_TRUE(result.reserve(HourCyclesVector::InlineLength));

  LanguageRegionLocaleId localeId(aLanguage, aRegion);

  // First open the "supplementalData" resource bundle.
  UErrorCode status = U_ZERO_ERROR;
  UResourceBundle* res = ures_openDirect(nullptr, "supplementalData", &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UResourceBundle, ures_close> closeRes(res);
  MOZ_ASSERT(ures_getType(res) == URES_TABLE);

  // Locate "timeDate" within the "supplementalData" resource bundle.
  UResourceBundle* timeData = ures_getByKey(res, "timeData", nullptr, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UResourceBundle, ures_close> closeTimeData(timeData);
  MOZ_ASSERT(ures_getType(timeData) == URES_TABLE);

  // Try to find a matching resource within "timeData". The two possible keys
  // into the "timeData" resource bundle are `language_region` and `region`.
  // Prefer `language_region` and otherwise fallback to `region`.
  UResourceBundle* hclocale =
      ures_getByKey(timeData, localeId.languageRegion(), nullptr, &status);
  if (status == U_MISSING_RESOURCE_ERROR) {
    status = U_ZERO_ERROR;
    hclocale = ures_getByKey(timeData, localeId.region(), nullptr, &status);
  }
  if (status == U_MISSING_RESOURCE_ERROR) {
    // Default to "h23" if no resource was found at all. This matches ICU.
    result.infallibleAppend(HourCycle::H23);
    return result;
  }
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UResourceBundle, ures_close> closeHcLocale(hclocale);
  MOZ_ASSERT(ures_getType(hclocale) == URES_TABLE);

  EnumSet<HourCycle> added{};

  auto addToResult = [&](const UChar* str, int32_t len) {
    // An hour cycle strings is one of "K", "h", "H", or "k"; optionally
    // followed by the suffix "b" or "B". We ignore the suffix because day
    // periods can't be expressed in the "hc" Unicode extension.
    MOZ_ASSERT(len == 1 || len == 2);

    // Default to "h23" for unsupported hour cycle strings.
    HourCycle hc = HourCycle::H23;
    switch (str[0]) {
      case 'K':
        hc = HourCycle::H11;
        break;
      case 'h':
        hc = HourCycle::H12;
        break;
      case 'H':
        hc = HourCycle::H23;
        break;
      case 'k':
        hc = HourCycle::H24;
        break;
    }

    // Add each unique hour cycle to the result array.
    if (!added.contains(hc)) {
      added += hc;

      result.infallibleAppend(hc);
    }
  };

  // Determine the preferred hour cycle for the locale.
  int32_t len = 0;
  const UChar* hc = ures_getStringByKey(hclocale, "preferred", &len, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  addToResult(hc, len);

  // Find any additionally allowed hour cycles of the locale.
  UResourceBundle* allowed =
      ures_getByKey(hclocale, "allowed", nullptr, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UResourceBundle, ures_close> closeAllowed(allowed);
  MOZ_ASSERT(ures_getType(allowed) == URES_ARRAY ||
             ures_getType(allowed) == URES_STRING);

  while (ures_hasNext(allowed)) {
    int32_t len = 0;
    const UChar* hc = ures_getNextString(allowed, &len, nullptr, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }
    addToResult(hc, len);
  }

  return result;
}

template <typename CharT>
static Result<Buffer<char>, ICUError> DuplicateChars(Span<CharT> aView) {
  auto chars = MakeUnique<char[]>(aView.Length() + 1);
  std::copy_n(aView.Elements(), aView.Length(), chars.get());
  chars[aView.Length()] = '\0';
  return Buffer{std::move(chars), aView.Length()};
}

static Result<Buffer<char>, ICUError> GetParentLocale(
    const UResourceBundle* aLocaleBundle) {
  UErrorCode status = U_ZERO_ERROR;

  // First check for an explicit parent locale using the "%%Parent" key.
  int32_t length = 0;
  const char16_t* parent =
      ures_getStringByKey(aLocaleBundle, "%%Parent", &length, &status);
  if (status == U_MISSING_RESOURCE_ERROR) {
    status = U_ZERO_ERROR;
    parent = nullptr;
  }
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  if (parent) {
    return DuplicateChars(Span{parent, size_t(length)});
  }

  // Retrieve the actual locale of the resource bundle.
  const char* locale =
      ures_getLocaleByType(aLocaleBundle, ULOC_ACTUAL_LOCALE, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  // Strip off the last subtag, if possible.
  if (const char* sep = std::strrchr(locale, '_')) {
    return DuplicateChars(Span{locale, size_t(sep - locale)});
  }

  // The parent locale of all locales is "root".
  if (std::strcmp(locale, "root") != 0) {
    static constexpr auto root = MakeStringSpan("root");
    return DuplicateChars(root);
  }

  // "root" itself doesn't have a parent locale.
  static constexpr auto empty = MakeStringSpan("");
  return DuplicateChars(empty);
}

static Result<Span<const char16_t>, ICUError> FindTimeSeparator(
    Span<const char> aRequestedLocale, Span<const char> aLocale,
    Span<const char> aNumberingSystem) {
  // We didn't find the numbering system. Retry using the default numbering
  // system "latn". (We don't use the default numbering system of the requested
  // locale to match ICU.)
  if (aLocale == MakeStringSpan("")) {
    return FindTimeSeparator(aRequestedLocale, aRequestedLocale, "latn");
  }

  // First open the resource bundle of the input locale.
  //
  // Note: ICU's resource API accepts both Unicode CLDR locale identifiers and
  // Unicode BCP 47 locale identifiers, so we don't have to convert the input
  // into a Unicode CLDR locale identifier.
  UErrorCode status = U_ZERO_ERROR;
  UResourceBundle* localeBundle =
      ures_open(nullptr, AssertNullTerminatedString(aLocale), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UResourceBundle, ures_close> closeLocaleBundle(localeBundle);

  do {
    // Search for the "NumberElements" table. Fall back to the parent locale if
    // no "NumberElements" table is present.
    UResourceBundle* numberElements =
        ures_getByKey(localeBundle, "NumberElements", nullptr, &status);
    if (status == U_MISSING_RESOURCE_ERROR) {
      break;
    }
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }
    ScopedICUObject<UResourceBundle, ures_close> closeNumberElements(
        numberElements);

    // Search for the table of the requested numbering system. Fall back to the
    // parent locale if no table was found.
    UResourceBundle* numberingSystem = ures_getByKey(
        numberElements, AssertNullTerminatedString(aNumberingSystem), nullptr,
        &status);
    if (status == U_MISSING_RESOURCE_ERROR) {
      break;
    }
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }
    ScopedICUObject<UResourceBundle, ures_close> closeNumberingSystem(
        numberingSystem);

    // Search for the "symbols" table. Fall back to the parent locale if no
    // "symbols" table is present.
    UResourceBundle* symbols =
        ures_getByKey(numberingSystem, "symbols", nullptr, &status);
    if (status == U_MISSING_RESOURCE_ERROR) {
      break;
    }
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }
    ScopedICUObject<UResourceBundle, ures_close> closeSymbols(symbols);

    // And finally look up the "timeSeparator" string in the "symbols" table. If
    // the string isn't present, fall back to the parent locale.
    int32_t length = 0;
    const UChar* str =
        ures_getStringByKey(symbols, "timeSeparator", &length, &status);
    if (status == U_MISSING_RESOURCE_ERROR) {
      break;
    }
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    Span<const char16_t> timeSeparator{str, size_t(length)};

    static constexpr auto defaultTimeSeparator = MakeStringSpan(u":");

    // Many numbering systems don't define their own symbols, but instead link
    // to the symbols for "latn" of the requested locale. The link is performed
    // through an alias entry like:
    // `symbols:alias{"/LOCALE/NumberElements/latn/symbols"}`
    //
    // ICU doesn't provide a public API to detect these alias entries, but
    // instead always automatically resolves the link. But that leads to
    // incorrectly using the symbols from the "root" locale instead of the
    // requested locale.
    //
    // Thankfully these alias entries are only present on the "root" locale. So
    // we are using this heuristic to detect alias entries:
    //
    // - If the resolved time separator is the default time separator ":".
    // - The current locale is "root".
    // - And the numbering system is neither "latn" nor "arab".
    // - Then search the time separator for "latn" of the requested locale.
    //
    // We have to exclude "arab", because it's also using ":" for the time
    // separator, but doesn't use an alias link to "latn".
    if (timeSeparator == defaultTimeSeparator &&
        aLocale == MakeStringSpan("root") &&
        aNumberingSystem != MakeStringSpan("latn") &&
        aNumberingSystem != MakeStringSpan("arab")) {
      return FindTimeSeparator(aRequestedLocale, aRequestedLocale,
                               MakeStringSpan("latn"));
    }

    return timeSeparator;
  } while (false);

  // Fall back to the parent locale.
  auto parent = GetParentLocale(localeBundle);
  if (parent.isErr()) {
    return parent.propagateErr();
  }
  return FindTimeSeparator(aRequestedLocale, parent.inspect().AsSpan(),
                           aNumberingSystem);
}

/* static */
Result<Span<const char16_t>, ICUError> DateTimeFormat::GetTimeSeparator(
    Span<const char> aLocale, Span<const char> aNumberingSystem) {
  return FindTimeSeparator(aLocale, aLocale, aNumberingSystem);
}

Result<DateTimeFormat::ComponentsBag, ICUError>
DateTimeFormat::ResolveComponents() {
  // Maps an ICU pattern string to a corresponding set of date-time components
  // and their values, and adds properties for these components to the result
  // object, which will be returned by the resolvedOptions method. For the
  // interpretation of ICU pattern characters, see
  // http://unicode.org/reports/tr35/tr35-dates.html#Date_Field_Symbol_Table

  DateTimeFormat::PatternVector pattern{};
  VectorToBufferAdaptor buffer(pattern);
  MOZ_TRY(GetPattern(buffer));

  DateTimeFormat::ComponentsBag bag{};

  using Text = DateTimeFormat::Text;
  using HourCycle = DateTimeFormat::HourCycle;
  using Numeric = DateTimeFormat::Numeric;
  using Month = DateTimeFormat::Month;

  auto text = Text::Long;
  auto numeric = Numeric::Numeric;
  auto month = Month::Long;
  uint8_t fractionalSecondDigits = 0;

  for (size_t i = 0, len = pattern.length(); i < len;) {
    char16_t c = pattern[i++];
    if (c == u'\'') {
      // Skip past string literals.
      while (i < len && pattern[i] != u'\'') {
        i++;
      }
      i++;
      continue;
    }

    // Count how many times the character is repeated.
    size_t count = 1;
    while (i < len && pattern[i] == c) {
      i++;
      count++;
    }

    // Determine the enum case of the field.
    switch (c) {
      // "text" cases
      case u'G':
      case u'E':
      case u'c':
      case u'B':
      case u'z':
      case u'O':
      case u'v':
      case u'V':
        if (count <= 3) {
          text = Text::Short;
        } else if (count == 4) {
          text = Text::Long;
        } else {
          text = Text::Narrow;
        }
        break;
      // "number" cases
      case u'y':
      case u'd':
      case u'h':
      case u'H':
      case u'm':
      case u's':
      case u'k':
      case u'K':
        if (count == 2) {
          numeric = Numeric::TwoDigit;
        } else {
          numeric = Numeric::Numeric;
        }
        break;
      // "numeric" cases
      case u'r':
      case u'U':
        // Both are mapped to numeric years.
        numeric = Numeric::Numeric;
        break;
      // "text & number" cases
      case u'M':
      case u'L':
        if (count == 1) {
          month = Month::Numeric;
        } else if (count == 2) {
          month = Month::TwoDigit;
        } else if (count == 3) {
          month = Month::Short;
        } else if (count == 4) {
          month = Month::Long;
        } else {
          month = Month::Narrow;
        }
        break;
      case u'S':
        fractionalSecondDigits = count;
        break;
      default: {
        // skip other pattern characters and literal text
      }
    }

    // Map ICU pattern characters back to the corresponding date-time
    // components of DateTimeFormat. See
    // http://unicode.org/reports/tr35/tr35-dates.html#Date_Field_Symbol_Table
    switch (c) {
      case u'E':
      case u'c':
        bag.weekday = Some(text);
        break;
      case u'G':
        bag.era = Some(text);
        break;
      case u'y':
      case u'r':
      case u'U':
        bag.year = Some(numeric);
        break;
      case u'M':
      case u'L':
        bag.month = Some(month);
        break;
      case u'd':
        bag.day = Some(numeric);
        break;
      case u'B':
        bag.dayPeriod = Some(text);
        break;
      case u'K':
        bag.hourCycle = Some(HourCycle::H11);
        bag.hour = Some(numeric);
        bag.hour12 = Some(true);
        break;
      case u'h':
        bag.hourCycle = Some(HourCycle::H12);
        bag.hour = Some(numeric);
        bag.hour12 = Some(true);
        break;
      case u'H':
        bag.hourCycle = Some(HourCycle::H23);
        bag.hour = Some(numeric);
        bag.hour12 = Some(false);
        break;
      case u'k':
        bag.hourCycle = Some(HourCycle::H24);
        bag.hour = Some(numeric);
        bag.hour12 = Some(false);
        break;
      case u'm':
        bag.minute = Some(numeric);
        break;
      case u's':
        bag.second = Some(numeric);
        break;
      case u'S':
        bag.fractionalSecondDigits = Some(fractionalSecondDigits);
        break;
      case u'z':
        switch (text) {
          case Text::Long:
            bag.timeZoneName = Some(TimeZoneName::Long);
            break;
          case Text::Short:
          case Text::Narrow:
            bag.timeZoneName = Some(TimeZoneName::Short);
            break;
        }
        break;
      case u'O':
        switch (text) {
          case Text::Long:
            bag.timeZoneName = Some(TimeZoneName::LongOffset);
            break;
          case Text::Short:
          case Text::Narrow:
            bag.timeZoneName = Some(TimeZoneName::ShortOffset);
            break;
        }
        break;
      case u'v':
      case u'V':
        switch (text) {
          case Text::Long:
            bag.timeZoneName = Some(TimeZoneName::LongGeneric);
            break;
          case Text::Short:
          case Text::Narrow:
            bag.timeZoneName = Some(TimeZoneName::ShortGeneric);
            break;
        }
        break;
    }
  }
  return bag;
}

const char* DateTimeFormat::ToString(
    DateTimeFormat::TimeZoneName aTimeZoneName) {
  switch (aTimeZoneName) {
    case TimeZoneName::Long:
      return "long";
    case TimeZoneName::Short:
      return "short";
    case TimeZoneName::ShortOffset:
      return "shortOffset";
    case TimeZoneName::LongOffset:
      return "longOffset";
    case TimeZoneName::ShortGeneric:
      return "shortGeneric";
    case TimeZoneName::LongGeneric:
      return "longGeneric";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::TimeZoneName");
}

const char* DateTimeFormat::ToString(DateTimeFormat::Month aMonth) {
  switch (aMonth) {
    case Month::Numeric:
      return "numeric";
    case Month::TwoDigit:
      return "2-digit";
    case Month::Long:
      return "long";
    case Month::Short:
      return "short";
    case Month::Narrow:
      return "narrow";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::Month");
}

const char* DateTimeFormat::ToString(DateTimeFormat::Text aText) {
  switch (aText) {
    case Text::Long:
      return "long";
    case Text::Short:
      return "short";
    case Text::Narrow:
      return "narrow";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::Text");
}

const char* DateTimeFormat::ToString(DateTimeFormat::Numeric aNumeric) {
  switch (aNumeric) {
    case Numeric::Numeric:
      return "numeric";
    case Numeric::TwoDigit:
      return "2-digit";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::Numeric");
}

const char* DateTimeFormat::ToString(DateTimeFormat::Style aStyle) {
  switch (aStyle) {
    case Style::Full:
      return "full";
    case Style::Long:
      return "long";
    case Style::Medium:
      return "medium";
    case Style::Short:
      return "short";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::Style");
}

const char* DateTimeFormat::ToString(DateTimeFormat::HourCycle aHourCycle) {
  switch (aHourCycle) {
    case HourCycle::H11:
      return "h11";
    case HourCycle::H12:
      return "h12";
    case HourCycle::H23:
      return "h23";
    case HourCycle::H24:
      return "h24";
  }
  MOZ_CRASH("Unexpected DateTimeFormat::HourCycle");
}

ICUResult DateTimeFormat::TryFormatToParts(
    UFieldPositionIterator* aFieldPositionIterator, size_t aSpanSize,
    DateTimePartVector& aParts) const {
  ScopedICUObject<UFieldPositionIterator, ufieldpositer_close> toClose(
      aFieldPositionIterator);

  size_t lastEndIndex = 0;
  auto AppendPart = [&](DateTimePartType type, size_t endIndex) {
    // For the part defined in FormatDateTimeToParts, it doesn't have ||Source||
    // property, we store Shared for simplicity,
    if (!aParts.emplaceBack(type, endIndex, DateTimePartSource::Shared)) {
      return false;
    }

    lastEndIndex = endIndex;
    return true;
  };

  int32_t fieldInt, beginIndexInt, endIndexInt;
  while ((fieldInt = ufieldpositer_next(aFieldPositionIterator, &beginIndexInt,
                                        &endIndexInt)) >= 0) {
    MOZ_ASSERT(beginIndexInt <= endIndexInt,
               "field iterator returning invalid range");

    size_t beginIndex = AssertedCast<size_t>(beginIndexInt);
    size_t endIndex = AssertedCast<size_t>(endIndexInt);

    // Technically this isn't guaranteed.  But it appears true in pratice,
    // and http://bugs.icu-project.org/trac/ticket/12024 is expected to
    // correct the documentation lapse.
    MOZ_ASSERT(lastEndIndex <= beginIndex,
               "field iteration didn't return fields in order start to "
               "finish as expected");

    DateTimePartType type =
        ConvertUFormatFieldToPartType(static_cast<UDateFormatField>(fieldInt));
    if (lastEndIndex < beginIndex) {
      if (!AppendPart(DateTimePartType::Literal, beginIndex)) {
        return Err(ICUError::InternalError);
      }
    }

    if (!AppendPart(type, endIndex)) {
      return Err(ICUError::InternalError);
    }
  }

  // Append any final literal.
  if (lastEndIndex < aSpanSize) {
    if (!AppendPart(DateTimePartType::Literal, aSpanSize)) {
      return Err(ICUError::InternalError);
    }
  }

  return Ok();
}

}  // namespace mozilla::intl
