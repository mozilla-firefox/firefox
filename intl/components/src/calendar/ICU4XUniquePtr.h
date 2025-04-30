/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_ICU4XUniquePtr_h_
#define intl_components_calendar_ICU4XUniquePtr_h_

#include "mozilla/UniquePtr.h"

#include "ICU4XCalendar.h"
#include "ICU4XDate.h"
#include "ICU4XIsoDate.h"

namespace mozilla::intl::calendar {

class ICU4XCalendarDeleter {
 public:
  void operator()(capi::ICU4XCalendar* ptr) {
    capi::ICU4XCalendar_destroy(ptr);
  }
};

using UniqueICU4XCalendar =
    mozilla::UniquePtr<capi::ICU4XCalendar, ICU4XCalendarDeleter>;

class ICU4XDateDeleter {
 public:
  void operator()(capi::ICU4XDate* ptr) { capi::ICU4XDate_destroy(ptr); }
};

using UniqueICU4XDate = mozilla::UniquePtr<capi::ICU4XDate, ICU4XDateDeleter>;

class ICU4XIsoDateDeleter {
 public:
  void operator()(capi::ICU4XIsoDate* ptr) { capi::ICU4XIsoDate_destroy(ptr); }
};

using UniqueICU4XIsoDate =
    mozilla::UniquePtr<capi::ICU4XIsoDate, ICU4XIsoDateDeleter>;

}  // namespace mozilla::intl::calendar

#endif
