/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the urlbar of a window opened with a URL argument keeps
// pageproxystate invalid while on the initial about:blank.
// Otherwise, we would show "Not Secure" while the page loads
// because the initial about:blank has a null principal.

const SLOW_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "slow-page.sjs";

add_task(async function () {
  let args = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  args.data = SLOW_PAGE;
  let win = await BrowserTestUtils.openNewBrowserWindow({ args });

  await TestUtils.waitForCondition(
    () => win.gURLBar.untrimmedValue == SLOW_PAGE
  );
  Assert.equal(win.gBrowser.currentURI.spec, "about:blank", "Still loading");
  Assert.equal(
    win.gURLBar.getAttribute("pageproxystate"),
    "invalid",
    "pageproxystate is invalid on initial about:blank"
  );

  await BrowserTestUtils.browserLoaded(
    win.gBrowser.selectedBrowser,
    false,
    SLOW_PAGE
  );
  Assert.equal(
    win.gURLBar.getAttribute("pageproxystate"),
    "valid",
    "pageproxystate is valid after page loads"
  );

  await BrowserTestUtils.closeWindow(win);
});
