/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PartnerLinkAttribution: "resource:///modules/PartnerLinkAttribution.sys.mjs",
  pktApi: "chrome://pocket/content/pktApi.sys.mjs",
  PlacesFeed: "resource://newtab/lib/PlacesFeed.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchService: "resource://gre/modules/SearchService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

const FAKE_BOOKMARK = {
  bookmarkGuid: "D3r1sKRobtbW",
  bookmarkTitle: "Foo",
  dateAdded: 123214232,
  url: "foo.com",
};
const TYPE_BOOKMARK = 1; // This is fake, for testing
const SOURCES = {
  DEFAULT: 0,
  SYNC: 1,
  IMPORT: 2,
  RESTORE: 5,
  RESTORE_ON_STARTUP: 6,
};

// The event dispatched in NewTabUtils when a link is blocked;
const BLOCKED_EVENT = "newtab-linkBlocked";

const TOP_SITES_BLOCKED_SPONSORS_PREF = "browser.topsites.blockedSponsors";
const POCKET_SITE_PREF = "extensions.pocket.site";

function getPlacesFeedForTest(sandbox) {
  let feed = new PlacesFeed();
  feed.store = {
    dispatch: sandbox.spy(),
    feeds: {
      get: sandbox.stub(),
    },
  };

  sandbox.stub(AboutNewTab, "activityStream").value({
    store: feed.store,
  });

  return feed;
}

add_task(async function test_construction() {
  info("PlacesFeed construction should work");
  let feed = new PlacesFeed();
  Assert.ok(feed, "PlacesFeed could be constructed.");
});

add_task(async function test_PlacesObserver() {
  info("PlacesFeed should have a PlacesObserver that dispatches to the store");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);

  let action = { type: "FOO" };
  feed.placesObserver.dispatch(action);

  await TestUtils.waitForTick();
  Assert.ok(feed.store.dispatch.calledOnce, "PlacesFeed.store dispatch called");
  Assert.equal(feed.store.dispatch.firstCall.args[0].type, action.type);

  sandbox.restore();
});

add_task(async function test_addToBlockedTopSitesSponsors_add_to_blocklist() {
  info(
    "PlacesFeed.addToBlockedTopSitesSponsors should add the blocked sponsors " +
      "to the blocklist"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  Services.prefs.setStringPref(
    TOP_SITES_BLOCKED_SPONSORS_PREF,
    `["foo","bar"]`
  );

  feed.addToBlockedTopSitesSponsors([
    { url: "test.com" },
    { url: "test1.com" },
  ]);

  let blockedSponsors = JSON.parse(
    Services.prefs.getStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF)
  );
  Assert.deepEqual(
    new Set(["foo", "bar", "test", "test1"]),
    new Set(blockedSponsors)
  );

  Services.prefs.clearUserPref(TOP_SITES_BLOCKED_SPONSORS_PREF);
  sandbox.restore();
});

add_task(async function test_addToBlockedTopSitesSponsors_no_dupes() {
  info(
    "PlacesFeed.addToBlockedTopSitesSponsors should not add duplicate " +
      "sponsors to the blocklist"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  Services.prefs.setStringPref(
    TOP_SITES_BLOCKED_SPONSORS_PREF,
    `["foo","bar"]`
  );

  feed.addToBlockedTopSitesSponsors([
    { url: "foo.com" },
    { url: "bar.com" },
    { url: "test.com" },
  ]);

  let blockedSponsors = JSON.parse(
    Services.prefs.getStringPref(TOP_SITES_BLOCKED_SPONSORS_PREF)
  );
  Assert.deepEqual(new Set(["foo", "bar", "test"]), new Set(blockedSponsors));

  Services.prefs.clearUserPref(TOP_SITES_BLOCKED_SPONSORS_PREF);
  sandbox.restore();
});

add_task(async function test_onAction_PlacesEvents() {
  info(
    "PlacesFeed.onAction should add bookmark, history, places, blocked " +
      "observers on INIT"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed.placesObserver, "handlePlacesEvent");

  feed.onAction({ type: actionTypes.INIT });
  // The PlacesObserver registration happens at the next tick of the
  // event loop.
  await TestUtils.waitForTick();

  // These are some dummy PlacesEvents that we'll pass through the
  // PlacesObserver service, checking that the handlePlacesEvent receives them
  // properly.
  let notifications = [
    new PlacesBookmarkAddition({
      dateAdded: 0,
      guid: "dQFSYrbM5SJN",
      id: -1,
      index: 0,
      isTagging: false,
      itemType: 1,
      parentGuid: "n_HOEFys1qsL",
      parentId: -2,
      source: 0,
      title: "test-123",
      tags: "tags",
      url: "http://example.com/test-123",
      frecency: 0,
      hidden: false,
      visitCount: 0,
      lastVisitDate: 0,
      targetFolderGuid: null,
      targetFolderItemId: -1,
      targetFolderTitle: null,
    }),
    new PlacesBookmarkRemoved({
      id: -1,
      url: "http://example.com/test-123",
      title: "test-123",
      itemType: 1,
      parentId: -2,
      index: 0,
      guid: "M3WYgJlm2Jlx",
      parentGuid: "DO1f97R4KC3Y",
      source: 0,
      isTagging: false,
      isDescendantRemoval: false,
    }),
    new PlacesHistoryCleared(),
    new PlacesVisitRemoved({
      url: "http://example.com/test-123",
      pageGuid: "sPVcW2V4H7Rg",
      reason: PlacesVisitRemoved.REASON_DELETED,
      transitionType: 0,
      isRemovedFromStore: true,
      isPartialVisistsRemoval: false,
    }),
  ];

  for (let notification of notifications) {
    PlacesUtils.observers.notifyListeners([notification]);
    Assert.ok(
      feed.placesObserver.handlePlacesEvent.calledOnce,
      "PlacesFeed.handlePlacesEvent called"
    );
    Assert.ok(feed.placesObserver.handlePlacesEvent.calledWith([notification]));
    feed.placesObserver.handlePlacesEvent.resetHistory();
  }

  info(
    "PlacesFeed.onAction remove bookmark, history, places, blocked " +
      "observers, and timers on UNINIT"
  );

  let placesChangedTimerCancel = sandbox.spy();
  feed.placesChangedTimer = {
    cancel: placesChangedTimerCancel,
  };

  // Unlike INIT, UNINIT removes the observers synchronously, so no need to
  // wait for the event loop to tick around again.
  feed.onAction({ type: actionTypes.UNINIT });

  for (let notification of notifications) {
    PlacesUtils.observers.notifyListeners([notification]);
    Assert.ok(
      feed.placesObserver.handlePlacesEvent.notCalled,
      "PlacesFeed.handlePlacesEvent not called"
    );
    feed.placesObserver.handlePlacesEvent.resetHistory();
  }

  Assert.equal(feed.placesChangedTimer, null);
  Assert.ok(placesChangedTimerCancel.calledOnce);

  sandbox.restore();
});

add_task(async function test_onAction_BLOCK_URL() {
  info("PlacesFeed.onAction should block a url on BLOCK_URL");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(NewTabUtils.activityStreamLinks, "blockURL");

  feed.onAction({
    type: actionTypes.BLOCK_URL,
    data: [{ url: "apple.com", pocket_id: 1234 }],
  });
  Assert.ok(
    NewTabUtils.activityStreamLinks.blockURL.calledWith({
      url: "apple.com",
      pocket_id: 1234,
    })
  );

  sandbox.restore();
});

add_task(async function test_onAction_BLOCK_URL_topsites_sponsors() {
  info(
    "PlacesFeed.onAction BLOCK_URL should update the blocked top " +
      "sites sponsors"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "addToBlockedTopSitesSponsors");

  feed.onAction({
    type: actionTypes.BLOCK_URL,
    data: [{ url: "foo.com", pocket_id: 1234, isSponsoredTopSite: 1 }],
  });
  Assert.ok(feed.addToBlockedTopSitesSponsors.calledWith([{ url: "foo.com" }]));

  sandbox.restore();
});

add_task(async function test_onAction_BOOKMARK_URL() {
  info("PlacesFeed.onAction should bookmark a url on BOOKMARK_URL");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(NewTabUtils.activityStreamLinks, "addBookmark");

  let data = { url: "pear.com", title: "A pear" };
  let _target = { browser: { ownerGlobal() {} } };
  feed.onAction({ type: actionTypes.BOOKMARK_URL, data, _target });
  Assert.ok(
    NewTabUtils.activityStreamLinks.addBookmark.calledWith(
      data,
      _target.browser.ownerGlobal
    )
  );

  sandbox.restore();
});

add_task(async function test_onAction_DELETE_BOOKMARK_BY_ID() {
  info("PlacesFeed.onAction should delete a bookmark on DELETE_BOOKMARK_BY_ID");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(NewTabUtils.activityStreamLinks, "deleteBookmark");

  feed.onAction({ type: actionTypes.DELETE_BOOKMARK_BY_ID, data: "g123kd" });
  Assert.ok(
    NewTabUtils.activityStreamLinks.deleteBookmark.calledWith("g123kd")
  );

  sandbox.restore();
});

add_task(async function test_onAction_DELETE_HISTORY_URL() {
  info(
    "PlacesFeed.onAction should delete a history entry on DELETE_HISTORY_URL"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(NewTabUtils.activityStreamLinks, "deleteHistoryEntry");
  sandbox.stub(NewTabUtils.activityStreamLinks, "blockURL");

  feed.onAction({
    type: actionTypes.DELETE_HISTORY_URL,
    data: { url: "guava.com", forceBlock: null },
  });
  Assert.ok(
    NewTabUtils.activityStreamLinks.deleteHistoryEntry.calledWith("guava.com")
  );
  Assert.ok(NewTabUtils.activityStreamLinks.blockURL.notCalled);

  sandbox.restore();
});

add_task(async function test_onAction_DELETE_HISTORY_URL_and_block() {
  info(
    "PlacesFeed.onAction should delete a history entry on " +
      "DELETE_HISTORY_URL and force a site to be blocked if specified"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(NewTabUtils.activityStreamLinks, "deleteHistoryEntry");
  sandbox.stub(NewTabUtils.activityStreamLinks, "blockURL");

  feed.onAction({
    type: actionTypes.DELETE_HISTORY_URL,
    data: { url: "guava.com", forceBlock: "g123kd" },
  });
  Assert.ok(
    NewTabUtils.activityStreamLinks.deleteHistoryEntry.calledWith("guava.com")
  );
  Assert.ok(
    NewTabUtils.activityStreamLinks.blockURL.calledWith({
      url: "guava.com",
      pocket_id: undefined,
    })
  );

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_NEW_WINDOW() {
  info(
    "PlacesFeed.onAction should call openTrustedLinkIn with the " +
      "correct url, where and params on OPEN_NEW_WINDOW"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openWindowAction = {
    type: actionTypes.OPEN_NEW_WINDOW,
    data: { url: "https://foo.com" },
    _target: { browser: { ownerGlobal: { openTrustedLinkIn } } },
  };

  feed.onAction(openWindowAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where, params] = openTrustedLinkIn.firstCall.args;
  Assert.equal(url, "https://foo.com");
  Assert.equal(where, "window");
  Assert.ok(!params.private);
  Assert.ok(!params.forceForeground);

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_PRIVATE_WINDOW() {
  info(
    "PlacesFeed.onAction should call openTrustedLinkIn with the " +
      "correct url, where, params and privacy args on OPEN_PRIVATE_WINDOW"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openWindowAction = {
    type: actionTypes.OPEN_PRIVATE_WINDOW,
    data: { url: "https://foo.com" },
    _target: { browser: { ownerGlobal: { openTrustedLinkIn } } },
  };

  feed.onAction(openWindowAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where, params] = openTrustedLinkIn.firstCall.args;
  Assert.equal(url, "https://foo.com");
  Assert.equal(where, "window");
  Assert.ok(params.private);
  Assert.ok(!params.forceForeground);

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_LINK() {
  info(
    "PlacesFeed.onAction should call openTrustedLinkIn with the " +
      "correct url, where and params on OPEN_LINK"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openLinkAction = {
    type: actionTypes.OPEN_LINK,
    data: { url: "https://foo.com" },
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn },
      },
    },
  };
  feed.onAction(openLinkAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where, params] = openTrustedLinkIn.firstCall.args;
  Assert.equal(url, "https://foo.com");
  Assert.equal(where, "current");
  Assert.ok(!params.private);
  Assert.ok(!params.forceForeground);

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_LINK_referrer() {
  info("PlacesFeed.onAction should open link with referrer on OPEN_LINK");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openLinkAction = {
    type: actionTypes.OPEN_LINK,
    data: { url: "https://foo.com", referrer: "https://foo.com/ref" },
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn, whereToOpenLink: () => "tab" },
      },
    },
  };
  feed.onAction(openLinkAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [, , params] = openTrustedLinkIn.firstCall.args;
  Assert.equal(params.referrerInfo.referrerPolicy, 5);
  Assert.equal(
    params.referrerInfo.originalReferrer.spec,
    "https://foo.com/ref"
  );

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_LINK_typed_bonus() {
  info(
    "PlacesFeed.onAction should mark link with typed bonus as " +
      "typed before opening OPEN_LINK"
  );
  let sandbox = sinon.createSandbox();
  let callOrder = [];
  // We can't stub out PlacesUtils.history.markPageAsTyped, since that's an
  // XPCOM component. We'll stub out history instead.
  sandbox.stub(PlacesUtils, "history").get(() => {
    return {
      markPageAsTyped: sandbox.stub().callsFake(() => {
        callOrder.push("markPageAsTyped");
      }),
    };
  });

  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub().callsFake(() => {
    callOrder.push("openTrustedLinkIn");
  });
  let openLinkAction = {
    type: actionTypes.OPEN_LINK,
    data: {
      typedBonus: true,
      url: "https://foo.com",
    },
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn, whereToOpenLink: () => "tab" },
      },
    },
  };
  feed.onAction(openLinkAction);

  Assert.deepEqual(callOrder, ["markPageAsTyped", "openTrustedLinkIn"]);

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_LINK_pocket() {
  info(
    "PlacesFeed.onAction should open the pocket link if it's a " +
      "pocket story on OPEN_LINK"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openLinkAction = {
    type: actionTypes.OPEN_LINK,
    data: {
      url: "https://foo.com",
      open_url: "https://getpocket.com/foo",
      type: "pocket",
    },
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn },
      },
    },
  };

  feed.onAction(openLinkAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where, params] = openTrustedLinkIn.firstCall.args;
  Assert.equal(url, "https://getpocket.com/foo");
  Assert.equal(where, "current");
  Assert.ok(!params.private);
  Assert.ok(!params.forceForeground);

  sandbox.restore();
});

add_task(async function test_onAction_OPEN_LINK_not_http() {
  info("PlacesFeed.onAction should not open link if not http");
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openLinkAction = {
    type: actionTypes.OPEN_LINK,
    data: { url: "file:///foo.com" },
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn },
      },
    },
  };

  feed.onAction(openLinkAction);

  Assert.ok(openTrustedLinkIn.notCalled, "openTrustedLinkIn not called");

  sandbox.restore();
});

add_task(async function test_onAction_FILL_SEARCH_TERM() {
  info(
    "PlacesFeed.onAction should call fillSearchTopSiteTerm " +
      "on FILL_SEARCH_TERM"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "fillSearchTopSiteTerm");

  feed.onAction({ type: actionTypes.FILL_SEARCH_TERM });

  Assert.ok(
    feed.fillSearchTopSiteTerm.calledOnce,
    "PlacesFeed.fillSearchTopSiteTerm called"
  );
  sandbox.restore();
});

add_task(async function test_onAction_ABOUT_SPONSORED_TOP_SITES() {
  info(
    "PlacesFeed.onAction should call openTrustedLinkIn with the " +
      "correct SUMO url on ABOUT_SPONSORED_TOP_SITES"
  );
  let sandbox = sinon.createSandbox();
  let feed = getPlacesFeedForTest(sandbox);
  let openTrustedLinkIn = sandbox.stub();
  let openLinkAction = {
    type: actionTypes.ABOUT_SPONSORED_TOP_SITES,
    _target: {
      browser: {
        ownerGlobal: { openTrustedLinkIn },
      },
    },
  };

  feed.onAction(openLinkAction);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where] = openTrustedLinkIn.firstCall.args;
  Assert.ok(url.endsWith("sponsor-privacy"));
  Assert.equal(where, "tab");

  sandbox.restore();
});

add_task(async function test_onAction_FILL_SEARCH_TERM() {
  info(
    "PlacesFeed.onAction should set the URL bar value to the label value " +
      "on FILL_SEARCH_TERM"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(SearchService.prototype, "getEngineByAlias").resolves(null);

  let feed = getPlacesFeedForTest(sandbox);
  let locationBar = { search: sandbox.stub() };
  let action = {
    type: actionTypes.FILL_SEARCH_TERM,
    data: { label: "@Foo" },
    _target: { browser: { ownerGlobal: { gURLBar: locationBar } } },
  };

  await feed.onAction(action);

  Assert.ok(locationBar.search.calledOnce, "gURLBar.search called");
  Assert.ok(
    locationBar.search.calledWithExactly("@Foo", {
      searchEngine: null,
      searchModeEntry: "topsites_newtab",
    })
  );

  sandbox.restore();
});

add_task(async function test_onAction_SAVE_TO_POCKET() {
  info("PlacesFeed.onAction should call saveToPocket on SAVE_TO_POCKET");
  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "saveToPocket");

  let action = {
    type: actionTypes.SAVE_TO_POCKET,
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: { browser: {} },
  };

  await feed.onAction(action);

  Assert.ok(feed.saveToPocket.calledOnce, "PlacesFeed.saveToPocket called");
  Assert.ok(
    feed.saveToPocket.calledWithExactly(
      action.data.site,
      action._target.browser
    )
  );

  sandbox.restore();
});

add_task(async function test_onAction_SAVE_TO_POCKET_not_logged_in() {
  info(
    "PlacesFeed.onAction should openTrustedLinkIn with sendToPocket " +
      "if not logged in on SAVE_TO_POCKET"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(pktApi, "isUserLoggedIn").returns(false);
  sandbox.stub(NimbusFeatures.pocketNewtab, "getVariable").returns(true);
  sandbox.stub(ExperimentAPI, "getExperimentMetaData").returns({
    slug: "slug",
    active: true,
    branch: { slug: "branch-slug" },
  });
  Services.prefs.setStringPref(POCKET_SITE_PREF, "getpocket.com");

  let feed = getPlacesFeedForTest(sandbox);

  let openTrustedLinkIn = sandbox.stub();
  let action = {
    type: actionTypes.SAVE_TO_POCKET,
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: {
      browser: {
        ownerGlobal: {
          openTrustedLinkIn,
        },
      },
    },
  };

  await feed.onAction(action);

  Assert.ok(openTrustedLinkIn.calledOnce, "openTrustedLinkIn called");
  let [url, where] = openTrustedLinkIn.firstCall.args;
  Assert.equal(
    url,
    "https://getpocket.com/signup?utm_source=firefox_newtab_save_button&utm_campaign=slug&utm_content=branch-slug"
  );
  Assert.equal(where, "tab");

  Services.prefs.clearUserPref(POCKET_SITE_PREF);

  sandbox.restore();
});

add_task(async function test_onAction_SAVE_TO_POCKET_logged_in() {
  info(
    "PlacesFeed.onAction should call " +
      "NewTabUtils.activityStreamLinks.addPocketEntry if we are saving a " +
      "pocket story on SAVE_TO_POCKET"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(pktApi, "isUserLoggedIn").returns(true);
  sandbox.stub(NewTabUtils.activityStreamLinks, "addPocketEntry");

  let feed = getPlacesFeedForTest(sandbox);

  let openTrustedLinkIn = sandbox.stub();
  let action = {
    type: actionTypes.SAVE_TO_POCKET,
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: {
      browser: {
        ownerGlobal: {
          openTrustedLinkIn,
        },
      },
    },
  };

  await feed.onAction(action);

  Assert.ok(
    NewTabUtils.activityStreamLinks.addPocketEntry.calledOnce,
    "NewTabUtils.activityStreamLinks.addPocketEntry called"
  );
  Assert.ok(
    NewTabUtils.activityStreamLinks.addPocketEntry.calledWithExactly(
      action.data.site.url,
      action.data.site.title,
      action._target.browser
    )
  );

  sandbox.restore();
});

add_task(async function test_saveToPocket_addPocketEntry_rejects() {
  info(
    "PlacesFeed.saveToPocket should still resolve if " +
      "NewTabUtils.activityStreamLinks.addPocketEntry rejects"
  );
  let sandbox = sinon.createSandbox();
  let e = new Error("Error");

  sandbox.stub(NewTabUtils.activityStreamLinks, "addPocketEntry").rejects(e);

  let feed = getPlacesFeedForTest(sandbox);

  let openTrustedLinkIn = sandbox.stub();
  let action = {
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: {
      browser: {
        ownerGlobal: {
          openTrustedLinkIn,
        },
      },
    },
  };

  try {
    await feed.saveToPocket(action.data.site, action._target.browser);
    Assert.ok(true, "PlacesFeed.saveToPocket Promise resolved");
  } catch {
    Assert.ok(false, "PlacesFeed.saveToPocket Promise rejected");
  }

  sandbox.restore();
});

add_task(async function test_saveToPocket_broadcast_to_content() {
  info(
    "PlacesFeed.saveToPocket should broadcast to content if we " +
      "successfully added a link to Pocket"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(pktApi, "isUserLoggedIn").returns(true);

  sandbox
    .stub(NewTabUtils.activityStreamLinks, "addPocketEntry")
    .resolves({ item: { open_url: "pocket.com/itemID", item_id: 1234 } });

  let feed = getPlacesFeedForTest(sandbox);

  let openTrustedLinkIn = sandbox.stub();
  let action = {
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: {
      browser: {
        ownerGlobal: {
          openTrustedLinkIn,
        },
      },
    },
  };

  await feed.saveToPocket(action.data.site, action._target.browser);
  Assert.ok(
    feed.store.dispatch.calledOnce,
    "PlacesFeed.store.dispatch was called"
  );
  Assert.equal(
    feed.store.dispatch.firstCall.args[0].type,
    actionTypes.PLACES_SAVED_TO_POCKET
  );
  Assert.deepEqual(feed.store.dispatch.firstCall.args[0].data, {
    url: "raspberry.com",
    title: "raspberry",
    pocket_id: 1234,
    open_url: "pocket.com/itemID",
  });

  sandbox.restore();
});

add_task(async function test_saveToPocket_broadcast_only_on_data() {
  info(
    "PlacesFeed.saveToPocket should broadcast to content if we " +
      "successfully added a link to Pocket"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(pktApi, "isUserLoggedIn").returns(true);

  sandbox
    .stub(NewTabUtils.activityStreamLinks, "addPocketEntry")
    .resolves(null);

  let feed = getPlacesFeedForTest(sandbox);

  let openTrustedLinkIn = sandbox.stub();
  let action = {
    data: { site: { url: "raspberry.com", title: "raspberry" } },
    _target: {
      browser: {
        ownerGlobal: {
          openTrustedLinkIn,
        },
      },
    },
  };

  await feed.saveToPocket(action.data.site, action._target.browser);
  Assert.ok(
    feed.store.dispatch.notCalled,
    "PlacesFeed.store.dispatch was not called"
  );

  sandbox.restore();
});

add_task(async function test_onAction_DELETE_FROM_POCKET() {
  info(
    "PlacesFeed.onAction should call deleteFromPocket on DELETE_FROM_POCKET"
  );
  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "deleteFromPocket");

  feed.onAction({
    type: actionTypes.DELETE_FROM_POCKET,
    data: { pocket_id: 12345 },
  });

  Assert.ok(
    feed.deleteFromPocket.calledOnce,
    "PlacesFeed.deleteFromPocket called"
  );
  Assert.ok(feed.deleteFromPocket.calledWithExactly(12345));

  sandbox.restore();
});

add_task(async function test_deleteFromPocket_resolves() {
  info(
    "PlacesFeed.deleteFromPocket should still resolve if deletePocketEntry " +
      "rejects"
  );
  let sandbox = sinon.createSandbox();
  let e = new Error("Error");
  sandbox.stub(NewTabUtils.activityStreamLinks, "deletePocketEntry").rejects(e);

  let feed = getPlacesFeedForTest(sandbox);
  await feed.deleteFromPocket(12345);

  try {
    await feed.deleteFromPocket(12345);
    Assert.ok(true, "PlacesFeed.deleteFromPocket Promise resolved");
  } catch {
    Assert.ok(false, "PlacesFeed.deleteFromPocket Promise rejected");
  }

  sandbox.restore();
});

add_task(async function test_deleteFromPocket_calls_deletePocketEntry() {
  info(
    "PlacesFeed.deleteFromPocket should call " +
      "NewTabUtils.deletePocketEntry and dispatch " +
      "POCKET_LINK_DELETED_OR_ARCHIVED when deleting from Pocket"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.activityStreamLinks, "deletePocketEntry");

  let feed = getPlacesFeedForTest(sandbox);
  await feed.deleteFromPocket(12345);

  Assert.ok(
    NewTabUtils.activityStreamLinks.deletePocketEntry.calledOnce,
    "NewTabUtils.activityStreamLinks.deletePocketEntry called"
  );
  Assert.ok(
    NewTabUtils.activityStreamLinks.deletePocketEntry.calledWithExactly(12345)
  );
  Assert.ok(
    feed.store.dispatch.calledOnce,
    "PlacesFeed.store.dispatch was called"
  );
  Assert.ok(
    feed.store.dispatch.calledWithExactly({
      type: actionTypes.POCKET_LINK_DELETED_OR_ARCHIVED,
    })
  );

  sandbox.restore();
});

add_task(async function test_onAction_ARCHIVE_FROM_POCKET() {
  info(
    "PlacesFeed.onAction should call archiveFromPocket on ARCHIVE_FROM_POCKET"
  );
  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "archiveFromPocket");

  await feed.onAction({
    type: actionTypes.ARCHIVE_FROM_POCKET,
    data: { pocket_id: 12345 },
  });

  Assert.ok(
    feed.archiveFromPocket.calledOnce,
    "PlacesFeed.archiveFromPocket called"
  );
  Assert.ok(feed.archiveFromPocket.calledWithExactly(12345));

  sandbox.restore();
});

add_task(async function test_archiveFromPocket_resolves() {
  info(
    "PlacesFeed.archiveFromPocket should resolve if archivePocketEntry rejects"
  );
  let sandbox = sinon.createSandbox();
  let e = new Error("Error");
  sandbox
    .stub(NewTabUtils.activityStreamLinks, "archivePocketEntry")
    .rejects(e);

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "archiveFromPocket");

  try {
    await feed.archiveFromPocket(12345);
    Assert.ok(true, "PlacesFeed.archiveFromPocket Promise resolved");
  } catch {
    Assert.ok(false, "PlacesFeed.archiveFromPocket Promise rejected");
  }

  sandbox.restore();
});

add_task(async function test_archiveFromPocket_calls_archivePocketEntry() {
  info(
    "PlacesFeed.archiveFromPocket should call " +
      "NewTabUtils.archivePocketEntry and dispatch " +
      "POCKET_LINK_DELETED_OR_ARCHIVED when deleting from Pocket"
  );
  let sandbox = sinon.createSandbox();
  sandbox.stub(NewTabUtils.activityStreamLinks, "archivePocketEntry");

  let feed = getPlacesFeedForTest(sandbox);
  await feed.archiveFromPocket(12345);

  Assert.ok(
    NewTabUtils.activityStreamLinks.archivePocketEntry.calledOnce,
    "NewTabUtils.activityStreamLinks.archivePocketEntry called"
  );
  Assert.ok(
    NewTabUtils.activityStreamLinks.archivePocketEntry.calledWithExactly(12345)
  );
  Assert.ok(
    feed.store.dispatch.calledOnce,
    "PlacesFeed.store.dispatch was called"
  );
  Assert.ok(
    feed.store.dispatch.calledWithExactly({
      type: actionTypes.POCKET_LINK_DELETED_OR_ARCHIVED,
    })
  );

  sandbox.restore();
});

add_task(async function test_onAction_HANDOFF_SEARCH_TO_AWESOMEBAR() {
  info(
    "PlacesFeed.onAction should call handoffSearchToAwesomebar " +
      "on HANDOFF_SEARCH_TO_AWESOMEBAR"
  );
  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "handoffSearchToAwesomebar");

  let action = {
    type: actionTypes.HANDOFF_SEARCH_TO_AWESOMEBAR,
    data: { text: "f" },
    meta: { fromTarget: {} },
    _target: { browser: { ownerGlobal: { gURLBar: { focus: () => {} } } } },
  };

  await feed.onAction(action);

  Assert.ok(
    feed.handoffSearchToAwesomebar.calledOnce,
    "PlacesFeed.handoffSearchToAwesomebar called"
  );
  Assert.ok(feed.handoffSearchToAwesomebar.calledWithExactly(action));

  sandbox.restore();
});

add_task(async function test_onAction_PARTNER_LINK_ATTRIBUTION() {
  info(
    "PlacesFeed.onAction should call makeAttributionRequest on " +
      "PARTNER_LINK_ATTRIBUTION"
  );
  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  sandbox.stub(feed, "makeAttributionRequest");

  let data = { targetURL: "https://partnersite.com", source: "topsites" };
  feed.onAction({
    type: actionTypes.PARTNER_LINK_ATTRIBUTION,
    data,
  });

  Assert.ok(
    feed.makeAttributionRequest.calledOnce,
    "PlacesFeed.makeAttributionRequest called"
  );
  Assert.ok(feed.makeAttributionRequest.calledWithExactly(data));

  sandbox.restore();
});

add_task(
  async function test_makeAttributionRequest_PartnerLinkAttribution_makeReq() {
    info(
      "PlacesFeed.makeAttributionRequest should call " +
        "PartnerLinkAttribution.makeRequest"
    );
    let sandbox = sinon.createSandbox();

    let feed = getPlacesFeedForTest(sandbox);
    sandbox.stub(PartnerLinkAttribution, "makeRequest");

    let data = { targetURL: "https://partnersite.com", source: "topsites" };
    feed.makeAttributionRequest(data);

    Assert.ok(
      PartnerLinkAttribution.makeRequest.calledOnce,
      "PartnerLinkAttribution.makeRequest called"
    );

    sandbox.restore();
  }
);

function createFakeURLBar(sandbox) {
  let fakeURLBar = {
    focus: sandbox.spy(),
    handoff: sandbox.spy(),
    setHiddenFocus: sandbox.spy(),
    removeHiddenFocus: sandbox.spy(),
    addEventListener: (ev, cb) => {
      fakeURLBar.listeners[ev] = cb;
    },
    removeEventListener: sandbox.spy(),

    listeners: [],
  };

  return fakeURLBar;
}

add_task(async function test_handoffSearchToAwesomebar_no_text() {
  info(
    "PlacesFeed.handoffSearchToAwesomebar should properly handle handoff " +
      "with no text passed in"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  let fakeURLBar = createFakeURLBar(sandbox);

  sandbox.stub(PrivateBrowsingUtils, "isBrowserPrivate").returns(false);
  sandbox.stub(feed, "_getDefaultSearchEngine").returns(null);

  feed.handoffSearchToAwesomebar({
    _target: { browser: { ownerGlobal: { gURLBar: fakeURLBar } } },
    data: {},
    meta: { fromTarget: {} },
  });

  Assert.ok(
    fakeURLBar.setHiddenFocus.calledOnce,
    "gURLBar.setHiddenFocus called"
  );
  Assert.ok(fakeURLBar.handoff.notCalled, "gURLBar.handoff not called");
  Assert.ok(
    feed.store.dispatch.notCalled,
    "PlacesFeed.store.dispatch not called"
  );

  // Now type a character.
  fakeURLBar.listeners.keydown({ key: "f" });
  Assert.ok(fakeURLBar.handoff.calledOnce, "gURLBar.handoff called");
  Assert.ok(
    fakeURLBar.removeHiddenFocus.calledOnce,
    "gURLBar.removeHiddenFocus called"
  );
  Assert.ok(feed.store.dispatch.calledOnce, "PlacesFeed.store.dispatch called");
  Assert.ok(
    feed.store.dispatch.calledWith({
      meta: {
        from: "ActivityStream:Main",
        skipMain: true,
        to: "ActivityStream:Content",
        toTarget: {},
      },
      type: "DISABLE_SEARCH",
    }),
    "PlacesFeed.store.dispatch called"
  );

  sandbox.restore();
});

add_task(async function test_handoffSearchToAwesomebar_with_text() {
  info(
    "PlacesFeed.handoffSearchToAwesomebar should properly handle handoff " +
      "with text data passed in"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  let fakeURLBar = createFakeURLBar(sandbox);

  sandbox.stub(PrivateBrowsingUtils, "isBrowserPrivate").returns(false);
  let engine = {};
  sandbox.stub(feed, "_getDefaultSearchEngine").returns(engine);

  const SESSION_ID = "decafc0ffee";
  AboutNewTab.activityStream.store.feeds.get.returns({
    sessions: {
      get: () => {
        return { session_id: SESSION_ID };
      },
    },
  });

  feed.handoffSearchToAwesomebar({
    _target: { browser: { ownerGlobal: { gURLBar: fakeURLBar } } },
    data: { text: "foo" },
    meta: { fromTarget: {} },
  });

  Assert.ok(fakeURLBar.handoff.calledOnce, "gURLBar.handoff was called");
  Assert.ok(fakeURLBar.handoff.calledWithExactly("foo", engine, SESSION_ID));
  Assert.ok(fakeURLBar.focus.notCalled, "gURLBar.focus not called");
  Assert.ok(
    fakeURLBar.setHiddenFocus.notCalled,
    "gURLBar.setHiddenFocus not called"
  );

  // Now call blur listener.
  fakeURLBar.listeners.blur();
  Assert.ok(feed.store.dispatch.calledOnce, "PlacesFeed.store.dispatch called");
  Assert.ok(
    feed.store.dispatch.calledWith({
      meta: {
        from: "ActivityStream:Main",
        skipMain: true,
        to: "ActivityStream:Content",
        toTarget: {},
      },
      type: "SHOW_SEARCH",
    })
  );

  sandbox.restore();
});

add_task(async function test_handoffSearchToAwesomebar_with_text_pb_mode() {
  info(
    "PlacesFeed.handoffSearchToAwesomebar should properly handle handoff " +
      "with text data passed in, in private browsing mode"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  let fakeURLBar = createFakeURLBar(sandbox);

  sandbox.stub(PrivateBrowsingUtils, "isBrowserPrivate").returns(true);
  let engine = {};
  sandbox.stub(feed, "_getDefaultSearchEngine").returns(engine);

  feed.handoffSearchToAwesomebar({
    _target: { browser: { ownerGlobal: { gURLBar: fakeURLBar } } },
    data: { text: "foo" },
    meta: { fromTarget: {} },
  });
  Assert.ok(fakeURLBar.handoff.calledOnce, "gURLBar.handoff was called");
  Assert.ok(fakeURLBar.handoff.calledWithExactly("foo", engine, undefined));
  Assert.ok(fakeURLBar.focus.notCalled, "gURLBar.focus not called");
  Assert.ok(
    fakeURLBar.setHiddenFocus.notCalled,
    "gURLBar.setHiddenFocus not called"
  );

  // Now call blur listener.
  fakeURLBar.listeners.blur();
  Assert.ok(feed.store.dispatch.calledOnce, "PlacesFeed.store.dispatch called");
  Assert.ok(
    feed.store.dispatch.calledWith({
      meta: {
        from: "ActivityStream:Main",
        skipMain: true,
        to: "ActivityStream:Content",
        toTarget: {},
      },
      type: "SHOW_SEARCH",
    })
  );

  sandbox.restore();
});

add_task(async function test_handoffSearchToAwesomebar_SHOW_SEARCH_on_esc() {
  info(
    "PlacesFeed.handoffSearchToAwesomebar should SHOW_SEARCH on ESC keydown"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  let fakeURLBar = createFakeURLBar(sandbox);

  sandbox.stub(PrivateBrowsingUtils, "isBrowserPrivate").returns(false);
  let engine = {};
  sandbox.stub(feed, "_getDefaultSearchEngine").returns(engine);

  feed.handoffSearchToAwesomebar({
    _target: { browser: { ownerGlobal: { gURLBar: fakeURLBar } } },
    data: { text: "foo" },
    meta: { fromTarget: {} },
  });
  Assert.ok(fakeURLBar.handoff.calledOnce, "gURLBar.handoff was called");
  Assert.ok(fakeURLBar.handoff.calledWithExactly("foo", engine, undefined));
  Assert.ok(fakeURLBar.focus.notCalled, "gURLBar.focus not called");

  // Now call ESC keydown.
  fakeURLBar.listeners.keydown({ key: "Escape" });
  Assert.ok(feed.store.dispatch.calledOnce, "PlacesFeed.store.dispatch called");
  Assert.ok(
    feed.store.dispatch.calledWith({
      meta: {
        from: "ActivityStream:Main",
        skipMain: true,
        to: "ActivityStream:Content",
        toTarget: {},
      },
      type: "SHOW_SEARCH",
    })
  );

  sandbox.restore();
});

add_task(
  async function test_handoffSearchToAwesomebar_with_session_id_no_text() {
    info(
      "PlacesFeed.handoffSearchToAwesomebar should properly handoff a " +
        "newtab session id with no text passed in"
    );

    let sandbox = sinon.createSandbox();

    let feed = getPlacesFeedForTest(sandbox);
    let fakeURLBar = createFakeURLBar(sandbox);

    sandbox.stub(PrivateBrowsingUtils, "isBrowserPrivate").returns(false);
    let engine = {};
    sandbox.stub(feed, "_getDefaultSearchEngine").returns(engine);

    const SESSION_ID = "decafc0ffee";
    AboutNewTab.activityStream.store.feeds.get.returns({
      sessions: {
        get: () => {
          return { session_id: SESSION_ID };
        },
      },
    });

    feed.handoffSearchToAwesomebar({
      _target: { browser: { ownerGlobal: { gURLBar: fakeURLBar } } },
      data: {},
      meta: { fromTarget: {} },
    });

    Assert.ok(
      fakeURLBar.setHiddenFocus.calledOnce,
      "gURLBar.setHiddenFocus was called"
    );
    Assert.ok(fakeURLBar.handoff.notCalled, "gURLBar.handoff not called");
    Assert.ok(fakeURLBar.focus.notCalled, "gURLBar.focus not called");
    Assert.ok(
      feed.store.dispatch.notCalled,
      "PlacesFeed.store.dispatch not called"
    );

    // Now type a character.
    fakeURLBar.listeners.keydown({ key: "f" });
    Assert.ok(fakeURLBar.handoff.calledOnce, "gURLBar.handoff was called");
    Assert.ok(fakeURLBar.handoff.calledWithExactly("", engine, SESSION_ID));

    Assert.ok(
      fakeURLBar.removeHiddenFocus.calledOnce,
      "gURLBar.removeHiddenFocus was called"
    );
    Assert.ok(
      feed.store.dispatch.calledOnce,
      "PlacesFeed.store.dispatch called"
    );
    Assert.ok(
      feed.store.dispatch.calledWith({
        meta: {
          from: "ActivityStream:Main",
          skipMain: true,
          to: "ActivityStream:Content",
          toTarget: {},
        },
        type: "DISABLE_SEARCH",
      })
    );

    sandbox.restore();
  }
);

add_task(async function test_observe_dispatch_PLACES_LINK_BLOCKED() {
  info(
    "PlacesFeed.observe should dispatch a PLACES_LINK_BLOCKED action " +
      "with the url of the blocked link"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  feed.observe(null, BLOCKED_EVENT, "foo123.com");
  Assert.equal(
    feed.store.dispatch.firstCall.args[0].type,
    actionTypes.PLACES_LINK_BLOCKED
  );
  Assert.deepEqual(feed.store.dispatch.firstCall.args[0].data, {
    url: "foo123.com",
  });

  sandbox.restore();
});

add_task(async function test_observe_no_dispatch() {
  info(
    "PlacesFeed.observe should not call dispatch if the topic is something " +
      "other than BLOCKED_EVENT"
  );

  let sandbox = sinon.createSandbox();

  let feed = getPlacesFeedForTest(sandbox);
  feed.observe(null, "someotherevent");
  Assert.ok(
    feed.store.dispatch.notCalled,
    "PlacesFeed.store.dispatch not called"
  );

  sandbox.restore();
});

add_task(
  async function test_handlePlacesEvent_dispatch_one_PLACES_LINKS_CHANGED() {
    let events = [
      {
        message:
          "PlacesFeed.handlePlacesEvent should only dispatch 1 PLACES_LINKS_CHANGED action " +
          "if many bookmark-added notifications happened at once",
        dispatchCallCount: 5,
        event: {
          itemType: TYPE_BOOKMARK,
          source: SOURCES.DEFAULT,
          dateAdded: FAKE_BOOKMARK.dateAdded,
          guid: FAKE_BOOKMARK.bookmarkGuid,
          title: FAKE_BOOKMARK.bookmarkTitle,
          url: "https://www.foo.com",
          isTagging: false,
          type: "bookmark-added",
        },
      },
      {
        message:
          "PlacesFeed.handlePlacesEvent should only dispatch 1 " +
          "PLACES_LINKS_CHANGED action if many onItemRemoved notifications " +
          "happened at once",
        dispatchCallCount: 5,
        event: {
          id: null,
          parentId: null,
          index: null,
          itemType: TYPE_BOOKMARK,
          url: "foo.com",
          guid: "rTU_oiklsU7D",
          parentGuid: "2BzBQXOPFmuU",
          source: SOURCES.DEFAULT,
          type: "bookmark-removed",
        },
      },
      {
        message:
          "PlacesFeed.handlePlacesEvent should only dispatch 1 " +
          "PLACES_LINKS_CHANGED action if any page-removed notifications " +
          "happened at once",
        dispatchCallCount: 5,
        event: {
          type: "page-removed",
          url: "foo.com",
          isRemovedFromStore: true,
        },
      },
    ];

    for (let { message, dispatchCallCount, event } of events) {
      info(message);

      let sandbox = sinon.createSandbox();
      let feed = getPlacesFeedForTest(sandbox);

      await feed.placesObserver.handlePlacesEvent([event]);
      await feed.placesObserver.handlePlacesEvent([event]);
      await feed.placesObserver.handlePlacesEvent([event]);
      await feed.placesObserver.handlePlacesEvent([event]);

      Assert.ok(feed.placesChangedTimer, "PlacesFeed dispatch timer created");

      // Let's speed things up a bit.
      feed.placesChangedTimer.delay = 0;

      // Wait for the timer to go off and get cleared
      await TestUtils.waitForCondition(
        () => !feed.placesChangedTimer,
        "PlacesFeed dispatch timer cleared"
      );

      Assert.equal(
        feed.store.dispatch.callCount,
        dispatchCallCount,
        `PlacesFeed.store.dispatch was called ${dispatchCallCount} times`
      );

      Assert.ok(
        feed.store.dispatch.withArgs(
          actionCreators.OnlyToMain({ type: actionTypes.PLACES_LINKS_CHANGED })
        ).calledOnce,
        "PlacesFeed.store.dispatch called with PLACES_LINKS_CHANGED once"
      );

      sandbox.restore();
    }
  }
);

add_task(async function test_PlacesObserver_dispatches() {
  let events = [
    {
      message:
        "PlacesObserver should dispatch a PLACES_HISTORY_CLEARED action " +
        "on history-cleared",
      args: { type: "history-cleared" },
      expectedAction: { type: actionTypes.PLACES_HISTORY_CLEARED },
    },
    {
      message:
        "PlacesObserver should dispatch a PLACES_LINKS_DELETED action " +
        "with the right url",
      args: {
        type: "page-removed",
        url: "foo.com",
        isRemovedFromStore: true,
      },
      expectedAction: {
        type: actionTypes.PLACES_LINKS_DELETED,
        data: { urls: ["foo.com"] },
      },
    },
    {
      message:
        "PlacesObserver should dispatch a PLACES_BOOKMARK_ADDED action with " +
        "the bookmark data - http",
      args: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.DEFAULT,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "http://www.foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
      expectedAction: {
        type: actionTypes.PLACES_BOOKMARK_ADDED,
        data: {
          bookmarkGuid: FAKE_BOOKMARK.bookmarkGuid,
          bookmarkTitle: FAKE_BOOKMARK.bookmarkTitle,
          dateAdded: FAKE_BOOKMARK.dateAdded * 1000,
          url: "http://www.foo.com",
        },
      },
    },
    {
      message:
        "PlacesObserver should dispatch a PLACES_BOOKMARK_ADDED action with " +
        "the bookmark data - https",
      args: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.DEFAULT,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "https://www.foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
      expectedAction: {
        type: actionTypes.PLACES_BOOKMARK_ADDED,
        data: {
          bookmarkGuid: FAKE_BOOKMARK.bookmarkGuid,
          bookmarkTitle: FAKE_BOOKMARK.bookmarkTitle,
          dateAdded: FAKE_BOOKMARK.dateAdded * 1000,
          url: "https://www.foo.com",
        },
      },
    },
  ];

  for (let { message, args, expectedAction } of events) {
    info(message);
    let sandbox = sinon.createSandbox();
    let dispatch = sandbox.spy();
    let observer = new PlacesFeed.PlacesObserver(dispatch);
    await observer.handlePlacesEvent([args]);
    Assert.ok(dispatch.calledWith(expectedAction));
    sandbox.restore();
  }
});

add_task(async function test_PlacesObserver_ignores() {
  let events = [
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARK_ADDED action - " +
        "not http/https for bookmark-added",
      event: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.DEFAULT,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARK_ADDED action - " +
        "has IMPORT source for bookmark-added",
      event: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.IMPORT,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARK_ADDED " +
        "action - has RESTORE source for bookmark-added",
      event: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.RESTORE,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARK_ADDED " +
        "action - has RESTORE_ON_STARTUP source for bookmark-added",
      event: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.RESTORE_ON_STARTUP,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARK_ADDED " +
        "action - has SYNC source for bookmark-added",
      event: {
        itemType: TYPE_BOOKMARK,
        source: SOURCES.SYNC,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should ignore events that are not of " +
        "TYPE_BOOKMARK for bookmark-added",
      event: {
        itemType: "nottypebookmark",
        source: SOURCES.DEFAULT,
        dateAdded: FAKE_BOOKMARK.dateAdded,
        guid: FAKE_BOOKMARK.bookmarkGuid,
        title: FAKE_BOOKMARK.bookmarkTitle,
        url: "https://www.foo.com",
        isTagging: false,
        type: "bookmark-added",
      },
    },
    {
      message:
        "PlacesObserver should ignore events that are not of " +
        "TYPE_BOOKMARK for bookmark-removed",
      event: {
        id: null,
        parentId: null,
        index: null,
        itemType: "nottypebookmark",
        url: null,
        guid: "461Z_7daEqIh",
        parentGuid: "hkHScG3aI3hh",
        source: SOURCES.DEFAULT,
        type: "bookmark-removed",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARKS_REMOVED " +
        "action - has SYNC source for bookmark-removed",
      event: {
        id: null,
        parentId: null,
        index: null,
        itemType: TYPE_BOOKMARK,
        url: "foo.com",
        guid: "uvRE3stjoZOI",
        parentGuid: "BnsXZl8VMJjB",
        source: SOURCES.SYNC,
        type: "bookmark-removed",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARKS_REMOVED " +
        "action - has IMPORT source for bookmark-removed",
      event: {
        id: null,
        parentId: null,
        index: null,
        itemType: TYPE_BOOKMARK,
        url: "foo.com",
        guid: "VF6YwhGpHrOW",
        parentGuid: "7Vz8v9nKcSoq",
        source: SOURCES.IMPORT,
        type: "bookmark-removed",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARKS_REMOVED " +
        "action - has RESTORE source for bookmark-removed",
      event: {
        id: null,
        parentId: null,
        index: null,
        itemType: TYPE_BOOKMARK,
        url: "foo.com",
        guid: "eKozFyXJP97R",
        parentGuid: "ya8Z2FbjKnD0",
        source: SOURCES.RESTORE,
        type: "bookmark-removed",
      },
    },
    {
      message:
        "PlacesObserver should not dispatch a PLACES_BOOKMARKS_REMOVED " +
        "action - has RESTORE_ON_STARTUP source for bookmark-removed",
      event: {
        id: null,
        parentId: null,
        index: null,
        itemType: TYPE_BOOKMARK,
        url: "foo.com",
        guid: "StSGMhrYYfyD",
        parentGuid: "vL8wsCe2j_eT",
        source: SOURCES.RESTORE_ON_STARTUP,
        type: "bookmark-removed",
      },
    },
  ];

  for (let { message, event } of events) {
    info(message);
    let sandbox = sinon.createSandbox();
    let dispatch = sandbox.spy();
    let observer = new PlacesFeed.PlacesObserver(dispatch);

    await observer.handlePlacesEvent([event]);
    Assert.ok(dispatch.notCalled, "PlacesObserver.dispatch not called");
    sandbox.restore();
  }
});

add_task(async function test_PlacesObserver_bookmark_removed() {
  info(
    "PlacesObserver should dispatch a PLACES_BOOKMARKS_REMOVED " +
      "action with the right URL and bookmarkGuid for bookmark-removed"
  );
  let sandbox = sinon.createSandbox();
  let dispatch = sandbox.spy();
  let observer = new PlacesFeed.PlacesObserver(dispatch);

  await observer.handlePlacesEvent([
    {
      id: null,
      parentId: null,
      index: null,
      itemType: TYPE_BOOKMARK,
      url: "foo.com",
      guid: "Xgnxs27I9JnX",
      parentGuid: "a4k739PL55sP",
      source: SOURCES.DEFAULT,
      type: "bookmark-removed",
    },
  ]);

  Assert.ok(
    dispatch.calledWith({
      type: actionTypes.PLACES_BOOKMARKS_REMOVED,
      data: { urls: ["foo.com"] },
    })
  );
  sandbox.restore();
});
