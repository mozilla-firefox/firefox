/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test for MLSuggest.sys.mjs.
 */
"use strict";

ChromeUtils.defineESModuleGetters(this, {
  MLSuggest: "moz-src:///browser/components/urlbar/private/MLSuggest.sys.mjs",
});

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/head.js",
  this
);

let nerResultsMap = {};
function getNerResult(query, nerExample) {
  nerResultsMap[query] = nerExample.map(
    ([entity, index, word, score = 0.999]) => ({
      entity,
      index,
      word,
      score,
    })
  );
}

let nerExamples = {
  "restaurants in seattle, wa": [
    ["B-CITYSTATE", 3, "seattle"],
    ["I-CITYSTATE", 4, ","],
    ["I-CITYSTATE", 5, "wa"],
  ],
  "hotels in new york, ny": [
    ["B-CITYSTATE", 3, "new"],
    ["I-CITYSTATE", 4, "york"],
    ["I-CITYSTATE", 5, ","],
    ["I-CITYSTATE", 6, "ny"],
  ],
  "restaurants seattle": [["B-CITY", 2, "seattle"]],
  "restaurants in seattle": [["B-CITY", 3, "seattle"]],
  "restaurants near seattle": [["B-CITY", 3, "seattle"]],
  "seattle restaurants": [["B-CITY", 1, "seattle"]],
  "seattle wa restaurants": [
    ["B-CITY", 1, "seattle"],
    ["B-STATE", 2, "wa"],
  ],
  "seattle, wa restaurants": [
    ["B-CITYSTATE", 1, "seattle"],
    ["I-CITYSTATE", 2, ","],
    ["I-CITYSTATE", 3, "wa"],
  ],
  "dumplings in ca": [["B-STATE", 4, "ca"]],
  "ramen ra": [["B-CITY", 3, "ra"]],
  "ra ramen": [["B-CITY", 1, "ra"]],
  "ramen st. louis": [
    ["B-CITY", 3, "st"],
    ["I-CITY", 4, "."],
    ["I-CITY", 5, "louis"],
  ],
  "st. louis ramen": [
    ["B-CITY", 1, "st"],
    ["I-CITY", 2, "."],
    ["I-CITY", 3, "louis"],
  ],
  "ramen winston-salem": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
    ["I-CITY", 5, "salem"],
  ],
  "winston-salem ramen": [
    ["B-CITY", 1, "winston"],
    ["I-CITY", 2, "-"],
    ["I-CITY", 3, "salem"],
  ],
  "ramen hawai'i": [
    ["B-CITY", 3, "ha"],
    ["I-CITY", 4, "##wai"],
    ["I-CITY", 5, "'"],
    ["I-CITY", 6, "i"],
  ],
  "hawai'i ramen": [
    ["B-CITY", 1, "ha"],
    ["I-CITY", 2, "##wai"],
    ["I-CITY", 3, "'"],
    ["I-CITY", 4, "i"],
  ],
  "cafe in ca": [["B-STATE", 3, "ca"]],
  "ramen noodles ra ca": [
    ["B-CITY", 4, "ra"],
    ["B-STATE", 5, "ca"],
  ],
  "ra ca ramen noodles": [
    ["B-CITY", 1, "ra"],
    ["B-STATE", 2, "ca"],
  ],
  "plumbers in seattle,wa": [
    ["B-CITYSTATE", 4, "seattle"],
    ["I-CITYSTATE", 5, ","],
    ["I-CITYSTATE", 6, "wa"],
  ],
  "cafe in san francisco": [
    ["B-CITY", 3, "san"],
    ["I-CITY", 4, "francisco"],
  ],
  "san francisco cafe": [
    ["B-CITY", 1, "san"],
    ["I-CITY", 2, "francisco"],
  ],
  "cafe in st. louis": [
    ["B-CITY", 3, "st"],
    ["I-CITY", 4, "."],
    ["I-CITY", 5, "louis"],
  ],
  "cafe in st . louis": [
    ["B-CITY", 3, "st"],
    ["I-CITY", 4, "."],
    ["I-CITY", 5, "louis"],
  ],
  "cafe in st.louis": [
    ["B-CITY", 3, "st"],
    ["I-CITY", 4, "."],
    ["I-CITY", 5, "louis"],
  ],
  "cafe in st .louis": [
    ["B-CITY", 3, "st"],
    ["I-CITY", 4, "."],
    ["I-CITY", 5, "louis"],
  ],
  "st. louis cafe": [
    ["B-CITY", 1, "st"],
    ["I-CITY", 2, "."],
    ["I-CITY", 3, "louis"],
  ],
  "st . louis cafe": [
    ["B-CITY", 1, "st"],
    ["I-CITY", 2, "."],
    ["I-CITY", 3, "louis"],
  ],
  "st.louis cafe": [
    ["B-CITY", 1, "st"],
    ["I-CITY", 2, "."],
    ["I-CITY", 3, "louis"],
  ],
  "st .louis cafe": [
    ["B-CITY", 1, "st"],
    ["I-CITY", 2, "."],
    ["I-CITY", 3, "louis"],
  ],
  "cafe in winston-salem": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
    ["I-CITY", 5, "salem"],
  ],
  "cafe in winston- salem": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
    ["I-CITY", 5, "salem"],
  ],
  "cafe in winston - salem": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
    ["I-CITY", 5, "salem"],
  ],
  "cafe in winston -salem": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
    ["I-CITY", 5, "salem"],
  ],
  "winston-salem cafe": [
    ["B-CITY", 1, "winston"],
    ["I-CITY", 2, "-"],
    ["I-CITY", 3, "salem"],
  ],
  "winston- salem cafe": [
    ["B-CITY", 1, "winston"],
    ["I-CITY", 2, "-"],
    ["I-CITY", 3, "salem"],
  ],
  "winston - salem cafe": [
    ["B-CITY", 1, "winston"],
    ["I-CITY", 2, "-"],
    ["I-CITY", 3, "salem"],
  ],
  "winston -salem cafe": [
    ["B-CITY", 1, "winston"],
    ["I-CITY", 2, "-"],
    ["I-CITY", 3, "salem"],
  ],
  "cafe in winston-": [
    ["B-CITY", 3, "winston"],
    ["I-CITY", 4, "-"],
  ],
  "cafe in hawai'i": [
    ["B-CITY", 3, "ha"],
    ["I-CITY", 4, "##wai"],
    ["I-CITY", 5, "'"],
    ["I-CITY", 6, "i"],
  ],
  "hawai'i cafe": [
    ["B-CITY", 1, "ha"],
    ["I-CITY", 2, "##wai"],
    ["I-CITY", 3, "'"],
    ["I-CITY", 4, "i"],
  ],
  "cafe in san francisco, ca": [
    ["B-ORG", 1, "cafe"],
    ["B-CITYSTATE", 3, "san"],
    ["I-CITYSTATE", 4, "francisco"],
    ["I-CITYSTATE", 5, ","],
    ["I-CITYSTATE", 6, "ca"],
  ],
  "cafe in sf": [["B-CITY", 3, "sf"]],
  "cafe in san francisco,ca": [
    ["B-ORG", 1, "cafe"],
    ["B-CITYSTATE", 3, "san"],
    ["I-CITYSTATE", 4, "francisco"],
    ["I-CITYSTATE", 5, ","],
    ["I-CITYSTATE", 6, "ca"],
  ],
  "cafe in san francisco,ca-": [
    ["B-ORG", 1, "cafe"],
    ["B-CITYSTATE", 3, "san"],
    ["I-CITYSTATE", 4, "francisco"],
    ["I-CITYSTATE", 5, ","],
    ["I-CITYSTATE", 6, "ca"],
    ["I-CITYSTATE", 7, "-"],
  ],
  "cafe in ca-": [
    ["B-STATE", 4, "ca"],
    ["I-STATE", 5, "-"],
  ],
};

for (const [query, nerExample] of Object.entries(nerExamples)) {
  getNerResult(query, nerExample);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.nerThreshold", 0.5],
      ["browser.urlbar.intentThreshold", 0.5],
    ],
  });
  // Stub these out so we don't end up invoking engine calls for now
  // until end-to-end engine calls work
  sinon
    .stub(MLSuggest, "_findIntent")
    .returns([{ label: "yelp_intent", score: 0.9 }]);
  sinon.stub(MLSuggest, "_findNER").callsFake(query => {
    return nerResultsMap[query] || [];
  });

  registerCleanupFunction(async function () {
    sinon.restore();
  });
});

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

// Helper function to test suggestions
async function testSuggestion(
  query,
  expectedCity,
  expectedState,
  remoteClients
) {
  let suggestion = MLSuggest.makeSuggestions(query);
  await remoteClients["ml-onnx-runtime"].rejectPendingDownloads(1);
  suggestion = await suggestion;

  info("Got suggestion for query: " + query);
  info("Got suggestion: " + JSON.stringify(suggestion));
  Assert.ok(suggestion, "MLSuggest returned a result");

  if (expectedCity) {
    Assert.equal(
      suggestion.location.city,
      expectedCity,
      "City extraction is correct"
    );
  }

  if (expectedState) {
    Assert.equal(
      suggestion.location.state,
      expectedState,
      "State extraction is correct"
    );
  }
}

add_task(async function test_MLSuggest() {
  const { cleanup, remoteClients } = await setup();

  const originalIntentOptions = MLSuggest.INTENT_OPTIONS;
  const originalNerOptions = MLSuggest.NER_OPTIONS;

  // Stubs to indicate that wasm is being mocked
  sinon.stub(MLSuggest, "INTENT_OPTIONS").get(() => {
    return { ...originalIntentOptions, modelId: "test-echo" };
  });
  sinon.stub(MLSuggest, "NER_OPTIONS").get(() => {
    return { ...originalNerOptions, modelId: "test-echo" };
  });

  await MLSuggest.initialize();
  await testSuggestion(
    "restaurants in seattle, wa",
    "seattle",
    "wa",
    remoteClients
  );
  await testSuggestion(
    "hotels in new york, ny",
    "new york",
    "ny",
    remoteClients
  );
  await testSuggestion("restaurants seattle", "seattle", null, remoteClients);
  await testSuggestion(
    "restaurants in seattle",
    "seattle",
    null,
    remoteClients
  );
  await testSuggestion(
    "restaurants near seattle",
    "seattle",
    null,
    remoteClients
  );
  await testSuggestion("seattle restaurants", "seattle", null, remoteClients);
  await testSuggestion(
    "seattle wa restaurants",
    "seattle",
    "wa",
    remoteClients
  );
  await testSuggestion(
    "seattle, wa restaurants",
    "seattle",
    "wa",
    remoteClients
  );

  await testSuggestion("dumplings in ca", null, "ca", remoteClients);
  await testSuggestion(
    "plumbers in seattle,wa",
    "seattle",
    "wa",
    remoteClients
  );

  Assert.strictEqual(
    Services.prefs.getFloatPref("browser.urlbar.nerThreshold"),
    0.5,
    "nerThreshold pref should have the expected default value"
  );

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();

  await cleanup();
  sinon.restore();
});

add_task(async function test_MLSuggest_recreates_engines_after_idle() {
  sinon.restore();
  const { cleanup } = await setup();

  const originalIntentOptions = MLSuggest.INTENT_OPTIONS;
  const originalNerOptions = MLSuggest.NER_OPTIONS;
  sinon
    .stub(MLSuggest, "INTENT_OPTIONS")
    .get(() => ({ ...originalIntentOptions, modelId: "test-echo" }));
  sinon
    .stub(MLSuggest, "NER_OPTIONS")
    .get(() => ({ ...originalNerOptions, modelId: "test-echo" }));

  await MLSuggest.initialize();
  Assert.ok(
    !EngineProcess.areAllEnginesTerminated(),
    "The engines are running after initialization"
  );
  await TestUtils.waitForCondition(
    () => EngineProcess.areAllEnginesTerminated(),
    "The engines shut down after browser.ml.modelCacheTimeout of idle"
  );

  const intent = await MLSuggest._findIntent("restaurants in seattle");
  Assert.deepEqual(
    intent?.output,
    ["restaurants in seattle"],
    "The intent engine still answers after shutting down for being idle"
  );
  const ner = await MLSuggest._findNER("restaurants in seattle");
  Assert.deepEqual(
    ner?.output,
    ["restaurants in seattle"],
    "The NER engine still answers after shutting down for being idle"
  );

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();
  await cleanup();
  sinon.restore();
});

/**
 *
 */
class MLEngineThatFails {
  static cnt = 0;
  // prefix with _query avoids lint error
  async run(_query) {
    MLEngineThatFails.cnt += 1;
    if (MLEngineThatFails.cnt === 1) {
      throw Error("Fake error");
    }
    return [
      {
        label: "yelp_intent",
        score: 0.9,
      },
    ];
  }
}

add_task(async function test_MLSuggest_restart_after_failure() {
  // Restore any previous stubs
  sinon.restore();

  sinon.stub(MLSuggest, "createEngine").callsFake(() => {
    return new MLEngineThatFails();
  });
  const { cleanup } = await setup();

  await MLSuggest.initialize();

  let suggestion = await MLSuggest.makeSuggestions(
    "restaurants in seattle, wa"
  );
  Assert.ok(!suggestion, "Suggestion should be null due to error");
  // second request
  let suggestion2;
  await TestUtils.waitForCondition(async () => {
    suggestion2 = await MLSuggest.makeSuggestions("restaurants in seattle, wa");
    return !!suggestion2;
  }, "Wait for suggestion after reinitialization");
  Assert.ok(suggestion2, "Suggestion should be good");
  const expected = { intent: "yelp_intent" };
  Assert.deepEqual(suggestion2.intent, expected.intent);

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();
  await cleanup();
  sinon.restore();
});

/**
 * For Mocking MLEngine with low score
 */
class MLEngineWithLowYelpIntent {
  // prefix with _query avoids lint error
  async run(_query) {
    return [
      {
        label: "yelp_intent",
        score: 0.3,
      },
    ];
  }
}

add_task(async function test_MLSuggest_low_intent_threshold() {
  // Restore any previous stubs
  sinon.restore();

  sinon.stub(MLSuggest, "createEngine").callsFake(() => {
    return new MLEngineWithLowYelpIntent();
  });
  const { cleanup } = await setup();

  await MLSuggest.initialize();

  let suggestion = await MLSuggest.makeSuggestions("no yelp");
  Assert.ok(suggestion, "Suggestion should be good");
  const expected = { intent: "" };
  Assert.deepEqual(suggestion.intent, expected.intent);

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();
  await cleanup();
  sinon.restore();
});

/**
 * For Mocking MLEngine with positive yelp itent
 */
class MLEngineWithHighYelpIntent {
  // prefix with _query avoids lint error
  async run(_query) {
    return [
      {
        label: "yelp_intent",
        score: 0.9,
      },
    ];
  }
}

// utility function for asserting suggestion
async function checkSuggestion(query, expected) {
  let suggestion = await MLSuggest.makeSuggestions(query);
  Assert.ok(suggestion, "Suggestion should be good");
  Assert.deepEqual(suggestion.intent, expected.intent);
  Assert.deepEqual(suggestion.location, expected.location);
  Assert.deepEqual(suggestion.subject, expected.subject);
}

// utility to prepare queriesAndExpectations
function prepQueriesAndExpectations(
  queryVal,
  intentVal,
  cityVal,
  stateVal,
  subjectVal
) {
  return {
    query: queryVal,
    expected: {
      intent: intentVal,
      location: { city: cityVal, state: stateVal },
      subject: subjectVal,
    },
  };
}

add_task(async function test_MLSuggest_city_dup_in_subject() {
  // Restore any previous stubs
  sinon.restore();

  sinon.stub(MLSuggest, "createEngine").callsFake(() => {
    return new MLEngineWithHighYelpIntent();
  });
  sinon.stub(MLSuggest, "_findNER").callsFake(query => {
    return nerResultsMap[query] || [];
  });
  const { cleanup } = await setup();

  await MLSuggest.initialize();

  // syntax for test_examples [[query, intent, city, state, subject]]
  let testExamples = [
    ["ramen ra", "yelp_intent", "ra", null, "ramen"],
    ["ra ramen", "yelp_intent", "ra", null, "ramen"],
    ["ramen st. louis", "yelp_intent", "st. louis", null, "ramen"],
    ["st. louis ramen", "yelp_intent", "st. louis", null, "ramen"],
    ["ramen winston-salem", "yelp_intent", "winston-salem", null, "ramen"],
    ["winston-salem ramen", "yelp_intent", "winston-salem", null, "ramen"],
    ["ramen hawai'i", "yelp_intent", "hawai'i", null, "ramen"],
    ["hawai'i ramen", "yelp_intent", "hawai'i", null, "ramen"],
    ["cafe in ca", "yelp_intent", null, "ca", "cafe"],
    ["ramen noodles ra ca", "yelp_intent", "ra", "ca", "ramen noodles"],
    ["ra ca ramen noodles", "yelp_intent", "ra", "ca", "ramen noodles"],
  ];
  let queriesAndExpectations = testExamples.map(args =>
    prepQueriesAndExpectations(...args)
  );
  for (const { query, expected } of queriesAndExpectations) {
    checkSuggestion(query, expected);
  }

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();
  await cleanup();
  sinon.restore();
});

add_task(async function test_MLSuggest_location_and_subject() {
  // Restore any previous stubs
  sinon.restore();

  sinon.stub(MLSuggest, "createEngine").callsFake(() => {
    return new MLEngineWithHighYelpIntent();
  });
  sinon.stub(MLSuggest, "_findNER").callsFake(query => {
    return nerResultsMap[query] || [];
  });
  const { cleanup } = await setup();

  await MLSuggest.initialize();

  // syntax for test_examples [[query, intent, city, state, subject]]
  let testExamples = [
    ["cafe in san francisco", "yelp_intent", "san francisco", null, "cafe"],
    ["san francisco cafe", "yelp_intent", "san francisco", null, "cafe"],
    ["cafe in st. louis", "yelp_intent", "st. louis", null, "cafe"],
    ["cafe in st . louis", "yelp_intent", "st. louis", null, "cafe"],
    ["cafe in st.louis", "yelp_intent", "st. louis", null, "cafe"],
    ["cafe in st .louis", "yelp_intent", "st. louis", null, "cafe"],
    ["cafe in winston-salem", "yelp_intent", "winston-salem", null, "cafe"],
    ["cafe in winston- salem", "yelp_intent", "winston-salem", null, "cafe"],
    ["cafe in winston - salem", "yelp_intent", "winston-salem", null, "cafe"],
    ["cafe in winston -salem", "yelp_intent", "winston-salem", null, "cafe"],
    ["st. louis cafe", "yelp_intent", "st. louis", null, "cafe"],
    ["st . louis cafe", "yelp_intent", "st. louis", null, "cafe"],
    ["st.louis cafe", "yelp_intent", "st. louis", null, "cafe"],
    ["st .louis cafe", "yelp_intent", "st. louis", null, "cafe"],
    ["winston-salem cafe", "yelp_intent", "winston-salem", null, "cafe"],
    ["winston- salem cafe", "yelp_intent", "winston-salem", null, "cafe"],
    ["winston - salem cafe", "yelp_intent", "winston-salem", null, "cafe"],
    ["winston -salem cafe", "yelp_intent", "winston-salem", null, "cafe"],
    ["cafe in winston-", "yelp_intent", "winston", null, "cafe"],
    ["cafe in hawai'i", "yelp_intent", "hawai'i", null, "cafe"],
    ["hawai'i cafe", "yelp_intent", "hawai'i", null, "cafe"],
    ["cafe in san francisco, ca", "yelp_intent", "san francisco", "ca", "cafe"],
    ["cafe in sf", "yelp_intent", "sf", null, "cafe"],
    ["cafe in san francisco,ca", "yelp_intent", "san francisco", "ca", "cafe"],
    ["cafe in san francisco,ca-", "yelp_intent", "san francisco", "ca", "cafe"],
    ["cafe in ca-", "yelp_intent", null, "ca", "cafe"],
  ];
  let queriesAndExpectations = testExamples.map(args =>
    prepQueriesAndExpectations(...args)
  );
  for (const { query, expected } of queriesAndExpectations) {
    checkSuggestion(query, expected);
  }

  await MLSuggest.shutdown();
  await EngineProcess.destroyMLEngine();
  await cleanup();
  sinon.restore();
});
