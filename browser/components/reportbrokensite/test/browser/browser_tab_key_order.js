/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* Tests of the expected tab key element focus order */

"use strict";

add_common_setup();

add_setup(setupBuiltInTestTrackers);

async function ensureTabOrder(rbs, order, expectBackButton = true) {
  let foundBackAlready = false;
  for (let matches of order) {
    // We need to tab through all elements in each match array in any order
    if (!Array.isArray(matches)) {
      matches = [matches];
    }
    let matchesLeft = matches.length;
    while (matchesLeft--) {
      const target = await rbs.pressKeyAndGetFocus("VK_TAB");
      if (target.matches(".subviewbutton-back") && !foundBackAlready) {
        if (!expectBackButton) {
          throw new Error("Found an unexpected back button");
        }
        foundBackAlready = true;
        matchesLeft++;
        continue;
      }
      let foundMatch = false;
      for (const [i, selector] of matches.entries()) {
        foundMatch = selector && target.matches(selector);
        if (foundMatch) {
          matches[i] = "";
          break;
        }
      }
      ok(
        foundMatch,
        `Expected [${matches}] next, got ${target.nodeName}(id=${target.id}, class=${target.className})`
      );
      if (!foundMatch) {
        return false;
      }
    }
  }
  if (!foundBackAlready && expectBackButton) {
    const target = await rbs.pressKeyAndGetFocus("VK_TAB");
    ok(target.matches(".subviewbutton-back"), "Ended on the back button");
  }
  return true;
}

async function checkMainPanel(rbs, { expectBackButton }) {
  return ensureTabOrder(
    rbs,
    [
      "url-input",
      [
        "account",
        "adblocker",
        "checkout",
        "content",
        "deceptive",
        "load",
        "media",
        "notsupported",
        "other",
        "slow",
      ].map(reason => `#report-broken-site-popup-reason-${reason}`),
      "#report-broken-site-popup-learn-more-link",
    ],
    expectBackButton
  );
}

function maybe(test, val) {
  return test ? [val] : [];
}

async function checkDetailsPanel(
  rbs,
  {
    expectSendMoreInfo,
    expectScreenshotToggle,
    expectBlockedTrackersToggle,
  } = {}
) {
  return ensureTabOrder(rbs, [
    "url-input",
    "#report-broken-site-popup-description",
    ...maybe(
      expectBlockedTrackersToggle,
      "#report-broken-site-popup-blocked-trackers-toggle"
    ),
    ...maybe(
      expectScreenshotToggle,
      "#report-broken-site-popup-screenshot-toggle"
    ),
    ...maybe(
      expectSendMoreInfo,
      "#report-broken-site-popup-send-more-info-button"
    ),
    "#report-broken-site-popup-preview-button",
    // moz-button-groups swap the order of buttons to follow
    // platform conventions, so the order of send/cancel will vary.
    [
      "#report-broken-site-popup-details-cancel-button",
      "#report-broken-site-popup-send-button",
    ],
  ]);
}

async function checkPreviewPanel(rbs, { expectedSummaries }) {
  return ensureTabOrder(rbs, [
    ".preview-basic summary",
    ".data",
    ...expectedSummaries.map(name => `.preview-${name} summary`),
    [
      // moz-button-groups swap the order of buttons to follow
      // platform conventions, so the order of send/cancel will vary.
      "#report-broken-site-popup-preview-cancel-button",
      "#report-broken-site-popup-preview-send-button",
    ],
  ]);
}

async function testTabOrder(menu, expectBlockedTrackersToggle = false) {
  // enable screenshots early enough so that a screenshot is actually taken.
  enableScreenshots();

  const rbs = await menu.openReportBrokenSite();

  // The number of summary elements can vary (on Windows there may be security details)
  const expectedSummaries = Object.keys(await rbs.reportData());

  const expectBackButton = menu.showsBackButton;
  await checkMainPanel(rbs, { expectBackButton });

  // Expect screenshots and send more info to be accessible if the prefs are enabled.
  await rbs.clickReason("load");
  enableSendMoreInfo();
  await isVisible(rbs.screenshotToggle);
  await isVisible(rbs.sendMoreInfoButton);
  await checkDetailsPanel(rbs, {
    expectSendMoreInfo: true,
    expectScreenshotToggle: true,
    expectBlockedTrackersToggle,
  });

  // If no screenshot could be taken, it should be hidden and therefore inaccessible.
  // If send more info is preffed off, it should likewise be hidden and inaccessible.
  rbs.screenshot = undefined;
  disableSendMoreInfo();
  await isNotVisible(rbs.screenshotToggle);
  await isNotVisible(rbs.sendMoreInfoButton);
  await checkDetailsPanel(rbs, {
    expectSendMoreInfo: false,
    expectScreenshotToggle: false,
    expectBlockedTrackersToggle,
  });
  await rbs.clickPreview();
  await checkPreviewPanel(rbs, {
    expectedSummaries,
  });

  // If there is a screenshot, but the pref for screenshots is off, it should be hidden.
  await rbs.clickBack();
  rbs.screenshot = "data:";
  disableScreenshots();
  await isNotVisible(rbs.screenshotToggle);
  await checkDetailsPanel(rbs, {
    expectSendMoreInfo: false,
    expectScreenshotToggle: false,
    expectBlockedTrackersToggle,
  });
  await rbs.clickPreview();
  await checkPreviewPanel(rbs, {
    expectedSummaries,
  });

  await rbs.close();
}

add_task(async function testTabOrdering() {
  ensureReportBrokenSitePreffedOn();

  await withNewTab(REPORTABLE_PAGE_URL, async () => {
    await testTabOrder(AppMenu());
    await testTabOrder(ProtectionsPanel());
    await testTabOrder(HelpMenu());
  });

  // Also test an origin with blocked trackers.
  await withNewTab(REPORTABLE_PAGE_URL3, async win => {
    await testTabOrder(AppMenu(win), true);
    await testTabOrder(ProtectionsPanel(win), true);
    await testTabOrder(HelpMenu(win), true);
  });
});
