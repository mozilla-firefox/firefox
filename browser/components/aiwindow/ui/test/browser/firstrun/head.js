/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

/**
 * Opens firstrun.html in a new foreground tab of the test window.
 *
 * @returns {Promise<MozTabbrowserTab>}
 */
function openFirstrunPage() {
  return BrowserTestUtils.openNewForegroundTab(gBrowser, FIRSTRUN_URL);
}

/**
 * Sets the home region, restored after the file's tests.
 *
 * @param {string} region
 */
function withHomeRegion(region) {
  const originalRegion = Region.home;
  Region._setHomeRegion(region, false);
  registerCleanupFunction(() => {
    if (originalRegion) {
      Region._setHomeRegion(originalRegion, false);
    } else {
      Services.prefs.clearUserPref("browser.search.region");
    }
  });
}
