/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Test that the "Report broken site" row of the redesigned Trust Panel's
 * "Fix site issue" subview opens Report Broken Site inside the Trust Panel.
 */

/* import-globals-from send_more_info.js */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "send_more_info.js",
  this
);

add_common_setup();

const { EnterprisePolicyTesting, PoliciesPrefTracker } =
  ChromeUtils.importESModule(
    "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
  );

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.protectionsPanelRedesign.enabled", true]],
  });
});

add_task(async function testOpensInTrustPanel() {
  ensureReportBrokenSitePreffedOn();
  Services.fog.testResetFOG();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    const trustPanel = TrustPanel();
    let rbs = await trustPanel.openReportBrokenSiteToDetailsPanel({
      url: "https://test.org/test/#fake",
      description: "test description",
    });
    is(
      rbs.mainView.closest("panelmultiview").id,
      "trustpanel-popup-multiView",
      "Report Broken Site is shown inside the Trust Panel"
    );
    Assert.deepEqual(
      Glean.webcompatreporting.opened.testGetValue().map(e => e.extra),
      [{ source: "trustPanel" }],
      "Opening is recorded with the Trust Panel as source"
    );

    await rbs.clickCancel();
    ok(!trustPanel.opened, "Cancel closes the Trust Panel");

    rbs = await trustPanel.openReportBrokenSite();
    rbs.isProperlyReset();
    await trustPanel.close();
  });
});

add_task(async function testReportSentClosingReason() {
  ensureReportBrokenSitePreffedOn();
  Services.fog.testResetFOG();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    const trustPanel = TrustPanel();
    let rbs = await trustPanel.openReportBrokenSiteToDetailsPanel({
      url: "https://test.org/test/#fake",
      description: "test description",
    });
    await rbs.clickCancel();

    rbs = await trustPanel.openReportBrokenSiteToDetailsPanel({
      url: "https://test.org/test/#fake",
      description: "test description",
    });
    await rbs.clickSend();
    const hidden = BrowserTestUtils.waitForPopupEvent(
      trustPanel.popup,
      "hidden"
    );
    await rbs.clickOkay();
    await hidden;

    Assert.deepEqual(
      Glean.trustpanel.closed.testGetValue().map(e => e.extra.closing_reason),
      ["dismissed", "reportSent"],
      "Cancelling records dismissed, sending records reportSent"
    );
  });
});

add_task(async function testDisabledOnUnreportablePage() {
  ensureReportBrokenSitePreffedOn();
  const trustPanel = TrustPanel();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    await trustPanel.open();
    isMenuItemEnabled(trustPanel.reportBrokenSite, "on a reportable page");
    await trustPanel.close();
  });

  await withNewTab("about:blank", async (_, tab) => {
    ReportBrokenSite.enableOrDisableMenuitems(tab.linkedBrowser);
    isMenuItemDisabled(trustPanel.reportBrokenSite, "on about:blank");
  });
});

add_task(async function testFallbackWhenPreffedOff() {
  ensureReportBrokenSitePreffedOff();

  await withNewTab(REPORTABLE_PAGE_URL, async (_, tab) => {
    await testWebcompatComFallback(tab, TrustPanel());
  });

  ensureReportBrokenSitePreffedOn();
});

add_task(async function testHiddenByPolicy() {
  ensureReportBrokenSitePreffedOn();
  const trustPanel = TrustPanel();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    await trustPanel.open();
    isMenuItemEnabled(trustPanel.reportBrokenSite, "without the policy");
    await trustPanel.close();
  });

  PoliciesPrefTracker.start();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: { DisableFeedbackCommands: true },
  });

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    await trustPanel.open();
    isMenuItemHidden(
      trustPanel.reportBrokenSite,
      "when disabled by the DisableFeedbackCommands policy"
    );
    await trustPanel.close();
  });

  PoliciesPrefTracker.stop();
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
});
