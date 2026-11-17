/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AITab } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
);
const { shareBudget } = AITab;

const sum = numbers => numbers.reduce((total, number) => total + number, 0);

add_task(function test_shareBudget_no_sections() {
  Assert.deepEqual(shareBudget([], 100), [], "No sections get no shares");
});

add_task(function test_shareBudget_single_section() {
  Assert.deepEqual(shareBudget([40], 100), [40], "A short section is whole");
  Assert.deepEqual(shareBudget([100], 100), [100], "An exact fit is whole");
  Assert.deepEqual(
    shareBudget([500], 100),
    [100],
    "A long section gets the whole budget"
  );
  Assert.deepEqual(shareBudget([0], 100), [0], "Empty text gets no share");
});

add_task(function test_shareBudget_sections_that_fit() {
  Assert.deepEqual(
    shareBudget([10, 20, 30], 100),
    [10, 20, 30],
    "Sections that fit together are all kept whole"
  );
  Assert.deepEqual(
    shareBudget([50, 50], 100),
    [50, 50],
    "Sections that use the budget exactly are all kept whole"
  );
});

add_task(function test_shareBudget_long_sections_split_evenly() {
  Assert.deepEqual(
    shareBudget([500, 900, 700, 800], 100),
    [25, 25, 25, 25],
    "Sections longer than an even share each get an even share"
  );
});

add_task(function test_shareBudget_zero_length_sections() {
  Assert.deepEqual(
    shareBudget([0, 500], 100),
    [0, 100],
    "An empty section leaves the whole budget to the other section"
  );
  Assert.deepEqual(
    shareBudget([500, 0, 500, 0], 100),
    [50, 0, 50, 0],
    "Empty sections hold no budget"
  );
  Assert.deepEqual(
    shareBudget([0, 0, 0], 100),
    [0, 0, 0],
    "Empty sections get no shares"
  );
});

add_task(function test_shareBudget_short_section_leaves_budget_to_long() {
  Assert.deepEqual(
    shareBudget([10, 500], 100),
    [10, 90],
    "A long section gets what a short section leaves"
  );
  Assert.deepEqual(
    shareBudget([500, 10, 20, 500], 100),
    [35, 10, 20, 35],
    "The shares are in the order of the sections, not sorted by length"
  );
  Assert.deepEqual(
    shareBudget([10, 30, 500, 500], 100),
    [10, 30, 30, 30],
    "A section at the even share is whole, and the rest is split evenly"
  );
});

add_task(function test_shareBudget_remainder_is_not_exceeded() {
  const shares = shareBudget([500, 500, 500], 100);
  Assert.deepEqual(
    shares.toSorted((a, b) => a - b),
    [33, 33, 34],
    "A budget that does not divide evenly is rounded down per section"
  );
  Assert.equal(sum(shares), 100, "The shares use the whole budget");
});

add_task(function test_shareBudget_zero_budget() {
  Assert.deepEqual(
    shareBudget([0, 10, 500], 0),
    [0, 0, 0],
    "No budget gives no shares"
  );
});

add_task(function test_shareBudget_more_sections_than_budget() {
  const shares = shareBudget([5, 5, 5, 5, 5], 3);
  Assert.lessOrEqual(sum(shares), 3, "The shares stay in the budget");
  Assert.ok(
    shares.every(share => share >= 0 && share <= 5),
    "No share is negative or longer than its section"
  );
});

add_task(function test_shareBudget_invariants() {
  const cases = [
    [[1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 25],
    [[30000, 0, 120, 4500, 9999, 10000, 10001, 1, 2500, 2501], 25000],
    [[7, 7, 7], 20],
    [[100, 1], 2],
  ];
  for (const [lengths, budget] of cases) {
    const shares = shareBudget(lengths, budget);
    Assert.equal(shares.length, lengths.length, "One share per section");
    Assert.ok(
      shares.every((share, index) => share >= 0 && share <= lengths[index]),
      `No share is longer than its section for ${JSON.stringify(lengths)}`
    );
    Assert.equal(
      sum(shares),
      Math.min(budget, sum(lengths)),
      `The shares use all the budget that the sections can fill for ${JSON.stringify(lengths)}`
    );
    const cut = shares.filter((share, index) => share < lengths[index]);
    if (!cut.length) {
      continue;
    }
    Assert.lessOrEqual(
      Math.max(...cut) - Math.min(...cut),
      1,
      `Cut sections get even shares for ${JSON.stringify(lengths)}`
    );
    Assert.ok(
      shares.every(share => share <= Math.min(...cut) + 1),
      `No whole section is longer than a cut section's share for ${JSON.stringify(lengths)}`
    );
  }
});
