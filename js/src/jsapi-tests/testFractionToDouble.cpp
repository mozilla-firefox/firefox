/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef JS_HAS_INTL_API

#  include <cmath>
#  include <stdint.h>

#  include "builtin/temporal/Int128.h"
#  include "builtin/temporal/Temporal.h"
#  include "jsapi-tests/tests.h"

using namespace js::temporal;

// Simple test using numerators and denominators where the result can be
// computed through standard double division.
BEGIN_TEST(testFraction_simple) {
  int64_t numerators[] = {
      0, 1, 2, 10, 100, INT32_MIN, INT32_MAX,
  };

  int64_t denominators[] = {
      1, 2, 3, 10, 100, 1000,
  };

  for (auto numerator : numerators) {
    for (auto denominator : denominators) {
      double result = double(numerator) / double(denominator);

      CHECK_EQUAL(FractionToDouble(numerator, denominator), result);
      CHECK_EQUAL(FractionToDouble(Int128{numerator}, Int128{denominator}),
                  result);

      CHECK_EQUAL(FractionToDouble(-numerator, denominator),
                  std::copysign(result, -numerator));
      CHECK_EQUAL(FractionToDouble(-Int128{numerator}, Int128{denominator}),
                  std::copysign(result, -numerator));
    }
  }

  return true;
}
END_TEST(testFraction_simple)

// Complex test with values exceeding Number.MAX_SAFE_INTEGER.
BEGIN_TEST(testFraction_complex) {
  struct {
    int64_t numerator;
    int64_t denominator;
    double result;
  } values[] = {
      // Number.MAX_SAFE_INTEGER
      {9007199254740991, 2, 4503599627370495.5},
      {9007199254740992, 2, 4503599627370496},
      {9007199254740993, 2, 4503599627370496.5},

      {INT64_MAX, 2, 4611686018427387903.5},
      {INT64_MIN, 2, -4611686018427387904.0},
  };

  for (auto [numerator, denominator, result] : values) {
    CHECK_EQUAL(FractionToDouble(numerator, denominator), result);
    CHECK_EQUAL(FractionToDouble(Int128{numerator}, Int128{denominator}),
                result);
  }

  return true;
}
END_TEST(testFraction_complex)

// Complex test with Int128 values exceeding Number.MAX_SAFE_INTEGER.
BEGIN_TEST(testFraction_complex_int128) {
  struct {
    Int128 numerator;
    Int128 denominator;
    double result;
  } values[] = {
      // Divide 1 by a growing divisor.
      {Int128{1}, Int128{1'000}, 0.001},
      {Int128{1}, Int128{1'000'000}, 0.000'001},
      {Int128{1}, Int128{1'000'000'000}, 0.000'000'001},
      {Int128{1}, Int128{1'000'000'000'000}, 0.000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000}, 0.000'000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000'000}, 0.000'000'000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000'000} * Int128{1'000},
       0.000'000'000'000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000'000} * Int128{1'000'000},
       0.000'000'000'000'000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000},
       0.000'000'000'000'000'000'000'000'001},
      {Int128{1}, Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000},
       0.000'000'000'000'000'000'000'000'000'001},
      {Int128{1},
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000'000},
       0.000'000'000'000'000'000'000'000'000'000'001},
      {Int128{1},
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000'000'000},
       0.000'000'000'000'000'000'000'000'000'000'000'001},

      // Divide a number not representable as an int64.
      {Int128{0x8000'0000} << 64, Int128{1'000},
       39614081257132168796771975.1680},
      {Int128{0x8000'0000} << 64, Int128{1'000'000},
       39614081257132168796771.9751680},
      {Int128{0x8000'0000} << 64, Int128{1'000'000'000},
       39614081257132168796.7719751680},
      {Int128{0x8000'0000} << 64, Int128{1'000'000'000'000},
       39614081257132168.7967719751680},
      {Int128{0x8000'0000} << 64, Int128{1'000'000'000'000'000},
       39614081257132.1687967719751680},
      {Int128{0x8000'0000} << 64, Int128{1'000'000'000'000'000'000},
       39614081257.1321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000},
       39614081.2571321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000},
       39614.0812571321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000},
       39.6140812571321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000},
       0.0396140812571321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000'000},
       0.0000396140812571321687967719751680},
      {Int128{0x8000'0000} << 64,
       Int128{1'000'000'000'000'000'000} * Int128{1'000'000'000'000'000'000},
       0.0000000396140812571321687967719751680},

      // Test divisor which isn't a multiple of ten.
      {Int128{0x8000'0000} << 64, Int128{2}, 19807040628566084398385987584.0},
      {Int128{0x8000'0000} << 64, Int128{3}, 13204693752377389598923991722.666},
      {Int128{0x8000'0000} << 64, Int128{3'333},
       11885412918431493788410433.5937},
      {Int128{0x8000'0000} << 64, Int128{3'333'333},
       11884225565562207195252.3120756},
      {Int128{0x8000'0000} << 64, Int128{3'333'333'333},
       11884224378328073076.864399858},
      {Int128{0x8000'0000} << 64, Int128{3'333'333'333'333},
       11884224377140839.0614693066343},
      {Int128{0x8000'0000} << 64, Int128{3'333'333'333'333'333},
       11884224377139.6518274540302643},
      {Int128{0x8000'0000} << 64, Int128{3'333'333'333'333'333'333},
       11884224377.1396506402200149881},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000}) + Int128{333},
       11884224.3771396506390327809728},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000'000}) +
           Int128{333'333},
       11884.2243771396506390315937388},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000'000'000}) +
           Int128{333'333'333},
       11.884224377139650639031592551588422437713965063903159255158842243},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000'000'000'000}) +
           Int128{333'333'333'333},
       0.0118842243771396506390315925504},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000'000'000'000'000}) +
           Int128{333'333'333'333'333},
       0.0000118842243771396506390315925504},
      {Int128{0x8000'0000} << 64,
       (Int128{3'333'333'333'333'333'333} * Int128{1'000'000'000'000'000'000}) +
           Int128{333'333'333'333'333'333},
       0.0000000118842243771396506390315925504},
  };

  for (auto [numerator, denominator, result] : values) {
    CHECK_EQUAL(FractionToDouble(numerator, denominator), result);
  }

  return true;
}
END_TEST(testFraction_complex_int128)

#endif
