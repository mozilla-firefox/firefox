/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "js/SliceBudget.h"

using namespace js;
using JS::SliceBudget;
using JS::TimeBudget;
using JS::UnlimitedBudget;
using JS::WorkBudget;

using mozilla::TimeDuration;

BEGIN_TEST(testSliceBudgetUnlimited) {
  SliceBudget budget = SliceBudget::unlimited();
  CHECK(budget.isUnlimited());
  CHECK(!budget.isTimeBudget());
  CHECK(!budget.isWorkBudget());

  CHECK(!budget.isOverBudget());

  budget.step(1000000);
  CHECK(!budget.isOverBudget());

  SliceBudget budget2 = SliceBudget(UnlimitedBudget());
  CHECK(budget2.isUnlimited());

  return true;
}
END_TEST(testSliceBudgetUnlimited)

BEGIN_TEST(testSliceBudgetWork) {
  SliceBudget budget = SliceBudget(WorkBudget(10000));
  CHECK(!budget.isUnlimited());
  CHECK(budget.isWorkBudget());
  CHECK(!budget.isTimeBudget());

  CHECK(budget.workBudget() == 10000);

  CHECK(!budget.isOverBudget());

  budget.step(5000);
  CHECK(!budget.isOverBudget());

  budget.step(5000);
  CHECK(budget.isOverBudget());

  return true;
}
END_TEST(testSliceBudgetWork)

BEGIN_TEST(testSliceBudgetTime) {
  SliceBudget budget = SliceBudget(TimeDuration::FromMilliseconds(10000));
  CHECK(!budget.isUnlimited());
  CHECK(!budget.isWorkBudget());
  CHECK(budget.isTimeBudget());

  CHECK(budget.timeBudget() == TimeDuration::FromMilliseconds(10000));

  CHECK(!budget.isOverBudget());

  budget.step(5000);
  budget.step(5000);
  CHECK(!budget.isOverBudget());

  // This doesn't test the deadline is correct as that would require waiting.

  return true;
}
END_TEST(testSliceBudgetTime)

BEGIN_TEST(testSliceBudgetTimeZero) {
  SliceBudget budget = SliceBudget(TimeDuration::FromMilliseconds(0));
  budget.step(1000);
  CHECK(budget.isOverBudget());

  return true;
}
END_TEST(testSliceBudgetTimeZero)

BEGIN_TEST(testSliceBudgetInterruptibleTime) {
  SliceBudget::InterruptRequestFlag wantInterrupt(false);

  // Interruptible 100 second budget. This test will finish in well under that
  // time.
  static constexpr int64_t LONG_TIME = 100;
  SliceBudget budget =
      SliceBudget(TimeDuration::FromSeconds(LONG_TIME), &wantInterrupt);
  CHECK(!budget.isUnlimited());
  CHECK(!budget.isWorkBudget());
  CHECK(budget.isTimeBudget());

  CHECK(budget.timeBudget() == TimeDuration::FromSeconds(LONG_TIME));

  CHECK(!budget.isOverBudget());

  // We do a little work, very small amount of time passes.
  budget.step(500);

  // Not enough work to check interrupt, and no interrupt anyway.
  CHECK(!budget.isOverBudget());

  // External signal: interrupt requested.
  wantInterrupt = true;

  // Interrupt requested, but not enough work has been done to check for it.
  CHECK(!budget.isOverBudget());

  // Do enough work for an expensive check.
  budget.step(1000);

  // Interrupt requested!
  CHECK(budget.isOverBudget());

  // The external flag is reset, but the budget will internally remember that an
  // interrupt was requested until the interrupt is cleared.
  CHECK(wantInterrupt);
  wantInterrupt = false;
  CHECK(budget.isOverBudget());
  budget.clearInterrupted();
  CHECK(!budget.isOverBudget());

  // This doesn't test the deadline is correct as that would require waiting.

  return true;
}
END_TEST(testSliceBudgetInterruptibleTime)

BEGIN_TEST(testSliceBudgetInterruptibleUnlimited) {
  SliceBudget::InterruptRequestFlag wantInterrupt(false);

  SliceBudget budget = SliceBudget(UnlimitedBudget(), &wantInterrupt);
  CHECK(budget.isUnlimited());
  CHECK(!budget.isOverBudget());

  // Do enough work for an expensive check.
  budget.step(1000);
  CHECK(!budget.isOverBudget());

  // We do a little work but not enough work to check interrupt.
  budget.step(500);
  CHECK(!budget.isOverBudget());

  // External signal: interrupt requested.
  wantInterrupt = true;

  // Interrupt requested, but not enough work has been done to check for it.
  CHECK(!budget.isOverBudget());

  // Do enough work for an expensive check.
  budget.step(1000);

  // Interrupt requested!
  CHECK(budget.isOverBudget());

  // The external flag is reset, but the budget will internally remember
  // interrupt was requested until the interrupt is cleared.
  CHECK(wantInterrupt);
  wantInterrupt = false;
  CHECK(budget.isOverBudget());
  budget.clearInterrupted();
  CHECK(!budget.isOverBudget());

  return true;
}
END_TEST(testSliceBudgetInterruptibleUnlimited)
