/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// TextGenerator is a ChromeOnly WebIDL interface (dom/chrome-webidl).
/* global TextGenerator */

/** MLEngine-shaped engine over the HWInference utility process. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @typedef {import("chrome://global/content/ml/EngineProcess.sys.mjs").PipelineOptions} PipelineOptions
 * @typedef {import("chrome://global/content/ml/Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 * @typedef {(data: ProgressAndStatusCallbackParams) => void} NotificationsCallback
 * @typedef {{ name: string, when: number }} RunTimestamp
 */

/**
 * PipelineOptions declares its model fields nullable; createEngine() only
 * hands this engine options it has resolved.
 *
 * @typedef {PipelineOptions & {
 *   engineId: string,
 *   taskName: string,
 *   modelId: string,
 *   modelRevision: string,
 *   modelFile: string,
 *   modelHubRootUrl: string,
 *   modelHubUrlTemplate: string,
 * }} ResolvedPipelineOptions
 */

/**
 * The llama.cpp request shape MLEngine consumers send to run().
 *
 * @typedef {object} LlamaRunRequest
 * @property {string | TextGenerationMessage[]} prompt
 * @property {number} [nPredict]
 * @property {number} [minOutputBufferSize]
 * @property {TextGenerationSampler[]} [samplers]
 * @property {number[]} [stopTokens]
 * @property {boolean} [stopOnEndOfGenerationTokens]
 */

/**
 * @typedef {object} EngineRunResult
 * @property {true} done
 * @property {string} finalOutput
 * @property {true} ok
 * @property {ReturnType<typeof toEngineMetrics>} metrics
 * @property {ReturnType<typeof toEngineResources>} resourcesBefore
 * @property {ReturnType<typeof toEngineResources>} resourcesAfter
 */

const lazy = XPCOMUtils.declareLazy({
  LLAMA_CPP_VERSION:
    "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs",
  MLEngineParent:
    "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs",
  MLTelemetry: "chrome://global/content/ml/MLTelemetry.sys.mjs",
  generateUUID: "chrome://global/content/ml/Utils.sys.mjs",
});

/** @param {TextGenerationResourceSnapshot} snapshot */
const toEngineResources = snapshot => ({
  cpuTime: snapshot.cpuTimeMs,
  memory: snapshot.memoryBytes,
});

// LlamaCppPipeline spells the float cache types fp16 and fp32; the
// generator, f16 and f32.
/**
 * @param {string} dtype
 * @returns {TextGenerationKVCacheDtype}
 */
function toKVCacheDtype(dtype) {
  return /** @type {TextGenerationKVCacheDtype} */ (dtype.replace(/^fp/, "f"));
}

/**
 * @param {PipelineOptions} options
 * @returns {TextGeneratorCreateOptions}
 */
function toCreateOptions(options) {
  /** @type {TextGeneratorCreateOptions} */
  const createOptions = {
    contextSize: options.numContext ?? undefined,
    batchSize: options.numBatch ?? undefined,
    ubatchSize: options.numUbatch ?? undefined,
    flashAttn: options.flashAttn ?? undefined,
    featureId: options.featureId ?? "",
  };
  if (options.kvCacheDtype) {
    createOptions.kvCacheDtype = toKVCacheDtype(options.kvCacheDtype);
  }
  if (options.numThreads) {
    createOptions.numThreads = options.numThreads;
  }
  if (options.numThreadsDecoding) {
    createOptions.numThreadsDecoding = options.numThreadsDecoding;
  }
  return createOptions;
}

/**
 * @param {LlamaRunRequest} request
 * @returns {TextGenerationRequest}
 */
function toGenerateRequest(request) {
  let prompt = request.prompt;
  if (!Array.isArray(prompt)) {
    prompt = [{ role: "user", content: String(prompt) }];
  }
  /** @type {TextGenerationRequest} */
  const generateRequest = {
    messages: prompt.map(message => ({
      role: message.role,
      content: message.content,
    })),
  };
  // Leave unset fields absent so TextGenerationRequest defaults apply.
  if (request.nPredict != null) {
    generateRequest.maxTokens = request.nPredict;
  }
  if (request.minOutputBufferSize != null) {
    generateRequest.bufferLength = request.minOutputBufferSize;
  }
  if (request.samplers != null) {
    generateRequest.samplers = request.samplers;
  }
  if (request.stopTokens != null) {
    generateRequest.stopTokens = request.stopTokens;
  }
  if (request.stopOnEndOfGenerationTokens != null) {
    generateRequest.stopOnEndOfGenerationTokens =
      request.stopOnEndOfGenerationTokens;
  }
  return generateRequest;
}

/**
 * @param {TextGenerationResult} result
 * @param {RunTimestamp[]} runTimestamps
 */
function toEngineMetrics(result, runTimestamps) {
  const { usage } = result;
  const outputTokens = usage.generatedTokens;
  const decodingTime = usage.timings.decodeMs;
  return {
    runTimestamps,
    inputTokens: usage.promptTokens,
    inputCharacters: usage.promptCharacters,
    outputTokens,
    inferenceTime: usage.timings.prefillMs + decodingTime,
    decodingTime,
    timeToFirstToken: usage.timings.prefillMs,
    tokensPerSecond: decodingTime
      ? outputTokens / (decodingTime / 1000)
      : undefined,
    timePerOutputToken: outputTokens ? decodingTime / outputTokens : undefined,
  };
}

/** Async-iterable sink that the delta callback pushes into. */
class ChunkQueue {
  /** @type {string[]} */
  #pending = [];
  #closed = false;
  /** @type {((value?: unknown) => void) | null} */
  #wakeUp = null;

  /** @param {string} text */
  push(text) {
    this.#pending.push(text);
    this.#wakeUp?.();
  }

  close() {
    this.#closed = true;
    this.#wakeUp?.();
  }

  async *[Symbol.asyncIterator]() {
    while (!this.#closed || this.#pending.length) {
      if (!this.#pending.length) {
        await new Promise(resolve => {
          this.#wakeUp = resolve;
        });
        this.#wakeUp = null;
        continue;
      }
      yield this.#pending.shift();
    }
  }
}

/** What recordEngineRun reports about a streamed run. */
class StreamStats {
  firstChunkAt = 0;
  lastChunkAt = 0;
  chunkCount = 0;
  characterCount = 0;
  streaming = false;

  constructor() {
    this.beforeRun = ChromeUtils.now();
  }

  /** @param {string} text */
  onChunk(text) {
    this.lastChunkAt = ChromeUtils.now();
    if (!this.firstChunkAt) {
      this.firstChunkAt = this.lastChunkAt;
    }
    this.chunkCount += 1;
    this.characterCount += text.length;
  }

  /** @param {EngineRunResult} result */
  metrics(result) {
    return {
      tokenCount: result.metrics.outputTokens,
      characterCount: this.characterCount,
      timeToFirstChunk: this.firstChunkAt
        ? this.firstChunkAt - this.beforeRun
        : null,
      averageChunkTime:
        this.chunkCount > 1
          ? (this.lastChunkAt - this.firstChunkAt) / (this.chunkCount - 1)
          : null,
    };
  }
}

export class TextGenerationEngine {
  /** @type {string} */
  engineId;
  /** @type {ResolvedPipelineOptions} */
  pipelineOptions;
  /** @type {"uninitialized" | "ready" | "closed"} */
  engineStatus = "uninitialized";
  /** @type {NotificationsCallback | null} */
  notificationsCallback = null;
  /** @type {InstanceType<typeof lazy.MLTelemetry>} */
  telemetry;

  /** @type {TextGenerator | null} */
  #generator = null;
  /** @type {RunTimestamp[]} */
  #initTimestamps = [];
  #inFlight = false;

  /** @param {PipelineOptions} pipelineOptions */
  static shouldRoute(pipelineOptions) {
    if (!Services.prefs.getBoolPref("browser.ml.llama.hwInference", false)) {
      return false;
    }
    return pipelineOptions.backend === "llama.cpp";
  }

  /**
   * @param {PipelineOptions} pipelineOptions
   * @param {NotificationsCallback | null} [notificationsCallback]
   * @param {AbortSignal} [abortSignal]
   */
  static async create(
    pipelineOptions,
    notificationsCallback = null,
    abortSignal = undefined
  ) {
    const start = ChromeUtils.now();
    const engine = new TextGenerationEngine(
      pipelineOptions,
      notificationsCallback
    );
    const { engineId } = engine;
    try {
      await engine.#initialize(abortSignal);
      engine.telemetry.recordEngineCreationSuccessFlow({
        engineId,
        duration: ChromeUtils.now() - start,
      });
    } catch (error) {
      engine.telemetry.recordEngineCreationFailure({
        modelId: pipelineOptions.modelId,
        featureId: pipelineOptions.featureId,
        taskName: pipelineOptions.taskName,
        engineId,
        error,
      });
      throw error;
    }
    return engine;
  }

  /**
   * @param {PipelineOptions} pipelineOptions
   * @param {NotificationsCallback | null} notificationsCallback
   */
  constructor(pipelineOptions, notificationsCallback) {
    this.pipelineOptions = /** @type {ResolvedPipelineOptions} */ (
      pipelineOptions
    );
    this.engineId = this.pipelineOptions.engineId;
    this.notificationsCallback = notificationsCallback;
    this.telemetry = new lazy.MLTelemetry({
      featureId: pipelineOptions.featureId,
      flowId: pipelineOptions.flowId,
      hostProcess: "hwinference",
    });
  }

  /** @param {AbortSignal} [abortSignal] */
  async #initialize(abortSignal) {
    abortSignal?.throwIfAborted();
    const options = this.pipelineOptions;
    this.#initTimestamps = [
      { name: "initializationStart", when: ChromeUtils.now() },
    ];

    const hub = await lazy.MLEngineParent.createModelHub({
      rootUrl: options.modelHubRootUrl,
      urlTemplate: options.modelHubUrlTemplate,
    });
    const sessionId = lazy.generateUUID();
    let modelBlob;
    try {
      [modelBlob] = await hub.getModelFileAsBlob({
        engineId: options.engineId,
        taskName: options.taskName,
        model: options.modelId,
        revision: options.modelRevision,
        file: options.modelFile,
        modelHubRootUrl: options.modelHubRootUrl,
        modelHubUrlTemplate: options.modelHubUrlTemplate,
        progressCallback: this.notificationsCallback,
        abortSignal,
        featureId: options.featureId ?? options.engineId,
        sessionId,
      });
    } finally {
      await hub.notifyModelDownloadComplete({
        engineId: options.engineId,
        model: options.modelId,
        revision: options.modelRevision,
        featureId: options.featureId ?? options.engineId,
        sessionId,
      });
    }

    const createOptions = toCreateOptions(options);
    this.#generator = await TextGenerator.create(modelBlob, createOptions);
    this.pipelineOptions.backend = "llama.cpp";
    this.#initTimestamps.push({
      name: "initializationEnd",
      when: ChromeUtils.now(),
    });
    this.engineStatus = "ready";
  }

  #runnableGenerator() {
    if (this.engineStatus !== "ready" || !this.#generator) {
      throw new Error("TextGenerationEngine: engine is not ready");
    }
    if (this.#inFlight) {
      throw new Error("A generation is already in progress");
    }
    return this.#generator;
  }

  /**
   * @param {LlamaRunRequest} request
   * @param {(text: string) => void} [onDelta]
   * @returns {Promise<EngineRunResult>}
   */
  async #execute(request, onDelta) {
    const generator = this.#runnableGenerator();
    this.#inFlight = true;
    try {
      const runStart = ChromeUtils.now();
      generator.clear();
      const result = await generator.generate(
        toGenerateRequest(request),
        onDelta
      );
      const runEnd = ChromeUtils.now();
      return {
        done: true,
        finalOutput: result.content,
        ok: true,
        metrics: toEngineMetrics(result, [
          ...this.#initTimestamps,
          { name: "runStart", when: runStart },
          { name: "runEnd", when: runEnd },
        ]),
        resourcesBefore: toEngineResources(result.resources.before),
        resourcesAfter: toEngineResources(result.resources.after),
      };
    } finally {
      this.#inFlight = false;
    }
  }

  /**
   * @param {StreamStats} stats
   * @param {EngineRunResult} result
   */
  #recordRun(stats, result) {
    this.telemetry.recordRunInferenceSuccessFlow(this.engineId, result.metrics);
    this.telemetry.recordEngineRun({
      beforeRun: stats.beforeRun,
      resourcesBefore: result.resourcesBefore,
      resourcesAfter: result.resourcesAfter,
      engineId: this.engineId,
      modelId: this.pipelineOptions.modelId,
      backend: this.pipelineOptions.backend,
      backendSourceRevision: lazy.LLAMA_CPP_VERSION,
      ...(stats.streaming ? stats.metrics(result) : {}),
    });
  }

  /** @param {LlamaRunRequest} request */
  async run(request) {
    const stats = new StreamStats();
    try {
      const result = await this.#execute(request);
      this.#recordRun(stats, result);
      return result;
    } catch (error) {
      this.telemetry.recordRunInferenceFailure(error);
      throw error;
    }
  }

  /** @param {LlamaRunRequest} request */
  async *runWithGenerator(request) {
    const stats = new StreamStats();
    stats.streaming = true;
    const queue = new ChunkQueue();
    /** @type {Promise<EngineRunResult> | null} */
    let completion = null;
    let settled = false;
    try {
      completion = this.#execute(request, text => {
        stats.onChunk(text);
        queue.push(text);
      });
      // Keeps the rejection handled; the await below rethrows it.
      completion.catch(() => {});
      const close = () => queue.close();
      completion.then(close, close);

      for await (const text of queue) {
        yield { text, tokens: [], isPrompt: false };
      }

      const result = await completion;
      // Link Preview keys its final flush on an empty terminal chunk.
      yield { text: "", tokens: [], isPrompt: false };

      this.#recordRun(stats, result);
      settled = true;
      return result;
    } catch (error) {
      settled = true;
      this.telemetry.recordRunInferenceFailure(error);
      throw error;
    } finally {
      // Breaking out of a `for await` loop returns this generator mid-decode.
      if (!settled) {
        this.cancel();
        const result = await completion?.catch(() => null);
        if (result) {
          this.#recordRun(stats, result);
        }
      }
    }
  }

  /** Stops the generation; the run settles and the generator stays usable. */
  cancel() {
    this.#generator?.cancel();
  }

  async terminate() {
    if (this.#generator) {
      this.#generator.terminate();
      this.#generator = null;
    }
    this.engineStatus = "closed";
  }
}
