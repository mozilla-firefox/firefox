/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);
const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowConstants.sys.mjs"
);
const { Chat } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs"
);
const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const { FEATURE_MAJOR_VERSIONS, MODEL_FEATURES, SERVICE_TYPES, PURPOSES } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { AIWindowTestUtils, MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { AIWindowUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs"
);
const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { IntentClassifier } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs"
);
const { GET_PAGE_CONTENT } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);
const { _setRemoteClientForTesting, _clearRemoteClientForTesting } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );
const { _setLoadPromptForTesting } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);

AIWindowTestUtils.init(this, window);

const TEST_MODEL = "test-model";

function getLastAssistantResponse(conversation) {
  return conversation.messages
    .filter(m => m.role == MESSAGE_ROLE.ASSISTANT)
    .filter(m => m.content.type === "text")
    .at(-1);
}

add_task(async function test_chat_streams_end_to_end() {
  const requests = [];
  await withServer(
    {
      streamChunks: ["Hello ", "from mock server."],
      onRequest(body) {
        requests.push(body);
      },
    },
    async () => {
      const conversation = new ChatConversation({
        title: "chat title",
        description: "chat desc",
        pageUrl: new URL("https://example.com"),
        pageMeta: {},
      });
      conversation.addUserMessage("Please say hello", "https://example.com", 0);
      conversation.addAssistantMessage("text", "");

      // withServer sets up the mock HTTP server, so use the real engine
      const engine = await openAIEngine.build({
        model: TEST_MODEL,
        serviceType: SERVICE_TYPES.AI,
        purpose: PURPOSES.CHAT,
        flowId: null,
        feature: MODEL_FEATURES.CHAT,
      });
      conversation.engine = engine;
      await Chat.fetchWithHistory({ conversation });

      Assert.equal(
        getLastAssistantResponse(conversation).content.body,
        "Hello from mock server.",
        "Assistant text streams end to end"
      );

      Assert.equal(requests.length, 1, "Single OpenAI request sent");
      Assert.ok(requests[0].stream, "Request uses streaming");
      Assert.equal(requests[0].messages?.length, 1, "User message forwarded");
      Assert.equal(
        requests[0].messages?.[0]?.content,
        "Please say hello",
        "User content forwarded"
      );
      Assert.equal(
        requests[0].messages?.[0]?.role,
        "user",
        "User role preserved"
      );

      const toolMessages = conversation.messages.filter(
        message => message.role === MESSAGE_ROLE.TOOL
      );
      Assert.equal(toolMessages.length, 0, "No tool calls for plain response");

      Assert.ok(Chat.lastUsage, "Usage should be captured from stream");
      Assert.equal(
        Chat.lastUsage.prompt_tokens,
        10,
        "prompt_tokens should be 10"
      );
      Assert.equal(
        Chat.lastUsage.completion_tokens,
        5,
        "completion_tokens should be 5"
      );
      Assert.equal(
        Chat.lastUsage.total_tokens,
        15,
        "total_tokens should be 15"
      );
    }
  );
});

add_task(async function test_chat_tool_call_get_open_tabs() {
  const { AIWindow } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs"
  );

  // Stubbing the isAIWindowActive check to allow tool calls to work in the test environment
  // Using a real AIWindow interferes with the openAIEngine
  // The stub will make the code think the current window is the active AIWindow which allows get_open_tabs to work
  const isAIWindowActiveStub = sinon
    .stub(AIWindow, "isAIWindowActive")
    .callsFake(win => win === window);

  let tab1, tab2;
  try {
    tab1 = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "https://example.com/one",
      true
    );
    tab2 = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "https://example.com/two",
      true
    );

    await withServer(
      {
        toolCall: { name: "get_open_tabs", args: "{}" },
        followupChunks: ["Here are your tabs."],
      },
      async () => {
        const engine = await openAIEngine.build({
          model: TEST_MODEL,
          serviceType: SERVICE_TYPES.AI,
          purpose: PURPOSES.CHAT,
          flowId: null,
          feature: MODEL_FEATURES.CHAT,
        });
        const conversation = new ChatConversation({
          title: "chat title",
          description: "chat desc",
          pageUrl: new URL("https://example.com"),
          pageMeta: {},
        });
        conversation.engine = engine;
        conversation.addUserMessage("List tabs", "https://example.com", 0);
        conversation.addAssistantMessage("text", "");

        await Chat.fetchWithHistory({ conversation });

        Assert.equal(
          getLastAssistantResponse(conversation).content.body,
          "Here are your tabs.",
          "Assistant should stream follow-up text"
        );

        const toolMessages = conversation.messages.filter(
          message => message.role === MESSAGE_ROLE.TOOL
        );
        Assert.equal(toolMessages.length, 1, "Tool result recorded");
        Assert.ok(
          Array.isArray(toolMessages[0].content.body.tabs),
          "Tool result should hold a tab list"
        );
        info("got tabs: " + JSON.stringify(toolMessages[0].content.body));
        Assert.equal(
          toolMessages[0].content.body.tabs.length,
          2,
          "Returns both tabs"
        );

        Assert.ok(Chat.lastUsage, "Usage should be captured after tool call");
        Assert.equal(
          Chat.lastUsage.total_tokens,
          15,
          "total_tokens should be 15"
        );
      }
    );
  } finally {
    isAIWindowActiveStub.restore();
    BrowserTestUtils.removeTab(tab1);
    BrowserTestUtils.removeTab(tab2);
  }
});

add_task(async function test_chat_tool_call_search_browsing_history() {
  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits([
    { uri: "https://example.com/llama", title: "Llama News" },
    { uri: "https://example.org/guide", title: "Llama Guide" },
  ]);

  const mockEngineManager = new MockEngineManager();

  try {
    const conversation = new ChatConversation({
      title: "chat title",
      description: "chat desc",
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    conversation.addUserMessage("Search history", "https://example.com", 0);
    conversation.addAssistantMessage("text", "");

    const engine = await openAIEngine.build({
      model: TEST_MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    conversation.engine = engine;
    const fetchPromise = Chat.fetchWithHistory({ conversation });
    await mockEngineManager.respondToWithToolCall({
      toolName: "search_browsing_history",
      toolParams: { searchTerm: "llama", historyLimit: 5 },
    });
    await mockEngineManager.respondTo({
      purpose: PURPOSES.CHAT,
      response: "History ready.",
    });
    await fetchPromise;

    Assert.equal(
      getLastAssistantResponse(conversation).content.body,
      "History ready.",
      "Assistant should stream follow-up text"
    );

    const toolMessages = conversation.messages.filter(
      message => message.role === MESSAGE_ROLE.TOOL
    );
    Assert.equal(toolMessages.length, 1, "Tool result recorded");
    const toolResult = toolMessages[0].content.body;
    info("got history: " + JSON.stringify(toolResult));
    Assert.greaterOrEqual(
      toolResult.results.length,
      1,
      "History tool returns stored visits"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await PlacesUtils.history.clear();
  }
});

add_task(async function test_chat_tool_call_get_page_content() {
  const html = `<!DOCTYPE html><html><body><article><h1>Headline</h1><p>Body text.</p></article></body></html>`;
  const { url, server: pageServer } = serveHTML(html);
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url, true);

  const mockEngineManager = new MockEngineManager();

  try {
    const conversation = new ChatConversation({
      title: "chat title",
      description: "chat desc",
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    conversation.addUserMessage("Read page", "https://example.com", 0);
    conversation.addAssistantMessage("text", "");

    const engine = await openAIEngine.build({
      model: TEST_MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    conversation.engine = engine;
    const fetchPromise = Chat.fetchWithHistory({ conversation });
    await mockEngineManager.respondToWithToolCall({
      toolName: "get_page_content",
      toolParams: { url_list: [url] },
    });
    await mockEngineManager.respondTo({
      purpose: PURPOSES.CHAT,
      response: "Content ready.",
    });
    await fetchPromise;

    Assert.equal(
      getLastAssistantResponse(conversation).content.body,
      "Content ready.",
      "Assistant should stream follow-up text"
    );

    const toolMessages = conversation.messages.filter(
      message => message.role === MESSAGE_ROLE.TOOL
    );
    Assert.equal(toolMessages.length, 1, "Tool result recorded");
    const contentArray = toolMessages[0].content.body;
    info("got content: " + contentArray);
    Assert.ok(
      Array.isArray(contentArray) &&
        typeof contentArray[0] === "string" &&
        contentArray[0].includes("Headline") &&
        contentArray[0].includes("Body text."),
      "Page content should be extracted"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    window.document.documentElement.removeAttribute("ai-window");
    BrowserTestUtils.removeTab(tab);
    await new Promise(resolve => pageServer.stop(resolve));
  }
});

add_task(async function test_chat_tool_call_get_navigation_info() {
  const { SmartWindowNavigationInfo } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/SmartWindowNavigationInfo.sys.mjs"
  );

  const FAKE_NAV_RESULTS = [
    {
      url: "about:preferences#manageMemories",
      label: "Manage memories",
      breadcrumb: "Settings > AI Controls > Smart Window > Manage memories",
      description: "Memories are what Smart Window learns from your activity.",
      similarity: 0.85,
    },
  ];

  const sb = sinon.createSandbox();
  sb.stub(SmartWindowNavigationInfo, "getRelevantNavigation").resolves(
    FAKE_NAV_RESULTS
  );

  const mockEngineManager = new MockEngineManager();

  try {
    const conversation = new ChatConversation({
      title: "chat title",
      description: "chat desc",
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    conversation.addUserMessage(
      "How do I manage memories?",
      "https://example.com",
      0
    );
    conversation.addAssistantMessage("text", "");

    const engine = await openAIEngine.build({
      model: TEST_MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    conversation.engine = engine;
    const fetchPromise = Chat.fetchWithHistory({ conversation });
    await mockEngineManager.respondToWithToolCall({
      toolName: "get_navigation_info",
      toolParams: { query: "manage memories" },
    });
    await mockEngineManager.respondTo({
      purpose: PURPOSES.CHAT,
      response: "Navigation ready.",
    });
    await fetchPromise;

    Assert.equal(
      getLastAssistantResponse(conversation).content.body,
      "Navigation ready.",
      "Assistant should stream follow-up text"
    );

    const toolMessages = conversation.messages.filter(
      message => message.role === MESSAGE_ROLE.TOOL
    );
    Assert.equal(toolMessages.length, 1, "Tool result recorded");

    const navResults = toolMessages[0].content.body;
    info("got nav results: " + JSON.stringify(navResults));
    Assert.equal(navResults.length, 1, "Returns the stubbed nav entry");
    Assert.equal(
      navResults[0].url,
      "about:preferences#manageMemories",
      "Nav entry url flows through"
    );
    Assert.ok(navResults[0].label, "Nav entry has label");
    Assert.ok(navResults[0].breadcrumb, "Nav entry has breadcrumb");
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sb.restore();
  }
});

add_task(async function test_chat_tool_call_get_user_memories() {
  // Clear existing memories
  const preTestMemories = await MemoryStore.getMemories({
    includeSoftDeleted: true,
  });
  for (const memory of preTestMemories) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }

  // Add temp test memories
  const testMemories = [
    {
      memory_summary: "Loves drinking coffee",
      category: "Food & Drink",
      intent: "Plan / Organize",
      score: 3,
    },
    {
      memory_summary: "Buys dog food online",
      category: "Pets & Animals",
      intent: "Buy / Acquire",
      score: 4,
    },
  ];
  for (const memory of testMemories) {
    await MemoryStore.addMemory(memory);
  }

  const mockEngineManager = new MockEngineManager();

  try {
    const conversation = new ChatConversation({
      title: "chat title",
      description: "chat desc",
      pageUrl: new URL("https://example.com"),
      pageMeta: {},
    });
    conversation.addUserMessage(
      "Tell me everything you know about me",
      "https://example.com",
      0
    );
    conversation.addAssistantMessage("text", "");

    const engine = await openAIEngine.build({
      model: TEST_MODEL,
      serviceType: SERVICE_TYPES.AI,
      purpose: PURPOSES.CHAT,
      flowId: null,
      feature: MODEL_FEATURES.CHAT,
    });
    conversation.engine = engine;
    const fetchPromise = Chat.fetchWithHistory({ conversation });
    await mockEngineManager.respondToWithToolCall({
      toolName: "get_user_memories",
      toolParams: {},
    });
    await mockEngineManager.respondTo({
      purpose: PURPOSES.CHAT,
      response: "Memories ready.",
    });
    await fetchPromise;

    Assert.equal(
      getLastAssistantResponse(conversation).content.body,
      "Memories ready.",
      "Assistant should stream follow-up text"
    );

    const toolMessages = conversation.messages.filter(
      message => message.role === MESSAGE_ROLE.TOOL
    );
    Assert.equal(toolMessages.length, 1, "Tool result recorded");
    const returnedMemories = toolMessages[0].content.body;
    info("got memories: " + returnedMemories);
    Assert.ok(
      returnedMemories.includes("Loves drinking coffee"),
      "Memories tool call should return the 1st expected memory"
    );
    Assert.ok(
      returnedMemories.includes("Buys dog food online"),
      "Memories tool call should return the 2nd expected memory"
    );
    Assert.equal(
      returnedMemories.length,
      2,
      "Memories tool call should return exactly 2 memories"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    // Clear temp memories
    const postTestMemories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });
    for (const memory of postTestMemories) {
      await MemoryStore.hardDeleteMemory(memory.id);
    }
  }
});

// End-to-end check that inference parameters actually reach the model request.
// Uses the real engine + a real local server (withServer), and asserts against
// the parsed body the server received:
//   1. params configured on the conversation (e.g. from the RS model config),
//   2. response_format,
//   3. tool_choice.
add_task(async function test_run_forwards_params_to_model_request() {
  const requests = [];
  await withServer(
    {
      streamChunks: ["ok"],
      onRequest: body => requests.push(body),
    },
    async () => {
      const engine = await openAIEngine.build({
        model: TEST_MODEL,
        serviceType: SERVICE_TYPES.AI,
        purpose: PURPOSES.CHAT,
        flowId: null,
        feature: MODEL_FEATURES.CHAT,
      });

      const conversation = new Conversation({
        engine,
        parameters: { temperature: 0.42, top_p: 0.9 },
      });
      conversation.setSystemMessage("system");
      conversation.addUserMessage("hello");

      const responseFormat = {
        type: "json_schema",
        json_schema: {
          name: "Test",
          strict: false,
          schema: { type: "object" },
        },
      };

      const generator = conversation.runWithGenerator({
        streamOptions: { enabled: true },
        inferenceParams: {
          tool_choice: "auto",
          response_format: responseFormat,
        },
      });
      // eslint-disable-next-line no-unused-vars
      for await (const _chunk of generator) {
        // Drain the stream so the request completes.
      }

      Assert.equal(requests.length, 1, "One request reached the model");
      const body = requests[0];

      // (1) conversation.parameters reach the request.
      Assert.equal(
        body.temperature,
        0.42,
        "temperature from conversation.parameters reaches the request"
      );
      Assert.equal(
        body.top_p,
        0.9,
        "top_p from conversation.parameters reaches the request"
      );

      // (2) response_format reaches the request.
      Assert.deepEqual(
        body.response_format,
        responseFormat,
        "response_format reaches the request"
      );

      // (3) tool_choice reaches the request.
      Assert.equal(body.tool_choice, "auto", "tool_choice reaches the request");
    }
  );
});

async function setupSmartPageChatEnvironment() {
  const version = feature => `v${FEATURE_MAJOR_VERSIONS[feature]}.0`;
  const records = [
    MODEL_FEATURES.CHAT,
    MODEL_FEATURES.TITLE_GENERATION,
    MODEL_FEATURES.CONVERSATION_STARTERS_SIDEBAR_SYSTEM,
    MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER,
    MODEL_FEATURES.CONVERSATION_SUGGESTIONS_ASSISTANT_LIMITATIONS,
  ].map(feature => ({
    feature,
    model: feature === MODEL_FEATURES.CHAT ? "generic" : "test-model",
    service_type: "ai",
    parameters: {},
    prompts:
      feature === MODEL_FEATURES.CONVERSATION_SUGGESTIONS_SIDEBAR_STARTER
        ? "Generate {n} prompts for {current_tab}.\nOpen tabs: {open_tabs}"
        : "Test system prompt.",
    version: version(feature),
    is_default: true,
  }));
  records.push({
    kind: "params",
    feature: MODEL_FEATURES.CHAT,
    model: "generic",
    model_choice_id: "0",
    service_type: "ai",
    purpose: "chat",
    parameters: {},
    version: version(MODEL_FEATURES.CHAT),
    is_default: true,
  });

  _setRemoteClientForTesting({ get: async () => records });
  _setLoadPromptForTesting(async () => "Test system prompt.");

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.aitab.enabled", true],
      ["browser.smartwindow.sidebar.openByDefault", false],
      ["browser.search.suggest.enabled", false],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.smartwindow.chat.interactionCount", 0],
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
    ],
  });

  const intentStub = sinon.stub(IntentClassifier, "_createEngine").resolves({
    run: () => [{ label: "chat", score: 1 }],
  });
  return { intentStub, mockEngine: new MockEngineManager() };
}

function seedSmartPageRecord(bodyText) {
  return AITabStore.create({
    convId: "smartpage-browser-test",
    slug: "test_page",
    title: "Stored test page",
    components: {
      surface: {
        components: [
          {
            id: "root",
            component: "Page",
            header: "header",
            children: ["body"],
          },
          {
            id: "header",
            component: "Header",
            title: "Stored test page",
          },
          { id: "body", component: "TextBlock", lead: bodyText },
        ],
        dataModel: {},
      },
    },
  });
}

async function openSmartPageSidebar(win, smartPageUrl) {
  const smartPageBrowser = win.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(smartPageBrowser, smartPageUrl);
  await BrowserTestUtils.browserLoaded(smartPageBrowser, {
    wantLoad: smartPageUrl,
  });

  // make sure the smart page is loaded
  await SpecialPowers.spawn(smartPageBrowser, [], async () => {
    await content.customElements.whenDefined("aitab-page");
    const page = content.document.querySelector("aitab-page").wrappedJSObject;
    await ContentTaskUtils.waitForCondition(
      () => page.status != "loading",
      "The Smart Page finishes loading"
    );
    Assert.equal(page.status, "ready", "The Smart Page is available");
    await page.updateComplete;
    await Promise.all(
      [...page.shadowRoot.querySelectorAll("*")].map(
        element => element.updateComplete
      )
    );
  });

  await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "https://example.com/",
    true
  );
  if (!AIWindowUI.isSidebarOpen(win)) {
    AIWindowUI.toggleSidebar(win);
  }
  const sidebarBrowser = win.document.getElementById("ai-window-browser");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarBrowser,
    { attributes: true, attributeFilter: ["data-conversation-id"] },
    () => sidebarBrowser.hasAttribute("data-conversation-id")
  );
  await AIWindowUI.openSidebar(win, null);
  return sidebarBrowser;
}

async function submitSmartPageChatPrompt(sidebarBrowser, prompt) {
  await SpecialPowers.spawn(sidebarBrowser, [prompt], async inputText => {
    const aiWindow = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindow.shadowRoot,
      { childList: true, subtree: true },
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
    );
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const chips = smartbar.querySelector(".smartbar-context-chips-header");
    await ContentTaskUtils.waitForMutationCondition(
      chips.shadowRoot,
      { childList: true, subtree: true },
      () => chips.websites?.length === 1
    );
    // Remove the implicit current-tab chip so it cannot set privateData
    // before GetPageContent runs.
    smartbar.removeContextMention(chips.websites[0].url);

    smartbar.inputField.focus();
    EventUtils.sendString(inputText, content);
    await smartbar.lastQueryContextPromise;
  });
  await AIWindowTestUtils.selectExplicitSmartbarAction(sidebarBrowser, "chat");
  await AIWindowTestUtils.waitForSmartbarAction(sidebarBrowser, "chat");
  await SpecialPowers.spawn(sidebarBrowser, [], async () => {
    content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar")
      .inputField.focus();
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
  });
}

add_task(async function test_chat_tool_call_reads_smartpage_and_sets_flags() {
  const smartPageUrl = "about:smartpage?page=test_page";
  const bodyText = "Stored smart page body text.";
  const { intentStub, mockEngine } = await setupSmartPageChatEnvironment();
  let win;
  let page;

  try {
    page = await seedSmartPageRecord(bodyText);
    win = await AIWindowTestUtils.openReadyAIWindow();
    const sidebarBrowser = await openSmartPageSidebar(win, smartPageUrl);
    await submitSmartPageChatPrompt(
      sidebarBrowser,
      "Read the stored test page."
    );

    const firstTurn = await mockEngine.captureRequest({ purpose: "chat" });
    const conversation = await TestUtils.waitForCondition(
      () => AIWindow.getActiveConversation(win),
      "A fresh sidebar conversation exists"
    );

    Assert.equal(
      conversation.securityProperties.privateData,
      false,
      "Private data has not been read before the tool call"
    );
    Assert.equal(
      conversation.securityProperties.untrustedInput,
      false,
      "Untrusted input has not been read before the tool call"
    );

    firstTurn.respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_read_smartpage",
          function: {
            name: GET_PAGE_CONTENT,
            arguments: JSON.stringify({ url_list: [smartPageUrl] }),
          },
        },
      ],
    });

    const followup = await mockEngine.captureRequest({ purpose: "chat" });
    const toolMessage = await TestUtils.waitForCondition(
      () =>
        conversation.messages.find(
          message => message.role === MESSAGE_ROLE.TOOL
        ),
      "The tool adds a result to the sidebar conversation"
    );
    Assert.equal(toolMessage.content.name, GET_PAGE_CONTENT);
    Assert.ok(
      toolMessage.content.body[0].includes(bodyText),
      "The tool result contains text extracted from the stored page"
    );

    Assert.equal(
      conversation.securityProperties.privateData,
      true,
      "The chat committed the private-data flag"
    );
    Assert.equal(
      conversation.securityProperties.untrustedInput,
      true,
      "The chat committed the untrusted-input flag"
    );

    followup.respond("I read the stored page.");
    await mockEngine.respondTo({
      purpose: "title-generation",
      response: "Stored page",
    });
  } finally {
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    if (page) {
      await AITabStore.deleteBySlug(page.slug);
    }
    intentStub.restore();
    _setLoadPromptForTesting(null);
    _clearRemoteClientForTesting();
    await SpecialPowers.popPrefEnv();
  }
});
