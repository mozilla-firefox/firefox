/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Smart tab grouping keeps labeling with its topic engine after the inference
 * process shuts it down for being idle. Only the model is swapped, for
 * test-echo, and the timeout is shortened so the test doesn't wait 2 minutes.
 */
add_task(async function test_smart_tab_topic_engine_runs_after_idle_shutdown() {
  const { cleanup } = await setup();
  const manager = new SmartTabGroupingManager();
  const request = { args: [["Tab titles to label"]], options: {} };

  try {
    const engine = await manager._createMLEngine({
      ...SMART_TAB_GROUPING_CONFIG.topicGeneration,
      modelId: "test-echo",
      timeoutMS: 1000,
    });
    manager.topicEngine = engine;
    await engine.run(request);
    is(engine.engineStatus, "ready", "The topic engine starts ready");

    await TestUtils.waitForCondition(
      () => EngineProcess.areAllEnginesTerminated(),
      "The inference process shuts down the idle topic engine"
    );
    is(
      engine.engineStatus,
      "suspended",
      "The topic engine is suspended after the idle shutdown"
    );
    ok(
      !SmartTabGroupingManager.isEngineClosed(engine),
      "Smart tab grouping keeps its idle topic engine rather than rebuilding it"
    );

    Assert.deepEqual(
      (await engine.run(request)).output,
      request.args,
      "The idle topic engine still labels tabs"
    );
    is(
      MLEngine.getInstance(engine.engineId)?.engineStatus,
      "ready",
      "Labeling after the idle shutdown reopens a ready topic engine"
    );
  } finally {
    await EngineProcess.destroyMLEngine();
    await cleanup();
  }
});
