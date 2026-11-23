// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainyearmonth.prototype.subtract
description: RangeError thrown when going out of range
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const min = Temporal.PlainYearMonth.from("-271821-04");
// The arithmetic starts from the first day of the month, and -271821-04-01 is
// before the first valid date, so the earliest possible result is -271821-05
const monthAfterEarliestResult = Temporal.PlainYearMonth.from("-271821-06");
for (const overflow of ["reject", "constrain"]) {
  assert.throws(RangeError, () => min.subtract({ months: 1 }, { overflow }), overflow);
  TemporalHelpers.assertPlainYearMonth(monthAfterEarliestResult.subtract({ months: 1 }, { overflow }),
    -271821, 5, "M05", `reaching earliest result with ${overflow}`);
}

reportCompare(0, 0);
