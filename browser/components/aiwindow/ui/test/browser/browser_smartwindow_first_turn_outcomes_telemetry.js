/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * This test case excludes a failed initial attempt even when a retry succeeds.
 */
add_task(async function test_failed_first_turn_and_retry() {
  const engineManager = new MockEngineManager();
  let win;

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    Services.fog.testResetFOG();

    await typeInSmartbar(browser, "fail this request");
    await submitSmartbar(browser);
    await engineManager.captureRequest({ purpose: "chat" });
    engineManager.engines.get("chat").rejectAllRequests();
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length === 1,
      "The failed response is reported"
    );
    Assert.equal(
      Glean.smartWindow.timeToTurnComplete.testGetValue(),
      undefined,
      "Failure does not count as successful completion"
    );

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    aiWindow.handleFooterAction({ action: "retry-after-error" });
    await engineManager.respondTo({ purpose: "chat", response: "Recovered." });
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length === 2,
      "The retry succeeds"
    );
    Assert.equal(
      Glean.smartWindow.modelResponse.testGetValue()[1].extra.error,
      "",
      "The retried response succeeds"
    );
    Assert.equal(
      Glean.smartWindow.timeToTurnComplete.testGetValue(),
      undefined,
      "A successful retry does not turn a failed first attempt into a sample"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await engineManager.rejectAllRequests();
    engineManager.cleanupMocks();
    Services.fog.testResetFOG();
  }
});

/**
 * This test case prevents a stopped first response from becoming a duration
 * sample when its model request subsequently resolves.
 */
add_task(async function test_cancelled_first_turn() {
  const engineManager = new MockEngineManager();
  let win;

  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    Services.fog.testResetFOG();

    await typeInSmartbar(browser, "stop this request");
    await submitSmartbar(browser);
    const pending = await engineManager.captureRequest({ purpose: "chat" });
    const doc = browser.contentDocument;
    doc.dispatchEvent(
      new doc.defaultView.CustomEvent("smartbar-stop-generation")
    );
    Assert.ok(
      !doc.querySelector("ai-window").isGenerating,
      "The stop action ends generation"
    );
    pending.respond("A late reply.");
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length === 1,
      "The cancelled request settles"
    );
    Assert.equal(
      Glean.smartWindow.timeToTurnComplete.testGetValue(),
      undefined,
      "A cancelled turn never records successful completion"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await engineManager.rejectAllRequests();
    engineManager.cleanupMocks();
    Services.fog.testResetFOG();
  }
});
