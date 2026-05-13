/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/// <reference path="head.js" />

// The runner.js script-blob exposes exactly one entry point — `run(done, args)`.
// These tests assert on the resulting `{ name, result }` / `{ name, error }`
// envelope. Smart-Window handler dispatch routes locally; everything else
// delegates to toolkit Runner.sys.mjs (covered by the unknown-message test).

const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);

// ---------------------------------------------------------------------------
// set_real_time_info — pure state setter; proves dispatch routes to
// ChatRunner.setRealTimeInfo and the envelope serializes correctly.
// ---------------------------------------------------------------------------

add_task(async function test_set_real_time_info_envelope() {
  const env = await dispatch("set_real_time_info", {
    url: "https://example.com/",
    title: "Example",
  });
  Assert.equal(env.name, "success", "set_real_time_info envelope: success");
  Assert.deepEqual(
    env.result,
    { ok: true },
    "set_real_time_info envelope.result is { ok: true }"
  );
});

// ---------------------------------------------------------------------------
// init_conversation — constructs a ChatConversation and stores it in module
// state. Proves the lazy import of ChatConversation works and the
// conversationId surfaces back through the envelope.
// ---------------------------------------------------------------------------

add_task(async function test_init_conversation_envelope() {
  const env = await dispatch("init_conversation");
  Assert.equal(env.name, "success", "init_conversation envelope: success");
  Assert.equal(env.result.ok, true, "init_conversation envelope.result.ok");
  Assert.ok(
    typeof env.result.conversationId === "string" &&
      env.result.conversationId.length,
    "init_conversation envelope.result.conversationId is a non-empty string"
  );
});

// ---------------------------------------------------------------------------
// Memories CRUD round-trip via the dispatcher: set → get_all → clear.
// Proves MemoriesRunner is wired and the normalize/serialize boundary is
// stable across the envelope.
// ---------------------------------------------------------------------------

add_task(async function test_memories_roundtrip_envelope() {
  // Clean slate.
  await dispatch("clear_memories");

  const setEnv = await dispatch("set_memories", [
    {
      memory_summary: "Likes spicy food",
      category: "Hobbies & Leisure",
      intent: "Research / Learn",
    },
    "Hates loud music",
  ]);
  Assert.equal(setEnv.name, "success", "set_memories envelope: success");
  Assert.equal(setEnv.result.ok, true, "set_memories envelope.result.ok");
  Assert.equal(
    setEnv.result.count,
    2,
    "set_memories envelope: 2 entries normalized + added"
  );

  const getAllEnv = await dispatch("get_all_memories");
  Assert.equal(getAllEnv.name, "success", "get_all_memories envelope: success");
  Assert.ok(
    Array.isArray(getAllEnv.result.memories) &&
      getAllEnv.result.memories.length >= 2,
    "get_all_memories envelope: ≥2 entries surfaced"
  );

  const clearEnv = await dispatch("clear_memories");
  Assert.equal(clearEnv.name, "success", "clear_memories envelope: success");
  Assert.ok(
    typeof clearEnv.result.deleted === "number" && clearEnv.result.deleted >= 2,
    "clear_memories envelope: deletion count ≥ what was added"
  );

  const allMemories = await MemoryStore.getMemories({
    includeSoftDeleted: true,
  });
  Assert.equal(allMemories.length, 0, "MemoryStore is empty after clear");
});

// ---------------------------------------------------------------------------
// search_browsing_history — proves dispatch routes to
// SearchBrowsingHistoryRunner and the result shape echoes through the
// envelope. We don't seed PlacesUtils here; we only assert on shape.
// ---------------------------------------------------------------------------

add_task(async function test_search_browsing_history_envelope() {
  const env = await dispatch("search_browsing_history", {
    searchTerm: "no-such-history-entry-aiwindow-runner-test",
    historyLimit: 5,
  });
  Assert.equal(
    env.name,
    "success",
    "search_browsing_history envelope: success"
  );
  Assert.equal(
    env.result.ok,
    true,
    "search_browsing_history envelope.result.ok"
  );
  Assert.ok(
    Array.isArray(env.result.result),
    "search_browsing_history envelope.result.result is an array"
  );
});

// ---------------------------------------------------------------------------
// Unknown dispatch name -> error envelope via toolkit fallthrough. The
// AiwindowRunner default arm forwards to toolkit Runner.handleMessage,
// which throws "Unknown message: ${name}". This covers both the
// fallthrough wiring AND the serializeError envelope shape in one shot.
// ---------------------------------------------------------------------------

add_task(async function test_unknown_message_toolkit_fallthrough() {
  const env = await dispatch("not_a_real_message");
  Assert.equal(env.name, "error", "envelope: error");
  Assert.ok(
    env.error.message.includes("Unknown message"),
    "envelope.error.message identifies the bad dispatch (from toolkit Runner)"
  );
  Assert.equal(env.error.name, "Error", "envelope.error.name is 'Error'");
  Assert.ok(
    typeof env.error.stack === "string" && env.error.stack.length,
    "envelope.error.stack is a non-empty string"
  );
});

// ---------------------------------------------------------------------------
// submit_turn with a malformed query -> error envelope. Validates that
// ChatRunner.submitTurn rejects bad input before reaching the engine and
// the error surfaces through the dispatcher envelope.
// ---------------------------------------------------------------------------

add_task(async function test_submit_turn_invalid_query_envelope() {
  const env = await dispatch("submit_turn", []);
  Assert.equal(env.name, "error", "submit_turn envelope: error");
  Assert.ok(
    /query must be a non-empty array/.test(env.error.message),
    "submit_turn envelope.error.message rejects empty query"
  );
});
