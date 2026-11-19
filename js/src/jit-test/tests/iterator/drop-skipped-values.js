// Iterator.prototype.drop must not read the value of skipped results.

for (const limit of [0, 1, 3, 4, Infinity]) {
  let nextCalls = 0;
  let valueReads = 0;
  const iterator = {
    __proto__: Iterator.prototype,
    next() {
      const value = nextCalls++;
      return {
        done: value >= 3,
        get value() {
          assertEq(value >= limit, true);
          assertEq(value < 3, true);
          valueReads++;
          return value;
        },
      };
    },
  };

  const helper = iterator.drop(limit);
  assertEq(nextCalls, 0);
  for (let value = limit; value < 3; value++) {
    const result = helper.next();
    assertEq(result.done, false);
    assertEq(result.value, value);
  }
  assertEq(helper.next().done, true);
  assertEq(helper.next().done, true);
  assertEq(nextCalls, 4);
  assertEq(valueReads, Math.max(3 - limit, 0));
}
