/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.ml.logLevel",
    prefix: "MLTelemetry",
  });
});

ChromeUtils.defineLazyGetter(lazy, "mlUtils", () => {
  return Cc["@mozilla.org/ml-utils;1"].getService(Ci.nsIMLUtils);
});

ChromeUtils.defineESModuleGetters(lazy, {
  isAddonEngineId: "chrome://global/content/ml/Utils.sys.mjs",
});

/**
 * MLTelemetry provides a mechanism tracking a "flow" of operations
 * related to a machine learning feature. A flow is a sequence of related
 * events that represent a single, complete user-level operation, for example
 * generating a summary for a page.
 *
 * This class uses a correlation ID pattern where flowId is passed to each
 * method, allowing flexible tracking across different parts of the system.
 *
 * @example
 * new MLTelemetry({ featureId: "ml-suggest-intent" }).sessionStart({ interaction: "button_click" });
 * @example
 * new MLTelemetry({ featureId: "ml-suggest-intent", flowId: "1234-5678" }).sessionStart({ interaction: "keyboard_shortcut"});
 */
export class MLTelemetry {
  static #systemMemoryMB = Math.round(
    Services.sysinfo.getProperty("memsize") / 1024 / 1024
  );

  /** @type {string} */
  #flowId;
  /** @type {string|undefined} */
  #featureId;
  /** @type {string|undefined} */
  #hostProcess;
  /** @type {number|undefined} */
  #startTime;

  /**
   * Creates a new MLTelemetry instance.
   *
   * @param {object} [options] - Configuration options.
   * @param {string | null} [options.featureId] - The identifier for the ML feature.
   * @param {string | null} [options.flowId] - An optional unique identifier for
   * this flow. If not provided, a new UUID will be generated.
   * @param {string} [options.hostProcess] - The process serving the engine,
   * "inference" or "hwinference"; see host_process in metrics.yaml.
   */
  constructor(options = {}) {
    this.#featureId = options.featureId;
    this.#flowId = options.flowId || crypto.randomUUID();
    this.#hostProcess = options.hostProcess;

    this.logEventToConsole(this.constructor, {
      featureId: this.#featureId,
      flowId: this.#flowId,
    });
  }

  /**
   * The unique identifier for this flow.
   *
   * @returns {string} The flow ID.
   */
  get flowId() {
    return this.#flowId;
  }

  /**
   * The feature identifier for this telemetry instance.
   *
   * @returns {string} The feature ID.
   */
  get featureId() {
    return this.#featureId;
  }

  /**
   * Returns the label used in telemetry for a given engine ID.
   * Converts addon engine IDs to "webextension" label.
   *
   * @param {string} engineId - The engine ID to convert.
   * @returns {string} The Glean label for the engine.
   */
  static getGleanLabel(engineId) {
    if (lazy.isAddonEngineId(engineId)) {
      return "webextension";
    }
    return engineId;
  }

  /**
   * Starts a telemetry session for the given flow.
   *
   * @param {object} [options] - Session start options.
   * @param {string} [options.interaction] - The interaction type (e.g., "button_click", "keyboard_shortcut").
   * @throws {Error} If session already exists for this flowId.
   */
  sessionStart({ interaction } = {}) {
    if (this.#startTime) {
      throw new Error(`Session already started for flowId: ${this.#flowId}`);
    }

    this.#startTime = ChromeUtils.now();

    Glean.firefoxAiRuntime.sessionStart.record({
      flow_id: this.flowId,
      feature_id: this.featureId,
      interaction,
    });

    this.logEventToConsole(this.sessionStart, {
      flow_id: this.flowId,
      feature_id: this.featureId,
      interaction,
    });
  }

  /**
   * Logs a debug message to the browser console, prefixed with the flow ID.
   *
   * @param {object} caller - The calling function or class.
   * @param {object} [data] - Optional data to be JSON-stringified and logged.
   */
  logEventToConsole(caller, data) {
    const flowId = data?.flowId || this.#flowId;
    const id = flowId.substring(0, 5);
    lazy.console.debug("flowId[%s]: %s", id, caller.name, data);
  }

  /**
   * Ends the telemetry session and records the final status and duration.
   *
   * @param {string} status - The final status of the session.
   * @throws {Error} If no active session found or status parameter is missing.
   * @returns {number} Duration in milliseconds.
   */
  endSession(status) {
    // Validate status
    if (!status) {
      throw new Error("status parameter is required");
    }
    // Validate that session was started
    if (!this.#startTime) {
      throw new Error(
        `sessionStart() was not called for flowId: ${this.#flowId}`
      );
    }

    const duration_ms = ChromeUtils.now() - this.#startTime;
    Glean.firefoxAiRuntime.sessionEnd.record({
      flow_id: this.#flowId,
      duration: Math.round(duration_ms),
      status,
    });

    this.logEventToConsole(this.endSession, {
      flowId: this.#flowId,
      feature_id: this.#featureId,
      status,
      duration_ms,
    });
    return duration_ms;
  }

  /**
   * Records a successful engine creation event.
   *
   * @param {object} options - Engine creation success options.
   * @param {string} [options.flowId] - The flow ID. Uses instance flowId if not provided.
   * @param {string} options.engineId - The engine identifier (e.g., "pdfjs", "ml-suggest-intent").
   * @param {number} options.duration - Engine creation time in milliseconds.
   */
  recordEngineCreationSuccessFlow({ flowId, engineId, duration }) {
    const currentFlowId = flowId || this.#flowId;
    const actualEngineId = engineId;
    const actualLabel = MLTelemetry.getGleanLabel(engineId);

    Glean.firefoxAiRuntime.engineCreationSuccessFlow.record({
      flow_id: currentFlowId,
      engineId: actualEngineId,
      duration: Math.round(duration),
      host_process: this.#hostProcess,
    });

    // Also record the old labeled timing distribution metric
    Glean.firefoxAiRuntime.engineCreationSuccess[
      actualLabel
    ].accumulateSingleSample(Math.round(duration));

    this.logEventToConsole(this.recordEngineCreationSuccessFlow, {
      flowId: currentFlowId,
      engineId: actualEngineId,
      label: actualLabel,
      duration,
    });
  }

  /**
   * Records a failed engine creation event.
   *
   * @param {object} options - Engine creation failure options.
   * @param {string} [options.flowId] - The flow ID. Uses instance flowId if not provided.
   * @param {string | null} options.modelId - The model identifier.
   * @param {string | null} options.featureId - The feature identifier.
   * @param {string | null} options.taskName - The task name.
   * @param {string | null} options.engineId - The engine identifier.
   * @param {unknown} options.error - The error class/message or object.
   */
  recordEngineCreationFailure({
    flowId,
    modelId,
    featureId,
    taskName,
    engineId,
    error,
  }) {
    const currentFlowId = flowId || this.#flowId;
    // Ensure error is always a string
    const errorString =
      typeof error === "object" && error !== null
        ? String(error.name || error.message || error)
        : String(error);

    Glean.firefoxAiRuntime.engineCreationFailure.record({
      flow_id: currentFlowId,
      modelId,
      featureId,
      taskName,
      engineId,
      error: errorString,
      host_process: this.#hostProcess,
    });

    this.logEventToConsole(this.recordEngineCreationFailure, {
      flowId: currentFlowId,
      modelId,
      featureId,
      taskName,
      engineId,
      error: errorString,
    });
  }

  /**
   * Records a successful inference run event.
   *
   * @param {string} engineId - The engine identifier.
   * @param {object} metrics - The inference metrics object.
   * @param {number} [metrics.preprocessingTime] - Time spent preprocessing (legacy).
   * @param {number} [metrics.tokenizingTime] - Time spent tokenizing in milliseconds.
   * @param {number} [metrics.inferenceTime] - Time spent on inference in milliseconds.
   * @param {number} [metrics.decodingTime] - Time spent decoding in milliseconds.
   * @param {number} [metrics.inputTokens] - Number of input tokens.
   * @param {number} [metrics.outputTokens] - Number of output tokens.
   * @param {number} [metrics.timeToFirstToken] - Time to first token in milliseconds.
   * @param {number} [metrics.tokensPerSecond] - Tokens per second.
   * @param {number} [metrics.timePerOutputToken] - Time per output token in milliseconds.
   */
  recordRunInferenceSuccessFlow(engineId, metrics) {
    try {
      const currentFlowId = this.#flowId;
      const EngineId = engineId || undefined;
      const Label = engineId ? MLTelemetry.getGleanLabel(engineId) : "no-label";

      // Handle legacy preprocessingTime field
      const tokenizingTime =
        metrics.preprocessingTime ?? metrics.tokenizingTime;

      // Ensure all metrics are properly rounded/typed for Glean
      // This will be updated to use the method from revision(D271263)
      const gleanPayload = {
        flow_id: currentFlowId,
        tokenizing_time:
          tokenizingTime != null ? Math.round(tokenizingTime) : undefined,
        inference_time:
          metrics.inferenceTime != null
            ? Math.round(metrics.inferenceTime)
            : undefined,
        decoding_time:
          metrics.decodingTime != null
            ? Math.round(metrics.decodingTime)
            : undefined,
        input_tokens:
          metrics.inputTokens != null
            ? Math.round(metrics.inputTokens)
            : undefined,
        output_tokens:
          metrics.outputTokens != null
            ? Math.round(metrics.outputTokens)
            : undefined,
        time_to_first_token:
          metrics.timeToFirstToken != null
            ? Math.round(metrics.timeToFirstToken)
            : undefined,
        tokens_per_second:
          metrics.tokensPerSecond != null
            ? Math.round(metrics.tokensPerSecond * 100) / 100
            : undefined,
        time_per_output_token:
          metrics.timePerOutputToken != null
            ? Math.round(metrics.timePerOutputToken * 100) / 100
            : undefined,
        host_process: this.#hostProcess,
      };

      Glean.firefoxAiRuntime.runInferenceSuccessFlow.record(gleanPayload);

      // record the old labeled timing distribution metric
      const totalTime = Math.round(
        (tokenizingTime || 0) +
          (metrics.inferenceTime || 0) +
          (metrics.decodingTime || 0)
      );

      Glean.firefoxAiRuntime.runInferenceSuccess[Label].accumulateSingleSample(
        totalTime
      );

      this.logEventToConsole(this.recordRunInferenceSuccessFlow, {
        ...gleanPayload,
        engineId: EngineId,
        label: Label,
      });
    } catch (telemetryError) {
      lazy.console.error("Failed to record ML telemetry:", telemetryError);
    }
  }

  /**
   * Records a failed inference run event.
   *
   * @param {string|object} error - The error class/message or object.
   * @param {string} [flow_id=this.#flowId] - The flow ID. Uses instance flowId if not provided.
   */
  recordRunInferenceFailure(error, flow_id = this.flowId) {
    // Ensure error is always a string
    const errorString = error instanceof Error ? error.message : String(error);

    Glean.firefoxAiRuntime.runInferenceFailure.record({
      flow_id,
      error: errorString,
      host_process: this.#hostProcess,
    });

    this.logEventToConsole(this.recordRunInferenceFailure, {
      flow_id,
      error: errorString,
    });
  }

  /**
   * Records an engine run event.
   *
   * See firefox.ai.runtime.engine_run in toolkit/components/ml/metrics.yaml for
   * documentation on each of the parameters here.
   *
   * @param {object} options - Engine run options.
   * @param {string} options.engineId
   * @param {string | null} options.modelId
   * @param {string | null} options.backend
   * @param {number} options.beforeRun
   * @param {{cpuTime: number | null, memory: number | null}} [options.resourcesBefore]
   * @param {{cpuTime: number | null, memory: number | null}} [options.resourcesAfter]
   * @param {number | null} [options.tokenCount]
   * @param {number | null} [options.characterCount]
   * @param {number | null} [options.timeToFirstChunk]
   * @param {number | null} [options.averageChunkTime]
   * @param {string | null} [options.backendSourceRevision]
   * @param {string} [options.flow_id]
   * @param {string} [options.feature_id]
   */
  recordEngineRun({
    engineId,
    modelId,
    backend,
    beforeRun,
    resourcesBefore,
    resourcesAfter,
    tokenCount,
    characterCount,
    timeToFirstChunk = null,
    averageChunkTime = null,
    backendSourceRevision = null,
    flow_id = this.#flowId,
    feature_id = this.#featureId,
  }) {
    let cpuMilliseconds = null;
    let cpuUtilization = null;
    const wallMilliseconds = ChromeUtils.now() - beforeRun;
    const cores = lazy.mlUtils.getOptimalCPUConcurrency();
    const memoryBytes = resourcesAfter?.memory ?? null;
    if (resourcesAfter?.cpuTime != null && resourcesBefore?.cpuTime != null) {
      cpuMilliseconds = resourcesAfter.cpuTime - resourcesBefore.cpuTime;
      cpuUtilization = (cpuMilliseconds / wallMilliseconds / cores) * 100;
    }

    /**
     * Round a potentially null number, preserving null for telemetry results.
     *
     * @param {number | null} number
     */
    function round(number) {
      if (number == null) {
        return null;
      }
      return Math.round(number);
    }

    const payload = {
      flow_id,
      cpu_milliseconds: round(cpuMilliseconds),
      wall_milliseconds: round(wallMilliseconds),
      cores: round(cores),
      cpu_utilization: round(cpuUtilization),
      memory_bytes: round(memoryBytes),
      feature_id,
      engine_id: engineId,
      model_id: modelId,
      backend,
      backend_source_revision: backendSourceRevision,
      // Specifically use the "||" operator since Glean expects "null" rather than
      // "undefined". When the counts are 0, this can mean nothing was generated for
      // the counts. We should count these as null.
      token_count: tokenCount || null,
      character_count: characterCount || null,
      time_to_first_chunk: round(timeToFirstChunk),
      average_chunk_time: round(averageChunkTime),
      system_memory_mb: MLTelemetry.#systemMemoryMB,
      host_process: this.#hostProcess,
    };

    Glean.firefoxAiRuntime.engineRun.record(payload);

    this.logEventToConsole(this.recordEngineRun, payload);
  }
}
