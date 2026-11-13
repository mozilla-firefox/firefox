/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test for EmbeddingsGenerator.sys.mjs
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  EMBEDDING_TYPE: "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs",
  EmbeddingsGenerator: "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs",
  EmbeddingsGeneratorFactory:
    "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const EMBEDDING_SIZE = 256;

// Contextual embeddings are only selected on Mac/Windows, see
// isMacOrWindows() in EmbeddingsGenerator.sys.mjs.
const IS_MAC_OR_WINDOWS =
  AppConstants.platform === "macosx" || AppConstants.platform === "win";

const PREF_MULTILINGUAL_REGIONS =
  "places.semanticHistory.multilingualEmbeddingRegions";

/**
 * Runs `fn` with `Region.home` reporting the given region code.
 *
 * @param {string} region Home region code, or "" for "not detected yet".
 * @param {Function} fn Callback, awaited before the stub is restored.
 */
async function withHomeRegion(region, fn) {
  const stub = sinon.stub(Region, "home").get(() => region);
  try {
    await fn();
  } finally {
    stub.restore();
  }
}

/**
 * Runs `fn` with `Services.locale.appLocaleAsBCP47` reporting the given locale.
 *
 * @param {string} locale BCP 47 language tag.
 * @param {Function} fn Callback, awaited before the locale is restored.
 */
async function withAppLocale(locale, fn) {
  const availableLocales = Services.locale.availableLocales;
  const requestedLocales = Services.locale.requestedLocales;
  Services.locale.availableLocales = [locale];
  Services.locale.requestedLocales = [locale];
  Assert.equal(
    Services.locale.appLocaleAsBCP47,
    locale,
    `App locale is now ${locale}`
  );
  try {
    await fn();
  } finally {
    Services.locale.requestedLocales = requestedLocales;
    Services.locale.availableLocales = availableLocales;
  }
}

async function setup() {
  const { removeMocks, remoteClients } = await createAndMockMLRemoteSettings({
    autoDownloadFromRemoteSettings: false,
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      // Enabled by default.
      ["browser.ml.enable", true],
      ["browser.ml.logLevel", "All"],
      ["browser.ml.modelCacheTimeout", 1000],
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
    },
  };
}

add_task(async function test_EmbeddingsGenerator_for_minimum_physical_memory() {
  let embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  Assert.ok(
    embeddingsGenerator.isEnoughPhysicalMemoryAvailable(),
    "Physical Memory size < 7GiB."
  );
});

add_task(async function test_EmbeddingsGenerator_for_minimum_cpu_cores() {
  let embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  Assert.ok(
    embeddingsGenerator.isEnoughCpuCoresAvailable(),
    "Number CPU cores < 2."
  );
});

class MockMLEngineForEmbedMany {
  constructor(is_static_embedding = false) {
    this.is_static_embedding = is_static_embedding;
  }

  async run(request) {
    // Contextual embedding engine has an additional array wrapping
    let texts = this.is_static_embedding ? request.args : request.args[0];
    return texts.map(text => {
      if (typeof text !== "string" || text.trim() === "") {
        throw new Error("Invalid input: text must be a non-empty string");
      }
      // Return a mock embedding vector (e.g., an array of zeros)
      return Array(EMBEDDING_SIZE).fill(0);
    });
  }
}

add_task(async function test_embedMany_valid_inputs() {
  // Pin the family: forPlaces() otherwise depends on the machine's home
  // region, and the static/contextual engines take differently shaped args.
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", "static"]],
  });
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forPlaces();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany(true);
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany(true));

  const texts = ["mdn documentation", "jira board"];
  const result = await embeddingsGenerator.embedMany(texts);

  Assert.ok(Array.isArray(result), "Result should be an array");
  Assert.equal(result.length, 2, "Should return 2 embeddings");
  for (const vector of result) {
    Assert.equal(vector.length, EMBEDDING_SIZE, "Check embeddings dimension");
  }

  sinon.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_embedMany_empty_array_input() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let threw = false;
  try {
    await embeddingsGenerator.embedMany([]);
  } catch (e) {
    threw = true;
    Assert.ok(
      e.message.includes("empty array"),
      "Should throw for empty array input"
    );
  }
  Assert.ok(threw, "Error should be thrown for empty array input");

  sinon.restore();
});

add_task(async function test_embedMany_invalid_input_null() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let caught = false;
  try {
    await embeddingsGenerator.embedMany([null, "hello"]);
  } catch (e) {
    caught = true;
    Assert.ok(e.message.includes("Invalid input"), "Should throw for null");
  }
  Assert.ok(caught, "Error should be thrown");

  sinon.restore();
});

add_task(async function test_embedMany_invalid_input_nonstring() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbedMany();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbedMany());

  let caught = false;
  try {
    await embeddingsGenerator.embedMany(["hello", 123]);
  } catch (e) {
    caught = true;
    Assert.ok(
      e.message.includes("Invalid input"),
      "Should throw for non-string"
    );
  }
  Assert.ok(caught, "Error should be thrown");

  sinon.restore();
});

class MockMLEngineForEmbed {
  async run(request) {
    const texts = [request.args[0]];
    return texts.map(text => {
      if (typeof text !== "string" || text.trim() === "") {
        throw new Error("Invalid input: text must be a non-empty string");
      }
      // Return a mock embedding vector (e.g., an array of zeros)
      return Array(EMBEDDING_SIZE).fill(0);
    });
  }
}

add_task(async function test_embed_valid_input() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbed();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbed());

  const result = await embeddingsGenerator.embed("test string");

  Assert.ok(Array.isArray(result), "Embedding result should be an array");
  Assert.equal(result[0].length, EMBEDDING_SIZE, "Check embedding dimension");

  sinon.restore();
});

add_task(async function test_embed_invalid_input_empty_string() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();
  sinon.stub(embeddingsGenerator, "createEngineIfNotPresent").callsFake(() => {
    return new MockMLEngineForEmbed();
  });
  embeddingsGenerator.setEngine(new MockMLEngineForEmbed());

  let threw = false;
  try {
    await embeddingsGenerator.embed("");
  } catch (e) {
    threw = true;
    Assert.ok(
      e.message.includes("Invalid input"),
      "Should throw for empty string"
    );
  }
  Assert.ok(threw, "Error should be thrown for empty string");

  sinon.restore();
});

add_task(async function test_onnx() {
  const embeddingsGenerator = EmbeddingsGenerator.forTest({
    type: EMBEDDING_TYPE.CONTEXTUAL,
  });

  Assert.equal(
    embeddingsGenerator.options.backend,
    "best-onnx",
    "Contextual resolves to the best-onnx sentinel backend"
  );
  Assert.equal(
    embeddingsGenerator.embeddingSize,
    384,
    "Default contextual dim comes from the engine's preferredDimension"
  );
});

add_task(async function test_forPlaces_prefDrivesContextual() {
  // forPlaces() reads `places.semanticHistory.embeddingType`. Setting it to
  // "contextual" picks the onnx engine only on Mac/Windows; on other
  // platforms (e.g. Linux) it falls back to static-embeddings.
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", "contextual"]],
  });
  try {
    const contextual = new EmbeddingsGeneratorFactory().forPlaces();
    if (IS_MAC_OR_WINDOWS) {
      Assert.equal(
        contextual.options.backend,
        "best-onnx",
        "forPlaces + 'contextual' pref resolves to best-onnx on Mac/Windows"
      );
      Assert.equal(
        contextual.embeddingSize,
        384,
        "Contextual dim defaults to 384 when no override pref is set"
      );
    } else {
      Assert.equal(
        contextual.options.backend,
        "static-embeddings",
        "forPlaces + 'contextual' falls back to static on non-Mac/Windows"
      );
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_forGeneral_returnsContextualEmbeddings() {
  const eg = new EmbeddingsGeneratorFactory().forGeneral();
  Assert.equal(
    eg.options.backend,
    "best-onnx",
    `forGeneral always uses best-onnx, got ${eg.options.backend}`
  );
  Assert.equal(
    eg.options.embeddingDimension,
    384,
    "forGeneral uses the contextual (384)"
  );
});

add_task(async function test_forPlaces_explicitStaticPref() {
  await withHomeRegion("FR", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["places.semanticHistory.embeddingType", "static"]],
    });
    try {
      const eg = new EmbeddingsGeneratorFactory().forPlaces();
      Assert.equal(
        eg.options.backend,
        "static-embeddings",
        "An explicit 'static' pref wins over the region default"
      );
      Assert.equal(
        eg.options.embeddingDimension,
        512,
        "forPlaces static path uses the engine's preferredDimension (512)"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });
});

add_task(async function test_forPlaces_regionDefaults() {
  // With no pref value, the embedding family is derived from the home region
  // and the app locale (en-US here). Non-English markets (currently just FR)
  // get contextual embeddings on Mac/Windows; everything else stays on static.
  const cases = [
    { region: "FR", contextual: true, desc: "non-English market" },
    { region: "fr", contextual: true, desc: "lowercased region code" },
    { region: "US", contextual: false, desc: "English market" },
    { region: null, contextual: false, desc: "region not detected yet" },
  ];

  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", ""]],
  });
  try {
    for (const { region, contextual, desc } of cases) {
      await withHomeRegion(region, () => {
        const eg = new EmbeddingsGeneratorFactory().forPlaces();
        const expected =
          contextual && IS_MAC_OR_WINDOWS ? "best-onnx" : "static-embeddings";
        Assert.equal(
          eg.options.backend,
          expected,
          `Region "${region}" (${desc}) resolves to ${expected}`
        );
      });
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_forPlaces_multilingualRegionsPref() {
  // The default pref value is '[["FR",["en-*","fr-*"]],["*",["fr-*"]]]', so
  // either France in English or French, or a French locale in any region.
  const cases = [
    { region: "FR", locale: "en-US", contextual: true, desc: "FR + en-*" },
    { region: "FR", locale: "fr-FR", contextual: true, desc: "FR + fr-*" },
    { region: "FR", locale: "de-DE", contextual: false, desc: "FR + de-*" },
    {
      region: "US",
      locale: "fr-FR",
      contextual: true,
      desc: "wildcard region",
    },
    { region: "US", locale: "en-US", contextual: false, desc: "US + en-*" },
    {
      region: null,
      locale: "fr-FR",
      contextual: true,
      desc: "wildcard region applies before the region resolves",
    },
    { region: null, locale: "en-US", contextual: false, desc: "no match" },
  ];

  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", ""]],
  });
  try {
    for (const { region, locale, contextual, desc } of cases) {
      await withAppLocale(locale, () =>
        withHomeRegion(region, () => {
          const expected =
            contextual && IS_MAC_OR_WINDOWS ? "best-onnx" : "static-embeddings";
          Assert.equal(
            new EmbeddingsGeneratorFactory().forPlaces().options.backend,
            expected,
            `Region "${region}" + locale "${locale}" (${desc}) resolves to ${expected}`
          );
        })
      );
    }
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_forPlaces_multilingualRegionsPrefOverrides() {
  // The region/locale list is Nimbus-settable, so a value that does not mention
  // FR must move the multilingual markets with it. An emptied or unparseable
  // value must not throw: empty disables multilingual embeddings everywhere,
  // junk falls back to the built-in defaults.
  const cases = [
    {
      setPref: '[["DE",["de-*"]]]',
      region: "DE",
      locale: "de-DE",
      contextual: true,
      desc: "custom region",
    },
    {
      setPref: '[["DE",["de-*"]]]',
      region: "FR",
      locale: "fr-FR",
      contextual: false,
      desc: "FR dropped from a custom list",
    },
    {
      setPref: '[["*",["fr"]]]',
      region: "US",
      locale: "fr",
      contextual: true,
      desc: "exact locale match, no wildcard",
    },
    {
      setPref: '[["*",["fr"]]]',
      region: "US",
      locale: "fr-FR",
      contextual: false,
      desc: "exact locale pattern does not match a variant",
    },
    {
      setPref: "",
      region: "FR",
      locale: "fr-FR",
      contextual: false,
      desc: "empty pref disables multilingual embeddings",
    },
    {
      setPref: "not json",
      region: "FR",
      locale: "fr-FR",
      contextual: true,
      desc: "invalid json falls back to the defaults",
    },
  ];

  for (const { setPref, region, locale, contextual, desc } of cases) {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["places.semanticHistory.embeddingType", ""],
        [PREF_MULTILINGUAL_REGIONS, setPref],
      ],
    });
    try {
      await withAppLocale(locale, () =>
        withHomeRegion(region, () => {
          const expected =
            contextual && IS_MAC_OR_WINDOWS ? "best-onnx" : "static-embeddings";
          Assert.equal(
            new EmbeddingsGeneratorFactory().forPlaces().options.backend,
            expected,
            `Pref "${setPref}" with region "${region}" + locale "${locale}" (${desc}) resolves to ${expected}`
          );
        })
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  }
});

add_task(async function test_factorySingletonIsShared() {
  // Freezing the region is only meaningful if every production caller shares
  // one factory, so guard the singleton against being turned into a per-caller
  // instance.
  const a = ChromeUtils.importESModule(
    "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs"
  );
  const b = ChromeUtils.importESModule(
    "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs"
  );
  Assert.strictEqual(
    a.embeddingsGeneratorFactory,
    b.embeddingsGeneratorFactory,
    "Separate imports resolve to the same factory instance"
  );
  Assert.ok(
    a.embeddingsGeneratorFactory instanceof EmbeddingsGeneratorFactory,
    "The exported singleton is an EmbeddingsGeneratorFactory"
  );
});

add_task(async function test_factory_freezesRegionOnFirstUse() {
  // Persisted embeddings are only comparable within one model, so a factory
  // must keep handing out the same embedding family even if the home region
  // changes underneath it.
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", ""]],
  });
  try {
    const factory = new EmbeddingsGeneratorFactory();
    const expected = IS_MAC_OR_WINDOWS ? "best-onnx" : "static-embeddings";

    await withHomeRegion("FR", () => {
      Assert.equal(
        factory.forPlaces().options.backend,
        expected,
        "First use captures the FR region"
      );
    });

    await withHomeRegion("US", () => {
      Assert.equal(
        factory.forPlaces().options.backend,
        expected,
        "A later region change does not alter the embedding family"
      );
      Assert.equal(factory.region, "FR", "The captured region stays frozen");
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_factory_freezesNullRegion() {
  // A region that has not resolved yet is frozen too: it is corrected on the
  // next startup rather than mid-session.
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", ""]],
  });
  try {
    const factory = new EmbeddingsGeneratorFactory();

    await withHomeRegion(null, () => {
      Assert.equal(
        factory.forPlaces().options.backend,
        "static-embeddings",
        "An unresolved region falls back to static"
      );
    });

    await withHomeRegion("FR", () => {
      Assert.equal(
        factory.forPlaces().options.backend,
        "static-embeddings",
        "Resolving to FR later does not switch the family mid-session"
      );
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_forPlaces_invalidPrefFallsBackToRegion() {
  // A junk pref value must not leak into resolveEngineOptions(), which would
  // throw "Unknown embedding type".
  await SpecialPowers.pushPrefEnv({
    set: [["places.semanticHistory.embeddingType", "bogus"]],
  });
  try {
    await withHomeRegion("US", () => {
      Assert.equal(
        new EmbeddingsGeneratorFactory().forPlaces().options.backend,
        "static-embeddings",
        "An unrecognized pref value falls back to the region default"
      );
    });
    await withHomeRegion("FR", () => {
      Assert.equal(
        new EmbeddingsGeneratorFactory().forPlaces().options.backend,
        IS_MAC_OR_WINDOWS ? "best-onnx" : "static-embeddings",
        "An unrecognized pref value falls back to the region default in FR"
      );
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_forTest_rejectsUnsupportedDimensions() {
  Assert.throws(
    () =>
      EmbeddingsGenerator.forTest({
        type: EMBEDDING_TYPE.STATIC,
        embeddingSize: 384,
      }),
    /Unsupported static embedding size/,
    "Static engine only accepts its supportedDimensions"
  );

  Assert.throws(
    () =>
      EmbeddingsGenerator.forTest({
        type: EMBEDDING_TYPE.CONTEXTUAL,
        embeddingSize: 100,
      }),
    /Unsupported contextual embedding size/,
    "Contextual dims must be a multiple of 8 within [128, 2048]"
  );

  Assert.throws(
    () => EmbeddingsGenerator.forTest({ type: "nope" }),
    /Unknown embedding type/,
    "Unknown embedding types are rejected"
  );
});

add_task(async function test_contextual_devPrefOverrides() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.embedGen.textEmbeddingSize", 512],
      ["browser.ml.embedGen.textEmbeddingFeatureModel", "test/model"],
    ],
  });
  try {
    const eg = EmbeddingsGenerator.forTest({
      type: EMBEDDING_TYPE.CONTEXTUAL,
    });
    Assert.equal(
      eg.embeddingSize,
      512,
      "Contextual dimension comes from the dev pref"
    );
    Assert.equal(
      eg.options.modelId,
      "test/model",
      "Contextual modelId comes from the dev pref"
    );
    Assert.deepEqual(
      eg.modelContext,
      {
        featureId: "simple-text-embedder",
        embeddingDimension: 512,
        modelId: "test/model",
      },
      "modelContext reflects the overridden model configuration"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_contextual_hasNoManualFallbackEngine() {
  // best-onnx resolves native -> wasm inside MLEngineChild, so the generator
  // must not carry (or act on) its own fallback engine config.
  for (const eg of [
    new EmbeddingsGeneratorFactory().forGeneral(),
    EmbeddingsGenerator.forTest({ type: EMBEDDING_TYPE.CONTEXTUAL }),
  ]) {
    Assert.equal(
      eg.options.fallbackEngine,
      undefined,
      "The contextual engine config declares no manual fallback"
    );
  }
});

add_task(
  async function test_ensureEngine_all_concurrent_callers_reject_on_failure() {
    const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();

    sinon
      .stub(embeddingsGenerator, "createEngineIfNotPresent")
      .callsFake(async () => {
        throw new Error("Engine init failed");
      });

    const p1 = embeddingsGenerator.ensureEngine();
    const p2 = embeddingsGenerator.ensureEngine();
    const p3 = embeddingsGenerator.ensureEngine();

    const [r1, r2, r3] = await Promise.allSettled([p1, p2, p3]);

    for (const result of [r1, r2, r3]) {
      Assert.equal(
        result.status,
        "rejected",
        "All callers should reject on failure"
      );
      Assert.ok(
        result.reason.message.includes("Engine init failed"),
        "All callers should receive the original error"
      );
    }

    sinon.restore();
  }
);

add_task(async function test_ensureEngine_allows_retry_after_failure() {
  const embeddingsGenerator = new EmbeddingsGeneratorFactory().forGeneral();

  let callCount = 0;
  sinon
    .stub(embeddingsGenerator, "createEngineIfNotPresent")
    .callsFake(async () => {
      callCount++;
      if (callCount === 1) {
        throw new Error("Engine init failed");
      }
    });

  let threw = false;
  try {
    await embeddingsGenerator.ensureEngine();
  } catch (e) {
    threw = true;
  }
  Assert.ok(threw, "First call should reject on failure");
  Assert.equal(callCount, 1, "createEngineIfNotPresent was called once");

  await embeddingsGenerator.ensureEngine();
  Assert.equal(
    callCount,
    2,
    "createEngineIfNotPresent should be retried after failure"
  );

  sinon.restore();
});

/**
 * The contextual engine shuts down after browser.ml.modelCacheTimeout of idle,
 * and the generator still embeds afterwards. The test swaps only the model, for
 * test-echo.
 */
add_task(async function test_contextual_engine_shuts_down_when_idle() {
  const { cleanup } = await setup();
  const embeddingsGenerator = EmbeddingsGenerator.forTest({
    type: EMBEDDING_TYPE.CONTEXTUAL,
  });
  embeddingsGenerator.options = {
    ...embeddingsGenerator.options,
    modelId: "test-echo",
  };

  Assert.deepEqual(
    (await embeddingsGenerator.embed("before going idle")).output,
    ["before going idle"],
    "The contextual engine embeds text"
  );
  await TestUtils.waitForCondition(
    () => EngineProcess.areAllEnginesTerminated(),
    "The contextual engine shuts down after browser.ml.modelCacheTimeout of idle"
  );
  Assert.deepEqual(
    (await embeddingsGenerator.embed("after going idle")).output,
    ["after going idle"],
    "The generator still embeds text after its engine shut down for being idle"
  );

  await EngineProcess.destroyMLEngine();
  await cleanup();
  await SpecialPowers.popPrefEnv();
});
