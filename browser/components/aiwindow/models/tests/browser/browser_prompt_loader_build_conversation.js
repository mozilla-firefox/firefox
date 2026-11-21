/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { buildConversation, loadPrompt } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs"
);

const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);

/**
 * @type {import("../AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const {
  MODEL_FEATURES,
  SERVICE_TYPES,
  PURPOSES,
  getRemoteClient,
  checkMajorVersion,
  FEATURE_MAJOR_VERSIONS,
  _setRemoteClientForTesting,
  _clearRemoteClientForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const PREF_MODEL = "browser.smartwindow.model";
const PREF_MODEL_CHOICE = "browser.smartwindow.firstrun.modelChoice";
const PREF_ENDPOINT = "browser.smartwindow.endpoint";
const PREF_CUSTOM_ENDPOINT = "browser.smartwindow.customEndpoint";
const PREF_CUSTOM_PROMPTS = "browser.smartwindow.customPrompts";

const CUSTOM_MODEL = "test-local-model:latest";
const CUSTOM_ENDPOINT = "http://localhost:1/v1";

let gChatParamsByChoice;
let gTitleGenerationConfig;
let gMemoriesGenerationConfig;

function parametersOf(record) {
  return typeof record.parameters === "string"
    ? JSON.parse(record.parameters)
    : record.parameters || {};
}

add_setup(async function read_real_config_records() {
  const records = await getRemoteClient().get();
  gChatParamsByChoice = new Map(
    records
      .filter(
        record =>
          record.kind === "params" &&
          record.feature === MODEL_FEATURES.CHAT &&
          record.model_choice_id
      )
      .map(record => [record.model_choice_id, record])
  );
  gTitleGenerationConfig = records.find(
    record =>
      !record.kind &&
      record.feature === MODEL_FEATURES.TITLE_GENERATION &&
      record.is_default &&
      checkMajorVersion(
        record.version,
        FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.TITLE_GENERATION]
      )
  );

  const memoriesFeature = MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM;
  gMemoriesGenerationConfig = records.find(
    record =>
      !record.kind &&
      record.feature === memoriesFeature &&
      record.is_default &&
      checkMajorVersion(record.version, FEATURE_MAJOR_VERSIONS[memoriesFeature])
  );

  Assert.ok(gChatParamsByChoice.get("1"), "Real RS has model choice 1 params");
  Assert.ok(gChatParamsByChoice.get("2"), "Real RS has model choice 2 params");
  Assert.ok(gTitleGenerationConfig, "Real RS has title-generation config");
  Assert.ok(
    gMemoriesGenerationConfig,
    "Real RS has memories-initial-generation-system config"
  );
  Assert.notEqual(
    gMemoriesGenerationConfig.model,
    CUSTOM_MODEL,
    "Memories config is pinned to a model that is not the custom one"
  );
});

add_task(async function test_buildConversation_uses_chat_params() {
  const expectedParams = gChatParamsByChoice.get("2");
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "2"]],
    clear: [[PREF_MODEL], [PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });

  try {
    const conversation = await buildConversation(MODEL_FEATURES.CHAT, {
      flowId: "test-flow",
    });

    Assert.ok(
      conversation instanceof Conversation,
      "buildConversation returns a Conversation instance"
    );
    Assert.equal(conversation.feature, MODEL_FEATURES.CHAT);
    Assert.deepEqual(conversation.parameters, parametersOf(expectedParams));
    Assert.equal(conversation.engine.model, expectedParams.model);
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_buildConversation_sends_inference_model() {
  const inferenceModel = "qwen3-235b-a22b-instruct-2507-maas";
  _setRemoteClientForTesting({
    get: async () => [
      {
        id: "chat--params--auto--v1",
        kind: "params",
        feature: MODEL_FEATURES.CHAT,
        model: "auto",
        inference_model: inferenceModel,
        model_choice_id: "4",
        version: `${FEATURE_MAJOR_VERSIONS[MODEL_FEATURES.CHAT]}.0`,
        parameters: { temperature: 0.5 },
      },
    ],
  });
  const createEngineSpy = sinon.spy(openAIEngine, "_createEngine");
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "4"]],
    clear: [[PREF_MODEL], [PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });

  try {
    const conversation = await buildConversation(MODEL_FEATURES.CHAT);

    Assert.equal(conversation.engine.model, inferenceModel);
    Assert.equal(
      createEngineSpy.lastCall.args[0].modelId,
      inferenceModel,
      "The endpoint gets inference_model, not the record's model"
    );
    Assert.deepEqual(conversation.parameters, { temperature: 0.5 });
  } finally {
    await SpecialPowers.popPrefEnv();
    createEngineSpy.restore();
    _clearRemoteClientForTesting();
  }
});

add_task(async function test_buildConversation_uses_non_chat_config() {
  await SpecialPowers.pushPrefEnv({
    clear: [[PREF_MODEL], [PREF_MODEL_CHOICE], [PREF_ENDPOINT]],
  });

  try {
    const conversation = await buildConversation(
      MODEL_FEATURES.TITLE_GENERATION
    );

    Assert.equal(conversation.feature, MODEL_FEATURES.TITLE_GENERATION);
    Assert.deepEqual(conversation.parameters, {});
    Assert.equal(conversation.engine.model, gTitleGenerationConfig.model);
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_buildConversation_custom_model_for_non_chat() {
  const createEngineSpy = sinon.spy(openAIEngine, "_createEngine");
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_MODEL_CHOICE, "0"],
      [PREF_CUSTOM_ENDPOINT, CUSTOM_ENDPOINT],
      [PREF_MODEL, CUSTOM_MODEL],
    ],
  });

  try {
    const feature = MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM;
    const conversation = await buildConversation(feature);

    Assert.equal(
      conversation.engine.model,
      CUSTOM_MODEL,
      "A non-chat feature sends the custom model, not the record's model"
    );

    // Check v1 non-chat prompts respect the custom endpoint
    const { prompt, version } = await loadPrompt(feature);
    Assert.equal(
      version,
      gMemoriesGenerationConfig.version,
      "The prompt still comes from the Remote Settings record"
    );
    Assert.ok(prompt.length, "Prompt is not empty");

    // memories-merge is a v2 prompt
    const merge = await loadPrompt(MODEL_FEATURES.MEMORIES_MERGE, {
      module: "system-instructions",
      model: CUSTOM_MODEL,
    });
    Assert.ok(
      merge.prompt.length,
      "Modular prompts resolve for a model no record is keyed on"
    );

    // Check the engine points at the custom model and endpoint
    Assert.equal(
      createEngineSpy.lastCall.args[0].modelId,
      CUSTOM_MODEL,
      "The engine is configured with the custom model"
    );
    Assert.equal(
      createEngineSpy.lastCall.args[0].baseURL,
      CUSTOM_ENDPOINT,
      "The engine is pointed at the custom endpoint"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
    createEngineSpy.restore();
  }
});

add_task(async function test_buildConversation_preset_choice_ignores_model() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_MODEL_CHOICE, "2"],
      [PREF_MODEL, CUSTOM_MODEL],
    ],
    clear: [[PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });

  try {
    const conversation = await buildConversation(
      MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM
    );

    Assert.equal(
      conversation.engine.model,
      gMemoriesGenerationConfig.model,
      "A preset model choice keeps the record's model"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_loadPrompt_uses_non_chat_prompt() {
  await SpecialPowers.pushPrefEnv({
    clear: [[PREF_CUSTOM_PROMPTS], [PREF_MODEL], [PREF_MODEL_CHOICE]],
  });

  try {
    const result = await loadPrompt(MODEL_FEATURES.TITLE_GENERATION);

    Assert.equal(result.version, gTitleGenerationConfig.version);
    Assert.ok(
      result.prompt.startsWith(gTitleGenerationConfig.prompts.split("\n")[0]),
      "Title generation prompt loaded from Remote Settings"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_loadPrompt_honors_custom_prompt_pref() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        PREF_CUSTOM_PROMPTS,
        JSON.stringify({ [MODEL_FEATURES.CHAT]: "OVERRIDE" }),
      ],
    ],
  });

  try {
    const { prompt, version } = await loadPrompt(MODEL_FEATURES.CHAT);

    Assert.equal(prompt, "OVERRIDE");
    Assert.equal(version, "");
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_buildConversation_reports_missing_feature() {
  await Assert.rejects(
    buildConversation("not-a-real-feature"),
    err => err.clientReason === "remoteSettingsUnavailable"
  );
});

add_task(async function test_buildConversation_reports_missing_major_version() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "2"]],
    clear: [[PREF_MODEL]],
  });

  try {
    await Assert.rejects(
      buildConversation(MODEL_FEATURES.CHAT, { majorVersionOverride: 999 }),
      err => err.clientReason === "modelConfigUnavailable"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * A params record as Remote Settings serves it, for the feature's current
 * major version.
 *
 * @param {string} feature
 * @param {string} model
 * @param {object} [extra]
 * @returns {object}
 */
function paramsRecord(feature, model, extra = {}) {
  return {
    id: `${feature}--params--${model}`,
    kind: "params",
    feature,
    model,
    version: `${FEATURE_MAJOR_VERSIONS[feature]}.0`,
    is_default: true,
    parameters: {},
    modules: [],
    ...extra,
  };
}

const CHAT_RECORD = paramsRecord(MODEL_FEATURES.CHAT, "test-chat-model", {
  is_default: false,
  model_choice_id: "1",
});
const SETUP_RECORD = paramsRecord(
  MODEL_FEATURES.AGENT_MONITOR_SETUP,
  "generic",
  { service_type: SERVICE_TYPES.AGENT, purpose: PURPOSES.MONITOR }
);

/**
 * @param {string} feature
 * @param {object[]} records - What Remote Settings holds
 * @returns {Promise<string>} The model the feature's engine is built for
 */
async function modelFor(feature, records) {
  _setRemoteClientForTesting({ get: async () => records });
  try {
    return (await buildConversation(feature)).engine.model;
  } finally {
    _clearRemoteClientForTesting();
  }
}

add_task(async function test_monitor_setup_uses_the_monitor_model() {
  const monitorModel = "test-monitor-model-a";
  _setRemoteClientForTesting({
    get: async () => [
      CHAT_RECORD,
      SETUP_RECORD,
      paramsRecord(MODEL_FEATURES.AGENT_MONITOR, monitorModel, {
        service_type: SERVICE_TYPES.AGENT,
        purpose: PURPOSES.MONITOR,
      }),
    ],
  });
  // Engine creation is spied on, not stubbed, so this also goes through the ML
  // engine's own validation of the feature id.
  const createEngineSpy = sinon.spy(openAIEngine, "_createEngine");
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "1"]],
    clear: [[PREF_MODEL], [PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });

  try {
    const monitor = await buildConversation(MODEL_FEATURES.AGENT_MONITOR);
    const monitorOptions = createEngineSpy.lastCall.args[0];
    const setup = await buildConversation(MODEL_FEATURES.AGENT_MONITOR_SETUP);
    const setupOptions = createEngineSpy.lastCall.args[0];

    Assert.equal(monitor.engine.model, monitorModel, "Monitor has a model");
    Assert.equal(
      setup.engine.model,
      monitorModel,
      "Monitor setup runs on the model the monitor agent resolves to"
    );
    Assert.equal(
      setupOptions.modelId,
      monitorOptions.modelId,
      "and that is the model the engine is created for"
    );
    Assert.equal(setupOptions.featureId, MODEL_FEATURES.AGENT_MONITOR_SETUP);
    Assert.equal(setupOptions.serviceType, SERVICE_TYPES.AGENT);
    Assert.equal(setupOptions.purpose, PURPOSES.MONITOR);
    Assert.notEqual(
      setupOptions.engineId,
      monitorOptions.engineId,
      "Monitor setup never shares an engine with a monitor check"
    );
    Assert.deepEqual(setup.parameters, parametersOf(SETUP_RECORD));
  } finally {
    await SpecialPowers.popPrefEnv();
    createEngineSpy.restore();
    _clearRemoteClientForTesting();
  }
});

add_task(async function test_monitor_setup_follows_the_monitor_default() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "1"]],
    clear: [[PREF_MODEL], [PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });
  const setup = MODEL_FEATURES.AGENT_MONITOR_SETUP;
  const monitor = MODEL_FEATURES.AGENT_MONITOR;

  try {
    Assert.equal(
      await modelFor(setup, [
        CHAT_RECORD,
        SETUP_RECORD,
        paramsRecord(monitor, "test-monitor-model-a"),
      ]),
      "test-monitor-model-a",
      "Monitor setup takes the monitor agent's default model"
    );
    Assert.equal(
      await modelFor(setup, [
        CHAT_RECORD,
        SETUP_RECORD,
        paramsRecord(monitor, "test-monitor-model-a", { is_default: false }),
        paramsRecord(monitor, "test-monitor-model-b"),
      ]),
      "test-monitor-model-b",
      "and follows when the monitor agent's default changes"
    );
    Assert.equal(
      await modelFor(setup, [
        CHAT_RECORD,
        SETUP_RECORD,
        paramsRecord(monitor, "test-monitor-model-a", {
          inference_model: "test-monitor-inference-model",
        }),
      ]),
      "test-monitor-inference-model",
      "down to the monitor agent's inference model"
    );
    Assert.equal(
      await modelFor(setup, [
        CHAT_RECORD,
        SETUP_RECORD,
        paramsRecord(monitor, "generic"),
      ]),
      "test-chat-model",
      "A generic monitor record sends both to the chat model"
    );
    Assert.equal(
      await modelFor(setup, [
        CHAT_RECORD,
        paramsRecord(setup, "test-setup-model"),
        paramsRecord(monitor, "test-monitor-model-a"),
      ]),
      "test-setup-model",
      "A setup record that names a model keeps it"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_monitor_setup_custom_model_wins() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_MODEL_CHOICE, "0"],
      [PREF_CUSTOM_ENDPOINT, CUSTOM_ENDPOINT],
      [PREF_MODEL, CUSTOM_MODEL],
    ],
  });

  try {
    Assert.equal(
      await modelFor(MODEL_FEATURES.AGENT_MONITOR_SETUP, [
        CHAT_RECORD,
        SETUP_RECORD,
        paramsRecord(MODEL_FEATURES.AGENT_MONITOR, "test-monitor-model-a"),
      ]),
      CUSTOM_MODEL,
      "A custom model user keeps their own model for monitor setup"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_model_source_leaves_other_features_alone() {
  await SpecialPowers.pushPrefEnv({
    set: [[PREF_MODEL_CHOICE, "1"]],
    clear: [[PREF_MODEL], [PREF_ENDPOINT], [PREF_CUSTOM_ENDPOINT]],
  });
  const monitor = MODEL_FEATURES.AGENT_MONITOR;
  const records = [
    CHAT_RECORD,
    SETUP_RECORD,
    paramsRecord(monitor, "test-monitor-model-a"),
    paramsRecord(MODEL_FEATURES.AITAB, "generic"),
    paramsRecord(MODEL_FEATURES.TITLE_GENERATION, "generic"),
  ];

  try {
    Assert.equal(
      await modelFor(monitor, records),
      "test-monitor-model-a",
      "The monitor agent's own model does not depend on monitor setup"
    );
    Assert.equal(
      await modelFor(monitor, [CHAT_RECORD, paramsRecord(monitor, "generic")]),
      "test-chat-model",
      "A generic monitor record still falls back to the chat model"
    );
    Assert.equal(
      await modelFor(MODEL_FEATURES.AITAB, records),
      "test-chat-model",
      "Another chat fallback feature still gets the chat model"
    );
    Assert.equal(
      await modelFor(MODEL_FEATURES.TITLE_GENERATION, records),
      "generic",
      "A feature without a model source keeps its record's model"
    );
    Assert.equal(
      await modelFor(MODEL_FEATURES.CHAT, records),
      "test-chat-model",
      "Chat keeps its own model"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_buildConversation_on_device_options() {
  const feature = MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM;
  /** @type {Partial<PipelineOptions>} */
  const onDeviceOptions = {
    backend: "llama.cpp",
    modelId: "Mozilla/test-model",
    modelFile: "model.gguf",
  };
  const run = sinon.stub().resolves({ finalOutput: "" });
  const createEngineStub = sinon
    .stub(openAIEngine, "_createEngine")
    .resolves({ run, engineStatus: "ready" });
  try {
    const conversation = await buildConversation(feature, {
      onDeviceOptions,
      flowId: "local-flow",
    });
    Assert.ok(conversation.isReady, "A conversation is ready to run");
    await conversation.run({ inferenceParams: { max_tokens: 8 } });
    const [options] = createEngineStub.firstCall.args;
    Assert.equal(options.backend, "llama.cpp");
    Assert.equal(options.modelId, onDeviceOptions.modelId);
    Assert.equal(options.flowId, "local-flow");
    Assert.equal(options.featureId, feature);
    Assert.ok(!("apiKey" in options), "Cloud credentials stay out");
    Assert.equal(run.firstCall.args[0].inferenceParams.max_tokens, 8);
    Assert.ok(
      options.engineId.endsWith(`-${onDeviceOptions.modelId}`),
      "The engine id names the on-device model"
    );

    const unauthorized = Object.assign(new Error("401 status code"), {
      status: 401,
    });
    run.rejects(unauthorized);
    await Assert.rejects(
      conversation.run({}),
      error => error === unauthorized,
      "A 401 on device is not retried"
    );
    Assert.ok(createEngineStub.calledOnce, "The engine is not recreated");
  } finally {
    createEngineStub.restore();
  }
});
