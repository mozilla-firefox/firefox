/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/// <reference path="../../../../../toolkit/components/translations/tests/browser/shared-head.js" />

// Load the shared-head file first.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/shared-head.js",
  this
);

/**
 * @type {import("../../actors/MLEngineParent.sys.mjs")}
 */
const { MLEngineParent, MLEngine } = ChromeUtils.importESModule(
  "resource://gre/actors/MLEngineParent.sys.mjs"
);

const { ModelHub, TestIndexedDBCache } = ChromeUtils.importESModule(
  "chrome://global/content/ml/ModelHub.sys.mjs"
);

const { getInferenceProcessInfo } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const MS_PER_SEC = 1000;
const IndexedDBCache = TestIndexedDBCache;

/**
 * @type {import("../../../ml/content/EngineProcess.sys.mjs")}
 */
const {
  createEngine,
  PipelineOptions,
  QuantizationLevel,
  ExecutionPriority,
  InferenceDevice,
  LogLevel,
} = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);

// This test suite shares some utility functions with translations as they work in a very
// similar fashion. Eventually, the plan is to unify these two components.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/translations/tests/browser/shared-head.js",
  this
);

/**
 * Mock out remote settings and set some default preferences for the testing environment.
 */
async function setup({
  disabled = false,
  prefs = [],
  records = null,
  backend,
} = {}) {
  const { removeMocks, remoteClients } = await createAndMockMLRemoteSettings({
    autoDownloadFromRemoteSettings: false,
    records,
    backend,
  });

  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.ml.enable", !disabled],
      ["browser.ml.logLevel", "All"],
      ["browser.ml.modelCacheTimeout", 1000],
      ["browser.ml.checkForMemory", false],
      ["browser.ml.queueWaitTimeout", 2],
      ["javascript.options.wasm_lazy_tiering", true],
      ...prefs,
    ],
  });

  return {
    remoteClients,
    async cleanup() {
      await removeMocks();
      await waitForCondition(
        () => EngineProcess.areAllEnginesTerminated(),
        "Waiting for all of the engines to be terminated.",
        100,
        200
      );
      await SpecialPowers.popPrefEnv();
    },
  };
}

function getDefaultWasmRecords(backend) {
  // A requested backend that isn't itself a wasm runtime (e.g. "best-onnx" or
  // "onnx-native") can still fall back to the wasm onnx backend at engine
  // creation time -- on platforms without the native onnxruntime, "best-onnx"
  // resolves to "onnx". WASM_FILENAME only knows the real wasm backends, so
  // map anything else to DEFAULT_BACKEND and register the concrete wasm record
  // that fallback would request.
  const wasmBackend =
    backend && MLEngineParent.WASM_FILENAME[backend]
      ? backend
      : MLEngineParent.DEFAULT_BACKEND;
  return [
    {
      name: MLEngineParent.WASM_FILENAME[wasmBackend],
      version: MLEngineParent.WASM_MAJOR_VERSION[wasmBackend] + ".0",
    },
  ];
}

async function createAndMockMLRemoteSettings({
  autoDownloadFromRemoteSettings = false,
  records = null,
  backend,
} = {}) {
  const wasmRecords = getDefaultWasmRecords(backend).map(
    ({ name, version }) => ({
      id: crypto.randomUUID(),
      name,
      version,
      last_modified: Date.now(),
      schema: Date.now(),
    })
  );
  const runtime = await createRemoteClient({
    collectionName: "test-translation-wasm",
    records: wasmRecords,
    attachmentMock: true,
    autoDownloadFromRemoteSettings,
  });

  const options = await createRemoteClient({
    records: records || [
      {
        taskName: "moz-echo",
        modelId: "mozilla/distilvit",
        processorId: "mozilla/distilvit",
        tokenizerId: "mozilla/distilvit",
        modelRevision: "main",
        processorRevision: "main",
        tokenizerRevision: "main",
        dtype: "q8",
        id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
      },
    ],
    collectionName: "test-ml-inference-options",
  });

  const allowDeny = await createRemoteClient({
    records: [
      {
        filter: "ALLOW",
        urlPrefix: "https://",
        id: "74a71cfd-1734-44e6-85c0-69cf3e874138",
      },
    ],
    collectionName: "test-ml-allow-deny-list",
  });

  const remoteClients = {
    "ml-onnx-runtime": runtime,
    "ml-inference-options": options,
    "ml-model-allow-deny-list": allowDeny,
  };

  MLEngineParent.mockRemoteSettings({
    "ml-onnx-runtime": runtime.client,
    "ml-inference-options": options,
    "ml-model-allow-deny-list": allowDeny,
  });

  return {
    async removeMocks() {
      await runtime.client.attachments.deleteAll();
      await runtime.client.db.clear();
      await options.db.clear();
      await allowDeny.db.clear();
      MLEngineParent.removeMocks();
    },
    remoteClients,
  };
}

/**
 * Creates a local RemoteSettingsClient for use within tests.
 *
 * @returns {RemoteSettings|AttachmentMock}
 */
async function createRemoteClient({
  records,
  collectionName,
  attachmentMock = false,
  autoDownloadFromRemoteSettings = false,
}) {
  const { RemoteSettings } = ChromeUtils.importESModule(
    "resource://services-settings/remote-settings.sys.mjs"
  );
  const client = RemoteSettings(`${collectionName}-${_remoteSettingsMockId++}`);
  await client.db.clear();
  await client.db.importChanges({}, Date.now(), records);

  if (attachmentMock) {
    return createAttachmentMock(
      client,
      collectionName,
      autoDownloadFromRemoteSettings
    );
  }
  return client;
}

/*
 * Perftest related
 */
const ONE_MIB = 1024 * 1024;
const INIT_START = "initializationStart";
const INIT_END = "initializationEnd";
const RUN_START = "runStart";
const RUN_END = "runEnd";
const PIPELINE_READY_START = "ensurePipelineIsReadyStart";
const PIPELINE_READY_END = "ensurePipelineIsReadyEnd";
const PIPELINE_READY_LATENCY = "pipeline-ready-latency";
const INITIALIZATION_LATENCY = "initialization-latency";
const MODEL_RUN_LATENCY = "model-run-latency";
const TOTAL_MEMORY_USAGE = "total-memory-usage";
const COLD_START_PREFIX = "cold-start-";
const PEAK_MEMORY_USAGE = "peak-memory-usage";
const ITERATIONS = 4;
const WHEN = "when";
const MEMORY = "memory";
const E2E_INIT_LATENCY = "e2e-init-latency";
const E2E_RUN_LATENCY = "e2e-run-latency";
const FIRST_TOKEN_LATENCY = "1st-token-latency";
const DECODING_LATENCY = "decoding-latency";
// Token speeds are apppropriate for comparing the speed of the same model.
const DECODING_TOKEN_SPEED = "decoding-tokenSpeed";
const PROMPT_TOKEN_SPEED = "prompt-tokenSpeed";
// Characters speed is appropriate for comparing the speed of two different models.
const DECODING_CHARACTERS_SPEED = "decoding-charactersSpeed";
const PROMPT_CHARACTERS_SPEED = "prompt-charactersSpeed";

const formatNumber = new Intl.NumberFormat("en-US", {
  maximumSignificantDigits: 4,
}).format;

function median(arr) {
  arr = [...arr].sort((a, b) => a - b);
  const mid = Math.floor(arr.length / 2);

  if (arr.length % 2) {
    return arr[mid];
  }

  return (arr[mid - 1] + arr[mid]) / 2;
}

function stringify(arr) {
  function pad(str) {
    str = str.padStart(7, " ");
    if (str[0] != " ") {
      str = " " + str;
    }
    return str;
  }

  return arr.reduce((acc, elem) => acc + pad(formatNumber(elem)), "");
}

function reportMetrics(journal) {
  let text = "\nResults (ms)\n";
  const names = Object.keys(journal);
  const prefixLen = 1 + Math.max(...names.map(str => str.length));
  for (const name in journal) {
    const med = median(journal[name]);
    text += (name + ":").padEnd(prefixLen, " ") + stringify(journal[name]);
    text += "   median " + formatNumber(med) + "\n";
  }
  const reportedMetrics = [];
  for (const [name, values] of Object.entries(journal)) {
    reportedMetrics.push({
      name,
      values,
      value: median(values),
    });
  }
  dump(text);
  info(`perfMetrics | ${JSON.stringify(reportedMetrics)}`);
}

/**
 * Fetches the latest metric entry with the specified name and retrieves its value for the given key.
 * If multiple metrics share the same name, the function returns the key from the most recent one.
 *
 * @param {Array<object>} metrics - The array of metric objects to search through.
 * @param {string} name - The name of the metric to find.
 * @param {string} key - The key within the metric object whose value should be returned.
 * @returns {*} - The value of the specified key in the latest metric with the given name, or undefined if no matching metric is found.
 */
function fetchMLMetric(metrics, name, key) {
  const matchingMetrics = metrics.filter(metric => metric.name === name);
  if (matchingMetrics.length === 0) {
    return undefined;
  } // Return undefined if no match found
  const latestMetric = matchingMetrics[matchingMetrics.length - 1];
  return latestMetric[key];
}

function fetchLatencyMetrics(metrics, isFirstRun) {
  let timestamps = [];
  if (Array.isArray(metrics?.runTimestamps)) {
    timestamps = metrics.runTimestamps;
  } else if (Array.isArray(metrics)) {
    timestamps = metrics;
  }
  const pipelineLatency =
    fetchMLMetric(timestamps, PIPELINE_READY_END, WHEN) -
    fetchMLMetric(timestamps, PIPELINE_READY_START, WHEN);
  const initLatency =
    fetchMLMetric(timestamps, INIT_END, WHEN) -
    fetchMLMetric(timestamps, INIT_START, WHEN);
  const runLatency =
    fetchMLMetric(timestamps, RUN_END, WHEN) -
    fetchMLMetric(timestamps, RUN_START, WHEN);
  return {
    [`${isFirstRun ? COLD_START_PREFIX : ""}${PIPELINE_READY_LATENCY}`]:
      pipelineLatency,
    [`${isFirstRun ? COLD_START_PREFIX : ""}${INITIALIZATION_LATENCY}`]:
      initLatency,
    [`${isFirstRun ? COLD_START_PREFIX : ""}${MODEL_RUN_LATENCY}`]: runLatency,
  };
}

function fetchMetrics(metrics, isFirstRun) {
  return {
    ...fetchLatencyMetrics(metrics, isFirstRun),
  };
}

async function initializeEngine(pipelineOptions, prefs = null) {
  const modelDirectory = normalizePathForOS(
    `${Services.env.get("MOZ_FETCHES_DIR")}/onnx-models`
  );
  info(`Model Directory: ${modelDirectory}`);

  const modelHubRootUrl = Services.env.get("MOZ_MODELS_HUB");
  if (!modelHubRootUrl) {
    throw new Error(
      "MOZ_MODELS_HUB is not set, you need to run with --hooks toolkit/components/ml/tests/tools/hooks_local_hub.py"
    );
  }

  info(`ModelHubRootUrl: ${modelHubRootUrl}`);
  var browserPrefs = [["browser.ml.modelHubRootUrl", modelHubRootUrl]];
  if (prefs) {
    browserPrefs = browserPrefs.concat(prefs);
  }

  const { cleanup } = await perfSetup({
    prefs: browserPrefs,
    backend: pipelineOptions.backend,
  });
  info("Get the engine process");
  const startTime = performance.now();
  let engine;
  try {
    engine = await createEngine(new PipelineOptions(pipelineOptions));
  } catch (error) {
    // A failed init would otherwise leak the engine process and the pushed
    // prefs into the file's remaining runs.
    await EngineProcess.destroyMLEngine();
    await cleanup();
    throw error;
  }
  const e2eInitTime = performance.now() - startTime;

  info("Get Pipeline Options");
  info("Run the inference");
  return {
    cleanup,
    engine,
    e2eInitTime,
  };
}

function normalizePathForOS(path) {
  if (Services.appinfo.OS === "WINNT") {
    // On Windows, replace forward slashes with backslashes
    return path.replace(/\//g, "\\");
  }

  // On Unix-like systems, replace backslashes with forward slashes
  return path.replace(/\\/g, "/");
}

async function perfSetup({ disabled = false, prefs = [], backend } = {}) {
  const { removeMocks, remoteClients } = await createAndMockMLRemoteSettings({
    autoDownloadFromRemoteSettings: false,
    backend,
  });

  var finalPrefs = [
    // Enabled by default.
    ["browser.ml.enable", !disabled],
    ["browser.ml.logLevel", "Error"],
    ["browser.ml.modelCacheTimeout", 1000],
    ["browser.ml.checkForMemory", false],
    ["javascript.options.wasm_lazy_tiering", true],
    ...prefs,
  ];

  await SpecialPowers.pushPrefEnv({
    set: finalPrefs,
  });

  let artifactDirectory = normalizePathForOS(
    `${Services.env.get("MOZ_FETCHES_DIR")}`
  );

  async function pathExists(path) {
    try {
      return await IOUtils.exists(path);
    } catch (e) {
      return false;
    }
  }

  // Stop immediately if this fails.
  if (!artifactDirectory) {
    artifactDirectory = normalizePathForOS(
      `${Services.env.get("MOZ_ML_LOCAL_DIR")}`
    );
  }

  if (!artifactDirectory) {
    throw new Error(
      `The wasm artifact directory is not set. This usually happens when running locally. " +
      "Please download all the files from taskcluster/kinds/fetch/onnxruntime-web-fetch.yml. " +
      "Place them in a directory and rerun the test with the environment variable 'MOZ_ML_LOCAL_DIR' " +
      "set such that all the files are directly inside 'MOZ_ML_LOCAL_DIR'`
    );
  }

  if (!PathUtils.isAbsolute(artifactDirectory)) {
    throw new Error(
      "Please provide an absolute path for 'MOZ_ML_LOCAL_DIR and not a relative path"
    );
  }

  async function download(record) {
    info(`Downloading record: ${record.name}`);
    const recordPath = normalizePathForOS(
      `${artifactDirectory}/${record.name}`
    );

    // Stop immediately if this fails.
    if (!(await pathExists(recordPath))) {
      throw new Error(`The wasm file <${recordPath}> does not exist. This usually happens when running locally. " +
        "Please download all the files from taskcluster/kinds/fetch/onnxruntime-web-fetch.yml. " +
        "Place them in the directory <${artifactDirectory}> " +
        "such that <${recordPath}> exists.`);
    }

    return {
      buffer: (await IOUtils.read(recordPath)).buffer,
    };
  }

  remoteClients["ml-onnx-runtime"].client.attachments.download = download;

  let downloadRSAttachmentStub;

  // Lot of unit tests even the one outside our components declare sinon
  // So we are not making it module level to allow including this file without issue
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );

  if (typeof MLEngineParent.downloadRSAttachment.restore !== "function") {
    downloadRSAttachmentStub = sinon
      .stub(MLEngineParent, "downloadRSAttachment")
      .callsFake(async ({ wasmRecord }) => {
        return (await download(wasmRecord)).buffer;
      });
  }

  return {
    remoteClients,
    async cleanup() {
      await removeMocks();
      await waitForCondition(
        () => EngineProcess.areAllEnginesTerminated(),
        "Waiting for all of the engines to be terminated.",
        100,
        200
      );
      await SpecialPowers.popPrefEnv();
      if (downloadRSAttachmentStub) {
        downloadRSAttachmentStub.restore();
      }
    },
  };
}

/**
 * Returns the current total physical memory usage in MiB for the inference process
 */
async function getTotalMemoryUsage() {
  const procInfo = await getInferenceProcessInfo();
  return Math.round(procInfo.memory / ONE_MIB);
}

/**
 * Runs an inference given the options and arguments
 *
 */
async function runInference({
  pipelineOptions,
  request,
  isFirstRun = false,
  browserPrefs = null,
}) {
  info(
    `runInference is request null | ${request === null || request === undefined}`
  );
  const { cleanup, engine, e2eInitTime } = await initializeEngine(
    pipelineOptions,
    browserPrefs
  );

  const streamerOptions = {
    perTokens: true,
    skipPrompt: pipelineOptions.taskName !== "text-generation",
    returnTokens: true,
    ...(request.streamerOptions || {}),
  };
  request = { ...request, streamerOptions };

  let metrics = {};
  let timeToFirstToken;
  let startTime;
  let runStartTime;
  let runEndTime;
  let numGeneratedCharacters = 0;
  let numGeneratedTokens = 0;
  let numPromptCharacters = 0;
  if (streamerOptions.skipPrompt && Array.isArray(request?.args)) {
    numPromptCharacters += request.args
      .flat()
      .reduce((sum, item) => sum + (item?.length || 0), 0);
  }
  let numPromptTokens = 0;
  const run = async () => {
    let isFirstTokenReceived = false;
    let result;
    let currentTokenLen = 0;
    let currentCharLen = 0;
    startTime = performance.now();
    runStartTime = startTime;
    const generator = engine.runWithGenerator(request);

    do {
      result = await generator.next();

      currentTokenLen = result.value?.tokens?.flat()?.length || 0;
      currentCharLen = result.value?.text?.length || 0;

      if (result.value?.isPrompt) {
        numPromptCharacters += currentCharLen;
        numPromptTokens += currentTokenLen;
      } else {
        numGeneratedCharacters += currentCharLen;
        numGeneratedTokens += currentTokenLen;
        if (!isFirstTokenReceived) {
          timeToFirstToken = performance.now() - startTime;
          isFirstTokenReceived = true;
          startTime = performance.now();
        }
      }
    } while (!result.done);

    return result.value;
  };

  try {
    const res = await run();
    runEndTime = performance.now();
    const decodingTime = runEndTime - startTime;
    metrics = fetchMetrics(res.metrics?.runTimestamps || [], isFirstRun);
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${TOTAL_MEMORY_USAGE}`] =
      await getTotalMemoryUsage();

    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${E2E_INIT_LATENCY}`] =
      e2eInitTime;
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${FIRST_TOKEN_LATENCY}`] =
      timeToFirstToken;
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${DECODING_LATENCY}`] =
      decodingTime;
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${E2E_RUN_LATENCY}`] =
      runEndTime - runStartTime;
    metrics[
      `${isFirstRun ? COLD_START_PREFIX : ""}${DECODING_CHARACTERS_SPEED}`
    ] = numGeneratedCharacters / (decodingTime / MS_PER_SEC);
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${DECODING_TOKEN_SPEED}`] =
      numGeneratedTokens / (decodingTime / MS_PER_SEC);
    metrics[
      `${isFirstRun ? COLD_START_PREFIX : ""}${PROMPT_CHARACTERS_SPEED}`
    ] = numPromptCharacters / (timeToFirstToken / MS_PER_SEC);
    metrics[`${isFirstRun ? COLD_START_PREFIX : ""}${PROMPT_TOKEN_SPEED}`] =
      numPromptTokens / (timeToFirstToken / MS_PER_SEC);
  } finally {
    await engine.terminate();
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
  return metrics;
}

/**
 * Can be used to track peak memory
 *
 */
class PeakMemoryTracker {
  constructor(interval = 500) {
    this._memory = 0;
    this._intervalId = null;
    this._interval = interval;
  }

  async collectPeakMemory() {
    const procInfo = await getInferenceProcessInfo();
    if (procInfo.memory && procInfo.memory > this._memory) {
      this._memory = procInfo.memory;
    }
  }

  start() {
    if (this._intervalId !== null) {
      return;
    } // Prevent multiple intervals
    this._intervalId = setInterval(() => {
      this.collectPeakMemory().catch(console.error);
    }, this._interval);
  }

  stop() {
    if (this._intervalId !== null) {
      clearInterval(this._intervalId);
      this._intervalId = null;
    }

    try {
      return Math.round(this._memory / ONE_MIB);
    } finally {
      this._memory = 0;
    }
  }
}
/**
 * Tags used in per-backend metric names, decoupled from the backend
 * identifiers so perfherder series survive a backend rename.
 *
 * @type {Record<string, string>}
 */
const BACKEND_TAGS = {
  "onnx-native": "NATIVE",
  onnx: "WASM",
};

/**
 * Resolves which ONNX backends a perf test should measure. The
 * MOZ_ML_BACKENDS environment variable (comma separated, set per CI task in
 * taskcluster/kinds/perftest/*.yml) overrides everything, including
 * test-provided candidates; without it, this returns the most preferred
 * candidate that can run here.
 *
 * @param {object} [opts]
 * @param {string[]} [opts.candidates] - Backends to consider, most preferred
 *   first. Defaults to the full ONNX matrix.
 * @returns {Promise<string[]>} Concrete backends to measure, never empty.
 */
async function resolveBackendMatrix({
  candidates = ["onnx-native", "onnx"],
} = {}) {
  const override = Services.env.get("MOZ_ML_BACKENDS");
  if (override) {
    const requested = override
      .split(",")
      .map(s => s.trim())
      .filter(Boolean);
    info(`Backend matrix pinned by MOZ_ML_BACKENDS: ${requested.join(", ")}`);
    return requested;
  }

  const nativeAvailable =
    await EngineProcess.requestIsNativeOnnxRuntimeAvailable();

  const matrix = candidates
    .filter(backend => backend !== "onnx-native" || nativeAvailable)
    .slice(0, 1);

  info(
    `Backend matrix: ${matrix.join(", ")} ` +
      `(native onnxruntime ${nativeAvailable ? "available" : "unavailable"})`
  );

  if (!matrix.length) {
    throw new Error(
      `No backend to measure: none of [${candidates.join(", ")}] can run here`
    );
  }

  return matrix;
}

/**
 * Runs a performance test on every applicable ONNX backend, tagging metric
 * names per backend (e.g. "SMART-TAB-TOPIC-model-run-latency-NATIVE") so each
 * is its own perfherder series. The tag is appended after the metric name so
 * the names declared in perfMetadata stay substrings of the reported names.
 * The tag is applied even when a single backend runs, keeping series names
 * platform-independent.
 *
 * @param {object} config - See runMLPerfTestOnBackend, plus:
 * @param {string[]} [config.backends] - Backend candidates for runs where
 *   MOZ_ML_BACKENDS is not set. Pass an explicit list for tests whose backend
 *   is not negotiable (e.g. llama.cpp).
 */
async function runMLPerfTest({ backends, ...config }) {
  await runMLPerfTestForEachBackend({
    name: config.name,
    backends,
    run: ({ backend, tag }) =>
      runMLPerfTestOnBackend({
        ...config,
        tag,
        options: { ...config.options, backend },
      }),
  });
}

/**
 * Runs `run` once per applicable ONNX backend, for tests that own their
 * engine creation and metric naming: they pass `backend` into the options
 * they build and append `tag` to their metric names. A backend that cannot
 * run fails the test.
 *
 * @param {object} config
 * @param {string} config.name - Feature name, used for logging.
 * @param {string[]} [config.backends] - Backend candidates for runs where
 *   MOZ_ML_BACKENDS is not set, most preferred first. Defaults to the full
 *   ONNX matrix.
 * @param {function({backend: string, tag: string}): Promise} config.run -
 *   Runs and reports the measurements for one backend.
 */
async function runMLPerfTestForEachBackend({ name, backends, run }) {
  const matrix = await resolveBackendMatrix(
    backends ? { candidates: backends } : {}
  );

  for (const backend of matrix) {
    const tag = BACKEND_TAGS[backend] ?? backend.toUpperCase();
    info(`Running ${name} on backend ${backend}`);
    await run({ backend, tag });
  }

  Assert.ok(
    true,
    `${name} measured every backend in the matrix (${matrix.join(", ")})`
  );
}

/**
 * Runs a performance test for the given name, options, and arguments and
 * reports the results for perfherder.
 */
async function runMLPerfTestOnBackend({
  name,
  tag,
  options,
  request,
  iterations = ITERATIONS,
  addColdStart = false,
  trackPeakMemory = false,
  peakMemoryInterval = 500,
  browserPrefs = null,
}) {
  info(`is request null | ${request === null || request === undefined}`);
  name = name.toUpperCase();
  const taggedMetric = metric => `${name}-${metric}-${tag}`;

  let METRICS;

  // When tracking peak memory we only do this because we're
  // stressing the system with 500ms callbacks so other netrics are impacted
  if (trackPeakMemory) {
    METRICS = [PEAK_MEMORY_USAGE];
  } else {
    METRICS = [
      PIPELINE_READY_LATENCY,
      INITIALIZATION_LATENCY,
      MODEL_RUN_LATENCY,
      TOTAL_MEMORY_USAGE,
      E2E_RUN_LATENCY,
      E2E_INIT_LATENCY,
      FIRST_TOKEN_LATENCY,
      DECODING_LATENCY,
      DECODING_CHARACTERS_SPEED,
      DECODING_TOKEN_SPEED,
      PROMPT_CHARACTERS_SPEED,
      PROMPT_TOKEN_SPEED,
      ...(addColdStart
        ? [
            `${COLD_START_PREFIX}${PIPELINE_READY_LATENCY}`,
            `${COLD_START_PREFIX}${INITIALIZATION_LATENCY}`,
            `${COLD_START_PREFIX}${MODEL_RUN_LATENCY}`,
            `${COLD_START_PREFIX}${TOTAL_MEMORY_USAGE}`,
          ]
        : []),
    ];
  }

  const journal = {};
  for (let metric of METRICS) {
    journal[taggedMetric(metric)] = [];
  }

  const pipelineOptions = new PipelineOptions(options);
  var tracker;

  let nIterations = addColdStart ? iterations + 1 : iterations;
  for (let i = 0; i < nIterations; i++) {
    if (trackPeakMemory) {
      tracker = new PeakMemoryTracker(peakMemoryInterval);
      tracker.start();
    }
    const shouldAddColdStart = addColdStart && i === 0;
    let metrics = await runInference({
      pipelineOptions,
      request,
      isFirstRun: shouldAddColdStart,
      browserPrefs,
    });
    if (trackPeakMemory) {
      journal[taggedMetric(PEAK_MEMORY_USAGE)].push(tracker.stop());
    } else {
      for (let [metricName, metricVal] of Object.entries(metrics)) {
        if (!Number.isFinite(metricVal) || metricVal < 0) {
          metricVal = 0;
        }
        // Add the metric if it wasn't there
        if (journal[taggedMetric(metricName)] === undefined) {
          journal[taggedMetric(metricName)] = [];
        }
        journal[taggedMetric(metricName)].push(metricVal);
      }
    }
  }
  Assert.ok(true);
  reportMetrics(journal);
}

/**
 * Measures floating point value within epsilon tolerance
 *
 * @param {number[]} a
 * @param {number[]} b
 * @param {number} [epsilon]
 * @returns {boolean}
 */
function isEqualWithTolerance(a, b, epsilon = 0.000001) {
  return Math.abs(Math.abs(a) - Math.abs(b)) < epsilon;
}

/**
 * Asserts whether two float arrays are equal within epsilon tolerance.
 *
 * @param {number[] | ArrayBufferLike} a
 * @param {number[] | ArrayBufferLike} b
 * @param {string} message
 * @param {number} [epsilon]
 */
function assertFloatArraysMatch(a, b, message, epsilon) {
  const raise = () => {
    // When logging errors, spread into a new array so that the logging is nice for
    // ArrayBufferLike values. This makes it easy to see how arrays differ
    console.log("a:", [...a]);
    console.log("b:", [...b]);
    throw new Error(message);
  };
  if (a.length !== b.length) {
    raise();
  }
  for (let i = 0; i < a.length; i++) {
    if (!isEqualWithTolerance(a[i], b[i], epsilon)) {
      raise();
    }
  }
  ok(true, message);
}

// Mock OpenAI Chat Completions server for mochitests
// Serves: http://localhost:11434/v1/chat/completions

function readRequestBody(request) {
  // Read the POST body as UTF-8 text
  const stream = request.bodyInputStream;
  const available = stream.available();
  return NetUtil.readInputStreamToString(stream, available, {
    charset: "UTF-8",
  });
}

/**
 * @param {object} [options]
 * @param {string} [options.echo] - Text echoed back on the non-streaming path.
 * @param {Function|null} [options.onRequest] - Called with each raw request.
 * @param {boolean} [options.holdStreamOpenAfterFinish] - When true, the
 *   streaming tool-call turn stops talking after finish_reason and never closes
 *   the connection, so the client is the only thing that can end the turn.
 */
function startMockOpenAI({
  echo = "This gets echoed.",
  onRequest = null,
  holdStreamOpenAfterFinish = false,
} = {}) {
  const server = new HttpServer();

  // Tracked so a test using holdStreamOpenAfterFinish can still stop the server.
  const heldResponses = [];

  server.registerPathHandler("/v1/chat/completions", (request, response) => {
    info("[openai] GET /v1/chat/completions");

    // Call the onRequest callback if provided to allow test inspection
    if (onRequest) {
      onRequest(request);
    }

    let bodyText = "";
    if (request.method === "POST") {
      try {
        bodyText = readRequestBody(request);
      } catch (_) {}
    }
    info("[openai] bodyText: " + bodyText);

    let body;
    try {
      body = JSON.parse(bodyText || "{}");
    } catch (_) {
      body = {};
    }

    const wantsStream = !!body.stream;
    const tools = Array.isArray(body.tools) ? body.tools : [];
    const askedForTools = tools.length;
    const messages = Array.isArray(body.messages) ? body.messages : [];
    const hasToolResult = messages.some(m => m && m.role === "tool");

    // ---- SSE helpers (for streaming mode) ----
    function startSSE() {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader(
        "Content-Type",
        "text/event-stream; charset=utf-8",
        false
      );
      response.setHeader("Cache-Control", "no-cache", false);
      response.setHeader("Access-Control-Allow-Origin", "*", false);
      response.processAsync();
    }
    function sendSSE(obj) {
      const line = `data: ${JSON.stringify(obj)}\n\n`;
      response.write(line);
    }
    function endSSE() {
      response.write("data: [DONE]\n\n");
      response.finish();
    }

    // ===========================
    // STREAMING BRANCHES (SSE)
    // ===========================
    if (wantsStream && askedForTools && !hasToolResult) {
      info("[openai] Streaming tool call, without tool results");
      // First turn: stream partial tool_calls, then finish with "tool_calls"
      startSSE();

      // Partial 1: name/args prefix
      sendSSE({
        id: "chatcmpl-mock-tools-stream-1",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: {
              content: "",
              tool_calls: [
                {
                  index: 0,
                  id: "call_1",
                  type: "function",
                  function: { name: "search_", arguments: '{ "type": "ne' },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      });

      // Partial 2: complete name/args
      sendSSE({
        id: "chatcmpl-mock-tools-stream-2",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: {
              content: "",
              tool_calls: [
                {
                  index: 0,
                  id: "call_1",
                  type: "function",
                  function: { name: "open_tabs", arguments: 'ws" }' },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      });

      // Signal the turn ends with tool calls
      sendSSE({
        id: "chatcmpl-mock-tools-stream-3",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [{ index: 0, delta: {}, finish_reason: "tool_calls" }],
      });

      if (holdStreamOpenAfterFinish) {
        heldResponses.push(response);
        return;
      }

      // Usage arrives after finish_reason, like the real endpoint.
      sendSSE({
        id: "chatcmpl-mock-tools-stream-usage",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [],
        usage: {
          prompt_tokens: 9839,
          completion_tokens: 12,
          total_tokens: 9851,
          prompt_tokens_details: { cached_tokens: 9800 },
        },
      });

      endSSE();
      return;
    }

    if (wantsStream && askedForTools && hasToolResult) {
      // Second turn (after tool result): stream normal assistant text
      info("[openai] Streaming tool call, with results");

      startSSE();

      sendSSE({
        id: "chatcmpl-mock-tools-stream-4",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: "Here are the tabs " },
            finish_reason: null,
          },
        ],
      });

      sendSSE({
        id: "chatcmpl-mock-tools-stream-5",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: "I found for you." },
            finish_reason: "stop",
          },
        ],
      });

      endSSE();
      return;
    }

    // Simple streaming (no tools)
    if (wantsStream && !askedForTools) {
      info("[openai] Simple streaming (no tools)");
      startSSE();

      // Send at least 3 chunks
      sendSSE({
        id: "chatcmpl-mock-stream-1",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: "Hello, " },
            finish_reason: null,
          },
        ],
      });

      sendSSE({
        id: "chatcmpl-mock-stream-2",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: "this is " },
            finish_reason: null,
          },
        ],
      });

      sendSSE({
        id: "chatcmpl-mock-stream-3",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: echo },
            finish_reason: null,
          },
        ],
      });

      // Final chunk with finish_reason
      sendSSE({
        id: "chatcmpl-mock-stream-4",
        object: "chat.completion.chunk",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            delta: { content: "" },
            finish_reason: "stop",
          },
        ],
      });

      endSSE();
      return;
    }

    // ===========================
    // NON-STREAMING BRANCHES
    // ===========================
    if (wantsStream) {
      throw new Error("Unhandled streaming case.");
    }

    // First turn w/ tools: return tool_calls message (finish_reason: tool_calls)
    if (askedForTools && !hasToolResult) {
      info("[openai] Non-streaming tool call without results");
      const payload = {
        id: "chatcmpl-mock-tools-1",
        object: "chat.completion",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            message: {
              role: "assistant",
              content: "",
              tool_calls: [
                {
                  id: "call_1",
                  type: "function",
                  function: {
                    name: "search_open_tabs",
                    arguments: JSON.stringify({ type: "news" }),
                  },
                },
              ],
            },
            finish_reason: "tool_calls",
          },
        ],
        usage: { prompt_tokens: 10, completion_tokens: 0, total_tokens: 10 },
        echo,
      };

      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader(
        "Content-Type",
        "application/json; charset=utf-8",
        false
      );
      response.setHeader("Access-Control-Allow-Origin", "*", false);
      response.write(JSON.stringify(payload));
      info("[openai] Sending back payload: " + JSON.stringify(payload));
      return;
    }

    // Second turn w/ tools (after tool result): normal assistant message
    if (askedForTools && hasToolResult) {
      info("[openai] Non-streaming tool call with results");
      const payload = {
        id: "chatcmpl-mock-tools-2",
        object: "chat.completion",
        created: Math.floor(Date.now() / 1000),
        model: "qwen3:0.6b",
        choices: [
          {
            index: 0,
            message: {
              role: "assistant",
              content: "Here are the tabs I found for you.",
            },
            finish_reason: "stop",
          },
        ],
        usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15 },
        echo,
      };

      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader(
        "Content-Type",
        "application/json; charset=utf-8",
        false
      );
      response.setHeader("Access-Control-Allow-Origin", "*", false);
      response.write(JSON.stringify(payload));
      info("[openai] Sending back payload: " + JSON.stringify(payload));
      return;
    }

    info("[openai] Non-streaming call");

    const payload = {
      id: "chatcmpl-mock-1",
      object: "chat.completion",
      created: Math.floor(Date.now() / 1000),
      model: "qwen3:0.6b",
      choices: [
        {
          index: 0,
          message: {
            role: "assistant",
            content: "This is a mock summary for testing end-to-end flow.",
          },
          finish_reason: "stop",
        },
      ],
      usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15 },
      echo,
    };

    info("[openai] Sending back payload: " + JSON.stringify(payload));
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader(
      "Content-Type",
      "application/json; charset=utf-8",
      false
    );
    response.setHeader("Access-Control-Allow-Origin", "*", false);
    response.write(JSON.stringify(payload));
  });

  function releaseHeldStreams() {
    while (heldResponses.length) {
      try {
        heldResponses.pop().finish();
      } catch (_) {
        // Already closed, because the client aborted the request.
      }
    }
  }

  // -1 tells it to pick an ephemeral port
  server.start(-1);
  const port = server.identity.primaryPort;
  return { server, port, releaseHeldStreams };
}

function stopMockOpenAI(server) {
  return new Promise(resolve => server.stop(resolve));
}

/**
 * Generates a numpy encoded float16 array to be used for generating static embeddings
 * test data.
 *
 * @param {number} vocabSize
 * @param {number} dimensions
 * @returns {{ numbers: Float16Array, encoding: Uint8Array }}
 */
function generateFloat16Numpy(vocabSize, dimensions) {
  const numbers = new Float16Array(vocabSize * dimensions);
  // Build the data:
  // [0.1, 0.2, 0.3, ..., 0.1 * vocabSize * dimensions]
  for (let i = 0; i < vocabSize; i++) {
    for (let j = 0; j < dimensions; j++) {
      const index = i * dimensions + j;
      numbers[index] = index / 10;
    }
  }
  const magic = new Uint8Array([0x93, 78, 85, 77, 80, 89]); // \x93NUMPY
  const version = new Uint8Array([1, 0]); // Version 1.0
  let header = `{'descr': '<f2', 'fortran_order': False, 'shape': (${vocabSize},${dimensions}), }`;

  // Pad header to 16-byte alignment
  const preLength = magic.length + version.length + 2; // +2 for header length field
  let padding = 16 - ((preLength + header.length + 1) % 16);
  if (padding === 16) {
    padding = 0;
  }
  header += " ".repeat(padding) + "\n";

  const headerBytes = new TextEncoder().encode(header);

  const headerLen = new Uint8Array(2);
  new DataView(headerLen.buffer).setUint16(0, headerBytes.length, true);

  const encoding = new Uint8Array(
    preLength + headerBytes.length + numbers.byteLength
  );

  // Write everything out.
  let offset = 0;
  encoding.set(magic, offset);
  offset += magic.length;
  encoding.set(version, offset);
  offset += version.length;
  encoding.set(headerLen, offset);
  offset += 2;
  encoding.set(headerBytes, offset);
  offset += headerBytes.length;
  encoding.set(new Uint8Array(numbers.buffer), offset);

  return { numbers, encoding };
}

/**
 * Checks that a process exists.
 *
 * @param {string} remoteType
 */
async function checkForRemoteType(remoteType) {
  let procinfo3 = await ChromeUtils.requestProcInfo();
  for (const child of procinfo3.children) {
    if (child.type === remoteType) {
      return true;
    }
  }
  return false;
}
