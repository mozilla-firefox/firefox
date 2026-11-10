/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  EnrollmentType: "resource://nimbus/ExperimentAPI.sys.mjs",
});

const { UrlbarProviderSemanticHistorySearch } = ChromeUtils.importESModule(
  "moz-src:///browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs"
);
const { UrlbarTelemetryUtils } = ChromeUtils.importESModule(
  "chrome://browser/content/urlbar/UrlbarTelemetryUtils.mjs"
);
const { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
);
ChromeUtils.defineLazyGetter(this, "QuickSuggestTestUtils", () => {
  const { QuickSuggestTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/QuickSuggestTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

let semanticManager = getPlacesSemanticHistoryManager();
let hasSufficientEntriesStub = sinon
  .stub(semanticManager, "hasSufficientEntriesForSearching")
  .resolves(true);

add_task(async function setup() {
  registerCleanupFunction(() => {
    sinon.restore();
  });

  // stub getEnrollmentMetadata once, then configure for both cases:
  const getEnrollmentStub = sinon.stub(
    lazy.NimbusFeatures.urlbar,
    "getEnrollmentMetadata"
  );
  getEnrollmentStub
    .withArgs(lazy.EnrollmentType.EXPERIMENT)
    .returns({ slug: "test-slug", branch: "control" });
  getEnrollmentStub.withArgs(lazy.EnrollmentType.ROLLOUT).returns(null);
  sinon.stub(lazy.NimbusFeatures.urlbar, "recordExposureEvent");

  // Set required prefs
  Services.prefs.setBoolPref("browser.ml.enable", true);
  Services.prefs.setBoolPref("places.semanticHistory.featureGate", true);
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", true);
  Services.prefs
    .getDefaultBranch("")
    .setIntPref("browser.urlbar.suggest.semanticHistory.minLength", 5);

  let cleanup = await QuickSuggestTestUtils.setRegionAndLocale({
    region: "US",
    locale: "en-US",
  });
  registerCleanupFunction(cleanup);
});

add_task(async function test_startQuery_adds_results() {
  const provider = new UrlbarProviderSemanticHistorySearch();

  const queryContext = {
    searchString: "test page",
    restrictInSearchMode: () => false,
  };

  // Trigger isActive() to initialize the semantic manager
  Assert.ok(await provider.isActive(queryContext), "Provider should be active");

  // Stub and simulate inference
  sinon.stub(semanticManager.embedder, "ensureEngine").callsFake(() => {});
  let url = "https://example.com";
  let inferStub = sinon.stub(semanticManager, "infer").resolves({
    results: [
      {
        id: 1,
        title: "Test Page",
        url,
        frecency: 100,
      },
    ],
  });
  await PlacesTestUtils.addVisits(url);

  let added = [];
  await provider.startQuery(queryContext, (_provider, result) => {
    added.push(result);
  });

  Assert.equal(added.length, 1, "One result should be added");
  Assert.equal(added[0].payload.url, url, "Correct URL should be used");
  Assert.equal(
    added[0].payload.icon,
    UrlbarShared.getIconForUrl(url),
    "Correct icon should be used"
  );
  Assert.ok(added[0].payload.isBlockable, "Result should be blockable");
  Assert.equal(added[0].payload.frecency, 100, "Frecency is returned");

  let parentController = UrlbarTestUtils.mockChildController().parentController;
  let stub = sinon.stub(parentController, "removeResult");
  let promiseRemoved = PlacesTestUtils.waitForNotification("page-removed");
  await provider.onEngagement(queryContext, parentController, {
    selType: "dismiss",
    result: { payload: { url } },
  });
  Assert.ok(stub.calledOnce, "Result should be removed on dismissal");
  await promiseRemoved;
  let visited = await PlacesUtils.history.hasVisits(url);
  Assert.ok(!visited, "URL should have been removed from history");
  inferStub.restore();
});

add_task(async function test_isActive_conditions() {
  const provider = new UrlbarProviderSemanticHistorySearch();

  // Stub canUseSemanticSearch to control the return value
  const canUseStub = sinon.stub(semanticManager, "canUseSemanticSearch");

  const shortQuery = { searchString: "hi" };
  const validQuery = {
    searchString: "hello world",
    restrictInSearchMode: () => false,
  };

  // Pref is disabled
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", false);
  Assert.ok(
    !(await provider.isActive(validQuery)),
    "Should be inactive when pref is disabled"
  );

  // Pref enabled, but string too short
  Services.prefs.setBoolPref("browser.urlbar.suggest.history", true);
  Assert.ok(
    !(await provider.isActive(shortQuery)),
    "Should be inactive for short search strings"
  );

  // All conditions met but semanticManager rejects
  canUseStub.get(() => false);
  hasSufficientEntriesStub.resetHistory();
  Assert.ok(
    !(await provider.isActive(validQuery)),
    "Should be inactive if canUseSemanticSearch returns false"
  );
  Assert.ok(
    hasSufficientEntriesStub.notCalled,
    "hasSufficientEntriesForSearching should not have been called"
  );

  // All conditions met
  canUseStub.get(() => true);
  Assert.ok(
    await provider.isActive(validQuery),
    "Should be active when all conditions are met"
  );

  const engineSearchMode = createContext("hello world", {
    searchMode: { engineName: "testEngine" },
  });
  Assert.equal(
    await provider.isActive(engineSearchMode),
    UrlbarPrefs.get("unifiedSearchButton.historyInSearchMode"),
    "Should only be active in search engine mode with historyInSearchMode"
  );

  const historySearchMode = createContext("hello world", {
    searchMode: { source: UrlbarShared.RESULT_SOURCE.HISTORY },
  });
  Assert.ok(
    await provider.isActive(historySearchMode),
    "Should be active when in history search mode"
  );
});

add_task(async function test_isActive_smartbar_uses_sw_gate() {
  const provider = new UrlbarProviderSemanticHistorySearch();

  // Reset any leftover stubs from earlier tests so we control both gates.
  if (semanticManager.canUseSemanticSearch.restore) {
    semanticManager.canUseSemanticSearch.restore();
  }

  Services.prefs.setBoolPref("browser.urlbar.suggest.history", true);

  const canUseStub = sinon.stub(semanticManager, "canUseSemanticSearch");
  const swStub = sinon.stub(semanticManager, "isEnabledForSmartWindow");

  const smartbarQuery = {
    searchString: "hello world",
    sapName: "smartbar",
    restrictInSearchMode: () => false,
  };
  const urlbarQuery = {
    searchString: "hello world",
    sapName: "urlbar",
    restrictInSearchMode: () => false,
  };

  canUseStub.get(() => false);
  swStub.get(() => true);
  Assert.ok(
    await provider.isActive(smartbarQuery),
    "Smartbar active when SW gate on (CW gate off)"
  );
  Assert.ok(
    !(await provider.isActive(urlbarQuery)),
    "Urlbar inactive when CW gate off (SW gate on)"
  );

  canUseStub.get(() => true);
  swStub.get(() => false);
  Assert.ok(
    !(await provider.isActive(smartbarQuery)),
    "Smartbar inactive when SW gate off (CW gate on)"
  );
  Assert.ok(
    await provider.isActive(urlbarQuery),
    "Urlbar active when CW gate on (SW gate off)"
  );

  canUseStub.restore();
  swStub.restore();
});

add_task(async function test_switchTab() {
  const userContextId1 = 2;
  const userContextId2 = 3;
  const privateContextId = -1;
  const url1 = "http://foo.mozilla.org/";
  const url2 = "http://foo2.mozilla.org/";
  await UrlbarProviderOpenTabs.registerOpenTab(
    url1,
    userContextId1,
    null,
    false
  );
  await UrlbarProviderOpenTabs.registerOpenTab(
    url2,
    userContextId1,
    null,
    false
  );
  await UrlbarProviderOpenTabs.registerOpenTab(
    url1,
    userContextId2,
    null,
    false
  );
  await UrlbarProviderOpenTabs.registerOpenTab(
    url1,
    privateContextId,
    null,
    false
  );
  await PlacesTestUtils.addVisits([url1, url2]);
  const provider = new UrlbarProviderSemanticHistorySearch();

  // Trigger isActive() to initialize the semantic manager
  const queryContext = createContext("firefox", { isPrivate: false });
  Assert.ok(await provider.isActive(queryContext), "Provider should be active");
  let inferStub = sinon.stub(semanticManager, "infer").resolves({
    results: [
      {
        id: 1,
        title: "Test Page 1",
        url: url1,
        distance: 0.1,
      },
      {
        id: 2,
        title: "Test Page 2",
        url: url2,
        distance: 0.2,
      },
    ],
  });
  const expectedDistances = { [url1]: 10, [url2]: 20 };

  function AssertSwitchToTabResult(result, url, userContextId, groupId = null) {
    Assert.equal(
      result.type,
      UrlbarShared.RESULT_TYPE.TAB_SWITCH,
      "Check result type"
    );
    Assert.equal(result.payload.url, url, "Check result URL");
    Assert.equal(
      result.payload.userContext.id,
      userContextId,
      "Check user context"
    );
    Assert.equal(result.payload.tabGroup, groupId, "Check tab group");
    Assert.equal(
      result.payload.icon,
      UrlbarShared.getIconForUrl(url),
      "Check icon"
    );
    Assert.strictEqual(
      result.payload.semanticDistance,
      expectedDistances[url],
      "Check semantic distance"
    );
  }
  function isUrlResult(result, url) {
    return (
      result.type === UrlbarShared.RESULT_TYPE.URL && result.payload.url === url
    );
  }

  let added = [];
  await provider.startQuery(queryContext, (_provider, result) => {
    added.push(result);
  });
  Assert.equal(added.length, 3, "Threee result should be added");
  AssertSwitchToTabResult(added[0], url1, userContextId1);
  AssertSwitchToTabResult(added[1], url1, userContextId2);
  AssertSwitchToTabResult(added[2], url2, userContextId1);

  info("Test private browsing context.");
  const privateContext = createContext("firefox");
  added.length = 0;
  await provider.startQuery(privateContext, (_provider, result) => {
    added.push(result);
  });
  Assert.equal(added.length, 2, "Two results should be added");
  AssertSwitchToTabResult(added[0], url1, privateContextId);
  Assert.ok(isUrlResult(added[1], url2), "Second result should be URL");

  info("Test tab groups and current page.");
  let tabGroudId1 = "group1";
  let tabGroudId2 = "group2";
  await UrlbarProviderOpenTabs.registerOpenTab(
    url1,
    userContextId1,
    tabGroudId1,
    false
  );
  await UrlbarProviderOpenTabs.registerOpenTab(
    url2,
    userContextId2,
    tabGroudId2,
    false
  );
  const groupContext = createContext("firefox", {
    isPrivate: false,
    currentPage: url1,
    userContextId: userContextId1,
    tabGroup: tabGroudId1,
  });

  added.length = 0;
  await provider.startQuery(groupContext, (_provider, result) => {
    added.push(result);
  });
  Assert.equal(added.length, 4, "Three results should be added");
  AssertSwitchToTabResult(added[0], url1, userContextId1);
  AssertSwitchToTabResult(added[1], url1, userContextId2);
  AssertSwitchToTabResult(added[2], url2, userContextId1);
  AssertSwitchToTabResult(added[3], url2, userContextId2, tabGroudId2);

  inferStub.restore();
});

add_task(async function test_getDomainScope() {
  const tests = [
    ["github.com/firefox", { host: "github.com", searchText: "firefox" }],
    [
      "github.com firefox bugs",
      { host: "github.com", searchText: "firefox bugs" },
    ],
    ["cnn.com DUI", null],
    ["cnn.com/DUI", null],
    ["cnn.com news", { host: "cnn.com", searchText: "news" }],
    ["cnn.com/news", { host: "cnn.com", searchText: "news" }],
    ["cnn.com", null],
    ["http://cnn.com", null],
    ["cnn.com/", null],
    ["https://cnn.com/", null],
    ["cnn.com ab", null],
    ["cnn.com - !", null],
    [
      "http://www.github.com/mozilla/semantic-history_bugs",
      { host: "github.com", searchText: "mozilla semantic history bugs" },
    ],
    [
      "github.com/mozilla/firefox/issues",
      { host: "github.com", searchText: "mozilla firefox issues" },
    ],
    [
      "github.com/mozilla/firefox bugs",
      { host: "github.com", searchText: "mozilla firefox bugs" },
    ],
    // Short segments count together towards the minimum length.
    ["cnn.com/us/DUI", { host: "cnn.com", searchText: "us DUI" }],
    ["cnn.com/a/b", null],
    ["cnn.com/a//b/", null],
    [
      "gist.github.com/some%20topic here",
      { host: "gist.github.com", searchText: "some topic here" },
    ],
    ["^ github.com firefox", { host: "github.com", searchText: "firefox" }],
    ["github.com. firefox", { host: "github.com", searchText: "firefox" }],
    ["github.com", null],
    ["https://github.com", null],
    ["firefox bugs", null],
    ["foo/bar", null],
    ["node.js tutorial", null],
    ["not a url.com/foo", null],
    ["user@github.com/foo", null],
    ["ftp://github.com/foo", null],
    ["192.168.1.1/foo", null],
    ["192.168.1.1 foo", null],
    ["[::1]/foo", null],
    ["[2001:db8::1] foo", null],
  ];

  UrlbarPrefs.set("suggest.semanticHistory.domainScopedSearch", true);
  for (let [searchString, expected] of tests) {
    Assert.deepEqual(
      UrlbarProviderSemanticHistorySearch.getDomainScope(
        createContext(searchString)
      ),
      expected,
      `Domain scope for "${searchString}"`
    );
  }
  UrlbarPrefs.clear("suggest.semanticHistory.domainScopedSearch");

  Assert.equal(
    UrlbarProviderSemanticHistorySearch.getDomainScope(
      createContext("github.com/firefox")
    ),
    null,
    "No domain scope when the pref is disabled"
  );
});

add_task(async function test_startQuery_domain_scoped() {
  const provider = new UrlbarProviderSemanticHistorySearch();
  let scopedUrl = "https://github.com/scoped";
  let unscopedUrl = "https://example.com/unscoped";
  let inferStub = sinon.stub(semanticManager, "infer").resolves({
    results: [{ id: 1, title: "Unscoped", url: unscopedUrl, frecency: 100 }],
  });
  let inferWithDomainStub = sinon
    .stub(semanticManager, "inferWithDomain")
    .resolves({
      results: [{ id: 2, title: "Scoped", url: scopedUrl, frecency: 100 }],
    });

  async function startQuery(searchString) {
    let added = [];
    await provider.startQuery(createContext(searchString), (_p, result) =>
      added.push(result)
    );
    return added.map(r => r.payload.url);
  }

  UrlbarPrefs.set("suggest.semanticHistory.domainScopedSearch", true);

  Assert.deepEqual(await startQuery("github.com firefox bugs"), [scopedUrl]);
  Assert.ok(
    inferWithDomainStub.calledOnceWith("firefox bugs", "github.com"),
    "inferWithDomain should be called with the search text and host"
  );
  Assert.ok(inferStub.notCalled, "infer should not be called");

  info("Fall back to infer when the domain has no indexed pages.");
  inferWithDomainStub.resolves(null);
  Assert.deepEqual(await startQuery("main.py error"), [unscopedUrl]);
  Assert.ok(inferStub.calledOnce, "infer should be called as a fallback");

  UrlbarPrefs.clear("suggest.semanticHistory.domainScopedSearch");
  inferStub.restore();
  inferWithDomainStub.restore();
});

add_task(async function test_semantic_distance_telemetry() {
  const provider = new UrlbarProviderSemanticHistorySearch();
  const queryContext = createContext("distance test", { isPrivate: false });
  let url = "https://example.com/distance";
  let distance = 0.3;
  let inferStub = sinon.stub(semanticManager, "infer").callsFake(async () => ({
    results: [{ id: 1, title: "Distance", url, frecency: 100, distance }],
  }));

  async function query() {
    let added = [];
    await provider.startQuery(queryContext, (_provider, result) => {
      added.push(result);
    });
    Assert.equal(added.length, 1, "One result should be added");
    return added[0];
  }

  let result = await query();
  Assert.strictEqual(
    result.payload.semanticDistance,
    30,
    "Distance is scaled to an integer"
  );

  distance = 1.5;
  result = await query();
  Assert.strictEqual(
    result.payload.semanticDistance,
    100,
    "Distance is clamped to 100"
  );

  distance = NaN;
  result = await query();
  Assert.strictEqual(
    result.payload.semanticDistance,
    undefined,
    "No distance when it is NaN"
  );

  distance = 0.3;
  result = await query();

  let other = new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.URL,
    source: UrlbarShared.RESULT_SOURCE.HISTORY,
    payload: { url: "https://example.com/other", title: "Other" },
  });
  let buildEvent = visibleResults =>
    UrlbarTelemetryUtils.buildEventInfo({
      method: "abandonment",
      action: "blur",
      interaction: "typed",
      numChars: 13,
      numWords: 2,
      searchSource: "urlbar",
      searchMode: null,
      visibleResults,
      viewIsOpen: true,
      windowMode: "classic",
    }).eventInfo;

  Assert.equal(
    buildEvent([other, result, other]).semantic_distances,
    `,${result.payload.semanticDistance},`,
    "Distances are aligned with the results"
  );
  Assert.ok(
    !("semantic_distances" in buildEvent([other])),
    "No semantic_distances without semantic results"
  );

  let inputFieldEvent = UrlbarTelemetryUtils.buildEventInfo({
    method: "engagement",
    action: "enter",
    interaction: "typed",
    numChars: 13,
    numWords: 2,
    searchSource: "urlbar",
    searchMode: null,
    selIndex: -1,
    visibleResults: [other, result],
    viewIsOpen: false,
    windowMode: "classic",
  }).eventInfo;
  Assert.equal(
    inputFieldEvent.selected_result,
    "input_field",
    "The input field was confirmed"
  );
  Assert.ok(
    !("semantic_distances" in inputFieldEvent),
    "No semantic_distances when confirming the input field with a closed view"
  );

  inferStub.restore();
});
