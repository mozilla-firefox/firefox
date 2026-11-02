/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @type {import("../../../../../../toolkit/components/ml/tests/MLTestUtils.sys.mjs")}
 */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

/**
 * @type {import("../AIWindowTestUtils.sys.mjs")}
 */
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const { GET_OPEN_TABS, MAX_TABS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
);

/**
 * @import { ChatConversation } from "../../modules/ChatConversation.sys.mjs"
 */

// Security caps mirrored from the implementation; keep in lockstep.
const MAX_METADATA_LENGTH = 100;

/**
 * Pin a deterministic lastAccessed order. Writes _lastAccessed directly to dodge
 * the getter's Infinity-for-selected-tab (which falls back to Date.now() and ties).
 *
 * @param {MozTabbrowserTab[]} tabsOldestFirst
 */
function pinLastAccessed(tabsOldestFirst) {
  tabsOldestFirst.forEach((tab, index) => {
    tab._lastAccessed = index + 1;
  });
}

/**
 * @param {ChatConversation} conversation
 */
async function waitForGetOpenTabsResult(conversation) {
  return TestUtils.waitForCondition(
    () =>
      conversation.messages.find(
        m => m.role === MESSAGE_ROLE.TOOL && m.content?.name === GET_OPEN_TABS
      ),
    "A tool result for get_open_tabs should be added to the conversation."
  );
}

/**
 * Drive one chat turn whose mocked model emits a get_open_tabs tool_call, so the
 * real Chat -> Tools path executes getOpenTabs. Returns the tool result message.
 *
 * @param {object} options
 * @param {MockEngineManager} options.mockEngineManager
 * @param {ChatConversation} options.conversation
 * @param {string} options.callId
 */
async function triggerGetOpenTabsToolCall({
  mockEngineManager,
  conversation,
  callId,
}) {
  await mockEngineManager.respondTo({
    purpose: "chat",
    response: {
      text: "",
      tokens: null,
      isPrompt: false,
      toolCalls: [
        {
          id: callId,
          function: { name: GET_OPEN_TABS, arguments: "{}" },
        },
      ],
    },
  });

  return waitForGetOpenTabsResult(conversation);
}

/**
 * Answer the sidebar conversation-starter request, capture the fresh conversation
 * (both security flags still false) before submit, then submit a user message.
 * Mirrors browser_security_chat.js: capturing before submit lets the initial-state
 * assertions observe securityProperties before the chat handler attaches tab info
 * or the tool runs.
 *
 * @param {object} options
 * @param {Window} options.win
 * @param {MozBrowser} options.sidebarBrowser
 * @param {MockEngineManager} options.mockEngineManager
 * @param {string} options.userMessage
 * @returns {Promise<ChatConversation>}
 */
async function startFreshTurn({
  win,
  sidebarBrowser,
  mockEngineManager,
  userMessage,
}) {
  await mockEngineManager.respondTo({
    purpose: "convo-starters-sidebar",
    response: "A suggested conversation starter.",
  });

  /** @type {ChatConversation} */
  const conversation = await TestUtils.waitForCondition(
    () => AIWindow.getActiveConversation(win),
    "Conversation should exist on the active AI window before submit."
  );

  Assert.equal(
    conversation.securityProperties.privateData,
    false,
    "privateData starts false on a fresh conversation."
  );
  Assert.equal(
    conversation.securityProperties.untrustedInput,
    false,
    "untrustedInput starts false on a fresh conversation."
  );

  await typeInSmartbar(sidebarBrowser, userMessage);
  await submitSmartbar(sidebarBrowser);

  return conversation;
}

/**
 * End-to-end: model sees the sanitized url/title/lastAccessed triple, titles are
 * spotlighted/truncated, about: pages filtered, security flags transition.
 */
add_task(async function test_get_open_tabs_returns_sanitized_data() {
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });

  const shortTitle = "Short News Article";
  const longTitle = "A".repeat(MAX_METADATA_LENGTH + 10);
  const trickyTitle = `Sneaky "quote" and \\ backslash`;

  info("Opening three http tabs with controlled titles.");
  const httpTab1 = await server.openTab({
    title: shortTitle,
    body: "<p>Tab body</p>",
  });
  const httpTab2 = await server.openTab({
    title: longTitle,
    body: "<p>Tab body</p>",
  });
  const httpTab3 = await server.openTab({
    title: trickyTitle,
    body: "<p>Tab body</p>",
  });

  info("Opening an about: tab that should be filtered out.");
  const aboutTab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:preferences",
    true
  );

  // Pin after the about tab opens; its selection rewrites httpTab3's _lastAccessed.
  pinLastAccessed([httpTab1.tab, httpTab2.tab, httpTab3.tab]);

  const conversation = await startFreshTurn({
    win,
    sidebarBrowser,
    mockEngineManager,
    userMessage: "What do I have open?",
  });

  const toolMessage = await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs_1",
  });
  const tabs = toolMessage.content.body;
  Assert.ok(Array.isArray(tabs), "Tool result body is an array of tabs.");

  // Only the three http tabs survive isAllowedURL; about: tabs are filtered out.
  Assert.equal(
    tabs.length,
    3,
    "Only http/https tabs are returned; about: tabs are excluded."
  );
  Assert.ok(
    tabs.every(
      t =>
        // eslint-disable-next-line sdl/no-insecure-url
        t.url.startsWith("http://") || t.url.startsWith("https://")
    ),
    "Every returned tab uses http or https."
  );

  Assert.deepEqual(
    tabs.map(t => t.url),
    [httpTab3.url, httpTab2.url, httpTab1.url],
    "Tabs are sorted by lastAccessed (most recent first)."
  );

  // Lock the exact key set: a new field needs security review, as the
  // sanitization below wouldn't cover it.
  for (const tab of tabs) {
    Assert.deepEqual(
      Object.keys(tab).sort(),
      ["lastAccessed", "pinned", "title", "url", "windowId"],
      "Tab info exposes exactly url, title, lastAccessed, windowId, pinned and no other fields."
    );
  }

  const [recordTricky, recordLong, recordShort] = tabs;

  Assert.ok(
    recordShort.title.startsWith('"') && recordShort.title.includes(shortTitle),
    "Short title is wrapped in quotes for spotlighting."
  );
  Assert.ok(
    recordShort.title.endsWith("(Untrusted webpage data)"),
    "Short title is suffixed with the spotlighting tag."
  );

  Assert.ok(
    recordLong.title.includes("A".repeat(MAX_METADATA_LENGTH) + "…"),
    "Long title is truncated to MAX_METADATA_LENGTH and ends with an ellipsis."
  );
  Assert.ok(
    !recordLong.title.includes("A".repeat(MAX_METADATA_LENGTH + 1)),
    "No content past the truncation cap leaks through."
  );

  // Quotes and backslashes are escaped so a title can't break out of the wrapper.
  Assert.ok(
    recordTricky.title.includes('\\"quote\\"'),
    "Embedded double quotes are backslash-escaped."
  );
  Assert.ok(
    recordTricky.title.includes("\\\\"),
    "Embedded backslashes are escaped."
  );

  // privateData advances once tab info is surfaced; untrustedInput stays false
  // because truncation + spotlighting relax it.
  Assert.equal(
    conversation.securityProperties.privateData,
    true,
    "privateData becomes true once tab info enters the conversation."
  );
  Assert.equal(
    conversation.securityProperties.untrustedInput,
    false,
    "untrustedInput stays false because titles are truncated and spotlighted."
  );

  BrowserTestUtils.removeTab(aboutTab);
  await server.cleanup();
  await mockEngineManager.rejectAllRequests();
  mockEngineManager.cleanupMocks();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * End-to-end: no http tabs open -> empty result, but getOpenTabs still raises
 * privateData (it sets it unconditionally) while untrustedInput stays false.
 */
add_task(async function test_get_open_tabs_empty_when_no_http_tabs() {
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  const conversation = await startFreshTurn({
    win,
    sidebarBrowser,
    mockEngineManager,
    userMessage: "What do I have open?",
  });

  const toolMessage = await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs_empty_1",
  });

  Assert.deepEqual(
    toolMessage.content.body,
    [],
    "Returns an empty array when no http tabs are open."
  );
  Assert.equal(
    conversation.securityProperties.privateData,
    true,
    "privateData is raised even when no matching tabs are returned."
  );
  Assert.equal(
    conversation.securityProperties.untrustedInput,
    false,
    "untrustedInput stays false when no tabs are returned."
  );

  await mockEngineManager.rejectAllRequests();
  mockEngineManager.cleanupMocks();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * End-to-end: >MAX_TABS open -> capped, least-recent dropped, most-recent-first.
 */
add_task(async function test_get_open_tabs_caps_and_sorts() {
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });

  const openedTabs = [];
  for (let i = 0; i < MAX_TABS + 1; i++) {
    openedTabs.push(await server.openTab({ body: `<p>Tab ${i}</p>` }));
  }
  pinLastAccessed(openedTabs.map(t => t.tab));

  const conversation = await startFreshTurn({
    win,
    sidebarBrowser,
    mockEngineManager,
    userMessage: "What do I have open?",
  });

  const toolMessage = await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs_cap_1",
  });

  const tabs = toolMessage.content.body;
  Assert.equal(
    tabs.length,
    MAX_TABS,
    "At most MAX_TABS tabs are returned even when more are open."
  );
  Assert.ok(
    !tabs.some(t => t.url === openedTabs[0].url),
    "The least-recently-accessed tab is dropped to fit the cap."
  );

  const expectedUrls = openedTabs
    .slice(1)
    .reverse()
    .map(t => t.url);
  Assert.deepEqual(
    tabs.map(t => t.url),
    expectedUrls,
    "Returned tabs are sorted most-recent-first."
  );

  Assert.equal(
    conversation.securityProperties.privateData,
    true,
    "privateData was raised on the conversation by the tool."
  );
  Assert.equal(
    conversation.securityProperties.untrustedInput,
    false,
    "untrustedInput stays false; tab titles are truncated and spotlighted."
  );

  await mockEngineManager.rejectAllRequests();
  mockEngineManager.cleanupMocks();
  await server.cleanup();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * End-to-end, seen-URLs ledger: getOpenTabs records surfaced http/https URLs,
 * adds to (not clobbers) the already-seen context URL, and never promotes
 * filtered-out about: URLs.
 */
add_task(async function test_get_open_tabs_records_seen_urls() {
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();
  const aboutBlankTab = win.gBrowser.selectedTab;
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: win.gBrowser,
  });

  const httpTabA = await server.openTab({ body: "<p>A</p>" });
  const httpTabB = await server.openTab({ body: "<p>B</p>" });
  await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:preferences",
    true
  );

  // Re-select about:blank so the about:preferences tab is NOT the current tab:
  // that way about:preferences can only enter seenUrls if the tool wrongly
  // promotes it, never via the implicit context mention.
  win.gBrowser.selectedTab = aboutBlankTab;

  const conversation = await startFreshTurn({
    win,
    sidebarBrowser,
    mockEngineManager,
    userMessage: "What do I have open?",
  });

  await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs_seen_1",
  });

  Assert.ok(
    conversation.seenUrls.has(httpTabA.url),
    "The first http tab URL is recorded as seen."
  );
  Assert.ok(
    conversation.seenUrls.has(httpTabB.url),
    "The second http tab URL is recorded as seen."
  );
  Assert.ok(
    conversation.seenUrls.has("about:blank"),
    "The pre-existing context-mention URL is preserved, not clobbered."
  );
  Assert.ok(
    !conversation.seenUrls.has("about:preferences"),
    "The filtered-out about: tab is never promoted into the seen ledger."
  );
  Assert.deepEqual(
    [...conversation.seenUrls].sort(),
    ["about:blank", httpTabA.url, httpTabB.url].sort(),
    "seenUrls contains exactly the context mention plus the two http tab URLs."
  );

  await mockEngineManager.rejectAllRequests();
  mockEngineManager.cleanupMocks();
  await server.cleanup();
  await BrowserTestUtils.closeWindow(win);
});

/**
 * End-to-end: getOpenTabs aggregates tabs from all AI windows only; a noise tab
 * in the main non-AI window must be excluded.
 */
add_task(async function test_get_open_tabs_aggregates_across_ai_windows() {
  const mockEngineManager = new MockEngineManager();
  const { win: winA, sidebarBrowser } = await openAIWindowWithSidebar();
  const winB = await openAIWindow();
  const server = await MLTestUtils.serveSharedHTMLInTab({
    browser: winA.gBrowser,
  });

  const tabInWinB = await server.openTab({
    title: "Tab in AI window B",
    body: "<p>B</p>",
    browser: winB.gBrowser,
  });

  const noiseTab = await server.openTab({
    title: "Tab in non-AI window",
    body: "<p>noise</p>",
    browser: gBrowser,
  });

  const tabAInWinA = await server.openTab({
    title: "Tab in AI window A",
    body: "<p>A</p>",
  });
  const tabBInWinA = await server.openTab({
    title: "Second tab in window A",
    body: "<p>A2</p>",
  });

  pinLastAccessed([tabAInWinA.tab, tabBInWinA.tab, tabInWinB.tab]);

  await SimpleTest.promiseFocus(winA);

  const conversation = await startFreshTurn({
    win: winA,
    sidebarBrowser,
    mockEngineManager,
    userMessage: "What do I have open across my windows?",
  });

  const toolMessage = await triggerGetOpenTabsToolCall({
    mockEngineManager,
    conversation,
    callId: "call_get_open_tabs_aggregate_1",
  });

  const tabs = toolMessage.content.body;
  Assert.equal(
    tabs.length,
    3,
    "Only the three AI-window tabs are returned; non-AI window is filtered out."
  );
  Assert.deepEqual(
    new Set(tabs.map(t => t.url)),
    new Set([tabAInWinA.url, tabBInWinA.url, tabInWinB.url]),
    "Result contains exactly the tabs from both AI windows."
  );
  Assert.ok(
    !tabs.some(t => t.url === noiseTab.url),
    "Tabs from non-AI windows are excluded."
  );
  Assert.equal(
    tabs[0].url,
    tabInWinB.url,
    "Most-recently-accessed tab (winB's, pinned newer) sorts first across windows."
  );

  const tabA = tabs.find(tab => tab.url === tabAInWinA.url);
  const tabA2 = tabs.find(tab => tab.url === tabBInWinA.url);
  const tabB = tabs.find(tab => tab.url === tabInWinB.url);
  Assert.equal(
    tabA.windowId,
    SessionStore.getWindowId(winA),
    "Tab matches the SessionStore ID of the window holding it."
  );
  Assert.equal(
    tabA2.windowId,
    SessionStore.getWindowId(winA),
    "Tabs in the same window have matching windowId."
  );
  Assert.equal(
    tabB.windowId,
    SessionStore.getWindowId(winB),
    "Tab in window B matches the window ID."
  );
  Assert.notEqual(
    tabA.windowId,
    tabB.windowId,
    "Tabs in different windows are distinguishable by window ID."
  );
  Assert.equal(
    conversation.securityProperties.privateData,
    true,
    "privateData is raised once tabs from the AI windows enter the conversation."
  );
  Assert.equal(
    conversation.securityProperties.untrustedInput,
    false,
    "untrustedInput stays false; tab titles are truncated and spotlighted."
  );

  BrowserTestUtils.removeTab(noiseTab.tab);
  await mockEngineManager.rejectAllRequests();
  mockEngineManager.cleanupMocks();
  await server.cleanup();
  await BrowserTestUtils.closeWindow(winA);
  await BrowserTestUtils.closeWindow(winB);
});
