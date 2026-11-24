/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Helper function to safely get the aichat browser element
 * Waits for ai-window, then shadowRoot, then #aichat-browser
 *
 * @param {object} browser - The browser element
 * @returns {Promise<object>} The aichat browser element
 */
async function getAichatBrowser(browser) {
  // Wait for ai-window element to exist
  const aiWindowEl = await TestUtils.waitForCondition(
    () => browser.contentDocument?.querySelector("ai-window"),
    "Wait for ai-window element to exist"
  );

  // Wait for shadowRoot to be available
  await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot,
    "Wait for ai-window shadowRoot to be ready"
  );

  // Wait for aichat-browser to be available
  const aichatBrowser = await TestUtils.waitForCondition(
    () => aiWindowEl.shadowRoot.querySelector("#aichat-browser"),
    "Wait for aichat-browser element"
  );

  // Ensure it's loaded with the correct URL
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

/**
 * Helper function to set up a conversation with tool UI data
 * This simulates the LLM response containing tool UI information
 * without directly manipulating implementation details
 *
 * @param {object} aichatBrowser - The aichat browser element
 * @param {object} toolUIData - The tool UI data configuration
 */
async function setupConversationWithToolUI(aichatBrowser, toolUIData) {
  await SpecialPowers.spawn(aichatBrowser, [toolUIData], async data => {
    const chatContent = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-chat-content"),
      "Wait for ai-chat-content element"
    );

    const chatContentJS = chatContent.wrappedJSObject || chatContent;
    await chatContent.updateComplete;

    // Simulate receiving tool UI data from the LLM
    // This represents the state after the LLM has responded with tool information
    const messages = [
      {
        role: "user",
        body: data.userMessage,
        ordinal: 0,
        convId: "test-conv",
      },
      {
        role: "assistant",
        body: data.assistantMessage,
        ordinal: 1,
        convId: "test-conv",
        messageId: data.messageId,
        appliedMemories: [],
        isLastChunk: true,
        toolUIData: data.toolUIData,
      },
    ];

    chatContentJS.conversationState = Cu.cloneInto(messages, content);
    await chatContent.updateComplete;
  });
}

/**
 * Test that ai-website-confirmation component renders correctly
 */
add_task(async function test_website_confirmation_renders() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with website confirmation UI
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Close some tabs",
      assistantMessage: "Please confirm which tabs to close.",
      messageId: "msg-123",
      toolUIData: {
        toolCallId: "tool-123",
        uiType: "website-confirmation",
        properties: {
          tabs: [
            {
              linkedPanel: "panel-1",
              url: "https://example.com",
              title: "Example Site",
              iconSrc: "chrome://branding/content/icon16.png",
              checked: true,
            },
            {
              linkedPanel: "panel-2",
              url: "https://mozilla.org",
              title: "Mozilla",
              iconSrc: "chrome://branding/content/icon16.png",
              checked: false,
            },
          ],
        },
      },
    });

    // Verify the confirmation UI renders
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-website-confirmation"),
        "Wait for ai-website-confirmation to render"
      );

      const confirmation = chatContent.shadowRoot.querySelector(
        "ai-website-confirmation"
      );
      Assert.ok(
        confirmation,
        "ai-website-confirmation component should be rendered"
      );

      // Verify it's in the correct location
      const parent = confirmation.parentElement.parentElement;
      Assert.ok(
        parent?.classList.contains("chat-bubble-assistant"),
        "ai-website-confirmation should be inside assistant message bubble"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that ai-action-confirmation renders for closed tabs
 */
add_task(async function test_action_result_renders_for_closed_tabs() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with action result for closed tabs
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Close the example and mozilla tabs",
      assistantMessage: "I've closed those tabs for you.",
      messageId: "msg-456",
      toolUIData: {
        toolCallId: "tool-456",
        uiType: "ai-action-result",
        properties: {
          confirmedData: {
            actionType: "close_tabs",
            selectedTabs: [
              {
                linkedPanel: "panel-1",
                url: "https://example.com",
                title: "Example Site",
              },
              {
                linkedPanel: "panel-2",
                url: "https://mozilla.org",
                title: "Mozilla",
              },
            ],
            operationId: "op-123",
            requestedCount: 2,
            failedTabs: [],
          },
        },
      },
    });

    // Verify the action result renders
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-action-confirmation"),
        "Wait for ai-action-confirmation to render"
      );

      const actionResult = chatContent.shadowRoot.querySelector(
        "ai-action-confirmation"
      );
      Assert.ok(
        actionResult,
        "ai-action-confirmation component should be rendered"
      );

      // Verify location
      const parent = actionResult.parentElement.parentElement;
      Assert.ok(
        parent?.classList.contains("chat-bubble-assistant"),
        "ai-action-confirmation should be inside assistant message bubble"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that ai-action-confirmation renders for restored tabs
 */
add_task(async function test_action_result_renders_for_restored_tabs() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with restored tabs result
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Undo close tabs",
      assistantMessage: "I've restored the tabs.",
      messageId: "msg-789",
      toolUIData: {
        toolCallId: "tool-789",
        uiType: "ai-action-result",
        properties: {
          confirmedData: {
            actionType: "close_tabs",
            wasRestored: true,
            restoredCount: 2,
            originalClosedTabs: [
              {
                linkedPanel: "panel-1",
                url: "https://example.com",
                title: "Example Site",
              },
              {
                linkedPanel: "panel-2",
                url: "https://mozilla.org",
                title: "Mozilla",
              },
            ],
          },
        },
      },
    });

    // Verify the restored action result renders
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-action-confirmation"),
        "Wait for ai-action-confirmation for restored tabs"
      );

      const actionResult = chatContent.shadowRoot.querySelector(
        "ai-action-confirmation"
      );
      Assert.ok(
        actionResult,
        "ai-action-confirmation should be rendered for restored tabs"
      );

      // Verify it's in the assistant bubble
      const parent = actionResult.parentElement.parentElement;
      Assert.ok(
        parent?.classList.contains("chat-bubble-assistant"),
        "ai-action-confirmation should be inside assistant message bubble"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that tab group confirmation renders correctly
 */
add_task(async function test_tab_group_confirmation_renders() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with tab group confirmation UI
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Group these tabs",
      assistantMessage: "Please confirm which tabs to group.",
      messageId: "msg-group",
      toolUIData: {
        toolCallId: "tool-group",
        uiType: "tab-group-confirmation",
        properties: {
          tabs: [
            {
              linkedPanel: "panel-1",
              url: "https://example.com",
              title: "Example Site",
              iconSrc: "chrome://branding/content/icon16.png",
              checked: true,
            },
            {
              linkedPanel: "panel-2",
              url: "https://mozilla.org",
              title: "Mozilla",
              iconSrc: "chrome://branding/content/icon16.png",
              checked: true,
            },
          ],
          tabGroupLabel: "Research",
        },
      },
    });

    // Verify the confirmation UI renders
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-website-confirmation"),
        "Wait for ai-website-confirmation to render for tab group"
      );

      const confirmation = chatContent.shadowRoot.querySelector(
        "ai-website-confirmation"
      );
      Assert.ok(
        confirmation,
        "ai-website-confirmation should render for tab groups"
      );

      // The component is rendered by ai-chat-content based on the toolUIData
      // We verify it exists and is in the right place
      const parent = confirmation.parentElement.parentElement;
      Assert.ok(
        parent?.classList.contains("chat-bubble-assistant"),
        "Confirmation should be inside assistant message bubble"
      );

      const footer = chatContent.shadowRoot.querySelector(
        "assistant-message-footer"
      );
      await footer.updateComplete;
      Assert.ok(
        footer.shadowRoot.querySelector("moz-button.retry-button"),
        "Retry button should still show for a non-resume-activity tab group confirmation"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that the footer retry button is hidden for a resume-activity
 * conversation's generated response, since retrying it would lose its
 * bespoke system prompt/context.
 */
add_task(async function test_retry_hidden_for_resume_activity_response() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Pick up your research",
      assistantMessage: "Here's where you left off.",
      messageId: "msg-resume",
      toolUIData: {
        toolCallId: "resume-activity-memory-1",
        uiType: "tab-group-confirmation",
        isResumeActivity: true,
        properties: {
          actionType: "open_tabs",
          tabGroupLabel: "Your research",
          tabs: [
            {
              url: "https://example.com",
              title: "Example Site",
              iconSrc: "page-icon:https://example.com",
              checked: false,
            },
          ],
        },
      },
    });

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      const footer = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("assistant-message-footer"),
        "Wait for assistant-message-footer to render"
      );
      await footer.updateComplete;

      Assert.ok(
        !footer.shadowRoot.querySelector("moz-button.retry-button"),
        "Retry button should be hidden for a resume-activity response"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that ai-action-confirmation renders for grouped tabs
 */
add_task(async function test_action_result_renders_for_grouped_tabs() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with action result for grouped tabs
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Group the research tabs",
      assistantMessage: "I've grouped those tabs for you.",
      messageId: "msg-grouped",
      toolUIData: {
        toolCallId: "tool-grouped",
        uiType: "ai-action-result",
        properties: {
          confirmedData: {
            actionType: "group_tabs",
            selectedTabs: [
              {
                url: "https://example.com",
                title: "Example Site",
              },
              {
                url: "https://mozilla.org",
                title: "Mozilla",
              },
            ],
            group: {
              id: "group-123",
              label: "Research",
              color: "blue",
              tabCount: 2,
            },
            operationId: "group-123",
          },
        },
      },
    });

    // Verify the action result renders for grouped tabs
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-action-confirmation"),
        "Wait for ai-action-confirmation to render for grouped tabs"
      );

      const actionResult = chatContent.shadowRoot.querySelector(
        "ai-action-confirmation"
      );
      Assert.ok(
        actionResult,
        "ai-action-confirmation should render for grouped tabs"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that ai-action-confirmation renders for ungrouped tabs
 */
add_task(async function test_action_result_renders_for_ungrouped_tabs() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with ungrouped tabs result
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Undo tab group",
      assistantMessage: "I've ungrouped those tabs.",
      messageId: "msg-ungrouped",
      toolUIData: {
        toolCallId: "tool-ungrouped",
        uiType: "ai-action-result",
        properties: {
          confirmedData: {
            actionType: "group_tabs",
            wasRestored: true,
            originalGroupedTabs: [
              {
                url: "https://example.com",
                title: "Example Site",
              },
              {
                url: "https://mozilla.org",
                title: "Mozilla",
              },
            ],
          },
        },
      },
    });

    // Verify the ungrouped action result renders
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-action-confirmation"),
        "Wait for ai-action-confirmation for ungrouped tabs"
      );

      const actionResult = chatContent.shadowRoot.querySelector(
        "ai-action-confirmation"
      );
      Assert.ok(
        actionResult,
        "ai-action-confirmation should render for ungrouped tabs"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that confirmation UI can dispatch submit events that bubble correctly
 */
add_task(async function test_confirmation_events_bubble_correctly() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with confirmation UI
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Close tabs",
      assistantMessage: "Please confirm the tabs to close.",
      messageId: "msg-confirm",
      toolUIData: {
        toolCallId: "tool-confirm",
        uiType: "website-confirmation",
        properties: {
          tabs: [
            {
              linkedPanel: "panel-1",
              url: "https://example.com",
              title: "Example",
              checked: true,
            },
          ],
        },
      },
    });

    // Test event dispatch and bubbling
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-website-confirmation"),
        "Wait for confirmation component"
      );

      const confirmation = chatContent.shadowRoot.querySelector(
        "ai-website-confirmation"
      );
      Assert.ok(confirmation, "Confirmation component should exist");

      // Set up event listeners to verify bubbling
      let eventBubbledToChatContent = false;
      let eventBubbledToDocument = false;
      let receivedDetail = null;

      // Add listener on chat-content element
      const chatContentHandler = event => {
        eventBubbledToChatContent = true;
        receivedDetail = event.detail;
      };
      chatContent.addEventListener(
        "ai-website-confirmation:submit",
        chatContentHandler,
        { once: true }
      );

      // Add listener on document
      const documentHandler = () => {
        eventBubbledToDocument = true;
      };
      content.document.addEventListener(
        "ai-website-confirmation:submit",
        documentHandler,
        { once: true }
      );

      // Create and dispatch the submit event
      const expectedTabs = [
        {
          linkedPanel: "panel-1",
          url: "https://example.com",
          title: "Example",
        },
      ];

      const submitEvent = new content.CustomEvent(
        "ai-website-confirmation:submit",
        {
          bubbles: true,
          composed: true,
          detail: {
            selectedTabs: expectedTabs,
          },
        }
      );

      // Dispatch the event
      confirmation.dispatchEvent(submitEvent);

      // Give event time to bubble (synchronous but just to be safe)
      await new Promise(resolve => content.setTimeout(resolve, 0));

      // Verify event bubbled correctly
      Assert.ok(
        eventBubbledToChatContent,
        "Submit event should bubble to ai-chat-content"
      );

      Assert.ok(
        eventBubbledToDocument,
        "Submit event should bubble to document"
      );

      Assert.ok(
        receivedDetail && receivedDetail.selectedTabs,
        "Event detail should contain selectedTabs"
      );

      Assert.equal(
        receivedDetail.selectedTabs.length,
        1,
        "Should have one selected tab"
      );

      Assert.equal(
        receivedDetail.selectedTabs[0].url,
        "https://example.com",
        "Selected tab should have correct URL"
      );

      // Clean up listeners (in case they didn't fire)
      chatContent.removeEventListener(
        "ai-website-confirmation:submit",
        chatContentHandler
      );
      content.document.removeEventListener(
        "ai-website-confirmation:submit",
        documentHandler
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that tab group confirmation events include tabGroupLabel
 */
add_task(async function test_tab_group_confirmation_event_detail() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    // Set up conversation with tab group confirmation
    await setupConversationWithToolUI(aichatBrowser, {
      userMessage: "Group tabs",
      assistantMessage: "Please confirm tabs to group.",
      messageId: "msg-group-event",
      toolUIData: {
        toolCallId: "tool-group-event",
        uiType: "tab-group-confirmation",
        properties: {
          tabs: [
            {
              linkedPanel: "panel-1",
              url: "https://example.com",
              title: "Example",
              checked: true,
            },
          ],
          tabGroupLabel: "Work",
        },
      },
    });

    // Test that submit event includes tabGroupLabel
    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("ai-website-confirmation"),
        "Wait for confirmation component"
      );

      const confirmation = chatContent.shadowRoot.querySelector(
        "ai-website-confirmation"
      );

      let receivedDetail = null;

      // Listen for the submit event
      const handler = event => {
        receivedDetail = event.detail;
      };
      chatContent.addEventListener("ai-website-confirmation:submit", handler, {
        once: true,
      });

      // Create and dispatch submit event with tabGroupLabel
      const submitEvent = new content.CustomEvent(
        "ai-website-confirmation:submit",
        {
          bubbles: true,
          composed: true,
          detail: {
            selectedTabs: [
              {
                linkedPanel: "panel-1",
                url: "https://example.com",
                title: "Example",
              },
            ],
            tabGroupLabel: "Work",
          },
        }
      );

      confirmation.dispatchEvent(submitEvent);

      // Wait for event to process
      await new Promise(resolve => content.setTimeout(resolve, 0));

      // Verify event detail includes tabGroupLabel
      Assert.ok(receivedDetail, "Should receive event detail");

      Assert.equal(
        receivedDetail.tabGroupLabel,
        "Work",
        "Event detail should include tabGroupLabel"
      );

      Assert.equal(
        receivedDetail.selectedTabs.length,
        1,
        "Should have selected tabs"
      );

      // Cleanup
      chatContent.removeEventListener(
        "ai-website-confirmation:submit",
        handler
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

/**
 * Test that "Select all" / "Deselect all" on either confirmation card is
 * forwarded as a tool UI update, so the parent can record it
 */
add_task(async function test_confirmation_toggle_all_dispatches_update() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    for (const [uiType, properties] of [
      ["website-confirmation", {}],
      ["tab-group-confirmation", { actionType: "open_tabs" }],
    ]) {
      const toolCallId = `tool-toggle-${uiType}`;
      await setupConversationWithToolUI(aichatBrowser, {
        userMessage: "Act on tabs",
        assistantMessage: "Please confirm the tabs.",
        messageId: `msg-toggle-${uiType}`,
        toolUIData: {
          toolCallId,
          uiType,
          properties: {
            ...properties,
            tabs: [
              {
                linkedPanel: "panel-1",
                url: "https://example.com",
                title: "Example",
                checked: false,
              },
            ],
          },
        },
      });

      await SpecialPowers.spawn(
        aichatBrowser,
        [uiType, toolCallId],
        async (type, expectedToolCallId) => {
          const chatContent = content.document.querySelector("ai-chat-content");
          const confirmation = await ContentTaskUtils.waitForCondition(
            () =>
              chatContent.shadowRoot.querySelector("ai-website-confirmation"),
            "Wait for confirmation component"
          );
          await confirmation.updateComplete;

          const updates = [];
          const listener = event => updates.push(event.detail);
          chatContent.addEventListener("AIChatContent:ToolUIUpdate", listener);

          const toggleButton =
            confirmation.shadowRoot.querySelectorAll("moz-button")[1];
          toggleButton.click();
          await confirmation.updateComplete;
          toggleButton.click();

          chatContent.removeEventListener(
            "AIChatContent:ToolUIUpdate",
            listener
          );

          Assert.deepEqual(
            updates.map(update => ({
              toolCallId: update.toolCallId,
              updateType: update.updateType,
              allSelected: update.updateData.allSelected,
            })),
            [
              {
                toolCallId: expectedToolCallId,
                updateType: "toggle-all-tab-selection",
                allSelected: true,
              },
              {
                toolCallId: expectedToolCallId,
                updateType: "toggle-all-tab-selection",
                allSelected: false,
              },
            ],
            `${type}: select all then deselect all are forwarded`
          );
        }
      );
    }
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});
