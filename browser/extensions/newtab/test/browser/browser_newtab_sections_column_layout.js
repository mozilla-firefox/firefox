/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The column layout CardSections uses for keyboard navigation follows the
 * column count Nova's container queries set (--sections-col-count), without
 * any focus, when the window is resized (bug 2091515).
 *
 * A card the new layout hides can't keep the section's tab stop, or Tab can't
 * reach the section. The test puts the tab stop on such a card, moves focus
 * out of the section, resizes, and waits for the stop to move to a card that's
 * shown.
 */

const SECTIONS_FEED_URL =
  "https://example.com/browser/browser/extensions/newtab/test/browser/newtab_sections.json";

function resizeChromeWindowTo(width, height) {
  if (window.outerWidth === width && window.outerHeight === height) {
    return Promise.resolve();
  }
  const resized = BrowserTestUtils.waitForEvent(window, "resize", false);
  window.resizeTo(width, height);
  return resized;
}

add_task(async function test_tab_stop_follows_column_count_on_resize() {
  const initialWidth = window.outerWidth;
  const initialHeight = window.outerHeight;
  registerCleanupFunction(() =>
    resizeChromeWindowTo(initialWidth, initialHeight)
  );

  sinon
    .stub(DiscoveryStreamFeed.prototype, "generateFeedUrl")
    .returns(SECTIONS_FEED_URL);
  registerCleanupFunction(() => sinon.restore());
  // The feed records the sections it shows here.
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.discoverystream.sections.interestPicker.visibleSections"
    )
  );

  await SpecialPowers.pushPrefEnv({
    set: [
      // @nova-cleanup(remove-pref): Drop this pref; Nova is the only layout.
      ["browser.newtabpage.activity-stream.nova.enabled", true],
      [
        "browser.newtabpage.activity-stream.discoverystream.sections.enabled",
        true,
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({
          collapsible: true,
          enabled: true,
          personalized: true,
        }),
      ],
      [
        "browser.newtabpage.activity-stream.discoverystream.endpoints",
        "https://example.com",
      ],
    ],
  });

  // Two columns. The default layout shows its eighth card at two columns and
  // hides it at three.
  await resizeChromeWindowTo(900, 1000);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:newtab",
    false
  );
  registerCleanupFunction(() => BrowserTestUtils.removeTab(tab));
  const browser = tab.linkedBrowser;
  await waitForPreloaded(browser);
  // Focus events only fire in a focused document.
  await SimpleTest.promiseFocus(window);
  browser.focus();

  // Puts the tab stop on the last shown card and returns the column count.
  const startColumnCount = await SpecialPowers.spawn(browser, [], async () => {
    const grid = () =>
      content.document.querySelector(".ds-section-grid.ds-card-grid");
    await ContentTaskUtils.waitForCondition(
      () => grid()?.querySelectorAll("article.ds-card .ds-card-link").length,
      "Sections cards render"
    );
    const shown = [...grid().querySelectorAll("article.ds-card")].filter(
      card => card.getClientRects().length
    );
    const lastShown = shown.at(-1);
    lastShown.querySelector(".ds-card-link").focus();
    await ContentTaskUtils.waitForCondition(
      () => lastShown.querySelector(".ds-card-link").tabIndex === 0,
      "The focused card holds the tab stop"
    );
    // Move focus out of the section; the stop stays on the card.
    content.document.activeElement.blur();
    lastShown.setAttribute("data-test-tab-stop", "");
    return parseInt(
      content.getComputedStyle(grid()).getPropertyValue("--sections-col-count"),
      10
    );
  });
  Assert.greater(startColumnCount, 1, "The window shows several columns");

  // Widen the window until CSS hides the card holding the tab stop.
  let hidden = false;
  for (const width of [1100, 1400]) {
    await resizeChromeWindowTo(width, 1000);
    hidden = await SpecialPowers.spawn(browser, [], async () => {
      const grid = content.document.querySelector(
        ".ds-section-grid.ds-card-grid"
      );
      const card = grid.querySelector("[data-test-tab-stop]");
      // Let a pending layout settle before reading.
      await new Promise(resolve =>
        content.requestAnimationFrame(() => content.setTimeout(resolve, 0))
      );
      return !card.getClientRects().length;
    });
    if (hidden) {
      break;
    }
  }
  Assert.ok(hidden, "A wider layout hides the card holding the tab stop");

  await SpecialPowers.spawn(browser, [], async () => {
    const grid = content.document.querySelector(
      ".ds-section-grid.ds-card-grid"
    );
    const stops = () =>
      [...grid.querySelectorAll("article.ds-card")].filter(
        card => card.querySelector(".ds-card-link")?.tabIndex === 0
      );
    await ContentTaskUtils.waitForCondition(
      () => stops().length === 1 && stops()[0].getClientRects().length,
      "The tab stop moves to a shown card, with no focus in the section"
    );
    Assert.ok(
      !stops()[0].hasAttribute("data-test-tab-stop"),
      "The hidden card no longer holds the tab stop"
    );
  });

  await SpecialPowers.popPrefEnv();
});
