/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures that disabling Translations releases the actor's
 * process keepalive without waiting for the actor to be garbage collected.
 */
add_task(async function test_process_shutdown_after_feature_disabled() {
  const { cleanup } = await loadTestPage({ page: SPANISH_PAGE_URL });

  try {
    const engineParent = await EngineProcess.getTranslationsEngineParent();
    const processShutdown =
      TranslationsEngineTestUtils.waitForProcessShutdown(engineParent);

    info("Disabling Translations while retaining its inference process actor.");
    await TranslationsFeature.block();
    await processShutdown;

    is(
      engineParent.processKeepAlive,
      null,
      "The destroyed actor no longer owns a process keepalive"
    );
    ok(
      EngineProcess.areAllEnginesTerminated(),
      "Unregistering the Translations actor releases its inference process"
    );
  } finally {
    await cleanup();
  }
});

/**
 * This test case verifies that every client receives an engine-termination
 * notification and that explicitly closed ports are removed independently.
 */
add_task(async function test_multiple_ports_with_live_process() {
  const { cleanup, resolveDownloads } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["browser.ml.enable", true]],
  });
  let processKeepAlive = null;
  const portStates = [];
  let controlState = null;

  try {
    processKeepAlive =
      await TranslationsEngineTestUtils.keepInferenceProcessAlive();

    for (let index = 0; index < 3; index++) {
      const state = {
        engineStatus: "uninitialized",
        port: await TranslationsParent.requestTranslationsPort({
          sourceLanguage: "es",
          targetLanguage: "en",
        }),
        receivedEngineTerminated: false,
      };
      state.port.onmessage = ({ data }) => {
        switch (data.type) {
          case "TranslationsPort:GetEngineStatusResponse":
            state.engineStatus = data.status;
            break;
          case "TranslationsPort:EngineTerminated":
            state.receivedEngineTerminated = true;
            break;
        }
      };
      state.port.postMessage({
        type: "TranslationsPort:GetEngineStatusRequest",
      });
      portStates.push(state);
    }

    await resolveDownloads(1);
    await waitForCondition(
      () => portStates.every(state => state.engineStatus === "ready"),
      "Waiting for every client port to become ready."
    );
    is(
      (await processKeepAlive.engineParent.getEngineStateForTests())
        .activePortCount,
      3,
      "The translations engine independently registers every client port."
    );

    const closedState = portStates.shift();
    closedState.port.postMessage({ type: "TranslationsPort:Close" });
    closedState.port.close();
    await waitForCondition(
      async () =>
        (await processKeepAlive.engineParent.getEngineStateForTests())
          .activePortCount === 2,
      "Waiting for the explicitly closed port to be removed."
    );

    controlState = {
      engineStatus: "uninitialized",
      port: await TranslationsParent.requestTranslationsPort({
        sourceLanguage: "fr",
        targetLanguage: "en",
      }),
      receivedEngineTerminated: false,
    };
    controlState.port.onmessage = ({ data }) => {
      switch (data.type) {
        case "TranslationsPort:GetEngineStatusResponse":
          controlState.engineStatus = data.status;
          break;
        case "TranslationsPort:EngineTerminated":
          controlState.receivedEngineTerminated = true;
          break;
      }
    };
    controlState.port.postMessage({
      type: "TranslationsPort:GetEngineStatusRequest",
    });
    await resolveDownloads(1);
    await waitForCondition(
      () => controlState.engineStatus === "ready",
      "Waiting for the control engine to become ready."
    );

    await TranslationsEngineTestUtils.waitForIdleTimeoutWithProcessAlive(
      processKeepAlive.engineParent,
      { sourceLanguage: "es", targetLanguage: "en" }
    );

    await waitForCondition(
      () => portStates.every(state => state.receivedEngineTerminated),
      "Waiting for every remaining client to receive EngineTerminated."
    );
    ok(
      !controlState.receivedEngineTerminated,
      "Terminating one engine does not notify a client for another language pair."
    );
    const { activeEngineCount, activePortCount } =
      await processKeepAlive.engineParent.getEngineStateForTests();
    is(activeEngineCount, 1, "The control engine remains active.");
    is(
      activePortCount,
      1,
      "Engine termination only removes ports for its language pair."
    );
  } finally {
    for (const { port } of portStates) {
      port.close();
    }
    controlState?.port.close();
    try {
      await processKeepAlive?.release();
    } finally {
      await cleanup();
    }
  }
});
