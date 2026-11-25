/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Test the URLBar value and user's interaction at startup and opening new
// window with system principal.

ChromeUtils.defineESModuleGetters(this, {
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
});

const TEST_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

const STARTUP_URL = `${TEST_ROOT}startup-delayed-redirect.sjs`;
const REDIRECT_URL = "https://example.com/";
const TYPED_TEXT = "user typed";

add_task(async function () {
  info("Setup homepage");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.startup.homepage", STARTUP_URL],
      ["browser.startup.page", 1],
    ],
  });

  info("Open a new window with system principal via keyboard shortcut");
  const onNewWindow = BrowserTestUtils.domWindowOpenedAndLoaded();
  EventUtils.synthesizeKey("n", { accelKey: true });
  const win = await onNewWindow;
  const browser = win.gBrowser.selectedBrowser;

  info("Wait until the loading URL appears in the URL bar");
  await TestUtils.waitForCondition(
    () => browser.browsingContext.nonWebControlledLoadingURI,
    "nonWebControlledLoadingURI should be set"
  );
  Assert.equal(
    win.gURLBar.value,
    UrlbarTestUtils.trimURL(STARTUP_URL),
    "URL bar should show the startup URL"
  );

  info("Simulate the user typing in the URL bar");
  win.gURLBar.focus();
  win.gURLBar.select();
  EventUtils.sendString(TYPED_TEXT, win);

  Assert.ok(
    win.gBrowser.userTypedValue,
    "userTypedValue should be set after typing"
  );
  Assert.equal(win.gURLBar.value, TYPED_TEXT, "URL bar should show typed text");

  info("Wait for the redirect to complete");
  await BrowserTestUtils.browserLoaded(browser, false, REDIRECT_URL);
  Assert.equal(
    win.gURLBar.value,
    TYPED_TEXT,
    "URL bar should preserve typed text after redirect"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});
