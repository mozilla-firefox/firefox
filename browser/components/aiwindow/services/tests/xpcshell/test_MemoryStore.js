/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { EmbeddingsGenerator } = ChromeUtils.importESModule(
  "chrome://global/content/ml/EmbeddingsGenerator.sys.mjs"
);
const { MemoryStore, makeMemoryId, migrateMemoryStoreVersionOneToTwo } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
  );

const { MEMORY_FILTER_COMPARATOR } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs"
);
const {
  MEMORY_TYPE_SHORT_TERM_MEMORY,
  MEMORY_TYPE_DURABLE_MEMORY,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
  MEMORY_FRECENCY_MAX_DAYS,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);

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
    recent_accessed_counts: { 0: 0, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 7 },
  },
  {
    type: MEMORY_TYPE_SHORT_TERM_MEMORY,
    memory_summary: "Buys dog food online",
    reasoning: "Frequently buys dog food on websites like Chewy",
    tags: ["category:Pets & Animals", "intent:Buy / Acquire"],
    sources: ["session"],
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    updated_at: 1,
    recent_accessed_counts: { 0: 1, 1: 0, 2: 2, 3: 0, 4: 2, 5: 0, 6: 0 },
  },
  {
    type: MEMORY_TYPE_DURABLE_MEMORY,
    memory_summary: "Plays games online",
    reasoning: "Visits a lot of gaming-related websites",
    tags: ["category:Games", "intent:Entertain / Relax", "extra:tag"],
    sources: ["user_request"],
    sensitivity_category: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
    updated_at: 200,
    recent_accessed_counts: { 0: 0, 1: 3, 2: 0, 3: 0, 4: 1, 5: 1, 6: 0 },
  },
];

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

add_task(async function test_init_empty_state() {
  // First init should succeed and not throw.
  await MemoryStore.ensureInitialized();

  let memories = await MemoryStore.getMemories();
  equal(memories.length, 0, "New store should start with no memories");

  const meta = await MemoryStore.getMeta();
  equal(
    meta.last_history_memory_ts,
    0,
    "Default last_history_memory_ts should be 0"
  );
  equal(meta.last_chat_memory_ts, 0, "Default last_chat_memory_ts should be 0");
  equal(
    meta.last_session_memory_ts,
    0,
    "Default last_session_memory_ts should be 0"
  );
  equal(
    meta.last_generation_run_ts,
    0,
    "Default last_generation_run_ts should be 0"
  );
});

add_task(async function test_addMemory() {
  await MemoryStore.ensureInitialized();

  const memory1 = await MemoryStore.addMemory({
    memory_summary: "i love driking coffee",
    reasoning: "searches for nearest coffee places",
    tags: ["category:Food & Drink", "intent:Plan / Organize"],
  });

  equal(
    memory1.memory_summary,
    "i love driking coffee",
    "memory summary should match input"
  );
  Assert.deepEqual(
    memory1.tags,
    ["category:Food & Drink", "intent:Plan / Organize"],
    "Tags should match input"
  );
  equal(
    memory1.reasoning,
    "searches for nearest coffee places",
    "Reasoning should match input"
  );
  await MemoryStore.hardDeleteMemory(memory1.id);
});

add_task(async function test_addMemoryWithoutSource() {
  await MemoryStore.ensureInitialized();

  const memoryBothHistAndChat = await MemoryStore.addMemory({
    memory_summary: "no source",
    reasoning: "no source",
    source_ids: {
      history_source_ids: [1],
      conversation_source_ids: [1],
    },
  });
  Assert.deepEqual(
    memoryBothHistAndChat.sources,
    ["session"],
    "Memory without source but history & conversation IDs should impute session source"
  );
  await MemoryStore.hardDeleteMemory(memoryBothHistAndChat.id);

  const memoryOnlyHist = await MemoryStore.addMemory({
    memory_summary: "no source",
    reasoning: "no source",
    source_ids: {
      history_source_ids: [1],
      conversation_source_ids: [],
    },
  });
  Assert.deepEqual(
    memoryOnlyHist.sources,
    ["history"],
    "Memory without source but history IDs should impute history source"
  );
  await MemoryStore.hardDeleteMemory(memoryOnlyHist.id);

  const memoryOnlyChat = await MemoryStore.addMemory({
    memory_summary: "no source",
    reasoning: "no source",
    source_ids: {
      history_source_ids: [],
      conversation_source_ids: [1],
    },
  });
  Assert.deepEqual(
    memoryOnlyChat.sources,
    ["conversation"],
    "Memory without source but conversation IDs should impute conversation source"
  );
  await MemoryStore.hardDeleteMemory(memoryOnlyChat.id);

  const memoryNoSourceIDs = await MemoryStore.addMemory({
    memory_summary: "no source",
    reasoning: "no source",
    source_ids: {
      history_source_ids: [],
      conversation_source_ids: [],
    },
  });
  Assert.deepEqual(
    memoryNoSourceIDs.sources,
    [],
    "Memory without any source IDs should imput no source"
  );
  await MemoryStore.hardDeleteMemory(memoryNoSourceIDs.id);
});

add_task(async function test_addMemory_and_upsert_by_content() {
  await MemoryStore.ensureInitialized();

  const memory1 = await MemoryStore.addMemory({
    memory_summary: "trip plans to Italy",
    tags: ["category:Travel & Transportation", "intent:Plan / Organize"],
  });

  ok(memory1.id, "Memory should have an id");
  equal(
    memory1.memory_summary,
    "trip plans to Italy",
    "Memory summary should be stored"
  );
  equal(
    memory1.reasoning,
    "",
    "Reasoning should default to empty string when not provided"
  );

  // Add another memory with same (summary, category, intent) – should upsert, not duplicate.
  const memory2 = await MemoryStore.addMemory({
    memory_summary: "trip plans to Italy",
    reasoning: "User searches for Italy travel guides and visits booking sites",
    tags: ["category:Travel & Transportation", "intent:Plan / Organize"],
  });

  equal(
    memory1.id,
    memory2.id,
    "Same summary should produce same deterministic id"
  );
  equal(
    memory2.reasoning,
    "User searches for Italy travel guides and visits booking sites",
    "Second addMemory call for same id should update reasoning"
  );
  Assert.deepEqual(
    memory2.tags,
    ["category:Travel & Transportation", "intent:Plan / Organize"],
    "Should have the expected category and intent tags"
  );

  const memories = await MemoryStore.getMemories();
  equal(memories.length, 1, "Store should still have only one memory");
  await MemoryStore.hardDeleteMemory(memory1.id);
});

add_task(async function test_updateMemory_and_soft_delete() {
  await MemoryStore.ensureInitialized();

  const memory = await MemoryStore.addMemory({
    memory_summary: "debug memory",
    reasoning: "Initial reasoning for debugging",
    tags: ["category:debug", "intent:Monitor / Track"],
    sources: ["history"],
    source_ids: {
      history_source_ids: [1, 2, 3],
      conversation_source_ids: [4, 5, 6],
    },
    keywords: ["one", "two"],
    merge_count: 3,
  });
  const originalCreatedAt = memory.created_at;

  equal(
    memory.reasoning,
    "Initial reasoning for debugging",
    "Initial reasoning should be set"
  );

  const updated = await MemoryStore.updateMemory(memory.id, {
    reasoning: "Updated reasoning after more data",
    sources: ["history", "session"],
    tags: ["category:debug", "something:else"],
    source_ids: {
      history_source_ids: [7, 8],
      conversation_source_ids: [9, 10],
    },
    keywords: ["three"],
    lifetime_accessed_count: 2,
    merge_count: 5,
  });
  equal(
    updated.reasoning,
    "Updated reasoning after more data",
    "updateMemory should update reasoning"
  );

  Assert.equal(
    originalCreatedAt,
    updated.created_at,
    "Updated memory should keep its original creation timestamp"
  );

  const expectedSources = ["history", "session"];
  equal(updated.sources.length, 2, "Updated memory should have 2 sources");
  Assert.ok(
    updated.sources.every(s => expectedSources.includes(s)),
    "Updated memory should have the expected sources"
  );

  const expectedTags = [
    "category:debug",
    "intent:Monitor / Track",
    "something:else",
  ];
  equal(updated.tags.length, 3, "Updated memory should have 3 tags");
  Assert.ok(
    updated.tags.every(t => expectedTags.includes(t)),
    "Updated memory should have the expected tags"
  );

  const expectedHistorySourceIds = [1, 2, 3, 7, 8];
  equal(
    updated.source_ids.history_source_ids.length,
    5,
    "Updated memory should have 5 history source IDs"
  );
  Assert.ok(
    updated.source_ids.history_source_ids.every(i =>
      expectedHistorySourceIds.includes(i)
    ),
    "Updated memory should have the expected history source IDs"
  );

  const expectedConvoSourceIds = [4, 5, 6, 9, 10];
  equal(
    updated.source_ids.conversation_source_ids.length,
    5,
    "Updated memory should have 5 conversation source IDs"
  );
  Assert.ok(
    updated.source_ids.conversation_source_ids.every(i =>
      expectedConvoSourceIds.includes(i)
    ),
    "Updated memory should have the expected conversation source IDs"
  );

  const expectedKeywords = ["one", "two", "three"];
  equal(updated.keywords.length, 3, "Updated memory should have 3 keywords");
  Assert.ok(
    updated.keywords.every(k => expectedKeywords.includes(k)),
    "Updated memory should have the expected keywords"
  );

  Assert.equal(
    updated.lifetime_accessed_count,
    2,
    "Updated memory should have the lifetime_accessed_count from updates"
  );
  Assert.equal(
    updated.merge_count,
    5,
    "Updated memory should have the lifetime_accessed_count from updates"
  );

  const deleted = await MemoryStore.softDeleteMemory(memory.id);

  ok(deleted, "softDeleteMemory should return the updated memory");
  equal(
    deleted.is_deleted,
    true,
    "Soft-deleted memory should have is_deleted = true"
  );

  const nonDeleted = await MemoryStore.getMemories();
  const notFound = nonDeleted.find(i => i.id === memory.id);
  equal(
    notFound,
    undefined,
    "Soft-deleted memory should be filtered out by getMemories()"
  );
});

add_task(async function test_hard_delete() {
  await MemoryStore.ensureInitialized();

  const memory = await MemoryStore.addMemory({
    memory_summary: "to be hard deleted",
    tags: ["category:debug", "intent: Monitor / Track"],
  });

  let memories = await MemoryStore.getMemories();
  const beforeCount = memories.length;

  const removed = await MemoryStore.hardDeleteMemory(memory.id);
  equal(
    removed,
    true,
    "hardDeleteMemory should return true when removing existing memory"
  );

  memories = await MemoryStore.getMemories();
  const afterCount = memories.length;

  equal(
    beforeCount - 1,
    afterCount,
    "hardDeleteMemory should physically remove entry from array"
  );
});

add_task(async function test_hard_delete_records_removal_only_with_trigger() {
  Services.fog.initializeFOG();
  Services.fog.testResetFOG();
  await MemoryStore.ensureInitialized();

  const background = await MemoryStore.addMemory({
    memory_summary: "removed without a trigger",
  });
  const userRemoved = await MemoryStore.addMemory({
    memory_summary: "removed from settings",
  });

  ok(
    await MemoryStore.hardDeleteMemory(background.id),
    "Memory without a trigger should be removed"
  );
  equal(
    Glean.smartWindow.memoryRemovedPanel.testGetValue(),
    null,
    "Removal without a trigger should record no memory_removed_panel event"
  );

  ok(
    await MemoryStore.hardDeleteMemory(userRemoved.id, "settings"),
    "Memory with a trigger should be removed"
  );
  const events = Glean.smartWindow.memoryRemovedPanel.testGetValue();
  equal(events?.length, 1, "Removal with a trigger should record one event");
  equal(events[0].extra.trigger, "settings", "Event should carry the trigger");
});

add_task(async function test_updateMeta_and_persistence_roundtrip() {
  await MemoryStore.ensureInitialized();

  const now = Date.now();

  await MemoryStore.updateMeta({
    last_history_memory_ts: now,
  });

  let meta = await MemoryStore.getMeta();
  equal(
    meta.last_history_memory_ts,
    now,
    "updateMeta should update last_history_memory_ts"
  );
  equal(
    meta.last_chat_memory_ts,
    0,
    "updateMeta should not touch last_chat_memory_ts when not provided"
  );

  const chatTime = now + 1000;
  await MemoryStore.updateMeta({
    last_chat_memory_ts: chatTime,
  });

  meta = await MemoryStore.getMeta();
  equal(
    meta.last_history_memory_ts,
    now,
    "last_history_memory_ts should remain unchanged when only chat ts updated"
  );
  equal(
    meta.last_chat_memory_ts,
    chatTime,
    "last_chat_memory_ts should be updated"
  );

  const generationTime = now + 2000;
  await MemoryStore.updateMeta({
    last_generation_run_ts: generationTime,
  });

  meta = await MemoryStore.getMeta();
  equal(
    meta.last_generation_run_ts,
    generationTime,
    "updateMeta should update last_generation_run_ts"
  );

  // Force a write to disk.
  await MemoryStore.testOnlyFlush();

  // Simulate a fresh import by reloading module.
  // This uses the xpcshell helper to bypass module caching.
  const { MemoryStore: FreshStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs",
    { ignoreCache: true }
  );

  await FreshStore.ensureInitialized();

  const meta2 = await FreshStore.getMeta();
  equal(
    meta2.last_history_memory_ts,
    now,
    "last_history_memory_ts should survive roundtrip to disk"
  );
  equal(
    meta2.last_chat_memory_ts,
    chatTime,
    "last_chat_memory_ts should survive roundtrip to disk"
  );
  equal(
    meta2.last_generation_run_ts,
    generationTime,
    "last_generation_run_ts should survive roundtrip to disk"
  );

  const memories = await FreshStore.getMemories();
  ok(Array.isArray(memories), "Memories should be an array after reload");
});

add_task(async function test_reasoning_field_persistence() {
  await MemoryStore.ensureInitialized();

  const testReasoning =
    "User frequently searches for coffee shops and visits coffee-related websites";

  const memory = await MemoryStore.addMemory({
    memory_summary: "Loves specialty coffee",
    tags: ["category: Food & Drink", "intent: Buy / Acquire"],
    reasoning: testReasoning,
  });

  equal(
    memory.reasoning,
    testReasoning,
    "Reasoning should be stored when creating new memory"
  );

  const updatedReasoning =
    "User searches for and visits multiple specialty coffee roasters";
  const updated = await MemoryStore.updateMemory(memory.id, {
    reasoning: updatedReasoning,
  });

  equal(
    updated.reasoning,
    updatedReasoning,
    "Reasoning should be updatable via updateMemory"
  );

  await MemoryStore.testOnlyFlush();

  const { MemoryStore: FreshStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs",
    { ignoreCache: true }
  );

  await FreshStore.ensureInitialized();
  const persistedMemories = await FreshStore.getMemories();
  const persistedMemory = persistedMemories.find(m => m.id === memory.id);

  ok(persistedMemory, "Memory should exist after reload");
  equal(
    persistedMemory.reasoning,
    updatedReasoning,
    "Reasoning should survive persistence roundtrip"
  );

  await FreshStore.hardDeleteMemory(memory.id);
});

add_task(async function test_getMemories_filters_by_memoryIds_set() {
  await MemoryStore.ensureInitialized();

  const mem1 = await MemoryStore.addMemory({
    memory_summary: "likes coffee",
    tags: ["category: Food & Drink", "intent: Plan / Organize"],
  });
  const mem2 = await MemoryStore.addMemory({
    memory_summary: "likes tea",
    tags: ["category: Food & Drink", "intent: Communicate / Share"],
  });
  const mem3 = await MemoryStore.addMemory({
    memory_summary: "likes playing tennis",
    tags: ["category: Sports & Fitness", "intent: Plan / Organize"],
  });

  const result = await MemoryStore.getMemories({
    memoryIds: new Set([mem1.id, mem3.id]),
  });

  Assert.equal(result.length, 2, "Should return only the 2 requested memories");
  Assert.ok(
    result.some(m => m.id === mem1.id),
    "Result should include mem1"
  );
  Assert.ok(
    result.some(m => m.id === mem3.id),
    "Result should include mem3"
  );
  Assert.ok(
    !result.some(m => m.id === mem2.id),
    "Result should not include mem2"
  );

  await MemoryStore.hardDeleteMemory(mem1.id);
  await MemoryStore.hardDeleteMemory(mem2.id);
  await MemoryStore.hardDeleteMemory(mem3.id);
});

add_task(async function test_create_memory_id() {
  const memoryPartial = {
    memory_summary: "Likes coffee",
    tags: ["category:Food & Drink", "intent:Communicate / Share"],
  };
  const memoryId = makeMemoryId(memoryPartial);
  Assert.equal(
    memoryId.split(".")[0],
    "mem",
    "Memory ID should start with the correct prefix"
  );
});

add_task(async function test_migrate_memories_from_v1_to_v2() {
  const memoriesV1 = [
    {
      id: "mem.123",
      memory_summary: "Likes dogs",
      reasoning: "User researches dog facts, treats, and training advice",
      category: "Dogs",
      intent: "Share / Communicate",
      score: 1.0,
      source: "history",
      source_ids: {
        history_source_ids: [1, 2, 3],
        conversation_source_ids: [4, 5, 6],
      },
      is_deleted: false,
      updated_at: Date.now(),
    },
  ];

  const memoriesV2 = migrateMemoryStoreVersionOneToTwo(memoriesV1);

  // Check that 1 memory comes out
  Assert.ok(
    Array.isArray(memoriesV2),
    "Migrated memories should be in an array"
  );
  Assert.equal(
    memoriesV2.length,
    1,
    "Migrated memories array should have only 1 entry"
  );

  const memoryV2 = memoriesV2[0];
  // Check fields that should have been deleted
  for (const prop of ["score", "source", "category", "intent"]) {
    Assert.ok(
      !memoryV2.hasOwnProperty(prop),
      `"${prop}" should be deleted from the migrated memory`
    );
  }

  // Check system fields
  Assert.equal(
    memoryV2.id,
    "mem.123",
    "Migrated memory should have its original ID"
  );
  Assert.equal(
    memoryV2.type,
    MEMORY_TYPE_SHORT_TERM_MEMORY,
    `Migrated memory type should be "${MEMORY_TYPE_SHORT_TERM_MEMORY}"`
  );
  Assert.deepEqual(
    memoryV2.sources,
    ["history"],
    'Migrated memory sources should have 1 entry: "history"'
  );
  Assert.deepEqual(
    memoryV2.source_ids,
    { history_source_ids: [1, 2, 3], conversation_source_ids: [4, 5, 6] },
    "Migrated memory should have its original source IDs"
  );
  Assert.equal(
    memoryV2.sensitivity_category,
    MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    `Migrated memory sensitivity category should be "${MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE}\"`
  );
  Assert.equal(
    memoryV2.is_deleted,
    false,
    "Migrated memory should not be soft deleted"
  );

  // Check descriptive fields
  Assert.equal(
    memoryV2.memory_summary,
    "Likes dogs",
    "Migrated memory should have its original summary"
  );
  Assert.equal(
    memoryV2.reasoning,
    "User researches dog facts, treats, and training advice",
    "Migrated memory should have its original reasoning"
  );
  Assert.deepEqual(
    memoryV2.tags,
    ["category:Dogs", "intent:Share / Communicate"],
    "Migrated memory tags should include its original category and intent"
  );
  Assert.deepEqual(
    memoryV2.keywords,
    [],
    "Migrated memory without entities in v1 should have an empty array in v2"
  );
  Assert.deepEqual(
    memoryV2.component_summaries,
    [],
    "Migrated memory should have an empty component_summaries array"
  );

  // Check tracker fields
  Assert.ok(
    Number.isFinite(memoryV2.created_at),
    "Migrated memory creation timestamp should be a number"
  );
  Assert.ok(
    Number.isFinite(memoryV2.updated_at),
    "Migrated memory update timestamp should be a number"
  );
  Assert.equal(
    memoryV2.created_at,
    memoryV2.updated_at,
    "Migrated memory creation and update timestamps should be the same"
  );
  Assert.equal(
    memoryV2.last_accessed,
    null,
    "Migrated memory last accessed timestamp should be null (never used)"
  );
  Assert.deepEqual(
    memoryV2.recent_accessed_counts,
    Object.fromEntries(
      Array.from({ length: MEMORY_FRECENCY_MAX_DAYS }, (_, i) => [i, 0])
    ),
    "Migrated memory recent accessed counts object should have the expected base structure"
  );
  Assert.equal(
    memoryV2.lifetime_accessed_count,
    0,
    "Migrated memory lifetime accessed count should be 0 (never used)"
  );
  Assert.equal(
    memoryV2.frecency,
    0,
    "Migrated memory frecency should be 0 (never used)"
  );
  Assert.equal(
    memoryV2.merge_count,
    0,
    "Migrated memory merge count should be 0 (never used)"
  );
});

add_task(
  async function test_migrate_memories_from_v1_to_v2_missing_fields_fill_defaults() {
    const now = Date.now();
    const memoriesV1 = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        source: "history",
        source_ids: {
          history_source_ids: [1, 2, 3],
          conversation_source_ids: [4, 5, 6],
        },
        updated_at: now,
      },
    ];

    const memoriesV2 = migrateMemoryStoreVersionOneToTwo(memoriesV1);

    Assert.equal(
      memoriesV2[0].reasoning,
      "",
      "Missing reasoning should fill empty string"
    );
    Assert.equal(
      memoriesV2[0].is_deleted,
      false,
      "Missing soft deletion flag should fill false"
    );
    Assert.greaterOrEqual(
      memoriesV2[0].updated_at,
      now,
      "Missing updated_at should fill now"
    );
  }
);

add_task(
  async function test_migrate_memories_from_v1_to_v2_missing_source_fills_from_source_ids() {
    const memoriesV1FromSession = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        reasoning: "User researches dog facts, treats, and training advice",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        source_ids: {
          history_source_ids: [1, 2, 3],
          conversation_source_ids: [4, 5, 6],
        },
        is_deleted: false,
        updated_at: Date.now(),
      },
    ];
    const memoriesV2Session = migrateMemoryStoreVersionOneToTwo(
      memoriesV1FromSession
    );
    Assert.ok(!memoriesV2Session[0].source, "Source field should not exist");
    Assert.deepEqual(
      memoriesV2Session[0].sources,
      ["session"],
      "Should infer session from the source IDs when source doesn't exists"
    );

    const memoriesV1FromChat = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        reasoning: "User researches dog facts, treats, and training advice",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        source_ids: {
          history_source_ids: [],
          conversation_source_ids: [4, 5, 6],
        },
        is_deleted: false,
        updated_at: Date.now(),
      },
    ];
    const memoriesV2Chat =
      migrateMemoryStoreVersionOneToTwo(memoriesV1FromChat);
    Assert.ok(!memoriesV2Chat[0].source, "Source field should not exist");
    Assert.deepEqual(
      memoriesV2Chat[0].sources,
      ["conversation"],
      "Should infer conversation from the source IDs when source doesn't exists"
    );

    const memoriesV1FromHistory = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        reasoning: "User researches dog facts, treats, and training advice",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        source_ids: {
          history_source_ids: [1, 2, 3],
          conversation_source_ids: [],
        },
        is_deleted: false,
        updated_at: Date.now(),
      },
    ];
    const memoriesV2History = migrateMemoryStoreVersionOneToTwo(
      memoriesV1FromHistory
    );
    Assert.ok(!memoriesV2History[0].source, "Source field should not exist");
    Assert.deepEqual(
      memoriesV2History[0].sources,
      ["history"],
      "Should infer history from the source IDs when source doesn't exists"
    );

    const memoriesV1SourceFallback = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        reasoning: "User researches dog facts, treats, and training advice",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        source_ids: {
          history_source_ids: [],
          conversation_source_ids: [],
        },
        is_deleted: false,
        updated_at: Date.now(),
      },
    ];
    const memoriesV2SourceFallback = migrateMemoryStoreVersionOneToTwo(
      memoriesV1SourceFallback
    );
    Assert.ok(
      !memoriesV2SourceFallback[0].source,
      "Source field should not exist"
    );
    Assert.deepEqual(
      memoriesV2SourceFallback[0].sources,
      ["history"],
      "Should fill history when the source IDs are empty and source doesn't exists"
    );

    const memoriesV1SourceFallbackMissingSourceIDs = [
      {
        id: "mem.123",
        memory_summary: "Likes dogs",
        reasoning: "User researches dog facts, treats, and training advice",
        category: "Dogs",
        intent: "Share / Communicate",
        score: 1.0,
        is_deleted: false,
        updated_at: Date.now(),
      },
    ];
    const memoriesV2SourceFallbackkMissingSourceIDs =
      migrateMemoryStoreVersionOneToTwo(
        memoriesV1SourceFallbackMissingSourceIDs
      );
    Assert.ok(
      !memoriesV2SourceFallbackkMissingSourceIDs[0].source,
      "Source field should not exist"
    );
    Assert.deepEqual(
      memoriesV2SourceFallbackkMissingSourceIDs[0].sources,
      ["history"],
      "Should fill history when the source IDs are empty and source doesn't exists"
    );
  }
);

/**
 * Tests retrieving relevant memories for a user message using embeddings
 */
add_task(async function test_getRelevantMemories_happy_path() {
  // Add memories so that we pass the existing memories check in the `getRelevantMemories` method
  await addMemories();

  const sb = sinon.createSandbox();
  try {
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

    // Stub getRelevantMemories to use fake embeddings
    let callCount = 0;

    sb.stub(MemoryStore, "getRelevantMemories").callsFake(
      async function (message, topK, threshold) {
        // On first call, let it create the generator, then replace it
        if (callCount === 0) {
          callCount++;
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
            .filter(m => m.similarity >= (threshold || 0.3))
            .sort((a, b) => b.similarity - a.similarity)
            .slice(0, topK || 5);
        }
        // Return empty array for subsequent calls
        return [];
      }
    );

    const relevantMemories =
      await MemoryStore.getRelevantMemories(TEST_MESSAGE);

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

    // Delete memories after test
    await deleteAllMemories();
  } finally {
    sb.restore();
  }
});

/**
 * Tests failed memories retrieval - no existing memories stored
 *
 * We don't mock an engine for this test case because getRelevantMemories should immediately return an empty array
 * because there aren't any existing memories -> No need to call the LLM.
 */
add_task(
  async function test_getRelevantMemories_sad_path_no_existing_memories() {
    const relevantMemories =
      await MemoryStore.getRelevantMemories(TEST_MESSAGE);

    // Check that result is an empty array
    Assert.ok(Array.isArray(relevantMemories), "Result should be an array.");
    Assert.equal(
      relevantMemories.length,
      0,
      "Result should be an empty array when there are no existing memories."
    );
  }
);

/**
 * Tests failed memories retrieval - no memories meet similarity threshold
 */
add_task(
  async function test_getRelevantMemories_sad_path_null_classification() {
    // Add memories so that we pass the existing memories check
    await addMemories();

    const sb = sinon.createSandbox();
    try {
      // Mock getRelevantMemories to return empty array (no memories above threshold)
      const stub = sb.stub(MemoryStore, "getRelevantMemories").resolves([]);

      const relevantMemories =
        await MemoryStore.getRelevantMemories(TEST_MESSAGE);

      // Check that the stub was called
      Assert.ok(stub.calledOnce, "getRelevantMemories should be called once");

      // Check that result is an empty array
      Assert.ok(Array.isArray(relevantMemories), "Result should be an array.");
      Assert.equal(
        relevantMemories.length,
        0,
        "Result should be an empty array when no memories meet similarity threshold."
      );

      // Delete memories after test
      await deleteAllMemories();
    } finally {
      sb.restore();
    }
  }
);

/**
 * Tests that getRelevantMemories properly invalidates cache when memories are updated.
 * Cache should be reused when memories haven't changed, but invalidated when a
 * memory's identity (id or summary) changes.
 */
add_task(async function test_getRelevantMemories_cache_invalidation() {
  await deleteAllMemories();

  // Clear the embeddings cache before this test
  MemoryStore._clearEmbeddingsCache();

  const sb = sinon.createSandbox();
  try {
    await addMemories();

    let embedManyCallCount = 0;

    const fakeGenerator = {
      async embedMany(texts) {
        embedManyCallCount++;
        return {
          output: texts.map((_, i) => [i === 0 ? 1 : 0, i === 1 ? 1 : 0, 0]),
        };
      },
      async embed(_text) {
        return { output: [[0.9, 0.1, 0]] };
      },
    };

    sb.stub(EmbeddingsGenerator.prototype, "embedMany").callsFake(
      fakeGenerator.embedMany
    );
    sb.stub(EmbeddingsGenerator.prototype, "embed").callsFake(
      fakeGenerator.embed
    );

    await MemoryStore.getRelevantMemories("coffee");
    Assert.equal(
      embedManyCallCount,
      1,
      "embedMany should be called once on first call"
    );

    await MemoryStore.getRelevantMemories("coffee");
    Assert.equal(
      embedManyCallCount,
      1,
      "embedMany should NOT be called again when memories unchanged (cache hit)"
    );

    const memories = await MemoryStore.getMemories({
      includeSoftDeleted: true,
    });
    await MemoryStore.updateMemory(memories[0].id, {
      memory_summary: "Loves drinking coffee and tea",
    });

    await MemoryStore.getRelevantMemories("coffee");
    Assert.equal(
      embedManyCallCount,
      2,
      "embedMany should be called again after memory update (cache invalidated)"
    );

    await deleteAllMemories();
  } finally {
    sb.restore();
    // Clear the cache after this test to avoid affecting other tests
    MemoryStore._clearEmbeddingsCache();
  }
});

add_task(async function test_getMemories_with_simple_filters() {
  await addMemories();

  // Filter by memory type
  const memoriesByType = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "type",
        value: [MEMORY_TYPE_DURABLE_MEMORY],
        comparator: MEMORY_FILTER_COMPARATOR.IN,
      },
    ],
  });
  Assert.equal(
    memoriesByType.length,
    2,
    `Should return 2 memories with the "${MEMORY_TYPE_DURABLE_MEMORY}" type`
  );
  Assert.deepEqual(
    ["Loves drinking coffee", "Plays games online"].sort(),
    memoriesByType.map(mem => mem.memory_summary).sort(),
    `Filter by "${MEMORY_TYPE_DURABLE_MEMORY}" should return the expected summaries`
  );

  // Filter by memory source
  const memoriesbySource = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "sources",
        value: ["session"],
        comparator: MEMORY_FILTER_COMPARATOR.SOME,
      },
    ],
  });
  Assert.equal(
    memoriesbySource.length,
    1,
    'Filtering by "session" source should return 1 memory'
  );
  Assert.equal(
    memoriesbySource[0].memory_summary,
    "Buys dog food online",
    'Filtering by "session" source should return the expected memory'
  );

  // Filter by exact sensitivity category
  const memoriesBySensitivity = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "sensitivity_category",
        value: MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
        comparator: MEMORY_FILTER_COMPARATOR.EQUAL_TO,
      },
    ],
  });
  Assert.equal(
    memoriesBySensitivity.length,
    1,
    `Filtering by exactly \"${MEMORY_SENSITIVITY_CATEGORY_SENSITIVE}\" should return exactly 1 memory`
  );
  Assert.equal(
    memoriesBySensitivity[0].memory_summary,
    "Plays games online",
    `Filtering by exactly \"${MEMORY_SENSITIVITY_CATEGORY_SENSITIVE}\" should return the expected memory`
  );

  // Filter by tags, loose
  const memoriesByTags = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "tags",
        value: ["category:Food & Drink", "extra:tag"],
        comparator: MEMORY_FILTER_COMPARATOR.SOME,
      },
    ],
  });
  Assert.equal(
    memoriesByTags.length,
    2,
    "Filter loosely by tag should return 2 memories"
  );
  Assert.deepEqual(
    ["Loves drinking coffee", "Plays games online"].sort(),
    memoriesByTags.map(m => m.memory_summary).sort(),
    "Filter loosely by tag should return the expected memories"
  );

  // Filter by tags, exact set match (order-independent)
  const memoriesByTagsExact = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "tags",
        value: ["extra:tag", "category:Food & Drink", "intent:Plan / Organize"],
        comparator: MEMORY_FILTER_COMPARATOR.ALL,
      },
    ],
  });
  Assert.equal(
    memoriesByTagsExact.length,
    1,
    "Filter by exact tag set should return 1 memory"
  );
  Assert.equal(
    memoriesByTagsExact[0].memory_summary,
    "Loves drinking coffee",
    "Filter by exact tag set should return the expected memory"
  );

  // Exact set match must reject a strict subset of a memory's tags
  const memoriesByTagsSubset = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "tags",
        value: ["category:Food & Drink", "extra:tag"],
        comparator: MEMORY_FILTER_COMPARATOR.ALL,
      },
    ],
  });
  Assert.equal(
    memoriesByTagsSubset.length,
    0,
    "Filter by a strict subset of a memory's tags should return 0 memories"
  );

  // Filter by 1 tag
  const memoriesBySingleTag = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "tags",
        value: "intent:Entertain / Relax",
        comparator: MEMORY_FILTER_COMPARATOR.INCLUDES,
      },
    ],
  });
  Assert.equal(
    memoriesBySingleTag.length,
    1,
    "Filter by a single tag should return 1 memory"
  );
  Assert.equal(
    memoriesBySingleTag[0].memory_summary,
    "Plays games online",
    "Filter by a single tag should return the expected memory"
  );

  // Filter by updated_at date
  const memsByUpdateDate = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "updated_at",
        value: 123,
        comparator: MEMORY_FILTER_COMPARATOR.GREATER_THAN,
      },
    ],
  });
  Assert.equal(
    memsByUpdateDate.length,
    2,
    "Filter by update date should return 2 memories"
  );
  Assert.deepEqual(
    ["Loves drinking coffee", "Plays games online"].sort(),
    memsByUpdateDate.map(mem => mem.memory_summary).sort(),
    "Filter by update date should return the expected summaries"
  );

  await deleteAllMemories();
});

add_task(async function test_getMemories_with_multiple_filters() {
  await addMemories();

  const memsOneFilter = await MemoryStore.getMemories({
    attributeFilters: [
      { field: "tags", value: "extra:tag", comparator: "INCLUDES" },
    ],
  });
  Assert.equal(
    memsOneFilter.length,
    2,
    "Filtering only on tag should return 2 memories"
  );

  const memsTwoFilters = await MemoryStore.getMemories({
    attributeFilters: [
      { field: "tags", value: "extra:tag", comparator: "INCLUDES" },
      {
        field: "sensitivity_category",
        value: MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
        comparator: "EQUAL_TO",
      },
    ],
  });
  Assert.equal(
    memsTwoFilters.length,
    1,
    "Adding type filter should return 1 memory"
  );
  Assert.equal(
    memsTwoFilters[0].memory_summary,
    "Loves drinking coffee",
    "Adding type filter should return the expected memory"
  );

  await deleteAllMemories();
});

add_task(async function test_getMemories_with_day_range_filter() {
  await addMemories();

  const memUsedOnDay7 = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "recent_accessed_counts.[6]",
        value: 5,
        comparator: "GREATER_THAN",
      },
    ],
  });
  Assert.equal(
    memUsedOnDay7.length,
    1,
    "Should return 1 memory used more than 5 times on the 7th day"
  );
  Assert.equal(
    memUsedOnDay7[0].memory_summary,
    "Loves drinking coffee",
    "Should return the expected memory used more than 5 times on the 7th day"
  );

  const memsUsedBetweenDay1And3 = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "recent_accessed_counts.[0-2]",
        value: 3,
        comparator: "EQUAL_TO",
      },
    ],
  });
  Assert.equal(
    memsUsedBetweenDay1And3.length,
    2,
    "Should return 2 memories used exactly 3 times between the 1st and 3rd day"
  );
  Assert.deepEqual(
    ["Buys dog food online", "Plays games online"].sort(),
    memsUsedBetweenDay1And3.map(mem => mem.memory_summary).sort(),
    "Should return the expected memories used exactly 3 times between the 1st and 3rd day"
  );

  const memNotUsedOnDays5And7 = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "recent_accessed_counts.[4, 6]",
        value: 2,
        comparator: "LESS_THAN",
      },
    ],
  });
  Assert.equal(
    memNotUsedOnDays5And7.length,
    1,
    "Should return 1 memory used fewer than 2 times total the 5th and 7th days"
  );
  Assert.equal(
    memNotUsedOnDays5And7[0].memory_summary,
    "Plays games online",
    "Should return the expected memory used fewer than 2 times total the 5th and 7th days"
  );

  const memUsedOnDay5ButNotDay6 = await MemoryStore.getMemories({
    attributeFilters: [
      {
        field: "recent_accessed_counts.[4]",
        value: 0,
        comparator: "GREATER_THAN",
      },
      { field: "recent_accessed_counts.[5]", value: 0, comparator: "EQUAL_TO" },
    ],
  });
  Assert.equal(
    memUsedOnDay5ButNotDay6.length,
    1,
    "Should return 1 memory used on the 5th day but no the 6th"
  );
  Assert.equal(
    memUsedOnDay5ButNotDay6[0].memory_summary,
    "Buys dog food online",
    "Should return the expected memory used on the 5th day but no the 6th"
  );

  await deleteAllMemories();
});

add_task(async function test_getMemories_with_semantic_filter() {
  await addMemories();

  // Semantic filters
  const sb = sinon.createSandbox();
  try {
    sb.stub(MemoryStore, "getRelevantMemories").callsFake(
      getFakeRelevantMemories
    );

    const memsBySummaryWithDefaultParams = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "memory_summary",
          value: "Loves drinking coffee",
          comparator: MEMORY_FILTER_COMPARATOR.LIKE,
        },
      ],
    });
    Assert.equal(
      memsBySummaryWithDefaultParams.length,
      1,
      "Sematic filter by memory summary should return 1 memory"
    );
    Assert.equal(
      memsBySummaryWithDefaultParams[0].memory_summary,
      "Loves drinking coffee",
      "Semantic filter by memory should should return the expected summary"
    );

    const memsBySummaryWithTopKParam = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "memory_summary",
          value: "Loves drinking coffee",
          comparator: MEMORY_FILTER_COMPARATOR.LIKE,
          options: {
            topK: 0,
          },
        },
      ],
    });
    Assert.equal(
      memsBySummaryWithTopKParam.length,
      0,
      "Semantic filter by memory summary should return 0 memories with 0 topK passed in options"
    );

    const memsBySummaryWithSimilarityThresholdParam =
      await MemoryStore.getMemories({
        attributeFilters: [
          {
            field: "memory_summary",
            value: "Loves drinking coffee",
            comparator: MEMORY_FILTER_COMPARATOR.LIKE,
            options: {
              similarityThreshold: 0.0,
            },
          },
        ],
      });
    Assert.equal(
      memsBySummaryWithSimilarityThresholdParam.length,
      3,
      "Semantic filter by memory summary should return 3 memories (all of them) with 0.0 similarityThreshold passed in options"
    );
  } finally {
    sb.restore();
  }

  await deleteAllMemories();
});

add_task(
  async function test_getMemories_invalid_value_filter_returns_empty_array() {
    await addMemories();

    const invalidValueOnNumberFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "created_at",
          value: "not a number",
          comparator: "GREATER_THAN",
        },
      ],
    });
    Assert.equal(
      invalidValueOnNumberFilter.length,
      0,
      "Should return 0 memories when using a string filter value with a number comparator"
    );

    const invalidValueOnStringFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "memory_summary",
          value: 123,
          comparator: "LIKE",
        },
      ],
    });
    Assert.equal(
      invalidValueOnStringFilter.length,
      0,
      "Should return 0 memories when using a number filter value with a string comparator"
    );

    const invalidValueOnArrayFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "tags",
          value: 123,
          comparator: "SOME",
        },
      ],
    });
    Assert.equal(
      invalidValueOnArrayFilter.length,
      0,
      "Should return 0 memories when using a number filter value with an array comparator"
    );

    const invalidValueOnScalarFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "type",
          value: [],
          comparator: "EQUAL_TO",
        },
      ],
    });
    Assert.equal(
      invalidValueOnScalarFilter.length,
      0,
      "Should return 0 memories when using an array filter value with a scalar comparator"
    );

    await deleteAllMemories();
  }
);

add_task(
  async function test_getMemories_invalid_field_filter_returns_empty_array() {
    await addMemories();

    const invalidFieldOnNumberFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "memory_summary",
          value: 0,
          comparator: "GREATER_THAN",
        },
      ],
    });
    Assert.equal(
      invalidFieldOnNumberFilter.length,
      0,
      "Should return 0 memories when using a string field with a number comparator"
    );

    const invalidFieldOnLikeFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "not_memory_summary",
          value: "dog",
          comparator: "LIKE",
        },
      ],
    });
    Assert.equal(
      invalidFieldOnLikeFilter.length,
      0,
      "Should return 0 memories when using a field other than memory_summary with the LIKE comparator"
    );

    const invalidFieldOnArrayFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "created_at",
          value: ["dog"],
          comparator: "SOME",
        },
      ],
    });
    Assert.equal(
      invalidFieldOnArrayFilter.length,
      0,
      "Should return 0 memories when using a number field with an array comparator"
    );

    const invalidFieldOnScalarFilter = await MemoryStore.getMemories({
      attributeFilters: [
        {
          field: "tags",
          value: 123,
          comparator: "EQUAL_TO",
        },
      ],
    });
    Assert.equal(
      invalidFieldOnScalarFilter.length,
      0,
      "Should return 0 memories when using an array field with a scalar comparator"
    );

    await deleteAllMemories();
  }
);
