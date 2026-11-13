/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

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
