/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";
const TRACKING_PAGE_WITH_META_REFRESH =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPageWithMetaRefresh.html";
const BENIGN_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/benignPage.html";
const NOTIFICATION_VALUE = "reduced-protection-reload";

let pbWindow;

function getNotification(browser) {
  let notificationBox = browser.getTabBrowser()?.getNotificationBox(browser);
  return notificationBox?.getNotificationWithValue(NOTIFICATION_VALUE);
}

function waitForContentBlockingEvent(aBrowser) {
  return new Promise(resolve => {
    let listener = {
      onContentBlockingEvent(webProgress, request, event) {
        if (event & Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT) {
          aBrowser.removeProgressListener(listener);
          resolve();
        }
      },
    };
    aBrowser.addProgressListener(listener);
  });
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.trackingprotection.enabled", true],
      ["privacy.reducePageProtection.infobar.enabled.pbmode", true],
      ["dom.security.https_first_pbm", false],
    ],
  });

  await UrlClassifierTestUtils.addTestTrackers();

  pbWindow = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  registerCleanupFunction(async () => {
    await BrowserTestUtils.closeWindow(pbWindow);
    UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

// The infobar must NOT appear on initial navigation (first load).
add_task(async function test_no_infobar_on_first_load() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  // Wait a tick for any async notification logic.
  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar on first load of a page with trackers");

  BrowserTestUtils.removeTab(tab);
});

// The infobar appears after reloading a page that had blocked trackers.
add_task(async function test_infobar_on_reload() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  info("Reloading to trigger the infobar");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for reduced protection notification to appear"
  );
  ok(
    notification,
    "Infobar appears after reload with previously blocked trackers"
  );

  BrowserTestUtils.removeTab(tab);
});

// The infobar does NOT appear when navigating to a new page
add_task(async function test_no_infobar_on_navigation() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  info("Navigating to a different page (not reloading)");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, BENIGN_PAGE);
  await loadedPromise;

  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar on address-bar navigation");

  BrowserTestUtils.removeTab(tab);
});

// Clicking the "Reload with reduced protection" button disables TP via
// ScopedPrefs and reloads the page so trackers are no longer blocked.
add_task(async function test_button_disables_tp_and_reloads() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  info("Reloading to trigger the infobar");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for reduced protection notification"
  );
  ok(notification, "Infobar appeared");

  info("Clicking the reload button on the infobar");
  let reloadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let button = notification.buttonContainer.querySelector("button:last-child");
  button.click();
  await reloadPromise;

  // After the button-triggered reload, TP should be disabled for this site
  // in this tab. The shield should not be active.
  let isActive = pbWindow.gProtectionsHandler.iconBox.hasAttribute("active");
  ok(!isActive, "Shield is not active: TP disabled via ScopedPrefs");

  BrowserTestUtils.removeTab(tab);
});

// After the infobar is shown for a host, it should not reappear for that host
// on subsequent reloads (dismissed-hosts tracking).
add_task(async function test_no_reappear_after_dismiss() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  info("Reloading to trigger the infobar for the first time");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for the first notification"
  );

  info("Reloading again - infobar should not reappear for this host");
  blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;
  await blockingPromise;

  // Give async showNotification time to run (or not).
  await TestUtils.waitForTick();

  ok(
    !getNotification(tab.linkedBrowser),
    "Infobar does not reappear for the same host"
  );

  BrowserTestUtils.removeTab(tab);
});

// Clicking the button disables all tracker-blocking scoped prefs, not just
// tracking protection.
add_task(async function test_button_disables_all_tracker_prefs() {
  const TRACKER_PREFS = [
    [
      "privacy.trackingprotection.enabled",
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_ENABLED,
    ],
    [
      "privacy.trackingprotection.cryptomining.enabled",
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CRYPTOMINING_ENABLED,
    ],
    [
      "privacy.trackingprotection.fingerprinting.enabled",
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_FINGERPRINTING_ENABLED,
    ],
    [
      "privacy.trackingprotection.socialtracking.enabled",
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_SOCIALTRACKING_ENABLED,
    ],
    [
      "privacy.trackingprotection.emailtracking.enabled",
      Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_EMAILTRACKING_ENABLED,
    ],
  ];

  // Content-classifier scoped prefs the button should also clear.
  const CONTENT_SCOPED_PREFS = [
    Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED,
    Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_CRYPTOMINING_ENABLED,
    Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_FINGERPRINTING_ENABLED,
    Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_SOCIALTRACKING_ENABLED,
    Ci.nsIScopedPrefs.PRIVACY_TRACKINGPROTECTION_CONTENT_EMAILTRACKING_ENABLED,
  ];

  // Enable the content protection engines pref to test the button clearing.
  await SpecialPowers.pushPrefEnv({
    set: [
      ...TRACKER_PREFS.map(([pref]) => [pref, true]),
      ["privacy.trackingprotection.content.protection.enabled", true],
      [
        "privacy.trackingprotection.content.protection.engines.pbmode",
        "trackers,cryptominers,fingerprinters,social-trackers,email-trackers",
      ],
    ],
  });

  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for reduced protection notification"
  );
  ok(notification, "Infobar appeared");

  let scopedPrefs = tab.linkedBrowser.browsingContext.scopedPrefs;
  let bc = tab.linkedBrowser.browsingContext;

  // The content scoped prefs have no override yet, so they resolve to their
  // enabled fallback. This makes the button clearing them to false observable.
  for (const scopedPref of CONTENT_SCOPED_PREFS) {
    Assert.equal(
      scopedPrefs.getBoolPrefScoped(scopedPref, bc),
      true,
      `Content scoped pref ${scopedPref} is enabled before button click`
    );
  }

  let reloadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  notification.buttonContainer.querySelector("button:last-child").click();
  await reloadPromise;

  for (const [, scopedPref] of TRACKER_PREFS) {
    Assert.equal(
      scopedPrefs.getBoolPrefScoped(scopedPref, bc),
      false,
      `Scoped pref ${scopedPref} is disabled after button click`
    );
  }
  for (const scopedPref of CONTENT_SCOPED_PREFS) {
    Assert.equal(
      scopedPrefs.getBoolPrefScoped(scopedPref, bc),
      false,
      `Content scoped pref ${scopedPref} is disabled after button click`
    );
  }

  BrowserTestUtils.removeTab(tab);
});

// The infobar should not appear in a normal (non-private) browsing
add_task(async function test_no_infobar_in_normal_browsing() {
  let blockingPromise = waitForContentBlockingEvent(gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar in a normal browsing window");

  BrowserTestUtils.removeTab(tab);
});

// The infobar should not appear when the feature pref is disabled.
add_task(async function test_pref_gating() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.reducePageProtection.infobar.enabled.pbmode", false]],
  });

  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar when the feature pref is disabled");

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// A JS reload (location.reload()) should not trigger the infobar even after
// the page had blocked trackers, as it lacks user activation.
add_task(async function test_no_infobar_on_js_reload() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  info("Triggering JS navigation to the same URL");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.location.reload();
  });
  await loadedPromise;

  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar after a JS navigation to the same URL");

  BrowserTestUtils.removeTab(tab);
});

// Clicking a link dismisses the infobar.
add_task(async function test_link_navigation_dismisses_infobar() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for notification to appear"
  );

  info("Clicking the in-page link to navigate away");
  loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#navigate-away",
    {},
    tab.linkedBrowser
  );

  await TestUtils.waitForCondition(
    () => !getNotification(tab.linkedBrowser),
    "Waiting for infobar to be dismissed on link click"
  );

  await loadedPromise;
  ok(!getNotification(tab.linkedBrowser), "Infobar dismissed after link click");

  BrowserTestUtils.removeTab(tab);
});

// Meta refresh (LOAD_CMD_NORMAL) should not trigger the infobar even after the
// page had blocked trackers.
add_task(async function test_no_infobar_on_meta_refresh() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE_WITH_META_REFRESH
  );
  await blockingPromise;

  info("Waiting for meta refresh to reload the page");
  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await loadedPromise;

  await TestUtils.waitForTick();

  let notification = getNotification(tab.linkedBrowser);
  ok(!notification, "No infobar after a meta refresh");

  BrowserTestUtils.removeTab(tab);
});

// Telemetry: banner_shown counter increments when the infobar appears.
add_task(async function test_telemetry_banner_shown() {
  Services.fog.testResetFOG();

  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for notification to appear"
  );

  Assert.equal(
    Glean.privacyReducedPageProtection.bannerShown.testGetValue(),
    1,
    "banner_shown counter incremented once when the infobar appeared"
  );

  BrowserTestUtils.removeTab(tab);
});

// Telemetry: reload_clicked counter increments when the reload button is clicked.
add_task(async function test_telemetry_reload_clicked() {
  Services.fog.testResetFOG();

  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for notification to appear"
  );

  let reloadPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  notification.buttonContainer.querySelector("button:last-child").click();
  await reloadPromise;

  Assert.equal(
    Glean.privacyReducedPageProtection.reloadClicked.testGetValue(),
    1,
    "reload_clicked counter incremented once when the reload button was clicked"
  );

  BrowserTestUtils.removeTab(tab);
});

// Telemetry: disable_clicked counter increments when "Don't show again" is clicked.
add_task(async function test_telemetry_disable_clicked() {
  Services.fog.testResetFOG();

  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for notification to appear"
  );

  notification.buttonContainer.querySelector("button:first-child").click();

  Assert.equal(
    Glean.privacyReducedPageProtection.disableClicked.testGetValue(),
    1,
    "disable_clicked counter incremented once when 'Don't show again' was clicked"
  );

  BrowserTestUtils.removeTab(tab);
  // Restore pref value before test ran due to flipped when clicking on disable
  Services.prefs.setBoolPref(
    "privacy.reducePageProtection.infobar.enabled.pbmode",
    true
  );
});

// Clicking "Don't show again" sets the feature pref to false.
add_task(async function test_dont_show_again_disables_pref() {
  let blockingPromise = waitForContentBlockingEvent(pbWindow.gBrowser);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    pbWindow.gBrowser,
    TRACKING_PAGE
  );
  await blockingPromise;

  let loadedPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  pbWindow.gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  await loadedPromise;

  let notification = await TestUtils.waitForCondition(
    () => getNotification(tab.linkedBrowser),
    "Waiting for reduced protection notification"
  );
  ok(notification, "Infobar appeared");

  let dontShowAgainButton =
    notification.buttonContainer.querySelector("button:first-child");
  ok(dontShowAgainButton, "Don't show again button is present");
  is(
    dontShowAgainButton.dataset.l10nId,
    "reduced-protection-infobar-never-show-button",
    "First button is 'Don't show again'"
  );

  dontShowAgainButton.click();

  ok(
    !Services.prefs.getBoolPref(
      "privacy.reducePageProtection.infobar.enabled.pbmode"
    ),
    "Feature pref is disabled after clicking Don't show again"
  );

  BrowserTestUtils.removeTab(tab);
  // Restore pref value before test ran due to flipped when clicking on disable
  Services.prefs.setBoolPref(
    "privacy.reducePageProtection.infobar.enabled.pbmode",
    true
  );
});
