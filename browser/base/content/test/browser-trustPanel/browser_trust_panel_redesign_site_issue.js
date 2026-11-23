/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test the "Site not working?" subview opened by the "Fix site issue"
 * button of the redesigned trust panel.
 */

"use strict";

const VIEW_ID = "trustpanel-siteIssueView";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.protectionsPanelRedesign.enabled", true],
      ["datareporting.healthreport.uploadEnabled", true],
      ["ui.new-webcompat-reporter.enabled", true],
    ],
  });
});

async function openSiteIssueSubview() {
  await UrlbarTestUtils.openTrustPanel(window);
  let view = document.getElementById(VIEW_ID);
  let shown = BrowserTestUtils.waitForEvent(view, "ViewShown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("trustpanel-fix-site-issue-button"),
    {},
    window
  );
  await shown;
  return view;
}

add_task(async function test_site_issue_view_content() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  let view = await openSiteIssueSubview();
  await TestUtils.waitForCondition(
    () => view.getAttribute("title") == "Site not working? Try these steps.",
    "Waiting for the subview title to be translated"
  );
  Assert.deepEqual(
    [...view.querySelectorAll("#trustpanel-site-issue-steps-list > li")].map(
      li => li.dataset.l10nId
    ),
    [
      "trustpanel-site-issue-step-extensions",
      "trustpanel-site-issue-step-vpn",
      "trustpanel-site-issue-step-etp",
    ],
    "The steps list extensions, the VPN and tracking protection"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("trustpanel-site-issue-learn-more")
    ),
    "The Learn more link is shown"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("trustpanel-report-broken-site-button")
    ),
    "The Report broken site row is shown"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_site_issue_opened_telemetry() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });
  Services.fog.testResetFOG();

  await openSiteIssueSubview();
  Assert.equal(
    Glean.trustpanel.siteIssueOpened.testGetValue()?.length,
    1,
    "Opening the subview is recorded once"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_vpn_step_follows_availability() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });
  let vpnStep = () => document.getElementById("trustpanel-site-issue-step-vpn");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", false]],
  });
  await openSiteIssueSubview();
  Assert.ok(
    BrowserTestUtils.isHidden(vpnStep()),
    "The VPN step is hidden without the built-in VPN"
  );
  await UrlbarTestUtils.closeTrustPanel(window);
  await SpecialPowers.popPrefEnv();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });
  await openSiteIssueSubview();
  Assert.ok(
    BrowserTestUtils.isVisible(vpnStep()),
    "The VPN step is shown with the built-in VPN"
  );
  await UrlbarTestUtils.closeTrustPanel(window);
  await SpecialPowers.popPrefEnv();

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_report_row_opens_report_broken_site() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await openSiteIssueSubview();
  let rbsView = PanelMultiView.getViewNode(
    document,
    "report-broken-site-popup-mainView"
  );
  let shown = BrowserTestUtils.waitForEvent(rbsView, "ViewShown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("trustpanel-report-broken-site-button"),
    {},
    window
  );
  await shown;
  Assert.equal(
    rbsView.closest("panelmultiview").id,
    "trustpanel-popup-multiView",
    "Report Broken Site opens inside the trust panel"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});
