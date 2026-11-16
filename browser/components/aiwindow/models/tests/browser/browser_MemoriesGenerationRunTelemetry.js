/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MemoriesSchedulers } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesSchedulers.sys.mjs"
);
const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const { JourneyStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/journeys/JourneyStore.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const { setGenerationRunError } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/Memories.sys.mjs"
);
const {
  PREF_GENERATE_MEMORIES_FROM_HISTORY,
  PREF_GENERATE_MEMORIES_FROM_CONVERSATION,
  GENERATION_RUN_ERROR_STAGE,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);
const { AIWindowTestUtils, MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const PREF_FIRSTRUN_HAS_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";
const PREF_TOU_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const PREF_LAST_USAGE_TIME = "browser.smartwindow.lastSmartWindowUsageTime";

const MINUTE_MS = 60 * 1000;
const HOUR_MS = 60 * MINUTE_MS;

const COOLDOWN_MS = 4 * HOUR_MS;

let gAIWindow;
let gMockEngineManager;

/**
 * Empty every store a run reads from or writes to.
 */
async function resetStores() {
  const memories = await MemoryStore.getMemories({ includeSoftDeleted: true });
  for (const memory of memories) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }
  await JourneyStore.clear();
  await PlacesUtils.history.clear();
  await ChatStore.deleteAllConversations();
}

/**
 * Store `count` memories with predictable summaries.
 *
 * @param {number} count
 * @returns {Promise<Array<string>>} The stored summaries.
 */
async function seedMemories(count) {
  const summaries = [];
  for (let i = 0; i < count; i++) {
    const summary = `Seeded memory ${i}`;
    await MemoryStore.addMemory({ memory_summary: summary });
    summaries.push(summary);
  }
  return summaries;
}

/**
 * Store a burst of activity close enough together to form one session.
 *
 * @param {number} startMs                    Time of the first visit
 * @param {object} options
 * @param {number} [options.visits]           Page visits with meaningful titles
 * @param {boolean} [options.sensitiveVisit]  Also store a visit whose title
 *                                            holds an email address that should trip the sensitive info detector
 * @param {Array<string>} [options.searches]  Search queries to store as
 *                                            visits to a search results page
 * @param {Array<string>} [options.chats]     User chat messages to store
 */
async function seedSession(
  startMs,
  { visits = 0, sensitiveVisit = false, searches = [], chats = [] }
) {
  for (let i = 0; i < visits; i++) {
    await writePageVisitToMozPlaces(
      `https://example.com/${startMs}/trail-${i}`,
      `Beginner hiking trails near Denver, part ${i}`,
      startMs + i * MINUTE_MS
    );
  }
  if (sensitiveVisit) {
    await writePageVisitToMozPlaces(
      `https://example.com/${startMs}/contact`,
      "Contact me at user@example.com",
      startMs + visits * MINUTE_MS
    );
  }
  for (let i = 0; i < searches.length; i++) {
    await PlacesUtils.history.insertMany([
      {
        url: `https://www.google.com/search?q=${encodeURIComponent(searches[i])}`,
        title: `${searches[i]} - Google Search`,
        visits: [{ date: new Date(startMs + (visits + 1 + i) * MINUTE_MS) }],
      },
    ]);
  }
  for (let i = 0; i < chats.length; i++) {
    await addChatMessage(startMs + (i + 1) * 30 * 1000, chats[i]);
  }
}

/**
 * Run the next request in the memories generation sequence.
 *
 * @returns {Promise<object>} The next model request from the memories pipeline: generation, the sensitivity/quality filter, or merge.
 */
function nextMemoriesRequest() {
  return gMockEngineManager.captureRequest({
    purpose: PURPOSES.MEMORY_GENERATION,
  });
}

/**
 * Answer the next generation request with candidate memories.
 *
 * @param {Array<string>} summaries   Memory summaries for each mocked memory object
 */
async function answerGeneration(summaries) {
  (await nextMemoriesRequest()).respond(
    JSON.stringify(
      summaries.map(summary => ({
        reasoning: "Repeatedly read about this topic",
        category: "Internet & Telecom",
        intent: "Research / Learn",
        memory_summary: summary,
        score: 4,
        evidence: [],
      }))
    )
  );
}

/**
 * Answer the next quality filter request.
 *
 * @param {Array<string>} kept  Summaries the filter keeps
 */
async function answerFilter(kept) {
  (await nextMemoriesRequest()).respond(
    JSON.stringify({ kept_memories: kept })
  );
}

/**
 * Answer the next merge request with a single group.
 *
 * @param {Array<string>} componentStatements   Memories that should be merged together
 * @param {string} newStatement                 Summary for the merged memory
 */
async function answerMerge(componentStatements, newStatement) {
  (await nextMemoriesRequest()).respond(
    JSON.stringify([
      {
        component_statements: componentStatements,
        new_reasoning: "These describe the same interest",
        new_statement: newStatement,
      },
    ])
  );
}

/**
 * Fail the next memories pipeline request.
 *
 * @param {Error} error
 */
async function rejectNextRequest(error) {
  (await nextMemoriesRequest()).reject(error);
}

/**
 * Return mocked HTTP errors from the LLM (i.e. 429, etc.)
 *
 * @param {number} status
 * @returns {Error} An error shaped like the model service's HTTP failures.
 */
function httpError(status) {
  return Object.assign(new Error(`${status} status code`), { status });
}

/**
 * Run one scheduler tick while answering its model requests.
 *
 * @param {MemoriesSchedulers} scheduler  Memories scheduler
 * @param {Function} answerRequests  Answers every request the tick sends
 */
async function runTick(scheduler, answerRequests) {
  const tick = scheduler.runNowForTesting();
  await answerRequests();
  await tick;
}

add_setup(async function () {
  await resetStores();

  // The scheduler is a singleton shared with the rest of the browser. Stop
  // it in case an earlier test or file left it running.
  MemoriesSchedulers.stop();

  gMockEngineManager = new MockEngineManager();
  registerCleanupFunction(() => gMockEngineManager.cleanupMocks());

  // Hold both sources off while the window opens.
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_GENERATE_MEMORIES_FROM_HISTORY, false],
      [PREF_GENERATE_MEMORIES_FROM_CONVERSATION, false],
    ],
  });

  gAIWindow = await AIWindowTestUtils.openAIWindow();
  await BrowserTestUtils.waitForMutationCondition(
    gAIWindow.document.documentElement,
    { attributes: true },
    () => AIWindowTestUtils.isAIWindow(gAIWindow)
  );

  registerCleanupFunction(async () => {
    MemoriesSchedulers.stop();
    await BrowserTestUtils.closeWindow(gAIWindow);
    await resetStores();
    Services.prefs.clearUserPref(PREF_LAST_USAGE_TIME);
  });
});

/**
 * Run a task with every scheduler gate genuinely satisfied: a real AI Window
 * is open, and the source, firstrun and ToU consent prefs are really set.
 *
 * @param {object} options
 * @param {number} [options.watermarkMs]          Session-memory watermark to
 *                                                persist; 0 makes it a first run
 * @param {boolean} [options.historyEnabled]      Whether the browsing source is on
 * @param {boolean} [options.conversationEnabled] Whether the chat source is on
 * @param {Function} task   Receives `{ startScheduler }`
 */
async function withGenerationRun(
  { watermarkMs = 0, historyEnabled = true, conversationEnabled = true },
  task
) {
  await resetStores();

  // Enable all prefs the scheduler will look for
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_GENERATE_MEMORIES_FROM_HISTORY, historyEnabled],
      [PREF_GENERATE_MEMORIES_FROM_CONVERSATION, conversationEnabled],
      [PREF_FIRSTRUN_HAS_COMPLETED, true],
      [PREF_TOU_CONSENT_TIME, Math.floor(Date.now() / 1000)],
    ],
  });

  // Set the watermarks
  await MemoriesManager.setLastSessionMemoryTimestamp(watermarkMs);
  Services.fog.testResetFOG();

  // Function to restart the scheduler
  MemoriesSchedulers.stop();
  const startScheduler = () => {
    const scheduler = MemoriesSchedulers.maybeRunAndSchedule();
    Assert.ok(
      scheduler,
      "Scheduler should start with a real AI Window open and every pref set."
    );
    return scheduler;
  };

  try {
    // Run the test task
    // Tasks start the scheduler when they're ready, so the environment is set properly
    await task({ startScheduler });
    gMockEngineManager.assertAllRequestsHandled();
  } finally {
    MemoriesSchedulers.stop();
    await SpecialPowers.popPrefEnv();
    await MemoriesManager.setLastSessionMemoryTimestamp(0);
    await MemoriesManager.setLastGenerationRunTimestamp(0);
    await resetStores();
  }
}

// A watermark old enough that the cooldown, which seeds from it, has elapsed.
const deltaWatermarkMs = () => Date.now() - COOLDOWN_MS - HOUR_MS;

/**
 * Tests stats record generation was not triggered when not enough evidence was accumulated.
 */
add_task(async function test_generation_not_triggered() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs() },
    async ({ startScheduler }) => {
      const scheduler = startScheduler();

      // Cooldown has elapsed, but there are no new visits or chat messages.
      await scheduler.runNowForTesting();
      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: false,
      });

      // The tick above restarted the cooldown, so new activity is not enough,
      // and nothing more is recorded.
      await visitPage("https://example.com/inside-cooldown");
      await scheduler.runNowForTesting();
      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: false,
      });
    }
  );
});

/**
 * Tests a first run through every stage and that Glean stats are recorded:
 * - sources with something to filter out
 * - one session
 * - generation
 * - sensitivity/quality filter
 * - save where a duplicate candidate updates the memory saved before it
 * - merge
 */
add_task(async function test_full_run() {
  await withGenerationRun({}, async ({ startScheduler }) => {
    // A first run needs MIN_RECENT_VISITS (10) recent visits to trigger.
    await seedSession(Date.now() - HOUR_MS, {
      visits: 11,
      sensitiveVisit: true,
      chats: [
        "What are good beginner hiking trails near Denver?",
        "My phone is 202-555-0123",
      ],
    });

    // A first run starts as soon as the scheduler is built.
    const kept = [
      "Plans beginner hikes near Denver",
      "Prefers trails under five miles",
      "Hikes on weekends",
      "Owns trekking poles",
      "Wants to see alpine lakes",
      "Checks trail conditions before hiking",
    ];
    const scheduler = startScheduler();
    await runTick(scheduler, async () => {
      // The repeated summary is saved twice, the second time as an update.
      await answerGeneration([...kept, kept[0], "Likes long trail names"]);
      await answerFilter(kept);
      await answerMerge([kept[1], kept[2]], "Hikes short trails on weekends");
    });

    assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
      generation_triggered: true,
      memories_before: 0, // No memories in the store before generation ran
      is_first_run: true, // Flag that this is the first generation run
      history_rows_pulled: 12,
      history_rows_kept: 11, // 1 sensitive visit seeded
      chat_messages_pulled: 2,
      chat_messages_kept: 1, // 1 sensitive chat seeded
      sessions_built: 1, // 1 session seeded
      sessions_skipped: 0, // All seeded sessions kept
      sessions_processed: 1, // Final number of sessions sent to generation
      sessions_processed_events: 12,
      sessions_processed_events_min: 12,
      sessions_processed_events_max: 12,
      generation_batches: 1, // Number of generation batches
      generation_batches_skipped: 0,
      generation_candidates: 8, // Number of initial memories generated
      filter_dropped: 1, // Number of memories filtered by sensitivity/uality
      save_new: 6, // Number of new memories saved before merge
      save_updated: 1, // Number of new memories that actually updated old ones
      merge_status: "ran", // Flag that the merge step ran
      merge_groups_proposed: 1, // Number of merge groups suggested
      merge_groups_saved: 1, // Number of merge groups actually saved
      merge_components_removed: 2, // Number of memories deleted in merge (consolidated)
      memories_after: 5, // Number of memories in the store after the whole pipeline
      outcome: "success",
    });

    // Memory maintenance runs should not trigger memory panel telemetry events
    Assert.equal(
      Glean.smartWindow.memoryRemovedPanel.testGetValue(),
      null,
      "Merge deletions should not be recorded as user removals."
    );
  });
});

/**
 * Test stats record session size counts, min, and max.
 */
add_task(async function test_session_sizes() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs() },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 3 * HOUR_MS, {
        visits: 2,
        searches: ["hiking boots"],
      });
      await seedSession(Date.now() - HOUR_MS, {
        visits: 4,
        chats: ["What are good beginner hiking trails near Denver?"],
      });
      const scheduler = startScheduler();
      await visitPage("https://example.com/session-sizes");

      await runTick(scheduler, () => answerGeneration([]));

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 7,
        history_rows_kept: 7,
        chat_messages_pulled: 1,
        chat_messages_kept: 1,
        sessions_built: 2,
        sessions_skipped: 0,
        sessions_processed: 2,
        sessions_processed_events: 8, // 8 events total: 6 visits and 2 chats
        sessions_processed_events_min: 3, // 1st session is the smallest: 2 visits + 1 chat
        sessions_processed_events_max: 5, // 2nd session is the biggest: 4 visits + 1 chat
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 0,
        merge_status: "not_run",
        memories_after: 0,
        outcome: "no_candidates",
      });
    }
  );
});

/**
 * Test a later run (not first) that saves too few memories to merge.
 */
add_task(async function test_merge_below_threshold() {
  await withGenerationRun(
    // Set the watermark to flag this as a delta run (not first)
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/below-threshold");

      await runTick(scheduler, async () => {
        await answerGeneration(["Plans beginner hikes near Denver"]);
        await answerFilter(["Plans beginner hikes near Denver"]);
      });

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false, // Flag as not the first run
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 1,
        filter_dropped: 0,
        save_new: 1,
        save_updated: 0,
        merge_status: "below_threshold", // Total memories under the merge threshold
        memories_after: 1,
        outcome: "success",
      });
    }
  );
});

/**
 * Test the stats record when the filter drops every candidate.
 */
add_task(async function test_all_filtered() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/all-filtered");

      await runTick(scheduler, async () => {
        await answerGeneration(["Too vague", "Also too vague"]);
        await answerFilter([]);
      });

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 2,
        filter_dropped: 2, // Filtered is the same as candidates, meaning everything is filtered out
        merge_status: "not_run", // Merge isn't run because nothing gets through generation
        memories_after: 0,
        outcome: "all_filtered",
      });
    }
  );
});

/**
 * Test stats record when generation returns no candidates, so the filter never runs.
 */
add_task(async function test_no_candidates() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/no-candidates");

      await runTick(scheduler, () => answerGeneration([]));

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0, // No memories generated; everything below should reflect that state
        generation_candidates: 0,
        merge_status: "not_run",
        memories_after: 0,
        outcome: "no_candidates",
      });
    }
  );
});

/**
 * Test stats record when not enough history was pulled, so nothing can run.
 */
add_task(async function test_no_sessions() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), historyEnabled: false },
    async ({ startScheduler }) => {
      await addChatMessage(Date.now() - 30 * MINUTE_MS, "ok");
      const scheduler = startScheduler();

      await scheduler.runNowForTesting();

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        chat_messages_pulled: 1,
        chat_messages_kept: 1,
        sessions_built: 1,
        sessions_skipped: 1,
        sessions_processed: 0, // Nothing gets through heuristic filters
        merge_status: "not_run",
        memories_after: 0,
        outcome: "no_sessions", // Generation records no sessions; nothing ran
      });
    }
  );
});

/**
 * Test stats record when the LLM server rejects the request.
 */
add_task(async function test_filter_server_rejected() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/filter-rejected");

      await runTick(scheduler, async () => {
        await answerGeneration(["Plans beginner hikes near Denver"]);
        await rejectNextRequest(httpError(400));
      });

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 1,
        merge_status: "not_reached",
        memories_after: 0,
        outcome: "error", // Error is recorded
        error_stage: "filter", // Errored stage is recorded
        error_type: "server_rejected", // Error type is recorded
      });
    }
  );
});

/**
 * Test stats record when a rate-limit request is thrown to the scheduler.
 */
add_task(async function test_filter_rate_limited() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/filter-rate-limited");

      await runTick(scheduler, async () => {
        await answerGeneration(["Plans beginner hikes near Denver"]);
        // 429 is a rate limit
        // This test raises the rate limit on the filter request, not generation
        await rejectNextRequest(httpError(429));
      });

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 1,
        merge_status: "not_reached",
        memories_after: 0,
        outcome: "error",
        error_stage: "filter",
        error_type: "rate_limited", // Rate limit error type is noted
      });
    }
  );
});

/**
 * Test stats record when the browser, itself throws an error.
 */
add_task(async function test_every_batch_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/every-batch-failed");

      await runTick(scheduler, () =>
        rejectNextRequest(new Error("Simulated generation failure"))
      );

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 1,
        generation_candidates: 0,
        merge_status: "not_reached",
        memories_after: 0,
        outcome: "error",
        error_stage: "generation", // Error thrown at generation
        error_type: "firefox_internal", // Mark this as a firefox-internal error, not a server one
      });
    }
  );
});

/**
 * Test stats record when an error happens at the merge stage.
 */
add_task(async function test_merge_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      const seeded = await seedMemories(6);
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const scheduler = startScheduler();
      await visitPage("https://example.com/merge-failed");

      await runTick(scheduler, async () => {
        await answerGeneration(["Plans beginner hikes near Denver", seeded[5]]);
        await answerFilter(["Plans beginner hikes near Denver", seeded[5]]);
        await rejectNextRequest(new Error("Simulated merge failure"));
      });

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 6,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 2,
        filter_dropped: 0,
        save_new: 1,
        save_updated: 1,
        merge_status: "ran", // Mark merge was attempted
        memories_after: 7, // Final memory count is the original 6 plus the generated 1, recovering from the errored merge attempt
        outcome: "error",
        error_stage: "merge", // Merge is flagged as the error stage
        error_type: "firefox_internal", // Browser error
      });
    }
  );
});

/**
 * Test stats record when 1 batch of several fails in generation and that the remaining batches still save memories.
 */
add_task(async function test_one_batch_failed() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    await seedSession(Date.now() - 3 * HOUR_MS, { visits: 3 });
    await seedSession(Date.now() - HOUR_MS, { visits: 3 });

    const stats = {};
    const run = MemoriesManager.generateMemoriesFromSessions({
      stats,
      batchSize: 1,
    });
    await rejectNextRequest(new Error("Simulated generation failure"));
    await answerGeneration(["Plans beginner hikes near Denver"]);
    await answerFilter(["Plans beginner hikes near Denver"]);
    await run;

    // No outcome: the run did not fail, and success is set by the scheduler.
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        is_first_run: true,
        history_rows_pulled: 6,
        history_rows_kept: 6,
        sessions_built: 2,
        sessions_skipped: 0,
        sessions_processed: 2,
        sessions_processed_events: 6,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 2, // 2 batches attempted
        generation_batches_skipped: 1, // 1 batch failed
        generation_candidates: 1, // 1 Memory saved from the single successful batch
        filter_dropped: 0,
        save_new: 1,
        save_updated: 0,
      },
      { stats }
    );
  });
});

/**
 * Test stats record when a batch hits rate limiting too many times, it's recorded
 */
add_task(async function test_batch_rate_limited() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    await seedSession(Date.now() - HOUR_MS, { visits: 3 });

    const stats = {};
    const run = MemoriesManager.generateMemoriesFromSessions({
      stats,
      maxBatchRetries: 1,
      initialMemoryGenerationRetryDelayMS: 1,
    });
    await rejectNextRequest(httpError(429));
    await Assert.rejects(run, /429/, "The rate limit is rethrown.");

    // No candidate count: generation did not finish.
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        is_first_run: true,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0, // No batches failed because this would trigger a cooldown
        outcome: "error",
        error_stage: "generation", // Generation stage recorded
        error_type: "rate_limited", // Flagged as rate limiting
      },
      { stats }
    );
  });
});

/**
 * Test stats record a failed chat read as a sources error.
 */
add_task(async function test_sources_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs() },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const sandbox = sinon.createSandbox();
      try {
        // Stub findMessagesByDate, so it raises an error
        sandbox
          .stub(ChatStore, "findMessagesByDate")
          .rejects(new Error("Simulated chat store failure"));
        const scheduler = startScheduler();
        await visitPage("https://example.com/sources-failed");

        await scheduler.runNowForTesting();
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        merge_status: "not_reached",
        memories_after: 0,
        outcome: "error",
        error_stage: "sources", // Record sources was the cause of the error
        error_type: "firefox_internal",
      });
    }
  );
});

/**
 * Test stats record a failed history read as a sources error, even though the
 * history source swallows it and the run carries on.
 */
add_task(async function test_history_source_failed() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    await seedSession(Date.now() - HOUR_MS, { visits: 3 });
    const sandbox = sinon.createSandbox();
    const stats = {};
    try {
      sandbox
        .stub(PlacesUtils, "withConnectionWrapper")
        .rejects(new Error("Simulated Places failure"));
      await MemoriesManager.generateMemoriesFromSessions({ stats });
    } finally {
      sandbox.restore();
    }

    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      {
        is_first_run: true,
        sessions_built: 0,
        sessions_skipped: 0,
        sessions_processed: 0,
        outcome: "error", // Not overwritten by no_sessions
        error_stage: "sources", // Error recorded in sources
        error_type: "firefox_internal",
      },
      { stats }
    );
  });
});

/**
 * Test stats record a failed save as a save error.
 */
add_task(async function test_save_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const sandbox = sinon.createSandbox();
      try {
        // Stub addMemory, so it raises an error
        sandbox
          .stub(MemoryStore, "addMemory")
          .rejects(new Error("Simulated memory store failure"));
        const scheduler = startScheduler();
        await visitPage("https://example.com/save-failed");

        await runTick(scheduler, async () => {
          await answerGeneration(["Plans beginner hikes near Denver"]);
          await answerFilter(["Plans beginner hikes near Denver"]);
        });
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 1,
        filter_dropped: 0,
        merge_status: "not_reached",
        memories_after: 0, // Save caused the error, so nothing's in the store
        outcome: "error",
        error_stage: "save", // Save is recorded as the error stage
        error_type: "firefox_internal",
      });
    }
  );
});

/**
 * Test stats record something outside the tagged steps as "other" error.
 */
add_task(async function test_other_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      const sandbox = sinon.createSandbox();
      try {
        // Stub something else to force an "other" error
        sandbox
          .stub(MemoriesManager, "setLastGenerationRunTimestamp")
          .rejects(new Error("Simulated bookkeeping failure"));
        const scheduler = startScheduler();
        await visitPage("https://example.com/other-failed");

        await runTick(scheduler, async () => {
          await answerGeneration(["Plans beginner hikes near Denver"]);
          await answerFilter(["Plans beginner hikes near Denver"]);
        });
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 1,
        filter_dropped: 0,
        save_new: 1,
        save_updated: 0,
        merge_status: "not_reached",
        memories_after: 1,
        outcome: "error",
        error_stage: "other",
        error_type: "firefox_internal",
      });
    }
  );
});

/**
 * Test stats still record an "other" error when the generation pipeline exist early (i.e. nothing generated)
 */
add_task(async function test_other_failed_after_early_outcome() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedSession(Date.now() - 30 * MINUTE_MS, { visits: 3 });
      // Restored before withGenerationRun's cleanup, which writes the clock.
      const sandbox = sinon.createSandbox();
      try {
        sandbox
          .stub(MemoriesManager, "setLastGenerationRunTimestamp")
          .rejects(new Error("Simulated bookkeeping failure"));
        const scheduler = startScheduler();
        await visitPage("https://example.com/other-failed-early-outcome");

        await runTick(scheduler, () => answerGeneration([]));
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.memoriesGenerationRun, {
        generation_triggered: true,
        memories_before: 0,
        is_first_run: false,
        history_rows_pulled: 3,
        history_rows_kept: 3,
        sessions_built: 1,
        sessions_skipped: 0,
        sessions_processed: 1,
        sessions_processed_events: 3,
        sessions_processed_events_min: 3,
        sessions_processed_events_max: 3,
        generation_batches: 1,
        generation_batches_skipped: 0,
        generation_candidates: 0,
        merge_status: "not_reached",
        memories_after: 0,
        outcome: "error",
        error_stage: "other",
        error_type: "firefox_internal",
      });
    }
  );
});

/**
 * Test setGenerationRunError records errors correctly and that it keeps the first error when multiple pile up.
 */
add_task(async function test_setGenerationRunError() {
  Services.fog.testResetFOG();
  const stage = GENERATION_RUN_ERROR_STAGE.GENERATION;

  // Test cases indepedently
  const cases = [
    [httpError(429), "rate_limited"],
    [httpError(503), "transient"],
    [new Error("503 status code"), "transient"],
    [new Error("NetworkError when attempting to fetch resource."), "transient"],
    [httpError(400), "server_rejected"],
    [new Error("400 status code"), "server_rejected"],
    [new TypeError("Simulated bug"), "firefox_internal"],
  ];
  for (const [error, expectedType] of cases) {
    info(`"${error.message}" should be classified as ${expectedType}.`);
    const stats = {};
    setGenerationRunError(stats, stage, error);
    assertGenerationRunEvent(
      Glean.smartWindow.memoriesGenerationRun,
      { outcome: "error", error_stage: stage, error_type: expectedType },
      { stats }
    );
  }

  // Test when multiple errors pile up in the same stats object
  const stats = {};
  setGenerationRunError(stats, stage, httpError(429));
  setGenerationRunError(
    stats,
    GENERATION_RUN_ERROR_STAGE.FILTER,
    new TypeError("Later failure")
  );
  info("The first error should be kept.");
  assertGenerationRunEvent(
    Glean.smartWindow.memoriesGenerationRun,
    { outcome: "error", error_stage: stage, error_type: "rate_limited" },
    { stats }
  );

  setGenerationRunError(undefined, stage, httpError(429));
  Assert.ok(true, "Undefined stats are ignored.");
});
