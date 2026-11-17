/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const RELEASE_NOTES_URL =
  "https://example.com/document-builder.sjs?html=release%20notes";

function windowCount() {
  return Array.from(Services.ww.getWindowEnumerator()).length;
}

// Clicking a text link in the About dialog while no browser window is tracked
// should open the link in exactly one new browser window.
add_task(async function test_about_dialog_link_opens_one_window() {
  await SpecialPowers.pushPrefEnv({
    set: [["app.releaseNotesURL.aboutDialog", RELEASE_NOTES_URL]],
  });

  let aboutDialogPromise = BrowserTestUtils.domWindowOpenedAndLoaded(
    null,
    win =>
      win.document.documentURI == "chrome://browser/content/aboutDialog.xhtml"
  );
  openAboutDialog();
  let aboutDialog = await aboutDialogPromise;

  let initialTabCount = gBrowser.tabs.length;
  let initialWindowCount = windowCount();
  let newWindowPromise = TestUtils.topicObserved("browser-window-before-show");

  BrowserWindowTracker.untrackForTestsOnly(window);
  try {
    aboutDialog.document.getElementById("releasenotes").click();
  } finally {
    BrowserWindowTracker.track(window);
  }
  Assert.equal(
    windowCount(),
    initialWindowCount + 1,
    "Only one new window should have opened"
  );

  let [newWindow] = await newWindowPromise;
  await BrowserTestUtils.waitForNewTab(
    newWindow.gBrowser,
    RELEASE_NOTES_URL,
    true
  );

  Assert.equal(
    gBrowser.tabs.length,
    initialTabCount,
    "The link should not also open in the existing browser window"
  );

  // Closing the window while its initial blank tab is still open can wait a
  // long time for session store data from that tab.
  BrowserTestUtils.removeTab(newWindow.gBrowser.tabs[0]);
  await BrowserTestUtils.closeWindow(newWindow);
  await BrowserTestUtils.closeWindow(aboutDialog);
});
