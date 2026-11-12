"use strict";

// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

async function before({ pushPrefs }) {
  await pushPrefs(
    ["browser.newtabpage.activity-stream.feeds.discoverystreamfeed", true],
    [
      "browser.newtabpage.activity-stream.discoverystream.config",
      JSON.stringify({
        collapsible: true,
        enabled: true,
      }),
    ]
  );
}

test_newtab({
  before,
  test: async function test_render_hardcoded_topsites() {
    const topSites = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector("section[data-section-id='topsites']")
    );
    ok(topSites, "Got the top sites section");
  },
});
