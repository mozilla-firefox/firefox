/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

/* import-globals-from ../head_aitab.js */
Services.scriptloader.loadSubScript(
  new URL("../head_aitab.js", getRootDirectory(gTestPath)).href,
  this
);

const { GENERATE_AITAB } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const { MODEL_FEATURES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const CHAT_URL = "https://example.org/only-mentioned-in-chat";
const SMART_PAGE_URL = "about:smartpage?page=some_page";

const tabUrl = index => `https://example.com/?tab=${index}`;

describe("when selecting URLs to build an AITab", () => {
  let gWin;
  let gMockEngineManager;
  let gCleanup;

  beforeEach(async () => {
    ({
      win: gWin,
      mockEngineManager: gMockEngineManager,
      cleanup: gCleanup,
    } = await openAITabTestWindow());
  });

  afterEach(async () => {
    await gCleanup?.();
    gCleanup = null;
  });

  describe("from the tab context menu", () => {
    async function createAITabFromTabMenu(urlsOldestFirst) {
      const tabs = await openTabsOldestFirst(gWin, urlsOldestFirst);
      await selectTabs(gWin, tabs);
      const menu = await openTabContextMenu(gWin, tabs.at(-1));
      return activateAndWaitForAITab(gWin, () =>
        activateItemAndWait(
          menu,
          gWin.document.getElementById("context_createAITab")
        )
      );
    }

    it("should list the selected tabs newest first", async () => {
      const aiTab = await createAITabFromTabMenu([
        tabUrl(0),
        tabUrl(1),
        tabUrl(2),
      ]);
      await assertAITabCreationPrompt(
        aiTab,
        [tabUrl(2), tabUrl(1), tabUrl(0)],
        gMockEngineManager
      );
    });

    it("should drop selected tabs that are not web pages", async () => {
      // should ignore about:robots
      const aiTab = await createAITabFromTabMenu([
        "about:robots",
        tabUrl(0),
        tabUrl(1),
      ]);
      await assertAITabCreationPrompt(
        aiTab,
        [tabUrl(1), tabUrl(0)],
        gMockEngineManager
      );
    });

    it("should list a page open in two selected tabs once", async () => {
      // should ignore the repeated tabUrl(0)
      const aiTab = await createAITabFromTabMenu([
        tabUrl(0),
        tabUrl(1),
        tabUrl(0),
      ]);
      await assertAITabCreationPrompt(
        aiTab,
        [tabUrl(0), tabUrl(1)],
        gMockEngineManager
      );
    });

    it("should drop about:smartpage without a page name", async () => {
      const aiTab = await createAITabFromTabMenu([
        tabUrl(0),
        SMART_PAGE_URL,
        "about:smartpage",
      ]);
      await assertAITabCreationPrompt(
        aiTab,
        [SMART_PAGE_URL, tabUrl(0)],
        gMockEngineManager
      );
    });
  });

  describe("from the chat", () => {
    it("should use a URL from the chat message after a tab was mentioned", async () => {
      // The prompts mocked by ../head.js lack the ones generateAITab loads.
      _setRemoteClientForTesting({
        get: async () => [...MOCK_RS_RECORDS, ...AITAB_GENERATION_RECORDS],
      });
      const mentionedTabUrl = tabUrl(0);
      await openTabsOldestFirst(gWin, [mentionedTabUrl]);

      const browser = gWin.gBrowser.selectedBrowser;
      await insertInlineMention(browser);
      await typeInSmartbar(browser, ` make a page with ${CHAT_URL}`);
      await submitSmartbar(browser);

      const { request, respond } = await gMockEngineManager.captureRequest({
        purpose: MODEL_FEATURES.CHAT,
      });
      const userPrompt = request.args.findLast(m => m.role === "user").content;
      const mentionedToken = userPrompt.match(
        /\]\((§url_token: [A-Z0-9_]+§)\)/
      )?.[1];
      const chatToken = userPrompt.match(
        /make a page with (§url_token: [A-Z0-9_]+§)/
      )?.[1];
      Assert.ok(
        mentionedToken,
        "The inline mention URL reaches the model as a token"
      );
      Assert.ok(chatToken, "The chat URL reaches the model as a token");
      Assert.notEqual(
        mentionedToken,
        chatToken,
        "The inline mention and the chat URLs have distinct tokens"
      );

      respond({
        text: "",
        tokens: null,
        isPrompt: false,
        toolCalls: [
          {
            id: `${GENERATE_AITAB}_0`,
            function: {
              name: GENERATE_AITAB,
              arguments: JSON.stringify({
                url_list: [chatToken, mentionedToken],
              }),
            },
          },
        ],
      });
      const { request: generationRequest, respond: respondToGeneration } =
        await gMockEngineManager.captureRequest({
          purpose: MODEL_FEATURES.AITAB,
        });
      const sourcePrompt = generationRequest.args.findLast(
        m => m.role === "user"
      ).content;
      const sourceTokens = Array.from(
        sourcePrompt.matchAll(/^URL: (§url_token: [A-Z0-9_]+§)$/gm),
        match => match[1]
      );
      // Generation has its own tokenizer, so token counters can differ.
      const withoutCounter = token => token.replace(/_\d+§$/, "§");
      Assert.deepEqual(
        sourceTokens.map(withoutCounter),
        [mentionedToken, chatToken].map(withoutCounter),
        "The generation model gets the mentioned tab first and the chat URL after it, even though the tool call listed them in reverse order"
      );
      const title = "A page from the mention and chat URL";
      respondToGeneration(JSON.stringify(makeAITabSurface(title)));
      await waitForAITabReadyCard(browser, title);
      await waitForAITabPromptComplete(browser);
    });
  });
});
