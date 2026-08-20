/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Dafsa.h"
#include "nsString.h"

using mozilla::Dafsa;

namespace dafsa_test_1 {
#include "dafsa_test_1.inc"  // kDafsa
}

static_assert(std::is_trivially_destructible_v<Dafsa>);

TEST(Dafsa, Constructor)
{
  static constinit Dafsa d(dafsa_test_1::kDafsa);
  (void)d;
}

TEST(Dafsa, StringsFound)
{
  Dafsa d(dafsa_test_1::kDafsa);

  int tag = d.Lookup("foo.bar.baz"_ns);
  EXPECT_EQ(tag, 1);

  tag = d.Lookup("a.test.string"_ns);
  EXPECT_EQ(tag, 0);

  tag = d.Lookup("a.test.string2"_ns);
  EXPECT_EQ(tag, 2);

  tag = d.Lookup("aaaa"_ns);
  EXPECT_EQ(tag, 4);
}

TEST(Dafsa, StringsNotFound)
{
  Dafsa d(dafsa_test_1::kDafsa);

  // Matches all but last letter.
  int tag = d.Lookup("foo.bar.ba"_ns);
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);

  // Matches prefix with extra letter.
  tag = d.Lookup("a.test.strings"_ns);
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);

  // Matches small portion.
  tag = d.Lookup("a.test"_ns);
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);

  // Matches repeating pattern with extra letters.
  tag = d.Lookup("aaaaa"_ns);
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);

  // Empty string.
  tag = d.Lookup(""_ns);
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);
}

TEST(Dafsa, HugeString)
{
  Dafsa d(dafsa_test_1::kDafsa);

  int tag = d.Lookup(nsLiteralCString(
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "
      "This is a very long string that is larger than the dafsa itself. "));
  EXPECT_EQ(tag, Dafsa::kKeyNotFound);
}
