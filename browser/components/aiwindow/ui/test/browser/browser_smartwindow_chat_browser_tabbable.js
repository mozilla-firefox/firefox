/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);

async function waitForAIWindow(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window:defined"),
      "Wait for ai-window to be defined"
    );
  });
}

// Empty Smart Window in fullpage mode previously left the embedded chat
// <browser> as a 0-height tab stop between the New Chat button and the
// smartbar. Verify it is now skipped while no chat is active.
add_task(async function test_aichat_browser_not_tabbable_when_chat_inactive() {
  const restoreSignIn = skipSignIn();
  const { restore } = await stubEngineNetworkBoundaries();

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await waitForAIWindow(browser);

    const result = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      const aichatBrowser = await ContentTaskUtils.waitForCondition(
        () => aiWindow.shadowRoot?.querySelector("#aichat-browser"),
        "Wait for #aichat-browser"
      );
      return {
        chatActive: aiWindow.classList.contains("chat-active"),
        tabindex: aichatBrowser.getAttribute("tabindex"),
      };
    });

    Assert.ok(
      !result.chatActive,
      "chat-active should not be set on a fresh Smart Window load"
    );
    Assert.equal(
      result.tabindex,
      "-1",
      "#aichat-browser should be removed from tab order while chat is inactive"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    restoreSignIn();
    await restore();
  }
});

// Once a chat starts and chat-active is applied, the embedded browser should
// rejoin tab order so its content (assistant message footer buttons, links,
// etc.) is reachable by keyboard.
add_task(async function test_aichat_browser_tabbable_when_chat_active() {
  const restoreSignIn = skipSignIn();
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: { streamChunks: ["Hello from mock."] },
  });

  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await waitForAIWindow(browser);

    await typeInSmartbar(browser, "start a chat");
    await submitSmartbar(browser);

    const tabindex = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForCondition(
        () => aiWindow.classList.contains("chat-active"),
        "chat-active should be applied after submitting a prompt"
      );
      return aiWindow.shadowRoot
        .querySelector("#aichat-browser")
        .getAttribute("tabindex");
    });

    Assert.equal(
      tabindex,
      null,
      "#aichat-browser should rejoin tab order while chat is active"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    restoreSignIn();
    await restore();
  }
});

// An agent command (e.g. /watch) renders the monitor card inside the chat
// browser, so it too must switch the layout to chat-active and put the browser
// back in the tab order. Previously this only happened in fullpage, so in the
// sidebar the whole "Create a task" panel was skipped by Tab/Shift+Tab until a
// click focused it (bug 2073449).
add_task(async function test_aichat_browser_tabbable_after_sidebar_command() {
  const restoreSignIn = skipSignIn();
  const { restore } = await stubEngineNetworkBoundaries();

  const originalRegion = Region.home;
  Region._setHomeRegion("US", false);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.agent.enabled", true],
      [
        "browser.smartwindow.agent.supportedRegions",
        JSON.stringify([["US", ["en-*"]]]),
      ],
    ],
  });

  let win;
  let sidebarBrowser;
  try {
    ({ win, sidebarBrowser } = await openAIWindowWithSidebar());

    await SpecialPowers.spawn(sidebarBrowser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      Assert.ok(
        !aiWindow.classList.contains("chat-active"),
        "Sidebar starts without the chat layout active"
      );
      const aichatBrowser = await ContentTaskUtils.waitForCondition(
        () => aiWindow.shadowRoot?.querySelector("#aichat-browser"),
        "Wait for #aichat-browser"
      );
      Assert.equal(
        aichatBrowser.getAttribute("tabindex"),
        "-1",
        "#aichat-browser is out of tab order before a command runs"
      );
    });

    await typeInSmartbar(sidebarBrowser, "/watch");
    await waitForPanelOpen(sidebarBrowser);

    await SpecialPowers.spawn(sidebarBrowser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
      smartbar.inputField.view.focus();
      EventUtils.synthesizeKey("KEY_Enter", {}, content);

      await ContentTaskUtils.waitForCondition(
        () => aiWindow.classList.contains("chat-active"),
        "Running /watch switches the sidebar into the chat view"
      );

      const aichatBrowser =
        aiWindow.shadowRoot.querySelector("#aichat-browser");
      Assert.equal(
        aichatBrowser.getAttribute("tabindex"),
        null,
        "#aichat-browser rejoins tab order so keyboard users can reach the task panel"
      );
    });
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await SpecialPowers.popPrefEnv();
    Region._setHomeRegion(originalRegion, false);
    restoreSignIn();
    await restore();
  }
});
