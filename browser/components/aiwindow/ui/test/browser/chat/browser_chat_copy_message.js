/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);

// Trailing slash needed, see promiseNavigateAndLoad in head.js
const WATCHABLE_PAGE = "https://example.com/";
const MODEL_REPLY = "Hello from mock.";

const l10n = new Localization(["preview/aiWindow.ftl"]);

/**
 * Waits for the last assistant message to show `text`, then clicks a button
 * in its footer.
 *
 * @param {MozBrowser} aichatBrowser - The chat content browser
 * @param {string} text - The text the message shows
 * @param {string} buttonSelector - The button, inside the footer's shadow root
 */
async function clickLastAssistantFooterButton(
  aichatBrowser,
  text,
  buttonSelector
) {
  await SpecialPowers.spawn(
    aichatBrowser,
    [text, buttonSelector],
    async (shownText, selector) => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const lastOf = query =>
        [...chatContent.shadowRoot.querySelectorAll(query)].at(-1);
      await ContentTaskUtils.waitForCondition(
        () =>
          lastOf('ai-chat-message[data-message-role="assistant"]')
            ?.shadowRoot?.querySelector(".message-assistant")
            ?.innerText.trim() === shownText,
        "Waiting for the assistant message to show its text"
      );
      const footer = await ContentTaskUtils.waitForCondition(
        () => lastOf("assistant-message-footer"),
        "Waiting for the assistant message footer"
      );
      await footer.updateComplete;
      footer.shadowRoot.querySelector(selector).click();
    }
  );
}

/**
 * Asserts that copying the last assistant message puts `expected` on the
 * clipboard.
 *
 * @param {MozBrowser} aichatBrowser - The chat content browser
 * @param {string} expected - The text the message shows
 */
async function assertLastAssistantMessageCopied(aichatBrowser, expected) {
  // promiseClipboardChange starts watching the clipboard before it calls
  // the click, so the click has to be passed in rather than awaited first
  await SimpleTest.promiseClipboardChange(expected, () =>
    clickLastAssistantFooterButton(
      aichatBrowser,
      expected,
      "moz-button.copy-button"
    )
  );
}

describe("when the user copies an assistant message", () => {
  let gWin;
  let gSidebarBrowser;
  let gAichatBrowser;
  let gRestoreSignIn;
  let gRestoreEngine;
  let gOriginalRegion;

  beforeEach(async () => {
    gOriginalRegion = Region.home;
    Region._setHomeRegion("US", false);
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.agent.enabled", true],
        ["browser.smartwindow.agent.supportedRegions", "US"],
      ],
    });
    gRestoreSignIn = skipSignIn();
    ({ restore: gRestoreEngine } = await stubEngineNetworkBoundaries({
      serverOptions: { streamChunks: [MODEL_REPLY] },
    }));
    ({ win: gWin, sidebarBrowser: gSidebarBrowser } =
      await openAIWindowWithSidebar(WATCHABLE_PAGE));
    gAichatBrowser = await getAichatBrowser(gSidebarBrowser);
  });

  afterEach(async () => {
    await BrowserTestUtils.closeWindow(gWin);
    await gRestoreEngine();
    gRestoreSignIn();
    await SpecialPowers.popPrefEnv();
    Region._setHomeRegion(gOriginalRegion, false);
  });

  describe("that is localized", () => {
    it("should copy the text shown rather than the text sent to the model", async () => {
      await typeInSmartbar(gSidebarBrowser, "/watch this page for changes");
      await submitSmartbar(gSidebarBrowser);

      await SpecialPowers.spawn(gAichatBrowser, [], async () => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const card = await ContentTaskUtils.waitForCondition(
          () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
          "Waiting for the watch task card"
        );
        await card.updateComplete;
        card.shadowRoot.querySelector("#cancel-create-button").click();
      });

      await assertLastAssistantMessageCopied(
        gAichatBrowser,
        await l10n.formatValue("smartwindow-agent-monitor-canceled")
      );
    });
  });

  describe("that is not localized", () => {
    it("should copy the model's reply", async () => {
      await typeInSmartbar(gSidebarBrowser, "hello");
      await submitSmartbar(gSidebarBrowser);

      await assertLastAssistantMessageCopied(gAichatBrowser, MODEL_REPLY);
    });
  });
});
