/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {import("../../content/Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 */

// import { Wllama } from "chrome://global/content/ml/wllama-module.mjs";
/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/* eslint-disable mozilla/reject-import-system-module-from-non-system */
import {
  createFileUrl,
  Progress,
} from "chrome://global/content/ml/Utils.sys.mjs";

import { OPFS } from "chrome://global/content/ml/OPFS.sys.mjs";

/**
 * Log level set by the pipeline.
 *
 * @type {string}
 */
let _logLevel = "Error";

/**
 * Lazy initialization container.
 *
 * @type {object}
 */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevel: _logLevel, // we can't use maxLogLevelPref in workers.
    prefix: "ML:LlamaPipeline",
  });
});

/**
 * Conditionally imports `wllama_module.mjs` or `wllama_module-dev.mjs` based on the build type.
 *
 * - The module is lazily loaded on first use in the `LlamaPipeline.initialize`.
 * - If running in Nightly, the non-minified (dev) version is used.
 * - Otherwise, the optimized production version is loaded.
 */
let wllamaPromise = AppConstants.NIGHTLY_BUILD
  ? import("chrome://global/content/ml/wllama-module-dev.mjs")
  : import("chrome://global/content/ml/wllama-module.mjs");

let wllamaModule = null;

/**
 * Initializes the LlamaPipeline with the specified model and runtime configuration.
 *
 * @param {object} mlEngineWorker - The machine learning engine worker responsible for execution.
 * @param {ArrayBuffer} wasm - The buffer to the WebAssembly (WASM) binary required for execution.
 * @param {object} options - Configuration options for the pipeline.
 * @param {string} [options.modelHubUrlTemplate] - URL template for fetching models.
 * @param {string} [options.modelHubRootUrl] - Root URL for the model hub.
 * @param {string} [options.modelId] - Identifier of the model to be loaded.
 * @param {string} [options.modelRevision] - Specific revision of the model to be used.
 * @param {string} [options.modelFile] - Name of the model file to load.
 * @param {number} [options.numContext=700] - Number of context tokens to use for inference.
 * @param {number} [options.numBatch=700] - Number of tokens to process in a batch.
 * @param {number} [options.numUbatch=700] - Number of micro-batches to split inference into.
 * @param {number} [options.numThreads=0] - Number of CPU threads to use (default: auto).
 * @param {boolean} [options.flashAttn=false] - Whether to enable Flash Attention for optimization.
 * @param {boolean} [options.useMmap=false] - Whether to use memory-mapped file loading.
 * @param {boolean} [options.useMlock=true] - Whether to lock model files in memory to prevent swapping.
 * @param {string} [options.kvCacheDtype="q8_0"] - Data type of the model weights (e.g., "q8_0" for 8-bit quantization).
 * @param {number} [options.numThreadsDecoding=0] - Number of threads to use for decoding (default: auto).
 *
 * @returns {Promise<LlamaPipeline>} A promise that resolves to an initialized LlamaPipeline instance.
 */
export class LlamaPipeline {
  wllama = null;

  constructor(wllama) {
    this.wllama = wllama;
  }

  static async initialize(
    mlEngineWorker,
    wasm,
    {
      modelHubUrlTemplate,
      modelHubRootUrl,
      modelId,
      modelRevision,
      modelFile,
      numContext = 700,
      numBatch = 700,
      numUbatch = 700,
      numThreads = 0,
      flashAttn = false,
      useMmap = false,
      useMlock = true,
      kvCacheDtype = "q8_0",
      numThreadsDecoding = 0,
    } = {}
  ) {
    if (!wllamaModule) {
      wllamaModule = await wllamaPromise;
    }
    let startInitTime = performance.now();

    const modelFilePath = (
      await mlEngineWorker.getModelFile(
        createFileUrl({
          model: modelId,
          revision: modelRevision,
          file: modelFile,
          urlTemplate: modelHubUrlTemplate,
          rootUrl: modelHubRootUrl,
        })
      )
    ).ok[2];

    lazy.console.debug("LlamaPipeline.initialize", { modelFilePath });

    const wasmUrl = URL.createObjectURL(
      new Blob([wasm], { type: "application/wasm" })
    );

    const configPaths = { "multi-thread/wllama.wasm": wasmUrl };

    const wllama = new wllamaModule.Wllama(configPaths, {
      logger: lazy.console,
    });

    const blobs = [await (await OPFS.getFileHandle(modelFilePath)).getFile()];

    let options = {};

    let cacheType = "f32";

    if (flashAttn) {
      cacheType = "f16";

      if (kvCacheDtype) {
        cacheType = kvCacheDtype.replace("fp", "f");
      }
    }

    if (numThreadsDecoding <= 0) {
      numThreadsDecoding = numThreads;
    }

    if (numThreads >= 1) {
      options.n_threads = numThreads;
    }

    if (numThreadsDecoding >= 1) {
      options.n_threads_decoding = numThreadsDecoding;
    }

    await wllama.loadModel(blobs, {
      n_ctx: numContext,
      useCache: false,
      n_gpu_layers: 0,
      offload_kqv: false,
      n_batch: numBatch,
      n_ubatch: numUbatch,
      use_mmap: useMmap,
      use_mlock: useMlock,
      flash_attn: flashAttn,
      cache_type_k: cacheType,
      cache_type_v: cacheType,
      ...options,
    });

    URL.revokeObjectURL(wasmUrl);

    lazy.console.debug("Init time", performance.now() - startInitTime);

    return new LlamaPipeline(wllama);
  }

  /**
   * Runs text generation based on the given prompt using the Llama model.
   *
   * @param {object} options - The options for text generation.
   * @param {string | string[]} options.prompt - The input prompt or an array of chat messages.
   * @param {number} [options.nPredict=100] - The number of tokens to generate.
   * @param {boolean} [options.skipPrompt=true] - If true, skips processing the prompt tokens.
   * @param {int} [options.stopTokens=[]] - List of custom token IDs for stopping the generation.
   * @param {int} [options.useCache=false] - If true, it skips re-evaluating the conversation history.
   *                                         Specifically, if a new prompt shares the same prefix as a previous prompt,
   *                                         the cached computation (kv-cache) for that prefix will be reused, avoiding redundant computation.
   * @param {float} [options.temp=0] - The sampling temperature.
   * @param {float} [options.topP=0] - The top probabilities to use for top-p sampling.
   * @param {int} [options.topK=0] - The top-k tokens to use for top-k sampling.
   * @param {object} [options.extraWllamaSamplingConfig={}] - Additional sampling settings. For details, refer to
   *                                                    github.com/ngxson/wllama/blob/2.2.1/src/wllama.ts#L118
   * @param {string|null} [requestId=null] - An optional identifier for tracking the request.
   * @param {?function(ProgressAndStatusCallbackParams):void|null} [inferenceProgressCallback=null] - A callback function to track inference progress.
   *        It receives an object containing:
   *        - `{boolean} ok`: Whether the operation succeeded.
   *        - `{Object} metadata`: Additional metadata (text, tokens, requestId, etc.).
   *        - `{Progress.ProgressType} type`: The type of progress event.
   *        - `{Progress.ProgressStatusText} statusText`: The current status.
   * @param {MessagePort|null} [port=null] - An optional MessageChannel port for sending progressive inference updates.
   *
   * @returns {Promise<string>} A promise that resolves to the generated text output.
   *
   * @throws {Error} If an error occurs during inference, it is thrown and also sent via the port or callback.
   */
  async run(
    {
      prompt,
      nPredict = 100,
      skipPrompt = true,
      stopTokens = [],
      useCache = false,
      temp = 0,
      topP = 0,
      topK = 0,
      ...extraWllamaSamplingConfig
    } = {},
    requestId = null,
    inferenceProgressCallback = null,
    port = null
  ) {
    try {
      let startTime = performance.now();
      let endPromptTime;
      let isPromptDone = false;
      let startPromptTime = startTime;
      let startDecodingTime = startTime;

      const textDecoder = new TextDecoder();

      const configSampling = {
        temp,
        top_p: topP,
        top_k: topK,
        ...extraWllamaSamplingConfig,
      };

      let promptTokens = null;

      if (Array.isArray(prompt)) {
        prompt = await this.wllama.formatChat(prompt, true);
      }

      if (!skipPrompt && (port || inferenceProgressCallback)) {
        promptTokens = await this.wllama.tokenize(prompt, true);
        port?.postMessage({
          tokens: promptTokens,
          ok: true,
          isPrompt: true,
          text: prompt,
        });

        inferenceProgressCallback?.({
          ok: true,
          metadata: {
            text: prompt,
            tokens: promptTokens,
            isPrompt: true,
            requestId,
          },
          type: Progress.ProgressType.INFERENCE,
          statusText: Progress.ProgressStatusText.IN_PROGRESS,
        });
      }

      const output = await this.wllama.createCompletion(
        promptTokens || prompt,
        {
          nPredict,
          sampling: configSampling,
          useCache,
          stopTokens,
          onNewToken: (token, piece, _currentText) => {
            if (!isPromptDone) {
              isPromptDone = true;
              endPromptTime = performance.now();
              startDecodingTime = endPromptTime;
            }

            const pieceText = textDecoder.decode(piece);

            port?.postMessage({
              tokens: [token],
              ok: true,
              isPrompt: false,
              text: pieceText,
            });

            inferenceProgressCallback?.({
              ok: true,
              metadata: {
                text: pieceText,
                tokens: [token],
                isPrompt: false,
                requestId,
              },
              type: Progress.ProgressType.INFERENCE,
              statusText: Progress.ProgressStatusText.IN_PROGRESS,
            });
          },
        }
      );

      const endTime = performance.now();
      lazy.console.debug("Decoding time", endTime - startDecodingTime);
      lazy.console.debug("Prompt time", endPromptTime - startPromptTime);
      lazy.console.debug("Overall time", endTime - startTime);
      lazy.console.debug("Generated", output);

      port?.postMessage({ done: true, finalOutput: output, ok: true });

      inferenceProgressCallback?.({
        ok: true,
        metadata: {
          text: "",
          requestId,
          tokens: [],
        },
        type: Progress.ProgressType.INFERENCE,
        statusText: Progress.ProgressStatusText.DONE,
      });

      return { done: true, finalOutput: output, ok: true, metrics: [] };
    } catch (error) {
      port?.postMessage({ done: true, ok: false, error });

      inferenceProgressCallback?.({
        ok: false,
        metadata: {
          text: "",
          requestId,
          tokens: [],
        },
        type: Progress.ProgressType.INFERENCE,
        statusText: Progress.ProgressStatusText.DONE,
      });

      throw error;
    }
  }
}
