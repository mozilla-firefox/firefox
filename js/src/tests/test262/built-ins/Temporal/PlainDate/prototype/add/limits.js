// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Temporal.PlainDate.prototype.add throws a RangeError if the calculation crosses a limit
esid: sec-temporal.plaindate.prototype.add
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const min = Temporal.PlainDate.from("-271821-04-19");
const max = Temporal.PlainDate.from("+275760-09-13");
const dayAfterMin = Temporal.PlainDate.from("-271821-04-20");
const dayBeforeMax = Temporal.PlainDate.from("+275760-09-12");
["reject", "constrain"].forEach((overflow) => {
  assert.throws(RangeError, () => min.add({ days: -1 }, { overflow }), `min with ${overflow}`);
  assert.throws(RangeError, () => max.add({ days: 1 }, { overflow }), `max with ${overflow}`);
  TemporalHelpers.assertPlainDate(dayAfterMin.add({ days: -1 }, { overflow }),
    -271821, 4, "M04", 19, `reaching min with ${overflow}`);
  TemporalHelpers.assertPlainDate(dayBeforeMax.add({ days: 1 }, { overflow }),
    275760, 9, "M09", 13, `reaching max with ${overflow}`);
});

reportCompare(0, 0);
