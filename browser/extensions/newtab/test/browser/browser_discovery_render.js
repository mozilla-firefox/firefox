"use strict";

// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

async function before({ pushPrefs }) {
  await pushPrefs([
    "browser.newtabpage.activity-stream.discoverystream.config",
    JSON.stringify({
      collapsible: true,
      enabled: true,
    }),
  ]);
  // @nova-cleanup(remove-pref): Remove this return block; delete the novaEnabled parameter
  // from test_render_hardcoded_topsites and remove the selector branch — always use
  // "section[data-section-id='topsites']"
  return {
    novaEnabled: Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.nova.enabled",
      false
    ),
  };
}

test_newtab({
  before,
  test: async function test_render_hardcoded_topsites({ novaEnabled }) {
    // @nova-cleanup(remove-conditional): Remove this branch; always use
    // "section[data-section-id='topsites']"
    const selector = novaEnabled
      ? "section[data-section-id='topsites']"
      : ".ds-top-sites";
    const topSites = await ContentTaskUtils.waitForCondition(() =>
      content.document.querySelector(selector)
    );
    ok(topSites, "Got the top sites section");
  },
});
