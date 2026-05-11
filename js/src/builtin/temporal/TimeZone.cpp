/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/temporal/TimeZone.h"

#include "mozilla/Assertions.h"
#include "mozilla/intl/TimeZone.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "mozilla/UniquePtr.h"

#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "jstypes.h"
#include "NamespaceImports.h"

#include "builtin/Date.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/SharedIntlData.h"
#include "builtin/temporal/Calendar.h"
#include "builtin/temporal/Instant.h"
#include "builtin/temporal/PlainDate.h"
#include "builtin/temporal/PlainDateTime.h"
#include "builtin/temporal/PlainTime.h"
#include "builtin/temporal/Temporal.h"
#include "builtin/temporal/TemporalParser.h"
#include "builtin/temporal/TemporalTypes.h"
#include "builtin/temporal/TemporalUnit.h"
#include "builtin/temporal/ZonedDateTime.h"
#include "gc/Barrier.h"
#include "gc/GCContext.h"
#include "gc/GCEnum.h"
#include "gc/Tracer.h"
#include "js/AllocPolicy.h"
#include "js/Class.h"
#include "js/ErrorReport.h"
#include "js/friend/ErrorMessages.h"
#include "js/Printer.h"
#include "js/RootingAPI.h"
#include "js/StableStringChars.h"
#include "vm/BytecodeUtil.h"
#include "vm/Compartment.h"
#include "vm/DateTime.h"
#include "vm/JSAtomState.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/Runtime.h"
#include "vm/StringType.h"

#include "vm/JSObject-inl.h"

using namespace js;
using namespace js::temporal;

void js::temporal::TimeZoneValue::trace(JSTracer* trc) {
  TraceRoot(trc, &object_, "TimeZoneValue::object");
}

/**
 * FormatOffsetTimeZoneIdentifier ( offsetMinutes [ , style ] )
 */
static JSLinearString* FormatOffsetTimeZoneIdentifier(JSContext* cx,
                                                      int32_t offsetMinutes) {
  MOZ_ASSERT(std::abs(offsetMinutes) < UnitsPerDay(TemporalUnit::Minute));

  // Step 1.
  char sign = offsetMinutes >= 0 ? '+' : '-';

  // Step 2.
  int32_t absoluteMinutes = std::abs(offsetMinutes);

  // Step 3.
  int32_t hour = absoluteMinutes / 60;

  // Step 4.
  int32_t minute = absoluteMinutes % 60;

  // Step 5. (Inlined FormatTimeString).
  //
  // Format: "sign hour{2} : minute{2}"
  char result[] = {
      sign, char('0' + (hour / 10)),   char('0' + (hour % 10)),
      ':',  char('0' + (minute / 10)), char('0' + (minute % 10)),
  };

  // Step 6.
  return NewStringCopyN<CanGC>(cx, result, std::size(result));
}

TimeZoneObject* js::temporal::CreateTimeZoneObject(
    JSContext* cx, Handle<JSLinearString*> identifier,
    Handle<JSLinearString*> primaryIdentifier) {
  auto* object = NewObjectWithGivenProto<TimeZoneObject>(cx, nullptr);
  if (!object) {
    return nullptr;
  }

  object->initFixedSlot(TimeZoneObject::IDENTIFIER_SLOT,
                        StringValue(identifier));

  object->initFixedSlot(TimeZoneObject::PRIMARY_IDENTIFIER_SLOT,
                        StringValue(primaryIdentifier));

  object->initFixedSlot(TimeZoneObject::OFFSET_MINUTES_SLOT, UndefinedValue());

  return object;
}

static TimeZoneObject* GetOrCreateTimeZoneObject(
    JSContext* cx, Handle<JSLinearString*> identifier,
    Handle<JSLinearString*> primaryIdentifier) {
  return cx->global()->globalIntlData().getOrCreateTimeZone(cx, identifier,
                                                            primaryIdentifier);
}

static TimeZoneObject* CreateTimeZoneObject(JSContext* cx,
                                            int32_t offsetMinutes) {
  // TODO: It's unclear if offset time zones should also be cached. Real world
  // experience will tell if a cache should be added.

  MOZ_ASSERT(std::abs(offsetMinutes) < UnitsPerDay(TemporalUnit::Minute));

  Rooted<JSLinearString*> identifier(
      cx, FormatOffsetTimeZoneIdentifier(cx, offsetMinutes));
  if (!identifier) {
    return nullptr;
  }

  auto* object = NewObjectWithGivenProto<TimeZoneObject>(cx, nullptr);
  if (!object) {
    return nullptr;
  }

  object->initFixedSlot(TimeZoneObject::IDENTIFIER_SLOT,
                        StringValue(identifier));

  object->initFixedSlot(TimeZoneObject::PRIMARY_IDENTIFIER_SLOT,
                        UndefinedValue());

  object->initFixedSlot(TimeZoneObject::OFFSET_MINUTES_SLOT,
                        Int32Value(offsetMinutes));

  return object;
}

static mozilla::UniquePtr<mozilla::intl::TimeZone> CreateIntlTimeZone(
    JSContext* cx, JSLinearString* identifier) {
  MOZ_ASSERT(StringIsAscii(identifier));

  Vector<char, mozilla::intl::TimeZone::TimeZoneIdentifierLength> chars(cx);
  if (!chars.resize(identifier->length())) {
    return nullptr;
  }

  js::CopyChars(reinterpret_cast<JS::Latin1Char*>(chars.begin()), *identifier);

  auto result = mozilla::intl::TimeZone::TryCreate(
      mozilla::Some(static_cast<mozilla::Span<const char>>(chars)));
  if (result.isErr()) {
    intl::ReportInternalError(cx, result.unwrapErr());
    return nullptr;
  }
  return result.unwrap();
}

static mozilla::intl::TimeZone* GetOrCreateIntlTimeZone(
    JSContext* cx, Handle<TimeZoneValue> timeZone) {
  MOZ_ASSERT(!timeZone.isOffset());

  // Obtain a cached mozilla::intl::TimeZone object.
  if (auto* tz = timeZone.getTimeZone()) {
    return tz;
  }

  auto* tz = CreateIntlTimeZone(cx, timeZone.primaryIdentifier()).release();
  if (!tz) {
    return nullptr;
  }

  auto* obj = timeZone.get().toTimeZoneObject();
  obj->setTimeZone(tz);

  intl::AddICUCellMemory(obj, TimeZoneObject::EstimatedMemoryUse);
  return tz;
}

/**
 * IsValidTimeZoneName ( timeZone )
 * IsAvailableTimeZoneName ( timeZone )
 * CanonicalizeTimeZoneName ( timeZone )
 */
static bool ValidateAndCanonicalizeTimeZoneName(
    JSContext* cx, Handle<JSLinearString*> timeZone,
    MutableHandle<JSLinearString*> identifier,
    MutableHandle<JSLinearString*> primaryIdentifier) {
  Rooted<JSAtom*> availableTimeZone(cx);
  Rooted<JSAtom*> primaryTimeZone(cx);
  intl::SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  if (!sharedIntlData.validateAndCanonicalizeTimeZone(
          cx, timeZone, &availableTimeZone, &primaryTimeZone)) {
    return false;
  }

  if (!primaryTimeZone) {
    if (auto chars = QuoteString(cx, timeZone)) {
      JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                               JSMSG_TEMPORAL_TIMEZONE_INVALID_IDENTIFIER,
                               chars.get());
    }
    return false;
  }
  MOZ_ASSERT(availableTimeZone);

  // Links to UTC are handled by SharedIntlData.
  MOZ_ASSERT(!StringEqualsLiteral(primaryTimeZone, "Etc/UTC"));
  MOZ_ASSERT(!StringEqualsLiteral(primaryTimeZone, "Etc/GMT"));

  // We don't need to check against "GMT", because ICU uses the tzdata rearguard
  // format, where "GMT" is a link to "Etc/GMT".
  MOZ_ASSERT(!StringEqualsLiteral(primaryTimeZone, "GMT"));

  identifier.set(availableTimeZone);
  primaryIdentifier.set(primaryTimeZone);
  return true;
}

static bool SystemTimeZoneOffset(JSContext* cx, int32_t* offset) {
  auto rawOffset = DateTimeInfo::getRawOffsetMs(cx->realm()->getDateTimeInfo());
  if (rawOffset.isErr()) {
    intl::ReportInternalError(cx);
    return false;
  }

  *offset = rawOffset.unwrap();
  return true;
}

/**
 * SystemTimeZoneIdentifier ( )
 *
 * Returns the IANA time zone name for the host environment's current time zone.
 */
JSLinearString* js::temporal::ComputeSystemTimeZoneIdentifier(JSContext* cx) {
  TimeZoneIdentifierVector timeZoneId;
  if (!DateTimeInfo::timeZoneId(cx->realm()->getDateTimeInfo(), timeZoneId)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<JSAtom*> availableTimeZone(cx);
  Rooted<JSAtom*> primaryTimeZone(cx);
  intl::SharedIntlData& sharedIntlData = cx->runtime()->sharedIntlData.ref();
  if (!sharedIntlData.validateAndCanonicalizeTimeZone(
          cx, static_cast<mozilla::Span<const char>>(timeZoneId),
          &availableTimeZone, &primaryTimeZone)) {
    return nullptr;
  }
  if (primaryTimeZone) {
    return primaryTimeZone;
  }

  // Before defaulting to "UTC", try to represent the system time zone using
  // the Etc/GMT + offset format. This format only accepts full hour offsets.
  int32_t offset;
  if (!SystemTimeZoneOffset(cx, &offset)) {
    return nullptr;
  }

  constexpr int32_t msPerHour = 60 * 60 * 1000;
  int32_t offsetHours = std::abs(offset / msPerHour);
  int32_t offsetHoursFraction = offset % msPerHour;
  if (offsetHoursFraction == 0 && offsetHours < 24) {
    // Etc/GMT + offset uses POSIX-style signs, i.e. a positive offset
    // means a location west of GMT.
    constexpr std::string_view etcGMT = "Etc/GMT";

    char offsetString[etcGMT.length() + 3];

    size_t n = etcGMT.copy(offsetString, etcGMT.length());
    offsetString[n++] = offset < 0 ? '+' : '-';
    if (offsetHours >= 10) {
      offsetString[n++] = char('0' + (offsetHours / 10));
    }
    offsetString[n++] = char('0' + (offsetHours % 10));

    MOZ_ASSERT(n == etcGMT.length() + 2 || n == etcGMT.length() + 3);

    // Check if the fallback is valid.
    if (!sharedIntlData.validateAndCanonicalizeTimeZone(
            cx, mozilla::Span<const char>{offsetString, n}, &availableTimeZone,
            &primaryTimeZone)) {
      return nullptr;
    }
    if (primaryTimeZone) {
      return primaryTimeZone;
    }
  }

  // Fallback to "UTC" if everything else fails.
  return cx->names().UTC;
}

/**
 * SystemTimeZoneIdentifier ( )
 *
 * Returns the IANA time zone name for the host environment's current time zone.
 */
JSLinearString* js::temporal::SystemTimeZoneIdentifier(JSContext* cx) {
  return cx->global()->globalIntlData().defaultTimeZone(cx);
}

/**
 * SystemTimeZoneIdentifier ( )
 */
bool js::temporal::SystemTimeZone(JSContext* cx,
                                  MutableHandle<TimeZoneValue> result) {
  auto* timeZone =
      cx->global()->globalIntlData().getOrCreateDefaultTimeZone(cx);
  if (!timeZone) {
    return false;
  }

  result.set(TimeZoneValue(timeZone));
  return true;
}

/**
 * GetNamedTimeZoneEpochNanoseconds ( timeZoneIdentifier, isoDateTime )
 */
static bool GetNamedTimeZoneEpochNanoseconds(JSContext* cx,
                                             Handle<TimeZoneValue> timeZone,
                                             const ISODateTime& isoDateTime,
                                             PossibleEpochNanoseconds* result) {
  MOZ_ASSERT(!timeZone.isOffset());
  MOZ_ASSERT(IsValidISODateTime(isoDateTime));
  MOZ_ASSERT(ISODateTimeWithinLimits(isoDateTime));

  // FIXME: spec issue - assert ISODateTimeWithinLimits instead of
  // IsValidISODate

  int64_t ms = MakeDate(isoDateTime);

  auto* tz = GetOrCreateIntlTimeZone(cx, timeZone);
  if (!tz) {
    return false;
  }

  auto getOffset = [&](mozilla::intl::TimeZone::LocalOption skippedTime,
                       mozilla::intl::TimeZone::LocalOption repeatedTime,
                       int32_t* offset) {
    auto result = tz->GetUTCOffsetMs(ms, skippedTime, repeatedTime);
    if (result.isErr()) {
      intl::ReportInternalError(cx, result.unwrapErr());
      return false;
    }

    *offset = result.unwrap();
    MOZ_ASSERT(std::abs(*offset) < UnitsPerDay(TemporalUnit::Millisecond));

    return true;
  };

  constexpr auto formerTime = mozilla::intl::TimeZone::LocalOption::Former;
  constexpr auto latterTime = mozilla::intl::TimeZone::LocalOption::Latter;

  int32_t formerOffset;
  if (!getOffset(formerTime, formerTime, &formerOffset)) {
    return false;
  }

  int32_t latterOffset;
  if (!getOffset(latterTime, latterTime, &latterOffset)) {
    return false;
  }

  if (formerOffset == latterOffset) {
    auto epochNs = GetUTCEpochNanoseconds(isoDateTime) -
                   EpochDuration::fromMilliseconds(formerOffset);
    *result = PossibleEpochNanoseconds{epochNs};
    return true;
  }

  int32_t disambiguationOffset;
  if (!getOffset(formerTime, latterTime, &disambiguationOffset)) {
    return false;
  }

  // Skipped time.
  if (disambiguationOffset == formerOffset) {
    *result = {};
    return true;
  }

  // Repeated time.
  auto formerInstant = GetUTCEpochNanoseconds(isoDateTime) -
                       EpochDuration::fromMilliseconds(formerOffset);
  auto latterInstant = GetUTCEpochNanoseconds(isoDateTime) -
                       EpochDuration::fromMilliseconds(latterOffset);

  // Ensure the returned epoch nanoseconds are sorted in numerical order.
  if (formerInstant > latterInstant) {
    std::swap(formerInstant, latterInstant);
  }

  *result = PossibleEpochNanoseconds{formerInstant, latterInstant};
  return true;
}

/**
 * GetNamedTimeZoneOffsetNanoseconds ( timeZoneIdentifier, epochNanoseconds )
 */
static bool GetNamedTimeZoneOffsetNanoseconds(
    JSContext* cx, Handle<TimeZoneValue> timeZone,
    const EpochNanoseconds& epochNanoseconds, int64_t* offset) {
  MOZ_ASSERT(!timeZone.isOffset());

  // Round down (floor) to the previous full milliseconds.
  int64_t millis = epochNanoseconds.floorToMilliseconds();

  auto* tz = GetOrCreateIntlTimeZone(cx, timeZone);
  if (!tz) {
    return false;
  }

  auto result = tz->GetOffsetMs(millis);
  if (result.isErr()) {
    intl::ReportInternalError(cx, result.unwrapErr());
    return false;
  }

  // FIXME: spec issue - should constrain the range to not exceed 24-hours.
  // https://github.com/tc39/ecma262/issues/3101

  int64_t nanoPerMs = 1'000'000;
  *offset = result.unwrap() * nanoPerMs;
  return true;
}

/**
 * Check if the time zone offset at UTC time |utcMilliseconds1| is the same as
 * the time zone offset at UTC time |utcMilliseconds2|.
 */
static bool EqualTimeZoneOffset(JSContext* cx,
                                mozilla::intl::TimeZone* timeZone,
                                int64_t utcMilliseconds1,
                                int64_t utcMilliseconds2, bool* result) {
  auto offset1 = timeZone->GetOffsetMs(utcMilliseconds1);
  if (offset1.isErr()) {
    intl::ReportInternalError(cx, offset1.unwrapErr());
    return false;
  }

  auto offset2 = timeZone->GetOffsetMs(utcMilliseconds2);
  if (offset2.isErr()) {
    intl::ReportInternalError(cx, offset2.unwrapErr());
    return false;
  }

  *result = offset1.unwrap() == offset2.unwrap();
  return true;
}

/**
 * GetNamedTimeZoneNextTransition ( timeZoneIdentifier, epochNanoseconds )
 */
bool js::temporal::GetNamedTimeZoneNextTransition(
    JSContext* cx, Handle<TimeZoneValue> timeZone,
    const EpochNanoseconds& epochNanoseconds,
    mozilla::Maybe<EpochNanoseconds>* result) {
  MOZ_ASSERT(!timeZone.isOffset());

  // Round down (floor) to the previous full millisecond.
  //
  // IANA has experimental support for transitions at sub-second precision, but
  // the default configuration doesn't enable it, therefore it's safe to round
  // to milliseconds here. In addition to that, ICU also only supports
  // transitions at millisecond precision.
  int64_t millis = epochNanoseconds.floorToMilliseconds();

  auto* tz = GetOrCreateIntlTimeZone(cx, timeZone);
  if (!tz) {
    return false;
  }

  // Skip over transitions which don't change the time zone offset.
  //
  // ICU4C returns all time zone rule changes as transitions, even if the
  // actual time zone offset didn't change. Temporal requires to ignore these
  // rule changes and instead only return transitions if the time zone offset
  // did change.
  while (true) {
    auto next = tz->GetNextTransition(millis);
    if (next.isErr()) {
      intl::ReportInternalError(cx, next.unwrapErr());
      return false;
    }

    // If there's no next transition, we're done.
    auto transition = next.unwrap();
    if (!transition) {
      *result = mozilla::Nothing();
      return true;
    }

    // Check if the time offset at the next transition is equal to the current
    // time zone offset.
    bool equalOffset;
    if (!EqualTimeZoneOffset(cx, tz, millis, *transition, &equalOffset)) {
      return false;
    }

    // If the time zone offset is equal, then search for the next transition
    // after |transition|.
    if (equalOffset) {
      millis = *transition;
      continue;
    }

    // Otherwise return |transition| as the next transition.
    auto transitionInstant = EpochNanoseconds::fromMilliseconds(*transition);
    if (!IsValidEpochNanoseconds(transitionInstant)) {
      *result = mozilla::Nothing();
      return true;
    }

    *result = mozilla::Some(transitionInstant);
    return true;
  }
}

/**
 * GetNamedTimeZonePreviousTransition ( timeZoneIdentifier, epochNanoseconds )
 */
bool js::temporal::GetNamedTimeZonePreviousTransition(
    JSContext* cx, Handle<TimeZoneValue> timeZone,
    const EpochNanoseconds& epochNanoseconds,
    mozilla::Maybe<EpochNanoseconds>* result) {
  MOZ_ASSERT(!timeZone.isOffset());

  // Round up (ceil) to the next full millisecond.
  //
  // IANA has experimental support for transitions at sub-second precision, but
  // the default configuration doesn't enable it, therefore it's safe to round
  // to milliseconds here. In addition to that, ICU also only supports
  // transitions at millisecond precision.
  int64_t millis = epochNanoseconds.ceilToMilliseconds();

  auto* tz = GetOrCreateIntlTimeZone(cx, timeZone);
  if (!tz) {
    return false;
  }

  auto previous = tz->GetPreviousTransition(millis);
  if (previous.isErr()) {
    intl::ReportInternalError(cx, previous.unwrapErr());
    return false;
  }

  // If there's no previous transition, we're done.
  auto transition = previous.unwrap();
  if (!transition) {
    *result = mozilla::Nothing();
    return true;
  }

  // Skip over transitions which don't change the time zone offset.
  //
  // ICU4C returns all time zone rule changes as transitions, even if the
  // actual time zone offset didn't change. Temporal requires to ignore these
  // rule changes and instead only return transitions if the time zone offset
  // did change.
  while (true) {
    // Request the transition before |transition|.
    auto beforePrevious = tz->GetPreviousTransition(*transition);
    if (beforePrevious.isErr()) {
      intl::ReportInternalError(cx, beforePrevious.unwrapErr());
      return false;
    }

    // If there's no before transition, stop searching.
    auto beforePreviousTransition = beforePrevious.unwrap();
    if (!beforePreviousTransition) {
      break;
    }

    // Check if the time zone offset at both transition points is equal.
    bool equalOffset;
    if (!EqualTimeZoneOffset(cx, tz, *transition, *beforePreviousTransition,
                             &equalOffset)) {
      return false;
    }

    // If time zone offset is not equal, then return |transition|.
    if (!equalOffset) {
      break;
    }

    // Otherwise continue searching from |beforePreviousTransition|.
    transition = beforePreviousTransition;
  }

  auto transitionInstant = EpochNanoseconds::fromMilliseconds(*transition);
  if (!IsValidEpochNanoseconds(transitionInstant)) {
    *result = mozilla::Nothing();
    return true;
  }

  *result = mozilla::Some(transitionInstant);
  return true;
}

/**
 * GetStartOfDay ( timeZone, isoDate )
 */
bool js::temporal::GetStartOfDay(JSContext* cx, Handle<TimeZoneValue> timeZone,
                                 const ISODate& isoDate,
                                 EpochNanoseconds* result) {
  MOZ_ASSERT(IsValidISODate(isoDate));

  // Step 1.
  auto isoDateTime = ISODateTime{isoDate, {}};

  // Step 2.
  PossibleEpochNanoseconds possibleEpochNs;
  if (!GetPossibleEpochNanoseconds(cx, timeZone, isoDateTime,
                                   &possibleEpochNs)) {
    return false;
  }
  MOZ_ASSERT(ISODateTimeWithinLimits(isoDateTime));

  // Step 3.
  if (!possibleEpochNs.empty()) {
    *result = possibleEpochNs[0];
    return true;
  }

  // Step 4.
  MOZ_ASSERT(!timeZone.isOffset());

  constexpr auto oneDay = EpochDuration::fromDays(1);

  // Step 5.
  auto previousDayEpochNs = GetUTCEpochNanoseconds(isoDateTime) - oneDay;
  mozilla::Maybe<EpochNanoseconds> transition{};
  if (!GetNamedTimeZoneNextTransition(cx, timeZone, previousDayEpochNs,
                                      &transition)) {
    return false;
  }

  // Step 6.
  MOZ_ASSERT(transition, "time zone transition not found");

  // Step 7.
  *result = *transition;
  return true;
}

/**
 * ToTemporalTimeZoneIdentifier ( temporalTimeZoneLike )
 */
bool js::temporal::ToTemporalTimeZone(JSContext* cx,
                                      Handle<ParsedTimeZone> string,
                                      MutableHandle<TimeZoneValue> result) {
  // Steps 1-3. (Not applicable)

  // Steps 4-5.
  if (!string.name()) {
    auto* obj = CreateTimeZoneObject(cx, string.offset());
    if (!obj) {
      return false;
    }

    result.set(TimeZoneValue(obj));
    return true;
  }

  // Steps 6-8.
  Rooted<JSLinearString*> identifier(cx);
  Rooted<JSLinearString*> primaryIdentifier(cx);
  if (!ValidateAndCanonicalizeTimeZoneName(cx, string.name(), &identifier,
                                           &primaryIdentifier)) {
    return false;
  }

  // Step 9.
  auto* obj = GetOrCreateTimeZoneObject(cx, identifier, primaryIdentifier);
  if (!obj) {
    return false;
  }

  result.set(TimeZoneValue(obj));
  return true;
}

/**
 * ToTemporalTimeZoneIdentifier ( temporalTimeZoneLike )
 */
bool js::temporal::ToTemporalTimeZone(JSContext* cx,
                                      Handle<Value> temporalTimeZoneLike,
                                      MutableHandle<TimeZoneValue> result) {
  // Step 1.
  if (temporalTimeZoneLike.isObject()) {
    JSObject* obj = &temporalTimeZoneLike.toObject();

    // Step 1.a.
    if (auto* zonedDateTime = obj->maybeUnwrapIf<ZonedDateTimeObject>()) {
      result.set(zonedDateTime->timeZone());
      return result.wrap(cx);
    }
  }

  // Step 2.
  if (!temporalTimeZoneLike.isString()) {
    ReportValueError(cx, JSMSG_UNEXPECTED_TYPE, JSDVG_IGNORE_STACK,
                     temporalTimeZoneLike, nullptr, "not a string");
    return false;
  }
  Rooted<JSString*> identifier(cx, temporalTimeZoneLike.toString());

  // Step 3.
  Rooted<ParsedTimeZone> timeZoneName(cx);
  if (!ParseTemporalTimeZoneString(cx, identifier, &timeZoneName)) {
    return false;
  }

  // Steps 4-9.
  return ToTemporalTimeZone(cx, timeZoneName, result);
}

JSLinearString* js::temporal::ToValidCanonicalTimeZoneIdentifier(
    JSContext* cx, Handle<JSString*> timeZone) {
  Rooted<ParsedTimeZone> parsedTimeZone(cx);
  if (!ParseTimeZoneIdentifier(cx, timeZone, &parsedTimeZone)) {
    // TODO: Test262 expects the time zone string is part of the error message,
    // so we have to overwrite the error message.
    //
    // https://github.com/tc39/test262/pull/4463
    if (!cx->isExceptionPending() || cx->isThrowingOutOfMemory()) {
      return nullptr;
    }

    // Clear the previous exception to ensure the error stack is recomputed.
    cx->clearPendingException();

    if (auto chars = QuoteString(cx, timeZone)) {
      JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                               JSMSG_TEMPORAL_TIMEZONE_INVALID_IDENTIFIER,
                               chars.get());
    }
    return nullptr;
  }

  auto timeZoneId = parsedTimeZone.name();
  if (timeZoneId) {
    Rooted<JSLinearString*> identifier(cx);
    Rooted<JSLinearString*> primaryIdentifier(cx);
    if (!ValidateAndCanonicalizeTimeZoneName(cx, timeZoneId, &identifier,
                                             &primaryIdentifier)) {
      return nullptr;
    }
    return primaryIdentifier;
  }

  int32_t offsetMinutes = parsedTimeZone.offset();
  MOZ_ASSERT(std::abs(offsetMinutes) < UnitsPerDay(TemporalUnit::Minute));

  return FormatOffsetTimeZoneIdentifier(cx, offsetMinutes);
}

/**
 * GetOffsetNanosecondsFor ( timeZone, epochNs )
 */
bool js::temporal::GetOffsetNanosecondsFor(JSContext* cx,
                                           Handle<TimeZoneValue> timeZone,
                                           const EpochNanoseconds& epochNs,
                                           int64_t* offsetNanoseconds) {
  // Step 1. (Not applicable)

  // Step 2.
  if (timeZone.isOffset()) {
    int32_t offset = timeZone.offsetMinutes();
    MOZ_ASSERT(std::abs(offset) < UnitsPerDay(TemporalUnit::Minute));

    *offsetNanoseconds = int64_t(offset) * ToNanoseconds(TemporalUnit::Minute);
    return true;
  }

  // Step 3.
  int64_t offset;
  if (!GetNamedTimeZoneOffsetNanoseconds(cx, timeZone, epochNs, &offset)) {
    return false;
  }
  MOZ_ASSERT(std::abs(offset) < ToNanoseconds(TemporalUnit::Day));

  *offsetNanoseconds = offset;
  return true;
}

/**
 * TimeZoneEquals ( one, two )
 */
bool js::temporal::TimeZoneEquals(const TimeZoneValue& one,
                                  const TimeZoneValue& two) {
  // Steps 1-3. (Not applicable in our implementation.)

  // Step 4.
  if (!one.isOffset() && !two.isOffset()) {
    return EqualStrings(one.primaryIdentifier(), two.primaryIdentifier());
  }

  // Step 5.
  if (one.isOffset() && two.isOffset()) {
    return one.offsetMinutes() == two.offsetMinutes();
  }

  // Step 6.
  return false;
}

/**
 * GetISOPartsFromEpoch ( epochNanoseconds )
 */
static ISODateTime GetISOPartsFromEpoch(
    const EpochNanoseconds& epochNanoseconds, int64_t offsetNanoseconds) {
  // Step 1.
  MOZ_ASSERT(IsValidEpochNanoseconds(epochNanoseconds));
  MOZ_ASSERT(std::abs(offsetNanoseconds) < ToNanoseconds(TemporalUnit::Day));

  auto totalNanoseconds =
      epochNanoseconds + EpochDuration::fromNanoseconds(offsetNanoseconds);

  // Step 2.
  int32_t remainderNs = totalNanoseconds.nanoseconds % 1'000'000;

  // Step 10. (Reordered)
  //
  // Reordered so the compiler can merge the divisons in steps 2, 3, and 10.
  int32_t millisecond = totalNanoseconds.nanoseconds / 1'000'000;

  // Step 3.
  int64_t epochMilliseconds = totalNanoseconds.floorToMilliseconds();

  // Steps 4-6.
  auto [year, month, day] = ToYearMonthDay(epochMilliseconds);

  // Steps 7-9.
  auto [hour, minute, second] = ToHourMinuteSecond(epochMilliseconds);

  // Step 10. (Moved above)

  // Steps 11-12.
  int32_t microsecond = remainderNs / 1000;

  // Step 13.
  int32_t nanosecond = remainderNs % 1000;

  // Step 14.
  auto isoDate = ISODate{year, month + 1, day};
  MOZ_ASSERT(IsValidISODate(isoDate));

  // Step 15.
  auto time = Time{hour, minute, second, millisecond, microsecond, nanosecond};
  MOZ_ASSERT(IsValidTime(time));

  // Step 16.
  auto result = ISODateTime{isoDate, time};

  // Always within date-time limits when the epoch nanoseconds are within limit.
  MOZ_ASSERT(ISODateTimeWithinLimits(result));

  return result;
}

/**
 * GetISODateTimeFor ( timeZone, epochNs )
 */
ISODateTime js::temporal::GetISODateTimeFor(const EpochNanoseconds& epochNs,
                                            int64_t offsetNanoseconds) {
  MOZ_ASSERT(IsValidEpochNanoseconds(epochNs));
  MOZ_ASSERT(std::abs(offsetNanoseconds) < ToNanoseconds(TemporalUnit::Day));

  // Step 1. (Not applicable)

  // Steps 2-3.
  return GetISOPartsFromEpoch(epochNs, offsetNanoseconds);
}

/**
 * GetISODateTimeFor ( timeZone, epochNs )
 */
bool js::temporal::GetISODateTimeFor(JSContext* cx,
                                     Handle<TimeZoneValue> timeZone,
                                     const EpochNanoseconds& epochNs,
                                     ISODateTime* result) {
  MOZ_ASSERT(IsValidEpochNanoseconds(epochNs));

  // Step 1.
  int64_t offsetNanoseconds;
  if (!GetOffsetNanosecondsFor(cx, timeZone, epochNs, &offsetNanoseconds)) {
    return false;
  }
  MOZ_ASSERT(std::abs(offsetNanoseconds) < ToNanoseconds(TemporalUnit::Day));

  // Steps 2-3.
  *result = GetISODateTimeFor(epochNs, offsetNanoseconds);
  return true;
}

/**
 * GetPossibleEpochNanoseconds ( timeZone, isoDateTime )
 */
bool js::temporal::GetPossibleEpochNanoseconds(
    JSContext* cx, Handle<TimeZoneValue> timeZone,
    const ISODateTime& isoDateTime, PossibleEpochNanoseconds* result) {
  // TODO: https://github.com/tc39/proposal-temporal/pull/3014
  if (!ISODateTimeWithinLimits(isoDateTime)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_PLAIN_DATE_TIME_INVALID);
    return false;
  }

  // Step 1. (Not applicable)

  // Step 2.
  PossibleEpochNanoseconds possibleEpochNanoseconds;
  if (timeZone.isOffset()) {
    int32_t offsetMin = timeZone.offsetMinutes();
    MOZ_ASSERT(std::abs(offsetMin) < UnitsPerDay(TemporalUnit::Minute));

    // Step 2.a.
    auto epochInstant = GetUTCEpochNanoseconds(isoDateTime) -
                        EpochDuration::fromMinutes(offsetMin);

    // Step 2.b.
    possibleEpochNanoseconds = PossibleEpochNanoseconds{epochInstant};
  } else {
    // Step 3.
    if (!GetNamedTimeZoneEpochNanoseconds(cx, timeZone, isoDateTime,
                                          &possibleEpochNanoseconds)) {
      return false;
    }
  }

  MOZ_ASSERT(possibleEpochNanoseconds.length() <= 2);

  // Step 4.
  for (const auto& epochInstant : possibleEpochNanoseconds) {
    if (!IsValidEpochNanoseconds(epochInstant)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_TEMPORAL_INSTANT_INVALID);
      return false;
    }
  }

  // Step 5.
  *result = possibleEpochNanoseconds;
  return true;
}

/**
 * AddTime ( time, timeDuration )
 */
static auto AddTime(const Time& time, int64_t nanoseconds) {
  MOZ_ASSERT(IsValidTime(time));
  MOZ_ASSERT(std::abs(nanoseconds) <= ToNanoseconds(TemporalUnit::Day));

  // Steps 1-2.
  return BalanceTime(time, nanoseconds);
}

/**
 * DisambiguatePossibleEpochNanoseconds ( possibleEpochNs, timeZone,
 * isoDateTime, disambiguation )
 */
bool js::temporal::DisambiguatePossibleEpochNanoseconds(
    JSContext* cx, const PossibleEpochNanoseconds& possibleEpochNs,
    Handle<TimeZoneValue> timeZone, const ISODateTime& isoDateTime,
    TemporalDisambiguation disambiguation, EpochNanoseconds* result) {
  MOZ_ASSERT(IsValidISODateTime(isoDateTime));

  // Steps 1-2.
  if (possibleEpochNs.length() == 1) {
    *result = possibleEpochNs.front();
    return true;
  }

  // Steps 3-4.
  if (!possibleEpochNs.empty()) {
    // Step 3.a.
    if (disambiguation == TemporalDisambiguation::Earlier ||
        disambiguation == TemporalDisambiguation::Compatible) {
      *result = possibleEpochNs.front();
      return true;
    }

    // Step 3.b.
    if (disambiguation == TemporalDisambiguation::Later) {
      *result = possibleEpochNs.back();
      return true;
    }

    // Step 3.c.
    MOZ_ASSERT(disambiguation == TemporalDisambiguation::Reject);

    // Step 3.d.
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_TEMPORAL_TIMEZONE_INSTANT_AMBIGUOUS);
    return false;
  }

  // Step 5.
  if (disambiguation == TemporalDisambiguation::Reject) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr,
        JSMSG_TEMPORAL_TIMEZONE_INSTANT_AMBIGUOUS_DATE_SKIPPED);
    return false;
  }

  constexpr auto oneDay = EpochDuration::fromDays(1);

  auto epochNanoseconds = GetUTCEpochNanoseconds(isoDateTime);

  // Step 6 and 8-9.
  auto dayBefore = epochNanoseconds - oneDay;
  MOZ_ASSERT(IsValidEpochNanoseconds(dayBefore));

  // Step 7 and 10-11.
  auto dayAfter = epochNanoseconds + oneDay;
  MOZ_ASSERT(IsValidEpochNanoseconds(dayAfter));

  // Step 12.
  int64_t offsetBefore;
  if (!GetOffsetNanosecondsFor(cx, timeZone, dayBefore, &offsetBefore)) {
    return false;
  }
  MOZ_ASSERT(std::abs(offsetBefore) < ToNanoseconds(TemporalUnit::Day));

  // Step 13.
  int64_t offsetAfter;
  if (!GetOffsetNanosecondsFor(cx, timeZone, dayAfter, &offsetAfter)) {
    return false;
  }
  MOZ_ASSERT(std::abs(offsetAfter) < ToNanoseconds(TemporalUnit::Day));

  // Step 14.
  int64_t nanoseconds = offsetAfter - offsetBefore;

  // Step 15.
  MOZ_ASSERT(std::abs(nanoseconds) <= ToNanoseconds(TemporalUnit::Day));

  // Step 16.
  if (disambiguation == TemporalDisambiguation::Earlier) {
    // Steps 16.a-b.
    auto earlierTime = ::AddTime(isoDateTime.time, -nanoseconds);
    MOZ_ASSERT(std::abs(earlierTime.days) <= 1,
               "subtracting nanoseconds is at most one day");

    // Step 16.c.
    auto earlierDate = BalanceISODate(isoDateTime.date,
                                      static_cast<int32_t>(earlierTime.days));

    // Step 16.d.
    auto earlierDateTime = ISODateTime{earlierDate, earlierTime.time};

    // Step 16.e.
    PossibleEpochNanoseconds earlierEpochNs;
    if (!GetPossibleEpochNanoseconds(cx, timeZone, earlierDateTime,
                                     &earlierEpochNs)) {
      return false;
    }

    // Step 16.f.
    MOZ_ASSERT(!earlierEpochNs.empty());

    // Step 16.g.
    *result = earlierEpochNs.front();
    return true;
  }

  // Step 17.
  MOZ_ASSERT(disambiguation == TemporalDisambiguation::Compatible ||
             disambiguation == TemporalDisambiguation::Later);

  // Steps 18-19.
  auto laterTime = ::AddTime(isoDateTime.time, nanoseconds);
  MOZ_ASSERT(std::abs(laterTime.days) <= 1,
             "adding nanoseconds is at most one day");

  // Step 20.
  auto laterDate =
      BalanceISODate(isoDateTime.date, static_cast<int32_t>(laterTime.days));

  // Step 21.
  auto laterDateTime = ISODateTime{laterDate, laterTime.time};

  // Step 22.
  PossibleEpochNanoseconds laterEpochNs;
  if (!GetPossibleEpochNanoseconds(cx, timeZone, laterDateTime,
                                   &laterEpochNs)) {
    return false;
  }

  // Steps 23-24.
  MOZ_ASSERT(!laterEpochNs.empty());

  // Step 25.
  *result = laterEpochNs.back();
  return true;
}

/**
 * GetEpochNanosecondsFor ( timeZone, isoDateTime, disambiguation )
 */
bool js::temporal::GetEpochNanosecondsFor(JSContext* cx,
                                          Handle<TimeZoneValue> timeZone,
                                          const ISODateTime& isoDateTime,
                                          TemporalDisambiguation disambiguation,
                                          EpochNanoseconds* result) {
  // Step 1.
  PossibleEpochNanoseconds possibleEpochNs;
  if (!GetPossibleEpochNanoseconds(cx, timeZone, isoDateTime,
                                   &possibleEpochNs)) {
    return false;
  }

  // Step 2.
  return DisambiguatePossibleEpochNanoseconds(
      cx, possibleEpochNs, timeZone, isoDateTime, disambiguation, result);
}

bool js::temporal::WrapTimeZoneValueObject(
    JSContext* cx, MutableHandle<TimeZoneObject*> timeZone) {
  // Handle the common case when |timeZone| is from the current compartment.
  if (MOZ_LIKELY(timeZone->compartment() == cx->compartment())) {
    return true;
  }

  if (timeZone->isOffset()) {
    auto* obj = CreateTimeZoneObject(cx, timeZone->offsetMinutes());
    if (!obj) {
      return false;
    }

    timeZone.set(obj);
    return true;
  }

  Rooted<JSString*> identifier(cx, timeZone->identifier());
  if (!cx->compartment()->wrap(cx, &identifier)) {
    return false;
  }

  Rooted<JSString*> primaryIdentifier(cx, timeZone->primaryIdentifier());
  if (!cx->compartment()->wrap(cx, &primaryIdentifier)) {
    return false;
  }

  Rooted<JSLinearString*> identifierLinear(cx, identifier->ensureLinear(cx));
  if (!identifierLinear) {
    return false;
  }

  Rooted<JSLinearString*> primaryIdentifierLinear(
      cx, primaryIdentifier->ensureLinear(cx));
  if (!primaryIdentifierLinear) {
    return false;
  }

  auto* obj =
      GetOrCreateTimeZoneObject(cx, identifierLinear, primaryIdentifierLinear);
  if (!obj) {
    return false;
  }

  timeZone.set(obj);
  return true;
}

void js::temporal::TimeZoneObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  MOZ_ASSERT(gcx->onMainThread());

  if (auto* timeZone = obj->as<TimeZoneObject>().getTimeZone()) {
    intl::RemoveICUCellMemory(gcx, obj, EstimatedMemoryUse);
    delete timeZone;
  }
}

const JSClassOps TimeZoneObject::classOps_ = {
    nullptr,                   // addProperty
    nullptr,                   // delProperty
    nullptr,                   // enumerate
    nullptr,                   // newEnumerate
    nullptr,                   // resolve
    nullptr,                   // mayResolve
    TimeZoneObject::finalize,  // finalize
    nullptr,                   // call
    nullptr,                   // construct
    nullptr,                   // trace
};

const JSClass TimeZoneObject::class_ = {
    "Temporal.TimeZone",
    JSCLASS_HAS_RESERVED_SLOTS(TimeZoneObject::SLOT_COUNT) |
        JSCLASS_FOREGROUND_FINALIZE,
    &TimeZoneObject::classOps_,
};
