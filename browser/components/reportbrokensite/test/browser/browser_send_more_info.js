/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Tests that the send more info link appears only when its pref
 * is set to true, and that when clicked it will open a tab to
 * the webcompat.com endpoint and send the right data.
 */

/* import-globals-from send_more_info.js */

"use strict";

const VIDEO_URL = `${BASE_URL}/videotest.mp4`;

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "send_more_info.js",
  this
);

add_common_setup();

requestLongerTimeout(2);

add_task(async function testSendMoreInfoPref() {
  ensureReportBrokenSitePreffedOn();

  await withNewTab(REPORTABLE_PAGE_URL, async function () {
    await navigateOnTab(gBrowser.selectedTab, REPORTABLE_PAGE_URL);

    disableSendMoreInfo();
    let rbs = await AppMenu().openReportBrokenSiteToDetailsPanel();
    await isHidden(
      rbs.sendMoreInfoButton,
      "send more info is not visible if preffed off"
    );
    await rbs.close();

    enableSendMoreInfo();
    rbs = await AppMenu().openReportBrokenSiteToDetailsPanel();
    await isNotHidden(
      rbs.sendMoreInfoButton,
      "send more info is visible if preffed on"
    );
    await BrowserTestUtils.waitForMutationCondition(
      rbs.sendMoreInfoButton,
      { attributes: true, attributeFilter: ["label"] },
      () => rbs.sendMoreInfoButton.label
    );
    is(
      rbs.sendMoreInfoButton.label.replace(/[⁨⁩]/g, ""),
      "Send info via webcompat.com",
      "send more info names webcompat.com"
    );
    await rbs.close();
  });
});

add_task(async function testSendingMoreInfo() {
  ensureReportBrokenSitePreffedOn();
  enableSendMoreInfo();
  enableScreenshots();

  await withNewTab(REPORTABLE_PAGE_URL, async (win, tab) => {
    await testSendMoreInfo(tab, AppMenu(win));
    await navigateOnTab(tab, REPORTABLE_PAGE_URL2);

    await testSendMoreInfo(tab, ProtectionsPanel(), {
      url: "https://override.com",
      description: "another test description",
      expectNoTabDetails: true,
    });

    // also load a video to ensure system codec
    // information is loaded and properly sent
    // and test that screenshots aren't included
    // if the opt-out is toggled.
    await withNewTab(VIDEO_URL, async (win2, tab2) => {
      await testSendMoreInfo(tab2, HelpMenu(win2), { screenshotOptOut: true });
    });
  });
});
