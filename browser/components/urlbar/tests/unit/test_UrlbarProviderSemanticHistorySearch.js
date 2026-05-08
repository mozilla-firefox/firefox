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

  const queryContext = { searchString: "test page" };

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
    UrlbarUtils.getIconForUrl(url),
    "Correct icon should be used"
  );
  Assert.ok(added[0].payload.isBlockable, "Result should be blockable");
  Assert.equal(added[0].payload.frecency, 100, "Frecency is returned");

  let controller = UrlbarTestUtils.newMockController();
  let stub = sinon.stub(controller, "removeResult");
  let promiseRemoved = PlacesTestUtils.waitForNotification("page-removed");
  await provider.onEngagement(queryContext, controller, {
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
  const validQuery = { searchString: "hello world" };

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
  Assert.ok(
    !(await provider.isActive(engineSearchMode)),
    "Should not be active when in search engine mode"
  );

  const historySearchMode = createContext("hello world", {
    searchMode: { source: UrlbarUtils.RESULT_SOURCE.HISTORY },
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

  const smartbarQuery = { searchString: "hello world", sapName: "smartbar" };
  const urlbarQuery = { searchString: "hello world", sapName: "urlbar" };

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
      },
      {
        id: 2,
        title: "Test Page 2",
        url: url2,
      },
    ],
  });

  function AssertSwitchToTabResult(result, url, userContextId, groupId = null) {
    Assert.equal(
      result.type,
      UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
      "Check result type"
    );
    Assert.equal(result.payload.url, url, "Check result URL");
    Assert.equal(
      result.payload.userContextId,
      userContextId,
      "Check user context"
    );
    Assert.equal(result.payload.tabGroup, groupId, "Check tab group");
    Assert.equal(
      result.payload.icon,
      UrlbarUtils.getIconForUrl(url),
      "Check icon"
    );
  }
  function isUrlResult(result, url) {
    return (
      result.type === UrlbarUtils.RESULT_TYPE.URL && result.payload.url === url
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
