/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Region } = ChromeUtils.importESModule(
  "resource://gre/modules/Region.sys.mjs"
);

const SUPPORTED_REGIONS_PREF = "browser.smartwindow.agent.supportedRegions";
const TEST_REGION = "US";
const TEST_MARKETS = JSON.stringify([[TEST_REGION, ["en-*"]]]);

add_setup(async function () {
  const originalRegion = Region.home;
  Region._setHomeRegion(TEST_REGION, false);
  registerCleanupFunction(() => {
    Region._setHomeRegion(originalRegion, false);
  });
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.search.suggest.enabled", false],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.agent.enabled", true],
      [SUPPORTED_REGIONS_PREF, TEST_MARKETS],
    ],
  });
});

/**
 * Reads the command panel's groups
 *
 * @param {MozBrowser} browser
 * @returns {Promise<Array>} panel groups
 */
async function getCommandGroups(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    return panelList.groups;
  });
}

add_task(async function test_command_palette_keyboard_navigation() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/");
  await waitForPanelOpen(sidebarBrowser);

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const panelList = smartbar.querySelector("smartwindow-panel-list");

    // The first command is selected by default
    await ContentTaskUtils.waitForCondition(
      () => panelList.selectedItemId === "watch",
      "The first command is selected by default"
    );

    smartbar.inputField.view.focus();

    // Arrow navigation keeps a valid selection
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, content);
    Assert.equal(
      panelList.selectedItemId,
      "watch",
      "ArrowDown keeps a command selected"
    );

    // Enter runs the highlighted command - it submits "/watch" to chat
    let committed = null;
    aiWindow.ownerDocument.addEventListener(
      "smartbar-commit",
      e => {
        committed = {
          action: e.detail.action,
          value: e.detail.value,
          submitType: e.detail.submitType,
        };
      },
      { capture: true, once: true }
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
    await ContentTaskUtils.waitForCondition(
      () => committed,
      "Enter submits the highlighted command to chat"
    );
    Assert.equal(committed.action, "chat", "Command is submitted to chat");
    Assert.equal(committed.value, "/watch", "Command value is submitted");
    Assert.equal(
      committed.submitType,
      "enter",
      "Enter key records submitType 'enter'"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_command_palette_escape_closes() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/");
  await waitForPanelOpen(sidebarBrowser);

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");

    smartbar.inputField.view.focus();
    EventUtils.synthesizeKey("KEY_Escape", {}, content);

    await ContentTaskUtils.waitForCondition(
      () => smartbar.inputField.isHandlingCommands === false,
      "Escape closes the command palette"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_slash_opens_command_palette() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/");
  await waitForPanelOpen(sidebarBrowser);

  const groups = await getCommandGroups(sidebarBrowser);
  Assert.equal(groups.length, 1, "One command group is shown");
  Assert.equal(
    groups[0].headerL10nId,
    "smartbar-command-tasks-header",
    "Group has the tasks header"
  );
  Assert.ok(
    groups[0].items.some(item => item.id === "watch"),
    "The watch command is listed"
  );

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_command_palette_opens_in_full_page() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await typeInSmartbar(browser, "/");
  await waitForPanelOpen(browser);

  const groups = await getCommandGroups(browser);
  Assert.ok(
    groups[0]?.items.some(item => item.id === "watch"),
    "The command palette opens in the full-page smartbar"
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    await ContentTaskUtils.waitForCondition(
      () => !smartbar.view?.isOpen,
      "The urlbar results view stays closed while the command palette is open"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_command_activates_chat_view_in_full_page() {
  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    Assert.ok(
      !aiWindow.classList.contains("chat-active"),
      "Full page starts on the new-tab starter view"
    );
  });

  await typeInSmartbar(browser, "/watch");
  await waitForPanelOpen(browser);

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    smartbar.inputField.view.focus();
    EventUtils.synthesizeKey("KEY_Enter", {}, content);

    await ContentTaskUtils.waitForCondition(
      () => aiWindow.classList.contains("chat-active"),
      "Running a command switches the new tab into the chat view"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_command_palette_filters_by_query() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/wat");
  await waitForPanelOpen(sidebarBrowser);
  const groups = await getCommandGroups(sidebarBrowser);
  Assert.ok(
    groups[0]?.items.some(item => item.id === "watch"),
    "'/wat' matches the watch command"
  );

  await typeInSmartbar(sidebarBrowser, "xyz");
  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    await ContentTaskUtils.waitForCondition(
      () => !panelList.groups.length,
      "Command panel empties when nothing matches"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_command_selection_submits_to_chat() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/");
  await waitForPanelOpen(sidebarBrowser);

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    const panel = panelList.shadowRoot.querySelector("panel-list");

    let committed = null;
    aiWindow.ownerDocument.addEventListener(
      "smartbar-commit",
      e => {
        committed = {
          action: e.detail.action,
          value: e.detail.value,
          submitType: e.detail.submitType,
        };
      },
      { capture: true, once: true }
    );

    const firstItem = panel.querySelector(
      "panel-item:not(.panel-section-header)"
    );
    firstItem.click();

    await ContentTaskUtils.waitForCondition(
      () => committed,
      "Clicking the command submits it to chat"
    );
    Assert.equal(committed.action, "chat", "Command is submitted to chat");
    Assert.equal(committed.value, "/watch", "Command value is submitted");
    Assert.equal(
      committed.submitType,
      "button",
      "Clicking a palette item records submitType 'button'"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_shift_enter_does_not_complete_command() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/wa");
  await waitForPanelOpen(sidebarBrowser);

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");

    smartbar.inputField.view.focus();
    EventUtils.synthesizeKey("KEY_Enter", { shiftKey: true }, content);

    Assert.ok(
      !smartbar.value.startsWith("/watch "),
      "Shift+Enter does not complete the command"
    );

    Assert.equal(
      smartbar.inputField.view.state.doc.childCount,
      2,
      "Shift+Enter inserts a line break"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_modifier_keys_do_not_complete_command() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await stubLoadURL(sidebarBrowser, { captureURL: true });
  await stubOpenSERP(sidebarBrowser);

  await typeInSmartbar(sidebarBrowser, "/wa");
  await waitForPanelOpen(sidebarBrowser);

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");

    smartbar.inputField.view.focus();

    for (const modifier of ["ctrlKey", "altKey", "metaKey"]) {
      EventUtils.synthesizeKey("KEY_Enter", { [modifier]: true }, content);
      Assert.ok(
        !smartbar.value.startsWith("/watch "),
        `${modifier}+Enter does not complete the command`
      );
    }
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_unknown_command_does_not_block_submit() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/wats");

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    await ContentTaskUtils.waitForCondition(
      () => smartbar.inputField.isHandlingCommands === false,
      "Unknown '/wats' does not keep the command palette handling input"
    );
  });

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_slash_command_classified_as_chat() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/watch watch the price");
  await waitForSmartbarAction(sidebarBrowser, "chat");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_slash_command_submit_does_not_navigate() {
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await stubLoadURL(sidebarBrowser, { captureURL: true });

  await typeInSmartbar(sidebarBrowser, "/watch watch the price");
  await submitSmartbar(sidebarBrowser);

  const { called } = await getStubLoadURLResult(sidebarBrowser);
  Assert.ok(!called, "Submitting a '/command' does not navigate Go");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_palette_hidden_when_agent_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.agent.enabled", false]],
  });

  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  await typeInSmartbar(sidebarBrowser, "/");

  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const panelList = smartbar.querySelector("smartwindow-panel-list");

    await ContentTaskUtils.waitForCondition(
      () =>
        !panelList.groups.length &&
        smartbar.inputField.isHandlingCommands === false,
      "Command palette stays closed when the agent pref is disabled"
    );
  });

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});
