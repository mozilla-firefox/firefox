/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { SmartTabGroupingManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
);

add_task(function test_calculate_probability_zero_inputs() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 1,
    TITLE_SIMILARITY_WEIGHT: 1,
    INTERCEPT: 0,
  };
  const groupSim = 0;
  const titleSim = 0;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(0)); // sigmoid(0) = 0.5
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "When both similarities are zero, the probability should be sigmoid(0) = 0.5."
  );
});

add_task(function test_calculate_probability_both_positive() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 1,
    TITLE_SIMILARITY_WEIGHT: 1,
    INTERCEPT: 0,
  };
  const groupSim = 1;
  const titleSim = 1;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(-2));
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "For positive similarities, the result should match sigmoid(2)."
  );
});

add_task(function test_calculate_probability_mixed_values() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 2,
    TITLE_SIMILARITY_WEIGHT: 3,
    INTERCEPT: 0.5,
  };
  const groupSim = 1;
  const titleSim = -1;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(0.5)); // sigmoid(-0.5)
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "Mixed values should yield sigmoid(-0.5)."
  );
});

add_task(function test_calculate_probability_zero_weights() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 0,
    TITLE_SIMILARITY_WEIGHT: 0,
    INTERCEPT: 0,
  };
  const groupSim = 5;
  const titleSim = -3;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(0)); // sigmoid(0) = 0.5
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "With zero weights, the probability should always be 0.5 (sigmoid(0))."
  );
});

add_task(function test_calculate_probability_extreme_positive() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 1,
    TITLE_SIMILARITY_WEIGHT: 1,
    INTERCEPT: 0,
  };
  const groupSim = 10;
  const titleSim = 10;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(-20));
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "Extreme positive similarities should yield a probability very close to 1."
  );
});

add_task(function test_calculate_probability_extreme_negative() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 1,
    TITLE_SIMILARITY_WEIGHT: 1,
    INTERCEPT: 0,
  };
  const groupSim = -10;
  const titleSim = -10;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(20));
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "Extreme negative similarities should yield a probability very close to 0."
  );
});

add_task(function test_calculate_probability_negative_intercept() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const params = {
    GROUP_SIMILARITY_WEIGHT: 1,
    TITLE_SIMILARITY_WEIGHT: 1,
    INTERCEPT: -1,
  };
  const groupSim = 0.5;
  const titleSim = 0.5;
  const result = smartTabGroupingManager.calculateProbability(
    groupSim,
    titleSim,
    params
  );
  const expected = 1 / (1 + Math.exp(0)); // sigmoid(0) = 0.5
  Assert.equal(
    result.toPrecision(4),
    expected.toPrecision(4),
    "A negative intercept can adjust the sum so that the output is still sigmoid(0) = 0.5."
  );
});
