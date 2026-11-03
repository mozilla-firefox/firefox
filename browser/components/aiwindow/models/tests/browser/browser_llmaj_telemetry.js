/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TelemetryScheduler } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/TelemetryManager.sys.mjs"
);
const {
  TelemetryEngine,
  TelemetryPromptEngine,
  Trigger,
  runLLMaJTelemetry,
  submitTelemetryResult,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs"
);
const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);

// Mirrors the (non-exported) constant in TelemetryManager.sys.mjs.
const LAST_RUN_PREF = "browser.smartwindow.lastLLMTelemetryRunTime";

/**
 * Replaces any scheduler started by the window with one that runs right away.
 *
 * @returns {Function} Restores a scheduler that waits for its cooldown.
 */
function startSchedulerNow() {
  TelemetryScheduler.maybeInit().destroy();
  const originalLastRun = Services.prefs.getIntPref(LAST_RUN_PREF, 0);
  Services.prefs.setIntPref(LAST_RUN_PREF, 0);
  const scheduler = TelemetryScheduler.maybeInit();
  return () => {
    scheduler.destroy();
    Services.prefs.setIntPref(
      LAST_RUN_PREF,
      originalLastRun || Math.floor(Date.now() / 1000)
    );
    TelemetryScheduler.maybeInit();
  };
}

function make429Error() {
  const error = new Error("429 status code");
  error.status = 429;
  return error;
}

add_task(async function test_updateLLMTelemetryRecord_merges_triggers() {
  const convId = Services.uuid.generateUUID().toString();

  await ChatStore.updateLLMTelemetryRecord(convId, {}, {}, 0, 0, [
    "uniform_sample",
  ]);
  await ChatStore.updateLLMTelemetryRecord(convId, {}, {}, 0, 0, [
    "uniform_sample",
    "min_turns",
  ]);

  const row = await ChatStore.findLLMTelemetryByConversationId(convId);
  Assert.deepEqual(
    row.triggers.sort(),
    ["min_turns", "uniform_sample"],
    "Triggers are merged without duplicates"
  );
});

add_task(async function test_runLLMaJTelemetry_queues_without_running() {
  Services.fog.testResetFOG();
  const sb = sinon.createSandbox();
  const conversation = {
    id: Services.uuid.generateUUID().toString(),
    currentTurnIndex: () => 2,
    _telemetryUniformProbability: 0.1,
  };

  sb.stub(TelemetryEngine.prototype, "getTriggers").resolves([
    new Trigger("uniform_sample", () => true, 0.1),
    new Trigger("min_turns", () => true, 0.5),
  ]);
  sb.stub(TelemetryEngine.prototype, "_fetchRecords").resolves([
    {
      version: "1.0",
      telemetry_name: "wasSuccessful",
      triggers: ["min_turns"],
      output_schema: {},
      prompt: "...",
    },
  ]);
  const buildSpy = sb.spy(TelemetryPromptEngine, "build");

  try {
    await runLLMaJTelemetry(conversation);

    const row = await ChatStore.findLLMTelemetryByConversationId(
      conversation.id
    );
    Assert.deepEqual(row.telemetryPrompts, { wasSuccessful: 2 });
    Assert.deepEqual(row.telemetryProbabilities, { wasSuccessful: 0.5 });
    Assert.equal(row.uniformSamplingProbability, 0.1);
    Assert.deepEqual(row.triggers.sort(), ["min_turns", "uniform_sample"]);
    Assert.equal(row.processed, 0, "Queued for the end-of-conversation run");
    Assert.ok(buildSpy.notCalled, "No mid-chat LLM engine is built");
    Assert.ok(
      !Glean.smartWindow.llmajBasedTelemetry.testGetValue()?.length,
      "No mid-chat records are submitted"
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_end_of_conversation_record_includes_triggers() {
  Services.fog.testResetFOG();
  const sb = sinon.createSandbox();
  const conv = {
    id: "c1",
    currentTurnIndex: () => 3,
    systemPromptVersion: "v1",
  };

  sb.stub(ChatStore, "getConversationsForTelemetry").resolves([
    {
      convId: "c1",
      modelId: "model",
      telemetryJobs: { wasSuccessful: 0 },
      telemetryProbs: { wasSuccessful: 0.5 },
      uniformSamplingProbability: 0.1,
      triggers: ["uniform_sample", "min_turns"],
    },
  ]);
  sb.stub(ChatStore, "findConversationById").resolves(conv);
  const markStub = sb.stub(ChatStore, "markLLMTelemetryProcessed").resolves();
  sb.stub(TelemetryEngine.prototype, "runTelemetryByName").resolves([
    {
      telemetry_name: "wasSuccessful",
      result: { wasSuccessful: "yes" },
      telemetry_version: "1.0",
    },
  ]);

  const restoreScheduler = startSchedulerNow();
  try {
    // The run sets LAST_RUN_PREF in its finally, after marking the conversation
    // processed, so wait for that too or the run outlives this task's cleanup.
    await TestUtils.waitForCondition(
      () => markStub.called && Services.prefs.getIntPref(LAST_RUN_PREF, 0),
      "scheduler marked the conversation as processed and finished its run"
    );

    const events = Glean.smartWindow.llmajBasedTelemetry.testGetValue();
    Assert.equal(events?.length, 1, "One end-of-conversation event recorded");
    const { extra } = events[0];
    Assert.equal(extra.record_type, "endOfConversation");
    Assert.equal(
      extra.triggers,
      JSON.stringify(["uniform_sample", "min_turns"]),
      "Stored trigger names are recorded"
    );
    Assert.equal(extra.uniform_sampled, "true");
    Assert.equal(extra.trigger_sampled, "true");
    Assert.equal(extra.chat_version, "v1");
  } finally {
    restoreScheduler();
    sb.restore();
  }
});

add_task(async function test_runPrompts_does_not_retry_and_rethrows_429() {
  const engine = new TelemetryEngine();
  const sb = sinon.createSandbox();
  const run = sb.stub();
  run.onFirstCall().rejects(new Error("bad output"));
  run.onSecondCall().rejects(make429Error());
  sb.stub(TelemetryPromptEngine, "build").resolves({ run });
  const pause = sb.stub(engine, "_pause").resolves();

  try {
    await Assert.rejects(
      engine._runPrompts(
        [
          { telemetry_name: "a" },
          { telemetry_name: "b" },
          { telemetry_name: "c" },
        ],
        {}
      ),
      /429/,
      "A 429 is rethrown to the caller"
    );
    Assert.equal(
      run.callCount,
      2,
      "Non-429 errors are skipped, nothing is retried, and the 429 stops the remaining prompts"
    );
    Assert.equal(
      pause.callCount,
      2,
      "Pauses before each prompt that ran, but not after the 429"
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_runPrompts_pauses_before_every_prompt() {
  const engine = new TelemetryEngine();
  const sb = sinon.createSandbox();
  const run = sb.stub().resolves({});
  sb.stub(TelemetryPromptEngine, "build").resolves({ run });
  const pause = sb.stub(engine, "_pause").resolves();

  try {
    const results = await engine._runPrompts(
      ["a", "b", "c"].map(telemetry_name => ({ telemetry_name })),
      {}
    );
    Assert.equal(results.length, 3, "All prompts ran");
    Assert.deepEqual(
      pause.args.map(([ms]) => ms),
      [4000, 4000, 4000],
      "A fixed pause precedes every prompt, including the first"
    );
    Assert.ok(
      pause.firstCall.calledBefore(run.firstCall),
      "The first prompt is not sent until after a pause"
    );
  } finally {
    sb.restore();
  }
});

add_task(async function test_judge_model_is_recorded() {
  Services.fog.testResetFOG();
  const engine = new TelemetryEngine();
  const sb = sinon.createSandbox();
  sb.stub(TelemetryPromptEngine, "build").resolves({
    run: async () => ({ wasSuccessful: "yes" }),
  });
  sb.stub(engine, "_pause").resolves();

  try {
    const results = await engine._runPrompts(
      [{ telemetry_name: "wasSuccessful", version: "1.0", model: "judge" }],
      {}
    );
    submitTelemetryResult(
      results,
      { id: "c1", currentTurnIndex: () => 0 },
      "inference",
      {}
    );

    const { extra } = Glean.smartWindow.llmajBasedTelemetry.testGetValue()[0];
    Assert.equal(extra.judge_model, "judge", "Judge model is recorded");
    Assert.equal(extra.model, "inference", "Inference model is recorded");
  } finally {
    sb.restore();
  }
});

add_task(async function test_scheduler_stops_run_on_429() {
  const sb = sinon.createSandbox();
  sb.stub(ChatStore, "getConversationsForTelemetry").resolves(
    ["c1", "c2"].map(convId => ({
      convId,
      modelId: "model",
      telemetryJobs: { wasSuccessful: 0 },
      telemetryProbs: {},
      triggers: [],
    }))
  );
  sb.stub(ChatStore, "findConversationById").callsFake(async id => ({
    id,
    currentTurnIndex: () => 0,
  }));
  const markStub = sb.stub(ChatStore, "markLLMTelemetryProcessed").resolves();
  const runStub = sb
    .stub(TelemetryEngine.prototype, "runTelemetryByName")
    .rejects(make429Error());

  const restoreScheduler = startSchedulerNow();
  try {
    await TestUtils.waitForCondition(
      () => runStub.called && Services.prefs.getIntPref(LAST_RUN_PREF, 0) !== 0,
      "scheduler finished its run"
    );
    Assert.equal(runStub.callCount, 1, "No conversations run after the 429");
    Assert.ok(
      markStub.notCalled,
      "The rate-limited conversation stays queued for the next run"
    );
  } finally {
    restoreScheduler();
    sb.restore();
  }
});

add_task(
  async function test_scheduler_marks_unloadable_conversation_processed() {
    const sb = sinon.createSandbox();
    sb.stub(ChatStore, "getConversationsForTelemetry").resolves(
      ["missing", "c2"].map(convId => ({
        convId,
        modelId: "model",
        telemetryJobs: { wasSuccessful: 0 },
        telemetryProbs: {},
        triggers: [],
        turnIndex: 3,
      }))
    );
    sb.stub(ChatStore, "findConversationById").callsFake(async id =>
      id === "missing" ? null : { id, currentTurnIndex: () => 0 }
    );
    const markStub = sb.stub(ChatStore, "markLLMTelemetryProcessed").resolves();
    const runStub = sb
      .stub(TelemetryEngine.prototype, "runTelemetryByName")
      .resolves([]);

    const restoreScheduler = startSchedulerNow();
    try {
      await TestUtils.waitForCondition(
        () => Services.prefs.getIntPref(LAST_RUN_PREF, 0) !== 0,
        "scheduler finished its run",
        100,
        100
      );
      Assert.ok(
        markStub.calledWith("missing", { wasSuccessful: 0 }, 3),
        "A conversation that fails to load is marked processed so it can't block the queue"
      );
      Assert.equal(
        runStub.callCount,
        1,
        "Only the loadable conversation is evaluated"
      );
      Assert.ok(
        markStub.calledWith("c2"),
        "The rest of the queue is still processed"
      );
    } finally {
      restoreScheduler();
      sb.restore();
    }
  }
);
