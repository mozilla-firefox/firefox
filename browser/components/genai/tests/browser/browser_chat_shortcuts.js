/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

requestLongerTimeout(3);

const { GenAI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/GenAI.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);
const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);

/**
 * Check that shortcuts aren't shown by default
 */
add_task(async function test_no_shortcuts() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.provider", ""]],
  });
  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    browser.focus();
    goDoCommand("cmd_selectAll");

    const selectionShortcutActionPanel = document.getElementById(
      "selection-shortcut-action-panel"
    );

    Assert.ok(
      !selectionShortcutActionPanel.hasAttribute("panelopen"),
      "No shortcuts found"
    );
  });

  await SpecialPowers.popPrefEnv();
});

function visibleBox(popup) {
  const outer = popup.getOuterScreenRect();
  const client = popup.getBoundingClientRect();
  const content = popup.shadowRoot
    .querySelector("[part=content]")
    .getBoundingClientRect();
  return {
    top: outer.y + content.top - client.top,
    bottom: outer.y + content.bottom - client.top,
    left: outer.x + content.left - client.left,
  };
}

async function showSelectionMenu(browser, options) {
  const panel = document.getElementById("selection-shortcut-action-panel");
  Assert.ok(!panel.hasAttribute("panelopen"), "Menu starts closed");

  await selectAllAndMouseUp(browser, options);
  await TestUtils.waitForCondition(
    () => panel.getAttribute("panelopen") === "true",
    "Selection menu opened"
  );
  return panel;
}

/**
 * Check that the menu opens with no chatbot provider configured, so that
 * clicking the AI action can start onboarding.
 */
add_task(async function test_show_menu_without_provider() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.provider", ""],
    ],
  });

  const panel = document.getElementById("selection-shortcut-action-panel");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      await showSelectionMenu(browser);

      Assert.ok(
        panel.hasAttribute("panelopen"),
        "Menu opens without a chat provider"
      );
    });
  } finally {
    if (panel.state != "closed") {
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      panel.hidePopup();
      await hidden;
    }
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_selection_menu_position_with_rtl_app_locale() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["intl.l10n.pseudo", "bidi"],
    ],
  });
  Assert.ok(Services.locale.isAppLocaleRTL, "app locale is RTL for this task");

  const panel = document.getElementById("selection-shortcut-action-panel");

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await TestUtils.waitForCondition(
      () => getComputedStyle(browser).direction == "rtl",
      "Anchor resolved to RTL"
    );

    const clickX = 300;
    await selectAllAndMouseUp(browser, { releaseX: clickX, releaseY: 20 });
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened"
    );

    const browserRect = browser.getBoundingClientRect();
    const panelRect = panel.getBoundingClientRect();
    const clickViewportX = browserRect.left + clickX;
    const margin = 10;
    Assert.ok(
      clickViewportX >= panelRect.left - margin &&
        clickViewportX <= panelRect.right + margin,
      `Popup (left=${panelRect.left}, right=${panelRect.right}) spans the click position (${clickViewportX}) even with an RTL app locale`
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hidePopup();
    await hidden;
  });

  await SpecialPowers.popPrefEnv();
  await TestUtils.waitForCondition(
    () => getComputedStyle(document.documentElement).direction == "ltr",
    "UI reverted to LTR"
  );
});

/**
 * Check that a blocked chatbot leaves the menu open with just the Search
 * action, now that Search exists independently of the chatbot.
 */
add_task(async function test_no_menu_when_chatbot_blocked() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.enabled", false],
    ],
  });

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);

      Assert.ok(
        document.getElementById("ai-action-button").hidden,
        "AI action is hidden when the chatbot is blocked"
      );
      Assert.ok(
        !document.getElementById("search-action-button").hidden,
        "Search still has an action to offer"
      );

      panel.hide();
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that a blocked chatbot, an unavailable search service and a disabled
 * Copy action together leave no menu to show at all.
 */
add_task(async function test_no_menu_when_nothing_available() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.copy.enabled", false],
      ["browser.ml.chat.enabled", false],
    ],
  });
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");
  const panel = document.getElementById("selection-shortcut-action-panel");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      // "failed" rather than the other statuses because it is the one that
      // makes the default engine getters throw.
      SearchService.forceInitializationStatusForTests("failed");
      try {
        await selectAllAndMouseUp(browser);
        await TestUtils.waitForCondition(
          () => spy.called,
          "Actor offered shortcuts to the parent"
        );

        // openPopup() sets state synchronously, while the panelopen attribute
        // only lands on popupshown, too late for this assertion.
        Assert.equal(
          panel.state,
          "closed",
          "Menu stays closed with neither chatbot nor search available"
        );
      } finally {
        SearchService.forceInitializationStatusForTests("success");
      }
    });
  } finally {
    spy.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that turning the setting off hides the menu with the feature gate on,
 * even when a chatbot has already been chosen.
 */
add_task(async function test_no_menu_when_setting_off() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", false],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = document.getElementById("selection-shortcut-action-panel");
      await selectAllAndMouseUp(browser);
      await TestUtils.waitForCondition(
        () => spy.called,
        "Actor offered shortcuts to the parent"
      );

      Assert.equal(
        panel.state,
        "closed",
        "Menu stays closed with the setting off"
      );
    });
  } finally {
    spy.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that a new selection with nothing left to offer closes the menu
 * opened for the previous selection.
 */
add_task(async function test_menu_closes_when_nothing_left_to_offer() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", true],
      ["browser.highlightToSearch.copy.enabled", false],
      ["browser.ml.chat.enabled", false],
    ],
  });
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);

      SearchService.forceInitializationStatusForTests("failed");
      spy.resetHistory();
      await BrowserTestUtils.synthesizeMouse(
        browser,
        60,
        10,
        { type: "mouseup" },
        browser
      );
      await TestUtils.waitForCondition(
        () => spy.called,
        "Actor offered shortcuts to the parent again"
      );

      Assert.equal(panel.state, "closed", "Stale menu is closed");
    });
  } finally {
    SearchService.forceInitializationStatusForTests("success");
    spy.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that Search stays hidden for an existing chatbot user while the
 * Highlight to Search feature gate is off, so release users see no change.
 */
add_task(async function test_search_hidden_without_feature_gate() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", false],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);

      Assert.ok(
        document.getElementById("search-action-button").hidden,
        "Search stays hidden for an existing chatbot user while the gate is off"
      );

      panel.hide();
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that clicking Search opens the selection in a new tab with the
 * default engine, tagged with the text_selection SAP, and records the click.
 */
add_task(async function test_search_action_click() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", true],
      ["browser.ml.chat.provider", ""],
    ],
  });
  const loadSearchStub = sinon.stub(SearchUIUtils, "loadSearch");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const searchButton = document.getElementById("search-action-button");
      Assert.ok(!searchButton.hidden, "Search is offered without a chatbot");
      Assert.deepEqual(
        Glean.sapImpressionCounts.textSelection.testGetValue(),
        { [SearchService.defaultEngine.id]: 1 },
        "Showing Search records one impression, for the default engine"
      );

      const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      searchButton.click();
      await panelHidden;

      Assert.ok(loadSearchStub.calledOnce, "loadSearch was called");
      const args = loadSearchStub.firstCall.args[0];
      Assert.equal(args.searchText, "hi", "Searches the selection");
      Assert.equal(args.where, "tab", "Opens in a new tab");
      Assert.equal(args.sapSource, "text_selection", "Tags the new SAP");
      Assert.ok(args.triggeringPrincipal, "Carries a triggering principal");
      Assert.ok(
        "policyContainer" in args,
        "Passes the policy container through"
      );

      const clicks = Glean.selectionMenu.actionClick.testGetValue();
      Assert.equal(clicks.length, 1, "One action clicked");
      Assert.equal(
        clicks[0].extra.action,
        "search",
        "Clicked the search action"
      );
      Assert.equal(clicks[0].extra.selection, "2", "Records selection length");
      Assert.equal(
        clicks[0].extra.smart_window,
        "false",
        "Not a Smart Window click"
      );

      await TestUtils.waitForCondition(
        () => Glean.selectionMenu.dismissed.testGetValue()?.length == 1,
        "Dismissal recorded"
      );
      Assert.equal(
        Glean.selectionMenu.dismissed.testGetValue()[0].extra.interacted,
        "true",
        "Clicking Search is an interaction"
      );
    });
  } finally {
    loadSearchStub.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that with the chatbot available but Search turned off, only the AI
 * action is offered.
 */
add_task(async function test_search_disabled_keeps_ai_action() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", false],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);

      Assert.ok(
        !document.getElementById("ai-action-button").hidden,
        "AI action is offered"
      );
      Assert.ok(
        document.getElementById("search-action-button").hidden,
        "Search is hidden when turned off"
      );
      Assert.deepEqual(
        Glean.sapImpressionCounts.textSelection.testGetValue(),
        {},
        "No impression is recorded while Search is hidden"
      );

      panel.hide();
    });
  } finally {
    await SpecialPowers.popPrefEnv();
  }
});

async function doPrivateWindowImpressionTest(shouldRecordCounts) {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", true],
      ["browser.ml.chat.provider", ""],
      // This pref is the opposite of what it seems like it should be.
      ["browser.engagement.search_counts.pbm", !shouldRecordCounts],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  try {
    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "data:text/plain,hi"
    );
    const browser = win.gBrowser.selectedBrowser;
    await SimpleTest.promiseFocus(browser);

    const selectPromise = SpecialPowers.spawn(browser, [], () =>
      ContentTaskUtils.waitForCondition(() => content.getSelection().toString())
    );
    win.goDoCommand("cmd_selectAll");
    await selectPromise;
    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { type: "mouseup" },
      browser
    );

    const panel = win.document.getElementById(
      "selection-shortcut-action-panel"
    );
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened in a private window"
    );
    Assert.ok(
      !win.document.getElementById("search-action-button").hidden,
      "Search is offered in a private window"
    );
    Assert.deepEqual(
      Glean.sapImpressionCounts.textSelection.testGetValue(),
      shouldRecordCounts ? { [SearchService.defaultPrivateEngine.id]: 1 } : {},
      "The impression follows the private browsing counts pref"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
}

/**
 * Check that the Search impression is recorded in a private window, using the
 * private default engine, unless private browsing counts are turned off.
 */
add_task(async function test_private_window_impression_counts_enabled() {
  await doPrivateWindowImpressionTest(true);
});

add_task(async function test_private_window_impression_counts_disabled() {
  await doPrivateWindowImpressionTest(false);
});

/**
 * Check that Smart Window never offers the Search or Copy actions.
 */
add_task(async function test_smart_window_hides_search_and_copy() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", true],
      ["browser.highlightToSearch.copy.enabled", true],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcuts.smartwindow", true],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();

  try {
    win.document.documentElement.setAttribute("ai-window", "");

    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "data:text/plain,hi"
    );
    const browser = win.gBrowser.selectedBrowser;
    await selectAllAndMouseUp(browser, { win });

    const panel = win.document.getElementById(
      "selection-shortcut-action-panel"
    );
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened in Smart Window"
    );
    Assert.ok(
      win.document.getElementById("search-action-button").hidden,
      "Search is not offered in Smart Window"
    );
    Assert.ok(
      win.document.getElementById("copy-action-button").hidden,
      "Copy is not offered in Smart Window"
    );
    Assert.deepEqual(
      Glean.sapImpressionCounts.textSelection.testGetValue(),
      {},
      "No impression is recorded in Smart Window"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that the menu refuses the contexts that refuse chat entrypoints, such
 * as extension pages, Document Picture-in-Picture and popup windows.
 */
add_task(async function test_no_menu_in_unsupported_context() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.highlightToSearch.featureGate", true]],
  });
  const sandbox = sinon.createSandbox();
  const spy = sandbox.spy(GenAI, "handleShortcutsMessage");
  sandbox.stub(GenAI, "isSupportedContext").returns(false);

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = document.getElementById("selection-shortcut-action-panel");
      await selectAllAndMouseUp(browser);
      await TestUtils.waitForCondition(
        () => spy.called,
        "Actor offered shortcuts to the parent"
      );

      Assert.equal(
        panel.state,
        "closed",
        "Menu stays closed in an unsupported context"
      );
    });
  } finally {
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that shortcuts in Smart Window submit to Smart Window
 */
add_task(async function test_smart_window_ask_chat() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcuts.smartwindow", true],
    ],
  });

  const submitChatMessage = sinon.stub();
  const isSidebarOpenStub = sinon
    .stub(AIWindowUI, "isSidebarOpen")
    .returns(true);
  const getSidebarAiWindowStub = sinon
    .stub(AIWindowUI, "_getSidebarAiWindow")
    .returns({ submitChatMessage });

  const win = await BrowserTestUtils.openNewBrowserWindow();

  try {
    win.document.documentElement.setAttribute("ai-window", "");

    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "data:text/plain,hi"
    );
    const browser = win.gBrowser.selectedBrowser;
    await selectAllAndMouseUp(browser, { win });

    const selectionPanel = win.document.getElementById(
      "selection-shortcut-action-panel"
    );
    await TestUtils.waitForCondition(
      () => selectionPanel.getAttribute("panelopen") === "true"
    );
    Assert.ok(
      selectionPanel.hasAttribute("panelopen"),
      "Shortcuts shown in Smart Window"
    );

    const popup = win.document.getElementById("chat-shortcuts-options-panel");
    const shortcuts = win.document.getElementById("ai-action-button");
    shortcuts.click();
    await BrowserTestUtils.waitForEvent(popup, "popupshown");

    popup.querySelector("toolbarbutton").click();

    await TestUtils.waitForCondition(() => submitChatMessage.calledOnce);
    Assert.equal(
      submitChatMessage.firstCall.args[0].text,
      "Summarize: hi",
      "Prompt is label + selection"
    );
    Assert.equal(
      submitChatMessage.firstCall.args[0].submitType,
      "shortcuts",
      "submitType is shortcuts"
    );

    const events = Glean.genaiChatbot.promptClick.testGetValue();
    Assert.equal(events.length, 1, "One prompt click");
    Assert.equal(events[0].extra.smart_window, "true", "Is smart window");

    // The selection menu is one shared panel, so its probes have to mark the
    // Smart Window flow to keep it out of Highlight to Search analysis.
    const displayed = Glean.selectionMenu.displayed.testGetValue();
    Assert.equal(
      displayed.at(-1).extra.smart_window,
      "true",
      "Menu displayed in Smart Window"
    );
    const clicks = Glean.selectionMenu.actionClick.testGetValue();
    Assert.equal(
      clicks.at(-1).extra.smart_window,
      "true",
      "AI action clicked in Smart Window"
    );
  } finally {
    isSidebarOpenStub.restore();
    getSidebarAiWindowStub.restore();
    await BrowserTestUtils.closeWindow(win);
  }

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Smart Window shortcuts show even when classic window
 * shortcuts have been hidden via browser.ml.chat.shortcuts.
 */
add_task(async function test_smart_window_ask_chat_ignores_classic_toggle() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", false],
      ["browser.ml.chat.shortcuts.smartwindow", true],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();

  try {
    win.document.documentElement.setAttribute("ai-window", "");

    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "data:text/plain,hi"
    );
    const browser = win.gBrowser.selectedBrowser;
    await selectAllAndMouseUp(browser, { win });

    const selectionPanel = win.document.getElementById(
      "selection-shortcut-action-panel"
    );
    await TestUtils.waitForCondition(
      () => selectionPanel.getAttribute("panelopen") === "true"
    );
    Assert.ok(
      selectionPanel.hasAttribute("panelopen"),
      "Shortcuts shown in Smart Window even with classic shortcuts disabled"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the AI action reads as a dropdown: one button carrying a caret, with
 * the popup state on the button itself, opening the prompts submenu on click.
 */
add_task(async function test_ai_action_dropdown() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllAndMouseUp(browser);

    const panel = document.getElementById("selection-shortcut-action-panel");
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true"
    );

    const aiActionButton = document.getElementById("ai-action-button");
    await aiActionButton.updateComplete;

    Assert.ok(
      !aiActionButton.isSplitButton,
      "AI action is a single target, not a split button"
    );
    Assert.ok(
      !aiActionButton.chevronButtonEl,
      "No separate chevron half renders"
    );

    const background = aiActionButton.shadowRoot.querySelector(
      "#main-button .button-background"
    );
    Assert.notEqual(
      getComputedStyle(background, "::after").backgroundImage,
      "none",
      "A caret is drawn on the AI action"
    );

    const mainButton = aiActionButton.shadowRoot.querySelector("#main-button");
    Assert.equal(
      mainButton.getAttribute("aria-haspopup"),
      "menu",
      "The AI action reports it has a popup"
    );
    Assert.equal(
      mainButton.getAttribute("aria-expanded"),
      "false",
      "Submenu starts collapsed"
    );

    const popup = document.getElementById("chat-shortcuts-options-panel");
    const shown = BrowserTestUtils.waitForEvent(popup, "popupshown");
    aiActionButton.click();
    await shown;

    await TestUtils.waitForCondition(
      () => mainButton.getAttribute("aria-expanded") === "true"
    );
    Assert.equal(
      mainButton.getAttribute("aria-expanded"),
      "true",
      "The AI action reports the submenu is open"
    );

    const hidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    popup.hidePopup();
    await hidden;

    await TestUtils.waitForCondition(
      () => mainButton.getAttribute("aria-expanded") === "false"
    );
    Assert.equal(
      mainButton.getAttribute("aria-expanded"),
      "false",
      "The AI action reports the submenu is closed again"
    );

    await aiActionButton.updateComplete;
    Assert.notEqual(
      getComputedStyle(background, "::after").backgroundImage,
      "none",
      "The caret survives the active-state flip"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the menu's on/off state is reported as a state metric that
 * follows both the feature gate and the setting.
 */
add_task(async function test_enabled_metric() {
  for (const [gate, enabled] of [
    [true, true],
    [true, false],
    [false, true],
    [false, false],
  ]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", gate],
        ["browser.highlightToSearch.enabled", enabled],
      ],
    });
    Assert.strictEqual(
      Glean.selectionMenu.enabled.testGetValue(),
      gate && enabled,
      `Reports the menu with the gate ${gate} and the setting ${enabled}`
    );
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that the AI submenu keeps its hide item until the Highlight to Search
 * feature gate is on, when Settings takes over turning the shortcut off.
 */
add_task(async function test_hide_item_follows_feature_gate() {
  for (const gate of [false, true]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", gate],
        ["browser.ml.chat.shortcuts", true],
        ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
        ["browser.ml.chat.provider", "http://localhost:8080"],
      ],
    });

    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);

      const popup = document.getElementById("chat-shortcuts-options-panel");
      const shown = BrowserTestUtils.waitForEvent(popup, "popupshown");
      EventUtils.synthesizeMouseAtCenter(
        document.getElementById("ai-action-button"),
        {},
        window
      );
      await shown;

      Assert.equal(
        !!popup.querySelector("[data-l10n-id='genai-shortcuts-hide']"),
        !gate,
        `The hide item is ${gate ? "absent" : "present"} with the gate ${
          gate ? "on" : "off"
        }`
      );

      const popupHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
      popup.hidePopup();
      await popupHidden;
      const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      panel.hide();
      await panelHidden;
      await TestUtils.waitForCondition(
        () => !panel.hasAttribute("panelopen"),
        "The selection menu closed"
      );
    });

    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check the selection menu records its own display and action click events,
 * separately from the chatbot-scoped shortcuts probes.
 */
add_task(async function test_selection_menu_events() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    const displayed = Glean.selectionMenu.displayed.testGetValue();
    Assert.equal(displayed.length, 1, "Menu displayed once");
    Assert.ok(displayed[0].extra.delay, "Waited some time");
    Assert.equal(displayed[0].extra.selection, 2, "Selected hi");
    Assert.equal(
      displayed[0].extra.smart_window,
      "false",
      "Not a Smart Window menu"
    );

    Assert.equal(
      Glean.selectionMenu.actionClick.testGetValue(),
      null,
      "No action clicked yet"
    );

    const popup = document.getElementById("chat-shortcuts-options-panel");
    document.getElementById("ai-action-button").click();
    await BrowserTestUtils.waitForEvent(popup, "popupshown");

    const clicks = Glean.selectionMenu.actionClick.testGetValue();
    Assert.equal(clicks.length, 1, "One action clicked");
    Assert.equal(clicks[0].extra.action, "ai", "Clicked the AI action");
    Assert.equal(clicks[0].extra.selection, 2, "Selected hi");
    Assert.equal(
      clicks[0].extra.smart_window,
      "false",
      "Not a Smart Window click"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

async function hideAndWaitForDismissed(panel, count) {
  const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hide();
  await panelHidden;
  return TestUtils.waitForCondition(
    () => Glean.selectionMenu.dismissed.testGetValue()?.length == count,
    `${count} dismissal(s) recorded`
  );
}

/**
 * Check that closing the menu records one dismissal, without an interaction.
 */
add_task(async function test_dismissed_without_interaction() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    Assert.equal(
      Glean.selectionMenu.dismissed.testGetValue(),
      null,
      "Nothing dismissed while the menu is open"
    );

    await hideAndWaitForDismissed(panel, 1);
    const [dismissed] = Glean.selectionMenu.dismissed.testGetValue();
    Assert.equal(dismissed.extra.interacted, "false", "Not interacted");
    Assert.greaterOrEqual(Number(dismissed.extra.duration), 0, "Has duration");
    Assert.equal(dismissed.extra.smart_window, "false", "Not Smart Window");
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a prompt chosen from the AI submenu counts as an interaction.
 */
add_task(async function test_dismissed_after_prompt_click() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });
  const askStub = sinon.stub(GenAI, "handleAskChat");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const popup = document.getElementById("chat-shortcuts-options-panel");
      const popupShown = BrowserTestUtils.waitForEvent(popup, "popupshown");
      await EventUtils.synthesizeMouseAtCenter(
        document.getElementById("ai-action-button"),
        {},
        window
      );
      await popupShown;

      const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      await EventUtils.synthesizeMouseAtCenter(
        popup.querySelector("toolbarbutton"),
        {},
        window
      );
      await panelHidden;
      await TestUtils.waitForCondition(
        () => Glean.selectionMenu.dismissed.testGetValue()?.length == 1,
        "Dismissal recorded"
      );

      Assert.ok(askStub.calledOnce, "The prompt was handled");
      Assert.equal(
        Glean.selectionMenu.dismissed.testGetValue()[0].extra.interacted,
        "true",
        "Choosing a prompt is an interaction"
      );
    });
  } finally {
    askStub.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that the AI submenu opening on hover during onboarding is not
 * mistaken for an interaction, although it records an action click.
 */
add_task(async function test_dismissed_after_onboarding_hover() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 0],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const popup = document.getElementById("chat-shortcuts-options-panel");
    const popupShown = BrowserTestUtils.waitForEvent(popup, "popupshown");
    EventUtils.sendMouseEvent(
      { type: "mouseover" },
      document.getElementById("ai-action-button")
    );
    await popupShown;
    Assert.equal(
      Glean.selectionMenu.actionClick.testGetValue()?.length,
      1,
      "The hover open records an action click"
    );

    await hideAndWaitForDismissed(panel, 1);
    Assert.equal(
      Glean.selectionMenu.dismissed.testGetValue()[0].extra.interacted,
      "false",
      "A hover-only open is not an interaction"
    );
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a new selection while the menu is still open ends the first
 * display, so displays and dismissals stay paired.
 */
add_task(async function test_dismissed_pairs_with_displayed_on_reshow() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { type: "mouseup" },
      browser
    );
    await TestUtils.waitForCondition(
      () => Glean.selectionMenu.displayed.testGetValue()?.length == 2,
      "Menu displayed again for the second selection"
    );

    await hideAndWaitForDismissed(panel, 2);
    Assert.equal(
      Glean.selectionMenu.displayed.testGetValue().length,
      Glean.selectionMenu.dismissed.testGetValue().length,
      "Every display has a dismissal"
    );
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Copy stays gated on the Nimbus feature even when the chatbot
 * path already shows the menu, so existing chat-shortcut users don't see it
 * before the feature ships.
 */
add_task(async function test_copy_action_gated_on_feature() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", false],
      ["browser.highlightToSearch.copy.enabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    Assert.ok(
      document.getElementById("copy-action-button").hidden,
      "Copy stays hidden without the feature gate, even for an existing chatbot user"
    );
    Assert.less(
      document
        .getElementById("copy-action-confirmation")
        .getBoundingClientRect().width,
      1,
      "Confirmation pill has no footprint without a copy"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Copy is hidden when its own setting is off, even with the
 * feature fully enabled.
 */
add_task(async function test_copy_action_hidden_when_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.copy.enabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    Assert.ok(
      document.getElementById("copy-action-button").hidden,
      "Copy stays hidden while its own setting is off"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Copy alone is enough to open the menu, so a user who has turned
 * off the chatbot and Search can still copy.
 */
add_task(async function test_menu_shows_with_only_copy() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", false],
      ["browser.highlightToSearch.copy.enabled", true],
      ["browser.ml.chat.enabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    Assert.ok(
      !document.getElementById("copy-action-button").hidden,
      "Copy is shown"
    );
    Assert.ok(
      document.getElementById("search-action-button").hidden,
      "Search is hidden"
    );
    Assert.ok(
      document.getElementById("ai-action-button").hidden,
      "AI is hidden"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that clicking Copy writes the selection to the clipboard, records
 * the action click, announces a confirmation to assistive technology, and
 * ends the interaction by closing the menu.
 */
add_task(async function test_copy_action() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.copy.enabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    const copyActionButton = document.getElementById("copy-action-button");
    Assert.ok(
      !copyActionButton.hidden,
      "Copy is shown with the feature enabled"
    );

    const moreActionsButton = document.getElementById("more-actions-button");
    const moreActionsSeparator = document.getElementById(
      "more-actions-separator"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(moreActionsButton) &&
        BrowserTestUtils.isVisible(moreActionsSeparator),
      "The three-dot button and its separator are shown before a copy"
    );

    const confirmation = document.getElementById("copy-action-confirmation");
    Assert.equal(
      confirmation.getAttribute("role"),
      "status",
      "Confirmation is announced to assistive technology"
    );
    Assert.less(
      confirmation.getBoundingClientRect().width,
      1,
      "Confirmation has no footprint before a copy"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await new Promise((resolve, reject) => {
      SimpleTest.waitForClipboard(
        "hi",
        () => EventUtils.synthesizeMouseAtCenter(copyActionButton, {}, window),
        resolve,
        () => reject(new Error("Clipboard did not receive the selection"))
      );
    });

    await TestUtils.waitForCondition(
      () => confirmation.hasAttribute("copied"),
      "Confirmation is shown after the text is translated"
    );
    Assert.greater(
      confirmation.getBoundingClientRect().width,
      0,
      "Confirmation shown after copying"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(moreActionsButton),
      "The confirmation replaces the three-dot button"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(moreActionsSeparator),
      "The rule stays on the near side of the confirmation"
    );
    Assert.lessOrEqual(
      moreActionsSeparator.getBoundingClientRect().right,
      confirmation.getBoundingClientRect().left,
      "The confirmation starts after the rule"
    );
    Assert.notEqual(
      getComputedStyle(confirmation).backgroundColor,
      "rgba(0, 0, 0, 0)",
      "Confirmation renders as a filled pill, not bare text"
    );
    Assert.greaterOrEqual(
      confirmation.getBoundingClientRect().left,
      copyActionButton.getBoundingClientRect().right,
      "Confirmation doesn't cover the clicked Copy icon"
    );
    const pillRect = confirmation.getBoundingClientRect();
    const tickRect = confirmation
      .querySelector(".copy-action-confirmation-icon")
      .getBoundingClientRect();
    Assert.less(
      Math.abs(tickRect.width - tickRect.height),
      1,
      "The tick sits in a square"
    );
    Assert.less(
      Math.abs(tickRect.height - pillRect.height),
      1,
      "The tick's square is as tall as the pill, so it reads as a circle in the round end"
    );

    const clicks = Glean.selectionMenu.actionClick.testGetValue();
    Assert.equal(clicks.length, 1, "One action clicked");
    Assert.equal(clicks[0].extra.action, "copy", "Clicked the copy action");
    Assert.equal(clicks[0].extra.selection, 2, "Selected hi");

    await TestUtils.waitForCondition(
      () => panel.hasAttribute("copy-fading"),
      "The whole bar fades out, not just the confirmation"
    );
    await panelHidden;
    Assert.ok(
      !panel.hasAttribute("copy-fading"),
      "The fade is cleared so the next selection shows the bar"
    );

    await TestUtils.waitForCondition(
      () => Glean.selectionMenu.dismissed.testGetValue()?.length == 1,
      "Dismissal recorded"
    );
    Assert.equal(
      Glean.selectionMenu.dismissed.testGetValue()[0].extra.interacted,
      "true",
      "Clicking Copy is an interaction"
    );
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the bar grows to fit the translated Copied pill, rather than
 * being sized for the pill before its text has been translated.
 */
add_task(async function test_copy_action_bar_fits_confirmation() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.search.enabled", false],
      ["browser.highlightToSearch.copy.enabled", true],
      ["browser.ml.chat.enabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const initialWidth = panel.getBoundingClientRect().width;
    const copyActionButton = document.getElementById("copy-action-button");
    const confirmation = document.getElementById("copy-action-confirmation");

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await new Promise((resolve, reject) => {
      SimpleTest.waitForClipboard(
        "hi",
        () => EventUtils.synthesizeMouseAtCenter(copyActionButton, {}, window),
        resolve,
        () => reject(new Error("Clipboard did not receive the selection"))
      );
    });

    await TestUtils.waitForCondition(
      () => confirmation.hasAttribute("copied"),
      "Confirmation is shown after the text is translated"
    );
    Assert.ok(
      confirmation.querySelector(".copy-action-confirmation-text").textContent,
      "Confirmation text is translated"
    );

    const pillRect = confirmation.getBoundingClientRect();
    const panelRect = panel.getBoundingClientRect();
    Assert.greater(
      panelRect.width,
      initialWidth,
      "The panel grows to fit the confirmation"
    );
    Assert.greaterOrEqual(
      pillRect.left,
      panelRect.left,
      "Confirmation doesn't overflow the start edge of the panel"
    );
    Assert.lessOrEqual(
      pillRect.right,
      panelRect.right,
      "Confirmation doesn't overflow the end edge of the panel"
    );
    Assert.lessOrEqual(
      confirmation.scrollWidth,
      confirmation.clientWidth,
      "Confirmation's content isn't wider than the pill"
    );
    const actionsBox = panel.querySelector(".panel-subview-body");
    Assert.lessOrEqual(
      actionsBox.scrollWidth,
      actionsBox.clientWidth,
      "Nothing in the bar is clipped"
    );
    Assert.greaterOrEqual(
      pillRect.left,
      copyActionButton.getBoundingClientRect().right,
      "Confirmation doesn't cover the Copy icon"
    );

    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

const MORE_ACTIONS_PREFS = [
  ["browser.ml.chat.shortcuts", true],
  ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
  ["browser.ml.chat.provider", "http://localhost:8080"],
  ["browser.highlightToSearch.featureGate", true],
  ["browser.highlightToSearch.enabled", true],
  ["browser.highlightToSearch.search.enabled", true],
  ["browser.highlightToSearch.copy.enabled", true],
];

async function openMoreActions() {
  const moreActionsPanel = document.getElementById(
    "selection-more-actions-panel"
  );
  const shown = BrowserTestUtils.waitForEvent(moreActionsPanel, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("more-actions-button"),
    {},
    window
  );
  await shown;
  return moreActionsPanel;
}

async function clickToggle(id) {
  const toggle = document.getElementById(id);
  await toggle.updateComplete;
  EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {});
  await toggle.updateComplete;
}

/**
 * Check that the three-dot button stays hidden for an existing chatbot user
 * while the Highlight to Search feature gate is off.
 */
add_task(async function test_more_actions_hidden_without_feature_gate() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.highlightToSearch.featureGate", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    Assert.ok(
      document.getElementById("more-actions-button").hidden,
      "Three-dot button stays hidden without the feature gate"
    );
    Assert.ok(
      document.getElementById("more-actions-separator").hidden,
      "The rule before it stays hidden too"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the three-dot menu shows a toggle per action reflecting its pref,
 * and records that it was shown.
 */
add_task(async function test_more_actions_menu_shows_toggles() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.highlightToSearch.copy.enabled", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    const moreActionsButton = document.getElementById("more-actions-button");
    Assert.ok(!moreActionsButton.hidden, "Three-dot button is shown");

    const moreActionsPanel = await openMoreActions();
    const aiToggle = document.getElementById("more-actions-ai-toggle");
    const searchToggle = document.getElementById("more-actions-search-toggle");
    const copyToggle = document.getElementById("more-actions-copy-toggle");
    Assert.ok(aiToggle.label, "AI row is already named when the menu opens");
    await Promise.all(
      [aiToggle, searchToggle, copyToggle].map(toggle => toggle.updateComplete)
    );

    Assert.ok(!aiToggle.hidden, "AI row is present");
    Assert.ok(aiToggle.pressed, "AI is on");
    Assert.ok(searchToggle.pressed, "Search is on");
    Assert.ok(!copyToggle.pressed, "Copy is off");

    const shown = Glean.selectionMenu.menuShown.testGetValue();
    Assert.equal(shown.length, 1, "Three-dot menu shown once");
    Assert.deepEqual(
      [shown[0].extra.ai, shown[0].extra.search, shown[0].extra.copy],
      ["on", "on", "off"],
      "The shown event carries each row's state"
    );

    const hidden = BrowserTestUtils.waitForEvent(
      moreActionsPanel,
      "popuphidden"
    );
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("more-actions-button"),
      {}
    );
    await hidden;

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that flipping a toggle writes its pref, records the change and
 * re-renders the bar without reopening the menu.
 */
add_task(async function test_more_actions_toggle_updates_bar() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await openMoreActions();

    const copyButton = document.getElementById("copy-action-button");
    const aiButton = document.getElementById("ai-action-button");
    Assert.ok(!copyButton.hidden, "Copy starts shown");

    await clickToggle("more-actions-copy-toggle");
    Assert.ok(
      !Services.prefs.getBoolPref("browser.highlightToSearch.copy.enabled"),
      "Copy pref is written"
    );
    Assert.ok(copyButton.hidden, "Copy leaves the bar immediately");
    Assert.equal(panel.state, "open", "The bar stays open");

    await clickToggle("more-actions-ai-toggle");
    Assert.ok(
      !Services.prefs.getBoolPref("browser.ml.chat.shortcuts"),
      "AI pref is written"
    );
    Assert.ok(aiButton.hidden, "AI leaves the bar immediately");
    Assert.ok(
      !document.getElementById("more-actions-button").hidden,
      "The three-dot button stays so the actions can be turned back on"
    );

    await clickToggle("more-actions-copy-toggle");
    Assert.ok(
      Services.prefs.getBoolPref("browser.highlightToSearch.copy.enabled"),
      "Copy pref is written back on"
    );
    Assert.ok(!copyButton.hidden, "Copy returns to the bar immediately");

    const toggled = Glean.selectionMenu.optionToggled.testGetValue();
    Assert.deepEqual(
      toggled.map(e => [e.extra.action, e.extra.enabled, e.extra.source]),
      [
        ["copy", "false", "menu"],
        ["ai", "false", "menu"],
        ["copy", "true", "menu"],
      ],
      "Each toggle is recorded with the menu as its source"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that turning off the last action in the three-dot menu explains that
 * the menu will go away, and turns it off only once the three-dot menu closes,
 * as unchecking the last action in Settings does. Turning an action back on
 * first cancels it.
 */
add_task(async function test_more_actions_uncheck_all_turns_menu_off() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const moreActionsPanel = await openMoreActions();
    const notice = document.getElementById("more-actions-all-off-notice");
    const enabledPref = "browser.highlightToSearch.enabled";

    await clickToggle("more-actions-copy-toggle");
    await clickToggle("more-actions-search-toggle");
    Assert.equal(panel.state, "open", "The bar stays open with an action left");
    Assert.ok(
      Services.prefs.getBoolPref(enabledPref),
      "The menu stays on with an action left"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(notice),
      "No notice with an action left"
    );

    const menuWidth = moreActionsPanel.getBoundingClientRect().width;
    await clickToggle("more-actions-ai-toggle");
    await document.l10n.translateElements([notice]);
    Assert.ok(BrowserTestUtils.isVisible(notice), "The notice is shown");
    Assert.ok(notice.textContent.trim(), "The notice says what will happen");
    Assert.less(
      Math.abs(moreActionsPanel.getBoundingClientRect().width - menuWidth),
      1,
      "Showing the notice does not widen the menu"
    );
    Assert.less(
      Math.abs(
        notice.getBoundingClientRect().width -
          document
            .getElementById("more-actions-copy-toggle")
            .getBoundingClientRect().width
      ),
      1,
      "The notice is as wide as the rows rather than widening the menu"
    );
    Assert.equal(
      notice.previousElementSibling.localName,
      "toolbarseparator",
      "The notice comes after the separator"
    );
    Assert.equal(
      notice.nextElementSibling.id,
      "more-actions-manage-settings",
      "The notice comes just before Manage settings"
    );
    const chevron = notice.querySelector(".more-actions-notice-chevron");
    Assert.ok(
      chevron && BrowserTestUtils.isVisible(chevron),
      "The path to the setting is separated by a chevron icon"
    );
    Assert.ok(
      !notice.textContent.includes(">"),
      "No greater-than sign is used as a separator"
    );
    Assert.equal(notice.getAttribute("role"), "status", "It is announced");
    Assert.ok(Services.prefs.getBoolPref(enabledPref), "The menu is still on");
    Assert.equal(panel.state, "open", "The bar is still open");
    for (const id of [
      "ai-action-button",
      "search-action-button",
      "copy-action-button",
      "more-actions-separator",
    ]) {
      Assert.ok(document.getElementById(id).hidden, `${id} is hidden`);
    }
    Assert.ok(
      BrowserTestUtils.isVisible(
        document.getElementById("more-actions-button")
      ),
      "Only the three-dot button is left"
    );

    await clickToggle("more-actions-search-toggle");
    Assert.ok(BrowserTestUtils.isHidden(notice), "Turning one on removes it");
    let menuHidden = BrowserTestUtils.waitForEvent(
      moreActionsPanel,
      "popuphidden"
    );
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await menuHidden;
    Assert.ok(
      Services.prefs.getBoolPref(enabledPref),
      "Closing the three-dot menu with an action on keeps the menu on"
    );
    Assert.equal(panel.state, "open", "The bar stays open");

    await openMoreActions();
    await clickToggle("more-actions-search-toggle");
    Assert.ok(BrowserTestUtils.isVisible(notice), "The notice is back");
    menuHidden = BrowserTestUtils.waitForEvent(moreActionsPanel, "popuphidden");
    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await menuHidden;
    await panelHidden;
    Assert.ok(
      !Services.prefs.getBoolPref(enabledPref),
      "Closing the three-dot menu with nothing left turns the menu off"
    );

    const toggled = Glean.selectionMenu.optionToggled.testGetValue();
    Assert.deepEqual(
      toggled.map(e => [e.extra.action, e.extra.enabled, e.extra.source]),
      [
        ["copy", "false", "menu"],
        ["search", "false", "menu"],
        ["ai", "false", "menu"],
        ["search", "true", "menu"],
        ["search", "false", "menu"],
        ["master", "false", "uncheck_all"],
      ],
      "The menu turning itself off is recorded once, when it happens"
    );
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the bar closes when the chatbot shortcuts pref is turned off from
 * outside the three-dot menu while that menu is open.
 */
add_task(async function test_more_actions_bar_closes_on_external_ai_off() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await openMoreActions();

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    Services.prefs.setBoolPref("browser.ml.chat.shortcuts", false);
    await panelHidden;
    Assert.equal(panel.state, "closed", "The bar closes");
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a selection made while the bar is still open starts without the
 * previous copy confirmation or its pending hide.
 */
add_task(async function test_copy_confirmation_reset_on_new_selection() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  const pendingHides = new Set();
  const { setTimeout: realSetTimeout, clearTimeout: realClearTimeout } = window;
  window.setTimeout = (callback, delay, ...args) => {
    const id = realSetTimeout(callback, delay, ...args);
    if (delay == 850) {
      pendingHides.add(id);
    }
    return id;
  };
  window.clearTimeout = id => {
    pendingHides.delete(id);
    realClearTimeout(id);
  };

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const confirmation = document.getElementById("copy-action-confirmation");

      EventUtils.synthesizeMouseAtCenter(
        document.getElementById("copy-action-button"),
        {},
        window
      );
      await TestUtils.waitForCondition(
        () => confirmation.hasAttribute("copied"),
        "Confirmation is shown after copying"
      );
      Assert.equal(pendingHides.size, 1, "The confirmation scheduled a hide");

      GenAI.handleShortcutsMessage(
        "GenAI:ShowShortcuts",
        {
          selection: "hi",
          delay: 0,
          screenXDevPx: 10,
          screenYDevPx: 10,
        },
        browser
      );
      Assert.ok(
        !confirmation.hasAttribute("copied"),
        "Confirmation is cleared by the new selection"
      );
      Assert.equal(pendingHides.size, 0, "The pending hide is cancelled");

      const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      panel.hide();
      await panelHidden;
    });
  } finally {
    window.setTimeout = realSetTimeout;
    window.clearTimeout = realClearTimeout;
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that a selection made while the Copied text is still being translated
 * doesn't bring the previous confirmation back.
 */
add_task(async function test_copy_confirmation_reset_during_translation() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.copy.enabled", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const confirmation = document.getElementById("copy-action-confirmation");
    const confirmationText = confirmation.querySelector(
      ".copy-action-confirmation-text"
    );

    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("copy-action-button"),
      {},
      window
    );
    GenAI.handleShortcutsMessage(
      "GenAI:ShowShortcuts",
      {
        selection: "hi",
        delay: 0,
        screenXDevPx: 10,
        screenYDevPx: 10,
      },
      browser
    );

    await document.l10n.translateElements([]);
    await new Promise(resolve =>
      requestAnimationFrame(() => requestAnimationFrame(resolve))
    );
    Assert.ok(
      !confirmation.hasAttribute("copied"),
      "The stale translation doesn't show the confirmation"
    );
    Assert.equal(
      confirmationText.textContent,
      "",
      "The stale translation leaves no text behind"
    );

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a Smart Window selection shows only the AI action, even after a
 * classic selection in the same window showed the other actions.
 */
add_task(async function test_smart_window_resets_classic_actions() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.ml.chat.shortcuts.smartwindow", true],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    Assert.ok(
      !document.getElementById("copy-action-button").hidden,
      "Classic selection shows Copy"
    );
    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;

    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    document.documentElement.setAttribute("ai-window", "");
    try {
      await showSelectionMenu(browser);
      for (const id of [
        "search-action-button",
        "copy-action-button",
        "more-actions-separator",
        "more-actions-button",
      ]) {
        Assert.ok(document.getElementById(id).hidden, `${id} is hidden`);
      }
      Assert.ok(
        !document.getElementById("ai-action-button").hidden,
        "The AI action is shown"
      );
    } finally {
      document.documentElement.removeAttribute("ai-window");
      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      panel.hide();
      await hidden;
    }
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the AI row is absent from the three-dot menu, not shown
 * unchecked, while the chatbot is blocked.
 */
add_task(async function test_more_actions_ai_row_absent_when_blocked() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, ["browser.ml.chat.enabled", false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await openMoreActions();

    Assert.ok(
      document.getElementById("more-actions-ai-toggle").hidden,
      "AI row is absent while the chatbot is blocked"
    );
    const shown = Glean.selectionMenu.menuShown.testGetValue();
    Assert.equal(shown[0].extra.ai, "absent", "The shown event says so");

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the three-dot menu closes with the bar.
 */
add_task(async function test_more_actions_closes_with_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const moreActionsPanel = await openMoreActions();

    const hidden = BrowserTestUtils.waitForEvent(
      moreActionsPanel,
      "popuphidden"
    );
    panel.hide();
    await hidden;
    Assert.equal(moreActionsPanel.state, "closed", "Three-dot menu closed");
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the copy confirmation keeps readable text on its fill in high
 * contrast mode.
 */
add_task(async function test_copy_confirmation_in_high_contrast() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", true],
      ["browser.highlightToSearch.enabled", true],
      ["browser.highlightToSearch.copy.enabled", true],
      ["ui.useAccessibilityTheme", 1],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const confirmation = document.getElementById("copy-action-confirmation");

    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("copy-action-button"),
      {},
      window
    );
    await TestUtils.waitForCondition(() => confirmation.hasAttribute("copied"));

    const style = getComputedStyle(confirmation);
    Assert.notEqual(
      style.color,
      style.backgroundColor,
      "Confirmation text differs from its background in high contrast"
    );

    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that shortcuts get shown on selection and open popup and sidebar
 */
add_task(async function test_show_shortcuts() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 0],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllAndMouseUp(browser);

    await TestUtils.waitForCondition(() => {
      const panelElement = document.getElementById(
        "selection-shortcut-action-panel"
      );
      return panelElement.getAttribute("panelopen") === "true";
    });

    const shortcuts = document.querySelector("#ai-action-button");
    Assert.ok(shortcuts, "Shortcuts added on select");
    let events = Glean.genaiChatbot.shortcutsDisplayed.testGetValue();
    Assert.equal(events.length, 1, "Shortcuts shown once");
    Assert.ok(events[0].extra.delay, "Waited some time");
    Assert.equal(events[0].extra.inputType, "", "Not in input");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");

    const popup = document.getElementById("chat-shortcuts-options-panel");
    Assert.equal(popup.state, "closed", "Popup is closed");

    Assert.equal(
      Services.prefs.getIntPref(
        "browser.ml.chat.shortcut.onboardingMouseoverCount"
      ),
      0,
      "Pref should start at 0"
    );

    EventUtils.sendMouseEvent({ type: "mouseover" }, shortcuts);
    await BrowserTestUtils.waitForEvent(popup, "popupshown");
    Assert.equal(
      Services.prefs.getIntPref(
        "browser.ml.chat.shortcut.onboardingMouseoverCount"
      ),
      1,
      "Pref should be 1 after mouseover"
    );
    Assert.equal(
      popup.state,
      "open",
      "Popup is open with mouseover the first time"
    );
    const hidePopup = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    popup.hidePopup();
    await hidePopup;

    EventUtils.sendMouseEvent({ type: "mouseover" }, shortcuts);
    await BrowserTestUtils.waitForEvent(popup, "popupshown");
    Assert.equal(
      Services.prefs.getIntPref(
        "browser.ml.chat.shortcut.onboardingMouseoverCount"
      ),
      2,
      "Pref should be 2 after second mouseover"
    );

    Assert.equal(
      popup.state,
      "open",
      "Popup is open with mouseover the second time"
    );
    const hidePopupSecondTime = BrowserTestUtils.waitForEvent(
      popup,
      "popuphidden"
    );
    popup.hidePopup();
    await hidePopupSecondTime;

    EventUtils.sendMouseEvent({ type: "mouseover" }, shortcuts);
    Assert.equal(
      Services.prefs.getIntPref(
        "browser.ml.chat.shortcut.onboardingMouseoverCount"
      ),
      2,
      "Pref should still be 2 after third mouseover"
    );
    Assert.equal(
      popup.state,
      "closed",
      "Popup doesn't open with mouseover after the second time"
    );

    let beforeClick = Glean.genaiChatbot.shortcutsExpanded.testGetValue();
    shortcuts.click();
    await BrowserTestUtils.waitForEvent(popup, "popupshown");
    Assert.equal(popup.state, "open", "Popup open with click");

    let afterClick = Glean.genaiChatbot.shortcutsExpanded.testGetValue();
    Assert.equal(
      afterClick.length,
      beforeClick.length + 1,
      "One shortcuts opened"
    );
    const lastEvent = afterClick[afterClick.length - 1];
    Assert.equal(lastEvent.extra.selection, 2, "Selected hi");
    Assert.equal(
      lastEvent.extra.warning,
      "false",
      "Warning lable value is correct"
    );

    const custom = popup.querySelector("textarea");
    Assert.ok(custom, "Got custom prompt entry");
    Assert.ok(custom.style.height, "Dynamic height for custom");

    Assert.ok(!SidebarController.isOpen, "Sidebar is closed");
    popup.querySelector("toolbarbutton").click();
    const isOpen = await TestUtils.waitForCondition(
      () => SidebarController.isOpen
    );

    Assert.ok(isOpen, "Chat opened sidebar");
    events = Glean.genaiChatbot.shortcutsPromptClick.testGetValue();
    Assert.equal(events.length, 1, "One shortcut clicked");
    Assert.equal(events[0].extra.prompt, "summarize", "Picked summarize");
    Assert.equal(events[0].extra.provider, "localhost", "With localhost");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");

    events = Glean.genaiChatbot.promptClick.testGetValue();
    Assert.equal(events.length, 1, "One shortcut clicked");
    Assert.equal(events[0].extra.content_type, "selection", "Using selection");
    Assert.equal(events[0].extra.prompt, "summarize", "Picked summarize");
    Assert.equal(events[0].extra.provider, "localhost", "With localhost");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");
    Assert.equal(events[0].extra.smart_window, "false", "Not smart window");
    Assert.equal(events[0].extra.source, "shortcuts", "From shortcuts menu");

    SidebarController.hide();
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that shortcuts shown on second tab
 */
add_task(async function test_show_shortcuts_second_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });
  await BrowserTestUtils.withNewTab(
    "data:text/html,<title>second</title>page",
    async browser => {
      await selectAllAndMouseUp(browser);

      await TestUtils.waitForCondition(() => {
        const panelElement = document.getElementById(
          "selection-shortcut-action-panel"
        );
        return panelElement.getAttribute("panelopen") === "true";
      });
      const sandbox = sinon.createSandbox();
      const stub = sandbox.stub(GenAI, "addAskChatItems");

      const shortcuts = document.querySelector("#ai-action-button");
      shortcuts.click();

      Assert.equal(stub.callCount, 1, "Shortcuts added on select");
      Assert.equal(stub.firstCall.args[0], browser, "Got correct browser");

      sandbox.restore();
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the warning label is shown when too much text is selected
 */
add_task(async function test_show_warning_label() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });
  await BrowserTestUtils.withNewTab(
    "data:text/plain,hi".repeat(1000),
    async browser => {
      await selectAllAndMouseUp(browser);

      const selectionShortcutActionPanel = document.getElementById(
        "selection-shortcut-action-panel"
      );

      // Wait for shortcut actions panel to open
      await BrowserTestUtils.waitForEvent(
        selectionShortcutActionPanel,
        "popupshown"
      );

      Assert.ok(selectionShortcutActionPanel, "Shortcuts panel shown");
      const aiActionButton =
        selectionShortcutActionPanel.querySelector("#ai-action-button");

      let events = Glean.genaiChatbot.shortcutsDisplayed.testGetValue();
      Assert.equal(events.length, 1, "Shortcuts shown once");
      Assert.ok(events[0].extra.delay, "Waited some time");
      Assert.equal(events[0].extra.inputType, "", "Not in input");
      Assert.greater(
        Number(events[0].extra.selection),
        8192,
        "Selected enough text"
      );

      // Click button
      aiActionButton.click();

      const chatShortcutsOptionsPanel = document.getElementById(
        "chat-shortcuts-options-panel"
      );

      // Wait for wait for shortcut options panel to open
      await BrowserTestUtils.waitForEvent(
        chatShortcutsOptionsPanel,
        "popupshown"
      );

      Assert.ok(
        chatShortcutsOptionsPanel.querySelector(".ask-chat-shortcut-warning"),
        "Warning label shown"
      );

      events = Glean.genaiChatbot.shortcutsExpanded.testGetValue();
      Assert.equal(
        events[0].extra.warning,
        "true",
        "Warning lable value is correct"
      );

      Assert.ok(
        !aiActionButton.getAttribute("type").includes("ghost"),
        "button is active"
      );

      const hidden = BrowserTestUtils.waitForEvent(
        chatShortcutsOptionsPanel,
        "popuphidden"
      );
      chatShortcutsOptionsPanel.hidePopup();
      await hidden;

      Assert.ok(
        aiActionButton.getAttribute("type").includes("ghost"),
        "button is inactive"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that only plain clicks would show shortcuts
 */
add_task(async function test_plain_clicks() {
  // Set longpress to 0 to prevent delay condition from failing
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.longPress", 0]],
  });

  const sandbox = sinon.createSandbox();
  const stub = sandbox
    .stub(GenAI, "handleShortcutsMessage")
    .withArgs("GenAI:ShowShortcuts");

  await BrowserTestUtils.withNewTab("data:text/plain,click", async browser => {
    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { type: "mouseup" },
      browser
    );

    await TestUtils.waitForCondition(() => stub.callCount !== 0);

    Assert.equal(stub.callCount, 1, "Plain click handled");

    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { button: 1, type: "mouseup" },
      browser
    );

    Assert.equal(stub.callCount, 1, "Middle click ignored");

    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { shiftKey: true, type: "mouseup" },
      browser
    );

    Assert.equal(stub.callCount, 1, "Modified click ignored");
  });

  await SpecialPowers.popPrefEnv();

  sandbox.restore();
});

/**
 * Check that input selection can show shortcuts
 */
add_task(async function test_input_selection() {
  Assert.equal(GenAI.ignoredInputs.size, 2, "Default ignore 2 types of fields");
  Assert.ok(GenAI.ignoredInputs.has("input"), "Default ignore inputs");
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.ml.chat.shortcuts.ignoreFields",
        "contenteditable,textarea,moz-multiline-editor",
      ],
    ],
  });
  Assert.equal(GenAI.ignoredInputs.size, 3, "Ignoring other fields not input");
  Assert.ok(GenAI.ignoredInputs.has("textarea"), "Now ignore textarea");
  Assert.ok(!GenAI.ignoredInputs.has("input"), "Not ignoring input for test");

  const sandbox = sinon.createSandbox();
  const stub = sandbox.stub(GenAI, "handleShortcutsMessage");

  await BrowserTestUtils.withNewTab(
    `data:text/html,<input value="input"/>`,
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        const input = content.document.querySelector("input");
        input.focus();
        input.selectionEnd = 3;
      });

      await BrowserTestUtils.synthesizeMouse(
        browser,
        60,
        10,
        { type: "mouseup" },
        browser
      );
      const showCalls = () =>
        stub.getCalls().filter(c => c.args[0] == "GenAI:ShowShortcuts");
      await TestUtils.waitForCondition(() => showCalls().length);

      Assert.equal(showCalls().length, 1, "Show shortcuts once");
      Assert.equal(
        showCalls()[0].args[1].selection,
        "inp",
        "Got selected text from input"
      );
      Assert.equal(showCalls()[0].args[1].inputType, "input", "Got input type");
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a selection the page makes while the mouse is clicked doesn't
 * show shortcuts, but one that comes with a drag does.
 */
add_task(async function test_page_made_selection() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.debounce", 0]],
  });
  const sandbox = sinon.createSandbox();
  const stub = sandbox.stub(GenAI, "handleShortcutsMessage");
  const showCalls = () =>
    stub.getCalls().filter(c => c.args[0] == "GenAI:ShowShortcuts");

  await BrowserTestUtils.withNewTab(
    `data:text/html,<p id="text">select this text</p><div style="height:3000px"></div>`,
    async browser => {
      const select = (start, end) =>
        SpecialPowers.spawn(browser, [start, end], (from, to) => {
          const node = content.document.getElementById("text").firstChild;
          content.getSelection().setBaseAndExtent(node, from, node, to);
        });
      const press = x =>
        BrowserTestUtils.synthesizeMouse(
          browser,
          x,
          10,
          { type: "mousedown" },
          browser
        );
      const release = x =>
        BrowserTestUtils.synthesizeMouse(
          browser,
          x,
          10,
          { type: "mouseup" },
          browser
        );

      await SimpleTest.promiseFocus(browser);

      // The page selects the whole line while the mouse is clicked in place.
      await press(10);
      await select(0, 16);
      await release(10);
      // Let the actor's zero-length debounce run, so the next gesture can't
      // cancel a message that would have been sent.
      await SpecialPowers.spawn(
        browser,
        [],
        () => new Promise(resolve => content.setTimeout(resolve, 100))
      );
      Assert.equal(
        showCalls().length,
        0,
        "A click the page answered is ignored"
      );

      // The user then drags across a single word.
      await press(10);
      await select(7, 11);
      await release(60);
      await TestUtils.waitForCondition(() => showCalls().length);

      Assert.equal(
        showCalls()[0].args[1].selection,
        "this",
        "The first shortcuts were for the dragged selection, not the page's"
      );

      // The user holds the button and scrolls to extend the selection.
      await press(10);
      await SpecialPowers.spawn(browser, [], () => content.scrollBy(0, 50));
      await select(0, 4);
      await release(10);
      await TestUtils.waitForCondition(() => showCalls().length == 2);

      Assert.equal(
        showCalls()[1].args[1].selection,
        "sele",
        "Scrolling while the button is held counts as the user selecting"
      );
    }
  );

  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_panel_actions_layout() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await selectAllAndMouseUp(browser);

    const panel = document.getElementById("selection-shortcut-action-panel");
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Panel should open after text selection"
    );

    const buttons = [...panel.querySelectorAll("moz-button")];
    Assert.deepEqual(
      buttons.map(button => button.id),
      [
        "ai-action-button",
        "search-action-button",
        "copy-action-button",
        "more-actions-button",
      ],
      "Panel holds the four controls in the correct order"
    );

    Assert.ok(!buttons[0].hidden, "AI action is visible");
    for (const button of buttons.slice(1)) {
      Assert.ok(button.hidden, `${button.id} is hidden`);
      Assert.equal(
        button.getBoundingClientRect().width,
        0,
        `${button.id} takes up no space while hidden`
      );
    }

    // panel-subview-body stacks its children, so check the actions really do
    // sit in a row.
    for (const button of buttons) {
      button.hidden = false;
      await button.updateComplete;
    }
    const rects = buttons.map(button => button.getBoundingClientRect());
    for (let i = 1; i < rects.length; i++) {
      Assert.equal(
        rects[i].top,
        rects[i - 1].top,
        `${buttons[i].id} shares a row with ${buttons[i - 1].id}`
      );
      Assert.greater(
        rects[i].left,
        rects[i - 1].left,
        `${buttons[i].id} follows ${buttons[i - 1].id} along the row`
      );
    }
    for (const button of buttons.slice(1)) {
      button.hidden = true;
    }

    await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });
    panel.updateActions(panel.selectionData.selection, browser);

    // moz-button maps the host aria-label and title onto the inner button, so
    // check the element that actually takes focus rather than the custom
    // element.
    await document.l10n.translateFragment(panel);
    for (const button of buttons) {
      await button.updateComplete;
      const ariaLabel = button.buttonEl.getAttribute("aria-label") ?? "";
      Assert.stringMatches(
        ariaLabel,
        /\S/,
        `${button.id} has an accessible name`
      );
      Assert.equal(
        button.buttonEl.getAttribute("title"),
        ariaLabel,
        `${button.id} shows the same text as a tooltip`
      );
      Assert.ok(button.iconSrc, `${button.id} has an icon`);
    }

    Assert.equal(
      buttons[0].buttonEl.getAttribute("aria-label"),
      "Ask about this text",
      "AI action uses the provider-agnostic label"
    );
    Assert.stringMatches(
      buttons[1].buttonEl.getAttribute("aria-label"),
      /^Search .+ for “hi”$/,
      "Search action names the engine and the selection"
    );
    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_panel_opens_without_search_service() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  const panel = document.getElementById("selection-shortcut-action-panel");
  panel.hidePopup();
  await TestUtils.waitForCondition(
    () => !panel.hasAttribute("panelopen"),
    "Panel left over from an earlier task should be closed"
  );

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await SimpleTest.promiseFocus(browser);

    // "failed" rather than the other statuses because it is the one that makes
    // the default engine getters throw.
    SearchService.forceInitializationStatusForTests("failed");
    try {
      await selectAllAndMouseUp(browser);
      await TestUtils.waitForCondition(
        () => panel.getAttribute("panelopen") === "true"
      );
      Assert.ok(
        panel.hasAttribute("panelopen"),
        "Panel still opens when the search service failed to initialize"
      );
    } finally {
      SearchService.forceInitializationStatusForTests("success");
    }
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that IME composition hides the shortcuts panel visually
 */
add_task(async function test_ime_composition_hides_panel() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hello", async browser => {
    await selectAllAndMouseUp(browser);

    const panel = document.getElementById("selection-shortcut-action-panel");
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Panel should open after text selection"
    );

    Assert.ok(!panel.hasAttribute("ime-hiding"), "No ime-hiding before IME");

    // Simulate IME composition - compositionstart sets #compositionActive, then
    // selectionchange triggers a CSS-only hide instead of hidePopup().
    await SpecialPowers.spawn(browser, [], () => {
      content.document.dispatchEvent(
        new content.CompositionEvent("compositionstart", {
          bubbles: true,
          data: "",
        })
      );
      content.document.dispatchEvent(
        new content.Event("selectionchange", { bubbles: true })
      );
    });

    await TestUtils.waitForCondition(
      () => panel.hasAttribute("ime-hiding"),
      "Panel should have ime-hiding attribute during IME composition"
    );
    Assert.equal(
      panel.getAttribute("panelopen"),
      "true",
      "hidePopup was not called during IME composition"
    );
  });

  await SpecialPowers.popPrefEnv();
});

const DEFAULT_IGNORE_FIELDS = "input,moz-multiline-editor";

/**
 * Check that textareas are ignored only while the Highlight to Search feature
 * gate is on, so existing chatbot users keep the menu there until then.
 */
add_task(async function test_textarea_ignored_with_feature_gate() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.ignoreFields", DEFAULT_IGNORE_FIELDS]],
  });
  Assert.ok(
    !GenAI.ignoredInputs.has("textarea"),
    "Textareas are not ignored while the feature gate is off"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.highlightToSearch.featureGate", true]],
  });
  Assert.ok(
    GenAI.ignoredInputs.has("textarea"),
    "Textareas are ignored while the feature gate is on"
  );
  Assert.ok(GenAI.ignoredInputs.has("input"), "Inputs are still ignored");

  await SpecialPowers.popPrefEnv();
  Assert.ok(
    !GenAI.ignoredInputs.has("textarea"),
    "Textareas are shown again once the feature gate is off"
  );
  await SpecialPowers.popPrefEnv();
});

async function selectTextareaAndMouseUp(browser) {
  await SimpleTest.promiseFocus(browser);
  await BrowserTestUtils.synthesizeMouse(
    browser,
    10,
    10,
    { type: "mousedown" },
    browser
  );
  await SpecialPowers.spawn(browser, [], () => {
    const textarea = content.document.querySelector("textarea");
    textarea.focus();
    textarea.select();
  });
  await BrowserTestUtils.synthesizeMouse(
    browser,
    60,
    10,
    { type: "mouseup" },
    browser
  );
}

/**
 * Check that selecting text in a textarea shows no menu while the feature gate
 * is on, and still does while it is off.
 */
add_task(async function test_textarea_selection_menu() {
  const panel = document.getElementById("selection-shortcut-action-panel");
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");

  for (const featureGate of [true, false]) {
    spy.resetHistory();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", featureGate],
        ["browser.ml.chat.shortcuts.ignoreFields", DEFAULT_IGNORE_FIELDS],
        ["browser.ml.chat.shortcuts", true],
        ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
        ["browser.ml.chat.provider", "http://localhost:8080"],
      ],
    });

    await BrowserTestUtils.withNewTab(
      "data:text/html,<textarea>some text</textarea>",
      async browser => {
        await selectTextareaAndMouseUp(browser);
        await TestUtils.waitForCondition(
          () => spy.calledWith("GenAI:ShowShortcuts"),
          "Actor offered the textarea selection to the parent"
        );

        if (featureGate) {
          Assert.equal(
            panel.state,
            "closed",
            "No menu for a textarea while the feature gate is on"
          );
        } else {
          await TestUtils.waitForCondition(
            () => panel.getAttribute("panelopen") === "true",
            "Menu opens for a textarea while the feature gate is off"
          );
          const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
          panel.hide();
          await hidden;
        }
      }
    );

    await SpecialPowers.popPrefEnv();
  }

  spy.restore();
});

/**
 * Check that the three-dot menu lines up with the bar it opens from, names the
 * chatbot row generically, and ends with a Manage settings row.
 */
add_task(async function test_more_actions_menu_layout() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, ["browser.ml.chat.provider", ""]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const openSpy = sinon.spy(
      document.getElementById("selection-more-actions-panel"),
      "openPopup"
    );
    const moreActionsPanel = await openMoreActions();

    const aiToggle = document.getElementById("more-actions-ai-toggle");
    Assert.equal(aiToggle.label, "Ask AI", "Chatbot row has a generic name");

    Assert.ok(
      openSpy.calledWith(
        panel.shadowRoot.querySelector("[part=content]"),
        "after_start"
      ),
      "Menu opens from the start edge of the bar"
    );
    openSpy.restore();

    const toggles = [
      aiToggle,
      document.getElementById("more-actions-search-toggle"),
      document.getElementById("more-actions-copy-toggle"),
    ];
    const manageSettings = document.getElementById(
      "more-actions-manage-settings"
    );
    await manageSettings.updateComplete;

    const rowInset = parseFloat(getComputedStyle(aiToggle).paddingInlineEnd);
    Assert.greater(rowInset, 0, "Rows are inset from the menu edge");
    const menuFontProbe = document.createElement("div");
    menuFontProbe.style.font = "menu";
    moreActionsPanel.append(menuFontProbe);
    const menuFont = getComputedStyle(menuFontProbe);
    for (const el of [
      aiToggle,
      manageSettings,
      manageSettings.shadowRoot.querySelector("button"),
      manageSettings.shadowRoot.querySelector(".text"),
    ]) {
      Assert.deepEqual(
        [
          getComputedStyle(el).fontSize,
          getComputedStyle(el).fontFamily,
          getComputedStyle(el).fontWeight,
        ],
        [menuFont.fontSize, menuFont.fontFamily, menuFont.fontWeight],
        `${el.localName} uses the menu font`
      );
    }
    menuFontProbe.remove();
    for (const toggle of toggles) {
      Assert.less(
        Math.abs(
          toggle.getBoundingClientRect().right -
            rowInset -
            toggle.buttonEl.getBoundingClientRect().right
        ),
        2,
        "Toggles sit at the end of their rows"
      );
    }

    const manageRect = manageSettings.getBoundingClientRect();
    for (const toggle of toggles) {
      Assert.greaterOrEqual(
        toggle.getBoundingClientRect().height,
        manageRect.height,
        "A toggle row is as tall as the Manage settings row"
      );
    }
    for (let i = 1; i < toggles.length; i++) {
      Assert.less(
        Math.abs(
          toggles[i].getBoundingClientRect().top -
            toggles[i - 1].getBoundingClientRect().bottom
        ),
        1,
        "Toggle rows are stacked with no gap"
      );
    }
    const iconLeft = el =>
      el.shadowRoot.querySelector("img").getBoundingClientRect().left;
    for (const toggle of toggles) {
      await toggle.updateComplete;
      Assert.less(
        Math.abs(iconLeft(toggle) - iconLeft(manageSettings)),
        2,
        "Row icons line up with the Manage settings icon"
      );
    }
    Assert.less(
      Math.abs(manageRect.width - toggles[0].getBoundingClientRect().width),
      1,
      "Manage settings spans the width of the toggle rows"
    );

    for (const row of [...toggles, manageSettings]) {
      const rowRect = row.getBoundingClientRect();
      const iconRect = row.shadowRoot
        .querySelector("img")
        .getBoundingClientRect();
      const textRect = row.shadowRoot
        .querySelector(".text")
        .getBoundingClientRect();
      Assert.less(
        Math.abs(
          iconRect.left - rowRect.left + iconRect.width / 2 - rowRect.height / 2
        ),
        0.5,
        `${row.id} icon is centred in the circle at the start of the row`
      );
      Assert.less(
        Math.abs(textRect.left - rowRect.left - rowRect.height),
        0.5,
        `${row.id} text starts at the edge of that circle`
      );
    }
    for (const toggle of toggles) {
      const rowRect = toggle.getBoundingClientRect();
      const switchRect = toggle.buttonEl.getBoundingClientRect();
      Assert.less(
        Math.abs(
          rowRect.right - switchRect.right - (switchRect.top - rowRect.top)
        ),
        0.5,
        `${toggle.id} switch is as far from the end of the row as from its top`
      );
    }
    const switchSpace =
      toggles[0].buttonEl.getBoundingClientRect().width +
      toggles[0].getBoundingClientRect().right -
      toggles[0].buttonEl.getBoundingClientRect().right;
    Assert.greaterOrEqual(
      manageRect.right -
        manageSettings.shadowRoot.querySelector(".text").getBoundingClientRect()
          .right,
      switchSpace,
      "Manage settings leaves room after its text for a switch"
    );

    const menuContent = contentRect(moreActionsPanel);
    const barBox = visibleBox(panel);
    const menuBox = visibleBox(moreActionsPanel);
    Assert.less(
      Math.abs(menuBox.top - barBox.bottom - 2),
      1,
      "The menu opens just below the bar"
    );
    Assert.less(
      Math.abs(menuBox.left - barBox.left),
      1,
      "The menu lines up with the start of the bar"
    );
    Assert.lessOrEqual(
      menuContent.height -
        (manageRect.bottom - toggles[0].getBoundingClientRect().top),
      20,
      "Little padding sits above the first row and below the last"
    );
    Assert.lessOrEqual(
      menuContent.width - toggles[0].getBoundingClientRect().width,
      20,
      "Little padding sits beside the rows"
    );

    const copyToggle = toggles[2];
    EventUtils.synthesizeMouseAtCenter(
      copyToggle,
      { type: "mousemove" },
      window
    );
    await TestUtils.waitForCondition(
      () => getComputedStyle(copyToggle).backgroundColor != "rgba(0, 0, 0, 0)",
      "A toggle row is highlighted on hover"
    );
    const copyPref = "browser.highlightToSearch.copy.enabled";
    await copyToggle.updateComplete;
    const labelRect = copyToggle.labelEl.getBoundingClientRect();
    const switchRect = copyToggle.buttonEl.getBoundingClientRect();
    const rowRect = copyToggle.getBoundingClientRect();
    const gapStart = Math.min(labelRect.right, switchRect.right);
    const gapEnd = Math.max(labelRect.left, switchRect.left);
    const gapTop = Math.max(labelRect.top, switchRect.top);
    const gapBottom = Math.min(labelRect.bottom, switchRect.bottom);
    Assert.greater(gapEnd, gapStart, "The label and switch have a gap");
    Assert.greater(
      gapBottom,
      gapTop,
      "The label and switch overlap vertically"
    );
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    EventUtils.synthesizeMouse(
      copyToggle,
      (gapStart + gapEnd) / 2 - rowRect.left,
      (gapTop + gapBottom) / 2 - rowRect.top,
      {},
      window
    );
    AccessibilityUtils.resetEnv();
    await TestUtils.waitForCondition(
      () => !Services.prefs.getBoolPref(copyPref),
      "Clicking between the label and switch toggles the row"
    );
    await copyToggle.updateComplete;
    EventUtils.synthesizeMouseAtCenter(copyToggle.buttonEl, {}, window);
    await TestUtils.waitForCondition(
      () => Services.prefs.getBoolPref(copyPref),
      "Clicking the switch restores the preference"
    );
    await copyToggle.updateComplete;
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    EventUtils.synthesizeMouse(copyToggle, 2, 10, {}, window);
    AccessibilityUtils.resetEnv();
    await TestUtils.waitForCondition(
      () => !Services.prefs.getBoolPref(copyPref),
      "Clicking the padding of a row toggles it"
    );
    await copyToggle.updateComplete;
    EventUtils.synthesizeMouseAtCenter(copyToggle.buttonEl, {}, window);
    await TestUtils.waitForCondition(
      () => Services.prefs.getBoolPref(copyPref),
      "Clicking the switch toggles the row once"
    );

    for (const [toggle, icon] of [
      [aiToggle, "highlights.svg"],
      [
        document.getElementById("more-actions-search-toggle"),
        "search-glass.svg",
      ],
      [document.getElementById("more-actions-copy-toggle"), "edit-copy.svg"],
    ]) {
      await toggle.updateComplete;
      Assert.ok(
        toggle.shadowRoot.querySelector("img.icon")?.src.endsWith(icon),
        `${icon} is shown beside its row`
      );
    }

    const manage = document.getElementById("more-actions-manage-settings");
    Assert.ok(BrowserTestUtils.isVisible(manage), "Manage settings is shown");
    Assert.ok(
      moreActionsPanel.querySelector("toolbarseparator"),
      "Manage settings is set apart from the toggles"
    );

    const separator = document.getElementById("more-actions-separator");
    const separatorRect = separator.getBoundingClientRect();
    Assert.deepEqual(
      [separatorRect.width, separatorRect.height],
      [1, 18],
      "A thin rule separates the actions from the three-dot button"
    );
    Assert.lessOrEqual(
      document.getElementById("copy-action-button").getBoundingClientRect()
        .right,
      separatorRect.left,
      "The rule comes after the last action"
    );
    Assert.lessOrEqual(
      separatorRect.right,
      document.getElementById("more-actions-button").getBoundingClientRect()
        .left,
      "The rule comes before the three-dot button"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await hidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the Ask AI menu opens just below the bar.
 */
add_task(async function test_ai_menu_opens_just_below_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const popup = await openAskDropdown(browser);

    Assert.less(
      Math.abs(visibleBox(popup).top - visibleBox(panel).bottom - 2),
      1,
      "The Ask AI menu opens just below the bar"
    );

    const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    panel.hide();
    await hidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Manage settings opens the Highlight to Search settings, closes
 * the menu and records the click.
 */
add_task(async function test_more_actions_manage_settings() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });
  const openPreferencesStub = sinon.stub(window, "openPreferences");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const moreActionsPanel = await openMoreActions();

      const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
      EventUtils.synthesizeMouseAtCenter(
        document.getElementById("more-actions-manage-settings"),
        {},
        window
      );
      await hidden;

      Assert.ok(
        openPreferencesStub.calledWith("general-highlight-to-search"),
        "Opened the Highlight to Search settings"
      );
      Assert.equal(moreActionsPanel.state, "closed", "Menu closed");

      const clicks = Glean.selectionMenu.menuClick.testGetValue();
      Assert.equal(clicks?.length, 1, "The click was recorded");
      Assert.equal(clicks[0].extra.item, "settings", "It names the entry");
    });
  } finally {
    openPreferencesStub.restore();
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check which sites are ignored: the defaults, their subdomains, and nothing
 * that merely ends with the same letters or has no host.
 */
add_task(async function test_ignored_sites_matching() {
  for (const spec of [
    "https://docs.google.com/document/d/1/edit",
    "https://example.atlassian.net/wiki/spaces/A",
    "https://atlassian.net/",
    "https://www.notion.so/a-page",
    "https://someone.notion.site/a-page",
  ]) {
    Assert.ok(
      GenAI.isIgnoredSite(Services.io.newURI(spec)),
      `${spec} is ignored`
    );
  }
  for (const spec of [
    "https://www.google.com/",
    "https://notatlassian.net/",
    "https://example.org/",
    "about:blank",
  ]) {
    Assert.ok(
      !GenAI.isIgnoredSite(Services.io.newURI(spec)),
      `${spec} is not ignored`
    );
  }
  Assert.ok(!GenAI.isIgnoredSite(null), "A missing URI is not ignored");
});

/**
 * Check that the menu is not shown for a selection on an ignored site or its
 * subdomains, judged by the frame the selection is in, but only while the
 * Highlight to Search feature gate is on.
 */
add_task(async function test_ignored_sites_menu() {
  const panel = document.getElementById("selection-shortcut-action-panel");
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.highlightToSearch.ignoredSites", "example.com"]],
  });

  for (const [url, frameUrl, featureGate, shouldShow, message] of [
    ["https://example.com/", null, true, false, "No menu on an ignored site"],
    ["https://www.example.com/", null, true, false, "No menu on its subdomain"],
    ["https://example.org/", null, true, true, "Menu shows on another site"],
    [
      "https://example.com/",
      null,
      false,
      true,
      "Menu shows on an ignored site while the feature gate is off",
    ],
    [
      "https://example.org/",
      "https://example.com/",
      true,
      false,
      "No menu for a selection in an ignored frame on another site",
    ],
    [
      "https://example.com/",
      "https://example.org/",
      true,
      true,
      "Menu shows for a selection in another frame on an ignored site",
    ],
  ]) {
    spy.resetHistory();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", featureGate],
        ["browser.ml.chat.shortcuts", true],
        ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
        ["browser.ml.chat.provider", "http://localhost:8080"],
      ],
    });

    await BrowserTestUtils.withNewTab(url, async browser => {
      if (frameUrl) {
        await SpecialPowers.spawn(browser, [frameUrl], async src => {
          const frame = content.document.createElement("iframe");
          frame.src = src;
          const loaded = ContentTaskUtils.waitForEvent(frame, "load");
          content.document.body.appendChild(frame);
          await loaded;
        });
        const frameContext = browser.browsingContext.children[0];
        await SimpleTest.promiseFocus(browser);
        await BrowserTestUtils.synthesizeMouse(
          "body",
          10,
          10,
          { type: "mousedown" },
          frameContext
        );
        await SpecialPowers.spawn(frameContext, [], () => {
          content.getSelection().selectAllChildren(content.document.body);
        });
        await BrowserTestUtils.synthesizeMouse(
          "body",
          60,
          10,
          { type: "mouseup" },
          frameContext
        );
      } else {
        await selectAllAndMouseUp(browser);
      }
      await TestUtils.waitForCondition(
        () => spy.calledWith("GenAI:ShowShortcuts"),
        "Actor offered the selection to the parent"
      );

      if (shouldShow) {
        await TestUtils.waitForCondition(
          () => panel.getAttribute("panelopen") === "true",
          message
        );
        const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
        panel.hide();
        await hidden;
      } else {
        Assert.equal(panel.state, "closed", message);
      }
    });

    await SpecialPowers.popPrefEnv();
  }

  Assert.deepEqual(
    Glean.selectionMenu.suppressed.testGetValue().map(e => e.extra.reason),
    ["site_list", "site_list", "site_list"],
    "Each suppressed selection is recorded with its reason"
  );

  spy.restore();
  await SpecialPowers.popPrefEnv();
});

const CALLOUT_SEEN_PREF = "browser.highlightToSearch.callout.seen";

function getCalloutPanel() {
  return document.getElementById("selection-callout-panel");
}

function getCalloutState() {
  return getCalloutPanel()?.state ?? "closed";
}

async function waitForCalloutOpen() {
  await TestUtils.waitForCondition(
    () => getCalloutState() == "open",
    "Selection callout opened"
  );
  return getCalloutPanel();
}

async function hideSelectionMenu(panel) {
  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hide();
  await hidden;
}

/**
 * Check that the feature callout opens beside the menu, keeps the menu open
 * and the AI submenu closed, and is only ever shown once.
 */
add_task(async function test_selection_callout_shown_once() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 0],
      [CALLOUT_SEEN_PREF, false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    let titleWhenShowing = null;
    const onShowing = event => {
      if (event.target.id == "selection-callout-panel") {
        titleWhenShowing = event.target
          .querySelector(".selection-callout-title")
          .textContent.trim();
      }
    };
    document.addEventListener("popupshowing", onShowing);
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();
    document.removeEventListener("popupshowing", onShowing);
    Assert.ok(titleWhenShowing, "Callout is translated before it opens");

    const title = callout.querySelector(".selection-callout-title");
    const text = callout.querySelector(".selection-callout-message");
    await document.l10n.translateElements([title, text]);
    Assert.ok(title.textContent.trim(), "Callout has a title");
    Assert.ok(text.textContent.trim(), "Callout explains the feature");
    Assert.equal(
      text.querySelector(".selection-callout-more-link")?.textContent,
      "•••",
      "The sentence names the three-dot button with a pressable link"
    );
    const badge = callout.querySelector("moz-badge");
    const mark = callout.querySelector(".selection-callout-mark");
    Assert.equal(badge?.getAttribute("type"), "new", "Callout has a New badge");
    const badgeRect = badge.getBoundingClientRect();
    const markRect = mark.getBoundingClientRect();
    Assert.greater(
      badgeRect.bottom,
      markRect.bottom,
      "The badge sits beneath the logo"
    );
    Assert.less(
      Math.abs(
        badgeRect.left +
          badgeRect.width / 2 -
          (markRect.left + markRect.width / 2)
      ),
      1.5,
      "The badge is centered under the logo"
    );
    Assert.ok(
      !callout.querySelector("#selection-callout-dismiss"),
      "Callout has no button to dismiss it"
    );
    Assert.ok(
      Services.prefs.getBoolPref(CALLOUT_SEEN_PREF),
      "Showing the callout counts as seeing it"
    );

    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("ai-action-button"),
      { type: "mouseover" },
      window
    );
    await TestUtils.waitForTick();
    Assert.equal(
      document.getElementById("chat-shortcuts-options-panel").state,
      "closed",
      "Hovering the AI action does not open its submenu over the callout"
    );
    Assert.equal(callout.state, "open", "Callout stays open");

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    await hideSelectionMenu(panel);
    await hidden;
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await TestUtils.waitForTick();
    Assert.equal(getCalloutState(), "closed", "Callout not shown again");
    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the caret of the callout outline points at the Search action, or
 * at the first action there is when Search is off, also with an RTL app locale.
 */
add_task(async function test_selection_callout_points_at_first_action() {
  for (const { name, prefs, targetId } of [
    { name: "Search", prefs: [], targetId: "search-action-button" },
    {
      name: "AI without Search",
      prefs: [["browser.highlightToSearch.search.enabled", false]],
      targetId: "ai-action-button",
    },
    {
      name: "Search in an RTL locale",
      prefs: [["intl.l10n.pseudo", "bidi"]],
      targetId: "search-action-button",
    },
  ]) {
    await SpecialPowers.pushPrefEnv({
      set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false], ...prefs],
    });
    if (prefs.length && prefs[0][0] == "intl.l10n.pseudo") {
      await TestUtils.waitForCondition(
        () => Services.locale.isAppLocaleRTL,
        "App locale is RTL"
      );
    }

    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser, { releaseX: 300 });
      const callout = await waitForCalloutOpen();

      const edge = callout.querySelector(".selection-callout-edge");
      await TestUtils.waitForCondition(
        () => edge.getAttribute("d"),
        "Callout outline was drawn"
      );
      const body = callout.querySelector(".panel-subview-body");
      const target = document.getElementById(targetId);
      const targetRect = target.getBoundingClientRect();
      const bodyRect = body.getBoundingClientRect();
      Assert.greater(
        bodyRect.top,
        targetRect.bottom,
        `${name}: Callout is below the action`
      );
      const targetCenter =
        targetRect.left + targetRect.width / 2 - bodyRect.left;
      const insideCaret = -6;
      Assert.ok(
        edge.isPointInFill({ x: targetCenter, y: insideCaret }),
        `${name}: Caret sits over the center of the action`
      );
      Assert.ok(
        !edge.isPointInFill({ x: targetCenter - 20, y: insideCaret }),
        `${name}: Caret does not extend left of the action`
      );
      Assert.ok(
        !edge.isPointInFill({ x: targetCenter + 20, y: insideCaret }),
        `${name}: Caret does not extend right of the action`
      );

      await hideSelectionMenu(panel);
    });

    await SpecialPowers.popPrefEnv();
    if (prefs.length && prefs[0][0] == "intl.l10n.pseudo") {
      await TestUtils.waitForCondition(
        () => !Services.locale.isAppLocaleRTL,
        "App locale is LTR again"
      );
    }
  }
});

/**
 * Check that the link in the callout's sentence lights the three-dot button it
 * names and opens its menu, which closes the callout.
 */
add_task(async function test_selection_callout_link_opens_more_actions() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();
    const button = document.getElementById("more-actions-button");
    const text = callout.querySelector(".selection-callout-message");
    await document.l10n.translateElements([text]);
    const link = callout.querySelector(".selection-callout-more-link");

    EventUtils.synthesizeMouseAtCenter(link, { type: "mousemove" }, window);
    await TestUtils.waitForCondition(
      () => button.hasAttribute("highlighted"),
      "Hovering the link lights the three-dot button"
    );
    EventUtils.synthesizeMouseAtCenter(
      callout.querySelector(".selection-callout-title"),
      { type: "mousemove" },
      window
    );
    await TestUtils.waitForCondition(
      () => !button.hasAttribute("highlighted"),
      "Leaving the link turns the light off"
    );

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    const shown = BrowserTestUtils.waitForEvent(
      document.getElementById("selection-more-actions-panel"),
      "popupshown"
    );
    EventUtils.synthesizeMouseAtCenter(link, {}, window);
    await shown;
    await hidden;
    Assert.ok(
      !button.hasAttribute("highlighted"),
      "The light is off once the callout is gone"
    );

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that opening the three-dot menu, which the callout names, closes the
 * callout.
 */
add_task(async function test_selection_callout_closes_for_more_actions() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    const shown = BrowserTestUtils.waitForEvent(
      document.getElementById("selection-more-actions-panel"),
      "popupshown"
    );
    document.getElementById("more-actions-button").click();
    await shown;
    await hidden;

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the callout closes with the menu and does not come back for the
 * next one.
 */
add_task(async function test_selection_callout_closes_with_menu() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    await hideSelectionMenu(panel);
    await hidden;
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await TestUtils.waitForTick();
    Assert.equal(getCalloutState(), "closed", "Callout does not come back");
    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the callout stays away when the menu has no Search or Copy action
 * to explain.
 */
add_task(async function test_selection_callout_needs_search_or_copy() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.highlightToSearch.search.enabled", false],
      ["browser.highlightToSearch.copy.enabled", false],
      [CALLOUT_SEEN_PREF, false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    await TestUtils.waitForTick();
    Assert.equal(getCalloutState(), "closed", "No callout for AI only");
    Assert.ok(
      !Services.prefs.getBoolPref(CALLOUT_SEEN_PREF),
      "Callout not seen"
    );
    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the callout stays away in Smart Window, which only offers the AI
 * action.
 */
add_task(async function test_selection_callout_not_in_smart_window() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.ml.chat.shortcuts.smartwindow", true],
      [CALLOUT_SEEN_PREF, false],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow();

  try {
    win.document.documentElement.setAttribute("ai-window", "");

    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "data:text/plain,hi"
    );
    const browser = win.gBrowser.selectedBrowser;
    await SimpleTest.promiseFocus(browser);

    const selectPromise = SpecialPowers.spawn(browser, [], () =>
      ContentTaskUtils.waitForCondition(() => content.getSelection().toString())
    );
    win.goDoCommand("cmd_selectAll");
    await selectPromise;
    await BrowserTestUtils.synthesizeMouse(
      browser,
      60,
      10,
      { type: "mouseup" },
      browser
    );

    const panel = win.document.getElementById(
      "selection-shortcut-action-panel"
    );
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened in Smart Window"
    );
    await TestUtils.waitForTick();
    Assert.equal(
      win.document.getElementById("selection-callout-panel")?.state ?? "closed",
      "closed",
      "No callout in Smart Window"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that the outline is drawn again when the callout changes size.
 */
add_task(async function test_selection_callout_redraws_on_resize() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const edge = callout.querySelector(".selection-callout-edge");
    await TestUtils.waitForCondition(
      () => edge.getAttribute("d"),
      "Callout outline was drawn"
    );
    const before = edge.getAttribute("d");
    const body = callout.querySelector(".panel-subview-body");
    const inlineWidth = body.style.width;
    body.style.width = "360px";
    await TestUtils.waitForCondition(
      () => edge.getAttribute("d") != before,
      "Callout outline was drawn again"
    );
    body.style.width = inlineWidth;

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the callout takes its colors from the popup rather than the
 * toolbar text, so a theme with light tab text and a light popup cannot leave
 * it with light text on a light card.
 */
add_task(async function test_selection_callout_colors_contrast() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  const popupSet = document.getElementById("mainPopupSet");
  const root = document.documentElement;
  popupSet.style.setProperty("--panel-background-color", "rgb(255, 255, 255)");
  popupSet.style.setProperty("--panel-text-color", "rgb(0, 0, 0)");
  root.style.setProperty("--text-color", "rgb(255, 255, 255)");

  try {
    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const callout = await waitForCalloutOpen();

      const text = getComputedStyle(
        callout.querySelector(".selection-callout-message")
      ).color;
      const card = getComputedStyle(
        callout.querySelector(".selection-callout-edge")
      ).fill;
      Assert.equal(
        card,
        "rgb(255, 255, 255)",
        "Card uses the popup background"
      );
      Assert.equal(text, "rgb(0, 0, 0)", "Text uses the popup text color");
      Assert.notEqual(text, card, "Text differs from the card behind it");

      await hideSelectionMenu(panel);
    });
  } finally {
    popupSet.style.removeProperty("--panel-background-color");
    popupSet.style.removeProperty("--panel-text-color");
    root.style.removeProperty("--text-color");
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that using the AI action while the callout is open closes it.
 */
add_task(async function test_selection_callout_closes_for_ai_action() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    document.getElementById("ai-action-button").click();
    await hidden;

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that hiding the menu with a CSS-only IME hide also closes the callout,
 * which would otherwise float over the page on its own.
 */
add_task(async function test_selection_callout_hides_for_ime() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    await SpecialPowers.spawn(browser, [], () => {
      content.document.dispatchEvent(
        new content.CompositionEvent("compositionstart", {
          bubbles: true,
          data: "",
        })
      );
      content.document.dispatchEvent(
        new content.Event("selectionchange", { bubbles: true })
      );
    });
    await hidden;
    Assert.ok(panel.hasAttribute("ime-hiding"), "Menu is hidden for the IME");

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the callout does not come back when a CSS-only IME hide ends and
 * the menu is shown again without having closed.
 */
add_task(async function test_selection_callout_stays_away_after_ime() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    let hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    await SpecialPowers.spawn(browser, [], () => {
      content.document.dispatchEvent(
        new content.CompositionEvent("compositionstart", {
          bubbles: true,
          data: "",
        })
      );
      content.document.dispatchEvent(
        new content.Event("selectionchange", { bubbles: true })
      );
    });
    await hidden;
    Assert.ok(panel.hasAttribute("ime-hiding"), "Menu is hidden for the IME");

    await SpecialPowers.spawn(browser, [], () => {
      content.document.dispatchEvent(
        new content.CompositionEvent("compositionend", {
          bubbles: true,
          data: "",
        })
      );
    });
    await BrowserTestUtils.synthesizeMouseAtCenter(
      browser,
      { type: "mouseup" },
      browser
    );
    await TestUtils.waitForCondition(
      () => !panel.hasAttribute("ime-hiding"),
      "Menu is shown again after the IME"
    );
    await TestUtils.waitForTick();
    Assert.equal(getCalloutState(), "closed", "Callout does not come back");

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a callout still waiting on its translation does not open once an
 * IME has hidden the menu.
 */
add_task(async function test_selection_callout_pending_does_not_open_in_ime() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, true]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    Assert.equal(getCalloutState(), "closed", "Callout is not open yet");

    await SpecialPowers.pushPrefEnv({ set: [[CALLOUT_SEEN_PREF, false]] });
    const pending = panel.showCallout();
    panel.setAttribute("ime-hiding", "true");
    await pending;
    await TestUtils.waitForTick();
    Assert.equal(getCalloutState(), "closed", "Callout stays closed");

    panel.removeAttribute("ime-hiding");
    await hideSelectionMenu(panel);
    await SpecialPowers.popPrefEnv();
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the first click on an action while the callout is open reaches
 * the action instead of only closing the callout.
 */
add_task(async function test_selection_callout_click_reaches_action() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, [CALLOUT_SEEN_PREF, false]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const hidden = BrowserTestUtils.waitForEvent(callout, "popuphidden");
    const panelHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await EventUtils.synthesizeNativeMouseEvent({
      type: "click",
      target: document.getElementById("copy-action-button"),
      atCenter: true,
    });
    await hidden;

    const clicks = Glean.selectionMenu.actionClick.testGetValue();
    Assert.equal(clicks?.length, 1, "The click copied the selection");
    Assert.equal(clicks[0].extra.action, "copy", "The copy action was clicked");

    await panelHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the size of the feature callout card and its text.
 */
add_task(async function test_selection_callout_sizes() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ...MORE_ACTIONS_PREFS,
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 0],
      [CALLOUT_SEEN_PREF, false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const callout = await waitForCalloutOpen();

    const body = callout.querySelector(".panel-subview-body");
    const bodyStyle = getComputedStyle(body);
    Assert.equal(body.getBoundingClientRect().width, 288, "Card is 288px wide");
    Assert.deepEqual(
      [
        bodyStyle.paddingTop,
        bodyStyle.paddingInlineStart,
        bodyStyle.paddingInlineEnd,
      ],
      ["12px", "15px", "15px"],
      "Card is padded 12px above and 15px on both sides"
    );
    Assert.equal(
      getComputedStyle(callout.querySelector(".selection-callout-title"))
        .fontSize,
      "15px",
      "Title is 15px"
    );
    Assert.equal(
      getComputedStyle(callout.querySelector(".selection-callout-message"))
        .fontSize,
      "13px",
      "Text is 13px"
    );
    const link = callout.querySelector(".selection-callout-more-link");
    const linkRect = link.getBoundingClientRect();
    Assert.deepEqual(
      [linkRect.width, linkRect.height],
      [16, 16],
      "The link in the sentence is a 16px icon"
    );
    Assert.ok(
      getComputedStyle(link, "::after").maskImage.includes("more.svg"),
      "The link draws the same icon as the three-dot button"
    );
    const textBefore = document.createRange();
    textBefore.selectNodeContents(link.previousSibling);
    const lastLine = [...textBefore.getClientRects()].at(-1);
    Assert.less(
      Math.abs(
        linkRect.top +
          linkRect.height / 2 -
          (lastLine.top + lastLine.height / 2)
      ),
      6,
      "The icon sits on the line of the words before it"
    );
    const mark = callout
      .querySelector(".selection-callout-mark")
      .getBoundingClientRect();
    Assert.deepEqual([mark.width, mark.height], [39, 39], "Mark is 39px");

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Read what the stylesheets declare for a property of a rule, which some
 * platforms or themes override in the computed style.
 *
 * @param {string} selector the end of the rule's selector
 * @param {string} property the property to read
 * @returns {string|null} the declared value
 */
function declaredStyle(selector, property) {
  let declared = null;
  const find = rules => {
    for (const rule of rules) {
      if (rule.selectorText?.endsWith(selector)) {
        declared = rule.style.getPropertyValue(property) || declared;
      }
      // Imported sheets, conditional groups and nested rules hold rules of
      // their own.
      const inner = rule.styleSheet?.cssRules ?? rule.cssRules;
      if (inner) {
        find(inner);
      }
    }
  };
  for (const sheet of document.styleSheets) {
    try {
      find(sheet.cssRules);
    } catch (e) {
      // Sheets that can't be read have no such rule.
    }
  }
  return declared;
}

/**
 * Check that the callout asks for no transparent margin around its window. A
 * margin makes the window cover the strip, which breaks the hover highlight of
 * the strip's buttons while the callout shows.
 */
add_task(async function test_selection_callout_window_has_no_margin() {
  Assert.equal(
    declaredStyle("#selection-callout-panel", "--panel-box-shadow-margin"),
    "0px",
    "The callout window has no margin"
  );
});

/**
 * Check that the callout takes its outline colors and its shadow from the
 * design system tokens instead of its own values.
 */
add_task(async function test_selection_callout_uses_design_tokens() {
  Assert.deepEqual(
    [
      declaredStyle(
        "#selection-callout-panel",
        "--selection-callout-edge-start"
      ),
      declaredStyle("#selection-callout-panel", "--selection-callout-edge-end"),
    ],
    ["var(--color-violet-30)", "var(--color-orange-30)"],
    "The outline gradient uses the violet and orange tokens"
  );
  const shadow = declaredStyle(".selection-callout-shape", "filter");
  for (const token of [
    "--box-shadow-color-lighter-layer-1",
    "--box-shadow-color-lighter-layer-2",
  ]) {
    Assert.ok(shadow?.includes(`var(${token})`), `The shadow uses ${token}`);
  }
});

function contentRect(popup) {
  return popup.shadowRoot
    .querySelector("[part=content]")
    .getBoundingClientRect();
}

async function openAskDropdown(browser) {
  const popup = document.getElementById("chat-shortcuts-options-panel");
  const shown = BrowserTestUtils.waitForEvent(popup, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("ai-action-button"),
    {},
    window
  );
  await shown;
  // Let the page hear that a dropdown is open before the next input.
  await SpecialPowers.spawn(browser, [], () => {});
  return popup;
}

/**
 * Check that Escape closes an open dropdown first and then the bar, whether
 * the focus is in the page or in the browser window.
 */
add_task(async function test_escape_closes_dropdown_then_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    let panel = await showSelectionMenu(browser);
    let popup = await openAskDropdown(browser);

    let dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
    await dropdownHidden;
    await SpecialPowers.spawn(browser, [], () => {});
    Assert.equal(panel.state, "open", "The first Escape leaves the bar open");

    let barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
    await barHidden;

    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    panel = await showSelectionMenu(browser);
    popup = await openAskDropdown(browser);
    dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await dropdownHidden;
    Assert.equal(
      panel.state,
      "open",
      "The first Escape leaves the bar open from the window"
    );
    barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Escape closes an open dropdown first when the selection is in a
 * cross-origin iframe, so the dropdown state reaches that frame's actor.
 */
add_task(async function test_escape_closes_dropdown_then_bar_in_iframe() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    await SpecialPowers.spawn(browser, ["https://example.org/"], async src => {
      const frame = content.document.createElement("iframe");
      frame.src = src;
      const loaded = ContentTaskUtils.waitForEvent(frame, "load");
      content.document.body.appendChild(frame);
      await loaded;
    });
    const frameContext = browser.browsingContext.children[0];
    await SimpleTest.promiseFocus(browser);
    await BrowserTestUtils.synthesizeMouse(
      "body",
      10,
      10,
      { type: "mousedown" },
      frameContext
    );
    await SpecialPowers.spawn(frameContext, [], () => {
      content.getSelection().selectAllChildren(content.document.body);
    });
    await BrowserTestUtils.synthesizeMouse(
      "body",
      60,
      10,
      { type: "mouseup" },
      frameContext
    );
    const panel = document.getElementById("selection-shortcut-action-panel");
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened for the frame selection"
    );

    const popup = await openAskDropdown(frameContext);

    const dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, frameContext);
    await dropdownHidden;
    await SpecialPowers.spawn(frameContext, [], () => {});
    Assert.equal(panel.state, "open", "The first Escape leaves the bar open");

    const barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, frameContext);
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the later presses of a double click do not take the bar down.
 */
add_task(async function test_double_click_keeps_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    let hiding = 0;
    const onHiding = event => {
      if (event.target == panel) {
        hiding++;
      }
    };
    panel.addEventListener("popuphiding", onHiding);

    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mousedown", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () => {});
    Assert.equal(panel.state, "open", "The second press keeps the bar open");
    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mouseup", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () => {});
    panel.removeEventListener("popuphiding", onHiding);
    Assert.equal(hiding, 0, "The bar never closed during the double click");

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a double click that ends with a different selection and nothing
 * new to show takes the bar down, rather than leaving it for stale text.
 */
add_task(async function test_click_burst_ending_without_selection_hides_bar() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, ["browser.ml.chat.shortcuts.debounce", 50]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mousedown", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    Assert.equal(panel.state, "open", "The bar stays open during the burst");

    const barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mouseup", clickCount: 2 },
      browser
    );
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that with the feature gate off, Escape and the first press outside
 * behave as they always have.
 */
add_task(async function test_dismissal_unchanged_without_feature_gate() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.highlightToSearch.featureGate", false],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    let panel = await showSelectionMenu(browser);
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
    await SpecialPowers.spawn(browser, [], () => {});
    Assert.equal(panel.state, "open", "Escape does not close the bar");

    const popup = await openAskDropdown(browser);
    const barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeMouse(browser, 300, 150, {}, browser);
    await barHidden;
    Assert.equal(popup.state, "closed", "A press outside closes both");
  });
  await SpecialPowers.popPrefEnv();
});

/**
 * Check the size of the strip, its buttons and the Copied pill.
 */
add_task(async function test_strip_sizes() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const rect = id => document.getElementById(id).getBoundingClientRect();

    Assert.equal(contentRect(panel).height, 42, "The strip is 42px tall");
    for (const id of [
      "search-action-button",
      "copy-action-button",
      "more-actions-button",
    ]) {
      Assert.deepEqual(
        [rect(id).width, rect(id).height],
        [36, 36],
        `${id} is a 36px circle`
      );
    }
    Assert.less(
      Math.abs(rect("ai-action-button").width - 51),
      3.5,
      "The AI action is about a 51px pill, depending on the font size"
    );
    Assert.equal(
      rect("ai-action-button").height,
      36,
      "The AI action is 36px tall"
    );
    const aiButtonPart = document
      .getElementById("ai-action-button")
      .shadowRoot.querySelector("[part=button]");
    Assert.equal(
      getComputedStyle(aiButtonPart, "::after").width,
      getComputedStyle(aiButtonPart, "::after").height,
      "The arrow is not squeezed narrower than its height"
    );

    const confirmation = document.getElementById("copy-action-confirmation");
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("copy-action-button"),
      {},
      window
    );
    await TestUtils.waitForCondition(
      () => confirmation.hasAttribute("copied"),
      "Confirmation is shown"
    );
    Assert.equal(
      confirmation.getBoundingClientRect().height,
      36,
      "The Copied pill is as tall as the buttons"
    );

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the size of the Ask AI dropdown.
 */
add_task(async function test_ask_dropdown_sizes() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const popup = await openAskDropdown(browser);

    const rows = popup.querySelectorAll("toolbarbutton");
    Assert.greater(rows.length, 0, "The dropdown has prompts");
    for (const row of rows) {
      Assert.equal(row.getBoundingClientRect().height, 32, "Rows are 32px");
    }
    Assert.greaterOrEqual(
      contentRect(popup).width,
      240,
      "The dropdown is at least 240px wide"
    );

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the placeholder of the Ask AI dropdown for a provider with no name.
 */
add_task(async function test_ask_dropdown_placeholder_without_provider_name() {
  for (const [gate, expected] of [
    [true, "Ask AI…"],
    [false, "Ask AI chatbot…"],
  ]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        ...MORE_ACTIONS_PREFS,
        ["browser.ml.chat.provider", "https://unknown.example"],
        ["browser.highlightToSearch.featureGate", gate],
      ],
    });

    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const popup = await openAskDropdown(browser);
      const textarea = popup.querySelector("textarea");
      await document.l10n.translateElements([textarea]);
      Assert.equal(
        textarea.placeholder,
        expected,
        `The placeholder reads "${expected}" with the gate ${gate ? "on" : "off"}`
      );
      await hideSelectionMenu(panel);
    });

    await SpecialPowers.popPrefEnv();
  }
});
