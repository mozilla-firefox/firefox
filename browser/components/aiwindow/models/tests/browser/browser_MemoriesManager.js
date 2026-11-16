/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const { ChatStore, ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);
const {
  MEMORY_TYPE_DURABLE_MEMORY,
  MEMORY_TYPE_LONG_TERM_MEMORY,
  MEMORY_TYPE_SHORT_TERM_MEMORY,
  MEMORY_TYPE_PROFILE_FACT,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
  MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_STRENGTH_THRESHOLD,
  MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD,
  MEMORY_MERGE_MIN_MEMORY_COUNT,
  MAX_MEMORY_SUMMARY_LENGTH,
  MAX_SESSIONS_FIRST_RUN,
  MAX_SESSIONS_DELTA_RUN,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { MEMORY_FILTER_COMPARATOR } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs"
);
const {
  computeMemoryFrecency,
  computeMemoryStrength,
  classifyMemoryAndCapStrength,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/Memories.sys.mjs"
);
const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

/**
 * Remove every memory (including soft-deleted ones) so each task starts from a
 * known-empty MemoryStore.
 */
async function clearMemories() {
  const memories = await MemoryStore.getMemories({ includeSoftDeleted: true });
  for (const memory of memories) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }
}

/**
 * Return the three stores to a clean state between tasks.
 */
async function resetStores() {
  await clearMemories();
  await PlacesUtils.history.clear();
  await ChatStore.destroyDatabase();
}

// Keeps generated memory_summary values unique across all tests by appending an incrementing int to each memory's summary.
let gMemorySeq = 0;

// One mock installed for the whole file: MemoriesManager caches the generation
// Conversation from the first merge, so a per-test mock would leave later merges
// routed to an engine whose _createEngine stub was already restored.
let gMockEngineManager;

// mergeMemories() overrides to run test with fewer minimum memories
const MERGE_ALL = Object.freeze({ minMemoryCount: 0 });

/**
 * Memory configuration fixture with usage and merge counts that clear durable
 * thresholds by a wide margin
 */
const STRONG_MEMORY_FIELDS = Object.freeze({
  lifetime_accessed_count: 100,
  merge_count: 5,
});

/**
 * Add a memory to the MemoryStore with valid, decay-surviving defaults. Any field can be overridden if needed.
 *
 * @param {object} [overrides]  Memory fields to override the defaults.
 * @returns {Promise<object>}   The stored memory, including its generated id.
 */
async function addMemory(overrides = {}) {
  const now = Date.now();
  const { source_ids: sourceIds, ...rest } = overrides;
  return MemoryStore.addMemory({
    memory_summary: `Test memory ${gMemorySeq++}`,
    type: MEMORY_TYPE_SHORT_TERM_MEMORY,
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    created_at: now,
    last_accessed: now,
    updated_at: rest.created_at ?? now,
    ...rest,
    source_ids: {
      history_source_ids: [],
      conversation_source_ids: [],
      ...sourceIds,
    },
  });
}

/**
 * Re-read a single memory (including soft-deleted) after a mutation.
 *
 * @param {string} id               The memory ID
 * @returns {Promise<object|null>}  The memory, or null if it no longer exists.
 */
async function getMemoryById(id) {
  const [memory] = await MemoryStore.getMemories({
    includeSoftDeleted: true,
    memoryIds: new Set([id]),
  });
  return memory ?? null;
}

/**
 * Insert a page into Places history and return its url_hash.
 *
 * @param {string} url            The page URL
 * @param {object} [opts]
 * @param {string} [opts.title]   Optional page title
 * @returns {Promise<number>}     The moz_places.url_hash for the inserted page.
 */
async function addPlacesPage(url, { title = "Test page" } = {}) {
  await PlacesUtils.history.insertMany([
    { url, title, visits: [{ date: new Date() }] },
  ]);
  return PlacesUtils.withConnectionWrapper(
    "browser_MemoriesManager-addPlacesPage",
    async db => {
      const rows = await db.execute(
        "SELECT url_hash FROM moz_places WHERE url = :url",
        { url }
      );
      return rows[0].getResultByName("url_hash");
    }
  );
}

/**
 * Create and persist a ChatStore conversation.
 *
 * @param {object} [opts]
 * @param {string} [opts.title]         Optional conversation title
 * @param {string} [opts.message]       Optional first message
 * @returns {Promise<ChatConversation>} The persisted conversation.
 */
async function addConversation({
  title = "Test conversation",
  message = "hello",
} = {}) {
  const conversation = new ChatConversation({});
  conversation.title = title;
  conversation.addUserMessage(message, "https://example.com/", 0);
  await ChatStore.updateConversation(conversation);
  return conversation;
}

/**
 * Order a memory's component_summaries by summary so they can be compared
 * against an expected set without depending on merge order.
 *
 * @param {Array<object>} componentSummaries  {memory_summary, reasoning} pairs
 * @returns {Array<object>}                   The pairs sorted by memory_summary.
 */
function sortComponentSummaries(componentSummaries) {
  return [...componentSummaries].sort((a, b) =>
    a.memory_summary.localeCompare(b.memory_summary)
  );
}

/**
 * Respond to the single merge request a mergeMemories() run makes, grouping the
 * given summaries into one merged memory.
 *
 * @param {Array<string>} componentStatements   The component memory summaries
 * @param {string} newStatement                 The merged memory's summary
 * @returns {Promise<void>}
 */
async function respondToMergeRequest(componentStatements, newStatement) {
  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
  turn.respond(
    JSON.stringify([
      {
        component_statements: componentStatements,
        new_reasoning: `Merged reasoning for ${newStatement}`,
        new_statement: newStatement,
      },
    ])
  );
}

// Marks the rendered session data in a generation prompt.
const SESSION_QUERY_PREFIX = "session query";

/**
 * Search-query text for the Nth visit of a session fixture. Zero-padded so no
 * query is a substring of another, which would make prompt assertions lie.
 *
 * @param {number} index  The visit's position in the fixture, 0 being oldest
 * @returns {string}      The query text
 */
function sessionQueryFor(index) {
  return `${SESSION_QUERY_PREFIX} ${String(index).padStart(3, "0")}`;
}

/**
 * Summary of the memory the mock model returns for the Nth generation batch.
 *
 * @param {number} batch  The generation batch's index
 * @returns {string}      The memory summary
 */
function sessionMemorySummaryFor(batch) {
  return `Memory from batch ${batch}`;
}

/**
 * Build history rows that sessionize into exactly `count` single-visit sessions.
 *
 * Visits are spaced 2h apart: comfortably past the sessionizer's 15m gap, so
 * every visit opens a new session, while still keeping the whole fixture inside
 * the production first-run lookback window. Row 0 is the oldest.
 *
 * @param {number} count        How many sessions to produce
 * @param {number} baseMicros   Visit date of the oldest row, in microseconds
 * @returns {Array<object>}     Rows shaped like getRecentHistory() output
 */
function buildSessionHistoryRows(count, baseMicros) {
  const twoHoursMicros = 2 * 60 * 60 * 1000 * 1000;
  const rows = [];
  for (let i = 0; i < count; i++) {
    rows.push({
      urlHash: i,
      domain: "mozilla.org",
      title: `Topic ${i}`,
      searchQuery: sessionQueryFor(i),
      visitDateMicros: baseMicros + i * twoHoursMicros,
      totalViewTimeMs: 1000,
      source: "search",
    });
  }
  return rows;
}

/**
 * Visit date of the last row buildSessionHistoryRows() would produce, in ms.
 * This is the session end the watermark should advance to.
 *
 * @param {number} count       How many sessions the fixture holds
 * @param {number} baseMicros  Visit date of the oldest row, in microseconds
 * @returns {number}           Newest session end, in milliseconds
 */
function newestSessionEndMs(count, baseMicros) {
  const rows = buildSessionHistoryRows(count, baseMicros);
  return Math.floor(rows[rows.length - 1].visitDateMicros / 1000);
}

/**
 * Drive one generateMemoriesFromSessions() run to completion against the mock
 * engine, answering each generation request with a single memory named for its
 * batch and then keeping all of them through the closing quality filter.
 *
 * @param {object} pipelineOpts  Forwarded to generateMemoriesFromSessions()
 * @returns {Promise<{
 *   generationPrompts: Array<string>,
 *   keptSummaries: Array<string>,
 *   persisted: Array<object>,
 * }>}
 *        `generationPrompts` holds one serialized request per generation batch,
 *        in the order the pipeline issued them.
 */
async function runSessionGenerationWithMockEngine(pipelineOpts) {
  const runPromise = MemoriesManager.generateMemoriesFromSessions(pipelineOpts);

  const generationPrompts = [];
  let filterTurn = null;
  while (!filterTurn) {
    const turn = await gMockEngineManager.captureRequest({
      purpose: PURPOSES.MEMORY_GENERATION,
    });
    const serializedArgs = JSON.stringify(turn.request.args);
    if (!serializedArgs.includes(SESSION_QUERY_PREFIX)) {
      filterTurn = turn;
      break;
    }
    generationPrompts.push(serializedArgs);
    turn.respond(
      JSON.stringify([
        {
          reasoning: "Repeatedly searched this topic",
          category: "Internet & Telecom",
          intent: "Research / Learn",
          memory_summary: sessionMemorySummaryFor(generationPrompts.length - 1),
          score: 4,
          evidence: [],
        },
      ])
    );
  }

  const keptSummaries = generationPrompts.map((_, batch) =>
    sessionMemorySummaryFor(batch)
  );
  filterTurn.respond(JSON.stringify({ kept_memories: keptSummaries }));

  return { generationPrompts, keptSummaries, persisted: await runPromise };
}

/**
 * Assert that a capped run sent the newest `cap` sessions of a `count`-session
 * fixture to the model and nothing older, and that the retained batches ran
 * oldest-first.
 *
 * @param {Array<string>} generationPrompts  Serialized generation requests
 * @param {number} count                     Sessions in the fixture
 * @param {number} cap                       Sessions the run should have kept
 */
function assertRetainedNewestSessions(generationPrompts, count, cap) {
  const firstRetained = count - cap;
  const allPrompts = generationPrompts.join("");
  for (let i = 0; i < count; i++) {
    const shouldBeSent = i >= firstRetained;
    Assert.equal(
      allPrompts.includes(sessionQueryFor(i)),
      shouldBeSent,
      `"${sessionQueryFor(i)}" should ${shouldBeSent ? "" : "not "}be sent to the model.`
    );
  }

  // Selection is newest-first, but the retained batches must still run
  // oldest-to-newest so the pipeline's watermark stays contiguous with the work
  // it actually did.
  Assert.ok(
    generationPrompts[0].includes(sessionQueryFor(firstRetained)),
    "The first batch should hold the oldest of the retained sessions."
  );
  Assert.ok(
    generationPrompts[generationPrompts.length - 1].includes(
      sessionQueryFor(count - 1)
    ),
    "The last batch should hold the newest of the retained sessions."
  );
}

add_setup(async function () {
  await resetStores();
  gMockEngineManager = new MockEngineManager();
  registerCleanupFunction(() => gMockEngineManager.cleanupMocks());
  registerCleanupFunction(resetStores);
});

/**
 * Tests retrieving all stored memories.
 */
add_task(async function test_getAllMemories() {
  await clearMemories();

  const summaries = [
    "Loves drinking coffee",
    "Buys dog food online",
    "Plays games online",
  ];
  for (const memory_summary of summaries) {
    await addMemory({ memory_summary });
  }

  const memories = await MemoriesManager.getAllMemories();

  Assert.equal(
    memories.length,
    summaries.length,
    "Should retrieve all stored memories."
  );

  const retrievedSummaries = memories.map(memory => memory.memory_summary);
  for (const memory_summary of summaries) {
    Assert.ok(
      retrievedSummaries.includes(memory_summary),
      `Memory summary "${memory_summary}" should be retrieved.`
    );
  }
});

/**
 * Tests retrieving specific memories by ID.
 */
add_task(async function test_getMemoriesByID() {
  await clearMemories();

  const first = await addMemory({ memory_summary: "Loves drinking coffee" });
  await addMemory({ memory_summary: "Buys dog food online" });
  const third = await addMemory({ memory_summary: "Plays games online" });

  const retrieved = await MemoriesManager.getMemoriesByID(
    new Set([first.id, third.id])
  );

  Assert.equal(
    retrieved.length,
    2,
    "Should retrieve both memories by their IDs."
  );

  const retrievedSummaries = retrieved.map(memory => memory.memory_summary);
  Assert.ok(
    retrievedSummaries.includes(first.memory_summary),
    "Retrieved memories should include the first requested summary."
  );
  Assert.ok(
    retrievedSummaries.includes(third.memory_summary),
    "Retrieved memories should include the second requested summary."
  );
});

/**
 * Tests that recording use increments counts and resolves the used memories.
 */
add_task(async function test_resolveUsedMemories() {
  await clearMemories();

  const used = await addMemory({
    memory_summary: "Loves drinking coffee",
    last_accessed: 1000,
  });
  const unused = await addMemory({ memory_summary: "Buys dog food online" });
  const untouchedUpdatedAt = used.updated_at;

  const sandbox = sinon.createSandbox();
  const requestSaveSpy = sandbox.spy(MemoryStore, "requestSave");
  let resolved;
  try {
    resolved = await MemoriesManager.resolveUsedMemories([
      used.id,
      "hallucinated-id",
    ]);
  } finally {
    sandbox.restore();
  }

  Assert.equal(resolved.length, 1, "Should resolve only the known memory.");
  Assert.equal(
    resolved[0].memory_summary,
    used.memory_summary,
    "Should resolve the used memory."
  );
  Assert.equal(
    resolved[0].lifetime_accessed_count,
    1,
    "Used memory lifetime count should be incremented."
  );
  Assert.equal(
    resolved[0].recent_accessed_counts[0],
    1,
    "Used memory should be counted in today's bucket."
  );
  Assert.greater(
    resolved[0].last_accessed,
    1000,
    "last_accessed should be stamped with the time of use."
  );
  Assert.equal(
    resolved[0].updated_at,
    untouchedUpdatedAt,
    "updated_at should not be bumped."
  );
  Assert.ok(requestSaveSpy.called, "Recording a use should schedule a save.");

  const unusedAfter = await getMemoryById(unused.id);
  Assert.equal(
    unusedAfter.lifetime_accessed_count,
    0,
    "Memories that weren't used should not be counted."
  );

  await MemoriesManager.resolveUsedMemories([used.id]);

  const usedAfter = await getMemoryById(used.id);
  Assert.equal(
    usedAfter.lifetime_accessed_count,
    2,
    "Repeat uses should accumulate."
  );
  Assert.equal(
    usedAfter.recent_accessed_counts[0],
    2,
    "Repeat uses should accumulate in today's bucket."
  );
});

/**
 * Tests that IDs which can't be resolved to a live memory are ignored.
 */
add_task(async function test_resolveUsedMemories_ignores_unusable_ids() {
  await clearMemories();

  const memory = await addMemory({ memory_summary: "Loves drinking coffee" });
  await MemoriesManager.softDeleteMemoryById(memory.id);

  const sandbox = sinon.createSandbox();
  const requestSaveSpy = sandbox.spy(MemoryStore, "requestSave");
  try {
    Assert.deepEqual(
      await MemoriesManager.resolveUsedMemories([memory.id]),
      [],
      "Soft-deleted memories should not be resolved."
    );
    Assert.deepEqual(
      await MemoriesManager.resolveUsedMemories(["no-such-memory"]),
      [],
      "Unknown IDs should be ignored rather than throwing."
    );
    Assert.deepEqual(
      await MemoriesManager.resolveUsedMemories([]),
      [],
      "An empty ID list should be a no-op."
    );
  } finally {
    sandbox.restore();
  }

  Assert.ok(
    requestSaveSpy.notCalled,
    "Having nothing to record should not schedule a save."
  );

  const memoryAfter = await getMemoryById(memory.id);
  Assert.equal(
    memoryAfter.lifetime_accessed_count,
    0,
    "A soft-deleted memory's count should be untouched."
  );
});

/**
 * Tests filtering memories by attribute through MemoryStore.
 */
add_task(
  async function test_getMemoriesByAttribute_with_filters_through_MemoryStore() {
    await clearMemories();

    await addMemory({
      memory_summary: "Loves drinking coffee",
      type: MEMORY_TYPE_DURABLE_MEMORY,
    });
    await addMemory({ memory_summary: "Buys dog food online" });
    await addMemory({
      memory_summary: "Plays games online",
      type: MEMORY_TYPE_DURABLE_MEMORY,
    });

    const durableMemories = await MemoriesManager.getMemoriesByAttribute([
      {
        field: "type",
        value: [MEMORY_TYPE_DURABLE_MEMORY],
        comparator: MEMORY_FILTER_COMPARATOR.IN,
      },
    ]);

    Assert.equal(
      durableMemories.length,
      2,
      `Should return 2 memories with the "${MEMORY_TYPE_DURABLE_MEMORY}" type.`
    );
    Assert.deepEqual(
      durableMemories.map(memory => memory.memory_summary).sort(),
      ["Loves drinking coffee", "Plays games online"].sort(),
      `Filter by "${MEMORY_TYPE_DURABLE_MEMORY}" should return the expected summaries.`
    );
  }
);

/**
 * Tests soft deleting a memory by ID.
 */
add_task(async function test_softDeleteMemoryById() {
  await clearMemories();

  const memory = await addMemory({ memory_summary: "Loves drinking coffee" });
  Assert.equal(
    memory.is_deleted,
    false,
    "Memory should not be soft deleted initially."
  );

  const softDeleted = await MemoriesManager.softDeleteMemoryById(memory.id);
  Assert.equal(
    softDeleted.is_deleted,
    true,
    "Memory should be soft deleted after softDeleteMemoryById."
  );

  const withDeleted = await MemoriesManager.getAllMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(
    withDeleted.filter(mem => mem.is_deleted).length,
    1,
    "There should be one soft deleted memory."
  );

  const withoutDeleted = await MemoriesManager.getAllMemories();
  Assert.ok(
    !withoutDeleted.some(mem => mem.id === memory.id),
    "Soft deleted memory should be excluded by default."
  );
});

/**
 * Tests attempting to soft delete a memory that doesn't exist by ID.
 */
add_task(async function test_softDeleteMemoryById_not_found() {
  await clearMemories();

  await addMemory({ memory_summary: "Loves drinking coffee" });

  const result = await MemoriesManager.softDeleteMemoryById("non-existent-id");
  Assert.equal(
    result,
    null,
    "softDeleteMemoryById should return null for a non-existent memory ID."
  );

  const withDeleted = await MemoriesManager.getAllMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(
    withDeleted.filter(mem => mem.is_deleted).length,
    0,
    "No memory should be soft deleted after deleting a non-existent ID."
  );
});

/**
 * Tests hard deleting a memory by ID.
 */
add_task(async function test_hardDeleteMemoryById() {
  await clearMemories();

  const first = await addMemory({ memory_summary: "Loves drinking coffee" });
  await addMemory({ memory_summary: "Buys dog food online" });

  const deletionResult = await MemoriesManager.hardDeleteMemoryById(first.id);
  Assert.ok(
    deletionResult,
    "hardDeleteMemoryById should return true on successful deletion."
  );

  const remaining = await MemoriesManager.getAllMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(
    remaining.length,
    1,
    "There should be one fewer memory after hard deletion."
  );
  Assert.ok(
    !remaining.some(mem => mem.id === first.id),
    "The hard-deleted memory should no longer be present."
  );
});

/**
 * Tests attempting to hard delete a memory that doesn't exist by ID.
 */
add_task(async function test_hardDeleteMemoryById_not_found() {
  await clearMemories();

  await addMemory({ memory_summary: "Loves drinking coffee" });
  await addMemory({ memory_summary: "Buys dog food online" });

  const deletionResult =
    await MemoriesManager.hardDeleteMemoryById("non-existent-id");
  Assert.ok(
    !deletionResult,
    "hardDeleteMemoryById should return false for a non-existent memory ID."
  );

  const remaining = await MemoriesManager.getAllMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(
    remaining.length,
    2,
    "Memory count should be unchanged after a failed hard deletion."
  );
});

/**
 * Tests saveMemories persists with the given source tag and leaves the
 * watermark untouched (advancing it is the caller's responsibility).
 */
add_task(async function test_saveMemories_persists_with_source() {
  await clearMemories();
  await MemoriesManager.setLastSessionMemoryTimestamp(777);

  const { persistedMemories } = await MemoriesManager.saveMemories([
    {
      memory_summary: "Likes both dogs and cats",
      reasoning: "r",
      tags: ["category:Pets & Animals", "intent:Entertain / Relax"],
      sources: ["session"],
    },
  ]);

  Assert.equal(persistedMemories.length, 1, "Should persist one memory.");
  Assert.deepEqual(
    persistedMemories[0].sources,
    ["session"],
    "Persisted memory should carry the session source tag."
  );
  Assert.equal(
    await MemoriesManager.getLastSessionMemoryTimestamp(),
    777,
    "saveMemories should not change the watermark."
  );

  // Reset the watermark so it doesn't leak into later tasks.
  await MemoriesManager.setLastSessionMemoryTimestamp(0);
});

/**
 * Tests the unified session watermark: read/write and migration seeding from
 * the legacy per-modality timestamps.
 */
add_task(async function test_sessionWatermark_readWrite_and_migration() {
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });

  await MemoriesManager.setLastSessionMemoryTimestamp(123456);
  Assert.equal(
    await MemoriesManager.getLastSessionMemoryTimestamp(),
    123456,
    "Should read back the persisted session watermark."
  );

  // With no session watermark, seed from the older of the two legacy values.
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 500,
    last_chat_memory_ts: 300,
  });
  Assert.equal(
    await MemoriesManager.getLastSessionMemoryTimestamp(),
    300,
    "Should seed from min(legacy history, legacy chat) when no session watermark."
  );

  // Reset meta so it doesn't leak into later tasks.
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });
});

/**
 * Tests the generation-run timestamp that drives the scheduler cooldown:
 * read/write, and migration seeding from the session watermark for profiles
 * written before the timestamp was persisted.
 */
add_task(async function test_generationRunTimestamp_readWrite_and_migration() {
  await MemoryStore.updateMeta({
    last_generation_run_ts: 0,
    last_session_memory_ts: 0,
  });

  // Run timestamp set
  await MemoriesManager.setLastGenerationRunTimestamp(123456);
  Assert.equal(
    await MemoriesManager.getLastGenerationRunTimestamp(),
    123456,
    "Should read back the persisted generation run timestamp."
  );

  // An old profile has no run timestamp, so seed from the session watermark.
  await MemoryStore.updateMeta({
    last_generation_run_ts: 0,
    last_session_memory_ts: 500,
  });
  Assert.equal(
    await MemoriesManager.getLastGenerationRunTimestamp(),
    500,
    "Should seed from the session watermark when no run timestamp is stored."
  );

  // A stored run timestamp takes precedence over the watermark outright, rather
  // than being reconciled with it.
  await MemoryStore.updateMeta({
    last_generation_run_ts: 700,
    last_session_memory_ts: 900,
  });
  Assert.equal(
    await MemoriesManager.getLastGenerationRunTimestamp(),
    700,
    "A stored run timestamp should win over the session watermark."
  );

  // Reset meta so it doesn't leak into later tasks.
  await MemoryStore.updateMeta({
    last_generation_run_ts: 0,
    last_session_memory_ts: 0,
  });
});

/**
 * Tests that runMemoryMaintenance hard-deletes soft-deleted memories in its
 * initial pre-pass, leaving live memories intact.
 */
add_task(
  async function test_runMemoryMaintenance_hardDeletesSoftDeletedMemories() {
    await clearMemories();

    const softDeleted = await addMemory({
      memory_summary: "Soft deleted memory",
      is_deleted: true,
    });
    const live = await addMemory({ memory_summary: "Live memory" });

    await MemoriesManager.runMemoryMaintenance();

    const remaining = await MemoriesManager.getAllMemories({
      includeSoftDeleted: true,
    });
    Assert.equal(
      remaining.length,
      1,
      "Only the live memory should remain after maintenance."
    );
    Assert.equal(
      remaining[0].id,
      live.id,
      "The surviving memory should be the live one."
    );
    Assert.ok(
      !(await getMemoryById(softDeleted.id)),
      "The soft-deleted memory should be hard-deleted."
    );
  }
);

/**
 * Tests that runMemoryMaintenance hard-deletes memories that have decayed past
 * the retention threshold, while retaining recently-accessed memories.
 */
add_task(async function test_runMemoryMaintenance_hardDeletesDecayedMemories() {
  await clearMemories();

  // Both are old enough for maintenance to age them; only last_accessed differs,
  // so retention alone decides which survives.
  const monthAgo = Date.now() - 30 * 24 * 60 * 60 * 1000;

  // last_accessed far in the past drives retention below the decay threshold.
  const decayed = await addMemory({
    memory_summary: "Decayed memory",
    created_at: monthAgo,
    last_accessed: 1,
  });
  const healthy = await addMemory({
    memory_summary: "Healthy memory",
    created_at: monthAgo,
  });

  await MemoriesManager.runMemoryMaintenance();

  const remaining = await MemoriesManager.getAllMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(
    remaining.length,
    1,
    "Only the healthy memory should remain after maintenance."
  );
  Assert.equal(
    remaining[0].id,
    healthy.id,
    "The surviving memory should be the recently-accessed one."
  );
  Assert.ok(
    !(await getMemoryById(decayed.id)),
    "The decayed memory should be hard-deleted."
  );
});

/**
 * Tests that runMemoryMaintenance measures decay for a never-used memory from
 * its created_at rather than from the epoch.
 */
add_task(async function test_runMemoryMaintenance_keepsNeverUsedNewMemory() {
  await clearMemories();

  // Old enough for maintenance to evaluate it, young enough that decay measured
  // from created_at retains it.
  const twoDaysAgo = Date.now() - 2 * 24 * 60 * 60 * 1000;

  const neverUsed = await addMemory({
    memory_summary: "Never used memory",
    created_at: twoDaysAgo,
    last_accessed: null,
  });

  const startTime = Date.now();
  await MemoriesManager.runMemoryMaintenance();

  const updated = await getMemoryById(neverUsed.id);
  Assert.ok(
    updated,
    "A memory that has never been used should survive its first maintenance run."
  );
  Assert.greaterOrEqual(
    updated.updated_at,
    startTime,
    "The memory should have been evaluated for decay, not skipped as too young."
  );
  Assert.equal(
    updated.last_accessed,
    null,
    "Maintenance should not invent a last_accessed for a memory that was never used."
  );
});

/**
 * Tests that runMemoryMaintenance refreshes each surviving memory's count- and
 * date-based metadata.
 */
add_task(async function test_runMemoryMaintenance_refreshesMetadata() {
  await clearMemories();

  // updated_at two days ago so the rolling window ages by exactly two slots.
  const twoDaysAgo = Date.now() - 2 * 24 * 60 * 60 * 1000;

  // Sentinels so we can prove frecency & strength were recomputed rather than left as-is.
  const initialFrecency = -1;
  const initialStrength = -1;

  const memory = await addMemory({
    memory_summary: "Metadata refresh memory",
    updated_at: twoDaysAgo,
    recent_accessed_counts: { 0: 5, 1: 3, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0 },
    frecency: initialFrecency,
    strength: initialStrength,
  });

  const startTime = Date.now();
  const sandbox = sinon.createSandbox();
  const requestSaveSpy = sandbox.spy(MemoryStore, "requestSave");
  const hardDeleteSpy = sandbox.spy(MemoryStore, "hardDeleteMemory");
  try {
    await MemoriesManager.runMemoryMaintenance();
  } finally {
    sandbox.restore();
  }

  Assert.ok(
    hardDeleteSpy.notCalled,
    "No delete should happen for a metadata-only run."
  );
  Assert.ok(
    requestSaveSpy.called,
    "Metadata-only changes should still schedule a save."
  );

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");

  Assert.deepEqual(
    updated.recent_accessed_counts,
    { 0: 0, 1: 0, 2: 5, 3: 3, 4: 0, 5: 0, 6: 0 },
    "recent_accessed_counts should shift by the number of days elapsed."
  );

  Assert.greaterOrEqual(
    updated.updated_at,
    startTime,
    "updated_at should be refreshed to the maintenance run time."
  );

  Assert.notEqual(
    updated.frecency,
    initialFrecency,
    "frecency should be recomputed."
  );
  Assert.equal(
    updated.frecency,
    computeMemoryFrecency(updated),
    "frecency should match a fresh computation over the aged window."
  );
  Assert.ok(
    Number.isFinite(updated.strength) &&
      updated.strength > 0 &&
      updated.strength != initialStrength,
    "strength should be recomputed to a positive value."
  );
  const expected = { ...updated, strength: computeMemoryStrength(updated) };
  classifyMemoryAndCapStrength(expected);
  Assert.less(
    Math.abs(updated.strength - expected.strength),
    0.01,
    "strength should match a fresh computation, capped to the memory's age."
  );
});

/**
 * Tests that runMemoryMaintenance clears the rolling usage window entirely when
 * more than a full window's worth of days have elapsed, rather than wrapping
 * stale counts back into it.
 */
add_task(async function test_runMemoryMaintenance_agesOutFullWindow() {
  await clearMemories();

  // updated_at well past MEMORY_FRECENCY_MAX_DAYS so every slot ages out, but
  // last_accessed stays current so the memory survives decay.
  const tenDaysAgo = Date.now() - 10 * 24 * 60 * 60 * 1000;

  const memory = await addMemory({
    memory_summary: "Fully aged window memory",
    created_at: tenDaysAgo,
    updated_at: tenDaysAgo,
    recent_accessed_counts: { 0: 5, 1: 3, 2: 0, 3: 0, 4: 0, 5: 2, 6: 4 },
  });

  await MemoriesManager.runMemoryMaintenance();

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");

  Assert.deepEqual(
    updated.recent_accessed_counts,
    { 0: 0, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0 },
    "Every slot should be zeroed once the elapsed days exceed the window."
  );
  Assert.equal(
    updated.frecency,
    computeMemoryFrecency(updated),
    "frecency should match a fresh computation over the emptied window."
  );
});

/**
 * Tests that runMemoryMaintenance promotes a memory that is both strong enough
 * and old enough for the durable tier.
 */
add_task(async function test_runMemoryMaintenance_promotesAgedStrongMemory() {
  await clearMemories();

  // Old enough for every tier, but accessed just now so it survives decay.
  const memory = await addMemory({
    memory_summary: "Aged strong memory",
    created_at: Date.now() - 60 * 24 * 60 * 60 * 1000,
    ...STRONG_MEMORY_FIELDS,
  });

  await MemoriesManager.runMemoryMaintenance();

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");
  Assert.equal(
    updated.type,
    MEMORY_TYPE_DURABLE_MEMORY,
    "A memory past the durable age and strength thresholds should be promoted to durable."
  );
  Assert.greater(
    updated.strength,
    MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD,
    "A memory that has aged into the durable tier should keep its uncapped strength."
  );
});

/**
 * Tests that runMemoryMaintenance denies a strong but young memory the long
 * term tier, and persists the capped strength.
 */
add_task(async function test_runMemoryMaintenance_capsYoungStrongMemory() {
  await clearMemories();

  // Old enough for maintenance to age it, far short of the long term threshold.
  const memory = await addMemory({
    memory_summary: "Young strong memory",
    created_at: Date.now() - 2 * 24 * 60 * 60 * 1000,
    ...STRONG_MEMORY_FIELDS,
  });

  await MemoriesManager.runMemoryMaintenance();

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");
  Assert.equal(
    updated.type,
    MEMORY_TYPE_SHORT_TERM_MEMORY,
    "A memory too young for the long term tier should stay short term however strong."
  );
  Assert.equal(
    updated.strength,
    MEMORY_TYPE_SHORT_TERM_TO_LONG_TERM_STRENGTH_THRESHOLD,
    "The capped strength should be persisted, not just used for the type decision."
  );
});

/**
 * Tests that runMemoryMaintenance stops a strong memory at the long term tier
 * while it is still too young for the durable one.
 */
add_task(async function test_runMemoryMaintenance_middleAgedStopsAtLongTerm() {
  await clearMemories();

  // Past the long term age threshold, short of the durable one.
  const memory = await addMemory({
    memory_summary: "Middle aged strong memory",
    created_at: Date.now() - 20 * 24 * 60 * 60 * 1000,
    ...STRONG_MEMORY_FIELDS,
  });

  await MemoriesManager.runMemoryMaintenance();

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");
  Assert.equal(
    updated.type,
    MEMORY_TYPE_LONG_TERM_MEMORY,
    "A memory too young for the durable tier should be promoted only to long term."
  );
  Assert.equal(
    updated.strength,
    MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD,
    "A memory too young for the durable tier should have its strength capped at that tier's threshold."
  );
});

/**
 * Tests that runMemoryMaintenance demotes a memory whose recomputed strength no
 * longer supports its stored type.
 */
add_task(async function test_runMemoryMaintenance_demotesWeakenedMemory() {
  await clearMemories();

  // Old enough for every tier, but with no usage or merges its strength
  // recomputes down to the floor.
  const memory = await addMemory({
    memory_summary: "Weakened durable memory",
    type: MEMORY_TYPE_DURABLE_MEMORY,
    created_at: Date.now() - 100 * 24 * 60 * 60 * 1000,
  });

  const sandbox = sinon.createSandbox();
  const requestSaveSpy = sandbox.spy(MemoryStore, "requestSave");
  try {
    await MemoriesManager.runMemoryMaintenance();
  } finally {
    sandbox.restore();
  }

  const updated = await getMemoryById(memory.id);
  Assert.ok(updated, "The memory should survive maintenance.");
  Assert.equal(
    updated.type,
    MEMORY_TYPE_SHORT_TERM_MEMORY,
    "A durable memory whose strength fell below every threshold should be demoted to short term."
  );
  Assert.ok(
    requestSaveSpy.called,
    "A type-only change should still schedule a save."
  );
});

/**
 * Tests that a first-run backlog larger than the session cap is trimmed to the
 * most recent sessions before any LLM call is made, that the retained sessions
 * still reach the model oldest-first, and that the watermark advances past the
 * older sessions the cap abandoned.
 */
add_task(async function test_generateMemoriesFromSessions_caps_first_run() {
  await clearMemories();
  // First run is defined by an unset watermark.
  await MemoryStore.updateMeta({
    last_session_memory_ts: 0,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });

  // A backlog deliberately larger than the cap.
  const EXCESS_SESSIONS = 25;
  const SESSION_COUNT = MAX_SESSIONS_FIRST_RUN + EXCESS_SESSIONS;
  const BATCH_SIZE = 10;
  const EXPECTED_GENERATION_REQUESTS = Math.ceil(
    MAX_SESSIONS_FIRST_RUN / BATCH_SIZE
  );
  const UNCAPPED_GENERATION_REQUESTS = Math.ceil(SESSION_COUNT / BATCH_SIZE);
  const baseMicros = 1_700_000_000_000_000;

  const sandbox = sinon.createSandbox();
  try {
    // Stubbing just the checks to make sure the scheduler runs
    sandbox
      .stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers")
      .returns(true);
    const getRecentHistory = sandbox
      .stub(MemoriesManager, "_getRecentHistory")
      .resolves(buildSessionHistoryRows(SESSION_COUNT, baseMicros));
    sandbox.stub(MemoriesManager, "_getRecentChats").resolves([]);
    const setWatermark = sandbox.spy(
      MemoriesManager,
      "setLastSessionMemoryTimestamp"
    );

    Services.fog.testResetFOG();
    const stats = {};
    const { generationPrompts, keptSummaries, persisted } =
      await runSessionGenerationWithMockEngine({
        batchSize: BATCH_SIZE,
        maxBatchRetries: 1,
        initialMemoryGenerationRetryDelayMS: 1,
        stats,
      });

    // The sources are stubbed, so they report no counts.
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        is_first_run: true,
        sessions_built: SESSION_COUNT,
        sessions_skipped: 0,
        sessions_processed: MAX_SESSIONS_FIRST_RUN,
        sessions_processed_events: MAX_SESSIONS_FIRST_RUN,
        sessions_processed_events_min: 1,
        sessions_processed_events_max: 1,
        generation_batches: EXPECTED_GENERATION_REQUESTS,
        generation_batches_skipped: 0,
        generation_candidates: EXPECTED_GENERATION_REQUESTS,
        filter_dropped: 0,
        save_new: EXPECTED_GENERATION_REQUESTS,
        save_updated: 0,
      },
      { stats }
    );

    const historyOpts = getRecentHistory.firstCall.args[0];
    Assert.equal(
      historyOpts.sinceMicros,
      undefined,
      "A full-lookup run should leave sinceMicros undefined."
    );
    Assert.ok(
      Number.isFinite(historyOpts.days),
      "A full-lookup run should define days to look up."
    );

    Assert.equal(
      generationPrompts.length,
      EXPECTED_GENERATION_REQUESTS,
      `A ${SESSION_COUNT}-session backlog should be capped to ` +
        `${MAX_SESSIONS_FIRST_RUN} sessions, costing ` +
        `${EXPECTED_GENERATION_REQUESTS} generation requests instead of ` +
        `${UNCAPPED_GENERATION_REQUESTS}.`
    );

    assertRetainedNewestSessions(
      generationPrompts,
      SESSION_COUNT,
      MAX_SESSIONS_FIRST_RUN
    );

    Assert.deepEqual(
      persisted.map(memory => memory.memory_summary).sort(),
      [...keptSummaries].sort(),
      "Every memory generated from the retained sessions should be persisted."
    );

    Assert.ok(
      setWatermark.calledOnce,
      "The watermark should be advanced exactly once."
    );
    Assert.equal(
      setWatermark.firstCall.args[0],
      newestSessionEndMs(SESSION_COUNT, baseMicros),
      "The watermark should advance to the newest session end."
    );
  } finally {
    sandbox.restore();
    await MemoryStore.updateMeta({
      last_session_memory_ts: 0,
      last_history_memory_ts: 0,
      last_chat_memory_ts: 0,
    });
    await clearMemories();
  }
});

/**
 * Tests that a delta run enforces its own, tighter session cap.
 */
add_task(async function test_generateMemoriesFromSessions_caps_delta_run() {
  await clearMemories();

  const EXCESS_SESSIONS = 25;
  const SESSION_COUNT = MAX_SESSIONS_DELTA_RUN + EXCESS_SESSIONS;
  const BATCH_SIZE = 10;
  const EXPECTED_GENERATION_REQUESTS = Math.ceil(
    MAX_SESSIONS_DELTA_RUN / BATCH_SIZE
  );
  const UNCAPPED_GENERATION_REQUESTS = Math.ceil(SESSION_COUNT / BATCH_SIZE);
  const baseMicros = 1_700_000_000_000_000;

  // Any non-zero watermark makes this a delta run. Set just before the oldest
  // visit so the whole fixture is genuinely new work.
  const watermarkMs = Math.floor(baseMicros / 1000) - 1;
  await MemoryStore.updateMeta({
    last_session_memory_ts: watermarkMs,
    last_history_memory_ts: 0,
    last_chat_memory_ts: 0,
  });

  const sandbox = sinon.createSandbox();
  try {
    // Stubbing just the checks to make sure the scheduler runs
    sandbox
      .stub(MemoriesManager, "shouldEnableMemoriesFromSchedulers")
      .returns(true);
    const getRecentHistory = sandbox
      .stub(MemoriesManager, "_getRecentHistory")
      .resolves(buildSessionHistoryRows(SESSION_COUNT, baseMicros));
    sandbox.stub(MemoriesManager, "_getRecentChats").resolves([]);
    const setWatermark = sandbox.spy(
      MemoriesManager,
      "setLastSessionMemoryTimestamp"
    );

    Services.fog.testResetFOG();
    const stats = {};
    const { generationPrompts, keptSummaries, persisted } =
      await runSessionGenerationWithMockEngine({
        batchSize: BATCH_SIZE,
        maxBatchRetries: 1,
        initialMemoryGenerationRetryDelayMS: 1,
        stats,
      });

    // The sources are stubbed, so they report no counts.
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        is_first_run: false,
        sessions_built: SESSION_COUNT,
        sessions_skipped: 0,
        sessions_processed: MAX_SESSIONS_DELTA_RUN,
        sessions_processed_events: MAX_SESSIONS_DELTA_RUN,
        sessions_processed_events_min: 1,
        sessions_processed_events_max: 1,
        generation_batches: EXPECTED_GENERATION_REQUESTS,
        generation_batches_skipped: 0,
        generation_candidates: EXPECTED_GENERATION_REQUESTS,
        filter_dropped: 0,
        save_new: EXPECTED_GENERATION_REQUESTS,
        save_updated: 0,
      },
      { stats }
    );

    // Guards against the run silently taking the first-run path, which would
    // apply the wrong cap and make the count below pass for the wrong reason.
    Assert.equal(
      getRecentHistory.firstCall.args[0].sinceMicros,
      MemoriesManager.getSessionMemoryDeltaStartMs(watermarkMs) * 1000,
      "A set watermark should pull history from one ms past the watermark."
    );

    Assert.equal(
      generationPrompts.length,
      EXPECTED_GENERATION_REQUESTS,
      `A ${SESSION_COUNT}-session delta backlog should be capped to ` +
        `${MAX_SESSIONS_DELTA_RUN} sessions, costing ` +
        `${EXPECTED_GENERATION_REQUESTS} generation requests instead of ` +
        `${UNCAPPED_GENERATION_REQUESTS}.`
    );

    assertRetainedNewestSessions(
      generationPrompts,
      SESSION_COUNT,
      MAX_SESSIONS_DELTA_RUN
    );

    Assert.deepEqual(
      persisted.map(memory => memory.memory_summary).sort(),
      [...keptSummaries].sort(),
      "Every memory generated from the retained sessions should be persisted."
    );

    Assert.ok(
      setWatermark.calledOnce,
      "The watermark should be advanced exactly once."
    );
    Assert.equal(
      setWatermark.firstCall.args[0],
      newestSessionEndMs(SESSION_COUNT, baseMicros),
      "The watermark should advance past the whole delta window, including the " +
        "sessions the cap dropped."
    );
  } finally {
    sandbox.restore();
    await MemoryStore.updateMeta({
      last_session_memory_ts: 0,
      last_history_memory_ts: 0,
      last_chat_memory_ts: 0,
    });
    await clearMemories();
  }
});

/**
 * Tests the full merge pipeline end-to-end through MemoriesManager.mergeMemories().
 */
add_task(async function test_mergeMemories_persists_and_deletes() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });
  const games = await addMemory({
    memory_summary: "Plays online strategy games",
    reasoning: "Frequently visits strategy gaming sites",
  });
  const originalMemories = await MemoriesManager.getAllMemories();
  Assert.equal(
    originalMemories.length,
    3,
    "Should start with 3 memories seeded"
  );

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });

  // Check the prompt is rendered correctly
  const serializedArgs = JSON.stringify(turn.request.args);
  Assert.ok(
    serializedArgs.includes(
      "Every group must contain at least two statements."
    ),
    "The request is framed by the statement-grouping system message."
  );
  for (const { memory_summary, reasoning } of originalMemories) {
    Assert.ok(
      serializedArgs.includes(memory_summary),
      `The prompt includes the statement "${memory_summary}".`
    );
    Assert.ok(
      serializedArgs.includes(reasoning),
      `The prompt includes the reasoning for "${memory_summary}".`
    );
  }

  // Mock the response and check how the rest of the pipeline behaves
  turn.respond(
    JSON.stringify([
      {
        component_statements: [
          "Prefers dark roast coffee",
          "Buys espresso beans monthly",
        ],
        new_reasoning: "User consistently browses for and buys coffee.",
        new_statement: "Enjoys strong coffee varieties",
      },
    ])
  );

  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  const summaries = memories.map(memory => memory.memory_summary);

  Assert.equal(
    memories.length,
    2,
    "Two components merged into one, leaving the merged memory and the ungrouped one."
  );
  Assert.ok(
    summaries.includes("Enjoys strong coffee varieties"),
    "The merged memory was persisted."
  );
  Assert.ok(
    summaries.includes("Plays online strategy games"),
    "The ungrouped memory was left untouched."
  );

  Assert.equal(
    await getMemoryById(coffee.id),
    null,
    "The first component was hard-deleted."
  );
  Assert.equal(
    await getMemoryById(espresso.id),
    null,
    "The second component was hard-deleted."
  );
  Assert.ok(
    await getMemoryById(games.id),
    "The ungrouped memory still exists in the store."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory has the correct values for its attributes
 * sourced from its component memories.
 */
add_task(async function test_mergeMemories_field_aggregation() {
  await clearMemories();

  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
    sources: ["history"],
    source_ids: { history_source_ids: ["h1", "h2"] },
    tags: ["category:Food & Drink", "intent:Buy / Acquire"],
    keywords: ["coffee", "espresso"],
    created_at: 1000,
    updated_at: 4000,
    last_accessed: 5000,
    recent_accessed_counts: { 0: 1, 1: 0, 2: 2, 3: 0, 4: 0, 5: 0, 6: 0 },
    lifetime_accessed_count: 3,
    merge_count: 1,
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
    sources: ["conversation"],
    source_ids: {
      history_source_ids: ["h2"],
      conversation_source_ids: ["c1"],
    },
    tags: ["category:Food & Drink", "intent:Compare / Evaluate"],
    keywords: ["espresso", "beans"],
    created_at: 3000,
    updated_at: 2000,
    last_accessed: 2000,
    recent_accessed_counts: { 0: 0, 1: 3, 2: 1, 3: 0, 4: 0, 5: 0, 6: 0 },
    lifetime_accessed_count: 4,
    merge_count: 2,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    "Enjoys coffee and espresso"
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.equal(
    memories.length,
    1,
    "The two components merged into one memory."
  );
  const merged = memories[0];

  Assert.deepEqual(
    [...merged.sources].sort(),
    ["conversation", "history"],
    "sources are unioned across components."
  );
  Assert.deepEqual(
    [...merged.source_ids.history_source_ids].sort(),
    ["h1", "h2"],
    "history_source_ids are unioned and deduped."
  );
  Assert.deepEqual(
    merged.source_ids.conversation_source_ids,
    ["c1"],
    "conversation_source_ids are unioned."
  );
  Assert.deepEqual(
    [...merged.tags].sort(),
    [
      "category:Food & Drink",
      "intent:Buy / Acquire",
      "intent:Compare / Evaluate",
    ],
    "tags are unioned and deduped."
  );
  Assert.deepEqual(
    [...merged.keywords].sort(),
    ["beans", "coffee", "espresso"],
    "keywords are unioned and deduped."
  );

  Assert.equal(
    merged.created_at,
    1000,
    "created_at is the earliest component."
  );
  Assert.equal(
    merged.last_accessed,
    5000,
    "last_accessed is the latest component."
  );
  Assert.equal(
    merged.updated_at,
    4000,
    "updated_at is inherited from the latest component, so the merged memory keeps the components' maintenance clock."
  );
  Assert.deepEqual(
    merged.recent_accessed_counts,
    { 0: 1, 1: 3, 2: 3, 3: 0, 4: 0, 5: 0, 6: 0 },
    "recent_accessed_counts are summed per day."
  );
  Assert.equal(
    merged.lifetime_accessed_count,
    7,
    "lifetime_accessed_count sums the components (3 + 4)."
  );
  Assert.equal(
    merged.merge_count,
    4,
    "merge_count is the summed component merge_counts plus one (1 + 2 + 1)."
  );
  Assert.ok(
    Number.isFinite(merged.strength) && merged.strength > 0,
    "strength is persisted as a positive number."
  );
  Assert.equal(
    merged.strength,
    computeMemoryStrength(merged),
    "strength matches a fresh computation over the merged memory's fields."
  );
  Assert.ok(
    Number.isFinite(merged.frecency) && merged.frecency > 0,
    "frecency is persisted as a positive number."
  );
  Assert.equal(
    merged.frecency,
    computeMemoryFrecency(merged),
    "frecency matches a fresh computation over the summed recent_accessed_counts."
  );
  Assert.equal(
    merged.sensitivity_category,
    MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    "All not-sensitive components stay not-sensitive."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory is sensitive whenever any of its components is, so a
 * sensitive statement cannot be laundered by merging it with benign ones.
 */
add_task(async function test_mergeMemories_sensitivity_is_contagious() {
  await clearMemories();

  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  });
  await addMemory({
    memory_summary: "Researches private medical symptoms",
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Researches private medical symptoms"],
    "Researches coffee and health topics"
  );
  await runPromise;

  const [merged] = await MemoriesManager.getAllMemories();
  Assert.equal(
    merged.sensitivity_category,
    MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
    "A single sensitive component makes the merged memory sensitive."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that profile facts are excluded from merging.
 */
add_task(async function test_mergeMemories_excludes_profile_facts() {
  await clearMemories();

  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });
  const profileFact = await addMemory({
    memory_summary: "Drinks coffee every morning",
    reasoning: "Stated directly by the user",
    type: MEMORY_TYPE_PROFILE_FACT,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });

  const serializedArgs = JSON.stringify(turn.request.args);
  Assert.ok(
    !serializedArgs.includes(profileFact.memory_summary),
    "The profile fact is withheld from the merge prompt."
  );
  Assert.ok(
    !serializedArgs.includes(profileFact.reasoning),
    "The profile fact's reasoning is withheld from the merge prompt."
  );
  for (const summary of [
    "Prefers dark roast coffee",
    "Buys espresso beans monthly",
  ]) {
    Assert.ok(
      serializedArgs.includes(summary),
      `The prompt still includes the mergeable statement "${summary}".`
    );
  }

  // Name the profile fact as a component anyway: the filter, not the model, is
  // what has to keep it out of the merge.
  turn.respond(
    JSON.stringify([
      {
        component_statements: [
          "Prefers dark roast coffee",
          "Buys espresso beans monthly",
          profileFact.memory_summary,
        ],
        new_reasoning: "User consistently browses for and buys coffee.",
        new_statement: "Enjoys strong coffee varieties",
      },
    ])
  );
  await runPromise;

  Assert.ok(
    await getMemoryById(profileFact.id),
    "The profile fact survives the merge run."
  );

  const memories = await MemoriesManager.getAllMemories();
  Assert.equal(
    memories.length,
    2,
    "Only the merged memory and the untouched profile fact remain."
  );

  const merged = memories.find(
    memory => memory.memory_summary === "Enjoys strong coffee varieties"
  );
  Assert.ok(merged, "The two mergeable memories still merged.");
  Assert.deepEqual(
    sortComponentSummaries(merged.component_summaries).map(
      component => component.memory_summary
    ),
    ["Buys espresso beans monthly", "Prefers dark roast coffee"],
    "The profile fact is not recorded as a component of the merged memory."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that a store holding nothing but profile facts is not sent to the model
 * at all, since filtering leaves no candidates to group.
 */
add_task(async function test_mergeMemories_skipsRunWithOnlyProfileFacts() {
  await clearMemories();

  const first = await addMemory({
    memory_summary: "Drinks coffee every morning",
    type: MEMORY_TYPE_PROFILE_FACT,
  });
  const second = await addMemory({
    memory_summary: "Lives in a cold climate",
    type: MEMORY_TYPE_PROFILE_FACT,
  });

  // No captureRequest here: with the count gate disabled, this resolves only if
  // the profile-fact filter leaves nothing mergeable behind.
  await MemoriesManager.mergeMemories(MERGE_ALL);

  Assert.ok(await getMemoryById(first.id), "The first profile fact survives.");
  Assert.ok(
    await getMemoryById(second.id),
    "The second profile fact survives."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests both sides of the default merge count gate: a store holding exactly
 * MEMORY_MERGE_MIN_MEMORY_COUNT mergeable memories never reaches the model,
 * and one more than that does.
 */
add_task(async function test_mergeMemories_respectsMinMemoryCount() {
  await clearMemories();

  for (let i = 0; i < MEMORY_MERGE_MIN_MEMORY_COUNT; i++) {
    await addMemory({ memory_summary: `Gated memory ${i}` });
  }

  // No captureRequest: at the threshold the run must return before the model.
  await MemoriesManager.mergeMemories();

  Assert.equal(
    (await MemoriesManager.getAllMemories()).length,
    MEMORY_MERGE_MIN_MEMORY_COUNT,
    "At the threshold nothing is merged and every memory survives."
  );
  gMockEngineManager.assertAllRequestsHandled();

  // One past the threshold, the same call does reach the model.
  await addMemory({ memory_summary: "Gated memory over the line" });

  const runPromise = MemoriesManager.mergeMemories();
  await respondToMergeRequest(
    ["Gated memory 0", "Gated memory 1"],
    "Merged gated memories"
  );
  await runPromise;

  const summaries = (await MemoriesManager.getAllMemories()).map(
    memory => memory.memory_summary
  );
  Assert.ok(
    summaries.includes("Merged gated memories"),
    "Exceeding the threshold lets the merge pass run."
  );
  Assert.ok(
    !summaries.includes("Gated memory 0"),
    "The merged components were deleted."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that merging durable memories yields a memory that is itself durable
 * based on inherited attributes.
 */
add_task(async function test_mergeMemories_retains_inherited_type() {
  await clearMemories();

  // Each component is old enough and used enough to earn the durable tier on
  // its own, so the merge inherits everything that tier requires.
  const createdAt = Date.now() - 60 * 24 * 60 * 60 * 1000;
  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    type: MEMORY_TYPE_DURABLE_MEMORY,
    created_at: createdAt,
    ...STRONG_MEMORY_FIELDS,
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    type: MEMORY_TYPE_DURABLE_MEMORY,
    created_at: createdAt,
    ...STRONG_MEMORY_FIELDS,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    "Enjoys coffee and espresso"
  );
  await runPromise;

  const [merged] = await MemoriesManager.getAllMemories();
  Assert.equal(
    merged.memory_summary,
    "Enjoys coffee and espresso",
    "The two components merged into one memory."
  );
  Assert.equal(
    merged.type,
    MEMORY_TYPE_DURABLE_MEMORY,
    "Merging two durable memories should never produce a weaker type."
  );
  Assert.greater(
    merged.strength,
    MEMORY_TYPE_LONG_TERM_TO_DURABLE_STRENGTH_THRESHOLD,
    "The merged memory clears the durable strength threshold on its own."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that a merged memory is promoted when the summed usage of its
 * components makes it strong enough, and old enough, for a higher tier.
 */
add_task(async function test_mergeMemories_promotes_from_merged_strength() {
  await clearMemories();

  // Old enough for every tier, and heavily used, so the summed usage clears the
  // durable strength threshold.
  const createdAt = Date.now() - 100 * 24 * 60 * 60 * 1000;
  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    created_at: createdAt,
    lifetime_accessed_count: 50,
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    created_at: createdAt,
    lifetime_accessed_count: 50,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    "Enjoys coffee and espresso"
  );
  await runPromise;

  const [merged] = await MemoriesManager.getAllMemories();
  Assert.equal(
    merged.type,
    MEMORY_TYPE_DURABLE_MEMORY,
    "A merged memory strong and old enough for the durable tier should be promoted into it."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that merging memories that have never been used leaves last_accessed null.
 */
add_task(async function test_mergeMemories_neverUsedComponents() {
  await clearMemories();

  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    last_accessed: null,
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    last_accessed: null,
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    "Enjoys coffee and espresso"
  );
  await runPromise;

  const [merged] = await MemoriesManager.getAllMemories();
  Assert.strictEqual(
    merged.last_accessed,
    null,
    "Merging never-used memories should leave last_accessed null."
  );

  await MemoriesManager.runMemoryMaintenance();
  Assert.ok(
    await getMemoryById(merged.id),
    "The merged memory should survive the next maintenance run."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests multipile merged memories are created when the LLM returns multiple.
 */
add_task(async function test_mergeMemories_multiple_groups() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });
  const strategy = await addMemory({
    memory_summary: "Plays online strategy games",
    reasoning: "Frequently visits strategy gaming sites",
  });
  const puzzle = await addMemory({
    memory_summary: "Solves daily puzzle games",
    reasoning: "Opens a puzzle game site every morning",
  });
  const originalMemories = await MemoriesManager.getAllMemories();
  Assert.equal(
    originalMemories.length,
    4,
    "Should start with 4 seeded memories"
  );

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
  turn.respond(
    JSON.stringify([
      {
        component_statements: [
          "Prefers dark roast coffee",
          "Buys espresso beans monthly",
        ],
        new_reasoning: "User browses for and buys coffee.",
        new_statement: "Enjoys strong coffee varieties",
      },
      {
        component_statements: [
          "Plays online strategy games",
          "Solves daily puzzle games",
        ],
        new_reasoning: "User regularly plays a variety of games.",
        new_statement: "Plays strategy and puzzle games",
      },
    ])
  );

  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  const summaries = memories.map(memory => memory.memory_summary).sort();

  Assert.deepEqual(
    summaries,
    ["Enjoys strong coffee varieties", "Plays strategy and puzzle games"],
    "Each group produced its own merged memory."
  );

  for (const component of [coffee, espresso, strategy, puzzle]) {
    Assert.equal(
      await getMemoryById(component.id),
      null,
      `Component "${component.memory_summary}" was hard-deleted.`
    );
  }

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests attributes are accumulated when merged memories from a prior run are merged again.
 */
add_task(async function test_mergeMemories_component_summaries_accumulation() {
  await clearMemories();
  const now = Date.now();

  await addMemory({
    created_at: now,
    memory_summary: "Enjoys strong coffee varieties",
    reasoning: "Previously merged from coffee statements",
    component_summaries: [
      {
        memory_summary: "Prefers dark roast coffee",
        reasoning: "Repeatedly browses dark roast coffee products",
      },
      {
        memory_summary: "Enjoys hot drinks",
        reasoning: "Browses coffee and tea together",
      },
    ],
    sources: ["history"],
    tags: ["category:Food & Drink", "intent:Buy / Acquire"],
    keywords: ["coffee", "espresso"],
  });
  await addMemory({
    created_at: now - 1000,
    memory_summary: "Plays strategy and puzzle games",
    reasoning: "Previously merged from gaming statements",
    // "Enjoys hot drinks" overlaps the first component to exercise dedup, with a
    // different reasoning so the winning entry is identifiable.
    component_summaries: [
      {
        memory_summary: "Plays online strategy games",
        reasoning: "Frequently visits strategy gaming sites",
      },
      {
        memory_summary: "Enjoys hot drinks",
        reasoning: "Reads about cocoa recipes while gaming",
      },
    ],
    sources: ["history", "conversation"],
    tags: ["category:Games", "intent:Buy / Acquire"],
    keywords: ["strategy", "coffee"],
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Enjoys strong coffee varieties", "Plays strategy and puzzle games"],
    "Enjoys coffee and games"
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.equal(
    memories.length,
    1,
    "The two components merged into one memory."
  );
  const merged = memories[0];

  Assert.deepEqual(
    sortComponentSummaries(merged.component_summaries),
    [
      // The second component's entry wins the dedup on this summary.
      {
        memory_summary: "Enjoys hot drinks",
        reasoning: "Reads about cocoa recipes while gaming",
      },
      {
        memory_summary: "Enjoys strong coffee varieties",
        reasoning: "Previously merged from coffee statements",
      },
      {
        memory_summary: "Plays online strategy games",
        reasoning: "Frequently visits strategy gaming sites",
      },
      {
        memory_summary: "Plays strategy and puzzle games",
        reasoning: "Previously merged from gaming statements",
      },
      {
        memory_summary: "Prefers dark roast coffee",
        reasoning: "Repeatedly browses dark roast coffee products",
      },
    ],
    "component_summaries is the deduped union of prior histories plus the components' own summaries and reasonings."
  );
  Assert.deepEqual(
    [...merged.sources].sort(),
    ["conversation", "history"],
    "sources is the deduped union across components."
  );
  Assert.deepEqual(
    [...merged.tags].sort(),
    ["category:Food & Drink", "category:Games", "intent:Buy / Acquire"],
    "tags is the deduped union across components."
  );
  Assert.deepEqual(
    [...merged.keywords].sort(),
    ["coffee", "espresso", "strategy"],
    "keywords is the deduped union across components."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests MemoryStore is left as-is when the LLM doesn't return any groups.
 */
add_task(async function test_mergeMemories_noop_when_no_candidates() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const games = await addMemory({
    memory_summary: "Plays online strategy games",
    reasoning: "Frequently visits strategy gaming sites",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
  turn.respond(JSON.stringify([]));

  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Plays online strategy games", "Prefers dark roast coffee"],
    "Both memories remain and no merged memory was added."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first memory was left in place."
  );
  Assert.ok(
    await getMemoryById(games.id),
    "The second memory was left in place."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests no merged memory is created when an LLM-returned group contains hallucinated memory summaries.
 */
add_task(async function test_mergeMemories_unknown_component_statement() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const games = await addMemory({
    memory_summary: "Plays online strategy games",
    reasoning: "Frequently visits strategy gaming sites",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    [
      "A statement that was never stored",
      "Another statement that was never stored",
    ],
    "Phantom merged memory"
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Plays online strategy games", "Prefers dark roast coffee"],
    "No phantom memory was created for the unmatched group."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first memory was left in place."
  );
  Assert.ok(
    await getMemoryById(games.id),
    "The second memory was left in place."
  );
  for (const memory of memories) {
    Assert.ok(
      Number.isFinite(memory.created_at) &&
        Number.isFinite(memory.last_accessed),
      `Memory "${memory.memory_summary}" has finite timestamps.`
    );
  }

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory is created when an LLM-returned group contains at least 2 real memory summaries.
 * Also tests that the hallucinated one is skipped.
 */
add_task(async function test_mergeMemories_partial_group_merges_matching() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    [
      "Prefers dark roast coffee",
      "Buys espresso beans monthly",
      "A statement that was never stored",
    ],
    "Enjoys strong coffee varieties"
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary),
    ["Enjoys strong coffee varieties"],
    "The two matched components merged; the phantom statement was ignored."
  );
  Assert.equal(
    await getMemoryById(coffee.id),
    null,
    "The first matched component was hard-deleted."
  );
  Assert.equal(
    await getMemoryById(espresso.id),
    null,
    "The second matched component was hard-deleted."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests that an LLM-returned group with only 1 memory summary is skipped.
 * Merged memories must have at least 2.
 */
add_task(async function test_mergeMemories_single_matching_component_skipped() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "A statement that was never stored"],
    "Enjoys strong coffee varieties"
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary),
    ["Prefers dark roast coffee"],
    "No unified memory was created from a single matched component."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The lone matched memory was left in place, not renamed or deleted."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged statement longer than MAX_MEMORY_SUMMARY_LENGTH is rejected
 * outright rather than truncated, leaving its components in place.
 */
add_task(async function test_mergeMemories_rejects_oversized_statement() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });

  const oversized = "a".repeat(MAX_MEMORY_SUMMARY_LENGTH + 1);

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    oversized
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Buys espresso beans monthly", "Prefers dark roast coffee"],
    "No merged memory was created from the oversized statement."
  );
  Assert.ok(
    memories.every(
      memory => memory.memory_summary.length <= MAX_MEMORY_SUMMARY_LENGTH
    ),
    "No stored summary exceeds the max length."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first component survived the rejected merge."
  );
  Assert.ok(
    await getMemoryById(espresso.id),
    "The second component survived the rejected merge."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged statement at exactly MAX_MEMORY_SUMMARY_LENGTH is accepted.
 */
add_task(async function test_mergeMemories_accepts_max_length_statement() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });

  const atLimit = "a".repeat(MAX_MEMORY_SUMMARY_LENGTH);

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    atLimit
  );
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary),
    [atLimit],
    "A statement exactly at the limit was merged and persisted."
  );
  Assert.equal(
    await getMemoryById(coffee.id),
    null,
    "The first component was hard-deleted."
  );
  Assert.equal(
    await getMemoryById(espresso.id),
    null,
    "The second component was hard-deleted."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory with an empty statement is rejected, leaving its
 * components in place.
 */
add_task(async function test_mergeMemories_rejects_empty_statement() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });

  Services.fog.testResetFOG();
  const stats = {};
  const runPromise = MemoriesManager.mergeMemories({ ...MERGE_ALL, stats });
  await respondToMergeRequest(
    ["Prefers dark roast coffee", "Buys espresso beans monthly"],
    ""
  );
  await runPromise;

  // The proposed group was invalid, so nothing was saved or removed.
  assertGenerationRunEvent(
    Glean.smartWindow.memoriesGenerationRun,
    {
      merge_status: "ran",
      merge_groups_proposed: 1,
      merge_groups_saved: 0,
      merge_components_removed: 0,
    },
    { stats }
  );

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Buys espresso beans monthly", "Prefers dark roast coffee"],
    "No merged memory was created from the empty statement."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first component survived the rejected merge."
  );
  Assert.ok(
    await getMemoryById(espresso.id),
    "The second component survived the rejected merge."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory with empty reasoning is rejected, leaving its
 * components in place.
 */
add_task(async function test_mergeMemories_rejects_empty_reasoning() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const espresso = await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
  turn.respond(
    JSON.stringify([
      {
        component_statements: [
          "Prefers dark roast coffee",
          "Buys espresso beans monthly",
        ],
        new_reasoning: "",
        new_statement: "Enjoys strong coffee varieties",
      },
    ])
  );

  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Buys espresso beans monthly", "Prefers dark roast coffee"],
    "No merged memory was created from the group with empty reasoning."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first component survived the rejected merge."
  );
  Assert.ok(
    await getMemoryById(espresso.id),
    "The second component survived the rejected merge."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests merge candidates without a list of component statements are skipped
 * without derailing the rest of the response.
 */
add_task(
  async function test_mergeMemories_rejects_invalid_component_statements() {
    await clearMemories();

    const coffee = await addMemory({
      memory_summary: "Prefers dark roast coffee",
      reasoning: "Repeatedly browses dark roast coffee products",
    });
    const espresso = await addMemory({
      memory_summary: "Buys espresso beans monthly",
      reasoning: "Places recurring orders for espresso beans",
    });
    const greenTea = await addMemory({
      memory_summary: "Drinks green tea at night",
      reasoning: "Browses green tea recipes in the evening",
    });
    const matcha = await addMemory({
      memory_summary: "Orders matcha powder in bulk",
      reasoning: "Places recurring orders for matcha powder",
    });

    const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

    const turn = await gMockEngineManager.captureRequest({
      purpose: PURPOSES.MEMORY_GENERATION,
    });
    turn.respond(
      JSON.stringify([
        {
          new_reasoning: "User browses for and buys coffee.",
          new_statement: "Enjoys strong coffee varieties",
        },
        {
          component_statements: "Prefers dark roast coffee",
          new_reasoning: "User browses for and buys coffee.",
          new_statement: "Enjoys coffee",
        },
        {
          component_statements: [
            "Drinks green tea at night",
            "Orders matcha powder in bulk",
          ],
          new_reasoning: "User drinks and buys green tea.",
          new_statement: "Enjoys green tea varieties",
        },
      ])
    );

    await runPromise;

    const memories = await MemoriesManager.getAllMemories();
    Assert.deepEqual(
      memories.map(memory => memory.memory_summary).sort(),
      [
        "Buys espresso beans monthly",
        "Enjoys green tea varieties",
        "Prefers dark roast coffee",
      ],
      "Only the group with a component statements array was merged."
    );
    for (const component of [coffee, espresso]) {
      Assert.ok(
        await getMemoryById(component.id),
        `Component "${component.memory_summary}" survived the rejected merges.`
      );
    }
    for (const component of [greenTea, matcha]) {
      Assert.equal(
        await getMemoryById(component.id),
        null,
        `Component "${component.memory_summary}" was hard-deleted by the valid merge.`
      );
    }

    gMockEngineManager.assertAllRequestsHandled();
  }
);

/**
 * Tests mergeMemories() can handle invalid LLM output.
 */
add_task(async function test_mergeMemories_unparseable_output() {
  await clearMemories();

  const coffee = await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  const games = await addMemory({
    memory_summary: "Plays online strategy games",
    reasoning: "Frequently visits strategy gaming sites",
  });

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
  turn.respond("Sorry, I could not group these statements into JSON.");

  // Resolves rather than rejecting on the unparseable response.
  await runPromise;

  const memories = await MemoriesManager.getAllMemories();
  Assert.deepEqual(
    memories.map(memory => memory.memory_summary).sort(),
    ["Plays online strategy games", "Prefers dark roast coffee"],
    "Both memories remain and nothing was merged."
  );
  Assert.ok(
    await getMemoryById(coffee.id),
    "The first memory was left in place."
  );
  Assert.ok(
    await getMemoryById(games.id),
    "The second memory was left in place."
  );

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests soft-deleted memories are not sent the model as merge options.
 */
add_task(async function test_mergeMemories_excludes_soft_deleted() {
  await clearMemories();

  await addMemory({
    memory_summary: "Prefers dark roast coffee",
    reasoning: "Repeatedly browses dark roast coffee products",
  });
  await addMemory({
    memory_summary: "Buys espresso beans monthly",
    reasoning: "Places recurring orders for espresso beans",
  });
  const deleted = await addMemory({
    memory_summary: "Researches private medical symptoms",
    reasoning: "Soft-deleted and must not reach the model",
  });
  await MemoriesManager.softDeleteMemoryById(deleted.id);

  const runPromise = MemoriesManager.mergeMemories(MERGE_ALL);

  const turn = await gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });

  const serializedArgs = JSON.stringify(turn.request.args);
  Assert.ok(
    serializedArgs.includes("Prefers dark roast coffee") &&
      serializedArgs.includes("Buys espresso beans monthly"),
    "The live memories are included in the prompt."
  );
  Assert.ok(
    !serializedArgs.includes("Researches private medical symptoms"),
    "The soft-deleted memory is excluded from the prompt."
  );

  // Respond with no groups; the prompt contents are what this test checks.
  turn.respond(JSON.stringify([]));
  await runPromise;

  gMockEngineManager.assertAllRequestsHandled();
});

/**
 * Tests a merged memory with the same summary as an existing memory is not deleted.
 */
add_task(
  async function test_mergeMemories_new_statement_collides_with_component() {
    await clearMemories();

    await addMemory({
      memory_summary: "Prefers dark roast coffee",
      reasoning: "Repeatedly browses dark roast coffee products",
    });
    await addMemory({
      memory_summary: "Buys espresso beans monthly",
      reasoning: "Places recurring orders for espresso beans",
    });

    Services.fog.testResetFOG();
    const stats = {};
    const runPromise = MemoriesManager.mergeMemories({ ...MERGE_ALL, stats });
    await respondToMergeRequest(
      ["Prefers dark roast coffee", "Buys espresso beans monthly"],
      "Prefers dark roast coffee"
    );
    await runPromise;

    // Saving the merged memory updated the matching component, so only the
    // other component was removed.
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        merge_status: "ran",
        merge_groups_proposed: 1,
        merge_groups_saved: 1,
        merge_components_removed: 1,
      },
      { stats }
    );

    const memories = await MemoriesManager.getAllMemories();
    Assert.equal(
      memories.length,
      1,
      "The merged memory survives even though its summary matches a component."
    );
    const [merged] = memories;
    Assert.equal(
      merged.memory_summary,
      "Prefers dark roast coffee",
      "The surviving memory carries the merged summary."
    );
    Assert.equal(
      merged.merge_count,
      1,
      "The surviving memory is the merged result, not the untouched original."
    );
    Assert.deepEqual(
      sortComponentSummaries(merged.component_summaries),
      [
        {
          memory_summary: "Buys espresso beans monthly",
          reasoning: "Places recurring orders for espresso beans",
        },
        {
          memory_summary: "Prefers dark roast coffee",
          reasoning: "Repeatedly browses dark roast coffee products",
        },
      ],
      "The merged memory records both component statements with their reasonings."
    );

    gMockEngineManager.assertAllRequestsHandled();
  }
);
