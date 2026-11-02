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
  UrlClassifierTestUtils:
    "resource://testing-common/UrlClassifierTestUtils.sys.mjs",
});

const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

// Same trackingPage.html, served from a different eTLD+1 than TRACKING_PAGE
// (and than the cryptomining/fingerprinting tracker hosts, which are under
// example.com) so that navigations between the two count as cross-site.
// another-tracking.example.net is a pre-registered test-only domain used
// elsewhere in the tree for this same purpose (see server-locations.txt).
const OTHER_HOST_TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://another-tracking.example.net/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";

// Cross-site to TRACKING_PAGE. Responds with 204 No Content, or redirects when
// given a ?redirect=<url> query.
const NO_CONTENT_URL =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://another-tracking.example.net/browser/browser/components/urlbar/tests/browser-trustPanel/navigation.sjs";

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
  await RemoteSettings("fxmonitor-breaches").emit("sync", {
    data: { current: [TEST_BREACH] },
  });

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
      // trackingPage.html contains a static <iframe src="http://trackertest.org/">.
      // The tests in this file check for other types of tracking, but its
      // presence can interfere with tests that check for a specific number of
      // blocked trackers. Thus, we exclude them from being counted as trackers:
      ["urlclassifier.trackingSkipURLs", "*://trackertest.org/*"],
      ["urlclassifier.trackingAnnotationSkipURLs", "*://trackertest.org/*"],
      // Some platforms reduce motion by default, which skips the tracker
      // count animations.
      ["ui.prefersReducedMotion", 0],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();

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
    await UrlClassifierTestUtils.cleanupTestTrackers();
    await PlacesUtils.history.clear();
    Services.prefs.clearUserPref("browser.urlbar.trackerCountShown");
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

/**
 * The tracker-count prefs only gate whether the trust icon reacts to blocked
 * trackers, not whether the platform records them. Waiting for the log to be
 * populated guarantees that any code under test observing
 * #computeTrackerCount() runs after the block has happened.
 *
 * @param {Browser} browser The browser whose content-blocking log to poll.
 */
async function waitForContentBlockingLog(browser) {
  await TestUtils.waitForCondition(
    () => Object.keys(JSON.parse(browser.getContentBlockingLog())).length,
    "Waiting for the content-blocking log to record the blocked tracker"
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

    await RemoteSettings("fxmonitor-breaches").emit("sync", {
      data: {
        current: [
          TEST_BREACH,
          {
            ...TEST_BREACH,
            id: "different-guid",
            Domain: "example.com",
          },
        ],
      },
    });

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

// The breach list stores domains in their ASCII (punycode) form, so an IDN
// site has to be matched against its ASCII host rather than the host we
// display in the panel.
add_task(async function test_breached_idn_site() {
  const IDN_ASCII_HOST = "xn--hxajbheg2az3al.xn--jxalpdlp";
  const IDN_URL = `https://${IDN_ASCII_HOST}/`;

  Assert.notEqual(
    Services.io.newURI(IDN_URL).displayHost,
    IDN_ASCII_HOST,
    "The test site is displayed as an IDN, not as punycode"
  );

  await RemoteSettings("fxmonitor-breaches").emit("sync", {
    data: {
      current: [
        TEST_BREACH,
        {
          ...TEST_BREACH,
          id: "idn-guid",
          Domain: IDN_ASCII_HOST,
        },
      ],
    },
  });

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: IDN_URL,
    waitForLoad: true,
  });

  try {
    await waitForTrustIconClass(
      "breached",
      "Waiting for breached class on the IDN site"
    );

    await Promise.all(
      trustIconContainer()
        .getAnimations({ subtree: true })
        .map(anim => anim.finished.catch(() => {}))
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
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
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
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

add_task(async function test_entry_page_class_on_entry_page() {
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
    trustIconContainer().classList.contains("entry-page"),
    "entry-page class is present on a site's first-visited page"
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

    await waitForContentBlockingLog(tab.linkedBrowser);

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

    await waitForContentBlockingLog(tab.linkedBrowser);

    Assert.ok(
      !trustIconContainer().classList.contains("has-blocked-trackers"),
      "has-blocked-trackers class should not appear when trackerCount.enabled is false"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

// Regression test for bug 2070204: when the tracker count feature is disabled,
// switching away from and back to a tab that has blocked trackers must not
// reveal (and animate) the tracker-count pill. On tab switch, updateIdentity
// runs #updateUrlbarIcon while the content-blocking log is already populated,
// which previously added the has-blocked-trackers class regardless of the pref.
add_task(async function test_no_tracker_count_on_tab_switch_when_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.trackerCount.enabled", false]],
  });

  await PlacesUtils.history.clear();

  const trackingTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(trackingTab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    // Ensure the tracker blocking event is recorded:
    await waitForContentBlockingLog(trackingTab.linkedBrowser);

    const otherTab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: "about:blank",
      waitForLoad: true,
    });

    await BrowserTestUtils.switchTab(gBrowser, trackingTab);

    Assert.ok(
      !trustIconContainer().classList.contains("has-blocked-trackers"),
      "has-blocked-trackers class should not appear after switching back when the tracker count is disabled"
    );

    await BrowserTestUtils.removeTab(otherTab);
  } finally {
    await BrowserTestUtils.removeTab(trackingTab);
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

add_task(async function test_same_site_navigation_cumulative_tracker_count() {
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

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      1,
      "One tracker is blocked on the first page"
    );

    // Removing has-blocked-trackers or adding entry-page would replay the
    // count pill's animation.
    const classChanges = [];
    const observer = new MutationObserver(() => {
      classChanges.push([...trustIconContainer().classList]);
    });
    observer.observe(trustIconContainer(), {
      attributes: true,
      attributeFilter: ["class"],
    });

    // Navigate within the same site (same host, new query) and block an
    // additional tracker of a different type.
    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      TRACKING_PAGE + "?same-site-page-2"
    );
    await loaded;

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("fingerprinting", "*");
    });

    await TestUtils.waitForCondition(
      () =>
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent == "2",
      "Waiting for the cumulative count to reach 2 after a fingerprinter is blocked on the second page"
    );
    observer.disconnect();

    Assert.ok(
      classChanges.every(classes => classes.includes("has-blocked-trackers")),
      "The count pill stays shown throughout the same-site navigation"
    );
    Assert.ok(
      !trustIconContainer().classList.contains("entry-page"),
      "The entry-page animation is not replayed on a same-site page"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      2,
      "The tracker count is cumulative across same-site navigations"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_different_host_navigation_resets_tracker_count() {
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

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      1,
      "One tracker is blocked on the first host"
    );

    // Navigate to a different host that also blocks a tracker.
    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      OTHER_HOST_TRACKING_PAGE
    );
    await loaded;

    await waitForTrustIconWithoutClass(
      "has-blocked-trackers",
      "Pill is absent right after navigating to a different host"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("fingerprinting", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a fingerprinter is blocked on the new host"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      1,
      "The tracker count resets when navigating to a different host"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(
  async function test_same_site_navigation_no_new_trackers_keeps_count_visible() {
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

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers after a cryptominer is blocked"
      );

      Assert.equal(
        Number(
          document.getElementById("trust-icon-tracker-count-shortform")
            .textContent
        ),
        1,
        "One tracker is blocked on the first page"
      );

      // Navigate within the same site without blocking any new trackers.
      const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      BrowserTestUtils.startLoadingURIString(
        tab.linkedBrowser,
        TRACKING_PAGE + "?same-site-no-new-trackers"
      );
      await loaded;

      Assert.ok(
        trustIconContainer().classList.contains("has-blocked-trackers"),
        "The tracker pill stays visible when the next page on the same site blocks no new trackers"
      );
      Assert.equal(
        Number(
          document.getElementById("trust-icon-tracker-count-shortform")
            .textContent
        ),
        1,
        "The tracker count is unchanged when no new trackers are blocked on the next same-site page"
      );
    } finally {
      await BrowserTestUtils.removeTab(tab);
    }
  }
);

add_task(
  async function test_new_tab_same_host_tracker_count_independent_of_other_tabs() {
    await PlacesUtils.history.clear();

    const tabA = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: TRACKING_PAGE,
      waitForLoad: true,
    });

    try {
      await SpecialPowers.spawn(tabA.linkedBrowser, [], () => {
        content.postMessage("cryptomining", "*");
      });

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers on the first page of tab A"
      );

      // Navigate within the same site on tab A, blocking an additional tracker.
      const loaded = BrowserTestUtils.browserLoaded(tabA.linkedBrowser);
      BrowserTestUtils.startLoadingURIString(
        tabA.linkedBrowser,
        TRACKING_PAGE + "?tab-a-page-2"
      );
      await loaded;

      await SpecialPowers.spawn(tabA.linkedBrowser, [], () => {
        content.postMessage("fingerprinting", "*");
      });

      await TestUtils.waitForCondition(
        () =>
          document.getElementById("trust-icon-tracker-count-shortform")
            .textContent == "2",
        "Waiting for tab A's cumulative count to reach 2 after a fingerprinter is blocked on the second page"
      );

      Assert.equal(
        Number(
          document.getElementById("trust-icon-tracker-count-shortform")
            .textContent
        ),
        2,
        "Tab A shows a cumulative count of 2 across its own same-site navigations"
      );

      const tabB = await BrowserTestUtils.openNewForegroundTab({
        gBrowser,
        opening: TRACKING_PAGE,
        waitForLoad: true,
      });

      try {
        await SpecialPowers.spawn(tabB.linkedBrowser, [], () => {
          content.postMessage("cryptomining", "*");
        });

        await waitForTrustIconClass(
          "has-blocked-trackers",
          "Waiting for has-blocked-trackers on tab B"
        );

        Assert.equal(
          Number(
            document.getElementById("trust-icon-tracker-count-shortform")
              .textContent
          ),
          1,
          "Tab B only counts its own blocked tracker, independent of tab A"
        );

        await BrowserTestUtils.switchTab(gBrowser, tabA);

        await waitForTrustIconClass(
          "has-blocked-trackers",
          "Waiting for has-blocked-trackers to reappear after switching back to tab A"
        );

        Assert.equal(
          Number(
            document.getElementById("trust-icon-tracker-count-shortform")
              .textContent
          ),
          2,
          "Switching back to tab A shows its cumulative count again"
        );
      } finally {
        await BrowserTestUtils.removeTab(tabB);
      }
    } finally {
      await BrowserTestUtils.removeTab(tabA);
    }
  }
);

add_task(async function test_reload_accumulates_tracker_count() {
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

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      1,
      "One tracker is blocked before the reload"
    );

    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    tab.linkedBrowser.reload();
    await loaded;

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await TestUtils.waitForCondition(
      () =>
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent == "2",
      "Waiting for the cumulative count to reach 2 after the reload blocks the tracker again"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      2,
      "The tracker count is cumulative across reloads"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_returning_to_previous_host_resets_tracker_count() {
  await PlacesUtils.history.clear();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
      content.postMessage("fingerprinting", "*");
    });

    await TestUtils.waitForCondition(
      () =>
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent == "2",
      "Waiting for the count to reach 2 on the first host"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      2,
      "Two trackers are blocked on the first host"
    );

    // Leave to a different host that blocks no trackers.
    let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      OTHER_HOST_TRACKING_PAGE
    );
    await loaded;

    await waitForTrustIconWithoutClass(
      "has-blocked-trackers",
      "Pill is absent on the other host, which blocks no trackers"
    );
    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      0,
      "The count is 0 on the other host"
    );

    // Return to the first host: its earlier count must not resurface.
    loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, TRACKING_PAGE);
    await loaded;

    await waitForTrustIconWithoutClass(
      "has-blocked-trackers",
      "Pill is absent after returning to the first host with no trackers blocked yet"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked on the revisited host"
    );

    Assert.equal(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      1,
      "The tracker count resets when returning to a previously visited host"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

function trackerCountShortform() {
  return document.getElementById("trust-icon-tracker-count-shortform")
    .textContent;
}

async function openTabWithBlockedCryptominer() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    content.postMessage("cryptomining", "*");
  });
  await TestUtils.waitForCondition(
    () => trackerCountShortform() == "1",
    "Waiting for the cryptominer to be counted"
  );
  return tab;
}

async function blockFingerprinterAndWaitForCount(browser, count) {
  await SpecialPowers.spawn(browser, [], () => {
    content.postMessage("fingerprinting", "*");
  });
  await TestUtils.waitForCondition(
    () => trackerCountShortform() == String(count),
    `Waiting for the tracker count to reach ${count}`
  );
}

add_task(async function test_cross_site_load_without_document_keeps_count() {
  await PlacesUtils.history.clear();
  const tab = await openTabWithBlockedCryptominer();

  try {
    // A 204 response starts a cross-site load that never replaces the page,
    // like a download or a stopped load.
    const stopped = BrowserTestUtils.browserStopped(
      tab.linkedBrowser,
      NO_CONTENT_URL,
      true
    );
    BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, NO_CONTENT_URL);
    await stopped;
    Assert.equal(
      tab.linkedBrowser.currentURI.spec,
      TRACKING_PAGE,
      "The page was not replaced"
    );

    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      TRACKING_PAGE + "?after-no-content"
    );
    await loaded;
    await blockFingerprinterAndWaitForCount(tab.linkedBrowser, 2);

    Assert.equal(
      trackerCountShortform(),
      "2",
      "The count is kept after a cross-site load that didn't load a page"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_same_site_link_through_cross_site_redirect() {
  await PlacesUtils.history.clear();
  const tab = await openTabWithBlockedCryptominer();

  try {
    const destination = TRACKING_PAGE + "?after-redirect";
    const loaded = BrowserTestUtils.browserLoaded(
      tab.linkedBrowser,
      false,
      destination
    );
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      `${NO_CONTENT_URL}?redirect=${encodeURIComponent(destination)}`
    );
    await loaded;
    await blockFingerprinterAndWaitForCount(tab.linkedBrowser, 2);

    Assert.equal(
      trackerCountShortform(),
      "2",
      "The count continues after redirecting through another site back to the same site"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_background_cross_site_navigation_resets_count() {
  await PlacesUtils.history.clear();
  const tabA = await openTabWithBlockedCryptominer();
  const tabB = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:blank",
  });

  try {
    const loaded = BrowserTestUtils.browserLoaded(tabA.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tabA.linkedBrowser,
      OTHER_HOST_TRACKING_PAGE
    );
    await loaded;
    await SpecialPowers.spawn(tabA.linkedBrowser, [], () => {
      content.postMessage("fingerprinting", "*");
    });
    await TestUtils.waitForCondition(
      () =>
        tabA.linkedBrowser
          .getContentBlockingLog()
          .includes("fingerprinting.example.com"),
      "Waiting for the fingerprinter to be blocked in the background tab"
    );

    await BrowserTestUtils.switchTab(gBrowser, tabA);
    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after switching to tab A"
    );

    Assert.equal(
      trackerCountShortform(),
      "1",
      "Only trackers from the site the tab navigated to in the background are counted"
    );
  } finally {
    await BrowserTestUtils.removeTab(tabB);
    await BrowserTestUtils.removeTab(tabA);
  }
});

add_task(async function test_trackers_blocked_after_load_are_counted() {
  await PlacesUtils.history.clear();
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "urlclassifier.features.cryptomining.blacklistHosts",
        "cryptomining.example.com,test1.example.com",
      ],
      [
        "urlclassifier.features.cryptomining.annotate.blacklistHosts",
        "cryptomining.example.com,test1.example.com",
      ],
    ],
  });
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  const tab = await openTabWithBlockedCryptominer();

  try {
    // A second cryptominer doesn't change the page's content-blocking state,
    // so it doesn't trigger a content-blocking event.
    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      const iframe = content.document.createElement("iframe");
      // eslint-disable-next-line sdl/no-insecure-url
      iframe.src = "http://test1.example.com/";
      content.document.body.appendChild(iframe);
    });
    await TestUtils.waitForCondition(
      () =>
        tab.linkedBrowser.getContentBlockingLog().includes("test1.example.com"),
      "Waiting for the second cryptominer to be blocked"
    );

    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      TRACKING_PAGE + "?after-late-tracker"
    );
    await loaded;
    await TestUtils.waitForCondition(
      () => trackerCountShortform() == "2",
      "Waiting for the count to include the tracker blocked after load"
    );

    Assert.equal(
      trackerCountShortform(),
      "2",
      "Trackers blocked after the previous page loaded are included"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Check that a tab's cumulative tracker count survives moving it to another
 * window, and keeps adding up on the same site afterwards.
 *
 * @param {Function} moveTab
 *   Moves the given tab to another window, and resolves to the window and
 *   the moved tab's browser as `{ win, browser }`.
 */
async function doTabMoveTest(moveTab) {
  await PlacesUtils.history.clear();
  const tab = await openTabWithBlockedCryptominer();

  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    TRACKING_PAGE + "?before-move"
  );
  await loaded;
  await blockFingerprinterAndWaitForCount(tab.linkedBrowser, 2);

  const { win, browser } = await moveTab(tab);
  try {
    const shortform = () =>
      win.document.getElementById("trust-icon-tracker-count-shortform")
        .textContent;
    await TestUtils.waitForCondition(
      () => shortform() == "2",
      "Waiting for the moved tab to show its cumulative count"
    );
    Assert.ok(true, "Moving the tab to another window keeps its count");

    loaded = BrowserTestUtils.browserLoaded(browser);
    BrowserTestUtils.startLoadingURIString(
      browser,
      TRACKING_PAGE + "?after-move"
    );
    await loaded;
    await SpecialPowers.spawn(browser, [], () => {
      content.postMessage("cryptomining", "*");
    });
    await TestUtils.waitForCondition(
      () => shortform() == "3",
      "Waiting for the count to keep adding up after the move"
    );
    Assert.ok(
      true,
      "The count keeps adding up on the same site after the move"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
}

add_task(async function test_tab_moved_to_new_window_keeps_count() {
  await doTabMoveTest(async tab => {
    const win = gBrowser.replaceTabWithWindow(tab);
    await TestUtils.topicObserved(
      "browser-delayed-startup-finished",
      subject => subject == win
    );
    return { win, browser: win.gBrowser.selectedBrowser };
  });
});

add_task(async function test_tab_moved_to_existing_window_keeps_count() {
  await doTabMoveTest(async tab => {
    const win = await BrowserTestUtils.openNewBrowserWindow();
    const movedTab = win.gBrowser.adoptTab(tab, { selectTab: true });
    return { win, browser: movedTab.linkedBrowser };
  });
});

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

add_task(async function test_no_entry_page_class_on_followup_navigations() {
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

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers class"
    );

    // Navigate within the same site (same host, new query).
    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      TRACKING_PAGE + "?same-site"
    );
    await loaded;

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Pill stays visible after same-site navigation with no new trackers"
    );

    await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers after a cryptominer is blocked on a new page"
    );

    Assert.ok(
      !trustIconContainer().classList.contains("entry-page"),
      "entry-page class is not present on followup navigations"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

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

add_task(async function test_no_entry_page_class_on_refresh() {
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
    trustIconContainer().classList.contains("entry-page"),
    "entry-page class is present on first-loaded page"
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
    !trustIconContainer().classList.contains("entry-page"),
    "entry-page class is not re-applied after a reload"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_no_entry_page_class_on_tab_switch_back() {
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
    trustIconContainer().classList.contains("entry-page"),
    "entry-page class is present on first-loaded page"
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
      !trustIconContainer().classList.contains("entry-page"),
      "entry-page class is not re-applied after switching back to the tab"
    );
  } finally {
    await BrowserTestUtils.removeTab(trackingTab);
    await BrowserTestUtils.removeTab(otherTab);
  }
});

add_task(async function test_tracker_count_shown_glean_event_entry_page() {
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
      events[0].extra.first_site_load_in_tab,
      "true",
      "first_site_load_in_tab is true on a first-visited page of the site"
    );
  } finally {
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(
  async function test_tracker_count_shown_glean_event_followup_navigation() {
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

      // Navigate within the same site (same host, new query).
      const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
      BrowserTestUtils.startLoadingURIString(
        tab.linkedBrowser,
        TRACKING_PAGE + "?same-site"
      );
      await loaded;

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Pill stays visible after same-site navigation with no new trackers"
      );

      await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
        content.postMessage("cryptomining", "*");
      });

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers after a cryptominer is blocked on a new page"
      );

      await Services.fog.testFlushAllChildren();

      const events = Glean.trustpanel.trackerCountShown.testGetValue();
      Assert.ok(
        Array.isArray(events) && events.length === 2,
        "The trackerCountShown Glean event was recorded exactly twice"
      );
      Assert.equal(
        events[0].extra.first_site_load_in_tab,
        "true",
        "first_site_load_in_tab is true on the first page on the site"
      );
      Assert.equal(
        events[1].extra.first_site_load_in_tab,
        "false",
        "first_site_load_in_tab is false on subsequent pages on the site"
      );
    } finally {
      await BrowserTestUtils.removeTab(tab);
    }
  }
);

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

add_task(
  // We update the tracker count in the address bar when
  // `onContentBlockingEvent` is called; however, that only gets called when a
  // particular category of trackers is first blocked (e.g. fingerprinters), not
  // for every blocked origin, leaving the count incomplete. To compensate,
  // we also update it in onNavigationComplete. This test verifies that.
  async function test_navigation_complete_refreshes_count() {
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

      await waitForTrustIconClass(
        "has-blocked-trackers",
        "Waiting for has-blocked-trackers"
      );

      const correctCount = document.getElementById(
        "trust-icon-tracker-count-shortform"
      ).textContent;
      Assert.greater(
        Number(correctCount),
        0,
        "Some trackers are blocked after postMessage"
      );

      // Fake a stale tracker count:
      document.getElementById(
        "trust-icon-tracker-count-shortform"
      ).textContent = "42";

      await gTrustPanelHandler.onNavigationComplete();

      Assert.equal(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent,
        correctCount,
        "onNavigationComplete corrects a stale toolbar count"
      );
      Assert.ok(
        trustIconContainer().classList.contains("has-blocked-trackers"),
        "has-blocked-trackers is still present after the correction"
      );
      Assert.ok(
        !trustIconContainer().classList.contains("scanning"),
        "scanning state is not re-entered after the correction"
      );
    } finally {
      await BrowserTestUtils.removeTab(tab);
    }
  }
);

add_task(async function test_tab_switch_back_shows_correct_count() {
  await PlacesUtils.history.clear();

  const trackingTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  try {
    await SpecialPowers.spawn(trackingTab.linkedBrowser, [], () => {
      content.postMessage("cryptomining", "*");
    });

    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for has-blocked-trackers on the tracking tab"
    );

    Assert.greater(
      Number(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent
      ),
      0,
      "Count is positive before switching away"
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
        "has-blocked-trackers reappears after switching back to the tracking tab"
      );

      Assert.greater(
        Number(
          document.getElementById("trust-icon-tracker-count-shortform")
            .textContent
        ),
        0,
        "Count is still positive after switching back to the tracking tab"
      );
    } finally {
      await BrowserTestUtils.removeTab(otherTab);
    }
  } finally {
    await BrowserTestUtils.removeTab(trackingTab);
  }
});

/**
 * Open a foreground tab whose content blocking log reports `count` blocked
 * fingerprinters, so the tracker count doesn't depend on loading trackers.
 *
 * @param {number} count
 * @param {string} [url]
 * @returns {Promise<MozTabbrowserTab>}
 */
async function openTabWithBlockedTrackers(count, url = "https://example.com/") {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: url,
    waitForLoad: true,
  });
  setBlockedTrackers(tab, count);
  return tab;
}

/**
 * Make the tab's content blocking log report `count` blocked fingerprinters.
 *
 * @param {MozTabbrowserTab} tab
 * @param {number} count
 */
function setBlockedTrackers(tab, count) {
  const log = {};
  for (let i = 0; i < count; i++) {
    log[`https://fingerprinter${i}.example.net`] = [
      [Ci.nsIWebProgressListener.STATE_BLOCKED_FINGERPRINTING_CONTENT, true, 1],
    ];
  }
  const serializedLog = JSON.stringify(log);
  tab.linkedBrowser.getContentBlockingLog = () => serializedLog;
  Object.defineProperty(
    tab.linkedBrowser.browsingContext.currentWindowGlobal,
    "contentBlockingLog",
    { get: () => serializedLog, configurable: true }
  );
}

function trackerCountTabSwitchAnimations() {
  const cssAnimations = (element, name) =>
    element
      .getAnimations()
      .filter(animation => animation.animationName == name);
  return {
    icon: cssAnimations(
      trustIconContainer(),
      "tracker-count-tab-switch-resize"
    ),
    count: cssAnimations(
      document.getElementById("trust-icon-tracker-count-shortform-wrapper"),
      "fade-in-tracker-count-tab-switch"
    ),
  };
}

function trackerCountTabSwitchFromWidth() {
  return parseFloat(
    trustIconContainer().style.getPropertyValue("--tracker-count-from-width")
  );
}

async function finishTrustIconAnimations() {
  await Promise.all(
    trustIconContainer()
      .getAnimations({ subtree: true })
      .map(animation => animation.finished.catch(() => {}))
  );
}

function shieldOnlyWidth() {
  return (
    document.getElementById("trust-icon").getBoundingClientRect().width +
    2 * parseFloat(getComputedStyle(trustIconContainer()).paddingInlineStart)
  );
}

add_task(async function test_tab_switch_resizes_tracker_count() {
  const smallTab = await openTabWithBlockedTrackers(4);
  const bigTab = await openTabWithBlockedTrackers(208);

  try {
    await BrowserTestUtils.switchTab(gBrowser, smallTab);
    await finishTrustIconAnimations();

    for (const [tab, count] of [
      [bigTab, "208"],
      [smallTab, "4"],
    ]) {
      const previousWidth = trustIconContainer().getBoundingClientRect().width;
      await BrowserTestUtils.switchTab(gBrowser, tab);

      Assert.equal(
        document.getElementById("trust-icon-tracker-count-shortform")
          .textContent,
        count,
        "The selected tab's count is shown"
      );
      const { icon, count: countAnimations } =
        trackerCountTabSwitchAnimations();
      Assert.equal(icon.length, 1, "The icon resizes");
      Assert.equal(countAnimations.length, 1, "The count fades in");
      Assert.less(
        Math.abs(trackerCountTabSwitchFromWidth() - previousWidth),
        0.5,
        "The icon resizes from its width in the previous tab"
      );
      const toWidth = parseFloat(
        trustIconContainer().style.getPropertyValue("--tracker-count-to-width")
      );
      for (const animation of [icon[0], countAnimations[0]]) {
        Assert.equal(
          animation.effect.getComputedTiming().duration,
          110,
          "Animation lasts 110ms"
        );
        Assert.equal(
          animation.effect.getKeyframes()[0].easing,
          "cubic-bezier(0.2, 0.8, 0.2, 1)",
          "Animation uses the tracker count easing"
        );
      }

      await finishTrustIconAnimations();
      Assert.less(
        Math.abs(toWidth - trustIconContainer().getBoundingClientRect().width),
        0.5,
        "The icon resizes to its width for the selected tab"
      );
      Assert.ok(
        !trustIconContainer().hasAttribute("tracker-count-tab-switch"),
        "The tab switch attribute is removed once the animation ends"
      );
      Assert.deepEqual(
        trustIconContainer()
          .getAnimations({ subtree: true })
          .filter(animation => animation.playState == "running")
          .map(animation => animation.animationName),
        [],
        "Nothing animates again once the tab switch animation ends"
      );
    }
  } finally {
    BrowserTestUtils.removeTab(smallTab);
    BrowserTestUtils.removeTab(bigTab);
  }
});

add_task(async function test_tab_switch_from_no_tracker_count() {
  const emptyTab = await openTabWithBlockedTrackers(0);
  const trackerTab = await openTabWithBlockedTrackers(12);

  try {
    await BrowserTestUtils.switchTab(gBrowser, emptyTab);
    await finishTrustIconAnimations();
    await BrowserTestUtils.switchTab(gBrowser, trackerTab);

    Assert.equal(
      trackerCountTabSwitchAnimations().icon.length,
      1,
      "The icon resizes"
    );
    Assert.less(
      Math.abs(trackerCountTabSwitchFromWidth() - shieldOnlyWidth()),
      0.5,
      "The icon grows from just the shield"
    );
    const toWidth = parseFloat(
      trustIconContainer().style.getPropertyValue("--tracker-count-to-width")
    );
    await finishTrustIconAnimations();
    Assert.less(
      Math.abs(toWidth - trustIconContainer().getBoundingClientRect().width),
      0.5,
      "The icon grows to its width for the selected tab"
    );
  } finally {
    BrowserTestUtils.removeTab(emptyTab);
    BrowserTestUtils.removeTab(trackerTab);
  }
});

add_task(async function test_tab_switch_from_hidden_tracker_count() {
  const firstTab = await openTabWithBlockedTrackers(4);
  const secondTab = await openTabWithBlockedTrackers(208);

  try {
    await BrowserTestUtils.switchTab(gBrowser, firstTab);
    await finishTrustIconAnimations();

    // The icon is hidden while the address bar is being edited.
    gURLBar.setPageProxyState("invalid");
    Assert.equal(
      trustIconContainer().getBoundingClientRect().width,
      0,
      "The icon is hidden"
    );
    await BrowserTestUtils.switchTab(gBrowser, secondTab);

    Assert.less(
      Math.abs(trackerCountTabSwitchFromWidth() - shieldOnlyWidth()),
      0.5,
      "The icon grows from just the shield, not from 0px"
    );
    await finishTrustIconAnimations();
  } finally {
    BrowserTestUtils.removeTab(firstTab);
    BrowserTestUtils.removeTab(secondTab);
  }
});

add_task(async function test_tab_switch_to_no_tracker_count() {
  const trackerTab = await openTabWithBlockedTrackers(12);
  const emptyTab = await openTabWithBlockedTrackers(0);

  try {
    await BrowserTestUtils.switchTab(gBrowser, trackerTab);
    await finishTrustIconAnimations();
    const previousWidth = trustIconContainer().getBoundingClientRect().width;
    await BrowserTestUtils.switchTab(gBrowser, emptyTab);

    const shrink = trustIconContainer()
      .getAnimations()
      .filter(
        animation =>
          animation.animationName == "tracker-count-tab-switch-shrink"
      );
    Assert.equal(shrink.length, 1, "The previous tab's pill shrinks");
    Assert.less(
      Math.abs(trackerCountTabSwitchFromWidth() - previousWidth),
      0.5,
      "The pill shrinks from its width in the previous tab"
    );
    Assert.equal(
      shrink[0].effect.getComputedTiming().duration,
      110,
      "Animation lasts 110ms"
    );

    await finishTrustIconAnimations();
    Assert.less(
      Math.abs(
        shieldOnlyWidth() - trustIconContainer().getBoundingClientRect().width
      ),
      0.5,
      "The icon ends at just the shield"
    );
    Assert.ok(
      !trustIconContainer().hasAttribute("tracker-count-tab-switch"),
      "The tab switch attribute is removed once the animation ends"
    );

    await BrowserTestUtils.switchTab(gBrowser, trackerTab);
    await finishTrustIconAnimations();
    await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 1]] });
    try {
      await BrowserTestUtils.switchTab(gBrowser, emptyTab);
      Assert.ok(
        !trustIconContainer().hasAttribute("tracker-count-tab-switch"),
        "No shrink with reduced motion"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    BrowserTestUtils.removeTab(trackerTab);
    BrowserTestUtils.removeTab(emptyTab);
  }
});

add_task(async function test_tab_switch_shrink_preserves_hover_background() {
  const style = document.createElementNS(
    "http://www.w3.org/1999/xhtml",
    "style"
  );
  // Supply a distinct palette that forced colors cannot collapse, but leave the
  // choice of background-color to the production state rules and keyframes.
  style.textContent = `
    #trust-icon-container {
      --urlbar-box-background-color: rgb(32, 64, 96);
      --urlbar-box-background-color-hover: rgb(160, 192, 224);
      --urlbar-box-background-color-active: rgb(96, 32, 64);
      forced-color-adjust: none;
    }
  `;
  document.documentElement.appendChild(style);
  const icon = trustIconContainer();
  let trackerTab;
  let emptyTab;
  let shrink;
  try {
    trackerTab = await openTabWithBlockedTrackers(12);
    emptyTab = await openTabWithBlockedTrackers(0);
    await finishTrustIconAnimations();

    // Observe the hovered shield without an animation as an independent oracle.
    // Target the shield, not the count end that moves during shrink, in LTR or RTL.
    const shield = document.getElementById("trust-icon");
    const hoverIcon = async () => {
      const bounds = shield.getBoundingClientRect();
      EventUtils.synthesizeMouseAtCenter(shield, { type: "mousemove" });
      await TestUtils.waitForCondition(
        () => icon.matches(":hover"),
        "Waiting for the trust icon to be hovered"
      );
      return {
        x: bounds.x + bounds.width / 2,
        y: bounds.y + bounds.height / 2,
      };
    };
    await hoverIcon();
    const hoveredBackground = getComputedStyle(icon).backgroundColor;

    await BrowserTestUtils.switchTab(gBrowser, trackerTab);
    await finishTrustIconAnimations();
    EventUtils.synthesizeMouse(document.documentElement, 0, 0, {
      type: "mousemove",
    });
    await TestUtils.waitForCondition(
      () => !icon.matches(":hover"),
      "Waiting for the pointer to leave the tracker pill"
    );
    const idleBackground = getComputedStyle(icon).backgroundColor;
    Assert.notEqual(
      idleBackground,
      hoveredBackground,
      "Idle and hovered backgrounds are distinguishable"
    );
    const pointer = await hoverIcon();
    Assert.equal(
      getComputedStyle(icon).backgroundColor,
      hoveredBackground,
      "The hovered pill and shield have the same resting background"
    );

    // Hold the real 110ms animation before switchTab can wait past its end.
    style.sheet.insertRule(`
      #trust-icon-container[tracker-count-tab-switch]:not(.has-blocked-trackers, .breached, .insecure) {
        animation-play-state: paused;
      }
    `);
    await BrowserTestUtils.switchTab(gBrowser, emptyTab);
    shrink = icon
      .getAnimations()
      .find(
        animation =>
          animation.animationName == "tracker-count-tab-switch-shrink"
      );
    Assert.ok(shrink, "The real tab-switch shrink animation is present");
    shrink.pause();
    const duration = shrink.effect.getComputedTiming().duration;
    for (const progress of [0, 0.5]) {
      shrink.currentTime = duration * progress;
      Assert.ok(
        icon.contains(document.elementFromPoint(pointer.x, pointer.y)),
        `The pointer is inside the shrinking icon at progress ${progress}`
      );
      Assert.ok(icon.matches(":hover"), "The shrinking shield remains hovered");
      const background = getComputedStyle(icon).backgroundColor;
      info(
        `Hover shrink at ${progress}: idle=${idleBackground}, hovered=${hoveredBackground}, actual=${background}`
      );
      Assert.equal(
        background,
        hoveredBackground,
        `The shrink preserves the hovered background at progress ${progress}`
      );
    }
  } finally {
    shrink?.finish();
    // Remove the pause rule before tab cleanup can trigger another shrink.
    style.remove();
    EventUtils.synthesizeMouse(document.documentElement, 0, 0, {
      type: "mousemove",
    });
    for (const tab of [emptyTab, trackerTab]) {
      if (tab && !tab.closing) {
        await BrowserTestUtils.removeTab(tab);
      }
    }
    await finishTrustIconAnimations();
  }
});

add_task(async function test_tab_switch_without_tracker_count_animation() {
  const trackerTab = await openTabWithBlockedTrackers(12);
  const insecureTab = await openTabWithBlockedTrackers(
    12,
    // eslint-disable-next-line sdl/no-insecure-url
    "http://example.com/"
  );

  try {
    await BrowserTestUtils.switchTab(gBrowser, trackerTab);
    await finishTrustIconAnimations();
    await BrowserTestUtils.switchTab(gBrowser, insecureTab);
    Assert.equal(
      trackerCountTabSwitchAnimations().icon.length,
      0,
      "No resize when the selected tab is insecure"
    );

    await SpecialPowers.pushPrefEnv({ set: [["ui.prefersReducedMotion", 1]] });
    try {
      await BrowserTestUtils.switchTab(gBrowser, trackerTab);
      Assert.equal(
        trackerCountTabSwitchAnimations().icon.length,
        0,
        "No resize with reduced motion"
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    BrowserTestUtils.removeTab(trackerTab);
    BrowserTestUtils.removeTab(insecureTab);
  }
});

add_task(async function test_same_site_navigation_expands_tracker_count() {
  const tab = await openTabWithBlockedTrackers(4);

  try {
    await finishTrustIconAnimations();
    setBlockedTrackers(tab, 0);
    const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      "https://example.com/?same-site"
    );
    await loaded;
    await waitForTrustIconWithoutClass(
      "has-blocked-trackers",
      "Waiting for the count to clear on the new page"
    );

    setBlockedTrackers(tab, 4);
    gTrustPanelHandler.onNavigationComplete();
    Assert.ok(
      trustIconContainer().matches(".has-blocked-trackers:not(.entry-page)"),
      "The count is shown beyond the entry page"
    );
    const expand = document
      .getElementById("trust-icon-tracker-count-shortform-wrapper")
      .getAnimations()
      .find(
        animation => animation.animationName == "show-shortform-tracker-count"
      );
    Assert.ok(expand, "The count expands in");
    Assert.equal(
      expand.effect.getComputedTiming().duration,
      110,
      "Animation lasts 110ms"
    );
    await finishTrustIconAnimations();
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_entry_page_crossfades_into_shortform() {
  const tab = await openTabWithBlockedTrackers(12);

  try {
    BrowserTestUtils.startLoadingURIString(
      tab.linkedBrowser,
      "https://example.net/"
    );
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    await waitForTrustIconClass("entry-page", "Waiting for the entry page");
    setBlockedTrackers(tab, 12);
    gTrustPanelHandler.onNavigationComplete();
    await waitForTrustIconClass(
      "has-blocked-trackers",
      "Waiting for the tracker count"
    );

    const cssAnimation = (id, name) =>
      document
        .getElementById(id)
        .getAnimations()
        .find(animation => animation.animationName == name);
    const hideLongform = cssAnimation(
      "trust-icon-tracker-count-longform-wrapper",
      "hide-longform-tracker-count"
    );
    const showShortform = cssAnimation(
      "trust-icon-tracker-count-shortform-wrapper",
      "show-shortform-tracker-count"
    );
    Assert.ok(hideLongform && showShortform, "Both animations are present");
    Assert.equal(
      showShortform.effect.getComputedTiming().delay,
      hideLongform.effect.getComputedTiming().delay,
      "The shortform expands while the longform collapses"
    );
    for (const animation of [hideLongform, showShortform]) {
      Assert.equal(
        animation.effect.getComputedTiming().duration,
        110,
        "Animation lasts 110ms"
      );
    }
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});
