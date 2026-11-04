/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  BOOKMARKS_RESTORE_SUCCESS_EVENT:
    "resource://newtab/lib/HighlightsFeed.sys.mjs",
  BOOKMARKS_RESTORE_FAILED_EVENT:
    "resource://newtab/lib/HighlightsFeed.sys.mjs",
  FilterAdult: "resource:///modules/FilterAdult.sys.mjs",
  HighlightsFeed: "resource://newtab/lib/HighlightsFeed.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  Screenshots: "resource://newtab/lib/Screenshots.sys.mjs",
  SectionsManager: "resource://newtab/lib/SectionsManager.sys.mjs",
  SECTION_ID: "resource://newtab/lib/HighlightsFeed.sys.mjs",
  SYNC_BOOKMARKS_FINISHED_EVENT: "resource://newtab/lib/HighlightsFeed.sys.mjs",
});

const FAKE_LINKS = new Array(20)
  .fill(null)
  .map((v, i) => ({ url: `http://www.site${i}.com` }));
const FAKE_IMAGE = "data123";
const FAKE_URL = "https://mozilla.org";
const FAKE_IMAGE_URL = "https://mozilla.org/preview.jpg";

function getHighlightsFeedForTest(sandbox) {
  let feed = new HighlightsFeed();
  feed.store = {
    dispatch: sandbox.spy(),
    getState() {
      return this.state;
    },
    state: {
      Prefs: {
        values: {
          "section.highlights.includeDownloads": false,
        },
      },
      TopSites: {
        initialized: true,
        rows: Array(12)
          .fill(null)
          .map((v, i) => ({ url: `http://www.topsite${i}.com` })),
      },
      Sections: [{ id: "highlights", initialized: false }],
    },
    subscribe: sandbox.stub().callsFake(cb => {
      cb();
      return () => {};
    }),
  };

  sandbox
    .stub(NewTabUtils.activityStreamLinks, "getHighlights")
    .resolves(FAKE_LINKS);
  sandbox
    .stub(NewTabUtils.activityStreamProvider, "_processHighlights")
    .callsFake(l => l.slice(0, 1));

  return feed;
}

async function fetchHighlightsRows(feed, options) {
  let sandbox = sinon.createSandbox();
  sandbox.stub(SectionsManager, "updateSection");
  await feed.fetchHighlights(options);
  let [, { rows }] = SectionsManager.updateSection.firstCall.args;

  sandbox.restore();
  return rows;
}

function fetchImage(feed, page) {
  return feed.fetchImage(
    Object.assign({ __sharedCache: { updateLink() {} } }, page)
  );
}

add_task(function test_construction() {
  info("HighlightsFeed construction should work");
  let sandbox = sinon.createSandbox();
  sandbox.stub(PageThumbs, "addExpirationFilter");

  let feed = getHighlightsFeedForTest(sandbox);
  Assert.ok(feed, "Was able to create a HighlightsFeed");

  info("HighlightsFeed construction should add a PageThumbs expiration filter");
  Assert.ok(
    PageThumbs.addExpirationFilter.calledOnce,
    "PageThumbs.addExpirationFilter was called once"
  );

  sandbox.restore();
});

add_task(function test_init_action() {
  let sandbox = sinon.createSandbox();

  let countObservers = topic => {
    return [...Services.obs.enumerateObservers(topic)].length;
  };

  const INITIAL_SYNC_BOOKMARKS_FINISHED_EVENT_COUNT = countObservers(
    SYNC_BOOKMARKS_FINISHED_EVENT
  );
  const INITIAL_BOOKMARKS_RESTORE_SUCCESS_EVENT_COUNT = countObservers(
    BOOKMARKS_RESTORE_SUCCESS_EVENT
  );
  const INITIAL_BOOKMARKS_RESTORE_FAILED_EVENT_COUNT = countObservers(
    BOOKMARKS_RESTORE_FAILED_EVENT
  );

  sandbox
    .stub(SectionsManager, "onceInitialized")
    .callsFake(callback => callback());
  sandbox.stub(SectionsManager, "enableSection");

  let feed = getHighlightsFeedForTest(sandbox);
  sandbox.stub(feed, "fetchHighlights");
  sandbox.stub(feed.downloadsManager, "init");

  feed.onAction({ type: actionTypes.INIT });

  info("HighlightsFeed.onAction(INIT) should add a sync observer");
  Assert.equal(
    countObservers(SYNC_BOOKMARKS_FINISHED_EVENT),
    INITIAL_SYNC_BOOKMARKS_FINISHED_EVENT_COUNT + 1
  );
  Assert.equal(
    countObservers(BOOKMARKS_RESTORE_SUCCESS_EVENT),
    INITIAL_BOOKMARKS_RESTORE_SUCCESS_EVENT_COUNT + 1
  );
  Assert.equal(
    countObservers(BOOKMARKS_RESTORE_FAILED_EVENT),
    INITIAL_BOOKMARKS_RESTORE_FAILED_EVENT_COUNT + 1
  );

  info(
    "HighlightsFeed.onAction(INIT) should call SectionsManager.onceInitialized"
  );
  Assert.ok(
    SectionsManager.onceInitialized.calledOnce,
    "SectionsManager.onceInitialized was called"
  );

  info("HighlightsFeed.onAction(INIT) should enable its section");
  Assert.ok(
    SectionsManager.enableSection.calledOnce,
    "SectionsManager.enableSection was called"
  );
  Assert.ok(SectionsManager.enableSection.calledWith(SECTION_ID));

  info("HighlightsFeed.onAction(INIT) should fetch highlights");
  Assert.ok(
    feed.fetchHighlights.calledOnce,
    "HighlightsFeed.fetchHighlights was called"
  );

  info("HighlightsFeed.onAction(INIT) should initialize the DownloadsManager");
  Assert.ok(
    feed.downloadsManager.init.calledOnce,
    "HighlightsFeed.downloadsManager.init was called"
  );

  feed.uninit();
  // Let's make sure that uninit also removed these observers while we're here.
  Assert.equal(
    countObservers(SYNC_BOOKMARKS_FINISHED_EVENT),
    INITIAL_SYNC_BOOKMARKS_FINISHED_EVENT_COUNT
  );
  Assert.equal(
    countObservers(BOOKMARKS_RESTORE_SUCCESS_EVENT),
    INITIAL_BOOKMARKS_RESTORE_SUCCESS_EVENT_COUNT
  );
  Assert.equal(
    countObservers(BOOKMARKS_RESTORE_FAILED_EVENT),
    INITIAL_BOOKMARKS_RESTORE_FAILED_EVENT_COUNT
  );

  sandbox.restore();
});

add_task(async function test_observe_fetch_highlights() {
  let topicDataPairs = [
    {
      description:
        "should fetch highlights when we are done a sync for bookmarks",
      shouldFetch: true,
      topic: SYNC_BOOKMARKS_FINISHED_EVENT,
      data: "bookmarks",
    },
    {
      description: "should fetch highlights after a successful import",
      shouldFetch: true,
      topic: BOOKMARKS_RESTORE_SUCCESS_EVENT,
      data: "html",
    },
    {
      description: "should fetch highlights after a failed import",
      shouldFetch: true,
      topic: BOOKMARKS_RESTORE_FAILED_EVENT,
      data: "json",
    },
    {
      description:
        "should not fetch highlights when we are doing a sync for something that is not bookmarks",
      shouldFetch: false,
      topic: SYNC_BOOKMARKS_FINISHED_EVENT,
      data: "tabs",
    },
    {
      description: "should not fetch highlights after a successful import",
      shouldFetch: false,
      topic: "someotherevent",
      data: "bookmarks",
    },
  ];

  for (let topicDataPair of topicDataPairs) {
    info(`HighlightsFeed.observe ${topicDataPair.description}`);
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);
    sandbox.stub(feed, "fetchHighlights");
    feed.observe(null, topicDataPair.topic, topicDataPair.data);

    if (topicDataPair.shouldFetch) {
      Assert.ok(
        feed.fetchHighlights.calledOnce,
        "HighlightsFeed.fetchHighlights was called"
      );
      Assert.ok(feed.fetchHighlights.calledWith({ broadcast: true }));
    } else {
      Assert.ok(
        feed.fetchHighlights.notCalled,
        "HighlightsFeed.fetchHighlights was not called"
      );
    }

    sandbox.restore();
  }
});

add_task(async function test_filterForThumbnailExpiration_calls() {
  info(
    "HighlightsFeed.filterForThumbnailExpiration should pass rows.urls " +
      "to the callback provided"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  let rows = [{ url: "foo.com" }, { url: "bar.com" }];

  feed.store.state.Sections = [{ id: "highlights", rows, initialized: true }];
  const stub = sinon.stub();

  feed.filterForThumbnailExpiration(stub);

  Assert.ok(stub.calledOnce, "Filter was called");
  Assert.ok(stub.calledWithExactly(rows.map(r => r.url)));

  sandbox.restore();
});

add_task(
  async function test_filterForThumbnailExpiration_include_preview_image_url() {
    info(
      "HighlightsFeed.filterForThumbnailExpiration should include " +
        "preview_image_url (if present) in the callback results"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);
    let rows = [
      { url: "foo.com" },
      { url: "bar.com", preview_image_url: "bar.jpg" },
    ];

    feed.store.state.Sections = [{ id: "highlights", rows, initialized: true }];
    const stub = sinon.stub();

    feed.filterForThumbnailExpiration(stub);

    Assert.ok(stub.calledOnce, "Filter was called");
    Assert.ok(stub.calledWithExactly(["foo.com", "bar.com", "bar.jpg"]));

    sandbox.restore();
  }
);

add_task(async function test_filterForThumbnailExpiration_not_initialized() {
  info(
    "HighlightsFeed.filterForThumbnailExpiration should pass an empty " +
      "array if not initialized"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  let rows = [{ url: "foo.com" }, { url: "bar.com" }];

  feed.store.state.Sections = [{ rows, initialized: false }];
  const stub = sinon.stub();

  feed.filterForThumbnailExpiration(stub);

  Assert.ok(stub.calledOnce, "Filter was called");
  Assert.ok(stub.calledWithExactly([]));

  sandbox.restore();
});

add_task(async function test_fetchHighlights_TopSites_not_initialized() {
  info(
    "HighlightsFeed.fetchHighlights should return early if TopSites are not " +
      "initialized"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.spy(feed.linksCache, "request");

  feed.store.state.TopSites.initialized = false;
  feed.store.state.Prefs.values["feeds.topsites"] = true;
  feed.store.state.Prefs.values["feeds.system.topsites"] = true;

  // Initially TopSites is uninitialised and fetchHighlights should return.
  await feed.fetchHighlights();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getHighlights.notCalled,
    "NewTabUtils.activityStreamLinks.getHighlights was not called"
  );
  Assert.ok(
    feed.linksCache.request.notCalled,
    "HighlightsFeed.linksCache.request was not called"
  );

  sandbox.restore();
});

add_task(async function test_fetchHighlights_sections_not_initialized() {
  info(
    "HighlightsFeed.fetchHighlights should return early if Sections are not " +
      "initialized"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.spy(feed.linksCache, "request");

  feed.store.state.TopSites.initialized = true;
  feed.store.state.Prefs.values["feeds.topsites"] = true;
  feed.store.state.Prefs.values["feeds.system.topsites"] = true;
  feed.store.state.Sections = [];

  await feed.fetchHighlights();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getHighlights.notCalled,
    "NewTabUtils.activityStreamLinks.getHighlights was not called"
  );
  Assert.ok(
    feed.linksCache.request.notCalled,
    "HighlightsFeed.linksCache.request was not called"
  );

  sandbox.restore();
});

add_task(async function test_fetchHighlights_TopSites_initialized() {
  info(
    "HighlightsFeed.fetchHighlights should fetch Highlights if TopSites are " +
      "initialised"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.spy(feed.linksCache, "request");

  // fetchHighlights should continue
  feed.store.state.TopSites.initialized = true;

  await feed.fetchHighlights();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getHighlights.calledOnce,
    "NewTabUtils.activityStreamLinks.getHighlights was called"
  );
  Assert.ok(
    feed.linksCache.request.calledOnce,
    "HighlightsFeed.linksCache.request was called"
  );

  sandbox.restore();
});

add_task(async function test_fetchHighlights_chronological_order() {
  info(
    "HighlightsFeed.fetchHighlights should chronologically order highlight " +
      "data types"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let links = [
    {
      url: "https://site0.com",
      type: "bookmark",
      bookmarkGuid: "1234",
      date_added: Date.now() - 80,
    }, // 3rd newest
    {
      url: "https://site1.com",
      type: "history",
      bookmarkGuid: "1234",
      date_added: Date.now() - 60,
    }, // append at the end
    {
      url: "https://site2.com",
      type: "history",
      date_added: Date.now() - 160,
    }, // append at the end
    {
      url: "https://site3.com",
      type: "history",
      date_added: Date.now() - 60,
    }, // append at the end
    {
      url: "https://site4.com",
      type: "bookmark",
      bookmarkGuid: "1234",
      date_added: Date.now(),
    }, // newest highlight
    {
      url: "https://site5.com",
      type: "bookmark",
      bookmarkGuid: "12345",
      date_added: Date.now() - 100,
    }, // 4th newest
    {
      url: "https://site6.com",
      type: "bookmark",
      bookmarkGuid: "1234",
      date_added: Date.now() - 40,
    }, // 2nd newest
  ];
  let expectedChronological = [4, 6, 0, 5];
  let expectedHistory = [1, 2, 3];
  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  [...expectedChronological, ...expectedHistory].forEach((link, index) => {
    Assert.equal(
      highlights[index].url,
      links[link].url,
      `highlight[${index}] should be link[${link}]`
    );
  });

  sandbox.restore();
});

add_task(async function test_fetchHighlights_TopSites_not_enabled() {
  info(
    "HighlightsFeed.fetchHighlights should fetch Highlights if TopSites " +
      "are not enabled"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.spy(feed.linksCache, "request");

  feed.store.state.Prefs.values["feeds.system.topsites"] = false;

  await feed.fetchHighlights();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getHighlights.calledOnce,
    "NewTabUtils.activityStreamLinks.getHighlights was called"
  );
  Assert.ok(
    feed.linksCache.request.calledOnce,
    "HighlightsFeed.linksCache.request was called"
  );

  sandbox.restore();
});

add_task(async function test_fetchHighlights_TopSites_not_shown() {
  info(
    "HighlightsFeed.fetchHighlights should fetch Highlights if TopSites " +
      "are not shown on NTP"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.spy(feed.linksCache, "request");

  feed.store.state.Prefs.values["feeds.topsites"] = false;

  await feed.fetchHighlights();

  Assert.ok(
    NewTabUtils.activityStreamLinks.getHighlights.calledOnce,
    "NewTabUtils.activityStreamLinks.getHighlights was called"
  );
  Assert.ok(
    feed.linksCache.request.calledOnce,
    "HighlightsFeed.linksCache.request was called"
  );

  sandbox.restore();
});

add_task(async function test_fetchHighlights_add_hostname_hasImage() {
  info(
    "HighlightsFeed.fetchHighlights should add shortURL hostname and hasImage to each link"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let links = [{ url: "https://mozilla.org" }];
  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights[0].hostname, NewTabUtils.shortURL(links[0]));
  Assert.equal(highlights[0].hasImage, true);

  sandbox.restore();
});

add_task(async function test_fetchHighlights_add_existing_image() {
  info(
    "HighlightsFeed.fetchHighlights should add an existing image if it " +
      "exists to the link without calling fetchImage"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let links = [{ url: "https://mozilla.org", image: FAKE_IMAGE }];
  sandbox.spy(feed, "fetchImage");
  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights[0].image, FAKE_IMAGE);
  Assert.ok(feed.fetchImage.notCalled, "HighlightsFeed.fetchImage not called");

  sandbox.restore();
});

add_task(async function test_fetchHighlights_correct_args() {
  info(
    "HighlightsFeed.fetchHighlights should call fetchImage with the correct " +
      "arguments for new links"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let links = [
    {
      url: "https://mozilla.org",
      preview_image_url: "https://mozilla.org/preview.jog",
    },
  ];
  sandbox.spy(feed, "fetchImage");
  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  await fetchHighlightsRows(feed);

  Assert.ok(feed.fetchImage.calledOnce, "HighlightsFeed.fetchImage called");

  let [arg] = feed.fetchImage.firstCall.args;
  Assert.equal(arg.url, links[0].url);
  Assert.equal(arg.preview_image_url, links[0].preview_image_url);

  sandbox.restore();
});

add_task(
  async function test_fetchHighlights_not_include_links_already_in_TopSites() {
    info(
      "HighlightsFeed.fetchHighlights should not include any links already in " +
        "Top Sites"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    let links = [
      { url: "https://mozilla.org" },
      { url: "http://www.topsite0.com" },
      { url: "http://www.topsite1.com" },
      { url: "http://www.topsite2.com" },
    ];
    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights.length, 1);
    Assert.equal(highlights[0].url, links[0].url);

    sandbox.restore();
  }
);

add_task(
  async function test_fetchHighlights_not_include_history_already_in_TopSites() {
    info(
      "HighlightsFeed.fetchHighlights should include bookmark but not " +
        "history already in Top Sites"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    let links = [
      { url: "http://www.topsite0.com", type: "bookmark" },
      { url: "http://www.topsite1.com", type: "history" },
    ];
    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights.length, 1);
    Assert.equal(highlights[0].url, links[0].url);

    sandbox.restore();
  }
);

add_task(
  async function test_fetchHighlights_not_include_history_same_hostname_as_bookmark() {
    info(
      "HighlightsFeed.fetchHighlights should not include history of same " +
        "hostname as a bookmark"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    let links = [
      { url: "https://site.com/bookmark", type: "bookmark" },
      { url: "https://site.com/history", type: "history" },
    ];
    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights.length, 1);
    Assert.equal(highlights[0].url, links[0].url);

    sandbox.restore();
  }
);

add_task(async function test_fetchHighlights_take_first_history_of_hostname() {
  info(
    "HighlightsFeed.fetchHighlights should take the first history of a hostname"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let links = [
    { url: "https://site.com/first", type: "history" },
    { url: "https://site.com/second", type: "history" },
    { url: "https://other", type: "history" },
  ];
  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights.length, 2);
  Assert.equal(highlights[0].url, links[0].url);
  Assert.equal(highlights[1].url, links[2].url);

  sandbox.restore();
});

add_task(
  async function test_fetchHighlights_take_bookmark_pocket_download_of_same_hostname() {
    info(
      "HighlightsFeed.fetchHighlights should take a bookmark, a pocket, and " +
        "downloaded item of the same hostname"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    let links = [
      { url: "https://site.com/bookmark", type: "bookmark" },
      { url: "https://site.com/pocket", type: "pocket" },
      { url: "https://site.com/download", type: "download" },
    ];
    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights.length, 3);
    Assert.equal(highlights[0].url, links[0].url);
    Assert.equal(highlights[1].url, links[1].url);
    Assert.equal(highlights[2].url, links[2].url);

    sandbox.restore();
  }
);

add_task(async function test_fetchHighlights_do_not_include_downloads() {
  info(
    "HighlightsFeed.fetchHighlights should not include downloads when " +
      "includeDownloads pref is false"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.store.state.Prefs.values["section.highlights.includeDownloads"] = false;
  feed.downloadsManager.getDownloads = () => [
    { url: "https://site1.com/download" },
    { url: "https://site2.com/download" },
  ];

  let links = [
    { url: "https://site.com/bookmark", type: "bookmark" },
    { url: "https://site.com/pocket", type: "pocket" },
  ];

  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights.length, 2);
  Assert.equal(highlights[0].url, links[0].url);
  Assert.equal(highlights[1].url, links[1].url);

  sandbox.restore();
});

add_task(async function test_fetchHighlights_include_downloads() {
  info(
    "HighlightsFeed.fetchHighlights should include downloads when " +
      "includeDownloads pref is true"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.store.state.Prefs.values["section.highlights.includeDownloads"] = true;
  feed.downloadsManager.getDownloads = () => [
    { url: "https://site.com/download" },
  ];

  let links = [
    { url: "https://site.com/bookmark", type: "bookmark" },
    { url: "https://site.com/pocket", type: "pocket" },
  ];

  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights.length, 3);
  Assert.equal(highlights[0].url, links[0].url);
  Assert.equal(highlights[1].url, links[1].url);
  Assert.equal(highlights[2].url, "https://site.com/download");
  Assert.equal(highlights[2].type, "download");

  sandbox.restore();
});

add_task(async function test_fetchHighlights_take_one_download() {
  info("HighlightsFeed.fetchHighlights should only take 1 download");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.store.state.Prefs.values["section.highlights.includeDownloads"] = true;
  feed.downloadsManager.getDownloads = () => [
    { url: "https://site1.com/download" },
    { url: "https://site2.com/download" },
  ];

  let links = [{ url: "https://site.com/bookmark", type: "bookmark" }];

  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights.length, 2);
  Assert.equal(highlights[0].url, links[0].url);
  Assert.equal(highlights[1].url, "https://site1.com/download");

  sandbox.restore();
});

add_task(async function test_fetchHighlights_chronological_sort() {
  info(
    "HighlightsFeed.fetchHighlights should sort bookmarks and downloads chronologically"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.store.state.Prefs.values["section.highlights.includeDownloads"] = true;
  feed.downloadsManager.getDownloads = () => [
    {
      url: "https://site1.com/download",
      type: "download",
      date_added: Date.now(),
    },
  ];

  let links = [
    {
      url: "https://site.com/bookmark",
      type: "bookmark",
      date_added: Date.now() - 10000,
    },
    {
      url: "https://site2.com/another-bookmark",
      type: "bookmark",
      date_added: Date.now() - 5000,
    },
    {
      url: "https://site3.com/visited",
      type: "history",
      date_added: Date.now(),
    },
  ];

  NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

  let highlights = await fetchHighlightsRows(feed);

  Assert.equal(highlights.length, 4);
  Assert.equal(highlights[0].url, "https://site1.com/download");
  Assert.equal(highlights[1].url, links[1].url);
  Assert.equal(highlights[2].url, links[0].url);
  Assert.equal(highlights[3].url, links[2].url); // history item goes last

  sandbox.restore();
});

add_task(
  async function test_fetchHighlights_set_type_to_bookmark_on_bookmarkGuid() {
    info(
      "HighlightsFeed.fetchHighlights should set type to bookmark if there " +
        "is a bookmarkGuid"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);
    feed.store.state.Prefs.values["section.highlights.includeBookmarks"] = true;
    feed.downloadsManager.getDownloads = () => [
      {
        url: "https://site1.com/download",
        type: "download",
        date_added: Date.now(),
      },
    ];

    let links = [
      {
        url: "https://mozilla.org",
        type: "history",
        bookmarkGuid: "1234567890",
      },
    ];

    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights[0].type, "bookmark");

    sandbox.restore();
  }
);

add_task(
  async function test_fetchHighlights_keep_history_type_on_bookmarkGuid() {
    info(
      "HighlightsFeed.fetchHighlights should keep history type if there is a " +
        "bookmarkGuid but don't include bookmarks"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);
    feed.store.state.Prefs.values["section.highlights.includeBookmarks"] =
      false;

    let links = [
      {
        url: "https://mozilla.org",
        type: "history",
        bookmarkGuid: "1234567890",
      },
    ];

    NewTabUtils.activityStreamLinks.getHighlights.resolves(links);

    let highlights = await fetchHighlightsRows(feed);

    Assert.equal(highlights[0].type, "history");

    sandbox.restore();
  }
);

add_task(async function test_fetchHighlights_filter_adult() {
  info("HighlightsFeed.fetchHighlights should filter out adult pages");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.stub(FilterAdult, "filter").returns([]);
  let highlights = await fetchHighlightsRows(feed);

  Assert.ok(FilterAdult.filter.calledOnce, "FilterAdult.filter called");
  Assert.equal(highlights.length, 0);

  sandbox.restore();
});

add_task(async function test_fetchHighlights_no_expose_internal_link_props() {
  info(
    "HighlightsFeed.fetchHighlights should not expose internal link properties"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let highlights = await fetchHighlightsRows(feed);
  let internal = Object.keys(highlights[0]).filter(key => key.startsWith("__"));

  Assert.equal(internal.join(""), "");

  sandbox.restore();
});

add_task(
  async function test_fetchHighlights_broadcast_when_feed_not_initialized() {
    info(
      "HighlightsFeed.fetchHighlights should broadcast if feed is not initialized"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    NewTabUtils.activityStreamLinks.getHighlights.resolves([]);
    sandbox.stub(SectionsManager, "updateSection");
    await feed.fetchHighlights();

    Assert.ok(
      SectionsManager.updateSection.calledOnce,
      "SectionsManager.updateSection called once"
    );
    Assert.ok(
      SectionsManager.updateSection.calledWithExactly(
        SECTION_ID,
        { rows: [] },
        true,
        undefined
      )
    );
    sandbox.restore();
  }
);

add_task(
  async function test_fetchHighlights_broadcast_on_broadcast_in_options() {
    info(
      "HighlightsFeed.fetchHighlights should broadcast if options.broadcast is true"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    NewTabUtils.activityStreamLinks.getHighlights.resolves([]);
    feed.store.state.Sections[0].initialized = true;

    sandbox.stub(SectionsManager, "updateSection");
    await feed.fetchHighlights({ broadcast: true });

    Assert.ok(
      SectionsManager.updateSection.calledOnce,
      "SectionsManager.updateSection called once"
    );
    Assert.ok(
      SectionsManager.updateSection.calledWithExactly(
        SECTION_ID,
        { rows: [] },
        true,
        undefined
      )
    );
    sandbox.restore();
  }
);

add_task(async function test_fetchHighlights_no_broadcast() {
  info(
    "HighlightsFeed.fetchHighlights should not broadcast if " +
      "options.broadcast is false and initialized is true"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  NewTabUtils.activityStreamLinks.getHighlights.resolves([]);
  feed.store.state.Sections[0].initialized = true;

  sandbox.stub(SectionsManager, "updateSection");
  await feed.fetchHighlights({ broadcast: false });

  Assert.ok(
    SectionsManager.updateSection.calledOnce,
    "SectionsManager.updateSection called once"
  );
  Assert.ok(
    SectionsManager.updateSection.calledWithExactly(
      SECTION_ID,
      { rows: [] },
      false,
      undefined
    )
  );
  sandbox.restore();
});

add_task(async function test_fetchImage_capture_if_available() {
  info("HighlightsFeed.fetchImage should capture the image, if available");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.stub(Screenshots, "getScreenshotForURL");
  sandbox.stub(Screenshots, "_shouldGetScreenshots").returns(true);

  await fetchImage(feed, {
    preview_image_url: FAKE_IMAGE_URL,
    url: FAKE_URL,
  });

  Assert.ok(
    Screenshots.getScreenshotForURL.calledOnce,
    "Screenshots.getScreenshotForURL called once"
  );
  Assert.ok(Screenshots.getScreenshotForURL.calledWith(FAKE_IMAGE_URL));

  sandbox.restore();
});

add_task(async function test_fetchImage_fallback_to_screenshot() {
  info("HighlightsFeed.fetchImage should fall back to capturing a screenshot");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.stub(Screenshots, "getScreenshotForURL");
  sandbox.stub(Screenshots, "_shouldGetScreenshots").returns(true);

  await fetchImage(feed, { url: FAKE_URL });

  Assert.ok(
    Screenshots.getScreenshotForURL.calledOnce,
    "Screenshots.getScreenshotForURL called once"
  );
  Assert.ok(Screenshots.getScreenshotForURL.calledWith(FAKE_URL));

  sandbox.restore();
});

add_task(async function test_fetchImage_updateSectionCard_args() {
  info(
    "HighlightsFeed.fetchImage should call " +
      "SectionsManager.updateSectionCard with the right arguments"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.stub(SectionsManager, "updateSectionCard");
  sandbox.stub(Screenshots, "getScreenshotForURL").resolves(FAKE_IMAGE);
  sandbox.stub(Screenshots, "_shouldGetScreenshots").returns(true);

  await fetchImage(feed, {
    preview_image_url: FAKE_IMAGE_URL,
    url: FAKE_URL,
  });
  Assert.ok(
    SectionsManager.updateSectionCard.calledOnce,
    "SectionsManager.updateSectionCard called"
  );
  Assert.ok(
    SectionsManager.updateSectionCard.calledWith(
      "highlights",
      FAKE_URL,
      { image: FAKE_IMAGE },
      true
    )
  );
  sandbox.restore();
});

add_task(async function test_fetchImage_no_update_card_with_image() {
  info("HighlightsFeed.fetchImage should not update the card with the image");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  sandbox.stub(SectionsManager, "updateSectionCard");
  sandbox.stub(Screenshots, "getScreenshotForURL").resolves(FAKE_IMAGE);
  sandbox.stub(Screenshots, "_shouldGetScreenshots").returns(true);

  let card = {
    preview_image_url: FAKE_IMAGE_URL,
    url: FAKE_URL,
  };
  await fetchImage(feed, card);
  Assert.ok(!card.image, "Image not set on card");
  sandbox.restore();
});

add_task(async function test_uninit_disable_section() {
  info("HighlightsFeed.onAction(UNINIT) should disable its section");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.init();

  sandbox.stub(SectionsManager, "disableSection");
  feed.onAction({ type: actionTypes.UNINIT });
  Assert.ok(
    SectionsManager.disableSection.calledOnce,
    "SectionsManager.disableSection called"
  );
  Assert.ok(SectionsManager.disableSection.calledWith(SECTION_ID));
  sandbox.restore();
});

add_task(async function test_uninit_remove_expiration_filter() {
  info("HighlightsFeed.onAction(UNINIT) should remove the expiration filter");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  feed.init();

  sandbox.stub(PageThumbs, "removeExpirationFilter");
  feed.onAction({ type: actionTypes.UNINIT });
  Assert.ok(
    PageThumbs.removeExpirationFilter.calledOnce,
    "PageThumbs.removeExpirationFilter called"
  );

  sandbox.restore();
});

add_task(async function test_onAction_relay_to_DownloadsManager_onAction() {
  info(
    "HighlightsFeed.onAction should relay all actions to " +
      "DownloadsManager.onAction"
  );
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);
  sandbox.stub(feed.downloadsManager, "onAction");

  let action = {
    type: actionTypes.COPY_DOWNLOAD_LINK,
    data: { url: "foo.png" },
    _target: {},
  };
  feed.onAction(action);

  Assert.ok(
    feed.downloadsManager.onAction.calledOnce,
    "HighlightsFeed.downloadManager.onAction called"
  );
  Assert.ok(feed.downloadsManager.onAction.calledWith(action));
  sandbox.restore();
});

add_task(async function test_onAction_fetch_highlights_on_SYSTEM_TICK() {
  info("HighlightsFeed.onAction should fetch highlights on SYSTEM_TICK");
  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  await feed.fetchHighlights();

  sandbox.spy(feed, "fetchHighlights");
  feed.onAction({ type: actionTypes.SYSTEM_TICK });

  Assert.ok(
    feed.fetchHighlights.calledOnce,
    "HighlightsFeed.fetchHighlights called"
  );
  Assert.ok(
    feed.fetchHighlights.calledWithExactly({
      broadcast: false,
      isStartup: false,
    })
  );
  sandbox.restore();
});

add_task(
  async function test_onAction_fetch_highlights_on_PREF_CHANGED_for_include() {
    info(
      "HighlightsFeed.onAction should fetch highlights on PREF_CHANGED " +
        "for include prefs"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    sandbox.spy(feed, "fetchHighlights");
    feed.onAction({
      type: actionTypes.PREF_CHANGED,
      data: { name: "section.highlights.includeBookmarks" },
    });

    Assert.ok(
      feed.fetchHighlights.calledOnce,
      "HighlightsFeed.fetchHighlights called"
    );
    Assert.ok(feed.fetchHighlights.calledWithExactly({ broadcast: true }));
    sandbox.restore();
  }
);

add_task(
  async function test_onAction_no_fetch_highlights_on_PREF_CHANGED_for_other() {
    info(
      "HighlightsFeed.onAction should not fetch highlights on PREF_CHANGED " +
        "for other prefs"
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    sandbox.spy(feed, "fetchHighlights");
    feed.onAction({
      type: actionTypes.PREF_CHANGED,
      data: { name: "section.topstories.pocketCta" },
    });

    Assert.ok(
      feed.fetchHighlights.notCalled,
      "HighlightsFeed.fetchHighlights not called"
    );

    sandbox.restore();
  }
);

add_task(async function test_onAction_fetch_highlights_on_actions() {
  info("HighlightsFeed.onAction should fetch highlights for various actions");

  let actions = [
    {
      actionType: "PLACES_HISTORY_CLEARED",
      expectsExpire: false,
      expectsBroadcast: true,
    },
    {
      actionType: "DOWNLOAD_CHANGED",
      expectsExpire: false,
      expectsBroadcast: true,
    },
    {
      actionType: "PLACES_LINKS_CHANGED",
      expectsExpire: true,
      expectsBroadcast: false,
    },
    {
      actionType: "PLACES_LINK_BLOCKED",
      expectsExpire: false,
      expectsBroadcast: true,
    },
  ];
  for (let action of actions) {
    info(
      `HighlightsFeed.onAction should fetch highlights on ${action.actionType}`
    );
    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    await feed.fetchHighlights();
    sandbox.spy(feed, "fetchHighlights");
    sandbox.stub(feed.linksCache, "expire");

    feed.onAction({ type: actionTypes[action.actionType] });
    Assert.ok(
      feed.fetchHighlights.calledOnce,
      "HighlightsFeed.fetchHighlights called"
    );
    Assert.ok(
      feed.fetchHighlights.calledWith({ broadcast: action.expectsBroadcast })
    );

    if (action.expectsExpire) {
      Assert.ok(
        feed.linksCache.expire.calledOnce,
        "HighlightsFeed.linksCache.expire called"
      );
    }

    sandbox.restore();
  }
});

add_task(
  async function test_onAction_fetch_highlights_no_broadcast_on_TOP_SITES_UPDATED() {
    info(
      "HighlightsFeed.onAction should fetch highlights with broadcast " +
        "false on TOP_SITES_UPDATED"
    );

    let sandbox = sinon.createSandbox();
    let feed = getHighlightsFeedForTest(sandbox);

    await feed.fetchHighlights();
    sandbox.spy(feed, "fetchHighlights");

    feed.onAction({ type: actionTypes.TOP_SITES_UPDATED });
    Assert.ok(
      feed.fetchHighlights.calledOnce,
      "HighlightsFeed.fetchHighlights called"
    );
    Assert.ok(
      feed.fetchHighlights.calledWithExactly({
        broadcast: false,
        isStartup: false,
      })
    );

    sandbox.restore();
  }
);

add_task(function test_orderHighlights_sorts_by_date_added_descending() {
  info(
    "_orderHighlights should sort non-history items by date_added descending"
  );

  let sandbox = sinon.createSandbox();
  let feed = getHighlightsFeedForTest(sandbox);

  let pages = [
    { url: "http://oldest.com", type: "bookmark", date_added: 100 },
    { url: "http://newest.com", type: "bookmark", date_added: 300 },
    { url: "http://middle.com", type: "bookmark", date_added: 200 },
    { url: "http://visited.com", type: "history" },
  ];

  let result = feed._orderHighlights(pages);

  Assert.equal(result.length, 4, "All items are returned");
  Assert.equal(result[0].url, "http://newest.com", "Newest bookmark is first");
  Assert.equal(result[1].url, "http://middle.com", "Middle bookmark is second");
  Assert.equal(result[2].url, "http://oldest.com", "Oldest bookmark is third");
  Assert.equal(
    result[3].url,
    "http://visited.com",
    "Visited item is appended at the end"
  );

  sandbox.restore();
});
