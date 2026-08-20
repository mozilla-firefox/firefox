/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import { BasePromiseWorker } from "resource://gre/modules/PromiseWorker.sys.mjs"
 * @import { PipelineOptions } from "chrome://global/content/ml/EngineProcess.sys.mjs"
 * @import { EngineStatus, EngineId, StatusByEngineId, PipelineOptionsRaw } from "../ml.d.ts"
 * @import { ProgressAndStatusCallbackParams } from "chrome://global/content/ml/Utils.sys.mjs"
 * @import { MLEngineParent } from "./MLEngineParent.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  BasePromiseWorker: "resource://gre/modules/PromiseWorker.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  PipelineOptions: "chrome://global/content/ml/EngineProcess.sys.mjs",
  DEFAULT_ENGINE_ID: "chrome://global/content/ml/EngineProcess.sys.mjs",
  DEFAULT_MODELS: "chrome://global/content/ml/EngineProcess.sys.mjs",
  WASM_BACKENDS: "chrome://global/content/ml/EngineProcess.sys.mjs",
  BACKENDS: "chrome://global/content/ml/EngineProcess.sys.mjs",
  console: () =>
    console.createInstance({
      maxLogLevelPref: "browser.ml.logLevel",
      prefix: "GeckoMLEngineChild",
    }),
  // Prefs:
  CACHE_TIMEOUT_MS: { pref: "browser.ml.modelCacheTimeout" },
  MODEL_HUB_ROOT_URL: { pref: "browser.ml.modelHubRootUrl" },
  MODEL_HUB_URL_TEMPLATE: { pref: "browser.ml.modelHubUrlTemplate" },
  LOG_LEVEL: { pref: "browser.ml.logLevel" },
  PIPELINE_OVERRIDE_OPTIONS: {
    pref: "browser.ml.overridePipelineOptions",
    default: "{}",
  },
  // Services
  mlUtils: { service: "@mozilla.org/ml-utils;1", iid: Ci.nsIMLUtils },
});

/**
 * Cached resolution of "best-onnx" for this inference-child process. Null
 * until the first request probes native ONNX availability (or an engine
 * creation observes it); thereafter set to the concrete backend to use.
 * See `dom/onnx/InferenceSession.cpp` for the underlying ORT load.
 *
 * @type {null | "onnx" | "onnx-native"}
 */
let gBestOnnxBackend = null;

/**
 * In-flight availability probe deduplicating concurrent cold-cache "best-onnx"
 * resolutions onto a single worker check. Mirrors the chrome-side cache in
 * `EngineProcess.sys.mjs`: reset to null on failure so a later request retries.
 *
 * @type {null | Promise<string>}
 */
let gBestOnnxBackendPromise = null;

const SAFE_OVERRIDE_OPTIONS = [
  "dtype",
  "logLevel",
  "modelRevision",
  "numThreads",
  "processorRevision",
  "timeoutMS",
  "tokenizerRevision",
];

/**
 * The engine child is responsible for the life cycle and instantiation of the local
 * machine learning inference engine.
 */
export class MLEngineChild extends JSProcessActorChild {
  /**
   * The cached engines.
   *
   * @type {Map<string, EngineDispatcher>}
   */
  #engineDispatchers = new Map();

  /**
   * Tracks that an engine is present, even if the dispatcher is not present yet.
   *
   * @type {Map<EngineId, PipelineOptions>}
   */
  #enginesPresent = new Map();

  /**
   * @param {object} message
   * @param {string} message.name
   * @param {any} message.data
   */
  // eslint-disable-next-line consistent-return
  async receiveMessage({ name, data }) {
    switch (name) {
      case "MLEngine:NewPort": {
        await this.#onNewPortCreated(data);
        break;
      }
      case "MLEngine:GetStatusByEngineId": {
        return this.getStatusByEngineId();
      }
      case "MLEngine:RequestIsNativeOnnxRuntimeAvailable": {
        return this.requestIsNativeOnnxRuntimeAvailable();
      }
      case "MLEngine:ForceShutdown": {
        for (const engineDispatcher of this.#engineDispatchers.values()) {
          await engineDispatcher.terminate(
            /* shutDownIfEmpty */ true,
            /* replacement */ false
          );
        }
        break;
      }
    }
  }

  /**
   * Handles the actions to be performed after a new port has been created.
   * Specifically, it ensures that the engine dispatcher is created if not already present,
   * and notifies the parent through the port once the engine dispatcher is ready.
   *
   * @param {object} config - Configuration object.
   * @param {MessagePort} config.port - The port of the channel.
   * @param {PipelineOptions} config.pipelineOptions - The options for the pipeline.
   * @returns {Promise<void>} - A promise that resolves once the necessary actions are complete.
   */
  async #onNewPortCreated({ port, pipelineOptions }) {
    try {
      // We get some default options from the prefs
      let options = new lazy.PipelineOptions({
        modelHubRootUrl: lazy.MODEL_HUB_ROOT_URL,
        modelHubUrlTemplate: lazy.MODEL_HUB_URL_TEMPLATE,
        timeoutMS: lazy.CACHE_TIMEOUT_MS,
        logLevel: lazy.LOG_LEVEL,
      });

      const updatedPipelineOptions =
        this.getUpdatedPipelineOptions(pipelineOptions);
      options.updateOptions(updatedPipelineOptions);
      const engineId = options.engineId;
      if (!engineId) {
        throw new Error("Expected an engineId, but none was present.");
      }
      this.#enginesPresent.set(engineId, options);

      // Check if we already have an engine under this id.
      let currentEngineDispatcher = this.#engineDispatchers.get(engineId);
      if (currentEngineDispatcher) {
        // The option matches, let's reuse the engine
        if (currentEngineDispatcher.pipelineOptions?.equals(options)) {
          port.postMessage({
            type: "EnginePort:EngineReady",
            error: null,
            resolvedBackend: currentEngineDispatcher.pipelineOptions?.backend,
          });
          return;
        }

        // The options do not match, terminate the old one so we have a single engine per id.
        await currentEngineDispatcher.terminate(
          /* shutDownIfEmpty */ false,
          /* replacement */ true
        );
        this.#engineDispatchers.delete(engineId);
      }

      const dispatcher = new EngineDispatcher(this, port, options);
      this.#engineDispatchers.set(engineId, dispatcher);

      // When the pipeline is mocked typically in unit tests, the WASM files are
      // mocked.  In these cases, the pipeline is not resolved during
      // initialization to allow the test to work.
      //
      // NOTE: This is done after adding to #engineDispatchers to ensure other
      // async calls see the new dispatcher.
      if (!lazy.PipelineOptions.isMocked(pipelineOptions)) {
        await dispatcher.isReady();
      }

      port.postMessage({
        type: "EnginePort:EngineReady",
        error: null,
        // The parent's requested backend may resolve to a more specific
        // one, e.g. "best-onnx" will resolve to "onnx" or "onnx-native".
        resolvedBackend: dispatcher.pipelineOptions?.backend,
      });
    } catch (error) {
      port.postMessage({
        type: "EnginePort:EngineReady",
        error,
      });
    }
  }

  /**
   * Gets the wasm array buffer from RemoteSettings.
   *
   * @param {?string} backend - The ML engine for which the WASM buffer is requested.
   * @returns {Promise<ArrayBuffer>}
   */
  getWasmArrayBuffer(backend) {
    return this.sendQuery("MLEngine:GetWasmArrayBuffer", backend);
  }

  /**
   * Gets the configuration of the worker
   *
   * @returns {Promise<ReturnType<typeof MLEngineParent.getWorkerConfig>>}
   */
  getWorkerConfig() {
    return this.sendQuery("MLEngine:GetWorkerConfig");
  }

  /**
   * Resolves to true if the native ONNX runtime is available, otherwise false.
   *
   * @returns {Promise<boolean>}
   */
  async requestIsNativeOnnxRuntimeAvailable() {
    const workerConfig = await this.getWorkerConfig();
    const worker = new lazy.BasePromiseWorker(
      workerConfig.url,
      workerConfig.options
    );

    try {
      return await worker.post("isNativeOnnxRuntimeAvailable", []);
    } finally {
      worker.terminate();
    }
  }

  /**
   * Resolves a requested backend to a concrete backend identifier. "best-onnx"
   * returns the cached choice if one exists, otherwise probes native ONNX
   * availability once per process (deduped via an in-flight promise) and caches
   * onnx-native or onnx accordingly. If the probe itself fails we leave the
   * cache unset and fall back to optimistically trying onnx-native, letting the
   * engine-creation try/catch resolve the real outcome. Any other value is
   * already concrete.
   *
   * @param {string} backend - Requested backend or "best-onnx".
   * @returns {Promise<string>} Resolved backend identifier.
   */
  async chooseBestBackend(backend) {
    if (backend !== lazy.BACKENDS.bestOnnx) {
      return backend;
    }
    if (gBestOnnxBackend) {
      return gBestOnnxBackend;
    }
    if (!gBestOnnxBackendPromise) {
      gBestOnnxBackendPromise = this.#resolveBestOnnxBackend();
    }
    const promise = gBestOnnxBackendPromise;
    return promise.catch(() => {
      if (gBestOnnxBackendPromise === promise) {
        gBestOnnxBackendPromise = null;
      }
      return lazy.BACKENDS.onnxNative;
    });
  }

  /**
   * Runs the availability probe and records the result in `gBestOnnxBackend`.
   *
   * @returns {Promise<string>}
   */
  async #resolveBestOnnxBackend() {
    const available = await this.requestIsNativeOnnxRuntimeAvailable();
    gBestOnnxBackend = available
      ? lazy.BACKENDS.onnxNative
      : lazy.BACKENDS.onnx;
    return gBestOnnxBackend;
  }

  /**
   * Gets the inference options from RemoteSettings.
   *
   * @param {string | null} featureId
   * @param {string | null} taskName
   * @param {string | null} modelId
   * @returns {Promise<object>}
   */
  getInferenceOptions(featureId, taskName, modelId) {
    return this.sendQuery("MLEngine:GetInferenceOptions", {
      featureId,
      taskName,
      modelId,
    });
  }

  /**
   * Retrieves a model file and headers by communicating with the parent actor.
   *
   * @param {object} config - The configuration accepted by the parent function.
   * @returns {Promise<[string, object]>} The file local path and headers
   */
  getModelFile(config) {
    return this.sendQuery("MLEngine:GetModelFile", config);
  }

  /**
   * Notify that the model download is completed by communicating with the parent actor.
   *
   * @param {object} config - The configuration accepted by the parent function.
   */
  async notifyModelDownloadComplete(config) {
    this.sendQuery("MLEngine:NotifyModelDownloadComplete", config);
  }

  /**
   * Removes an engine by its ID. Optionally shuts down if no engines remain.
   *
   * @param {string} engineId - The ID of the engine to remove.
   * @param {boolean} shutDownIfEmpty - If true, shuts down the engine process if no engines remain.
   * @param {boolean} replacement - Flag indicating whether the engine is being replaced.
   */
  async removeEngine(engineId, shutDownIfEmpty, replacement) {
    this.#engineDispatchers.delete(engineId);
    this.#enginesPresent.delete(engineId);

    try {
      await this.sendQuery("MLEngine:Removed", {
        engineId,
        shutdown: shutDownIfEmpty,
        replacement,
      });
    } catch (error) {
      lazy.console.error("Failed to send MLEngine:Removed", error);
    }

    if (this.#engineDispatchers.size === 0 && shutDownIfEmpty) {
      try {
        await this.sendQuery("MLEngine:DestroyEngineProcess");
      } catch (error) {
        lazy.console.error(
          "Failed to send MLEngine:DestroyEngineProcess",
          error
        );
      }
    }
  }

  /**
   * Collects information about the current status.
   *
   * @returns {StatusByEngineId}
   */
  getStatusByEngineId() {
    /** @type {StatusByEngineId} */
    const statusMap = new Map();

    for (let [engineId, options] of this.#enginesPresent) {
      const dispatcher = this.#engineDispatchers.get(engineId);
      let status = dispatcher?.getStatus();
      if (!status) {
        // This engine doesn't have a dispatcher yet.
        status = {
          status: "SHUTTING_DOWN_PREVIOUS_ENGINE",
          options,
        };
      }
      statusMap.set(engineId, status);
    }
    return statusMap;
  }

  /**
   * @param {PipelineOptions} pipelineOptions - options that we want to safely override
   * @returns {PipelineOptionsRaw} - updated pipeline options
   */
  getUpdatedPipelineOptions(pipelineOptions) {
    const overrideOptionsByFeature = JSON.parse(lazy.PIPELINE_OVERRIDE_OPTIONS);
    /** @type {any} - This is hard to type check. */
    const overrideOptions = {};
    const { featureId } = pipelineOptions;
    if (
      featureId &&
      overrideOptionsByFeature.hasOwnProperty(pipelineOptions.featureId)
    ) {
      for (let key of Object.keys(overrideOptionsByFeature[featureId])) {
        if (SAFE_OVERRIDE_OPTIONS.includes(key)) {
          overrideOptions[key] = overrideOptionsByFeature[featureId][key];
        }
      }
    }
    return { ...pipelineOptions, ...overrideOptions };
  }
}

/**
 * This classes manages the lifecycle of an ML Engine, and handles dispatching messages
 * to it.
 */
class EngineDispatcher {
  /** @type {MessagePort | null} */
  #port = null;

  /** @type {number | null} */
  #keepAliveTimeout = null;

  /** @type {Promise<InferenceEngine>} */
  #engine;

  /** @type {?string} */
  #taskName;

  /** @type {?string} */
  #featureId;

  /** @type {string} */
  #engineId;

  /** @type {PipelineOptions | null} */
  pipelineOptions = null;

  /** @type {EngineStatus} */
  #status;

  /**
   * Creates the inference engine given the wasm runtime and the run options.
   *
   * The initialization is done in three steps:
   * 1. The wasm runtime is fetched from RS
   * 2. The inference options are fetched from RS and augmented with the pipeline options.
   * 3. The inference engine is created with the wasm runtime and the options.
   *
   * Any exception here will be bubbled up for the constructor to log.
   *
   * @param {PipelineOptions} pipelineOptions
   * @param {?function(ProgressAndStatusCallbackParams):void} notificationsCallback The callback to call for updating about notifications such as dowload progress status.
   * @returns {Promise<InferenceEngine>}
   */
  async initializeInferenceEngine(pipelineOptions, notificationsCallback) {
    let remoteSettingsOptions = await this.mlEngineChild.getInferenceOptions(
      this.#featureId,
      this.#taskName,
      pipelineOptions.modelId ?? null
    );

    // Merge the RemoteSettings inference options with the pipeline options provided.
    let mergedOptions = new lazy.PipelineOptions(remoteSettingsOptions);
    mergedOptions.updateOptions(pipelineOptions);

    // If the merged options don't have a modelId and we have a default modelId, we set it
    if (!mergedOptions.modelId) {
      const defaultModelEntry = this.#taskName
        ? lazy.DEFAULT_MODELS[this.#taskName]
        : null;

      if (defaultModelEntry) {
        lazy.console.debug(
          `Using default model ${defaultModelEntry.modelId} for task ${this.#taskName}`
        );
        mergedOptions.updateOptions(defaultModelEntry);
      } else {
        throw new Error(`No default model found for task ${this.#taskName}`);
      }
    }

    lazy.console.debug("Inference engine options:", mergedOptions);
    this.pipelineOptions = mergedOptions;

    const requestedBackend = pipelineOptions.backend;
    this.pipelineOptions.backend =
      await this.mlEngineChild.chooseBestBackend(requestedBackend);

    // Retrigger validation
    this.pipelineOptions = new lazy.PipelineOptions(this.pipelineOptions);

    const workerConfig = await this.mlEngineChild.getWorkerConfig();

    const tryCreate = async () => {
      const opts = /** @type {PipelineOptions} */ (this.pipelineOptions);
      let wasm = null;
      if (lazy.WASM_BACKENDS.includes(opts.backend || lazy.BACKENDS.onnx)) {
        wasm = await this.mlEngineChild.getWasmArrayBuffer(opts.backend);
      }
      return InferenceEngine.create({
        workerUrl: workerConfig.url,
        workerOptions: workerConfig.options,
        wasm,
        pipelineOptions: opts,
        notificationsCallback,
        getModelFileFn: this.mlEngineChild.getModelFile.bind(
          this.mlEngineChild
        ),
        notifyModelDownloadCompleteFn:
          this.mlEngineChild.notifyModelDownloadComplete.bind(
            this.mlEngineChild
          ),
      });
    };

    const triedNativeForBestOnnx =
      requestedBackend === lazy.BACKENDS.bestOnnx &&
      this.pipelineOptions.backend === lazy.BACKENDS.onnxNative;

    try {
      const engine = await tryCreate();
      if (triedNativeForBestOnnx) {
        gBestOnnxBackend = lazy.BACKENDS.onnxNative;
      }
      return engine;
    } catch (error) {
      // best-onnx promised the caller a working onnx engine. If the native
      // attempt failed, fall back to the wasm onnx backend. The
      // NotSupportedError raised from dom/onnx/InferenceSession.cpp gets
      // wrapped at Pipeline.mjs:87, which strips the original .name, so
      // match on the message text from the C++ source instead. On that
      // match we cache the wasm choice; on any other failure we still
      // retry once with wasm but leave the cache alone.
      if (!triedNativeForBestOnnx) {
        throw error;
      }
      // KEEP IN SYNC: dom/onnx/InferenceSession.cpp raises this exact
      // string via MaybeRejectWithNotSupportedError when libonnxruntime
      // is missing. We match on message text because the BackendError
      // wrapper at Pipeline.mjs:87 strips the original error name.
      const isOrtUnavailable =
        /onnxruntime shared library could not be loaded/.test(
          error?.message ?? ""
        );
      if (isOrtUnavailable) {
        gBestOnnxBackend = lazy.BACKENDS.onnx;
        lazy.console.warn(
          "Native onnx runtime not available; falling back to wasm onnx backend."
        );
      } else {
        lazy.console.warn(
          "onnx-native engine creation failed; retrying with wasm onnx backend.",
          error
        );
      }
      this.pipelineOptions.backend = lazy.BACKENDS.onnx;
      this.pipelineOptions = new lazy.PipelineOptions(this.pipelineOptions);
      return tryCreate();
    }
  }

  /**
   * Private Constructor for an Engine Dispatcher.
   *
   * @param {MLEngineChild} mlEngineChild
   * @param {MessagePort} port
   * @param {PipelineOptions} pipelineOptions
   */
  constructor(mlEngineChild, port, pipelineOptions) {
    this.#status = "INITIALIZING";
    /** @type {MLEngineChild} */
    this.mlEngineChild = mlEngineChild;
    const { featureId, taskName, timeoutMS, engineId } = pipelineOptions;
    if (typeof timeoutMS != "number") {
      throw new Error("Expected a timeoutMS");
    }
    if (!engineId) {
      throw new Error("Expected an engineId");
    }
    this.#featureId = featureId;
    this.#taskName = taskName;
    this.timeoutMS = timeoutMS;
    this.#engineId = engineId;

    this.#engine = this.initializeInferenceEngine(
      pipelineOptions,
      notificationsData => {
        this.handleInitProgressStatus(port, notificationsData);
      }
    );

    this.#engine
      .then(() => {
        this.#status = "IDLE";
        // Trigger the keep alive timer.
        void this.keepAlive();
      })
      .catch(error => {
        if (
          // Ignore errors from tests intentionally causing errors.
          !error?.message?.startsWith("Intentionally")
        ) {
          lazy.console.error("Could not initialize the engine", error);
        }
      });

    this.#setupMessageHandler(port);
  }

  /**
   * Returns the status of the engine
   */
  getStatus() {
    return {
      status: this.#status,
      options: this.pipelineOptions,
    };
  }

  /**
   * @param {MessagePort} port
   * @param {ProgressAndStatusCallbackParams} notificationsData
   */
  handleInitProgressStatus(port, notificationsData) {
    port.postMessage({
      type: "EnginePort:InitProgress",
      statusResponse: notificationsData,
    });
  }

  /**
   * The worker will be shutdown automatically after some amount of time of not being used, unless:
   *
   * - timeoutMS is set to -1
   */
  keepAlive() {
    if (this.#keepAliveTimeout) {
      // Clear any previous timeout.
      lazy.clearTimeout(this.#keepAliveTimeout);
    }
    if (this.timeoutMS && this.timeoutMS >= 0) {
      this.#keepAliveTimeout = lazy.setTimeout(
        this.terminate.bind(
          this,
          /* shutDownIfEmpty */ true,
          /* replacement */ false
        ),
        this.timeoutMS
      );
    } else {
      this.#keepAliveTimeout = null;
    }
  }

  /**
   * Wait for the engine to be ready.
   */
  async isReady() {
    await this.#engine;
  }

  /**
   * @param {MessagePort} port
   */
  #setupMessageHandler(port) {
    this.#port = port;
    port.onmessage = async event => {
      const { data } = /** @type {any} */ (event);

      switch (data.type) {
        case "EnginePort:Discard": {
          port.close();
          this.#port = null;
          break;
        }
        case "EnginePort:Terminate": {
          await this.terminate(data.shutdown, data.replacement);
          break;
        }
        case "EnginePort:Run": {
          const resourcesBefore = {
            cpuTime: ChromeUtils.cpuTimeSinceProcessStart,
            memory: ChromeUtils.currentProcessMemoryUsage,
          };

          const { requestId, request, engineRunOptions } = data;
          try {
            await this.isReady();
          } catch (error) {
            port.postMessage({
              type: "EnginePort:RunResponse",
              requestId,
              response: null,
              error,
            });
            // The engine failed to load. Terminate the entire dispatcher.
            await this.terminate(
              /* shutDownIfEmpty */ true,
              /* replacement */ false
            );
            return;
          }

          // Do not run the keepAlive timer until we are certain that the engine loaded,
          // as the engine shouldn't be killed while it is initializing.
          this.keepAlive();

          this.#status = "RUNNING";
          const engine = await this.#engine;

          try {
            const response = await engine.run(
              request,
              requestId,
              engineRunOptions
            );

            const resourcesAfter = {
              cpuTime: ChromeUtils.cpuTimeSinceProcessStart,
              memory: ChromeUtils.currentProcessMemoryUsage,
            };

            port.postMessage({
              type: "EnginePort:RunResponse",
              requestId,
              response,
              error: null,
              resourcesBefore,
              resourcesAfter,
            });
          } catch (error) {
            port.postMessage({
              type: "EnginePort:RunResponse",
              requestId,
              response: null,
              error,
            });
          }
          this.#status = "IDLE";
          break;
        }
        default:
          lazy.console.error("Unknown port message to engine: ", data);
          break;
      }
    };
  }

  /**
   * Terminates the engine and its worker after a timeout.
   *
   * @param {boolean} shutDownIfEmpty - If true, shuts down the engine process if no engines remain.
   * @param {boolean} replacement - Flag indicating whether the engine is being replaced.
   */
  async terminate(shutDownIfEmpty, replacement) {
    if (this.#keepAliveTimeout) {
      lazy.clearTimeout(this.#keepAliveTimeout);
      this.#keepAliveTimeout = null;
    }
    if (this.#port) {
      // This call will trigger back an EnginePort:Discard that will close the port
      this.#port.postMessage({ type: "EnginePort:EngineTerminated" });
    }

    this.#status = "TERMINATING";
    try {
      const engine = await this.#engine;
      await engine.terminate();
    } catch (error) {
      lazy.console.error("Failed to get the engine", error);
    }
    this.#status = "TERMINATED";

    await this.mlEngineChild.removeEngine(
      this.#engineId,
      shutDownIfEmpty,
      replacement
    );
  }
}

/**
 * Wrapper for a function that fetches a model file from a specified URL and task name.
 *
 * @param {object} config
 * @param {string | null | undefined} config.engineId - The engine id - defaults to "default-engine".
 * @param {string | null | undefined} config.taskName - name of the inference task.
 * @param {string | null | undefined} config.url - The URL of the model file to fetch. Can be a path relative to
 * the model hub root or an absolute URL.
 * @param {string | null | undefined} config.modelHubRootUrl - root url of the model hub. When not provided, uses the default from prefs.
 * @param {string | null | undefined} config.modelHubUrlTemplate - url template of the model hub. When not provided, uses the default from prefs.
 * @param {function(object):Promise<[string, object]>} config.getModelFileFn - A function that actually retrieves the model and headers.
 * @param {string | null | undefined} config.featureId - The feature id
 * @param {string} config.sessionId - Shared across the same session.
 * @param {object} config.telemetryData - Additional telemetry data.
 * @returns {Promise<BasePromiseWorker.Meta>} A promise that resolves to a Meta object containing the URL, response headers,
 * and model path.
 */
async function getModelFile({
  engineId,
  taskName,
  url,
  getModelFileFn,
  modelHubRootUrl,
  modelHubUrlTemplate,
  featureId,
  sessionId,
  telemetryData,
}) {
  const [data, headers] = await getModelFileFn({
    engineId: engineId || lazy.DEFAULT_ENGINE_ID,
    taskName,
    url,
    rootUrl: modelHubRootUrl || lazy.MODEL_HUB_ROOT_URL,
    urlTemplate: modelHubUrlTemplate || lazy.MODEL_HUB_URL_TEMPLATE,
    featureId,
    sessionId,
    telemetryData,
  });
  return new lazy.BasePromiseWorker.Meta([url, headers, data], {});
}

/**
 * Wrapper around the ChromeWorker that runs the inference.
 */
class InferenceEngine {
  /** @type {?BasePromiseWorker} */
  #worker;

  /**
   * Initialize the worker.
   *
   * @param {object} config
   * @param {string} config.workerUrl  The url of the worker
   * @param {object} config.workerOptions the options to pass to BasePromiseWorker
   * @param {?ArrayBuffer} config.wasm
   * @param {PipelineOptions} config.pipelineOptions
   * @param {?function(ProgressAndStatusCallbackParams):void} config.notificationsCallback The callback to call for updating about notifications such as dowload progress status.
   * @param {function(object):Promise<[string, object]>} config.getModelFileFn - A function that actually retrieves the model and headers.
   * @param {function(object):Promise<void>} config.notifyModelDownloadCompleteFn - A function to notify that all files needing downloads are completed.
   * @returns {Promise<InferenceEngine>}
   */
  static async create({
    workerUrl,
    workerOptions,
    wasm,
    pipelineOptions,
    notificationsCallback,
    getModelFileFn,
    notifyModelDownloadCompleteFn,
  }) {
    // Check for the numThreads value. If it's not set, use the best value for the platform, which is the number of physical cores
    pipelineOptions.numThreads =
      pipelineOptions.numThreads || lazy.mlUtils.getOptimalCPUConcurrency();

    /** @type {Record<string, Function>} */
    const functions = {
      /**
       * @param {object} options
       * @param {string} [options.url]
       * @param {string} [options.sessionId]
       */
      getModelFile: async ({ url, sessionId = "" } = {}) =>
        getModelFile({
          engineId: pipelineOptions.engineId,
          url,
          taskName: pipelineOptions.taskName,
          getModelFileFn,
          modelHubRootUrl: pipelineOptions.modelHubRootUrl,
          modelHubUrlTemplate: pipelineOptions.modelHubUrlTemplate,
          featureId: pipelineOptions.featureId,
          sessionId,
          // We have model, revision that are parsed for the url.
          // However, we want to save in telemetry the ones that are configured
          // for the pipeline. This allows consistent reporting regarding of how
          // the backend constructs the url.
          telemetryData: {
            modelId: pipelineOptions.modelId,
            modelRevision: pipelineOptions.modelRevision,
          },
        }),
      onInferenceProgress: notificationsCallback ?? (() => {}),
      notifyModelDownloadComplete: async (sessionId = "") =>
        notifyModelDownloadCompleteFn({
          sessionId,
          featureId: pipelineOptions.featureId,
          engineId: pipelineOptions.engineId,
          modelId: pipelineOptions.modelId,
          modelRevision: pipelineOptions.modelRevision,
        }),
    };

    /** @type {BasePromiseWorker} */
    const worker = new lazy.BasePromiseWorker(
      workerUrl,
      workerOptions,
      functions
    );

    const startTime = ChromeUtils.now();
    const args = [wasm, pipelineOptions];
    const closure = {};
    const transferables = wasm instanceof ArrayBuffer ? [wasm] : [];
    await worker.post("initializeEngine", args, closure, transferables);
    ChromeUtils.addProfilerMarker(
      "MLEngineChild",
      { startTime },
      `Initialize engine`
    );
    return new InferenceEngine(worker);
  }

  /**
   * @param {BasePromiseWorker} worker
   */
  constructor(worker) {
    this.#worker = worker;
  }

  /**
   * @param {string} request
   * @param {string} requestId - The identifier used to internally track this request.
   * @param {object} engineRunOptions - Additional run options for the engine.
   * @param {boolean} engineRunOptions.enableInferenceProgress - Whether to enable inference progress.
   * @returns {Promise<string>}
   */
  run(request, requestId, engineRunOptions) {
    if (!this.#worker) {
      throw new Error(
        "Attempting to call InferenceEngine#run after the worker was shut down."
      );
    }
    return this.#worker.post("run", [request, requestId, engineRunOptions]);
  }

  async terminate() {
    if (this.#worker) {
      this.#worker.terminate();
      this.#worker = null;
    }
  }
}
