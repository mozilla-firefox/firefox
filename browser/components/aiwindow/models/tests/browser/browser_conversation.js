/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);
const { buildConversation, loadPrompt } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs"
);
const { MODEL_FEATURES, renderPrompt } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { ConversationStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ConversationStore.sys.mjs"
);
const { EngineProcess } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EngineProcess.sys.mjs"
);
const { MLEngine } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs"
);

/**
 * Uses the real engine because MockEngineManager replaces the engine lifecycle
 * that idle shutdown goes through.
 */
add_task(async function test_conversation_stays_ready_through_idle_shutdown() {
  // Engines left by earlier runs keep the 2 minute default timeout and would
  // keep the inference process alive.
  await EngineProcess.destroyMLEngine();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.modelCacheTimeout", 1000]],
  });
  let requestCount = 0;

  await withServer(
    {
      onRequest: () => requestCount++,
    },
    async () => {
      const conversation = await buildConversation(
        MODEL_FEATURES.TITLE_GENERATION
      );
      conversation.addUserMessage("hello");
      const engine = conversation.engine.engineInstance;
      await conversation.run();
      Assert.equal(engine.engineStatus, "ready", "The engine starts ready");
      Assert.ok(
        conversation.isReady,
        "A conversation with a ready engine is ready"
      );

      await TestUtils.waitForCondition(
        () => EngineProcess.areAllEnginesTerminated(),
        "The inference process shuts down the idle engine"
      );
      Assert.equal(
        engine.engineStatus,
        "suspended",
        "The engine is suspended after the idle shutdown"
      );
      Assert.ok(
        conversation.isReady,
        "A conversation whose engine went idle is still ready"
      );

      const requestsBefore = requestCount;
      await conversation.run();
      Assert.equal(
        requestCount,
        requestsBefore + 1,
        "A turn on the idle conversation reaches the model"
      );
      Assert.equal(
        MLEngine.getInstance(engine.engineId)?.engineStatus,
        "ready",
        "The turn reopens a ready engine for the suspended one"
      );

      await TestUtils.waitForCondition(
        () => EngineProcess.areAllEnginesTerminated(),
        "The inference process shuts down the reopened engine once idle"
      );
      Assert.ok(
        conversation.isReady,
        "The conversation is still ready after going idle a second time"
      );
      let streamed = "";
      for await (const chunk of conversation.runWithGenerator({
        streamOptions: { enabled: true },
      })) {
        // The stream's closing chunk carries no text.
        streamed += chunk.text ?? "";
      }
      Assert.equal(
        streamed,
        "Hello from mock.",
        "A streamed turn on the idle conversation gets the model's reply"
      );

      await engine.terminate();
      Assert.ok(
        !conversation.isReady,
        "A conversation whose engine was terminated is not ready"
      );
    }
  );

  await EngineProcess.destroyMLEngine();
  await SpecialPowers.popPrefEnv();
});

add_task(function test_default_conversation_is_usable() {
  const conversation = new Conversation();

  Assert.deepEqual(conversation.messages, [], "Messages start empty");
  Assert.equal(conversation.feature, null, "Feature starts unset");
  Assert.equal(conversation.engine, null, "Engine starts unset");
  Assert.deepEqual(conversation.parameters, {}, "Parameters start empty");
  Assert.equal(conversation.seenUrls.size, 0, "Seen URLs start empty");
  Assert.equal(
    conversation.serpUrlsForAnonymousFetch.size,
    0,
    "Anonymous fetch URLs start empty"
  );
  Assert.deepEqual(
    conversation.securityProperties.toJSON(),
    { privateData: false, untrustedInput: false },
    "Security properties start clean"
  );

  conversation.setSystemMessage("system prompt");
  conversation.addUserMessage("hello");
  conversation.addSeenUrls(["https://example.com/"]);
  conversation.addSerpUrlsForAnonymousFetch(["https://example.org/"]);
  conversation.securityProperties.setUntrustedInput();
  conversation.securityProperties.commit();

  Assert.deepEqual(
    conversation.getMessagesInChatCompletionsFormat(),
    [
      { role: "system", content: "system prompt" },
      { role: "user", content: "hello" },
    ],
    "The default message list accepts messages"
  );
  Assert.deepEqual(
    [...conversation.seenUrls],
    ["https://example.com/"],
    "The default seen URL ledger accepts URLs"
  );
  Assert.deepEqual(
    [...conversation.serpUrlsForAnonymousFetch],
    ["https://example.org/"],
    "The default anonymous fetch ledger accepts URLs"
  );
  Assert.ok(
    conversation.securityProperties.untrustedInput,
    "The default security properties track untrusted input"
  );
});

add_task(async function test_build_conversation_uses_remote_settings_dump() {
  const feature = MODEL_FEATURES.TITLE_GENERATION;
  const conversation = await buildConversation(feature);

  Assert.equal(conversation.feature, feature, "The feature is configured");
  Assert.equal(
    conversation.engine.feature,
    feature,
    "The engine is configured for the feature"
  );
  Assert.ok(
    conversation.engine.model,
    "The engine model is loaded from the Remote Settings dump"
  );
  Assert.deepEqual(
    conversation.parameters,
    {},
    "Inference parameters are loaded from the Remote Settings dump"
  );
  Assert.deepEqual(conversation.messages, [], "The conversation starts empty");
  Assert.equal(conversation.seenUrls.size, 0, "Seen URLs start empty");
  Assert.equal(
    conversation.serpUrlsForAnonymousFetch.size,
    0,
    "Anonymous fetch URLs start empty"
  );
  Assert.deepEqual(
    conversation.securityProperties.toJSON(),
    { privateData: false, untrustedInput: false },
    "Security properties start clean"
  );
});

add_task(async function test_load_monitor_prompts_from_v2_remote_settings() {
  const feature = MODEL_FEATURES.AGENT_MONITOR;
  const [systemInstructions, userData] = await Promise.all([
    loadPrompt(feature, { module: "system-instructions" }),
    loadPrompt(feature, { module: "user-data" }),
  ]);

  Assert.ok(systemInstructions.version, "System instructions have a version");
  Assert.ok(userData.version, "User data has a version");

  const monitorPrompt = "MONITOR_PROMPT_SENTINEL";
  const renderedSystemPrompt = renderPrompt(systemInstructions.prompt, {
    monitorPrompt,
  });
  Assert.ok(
    renderedSystemPrompt.includes(monitorPrompt),
    "System instructions accept the monitor directive"
  );

  const userValues = {
    checkedAt: "CHECKED_AT_SENTINEL",
    pageUrls: "PAGE_URLS_SENTINEL",
    pageContent: "PAGE_CONTENT_SENTINEL",
  };
  const renderedUserData = renderPrompt(userData.prompt, userValues);
  for (const value of Object.values(userValues)) {
    Assert.ok(renderedUserData.includes(value), `User data accepts ${value}`);
  }
});

add_task(async function test_save_persists_committed_security_flags() {
  // Built the same way a tool call builds its conversation.
  const conversation = await buildConversation(MODEL_FEATURES.AITAB);
  conversation.addSeenUrls(["https://parent.example/seen"]);
  conversation.securityProperties.setPrivateData();
  conversation.securityProperties.setUntrustedInput();
  conversation.securityProperties.commit();

  try {
    await conversation.save();

    // Read the row back: an uncommitted flag looks true in memory and
    // false on disk, so only a round trip tells them apart.
    const stored = await ConversationStore.findConversationById(
      conversation.id
    );
    Assert.ok(stored, "the conversation is persisted");
    Assert.equal(stored.feature, "aitab", "its feature is stored");
    Assert.ok(
      stored.securityProperties.privateData,
      "privateData reaches the database as true"
    );
    Assert.ok(
      stored.securityProperties.untrustedInput,
      "untrustedInput reaches the database as true"
    );
    Assert.deepEqual(
      Array.from(stored.seenUrls),
      ["https://parent.example/seen"],
      "seen URLs are stored exactly as they were set"
    );
  } finally {
    await ConversationStore.deleteConversationById(conversation.id);
  }
});
