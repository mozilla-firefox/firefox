/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

// AI chat content loads Fluent strings asynchronously, which may not complete
// before the test finishes. This is expected and doesn't affect test behavior.
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Missing message.*smartwindow-messages-document-title/
);

/**
 * Waits for the input-cta to show the stop button.
 *
 * @param {MozBrowser} browser
 */
async function waitForStopButton(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const inputCta = smartbar.querySelector("input-cta");
    await ContentTaskUtils.waitForCondition(
      () => inputCta.getAttribute("action") == "stop",
      "Wait for stop button to appear"
    );
  });
}

add_task(async function test_stop_button_appears_during_generation() {
  const mockEngineManager = new MockEngineManager();

  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  try {
    await typeInSmartbar(browser, "test message");
    await submitSmartbar(browser);
    await waitForStopButton(browser);

    const action = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowElement = content.document.querySelector("ai-window");
      const smartbar = aiWindowElement.shadowRoot.querySelector(
        "#ai-window-smartbar"
      );
      return smartbar.querySelector("input-cta").getAttribute("action");
    });

    Assert.equal(
      action,
      "stop",
      "Stop button should be visible during generation"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_stop_button_stops_generation() {
  const mockEngineManager = new MockEngineManager();

  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  try {
    await typeInSmartbar(browser, "test message");
    await submitSmartbar(browser);
    await waitForStopButton(browser);
    await submitSmartbar(browser, { useButton: true });

    // Stop button should disappear and CTA should be restored.
    const stopButtonGone = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowElement = content.document.querySelector("ai-window");
      const smartbar = aiWindowElement.shadowRoot.querySelector(
        "#ai-window-smartbar"
      );
      const inputCta = smartbar.querySelector("input-cta");
      await ContentTaskUtils.waitForCondition(
        () => inputCta.getAttribute("action") != "stop",
        "Stop button should disappear after clicking stop"
      );
      return inputCta.getAttribute("action") != "stop";
    });

    Assert.ok(
      stopButtonGone,
      "Stop button should disappear after clicking stop"
    );

    // Loading spinner should be dismissed.
    const innerBC = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowElement = content.document.querySelector("ai-window");
      const aichatBrowser = await ContentTaskUtils.waitForCondition(
        () => aiWindowElement.shadowRoot?.querySelector("#aichat-browser"),
        "Wait for aichat-browser"
      );
      return aichatBrowser.browsingContext;
    });

    const loaderGone = await SpecialPowers.spawn(innerBC, [], async () => {
      const chatContent = await ContentTaskUtils.waitForCondition(
        () => content.document.querySelector("ai-chat-content"),
        "Wait for ai-chat-content"
      );
      await ContentTaskUtils.waitForCondition(
        () => !chatContent.shadowRoot?.querySelector("chat-assistant-loader"),
        "Loading spinner should be dismissed after stop"
      );
      return !chatContent.shadowRoot?.querySelector("chat-assistant-loader");
    });

    Assert.ok(loaderGone, "Loading spinner should be dismissed after stop");
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(
  async function test_smartbar_cta_reflects_input_when_typing_during_generation() {
    const mockEngineManager = new MockEngineManager();

    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    try {
      await typeInSmartbar(browser, "first message");
      await submitSmartbar(browser);
      await waitForStopButton(browser);
      await typeInSmartbar(browser, "second message");
      await mockEngineManager.respondTo({
        purpose: "chat",
        response: ["Hello", " from", " mock."],
      });

      const action = await SpecialPowers.spawn(browser, [], async () => {
        const aiWindowElement = content.document.querySelector("ai-window");
        const smartbar = aiWindowElement.shadowRoot.querySelector(
          "#ai-window-smartbar"
        );
        const inputCta = smartbar.querySelector("input-cta");
        await ContentTaskUtils.waitForMutationCondition(
          inputCta,
          { attributes: true, attributeFilter: ["action"] },
          () => inputCta.getAttribute("action") !== "stop"
        );
        return inputCta.getAttribute("action");
      });

      Assert.equal(
        action,
        "chat",
        "Smartbar CTA should show current action after generation completes"
      );
    } finally {
      await BrowserTestUtils.closeWindow(win);
      await mockEngineManager.rejectAllRequests();
      mockEngineManager.cleanupMocks();
    }
  }
);
