/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

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
