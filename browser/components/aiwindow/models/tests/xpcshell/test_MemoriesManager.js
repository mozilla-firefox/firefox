/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

do_get_profile();
("use strict");

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const {
  HISTORY: SOURCE_HISTORY,
  CONVERSATION_USER_REQUEST: SOURCE_USER_REQUEST,
  MAX_MEMORY_SUMMARY_LENGTH,
  MEMORY_TYPE_DURABLE_MEMORY,
  MEMORY_TYPE_SHORT_TERM_MEMORY,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const { AIWindow } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs"
);
const { AuthProvider } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs"
);
const { EveryWindow } = ChromeUtils.importESModule(
  "resource:///modules/EveryWindow.sys.mjs"
);
const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);
const { Conversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs"
);
const { MODEL_FEATURES, SERVICE_TYPES, PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

/**
 * Constants for test memories
 */
const TEST_MODEL = "test-model";
const TEST_MESSAGE = "Remember I like coffee.";
const TEST_MEMORIES = [
  {
    type: MEMORY_TYPE_DURABLE_MEMORY,
    memory_summary: "Loves drinking coffee",
    reasoning: "Frequeently orders coffee online for pickup",
    tags: ["category:Food & Drink", "intent:Plan / Organize", "extra:tag"],
    sources: ["history"],
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    updated_at: 1000,
  },
  {
    type: MEMORY_TYPE_SHORT_TERM_MEMORY,
    memory_summary: "Buys dog food online",
    reasoning: "Frequently buys dog food on websites like Chewy",
    tags: ["category:Pets & Animals", "intent:Buy / Acquire"],
    sources: ["session"],
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    updated_at: 1,
  },
  {
    type: MEMORY_TYPE_DURABLE_MEMORY,
    memory_summary: "Plays games online",
    reasoning: "Visits a lot of gaming-related websites",
    tags: ["category:Games", "intent:Entertain / Relax", "extra:tag"],
    sources: ["user_request"],
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
    updated_at: 200,
  },
];

/**
 * Constants for preference keys and test values
 */
const PREF_API_KEY = "browser.smartwindow.apiKey";
const PREF_ENDPOINT = "browser.smartwindow.endpoint";
const PREF_MODEL = "browser.smartwindow.model";

const API_KEY = "fake-key";
const ENDPOINT = "https://api.fake-endpoint.com/v1";
const MODEL = "fake-model";

/**
 * Helper function to delete all memories before and after a test
 */
async function deleteAllMemories() {
  const memories = await MemoryStore.getMemories({ includeSoftDeleted: true });
  for (const memory of memories) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }
}

/**
 * Helper function to bulk-add memories
 */
async function addMemories() {
  await deleteAllMemories();
  for (const memory of TEST_MEMORIES) {
    await MemoryStore.addMemory(memory);
  }
}

/**
 * Helper function to return relevant memories with fake semantic embeddings
 *
 * @param {string} message
 * @param {int} topK
 * @param {float} threshold
 */
async function getFakeRelevantMemories(message, topK, threshold) {
  // Mock the private embeddings generator in MemoriesManager
  // We'll create a fake generator that returns predictable embeddings
  const fakeGenerator = {
    async embedMany(_texts) {
      // Return fake embeddings: one for each memory
      // Coffee memory gets [1, 0, 0], dog food gets [0, 1, 0], games gets [0, 0, 1]

      const embeds = [];
      for (const text of _texts) {
        if (text.includes("coffee")) {
          embeds.push([1, 0, 0]); //  "Loves drinking coffee" embedding
        } else if (text.includes("dog food")) {
          embeds.push([0, 1, 0]); //  "Buys dog food online" embedding (orthogonal)
        } else {
          embeds.push([0, 0, 1]); //  "Plays games online" embedding (orthogonal)
        }
      }
      return { output: embeds };
    },
    async embed(_text) {
      // Query about coffee should be similar to first memory
      return {
        output: [[0.9, 0.1, 0]], // Similar to coffee embedding
      };
    },
  };

  // Create a version that uses our fake generator
  // Sort by id to ensure deterministic order
  const memories = (
    await MemoryStore.getMemories({ includeSoftDeleted: true })
  ).sort((a, b) => a.id.localeCompare(b.id));
  if (memories.length === 0) {
    return [];
  }

  // Use fake embeddings
  const memoryEmbeddings = (
    await fakeGenerator.embedMany(
      memories.map(m => `${m.memory_summary}. ${m.reasoning || ""}`)
    )
  ).output;

  let queryEmbedding = (await fakeGenerator.embed(message)).output;
  if (Array.isArray(queryEmbedding) && queryEmbedding.length === 1) {
    queryEmbedding = queryEmbedding[0];
  }

  // Calculate cosine similarity manually
  const { cosSim } = ChromeUtils.importESModule(
    "chrome://global/content/ml/NLPUtils.sys.mjs"
  );

  const similarities = memoryEmbeddings.map((memEmb, idx) => ({
    ...memories[idx],
    similarity: cosSim(queryEmbedding, memEmb),
  }));

  return similarities
    .filter(m => m.similarity >= (threshold ?? 0.3))
    .sort((a, b) => b.similarity - a.similarity)
    .slice(0, topK ?? 5);
}

add_setup(async function () {
  // Setup prefs used across multiple tests
  Services.prefs.setStringPref(PREF_API_KEY, API_KEY);
  Services.prefs.setStringPref(PREF_ENDPOINT, ENDPOINT);
  Services.prefs.setStringPref(PREF_MODEL, MODEL);
  Services.prefs.setBoolPref("browser.ml.enable", true);

  // Clear prefs after testing
  registerCleanupFunction(() => {
    const prefsToClean = [
      PREF_API_KEY,
      PREF_ENDPOINT,
      PREF_MODEL,
      "browser.ml.enable",
    ];
    for (let pref of prefsToClean) {
      if (Services.prefs.prefHasUserValue(pref)) {
        Services.prefs.clearUserPref(pref);
      }
    }
  });
});

/**
 * Tests classifying a user message into memory categories and intents
 */
add_task(async function test_memoryClassifyMessage_happy_path() {
  const sb = sinon.createSandbox();
  try {
    const fakeConversation = {
      replaceMessages() {},
      clearMessages() {},
      setSystemMessage() {},
      addUserMessage() {},
      run() {
        return {
          finalOutput: `{
            "categories": ["Food & Drink"],
            "intents": ["Plan / Organize"]
          }`,
        };
      },
    };

    const stub = sb
      .stub(MemoriesManager, "ensureConversationForUsage")
      .returns(fakeConversation);
    const messageClassification =
      await MemoriesManager.memoryClassifyMessage(TEST_MESSAGE);
    // Check that the stub was called
    Assert.ok(
      stub.calledOnce,
      "ensureConversationForUsage should be called once"
    );

    // Check classification result was returned correctly
    Assert.equal(
      typeof messageClassification,
      "object",
      "Result should be an object."
    );
    Assert.equal(
      Object.keys(messageClassification).length,
      2,
      "Result should have two keys."
    );
    Assert.deepEqual(
      messageClassification.categories,
      ["Food & Drink"],
      "Categories should match the fake response."
    );
    Assert.deepEqual(
      messageClassification.intents,
      ["Plan / Organize"],
      "Intents should match the fake response."
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests failed message classification - LLM returns empty output
 */
add_task(async function test_memoryClassifyMessage_sad_path_empty_output() {
  const sb = sinon.createSandbox();
  try {
    const fakeConversation = {
      replaceMessages() {},
      clearMessages() {},
      setSystemMessage() {},
      addUserMessage() {},
      run() {
        return {
          finalOutput: ``,
        };
      },
    };

    const stub = sb
      .stub(MemoriesManager, "ensureConversationForUsage")
      .returns(fakeConversation);
    const messageClassification =
      await MemoriesManager.memoryClassifyMessage(TEST_MESSAGE);
    // Check that the stub was called
    Assert.ok(
      stub.calledOnce,
      "ensureConversationForUsage should be called once"
    );

    // Check classification result was returned correctly despite empty output
    Assert.equal(
      typeof messageClassification,
      "object",
      "Result should be an object."
    );
    Assert.equal(
      Object.keys(messageClassification).length,
      2,
      "Result should have two keys."
    );
    Assert.equal(
      messageClassification.category,
      null,
      "Category should be null for empty output."
    );
    Assert.equal(
      messageClassification.intent,
      null,
      "Intent should be null for empty output."
    );
  } finally {
    sb.restore();
  }
});

/**
 * Tests failed message classification - LLM returns incorrect schema
 */
add_task(async function test_memoryClassifyMessage_sad_path_bad_schema() {
  const sb = sinon.createSandbox();
  try {
    const fakeConversation = {
      replaceMessages() {},
      clearMessages() {},
      setSystemMessage() {},
      addUserMessage() {},
      run() {
        return {
          finalOutput: `{
            "wrong_key": "some value"
          }`,
        };
      },
    };

    const stub = sb
      .stub(MemoriesManager, "ensureConversationForUsage")
      .returns(fakeConversation);
    const messageClassification =
      await MemoriesManager.memoryClassifyMessage(TEST_MESSAGE);
    // Check that the stub was called
    Assert.ok(
      stub.calledOnce,
      "ensureConversationForUsage should be called once"
    );

    // Check classification result was returned correctly despite bad schema
    Assert.equal(
      typeof messageClassification,
      "object",
      "Result should be an object."
    );
    Assert.equal(
      Object.keys(messageClassification).length,
      2,
      "Result should have two keys."
    );
    Assert.equal(
      messageClassification.category,
      null,
      "Category should be null for bad schema output."
    );
    Assert.equal(
      messageClassification.intent,
      null,
      "Intent should be null for bad schema output."
    );
  } finally {
    sb.restore();
  }
});

/**
 * Ensures calls to getRelevantMemories via the public MemoriesManager API are correctly
 * routed through MemoryStore and returned
 */
add_task(async function test_getRelevantMemories_through_MemoryStore() {
  await addMemories();

  const sb = sinon.createSandbox();
  try {
    sb.stub(MemoryStore, "getRelevantMemories").callsFake(
      getFakeRelevantMemories
    );

    const relevantMemories =
      await MemoriesManager.getRelevantMemories(TEST_MESSAGE);

    // Check that the correct relevant memory was returned
    Assert.ok(Array.isArray(relevantMemories), "Result should be an array.");
    Assert.greaterOrEqual(
      relevantMemories.length,
      1,
      "Result should contain at least one relevant memory."
    );

    // The coffee memory should be ranked higher due to similarity
    Assert.equal(
      relevantMemories[0].memory_summary,
      "Loves drinking coffee",
      "Most relevant memory should be about coffee."
    );

    // Check that similarity score is present
    Assert.ok(
      "similarity" in relevantMemories[0],
      "Result should include similarity score"
    );
    Assert.strictEqual(
      typeof relevantMemories[0].similarity,
      "number",
      "Similarity should be a number"
    );
  } finally {
    sb.restore();
    await deleteAllMemories();
  }
});

/**
 * Tests that shouldEnableMemoriesFromSchedulers returns false when the
 * browser.smartwindow.firstrun.hasCompleted pref is false, even if every
 * other gate (AI Window enabled, source pref, ToU consent, active window)
 * is satisfied.
 */
add_task(async function test_shouldEnableMemoriesFromSchedulers_firstrunGate() {
  const PREF_GENERATE_MEMORIES_FROM_HISTORY =
    "browser.smartwindow.memories.generateFromHistory";
  const PREF_FIRSTRUN_HAS_COMPLETED =
    "browser.smartwindow.firstrun.hasCompleted";

  const sb = sinon.createSandbox();

  try {
    sb.stub(AIWindow, "isAIWindowEnabled").returns(true);
    sb.stub(AIWindow, "isAIWindowActive").returns(true);
    sb.stub(AuthProvider, "hasToUConsent").get(() => true);
    sb.stub(EveryWindow, "readyWindows").get(() => [{}]);

    Services.prefs.setBoolPref(PREF_GENERATE_MEMORIES_FROM_HISTORY, true);

    Services.prefs.setBoolPref(PREF_FIRSTRUN_HAS_COMPLETED, false);
    Assert.equal(
      MemoriesManager.shouldEnableMemoriesFromSchedulers(SOURCE_HISTORY),
      false,
      "Should be false when firstrun has not completed"
    );

    Services.prefs.setBoolPref(PREF_FIRSTRUN_HAS_COMPLETED, true);
    Assert.equal(
      MemoriesManager.shouldEnableMemoriesFromSchedulers(SOURCE_HISTORY),
      true,
      "Should be true when all gates pass (including firstrun completed)"
    );
  } finally {
    sb.restore();
    Services.prefs.clearUserPref(PREF_GENERATE_MEMORIES_FROM_HISTORY);
    Services.prefs.clearUserPref(PREF_FIRSTRUN_HAS_COMPLETED);
  }
});

// --- saveRequestedMemory tests ---
add_task(async function test_saveRequestedMemory_rejects_empty_summary() {
  try {
    const result = await MemoriesManager.saveRequestedMemory("");
    Assert.equal(result.ok, false, "Should reject empty summary");
  } finally {
    await deleteAllMemories();
  }
});

add_task(async function test_saveRequestedMemory_rejects_whitespace_summary() {
  try {
    const result = await MemoriesManager.saveRequestedMemory("   ");
    Assert.equal(result.ok, false, "Should reject whitespace-only summary");
  } finally {
    await deleteAllMemories();
  }
});

add_task(async function test_saveRequestedMemory_truncates_long_summary() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "remember I prefer Walmart" } }]);
  try {
    await deleteAllMemories();
    const result = await MemoriesManager.saveRequestedMemory(
      "a".repeat(MAX_MEMORY_SUMMARY_LENGTH + 50)
    );
    Assert.equal(result.ok, true, "Truncated summary should be accepted");
    Assert.equal(
      result.memory.memory_summary.length,
      MAX_MEMORY_SUMMARY_LENGTH,
      `Summary not truncated to ${MAX_MEMORY_SUMMARY_LENGTH} characters`
    );
  } finally {
    sandbox.restore();
    await deleteAllMemories();
  }
});

add_task(async function test_saveRequestedMemory_blocks_pii_in_summary() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "contact me" } }]);
  try {
    const result = await MemoriesManager.saveRequestedMemory(
      "My email is jane.doe@example.com"
    );
    Assert.equal(result.ok, false, "PII should be blocked in summary");
  } finally {
    sandbox.restore();
  }
});

add_task(async function test_saveRequestedMemory_blocks_pii_in_message() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "remember my card 4111 1111 1111 1111" } }]);
  try {
    const result =
      await MemoriesManager.saveRequestedMemory("Has a credit card");
    Assert.equal(
      result.ok,
      false,
      "PII should be blocked in originating message"
    );
  } finally {
    sandbox.restore();
    await deleteAllMemories();
  }
});

add_task(async function test_saveRequestedMemory_happy_path_creates() {
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(ChatStore, "getMostRecentMessages")
    .resolves([{ content: { body: "remember I prefer Walmart" } }]);
  try {
    await deleteAllMemories();
    const result = await MemoriesManager.saveRequestedMemory(
      "Prefers Walmart for shopping"
    );
    Assert.equal(result.ok, true, "save succeeded");
    Assert.equal(result.action, "created", "action is created");
    Assert.equal(
      result.memory.memory_summary,
      "Prefers Walmart for shopping",
      "memory_summary should match the input summary"
    );
    Assert.equal(result.memory.tags.length, 0, "Tags array defaults to empty");
    Assert.deepEqual(
      result.memory.sources,
      [SOURCE_USER_REQUEST],
      `Source should be ${SOURCE_USER_REQUEST}`
    );
    // Assert.equal(result.memory.score, 5, "Score should be 5"); #??
  } finally {
    sandbox.restore();
    await deleteAllMemories();
  }
});

/**
 * Tests the unified entry point short-circuits when neither source is enabled.
 */
add_task(async function test_generateMemoriesFromSessions_disabled_noop() {
  const sb = sinon.createSandbox();
  try {
    sb.stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers").returns(
      false
    );
    const result = await MemoriesManager.generateMemoriesFromSessions({
      maxBatchRetries: 1,
      initialMemoryGenerationRetryDelayMS: 1,
    });
    Assert.deepEqual(result, [], "Should return [] when no source is enabled");
  } finally {
    sb.restore();
  }
});

/**
 * Tests the unified entry point end-to-end: builds sessions from fetched
 * history, runs the pipeline, persists, and advances the watermark to the
 * processed session end.
 */
add_task(async function test_generateMemoriesFromSessions_happy_path() {
  await deleteAllMemories();
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });

  const sb = sinon.createSandbox();
  try {
    sb.stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers").returns(
      true
    );

    const visitDateMicros = 1_700_000_000_000_000;
    sb.stub(MemoriesManager, "_getRecentHistory").resolves([
      {
        urlHash: 1,
        domain: "mozilla.org",
        title: "Mozilla",
        searchQuery: "firefox privacy",
        visitDateMicros,
        totalViewTimeMs: 1000,
        source: "search",
      },
    ]);
    sb.stub(MemoriesManager, "_getRecentChats").resolves([]);

    let callIndex = 0;
    const fakeEngine = {
      run() {
        callIndex++;
        if (callIndex === 1) {
          return {
            finalOutput: `[{"reasoning":"r","category":"Internet & Telecom","intent":"Research / Learn","memory_summary":"Researches firefox privacy","score":4,"evidence":[]}]`,
          };
        }
        if (callIndex === 2) {
          return {
            finalOutput: `{"kept_memories":["Researches firefox privacy"]}`,
          };
        }
        return {
          finalOutput: `{"unique_memories":[{"main_memory":"Researches firefox privacy"}]}`,
        };
      },
    };

    const conversation = new Conversation({
      feature: MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM,
      model: TEST_MODEL,
      serviceType: SERVICE_TYPES.MEMORIES,
      purpose: PURPOSES.MEMORY_GENERATION,
      parameters: {},
      flowId: null,
      engine: fakeEngine,
    });

    sb.stub(MemoriesManager, "ensureConversationForGeneration").resolves(
      conversation
    );

    const setWatermark = sb.spy(
      MemoriesManager,
      "setLastSessionMemoryTimestamp"
    );

    const persisted = await MemoriesManager.generateMemoriesFromSessions({
      maxBatchRetries: 1,
      initialMemoryGenerationRetryDelayMS: 1,
    });

    Assert.equal(persisted.length, 1, "Should persist one memory");
    Assert.equal(
      persisted[0].memory_summary,
      "Researches firefox privacy",
      "Persisted memory should come from the pipeline"
    );
    Assert.ok(setWatermark.calledOnce, "Watermark should be advanced once");
    Assert.equal(
      setWatermark.firstCall.args[0],
      Math.floor(visitDateMicros / 1000),
      "Watermark should advance to the session end (visit time in ms)"
    );
  } finally {
    sb.restore();
    await deleteAllMemories();
  }
});

/**
 * Tests that when every built session gates to SKIP, the watermark still
 * advances past them so the same trivial sessions are not re-pulled and
 * re-gated on the next run.
 */
add_task(
  async function test_generateMemoriesFromSessions_all_skipped_advances_watermark() {
    await deleteAllMemories();
    await MemoryStore.updateMeta({
      last_session_memory_ts: 0,
      last_history_memory_ts: 0,
      last_chat_memory_ts: 0,
    });

    const sb = sinon.createSandbox();
    try {
      sb.stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers").returns(
        true
      );

      const visitDateMicros = 1_700_000_000_000_000;
      // Auth-domain visit with a nav title and no query -> gate SKIPs it.
      sb.stub(MemoriesManager, "_getRecentHistory").resolves([
        {
          urlHash: 2,
          domain: "accounts.google.com",
          title: "Sign in",
          visitDateMicros,
          totalViewTimeMs: 100,
          source: "typed",
        },
      ]);
      sb.stub(MemoriesManager, "_getRecentChats").resolves([]);

      const setWatermark = sb.spy(
        MemoriesManager,
        "setLastSessionMemoryTimestamp"
      );

      const persisted = await MemoriesManager.generateMemoriesFromSessions({
        maxBatchRetries: 1,
        initialMemoryGenerationRetryDelayMS: 1,
      });

      Assert.deepEqual(
        persisted,
        [],
        "Should persist nothing when all gated out"
      );
      Assert.ok(
        setWatermark.calledOnce,
        "Watermark should still advance past the skipped sessions"
      );
      Assert.equal(
        setWatermark.firstCall.args[0],
        Math.floor(visitDateMicros / 1000),
        "Watermark should advance to the skipped session end (visit time in ms)"
      );
    } finally {
      sb.restore();
      await deleteAllMemories();
    }
  }
);

/**
 * Stubs an enabled history source with one gate-passing search session and an
 * engine whose run() is driven by the given callback.
 *
 * @param {object} sb       Sinon sandbox to register the stubs on
 * @param {Function} runFn  Implementation for the fake engine's run()
 */
function stubPipelineWithEngine(sb, runFn) {
  const visitDateMicros = 1_700_000_000_000_000;
  sb.stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers").returns(true);
  // A search session with a query passes the gate, so the pipeline runs.
  sb.stub(MemoriesManager, "_getRecentHistory").resolves([
    {
      urlHash: 1,
      domain: "mozilla.org",
      title: "Mozilla",
      searchQuery: "firefox privacy",
      visitDateMicros,
      totalViewTimeMs: 1000,
      source: "search",
    },
  ]);
  sb.stub(MemoriesManager, "_getRecentChats").resolves([]);

  const fakeEngine = {
    run(...args) {
      return runFn(...args);
    },
  };

  const conversation = new Conversation({
    feature: MODEL_FEATURES.MEMORIES_INITIAL_GENERATION_SYSTEM,
    model: TEST_MODEL,
    serviceType: SERVICE_TYPES.MEMORIES,
    purpose: PURPOSES.MEMORY_GENERATION,
    parameters: {},
    flowId: null,
    engine: fakeEngine,
  });

  sb.stub(MemoriesManager, "ensureConversationForGeneration").resolves(
    conversation
  );
}

/**
 * Tests that a 429 from the pipeline is re-thrown (so the scheduler can back
 * off) and the watermark is not advanced. The 429 surfaces during generation.
 */
add_task(async function test_generateMemoriesFromSessions_rethrows_429() {
  await deleteAllMemories();
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });

  const sb = sinon.createSandbox();
  try {
    stubPipelineWithEngine(sb, function () {
      throw Object.assign(new Error("rate limited"), { status: 429 });
    });
    const setWatermark = sb.spy(
      MemoriesManager,
      "setLastSessionMemoryTimestamp"
    );

    await Assert.rejects(
      MemoriesManager.generateMemoriesFromSessions({
        maxBatchRetries: 1,
        initialMemoryGenerationRetryDelayMS: 1,
      }),
      /rate limited/,
      "A 429 should be re-thrown to the scheduler"
    );
    Assert.ok(
      setWatermark.notCalled,
      "Watermark should not advance when the pipeline is rate-limited"
    );
  } finally {
    sb.restore();
    await deleteAllMemories();
  }
});

/**
 * Tests that a non-retryable wholesale failure is swallowed (returns []) so the
 * run is retried on the next interval rather than propagating. Generation
 * succeeds and the failure surfaces at the (unguarded) filter step.
 */
add_task(
  async function test_generateMemoriesFromSessions_swallows_non_retryable() {
    await deleteAllMemories();
    await MemoryStore.updateMeta({
      last_session_memory_ts: 0,
      last_history_memory_ts: 0,
      last_chat_memory_ts: 0,
    });

    const sb = sinon.createSandbox();
    try {
      let callIndex = 0;
      stubPipelineWithEngine(sb, function () {
        callIndex++;
        if (callIndex === 1) {
          // Generation succeeds so there are candidates for the filter step.
          return {
            finalOutput: `[{"reasoning":"r","category":"Internet & Telecom","intent":"Research / Learn","memory_summary":"Researches firefox privacy","score":4,"evidence":[]}]`,
          };
        }
        // Filter step fails with a non-retryable error -> swallowed by the manager.
        throw new Error("some other failure");
      });
      const setWatermark = sb.spy(
        MemoriesManager,
        "setLastSessionMemoryTimestamp"
      );

      const result = await MemoriesManager.generateMemoriesFromSessions({
        maxBatchRetries: 1,
        initialMemoryGenerationRetryDelayMS: 1,
      });
      Assert.deepEqual(result, [], "Non-retryable failure should return []");
      Assert.ok(
        setWatermark.notCalled,
        "Watermark should not advance on a wholesale failure"
      );
    } finally {
      sb.restore();
      await deleteAllMemories();
    }
  }
);

/**
 * Tests that filtering parameters are correctly passed through the public getMemories API in MemoriesManager
 * to MemoryStore
 */
