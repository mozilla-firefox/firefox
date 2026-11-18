/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

const { AboutProtectionsParent } = ChromeUtils.importESModule(
  "resource:///actors/AboutProtectionsParent.sys.mjs"
);

const LOG = {
  "https://1.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT, true, 1],
  ],
  "https://2.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_FINGERPRINTING_CONTENT, true, 1],
  ],
  "https://3.example.com": [
    [Ci.nsIWebProgressListener.STATE_BLOCKED_CRYPTOMINING_CONTENT, true, 2],
  ],
  "https://4.example.com": [
    [Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER, true, 3],
  ],
  "https://5.example.com": [
    [Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER, true, 1],
  ],
  // Cookie blocked for other reason, then identified as a tracker
  "https://6.example.com": [
    [
      Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_ALL |
        Ci.nsIWebProgressListener.STATE_LOADED_LEVEL_1_TRACKING_CONTENT,
      true,
      4,
    ],
  ],
};

requestLongerTimeout(2);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.vpn_promo.enabled", true],
      ["browser.contentblocking.report.vpn_regions", "us,ca,nz,sg,my,gb"],
      [
        "browser.vpn_promo.disallowed_regions",
        "ae,by,cn,cu,iq,ir,kp,om,ru,sd,sy,tm,tr",
      ],

      // Change the endpoints to prevent non-local network connections when landing on the page.
      ["browser.contentblocking.report.monitor.url", ""],
      ["browser.contentblocking.report.monitor.sign_in_url", ""],
      ["browser.contentblocking.report.social.url", ""],
      ["browser.contentblocking.report.cookie.url", ""],
      ["browser.contentblocking.report.tracker.url", ""],
      ["browser.contentblocking.report.fingerprinter.url", ""],
      ["browser.contentblocking.report.cryptominer.url", ""],
      ["browser.contentblocking.report.mobile-ios.url", ""],
      ["browser.contentblocking.report.mobile-android.url", ""],
      ["browser.contentblocking.report.monitor.home_page_url", ""],
      ["browser.contentblocking.report.monitor.preferences_url", ""],
      ["browser.contentblocking.report.vpn.url", ""],
      ["browser.contentblocking.report.vpn-promo.url", ""],
      ["browser.contentblocking.report.vpn-android.url", ""],
      ["browser.contentblocking.report.vpn-ios.url", ""],
    ],
  });

  let oldCanRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;
  registerCleanupFunction(() => {
    Services.telemetry.canRecordExtended = oldCanRecord;
    // AboutProtectionsParent.setTestOverride(null);
  });
});

// Waits until the security.ui.protections Glean event named `name` has recorded
// exactly `count` events, and resolves with all of them in recording order.
// Events are recorded in the content process, so child data has to be flushed
// into the parent before it can be read back.
function waitForProtectionsEvents(name, count) {
  info(`waiting for ${count} ${name} event(s)`);
  return TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    let events = Glean.securityUiProtections[name].testGetValue();
    info(`got ${events ? events.length : 0} ${name} event(s)`);
    return events?.length === count ? events : null;
  }, `waiting for ${count} ${name} event(s)`);
}

async function clearProtectionsEvents() {
  // Flush first, so events still in flight from a previous task are discarded
  // by the reset rather than landing in the middle of the next one.
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
}

add_task(async function checkTelemetryLoadEvents() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.database.enabled", false],
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["browser.vpn_promo.enabled", false],
    ],
  });
  await addArbitraryTimeout();

  // Clear everything.
  await clearProtectionsEvents();

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  let loadEvents = await waitForProtectionsEvents("showProtectionReport", 1);
  is(loadEvents.length, 1, `recorded telemetry for showing the report`);

  await BrowserTestUtils.reloadTab(tab);
  loadEvents = await waitForProtectionsEvents("closeProtectionReport", 1);
  is(loadEvents.length, 1, `recorded telemetry for closing the report`);

  await BrowserTestUtils.removeTab(tab);
});

let addArbitraryTimeout = async () => {
  // There's an arbitrary interval of 2 seconds in which the content
  // processes sync their event data with the parent process, we wait
  // this out to ensure that we clear everything that is left over from
  // previous tests and don't receive random events in the middle of our tests.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(c => setTimeout(c, 2000));
};

add_task(async function checkTelemetryClickEvents() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.database.enabled", true],
      ["browser.contentblocking.report.monitor.enabled", true],
      ["browser.contentblocking.report.lockwise.enabled", true],
      ["browser.vpn_promo.enabled", false],
    ],
  });
  await addArbitraryTimeout();

  // Clear everything.
  await clearProtectionsEvents();

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  // Add user logins.
  await Services.logins.addLoginAsync(TEST_LOGIN1);
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const managePasswordsButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-passwords-button");
      },
      "Manage passwords button exists"
    );
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(managePasswordsButton),
      "manage passwords button is visible"
    );
    managePasswordsButton.click();
  });

  let events = await waitForProtectionsEvents("clickLwOpenButton", 1);
  is(
    events.at(-1).extra.value,
    "manage_passwords",
    `recorded telemetry for lw_open_button when there are no breached passwords`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  // Add breached logins.
  AboutProtectionsParent.setTestOverride(
    mockGetLoginDataWithSyncedDevices(false, 4)
  );
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const managePasswordsButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-passwords-button");
      },
      "Manage passwords button exists"
    );
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(managePasswordsButton),
      "manage passwords button is visible"
    );
    managePasswordsButton.click();
  });

  events = await waitForProtectionsEvents("clickLwOpenButton", 2);
  is(
    events.at(-1).extra.value,
    "manage_breached_passwords",
    `recorded telemetry for lw_open_button when there are breached passwords`
  );
  AboutProtectionsParent.setTestOverride(null);
  await Services.logins.removeLoginAsync(TEST_LOGIN1);
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await BrowserTestUtils.reloadTab(tab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    // Show all elements, so we can click on them, even though our user is not logged in.
    let hidden_elements = content.document.querySelectorAll(".hidden");
    for (let el of hidden_elements) {
      el.style.display = "block ";
    }

    const savePasswordsButton = await ContentTaskUtils.waitForCondition(() => {
      // Opens an extra tab
      return content.document.getElementById("save-passwords-button");
    }, "Save Passwords button exists");

    savePasswordsButton.click();
  });

  events = await waitForProtectionsEvents("clickLwOpenButton", 3);
  is(
    events.at(-1).extra.value,
    "save_passwords",
    `recorded telemetry for lw_open_button when there are no stored passwords`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const lockwiseAboutLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("lockwise-how-it-works");
    }, "lockwiseReportLink exists");

    lockwiseAboutLink.click();
  });

  events = await waitForProtectionsEvents("clickLwAboutLink", 1);
  is(events.length, 1, `recorded telemetry for lw_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    let monitorAboutLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-link");
    }, "monitorAboutLink exists");

    monitorAboutLink.click();
  });

  events = await waitForProtectionsEvents("clickMtrAboutLink", 1);
  is(events.length, 1, `recorded telemetry for mtr_about_link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const signUpForMonitorLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("sign-up-for-monitor-link");
    }, "signUpForMonitorLink exists");

    signUpForMonitorLink.click();
  });

  events = await waitForProtectionsEvents("clickMtrSignupButton", 1);
  is(events.length, 1, `recorded telemetry for mtr_signup_button`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const socialLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("social-link");
    }, "Learn more link for social tab exists");

    socialLearnMoreLink.click();
  });

  events = await waitForProtectionsEvents("clickTrackersAboutLink", 1);
  is(
    events.at(-1).extra.value,
    "social",
    `recorded telemetry for social trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const cookieLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("cookie-link");
    }, "Learn more link for cookie tab exists");

    cookieLearnMoreLink.click();
  });

  events = await waitForProtectionsEvents("clickTrackersAboutLink", 2);
  is(
    events.at(-1).extra.value,
    "cookie",
    `recorded telemetry for cookie trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const trackerLearnMoreLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("tracker-link");
    }, "Learn more link for tracker tab exists");

    trackerLearnMoreLink.click();
  });

  events = await waitForProtectionsEvents("clickTrackersAboutLink", 3);
  is(
    events.at(-1).extra.value,
    "tracker",
    `recorded telemetry for content tracker trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const fingerprinterLearnMoreLink = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("fingerprinter-link");
      },
      "Learn more link for fingerprinter tab exists"
    );

    fingerprinterLearnMoreLink.click();
  });

  events = await waitForProtectionsEvents("clickTrackersAboutLink", 4);
  is(
    events.at(-1).extra.value,
    "fingerprinter",
    `recorded telemetry for fingerprinter trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const cryptominerLearnMoreLink = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("cryptominer-link");
      },
      "Learn more link for cryptominer tab exists"
    );

    cryptominerLearnMoreLink.click();
  });

  events = await waitForProtectionsEvents("clickTrackersAboutLink", 5);
  is(
    events.at(-1).extra.value,
    "cryptominer",
    `recorded telemetry for cryptominer trackers_about_link`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const protectionSettings = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("protection-settings");
    }, "protection-settings link exists");

    protectionSettings.click();
  });

  events = await waitForProtectionsEvents("clickSettingsLink", 1);
  is(
    events.at(-1).extra.value,
    "header-settings",
    `recorded telemetry for settings_link header-settings`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const customProtectionSettings = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("manage-protections");
      },
      "manage-protections link exists"
    );
    // Show element so we can click on it
    customProtectionSettings.style.display = "block";

    customProtectionSettings.click();
  });

  events = await waitForProtectionsEvents("clickSettingsLink", 2);
  is(
    events.at(-1).extra.value,
    "custom-card-settings",
    `recorded telemetry for settings_link custom-card-settings`
  );
  await BrowserTestUtils.removeTab(gBrowser.selectedTab);

  // Add breached logins and some resolved breaches.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 1,
    })
  );
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const resolveBreachesButton = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-partial-breaches-link");
      },
      "Monitor resolve breaches button exists"
    );

    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(resolveBreachesButton),
      "Resolve breaches button is visible"
    );

    resolveBreachesButton.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 1);
  is(
    events.at(-1).extra.value,
    "resolve_breaches",
    `recorded telemetry for resolve breaches button`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const monitorKnownBreachesBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-known-breaches-link");
      },
      "Monitor card known breaches block exists"
    );

    monitorKnownBreachesBlock.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 2);
  is(
    events.at(-1).extra.value,
    "known_resolved_breaches",
    `recorded telemetry for monitor known breaches block`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const monitorExposedPasswordsBlock =
      await ContentTaskUtils.waitForCondition(() => {
        return content.document.getElementById(
          "monitor-exposed-passwords-link"
        );
      }, "Monitor card exposed passwords block exists");

    monitorExposedPasswordsBlock.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 3);
  is(
    events.at(-1).extra.value,
    "exposed_passwords_unresolved_breaches",
    `recorded telemetry for monitor exposed passwords block`
  );

  // Add breached logins and no resolved breaches.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 0,
    })
  );
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const manageBreachesButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor manage breaches button exists");

    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(manageBreachesButton),
      "Manage breaches button is visible"
    );

    manageBreachesButton.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 4);
  is(
    events.at(-1).extra.value,
    "manage_breaches",
    `recorded telemetry for manage breaches button`
  );

  // All breaches are resolved.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 3,
      numBreachesResolved: 3,
    })
  );
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const viewReportButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor view report button exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(viewReportButton),
      "View report button is visible"
    );

    viewReportButton.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 5);
  is(
    events.at(-1).extra.value,
    "view_report",
    `recorded telemetry for view report button`
  );

  // No breaches are present.
  AboutProtectionsParent.setTestOverride(
    mockGetMonitorData({
      potentiallyBreachedLogins: 4,
      numBreaches: 0,
      numBreachesResolved: 0,
    })
  );
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const viewReportButton = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-breaches-link");
    }, "Monitor view report button exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(viewReportButton),
      "View report button is visible"
    );

    viewReportButton.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 6);
  is(
    events.at(-1).extra.value,
    "view_report",
    `recorded telemetry for view report button`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const monitorEmailBlock = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("monitor-stored-emails-link");
    }, "Monitor card email block exists");

    monitorEmailBlock.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 7);
  is(
    events.at(-1).extra.value,
    "stored_emails",
    `recorded telemetry for monitor email block`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const monitorKnownBreachesBlock = await ContentTaskUtils.waitForCondition(
      () => {
        return content.document.getElementById("monitor-known-breaches-link");
      },
      "Monitor card known breaches block exists"
    );

    monitorKnownBreachesBlock.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 8);
  is(
    events.at(-1).extra.value,
    "known_unresolved_breaches",
    `recorded telemetry for monitor known breaches block`
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const monitorExposedPasswordsBlock =
      await ContentTaskUtils.waitForCondition(() => {
        return content.document.getElementById(
          "monitor-exposed-passwords-link"
        );
      }, "Monitor card exposed passwords block exists");

    monitorExposedPasswordsBlock.click();
  });

  events = await waitForProtectionsEvents("clickMtrReportLink", 9);
  is(
    events.at(-1).extra.value,
    "exposed_passwords_all_breaches",
    `recorded telemetry for monitor exposed passwords block`
  );

  // Clean up.
  AboutProtectionsParent.setTestOverride(null);
  await BrowserTestUtils.removeTab(tab);
});

// This tests that telemetry is sent when saveEvents is called.
add_task(async function test_save_telemetry() {
  Services.fog.testResetFOG();

  await TrackingDBService.saveEvents(JSON.stringify(LOG));

  is(Glean.contentblocking.trackersBlockedCount.testGetValue(), 6);

  // Use the TrackingDBService API to delete the data.
  await TrackingDBService.clearAll();
});

// Test that telemetry is sent if entrypoint param is included,
// and test that it is recorded as default if entrypoint param is not properly included
add_task(async function checkTelemetryLoadEventForEntrypoint() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.contentblocking.database.enabled", false],
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["browser.vpn_promo.enabled", false],
    ],
  });
  await addArbitraryTimeout();

  // Clear everything.
  await clearProtectionsEvents();

  info("Typo in 'entrypoint' should not be recorded");
  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections?entryPoint=newPage",
    gBrowser,
  });

  let loadEvents = await waitForProtectionsEvents("showProtectionReport", 1);
  is(
    loadEvents.at(-1).extra.value,
    "direct",
    `recorded telemetry for showing the report contains default 'direct' entrypoint`
  );

  await BrowserTestUtils.removeTab(tab);

  tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections?entrypoint=page",
    gBrowser,
  });

  loadEvents = await waitForProtectionsEvents("showProtectionReport", 2);
  is(
    loadEvents.at(-1).extra.value,
    "page",
    "recorded telemetry for showing the report contains correct entrypoint"
  );

  await BrowserTestUtils.removeTab(tab);

  tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  loadEvents = await waitForProtectionsEvents("showProtectionReport", 3);
  is(
    loadEvents.at(-1).extra.value,
    "direct",
    "a later load without an entrypoint is recorded as 'direct'"
  );

  // Clean up.
  await BrowserTestUtils.removeTab(tab);
});

// This test is skipping due to failures on try, it passes locally.
// Test that telemetry is sent from the vpn card
add_task(async function checkTelemetryClickEventsVPN() {
  if (Services.sysinfo.getProperty("name") != "Windows_NT") {
    ok(true, "User is on an unsupported platform, the vpn card will not show");
    return;
  }
  await addArbitraryTimeout();
  // Clear everything.
  await clearProtectionsEvents();
  // user is not subscribed to VPN, and is in the us
  AboutProtectionsParent.setTestOverride(getVPNOverrides(false, "us"));
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.vpn_promo.enabled", true],
      [
        "browser.vpn_promo.disallowed_regions",
        "ae,by,cn,cu,iq,ir,kp,om,ru,sd,sy,tm,tr",
      ],
      ["browser.contentblocking.report.vpn_regions", "us,ca,nz,sg,my,gb"],
      ["browser.contentblocking.database.enabled", false],
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["browser.contentblocking.report.hide_vpn_banner", true],
      ["browser.contentblocking.report.vpn-android.url", ""],
      ["browser.contentblocking.report.vpn-ios.url", ""],
      ["browser.contentblocking.report.vpn.url", ""],
    ],
  });
  Services.locale.availableLocales = ["en-US"];
  Services.locale.requestedLocales = ["en-US"];
  await promiseSetHomeRegion("US");

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  info("checking for vpn link");
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const getVPNLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("get-vpn-link");
    }, "get vpn link exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(getVPNLink),
      "get vpn link is visible"
    );
    EventUtils.sendMouseEvent(
      { type: "click", button: 1 },
      getVPNLink,
      content
    );
  });

  let events = await waitForProtectionsEvents("clickVpnCardLink", 1);
  is(
    events.length,
    1,
    `recorded telemetry for vpn_card_link when user is not subscribed`
  );

  // User is subscribed to VPN
  AboutProtectionsParent.setTestOverride(getVPNOverrides(true, "us"));
  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const androidVPNLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("vpn-google-playstore-link");
    }, "android vpn link exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(androidVPNLink),
      "android vpn link is visible"
    );
    await ContentTaskUtils.waitForCondition(() => {
      return content.document
        .querySelector(".vpn-card")
        .classList.contains("subscribed");
    }, "subscribed class is added to the vpn card");

    EventUtils.sendMouseEvent(
      { type: "click", button: 1 },
      androidVPNLink,
      content
    );
  });

  events = await waitForProtectionsEvents("clickVpnAppLinkAndroid", 1);
  is(events.length, 1, `recorded telemetry for vpn_app_link_android link`);

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const iosVPNLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("vpn-app-store-link");
    }, "ios vpn link exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(iosVPNLink),
      "ios vpn link is visible"
    );
    await ContentTaskUtils.waitForCondition(() => {
      return content.document
        .querySelector(".vpn-card")
        .classList.contains("subscribed");
    }, "subscribed class is added to the vpn card");

    EventUtils.sendMouseEvent(
      { type: "click", button: 1 },
      iosVPNLink,
      content
    );
  });

  events = await waitForProtectionsEvents("clickVpnAppLinkIos", 1);
  is(events.length, 1, `recorded telemetry for vpn_app_link_ios link`);

  // Clean up.
  await BrowserTestUtils.removeTab(tab);
}).skip();

// This test is skipping due to failures on try, it passes locally.
// Test that telemetry is sent from the vpn banner
add_task(async function checkTelemetryEventsVPNBanner() {
  if (Services.sysinfo.getProperty("name") != "Windows_NT") {
    ok(true, "User is on an unsupported platform, the vpn card will not show");
    return;
  }
  AboutProtectionsParent.setTestOverride(getVPNOverrides(false, "us"));
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.vpn_promo.enabled", true],
      ["browser.contentblocking.report.vpn_regions", "us,ca,nz,sg,my,gb"],
      [
        "browser.vpn_promo.disallowed_regions",
        "ae,by,cn,cu,iq,ir,kp,om,ru,sd,sy,tm,tr",
      ],
      ["browser.contentblocking.database.enabled", false],
      ["browser.contentblocking.report.monitor.enabled", false],
      ["browser.contentblocking.report.lockwise.enabled", false],
      ["browser.contentblocking.report.hide_vpn_banner", false],
      ["browser.contentblocking.report.vpn-promo.url", ""],
    ],
  });
  await addArbitraryTimeout();

  // The VPN banner only shows if the user is in en*
  Services.locale.availableLocales = ["en-US"];
  Services.locale.requestedLocales = ["en-US"];

  // Clear everything.
  await clearProtectionsEvents();

  // User is not subscribed to VPN
  AboutProtectionsParent.setTestOverride(getVPNOverrides(false, "us"));

  let tab = await BrowserTestUtils.openNewForegroundTab({
    url: "about:protections",
    gBrowser,
  });

  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const bannerVPNLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.getElementById("vpn-banner-link");
    }, "vpn banner link exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(bannerVPNLink),
      "vpn banner link is visible"
    );
    EventUtils.sendMouseEvent(
      { type: "click", button: 1 },
      bannerVPNLink,
      content
    );
  });

  let events = await waitForProtectionsEvents("clickVpnBannerLink", 1);
  is(events.length, 1, `recorded telemetry for vpn_banner_link`);

  // VPN Banner flips this pref each time it shows, flip back between each instruction.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.contentblocking.report.hide_vpn_banner", false]],
  });

  await BrowserTestUtils.reloadTab(tab);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    const bannerExitLink = await ContentTaskUtils.waitForCondition(() => {
      return content.document.querySelector(".vpn-banner .exit-icon");
    }, "vpn banner exit link exists");
    await ContentTaskUtils.waitForCondition(
      () => ContentTaskUtils.isVisible(bannerExitLink),
      "vpn banner exit link is visible"
    );
    EventUtils.sendMouseEvent(
      { type: "click", button: 1 },
      bannerExitLink,
      content
    );
  });

  events = await waitForProtectionsEvents("clickVpnBannerClose", 1);
  is(events.length, 1, `recorded telemetry for vpn_banner_close`);

  // Clean up.
  await BrowserTestUtils.removeTab(tab);
}).skip();
