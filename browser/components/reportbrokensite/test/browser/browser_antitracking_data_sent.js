/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Tests to ensure that the right data is sent for
 * private windows and when ETP blocks content.
 */

/* import-globals-from send.js */
/* import-globals-from send_more_info.js */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "send_more_info.js",
  this
);

add_common_setup();

add_task(setupStrictETP);

function getEtpCategory() {
  return Services.prefs.getStringPref(
    "browser.contentblocking.category",
    "standard"
  );
}

add_task(async function testSendButton() {
  ensureReportBrokenSitePreffedOn();

  await withNewTab(REPORTABLE_PAGE_URL3, async (win, tab) => {
    await testSend(tab, AppMenu(win), {
      breakageCategory: "adblocker",
      description: "another test description",
      antitracking: {
        blockList: "strict",
        blockedOrigins: null,
        isPrivateBrowsing: true,
        hasTrackingContentBlocked: true,
        hasMixedActiveContentBlocked: true,
        hasMixedDisplayContentBlocked: true,
        btpHasPurgedSite: false,
        btpPurgeHistory: [],
        etpCategory: getEtpCategory(),
      },
      frameworks: {
        fastclick: true,
        marfeel: true,
        mobify: true,
      },
    });
  });
});

add_task(async function testSendingMoreInfo() {
  ensureReportBrokenSitePreffedOn();
  enableSendMoreInfo();

  await withNewTab(REPORTABLE_PAGE_URL3, async (win, tab) => {
    await testSendMoreInfo(tab, AppMenu(win), {
      antitracking: {
        blockList: "strict",
        blockedOrigins: ["https://trackertest.org"],
        isPrivateBrowsing: true,
        hasTrackingContentBlocked: true,
        hasMixedActiveContentBlocked: true,
        hasMixedDisplayContentBlocked: true,
        btpHasPurgedSite: false,
        btpPurgeHistory: [],
        etpCategory: getEtpCategory(),
      },
      frameworks: { fastclick: true, mobify: true, marfeel: true },
    });
  });
});
