/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Decimal.h"

#include <math.h>

#include "gtest/gtest.h"

using mozilla::Decimal;

// mozilla::Decimal::fromDouble should produce the same decimal representation
// as the shortest-representation form of the double input, but truncated to
// DBL_DIG (15) significant digits when output as a double or string.

TEST(TestDecimal, FromDoubleSmallValuesNotZeroed)
{
  EXPECT_EQ(Decimal::fromDouble(1e-7), Decimal::fromString("1e-7"));
  EXPECT_EQ(Decimal::fromDouble(1.5e-7), Decimal::fromString("1.5e-7"));
  EXPECT_EQ(Decimal::fromDouble(1e-20), Decimal::fromString("1e-20"));
}

TEST(TestDecimal, FromDoubleSerializePrecision)
{
  EXPECT_EQ(Decimal::fromDouble(0.123456789012345).toString(),
            "0.123456789012345");
  // 0.30000000000000004 must not serialize as 0.3, which is a different f64
  // value
  EXPECT_EQ(Decimal::fromDouble(0.30000000000000004).toString(),
            "0.30000000000000004");
  // 0.06 must not serialize as 0.059999999999999998 as that's not the shortest
  // representation
  EXPECT_EQ(Decimal::fromDouble(0.06).toString(), "0.06");
}

TEST(TestDecimal, FromDoubleToDoubleIdentity)
{
  static const double values[] = {
      0.06,
      0.1,
      123.456,
      1234567.891,
      43210.1,
      9007199254740992.0,  // 2^53
      1e-7,
      1.5e-7,
      1e20,
      1e21,
      1e23,
      -123.456,
      -1.5e-7,
  };
  for (double value : values) {
    EXPECT_EQ(Decimal::fromDouble(value).toDouble(), value);
  }
}

TEST(TestDecimal, FromDoubleSpecials)
{
  Decimal posInf = Decimal::fromDouble(INFINITY);
  EXPECT_TRUE(posInf.isInfinity());
  EXPECT_EQ(posInf.toString(), "Infinity");

  Decimal negInf = Decimal::fromDouble(-INFINITY);
  EXPECT_TRUE(negInf.isInfinity());
  EXPECT_TRUE(negInf.isNegative());
  EXPECT_EQ(negInf.toString(), "-Infinity");

  Decimal nan = Decimal::fromDouble(NAN);
  EXPECT_TRUE(nan.isNaN());
  EXPECT_EQ(nan.toString(), "NaN");

  Decimal zero = Decimal::fromDouble(-0.0);
  EXPECT_TRUE(zero.isZero());
  EXPECT_FALSE(zero.isNegative());
}

TEST(TestDecimal, ToStringExponentStyle)
{
  EXPECT_EQ(Decimal::fromString("1e-07").toString(), "1e-7");
  EXPECT_EQ(Decimal::fromString("1.5e-07").toString(), "1.5e-7");
  EXPECT_EQ(Decimal::fromString("1e23").toString(), "1e+23");
  EXPECT_EQ(Decimal::fromString("1e02").toString(), "1e+2");
}
