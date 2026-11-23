// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainyearmonth.prototype.add
description: RangeError thrown when going out of range
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const max = Temporal.PlainYearMonth.from("+275760-09");
const monthBeforeMax = Temporal.PlainYearMonth.from("+275760-08");
for (const overflow of ["reject", "constrain"]) {
  assert.throws(RangeError, () => max.add({ months: 1 }, { overflow }), overflow);
  TemporalHelpers.assertPlainYearMonth(monthBeforeMax.add({ months: 1 }, { overflow }),
    275760, 9, "M09", `reaching max with ${overflow}`);
}

reportCompare(0, 0);
