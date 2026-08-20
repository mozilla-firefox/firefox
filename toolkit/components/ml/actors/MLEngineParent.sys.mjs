/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { MLTelemetry } from "chrome://global/content/ml/MLTelemetry.sys.mjs";

/**
 * @import { MLEngineChild } from "./MLEngineChild.sys.mjs"
 * @import { RemoteSettingsClient } from "resource://services-settings/RemoteSettingsClient.sys.mjs"
 * @import {
 *   ChunkResponse,
 *   EngineFeatureIds,
 *   EngineResponses,
 *   EngineRequests,
 *   ParsedModelHubUrl,
 *   RecordsML,
 *   RemoteSettingsInferenceOptions,
 *   StatusByEngineId,
 *   SyncEvent,
 * } from "../ml.d.ts"
 * @import { WasmRecord } from "../../translations/translations.d.ts"
 * @import { ModelHub } from "chrome://global/content/ml/ModelHub.sys.mjs"
 * @import { ProgressAndStatusCallbackParams } from "chrome://global/content/ml/Utils.sys.mjs"
 * @import { PipelineOptions } from "chrome://global/content/ml/EngineProcess.sys.mjs";
 */

const lazy = XPCOMUtils.declareLazy({
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  Utils: "resource://services-settings/Utils.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  ModelHub: "chrome://global/content/ml/ModelHub.sys.mjs",
  Progress: "chrome://global/content/ml/Utils.sys.mjs",
  OPFS: "chrome://global/content/ml/OPFS.sys.mjs",
  BACKENDS: "chrome://global/content/ml/EngineProcess.sys.mjs",
  stringifyForLog: "chrome://global/content/ml/Utils.sys.mjs",
  console: () =>
    console.createInstance({
      maxLogLevelPref: "browser.ml.logLevel",
      prefix: "GeckoMLEngineParent",
    }),
  mlUtils: { service: "@mozilla.org/ml-utils;1", iid: Ci.nsIMLUtils },
  CHECK_FOR_MEMORY: { pref: "browser.ml.checkForMemory" },
  MINIMUM_PHYSICAL_MEMORY: { pref: "browser.ml.minimumPhysicalMemory" },
});

const ONE_GiB = 1024 * 1024 * 1024;
const RS_RUNTIME_COLLECTION = "ml-onnx-runtime";
const RS_INFERENCE_OPTIONS_COLLECTION = "ml-inference-options";
const RS_ALLOW_DENY_COLLECTION = "ml-model-allow-deny-list";
const TERMINATE_TIMEOUT = 5000;

const RUNTIME_ROOT_IN_OPFS = "mlRuntimeFiles";

/**
 * Custom error class for handling insufficient memory errors.
 *
 * @augments Error
 */
export class NotEnoughMemoryError extends Error {
  /**
   * Creates an instance of NotEnoughMemoryError.
   *
   * @param {object} options - The error options.
   * @param {string} [options.message="Not enough physical memory available"] - The error message.
   * @param {number} options.requiredMemory - The amount of memory required in bytes.
   * @param {number} options.availableMemory - The amount of available memory in bytes.
   */
  constructor({
    message = "Not enough physical memory available",
    requiredMemory,
    availableMemory,
  }) {
    super(message);
    this.name = "NotEnoughMemoryError";
    this.requiredMemory = requiredMemory;
    this.availableMemory = availableMemory;
    // TypeScript doesn't support this yet.
    // @ts-expect-error - Property 'captureStackTrace' does not exist on type 'ErrorConstructor'.ts(2339)
    Error.captureStackTrace(this, this.constructor);
  }

  /**
   * Returns a formatted string with details about the memory issue.
   *
   * @returns {string} A string describing the required and available memory in GiB.
   */
  getDetails() {
    return `Required Memory: ${(this.requiredMemory / ONE_GiB).toFixed(2)} GiB, Available Memory: ${(this.availableMemory / ONE_GiB).toFixed(2)} GiB`;
  }
}

/**
 * The ML engine is in its own content process. This actor handles the
 * marshalling of the data such as the engine payload.
 */
export class MLEngineParent extends JSProcessActorParent {
  /**
   * The RemoteSettingsClient that downloads the wasm binaries.
   *
   * @type {Record<string, RemoteSettingsClient>}
   */
  static #remoteClients = {};

  /** @type {Record<string, Promise<WasmRecord> | null>} */
  static #wasmRecord = {};

  /**
   * Locks to prevent race conditions when creating engines.
   *
   * @type {Map<string, Promise<void>>}
   */
  static engineLocks = new Map();

  /**
   * AbortSignal to potentially cancel the engine creation.
   *
   * @type {Map<string, ?AbortSignal>}
   */
  static engineCreationAbortSignal = new Map();

  /**
   * AbortControllers used to cancel ongoing operations initiated by the engine worker
   * but not owned by it (e.g. operations executed in the parent process such as
   * model downloads). This ensures that when the engine is terminated, any related
   * in-flight work started on its behalf can also be aborted.
   *
   * Key: engineId
   *
   * @type {Map<string, AbortController>}
   */
  static engineCreationAbortControllers = new Map();

  /**
   * The ChildID gets set by EngineProcess.sys.mjs
   *
   * @type {null | number}
   */
  childID = null;

  /**
   * The following constant controls the major and minor version for onnx wasm downloaded from
   * Remote Settings.
   *
   * When a breaking change is introduced, increment this value and add a corresponding version
   *
   * onnx:
   * - 1 => Transformers 2.x
   * - 2 => Transformers < 3.1
   * - 3 => Transformers < 3.4
   * - 4 => Transformers >= 3.4.0
   * - 5 => Transformers >= 3.5.1
   *
   * @type {Record<string, number>}
   */
  static WASM_MAJOR_VERSION = {
    [lazy.BACKENDS.onnx]: 5,
  };

  /**
   * This wasm file supports CPU, WebGPU and WebNN.
   *
   * Since SIMD is supported by all major JavaScript engines, non-SIMD build is no longer provided.
   * We also serve the threaded build since we can simply set numThreads to 1 to disable multi-threading.
   *
   * @type {Record<string, string>}
   */
  static WASM_FILENAME = {
    [lazy.BACKENDS.onnx]: "ort-wasm-simd-threaded.jsep.wasm",
  };

  /**
   * This default backend to use when none is specified.
   */
  static DEFAULT_BACKEND = lazy.BACKENDS.onnx;

  /**
   * The modelhub used to retrieve files.
   *
   * @type {ModelHub | null}
   */
  modelHub = null;

  /**
   * Tracks the most recent revision for each task and model pair that are marked for deletion.
   * Keys are task names and model names. Values contain their respective revisions.
   *
   * @type {Map<string, ParsedModelHubUrl & { taskName: string }>}
   */
  #modelFilesInUse = new Map();

  /**
   * The callback to call for updating about notifications such as dowload progress status.
   *
   * @type {?function(ProgressAndStatusCallbackParams):void}
   */
  notificationsCallback = null;

  /**
   * Set by EngineProcess when creating the MLEngineParent.
   * Keeps the "inference" process alive until it is cleared.
   *
   * NOTE: Invalidating this keepAlive does not guarantee that the process will
   * exit, and this actor may be re-used if it does not (e.g. because the
   * inference process was kept alive by TranslationsEngine).
   *
   * @type {nsIContentParentKeepAlive | null}
   */
  processKeepAlive = null;

  /**
   * Remote settings isn't available in tests, so provide mocked responses.
   *
   * @param {Record<string, RemoteSettingsClient>} remoteClients
   */
  static mockRemoteSettings(remoteClients) {
    lazy.console.log("Mocking remote settings in MLEngineParent.");
    MLEngineParent.#remoteClients = remoteClients;
    MLEngineParent.#wasmRecord = {};
  }

  /**
   * Remove anything that could have been mocked.
   */
  static removeMocks() {
    lazy.console.log("Removing mocked remote client in MLEngineParent.");
    MLEngineParent.#remoteClients = {};
    MLEngineParent.#wasmRecord = {};
  }

  /**
   * Creates a new MLEngine.
   *
   * @template {EngineFeatureIds} FeatureId
   *
   * If there's an existing engine with the same pipelineOptions, it will be reused.
   *
   * @param {object} params Parameters object.
   * @param {PipelineOptions} params.pipelineOptions
   * @param {?function(ProgressAndStatusCallbackParams):void} params.notificationsCallback A function to call to indicate progress status.
   * @param {?AbortSignal} params.abortSignal - AbortSignal to cancel the download.
   * @returns {Promise<MLEngine<FeatureId>>}
   */
  async getEngine({ pipelineOptions, notificationsCallback, abortSignal }) {
    if (
      lazy.CHECK_FOR_MEMORY &&
      lazy.mlUtils.totalPhysicalMemory < lazy.MINIMUM_PHYSICAL_MEMORY * ONE_GiB
    ) {
      throw new NotEnoughMemoryError({
        availableMemory: lazy.mlUtils.totalPhysicalMemory,
        requiredMemory: lazy.MINIMUM_PHYSICAL_MEMORY * ONE_GiB,
      });
    }

    const { featureId, engineId } = pipelineOptions;
    if (!engineId) {
      throw new Error("Expected to receive an engineId in the PipelineOptions");
    }

    // Allow notifications callback changes even when reusing engine.
    this.notificationsCallback = notificationsCallback;

    if (MLEngineParent.engineLocks.has(engineId)) {
      // Wait for the existing lock to resolve
      await MLEngineParent.engineLocks.get(engineId);
    }
    /** @type {PromiseWithResolvers<void>} */
    const { promise: lockPromise, resolve: resolveLock } =
      Promise.withResolvers();
    MLEngineParent.engineLocks.set(engineId, lockPromise);

    /** @type {?AbortController} */
    // Parent-owned controller used to cancel engine-related operations
    // (e.g. model downloads) when the engine is terminated.
    let abortController = null;

    try {
      const currentEngine = MLEngine.getInstance(engineId);
      if (currentEngine) {
        if (
          currentEngine.pipelineOptions.equals(pipelineOptions) &&
          currentEngine.engineStatus === "ready"
        ) {
          lazy.console.debug(`Reusing existing engine for ${engineId}`);
          // Coerce the return type since we can't key off of the engineId here.
          return /** @type {MLEngine<FeatureId>} */ (currentEngine);
        }
        lazy.console.debug(`Replacing existing engine for ${engineId}`);
        if (currentEngine.isObserving) {
          try {
            Services.obs.removeObserver(currentEngine, "ipc:content-shutdown");
            currentEngine.isObserving = false;
          } catch (e) {
            lazy.console.error("Failed to remove observer", e);
          }
        }

        await MLEngine.removeInstance(
          engineId,
          /* shutdown */ false,
          /* replacement*/ true
        );
      }

      const start = ChromeUtils.now();

      // Parent-owned controller used to cancel engine-related operations
      // (e.g. model downloads) if the engine is terminated during creation.
      abortController = new AbortController();

      // Allow cancellation from either the parent or an optional caller-provided signal.
      const signals = [abortController.signal, abortSignal].filter(s =>
        AbortSignal.isInstance(s)
      );

      // Signal passed to operations initiated by the engine.
      MLEngineParent.engineCreationAbortSignal.set(
        engineId,
        AbortSignal.any(signals)
      );

      // Keep the controller so the parent can trigger cancellation.
      MLEngineParent.engineCreationAbortControllers.set(
        engineId,
        abortController
      );

      /** @type {MLEngine<any>} */
      const engine = await MLEngine.initialize({
        mlEngineParent: this,
        pipelineOptions,
        notificationsCallback,
      });

      // Observe ipc:content-shutdown to detect inference process crashes.
      // Use a weak reference so this observer doesn't root the engine
      // and its pending request promises, which may prevent windows
      // that triggered inference from being cycle-collected.
      Services.obs.addObserver(engine, "ipc:content-shutdown", true);
      engine.isObserving = true;

      const creationTime = ChromeUtils.now() - start;

      engine.telemetry.recordEngineCreationSuccessFlow({
        engineId,
        duration: creationTime,
      });

      // TODO - What happens if the engine is already killed here?
      return engine;
    } catch (error) {
      // Abort any pending operations as the engine creating failed
      abortController?.abort();
      const { modelId, taskName, flowId } = pipelineOptions;
      const telemetry = new MLTelemetry({ featureId, flowId });
      telemetry.recordEngineCreationFailure({
        modelId,
        featureId,
        taskName,
        engineId,
        error,
      });

      throw error;
    } finally {
      MLEngineParent.engineLocks.delete(engineId);
      resolveLock();
    }
  }

  /**
   * Validates a taskName
   *
   * Throws an exception if the task name is invalid.
   *
   * @param {string} taskName
   */
  checkTaskName(taskName) {
    // Define a regular expression to verify taskName pattern (alphanumeric and underscores/dashes)
    const validTaskNamePattern = /^[a-zA-Z0-9_\-]+$/;

    // Check if taskName matches the pattern
    if (!validTaskNamePattern.test(taskName)) {
      // Handle invalid taskName, e.g., throw an error or return null
      throw new Error(
        "Invalid task name. Task name should contain only alphanumeric characters and underscores/dashes."
      );
    }
  }

  /**
   * @see {MLEngineChild#sendQuery} for the message shapes.
   * @param {any} message
   */
  // eslint-disable-next-line consistent-return
  async receiveMessage(message) {
    switch (message.name) {
      case "MLEngine:GetWasmArrayBuffer":
        return MLEngineParent.getWasmArrayBuffer(message.data);

      case "MLEngine:GetModelFile":
        return this.getModelFile(message.data);

      case "MLEngine:NotifyModelDownloadComplete":
        return this.notifyModelDownloadComplete(message.data);

      case "MLEngine:GetWorkerConfig":
        return MLEngineParent.getWorkerConfig();

      case "MLEngine:DestroyEngineProcess":
        if (this.processKeepAlive) {
          ChromeUtils.addProfilerMarker(
            "EngineProcess",
            {},
            `Dropping MLEngine "inference" process keep-alive`
          );
          this.processKeepAlive.invalidateKeepAlive();
          this.processKeepAlive = null;
        }
        return null;
      case "MLEngine:GetInferenceOptions":
        this.checkTaskName(message.json.taskName);
        return MLEngineParent.getInferenceOptions(
          message.json.featureId,
          message.json.taskName,
          message.json.modelId
        );
      case "MLEngine:Removed":
        if (!message.json.replacement) {
          try {
            // when receiving this message from the child, we know it's not a replacement.
            await MLEngine.removeInstance(
              message.json.engineId,
              message.json.shutdown,
              /* replacement */ false
            );
          } catch (e) {
            lazy.console.error("Failed to remove instance", e);
          }
        }
        return null;
    }
  }

  /**
   * Deletes all previous revisions for the current task and model used by the engine.
   *
   * @returns {Promise<void>}
   */
  async deletePreviousModelRevisions() {
    if (this.modelHub === null) {
      lazy.console.debug(
        "Ignored attempt to delete previous models when the engine is not fully initialized."
      );
      return;
    }
    const modelHub = this.modelHub;
    await Promise.all(
      [...this.#modelFilesInUse].map(async ([key, entry]) => {
        await modelHub.deleteNonMatchingModelRevisions({
          taskName: entry.taskName,
          modelWithHostname: entry.modelWithHostname,
          targetRevision: entry.revision,
        });
        this.#modelFilesInUse.delete(key);
      })
    );
  }

  /**
   * Retrieves a model file from the specified URL.
   * This function normalizes the URL, extracts the organization, model name, and file path,
   * then fetches the model file using the ModelHub API. The `modelHub` instance is created
   * only once and reused for subsequent calls to optimize performance.
   *
   * @param {object} config
   * @param {string} config.engineId - The engine id.
   * @param {string} config.taskName - name of the inference task.
   * @param {string} config.url - The URL of the model file to fetch. Can be a path relative to
   * the model hub root or an absolute URL.
   * @param {string} config.rootUrl - The URL of the model file to fetch. Can be a path relative to
   * the model hub root or an absolute URL.
   * @param {string} config.urlTemplate - The URL of the model file to fetch. Can be a path relative to
   * the model hub root or an absolute URL.
   * @param {string} config.featureId - The engine id.
   * @param {string} config.sessionId - Shared across the same model download session.
   * @returns {Promise<[string, object]>} The file local path and headers
   */
  async getModelFile({
    engineId,
    taskName,
    url,
    rootUrl,
    urlTemplate,
    featureId,
    sessionId,
  }) {
    let downloadStart = ChromeUtils.now();

    // Create the model hub instance if needed
    if (this.modelHub === null) {
      lazy.console.debug("Creating model hub instance");
      this.modelHub = new lazy.ModelHub({
        rootUrl,
        urlTemplate,
        allowDenyList: await MLEngineParent.getAllowDenyList(),
      });
    }

    if (url.startsWith(rootUrl)) {
      url = url.slice(rootUrl.length);
      // Make sure we get a front slash
      if (!url.startsWith("/")) {
        url = `/${url}`;
      }
    }

    // Parsing url to get model name, and file path.
    // if this errors out, it will be caught in the worker
    const parsedUrl = this.modelHub.parseUrl(url, { rootUrl, urlTemplate });

    const [data, headers] = await this.modelHub.getModelDataAsFile({
      engineId,
      taskName,
      model: parsedUrl.model,
      revision: parsedUrl.revision,
      file: parsedUrl.file,
      modelHubRootUrl: rootUrl,
      modelHubUrlTemplate: urlTemplate,
      progressCallback: this.notificationsCallback?.bind(this),
      abortSignal: MLEngineParent.engineCreationAbortSignal.get(engineId),
      featureId,
      sessionId,
    });

    // Keep the latest revision for each task, model
    this.#modelFilesInUse.set(`${taskName}-${parsedUrl.model}`, {
      taskName,
      ...parsedUrl,
    });

    const sizeMB = Math.round(headers.fileSize / (1024 * 1024));
    lazy.console.debug(
      `Model ${parsedUrl.model} was fetched from ${url}, size ${sizeMB}MiB`
    );

    ChromeUtils.addProfilerMarker(
      "MLEngineParent",
      { startTime: downloadStart },
      `Downloaded model ${parsedUrl.file}: ${sizeMB}MB`
    );

    return [data, headers];
  }

  /**
   * Notify that a model download is complete.
   *
   * @param {object} config
   * @param {string} config.engineId - The engine id.
   * @param {string} config.model - The model name (organization/name).
   * @param {string} config.revision - The model revision.
   * @param {string} config.featureId - The engine id.
   * @param {string} config.sessionId - Shared across the same model download session.
   * @returns {Promise<void>}
   */
  async notifyModelDownloadComplete({
    engineId,
    model,
    revision,
    featureId,
    sessionId,
  }) {
    if (this.modelHub === null) {
      lazy.console.debug(
        "Model hub instance not created, skipping notifyModelDownloadComplete"
      );
      return;
    }
    await this.modelHub.notifyModelDownloadComplete({
      engineId,
      sessionId,
      featureId,
      model,
      revision,
    });
  }

  /**
   * Gets the wasm file from remote settings.
   *
   * @param {RemoteSettingsClient} client
   * @param {string} backend - The ML engine for which the WASM buffer is requested.
   */
  static async #getWasmArrayRecord(client, backend) {
    const wasmRecords = /** @type {WasmRecord[]} */ (
      await lazy.TranslationsParent.getMaxSupportedVersionRecords(client, {
        filters: {
          name: MLEngineParent.WASM_FILENAME[
            backend || MLEngineParent.DEFAULT_BACKEND
          ],
        },
        minSupportedMajorVersion:
          MLEngineParent.WASM_MAJOR_VERSION[
            backend || MLEngineParent.DEFAULT_BACKEND
          ],
        maxSupportedMajorVersion:
          MLEngineParent.WASM_MAJOR_VERSION[
            backend || MLEngineParent.DEFAULT_BACKEND
          ],
      })
    );

    if (wasmRecords.length === 0) {
      // The remote settings client provides an empty list of records when there is
      // an error.
      throw new Error("Unable to get the ML engine from Remote Settings.");
    }

    if (wasmRecords.length > 1) {
      lazy.console.error(wasmRecords);
      throw new Error("Expected the ml engine to only have 1 record.");
    }
    const [record] = wasmRecords;
    lazy.console.debug(
      `Using runtime ${record.name}@${record.version}`,
      record
    );
    return record;
  }

  /**
   * Gets the configuration of the worker
   *
   * @returns {{ url: string, options: WorkerOptions }}
   */
  static getWorkerConfig() {
    return {
      url: "chrome://global/content/ml/MLEngine.worker.mjs",
      options: { type: "module" },
    };
  }

  /**
   * Gets the allow/deny list from remote settings
   *
   * @returns {Promise<RecordsML["ml-model-allow-deny-list"][]>}
   */
  static async getAllowDenyList() {
    return /** @type {Promise<RecordsML["ml-model-allow-deny-list"][]>} */ (
      MLEngineParent.#getRemoteClient(RS_ALLOW_DENY_COLLECTION).get()
    );
  }

  /**
   * Gets the inference options from remote settings given a feature id or task name.
   *
   * Each feature can store default options in Remote Settings.
   *
   * We fallback to taskName if there is no featureId provided.
   *
   * @param {string} featureId - id of the feature
   * @param {string} taskName - name of the inference task
   * @param {string|null} modelId - name of the model id
   * @returns {Promise<RemoteSettingsInferenceOptions | { runtimeFilename: string }>}
   */

  static async getInferenceOptions(featureId, taskName, modelId) {
    const client = MLEngineParent.#getRemoteClient(
      RS_INFERENCE_OPTIONS_COLLECTION
    );

    /** @type {Record<string, string>} */
    const filters = featureId ? { featureId } : { taskName };
    if (modelId) {
      filters.modelId = modelId;
    }

    /** @type {Array<RecordsML["ml-inference-options"]>} */
    let records = /** @type {any[]} */ (await client.get({ filters }));

    // If no records found and we searched by featureId, retry with taskName
    if (records.length === 0 && featureId) {
      lazy.console.debug(`No record for feature id "${featureId}"`);
      /** @type {Record<string, string>} */
      const fallbackFilters = { taskName };
      if (modelId) {
        fallbackFilters.modelId = modelId;
      }
      records = /** @type {any} */ (
        await client.get({ filters: fallbackFilters })
      );
      lazy.console.debug(`fallbackFilters: "${fallbackFilters}"`);
    }

    // Handle case where multiple records exist
    if (records.length > 1) {
      throw new Error(
        `Found more than one inference options record for "${featureId}" and "${taskName}", and no matching modelId in pipelineOptions`
      );
    }

    // If still no records, return default runtime options
    if (records.length === 0) {
      return {
        runtimeFilename:
          MLEngineParent.WASM_FILENAME[MLEngineParent.DEFAULT_BACKEND],
      };
    }

    const options = records[0];
    return {
      modelRevision: options.modelRevision,
      modelId: options.modelId,
      tokenizerRevision: options.tokenizerRevision,
      tokenizerId: options.tokenizerId,
      processorRevision: options.processorRevision,
      processorId: options.processorId,
      dtype: options.dtype,
      numThreads: options.numThreads,
      runtimeFilename:
        MLEngineParent.WASM_FILENAME[
          options.backend || MLEngineParent.DEFAULT_BACKEND
        ],
    };
  }

  /**
   * Downloads and verifies a Remote Settings attachment.
   *
   * This method fetches a file from a remote base URL (either resolved from `lazy.Utils.baseAttachmentsURL()` or a fallback CDN),
   * verifies its hash and size, and returns its binary data as an `ArrayBuffer`.
   *
   * @param {object} options - The input options.
   * @param {WasmRecord} options.wasmRecord - wasm records
   * @param {string} options.localRoot - The root where to save the attachment.
   *
   * @returns {Promise<ArrayBuffer>} A promise that resolves to the downloaded file's binary content as an ArrayBuffer.
   *
   * @throws {Error} If the content hash of the downloaded file does not match the expected hash.
   */
  static async downloadRSAttachment({ wasmRecord, localRoot }) {
    const { attachment, version } = wasmRecord;
    const { location, filename, hash, size } = attachment;

    // If the base URL cannot be obtained from Remote Settings, no need to go further.
    let baseURL;
    try {
      baseURL = await lazy.Utils.baseAttachmentsURL();
    } catch (error) {
      console.error("Error fetching content from Remote settings:", error);
      throw error;
    }

    // Validate inputs
    let checkError = lazy.ModelHub.checkInput(localRoot, version, filename);
    if (checkError) {
      throw checkError;
    }

    const fileObject = await lazy.OPFS.download({
      savePath: `${RUNTIME_ROOT_IN_OPFS}/${localRoot}/${version}/${filename}`,
      deletePreviousVersions: true,
      skipIfExists: true,
      source: baseURL + location,
      sha256Hash: hash,
      fileSize: size,
    });

    return fileObject.arrayBuffer();
  }

  /**
   * Download the wasm for the ML inference engine.
   *
   * @param {string} backend - The ML engine for which the WASM buffer is requested.
   * @returns {Promise<ArrayBuffer>}
   */
  static async getWasmArrayBuffer(backend) {
    const client = MLEngineParent.#getRemoteClient(RS_RUNTIME_COLLECTION);
    backend = backend || MLEngineParent.DEFAULT_BACKEND;

    if (!MLEngineParent.#wasmRecord[backend]) {
      // Place the records into a promise to prevent any races.
      MLEngineParent.#wasmRecord[backend] = MLEngineParent.#getWasmArrayRecord(
        client,
        backend
      );
    }

    let wasmRecord;
    try {
      wasmRecord = await MLEngineParent.#wasmRecord[backend];
      if (!wasmRecord) {
        return Promise.reject(
          "Error: Unable to get the ML engine from Remote Settings."
        );
      }
    } catch (error) {
      MLEngineParent.#wasmRecord[backend] = null;
      throw error;
    }

    /** @type {ArrayBuffer} */
    let buffer;

    if (wasmRecord.attachment) {
      buffer = await MLEngineParent.downloadRSAttachment({
        wasmRecord,
        localRoot: backend,
      });
    } else {
      // fallback for mocked unit tests.
      // @ts-expect-error - This API is not well-typed.
      buffer = (await client.attachments.download(wasmRecord)).buffer;
    }

    return buffer;
  }

  /**
   * Lazily initializes the RemoteSettingsClient for the downloaded wasm binary data.
   *
   * @param {string} collectionName - The name of the collection to use.
   * @returns {RemoteSettingsClient}
   */
  static #getRemoteClient(collectionName) {
    if (MLEngineParent.#remoteClients[collectionName]) {
      return MLEngineParent.#remoteClients[collectionName];
    }

    /** @type {RemoteSettingsClient} */
    const client = lazy.RemoteSettings(collectionName, {
      bucketName: "main",
    });

    MLEngineParent.#remoteClients[collectionName] = client;

    client.on(
      "sync",
      /**
       * @param {object} param
       * @param {SyncEvent} param.data
       */
      async ({ data }) => {
        const { created, updated, deleted } = data;
        lazy.console.debug(`"sync" event for ${collectionName}`, {
          created,
          updated,
          deleted,
        });

        // Remove all the deleted records.
        for (const record of deleted) {
          await client.attachments.deleteDownloaded(record);
        }

        // Remove any updated records, and download the new ones.
        for (const { old: oldRecord } of updated) {
          await client.attachments.deleteDownloaded(oldRecord);
        }

        // Do nothing for the created records.
      }
    );

    return client;
  }

  /**
   * Goes through the engines and determines their status. This is used by about:inference
   * to display debug information about the engines.
   *
   * @see MLEngineChild#getStatusByEngineId
   *
   * @returns {Promise<StatusByEngineId>}
   */
  getStatusByEngineId() {
    return this.sendQuery("MLEngine:GetStatusByEngineId");
  }

  /**
   * Resolves to true if the native ONNX runtime is available, otherwise false.
   *
   * @returns {Promise<boolean>}
   */
  requestIsNativeOnnxRuntimeAvailable() {
    return this.sendQuery("MLEngine:RequestIsNativeOnnxRuntimeAvailable");
  }

  /**
   * Send a message to gracefully shutdown all of the ML engines in the engine process.
   * This mostly exists for testing the shutdown paths of the code.
   */
  forceShutdown() {
    lazy.console.debug("MLEngine:ForceShutdown called");
    return this.sendQuery("MLEngine:ForceShutdown");
  }
}

/**
 * A utility class that manages a main promise for the full response
 * and a sequence of chunk promises for incremental parts of the response.
 *
 * @template T
 */
class ResponseOrChunkResolvers {
  /**
   * Resolver for the main promise (full response).
   *
   * @type {PromiseWithResolvers<T>}
   */
  mainResolvers;

  /**
   * The main promise for the full response.
   *
   * @type {Promise<T>}
   */
  promise;

  /**
   * Index tracking the next chunk resolver to be returned.
   *
   * @type {number}
   */
  nextchunkResolverIdx = 0;

  /**
   * Array of resolvers for incremental chunk promises.
   *
   * @type {Array<PromiseWithResolvers<ProgressAndStatusCallbackParams>>}
   */
  chunkResolvers = [];

  /**
   * Initializes the class with a main promise resolver
   * and the first chunk resolver for incremental data.
   */
  constructor() {
    lazy.console.debug("Initializing ResponseOrChunkResolvers ...");
    this.mainResolvers = Promise.withResolvers();
    this.promise = this.mainResolvers.promise;

    // Initialize the first chunk resolver
    this.chunkResolvers.push(Promise.withResolvers());
  }

  /**
   * Resolves the main promise with the provided value, indicating the full response is ready.
   *
   * @param {*} value - The value to resolve the main promise with (e.g., the complete response data).
   */
  resolve(value) {
    this.mainResolvers.resolve(value);
  }

  /**
   * Rejects the main promise with the provided reason, indicating that the full response failed.
   *
   * @param {*} reason - The reason for rejecting the main promise (e.g., an error).
   */
  reject(reason) {
    this.mainResolvers.reject(reason);
  }

  /**
   * Returns the promise for the next chunk of the response and advances the internal index.
   * Each call retrieves the promise for the next incremental part of the response.
   *
   * @returns {Promise<ProgressAndStatusCallbackParams>} The promise for the next chunk of data.
   */
  getAndAdvanceChunkPromise() {
    this.nextchunkResolverIdx += 1;
    return this.chunkResolvers[this.nextchunkResolverIdx - 1].promise;
  }

  /**
   * Resolves the current chunk promise with the provided value
   * and prepares a new chunk resolver for the next incremental part of the response.
   *
   * @param {ProgressAndStatusCallbackParams} value - The value to resolve the current chunk promise with (e.g., a part of the response data).
   */
  resolveChunk(value) {
    // Create a new chunk resolver for future chunks
    this.chunkResolvers.push(Promise.withResolvers());
    // Resolve the current chunk
    this.chunkResolvers[this.chunkResolvers.length - 2].resolve(value);
  }

  /**
   * Rejects the current chunk promise with the provided reason
   * and prepares a new chunk resolver for the next incremental part of the response.
   *
   * @param {*} reason - The reason for rejecting the current chunk promise (e.g., an error with this chunk).
   */
  rejectChunk(reason) {
    // Create a new chunk resolver for future chunks
    this.chunkResolvers.push(Promise.withResolvers());
    // Reject the current chunk
    this.chunkResolvers[this.chunkResolvers.length - 2].reject(reason);
  }
}

/**
 * The interface to communicate to an MLEngine in the parent process. The engine manages
 * its own lifetime, and is kept alive with a timeout. A reference to this engine can
 * be retained, but once idle, the engine will be destroyed. If a new request to run
 * is sent, the engine will be recreated on demand. This balances the cost of retaining
 * potentially large amounts of memory to run models, with the speed and ease of running
 * the engine.
 *
 * @template {EngineFeatureIds} FeatureID
 */
export class MLEngine {
  /**
   * The cached engines.
   *
   * @type {Map<string, MLEngine<any>>}
   */
  static #instances = new Map();

  /**
   * @type {MessagePort | null}
   */
  #port = null;

  /**
   * A monotonically increasing ID to track requests.
   */
  #nextRequestId = 0;

  /**
   * Tie together a message id to a resolved response.
   *
   * @type {Map<number, PromiseWithResolvers<EngineResponses[FeatureID]> | ResponseOrChunkResolvers<EngineResponses[keyof EngineResponses]>>}
   */
  #requests = new Map();

  /**
   * @type {"uninitialized" | "ready" | "error" | "closed" | "crashed"}
   */
  engineStatus = "uninitialized";

  /**
   * Whether this engine is currently registered as an "ipc:content-shutdown"
   * observer. Used to avoid removing an observer that was never added, which
   * would throw NS_ERROR_ILLEGAL_VALUE.
   *
   * @type {boolean}
   */
  isObserving = false;

  /**
   * Unique identifier for the engine.
   *
   * @type {string}
   */
  engineId;

  /**
   * Callback to call when receiving an initializing progress status.
   *
   * @type {?function(ProgressAndStatusCallbackParams):void}
   */
  notificationsCallback = null;

  /**
   * Removes an instance of the MLEngine with the given engineId.
   *
   * @param {string} engineId - The ID of the engine instance to be removed.
   * @param {boolean} shutdown - Flag indicating whether to shutdown the engine.
   * @param {boolean} replacement - Flag indicating whether the engine is being replaced.
   * @returns {Promise<void>} A promise that resolves once the engine is removed.
   */
  static async removeInstance(engineId, shutdown, replacement) {
    for (const [id, engine] of MLEngine.#instances.entries()) {
      if (engine.engineId == engineId) {
        lazy.console.debug(`Removing engine ${engineId}`);
        await engine.terminate(shutdown, replacement);
        // Abort any pending operations for the engine
        MLEngineParent.engineCreationAbortControllers.get(engineId)?.abort();
        MLEngine.#instances.delete(id);
        lazy.console.debug(`Removed engine ${engineId}`);
      }
    }
  }

  /**
   * Retrieves an instance of the MLEngine with the given engineId.
   *
   * @param {string} engineId - The ID of the engine instance to retrieve.
   * @returns {MLEngine<unknown> | null} The engine instance with the given ID, or null if not found.
   */
  static getInstance(engineId) {
    return MLEngine.#instances.get(engineId) || null;
  }

  /**
   * Private constructor for an ML Engine.
   *
   * @param {object} config - The configuration object for the instance.
   * @param {MLEngineParent} config.mlEngineParent - The parent machine learning engine associated with this instance.
   * @param {PipelineOptions} config.pipelineOptions - The options for configuring the pipeline associated with this instance.
   * @param {?function(ProgressAndStatusCallbackParams):void} config.notificationsCallback - The initialization progress callback function to call.
   */
  constructor({ mlEngineParent, pipelineOptions, notificationsCallback }) {
    const engineId = pipelineOptions.engineId;
    if (!engineId) {
      throw new Error("Expected to have an engineId on PipelineOptions");
    }
    /** @type {Record<string, Array<(data: unknown) => void>>} */
    this.events = {};
    this.engineId = engineId;
    MLEngine.#instances.set(engineId, this);
    /** @type {MLEngineParent} */
    this.mlEngineParent = mlEngineParent;
    /** @type {PipelineOptions} */
    this.pipelineOptions = pipelineOptions;
    this.notificationsCallback = notificationsCallback;
    this.telemetry = new MLTelemetry({
      featureId: pipelineOptions.featureId,
      flowId: pipelineOptions.flowId,
    });
    this.QueryInterface = ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]);
  }

  /**
   * Validates an inference request before sending to child process.
   *
   * @param {object} request - The request to validate
   * @returns {object|null} The validated request, or null if blocked
   */
  #validateRequest(request) {
    return request;
  }

  /**
   * Validates an inference response after receiving from child process.
   *
   * @param {object} response - The response to validate
   * @returns {object|null} The validated response, or null if blocked
   */
  #validateResponse(response) {
    lazy.console.debug("[MLSecurity] Validating response:", response);
    return response;
  }

  /**
   * Observes shutdown events from the child process.
   *
   * When the inference process is shutdown, we want to set the port to null and throw an error.
   *
   * @param {any} aSubject
   * @param {string} aTopic
   */
  observe(aSubject, aTopic) {
    aSubject.QueryInterface(Ci.nsIPropertyBag2);

    if (!aSubject.get("abnormal")) {
      // ignoring normal events
      return;
    }

    const childID = aSubject.get("childID");

    if (
      aTopic === "ipc:content-shutdown" &&
      childID === this.mlEngineParent.childID
    ) {
      const pid = aSubject.get("osPid");
      lazy.console.error(
        `Got abnormal shutdown of the inference process (pid=${pid})`
      );
      this.#port = null;
      const err = new Error(
        `The inference process was shutdown (pid=${pid}), childId=${childID}`
      );
      this.setEngineStatus("crashed");
      throw err;
    }
  }

  /**
   * Initialize the MLEngine.
   *
   * @template {EngineFeatureIds} FeatureId
   *
   * @param {object} config - The configuration object for the instance.
   * @param {MLEngineParent} config.mlEngineParent - The parent machine learning engine associated with this instance.
   * @param {PipelineOptions} config.pipelineOptions - The options for configuring the pipeline associated with this instance.
   * @param {?function(ProgressAndStatusCallbackParams):void} config.notificationsCallback - The initialization progress callback function to call.
   * @returns {Promise<MLEngine<FeatureId>>}
   */
  static async initialize({
    mlEngineParent,
    pipelineOptions,
    notificationsCallback,
  }) {
    lazy.console.debug("Initializing ML engine", pipelineOptions);

    /** @type {MLEngine<FeatureId>} */
    const mlEngine = new MLEngine({
      mlEngineParent,
      pipelineOptions,
      notificationsCallback,
    });

    /**
     * Helper to ensure we never leave resources dangling if something fails.
     *
     * @param {unknown} err
     */
    const hardTeardown = err => {
      try {
        mlEngine.setEngineStatus?.("error");
      } catch {}
      try {
        mlEngine.#port?.close?.();
      } catch {}
      // If you also keep a child port reference inside the instance, close it here too.
      // try { mlEngine.#childPort?.close?.(); } catch {}
      mlEngine.#port = null;
      return err;
    };
    try {
      await mlEngine.setupPortCommunication();

      try {
        // Only attempt this after comms are up.
        await mlEngine.mlEngineParent.deletePreviousModelRevisions();
      } catch (err) {
        // Treat model cleanup failure as fatal for a clean init.
        let message = err;
        if (err && typeof err === "object" && "message" in err && err.message) {
          message = err.message;
        }
        throw hardTeardown(
          new Error(`Failed to delete previous model revisions: ${message}`)
        );
      }
      return mlEngine;
    } catch (err) {
      // setupPortCommunication already tries to clean up, but make this idempotent.
      throw hardTeardown(err);
    }
  }

  /**
   * Registers an event listener for the specified event.
   *
   * @param {string} event - The name of the event.
   * @param {(...args: any[]) => void} listener - The callback function to execute when the event is triggered.
   */
  on(event, listener) {
    if (!this.events[event]) {
      this.events[event] = [];
    }
    this.events[event].push(listener);
  }

  /**
   * Removes an event listener for the specified event.
   *
   * @param {string} event - The name of the event.
   * @param {(...args: any) => void} listenerToRemove - The callback function to remove.
   */
  off(event, listenerToRemove) {
    if (!this.events[event]) {
      return;
    }

    this.events[event] = this.events[event].filter(
      listener => listener !== listenerToRemove
    );
  }

  /**
   * Emits the specified event, invoking all registered listeners with the provided data.
   *
   * @param {string} event - The name of the event.
   * @param {any} data - The data to pass to the event listeners.
   */
  emit(event, data) {
    if (!this.events[event]) {
      return;
    }
    this.events[event].forEach(listener => listener(data));
  }

  /**
   * Sets the engine status and emits a statusChanged event.
   *
   * @param {"uninitialized" | "ready" | "error" | "closed" | "crashed"} status - The new status of the engine.
   */
  setEngineStatus(status) {
    this.engineStatus = status;
    this.emit("statusChanged", status);
  }

  /**
   * Create a MessageChannel to communicate with the engine directly.
   * And ensure the engine is fully initialized with all required files for the current model version downloaded.
   */
  async setupPortCommunication() {
    if (this.#port !== null) {
      throw new Error("Port already exists");
    }
    lazy.console.debug("Creating ML engine port");
    const { port1: childPort, port2: parentPort } = new MessageChannel();
    const transferables = [childPort];
    this.#port = parentPort;
    const newPortResolvers = Promise.withResolvers();

    // Wire messages before attempting to send the port

    // @ts-expect-error - The onmessage event isn't particularly well typed at this time.
    this.#port.onmessage =
      /**
       * @param {MessageEvent} message
       */
      message => this.handlePortMessage(message, newPortResolvers);

    /**
     * Helper to clean up on failure
     *
     * @param {unknown} err
     */
    const cleanupOnError = err => {
      try {
        if (this.#port && this.#port.onmessage) {
          this.#port.removeEventListener("message", this.#port.onmessage);
        }
      } catch {}
      try {
        this.#port?.close?.();
      } catch {}
      try {
        childPort.close();
      } catch {}
      this.#port = null;

      // Make sure the awaiting code is released
      try {
        newPortResolvers.reject(err);
      } catch {}
    };

    try {
      this.mlEngineParent.sendAsyncMessage(
        "MLEngine:NewPort",
        {
          port: childPort,
          pipelineOptions: this.pipelineOptions.getOptions(),
        },
        transferables
      );
    } catch (error) {
      this.setEngineStatus("error");
      cleanupOnError(error);
      try {
        await newPortResolvers.promise;
      } catch {
        // Avoid unhandledrejection noise in some runtimes
      }
      throw error;
    }
    try {
      await newPortResolvers.promise;
      this.setEngineStatus("ready");
    } catch (error) {
      // If the resolver rejected for any other reason, still tidy up
      this.setEngineStatus("error");
      cleanupOnError(error);
      throw error;
    }
  }

  /**
   * Handles messages received from the port.
   *
   * @param {MessageEvent} event - The message event.
   * @param {PromiseWithResolvers<void>} newPortResolvers - An object containing a promise for mlEngine new port setup, along with two functions to resolve or reject it.
   */
  handlePortMessage = (event, newPortResolvers) => {
    const { data } = event;
    switch (data.type) {
      case "EnginePort:EngineReady": {
        if (data.error) {
          newPortResolvers.reject(data.error);
        } else {
          // The child reports the backend it actually used; record it so
          // telemetry and log messages reflect reality rather than the
          // requested sentinel (e.g. "best-onnx").
          if (data.resolvedBackend) {
            this.pipelineOptions.backend = data.resolvedBackend;
          }
          newPortResolvers.resolve();
        }

        break;
      }
      case "EnginePort:RunResponse": {
        const { response, error, requestId, resourcesBefore, resourcesAfter } =
          data;
        const request = this.#requests.get(requestId);
        if (request) {
          if (error) {
            this.telemetry.recordRunInferenceFailure(error);
            request.reject(error);
          } else if (response) {
            // Validate response before returning to caller
            /** @type {any} */
            const validatedResponse = this.#validateResponse(response);
            if (!validatedResponse) {
              request.reject(new Error("Response failed security validation"));
            } else {
              this.telemetry.recordRunInferenceSuccessFlow(
                this.engineId,
                validatedResponse.metrics
              );
              // Attach resource metrics from the child process
              validatedResponse.resourcesBefore = resourcesBefore;
              validatedResponse.resourcesAfter = resourcesAfter;
              request.resolve(validatedResponse);
            }
          }
        } else {
          lazy.console.error(
            "Could not resolve response in the MLEngineParent",
            data
          );
        }

        this.#requests.delete(requestId);
        break;
      }
      case "EnginePort:EngineTerminated": {
        // The engine was terminated, and if a new run is needed a new port
        // will need to be requested.
        this.setEngineStatus("closed");
        newPortResolvers?.reject(
          new Error("Engine was terminated before initialization completed.")
        );

        this.discardPort();
        break;
      }
      case "EnginePort:InitProgress": {
        /** @type {ProgressAndStatusCallbackParams} */
        const statusResponse = data.statusResponse;
        if (data.statusResponse.type === lazy.Progress.ProgressType.INFERENCE) {
          const requestId = data.statusResponse.metadata.requestId;
          const request = this.#requests.get(requestId);

          if (request) {
            if (data.statusResponse.ok) {
              if ("resolveChunk" in request) {
                request.resolveChunk(statusResponse);
              }
            } else if ("rejectChunk" in request) {
              request.rejectChunk(statusResponse);
            }
          } else {
            lazy.console.error(
              "Could not resolve response in the MLEngineParent",
              data.statusResponse
            );
          }
        }

        // TODO(aristide) Don't send the chunk data back to the callback
        this.notificationsCallback?.(data.statusResponse);
        break;
      }
      default:
        lazy.console.error("Unknown port message from engine", data);
        break;
    }
  };

  /**
   * Discards the current port and closes the connection.
   */
  discardPort() {
    if (this.#port !== null) {
      this.#port.postMessage({ type: "EnginePort:Discard" });
      this.#port.close();
      this.#port = null;
    }
  }

  /**
   * Terminates the engine.
   *
   * @param {boolean} [shutdown] - Flag indicating whether to shutdown the engine.
   * @param {boolean} [replacement] - Flag indicating whether the engine is being replaced.
   * @returns {Promise<void>} A promise that resolves once the engine is terminated.
   */
  async terminate(shutdown, replacement) {
    if (this.#port !== null) {
      lazy.console.debug(`Terminating engine ${this.engineId}`);
      this.#port.postMessage({
        type: "EnginePort:Terminate",
        shutdown,
        replacement,
      });
      await this.#waitForStatus("closed");
    }
  }

  /**
   * Waits for the engine to reach the desired status.
   *
   * @param {string} desiredStatus - The desired engine status.
   * @returns {Promise<string>} - A promise that resolves when the engine reaches the desired status.
   */

  #waitForStatus(desiredStatus) {
    return new Promise((resolve, reject) => {
      // Initial check in case the status is already the desired one
      if (this.engineStatus === desiredStatus) {
        resolve(`Engine status is now ${desiredStatus} `);
      }

      /**
       * @param {string} status
       */
      const onStatusChanged = status => {
        if (status === desiredStatus) {
          this.off("statusChanged", onStatusChanged);
          lazy.clearTimeout(timeoutId);
          resolve(`Engine status is now ${desiredStatus} `);
        }
      };

      // Set a timeout to reject the promise if the status doesn't change in time
      const timeoutId = lazy.setTimeout(() => {
        this.off("statusChanged", onStatusChanged);
        reject(
          `Timeout after ${TERMINATE_TIMEOUT} ms: Engine status did not reach ${desiredStatus} `
        );
      }, TERMINATE_TIMEOUT);

      this.on("statusChanged", onStatusChanged);
    });
  }

  /**
   * Run the inference request
   *
   * @param {EngineRequests[FeatureID]} request
   * @returns {Promise<EngineResponses[FeatureID]>}
   */
  async run(request) {
    /** @type {PromiseWithResolvers<EngineResponses[FeatureID]>} */
    const resolvers = Promise.withResolvers();
    const requestId = this.#nextRequestId++;
    this.#requests.set(requestId, resolvers);
    let transferables = [];
    if (
      request &&
      typeof request === "object" &&
      "data" in request &&
      request.data instanceof ArrayBuffer
    ) {
      transferables.push(request.data);
    }

    // If the port is null maybe the inference process has shut down
    if (this.#port === null) {
      throw new Error("Port does not exist");
    }

    // Validate request before sending to child process
    const validatedRequest = this.#validateRequest(request);
    if (!validatedRequest) {
      throw new Error("Request failed security validation");
    }

    const beforeRun = ChromeUtils.now();

    this.#port.postMessage(
      {
        type: "EnginePort:Run",
        requestId,
        request: validatedRequest,
        engineRunOptions: { enableInferenceProgress: false },
      },
      transferables
    );

    const result = await resolvers.promise;

    this.telemetry.recordEngineRun({
      beforeRun,
      resourcesBefore: result.resourcesBefore,
      resourcesAfter: result.resourcesAfter,
      engineId: this.engineId,
      modelId: this.pipelineOptions.modelId,
      backend: this.pipelineOptions.backend,
    });

    return result;
  }

  /**
   * Run the inference request using an async generator function.
   *
   * @param {EngineRequests[FeatureID]} request - The inference request containing the input data.
   * @returns {AsyncGenerator<ChunkResponse>} An async generator yielding chunks of generated responses.
   */
  async *runWithGenerator(request) {
    lazy.console.debug(`runWithGenerator called for request ${request}`);
    const startTime = ChromeUtils.now();

    // Create a promise to track when the engine has fully completed all runs
    const responseChunkResolvers = new ResponseOrChunkResolvers();

    const requestId = this.#nextRequestId++;
    this.#requests.set(requestId, responseChunkResolvers);

    let completed = false;

    // Track when the engine is fully completed
    const completionPromise = responseChunkResolvers.promise.finally(() => {
      completed = true;
    });

    // A consumer can abandon this generator before the `await completionPromise`
    // below runs (an early return from its `for await`, or a throw while
    // handling a chunk). The engine still settles the request, so make sure its
    // rejection always has a handler and isn't reported as an uncaught rejection
    // that keeps the caller alive when nobody is awaiting it anymore.
    completionPromise.catch(() => {});

    // Handle transferables for performance optimization
    const transferables = [];
    if (
      request &&
      typeof request === "object" &&
      "data" in request &&
      request.data instanceof ArrayBuffer
    ) {
      transferables.push(request.data);
    }

    // If the port is null maybe the inference process has shut down
    if (this.#port === null) {
      throw new Error("The port is null");
    }

    // Validate request before sending to child process
    const validatedRequest = this.#validateRequest(request);
    if (!validatedRequest) {
      throw new Error("Request failed security validation");
    }

    // Send the request to the engine via postMessage with optional transferables
    this.#port.postMessage(
      {
        type: "EnginePort:Run",
        requestId,
        request: validatedRequest,
        engineRunOptions: { enableInferenceProgress: true },
      },
      transferables
    );

    /**
     * @param {number} delay
     */
    const timeoutPromise = delay =>
      new Promise(resolve =>
        lazy.setTimeout(() => resolve({ timeout: true, ok: true }), delay)
      );

    // Collect both the token and text counts, as the tokens aren't always available.
    let tokenCount = 0;
    let characterCount = 0;

    // Only generated (non-prompt) chunks are counted for cadence telemetry.
    let firstChunkTime = null;
    let lastChunkTime = null;
    let generatedChunkCount = 0;
    let interChunkTimeTotal = 0;

    let chunkPromise = responseChunkResolvers.getAndAdvanceChunkPromise();
    let chunkStartTime = ChromeUtils.now();

    // Loop to yield chunks as they arrive
    while (true) {
      // Wait for the chunk with a timeout
      const chunk = await Promise.race([chunkPromise, timeoutPromise(10)]);

      // If there was no timeout we can yield the chunk and move to the next
      if (!chunk.timeout) {
        lazy.console.debug(
          `Chunk received ${lazy.stringifyForLog(chunk.metadata)}`
        );
        tokenCount += chunk.metadata.tokens?.length ?? 0;
        characterCount += chunk.metadata.text?.length ?? 0;

        if (!chunk.metadata.isPrompt) {
          const now = ChromeUtils.now();
          if (firstChunkTime === null) {
            firstChunkTime = now;
          } else {
            interChunkTimeTotal += now - lastChunkTime;
          }
          lastChunkTime = now;
          generatedChunkCount++;
        }

        yield {
          text: chunk.metadata.text,
          tokens: chunk.metadata.tokens,
          isPrompt: chunk.metadata.isPrompt,
          toolCalls: chunk.metadata.toolCalls,
          usage: chunk.metadata.usage,
        };

        // Be a bit defensive here in getting the metadata, as different engines may
        // report different things back.
        let markerText;
        if (chunk.metadata.tokens?.length) {
          markerText = `${chunk.metadata.tokens?.length} tokens`;
        } else if (chunk.metadata.text?.length) {
          markerText = `${chunk.metadata.text?.length} characters`;
        } else {
          markerText = "empty response";
        }

        ChromeUtils.addProfilerMarker(
          "MLEngineParent",
          { startTime: chunkStartTime },
          `chunk generated ${markerText}` +
            ` (${this.pipelineOptions.backend} ${this.pipelineOptions.modelId})`
        );

        chunkStartTime = ChromeUtils.now();
        chunkPromise = responseChunkResolvers.getAndAdvanceChunkPromise();
      } else if (this.#port === null) {
        // in case of a timeout check if the inference process is still alive
        lazy.console.error("The port was closed.");
        if (this.engineStatus === "crashed") {
          throw new Error(
            "The inference process has crashed, the port is null. This was for the following request: " +
              lazy.stringifyForLog(request)
          );
        }
        break;
      }

      // Warn if the engine completed before receiving all chunks
      if (completed) {
        lazy.console.warn(
          "Warning: The run completed before the last chunk was received. The full output may not have been received."
        );
        break;
      }

      // Check if this is the last chunk or if an error occurred
      if (
        chunk.statusText === lazy.Progress.ProgressStatusText.DONE ||
        !chunk.ok
      ) {
        break;
      }
    }

    // Wait for the engine to fully complete before exiting
    const result = await completionPromise;

    // Tokens may not be available.
    let markerText;
    if (tokenCount) {
      markerText = `${tokenCount} tokens`;
    } else if (characterCount) {
      markerText = `${characterCount} characters`;
    } else {
      markerText = "an empty response";
    }

    ChromeUtils.addProfilerMarker(
      "MLEngineParent",
      { startTime },
      `runWithGenerator generated ${markerText}` +
        ` (${this.pipelineOptions.backend} ${this.pipelineOptions.modelId})`
    );

    this.telemetry.recordEngineRun({
      beforeRun: startTime,
      resourcesBefore: result.resourcesBefore,
      resourcesAfter: result.resourcesAfter,
      engineId: this.engineId,
      modelId: this.pipelineOptions.modelId,
      backend: this.pipelineOptions.backend,
      tokenCount,
      characterCount,
      timeToFirstChunk:
        firstChunkTime === null ? null : firstChunkTime - startTime,
      averageChunkTime:
        generatedChunkCount > 1
          ? interChunkTimeTotal / (generatedChunkCount - 1)
          : null,
    });

    return result;
  }
}
