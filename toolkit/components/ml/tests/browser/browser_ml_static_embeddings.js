/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @import { Request as EngineRequest, MLEngine as MLEngineClass } from "../../actors/MLEngineParent.sys.mjs"
 * @import { StaticEmbeddingsOptions } from "../../content/backends/StaticEmbeddingsPipeline.d.ts"
 */

const { parseNpy } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);
const { EMBEDDING_TYPE, EmbeddingsGenerator } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs"
);

const vocabSize = 9;
const dimensions = 8;

/**
 * Mock out the URL requests with a small bad embeddings model.
 */
function getMockedValues() {
  const { encoding } = generateFloat16Numpy(vocabSize, dimensions);
  const tokenizer =
    // prettier-ignore
    {
      version: "1.0",
      truncation: null,
      padding: null,
      added_tokens: [{ id: 0, content: "[UNK]", single_word: false, lstrip: false, rstrip: false, normalized: false, special: true }],
      normalizer: { type: "BertNormalizer", clean_text: true, handle_chinese_chars: true, strip_accents: null, lowercase: true },
      pre_tokenizer: { type: "BertPreTokenizer" },
      post_processor: {
        type: "TemplateProcessing",
        single: [
          { SpecialToken: { id: "[CLS]", type_id: 0 } },
          { Sequence: { id: "A", type_id: 0 } },
          { SpecialToken: { id: "[SEP]", type_id: 0 } },
        ],
        pair: [],
        special_tokens: {},
      },
      decoder: { type: "WordPiece", prefix: "##", cleanup: true },
      model: {
        type: "WordPiece", unk_token: "[UNK]", continuing_subword_prefix: "##", max_input_chars_per_word: 100,
        vocab: { "[UNK]": 0, the: 1, quick: 2, brown: 3, dog: 4, jumped: 5, over: 6, lazy: 7, fox: 8 },
      },
    };

  return {
    "https://model-hub.mozilla.org/mozilla/static-embeddings/v1.0.0/models/minishlab/potion-retrieval-32M/tokenizer.json":
      tokenizer,
    [`https://model-hub.mozilla.org/mozilla/static-embeddings/v1.0.0/models/minishlab/potion-retrieval-32M/fp16.d${dimensions}.npy`]:
      encoding,
  };
}

add_task(async function test_static_embeddings() {
  /** @type {StaticEmbeddingsOptions} */
  const staticEmbeddingsOptions = {
    dtype: "fp16",
    subfolder: "models/minishlab/potion-retrieval-32M",
    dimensions,
    mockedValues: getMockedValues(),
    compression: false,
  };

  /** @type {MLEngineClass} */
  const engine = await createEngine(
    new PipelineOptions({
      featureId: "simple-text-embedder",
      engineId: "test-static-embeddings",

      modelId: "mozilla/static-embeddings",
      modelRevision: "v1.0.0",
      taskName: "static-embeddings",
      modelHub: "mozilla",
      backend: "static-embeddings",

      staticEmbeddingsOptions,
    })
  );

  const inputText = "The quick brown fox jumped over the lazy fox";
  const { output, metrics } = await engine.run({
    args: [inputText],
    options: {
      pooling: "mean",
      normalize: true,
    },
  });

  is(output.length, 1, "One embedding was returned");
  const [embedding] = output;
  is(embedding.length, dimensions, "The dimensions match");
  is(
    embedding.constructor.name,
    "Float32Array",
    "The embedding was returned as a Float32Array"
  );

  assertFloatArraysMatch(
    embedding,
    [
      0.3156551122, 0.3262447714, 0.3368626534, 0.3474076688, 0.3580137789,
      0.3685869872, 0.3791790008, 0.3898085951,
    ],
    "The embeddings were computed as expected.",
    0.00001 // epsilon
  );

  Assert.ok(metrics, "metrics should be present on the run result");
  Assert.ok(
    Array.isArray(metrics.runTimestamps),
    "metrics.runTimestamps should be an array"
  );
  const timestampNames = metrics.runTimestamps.map(t => t.name);
  for (const name of [
    "initializationStart",
    "initializationEnd",
    "runStart",
    "runEnd",
  ]) {
    Assert.ok(
      timestampNames.includes(name),
      `metrics.runTimestamps should include ${name}`
    );
  }
  Assert.equal(
    metrics.inputChars,
    inputText.length,
    "inputChars should equal the input string length"
  );
  Assert.greater(metrics.inputTokens, 0, "inputTokens should be > 0");
  Assert.greaterOrEqual(
    metrics.inferenceTime,
    0,
    "inferenceTime should be >= 0"
  );

  // inputChars measures UTF-16 code units, so a non-BMP character like an
  // emoji (encoded as a surrogate pair) contributes 2 to inputChars.
  const emojiText = "😄";
  Assert.equal(emojiText.length, 2, "The emoji should be 2 UTF-16 code units");
  const { metrics: emojiMetrics } = await engine.run({
    args: [emojiText],
    options: {
      pooling: "mean",
      normalize: true,
    },
  });
  Assert.equal(
    emojiMetrics.inputChars,
    emojiText.length,
    "inputChars should equal text.length (UTF-16 code units) for non-BMP input"
  );
});

/**
 * Creating the contextual embeddings engine leaves a live static embeddings
 * engine running. Like test_static_embeddings, it mocks only the model download.
 */
add_task(async function test_static_engine_survives_contextual_engine() {
  const staticGenerator = EmbeddingsGenerator.forTest({
    type: EMBEDDING_TYPE.STATIC,
  });
  staticGenerator.options.staticEmbeddingsOptions = {
    ...staticGenerator.options.staticEmbeddingsOptions,
    dimensions,
    mockedValues: getMockedValues(),
    compression: false,
  };
  const staticEngine = await staticGenerator.ensureEngine();

  let contextualSlotEngine;
  try {
    // The contextual model can't run in tests, so the static model runs under
    // the contextual engineId. A shared engineId would then replace the static
    // engine, since numThreads differs.
    const contextualOptions = new PipelineOptions(
      EmbeddingsGenerator.forTest({ type: EMBEDDING_TYPE.CONTEXTUAL }).options
    );
    contextualSlotEngine = await createEngine(
      new PipelineOptions({
        ...staticGenerator.options,
        engineId: contextualOptions.engineId,
        numThreads: contextualOptions.numThreads,
      })
    );

    is(
      staticEngine.engineStatus,
      "ready",
      "The static engine stays ready after the contextual engine starts"
    );
    const { output } = await staticEngine.run({
      args: ["the quick brown fox"],
      options: { pooling: "mean", normalize: true },
    });
    is(output[0].length, dimensions, "The static engine still runs its model");
  } finally {
    await contextualSlotEngine?.terminate();
    await staticEngine.terminate();
  }
});

/**
 * The static embeddings generator keeps embedding after its engine shuts down
 * for being idle. Like test_static_embeddings, it mocks only the model download.
 */
add_task(async function test_static_generator_embeds_after_idle_shutdown() {
  await EngineProcess.destroyMLEngine();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.modelCacheTimeout", 1000]],
  });
  const staticGenerator = EmbeddingsGenerator.forTest({
    type: EMBEDDING_TYPE.STATIC,
  });
  staticGenerator.options.staticEmbeddingsOptions = {
    ...staticGenerator.options.staticEmbeddingsOptions,
    dimensions,
    mockedValues: getMockedValues(),
    compression: false,
  };

  try {
    const before = await staticGenerator.embed("the quick brown fox");
    is(
      before.output[0].length,
      dimensions,
      "The static generator embeds text before going idle"
    );
    const engine = await staticGenerator.ensureEngine();
    is(engine.engineStatus, "ready", "The static engine starts ready");

    await TestUtils.waitForCondition(
      () => EngineProcess.areAllEnginesTerminated(),
      "The inference process shuts down the idle static engine"
    );
    is(
      engine.engineStatus,
      "suspended",
      "The static engine is suspended after the idle shutdown"
    );

    const after = await staticGenerator.embed("the quick brown fox");
    Assert.deepEqual(
      after.output,
      before.output,
      "The static generator embeds the same text the same way after going idle"
    );
    is(
      MLEngine.getInstance(engine.engineId)?.engineStatus,
      "ready",
      "Embedding after the idle shutdown reopens a ready static engine"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await SpecialPowers.popPrefEnv();
  }
});
