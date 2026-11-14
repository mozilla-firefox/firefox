/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/GenAI.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { SearchService } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchService.sys.mjs"
);

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
