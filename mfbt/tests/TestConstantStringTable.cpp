/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ConstantStringTable.h"

#include "mozilla/Assertions.h"

#include <string_view>

void TestConstantStringTable() {
  using namespace mozilla;
  using namespace std::literals;

  constexpr ConstantStringTable table = StringLiterals<"a", "bb", "ccc">{};
  MOZ_RELEASE_ASSERT(table.size() == 3);
  MOZ_RELEASE_ASSERT(table[0] == "a"sv);
  MOZ_RELEASE_ASSERT(table[1] == "bb"sv);
  MOZ_RELEASE_ASSERT(table[2] == "ccc"sv);
}

void TestConstantStringTableSingleton() {
  using namespace mozilla;
  using namespace std::literals;

  constexpr ConstantStringTable table = StringLiterals<"ccc">{};
  MOZ_RELEASE_ASSERT(table.size() == 1);
  MOZ_RELEASE_ASSERT(table[0] == "ccc"sv);
}

void TestConstantStringTableOffsetType() {
  using namespace mozilla;
  using namespace std::literals;

  constexpr ConstantStringTable small_table = StringLiterals<"ccc">{};
  static_assert(std::is_same_v<decltype(small_table)::offset_type, uint8_t>);

  constexpr ConstantStringTable medium_table = StringLiterals<
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb">{};
  static_assert(std::is_same_v<decltype(medium_table)::offset_type, uint16_t>);
}

int main() {
  TestConstantStringTable();
  TestConstantStringTableSingleton();
  return 0;
}
