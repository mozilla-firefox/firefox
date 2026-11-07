/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {
  RunSearch,
  SEARCH_QUERY_ENDPOINT_PREF,
  SEARCH_QUERY_APIKEY_PREF,
  SEARCH_THE_WEB_FAST_PREF,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const { MockEngineManager, MockSearchManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.smartwindow.lastSmartWindowUsageTime");
});

/**
 * Configures search requests for the mocked provider.
 *
 * @returns {Promise<void>}
 */
add_setup(async function setup_search_provider() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_QUERY_ENDPOINT_PREF, "https://search.example.test/v1/search"],
      [SEARCH_QUERY_APIKEY_PREF, "mock-search-api-key"],
      [SEARCH_THE_WEB_FAST_PREF, true],
    ],
  });
});

/**
 * Test that run_search tool calls work end-to-end from the AI window.
 *
 * This catches the browsingContext vs win context mismatch (Bug 2012830):
 * ai-window.mjs must pass { browsingContext } to Chat.fetchWithHistory so that
 * RunSearch.runSearch and the post-tool sidebar handoff can find the window.
 */

/**
 * This test case ensures that a full-page search preserves the conversation
 * and renders its response in the sidebar with a generated title.
 */
add_task(async function test_run_search_fullpage_opens_sidebar() {
  const sb = sinon.createSandbox();
  const handoffSpy = sb.spy(AIWindow, "openSidebarAndContinue");
  const searchManager = new MockSearchManager();
  const engineManager = new MockEngineManager();
  let win;

  try {
    const runSearchStub = sb
      .stub(RunSearch, "runSearch")
      .callsFake((_params, browsingContext) =>
        AIWindowTestUtils.runSearchWithSidebarHandoff(browsingContext)
      );

    win = await openAIWindow();
    const tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    await AIWindowTestUtils.preloadAndCloseSidebar(win, tab);

    info("Submit a full-page chat request and hand it off to the sidebar");
    await AIWindowTestUtils.dispatchSmartbarCommit(
      tab.linkedBrowser,
      "search the web for something",
      "chat"
    );
    const continuation = await AIWindowTestUtils.captureSearchHandoff(
      engineManager,
      searchManager,
      "test search query"
    );
    Assert.ok(runSearchStub.calledOnce, "The search tool runs once");
    Assert.ok(handoffSpy.calledOnce, "The handoff continues in the sidebar");
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Search opens the sidebar");

    info("Complete the sidebar response and background title generation");
    const sidebarBrowser = win.document.getElementById("ai-window-browser");
    await AIWindowTestUtils.completeSearchHandoff(
      engineManager,
      sidebarBrowser,
      continuation,
      "Here are your search results."
    );
    await AIWindowTestUtils.assertSidebarSmartbarFocused(
      win,
      "Full-page search handoff"
    );
  } finally {
    await AIWindowTestUtils.cleanupSearchHandoff({
      win,
      engineManager,
      searchManager,
      sandbox: sb,
    });
  }
});

// Bug 2037648: when the assistant kicks off a search, the load must not steal
// keyboard focus into the loaded search results.
add_task(async function test_run_search_skips_search_load_focus() {
  const sb = sinon.createSandbox();
  let win;
  try {
    const loadSearchStub = sb.stub(SearchUIUtils, "loadSearch").resolves();

    win = await openAIWindow();
    await AIWindow.performSearch("flights SFO to Boston", win);

    Assert.ok(
      loadSearchStub.calledOnce,
      "SearchUIUtils.loadSearch should be invoked once"
    );
    const args = loadSearchStub.firstCall.args[0];
    Assert.strictEqual(
      args.avoidBrowserFocus,
      true,
      "Search hand-off must skip focusing the loaded search results browser"
    );
    Assert.equal(args.where, "current", "Should load in the current tab");
  } finally {
    sb.restore();
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
  }
});

/**
 * This test case ensures that a sidebar search navigates the adjacent tab
 * and renders the completed response in the sidebar.
 */
add_task(
  async function test_run_search_from_sidebar_uses_tab_browsingContext() {
    const sb = sinon.createSandbox();
    const handoffSpy = sb.spy(AIWindow, "openSidebarAndContinue");
    const searchManager = new MockSearchManager();
    const engineManager = new MockEngineManager();
    let win;
    let expectedBrowsingContext;

    try {
      const runSearchStub = sb
        .stub(RunSearch, "runSearch")
        .callsFake(async (_params, browsingContext) => {
          Assert.strictEqual(
            browsingContext,
            expectedBrowsingContext,
            "The tool receives the adjacent tab's browsing context"
          );
          const ctxBrowser = browsingContext.embedderElement;
          const loaded = BrowserTestUtils.browserLoaded(ctxBrowser);
          BrowserTestUtils.startLoadingURIString(
            ctxBrowser,
            "https://example.com/search_results"
          );
          await loaded;

          return "Mock search results";
        });

      ({ win } = await openAIWindowWithSidebar());
      expectedBrowsingContext = win.gBrowser.selectedBrowser.browsingContext;
      const sidebarBrowser = win.document.getElementById("ai-window-browser");

      info("Submit a sidebar chat request that searches in the adjacent tab");
      await AIWindowTestUtils.dispatchSmartbarCommit(
        sidebarBrowser,
        "search the web from sidebar",
        "chat"
      );
      const continuation = await AIWindowTestUtils.captureSearchHandoff(
        engineManager,
        searchManager,
        "sidebar search query"
      );
      Assert.ok(runSearchStub.calledOnce, "The search tool runs once");
      Assert.ok(handoffSpy.calledOnce, "The handoff continues in the sidebar");

      info("Complete the sidebar response and background title generation");
      await AIWindowTestUtils.completeSearchHandoff(
        engineManager,
        sidebarBrowser,
        continuation,
        "Sidebar search results."
      );
      Assert.ok(AIWindowUI.isSidebarOpen(win), "The sidebar remains open");
    } finally {
      await AIWindowTestUtils.cleanupSearchHandoff({
        win,
        engineManager,
        searchManager,
        sandbox: sb,
      });
    }
  }
);
