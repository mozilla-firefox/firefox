/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SCOPED_PREF = Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_ENABLED;
const SCOPED_CONTENT_PREF =
  Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.trackingprotection.enabled", true]],
  });
  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

async function loadTrackerInTab(browser) {
  let blockedPromise = waitForContentBlockingEvent(window).then(
    () => "blocked"
  );
  let loadPromise = SpecialPowers.spawn(
    browser,
    [TEST_3RD_PARTY_DOMAIN_TP],
    async function (url) {
      let iframe = content.document.createElement("iframe");
      let loaded = ContentTaskUtils.waitForEvent(iframe, "load").then(
        () => "loaded"
      );
      iframe.src = url;
      content.document.body.appendChild(iframe);
      return loaded;
    }
  );
  return Promise.race([loadPromise, blockedPromise]);
}

add_task(async function test_scoped_pref_disable() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_TOP_PAGE
  );
  let browser = tab.linkedBrowser;
  let result = await loadTrackerInTab(browser);
  is(result, "blocked", "Tracker is blocked when TP is enabled");

  let bc = tab.linkedBrowser.browsingContext;
  // Cover both engines, as ReducedProtectionNotification does.
  bc.scopedPrefs.setBoolPrefScoped(SCOPED_PREF, bc, false);
  bc.scopedPrefs.setBoolPrefScoped(SCOPED_CONTENT_PREF, bc, false);
  // Start a new document so the first load's blocking event can't resolve the
  // next check.
  await BrowserTestUtils.reloadTab(tab);

  result = await loadTrackerInTab(browser);
  is(
    result,
    "loaded",
    "Tracker is allowed after disabling TP for this bc via ScopedPrefs"
  );

  await BrowserTestUtils.removeTab(tab);
});
