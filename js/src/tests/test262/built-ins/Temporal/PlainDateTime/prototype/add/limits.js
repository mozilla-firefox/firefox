// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.add
description: Checking limits of representable PlainDateTime
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const min = new Temporal.PlainDateTime(-271821, 4, 19, 0, 0, 0, 0, 0, 1);
const max = new Temporal.PlainDateTime(275760, 9, 13, 23, 59, 59, 999, 999, 999);
const nsAfterMin = new Temporal.PlainDateTime(-271821, 4, 19, 0, 0, 0, 0, 0, 2);
const nsBeforeMax = new Temporal.PlainDateTime(275760, 9, 13, 23, 59, 59, 999, 999, 998);

["reject", "constrain"].forEach((overflow) => {
  assert.throws(
    RangeError,
    () => max.add({ nanoseconds: 1 }, { overflow }),
    `adding 1 nanosecond beyond maximum limit (overflow = ${overflow})`
  );
  assert.throws(
    RangeError,
    () => min.add({ nanoseconds: -1 }, { overflow }),
    `adding -1 nanosecond beyond minimum limit (overflow = ${overflow})`
  );
  TemporalHelpers.assertPlainDateTime(
    nsBeforeMax.add({ nanoseconds: 1 }, { overflow }),
    275760, 9, "M09", 13, 23, 59, 59, 999, 999, 999,
    `adding 1 nanosecond to reach maximum limit (overflow = ${overflow})`
  );
  TemporalHelpers.assertPlainDateTime(
    nsAfterMin.add({ nanoseconds: -1 }, { overflow }),
    -271821, 4, "M04", 19, 0, 0, 0, 0, 0, 1,
    `adding -1 nanosecond to reach minimum limit (overflow = ${overflow})`
  );

});

reportCompare(0, 0);
