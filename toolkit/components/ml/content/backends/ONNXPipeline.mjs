/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

// The following globals are defined in dom/webidl/ONNX.webidl
/* global Tensor, InferenceSession */

/**
 * @typedef {import("../../content/Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 */

/**
 * @typedef {object} PipelineMetrics
 * @property {number} [preprocessingTime] - Time spent preprocessing inputs (ms).
 * @property {number} [tokenizingTime] - Time spent tokenizing (same as preprocessingTime, for Glean consistency) (ms).
 * @property {number} [inferenceTime] - Time spent running the model (ms).
 * @property {number} [decodingTime] - Time spent decoding outputs (ms).
 * @property {number} inputTokens - Number of tokens in the input.
 * @property {number} outputTokens - Number of tokens in the output.
 * @property {number|null} [timeToFirstToken] - Time to the first generated token (ms).
 * @property {number} [tokensPerSecond] - Inference throughput (tokens/s).
 * @property {number} [timePerOutputToken] - Latency per output token (ms).
 * @property {Array<object>} [runTimestamps] - Timeline of execution events.
 */

/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

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
    prefix: "GeckoMLONNXPipeline",
  });
});

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    Progress: "chrome://global/content/ml/Utils.sys.mjs",
    setLogLevel: "chrome://global/content/ml/Utils.sys.mjs",
  },
  { global: "current" }
);

const NATIVE_BACKEND = "onnx-native";
const WASM_BACKEND = "onnx";

/**
 * A global reference to the Transformers library.
 * Initially `null` until `importTransformers` sets it.
 *
 * @global
 * @type {object | null}
 */
let transformers = null;

/**
 * Conditionally imports the Transformers library (transformers.js or transformers-dev.js)
 * on first usage, depending on the environment. If the "onnx-native" backend is used,
 * it exposes the `onnxruntime` object on the global scope under `Symbol.for("onnxruntime")`.
 *
 * @async
 * @function importTransformers
 * @param {string} backend - The backend to use (e.g. "onnx-native" or "onnx").
 * @returns {Promise<import("chrome://global/content/ml/transformers-dev.js")>}
 * A promise that resolves once the Transformers library is imported.
 */
export async function importTransformers(backend) {
  if (transformers) {
    return transformers;
  }

  let importStart = ChromeUtils.now();

  lazy.console.debug(`Using backend ${backend}`);

  if (backend === NATIVE_BACKEND) {
    // check if we have the native backend.
    if (typeof InferenceSession === "undefined") {
      throw new Error("onnx-native backend not supported");
    }

    // Exposing an onnxruntime object to the Transformers lib.
    const onnxruntime = {
      InferenceSession,
      Tensor,
      supportedDevices: ["cpu"],
      defaultDevices: ["cpu"],
    };
    globalThis[Symbol.for("onnxruntime")] = onnxruntime;
  }
  if (AppConstants.NIGHTLY_BUILD) {
    lazy.console.debug("Nightly detected. Using transformers-dev.js");
    transformers =
      await import("chrome://global/content/ml/transformers-dev.js");
  } else {
    lazy.console.debug("Beta or Release detected, using transformers.js");
    transformers = await import("chrome://global/content/ml/transformers.js");
  }

  ChromeUtils.addProfilerMarker(
    "MLEngine:ONNX",
    { startTime: importStart },
    `Load transformers.js for ${backend}`
  );

  return transformers;
}

/**
 * Apply logit processor for bad words. This is a patch to a bug with bad words processing in transformers.js
 * https://github.com/huggingface/transformers.js/pull/1278/files
 * // TODO remove once Transformers 3.4.3+ is vendored
 *
 * @param {bigint[][]} input_ids The input IDs.
 * @param {Tensor} logits The logits.
 * @returns {Tensor} The processed logits.
 */
function badWordsProcessorPatchWithBugFix(input_ids, logits) {
  for (let i = 0; i < input_ids.length; ++i) {
    const batch_logits_data = /** @type {Float32Array} */ (logits[i].data);
    const ids = input_ids[i];
    for (const bad_word_ids of this.bad_words_ids) {
      // There aren't enough tokens to match the banned sequence
      if (ids.length < bad_word_ids.length - 1) {
        continue;
      }
      // Whether to modify the logits of the last token in the bad word id sequence
      let mark = true;

      // For each bad word in the list, if the current sequence of input ids ends with this sequence (excluding the last),
      // then we set the logits of the last bad word id to -Infinity.
      for (let j = 1; j <= bad_word_ids.length - 1; ++j) {
        // NOTE: We use != instead of !== to compare bigint and number
        // @ts-ignore
        if (bad_word_ids.at(-j - 1) != ids.at(-j)) {
          // We have found a mismatch
          mark = false;
          break;
        }
      }
      if (mark) {
        batch_logits_data[bad_word_ids.at(-1)] = -Infinity;
      }
    }
  }
  return logits;
}

/**
 * Echo inference for testing purposes.
 *
 * @async
 * @param {object} request - The request object containing args.
 * @param {object} _model - The model used for inference.
 * @param {object} _tokenizer - The tokenizer used for decoding.
 * @param {object} _processor - The processor used for preparing  data.
 * @param {object} config - The config
 * @returns {Promise<object>} The result object containing the processed text.
 */
async function echo(request, _model, _tokenizer, _processor, config) {
  let result = {};
  for (let key in config) {
    result[key] = String(config[key]);
  }
  result.echo = request.data;

  // Sleeping to simulate inference latency
  await new Promise(resolve => setTimeout(resolve, request.sleepTime ?? 100));

  return {
    metrics: {
      preprocessingTime: 0,
      decodingTime: 0,
      inputTokens: 0,
      outputTokens: 0,
    },
    output: result,
  };
}

/**
 * Converts an image to text using a machine learning model.
 *
 * @async
 * @param {object} request - The request object containing image data.
 * @param {string} [request.url] - The URL of the image to process. If `url` is not provided, other fields are used.
 * @param {ArrayBuffer} [request.data] - The raw image data to process. Ignored if `url` is provided.
 * @param {number} [request.width] - The image width. Ignored if `url` is provided.
 * @param {number} [request.height] - The image height. Ignored if `url` is provided.
 * @param {number} [request.channels] - The image channels. Can be 1, 2, 3 or 4. Defaults to 4. Ignored if `url` is provided.
 * @param {object} model - The model used for inference.
 * @param {object} tokenizer - The tokenizer used for decoding.
 * @param {object} processor - The processor used for preparing image data.
 * @param {object} _config - The config
 * @returns {Promise<object>} The result object containing the processed text.
 */
async function imageToText(request, model, tokenizer, processor, _config) {
  let result = {
    metrics: {
      inferenceTime: 0,
      preprocessingTime: 0,
      decodingTime: 0,
      inputTokens: null,
      outputTokens: 0,
    },
  };
  // Destructure to simplify assignments
  const { metrics } = result;

  let startLoad = ChromeUtils.now();
  let rawImage;

  if ("url" in request) {
    rawImage = await transformers.RawImage.fromURL(request.url);
  } else {
    rawImage = new transformers.RawImage(
      request.data,
      request.width,
      request.height,
      request.channels || 4
    );
  }

  lazy.console.debug("Image loaded in ", ChromeUtils.now() - startLoad);

  const startProcessing = ChromeUtils.now();
  const { pixel_values } = await processor(rawImage);
  metrics.preprocessingTime += ChromeUtils.now() - startProcessing;
  const toReturn = [];
  const streamer = request.options?.streamer;
  for (const batch of pixel_values) {
    batch.dims = [1, ...batch.dims];
    const startInference = ChromeUtils.now();
    const output = await model.generate({ inputs: batch, streamer });
    metrics.inferenceTime += ChromeUtils.now() - startInference;
    const startDecoding = ChromeUtils.now();
    const decoded = tokenizer
      .batch_decode(output, {
        skip_special_tokens: true,
      })
      .map(x => ({ generated_text: x.trim() }));
    metrics.decodingTime += ChromeUtils.now() - startDecoding;
    toReturn.push(decoded);
  }
  lazy.console.debug("Inference done in ", ChromeUtils.now() - startProcessing);
  result.output = toReturn[0][0].generated_text;

  // Bug 1918220 - replace the result for models with that bug
  if (result.output === "T") {
    lazy.console.debug("Replacing `T` with `Text document.`");
    result.output = "Text document.";
  }
  return result;
}

async function getDomainId(domain, domainVocab) {
  return domainVocab[domain] ?? domainVocab["<unk>"];
}

/**
 * Converts a query or webpage title into a goal embedding using a multi-task ONNX model.
 *
 * Accepts either:
 * - Query: just `text`
 * - Page: requires both `text` and `domain`
 *
 * @param {object} request - The input request to the pipeline.
 * @param {Array} request.inputArgs - The input arguments: [texts], [taskTypes], [domains?]
 * @param {object} model - The ONNX model session.
 * @param {object} tokenizer - The tokenizer instance.
 * @param {object} _processor - (Unused)
 * @param {object} config - The engine configuration options
 * @param {object} modelConfig - The model configuration options
 * @returns {object} - Inference result with embedding(s)
 */
async function textToGoal(
  request,
  model,
  tokenizer,
  _processor,
  config,
  modelConfig
) {
  const result = {
    metrics: {
      preprocessingTime: 0,
      inferenceTime: 0,
      inputTokens: 0,
      outputTokens: 0,
    },
    output: [],
  };
  const { metrics } = result;

  const texts = request.args?.[0] ?? [];
  const taskTypes = request.args?.[1] ?? []; // ["query", "page", ...]
  const domains = request.args?.[2] ?? []; // Optional (needed for "page")

  for (let i = 0; i < texts.length; i++) {
    const text = texts[i];
    const task = taskTypes[i] ?? "query";
    const domain = domains[i] ?? "";

    const startToken = ChromeUtils.now();

    const encoded = await tokenizer(text, {
      padding: "max_length",
      truncation: true,
      max_length: 64,
      return_attention_mask: true,
    });
    const tokenizeTime = ChromeUtils.now() - startToken;
    metrics.preprocessingTime += tokenizeTime;
    ChromeUtils.addProfilerMarker(
      "MLEngine:ONNX",
      { startTime: startToken },
      `Tokenize text: ${encoded.input_ids.data.length} tokens`
    );
    const input_ids = encoded.input_ids.ort_tensor;
    const attention_mask = encoded.attention_mask.ort_tensor;
    const domain_vocab = modelConfig["transformers.js_config"].domain_vocab;
    const domain_id =
      task === "page" ? await getDomainId(domain, domain_vocab) : 0;
    // choose Tensor based on WASM or ONNX-NATIVE backend respectively
    const tensorFactory =
      config.backend === WASM_BACKEND ? transformers.Tensor : globalThis.Tensor;

    const domain_ids = new tensorFactory(
      "int64",
      BigInt64Array.from([BigInt(domain_id)]),
      [1]
    );
    const task_type = new tensorFactory(
      "int64",
      BigInt64Array.from([task === "query" ? 0n : 1n]),
      [1]
    );

    const inputs = {
      input_ids,
      attention_mask,
      domain_ids,
      task_type,
    };

    const startInfer = ChromeUtils.now();
    const session = model.sessions.model;
    const output = await session.run(inputs);
    const inferenceTime = ChromeUtils.now() - startInfer;
    metrics.inferenceTime += inferenceTime;
    ChromeUtils.addProfilerMarker(
      "MLEngine:ONNX",
      { startTime: startInfer },
      `textToGoal`
    );
    metrics.inputTokens += encoded.input_ids.ort_tensor.dims[1];

    result.output.push({
      embedding: Array.from(output.embedding.data),
      task,
      domain,
    });
  }

  if (result.output.length === 1) {
    result.output = result.output[0];
  }

  return result;
}

/**
 * Fusion "head" scoring pipeline.
 *
 * Modeled on `textToGoal` above (raw-tensor construction + direct session.run).
 *
 * @param {object} request - The input request to the pipeline.
 * @param {Array} request.args - `args[0]` is a `number[][]` of shape [N, X].
 * @param {object} model - The ONNX model (head). Loaded via AutoModel.
 * @param {object} _tokenizer - (Unused; the head takes float features, not text.)
 * @param {object} _processor - (Unused)
 * @param {object} config - The engine configuration options.
 * @param {object} _modelConfig - The model configuration options.
 * @returns {Promise<object>} Inference result: { output: [{label, score}], metrics }.
 */
async function fusionHead(
  request,
  model,
  _tokenizer,
  _processor,
  config,
  _modelConfig
) {
  const metrics = {
    preprocessingTime: 0,
    inferenceTime: 0,
    inputTokens: 0,
    outputTokens: 0,
  };

  const rows = request.args?.[0] ?? [];
  if (!rows.length) {
    return { output: [], metrics };
  }

  const numRows = rows.length;
  const featureDim = rows[0].length;

  // Flatten [N, X] -> Float32Array for a single batched head inference.
  const startPre = ChromeUtils.now();
  const flat = new Float32Array(numRows * featureDim);
  for (let i = 0; i < numRows; i++) {
    flat.set(rows[i], i * featureDim);
  }
  // Choose the Tensor implementation for the active backend, exactly like
  // textToGoal does for its int64 inputs.
  const tensorFactory =
    config.backend === WASM_BACKEND ? transformers.Tensor : globalThis.Tensor;
  const inputTensor = new tensorFactory("float32", flat, [numRows, featureDim]);
  metrics.preprocessingTime += ChromeUtils.now() - startPre;

  const startInfer = ChromeUtils.now();
  const session = model.sessions.model;
  // The head has a single float input; feed by its declared input name.
  const inputName = session.inputNames[0];
  const output = await session.run({ [inputName]: inputTensor });
  metrics.inferenceTime += ChromeUtils.now() - startInfer;
  ChromeUtils.addProfilerMarker(
    "MLEngine:ONNX",
    { startTime: startInfer },
    `fusionHead`
  );

  // Head produces a single [N, numClasses] logits output.
  const logitsTensor = output[session.outputNames[0]];
  const logits = logitsTensor.data;
  const numClasses = logitsTensor.dims[logitsTensor.dims.length - 1];

  // id2label maps the argmax index to the field-type string. It lives on the
  // loaded model's config for a standard classification model.
  const id2label = model.config?.id2label ?? {};

  const result = [];
  for (let i = 0; i < numRows; i++) {
    const base = i * numClasses;

    // Softmax + argmax over this row's logits.
    let maxLogit = -Infinity;
    let argmax = 0;
    for (let c = 0; c < numClasses; c++) {
      const v = logits[base + c];
      if (v > maxLogit) {
        maxLogit = v;
        argmax = c;
      }
    }
    let sumExp = 0;
    for (let c = 0; c < numClasses; c++) {
      sumExp += Math.exp(logits[base + c] - maxLogit);
    }
    const score = 1 / sumExp; // exp(maxLogit - maxLogit) / sumExp

    result.push({
      label: id2label[argmax] ?? String(argmax),
      score,
    });
  }

  return { output: result, metrics };
}

/**
 * Configuration for engine. Each task has a configuration object that
 * gets merged at runtime with the options from PipelineOptions.
 *
 * When a key exists in both the default configuration and the options,
 * the value from the options is used.
 *
 * The configuration keys that are not exposed as options are all the
 * callables that are used in the pipeline:
 *
 * - modelClass
 * - tokenizerClass
 * - processorClass
 * - pipelineFunction
 *
 * @type {object}
 */
const ENGINE_CONFIGURATION = {
  "moz-image-to-text": {
    modelId: "mozilla/distilvit",
    modelClass: "AutoModelForVision2Seq",
    tokenizerId: "mozilla/distilvit",
    tokenizerClass: "AutoTokenizer",
    processorId: "mozilla/distilvit",
    processorClass: "AutoProcessor",
    pipelineFunction: imageToText,
  },
  "moz-echo": {
    modelId: null,
    modelClass: null,
    tokenizerId: null,
    tokenizerClass: null,
    processorId: null,
    processorClass: null,
    pipelineFunction: echo,
  },
  "moz-text-to-goal": {
    modelId: "mozilla/iab-multitask-inference",
    modelClass: "AutoModel",
    tokenizerId: "mozilla/iab-multitask-inference",
    tokenizerClass: "AutoTokenizer",
    pipelineFunction: textToGoal,
  },
  // Takes windowed float features (not text), so there is no tokenizer or processor.
  // See fusionHead above.
  "moz-formfill-head": {
    modelId: null,
    modelClass: "AutoModel",
    tokenizerId: null,
    tokenizerClass: null,
    processorId: null,
    processorClass: null,
    pipelineFunction: fusionHead,
  },
};

/**
 * Copied over from ONNX see https://github.com/microsoft/onnxruntime
 *
 * This is the code used by the lib to detect multi-thread support.
 *
 */
function isMultiThreadSupported() {
  // If 'SharedArrayBuffer' is not available, WebAssembly threads will not work.
  if (typeof SharedArrayBuffer === "undefined") {
    return false;
  }
  try {
    // Test for transferability of SABs (for browsers. needed for Firefox)
    // https://groups.google.com/forum/#!msg/mozilla.dev.platform/IHkBZlHETpA/dwsMNchWEQAJ
    if (typeof MessageChannel !== "undefined") {
      new MessageChannel().port1.postMessage(new SharedArrayBuffer(1));
    }

    // Test for WebAssembly threads capability (for both browsers and Node.js)
    // This typed array is a WebAssembly program containing threaded instructions.
    //
    // This is the corresponding wasm module generated by wasm2wat:
    // (module
    //  (type (;0;) (func))
    //  (func (;0;) (type 0)
    //    i32.const 0
    //    i32.atomic.load
    //    drop)
    //  (memory (;0;) 1 1 shared))
    return WebAssembly.validate(
      new Uint8Array([
        0, 97, 115, 109, 1, 0, 0, 0, 1, 4, 1, 96, 0, 0, 3, 2, 1, 0, 5, 4, 1, 3,
        1, 1, 10, 11, 1, 9, 0, 65, 0, 254, 16, 2, 0, 26, 11,
      ])
    );
  } catch {
    return false;
  }
}

/**
 * Copied over from ONNX see https://github.com/microsoft/onnxruntime
 *
 * This is the code used by the lib to detect GPU support.
 *
 */
async function checkGPUSupport() {
  if (!navigator?.gpu) {
    return false;
  }

  const adapter = await navigator.gpu.requestAdapter({
    powerPreference: "high-performance",
    forceFallbackAdapter: false,
  });

  return !!adapter;
}

/**
 * Represents a pipeline for processing machine learning tasks.
 */
export class ONNXPipeline {
  #mlEngineWorker = null;
  #model = null;
  #tokenizer = null;
  #processor = null;
  #pipelineFunction = null;
  #genericPipelineFunction = null;
  #isReady = false;
  #config = null;
  #metrics = null;
  #errorFactory = null;
  #modelConfig = null;

  /**
   * Creates an instance of a Pipeline.
   *
   * @param {object} mlEngineWorker - Implements the Cache interface and used to get models
   * @param {object} config - The configuration options
   * @param {any} errorFactory - error class passed by the backend factory.
   */
  constructor(mlEngineWorker, config, errorFactory) {
    this.#errorFactory = errorFactory;
    this.#mlEngineWorker = mlEngineWorker;
    this.#metrics = [];
    let device;
    let session_options = {};

    // Setting up the Transformers.js environment
    // See https://huggingface.co/docs/transformers.js/api/env

    // Caching strategy.
    // Here we make sure that everytime transformers.js requires a file, it uses
    // mlEngineWorker, which transfers the request to the main thread and uses the
    // ModelHub that caches files into IndexDB.
    transformers.env.useBrowserCache = false;
    transformers.env.allowLocalModels = false;
    transformers.env.remoteHost = config.modelHubRootUrl;
    transformers.env.remotePathTemplate = config.modelHubUrlTemplate;
    transformers.env.useCustomCache = true;
    transformers.env.customCache = this.#mlEngineWorker;
    // using `NO_LOCAL` so when the custom cache is used, we don't try to fetch it (see MLEngineWorker.match)
    transformers.env.localModelPath = "NO_LOCAL";

    if (config.backend === WASM_BACKEND) {
      transformers.env.backends.onnx.wasm.numThreads = config.numThreads;

      // ONNX runtime - we set up the wasm runtime we got from RS for the ONNX backend to pick
      transformers.env.backends.onnx.wasm.wasmPaths = {};
      transformers.env.backends.onnx.wasm.wasmBinary = config.runtime;

      // Set the onnxruntime-web log/verbosity.
      // onnx log levels are "error" | "verbose" | "info" | "warning" | "fatal"
      // the default level is "warning"
      switch (config.logLevel) {
        case "All":
        case "Trace":
          transformers.env.backends.onnx.logLevel = "verbose";
          transformers.env.backends.onnx.trace = true;
          transformers.env.backends.onnx.debug = true;
          break;
        case "Debug":
          transformers.env.backends.onnx.logLevel = "verbose";
          transformers.env.backends.onnx.debug = true;
          break;
        default:
          transformers.env.backends.onnx.logLevel = "warning";
          transformers.env.backends.onnx.trace = false;
          transformers.env.backends.onnx.debug = false;
          break;
      }
      lazy.console.debug("Transformers.js env", transformers.env);
      if (config.device === "cpu") {
        config.device = "wasm";
      }
      device = config.device || "wasm";
    } else {
      device = "cpu";
      session_options.intraOpNumThreads = config.numThreads;
      session_options.interOpNumThreads = config.numThreads;
      session_options.execution_mode = "sequential";
    }

    transformers.NoBadWordsLogitsProcessor.prototype._call =
      badWordsProcessorPatchWithBugFix; // Fix bug with bad words filter

    lazy.console.debug("Transformers.js env", transformers.env);

    const dtype = (config.dtype = config.dtype || "q8");
    const modelRevision = (config.modelRevision =
      config.modelRevision || "main");

    lazy.console.debug(
      `Setting up pipeline for ${device} using ${dtype} quantization.`
    );

    if (config.pipelineFunction && config.taskName != "test-echo") {
      lazy.console.debug("Using internal inference function");

      //moz-text-to-goal customizes feature-extraction
      if (config.taskName === "moz-text-to-goal") {
        config.taskName = "feature-extraction";
        //obtain modelConfig options using AutoConfig
        this.#modelConfig = transformers.AutoConfig.from_pretrained(
          config.modelId,
          { revision: config.modelRevision }
        );
        lazy.console.debug(
          `Switching  config.taskName from  moz-text-to-goal to ${config.taskName}`
        );
      }

      // use the model revision of the tokenizer or processor don't have one
      if (!config.tokenizerRevision) {
        config.tokenizerRevision = modelRevision;
      }
      if (!config.processorRevision) {
        config.processorRevision = modelRevision;
      }

      this.#pipelineFunction = config.pipelineFunction;

      if (config.modelClass && config.modelId) {
        lazy.console.debug(
          `Loading model ${config.modelId} with class ${config.modelClass}`
        );
        this.#model = transformers[config.modelClass].from_pretrained(
          config.modelId,
          {
            revision: modelRevision,
            device,
            dtype,
            use_external_data_format: config.useExternalDataFormat ?? false,
          }
        );
      }
      if (config.tokenizerClass && config.tokenizerId) {
        lazy.console.debug(
          `Loading tokenizer ${config.tokenizerId} with class ${config.tokenizerClass}`
        );
        this.#tokenizer = transformers[config.tokenizerClass].from_pretrained(
          config.tokenizerId,
          { revision: config.tokenizerRevision, device, dtype }
        );
      }
      if (config.processorClass && config.processorId) {
        lazy.console.debug(
          `Loading processor ${config.processorId} with class ${config.processorClass}`
        );
        this.#processor = transformers[config.processorClass].from_pretrained(
          config.processorId,
          { revision: config.processorRevision, device, dtype }
        );
      }
    } else {
      lazy.console.debug("Using generic pipeline function");
      if (config.modelId != "test-echo") {
        this.#genericPipelineFunction = transformers.pipeline(
          config.taskName,
          config.modelId,
          {
            revision: config.modelRevision,
            device,
            dtype,
            use_external_data_format: config.useExternalDataFormat,
            session_options,
          }
        );
      } else {
        this.#genericPipelineFunction = async () => {};
      }
    }
    this.#config = config;
    lazy.console.debug("Pipeline initialized");
  }

  async #metricsSnapShot({ name, snapshot = {} }) {
    if (!("when" in snapshot)) {
      snapshot.when = ChromeUtils.now();
    }
    this.#metrics.push({ name, ...snapshot });
  }

  /**
   * Initializes the pipeline with given options.
   *
   * @static
   * @async
   * @param {object} mlEngineWorker - Implements the Cache interface and used to get models
   * @param {ArrayBuffer} runtime - The runtime wasm file.
   * @param {PipelineOptions} options - The options for initialization.
   * @param {any} errorFactory - error class passed by the backend factory.
   * @returns {Promise<Pipeline>} The initialized pipeline instance.
   */
  static async initialize(mlEngineWorker, runtime, options, errorFactory) {
    let initStart = ChromeUtils.now();
    let snapShot = {
      when: initStart,
    };

    if (options.logLevel) {
      _logLevel = options.logLevel;
      lazy.setLogLevel(options.logLevel); // setting Utils log level
    }
    const taskName = options.taskName;
    lazy.console.debug(`Initializing Pipeline for task ${taskName}`);
    let config;

    if (!ENGINE_CONFIGURATION[taskName]) {
      lazy.console.debug(`Unknown internal task ${taskName}`);
      // generic pipeline function
      config = {
        pipelineFunction: null,
      };
    } else {
      // Loading the config defaults for the task
      lazy.console.debug(`Internal task detected ${taskName}`);
      config = { ...ENGINE_CONFIGURATION[taskName] };
    }
    config.runtime = runtime;

    // Overriding the defaults with the options
    options.applyToConfig(config);
    config.backend = config.backend || "onnx";

    await importTransformers(config.backend);

    // reapply logLevel if it has changed.
    if (lazy.console.logLevel != config.logLevel) {
      lazy.console.logLevel = config.logLevel;
    }

    let pipelineReadyStart = ChromeUtils.now();
    const pipeline = new ONNXPipeline(mlEngineWorker, config, errorFactory);
    await pipeline.ensurePipelineIsReady();

    ChromeUtils.addProfilerMarker(
      "MLEngine:ONNX",
      { startTime: pipelineReadyStart },
      `Pipeline ready`
    );

    await pipeline.#metricsSnapShot({
      name: "initializationStart",
      snapshot: snapShot,
    });
    await pipeline.#metricsSnapShot({ name: "initializationEnd" });

    ChromeUtils.addProfilerMarker(
      "MLEngine:ONNX",
      { startTime: initStart },
      `Initialized: task=${taskName}, backend=${config.backend}`
    );

    return pipeline;
  }

  /**
   * Ensure all promises are resolved to complete file downloads and model initialization in memory.
   */
  async ensurePipelineIsReady() {
    if (!this.#isReady) {
      if (this.#config.device === "gpu") {
        if (!(await checkGPUSupport())) {
          lazy.console.warn(
            "GPU not supported. ONNX will fallback to CPU instead."
          );
        }
      }
      // deactive console.warn, see https://bugzilla.mozilla.org/show_bug.cgi?id=1891003
      const originalWarn = console.warn;
      await this.#metricsSnapShot({ name: "ensurePipelineIsReadyStart" });

      console.warn = () => {};
      try {
        if (
          this.#genericPipelineFunction &&
          this.#config.modelId != "test-echo"
        ) {
          lazy.console.debug("Initializing pipeline");
          try {
            this.#genericPipelineFunction = await this.#genericPipelineFunction;
            this.#isReady = true;
          } catch (error) {
            lazy.console.debug("Error initializing pipeline", error);
            throw this.#errorFactory(error);
          }
        } else {
          lazy.console.debug("Initializing model, tokenizer and processor");

          try {
            [this.#model, this.#tokenizer, this.#processor, this.#modelConfig] =
              await Promise.all([
                this.#model,
                this.#tokenizer,
                this.#processor,
                this.#modelConfig,
              ]);
            this.#isReady = true;
          } catch (error) {
            lazy.console.debug("Error initializing pipeline", error);
            throw this.#errorFactory(error);
          }
        }
      } finally {
        console.warn = originalWarn;
      }
      await this.#metricsSnapShot({ name: "ensurePipelineIsReadyEnd" });
      lazy.console.debug("Pipeline is fully initialized");
    }
  }

  /**
   * Runs the pipeline with the given request.
   *
   * @async
   * @param {T} request - The request object to be processed. The fields it may contain
   * depends on the task. See each pipeline function for more details.
   * When the pipeline is initialized with the generic pipeline function, the request contains
   * `args` and `options` fields. The `args` field is an array of values that are passed
   * to the generic pipeline function. The `options` field is an object that contains the options for the pipeline.
   * The request may include a `streamerOptions` field to configure streaming behavior.
   * `streamerOptions` is an object with the following properties:
   *
   * - `perTokens` (boolean): If `true`, streams data per token; otherwise, streams per word.
   * - `skipPrompt` (boolean): If `false`, the first returned value will include the prompt.
   * - `returnTokens` (boolean): If `true`, the response will include tokens.
   * @param {string} requestId - The identifier used to internally track this request.
   * @param {?function(ProgressAndStatusCallbackParams):void} inferenceProgressCallback A function to call to indicate inference progress.
   * @returns {Promise<object>} The result object from the pipeline execution.
   */
  async run(request, requestId, inferenceProgressCallback = null) {
    lazy.console.debug("Running task: ", this.#config.taskName);

    /** @type {PipelineMetrics} */
    const metrics = {
      inputTokens: 0,
      outputTokens: 0,
      preprocessingTime: 0,
      tokenizingTime: 0, // Same as preprocessingTime, but named for Glean consistency
      inferenceTime: 0,
      decodingTime: 0,
      timeToFirstToken: null,
      tokensPerSecond: 0,
      timePerOutputToken: 0,
      runTimestamps: [],
    };

    /**
     * Helper to record a timestamp in the metrics timeline.
     *
     * @param {string} name
     */
    const snapshot = name => {
      metrics.runTimestamps.push({ name, when: ChromeUtils.now() });
    };

    const runStartTime = ChromeUtils.now();
    snapshot("runStart");

    const tokenizer =
      this.#genericPipelineFunction?.tokenizer ?? this.#tokenizer;

    if (this.#genericPipelineFunction && tokenizer && request.args?.[0]) {
      try {
        const inputs = [request.args[0]].flat();
        for (const text of inputs) {
          if (typeof text === "string") {
            const encoded = await tokenizer.encode(text);
            metrics.inputTokens += encoded.length;
          }
        }
      } catch (e) {
        lazy.console.debug(
          "Could not count input tokens for generic pipeline",
          e
        );
      }
    }

    const progressInfo = {
      ok: true,
      id: request.id ?? requestId,
    };

    const streamerOptions = {
      perTokens: false,
      skipPrompt: true,
      returnTokens: false,
      ...request.streamerOptions,
    };

    let streamer;
    let chunkTokens = [];
    let chunkText = "";
    let nextTokensArePrompt = !streamerOptions.skipPrompt;
    let restoreTokenizer = false;

    let firstTokenTimestamp = null;
    if (tokenizer && inferenceProgressCallback) {
      const flushPrompts = _tokens => {
        streamer.token_cache = _tokens;
        streamer.end();
        streamer.tokenizer = {
          decode: () => {
            streamer.token_cache = [];
            return "";
          },
        };
        restoreTokenizer = true;
        streamer.next_tokens_are_prompt = false;
      };

      streamer = new transformers.TextStreamer(tokenizer, {
        skip_prompt: streamerOptions.skipPrompt,
        decode_kwargs: {
          skip_special_tokens: true,
        },
        token_callback_function: tokens => {
          if (restoreTokenizer) {
            streamer.tokenizer = tokenizer;
            restoreTokenizer = false;
          }

          // Record Time To First Token on the very first callback
          const now = ChromeUtils.now();
          if (metrics.timeToFirstToken === null) {
            metrics.timeToFirstToken = now - runStartTime;
            firstTokenTimestamp = now;
          }

          metrics.outputTokens += tokens.length;

          if (streamerOptions.perTokens) {
            if (nextTokensArePrompt) {
              flushPrompts(tokens);
            }

            inferenceProgressCallback({
              ...progressInfo,
              metadata: {
                text: chunkText,
                tokens: streamerOptions.returnTokens ? tokens : null,
                isPrompt: nextTokensArePrompt,
                requestId,
              },
              type: lazy.Progress.ProgressType.INFERENCE,
              statusText: lazy.Progress.ProgressStatusText.IN_PROGRESS,
            });

            chunkText = "";
          } else {
            chunkTokens.push(tokens);

            if (nextTokensArePrompt) {
              flushPrompts(tokens);
            }
          }
          nextTokensArePrompt = false;
        },
        // Per-word callback function
        callback_function: text => {
          if (streamerOptions.perTokens) {
            chunkText = text;
          } else {
            inferenceProgressCallback({
              ...progressInfo,
              metadata: {
                text,
                tokens: streamerOptions.returnTokens ? chunkTokens : null,
                requestId,
                isPrompt: nextTokensArePrompt,
              },
              type: lazy.Progress.ProgressType.INFERENCE,
              statusText: lazy.Progress.ProgressStatusText.IN_PROGRESS,
            });
            // reset the chunks.
            chunkTokens = [];
          }
        },
      });
    }

    // Override streamer in options
    const requestWithCallback = inferenceProgressCallback
      ? {
          ...request,
          options: { ...request.options, streamer },
        }
      : request;

    let result;

    if (this.#genericPipelineFunction) {
      if (this.#config.modelId === "test-echo") {
        result = {
          output: requestWithCallback.args,
          config: this.#config,
          multiThreadSupported: isMultiThreadSupported(),
          metrics: { ...metrics },
        };
      } else {
        const start = ChromeUtils.now();
        let output = await this.#genericPipelineFunction(
          ...requestWithCallback.args,
          requestWithCallback.options || {}
        );
        const inferenceTime = ChromeUtils.now() - start;
        metrics.inferenceTime = inferenceTime;
        ChromeUtils.addProfilerMarker(
          "MLEngine:ONNX",
          { startTime: start },
          `Inference`
        );
        if (output instanceof transformers.Tensor) {
          output = output.tolist();
        }
        result = output;
      }
    } else {
      result = await this.#pipelineFunction(
        requestWithCallback,
        this.#model,
        this.#tokenizer,
        this.#processor,
        this.#config,
        this.#modelConfig
      );
      result.metrics ??= {};
    }

    if (result.metrics) {
      for (const [key, value] of Object.entries(result.metrics)) {
        if (value !== undefined && value !== null) {
          metrics[key] = value;
        }
      }
    }

    snapshot("runEnd");
    const runEndTime = ChromeUtils.now();

    // Calculate metrics
    try {
      // If we streamed, decoding time is Time(End) - Time(FirstToken).
      // Otherwise, we fallback to inferenceTime (e.g. for embeddings or image-to-text without streaming).
      if (metrics.timeToFirstToken !== null && firstTokenTimestamp !== null) {
        metrics.decodingTime = runEndTime - firstTokenTimestamp;
      } else {
        metrics.decodingTime = metrics.inferenceTime;
      }

      // Sync tokenizingTime with preprocessingTime for Glean metrics consistency
      metrics.tokenizingTime = metrics.preprocessingTime;

      // Calculate throughput metrics if we have the necessary data
      if (metrics.inferenceTime > 0 && metrics.outputTokens > 0) {
        metrics.tokensPerSecond =
          metrics.outputTokens / (metrics.inferenceTime / 1000);
        metrics.timePerOutputToken =
          metrics.inferenceTime / metrics.outputTokens;
      }
    } catch (e) {
      lazy.console.debug("Error computing throughput metrics", e);
    }

    // Merge initialization snapshots from this.#metrics into runTimestamps
    // so tests can access all timing data
    metrics.runTimestamps = [...this.#metrics, ...metrics.runTimestamps];

    result.metrics = metrics;

    if (streamer) {
      inferenceProgressCallback?.({
        ...progressInfo,
        metadata: {
          text: "",
          requestId,
        },
        type: lazy.Progress.ProgressType.INFERENCE,
        statusText: lazy.Progress.ProgressStatusText.DONE,
      });
    }

    return result;
  }
}
