/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const perfMetadata = {
  owner: "GenAI Team",
  name: "ML Smart Tab Clustering",
  description: "Testing Smart Tab Clustering",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "latency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "memory",
          unit: "MiB",
          shouldAlert: false,
        },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(250);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

// Clustering / Nearest Neighbor tests
const ROOT_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/tab_grouping/";

/*
 * Generate n random samples by loading existing labels and embeddings
 */
function generateSamples(labels, embeddings, n) {
  let generatedLabels = [];
  let generatedEmbeddings = [];
  for (let i = 0; i < n; i++) {
    const randomIndex = Math.floor(Math.random() * labels.length);
    generatedLabels.push(labels[randomIndex]);
    if (embeddings) {
      generatedEmbeddings.push(embeddings[randomIndex]);
    }
  }
  return {
    labels: generatedLabels,
    embeddings: generatedEmbeddings,
  };
}

async function generateEmbeddings(textList) {
  const options = new PipelineOptions({
    taskName: "feature-extraction",
    modelId: "Mozilla/smart-tab-embedding",
    modelHubUrlTemplate: "{model}/{revision}",
    modelRevision: "main",
    dtype: "q8",
    timeoutMS: 2 * 60 * 1000,
  });
  const requestInfo = {
    inputArgs: textList,
    runOptions: {
      pooling: "mean",
      normalize: true,
    },
  };

  const request = {
    args: [requestInfo.inputArgs],
    options: requestInfo.runOptions,
  };
  const mlEngineParent = await EngineProcess.getMLEngineParent();
  const engine = await mlEngineParent.getEngine(options);
  const output = await engine.run(request);
  return output;
}

const singleTabMetrics = {};
singleTabMetrics["SINGLE-TAB-LATENCY"] = [];
singleTabMetrics["SINGLE-TAB-LOGISTIC-REGRESSION-LATENCY"] = [];

add_task(async function test_clustering_nearest_neighbors() {
  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  const { cleanup } = await perfSetup({
    prefs: [["browser.ml.modelHubRootUrl", modelHubRootUrl]],
  });

  const stgManager = new SmartTabGroupingManager();

  let generateEmbeddingsStub = sinon.stub(
    SmartTabGroupingManager.prototype,
    "_generateEmbeddings"
  );
  generateEmbeddingsStub.callsFake(async textList => {
    return await generateEmbeddings(textList);
  });

  const labelsPath = `gen_set_2_labels.tsv`;
  const rawLabels = await fetchFile(ROOT_URL, labelsPath);
  let labels = parseTsvStructured(rawLabels);
  labels = labels.map(l => ({ ...l, label: l.smart_group_label }));
  const startTime = performance.now();
  const similarTabs = await stgManager.findNearestNeighbors({
    allTabs: labels,
    groupedIndices: [1],
    alreadyGroupedIndices: [],
    groupLabel: "Travel Planning",
    threshold: 0.3,
  });
  const endTime = performance.now();
  singleTabMetrics["SINGLE-TAB-LATENCY"].push(endTime - startTime);
  const titles = similarTabs.map(s => s.label);
  Assert.equal(
    titles.length,
    5,
    "Proper number of similar tabs should be returned"
  );
  Assert.equal(
    titles[0],
    "Tourist Behavior and Decision Making: A Research Overview"
  );
  Assert.equal(
    titles[1],
    "Impact of Tourism on Local Communities - Google Scholar"
  );
  Assert.equal(titles[2], "Cheap Flights, Airline Tickets & Airfare Deals");
  Assert.equal(titles[3], "Hotel Deals: Save Big on Hotels with Expedia");
  Assert.equal(
    titles[4],
    "The Influence of Travel Restrictions on the Spread of COVID-19 - Nature"
  );
  generateEmbeddingsStub.restore();
  await EngineProcess.destroyMLEngine();
  await cleanup();
});

add_task(async function test_clustering_logistic_regression() {
  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  const { cleanup } = await perfSetup({
    prefs: [["browser.ml.modelHubRootUrl", modelHubRootUrl]],
  });

  const stgManager = new SmartTabGroupingManager();

  let generateEmbeddingsStub = sinon.stub(
    SmartTabGroupingManager.prototype,
    "_generateEmbeddings"
  );
  generateEmbeddingsStub.callsFake(async textList => {
    return await generateEmbeddings(textList);
  });

  const labelsPath = `gen_set_2_labels.tsv`;
  const rawLabels = await fetchFile(ROOT_URL, labelsPath);
  let labels = parseTsvStructured(rawLabels);
  labels = labels.map(l => ({ ...l, label: l.smart_group_label }));
  const startTime = performance.now();
  const similarTabs = await stgManager.findSimilarTabsLogisticRegression({
    allTabs: labels,
    groupedIndices: [1],
    alreadyGroupedIndices: [],
    groupLabel: "Travel Planning",
  });
  const endTime = performance.now();
  singleTabMetrics["SINGLE-TAB-LOGISTIC-REGRESSION-LATENCY"].push(
    endTime - startTime
  );
  const titles = similarTabs.map(s => s.label);
  Assert.equal(
    titles.length,
    5,
    "Proper number of similar tabs should be returned"
  );
  Assert.equal(
    titles[0],
    "Tourist Behavior and Decision Making: A Research Overview"
  );
  Assert.equal(
    titles[1],
    "Impact of Tourism on Local Communities - Google Scholar"
  );
  Assert.equal(titles[2], "Cheap Flights, Airline Tickets & Airfare Deals");
  Assert.equal(
    titles[3],
    "The Influence of Travel Restrictions on the Spread of COVID-19 - Nature"
  );
  Assert.equal(titles[4], "Hotel Deals: Save Big on Hotels with Expedia");
  reportMetrics(singleTabMetrics);
  generateEmbeddingsStub.restore();
  await EngineProcess.destroyMLEngine();
  await cleanup();
});

const N_TABS = [25];
const methods = [
  "KMEANS_ANCHOR",
  "NEAREST_NEIGHBORS_ANCHOR",
  "LOGISTIC_REGRESSION_ANCHOR",
];
const nTabMetrics = {};

for (let method of methods) {
  for (let n of N_TABS) {
    if (method === "KMEANS_ANCHOR" && n > 25) {
      break;
    }
    nTabMetrics[`${method}-${n}-TABS-latency`] = [];
  }
}

add_task(async function test_n_clustering() {
  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  const { cleanup } = await perfSetup({
    prefs: [["browser.ml.modelHubRootUrl", modelHubRootUrl]],
  });

  const stgManager = new SmartTabGroupingManager();

  let generateEmbeddingsStub = sinon.stub(
    SmartTabGroupingManager.prototype,
    "_generateEmbeddings"
  );
  generateEmbeddingsStub.callsFake(async textList => {
    return await generateEmbeddings(textList);
  });

  const labelsPath = `gen_set_2_labels.tsv`;
  const rawLabels = await fetchFile(ROOT_URL, labelsPath);
  const labels = parseTsvStructured(rawLabels);

  for (let n of N_TABS) {
    for (let method of methods) {
      for (let i = 0; i < 1; i++) {
        const samples = generateSamples(labels, null, n);
        let startTime = performance.now();
        if (method === "KMEANS_ANCHOR" && n <= 50) {
          await stgManager.generateClusters(
            samples.labels,
            null,
            0,
            null,
            [0],
            []
          );
        } else if (method === "NEAREST_NEIGHBORS_ANCHOR") {
          await stgManager.findNearestNeighbors({
            allTabs: samples.labels,
            groupedIndices: [0],
            alreadyGroupedIndices: [],
            groupLabel: "Random Group Name",
          });
        } else if (method === "LOGISTIC_REGRESSION_ANCHOR") {
          await stgManager.findSimilarTabsLogisticRegression({
            allTabs: samples.labels,
            groupedIndices: [0],
            alreadyGroupedIndices: [],
            groupLabel: "Random Group Name",
          });
        }
        let endTime = performance.now();
        const key = `${method}-${n}-TABS-latency`;
        if (key in nTabMetrics) {
          nTabMetrics[key].push(endTime - startTime);
        }
        await EngineProcess.destroyMLEngine();
      }
    }
  }
  reportMetrics(nTabMetrics);
  generateEmbeddingsStub.restore();
  await cleanup();
});
