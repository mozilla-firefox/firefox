"use strict";

// test_newtab calls SpecialPowers.spawn, which injects ContentTaskUtils in the
// scope of the callback. Eslint doesn't know about that.
/* global ContentTaskUtils */

add_setup(async function () {
  // test_render_search_handoff below drives the handoff search bar, which the
  // newtab <moz-urlbar> supersedes when its feature gate is on.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.urlbar.newtab.featureGate", false]],
  });
  // A preloaded about:newtab is already mounting <moz-urlbar> by the time of
  // this push, and does not swap it for the handoff bar.
  NewTabPagePreloading.removePreloadedBrowser(window);
});

test_newtab({
  test: async function test_render_search_handoff() {
    const selector = "content-search-handoff-ui";

    let search = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector(selector),
      "Wait for search handoff component to render"
    );
    ok(search, "Got the content search handoff UI");
  },
});

test_newtab(function test_render_topsites() {
  let topSites = content.document.querySelector(".top-sites-list");
  ok(topSites, "Got the top sites section");
});

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs([
      "browser.newtabpage.activity-stream.feeds.topsites",
      false,
    ]);
  },
  test: function test_render_no_topsites() {
    let topSites = content.document.querySelector(".top-sites-list");
    ok(!topSites, "No top sites section");
  },
});

// This next test runs immediately after test_render_no_topsites to make sure
// the topsites pref is restored
test_newtab(function test_render_topsites_again() {
  let topSites = content.document.querySelector(".top-sites-list");
  ok(topSites, "Got the top sites section again");
});

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs([
      "browser.newtabpage.activity-stream.logowordmark.alwaysVisible",
      false,
    ]);
  },
  test: function test_render_logo_ignores_alwaysVisible_false() {
    let logoWordmark = content.document.querySelector(".logo-and-wordmark");
    ok(logoWordmark, "The logo is rendered even when alwaysVisible is false");
  },
});

test_newtab({
  async before({ pushPrefs }) {
    await pushPrefs([
      "browser.newtabpage.activity-stream.logowordmark.alwaysVisible",
      true,
    ]);
  },
  test: function test_render_logo() {
    let logoWordmark = content.document.querySelector(".logo-and-wordmark");
    ok(logoWordmark, "The logo is rendered when pref is true");
  },
});
