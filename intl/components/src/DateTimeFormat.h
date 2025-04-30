/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_DateTimeFormat_h_
#define intl_components_DateTimeFormat_h_
#include <functional>
#include "unicode/udat.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/ICU4CGlue.h"
#include "mozilla/intl/ICUError.h"

#include "mozilla/intl/DateTimePart.h"
#include "mozilla/intl/DateTimePatternGenerator.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/Try.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Utf8.h"
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"

/*
 * To work around webcompat problems caused by Narrow No-Break Space in
 * formatted date/time output, where existing code on the web naively
 * assumes there will be a normal Space, we replace any occurrences of
 * U+202F in the formatted results with U+0020.
 *
 * The intention is to undo this hack once other major browsers are also
 * ready to ship with the updated (ICU72) i18n data that uses NNBSP.
 *
 * See https://bugzilla.mozilla.org/show_bug.cgi?id=1806042 for details,
 * and see DateIntervalFormat.cpp for the other piece of this hack.
 */
#define DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES 1

namespace mozilla::intl {

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
static inline bool IsSpecialSpace(char16_t c) {
  // NARROW NO-BREAK SPACE and THIN SPACE
  return c == 0x202F || c == 0x2009;
}
#endif

class Calendar;

/**
 * Intro to mozilla::intl::DateTimeFormat
 * ======================================
 *
 * This component is a Mozilla-focused API for the date formatting provided by
 * ICU. The methods internally call out to ICU4C. This is responsible for and
 * owns any resources opened through ICU, through RAII.
 *
 * The construction of a DateTimeFormat contains the majority of the cost
 * of the DateTimeFormat operation. DateTimeFormat::TryFormat should be
 * relatively inexpensive after the initial construction.
 *
 * This class supports creating from Styles (a fixed set of options) and from a
 * components bag (a list of components and their lengths).
 *
 * This API serves to back the ECMA-402 Intl.DateTimeFormat API.
 * https://tc39.es/ecma402/#datetimeformat-objects
 *
 *
 * ECMA-402 Intl.DateTimeFormat API and implementation details with ICU
 * skeletons and patterns.
 * ====================================================================
 *
 * Different locales have different ways to display dates using the same
 * basic components. For example, en-US might use "Sept. 24, 2012" while
 * fr-FR might use "24 Sept. 2012". The intent of Intl.DateTimeFormat is to
 * permit production of a format for the locale that best matches the
 * set of date-time components and their desired representation as specified
 * by the API client.
 *
 * ICU4C supports specification of date and time formats in three ways:
 *
 * 1) A style is just one of the identifiers FULL, LONG, MEDIUM, or SHORT.
 *    The date-time components included in each style and their representation
 *    are defined by ICU using CLDR locale data (CLDR is the Unicode
 *    Consortium's Common Locale Data Repository).
 *
 * 2) A skeleton is a string specifying which date-time components to include,
 *    and which representations to use for them. For example, "yyyyMMMMdd"
 *    specifies a year with at least four digits, a full month name, and a
 *    two-digit day. It does not specify in which order the components appear,
 *    how they are separated, the localized strings for textual components
 *    (such as weekday or month), whether the month is in format or
 *    stand-alone form¹, or the numbering system used for numeric components.
 *    All that information is filled in by ICU using CLDR locale data.
 *    ¹ The format form is the one used in formatted strings that include a
 *    day; the stand-alone form is used when not including days, e.g., in
 *    calendar headers. The two forms differ at least in some Slavic languages,
 *    e.g. Russian: "22 марта 2013 г." vs. "Март 2013".
 *
 * 3) A pattern is a string specifying which date-time components to include,
 *    in which order, with which separators, in which grammatical case. For
 *    example, "EEEE, d MMMM y" specifies the full localized weekday name,
 *    followed by comma and space, followed by the day, followed by space,
 *    followed by the full month name in format form, followed by space,
 *    followed by the full year. It
 *    still does not specify localized strings for textual components and the
 *    numbering system - these are determined by ICU using CLDR locale data or
 *    possibly API parameters.
 *
 * All actual formatting in ICU4C is done with patterns; styles and skeletons
 * have to be mapped to patterns before processing.
 *
 * The options of Intl.DateTimeFormat most closely correspond to ICU skeletons.
 * This implementation therefore converts DateTimeFormat options to ICU
 * skeletons, and then lets ICU map skeletons to actual ICU patterns. The
 * pattern may not directly correspond to what the skeleton requests, as the
 * mapper (UDateTimePatternGenerator) is constrained by the available locale
 * data for the locale.
 *
 * An ICU pattern represents the information of the following DateTimeFormat
 * internal properties described in the specification, which therefore don't
 * exist separately in the implementation:
 * - [[weekday]], [[era]], [[year]], [[month]], [[day]], [[hour]], [[minute]],
 *   [[second]], [[timeZoneName]]
 * - [[hour12]]
 * - [[hourCycle]]
 * - [[hourNo0]]
 * When needed for the resolvedOptions method, the resolveICUPattern function
 * queries the UDateFormat's internal pattern and then maps the it back to the
 * specified properties of the object returned by resolvedOptions.
 *
 * ICU date-time skeletons and patterns aren't fully documented in the ICU
 * documentation (see http://bugs.icu-project.org/trac/ticket/9627). The best
 * documentation at this point is in UTR 35:
 * http://unicode.org/reports/tr35/tr35-dates.html#Date_Format_Patterns
 *
 * Future support for ICU4X
 * ========================
 * This implementation exposes a components bag, and internally handles the
 * complexity of working with skeletons and patterns to generate the correct
 * results. In the future, if and when we switch to ICU4X, the complexities of
 * manipulating patterns will be able to be removed, as ICU4X will directly know
 * how to apply the components bag.
 */
class DateTimeFormat final {
 public:
  /**
   * The hour cycle for components.
   */
  enum class HourCycle {
    H11,
    H12,
    H23,
    H24,
  };

  /**
   * The style for dates or times.
   */
  enum class Style {
    Full,
    Long,
    Medium,
    Short,
  };

  /**
   * A bag of options to determine the length of the time and date styles. The
   * hour cycle can be overridden.
   */
  struct StyleBag {
    Maybe<Style> date = Nothing();
    Maybe<Style> time = Nothing();
    Maybe<HourCycle> hourCycle = Nothing();
    Maybe<bool> hour12 = Nothing();
  };

  /**
   * How to to display numeric components such as the year and the day.
   */
  enum class Numeric {
    Numeric,
    TwoDigit,
  };

  /**
   * How to display the text components, such as the weekday or day period.
   */
  enum class Text {
    Long,
    Short,
    Narrow,
  };

  /**
   * How to display the month.
   */
  enum class Month {
    Numeric,
    TwoDigit,
    Long,
    Short,
    Narrow,
  };

  /**
   * How to display the time zone name.
   */
  enum class TimeZoneName {
    Long,
    Short,
    ShortOffset,
    LongOffset,
    ShortGeneric,
    LongGeneric,
  };

  /**
   * Get static strings representing the enums. These match ECMA-402's resolved
   * options.
   * https://tc39.es/ecma402/#sec-intl.datetimeformat.prototype.resolvedoptions
   */
  static const char* ToString(DateTimeFormat::HourCycle aHourCycle);
  static const char* ToString(DateTimeFormat::Style aStyle);
  static const char* ToString(DateTimeFormat::Numeric aNumeric);
  static const char* ToString(DateTimeFormat::Text aText);
  static const char* ToString(DateTimeFormat::Month aMonth);
  static const char* ToString(DateTimeFormat::TimeZoneName aTimeZoneName);

  /**
   * A components bag specifies the components used to display a DateTime. Each
   * component can be styled individually, and ICU will attempt to create a best
   * match for a given locale.
   */
  struct ComponentsBag {
    Maybe<Text> era = Nothing();
    Maybe<Numeric> year = Nothing();
    Maybe<Month> month = Nothing();
    Maybe<Numeric> day = Nothing();
    Maybe<Text> weekday = Nothing();
    Maybe<Numeric> hour = Nothing();
    Maybe<Numeric> minute = Nothing();
    Maybe<Numeric> second = Nothing();
    Maybe<TimeZoneName> timeZoneName = Nothing();
    Maybe<bool> hour12 = Nothing();
    Maybe<HourCycle> hourCycle = Nothing();
    Maybe<Text> dayPeriod = Nothing();
    Maybe<uint8_t> fractionalSecondDigits = Nothing();
  };

  // Do not allow copy as this class owns the ICU resource. Move is not
  // currently implemented, but a custom move operator could be created if
  // needed.
  DateTimeFormat(const DateTimeFormat&) = delete;
  DateTimeFormat& operator=(const DateTimeFormat&) = delete;

  // mozilla::Vector can avoid heap allocations for small transient buffers.
  using PatternVector = Vector<char16_t, 128>;
  using SkeletonVector = Vector<char16_t, 16>;

  /**
   * Create a DateTimeFormat from styles.
   *
   * The "style" model uses different options for formatting a date or time
   * based on how the result will be styled, rather than picking specific
   * fields or lengths.
   *
   * Takes an optional time zone which will override the user's default
   * time zone. This is a UTF-16 string that takes the form "GMT±hh:mm", or
   * an IANA time zone identifier, e.g. "America/Chicago".
   */
  static Result<UniquePtr<DateTimeFormat>, ICUError> TryCreateFromStyle(
      Span<const char> aLocale, const StyleBag& aStyleBag,
      DateTimePatternGenerator* aDateTimePatternGenerator,
      Maybe<Span<const char16_t>> aTimeZoneOverride = Nothing{});

 private:
  /**
   * Create a DateTimeFormat from a UTF-16 skeleton.
   *
   * A skeleton is an unordered list of fields that are used to find an
   * appropriate date time format pattern. Example skeletons would be "yMd",
   * "yMMMd", "EBhm". If the skeleton includes string literals or other
   * information, it will be discarded when matching against skeletons.
   *
   * Takes an optional time zone which will override the user's default
   * time zone. This is a string that takes the form "GMT±hh:mm", or
   * an IANA time zone identifier, e.g. "America/Chicago".
   */
  static Result<UniquePtr<DateTimeFormat>, ICUError> TryCreateFromSkeleton(
      Span<const char> aLocale, Span<const char16_t> aSkeleton,
      DateTimePatternGenerator* aDateTimePatternGenerator,
      Maybe<DateTimeFormat::HourCycle> aHourCycle,
      Maybe<Span<const char16_t>> aTimeZoneOverride);

 public:
  /**
   * Create a DateTimeFormat from a ComponentsBag.
   *
   * See the ComponentsBag for additional documentation.
   *
   * Takes an optional time zone which will override the user's default
   * time zone. This is a string that takes the form "GMT±hh:mm", or
   * an IANA time zone identifier, e.g. "America/Chicago".
   */
  static Result<UniquePtr<DateTimeFormat>, ICUError> TryCreateFromComponents(
      Span<const char> aLocale, const ComponentsBag& bag,
      DateTimePatternGenerator* aDateTimePatternGenerator,
      Maybe<Span<const char16_t>> aTimeZoneOverride = Nothing{});

  /**
   * Create a DateTimeFormat from a raw pattern.
   *
   * Warning: This method should not be added to new code. In the near future we
   * plan to remove it.
   */
  static Result<UniquePtr<DateTimeFormat>, ICUError> TryCreateFromPattern(
      Span<const char> aLocale, Span<const char16_t> aPattern,
      Maybe<Span<const char16_t>> aTimeZoneOverride = Nothing{});

  /**
   * Use the format settings to format a date time into a string. The non-null
   * terminated string will be placed into the provided buffer. The idea behind
   * this API is that the constructor is expensive, and then the format
   * operation is cheap.
   *
   * aUnixEpoch is the number of milliseconds since 1 January 1970, UTC.
   */
  template <typename B>
  ICUResult TryFormat(double aUnixEpoch, B& aBuffer) const {
    static_assert(
        std::is_same_v<typename B::CharType, unsigned char> ||
            std::is_same_v<typename B::CharType, char> ||
            std::is_same_v<typename B::CharType, char16_t>,
        "The only buffer CharTypes supported by DateTimeFormat are char "
        "(for UTF-8 support) and char16_t (for UTF-16 support).");

    if constexpr (std::is_same_v<typename B::CharType, char> ||
                  std::is_same_v<typename B::CharType, unsigned char>) {
      // The output buffer is UTF-8, but ICU uses UTF-16 internally.

      // Write the formatted date into the u16Buffer.
      PatternVector u16Vec;

      auto result = FillBufferWithICUCall(
          u16Vec, [this, &aUnixEpoch](UChar* target, int32_t length,
                                      UErrorCode* status) {
            return udat_format(mDateFormat, aUnixEpoch, target, length,
                               /* UFieldPosition* */ nullptr, status);
          });
      if (result.isErr()) {
        return result;
      }

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
      for (auto& c : u16Vec) {
        if (IsSpecialSpace(c)) {
          c = ' ';
        }
      }
#endif

      if (!FillBuffer(u16Vec, aBuffer)) {
        return Err(ICUError::OutOfMemory);
      }
      return Ok{};
    } else {
      static_assert(std::is_same_v<typename B::CharType, char16_t>);

      // The output buffer is UTF-16. ICU can output directly into this buffer.
      auto result = FillBufferWithICUCall(
          aBuffer, [&](UChar* target, int32_t length, UErrorCode* status) {
            return udat_format(mDateFormat, aUnixEpoch, target, length, nullptr,
                               status);
          });
      if (result.isErr()) {
        return result;
      }

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
      for (auto& c : Span(aBuffer.data(), aBuffer.length())) {
        if (IsSpecialSpace(c)) {
          c = ' ';
        }
      }
#endif

      return Ok{};
    }
  };

  /**
   * Format the Unix epoch time into a DateTimePartVector.
   *
   * The caller has to create the buffer and the vector and pass to this method.
   * The formatted string will be stored in the buffer and formatted parts in
   * the vector.
   *
   * aUnixEpoch is the number of milliseconds since 1 January 1970, UTC.
   *
   * See:
   * https://tc39.es/ecma402/#sec-formatdatetimetoparts
   */
  template <typename B>
  ICUResult TryFormatToParts(double aUnixEpoch, B& aBuffer,
                             DateTimePartVector& aParts) const {
    static_assert(std::is_same_v<typename B::CharType, char16_t>,
                  "Only char16_t is supported (for UTF-16 support) now.");

    UErrorCode status = U_ZERO_ERROR;
    UFieldPositionIterator* fpositer = ufieldpositer_open(&status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    auto result = FillBufferWithICUCall(
        aBuffer, [this, aUnixEpoch, fpositer](UChar* chars, int32_t size,
                                              UErrorCode* status) {
          return udat_formatForFields(mDateFormat, aUnixEpoch, chars, size,
                                      fpositer, status);
        });
    if (result.isErr()) {
      ufieldpositer_close(fpositer);
      return result.propagateErr();
    }

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
    for (auto& c : Span(aBuffer.data(), aBuffer.length())) {
      if (IsSpecialSpace(c)) {
        c = ' ';
      }
    }
#endif

    return TryFormatToParts(fpositer, aBuffer.length(), aParts);
  }

  /**
   * Copies the pattern for the current DateTimeFormat to a buffer.
   *
   * Warning: This method should not be added to new code. In the near future we
   * plan to remove it.
   */
  template <typename B>
  ICUResult GetPattern(B& aBuffer) const {
    return FillBufferWithICUCall(
        aBuffer, [&](UChar* target, int32_t length, UErrorCode* status) {
          return udat_toPattern(mDateFormat, /* localized*/ false, target,
                                length, status);
        });
  }

  /**
   * Copies the skeleton that was used to generate the current DateTimeFormat to
   * the given buffer. If no skeleton was used, then a skeleton is generated
   * from the resolved pattern. Note that going from skeleton -> resolved
   * pattern -> skeleton is not a 1:1 mapping, as the resolved pattern can
   * contain different symbols than the requested skeleton.
   *
   * Warning: This method should not be added to new code. In the near future we
   * plan to remove it.
   */
  template <typename B>
  ICUResult GetOriginalSkeleton(B& aBuffer) {
    static_assert(std::is_same_v<typename B::CharType, char16_t>);
    if (mOriginalSkeleton.length() == 0) {
      // Generate a skeleton from the resolved pattern, there was no originally
      // cached skeleton.
      PatternVector pattern{};
      VectorToBufferAdaptor buffer(pattern);
      MOZ_TRY(GetPattern(buffer));

      VectorToBufferAdaptor skeleton(mOriginalSkeleton);
      MOZ_TRY(DateTimePatternGenerator::GetSkeleton(pattern, skeleton));
    }

    if (!FillBuffer(mOriginalSkeleton, aBuffer)) {
      return Err(ICUError::OutOfMemory);
    }
    return Ok();
  }

  /**
   * Determines the resolved components for the current DateTimeFormat.
   *
   * When a DateTimeFormat is created, even from a components bag, the resolved
   * formatter may tweak the resolved components depending on the configuration
   * and the locale.
   *
   * For the implementation, with ICU4C, this takes a string pattern and maps it
   * back to a ComponentsBag.
   */
  Result<ComponentsBag, ICUError> ResolveComponents();

  ~DateTimeFormat();

  /**
   * Clones the Calendar from a DateTimeFormat, and sets its time with the
   * relative milliseconds since 1 January 1970, UTC.
   */
  Result<UniquePtr<Calendar>, ICUError> CloneCalendar(double aUnixEpoch) const;

  /**
   * Return the hour cycle used in the input pattern or Nothing if none was
   * found.
   */
  static Maybe<DateTimeFormat::HourCycle> HourCycleFromPattern(
      Span<const char16_t> aPattern);

  using HourCyclesVector = Vector<HourCycle, 4>;

  /**
   * Returns the allowed hour cycles for the input locale.
   *
   * NOTE: This function currently takes a language subtag and an optional
   * region subtag. This is a restriction until bug 1719746 has migrated
   * language tag processing into the unified Intl component. After bug 1719746,
   * this function should be changed to accept a single locale tag.
   */
  static Result<HourCyclesVector, ICUError> GetAllowedHourCycles(
      Span<const char> aLanguage, Maybe<Span<const char>> aRegion);

  /**
   * Returns an iterator over all supported date-time formatter locales.
   *
   * The returned strings are ICU locale identifiers and NOT BCP 47 language
   * tags.
   *
   * Also see <https://unicode-org.github.io/icu/userguide/locale>.
   */
  static auto GetAvailableLocales() {
    return AvailableLocalesEnumeration<udat_countAvailable,
                                       udat_getAvailable>();
  }

  /**
   * Return the time separator for the given locale and numbering system.
   */
  template <typename B>
  static ICUResult GetTimeSeparator(Span<const char> aLocale,
                                    Span<const char> aNumberingSystem,
                                    B& aBuffer) {
    static_assert(std::is_same_v<typename B::CharType, char16_t>);
    auto separator = GetTimeSeparator(aLocale, aNumberingSystem);
    if (separator.isErr()) {
      return separator.propagateErr();
    }
    if (!FillBuffer(separator.unwrap(), aBuffer)) {
      return Err(ICUError::OutOfMemory);
    }
    return Ok();
  }

 private:
  explicit DateTimeFormat(UDateFormat* aDateFormat);

  ICUResult CacheSkeleton(Span<const char16_t> aSkeleton);

  ICUResult TryFormatToParts(UFieldPositionIterator* aFieldPositionIterator,
                             size_t aSpanSize,
                             DateTimePartVector& aParts) const;
  /**
   * Replaces all hour pattern characters in |patternOrSkeleton| to use the
   * matching hour representation for |hourCycle|.
   */
  static void ReplaceHourSymbol(Span<char16_t> aPatternOrSkeleton,
                                DateTimeFormat::HourCycle aHourCycle);

  /**
   * Find a matching pattern using the requested hour-12 options.
   *
   * This function is needed to work around the following two issues.
   * - https://unicode-org.atlassian.net/browse/ICU-21023
   * - https://unicode-org.atlassian.net/browse/CLDR-13425
   *
   * We're currently using a relatively simple workaround, which doesn't give
   * the most accurate results. For example:
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
   * |DateTimePatternGenerator::GetBestPattern| to find an equivalent pattern
   * for "h23", we'll end up with the pattern "MMMM d, y, HH:mm:ss z", so the
   * combinator element " 'at' " was lost in the process.
   */
  static ICUResult FindPatternWithHourCycle(
      DateTimePatternGenerator& aDateTimePatternGenerator,
      DateTimeFormat::PatternVector& aPattern, bool aHour12,
      DateTimeFormat::SkeletonVector& aSkeleton);

  static Result<Span<const char16_t>, ICUError> GetTimeSeparator(
      Span<const char> aLocale, Span<const char> aNumberingSystem);

  UDateFormat* mDateFormat = nullptr;

  SkeletonVector mOriginalSkeleton;
};

}  // namespace mozilla::intl

#endif
