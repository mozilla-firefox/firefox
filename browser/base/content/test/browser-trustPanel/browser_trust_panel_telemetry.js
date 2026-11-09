/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test the telemetry recorded by the trust panel
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  BreachAlertStorage: "resource://gre/modules/BreachAlertStore.sys.mjs",
  ContentBlockingAllowList:
    "resource://gre/modules/ContentBlockingAllowList.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  SiteDataTestUtils: "resource://testing-common/SiteDataTestUtils.sys.mjs",
  UrlClassifierTestUtils:
    "resource://testing-common/UrlClassifierTestUtils.sys.mjs",
});

const TEST_ORIGIN = "https://example.com";
// eslint-disable-next-line sdl/no-insecure-url
const INSECURE_TEST_ORIGIN = "http://example.com";
const BAD_CERT_ORIGIN = "https://self-signed.example.com";
const TRACKING_PAGE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://tracking.example.org/browser/browser/base/content/test/browser-protectionsUI/trackingPage.html";
const BREACHED_ORIGIN = "https://example.org";
const NAVIGATION_TARGET = `${TEST_ORIGIN}/?navigated`;

const TEST_BREACH = {
  // Breaches older than a year are not taken into account.
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

async function toggleETP(tab) {
  await UrlbarTestUtils.openTrustPanel(window);

  let waitForReload = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("trustpanel-toggle"),
    {},
    window
  );
  await waitForReload;
}

async function openClearCookiesSubview() {
  await UrlbarTestUtils.openTrustPanel(window);

  let viewShown = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-clearcookiesView"),
    "ViewShown"
  );
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("trustpanel-clear-cookies-button"),
    {},
    window
  );
  await viewShown;
}

async function clickAndWaitForPanelToClose(buttonId) {
  let popupHidden = BrowserTestUtils.waitForEvent(document, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById(buttonId),
    {},
    window
  );
  await popupHidden;
}

async function openSecurityInfoSubview() {
  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );
}

async function openTrackerList() {
  let listShown = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-blockerView"),
    "ViewShown"
  );
  document.getElementById("trustpanel-blocker-see-all").click();
  await listShown;
}

// Drills into the detail subview of a blocker, identified by its
// l10nKeys.general, from either the blocked or the allowed section.
async function openTrackerDetails(blocker) {
  let button = await TestUtils.waitForCondition(
    () =>
      document.querySelector(
        `#trustpanel-blockerView [data-l10n-id="trustpanel-list-label-${blocker}"]`
      ),
    `Waiting for the ${blocker} button`
  );

  let detailsShown = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-blockerDetailsView"),
    "ViewShown"
  );
  // Click without coordinates: inserting the buttons resizes the panel, which
  // can move the button out from under a synthesized mouse position.
  button.click();
  await detailsShown;
}

function allowInsecureLoads(origin) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(origin),
    {}
  );
  Services.perms.addFromPrincipal(
    principal,
    "https-only-load-insecure",
    Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW,
    Ci.nsIPermissionManager.EXPIRE_NEVER
  );
}

// Always get integer event count for Glean event
function eventCount(category, name) {
  return Glean[category][name].testGetValue()?.length ?? 0;
}

// The extras of every recorded event, in the order they were recorded
function eventExtras(category, name) {
  return Glean[category][name].testGetValue()?.map(event => event.extra) ?? [];
}

function assertToggleEvents(offCount, onCount, message) {
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickEtpToggleOff"),
    offCount,
    `${message}: click_etp_toggle_off count`
  );
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickEtpToggleOn"),
    onCount,
    `${message}: click_etp_toggle_on count`
  );
}

function assertClearCookiesEvents(
  openedCount,
  confirmedCount,
  cancelledCount,
  message
) {
  Assert.equal(
    eventCount("trustpanel", "clearCookiesOpened"),
    openedCount,
    `${message}: clear_cookies_opened count`
  );
  Assert.equal(
    eventCount("trustpanel", "clearCookiesConfirmed"),
    confirmedCount,
    `${message}: clear_cookies_confirmed count`
  );
  Assert.equal(
    eventCount("trustpanel", "clearCookiesCancelled"),
    cancelledCount,
    `${message}: clear_cookies_cancelled count`
  );
}

add_task(async function test_etp_toggle_telemetry() {
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  registerCleanupFunction(() => {
    Services.perms.removeAll();
  });

  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(document.getElementById("trust-icon")),
    "Waiting for the trust icon"
  );

  assertToggleEvents(0, 0, "Before interacting with the panel");

  info("Turn ETP off for the site");
  await toggleETP(tab);

  Assert.ok(
    ContentBlockingAllowList.includes(gBrowser.selectedBrowser),
    "The site has an ETP exception"
  );
  assertToggleEvents(1, 0, "After disabling ETP");

  info("Turn ETP back on for the site");
  await toggleETP(tab);

  Assert.ok(
    !ContentBlockingAllowList.includes(gBrowser.selectedBrowser),
    "The ETP exception has been removed"
  );
  assertToggleEvents(1, 1, "After re-enabling ETP");

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_clear_cookies_cancelled_telemetry() {
  Services.fog.testResetFOG();

  SiteDataTestUtils.addToCookies({
    origin: TEST_ORIGIN,
    name: "test-cancel",
    value: "1",
  });

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openClearCookiesSubview();
  assertClearCookiesEvents(1, 0, 0, "After opening the subview");

  info("Back out of the subview");
  await clickAndWaitForPanelToClose("trustpanel-clear-cookie-cancel");

  assertClearCookiesEvents(1, 0, 1, "After cancelling");
  Assert.ok(
    SiteDataTestUtils.hasCookies(TEST_ORIGIN),
    "Cancelling leaves the site data intact"
  );

  await SiteDataTestUtils.clear();
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_clear_cookies_confirmed_telemetry() {
  Services.fog.testResetFOG();

  SiteDataTestUtils.addToCookies({
    origin: TEST_ORIGIN,
    name: "test-confirm",
    value: "1",
  });

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openClearCookiesSubview();
  assertClearCookiesEvents(1, 0, 0, "After opening the subview");

  info("Confirm clearing the site data");
  await clickAndWaitForPanelToClose("trustpanel-clear-cookie-clear");

  assertClearCookiesEvents(1, 1, 0, "After confirming");
  await TestUtils.waitForCondition(
    () => !SiteDataTestUtils.hasCookies(TEST_ORIGIN),
    "Waiting for the site data to be cleared"
  );

  await SiteDataTestUtils.clear();
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_security_info_opened_telemetry() {
  for (let [origin, connection] of [
    [TEST_ORIGIN, "secure"],
    [INSECURE_TEST_ORIGIN, "not-secure"],
  ]) {
    Services.fog.testResetFOG();

    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: origin,
      waitForLoad: true,
    });

    info(`Open the security information subview for ${origin}`);
    await openSecurityInfoSubview();

    Assert.deepEqual(
      eventExtras("trustpanel", "securityInfoOpened"),
      [{ connection }],
      `${origin}: the connection state of the page is recorded`
    );

    await UrlbarTestUtils.closeTrustPanel(window);
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_security_info_page_info_telemetry() {
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openSecurityInfoSubview();

  info("Open Page Info from the subview");
  let pageInfoPromise = BrowserTestUtils.domWindowOpenedAndLoaded();
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("trustpanel-siteinformation-morelink"),
    {},
    window
  );
  let pageInfo = await pageInfoPromise;

  Assert.equal(
    pageInfo.document.documentURI,
    "chrome://browser/content/pageinfo/pageInfo.xhtml",
    "The Page Info dialog was opened"
  );
  Assert.equal(
    eventCount("trustpanel", "securityInfoPageInfoOpened"),
    1,
    "Opening Page Info is recorded"
  );

  await BrowserTestUtils.closeWindow(pageInfo);
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_security_info_cert_exception_telemetry() {
  Services.fog.testResetFOG();

  registerCleanupFunction(() => {
    Cc["@mozilla.org/security/certoverride;1"]
      .getService(Ci.nsICertOverrideService)
      .clearValidityOverride("self-signed.example.com", -1, {});
  });

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  await loadBadCertPage(BAD_CERT_ORIGIN);

  await openSecurityInfoSubview();
  Assert.deepEqual(
    eventExtras("trustpanel", "securityInfoOpened"),
    [{ connection: "secure-cert-user-overridden" }],
    "The overridden certificate is recorded as the connection state"
  );

  info("Remove the certificate error exception");
  let errorPage = BrowserTestUtils.waitForErrorPage(gBrowser.selectedBrowser);
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("identity-popup-remove-cert-exception"),
    {},
    window
  );
  await errorPage;

  Assert.equal(
    eventCount("trustpanel", "securityInfoCertExceptionRemoved"),
    1,
    "Removing the certificate exception is recorded"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_security_info_https_only_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_only_mode", true]],
  });
  Services.fog.testResetFOG();

  info("Turn HTTPS-Only Mode off for the site, so the menulist is shown");
  allowInsecureLoads(INSECURE_TEST_ORIGIN);

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openSecurityInfoSubview();

  let menulist = document.getElementById(
    "trustpanel-popup-security-httpsonlymode-menulist"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(menulist),
    "The HTTPS-Only Mode menulist is shown"
  );
  Assert.equal(
    parseInt(menulist.value, 10),
    1,
    "HTTPS-Only Mode is off for the site"
  );

  info("Turn HTTPS-Only Mode back on for the site");
  let reloaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  menulist.getItemAtIndex(0).doCommand();
  await reloaded;

  Assert.deepEqual(
    eventExtras("trustpanel", "securityInfoHttpsOnlyChanged"),
    [{ setting: "on" }],
    "The newly selected setting is recorded"
  );

  Services.perms.removeAll();
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_tracker_subviews_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
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
  await UrlClassifierTestUtils.addBuiltInTestTrackers();
  await UrlClassifierTestUtils.addTestHostsFromPrefs();
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TRACKING_PAGE,
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);

  let blockerHeader = document.getElementById(
    "trustpanel-blocker-section-header"
  );

  info("Block a cryptominer, so an allowed and a blocked category are listed");
  await SpecialPowers.spawn(tab.linkedBrowser, [], function () {
    content.postMessage("cryptomining", "*");
  });
  await TestUtils.waitForCondition(
    () => parseInt(blockerHeader.textContent, 10) == 1,
    "Waiting for the blocked cryptominer"
  );

  Assert.equal(
    eventCount("trustpanel", "trackerListOpened"),
    0,
    "Nothing is recorded before the tracker list is opened"
  );

  info("Open the tracker list and drill into the allowed tracking content");
  await openTrackerList();
  Assert.equal(
    eventCount("trustpanel", "trackerListOpened"),
    1,
    "Opening the tracker list is recorded"
  );

  await openTrackerDetails("tracking-content");
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickTrackers"),
    1,
    "Opening the tracking content details records the protections panel event"
  );
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickCryptominers"),
    0,
    "No other category is recorded"
  );

  info("Drill into the blocked cryptominer category");
  await UrlbarTestUtils.closeTrustPanel(window);
  await UrlbarTestUtils.openTrustPanel(window);
  await openTrackerList();
  await openTrackerDetails("cryptominer");

  Assert.equal(
    eventCount("trustpanel", "trackerListOpened"),
    2,
    "Each open of the tracker list is recorded"
  );
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickCryptominers"),
    1,
    "Opening the cryptominer details records its own event"
  );
  Assert.equal(
    eventCount("securityUiProtectionspopup", "clickTrackers"),
    1,
    "The tracking content event is not recorded again"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
  await UrlClassifierTestUtils.cleanupTestTrackers();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_closed_telemetry() {
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  info("Dismiss the panel without acting on it");
  await UrlbarTestUtils.openTrustPanel(window);
  let shownAt = performance.now();
  await TestUtils.waitForCondition(
    () => performance.now() - shownAt > 50,
    "Holding the panel open long enough to measure"
  );
  await UrlbarTestUtils.closeTrustPanel(window);

  let closed = Glean.trustpanel.closed.testGetValue();
  Assert.equal(closed?.length, 1, "Closing the panel is recorded");
  Assert.equal(
    closed[0].extra.closing_reason,
    "dismissed",
    "A panel closed without interaction counts as abandoned"
  );
  Assert.equal(
    closed[0].extra.opening_reason,
    "shieldButtonClicked",
    "The opening reason is carried over to the close event"
  );
  Assert.greaterOrEqual(
    parseInt(closed[0].extra.duration_ms, 10),
    50,
    "The duration covers how long the panel was open"
  );

  info("Close the panel by turning ETP off");
  Services.fog.testResetFOG();
  await toggleETP(tab);

  closed = Glean.trustpanel.closed.testGetValue();
  Assert.equal(closed?.length, 1, "One close event per panel session");
  Assert.equal(
    closed[0].extra.closing_reason,
    "etpToggled",
    "The action that ended the session is recorded"
  );

  Services.perms.removeAll();
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_closed_by_navigation_telemetry() {
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);

  info("Navigate away while the panel is open");
  let popupHidden = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-popup"),
    "popuphidden"
  );
  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(gBrowser, NAVIGATION_TARGET);
  await popupHidden;
  await loaded;

  Assert.deepEqual(
    eventExtras("trustpanel", "closed").map(extra => extra.closing_reason),
    ["navigated"],
    "A panel closed by a navigation is not counted as abandonment"
  );

  await BrowserTestUtils.removeTab(tab);
});

// The off <-> off-temporarily switch is the one HTTPS-Only change that neither
// reloads nor closes the panel, so its closing reason has to survive until the
// user dismisses the panel themselves.
add_task(async function test_closed_by_https_only_change_telemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_only_mode", true]],
  });
  Services.fog.testResetFOG();

  info("Turn HTTPS-Only Mode off for the site, so the menulist is shown");
  allowInsecureLoads(INSECURE_TEST_ORIGIN);

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openSecurityInfoSubview();

  let menulist = document.getElementById(
    "trustpanel-popup-security-httpsonlymode-menulist"
  );
  info("Switch from off to off temporarily");
  menulist.getItemAtIndex(2).doCommand();

  await TestUtils.waitForCondition(
    () => eventCount("trustpanel", "securityInfoHttpsOnlyChanged"),
    "Waiting for the setting change to be recorded"
  );
  Assert.deepEqual(
    eventExtras("trustpanel", "securityInfoHttpsOnlyChanged"),
    [{ setting: "off-temporarily" }],
    "The newly selected setting is recorded"
  );
  Assert.equal(
    document.getElementById("trustpanel-popup").state,
    "open",
    "Switching between off and off temporarily does not close the panel"
  );

  await UrlbarTestUtils.closeTrustPanel(window);

  Assert.deepEqual(
    eventExtras("trustpanel", "closed").map(extra => extra.closing_reason),
    ["httpsOnlyChanged"],
    "The change is not counted as abandonment even though the panel stayed open"
  );

  Services.perms.removeAll();
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

// An action that leaves the panel open keeps the close reason correctly,
// even if the user navigates away
add_task(async function test_action_closing_reason_survives_navigation() {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.https_only_mode", true]],
  });
  Services.fog.testResetFOG();

  info("Turn HTTPS-Only Mode off for the site, so the menulist is shown");
  allowInsecureLoads(INSECURE_TEST_ORIGIN);

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  await openSecurityInfoSubview();

  let menulist = document.getElementById(
    "trustpanel-popup-security-httpsonlymode-menulist"
  );
  info("Switch from off to off temporarily, which leaves the panel open");
  menulist.getItemAtIndex(2).doCommand();

  await TestUtils.waitForCondition(
    () => eventCount("trustpanel", "securityInfoHttpsOnlyChanged"),
    "Waiting for the setting change to be recorded"
  );
  Assert.equal(
    document.getElementById("trustpanel-popup").state,
    "open",
    "Switching between off and off temporarily does not close the panel"
  );

  info("Navigate away while the panel is still open");
  let popupHidden = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-popup"),
    "popuphidden"
  );
  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(gBrowser, NAVIGATION_TARGET);
  await popupHidden;
  await loaded;

  Assert.deepEqual(
    eventExtras("trustpanel", "closed").map(extra => extra.closing_reason),
    ["httpsOnlyChanged"],
    "The navigation does not take the close away from the setting change"
  );

  Services.perms.removeAll();
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_closed_by_breach_alert_cta_telemetry() {
  await RemoteSettings("fxmonitor-breaches").emit("sync", {
    data: { current: [TEST_BREACH] },
  });
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.trustPanel.breachAlerts", true]],
  });
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: BREACHED_ORIGIN,
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);

  const breachAlertSection = document.getElementById(
    "trustpanel-breach-alert-section"
  );
  let ctaButton = await TestUtils.waitForCondition(
    () =>
      !breachAlertSection.hidden &&
      breachAlertSection.shadowRoot?.querySelector("moz-button[type=primary]"),
    "Waiting for the breach alert's Check Mozilla Monitor button"
  );

  // Stub the tab switch out: it would try to reach monitor.mozilla.org, and it
  // is what would close the panel by moving focus away from it.
  const sandbox = sinon.createSandbox();
  sandbox.stub(window, "switchToTabHavingURI");
  try {
    info("Click through to Mozilla Monitor");
    ctaButton.click();
    await TestUtils.waitForCondition(
      () => window.switchToTabHavingURI.called,
      "Waiting for the tab switch to Mozilla Monitor"
    );
  } finally {
    sandbox.restore();
  }

  await UrlbarTestUtils.closeTrustPanel(window);

  Assert.deepEqual(
    eventExtras("trustpanel", "closed").map(extra => extra.closing_reason),
    ["monitorOpened"],
    "Clicking through to Monitor is not counted as abandonment"
  );

  await BrowserTestUtils.removeTab(tab);
  await RemoteSettings("fxmonitor-breaches").emit("sync", {
    data: { current: [] },
  });
  const storage = new BreachAlertStorage();
  await storage.initialize();
  await storage.clearAllBreachAlertDismissals();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_opened_opening_reason_telemetry() {
  Services.fog.testResetFOG();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });

  info("Open the panel from the shield icon");
  await UrlbarTestUtils.openTrustPanel(window);
  await TestUtils.waitForCondition(
    () => eventCount("trustpanel", "opened"),
    "Waiting for the open to be recorded"
  );
  Assert.equal(
    Glean.trustpanel.opened.testGetValue()[0].extra.opening_reason,
    "shieldButtonClicked",
    "The reason the shield click passes is recorded"
  );

  await UrlbarTestUtils.closeTrustPanel(window);

  info("Open the panel without a reason");
  Services.fog.testResetFOG();
  let shown = BrowserTestUtils.waitForEvent(
    document.getElementById("trustpanel-popup"),
    "popupshown"
  );
  await gTrustPanelHandler.showPopup();
  await shown;
  await TestUtils.waitForCondition(
    () => eventCount("trustpanel", "opened"),
    "Waiting for the open to be recorded"
  );
  Assert.equal(
    Glean.trustpanel.opened.testGetValue()[0].extra.opening_reason,
    "unknown",
    "Opening without a reason falls back to unknown"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
});
