/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests unit conversion on browser.
 */

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.unitConversion.enabled", true]],
  });

  registerCleanupFunction(function () {
    SpecialPowers.clipboardCopyString("");
  });
});

add_task(async function test_selectByMouse() {
  const win = await BrowserTestUtils.openNewBrowserWindow();

  // Clear clipboard content.
  SpecialPowers.clipboardCopyString("");

  const row = await doUnitConversion(win);

  info("Check if the result is copied to clipboard when selecting by mouse");
  let engaged = UrlbarTestUtils.promiseProviderEngagement(win);
  EventUtils.synthesizeMouseAtCenter(row, {}, win);
  await engaged;
  assertClipboard();

  await UrlbarTestUtils.promisePopupClose(win);
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_selectByKey() {
  const win = await BrowserTestUtils.openNewBrowserWindow();

  // Clear clipboard content.
  SpecialPowers.clipboardCopyString("");

  await doUnitConversion(win);

  // As gURLBar might lost focus,
  // give focus again in order to enable key event on the result.
  win.gURLBar.focus();

  info("Check if the result is copied to clipboard when selecting by key");
  let engaged = UrlbarTestUtils.promiseProviderEngagement(win);
  EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
  EventUtils.synthesizeKey("KEY_Enter", {}, win);
  await engaged;
  assertClipboard();

  await UrlbarTestUtils.promisePopupClose(win);
  await BrowserTestUtils.closeWindow(win);
});

function assertClipboard() {
  Assert.equal(
    SpecialPowers.getClipboardData("text/plain"),
    "100 cm",
    "The result of conversion is copied to clipboard"
  );
}

async function doUnitConversion(win) {
  info("Do unit conversion then wait the result");

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: "1m to cm",
    waitForFocus: SimpleTest.waitForFocus,
  });

  const row = await UrlbarTestUtils.waitForAutocompleteResultAt(win, 1);

  Assert.ok(row.querySelector(".urlbarView-favicon"), "The icon is displayed");
  Assert.equal(
    row.querySelector(".urlbarView-dynamic-unitConversion-output").textContent,
    "100 cm",
    "The unit is converted"
  );

  return row;
}
