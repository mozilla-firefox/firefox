/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/GenAI.sys.mjs"
);

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
