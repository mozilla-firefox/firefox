/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits>

#include "mozilla/Assertions.h"
#include "mozilla/CumulativeAverage.h"

using mozilla::CumulativeAverage;

class MyClass {
 public:
  double mValue;

  explicit MyClass(double aValue = 0.0) : mValue(aValue) {}

  bool operator==(const MyClass& aOther) const = default;

  MyClass operator-(const MyClass& aOther) const {
    return MyClass(mValue - aOther.mValue);
  }

  MyClass& operator+=(const MyClass& aOther) {
    mValue += aOther.mValue;
    return *this;
  }

  MyClass operator/(uint64_t aDiv) const {
    return MyClass(mValue / static_cast<double>(aDiv));
  }
};

class CumulativeAverageSuite {
 public:
  CumulativeAverageSuite() = default;

  void runTests() {
    testEmpty();
    testSingleInsert();
    testReset();
    testNumericalCorrectness();
    testOppositeSignsNoIntermediateOverflow();
    testSubnormalValues();
    testClass();
    testFloatNoIntermediateOverflow();
  }

 private:
  void testEmpty() {
    CumulativeAverage<double> avg;
    MOZ_RELEASE_ASSERT(avg.empty());
    MOZ_RELEASE_ASSERT(avg.count() == 0);
  }

  void testSingleInsert() {
    CumulativeAverage<double> avg;
    avg.insert(42.0);
    MOZ_RELEASE_ASSERT(!avg.empty());
    MOZ_RELEASE_ASSERT(avg.count() == 1);
    MOZ_RELEASE_ASSERT(avg.mean() == 42.0);
  }

  void testReset() {
    CumulativeAverage<double> avg;
    avg.insert(10.0);
    avg.reset();
    MOZ_RELEASE_ASSERT(avg.empty());
    MOZ_RELEASE_ASSERT(avg.count() == 0);
    avg.insert(5.0);
    MOZ_RELEASE_ASSERT(avg.mean() == 5.0);
  }

  void testNumericalCorrectness() {
    CumulativeAverage<double> avg;
    avg.insert(10.0);
    avg.insert(20.0);
    avg.insert(30.0);
    MOZ_RELEASE_ASSERT(avg.count() == 3);
    MOZ_RELEASE_ASSERT(avg.mean() == 20.0);
  }

  void testOppositeSignsNoIntermediateOverflow() {
    constexpr double kMax = std::numeric_limits<double>::max();
    CumulativeAverage<double> avg;
    avg.insert(kMax);
    avg.insert(-kMax);
    MOZ_RELEASE_ASSERT(avg.mean() == 0.0);

    avg.reset();
    avg.insert(-kMax);
    avg.insert(kMax);
    MOZ_RELEASE_ASSERT(avg.mean() == 0.0);
  }

  void testSubnormalValues() {
    // Smallest representable (subnormal) magnitudes average correctly through
    // the normal recurrence; these do not reach the overflow guard.
    constexpr double kMin = std::numeric_limits<double>::denorm_min();
    CumulativeAverage<double> avg;
    avg.insert(kMin);
    avg.insert(-kMin);
    MOZ_RELEASE_ASSERT(avg.mean() == 0.0);

    avg.reset();
    avg.insert(-kMin);
    avg.insert(kMin);
    MOZ_RELEASE_ASSERT(avg.mean() == 0.0);
  }

  void testClass() {
    // Overflow protection applies only to built-in floating-point types.
    // If kMax is double's maximum, inserting MyClass(kMax) then MyClass(-kMax)
    // makes avg.mean().mValue negative infinity, not 0, because subtraction
    // overflows.
    CumulativeAverage<MyClass> avg;
    MOZ_RELEASE_ASSERT(avg.empty());
    avg.insert(MyClass(4.0));
    MOZ_RELEASE_ASSERT(avg.mean() == MyClass(4.0));
    avg.reset();
    MOZ_RELEASE_ASSERT(avg.empty());
  }

  void testFloatNoIntermediateOverflow() {
    // The naive formula (mean * (n-1) + value) / n overflows float to infinity
    // once mean * n exceeds FLT_MAX (~3.4e38). CumulativeAverage never forms
    // that product, so it stays finite regardless of sample magnitude.
    CumulativeAverage<float> avg;
    constexpr float kLarge = 1e38f;  // 5 * kLarge > FLT_MAX
    for (int i = 0; i < 5; ++i) {
      avg.insert(kLarge);
    }
    MOZ_RELEASE_ASSERT(avg.mean() == kLarge);
  }
};

int main() {
  CumulativeAverageSuite suite;
  suite.runTests();
  return 0;
}
