/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Long enough that a single assistant reply overflows the scroll container on
// any window size the harness gives us.
const LONG_MESSAGE = "Lorem ipsum dolor sit amet ".repeat(200);

async function setupChatContent(streamChunks = ["Hello from mock."]) {
  const restoreSignIn = skipSignIn();
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: { streamChunks },
  });
  return { restoreSignIn, restore };
}

/**
 * Test that the overflowing attribute shows up when the scrollbar is visible.
 *
 * Bug 2067087: the attribute has to land on the *first* reply that overflows.
 * It used to take a second turn to appear, because .chat-inner-wrapper was
 * pinned to the scroll container's height, so the ResizeObserver driving the
 * attribute never saw the content grow.
 */
add_task(async function test_scrolling_and_overflowing_attribute() {
  const { restoreSignIn, restore } = await setupChatContent([LONG_MESSAGE]);

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const aiWindowEl = browser.contentDocument?.querySelector("ai-window");
    const aichatBrowser = await TestUtils.waitForCondition(
      () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
      "Wait for aichat-browser"
    );

    await typeInSmartbar(browser, "Tell me a long story");
    await submitSmartbar(browser);

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => chatContent.shadowRoot.querySelector(".chat-bubble-assistant")
      );
    });

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      // Let the ResizeObserver deliver the settled size before asserting.
      await new Promise(r =>
        content.requestAnimationFrame(() => content.requestAnimationFrame(r))
      );

      const chatContentWrapper = chatContent.shadowRoot.querySelector(
        ".chat-content-wrapper"
      );

      Assert.ok(chatContentWrapper, "chat-content-wrapper should exist");

      // Distinguishes "the attribute is wrong" from "the reply was not long
      // enough to overflow the harness window".
      Assert.greater(
        chatContentWrapper.scrollHeight,
        chatContentWrapper.clientHeight + 10,
        "Sanity check: a single reply overflows the scroll container"
      );

      await ContentTaskUtils.waitForCondition(
        () => chatContentWrapper.hasAttribute("overflowing"),
        "Wait for the overflowing attribute"
      );
      Assert.ok(
        chatContentWrapper.hasAttribute("overflowing"),
        "overflowing lands on the first reply, without a second turn"
      );

      // Bug 2037529: a scrollable region is inherently tab-focusable so it can
      // be scrolled with the keyboard, which produced a 'blank' focus stop
      // between the chat header and the first chat element. The explicit
      // tabindex="-1" opts out of that behavior.
      Assert.equal(
        chatContentWrapper.getAttribute("tabindex"),
        "-1",
        "chat-content-wrapper must opt out of scrollable-region tab focus"
      );
    });

    await BrowserTestUtils.closeWindow(win);
  } finally {
    restoreSignIn();
    await restore();
  }
});

add_task(async function test_long_word_wraps_in_assistant_reply() {
  const longWord = "3." + "1415926535".repeat(100);
  const { restoreSignIn, restore } = await setupChatContent([longWord]);

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const aiWindowEl = browser.contentDocument?.querySelector("ai-window");
    const aichatBrowser = await TestUtils.waitForCondition(
      () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
      "Wait for aichat-browser"
    );

    await typeInSmartbar(browser, "List 1000 digits of pi");
    await submitSmartbar(browser);

    await SpecialPowers.spawn(aichatBrowser, [longWord], async expected => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () =>
          ContentTaskUtils.querySelectorDeep(
            chatContent.shadowRoot,
            ".message-assistant"
          )?.textContent.includes(expected)
      );
      const messageText = ContentTaskUtils.querySelectorDeep(
        chatContent.shadowRoot,
        ".message-assistant"
      );
      const bubble = chatContent.shadowRoot.querySelector(
        ".chat-bubble-assistant"
      );
      const range = content.document.createRange();
      range.selectNodeContents(messageText);
      Assert.lessOrEqual(
        range.getBoundingClientRect().width,
        bubble.getBoundingClientRect().width,
        "The long word wraps within the assistant bubble"
      );

      const chatContentWrapper = chatContent.shadowRoot.querySelector(
        ".chat-content-wrapper"
      );
      Assert.lessOrEqual(
        chatContentWrapper.scrollWidth,
        chatContentWrapper.clientWidth,
        "The chat doesn't scroll horizontally"
      );
    });

    await BrowserTestUtils.closeWindow(win);
  } finally {
    restoreSignIn();
    await restore();
  }
});

/**
 * Test that the overflowing attribute DOESN'T show up when the scrollbar is NOT visible
 */
add_task(async function test_no_scrolling_and_no_overflowing_attribute() {
  const { restoreSignIn, restore } = await setupChatContent();

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(
      browser,
      "hello this should be a short message that doesn't trigger the overflowing attribute."
    );
    await submitSmartbar(browser);

    const aiWindowEl = browser.contentDocument?.querySelector("ai-window");
    const aichatBrowser = await TestUtils.waitForCondition(
      () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
      "Wait for aichat-browser"
    );

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      // Measuring before the reply renders would assert against a transcript
      // that is still growing, and pass or fail on timing alone.
      await ContentTaskUtils.waitForMutationCondition(
        chatContent.shadowRoot,
        { childList: true, subtree: true },
        () => chatContent.shadowRoot.querySelector(".chat-bubble-assistant")
      );
      // Let the ResizeObserver deliver the settled size before asserting.
      await new Promise(r =>
        content.requestAnimationFrame(() => content.requestAnimationFrame(r))
      );

      const chatContentWrapper = chatContent.shadowRoot.querySelector(
        ".chat-content-wrapper"
      );
      Assert.ok(chatContentWrapper, "chat-content-wrapper should exist");

      Assert.lessOrEqual(
        chatContentWrapper.scrollHeight,
        chatContentWrapper.clientHeight + 10,
        "Sanity check: the short conversation fits without scrolling"
      );

      Assert.ok(
        !chatContentWrapper.hasAttribute("overflowing"),
        "chat-content-wrapper should NOT have the overflowing attribute"
      );
    });

    await BrowserTestUtils.closeWindow(win);
  } finally {
    restoreSignIn();
    await restore();
  }
});
