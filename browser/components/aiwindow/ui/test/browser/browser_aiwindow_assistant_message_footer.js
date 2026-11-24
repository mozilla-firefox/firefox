"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_assistant_message_footer_page.html";

add_task(async function test_message_footer_wires_buttons() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const doc = content.document;
      const footer = doc.getElementById("footer");

      footer.messageId = "msg-1";
      footer.appliedMemories = [{ memory_summary: "User is vegan" }];

      await content.customElements.whenDefined("assistant-message-footer");

      const shadow = footer.shadowRoot;
      ok(shadow, "Footer has a shadow root");

      const copyButton = shadow.querySelector("moz-button.copy-button");
      const retryButton = shadow.querySelector("moz-button.retry-button");
      const appliedButton = shadow.querySelector("applied-memories-button");

      ok(copyButton, "Found copy button");
      ok(retryButton, "Found retry button");
      ok(appliedButton, "Found applied memories button");

      is(
        appliedButton.messageId,
        "msg-1",
        "Footer passes messageId to applied memories button"
      );
      is(
        appliedButton.appliedMemories.length,
        1,
        "Footer passes appliedMemories to applied memories button"
      );

      let copyDetail = null;
      function onCopy(evt) {
        footer.removeEventListener("copy-message", onCopy);
        copyDetail = evt.detail;
      }
      footer.addEventListener("copy-message", onCopy);

      copyButton.click();
      await content.Promise.resolve();

      ok(copyDetail, "copy-message event fired");
      is(copyDetail.messageId, "msg-1", "copy-message includes messageId");

      let retryDetail = null;
      function onRetry(evt) {
        footer.removeEventListener("retry-message", onRetry);
        retryDetail = evt.detail;
      }
      footer.addEventListener("retry-message", onRetry);

      retryButton.click();
      await content.Promise.resolve();

      ok(retryDetail, "retry-message event fired");
      is(retryDetail.messageId, "msg-1", "retry-message includes messageId");
    });
  });
});

add_task(async function test_message_footer_hides_retry() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const footer = content.document.getElementById("footer");
      footer.messageId = "msg-1";
      footer.hideRetry = true;

      await content.customElements.whenDefined("assistant-message-footer");
      await footer.updateComplete;

      const shadow = footer.shadowRoot;
      ok(
        !shadow.querySelector("moz-button.retry-button"),
        "Retry button is not rendered when hideRetry is true"
      );

      footer.hideRetry = false;
      await footer.updateComplete;

      ok(
        shadow.querySelector("moz-button.retry-button"),
        "Retry button is rendered again once hideRetry is false"
      );
    });
  });
});

add_task(async function test_feedback_buttons() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    await SpecialPowers.spawn(browser, [], async () => {
      const footer = content.document.getElementById("footer");
      footer.messageId = "msg-1";

      await content.customElements.whenDefined("assistant-message-footer");

      const shadow = footer.shadowRoot;
      const thumbsUp = shadow.querySelector("moz-button.thumbs-up-button");
      const thumbsDown = shadow.querySelector("moz-button.thumbs-down-button");

      ok(thumbsUp, "thumbs-up button is visible");
      ok(thumbsDown, "thumbs-down button is visible");

      let upEventPromise = ContentTaskUtils.waitForEvent(footer, "thumbs-up");
      thumbsUp.click();
      let upEvent = await upEventPromise;
      is(upEvent.detail.messageId, "msg-1", "thumbs-up includes messageId");

      let downEventPromise = ContentTaskUtils.waitForEvent(
        footer,
        "thumbs-down"
      );
      thumbsDown.click();
      let downEvent = await downEventPromise;
      is(downEvent.detail.messageId, "msg-1", "thumbs-down includes messageId");
    });
  });
});

add_task(async function test_message_footer_shows_up_on_last_chunk() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Hello from mock.",
    });

    const aiWindowEl = browser.contentDocument?.querySelector("ai-window");
    const aichatBrowser = await TestUtils.waitForCondition(
      () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
      "Wait for aichat-browser"
    );

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => chatContent.shadowRoot.querySelector("assistant-message-footer")
      );

      Assert.ok(
        chatContent.shadowRoot.querySelector("assistant-message-footer"),
        "assistant chat bubble should have the footer when message is complete"
      );
    });
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    restoreSignIn();
  }
});
