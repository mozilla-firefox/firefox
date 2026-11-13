/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const UPGRADE_NOT_AVAILABLE_PREF = "browser.ipProtection.upgradeNotAvailable";

// The recommended option, which the list always renders first.
const RECOMMENDED_CODE = "REC";

const MOCK_LOCATIONS_LIST = [
  { code: "US", available: true },
  { code: "CA", available: true },
  { code: "DE", available: false },
];

/**
 * Opens the IP Protection panel, then navigates to the locations subview.
 *
 * @param {object} state - Additional state to set on the panel.
 * @param {boolean} [keyboardActivated] - Whether to open via keyboard activation.
 * @returns {Promise<{backButton: Element, firstListItem: Element, promoButton: Element|null, locationsView: Element}>}
 */
async function openLocationsSubview(state = {}, keyboardActivated = false) {
  await openPanel({
    isEnrolledAndEntitled: true,
    locationsList: MOCK_LOCATIONS_LIST,
    ...state,
  });

  let panel = IPProtection.getPanel(window);
  let locationsView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.LOCATIONS_PANELVIEW
  );

  let mainView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.MAIN_PANELVIEW
  );
  let content = mainView.querySelector(IPProtectionPanel.CONTENT_TAGNAME);
  let locationButton = content?.statusCardEl?.locationButtonEl ?? null;

  let viewShownPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  );
  panel.showLocationSelector(keyboardActivated, locationButton);
  await viewShownPromise;

  let locationsEl = locationsView.querySelector(
    IPProtectionPanel.LOCATIONS_TAGNAME
  );
  await locationsEl?.updateComplete;

  let locationsList = locationsView.querySelector("locations-list");
  await locationsList?.updateComplete;

  let backButton = locationsView.querySelector(".subviewbutton-back");
  let firstListItem = locationsList?.querySelector(".location-item");
  let promoButton = locationsView.querySelector("moz-promo moz-button");

  return {
    backButton,
    firstListItem,
    locationButton,
    promoButton,
    locationsView,
  };
}

/**
 * Tests that we can tab through all elements in the subview with the promo shown,
 * ignoring list items.
 */
add_task(async function test_locations_tab_nav_with_promo() {
  let { backButton, firstListItem, promoButton } = await openLocationsSubview({
    hasUpgraded: false,
  });

  Assert.ok(promoButton, "promo button should be present when not upgraded");

  backButton.focus();

  await expectFocusAfterKey("Tab", firstListItem);
  await expectFocusAfterKey("Tab", promoButton);
  await expectFocusAfterKey("Tab", backButton);

  await expectFocusAfterKey("Shift+Tab", promoButton);
  await expectFocusAfterKey("Shift+Tab", firstListItem);
  await expectFocusAfterKey("Shift+Tab", backButton);

  await closePanel();
  cleanupService();
});

/**
 * Tests that we can tab through all elements in the subview with the promo hidden,
 * ignoring list items.
 */
add_task(async function test_locations_tab_nav_without_promo() {
  let { backButton, firstListItem, promoButton } = await openLocationsSubview({
    hasUpgraded: true,
  });

  Assert.ok(!promoButton, "promo button should not be present when upgraded");

  backButton.focus();

  await expectFocusAfterKey("Tab", firstListItem);
  await expectFocusAfterKey("Tab", backButton);

  // Shift+Tab reverses.
  await expectFocusAfterKey("Shift+Tab", firstListItem);
  await expectFocusAfterKey("Shift+Tab", backButton);

  await closePanel();
  cleanupService();
});

/**
 * Tests that the promo button is not present when the upgradeNotAvailable pref
 * is set, and that tab order skips directly from the list to the back button.
 * Covers the pref being set before the panel opens as well as being flipped
 * while the subview is showing.
 */
add_task(async function test_locations_tab_nav_upgrade_not_available() {
  /**
   * @param {Element} locationsView - The locations subview.
   * @param {string} description - What the current pref state is.
   */
  let assertPromoSkipped = async (locationsView, description) => {
    Assert.ok(
      !locationsView.querySelector("moz-promo moz-button"),
      `promo button should not be present ${description}`
    );

    let backButton = locationsView.querySelector(".subviewbutton-back");
    let firstListItem = locationsView.querySelector(".location-item");

    backButton.focus();

    await expectFocusAfterKey("Tab", firstListItem);
    await expectFocusAfterKey("Tab", backButton);

    await expectFocusAfterKey("Shift+Tab", firstListItem);
    await expectFocusAfterKey("Shift+Tab", backButton);
  };

  let setUpgradeNotAvailable = async (locationsView, value) => {
    Services.prefs.setBoolPref(UPGRADE_NOT_AVAILABLE_PREF, value);
    let locationsEl = locationsView.querySelector(
      IPProtectionPanel.LOCATIONS_TAGNAME
    );
    await locationsEl.updateComplete;
  };

  await SpecialPowers.pushPrefEnv({
    set: [[UPGRADE_NOT_AVAILABLE_PREF, true]],
  });

  let { locationsView } = await openLocationsSubview({ hasUpgraded: false });

  await assertPromoSkipped(locationsView, "when the pref is set on startup");

  // Clearing the pref at runtime should bring the promo back.
  await setUpgradeNotAvailable(locationsView, false);

  let promoButton = locationsView.querySelector("moz-promo moz-button");
  Assert.ok(
    promoButton,
    "promo button should be present after the pref is cleared at runtime"
  );

  let backButton = locationsView.querySelector(".subviewbutton-back");
  let firstListItem = locationsView.querySelector(".location-item");

  backButton.focus();

  await expectFocusAfterKey("Tab", firstListItem);
  await expectFocusAfterKey("Tab", promoButton);
  await expectFocusAfterKey("Tab", backButton);

  // Setting it again at runtime should remove the promo from the tab order.
  await setUpgradeNotAvailable(locationsView, true);

  await assertPromoSkipped(locationsView, "after the pref is set at runtime");

  await closePanel();
  cleanupService();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that Tab from any list item skips directly to promo,
 * and Shift+Tab from any list item goes back to the back button.
 */
add_task(async function test_locations_tab_exits_list_from_any_item() {
  let { locationsView, promoButton } = await openLocationsSubview({
    hasUpgraded: false,
  });

  Assert.ok(promoButton, "promo button should be present");

  let locationsList = locationsView.querySelector("locations-list");
  let listItems = Array.from(
    locationsList.querySelectorAll(".location-item:not([aria-disabled])")
  );

  Assert.greater(listItems.length, 1, "should have more than one enabled item");

  // Tab from a non-first list item should jump straight to promo.
  listItems[1].focus();
  await expectFocusAfterKey("Tab", promoButton);

  // Shift+Tab from a non-first list item should jump straight to back button.
  let backButton = locationsView.querySelector(".subviewbutton-back");
  listItems[1].focus();
  await expectFocusAfterKey("Shift+Tab", backButton);

  await closePanel();
  cleanupService();
});

/**
 * Tests that the list keeps a single tab stop that follows the focused item
 * while the subview stays open, so tabbing out of and back into the list
 * returns focus to that item rather than the selected option it started on.
 */
add_task(async function test_locations_tab_stop_follows_focused_item() {
  let { backButton, locationsView, promoButton } = await openLocationsSubview({
    hasUpgraded: false,
  });

  Assert.ok(promoButton, "promo button should be present");

  let locationsList = locationsView.querySelector("locations-list");
  let listItems = Array.from(
    locationsList.querySelectorAll(".location-item:not([aria-disabled])")
  );

  Assert.greater(listItems.length, 1, "should have more than one enabled item");

  // Move focus onto a non-default list item; the roving tabindex should follow.
  let targetItem = listItems[1];
  targetItem.focus();
  await locationsList.updateComplete;

  Assert.equal(
    targetItem.getAttribute("tabindex"),
    "0",
    "the focused item should become the single tab stop"
  );
  Assert.equal(
    listItems.filter(item => item.getAttribute("tabindex") === "0").length,
    1,
    "only one list item should be in the tab order"
  );

  // Tab out of the list and cycle back in; focus should return to the last
  // focused item, not the recommended item.
  await expectFocusAfterKey("Tab", promoButton);
  await expectFocusAfterKey("Tab", backButton);
  await expectFocusAfterKey("Tab", targetItem);

  await closePanel();
  cleanupService();
});

/**
 * Tests that ArrowDown and ArrowUp navigate only within the list items and
 * wrap correctly.
 */
add_task(async function test_locations_arrow_keys_navigate_list() {
  let { firstListItem, locationsView } = await openLocationsSubview();

  let locationsList = locationsView.querySelector("locations-list");
  let listItems = Array.from(locationsList.querySelectorAll(".location-item"));

  Assert.greater(listItems.length, 1, "should have more than one enabled item");

  firstListItem.focus();

  await expectFocusAfterKey("ArrowDown", listItems[1]);
  await expectFocusAfterKey("ArrowUp", listItems[0]);

  // ArrowUp from first item wraps to last.
  await expectFocusAfterKey("ArrowUp", listItems[listItems.length - 1]);

  // ArrowDown from last item wraps to first.
  await expectFocusAfterKey("ArrowDown", listItems[0]);

  await closePanel();
  cleanupService();
});

/**
 * Tests that an unavailable location is still reachable with the arrow keys and
 * is not skipped during keyboard navigation.
 */
add_task(async function test_locations_arrow_keys_include_unavailable() {
  let { locationsView } = await openLocationsSubview();

  let locationsList = locationsView.querySelector("locations-list");
  let listItems = Array.from(locationsList.querySelectorAll(".location-item"));

  let unavailableItem = locationsList.querySelector(
    ".location-item[aria-disabled]"
  );

  Assert.ok(unavailableItem, "an unavailable location item should be present");
  Assert.ok(
    listItems.includes(unavailableItem),
    "the unavailable item should be part of the navigable list"
  );

  let unavailableIndex = listItems.indexOf(unavailableItem);
  let previousItem =
    listItems[(unavailableIndex - 1 + listItems.length) % listItems.length];

  // ArrowDown from the item before the unavailable one should land on it.
  previousItem.focus();
  await expectFocusAfterKey("ArrowDown", unavailableItem);

  // ArrowDown again should continue past it, confirming it is a normal stop.
  let nextItem = listItems[(unavailableIndex + 1) % listItems.length];
  await expectFocusAfterKey("ArrowDown", nextItem);

  // ArrowUp should move focus back onto the unavailable item.
  await expectFocusAfterKey("ArrowUp", unavailableItem);

  await closePanel();
  cleanupService();
});

/**
 * Tests that ArrowDown and ArrowUp do not move focus when on non-list elements.
 */
add_task(async function test_locations_arrow_keys_ignored_outside_list() {
  let { backButton } = await openLocationsSubview();

  backButton.focus();

  // ArrowDown on back button should not move focus.
  EventUtils.synthesizeKey("KEY_ArrowDown", {});
  Assert.equal(
    document.activeElement,
    backButton,
    "ArrowDown on back button should not move focus"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that ArrowLeft closes the locations subview and returns focus to the
 * location button when keyboard activated.
 */
add_task(async function test_locations_arrow_left_closes_subview() {
  let { firstListItem, locationsView, locationButton } =
    await openLocationsSubview({}, true);

  firstListItem.focus();

  let viewHidingPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewHiding"
  );
  EventUtils.synthesizeKey("KEY_ArrowLeft", {});
  await viewHidingPromise;

  Assert.ok(
    !locationsView.hasAttribute("visible"),
    "ArrowLeft should close the subview"
  );

  Assert.ok(
    locationButton.matches(":focus"),
    "focus should return to the location button after ArrowLeft"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that ArrowRight closes the locations subview in RTL and returns focus
 * to the location button when keyboard activated.
 */
add_task(async function test_locations_arrow_right_closes_subview_in_rtl() {
  await SpecialPowers.pushPrefEnv({ set: [["intl.l10n.pseudo", "bidi"]] });

  let { firstListItem, locationsView, locationButton } =
    await openLocationsSubview({}, true);

  firstListItem.focus();

  let viewHidingPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewHiding"
  );
  EventUtils.synthesizeKey("KEY_ArrowRight", {});
  await viewHidingPromise;

  Assert.ok(
    !locationsView.hasAttribute("visible"),
    "ArrowRight should close the subview in RTL"
  );

  Assert.ok(
    locationButton.matches(":focus"),
    "focus should return to the location button after ArrowRight"
  );

  await closePanel();
  cleanupService();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that opening the subview via keyboard focuses the list's tab stop, which
 * starts on the selected location rather than the first list item, and that
 * closing via keyboard returns focus to the location button.
 *
 * Reopening the subview should move the tab stop back to the selected location:
 * PanelMultiView reuses the same list element when moving the subview between
 * its view stacks, so the roving tabindex has to be reset when the list is
 * reconnected.
 *
 * Opening via mouse should not affect focus in either direction.
 */
add_task(async function test_locations_keyboard_open_focuses_selected_item() {
  let { firstListItem, locationsView, locationButton } =
    await openLocationsSubview({ location: "CA" }, true);

  Assert.ok(locationButton, "location button should be present");

  let itemFor = code => locationsView.querySelector(`#location-option-${code}`);
  let locationsList = locationsView.querySelector("locations-list");

  Assert.ok(itemFor("CA"), "the selected location item should be present");
  Assert.notEqual(
    itemFor("CA"),
    firstListItem,
    "the selected location should not be the first list item"
  );
  Assert.equal(
    document.activeElement,
    itemFor("CA"),
    "keyboard-activated open should focus the selected location"
  );

  // Move the tab stop off the selected location.
  firstListItem.focus();
  await locationsList.updateComplete;

  Assert.equal(
    firstListItem.getAttribute("tabindex"),
    "0",
    "the newly focused item should become the tab stop"
  );
  Assert.equal(
    itemFor("CA").getAttribute("tabindex"),
    "-1",
    "the selected location should no longer be the tab stop"
  );

  // Close the subview with the back button.
  let backButton = locationsView.querySelector(".subviewbutton-back");
  backButton.focus();

  let viewHidingPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewHiding"
  );
  EventUtils.synthesizeKey("KEY_Enter", {});
  await viewHidingPromise;

  Assert.ok(
    locationButton.matches(":focus"),
    "focus should return to the location button after pressing the back button via keyboard"
  );

  // Reopen the subview; the tab stop should be back on the selected location.
  let panel = IPProtection.getPanel(window);
  let viewShownPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  );
  panel.showLocationSelector(true, locationButton);
  await viewShownPromise;
  await locationsList.updateComplete;

  Assert.equal(
    itemFor("CA").getAttribute("tabindex"),
    "0",
    "reopening the subview should restore the tab stop to the selected location"
  );
  Assert.equal(
    itemFor(RECOMMENDED_CODE).getAttribute("tabindex"),
    "-1",
    "the previously focused item should no longer be the tab stop"
  );
  Assert.equal(
    document.activeElement,
    itemFor("CA"),
    "reopening via keyboard should focus the selected location again"
  );

  await closePanel();
  cleanupService();

  let { locationsView: mouseLocationsView } = await openLocationsSubview(
    { location: "CA" },
    false
  );

  Assert.notEqual(
    document.activeElement,
    mouseLocationsView.querySelector("#location-option-CA"),
    "mouse-activated open should not focus the selected location"
  );

  await closePanel();
  cleanupService();
});
