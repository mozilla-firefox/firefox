/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RunSearch } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.smartwindow.lastSmartWindowUsageTime");
});

function stubPreviousSearch(sandbox) {
  const fetchWithHistory = sandbox.stub(Chat, "fetchWithHistory");
  fetchWithHistory.callsFake(options => {
    options.conversation._searchTheWebTurn =
      options.conversation.currentTurnIndex();
    return fetchWithHistory.wrappedMethod.call(Chat, options);
  });
}

/**
 * Test that run_search tool calls work end-to-end from the AI window.
 *
 * This catches the browsingContext vs win context mismatch (Bug 2012830):
 * ai-window.mjs must pass { browsingContext } to Chat.fetchWithHistory so that
 * RunSearch.runSearch and the post-tool sidebar handoff can find the window.
 */

// Fullpage run_search from a new AI tab: the tool navigates the tab away, then
// the post-tool handoff in Chat.sys.mjs opens the sidebar to continue streaming.
add_task(async function test_run_search_fullpage_opens_sidebar() {
  const sb = sinon.createSandbox();
  stubPreviousSearch(sb);
  const handoffSpy = sb.spy(AIWindow, "openSidebarAndContinue");
  let win;
  let gotToolResultRequest = false;
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: {
      toolCall: {
        name: "search_the_web",
        args: JSON.stringify({ query: "test search query" }),
      },
      followupChunks: ["Here are your search results."],
      onRequest(body) {
        const messages = Array.isArray(body.messages) ? body.messages : [];
        if (messages.some(m => m && m.role === "tool")) {
          gotToolResultRequest = true;
        }
      },
    },
  });

  try {
    // Stub run_search to move the full-page conversation to the sidebar and
    // navigate the browser away, like the real RunSearch does, so the original
    // browsingContext becomes stale.
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
    const browser = tab.linkedBrowser;
    await AIWindowTestUtils.preloadAndCloseSidebar(win, tab);

    await AIWindowTestUtils.dispatchSmartbarCommit(
      browser,
      "search the web for something",
      "chat"
    );

    await TestUtils.waitForCondition(
      () => runSearchStub.calledOnce,
      "run_search tool should be called"
    );

    // The sidebar opening is the observable effect of the full handoff
    // chain: pre-captured browsingContext was valid, getTabForBrowser
    // found the tab, and openSidebarAndContinue ran.
    await TestUtils.waitForCondition(
      () => AIWindowUI.isSidebarOpen(win),
      "Sidebar should open after fullpage run_search handoff"
    );

    await TestUtils.waitForCondition(
      () => gotToolResultRequest,
      "Server should receive follow-up request with tool results"
    );

    Assert.ok(handoffSpy.calledOnce, "The handoff continues in the sidebar");
    await AIWindowTestUtils.assertSidebarSmartbarFocused(
      win,
      "Full-page search handoff"
    );
  } finally {
    sb.restore();
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await restore();
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

// When the AI window is already in sidebar mode, run_search must receive the
// adjacent tab's browsingContext (not the sidebar's own). Without the fix in
// ai-window.mjs, the sidebar's browsingContext.embedderElement is the sidebar
// <browser> (#ai-window-browser), causing gBrowser.getTabForBrowser() to
// return null and aborting the search handoff.
add_task(
  async function test_run_search_from_sidebar_uses_tab_browsingContext() {
    const sb = sinon.createSandbox();
    stubPreviousSearch(sb);
    const handoffSpy = sb.spy(AIWindow, "openSidebarAndContinue");
    let win;
    // Track whether the server receives the follow-up request with tool
    // results, which proves openSidebarAndContinue -> reloadAndContinue
    // -> #fetchAIResponse() fired successfully.
    let gotToolResultRequest = false;
    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: {
        toolCall: {
          name: "search_the_web",
          args: JSON.stringify({ query: "sidebar search query" }),
        },
        followupChunks: ["Sidebar search results."],
        onRequest(body) {
          const messages = Array.isArray(body.messages) ? body.messages : [];
          if (messages.some(m => m && m.role === "tool")) {
            gotToolResultRequest = true;
          }
        },
      },
    });

    try {
      const runSearchStub = sb
        .stub(RunSearch, "runSearch")
        .callsFake(async (_params, browsingContext) => {
          const ctxBrowser = browsingContext.embedderElement;
          Assert.notEqual(
            ctxBrowser.id,
            "ai-window-browser",
            "browsingContext should be a tab browser, not the sidebar browser"
          );
          Assert.ok(
            ctxBrowser.documentGlobal.gBrowser.getTabForBrowser(ctxBrowser),
            "browsingContext embedderElement should belong to a tab"
          );
          BrowserTestUtils.startLoadingURIString(
            ctxBrowser,
            "https://example.com/search_results"
          );
          await BrowserTestUtils.browserLoaded(ctxBrowser);
          return "Mock search results";
        });

      win = await openAIWindow();

      // Open a content tab so there's a real tab adjacent to the sidebar.
      const tab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.com/"
      );

      AIWindowUI.openSidebar(win);
      Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");

      const sidebarBrowser = win.document.getElementById("ai-window-browser");
      await TestUtils.waitForCondition(() => {
        const el = sidebarBrowser.contentDocument?.querySelector("ai-window");
        return el?.shadowRoot;
      }, "Wait for sidebar ai-window element and shadow root");

      await AIWindowTestUtils.dispatchSmartbarCommit(
        sidebarBrowser,
        "search the web from sidebar",
        "chat"
      );

      await TestUtils.waitForCondition(
        () => runSearchStub.calledOnce,
        "run_search tool should be called from sidebar"
      );

      // The follow-up request proves the full chain completed:
      // openSidebarAndContinue called reloadAndContinue which triggered
      // a new fetchAIResponse with the tool result.
      await TestUtils.waitForCondition(
        () => gotToolResultRequest,
        "Server should receive follow-up request with tool results"
      );

      Assert.ok(handoffSpy.calledOnce, "The handoff continues in the sidebar");
      Assert.ok(
        AIWindowUI.isSidebarOpen(win),
        "Sidebar should remain open after sidebar run_search"
      );

      await BrowserTestUtils.removeTab(tab);
    } finally {
      sb.restore();
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      await restore();
    }
  }
);
