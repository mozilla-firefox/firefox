/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * What a press of "Write it for me" does, from the button to the filled form,
 * is tested through the real hosts in
 * ui/test/browser/browser_aiwindow_monitor_suggest_*.js. These are the few
 * checks a press can't reach.
 */

/**
 * @type {import("../../../ui/test/AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { CHAT_ROLE_LABELS, MonitorSetup, describeLanguage } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs"
  );

const { MODEL_FEATURES, PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const { FEATURES, PipelineOptions } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);

/**
 * A mock engine never reaches the ML engine's own check of the feature id. An
 * id it does not know is refused before any request is made.
 */
add_task(async function test_ml_engine_accepts_the_feature() {
  const feature = MODEL_FEATURES.AGENT_MONITOR_SETUP;
  Assert.ok(Object.hasOwn(FEATURES, feature), "The ML engine lists it");

  // The options openAIEngine creates its engine with.
  const options = new PipelineOptions({
    backend: "openai",
    baseURL: "https://example.com/v1",
    engineId: `smart-openai-${feature}-test-model`,
    featureId: feature,
    modelId: "test-model",
    modelRevision: "main",
    taskName: "text-generation",
    serviceType: "agent",
    purpose: PURPOSES.MONITOR,
  });
  Assert.equal(options.featureId, feature, "The feature id is accepted");

  const setupEngineId = new PipelineOptions({ featureId: feature }).engineId;
  const monitorEngineId = new PipelineOptions({
    featureId: MODEL_FEATURES.AGENT_MONITOR,
  }).engineId;
  Assert.equal(
    setupEngineId,
    FEATURES[feature].engineId,
    "Left to the ML engine, it gets the engine id listed for it"
  );
  Assert.notEqual(
    setupEngineId,
    monitorEngineId,
    "which is not the monitor agent's, so neither replaces the other's engine"
  );

  Assert.throws(
    () => new PipelineOptions({ featureId: `${feature}-unlisted` }),
    /featureId/,
    "An unlisted feature id is still refused"
  );
});

// The button is only offered with a page or chat to go on. Asked without
// either, the module answers with nothing and never builds an engine.
add_task(async function test_nothing_to_ground_suggests_nothing() {
  const mockEngineManager = new MockEngineManager();
  try {
    Assert.strictEqual(
      await MonitorSetup.suggest({}),
      null,
      "No pages and no chat give nothing to ground a suggestion in"
    );
    Assert.strictEqual(
      await MonitorSetup.suggest({
        watchUrls: ["about:blank"],
        chatHistory: "  \n ",
      }),
      null,
      "Pages a monitor can't watch and a blank chat count as nothing"
    );
    Assert.equal(mockEngineManager.engines.size, 0, "No engine was built");
  } finally {
    mockEngineManager.cleanupMocks();
  }
});

// A press waits SUGGESTION_TIMEOUT_MS for the model. This limit stands in for
// it: long enough for the request to reach the engine, short enough to wait out.
const SHORT_TIMEOUT_MS = 1000;

add_task(async function test_timed_out_suggestion_aborts_its_request() {
  const mockEngineManager = new MockEngineManager();
  const purpose = PURPOSES.MONITOR;
  const options = {
    chatHistory: `${CHAT_ROLE_LABELS.user}: tell me when the trail shoes restock`,
    useMemories: false,
  };
  const suggestion = {
    title: "Trail shoes restock",
    condition: "The trail shoes are back in stock",
  };
  try {
    const answered = MonitorSetup.suggest(options);
    const { respond } = await mockEngineManager.captureRequest({ purpose });
    respond(JSON.stringify(suggestion));
    Assert.deepEqual(
      await answered,
      suggestion,
      "An answer within the limit is the suggestion"
    );

    const timedOut = MonitorSetup.suggest({
      ...options,
      timeoutMs: SHORT_TIMEOUT_MS,
    });
    // The engine holds this request and never answers it
    const held = await Promise.race([
      mockEngineManager.captureRequest({ purpose }),
      timedOut.then(() => null),
    ]);
    Assert.ok(held, "The request reached the engine before the limit");
    const engine = mockEngineManager.engines.get(purpose);
    Assert.equal(engine.runRequests.size, 1, "The request is in flight");

    Assert.strictEqual(
      await timedOut,
      null,
      "With no answer by the limit there is no suggestion"
    );
    Assert.equal(
      engine.runRequests.size,
      0,
      "and the request in flight was aborted"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(function test_language_is_described_by_name() {
  Assert.equal(describeLanguage("fr"), "French (fr)");
  Assert.equal(describeLanguage("pt-BR"), "Brazilian Portuguese (pt-BR)");
  Assert.equal(
    describeLanguage("xx"),
    "xx",
    "A tag without a name is passed as it is"
  );
  Assert.equal(
    describeLanguage("not a tag"),
    "not a tag",
    "and so is one that is not a valid tag"
  );
});
