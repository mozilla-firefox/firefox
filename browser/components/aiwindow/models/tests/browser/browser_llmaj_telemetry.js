/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TelemetryScheduler } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/TelemetryManager.sys.mjs"
);
const { TelemetryEngine, TelemetryPromptEngine, Trigger, runLLMaJTelemetry } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/TelemetryUtils.sys.mjs"
  );
const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);

// Mirrors the (non-exported) constant in TelemetryManager.sys.mjs.
const LAST_RUN_PREF = "browser.smartwindow.lastLLMTelemetryRunTime";

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

  // Replace any scheduler started by the window with one that runs right away.
  TelemetryScheduler.maybeInit().destroy();
  const originalLastRun = Services.prefs.getIntPref(LAST_RUN_PREF, 0);
  Services.prefs.setIntPref(LAST_RUN_PREF, 0);
  const scheduler = TelemetryScheduler.maybeInit();
  try {
    // The run sets LAST_RUN_PREF in its finally, after its post-request delay,
    // so wait for that too or the run outlives this task's cleanup.
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
    scheduler.destroy();
    sb.restore();
    Services.prefs.setIntPref(
      LAST_RUN_PREF,
      originalLastRun || Math.floor(Date.now() / 1000)
    );
    TelemetryScheduler.maybeInit();
  }
});
