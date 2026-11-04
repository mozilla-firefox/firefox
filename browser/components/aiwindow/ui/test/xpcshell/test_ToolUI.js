/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

do_get_profile();

const { ToolUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
);
const { ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);
const { tabManagementService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/TabManagementService.sys.mjs"
);
const { BrowserWindowTracker } = ChromeUtils.importESModule(
  "resource:///modules/BrowserWindowTracker.sys.mjs"
);
const { ToolUITelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUITelemetry.sys.mjs"
);

let gPermanentKeySeq = 0;
let gMockWindows = [];

// Build a mock tab with a unique permanentKey (the identity the resolver uses).
function makeTab(url, { userContextId = 0, label = "Tab" } = {}) {
  return {
    userContextId,
    label,
    permanentKey: { id: ++gPermanentKeySeq },
    linkedBrowser: {
      // A real nsIURI: AIWindow's chat-tab check compares URIs, not strings.
      currentURI: Services.io.newURI(url),
    },
  };
}

function makeWindow(tabs, selectedTab = null) {
  const win = {
    closed: false,
    gBrowser: { tabs, selectedTab },
    // AIWindow.isAIWindowActive() checks for the ai-window attribute.
    document: {
      documentElement: { hasAttribute: name => name === "ai-window" },
    },
  };
  // Real tabs know which window they are in, and the grouping checks rely on
  // it.
  tabs.forEach(tab => {
    tab.documentGlobal = win;
  });
  gMockWindows.push(win);
  return win;
}

// Reset mock windows before each task.
const _addTask = add_task;
add_task = (task, fn = task) =>
  _addTask(task, async (...args) => {
    gMockWindows = [];
    return fn(...args);
  });

add_setup(function () {
  const descriptor = Object.getOwnPropertyDescriptor(
    BrowserWindowTracker,
    "orderedWindows"
  );
  Object.defineProperty(BrowserWindowTracker, "orderedWindows", {
    configurable: true,
    get: () => gMockWindows,
  });
  registerCleanupFunction(() => {
    Object.defineProperty(BrowserWindowTracker, "orderedWindows", descriptor);
  });
});

// Build a token-bearing selection for the given mock tabs and register the
// token -> permanentKey map under toolCallId
function registerSelection(toolCallId, tabs) {
  const tokenToKey = new Map();
  const selectedTabs = tabs.map(tab => {
    const token = `tok-${tab.permanentKey.id}`;
    tokenToKey.set(token, tab.permanentKey);
    return {
      token,
      url: tab.linkedBrowser.currentURI.spec,
      title: tab.label,
      userContextId: tab.userContextId,
    };
  });
  ToolUI.registerTabKeys(toolCallId, tokenToKey);
  return { selectedTabs, tokenToKey };
}

// Build a conversation with a single assistant message carrying an
// open_tabs tab-group-confirmation, ready to be updated via handleUpdate.
function makeOpenTabsConfirmation(toolCallId) {
  const conversation = new ChatConversation({});
  conversation.addAssistantMessage("text", "Confirm?");
  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );
  assistantMessage.toolUIData = {
    toolCallId,
    uiType: "tab-group-confirmation",
    properties: { actionType: "open_tabs" },
  };
  return { conversation, assistantMessage };
}

/**
 * Test that ToolUI.handleUpdate returns false when missing required data
 */
add_task(async function test_handleUpdate_missing_messageId() {
  const conversation = new ChatConversation({});

  const result = await ToolUI.handleUpdate(
    {
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(result, false, "Should return false when messageId is missing");
});

add_task(async function test_handleUpdate_missing_toolCallId() {
  const conversation = new ChatConversation({});

  const result = await ToolUI.handleUpdate(
    {
      messageId: "message-123",
      updateType: "confirmation-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(result, false, "Should return false when toolCallId is missing");
});

/**
 * Test that ToolUI.handleUpdate returns false when message not found
 */
add_task(async function test_handleUpdate_message_not_found() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const result = await ToolUI.handleUpdate(
    {
      messageId: "non-existent-id",
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(result, false, "Should return false when message not found");
});

/**
 * Test that ToolUI.handleUpdate returns false when toolUIData doesn't exist
 */
add_task(async function test_handleUpdate_no_toolUIData() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(
    result,
    false,
    "Should return false when message has no toolUIData"
  );
});

/**
 * Test that ToolUI.handleUpdate returns false when toolCallId doesn't match
 */
add_task(async function test_handleUpdate_toolCallId_mismatch() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Manually add toolUIData to the message
  assistantMessage.toolUIData = {
    toolCallId: "different-tool-456",
    uiType: "website-confirmation",
    properties: {},
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(
    result,
    false,
    "Should return false when toolCallId doesn't match"
  );
});

/**
 * Test that ToolUI.handleUpdate successfully updates for confirmation
 */
add_task(async function test_handleUpdate_confirmation_success() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Manually add toolUIData to the message
  const originalToolCallId = "test-tool-123";
  assistantMessage.toolUIData = {
    toolCallId: originalToolCallId,
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Test Tab" }],
    },
  };

  // Mock the tabManagementService for this test
  const originalCloseTabs = tabManagementService.closeTabs;
  tabManagementService.closeTabs = async function () {
    return {
      operationId: "mock-operation-123",
      requestedCount: 1,
      failedTabs: [],
    };
  };

  // Create a mock tab and register its token -> permanentKey selection.
  const mockTab = makeTab("https://example.com/", { label: "Test Tab" });
  const mockWindow = makeWindow([mockTab]);

  const { selectedTabs } = registerSelection(originalToolCallId, [mockTab]);
  const updateData = { selectedTabs };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: originalToolCallId,
      updateType: "confirmation-tab-selection",
      updateData,
    },
    conversation,
    mockWindow
  );

  // Restore the original function
  tabManagementService.closeTabs = originalCloseTabs;

  // After refactoring, conversation.updateToolUI is called which updates the message
  Assert.equal(result, true, "Should return true on successful update");
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "ai-action-result",
    "Should change uiType to ai-action-result"
  );
  const confirmedData = assistantMessage.toolUIData.properties.confirmedData;
  Assert.deepEqual(
    {
      selectedTabs: confirmedData.selectedTabs,
      operationIds: confirmedData.operationIds,
    },
    {
      ...updateData,
      operationIds: ["mock-operation-123"],
    },
    "Should add confirmedData to properties with operationIds"
  );
  Assert.ok(
    typeof confirmedData.actionTimestamp === "number" &&
      confirmedData.actionTimestamp > 0,
    "Should include actionTimestamp for undo time calculation"
  );
});

/**
 * Test that confirmation resolves the pending tool message body with the
 * action read from the tool message.
 */
add_task(async function test_handleUpdate_confirmation_resolves_tool_action() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Close my tabs", {});
  conversation.addAssistantMessage("text", "Confirm?");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  const originalToolCallId = "test-tool-789";
  assistantMessage.toolUIData = {
    toolCallId: originalToolCallId,
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Test Tab" }],
    },
  };

  conversation.addToolCallMessage({
    tool_call_id: originalToolCallId,
    name: "manage_tabs",
    body: { pending: true, action: "close_tabs" },
  });

  const toolMessage = conversation.messages.at(-1);

  const originalCloseTabs = tabManagementService.closeTabs;
  tabManagementService.closeTabs = async function () {
    return {
      operationId: "mock-operation-789",
      requestedCount: 1,
      failedTabs: [],
    };
  };

  const mockTab = makeTab("https://example.com/", { label: "Test Tab" });
  const mockWindow = makeWindow([mockTab]);

  const { selectedTabs } = registerSelection(originalToolCallId, [mockTab]);

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: originalToolCallId,
      updateType: "confirmation-tab-selection",
      updateData: { selectedTabs },
    },
    conversation,
    mockWindow
  );

  tabManagementService.closeTabs = originalCloseTabs;

  Assert.equal(result, true, "Should return true on successful confirmation");
  Assert.equal(
    toolMessage.content.body.action,
    "close_tabs",
    "Resolved tool message body should carry the action from the pending tool message"
  );
  Assert.equal(
    toolMessage.content.body.description,
    "User confirmed the requested action. selectedTabs contains the tabs that were acted upon.",
    "Resolved tool message body should include the confirmation description"
  );
  Assert.deepEqual(
    toolMessage.content.body.selectedTabs,
    [{ url: "https://example.com/", title: "Test Tab" }],
    "Resolved tool message body should include the acted-upon tabs"
  );
});

/**
 * Test that ToolUI.handleUpdate successfully updates for cancellation
 */
add_task(async function test_handleUpdate_cancellation_success() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Manually add toolUIData to the message
  const originalToolCallId = "test-tool-456";
  assistantMessage.toolUIData = {
    toolCallId: originalToolCallId,
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Test Tab" }],
    },
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: originalToolCallId,
      updateType: "cancel-tab-selection",
    },
    conversation,
    null
  );

  Assert.equal(result, true, "Should return true on successful cancellation");
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "cancelled-component",
    "Should change uiType to cancelled-component"
  );
  Assert.ok(
    assistantMessage.toolUIData.properties.tabs,
    "Should preserve original properties"
  );
});

/**
 * Test that "Select all" / "Deselect all" record telemetry for the card they
 * were clicked on, without changing the card
 */
add_task(async function test_handleUpdate_toggle_all_telemetry() {
  const originalRecord = ToolUITelemetry.recordBrowserActionPromptToggleAll;
  let recorded = [];
  ToolUITelemetry.recordBrowserActionPromptToggleAll = data =>
    recorded.push(data);

  const toggleAll = async (toolUIData, allSelected) => {
    recorded = [];
    const conversation = new ChatConversation({});
    const message = conversation.addAssistantMessage("text", "Confirm?");
    message.toolUIData = { toolCallId: "toggle-all", ...toolUIData };
    const result = await ToolUI.handleUpdate(
      {
        messageId: message.id,
        toolCallId: "toggle-all",
        updateType: "toggle-all-tab-selection",
        updateData: { allSelected },
      },
      conversation,
      null,
      "sidebar"
    );
    Assert.equal(
      message.toolUIData.uiType,
      toolUIData.uiType,
      "The card is left as it was"
    );
    return { result, conversation };
  };

  try {
    let { result, conversation } = await toggleAll(
      {
        uiType: "website-confirmation",
        properties: { tabs: [{}, {}] },
      },
      true
    );
    Assert.equal(result, true, "Select all is handled");
    Assert.deepEqual(
      recorded,
      [
        {
          location: "sidebar",
          chat_id: conversation.id,
          message_seq: conversation.messageCount,
          action: "close_tabs",
          prompt_type: "safety_confirmation",
          entrypoint: "browser_action",
          toggle: "select_all",
          candidates: 2,
        },
      ],
      "Records select all on a chat request card"
    );

    ({ result } = await toggleAll(
      {
        uiType: "tab-group-confirmation",
        isResumeActivity: true,
        properties: { actionType: "open_tabs", tabs: [{}, {}, {}] },
      },
      false
    ));
    Assert.equal(result, true, "Deselect all is handled");
    Assert.equal(recorded.length, 1, "Records one event");
    Assert.equal(recorded[0].action, "open_tabs", "Records the card action");
    Assert.equal(recorded[0].entrypoint, "resume", "Records the resume pill");
    Assert.equal(recorded[0].toggle, "deselect_all", "Records deselect all");
    Assert.equal(recorded[0].candidates, 3, "Counts the tabs on the card");

    ({ result } = await toggleAll(
      { uiType: "ai-action-result", properties: {} },
      true
    ));
    Assert.equal(result, false, "Ignored for a card that is not a picker");
    Assert.equal(recorded.length, 0, "Records nothing for that card");
  } finally {
    ToolUITelemetry.recordBrowserActionPromptToggleAll = originalRecord;
  }
});

/**
 * Test that ToolUI.handleUpdate fails confirmation without valid window
 */
add_task(async function test_handleUpdate_confirmation_no_window() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Test Tab" }],
    },
  };

  const updateData = {
    selectedTabs: [
      {
        linkedPanel: "panel-1",
        url: "https://example.com/",
        title: "Test Tab",
      },
    ],
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
      updateData,
    },
    conversation,
    null // No window provided
  );

  Assert.equal(
    result,
    false,
    "Should return false when no window provided for confirmation"
  );
});

/**
 * Test that ToolUI.handleUpdate successfully handles undo-tab-close
 */
add_task(async function test_handleUpdate_undo_tab_close_success() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Set up the message as if tabs were already closed
  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "ai-action-result",
    properties: {
      confirmedData: {
        selectedTabs: [
          {
            linkedPanel: "panel-1",
            url: "https://example.com/",
            title: "Test Tab",
          },
        ],
        operationIds: ["test-operation-123"],
      },
    },
  };

  // Mock the tabManagementService for undo
  const originalRestoreTabs = tabManagementService.restoreTabs;
  tabManagementService.restoreTabs = async function () {
    return {
      restoredCount: 1,
      requestedCount: 1,
      failedTabs: [],
    };
  };

  const mockWindow = {
    gBrowser: {
      tabs: [],
      selectedTab: null,
    },
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "undo-tab-close",
      updateData: {
        operationIds: ["test-operation-123"],
        selectedTabs: [
          {
            linkedPanel: "panel-1",
            url: "https://example.com/",
            title: "Test Tab",
          },
        ],
      },
    },
    conversation,
    mockWindow
  );

  // Restore the original function
  tabManagementService.restoreTabs = originalRestoreTabs;

  Assert.equal(result, true, "Should return true on successful undo");
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "ai-action-result",
    "Should keep uiType as ai-action-result"
  );
  Assert.equal(
    assistantMessage.toolUIData.properties.confirmedData.wasRestored,
    true,
    "Should mark as restored"
  );
});

/**
 * Test that ToolUI.handleUpdate fails undo without operationId
 */
add_task(async function test_handleUpdate_undo_tab_close_no_operation_id() {
  const conversation = new ChatConversation({});

  const mockWindow = {
    gBrowser: {
      tabs: [],
      selectedTab: null,
    },
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: "message-123",
      toolCallId: "test-tool-123",
      updateType: "undo-tab-close",
      updateData: {
        selectedTabs: [],
      },
    },
    conversation,
    mockWindow
  );

  Assert.equal(
    result,
    false,
    "Should return false when no operationId provided"
  );
});

/**
 * Test that ToolUI.handleUpdate returns false for unknown updateType
 */
add_task(async function test_handleUpdate_unknown_updateType() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Add toolUIData to the message
  const originalToolCallId = "test-tool-789";
  const originalUIData = {
    toolCallId: originalToolCallId,
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Test Tab" }],
    },
  };
  assistantMessage.toolUIData = { ...originalUIData };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: originalToolCallId,
      updateType: "invalid-update-type",
    },
    conversation,
    null
  );

  Assert.equal(result, false, "Should return false for unknown updateType");
  Assert.deepEqual(
    assistantMessage.toolUIData,
    originalUIData,
    "Should preserve original toolUIData when updateType is unknown"
  );
});

/**
 * Test closing tabs when only some tabs match verification
 */
add_task(async function test_closeSelectedTabs_partial_match() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [],
    },
  };

  const originalCloseTabs = tabManagementService.closeTabs;
  let closedTabs = null;
  tabManagementService.closeTabs = async function ({ tabs }) {
    closedTabs = tabs;
    return {
      operationId: "mock-operation-123",
      requestedCount: tabs.length,
      failedTabs: [],
    };
  };

  // One selection resolves to a live tab
  // the other's token points at a tab that is no longer open
  const liveTab = makeTab("https://example.com/", { label: "Test Tab 1" });
  const mockWindow = makeWindow([liveTab]);

  const tokenToKey = new Map();
  tokenToKey.set("tok-live", liveTab.permanentKey);
  tokenToKey.set("tok-stale", { id: "gone" });
  ToolUI.registerTabKeys("test-tool-123", tokenToKey);

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
      updateData: {
        selectedTabs: [
          {
            token: "tok-live",
            url: "https://example.com/",
            title: "Test Tab 1",
          },
          {
            token: "tok-stale",
            url: "https://mozilla.org/",
            title: "Test Tab 2",
          },
        ],
      },
    },
    conversation,
    mockWindow
  );

  tabManagementService.closeTabs = originalCloseTabs;

  Assert.equal(
    result,
    true,
    "Should return true when at least one tab matches"
  );
  Assert.equal(closedTabs.length, 1, "Should only close the resolvable tab");
  Assert.equal(
    closedTabs[0].linkedBrowser.currentURI.spec,
    "https://example.com/",
    "Should close the correct tab"
  );
});

/**
 * Test that unloaded/lazy tabs still resolve and close
 */
add_task(async function test_closeSelectedTabs_unloaded_tabs() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [],
    },
  };

  const originalCloseTabs = tabManagementService.closeTabs;
  let closedTabs = null;
  tabManagementService.closeTabs = async function ({ tabs }) {
    closedTabs = tabs;
    return {
      operationId: "mock-operation-123",
      requestedCount: tabs.length,
      failedTabs: [],
    };
  };

  // Unloaded/lazy tabs still retain a linkedBrowser with a permanentKey.
  const tabA = makeTab("https://example.com/", { label: "Example" });
  const tabB = makeTab("https://mozilla.org/", { label: "Mozilla" });
  const mockWindow = makeWindow([tabA, tabB]);

  const { selectedTabs } = registerSelection("test-tool-123", [tabA, tabB]);

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
      updateData: { selectedTabs },
    },
    conversation,
    mockWindow
  );

  tabManagementService.closeTabs = originalCloseTabs;

  Assert.equal(result, true, "Should succeed closing unloaded tabs");
  Assert.equal(closedTabs.length, 2, "Both unloaded tabs should resolve");
  Assert.deepEqual(
    closedTabs.map(t => t.linkedBrowser.currentURI.spec).sort(),
    ["https://example.com/", "https://mozilla.org/"],
    "Each selection resolves to its own unloaded tab"
  );
});

/**
 * Test that two unloaded tabs sharing the same URL and container each resolve
 * to a distinct tab
 */
add_task(async function test_closeSelectedTabs_duplicate_unloaded_tabs() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [],
    },
  };

  const originalCloseTabs = tabManagementService.closeTabs;
  let closedTabs = null;
  tabManagementService.closeTabs = async function ({ tabs }) {
    closedTabs = tabs;
    return {
      operationId: "mock-operation-123",
      requestedCount: tabs.length,
      failedTabs: [],
    };
  };

  // Two tabs at the same URL and container, distinguished only by permanentKey.
  const tab1 = makeTab("https://example.com/", { label: "Example" });
  const tab2 = makeTab("https://example.com/", { label: "Example" });
  const mockWindow = makeWindow([tab1, tab2]);

  const { selectedTabs } = registerSelection("test-tool-123", [tab1, tab2]);

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
      updateData: { selectedTabs },
    },
    conversation,
    mockWindow
  );

  tabManagementService.closeTabs = originalCloseTabs;

  Assert.equal(result, true, "Should succeed closing duplicate unloaded tabs");
  Assert.equal(closedTabs.length, 2, "Both duplicate tabs should resolve");
  Assert.notStrictEqual(
    closedTabs[0],
    closedTabs[1],
    "The two selections resolve to distinct tab objects"
  );
});

/**
 * Test that a selection for an unloaded tab does not match a loaded tab that
 * happens to share the same URL
 */
add_task(
  async function test_closeSelectedTabs_unloaded_does_not_steal_loaded() {
    const conversation = new ChatConversation({});
    conversation.addUserMessage("Test prompt", {});
    conversation.addAssistantMessage("text", "Test response");

    const assistantMessage = conversation.messages.find(
      m => m.role === 1 && m.content?.type === "text"
    );

    assistantMessage.toolUIData = {
      toolCallId: "test-tool-123",
      uiType: "website-confirmation",
      properties: {
        tabs: [],
      },
    };

    const originalCloseTabs = tabManagementService.closeTabs;
    let closedTabs = null;
    tabManagementService.closeTabs = async function ({ tabs }) {
      closedTabs = tabs;
      return {
        operationId: "mock-operation-123",
        requestedCount: tabs.length,
        failedTabs: [],
      };
    };

    // Two tabs share the same URL.
    // selecting one by its token must resolve to exactly that tab (permanentKey)
    const otherTab = makeTab("https://example.com/", { label: "Example" });
    const targetTab = makeTab("https://example.com/", { label: "Example" });
    const mockWindow = makeWindow([otherTab, targetTab]);

    const { selectedTabs } = registerSelection("test-tool-123", [targetTab]);

    const result = await ToolUI.handleUpdate(
      {
        messageId: assistantMessage.id,
        toolCallId: "test-tool-123",
        updateType: "confirmation-tab-selection",
        updateData: { selectedTabs },
      },
      conversation,
      mockWindow
    );

    tabManagementService.closeTabs = originalCloseTabs;

    Assert.equal(result, true, "Should succeed closing the selected tab");
    Assert.equal(closedTabs.length, 1, "Only one tab should resolve");
    Assert.strictEqual(
      closedTabs[0],
      targetTab,
      "Should resolve exactly the selected tab, not the same-URL sibling"
    );
  }
);

/**
 * Test when no tabs pass verification
 */
add_task(async function test_closeSelectedTabs_no_matches() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [],
    },
  };

  const originalCloseTabs = tabManagementService.closeTabs;
  let closeTabsCalled = false;
  tabManagementService.closeTabs = async function () {
    closeTabsCalled = true;
    return {
      operationId: "mock-operation-123",
      requestedCount: 1,
      failedTabs: [],
    };
  };

  const mockWindow = {
    gBrowser: {
      tabs: [], // No tabs available
      selectedTab: null,
    },
  };
  mockWindow.gBrowser.tabs.find = function () {
    return undefined;
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "confirmation-tab-selection",
      updateData: {
        selectedTabs: [
          {
            linkedPanel: "panel-1",
            url: "https://example.com/",
            title: "Test Tab",
          },
        ],
      },
    },
    conversation,
    mockWindow
  );

  tabManagementService.closeTabs = originalCloseTabs;

  Assert.equal(result, false, "Should return false when no tabs match");
  Assert.equal(
    closeTabsCalled,
    false,
    "closeTabs should not be called when no tabs match"
  );
});

/**
 * Test that undo fails gracefully when restoration fails
 */
add_task(async function test_undo_with_failed_restoration() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "ai-action-result",
    properties: {
      confirmedData: {
        selectedTabs: [],
        operationIds: ["test-operation-123"],
      },
    },
  };

  const originalRestoreTabs = tabManagementService.restoreTabs;
  tabManagementService.restoreTabs = async function () {
    throw new Error("Failed to restore tabs");
  };

  const mockWindow = {
    gBrowser: {
      tabs: [],
      selectedTab: null,
    },
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "undo-tab-close",
      updateData: {
        operationIds: ["test-operation-123"],
        selectedTabs: [],
      },
    },
    conversation,
    mockWindow
  );

  tabManagementService.restoreTabs = originalRestoreTabs;

  Assert.equal(result, false, "Should return false when restoration fails");
});

/**
 * Test ToolUI.closeSelectedTabs public method directly
 */
add_task(async function test_closeSelectedTabs_public_method() {
  // Mock the tabManagementService since closeSelectedTabs calls it internally
  // and we need to control its behavior in the test environment
  const originalCloseTabs = tabManagementService.closeTabs;
  let passedTabs = null;
  // Mock closeTabs to capture what tabs are passed and return a controlled result
  tabManagementService.closeTabs = async function ({ tabs }) {
    passedTabs = tabs; // Capture tabs to verify verification logic worked correctly
    return {
      operationId: "test-operation-456",
      closedTabs: tabs,
      requestedCount: tabs.length,
      failedTabs: [],
    };
  };

  const tabA = makeTab("https://example.com/", { label: "Example Tab" });
  const tabB = makeTab("https://mozilla.org/", { label: "Mozilla Tab" });
  const mockWindow = makeWindow([tabA, tabB]);

  const { selectedTabs, tokenToKey } = registerSelection("public-method", [
    tabA,
    tabB,
  ]);

  let result;
  try {
    result = await ToolUI.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      mockWindow
    );
  } finally {
    // Restore original function even if test throws
    tabManagementService.closeTabs = originalCloseTabs;
  }

  // Verify the method returns the tabManagementService result
  Assert.ok(result, "Should return a result object");
  Assert.deepEqual(
    result.operationIds,
    ["test-operation-456"],
    "Should return the operationIds from each closed window"
  );
  // Verify that only resolved tabs were passed to the service
  Assert.equal(
    passedTabs.length,
    2,
    "Should pass 2 resolved tabs to tabManagementService"
  );
  Assert.deepEqual(
    passedTabs.map(t => t.linkedBrowser.currentURI.spec),
    ["https://example.com/", "https://mozilla.org/"],
    "Should pass the resolved tabs in selection order"
  );
});

/**
 * Test ToolUI.closeSelectedTabs returns null when no window provided
 */
add_task(async function test_closeSelectedTabs_no_window() {
  const selectedTabsData = [
    {
      linkedPanel: "panel-1",
      url: "https://example.com/",
      title: "Example Tab",
    },
  ];

  const result = await ToolUI.closeSelectedTabs(selectedTabsData, null, null);

  Assert.equal(result, null, "Should return null when no window provided");
});

/**
 * Test ToolUI.closeSelectedTabs returns null when no valid tabs to close
 */
add_task(async function test_closeSelectedTabs_no_valid_tabs() {
  // Window has no tabs, so the selection's token resolves to nothing.
  const mockWindow = makeWindow([]);
  const missingTab = makeTab("https://example.com/", { label: "Example Tab" });
  const { selectedTabs, tokenToKey } = registerSelection("no-valid", [
    missingTab,
  ]);

  const result = await ToolUI.closeSelectedTabs(
    selectedTabs,
    tokenToKey,
    mockWindow
  );

  Assert.equal(result, null, "Should return null when no valid tabs found");
});

/**
 * Test that undo updates UI correctly with restore results
 */
add_task(async function test_undo_updates_ui_correctly() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  conversation.addAssistantMessage("text", "Test response");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  const originalSelectedTabs = [
    {
      linkedPanel: "panel-1",
      url: "https://example.com/",
      title: "Example Tab",
    },
    {
      linkedPanel: "panel-2",
      url: "https://mozilla.org/",
      title: "Mozilla Tab",
    },
  ];

  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "ai-action-result",
    properties: {
      confirmedData: {
        selectedTabs: originalSelectedTabs,
        operationIds: ["test-operation-123"],
      },
    },
  };

  const originalRestoreTabs = tabManagementService.restoreTabs;
  tabManagementService.restoreTabs = async function () {
    return {
      restoredCount: 2,
      requestedCount: 2,
      failedTabs: [],
    };
  };

  const mockWindow = {
    gBrowser: {
      tabs: [],
      selectedTab: null,
    },
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "test-tool-123",
      updateType: "undo-tab-close",
      updateData: {
        operationIds: ["test-operation-123"],
        selectedTabs: originalSelectedTabs,
      },
    },
    conversation,
    mockWindow
  );

  tabManagementService.restoreTabs = originalRestoreTabs;

  Assert.equal(result, true, "Should return true on successful undo");
  Assert.equal(
    assistantMessage.toolUIData.properties.confirmedData.wasRestored,
    true,
    "Should set wasRestored flag to true"
  );
  Assert.equal(
    assistantMessage.toolUIData.properties.confirmedData.restoredCount,
    2,
    "Should include restoredCount in update"
  );
  Assert.deepEqual(
    assistantMessage.toolUIData.properties.confirmedData.originalClosedTabs,
    originalSelectedTabs,
    "Should preserve original closed tabs data"
  );
});

/**
 * Test that autoCancelActiveConfirmation cancels active confirmations
 */
add_task(async function test_autoCancelActiveConfirmation_cancels() {
  const conversation = new ChatConversation({});

  // Add a user message and assistant message with confirmation UI
  conversation.addUserMessage("Close some tabs", {});
  conversation.addAssistantMessage("text", "I'll help you close some tabs.");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Set up the message with a website-confirmation
  // Include originalUserPrompt since it would be added by addUIToolToCurrentMessage
  assistantMessage.toolUIData = {
    toolCallId: "test-tool-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [
        { id: "tab1", label: "Test Tab 1" },
        { id: "tab2", label: "Test Tab 2" },
      ],
      originalUserPrompt: "Close some tabs",
    },
  };

  // Call autoCancelActiveConfirmation
  const result = await ToolUI.autoCancelActiveConfirmation(
    conversation,
    null,
    "sidebar"
  );

  // Verify a confirmation was cancelled
  Assert.equal(result, true, "Should return true when cancellation occurs");

  // Verify the confirmation was cancelled
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "cancelled-component",
    "Should change uiType to cancelled-component"
  );
  Assert.ok(
    assistantMessage.toolUIData.properties.tabs,
    "Should preserve original properties"
  );

  // Verify retry data was stored (it's always stored when original prompt is found)
  Assert.ok(
    conversation.pendingRetry,
    "Should set pendingRetry when original prompt is found"
  );
  Assert.equal(
    conversation.pendingRetry.originalUserPrompt,
    "Close some tabs",
    "Should store the original user prompt"
  );
});

/**
 * Test that autoCancelActiveConfirmation stores retry data with correct details
 */
add_task(async function test_autoCancelActiveConfirmation_with_retry() {
  const conversation = new ChatConversation({});

  // Add a user message and assistant message with confirmation UI
  conversation.addUserMessage("Close all example.com tabs", {});
  conversation.addAssistantMessage("text", "I'll help you close those tabs.");

  const assistantMessage = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );

  // Set up the message with a website-confirmation
  // Include originalUserPrompt since it would be added by addUIToolToCurrentMessage
  assistantMessage.toolUIData = {
    toolCallId: "test-tool-456",
    uiType: "website-confirmation",
    properties: {
      tabs: [
        { id: "tab1", label: "Example Tab 1" },
        { id: "tab2", label: "Example Tab 2" },
      ],
      originalUserPrompt: "Close all example.com tabs",
    },
  };

  // Call autoCancelActiveConfirmation
  const result = await ToolUI.autoCancelActiveConfirmation(
    conversation,
    null,
    "sidebar"
  );

  // Verify cancellation occurred
  Assert.equal(result, true, "Should return true when cancellation occurs");

  // Verify the confirmation was cancelled
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "cancelled-component",
    "Should change uiType to cancelled-component"
  );

  // Verify retry data was stored with correct details
  Assert.ok(conversation.pendingRetry, "Should set pendingRetry");
  Assert.equal(
    conversation.pendingRetry.originalUserPrompt,
    "Close all example.com tabs",
    "Should store the original user prompt"
  );
  Assert.equal(
    conversation.pendingRetry.cancelledMessageId,
    assistantMessage.id,
    "Should store the cancelled message ID"
  );
  Assert.equal(
    conversation.pendingRetry.cancelledToolCallId,
    "test-tool-456",
    "Should store the cancelled tool call ID"
  );
  Assert.strictEqual(
    typeof conversation.pendingRetry.timestamp,
    "number",
    "Should include a timestamp"
  );
});

/**
 * Test that autoCancelActiveConfirmation returns early when no active confirmation
 */
add_task(
  async function test_autoCancelActiveConfirmation_no_active_confirmation() {
    const conversation = new ChatConversation({});

    // Add messages without any confirmation UI
    conversation.addUserMessage("Just a regular message", {});
    conversation.addAssistantMessage("text", "Just a regular response.");

    // Call autoCancelActiveConfirmation
    const result = await ToolUI.autoCancelActiveConfirmation(
      conversation,
      null,
      "sidebar"
    );

    // Verify it returns false when no confirmation to cancel
    Assert.equal(
      result,
      false,
      "Should return false when no confirmation exists"
    );

    // Verify no retry data was stored
    Assert.equal(
      conversation.pendingRetry,
      null,
      "Should not set pendingRetry when no active confirmation exists"
    );

    // Verify messages remain unchanged
    const assistantMessage = conversation.messages.find(
      m => m.role === 1 && m.content?.type === "text"
    );
    Assert.equal(
      assistantMessage.toolUIData,
      null,
      "Should not modify messages without confirmations"
    );
  }
);

/**
 * Test that injectRetryToolUIDataIfNeeded injects retry UI
 */
add_task(async function test_injectRetryToolUIDataIfNeeded() {
  const conversation = new ChatConversation({});

  // Set up pendingRetry data
  conversation.pendingRetry = {
    originalUserPrompt: "Close all example.com tabs",
    cancelledMessageId: "cancelled-msg-123",
    cancelledToolCallId: "cancelled-tool-123",
    timestamp: Date.now(),
  };

  // Add new messages
  conversation.addUserMessage("What's the weather?", {});
  const assistantMessage = conversation.addAssistantMessage(
    "text",
    "I can't help with weather."
  );

  // Call injectRetryToolUIDataIfNeeded (parameters are swapped)
  const result = ToolUI.injectRetryToolUIDataIfNeeded(
    assistantMessage,
    conversation
  );

  // Verify injection occurred
  Assert.equal(result, true, "Should return true when injection occurs");

  // Verify retry UI was injected
  Assert.ok(assistantMessage.toolUIData, "Should inject toolUIData");
  Assert.equal(
    assistantMessage.toolUIData.uiType,
    "retry-component",
    "Should set uiType to retry-component"
  );
  Assert.equal(
    assistantMessage.toolUIData.properties.originalUserPrompt,
    "Close all example.com tabs",
    "Should include the original prompt in properties"
  );
  Assert.ok(
    assistantMessage.toolUIData.toolCallId,
    "Should generate a synthetic toolCallId"
  );

  // Verify pendingRetry was cleared
  Assert.equal(
    conversation.pendingRetry,
    null,
    "Should clear pendingRetry after injection"
  );
});

/**
 * Test that injectRetryToolUIDataIfNeeded doesn't inject when no pendingRetry
 */
add_task(async function test_injectRetryToolUIDataIfNeeded_no_pending() {
  const conversation = new ChatConversation({});

  // Don't set pendingRetry

  // Add new messages
  conversation.addUserMessage("What's the weather?", {});
  const assistantMessage = conversation.addAssistantMessage(
    "text",
    "I can't help with weather."
  );

  // Call injectRetryToolUIDataIfNeeded (parameters are swapped)
  const result = ToolUI.injectRetryToolUIDataIfNeeded(
    assistantMessage,
    conversation
  );

  // Verify no injection occurred
  Assert.equal(result, false, "Should return false when no pendingRetry");

  // Verify no UI was injected
  Assert.equal(
    assistantMessage.toolUIData,
    null,
    "Should not inject toolUIData when no pendingRetry"
  );
});

/**
 * Test that injectRetryToolUIDataIfNeeded doesn't inject for non-text assistant messages
 */
add_task(async function test_injectRetryToolUIDataIfNeeded_tool_message() {
  const conversation = new ChatConversation({});

  // Set up pendingRetry data
  conversation.pendingRetry = {
    originalUserPrompt: "Close all example.com tabs",
    cancelledMessageId: "cancelled-msg-123",
    cancelledToolCallId: "cancelled-tool-123",
    timestamp: Date.now(),
  };

  // Add a tool_use message (not text)
  conversation.addUserMessage("What's the weather?", {});
  const assistantMessage = conversation.addAssistantMessage("tool_use", {
    tool_name: "some_tool",
    tool_input: {},
  });

  // Call injectRetryToolUIDataIfNeeded (parameters are swapped)
  const result = ToolUI.injectRetryToolUIDataIfNeeded(
    assistantMessage,
    conversation
  );

  // Verify no injection occurred for tool_use message
  Assert.equal(
    result,
    false,
    "Should return false for non-text assistant messages"
  );

  // Verify no UI was injected
  Assert.equal(
    assistantMessage.toolUIData,
    null,
    "Should not inject toolUIData for tool_use messages"
  );

  // Verify pendingRetry was NOT cleared
  Assert.ok(
    conversation.pendingRetry,
    "Should not clear pendingRetry for non-text messages"
  );
});

/**
 * Test that injectRetryToolUIDataIfNeeded doesn't inject for user messages
 */
add_task(async function test_injectRetryToolUIDataIfNeeded_user_message() {
  const conversation = new ChatConversation({});

  // Set up pendingRetry data
  conversation.pendingRetry = {
    originalUserPrompt: "Close all example.com tabs",
    cancelledMessageId: "cancelled-msg-123",
    cancelledToolCallId: "cancelled-tool-123",
    timestamp: Date.now(),
  };

  // Add a user message (not assistant)
  const userMessage = conversation.addUserMessage("Another request", {});

  // Call injectRetryToolUIDataIfNeeded with user message
  const result = ToolUI.injectRetryToolUIDataIfNeeded(
    userMessage,
    conversation
  );

  // Verify no injection occurred for user message
  Assert.equal(result, false, "Should return false for user messages");

  // Verify no UI was injected
  Assert.equal(
    userMessage.toolUIData,
    null,
    "Should not inject toolUIData for user messages"
  );

  // Verify pendingRetry was NOT cleared
  Assert.ok(
    conversation.pendingRetry,
    "Should not clear pendingRetry for user messages"
  );
});

/**
 * Test that retry prompt handler clears UI and returns true
 */
add_task(async function test_handleUpdate_retry_prompt() {
  const conversation = new ChatConversation({});

  // Add messages with retry UI
  conversation.addUserMessage("New prompt", {});
  const assistantMessage = conversation.addAssistantMessage(
    "text",
    "Here's a response."
  );

  // Manually add retry toolUIData
  assistantMessage.toolUIData = {
    toolCallId: "retry-tool-123",
    uiType: "retry-component",
    properties: {
      prompt: "Original prompt to retry",
    },
  };

  // Call handleUpdate with retry-prompt updateType
  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "retry-tool-123",
      updateType: "retry-prompt",
      updateData: {
        prompt: "Original prompt to retry",
      },
    },
    conversation,
    null
  );

  // Verify the handler succeeded
  Assert.equal(result, true, "Should return true for retry-prompt update");

  // Verify the UI was cleared
  Assert.equal(
    assistantMessage.toolUIData,
    null,
    "Should clear toolUIData after retry-prompt"
  );
});

/**
 * Test that findOriginalUserPrompt is public and works correctly
 */
add_task(async function test_findOriginalUserPrompt_is_public() {
  const conversation = new ChatConversation({});

  // Add messages with parent chain
  const userMsg = conversation.addUserMessage("Test user prompt", {});
  const assistantMsg = conversation.addAssistantMessage("text", "Response");
  assistantMsg.parentMessageId = userMsg.id;

  // Test that the public method exists and works
  const originalPrompt = ToolUI.findOriginalUserPrompt(
    conversation.messages,
    assistantMsg
  );

  Assert.equal(
    originalPrompt,
    "Test user prompt",
    "Public findOriginalUserPrompt should return the user prompt"
  );
});

/**
 * Test that originalUserPrompt is added to website confirmations
 */
add_task(async function test_website_confirmation_gets_originalUserPrompt() {
  const conversation = new ChatConversation({});

  // Add user message
  conversation.addUserMessage("Close my tabs please", {});

  // Create assistant message to attach UI to
  const assistantMsg = conversation.addAssistantMessage(
    "text",
    "I'll help with that"
  );

  // Add website confirmation UI data
  const result = conversation.addUIToolToCurrentMessage("tool-123", {
    uiType: "website-confirmation",
    properties: {
      tabs: [{ id: "tab1", label: "Tab 1" }],
    },
  });

  Assert.ok(result.success, "Should successfully add UI tool");

  // Check that originalUserPrompt was added
  const updatedMessage = conversation.messages.find(
    m => m.id === assistantMsg.id
  );

  Assert.equal(
    updatedMessage.toolUIData.properties.originalUserPrompt,
    "Close my tabs please",
    "Should add originalUserPrompt to website confirmation properties"
  );
});

/**
 * Test that isRestored flag is preserved through message events
 */
add_task(async function test_isRestored_flag_preserved() {
  // NOTE: This is not a deep functional test but serves as a regression guard
  // to ensure the isRestored flag and originalUserPrompt continue to work together.
  // The actual integration is tested at the ai-window/ai-chat-content level.
  // This test documents the expected data structure and prevents accidental removal.
  const conversation = new ChatConversation({});

  // Add message with website confirmation
  conversation.addUserMessage("Close tabs", {});
  const assistantMsg = conversation.addAssistantMessage("text", "Closing tabs");

  assistantMsg.toolUIData = {
    toolCallId: "test-123",
    uiType: "website-confirmation",
    properties: {
      tabs: [],
      originalUserPrompt: "Close tabs",
    },
  };

  // Simulate what happens when message is restored
  const restoredMessage = {
    ...assistantMsg,
    isPreviousMessage: true,
    isRestored: true, // This would be set by ai-window when restoring
  };

  // Verify the flag exists
  Assert.ok(
    restoredMessage.isRestored,
    "Restored messages should have isRestored flag"
  );

  Assert.equal(
    restoredMessage.toolUIData.properties.originalUserPrompt,
    "Close tabs",
    "Original user prompt should be available in restored message"
  );
});

/**
 * Test that closeSelectedTabs tags the active tab with
 * smartWindowActionSource when it is among the verified tabs.
 */
add_task(async function test_closeSelectedTabs_tags_active_tab_source() {
  const originalCloseTabs = tabManagementService.closeTabs;
  tabManagementService.closeTabs = async function () {
    return { operationId: "op-active", requestedCount: 2, failedTabs: [] };
  };

  const activeTab = makeTab("https://example.com/", { label: "Active" });
  const otherTab = makeTab("https://mozilla.org/", { label: "Other" });
  const mockWindow = makeWindow([activeTab, otherTab], activeTab);

  const { selectedTabs, tokenToKey } = registerSelection("tags-active", [
    activeTab,
    otherTab,
  ]);

  try {
    await ToolUI.closeSelectedTabs(selectedTabs, tokenToKey, mockWindow);
  } finally {
    tabManagementService.closeTabs = originalCloseTabs;
  }

  Assert.equal(
    activeTab.smartWindowActionSource,
    "close_current_tab",
    "Active tab gets tagged with smartWindowActionSource"
  );
  Assert.equal(
    otherTab.smartWindowActionSource,
    undefined,
    "Non-active tabs are not tagged"
  );
});

/**
 * Test a selection spanning multiple windows closing each tab in the owning window.
 */
add_task(async function test_closeSelectedTabs_spans_multiple_windows() {
  const tabA = makeTab("https://example.com/", { label: "Example" });
  const windowA = makeWindow([tabA]);
  const tabB = makeTab("https://mozilla.org/", { label: "Mozilla" });
  const windowB = makeWindow([tabB]);
  const tabC = makeTab("https://example.org", { label: "Example Org" });
  const windowC = makeWindow([tabC]);

  // Record each closeTabs call so we can assert per-window dispatches
  const originalCloseTabs = tabManagementService.closeTabs;
  const closeCalls = [];
  tabManagementService.closeTabs = async function ({ tabs, window }) {
    closeCalls.push({ tabs, window });
    return {
      operationId: `op-${closeCalls.length}`,
      requestedCount: tabs.length,
      failedTabs: [],
    };
  };

  const { selectedTabs, tokenToKey } = registerSelection("multiple-tabs", [
    tabA,
    tabB,
    tabC,
  ]);

  let result;
  try {
    result = await ToolUI.closeSelectedTabs(selectedTabs, tokenToKey, windowA);
  } finally {
    tabManagementService.closeTabs = originalCloseTabs;
  }

  Assert.ok(result, "Should return a result for a cross-window selection");
  Assert.equal(result.requestedCount, 3, "Should account for all three tabs");
  Assert.equal(closeCalls.length, 3, "Should issue one close call per window");

  const byWindow = new Map(closeCalls.map(call => [call.window, call.tabs]));
  for (const [window, tab] of [
    [windowA, tabA],
    [windowB, tabB],
    [windowC, tabC],
  ]) {
    Assert.deepEqual(
      byWindow.get(window),
      [tab],
      `${tab.label} close call receives its own tab in its owning window`
    );
  }
});

/**
 * When the interacting window holds none of the selected tabs, createTabGroup
 * targets the window owning the most of them.
 */
add_task(async function test_createTabGroup_selects_owning_window() {
  const interactingWindow = makeWindow([]);
  const tabA = makeTab("https://example.com/", { label: "Example" });
  const tabB = makeTab("https://mozilla.org/", { label: "Mozilla" });
  const ownerWindow = makeWindow([tabA, tabB]);
  const tabC = makeTab("https://example.org", { label: "Example Org" });
  // A competing window owns a selected tab
  makeWindow([tabC]);

  const originalCreateTabGroup = tabManagementService.createTabGroup;
  let createCall = null;
  tabManagementService.createTabGroup = async function ({ tabs, window }) {
    createCall = { tabs, window };
    return { success: true, group: { id: "group-1" }, failedTabs: [] };
  };

  const { selectedTabs, tokenToKey } = registerSelection("group-tabs", [
    tabA,
    tabB,
    tabC,
  ]);

  let result;
  try {
    result = await ToolUI.createTabGroup({
      tabs: selectedTabs,
      tokenToKey,
      window: interactingWindow,
      label: "My group",
    });
  } finally {
    tabManagementService.createTabGroup = originalCreateTabGroup;
  }

  Assert.ok(result?.success, "Should create a group across windows");
  Assert.equal(
    createCall.window,
    ownerWindow,
    "Should target the window owning the most selected tabs"
  );
  Assert.deepEqual(
    createCall.tabs,
    [tabA, tabB],
    "Should only group the owning window's tabs"
  );
  Assert.deepEqual(
    result.failedTabs,
    [{ tab: tabC, reason: "other-window" }],
    "Tabs in other windows are reported as failed"
  );
});

/**
 * Undo restores each operationId per window and aggregates the results into
 * a telemetry record.
 */
add_task(async function test_undo_tab_close_multiple_operation_ids() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", {});
  const assistantMessage = conversation.addAssistantMessage(
    "text",
    "Test response"
  );
  assistantMessage.toolUIData = {
    toolCallId: "multi-undo",
    uiType: "ai-action-result",
    properties: {
      confirmedData: {
        selectedTabs: [],
        operationIds: ["op-1", "op-2"],
        actionType: "close_tabs",
      },
    },
  };

  const originalRestoreTabs = tabManagementService.restoreTabs;
  const restoredIds = [];
  tabManagementService.restoreTabs = async function ({ operationId }) {
    restoredIds.push(operationId);
    return { restoredCount: 1, requestedCount: 1, failedTabs: [] };
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "multi-undo",
      updateType: "undo-tab-close",
      updateData: { operationIds: ["op-1", "op-2"], selectedTabs: [] },
    },
    conversation,
    makeWindow([])
  );

  tabManagementService.restoreTabs = originalRestoreTabs;

  Assert.equal(result, true, "Should return true on multi-window undo");
  Assert.deepEqual(
    restoredIds,
    ["op-1", "op-2"],
    "Should restore each operation"
  );
  Assert.equal(
    assistantMessage.toolUIData.properties.confirmedData.restoredCount,
    2,
    "Should aggregate restoredCount across operations"
  );
});

/**
 * Test close tab undo where one operation restores and another fails.
 */
add_task(async function test_undo_tab_close_partial_success() {
  const operationIds = ["op-ok", "op-fail"];
  const message = {
    id: "msg-1",
    toolUIData: {
      toolCallId: "undo",
      uiType: "ai-action-result",
      properties: { confirmedData: { operationIds } },
    },
  };
  const conversation = {
    id: "chat-1",
    messages: [message],
    get messageCount() {
      return this.messages.length;
    },
    updateToolUI: (m, data) => (m.toolUIData = data),
  };

  const originalRestoreTabs = tabManagementService.restoreTabs;
  const originalRecordUndo = ToolUITelemetry.recordBrowserActionUndo;
  tabManagementService.restoreTabs = async ({ operationId }) =>
    operationId === "op-ok"
      ? { restoredCount: 1, requestedCount: 1, failedTabs: [] }
      : { restoredCount: 0, requestedCount: 1, failedTabs: [{ tab: {} }] };
  let undo;
  ToolUITelemetry.recordBrowserActionUndo = data => (undo = data);

  const result = await ToolUI.handleUpdate(
    {
      messageId: message.id,
      toolCallId: "undo",
      updateType: "undo-tab-close",
      updateData: { operationIds },
    },
    conversation,
    makeWindow([])
  );

  tabManagementService.restoreTabs = originalRestoreTabs;
  ToolUITelemetry.recordBrowserActionUndo = originalRecordUndo;

  Assert.equal(result, true, "Should return true when some tabs restored");
  Assert.equal(undo.result, "partial_success", "Reports partial success");
  Assert.equal(undo.tabs_restored, 1, "Aggregates the restored count");
  Assert.equal(
    undo.error,
    "one_or_more_tabs_failed_to_restore",
    "Flags that some tabs failed to restore"
  );
});

/**
 * Undo for group_tabs ungroups each operationId and marks the card as restored.
 */
add_task(async function test_undo_tab_group_uses_operation_ids() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Group my tabs", {});
  const assistantMessage = conversation.addAssistantMessage(
    "text",
    "Grouped tabs"
  );
  assistantMessage.toolUIData = {
    toolCallId: "group-undo",
    uiType: "ai-action-result",
    properties: {
      confirmedData: {
        selectedTabs: [],
        operationIds: ["group-1"],
        actionType: "group_tabs",
      },
    },
  };

  const originalUngroupTabs = tabManagementService.ungroupTabs;
  const ungroupedIds = [];
  tabManagementService.ungroupTabs = async function ({ groupId }) {
    ungroupedIds.push(groupId);
    return { success: true, ungroupedTabs: [{ url: "https://example.com/" }] };
  };

  const result = await ToolUI.handleUpdate(
    {
      messageId: assistantMessage.id,
      toolCallId: "group-undo",
      updateType: "undo-tab-group",
      updateData: { operationIds: ["group-1"] },
    },
    conversation,
    makeWindow([])
  );

  tabManagementService.ungroupTabs = originalUngroupTabs;

  Assert.equal(result, true, "Should return true on group undo");
  Assert.deepEqual(
    ungroupedIds,
    ["group-1"],
    "Should ungroup each operationId"
  );
  Assert.equal(
    assistantMessage.toolUIData.properties.confirmedData.wasRestored,
    true,
    "Should mark the group card as restored"
  );
});

/**
 * Test failed ungroup records error telemetry.
 */
add_task(async function test_undo_tab_group_error_telemetry() {
  const operationIds = ["group-1", "group-2"];
  const message = {
    id: "msg-1",
    toolUIData: {
      toolCallId: "undo",
      uiType: "ai-action-result",
      properties: { confirmedData: { operationIds } },
    },
  };
  const conversation = {
    id: "chat-1",
    messages: [message],
    get messageCount() {
      return this.messages.length;
    },
    updateToolUI: (m, data) => (m.toolUIData = data),
  };

  const originalUngroupTabs = tabManagementService.ungroupTabs;
  const originalRecordUndo = ToolUITelemetry.recordBrowserActionUndo;
  tabManagementService.ungroupTabs = async ({ groupId }) =>
    groupId === "group-1"
      ? { success: true, ungroupedTabs: [{}, {}, {}] }
      : { success: false, error: "group_not_found" };
  let undo;
  ToolUITelemetry.recordBrowserActionUndo = data => (undo = data);

  const result = await ToolUI.handleUpdate(
    {
      messageId: message.id,
      toolCallId: "undo",
      updateType: "undo-tab-group",
      updateData: { operationIds, actionTimestamp: 1 },
    },
    conversation,
    makeWindow([])
  );

  tabManagementService.ungroupTabs = originalUngroupTabs;
  ToolUITelemetry.recordBrowserActionUndo = originalRecordUndo;

  Assert.equal(result, false, "Should return false when an operation fails");
  Assert.equal(undo.action, "group_tabs", "Records the group action type");
  Assert.equal(undo.result, "error", "Records an error result");
  Assert.equal(undo.error, "group_not_found", "Propagates the failure reason");
  Assert.equal(
    undo.tabs_restored,
    0,
    "Does not count tabs from earlier successful operations"
  );
  Assert.equal(undo.chat_id, conversation.id, "Records the conversation id");
  Assert.equal(
    undo.message_seq,
    conversation.messages.length,
    "Records the message sequence"
  );
  Assert.greaterOrEqual(
    undo.time_delta,
    0,
    "Records a non-negative time delta"
  );
});

/**
 * Test ToolUI.openAndGroupTabs returns null for an empty tabs array
 */
add_task(async function test_openAndGroupTabs_no_tabs_returns_null() {
  const result = await ToolUI.openAndGroupTabs({
    tabs: [],
    window: makeWindow([]),
  });

  Assert.equal(result, null, "Should return null for an empty tabs array");
});

/**
 * Test ToolUI.openAndGroupTabs carries mergedCount through from
 * resolveOrOpenTabs into the final result
 */
add_task(async function test_openAndGroupTabs_merges_mergedCount() {
  const mockWindow = makeWindow([]);

  const originalResolve = tabManagementService.resolveOrOpenTabs;
  const originalCreateGroup = tabManagementService.createTabGroup;
  tabManagementService.resolveOrOpenTabs = async () => ({
    resolvedTabs: [makeTab("https://example.com/")],
    mergedCount: 1,
    failedUrls: [],
  });
  tabManagementService.createTabGroup = async () => ({
    success: true,
    group: { id: "g1", label: "L", color: "blue", tabCount: 1 },
    failedTabs: [],
  });

  let result;
  try {
    result = await ToolUI.openAndGroupTabs({
      tabs: [{ url: "https://example.com/" }],
      window: mockWindow,
      label: "L",
    });
  } finally {
    tabManagementService.resolveOrOpenTabs = originalResolve;
    tabManagementService.createTabGroup = originalCreateGroup;
  }

  Assert.ok(result.success, "Should succeed");
  Assert.equal(
    result.mergedCount,
    1,
    "Should carry mergedCount through from resolveOrOpenTabs"
  );
});

// Calls openAndGroupTabs with the tab services stubbed out, and gives back both
// the tabs it tried to group and the result.
async function captureGroupedTabs({ resolvedTabs, ...options }) {
  const originalResolve = tabManagementService.resolveOrOpenTabs;
  const originalCreateGroup = tabManagementService.createTabGroup;
  let groupedTabs = null;

  tabManagementService.resolveOrOpenTabs = async () => ({
    resolvedTabs,
    mergedCount: 0,
    failedUrls: [],
  });
  tabManagementService.createTabGroup = async ({ tabs }) => {
    groupedTabs = tabs;
    return {
      success: true,
      group: { id: "g1", label: "L", color: "blue", tabCount: tabs.length },
      failedTabs: [],
    };
  };

  try {
    const result = await ToolUI.openAndGroupTabs(options);
    return { groupedTabs, result };
  } finally {
    tabManagementService.resolveOrOpenTabs = originalResolve;
    tabManagementService.createTabGroup = originalCreateGroup;
  }
}

/**
 * Test ToolUI.openAndGroupTabs puts the originating chat tab in the group it
 * creates, ahead of the tabs that were just opened (bug 2068714)
 */
add_task(async function test_openAndGroupTabs_includes_origin_tab() {
  const openedTab = makeTab("https://example.com/");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html", {
    label: "Smart Window",
  });
  const win = makeWindow([chatTab, openedTab], chatTab);

  const { groupedTabs, result } = await captureGroupedTabs({
    resolvedTabs: [openedTab],
    tabs: [{ url: "https://example.com/" }],
    window: win,
    label: "L",
    originTab: chatTab,
  });

  Assert.deepEqual(
    groupedTabs,
    [chatTab, openedTab],
    "Should group the chat tab ahead of the opened tab"
  );
  Assert.equal(
    result.mergedCount,
    0,
    "Should not count the chat tab as a merged tab"
  );
});

/**
 * Test ToolUI.openAndGroupTabs leaves the group untouched when there is no
 * originating tab, which is the case for sidebar conversations
 */
add_task(async function test_openAndGroupTabs_without_origin_tab() {
  const openedTab = makeTab("https://example.com/");

  const { groupedTabs } = await captureGroupedTabs({
    resolvedTabs: [openedTab],
    tabs: [{ url: "https://example.com/" }],
    window: makeWindow([]),
    label: "L",
  });

  Assert.deepEqual(
    groupedTabs,
    [openedTab],
    "Should group only the opened tab when there is no origin tab"
  );
});

/**
 * Test ToolUI.openAndGroupTabs does not add the originating tab twice when it
 * is already among the resolved tabs
 */
add_task(async function test_openAndGroupTabs_origin_tab_not_duplicated() {
  const openedTab = makeTab("https://example.com/");
  const win = makeWindow([openedTab]);
  openedTab.documentGlobal = win;

  const { groupedTabs } = await captureGroupedTabs({
    resolvedTabs: [openedTab],
    tabs: [{ url: "https://example.com/" }],
    window: win,
    label: "L",
    originTab: openedTab,
  });

  Assert.deepEqual(
    groupedTabs,
    [openedTab],
    "Should not duplicate a resolved tab that is also the origin tab"
  );
});

/**
 * Test a chat tab that is half of a split view is left out, since grouping it
 * would move the whole split view and drag in its partner tab
 */
add_task(async function test_openAndGroupTabs_skips_split_view_origin_tab() {
  const openedTab = makeTab("https://example.com/");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const win = makeWindow([chatTab, openedTab], chatTab);
  chatTab.splitview = { id: "split-1" };

  const { groupedTabs } = await captureGroupedTabs({
    resolvedTabs: [openedTab],
    tabs: [{ url: "https://example.com/" }],
    window: win,
    label: "L",
    originTab: chatTab,
  });

  Assert.deepEqual(
    groupedTabs,
    [openedTab],
    "Should not group a chat tab that is in a split view"
  );
});

/**
 * Test the originating tab passed to handleUpdate reaches the created group,
 * covering the plumbing from ai-window through to the grouping service
 */
add_task(async function test_handleUpdate_open_tabs_groups_origin_tab() {
  const toolCallId = "test-open-tabs-origin";
  const { conversation, assistantMessage } =
    makeOpenTabsConfirmation(toolCallId);

  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const mockWindow = makeWindow([chatTab], chatTab);
  const selectedTabs = [
    { url: "https://example.com/", title: "Example" },
    { url: "https://mozilla.org/", title: "Mozilla" },
  ];

  const originalResolve = tabManagementService.resolveOrOpenTabs;
  const originalCreateGroup = tabManagementService.createTabGroup;
  const originalFindChatTab = ToolUI.findChatTab;
  let groupedTabs = null;
  // The real lookup needs a live tab-state manager, which mock tabs cannot
  // provide; browser_smartwindow_manage_tabs_tool.js covers it for real.
  ToolUI.findChatTab = () => chatTab;
  tabManagementService.resolveOrOpenTabs = async () => {
    // resolveOrOpenTabs hands back tabs that are in the window
    const opened = [makeTab(selectedTabs[0].url), makeTab(selectedTabs[1].url)];
    opened.forEach(tab => {
      tab.documentGlobal = mockWindow;
    });
    return { resolvedTabs: opened, mergedCount: 0, failedUrls: [] };
  };
  tabManagementService.createTabGroup = async ({ tabs }) => {
    groupedTabs = tabs;
    return {
      success: true,
      group: { id: "group-1", label: "Trip", color: "blue", tabCount: 3 },
      failedTabs: [],
    };
  };

  let result;
  try {
    result = await ToolUI.handleUpdate(
      {
        messageId: assistantMessage.id,
        toolCallId,
        updateType: "confirm-open-and-group-tabs-selection",
        updateData: { selectedTabs, tabGroupLabel: "Trip" },
      },
      conversation,
      mockWindow,
      "fullpage"
    );
  } finally {
    tabManagementService.resolveOrOpenTabs = originalResolve;
    tabManagementService.createTabGroup = originalCreateGroup;
    ToolUI.findChatTab = originalFindChatTab;
  }

  Assert.equal(result, true, "Should return true on success");
  Assert.equal(groupedTabs.length, 3, "Should group the chat tab as well");
  Assert.equal(
    groupedTabs[0],
    chatTab,
    "Should lead the group with the chat tab"
  );

  // The card should still only mention the tabs the user picked, so adding the
  // chat tab does not change the numbers they see.
  const confirmedData = assistantMessage.toolUIData.properties.confirmedData;
  Assert.equal(
    confirmedData.selectedTabs.length,
    2,
    "Should leave the user's selection untouched"
  );
});

/**
 * Test ToolUI.openOrSwitchToTab switches to an already-open tab without
 * navigating
 */
add_task(async function test_openOrSwitchToTab_switches_when_open() {
  const existingTab = makeTab("https://example.com/");
  const mockWindow = makeWindow([existingTab]);

  const originalFind = tabManagementService.findOpenTab;
  const originalSwitch = tabManagementService.switchToTab;
  let switchedTo = null;
  tabManagementService.findOpenTab = () => existingTab;
  tabManagementService.switchToTab = ({ tab }) => {
    switchedTo = tab;
  };

  let result;
  try {
    result = await ToolUI.openOrSwitchToTab({
      tab: { url: "https://example.com/" },
      window: mockWindow,
    });
  } finally {
    tabManagementService.findOpenTab = originalFind;
    tabManagementService.switchToTab = originalSwitch;
  }

  Assert.deepEqual(
    result,
    { success: true, switched: true },
    "Should report success and switched"
  );
  Assert.equal(switchedTo, existingTab, "Should switch to the found tab");
});

/**
 * Test ToolUI.openOrSwitchToTab when the tab isn't already open: it should
 * open a new tab and switch to it, without navigating the current tab.
 */
add_task(
  async function test_openOrSwitchToTab_opens_and_switches_when_not_open() {
    const mockWindow = makeWindow([]);
    const newTab = makeTab("https://example.com/");

    const originalFind = tabManagementService.findOpenTab;
    const originalOpen = tabManagementService.openTabs;
    const originalSwitch = tabManagementService.switchToTab;
    let switchedTo = null;
    tabManagementService.findOpenTab = () => null;
    tabManagementService.openTabs = ({ urls }) => {
      Assert.deepEqual(
        urls,
        ["https://example.com/"],
        "Should open the tab's URL"
      );
      return { openedTabs: [newTab], failedUrls: [] };
    };
    tabManagementService.switchToTab = ({ tab }) => {
      switchedTo = tab;
    };

    let result;
    try {
      result = await ToolUI.openOrSwitchToTab({
        tab: { url: "https://example.com/" },
        window: mockWindow,
      });
    } finally {
      tabManagementService.findOpenTab = originalFind;
      tabManagementService.openTabs = originalOpen;
      tabManagementService.switchToTab = originalSwitch;
    }

    Assert.deepEqual(
      result,
      { success: true, switched: false },
      "Should report success without being marked as switched"
    );
    Assert.equal(switchedTo, newTab, "Should switch to the newly-opened tab");
  }
);

/**
 * Test ToolUI.openOrSwitchToTab when opening the new tab fails
 */
add_task(
  async function test_openOrSwitchToTab_reports_failure_when_open_fails() {
    const mockWindow = makeWindow([]);

    const originalFind = tabManagementService.findOpenTab;
    const originalOpen = tabManagementService.openTabs;
    tabManagementService.findOpenTab = () => null;
    tabManagementService.openTabs = () => ({
      openedTabs: [],
      failedUrls: [{ url: "https://example.com/", reason: "boom" }],
    });

    let result;
    try {
      result = await ToolUI.openOrSwitchToTab({
        tab: { url: "https://example.com/" },
        window: mockWindow,
      });
    } finally {
      tabManagementService.findOpenTab = originalFind;
      tabManagementService.openTabs = originalOpen;
    }

    Assert.deepEqual(
      result,
      { success: false, switched: false },
      "Should report failure when the tab could not be opened"
    );
  }
);

/**
 * Test the full CONFIRM_OPEN_AND_GROUP_TABS_SELECTION flow for multiple
 * tabs: creates a group and carries the result through to confirmedData
 */
add_task(async function test_handleUpdate_open_tabs_multi_success() {
  const toolCallId = "test-open-tabs-multi";
  const { conversation, assistantMessage } =
    makeOpenTabsConfirmation(toolCallId);

  const mockWindow = makeWindow([]);
  const selectedTabs = [
    { url: "https://example.com/", title: "Example" },
    { url: "https://mozilla.org/", title: "Mozilla" },
  ];

  const originalResolve = tabManagementService.resolveOrOpenTabs;
  const originalCreateGroup = tabManagementService.createTabGroup;
  tabManagementService.resolveOrOpenTabs = async () => ({
    resolvedTabs: [makeTab(selectedTabs[0].url), makeTab(selectedTabs[1].url)],
    mergedCount: 1,
    failedUrls: [],
  });
  tabManagementService.createTabGroup = async () => ({
    success: true,
    group: { id: "group-1", label: "Trip", color: "blue", tabCount: 2 },
    failedTabs: [],
  });

  let result;
  try {
    result = await ToolUI.handleUpdate(
      {
        messageId: assistantMessage.id,
        toolCallId,
        updateType: "confirm-open-and-group-tabs-selection",
        updateData: { selectedTabs, tabGroupLabel: "Trip" },
      },
      conversation,
      mockWindow
    );
  } finally {
    tabManagementService.resolveOrOpenTabs = originalResolve;
    tabManagementService.createTabGroup = originalCreateGroup;
  }

  Assert.equal(result, true, "Should return true on success");

  const confirmedData = assistantMessage.toolUIData.properties.confirmedData;
  Assert.equal(
    confirmedData.actionType,
    "open_tabs",
    "Should record actionType"
  );
  Assert.deepEqual(
    confirmedData.operationIds,
    ["group-1"],
    "Should record the created group's id as operationIds"
  );
  Assert.equal(
    confirmedData.mergedCount,
    1,
    "Should carry mergedCount through"
  );
  Assert.equal(
    confirmedData.switched,
    false,
    "Should not be marked as switched for a multi-tab action"
  );
});

/**
 * Test the full CONFIRM_OPEN_AND_GROUP_TABS_SELECTION flow for a single
 * already-open tab: switches instead of grouping or navigating
 */
add_task(async function test_handleUpdate_open_tabs_single_switches() {
  const toolCallId = "test-open-tabs-switch";
  const { conversation, assistantMessage } =
    makeOpenTabsConfirmation(toolCallId);

  const existingTab = makeTab("https://example.com/", { label: "Example" });
  const mockWindow = makeWindow([existingTab]);
  const selectedTabs = [{ url: "https://example.com/", title: "Example" }];

  const originalFind = tabManagementService.findOpenTab;
  const originalSwitch = tabManagementService.switchToTab;
  const originalOpen = tabManagementService.openTabs;
  let switchCalled = false;
  let openCalled = false;
  tabManagementService.findOpenTab = () => existingTab;
  tabManagementService.switchToTab = () => {
    switchCalled = true;
  };
  tabManagementService.openTabs = () => {
    openCalled = true;
    return { openedTabs: [], failedUrls: [] };
  };

  let result;
  try {
    result = await ToolUI.handleUpdate(
      {
        messageId: assistantMessage.id,
        toolCallId,
        updateType: "confirm-open-and-group-tabs-selection",
        updateData: { selectedTabs, tabGroupLabel: "Trip" },
      },
      conversation,
      mockWindow
    );
  } finally {
    tabManagementService.findOpenTab = originalFind;
    tabManagementService.switchToTab = originalSwitch;
    tabManagementService.openTabs = originalOpen;
  }

  Assert.equal(result, true, "Should return true on success");
  Assert.ok(switchCalled, "Should switch to the existing tab");
  Assert.ok(!openCalled, "Should not open a new tab when switching");

  const confirmedData = assistantMessage.toolUIData.properties.confirmedData;
  Assert.equal(confirmedData.switched, true, "Should record switched");
  Assert.equal(
    confirmedData.group,
    null,
    "Should not create a group for a single tab"
  );
});

/**
 * Test the full CONFIRM_OPEN_AND_GROUP_TABS_SELECTION flow for a single
 * not-yet-open tab: opens a new tab and switches to it, rather than
 * navigating the tab hosting the conversation itself
 */
add_task(async function test_handleUpdate_open_tabs_single_opens_new_tab() {
  const toolCallId = "test-open-tabs-open";
  const { conversation, assistantMessage } =
    makeOpenTabsConfirmation(toolCallId);

  const mockWindow = makeWindow([]);
  const selectedTabs = [{ url: "https://example.com/", title: "Example" }];
  const newTab = makeTab("https://example.com/");

  const originalFind = tabManagementService.findOpenTab;
  const originalOpen = tabManagementService.openTabs;
  const originalSwitch = tabManagementService.switchToTab;
  let switchedTo = null;
  tabManagementService.findOpenTab = () => null;
  tabManagementService.openTabs = () => ({
    openedTabs: [newTab],
    failedUrls: [],
  });
  tabManagementService.switchToTab = ({ tab }) => {
    switchedTo = tab;
  };

  let result;
  try {
    result = await ToolUI.handleUpdate(
      {
        messageId: assistantMessage.id,
        toolCallId,
        updateType: "confirm-open-and-group-tabs-selection",
        updateData: { selectedTabs, tabGroupLabel: "Trip" },
      },
      conversation,
      mockWindow
    );
  } finally {
    tabManagementService.findOpenTab = originalFind;
    tabManagementService.openTabs = originalOpen;
    tabManagementService.switchToTab = originalSwitch;
  }

  Assert.equal(result, true, "Should return true on success");
  Assert.equal(switchedTo, newTab, "Should switch to the newly-opened tab");

  const confirmedData = assistantMessage.toolUIData.properties.confirmedData;
  Assert.equal(
    confirmedData.switched,
    false,
    "Should not be marked as switched"
  );
});

/**
 * Test a tab that goes away between the confirmation card and the confirm is
 * not counted as grouped. It is dropped before grouping and never reaches
 * failedTabs, so counting the selection would report it as a success.
 */
add_task(async function test_confirm_tab_group_skips_a_closed_tab() {
  const toolCallId = "group-closed-tab";
  const conversation = new ChatConversation({});
  conversation.addAssistantMessage("text", "Confirm?");
  const message = conversation.messages.find(
    m => m.role === 1 && m.content?.type === "text"
  );
  message.toolUIData = {
    toolCallId,
    uiType: "tab-group-confirmation",
    properties: { actionType: "group_tabs" },
  };
  conversation.stashPendingBrowserActionTelemetry(toolCallId, {
    action: "group_tabs",
  });

  const tabA = makeTab("https://example.com/a");
  const tabB = makeTab("https://example.com/b");
  const closedTab = makeTab("https://example.com/c");
  const mockWindow = makeWindow([tabA, tabB, closedTab]);
  const { selectedTabs } = registerSelection(toolCallId, [
    tabA,
    tabB,
    closedTab,
  ]);

  // The third tab is gone by the time the user confirms.
  mockWindow.gBrowser.tabs = [tabA, tabB];

  const originalCreateGroup = tabManagementService.createTabGroup;
  const originalRecord = ToolUITelemetry.recordBrowserActionComplete;
  let complete = null;
  tabManagementService.createTabGroup = async ({ tabs }) => ({
    success: true,
    group: { id: "g1", label: "L", color: "blue", tabCount: tabs.length },
    failedTabs: [],
  });
  ToolUITelemetry.recordBrowserActionComplete = data => (complete = data);

  try {
    await ToolUI.handleUpdate(
      {
        messageId: message.id,
        toolCallId,
        updateType: "confirm-tab-group-selection",
        updateData: { selectedTabs, tabGroupLabel: "L" },
      },
      conversation,
      mockWindow,
      "fullpage"
    );
  } finally {
    tabManagementService.createTabGroup = originalCreateGroup;
    ToolUITelemetry.recordBrowserActionComplete = originalRecord;
  }

  Assert.equal(
    complete.tabs_affected,
    2,
    "Only the tabs that made it into the group are counted"
  );
  Assert.equal(
    complete.result,
    "partial_success",
    "A tab that went away is not reported as a success"
  );
});

/**
 * Test the chat tab is left out when none of the requested tabs can be
 * grouped, so we do not end up creating a group that holds only the chat tab
 */
add_task(async function test_openAndGroupTabs_skips_chat_tab_when_none_group() {
  const closingTab = makeTab("https://example.com/closing");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const win = makeWindow([chatTab, closingTab], chatTab);
  closingTab.closing = true;

  const { groupedTabs } = await captureGroupedTabs({
    resolvedTabs: [closingTab],
    tabs: [{ url: "https://example.com/closing" }],
    window: win,
    label: "L",
    originTab: chatTab,
  });

  Assert.deepEqual(
    groupedTabs,
    [closingTab],
    "Should not add the chat tab when no requested tab can be grouped"
  );
});

/**
 * Test the chat tab still joins when only some of the requested tabs can be
 * grouped
 */
add_task(async function test_openAndGroupTabs_adds_chat_tab_when_some_group() {
  const closingTab = makeTab("https://example.com/closing");
  const openTab = makeTab("https://example.com/ok");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const win = makeWindow([chatTab, closingTab, openTab], chatTab);
  closingTab.closing = true;

  const { groupedTabs } = await captureGroupedTabs({
    resolvedTabs: [closingTab, openTab],
    tabs: [{ url: "https://example.com/ok" }],
    window: win,
    label: "L",
    originTab: chatTab,
  });

  Assert.deepEqual(
    groupedTabs,
    [chatTab, closingTab, openTab],
    "Should add the chat tab when at least one requested tab can be grouped"
  );
});

/**
 * Test a pinned or grouped chat tab is not moved into the group
 */
add_task(async function test_openAndGroupTabs_keeps_chat_tab_in_place() {
  for (const [name, state] of [
    ["pinned", { pinned: true }],
    ["grouped", { group: { id: "other" } }],
  ]) {
    const openTab = makeTab("https://example.com/ok");
    const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
    const win = makeWindow([chatTab, openTab], chatTab);
    Object.assign(chatTab, state);

    const { groupedTabs } = await captureGroupedTabs({
      resolvedTabs: [openTab],
      tabs: [{ url: "https://example.com/ok" }],
      window: win,
      label: "L",
      originTab: chatTab,
    });

    Assert.deepEqual(
      groupedTabs,
      [openTab],
      `Should not move a ${name} chat tab`
    );
  }
});

/**
 * Test a group is not created out of the chat tab alone when every other tab
 * the user confirmed turns out to be ungroupable
 */
add_task(async function test_createTabGroup_drops_a_lone_chat_tab() {
  const closingTab = makeTab("https://example.com/a");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const win = makeWindow([chatTab, closingTab]);
  // The tab the user asked for is closing.
  closingTab.closing = true;

  const { selectedTabs, tokenToKey } = registerSelection("lone-chat", [
    closingTab,
    chatTab,
  ]);
  selectedTabs.at(-1).isChatTab = true;

  const originalCreateTabGroup = tabManagementService.createTabGroup;
  let createCall = null;
  tabManagementService.createTabGroup = async function ({ tabs }) {
    createCall = tabs;
    return { success: false, group: null, failedTabs: [], error: "none" };
  };

  try {
    await ToolUI.createTabGroup({
      tabs: selectedTabs,
      tokenToKey,
      window: win,
      label: "L",
    });
  } finally {
    tabManagementService.createTabGroup = originalCreateTabGroup;
  }

  Assert.ok(
    !createCall.includes(chatTab),
    "Should not group the chat tab on its own"
  );
});

/**
 * Test the chat tab is still grouped when at least one of the tabs the user
 * confirmed can be grouped
 */
add_task(async function test_createTabGroup_keeps_chat_tab_with_others() {
  const openTab = makeTab("https://example.com/a");
  const chatTab = makeTab("chrome://browser/content/aiwindow/aiWindow.html");
  const win = makeWindow([chatTab, openTab]);

  const { selectedTabs, tokenToKey } = registerSelection("with-others", [
    openTab,
    chatTab,
  ]);
  selectedTabs.at(-1).isChatTab = true;

  const originalCreateTabGroup = tabManagementService.createTabGroup;
  let createCall = null;
  tabManagementService.createTabGroup = async function ({ tabs }) {
    createCall = tabs;
    return {
      success: true,
      group: { id: "g1", tabCount: tabs.length },
      failedTabs: [],
    };
  };

  try {
    await ToolUI.createTabGroup({
      tabs: selectedTabs,
      tokenToKey,
      window: win,
      label: "L",
    });
  } finally {
    tabManagementService.createTabGroup = originalCreateTabGroup;
  }

  Assert.ok(createCall.includes(chatTab), "Should still group the chat tab");
  Assert.equal(createCall.length, 2, "Both tabs go into the group");
});
