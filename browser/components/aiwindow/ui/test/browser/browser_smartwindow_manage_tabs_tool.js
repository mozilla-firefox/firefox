/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { UI_TYPES, ToolUI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolUI.sys.mjs"
);

const { tabManagementService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/TabManagementService.sys.mjs"
);

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// Import toolFns to stub the manageTabs function
const { toolFns } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

/**
 * Test suite for manage_tabs tool functionality in AI Window
 */
add_setup(async function () {
  await Services.fog.testFlushAllChildren();

  // Set up test preferences to avoid network issues
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.endpoint", "http://localhost:0/v1"],
      ["dom.security.https_first", false],
    ],
  });
});

/**
 * Test that confirmation UI appears with correct content when manage_tabs tool is called
 */
add_task(async function test_manage_tabs_confirmation_ui() {
  const sandbox = sinon.createSandbox();
  const mockEngineMan = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;

    // Mock the manageTabs tool to return confirmation UI
    sandbox.stub(toolFns, "manageTabs").resolves({
      toolResult: {
        description:
          "The following tabs were found. User confirmation is required to close them.",
        pending: true,
        action: "close_tabs",
        selectedTabs: [
          { url: "https://amazon.com", title: "Amazon", checked: true },
          { url: "https://ebay.com", title: "eBay", checked: true },
        ],
      },
      uiData: {
        uiType: UI_TYPES.WEBSITE_CONFIRMATION,
        toolCallId: "test-tool-call-1",
        properties: {
          tabs: [
            {
              url: "https://amazon.com",
              title: "Amazon",
              linkedPanel: "panel-1",
              checked: true,
            },
            {
              url: "https://ebay.com",
              title: "eBay",
              linkedPanel: "panel-2",
              checked: true,
            },
          ],
          originalUserPrompt: "close my shopping tabs",
        },
      },
    });

    // Step 1: User asks to close tabs
    await typeInSmartbar(browser, "close my shopping tabs");
    await submitSmartbar(browser);

    // Step 2: Mock LLM response with tool call
    await mockEngineMan.respondTo({
      purpose: "chat",
      response: [
        {
          text: "",
          tokens: null,
          isPrompt: false,
          toolCalls: [
            {
              id: "call_manage_1",
              function: {
                name: "manage_tabs",
                arguments: JSON.stringify({
                  action: "close_tabs",
                  ask_confirmation: true,
                }),
              },
            },
          ],
        },
        {
          text: "I found 2 shopping tabs that match your request. Please confirm which ones you'd like to close.",
          tokens: null,
          isPrompt: false,
          toolCalls: null,
        },
      ],
    });

    // Wait for confirmation UI to appear and get its data
    const confirmationData = await TestUtils.waitForCondition(async () => {
      return SpecialPowers.spawn(browser, [], async () => {
        const aiWindowEl = content.document.querySelector("ai-window");
        if (!aiWindowEl?.shadowRoot) {
          return null;
        }

        const aichatBrowser =
          aiWindowEl.shadowRoot.querySelector("#aichat-browser");
        if (!aichatBrowser) {
          return null;
        }

        return SpecialPowers.spawn(aichatBrowser, [], async () => {
          const chatContent = content.document.querySelector("ai-chat-content");
          if (!chatContent) {
            return null;
          }

          await chatContent.updateComplete;
          const confirmation = chatContent.shadowRoot.querySelector(
            "ai-website-confirmation"
          );

          if (!confirmation) {
            return null;
          }

          // Confirmation exists, now get the data
          const tabItems =
            confirmation.shadowRoot.querySelectorAll("ai-website-select");

          const confirmButton = confirmation.shadowRoot.querySelector(
            "moz-button[type='primary']"
          );
          const closeButton =
            confirmation.shadowRoot.querySelector(".close-button");

          return {
            tabCount: tabItems.length,
            hasConfirmButton: !!confirmButton,
            hasCancelButton: !!closeButton,
          };
        });
      });
    }, "Waiting for confirmation UI to appear and retrieving its data");

    Assert.ok(confirmationData, "Confirmation UI data should be available");
    Assert.equal(confirmationData.tabCount, 2, "Should show 2 tabs");
    Assert.ok(confirmationData.hasConfirmButton, "Should have confirm button");
    Assert.ok(confirmationData.hasCancelButton, "Should have cancel button");
  } finally {
    sandbox.restore();
    mockEngineMan.cleanupMocks();
    await BrowserTestUtils.closeWindow(win);
    Services.fog.testResetFOG();
  }
});

/**
 * Test retry flow after cancellation
 */
add_task(async function test_retry_after_cancellation() {
  const sandbox = sinon.createSandbox();
  const mockEngineMan = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;

    let manageTabsCallCount = 0;
    sandbox.stub(toolFns, "manageTabs").callsFake(async () => {
      manageTabsCallCount++;
      return {
        toolResult: {
          description: "Found tab to close",
          pending: true,
          action: "close_tabs",
          selectedTabs: [
            { url: "https://amazon.com", title: "Amazon", checked: true },
          ],
        },
        uiData: {
          uiType: UI_TYPES.WEBSITE_CONFIRMATION,
          toolCallId: `test-tool-call-${manageTabsCallCount}`,
          properties: {
            tabs: [
              {
                url: "https://amazon.com",
                title: "Amazon",
                linkedPanel: "panel-1",
                checked: true,
              },
            ],
            originalUserPrompt: "close amazon tab",
          },
        },
      };
    });

    // Step 1: Initial request
    await typeInSmartbar(browser, "close amazon tab");
    await submitSmartbar(browser);

    // Step 2: LLM response with tool call
    await mockEngineMan.respondTo({
      purpose: "chat",
      response: [
        {
          text: "",
          tokens: null,
          isPrompt: false,
          toolCalls: [
            {
              id: "call_manage_3",
              function: {
                name: "manage_tabs",
                arguments: JSON.stringify({
                  action: "close_tabs",
                  ask_confirmation: true,
                }),
              },
            },
          ],
        },
        {
          text: "I found the Amazon tab. Please confirm if you'd like to close it.",
          tokens: null,
          isPrompt: false,
          toolCalls: null,
        },
      ],
    });

    // Wait for confirmation UI
    await TestUtils.waitForCondition(async () => {
      return SpecialPowers.spawn(browser, [], async () => {
        const aiWindowEl = content.document.querySelector("ai-window");
        const aichatBrowser =
          aiWindowEl?.shadowRoot?.querySelector("#aichat-browser");
        if (!aichatBrowser) {
          return false;
        }

        return SpecialPowers.spawn(aichatBrowser, [], async () => {
          const chatContent = content.document.querySelector("ai-chat-content");
          await chatContent?.updateComplete;
          return !!chatContent?.shadowRoot?.querySelector(
            "ai-website-confirmation"
          );
        });
      });
    }, "Waiting for initial confirmation UI");

    // Step 3: User submits new prompt (auto-cancels confirmation)
    await typeInSmartbar(browser, "what's the weather?");
    await submitSmartbar(browser);

    // Step 4: New response should trigger auto-cancel and show retry UI
    await mockEngineMan.respondTo({
      purpose: "chat",
      response:
        "I'd need access to real-time data to tell you the current weather.",
    });

    // Wait for retry component to appear
    await TestUtils.waitForCondition(async () => {
      return SpecialPowers.spawn(browser, [], async () => {
        const aiWindowEl = content.document.querySelector("ai-window");
        const aichatBrowser =
          aiWindowEl?.shadowRoot?.querySelector("#aichat-browser");
        if (!aichatBrowser) {
          return false;
        }

        return SpecialPowers.spawn(aichatBrowser, [], async () => {
          const chatContent = content.document.querySelector("ai-chat-content");
          await chatContent?.updateComplete;

          // Look for the retry button in the conversation
          // The retry button is rendered inside chat-bubble divs
          const retryButton =
            chatContent.shadowRoot.querySelector(".tool-retry-button");
          return !!retryButton;
        });
      });
    }, "Waiting for retry component");

    // Verify retry UI exists
    const hasRetryUI = await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowEl = content.document.querySelector("ai-window");
      const aichatBrowser =
        aiWindowEl.shadowRoot.querySelector("#aichat-browser");

      return SpecialPowers.spawn(aichatBrowser, [], async () => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const retryButton =
          chatContent.shadowRoot.querySelector(".tool-retry-button");
        return !!retryButton;
      });
    });

    Assert.ok(hasRetryUI, "Retry UI should be displayed after cancellation");
    Assert.equal(
      manageTabsCallCount,
      1,
      "manageTabs should have been called once"
    );

    // Step 5: Click retry button
    await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowEl = content.document.querySelector("ai-window");
      const aichatBrowser =
        aiWindowEl.shadowRoot.querySelector("#aichat-browser");

      return SpecialPowers.spawn(aichatBrowser, [], async () => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const retryButton =
          chatContent.shadowRoot.querySelector(".tool-retry-button");

        if (!retryButton) {
          throw new Error("Retry button not found");
        }

        retryButton.click();
      });
    });

    // Wait a bit for the retry to process
    await TestUtils.waitForTick();

    // Step 6: Verify the retry prompt appears as a new user message in the conversation
    const retryMessageFound = await TestUtils.waitForCondition(async () => {
      return SpecialPowers.spawn(browser, [], async () => {
        const aiWindowEl = content.document.querySelector("ai-window");

        // Get the conversation messages from the conversation object
        const conversation = aiWindowEl.conversation;
        if (!conversation || !conversation.messages) {
          return false;
        }

        const messages = conversation.messages;

        // Look for the pattern: weather response followed by retry of "close amazon tab"
        let foundWeatherResponse = false;

        for (const msg of messages) {
          // Check if this is the weather response (role = 1 for assistant)
          const msgBody =
            typeof msg.content?.body === "string" ? msg.content.body : "";

          if (
            msg.role === 1 && // Assistant role
            msgBody.toLowerCase().includes("weather")
          ) {
            foundWeatherResponse = true;
          }

          // After finding weather response, look for the retry message (role = 0 for user)
          if (
            foundWeatherResponse &&
            msg.role === 0 && // User role
            msgBody.toLowerCase() === "close amazon tab"
          ) {
            return true; // Found retry message after weather response
          }
        }

        return false;
      });
    }, "Waiting for retry message to appear in conversation");

    Assert.ok(
      retryMessageFound,
      "Retry should resubmit 'close amazon tab' as a new message"
    );
  } finally {
    sandbox.restore();
    mockEngineMan.cleanupMocks();
    await BrowserTestUtils.closeWindow(win);
    Services.fog.testResetFOG();
  }
});

/**
 * A tab in a non-interacting window is closed from another window.
 */
add_task(async function test_close_tab_in_non_interacting_window() {
  const interactingWindow = await openAIWindow();
  const otherWindow = await openAIWindow();

  const tab = await BrowserTestUtils.openNewForegroundTab(
    otherWindow.gBrowser,
    "https://example.com/"
  );
  const tokenToKey = new Map([["token-a", tab.permanentKey]]);
  const selectedTabs = [{ token: "token-a", url: "https://example.com/" }];

  try {
    Assert.ok(
      otherWindow.gBrowser.tabs.includes(tab),
      "Target tab starts open in the non-interacting window"
    );

    const result = await ToolUI.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      interactingWindow
    );

    Assert.ok(result, "closeSelectedTabs should report a result");
    Assert.equal(
      result.operationIds.length,
      1,
      "Should surface an undo operationId"
    );
    Assert.equal(
      result.requestedCount,
      1,
      "Should have requested closing one tab"
    );

    await TestUtils.waitForCondition(
      () => !otherWindow.gBrowser.tabs.includes(tab),
      "Tab in the non-interacting window should be closed"
    );
    Assert.ok(
      !otherWindow.gBrowser.tabs.includes(tab),
      "Target tab is closed in its owning window"
    );
  } finally {
    await BrowserTestUtils.closeWindow(otherWindow);
    await BrowserTestUtils.closeWindow(interactingWindow);
  }
});

/**
 * A tab selection spanning multiple windows closes each tab in its own window.
 */
add_task(async function test_close_tabs_spanning_multiple_windows() {
  const windowA = await openAIWindow();
  const windowB = await openAIWindow();

  const tabA = await BrowserTestUtils.openNewForegroundTab(
    windowA.gBrowser,
    "https://example.com/"
  );
  const tabB = await BrowserTestUtils.openNewForegroundTab(
    windowB.gBrowser,
    "https://example.org/"
  );

  const tokenToKey = new Map([
    ["token-a", tabA.permanentKey],
    ["token-b", tabB.permanentKey],
  ]);
  const selectedTabs = [
    { token: "token-a", url: "https://example.com/" },
    { token: "token-b", url: "https://example.org/" },
  ];

  try {
    const result = await ToolUI.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      windowA
    );

    Assert.equal(
      result.requestedCount,
      2,
      "Should have requested closing both tabs"
    );
    Assert.ok(
      !windowA.gBrowser.tabs.includes(tabA),
      "Tab in window A is closed"
    );
    Assert.ok(
      !windowB.gBrowser.tabs.includes(tabB),
      "Tab in window B is closed"
    );
  } finally {
    await BrowserTestUtils.closeWindow(windowA);
    await BrowserTestUtils.closeWindow(windowB);
  }
});

/**
 * Undoing a cross-window close restores the tabs in the owning windows.
 */
add_task(async function test_undo_close_tabs_spanning_multiple_windows() {
  const windowA = await openAIWindow();
  const windowB = await openAIWindow();

  const tabA = await BrowserTestUtils.openNewForegroundTab(
    windowA.gBrowser,
    "https://example.com/"
  );
  const tabB = await BrowserTestUtils.openNewForegroundTab(
    windowB.gBrowser,
    "https://example.org/"
  );

  // Flush tab state so SessionStore records the loaded URL for each tab
  await TabStateFlusher.flush(tabA.linkedBrowser);
  await TabStateFlusher.flush(tabB.linkedBrowser);

  const tokenToKey = new Map([
    ["token-a", tabA.permanentKey],
    ["token-b", tabB.permanentKey],
  ]);
  const selectedTabs = [
    { token: "token-a", url: "https://example.com/" },
    { token: "token-b", url: "https://example.org/" },
  ];

  try {
    const closeResult = await ToolUI.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      windowA
    );

    Assert.equal(
      closeResult.operationIds.length,
      2,
      "Should produce one operationId per owning window"
    );

    const tabCountA = windowA.gBrowser.tabs.length;
    const tabCountB = windowB.gBrowser.tabs.length;

    // Undo restores each operation in its own window
    for (const operationId of closeResult.operationIds) {
      const restoreResult = await tabManagementService.restoreTabs({
        operationId,
      });
      Assert.equal(
        restoreResult.restoredCount,
        1,
        `Operation ${operationId} restores its tab`
      );
      Assert.equal(
        restoreResult.failedTabs.length,
        0,
        `Operation ${operationId} has no restore failures`
      );
    }

    await TestUtils.waitForCondition(
      () =>
        windowA.gBrowser.tabs.length === tabCountA + 1 &&
        windowB.gBrowser.tabs.length === tabCountB + 1,
      "Both windows should have their restored tab"
    );
  } finally {
    await BrowserTestUtils.closeWindow(windowA);
    await BrowserTestUtils.closeWindow(windowB);
  }
});

/**
 * Closing the last tab of a non-interacting window keeps that window open
 * instead of closing the window, so the tab stays restorable.
 */
add_task(async function test_close_last_tab_keeps_window_open() {
  const interactingWindow = await openAIWindow();
  const otherWindow = await openAIWindow();

  const tab = otherWindow.gBrowser.selectedTab;
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.com/"
  );
  await BrowserTestUtils.browserLoaded(
    tab.linkedBrowser,
    false,
    "https://example.com/"
  );
  // Flush tab state so SessionStore records the loaded URL
  await TabStateFlusher.flush(tab.linkedBrowser);

  Assert.equal(
    otherWindow.gBrowser.tabs.length,
    1,
    "Non-interacting window starts with a single tab"
  );

  const tokenToKey = new Map([["token-a", tab.permanentKey]]);
  const selectedTabs = [{ token: "token-a", url: "https://example.com/" }];

  try {
    const result = await ToolUI.closeSelectedTabs(
      selectedTabs,
      tokenToKey,
      interactingWindow
    );

    Assert.equal(
      result.operationIds.length,
      1,
      "Should close the last tab and surface an undo operationId"
    );

    Assert.ok(!otherWindow.closed, "Non-interacting window stays open");
    Assert.equal(
      otherWindow.gBrowser.tabs.length,
      1,
      "The closed tab was replaced"
    );
    Assert.ok(
      !otherWindow.gBrowser.tabs.includes(tab),
      "The original tab is closed"
    );

    const restoreResult = await tabManagementService.restoreTabs({
      operationId: result.operationIds[0],
    });
    Assert.equal(
      restoreResult.restoredCount,
      1,
      "The last tab can still be undone in its owning window"
    );
  } finally {
    await BrowserTestUtils.closeWindow(otherWindow);
    await BrowserTestUtils.closeWindow(interactingWindow);
  }
});
