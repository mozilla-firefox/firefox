/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_DateTimeFormatUtils_h_
#define intl_components_DateTimeFormatUtils_h_
#include "unicode/udat.h"

#if !MOZ_SYSTEM_ICU
#  include "unicode/calendar.h"
#endif

#include "mozilla/Result.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/intl/DateTimePart.h"
#include "mozilla/intl/ICUError.h"

namespace mozilla::intl {
DateTimePartType ConvertUFormatFieldToPartType(UDateFormatField fieldName);

Result<Ok, ICUError> ApplyCalendarOverride(UDateFormat* aDateFormat);

#if !MOZ_SYSTEM_ICU
Result<UniquePtr<icu::Calendar>, ICUError> CreateCalendarOverride(
    const icu::Calendar* calendar);
#endif
}  // namespace mozilla::intl

#endif
