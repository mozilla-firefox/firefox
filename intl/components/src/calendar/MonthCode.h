/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef intl_components_calendar_MonthCode_h_
#define intl_components_calendar_MonthCode_h_

#include <stddef.h>
#include <stdint.h>
#include <string_view>

namespace mozilla::intl::calendar {

// Copied from js/src/builtin/temporal/MonthCode.h

class MonthCode final {
 public:
  enum class Code {
    Invalid = 0,

    // Months 01 - M12.
    M01 = 1,
    M02,
    M03,
    M04,
    M05,
    M06,
    M07,
    M08,
    M09,
    M10,
    M11,
    M12,

    // Epagomenal month M13.
    M13,

    // Leap months M01 - M12.
    M01L,
    M02L,
    M03L,
    M04L,
    M05L,
    M06L,
    M07L,
    M08L,
    M09L,
    M10L,
    M11L,
    M12L,
  };

 private:
  static constexpr int32_t toLeapMonth =
      static_cast<int32_t>(Code::M01L) - static_cast<int32_t>(Code::M01);

  Code code_ = Code::Invalid;

 public:
  constexpr MonthCode() = default;

  constexpr explicit MonthCode(Code code) : code_(code) {}

  constexpr explicit MonthCode(int32_t month, bool isLeapMonth = false) {
    code_ = static_cast<Code>(month + (isLeapMonth ? toLeapMonth : 0));
  }

  constexpr auto code() const { return code_; }

  constexpr int32_t ordinal() const {
    int32_t ordinal = static_cast<int32_t>(code_);
    if (isLeapMonth()) {
      ordinal -= toLeapMonth;
    }
    return ordinal;
  }

  constexpr bool isLeapMonth() const { return code_ >= Code::M01L; }

  constexpr explicit operator std::string_view() const {
    constexpr const char* name =
        "M01L"
        "M02L"
        "M03L"
        "M04L"
        "M05L"
        "M06L"
        "M07L"
        "M08L"
        "M09L"
        "M10L"
        "M11L"
        "M12L"
        "M13";
    size_t index = (ordinal() - 1) * 4;
    size_t length = 3 + isLeapMonth();
    return {name + index, length};
  }
};

}  // namespace mozilla::intl::calendar

#endif
