/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the macOS menu bar shows the hidden window's menus, and not only
// the application menu, while the Picture-in-Picture player is focused.
add_task(async function test_pip_player_shows_hidden_window_menu_bar() {
  let appShell = Cc["@mozilla.org/widget/appshell/mac;1"].getService(
    Ci.nsICocoaAppShellTestUtils
  );

  let hiddenDoc = Services.appShell.hiddenDOMWindow.document;
  let expectedTitles = Array.from(
    hiddenDoc.querySelectorAll("menubar > menu:not([hidden], [collapsed])"),
    menu => menu.getAttribute("label")
  );
  Assert.greater(
    expectedTitles.length,
    0,
    "The hidden window has menus in its menu bar."
  );

  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    let pipWin = await triggerPictureInPicture(browser, "with-controls");
    ok(pipWin, "Got Picture-in-Picture window.");
    await SimpleTest.promiseFocus(pipWin);

    await TestUtils.waitForCondition(() => {
      let titles = appShell.getMainMenuItemTitles();
      return expectedTitles.every(title => titles.includes(title));
    }, "The menu bar shows the hidden window's menus.");

    await BrowserTestUtils.closeWindow(pipWin);
  });
});
