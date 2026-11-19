/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Bug 1895789 to standarize contextmenu helpers in BrowserTestUtils
async function openContextMenu(doc = document, targetEl) {
  const contextMenu = doc.getElementById("contentAreaContextMenu");
  const promise = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  if (targetEl) {
    await BrowserTestUtils.synthesizeMouseAtCenter(
      targetEl,
      { type: "contextmenu" },
      doc.documentGlobal.gBrowser.selectedBrowser
    );
  } else {
    await BrowserTestUtils.synthesizeMouse(
      null,
      0,
      0,
      { type: "contextmenu" },
      doc.documentGlobal.gBrowser.selectedBrowser
    );
  }
  await promise;
}

async function hideContextMenu(doc = document) {
  const contextMenu = doc.getElementById("contentAreaContextMenu");
  const promise = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.hidePopup();
  await promise;
}

async function waitMenuState(menu, shouldHide, description) {
  await TestUtils.waitForCondition(() => {
    return menu && menu.hidden === shouldHide;
  }, description);
}

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.ml.chat.page.menuBadge");
});

/**
 * Check that chat context menu is shown with appropriate prefs set
 */
add_task(async function test_hidden_menu() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.page", false]],
  });

  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await openContextMenu();

    await this.waitMenuState(
      document.getElementById("context-ask-chat"),
      true,
      "Menu should be hidden"
    );

    Assert.ok(
      document.getElementById("context-ask-chat").hidden,
      "Ask chat menu is hidden"
    );
    await hideContextMenu();
  });
  // Test is using new page feature functionality
  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the chat context menu is hidden by default
 */
add_task(async function test_menu_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.provider", "http://localhost:8080"]],
  });
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await openContextMenu();

    await this.waitMenuState(
      document.getElementById("context-ask-chat"),
      false,
      "Menu should be visible"
    );

    Assert.ok(
      !document.getElementById("context-ask-chat").hidden,
      "Ask chat menu is shown"
    );
    await hideContextMenu();
  });
});

/**
 * Check that the remove option resets provider
 */
add_task(async function test_remove_option() {
  Services.fog.testResetFOG();
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    await openContextMenu();
    Assert.ok(
      Services.prefs.getStringPref("browser.ml.chat.provider"),
      "Provider is set"
    );

    await this.waitMenuState(
      document.getElementById("context-ask-chat"),
      false,
      "Menu should be visible"
    );

    const menu = document.getElementById("context-ask-chat");
    menu.getItemAtIndex(menu.itemCount - 1).click();

    await hideContextMenu();

    Assert.equal(
      Services.prefs.getStringPref("browser.ml.chat.provider"),
      "",
      "Provider reset"
    );

    const events = Glean.genaiChatbot.contextmenuRemove.testGetValue();
    Assert.equal(events.length, 1, "One remove event recorded");
    Assert.equal(events[0].extra.provider, "localhost", "Provider recorded");
  });
});

/**
 * Check that the chat context menu is hidden in popup windows because
 * they don't have a sidebar
 */
add_task(async function test_hidden_in_popup() {
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    const popupWindowPromise = BrowserTestUtils.waitForNewWindow();
    await SpecialPowers.spawn(browser, [], () => {
      content.open("https://example.com", "_blank", "popup");
    });
    const popupWin = await popupWindowPromise;

    await openContextMenu(popupWin.document);
    await waitMenuState(
      popupWin.document.getElementById("context-ask-chat"),
      true,
      "Menu should be hidden in popup"
    );
    await hideContextMenu(popupWin.document);

    await BrowserTestUtils.closeWindow(popupWin);
  });
});

/**
 * Check that the chat context menu is hidden inside a Smart Window,
 * both directly and when accessed from a sidebar panel sub-window.
 */
add_task(async function test_visible_in_smart_window() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.shortcuts.smartwindow", true],
    ],
  });

  const { GenAI } = ChromeUtils.importESModule(
    "moz-src:///browser/components/genai/GenAI.sys.mjs"
  );
  const menu = document.getElementById("context-ask-chat");
  const aiWindowDoc = {
    documentElement: { hasAttribute: attr => attr === "ai-window" },
  };
  const buildMenu = async (
    documentGlobal,
    selectionInfo = { text: "selected" }
  ) => {
    let hidden = null;
    await GenAI.buildAskChatMenu(menu, {
      browser: {
        browsingContext: { currentURI: { spec: "https://example.com" } },
        documentGlobal,
      },
      selectionInfo,
      showItem: (item, show) => {
        hidden = !show;
      },
      source: "page",
      contextTabs: null,
    });
    return hidden;
  };

  Assert.ok(
    !(await buildMenu({ document: aiWindowDoc })),
    "Menu shown when documentGlobal is the Smart Window with selection"
  );
  Assert.ok(
    !(await buildMenu({
      document: { documentElement: { hasAttribute: () => false } },
      browsingContext: { topChromeWindow: { document: aiWindowDoc } },
    })),
    "Menu shown when documentGlobal is a sidebar sub-window inside a Smart Window with selection"
  );
  Assert.ok(
    await buildMenu({ document: aiWindowDoc }, {}),
    "Menu hidden in Smart Window when there is no selection"
  );

  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.shortcuts.smartwindow", false],
    ],
  });

  Assert.ok(
    !(await buildMenu({ document: aiWindowDoc })),
    "Menu still shown in Smart Window even when smartwindow shortcuts pref is off"
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that handleAskChat submits to Smart Window when selection is from a sidebar sub-window
 */
add_task(async function test_smart_window_sidebar_ask_chat() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.smartwindow", true]],
  });

  const { GenAI } = ChromeUtils.importESModule(
    "moz-src:///browser/components/genai/GenAI.sys.mjs"
  );
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  const { AIWindowUI } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
  );

  const submitChatMessage = sinon.stub();
  const isSidebarOpenStub = sinon
    .stub(AIWindowUI, "isSidebarOpen")
    .returns(true);
  const getSidebarAiWindowStub = sinon
    .stub(AIWindowUI, "_getSidebarAiWindow")
    .returns({ submitChatMessage });

  const aiWindowDoc = {
    documentElement: { hasAttribute: attr => attr === "ai-window" },
  };

  await GenAI.handleAskChat(
    { id: "summarize", label: "Summarize" },
    {
      window: {
        browsingContext: {
          topChromeWindow: { document: aiWindowDoc },
        },
      },
      selection: "hello",
      contentType: "selection",
      entry: "page",
    }
  );

  Assert.ok(
    submitChatMessage.calledOnce,
    "submitChatMessage called from sidebar sub-window"
  );
  Assert.equal(
    submitChatMessage.firstCall.args[0].text,
    "Summarize: hello",
    "Prompt is label + selection"
  );

  isSidebarOpenStub.restore();
  getSidebarAiWindowStub.restore();

  await SpecialPowers.popPrefEnv();
});

/**
 * Check tab behavior of chat menu items without sidebar pref
 */
add_task(async function test_open_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.sidebar", false],
      ["browser.ml.chat.page", false],
    ],
  });
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const origTabs = gBrowser.tabs.length;
    await openContextMenu();
    await BrowserTestUtils.switchTab(gBrowser, () =>
      document.getElementById("context-ask-chat").getItemAtIndex(0).click()
    );
    await hideContextMenu();

    Assert.equal(gBrowser.tabs.length, origTabs + 1, "Chat opened tabs");
    Assert.ok(!SidebarController.isOpen, "Chat did not open sidebar");
    gBrowser.removeTab(gBrowser.selectedTab);
    await SpecialPowers.popPrefEnv();
  });
});

/**
 * Check sidebar behavior of chat menu items with sidebar pref
 */
add_task(async function test_open_sidebar() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.sidebar", true],
      ["browser.ml.chat.page", false],
    ],
  });
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const origTabs = gBrowser.tabs.length;
    await openContextMenu();
    document.getElementById("context-ask-chat").getItemAtIndex(0).click();
    await hideContextMenu();

    Assert.equal(gBrowser.tabs.length, origTabs, "Chat did not open tab");
    Assert.ok(SidebarController.isOpen, "Chat opened sidebar");
    SidebarController.hide();
  });

  let events = Glean.genaiChatbot.contextmenuPromptClick.testGetValue();
  Assert.equal(events.length, 1, "One context menu click");
  Assert.equal(events[0].extra.prompt, "summarize", "Picked summarize");
  Assert.equal(events[0].extra.provider, "localhost", "With localhost");
  Assert.equal(events[0].extra.selection, "0", "No selection");

  events = Glean.genaiChatbot.promptClick.testGetValue();
  Assert.equal(events.length, 1, "One context menu click");
  Assert.equal(events[0].extra.content_type, "selection", "Using selection");
  Assert.equal(events[0].extra.prompt, "summarize", "Picked summarize");
  Assert.equal(events[0].extra.provider, "localhost", "With localhost");
  Assert.equal(events[0].extra.selection, "0", "No selection");
  Assert.equal(events[0].extra.smart_window, "false", "Not smart window");
  Assert.equal(events[0].extra.source, "page", "From page menu");

  await SpecialPowers.popPrefEnv();
});

/**
 * Check modified prompts record as custom
 */
add_task(async function test_custom_prompt() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.prompts.0", JSON.stringify({ id: "prompt" })],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.sidebar", true],
    ],
  });
  await BrowserTestUtils.withNewTab("https://example.com", async () => {
    await openContextMenu();

    await this.waitMenuState(
      document.getElementById("context-ask-chat"),
      false,
      "Menu should be visible"
    );

    document.getElementById("context-ask-chat").getItemAtIndex(0).click();
    await hideContextMenu();
    SidebarController.hide();
  });

  Assert.equal(
    Glean.genaiChatbot.contextmenuPromptClick.testGetValue()[0].extra.prompt,
    "custom",
    "Custom id replaced with 'custom'"
  );

  await SpecialPowers.popPrefEnv();
});

const TURN_ON_ITEM = "context-turn-on-highlight-menu";
const MENU_OFF_PREFS = [
  ["browser.highlightToSearch.featureGate", true],
  ["browser.highlightToSearch.enabled", false],
  ["browser.highlightToSearch.search.enabled", false],
  ["browser.highlightToSearch.copy.enabled", false],
  ["browser.ml.chat.shortcuts", false],
  ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
  ["browser.ml.chat.provider", "http://localhost:8080"],
];

async function selectAllText(browser) {
  await SpecialPowers.spawn(browser, [], () => {
    content.getSelection().selectAllChildren(content.document.body);
  });
}

/**
 * Check that the item to turn the selection menu back on is only offered over
 * a selection while the menu is off.
 */
add_task(async function test_turn_on_highlight_menu_visibility() {
  await SpecialPowers.pushPrefEnv({ set: MENU_OFF_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await openContextMenu();
    Assert.ok(
      document.getElementById(TURN_ON_ITEM).hidden,
      "Not offered without a selection"
    );
    await hideContextMenu();

    await selectAllText(browser);
    await openContextMenu();
    Assert.ok(
      !document.getElementById(TURN_ON_ITEM).hidden,
      "Offered over a selection while the menu is off"
    );
    await hideContextMenu();

    await SpecialPowers.pushPrefEnv({
      set: [["browser.highlightToSearch.enabled", true]],
    });
    await openContextMenu();
    Assert.ok(
      document.getElementById(TURN_ON_ITEM).hidden,
      "Not offered while the menu is on"
    );
    await hideContextMenu();
    await SpecialPowers.popPrefEnv();

    await SpecialPowers.pushPrefEnv({
      set: [["browser.highlightToSearch.featureGate", false]],
    });
    await openContextMenu();
    Assert.ok(
      document.getElementById(TURN_ON_ITEM).hidden,
      "Not offered without the feature gate"
    );
    await hideContextMenu();
    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item is not offered over text selected in a text field, where
 * the selection menu is never shown.
 */
add_task(async function test_turn_on_highlight_menu_not_in_text_field() {
  await SpecialPowers.pushPrefEnv({ set: MENU_OFF_PREFS });

  await BrowserTestUtils.withNewTab(
    "data:text/html,<body style='margin:0'><textarea>some text</textarea>",
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        const textarea = content.document.querySelector("textarea");
        textarea.focus();
        textarea.select();
      });
      await openContextMenu();
      Assert.ok(
        document.getElementById(TURN_ON_ITEM).hidden,
        "Not offered over text selected in a text field"
      );
      await hideContextMenu();
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item turns the menu on with every action, records it, and
 * shows the menu over the selection, where it hides like any other.
 */
add_task(async function test_turn_on_highlight_menu_click() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [...MENU_OFF_PREFS, ["browser.highlightToSearch.callout.seen", false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllText(browser);
    await openContextMenu();

    const contextMenu = document.getElementById("contentAreaContextMenu");
    const panel = document.getElementById("selection-shortcut-action-panel");
    const menuHidden = BrowserTestUtils.waitForEvent(
      contextMenu,
      "popuphidden"
    );
    contextMenu.activateItem(document.getElementById(TURN_ON_ITEM));
    await menuHidden;
    Assert.ok(
      Services.prefs.getBoolPref("browser.highlightToSearch.callout.seen"),
      "Turning the menu on counts as seeing the callout"
    );

    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "The selection menu opens over the selection"
    );
    for (const pref of [
      "browser.highlightToSearch.enabled",
      "browser.highlightToSearch.search.enabled",
      "browser.highlightToSearch.copy.enabled",
      "browser.ml.chat.shortcuts",
    ]) {
      Assert.ok(Services.prefs.getBoolPref(pref), `${pref} is on`);
    }
    for (const id of [
      "ai-action-button",
      "search-action-button",
      "copy-action-button",
    ]) {
      Assert.ok(!document.getElementById(id).hidden, `${id} is shown`);
    }

    const toggled = Glean.selectionMenu.optionToggled.testGetValue();
    Assert.deepEqual(
      toggled.map(e => [e.extra.action, e.extra.enabled, e.extra.source]),
      [["master", "true", "context_menu"]],
      "Turning the menu on is recorded"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    await panelHidden;
    Assert.equal(panel.state, "closed", "It hides when the selection changes");
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item keeps the actions that were left on, rather than
 * turning everything on.
 */
add_task(async function test_turn_on_highlight_menu_keeps_actions() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MENU_OFF_PREFS,
      ["browser.highlightToSearch.search.enabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllText(browser);
    await openContextMenu();

    const contextMenu = document.getElementById("contentAreaContextMenu");
    const panel = document.getElementById("selection-shortcut-action-panel");
    const menuHidden = BrowserTestUtils.waitForEvent(
      contextMenu,
      "popuphidden"
    );
    contextMenu.activateItem(document.getElementById(TURN_ON_ITEM));
    await menuHidden;
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "The selection menu opens"
    );
    Assert.ok(
      !Services.prefs.getBoolPref("browser.highlightToSearch.copy.enabled"),
      "Copy stays off"
    );
    Assert.ok(
      document.getElementById("copy-action-button").hidden,
      "Copy is not shown"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item brings Search and Copy back when the chatbot is blocked,
 * where the AI action can't be what the menu was left with.
 */
add_task(async function test_turn_on_highlight_menu_chatbot_blocked() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MENU_OFF_PREFS,
      ["browser.ml.chat.enabled", false],
      ["browser.ml.chat.shortcuts", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllText(browser);
    await openContextMenu();

    const contextMenu = document.getElementById("contentAreaContextMenu");
    const panel = document.getElementById("selection-shortcut-action-panel");
    const menuHidden = BrowserTestUtils.waitForEvent(
      contextMenu,
      "popuphidden"
    );
    contextMenu.activateItem(document.getElementById(TURN_ON_ITEM));
    await menuHidden;
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "The selection menu opens with something to offer"
    );
    for (const pref of [
      "browser.highlightToSearch.search.enabled",
      "browser.highlightToSearch.copy.enabled",
    ]) {
      Assert.ok(Services.prefs.getBoolPref(pref), `${pref} is on`);
    }

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item is not offered in Smart Window.
 */
add_task(async function test_turn_on_highlight_menu_not_in_smart_window() {
  await SpecialPowers.pushPrefEnv({ set: MENU_OFF_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllText(browser);
    document.documentElement.setAttribute("ai-window", "");
    try {
      await openContextMenu();
      Assert.ok(
        document.getElementById(TURN_ON_ITEM).hidden,
        "Not offered in Smart Window"
      );
      await hideContextMenu();
    } finally {
      document.documentElement.removeAttribute("ai-window");
    }
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item is not offered over a link, even when its text is
 * selected.
 */
add_task(async function test_turn_on_highlight_menu_not_on_link() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MENU_OFF_PREFS,
      ["ui.mouse.right_click.select_under_cursor", false],
    ],
  });

  await BrowserTestUtils.withNewTab(
    "data:text/html,<a href='https://example.com/'>some link text</a>",
    async browser => {
      await selectAllText(browser);
      await openContextMenu(undefined, "a");
      Assert.ok(gContextMenu.onLink, "The menu was opened on a link");
      Assert.ok(
        document.getElementById(TURN_ON_ITEM).hidden,
        "Not offered over a link"
      );
      await hideContextMenu();
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the item is not offered over selected text that looks like a URL.
 */
add_task(async function test_turn_on_highlight_menu_not_on_plain_text_link() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MENU_OFF_PREFS,
      ["ui.mouse.right_click.select_under_cursor", false],
    ],
  });

  await BrowserTestUtils.withNewTab(
    "data:text/html,<p>visit <span>example.com</span> today</p>",
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        const span = content.document.querySelector("span");
        content.getSelection().selectAllChildren(span);
      });
      await openContextMenu(undefined, "span");
      Assert.ok(
        gContextMenu.onPlainTextLink,
        "The menu was opened on a plain text link"
      );
      Assert.ok(
        document.getElementById(TURN_ON_ITEM).hidden,
        "Not offered over a plain text link"
      );
      await hideContextMenu();
    }
  );

  await SpecialPowers.popPrefEnv();
});
