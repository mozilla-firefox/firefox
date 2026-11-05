/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

/**
 * This module handles the generation of embeddings using a dedicated
 * machine learning (ML) engine.
 * reference -> https://firefox-source-docs.mozilla.org/toolkit/components/ml/
 *
 * An embedding is a n-dimensional numerical representation of text
 * (e.g., mdn documentation). The embedding generation involves converting
 * text data such as title to meaningful vectors. These vectors could be
 * compared using multi-dimensional distance measures such as cosine distance.
 * Each vector is made up of values representing the relationship with
 * features defined by the model.
 *
 * Production callers MUST go through the `embeddingsGeneratorFactory`
 * singleton (`forPlaces` / `forGeneral`) so the policy for picking an
 * embedding family stays in one place and stays stable for the lifetime of
 * the process. `EmbeddingsGenerator.forTest` is provided for tests and dev
 * tooling.
 *
 * Note: The "engine" referenced in this module is specifically an ML engine
 * used for feature extraction and embedding generation.
 */
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "mlUtils",
  "@mozilla.org/ml-utils;1",
  Ci.nsIMLUtils
);

ChromeUtils.defineESModuleGetters(lazy, {
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RegionLocaleMap: "moz-src:///toolkit/modules/RegionLocaleMap.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.ml.logLevel",
    logModule: "GeckoMLEmbeddingsGenerator",
  });
});

// We set a limit for semantic search to see if device has at least 7 GiB
// Note: 8GB is ~7.48 GiB. So threshold has been set as 7 GiB (i.e 7.518 GB)
const REQUIRED_MEMORY_BYTES = 7 * 1024 * 1024 * 1024;
const REQUIRED_CPU_CORES = 2;

export const EMBEDDING_TYPE = Object.freeze({
  STATIC: "static",
  CONTEXTUAL: "contextual",
});

// Maps an embedding type to the engineConfigId that implements it.
const EMBEDDING_BACKEND_DEFAULTS = Object.freeze({
  [EMBEDDING_TYPE.STATIC]: "static-embeddings",
  [EMBEDDING_TYPE.CONTEXTUAL]: "onnx",
});

// Pref that picks static vs contextual (i.e. transformer based) embeddings
// for Places semantic history.
const PREF_PLACES_EMBEDDING_TYPE = "places.semanticHistory.embeddingType";

// Regions and locales that are better served by multilingual (contextual)
// embeddings. See RegionLocaleMap for the format: the default below reads as
// "France in English or French, or a French locale anywhere".
const PREF_MULTILINGUAL_EMBEDDING_REGIONS =
  "places.semanticHistory.multilingualEmbeddingRegions";

/** @type {[string, string[]][]} */
const MULTILINGUAL_REGIONS_DEFAULT = [
  ["FR", ["en-*", "fr-*"]],
  ["*", ["fr-*"]],
];

// Custom embedding size and model for ML driver support of testing new embeddings.
const PREF_CONTEXTUAL_EMBEDDING_DIM = "browser.ml.embedGen.textEmbeddingSize";
const PREF_CONTEXTUAL_MODEL_NAME =
  "browser.ml.embedGen.textEmbeddingFeatureModel";

// Default embedding-dimension envelope for engines that do not pin a list of
// supportedDimensions: multiple of 8, in [128, 2048].
const EMBEDDING_DIM_MIN = 128;
const EMBEDDING_DIM_MAX = 2048;
const EMBEDDING_DIM_GRANULARITY = 8;

function isValidOnnxEmbeddingDim(dim) {
  return (
    Number.isInteger(dim) &&
    dim >= EMBEDDING_DIM_MIN &&
    dim <= EMBEDDING_DIM_MAX &&
    dim % EMBEDDING_DIM_GRANULARITY === 0
  );
}

const staticEmbeddingsOptions = Object.freeze({
  // See https://huggingface.co/Mozilla/static-embeddings/blob/main/models/minishlab/potion-retrieval-32M/README.md
  subfolder: "models/minishlab/potion-retrieval-32M",
  // Available: fp32, fp16, fp8_e5m2, fp8_e4m3
  dtype: "fp16",
  // Use zstd compression.
  compression: true,
});

// Registry of engine configurations keyed by engineConfigId. Each entry is
// the base option dict for that engine; resolveEngineOptions() clones the
// entry, fills in dimension + per-call overrides, and stamps engineConfigId.
const ENGINE_OPTIONS = new Map([
  [
    "onnx",
    {
      taskName: "feature-extraction",
      featureId: "simple-text-embedder",
      timeoutMS: -1,
      numThreads: 2,
      backend: "best-onnx",
      preferredDimension: 384,
    },
  ],
  [
    "static-embeddings",
    {
      featureId: "simple-text-embedder",
      modelId: "mozilla/static-embeddings",
      modelRevision: "v1.0.0",
      taskName: "static-embeddings",
      modelHub: "mozilla",
      backend: "static-embeddings",
      supportedDimensions: [32, 64, 128, 256, 512],
      preferredDimension: 512,
    },
  ],
]);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "multilingualEmbeddingRegions",
  PREF_MULTILINGUAL_EMBEDDING_REGIONS,
  JSON.stringify(MULTILINGUAL_REGIONS_DEFAULT),
  null,
  json =>
    lazy.RegionLocaleMap.fromJSON(json, {
      fallback: MULTILINGUAL_REGIONS_DEFAULT,
      onInvalid: () =>
        lazy.console.debug(
          `Invalid json in ${PREF_MULTILINGUAL_EMBEDDING_REGIONS} pref.`
        ),
    })
);

/**
 * Resolve engine options for the given embedding type. Reads
 * `browser.ml.embedGen.*` dev prefs for the contextual path.
 *
 * @param {string} embeddingType
 *   One of EMBEDDING_TYPE.*
 * @param {number} [embeddingSize]
 *   Explicit dimension override. Otherwise the engine's preferredDimension
 *   (static) or the dev pref (contextual) is used.
 * @returns {object} merged engine options
 */
function resolveEngineOptions(embeddingType, embeddingSize) {
  const engineConfigId = EMBEDDING_BACKEND_DEFAULTS[embeddingType];
  if (!engineConfigId) {
    throw new TypeError(`Unknown embedding type ${embeddingType}`);
  }
  const base = ENGINE_OPTIONS.get(engineConfigId);
  const options = { ...base, engineConfigId };

  if (embeddingType === EMBEDDING_TYPE.STATIC) {
    const dim = embeddingSize ?? options.preferredDimension;
    if (!options.supportedDimensions.includes(dim)) {
      throw new TypeError(`Unsupported static embedding size ${dim}`);
    }
    options.embeddingDimension = dim;
    options.staticEmbeddingsOptions = {
      ...staticEmbeddingsOptions,
      dimensions: dim,
    };
    return options;
  }

  // Contextual: dimension / model come from dev prefs unless explicit
  // overrides were passed.
  const dim =
    embeddingSize ??
    Services.prefs.getIntPref(
      PREF_CONTEXTUAL_EMBEDDING_DIM,
      options.preferredDimension
    );
  if (!isValidOnnxEmbeddingDim(dim)) {
    throw new TypeError(`Unsupported contextual embedding size ${dim}`);
  }
  options.embeddingDimension = dim;

  const modelOverride = Services.prefs.getStringPref(
    PREF_CONTEXTUAL_MODEL_NAME,
    ""
  );
  if (modelOverride) {
    options.modelId = modelOverride;
  }
  return options;
}

/**
 * Returns true if user is on Mac or Windows as part of a best effort to not
 * use onnx-wasm for contextual (transformer based) embeddings.
 * Separate function to support testing.
 *
 * TODO Bug #2039862 We may able to add linux support
 */
function isMacOrWindows() {
  return AppConstants.platform === "macosx" || AppConstants.platform === "win";
}

/**
 * Builds {@link EmbeddingsGenerator} instances for production callers.
 *
 * The home region is read once, on first use, and then frozen for the lifetime
 * of the factory. Persisted embeddings are only comparable when they come from
 * the same model, and a changing region would otherwise make two generators
 * built at different points in the session disagree on the embedding family,
 * forcing the vector tables to be rebuilt mid-session.
 *
 * Use the `embeddingsGeneratorFactory` singleton. The class is exported so
 * tests can get an instance with an unfrozen region.
 */
export class EmbeddingsGeneratorFactory {
  #region;
  #regionResolved = false;

  /**
   * The home region captured on first use, frozen thereafter.
   *
   * Note that `Region.home` is null until `Region.init()` has resolved. We
   * deliberately freeze that null rather than retrying, so the embedding family
   * cannot change underneath already-persisted embeddings. A region that
   * resolves later is picked up on the next startup.
   *
   * @returns {?string}
   */
  get region() {
    if (!this.#regionResolved) {
      this.#region = lazy.Region.home;
      this.#regionResolved = true;
    }
    return this.#region;
  }

  /**
   * Whether to use embedding models that perform better in non-english markets.
   *
   * The home region and the app locale are matched against the
   * `places.semanticHistory.multilingualEmbeddingRegions` pref. Entries are
   * OR-ed, and the wildcard region applies whatever the home region is, so it
   * also covers a region that has not resolved yet.
   *
   * @returns {boolean}
   */
  #useMultiLingualEmbedding() {
    return lazy.multilingualEmbeddingRegions.matches(
      this.region,
      Services.locale.appLocaleAsBCP47
    );
  }

  /**
   * Get the type of embedding we're using for Places (static vs contextual)
   * based on config/Nimbus prefs, region and locale. Contextual embeddings are
   * best with the native ONNX runtime, so they're only selected when it is
   * likely available for the OS.
   *
   * The default is static embeddings, but for non-English markets we use
   * contextual embeddings because they perform better at matching strings
   * despite being trained mostly with English.
   *
   * @returns {string} One of the EMBEDDING_TYPE values.
   */
  #resolvePlacesEmbeddingType() {
    let embeddingType = Services.prefs.getStringPref(
      PREF_PLACES_EMBEDDING_TYPE,
      ""
    );
    if (!Object.values(EMBEDDING_TYPE).includes(embeddingType)) {
      embeddingType = this.#useMultiLingualEmbedding()
        ? EMBEDDING_TYPE.CONTEXTUAL
        : "";
    }

    // We make a best effort to not use onnx-wasm engine. There is no
    // reliable check without trying to use it. Our focus is Mac/Windows
    // for contextual embeddings until onnx-native has wider Linux adoption.
    if (embeddingType !== EMBEDDING_TYPE.CONTEXTUAL || !isMacOrWindows()) {
      embeddingType = EMBEDDING_TYPE.STATIC;
    }
    return embeddingType;
  }

  /**
   * Places semantic history. Embedding family comes from the Nimbus-driven
   * pref `places.semanticHistory.embeddingType`, falling back to the region
   * and locale.
   *
   * @returns {EmbeddingsGenerator}
   */
  forPlaces() {
    return new EmbeddingsGenerator(
      resolveEngineOptions(this.#resolvePlacesEmbeddingType())
    );
  }

  /**
   * Smart Window memories, navigation, and other general callers.
   *
   * @returns {EmbeddingsGenerator}
   */
  forGeneral() {
    return new EmbeddingsGenerator(
      resolveEngineOptions(EMBEDDING_TYPE.CONTEXTUAL)
    );
  }
}

export const embeddingsGeneratorFactory = new EmbeddingsGeneratorFactory();

export class EmbeddingsGenerator {
  #engine = undefined;
  #promiseEngine;
  #embeddingSize;
  options;

  /**
   * Internal — use {@link EmbeddingsGeneratorFactory} via the
   * `embeddingsGeneratorFactory` singleton, or
   * {@link EmbeddingsGenerator.forTest}.
   *
   * @param {object} resolvedOptions Output of `resolveEngineOptions`.
   */
  constructor(resolvedOptions) {
    this.options = resolvedOptions;
    this.#embeddingSize = resolvedOptions.embeddingDimension;
  }

  /**
   * Tests and dev tooling only. Production callers MUST use the
   * `embeddingsGeneratorFactory` singleton so policy stays in one place.
   *
   * @param {object} opts
   * @param {string} opts.type One of EMBEDDING_TYPE.*
   * @param {number} [opts.embeddingSize] Explicit dimension override.
   * @returns {EmbeddingsGenerator}
   */
  static forTest({ type, embeddingSize } = {}) {
    return new EmbeddingsGenerator(resolveEngineOptions(type, embeddingSize));
  }

  get embeddingSize() {
    return this.#embeddingSize;
  }

  /**
   * Returns model metadata that affects how embeddings are stored persistently.
   * If any of these values change, embeddings should be recomputed.
   *
   * Note that we are using only feature information passed in the constructor and
   * don't rely on inference manager details. This is to keep from persistent
   * embeddings to rerun unless needed and not require the ML Engine to be created
   * (which may trigger a model download) before the dimension can be checked.
   *
   * @returns {{
   *   featureId: string,
   *   embeddingDimension: number,
   *   modelId: string | undefined
   * }}
   */
  get modelContext() {
    const { featureId, embeddingDimension, modelId } = this.options || {};
    return { featureId, embeddingDimension, modelId };
  }

  /**
   * Checks if there is sufficient physical memory available.
   *
   * Compares the system's total physical memory against a defined
   * threshold (REQUIRED_MEMORY_BYTES). It retrieves the total memory
   * using the lazy.mlUtils.totalPhysicalMemory property and returns
   * a boolean indicating whether the available memory meets or exceeds
   * the threshold.
   *
   * @returns {boolean}
   *   - **true** if the total physical memory is equal to or greater
   *     than REQUIRED_MEMORY_BYTES.
   *   - **false** if the total physical memory is less than
   *     REQUIRED_MEMORY_BYTES.
   */
  isEnoughPhysicalMemoryAvailable() {
    lazy.console.debug(
      `totalPhysicalMemory = ${lazy.mlUtils.totalPhysicalMemory}`
    );
    return lazy.mlUtils.totalPhysicalMemory >= REQUIRED_MEMORY_BYTES;
  }

  /**
   * Checks if there are sufficient CPU cores available.
   *
   * Compares the system's optimal CPU concurrency against a defined
   * threshold (REQUIRED_CPU_CORES). It retrieves the number of available
   * CPU cores using the `lazy.mlUtils.getOptimalCPUConcurrency()` method
   * and returns a boolean indicating whether the available cores meet
   * or exceed the threshold.
   *
   * @returns {boolean}
   *   - **true** if the number of CPU cores is equal to or greater
   *     than REQUIRED_CPU_CORES.
   *   - **false** if the number of CPU cores is less than
   *     REQUIRED_CPU_CORES.
   */
  isEnoughCpuCoresAvailable() {
    lazy.console.debug(
      `Number of CPU cores = ${lazy.mlUtils.getOptimalCPUConcurrency()}`
    );
    return lazy.mlUtils.getOptimalCPUConcurrency() >= REQUIRED_CPU_CORES;
  }

  /**
   * Creates an ML engine if it does not already exist.
   *
   * @private
   * @returns {Promise<void>}
   *   Resolves when the engine is created or already exists.
   * @throws {Error}
   *   If the engine cannot be initialized.
   */
  async createEngineIfNotPresent() {
    if (!this.#engine) {
      try {
        this.#engine = await lazy.createEngine(this.options);
      } catch (ex) {
        lazy.console.error(`Unable to initialize the ML engine. Error:` + ex);
        throw new Error("Unable to initialize the ML engine.", { cause: ex });
      }
    }
  }

  /**
   * Shuts down the ML engine if it has been initialized.
   *
   * @private
   * @returns {Promise<void>}
   */
  async shutdown() {
    await this.#engine.terminate?.();
  }

  /**
   * Embeds a single text using the ML engine.
   *
   * @param {string} text
   *   The input text to be embedded.
   * @returns {Promise<any>}
   *   A promise that resolves with the embedding result from the engine.
   */
  async embed(text) {
    await this.ensureEngine();
    if (typeof text !== "string" || text.trim() === "") {
      throw new Error("Invalid input: text must be a non-empty string");
    }
    const request = {
      args: [text],
      options: { pooling: "mean", normalize: true },
    };
    return this.engineRun(request);
  }

  /**
   * Generates embeddings for multiple texts (batch of text) using the
   * ML engine.
   *
   * This asynchronous method takes an array of input texts and returns
   * an array of corresponding embedding vectors. Each vector is a
   * float array of size `#embeddingSize`, representing the mean-pooled
   * and normalized embedding of the input text.
   *
   * @param {string[]} texts
   *   The array of texts to be embedded.
   * @returns {Promise<number[][]>}
   *   A promise that resolves to an array of embedding vectors,
   *   where each vector is an array of floats with length equal to
   *  `#embeddingSize`.
   */
  async embedMany(texts) {
    await this.ensureEngine();

    if (!Array.isArray(texts)) {
      throw new Error("Expected an array of texts");
    }

    if (texts.length === 0) {
      throw new Error("embedMany received an empty array of texts");
    }

    // call the engine once with the batch of texts.
    let batchTensors = await this.engineRun({
      args: this.options.backend == "static-embeddings" ? texts : [texts],
      options: { pooling: "mean", normalize: true, max_length: 100 },
    });

    // If the result is triple nested, extract the inner array.
    if (
      Array.isArray(batchTensors) &&
      batchTensors.length === 1 &&
      Array.isArray(batchTensors[0])
    ) {
      if (batchTensors[0].length !== this.#embeddingSize) {
        batchTensors = batchTensors[0];
      }
    }
    return batchTensors;
  }

  /**
   * Ensures the ML engine is initialized.
   *
   * @returns {Promise<object>}
   *   A promise resolving to the initialized engine.
   */
  async ensureEngine() {
    if (this.#engine?.engineStatus == "closed") {
      this.#promiseEngine = null;
      this.#engine = null;
    }
    if (this.#promiseEngine) {
      return this.#promiseEngine;
    }
    let { promise, resolve, reject } = Promise.withResolvers();
    // Suppress unhandled rejection when there are no concurrent callers awaiting this promise.
    promise.catch(() => {});
    this.#promiseEngine = promise;
    try {
      await this.createEngineIfNotPresent();
      resolve(this.#engine);
    } catch (e) {
      this.#promiseEngine = null;
      reject(e);
      throw e;
    }
    return this.#engine;
  }

  // for easier testing purpose
  async engineRun(request) {
    return await this.#engine.run(request);
  }

  // Helper to wrap createEngine for testing purpose
  createEngine(args) {
    return lazy.createEngine(args);
  }

  // Helper for test
  setEngine(mockEngine) {
    this.#engine = mockEngine;
  }
}
