/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { PipelineOptions } from "chrome://global/content/ml/EngineProcess.sys.mjs"
 * @import { BackendError } from "./Pipeline.mjs"
 * @import { MLEngineWorker } from "../MLEngine.worker.mjs"
 * @import { TypedArray } from "../../ml.d.ts"
 * @import { EmbeddingDType, EmbeddingRequest, EmbeddingResponse, PreTrainedTokenizer } from "./StaticEmbeddingsPipeline.d.ts"
 */

/**
 * @typedef {object} Lazy
 * @property {typeof import("chrome://global/content/ml/Utils.sys.mjs").createFileUrl} createFileUrl
 * @property {typeof import("chrome://global/content/ml/Utils.sys.mjs").parseNpy} parseNpy
 * @property {typeof import("chrome://global/content/ml/OPFS.sys.mjs").OPFS} OPFS
 * @property {typeof import("chrome://global/content/ml/backends/ONNXPipeline.mjs").importTransformers} importTransformers
 * @property {typeof import("chrome://global/content/ml/EngineProcess.sys.mjs").QuantizationLevel} QuantizationLevel
 */

/** @type {Lazy} */
const lazy = /** @type {any} */ ({});

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    createFileUrl: "chrome://global/content/ml/Utils.sys.mjs",
    parseNpy: "chrome://global/content/ml/Utils.sys.mjs",
    OPFS: "chrome://global/content/ml/OPFS.sys.mjs",
    importTransformers: "chrome://global/content/ml/backends/ONNXPipeline.mjs",
    QuantizationLevel: "chrome://global/content/ml/EngineProcess.sys.mjs",
  },
  { global: "current" }
);

/**
 * Mock out a response object for tests. This should have the same type as @see {Response}
 */
class MockedResponse {
  /** @type {any} */
  #value;
  /**
   * @param {any} value
   */
  constructor(value) {
    this.#value = value;
  }

  /**
   * @returns {ReturnType<Response["json"]>}
   */
  json() {
    return this.#value;
  }

  /**
   * @returns {ReturnType<Response["arrayBuffer"]>}
   */
  arrayBuffer() {
    return this.#value;
  }
}

/**
 * Embeddings are typically generated through running text through a BERT-like model
 * that is an encoder-only transformer. However, this is expensive and slow. Static
 * embeddings allow for a cheaper way to generate an embedding by just averaging the
 * values of each token's embedding vector. This involves a simple lookup per token, and
 * then some vector math. These embeddings are often good enough for looking up
 * semantically similar values.
 */
export class StaticEmbeddingsPipeline {
  /** @type {PreTrainedTokenizer} */
  #tokenizer;

  /**
   * The embedding dimensions size, e.g. 128, 256, 512
   *
   * @type {number}
   */
  #dimensions;

  /** @type {number | null} */
  #initializeStart;

  /**
   * Snapshots captured during initialization, prepended onto the per-run
   * runTimestamps timeline when a request completes.
   *
   * @type {Array<{name: string, when: number}>}
   */
  #metrics = [];

  /**
   * Get a native JS double out of the backing data array.
   *
   * @type {(index: number) => number}
   */
  #getFloat;

  /**
   * @param {object} entry
   * @param {string} entry.name
   * @param {{when?: number}} [entry.snapshot]
   */
  #metricsSnapShot({ name, snapshot = {} }) {
    if (!("when" in snapshot)) {
      snapshot.when = ChromeUtils.now();
    }
    this.#metrics.push({ name, ...snapshot });
  }

  /**
   * @type {TypedArray}
   */
  embeddings;

  /**
   * @param {PreTrainedTokenizer} tokenizer
   * @param {ArrayBuffer} npyData
   * @param {EmbeddingDType} dtype
   * @param {number} dimensions
   * @param {number} initializeStart
   */
  constructor(tokenizer, npyData, dtype, dimensions, initializeStart) {
    this.#tokenizer = tokenizer;
    this.#dimensions = dimensions;
    this.#initializeStart = initializeStart;

    const {
      data: embeddings,
      shape: [vocabSize, dimActual],
    } = lazy.parseNpy(npyData);

    if (dimActual != this.#dimensions) {
      throw new Error(
        `The dimensions requested (${this.#dimensions}) and the dimensions received (${dimActual}) did not match`
      );
    }
    if (tokenizer.model.vocab.length != vocabSize) {
      throw new Error(
        `The tokenizer vocab size (${this.#dimensions}) did not match the data vocab size (${vocabSize})`
      );
    }

    this.embeddings = embeddings;

    switch (dtype) {
      case lazy.QuantizationLevel.FP32:
      case lazy.QuantizationLevel.FP16:
        // No processing is needed.
        this.#getFloat = index => this.embeddings[index];
        break;
      case lazy.QuantizationLevel.FP8_E5M2:
        this.#getFloat = this.#getFp8_E5M2;
        break;
      case lazy.QuantizationLevel.FP8_E4M3:
        this.#getFloat = this.#getFp8_E4M3;
        break;
      default:
        throw new Error("Unsupported dtype: " + dtype);
    }
  }

  /**
   * @param {MLEngineWorker} worker
   * @param {null} _wasm
   * @param {PipelineOptions} pipelineOptions
   * @param {(error: any) => BackendError} _createError
   */
  static async initialize(worker, _wasm, pipelineOptions, _createError) {
    let initializeStart = ChromeUtils.now();

    const {
      backend,
      modelHubRootUrl,
      modelHubUrlTemplate,
      modelId,
      modelRevision,
      staticEmbeddingsOptions,
    } = pipelineOptions;

    if (!staticEmbeddingsOptions) {
      throw new Error("No staticEmbeddingsOptions were provided.");
    }
    if (!modelId) {
      throw new Error("No modelId was provided.");
    }
    if (!modelRevision) {
      throw new Error("No modelRevision was provided.");
    }
    if (!modelHubUrlTemplate) {
      throw new Error("No modelHubUrlTemplate was provided.");
    }
    if (!modelHubRootUrl) {
      throw new Error("No modelHubRootUrl was provided.");
    }
    if (!backend) {
      throw new Error("No backend was provided.");
    }

    // These are the options that are specific to this engine.
    const { subfolder, dtype, dimensions, compression, mockedValues } =
      staticEmbeddingsOptions;

    const extension = compression ? ".zst" : "";

    const files = [
      `${subfolder}/tokenizer.json${extension}`,
      `${subfolder}/${dtype}.d${dimensions}.npy${extension}`,
    ];

    /**
     * @param {string} fileName
     * @returns {Promise<Response | MockedResponse>}
     */
    const getResponse = async fileName => {
      const url = lazy.createFileUrl({
        file: fileName,
        model: modelId,
        revision: modelRevision,
        urlTemplate: modelHubUrlTemplate,
        rootUrl: modelHubRootUrl,
      });
      if (mockedValues) {
        const mockedValue = mockedValues[url];
        if (!mockedValue) {
          throw new Error(
            "Could not find mocked value for requested url: " + url
          );
        }
        if (url.endsWith(`.json${extension}`)) {
          return new MockedResponse(mockedValue);
        }
        return new MockedResponse(new Uint8Array(mockedValue).buffer);
      }
      const modelFile = await worker.getModelFile({ url });
      const filePath = modelFile.ok[2];
      const opfsStart = ChromeUtils.now();
      const fileHandle = await lazy.OPFS.getFileHandle(filePath);
      const file = await fileHandle.getFile();
      ChromeUtils.addProfilerMarker(
        "MLEngine:OPFS",
        { startTime: opfsStart },
        `Retrieved model file from OPFS`
      );

      let stream = file.stream();
      if (compression) {
        const decompressionStream = new DecompressionStream("zstd");
        stream = stream.pipeThrough(decompressionStream);
      }
      return new Response(stream);
    };

    const [tokenizerJsonResponse, npyDataResponse] = await Promise.all(
      files.map(getResponse)
    );

    const npyData = await npyDataResponse.arrayBuffer();
    const tokenizerJson = await tokenizerJsonResponse.json();

    let assetsLoad = ChromeUtils.now();
    ChromeUtils.addProfilerMarker(
      "StaticEmbeddingsPipeline",
      initializeStart,
      "Assets load"
    );
    const { PreTrainedTokenizer } = await lazy.importTransformers(backend);
    const tokenizer = new PreTrainedTokenizer(tokenizerJson, {});
    ChromeUtils.addProfilerMarker(
      "StaticEmbeddingsPipeline",
      assetsLoad,
      "Tokenizer load"
    );

    const pipeline = new StaticEmbeddingsPipeline(
      tokenizer,
      npyData,
      dtype,
      dimensions,
      initializeStart
    );
    pipeline.#metricsSnapShot({
      name: "initializationStart",
      snapshot: { when: initializeStart },
    });
    pipeline.#metricsSnapShot({ name: "initializationEnd" });
    return pipeline;
  }

  /**
   * @param {number} index
   */
  #getFp8_E5M2 = index => {
    const byte = this.embeddings[index];

    // Do some bit manipulation to extract the sign (S), the exponent (E), and the
    // mantissa (M)
    // This is format: | S E E E | E E M M |
    // To do the manipulation, shift the bits to the right (>>) and mask off the relevant
    // bits with an & operation.
    const sign = (byte >> 7) & 0b0000_0001;
    const exponent = (byte >> 2) & 0b0001_1111;
    const mantissa = byte & 0b0000_0011;
    const bias = 15;

    if (exponent === 0) {
      if (mantissa === 0) {
        // Zero
        return sign ? -0 : 0;
      }
      // Subnormal: exponent = 1 - bias, no implicit leading 1
      const frac = mantissa / 4; // 2 mantissa bits → divide by 2^2
      const value = frac * Math.pow(2, 1 - bias);
      return sign ? -value : value;
    } else if (exponent === 0x1f) {
      if (mantissa === 0) {
        return sign ? -Infinity : Infinity;
      }
      return NaN;
    }
    // Normalized
    const frac = 1 + mantissa / 4;
    const value = frac * Math.pow(2, exponent - bias);
    return sign ? -value : value;
  };

  /**
   * @param {number} index
   */
  #getFp8_E4M3 = index => {
    const byte = this.embeddings[index];

    // Do some bit manipulation to extract the sign (S), the exponent (E), and the
    // mantissa (M)
    // This is format: | S E E E | E M M M |
    // To do the manipulation, shift the bits to the right (>>) and mask off the relevant
    // bits with an & operation.
    const sign = (byte >> 7) & 0b0000_0001;
    const exponent = (byte >> 3) & 0b0000_1111;
    const mantissa = byte & 0b0000_0111;
    const bias = 7;

    if (exponent === 0) {
      if (mantissa === 0) {
        return sign ? -0 : 0;
      }
      // Subnormal
      const frac = mantissa / 8; // 3 mantissa bits → divide by 2^3
      const value = frac * Math.pow(2, 1 - bias);
      return sign ? -value : value;
    } else if (exponent === 0xf) {
      if (mantissa === 0) {
        return sign ? -Infinity : Infinity;
      }
      return NaN;
    }

    // Normalized
    const frac = 1 + mantissa / 8;
    const value = frac * Math.pow(2, exponent - bias);

    return sign ? -value : value;
  };

  /**
   * @param {EmbeddingRequest} request
   * @param {number} _requestId
   * @param {null} _engineRunOptions
   * @returns {EmbeddingResponse}
   */
  run(request, _requestId, _engineRunOptions) {
    if (request.options.pooling != "mean") {
      throw new Error(
        `Only "mean" pooling is currently supported, please add support "${request.options.pooling}" here.`
      );
    }

    let tokenCount = 0;
    let charCount = 0;
    const sequenceCount = request.args.length;

    const metrics = { runTimestamps: [] };
    const snapshot = (name, when = ChromeUtils.now()) => {
      metrics.runTimestamps.push({ name, when });
    };

    let beforeResponse = ChromeUtils.now();
    snapshot("runStart", beforeResponse);
    const output = request.args.map(text => {
      // Always do the vector math in f32 space, even if the underlying precision
      // is lower.
      const embedding = new Float32Array(this.#dimensions);
      charCount += text.length;

      let tokenizeStart = ChromeUtils.now();
      /** @type {number[]} */
      const tokenIds = this.#tokenizer.encode(text);
      ChromeUtils.addProfilerMarker(
        "MLEngine:StaticEmbeddings",
        { startTime: tokenizeStart },
        `Tokenized text: ${tokenIds.length} tokens`
      );
      tokenCount += tokenIds.length;

      // Sum up the embeddings.
      for (const tokenId of tokenIds) {
        for (let i = 0; i < this.#dimensions; i++) {
          // Inflate the double into a JavaScript double, then add it.
          embedding[i] += this.#getFloat(tokenId * this.#dimensions + i);
        }
      }

      if (request.options.normalize) {
        // Compute the average by dividing by the tokens provided.
        // Also compute the sum of the squares while we're here.
        let sumSquares = 0;
        for (let i = 0; i < this.#dimensions; i++) {
          const n = embedding[i] / tokenIds.length;
          embedding[i] = n;
          sumSquares += n * n;
        }

        // Apply the normalization.
        const magnitude = Math.sqrt(sumSquares);
        if (magnitude != 0) {
          for (let i = 0; i < this.#dimensions; i++) {
            embedding[i] = embedding[i] / magnitude;
          }
        }
      } else {
        // Only compute the average by dividing by the tokens provided.
        for (let i = 0; i < this.#dimensions; i++) {
          embedding[i] = embedding[i] / tokenIds.length;
        }
      }

      return embedding;
    });

    const endTime = ChromeUtils.now();
    snapshot("runEnd", endTime);
    const inferenceTime = endTime - beforeResponse;

    ChromeUtils.addProfilerMarker(
      "MLEngine:StaticEmbeddings",
      { startTime: beforeResponse },
      `Processing ${sequenceCount} sequences with ${tokenCount} tokens`
    );

    if (this.#initializeStart) {
      ChromeUtils.addProfilerMarker(
        "MLEngine:StaticEmbeddings",
        { startTime: this.#initializeStart },
        "Time to first response"
      );
      this.#initializeStart = null;
    }

    return {
      output,
      metrics: {
        runTimestamps: [...this.#metrics, ...metrics.runTimestamps],
        inputChars: charCount,
        inputTokens: tokenCount,
        inferenceTime,
        tokensPerSecond:
          inferenceTime > 0 && tokenCount > 0
            ? tokenCount / (inferenceTime / 1000)
            : undefined,
        charsPerSecond:
          inferenceTime > 0 && charCount > 0
            ? charCount / (inferenceTime / 1000)
            : undefined,
      },
    };
  }
}
