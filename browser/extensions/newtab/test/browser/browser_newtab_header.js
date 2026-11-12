"use strict";

// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

// Tests that:
// 1. Top sites header is hidden and the topsites section is not collapsed on load.
// 2. Pocket header and section are visible and not collapsed on load.
// 3. Recent activity section and header are visible and not collapsed on load.
test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs(
      [
        "browser.newtabpage.activity-stream.discoverystream.config",
        JSON.stringify({ collapsible: true, enabled: true }),
      ],
      ["browser.newtabpage.activity-stream.feeds.system.topstories", true]
    );
  },
  test: async function test_render_customizeMenu() {
    // Top sites section
    await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(".top-sites"),
      "Wait for the top sites section to load"
    );

    let topSitesSection = content.document.querySelector(".top-sites");
    let titleContainer = topSitesSection.querySelector(
      ".section-title-container"
    );
    ok(
      !titleContainer ||
        (titleContainer && titleContainer.style.visibility === "hidden"),
      "Top sites header should  not be visible"
    );

    let isTopSitesCollapsed = topSitesSection.className.includes("collapsed");
    ok(!isTopSitesCollapsed, "Top sites should not be collapsed on load");

    // Pocket section
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.querySelector("section[data-section-id='topstories']"),
      "Wait for the pocket section to load"
    );

    let pocketSection = content.document.querySelector(
      "section[data-section-id='topstories']"
    );
    let isPocketSectionCollapsed =
      pocketSection.className.includes("collapsed");
    ok(
      !isPocketSectionCollapsed,
      "Pocket section should not be collapsed on load"
    );

    // In Nova, CollapsibleSection hides its .section-title; CardGrid renders .ds-header instead
    let pocketHeader = content.document.querySelector(
      "section[data-section-id='topstories'] .ds-header"
    );
    ok(
      pocketHeader && !pocketHeader.style.visibility,
      "Pocket header should be visible"
    );
  },
});
