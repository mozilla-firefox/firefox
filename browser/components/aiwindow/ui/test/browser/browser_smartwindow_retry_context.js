/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UserRoleOpts, AssistantRoleOpts } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
);
const { MESSAGE_ROLE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
);

// Regression test for Bug 2023557: clicking retry on an assistant response
// must re-send the original user prompt with its @mention context intact.
add_task(async function test_retry_preserves_context_mentions() {
  const sb = this.sinon.createSandbox();
  let win;

  try {
    const fetchWithHistoryStub = sb.stub(this.Chat, "fetchWithHistory");
    sb.stub(this.openAIEngine, "build").resolves({
      loadPrompt: () => Promise.resolve("Mock system prompt"),
    });
    const injectRealTimeContextStub = sb
      .stub(this.ChatConversation.prototype, "injectRealTimeContext")
      .resolves();

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const contextMentions = [
      {
        type: "tab",
        url: "https://example.com/mentioned",
        label: "Mentioned page",
      },
    ];

    const conversation = new this.ChatConversation({});
    conversation.addUserMessage(
      "Summarize this",
      null,
      new UserRoleOpts({ contextMentions })
    );
    conversation.addAssistantMessage(
      "text",
      "Initial response",
      new AssistantRoleOpts()
    );
    const assistantMessageId = conversation.messages.at(-1).id;

    aiWindow.openConversation(conversation);

    aiWindow.handleFooterAction({
      action: "retry",
      messageId: assistantMessageId,
    });

    await TestUtils.waitForCondition(
      () => fetchWithHistoryStub.calledOnce,
      "fetchWithHistory should be called once for the retry"
    );

    const retryConversation =
      fetchWithHistoryStub.firstCall.args[0].conversation;
    const userMessage = retryConversation.messages.findLast(
      m => m.role === MESSAGE_ROLE.USER
    );

    Assert.deepEqual(
      userMessage?.content?.contextMentions,
      contextMentions,
      "Retried user message preserves contextMentions"
    );

    Assert.ok(
      injectRealTimeContextStub.calledOnce,
      "injectRealTimeContext should be called once during retry"
    );
    Assert.deepEqual(
      injectRealTimeContextStub.firstCall.args[1]?.contextMentions,
      contextMentions,
      "injectRealTimeContext receives the original contextMentions on retry"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
  }
});

add_task(async function test_retry_from_reply_after_tool_call() {
  const sb = this.sinon.createSandbox();
  let win;

  try {
    const fetchWithHistoryStub = sb.stub(this.Chat, "fetchWithHistory");
    sb.stub(this.openAIEngine, "build").resolves({
      loadPrompt: () => Promise.resolve("Mock system prompt"),
    });
    sb.stub(
      this.ChatConversation.prototype,
      "injectRealTimeContext"
    ).resolves();

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const aiWindow = await TestUtils.waitForCondition(
      () => browser.contentDocument?.querySelector("ai-window"),
      "Wait for ai-window element"
    );

    const conversation = new this.ChatConversation({});
    conversation.addUserMessage("What tabs do I have open?");
    conversation.addAssistantMessage("text", "");
    conversation.addAssistantMessage("function", {
      tool_calls: [
        {
          id: "call_1",
          type: "function",
          function: { name: "get_open_tabs", arguments: "{}" },
        },
      ],
    });
    conversation.addToolCallMessage({
      tool_call_id: "call_1",
      name: "get_open_tabs",
      body: [],
    });
    const reply = conversation.addAssistantMessage(
      "text",
      "You have no open tabs."
    );

    aiWindow.openConversation(conversation);

    aiWindow.handleFooterAction({ action: "retry", messageId: reply.id });

    await TestUtils.waitForCondition(
      () => fetchWithHistoryStub.calledOnce,
      "fetchWithHistory should be called once for the retry"
    );

    const retryConversation =
      fetchWithHistoryStub.firstCall.args[0].conversation;
    Assert.equal(
      retryConversation.messages.findLast(m => m.role === MESSAGE_ROLE.USER)
        ?.content?.body,
      "What tabs do I have open?",
      "Retry re-sends the turn's user prompt"
    );
    Assert.ok(
      !retryConversation.messages.some(m => m.role === MESSAGE_ROLE.TOOL),
      "Retry removes the tool call results of the retried turn"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    sb.restore();
  }
});
