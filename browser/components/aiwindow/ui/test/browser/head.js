/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  AIWindowAccountAuth:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowAccountAuth.sys.mjs",
  Chat: "moz-src:///browser/components/aiwindow/models/Chat.sys.mjs",
  ChatConversation:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs",
  getModelForChoice:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  IntentClassifier:
    "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs",
  MemoriesManager:
    "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs",
  MENTION_TYPE:
    "moz-src:///browser/components/urlbar/SmartbarMentionsPanelSearch.sys.mjs",
  openAIEngine: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  SmartbarMentionsPanelSearch:
    "moz-src:///browser/components/urlbar/SmartbarMentionsPanelSearch.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SessionWindowUI:
    "moz-src:///browser/components/sessionstore/SessionWindowUI.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const { _setLoadPromptForTesting } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);

const { _setRemoteClientForTesting, _clearRemoteClientForTesting } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );

// Aliased to avoid colliding with ChatConversation.sys.mjs's own _setLoadPromptForTesting above.
const {
  _setLoadPromptForTesting: _setConversationSuggestionsLoadPromptForTesting,
  _setBuildConversationForTesting,
  _setGetConversationsByIdForTesting,
  _clearResumeActivityCacheForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ConversationSuggestions.sys.mjs"
);

/**
 * @import { SmartbarAction } from "chrome://browser/content/aiwindow/components/input-cta/input-cta.mjs"
 */

async function modelFor(choiceId) {
  return (await getModelForChoice(choiceId)).model;
}

const AIWINDOW_URL = "chrome://browser/content/aiwindow/aiWindow.html";
const FIRSTRUN_URL = "chrome://browser/content/aiwindow/firstrun.html";

let gIntentEngineStub;

// Minimal RS records returned by the global getRemoteClient stub.
// Version numbers must match FEATURE_MAJOR_VERSIONS in models/Utils.sys.mjs.
const MOCK_RS_RECORDS = [
  ["chat", 11],
  ["title-generation", 1],
  ["tab-group-naming", 1],
  ["conversation-starters-sidebar-system", 1],
  ["conversation-suggestions-sidebar-starter", 3],
  ["conversation-suggestions-followup", 1],
  ["conversation-suggestions-assistant-limitations", 1],
  ["conversation-suggestions-memories", 1],
  ["memories-initial-generation-system", 2],
  ["memories-initial-generation-user", 2],
  ["memories-sensitivity-filter-system", 1],
  ["memories-sensitivity-filter-user", 1],
  ["memories-quality-filter-system", 1],
  ["memories-quality-filter-user", 1],
  ["memories-message-classification-system", 1],
  ["memories-message-classification-user", 1],
  ["search-answer-generation", 1],
]
  .map(([feature, major]) => ({
    feature,
    // chat uses "generic" as the fallback model name (see selectMainConfig);
    // all other features use "test-model" with is_default: true.
    model: feature === "chat" ? "generic" : "test-model",
    service_type: "ai",
    parameters: {},
    prompts:
      feature === "conversation-suggestions-sidebar-starter"
        ? "Generate {n} prompts for {current_tab}.\nOpen tabs: {open_tabs}"
        : "Test system prompt.",
    version: `v${major}.0`,
    is_default: true,
  }))
  // Chat resolves model+params from v2 kind:"params" records (one generic
  // fallback + one per model choice).
  .concat([
    {
      kind: "params",
      feature: "chat",
      model: "generic",
      model_choice_id: "0",
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      is_default: true,
      version: "v10.0",
    },
    {
      kind: "params",
      feature: "chat",
      model: "gemini-3.1-flash-lite",
      model_choice_id: "1",
      model_details: {
        model: "gemini-3.1-flash-lite",
        ownerName: "Google",
        labelId: "fast",
        shortName: "Gemini 3.1 Flash Lite",
        brandName: "Gemini",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      prompts: "Test system prompt.",
      version: "v10.0",
    },
    {
      kind: "params",
      feature: "chat",
      model: "qwen3-235b-a22b-instruct-2507-maas",
      model_choice_id: "2",
      model_details: {
        model: "qwen3-235b-a22b-instruct-2507-maas",
        ownerName: "Alibaba",
        labelId: "allpurpose",
        shortName: "Qwen 3 235B",
        brandName: "Qwen",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      prompts: "Test system prompt.",
      version: "v10.0",
    },
    {
      kind: "params",
      feature: "chat",
      model: "gpt-oss-120b",
      model_choice_id: "3",
      model_details: {
        model: "gpt-oss-120b",
        ownerName: "OpenAI",
        labelId: "personal",
        shortName: "GPT OSS 120B",
        brandName: "GPT OSS",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      prompts: "Test system prompt.",
      version: "v10.0",
    },
    // TODO 2053495
    // v11 records for mistral release (browser.smartwindow.mistralRelease pref)
    {
      kind: "params",
      feature: "chat",
      model: "generic",
      model_choice_id: "0",
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      prompts: "Test system prompt.",
      version: "v11.0",
      is_default: true,
    },
    {
      kind: "params",
      feature: "chat",
      model: "mistral-small-2603",
      model_choice_id: "3",
      model_details: {
        model: "mistral-small-2603",
        ownerName: "Mistral",
        labelId: "personal",
        shortName: "Mistral Small 4",
        brandName: "Mistral",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      version: "v11.0",
    },
    {
      kind: "params",
      feature: "chat",
      model: "gemini-3.1-flash-lite",
      model_choice_id: "1",
      model_details: {
        model: "gemini-3.1-flash-lite",
        ownerName: "Google",
        labelId: "fast",
        shortName: "Gemini 3.1 Flash Lite",
        brandName: "Gemini",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      prompts: "Test system prompt.",
      version: "v11.0",
    },
    {
      kind: "params",
      feature: "chat",
      model: "qwen3-235b-a22b-instruct-2507-maas",
      model_choice_id: "2",
      model_details: {
        model: "qwen3-235b-a22b-instruct-2507-maas",
        ownerName: "Alibaba",
        labelId: "allpurpose",
        shortName: "Qwen 3 235B",
        brandName: "Qwen",
      },
      service_type: "ai",
      purpose: "chat",
      parameters: {},
      version: "v11.0",
    },
    // The relevant-memories module is loaded for the chat model, so it resolves
    // via the "generic" fallback rather than is_default like the v1 records.
    {
      kind: "params",
      feature: "memories-context",
      model: "generic",
      service_type: "memories",
      parameters: {},
      is_default: true,
      modules: [{ name: "relevant-memories", version: "1.0" }],
      version: "v1.0",
    },
    {
      kind: "module",
      feature: "memories-context",
      module: "relevant-memories",
      model: "generic",
      // The prompt renders the retrieved memory list, so it needs the
      // placeholder the real prompt has.
      prompts:
        "# Existing Memories\n\n## Existing Memories\n{relevantMemoriesList}",
      version: "v1.0",
    },
    // tab-group-naming resolves through the v2 modular path: a params manifest
    // plus one module record per prompt module (system-instructions, user-data).
    {
      kind: "params",
      feature: "tab-group-naming",
      model: "test-model",
      service_type: "ai",
      purpose: "auto-tab-grouping",
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
      feature: "tab-group-naming",
      module: "system-instructions",
      model: "test-model",
      prompts: "Name this group of tabs.",
      version: "v1.0",
    },
    {
      kind: "module",
      feature: "tab-group-naming",
      module: "user-data",
      model: "test-model",
      prompts: "Tabs:\n{titles}",
      version: "v1.0",
    },
  ]);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", true],
      ["browser.smartwindow.chat.interactionCount", 0],
    ],
  });

  // Stub intent engine so it doesn't attempt network requests
  const fakeIntentEngine = {
    run() {
      return [
        { label: "chat", score: 0.95 },
        { label: "search", score: 0.05 },
      ];
    },
  };
  gIntentEngineStub = sinon
    .stub(IntentClassifier, "_createEngine")
    .resolves(fakeIntentEngine);
  registerCleanupFunction(() => gIntentEngineStub.restore());

  // Stub the RS client so PromptLoader.buildConversation never hits real Remote Settings.
  _setRemoteClientForTesting({ get: async () => MOCK_RS_RECORDS });
  registerCleanupFunction(() => _clearRemoteClientForTesting());

  // Stub ChatConversation's loadPrompt so loadSystemPrompt and
  // injectRealTimeContext don't hit RS.
  _setLoadPromptForTesting(async () => "Test system prompt.");
  registerCleanupFunction(() => {
    _setLoadPromptForTesting(null);
  });
});

/**
 * Opens a new AI Window
 *
 * @param {object} options
 * @param {string} [options.waitForTabURL] - URL to wait for, or empty string to skip waiting
 * @returns {Promise<Window>}
 */
async function openAIWindow({ waitForTabURL = AIWINDOW_URL } = {}) {
  info("Opening new AI Window");
  const win = await BrowserTestUtils.openNewBrowserWindow({
    aiWindow: true,
    waitForTabURL,
  });
  info("Waiting for AI window attr");
  await BrowserTestUtils.waitForMutationCondition(
    win.document.documentElement,
    { attributes: true },
    () => win.document.documentElement.hasAttribute("ai-window")
  );
  info("Promising focus");
  await SimpleTest.promiseFocus(win);
  return win;
}

/**
 * Waits for the sidebar ai-window element to connect.
 *
 * @param {Window} win - Window reference
 * @returns {Promise<MozBrowser>} - The sidebar browser element
 */
async function waitForSidebarReady(win) {
  const sidebarBrowser = win.document.getElementById("ai-window-browser");
  await TestUtils.waitForCondition(
    () => sidebarBrowser.contentDocument?.querySelector("ai-window:defined"),
    "Sidebar ai-window should be loaded"
  );
  return sidebarBrowser;
}

/**
 * Waits for the sidebar slide animation to settle into its closed state. The
 * close animation keeps the box uncollapsed until the slide finishes, so
 * `box.collapsed` (set by AIWindowUI._commitSidebarCollapsed) is the only
 * reliable signal that the sidebar is fully closed.
 *
 * @param {Window} win - Window reference
 * @returns {Promise<Element>} - The sidebar box element
 */
async function waitForSidebarClosed(win) {
  const box = win.document.getElementById("ai-window-box");
  await TestUtils.waitForCondition(
    () => box.collapsed,
    "Wait for AI sidebar slide-close to finish"
  );
  return box;
}

/**
 * Waits for the sidebar slide animation to settle into its open state. During an
 * open slide the box is uncollapsed up-front while the splitter stays hidden, so
 * the splitter becoming visible (in AIWindowUI._commitSidebarCollapsed) is the
 * signal that the slide has fully committed.
 *
 * @param {Window} win - Window reference
 * @returns {Promise<Element>} - The sidebar box element
 */
async function waitForSidebarOpen(win) {
  const box = win.document.getElementById("ai-window-box");
  const splitter = win.document.getElementById("ai-window-splitter");
  await TestUtils.waitForCondition(
    () =>
      !box.collapsed && !splitter.collapsed && AIWindowUI.isSidebarOpen(win),
    "Wait for AI sidebar slide-open to finish"
  );
  return box;
}

/**
 * Opens a new AI Window with about:blank
 * and the chat assistant sidebar open
 *
 * @param {string} [url] - URL to navigate the tab to
 * @returns {Promise<{win: Window, sidebarBrowser: MozBrowser}>}
 */
async function openAIWindowWithSidebar(url = "about:blank") {
  const win = await openAIWindow();
  return openAIWindowSidebar(win, url);
}

/**
 * Navigates an AI Window to the given URL and opens the sidebar.
 *
 * @param {Window} win
 * @param {string} [url] - URL to navigate the tab to
 * @returns {Promise<{win: Window, sidebarBrowser: MozBrowser}>}
 */
async function openAIWindowSidebar(win, url = "about:blank") {
  BrowserTestUtils.startLoadingURIString(win.gBrowser.selectedBrowser, url);
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser, {
    wantLoad: url,
  });
  if (!AIWindowUI.isSidebarOpen(win)) {
    info("Opening sidebar");
    AIWindowUI.toggleSidebar(win);
  }
  const sidebarBrowser = await waitForSidebarReady(win);
  return { win, sidebarBrowser };
}

/**
 * NOTE: If using to navigate to https://example.com/ make sure
 * to use a trailing slash in the URL or this function will hang.
 *
 * @param {MozBrowser} browser
 * @param {string} url
 */
function promiseNavigateAndLoad(browser, url) {
  let loaded = BrowserTestUtils.browserLoaded(browser, {
    wantLoad: url,
  });
  BrowserTestUtils.startLoadingURIString(browser, url);
  return loaded;
}

async function getPromptButtons(browser) {
  const aiWindow = await TestUtils.waitForCondition(
    () => browser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element"
  );
  const promptsEl = await TestUtils.waitForCondition(
    () => aiWindow.shadowRoot.querySelector("smartwindow-prompts"),
    "Wait for smartwindow-prompts element"
  );
  return promptsEl.shadowRoot.querySelectorAll(".sw-prompt-button");
}

async function getConversationId(browser) {
  const aiWindow = await TestUtils.waitForCondition(
    () => browser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element"
  );
  return aiWindow.conversationId.toString();
}

async function stubResumeActivityGeneration(sb) {
  const urls = [1, 2, 3, 4, 5].map(id => ({
    url: `https://example.com/${id}`,
    title: `Example ${id}`,
  }));
  for (const [index, page] of urls.entries()) {
    await PlacesUtils.history.insert({
      ...page,
      visits: [
        {
          date: new Date(Date.now() - (urls.length - index) * 60_000),
          transition: PlacesUtils.history.TRANSITIONS.LINK,
        },
      ],
    });
  }

  const memories = [
    {
      id: "memory-1",
      memory_summary: "Research project",
      source_ids: {
        history_source_ids: urls
          .slice(0, 4)
          .map(({ url }) => PlacesUtils.history.hashURL(url)),
      },
    },
    {
      id: "memory-2",
      memory_summary: "Trip planning",
      source_ids: {
        history_source_ids: [PlacesUtils.history.hashURL(urls[4].url)],
      },
    },
  ];

  const getMemoriesStub = sb
    .stub(MemoriesManager, "getMemoriesByAttribute")
    .resolves(memories);
  // Keep cached results from being filtered as deleted.
  sb.stub(MemoriesManager, "getAllMemories").resolves(memories);
  // Prevent cached results from leaking between tests.
  _clearResumeActivityCacheForTesting();
  _setGetConversationsByIdForTesting(async () => []);
  _setConversationSuggestionsLoadPromptForTesting(async () => ({
    prompt: "Test prompt",
  }));
  _setBuildConversationForTesting(async () => ({
    setSystemMessage() {},
    addUserMessage() {},
    securityProperties: {
      setPrivateData() {},
      setUntrustedInput() {},
      commit() {},
    },
    run: sb.stub().resolves({
      finalOutput: JSON.stringify([
        {
          id: 0,
          headline: "Pick up your research",
          status: "Continue reading",
        },
      ]),
    }),
  }));
  sb.stub(openAIEngine, "getFxAccountToken").resolves(null);

  return {
    getMemoriesStub,
    memories,
    async cleanup() {
      _setGetConversationsByIdForTesting(null);
      _setConversationSuggestionsLoadPromptForTesting(null);
      _setBuildConversationForTesting(null);
      _clearResumeActivityCacheForTesting();
      for (const { url } of urls) {
        await PlacesUtils.history.remove(url);
      }
    },
  };
}

/**
 * Shared setup/teardown for tests that click a resume pill: enables the
 * memories prefs, stubs resume-activity generation so a real pill renders,
 * opens the AI Window, and hands the caller its buttons to click. `run`
 * supplies whatever additional stubs it needs (engine build, fetchWithHistory,
 * etc.) via its own sandbox before calling this.
 *
 * @param {object} sb - Sinon sandbox, owned and restored by the caller
 * @param {Function} run - Async callback invoked with
 *   {win, browser, aiWindow, buttons}
 */
async function testResumeActivityClick(sb, run) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", true],
      ["browser.smartwindow.memories.generateFromHistory", true],
    ],
  });
  const resumeActivityStubs = await stubResumeActivityGeneration(sb);
  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const buttons = await TestUtils.waitForCondition(async () => {
      const found = await getPromptButtons(browser);
      return found.length ? found : false;
    }, "Wait for prompt buttons to replace loading skeletons");
    await run({ win, browser, aiWindow, buttons });
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await resumeActivityStubs.cleanup();
    await SpecialPowers.popPrefEnv();
  }
}

/**
 * @typedef {object} EngineRunResponse
 * @property {string} [finalOutput] - The text content returned by the mock
 *   engine's run() method. Used by non-chat features like title generation.
 */

/**
 * @typedef {object} StubEngineNetworkBoundariesConfig
 * @property {Set<string>} [passthroughFeatures] - Feature names that use the
 *   real openAIEngine.build (going through RemoteSettings, ML engine creation,
 *   and the mock HTTP server). All other features get a mock engine that
 *   resolves immediately. Defaults to new Set(["chat"]).
 * @property {string} [fxAccountToken] - Value that the stubbed
 *   getFxAccountToken resolves with. Defaults to "mock-fxa-token".
 * @property {EngineRunResponse} [engineRunResponse] - What the mock engine's
 *   run() resolves with for non-passthrough features. Defaults to
 *   { finalOutput: "Mock" }.
 * @property {MockOpenAIServerOptions|null} [serverOptions] - Options passed
 *   to startMockOpenAI. When non-null, a mock server is started and the
 *   endpoint pref is pushed so passthrough features route to it. Defaults to
 *   {} which starts a server with startMockOpenAI defaults (single-chunk
 *   "Hello from mock." stream). Pass null to skip starting a server.
 */

/**
 * @typedef {object} StubEngineNetworkBoundariesResult
 * @property {Function} restore - Async cleanup function that restores all
 *   stubs, pops the endpoint pref, and stops the mock server.
 * @property {number|null} port - The mock server's port number, or null when
 *   no server was started.
 * @property {Array<object>} capturedRequests - Array that collects every
 *   parsed request body sent to the mock server. Empty when no server was
 *   started. Mutate .length = 0 to clear between phases of a test.
 */

/**
 * Stubs openAIEngine.build and openAIEngine.getFxAccountToken so that
 * background async operations (title generation, conversation starters, etc.)
 * resolve immediately instead of going through RemoteSettings / ML engine
 * creation / FxA token fetching. Without this, fire-and-forget calls like
 * #addConversationTitle() can leave suspended async chains that prevent
 * window GC, causing leaked-window failures under --verify.
 *
 * Features listed in passthroughFeatures still use the real build path
 * (e.g. "chat" needs to hit the mock HTTP server).
 *
 * By default starts a mock OpenAI HTTP server and pushes the endpoint pref
 * so passthrough features route to it. Pass serverOptions: null to skip.
 *
 * Call the returned async restore function to clean up stubs, pop prefs, and
 * stop the server.
 *
 * @deprecated Please use MockEngineManager in AIWindowTestUtils.sys.mjs.
 *   TODO (Bug 2045844): Remove and replace existing usages across test files.
 * @param {StubEngineNetworkBoundariesConfig} [config]
 * @returns {Promise<StubEngineNetworkBoundariesResult>}
 */
async function stubEngineNetworkBoundaries({
  passthroughFeatures = new Set(["chat"]),
  fxAccountToken = "mock-fxa-token",
  engineRunResponse = { finalOutput: "Mock" },
  serverOptions = {},
} = {}) {
  // Save the original build before stubbing so passthrough features (e.g.
  // "chat") can still create a real engine that routes requests to the mock
  // HTTP server via RemoteSettings + EngineProcess.
  const originalBuild = openAIEngine.build.bind(openAIEngine);
  const buildStub = sinon
    .stub(openAIEngine, "build")
    .callsFake(async (featureOrConfig, ...rest) => {
      const feature =
        typeof featureOrConfig === "object" && featureOrConfig !== null
          ? featureOrConfig.feature
          : featureOrConfig;
      if (passthroughFeatures.has(feature)) {
        return originalBuild(featureOrConfig, ...rest);
      }
      // Non-passthrough features get a mock engine whose methods resolve
      // synchronously, preventing dangling async chains.
      return {
        feature,
        model:
          typeof featureOrConfig === "object"
            ? featureOrConfig.model
            : "test-model",
        async run() {
          return engineRunResponse;
        },
        runWithGenerator() {},
      };
    });

  const tokenStub = sinon
    .stub(openAIEngine, "getFxAccountToken")
    .resolves(fxAccountToken);

  const capturedRequests = [];
  let server = null;
  let port = null;
  if (serverOptions) {
    const callerOnRequest = serverOptions.onRequest;
    ({ server, port } = startMockOpenAI({
      ...serverOptions,
      onRequest(body) {
        capturedRequests.push(body);
        callerOnRequest?.(body);
      },
    }));
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.endpoint", `http://localhost:${port}/v1`]],
    });
  }

  async function restore() {
    buildStub.restore();
    tokenStub.restore();
    if (server) {
      await SpecialPowers.popPrefEnv();
      await stopMockOpenAI(server);
    }
  }

  return { restore, port, capturedRequests };
}

/**
 * Stubs AIWindowAccountAuth.ensureAIWindowAccess to skip sign-in flow
 * Call the returned restore function to clean up the stub
 *
 * @returns {Function} restore function to clean up the stub
 */
function skipSignIn() {
  const stub = sinon
    .stub(AIWindowAccountAuth, "ensureAIWindowAccess")
    .resolves(true);
  return () => stub.restore();
}

/**
 * Clicks the New Chat button in the sidebar
 *
 * @param {Window} win
 */
async function clickNewChatButton(win) {
  const sidebarBrowser = win.document.getElementById("ai-window-browser");
  await TestUtils.waitForCondition(
    () => sidebarBrowser.contentDocument?.querySelector("ai-window:defined"),
    "Wait for ai-window to be defined in sidebar"
  );
  const aiWindow = sidebarBrowser.contentDocument.querySelector("ai-window");
  const newChatBtn = aiWindow.shadowRoot.querySelector(".new-chat-icon-button");
  newChatBtn.click();
}

/**
 * Opens the Smartbar's context menu via the "+" button and clicks the
 * panel item whose visible text matches the given label. Waits for the
 * panel to populate before selecting.
 *
 * @param {MozBrowser} sidebarBrowser - The sidebar browser element
 *   (e.g. win.document.getElementById("ai-window-browser"))
 * @param {string} label - The visible text label of the tab to select
 *   (e.g. the tab's title)
 */
async function openTabContextMenuAndClickTabByLabel(sidebarBrowser, label) {
  await SpecialPowers.spawn(sidebarBrowser, [label], async tabLabel => {
    const aiWindowElement = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window"),
      "Wait for ai-window"
    );
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar"
    );
    const contextButton = smartbar.querySelector("context-icon-button");
    const button = contextButton.shadowRoot.querySelector("moz-button");
    button.click();

    const panelList = smartbar.querySelector("smartwindow-panel-list");
    const panel = panelList.shadowRoot.querySelector("panel-list");
    await ContentTaskUtils.waitForMutationCondition(
      panel,
      { childList: true, subtree: true },
      () => panel.querySelector("panel-item:not(.panel-section-header)")
    );

    const items = panel.querySelectorAll(
      "panel-item:not(.panel-section-header)"
    );
    let targetItem;
    for (const item of items) {
      if (item.textContent.trim() === tabLabel) {
        targetItem = item;
        break;
      }
    }
    Assert.ok(
      targetItem,
      `Should find a tab labeled '${tabLabel}' in the context menu`
    );
    targetItem.click();
  });
}

async function getSmartbarContextChipLabels(browser, expectedUrl) {
  await TestUtils.waitForCondition(
    () => browser.contentDocument?.querySelector("ai-window:defined"),
    "Sidebar ai-window should be loaded"
  );

  return SpecialPowers.spawn(browser, [expectedUrl], async url => {
    const aiWindowElement = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window"),
      "Wait for ai-window to be rendered"
    );
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    const chipContainer = await ContentTaskUtils.waitForCondition(
      () => smartbar.querySelector(".smartbar-context-chips-header"),
      "Wait for chip container to be rendered"
    );

    if (url) {
      await ContentTaskUtils.waitForCondition(
        () =>
          Array.isArray(chipContainer.websites) &&
          chipContainer.websites.some(site => site.url.includes(url)),
        `Wait for chip with URL containing "${url}"`
      );
    } else {
      await ContentTaskUtils.waitForCondition(
        () =>
          Array.isArray(chipContainer.websites) &&
          chipContainer.websites.length,
        "Wait for at least one chip"
      );
    }

    const chips = chipContainer.shadowRoot.querySelectorAll("ai-website-chip");
    const chipLabels = Array.from(chips).map(
      chip => chip.shadowRoot?.querySelector(".chip-label")?.textContent ?? ""
    );

    return chipLabels;
  });
}

/**
 * Submits the current smartbar input.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {object} [options] - Options for submission
 * @param {boolean} [options.useButton=false] - If true, submit via CTA button
 */
async function submitSmartbar(browser, { useButton = false } = {}) {
  await SpecialPowers.spawn(browser, [useButton], async clickButton => {
    const aiWindow = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindow.shadowRoot,
      { childList: true, subtree: true },
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
    );
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    if (clickButton) {
      const inputCta = smartbar.querySelector("input-cta");
      const mozButton = inputCta.shadowRoot.querySelector("moz-button");
      const button = mozButton.shadowRoot.querySelector("button");
      button.click();
    } else {
      const inputCta = smartbar.querySelector("input-cta");
      await ContentTaskUtils.waitForCondition(
        () => inputCta.getAttribute("action") !== "stop",
        "Wait for generation to complete before submitting via Enter"
      );
      const inputField = smartbar.inputField;
      inputField.focus();
      EventUtils.synthesizeKey("KEY_Enter", {}, content);
    }
  });
}

/**
 * Select an explicit action from the smartbar CTA dropdown menu.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {SmartbarAction} action - The action to select
 */
async function selectExplicitSmartbarAction(browser, action) {
  await SpecialPowers.spawn(browser, [action], async actionType => {
    const aiWindow = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindow.shadowRoot,
      { childList: true, subtree: true },
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
    );
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const inputCta = smartbar.querySelector("input-cta");
    const mozButton = inputCta.shadowRoot.querySelector("moz-button");

    await ContentTaskUtils.waitForMutationCondition(
      mozButton.shadowRoot,
      { childList: true, subtree: true },
      () => mozButton.shadowRoot.querySelector("#chevron-button")
    );
    const chevronButton = mozButton.shadowRoot.querySelector("#chevron-button");
    const panelList = inputCta.shadowRoot.querySelector("panel-list");
    const shownPromise = ContentTaskUtils.waitForEvent(panelList, "shown");
    chevronButton.click();
    await shownPromise;

    const actionItem = panelList.querySelector(
      `panel-item[icon="${actionType}"]`
    );
    actionItem.click();
  });
}

/**
 * Select the first search engine from the smartbar CTA "Search with…" submenu.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function selectSmartbarSearchEngine(browser) {
  const inputCta = BrowserTestUtils.querySelectorDeep(
    browser.contentDocument,
    "input-cta"
  );
  const mozButton = BrowserTestUtils.querySelectorDeep(inputCta, "moz-button");

  const chevronButton = await TestUtils.waitForCondition(() =>
    mozButton.shadowRoot.querySelector("#chevron-button")
  );
  const [mainPanel, searchSubpanel] =
    inputCta.shadowRoot.querySelectorAll("panel-list");

  const mainShown = BrowserTestUtils.waitForEvent(mainPanel, "shown");
  chevronButton.click();
  await mainShown;

  const subpanelShown = BrowserTestUtils.waitForEvent(searchSubpanel, "shown");
  mainPanel.querySelector('panel-item[icon="search-with"]').click();
  await subpanelShown;

  const engineItem = await TestUtils.waitForCondition(() =>
    searchSubpanel.querySelector('panel-item[icon="engine"]')
  );
  engineItem.click();
}

/**
 * Wait for the smartbar action to be set.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {string} expectedAction - The expected action value
 */
async function waitForSmartbarAction(browser, expectedAction) {
  await SpecialPowers.spawn(browser, [expectedAction], async action => {
    const aiWindow = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindow.shadowRoot,
      { childList: true, subtree: true },
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
    );
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    await ContentTaskUtils.waitForCondition(
      () => smartbar.smartbarAction === action,
      `Wait for smartbar action to be "${action}"`
    );
  });
}

/**
 * Stub the smartbar's load path to prevent navigation. The load funnels through
 * the private #loadURL into controller.loadURL, so the controller is the seam.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {object} [options] - Options for the stub
 * @param {boolean} [options.captureURL=false] - If true, capture the URL
 */
async function stubLoadURL(browser, { captureURL = false } = {}) {
  await SpecialPowers.spawn(browser, [captureURL], async capture => {
    const aiWindow = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindow.shadowRoot,
      { childList: true, subtree: true },
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
    );
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    if (capture) {
      content._stubLoadURLCalled = false;
      content._stubLoadedURL = null;
      smartbar.controller.loadURL = ({ loadRequest }) => {
        content._stubLoadURLCalled = true;
        content._stubLoadedURL = loadRequest.urlLoad?.url ?? null;
        return {};
      };
    } else {
      smartbar.controller.loadURL = () => ({});
    }
  });
}

/**
 * Get the result of a stubbed and captured stubLoadURL call.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<{called: boolean, url: string|null}>}
 */
async function getStubLoadURLResult(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    return {
      called: content._stubLoadURLCalled,
      url: content._stubLoadedURL,
    };
  });
}

/**
 * Stub the controller's openSERP method to capture the search terms instead of
 * running a real search.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function stubOpenSERP(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(() =>
      ContentTaskUtils.querySelectorDeep(aiWindow, "#ai-window-smartbar")
    );
    content._stubOpenSERPCalled = false;
    content._stubOpenSERPTerms = null;
    content._stubOpenSERPEngine = null;
    smartbar.controller.openSERP = (engineId, searchTerms) => {
      content._stubOpenSERPCalled = true;
      content._stubOpenSERPTerms = searchTerms;
      content._stubOpenSERPEngine = engineId;
    };
  });
}

/**
 * Get the result of a stubbed openSERP call.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<{called: boolean, terms: ?string, engineId: ?string}>}
 */
async function getStubOpenSERPResult(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    return {
      called: content._stubOpenSERPCalled,
      terms: content._stubOpenSERPTerms,
      engineId: content._stubOpenSERPEngine,
    };
  });
}

/**
 * Assert the current smartbar value.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {string} expectedValue - The expected value
 * @param {string} message - The assertion message
 */
async function assertSmartbarValue(browser, expectedValue, message) {
  await SpecialPowers.spawn(
    browser,
    [expectedValue, message],
    async (val, msg) => {
      const aiWindow = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForMutationCondition(
        aiWindow.shadowRoot,
        { childList: true, subtree: true },
        () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar")
      );
      const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
      Assert.equal(smartbar.value, val, msg);
    }
  );
}

/**
 * Type text into the smartbar and wait for a pending query to complete.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {string} text - Text to type
 */
async function typeInSmartbar(browser, text) {
  await SpecialPowers.spawn(browser, [text], async searchText => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    info("typeInSmartbar: smartbar found, calling focus()");
    smartbar.inputField.focus();
    await ContentTaskUtils.waitForCondition(
      () => smartbar.matches(":focus-within"),
      "Wait for smartbar to receive focus"
    );
    info("typeInSmartbar: focus received, sending string");
    EventUtils.sendString(searchText, content);
    info("typeInSmartbar: string sent, awaiting lastQueryContextPromise");
    await smartbar.lastQueryContextPromise;
    info("typeInSmartbar: query complete");
  });
}

/**
 * Waits for the Smartbar suggestions view to open.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {Function} openFn - A function that should trigger the view opening
 */
async function promiseSmartbarSuggestionsOpen(browser, openFn) {
  if (!openFn) {
    throw new Error(
      "openFn should be supplied to promiseSmartbarSuggestionsOpen"
    );
  }

  const opened = SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    if (smartbar.view.isOpen) {
      return;
    }
    await ContentTaskUtils.waitForMutationCondition(
      smartbar,
      { attributes: true },
      () => smartbar.hasAttribute("open")
    );
  });
  await openFn();
  await opened;
}

/**
 * Waits for the Smartbar suggestions view to close.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function promiseSmartbarSuggestionsClose(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    if (!smartbar.view.isOpen) {
      return;
    }
    await ContentTaskUtils.waitForMutationCondition(
      smartbar,
      { attributes: true },
      () => !smartbar.hasAttribute("open")
    );
  });
}

/**
 * Asserts the Smartbar suggestions view position and visibility.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {boolean} shouldBeVisible - Whether the suggestions view should be visible
 * @param {string} expectedPosition - The expected position
 */
async function assertSmartbarSuggestionsVisible(
  browser,
  shouldBeVisible,
  expectedPosition = "bottom"
) {
  const aiWindowElement =
    browser.contentWindow.document.querySelector("ai-window");
  const smartbarElement = aiWindowElement.shadowRoot.querySelector(
    "#ai-window-smartbar"
  );
  const urlbarView = smartbarElement.querySelector(".urlbarView");

  Assert.equal(
    BrowserTestUtils.isVisible(urlbarView),
    shouldBeVisible,
    `Suggestions view element should be visible: ${shouldBeVisible}`
  );
  Assert.equal(
    smartbarElement.getAttribute("suggestions-position"),
    expectedPosition,
    `Suggestions position should be: ${expectedPosition}`
  );
}

/**
 * Wait for panel list to be visible.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<boolean>} True if panel is visible
 */
async function waitForPanelOpen(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    const panel = panelList.shadowRoot.querySelector("panel-list");

    await ContentTaskUtils.waitForMutationCondition(
      panel,
      { attributes: true, attributeFilter: ["open"] },
      () => panel.hasAttribute("open")
    );

    return panel.hasAttribute("open");
  });
}

/**
 * Wait for the mentions panel list to be visible.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<boolean>} True if mentions are open
 */
async function waitForMentionsOpen(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    const panel = panelList.shadowRoot.querySelector("panel-list");

    await ContentTaskUtils.waitForMutationCondition(
      panel,
      { attributes: true, attributeFilter: ["open"] },
      () => panel.hasAttribute("open")
    );

    return panel.hasAttribute("open");
  });
}

/**
 * Wait for a mention to be inserted.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<boolean>} True if the mention exists
 */
async function waitForMentionInserted(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const editor = smartbar.querySelector("moz-multiline-editor");

    await ContentTaskUtils.waitForMutationCondition(
      editor.shadowRoot,
      { childList: true, subtree: true },
      () => editor.shadowRoot.querySelector("ai-website-chip") !== null
    );

    return !!editor.shadowRoot.querySelector("ai-website-chip");
  });
}

/**
 * Click the first non-header item in the smartbar mention suggestions panel.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function selectFirstMentionPanelItem(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const panelList = smartbar.querySelector("smartwindow-panel-list");
    const panel = panelList.shadowRoot.querySelector("panel-list");
    const firstItem = panel.querySelector(
      "panel-item:not(.panel-section-header)"
    );
    firstItem.click();
  });
}

/**
 * Type "@", wait for the mention panel, click the first suggestion, and wait
 * for the mention chip to appear in the editor.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function insertInlineMention(browser) {
  await typeInSmartbar(browser, "@");
  await waitForPanelOpen(browser);
  await selectFirstMentionPanelItem(browser);
  await waitForMentionInserted(browser);
}

/**
 * Return inline @mention data from the editor's mentions plugin.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<Array<{type: string, id: string, label: string}>>}
 */
async function getEditorInlineMentions(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const editor = smartbar.querySelector("moz-multiline-editor");
    return editor.getAllMentions();
  });
}

/**
 * Get the context chips from the smartbar header.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<Array<{url: string, label: string}>>} The context chip data
 */
async function getSmartbarContextChips(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const chipContainer = smartbar.querySelector(
      ".smartbar-context-chips-header"
    );
    return chipContainer.websites.map(w => ({ url: w.url, label: w.label }));
  });
}

/**
 * Gets the smartbar model select data.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<object>} Returns model select data
 */
async function getSmartbarModelSelectData(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const modelSelect = smartbar.querySelector("input-model-select");

    return {
      availableModels: modelSelect.availableModels,
      selectedModelId: aiWindowElement.selectedModelId,
    };
  });
}

/**
 * Switches to a model in the smartbar by choice ID.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {string} modelChoiceId - Model choice id to switch to
 */
async function switchSmartbarModel(browser, modelChoiceId) {
  return SpecialPowers.spawn(browser, [modelChoiceId], async choiceId => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const modelSelect = smartbar.querySelector("input-model-select");
    const triggerButton = modelSelect.shadowRoot.querySelector("moz-button");
    triggerButton.click();

    const panelList = modelSelect.shadowRoot.querySelector("panel-list");
    await ContentTaskUtils.waitForMutationCondition(
      panelList,
      { attributes: true },
      () => panelList.hasAttribute("open")
    );

    const modelKeys = Object.keys(modelSelect.availableModels);
    const index = modelKeys.indexOf(choiceId);
    if (index === -1) {
      throw new Error(`Model choice "${choiceId}" not available`);
    }
    const targetModelId = modelSelect.availableModels[choiceId].model;
    panelList.querySelectorAll("button.model-item")[index].click();

    await ContentTaskUtils.waitForMutationCondition(
      aiWindowElement,
      { attributes: true },
      () => aiWindowElement.selectedModelId === targetModelId
    );
  });
}

/**
 * Returns the chat messages currently displayed in the sidebar.
 *
 * @param {MozBrowser} sidebarBrowser - The sidebar browser element
 * @returns {Promise<Array<{role: string, message: string}>>}
 */
async function getSidebarChatMessages(sidebarBrowser) {
  const aiWindow = await TestUtils.waitForCondition(
    () => sidebarBrowser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element"
  );
  const aichatBrowser = await TestUtils.waitForCondition(
    () => aiWindow.shadowRoot?.querySelector("#aichat-browser"),
    "Wait for #aichat-browser element"
  );
  return SpecialPowers.spawn(aichatBrowser, [], async () => {
    const contentEl = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-chat-content"),
      "Wait for ai-chat-content element"
    );
    await contentEl.updateComplete;
    const messageEls = contentEl.shadowRoot.querySelectorAll("ai-chat-message");
    return Array.from(messageEls, el => {
      const elJS = el.wrappedJSObject || el;
      return {
        role: elJS.role,
        message: elJS.message,
        hasRendered: !!el.shadowRoot?.querySelector(
          ".message-assistant, .message-user"
        ),
      };
    });
  });
}

/**
 * Default bound for the render waits below. Generous enough to never false-fail
 * behind the mock LLM round-trip, but a fraction of the harness timeout so a
 * genuine stall fails fast with a clear message rather than hanging.
 */
const RENDER_TIMEOUT_MS = 15000;

/**
 * Resolves the #aichat-browser frame for the AI Window hosted in the given
 * browser. By the time the post-response helpers below run, both ai-window and
 * #aichat-browser already exist, so the check resolves immediately; the bound
 * just guarantees a fast, clear failure if they ever don't.
 *
 * @param {MozBrowser} browser - The browser hosting the AI Window
 *
 * @returns {Promise<MozBrowser>} The #aichat-browser frame
 */
function getAIChatBrowser(browser) {
  return BrowserTestUtils.waitForMutationCondition(
    browser.contentDocument.documentElement,
    { childList: true, subtree: true },
    () =>
      browser.contentDocument
        ?.querySelector("ai-window")
        ?.shadowRoot?.querySelector("#aichat-browser"),
    { msg: "ai-window #aichat-browser", timeout: RENDER_TIMEOUT_MS }
  );
}

/**
 * Runs a SpecialPowers.spawn task bounded by a timeout. The content task can use
 * plain ContentTaskUtils.waitForMutationCondition (event-driven, no polling),
 * which never rejects on its own; this wrapper races the spawn against a timer so
 * a task that never settles fails fast with a clear message instead of hanging
 * until the harness aborts the SpecialPowers actor.
 *
 * @param {MozBrowser} browser - The frame to run the task in
 * @param {Array} args - Arguments forwarded to the task
 * @param {Function} task - The content task
 * @param {string} label - Description used in the timeout error message
 * @param {number} [timeoutMs=RENDER_TIMEOUT_MS]
 *
 * @returns {Promise<any>} The task's return value
 */
function spawnBounded(
  browser,
  args,
  task,
  label,
  timeoutMs = RENDER_TIMEOUT_MS
) {
  const spawned = SpecialPowers.spawn(browser, args, task);
  // If the timeout wins, the spawn promise is abandoned; handle its eventual
  // rejection (the actor is torn down at cleanup) so it isn't surfaced as an
  // unhandled rejection. A real task error still propagates through the race.
  spawned.catch(() => {});

  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(
      () => reject(new Error(`Timed out waiting for: ${label}`)),
      timeoutMs
    );
  });
  return Promise.race([spawned, timeout]).finally(() => clearTimeout(timer));
}

/**
 * Checks for presence of a selector within the chat messages, either at a
 * specific message element or in any message element
 *
 * @param {MozBrowser} browser - browser that the messages are in
 * @param {string} selector - The selector to look for
 * @param {number} [nthElement] - Which message index to check for the selector,
 *                                defaults to last item
 *
 * @returns {boolean} Whether the selector was found
 */
async function checkForElementInChatMessage(
  browser,
  selector,
  nthElement = -1
) {
  const aiChatBrowser = await getAIChatBrowser(browser);

  return spawnBounded(
    aiChatBrowser,
    [selector, nthElement],
    async (sel, nthEl) => {
      await ContentTaskUtils.waitForMutationCondition(
        content.document.documentElement,
        { childList: true, subtree: true },
        () => content.document.querySelector("ai-chat-content")
      );
      const contentEl = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForMutationCondition(
        contentEl.shadowRoot,
        { childList: true, subtree: true },
        () => {
          const messages = Array.from(
            contentEl.shadowRoot.querySelectorAll("ai-chat-message")
          );
          const message = messages.at(nthEl);
          return message && ContentTaskUtils.querySelectorDeep(message, sel);
        }
      );

      const messages = Array.from(
        contentEl.shadowRoot.querySelectorAll("ai-chat-message")
      );
      return !!ContentTaskUtils.querySelectorDeep(messages.at(nthEl), sel);
    },
    `${selector} in chat message ${nthElement}`
  );
}

async function checkForNumberOfElementsInChatMessage(
  browser,
  selector,
  amount,
  nthElement = -1
) {
  const aiChatBrowser = await getAIChatBrowser(browser);

  return spawnBounded(
    aiChatBrowser,
    [selector, nthElement, amount],
    async (sel, nthEl, amt) => {
      await ContentTaskUtils.waitForMutationCondition(
        content.document.documentElement,
        { childList: true, subtree: true },
        () => content.document.querySelector("ai-chat-content")
      );
      const contentEl = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForMutationCondition(
        contentEl.shadowRoot,
        { childList: true, subtree: true },
        () => {
          const messages = Array.from(
            contentEl.shadowRoot.querySelectorAll("ai-chat-message")
          );
          const message = messages.at(nthEl);
          return (
            message &&
            ContentTaskUtils.querySelectorDeep(message, "ai-chat-grid")
          );
        }
      );

      const messages = Array.from(
        contentEl.shadowRoot.querySelectorAll("ai-chat-message")
      );
      const aiChatGrid = ContentTaskUtils.querySelectorDeep(
        messages.at(nthEl),
        "ai-chat-grid"
      );

      // The counted children render asynchronously inside the grid's own shadow
      // root, so observe there.
      await ContentTaskUtils.waitForMutationCondition(
        aiChatGrid.shadowRoot,
        { childList: true, subtree: true },
        () =>
          ContentTaskUtils.querySelectorDeep(aiChatGrid, sel)?.children
            .length === amt
      );

      return true;
    },
    `${amount} ${selector} in chat message ${nthElement}`
  );
}

/**
 * Mock OpenAI server helpers
 */

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

function readRequestBody(request) {
  const stream = request.bodyInputStream;
  const available = stream.available();
  return NetUtil.readInputStreamToString(stream, available, {
    charset: "UTF-8",
  });
}

/**
 * @typedef {object} MockToolCall
 * @property {string} name - The tool function name (e.g. "run_search",
 *   "get_page_content").
 * @property {string} [args] - JSON-encoded arguments for the tool call.
 *   Defaults to "{}".
 */

/**
 * @typedef {object} MockOpenAIServerOptions
 * @property {string[]} [streamChunks] - Array of content strings sent as
 *   individual SSE chunks in the streaming response. Defaults to
 *   ["Hello from mock."].
 * @property {MockToolCall|null} [toolCall] - When non-null, the first
 *   streaming request that includes tools will respond with this tool call
 *   instead of text content. A subsequent request containing the tool result
 *   will receive followupChunks as the response. Defaults to null.
 * @property {string[]} [followupChunks] - Content chunks sent in the
 *   streaming response after a tool result is received. Only used when
 *   toolCall is set. Defaults to ["Tool complete."].
 * @property {Function} [onRequest] - Callback invoked with the parsed
 *   request body for every request to /v1/chat/completions.
 */

/**
 * Starts a local HTTP server that mimics the OpenAI chat completions API.
 *
 * Handles both streaming (SSE) and non-streaming (JSON) requests to
 * /v1/chat/completions. When toolCall is configured, the server simulates
 * a tool-use round-trip: the first request returns the tool call, and the
 * follow-up request (containing the tool result) returns followupChunks.
 *
 * @deprecated - Please use MockEngineManager in AIWindowTestUtils.sys.mjs unless
 * a test is explicitly needing to test the network layer of the OpenAI chat protocol.
 *   TODO (Bug 2045844): Remove and replace existing usages across test files.
 * @param {MockOpenAIServerOptions} [options]
 * @returns {{ server: HttpServer, port: number }} The running server and
 *   its port number.
 */
function startMockOpenAI({
  streamChunks = ["Hello from mock."],
  streamChunkDelayMs = 0,
  toolCall = null,
  followupChunks = ["Tool complete."],
  onRequest,
} = {}) {
  const server = new HttpServer();

  server.registerPathHandler("/v1/chat/completions", (request, response) => {
    let bodyText = "";
    if (request.method === "POST") {
      try {
        bodyText = readRequestBody(request);
      } catch (_) {}
    }

    let body;
    try {
      body = JSON.parse(bodyText || "{}");
    } catch (_) {
      body = {};
    }

    onRequest?.(body);

    const wantsStream = !!body.stream;
    const tools = Array.isArray(body.tools) ? body.tools : [];
    const askedForTools = tools.length;
    const messages = Array.isArray(body.messages) ? body.messages : [];
    const hasToolResult = messages.some(m => m && m.role === "tool");
    const timestamp = Math.floor(Date.now() / 1000);

    const startSSE = () => {
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.setHeader(
        "Content-Type",
        "text/event-stream; charset=utf-8",
        false
      );
      response.setHeader("Cache-Control", "no-cache", false);
      response.setHeader("Access-Control-Allow-Origin", "*", false);
      response.processAsync();
    };

    const sendSSE = obj => {
      // Encode data so special §followup:§-type tokens preserves utf-8
      response.write(
        Array.from(
          new TextEncoder().encode(`data: ${JSON.stringify(obj)}\n\n`),
          b => String.fromCharCode(b)
        ).join("")
      );
    };

    if (wantsStream && toolCall && askedForTools && !hasToolResult) {
      startSSE();

      sendSSE({
        id: "chatcmpl-aiwindow-stream-tool-1",
        object: "chat.completion.chunk",
        created: timestamp,
        model: "aiwindow-mock",
        choices: [
          {
            index: 0,
            delta: {
              content: "",
              tool_calls: [
                {
                  index: 0,
                  id: "call_1",
                  type: "function",
                  function: {
                    name: toolCall.name,
                    arguments: toolCall.args ?? "{}",
                  },
                },
              ],
            },
            finish_reason: null,
          },
        ],
      });

      sendSSE({
        id: "chatcmpl-aiwindow-stream-tool-2",
        object: "chat.completion.chunk",
        created: timestamp,
        model: "aiwindow-mock",
        choices: [{ index: 0, delta: {}, finish_reason: "tool_calls" }],
      });

      response.write("data: [DONE]\n\n");
      response.finish();
      return;
    }

    if (wantsStream && toolCall && askedForTools && hasToolResult) {
      startSSE();

      followupChunks.forEach((chunk, index) => {
        sendSSE({
          id: `chatcmpl-aiwindow-stream-tool-followup-${index}`,
          object: "chat.completion.chunk",
          created: timestamp,
          model: "aiwindow-mock",
          choices: [
            {
              index: 0,
              delta: { content: chunk },
              finish_reason:
                index === followupChunks.length - 1 ? "stop" : null,
            },
          ],
        });
      });

      response.write("data: [DONE]\n\n");
      response.finish();
      return;
    }

    if (wantsStream) {
      startSSE();

      (async () => {
        for (const [index, chunk] of streamChunks.entries()) {
          if (streamChunkDelayMs) {
            await new Promise(resolve =>
              setTimeout(resolve, streamChunkDelayMs)
            );
          }
          sendSSE({
            id: `chatcmpl-aiwindow-stream-${index}`,
            object: "chat.completion.chunk",
            created: timestamp,
            model: "aiwindow-mock",
            choices: [
              {
                index: 0,
                delta: { content: chunk },
                finish_reason:
                  index === streamChunks.length - 1 ? "stop" : null,
              },
            ],
          });
        }

        response.write("data: [DONE]\n\n");
        response.finish();
      })();
      return;
    }

    // Non-streaming fallback for conversation starters, title generation, etc.
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "application/json", false);
    response.write(
      JSON.stringify({
        id: "chatcmpl-aiwindow-non-stream",
        object: "chat.completion",
        created: timestamp,
        model: "aiwindow-mock",
        choices: [
          {
            index: 0,
            message: { role: "assistant", content: "Mock response" },
            finish_reason: "stop",
          },
        ],
      })
    );
  });

  server.start(-1);
  return { server, port: server.identity.primaryPort };
}

/**
 * Stops a running mock OpenAI server.
 *
 * @param {HttpServer} server - The server instance returned by startMockOpenAI.
 * @returns {Promise<void>} Resolves when the server has fully stopped.
 */
function stopMockOpenAI(server) {
  return new Promise(resolve => server.stop(resolve));
}

/**
 * Retrieves the context chip labels from a user message rendered in the
 * ai-chat-content area. Waits for the aichat-browser, chat content, and
 * chips to be available before reading labels.
 *
 * @param {MozBrowser} sidebarBrowser - The sidebar browser element
 *   (e.g. win.document.getElementById("ai-window-browser"))
 * @param {number} [messageIndex=0] - Zero-based index selecting which user
 *   message to read chips from (0 = first user message, 1 = second, etc.)
 * @returns {Promise<string[]>} Array of chip label strings from the
 *   website-chip-container in the selected user message
 */
async function getUserMessageChipLabels(sidebarBrowser, messageIndex = 0) {
  await TestUtils.waitForCondition(
    () => sidebarBrowser.contentDocument?.querySelector("ai-window:defined"),
    "Sidebar ai-window should be loaded"
  );

  const aiWindowEl = sidebarBrowser.contentDocument.querySelector("ai-window");
  const aichatBrowser = await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
    "Wait for aichat-browser"
  );

  return SpecialPowers.spawn(aichatBrowser, [messageIndex], async msgIndex => {
    const chatContent = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-chat-content"),
      "Wait for ai-chat-content"
    );

    const containers = await ContentTaskUtils.waitForCondition(() => {
      const found = chatContent.shadowRoot.querySelectorAll(
        ".chat-bubble-user website-chip-container"
      );
      return found.length > msgIndex ? found : null;
    }, `Wait for user message at index ${msgIndex}`);

    const chipContainer = containers[msgIndex];

    const chips = await ContentTaskUtils.waitForCondition(() => {
      const found =
        chipContainer.shadowRoot.querySelectorAll("ai-website-chip");
      return found.length ? found : null;
    }, "Wait for context chips to render");

    return Array.from(chips).map(
      chip => chip.shadowRoot?.querySelector(".chip-label")?.textContent ?? ""
    );
  });
}

/**
 * Convenience wrapper that starts a mock OpenAI server, pushes the endpoint
 * pref, stubs getFxAccountToken, runs a task, then tears everything down.
 *
 * Consider using stubEngineNetworkBoundaries instead for new tests — it
 * additionally stubs openAIEngine.build to prevent leaked-window issues from
 * background async operations, and its setup/restore pattern fits
 * beforeEach/afterEach without requiring a callback wrapper.
 *
 * @param {MockOpenAIServerOptions} serverOptions - Options for the mock server.
 * @param {Function} task - Async callback receiving { port }.
 */
async function withServer(serverOptions, task) {
  const { server, port } = startMockOpenAI(serverOptions);
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", `http://localhost:${port}/v1`],
      ["browser.smartwindow.customEndpoint", `http://localhost:${port}/v1`],
    ],
  });

  const getFxAccountTokenStub = sinon
    .stub(openAIEngine, "getFxAccountToken")
    .resolves("mock-fxa-token");

  try {
    await task({ port });
  } finally {
    getFxAccountTokenStub.restore();
    await SpecialPowers.popPrefEnv();
    await stopMockOpenAI(server);
  }
}

/**
 * Waits for ai-window, then its shadowRoot, then the loaded #aichat-browser.
 *
 * @param {object} browser - The chrome browser element hosting ai-window
 * @returns {Promise<object>} The aichat browser element
 */
async function getAichatBrowser(browser) {
  const aiWindowEl = await TestUtils.waitForCondition(
    () => browser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element to exist"
  );

  await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot,
    "Wait for ai-window shadowRoot to be ready"
  );

  const aichatBrowser = await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot.querySelector("#aichat-browser"),
    "Wait for aichat-browser element"
  );

  if (aichatBrowser.currentURI?.spec !== "about:aichatcontent") {
    await BrowserTestUtils.browserLoaded(
      aichatBrowser,
      false,
      "about:aichatcontent"
    );
  }

  Assert.equal(
    aichatBrowser.currentURI.spec,
    "about:aichatcontent",
    "aichat-browser should be loaded with about:aichatcontent"
  );

  return aichatBrowser;
}
