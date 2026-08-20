/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test TrustPanel Breach Icon logic.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  BrowserTestUtils: "resource://testing-common/BrowserTestUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/protectionsUI/trackingPage.html";

const TEST_BREACH = {
  // Make sure the breach is a recent one, since breaches older than a year are not taken into account:
  AddedDate: Temporal.Now.plainDateTimeISO().toString(),
  BreachDate: Temporal.Now.plainDateISO().toString(),
  Domain: "example.org",
  Name: "TestBreach",
  PwnCount: 42,
  DataClasses: ["Email addresses", "Passwords"],
  _status: "synced",
  id: "047940fe-d2fd-4314-b636-b4a952ee1234",
  last_modified: "1541615610052",
  schema: "1541615609018",
};

add_setup(async function setup() {
  const db = RemoteSettings("fxmonitor-breaches").db;
  await db.clear();
  await db.create(TEST_BREACH, { useRecordId: true });
  await db.importChanges({}, Date.now());

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.trustPanel.featureGate", true],
      ["browser.urlbar.trustPanel.breachAlerts", true],
      [
        "urlclassifier.features.cryptomining.blacklistHosts",
        "cryptomining.example.com",
      ],
      [
        "urlclassifier.features.cryptomining.annotate.blacklistHosts",
        "cryptomining.example.com",
      ],
    ],
  });

  // Wait a tick to ensure any initial about:blank background fetches
  // complete BEFORE we reset the cache.
  /* eslint-disable mozilla/no-arbitrary-setTimeout */
  await new Promise(r => setTimeout(r, 500));

  registerCleanupFunction(async () => {
    // The trust icon lives in the (chrome) urlbar, so its animations can outlive
    // the tab that triggered them. Let them finish so vsync is disabled before
    // the harness's end-of-file teardown check.
    await Promise.all(
      trustIconContainer()
        .getAnimations({ subtree: true })
        .map(anim => anim.finished.catch(() => {}))
    );
    await PlacesUtils.history.clear();
    Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
    await db.clear();
    await db.importChanges({}, Date.now());
  });
});

function trustIconContainer() {
  return document.getElementById("trust-icon-container");
}

async function waitForTrustIconClass(className, message) {
  await TestUtils.waitForCondition(
    () => trustIconContainer()?.classList.contains(className),
    message,
    100,
    100
  );
}

async function waitForTrustIconWithoutClass(className, message) {
  await TestUtils.waitForCondition(
    () => !trustIconContainer()?.classList.contains(className),
    message,
    100,
    100
  );
}

add_task(async function test_breached_urlbar_icon_animation_logic() {
  let tab1;
  let tab2;
  let tab3;

  try {
    info("1. Visit breached site in first tab");

    tab1 = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: "https://example.org",
      waitForLoad: true,
    });

    await waitForTrustIconClass("breached", "Waiting for breached class");

    Assert.ok(
      trustIconContainer().classList.contains("breached"),
      "The trust icon has the breached class"
    );

    await waitForTrustIconClass(
      "breach-animating",
      "Waiting for breach animation"
    );

    Assert.ok(
      trustIconContainer().classList.contains("breach-animating"),
      "Should have breach-animating class on first visit"
    );

    info("2. Open a second, safe tab");

    tab2 = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: "about:blank",
      waitForLoad: true,
    });

    await waitForTrustIconWithoutClass(
      "breached",
      "Waiting for no breached class"
    );

    Assert.ok(
      !trustIconContainer().classList.contains("breached"),
      "Icon should not be breached on blank page"
    );

    info("3. Switch back to the first tab");

    await BrowserTestUtils.switchTab(gBrowser, tab1);

    await waitForTrustIconClass(
      "breached",
      "Waiting for breached after tab switch"
    );

    Assert.ok(
      trustIconContainer().classList.contains("breached"),
      "The trust icon should be breached again after switching back"
    );

    await waitForTrustIconWithoutClass(
      "breach-animating",
      "Waiting for no animation on tab switch"
    );

    Assert.ok(
      !trustIconContainer().classList.contains("breach-animating"),
      "Should NOT animate on tab switch back"
    );

    info("4. Visit a DIFFERENT breached site");

    const db = RemoteSettings("fxmonitor-breaches").db;
    await db.create(
      {
        ...TEST_BREACH,
        id: "different-guid",
        Domain: "example.com",
      },
      { useRecordId: true }
    );
    await db.importChanges({}, Date.now());

    tab3 = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: "https://example.com",
      waitForLoad: true,
    });

    await waitForTrustIconClass(
      "breached",
      "Waiting for breached on second domain"
    );

    Assert.ok(
      trustIconContainer().classList.contains("breached"),
      "The trust icon should be breached for the second site"
    );

    await waitForTrustIconClass("breach-animating", "Waiting for re-animation");

    Assert.ok(
      trustIconContainer().classList.contains("breach-animating"),
      "Should re-animate on new breached domain"
    );
  } finally {
    for (let tab of [tab3, tab2, tab1]) {
      if (tab && !tab.closing) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
  }
});

add_task(async function test_no_trackers_blocked() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  Assert.ok(
    !trustIconContainer().classList.contains("has-blocked-trackers"),
    "has-blocked-trackers class not present when no trackers are blocked"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_trackerCountShown_pref() {
  Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
  await PlacesUtils.history.clear();

  Assert.ok(
    !UrlbarPrefs.get("trackerCountShown"),
    "trackerCountShown pref starts as false"
  );

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    // See trackingAPI.js - this postMessage causes it to inject an iframe with
    // one of the blocked tracking hosts:
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers class"
  );

  Assert.ok(
    UrlbarPrefs.get("trackerCountShown"),
    "trackerCountShown pref is set to true after first tracker count display"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_blocked_trackers_updates_toolbar() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    // See trackingAPI.js - this postMessage causes it to inject an iframe with
    // one of the blocked tracking hosts:
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "has-blocked-trackers class should appear after a cryptominer is blocked"
  );

  Assert.equal(
    document.getElementById("trust-icon-tracker-count-shortform").textContent,
    "1",
    "Shortform label shows the blocked tracker count"
  );

  const iconContainer = document.getElementById("trust-icon-container");
  document.l10n.translateElements([iconContainer]);
  await TestUtils.waitForCondition(
    () =>
      iconContainer.getAttribute("aria-label") ===
      "View site information (1 tracker blocked)",
    "The accessibility tree exposes the number of trackers blocked"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_multiple_blockers_count() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "urlclassifier.features.fingerprinting.blacklistHosts",
        "fingerprinting.example.com",
      ],
      [
        "urlclassifier.features.fingerprinting.annotate.blacklistHosts",
        "fingerprinting.example.com",
      ],
    ],
  });
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    // See trackingAPI.js - this postMessage causes it to inject an iframe with
    // one of the blocked tracking hosts:
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers after cryptomining"
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.postMessage("fingerprinting", "*");
  });

  await TestUtils.waitForCondition(
    () =>
      document.getElementById("trust-icon-tracker-count-shortform")
        .textContent == "2",
    "Shortform count should update to 2 after a second blocker fires"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_blocked_trackers_cleared_on_navigation() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    // See trackingAPI.js - this postMessage causes it to inject an iframe with
    // one of the blocked tracking hosts:
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers class to appear"
  );

  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com"
  );
  await loaded;

  await waitForTrustIconWithoutClass(
    "has-blocked-trackers",
    "has-blocked-trackers class should be removed after navigating to a new page"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_tracker_count_cache_cleared_on_navigation() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers class"
  );

  Assert.equal(
    document.getElementById("trust-icon-tracker-count-shortform").textContent,
    "1",
    "Shortform shows 1 blocked tracker before navigation"
  );

  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com"
  );
  await loaded;

  await waitForTrustIconWithoutClass(
    "has-blocked-trackers",
    "has-blocked-trackers should be absent after navigating to a clean page"
  );

  // The count text updates once the scanning hold resolves (it's hidden behind
  // the scanning shield until then), so wait for it rather than reading eagerly.
  await TestUtils.waitForCondition(
    () =>
      document.getElementById("trust-icon-tracker-count-shortform")
        .textContent === "0",
    "Shortform count becomes 0 after navigating to a clean page"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_first_visit_class_on_first_visit() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    // See trackingAPI.js - this postMessage causes it to inject an iframe with
    // one of the blocked tracking hosts:
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers class"
  );

  Assert.ok(
    trustIconContainer().classList.contains("first-visit"),
    "first-visit class is present on a site's first visit"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_tracker_count_hidden_when_feature_gate_disabled() {
  await ExperimentAPI.ready();
  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "urlbar",
    value: { trackerCountFeatureGate: false },
  });

  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      // See trackingAPI.js - this postMessage causes it to inject an iframe with
      // one of the blocked tracking hosts:
      content.postMessage("cryptomining", "*");
    });

    // Unfortunately #updateToolbarTrackerCount is asynchronous and, when the
    // tracker count is disabled, doesn't result in observable effects that we
    // can await:
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(r => setTimeout(r, 500));

    Assert.ok(
      !trustIconContainer().classList.contains("has-blocked-trackers"),
      "has-blocked-trackers class should not appear when trackerCountFeatureGate is false"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await doCleanup();
  }
});

add_task(async function test_tracker_count_hidden_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.trackerCount.enabled", false]],
  });

  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    // Unfortunately #updateToolbarTrackerCount is asynchronous and, when the
    // tracker count is disabled, doesn't result in observable effects that we
    // can await:
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(r => setTimeout(r, 500));

    Assert.ok(
      !trustIconContainer().classList.contains("has-blocked-trackers"),
      "has-blocked-trackers class should not appear when trackerCount.enabled is false"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_scanning_shield_at_navigation_start() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await waitForTrustIconClass("secure", "Waiting for secure icon");

  // Simulate navigation start: the icon must drop to the neutral scanning
  // shield immediately and must NOT keep showing the secure check-mark before
  // the blocker check has run.
  gTrustPanelHandler.resetIconForNavigation();

  Assert.ok(
    trustIconContainer().classList.contains("scanning"),
    "Icon shows the scanning shield at navigation start"
  );
  Assert.ok(
    !trustIconContainer().classList.contains("secure"),
    "Icon does not show the secure check-mark before the blocker check"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_scanning_resolves_to_secure() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await waitForTrustIconClass(
    "secure",
    "Scanning resolves to secure once the blocker check completes"
  );
  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Scanning shield is not left showing after the blocker check"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_reset_icon_same_vs_cross_host() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await waitForTrustIconClass("secure", "Waiting for resolved secure icon");

  // A navigation to a subdomain of the same site (different host, same base
  // domain) preserves the resolved icon: no re-scan.
  gTrustPanelHandler.resetIconForNavigation(
    Services.io.newURI("https://www.example.com/other")
  );
  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Same-site navigation (subdomain) preserves the icon, no re-scan"
  );
  Assert.ok(
    trustIconContainer().classList.contains("secure"),
    "Same-site navigation keeps the resolved secure state"
  );

  // A navigation to a different site (different base domain) drops back to
  // the scanning shield.
  gTrustPanelHandler.resetIconForNavigation(
    Services.io.newURI("https://example.org/")
  );
  Assert.ok(
    trustIconContainer().classList.contains("scanning"),
    "Cross-site navigation drops to the scanning shield"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_same_site_navigation_preserves_state() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await waitForTrustIconClass("secure", "Initial page resolves to secure");

  // Navigate within the same site; the icon must stay resolved (no scanning,
  // no re-animation) rather than re-scanning.
  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com/?same-site"
  );
  await loaded;

  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Same-site navigation does not re-enter the scanning state"
  );
  Assert.ok(
    trustIconContainer().classList.contains("secure"),
    "Same-site navigation preserves the resolved secure icon"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_same_site_navigation_preserves_count_and_highlight() {
    await PlacesUtils.history.clear();

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      // See trackingAPI.js - this postMessage causes it to inject an iframe with
      // one of the blocked tracking hosts:
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked"
    );
    Assert.equal(
      document.getElementById("trust-icon-tracker-count-shortform").textContent,
      "1",
      "Count shows 1 before the same-site navigation"
    );

    // Navigate within the same site (same host, new query). The new page blocks
    // no tracker, so its recomputed count starts at 0 -- but the highlight pill
    // and its count must be preserved rather than flickering off / dropping to
    // 0 between pages of the same site.
    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      TRACKING_PAGE + "?same-site"
    );
    await loaded;

    await waitForTrustIconClass(
      "same-site-nav",
      "Same-site navigation marks the icon so the reveal stays suppressed"
    );
    Assert.ok(
      trustIconContainer().classList.contains("has-blocked-trackers"),
      "Highlight pill is preserved across a same-site navigation"
    );
    Assert.ok(
      !trustIconContainer().classList.contains("first-visit"),
      "Same-site navigation is not treated as a first visit"
    );
    Assert.equal(
      document.getElementById("trust-icon-tracker-count-shortform").textContent,
      "1",
      "Count is preserved (not dropped to 0) across the same-site navigation"
    );

    await BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_scanning_does_not_mask_breach() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.org",
    waitForLoad: true,
  });

  // A breached page is a definitive state and must show through, rather than
  // being replaced by the scanning shield.
  await waitForTrustIconClass("breached", "Waiting for breached class");
  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Breach shows through instead of the scanning shield"
  );

  // Let the breach pulse animation finish before teardown, otherwise it can
  // leave the compositor's vsync enabled when the test file ends.
  await Promise.all(
    trustIconContainer()
      .getAnimations({ subtree: true })
      .map(anim => anim.finished.catch(() => {}))
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_no_scanning_when_feature_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.trackerCount.enabled", false]],
  });
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await waitForTrustIconClass("secure", "Waiting for secure icon");

  // With the tracker feature off there is no behavior change: navigation
  // start leaves the secure icon untouched.
  gTrustPanelHandler.resetIconForNavigation();

  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Scanning shield does not appear when the tracker feature is disabled"
  );
  Assert.ok(
    trustIconContainer().classList.contains("secure"),
    "Secure icon is unchanged when the tracker feature is disabled"
  );

  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_no_first_visit_class_on_return_visit() {
  await PlacesUtils.history.clear();
  Services.prefs.setBoolPref("browser.urlbar.trackerCountShown", true);

  // Add a visit to the tracking host from more than 20 seconds ago so that
  // #markFirstVisit treats this as a return visit.
  await PlacesTestUtils.addVisits({
    // eslint-disable-next-line sdl/no-insecure-url
    uri: "http://tracking.example.org/",
    visitDate: new Date(Date.now() - 60 * 1000),
  });

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      // See trackingAPI.js - this postMessage causes it to inject an iframe with
      // one of the blocked tracking hosts:
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers class"
    );

    Assert.ok(
      !trustIconContainer().classList.contains("first-visit"),
      "first-visit class is not present on a return visit"
    );
  } finally {
    Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
    await BrowserTestUtils.removeTab(tab);
  }
});
add_task(
  async function test_first_visit_class_on_return_visit_when_tracker_count_never_shown() {
    Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
    await PlacesUtils.history.clear();

    // Add a visit older than 20 seconds so #markFirstVisit would treat this as a
    // return visit based on history alone.
    await PlacesTestUtils.addVisits({
      // eslint-disable-next-line sdl/no-insecure-url
      uri: "http://tracking.example.org/",
      visitDate: new Date(Date.now() - 60 * 1000),
    });

    Assert.ok(
      !UrlbarPrefs.get("trackerCountShown"),
      "trackerCountShown pref starts as false"
    );

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    try {
      await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
        content.postMessage("cryptomining", "*");
      });

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers class"
      );

      Assert.ok(
        trustIconContainer().classList.contains("first-visit"),
        "first-visit class is present on a return visit when the tracker count has never been shown"
      );
    } finally {
      Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
      await BrowserTestUtils.removeTab(tab);
    }
  }
);

add_task(async function test_tab_switch_preserves_resolved_secure_icon() {
  await PlacesUtils.history.clear();

  const noTrackerTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });
  await waitForTrustIconClass("secure", "No-tracker tab resolves to secure");

  const otherTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:blank",
    waitForLoad: true,
  });

  // Returning must keep the check-mark, not re-enter scanning. Assert
  // synchronously: scanning self-resolves in seconds, so a polled wait would
  // miss the flash of the shield the user actually sees.
  await BrowserTestUtils.switchTab(gBrowser, noTrackerTab);
  Assert.ok(
    trustIconContainer().classList.contains("secure"),
    "Tab switch preserves the check-mark"
  );
  Assert.ok(
    !trustIconContainer().classList.contains("scanning"),
    "Tab switch does not drop to the scanning shield"
  );

  await BrowserTestUtils.removeTab(otherTab);
  await BrowserTestUtils.removeTab(noTrackerTab);
});

add_task(async function test_no_first_visit_class_on_refresh() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers on first load"
  );

  Assert.ok(
    trustIconContainer().classList.contains("first-visit"),
    "first-visit class is present on first load"
  );

  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  tab.linkedBrowser.reload();
  await loaded;

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers after reload"
  );

  Assert.ok(
    !trustIconContainer().classList.contains("first-visit"),
    "first-visit class is not re-applied after a reload"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_no_first_visit_class_on_tab_switch_back() {
  await PlacesUtils.history.clear();

  const trackingTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await SpecialPowers.spawn(trackingTab.linkedBrowser, [], () => {
    content.postMessage("cryptomining", "*");
  });

  await waitForTrustIconClass(
    "has-blocked-trackers",
    "Waiting for has-blocked-trackers on first load"
  );

  Assert.ok(
    trustIconContainer().classList.contains("first-visit"),
    "first-visit class is present on first load"
  );

  const otherTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:blank",
    waitForLoad: true,
  });

  try {
    await BrowserTestUtils.switchTab(gBrowser, trackingTab);

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers to reappear after switching back"
    );

    Assert.ok(
      !trustIconContainer().classList.contains("first-visit"),
      "first-visit class is not re-applied after switching back to the tab"
    );
  } finally {
    await BrowserTestUtils.removeTab(trackingTab);
    await BrowserTestUtils.removeTab(otherTab);
  }
});

add_task(async function test_tracker_count_shown_glean_event_first_visit() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    Assert.equal(
      Glean.trustpanel.trackerCountShown.testGetValue(),
      null,
      "No trackerCountShown event recorded yet"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers class on the trust icon container"
    );

    await Services.fog.testFlushAllChildren();

    const events = Glean.trustpanel.trackerCountShown.testGetValue();
    Assert.ok(
      Array.isArray(events) && events.length === 1,
      "The trackerCountShown Glean event was recorded exactly once"
    );
    Assert.equal(
      events[0].extra.first_visit,
      "true",
      "first_visit is true on a first visit to the site"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_tracker_count_shown_glean_event_return_visit() {
  await PlacesUtils.history.clear();

  // Add a past visit so that #markFirstVisit treats this as a return visit
  // (it only counts visits older than 20 seconds).
  await PlacesTestUtils.addVisits({
    // eslint-disable-next-line sdl/no-insecure-url
    uri: "http://tracking.example.org/",
    visitDate: new Date(Date.now() - 60 * 1000),
  });

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await Services.fog.testFlushAllChildren();
    Services.fog.testResetFOG();

    Assert.equal(
      Glean.trustpanel.trackerCountShown.testGetValue(),
      null,
      "No trackerCountShown event recorded yet"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers class on the trust icon container"
    );

    await Services.fog.testFlushAllChildren();

    const events = Glean.trustpanel.trackerCountShown.testGetValue();
    Assert.ok(
      Array.isArray(events) && events.length === 1,
      "The trackerCountShown Glean event was recorded exactly once"
    );
    Assert.equal(
      events[0].extra.first_visit,
      "false",
      "first_visit is false on a return visit to the site"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(
  async function test_tracker_count_shown_not_recorded_again_on_tab_switch() {
    await PlacesUtils.history.clear();

    const trackerTab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    const otherTab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    try {
      await Services.fog.testFlushAllChildren();
      Services.fog.testResetFOG();

      // Trigger the tracker count on the first tab.
      await BrowserTestUtils.switchTab(gBrowser, trackerTab);
      await SpecialPowers.spawn(trackerTab.linkedBrowser, [], () => {
        content.postMessage("cryptomining", "*");
      });
      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers class"
      );

      await Services.fog.testFlushAllChildren();
      Assert.equal(
        Glean.trustpanel.trackerCountShown.testGetValue().length,
        1,
        "Event recorded once after first appearance of the tracker count"
      );

      // Switch to the other tab (also a tracking page) and trigger its trackers.
      await BrowserTestUtils.switchTab(gBrowser, otherTab);
      await SpecialPowers.spawn(otherTab.linkedBrowser, [], () => {
        content.postMessage("cryptomining", "*");
      });
      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers class on the other tab"
      );

      await Services.fog.testFlushAllChildren();
      Assert.equal(
        Glean.trustpanel.trackerCountShown.testGetValue().length,
        2,
        "Event also recorded for the other tab"
      );

      // Switch back to the first tab.
      await BrowserTestUtils.switchTab(gBrowser, trackerTab);
      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers class to reappear after tab switch"
      );

      await Services.fog.testFlushAllChildren();
      Assert.equal(
        Glean.trustpanel.trackerCountShown.testGetValue().length,
        2,
        "No new event recorded after switching back to the same tab"
      );
    } finally {
      await BrowserTestUtils.removeTab(trackerTab);
      await BrowserTestUtils.removeTab(otherTab);
    }
  }
);
