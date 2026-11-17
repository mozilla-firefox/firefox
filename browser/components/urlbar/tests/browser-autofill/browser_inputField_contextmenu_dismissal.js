/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests Dismiss / Forget this site on the urlbar input field context menu for
 * adaptive autofill heuristic results.
 */

"use strict";

const ADAPTIVE_URL = "https://example.com/adaptive-page";
const ORIGIN_URL = "https://example.com/";
const SEARCH_STRING = "exa";
const ADAPTIVE_INPUT = "exa";

const DISMISS_ID = "urlbar-input-dismiss-autofill";
const FORGET_ID = "urlbar-input-remove-from-history";

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.autoFill", true],
      ["browser.urlbar.autoFill.adaptiveHistory.enabled", true],
      ["browser.urlbar.autoFill.adaptiveHistory.minCharsThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.useCountThreshold", 0],
      ["browser.urlbar.autoFill.adaptiveHistory.urlMinPicks", 1],
      ["browser.urlbar.suggest.quicksuggest.sponsored", false],
      ["browser.urlbar.suggest.quicksuggest.nonsponsored", false],
    ],
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
  });
});

async function addAdaptiveHistoryEntry(url, input, useCount = 3) {
  await PlacesTestUtils.addVisits({
    uri: url,
    transition: PlacesUtils.history.TRANSITIONS.TYPED,
  });
  for (let i = 0; i < useCount; i++) {
    await UrlbarUtils.addToInputHistory(url, input);
  }
}

async function showAutofillFor(win, value) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value,
    fireInputEvent: true,
  });
}

async function readContextMenuItems(win) {
  let cxmenu = win.EditContextMenu.popup;
  let openPromise = BrowserTestUtils.waitForEvent(cxmenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    win.gURLBar.inputField,
    { type: "contextmenu", button: 2 },
    win
  );
  await openPromise;
  await new Promise(win.requestAnimationFrame);

  let dismiss = cxmenu.querySelector(`[anonid="${DISMISS_ID}"]`);
  let forget = cxmenu.querySelector(`[anonid="${FORGET_ID}"]`);
  let state = {
    dismissHidden: !dismiss || dismiss.hidden,
    forgetHidden: !forget || forget.hidden,
  };

  let closePromise = BrowserTestUtils.waitForEvent(cxmenu, "popuphidden");
  cxmenu.hidePopup();
  await closePromise;
  return state;
}

async function clickContextMenuItem(win, id) {
  await UrlbarTestUtils.activateContextMenuItem(win, id);
}

add_task(async function dismiss_via_context_menu_blocks_adaptive_url() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ADAPTIVE_URL, ADAPTIVE_INPUT);

  await showAutofillFor(window, SEARCH_STRING);
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.autofill?.type,
    "adaptive_url",
    "Pre-condition: heuristic is adaptive_url autofill"
  );

  await clickContextMenuItem(window, DISMISS_ID);

  let originId = await PlacesTestUtils.getDatabaseValue(
    "moz_places",
    "origin_id",
    { url: ADAPTIVE_URL }
  );
  await TestUtils.waitForCondition(async () => {
    let val = await PlacesTestUtils.getDatabaseValue(
      "moz_origins",
      "block_pages_until_ms",
      { id: originId }
    );
    return val > Date.now();
  }, "block_pages_until_ms should be set after context-menu dismiss");

  let blockUntilMs = await PlacesTestUtils.getDatabaseValue(
    "moz_origins",
    "block_until_ms",
    { id: originId }
  );
  Assert.equal(
    blockUntilMs,
    null,
    "Origin-level block should not be set when dismissing a page URL"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function dismiss_via_context_menu_blocks_adaptive_origin() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ORIGIN_URL, ADAPTIVE_INPUT);

  await showAutofillFor(window, SEARCH_STRING);
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.autofill?.type,
    "adaptive_origin",
    "Pre-condition: heuristic is adaptive_origin autofill"
  );

  await clickContextMenuItem(window, DISMISS_ID);

  let originId = await PlacesTestUtils.getDatabaseValue(
    "moz_places",
    "origin_id",
    { url: ORIGIN_URL }
  );
  await TestUtils.waitForCondition(async () => {
    let val = await PlacesTestUtils.getDatabaseValue(
      "moz_origins",
      "block_until_ms",
      { id: originId }
    );
    return val > Date.now();
  }, "block_until_ms should be set after context-menu dismiss of an origin");

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function dismiss_via_context_menu_blocks_origin_autofill() {
  await PlacesUtils.history.clear();
  // Plain origin autofill: typed visit, no adaptive entry. Frecency must be
  // recalculated for the URL to be eligible for autofill.
  await PlacesTestUtils.addVisits({
    uri: ORIGIN_URL,
    transition: PlacesUtils.history.TRANSITIONS.TYPED,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  await showAutofillFor(window, SEARCH_STRING);
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.ok(result.heuristic, "Result should be the heuristic");
  Assert.ok(result.autofill, "Result should be autofill");
  Assert.notEqual(
    result.autofill?.type,
    "adaptive_origin",
    "Pre-condition: heuristic is plain (non-adaptive) origin autofill"
  );

  await clickContextMenuItem(window, DISMISS_ID);

  let originId = await PlacesTestUtils.getDatabaseValue(
    "moz_places",
    "origin_id",
    { url: ORIGIN_URL }
  );
  await TestUtils.waitForCondition(async () => {
    let val = await PlacesTestUtils.getDatabaseValue(
      "moz_origins",
      "block_until_ms",
      { id: originId }
    );
    return val > Date.now();
  }, "block_until_ms should be set after context-menu dismiss of plain origin autofill");

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function forget_via_context_menu_removes_from_history() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ADAPTIVE_URL, ADAPTIVE_INPUT);

  await showAutofillFor(window, SEARCH_STRING);
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.autofill?.type,
    "adaptive_url",
    "Pre-condition: heuristic is adaptive_url autofill"
  );

  await clickContextMenuItem(window, FORGET_ID);

  await TestUtils.waitForCondition(async () => {
    let page = await PlacesUtils.history.fetch(ADAPTIVE_URL);
    return !page;
  }, "URL should be removed from history after Forget");

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function dismiss_item_hidden_in_private_window() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ADAPTIVE_URL, ADAPTIVE_INPUT);

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await showAutofillFor(privateWin, SEARCH_STRING);

  let { dismissHidden, forgetHidden } = await readContextMenuItems(privateWin);
  Assert.ok(dismissHidden, "Dismiss should be hidden in a private window");
  // Forget (Remove from history) still applies in private windows because
  // the underlying browsing history is shared with non-private profiles.
  Assert.ok(
    !forgetHidden,
    "Forget should remain visible for non-origin URLs in a private window"
  );

  await UrlbarTestUtils.promisePopupClose(privateWin);
  await BrowserTestUtils.closeWindow(privateWin);
  await PlacesUtils.history.clear();
});

add_task(async function forget_item_hidden_for_origin_autofill() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ORIGIN_URL, ADAPTIVE_INPUT);

  await showAutofillFor(window, SEARCH_STRING);
  let { result } = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    result.autofill?.type,
    "adaptive_origin",
    "Pre-condition: origin-typed autofill"
  );

  let { dismissHidden, forgetHidden } = await readContextMenuItems(window);
  Assert.ok(!dismissHidden, "Dismiss should be visible for origin autofill");
  Assert.ok(forgetHidden, "Forget should be hidden for origin autofill");

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
});

add_task(async function context_menu_items_hidden_without_autofill_result() {
  await PlacesUtils.history.clear();

  await showAutofillFor(window, "no-match-string-xyz");
  let { dismissHidden, forgetHidden } = await readContextMenuItems(window);
  Assert.ok(
    dismissHidden,
    "Dismiss should be hidden when no autofill heuristic is showing"
  );
  Assert.ok(
    forgetHidden,
    "Forget should be hidden when no autofill heuristic is showing"
  );

  await UrlbarTestUtils.promisePopupClose(window);
});

add_task(async function dismiss_clears_backspace_entry() {
  await PlacesUtils.history.clear();
  await addAdaptiveHistoryEntry(ADAPTIVE_URL, ADAPTIVE_INPUT);

  // Pre-populate a backspace entry for the page scope.
  UrlbarUtils._backspaceBlocks.clear();
  UrlbarUtils._backspaceBlocks.set("page:example.com", {
    level: "url",
    count: 1,
    blockedAt: Date.now(),
  });

  await showAutofillFor(window, SEARCH_STRING);
  await clickContextMenuItem(window, DISMISS_ID);

  await TestUtils.waitForCondition(
    () => !UrlbarUtils._backspaceBlocks.has("page:example.com"),
    "Context-menu dismiss should clear the page-scope backspace entry"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await PlacesUtils.history.clear();
  UrlbarUtils._backspaceBlocks.clear();
});
