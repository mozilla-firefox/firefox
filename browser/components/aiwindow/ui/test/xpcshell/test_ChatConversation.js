/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const STATE_CHOOSE = "choose";

do_get_profile();

const { ChatConversation, MESSAGE_ROLE, ChatMessage } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
  );

const { SecurityProperties } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/SecurityProperties.sys.mjs"
);

const { MEMORIES_FLAG_SOURCE, SYSTEM_PROMPT_TYPE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs"
);

const { UserRoleOpts, AssistantRoleOpts, ToolRoleOpts } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatMessage.sys.mjs"
  );

const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);

const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);

const { EmbeddingsGenerator } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs"
);

const { _setLoadPromptForTesting } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatConversation.sys.mjs"
);
const { _setRemoteClientForTesting, _clearRemoteClientForTesting } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

function setTextSystemMessage(conversation, body, version) {
  return conversation.setSystemMessage({
    type: SYSTEM_PROMPT_TYPE.TEXT,
    body,
    ...(version && { version }),
  });
}

function addLegacySystemMessage(conversation, type, body, version) {
  return conversation.addMessage(
    MESSAGE_ROLE.SYSTEM,
    { type, body, ...(version && { version }) },
    conversation.currentTurnIndex()
  );
}

add_task(function test_ChatConversation_constructor_defaults() {
  const conversation = new ChatConversation({});

  Assert.withSoftAssertions(function (soft) {
    soft.equal(conversation.id.length, 36);
    soft.ok(Array.isArray(conversation.messages));
    soft.ok(!isNaN(conversation.createdDate));
    soft.ok(!isNaN(conversation.updatedDate));
    soft.strictEqual(conversation.title, undefined);
    soft.strictEqual(conversation.description, undefined);
    soft.strictEqual(conversation.pageUrl, undefined);
    soft.strictEqual(conversation.pageMeta, undefined);
    soft.strictEqual(conversation.memoriesToggled, null);
  });
});

add_task(function test_ChatConversation_memoriesToggled_property() {
  Assert.withSoftAssertions(soft => {
    // Test default value
    const conversation1 = new ChatConversation({});
    soft.equal(
      conversation1.memoriesToggled,
      null,
      "Default memoriesToggled should be null"
    );

    // Test setting via constructor
    const conversation2 = new ChatConversation({ memoriesToggled: true });
    soft.equal(
      conversation2.memoriesToggled,
      true,
      "Constructor should set memoriesToggled to true"
    );

    const conversation3 = new ChatConversation({ memoriesToggled: false });
    soft.equal(
      conversation3.memoriesToggled,
      false,
      "Constructor should set memoriesToggled to false"
    );

    // Test setting via property
    conversation1.memoriesToggled = true;
    soft.equal(
      conversation1.memoriesToggled,
      true,
      "Should be able to set memoriesToggled to true"
    );

    conversation1.memoriesToggled = false;
    soft.equal(
      conversation1.memoriesToggled,
      false,
      "Should be able to set memoriesToggled to false"
    );

    conversation1.memoriesToggled = null;
    soft.equal(
      conversation1.memoriesToggled,
      null,
      "Should be able to reset memoriesToggled to null"
    );
  });
});

add_task(function test_ChatConversation_addMessage() {
  const conversation = new ChatConversation({});

  const content = {
    type: "text",
    content: "hello world",
    userContext: {},
  };

  conversation.addMessage(MESSAGE_ROLE.USER, content, 0, {
    pageUrl: new URL("https://www.mozilla.com"),
  });

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.strictEqual(message.role, MESSAGE_ROLE.USER);
    soft.strictEqual(message.content, content);
    soft.strictEqual(message.pageUrl.href, "https://www.mozilla.com/");
    soft.strictEqual(message.turnIndex, 0);
  });
});

add_task(function test_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.USER);
    soft.equal(message.turnIndex, 1);
    soft.deepEqual(message.pageUrl, new URL("https://www.mozilla.com"));
    soft.deepEqual(message.content, {
      type: "text",
      body: "user to assistant msg",
      userContext: {},
      contextPageUrl: "https://www.mozilla.com/",
    });
  });
});

add_task(function test_revisionRootMessageId_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, "https://www.firefox.com");

  const message = conversation.messages[0];

  Assert.equal(message.revisionRootMessageId, message.id);
});

add_task(function test_opts_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(
    content,
    "https://www.firefox.com",
    new UserRoleOpts({ revisionRootMessageId: "321" })
  );

  const message = conversation.messages[0];

  Assert.equal(message.revisionRootMessageId, "321");
});

add_task(async function test_userContext_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  const userContext = { testContextInfo: "123" };
  conversation.addUserMessage(
    content,
    "https://www.firefox.com",
    new UserRoleOpts({ revisionRootMessageId: "321" }),
    userContext
  );

  const message = conversation.messages[0];

  Assert.deepEqual(message.content.userContext, userContext);
});

add_task(function test_contextPageUrl_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  conversation.addUserMessage(
    "user msg",
    new URL("https://www.mozilla.com/page")
  );

  const message = conversation.messages[0];

  Assert.equal(message.content.contextPageUrl, "https://www.mozilla.com/page");
});

add_task(function test_noContextPageUrl_ChatConversation_addUserMessage() {
  const conversation = new ChatConversation({});

  conversation.addUserMessage("user msg", null);

  const message = conversation.messages[0];

  Assert.ok(!("contextPageUrl" in message.content));
});

add_task(function test_ChatConversation_addAssistantMessage() {
  const conversation = new ChatConversation({});

  const content = "response from assistant";
  conversation.addAssistantMessage("text", content);

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.ASSISTANT);
    soft.equal(message.turnIndex, 0);
    soft.deepEqual(message.pageUrl, null);
    soft.deepEqual(message.content, {
      type: "text",
      body: "response from assistant",
    });
    soft.strictEqual(message.modelId, null, "modelId should default to false");
    soft.strictEqual(message.params, null, "params should default to null");
    soft.strictEqual(message.usage, null, "usage should default to null");
    soft.strictEqual(
      message.memoriesEnabled,
      false,
      "memoriesEnabled should default to false"
    );
    soft.strictEqual(
      message.memoriesFlagSource,
      null,
      "memoriesFlagSource should default to null"
    );
    soft.deepEqual(
      message.memoriesApplied,
      [],
      "memoriesApplied should default to emtpy array"
    );
    soft.deepEqual(
      message.webSearchQueries,
      [],
      "webSearchQueries should default to emtpy array"
    );
  });
});

add_task(function test_ChatConversation_addAssistantWithL10nMessage() {
  const conversation = new ChatConversation({});

  const events = [];
  const onUpdate = (_event, m) => events.push(["update", m]);
  const onComplete = (_event, m) => events.push(["complete", m]);
  conversation.on("chat-conversation:message-update", onUpdate);
  conversation.on("chat-conversation:message-complete", onComplete);

  const message = conversation.addAssistantWithL10nMessage(
    "smartwindow-agent-monitor-limit-reached",
    { count: 5 },
    { l10nName: "tasks", href: "about:smartwindowtasks" }
  );

  conversation.off("chat-conversation:message-update", onUpdate);
  conversation.off("chat-conversation:message-complete", onComplete);

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.ASSISTANT);
    soft.deepEqual(message.content, {
      type: "text",
      body: "",
      l10nId: "smartwindow-agent-monitor-limit-reached",
      l10nArgs: { count: 5 },
      link: { l10nName: "tasks", href: "about:smartwindowtasks" },
    });

    soft.ok(
      events.some(([type, m]) => type === "update" && m === message),
      "emits chat-conversation:message-update"
    );
    soft.ok(
      events.some(([type, m]) => type === "complete" && m === message),
      "emits chat-conversation:message-complete"
    );
  });
});

add_task(function test_opts_ChatConversation_addAssistantMessage() {
  const conversation = new ChatConversation({});

  const content = "response from assistant";
  const assistantOpts = new AssistantRoleOpts(
    "the-model-id",
    { some: "params for model" },
    { usage: "data" },
    true,
    1,
    ["memory"],
    ["search"]
  );
  conversation.addAssistantMessage("text", content, assistantOpts);

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.ASSISTANT);
    soft.equal(message.turnIndex, 0);
    soft.deepEqual(message.pageUrl, null);
    soft.deepEqual(message.content, {
      type: "text",
      body: "response from assistant",
    });
    soft.strictEqual(
      message.modelId,
      "the-model-id",
      "modelId should be 'the-model-id'"
    );
    soft.deepEqual(
      message.params,
      { some: "params for model" },
      'params should equal { some: "params for model"}'
    );
    soft.deepEqual(
      message.usage,
      { usage: "data" },
      'usage should equal {"usage": "data"}'
    );
    soft.strictEqual(
      message.memoriesEnabled,
      true,
      "memoriesEnabled should equal true"
    );
    soft.strictEqual(
      message.memoriesFlagSource,
      1,
      "memoriesFlagSource equal 1"
    );
    soft.deepEqual(
      message.memoriesApplied,
      ["memory"],
      "memoriesApplied should equal ['memory']"
    );
    soft.deepEqual(
      message.webSearchQueries,
      ["search"],
      "memoriesApplied should equal ['search']"
    );
  });
});

add_task(function test_ChatConversation_addToolCallMessage() {
  const conversation = new ChatConversation({});

  const content = {
    random: "tool call specific keys",
  };
  conversation.addToolCallMessage(content);

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.TOOL);
    soft.equal(message.turnIndex, 0);
    soft.deepEqual(message.pageUrl, null);
    soft.deepEqual(message.content, {
      random: "tool call specific keys",
    });
    soft.equal(message.modelId, null, "modelId should default to null");
  });
});

add_task(function test_opts_ChatConversation_addToolCallMessage() {
  const conversation = new ChatConversation({});

  const content = {
    random: "tool call specific keys",
  };
  conversation.addToolCallMessage(content, new ToolRoleOpts("the-model-id"));

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.TOOL);
    soft.equal(message.turnIndex, 0);
    soft.deepEqual(message.pageUrl, null);
    soft.deepEqual(message.content, {
      random: "tool call specific keys",
    });
    soft.equal(
      message.modelId,
      "the-model-id",
      "modelId should equal the-model-id"
    );
  });
});

add_task(function test_ChatConversation_setTextSystemMessage() {
  const conversation = new ChatConversation({});

  const content = {
    random: "system call specific keys",
  };
  setTextSystemMessage(conversation, content);

  const message = conversation.messages[0];

  Assert.withSoftAssertions(function (soft) {
    soft.equal(message.role, MESSAGE_ROLE.SYSTEM);
    soft.equal(message.turnIndex, 0);
    soft.deepEqual(message.pageUrl, null);
    soft.deepEqual(message.content, {
      type: "text",
      body: { random: "system call specific keys" },
    });
  });
});

add_task(function test_ChatConversation_getSitesList() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("https://www.firefox.com"));
  conversation.addUserMessage(content, new URL("https://www.cnn.com"));
  conversation.addUserMessage(content, new URL("https://www.espn.com"));
  conversation.addUserMessage(content, new URL("https://www.espn.com"));

  const sites = conversation.getSitesList();

  Assert.deepEqual(sites, [
    URL.parse("https://www.mozilla.com/"),
    URL.parse("https://www.firefox.com/"),
    URL.parse("https://www.cnn.com/"),
    URL.parse("https://www.espn.com/"),
  ]);
});

add_task(function test_ChatConversation_getMostRecentPageVisited() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("https://www.firefox.com"));
  conversation.addUserMessage(content, new URL("https://www.cnn.com"));
  conversation.addUserMessage(content, new URL("https://www.espn.com"));
  conversation.addUserMessage(content, new URL("https://www.espn.com"));

  const mostRecentPageVisited = conversation.getMostRecentPageVisited();

  Assert.equal(mostRecentPageVisited, "https://www.espn.com/");
});

add_task(function test_revisited_ChatConversation_getMostRecentPageVisited() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("https://www.firefox.com"));
  conversation.addUserMessage(content, new URL("https://www.mozilla.com"));
  conversation.addUserMessage(content, new URL("about:aiwindow"));

  const mostRecentPageVisited = conversation.getMostRecentPageVisited();
  Assert.equal(mostRecentPageVisited, "https://www.mozilla.com/");
});

add_task(function test_noBrowsing_ChatConversation_getMostRecentPageVisited() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";
  conversation.addUserMessage(content, new URL("about:aiwindow"));
  conversation.addUserMessage(content, null);
  conversation.addUserMessage(content, null);

  const mostRecentPageVisited = conversation.getMostRecentPageVisited();

  Assert.equal(mostRecentPageVisited, null);
});

add_task(function test_renderState_includes_tool_messages() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("get open tab", "about:aiwindow");
  conversation.addAssistantMessage("text", "Checking");
  conversation.addToolCallMessage({
    tool_call_id: "tc_1",
    body: [{ url: "https://example.com/", title: "Example" }],
    name: "get_open_tabs",
  });
  setTextSystemMessage(conversation, "some system message");
  conversation.addAssistantMessage("text", "You have one tab open.");

  const renderState = conversation.renderState();

  Assert.equal(renderState[0].role, MESSAGE_ROLE.USER);
  Assert.equal(renderState[1].role, MESSAGE_ROLE.ASSISTANT);
  Assert.equal(renderState[2].role, MESSAGE_ROLE.TOOL);
  Assert.equal(renderState[2].content.name, "get_open_tabs");
  Assert.equal(renderState[3].role, MESSAGE_ROLE.ASSISTANT);
});

add_task(function test_ChatConversation_currentTurnIndex() {
  const conversation = new ChatConversation({});

  const content = "user to assistant msg";

  setTextSystemMessage(conversation, "the system prompt");
  conversation.addUserMessage(content, "about:aiwindow");
  conversation.addAssistantMessage("text", "a response");
  conversation.addUserMessage(content, "about:aiwindow");
  conversation.addAssistantMessage("text", "a response");
  conversation.addUserMessage(content, "about:aiwindow");
  conversation.addAssistantMessage("text", "a response");
  conversation.addUserMessage(content, "about:aiwindow");
  conversation.addAssistantMessage("text", "a response");
  conversation.addUserMessage(content, "about:aiwindow");
  conversation.addAssistantMessage("text", "a response");

  Assert.deepEqual(conversation.currentTurnIndex(), 4);
});

add_task(function test_ChatConversation_helpersTurnIndexing() {
  const conversation = new ChatConversation({});

  setTextSystemMessage(conversation, "the system prompt");
  conversation.addUserMessage("a user's prompt", "https://www.somesite.com");
  conversation.addToolCallMessage({ some: "tool call details" });
  conversation.addAssistantMessage("text", "the llm response");
  conversation.addUserMessage(
    "a user's second prompt",
    "https://www.somesite.com"
  );
  conversation.addToolCallMessage({ some: "more tool call details" });
  conversation.addAssistantMessage("text", "the second llm response");

  Assert.withSoftAssertions(function (soft) {
    soft.equal(conversation.messages.length, 7);

    soft.equal(conversation.messages[0].turnIndex, 0);
    soft.equal(conversation.messages[1].turnIndex, 0);
    soft.equal(conversation.messages[2].turnIndex, 0);
    soft.equal(conversation.messages[3].turnIndex, 0);
    soft.equal(conversation.messages[4].turnIndex, 1);
    soft.equal(conversation.messages[5].turnIndex, 1);
    soft.equal(conversation.messages[6].turnIndex, 1);
  });
});

add_task(function test_ChatConversation_getMessagesInChatCompletionsFormat() {
  const conversation = new ChatConversation({});
  setTextSystemMessage(conversation, "the system prompt");
  conversation.addUserMessage(
    "a user's prompt",
    "https://www.somesite.com",
    new UserRoleOpts(),
    { testContext: "321" }
  );
  conversation.addToolCallMessage({
    tool_call_id: "123",
    name: "tool_1",
    body: [1, 2, 3],
  });
  conversation.addAssistantMessage("text", "the llm response");
  conversation.addUserMessage(
    "a user's second prompt",
    "some question",
    new UserRoleOpts(),
    { testContext: "654" }
  );
  conversation.addToolCallMessage({
    tool_call_id: "456",
    name: "tool_1",
    body: [4, 5, 6],
  });
  conversation.addAssistantMessage("text", "the second llm response");

  const openAiFormat = conversation.getMessagesInChatCompletionsFormat();

  Assert.deepEqual(openAiFormat, [
    { role: "system", content: "the system prompt" },
    { role: "user", content: "321" },
    { role: "user", content: "a user's prompt" },
    { role: "tool", content: "[1,2,3]", name: "tool_1", tool_call_id: "123" },
    { role: "assistant", content: "the llm response" },
    { role: "user", content: "654" },
    { role: "user", content: "a user's second prompt" },
    { role: "tool", content: "[4,5,6]", name: "tool_1", tool_call_id: "456" },
    { role: "assistant", content: "the second llm response" },
  ]);
});

add_task(
  function test_getMessagesInChatCompletionsFormat_keeps_browser_context_in_history() {
    const conversation = new ChatConversation({});
    conversation.addUserMessage("turn 1", null, new UserRoleOpts(), {
      realTimeContext: "TAB: A",
      memoriesContext: "MEMORIES 1",
    });
    conversation.addAssistantMessage("text", "answer 1");
    conversation.addUserMessage("turn 2", null, new UserRoleOpts(), {});
    conversation.addAssistantMessage("text", "answer 2");
    conversation.addUserMessage("turn 3", null, new UserRoleOpts(), {
      realTimeContext: "TAB: B",
      memoriesContext: "MEMORIES 3",
    });

    Assert.deepEqual(
      conversation
        .getMessagesInChatCompletionsFormat()
        .map(({ role, content }) => ({ role, content })),
      [
        { role: "user", content: "TAB: A" },
        { role: "user", content: "turn 1" },
        { role: "assistant", content: "answer 1" },
        { role: "user", content: "turn 2" },
        { role: "assistant", content: "answer 2" },
        { role: "user", content: "TAB: B" },
        { role: "user", content: "MEMORIES 3" },
        { role: "user", content: "turn 3" },
      ],
      "Browser context stays before the message it was sent with. Memories only go with the latest message"
    );
  }
);

add_task(async function test_unrelatedMessage_ChatConversation_retryMessage() {
  const conversation = new ChatConversation({});
  setTextSystemMessage(conversation, "the system prompt");
  conversation.addUserMessage("a user's prompt", "https://www.somesite.com");

  const unrelatedMessage = new ChatMessage({
    ordinal: 0,
    role: MESSAGE_ROLE.USER,
    content: "some content",
    turnIndex: 0,
  });

  await Assert.rejects(
    conversation.retryMessage(unrelatedMessage),
    err =>
      /Unrelated message/.test(err.message) &&
      err.clientReason === "retryInvalidMessage",
    "retryMessage should reject with clientReason retryInvalidMessage"
  );
});

add_task(async function test_nonUserMessage_ChatConversation_retryMessage() {
  const conversation = new ChatConversation({});
  setTextSystemMessage(conversation, "the system prompt");
  conversation.addUserMessage("a user's prompt", "https://www.somesite.com");

  await Assert.rejects(
    conversation.retryMessage(conversation.messages[0]),
    err =>
      /Not a user message/.test(err.message) &&
      err.clientReason === "retryInvalidMessage",
    "retryMessage should reject with clientReason retryInvalidMessage"
  );
});

add_task(
  async function test_ChatConversation_retryMessage_returnsRemovedMessages() {
    let sandbox = lazy.sinon.createSandbox();

    const conversation = new ChatConversation({});

    sandbox.stub(conversation, "injectRealTimeContext").callsFake(() => {
      addLegacySystemMessage(
        conversation,
        SYSTEM_PROMPT_TYPE.REAL_TIME,
        "real time data"
      );
    });

    sandbox.stub(conversation, "injectMemoriesContext").callsFake(() => {
      addLegacySystemMessage(
        conversation,
        SYSTEM_PROMPT_TYPE.MEMORIES,
        "memories data"
      );
    });

    setTextSystemMessage(conversation, "the system prompt");
    conversation.addUserMessage("a user's prompt", "https://www.somesite.com");
    conversation.addToolCallMessage({ some: "tool call details" });
    conversation.addAssistantMessage("text", "the llm response");
    conversation.addUserMessage("a user's second prompt", "some question");
    conversation.addToolCallMessage({ some: "more tool call details" });
    conversation.addAssistantMessage("text", "the second llm response");

    const toDeleteMessages = await conversation.retryMessage(
      conversation.messages[1]
    );

    Assert.withSoftAssertions(function (soft) {
      soft.equal(toDeleteMessages.length, 6, "Incorrect number of messages");
      soft.equal(toDeleteMessages[0].content.body, "a user's prompt");
      soft.equal(toDeleteMessages[1].content.some, "tool call details");
      soft.equal(toDeleteMessages[2].content.body, "the llm response");
      soft.equal(toDeleteMessages[3].content.body, "a user's second prompt");
      soft.equal(toDeleteMessages[4].content.some, "more tool call details");
      soft.equal(toDeleteMessages[5].content.body, "the second llm response");
    });

    sandbox.restore();
  }
);

add_task(
  async function test_uniqueOrdinalsWithoutMemories_ChatConversation_retryMessage() {
    const conversation = new ChatConversation({});

    setTextSystemMessage(conversation, "the system prompt");
    addLegacySystemMessage(
      conversation,
      SYSTEM_PROMPT_TYPE.REAL_TIME,
      "real time data"
    );
    addLegacySystemMessage(
      conversation,
      SYSTEM_PROMPT_TYPE.MEMORIES,
      "memories data"
    );
    conversation.addUserMessage("a user's prompt", "https://www.somesite.com");
    conversation.addAssistantMessage("text", "the llm response");

    const retryTarget = conversation.messages.find(
      m => m.role === MESSAGE_ROLE.USER
    );
    const originalUserOrdinal = retryTarget.ordinal;
    await conversation.retryMessage(retryTarget);

    addLegacySystemMessage(
      conversation,
      SYSTEM_PROMPT_TYPE.REAL_TIME,
      "new real time data"
    );
    conversation.addUserMessage("a user's prompt", "https://www.somesite.com");
    conversation.addAssistantMessage("text", "the new llm response");

    const ordinals = conversation.messages.map(m => m.ordinal);
    const uniqueOrdinals = new Set(ordinals);

    Assert.withSoftAssertions(function (soft) {
      soft.equal(
        ordinals.length,
        uniqueOrdinals.size,
        "All ordinals should be unique after retry without memories"
      );
      soft.ok(
        conversation.messages
          .filter(m => m.role === MESSAGE_ROLE.ASSISTANT)
          .at(-1).ordinal > originalUserOrdinal,
        "New assistant ordinal must be greater than original user ordinal"
      );
    });
  }
);

add_task(
  async function test_injectRealTimeContext_writes_to_userMessage_userContext() {
    _setRemoteClientForTesting({
      get: () =>
        Promise.resolve([
          {
            id: "browser-context--tab--v1--generic",
            kind: "module",
            feature: "browser-context",
            module: "tab",
            model: "generic",
            version: "1.0",
            prompts: "TAB: {title} ({url})",
          },
        ]),
      on: () => {},
    });
    registerCleanupFunction(_clearRemoteClientForTesting);

    const conversation = new ChatConversation({});
    const userMessage = conversation.addUserMessage("hi");
    await conversation.injectRealTimeContext(userMessage, {
      getRealTimeMapping: async () => ({
        url: "https://example.com",
        title: "Example",
        hasTabInfo: true,
      }),
    });

    Assert.equal(
      userMessage.content.userContext.realTimeContext,
      "TAB: Example (https://example.com)",
      "injectRealTimeContext writes rendered prompt onto userMessage.content.userContext.realTimeContext"
    );
  }
);

add_task(
  async function test_injectRealTimeContext_no_op_without_user_message() {
    _setRemoteClientForTesting({
      get: () =>
        Promise.resolve([
          {
            id: "browser-context--tab--v1--generic",
            kind: "module",
            feature: "browser-context",
            module: "tab",
            model: "generic",
            version: "1.0",
            prompts: "TAB: {title} ({url})",
          },
        ]),
      on: () => {},
    });
    registerCleanupFunction(_clearRemoteClientForTesting);

    const conversation = new ChatConversation({});
    const opts = {
      getRealTimeMapping: async () => ({
        url: "https://example.com",
        title: "Example",
        hasTabInfo: true,
      }),
    };
    await conversation.injectRealTimeContext(undefined, opts);
    const userMessage = conversation.addUserMessage("hi");
    await conversation.injectRealTimeContext(userMessage, opts);

    Assert.equal(
      userMessage.content.userContext.realTimeContext,
      "TAB: Example (https://example.com)",
      "The next user message still gets the context"
    );
  }
);

add_task(async function test_injectRealTimeContext_no_op_when_mapping_null() {
  const mockGetRealTimeMapping = lazy.sinon.stub().resolves(null);

  const conversation = new ChatConversation({});
  const userMessage = conversation.addUserMessage("hi");
  await conversation.injectRealTimeContext(userMessage, {
    getRealTimeMapping: mockGetRealTimeMapping,
  });

  Assert.ok(
    !userMessage.content.userContext?.realTimeContext,
    "Leaves userContext.realTimeContext unset when mapping is null"
  );
});

add_task(
  async function test_injectRealTimeContext_marks_missing_context_after_reopen() {
    const conversation = new ChatConversation({});
    const earlierMessage = conversation.addUserMessage("turn 1");
    earlierMessage.content.userContext.realTimeContext =
      "TAB: Example (https://example.com)";
    conversation.addAssistantMessage("text", "answer");

    const userMessage = conversation.addUserMessage("turn 2");
    await conversation.injectRealTimeContext(userMessage, {
      getRealTimeMapping: async () => null,
    });

    Assert.equal(
      userMessage.content.userContext.realTimeContext,
      "No page context is available for this message.",
      "Model is told the stored tab no longer applies"
    );
  }
);

add_task(async function test_injectRealTimeContext_dedupes_when_unchanged() {
  _setRemoteClientForTesting({
    get: () =>
      Promise.resolve([
        {
          id: "browser-context--tab--v1--generic",
          kind: "module",
          feature: "browser-context",
          module: "tab",
          model: "generic",
          version: "1.0",
          prompts: "TAB: {title} ({url})",
        },
      ]),
    on: () => {},
  });
  registerCleanupFunction(_clearRemoteClientForTesting);

  const conversation = new ChatConversation({});
  const sameMapping = async () => ({
    url: "https://example.com",
    title: "Example",
    hasTabInfo: true,
  });

  const firstMessage = conversation.addUserMessage("turn 1");
  await conversation.injectRealTimeContext(firstMessage, {
    getRealTimeMapping: sameMapping,
  });
  Assert.equal(
    firstMessage.content.userContext.realTimeContext,
    "TAB: Example (https://example.com)",
    "First turn writes context"
  );

  const secondMessage = conversation.addUserMessage("turn 2");
  await conversation.injectRealTimeContext(secondMessage, {
    getRealTimeMapping: sameMapping,
  });
  Assert.ok(
    !secondMessage.content.userContext?.realTimeContext,
    "Second turn skips writing when context is identical to last-injected"
  );

  const thirdMessage = conversation.addUserMessage("turn 3");
  await conversation.injectRealTimeContext(thirdMessage, {
    getRealTimeMapping: async () => ({
      url: "https://other.example",
      title: "Other",
      hasTabInfo: true,
    }),
  });
  Assert.equal(
    thirdMessage.content.userContext.realTimeContext,
    "TAB: Other (https://other.example)",
    "Third turn writes when context changes"
  );
});

add_task(
  async function test_injectRealTimeContext_reinjects_after_retry_or_missing_context() {
    _setRemoteClientForTesting({
      get: () =>
        Promise.resolve([
          {
            id: "browser-context--tab--v1--generic",
            kind: "module",
            feature: "browser-context",
            module: "tab",
            model: "generic",
            version: "1.0",
            prompts: "TAB: {title} ({url})",
          },
        ]),
      on: () => {},
    });
    registerCleanupFunction(_clearRemoteClientForTesting);

    const conversation = new ChatConversation({});
    const tab = async () => ({
      url: "https://example.com",
      title: "Example",
      hasTabInfo: true,
    });
    const sendTurn = async (prompt, getRealTimeMapping) => {
      const userMessage = conversation.addUserMessage(prompt);
      await conversation.injectRealTimeContext(userMessage, {
        getRealTimeMapping,
      });
      conversation.addAssistantMessage("text", "answer");
      return userMessage;
    };
    const contextOf = message => message.content.userContext?.realTimeContext;
    const context = "TAB: Example (https://example.com)";

    const firstMessage = await sendTurn("turn 1", tab);
    await conversation.retryMessage(firstMessage);
    Assert.equal(
      contextOf(await sendTurn("turn 1", tab)),
      context,
      "Retried message receives the context again"
    );

    Assert.equal(
      contextOf(await sendTurn("turn 2", async () => null)),
      "No page context is available for this message.",
      "Model is told the earlier tab no longer applies"
    );
    Assert.ok(
      !contextOf(await sendTurn("turn 2b", async () => null)),
      "The note is not repeated while there is still no page context"
    );
    Assert.equal(
      contextOf(await sendTurn("turn 3", tab)),
      context,
      "Context is resubmitted after the tab was left out"
    );
  }
);

add_task(function test_ChatConversation_renderState_filters_phantom_messages() {
  const conversation = new ChatConversation({});

  conversation.addUserMessage("What's the weather?", "about:aiwindow");
  conversation.addAssistantMessage("text", "");
  conversation.addAssistantMessage("function", {
    tool_calls: [
      {
        id: "call_1",
        function: {
          name: "run_search",
          arguments: '{"query":"weather"}',
        },
      },
    ],
  });
  conversation.addAssistantMessage("text", "Here is the weather forecast.");

  const renderState = conversation.renderState();

  Assert.equal(
    renderState.length,
    2,
    "Should only contain user message and real assistant message"
  );
  Assert.equal(renderState[0].role, MESSAGE_ROLE.USER);
  Assert.equal(renderState[1].role, MESSAGE_ROLE.ASSISTANT);
  Assert.equal(renderState[1].content.body, "Here is the weather forecast.");
});

add_task(function test_renderState_includes_empty_message_with_tool_ui() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Create a page", null);
  const message = conversation.addAssistantMessage("text", "");
  conversation.addUIToolToCurrentMessage(
    "aitab-call",
    { uiType: "aitab", properties: { state: STATE_CHOOSE } },
    { emitComplete: false }
  );

  Assert.ok(conversation.renderState().includes(message));
});

function addToolRound(conversation, id) {
  conversation.addAssistantMessage("function", {
    tool_calls: [
      { id, type: "function", function: { name: "tool_1", arguments: "{}" } },
    ],
  });
  conversation.addToolCallMessage({
    tool_call_id: id,
    name: "tool_1",
    body: [],
  });
}

async function* toolCallStream(text) {
  if (text) {
    yield { text };
  }
  yield {
    toolCalls: [
      { id: "call_1", function: { name: "tool_1", arguments: "{}" } },
    ],
  };
}

add_task(
  async function test_receiveResponse_streams_reply_after_tool_results_into_new_message() {
    const conversation = new ChatConversation({});
    const completed = [];
    conversation.on("chat-conversation:message-complete", (_event, message) =>
      completed.push(message)
    );
    conversation.addUserMessage("Find my recipe page");
    const preamble = conversation.addAssistantMessage("text", "");

    await conversation.receiveResponse(toolCallStream("Let me look."));
    addToolRound(conversation, "call_1");
    conversation.addCitations([{ url: "https://example.com/recipe" }]);
    await conversation.receiveResponse(
      (async function* () {
        yield { text: "Found it." };
      })()
    );

    const reply = conversation.messages.at(-1);
    Assert.deepEqual(
      conversation.messages.map(({ role, content }) => [role, content.type]),
      [
        [MESSAGE_ROLE.USER, "text"],
        [MESSAGE_ROLE.ASSISTANT, "text"],
        [MESSAGE_ROLE.ASSISTANT, "function"],
        [MESSAGE_ROLE.TOOL, undefined],
        [MESSAGE_ROLE.ASSISTANT, "text"],
      ],
      "The reply is stored after the tool results"
    );
    Assert.equal(preamble.content.body, "Let me look.", "The preamble is kept");
    Assert.equal(
      reply.content.body,
      "Found it.",
      "The reply has its own message"
    );
    Assert.deepEqual(
      reply.citations.map(({ url }) => url),
      ["https://example.com/recipe"],
      "The reply carries the turn's citations"
    );
    Assert.deepEqual(
      completed,
      [reply],
      "Only the reply completes, once the turn is done"
    );
  }
);

add_task(async function test_receiveResponse_empty_reply_completes_preamble() {
  const conversation = new ChatConversation({});
  const completed = [];
  conversation.on("chat-conversation:message-complete", (_event, message) =>
    completed.push(message)
  );
  conversation.addUserMessage("Find my recipe page");
  const preamble = conversation.addAssistantMessage("text", "");

  await conversation.receiveResponse(toolCallStream("Let me look."));
  addToolRound(conversation, "call_1");
  conversation.addCitations([{ url: "https://example.com/recipe" }]);
  await conversation.receiveResponse((async function* () {})());

  Assert.deepEqual(
    completed,
    [preamble],
    "The preamble completes the turn when the reply after the tools is empty"
  );
  Assert.deepEqual(
    preamble.citations.map(({ url }) => url),
    ["https://example.com/recipe"],
    "The preamble carries the citations found by the turn's tools"
  );
});

add_task(
  async function test_deduplicatesMemoryIds_ChatConversation_receiveResponse() {
    let sandbox = lazy.sinon.createSandbox();

    const mockMemories = [
      { id: "mem-1", lifetime_accessed_count: 0, recent_accessed_counts: {} },
      { id: "mem-2", lifetime_accessed_count: 0, recent_accessed_counts: {} },
    ];
    sandbox.stub(MemoryStore, "getMemories").resolves(mockMemories);
    sandbox.stub(MemoryStore, "requestSave").resolves();

    const conversation = new ChatConversation({});
    conversation.addAssistantMessage("text", "some response");
    const assistantMsg = conversation.messages.at(-1);
    assistantMsg.tokens.existing_memory = ["mem-1", "mem-1", "mem-2", "mem-2"];

    async function* emptyStream() {}
    await conversation.receiveResponse(emptyStream());

    Assert.ok(
      MemoryStore.getMemories.calledOnce,
      "MemoryStore.getMemories should be called exactly once"
    );
    Assert.ok(
      MemoryStore.requestSave.calledOnce,
      "The recorded use should be saved exactly once for the message"
    );
    for (const memory of mockMemories) {
      Assert.equal(
        memory.lifetime_accessed_count,
        1,
        `Use should be recorded once per distinct memory (${memory.id})`
      );
    }
    const memoryIds = new Set(
      assistantMsg.memoriesApplied.map(memory => memory.id)
    );
    Assert.equal(
      memoryIds.size,
      2,
      "memoryIds should be a deduplicated Set of size 2"
    );
    Assert.ok(memoryIds.has("mem-1"), "memoryIds should contain mem-1");
    Assert.ok(memoryIds.has("mem-2"), "memoryIds should contain mem-2");
    Assert.deepEqual(
      assistantMsg.memoriesApplied,
      mockMemories,
      "memoriesApplied should be set to the resolved memories"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_resolvesMemoriesOncePerTurn_ChatConversation_receiveResponse() {
    let sandbox = lazy.sinon.createSandbox();

    const mockMemories = [
      { id: "mem-1", lifetime_accessed_count: 0, recent_accessed_counts: {} },
    ];
    sandbox.stub(MemoryStore, "getMemories").resolves(mockMemories);
    sandbox.stub(MemoryStore, "requestSave").resolves();

    const conversation = new ChatConversation({});
    conversation.addUserMessage("What do you remember?");
    conversation.addAssistantMessage("text", "");
    const assistantMsg = conversation.messages.at(-1);

    // First pass: The model cites a memory and requests a tool call.
    const withToolCalls = await conversation.receiveResponse(
      toolCallStream("Let me check that.§existing_memory: mem-1§")
    );

    Assert.equal(
      withToolCalls.pendingToolCalls.length,
      1,
      "The first pass should report the pending tool call"
    );
    Assert.ok(
      MemoryStore.getMemories.notCalled,
      "Use should not be recorded while the turn is still running"
    );
    Assert.deepEqual(
      assistantMsg.memoriesApplied,
      ["mem-1"],
      "memoriesApplied should still hold the raw cited id mid-turn"
    );

    addToolRound(conversation, "call_1");

    // Second pass: The same memory is cited again once the tool result is in.
    async function* finalStream() {
      yield { text: "Here is what I found.§existing_memory: mem-1§" };
    }
    const final = await conversation.receiveResponse(finalStream());
    const replyMsg = conversation.messages.at(-1);

    Assert.equal(
      final.pendingToolCalls,
      null,
      "The final pass should not report any pending tool call"
    );
    Assert.ok(
      MemoryStore.getMemories.calledOnce,
      "MemoryStore.getMemories should be called exactly once for the turn"
    );
    Assert.ok(
      MemoryStore.requestSave.calledOnce,
      "The recorded use should be saved exactly once for the turn"
    );
    Assert.equal(
      mockMemories[0].lifetime_accessed_count,
      1,
      "Use should be recorded once even though the memory was cited twice"
    );
    Assert.deepEqual(
      replyMsg.memoriesApplied,
      mockMemories,
      "The reply should end up with the resolved memories"
    );
    Assert.deepEqual(
      assistantMsg.memoriesApplied,
      [],
      "The preamble should not keep the raw cited ids"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_promptEmbeddedMemories_ChatConversation_receiveResponse() {
    let sandbox = lazy.sinon.createSandbox();

    const mockMemories = [
      { id: "mem-1", lifetime_accessed_count: 0, recent_accessed_counts: {} },
    ];
    sandbox.stub(MemoryStore, "getMemories").resolves(mockMemories);
    sandbox.stub(MemoryStore, "requestSave").resolves();

    const conversation = new ChatConversation({});
    conversation.promptEmbeddedMemories = [
      { id: "mem-1", memory_summary: "Trip planning" },
    ];
    conversation.addAssistantMessage("text", "some response");
    const assistantMsg = conversation.messages.at(-1);

    async function* emptyStream() {}
    await conversation.receiveResponse(emptyStream());

    Assert.deepEqual(
      assistantMsg.memoriesApplied,
      mockMemories,
      "memoriesApplied should include the prompt-embedded memory, resolved through resolveUsedMemories, when nothing is cited"
    );
    Assert.equal(
      mockMemories[0].lifetime_accessed_count,
      1,
      "Prompt-embedded memory use should be recorded like a cited one"
    );
    Assert.deepEqual(
      conversation.promptEmbeddedMemories,
      [],
      "promptEmbeddedMemories should be consumed after the turn completes"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_citedAndPromptEmbeddedMemoriesAreUnioned_ChatConversation_receiveResponse() {
    let sandbox = lazy.sinon.createSandbox();

    const mockMemories = [
      { id: "mem-1", lifetime_accessed_count: 0, recent_accessed_counts: {} },
      {
        id: "mem-embedded",
        lifetime_accessed_count: 0,
        recent_accessed_counts: {},
      },
    ];
    sandbox.stub(MemoryStore, "getMemories").resolves(mockMemories);
    sandbox.stub(MemoryStore, "requestSave").resolves();

    const conversation = new ChatConversation({});
    conversation.promptEmbeddedMemories = [
      { id: "mem-embedded", memory_summary: "Trip planning" },
    ];
    conversation.addAssistantMessage("text", "some response");
    const assistantMsg = conversation.messages.at(-1);
    assistantMsg.tokens.existing_memory = ["mem-1"];

    async function* emptyStream() {}
    await conversation.receiveResponse(emptyStream());

    const memoryIds = new Set(
      assistantMsg.memoriesApplied.map(memory => memory.id)
    );
    Assert.equal(
      memoryIds.size,
      2,
      "memoriesApplied should include both the cited and prompt-embedded memories"
    );
    Assert.ok(memoryIds.has("mem-1"), "Should include the cited memory");
    Assert.ok(
      memoryIds.has("mem-embedded"),
      "Should include the prompt-embedded memory"
    );
    Assert.deepEqual(
      conversation.promptEmbeddedMemories,
      [],
      "promptEmbeddedMemories should be cleared after the turn completes"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_citedAndPromptEmbeddedSameMemoryIsDeduped_ChatConversation_receiveResponse() {
    let sandbox = lazy.sinon.createSandbox();

    const mockMemories = [
      { id: "mem-1", lifetime_accessed_count: 0, recent_accessed_counts: {} },
    ];
    sandbox.stub(MemoryStore, "getMemories").resolves(mockMemories);
    sandbox.stub(MemoryStore, "requestSave").resolves();

    const conversation = new ChatConversation({});
    conversation.promptEmbeddedMemories = [
      { id: "mem-1", memory_summary: "Trip planning" },
    ];
    conversation.addAssistantMessage("text", "some response");
    const assistantMsg = conversation.messages.at(-1);
    assistantMsg.tokens.existing_memory = ["mem-1"];

    async function* emptyStream() {}
    await conversation.receiveResponse(emptyStream());

    Assert.equal(
      assistantMsg.memoriesApplied.length,
      1,
      "The same memory cited and prompt-embedded should only appear once"
    );
    Assert.equal(
      mockMemories[0].lifetime_accessed_count,
      1,
      "Use should be recorded once, not twice, for the same memory"
    );

    sandbox.restore();
  }
);

add_task(function test_ChatConversation_rehydratesHistoryResultsPool() {
  const records = [
    { url: "https://example.com/1", title: "Page 1" },
    { url: "https://example.com/2", title: "Page 2" },
  ];
  const message = new ChatMessage({
    ordinal: 1,
    role: MESSAGE_ROLE.ASSISTANT,
    turnIndex: 0,
    content: { type: "text", body: "Here is what I found" },
    historyResults: records,
  });

  const conversation = new ChatConversation({ messages: [message] });

  Assert.deepEqual(
    conversation.getHistoryResultsSnapshot(),
    records,
    "constructor rehydrates the history results pool from message snapshots"
  );
});

add_task(
  async function test_receiveResponse_snapshotsHistoryResultsOnMessage() {
    const conversation = new ChatConversation({});
    conversation.addAssistantMessage("text", "");
    const assistantMsg = conversation.messages.at(-1);

    const records = [{ url: "https://example.com/1", title: "Page 1" }];
    conversation.addHistoryResults(records);

    async function* emptyStream() {}
    await conversation.receiveResponse(emptyStream());

    Assert.deepEqual(
      assistantMsg.historyResults,
      conversation.getHistoryResultsSnapshot(),
      "receiveResponse snapshots the history results pool onto the completed message"
    );
    Assert.equal(
      assistantMsg.historyResults.length,
      1,
      "the snapshot contains the searched record"
    );
  }
);

add_task(function test_ChatConversation_rehydratesCitationsPool() {
  const records = [
    { url: "https://example.com/1", title: "Source 1" },
    { url: "https://example.com/2", title: "Source 2" },
  ];
  const message = new ChatMessage({
    ordinal: 1,
    role: MESSAGE_ROLE.ASSISTANT,
    turnIndex: 0,
    content: { type: "text", body: "Here is what I found" },
    citations: records,
  });

  const conversation = new ChatConversation({ messages: [message] });

  // The snapshot only covers URLs read in the current turn.
  Assert.deepEqual(
    conversation.getCitationsSnapshot(),
    [],
    "a restored conversation starts with no pending citations"
  );

  conversation.addCitations([{ url: "https://example.com/2" }]);
  Assert.deepEqual(
    conversation.getCitationsSnapshot(),
    [{ url: "https://example.com/2", title: "Source 2" }],
    "constructor rehydrates the citations pool from message snapshots"
  );
});

add_task(async function test_receiveResponse_snapshotsCitationsOnMessage() {
  const conversation = new ChatConversation({});
  conversation.addAssistantMessage("text", "");
  const assistantMsg = conversation.messages.at(-1);

  const records = [{ url: "https://example.com/1", title: "Source 1" }];
  conversation.addCitations(records);

  async function* emptyStream() {}
  await conversation.receiveResponse(emptyStream());

  Assert.deepEqual(
    assistantMsg.citations,
    records,
    "receiveResponse snapshots this turn's citations onto the completed message"
  );
});

add_task(function test_applyHistoryAssets_keepsThumbnailForCitedUrl() {
  const url = "https://example.com/1";
  const conversation = new ChatConversation({});
  conversation.addHistoryResults([{ url, title: "Page 1" }]);
  conversation.applyHistoryAssets([
    {
      url,
      image: "moz-page-thumb://thumb",
      requestedThumbnail: true,
      hasFavicon: false,
    },
  ]);
  conversation.addCitations([{ url, title: "Source 1" }]);
  // A citation request sends no thumbnail
  conversation.applyHistoryAssets([
    { url, image: null, requestedThumbnail: false, hasFavicon: true },
  ]);

  Assert.equal(
    conversation.getHistoryResultsSnapshot()[0].image,
    "moz-page-thumb://thumb",
    "citation-only asset resolution keeps the history record’s thumbnail"
  );
  Assert.ok(
    conversation.getHistoryResultsSnapshot()[0].hasFavicon,
    "history record picks up the resolved favicon availability"
  );
  Assert.ok(
    conversation.getCitationsSnapshot()[0].hasFavicon,
    "citation picks up the resolved favicon availability"
  );

  conversation.applyHistoryAssets([
    { url, image: null, requestedThumbnail: true, hasFavicon: true },
  ]);
  Assert.equal(
    conversation.getHistoryResultsSnapshot()[0].image,
    null,
    "an evicted thumbnail still clears when a thumbnail was requested"
  );
});

add_task(async function test_addUserMessage_clearsPendingCitations() {
  const conversation = new ChatConversation({});
  conversation.addCitations([{ url: "https://example.com/1", title: "S1" }]);

  conversation.addUserMessage("A follow-up question", "https://example.com/");
  conversation.addAssistantMessage("text", "");
  const assistantMsg = conversation.messages.at(-1);

  async function* emptyStream() {}
  await conversation.receiveResponse(emptyStream());

  Assert.deepEqual(
    assistantMsg.citations,
    [],
    "addUserMessage clears pending citations so chips belong to their own reply"
  );
});

add_task(async function test_retryMessage_clearsPendingCitations() {
  const conversation = new ChatConversation({});
  const userMsg = conversation.addUserMessage(
    "Search the web",
    "https://example.com/"
  );
  conversation.addAssistantMessage("text", "Here is what I found");
  conversation.addCitations([{ url: "https://example.com/1", title: "S1" }]);

  await conversation.retryMessage(userMsg);

  Assert.deepEqual(
    conversation.getCitationsSnapshot(),
    [],
    "retryMessage clears the pending citations from the previous turn"
  );

  conversation.addAssistantMessage("text", "");
  const assistantMsg = conversation.messages.at(-1);
  async function* emptyStream() {}
  await conversation.receiveResponse(emptyStream());

  Assert.deepEqual(
    assistantMsg.citations,
    [],
    "the regenerated reply does not inherit citations from the previous turn"
  );
});

add_task(async function test_addUserMessage_sets_memories_fields() {
  const conversation = new ChatConversation({});

  const userOpts = new UserRoleOpts({
    memoriesEnabled: false,
    memoriesFlagSource: MEMORIES_FLAG_SOURCE.CONVERSATION,
  });

  await conversation.addUserMessage("hello", null, userOpts);

  const lastUserMessage = conversation.messages
    .filter(m => m.role === MESSAGE_ROLE.USER)
    .at(-1);

  Assert.ok(lastUserMessage, "Last user message exists");
  Assert.equal(
    lastUserMessage.memoriesEnabled,
    false,
    "memoriesEnabled is persisted on the user message"
  );
  Assert.equal(
    lastUserMessage.memoriesFlagSource,
    MEMORIES_FLAG_SOURCE.CONVERSATION,
    "memoriesFlagSource is persisted on the user message"
  );
});

add_task(async function test_generatePrompt_emitsUserMessage() {
  const sandbox = lazy.sinon.createSandbox();
  const conversation = new ChatConversation({});
  _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));
  sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
  sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

  let emittedMessage = null;
  conversation.on("chat-conversation:message-update", (_, msg) => {
    emittedMessage = msg;
  });

  await conversation.generatePrompt("hello", null);

  Assert.ok(emittedMessage, "event should have been emitted");
  Assert.equal(emittedMessage.content.body, "hello");
  Assert.equal(emittedMessage.role, MESSAGE_ROLE.USER);
  _setLoadPromptForTesting(null);
  sandbox.restore();
});

add_task(async function test_generatePrompt_skipUserDispatch() {
  const sandbox = lazy.sinon.createSandbox();
  const conversation = new ChatConversation({});
  _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));
  sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
  sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

  let emitted = false;
  conversation.on("chat-conversation:message-update", () => {
    emitted = true;
  });

  await conversation.generatePrompt("hello", null, undefined, true);

  Assert.ok(
    !emitted,
    "event should not be emitted when skipUserDispatch is true"
  );
  _setLoadPromptForTesting(null);
  sandbox.restore();
});

add_task(async function test_generatePrompt_memoriesContextErrorDoesNotThrow() {
  let sandbox = lazy.sinon.createSandbox();

  // Seed a memory so getRelevantMemories reaches the embeddings path
  await MemoryStore.addMemory({
    id: "memory-embed-fail",
    memory_summary: "User likes hiking",
    category: "preference",
    intent: "profile",
    reasoning: "Test memory",
    score: 0.5,
    updated_at: Date.now(),
    is_deleted: false,
  });
  MemoriesManager._clearEmbeddingsCache();

  sandbox
    .stub(EmbeddingsGenerator.prototype, "embedMany")
    .rejects(new Error("Failed to download embedding model"));

  const conversation = new ChatConversation({});
  _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));
  sandbox.stub(conversation, "injectRealTimeContext").callsFake(async msg => {
    msg.content.userContext ??= {};
    msg.content.userContext.realTimeContext = "real time context";
  });

  const result = await conversation.generatePrompt("hello", null, {
    memoriesEnabled: true,
  });

  Assert.ok(result, "generatePrompt should resolve successfully");

  const userMessage = conversation.messages.find(
    m => m.role === MESSAGE_ROLE.USER
  );

  Assert.ok(
    EmbeddingsGenerator.prototype.embedMany.calledOnce,
    "embedMany should have been called exactly once"
  );
  Assert.equal(
    userMessage.content.userContext.realTimeContext,
    "real time context",
    "realTimeContext should still be set despite embeddings failure"
  );
  Assert.ok(
    !("memoriesContext" in userMessage.content.userContext),
    "memoriesContext should not be set when embedMany rejects"
  );

  await MemoryStore.hardDeleteMemory("memory-embed-fail", "other");
  MemoriesManager._clearEmbeddingsCache();
  _setLoadPromptForTesting(null);
  sandbox.restore();
});

add_task(
  async function test_generatePrompt_userContextPopulatedBeforeResolving() {
    const sandbox = lazy.sinon.createSandbox();
    const conversation = new ChatConversation({});
    _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));
    sandbox.stub(conversation, "injectRealTimeContext").callsFake(async msg => {
      msg.content.userContext ??= {};
      msg.content.userContext.realTimeContext = "real time context";
    });
    sandbox.stub(conversation, "injectMemoriesContext").callsFake(async msg => {
      msg.content.userContext ??= {};
      msg.content.userContext.memoriesContext = "memories context";
    });

    await conversation.generatePrompt("hello", null, {
      memoriesEnabled: true,
    });

    const userMessage = conversation.messages.find(
      m => m.role === MESSAGE_ROLE.USER
    );

    Assert.withSoftAssertions(function (soft) {
      soft.equal(
        userMessage.content.userContext.realTimeContext,
        "real time context",
        "realTimeContext should be set on userContext before generatePrompt resolves"
      );
      soft.equal(
        userMessage.content.userContext.memoriesContext,
        "memories context",
        "memoriesContext should be set on userContext before generatePrompt resolves"
      );
    });
    _setLoadPromptForTesting(null);
    sandbox.restore();
  }
);

add_task(
  async function test_injectRealTimeContext_setsPrivateData_when_hasTabInfo() {
    const conversation = new ChatConversation({});
    const userMessage = conversation.addUserMessage("hi");
    const mockGetRealTimeMapping = lazy.sinon.stub().resolves({
      todayDate: "2024-01-15",
      url: "https://example.com",
      title: "Example Page",
      hasTabInfo: true,
      locale: "en-US",
      timezone: "America/Los_Angeles",
      isoTimestamp: "2024-01-15T10:30:00",
    });
    _setRemoteClientForTesting({
      get: lazy.sinon.stub().resolves([
        {
          id: "browser-context--tab--v1--generic",
          kind: "module",
          feature: "browser-context",
          module: "tab",
          model: "generic",
          version: "1.0",
          prompts: "TAB {todayDate}",
        },
      ]),
    });

    await conversation.injectRealTimeContext(userMessage, {
      getRealTimeMapping: mockGetRealTimeMapping,
    });

    conversation.securityProperties.commit();
    Assert.ok(
      conversation.securityProperties.privateData,
      "privateData should be true after commit when hasTabInfo is true"
    );
  }
);

add_task(
  async function test_injectRealTimeContext_doesNotSetPrivateData_when_noTabInfo() {
    const conversation = new ChatConversation({});
    const userMessage = conversation.addUserMessage("hi");
    const mockGetRealTimeMapping = lazy.sinon.stub().resolves({
      todayDate: "2024-01-15",
      hasTabInfo: false,
      locale: "en-US",
      timezone: "America/Los_Angeles",
      isoTimestamp: "2024-01-15T10:30:00",
    });
    _setRemoteClientForTesting({
      get: lazy.sinon.stub().resolves([]),
    });

    await conversation.injectRealTimeContext(userMessage, {
      getRealTimeMapping: mockGetRealTimeMapping,
    });

    conversation.securityProperties.commit();
    Assert.ok(
      !conversation.securityProperties.privateData,
      "privateData should remain false when hasTabInfo is false"
    );
  }
);

add_task(
  async function test_generatePrompt_commitsPrivateData_when_hasTabInfo() {
    const conversation = new ChatConversation({});
    const sandbox = lazy.sinon.createSandbox();
    _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));

    sandbox.stub(conversation, "injectRealTimeContext").callsFake(async () => {
      conversation.securityProperties.setPrivateData();
    });
    sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

    await conversation.generatePrompt("hello", null);

    Assert.ok(
      conversation.securityProperties.privateData,
      "privateData should be committed true when injectRealTimeContext stages it"
    );
    _setLoadPromptForTesting(null);
    sandbox.restore();
  }
);

add_task(
  async function test_generatePrompt_commitsPrivateData_when_memoriesEnabled() {
    const conversation = new ChatConversation({});
    const sandbox = lazy.sinon.createSandbox();
    _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));

    sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
    sandbox.stub(conversation, "injectMemoriesContext").callsFake(async () => {
      conversation.securityProperties.setPrivateData();
    });

    await conversation.generatePrompt("hello", null, {
      memoriesEnabled: true,
    });

    Assert.ok(
      conversation.securityProperties.privateData,
      "privateData should be committed true when injectMemoriesContext stages it"
    );
    _setLoadPromptForTesting(null);
    sandbox.restore();
  }
);

add_task(
  async function test_generatePrompt_doesNotSetPrivateData_when_noTabOrMemories() {
    const conversation = new ChatConversation({});
    const sandbox = lazy.sinon.createSandbox();
    _setLoadPromptForTesting(lazy.sinon.stub().resolves("system prompt"));

    sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
    sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

    await conversation.generatePrompt("hello", null);

    Assert.ok(
      !conversation.securityProperties.privateData,
      "privateData should remain false when no private data was staged"
    );
    _setLoadPromptForTesting(null);
    sandbox.restore();
  }
);

add_task(function test_securityProperties_plainObject_normalization() {
  const conversation = new ChatConversation({
    securityProperties: { untrustedInput: true },
  });

  Assert.withSoftAssertions(function (soft) {
    soft.ok(
      conversation.securityProperties instanceof SecurityProperties,
      "securityProperties should be a SecurityProperties instance"
    );
    soft.equal(
      conversation.securityProperties.untrustedInput,
      true,
      "untrustedInput should be true when explicitly set"
    );
    soft.equal(
      conversation.securityProperties.privateData,
      false,
      "privateData should default to false when missing from input"
    );
  });
});

add_task(async function test_convertUrlToToken_tokenGeneration() {
  const cases = [
    {
      message: "Works for a URL with a path.",
      url: "http://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message:
        "Returns a new number for a URL that is different but creates the same token.",
      url: "http://www.github.com/foo/bar/baz?ignored",
      expected: "GITHUB_COM_FOO_BAR_BAZ_2",
    },
    {
      message: "Returns the exact same token given another URL",
      url: "http://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message:
        "Returns a different token given the same URL with a different protocol",
      url: "https://www.github.com/foo/bar/baz",
      expected: "GITHUB_COM_FOO_BAR_BAZ_3",
    },
    {
      message: "Can handle about URLs.",
      url: "about:config",
      expected: "ABOUT_CONFIG_1",
    },
    {
      message: "Uses non-http protocols",
      url: "ftp://github.com/foo/bar/baz",
      expected: "FTP_GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message: "Uses invalid protocols",
      url: "asdf://github.com/foo/bar/baz",
      expected: "ASDF_GITHUB_COM_FOO_BAR_BAZ_1",
    },
    {
      message: "Ignores the port.",
      url: "http://github.com:1234/ignore/port",
      expected: "GITHUB_COM_IGNORE_PORT_1",
    },
    {
      message: "Ignores the params.",
      url: "http://www.github.com/ignore/params?token=xxx",
      expected: "GITHUB_COM_IGNORE_PARAMS_1",
    },
    {
      message: "Ignores the hash.",
      url: "http://www.github.com/ignore/hash/part?token=xxx#hash",
      expected: "GITHUB_COM_IGNORE_HASH_PART_1",
    },
    {
      message: "Truncates text in the host from 110 to 100.",
      url: `http://www.${"a".repeat(110)}.com/foo`,
      expected: "A".repeat(100) + "_1",
    },
    {
      message: "Skips text in the path that is too long",
      url: `http://github.com/skip/long/path/` + "A".repeat(100),
      expected: "GITHUB_COM_SKIP_LONG_PATH_1",
    },
  ];
  // Re-use the chat conversation.
  const conversation = new ChatConversation({});

  for (const { message, url, expected } of cases) {
    const token = conversation.convertUrlToToken(url);
    Assert.equal(token, expected, message);
  }
});

add_task(async function test_generatePrompt_systemPrompt_from_composer() {
  const sandbox = lazy.sinon.createSandbox();
  _setLoadPromptForTesting(
    lazy.sinon.stub().resolves("composed system prompt body")
  );
  const conversation = new ChatConversation({});
  sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
  sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

  await conversation.generatePrompt("hello", null);

  _setLoadPromptForTesting(null);
  sandbox.restore();

  const systemMessage = conversation.messages.find(
    m => m.role === MESSAGE_ROLE.SYSTEM
  );
  Assert.equal(
    systemMessage.content.body,
    "composed system prompt body",
    "system message body is whatever loadPrompt returns"
  );
});

add_task(async function test_generatePrompt_persistsPromptVersion() {
  const sandbox = lazy.sinon.createSandbox();
  _setLoadPromptForTesting(async () => ({
    prompt: "system prompt with table instructions",
    version: "chat-v1",
  }));

  const conversation = new ChatConversation({});
  sandbox.stub(conversation, "injectRealTimeContext").resolves(null);
  sandbox.stub(conversation, "injectMemoriesContext").resolves(null);

  await conversation.generatePrompt("hello", null);

  const systemMessage = conversation.messages.find(
    m => m.role === MESSAGE_ROLE.SYSTEM
  );
  Assert.equal(
    systemMessage.content.version,
    "chat-v1",
    "version is stored on the system message content"
  );
  Assert.equal(
    conversation.systemPromptVersion,
    "chat-v1",
    "getter reads the version from the system message"
  );

  _setLoadPromptForTesting(null);
  sandbox.restore();
});

add_task(function test_systemPromptVersion_readsFromExistingSystemMessage() {
  const conversation = new ChatConversation({});
  setTextSystemMessage(conversation, "body", "chat-v2");

  Assert.equal(
    conversation.systemPromptVersion,
    "chat-v2",
    "getter returns the version from a pre-existing system message"
  );
});

add_task(function test_systemPromptVersion_emptyForLegacyMessage() {
  const conversation = new ChatConversation({});
  setTextSystemMessage(conversation, "body");
  Assert.equal(
    conversation.systemPromptVersion,
    "",
    "getter returns empty string for legacy system messages with no version"
  );
});

add_task(
  function test_addUIToolToCurrentMessage_attaches_to_existing_message() {
    const conversation = new ChatConversation({});

    // Add a user message and assistant message
    conversation.addUserMessage("Test prompt", null);
    conversation.addAssistantMessage("text", "Here's a response");

    const uiData = {
      uiType: "website-confirmation",
      title: "Test Title",
      description: "Test Description",
      properties: { tabs: [] },
    };

    const result = conversation.addUIToolToCurrentMessage(
      "tool-call-123",
      uiData
    );

    Assert.ok(result.success, "Should return success");
    Assert.equal(
      result.message,
      "Tool UI data added to existing assistant message",
      "Should indicate data was added"
    );

    const lastMessage = conversation.messages.at(-1);
    Assert.ok(lastMessage.toolUIData, "Message should have toolUIData");
    Assert.equal(
      lastMessage.toolUIData.toolCallId,
      "tool-call-123",
      "Tool call ID should match"
    );
    Assert.equal(
      lastMessage.toolUIData.uiType,
      "website-confirmation",
      "UI type should match"
    );
    Assert.equal(
      lastMessage.toolUIData.title,
      "Test Title",
      "Title should match"
    );
  }
);

add_task(function test_addUIToolToCurrentMessage_creates_synthetic_message() {
  const conversation = new ChatConversation({});

  // Only add a user message, no assistant message
  conversation.addUserMessage("Test prompt", null);

  const uiData = {
    uiType: "test-ui",
    title: "Test",
  };

  const result = conversation.addUIToolToCurrentMessage(
    "tool-call-456",
    uiData
  );

  Assert.ok(result.success, "Should return success");

  // Verify synthetic message was created
  const assistantMessages = conversation.messages.filter(
    m => m.role === MESSAGE_ROLE.ASSISTANT && m.content?.type === "text"
  );
  Assert.equal(
    assistantMessages.length,
    1,
    "Should have created one assistant message"
  );
  Assert.equal(
    assistantMessages[0].content.body,
    "",
    "Synthetic message should have empty body"
  );
  Assert.ok(
    assistantMessages[0].toolUIData,
    "Synthetic message should have toolUIData"
  );
});

add_task(function test_addUIToolToCurrentMessage_progressive_updates() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", null);
  conversation.addAssistantMessage("text", "Response");

  // First call
  const result1 = conversation.addUIToolToCurrentMessage("tool-call-789", {
    uiType: "test-ui",
    title: "Initial",
    properties: { value: 1 },
  });

  Assert.ok(result1.success, "First call should succeed");
  Assert.ok(!result1.isUpdate, "First call should not be an update");

  const message = conversation.messages.at(-1);
  Assert.equal(
    message.toolUIData.title,
    "Initial",
    "Initial title should be set"
  );
  Assert.equal(message.toolUIData.updateCount, 0, "Update count should be 0");

  // Update call with same toolCallId
  const result2 = conversation.addUIToolToCurrentMessage("tool-call-789", {
    title: "Updated",
    properties: { value: 2, extra: true },
  });

  Assert.ok(result2.success, "Update call should succeed");
  Assert.ok(result2.isUpdate, "Second call should be an update");
  Assert.equal(message.toolUIData.title, "Updated", "Title should be updated");
  Assert.equal(
    message.toolUIData.updateCount,
    1,
    "Update count should increment"
  );
  Assert.equal(
    message.toolUIData.properties.value,
    2,
    "Properties should be merged"
  );
  Assert.ok(
    message.toolUIData.properties.extra,
    "New properties should be added"
  );
});

add_task(
  function test_addUIToolToCurrentMessage_different_toolcallid_replaces() {
    const conversation = new ChatConversation({});
    conversation.addUserMessage("Test prompt", null);
    conversation.addAssistantMessage("text", "Response");

    // Add first UI tool
    conversation.addUIToolToCurrentMessage("tool-call-1", {
      uiType: "test-ui",
      title: "First",
    });

    let lastMessage = conversation.messages.at(-1);
    Assert.equal(
      lastMessage.toolUIData.title,
      "First",
      "First UI data should be present"
    );
    Assert.equal(
      lastMessage.toolUIData.toolCallId,
      "tool-call-1",
      "First tool call ID should be present"
    );

    // Add second UI tool with different ID - should replace
    conversation.addUIToolToCurrentMessage("tool-call-2", {
      uiType: "test-ui",
      title: "Second",
    });

    lastMessage = conversation.messages.at(-1);
    Assert.equal(
      lastMessage.toolUIData.title,
      "Second",
      "Second UI data should replace the first"
    );
    Assert.equal(
      lastMessage.toolUIData.toolCallId,
      "tool-call-2",
      "Second tool call ID should replace the first"
    );
  }
);

add_task(function test_addUIToolToCurrentMessage_emits_events() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Test prompt", null);
  conversation.addAssistantMessage("text", "Response");

  let updateEventFired = false;
  let completeEventFired = false;

  conversation.on("chat-conversation:message-update", () => {
    updateEventFired = true;
  });
  conversation.on("chat-conversation:message-complete", () => {
    completeEventFired = true;
  });

  conversation.addUIToolToCurrentMessage("tool-call-event", {
    uiType: "test-ui",
  });

  Assert.ok(updateEventFired, "Update event should be emitted");
  Assert.ok(completeEventFired, "Complete event should be re-emitted");
});

add_task(function test_addUIToolToCurrentMessage_can_defer_completion() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Create a page", null);
  conversation.addAssistantMessage("text", "");

  let updates = 0;
  let completions = 0;
  conversation.on("chat-conversation:message-update", () => updates++);
  conversation.on("chat-conversation:message-complete", () => completions++);

  conversation.addUIToolToCurrentMessage(
    "aitab-call",
    { uiType: "aitab", properties: { state: STATE_CHOOSE } },
    { emitComplete: false }
  );

  Assert.equal(updates, 1);
  Assert.equal(completions, 0);
});

add_task(async function test_addUserMessage_dismisses_prior_undo() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Close my tabs", "https://example.com/", 0);
  conversation.addAssistantMessage("text", "Closed");

  const assistant = conversation.messages.at(-1);
  assistant.toolUIData = {
    toolCallId: "t1",
    uiType: "ai-action-result",
    properties: { confirmedData: { operationIds: ["op-1"] } },
  };

  // User sends a new message
  conversation.addUserMessage("show my history", "https://example.com/", 0);

  Assert.deepEqual(
    assistant.toolUIData.properties.confirmedData.operationIds,
    ["op-1"],
    "Dismissal preserves other property keys"
  );
  Assert.strictEqual(
    assistant.toolUIData.properties.undoDismissed,
    true,
    "Prior ai-action-result with active operationId gets undoDismissed on follow-up"
  );
});

add_task(async function test_dismissPendingUndos_skips_without_operationId() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Close my tabs", "https://example.com/", 0);
  conversation.addAssistantMessage("text", "Closed");

  const assistant = conversation.messages.at(-1);
  assistant.toolUIData = {
    toolCallId: "t1",
    uiType: "ai-action-result",
    properties: { confirmedData: {} },
  };

  conversation.addUserMessage("show my history", "https://example.com/", 0);

  Assert.ok(
    !assistant.toolUIData.properties.undoDismissed,
    "ai-action-result without operationId is not dismissed"
  );
});

add_task(async function test_dismissPendingUndos_only_dismisses_most_recent() {
  const conversation = new ChatConversation({});

  conversation.addUserMessage("Close A", "https://example.com/", 0);
  conversation.addAssistantMessage("text", "Closed A");
  const olderAssistant = conversation.messages.at(-1);

  conversation.addAssistantMessage("text", "Closed B");
  const newerAssistant = conversation.messages.at(-1);

  // Set toolUIData on both before the next user message triggers undoDismissed
  olderAssistant.toolUIData = {
    toolCallId: "t1",
    uiType: "ai-action-result",
    properties: { confirmedData: { operationIds: ["op-older"] } },
  };
  newerAssistant.toolUIData = {
    toolCallId: "t2",
    uiType: "ai-action-result",
    properties: { confirmedData: { operationIds: ["op-newer"] } },
  };

  conversation.addUserMessage("show my history", "https://example.com/", 0);

  Assert.withSoftAssertions(function (soft) {
    soft.strictEqual(
      newerAssistant.toolUIData.properties.undoDismissed,
      true,
      "Most recent qualifying card is dismissed"
    );
    soft.ok(
      !olderAssistant.toolUIData.properties.undoDismissed,
      "Older qualifying card is left untouched"
    );
  });
});

add_task(function test_addToolCallMessage_emits_message_update() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Get opened tabs", null);

  let received = null;
  let calls = 0;
  conversation.on("chat-conversation:message-update", (_event, msg) => {
    if (msg?.role === MESSAGE_ROLE.TOOL) {
      received = msg;
      calls++;
    }
  });

  const toolMessage = conversation.addToolCallMessage({
    tool_call_id: "tc_abc",
    body: [{ url: "https://example.com/", title: "Example" }],
    name: "get_open_tabs",
  });

  Assert.withSoftAssertions(function (soft) {
    soft.equal(
      calls,
      1,
      "Update event fires exactly once for the tool message"
    );
    soft.strictEqual(
      received,
      toolMessage,
      "Event payload is the newly added tool message"
    );
    soft.equal(
      received?.content?.name,
      "get_open_tabs",
      "tool message carries the tool name"
    );
    soft.equal(
      received?.content?.tool_call_id,
      "tc_abc",
      "tool message carries the tool_call_id"
    );
  });
});

add_task(function test_updateToolCallMessage_reconciles_pending_in_place() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Search the web", null);

  const toolUpdates = [];
  conversation.on("chat-conversation:message-update", (_event, msg) => {
    if (msg?.role === MESSAGE_ROLE.TOOL) {
      toolUpdates.push(msg);
    }
  });

  // A slow tool emits a placeholder tool message up front so the action log
  // can render its pending row.
  const pending = conversation.addToolCallMessage({
    tool_call_id: "tc_slow",
    body: { pending: true },
    name: "search_the_web",
  });

  // On completion the same message is reconciled with the real result.
  const finalized = conversation.updateToolCallMessage(pending, {
    tool_call_id: "tc_slow",
    body: { could_answer: true },
    name: "search_the_web",
  });

  const toolMessages = conversation.messages.filter(
    m => m.role === MESSAGE_ROLE.TOOL
  );

  Assert.withSoftAssertions(function (soft) {
    soft.strictEqual(
      finalized,
      pending,
      "Reconciles the same message rather than creating a new one"
    );
    soft.equal(
      toolMessages.length,
      1,
      "No duplicate tool message is added on reconciliation"
    );
    soft.equal(
      toolMessages[0].content.body.could_answer,
      true,
      "Placeholder body is replaced by the real result"
    );
    soft.equal(
      toolUpdates.length,
      2,
      "Emits once for the pending row and once for the reconciliation"
    );
  });
});

add_task(function test_updateToolCallMessage_adds_when_no_pending() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Search the web", null);

  const added = conversation.updateToolCallMessage(null, {
    tool_call_id: "tc_fresh",
    body: { ok: true },
    name: "search_the_web",
  });

  const toolMessages = conversation.messages.filter(
    m => m.role === MESSAGE_ROLE.TOOL
  );

  Assert.withSoftAssertions(function (soft) {
    soft.ok(added, "Falls back to adding a fresh tool message");
    soft.equal(toolMessages.length, 1, "Exactly one tool message is present");
    soft.equal(
      toolMessages[0].content.tool_call_id,
      "tc_fresh",
      "The fresh tool message carries the tool_call_id"
    );
  });
});

add_task(function test_addToolCallMessage_emits_for_error_payload() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("Anything", null);

  let received = null;
  conversation.on("chat-conversation:message-update", (_event, msg) => {
    if (msg?.role === MESSAGE_ROLE.TOOL) {
      received = msg;
    }
  });

  conversation.addToolCallMessage({
    tool_call_id: "tc_err",
    body: { error: "Invalid JSON arguments" },
  });

  Assert.ok(received, "Error-path TOOL message still emits the event");
  Assert.equal(
    received.content.name,
    undefined,
    "Error-path TOOL message has no name field"
  );
});

add_task(function test_resolvePendingToolConfirmation_no_messages() {
  const conversation = new ChatConversation({});
  Assert.equal(
    conversation.resolvePendingToolConfirmation({ description: "x" }, "tc-1"),
    false,
    "Returns false when there are no messages"
  );
});

add_task(function test_resolvePendingToolConfirmation_non_tool_message() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("hi", null);
  conversation.addAssistantMessage("text", "hello");

  Assert.equal(
    conversation.resolvePendingToolConfirmation({ description: "x" }, "tc-1"),
    false,
    "Returns false when last message is not a TOOL message"
  );
});

add_task(function test_resolvePendingToolConfirmation_tool_not_pending() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("hi", null);
  conversation.addAssistantMessage("text", "hello");
  conversation.addToolCallMessage({
    tool_call_id: "tc-1",
    name: "manage_tabs",
    body: { success: true },
  });

  Assert.equal(
    conversation.resolvePendingToolConfirmation({ description: "x" }, "tc-1"),
    false,
    "Returns false when the tool body is not pending"
  );
});

add_task(function test_resolvePendingToolConfirmation_toolCallId_mismatch() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("close my tabs", null);
  conversation.addAssistantMessage("text", "confirm?");
  conversation.addToolCallMessage({
    tool_call_id: "tc-1",
    name: "manage_tabs",
    body: { pending: true },
  });

  const noopResolved = conversation.resolvePendingToolConfirmation(
    { description: "mismatch" },
    "tc-other"
  );
  Assert.equal(
    noopResolved,
    false,
    "Returns false when toolCallId does not match the most recent tool message"
  );
  Assert.deepEqual(
    conversation.messages.at(-1).content.body,
    { pending: true },
    "Body should remain unchanged when toolCallId does not match"
  );

  const okResolved = conversation.resolvePendingToolConfirmation(
    { description: "match" },
    "tc-1"
  );
  Assert.equal(
    okResolved,
    true,
    "Returns true when toolCallId filter matches the tail"
  );
});

add_task(function test_resolvePendingToolConfirmation_resolves_pending_tail() {
  const conversation = new ChatConversation({});
  conversation.addUserMessage("close my tabs", null);
  conversation.addAssistantMessage("text", "confirm?");
  conversation.addToolCallMessage({
    tool_call_id: "tc-1",
    name: "manage_tabs",
    body: { pending: true, action: "close" },
  });

  const tailBefore = conversation.messages.at(-1);

  let emittedMessage = null;
  conversation.on("chat-conversation:message-update", (_event, m) => {
    emittedMessage = m;
  });

  const outcome = { description: "User confirmed." };
  const resolved = conversation.resolvePendingToolConfirmation(outcome, "tc-1");

  Assert.equal(resolved, true, "Returns true when a pending tail is resolved");
  const tailAfter = conversation.messages.at(-1);
  Assert.strictEqual(
    tailAfter,
    tailBefore,
    "Resolves in place rather than appending a new message"
  );
  Assert.deepEqual(
    tailAfter.content.body,
    outcome,
    "Body is replaced with the supplied outcome"
  );
  Assert.equal(
    tailAfter.content.tool_call_id,
    "tc-1",
    "Other content keys are preserved"
  );
  Assert.equal(
    emittedMessage,
    tailAfter,
    "Emits chat-conversation:message-update with the resolved tail message"
  );
});
