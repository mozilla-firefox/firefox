/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { SmartTabGroupingManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs"
);

add_task(function test_average_similarity_single_candidate_single_anchor() {
  const anchors = [[1, 0]];
  const candidates = [[1, 0]];
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const result = smartTabGroupingManager.getAverageSimilarity(
    anchors,
    candidates
  );
  Assert.equal(result.length, 1, "Should return one similarity score.");
  Assert.equal(
    result[0],
    1,
    "Cosine similarity should be 1 for identical vectors."
  );
});

add_task(function test_average_similarity_single_candidate_multiple_anchor() {
  const anchors = [
    [1, 0],
    [0, 1],
  ];
  const candidates = [[1, 0]];
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const result = smartTabGroupingManager.getAverageSimilarity(
    anchors,
    candidates
  );
  const expectedAverage = (1 + 0) / 2;
  Assert.equal(result.length, 1, "Should return one similarity score.");
  Assert.equal(
    result[0],
    expectedAverage,
    "Average similarity should be about 0.5."
  );
});

add_task(function test_average_similarity_multiple_candidate_single_anchor() {
  const anchors = [[1, 0]];
  const candidates = [
    [1, 0],
    [-1, 0],
  ];
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const result = smartTabGroupingManager.getAverageSimilarity(
    anchors,
    candidates
  );
  Assert.equal(result.length, 2, "Should return two similarity scores.");
  Assert.equal(result[0], 1, "First candidate similarity should be 1.");
  Assert.equal(result[1], -1, "Second candidate similarity should be -1.");
});

add_task(
  function test_average_similarity_multiple_candidates_multiple_anchors() {
    const anchors = [
      [1, 0],
      [0, 1],
    ];
    const candidates = [
      [1, 1],
      [1, -1],
    ];
    const smartTabGroupingManager = new SmartTabGroupingManager();
    const result = smartTabGroupingManager.getAverageSimilarity(
      anchors,
      candidates
    );
    // Expected values based on standard cosine similarity for normalized vectors.
    Assert.equal(result.length, 2, "Should return two similarity scores.");
    Assert.equal(
      result[0].toPrecision(4),
      0.7071,
      "First candidate average similarity should be approx 0.7071."
    );
    Assert.equal(
      result[1],
      0,
      "Second candidate average similarity should be 0."
    );
  }
);

add_task(function test_average_similarity_empty_candidates() {
  const anchors = [
    [1, 0],
    [0, 1],
  ];
  const candidates = [];
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const result = smartTabGroupingManager.getAverageSimilarity(
    anchors,
    candidates
  );
  Assert.deepEqual(
    result,
    [],
    "Empty candidate embeddings should return an empty array."
  );
});

add_task(function test_average_similarity_multiple_candidate_empty_anchors() {
  const anchors = [];
  const candidates = [[1, 0]];
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const result = smartTabGroupingManager.getAverageSimilarity(
    anchors,
    candidates
  );
  Assert.equal(
    result.length,
    1,
    "One candidate should produce one similarity score."
  );
  Assert.ok(
    isNaN(result[0]),
    "Empty anchor embeddings should result in NaN (due to 0/0 division)."
  );
});
