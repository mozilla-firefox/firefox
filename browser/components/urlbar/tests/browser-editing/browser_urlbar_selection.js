/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const exampleSearch = "f oo  bar";
const exampleUrl = "https://example.com/1";

function click(target) {
  let promise = BrowserTestUtils.waitForEvent(target, "click");
  EventUtils.synthesizeMouseAtCenter(target, {}, target.documentGlobal);
  return promise;
}

function openContextMenu(target) {
  let popupShownPromise = BrowserTestUtils.waitForPopupEvent(
    window.EditContextMenu.popup,
    "shown"
  );

  EventUtils.synthesizeMouseAtCenter(
    target,
    {
      type: "contextmenu",
      button: 2,
    },
    target.documentGlobal
  );
  return popupShownPromise;
}

function drag(target, fromX, fromY, toX, toY) {
  let promise = BrowserTestUtils.waitForEvent(target, "mouseup");
  EventUtils.synthesizeMouse(
    target,
    fromX,
    fromY,
    { type: "mousemove" },
    target.documentGlobal
  );
  EventUtils.synthesizeMouse(
    target,
    fromX,
    fromY,
    { type: "mousedown" },
    target.documentGlobal
  );
  EventUtils.synthesizeMouse(
    target,
    toX,
    toY,
    { type: "mousemove" },
    target.documentGlobal
  );
  EventUtils.synthesizeMouse(
    target,
    toX,
    toY,
    { type: "mouseup" },
    target.documentGlobal
  );
  return promise;
}

function resetPrimarySelection(val = "") {
  if (
    Services.clipboard.isClipboardTypeSupported(
      Services.clipboard.kSelectionClipboard
    )
  ) {
    // Reset the clipboard.
    clipboardHelper.copyStringToClipboard(
      val,
      Services.clipboard.kSelectionClipboard
    );
  }
}

function checkPrimarySelection(expectedVal = "") {
  if (
    Services.clipboard.isClipboardTypeSupported(
      Services.clipboard.kSelectionClipboard
    )
  ) {
    let primaryAsText = SpecialPowers.getClipboardData(
      "text/plain",
      SpecialPowers.Ci.nsIClipboard.kSelectionClipboard
    );
    Assert.equal(primaryAsText, expectedVal);
  }
}

add_setup(async function () {
  // On macOS, we must "warm up" the Urlbar to get the first test to pass.
  gURLBar.value = "";
  await click(gURLBar.inputField);
  gURLBar.blur();
});

add_task(async function leftClickSelectsAll() {
  resetPrimarySelection();
  gURLBar.value = exampleSearch;
  await click(gURLBar.inputField);
  Assert.equal(
    gURLBar.selectionStart,
    0,
    "The entire search term should be selected."
  );
  Assert.equal(
    gURLBar.selectionEnd,
    exampleSearch.length,
    "The entire search term should be selected."
  );
  gURLBar.blur();
  checkPrimarySelection();
});

add_task(async function leftClickSelectsUrl() {
  resetPrimarySelection();
  gURLBar.value = exampleUrl;
  await click(gURLBar.inputField);
  Assert.equal(gURLBar.selectionStart, 0, "The entire url should be selected.");
  Assert.equal(
    gURLBar.selectionEnd,
    UrlbarTestUtils.trimURL(exampleUrl).length,
    "The entire url should be selected."
  );
  gURLBar.blur();
  checkPrimarySelection();
});

add_task(async function rightClickSelectsAll() {
  gURLBar.inputField.focus();
  gURLBar.value = exampleUrl;

  // Remove the selection so the focus() call above doesn't influence the test.
  gURLBar.selectionStart = gURLBar.selectionEnd = 0;

  resetPrimarySelection();

  await openContextMenu(gURLBar.inputField);

  Assert.equal(gURLBar.selectionStart, 0, "The entire URL should be selected.");
  Assert.equal(
    gURLBar.selectionEnd,
    UrlbarTestUtils.trimURL(exampleUrl).length,
    "The entire URL should be selected."
  );

  checkPrimarySelection();

  let contextMenu = window.EditContextMenu.popup;

  // While the context menu is open, test the "Select All" button.
  let contextMenuItem = contextMenu.querySelector(
    "#edit-contextmenu-select-all"
  );
  Assert.ok(
    contextMenuItem,
    "The context menu should have the select all menu item."
  );

  let controller =
    document.commandDispatcher.getControllerForCommand("cmd_selectAll");
  let enabled = controller.isCommandEnabled("cmd_selectAll");
  Assert.ok(enabled, "The context menu select all item should be enabled.");

  // Activating a menuitem hides the menu and fires its command afterwards, so
  // the click alone doesn't mean cmd_selectAll has run. The item forwards to the
  // <command> element, which is where the event fires.
  let commandPromise = BrowserTestUtils.waitForEvent(
    document.getElementById("cmd_selectAll"),
    "command"
  );
  await click(contextMenuItem);
  await commandPromise;
  Assert.equal(
    gURLBar.selectionStart,
    0,
    "The entire URL should be selected after clicking selectAll button."
  );
  Assert.equal(
    gURLBar.selectionEnd,
    UrlbarTestUtils.trimURL(exampleUrl).length,
    "The entire URL should be selected after clicking selectAll button."
  );

  window.EditContextMenu.popup.hidePopup();
  gURLBar.blur();
  checkPrimarySelection(gURLBar.untrimmedValue);
  await SpecialPowers.popPrefEnv();
});

add_task(async function contextMenuDoesNotCancelSelection() {
  gURLBar.inputField.focus();
  gURLBar.value = exampleUrl;

  gURLBar.selectionStart = 3;
  gURLBar.selectionEnd = 7;

  resetPrimarySelection();

  await openContextMenu(gURLBar.inputField);

  Assert.equal(
    gURLBar.selectionStart,
    3,
    "The selection should not have changed."
  );
  Assert.equal(
    gURLBar.selectionEnd,
    7,
    "The selection should not have changed."
  );

  window.EditContextMenu.popup.hidePopup();
  gURLBar.blur();
  checkPrimarySelection();
});

add_task(async function dragSelect() {
  resetPrimarySelection();
  gURLBar.value = exampleSearch.repeat(10);
  // Drags from an artibrary offset of 30 to test for bug 1562145: that the
  // selection does not start at the beginning.
  await drag(gURLBar.inputField, 30, 10, 60, 10);
  Assert.greater(
    gURLBar.selectionStart,
    0,
    "Selection should not start at the beginning of the string."
  );

  let selectedVal = gURLBar.value.substring(
    gURLBar.selectionStart,
    gURLBar.selectionEnd
  );
  gURLBar.blur();
  checkPrimarySelection(selectedVal);
});

/**
 * Testing for bug 1571018: that the entire Urlbar isn't selected when the
 * Urlbar is dragged following a selectsAll event then a blur.
 */
add_task(async function dragAfterSelectAll() {
  resetPrimarySelection();
  gURLBar.value = exampleSearch.repeat(10);
  await click(gURLBar.inputField);
  Assert.equal(
    gURLBar.selectionStart,
    0,
    "The entire search term should be selected."
  );
  Assert.equal(
    gURLBar.selectionEnd,
    exampleSearch.repeat(10).length,
    "The entire search term should be selected."
  );

  gURLBar.blur();
  checkPrimarySelection();

  // The offset of 30 is arbitrary.
  await drag(gURLBar.inputField, 30, 10, 60, 10);

  Assert.notEqual(
    gURLBar.selectionStart,
    0,
    "Only part of the search term should be selected."
  );
  Assert.notEqual(
    gURLBar.selectionEnd,
    exampleSearch.repeat(10).length,
    "Only part of the search term should be selected."
  );

  checkPrimarySelection(
    gURLBar.value.substring(gURLBar.selectionStart, gURLBar.selectionEnd)
  );
});

/**
 * Testing for bug 1571018: that the entire Urlbar is selected when the Urlbar
 * is refocused following a partial text selection then a blur.
 */
add_task(async function selectAllAfterDrag() {
  gURLBar.value = exampleSearch;

  gURLBar.selectionStart = 3;
  gURLBar.selectionEnd = 7;

  gURLBar.blur();

  await click(gURLBar.inputField);

  Assert.equal(
    gURLBar.selectionStart,
    0,
    "The entire search term should be selected."
  );
  Assert.equal(
    gURLBar.selectionEnd,
    exampleSearch.length,
    "The entire search term should be selected."
  );

  gURLBar.blur();
});
