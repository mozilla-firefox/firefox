/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

const AITAB_GENERATION_RECORDS = [
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

function setupAITabGenerationMocks(context) {
  const { MockEngineManager } = ChromeUtils.importESModule(
    "resource://testing-common/AIWindowTestUtils.sys.mjs"
  );
  const { RemoteSettings } = ChromeUtils.importESModule(
    "resource://services-settings/remote-settings.sys.mjs"
  );

  context.sandbox = sinon.createSandbox();
  context.sandbox.stub(RemoteSettings("ai-window-aitab"), "get").resolves([]);
  _setRemoteClientForTesting({
    get: context.sandbox
      .stub()
      .resolves([...MOCK_RS_RECORDS, ...AITAB_GENERATION_RECORDS]),
  });
  context.restoreSignIn = skipSignIn();
  context.mockEngine = new MockEngineManager();
}

async function openAITabGenerationChat(context) {
  context.win = await openAIWindow();
  context.chatTab = context.win.gBrowser.selectedTab;
}

async function cleanupAITabGeneration(context) {
  try {
    await context.mockEngine?.rejectAllRequests();
    if (context.win && !context.win.closed) {
      await BrowserTestUtils.closeWindow(context.win);
    }
  } finally {
    context.mockEngine?.cleanupMocks();
    context.restoreSignIn?.();
    context.sandbox?.restore();
    _setRemoteClientForTesting({ get: async () => MOCK_RS_RECORDS });
  }
}

function makeAITabSurface(title) {
  return {
    components: [
      { id: "root", component: "Page", header: "header", children: [] },
      { id: "header", component: "Header", title },
    ],
    dataModel: {},
  };
}

async function waitForAITabReadyCard(browser, title) {
  const chatBrowser = await getAichatBrowser(browser);
  return TestUtils.waitForCondition(async () => {
    return SpecialPowers.spawn(chatBrowser, [title], async expectedTitle => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const card = Array.from(
        chatContent?.shadowRoot.querySelectorAll("aitab-tool-ui") ?? []
      ).at(-1);
      if (!card) {
        return null;
      }

      await card.wrappedJSObject.updateComplete;
      if (
        card.wrappedJSObject.state !== "choose" ||
        card.wrappedJSObject.title !== expectedTitle
      ) {
        return null;
      }
      return card.wrappedJSObject.viewerURL;
    });
  }, `The generated page card shows "${title}"`);
}

async function waitForAITabPromptComplete(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = content.document.querySelector("ai-window");
    const smartbar = aiWindow.shadowRoot.querySelector("#ai-window-smartbar");
    const inputCta = smartbar.querySelector("input-cta");
    await ContentTaskUtils.waitForCondition(
      () => inputCta.getAttribute("action") !== "stop",
      "The smartbar finishes processing the prompt"
    );
  });
}

async function submitAITabPrompt(context, prompt, toolParams, title) {
  const browser = context.chatTab.linkedBrowser;
  await typeInSmartbar(browser, prompt);
  await submitSmartbar(browser);
  await context.mockEngine.respondToWithToolCall({
    toolName: "generate_page",
    toolParams,
  });
  await context.mockEngine.respondTo({
    purpose: "aitab",
    response: JSON.stringify(makeAITabSurface(title)),
  });
  if (toolParams.modify_slug) {
    await context.mockEngine.respondTo({
      purpose: "chat",
      response: "The page has been updated.",
    });
  }
  await waitForAITabPromptComplete(browser);
}

async function createTestAITab(context, title) {
  await submitAITabPrompt(
    context,
    `Create a page about Lisbon hotels with the heading "${title}".`,
    {
      raw_content: "Compare hotels in Lisbon for a weekend visit.",
      focus: title,
    },
    title
  );
  await context.mockEngine.respondTo({
    purpose: "title-generation",
    response: "Lisbon hotels",
  });
  return waitForAITabReadyCard(context.chatTab.linkedBrowser, title);
}

async function reviseTestAITab(context, viewerURL, title) {
  await submitAITabPrompt(
    context,
    `Change the page heading to "${title}".`,
    {
      modify_slug: new URL(viewerURL).searchParams.get("page"),
      modify_instructions: `Change the heading to "${title}".`,
    },
    title
  );
}

async function openGeneratedAITab(context, viewerURL) {
  const opened = BrowserTestUtils.waitForNewTab(
    context.win.gBrowser,
    viewerURL,
    true
  );
  const chatBrowser = await getAichatBrowser(context.chatTab.linkedBrowser);
  await SpecialPowers.spawn(chatBrowser, [], () => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const card = Array.from(
      chatContent.shadowRoot.querySelectorAll("aitab-tool-ui")
    ).at(-1);
    card.shadowRoot
      .querySelector('[data-l10n-id="smartwindow-aitab-open-new"]')
      .click();
  });
  return opened;
}

async function selectAITabTestTab(tab) {
  const win = tab.documentGlobal;
  tab.scrollIntoView({ behavior: "instant" });
  await BrowserTestUtils.switchTab(win.gBrowser, () => {
    EventUtils.synthesizeMouseAtCenter(tab, {}, win);
  });
  Assert.equal(win.gBrowser.selectedTab, tab, "The clicked tab becomes active");
}

async function waitForAITabHeading(tab, title) {
  await SpecialPowers.spawn(tab.linkedBrowser, [title], async expectedTitle => {
    await ContentTaskUtils.waitForCondition(() => {
      const page = content.document.querySelector("aitab-page");
      const header = page?.shadowRoot?.querySelector("aitab-header");
      return (
        header?.shadowRoot?.querySelector("h1")?.textContent === expectedTitle
      );
    }, `The page renders the heading "${expectedTitle}"`);
  });
}

const PAGE_CONFIG = {
  component: "Page",
  children: [
    {
      id: "hdr",
      component: "Header",
      eyebrow: "From your open tabs",
      title: "Hotels in Lisbon",
      subhead: "4 options gathered from your open tabs",
    },
    {
      id: "note",
      component: "TextBlock",
      lead: "What you are comparing",
      paragraphs: [],
    },
    { id: "plan", component: "Timeline", title: "Booking plan", items: [] },
    { id: "unknown", component: "NotAComponent", title: "Unknown component" },
    { id: "untyped", title: "A block with no component" },
  ],
};

/**
 * Returns the conversation associated with the tab's ai-window element,
 * or undefined if unavailable.
 *
 * @param {MozTabbrowserTab} tab - Tab hosting the chat.
 * @returns {ChatConversation|undefined} The tab's conversation, if available.
 */
function getTabConversation(tab) {
  return tab.linkedBrowser.contentDocument?.querySelector("ai-window")
    ?.conversation;
}

/**
 * Activates the trigger and resolves with the tab it opened once that tab's
 * chat carries the user turn.
 *
 * @param {Window} win - Browser window containing the trigger.
 * @param {() => (void|Promise<void>)} activate - Activates the UI trigger.
 * @param {object} [options] - Assertions for the existing chat.
 * @param {boolean} [options.expectCurrentChatUntouched=true] - Whether to
 *   assert that the current chat has no messages.
 * @returns {Promise<MozTabbrowserTab>} The new background chat tab.
 */
async function activateAndWaitForAITab(
  win,
  activate,
  { expectCurrentChatUntouched = true } = {}
) {
  const { MESSAGE_ROLE } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
  );
  const selectedTab = win.gBrowser.selectedTab;
  const tabOpened = BrowserTestUtils.waitForEvent(
    win.gBrowser.tabContainer,
    "TabOpen"
  );
  await activate();
  const tab = (await tabOpened).target;

  let conversation;
  try {
    // Building the engine and system prompt comes before the user turn, which
    // can outlast the default timeout on a slow run.
    await TestUtils.waitForCondition(
      () => {
        conversation = getTabConversation(tab);
        return conversation?.messages.some(m => m.role === MESSAGE_ROLE.USER);
      },
      "Wait for the new tab's chat to submit its conversation",
      100,
      150
    );
  } catch (e) {
    throw new Error(
      `${e.message ?? e} | url=${tab.linkedBrowser.currentURI.spec}` +
        ` conversation=${conversation?.id} messages=${conversation?.messageCount}`
    );
  }

  Assert.equal(
    tab.linkedBrowser.currentURI.spec,
    AIWINDOW_URL,
    "The new tab is a full page chat"
  );
  Assert.equal(
    win.gBrowser.selectedTab,
    selectedTab,
    "The new tab opened in the background"
  );
  if (expectCurrentChatUntouched) {
    Assert.ok(
      !AIWindow.getActiveConversation(win)?.messageCount,
      "The current tab's chat is left alone"
    );
  }
  return tab;
}

/**
 * Checks that the user message contains the localized AITab creation prompt
 * followed by the expected URLs in the given order, then answers the pending
 * model request so nothing is left dangling.
 *
 * @param {MozTabbrowserTab} tab - Tab hosting the submitted conversation.
 * @param {string[]} urls - Expected context URLs, in prompt order.
 * @param {MockEngineManager} mockEngineManager - Mock handling the chat request.
 * @returns {Promise<void>}
 */
async function assertAITabCreationPrompt(tab, urls, mockEngineManager) {
  const { MESSAGE_ROLE } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
  );
  const l10n = new Localization(["preview/aiWindow.ftl"]);
  const userMessages = getTabConversation(tab).messages.filter(
    m => m.role === MESSAGE_ROLE.USER
  );
  Assert.equal(userMessages.length, 1, "The conversation has one user turn");
  const prompt = await l10n.formatValue("ai-tab-create-page-prompt", {
    tabCount: urls.length,
  });
  Assert.equal(
    userMessages[0].content.body,
    [prompt, ...urls].join("\n"),
    "The user turn is the localized prompt followed by the tab URLs"
  );
  Assert.ok(
    !userMessages[0].content.contextPageUrl,
    "The user turn carries no page context besides the listed URLs"
  );
  const { request, respond } = await mockEngineManager.captureRequest({
    purpose: "chat",
  });
  Assert.ok(request, "The submitted conversation requested a model response");
  respond("Mock AI Tab reply.");
}

/**
 * Opens a tab's context menu with a right-click and waits until it is shown.
 *
 * @param {Window} win - Browser window containing the tab.
 * @param {MozTabbrowserTab} tab - Tab whose context menu to open.
 * @returns {Promise<XULPopupElement>} The open tab context menu.
 */
async function openTabContextMenu(win, tab) {
  tab.scrollIntoView({ behavior: "instant" });
  const menu = win.document.getElementById("tabContextMenu");
  const shown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu", button: 2 },
    win
  );
  await shown;
  return menu;
}

/**
 * Runs a menu item's action and waits for its popup to close.
 *
 * @param {XULPopupElement} menu - Open popup containing the item.
 * @param {Element} item - Menu item whose action to run.
 * @returns {Promise<void>}
 */
async function activateItemAndWait(menu, item) {
  const hidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.activateItem(item);
  await hidden;
}
