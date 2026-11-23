/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test the Delete site data subview of the redesigned trust panel.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SiteDataTestUtils: "resource://testing-common/SiteDataTestUtils.sys.mjs",
});

const TEST_ORIGIN = "https://example.com";
const REDESIGN_PREF = "browser.protectionsPanelRedesign.enabled";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: [[REDESIGN_PREF, true]] });
  registerCleanupFunction(async () => {
    await SiteDataTestUtils.clear();
  });
});

function eventCount(name) {
  return Glean.trustpanel[name].testGetValue()?.length ?? 0;
}

async function openTabWithSiteData(name) {
  SiteDataTestUtils.addToCookies({ origin: TEST_ORIGIN, name, value: "1" });
  return BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: TEST_ORIGIN,
    waitForLoad: true,
  });
}

async function openClearSiteDataSubview(viewId) {
  await UrlbarTestUtils.openTrustPanel(window);
  // The redesigned main view (bug 2084203) has its own button; until it
  // lands, the current main view's button opens the redesigned subview.
  let button =
    (Services.prefs.getBoolPref(REDESIGN_PREF) &&
      document.getElementById("trustpanel-delete-site-data-button")) ||
    document.getElementById("trustpanel-clear-cookies-button");
  await TestUtils.waitForCondition(
    () => !button.disabled,
    "Waiting for the delete site data button to be enabled"
  );
  let view = document.getElementById(viewId);
  let shown = BrowserTestUtils.waitForEvent(view, "ViewShown");
  EventUtils.synthesizeMouseAtCenter(button, {}, window);
  await shown;
  return view;
}

async function clickAndWaitForPanelToClose(id) {
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(document.getElementById(id), {}, window);
  await hidden;
}

add_task(async function test_redesign_view_content() {
  const tab = await openTabWithSiteData("test-content");

  let view = await openClearSiteDataSubview(
    "trustpanel-clearcookiesView-redesign"
  );
  Assert.deepEqual(
    document.l10n.getAttributes(view),
    {
      id: "trustpanel-clear-cookies-header-redesign",
      args: { host: "example.com" },
    },
    "The subview title names the site"
  );
  await TestUtils.waitForCondition(
    () => view.getAttribute("title") == "Delete site data for example.com",
    "Waiting for the subview title to be translated"
  );
  Assert.equal(
    view.querySelector(".trustpanel-section > label").dataset.l10nId,
    "trustpanel-clear-cookies-description-redesign",
    "The subview uses the redesigned description"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById("trustpanel-clearcookiesView")
    ),
    "The previous design's subview is not shown"
  );
  Assert.ok(
    view.hasAttribute("shrink-to-fit"),
    "The subview opts out of the main view height"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_redesign_cancel_keeps_site_data() {
  Services.fog.testResetFOG();
  const tab = await openTabWithSiteData("test-cancel");

  await openClearSiteDataSubview("trustpanel-clearcookiesView-redesign");
  Assert.equal(eventCount("clearCookiesOpened"), 1, "Opening is recorded");

  await clickAndWaitForPanelToClose("trustpanel-clear-cookie-cancel-redesign");
  Assert.equal(eventCount("clearCookiesCancelled"), 1, "Cancel is recorded");
  Assert.equal(eventCount("clearCookiesConfirmed"), 0, "Nothing was cleared");
  Assert.ok(
    SiteDataTestUtils.hasCookies(TEST_ORIGIN),
    "Cancelling leaves the site data intact"
  );

  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_redesign_clear_removes_site_data() {
  Services.fog.testResetFOG();
  const tab = await openTabWithSiteData("test-clear");

  await openClearSiteDataSubview("trustpanel-clearcookiesView-redesign");
  await clickAndWaitForPanelToClose("trustpanel-clear-cookie-clear-redesign");
  Assert.equal(eventCount("clearCookiesConfirmed"), 1, "Clearing is recorded");
  await TestUtils.waitForCondition(
    () => !SiteDataTestUtils.hasCookies(TEST_ORIGIN),
    "Waiting for the site data to be removed"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_pref_off_shows_previous_design() {
  await SpecialPowers.pushPrefEnv({ set: [[REDESIGN_PREF, false]] });
  const tab = await openTabWithSiteData("test-pref-off");

  await openClearSiteDataSubview("trustpanel-clearcookiesView");
  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById("trustpanel-clearcookiesView-redesign")
    ),
    "The redesigned subview is not shown with the pref off"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await SpecialPowers.popPrefEnv();
  await SiteDataTestUtils.clear();
  BrowserTestUtils.removeTab(tab);
});
