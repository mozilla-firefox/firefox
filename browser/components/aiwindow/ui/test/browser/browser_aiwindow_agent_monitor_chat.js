/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

async function setupConversationWithToolUI(aichatBrowser, data) {
  await SpecialPowers.spawn(aichatBrowser, [data], async config => {
    const chatContent = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("ai-chat-content"),
      "Wait for ai-chat-content element"
    );

    const chatContentJS = chatContent.wrappedJSObject || chatContent;
    await chatContent.updateComplete;

    const messages = [
      {
        role: "user",
        body: config.userMessage,
        ordinal: 0,
        convId: "test-conv",
      },
      {
        role: "assistant",
        body: config.assistantMessage,
        ordinal: 1,
        convId: "test-conv",
        messageId: config.messageId,
        appliedMemories: [],
        isLastChunk: true,
        toolUIData: config.toolUIData,
      },
    ];

    chatContentJS.conversationState = Cu.cloneInto(messages, content);
    await chatContent.updateComplete;
  });
}

const MONITOR_CARD = {
  userMessage: "monitor this page",
  assistantMessage: "Set up a monitor for this page.",
  messageId: "monitor-msg-1",
  toolUIData: {
    toolCallId: "monitor-call-1",
    uiType: "agent-monitor-item",
    properties: {
      agent: {
        monitorName: "Example product",
        url: "https://example.com/product",
        watchUrls: ["https://example.com/product"],
        condition: "the price drops",
        conditionPresets: [],
      },
    },
  },
};

const DISPLAY_MONITOR_CARD = {
  userMessage: "monitor this page",
  assistantMessage: "Watching this page.",
  messageId: "monitor-msg-2",
  toolUIData: {
    toolCallId: "monitor-call-2",
    uiType: "agent-monitor-item",
    properties: {
      mode: "display",
      agent: {
        id: "monitor-id-2",
        monitorName: "Example product",
        url: "https://example.com/product",
        watchUrls: ["https://example.com/product"],
        condition: "the price drops",
        status: { label: "Watching", kind: "watching" },
        schedule: { frequency: "daily", time: "09:00", weekday: "1" },
      },
    },
  },
};

/**
 * Clicks an action in the expanded display card and returns the detail of the
 * event it makes the chat dispatch.
 *
 * @param {string} selector - Action to click, inside the card's shadow root
 * @param {object} [options]
 * @param {string} [options.matchText]
 * @param {boolean} [options.edit] - Open edit mode before clicking
 * @param {string} [options.eventType] - Event to capture on ai-chat-content
 * @param {string} [options.innerSelector] - Click this inside the action's own
 *  shadow root instead of the action itself, for nested components like chips
 * @returns {Promise<object|null>} The captured event detail
 */
async function getDisplayCardActionDetail(
  selector,
  {
    matchText,
    edit = false,
    eventType = "AIChatContent:ToolUIUpdate",
    innerSelector = "",
  } = {}
) {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);
    await setupConversationWithToolUI(aichatBrowser, DISPLAY_MONITOR_CARD);

    return await SpecialPowers.spawn(
      aichatBrowser,
      [{ selector, matchText, edit, eventType, innerSelector }],
      async args => {
        const chatContent = content.document.querySelector("ai-chat-content");
        const card = await ContentTaskUtils.waitForCondition(
          () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
          "Wait for agent-monitor-item"
        );
        await card.updateComplete;
        Assert.equal(
          card.getAttribute("mode"),
          "display",
          "card renders in display mode"
        );

        card.shadowRoot.querySelector(".chev").click();
        await ContentTaskUtils.waitForCondition(
          () => card.shadowRoot.querySelector(".watch-expand"),
          "Wait for the expanded body"
        );

        if (args.edit) {
          card.shadowRoot.querySelector("#edit-button").click();
          await ContentTaskUtils.waitForCondition(
            () =>
              card.shadowRoot.querySelector(
                'moz-button[data-l10n-id="ai-tasks-alert-save-button"]'
              ),
            "Wait for edit mode Save button"
          );
        }

        let detail = null;
        chatContent.addEventListener(args.eventType, e => (detail = e.detail), {
          once: true,
        });

        const action = card.shadowRoot.querySelector(args.selector);
        Assert.ok(action, `Action (${args.selector}) exists`);
        let button = action;
        if (args.innerSelector) {
          button = action.shadowRoot.querySelector(args.innerSelector);
          Assert.ok(button, `Action target (${args.innerSelector}) exists`);
        }
        button.click();
        await new Promise(resolve => content.setTimeout(resolve, 0));
        return detail;
      }
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.agent.enabled", true]],
  });
});

add_task(async function test_agent_monitor_item_renders_in_chat() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item to render"
      );

      const card = chatContent.shadowRoot.querySelector("agent-monitor-item");
      Assert.ok(card, "agent-monitor-item should render");
      Assert.equal(
        card.getAttribute("mode"),
        "create",
        "card is rendered in create mode"
      );

      const parent = card.parentElement.parentElement;
      Assert.ok(
        parent?.classList.contains("chat-bubble-assistant"),
        "card is inside the assistant message bubble"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_create_card_autofocuses_task_name() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      const card = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item"
      );
      await card.updateComplete;

      // Keyboard users must land on the task name field without a click.
      await ContentTaskUtils.waitForCondition(() => {
        const nameInput = card.shadowRoot.querySelector(".monitor-name-input");
        return nameInput && card.shadowRoot.activeElement === nameInput;
      }, "The task name field is focused when a create card is seeded");
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_monitor_start_dispatches_create_update() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      const card = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item"
      );
      await card.updateComplete;

      let detail = null;
      chatContent.addEventListener(
        "AIChatContent:ToolUIUpdate",
        e => (detail = e.detail),
        { once: true }
      );

      const startButton = card.shadowRoot.querySelector(
        'moz-button[data-l10n-id="ai-tasks-alert-create-button"]'
      );
      Assert.ok(startButton, "Create alert button exists");
      startButton.click();
      await new Promise(resolve => content.setTimeout(resolve, 0));

      Assert.ok(detail, "AIChatContent:ToolUIUpdate should fire on submit");
      Assert.equal(
        detail.updateType,
        "create-watch",
        "updateType is create-watch"
      );
      Assert.equal(
        detail.messageId,
        "monitor-msg-1",
        "messageId is correlated back"
      );
      Assert.equal(
        detail.toolCallId,
        "monitor-call-1",
        "toolCallId is correlated back"
      );
      Assert.equal(
        detail.updateData?.mode,
        "create",
        "submit payload reports create mode"
      );
      Assert.deepEqual(
        detail.updateData?.watchUrls,
        ["https://example.com/product"],
        "submit payload carries the seeded watch URL"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_form_edit_dispatches_draft_update() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);

    const detail = await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      const card = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item"
      );
      await card.updateComplete;

      let captured = null;
      chatContent.addEventListener(
        "AIChatContent:ToolUIUpdate",
        e => (captured = e.detail),
        { once: true }
      );

      const nameInput = card.shadowRoot.querySelector(
        "moz-input-text.monitor-name-input"
      );
      // value is a Lit property, so it has to be set on the element itself and
      // not on the Xray wrapper, or the card reads it back empty
      const nameInputJS = nameInput.wrappedJSObject || nameInput;
      nameInputJS.value = "Example product";
      nameInput.dispatchEvent(new content.Event("change", { bubbles: true }));
      await new Promise(resolve => content.setTimeout(resolve, 0));

      return captured;
    });

    Assert.ok(detail, "AIChatContent:ToolUIUpdate fires on a form edit");
    Assert.equal(
      detail.updateType,
      "save-watch-draft",
      "updateType is save-watch-draft"
    );
    Assert.equal(
      detail.messageId,
      "monitor-msg-1",
      "messageId is correlated back"
    );
    Assert.equal(
      detail.toolCallId,
      "monitor-call-1",
      "toolCallId is correlated back"
    );
    Assert.equal(
      detail.updateData?.draft?.monitorName,
      "Example product",
      "draft payload carries the edited field"
    );
    Assert.deepEqual(
      detail.updateData?.draft?.watchUrls,
      ["https://example.com/product"],
      "draft payload is a full snapshot of the form, not just the edit"
    );
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_monitor_cancel_dispatches_cancel_update() {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const browser = win.gBrowser.selectedBrowser;
    const aichatBrowser = await getAichatBrowser(browser);

    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);

    await SpecialPowers.spawn(aichatBrowser, [], async () => {
      const chatContent = content.document.querySelector("ai-chat-content");

      const card = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item"
      );
      await card.updateComplete;

      let detail = null;
      chatContent.addEventListener(
        "AIChatContent:ToolUIUpdate",
        e => (detail = e.detail),
        { once: true }
      );

      const cancelButton = card.shadowRoot.querySelector(
        "#cancel-create-button"
      );
      Assert.ok(cancelButton, "Cancel button exists");
      cancelButton.click();
      await new Promise(resolve => content.setTimeout(resolve, 0));

      Assert.ok(detail, "AIChatContent:ToolUIUpdate should fire on cancel");
      Assert.equal(
        detail.updateType,
        "cancel-watch",
        "updateType is cancel-watch"
      );
      Assert.equal(
        detail.messageId,
        "monitor-msg-1",
        "messageId is correlated back"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

add_task(async function test_display_save_dispatches_update_update() {
  const detail = await getDisplayCardActionDetail(
    'moz-button[data-l10n-id="ai-tasks-alert-save-button"]',
    {
      edit: true,
    }
  );
  Assert.ok(detail, "ToolUIUpdate fires when saving an edit");
  Assert.equal(
    detail.updateType,
    "update-watch",
    "saving a display card routes to update-watch, not create"
  );
  Assert.equal(
    detail.updateData?.mode,
    "display",
    "submit reports display mode"
  );
  Assert.equal(detail.updateData?.id, "monitor-id-2", "submit carries the id");
});

add_task(async function test_delete_dispatches_delete_update() {
  const detail = await getDisplayCardActionDetail(
    'moz-button[data-l10n-id="ai-tasks-alert-delete-button"]'
  );
  Assert.ok(detail, "ToolUIUpdate fires on delete");
  Assert.equal(detail.updateType, "delete-watch", "updateType is delete-watch");
  Assert.deepEqual(
    detail.updateData,
    { id: "monitor-id-2" },
    "delete carries the monitor id"
  );
});

add_task(async function test_pause_dispatches_pause_update() {
  const detail = await getDisplayCardActionDetail(
    'moz-button[data-l10n-id="ai-tasks-alert-pause-button"]'
  );
  Assert.ok(detail, "ToolUIUpdate fires on pause");
  Assert.equal(detail.updateType, "pause-watch", "updateType is pause-watch");
  Assert.deepEqual(
    detail.updateData,
    { id: "monitor-id-2", paused: true },
    "pause carries the id and desired paused state"
  );
});

add_task(async function test_check_now_dispatches_check_update() {
  const detail = await getDisplayCardActionDetail(
    'moz-button[data-l10n-id="ai-tasks-alert-check-now-button"]',
    {}
  );
  Assert.ok(detail, "ToolUIUpdate fires on check now");
  Assert.equal(detail.updateType, "check-watch", "updateType is check-watch");
  Assert.deepEqual(
    detail.updateData,
    { id: "monitor-id-2" },
    "check now carries the monitor id"
  );
});

add_task(async function test_watch_url_chip_dispatches_open_link() {
  const detail = await getDisplayCardActionDetail("smartwindow-chip", {
    eventType: "AIChatContent:OpenLink",
    innerSelector: ".chip",
  });
  Assert.ok(detail, "OpenLink fires when a watched page chip is clicked");
  Assert.equal(
    detail.url,
    "https://example.com/product",
    "OpenLink carries the full watched page URL"
  );
  Assert.ok(
    detail.preferSwitchToTab,
    "A watched page prefers an already open tab"
  );
});

add_task(async function test_create_card_chip_shows_open_tab_title() {
  const OPEN_URL =
    "https://example.com/browser/browser/components/aiwindow/ui/test/browser/test_context_url_page.html";
  const CLOSED_URL = "https://example.org/not-open";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, OPEN_URL);
  const openTitle = tab.linkedBrowser.contentTitle;
  Assert.ok(openTitle, "The open test page has a title");

  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const win = await openAIWindow();

  try {
    const aichatBrowser = await getAichatBrowser(win.gBrowser.selectedBrowser);
    await setupConversationWithToolUI(aichatBrowser, {
      ...MONITOR_CARD,
      toolUIData: {
        ...MONITOR_CARD.toolUIData,
        properties: {
          agent: {
            ...MONITOR_CARD.toolUIData.properties.agent,
            url: OPEN_URL,
            watchUrls: [OPEN_URL, CLOSED_URL],
          },
        },
      },
    });

    await SpecialPowers.spawn(aichatBrowser, [openTitle], async title => {
      const chatContent = content.document.querySelector("ai-chat-content");
      const card = await ContentTaskUtils.waitForCondition(
        () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
        "Wait for agent-monitor-item"
      );
      const chipLabels = () =>
        [
          ...card.shadowRoot.querySelectorAll(
            ".page-pills-row smartwindow-chip"
          ),
        ].map(chip => chip.getAttribute("label"));

      await ContentTaskUtils.waitForCondition(
        () => chipLabels()[0] === title,
        "Wait for the open tab's title to reach its chip"
      );
      Assert.deepEqual(
        chipLabels(),
        [title, "example.org"],
        "An open page is labelled with its tab title, others keep the hostname"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    BrowserTestUtils.removeTab(tab);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
});

// The sidebar's chat header lives in chrome and floats over the chat document,
// so a dropdown placed against the top of the viewport is painted over and the
// header's buttons take the clicks meant for its first row (bug 2073461).

/**
 * Opens the create card's time dropdown with the field pinned to an edge of
 * the chat viewport, so the list has to open in a known direction.
 *
 * @param {MozBrowser} aichatBrowser
 * @param {"top"|"bottom"} pinTo - Viewport edge to pin the time field to
 * @returns {Promise<object>} Where the list was placed and where it ended up
 */
function showTimeDropdown(aichatBrowser, pinTo) {
  return SpecialPowers.spawn(aichatBrowser, [pinTo], async edge => {
    const chatContent = content.document.querySelector("ai-chat-content");
    const cardEl = await ContentTaskUtils.waitForCondition(
      () => chatContent.shadowRoot.querySelector("agent-monitor-item"),
      "Wait for agent-monitor-item"
    );
    const card = cardEl.wrappedJSObject || cardEl;
    await card.updateComplete;

    // The time field is the last of the scheduler's selects.
    const selects = cardEl.shadowRoot.querySelectorAll("moz-select");
    const timeSelectEl = selects[selects.length - 1];
    const timeSelect = timeSelectEl.wrappedJSObject || timeSelectEl;
    timeSelectEl.style.position = "fixed";
    timeSelectEl.style.insetInlineStart = "10px";
    timeSelectEl.style[edge === "top" ? "insetBlockStart" : "insetBlockEnd"] =
      "0px";
    await timeSelect.updateComplete;

    const panel = timeSelect.panelList;
    Assert.ok(panel, "The time field opens a panel-list");

    // panel-list dispatches "shown" at its own target before the event bubbles
    // to the chat, so this records where the list was placed before the chat
    // corrects it.
    let placed;
    panel.addEventListener(
      "shown",
      () => {
        const rect = panel.getBoundingClientRect();
        placed = { top: rect.top, bottom: rect.bottom };
      },
      { once: true }
    );
    const shown = new Promise(resolve =>
      panel.addEventListener("shown", resolve, { once: true })
    );
    timeSelect.panelTrigger.click();
    await shown;

    const rect = panel.getBoundingClientRect();
    return {
      valign: panel.getAttribute("valign"),
      placed,
      corrected: { top: rect.top, bottom: rect.bottom },
      viewportHeight: content.innerHeight,
    };
  });
}

/**
 * How far the floating chat header reaches into the chat document.
 *
 * @param {MozBrowser} sidebarBrowser
 * @param {MozBrowser} aichatBrowser
 * @returns {number}
 */
function getHeaderBottom(sidebarBrowser, aichatBrowser) {
  const aiWindow = sidebarBrowser.contentDocument.querySelector("ai-window");
  const header = aiWindow.shadowRoot.querySelector(".sidebar-header");
  return (
    header.getBoundingClientRect().bottom -
    aichatBrowser.getBoundingClientRect().top
  );
}

/**
 * Opens a sidebar chat holding the monitor create card.
 *
 * @param {Function} task - Receives { sidebarBrowser, aichatBrowser }
 */
async function withMonitorCardInSidebar(task) {
  const restoreSignIn = skipSignIn();
  const mockEngineManager = new MockEngineManager();
  const { win, sidebarBrowser } = await openAIWindowWithSidebar();

  try {
    const aichatBrowser = await getAichatBrowser(sidebarBrowser);
    await setupConversationWithToolUI(aichatBrowser, MONITOR_CARD);
    await task({ sidebarBrowser, aichatBrowser });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    restoreSignIn();
    await mockEngineManager.rejectAllRequests();
    mockEngineManager.cleanupMocks();
  }
}

add_task(async function test_time_dropdown_opening_up_clears_the_header() {
  await withMonitorCardInSidebar(async ({ sidebarBrowser, aichatBrowser }) => {
    const headerBottom = getHeaderBottom(sidebarBrowser, aichatBrowser);
    Assert.greater(headerBottom, 0, "The chat header floats over the chat");

    const { valign, placed, corrected } = await showTimeDropdown(
      aichatBrowser,
      "bottom"
    );

    Assert.equal(valign, "top", "The list opens above the time field");
    Assert.greaterOrEqual(
      corrected.top,
      headerBottom,
      "The list starts below the header floating over the chat"
    );
    Assert.greater(
      corrected.top,
      placed.top,
      "The list was moved out of the header's strip"
    );
    // Moving the list down has to cost it height, not slide it down over the
    // field it belongs to.
    Assert.lessOrEqual(
      Math.round(corrected.bottom),
      Math.round(placed.bottom),
      "The list still ends where it did, at the time field"
    );
  });
});

// A field scrolled up under the header would otherwise have its list open
// downwards from underneath it.
add_task(async function test_time_dropdown_opening_down_clears_the_header() {
  await withMonitorCardInSidebar(async ({ sidebarBrowser, aichatBrowser }) => {
    const headerBottom = getHeaderBottom(sidebarBrowser, aichatBrowser);

    const { valign, corrected, viewportHeight } = await showTimeDropdown(
      aichatBrowser,
      "top"
    );

    Assert.equal(valign, "bottom", "The list opens below the time field");
    Assert.greaterOrEqual(
      corrected.top,
      headerBottom,
      "The list starts below the header floating over the chat"
    );
    Assert.lessOrEqual(
      Math.round(corrected.bottom),
      viewportHeight,
      "Moving the list down keeps it inside the viewport"
    );
  });
});
