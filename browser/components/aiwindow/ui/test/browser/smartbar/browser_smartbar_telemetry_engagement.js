/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;
let expectedModel;

add_setup(async function () {
  expectedModel = await setupSmartbarTelemetry();
  win = await openSharedAIWindow();
});

describe("Smartbar engagement telemetry", () => {
  let browser;
  let sandbox;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    await resetTelemetry();
    await openOnlyAIWindowTab(win);
    browser = win.gBrowser.selectedBrowser;
  });

  afterEach(() => {
    sandbox.restore();
  });

  // TODO (bug 2025792): Add tests for `extra_key`s `bounce` and `disable`.
  describe("urlbar engagement", () => {
    it("carries the Smart Window extra keys for a chat", async () => {
      sandbox.stub(Chat, "fetchWithHistory");
      sandbox.stub(openAIEngine, "build");
      await typeInSmartbar(browser, "tell me a joke");

      await submitSmartbar(browser);

      const events = Glean.urlbar.engagement.testGetValue() ?? [];
      const smartbarEvent = events.find(e => e.extra.sap === "smartbar");
      Assert.ok(smartbarEvent, "Should have a smartbar engagement event");
      const extra = smartbarEvent.extra;
      Assert.ok(extra.chat_id, "engagement has chat_id");
      Assert.equal(extra.intent, "chat", "engagement has correct intent");
      Assert.equal(
        extra.window_mode,
        "smartwindow",
        "engagement has correct window mode"
      );
      Assert.equal(
        extra.location,
        "fullpage",
        "engagement has correct location"
      );
      Assert.equal(extra.model, expectedModel, "engagement has correct model");
      Assert.equal(
        extra.selected_result,
        "ai_chat",
        "engagement has correct selected_result"
      );
    });

    it("reports the AI search fallback row when it is picked", async () => {
      await stubLoadURL(browser);
      await promiseSmartbarSuggestionsOpen(browser, () =>
        typeInSmartbar(browser, "tell me a joke")
      );
      await waitForSmartbarAction(browser, "chat");

      await SpecialPowers.spawn(
        browser,
        [UrlbarShared.RESULT_TYPE.SEARCH],
        async searchResultType => {
          const smartbar = content.document
            .querySelector("ai-window")
            .shadowRoot.querySelector("#ai-window-smartbar");
          const fallbackRow = await ContentTaskUtils.waitForCondition(
            () =>
              [...smartbar.querySelectorAll(".urlbarView-row")].find(
                row =>
                  row.result?.providerName == "UrlbarProviderAiChat" &&
                  row.result.type == searchResultType
              ),
            "Wait for the AiChat search fallback row to render"
          );
          EventUtils.synthesizeMouseAtCenter(fallbackRow, {}, content);
        }
      );

      const events = Glean.urlbar.engagement.testGetValue() ?? [];
      const smartbarEvent = events.find(e => e.extra.sap === "smartbar");
      Assert.equal(
        smartbarEvent?.extra.selected_result,
        "ai_search_fallback",
        "engagement has correct selected_result"
      );
      Assert.equal(
        smartbarEvent?.extra.results,
        "ai_chat,ai_search_fallback",
        "smartbar lists the correct results"
      );
    });
  });

  describe("action menu", () => {
    it("records intent_change_preview when it opens", async () => {
      await typeInSmartbar(browser, "test query");

      await openActionMenu(browser);

      const events = Glean.smartWindow.intentChangePreview.testGetValue();
      Assert.equal(events.length, 1, "intent_change_preview event recorded");
      const extra = events[0].extra;
      Assert.ok(extra.chat_id, "intent_change_preview has chat_id");
      Assert.equal(
        extra.current_intent,
        "chat",
        "intent_change_preview has correct current_intent"
      );
      Assert.equal(
        extra.location,
        "fullpage",
        "intent_change_preview has correct location"
      );
      Assert.equal(
        extra.message_seq,
        "0",
        "intent_change_preview has correct message_seq"
      );
    });
  });
});
