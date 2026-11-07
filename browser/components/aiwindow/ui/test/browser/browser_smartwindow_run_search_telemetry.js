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
 * This test case ensures that a turn whose first text appears after a search
 * handoff reaches the renderer, with the missing first-character telemetry
 * documented as an expected failure.
 */
add_task(async function test_first_character_telemetry_after_search_handoff() {
  const sb = sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();
  const mockSearchManager = new MockSearchManager();
  let win;
  let firstCharacterDisplayed;

  await SpecialPowers.pushPrefEnv({
    set: [
      [SEARCH_QUERY_ENDPOINT_PREF, "https://search.example.test/v1/search"],
      [SEARCH_QUERY_APIKEY_PREF, "mock-search-api-key"],
      [SEARCH_THE_WEB_FAST_PREF, true],
    ],
  });

  try {
    // Keep search navigation local while exercising the real sidebar handoff.
    const runSearchStub = sb
      .stub(RunSearch, "runSearch")
      .callsFake(async (_params, browsingContext) => {
        const results =
          await AIWindowTestUtils.runSearchWithSidebarHandoff(browsingContext);
        const ownerWindow = browsingContext.topChromeWindow;
        const sidebarBrowser =
          ownerWindow.document.getElementById("ai-window-browser");
        firstCharacterDisplayed = sb.spy(
          sidebarBrowser.contentDocument.querySelector("ai-window"),
          "onFirstCharacterDisplayed"
        );

        return results;
      });

    win = await openAIWindow();
    const tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      AIWINDOW_URL
    );
    await AIWindowTestUtils.preloadAndCloseSidebar(win, tab);
    Services.fog.testResetFOG();

    info("Submit a full-page request and search twice before returning text");
    await AIWindowTestUtils.dispatchSmartbarCommit(
      tab.linkedBrowser,
      "search the web for something",
      "chat"
    );
    for (const id of ["initial-search", "handoff-search"]) {
      await mockEngineManager.respondTo({
        purpose: "chat",
        response: {
          text: "",
          toolCalls: [
            {
              id,
              type: "function",
              function: {
                name: "search_the_web",
                arguments: JSON.stringify({ query: "test search query" }),
              },
            },
          ],
        },
      });
      if (id === "initial-search") {
        await mockSearchManager.respondTo({ response: { results: [] } });
      }
    }

    const continuation = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    Assert.ok(
      runSearchStub.calledOnce,
      "The repeated search triggers a handoff"
    );
    Assert.ok(
      AIWindowUI.isSidebarOpen(win),
      "The sidebar is open after handoff"
    );

    info("Return the first response text in the sidebar");
    continuation.respond("Here are your search results.");
    await TestUtils.waitForCondition(
      () => firstCharacterDisplayed.called,
      "The sidebar receives the first-character event through the content actor"
    );
    const sidebarBrowser = win.document.getElementById("ai-window-browser");
    const messages = await getSidebarChatMessages(sidebarBrowser);
    Assert.ok(
      messages.some(
        message =>
          message.role === "assistant" &&
          message.message === "Here are your search results." &&
          message.hasRendered
      ),
      "The response text is rendered after the search handoff"
    );

    const [request] = Glean.smartWindow.modelRequest.testGetValue();
    const firstCharacters =
      Glean.smartWindow.timeToFirstCharacter.testGetValue() ?? [];
    // The handoff loses the originating AIWindow's pending telemetry context.
    todo_is(
      firstCharacters.length,
      1,
      "Bug 2075708: One first-character event is recorded after the search handoff"
    );
    if (firstCharacters.length) {
      Assert.deepEqual(
        firstCharacters.map(({ extra }) => ({
          chat_id: extra.chat_id,
          request_id: extra.request_id,
        })),
        [
          {
            chat_id: request.extra.chat_id,
            request_id: request.extra.request_id,
          },
        ],
        "Exactly one first-character event remains associated with the original request after handoff"
      );
    }
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    mockSearchManager.rejectAllRequests();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    mockSearchManager.cleanupMocks();
    sb.restore();
    Services.fog.testResetFOG();
    await SpecialPowers.popPrefEnv();
  }
});
