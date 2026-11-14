/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

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

    // The native click left the real cursor where the copy button appears, so
    // the OS would hover it each time a later selection menu opens.
    await EventUtils.promiseNativeMouseEvent({
      type: "mousemove",
      target: gURLBar.inputField,
      offsetX: 0,
      offsetY: 0,
    });
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
