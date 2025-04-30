/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "UniqueOrNonOwningPtr.h"

using mozilla::MakeUniqueOfUniqueOrNonOwning;
using mozilla::UniqueOrNonOwningPtr;

TEST(UniqueOrNonOwningPtrTest, Empty)
{
#ifdef HAVE_64BIT_BUILD
  using T = uint64_t;
#else
  using T = uint32_t;
#endif
  using Ptr = UniqueOrNonOwningPtr<T>;

  static_assert(sizeof(Ptr) == sizeof(T), "Unexpected size bloat");

  Ptr ptr;
  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);
}

struct Foo {
  uint64_t mA;
  uint32_t mB;

  Foo(uint64_t aA, uint32_t aB) : mA{aA}, mB{aB} {}
  Foo(const Foo& aOther) = default;

  bool operator==(const Foo& aOther) const {
    return mA == aOther.mA && mB == aOther.mB;
  }
};

TEST(UniqueOrNonOwningPtrTest, NonOwningStruct)
{
  using T = Foo;
  using Ptr = UniqueOrNonOwningPtr<T>;

  T val{12, 918};
  Ptr ptr = Ptr::NonOwning(&val);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(ptr.get(), &val);
  EXPECT_EQ(*ptr, val);
  EXPECT_EQ(ptr->mA, val.mA);
  EXPECT_EQ(ptr->mB, val.mB);

  Ptr ptr2 = std::move(ptr);
  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);
  EXPECT_TRUE(ptr2);
  EXPECT_EQ(ptr2.get(), &val);
  EXPECT_EQ(*ptr2, val);
  EXPECT_EQ(ptr2->mA, val.mA);
  EXPECT_EQ(ptr2->mB, val.mB);
}

TEST(UniqueOrNonOwningPtrTest, OwnedStruct)
{
  using T = Foo;
  using Ptr = UniqueOrNonOwningPtr<T>;

  T copy{12, 918};
  Ptr ptr = MakeUniqueOfUniqueOrNonOwning<T>(copy);
  EXPECT_TRUE(ptr);
  EXPECT_NE(ptr.get(), nullptr);
  EXPECT_NE(ptr.get(), &copy);
  EXPECT_EQ(*ptr, copy);
  EXPECT_EQ(ptr->mA, copy.mA);
  EXPECT_EQ(ptr->mB, copy.mB);

  Ptr ptr2 = std::move(ptr);
  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);
  EXPECT_TRUE(ptr2);
  EXPECT_NE(ptr2.get(), nullptr);
  EXPECT_NE(ptr2.get(), &copy);
  EXPECT_EQ(*ptr2, copy);
  EXPECT_EQ(ptr2->mA, copy.mA);
  EXPECT_EQ(ptr2->mB, copy.mB);
}
