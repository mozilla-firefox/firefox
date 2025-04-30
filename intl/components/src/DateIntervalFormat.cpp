/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DateTimeFormatUtils.h"
#include "ScopedICUObject.h"

#include "mozilla/intl/Calendar.h"
#include "mozilla/intl/DateIntervalFormat.h"
#include "mozilla/intl/DateTimeFormat.h"

#if !MOZ_SYSTEM_ICU
#  include "unicode/calendar.h"
#  include "unicode/datefmt.h"
#  include "unicode/dtitvfmt.h"
#endif

namespace mozilla::intl {

/**
 * PartitionDateTimeRangePattern ( dateTimeFormat, x, y ), steps 9-11.
 *
 * Examine the formatted value to see if any interval span field is present.
 *
 * https://tc39.es/ecma402/#sec-partitiondatetimerangepattern
 */
static ICUResult DateFieldsPracticallyEqual(
    const UFormattedValue* aFormattedValue, bool* aEqual) {
  if (!aFormattedValue) {
    return Err(ICUError::InternalError);
  }

  MOZ_ASSERT(aEqual);
  *aEqual = false;
  UErrorCode status = U_ZERO_ERROR;
  UConstrainedFieldPosition* fpos = ucfpos_open(&status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UConstrainedFieldPosition, ucfpos_close> toCloseFpos(fpos);

  // We're only interested in UFIELD_CATEGORY_DATE_INTERVAL_SPAN fields.
  ucfpos_constrainCategory(fpos, UFIELD_CATEGORY_DATE_INTERVAL_SPAN, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  bool hasSpan = ufmtval_nextPosition(aFormattedValue, fpos, &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  // When no date interval span field was found, both dates are "practically
  // equal" per PartitionDateTimeRangePattern.
  *aEqual = !hasSpan;
  return Ok();
}

/* static */
Result<UniquePtr<DateIntervalFormat>, ICUError> DateIntervalFormat::TryCreate(
    Span<const char> aLocale, Span<const char16_t> aSkeleton,
    Span<const char16_t> aTimeZone) {
  UErrorCode status = U_ZERO_ERROR;
  UDateIntervalFormat* dif =
      udtitvfmt_open(IcuLocale(aLocale), aSkeleton.data(),
                     AssertedCast<int32_t>(aSkeleton.size()), aTimeZone.data(),
                     AssertedCast<int32_t>(aTimeZone.size()), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

  auto result = UniquePtr<DateIntervalFormat>(new DateIntervalFormat(dif));

#if !MOZ_SYSTEM_ICU
  auto* dtif = reinterpret_cast<icu::DateIntervalFormat*>(dif);
  const icu::Calendar* calendar = dtif->getDateFormat()->getCalendar();

  auto replacement = CreateCalendarOverride(calendar);
  if (replacement.isErr()) {
    return replacement.propagateErr();
  }

  if (auto newCalendar = replacement.unwrap()) {
    dtif->adoptCalendar(newCalendar.release());
  }
#endif

  return result;
}

DateIntervalFormat::~DateIntervalFormat() {
  MOZ_ASSERT(mDateIntervalFormat);
  udtitvfmt_close(mDateIntervalFormat.GetMut());
}

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
// We reach inside the UFormattedValue and modify its internal string. (It's
// crucial that this is just an in-place replacement that doesn't alter any
// field positions, etc., )
static void ReplaceSpecialSpaces(const UFormattedValue* aValue) {
  UErrorCode status = U_ZERO_ERROR;
  int32_t len;
  const UChar* str = ufmtval_getString(aValue, &len, &status);
  if (U_FAILURE(status)) {
    return;
  }

  for (const auto& c : Span(str, len)) {
    if (IsSpecialSpace(c)) {
      const_cast<UChar&>(c) = ' ';
    }
  }
}
#endif

ICUResult DateIntervalFormat::TryFormatCalendar(
    const Calendar& aStart, const Calendar& aEnd,
    AutoFormattedDateInterval& aFormatted, bool* aPracticallyEqual) const {
  MOZ_ASSERT(aFormatted.IsValid());

  UErrorCode status = U_ZERO_ERROR;
  udtitvfmt_formatCalendarToResult(mDateIntervalFormat.GetConst(),
                                   aStart.GetUCalendar(), aEnd.GetUCalendar(),
                                   aFormatted.GetFormatted(), &status);

  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
  ReplaceSpecialSpaces(aFormatted.Value());
#endif

  MOZ_TRY(DateFieldsPracticallyEqual(aFormatted.Value(), aPracticallyEqual));
  return Ok();
}

ICUResult DateIntervalFormat::TryFormatDateTime(
    double aStart, double aEnd, AutoFormattedDateInterval& aFormatted,
    bool* aPracticallyEqual) const {
  MOZ_ASSERT(aFormatted.IsValid());

  UErrorCode status = U_ZERO_ERROR;
  udtitvfmt_formatToResult(mDateIntervalFormat.GetConst(), aStart, aEnd,
                           aFormatted.GetFormatted(), &status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }

#if DATE_TIME_FORMAT_REPLACE_SPECIAL_SPACES
  ReplaceSpecialSpaces(aFormatted.Value());
#endif

  MOZ_TRY(DateFieldsPracticallyEqual(aFormatted.Value(), aPracticallyEqual));
  return Ok();
}

ICUResult DateIntervalFormat::TryFormatDateTime(
    double aStart, double aEnd, const DateTimeFormat* aDateTimeFormat,
    AutoFormattedDateInterval& aFormatted, bool* aPracticallyEqual) const {
#if MOZ_SYSTEM_ICU
  // We can't access the calendar used by UDateIntervalFormat to change it to a
  // proleptic Gregorian calendar. Instead we need to call a different formatter
  // function which accepts UCalendar instead of UDate.
  // But creating new UCalendar objects for each call is slow, so when we can
  // ensure that the input dates are later than the Gregorian change date,
  // directly call the formatter functions taking UDate.

  constexpr int32_t msPerDay = 24 * 60 * 60 * 1000;

  // The Gregorian change date "1582-10-15T00:00:00.000Z".
  constexpr double GregorianChangeDate = -12219292800000.0;

  // Add a full day to account for time zone offsets.
  constexpr double GregorianChangeDatePlusOneDay =
      GregorianChangeDate + msPerDay;

  if (aStart < GregorianChangeDatePlusOneDay ||
      aEnd < GregorianChangeDatePlusOneDay) {
    // Create calendar objects for the start and end date by cloning the date
    // formatter calendar. The date formatter calendar already has the correct
    // time zone set and was changed to use a proleptic Gregorian calendar.
    auto startCal = aDateTimeFormat->CloneCalendar(aStart);
    if (startCal.isErr()) {
      return startCal.propagateErr();
    }

    auto endCal = aDateTimeFormat->CloneCalendar(aEnd);
    if (endCal.isErr()) {
      return endCal.propagateErr();
    }

    return TryFormatCalendar(*startCal.unwrap(), *endCal.unwrap(), aFormatted,
                             aPracticallyEqual);
  }
#endif

  // The common fast path which doesn't require creating calendar objects.
  return TryFormatDateTime(aStart, aEnd, aFormatted, aPracticallyEqual);
}

ICUResult DateIntervalFormat::TryFormattedToParts(
    const AutoFormattedDateInterval& aFormatted,
    DateTimePartVector& aParts) const {
  MOZ_ASSERT(aFormatted.IsValid());
  const UFormattedValue* value = aFormatted.Value();
  if (!value) {
    return Err(ICUError::InternalError);
  }

  size_t lastEndIndex = 0;
  auto AppendPart = [&](DateTimePartType type, size_t endIndex,
                        DateTimePartSource source) {
    if (!aParts.emplaceBack(type, endIndex, source)) {
      return false;
    }

    lastEndIndex = endIndex;
    return true;
  };

  UErrorCode status = U_ZERO_ERROR;
  UConstrainedFieldPosition* fpos = ucfpos_open(&status);
  if (U_FAILURE(status)) {
    return Err(ToICUError(status));
  }
  ScopedICUObject<UConstrainedFieldPosition, ucfpos_close> toCloseFpos(fpos);

  size_t categoryEndIndex = 0;
  DateTimePartSource source = DateTimePartSource::Shared;

  while (true) {
    bool hasMore = ufmtval_nextPosition(value, fpos, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }
    if (!hasMore) {
      break;
    }

    int32_t category = ucfpos_getCategory(fpos, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    int32_t field = ucfpos_getField(fpos, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    int32_t beginIndexInt, endIndexInt;
    ucfpos_getIndexes(fpos, &beginIndexInt, &endIndexInt, &status);
    if (U_FAILURE(status)) {
      return Err(ToICUError(status));
    }

    MOZ_ASSERT(beginIndexInt <= endIndexInt,
               "field iterator returning invalid range");

    size_t beginIndex = AssertedCast<size_t>(beginIndexInt);
    size_t endIndex = AssertedCast<size_t>(endIndexInt);

    // Indices are guaranteed to be returned in order (from left to right).
    MOZ_ASSERT(lastEndIndex <= beginIndex,
               "field iteration didn't return fields in order start to "
               "finish as expected");

    if (category == UFIELD_CATEGORY_DATE_INTERVAL_SPAN) {
      // Append any remaining literal parts before changing the source kind.
      if (lastEndIndex < beginIndex) {
        if (!AppendPart(DateTimePartType::Literal, beginIndex, source)) {
          return Err(ICUError::InternalError);
        }
      }

      // The special field category UFIELD_CATEGORY_DATE_INTERVAL_SPAN has only
      // two allowed values (0 or 1), indicating the begin of the start- resp.
      // end-date.
      MOZ_ASSERT(field == 0 || field == 1,
                 "span category has unexpected value");

      source = field == 0 ? DateTimePartSource::StartRange
                          : DateTimePartSource::EndRange;
      categoryEndIndex = endIndex;
      continue;
    }

    // Ignore categories other than UFIELD_CATEGORY_DATE.
    if (category != UFIELD_CATEGORY_DATE) {
      continue;
    }

    DateTimePartType type =
        ConvertUFormatFieldToPartType(static_cast<UDateFormatField>(field));
    if (lastEndIndex < beginIndex) {
      if (!AppendPart(DateTimePartType::Literal, beginIndex, source)) {
        return Err(ICUError::InternalError);
      }
    }

    if (!AppendPart(type, endIndex, source)) {
      return Err(ICUError::InternalError);
    }

    if (endIndex == categoryEndIndex) {
      // Append any remaining literal parts before changing the source kind.
      if (lastEndIndex < endIndex) {
        if (!AppendPart(DateTimePartType::Literal, endIndex, source)) {
          return Err(ICUError::InternalError);
        }
      }

      source = DateTimePartSource::Shared;
    }
  }

  // Append any final literal.
  auto spanResult = aFormatted.ToSpan();
  if (spanResult.isErr()) {
    return spanResult.propagateErr();
  }
  size_t formattedSize = spanResult.unwrap().size();
  if (lastEndIndex < formattedSize) {
    if (!AppendPart(DateTimePartType::Literal, formattedSize, source)) {
      return Err(ICUError::InternalError);
    }
  }

  return Ok();
}

}  // namespace mozilla::intl
