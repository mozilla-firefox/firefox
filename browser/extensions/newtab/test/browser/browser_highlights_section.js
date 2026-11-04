"use strict";

const { PREFS_CONFIG } = ChromeUtils.importESModule(
  "resource://newtab/lib/ActivityStream.sys.mjs"
);

add_setup(async function () {
  let sandbox = sinon.createSandbox();

  sandbox
    .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
    .returns(
      "https://example.com/browser/browser/extensions/newtab/test/browser/topstories.json"
    );

  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        PREFS_CONFIG.get("discoverystream.config").getValue({
          geo: "US",
          locale: "en-US",
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
    ],
  });

  registerCleanupFunction(async () => {
    // This seems silly, since the mochitest harness will pop the pref off
    // automatically, but this seems to help avoid a "waiting for vsync to be
    // disabled" issue.
    await SpecialPowers.popPrefEnv();
    sandbox.restore();
  });
});

/**
 * Helper for setup and cleanup of Highlights section tests.
 *
 * @param bookmarkCount Number of bookmark higlights to add
 * @param test The test case
 */
function test_highlights(bookmarkCount, test) {
  test_newtab({
    async before({ tab }) {
      if (bookmarkCount) {
        await addHighlightsBookmarks(bookmarkCount);
        // Wait for HighlightsFeed to update and display the items.
        await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
          await ContentTaskUtils.waitForCondition(
            () =>
              content.document.querySelector(
                "[data-section-id='highlights'] .card-outer:not(.placeholder)"
              ),
            "No highlights cards found."
          );
        });
      }
    },
    test,
    async after() {
      await clearHistoryAndBookmarks();
      await clearHighlightsBookmarks();
    },
  });
}

test_highlights(
  2, // Number of highlights cards
  function check_highlights_cards() {
    let found = content.document.querySelectorAll(
      "[data-section-id='highlights'] .card-outer:not(.placeholder)"
    ).length;
    is(found, 2, "there should be 2 highlights cards");

    found = content.document.querySelectorAll(
      "[data-section-id='highlights'] .section-list .placeholder"
    ).length;
    is(found, 2, "there should be 1 row * 4 - 2 = 2 highlights placeholder");

    found = content.document.querySelectorAll(
      "[data-section-id='highlights'] .card-context-icon.icon-bookmark-added"
    ).length;
    is(found, 2, "there should be 2 bookmark icons");
  }
);

test_highlights(
  1, // Number of highlights cards
  async function check_highlights_context_menu() {
    const menuButton = content.document.querySelector(
      "[data-section-id='highlights'] .card-outer .context-menu-button"
    );
    // Open the menu.
    menuButton.click();
    // Wait for React to re-render the context menu.
    await new Promise(r => content.requestAnimationFrame(r));
    const found = content.document.querySelector(
      "[data-section-id='highlights'] .card-outer .context-menu"
    );
    ok(found && !found.hidden, "Should find a visible context menu");
  }
);

test_highlights(
  1, // Number of highlights cards
  async function check_highlights_context_menu() {
    // Removing the bookmark leaves the highlight on the page as a history
    // item, so what goes away is the bookmark icon rather than the card.
    const bookmarkIconSelector =
      "[data-section-id='highlights'] .card-outer .card-context-icon.icon-bookmark-added";
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(bookmarkIconSelector),
      "The highlight is a bookmark card to begin with"
    );

    const menuButton = content.document.querySelector(
      "[data-section-id='highlights'] .card-outer .context-menu-button"
    );
    // Open the menu.
    menuButton.click();
    // Wait for React to re-render the context menu.
    await new Promise(r => content.requestAnimationFrame(r));
    const contextMenu = content.document.querySelector(
      "[data-section-id='highlights'] .card-outer .context-menu"
    );
    ok(
      contextMenu && !contextMenu.hidden,
      "Should find a visible context menu"
    );

    const removeBookmarkBtn = contextMenu.querySelector(
      "[data-section-id='highlights'] button"
    );
    removeBookmarkBtn.click();

    await ContentTaskUtils.waitForCondition(
      () => !content.document.querySelector(bookmarkIconSelector),
      "no more bookmark cards should be visible"
    );
  }
);
