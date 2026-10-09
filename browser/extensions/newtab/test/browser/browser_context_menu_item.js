"use strict";

// test_tab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

// Test that we do not set icons in individual tile and card context menus on
// newtab page.
test_newtab({
  // Tests get no default Top Sites, and the row needs to hold a real tile to
  // read a context menu from. setTestTopSites also turns search shortcuts off,
  // so the only tile is a top site, whose menu is the one under test here.
  before: setTestTopSites,
  test: async function test_contextMenuIcons() {
    const siteSelector =
      ".top-site-outer:not(.search-shortcut, .placeholder, .add-button-tile)";
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(siteSelector),
      "Topsites have loaded"
    );
    const contextMenuItems =
      await content.openContextMenuAndGetOptions(siteSelector);
    let icon = contextMenuItems[0].querySelector(".icon");
    ok(!icon, "icon was not rendered");
  },
});
