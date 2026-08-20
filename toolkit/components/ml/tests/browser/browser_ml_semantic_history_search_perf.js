/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const UPDATE_TASK_LATENCY = "update-task-latency";
const SEARCH_LATENCY = "search-latency";
const INFERENCE_LATENCY = "inference-latency";
const VECTOR_DB_DISK_SIZE = "vectordb-disk-memory-usage";
const perfMetadata = {
  owner: "GenAI Team",
  name: "ML Semantic History Search",
  description: "Test for latency for ML Semantic Search History Feature",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "latency",
          unit: "ms",
          shouldAlert: true,
        },
        {
          name: "memory",
          unit: "MB",
          shouldAlert: true,
        },
      ],
      verbose: true,
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      try_platform: ["mac", "win"],
    },
  },
};

requestLongerTimeout(45);

// This perf test focus is search performance, so we pin the embedding model and dimension
// to ensure consistency.

const CUSTOM_EMBEDDER_MODEL = "Xenova/all-MiniLM-L6-v2";
const CUSTOM_EMBEDDER_DIM = 384;

const CUSTOM_EMBEDDER_OPTIONS = {
  taskName: "feature-extraction",
  featureId: "simple-text-embedder",
  modelId: CUSTOM_EMBEDDER_MODEL,
  embeddingDimension: CUSTOM_EMBEDDER_DIM,
  dtype: "q8",
  modelRevision: "main",
  numThreads: 2,
  timeoutMS: -1,
};

const ROOT_URL =
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data/search_history";
const PLACES_SAMPLE_DATA = `${ROOT_URL}/profile_places.json`;

async function get_synthetic_data(inputDataPath) {
  const response = await fetch(inputDataPath);
  if (!response.ok) {
    throw new Error(
      `Failed to fetch data: ${response.statusText} from ${inputDataPath}`
    );
  }
  return response.json();
}

async function loadProfileData(profileData, conn) {
  const BATCH_SIZE = 100;

  await conn.executeTransaction(async () => {
    for (let i = 0; i < profileData.length; i += BATCH_SIZE) {
      const batch = profileData.slice(i, i + BATCH_SIZE);
      const values = [];
      const placeholders = [];

      for (const row of batch) {
        const {
          url,
          title,
          rev_host,
          visit_count,
          hidden,
          typed,
          frecency,
          last_visit_date,
          guid,
          foreign_count,
          url_hash,
          description,
          preview_image_url,
          site_name,
          origin_id,
          recalc_frecency,
          alt_frecency,
          recalc_alt_frecency,
        } = row;

        placeholders.push(
          "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );
        values.push(
          url,
          title,
          rev_host,
          visit_count,
          hidden,
          typed,
          frecency,
          last_visit_date,
          guid,
          foreign_count,
          url_hash,
          description,
          preview_image_url,
          site_name,
          origin_id,
          recalc_frecency,
          alt_frecency,
          recalc_alt_frecency
        );
      }

      const query = `
        INSERT INTO places.moz_places (
          url, title, rev_host, visit_count, hidden, typed, frecency, 
          last_visit_date, guid, foreign_count, url_hash, description,
          preview_image_url, site_name, origin_id, recalc_frecency,
          alt_frecency, recalc_alt_frecency
        ) VALUES ${placeholders.join(", ")}
      `;

      await conn.execute(query, values);

      info(`Inserted ${Math.min(i + BATCH_SIZE, profileData.length)} rows...`);
    }
  });
}

// The update task does not notify on its throttle and error exits
// (Bug 2063126); an unbounded wait would hang until CI kills the job.
async function waitForPendingUpdates() {
  const topic = "places-semantichistorymanager-update-complete";
  let seen = false;
  function observer() {
    Services.obs.removeObserver(observer, topic);
    seen = true;
  }
  Services.obs.addObserver(observer, topic);
  try {
    await TestUtils.waitForCondition(
      () => seen,
      `"${topic}" was not fired: semantic indexing stalled, throttled, or ` +
        'erroring on every chunk (check the log for "Error executing vector ' +
        'DB update task")',
      1000,
      600
    );
  } catch (error) {
    Services.obs.removeObserver(observer, topic);
    throw error;
  }
}

async function cleanupDatabase(conn) {
  try {
    let placesRows = await conn.execute(`
        SELECT COUNT(*) as counts from places.moz_places
      `);
    if (placesRows.length) {
      const placesRowsCount = placesRows[0].getResultByName("counts");
      info(`placesRowsCount = ${placesRowsCount}`);
    } else {
      info("No rows found in places.moz_places.");
    }

    let vecRows = await conn.execute(`
        SELECT COUNT(*) as counts from vec_history_mapping
      `);
    if (vecRows.length) {
      const vecRowsCount = vecRows[0].getResultByName("counts");
      info(`vecRowsCount = ${vecRowsCount}`);
    } else {
      info("No rows found in vec_history_mapping.");
    }

    // Delete all records from moz_places
    await conn.execute(`DELETE FROM places.moz_places;`);
    info("Cleared places.moz_places");

    // wait for all the updates to happen
    await waitForPendingUpdates();

    vecRows = await conn.execute(`
      SELECT COUNT(*) as counts from vec_history_mapping
    `);
    if (vecRows.length) {
      const vecRowsCount = vecRows[0].getResultByName("counts");
      info(`vecRowsCount (after deletion) = ${vecRowsCount}`);
    } else {
      info("No rows found in vec_history_mapping (after deletion).");
    }

    info("Database cleanup completed successfully.");
  } catch (error) {
    console.error("Error during database cleanup:", error);
  }
}

async function getCpuTimeFromProcInfo() {
  const NS_PER_MS = 1000000;
  let cpuTimeForProcess = p => p.cpuTime / NS_PER_MS;
  let procInfo = await ChromeUtils.requestProcInfo();
  return (
    cpuTimeForProcess(procInfo) +
    procInfo.children.map(cpuTimeForProcess).reduce((a, b) => a + b, 0)
  );
}

async function prepareSemanticSearchTest({
  rowLimit,
  backend,
  testFlag = true,
  concurrentInferenceFlag = false,
}) {
  const lazy = {};

  ChromeUtils.defineESModuleGetters(lazy, {
    getPlacesSemanticHistoryManager:
      "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs",
  });

  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  if (!modelHubRootUrl) {
    throw new Error(
      "MOZ_MODELS_HUB is not set, you need to run with --hooks toolkit/components/ml/tests/tools/hooks_local_hub.py"
    );
  }

  const { cleanup } = await perfSetup({
    prefs: [
      ["browser.ml.enable", true],
      ["places.semanticHistory.featureGate", true],
      ["browser.ml.modelHubRootUrl", modelHubRootUrl],
      ["javascript.options.wasm_lazy_tiering", true],
      ["browser.ml.logLevel", "Info"],
      ["places.semanticHistory.embeddingType", "contextual"],
      ["browser.ml.embedGen.textEmbeddingSize", CUSTOM_EMBEDDER_DIM],
      ["browser.ml.embedGen.textEmbeddingFeatureModel", CUSTOM_EMBEDDER_MODEL],
      // Indexing must complete for the measurement; wasm-speed chunks would
      // otherwise trip the failsafe, which stops indexing without notifying
      // (Bug 2063126).
      ["places.semanticHistory.maxChunkTimeMs", 600000],
      ["places.semanticHistory.maxChunkTimeMsFailsafe", 600000],
    ],
  });

  let semanticManager = lazy.getPlacesSemanticHistoryManager(
    {
      rowLimit,
      samplingAttrib: "frecency",
      changeThresholdCount: 0,
      distanceThreshold: 1.0,
      testFlag,
    },
    true
  );

  if (!semanticManager.qualifiedForSemanticSearch) {
    info("Skipping test due to insufficient hardware.");
    Assert.ok(true);
    return { skip: true };
  }

  // Skip featureGate, Region and other non critical checks.
  let canUseSemanticStub = sinon.stub(semanticManager, "canUseSemanticSearch");
  canUseSemanticStub.get(() => true);
  registerCleanupFunction(() => {
    canUseSemanticStub.restore();
  });

  // Ensures dtype/revision/backend is consistent for the test
  semanticManager.embedder.options = { ...CUSTOM_EMBEDDER_OPTIONS, backend };
  await semanticManager.embedder.ensureEngine();

  const conn = await semanticManager.getConnection();
  const data = await get_synthetic_data(PLACES_SAMPLE_DATA);
  const profileData = data.slice(1, rowLimit);
  await loadProfileData(profileData, conn);

  if (!concurrentInferenceFlag) {
    await waitForPendingUpdates();
  }

  const vecDBFileSize = !concurrentInferenceFlag
    ? await semanticManager.checkpointAndMeasureDbSize()
    : 0;

  return {
    semanticManager,
    conn,
    vecDBFileSize,
    cleanup,
    skip: false,
  };
}

async function runInferenceAndCollectMetrics({
  semanticManager,
  numIterations,
  searchQuery,
  searchQueries,
  journal,
  tag,
  concurrentInferenceFlag = false,
}) {
  const prefix = "SEMANTIC";
  // Use a single searchQuery or alternate through a list of queries in searchQueries
  // Ideally numIterations is a multiple of the length of searchQueries
  let numQueries = searchQueries && searchQueries.length;
  Assert.ok(
    numQueries || searchQuery,
    "Single query or non-empty multiple queries must be specified"
  );
  let queryContext = { searchString: searchQuery };
  const startCpu = Math.floor(await getCpuTimeFromProcInfo());
  let curQueryIndex = 0;
  for (let i = 0; i < numIterations; i++) {
    if (searchQueries) {
      queryContext = {
        searchString: searchQueries[curQueryIndex++ % numQueries],
      };
    }
    const startTime = performance.now();
    const res = await semanticManager.infer(queryContext);
    const endTime = performance.now();

    const duration = endTime - startTime;
    info(`inference time = ${duration}`);
    info(`results: ${JSON.stringify(res.results)}`);
    if (!concurrentInferenceFlag) {
      Assert.ok(!!res.results.length);
    } else {
      Assert.ok(true);
    }

    const memUsage = await getTotalMemoryUsage();
    const metrics = fetchMetrics(
      (res.metrics && res.metrics.runTimestamps) || []
    );
    let embeddingLatency = 0;

    for (const [metricName, value] of Object.entries(metrics)) {
      const safeValue = value > 0 ? value : 0;
      journal[`${prefix}-${metricName}-${tag}`] =
        journal[`${prefix}-${metricName}-${tag}`] || [];
      journal[`${prefix}-${metricName}-${tag}`].push(safeValue);

      if (metricName === MODEL_RUN_LATENCY) {
        embeddingLatency = safeValue;
      }
    }

    journal[`${prefix}-${SEARCH_LATENCY}-${tag}`] =
      journal[`${prefix}-${SEARCH_LATENCY}-${tag}`] || [];
    journal[`${prefix}-${SEARCH_LATENCY}-${tag}`].push(
      duration - embeddingLatency
    );

    journal[`${prefix}-${INFERENCE_LATENCY}-${tag}`] =
      journal[`${prefix}-${INFERENCE_LATENCY}-${tag}`] || [];
    journal[`${prefix}-${INFERENCE_LATENCY}-${tag}`].push(duration);

    journal[`${prefix}-${TOTAL_MEMORY_USAGE}-${tag}`] =
      journal[`${prefix}-${TOTAL_MEMORY_USAGE}-${tag}`] || [];
    journal[`${prefix}-${TOTAL_MEMORY_USAGE}-${tag}`].push(memUsage);
  }

  const endCpu = Math.floor(await getCpuTimeFromProcInfo());
  info(`Inference CPU time = ${endCpu - startCpu} ms`);
}

async function cleanupSemanticSearchTest({ semanticManager, conn, cleanup }) {
  const updateLatencies = semanticManager.getUpdateTaskLatency();
  const updateTime = updateLatencies.length
    ? updateLatencies.reduce((a, b) => a + b, 0)
    : 0;

  await cleanupDatabase(conn);
  await EngineProcess.destroyMLEngine();
  semanticManager.stopProcess();
  await semanticManager.embedder.shutdown();
  await semanticManager.semanticDB.removeDatabaseFiles();
  await cleanup();

  return updateTime;
}

async function runShortAndLongQueryPerfTest(
  concurrentInferenceFlag,
  { backend, tag }
) {
  const rowLimit = 500;
  const numIterations = 20;
  const mode = concurrentInferenceFlag ? "CONCURRENT" : "SEQUENTIAL";
  info(`Running ${mode} inference performance test...`);

  const setupResult = await prepareSemanticSearchTest({
    rowLimit,
    backend,
    concurrentInferenceFlag,
  });

  if (setupResult.skip) {
    return;
  }

  const { semanticManager, conn, vecDBFileSize, cleanup } = setupResult;

  try {
    await measureSemanticSearch({
      semanticManager,
      conn,
      cleanup,
      vecDBFileSize,
      numIterations,
      concurrentInferenceFlag,
      tag,
    });
  } catch (error) {
    // The happy path tears down inside measureSemanticSearch; on failure the
    // engine, database and prefs would otherwise leak into the next backend.
    await cleanupSemanticSearchTest({ semanticManager, conn, cleanup });
    throw error;
  }
}

async function measureSemanticSearch({
  semanticManager,
  conn,
  cleanup,
  vecDBFileSize,
  numIterations,
  concurrentInferenceFlag,
  tag,
}) {
  const journalShort = {};
  await runInferenceAndCollectMetrics({
    semanticManager,
    numIterations,
    searchQuery: "health tips",
    journal: journalShort,
    tag,
    concurrentInferenceFlag,
  });

  const journalShortPrefixed = Object.fromEntries(
    Object.entries(journalShort).map(([k, v]) => [`SHORT-${k}`, v])
  );
  journalShortPrefixed[`SEMANTIC-VECTOR_DB_DISK_SIZE-${tag}`] = [vecDBFileSize];
  info(`Short query journal = ${JSON.stringify(journalShortPrefixed)}`);
  reportMetrics(journalShortPrefixed);

  const journalLongMultiple = {};
  await runInferenceAndCollectMetrics({
    semanticManager,
    numIterations,
    searchQueries: [
      "best recipe with nutritional value and taste that kids like",
      "symptoms and causes of meningitis",
      "test plan for stress testing",
      "Care and feeding of the ball python snake",
      "schools in richmond virginia",
      "2024 Form 1040 filing instructions for IRS",
      "Oscar winners best picture",
    ],
    journal: journalLongMultiple,
    tag,
    concurrentInferenceFlag,
  });
  const journalLongMultiplePrefixed = Object.fromEntries(
    Object.entries(journalLongMultiple).map(([k, v]) => [
      `LONG-MULTIPLE-${k}`,
      v,
    ])
  );
  info(
    `Long multiple query journal = ${JSON.stringify(journalLongMultiplePrefixed)}`
  );
  reportMetrics(journalLongMultiplePrefixed);

  const journalLong = {};
  await runInferenceAndCollectMetrics({
    semanticManager,
    numIterations,
    searchQuery: "best recipe with nutritional value and taste that kids like",
    journal: journalLong,
    tag,
    concurrentInferenceFlag,
  });
  const updateTime = await cleanupSemanticSearchTest({
    semanticManager,
    conn,
    cleanup,
  });
  const journalLongPrefixed = Object.fromEntries(
    Object.entries(journalLong).map(([k, v]) => [`LONG-${k}`, v])
  );
  journalLongPrefixed[`LONG-SEMANTIC-UPDATE_TASK_LATENCY-${tag}`] = [
    updateTime,
  ];
  journalLongPrefixed[`LONG-SEMANTIC-VECTOR_DB_DISK_SIZE-${tag}`] = [
    vecDBFileSize,
  ];
  info(`Long query journal = ${JSON.stringify(journalLongPrefixed)}`);
  reportMetrics(journalLongPrefixed);
}

// One backend per session: cleanupSemanticSearchTest() deletes the Places
// database, which the setup cannot recover from. CI covers the other backend
// in its own job via MOZ_ML_BACKENDS, which overrides this list.
const SEMANTIC_BACKENDS = ["onnx-native"];

add_task(async function test_short_and_long_query_perf() {
  await runMLPerfTestForEachBackend({
    name: "SEMANTIC",
    backends: SEMANTIC_BACKENDS,
    run: backendInfo => runShortAndLongQueryPerfTest(false, backendInfo),
  });
});

add_task(async function test_concurrent_short_and_long_query_perf() {
  await runMLPerfTestForEachBackend({
    name: "SEMANTIC-CONCURRENT",
    backends: SEMANTIC_BACKENDS,
    run: backendInfo => runShortAndLongQueryPerfTest(true, backendInfo),
  });
});
