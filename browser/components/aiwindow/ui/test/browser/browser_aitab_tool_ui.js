/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);

const SOURCE_URL =
  "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_context_url_page.html";

const SURFACE = {
  components: [
    { id: "root", component: "Page", header: "header", children: ["body"] },
    { id: "header", component: "Header", title: "Lisbon hotels" },
    { id: "body", component: "TextBlock", lead: "A generated page." },
  ],
  dataModel: {},
};

const AITAB_RECORDS = [
  {
    kind: "params",
    feature: "aitab",
    model: "test-model",
    service_type: "ai",
    purpose: "aitab",
    parameters: {},
    modules: [
      { name: "system-instructions", version: "v1.0" },
      { name: "user-data", version: "v1.0" },
    ],
    version: "v1.0",
    is_default: true,
  },
  {
    kind: "module",
    feature: "aitab",
    module: "system-instructions",
    model: "test-model",
    prompts: "Create a page using these components:\n{schemas}",
    version: "v1.0",
  },
  {
    kind: "module",
    feature: "aitab",
    module: "user-data",
    model: "test-model",
    prompts: "Focus: {focus}\n{pageContent}",
    version: "v1.0",
  },
];

let STATE_CHOOSE, STATE_COMPLETE;

add_setup(async function importAITabStates() {
  ({ STATE_CHOOSE, STATE_COMPLETE } =
    await import("chrome://browser/content/aiwindow/components/aitab-tool-ui.mjs"));
});

async function startAITabGeneration(win, mockEngine) {
  const browser = win.gBrowser.selectedBrowser;
  await typeInSmartbar(browser, "Create a page about Lisbon hotels");
  await submitSmartbar(browser);

  const chatRequest = await mockEngine.captureRequest({ purpose: "chat" });
  chatRequest.respond({
    text: "",
    tokens: null,
    isPrompt: false,
    toolCalls: [
      {
        id: "aitab-call",
        function: {
          name: "generate_page",
          arguments: JSON.stringify({
            url_list: [SOURCE_URL],
            focus: "Lisbon hotels",
          }),
        },
      },
    ],
  });
  return mockEngine.captureRequest({ purpose: "aitab" });
}

async function readAITabCard(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const chatBrowser = aiWindow?.shadowRoot?.querySelector("#aichat-browser");
    if (!chatBrowser) {
      return null;
    }
    return SpecialPowers.spawn(chatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const card = chatContent?.shadowRoot.querySelector("aitab-tool-ui");
      if (!card) {
        return null;
      }
      const cardElement = card.wrappedJSObject || card;
      await cardElement.updateComplete;
      return {
        state: cardElement.state,
        title: card.shadowRoot.querySelector(".page-title")?.textContent.trim(),
        buttonCount: card.shadowRoot.querySelectorAll("moz-button").length,
        hasSpinner: !!chatContent.shadowRoot.querySelector(
          "chat-assistant-loader"
        ),
      };
    });
  });
}

async function readAITabActionLog(browser) {
  const chatBrowser = await getAichatBrowser(browser);
  return SpecialPowers.spawn(chatBrowser, [], async () => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const log = chatContent.shadowRoot.querySelector("ai-action-result");
    if (!log) {
      return null;
    }
    const logElement = log.wrappedJSObject || log;
    await logElement.updateComplete;
    return {
      isLoading: logElement.isLoading,
      rowLabels: logElement.rows.map(row => row.labelL10nId),
      hasCard: !!chatContent.shadowRoot.querySelector("aitab-tool-ui"),
      hasSpinner: !!chatContent.shadowRoot.querySelector(
        "chat-assistant-loader"
      ),
    };
  });
}

function waitForAITabActionLog(browser, label) {
  return TestUtils.waitForCondition(async () => {
    const log = await readAITabActionLog(browser);
    return log?.rowLabels.includes(label) ? log : false;
  }, `Wait for the AITab action log row ${label}`);
}

function waitForAITabCardState(browser, state) {
  return TestUtils.waitForCondition(async () => {
    const card = await readAITabCard(browser);
    return card?.state === state ? card : false;
  }, `Wait for the AITab ${state} state`);
}

async function clickAITabAction(win, mockEngine, buttonIndex) {
  const generationRequest = await startAITabGeneration(win, mockEngine);
  generationRequest.respond(JSON.stringify(SURFACE));
  await waitForAITabCardState(win.gBrowser.selectedBrowser, STATE_CHOOSE);

  return SpecialPowers.spawn(
    win.gBrowser.selectedBrowser,
    [buttonIndex],
    async actionIndex => {
      const aiWindow = content.document.querySelector("ai-window");
      const chatBrowser = aiWindow.shadowRoot.querySelector("#aichat-browser");
      return SpecialPowers.spawn(chatBrowser, [actionIndex], async index => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const card = chatContent.shadowRoot.querySelector("aitab-tool-ui");
        let requestedAction;
        card.addEventListener(
          "aitab-open-request",
          event => {
            event.stopImmediatePropagation();
            requestedAction = event.detail;
          },
          { capture: true, once: true }
        );
        card.shadowRoot.querySelectorAll("moz-button")[index].click();
        return requestedAction;
      });
    }
  );
}

const OPEN_HERE_BUTTON_INDEX = 0;
const OPEN_NEW_TAB_BUTTON_INDEX = 1;

async function clickReadyAITabAction(browser, buttonIndex) {
  const chatBrowser = await getAichatBrowser(browser);
  await SpecialPowers.spawn(chatBrowser, [buttonIndex], index => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const card = chatContent.shadowRoot.querySelector("aitab-tool-ui");
    const button = card.shadowRoot.querySelectorAll("moz-button")[index];
    content.setTimeout(() => button.click());
  });
}

async function clickCompletedAITabTitle(browser) {
  const chatBrowser = await getAichatBrowser(browser);
  await SpecialPowers.spawn(chatBrowser, [], async () => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const card = chatContent.shadowRoot.querySelector("aitab-tool-ui");
    await card.wrappedJSObject.updateComplete;
    const link = card.shadowRoot.querySelector("a.page-title");
    content.setTimeout(() => link.click());
  });
}

describe("AITab ToolUI", () => {
  let win;
  let mockEngine;
  let restoreSignIn;

  beforeEach(async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.aitab.enabled", true]],
    });
    await AITabStore.destroyDatabase();
    _setRemoteClientForTesting({
      get: async () => [...MOCK_RS_RECORDS, ...AITAB_RECORDS],
    });
    restoreSignIn = skipSignIn();
    mockEngine = new MockEngineManager();
    win = await openAIWindow();
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
    restoreSignIn();
    await BrowserTestUtils.closeWindow(win);
    await AITabStore.destroyDatabase();
    _setRemoteClientForTesting({ get: async () => MOCK_RS_RECORDS });
    await SpecialPowers.popPrefEnv();
  });

  it("shows an action log while the page is being created", async () => {
    await startAITabGeneration(win, mockEngine);
    const loading = await waitForAITabActionLog(
      win.gBrowser.selectedBrowser,
      "smartwindow-aitab-creating"
    );
    Assert.ok(loading.isLoading, "The action log is loading");
    Assert.ok(!loading.hasCard, "The AITab card waits for generation");
    Assert.deepEqual(loading.rowLabels, ["smartwindow-aitab-creating"]);
    Assert.ok(!loading.hasSpinner, "The loading UI suppresses the spinner");
  });

  it("shows the ready card and commits extracted page security flags when the AITab page is generated", async () => {
    const generationRequest = await startAITabGeneration(win, mockEngine);
    const conversation = AIWindow.getActiveConversation(win);
    Assert.ok(
      JSON.stringify(generationRequest.request.args).includes(
        "Test page for context URL chip"
      ),
      "AITab generation extracted the source page"
    );
    Assert.equal(
      conversation.securityProperties.untrustedInput,
      false,
      "The extracted page's security flags are staged during generation"
    );

    generationRequest.respond(JSON.stringify(SURFACE));
    const ready = await waitForAITabCardState(
      win.gBrowser.selectedBrowser,
      STATE_CHOOSE
    );
    Assert.equal(ready.title, "Lisbon hotels");
    Assert.equal(ready.buttonCount, 2);

    const completed = await waitForAITabActionLog(
      win.gBrowser.selectedBrowser,
      "smartwindow-aitab-created-an-aitab"
    );
    Assert.ok(!completed.isLoading, "The action log has completed");
    Assert.deepEqual(
      completed.rowLabels,
      ["smartwindow-aitab-created-an-aitab"],
      "The completed result replaces the pending row"
    );

    await TestUtils.waitForCondition(
      () => conversation.securityProperties.untrustedInput,
      "Wait for generate_page to commit the staged security flags"
    );
    Assert.equal(
      conversation.securityProperties.privateData,
      true,
      "Extracted page content is committed as private data"
    );
    Assert.equal(
      conversation.securityProperties.untrustedInput,
      true,
      "Extracted page content is committed as untrusted input"
    );
  });

  it("emits only the current target from Open here", async () => {
    const requestedAction = await clickAITabAction(win, mockEngine, 0);
    Assert.deepEqual(requestedAction, { openTarget: "current" });
  });

  it("emits only the new target from Open in new tab", async () => {
    const requestedAction = await clickAITabAction(win, mockEngine, 1);
    Assert.deepEqual(requestedAction, { openTarget: "new" });
  });

  describe("AITab failure cleanup", () => {
    let sandbox;
    let generationRequest;
    let conversation;
    let browser;

    beforeEach(async () => {
      sandbox = sinon.createSandbox();
      generationRequest = await startAITabGeneration(win, mockEngine);
      browser = win.gBrowser.selectedBrowser;
      conversation = AIWindow.getActiveConversation(win);
      await waitForAITabActionLog(browser, "smartwindow-aitab-creating");
    });

    afterEach(() => {
      sandbox.restore();
    });

    it("updates the action log and retains the generation error", async () => {
      await checkFailure(
        "invalid JSON",
        "The page could not be created: the model did not return valid JSON. " +
          "Please surface to the user that there was an internal error, but " +
          "the user may try again."
      );
    });

    it("updates the action log and retains the persistence error", async () => {
      // stub create to fake a disk space error
      sandbox
        .stub(AITabStore, "create")
        .rejects(new Error("no space left on device"));

      await checkFailure(
        JSON.stringify(SURFACE),
        "The page could not be created: it could not be saved. Tell the " +
          "user the page could not be saved. Do not try again."
      );
    });

    async function checkFailure(response, expectedError) {
      generationRequest.respond(response);
      const followUp = await mockEngine.captureRequest({ purpose: "chat" });
      const toolMessage = conversation.messages.find(
        message => message.content?.tool_call_id === "aitab-call"
      );

      Assert.equal(toolMessage.content.body, expectedError);
      Assert.ok(
        !conversation.messages.some(
          message => message.toolUIData?.toolCallId === "aitab-call"
        ),
        "The failed tool has no AITab UI data"
      );
      const failed = await waitForAITabActionLog(
        browser,
        "smartwindow-aitab-creating-an-aitab-failed"
      );
      Assert.deepEqual(
        failed.rowLabels,
        ["smartwindow-aitab-creating-an-aitab-failed"],
        "The failure replaces the pending row"
      );
      await TestUtils.waitForCondition(
        async () => (await readAITabCard(browser)) === null,
        "The failed generation shows no AITab card"
      );

      followUp.respond({
        text: "The page could not be created.",
        tokens: null,
        isPrompt: false,
      });
    }
  });

  describe("navigation", () => {
    let chatTab;
    let viewerURL;
    let initialTabCount;

    beforeEach(async () => {
      const generationRequest = await startAITabGeneration(win, mockEngine);
      generationRequest.respond(JSON.stringify(SURFACE));
      chatTab = win.gBrowser.selectedTab;
      await waitForAITabCardState(chatTab.linkedBrowser, STATE_CHOOSE);
      const conversation = AIWindow.getActiveConversation(win);
      const [page] = await AITabStore.getAITabPagesByConvId(conversation.id);
      viewerURL = `about:smartpage?page=${page.slug}`;
      initialTabCount = win.gBrowser.tabs.length;
    });

    it("opens the stored page in the current tab from Open here", async () => {
      const loaded = BrowserTestUtils.browserLoaded(
        chatTab.linkedBrowser,
        false,
        viewerURL
      );

      await clickReadyAITabAction(
        chatTab.linkedBrowser,
        OPEN_HERE_BUTTON_INDEX
      );
      await loaded;

      Assert.equal(win.gBrowser.tabs.length, initialTabCount);
      Assert.equal(win.gBrowser.selectedTab, chatTab);
      Assert.equal(chatTab.linkedBrowser.currentURI.spec, viewerURL);
    });

    it("opens the stored page in a new tab from Open in new tab", async () => {
      const unrelatedTab = BrowserTestUtils.addTab(
        win.gBrowser,
        "about:blank",
        {
          index: Array.from(win.gBrowser.tabs).indexOf(chatTab) + 1,
        }
      );
      const opened = BrowserTestUtils.waitForNewTab(
        win.gBrowser,
        viewerURL,
        true
      );

      await clickReadyAITabAction(
        chatTab.linkedBrowser,
        OPEN_NEW_TAB_BUTTON_INDEX
      );
      const newTab = await opened;

      Assert.equal(win.gBrowser.tabs.length, initialTabCount + 2);
      Assert.notEqual(newTab, chatTab);
      Assert.equal(win.gBrowser.selectedTab, newTab);
      Assert.equal(newTab.linkedBrowser.currentURI.spec, viewerURL);
      const tabs = Array.from(win.gBrowser.tabs);
      Assert.equal(
        tabs.indexOf(newTab),
        tabs.indexOf(chatTab) + 1,
        "The generated page opens immediately after the chat tab"
      );
      Assert.equal(
        tabs.indexOf(unrelatedTab),
        tabs.indexOf(newTab) + 1,
        "The unrelated tab remains after the generated page"
      );
    });

    it("opens a new tab from the completed title when the page is closed", async () => {
      const opened = BrowserTestUtils.waitForNewTab(
        win.gBrowser,
        viewerURL,
        true
      );
      await clickReadyAITabAction(
        chatTab.linkedBrowser,
        OPEN_NEW_TAB_BUTTON_INDEX
      );
      const pageTab = await opened;

      await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
      await waitForAITabCardState(chatTab.linkedBrowser, STATE_COMPLETE);
      BrowserTestUtils.removeTab(pageTab);

      const reopened = BrowserTestUtils.waitForNewTab(
        win.gBrowser,
        viewerURL,
        true
      );
      await clickCompletedAITabTitle(chatTab.linkedBrowser);
      const newTab = await reopened;

      Assert.equal(win.gBrowser.tabs.length, initialTabCount + 1);
      Assert.equal(win.gBrowser.selectedTab, newTab);
    });

    it("focuses the matching tab from the completed title when the page is open", async () => {
      const opened = BrowserTestUtils.waitForNewTab(
        win.gBrowser,
        viewerURL,
        true
      );
      await clickReadyAITabAction(
        chatTab.linkedBrowser,
        OPEN_NEW_TAB_BUTTON_INDEX
      );
      const pageTab = await opened;

      await BrowserTestUtils.switchTab(win.gBrowser, chatTab);
      await waitForAITabCardState(chatTab.linkedBrowser, STATE_COMPLETE);
      const selected = BrowserTestUtils.waitForEvent(
        win.gBrowser.tabContainer,
        "TabSelect",
        false,
        event => event.target === pageTab
      );

      await clickCompletedAITabTitle(chatTab.linkedBrowser);
      await selected;

      Assert.equal(
        win.gBrowser.tabs.length,
        initialTabCount + 1,
        "No duplicate page tab is opened"
      );
      Assert.equal(
        win.gBrowser.selectedTab,
        pageTab,
        "The existing tab is focused"
      );
    });
  });

  it("keeps the complete state after returning to the chat tab", async () => {
    Services.fog.testResetFOG();
    const generationRequest = await startAITabGeneration(win, mockEngine);
    generationRequest.respond(JSON.stringify(SURFACE));
    const chatTab = win.gBrowser.selectedTab;
    await waitForAITabCardState(chatTab.linkedBrowser, STATE_CHOOSE);
    const initialTabCount = win.gBrowser.tabs.length;

    await SpecialPowers.spawn(chatTab.linkedBrowser, [], async () => {
      const aiWindow = content.document.querySelector("ai-window");
      const chatBrowser = aiWindow.shadowRoot.querySelector("#aichat-browser");
      await SpecialPowers.spawn(chatBrowser, [], async () => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const card = chatContent.shadowRoot.querySelector("aitab-tool-ui");
        card.shadowRoot.querySelectorAll("moz-button")[1].click();
      });
    });

    await TestUtils.waitForCondition(
      () => win.gBrowser.tabs.length === initialTabCount + 1,
      "The AITab opens in a new tab"
    );
    const newTab = win.gBrowser.selectedTab;
    win.gBrowser.selectedTab = chatTab;

    await waitForAITabCardState(chatTab.linkedBrowser, STATE_COMPLETE);
    const opened = Glean.smartWindow.aitabOpen.testGetValue();
    Assert.equal(
      opened?.length,
      1,
      "Opening the page from the card is recorded"
    );
    Assert.equal(opened[0].extra.open_target, "new");
    Assert.equal(opened[0].extra.card_state, STATE_CHOOSE);
    Assert.ok(opened[0].extra.chat_id, "The open shares the chat id");

    const selected = BrowserTestUtils.waitForEvent(
      win.gBrowser.tabContainer,
      "TabSelect",
      false,
      event => event.target === newTab
    );
    await clickCompletedAITabTitle(chatTab.linkedBrowser);
    await selected;
    const revisited = Glean.smartWindow.aitabOpen.testGetValue();
    Assert.equal(
      revisited.length,
      2,
      "A later open from the finished card is recorded"
    );
    Assert.equal(revisited[1].extra.card_state, STATE_COMPLETE);
    Assert.equal(revisited[1].extra.open_target, "new");
    BrowserTestUtils.removeTab(newTab);
  });
});
