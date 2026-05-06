/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {
  TelemetryEngine,
  TelemetryPromptEngine,
  Trigger,
  TRIGGER_CHECK_STRATEGIES,
  normalizeMetadata,
  submitTelemetryResult,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs"
);
const { openAIEngine } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

function makeConversation({
  turnIndex = 0,
  uniformSample = false,
  telemetryTriggers = new Set(),
} = {}) {
  return {
    _telemetryUniformSample: uniformSample,
    _checkedTelemetryTriggers: telemetryTriggers,
    currentTurnIndex() {
      return turnIndex;
    },
    getMessagesInOpenAiFormat() {
      return [];
    },
  };
}

const LONG_CONVERSATION = "long_conversation";
const UNIFORM_SAMPLE = "uniform_sample";

add_task(async function test_Trigger_stores_name_and_samplingProbability() {
  const trigger = new Trigger("my_trigger", () => true, 0.75, "a description");
  Assert.equal(trigger.name, "my_trigger", "name should be stored");
  Assert.equal(
    trigger.samplingProbability,
    0.75,
    "samplingProbability should be stored"
  );
  Assert.equal(
    trigger.description,
    "a description",
    "description should be stored"
  );
});

add_task(async function test_Trigger_default_samplingProbability_is_one() {
  const trigger = new Trigger("test_trigger", () => true);
  Assert.equal(
    trigger.samplingProbability,
    1.0,
    "default samplingProbability should be 1.0"
  );
});

add_task(
  async function test_Trigger_check_receives_conversation_and_returns_true() {
    const conversation = makeConversation({ uniformSample: true });
    const trigger = new Trigger(
      "test_trigger",
      conv => conv._telemetryUniformSample === true,
      1.0
    );
    Assert.ok(
      trigger.check(conversation),
      "check should receive the conversation and return true"
    );
  }
);

add_task(
  async function test_Trigger_check_returns_false_when_condition_unmet() {
    const conversation = makeConversation({ uniformSample: false });
    const trigger = new Trigger(
      "test_trigger",
      conv => conv._telemetryUniformSample === true,
      1.0
    );
    Assert.ok(
      !trigger.check(conversation),
      "check should return false when condition is not met"
    );
  }
);

add_task(async function test_getTriggers_uniform_sample_fires_when_sampled() {
  const conversation = makeConversation({ turnIndex: 0, uniformSample: false });
  const engine = new TelemetryEngine();

  const sb = sinon.createSandbox();
  try {
    sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
      engine._triggers = [
        new Trigger(
          UNIFORM_SAMPLE,
          conv => TRIGGER_CHECK_STRATEGIES[UNIFORM_SAMPLE](conv, {}),
          1.0
        ),
      ];
    });
    const triggers = await engine.getTriggers(conversation);

    Assert.ok(
      conversation._telemetryUniformSample,
      "_telemetryUniformSample should be set to True"
    );
    Assert.ok(
      triggers.some(t => t.name === UNIFORM_SAMPLE),
      `${UNIFORM_SAMPLE} should fire`
    );
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_getTriggers_uniform_sample_at_turn_fires_at_correct_turn() {
    const turn = 2;
    const conversation = makeConversation({
      turnIndex: turn,
      uniformSample: true,
    });
    const engine = new TelemetryEngine();

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            "uniform_sample_turn2",
            conv =>
              TRIGGER_CHECK_STRATEGIES.uniform_sample_at_turn(conv, { turn }),
            1.0
          ),
        ];
      });
      const triggers = await engine.getTriggers(conversation);
      Assert.ok(
        triggers.some(t => t.name === "uniform_sample_turn2"),
        "uniform_sample_turn2 should fire at turn 2 when sampled"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_getTriggers_uniform_sample_turn2_does_not_fire_without_uniform_sample() {
    const turn = 2;
    const conversation = makeConversation({
      turnIndex: turn,
      uniformSample: false,
    });
    const engine = new TelemetryEngine();

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            "uniform_sample_turn2",
            conv =>
              TRIGGER_CHECK_STRATEGIES.uniform_sample_at_turn(conv, { turn }),
            1.0
          ),
        ];
      });
      const triggers = await engine.getTriggers(conversation);

      Assert.ok(
        !triggers.some(t => t.name === "uniform_sample_turn2"),
        "uniform_sample_turn2 should not fire when _telemetryUniformSample is false"
      );
      Assert.ok(
        !conversation._telemetryUniformSample,
        "_telemetryUniformSample should not be set to True at turn 2"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_getTriggers_uniform_sample_at_turn_does_not_fire_at_wrong_turn() {
    const turn = 2;
    const conversation = makeConversation({
      turnIndex: turn + 1,
      uniformSample: true,
    });
    const engine = new TelemetryEngine();

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            "uniform_sample_turn2",
            conv =>
              TRIGGER_CHECK_STRATEGIES.uniform_sample_at_turn(conv, { turn }),
            1.0
          ),
        ];
      });
      const triggers = await engine.getTriggers(conversation);

      Assert.ok(
        !triggers.some(t => t.name === "uniform_sample_turn2"),
        "uniform_sample_turn2 should not fire at turn 3"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(async function test_getTriggers_min_turns_fires_at_threshold() {
  const conversation = makeConversation({
    turnIndex: 10,
    uniformSample: false,
  });
  const engine = new TelemetryEngine();

  const sb = sinon.createSandbox();
  try {
    sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
      engine._triggers = [
        new Trigger(
          LONG_CONVERSATION,
          conv => TRIGGER_CHECK_STRATEGIES.min_turns(conv, { minTurns: 10 }),
          1.0
        ),
      ];
    });

    const triggers = await engine.getTriggers(conversation);
    Assert.ok(
      triggers.some(t => t.name === LONG_CONVERSATION),
      `${LONG_CONVERSATION} should fire at turn 10`
    );
    Assert.ok(
      conversation._checkedTelemetryTriggers.has(LONG_CONVERSATION),
      `${LONG_CONVERSATION} should be included in _checkedTelemetryTriggers`
    );
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_getTriggers_min_turns_does_not_fire_below_threshold() {
    const conversation = makeConversation({
      turnIndex: 9,
      uniformSample: false,
    });
    const engine = new TelemetryEngine();

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            LONG_CONVERSATION,
            conv => TRIGGER_CHECK_STRATEGIES.min_turns(conv, { minTurns: 10 }),
            1.0
          ),
        ];
      });

      const triggers = await engine.getTriggers(conversation);
      Assert.ok(
        !triggers.some(t => t.name === LONG_CONVERSATION),
        `${LONG_CONVERSATION} should not fire before turn 10`
      );
      Assert.ok(
        !conversation._checkedTelemetryTriggers.has(LONG_CONVERSATION),
        `${LONG_CONVERSATION} should be not included in _checkedTelemetryTriggers before turn 10`
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_getTriggers_trigger_does_not_fire_when_random_exceeds_probability() {
    const turn = 10;
    const conversation = makeConversation({
      turnIndex: turn,
      uniformSample: false,
    });
    const engine = new TelemetryEngine();

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            LONG_CONVERSATION,
            conv => TRIGGER_CHECK_STRATEGIES.min_turns(conv, { minTurns: 10 }),
            0.5
          ),
        ];
      });

      sb.stub(engine, "_getRandom").returns(0.9);
      const triggers = await engine.getTriggers(conversation);

      Assert.ok(
        !triggers.some(t => t.name === LONG_CONVERSATION),
        "trigger should not fire when Math.random() exceeds samplingProbability"
      );
      Assert.ok(
        conversation._checkedTelemetryTriggers.has(LONG_CONVERSATION),
        `${LONG_CONVERSATION} should be included in _checkedTelemetryTriggers`
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_getTriggers_trigger_does_not_refire_for_same_conversation() {
    const turn = 10;
    const conversation = makeConversation({
      turnIndex: turn,
      uniformSample: true,
    });
    const conversation2 = makeConversation({
      turnIndex: turn + 1,
      uniformSample: true,
      telemetryTriggers: new Set([LONG_CONVERSATION]),
    });

    const engine = new TelemetryEngine();
    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "getTriggerDefinitions").callsFake(async function () {
        engine._triggers = [
          new Trigger(
            LONG_CONVERSATION,
            conv => TRIGGER_CHECK_STRATEGIES.min_turns(conv, { minTurns: 10 }),
            0.5
          ),
        ];
      });

      const randomStub = sb.stub(engine, "_getRandom").returns(0.9);
      const triggers1 = await engine.getTriggers(conversation);
      Assert.ok(
        !triggers1.some(t => t.name === LONG_CONVERSATION),
        `${LONG_CONVERSATION} should not fire when sampling probability not met`
      );
      Assert.ok(
        conversation._checkedTelemetryTriggers.has(LONG_CONVERSATION),
        `${LONG_CONVERSATION} should be included in _checkedTelemetryTriggers`
      );

      randomStub.returns(0.3);
      const triggers2 = await engine.getTriggers(conversation2);
      Assert.ok(
        !triggers2.some(t => t.name === LONG_CONVERSATION),
        `${LONG_CONVERSATION} should not refire on a second call for the same conversation`
      );
      Assert.ok(
        conversation2._checkedTelemetryTriggers.has(LONG_CONVERSATION),
        `${LONG_CONVERSATION} should be included in _checkedTelemetryTriggers`
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(async function test_runTelemetry_returns_empty_for_no_triggers() {
  const engine = new TelemetryEngine();

  const results = await engine.runTelemetry([], makeConversation());
  Assert.deepEqual(
    results,
    [],
    "runTelemetry with no triggers should return []"
  );
});

add_task(async function test_runTelemetry_runs_engine_for_matched_trigger() {
  const engine = new TelemetryEngine();
  const TELEMETRY_NAME = "wasSuccessful";

  const sb = sinon.createSandbox();
  try {
    const fakeResult = {
      wasSuccessful: "successful",
      conversationTopic: "sports",
      memoryReferenced: "no",
    };
    sb.stub(engine, "_fetchRecords").resolves([
      {
        id: "my-prompt-v1",
        version: "1.0",
        telemetry_name: TELEMETRY_NAME,
        triggers: [UNIFORM_SAMPLE],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
      },
    ]);
    const fakeEngine = { run: sb.stub().resolves(fakeResult) };
    sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

    const trigger = new Trigger(UNIFORM_SAMPLE, () => true, 1.0);
    const results = await engine.runTelemetry([trigger], makeConversation());

    Assert.ok(
      results.length,
      "should return results when a trigger matches a prompt record"
    );
    Assert.ok(
      results.some(r => r.telemetry_name === TELEMETRY_NAME),
      `${TELEMETRY_NAME} result should be present`
    );
    Assert.ok(fakeEngine.run.called, "engine.run should be called");
    Assert.equal(
      results[0].samplingProbability,
      1.0,
      "result should include the samplingProbability of the matching trigger"
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_runTelemetry_deduplicates_by_telemetry_name() {
  const engine = new TelemetryEngine();
  const TELEMETRY_NAME = "wasSuccessful";

  const sb = sinon.createSandbox();
  try {
    sb.stub(engine, "_fetchRecords").resolves([
      {
        id: "my-prompt-v1",
        version: "1.0",
        telemetry_name: TELEMETRY_NAME,
        triggers: [UNIFORM_SAMPLE, `${UNIFORM_SAMPLE}_turn2`],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
      },
    ]);
    const fakeEngine = {
      run: sb.stub().resolves({ wasSuccessful: "successful" }),
    };
    sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

    const triggers = [
      new Trigger(UNIFORM_SAMPLE, () => true, 1.0),
      new Trigger(`${UNIFORM_SAMPLE}_turn2`, () => true, 1.0),
    ];
    const results = await engine.runTelemetry(triggers, makeConversation());

    const wasSuccessfulCount = results.filter(
      r => r.telemetry_name === "wasSuccessful"
    ).length;
    Assert.equal(
      wasSuccessfulCount,
      1,
      "wasSuccessful prompt should run only once despite multiple matching triggers"
    );
    Assert.equal(
      TelemetryPromptEngine.build.callCount,
      1,
      "build should be called only once for the deduplicated prompt"
    );
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_runTelemetry_samplingProbability_comes_from_first_matching_trigger() {
    const engine = new TelemetryEngine();
    const TELEMETRY_NAME = "wasSuccessful";

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "_fetchRecords").resolves([
        {
          id: "my-prompt-v1",
          version: "1.0",
          telemetry_name: TELEMETRY_NAME,
          triggers: [UNIFORM_SAMPLE, `${UNIFORM_SAMPLE}_turn2`],
          output_schema: { myField: ["a", "b"] },
          prompt: "...",
        },
      ]);
      const fakeEngine = {
        run: sb.stub().resolves({ wasSuccessful: "successful" }),
      };
      sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

      const triggers = [
        new Trigger(UNIFORM_SAMPLE, () => true, 0.3),
        new Trigger(`${UNIFORM_SAMPLE}_turn2`, () => true, 0.7),
      ];
      const results = await engine.runTelemetry(triggers, makeConversation());

      Assert.equal(
        results[0].samplingProbability,
        0.3,
        "samplingProbability should come from the first matching trigger in the record's trigger list"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_runTelemetry_does_not_run_prompt_for_unmatched_trigger() {
    const engine = new TelemetryEngine();
    const TELEMETRY_NAME = "wasSuccessful";

    const sb = sinon.createSandbox();
    try {
      sb.stub(engine, "_fetchRecords").resolves([
        {
          id: "my-prompt-v1",
          version: "1.0",
          telemetry_name: UNIFORM_SAMPLE,
          triggers: [UNIFORM_SAMPLE, `${UNIFORM_SAMPLE}_turn2`],
          output_schema: { myField: ["a", "b"] },
          prompt: "...",
        },
        {
          id: "my-prompt-v2",
          version: "1.0",
          telemetry_name: "isLongConvo",
          triggers: [LONG_CONVERSATION],
          output_schema: { myField: ["a", "b"] },
          prompt: "...",
        },
      ]);
      const fakeEngine = { run: sb.stub().resolves({}) };
      sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

      // long_conversation matches isLongConvo prompt, not wasSuccessful
      const trigger = new Trigger(LONG_CONVERSATION, () => true, 1.0);
      const results = await engine.runTelemetry([trigger], makeConversation());

      Assert.ok(
        !results.some(r => r.telemetry_name === TELEMETRY_NAME),
        `${TELEMETRY_NAME} should not run when only ${LONG_CONVERSATION} trigger fires`
      );
      Assert.ok(
        results.some(r => r.telemetry_name === "isLongConvo"),
        `isLongConvo should run when ${LONG_CONVERSATION} trigger fires`
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(async function test_runTelemetryByName_happy_path() {
  const engine = new TelemetryEngine();
  const TELEMETRY_NAMES = ["wasSuccessful", "isLongConvo"];

  const sb = sinon.createSandbox();
  try {
    sb.stub(engine, "_fetchRecords").resolves([
      {
        id: "my-prompt-v1",
        version: "1.0",
        telemetry_name: "wasSuccessful",
        triggers: [UNIFORM_SAMPLE, `${UNIFORM_SAMPLE}_turn2`],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
        run_terminal: true,
      },
      {
        id: "my-prompt-v2",
        version: "1.0",
        telemetry_name: "isLongConvo",
        triggers: [LONG_CONVERSATION],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
        run_terminal: true,
      },
    ]);
    sb.stub(engine, "_runPrompts").callsFake(records =>
      Promise.resolve(
        records.map(r => ({ telemetry_name: r.telemetry_name, result: {} }))
      )
    );
    const fakeEngine = { run: sb.stub().resolves({}) };
    sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

    // long_conversation matches isLongConvo prompt, not wasSuccessful
    const results = await engine.runTelemetryByName(
      TELEMETRY_NAMES,
      makeConversation()
    );

    Assert.strictEqual(
      results.length,
      TELEMETRY_NAMES.length,
      `Results should be run on all telemetry`
    );
    Assert.ok(
      results.some(r => r.telemetry_name === TELEMETRY_NAMES[0]),
      `${TELEMETRY_NAMES[0]} should be run`
    );
    Assert.ok(
      results.some(r => r.telemetry_name === TELEMETRY_NAMES[1]),
      `${TELEMETRY_NAMES[1]} should be run`
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_runTelemetryByName_dont_run_terminal() {
  const engine = new TelemetryEngine();
  const TELEMETRY_NAMES = ["wasSuccessful", "isLongConvo"];

  const sb = sinon.createSandbox();
  try {
    sb.stub(engine, "_fetchRecords").resolves([
      {
        id: "my-prompt-v1",
        version: "1.0",
        telemetry_name: "wasSuccessful",
        triggers: [UNIFORM_SAMPLE, `${UNIFORM_SAMPLE}_turn2`],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
        run_terminal: true,
      },
      {
        id: "my-prompt-v2",
        version: "1.0",
        telemetry_name: "isLongConvo",
        triggers: [LONG_CONVERSATION],
        output_schema: { myField: ["a", "b"] },
        prompt: "...",
        run_terminal: false,
      },
    ]);
    sb.stub(engine, "_runPrompts").callsFake(records =>
      Promise.resolve(
        records.map(r => ({ telemetry_name: r.telemetry_name, result: {} }))
      )
    );
    const fakeEngine = { run: sb.stub().resolves({}) };
    sb.stub(TelemetryPromptEngine, "build").resolves(fakeEngine);

    // long_conversation matches isLongConvo prompt, not wasSuccessful
    const results = await engine.runTelemetryByName(
      TELEMETRY_NAMES,
      makeConversation()
    );

    Assert.strictEqual(
      results.length,
      1,
      `Results should not be run on non-terminal telemetry`
    );
    Assert.ok(
      results.some(r => r.telemetry_name === "wasSuccessful"),
      `wasSuccessful should be run`
    );
    Assert.ok(
      !results.some(r => r.telemetry_name === "isLongConvo"),
      `isLongConvo should not be run`
    );
  } finally {
    sb.restore();
  }
});

async function buildTestEngine(outputSchema) {
  const sb = sinon.createSandbox();
  sb.stub(openAIEngine, "_createEngine").resolves({});
  try {
    const engine = await TelemetryPromptEngine.build({
      telemetry_name: "test",
      version: "1.0",
      model: "fake-model",
      triggers: [UNIFORM_SAMPLE],
      output_schema: outputSchema,
      prompt: "test prompt {chatConversation} {fields}",
    });
    return { engine, sb };
  } catch (e) {
    sb.restore();
    throw e;
  }
}

add_task(async function test_verifyResult_returns_valid_values() {
  const schema = {
    wasSuccessful: ["successful", "not successful", "ongoing"],
    memoryReferenced: ["yes", "no"],
  };
  const { engine, sb } = await buildTestEngine(schema);
  try {
    const result = engine.verifyResult({
      finalOutput: JSON.stringify({
        wasSuccessful: "successful",
        memoryReferenced: "no",
      }),
    });
    Assert.equal(
      result.wasSuccessful,
      "successful",
      "valid value should be preserved"
    );
    Assert.equal(
      result.memoryReferenced,
      "no",
      "valid value should be preserved"
    );
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_verifyResult_returns_unknown_for_invalid_field_value() {
    const schema = {
      wasSuccessful: ["successful", "not successful", "ongoing"],
    };
    const { engine, sb } = await buildTestEngine(schema);
    try {
      const result = engine.verifyResult({
        finalOutput: JSON.stringify({ wasSuccessful: "definitely_successful" }),
      });
      Assert.equal(
        result.wasSuccessful,
        "unknown",
        "value not in schema should be replaced with 'unknown'"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(async function test_verifyResult_returns_unknown_for_missing_field() {
  const schema = {
    wasSuccessful: ["successful", "not successful", "ongoing"],
    memoryReferenced: ["yes", "no"],
  };
  const { engine, sb } = await buildTestEngine(schema);
  try {
    const result = engine.verifyResult({
      finalOutput: JSON.stringify({ wasSuccessful: "successful" }),
    });
    Assert.equal(
      result.wasSuccessful,
      "successful",
      "present valid field should be preserved"
    );
    Assert.equal(
      result.memoryReferenced,
      "unknown",
      "missing field should be 'unknown'"
    );
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_verifyResult_returns_all_unknown_for_invalid_json() {
    const schema = {
      wasSuccessful: ["successful", "not successful", "ongoing"],
      memoryReferenced: ["yes", "no"],
    };
    const { engine, sb } = await buildTestEngine(schema);
    try {
      const result = engine.verifyResult({
        finalOutput: "not { valid } json {{",
      });
      Assert.equal(
        result.wasSuccessful,
        "unknown",
        "invalid JSON should yield 'unknown'"
      );
      Assert.equal(
        result.memoryReferenced,
        "unknown",
        "invalid JSON should yield 'unknown' for all fields"
      );
    } finally {
      sb.restore();
    }
  }
);

add_task(async function test_normalizeMetadata_defaults() {
  const result = normalizeMetadata();

  Assert.deepEqual(result, {
    telemetry_version: "",
    chat_version: "",
    record_type: "",
    uniform_sampled: false,
    uniform_sampling_probability: 0,
    trigger_sampled: false,
    trigger_sampling_probability: 0,
    triggers: "",
  });
});

add_task(async function test_normalizeMetadata_scales_probabilities_and_formats_triggers() {
  const result = normalizeMetadata({
    telemetry_version: "1",
    chat_version: "chat-v2",
    record_type: "terminal",
    uniform_sampled: true,
    uniform_sampling_probability: 0.25,
    trigger_sampled: true,
    trigger_sampling_probability: 0.123,
    triggers: ["uniform_sample", "long_conversation"],
  });

  Assert.deepEqual(result, {
    telemetry_version: "1",
    chat_version: "chat-v2",
    record_type: "terminal",
    uniform_sampled: true,
    uniform_sampling_probability: 250,
    trigger_sampled: true,
    trigger_sampling_probability: 123,
    triggers: JSON.stringify(["uniform_sample", "long_conversation"]),
  });
});

add_task(async function test_submitTelemetryResult_records_one_event_per_attribute() {
  const sb = sinon.createSandbox();

  try {
    const recordStub = sb.stub(
      Glean.smartWindow.llmResponseTelemetry,
      "record"
    );

    const conversation = {
      id: "conversation-id",
      currentTurnIndex() {
        return 3;
      },
    };

    submitTelemetryResult(
      [
        {
          result: {
            was_successful: "successful",
            conversation_topic: "sports",
            memory_referenced: true,
          },
        },
      ],
      conversation,
      "fake-model",
      {
        telemetry_version: "1",
        chat_version: "chat-v1",
        record_type: "terminal",
        uniform_sampled: true,
        uniform_sampling_probability: 0.5,
        trigger_sampled: true,
        trigger_sampling_probability: 0.25,
        triggers: ["uniform_sample"],
      }
    );

    Assert.equal(
      recordStub.callCount,
      3,
      "Should record one event per telemetry result attribute"
    );

    const first = recordStub.getCall(0).args[0];

    Assert.withSoftAssertions(function (soft) {
      soft.equal(first.chat_id, "conversation-id");
      soft.equal(first.model, "fake-model");
      soft.equal(first.turn_number, 3);
      soft.equal(first.telemetry_version, "1");
      soft.equal(first.chat_version, "chat-v1");
      soft.equal(first.record_type, "terminal");
      soft.equal(first.uniform_sampled, true);
      soft.equal(first.uniform_sampling_probability, 500);
      soft.equal(first.trigger_sampled, true);
      soft.equal(first.trigger_sampling_probability, 250);
      soft.equal(first.triggers, JSON.stringify(["uniform_sample"]));
      soft.equal(first.attribute_name, "was_successful");
      soft.equal(first.attribute_value, "successful");
    });

    const second = recordStub.getCall(1).args[0];
    Assert.withSoftAssertions(function (soft) {
      soft.equal(second.attribute_name, "conversation_topic");
      soft.equal(second.attribute_value, "sports");
    });

    const third = recordStub.getCall(2).args[0];
    Assert.withSoftAssertions(function (soft) {
      soft.equal(third.attribute_name, "memory_referenced");
      soft.equal(third.attribute_value, "true");
    });
  } finally {
    sb.restore();
  }
});

add_task(async function test_submitTelemetryResult_uses_unknown_for_nullish_attribute_value() {
  const sb = sinon.createSandbox();

  try {
    const recordStub = sb.stub(
      Glean.smartWindow.llmResponseTelemetry,
      "record"
    );

    const conversation = {
      id: "conversation-id",
      currentTurnIndex() {
        return 1;
      },
    };

    submitTelemetryResult(
      [
        {
          result: {
            was_successful: null,
          },
        },
      ],
      conversation,
      "fake-model",
      {}
    );

    Assert.equal(recordStub.callCount, 1);

    const event = recordStub.getCall(0).args[0];
    Assert.equal(event.attribute_name, "was_successful");
    Assert.equal(event.attribute_value, "unknown");
  } finally {
    sb.restore();
  }
});
