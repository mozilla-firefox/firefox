/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Intl.DateTimeFormat implementation. */

#include "builtin/intl/DateTimeFormat.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateIntervalFormat.h"
#include "mozilla/intl/DateTimeFormat.h"
#include "mozilla/intl/DateTimePart.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/TimeZone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/UsingEnum.h"

#include "builtin/Array.h"
#include "builtin/Date.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LanguageTag.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/Packed.h"
#include "builtin/intl/ParameterNegotiation.h"
#include "builtin/intl/SharedIntlData.h"
#include "builtin/temporal/Calendar.h"
#include "builtin/temporal/Instant.h"
#include "builtin/temporal/PlainDate.h"
#include "builtin/temporal/PlainDateTime.h"
#include "builtin/temporal/PlainMonthDay.h"
#include "builtin/temporal/PlainTime.h"
#include "builtin/temporal/PlainYearMonth.h"
#include "builtin/temporal/Temporal.h"
#include "builtin/temporal/TemporalParser.h"
#include "builtin/temporal/TimeZone.h"
#include "builtin/temporal/ZonedDateTime.h"
#include "gc/GCContext.h"
#include "js/Date.h"
#include "js/experimental/Intl.h"     // JS::AddMozDateTimeFormatConstructor
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/GCAPI.h"
#include "js/PropertyAndElement.h"  // JS_DefineFunctions, JS_DefineProperties
#include "js/PropertySpec.h"
#include "js/StableStringChars.h"
#include "js/Wrapper.h"
#include "vm/DateTime.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/PlainObject.h"  // js::PlainObject
#include "vm/Runtime.h"
#include "vm/Warnings.h"

#include "vm/GeckoProfiler-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::intl;
using namespace js::temporal;

using JS::ClippedTime;

const JSClassOps DateTimeFormatObject::classOps_ = {
    nullptr,                         // addProperty
    nullptr,                         // delProperty
    nullptr,                         // enumerate
    nullptr,                         // newEnumerate
    nullptr,                         // resolve
    nullptr,                         // mayResolve
    DateTimeFormatObject::finalize,  // finalize
    nullptr,                         // call
    nullptr,                         // construct
    nullptr,                         // trace
};

const JSClass DateTimeFormatObject::class_ = {
    "Intl.DateTimeFormat",
    JSCLASS_HAS_RESERVED_SLOTS(DateTimeFormatObject::SLOT_COUNT) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_DateTimeFormat) |
        JSCLASS_BACKGROUND_FINALIZE,
    &DateTimeFormatObject::classOps_,
    &DateTimeFormatObject::classSpec_,
};

const JSClass& DateTimeFormatObject::protoClass_ = PlainObject::class_;

static bool dateTimeFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                              Value* vp);

static bool dateTimeFormat_format(JSContext* cx, unsigned argc, Value* vp);

static bool dateTimeFormat_formatToParts(JSContext* cx, unsigned argc,
                                         Value* vp);

static bool dateTimeFormat_formatRange(JSContext* cx, unsigned argc, Value* vp);

static bool dateTimeFormat_formatRangeToParts(JSContext* cx, unsigned argc,
                                              Value* vp);

static bool dateTimeFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                           Value* vp);

static bool dateTimeFormat_toSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setString(cx->names().DateTimeFormat);
  return true;
}

static const JSFunctionSpec dateTimeFormat_static_methods[] = {
    JS_FN("supportedLocalesOf", dateTimeFormat_supportedLocalesOf, 1, 0),
    JS_FS_END,
};

static const JSFunctionSpec dateTimeFormat_methods[] = {
    JS_FN("resolvedOptions", dateTimeFormat_resolvedOptions, 0, 0),
    JS_FN("formatToParts", dateTimeFormat_formatToParts, 1, 0),
    JS_FN("formatRange", dateTimeFormat_formatRange, 2, 0),
    JS_FN("formatRangeToParts", dateTimeFormat_formatRangeToParts, 2, 0),
    JS_FN("toSource", dateTimeFormat_toSource, 0, 0),
    JS_FS_END,
};

static const JSPropertySpec dateTimeFormat_properties[] = {
    JS_PSG("format", dateTimeFormat_format, 0),
    JS_STRING_SYM_PS(toStringTag, "Intl.DateTimeFormat", JSPROP_READONLY),
    JS_PS_END,
};

static bool DateTimeFormat(JSContext* cx, unsigned argc, Value* vp);

const ClassSpec DateTimeFormatObject::classSpec_ = {
    GenericCreateConstructor<DateTimeFormat, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<DateTimeFormatObject>,
    dateTimeFormat_static_methods,
    nullptr,
    dateTimeFormat_methods,
    dateTimeFormat_properties,
    nullptr,
    ClassSpec::DontDefineConstructor,
};

struct js::intl::DateTimeFormatOptions {
  enum class Required : int8_t { Any, Date, Time };
  Required required = Required::Any;

  enum class Defaults : int8_t { Date, Time, All };
  Defaults defaults = Defaults::Date;

  using HourCycle = mozilla::intl::DateTimeFormat::HourCycle;
  mozilla::Maybe<HourCycle> hourCycle{};

  mozilla::Maybe<bool> hour12{};

  using DateStyle = mozilla::intl::DateTimeFormat::Style;
  mozilla::Maybe<DateStyle> dateStyle{};

  using TimeStyle = mozilla::intl::DateTimeFormat::Style;
  mozilla::Maybe<TimeStyle> timeStyle{};

  // Components of date and time formats
  //
  // https://tc39.es/ecma402/#table-datetimeformat-components

  using Weekday = mozilla::intl::DateTimeFormat::Text;
  mozilla::Maybe<Weekday> weekday{};

  using Era = mozilla::intl::DateTimeFormat::Text;
  mozilla::Maybe<Era> era{};

  using Year = mozilla::intl::DateTimeFormat::Numeric;
  mozilla::Maybe<Year> year{};

  using Month = mozilla::intl::DateTimeFormat::Month;
  mozilla::Maybe<Month> month{};

  using Day = mozilla::intl::DateTimeFormat::Numeric;
  mozilla::Maybe<Day> day{};

  using DayPeriod = mozilla::intl::DateTimeFormat::Text;
  mozilla::Maybe<DayPeriod> dayPeriod{};

  using Hour = mozilla::intl::DateTimeFormat::Numeric;
  mozilla::Maybe<Hour> hour{};

  using Minute = mozilla::intl::DateTimeFormat::Numeric;
  mozilla::Maybe<Minute> minute{};

  using Second = mozilla::intl::DateTimeFormat::Numeric;
  mozilla::Maybe<Second> second{};

  mozilla::Maybe<int8_t> fractionalSecondDigits{};

  using TimeZoneName = mozilla::intl::DateTimeFormat::TimeZoneName;
  mozilla::Maybe<TimeZoneName> timeZoneName{};
};

struct PackedDateTimeFormatOptions {
  using RawValue = uint64_t;

  using RequiredField =
      packed::EnumField<RawValue, DateTimeFormatOptions::Required::Any,
                        DateTimeFormatOptions::Required::Time>;

  using DefaultsField =
      packed::EnumField<RequiredField, DateTimeFormatOptions::Defaults::Date,
                        DateTimeFormatOptions::Defaults::All>;

  using HourCycleField =
      packed::OptionalEnumField<DefaultsField,
                                DateTimeFormatOptions::HourCycle::H11,
                                DateTimeFormatOptions::HourCycle::H24>;

  using Hour12Field = packed::OptionalBooleanField<HourCycleField>;

  using DateStyleField =
      packed::OptionalEnumField<Hour12Field,
                                DateTimeFormatOptions::DateStyle::Full,
                                DateTimeFormatOptions::DateStyle::Short>;

  using TimeStyleField =
      packed::OptionalEnumField<DateStyleField,
                                DateTimeFormatOptions::TimeStyle::Full,
                                DateTimeFormatOptions::TimeStyle::Short>;

  using WeekdayField =
      packed::OptionalEnumField<TimeStyleField,
                                DateTimeFormatOptions::Weekday::Long,
                                DateTimeFormatOptions::Weekday::Narrow>;

  using EraField =
      packed::OptionalEnumField<WeekdayField, DateTimeFormatOptions::Era::Long,
                                DateTimeFormatOptions::Era::Narrow>;

  using YearField =
      packed::OptionalEnumField<EraField, DateTimeFormatOptions::Year::Numeric,
                                DateTimeFormatOptions::Year::TwoDigit>;

  using MonthField =
      packed::OptionalEnumField<YearField,
                                DateTimeFormatOptions::Month::Numeric,
                                DateTimeFormatOptions::Month::Narrow>;

  using DayField =
      packed::OptionalEnumField<MonthField, DateTimeFormatOptions::Day::Numeric,
                                DateTimeFormatOptions::Day::TwoDigit>;

  using DayPeriodField =
      packed::OptionalEnumField<DayField,
                                DateTimeFormatOptions::DayPeriod::Long,
                                DateTimeFormatOptions::DayPeriod::Narrow>;

  using HourField =
      packed::OptionalEnumField<DayPeriodField,
                                DateTimeFormatOptions::Hour::Numeric,
                                DateTimeFormatOptions::Hour::TwoDigit>;

  using MinuteField =
      packed::OptionalEnumField<HourField,
                                DateTimeFormatOptions::Minute::Numeric,
                                DateTimeFormatOptions::Minute::TwoDigit>;

  using SecondField =
      packed::OptionalEnumField<MinuteField,
                                DateTimeFormatOptions::Second::Numeric,
                                DateTimeFormatOptions::Second::TwoDigit>;

  using FractionalSecondDigitsField =
      packed::RangeField<SecondField, int8_t, 0, 3>;

  using TimeZoneNameField = packed::OptionalEnumField<
      FractionalSecondDigitsField, DateTimeFormatOptions::TimeZoneName::Long,
      DateTimeFormatOptions::TimeZoneName::LongGeneric>;

  using PackedValue = packed::PackedValue<TimeZoneNameField>;

  static auto pack(const DateTimeFormatOptions& options) {
    RawValue rawValue =
        RequiredField::pack(options.required) |
        DefaultsField::pack(options.defaults) |
        HourCycleField::pack(options.hourCycle) |
        Hour12Field::pack(options.hour12) |
        DateStyleField::pack(options.dateStyle) |
        TimeStyleField::pack(options.timeStyle) |
        WeekdayField::pack(options.weekday) | EraField::pack(options.era) |
        YearField::pack(options.year) | MonthField::pack(options.month) |
        DayField::pack(options.day) | DayPeriodField::pack(options.dayPeriod) |
        HourField::pack(options.hour) | MinuteField::pack(options.minute) |
        SecondField::pack(options.second) |
        FractionalSecondDigitsField::pack(
            options.fractionalSecondDigits.valueOr(0)) |
        TimeZoneNameField::pack(options.timeZoneName);
    return PackedValue::toValue(rawValue);
  }

  static auto unpack(JS::Value value) {
    auto maybeFractionalDigits = [](int8_t digits) -> mozilla::Maybe<int8_t> {
      return digits > 0 ? mozilla::Some(digits) : mozilla::Nothing();
    };

    RawValue rawValue = PackedValue::fromValue(value);
    return DateTimeFormatOptions{
        .required = RequiredField::unpack(rawValue),
        .defaults = DefaultsField::unpack(rawValue),
        .hourCycle = HourCycleField::unpack(rawValue),
        .hour12 = Hour12Field::unpack(rawValue),
        .dateStyle = DateStyleField::unpack(rawValue),
        .timeStyle = TimeStyleField::unpack(rawValue),
        .weekday = WeekdayField::unpack(rawValue),
        .era = EraField::unpack(rawValue),
        .year = YearField::unpack(rawValue),
        .month = MonthField::unpack(rawValue),
        .day = DayField::unpack(rawValue),
        .dayPeriod = DayPeriodField::unpack(rawValue),
        .hour = HourField::unpack(rawValue),
        .minute = MinuteField::unpack(rawValue),
        .second = SecondField::unpack(rawValue),
        .fractionalSecondDigits = maybeFractionalDigits(
            FractionalSecondDigitsField::unpack(rawValue)),
        .timeZoneName = TimeZoneNameField::unpack(rawValue),
    };
  }
};

DateTimeFormatOptions js::intl::DateTimeFormatObject::getOptions() const {
  const auto& slot = getFixedSlot(OPTIONS_SLOT);
  if (slot.isUndefined()) {
    return {};
  }
  return PackedDateTimeFormatOptions::unpack(slot);
}

void js::intl::DateTimeFormatObject::setOptions(
    const DateTimeFormatOptions& options) {
  setFixedSlot(OPTIONS_SLOT, PackedDateTimeFormatOptions::pack(options));
}

static constexpr std::string_view HourCycleToString(
    DateTimeFormatOptions::HourCycle hourCycle) {
  MOZ_USING_ENUM(DateTimeFormatOptions::HourCycle, H11, H12, H23, H24);
  switch (hourCycle) {
    case H11:
      return "h11";
    case H12:
      return "h12";
    case H23:
      return "h23";
    case H24:
      return "h24";
  }
  MOZ_CRASH("invalid date time format hour cycle");
}

template <typename DateOrTimeStyle>
static constexpr std::string_view DateOrTimeStyleToString(
    DateOrTimeStyle dateOrTimeStyle) {
  switch (dateOrTimeStyle) {
    case DateOrTimeStyle::Full:
      return "full";
    case DateOrTimeStyle::Long:
      return "long";
    case DateOrTimeStyle::Medium:
      return "medium";
    case DateOrTimeStyle::Short:
      return "short";
  }
  MOZ_CRASH("invalid date time format date or time style");
}

static constexpr std::string_view DateStyleToString(
    DateTimeFormatOptions::DateStyle dateStyle) {
  return DateOrTimeStyleToString(dateStyle);
}

static constexpr std::string_view TimeStyleToString(
    DateTimeFormatOptions::TimeStyle timeStyle) {
  return DateOrTimeStyleToString(timeStyle);
}

template <typename TextComponent>
static constexpr std::string_view TextComponentToString(
    TextComponent textComponent) {
  switch (textComponent) {
    case TextComponent::Narrow:
      return "narrow";
    case TextComponent::Short:
      return "short";
    case TextComponent::Long:
      return "long";
  }
  MOZ_CRASH("invalid date time format text component");
}

template <typename NumericComponent>
static constexpr std::string_view NumericComponentToString(
    NumericComponent numericComponent) {
  switch (numericComponent) {
    case NumericComponent::TwoDigit:
      return "2-digit";
    case NumericComponent::Numeric:
      return "numeric";
  }
  MOZ_CRASH("invalid date time format numeric component");
}

static constexpr std::string_view WeekdayToString(
    DateTimeFormatOptions::Weekday weekday) {
  return TextComponentToString(weekday);
}

static constexpr std::string_view EraToString(DateTimeFormatOptions::Era era) {
  return TextComponentToString(era);
}

static constexpr std::string_view YearToString(
    DateTimeFormatOptions::Year year) {
  return NumericComponentToString(year);
}

static constexpr std::string_view MonthToString(
    DateTimeFormatOptions::Month month) {
  MOZ_USING_ENUM(DateTimeFormatOptions::Month, TwoDigit, Numeric, Narrow, Short,
                 Long);
  switch (month) {
    case TwoDigit:
      return "2-digit";
    case Numeric:
      return "numeric";
    case Narrow:
      return "narrow";
    case Short:
      return "short";
    case Long:
      return "long";
  }
  MOZ_CRASH("invalid date time format month");
}

static constexpr std::string_view DayToString(DateTimeFormatOptions::Day day) {
  return NumericComponentToString(day);
}

static constexpr std::string_view DayPeriodToString(
    DateTimeFormatOptions::DayPeriod dayPeriod) {
  return TextComponentToString(dayPeriod);
}

static constexpr std::string_view HourToString(
    DateTimeFormatOptions::Hour hour) {
  return NumericComponentToString(hour);
}

static constexpr std::string_view MinuteToString(
    DateTimeFormatOptions::Minute minute) {
  return NumericComponentToString(minute);
}

static constexpr std::string_view SecondToString(
    DateTimeFormatOptions::Second second) {
  return NumericComponentToString(second);
}

static constexpr std::string_view TimeZoneNameToString(
    DateTimeFormatOptions::TimeZoneName timeZoneName) {
  MOZ_USING_ENUM(DateTimeFormatOptions::TimeZoneName, Short, Long, ShortOffset,
                 LongOffset, ShortGeneric, LongGeneric);
  switch (timeZoneName) {
    case Short:
      return "short";
    case Long:
      return "long";
    case ShortOffset:
      return "shortOffset";
    case LongOffset:
      return "longOffset";
    case ShortGeneric:
      return "shortGeneric";
    case LongGeneric:
      return "longGeneric";
  }
  MOZ_CRASH("invalid date time format time zone name");
}

enum class FormatMatcher { Basic, BestFit };

static constexpr std::string_view FormatMatcherToString(
    FormatMatcher formatMatcher) {
  MOZ_USING_ENUM(FormatMatcher, Basic, BestFit);
  switch (formatMatcher) {
    case Basic:
      return "basic";
    case BestFit:
      return "best fit";
  }
  MOZ_CRASH("invalid format matcher");
}

enum class DateTimeFormatConstructorKind {
  Standard,
  EnableMozExtensions,
};

/**
 * CreateDateTimeFormat ( newTarget, locales, options, required, defaults [ ,
 * toLocaleStringTimeZone ] )
 */
static bool CreateDateTimeFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<JS::Value> locales, Handle<JS::Value> optionsValue,
    DateTimeFormatOptions::Required required,
    DateTimeFormatOptions::Defaults defaults,
    Handle<JSLinearString*> toLocaleStringTimeZone = nullptr,
    DateTimeFormatConstructorKind dtfKind =
        DateTimeFormatConstructorKind::Standard) {
  // Step 1. (Performed in caller)

  // Steps 2-4. (Inlined ResolveOptions)

  // ResolveOptions, step 1.
  auto* requestedLocales = CanonicalizeLocaleList(cx, locales);
  if (!requestedLocales) {
    return false;
  }
  dateTimeFormat->setRequestedLocales(requestedLocales);

  auto dtfOptions = DateTimeFormatOptions{
      .required = required,
      .defaults = defaults,
  };

  if (!optionsValue.isUndefined()) {
    // ResolveOptions, steps 2-3.
    Rooted<JSObject*> options(cx, JS::ToObject(cx, optionsValue));
    if (!options) {
      return false;
    }

    // ResolveOptions, step 4.
    LocaleMatcher matcher;
    if (!GetLocaleMatcherOption(cx, options, &matcher)) {
      return false;
    }

    // ResolveOptions, step 5.
    //
    // This implementation only supports the "lookup" locale matcher, therefore
    // the "localeMatcher" option doesn't need to be stored.

    // ResolveOptions, step 6.
    Rooted<JSLinearString*> calendar(cx);
    if (!GetUnicodeExtensionOption(cx, options, UnicodeExtensionKey::Calendar,
                                   &calendar)) {
      return false;
    }
    if (calendar) {
      dateTimeFormat->setCalendar(calendar);
    }

    Rooted<JSLinearString*> numberingSystem(cx);
    if (!GetUnicodeExtensionOption(cx, options,
                                   UnicodeExtensionKey::NumberingSystem,
                                   &numberingSystem)) {
      return false;
    }
    if (numberingSystem) {
      dateTimeFormat->setNumberingSystem(numberingSystem);
    }

    if (!GetBooleanOption(cx, options, cx->names().hour12,
                          &dtfOptions.hour12)) {
      return false;
    }

    static constexpr auto hourCycles =
        MapOptions<HourCycleToString>(DateTimeFormatOptions::HourCycle::H11,
                                      DateTimeFormatOptions::HourCycle::H12,
                                      DateTimeFormatOptions::HourCycle::H23,
                                      DateTimeFormatOptions::HourCycle::H24);
    if (!GetStringOption(cx, options, cx->names().hourCycle, hourCycles,
                         &dtfOptions.hourCycle)) {
      return false;
    }

    // ResolveOptions, step 7.
    if (dtfOptions.hour12.isSome()) {
      // The "hourCycle" option is ignored if "hour12" is also present.
      dtfOptions.hourCycle = mozilla::Nothing();
    }

    // ResolveOptions, step 8. (Performed in ResolveLocale)

    // ResolveOptions, step 9. (Return)

    // Step 5. (Not applicable when ResolveOptions is inlined.)

    // Step 6-14. (Performed in ResolveLocale)

    // Step 15.
    Rooted<JS::Value> timeZoneOption(cx);
    if (!GetProperty(cx, options, options, cx->names().timeZone,
                     &timeZoneOption)) {
      return false;
    }

    // Steps 16-19.
    JSLinearString* timeZone = nullptr;
    if (timeZoneOption.isUndefined()) {
      // Step 16.
      if (toLocaleStringTimeZone) {
        timeZone = toLocaleStringTimeZone;
      } else {
        timeZone = cx->global()->globalIntlData().defaultTimeZone(cx);
        if (!timeZone) {
          return false;
        }
      }

      // Steps 18-19.  (Not applicable in our implementation.)
    } else {
      // Step 17.
      if (toLocaleStringTimeZone) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_INVALID_DATETIME_OPTION, "timeZone",
                                  "Temporal.ZonedDateTime.toLocaleString");
        return false;
      }

      Rooted<JSString*> timeZoneStr(cx, JS::ToString(cx, timeZoneOption));
      if (!timeZoneStr) {
        return false;
      }

      // Steps 18-19.
      timeZone = temporal::ToValidCanonicalTimeZoneIdentifier(cx, timeZoneStr);
      if (!timeZone) {
        return false;
      }
    }

    // Step 20.
    dateTimeFormat->setTimeZone(timeZone);

    if (dtfKind == DateTimeFormatConstructorKind::EnableMozExtensions) {
      Rooted<JS::Value> patternOption(cx);
      if (!GetProperty(cx, options, options, cx->names().pattern,
                       &patternOption)) {
        return false;
      }

      if (!patternOption.isUndefined()) {
        auto* pattern = JS::ToString(cx, patternOption);
        if (!pattern) {
          return false;
        }
        dateTimeFormat->setPattern(pattern);
      }
    }

    // Steps 21-22. (Not applicable in our implementation.)

    // Step 23. (Moved below)

    // Step 24.
    static constexpr auto weekdays =
        MapOptions<WeekdayToString>(DateTimeFormatOptions::Weekday::Narrow,
                                    DateTimeFormatOptions::Weekday::Short,
                                    DateTimeFormatOptions::Weekday::Long);
    if (!GetStringOption(cx, options, cx->names().weekday, weekdays,
                         &dtfOptions.weekday)) {
      return false;
    }

    static constexpr auto eras = MapOptions<EraToString>(
        DateTimeFormatOptions::Era::Narrow, DateTimeFormatOptions::Era::Short,
        DateTimeFormatOptions::Era::Long);
    if (!GetStringOption(cx, options, cx->names().era, eras, &dtfOptions.era)) {
      return false;
    }

    static constexpr auto years =
        MapOptions<YearToString>(DateTimeFormatOptions::Year::TwoDigit,
                                 DateTimeFormatOptions::Year::Numeric);
    if (!GetStringOption(cx, options, cx->names().year, years,
                         &dtfOptions.year)) {
      return false;
    }

    static constexpr auto months =
        MapOptions<MonthToString>(DateTimeFormatOptions::Month::TwoDigit,
                                  DateTimeFormatOptions::Month::Numeric,
                                  DateTimeFormatOptions::Month::Narrow,
                                  DateTimeFormatOptions::Month::Short,
                                  DateTimeFormatOptions::Month::Long);
    if (!GetStringOption(cx, options, cx->names().month, months,
                         &dtfOptions.month)) {
      return false;
    }

    static constexpr auto days =
        MapOptions<DayToString>(DateTimeFormatOptions::Day::TwoDigit,
                                DateTimeFormatOptions::Day::Numeric);
    if (!GetStringOption(cx, options, cx->names().day, days, &dtfOptions.day)) {
      return false;
    }

    static constexpr auto dayPeriods =
        MapOptions<DayPeriodToString>(DateTimeFormatOptions::DayPeriod::Narrow,
                                      DateTimeFormatOptions::DayPeriod::Short,
                                      DateTimeFormatOptions::DayPeriod::Long);
    if (!GetStringOption(cx, options, cx->names().dayPeriod, dayPeriods,
                         &dtfOptions.dayPeriod)) {
      return false;
    }

    static constexpr auto hours =
        MapOptions<HourToString>(DateTimeFormatOptions::Hour::TwoDigit,
                                 DateTimeFormatOptions::Hour::Numeric);
    if (!GetStringOption(cx, options, cx->names().hour, hours,
                         &dtfOptions.hour)) {
      return false;
    }

    static constexpr auto minutes =
        MapOptions<MinuteToString>(DateTimeFormatOptions::Minute::TwoDigit,
                                   DateTimeFormatOptions::Minute::Numeric);
    if (!GetStringOption(cx, options, cx->names().minute, minutes,
                         &dtfOptions.minute)) {
      return false;
    }

    static constexpr auto seconds =
        MapOptions<SecondToString>(DateTimeFormatOptions::Second::TwoDigit,
                                   DateTimeFormatOptions::Second::Numeric);
    if (!GetStringOption(cx, options, cx->names().second, seconds,
                         &dtfOptions.second)) {
      return false;
    }

    mozilla::Maybe<int32_t> fractionalSecondDigits{};
    if (!GetNumberOption(cx, options, cx->names().fractionalSecondDigits, 1, 3,
                         &fractionalSecondDigits)) {
      return false;
    }
    dtfOptions.fractionalSecondDigits = fractionalSecondDigits;

    static constexpr auto timeZoneNames = MapOptions<TimeZoneNameToString>(
        DateTimeFormatOptions::TimeZoneName::Short,
        DateTimeFormatOptions::TimeZoneName::Long,
        DateTimeFormatOptions::TimeZoneName::ShortOffset,
        DateTimeFormatOptions::TimeZoneName::LongOffset,
        DateTimeFormatOptions::TimeZoneName::ShortGeneric,
        DateTimeFormatOptions::TimeZoneName::LongGeneric);
    if (!GetStringOption(cx, options, cx->names().timeZoneName, timeZoneNames,
                         &dtfOptions.timeZoneName)) {
      return false;
    }

    // Step 25.
    //
    // This implementation only supports the "best fit" format matcher,
    // therefore the "formatMatcher" option doesn't need to be stored.
    //
    // See bug 852837.
    static constexpr auto formatMatchers = MapOptions<FormatMatcherToString>(
        FormatMatcher::Basic, FormatMatcher::BestFit);
    FormatMatcher formatMatcher;
    if (!GetStringOption(cx, options, cx->names().formatMatcher, formatMatchers,
                         FormatMatcher::BestFit, &formatMatcher)) {
      return false;
    }

    // Steps 26-27.
    static constexpr auto dateStyles =
        MapOptions<DateStyleToString>(DateTimeFormatOptions::DateStyle::Full,
                                      DateTimeFormatOptions::DateStyle::Long,
                                      DateTimeFormatOptions::DateStyle::Medium,
                                      DateTimeFormatOptions::DateStyle::Short);
    if (!GetStringOption(cx, options, cx->names().dateStyle, dateStyles,
                         &dtfOptions.dateStyle)) {
      return false;
    }

    // Steps 28-29.
    static constexpr auto timeStyles =
        MapOptions<TimeStyleToString>(DateTimeFormatOptions::TimeStyle::Full,
                                      DateTimeFormatOptions::TimeStyle::Long,
                                      DateTimeFormatOptions::TimeStyle::Medium,
                                      DateTimeFormatOptions::TimeStyle::Short);
    if (!GetStringOption(cx, options, cx->names().timeStyle, timeStyles,
                         &dtfOptions.timeStyle)) {
      return false;
    }

    // Step 30.
    if (dtfOptions.dateStyle.isSome() || dtfOptions.timeStyle.isSome()) {
      // Step 23.
      const char* explicitFormatComponent = ([&]() -> const char* {
        if (dtfOptions.weekday.isSome()) {
          return "weekday";
        }
        if (dtfOptions.era.isSome()) {
          return "era";
        }
        if (dtfOptions.year.isSome()) {
          return "year";
        }
        if (dtfOptions.month.isSome()) {
          return "month";
        }
        if (dtfOptions.day.isSome()) {
          return "day";
        }
        if (dtfOptions.dayPeriod.isSome()) {
          return "dayPeriod";
        }
        if (dtfOptions.hour.isSome()) {
          return "hour";
        }
        if (dtfOptions.minute.isSome()) {
          return "minute";
        }
        if (dtfOptions.second.isSome()) {
          return "second";
        }
        if (dtfOptions.fractionalSecondDigits.isSome()) {
          return "fractionalSecondDigits";
        }
        if (dtfOptions.timeZoneName.isSome()) {
          return "timeZoneName";
        }
        return nullptr;
      })();

      // Step 30.a.
      if (explicitFormatComponent) {
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr, JSMSG_INVALID_DATETIME_OPTION,
            explicitFormatComponent,
            dtfOptions.dateStyle.isSome() ? "dateStyle" : "timeStyle");
        return false;
      }

      // Step 30.b.
      if (required == DateTimeFormatOptions::Required::Date &&
          dtfOptions.timeStyle.isSome()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_INVALID_DATETIME_STYLE, "timeStyle",
                                  "date");
        return false;
      }

      // Step 30.c.
      if (required == DateTimeFormatOptions::Required::Time &&
          dtfOptions.dateStyle.isSome()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_INVALID_DATETIME_STYLE, "dateStyle",
                                  "time");
        return false;
      }
    }

    // Steps 31-33. (See ResolveDateTimeFormat)
  } else {
    // Absent |options| object only requires to store the current time zone.

    // Step 16.
    JSLinearString* timeZone;
    if (toLocaleStringTimeZone) {
      timeZone = toLocaleStringTimeZone;
    } else {
      timeZone = cx->global()->globalIntlData().defaultTimeZone(cx);
      if (!timeZone) {
        return false;
      }
    }

    // Step 20.
    dateTimeFormat->setTimeZone(timeZone);
  }
  dateTimeFormat->setOptions(dtfOptions);

  // Step 34.
  return true;
}

/**
 * 12.2.1 Intl.DateTimeFormat([ locales [, options]])
 *
 * ES2017 Intl draft rev 94045d234762ad107a3d09bb6f7381a65f1a2f9b
 */
static bool DateTimeFormat(JSContext* cx, const CallArgs& args,
                           DateTimeFormatOptions::Required required,
                           DateTimeFormatOptions::Defaults defaults,
                           DateTimeFormatConstructorKind dtfKind) {
  AutoJSConstructorProfilerEntry pseudoFrame(cx, "Intl.DateTimeFormat");

  // Step 1 (Handled by OrdinaryCreateFromConstructor fallback code).

  // Step 2 (Inlined 9.1.14, OrdinaryCreateFromConstructor).
  JSProtoKey protoKey = dtfKind == DateTimeFormatConstructorKind::Standard
                            ? JSProto_DateTimeFormat
                            : JSProto_Null;
  Rooted<JSObject*> proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey, &proto)) {
    return false;
  }

  Rooted<DateTimeFormatObject*> dateTimeFormat(cx);
  dateTimeFormat = NewObjectWithClassProto<DateTimeFormatObject>(cx, proto);
  if (!dateTimeFormat) {
    return false;
  }

  // Step 2.
  if (!CreateDateTimeFormat(cx, dateTimeFormat, args.get(0), args.get(1),
                            required, defaults, nullptr, dtfKind)) {
    return false;
  }

  // Step 3.
  if (dtfKind == DateTimeFormatConstructorKind::Standard) {
    return ChainLegacyIntlFormat(cx, JSProto_DateTimeFormat, args,
                                 dateTimeFormat);
  }

  // Step 4.
  args.rval().setObject(*dateTimeFormat);
  return true;
}

static bool DateTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  auto required = DateTimeFormatOptions::Required::Any;
  auto defaults = DateTimeFormatOptions::Defaults::Date;
  return DateTimeFormat(cx, args, required, defaults,
                        DateTimeFormatConstructorKind::Standard);
}

static bool MozDateTimeFormat(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Don't allow to call mozIntl.DateTimeFormat as a function. That way we
  // don't need to worry how to handle the legacy initialization semantics
  // when applied on mozIntl.DateTimeFormat.
  if (!ThrowIfNotConstructing(cx, args, "mozIntl.DateTimeFormat")) {
    return false;
  }

  auto required = DateTimeFormatOptions::Required::Any;
  auto defaults = DateTimeFormatOptions::Defaults::Date;
  return DateTimeFormat(cx, args, required, defaults,
                        DateTimeFormatConstructorKind::EnableMozExtensions);
}

static auto ToRequired(DateTimeFormatKind kind) {
  MOZ_USING_ENUM(DateTimeFormatOptions::Required, Any, Date, Time);
  switch (kind) {
    case DateTimeFormatKind::All:
      return Any;
    case DateTimeFormatKind::Date:
      return Date;
    case DateTimeFormatKind::Time:
      return Time;
  }
  MOZ_CRASH("invalid date time format kind");
}

static auto ToDefaults(DateTimeFormatKind kind) {
  MOZ_USING_ENUM(DateTimeFormatOptions::Defaults, All, Date, Time);
  switch (kind) {
    case DateTimeFormatKind::All:
      return All;
    case DateTimeFormatKind::Date:
      return Date;
    case DateTimeFormatKind::Time:
      return Time;
  }
  MOZ_CRASH("invalid date time format kind");
}

static DateTimeFormatObject* CreateDateTimeFormat(
    JSContext* cx, Handle<Value> locales, Handle<Value> options,
    Handle<JSLinearString*> toLocaleStringTimeZone, DateTimeFormatKind kind) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, NewBuiltinClassInstance<DateTimeFormatObject>(cx));
  if (!dateTimeFormat) {
    return nullptr;
  }

  auto required = ToRequired(kind);
  auto defaults = ToDefaults(kind);

  if (!CreateDateTimeFormat(cx, dateTimeFormat, locales, options, required,
                            defaults, toLocaleStringTimeZone,
                            DateTimeFormatConstructorKind::Standard)) {
    return nullptr;
  }

  return dateTimeFormat;
}

DateTimeFormatObject* js::intl::CreateDateTimeFormat(JSContext* cx,
                                                     Handle<Value> locales,
                                                     Handle<Value> options,
                                                     DateTimeFormatKind kind) {
  return CreateDateTimeFormat(cx, locales, options, nullptr, kind);
}

DateTimeFormatObject* js::intl::GetOrCreateDateTimeFormat(
    JSContext* cx, Handle<Value> locales, Handle<Value> options,
    DateTimeFormatKind kind) {
  // Try to use a cached instance when |locales| is either undefined or a
  // string, and |options| is undefined.
  if ((locales.isUndefined() || locales.isString()) && options.isUndefined()) {
    Rooted<JSLinearString*> locale(cx);
    if (locales.isString()) {
      locale = locales.toString()->ensureLinear(cx);
      if (!locale) {
        return nullptr;
      }
    }
    return cx->global()->globalIntlData().getOrCreateDateTimeFormat(cx, kind,
                                                                    locale);
  }

  // Create a new Intl.DateTimeFormat instance.
  return CreateDateTimeFormat(cx, locales, options, nullptr, kind);
}

void js::intl::DateTimeFormatObject::finalize(JS::GCContext* gcx,
                                              JSObject* obj) {
  auto* dateTimeFormat = &obj->as<DateTimeFormatObject>();
  auto* df = dateTimeFormat->getDateFormat();
  auto* dif = dateTimeFormat->getDateIntervalFormat();

  if (df) {
    RemoveICUCellMemory(gcx, obj,
                        DateTimeFormatObject::UDateFormatEstimatedMemoryUse);
    delete df;
  }

  if (dif) {
    RemoveICUCellMemory(
        gcx, obj, DateTimeFormatObject::UDateIntervalFormatEstimatedMemoryUse);
    delete dif;
  }
}

bool JS::AddMozDateTimeFormatConstructor(JSContext* cx,
                                         JS::Handle<JSObject*> intl) {
  Rooted<JSObject*> ctor(
      cx, GlobalObject::createConstructor(cx, MozDateTimeFormat,
                                          cx->names().DateTimeFormat, 0));
  if (!ctor) {
    return false;
  }

  Rooted<JSObject*> proto(
      cx, GlobalObject::createBlankPrototype<PlainObject>(cx, cx->global()));
  if (!proto) {
    return false;
  }

  if (!LinkConstructorAndPrototype(cx, ctor, proto)) {
    return false;
  }

  // 12.3.2
  if (!JS_DefineFunctions(cx, ctor, dateTimeFormat_static_methods)) {
    return false;
  }

  // 12.4.4 and 12.4.5
  if (!JS_DefineFunctions(cx, proto, dateTimeFormat_methods)) {
    return false;
  }

  // 12.4.2 and 12.4.3
  if (!JS_DefineProperties(cx, proto, dateTimeFormat_properties)) {
    return false;
  }

  Rooted<JS::Value> ctorValue(cx, ObjectValue(*ctor));
  return DefineDataProperty(cx, intl, cx->names().DateTimeFormat, ctorValue, 0);
}

/**
 * Resolve the actual locale to finish initialization of the DateTimeFormat.
 */
static bool ResolveLocale(JSContext* cx,
                          Handle<DateTimeFormatObject*> dateTimeFormat) {
  // Return if the locale was already resolved.
  if (dateTimeFormat->isLocaleResolved()) {
    return true;
  }
  auto dtfOptions = dateTimeFormat->getOptions();

  Rooted<ArrayObject*> requestedLocales(
      cx, &dateTimeFormat->getRequestedLocales()->as<ArrayObject>());

  // %Intl.DateTimeFormat%.[[RelevantExtensionKeys]] is « "ca", "hc", "nu" ».
  mozilla::EnumSet<UnicodeExtensionKey> relevantExtensionKeys{
      UnicodeExtensionKey::Calendar,
      UnicodeExtensionKey::HourCycle,
      UnicodeExtensionKey::NumberingSystem,
  };

  // Initialize locale options from constructor arguments.
  Rooted<LocaleOptions> localeOptions(cx);
  if (auto* ca = dateTimeFormat->getCalendar()) {
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::Calendar, ca);
  }
  if (dtfOptions.hour12.isSome()) {
    // Explicitly opt-out of hourCycle if the hour12 option is present, because
    // the latter takes precedence over hourCycle.
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::HourCycle, nullptr);
  } else {
    if (auto hourCycle = dtfOptions.hourCycle) {
      MOZ_USING_ENUM(DateTimeFormatOptions::HourCycle, H11, H12, H23, H24);

      JSLinearString* hc;
      switch (*hourCycle) {
        case H11:
          hc = cx->names().h11;
          break;
        case H12:
          hc = cx->names().h12;
          break;
        case H23:
          hc = cx->names().h23;
          break;
        case H24:
          hc = cx->names().h24;
          break;
      }
      localeOptions.setUnicodeExtension(UnicodeExtensionKey::HourCycle, hc);
    }
  }
  if (auto* nu = dateTimeFormat->getNumberingSystem()) {
    localeOptions.setUnicodeExtension(UnicodeExtensionKey::NumberingSystem, nu);
  }

  // Use the default locale data.
  auto localeData = LocaleData::Default;

  // Resolve the actual locale.
  Rooted<ResolvedLocale> resolved(cx);
  if (!ResolveLocale(cx, AvailableLocaleKind::DateTimeFormat, requestedLocales,
                     localeOptions, relevantExtensionKeys, localeData,
                     &resolved)) {
    return false;
  }

  // Finish initialization by setting the actual locale and extensions.

  // Changes from "Intl era and monthCode" proposal.
  //
  // https://tc39.es/proposal-intl-era-monthcode/#sec-createdatetimeformat
  if (auto ca = resolved.extension(UnicodeExtensionKey::Calendar)) {
    if (StringEqualsLiteral(ca, "islamic")) {
      if (!WarnNumberASCII(cx, JSMSG_ISLAMIC_FALLBACK)) {
        return false;
      }

      // Fallback to "islamic-tbla" calendar.
      auto* str = NewStringCopyZ<CanGC>(cx, "islamic-tbla");
      if (!str) {
        return false;
      }
      dateTimeFormat->setCalendar(str);
    } else {
      dateTimeFormat->setCalendar(ca);
    }
  } else {
    dateTimeFormat->setCalendar(cx->names().default_);
  }

  auto hc = resolved.extension(UnicodeExtensionKey::HourCycle);
  if (hc) {
    MOZ_ASSERT(dtfOptions.hour12.isNothing());
    MOZ_USING_ENUM(DateTimeFormatOptions::HourCycle, H11, H12, H23, H24);
    if (StringEqualsLiteral(hc, "h11")) {
      dtfOptions.hourCycle = mozilla::Some(H11);
    } else if (StringEqualsLiteral(hc, "h12")) {
      dtfOptions.hourCycle = mozilla::Some(H12);
    } else if (StringEqualsLiteral(hc, "h23")) {
      dtfOptions.hourCycle = mozilla::Some(H23);
    } else {
      MOZ_ASSERT(StringEqualsLiteral(hc, "h24"));
      dtfOptions.hourCycle = mozilla::Some(H24);
    }
  } else {
    // The first element of [[LocaleData]].[[<locale>]].[[hc]] is |null|, so
    // hour-cycle is left unset if no explicit option was present.
  }

  if (auto nu = resolved.extension(UnicodeExtensionKey::NumberingSystem)) {
    dateTimeFormat->setNumberingSystem(nu);
  } else {
    dateTimeFormat->setNumberingSystem(cx->names().default_);
  }

  auto* locale = resolved.toLocale(cx);
  if (!locale) {
    return false;
  }
  dateTimeFormat->setLocale(locale);

  // Set the resolved options.
  dateTimeFormat->setOptions(dtfOptions);

  MOZ_ASSERT(dateTimeFormat->isLocaleResolved(),
             "locale successfully resolved");
  return true;
}

static JSLinearString* ResolveCalendar(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat) {
  MOZ_ASSERT(dateTimeFormat->isLocaleResolved());

  auto* calendar = dateTimeFormat->getCalendar();
  if (calendar == cx->names().default_) {
    calendar = DefaultCalendar(cx, dateTimeFormat->getLocale());
    if (!calendar) {
      return nullptr;
    }
    dateTimeFormat->setCalendar(calendar);
  }
  return calendar;
}

static JSLinearString* ResolveNumberingSystem(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat) {
  MOZ_ASSERT(dateTimeFormat->isLocaleResolved());

  auto* numberingSystem = dateTimeFormat->getNumberingSystem();
  if (numberingSystem == cx->names().default_) {
    numberingSystem = DefaultNumberingSystem(cx, dateTimeFormat->getLocale());
    if (!numberingSystem) {
      return nullptr;
    }
    dateTimeFormat->setNumberingSystem(numberingSystem);
  }
  return numberingSystem;
}

enum class HourCycle {
  // 12 hour cycle, from 0 to 11.
  H11,

  // 12 hour cycle, from 1 to 12.
  H12,

  // 24 hour cycle, from 0 to 23.
  H23,

  // 24 hour cycle, from 1 to 24.
  H24
};

static UniqueChars DateTimeFormatLocale(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    mozilla::Maybe<mozilla::intl::DateTimeFormat::HourCycle> hourCycle =
        mozilla::Nothing()) {
  MOZ_ASSERT(dateTimeFormat->isLocaleResolved());

  // ICU expects calendar, numberingSystem, and hourCycle as Unicode locale
  // extensions on locale.
  //
  // We don't add any Unicode extension keywords when the default values can be
  // used, because ICU optimizes for this case.

  JS::RootedVector<UnicodeExtensionKeyword> keywords(cx);

  auto* calendar = dateTimeFormat->getCalendar();
  if (calendar != cx->names().default_) {
    if (!keywords.emplaceBack("ca", calendar)) {
      return nullptr;
    }
  }

  auto* numberingSystem = dateTimeFormat->getNumberingSystem();
  if (numberingSystem != cx->names().default_) {
    if (!keywords.emplaceBack("nu", numberingSystem)) {
      return nullptr;
    }
  }

  if (hourCycle) {
    MOZ_USING_ENUM(mozilla::intl::DateTimeFormat::HourCycle, H11, H12, H23,
                   H24);

    JSAtom* hourCycleStr;
    switch (*hourCycle) {
      case H11:
        hourCycleStr = cx->names().h11;
        break;
      case H12:
        hourCycleStr = cx->names().h12;
        break;
      case H23:
        hourCycleStr = cx->names().h23;
        break;
      case H24:
        hourCycleStr = cx->names().h24;
        break;
    }

    if (!keywords.emplaceBack("hc", hourCycleStr)) {
      return nullptr;
    }
  }

  Rooted<JSLinearString*> localeStr(cx, dateTimeFormat->getLocale());
  return FormatLocale(cx, localeStr, keywords);
}

enum class Required { Date, Time, YearMonth, MonthDay, Any };

enum class Defaults { Date, Time, YearMonth, MonthDay, ZonedDateTime, All };

enum class Inherit { All, Relevant };

struct DateTimeFormatArgs {
  Required required;
  Defaults defaults;
  Inherit inherit;
};

/**
 * Get the "required" argument passed to CreateDateTimeFormat.
 */
static auto GetRequired(DateTimeFormatOptions::Required required) {
  MOZ_USING_ENUM(Required, Date, Time, Any);
  switch (required) {
    case DateTimeFormatOptions::Required::Date:
      return Date;
    case DateTimeFormatOptions::Required::Time:
      return Time;
    case DateTimeFormatOptions::Required::Any:
      return Any;
  }
  MOZ_CRASH("invalid date time format required");
}

/**
 * Get the "defaults" argument passed to CreateDateTimeFormat.
 */
static auto GetDefaults(DateTimeFormatOptions::Defaults defaults) {
  MOZ_USING_ENUM(Defaults, Date, Time, All);
  switch (defaults) {
    case DateTimeFormatOptions::Defaults::Date:
      return Date;
    case DateTimeFormatOptions::Defaults::Time:
      return Time;
    case DateTimeFormatOptions::Defaults::All:
      return All;
  }
  MOZ_CRASH("invalid date time format defaults");
}

/**
 * Compute the (required, defaults, inherit) arguments passed to
 * GetDateTimeFormat.
 */
static DateTimeFormatArgs GetDateTimeFormatArgs(
    const DateTimeFormatOptions& options, DateTimeValueKind kind) {
  switch (kind) {
    case DateTimeValueKind::Number:
      return {GetRequired(options.required), GetDefaults(options.defaults),
              Inherit::All};
    case DateTimeValueKind::TemporalDate:
      return {Required::Date, Defaults::Date, Inherit::Relevant};
    case DateTimeValueKind::TemporalTime:
      return {Required::Time, Defaults::Time, Inherit::Relevant};
    case DateTimeValueKind::TemporalDateTime:
      return {Required::Any, Defaults::All, Inherit::Relevant};
    case DateTimeValueKind::TemporalYearMonth:
      return {Required::YearMonth, Defaults::YearMonth, Inherit::Relevant};
    case DateTimeValueKind::TemporalMonthDay:
      return {Required::MonthDay, Defaults::MonthDay, Inherit::Relevant};
    case DateTimeValueKind::TemporalZonedDateTime:
      return {Required::Any, Defaults::ZonedDateTime, Inherit::All};
    case DateTimeValueKind::TemporalInstant:
      return {Required::Any, Defaults::All, Inherit::All};
  }
  MOZ_CRASH("invalid date-time value kind");
}

enum class DateTimeField {
  Weekday,
  Era,
  Year,
  Month,
  Day,
  DayPeriod,
  Hour,
  Minute,
  Second,
  FractionalSecondDigits,
};

/**
 * GetDateTimeFormat ( formats, matcher, options, required, defaults, inherit )
 *
 * https://tc39.es/proposal-temporal/#sec-getdatetimeformat
 */
static mozilla::Maybe<mozilla::intl::DateTimeFormat::ComponentsBag>
GetDateTimeFormat(const mozilla::intl::DateTimeFormat::ComponentsBag& options,
                  Required required, Defaults defaults, Inherit inherit) {
  // Steps 1-5.
  mozilla::EnumSet<DateTimeField> requiredOptions;
  switch (required) {
    case Required::Date:
      requiredOptions = {
          DateTimeField::Weekday,
          DateTimeField::Year,
          DateTimeField::Month,
          DateTimeField::Day,
      };
      break;
    case Required::Time:
      requiredOptions = {
          DateTimeField::DayPeriod,
          DateTimeField::Hour,
          DateTimeField::Minute,
          DateTimeField::Second,
          DateTimeField::FractionalSecondDigits,
      };
      break;
    case Required::YearMonth:
      requiredOptions = {
          DateTimeField::Year,
          DateTimeField::Month,
      };
      break;
    case Required::MonthDay:
      requiredOptions = {
          DateTimeField::Month,
          DateTimeField::Day,
      };
      break;
    case Required::Any:
      requiredOptions = {
          DateTimeField::Weekday,
          DateTimeField::Year,
          DateTimeField::Month,
          DateTimeField::Day,
          DateTimeField::DayPeriod,
          DateTimeField::Hour,
          DateTimeField::Minute,
          DateTimeField::Second,
          DateTimeField::FractionalSecondDigits,
      };
      break;
  }
  MOZ_ASSERT(!requiredOptions.contains(DateTimeField::Era),
             "standalone era not supported");

  // Steps 6-10.
  mozilla::EnumSet<DateTimeField> defaultOptions;
  switch (defaults) {
    case Defaults::Date:
      defaultOptions = {
          DateTimeField::Year,
          DateTimeField::Month,
          DateTimeField::Day,
      };
      break;
    case Defaults::Time:
      defaultOptions = {
          DateTimeField::Hour,
          DateTimeField::Minute,
          DateTimeField::Second,
      };
      break;
    case Defaults::YearMonth:
      defaultOptions = {
          DateTimeField::Year,
          DateTimeField::Month,
      };
      break;
    case Defaults::MonthDay:
      defaultOptions = {
          DateTimeField::Month,
          DateTimeField::Day,
      };
      break;
    case Defaults::ZonedDateTime:
    case Defaults::All:
      defaultOptions = {
          DateTimeField::Year, DateTimeField::Month,  DateTimeField::Day,
          DateTimeField::Hour, DateTimeField::Minute, DateTimeField::Second,
      };
      break;
  }
  MOZ_ASSERT(!defaultOptions.contains(DateTimeField::Weekday));
  MOZ_ASSERT(!defaultOptions.contains(DateTimeField::Era));
  MOZ_ASSERT(!defaultOptions.contains(DateTimeField::DayPeriod));
  MOZ_ASSERT(!defaultOptions.contains(DateTimeField::FractionalSecondDigits));

  // Steps 11-12.
  mozilla::intl::DateTimeFormat::ComponentsBag formatOptions;
  if (inherit == Inherit::All) {
    // Step 11.a.
    formatOptions = options;
  } else {
    // Step 12.a. (Implicit)

    // Step 12.b.
    switch (required) {
      case Required::Date:
      case Required::YearMonth:
      case Required::Any:
        formatOptions.era = options.era;
        break;
      case Required::Time:
      case Required::MonthDay:
        // |era| option not applicable for these types.
        break;
    }

    // Step 12.c.
    switch (required) {
      case Required::Time:
      case Required::Any:
        formatOptions.hourCycle = options.hourCycle;
        formatOptions.hour12 = options.hour12;
        break;
      case Required::Date:
      case Required::YearMonth:
      case Required::MonthDay:
        // |hourCycle| and |hour12| options not applicable for these types.
        break;
    }
  }

  // Steps 13-14.
  bool anyPresent = options.weekday || options.year || options.month ||
                    options.day || options.dayPeriod || options.hour ||
                    options.minute || options.second ||
                    options.fractionalSecondDigits;

  // Step 15.
  bool needDefaults = true;

  // Step 16. (Loop unrolled)
  if (requiredOptions.contains(DateTimeField::Weekday) && options.weekday) {
    formatOptions.weekday = options.weekday;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Year) && options.year) {
    formatOptions.year = options.year;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Month) && options.month) {
    formatOptions.month = options.month;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Day) && options.day) {
    formatOptions.day = options.day;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::DayPeriod) && options.dayPeriod) {
    formatOptions.dayPeriod = options.dayPeriod;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Hour) && options.hour) {
    formatOptions.hour = options.hour;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Minute) && options.minute) {
    formatOptions.minute = options.minute;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::Second) && options.second) {
    formatOptions.second = options.second;
    needDefaults = false;
  }
  if (requiredOptions.contains(DateTimeField::FractionalSecondDigits) &&
      options.fractionalSecondDigits) {
    formatOptions.fractionalSecondDigits = options.fractionalSecondDigits;
    needDefaults = false;
  }

  // Step 17.
  if (needDefaults) {
    // Step 17.a.
    if (anyPresent && inherit == Inherit::Relevant) {
      return mozilla::Nothing();
    }

    // Step 17.b. (Loop unrolled)
    auto numericOption =
        mozilla::Some(mozilla::intl::DateTimeFormat::Numeric::Numeric);
    if (defaultOptions.contains(DateTimeField::Year)) {
      formatOptions.year = numericOption;
    }
    if (defaultOptions.contains(DateTimeField::Month)) {
      formatOptions.month =
          mozilla::Some(mozilla::intl::DateTimeFormat::Month::Numeric);
    }
    if (defaultOptions.contains(DateTimeField::Day)) {
      formatOptions.day = numericOption;
    }
    if (defaultOptions.contains(DateTimeField::Hour)) {
      formatOptions.hour = numericOption;
    }
    if (defaultOptions.contains(DateTimeField::Minute)) {
      formatOptions.minute = numericOption;
    }
    if (defaultOptions.contains(DateTimeField::Second)) {
      formatOptions.second = std::move(numericOption);
    }

    // Step 17.c.
    if (defaults == Defaults::ZonedDateTime && !formatOptions.timeZoneName) {
      formatOptions.timeZoneName =
          mozilla::Some(mozilla::intl::DateTimeFormat::TimeZoneName::Short);
    }
  }

  // Steps 18-20. (Performed in caller).

  return mozilla::Some(formatOptions);
}

/**
 * AdjustDateTimeStyleFormat ( formats, baseFormat, matcher, allowedOptions )
 *
 * https://tc39.es/proposal-temporal/#sec-adjustdatetimestyleformat
 */
static mozilla::Maybe<mozilla::intl::DateTimeFormat::ComponentsBag>
AdjustDateTimeStyleFormat(
    const mozilla::intl::DateTimeFormat::ComponentsBag& baseFormat,
    mozilla::EnumSet<DateTimeField> allowedOptions) {
  // Step 1.
  bool anyConflictingFields = false;

  // Step 5. (Reordered)
  mozilla::intl::DateTimeFormat::ComponentsBag formatOptions;

  // Steps 2 and 6. (Loops unrolled)
  if (baseFormat.era) {
    if (allowedOptions.contains(DateTimeField::Era)) {
      formatOptions.era = baseFormat.era;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.weekday) {
    if (allowedOptions.contains(DateTimeField::Weekday)) {
      formatOptions.weekday = baseFormat.weekday;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.year) {
    if (allowedOptions.contains(DateTimeField::Year)) {
      formatOptions.year = baseFormat.year;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.month) {
    if (allowedOptions.contains(DateTimeField::Month)) {
      formatOptions.month = baseFormat.month;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.day) {
    if (allowedOptions.contains(DateTimeField::Day)) {
      formatOptions.day = baseFormat.day;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.dayPeriod) {
    if (allowedOptions.contains(DateTimeField::DayPeriod)) {
      formatOptions.dayPeriod = baseFormat.dayPeriod;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.hour) {
    if (allowedOptions.contains(DateTimeField::Hour)) {
      formatOptions.hour = baseFormat.hour;
      formatOptions.hourCycle = baseFormat.hourCycle;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.minute) {
    if (allowedOptions.contains(DateTimeField::Minute)) {
      formatOptions.minute = baseFormat.minute;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.second) {
    if (allowedOptions.contains(DateTimeField::Second)) {
      formatOptions.second = baseFormat.second;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.fractionalSecondDigits) {
    if (allowedOptions.contains(DateTimeField::FractionalSecondDigits)) {
      formatOptions.fractionalSecondDigits = baseFormat.fractionalSecondDigits;
    } else {
      anyConflictingFields = true;
    }
  }
  if (baseFormat.timeZoneName) {
    anyConflictingFields = true;
  }

  // Steps 3-4.
  if (!anyConflictingFields) {
    return mozilla::Nothing();
  }

  // Steps 5-6. (Moved above)

  // Steps 7-9. (Performed in caller)

  return mozilla::Some(formatOptions);
}

static const char* DateTimeValueKindToString(DateTimeValueKind kind) {
  switch (kind) {
    case DateTimeValueKind::Number:
      return "number";
    case DateTimeValueKind::TemporalDate:
      return "Temporal.PlainDate";
    case DateTimeValueKind::TemporalTime:
      return "Temporal.PlainTime";
    case DateTimeValueKind::TemporalDateTime:
      return "Temporal.PlainDateTime";
    case DateTimeValueKind::TemporalYearMonth:
      return "Temporal.PlainYearMonth";
    case DateTimeValueKind::TemporalMonthDay:
      return "Temporal.PlainMonthDay";
    case DateTimeValueKind::TemporalZonedDateTime:
      return "Temporal.ZonedDateTime";
    case DateTimeValueKind::TemporalInstant:
      return "Temporal.Instant";
  }
  MOZ_CRASH("invalid date-time value kind");
}

class TimeZoneOffsetString {
  static constexpr std::u16string_view GMT = u"GMT";

  // Time zone offset string format is "±hh:mm".
  static constexpr size_t offsetLength = 6;

  // ICU custom time zones are in the format "GMT±hh:mm".
  char16_t timeZone_[GMT.size() + offsetLength] = {};

  TimeZoneOffsetString() = default;

 public:
  TimeZoneOffsetString(const TimeZoneOffsetString& other) { *this = other; }

  TimeZoneOffsetString& operator=(const TimeZoneOffsetString& other) {
    std::copy_n(other.timeZone_, std::size(timeZone_), timeZone_);
    return *this;
  }

  operator mozilla::Span<const char16_t>() const {
    return mozilla::Span(timeZone_);
  }

  static TimeZoneOffsetString GMTZero() {
    static constexpr std::u16string_view gmtZero = u"GMT+00:00";
    static_assert(gmtZero.length() ==
                  std::extent_v<decltype(TimeZoneOffsetString::timeZone_)>);

    TimeZoneOffsetString result{};
    gmtZero.copy(result.timeZone_, gmtZero.length());
    return result;
  }

  /**
   * |timeZone| is either a canonical IANA time zone identifier or a normalized
   * time zone offset string.
   */
  static mozilla::Maybe<TimeZoneOffsetString> from(
      const JSLinearString* timeZone) {
    MOZ_RELEASE_ASSERT(!timeZone->empty(), "time zone is a non-empty string");

    // If the time zone string starts with either "+" or "-", it is a normalized
    // time zone offset string, because (canonical) IANA time zone identifiers
    // can't start with "+" or "-".
    char16_t timeZoneSign = timeZone->latin1OrTwoByteChar(0);
    MOZ_ASSERT(timeZoneSign != 0x2212,
               "Minus sign is normalized to Ascii minus");
    if (timeZoneSign != '+' && timeZoneSign != '-') {
      return mozilla::Nothing();
    }

    // Release assert because we don't want CopyChars to write out-of-bounds.
    MOZ_RELEASE_ASSERT(timeZone->length() == offsetLength);

    // ToValidCanonicalTimeZoneIdentifier normalizes offset strings to the
    // format "±hh:mm".
    MOZ_ASSERT(mozilla::IsAsciiDigit(timeZone->latin1OrTwoByteChar(1)));
    MOZ_ASSERT(mozilla::IsAsciiDigit(timeZone->latin1OrTwoByteChar(2)));
    MOZ_ASSERT(timeZone->latin1OrTwoByteChar(3) == ':');
    MOZ_ASSERT(mozilla::IsAsciiDigit(timeZone->latin1OrTwoByteChar(4)));
    MOZ_ASSERT(mozilla::IsAsciiDigit(timeZone->latin1OrTwoByteChar(5)));

    // ToValidCanonicalTimeZoneIdentifier verifies the offset is at most ±23:59.
#ifdef DEBUG
    auto twoDigit = [&](size_t offset) {
      auto c1 = timeZone->latin1OrTwoByteChar(offset);
      auto c2 = timeZone->latin1OrTwoByteChar(offset + 1);
      return mozilla::AsciiAlphanumericToNumber(c1) * 10 +
             mozilla::AsciiAlphanumericToNumber(c2);
    };

    int32_t hours = twoDigit(1);
    MOZ_ASSERT(0 <= hours && hours <= 23);

    int32_t minutes = twoDigit(4);
    MOZ_ASSERT(0 <= minutes && minutes <= 59);
#endif

    TimeZoneOffsetString result{};

    // Copy the string "GMT" followed by the offset string.
    size_t copied = GMT.copy(result.timeZone_, GMT.size());
    CopyChars(result.timeZone_ + copied, *timeZone);

    return mozilla::Some(result);
  }
};

class TimeZoneChars final {
  JS::AutoStableStringChars timeZone_;
  mozilla::Maybe<TimeZoneOffsetString> timeZoneOffset_;

  mozilla::Maybe<mozilla::Span<const char16_t>> maybeSpan() const {
    if (timeZone_.isTwoByte()) {
      return mozilla::Some(timeZone_.twoByteRange());
    }
    if (timeZoneOffset_) {
      return timeZoneOffset_;
    }
    return mozilla::Nothing();
  }

 public:
  explicit TimeZoneChars(JSContext* cx) : timeZone_(cx) {}

  operator mozilla::Span<const char16_t>() const {
    return maybeSpan().valueOr(mozilla::Span<const char16_t>{});
  }

  operator mozilla::Maybe<mozilla::Span<const char16_t>>() const {
    return maybeSpan();
  }

  bool init(JSContext* cx, JSLinearString* timeZone) {
    auto timeZoneOffset = TimeZoneOffsetString::from(timeZone);
    if (timeZoneOffset) {
      timeZoneOffset_ = std::move(timeZoneOffset);
      return true;
    }
    return timeZone_.initTwoByte(cx, timeZone);
  }

  void setToOffset(const TimeZoneOffsetString& offset) {
    timeZoneOffset_ = mozilla::Some(offset);
  }
};

static bool IsPlainDateTimeValue(DateTimeValueKind kind) {
  switch (kind) {
    case DateTimeValueKind::Number:
    case DateTimeValueKind::TemporalZonedDateTime:
    case DateTimeValueKind::TemporalInstant:
      return false;
    case DateTimeValueKind::TemporalDate:
    case DateTimeValueKind::TemporalTime:
    case DateTimeValueKind::TemporalDateTime:
    case DateTimeValueKind::TemporalYearMonth:
    case DateTimeValueKind::TemporalMonthDay:
      return true;
  }
  MOZ_CRASH("invalid date time value kind");
}

/**
 * Returns a new mozilla::intl::DateTimeFormat with the locale and date-time
 * formatting options of the given DateTimeFormat.
 */
static mozilla::intl::DateTimeFormat* NewDateTimeFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    DateTimeValueKind kind) {
  if (!ResolveLocale(cx, dateTimeFormat)) {
    return nullptr;
  }
  auto dtfOptions = dateTimeFormat->getOptions();

  auto locale = DateTimeFormatLocale(cx, dateTimeFormat);
  if (!locale) {
    return nullptr;
  }

  TimeZoneChars timeZone(cx);
  if (IsPlainDateTimeValue(kind)) {
    timeZone.setToOffset(TimeZoneOffsetString::GMTZero());
  } else {
    if (!timeZone.init(cx, dateTimeFormat->getTimeZone())) {
      return nullptr;
    }
  }

  // This is a DateTimeFormat defined by a pattern option. This is internal
  // to Mozilla, and not part of the ECMA-402 API.
  if (auto* patternString = dateTimeFormat->getPattern()) {
    JS::AutoStableStringChars pattern(cx);
    if (!pattern.initTwoByte(cx, patternString)) {
      return nullptr;
    }

    auto dfResult = mozilla::intl::DateTimeFormat::TryCreateFromPattern(
        mozilla::MakeStringSpan(locale.get()), pattern.twoByteRange(),
        timeZone);
    if (dfResult.isErr()) {
      ReportInternalError(cx, dfResult.unwrapErr());
      return nullptr;
    }
    return dfResult.unwrap().release();
  }

  // This is a DateTimeFormat defined by a time style or date style.
  if (dtfOptions.dateStyle.isSome() || dtfOptions.timeStyle.isSome()) {
    switch (kind) {
      case DateTimeValueKind::TemporalDate:
      case DateTimeValueKind::TemporalYearMonth:
      case DateTimeValueKind::TemporalMonthDay: {
        if (dtfOptions.dateStyle.isNothing()) {
          JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                    JSMSG_INVALID_FORMAT_OPTIONS,
                                    DateTimeValueKindToString(kind));
          return nullptr;
        }
        break;
      }

      case DateTimeValueKind::TemporalTime: {
        if (dtfOptions.timeStyle.isNothing()) {
          JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                    JSMSG_INVALID_FORMAT_OPTIONS,
                                    DateTimeValueKindToString(kind));
          return nullptr;
        }
        break;
      }

      case DateTimeValueKind::Number:
      case DateTimeValueKind::TemporalDateTime:
      case DateTimeValueKind::TemporalZonedDateTime:
      case DateTimeValueKind::TemporalInstant:
        break;
    }

    mozilla::intl::DateTimeFormat::StyleBag style = {
        .date = dtfOptions.dateStyle,
        .time = dtfOptions.timeStyle,
        .hourCycle = dtfOptions.hourCycle,
        .hour12 = dtfOptions.hour12,
    };

    auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();
    auto* dtpg = sharedIntlData.getDateTimePatternGenerator(cx, locale.get());
    if (!dtpg) {
      return nullptr;
    }

    auto dfResult = mozilla::intl::DateTimeFormat::TryCreateFromStyle(
        mozilla::MakeStringSpan(locale.get()), style, dtpg, timeZone);
    if (dfResult.isErr()) {
      ReportInternalError(cx, dfResult.unwrapErr());
      return nullptr;
    }
    auto df = dfResult.unwrap();

    mozilla::EnumSet<DateTimeField> allowedOptions;
    switch (kind) {
      case DateTimeValueKind::TemporalDate:
        allowedOptions = {
            DateTimeField::Weekday, DateTimeField::Era, DateTimeField::Year,
            DateTimeField::Month,   DateTimeField::Day,
        };
        break;
      case DateTimeValueKind::TemporalTime:
        allowedOptions = {
            DateTimeField::DayPeriod,
            DateTimeField::Hour,
            DateTimeField::Minute,
            DateTimeField::Second,
            DateTimeField::FractionalSecondDigits,
        };
        break;
      case DateTimeValueKind::TemporalDateTime:
        allowedOptions = {
            DateTimeField::Weekday, DateTimeField::Era,
            DateTimeField::Year,    DateTimeField::Month,
            DateTimeField::Day,     DateTimeField::DayPeriod,
            DateTimeField::Hour,    DateTimeField::Minute,
            DateTimeField::Second,  DateTimeField::FractionalSecondDigits,
        };
        break;
      case DateTimeValueKind::TemporalYearMonth:
        allowedOptions = {
            DateTimeField::Era,
            DateTimeField::Year,
            DateTimeField::Month,
        };
        break;
      case DateTimeValueKind::TemporalMonthDay:
        allowedOptions = {
            DateTimeField::Month,
            DateTimeField::Day,
        };
        break;

      case DateTimeValueKind::Number:
      case DateTimeValueKind::TemporalZonedDateTime:
      case DateTimeValueKind::TemporalInstant:
        break;
    }

    if (allowedOptions.isEmpty()) {
      return df.release();
    }

    auto baseFormatResult = df->ResolveComponents();
    if (baseFormatResult.isErr()) {
      ReportInternalError(cx, baseFormatResult.unwrapErr());
      return nullptr;
    }
    auto baseFormat = baseFormatResult.unwrap();

    auto adjusted = AdjustDateTimeStyleFormat(baseFormat, allowedOptions);
    if (adjusted.isNothing()) {
      return df.release();
    }

    auto dfAdjustedResult =
        mozilla::intl::DateTimeFormat::TryCreateFromComponents(
            mozilla::MakeStringSpan(locale.get()), *adjusted, dtpg, timeZone);
    if (dfAdjustedResult.isErr()) {
      ReportInternalError(cx, dfAdjustedResult.unwrapErr());
      return nullptr;
    }
    return dfAdjustedResult.unwrap().release();
  }

  // This is a DateTimeFormat defined by a components bag.
  mozilla::intl::DateTimeFormat::ComponentsBag bag = {
      .era = dtfOptions.era,
      .year = dtfOptions.year,
      .month = dtfOptions.month,
      .day = dtfOptions.day,
      .weekday = dtfOptions.weekday,
      .hour = dtfOptions.hour,
      .minute = dtfOptions.minute,
      .second = dtfOptions.second,
      .timeZoneName = dtfOptions.timeZoneName,
      .hour12 = dtfOptions.hour12,
      .hourCycle = dtfOptions.hourCycle,
      .dayPeriod = dtfOptions.dayPeriod,
      .fractionalSecondDigits = dtfOptions.fractionalSecondDigits,
  };

  auto [required, defaults, inherit] = GetDateTimeFormatArgs(dtfOptions, kind);

  auto resolvedBag = GetDateTimeFormat(bag, required, defaults, inherit);
  if (!resolvedBag) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_INVALID_FORMAT_OPTIONS,
                              DateTimeValueKindToString(kind));
    return nullptr;
  }
  bag = *resolvedBag;

  auto& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  auto* dtpg = sharedIntlData.getDateTimePatternGenerator(cx, locale.get());
  if (!dtpg) {
    return nullptr;
  }

  auto dfResult = mozilla::intl::DateTimeFormat::TryCreateFromComponents(
      mozilla::MakeStringSpan(locale.get()), bag, dtpg, timeZone);
  if (dfResult.isErr()) {
    ReportInternalError(cx, dfResult.unwrapErr());
    return nullptr;
  }
  return dfResult.unwrap().release();
}

void js::intl::DateTimeFormatObject::maybeClearCache(DateTimeValueKind kind) {
  if (getDateTimeValueKind() == kind) {
    return;
  }
  setDateTimeValueKind(kind);

  if (auto* df = getDateFormat()) {
    RemoveICUCellMemory(this,
                        DateTimeFormatObject::UDateFormatEstimatedMemoryUse);
    delete df;

    setDateFormat(nullptr);
  }

  if (auto* dif = getDateIntervalFormat()) {
    RemoveICUCellMemory(
        this, DateTimeFormatObject::UDateIntervalFormatEstimatedMemoryUse);
    delete dif;

    setDateIntervalFormat(nullptr);
  }
}

static mozilla::intl::DateTimeFormat* GetOrCreateDateTimeFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    DateTimeValueKind kind) {
  // Clear previously created formatters if their type doesn't match.
  dateTimeFormat->maybeClearCache(kind);

  // Obtain a cached mozilla::intl::DateTimeFormat object.
  if (auto* df = dateTimeFormat->getDateFormat()) {
    return df;
  }

  auto* df = NewDateTimeFormat(cx, dateTimeFormat, kind);
  if (!df) {
    return nullptr;
  }
  dateTimeFormat->setDateFormat(df);

  AddICUCellMemory(dateTimeFormat,
                   DateTimeFormatObject::UDateFormatEstimatedMemoryUse);
  return df;
}

static auto OptionToString(mozilla::intl::DateTimeFormat::HourCycle hourCycle) {
  return HourCycleToString(hourCycle);
}

static auto OptionToString(mozilla::intl::DateTimeFormat::Text text) {
  return TextComponentToString(text);
}

static auto OptionToString(mozilla::intl::DateTimeFormat::Numeric numeric) {
  return NumericComponentToString(numeric);
}

static auto OptionToString(mozilla::intl::DateTimeFormat::Month month) {
  return MonthToString(month);
}

static auto OptionToString(
    mozilla::intl::DateTimeFormat::TimeZoneName timeZoneName) {
  return TimeZoneNameToString(timeZoneName);
}

template <typename T>
static bool SetOptionsProperty(JSContext* cx,
                               MutableHandle<IdValueVector> options,
                               Handle<PropertyName*> name,
                               mozilla::Maybe<T> intlProp) {
  if (!intlProp) {
    return true;
  }
  auto* str = NewStringCopy<CanGC>(cx, OptionToString(*intlProp));
  if (!str) {
    return false;
  }
  return options.emplaceBack(NameToId(name), StringValue(str));
}

enum class IncludeDateTimeFields : bool { No, Yes };

/**
 * Extracts the resolved components from a DateTimeFormat and applies them to
 * the object for resolved components.
 */
static bool ResolveDateTimeFormatComponents(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    MutableHandle<IdValueVector> options,
    IncludeDateTimeFields includeDateTimeFields) {
  auto* df =
      GetOrCreateDateTimeFormat(cx, dateTimeFormat, DateTimeValueKind::Number);
  if (!df) {
    return false;
  }

  auto result = df->ResolveComponents();
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  auto components = result.unwrap();

  // Map the resolved mozilla::intl::DateTimeFormat::ComponentsBag to the
  // options object as returned by DateTimeFormat.prototype.resolvedOptions.
  //
  // Resolved options must match the ordering as defined in:
  // https://tc39.es/ecma402/#sec-intl.datetimeformat.prototype.resolvedoptions

  if (!SetOptionsProperty(cx, options, cx->names().hourCycle,
                          components.hourCycle)) {
    return false;
  }

  if (components.hour12) {
    if (!options.emplaceBack(NameToId(cx->names().hour12),
                             BooleanValue(*components.hour12))) {
      return false;
    }
  }

  if (includeDateTimeFields == IncludeDateTimeFields::No) {
    // Do not include date time fields.
    return true;
  }

  if (!SetOptionsProperty(cx, options, cx->names().weekday,
                          components.weekday)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().era, components.era)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().year, components.year)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().month, components.month)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().day, components.day)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().dayPeriod,
                          components.dayPeriod)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().hour, components.hour)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().minute, components.minute)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().second, components.second)) {
    return false;
  }
  if (!SetOptionsProperty(cx, options, cx->names().timeZoneName,
                          components.timeZoneName)) {
    return false;
  }

  if (components.fractionalSecondDigits) {
    if (!options.emplaceBack(NameToId(cx->names().fractionalSecondDigits),
                             Int32Value(*components.fractionalSecondDigits))) {
      return false;
    }
  }

  return true;
}

/**
 * ToDateTimeFormattable ( value )
 *
 * https://tc39.es/proposal-temporal/#sec-todatetimeformattable
 */
static auto ToDateTimeFormattable(const Value& value) {
  MOZ_ASSERT(!value.isUndefined());

  // Step 1. (Inlined IsTemporalObject)
  if (value.isObject()) {
    auto* obj = CheckedUnwrapStatic(&value.toObject());
    if (obj) {
      if (obj->is<PlainDateObject>()) {
        return DateTimeValueKind::TemporalDate;
      }
      if (obj->is<PlainDateTimeObject>()) {
        return DateTimeValueKind::TemporalDateTime;
      }
      if (obj->is<PlainTimeObject>()) {
        return DateTimeValueKind::TemporalTime;
      }
      if (obj->is<PlainYearMonthObject>()) {
        return DateTimeValueKind::TemporalYearMonth;
      }
      if (obj->is<PlainMonthDayObject>()) {
        return DateTimeValueKind::TemporalMonthDay;
      }
      if (obj->is<ZonedDateTimeObject>()) {
        return DateTimeValueKind::TemporalZonedDateTime;
      }
      if (obj->is<InstantObject>()) {
        return DateTimeValueKind::TemporalInstant;
      }
      return DateTimeValueKind::Number;
    }
  }

  // Step 2. (ToNumber performed in caller)
  return DateTimeValueKind::Number;
}

/**
 * Ensure the calendar value is resolved.
 */
static bool ResolveCalendarValue(JSContext* cx,
                                 Handle<DateTimeFormatObject*> dateTimeFormat) {
  if (dateTimeFormat->getCalendarValue()) {
    return true;
  }

  // Ensure the calendar option is resolved.
  if (!ResolveLocale(cx, dateTimeFormat)) {
    return false;
  }

  Rooted<JSString*> calendarString(cx, ResolveCalendar(cx, dateTimeFormat));
  if (!calendarString) {
    return false;
  }

  Rooted<CalendarValue> calendar(cx);
  if (!CanonicalizeCalendar(cx, calendarString, &calendar)) {
    return false;
  }
  dateTimeFormat->setCalendarValue(calendar);
  return true;
}

/**
 * Throws an error if `dateTimeFormat`'s calendar is not equal to `calendarId`.
 */
static bool ThrowIfCalendarNotEqual(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    CalendarId calendarId) {
  if (!ResolveCalendarValue(cx, dateTimeFormat)) {
    return false;
  }
  auto calendar = dateTimeFormat->getCalendarValue();

  if (calendarId != calendar.identifier()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_CALENDAR_INCOMPATIBLE,
                              CalendarIdentifier(calendarId).data(),
                              CalendarIdentifier(calendar).data());
    return false;
  }
  return true;
}

struct EpochMilliseconds {
  double milliseconds = 0;

  EpochMilliseconds() = default;

  explicit EpochMilliseconds(EpochNanoseconds epochNs)
      : milliseconds(epochNs.floorToMilliseconds()) {}

  explicit EpochMilliseconds(JS::ClippedTime time)
      : milliseconds(time.toDouble()) {
    MOZ_ASSERT(time.isValid());
  }

  double toDouble() const { return milliseconds; }
};

/**
 * HandleDateTimeTemporalDate ( dateTimeFormat, temporalDate )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporaldate
 */
static bool HandleDateTimeTemporalDate(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<PlainDateObject*> unwrappedTemporalDate, EpochMilliseconds* result) {
  auto isoDate = unwrappedTemporalDate->date();
  auto calendarId = unwrappedTemporalDate->calendar().identifier();

  // Step 1.
  if (calendarId != CalendarId::ISO8601) {
    if (!ThrowIfCalendarNotEqual(cx, dateTimeFormat, calendarId)) {
      return false;
    }
  }

  // Step 2.
  auto isoDateTime = ISODateTime{isoDate, {12, 0, 0}};

  // Step 3.
  auto epochNs = GetUTCEpochNanoseconds(isoDateTime);

  // Steps 4-5. (Performed in NewDateTimeFormat)

  // Step 6.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeTemporalYearMonth ( dateTimeFormat, temporalYearMonth )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporalyearmonth
 */
static bool HandleDateTimeTemporalYearMonth(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<PlainYearMonthObject*> unwrappedTemporalYearMonth,
    EpochMilliseconds* result) {
  auto isoDate = unwrappedTemporalYearMonth->date();
  auto calendarId = unwrappedTemporalYearMonth->calendar().identifier();

  // Step 1.
  if (!ThrowIfCalendarNotEqual(cx, dateTimeFormat, calendarId)) {
    return false;
  }

  // Step 2.
  auto isoDateTime = ISODateTime{isoDate, {12, 0, 0}};

  // Step 3.
  auto epochNs = GetUTCEpochNanoseconds(isoDateTime);

  // Steps 4-5. (Performed in NewDateTimeFormat)

  // Step 6.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeTemporalMonthDay ( dateTimeFormat, temporalMonthDay )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporalmonthday
 */
static bool HandleDateTimeTemporalMonthDay(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<PlainMonthDayObject*> unwrappedTemporalMonthDay,
    EpochMilliseconds* result) {
  auto isoDate = unwrappedTemporalMonthDay->date();
  auto calendarId = unwrappedTemporalMonthDay->calendar().identifier();

  // Step 1.
  if (!ThrowIfCalendarNotEqual(cx, dateTimeFormat, calendarId)) {
    return false;
  }

  // Step 2.
  auto isoDateTime = ISODateTime{isoDate, {12, 0, 0}};

  // Step 3.
  auto epochNs = GetUTCEpochNanoseconds(isoDateTime);

  // Steps 4-5. (Performed in NewDateTimeFormat)

  // Step 6.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeTemporalTime ( dateTimeFormat, temporalTime )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporaltime
 */
static bool HandleDateTimeTemporalTime(PlainTimeObject* unwrappedTemporalTime,
                                       EpochMilliseconds* result) {
  auto time = unwrappedTemporalTime->time();

  // Steps 1-2.
  auto isoDateTime = ISODateTime{{1970, 1, 1}, time};

  // Step 3.
  auto epochNs = GetUTCEpochNanoseconds(isoDateTime);

  // Steps 4-5. (Performed in NewDateTimeFormat)

  // Step 6.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeTemporalDateTime ( dateTimeFormat, dateTime )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporaldatetime
 */
static bool HandleDateTimeTemporalDateTime(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<PlainDateTimeObject*> unwrappedDateTime, EpochMilliseconds* result) {
  auto isoDateTime = unwrappedDateTime->dateTime();
  auto calendarId = unwrappedDateTime->calendar().identifier();

  // Step 1.
  if (calendarId != CalendarId::ISO8601) {
    if (!ThrowIfCalendarNotEqual(cx, dateTimeFormat, calendarId)) {
      return false;
    }
  }

  // Step 2.
  auto epochNs = GetUTCEpochNanoseconds(isoDateTime);

  // Step 3. (Performed in NewDateTimeFormat)

  // Step 4.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeTemporalInstant ( dateTimeFormat, instant )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimetemporalinstant
 */
static bool HandleDateTimeTemporalInstant(InstantObject* unwrappedInstant,
                                          EpochMilliseconds* result) {
  // Step 1. (Performed in NewDateTimeFormat)

  // Step 2.
  auto epochNs = unwrappedInstant->epochNanoseconds();
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * Temporal.ZonedDateTime.prototype.toLocaleString ( [ locales [ , options ] ] )
 */
static bool HandleDateTimeTemporalZonedDateTime(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    Handle<ZonedDateTimeObject*> unwrappedZonedDateTime,
    EpochMilliseconds* result) {
  auto epochNs = unwrappedZonedDateTime->epochNanoseconds();
  auto calendarId = unwrappedZonedDateTime->calendar().identifier();

  // Step 4.
  if (calendarId != CalendarId::ISO8601) {
    if (!ThrowIfCalendarNotEqual(cx, dateTimeFormat, calendarId)) {
      return false;
    }
  }

  // Step 5.
  *result = EpochMilliseconds{epochNs};
  return true;
}

/**
 * HandleDateTimeOthers ( dateTimeFormat, x )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimeothers
 */
static bool HandleDateTimeOthers(JSContext* cx, const char* method, double x,
                                 EpochMilliseconds* result) {
  // Step 1.
  auto clipped = JS::TimeClip(x);

  // Step 2.
  if (!clipped.isValid()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_DATE_NOT_FINITE, "DateTimeFormat", method);
    return false;
  }

  // Step 4. (Performed in NewDateTimeFormat)

  // Steps 3 and 5.
  *result = EpochMilliseconds{clipped};
  return true;
}

/**
 * HandleDateTimeValue ( dateTimeFormat, x )
 *
 * https://tc39.es/proposal-temporal/#sec-temporal-handledatetimevalue
 */
static bool HandleDateTimeValue(JSContext* cx, const char* method,
                                Handle<DateTimeFormatObject*> dateTimeFormat,
                                JSObject* x, EpochMilliseconds* result) {
  // Step 1.
  Rooted<JSObject*> unwrapped(cx, CheckedUnwrapStatic(x));
  if (!unwrapped) {
    ReportAccessDenied(cx);
    return false;
  }

  // Step 1.a.
  if (unwrapped->is<PlainDateObject>()) {
    return HandleDateTimeTemporalDate(cx, dateTimeFormat,
                                      unwrapped.as<PlainDateObject>(), result);
  }

  // Step 1.b.
  if (unwrapped->is<PlainYearMonthObject>()) {
    return HandleDateTimeTemporalYearMonth(
        cx, dateTimeFormat, unwrapped.as<PlainYearMonthObject>(), result);
  }

  // Step 1.c.
  if (unwrapped->is<PlainMonthDayObject>()) {
    return HandleDateTimeTemporalMonthDay(
        cx, dateTimeFormat, unwrapped.as<PlainMonthDayObject>(), result);
  }

  // Step 1.d.
  if (unwrapped->is<PlainTimeObject>()) {
    return HandleDateTimeTemporalTime(&unwrapped->as<PlainTimeObject>(),
                                      result);
  }

  // Step 1.e.
  if (unwrapped->is<PlainDateTimeObject>()) {
    return HandleDateTimeTemporalDateTime(
        cx, dateTimeFormat, unwrapped.as<PlainDateTimeObject>(), result);
  }

  // Step 1.f.
  if (unwrapped->is<InstantObject>()) {
    return HandleDateTimeTemporalInstant(&unwrapped->as<InstantObject>(),
                                         result);
  }

  // Step 1.g.
  MOZ_ASSERT(unwrapped->is<ZonedDateTimeObject>());

  // Step 1.h.
  JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_UNEXPECTED_TYPE,
                            "object", unwrapped->getClass()->name);
  return false;
}

struct DateTimeValue {
  EpochMilliseconds time;
  DateTimeValueKind kind{};
};

/**
 * DateTime Format Functions, steps 3-4 and 5 (partial).
 * Intl.DateTimeFormat.prototype.formatToParts, steps 3-4 and 5 (partial).
 */
static bool ToDateTimeValue(JSContext* cx, const char* method,
                            Handle<DateTimeFormatObject*> dateTimeFormat,
                            Handle<JS::Value> date, DateTimeValue* result) {
  // DateTime Format Functions, step 3.
  // Intl.DateTimeFormat.prototype.formatToParts, step 3.
  if (date.isUndefined()) {
    result->time = EpochMilliseconds{DateNow(cx)};
    result->kind = DateTimeValueKind::Number;
    return true;
  }

  // DateTime Format Functions, step 4.
  // Intl.DTF.prototype.formatToParts, step 4.
  auto kind = ToDateTimeFormattable(date);
  result->kind = kind;

  // DateTime Format Functions, step 5. (Call to FormatDateTime)
  // - FormatDateTime, step 1. (Call to PartitionDateTimePattern)
  // -- PartitionDateTimePattern, step 1. (Call to HandleDateTimeValue)
  // --- HandleDateTimeValue, step 1.
  //
  // Intl.DTF.prototype.formatToParts, step 5. (Call to FormatDateTimeToParts)
  // - FormatDateTimeToParts, step 1. (Call to PartitionDateTimePattern)
  // -- PartitionDateTimePattern, step 1. (Call to HandleDateTimeValue)
  // --- HandleDateTimeValue, step 1.
  if (kind != DateTimeValueKind::Number) {
    MOZ_ASSERT(date.isObject());
    return HandleDateTimeValue(cx, method, dateTimeFormat, &date.toObject(),
                               &result->time);
  }

  // ToDateTimeFormattable, step 2. (Call to ToNumber)
  double num;
  if (!JS::ToNumber(cx, date, &num)) {
    return false;
  }

  // DateTime Format Functions, step 5. (Call to FormatDateTime)
  // - FormatDateTime, step 1. (Call to PartitionDateTimePattern)
  // -- PartitionDateTimePattern, step 1. (Call to HandleDateTimeValue)
  // --- HandleDateTimeValue, step 2. (Call to HandleDateTimeOthers)
  //
  // Intl.DTF.prototype.formatToParts, step 5. (Call to FormatDateTimeToParts)
  // - FormatDateTimeToParts, step 1. (Call to PartitionDateTimePattern)
  // -- PartitionDateTimePattern, step 1. (Call to HandleDateTimeValue)
  // --- HandleDateTimeValue, step 2. (Call to HandleDateTimeOthers)
  return HandleDateTimeOthers(cx, method, num, &result->time);
}

/**
 * FormatDateTime ( dateTimeFormat, x )
 * PartitionDateTimePattern ( dateTimeFormat, x )
 *
 * Returns a String value representing x according to the effective locale and
 * the formatting options of the given DateTimeFormat.
 */
static bool FormatDateTime(JSContext* cx,
                           const mozilla::intl::DateTimeFormat* df,
                           EpochMilliseconds x,
                           MutableHandle<JS::Value> result) {
  // FormatDateTime, step 1. (Inlined call to PartitionDateTimePattern)

  // PartitionDateTimePattern, step 1. (Performed in caller)

  // PartitionDateTimePattern, steps 2-7.
  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  auto dfResult = df->TryFormat(x.toDouble(), buffer);
  if (dfResult.isErr()) {
    ReportInternalError(cx, dfResult.unwrapErr());
    return false;
  }

  // FormatDateTime, steps 2-4.
  auto* str = buffer.toString(cx);
  if (!str) {
    return false;
  }
  result.setString(str);
  return true;
}

static bool FormatDateTime(JSContext* cx,
                           Handle<DateTimeFormatObject*> dateTimeFormat,
                           const DateTimeValue& date,
                           MutableHandle<JS::Value> result) {
  auto* df = GetOrCreateDateTimeFormat(cx, dateTimeFormat, date.kind);
  if (!df) {
    return false;
  }
  return FormatDateTime(cx, df, date.time, result);
}

static JSString* DateTimePartTypeToString(
    JSContext* cx, mozilla::intl::DateTimePartType type) {
  switch (type) {
    case mozilla::intl::DateTimePartType::Literal:
      return cx->names().literal;
    case mozilla::intl::DateTimePartType::Era:
      return cx->names().era;
    case mozilla::intl::DateTimePartType::Year:
      return cx->names().year;
    case mozilla::intl::DateTimePartType::YearName:
      return cx->names().yearName;
    case mozilla::intl::DateTimePartType::RelatedYear:
      return cx->names().relatedYear;
    case mozilla::intl::DateTimePartType::Month:
      return cx->names().month;
    case mozilla::intl::DateTimePartType::Day:
      return cx->names().day;
    case mozilla::intl::DateTimePartType::Hour:
      return cx->names().hour;
    case mozilla::intl::DateTimePartType::Minute:
      return cx->names().minute;
    case mozilla::intl::DateTimePartType::Second:
      return cx->names().second;
    case mozilla::intl::DateTimePartType::Weekday:
      return cx->names().weekday;
    case mozilla::intl::DateTimePartType::DayPeriod:
      return cx->names().dayPeriod;
    case mozilla::intl::DateTimePartType::TimeZoneName:
      return cx->names().timeZoneName;
    case mozilla::intl::DateTimePartType::FractionalSecondDigits:
      return cx->names().fractionalSecond;
    case mozilla::intl::DateTimePartType::Unknown:
      return cx->names().unknown;
  }

  MOZ_CRASH(
      "unenumerated, undocumented format field returned "
      "by iterator");
}

static JSString* DateTimePartSourceToString(
    JSContext* cx, mozilla::intl::DateTimePartSource source) {
  switch (source) {
    case mozilla::intl::DateTimePartSource::Shared:
      return cx->names().shared;
    case mozilla::intl::DateTimePartSource::StartRange:
      return cx->names().startRange;
    case mozilla::intl::DateTimePartSource::EndRange:
      return cx->names().endRange;
  }

  MOZ_CRASH(
      "unenumerated, undocumented format field returned "
      "by iterator");
}

enum class DateTimeSource : bool { No, Yes };

/**
 * FormatDateTimeToParts ( dateTimeFormat, x )
 * FormatDateTimeRangeToParts ( dateTimeFormat, x, y )
 *
 * Create the part object for FormatDateTime{Range}ToParts.
 */
static PlainObject* CreateDateTimePart(JSContext* cx,
                                       const mozilla::intl::DateTimePart& part,
                                       Handle<JSString*> value,
                                       DateTimeSource dateTimeSource) {
  Rooted<IdValueVector> properties(cx, cx);

  auto* type = DateTimePartTypeToString(cx, part.mType);
  if (!properties.emplaceBack(NameToId(cx->names().type), StringValue(type))) {
    return nullptr;
  }

  if (!properties.emplaceBack(NameToId(cx->names().value),
                              StringValue(value))) {
    return nullptr;
  }

  if (dateTimeSource == DateTimeSource::Yes) {
    auto* source = DateTimePartSourceToString(cx, part.mSource);
    if (!properties.emplaceBack(NameToId(cx->names().source),
                                StringValue(source))) {
      return nullptr;
    }
  }

  return NewPlainObjectWithUniqueNames(cx, properties);
}

/**
 * FormatDateTimeToParts ( dateTimeFormat, x )
 * FormatDateTimeRangeToParts ( dateTimeFormat, x, y )
 *
 * Create the parts array for FormatDateTime{Range}ToParts.
 */
static bool CreateDateTimePartArray(
    JSContext* cx, mozilla::Span<const char16_t> formattedSpan,
    DateTimeSource dateTimeSource,
    const mozilla::intl::DateTimePartVector& parts,
    MutableHandle<JS::Value> result) {
  Rooted<JSString*> overallResult(cx, NewStringCopy<CanGC>(cx, formattedSpan));
  if (!overallResult) {
    return false;
  }

  Rooted<ArrayObject*> partsArray(
      cx, NewDenseFullyAllocatedArray(cx, parts.length()));
  if (!partsArray) {
    return false;
  }
  partsArray->ensureDenseInitializedLength(0, parts.length());

  if (overallResult->empty()) {
    // An empty string contains no parts, so avoid extra work below.
    result.setObject(*partsArray);
    return true;
  }

  Rooted<JSString*> value(cx);

  size_t index = 0;
  size_t beginIndex = 0;
  for (const auto& part : parts) {
    MOZ_ASSERT(part.mEndIndex > beginIndex);
    value = NewDependentString(cx, overallResult, beginIndex,
                               part.mEndIndex - beginIndex);
    if (!value) {
      return false;
    }
    beginIndex = part.mEndIndex;

    auto* obj = CreateDateTimePart(cx, part, value, dateTimeSource);
    if (!obj) {
      return false;
    }
    partsArray->initDenseElement(index++, ObjectValue(*obj));
  }
  MOZ_ASSERT(index == parts.length());
  MOZ_ASSERT(beginIndex == formattedSpan.size());

  result.setObject(*partsArray);
  return true;
}

/**
 * FormatDateTimeToParts ( dateTimeFormat, x )
 */
static bool FormatDateTimeToParts(JSContext* cx,
                                  const mozilla::intl::DateTimeFormat* df,
                                  EpochMilliseconds x,
                                  DateTimeSource dateTimeSource,
                                  MutableHandle<JS::Value> result) {
  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> buffer(cx);
  mozilla::intl::DateTimePartVector parts;
  auto r = df->TryFormatToParts(x.toDouble(), buffer, parts);
  if (r.isErr()) {
    ReportInternalError(cx, r.unwrapErr());
    return false;
  }

  return CreateDateTimePartArray(cx, buffer, dateTimeSource, parts, result);
}

/**
 * FormatDateTimeToParts ( dateTimeFormat, x )
 */
static bool FormatDateTimeToParts(JSContext* cx,
                                  Handle<DateTimeFormatObject*> dateTimeFormat,
                                  const DateTimeValue& date,
                                  MutableHandle<JS::Value> result) {
  auto* df = GetOrCreateDateTimeFormat(cx, dateTimeFormat, date.kind);
  if (!df) {
    return false;
  }
  return FormatDateTimeToParts(cx, df, date.time, DateTimeSource::No, result);
}

struct DateTimeRangeValue {
  EpochMilliseconds start;
  EpochMilliseconds end;
  DateTimeValueKind kind{};
};

/**
 * Intl.DateTimeFormat.prototype.formatRange, steps 3-5 and 6 (partial).
 * Intl.DateTimeFormat.prototype.formatRangeToParts, steps 3-5 and 6 (partial).
 */
static bool ToDateTimeRangeValue(JSContext* cx, const char* method,
                                 Handle<DateTimeFormatObject*> dateTimeFormat,
                                 Handle<JS::Value> start, Handle<JS::Value> end,
                                 DateTimeRangeValue* result) {
  // Intl.DateTimeFormat.prototype.formatRange, step 3.
  // Intl.DateTimeFormat.prototype.formatRangeToParts, step 3.
  if (start.isUndefined() || end.isUndefined()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_UNDEFINED_DATE,
                              start.isUndefined() ? "start" : "end", method);
    return false;
  }

  // Intl.DateTimeFormat.prototype.formatRange, step 4.
  // Intl.DateTimeFormat.prototype.formatRangeToParts, step 4.
  auto startKind = ToDateTimeFormattable(start);

  // Intl.DateTimeFormat.prototype.formatRange, step 5.
  // Intl.DateTimeFormat.prototype.formatRangeToParts, step 5.
  auto endKind = ToDateTimeFormattable(end);

  // ToDateTimeFormattable, step 2. (Call to ToNumber)
  double startNum;
  if (startKind == DateTimeValueKind::Number) {
    if (!JS::ToNumber(cx, start, &startNum)) {
      return false;
    }
  }

  // ToDateTimeFormattable, step 2. (Call to ToNumber)
  double endNum;
  if (endKind == DateTimeValueKind::Number) {
    if (!JS::ToNumber(cx, end, &endNum)) {
      return false;
    }
  }

  // clang-format off
  //
  // Intl.DTF.prototype.formatRange, step 6. (Call to FormatDateTimeRange)
  // - FormatDateTimeRange, step 1. (Call to PartitionDateTimeRangePattern)
  //
  // Intl.DTF.prototype.formatRangeToParts, step 6. (Call to FormatDateTimeRangeToParts)
  // - FormatDateTimeRangeToParts, step 1. (Call to PartitionDateTimeRangePattern)
  //
  // clang-format on

  // PartitionDateTimeRangePattern, step 1.
  if (startKind != endKind) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NOT_EXPECTED_TYPE, method,
                              DateTimeValueKindToString(startKind),
                              DateTimeValueKindToString(endKind));
    return false;
  }
  result->kind = startKind;

  // PartitionDateTimeRangePattern, steps 2-3. (Call to HandleDateTimeValue)

  // HandleDateTimeValue, step 1.
  if (startKind != DateTimeValueKind::Number) {
    MOZ_ASSERT(start.isObject());
    MOZ_ASSERT(end.isObject());
    return HandleDateTimeValue(cx, method, dateTimeFormat, &start.toObject(),
                               &result->start) &&
           HandleDateTimeValue(cx, method, dateTimeFormat, &end.toObject(),
                               &result->end);
  }

  // HandleDateTimeValue, step 2.
  return HandleDateTimeOthers(cx, method, startNum, &result->start) &&
         HandleDateTimeOthers(cx, method, endNum, &result->end);
}

bool js::intl::FormatDateTime(JSContext* cx,
                              Handle<DateTimeFormatObject*> dateTimeFormat,
                              double millis, MutableHandle<Value> result) {
  auto x = JS::TimeClip(millis);
  MOZ_ASSERT(x.isValid());

  auto epochMillis = EpochMilliseconds{x};
  auto dateTime = DateTimeValue{epochMillis, DateTimeValueKind::Number};
  return FormatDateTime(cx, dateTimeFormat, dateTime, result);
}

/**
 * Returns a new DateIntervalFormat with the locale and date-time formatting
 * options of the given DateTimeFormat.
 */
static mozilla::intl::DateIntervalFormat* NewDateIntervalFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    mozilla::intl::DateTimeFormat& mozDtf, DateTimeValueKind kind) {
  if (!ResolveLocale(cx, dateTimeFormat)) {
    return nullptr;
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> pattern(cx);
  auto result = mozDtf.GetPattern(pattern);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }

  // Determine the hour cycle used in the resolved pattern.
  auto hcPattern = mozilla::intl::DateTimeFormat::HourCycleFromPattern(pattern);

  auto locale = DateTimeFormatLocale(cx, dateTimeFormat, hcPattern);
  if (!locale) {
    return nullptr;
  }

  TimeZoneChars timeZone(cx);
  if (IsPlainDateTimeValue(kind)) {
    timeZone.setToOffset(TimeZoneOffsetString::GMTZero());
  } else {
    if (!timeZone.init(cx, dateTimeFormat->getTimeZone())) {
      return nullptr;
    }
  }

  FormatBuffer<char16_t, INITIAL_CHAR_BUFFER_SIZE> skeleton(cx);
  auto skelResult = mozDtf.GetOriginalSkeleton(skeleton);
  if (skelResult.isErr()) {
    ReportInternalError(cx, skelResult.unwrapErr());
    return nullptr;
  }

  auto dif = mozilla::intl::DateIntervalFormat::TryCreate(
      mozilla::MakeStringSpan(locale.get()), skeleton, timeZone);
  if (dif.isErr()) {
    ReportInternalError(cx, dif.unwrapErr());
    return nullptr;
  }
  return dif.unwrap().release();
}

static mozilla::intl::DateIntervalFormat* GetOrCreateDateIntervalFormat(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    mozilla::intl::DateTimeFormat& mozDtf, DateTimeValueKind kind) {
  dateTimeFormat->maybeClearCache(kind);

  // Obtain a cached DateIntervalFormat object.
  if (auto* dif = dateTimeFormat->getDateIntervalFormat()) {
    return dif;
  }

  auto* dif = NewDateIntervalFormat(cx, dateTimeFormat, mozDtf, kind);
  if (!dif) {
    return nullptr;
  }
  dateTimeFormat->setDateIntervalFormat(dif);

  AddICUCellMemory(dateTimeFormat,
                   DateTimeFormatObject::UDateIntervalFormatEstimatedMemoryUse);
  return dif;
}

/**
 * PartitionDateTimeRangePattern ( dateTimeFormat, x, y )
 */
static bool PartitionDateTimeRangePattern(
    JSContext* cx, const mozilla::intl::DateTimeFormat* df,
    const mozilla::intl::DateIntervalFormat* dif,
    mozilla::intl::AutoFormattedDateInterval& formatted, EpochMilliseconds x,
    EpochMilliseconds y, bool* equal) {
  auto result =
      dif->TryFormatDateTime(x.toDouble(), y.toDouble(), df, formatted, equal);
  if (result.isErr()) {
    ReportInternalError(cx, result.unwrapErr());
    return false;
  }
  return true;
}

/**
 * FormatDateTimeRange( dateTimeFormat, x, y )
 *
 * Returns a String value representing the range between x and y according to
 * the effective locale and the formatting options of the given DateTimeFormat.
 */
static bool FormatDateTimeRange(JSContext* cx,
                                Handle<DateTimeFormatObject*> dateTimeFormat,
                                const DateTimeRangeValue& values,
                                MutableHandle<JS::Value> result) {
  auto* df = GetOrCreateDateTimeFormat(cx, dateTimeFormat, values.kind);
  if (!df) {
    return false;
  }
  auto* dif =
      GetOrCreateDateIntervalFormat(cx, dateTimeFormat, *df, values.kind);
  if (!dif) {
    return false;
  }

  mozilla::intl::AutoFormattedDateInterval formatted;
  if (!formatted.IsValid()) {
    ReportInternalError(cx, formatted.GetError());
    return false;
  }

  bool equal;
  if (!PartitionDateTimeRangePattern(cx, df, dif, formatted, values.start,
                                     values.end, &equal)) {
    return false;
  }

  // PartitionDateTimeRangePattern, step 12.
  if (equal) {
    return FormatDateTime(cx, df, values.start, result);
  }

  auto spanResult = formatted.ToSpan();
  if (spanResult.isErr()) {
    ReportInternalError(cx, spanResult.unwrapErr());
    return false;
  }

  auto* resultStr = NewStringCopy<CanGC>(cx, spanResult.unwrap());
  if (!resultStr) {
    return false;
  }
  result.setString(resultStr);
  return true;
}

/**
 * FormatDateTimeRangeToParts ( dateTimeFormat, x, y )
 */
static bool FormatDateTimeRangeToParts(
    JSContext* cx, Handle<DateTimeFormatObject*> dateTimeFormat,
    const DateTimeRangeValue& values, MutableHandle<JS::Value> result) {
  auto* df = GetOrCreateDateTimeFormat(cx, dateTimeFormat, values.kind);
  if (!df) {
    return false;
  }
  auto* dif =
      GetOrCreateDateIntervalFormat(cx, dateTimeFormat, *df, values.kind);
  if (!dif) {
    return false;
  }

  mozilla::intl::AutoFormattedDateInterval formatted;
  if (!formatted.IsValid()) {
    ReportInternalError(cx, formatted.GetError());
    return false;
  }

  bool equal;
  if (!PartitionDateTimeRangePattern(cx, df, dif, formatted, values.start,
                                     values.end, &equal)) {
    return false;
  }

  // PartitionDateTimeRangePattern, step 12.
  if (equal) {
    return FormatDateTimeToParts(cx, df, values.start, DateTimeSource::Yes,
                                 result);
  }

  mozilla::intl::DateTimePartVector parts;
  auto r = dif->TryFormattedToParts(formatted, parts);
  if (r.isErr()) {
    ReportInternalError(cx, r.unwrapErr());
    return false;
  }

  auto spanResult = formatted.ToSpan();
  if (spanResult.isErr()) {
    ReportInternalError(cx, spanResult.unwrapErr());
    return false;
  }
  return CreateDateTimePartArray(cx, spanResult.unwrap(), DateTimeSource::Yes,
                                 parts, result);
}

static bool IsDateTimeFormat(Handle<JS::Value> v) {
  return v.isObject() && v.toObject().is<DateTimeFormatObject>();
}

/**
 * UnwrapDateTimeFormat ( dtf )
 */
static bool UnwrapDateTimeFormat(JSContext* cx, MutableHandle<JS::Value> dtf) {
  // Step 1. (Error handling moved to caller)
  if (!dtf.isObject()) {
    return true;
  }

  auto* obj = &dtf.toObject();
  if (obj->canUnwrapAs<DateTimeFormatObject>()) {
    return true;
  }

  Rooted<JSObject*> format(cx, obj);
  return UnwrapLegacyIntlFormat(cx, JSProto_DateTimeFormat, format, dtf);
}

static constexpr uint32_t DateTimeFormatFunction_DateTimeFormat = 0;

/**
 * DateTime Format Functions
 */
static bool DateTimeFormatFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-2.
  auto* format = &args.callee().as<JSFunction>();
  auto dtfValue =
      format->getExtendedSlot(DateTimeFormatFunction_DateTimeFormat);
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &dtfValue.toObject().as<DateTimeFormatObject>());

  // Steps 3-4.
  DateTimeValue x;
  if (!ToDateTimeValue(cx, "format", dateTimeFormat, args.get(0), &x)) {
    return false;
  }

  // Step 5.
  return FormatDateTime(cx, dateTimeFormat, x, args.rval());
}

/**
 * get Intl.DateTimeFormat.prototype.format
 */
static bool dateTimeFormat_format(JSContext* cx, const CallArgs& args) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &args.thisv().toObject().as<DateTimeFormatObject>());

  // Step 4.
  auto* boundFormat = dateTimeFormat->getBoundFormat();
  if (!boundFormat) {
    Handle<PropertyName*> funName = cx->names().empty_;
    auto* fn =
        NewNativeFunction(cx, DateTimeFormatFunction, 1, funName,
                          gc::AllocKind::FUNCTION_EXTENDED, GenericObject);
    if (!fn) {
      return false;
    }
    fn->initExtendedSlot(DateTimeFormatFunction_DateTimeFormat,
                         ObjectValue(*dateTimeFormat));

    dateTimeFormat->setBoundFormat(fn);
    boundFormat = fn;
  }

  // Step 5.
  args.rval().setObject(*boundFormat);
  return true;
}

/**
 * get Intl.DateTimeFormat.prototype.format
 */
static bool dateTimeFormat_format(JSContext* cx, unsigned argc, Value* vp) {
  // Steps 1-3.
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!UnwrapDateTimeFormat(cx, args.mutableThisv())) {
    return false;
  }
  return CallNonGenericMethod<IsDateTimeFormat, dateTimeFormat_format>(cx,
                                                                       args);
}

/**
 * Intl.DateTimeFormat.prototype.formatToParts ( date )
 */
static bool dateTimeFormat_formatToParts(JSContext* cx, const CallArgs& args) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &args.thisv().toObject().as<DateTimeFormatObject>());

  // Steps 3-4.
  DateTimeValue x;
  if (!ToDateTimeValue(cx, "formatRange", dateTimeFormat, args.get(0), &x)) {
    return false;
  }

  // Step 5.
  return FormatDateTimeToParts(cx, dateTimeFormat, x, args.rval());
}

/**
 * Intl.DateTimeFormat.prototype.formatToParts ( date )
 */
static bool dateTimeFormat_formatToParts(JSContext* cx, unsigned argc,
                                         Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDateTimeFormat, dateTimeFormat_formatToParts>(
      cx, args);
}

/**
 * Intl.DateTimeFormat.prototype.formatRange ( startDate, endDate )
 */
static bool dateTimeFormat_formatRange(JSContext* cx, const CallArgs& args) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &args.thisv().toObject().as<DateTimeFormatObject>());

  // Steps 3-5.
  DateTimeRangeValue values;
  if (!ToDateTimeRangeValue(cx, "formatRange", dateTimeFormat, args.get(0),
                            args.get(1), &values)) {
    return false;
  }

  // Step 6.
  return FormatDateTimeRange(cx, dateTimeFormat, values, args.rval());
}

/**
 * Intl.DateTimeFormat.prototype.formatRange ( startDate, endDate )
 */
static bool dateTimeFormat_formatRange(JSContext* cx, unsigned argc,
                                       Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDateTimeFormat, dateTimeFormat_formatRange>(
      cx, args);
}

/**
 * Intl.DateTimeFormat.prototype.formatRangeToParts ( date )
 */
static bool dateTimeFormat_formatRangeToParts(JSContext* cx,
                                              const CallArgs& args) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &args.thisv().toObject().as<DateTimeFormatObject>());

  // Steps 3-5.
  DateTimeRangeValue values;
  if (!ToDateTimeRangeValue(cx, "formatRangeToParts", dateTimeFormat,
                            args.get(0), args.get(1), &values)) {
    return false;
  }

  // Step 6.
  return FormatDateTimeRangeToParts(cx, dateTimeFormat, values, args.rval());
}

/**
 * Intl.DateTimeFormat.prototype.formatRangeToParts ( date )
 */
static bool dateTimeFormat_formatRangeToParts(JSContext* cx, unsigned argc,
                                              Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  return CallNonGenericMethod<IsDateTimeFormat,
                              dateTimeFormat_formatRangeToParts>(cx, args);
}

/**
 * Intl.DateTimeFormat.prototype.resolvedOptions ( )
 */
static bool dateTimeFormat_resolvedOptions(JSContext* cx,
                                           const CallArgs& args) {
  Rooted<DateTimeFormatObject*> dateTimeFormat(
      cx, &args.thisv().toObject().as<DateTimeFormatObject>());

  if (!ResolveLocale(cx, dateTimeFormat)) {
    return false;
  }
  auto dtfOptions = dateTimeFormat->getOptions();

  // Step 3.
  Rooted<IdValueVector> options(cx, cx);

  // Step 4.
  if (!options.emplaceBack(NameToId(cx->names().locale),
                           StringValue(dateTimeFormat->getLocale()))) {
    return false;
  }

  auto* calendar = ResolveCalendar(cx, dateTimeFormat);
  if (!calendar) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().calendar),
                           StringValue(calendar))) {
    return false;
  }

  auto* numberingSystem = ResolveNumberingSystem(cx, dateTimeFormat);
  if (!numberingSystem) {
    return false;
  }
  if (!options.emplaceBack(NameToId(cx->names().numberingSystem),
                           StringValue(numberingSystem))) {
    return false;
  }

  if (!options.emplaceBack(NameToId(cx->names().timeZone),
                           StringValue(dateTimeFormat->getTimeZone()))) {
    return false;
  }

  // The raw pattern option is only internal to Mozilla, and not part of the
  // ECMA-402 API.
  if (auto* pattern = dateTimeFormat->getPattern()) {
    if (!options.emplaceBack(NameToId(cx->names().pattern),
                             StringValue(pattern))) {
      return false;
    }
  }

  bool hasDateStyle = dtfOptions.dateStyle.isSome();
  bool hasTimeStyle = dtfOptions.timeStyle.isSome();

  if (hasDateStyle || hasTimeStyle) {
    if (hasTimeStyle) {
      // timeStyle (unlike dateStyle) requires resolving the pattern to ensure
      // "hourCycle" and "hour12" properties are added to |result|.
      if (!ResolveDateTimeFormatComponents(cx, dateTimeFormat, &options,
                                           IncludeDateTimeFields::No)) {
        return false;
      }
    }

    if (hasDateStyle) {
      auto* dateStyle =
          NewStringCopy<CanGC>(cx, DateStyleToString(*dtfOptions.dateStyle));
      if (!dateStyle) {
        return false;
      }
      if (!options.emplaceBack(NameToId(cx->names().dateStyle),
                               StringValue(dateStyle))) {
        return false;
      }
    }

    if (hasTimeStyle) {
      auto* timeStyle =
          NewStringCopy<CanGC>(cx, TimeStyleToString(*dtfOptions.timeStyle));
      if (!timeStyle) {
        return false;
      }
      if (!options.emplaceBack(NameToId(cx->names().timeStyle),
                               StringValue(timeStyle))) {
        return false;
      }
    }
  } else {
    if (!ResolveDateTimeFormatComponents(cx, dateTimeFormat, &options,
                                         IncludeDateTimeFields::Yes)) {
      return false;
    }
  }

  // Step 6.
  auto* result = NewPlainObjectWithUniqueNames(cx, options);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * Intl.DateTimeFormat.prototype.resolvedOptions ( )
 */
static bool dateTimeFormat_resolvedOptions(JSContext* cx, unsigned argc,
                                           Value* vp) {
  // Steps 1-2.
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!UnwrapDateTimeFormat(cx, args.mutableThisv())) {
    return false;
  }
  return CallNonGenericMethod<IsDateTimeFormat, dateTimeFormat_resolvedOptions>(
      cx, args);
}

/**
 * Intl.DateTimeFormat.supportedLocalesOf ( locales [ , options ] )
 */
static bool dateTimeFormat_supportedLocalesOf(JSContext* cx, unsigned argc,
                                              Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Steps 1-3.
  auto* array = SupportedLocalesOf(cx, AvailableLocaleKind::DateTimeFormat,
                                   args.get(0), args.get(1));
  if (!array) {
    return false;
  }
  args.rval().setObject(*array);
  return true;
}

bool js::intl::TemporalObjectToLocaleString(
    JSContext* cx, const CallArgs& args, DateTimeFormatKind formatKind,
    Handle<JSLinearString*> toLocaleStringTimeZone) {
  Rooted<JSObject*> thisValue(cx, &args.thisv().toObject());

  auto kind = ToDateTimeFormattable(args.thisv());
  MOZ_ASSERT(kind != DateTimeValueKind::Number);
  MOZ_ASSERT_IF(kind != DateTimeValueKind::TemporalZonedDateTime,
                toLocaleStringTimeZone == nullptr);
  MOZ_ASSERT_IF(kind == DateTimeValueKind::TemporalZonedDateTime,
                toLocaleStringTimeZone != nullptr);

  auto locales = args.get(0);
  auto options = args.get(1);

  Rooted<DateTimeFormatObject*> dateTimeFormat(cx);
  if (kind != DateTimeValueKind::TemporalZonedDateTime) {
    dateTimeFormat =
        GetOrCreateDateTimeFormat(cx, locales, options, formatKind);
  } else {
    // Cache doesn't yet support Temporal.ZonedDateTime.
    dateTimeFormat = ::CreateDateTimeFormat(cx, locales, options,
                                            toLocaleStringTimeZone, formatKind);
  }
  if (!dateTimeFormat) {
    return false;
  }

  EpochMilliseconds x;
  if (kind == DateTimeValueKind::TemporalZonedDateTime) {
    auto zonedDateTime = thisValue.as<ZonedDateTimeObject>();
    if (!HandleDateTimeTemporalZonedDateTime(cx, dateTimeFormat, zonedDateTime,
                                             &x)) {
      return false;
    }
  } else {
    if (!HandleDateTimeValue(cx, "toLocaleString", dateTimeFormat, thisValue,
                             &x)) {
      return false;
    }
  }

  return FormatDateTime(cx, dateTimeFormat, {x, kind}, args.rval());
}
