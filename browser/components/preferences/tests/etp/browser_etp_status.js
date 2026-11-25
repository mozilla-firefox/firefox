/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TRACKER_COUNT_PREF = "browser.urlbar.trackerCount.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Ensure we start from ETP "standard".
      ["browser.contentblocking.category", "standard"],
    ],
  });
});

/**
 * Helper for waiting for and asserting the ETP status item state.
 * Can be called both when the item already changed or when we need to wait for the change.
 *
 * @param {Document} doc - The preferences document.
 * @param {*} options - The expected ETP status item state.
 * @param {string} options.l10nId - The expected l10nId attribute value.
 * @param {string} options.description - The expected description attribute value.
 */
async function waitForAndAssertEtpStatusItemState(doc, { l10nId }) {
  let etpStatusItem = doc.getElementById("etpStatusItem");
  Assert.ok(
    BrowserTestUtils.isVisible(etpStatusItem),
    "ETP status box item should be visible"
  );

  let condition = () => {
    return etpStatusItem.getAttribute("data-l10n-id") == l10nId;
  };
  if (!condition()) {
    await BrowserTestUtils.waitForMutationCondition(
      etpStatusItem,
      { attributes: true, attributeFilter: ["data-l10n-id", "description"] },
      condition
    );
  }

  Assert.equal(etpStatusItem.getAttribute("data-l10n-id"), l10nId);
}

// Test that the ETP status section updates correctly when changing the ETP category.
add_task(async function test_status_categories() {
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.contentDocument;

  await waitForAndAssertEtpStatusItemState(doc, {
    l10nId: "preferences-etp-level-standard",
  });

  info("Switch to ETP `strict`");
  Services.prefs.setStringPref("browser.contentblocking.category", "strict");

  await waitForAndAssertEtpStatusItemState(doc, {
    l10nId: "preferences-etp-level-strict",
  });

  info("Switch to ETP `custom`");
  Services.prefs.setStringPref("browser.contentblocking.category", "custom");

  await waitForAndAssertEtpStatusItemState(doc, {
    l10nId: "preferences-etp-level-custom",
  });

  info("Switch back to default ETP `standard`");
  Services.prefs.clearUserPref("browser.contentblocking.category");

  await waitForAndAssertEtpStatusItemState(doc, {
    l10nId: "preferences-etp-level-standard",
  });

  gBrowser.removeCurrentTab();
});

add_task(async function test_tracker_count_toggle() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      // The feature gate ships disabled by default (bug 2065206); enable it so
      // the toggle is shown.
      ["browser.urlbar.trackerCount.featureGate", true],
      [TRACKER_COUNT_PREF, true],
    ],
  });
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.contentDocument;

  let toggle = doc.getElementById("etpTrackerCountEnabled");
  Assert.ok(
    BrowserTestUtils.isVisible(toggle),
    "Tracker count toggle is visible"
  );
  Assert.ok(toggle.checked, "Toggle is on when pref is true");

  info("Click the toggle to disable the pref");
  let prefChange = waitForAndAssertPrefState(
    TRACKER_COUNT_PREF,
    false,
    "Pref set to false after toggling off"
  );
  synthesizeClick(toggle);
  await prefChange;
  Assert.ok(!toggle.checked, "Toggle is off after clicking");
  Assert.deepEqual(
    Glean.privacyUiEtp.trackerCountToggled
      .testGetValue()
      ?.map(event => event.extra),
    [{ enabled: "false" }],
    "Disabling the tracker count is recorded"
  );

  info("Click the toggle to enable the pref again");
  prefChange = waitForAndAssertPrefState(
    TRACKER_COUNT_PREF,
    true,
    "Pref set to true after toggling on"
  );
  synthesizeClick(toggle);
  await prefChange;
  Assert.ok(toggle.checked, "Toggle is on after clicking");
  Assert.deepEqual(
    Glean.privacyUiEtp.trackerCountToggled
      .testGetValue()
      ?.map(event => event.extra),
    [{ enabled: "false" }, { enabled: "true" }],
    "Enabling the tracker count is recorded"
  );

  await SpecialPowers.popPrefEnv();
  gBrowser.removeCurrentTab();
});

add_task(async function test_tracker_count_hidden_when_gate_disabled() {
  await ExperimentAPI.ready();
  const doCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "urlbar",
    value: { trackerCountFeatureGate: false },
  });

  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.contentDocument;

  await TestUtils.waitForCondition(
    () => doc.querySelector('setting-group[groupid="etpStatus"]'),
    "Wait for etpStatus setting group"
  );

  let toggle = doc.getElementById("etpTrackerCountEnabled");
  ok(toggle, "Tracker count toggle exists in the DOM");
  is_element_hidden(
    toggle,
    "Tracker count toggle is hidden when feature gate is off"
  );

  gBrowser.removeCurrentTab();
  await doCleanup();
});

// Test that the protections dashboard link in the ETP status section opens the about:protections page.
add_task(async function test_protections_dashboard_link() {
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.contentDocument;

  let protectionsDashboardLink = doc.getElementById("protectionsDashboardLink");

  Assert.ok(
    BrowserTestUtils.isVisible(protectionsDashboardLink),
    "Protections dashboard link is visible."
  );

  let linkPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "about:protections"
  );
  protectionsDashboardLink.scrollIntoView();
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#protectionsDashboardLink",
    {},
    gBrowser.selectedBrowser
  );
  let tab = await linkPromise;

  Assert.equal(
    tab.linkedBrowser.currentURI.spec,
    "about:protections",
    "Protections dashboard link opened the about:protections page."
  );

  // about:protections tab.
  BrowserTestUtils.removeTab(tab);
  // Preferences tab.
  gBrowser.removeCurrentTab();
});
