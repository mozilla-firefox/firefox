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

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["places.semanticHistory.smartwindow.featureGate", false],
    ],
  });
});

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
