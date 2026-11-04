/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * manage_tabs requests made from a fullpage chat, with only the model mocked.
 */

/**
 * @type {import("../AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * @type {import("../../../../../../toolkit/components/ml/tests/MLTestUtils.sys.mjs")}
 */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { GROUP_TABS, manageTabsAction } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ManageTabs.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["places.semanticHistory.smartwindow.featureGate", false],
    ],
  });
});

/**
 * Resolves with the next chat request or rejects if generation ends first.
 *
 * @param {MockEngineManager} mockEngineManager
 * @param {MozBrowser} browser
 */
async function captureFollowUpRequest(mockEngineManager, browser) {
  const inputCta = browser.contentDocument
    .querySelector("ai-window")
    .shadowRoot.querySelector("#ai-window-smartbar input-cta");
  return Promise.race([
    mockEngineManager.captureRequest({ purpose: "chat" }),
    BrowserTestUtils.waitForMutationCondition(
      inputCta,
      { attributeFilter: ["action"] },
      () => inputCta.getAttribute("action") !== "stop"
    ).then(() => {
      throw new Error("Generation ended without a follow-up request");
    }),
  ]);
}

/**
 * Without a card nobody can choose the chat tab, so grouping one tab must leave
 * it out.
 */
add_task(async function test_group_tabs_without_card_skips_chat_tab() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    await server.openTab({ title: "Other tab" });
    win.gBrowser.selectedTab = chatTab;

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });

    // A real model can only use tokens from get_open_tabs.
    const { request, respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    const requestedToken = conversation.urlToToken.get(requested.url);
    Assert.ok(
      requestedToken && JSON.stringify(request.args).includes(requestedToken),
      "The requested tab reaches the model"
    );
    Assert.ok(
      !request.tools.find(tool => tool.function.name === "manage_tabs").function
        .parameters.properties.include_chat_tab,
      "include_chat_tab is not offered while its pref is off"
    );
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_group_tabs",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "group_tabs",
              ask_confirmation: false,
              include_chat_tab: true,
              url_tokens: [`§url_token: ${requestedToken}§`],
              label: "Tabs",
            }),
          },
        },
      ],
    });

    await TestUtils.waitForCondition(
      () => requested.tab.group,
      "The requested tab is grouped"
    );
    Assert.deepEqual(
      requested.tab.group.tabs,
      [requested.tab],
      "The group holds only the requested tab"
    );
    Assert.ok(!chatTab.group, "The chat tab is not grouped");
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Only the user knows whether the chat belongs in the group, so the card offers
 * it unticked. The model is never told about it.
 */
add_task(async function test_group_tabs_card_offers_the_chat_tab_unticked() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    await server.openTab({ title: "Other tab" });
    win.gBrowser.selectedTab = chatTab;

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });

    const { respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    const requestedToken = conversation.urlToToken.get(requested.url);
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_group_tabs",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "group_tabs",
              ask_confirmation: true,
              url_tokens: [`§url_token: ${requestedToken}§`],
              label: "Tabs",
            }),
          },
        },
      ],
    });

    const aichatBrowser = await getAIChatBrowser(browser);
    const rows = await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const { shadowRoot } = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        shadowRoot,
        { childList: true, subtree: true },
        () => shadowRoot.querySelector("ai-website-confirmation")
      );
      const card = shadowRoot.querySelector("ai-website-confirmation");
      await card.updateComplete;
      return Array.from(
        card.shadowRoot.querySelectorAll("ai-website-select"),
        ({ wrappedJSObject: row }) => ({ url: row.url, checked: row.checked })
      );
    });
    Assert.deepEqual(
      rows,
      [
        { url: requested.url, checked: true },
        { url: AIWINDOW_URL, checked: false },
      ],
      "Only the requested tab is ticked"
    );

    await SpecialPowers.spawn(aichatBrowser, [AIWINDOW_URL], async chatUrl => {
      const card = content.document
        .querySelector("ai-chat-content")
        .shadowRoot.querySelector("ai-website-confirmation");
      const chatRow = Array.from(
        card.shadowRoot.querySelectorAll("ai-website-select")
      ).find(row => row.wrappedJSObject.url === chatUrl);
      chatRow.shadowRoot
        .querySelector("moz-checkbox")
        .shadowRoot.querySelector("input")
        .click();
      await ContentTaskUtils.waitForCondition(
        () => chatRow.wrappedJSObject.checked,
        "The chat tab is ticked"
      );
      await card.updateComplete;
      card.shadowRoot.querySelector("moz-button[type='primary']").click();
    });

    await TestUtils.waitForCondition(
      () => requested.tab.group,
      "The requested tab is grouped"
    );
    const groupTabs = requested.tab.group.tabs;
    Assert.ok(
      groupTabs.length == 2 && groupTabs.includes(chatTab),
      "The group holds the requested and the chat tab"
    );

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const { request } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    Assert.ok(
      JSON.stringify(request.args).includes(requestedToken),
      "The model is told about the requested tab"
    );
    Assert.ok(
      !JSON.stringify(request.args).includes(AIWINDOW_URL),
      "The model is not told about the chat tab"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_group_tabs_card_offers_included_chat_tab_ticked() {
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    win.gBrowser.selectedTab = chatTab;
    const { conversation } =
      chatTab.linkedBrowser.contentDocument.querySelector("ai-window");
    conversation.addUserMessage("Group these tabs");
    const { toolResult, uiData } = await manageTabsAction(
      {
        action: GROUP_TABS,
        validUrls: new Set([requested.url]),
        ask_confirmation: true,
        include_chat_tab: true,
        label: "Tabs",
        toolCallId: "call_group_tabs",
      },
      conversation
    );

    const chatRow = uiData.properties.tabs.find(tab => tab.isChatTab);
    Assert.ok(chatRow.checked, "The chat tab is offered ticked");
    Assert.equal(
      toolResult.selectedTabs.length,
      1,
      "The model is not told about the chat tab"
    );
  } finally {
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * A chat about the same topic belongs in the group, so the model can include it
 * even without a card.
 */
add_task(async function test_group_tabs_with_include_chat_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.manageTabs.includeChatTab", true]],
  });
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    const other = await server.openTab({ title: "Other tab" });
    win.gBrowser.selectedTab = chatTab;

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });

    const { request, respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    Assert.ok(
      request.tools.find(tool => tool.function.name === "manage_tabs").function
        .parameters.properties.include_chat_tab,
      "include_chat_tab is offered while its pref is on"
    );
    const requestedToken = conversation.urlToToken.get(requested.url);
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_group_with_chat",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "group_tabs",
              ask_confirmation: false,
              include_chat_tab: true,
              url_tokens: [`§url_token: ${requestedToken}§`],
              label: "Tabs",
            }),
          },
        },
      ],
    });

    await TestUtils.waitForCondition(
      () => requested.tab.group,
      "The requested tab is grouped"
    );
    const groupTabs = requested.tab.group.tabs;
    Assert.ok(
      groupTabs.length == 2 && groupTabs.includes(chatTab),
      "The group holds the requested and the chat tab"
    );
    Assert.ok(!other.tab.group, "The other tab is not grouped");

    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const followUp = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    Assert.ok(
      JSON.stringify(followUp.request.args).includes(requestedToken),
      "The model is told about the requested tab"
    );
    Assert.ok(
      !JSON.stringify(followUp.request.args).includes(AIWINDOW_URL),
      "The model is not told about the chat tab"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

/**
 * If a manage_tabs call fails we show no card. Let the model reply with what
 * went wrong.
 */
add_task(async function test_failed_manage_tabs_lets_the_model_reply() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    win.gBrowser.selectedTab = chatTab;

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });

    const { respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    const requestedToken = conversation.urlToToken.get(requested.url);
    // Tab was closed after submission, but before a reply.
    BrowserTestUtils.removeTab(requested.tab);
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_close_tabs",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "close_tabs",
              ask_confirmation: true,
              url_tokens: [`§url_token: ${requestedToken}§`],
            }),
          },
        },
      ],
    });

    const followUp = await captureFollowUpRequest(mockEngineManager, browser);
    Assert.ok(
      JSON.stringify(followUp.request.args).includes(
        "None of the provided URL tokens match an open tab"
      ),
      "The model is told no tab matched"
    );
    const reply = "Reply from mock.";
    followUp.respond(reply);

    await TestUtils.waitForCondition(
      async () =>
        (await getSidebarChatMessages(browser)).some(
          ({ role, message, hasRendered }) =>
            role === "assistant" && message === reply && hasRendered
        ),
      "The reply is shown in the chat"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_card_ends_the_turn_without_a_follow_up() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const requested = await server.openTab({ title: "Requested tab" });
    win.gBrowser.selectedTab = chatTab;

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });

    const { respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    const requestedToken = conversation.urlToToken.get(requested.url);
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_close_tabs",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "close_tabs",
              ask_confirmation: true,
              url_tokens: [`§url_token: ${requestedToken}§`],
            }),
          },
        },
      ],
    });

    await Assert.rejects(
      captureFollowUpRequest(mockEngineManager, browser),
      /Generation ended without a follow-up request/,
      "The model gets no further turn while the card is shown"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * The model did not receive any context for the current tab and asking to close
 * it fails: The user should still receive a reply.
 */
add_task(async function test_closing_the_fullpage_chat_tab_gets_a_reply() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const chatTab = win.gBrowser.selectedTab;

  try {
    Assert.deepEqual(win.gBrowser.tabs, [chatTab], "Only the chat tab is open");

    const browser = chatTab.linkedBrowser;
    await typeInSmartbar(browser, "hello");
    await submitSmartbar(browser);
    const conversation =
      browser.contentDocument.querySelector("ai-window").conversation;
    const openTabsResult = await triggerGetOpenTabsToolCall({
      mockEngineManager,
      conversation,
      callId: "call_get_open_tabs",
    });
    Assert.deepEqual(
      openTabsResult.content.body,
      [],
      "get_open_tabs leaves the chat tab out"
    );

    const { request, respond } = await mockEngineManager.captureRequest({
      purpose: "chat",
    });
    Assert.ok(
      !JSON.stringify(request.args).includes(AIWINDOW_URL) &&
        !conversation.urlToToken.size,
      "The model receives no URL or token for the chat tab"
    );
    respond({
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: "call_close_active_tab",
          function: {
            name: "manage_tabs",
            arguments: JSON.stringify({
              action: "close_tabs",
              ask_confirmation: true,
              url_tokens: ["§url_token: ACTIVE_TAB§"],
            }),
          },
        },
      ],
    });

    const followUp = await captureFollowUpRequest(mockEngineManager, browser);
    Assert.ok(
      JSON.stringify(followUp.request.args).includes(
        "No valid URLs were provided to manage_tabs"
      ),
      "The model is told why nothing was closed"
    );
    const reply = "Reply from mock.";
    followUp.respond(reply);

    await TestUtils.waitForCondition(
      async () =>
        (await getSidebarChatMessages(browser)).some(
          ({ role, message, hasRendered }) =>
            role === "assistant" && message === reply && hasRendered
        ),
      "The reply is shown in the chat"
    );
    Assert.ok(
      win.gBrowser.tabs.includes(chatTab) && !chatTab.closing,
      "The chat tab stays open"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await BrowserTestUtils.closeWindow(win);
  }
});

/**
 * Pins a tab through the context menu.
 *
 * @param {ChromeWindow} win
 * @param {MozTabbrowserTab} tab
 */
async function pinTabFromContextMenu(win, tab) {
  const menu = win.document.getElementById("tabContextMenu");
  const shown = BrowserTestUtils.waitForPopupEvent(menu, "shown");
  EventUtils.synthesizeMouseAtCenter(
    tab,
    { type: "contextmenu", button: 2 },
    win
  );
  await shown;
  const hidden = BrowserTestUtils.waitForPopupEvent(menu, "hidden");
  menu.activateItem(win.document.getElementById("context_pinTab"));
  await hidden;
  await BrowserTestUtils.waitForMutationCondition(
    tab,
    { attributeFilter: ["pinned"] },
    () => tab.pinned
  );
}

/**
 * Send a tab group request from the chat tab.
 *
 * @param {object} options
 * @param {MockEngineManager} options.mockEngineManager
 * @param {MozTabbrowserTab} options.chatTab
 * @param {Array<string>} options.urls
 * @returns {Promise<MozBrowser>} The browser containing the chat
 */
async function requestGroupTabs({ mockEngineManager, chatTab, urls }) {
  const browser = chatTab.linkedBrowser;
  await typeInSmartbar(browser, "hello");
  await submitSmartbar(browser);
  const conversation =
    browser.contentDocument.querySelector("ai-window").conversation;
  await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs",
  });

  const { respond } = await mockEngineManager.captureRequest({
    purpose: "chat",
  });
  respond({
    text: "",
    tokens: null,
    isPrompt: false,
    toolCalls: [
      {
        id: "call_group_tabs",
        function: {
          name: "manage_tabs",
          arguments: JSON.stringify({
            action: "group_tabs",
            ask_confirmation: false,
            url_tokens: urls.map(
              url => `§url_token: ${conversation.urlToToken.get(url)}§`
            ),
            label: "Tabs",
          }),
        },
      },
    ],
  });

  return getAIChatBrowser(browser);
}

/**
 * Wait for the tab group card and confirm it.
 *
 * @param {MozBrowser} aichatBrowser
 * @param {object} [options]
 * @param {MozTabbrowserTab} [options.chatTab] - Chat tab to tick
 */
async function confirmTabGroup(aichatBrowser, { chatTab = null } = {}) {
  if (chatTab) {
    const inputCta = chatTab.linkedBrowser.contentDocument
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar input-cta");
    await BrowserTestUtils.waitForMutationCondition(
      inputCta,
      { attributeFilter: ["action"] },
      () => inputCta.getAttribute("action") !== "stop"
    );
  }
  await SpecialPowers.spawn(
    aichatBrowser,
    [chatTab && AIWINDOW_URL],
    async chatUrl => {
      const { shadowRoot } = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForMutationCondition(
        shadowRoot,
        { childList: true, subtree: true },
        () => shadowRoot.querySelector("ai-website-confirmation")
      );
      const card = shadowRoot.querySelector("ai-website-confirmation");
      await card.updateComplete;
      if (chatUrl) {
        const chatRow = Array.from(
          card.shadowRoot.querySelectorAll("ai-website-select")
        ).find(row => row.wrappedJSObject.url === chatUrl);
        chatRow.shadowRoot
          .querySelector("moz-checkbox")
          .shadowRoot.querySelector("input")
          .click();
        await ContentTaskUtils.waitForCondition(
          () => chatRow.wrappedJSObject.checked,
          "The chat tab is checked"
        );
        await card.updateComplete;
      }
      card.shadowRoot.querySelector("moz-button[type='primary']").click();
    }
  );
}

/**
 * Wait for the undo button and click it.
 *
 * @param {MozBrowser} aichatBrowser
 */
async function undoTabGroup(aichatBrowser) {
  await SpecialPowers.spawn(aichatBrowser, [], async () => {
    const { shadowRoot } = content.document.querySelector("ai-chat-content");
    await ContentTaskUtils.waitForMutationCondition(
      shadowRoot,
      { childList: true, subtree: true },
      () => shadowRoot.querySelector("ai-action-confirmation")
    );
    const card = shadowRoot.querySelector("ai-action-confirmation");
    await card.updateComplete;
    card.shadowRoot.querySelector(".action-confirmation-undo").click();
  });
}

add_task(async function test_grouping_unpins_tabs_and_undo_pins_them_again() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;

  try {
    const first = await server.openTab({ title: "Pinned tab" });
    const second = await server.openTab({ title: "Other pinned tab" });
    for (const tab of [first.tab, second.tab, chatTab]) {
      await pinTabFromContextMenu(win, tab);
    }
    win.gBrowser.selectedTab = chatTab;

    const aichatBrowser = await requestGroupTabs({
      mockEngineManager,
      chatTab,
      urls: [first.url, second.url],
    });
    await confirmTabGroup(aichatBrowser, { chatTab });

    await TestUtils.waitForCondition(
      () => first.tab.group,
      "The pinned tabs are grouped"
    );
    const groupTabs = first.tab.group.tabs;
    Assert.equal(groupTabs.length, 3, "The group holds three tabs");
    Assert.ok(
      groupTabs.includes(second.tab) && groupTabs.includes(chatTab),
      "The group holds the pinned tabs and the checked chat tab"
    );
    Assert.ok(
      !first.tab.pinned && !second.tab.pinned && !chatTab.pinned,
      "The grouped tabs are unpinned"
    );

    await undoTabGroup(aichatBrowser);

    await TestUtils.waitForCondition(
      () => first.tab.pinned && second.tab.pinned && chatTab.pinned,
      "Undo pins the tabs again"
    );
    Assert.ok(
      !first.tab.group && !second.tab.group && !chatTab.group,
      "Undo ungroups the tabs"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_grouping_moves_grouped_tabs_and_undo_moves_back() {
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });
  const chatTab = win.gBrowser.selectedTab;
  Services.fog.testResetFOG();

  try {
    const grouped = await server.openTab({ title: "Grouped tab" });
    const ungrouped = await server.openTab({ title: "Ungrouped tab" });
    const kept = await server.openTab({ title: "Kept tab" });
    const lone = await server.openTab({ title: "Lone tab" });
    const oldGroup = win.gBrowser.addTabGroup([grouped.tab, kept.tab], {
      label: "Old group",
    });
    const loneGroup = win.gBrowser.addTabGroup([lone.tab], {
      label: "Lone group",
    });
    win.gBrowser.selectedTab = chatTab;

    const aichatBrowser = await requestGroupTabs({
      mockEngineManager,
      chatTab,
      urls: [grouped.url, lone.url, ungrouped.url],
    });
    await confirmTabGroup(aichatBrowser);

    await TestUtils.waitForCondition(
      () => ungrouped.tab.group,
      "The ungrouped tab is grouped"
    );
    const [prompt] = Glean.smartWindow.browserActionPrompt.testGetValue();
    Assert.equal(
      prompt.extra.reason,
      "grouped_tab",
      "The card was shown for the grouped tabs"
    );
    const newGroup = ungrouped.tab.group;
    Assert.notEqual(newGroup, oldGroup, "A new group is created");
    Assert.equal(newGroup.tabs.length, 3, "The new group holds three tabs");
    Assert.ok(
      newGroup.tabs.includes(grouped.tab) && newGroup.tabs.includes(lone.tab),
      "The grouped tabs moved into the new group"
    );
    Assert.deepEqual(
      oldGroup.tabs,
      [kept.tab],
      "The old group keeps its other tab"
    );
    await TestUtils.waitForCondition(
      () => !loneGroup.isConnected,
      "The emptied group is removed"
    );

    await undoTabGroup(aichatBrowser);

    await TestUtils.waitForCondition(
      () => !ungrouped.tab.group,
      "Undo ungroups the new group"
    );
    Assert.equal(grouped.tab.group, oldGroup, "Undo moves a tab back");
    Assert.deepEqual(
      [lone.tab.group.id, lone.tab.group.label, lone.tab.group.color],
      [loneGroup.id, loneGroup.label, loneGroup.color],
      "Undo recreates the removed group"
    );
  } finally {
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
    await server.cleanup();
    await BrowserTestUtils.closeWindow(win);
  }
});
