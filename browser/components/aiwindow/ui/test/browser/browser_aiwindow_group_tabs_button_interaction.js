/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head_aiwindow_group_tabs_button.js */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_aiwindow_group_tabs_button.js",
  this
);

describe("Auto Tab Grouping toolbar button", () => {
  let win, mockEngineManager;

  beforeEach(() => {
    mockEngineManager = setupAutoTabGroupingTest();
  });

  afterEach(async () => {
    await cleanupAutoTabGroupingTest(win, mockEngineManager);
    win = null;
  });

  describe("when the button is in the overflow menu", () => {
    let placement;

    afterEach(() => {
      if (!placement) {
        return;
      }
      CustomizableUI.addWidgetToArea(
        "smartwindow-group-tabs-button",
        placement.area,
        placement.position
      );
      placement = null;
    });

    it("opens the panel anchored to the overflow button", async () => {
      win = await openGroupingWindowWithTabs();
      placement = CustomizableUI.getPlacementOfWidget(
        "smartwindow-group-tabs-button"
      );
      CustomizableUI.addWidgetToArea(
        "smartwindow-group-tabs-button",
        CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
      );

      const overflowPanel = win.document.getElementById("widget-overflow");
      const overflowShown = BrowserTestUtils.waitForEvent(
        overflowPanel,
        "popupshown"
      );
      EventUtils.synthesizeMouseAtCenter(
        win.document.getElementById("nav-bar-overflow-button"),
        {},
        win
      );
      await overflowShown;

      EventUtils.synthesizeMouseAtCenter(
        win.document.getElementById("smartwindow-group-tabs-button"),
        {},
        win
      );
      const panel = await TestUtils.waitForCondition(() =>
        win.document.getElementById("smartwindow-group-tabs-panel")
      );
      await TestUtils.waitForCondition(
        () => panel.state === "open",
        "The panel opens even though the overflow menu rolls up with the click"
      );
      Assert.equal(
        panel.anchorNode.id,
        "nav-bar-overflow-button",
        "The panel is anchored to the overflow button, not the button it hides"
      );
      Assert.equal(
        overflowPanel.state,
        "closed",
        "The overflow menu is gone by the time the panel is up"
      );

      await closePanel(win);
    });
  });

  describe("previewing a suggested group", () => {
    // Open the flyout for the first suggestion and return its tab rows. The
    // opening popup takes focus off the row, so put it back once it settles.
    async function openFlyout(panel) {
      const row = panel.querySelector(".swgt-suggestion");
      row.focus();
      await TestUtils.waitForCondition(
        () =>
          panel._flyoutPanel?.state === "open" &&
          panel._flyoutPanel.querySelectorAll(".swgt-flyout-tab").length,
        "The flyout is open and lists the group's tabs"
      );
      row.focus();
      return {
        row,
        tabRows: [...panel._flyoutPanel.querySelectorAll(".swgt-flyout-tab")],
      };
    }

    it("is a dismissable popup beside a panel that is not", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row } = await openFlyout(panel);
      const flyout = panel._flyoutPanel;

      Assert.ok(
        panel.hasAttribute("noautohide"),
        "The panel stays up while the pointer is over the flyout"
      );
      Assert.ok(
        !flyout.hasAttribute("noautohide"),
        "The flyout can be rolled up, which is what lets Wayland place it past the window's edge"
      );
      Assert.equal(
        flyout.getAttribute("consumeoutsideclicks"),
        "never",
        "The click that rolls the flyout up still reaches the panel row under it"
      );

      // Rolling up bypasses _hideFlyout, so hide the flyout the way it does.
      flyout.hidePopup();
      await TestUtils.waitForCondition(
        () => flyout.state === "closed",
        "The flyout is hidden"
      );
      Assert.equal(
        panel._activeRow,
        null,
        "The row is released when something else hides the flyout"
      );
      Assert.equal(
        row.getAttribute("aria-expanded"),
        "false",
        "The row no longer reports its flyout as expanded"
      );
      Assert.ok(
        !row.classList.contains("is-active"),
        "The row drops its highlight"
      );
      Assert.equal(panel.state, "open", "The panel itself stays open");

      row.dispatchEvent(new win.MouseEvent("mouseenter"));
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && flyout.state !== "closed",
        "Pointing at the row again reopens its flyout"
      );
    });

    it("stays closed when rolling up hands focus back to its row", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row, tabRows } = await openFlyout(panel);
      const flyout = panel._flyoutPanel;

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "Focus moved into the flyout"
      );

      flyout.hidePopup();
      await TestUtils.waitForCondition(
        () => flyout.state === "closed",
        "The flyout is hidden"
      );
      Assert.equal(
        win.document.activeElement,
        row,
        "Hiding the flyout hands focus back to its row"
      );
      Assert.equal(
        panel._dismissedRow,
        row,
        "The row getting focus back is not a request to reopen the flyout"
      );
      Assert.equal(panel._activeRow, null, "The row is released");

      panel.querySelector(".swgt-create-all").focus();
      row.focus();
      Assert.equal(
        panel._activeRow,
        null,
        "Focus returning to the row leaves the flyout closed, as after Escape"
      );

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && flyout.state !== "closed",
        "Asking for the flyout again reopens it"
      );
    });

    it("hides the flyout once the pointer leaves it", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row } = await openFlyout(panel);

      row.blur();
      panel._flyoutPanel.dispatchEvent(new win.MouseEvent("mouseleave"));
      Assert.notEqual(
        panel._hideTimer,
        0,
        "Leaving the flyout schedules a hide"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The flyout hides itself once the pointer has left"
      );

      panel.querySelector(".swgt-create-all").focus();
      row.focus();
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && panel._flyoutPanel.state !== "closed",
        "Focusing the row again reopens the flyout the pointer dismissed"
      );
    });

    it("hides the flyout when another panel row is hovered", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row } = await openFlyout(panel);

      row.blur();
      row.dispatchEvent(new win.MouseEvent("mouseleave"));
      panel
        .querySelector(".swgt-create-all")
        .dispatchEvent(new win.MouseEvent("mouseover", { bubbles: true }));

      Assert.ok(
        !row.classList.contains("is-active"),
        "The suggestion drops its highlight the moment the pointer moves on"
      );
      Assert.equal(
        panel._activeRow,
        null,
        "The suggestion row is no longer marked active"
      );
      Assert.equal(panel._hideTimer, 0, "No delayed hide is left pending");
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Moving onto 'Create Groups' closes the suggestion's flyout"
      );

      panel.querySelector(".swgt-create-all").focus();
      row.focus();
      await TestUtils.waitForCondition(
        () => panel._activeRow === row && panel._flyoutPanel.state !== "closed",
        "Keyboard focus on that suggestion still opens its flyout afterwards"
      );
    });

    it("keeps the flyout the keyboard opened when the pointer drifts away", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row, tabRows } = await openFlyout(panel);
      const suggestion = AutoTabGrouping._getState(win).suggestions[0];

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "Focus moved into the flyout"
      );

      row.dispatchEvent(new win.MouseEvent("mouseleave"));

      Assert.equal(
        panel._flyoutPanel._flyoutEl.suggestion,
        suggestion,
        "The flyout still lists the group the keyboard drilled into"
      );
      Assert.equal(
        panel._hideTimer,
        0,
        "Pointing away does not schedule a hide while the flyout holds focus"
      );
    });

    it("hands the flyout over to a row the pointer lands on", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { tabRows } = await openFlyout(panel);
      const rows = [...panel.querySelectorAll(".swgt-suggestion")];
      const suggestions = AutoTabGrouping._getState(win).suggestions;

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "Focus moved into the flyout"
      );

      rows[1].dispatchEvent(new win.MouseEvent("mouseover", { bubbles: true }));
      rows[1].dispatchEvent(new win.MouseEvent("mouseenter"));

      await TestUtils.waitForCondition(
        () => panel._flyoutPanel._flyoutEl.suggestion === suggestions[1],
        "Pointing at another suggestion takes the flyout the keyboard opened"
      );
      Assert.ok(
        panel._flyoutPanel.contains(win.document.activeElement),
        "Focus stays on the tabs, which now belong to the hovered suggestion"
      );
    });

    it("lists the group's tabs and switches to the one clicked", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { tabRows } = await openFlyout(panel);

      Assert.equal(
        panel._flyoutPanel.getAttribute("flip"),
        "slide",
        "The flyout slides rather than flipping when it meets a screen edge"
      );

      const suggestion = AutoTabGrouping._getState(win).suggestions[0];
      Assert.equal(
        tabRows.length,
        suggestion.tabs.length,
        "One row per tab in the suggested group"
      );

      tabRows[1].click();
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "Selecting a tab closes the panel"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        suggestion.tabs[1],
        "The clicked tab is selected"
      );
      Assert.equal(
        win.gBrowser.tabGroups.length,
        0,
        "Previewing a group's tabs does not create the group"
      );
    });

    it("moves focus into the flyout and back out again", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const { row, tabRows } = await openFlyout(panel);

      EventUtils.synthesizeKey("KEY_ArrowRight", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement === tabRows[0],
        "The forward arrow moves focus to the first tab"
      );

      EventUtils.synthesizeKey("KEY_End", {}, win);
      Assert.equal(
        win.document.activeElement,
        tabRows.at(-1),
        "End jumps to the last tab"
      );
      EventUtils.synthesizeKey("KEY_Home", {}, win);
      Assert.equal(
        win.document.activeElement,
        tabRows[0],
        "Home jumps back to the first tab"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        tabRows[1],
        "ArrowDown moves to the next tab"
      );

      let escapeEvent;
      win.addEventListener(
        "keydown",
        e => {
          escapeEvent = e;
        },
        { once: true }
      );
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      Assert.equal(
        win.document.activeElement,
        row,
        "Escape steps back out to the suggestion row"
      );
      Assert.ok(
        escapeEvent.defaultPrevented,
        "Escape is consumed instead of reaching the window's own Escape key"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "Escape inside the flyout leaves the panel open"
      );
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Escape closes the flyout it backed out of"
      );

      EventUtils.synthesizeKey("KEY_ArrowLeft", {}, win);
      await TestUtils.waitForCondition(
        () => win.document.activeElement?.matches(".swgt-flyout-tab"),
        "Either arrow re-enters the flyout, whichever side it opens on"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The flyout is closed again"
      );
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => !win.document.getElementById("smartwindow-group-tabs-panel"),
        "A second Escape, with no flyout left to close, closes the panel"
      );
    });

    it("closes only the flyout when Escape is pressed while pointing at it", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      await openFlyout(panel);

      // Focus stays on the card: hovering a row opens the flyout without
      // moving focus, which is the case that used to close both panels.
      Assert.ok(
        !panel._flyoutPanel.contains(win.document.activeElement),
        "Focus is outside the flyout"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "Escape closes the pointer-opened flyout"
      );
      Assert.ok(
        win.document.getElementById("smartwindow-group-tabs-panel"),
        "The panel itself stays open"
      );
    });

    it("drops a focus request when the flyout hides before it opens", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const rows = [...panel.querySelectorAll(".swgt-suggestion")];
      const suggestions = AutoTabGrouping._getState(win).suggestions;

      AutoTabGrouping._showFlyoutById(win, panel, suggestions[0].id, rows[0]);
      AutoTabGrouping._focusFlyout(panel);
      AutoTabGrouping._hideFlyout(panel);

      rows[1].focus();
      await BrowserTestUtils.waitForEvent(panel._flyoutPanel, "popupshown");
      Assert.equal(
        win.document.activeElement,
        rows[1],
        "Reopening the flyout on hover does not steal focus into it"
      );
    });
  });

  describe("moving through the panel's rows with the keyboard", () => {
    let flyoutStub;

    beforeEach(() => {
      // Focusing a row opens its flyout, and the opening popup takes focus off
      // the row; the flyout's own navigation is covered above.
      flyoutStub = sinon.stub(AutoTabGrouping, "_showFlyoutById");
    });

    afterEach(() => {
      flyoutStub.restore();
    });

    it("wraps around with the arrow keys and jumps with Home and End", async () => {
      win = await openGroupingWindowWithTabs();
      const panel = await openPanelWithSuggestions(win);
      const card = panel.querySelector(".swgt-card");
      const rows = [...panel.querySelectorAll(".swgt-row")];
      Assert.equal(rows.length, 3, "'Create all' plus the two suggestions");
      if (win.document.activeElement !== card) {
        await BrowserTestUtils.waitForEvent(card, "focus");
      }

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowDown enters the list at the first row"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[1],
        "ArrowDown moves to the next row"
      );

      EventUtils.synthesizeKey("KEY_ArrowUp", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowUp moves back to the previous row"
      );

      EventUtils.synthesizeKey("KEY_ArrowUp", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows.at(-1),
        "ArrowUp from the first row wraps to the last"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "ArrowDown from the last row wraps to the first"
      );

      EventUtils.synthesizeKey("KEY_End", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows.at(-1),
        "End jumps to the last row"
      );

      EventUtils.synthesizeKey("KEY_Home", {}, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "Home jumps back to the first row"
      );

      EventUtils.synthesizeKey("KEY_ArrowDown", { accelKey: true }, win);
      Assert.equal(
        win.document.activeElement,
        rows[0],
        "A modified arrow is left alone"
      );
    });
  });

  describe("panel localization and keyboard access", () => {
    it("localizes strings and supports keyboard operation", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.autoTabGrouping.enabled", true]],
      });

      win = await openAIWindow();
      await navigateToContent(win);
      await addWebTabs(win);

      const button = win.document.getElementById(
        "smartwindow-group-tabs-button"
      );
      const popupSet = win.document.getElementById("mainPopupSet");

      AIWindowUI.toggleGroupTabsPanel(win);
      const panel = await BrowserTestUtils.waitForMutationCondition(
        popupSet,
        { childList: true },
        () => win.document.getElementById("smartwindow-group-tabs-panel")
      );
      const card = panel.querySelector(".swgt-card");
      await BrowserTestUtils.waitForMutationCondition(
        card,
        { childList: true, subtree: true },
        () => card.querySelectorAll(".swgt-suggestion").length === 2
      );

      Assert.equal(card.getAttribute("role"), "dialog", "Card is a dialog");
      if (win.document.activeElement !== card) {
        await BrowserTestUtils.waitForEvent(card, "focus");
      }
      Assert.equal(
        win.document.activeElement,
        card,
        "Focus moves to the dialog card when the panel opens"
      );

      const heading = panel.querySelector(".swgt-header");
      Assert.equal(
        heading.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-panel-heading",
        "Heading is localized via Fluent"
      );
      const createAll = panel.querySelector(".swgt-create-all");
      Assert.equal(
        createAll.querySelector(".swgt-row-label").getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-create-all",
        "'Create all' label is localized via Fluent"
      );
      const suggestionRow = panel.querySelector(".swgt-suggestion");
      Assert.equal(
        suggestionRow.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-suggestion",
        "Suggestion row's accessible name is localized via Fluent"
      );
      const accService = Cc["@mozilla.org/accessibilityService;1"].getService(
        Ci.nsIAccessibilityService
      );
      await TestUtils.waitForCondition(() => {
        const acc = accService.getAccessibleFor(suggestionRow);
        return acc && acc.name && acc.name.trim();
      }, "Suggestion row exposes a non-empty accessible name");

      const favicons = suggestionRow.querySelector(".swgt-favicons");
      Assert.equal(
        favicons.getAttribute("aria-hidden"),
        "true",
        "Tab favicons are hidden from assistive technology"
      );
      const images = [...favicons.querySelectorAll("img.swgt-favicon")];
      Assert.greater(images.length, 0, "Suggestion row shows tab favicons");
      Assert.ok(
        images.every(img => img.src.startsWith("page-icon:")),
        "Each favicon loads through page-icon:"
      );

      suggestionRow.focus();
      const flyoutList = await BrowserTestUtils.waitForMutationCondition(
        popupSet,
        { childList: true, subtree: true },
        () => panel._flyoutPanel?.querySelector(".swgt-flyout-list")
      );
      Assert.equal(
        flyoutList.getAttribute("data-l10n-id"),
        "smartwindow-group-tabs-flyout-list",
        "Flyout's accessible name is localized via Fluent"
      );
      await TestUtils.waitForCondition(
        () =>
          accService.getAccessibleFor(flyoutList)?.role ===
          Ci.nsIAccessibleRole.ROLE_MENUPOPUP,
        "Flyout is exposed as a menu"
      );
      const flyoutTab = flyoutList.querySelector(".swgt-flyout-tab");
      Assert.equal(
        accService.getAccessibleFor(flyoutTab).role,
        Ci.nsIAccessibleRole.ROLE_MENUITEM,
        "Each tab in the flyout is a menu item"
      );
      Assert.equal(
        suggestionRow.getAttribute("aria-expanded"),
        "true",
        "Focused suggestion row reports its flyout as expanded"
      );

      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await TestUtils.waitForCondition(
        () => panel._flyoutPanel.state === "closed",
        "The first Escape closes the open flyout"
      );

      const focusReturned = BrowserTestUtils.waitForEvent(button, "focus");
      EventUtils.synthesizeKey("KEY_Escape", {}, win);
      await focusReturned;
      Assert.ok(
        !win.document.getElementById("smartwindow-group-tabs-panel"),
        "A second Escape closes the panel"
      );
      Assert.equal(
        win.document.activeElement,
        button,
        "Focus returns to the toolbar button after Escape"
      );
    });
  });
});
