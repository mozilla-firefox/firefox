// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.round
description: Checking limits of representable PlainDateTime
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const min = new Temporal.PlainDateTime(-271821, 4, 19, 0, 0, 0, 0, 0, 1);
const max = new Temporal.PlainDateTime(275760, 9, 13, 23, 59, 59, 999, 999, 999);

// Rounding in the other direction stays inside the limits
const minCeil = {
  day: [-271821, 4, "M04", 20, 0, 0, 0, 0, 0, 0],
  hour: [-271821, 4, "M04", 19, 1, 0, 0, 0, 0, 0],
  minute: [-271821, 4, "M04", 19, 0, 1, 0, 0, 0, 0],
  second: [-271821, 4, "M04", 19, 0, 0, 1, 0, 0, 0],
  millisecond: [-271821, 4, "M04", 19, 0, 0, 0, 1, 0, 0],
  microsecond: [-271821, 4, "M04", 19, 0, 0, 0, 0, 1, 0],
};
const maxFloor = {
  day: [275760, 9, "M09", 13, 0, 0, 0, 0, 0, 0],
  hour: [275760, 9, "M09", 13, 23, 0, 0, 0, 0, 0],
  minute: [275760, 9, "M09", 13, 23, 59, 0, 0, 0, 0],
  second: [275760, 9, "M09", 13, 23, 59, 59, 0, 0, 0],
  millisecond: [275760, 9, "M09", 13, 23, 59, 59, 999, 0, 0],
  microsecond: [275760, 9, "M09", 13, 23, 59, 59, 999, 999, 0],
};

["day", "hour", "minute", "second", "millisecond", "microsecond"].forEach((smallestUnit) => {
  assert.throws(
    RangeError,
    () => min.round({ smallestUnit, roundingMode: "floor" }),
    `rounding beyond limit (unit = ${smallestUnit}, rounding mode = floor)`
  );
  assert.throws(
    RangeError,
    () => max.round({ smallestUnit, roundingMode: "ceil" }),
    `rounding beyond limit (unit = ${smallestUnit}, rounding mode = ceil)`
  );
  TemporalHelpers.assertPlainDateTime(
    min.round({ smallestUnit, roundingMode: "ceil" }),
    ...minCeil[smallestUnit],
    `rounding minimum away from limit (unit = ${smallestUnit}, rounding mode = ceil)`
  );
  TemporalHelpers.assertPlainDateTime(
    max.round({ smallestUnit, roundingMode: "floor" }),
    ...maxFloor[smallestUnit],
    `rounding maximum away from limit (unit = ${smallestUnit}, rounding mode = floor)`
  );
});

TemporalHelpers.assertPlainDateTime(
  min.round({ smallestUnit: "nanosecond", roundingMode: "floor" }),
  -271821, 4, "M04", 19, 0, 0, 0, 0, 0, 1,
  "rounding minimum to nanosecond stays at the limit"
);
TemporalHelpers.assertPlainDateTime(
  max.round({ smallestUnit: "nanosecond", roundingMode: "ceil" }),
  275760, 9, "M09", 13, 23, 59, 59, 999, 999, 999,
  "rounding maximum to nanosecond stays at the limit"
);

reportCompare(0, 0);
