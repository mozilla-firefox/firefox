/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for conversation starter prompts in the AI Window.
 *
 * These tests verify that:
 * - Prompts are rendered correctly in both sidebar and fullpage modes
 * - Submitting starter prompts respects the memories preference
 * - Prompts are removed after selection
 */

"use strict";

// TODO (Bug 2090225): Split tasks into smaller files to reduce the timeout.
requestLongerTimeout(6);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
});

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const PROMPTS_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_prompts_page.html";

// Disable memories by default; relevant tests opt in explicitly.
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", false],
      ["browser.smartwindow.memories.generateFromHistory", false],
      ["places.semanticHistory.smartwindow.featureGate", false],
    ],
  });
});

/**
 * Creates a smartwindow-prompts element directly on a static test page, for
 * testing pure rendering behavior in isolation from the starter-generation
 * flow.
 *
 * @param {MozTabbrowserTab} tab
 * @param {Array<object>} prompts
 * @param {string} [mode]
 */
async function createPromptsElement(tab, prompts, mode = "fullpage") {
  const browser = tab.linkedBrowser;
  const contentWin = browser.contentWindow;
  const contentDoc = browser.contentDocument;

  await contentWin.customElements.whenDefined("smartwindow-prompts");
  const el = contentDoc.createElement("smartwindow-prompts");
  el.mode = mode;
  el.prompts = prompts;
  contentDoc.body.appendChild(el);
  await el.updateComplete;
  return el;
}

function getSidebarPromptButtons(win) {
  const sidebarBrowser = win.document.getElementById("ai-window-browser");
  const aiWindowEl =
    sidebarBrowser?.contentDocument?.querySelector("ai-window");
  const promptsEl = aiWindowEl?.shadowRoot?.querySelector(
    "smartwindow-prompts"
  );
  if (!promptsEl) {
    return [];
  }
  const buttons = promptsEl.shadowRoot.querySelectorAll(".sw-prompt-button");
  return Array.from(buttons).map(b => b.textContent.trim());
}

async function navigateTo(url, window) {
  const parentBrowser = window.gBrowser.selectedBrowser;
  const loaded = BrowserTestUtils.browserLoaded(parentBrowser);
  BrowserTestUtils.startLoadingURIString(parentBrowser, url);
  await loaded;
}

async function openBackgroundTab(url, window) {
  let tab = BrowserTestUtils.addTab(window.gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  return tab;
}

describe("sidebar conversation starter prompts", () => {
  let responseContent,
    gMockEngineManager,
    gStarterRequestCount,
    gStartersReleased,
    gAiWindow,
    backgroundTab;

  /**
   * Answers every starter request with responseContent as it reaches the
   * model, waiting for gStartersReleased first while that is set.
   *
   * @param {MockEngineManager} mockEngineManager
   */
  async function answerStarterRequests(mockEngineManager) {
    for (;;) {
      const { respond } = await mockEngineManager.captureRequest({
        purpose: "convo-starters-sidebar",
      });
      gStarterRequestCount++;
      await gStartersReleased;
      respond(responseContent[0]);
    }
  }

  beforeEach(async () => {
    responseContent = ["prompt 1\nprompt 2"];
    gStarterRequestCount = 0;
    gStartersReleased = null;
    gMockEngineManager = new MockEngineManager();
    answerStarterRequests(gMockEngineManager).catch(() => {});

    gAiWindow = await openAIWindow();
  });

  afterEach(async () => {
    if (backgroundTab) {
      BrowserTestUtils.removeTab(backgroundTab);
    }

    await BrowserTestUtils.closeWindow(gAiWindow);
    await gMockEngineManager.rejectAllRequests();
    gMockEngineManager.cleanupMocks();

    gAiWindow = null;
  });

  describe("when switching tabs while starter prompts load", () => {
    let firstTab, secondTab;
    afterEach(async () => {
      if (firstTab) {
        await BrowserTestUtils.removeTab(firstTab);
      }

      if (secondTab) {
        await BrowserTestUtils.removeTab(secondTab);
      }
    });

    it("should not load prompts to the wrong tab", async () => {
      // Trigger opening the sidebar so initial starter prompts display
      await navigateTo("https://example.com", gAiWindow);
      firstTab = gAiWindow.gBrowser.selectedTab;

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should be open"
      );
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
        "First set of prompts should be rendered"
      );

      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 1", "prompt 2"],
        "Should display first set of prompts"
      );

      // Open up second tab
      secondTab = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: gAiWindow.gBrowser,
        opening: "https://example.net",
        waitForLoad: true,
      });

      // Start a conversation so that starter prompts should not be displayed
      const sidebarBrowser =
        gAiWindow.document.getElementById("ai-window-browser");
      await typeInSmartbar(sidebarBrowser, "Hello world");
      await submitSmartbar(sidebarBrowser);
      await gMockEngineManager.respondTo({ purpose: "chat", response: "Hi" });
      await TestUtils.waitForCondition(
        () => !getSidebarPromptButtons(gAiWindow).length,
        "Starter prompts should be hidden after starting a conversation on tab 2"
      );

      // Navigate tab 1 in the background so switching back to it triggers
      // an uncached starter request instead of reusing the starters cached
      // for the initial example.com load.
      const firstTabLoaded = BrowserTestUtils.browserLoaded(
        firstTab.linkedBrowser
      );
      BrowserTestUtils.startLoadingURIString(
        firstTab.linkedBrowser,
        "https://example.org"
      );
      await firstTabLoaded;

      // wait for conversation messages
      await TestUtils.waitForCondition(() => {
        const el = sidebarBrowser?.contentDocument?.querySelector("ai-window");
        return el && el.conversationMessageCount > 0;
      }, "Tab 2's conversation should have messages after submit");

      // Hold the starter prompts mocked response
      const { promise: startersReleased, resolve: releaseStarters } =
        Promise.withResolvers();
      gStartersReleased = startersReleased;
      const requestCountBeforeSwitch = gStarterRequestCount;

      // Switch back to tab 1 to trigger loading starter prompts, and before
      // they are answered switch back to tab 2 where starter prompts should
      // not display
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, firstTab);
      await TestUtils.waitForCondition(
        () => gStarterRequestCount > requestCountBeforeSwitch,
        "Switching to tab 1 should request starter prompts"
      );
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, secondTab);

      gStartersReleased = null;
      releaseStarters();

      // Wait for conversation to start and starters to hide
      const sidebarBrowser2 =
        gAiWindow.document.getElementById("ai-window-browser");
      await TestUtils.waitForCondition(() => {
        const aiWindowEl =
          sidebarBrowser2?.contentDocument?.querySelector("ai-window");
        return (
          aiWindowEl &&
          aiWindowEl.conversationMessageCount > 0 &&
          !aiWindowEl.showStarters
        );
      }, "Sidebar should have settled on tab 2's conversation with starters hidden");

      // Verify that starter prompts aren't being displayed on tab 2
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        [],
        "Starter prompts from tab 1 should not appear on tab 2"
      );
    });
  });

  it("should cancel the latest starter load after an older load finishes", async () => {
    await navigateTo("https://example.com", gAiWindow);
    await TestUtils.waitForCondition(
      () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
      "Initial starter prompts should be rendered"
    );

    const sidebarBrowser =
      gAiWindow.document.getElementById("ai-window-browser");
    const aiWindow = sidebarBrowser.contentDocument.querySelector("ai-window");
    const tab = gAiWindow.gBrowser.selectedTab;
    const sandbox = sinon.createSandbox();
    const loads = Array.from({ length: 3 }, () => ({
      entered: Promise.withResolvers(),
      release: Promise.withResolvers(),
    }));
    const generationStarted = Promise.withResolvers();
    const releaseGeneration = Promise.withResolvers();
    const formatValues = aiWindow.ownerDocument.l10n.formatValues.bind(
      aiWindow.ownerDocument.l10n
    );
    const loadSystemPrompt = aiWindow.conversation.loadSystemPrompt.bind(
      aiWindow.conversation
    );
    let loadIndex = 0;
    let olderLoad, newerLoad;

    sandbox
      .stub(aiWindow.ownerDocument.l10n, "formatValues")
      .callsFake(async (...args) => {
        const load = loads[loadIndex++];
        const values = await formatValues(...args);
        load.entered.resolve();
        await load.release.promise;
        return values;
      });
    sandbox
      .stub(aiWindow.conversation, "loadSystemPrompt")
      .callsFake(async (...args) => {
        generationStarted.resolve();
        await releaseGeneration.promise;
        return loadSystemPrompt(...args);
      });

    try {
      aiWindow.conversation.transientStarterUrl = null;
      olderLoad = aiWindow.loadStarterPrompts(true, tab);
      await loads[0].entered.promise;

      aiWindow.conversation.transientStarterUrl = null;
      newerLoad = aiWindow.loadStarterPrompts(true, tab);
      await loads[1].entered.promise;

      loads[0].release.resolve();
      await olderLoad;

      await typeInSmartbar(sidebarBrowser, "Hello world");
      await submitSmartbar(sidebarBrowser);
      await generationStarted.promise;
      Assert.equal(
        aiWindow.conversationMessageCount,
        0,
        "Chat initialization should still be waiting for its system prompt"
      );

      Assert.ok(aiWindow.isGenerating, "Chat generation is in progress");
      loads[2].release.resolve();
      aiWindow.conversation.transientStarterUrl = null;
      await aiWindow.loadStarterPrompts(true, tab);
      Assert.equal(
        loadIndex,
        2,
        "No new starter load begins while chat initialization is in progress"
      );

      loads[1].release.resolve();
      await newerLoad;
      await aiWindow.updateComplete;
      Assert.ok(
        !aiWindow.showStarters,
        "The latest starter load should remain canceled after chat submission"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        [],
        "Canceled starter prompts should not reappear before messages arrive"
      );

      releaseGeneration.resolve();
      await gMockEngineManager.respondTo({ purpose: "chat", response: "Hi" });
    } finally {
      for (const load of loads) {
        load.release.resolve();
      }
      releaseGeneration.resolve();
      await Promise.allSettled([olderLoad, newerLoad]);
      sandbox.restore();
      await TestUtils.waitForCondition(
        () => !aiWindow.isGenerating,
        "Chat generation should finish before closing the window"
      );
    }
  });

  describe("when the conversation is empty", () => {
    it("should load new prompts when the tab changes URL", async () => {
      await navigateTo("https://example.com", gAiWindow);

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should be open"
      );
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
        "First set of prompts should be rendered"
      );

      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 1", "prompt 2"],
        "Should display first set of prompts"
      );

      responseContent[0] = "prompt 3\nprompt 4";

      await navigateTo("https://example.org", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 3"),
        "Second set of prompts should be rendered"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 3", "prompt 4"],
        "Should display updated prompts after URL change"
      );
    });

    it("should not reload prompts when background tabs change URL", async () => {
      const updateNewChatContentSpy = sinon.spy(
        lazy.AIWindowUI,
        "updateNewChatContent"
      );

      try {
        await navigateTo("https://example.com", gAiWindow);

        await TestUtils.waitForCondition(
          () => AIWindowUI.isSidebarOpen(gAiWindow),
          "Sidebar should be open"
        );
        await TestUtils.waitForCondition(
          () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
          "First set of prompts should be rendered"
        );

        Assert.deepEqual(
          getSidebarPromptButtons(gAiWindow),
          ["prompt 1", "prompt 2"],
          "Should display first set of prompts"
        );

        responseContent[0] = "prompt 3\nprompt 4";
        updateNewChatContentSpy.resetHistory();

        backgroundTab = await openBackgroundTab(
          "https://example.org",
          gAiWindow
        );

        Assert.deepEqual(
          getSidebarPromptButtons(gAiWindow),
          ["prompt 1", "prompt 2"],
          "Should continue to display initial starter prompts after background URL load"
        );
        Assert.equal(
          0,
          updateNewChatContentSpy.callCount,
          "There should not be any more calls to update starter prompts"
        );
      } finally {
        updateNewChatContentSpy.restore();
      }
    });

    it("should load new prompts when navigating back to a previously visited URI", async () => {
      await navigateTo("https://example.com", gAiWindow);

      await TestUtils.waitForCondition(
        () => AIWindowUI.isSidebarOpen(gAiWindow),
        "Sidebar should be open"
      );
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 1"),
        "First set of prompts should be rendered"
      );

      const requestCountAfterFirstLoad = gStarterRequestCount;

      responseContent[0] = "prompt 3\nprompt 4";
      await navigateTo("https://example.org", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 3"),
        "Second set of prompts should be rendered"
      );
      Assert.equal(
        gStarterRequestCount,
        requestCountAfterFirstLoad + 1,
        "Navigating to a new URI should generate a new starter request"
      );

      responseContent[0] = "prompt 5\nprompt 6";
      await navigateTo("https://example.com", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 5"),
        "New prompts should be rendered when navigating back"
      );
      Assert.deepEqual(
        getSidebarPromptButtons(gAiWindow),
        ["prompt 5", "prompt 6"],
        "Should display newly generated prompts when navigating back"
      );
      Assert.equal(
        gStarterRequestCount,
        requestCountAfterFirstLoad + 2,
        "Navigating back to a previously visited URI should generate a new starter request"
      );
    });

    it("should evict the oldest cached prompts after exceeding the cache limit", async () => {
      for (let i = 0; i <= 20; i++) {
        responseContent[0] = `prompt ${i}a\nprompt ${i}b`;
        await navigateTo(`https://example.com/${i}`, gAiWindow);
        await TestUtils.waitForCondition(
          () => getSidebarPromptButtons(gAiWindow).includes(`prompt ${i}a`),
          `Prompts for URI ${i} should be rendered`
        );
      }

      const requestCountAfterFillingCache = gStarterRequestCount;

      responseContent[0] = "prompt evicted a\nprompt evicted b";
      await navigateTo("https://example.com/0", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt evicted a"),
        "Evicted prompts should be regenerated for the oldest URI"
      );

      Assert.equal(
        gStarterRequestCount,
        requestCountAfterFillingCache + 1,
        "Revisiting the oldest URI after the cache limit should generate a new starter request"
      );
    });
  });
});

add_task(async function test_starters_render_while_resume_cards_pending() {
  const sb = sinon.createSandbox();
  let win;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", true],
      ["browser.smartwindow.memories.generateFromHistory", true],
    ],
  });

  try {
    // The resume gate refreshes memories first. Leave that pending so the
    // cards can't load; the starters must not wait on it.
    const getAllMemoriesStub = sb
      .stub(MemoriesManager, "getAllMemories")
      .returns(new Promise(() => {}));
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const buttons = await getPromptButtons(browser);
    const aiWindow = browser.contentDocument.querySelector("ai-window");

    await TestUtils.waitForCondition(
      () => getAllMemoriesStub.called,
      "Wait for the resume gate to start refreshing memories"
    );
    Assert.equal(
      buttons.length,
      3,
      "Starters should render while the resume cards are pending"
    );
    Assert.deepEqual(aiWindow.resumeCards, [], "No resume cards have loaded");
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_starter_prompts_click_triggers_chat_on_new_tab() {
  const sb = sinon.createSandbox();

  try {
    const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory");
    sb.stub(openAIEngine, "build").resolves({});

    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const buttons = await getPromptButtons(browser);
    const firstPromptText = buttons[0].ariaLabel;
    buttons[0].click();

    await TestUtils.waitForCondition(
      () => fetchWithHistoryStub.calledOnce,
      "fetchWithHistory should be called after clicking prompt"
    );

    const conversation = fetchWithHistoryStub.firstCall.args[0].conversation;
    const messages = conversation.getMessagesInChatCompletionsFormat();
    const userMessage = messages.findLast(m => m.role === "user");

    Assert.equal(
      userMessage.content,
      firstPromptText,
      "Should submit starter prompt text as user message on New Tab"
    );

    await BrowserTestUtils.closeWindow(win);
  } finally {
    sb.restore();
  }
});

add_task(
  async function test_starter_prompts_click_fetches_memories_when_enabled() {
    const sb = sinon.createSandbox();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    try {
      sb.stub(Chat, "fetchWithHistory");
      sb.stub(openAIEngine, "build").resolves({});
      const memoriesStub = sb
        .stub(this.ChatConversation.prototype, "injectMemoriesContext")
        .resolves(null);

      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      (await getPromptButtons(browser))[0].click();

      await TestUtils.waitForCondition(
        () => memoriesStub.called,
        "injectMemoriesContext should be called with memories enabled"
      );

      Assert.ok(
        memoriesStub.calledOnce,
        "injectMemoriesContext should be called once"
      );

      await BrowserTestUtils.closeWindow(win);
    } finally {
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(
  async function test_starter_prompts_click_skips_memories_when_disabled() {
    const sb = sinon.createSandbox();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", false],
        ["browser.smartwindow.memories.generateFromHistory", false],
      ],
    });

    try {
      const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory");
      sb.stub(openAIEngine, "build").resolves({});
      const memoriesStub = sb
        .stub(this.ChatConversation.prototype, "injectMemoriesContext")
        .resolves(null);

      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      (await getPromptButtons(browser))[0].click();

      await TestUtils.waitForCondition(
        () => fetchWithHistoryStub.calledOnce,
        "fetchWithHistory should be called after clicking prompt"
      );

      Assert.ok(
        memoriesStub.notCalled,
        "injectMemoriesContext should not be called when memories are disabled"
      );

      await BrowserTestUtils.closeWindow(win);
    } finally {
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_starter_prompts_hidden_after_click_on_new_tab() {
  const sb = sinon.createSandbox();

  try {
    sb.stub(Chat, "fetchWithHistory");
    sb.stub(openAIEngine, "build").resolves({});

    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    (await getPromptButtons(browser))[0].click();

    await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowElement = content.document.querySelector("ai-window");
      await ContentTaskUtils.waitForMutationCondition(
        aiWindowElement.shadowRoot,
        { childList: true, subtree: true },
        () => !aiWindowElement.shadowRoot.querySelector("smartwindow-prompts")
      );
    });

    await BrowserTestUtils.closeWindow(win);
  } finally {
    sb.restore();
  }
});

// Confirms a resume pill's click event carries memory/content while a chat
// pill's stays plain {text, type}. Clicks both in one render purely to check
// each shape - not a simulated user session.
add_task(async function test_prompt_selected_event_detail() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const memory = { id: "memory-1", memory_summary: "Research project" };
    const content = {
      headline: "Pick up your research",
      status: "Continue reading",
      previewTabs: [
        { url: "https://example.com/1", title: "First tab" },
        { url: "https://example.com/2", title: "Second tab" },
      ],
    };
    const el = await createPromptsElement(tab, [
      {
        text: "Pick up your research",
        type: "resume",
        memory,
        content,
      },
      { text: "Write a first draft", type: "chat" },
    ]);

    const details = [];
    el.addEventListener("SmartWindowPrompt:prompt-selected", event => {
      details.push(event.detail);
    });

    const buttons = el.shadowRoot.querySelectorAll(".sw-prompt-button");
    buttons[0].click();
    buttons[1].click();

    Assert.deepEqual(
      details[0],
      {
        text: "Pick up your research",
        type: "resume",
        memory,
        content,
      },
      "Resume prompt events should include their memory and content payload"
    );
    Assert.deepEqual(
      details[1],
      { text: "Write a first draft", type: "chat" },
      "Chat prompt event detail should remain unchanged"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

// Favicon cluster rendering. Tested in isolation on a static page rather
// than the full AI Window, since this is pure rendering behavior with no
// starter-generation or chat involved.

add_task(async function test_prompts_no_favicons_without_preview_icons() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, [
      { text: "Write a first draft", type: "chat" },
    ]);
    Assert.ok(
      !el.shadowRoot.querySelector(".sw-prompt-favicons"),
      "No favicon cluster should render without previewIcons"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(
  async function test_prompts_favicon_cluster_renders_all_icons_up_to_max() {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      PROMPTS_PAGE
    );
    try {
      const el = await createPromptsElement(tab, [
        {
          text: "Pick up your Tokyo trip",
          type: "resume",
          previewIcons: [
            { iconSrc: "page-icon:https://example.com/1" },
            { iconSrc: "page-icon:https://example.com/2" },
          ],
        },
      ]);
      Assert.equal(
        el.shadowRoot.querySelectorAll(".sw-prompt-favicon").length,
        2,
        "Should render one icon per previewIcons entry when under the max"
      );
      Assert.ok(
        !el.shadowRoot.querySelector(".sw-prompt-favicon-overflow"),
        "No overflow badge should render when under the max"
      );
    } finally {
      BrowserTestUtils.removeTab(tab);
    }
  }
);

add_task(
  async function test_prompts_favicon_cluster_shows_overflow_badge_beyond_max() {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      PROMPTS_PAGE
    );
    try {
      const el = await createPromptsElement(tab, [
        {
          text: "Continue shopping",
          type: "resume",
          previewIcons: [
            { iconSrc: "page-icon:https://example.com/1" },
            { iconSrc: "page-icon:https://example.com/2" },
            { iconSrc: "page-icon:https://example.com/3" },
            { iconSrc: "page-icon:https://example.com/4" },
          ],
        },
      ]);
      Assert.equal(
        el.shadowRoot.querySelectorAll(".sw-prompt-favicon").length,
        2,
        "Should cap visible icons when previewIcons exceeds the max"
      );
      const overflow = el.shadowRoot.querySelector(
        ".sw-prompt-favicon-overflow"
      );
      Assert.ok(
        overflow,
        "Overflow badge should render when previewIcons exceeds the max"
      );
      Assert.equal(
        overflow.textContent,
        "+2",
        "Overflow badge should show the count of hidden icons"
      );
    } finally {
      BrowserTestUtils.removeTab(tab);
    }
  }
);

function buildManyPrompts(count = 10) {
  return Array.from({ length: count }, (_, i) => ({
    text: `Overflow test prompt number ${i}`,
    type: "chat",
  }));
}

/**
 * Shrinks the element to a fixed width narrow enough to force overflow,
 * and waits for the resulting (ResizeObserver-driven, async) scroll-state
 * update, rather than asserting immediately after the style change.
 *
 * @param {HTMLElement} el
 */
async function forceOverflowByShrinking(el) {
  el.style.cssText = "display: block; width: 250px;";
  await TestUtils.waitForCondition(
    () => el.canScrollEnd,
    "Wait for the resize observer to detect overflow"
  );
}

add_task(async function test_prompts_no_scroll_when_content_fits() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, [
      { text: "Write a first draft", type: "chat" },
      { text: "Brainstorm ideas", type: "chat" },
    ]);
    Assert.ok(
      !el.shadowRoot.querySelector(".sw-prompts-scroll-button"),
      "No scroll arrows should render when content fits"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_prompts_shows_end_arrow_when_overflowing() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, buildManyPrompts());
    await forceOverflowByShrinking(el);

    Assert.ok(
      el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
      "End arrow should render"
    );
    Assert.ok(
      !el.shadowRoot.querySelector(".sw-prompts-scroll-start"),
      "Start arrow should not render at the very start"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(
  async function test_prompts_scroll_end_arrow_advances_and_reveals_start_arrow() {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      PROMPTS_PAGE
    );
    // Reduced motion turns off scroll-behavior: smooth, so each click
    // settles immediately instead of racing an in-flight animation.
    await SpecialPowers.pushPrefEnv({
      set: [["ui.prefersReducedMotion", 1]],
    });
    try {
      const el = await createPromptsElement(tab, buildManyPrompts());
      await forceOverflowByShrinking(el);

      el.shadowRoot.querySelector(".sw-prompts-scroll-end").click();
      await TestUtils.waitForCondition(
        () => el.canScrollStart,
        "Wait for the scroll to advance past the first pill"
      );

      Assert.ok(
        el.shadowRoot.querySelector(".sw-prompts-scroll-start"),
        "Start arrow should render once scrolled forward"
      );
    } finally {
      BrowserTestUtils.removeTab(tab);
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(
  async function test_prompts_scroll_stops_at_last_pill_without_wrapping() {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      PROMPTS_PAGE
    );
    await SpecialPowers.pushPrefEnv({
      set: [["ui.prefersReducedMotion", 1]],
    });
    try {
      const prompts = buildManyPrompts();
      const el = await createPromptsElement(tab, prompts);
      await forceOverflowByShrinking(el);

      // More clicks than there are pills - should hold at the last pill
      // rather than wrapping back around to the first.
      for (let i = 0; i < prompts.length + 2; i++) {
        const endButton = el.shadowRoot.querySelector(".sw-prompts-scroll-end");
        if (!endButton) {
          break;
        }
        endButton.click();
        await TestUtils.waitForTick();
      }

      await TestUtils.waitForCondition(
        () => !el.canScrollEnd,
        "Wait for the last pill to settle into view"
      );
      Assert.ok(
        el.canScrollStart,
        "Should still be able to scroll back from the end"
      );
    } finally {
      BrowserTestUtils.removeTab(tab);
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_prompts_resize_updates_scroll_state() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, buildManyPrompts());
    await forceOverflowByShrinking(el);

    el.style.width = "10000px";
    await TestUtils.waitForCondition(
      () => !el.canScrollEnd,
      "Wait for the resize observer to detect the content now fits"
    );
    Assert.ok(
      !el.shadowRoot.querySelector(".sw-prompts-scroll-button"),
      "No scroll arrows should render once widened to fit"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_prompts_resize_observer_reattaches_after_empty() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, buildManyPrompts());
    const win = tab.linkedBrowser.contentWindow;

    // render() omits the container while prompts is empty, detaching it.
    // Let the resulting disconnect-triggered resize notification settle
    // before continuing, so it can't mask a stale observer later.
    el.prompts = [];
    await el.updateComplete;
    await new Promise(resolve =>
      win.requestAnimationFrame(() => win.requestAnimationFrame(resolve))
    );

    // A new container is created here; the observer must re-attach to it.
    el.prompts = buildManyPrompts();
    await el.updateComplete;
    Assert.ok(
      !el.canScrollEnd,
      "Sanity check: the new container isn't overflowing yet"
    );

    await forceOverflowByShrinking(el);
    Assert.ok(
      el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
      "Resize observer should still detect overflow on the recreated container"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_prompts_rtl_overflow_scroll_state() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROMPTS_PAGE
  );
  try {
    const el = await createPromptsElement(tab, buildManyPrompts());
    el.setAttribute("dir", "rtl");
    await forceOverflowByShrinking(el);

    Assert.ok(
      el.shadowRoot.querySelector(".sw-prompts-scroll-end"),
      "End arrow should render in RTL too"
    );

    el.shadowRoot.querySelector(".sw-prompts-scroll-end").click();
    await TestUtils.waitForCondition(
      () => el.canScrollStart,
      "Wait for the scroll to advance past the first pill in RTL"
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
  }
});
