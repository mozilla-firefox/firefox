/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SmartWindowTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs"
);
const { GetPageContent, RunSearch } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);
const { buildEngineForFeature } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs"
);
const { MODEL_FEATURES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

// Resume pills only show when resume cards are off.
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.resumeCards.enabled", false]],
  });
});

async function dispatchSmartbarCommit(browser, value, action) {
  await SpecialPowers.spawn(browser, [value, action], async (val, act) => {
    const aiWindow = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );

    const commitEvent = new content.CustomEvent("smartbar-commit", {
      detail: {
        value: val,
        action: act,
      },
      bubbles: true,
      composed: true,
    });

    smartbar.ownerDocument.dispatchEvent(commitEvent);
  });
}

async function dispatchMemoriesToggle(browser, pressed) {
  await SpecialPowers.spawn(browser, [pressed], async pressedState => {
    const aiWindow = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindow.shadowRoot.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );

    const toggleEvent = new content.CustomEvent(
      "aiwindow-memories-toggle:on-change",
      {
        detail: { pressed: pressedState },
        bubbles: true,
        composed: true,
      }
    );

    smartbar.dispatchEvent(toggleEvent);
  });
}

async function getFirstPromptTextAndClick(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindow = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    const promptsElement = await ContentTaskUtils.waitForCondition(
      () => aiWindow.shadowRoot.querySelector("smartwindow-prompts"),
      "Wait for smartwindow-prompts element"
    );
    const buttons =
      promptsElement.shadowRoot.querySelectorAll(".sw-prompt-button");
    const firstPromptText = buttons[0].textContent.trim();
    buttons[0].click();
    return firstPromptText;
  });
}

add_task(async function test_smartbar_commit_telemetry() {
  const sb = this.sinon.createSandbox();
  let win;

  try {
    Services.fog.testResetFOG();
    sb.stub(this.Chat, "fetchWithHistory");
    sb.stub(this.openAIEngine, "build");

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await dispatchSmartbarCommit(browser, "Telemetry prompt", "chat");
    await TestUtils.waitForTick();

    const events = Glean.smartWindow.chatSubmit.testGetValue();
    Assert.equal(events?.length, 1, "One smartbar commit event was recorded");
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "smartbar commit event includes the conversation id"
    );
    Assert.equal(
      events[0].extra.location,
      "fullpage",
      "smartbar commit event includes the location"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
  }
});

add_task(async function test_memories_toggle_telemetry() {
  Services.fog.testResetFOG();

  const win = await openAIWindow();
  let MemoryStore;
  try {
    ({ MemoryStore } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
    ));
    const preExistingMemories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });
    for (const memory of preExistingMemories) {
      await MemoryStore.hardDeleteMemory(memory.id, "other");
    }
    const seededMemories = [
      {
        id: "memory-1",
        memory_summary: "User is vegan",
        category: "preference",
        intent: "profile",
        reasoning: "Test memory",
        score: 0.5,
        updated_at: Date.now(),
        is_deleted: false,
      },
      {
        id: "memory-2",
        memory_summary: "User has a cat",
        category: "personal",
        intent: "profile",
        reasoning: "Test memory",
        score: 0.5,
        updated_at: Date.now(),
        is_deleted: false,
      },
    ];
    for (const memory of seededMemories) {
      await MemoryStore.addMemory(memory);
    }

    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await dispatchMemoriesToggle(browser, true);
    await TestUtils.waitForTick();

    const events = Glean.smartWindow.memoriesToggle.testGetValue();
    Assert.equal(events?.length, 1, "One memories toggle event was recorded");
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "memories toggle event includes the conversation id"
    );
    Assert.equal(
      events[0].extra.toggle,
      "true",
      "memories toggle event includes the enabled state"
    );
    Assert.equal(
      events[0].extra.memories,
      "2",
      "memories toggle event includes memories count"
    );
    Assert.equal(
      events[0].extra.location,
      "fullpage",
      "memories toggle event includes the location"
    );
  } finally {
    if (MemoryStore) {
      const postTestMemories = await MemoryStore.getMemories({
        includeSoftDeleted: true,
      });
      for (const memory of postTestMemories) {
        await MemoryStore.hardDeleteMemory(memory.id, "other");
      }
    }
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_quick_prompt_displayed_telemetry() {
  Services.fog.testResetFOG();

  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await TestUtils.waitForCondition(
      async () => !!(await getPromptButtons(browser)).length,
      "Wait for prompts to render"
    );
    await TestUtils.waitForTick();

    const promptCount = (await getPromptButtons(browser)).length;
    const events = Glean.smartWindow.quickPromptDisplayed.testGetValue();
    Assert.greater(
      events?.length,
      0,
      "At least one quick prompt displayed event was recorded"
    );
    const lastEvent = events.at(-1);
    Assert.equal(
      lastEvent.extra.chat_id,
      conversationId,
      "quick prompt displayed includes the conversation id"
    );
    Assert.equal(
      lastEvent.extra.prompts,
      String(promptCount),
      "quick prompt displayed includes the prompt count"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_followup_displayed_telemetry_sends_count() {
  const mockEngineManager = new MockEngineManager();
  Services.fog.testResetFOG();

  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await dispatchSmartbarCommit(browser, "Hello", "chat");
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: [
        "Here is my response.",
        "§followup: Follow up question 1§",
        "§followup: Follow up question 2§",
      ],
    });

    const followUpEvent = await TestUtils.waitForCondition(
      () =>
        Glean.smartWindow.quickPromptDisplayed
          .testGetValue()
          ?.find(
            e => e.extra.chat_id === conversationId && e.extra.message_seq > 0
          ),
      "Wait for follow-up displayed event"
    );

    Assert.equal(
      followUpEvent.extra.prompts,
      "2",
      "Follow-up displayed event includes the final prompt count"
    );
    Assert.equal(
      followUpEvent.extra.resume_prompts,
      "0",
      "Follow-up displayed event has zero resume prompts"
    );

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    aiWindow.onQuickPromptClicked("Follow up question 1", false);

    const clickEvent = await TestUtils.waitForCondition(
      () =>
        Glean.smartWindow.quickPromptClicked
          .testGetValue()
          ?.find(e => e.extra.chat_id === conversationId),
      "Wait for follow-up clicked event"
    );

    Assert.equal(
      clickEvent.extra.starter_type,
      "followup",
      "Follow-up clicked event includes the starter type"
    );
    await TestUtils.waitForCondition(
      () => mockEngineManager.engines.get("chat").runRequests.size,
      "Follow-up click should submit a new request"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_prompt_selected_telemetry() {
  const sb = this.sinon.createSandbox();
  let win;

  try {
    Services.fog.testResetFOG();
    sb.stub(this.Chat, "fetchWithHistory");
    sb.stub(this.openAIEngine, "build");

    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.endpoint", "http://localhost:0/v1"]],
    });

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    const firstPromptText = await getFirstPromptTextAndClick(browser);
    Assert.ok(firstPromptText, "Prompt text is available");
    await TestUtils.waitForTick();

    const events = Glean.smartWindow.quickPromptClicked.testGetValue();
    Assert.equal(events?.length, 1, "One prompt selected event was recorded");
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "prompt selected event includes the conversation id"
    );
    Assert.equal(
      events[0].extra.location,
      "fullpage",
      "prompt selected event includes the location"
    );
    Assert.equal(
      events[0].extra.starter_type,
      "default",
      "prompt selected event includes the starter type"
    );
  } finally {
    await SpecialPowers.popPrefEnv();
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
  }
});

add_task(async function test_resume_prompt_click_starter_type_telemetry() {
  const sb = sinon.createSandbox();
  try {
    Services.fog.testResetFOG();
    sb.stub(openAIEngine, "build").resolves({});
    sb.stub(Chat, "fetchWithHistory").resolves();

    await testResumeActivityClick(sb, async ({ buttons }) => {
      buttons[0].click();

      const events = await TestUtils.waitForCondition(
        () => Glean.smartWindow.quickPromptClicked.testGetValue(),
        "Wait for quick prompt clicked event"
      );

      Assert.equal(events.length, 1, "One prompt clicked event was recorded");
      Assert.equal(
        events[0].extra.location,
        "fullpage",
        "Resume pill click records location fullpage"
      );
      Assert.equal(
        events[0].extra.starter,
        "true",
        "Resume pill click records starter true"
      );
      Assert.equal(
        events[0].extra.starter_type,
        "resume",
        "Resume pill click records starter_type resume"
      );
    });
  } finally {
    sb.restore();
  }
});

add_task(async function test_dismiss_telemetry() {
  const sb = sinon.createSandbox();
  try {
    Services.fog.testResetFOG();

    await testResumeActivityClick(sb, async ({ browser }) => {
      const conversationId = await getConversationId(browser);
      const dismissButton = await getDismissButton(browser);
      dismissButton.click();

      const events = await TestUtils.waitForCondition(
        () => Glean.smartWindow.quickPromptDismissed.testGetValue(),
        "Wait for quick prompt dismissed event"
      );

      Assert.equal(events.length, 1, "One prompt dismissed event was recorded");
      Assert.equal(
        events[0].extra.chat_id,
        conversationId,
        "Dismissed event includes the conversation id"
      );
    });
  } finally {
    sb.restore();
  }
});

add_task(async function test_fullpage_resume_starters_displayed_telemetry() {
  const sb = sinon.createSandbox();
  let win;

  Services.fog.testResetFOG();
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
    const promptsEl = aiWindow.shadowRoot.querySelector("smartwindow-prompts");
    const promptCount = promptsEl.prompts.length;
    const resumeCount = promptsEl.prompts.filter(
      prompt => prompt.type === "resume"
    ).length;

    const events = Glean.smartWindow.quickPromptDisplayed.testGetValue();
    Assert.greater(
      events?.length,
      0,
      "At least one quick prompt displayed event was recorded"
    );
    const lastEvent = events.at(-1);
    Assert.equal(
      lastEvent.extra.prompts,
      String(promptCount),
      "Displayed event includes the total prompt count"
    );
    Assert.equal(
      lastEvent.extra.resume_prompts,
      String(resumeCount),
      "Displayed event includes the resume-pill-only count"
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

add_task(async function test_chat_storage_metric() {
  const { ChatConversation } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
  );
  const { ChatStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
  );

  // ChatStore is a persistent singleton whose #lastRecordedSize guard survives
  // testResetFOG(). Prior tests can leave the DB file grown to a size the next
  // write reuses without changing, which would suppress the chat_storage emit
  // and hang this test. Start from a clean DB so the write always changes size.
  await ChatStore.destroyDatabase();
  Services.fog.testResetFOG();
  let conversation;
  try {
    conversation = new ChatConversation({});
    const largePayload = "x".repeat(12000);
    conversation.addUserMessage(largePayload);
    conversation.addAssistantMessage("text", largePayload);
    await ChatStore.updateConversation(conversation);
    const expectedSize = await ChatStore.getDatabaseSize();

    // Writes only queue a coalesced measurement, so flush it rather than
    // waiting out DB_SIZE_RECORD_DELAY_MS.
    await ChatStore.recordDatabaseSizeNow();

    Assert.equal(
      Glean.smartWindow.chatStorage.testGetValue(),
      expectedSize,
      "chat storage metric should be recorded"
    );
  } finally {
    if (conversation) {
      await ChatStore.deleteConversationById(conversation.id);
    }
  }
});

add_task(async function test_memories_count_metric() {
  Services.fog.testResetFOG();

  const { MemoryStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
  );
  const existingMemories = await MemoryStore.getMemories({
    includeSoftDeleted: true,
  });
  for (const memory of existingMemories) {
    await MemoryStore.hardDeleteMemory(memory.id, "other");
  }

  const memories = [
    {
      id: "memory-history",
      memory_summary: "User is vegan",
      sources: ["history"],
    },
    {
      id: "memory-conversation",
      memory_summary: "User has a cat",
      sources: ["conversation"],
    },
    {
      id: "memory-session",
      memory_summary: "User researches firefox privacy",
      sources: ["session"],
    },
  ];
  for (const memory of memories) {
    await MemoryStore.addMemory(memory);
  }

  await TestUtils.waitForCondition(() => {
    return (
      Glean.smartWindow.memoriesCount.history.testGetValue() === 1 &&
      Glean.smartWindow.memoriesCount.conversation.testGetValue() === 1 &&
      Glean.smartWindow.memoriesCount.session.testGetValue() === 1
    );
  }, "memories_count should record history, conversation and session counts");

  for (const memory of memories) {
    await MemoryStore.hardDeleteMemory(memory.id, "other");
  }
});

add_task(async function test_memories_last_updated_metric() {
  Services.fog.testResetFOG();

  const { MemoryStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
  );

  const memory = {
    id: "memory-updated",
    memory_summary: "User likes tea",
    category: "preference",
    intent: "profile",
    reasoning: "Test memory",
    score: 0.5,
    updated_at: Date.now(),
    is_deleted: false,
    source: "history",
  };

  await MemoryStore.addMemory(memory);

  const value = Glean.smartWindow.memoriesLastUpdated.testGetValue();
  Assert.ok(value instanceof Date, "memories_last_updated records a datetime");

  await MemoryStore.hardDeleteMemory(memory.id, "other");
});

add_task(async function test_get_page_content_telemetry() {
  const sb = this.sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();

  try {
    Services.fog.testResetFOG();
    const getPageContentStub = sb
      .stub(GetPageContent, "getPageContent")
      .resolves([
        { url: "https://example.com/", ok: true, content: "abc" },
        { url: "https://example.com/", ok: true, content: "defg" },
      ]);

    const win = await openAIWindow();
    try {
      const browser = win.gBrowser.selectedBrowser;
      await dispatchSmartbarCommit(browser, "Read the page", "chat");
      await mockEngineManager.respondToWithToolCall({
        toolName: "get_page_content",
        toolParams: { url: "https://example.com/" },
      });

      await TestUtils.waitForCondition(
        () => getPageContentStub.calledOnce,
        "get_page_content tool should be called"
      );

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.getPageContent.testGetValue()?.length,
        "get_page_content telemetry should be recorded"
      );

      const events = Glean.smartWindow.getPageContent.testGetValue();
      Assert.equal(events?.length, 1, "One get_page_content event recorded");
      Assert.equal(
        events[0].extra.length,
        "7",
        "get_page_content length equals total characters read"
      );
    } finally {
      await BrowserTestUtils.closeWindow(win);
    }
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sb.restore();
  }
});

add_task(async function test_search_handoff_telemetry() {
  const sb = this.sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();
  let win;

  try {
    Services.fog.testResetFOG();
    const { SearchService } = ChromeUtils.importESModule(
      "moz-src:///toolkit/components/search/SearchService.sys.mjs"
    );
    await SearchService.init();
    const runSearchStub = sb
      .stub(RunSearch, "runSearch")
      .resolves("Mock search results");

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await dispatchSmartbarCommit(
      browser,
      "search the web for something",
      "chat"
    );
    await mockEngineManager.respondToWithToolCall({
      toolName: "run_search",
      toolParams: { query: "test search query" },
    });

    await TestUtils.waitForCondition(
      () => runSearchStub.calledOnce,
      "run_search tool should be called"
    );

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.searchHandoff.testGetValue()?.length,
      "search handoff telemetry should be recorded"
    );

    const events = Glean.smartWindow.searchHandoff.testGetValue();
    Assert.equal(events?.length, 1, "One search handoff event recorded");
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "search handoff includes the conversation id"
    );
    Assert.ok(events[0].extra.provider, "search handoff includes the provider");
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sb.restore();
  }
});

add_task(async function test_run_search_toolcall_telemetry() {
  const sb = this.sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();
  let win;

  try {
    Services.fog.testResetFOG();
    const { SearchService } = ChromeUtils.importESModule(
      "moz-src:///toolkit/components/search/SearchService.sys.mjs"
    );
    await SearchService.init();
    sb.stub(RunSearch, "runSearch").resolves("Mock search results");

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    await dispatchSmartbarCommit(
      browser,
      "search the web for something",
      "chat"
    );
    await mockEngineManager.respondToWithToolCall({
      toolName: "run_search",
      toolParams: { query: "test search query" },
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.toolCall.testGetValue()?.length,
      "tool_call telemetry should be recorded"
    );

    const events = Glean.smartWindow.toolCall.testGetValue();
    Assert.equal(events?.length, 1, "One tool_call event recorded");

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const { engine } = await buildEngineForFeature(MODEL_FEATURES.CHAT);

    Assert.deepEqual(
      events[0].extra,
      {
        tool_name: "run_search",
        chat_id: conversationId,
        error: "",
        location: "fullpage",
        model: engine.model,
        prompt_version: aiWindow.conversation.systemPromptVersion,
        // user message + assistant tool-call + tool-result = 3 at the
        // time the event was recorded. The conversation may add more
        // messages afterwards, so this can't be derived live.
        message_seq: "3",
      },
      "tool_call event has the expected extras"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sb.restore();
  }
});

add_task(async function test_run_search_toolcall_failure_telemetry() {
  const sb = this.sinon.createSandbox();
  const mockEngineManager = new MockEngineManager();
  let win;

  try {
    Services.fog.testResetFOG();
    const { SearchService } = ChromeUtils.importESModule(
      "moz-src:///toolkit/components/search/SearchService.sys.mjs"
    );
    await SearchService.init();
    sb.stub(RunSearch, "runSearch").rejects(
      new Error("simulated tool failure")
    );

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await dispatchSmartbarCommit(
      browser,
      "search the web for something",
      "chat"
    );
    await mockEngineManager.respondToWithToolCall({
      toolName: "run_search",
      toolParams: { query: "test search query" },
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.toolCall.testGetValue()?.length,
      "tool_call telemetry should be recorded on failure"
    );

    const events = Glean.smartWindow.toolCall.testGetValue();
    Assert.equal(events?.length, 1, "One tool_call event recorded on failure");
    Assert.equal(
      events[0].extra.tool_name,
      "run_search",
      "tool_call event names the invoked tool"
    );
    Assert.equal(
      events[0].extra.error,
      "execution_failed",
      "tool_call event marks failed execution with error code"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    sb.restore();
  }
});

add_task(async function test_chat_retrieved_telemetry() {
  Services.fog.testResetFOG();

  const { ChatConversation } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
  );
  const { ChatStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
  );

  const conversation = new ChatConversation({
    updatedDate: Date.now() - 5 * 60 * 1000,
  });
  conversation.addUserMessage("Hello");
  conversation.addAssistantMessage("text", "Hi");
  await ChatStore.updateConversation(conversation);

  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;
    browser.setAttribute("data-conversation-id", conversation.id);

    BrowserTestUtils.startLoadingURIString(
      browser,
      "chrome://browser/content/aiwindow/aiWindow.html"
    );
    await BrowserTestUtils.browserLoaded(browser, {
      wantLoad: "chrome://browser/content/aiwindow/aiWindow.html",
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.chatRetrieved.testGetValue()?.length,
      "chat retrieved telemetry should be recorded"
    );

    const events = Glean.smartWindow.chatRetrieved.testGetValue();
    Assert.greater(
      events?.length,
      0,
      "At least one chat retrieved event recorded"
    );
    Assert.equal(
      events[0].extra.chat_id,
      conversation.id,
      "chat retrieved includes the conversation id"
    );
    Assert.equal(
      events[0].extra.message_seq,
      String(conversation.messageCount),
      "chat retrieved includes the message count"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_link_click_telemetry() {
  Services.fog.testResetFOG();
  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;
    const conversationId = await getConversationId(browser);

    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    aiWindow.onOpenLink();

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.linkClick.testGetValue()?.length,
      "link click telemetry should be recorded"
    );

    const events = Glean.smartWindow.linkClick.testGetValue();
    Assert.equal(events?.length, 1, "One link click event recorded");
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "link click includes the conversation id"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_memory_removed_panel_telemetry() {
  const win = await openAIWindow();
  let MemoryStore;
  try {
    ({ MemoryStore } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
    ));
    const preExistingMemories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });
    for (const memory of preExistingMemories) {
      await MemoryStore.hardDeleteMemory(memory.id, "other");
    }
    Services.fog.testResetFOG();
    const seededMemories = [
      {
        id: "memory-1",
        memory_summary: "User is vegan",
        category: "preference",
        intent: "profile",
        reasoning: "Test memory",
        score: 0.5,
        updated_at: Date.now(),
        is_deleted: false,
      },
      {
        id: "memory-2",
        memory_summary: "User has a cat",
        category: "personal",
        intent: "profile",
        reasoning: "Test memory",
        score: 0.5,
        updated_at: Date.now(),
        is_deleted: false,
      },
    ];
    for (const memory of seededMemories) {
      await MemoryStore.addMemory(memory);
    }

    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const { AssistantRoleOpts } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
    );

    const conversation = new ChatConversation({});
    conversation.addUserMessage("Hello");
    const memories = [
      { id: "memory-1", memory_summary: "User is vegan" },
      { id: "memory-2", memory_summary: "User has a cat" },
    ];
    const assistantOpts = new AssistantRoleOpts(
      null,
      null,
      null,
      true,
      null,
      memories,
      []
    );
    conversation.addAssistantMessage("text", "Hi", assistantOpts);
    aiWindow.openConversation(conversation);
    const assistantMessageId = conversation.messages.at(-1).id;
    aiWindow.handleFooterAction({
      action: "remove-applied-memory",
      messageId: assistantMessageId,
      memory: memories[0],
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.memoryRemovedPanel.testGetValue()?.length,
      "memory removed panel telemetry should be recorded"
    );

    const events = Glean.smartWindow.memoryRemovedPanel.testGetValue();
    Assert.equal(events?.length, 1, "One memory removed panel event recorded");
    Assert.equal(
      events[0].extra.memories,
      "1",
      "memory removed panel event includes memories count"
    );
    Assert.equal(
      events[0].extra.in_use,
      "1",
      "in_use reflects remaining applied memories count for the message"
    );
  } finally {
    if (MemoryStore) {
      const postTestMemories = await MemoryStore.getMemories({
        includeSoftDeleted: true,
      });
      for (const memory of postTestMemories) {
        await MemoryStore.hardDeleteMemory(memory.id, "other");
      }
    }
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_memory_removed_panel_telemetry_last_memory() {
  const win = await openAIWindow();
  let MemoryStore;
  try {
    ({ MemoryStore } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
    ));
    const preExistingMemories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });
    for (const memory of preExistingMemories) {
      await MemoryStore.hardDeleteMemory(memory.id, "other");
    }
    Services.fog.testResetFOG();

    const seededMemory = {
      id: "memory-1",
      memory_summary: "User is vegan",
      category: "preference",
      intent: "profile",
      reasoning: "Test memory",
      score: 0.5,
      updated_at: Date.now(),
      is_deleted: false,
    };
    await MemoryStore.addMemory(seededMemory);

    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const { AssistantRoleOpts } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
    );

    const conversation = new ChatConversation({});
    conversation.addUserMessage("Hello");
    const memories = [{ id: "memory-1", memory_summary: "User is vegan" }];
    const assistantOpts = new AssistantRoleOpts(
      null,
      null,
      null,
      true,
      null,
      memories,
      []
    );
    conversation.addAssistantMessage("text", "Hi", assistantOpts);
    aiWindow.openConversation(conversation);
    const assistantMessageId = conversation.messages.at(-1).id;
    aiWindow.handleFooterAction({
      action: "remove-applied-memory",
      messageId: assistantMessageId,
      memory: memories[0],
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.memoryRemovedPanel.testGetValue()?.length,
      "memory removed panel telemetry should be recorded"
    );

    const events = Glean.smartWindow.memoryRemovedPanel.testGetValue();
    Assert.equal(events?.length, 1, "One memory removed panel event recorded");
    Assert.equal(
      events[0].extra.in_use,
      "0",
      "in_use is 0 when the last applied memory for the message is removed"
    );
  } finally {
    if (MemoryStore) {
      const postTestMemories = await MemoryStore.getMemories({
        includeSoftDeleted: true,
      });
      for (const memory of postTestMemories) {
        await MemoryStore.hardDeleteMemory(memory.id, "other");
      }
    }
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_memory_applied_telemetry() {
  let win;

  try {
    Services.fog.testResetFOG();

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const conversationId = await getConversationId(browser);
    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );
    aiWindow.onMemoriesApplied();

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.memoryApplied.testGetValue()?.length,
      "memory applied telemetry should be recorded"
    );

    const events = Glean.smartWindow.memoryApplied.testGetValue();
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "memory applied event includes the conversation id"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
  }
});

add_task(async function test_memory_applied_click_telemetry() {
  Services.fog.testResetFOG();

  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;

    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const { AssistantRoleOpts } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
    );

    const conversation = new ChatConversation({});
    conversation.addUserMessage("Hello");
    conversation.addAssistantMessage("text", "Hi", new AssistantRoleOpts());
    aiWindow.openConversation(conversation);
    aiWindow.handleFooterAction({
      action: "toggle-applied-memories",
      open: true,
    });

    const conversationId = await getConversationId(browser);
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.memoryAppliedClick.testGetValue()?.length,
      "memory applied click telemetry should be recorded"
    );

    const events = Glean.smartWindow.memoryAppliedClick.testGetValue();
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "memory applied click event includes the conversation id"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_retry_no_memories_telemetry() {
  Services.fog.testResetFOG();

  const win = await openAIWindow();
  try {
    const browser = win.gBrowser.selectedBrowser;

    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const { AssistantRoleOpts } = ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
    );

    const conversation = new ChatConversation({});
    conversation.addUserMessage("Hello");
    conversation.addAssistantMessage("text", "Hi", new AssistantRoleOpts());
    aiWindow.openConversation(conversation);
    aiWindow.handleFooterAction({
      action: "retry-without-memories",
      messageId: null,
    });

    const conversationId = await getConversationId(browser);
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.retryNoMemories.testGetValue()?.length,
      "retry without memories telemetry should be recorded"
    );

    const events = Glean.smartWindow.retryNoMemories.testGetValue();
    Assert.equal(
      events[0].extra.chat_id,
      conversationId,
      "retry without memories event includes the conversation id"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});
