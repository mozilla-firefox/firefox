/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Test to click on all clickable elements to let a11y checks run on them. */

"use strict";

add_common_setup();

add_setup(setupBuiltInTestTrackers);

async function checkMainPanel(menu) {
  let rbs = await menu.openReportBrokenSite();

  // a11y for the URL widgets is already tested in browser_url_widgets.

  // click on all the reason buttons except deceptive site, which is special.
  for (const reasonButton of rbs.reasonButtons.filter(
    e => !e.id.includes("deceptive")
  )) {
    await rbs.clickAndWaitForViewToShow(reasonButton, rbs.detailsView);
    await rbs.clickBack();
  }

  await rbs.clickDeceptiveSiteReport();

  // the learn more link is tested in browser_learn_more_link.js.js#ensureLearnMoreLinkWorks.

  await rbs.close();
}

async function checkDetailsPanel(menu) {
  let rbs = await menu.openReportBrokenSiteToDetailsPanel();

  // a11y for the URL widgets is already tested in browser_url_widgets.

  // check the description/comment box.
  rbs.clickAndWaitForEvent(rbs.descriptionTextarea, "focus");

  // check the blocked trackers toggle.
  if (rbs.hasBlockedTrackers) {
    await isVisible(
      rbs.blockedTrackersToggle,
      "blocked trackers toggle is visible"
    );
    rbs.click(rbs.blockedTrackersToggle);
    await isPressed(rbs.blockedTrackersToggle, "blocked trackers toggle works");
  }

  // check the screenshot toggle.
  if (rbs.hasScreenshot) {
    await isVisible(rbs.screenshotToggle, "blocked trackers toggle is visible");
    rbs.click(rbs.screenshotToggle);
    await isPressed(rbs.screenshotToggle, "screenshot toggle works");
  }

  // send more info is tested in browser_send_more_info.js#testSendingMoreInfo.

  // check the preview report, cancel, and send buttons.
  await rbs.clickPreview();
  await rbs.clickBack();
  await rbs.clickCancel();
  rbs = await menu.openReportBrokenSiteToDetailsPanel();
  await rbs.clickSend();
  await rbs.clickOkay();
}

async function checkPreviewPanel(menu) {
  let rbs = await menu.openReportBrokenSiteToDetailsPanel();
  await rbs.clickPreview();
  await rbs.clickCancel();

  rbs = await menu.openReportBrokenSiteToDetailsPanel();
  await rbs.clickPreview();
  await rbs.clickSend();
  await rbs.clickOkay();
}

add_task(async function testAllClickableElementsPassA11YChecks() {
  ensureReportBrokenSitePreffedOn();
  enableScreenshots();
  enableSendMoreInfo();

  // the fallback for sending Report Broken Site to webcompat.com is tested in browser_webcompat.com_fallback.js.

  await withNewTab(REPORTABLE_PAGE_URL3, async win => {
    for (const menu of [AppMenu(win), ProtectionsPanel(win), HelpMenu(win)]) {
      await this.checkMainPanel(menu);
      await this.checkDetailsPanel(menu);
      await this.checkPreviewPanel(menu);
    }
  });
});
