"use strict";

// This tests searching in the urlbar (a.k.a. the quantumbar).

/**
 * WHOA THERE: We should never be adding new things to
 * EXPECTED_REFLOWS_FIRST_OPEN or EXPECTED_REFLOWS_SECOND_OPEN.
 * Instead of adding reflows to these lists, you should be modifying your code
 * to avoid the reflow.
 *
 * See https://firefox-source-docs.mozilla.org/performance/bestpractices.html
 * for tips on how to do that.
 */

/* These reflows happen only the first time the panel opens. */
const EXPECTED_REFLOWS_FIRST_OPEN = [];

/* These reflows happen every time the panel opens. */
const EXPECTED_REFLOWS_SECOND_OPEN = [];

add_setup(async function () {
  const { AboutNewTab } = ChromeUtils.importESModule(
    "resource:///modules/AboutNewTab.sys.mjs"
  );

  // runUrlbarTest clears the input before each observed opening, so the panel
  // is measured showing Top Sites. Tests get none by default, and without a
  // result the view stays closed and the geometry reads reflow instead.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
      ["browser.newtabpage.activity-stream.feeds.system.topsites", false],
      ["browser.newtabpage.activity-stream.feeds.system.topsites", true],
    ],
  });

  // The row is filled asynchronously from the pref, so wait for it rather than
  // racing the feed.
  await TestUtils.waitForCondition(
    () => AboutNewTab.getTopSites().length,
    "Waiting for the Top Sites row to be populated"
  );
});

add_task(async function quantumbar() {
  await runUrlbarTest(
    false,
    EXPECTED_REFLOWS_FIRST_OPEN,
    EXPECTED_REFLOWS_SECOND_OPEN
  );
});
