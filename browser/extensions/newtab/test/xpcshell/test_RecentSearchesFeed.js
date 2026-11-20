/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  actionTypes: "resource://newtab/common/Actions.mjs",
  DEFAULT_FORM_HISTORY_PARAM:
    "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs",
  FormHistory: "resource://gre/modules/FormHistory.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  MAX_SEARCHES: "resource://newtab/lib/Widgets/RecentSearchesFeed.sys.mjs",
  RecentSearchesFeed:
    "resource://newtab/lib/Widgets/RecentSearchesFeed.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SearchSuggestionController:
    "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

const FORM_HISTORY_TOPIC = "satchel-storage-changed";
const ENGINE_TOPIC = "browser-search-engine-modified";
const PREF_LASTDEFAULTCHANGED =
  "browser.urlbar.recentsearches.lastDefaultChanged";

const PREF_WIDGETS_ENABLED = "widgets.enabled";
const PREF_RECENT_SEARCHES_ENABLED = "widgets.recentSearches.enabled";
const PREF_SYSTEM_RECENT_SEARCHES_ENABLED =
  "widgets.system.recentSearches.enabled";
const PREF_RECENT_SEARCHES_TAB = "widgets.recentSearches.tab";
const TRENDING_TAB = "trending";

function feedWithPrefs(values) {
  const feed = new RecentSearchesFeed();
  feed.store = {
    getState() {
      return this.state;
    },
    dispatch: sinon.spy(),
    state: { Prefs: { values } },
  };
  return feed;
}

function enabledPrefs() {
  return {
    [PREF_WIDGETS_ENABLED]: true,
    [PREF_RECENT_SEARCHES_ENABLED]: true,
    [PREF_SYSTEM_RECENT_SEARCHES_ENABLED]: true,
  };
}

function trendingTabPrefs() {
  return { ...enabledPrefs(), [PREF_RECENT_SEARCHES_TAB]: TRENDING_TAB };
}

async function switchToTrendingTab(feed) {
  feed.store.state.Prefs.values[PREF_RECENT_SEARCHES_TAB] = TRENDING_TAB;
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: PREF_RECENT_SEARCHES_TAB, value: TRENDING_TAB },
  });
}

function broadcastCall(feed) {
  return feed.store.dispatch
    .getCalls()
    .map(c => c.args[0])
    .find(a => a.type === actionTypes.WIDGETS_RECENT_SEARCHES_UPDATE);
}

// The searches and the trending searches are broadcast separately, so pick out
// the latest broadcast that carries the list under test.
function lastBroadcast(feed, key) {
  return feed.store.dispatch
    .getCalls()
    .map(c => c.args[0])
    .filter(
      a =>
        a.type === actionTypes.WIDGETS_RECENT_SEARCHES_UPDATE && key in a.data
    )
    .at(-1)?.data[key];
}

add_task(async function test_enabled_reflects_registry() {
  const feed = feedWithPrefs(enabledPrefs());
  Assert.ok(feed.enabled, "Enabled when all three prefs are on");

  const off = feedWithPrefs({
    ...enabledPrefs(),
    [PREF_RECENT_SEARCHES_ENABLED]: false,
  });
  Assert.ok(!off.enabled, "Disabled when the user pref is off");

  const noSystem = feedWithPrefs({
    ...enabledPrefs(),
    [PREF_SYSTEM_RECENT_SEARCHES_ENABLED]: false,
  });
  Assert.ok(!noSystem.enabled, "Disabled when the system pref is off");

  const noContainer = feedWithPrefs({
    ...enabledPrefs(),
    [PREF_WIDGETS_ENABLED]: false,
  });
  Assert.ok(!noContainer.enabled, "Disabled when the widgets container is off");
});

// Adds a search to form history against the default engine, as the search
// service does when the user searches, last used at `lastUsedMs`.
async function addSearch(value, lastUsedMs = Date.now()) {
  await SearchService.init();
  await FormHistory.update({
    op: "add",
    fieldname: DEFAULT_FORM_HISTORY_PARAM,
    value,
    source: SearchService.defaultEngine.name,
    // FormHistory keeps microseconds.
    timesUsed: 1,
    firstUsed: lastUsedMs * 1000,
    lastUsed: lastUsedMs * 1000,
  });
}

async function initFeed(prefs = enabledPrefs()) {
  const feed = feedWithPrefs(prefs);
  await feed.onAction({ type: actionTypes.INIT });
  return feed;
}

// INIT leaves the feed watching form history, so tests that only want the
// first broadcast shut it down again.
async function searchesFromInit(prefs = enabledPrefs()) {
  const feed = await initFeed(prefs);
  const searches = broadcastCall(feed)?.data.searches;
  await feed.onAction({ type: actionTypes.UNINIT });
  return searches;
}

// The feed queues its re-read with dispatchToMainThread, so anything dispatched
// after it runs later: awaiting this drains whatever the feed queued.
async function flushQueuedUpdate() {
  await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));
}

function updateBroadcasts(feed) {
  return feed.store.dispatch
    .getCalls()
    .map(call => call.args[0])
    .filter(
      action => action.type === actionTypes.WIDGETS_RECENT_SEARCHES_UPDATE
    );
}

function searchValues(searches) {
  return searches.map(search => search.value);
}

add_task(async function test_init_does_not_fetch_trending() {
  const sandbox = sinon.createSandbox();
  const fetch = sandbox.stub(SearchSuggestionController.prototype, "fetch");

  const feed = await initFeed();
  const call = broadcastCall(feed);
  Assert.ok(
    !fetch.called,
    "Nothing is asked of the engine while the widget is on the searches tab"
  );
  Assert.ok(
    !("trending" in call.data),
    "The searches broadcast leaves trending alone"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  sandbox.restore();
});

add_task(async function test_init_fetches_trending_on_the_trending_tab() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SearchSuggestionController, "engineOffersSuggestions")
    .returns(true);
  const fetch = sandbox
    .stub(SearchSuggestionController.prototype, "fetch")
    .resolves({ remote: [{ value: "nirvana" }] });

  const feed = await initFeed(trendingTabPrefs());

  Assert.deepEqual(
    lastBroadcast(feed, "trending"),
    ["nirvana"],
    "A tab that opens on the trending tab has its trending searches"
  );
  Assert.equal(fetch.callCount, 1, "And asked the engine once for them");

  await feed.onAction({ type: actionTypes.UNINIT });
  sandbox.restore();
});

// Bug 2063718: turning the widget on mid-session starts it just like INIT does,
// so a profile that left off on the trending tab gets its trending searches
// rather than a card with nothing in it.
add_task(async function test_enabling_the_widget_fetches_trending() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SearchSuggestionController, "engineOffersSuggestions")
    .returns(true);
  const fetch = sandbox
    .stub(SearchSuggestionController.prototype, "fetch")
    .resolves({ remote: [{ value: "nirvana" }] });

  // Off at startup, with the trending tab remembered from last time.
  const feed = feedWithPrefs({
    ...trendingTabPrefs(),
    [PREF_RECENT_SEARCHES_ENABLED]: false,
  });
  await feed.onAction({ type: actionTypes.INIT });
  Assert.ok(!fetch.called, "Asks the engine nothing while the widget is off");

  feed.store.state.Prefs.values[PREF_RECENT_SEARCHES_ENABLED] = true;
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: PREF_RECENT_SEARCHES_ENABLED, value: true },
  });

  Assert.deepEqual(
    lastBroadcast(feed, "trending"),
    ["nirvana"],
    "Turning the widget on fills the tab it opens on"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  sandbox.restore();
});

// The engine's own support is the only thing gating a fetch now.
add_task(async function test_trending_is_empty_without_an_engine_offering_it() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SearchSuggestionController, "engineOffersSuggestions")
    .returns(false);
  const fetch = sandbox.stub(SearchSuggestionController.prototype, "fetch");

  const feed = feedWithPrefs(enabledPrefs());
  await switchToTrendingTab(feed);

  Assert.deepEqual(
    broadcastCall(feed).data.trending,
    [],
    "Sends no trending searches when the engine has none to give"
  );
  Assert.ok(!fetch.called, "And does not ask the engine for any");

  sandbox.restore();
});

add_task(async function test_trending_comes_from_the_engine() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SearchSuggestionController, "engineOffersSuggestions")
    .returns(true);
  const fetch = sandbox
    .stub(SearchSuggestionController.prototype, "fetch")
    .resolves({
      remote: [{ value: "nirvana" }, { value: "frightened rabbit" }],
    });

  const feed = feedWithPrefs(enabledPrefs());
  await switchToTrendingTab(feed);

  const { trending, engineName } = broadcastCall(feed).data;
  Assert.deepEqual(
    trending,
    ["nirvana", "frightened rabbit"],
    "Passes the engine's trending suggestions through"
  );
  Assert.equal(
    engineName,
    SearchService.defaultEngine.name,
    "Names the engine alongside the trends it gave, for the credit line"
  );
  Assert.ok(
    fetch.getCall(0).args[0].fetchTrending,
    "Asks the controller for trending suggestions"
  );
  Assert.equal(
    fetch.getCall(0).args[0].searchString,
    "",
    "Fetches trending for an empty search string, as the urlbar does"
  );

  sandbox.restore();
});

add_task(async function test_init_broadcasts_no_searches() {
  const feed = feedWithPrefs(enabledPrefs());
  await feed.onAction({ type: actionTypes.INIT });

  const call = broadcastCall(feed);
  Assert.ok(call, "Broadcasts WIDGETS_RECENT_SEARCHES_UPDATE on INIT");
  Assert.deepEqual(call.data.searches, [], "No form history, no searches");
  Assert.ok(
    !("engineName" in call.data),
    "The searches broadcast leaves the engine name to the trends it credits"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
});

add_task(async function test_removing_a_search_forgets_it() {
  const now = Date.now();
  await addSearch("keep me", now - 1000);
  await addSearch("forget me", now - 2000);
  const feed = await initFeed();
  feed.store.dispatch.resetHistory();

  await feed.onAction({
    type: actionTypes.WIDGETS_RECENT_SEARCHES_REMOVE_SEARCH,
    data: { search: "forget me" },
  });

  Assert.deepEqual(
    searchValues(broadcastCall(feed).data.searches),
    ["keep me"],
    "Broadcasts the list without the removed search"
  );
  Assert.deepEqual(
    searchValues(await searchesFromInit()),
    ["keep me"],
    "And the search is gone from form history, not just from the message"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  await FormHistory.update({ op: "remove" });
});

add_task(async function test_a_new_search_reaches_open_tabs() {
  const feed = await initFeed();
  feed.store.dispatch.resetHistory();

  await addSearch("just searched");
  await TestUtils.waitForCondition(
    () => broadcastCall(feed),
    "Waiting for the new search to be broadcast"
  );
  Assert.deepEqual(
    searchValues(broadcastCall(feed).data.searches),
    ["just searched"],
    "A search performed elsewhere reaches tabs that are already open"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  await FormHistory.update({ op: "remove" });
});

add_task(async function test_switching_the_default_engine_updates_the_list() {
  await addSearch("before the switch", Date.now() - 1000);
  const feed = await initFeed();
  Assert.deepEqual(
    searchValues(broadcastCall(feed).data.searches),
    ["before the switch"],
    "Starts with the searches of the engine in use"
  );
  feed.store.dispatch.resetHistory();

  // The urlbar records the switch off the same notification, and searches from
  // before it belong to the engine the user left.
  UrlbarPrefs.set("recentsearches.lastDefaultChanged", Date.now().toString());
  Services.obs.notifyObservers(null, ENGINE_TOPIC, "engine-default");

  await TestUtils.waitForCondition(
    () => broadcastCall(feed),
    "Waiting for the switch to be broadcast"
  );
  Assert.deepEqual(
    broadcastCall(feed).data.searches,
    [],
    "Drops the searches that belong to the engine the user switched away from"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  Services.prefs.clearUserPref(PREF_LASTDEFAULTCHANGED);
  await FormHistory.update({ op: "remove" });
});

add_task(async function test_switching_the_default_engine_updates_trending() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(SearchSuggestionController, "engineOffersSuggestions")
    .returns(true);
  const fetch = sandbox
    .stub(SearchSuggestionController.prototype, "fetch")
    .resolves({ remote: [{ value: "what the old engine had" }] });

  const feed = await initFeed();
  Services.obs.notifyObservers(null, ENGINE_TOPIC, "engine-default");
  Assert.ok(
    !fetch.called,
    "Leaves the engine alone while no one has opened the Trending tab"
  );

  await switchToTrendingTab(feed);
  fetch.resolves({ remote: [{ value: "what the new engine has" }] });
  feed.store.dispatch.resetHistory();

  Services.obs.notifyObservers(null, ENGINE_TOPIC, "engine-default");
  await TestUtils.waitForCondition(
    () => lastBroadcast(feed, "trending"),
    "Waiting for the new engine's trending searches"
  );
  Assert.deepEqual(
    lastBroadcast(feed, "trending"),
    ["what the new engine has"],
    "Replaces trending with what the new engine has"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  sandbox.restore();
});

add_task(async function test_a_burst_of_changes_is_coalesced() {
  await addSearch("searched", Date.now() - 1000);
  const feed = await initFeed();
  feed.store.dispatch.resetHistory();

  for (const change of [
    "formhistory-add",
    "formhistory-update",
    "formhistory-remove",
    "formhistory-expireoldentries",
  ]) {
    feed.observe(null, FORM_HISTORY_TOPIC, change);
  }

  await TestUtils.waitForCondition(
    () => broadcastCall(feed),
    "Waiting for the burst to be broadcast"
  );
  await flushQueuedUpdate();
  await flushQueuedUpdate();

  Assert.equal(
    updateBroadcasts(feed).length,
    1,
    "Four notifications are one read and one broadcast"
  );

  await feed.onAction({ type: actionTypes.UNINIT });
  await FormHistory.update({ op: "remove" });
});

add_task(async function test_searches_carry_their_last_use() {
  const lastUsed = Date.now() - 2000;
  await addSearch("timed", lastUsed);
  const [search] = await searchesFromInit();
  Assert.equal(search.value, "timed", "Sends the search string");
  Assert.equal(
    search.lastUsed,
    lastUsed,
    "Sends when it was last used, as a ms epoch"
  );
  await FormHistory.update({ op: "remove" });
});

add_task(async function test_searches_are_newest_first() {
  const now = Date.now();
  await addSearch("oldest", now - 3000);
  await addSearch("newest", now - 1000);
  await addSearch("middle", now - 2000);

  Assert.deepEqual(
    searchValues(await searchesFromInit()),
    ["newest", "middle", "oldest"],
    "Sends the searches newest first, as the urlbar orders them"
  );

  await FormHistory.update({ op: "remove" });
});

add_task(async function test_searches_are_capped() {
  const now = Date.now();
  for (let i = 0; i < MAX_SEARCHES + 2; i++) {
    // Ascending age, so "search0" is the newest.
    await addSearch(`search${i}`, now - i * 1000);
  }

  Assert.equal(
    (await searchesFromInit()).length,
    MAX_SEARCHES,
    "Caps at the number of rows the large card holds"
  );

  await FormHistory.update({ op: "remove" });
});

add_task(async function test_expired_searches_are_left_behind() {
  const now = Date.now();
  await addSearch("newer", now - 1000);
  await addSearch("older", now - 2000);
  await addSearch(
    "expired",
    now - UrlbarPrefs.get("recentsearches.expirationMs") * 2
  );

  const stored = await FormHistory.search(["value"], {
    fieldname: DEFAULT_FORM_HISTORY_PARAM,
    source: SearchService.defaultEngine.name,
  });
  Assert.deepEqual(
    stored.map(entry => entry.value).sort(),
    ["expired", "newer", "older"],
    "Form history has all three, so only the query can drop one"
  );

  Assert.deepEqual(
    searchValues(await searchesFromInit()),
    ["newer", "older"],
    "Sends the searches inside the expiration window and no others"
  );

  await FormHistory.update({ op: "remove" });
});

// New tabs receive the store state from NewTabInit, so the feed does not
// rebroadcast on NEW_TAB_INIT.
add_task(async function test_new_tab_init_does_not_broadcast() {
  const feed = feedWithPrefs(enabledPrefs());
  await feed.onAction({ type: actionTypes.NEW_TAB_INIT });
  Assert.ok(!feed.store.dispatch.called, "Does not dispatch on NEW_TAB_INIT");
});

function observedTopics(feed) {
  // The observer service hands back the XPConnect wrapper around the feed, so
  // unwrap before comparing.
  return [FORM_HISTORY_TOPIC, ENGINE_TOPIC].filter(topic =>
    Array.from(Services.obs.enumerateObservers(topic)).some(
      observer => (observer.wrappedJSObject ?? observer) === feed
    )
  );
}

add_task(async function test_uninit_stops_listening() {
  const feed = await initFeed();
  Assert.deepEqual(
    observedTopics(feed),
    [FORM_HISTORY_TOPIC, ENGINE_TOPIC],
    "Listens to form history and the default engine while running"
  );

  await feed.onAction({ type: actionTypes.UNINIT });

  Assert.deepEqual(observedTopics(feed), [], "Drops both on UNINIT");

  feed.store.dispatch.resetHistory();
  await addSearch("after the teardown");
  await flushQueuedUpdate();
  await flushQueuedUpdate();
  Assert.deepEqual(
    updateBroadcasts(feed),
    [],
    "And a search made afterwards reaches no one"
  );

  await FormHistory.update({ op: "remove" });
});

add_task(async function test_turning_the_widget_off_stops_listening() {
  const feed = await initFeed();
  Assert.equal(
    observedTopics(feed).length,
    2,
    "Listening while the widget is on"
  );

  feed.store.state.Prefs.values[PREF_RECENT_SEARCHES_ENABLED] = false;
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: PREF_RECENT_SEARCHES_ENABLED, value: false },
  });

  Assert.deepEqual(
    observedTopics(feed),
    [],
    "Drops both when the widget is switched off"
  );

  feed.store.dispatch.resetHistory();
  await addSearch("after it was switched off");
  await flushQueuedUpdate();
  await flushQueuedUpdate();
  Assert.deepEqual(
    updateBroadcasts(feed),
    [],
    "And a search made afterwards reaches no one"
  );

  await FormHistory.update({ op: "remove" });
});

add_task(async function test_no_dispatch_when_disabled() {
  const feed = feedWithPrefs({
    ...enabledPrefs(),
    [PREF_RECENT_SEARCHES_ENABLED]: false,
  });
  await feed.onAction({ type: actionTypes.INIT });
  Assert.ok(!feed.store.dispatch.called, "Does not dispatch when disabled");
});

add_task(async function test_pref_changed_triggers_update() {
  const feed = feedWithPrefs(enabledPrefs());
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: PREF_RECENT_SEARCHES_ENABLED, value: true },
  });
  Assert.ok(broadcastCall(feed), "Broadcasts when an enablement pref changes");

  await feed.onAction({ type: actionTypes.UNINIT });
});

add_task(async function test_unrelated_pref_changed_is_ignored() {
  const feed = feedWithPrefs(enabledPrefs());
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "some.other.pref", value: true },
  });
  Assert.ok(!feed.store.dispatch.called, "Ignores unrelated pref changes");
});

add_task(async function test_trainhop_config_change_triggers_update() {
  // The system pref is off, so only the trainhop rollout enables the widget.
  // This fails if "trainhopConfig" is dropped from ENABLEMENT_PREFS.
  const feed = feedWithPrefs({
    "widgets.enabled": true,
    [PREF_RECENT_SEARCHES_ENABLED]: true,
    [PREF_SYSTEM_RECENT_SEARCHES_ENABLED]: false,
    region: "US",
    trainhopConfig: { widgets: { recentSearchesEnabled: true } },
  });
  Assert.ok(feed.enabled, "Trainhop config alone enables the feed");

  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "trainhopConfig" },
  });
  Assert.ok(broadcastCall(feed), "Broadcasts when trainhopConfig changes");

  await feed.onAction({ type: actionTypes.UNINIT });
});

add_task(async function test_region_resolves_after_init() {
  const prefs = {
    ...enabledPrefs(),
    [PREF_SYSTEM_RECENT_SEARCHES_ENABLED]: false,
    trainhopConfig: { widgets: { recentSearchesEnabled: true } },
  };
  const feed = await initFeed(prefs);
  Assert.ok(!broadcastCall(feed), "No searches before the region resolves");

  prefs.region = "FR";
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "region", value: "FR" },
  });
  Assert.ok(broadcastCall(feed), "Loads searches when the region resolves");

  await feed.onAction({ type: actionTypes.UNINIT });
});

add_task(async function test_pref_changed_while_disabled_does_not_dispatch() {
  const feed = feedWithPrefs({
    ...enabledPrefs(),
    [PREF_RECENT_SEARCHES_ENABLED]: false,
  });
  await feed.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: PREF_RECENT_SEARCHES_ENABLED, value: false },
  });
  Assert.ok(
    !feed.store.dispatch.called,
    "An enablement pref change that leaves the widget off stays quiet"
  );
});

add_task(async function test_opening_a_search_reports_the_widget_sap() {
  const sandbox = sinon.createSandbox();
  const loadSearch = sandbox.stub(SearchUIUtils, "loadSearch").resolves();

  const feed = feedWithPrefs(enabledPrefs());
  await feed.onAction({
    type: actionTypes.WIDGETS_RECENT_SEARCHES_OPEN_LINK,
    data: { search: "puffin colonies" },
    _target: { window: {} },
  });

  const [{ searchText, sapSource }] = loadSearch.getCall(0).args;
  Assert.equal(searchText, "puffin colonies", "Searches for the row picked");
  Assert.equal(
    sapSource,
    "newtab_search_widget",
    "Reports the widget's own search access point, not the newtab's"
  );
  Assert.ok(
    sapSource in BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES,
    "And that access point is one search telemetry knows"
  );

  sandbox.restore();
});
