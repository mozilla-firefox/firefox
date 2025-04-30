/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

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
      try_platform: ["linux", "mac", "win"],
    },
  },
};

requestLongerTimeout(120);
const CUSTOM_EMBEDDER_OPTIONS = {
  taskName: "feature-extraction",
  featureId: "simple-text-embedder",
  modelId: "Xenova/all-MiniLM-L6-v2",
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

async function waitForPendingUpdates() {
  return new Promise(resolve => {
    // Listen for the update completion event
    Services.obs.addObserver(function observer() {
      Services.obs.removeObserver(
        observer,
        "places-semantichistorymanager-update-complete"
      );
      resolve();
    }, "places-semantichistorymanager-update-complete");
  });
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
  testFlag = true,
  concurrentInferenceFlag = false,
}) {
  const lazy = {};

  ChromeUtils.defineESModuleGetters(lazy, {
    PlacesSemanticHistoryManager:
      "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs",
  });

  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  if (!modelHubRootUrl) {
    throw new Error(
      "MOZ_MODELS_HUB is not set, you need to run with --hooks toolkit/components/ml/tests/tools/hook_local_hub.py"
    );
  }

  const { cleanup } = await perfSetup({
    prefs: [
      ["browser.ml.modelHubRootUrl", modelHubRootUrl],
      ["javascript.options.wasm_lazy_tiering", true],
      ["browser.ml.logLevel", "Info"],
    ],
  });

  const semanticManager = new lazy.PlacesSemanticHistoryManager({
    embeddingSize: 384,
    rowLimit,
    samplingAttrib: "frecency",
    changeThresholdCount: 0,
    distanceThreshold: 1.0,
    testFlag,
  });

  if (!semanticManager.qualifiedForSemanticSearch) {
    info("Skipping test due to insufficient hardware.");
    Assert.ok(true);
    return { skip: true };
  }

  semanticManager.embedder.options = CUSTOM_EMBEDDER_OPTIONS;
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
  journal,
  concurrentInferenceFlag = false,
}) {
  const queryContext = { searchString: searchQuery };
  const startCpu = Math.floor(await getCpuTimeFromProcInfo());

  for (let i = 0; i < numIterations; i++) {
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
    const metrics = fetchMetrics(res.metrics);
    let embeddingLatency = 0;

    for (const [metricName, value] of Object.entries(metrics)) {
      const safeValue = value > 0 ? value : 0;
      journal[`SEMANTIC-${metricName}`] =
        journal[`SEMANTIC-${metricName}`] || [];
      journal[`SEMANTIC-${metricName}`].push(safeValue);

      if (metricName === MODEL_RUN_LATENCY) {
        embeddingLatency = safeValue;
      }
    }

    journal[`SEMANTIC-${SEARCH_LATENCY}`] =
      journal[`SEMANTIC-${SEARCH_LATENCY}`] || [];
    journal[`SEMANTIC-${SEARCH_LATENCY}`].push(duration - embeddingLatency);

    journal[`SEMANTIC-${INFERENCE_LATENCY}`] =
      journal[`SEMANTIC-${INFERENCE_LATENCY}`] || [];
    journal[`SEMANTIC-${INFERENCE_LATENCY}`].push(duration);

    journal[`SEMANTIC-${TOTAL_MEMORY_USAGE}`] =
      journal[`SEMANTIC-${TOTAL_MEMORY_USAGE}`] || [];
    journal[`SEMANTIC-${TOTAL_MEMORY_USAGE}`].push(memUsage);
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
  await semanticManager.semanticDB.dropSchema();
  await cleanup();

  return updateTime;
}

async function runShortAndLongQueryPerfTest(concurrentInferenceFlag) {
  const rowLimit = 10000;
  const numIterations = 20;
  const mode = concurrentInferenceFlag ? "CONCURRENT" : "SEQUENTIAL";
  info(`Running ${mode} inference performance test...`);

  const setupResult = await prepareSemanticSearchTest({
    rowLimit,
    concurrentInferenceFlag,
  });

  if (setupResult.skip) {
    return;
  }

  const { semanticManager, conn, vecDBFileSize, cleanup } = setupResult;

  const journalShort = {};
  await runInferenceAndCollectMetrics({
    semanticManager,
    numIterations,
    searchQuery: "health tips",
    journal: journalShort,
    concurrentInferenceFlag,
  });

  const journalShortPrefixed = Object.fromEntries(
    Object.entries(journalShort).map(([k, v]) => [`SHORT-${k}`, v])
  );
  journalShortPrefixed["SEMANTIC-VECTOR_DB_DISK_SIZE"] = [vecDBFileSize];
  info(`Short query journal = ${JSON.stringify(journalShortPrefixed)}`);
  reportMetrics(journalShortPrefixed);

  const journalLong = {};
  await runInferenceAndCollectMetrics({
    semanticManager,
    numIterations,
    searchQuery: "best recipe with nutritional value and taste that kids like",
    journal: journalLong,
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
  journalLongPrefixed["LONG-SEMANTIC-UPDATE_TASK_LATENCY"] = [updateTime];
  journalLongPrefixed["LONG-SEMANTIC-VECTOR_DB_DISK_SIZE"] = [vecDBFileSize];
  info(`Long query journal = ${JSON.stringify(journalLongPrefixed)}`);
  reportMetrics(journalLongPrefixed);
}

add_task(async function test_short_and_long_query_perf() {
  await runShortAndLongQueryPerfTest(false);
});

add_task(async function test_concurrent_short_and_long_query_perf() {
  await runShortAndLongQueryPerfTest(true);
});
