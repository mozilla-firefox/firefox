/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TopSites, insertPinned, DEFAULT_TOP_SITES } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/topsites/TopSites.sys.mjs"
  );

ChromeUtils.defineESModuleGetters(this, {
  FilterAdult: "resource:///modules/FilterAdult.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  TOP_SITES_DEFAULT_ROWS:
    "moz-src:///browser/components/topsites/constants.mjs",
  TOP_SITES_MAX_SITES_PER_ROW:
    "moz-src:///browser/components/topsites/constants.mjs",
});

const FAKE_FAVICON = "data987";
const FAKE_FAVICON_SIZE = 128;
// Two visits on the same day.
const FAKE_FRECENCY = PlacesUtils.history.pageFrecencyThreshold(0, 2, false);
const FAKE_LINKS = new Array(2 * TOP_SITES_MAX_SITES_PER_ROW)
  .fill(null)
  .map((v, i) => ({
    frecency: FAKE_FRECENCY,
    url: `http://www.site${i}.com`,
  }));

function FakeTippyTopProvider() {}
FakeTippyTopProvider.prototype = {
  async init() {
    this.initialized = true;
  },
  processSite(site) {
    return site;
  },
};

let gSearchServiceInitStub;
let gGetTopSitesStub;

function stubTopSites(sandbox) {
  async function cleanup() {
    if (TopSites._refreshing) {
      info("Wait for refresh to finish.");
      // Wait for refresh to finish or else removing the store while a process
      // is running will result in errors.
      await TestUtils.topicObserved("topsites-refreshed");
      info("Top sites was refreshed.");
    }
    TopSites._tippyTopProvider.initialized = false;
    TopSites.pinnedCache.clear();
    TopSites.frecentCache.clear();
    TopSites._reset();
    stub.restore();
    info("Finished cleaning up TopSites.");
  }

  // To avoid having to setup search for each test, we stub this method and
  // unstub it when the unit test calls for the search shortcuts.
  let stub = sandbox.stub(TopSites, "updateCustomSearchShortcuts");

  TopSites._requestRichIcon = sandbox.stub();
  // Set preferences to match the store state.
  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    2
  );
  info("Created mock store for TopSites.");
  return cleanup;
}

function createExpectedPinnedLink(link, index) {
  link.isDefault = false;
  link.isPinned = true;
  link.searchTopSite = false;
  link.favicon = FAKE_FAVICON;
  link.faviconSize = FAKE_FAVICON_SIZE;
  link.pinIndex = index;
  return link;
}

function assertLinks(actualLinks, expectedLinks) {
  Assert.equal(
    actualLinks.length,
    expectedLinks.length,
    "Links have equal length."
  );
  for (let i = 0; i < actualLinks.length; ++i) {
    Assert.deepEqual(actualLinks[i], expectedLinks[i], "Link entry matches");
  }
}

add_setup(async () => {
  // Places requires a profile.
  do_get_profile();

  let sandbox = sinon.createSandbox();
  sandbox.stub(SearchService, "defaultEngine").get(() => {
    return { identifier: "ddg", searchUrlDomain: "duckduckgo.com" };
  });

  gGetTopSitesStub = sandbox
    .stub(NewTabUtils.activityStreamLinks, "getTopSites")
    .resolves(FAKE_LINKS);

  gSearchServiceInitStub = sandbox.stub(SearchService, "init").resolves();

  sandbox.stub(NewTabUtils.activityStreamProvider, "_faviconBytesToDataURI");

  sandbox
    .stub(NewTabUtils.activityStreamProvider, "_addFavicons")
    .callsFake(l => {
      return Promise.resolve(
        l.map(link => {
          link.favicon = FAKE_FAVICON;
          link.faviconSize = FAKE_FAVICON_SIZE;
          return link;
        })
      );
    });

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

add_task(async function test_construction() {
  Assert.ok(TopSites._currentSearchHostname, "_currentSearchHostname defined");
});

add_task(async function test_refreshDefaults() {
  let sandbox = sinon.createSandbox();
  let cleanup = stubTopSites(sandbox);
  Assert.ok(
    !DEFAULT_TOP_SITES.length,
    "Should have 0 DEFAULT_TOP_SITES initially."
  );

  // We have to init to subscribe to changes to the preferences.
  await TopSites.init();

  info(
    "TopSites.refreshDefaults should add defaults on default.sites pref change."
  );
  Services.prefs.setStringPref(
    "browser.newtabpage.activity-stream.default.sites",
    "https://foo.com"
  );

  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have 1 DEFAULT_TOP_SITES now."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should refresh on topSiteRows PREF_CHANGED");
  let refreshStub = sandbox.stub(TopSites, "refresh");
  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    1
  );
  Assert.ok(TopSites.refresh.calledOnce, "refresh called");
  refreshStub.restore();

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should have default sites with .isDefault = true");
  TopSites.refreshDefaults("https://foo.com");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have a DEFAULT_TOP_SITES now."
  );
  Assert.ok(
    DEFAULT_TOP_SITES[0].isDefault,
    "Lone top site should be the default."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should have default sites with appropriate hostname");
  TopSites.refreshDefaults("https://foo.com");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    1,
    "Should have a DEFAULT_TOP_SITES now."
  );
  let [site] = DEFAULT_TOP_SITES;
  Assert.equal(
    site.hostname,
    NewTabUtils.shortURL(site),
    "Lone top site should have the right hostname."
  );

  // Reset the DEFAULT_TOP_SITES;
  DEFAULT_TOP_SITES.length = 0;

  info("refreshDefaults should add no defaults on empty pref");
  TopSites.refreshDefaults("");
  Assert.equal(
    DEFAULT_TOP_SITES.length,
    0,
    "Should have 0 DEFAULT_TOP_SITES now."
  );

  info("refreshDefaults should be able to clear defaults");
  TopSites.refreshDefaults("https://foo.com");
  TopSites.refreshDefaults("");

  Assert.equal(
    DEFAULT_TOP_SITES.length,
    0,
    "Should have 0 DEFAULT_TOP_SITES now."
  );

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.default.sites"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.topSitesRows"
  );
  TopSites.uninit();
  sandbox.restore();
  await cleanup();
});

add_task(
  async function test_getLinksWithDefaults_on_SearchService_init_failure() {
    let sandbox = sinon.createSandbox();
    let cleanup = stubTopSites(sandbox);

    TopSites.refreshDefaults("https://foo.com");

    gSearchServiceInitStub.rejects(
      new Error("Simulating search init failures")
    );

    const result = await TopSites.getLinksWithDefaults();
    Assert.ok(result);

    gSearchServiceInitStub.resolves();

    sandbox.restore();
    await cleanup();
  }
);

add_task(async function test_getLinksWithDefaults() {
  NewTabUtils.activityStreamLinks.getTopSites.resetHistory();

  let sandbox = sinon.createSandbox();
  let cleanup = stubTopSites(sandbox);

  TopSites.refreshDefaults("https://foo.com");

  info("getLinksWithDefaults should get the links from NewTabUtils");
  let result = await TopSites.getLinksWithDefaults();

  const reference = FAKE_LINKS.map(site =>
    Object.assign({}, site, {
      hostname: NewTabUtils.shortURL(site),
      typedBonus: true,
    })
  );

  Assert.deepEqual(result, reference);
  Assert.ok(NewTabUtils.activityStreamLinks.getTopSites.calledOnce);

  info("getLinksWithDefaults should indicate the links get typed bonus");
  Assert.ok(result[0].typedBonus, "Expected typed bonus property to be true.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_filterAdult() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should filter out non-pinned adult sites");

  sandbox.stub(FilterAdult, "filter").returns([]);
  const TEST_URL = "https://foo.com/";
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [{ url: TEST_URL }]);

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  const result = await TopSites.getLinksWithDefaults();
  Assert.ok(FilterAdult.filter.calledOnce);
  Assert.equal(result.length, 1);
  Assert.equal(result[0].url, TEST_URL);

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_caching() {
  let sandbox = sinon.createSandbox();

  info(
    "getLinksWithDefaults should filter out the defaults that have been blocked"
  );
  // make sure we only have one top site, and we block the only default site we have to show
  const url = "www.myonlytopsite.com";
  const topsite = {
    frecency: FAKE_FRECENCY,
    hostname: NewTabUtils.shortURL({ url }),
    typedBonus: true,
    url,
  };

  const blockedDefaultSite = { url: "https://foo.com" };
  gGetTopSitesStub.resolves([topsite]);
  sandbox.stub(NewTabUtils.blockedLinks, "isBlocked").callsFake(site => {
    return site.url === blockedDefaultSite.url;
  });

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");
  const result = await TopSites.getLinksWithDefaults();

  // what we should be left with is just the top site we added, and not the default site we blocked
  Assert.equal(result.length, 1);
  Assert.deepEqual(result[0], topsite);
  let foundBlocked = result.find(site => site.url === blockedDefaultSite.url);
  Assert.ok(!foundBlocked, "Should not have found blocked site.");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_dedupe() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should call dedupe.group on the links");
  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  let stub = sandbox.stub(TopSites.dedupe, "group").callsFake((...id) => id);
  await TopSites.getLinksWithDefaults();

  Assert.ok(stub.calledOnce, "dedupe.group was called once");
  sandbox.restore();
  await cleanup();
});

add_task(async function test__dedupe_key() {
  let sandbox = sinon.createSandbox();

  info("_dedupeKey should dedupe on hostname instead of url");
  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  let site = { url: "foo", hostname: "bar" };
  let result = TopSites._dedupeKey(site);

  Assert.equal(result, site.hostname, "deduped on hostname");
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_adds_defaults() {
  let sandbox = sinon.createSandbox();

  info(
    "getLinksWithDefaults should add defaults if there are are not enough links"
  );
  const TEST_LINKS = [{ frecency: FAKE_FRECENCY, url: "foo.com" }];
  gGetTopSitesStub.resolves(TEST_LINKS);
  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  let result = await TopSites.getLinksWithDefaults();

  let reference = [...TEST_LINKS, ...DEFAULT_TOP_SITES].map(s =>
    Object.assign({}, s, {
      hostname: NewTabUtils.shortURL(s),
      typedBonus: true,
    })
  );

  Assert.deepEqual(result, reference);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(
  async function test_getLinksWithDefaults_adds_defaults_for_visible_slots() {
    let sandbox = sinon.createSandbox();

    info(
      "getLinksWithDefaults should only add defaults up to the number of visible slots"
    );
    const numVisible = TOP_SITES_DEFAULT_ROWS * TOP_SITES_MAX_SITES_PER_ROW;
    let testLinks = [];
    for (let i = 0; i < numVisible - 1; i++) {
      testLinks.push({ frecency: FAKE_FRECENCY, url: `foo${i}.com` });
    }
    gGetTopSitesStub.resolves(testLinks);

    let cleanup = stubTopSites(sandbox);
    TopSites.refreshDefaults("https://foo.com");

    let result = await TopSites.getLinksWithDefaults();

    let reference = [...testLinks, DEFAULT_TOP_SITES[0]].map(s =>
      Object.assign({}, s, {
        hostname: NewTabUtils.shortURL(s),
        typedBonus: true,
      })
    );

    Assert.equal(result.length, numVisible);
    Assert.deepEqual(result, reference);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
    await cleanup();
  }
);

add_task(async function test_getLinksWithDefaults_no_throw_on_no_links() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should not throw if NewTabUtils returns null");
  gGetTopSitesStub.resolves(null);

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  await TopSites.getLinksWithDefaults();
  Assert.ok(true, "getLinksWithDefaults did not throw");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_get_more_on_request() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should get more if the user has asked for more");
  let testLinks = new Array(4 * TOP_SITES_MAX_SITES_PER_ROW)
    .fill(null)
    .map((v, i) => ({
      frecency: FAKE_FRECENCY,
      url: `http://www.site${i}.com`,
    }));
  gGetTopSitesStub.resolves(testLinks);

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  const TEST_ROWS = 3;
  Services.prefs.setIntPref(
    "browser.newtabpage.activity-stream.topSitesRows",
    TEST_ROWS
  );

  let result = await TopSites.getLinksWithDefaults();
  Assert.equal(result.length, TEST_ROWS * TOP_SITES_MAX_SITES_PER_ROW);

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.topSitesRows"
  );
  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_reuse_cache() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should reuse the cache on subsequent calls");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  gGetTopSitesStub.resetHistory();

  await TopSites.getLinksWithDefaults();
  await TopSites.getLinksWithDefaults();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getTopSites.calledOnce,
    "getTopSites only called once"
  );

  sandbox.restore();
  await cleanup();
});

add_task(
  async function test_getLinksWithDefaults_ignore_cache_on_requesting_more() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should ignore the cache when requesting more");

    let cleanup = stubTopSites(sandbox);
    TopSites.refreshDefaults("https://foo.com");

    gGetTopSitesStub.resetHistory();

    await TopSites.getLinksWithDefaults();
    Services.prefs.setIntPref(
      "browser.newtabpage.activity-stream.topSitesRows",
      3
    );
    await TopSites.getLinksWithDefaults();

    Assert.ok(
      NewTabUtils.activityStreamLinks.getTopSites.calledTwice,
      "getTopSites called twice"
    );

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.topSitesRows"
    );
    sandbox.restore();
    await cleanup();
  }
);

add_task(
  async function test_getLinksWithDefaults_migrate_pinned_favicon_data() {
    let sandbox = sinon.createSandbox();
    info(
      "getLinksWithDefaults should migrate pinned favicon data without getting favicons again"
    );

    let cleanup = stubTopSites(sandbox);
    TopSites.refreshDefaults("https://foo.com");

    gGetTopSitesStub.resetHistory();

    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    await TopSites.getLinksWithDefaults();

    let originalCallCount =
      NewTabUtils.activityStreamProvider._addFavicons.callCount;
    TopSites.pinnedCache.expire();

    let result = await TopSites.getLinksWithDefaults();

    Assert.equal(
      NewTabUtils.activityStreamProvider._addFavicons.callCount,
      originalCallCount,
      "_addFavicons was not called again."
    );
    Assert.equal(result[0].favicon, FAKE_FAVICON);
    Assert.equal(result[0].faviconSize, FAKE_FAVICON_SIZE);

    sandbox.restore();
    await cleanup();
  }
);

add_task(async function test_getLinksWithDefaults_no_internal_properties() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should not expose internal link properties");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  let result = await TopSites.getLinksWithDefaults();

  let internal = Object.keys(result[0]).filter(key => key.startsWith("__"));
  Assert.equal(internal.join(""), "");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_copy_frecent_screenshot() {
  // TopSites pulls data from NewTabUtils.activityStreamLinks.getTopSites()
  // which can still pass screenshots to it if they are available.
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should copy the screenshot of the frecent site if it exists"
  );

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  const TEST_SCREENSHOT = "screenshot";

  gGetTopSitesStub.resolves([
    { url: "https://foo.com/", screenshot: TEST_SCREENSHOT },
  ]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/" }]);

  let result = await TopSites.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, TEST_SCREENSHOT);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_copies_both_screenshots() {
  // TopSites pulls data from NewTabUtils.activityStreamLinks.getTopSites()
  // and NewTabUtils.pinnedLinks which can pass screenshot data to it if they
  // are available.
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should still copy the frecent screenshot if " +
      "customScreenshotURL is set"
  );

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  gGetTopSitesStub.resolves([
    { url: "https://foo.com/", screenshot: "screenshot" },
  ]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/", customScreenshotURL: "custom" }]);

  let result = await TopSites.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, "screenshot");
  Assert.equal(result[0].customScreenshotURL, "custom");

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_persist_screenshot() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults should keep the same screenshot if no frecent site is found"
  );

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  const CUSTOM_SCREENSHOT = "custom";

  gGetTopSitesStub.resolves([]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://foo.com/", screenshot: CUSTOM_SCREENSHOT }]);

  let result = await TopSites.getLinksWithDefaults();

  Assert.equal(result[0].screenshot, CUSTOM_SCREENSHOT);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(
  async function test_getLinksWithDefaults_no_overwrite_pinned_screenshot() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should not overwrite pinned site screenshot");

    let cleanup = stubTopSites(sandbox);
    TopSites.refreshDefaults("https://foo.com");

    const EXISTING_SCREENSHOT = "some-screenshot";

    gGetTopSitesStub.resolves([{ url: "https://foo.com/", screenshot: "foo" }]);
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [
        { url: "https://foo.com/", screenshot: EXISTING_SCREENSHOT },
      ]);

    let result = await TopSites.getLinksWithDefaults();

    Assert.equal(result[0].screenshot, EXISTING_SCREENSHOT);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
    await cleanup();
  }
);

add_task(
  async function test_getLinksWithDefaults_no_searchTopSite_from_frecent() {
    let sandbox = sinon.createSandbox();
    info("getLinksWithDefaults should not set searchTopSite from frecent site");

    let cleanup = stubTopSites(sandbox);
    TopSites.refreshDefaults("https://foo.com");

    const EXISTING_SCREENSHOT = "some-screenshot";

    gGetTopSitesStub.resolves([
      {
        url: "https://foo.com/",
        searchTopSite: true,
        screenshot: EXISTING_SCREENSHOT,
      },
    ]);
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    let result = await TopSites.getLinksWithDefaults();

    Assert.ok(!result[0].searchTopSite);
    // But it should copy over other properties
    Assert.equal(result[0].screenshot, EXISTING_SCREENSHOT);

    gGetTopSitesStub.resolves(FAKE_LINKS);
    sandbox.restore();
    await cleanup();
  }
);

add_task(async function test_getLinksWithDefaults_concurrency_getTopSites() {
  let sandbox = sinon.createSandbox();
  info(
    "getLinksWithDefaults concurrent calls should call the backing data once"
  );

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  NewTabUtils.activityStreamLinks.getTopSites.resetHistory();

  await Promise.all([
    TopSites.getLinksWithDefaults(),
    TopSites.getLinksWithDefaults(),
  ]);

  Assert.ok(
    NewTabUtils.activityStreamLinks.getTopSites.calledOnce,
    "getTopSites only called once"
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_deduping_no_dedupe_pinned() {
  let sandbox = sinon.createSandbox();
  info("getLinksWithDefaults should not dedupe pinned sites");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults("https://foo.com");

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      { url: "https://developer.mozilla.org/en-US/docs/Web" },
      { url: "https://developer.mozilla.org/en-US/docs/Learn" },
    ]);

  let sites = await TopSites.getLinksWithDefaults();
  Assert.equal(sites.length, 2 * TOP_SITES_MAX_SITES_PER_ROW);
  Assert.equal(sites[0].url, NewTabUtils.pinnedLinks.links[0].url);
  Assert.equal(sites[1].url, NewTabUtils.pinnedLinks.links[1].url);
  Assert.equal(sites[0].hostname, sites[1].hostname);

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_prefer_pinned_sites() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should prefer pinned sites over links");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults();

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      { url: "https://developer.mozilla.org/en-US/docs/Web" },
      { url: "https://developer.mozilla.org/en-US/docs/Learn" },
    ]);

  const SECOND_TOP_SITE_URL = "https://www.mozilla.org/";

  gGetTopSitesStub.resolves([
    { frecency: FAKE_FRECENCY, url: "https://developer.mozilla.org/" },
    { frecency: FAKE_FRECENCY, url: SECOND_TOP_SITE_URL },
  ]);

  let sites = await TopSites.getLinksWithDefaults();

  // Expecting 3 links where there's 2 pinned and 1 www.mozilla.org, so
  // the frecent with matching hostname as pinned is removed.
  Assert.equal(sites.length, 3);
  Assert.equal(sites[0].url, NewTabUtils.pinnedLinks.links[0].url);
  Assert.equal(sites[1].url, NewTabUtils.pinnedLinks.links[1].url);
  Assert.equal(sites[2].url, SECOND_TOP_SITE_URL);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_title_and_null() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should return sites that have a title");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults();

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [{ url: "https://github.com/mozilla/activity-stream" }]);

  let sites = await TopSites.getLinksWithDefaults();
  for (let site of sites) {
    Assert.ok(site.hostname);
  }

  info("getLinksWithDefaults should not throw for null entries");
  sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [null]);
  await TopSites.getLinksWithDefaults();
  Assert.ok(true, "getLinksWithDefaults didn't throw");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_getLinksWithDefaults_calls__fetchIcon() {
  let sandbox = sinon.createSandbox();

  info("getLinksWithDefaults should return sites that have a title");

  let cleanup = stubTopSites(sandbox);
  TopSites.refreshDefaults();

  sandbox.spy(TopSites, "_fetchIcon");
  let results = await TopSites.getLinksWithDefaults();
  Assert.ok(results.length, "Got back some results");
  Assert.equal(TopSites._fetchIcon.callCount, results.length);
  for (let result of results) {
    Assert.ok(TopSites._fetchIcon.calledWith(result));
  }

  sandbox.restore();
  await cleanup();
});

add_task(async function test_init() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(NimbusFeatures.newtab, "onUpdate");

  let cleanup = stubTopSites(sandbox);

  sandbox.stub(TopSites, "refresh");
  await TopSites.init();

  info("TopSites.init should call refresh");
  Assert.ok(TopSites.refresh.calledOnce, "refresh called once");
  Assert.ok(
    TopSites.refresh.calledWithExactly({
      isStartup: true,
    })
  );

  TopSites.uninit();
  sandbox.restore();
  await cleanup();
});

/**
 * If multiple callers are attempting to initializing TopSites, we should
 * initialize only once and wait until its completed.
 */
add_task(async function test_multiple_init() {
  info("Initing TopSites multiple times should call _readDefaults only once.");
  let sandbox = sinon.createSandbox();
  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  sandbox.stub(TopSites, "_readDefaults");
  let cleanup = stubTopSites(sandbox);

  Assert.ok(TopSites._readDefaults.notCalled, "Read defaults not called.");
  for (let i = 0; i < 5; ++i) {
    await TopSites.init();
  }
  Assert.ok(TopSites._readDefaults.calledOnce, "Read defaults called once.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_multiple_init_delay() {
  TopSites.uninit();

  info(
    "Initing TopSites multiple times should allow callers " +
      "only call readDefaults once and wait until its finished."
  );
  let sandbox = sinon.createSandbox();

  let resolvePromise;
  let promise = new Promise(resolve => {
    resolvePromise = resolve;
  });

  sandbox.stub(NimbusFeatures.newtab, "onUpdate");
  sandbox.stub(TopSites, "_readDefaults").returns(promise);
  let cleanup = stubTopSites(sandbox);

  Assert.ok(TopSites._readDefaults.notCalled, "Read defaults not called.");
  let finishedPromiseCount = 0;
  let promises = [];
  let callInit = async () => {
    await TopSites.init();
    ++finishedPromiseCount;
  };
  for (let i = 0; i < 5; ++i) {
    promises.push(callInit());
  }
  Assert.equal(
    finishedPromiseCount,
    0,
    "Finished promise count should be equal."
  );
  Assert.ok(TopSites._readDefaults.calledOnce, "Read defaults called once.");

  info("Resolve the promises.");
  resolvePromise();
  await Promise.all(promises);
  Assert.equal(
    finishedPromiseCount,
    5,
    "Finished promise count should be equal."
  );
  Assert.ok(
    TopSites._readDefaults.calledOnce,
    "Read defaults was still only called once."
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_uninit() {
  info("Un-initing TopSites should expire caches.");
  let sandbox = sinon.createSandbox();

  let cleanup = stubTopSites(sandbox);
  sandbox.stub(TopSites, "refresh");
  await TopSites.init();

  sandbox.stub(TopSites.pinnedCache, "expire");
  sandbox.stub(TopSites.frecentCache, "expire");
  TopSites.uninit();

  Assert.ok(
    TopSites.pinnedCache.expire.calledOnce,
    "pinnedCache.expire called once"
  );
  Assert.ok(
    TopSites.frecentCache.expire.calledOnce,
    "frecentCache.expire called once"
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_get_sites_init() {
  info("TopSites.getSites should initialize TopSites if its not inited.");
  let sandbox = sinon.createSandbox();

  let cleanup = stubTopSites(sandbox);
  sandbox.stub(TopSites, "init");

  Assert.ok(TopSites.init.notCalled, "TopSites.init not called.");
  await TopSites.getSites();
  Assert.ok(TopSites.init.calledOnce, "TopSites.init called once.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_get_sites_already_inited() {
  info(
    "TopSites.getSites should not call related initialization methods " +
      "more than once if TopSites is already inited."
  );
  let sandbox = sinon.createSandbox();

  let cleanup = stubTopSites(sandbox);
  sandbox.spy(TopSites, "_readDefaults");
  await TopSites.init();

  Assert.ok(
    TopSites._readDefaults.calledOnce,
    "TopSites._readDefaults called once."
  );
  Assert.ok(
    TopSites.updateCustomSearchShortcuts.calledOnce,
    "TopSites.updateCustomSearchShortcuts called once."
  );
  await TopSites.getSites();
  Assert.ok(
    TopSites._readDefaults.calledOnce,
    "TopSites._readDefaults still only called once."
  );
  Assert.ok(
    TopSites.updateCustomSearchShortcuts.calledOnce,
    "TopSites.updateCustomSearchShortcuts still only called once."
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_get_sites_delayed_init() {
  info("TopSites.getSites should wait until initialization is done.");
  let sandbox = sinon.createSandbox();

  let cleanup = stubTopSites(sandbox);

  // Ensure it's not initialized.
  TopSites.uninit();

  let resolvePromise;
  let promise = new Promise(resolve => {
    resolvePromise = resolve;
  });
  sandbox.stub(TopSites, "init").returns(promise);

  let promises = [];
  let finishedPromiseCount = 0;
  let callGetSites = async () => {
    await TopSites.getSites();
    finishedPromiseCount += 1;
  };
  for (let i = 0; i < 5; ++i) {
    promises.push(callGetSites());
  }

  Assert.equal(
    finishedPromiseCount,
    0,
    "All calls to TopSites.getSites() haven't finished."
  );
  resolvePromise();
  await Promise.all(promises);
  Assert.equal(
    finishedPromiseCount,
    5,
    "All calls to TopSites.getSites() finished."
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_refresh() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(NimbusFeatures.newtab, "onUpdate");

  let cleanup = stubTopSites(sandbox);

  sandbox.stub(TopSites, "_fetchIcon");
  TopSites._startedUp = true;

  info("TopSites.refresh should wait for tippytop to initialize");
  TopSites._tippyTopProvider.initialized = false;
  sandbox.stub(TopSites._tippyTopProvider, "init").resolves();

  await TopSites.refresh();

  Assert.ok(
    TopSites._tippyTopProvider.init.calledOnce,
    "TopSites._tippyTopProvider.init called once"
  );

  info(
    "TopSites.refresh should not init the tippyTopProvider if already initialized"
  );
  TopSites._tippyTopProvider.initialized = true;
  TopSites._tippyTopProvider.init.resetHistory();

  await TopSites.refresh();

  Assert.ok(
    TopSites._tippyTopProvider.init.notCalled,
    "tippyTopProvider not initted again"
  );

  sandbox.restore();
  await cleanup();
});

add_task(async function test_refresh_updateTopSites() {
  // Ensure that TopSites isn't already initialized.
  TopSites.uninit();

  let sandbox = sinon.createSandbox();
  let cleanup = stubTopSites(sandbox);

  await TopSites.init();
  TopSites._reset();

  // Clear the internal store.
  TopSites._reset();

  let sites = await TopSites.getSites();
  Assert.equal(sites.length, 0, "Sites is empty.");

  info("TopSites.refresh should update TopSites.sites");
  let promise = TestUtils.topicObserved("topsites-refreshed");
  // TODO: On New Tab, subscribe to updates to Top Sites.
  await TopSites.refresh({ isStartup: true });
  await promise;

  sites = await TopSites.getSites();
  Assert.ok(sites.length, "Sites has values.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_refresh_dispatch() {
  let sandbox = sinon.createSandbox();

  info("TopSites.refresh should dispatch an action with the links returned");

  let cleanup = stubTopSites(sandbox);
  sandbox.stub(TopSites, "_fetchIcon");
  TopSites._startedUp = true;

  await TopSites.refresh();
  let reference = FAKE_LINKS.map(site =>
    Object.assign({}, site, {
      hostname: NewTabUtils.shortURL(site),
      typedBonus: true,
    })
  );

  // TODO: On New Tab, subscribe to updates to Top Sites.
  let sites = await TopSites.getSites();
  Assert.deepEqual(sites, reference, "Sites are updated.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_refresh_empty_slots() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSites.refresh should handle empty slots in the resulting top sites array"
  );

  let cleanup = stubTopSites(sandbox);
  sandbox.stub(TopSites, "_fetchIcon");
  TopSites._startedUp = true;

  gGetTopSitesStub.resolves([FAKE_LINKS[0]]);
  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      null,
      null,
      FAKE_LINKS[1],
      null,
      null,
      null,
      null,
      null,
      FAKE_LINKS[2],
    ]);
  await TopSites.refresh();

  let reference = FAKE_LINKS.map(site =>
    Object.assign({}, site, {
      hostname: NewTabUtils.shortURL(site),
      typedBonus: true,
    })
  );
  const expected = [
    reference[0],
    null,
    createExpectedPinnedLink(reference[1], 2),
    null,
    null,
    null,
    null,
    null,
    createExpectedPinnedLink(reference[2], 8),
  ];

  // TODO: On New Tab, subscribe to updates to Top Sites.
  let sites = await TopSites.getSites();
  assertLinks(sites, expected);

  gGetTopSitesStub.resolves(FAKE_LINKS);
  sandbox.restore();
  await cleanup();
});

add_task(async function test_insert_part_2() {
  let sandbox = sinon.createSandbox();

  info(
    "TopSites.handlePlacesEvents should call refresh without a target " +
      "if we clear history."
  );

  let cleanup = stubTopSites(sandbox);
  sandbox.stub(TopSites, "refresh");
  TopSites.refresh.resetHistory();
  await PlacesUtils.history.clear();
  Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");

  TopSites.refresh.resetHistory();

  info(
    "TopSites.handlePlacesEvents should call refresh without a target " +
      "if we remove a Topsite from history"
  );
  let uri = Services.io.newURI("https://www.example.com/");
  await PlacesTestUtils.addVisits({
    uri,
    transition: PlacesUtils.history.TRANSITION_TYPED,
    visitDate: Date.now() * 1000,
  });
  Assert.ok(TopSites.refresh.notCalled, "TopSites.refresh not called");
  await PlacesUtils.history.remove(uri);

  Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");

  info("TopSites.handlePlacesEvents should call refresh on newtab-linkBlocked");
  TopSites.refresh.resetHistory();
  // The event dispatched in NewTabUtils when a link is blocked;
  TopSites.observe(null, "newtab-linkBlocked", null);
  Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");

  info("TopSites should call refresh on bookmark-added");
  TopSites.refresh.resetHistory();
  let bookmark = await PlacesUtils.bookmarks.insert({
    url: "https://bookmark.example.com",
    title: "Bookmark 1",
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });
  Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");

  info("TopSites should call refresh on bookmark-removed");
  TopSites.refresh.resetHistory();
  await PlacesUtils.bookmarks.remove(bookmark);
  Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");

  info(
    "TopSites.insert should call pin with correct args " +
      "without an index specified"
  );
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");

  let addAction = {
    data: { site: { url: "foo.bar", label: "foo" } },
  };
  TopSites.insert(addAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(addAction.data.site, 0));

  info("TopSites.insert should call pin with correct args");
  NewTabUtils.pinnedLinks.pin.resetHistory();
  let dropAction = {
    data: { site: { url: "foo.bar", label: "foo" }, index: 3 },
  };
  TopSites.insert(dropAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(dropAction.data.site, 3));

  sandbox.restore();
  await cleanup();
});

add_task(async function test_insert_part_1() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  let cleanup = stubTopSites(sandbox);

  {
    info(
      "TopSites.insert should pin site in first slot of pinned list with " +
        "empty first slot"
    );

    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);
    let site = { url: "foo.bar", label: "foo" };
    await TopSites.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  {
    info(
      "TopSites.insert should move a pinned site in first slot to the " +
        "next slot: part 1"
    );
    let site1 = { url: "example.com" };
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [site1]);
    let site = { url: "foo.bar", label: "foo" };

    await TopSites.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  {
    info(
      "TopSites.insert should move a pinned site in first slot to the " +
        "next slot: part 2"
    );
    let site1 = { url: "example.com" };
    let site2 = { url: "example.org" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [site1, null, site2]);
    let site = { url: "foo.bar", label: "foo" };
    await TopSites.insert({ data: { site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  sandbox.restore();
  await cleanup();
});

add_task(async function test_insert_part_2() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  let cleanup = stubTopSites(sandbox);

  {
    info(
      "TopSites.insert should unpin the last site if all slots are " +
        "already pinned"
    );
    let site1 = { url: "example.com" };
    let site2 = { url: "example.org" };
    let site3 = { url: "example.net" };
    let site4 = { url: "example.biz" };
    let site5 = { url: "example.info" };
    let site6 = { url: "example.news" };
    let site7 = { url: "example.lol" };
    let site8 = { url: "example.golf" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [site1, site2, site3, site4, site5, site6, site7, site8]);
    Services.prefs.setIntPref(
      "browser.newtabpage.activity-stream.topSitesRows",
      1
    );
    let site = { url: "foo.bar", label: "foo" };
    await TopSites.insert({ data: { site } });
    Assert.equal(
      NewTabUtils.pinnedLinks.pin.callCount,
      8,
      "NewTabUtils.pinnedLinks.pin called 8 times"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 1));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site3, 3));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site4, 4));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site5, 5));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site6, 6));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site7, 7));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.topSitesRows"
    );
  }

  {
    info("TopSites.insert should trigger refresh");
    sandbox.stub(TopSites, "refresh");
    let addAction = {
      data: { site: { url: "foo.com" } },
    };

    await TopSites.insert(addAction);

    Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");
  }

  {
    info("TopSites.insert should correctly handle different index values");
    let index = -1;
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index, site } };

    await TopSites.insert(action);
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));

    index = undefined;
    await TopSites.insert(action);
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 0));

    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  sandbox.restore();
  await cleanup();
});

add_task(async function test_insert_part_3() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  let cleanup = stubTopSites(sandbox);

  {
    info("TopSites.insert should pin site in specified slot that is free");
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };

    await TopSites.insert({ data: { index: 2, site, draggedFromIndex: 0 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));

    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  {
    info(
      "TopSites.insert should move a pinned site in specified slot " +
        "to the next slot"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };

    await TopSites.insert({ data: { index: 2, site, draggedFromIndex: 3 } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith({ url: "example.com" }, 3)
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  {
    info(
      "TopSites.insert should move pinned sites in the direction " +
        "of the dragged site"
    );

    let site1 = { url: "foo.bar", label: "foo" };
    let site2 = { url: "example.com", label: "example" };
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, site2]);

    await TopSites.insert({
      data: { index: 2, site: site1, draggedFromIndex: 0 },
    });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 1));
    NewTabUtils.pinnedLinks.pin.resetHistory();

    await TopSites.insert({
      data: { index: 2, site: site1, draggedFromIndex: 5 },
    });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site1, 2));
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site2, 3));
    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  {
    info("TopSites.insert should not insert past the visible top sites");
    let site1 = { url: "foo.bar", label: "foo" };
    await TopSites.insert({
      data: { index: 42, site: site1, draggedFromIndex: 0 },
    });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.notCalled,
      "NewTabUtils.pinnedLinks.pin wasn't called"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  sandbox.restore();
  await cleanup();
});

add_task(async function test_pin_part_1() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.spy(TopSites.pinnedCache, "request");
  let cleanup = stubTopSites(sandbox);

  {
    info("TopSites.pin should pin site in specified slot empty pinned list");
    let site = {
      url: "foo.bar",
      label: "foo",
    };
    Assert.ok(
      TopSites.pinnedCache.request.notCalled,
      "TopSites.pinnedCache.request not called"
    );
    await TopSites.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.called,
      "NewTabUtils.pinnedLinks.pin called"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    TopSites.pinnedCache.request.resetHistory();
  }

  {
    info(
      "TopSites.pin should not do a link object lookup if custom " +
        "screenshot field is not set"
    );
    let site = { url: "foo.bar", label: "foo" };
    await TopSites.pin({ data: { index: 2, site } });
    Assert.ok(
      !TopSites.pinnedCache.request.called,
      "TopSites.pinnedCache.request never called"
    );
    NewTabUtils.pinnedLinks.pin.resetHistory();
    TopSites.pinnedCache.request.resetHistory();
  }

  {
    info(
      "TopSites.pin should pin site in specified slot of pinned " +
        "list that is free"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    await TopSites.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
  }

  sandbox.restore();
  await cleanup();
});

add_task(async function test_pin_part_2() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");

  {
    info("TopSites.pin should save the searchTopSite attribute if set");
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo", searchTopSite: true };
    let cleanup = stubTopSites(sandbox);
    await TopSites.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.firstCall.args[0].searchTopSite);
    NewTabUtils.pinnedLinks.pin.resetHistory();
    await cleanup();
  }

  {
    info(
      "TopSites.pin should NOT move a pinned site in specified " +
        "slot to the next slot"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [null, null, { url: "example.com" }]);

    let site = { url: "foo.bar", label: "foo" };
    let cleanup = stubTopSites(sandbox);
    await TopSites.pin({ data: { index: 2, site } });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(NewTabUtils.pinnedLinks.pin.calledWith(site, 2));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    await cleanup();
  }

  {
    info(
      "TopSites.pin should not update LinksCache object with screenshot data" +
        "properties between migrations"
    );
    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "https://foo.com/" }]);

    let cleanup = stubTopSites(sandbox);
    let pinnedLinks = await TopSites.pinnedCache.request();
    Assert.equal(pinnedLinks.length, 1);
    TopSites.pinnedCache.expire();

    pinnedLinks[0].__sharedCache.updateLink("screenshot", "foo");

    pinnedLinks = await TopSites.pinnedCache.request();
    Assert.equal(pinnedLinks[0].screenshot, undefined);

    // Force cache expiration in order to trigger a migration of objects
    TopSites.pinnedCache.expire();
    pinnedLinks[0].__sharedCache.updateLink("screenshot", "bar");

    pinnedLinks = await TopSites.pinnedCache.request();
    Assert.equal(pinnedLinks[0].screenshot, undefined);
    await cleanup();
  }

  sandbox.restore();
});

add_task(async function test_pin_part_3() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.spy(TopSites, "insert");

  {
    info("TopSites.pin should call insert if index < 0");
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index: -1, site } };
    let cleanup = stubTopSites(sandbox);
    await TopSites.pin(action);

    Assert.ok(TopSites.insert.calledOnce, "TopSites.insert called once");
    Assert.ok(TopSites.insert.calledWithExactly(action));
    NewTabUtils.pinnedLinks.pin.resetHistory();
    TopSites.insert.resetHistory();
    await cleanup();
  }

  {
    info("TopSites.pin should not call insert if index == 0");
    let site = { url: "foo.bar", label: "foo" };
    let action = { data: { index: 0, site } };
    let cleanup = stubTopSites(sandbox);
    await TopSites.pin(action);

    Assert.ok(!TopSites.insert.called, "TopSites.insert not called");
    NewTabUtils.pinnedLinks.pin.resetHistory();
    await cleanup();
  }

  {
    info("TopSites.pin should trigger refresh");
    let cleanup = stubTopSites(sandbox);
    sandbox.stub(TopSites, "refresh");
    let pinExistingAction = {
      data: { site: FAKE_LINKS[4], index: 4 },
    };

    await TopSites.pin(pinExistingAction);

    Assert.ok(TopSites.refresh.calledOnce, "TopSites.refresh called once");
    NewTabUtils.pinnedLinks.pin.resetHistory();
    await cleanup();
  }

  sandbox.restore();
});

add_task(async function test_pin_part_4() {
  let sandbox = sinon.createSandbox();
  let cleanup = stubTopSites(sandbox);

  info("TopSites.pin should call with correct parameters");
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.spy(TopSites, "pin");

  let pinAction = {
    data: { site: { url: "foo.com" }, index: 7 },
  };
  await TopSites.pin(pinAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledOnce,
    "NewTabUtils.pinnedLinks.pin called once"
  );
  Assert.ok(
    NewTabUtils.pinnedLinks.pin.calledWithExactly(
      pinAction.data.site,
      pinAction.data.index
    )
  );
  Assert.ok(
    TopSites.pin.calledOnce,
    "TopSites.pin should have been called once"
  );

  info(
    "TopSites.pin should unblock a previously blocked top site if " +
      "we are now adding it manually via 'Add a Top Site' option"
  );
  sandbox.stub(NewTabUtils.blockedLinks, "unblock");
  pinAction = {
    data: { site: { url: "foo.com" }, index: -1 },
  };
  await TopSites.pin(pinAction);
  Assert.ok(
    NewTabUtils.blockedLinks.unblock.calledWith({
      url: pinAction.data.site.url,
    })
  );

  info("TopSites.pin should call insert");
  sandbox.stub(TopSites, "insert");
  let addAction = {
    data: { site: { url: "foo.com" } },
  };

  await TopSites.pin(addAction);
  Assert.ok(TopSites.insert.calledOnce, "TopSites.insert called once");

  info("TopSites.unpin should call unpin with correct parameters");

  sandbox
    .stub(NewTabUtils.pinnedLinks, "links")
    .get(() => [
      null,
      null,
      { url: "foo.com" },
      null,
      null,
      null,
      null,
      null,
      FAKE_LINKS[0],
    ]);
  sandbox.stub(NewTabUtils.pinnedLinks, "unpin");

  let unpinAction = {
    data: { site: { url: "foo.com" } },
  };
  await TopSites.unpin(unpinAction);
  Assert.ok(
    NewTabUtils.pinnedLinks.unpin.calledOnce,
    "NewTabUtils.pinnedLinks.unpin called once"
  );
  Assert.ok(NewTabUtils.pinnedLinks.unpin.calledWith(unpinAction.data.site));

  sandbox.restore();
  await cleanup();
});

add_task(async function test_integration() {
  let sandbox = sinon.createSandbox();

  info("Test adding a pinned site and removing it with actions");
  let cleanup = stubTopSites(sandbox);

  TopSites._startedUp = true;

  TopSites._requestRichIcon = sandbox.stub();
  let url = "https://pin.me";
  sandbox.stub(NewTabUtils.pinnedLinks, "pin").callsFake(link => {
    NewTabUtils.pinnedLinks.links.push(link);
  });

  await TopSites.insert({ data: { site: { url } } });
  await TestUtils.topicObserved("topsites-refreshed");
  let oldSites = await TopSites.getSites();
  NewTabUtils.pinnedLinks.links.pop();
  // The event dispatched in NewTabUtils when a link is blocked;
  TopSites.observe(null, "newtab-linkBlocked", null);
  await TestUtils.topicObserved("topsites-refreshed");
  let newSites = await TopSites.getSites();

  Assert.equal(oldSites[0].url, url, "Url matches.");
  Assert.equal(newSites[0].url, FAKE_LINKS[0].url, "Url matches.");

  sandbox.restore();
  await cleanup();
});

add_task(async function test_improvesearch_noDefaultSearchTile_experiment() {
  let sandbox = sinon.createSandbox();

  sandbox.stub(SearchService, "getDefault").resolves({
    identifier: "google",
  });

  {
    info(
      "TopSites.getLinksWithDefaults should filter out alexa top 5 " +
        "search from the default sites"
    );
    let cleanup = stubTopSites(sandbox);
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
      true
    );
    let top5Test = [
      "https://google.com",
      "https://search.yahoo.com",
      "https://yahoo.com",
      "https://bing.com",
      "https://ask.com",
      "https://duckduckgo.com",
    ];

    gGetTopSitesStub.resolves([
      { url: "https://amazon.com" },
      ...top5Test.map(url => ({ url })),
    ]);

    const urlsReturned = (await TopSites.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(
      urlsReturned.includes("https://amazon.com"),
      "amazon included in default links"
    );
    top5Test.forEach(url =>
      Assert.ok(!urlsReturned.includes(url), `Should not include ${url}`)
    );

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
    );
    gGetTopSitesStub.resolves(FAKE_LINKS);
    await cleanup();
  }

  {
    info(
      "TopSites.getLinksWithDefaults should not filter out alexa, default " +
        "search from the query results if the experiment pref is off"
    );
    let cleanup = stubTopSites(sandbox);
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
      false
    );
    gGetTopSitesStub.resolves([
      { url: "https://google.com" },
      { url: "https://foo.com" },
      { url: "https://duckduckgo" },
    ]);
    let urlsReturned = (await TopSites.getLinksWithDefaults()).map(
      link => link.url
    );

    Assert.ok(urlsReturned.includes("https://google.com"));
    gGetTopSitesStub.resolves(FAKE_LINKS);
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
    );
    await cleanup();
  }

  {
    info(
      "TopSites.getLinksWithDefaults should filter out the current " +
        "default search from the default sites"
    );
    let cleanup = stubTopSites(sandbox);
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
      true
    );

    sandbox.stub(TopSites, "_currentSearchHostname").get(() => "amazon");
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.default.sites",
      "https://google.com,https://amazon.com"
    );
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

    let urlsReturned = (await TopSites.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(!urlsReturned.includes("https://amazon.com"));

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.default.sites"
    );
    gGetTopSitesStub.resolves(FAKE_LINKS);
    await cleanup();
  }

  {
    info(
      "TopSites.getLinksWithDefaults should not filter out current " +
        "default search from pinned sites even if it matches the current " +
        "default search"
    );
    let cleanup = stubTopSites(sandbox);
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
      true
    );

    sandbox
      .stub(NewTabUtils.pinnedLinks, "links")
      .get(() => [{ url: "google.com" }]);
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

    let urlsReturned = (await TopSites.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(urlsReturned.includes("google.com"));

    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
    );
    gGetTopSitesStub.resolves(FAKE_LINKS);
    await cleanup();
  }

  sandbox.restore();
});

add_task(
  async function test_improvesearch_noDefaultSearchTile_experiment_part_2() {
    let sandbox = sinon.createSandbox();

    sandbox.stub(SearchService, "getDefault").resolves({
      identifier: "google",
    });

    sandbox.stub(TopSites, "refresh");

    {
      info(
        "TopSites.getLinksWithDefaults should call refresh and set " +
          "._currentSearchHostname to the new engine hostname when the " +
          "default search engine has been set"
      );
      let cleanup = stubTopSites(sandbox);
      Services.prefs.setBoolPref(
        "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
        true
      );

      TopSites.observe(
        null,
        "browser-search-engine-modified",
        "engine-default"
      );
      Assert.equal(TopSites._currentSearchHostname, "duckduckgo");
      // Refresh is called twice:
      // 1) For the change of `noDefaultSearchTile`
      // 2) Default search engine changed "browser-search-engine-modified"
      Assert.ok(TopSites.refresh.calledTwice, "TopSites.refresh called twice");

      Services.prefs.clearUserPref(
        "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
      );
      gGetTopSitesStub.resolves(FAKE_LINKS);
      TopSites.refresh.resetHistory();
      await cleanup();
    }

    {
      info(
        "TopSites.getLinksWithDefaults should call refresh when the " +
          "experiment pref has changed"
      );
      let cleanup = stubTopSites(sandbox);
      Services.prefs.setBoolPref(
        "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
        true
      );
      Assert.ok(
        TopSites.refresh.calledOnce,
        "TopSites.refresh was called once"
      );

      Services.prefs.setBoolPref(
        "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
        false
      );
      Assert.ok(
        TopSites.refresh.calledTwice,
        "TopSites.refresh was called twice"
      );

      Services.prefs.clearUserPref(
        "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile"
      );
      gGetTopSitesStub.resolves(FAKE_LINKS);
      TopSites.refresh.resetHistory();
      await cleanup();
    }

    sandbox.restore();
  }
);

// eslint-disable-next-line max-statements
add_task(async function test_improvesearch_topSitesSearchShortcuts() {
  let sandbox = sinon.createSandbox();
  let searchEngines = [{ aliases: ["@google"] }, { aliases: ["@amazon"] }];
  sandbox.stub(SearchService, "getAppProvidedEngines").resolves(searchEngines);
  sandbox.stub(NewTabUtils.pinnedLinks, "pin").callsFake((site, index) => {
    NewTabUtils.pinnedLinks.links[index] = site;
  });

  let prepTopSites = () => {
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
      true
    );
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.searchEngines",
      "google,amazon"
    );
    Services.prefs.setStringPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.havePinned",
      ""
    );
  };

  {
    info(
      "TopSites should updateCustomSearchShortcuts when experiment " +
        "pref is turned on"
    );
    let cleanup = stubTopSites(sandbox);
    prepTopSites();
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
      false
    );
    // stubTopSites stubs updateCustomSearchShortcuts, when we need to add
    // a spy.
    TopSites.updateCustomSearchShortcuts.restore();
    sandbox.spy(TopSites, "updateCustomSearchShortcuts");

    // turn the experiment on
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
      true
    );

    Assert.ok(
      TopSites.updateCustomSearchShortcuts.calledOnce,
      "TopSites.updateCustomSearchShortcuts called once"
    );
    Services.prefs.clearUserPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts"
    );
    TopSites.updateCustomSearchShortcuts.restore();
    await cleanup();
  }

  {
    info(
      "TopSites should filter out default top sites that match a " +
        "hostname of a search shortcut if previously blocked"
    );
    let cleanup = stubTopSites(sandbox);
    prepTopSites();
    TopSites.refreshDefaults("https://amazon.ca");
    sandbox
      .stub(NewTabUtils.blockedLinks, "links")
      .value([{ url: "https://amazon.com" }]);
    sandbox.stub(NewTabUtils.blockedLinks, "isBlocked").callsFake(site => {
      return NewTabUtils.blockedLinks.links[0].url === site.url;
    });

    let urlsReturned = (await TopSites.getLinksWithDefaults()).map(
      link => link.url
    );
    Assert.ok(!urlsReturned.includes("https://amazon.ca"));
    await cleanup();
  }

  {
    info("TopSites should update frecent search topsite icon");
    let cleanup = stubTopSites(sandbox);
    prepTopSites();
    sandbox.stub(TopSites._tippyTopProvider, "processSite").callsFake(site => {
      site.tippyTopIcon = "icon.png";
      site.backgroundColor = "#fff";
      return site;
    });
    gGetTopSitesStub.resolves([{ url: "https://google.com" }]);

    let urlsReturned = await TopSites.getLinksWithDefaults();

    let defaultSearchTopsite = urlsReturned.find(
      s => s.url === "https://google.com"
    );
    Assert.ok(defaultSearchTopsite.searchTopSite);
    Assert.equal(defaultSearchTopsite.tippyTopIcon, "icon.png");
    Assert.equal(defaultSearchTopsite.backgroundColor, "#fff");
    gGetTopSitesStub.resolves(FAKE_LINKS);
    TopSites._tippyTopProvider.processSite.restore();
    await cleanup();
  }

  {
    info("TopSites should update default search topsite icon");
    let cleanup = stubTopSites(sandbox);
    prepTopSites();
    sandbox.stub(TopSites._tippyTopProvider, "processSite").callsFake(site => {
      site.tippyTopIcon = "icon.png";
      site.backgroundColor = "#fff";
      return site;
    });
    gGetTopSitesStub.resolves([{ url: "https://foo.com" }]);

    let urlsReturned = await TopSites.getLinksWithDefaults();

    let defaultSearchTopsite = urlsReturned.find(
      s => s.url === "https://amazon.com"
    );
    Assert.ok(defaultSearchTopsite.searchTopSite);
    Assert.equal(defaultSearchTopsite.tippyTopIcon, "icon.png");
    Assert.equal(defaultSearchTopsite.backgroundColor, "#fff");
    gGetTopSitesStub.resolves(FAKE_LINKS);
    TopSites._tippyTopProvider.processSite.restore();
    await cleanup();
  }

  {
    info(
      "TopSites should dispatch UPDATE_SEARCH_SHORTCUTS on " +
        "updateCustomSearchShortcuts"
    );
    let cleanup = stubTopSites(sandbox);
    prepTopSites();
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.noDefaultSearchTile",
      true
    );
    // stubTopSites stubs updateCustomSearchShortcuts, when in this case, we
    // want to check the effect of the method.
    TopSites.updateCustomSearchShortcuts.restore();
    await TopSites.updateCustomSearchShortcuts();
    let searchShortcuts = await TopSites.getSearchShortcuts();
    Assert.deepEqual(searchShortcuts, [
      {
        keyword: "@google",
        shortURL: "google",
        url: "https://google.com",
        backgroundColor: undefined,
        smallFavicon:
          "chrome://activity-stream/content/data/content/tippytop/favicons/google-com.ico",
        tippyTopIcon:
          "chrome://activity-stream/content/data/content/tippytop/images/google-com@2x.png",
      },
      {
        keyword: "@amazon",
        shortURL: "amazon",
        url: "https://amazon.com",
        backgroundColor: undefined,
        smallFavicon:
          "chrome://activity-stream/content/data/content/tippytop/favicons/amazon.ico",
        tippyTopIcon:
          "chrome://activity-stream/content/data/content/tippytop/images/amazon@2x.png",
      },
    ]);
    await cleanup();
  }

  {
    info(
      "TopSites should refresh when top sites search shortcut feature gate pref changes."
    );
    let cleanup = stubTopSites(sandbox);
    prepTopSites();

    let promise = TestUtils.topicObserved("topsites-refreshed");
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
      false
    );
    await promise;

    let sites = await TopSites.getSites();
    let searchTopSiteCount = sites.reduce(
      (acc, current) => (current.searchTopSite ? 1 : 0 + acc),
      0
    );
    Assert.equal(searchTopSiteCount, 0, "Number of search top sites.");

    promise = TestUtils.topicObserved("topsites-refreshed");
    Services.prefs.setBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts",
      true
    );
    await promise;
    sites = await TopSites.getSites();
    searchTopSiteCount = sites.reduce(
      (acc, current) => acc + (current.searchTopSite ? 1 : 0),
      0
    );
    Assert.equal(searchTopSiteCount, 2, "Number of search top sites.");
    await cleanup();
  }

  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.searchEngines"
  );
  Services.prefs.clearUserPref(
    "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts.havePinned"
  );
  sandbox.restore();
});

add_task(async function test_updatePinnedSearchShortcuts() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.pinnedLinks, "pin");
  sandbox.stub(NewTabUtils.pinnedLinks, "unpin");

  {
    info(
      "TopSites.updatePinnedSearchShortcuts should unpin a " +
        "shortcut in deletedShortcuts"
    );
    let cleanup = stubTopSites(sandbox);

    let deletedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let addedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      null,
      null,
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);
    TopSites.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.notCalled,
      "NewTabUtils.pinnedLinks.pin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledOnce,
      "NewTabUtils.pinnedLinks.unpin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledWith({
        url: "https://google.com",
      })
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    await cleanup();
  }

  {
    info(
      "TopSites.updatePinnedSearchShortcuts should pin a shortcut " +
        "in addedShortcuts"
    );
    let cleanup = stubTopSites(sandbox);

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      null,
      null,
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);
    TopSites.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });
    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.notCalled,
      "NewTabUtils.pinnedLinks.unpin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledOnce,
      "NewTabUtils.pinnedLinks.pin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith(
        {
          label: "google",
          searchTopSite: true,
          searchVendor: "google",
          url: "https://google.com",
        },
        0
      )
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    await cleanup();
  }

  {
    info(
      "TopSites.updatePinnedSearchShortcuts should pin and unpin " +
        "in the same action"
    );
    let cleanup = stubTopSites(sandbox);

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
      {
        url: "https://ebay.com",
        searchVendor: "ebay",
        label: "ebay",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ];

    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => [
      { url: "https://foo.com" },
      {
        url: "https://amazon.com",
        searchVendor: "amazon",
        label: "amazon",
        searchTopSite: true,
      },
    ]);
    TopSites.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });

    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.calledOnce,
      "NewTabUtils.pinnedLinks.unpin called once"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledTwice,
      "NewTabUtils.pinnedLinks.pin called twice"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    await cleanup();
  }

  {
    info(
      "TopSites.updatePinnedSearchShortcuts should pin a shortcut in " +
        "addedShortcuts even if pinnedLinks is full"
    );
    let cleanup = stubTopSites(sandbox);

    let addedShortcuts = [
      {
        url: "https://google.com",
        searchVendor: "google",
        label: "google",
        searchTopSite: true,
      },
    ];
    let deletedShortcuts = [];
    sandbox.stub(NewTabUtils.pinnedLinks, "links").get(() => FAKE_LINKS);
    TopSites.updatePinnedSearchShortcuts({ addedShortcuts, deletedShortcuts });

    Assert.ok(
      NewTabUtils.pinnedLinks.unpin.notCalled,
      "NewTabUtils.pinnedLinks.unpin not called"
    );
    Assert.ok(
      NewTabUtils.pinnedLinks.pin.calledWith(
        { label: "google", searchTopSite: true, url: "https://google.com" },
        0
      ),
      "NewTabUtils.pinnedLinks.unpin not called"
    );

    NewTabUtils.pinnedLinks.pin.resetHistory();
    NewTabUtils.pinnedLinks.unpin.resetHistory();
    await cleanup();
  }

  sandbox.restore();
});

add_task(async function test_insertPinned() {
  info("#insertPinned");

  function createLinks(count) {
    return new Array(count).fill(null).map((v, i) => ({ url: `site${i}.com` }));
  }

  info("should place pinned links where they belong");
  {
    let links = createLinks(12);
    const pinned = [
      { url: "http://github.com/mozilla/activity-stream", title: "moz/a-s" },
      { url: "http://example.com", title: "example" },
    ];

    const result = insertPinned(links, pinned);
    for (let index of [0, 1]) {
      Assert.equal(result[index].url, pinned[index].url, "Pinned URL matches");
      Assert.ok(result[index].isPinned, "Link is marked as pinned");
      Assert.equal(result[index].pinIndex, index, "Pin index is correct");
    }
    Assert.deepEqual(result.slice(2), links, "Remaining links are unchanged");
  }

  info("should handle empty slots in the pinned list");
  {
    let links = createLinks(12);
    const pinned = [
      null,
      { url: "http://github.com/mozilla/activity-stream", title: "moz/a-s" },
      null,
      null,
      { url: "http://example.com", title: "example" },
    ];

    const result = insertPinned(links, pinned);
    for (let index of [1, 4]) {
      Assert.equal(result[index].url, pinned[index].url, "Pinned URL matches");
      Assert.ok(result[index].isPinned, "Link is marked as pinned");
      Assert.equal(result[index].pinIndex, index, "Pin index is correct");
    }
    result.splice(4, 1);
    result.splice(1, 1);
    Assert.deepEqual(result, links, "Remaining links are unchanged");
  }

  info("should handle a pinned site past the end of the list of links");
  {
    const pinned = [];
    pinned[11] = {
      url: "http://github.com/mozilla/activity-stream",
      title: "moz/a-s",
    };

    const result = insertPinned([], pinned);
    Assert.equal(result[11].url, pinned[11].url, "Pinned URL matches");
    Assert.ok(result[11].isPinned, "Link is marked as pinned");
    Assert.equal(result[11].pinIndex, 11, "Pin index is correct");
  }

  info("should unpin previously pinned links no longer in the pinned list");
  {
    let links = createLinks(12);
    const pinned = [];
    links[2].isPinned = true;
    links[2].pinIndex = 2;

    const result = insertPinned(links, pinned);
    Assert.ok(!result[2].isPinned, "isPinned property removed");
    Assert.ok(!result[2].pinIndex, "pinIndex property removed");
  }

  info("should handle a link present in both the links and pinned list");
  {
    let links = createLinks(12);
    const pinned = [links[7]];

    const result = insertPinned(links, pinned);
    Assert.equal(links.length, result.length, "Length of links is unchanged");
  }

  info("should not modify the original data");
  {
    let links = createLinks(12);
    const pinned = [{ url: "http://example.com" }];

    insertPinned(links, pinned);

    Assert.equal(
      typeof pinned[0].isPinned,
      "undefined",
      "Pinned data is not mutated"
    );
  }
});
