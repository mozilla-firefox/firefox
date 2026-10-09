"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DEFAULT_TOP_SITES: "resource://newtab/lib/TopSitesFeed.sys.mjs",
});

async function newtabWithSponsoredTopsites(callback = () => {}) {
  // Open about:newtab without using the default load listener
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );

  // Specially wait for potentially preloaded browsers
  let browser = tab.linkedBrowser;
  await waitForPreloaded(browser);

  // Force TopSitesFeed to re-broadcast its state to the now-connected content
  // process. The initial broadcast from add_setup may have fired before this
  // tab's content process was registered to receive messages.
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.showSponsoredTopSites",
    false
  );
  Services.prefs.setBoolPref(
    "browser.newtabpage.activity-stream.showSponsoredTopSites",
    true
  );

  // Wait for the re-broadcast to conclude and for sponsored topsites to render.
  await BrowserTestUtils.waitForCondition(
    () =>
      SpecialPowers.spawn(
        browser,
        [],
        () =>
          content.document.querySelector(
            '.top-sites [data-is-sponsored-link="true"]'
          ) !== null
      ),
    "Should find sponsored topsites after pref re-broadcast"
  );

  try {
    await SpecialPowers.spawn(browser, [], callback);
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
}

add_setup(async function () {
  lazy.DEFAULT_TOP_SITES.push(
    {
      isDefault: true,
      url: "https://example.com/",
      hostname: "example",
      label: "Example Sponsor",
      show_sponsored_label: true,
      sponsored_position: 1,
      sponsored_tile_id: "test-tile-1",
      partner: "test",
      block_key: "test-tile-1",
      faviconSize: 16,
    },
    {
      isDefault: true,
      url: "https://example2.com/",
      hostname: "example2",
      label: "Example Sponsor",
      show_sponsored_label: true,
      sponsored_position: 2,
      sponsored_tile_id: "test-tile-2",
      partner: "test",
      block_key: "test-tile-2",
      faviconSize: 16,
    }
  );

  await pushPrefs([
    "browser.newtabpage.activity-stream.showSponsoredTopSites",
    true,
  ]);
  let topSitesFeed = AboutNewTab.activityStream.store.feeds.get(
    "feeds.system.topsites"
  );
  await topSitesFeed.refresh({ broadcast: true });

  registerCleanupFunction(async () => {
    lazy.DEFAULT_TOP_SITES.length = 0;
  });
});

add_task(async function test_dismiss() {
  await newtabWithSponsoredTopsites(async () => {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector(
          '.top-sites [data-is-sponsored-link="true"]'
        ),
      "Should find a visible sponsored topsite"
    );

    // Count the sponsored tiles rather than the whole row, which also depends
    // on the search engine, on what earlier tests pinned, and on whether there
    // is a free cell for the add-shortcut tile.
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelectorAll(
          '.top-sites [data-is-sponsored-link="true"]'
        ).length === 2,
      "Wait for 2 sponsored top sites"
    );

    const contextMenuDiv = content.document.querySelector(
      '.top-sites [data-is-sponsored-link="true"] + div'
    );

    const contextMenuButton = contextMenuDiv.querySelector(
      ".context-menu-button"
    );

    contextMenuButton.click();

    await ContentTaskUtils.waitForCondition(
      () => contextMenuDiv.querySelector(".context-menu"),
      "Should find context menu after clicking button"
    );

    const contextMenu = contextMenuDiv.querySelector(".context-menu");

    const dismissButton = contextMenu.querySelector(
      "li.context-menu-item:nth-child(4) button"
    );

    dismissButton.click();

    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelectorAll(
          '.top-sites [data-is-sponsored-link="true"]'
        ).length === 1,
      "Should find only 1 sponsored topsite"
    );

    Assert.equal(
      content.document.querySelectorAll(
        '.top-sites [data-is-sponsored-link="true"]'
      ).length,
      1,
      "Should have 1 sponsored topsite after dismiss"
    );
  });

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.unifiedAds.blockedAds"
  );
  Services.prefs.clearUserPref("browser.topsites.blockedSponsors");
  Services.prefs.clearUserPref("browser.newtabpage.blocked");
});
