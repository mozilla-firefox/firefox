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

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  MESSAGE_ROLE:
    "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs",
});

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const PROMPTS_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_prompts_page.html";

// Disable memories by default; relevant tests opt in explicitly.
// Resume pills only show when resumeCards is false.
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", false],
      ["browser.smartwindow.memories.generateFromHistory", false],
      ["browser.smartwindow.resumeCards.enabled", false],
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

function startMockNonStreamingServer(responseContent) {
  const mockServer = new HttpServer();
  mockServer.delay = 1;
  let reqCount = 0;
  const pendingResponses = [];

  mockServer.registerPathHandler("/v1/chat/completions", (_req, res) => {
    reqCount++;
    res.processAsync();

    const body = JSON.stringify({
      id: "chatcmpl-mock",
      object: "chat.completion",
      created: Math.floor(Date.now() / 1000),
      model: "mock",
      choices: [
        {
          index: 0,
          message: { role: "assistant", content: responseContent[0] },
          finish_reason: "stop",
        },
      ],
    });

    const entry = { timerId: null, res };
    pendingResponses.push(entry);

    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    entry.timerId = setTimeout(() => {
      const idx = pendingResponses.indexOf(entry);
      if (idx !== -1) {
        pendingResponses.splice(idx, 1);
      }
      try {
        res.setStatusLine(_req.httpVersion, 200, "OK");
        res.setHeader("Content-Type", "application/json", false);
        res.write(body);
        res.finish();
      } catch (e) {
        // Connection may have closed before the delayed response was sent
        // or connection already torn down.
      }
    }, mockServer.delay);
  });

  mockServer.start(-1);
  return {
    pendingResponses,
    server: mockServer,
    port: mockServer.identity.primaryPort,
    get requestCount() {
      return reqCount;
    },
    cleanup() {
      for (const { timerId, res } of pendingResponses) {
        clearTimeout(timerId);
        try {
          res.finish();
        } catch (e) {
          // Already finished or connection gone.
        }
      }
      pendingResponses.length = 0;
    },
  };
}

describe("sidebar conversation starter prompts", () => {
  let responseContent, mock, gAiWindow, backgroundTab;

  beforeEach(async () => {
    responseContent = ["prompt 1\nprompt 2"];
    mock = startMockNonStreamingServer(responseContent);

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.endpoint", `http://localhost:${mock.port}/v1`],
      ],
    });

    gAiWindow = await openAIWindow();
  });

  afterEach(async () => {
    mock.cleanup();
    if (backgroundTab) {
      BrowserTestUtils.removeTab(backgroundTab);
    }

    await BrowserTestUtils.closeWindow(gAiWindow);
    await SpecialPowers.popPrefEnv();
    await stopMockOpenAI(mock.server);

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
        "https://example.com"
      );
      await firstTabLoaded;

      // wait for conversation messages
      await TestUtils.waitForCondition(() => {
        const el = sidebarBrowser?.contentDocument?.querySelector("ai-window");
        return el && el.conversationMessageCount > 0;
      }, "Tab 2's conversation should have messages after submit");

      // Add an artificial delay on the starter prompts mocked response
      mock.server.delay = 200;

      // Switch back to tab 1 to trigger loading starter prompts, and before
      // the 200 ms delay switch back to tab 2 where starter prompts should
      // not display
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, firstTab);
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(resolve => gAiWindow.setTimeout(resolve, 10));
      await BrowserTestUtils.switchTab(gAiWindow.gBrowser, secondTab);

      await TestUtils.waitForCondition(
        () => mock.pendingResponses.length === 0,
        "Waiting for all starter prompt requests/etc to resolve"
      );

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
      const updateStarterPromptsSpy = sinon.spy(
        lazy.AIWindowUI,
        "updateStarterPrompts"
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
        updateStarterPromptsSpy.resetHistory();

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
          updateStarterPromptsSpy.callCount,
          "There should not be any more calls to update starter prompts"
        );
      } finally {
        updateStarterPromptsSpy.restore();
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

      const requestCountAfterFirstLoad = mock.requestCount;

      responseContent[0] = "prompt 3\nprompt 4";
      await navigateTo("https://example.org", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt 3"),
        "Second set of prompts should be rendered"
      );
      Assert.equal(
        mock.requestCount,
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
        mock.requestCount,
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

      const requestCountAfterFillingCache = mock.requestCount;

      responseContent[0] = "prompt evicted a\nprompt evicted b";
      await navigateTo("https://example.com/0", gAiWindow);
      await TestUtils.waitForCondition(
        () => getSidebarPromptButtons(gAiWindow).includes("prompt evicted a"),
        "Evicted prompts should be regenerated for the oldest URI"
      );

      Assert.equal(
        mock.requestCount,
        requestCountAfterFillingCache + 1,
        "Revisiting the oldest URI after the cache limit should generate a new starter request"
      );
    });
  });
});

add_task(async function test_fullpage_resume_starters() {
  const sb = sinon.createSandbox();
  let win;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", true],
      ["browser.smartwindow.memories.generateFromHistory", true],
    ],
  });

  let resumeActivityStubs;
  try {
    resumeActivityStubs = await stubResumeActivityGeneration(sb);
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const buttons = await getPromptButtons(browser);
    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const promptsEl = aiWindow.shadowRoot.querySelector("smartwindow-prompts");

    // One valid resume pill plus three static starters.
    Assert.equal(buttons.length, 4, "Resume pill plus static starters");
    Assert.equal(
      buttons[0].ariaLabel,
      "Pick up your research",
      "The resume pill should be prepended to static starters"
    );
    Assert.equal(
      promptsEl.prompts.filter(prompt => prompt.type === "resume").length,
      1,
      "Entries with empty generated headlines should be omitted"
    );
    Assert.deepEqual(
      {
        previewIcons: promptsEl.prompts[0].previewIcons,
        previewTabsLength: promptsEl.prompts[0].content.previewTabs.length,
        memoryId: promptsEl.prompts[0].memory.id,
      },
      {
        previewIcons: [1, 2, 3, 4].map(id => ({
          iconSrc: `page-icon:https://example.com/${id}`,
        })),
        previewTabsLength: 4,
        memoryId: "memory-1",
      },
      "The resume pill should retain favicons, tab payload, and memory id"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
    await resumeActivityStubs?.cleanup();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_fullpage_resume_starters_not_regenerated_after_new_chat() {
    const sb = sinon.createSandbox();
    let win;

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    let resumeActivityStubs;
    try {
      resumeActivityStubs = await stubResumeActivityGeneration(sb);
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await getPromptButtons(browser);
      const aiWindow = browser.contentDocument.querySelector("ai-window");

      Assert.ok(
        aiWindow.shadowRoot
          .querySelector("smartwindow-prompts")
          .prompts.some(prompt => prompt.type === "resume"),
        "Resume pill should show on the tab's initial load"
      );

      resumeActivityStubs.suggestSpy.resetHistory();
      aiWindow.onCreateNewChatClick();

      await TestUtils.waitForCondition(() => {
        const promptsEl = aiWindow.shadowRoot.querySelector(
          "smartwindow-prompts"
        );
        return (
          promptsEl?.prompts.length &&
          promptsEl.prompts.every(prompt => prompt.type !== "resume")
        );
      }, "Starters should reload without a resume pill after New Chat");

      Assert.ok(
        resumeActivityStubs.suggestSpy.notCalled,
        "New Chat should not re-trigger resume-activity generation"
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await resumeActivityStubs?.cleanup();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(
  async function test_fullpage_resume_starters_fresh_opportunity_per_tab() {
    const sb = sinon.createSandbox();
    let win, tab2;

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    let resumeActivityStubs;
    try {
      resumeActivityStubs = await stubResumeActivityGeneration(sb);

      win = await openAIWindow();
      await getPromptButtons(win.gBrowser.selectedBrowser);
      const aiWindow1 =
        win.gBrowser.selectedBrowser.contentDocument.querySelector("ai-window");
      aiWindow1.onCreateNewChatClick();
      await TestUtils.waitForCondition(() => {
        const promptsEl = aiWindow1.shadowRoot.querySelector(
          "smartwindow-prompts"
        );
        return (
          promptsEl?.prompts.length &&
          promptsEl.prompts.every(prompt => prompt.type !== "resume")
        );
      }, "First tab's starters should reload without a resume pill after New Chat");

      tab2 = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: win.gBrowser,
        opening: AIWINDOW_URL,
        waitForLoad: true,
      });
      await getPromptButtons(tab2.linkedBrowser);
      const aiWindow2 =
        tab2.linkedBrowser.contentDocument.querySelector("ai-window");

      Assert.ok(
        aiWindow2.shadowRoot
          .querySelector("smartwindow-prompts")
          .prompts.some(prompt => prompt.type === "resume"),
        "A newly opened tab should get its own fresh resume-pill opportunity"
      );
    } finally {
      if (tab2) {
        BrowserTestUtils.removeTab(tab2);
      }
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await resumeActivityStubs?.cleanup();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_dismiss_removes_pill_and_stays_dismissed() {
  const sb = sinon.createSandbox();
  let win, tab2;

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.memories.generateFromConversation", true],
      ["browser.smartwindow.memories.generateFromHistory", true],
    ],
  });

  let resumeActivityStubs;
  try {
    resumeActivityStubs = await stubResumeActivityGeneration(sb);
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await getPromptButtons(browser);

    const dismissButton = await getDismissButton(browser);
    dismissButton.click();

    await TestUtils.waitForCondition(async () => {
      const remaining = await getPromptButtons(browser);
      return remaining.length === 3;
    }, "Dismissed pill should be removed");
    const remaining = await getPromptButtons(browser);
    Assert.ok(
      Array.from(remaining).every(
        button => button.ariaLabel !== "Pick up your research"
      ),
      "The dismissed resume pill should no longer render"
    );

    tab2 = await BrowserTestUtils.openNewForegroundTab({
      gBrowser: win.gBrowser,
      opening: AIWINDOW_URL,
      waitForLoad: true,
    });
    await getPromptButtons(tab2.linkedBrowser);
    const aiWindow2 =
      tab2.linkedBrowser.contentDocument.querySelector("ai-window");

    // Skeletons aren't type "resume" either, so wait past them first.
    await TestUtils.waitForCondition(() => {
      const promptsEl = aiWindow2.shadowRoot.querySelector(
        "smartwindow-prompts"
      );
      return (
        promptsEl?.prompts.length &&
        promptsEl.prompts.every(prompt => prompt.type !== "skeleton")
      );
    }, "New tab's starters should resolve past the loading skeletons");

    Assert.ok(
      aiWindow2.shadowRoot
        .querySelector("smartwindow-prompts")
        .prompts.every(prompt => prompt.type !== "resume"),
      "The dismissed memory's only candidate should not resurface on a new tab"
    );
  } finally {
    if (tab2) {
      BrowserTestUtils.removeTab(tab2);
    }
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
    await resumeActivityStubs?.cleanup();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_resume_pool_displays_top_3_and_dismiss_reveals_next() {
    const sb = sinon.createSandbox();
    let win, tab2;

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    let resumeActivityStubs;
    try {
      resumeActivityStubs = await stubResumeActivityGenerationPool(sb, 4);
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await getPromptButtons(browser);
      const aiWindow = browser.contentDocument.querySelector("ai-window");
      const promptsEl = aiWindow.shadowRoot.querySelector(
        "smartwindow-prompts"
      );

      const resumeIds = () =>
        promptsEl.prompts
          .filter(prompt => prompt.type === "resume")
          .map(prompt => prompt.memory.id);

      await TestUtils.waitForCondition(
        () => resumeIds().length === 3,
        "Wait for the top 3 resume pills to resolve"
      );
      Assert.deepEqual(
        resumeIds(),
        ["pool-memory-0", "pool-memory-1", "pool-memory-2"],
        "Only the top 3 of the 4-candidate pool should display"
      );

      const dismissButton = await getDismissButton(browser);
      dismissButton.click();

      await TestUtils.waitForCondition(
        () => !resumeIds().includes("pool-memory-0"),
        "Dismissed candidate should be removed from the current tab"
      );

      tab2 = await BrowserTestUtils.openNewForegroundTab({
        gBrowser: win.gBrowser,
        opening: AIWINDOW_URL,
        waitForLoad: true,
      });
      await getPromptButtons(tab2.linkedBrowser);
      const aiWindow2 =
        tab2.linkedBrowser.contentDocument.querySelector("ai-window");
      const promptsEl2 = aiWindow2.shadowRoot.querySelector(
        "smartwindow-prompts"
      );

      const resumeIds2 = () =>
        promptsEl2.prompts
          .filter(prompt => prompt.type === "resume")
          .map(prompt => prompt.memory.id);

      await TestUtils.waitForCondition(
        () => resumeIds2().length === 3,
        "New tab's top 3 resume pills should resolve"
      );
      Assert.deepEqual(
        resumeIds2(),
        ["pool-memory-1", "pool-memory-2", "pool-memory-3"],
        "A new tab should surface the pool's 4th candidate in the dismissed slot"
      );
    } finally {
      if (tab2) {
        BrowserTestUtils.removeTab(tab2);
      }
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await resumeActivityStubs?.cleanup();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_resume_prompt_click_shows_confirmation_card() {
  const sb = sinon.createSandbox();
  try {
    Services.fog.testResetFOG();
    sb.stub(openAIEngine, "build").resolves({});
    const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory").resolves();

    await testResumeActivityClick(sb, async ({ aiWindow, buttons }) => {
      const conversationIdAtClick = aiWindow.conversationId;
      buttons[0].click();

      await TestUtils.waitForCondition(
        () => fetchWithHistoryStub.calledOnce,
        "Should generate a response for the resume-activity conversation"
      );

      Assert.equal(
        aiWindow.conversationId,
        conversationIdAtClick,
        "Should resume in the conversation the pill was clicked in"
      );

      const userMessage = aiWindow.conversation.messages.findLast(
        message => message.role === lazy.MESSAGE_ROLE.USER
      );
      const submitEvents = Glean.smartWindow.chatSubmit.testGetValue();
      Assert.equal(
        submitEvents?.length,
        1,
        "Resume pill click records a single chat submit event"
      );
      Assert.equal(
        submitEvents[0].extra.submit_type,
        "resume",
        "Resume pill click records submit_type resume"
      );
      Assert.equal(
        submitEvents[0].extra.chat_id,
        conversationIdAtClick,
        "Chat submit event carries the clicked conversation id"
      );
      Assert.equal(
        submitEvents[0].extra.length,
        String(userMessage.content.body.length),
        "Chat submit event measures the resume-activity user turn"
      );

      const assistantMessage = aiWindow.conversation.messages.at(-1);
      Assert.deepEqual(
        assistantMessage.toolUIData,
        {
          uiType: "tab-group-confirmation",
          toolCallId: "resume-activity-memory-1",
          isResumeActivity: true,
          properties: {
            actionType: "open_tabs",
            tabGroupLabel: "Your research",
            tabs: [1, 2, 3, 4].map((id, index) => ({
              token: String(index),
              url: `https://example.com/${id}`,
              title: `Example ${id}`,
              iconSrc: `page-icon:https://example.com/${id}`,
              checked: false,
            })),
          },
        },
        "Should attach a tab-selection confirmation card built from the pill's preview tabs"
      );
    });
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_prompt_click_injects_context() {
  const sb = sinon.createSandbox();
  try {
    sb.stub(openAIEngine, "build").resolves({});
    const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory").resolves();
    const realTimeStub = sb
      .stub(this.ChatConversation.prototype, "injectRealTimeContext")
      .resolves();

    await testResumeActivityClick(sb, async ({ buttons }) => {
      buttons[0].click();

      await TestUtils.waitForCondition(
        () => fetchWithHistoryStub.calledOnce,
        "Should generate a response for the resume-activity conversation"
      );

      Assert.ok(
        realTimeStub.calledOnce,
        "Should inject real-time context onto the resume-activity user message"
      );
    });
  } finally {
    sb.restore();
  }
});

add_task(async function test_resume_prompt_click_marks_memory_applied() {
  const sb = sinon.createSandbox();
  const { server, port } = startMockOpenAI({
    streamChunks: ["Sure, here's an update."],
  });
  try {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.endpoint", `http://localhost:${port}/v1`]],
    });

    try {
      await testResumeActivityClick(
        sb,
        async ({ browser, buttons }) => {
          buttons[0].click();

          const aiWindowEl = browser.contentDocument.querySelector("ai-window");
          const aichatBrowser = await TestUtils.waitForCondition(
            () => aiWindowEl.shadowRoot?.querySelector("#aichat-browser"),
            "Wait for aichat-browser"
          );

          await SpecialPowers.spawn(aichatBrowser, [], async () => {
            const chatContent =
              content.document.querySelector("ai-chat-content");

            await ContentTaskUtils.waitForMutationCondition(
              chatContent.shadowRoot,
              { childList: true, subtree: true },
              () =>
                chatContent.shadowRoot.querySelector("assistant-message-footer")
            );
            let appliedButton;
            await ContentTaskUtils.waitForCondition(() => {
              const button = chatContent.shadowRoot
                .querySelector("assistant-message-footer")
                ?.shadowRoot?.querySelector("applied-memories-button");
              // Unwrap the content element so Lit's prototype-backed
              // properties are visible through SpecialPowers.
              appliedButton = button?.wrappedJSObject ?? button;
              return appliedButton?.appliedMemories?.length;
            }, "Wait for the resume-activity memory to be marked applied");

            Assert.equal(
              appliedButton.appliedMemories.length,
              1,
              "Resume-activity response should have exactly one applied memory"
            );
            Assert.equal(
              appliedButton.appliedMemories[0].memory_summary,
              "Research project",
              "Applied memory should be the one that drove the resume-activity conversation"
            );
          });
        },
        { token: "mock-fxa-token" }
      );
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  } finally {
    sb.restore();
    await stopMockOpenAI(server);
  }
});

add_task(
  async function test_resume_prompt_click_shows_confirmation_card_without_memory_context_when_toggled_off() {
    const sb = sinon.createSandbox();
    try {
      const engineBuildStub = sb
        .stub(openAIEngine, "build")
        .resolves({ model: "stub-resume-model" });
      const fetchWithHistoryStub = sb.stub(Chat, "fetchWithHistory").resolves();
      // The engine's model drives system prompt assembly, so record which
      // model each load ran against.
      const modelsAtPromptLoad = [];
      const loadSystemPrompt = this.ChatConversation.prototype.loadSystemPrompt;
      sb.stub(this.ChatConversation.prototype, "loadSystemPrompt").callsFake(
        function (...args) {
          modelsAtPromptLoad.push(this.engine?.model ?? null);
          return loadSystemPrompt.apply(this, args);
        }
      );

      await testResumeActivityClick(sb, async ({ aiWindow, buttons }) => {
        const memoriesButton = aiWindow.shadowRoot.querySelector(
          "memories-icon-button"
        );
        memoriesButton.dispatchEvent(
          new CustomEvent("aiwindow-memories-toggle:on-change", {
            bubbles: true,
            composed: true,
            detail: { pressed: false },
          })
        );
        await TestUtils.waitForCondition(
          () => memoriesButton.pressed === false,
          "Memories toggle should turn off"
        );

        const conversationIdAtClick = aiWindow.conversationId;
        engineBuildStub.resetHistory();
        modelsAtPromptLoad.length = 0;
        buttons[0].click();

        await TestUtils.waitForCondition(
          () => fetchWithHistoryStub.calledOnce,
          "Should generate a response for the resume-activity conversation"
        );

        Assert.equal(
          aiWindow.conversationId,
          conversationIdAtClick,
          "Should resume in the conversation the pill was clicked in on the memory-free path"
        );

        Assert.equal(
          engineBuildStub.callCount,
          1,
          "Should build the engine once, in the response path, not in the conversation builder"
        );
        Assert.equal(
          aiWindow.conversation.engine.model,
          "stub-resume-model",
          "Should run the memory-free conversation on the engine the response path built"
        );
        Assert.equal(
          modelsAtPromptLoad.at(-1),
          "stub-resume-model",
          "Should assemble the final system prompt for the built engine's model"
        );

        const assistantMessage = aiWindow.conversation.messages.at(-1);
        Assert.deepEqual(
          assistantMessage.toolUIData,
          {
            uiType: "tab-group-confirmation",
            toolCallId: "resume-activity-memory-1",
            isResumeActivity: true,
            properties: {
              actionType: "open_tabs",
              tabGroupLabel: "Your research",
              tabs: [1, 2, 3, 4].map((id, index) => ({
                token: String(index),
                url: `https://example.com/${id}`,
                title: `Example ${id}`,
                iconSrc: `page-icon:https://example.com/${id}`,
                checked: false,
              })),
            },
          },
          "Should still attach a tab-selection confirmation card when memories are toggled off"
        );

        const userMessage = aiWindow.conversation.messages.at(-2);
        Assert.ok(
          !userMessage.content.userContext?.resumeActivityContext,
          "Should not bake memory context into the conversation when memories are toggled off"
        );
      });
    } finally {
      sb.restore();
    }
  }
);

add_task(
  async function test_fullpage_resume_starters_disabled_when_prefs_off() {
    const sb = sinon.createSandbox();
    let win;

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", false],
        ["browser.smartwindow.memories.generateFromHistory", false],
      ],
    });

    try {
      // Existing memories should not override disabled generation prefs.
      const existingMemories = [{ id: "memory-1", memory_summary: "Test" }];
      sb.stub(MemoriesManager, "getAllMemories").resolves(existingMemories);
      const suggestSpy = sb.spy(JourneyManager, "suggestJourneysToResume");
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await getPromptButtons(browser);
      const aiWindow = browser.contentDocument.querySelector("ai-window");
      const promptsEl = aiWindow.shadowRoot.querySelector(
        "smartwindow-prompts"
      );

      Assert.ok(
        promptsEl.prompts.every(prompt => prompt.type !== "resume"),
        "Resume pills should not render with both memories prefs off"
      );
      Assert.ok(
        suggestSpy.notCalled,
        "Resume starter generation should not run with both memories prefs off"
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(
  async function test_fullpage_resume_starters_disabled_by_feature_pref() {
    const sb = sinon.createSandbox();
    let win;

    await SpecialPowers.pushPrefEnv({
      set: [
        // Memories on, so the gate under test is the only thing holding back
        // resume generation.
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
        ["browser.smartwindow.resumeActivity.enabled", false],
        // Cards are the other resume surface; leave it on to prove the section
        // stays empty rather than relying on its own pref being off.
        ["browser.smartwindow.resumeCards.enabled", true],
      ],
    });

    let resumeActivityStubs;
    try {
      resumeActivityStubs = await stubResumeActivityGeneration(sb);
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      const buttons = await getPromptButtons(browser);
      const aiWindow = browser.contentDocument.querySelector("ai-window");
      const promptsEl = aiWindow.shadowRoot.querySelector(
        "smartwindow-prompts"
      );

      Assert.ok(
        resumeActivityStubs.suggestSpy.notCalled,
        "Resume generation should not run when the feature is disabled"
      );
      Assert.deepEqual(
        promptsEl.prompts.map(prompt => prompt.type),
        ["chat", "chat", "chat"],
        "Only the static starters should render: no skeletons and no resume pills"
      );
      Assert.equal(
        buttons.length,
        3,
        "Static starters should still be clickable"
      );
      Assert.deepEqual(aiWindow.resumeCards, [], "No resume cards should load");
      Assert.equal(
        aiWindow.shadowRoot
          .querySelector("smartwindow-resume-section")
          .shadowRoot.querySelector(".resume-section-grid"),
        null,
        "The resume section should render nothing"
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await resumeActivityStubs?.cleanup();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(
  async function test_fullpage_resume_starters_disabled_without_existing_memories() {
    const sb = sinon.createSandbox();
    let win;

    // Enabled prefs cannot generate resume starters without memories.
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    try {
      sb.stub(MemoriesManager, "getAllMemories").resolves([]);
      const suggestSpy = sb.spy(JourneyManager, "suggestJourneysToResume");
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await getPromptButtons(browser);
      const aiWindow = browser.contentDocument.querySelector("ai-window");
      const promptsEl = aiWindow.shadowRoot.querySelector(
        "smartwindow-prompts"
      );

      Assert.ok(
        promptsEl.prompts.every(prompt => prompt.type !== "resume"),
        "Resume pills should not render with no existing memories, even with prefs on"
      );
      Assert.ok(
        suggestSpy.notCalled,
        "Resume starter generation should not run with no existing memories"
      );
    } finally {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
      sb.restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

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
