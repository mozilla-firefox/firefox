/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
const HOST_PREFIX =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/smarttabgrouping/data/";

const CLUSTERING_TEST_IDS = ["pgh_trip", "gen_set_2", "animal"];

async function getGroupScore(
  clusterMethod,
  umapMethod,
  tabs,
  embeddings,
  iterations = 10
) {
  const groupManager = new SmartTabGroupingManager();
  const randFunc = simpleNumberSequence();
  groupManager.setDataTitleKey("title");
  groupManager.setClusteringMethod(clusterMethod);
  groupManager.setDimensionReductionMethod(umapMethod);
  let total_score = 0.0;
  for (let i = 0; i < iterations; i++) {
    const groupingResult = await groupManager.generateClusters(
      tabs,
      embeddings,
      0,
      randFunc
    );
    const score = groupingResult.getRandScore("smart_group_label");
    total_score += score;
  }
  return total_score / iterations;
}

async function runClusteringTest(data, precomputedEmbeddings = null) {
  if (!precomputedEmbeddings) {
    shuffleArray(data, simpleNumberSequence(0));
  }
  const testParams = [[CLUSTER_METHODS.KMEANS, null]];
  let score = 0;
  for (let testP of testParams) {
    score = await getGroupScore(
      testP[0],
      testP[1],
      data,
      precomputedEmbeddings
    );
    console.log(
      `${testP[0]} ${testP[1] || "No dim reduction."} Rand score is ${score}`
    );
  }
  if (testParams.length === 1) {
    return score;
  }
  console.warn(
    "Test checks on score not enabled because we are testing multiple methods"
  );
  return 1;
}

async function setup({ disabled = false, prefs = [] } = {}) {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.ml.enable", !disabled],
      ["browser.ml.logLevel", "All"],
      ["browser.ml.modelCacheTimeout", 1000],
      ...prefs,
    ],
  });
}

add_task(function testGetBestAnchorClusterInfo() {
  const { anchorClusterIndex, numAnchorItemsInCluster } =
    getBestAnchorClusterInfo(
      [
        [0, 1, 2],
        [3, 4],
        [5, 6, 7],
      ],
      [1, 2, 3]
    );
  Assert.equal(anchorClusterIndex, 0);
  Assert.equal(numAnchorItemsInCluster, 2);
});

add_task(function testAverageStats() {
  const res = averageStatsValues([{ a: 1 }, { a: 2 }, { a: 3 }]);
  Assert.equal(res.a, 2);
});

add_task(async function testClustering() {
  const testSets = CLUSTERING_TEST_IDS.map(test_id => [
    `${test_id}_embeddings.tsv`,
    `${test_id}_labels.tsv`,
  ]);
  for (const test of testSets) {
    const rawEmbeddings = await fetchFile(HOST_PREFIX, test[0]);
    const embeddings = parseTsvEmbeddings(rawEmbeddings);
    const rawLabels = await fetchFile(HOST_PREFIX, test[1]);
    const labels = parseTsvStructured(rawLabels);
    const score = await runClusteringTest(labels, embeddings);
    Assert.greater(score, 0.5, `Clustering ok for dataset ${test[0]}`);
  }
});

/**
 * Run tests for finding similar items for a single item or cluster of items with label anchorLabel
 * @param {Number[][]} embeddings Embeddings for each item
 * @param {String} anchorLabel String representing the ID of the anchor cluster
 * @param {Object[]} labels Dict representing each document (unused )
 * @param {String[]} labelClusterList List of items, with id of cluster for each (unused in)
 * @param {String} testName Name of the test dataset
 * @returns {Object[]}
 */
async function anchorTestsForCluster(
  embeddings,
  anchorLabel,
  documents,
  labelClusterList,
  testName,
  maxDocsToTest = 4
) {
  const scores = [];
  const possibleAnchorIndices = [];
  labelClusterList.forEach((cur, index) => {
    if (cur === anchorLabel) {
      possibleAnchorIndices.push(index);
    }
  });
  for (let numAnchors = 1; numAnchors < maxDocsToTest; numAnchors++) {
    shuffleArray(labelClusterList);
    const silBoost = 0.0;
    for (const anchorMethod of [ANCHOR_METHODS.FIXED, ANCHOR_METHODS.DRIFT]) {
      const anchorTestScoreInfo = await runAnchorTabTest(
        documents,
        embeddings,
        Array.from({ length: numAnchors }, (_, i) => i),
        anchorMethod,
        silBoost
      );
      scores.push({
        ...anchorTestScoreInfo,
        testName,
        anchorMethod,
        numAnchors,
        silBoost,
      });
    }
  }
  return scores;
}

add_task(async function testAnchorClustering() {
  const testSets = CLUSTERING_TEST_IDS.map(test_id => [
    `${test_id}_embeddings.tsv`,
    `${test_id}_labels.tsv`,
  ]);
  const LABEL_DICT_KEY = "smart_group_label";
  const scoreInfo = [];

  for (const test of testSets) {
    const rawEmbeddings = await fetchFile(HOST_PREFIX, test[0]);
    const embeddings = parseTsvEmbeddings(rawEmbeddings);
    const rawLabels = await fetchFile(HOST_PREFIX, test[1]);
    const labels = parseTsvStructured(rawLabels);
    const labelClusterList = labels.map(a => a[LABEL_DICT_KEY]);
    const uniqueLabels = Array.from(new Set(labelClusterList));

    /** Anchor Tests */
    const NUM_ANCHOR_LABELS_TO_TEST = 3;

    for (
      let labelIndex = 0;
      labelIndex < Math.max(uniqueLabels.length, NUM_ANCHOR_LABELS_TO_TEST);
      labelIndex++
    ) {
      const anchorLabel = uniqueLabels[labelIndex];
      const scoresForRuns = await anchorTestsForCluster(
        embeddings,
        anchorLabel,
        labels,
        labelClusterList,
        test[0]
      );
      scoreInfo.push(...scoresForRuns);
    }
  }

  const ACCEPTABLE_KAPPA = 0.15; // Quite a low bar, but have to start somewhere
  const percentageOfAcceptableKappa =
    scoreInfo.filter(a => a.kappa > ACCEPTABLE_KAPPA).length / scoreInfo.length;
  console.log(
    `${Math.round(percentageOfAcceptableKappa * 100)} % of tests greater than ${ACCEPTABLE_KAPPA} kappa`
  );
  Assert.greater(
    percentageOfAcceptableKappa,
    0.5,
    `Acceptable accuracy rates of anchor cluster operations`
  );
  // Print / log score info here when testing
  SimpleTest.waitForExplicitFinish();
});

add_task(function processTopicModelResult() {
  const smartTabGroupingManager = new SmartTabGroupingManager();
  const input_phrases = [
    "",
    "None", // Target of model if uncertain
    " Adult Content", // This is a common target of the model on inapproprate content
    " Zoom Room zoom",
    " Cats are great",
    " Cats",
    "Dogs!!!",
  ];
  const output_phrases = [
    "",
    "",
    "",
    "Zoom Room",
    "Cats are great",
    "Cats",
    "Dogs!!!",
  ];
  for (let i = 0; i < input_phrases.length; i++) {
    Assert.equal(
      smartTabGroupingManager.processTopicModelResult(input_phrases[i]),
      output_phrases[i]
    );
  }
});

add_task(function testDuplicateWords() {
  const input_phrases = [
    "",
    " ",
    "Ask me about my cat",
    "Ask me about my cat cats cat",
    "Zoom Room zoom",
    "Zoom zooms",
    "Event events",
    "Events event",
  ];
  const output_phrases = [
    "",
    "",
    "Ask me about my cat",
    "Ask me about my cat",
    "Zoom Room",
    "Zoom",
    "Event",
    "Events",
  ];
  for (let i = 0; i < input_phrases.length; i++) {
    Assert.equal(
      SmartTabGroupingManager.cutAtDuplicateWords(input_phrases[i]),
      output_phrases[i]
    );
  }
});
