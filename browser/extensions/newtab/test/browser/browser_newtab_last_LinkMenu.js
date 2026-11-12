/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function setupPrefs() {
  sinon
    .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
    .returns(
      "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
    );
  await setDefaultTopSites();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.newtabpage.activity-stream.feeds.discoverystreamfeed", true],
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({
          collapsible: true,
          enabled: true,
          personalized: false,
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
    ],
  });
}

async function resetPrefs() {
  // setupPrefs pushes 3 pref environments: 1 from its own pushPrefEnv and
  // 2 inside setDefaultTopSites.
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
}

let initialHeight;
let initialWidth;
// Sizes the inner rather than the outer window: the window decoration
// in between varies by OS and pixel density, so a fixed outer size gives a
// different viewport per platform.
async function setSize(width, height) {
  initialWidth ??= window.innerWidth;
  initialHeight ??= window.innerHeight;
  if (window.innerWidth === width && window.innerHeight === height) {
    return;
  }
  let resizePromise = BrowserTestUtils.waitForEvent(window, "resize", false);
  window.resizeTo(
    width + window.outerWidth - window.innerWidth,
    height + window.outerHeight - window.innerHeight
  );
  await resizePromise;
}

// The first size setSize saw is the one the file found; the rest are its own.
function resetSize() {
  return setSize(initialWidth, initialHeight);
}

add_task(async function test_newtab_last_LinkMenu() {
  await setupPrefs();

  // Open about:newtab without using the default load listener
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );

  // Specially wait for potentially preloaded browsers
  let browser = tab.linkedBrowser;
  await waitForPreloaded(browser);

  // Wait for React to render something
  await TestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(
        browser,
        [],
        () => content.document.getElementById("root")?.children.length
      ),
    "Should render activity stream content"
  );

  // Top sites and stories sit at different places in the layout, so each needs
  // its own width to put its menu at the edge. Top sites must also clear
  // $break-point-large (866px) for open-left to match the rendered columns.
  const topSitesWidth = 900;
  const storiesWidth = 740;

  await setSize(topSitesWidth, 450);

  // Test context menu position for topsites.
  await SpecialPowers.spawn(browser, [], async () => {
    // The subject is the rightmost top site of the row, whose menu has to open
    // to the left. Found rather than counted to, because the placeholders, the
    // add-shortcut tile and the search shortcut have no menu to open.
    const lastTileWithMenu = () => {
      const tiles = [
        ...content.document.querySelectorAll(
          ".top-site-outer:not(.placeholder, .add-button-tile, .search-shortcut)"
        ),
      ].filter(
        tile =>
          tile.querySelector(".context-menu-button") &&
          tile.getBoundingClientRect().width
      );
      return tiles[tiles.length - 1];
    };
    await ContentTaskUtils.waitForCondition(
      lastTileWithMenu,
      "Wait for the topsite cards to render"
    );
    const topsiteOuter = lastTileWithMenu();
    const topsiteContextMenuButton = topsiteOuter.querySelector(
      ".context-menu-button"
    );

    topsiteContextMenuButton.click();

    await ContentTaskUtils.waitForCondition(
      () => topsiteOuter.classList.contains("active"),
      "Wait for the topsite menu to be active"
    );

    is(
      content.window.scrollMaxX,
      0,
      "there should be no horizontal scroll bar"
    );

    // Close the topsite menu before the story-card block below. Both menus are
    // now panel-list popovers; opening a second auto-popover light-dismisses the
    // first, so we check each menu's positioning in isolation.
    topsiteContextMenuButton.click();
    await ContentTaskUtils.waitForCondition(
      () => !topsiteOuter.classList.contains("active"),
      "Wait for the topsite menu to close"
    );
  });

  if (storiesWidth !== topSitesWidth) {
    await setSize(storiesWidth, 450);
  }

  // Test context menu position for topstories.
  await SpecialPowers.spawn(browser, [], async () => {
    // Pocket section might take a bit more time to load,
    // so wait for the button to be ready.
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          ".ds-card:nth-child(1n) .context-menu-button"
        ),
      "Wait for the story card and button"
    );

    const dsCard = content.document.querySelector(".ds-card:nth-child(1n)");
    const dsCarContextMenuButton = dsCard.querySelector(".context-menu-button");

    dsCarContextMenuButton.click();

    await ContentTaskUtils.waitForCondition(
      () => dsCard.classList.contains("active"),
      "Wait for the story menu to be active"
    );

    is(
      content.window.scrollMaxX,
      0,
      "there should be no horizontal scroll bar"
    );
  });

  // Resetting the window size to what it was.
  await resetSize();
  // Resetting prefs we set for this test.
  await resetPrefs();
  BrowserTestUtils.removeTab(tab);
  sinon.restore();
});
