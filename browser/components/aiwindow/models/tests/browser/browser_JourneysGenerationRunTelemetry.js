/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MemoriesSchedulers } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesSchedulers.sys.mjs"
);
const { MemoriesManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesManager.sys.mjs"
);
const { JourneyManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/journeys/JourneyManager.sys.mjs"
);
const { JourneyStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/journeys/JourneyStore.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);
const {
  PREF_GENERATE_MEMORIES_FROM_HISTORY,
  PREF_GENERATE_MEMORIES_FROM_CONVERSATION,
  MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
  MEMORY_SENSITIVITY_CATEGORY_SENSITIVE,
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
 * Store a burst of page visits close enough together to form one session.
 *
 * @param {number} startMs  Time of the first visit
 * @param {number} visits   Page visits with meaningful titles
 */
async function seedSession(startMs, visits) {
  for (let i = 0; i < visits; i++) {
    await writePageVisitToMozPlaces(
      `https://example.com/${startMs}/trail-${i}`,
      `Beginner hiking trails near Denver, part ${i}`,
      startMs + i * MINUTE_MS
    );
  }
}

/**
 * URL of a page seeded for a journey memory.
 *
 * @param {string} summary  Summary of the memory the page belongs to
 * @param {number} index    Position of the page in the memory's history
 * @returns {string}
 */
function journeyPageUrl(summary, index) {
  return `https://example.com/journey/${encodeURIComponent(summary)}/${index}`;
}

/**
 * Store a memory that can have a journey, linked to real history visits.
 *
 * The visits are dated before the run's watermark, so memory generation
 * doesn't read them.
 *
 * @param {string} summary
 * @param {object} [options]
 * @param {number} [options.pages]       Linked history pages; 0 makes the
 *                                       memory ineligible
 * @param {boolean} [options.sensitive]  Store it as a sensitive memory
 * @param {Array<string>} [options.conversationIds]  Linked chat
 *                                       conversation ids
 * @param {number} [options.createdAt]   Creation time; journey generation
 *                                       picks the most recent memories
 * @returns {Promise<object>} The stored memory.
 */
async function seedJourneyMemory(
  summary,
  { pages = 1, sensitive = false, conversationIds = [], createdAt } = {}
) {
  const visitMs = deltaWatermarkMs() - HOUR_MS;
  const urlHashes = [];
  for (let i = 0; i < pages; i++) {
    const url = journeyPageUrl(summary, i);
    await writePageVisitToMozPlaces(
      url,
      `${summary}, page ${i}`,
      visitMs + i * MINUTE_MS
    );
    urlHashes.push(PlacesUtils.history.hashURL(url));
  }
  return MemoryStore.addMemory({
    memory_summary: summary,
    sensitivity_category: sensitive
      ? MEMORY_SENSITIVITY_CATEGORY_SENSITIVE
      : MEMORY_SENSITIVITY_CATEGORY_NOT_SENSITIVE,
    source_ids: {
      history_source_ids: urlHashes,
      conversation_source_ids: conversationIds,
    },
    ...(createdAt ? { created_at: createdAt } : {}),
  });
}

/**
 * Answer the next memory generation request with candidate memories.
 *
 * @param {Array<string>} summaries   Memory summaries for each mocked memory object
 */
async function answerGeneration(summaries) {
  (
    await gMockEngineManager.captureRequest({
      purpose: PURPOSES.MEMORY_GENERATION,
    })
  ).respond(
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
 * Answer the next journey generation request with raw model output.
 *
 * @param {string} response
 */
async function respondToJourneyRequest(response) {
  (await gMockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })).respond(
    response
  );
}

/**
 * Answer the next journey generation request.
 *
 * @param {Array<{headline: string, status: string}>} journeys  One journey per
 *   memory sent, in the order they were sent
 */
async function answerJourneys(journeys) {
  await respondToJourneyRequest(
    JSON.stringify(
      journeys.map(({ headline, status }, id) => ({ id, headline, status }))
    )
  );
}

/**
 * Fail the next journey generation request.
 *
 * @param {Error} error
 */
async function rejectJourneyRequest(error) {
  (await gMockEngineManager.captureRequest({ purpose: PURPOSES.CHAT })).reject(
    error
  );
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
 * @param {number} [options.lastGenerationMs]     Persisted cooldown clock. Left
 *                                                at 0 so it seeds from
 *                                                `watermarkMs`.
 * @param {boolean} [options.historyEnabled]      Whether the browsing source is on
 * @param {boolean} [options.conversationEnabled] Whether the chat source is on
 * @param {Function} task   Receives `{ startScheduler }`
 */
async function withGenerationRun(
  {
    watermarkMs = 0,
    lastGenerationMs = 0,
    historyEnabled = true,
    conversationEnabled = true,
  },
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
  await MemoriesManager.setLastGenerationRunTimestamp(lastGenerationMs);
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
 * Run a memories tick that reaches journey generation: new activity arms the
 * trigger and memory generation returns nothing, then the journey request is
 * answered.
 *
 * @param {Function} startScheduler  From withGenerationRun
 * @param {Function} answerJourneyRequests  Answers the journey requests
 */
async function runJourneysTick(startScheduler, answerJourneyRequests) {
  await seedSession(Date.now() - 30 * MINUTE_MS, 3);
  const scheduler = startScheduler();
  await visitPage("https://example.com/journeys-trigger");

  await runTick(scheduler, async () => {
    await answerGeneration([]);
    await answerJourneyRequests();
  });
}

/**
 * Assert a journey failure left the memories event as the memories run
 * recorded it: memory generation in runJourneysTick returns no candidates.
 */
function assertMemoriesRunUnaffected() {
  const [event] = getGenerationRunEvents(
    Glean.smartWindow.memoriesGenerationRun
  );
  Assert.equal(
    event?.extra.outcome,
    "no_candidates",
    "The memories run keeps its own outcome."
  );
  Assert.equal(
    event?.extra.error_stage,
    undefined,
    "The journey failure isn't recorded on the memories run."
  );
}

/**
 * Tests a scheduler tick records one journeys event, with a stored memory's
 * history pages resolving without semantic history.
 */
add_task(async function test_journeys_event_recorded() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");

      await runJourneysTick(startScheduler, () =>
        answerJourneys([
          {
            headline: "Beginner hikes near Denver",
            status: "Comparing trails",
          },
        ])
      );

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1,
        journey_memory_candidates: 1, // The page resolved, so the memory was sent
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 1,
        save_new: 1,
        journeys_eligible_after: 1,
        outcome: "success",
      });
    }
  );
});

/**
 * Tests that stats saves a journey for every eligible memory.
 */
add_task(async function test_success() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");
      await seedJourneyMemory("Shops for trekking poles");

      await runJourneysTick(startScheduler, () =>
        answerJourneys([
          { headline: "Beginner hikes", status: "Comparing trails" },
          { headline: "Trekking poles", status: "Reading reviews" },
        ])
      );

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 2,
        journey_memory_candidates: 2,
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 2,
        save_new: 2,
        journeys_eligible_after: 2,
        outcome: "success",
      });
    }
  );
});

/**
 * Tests that stats source memory counts respond to cap and filters
 */
add_task(async function test_cap_and_lost_pages() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      const now = Date.now();
      const summaries = Array.from(
        { length: 7 },
        (_, i) => `Journey memory ${i}`
      );
      for (let i = 0; i < summaries.length; i++) {
        // Memory 0 is the most recent, memory 6 the oldest and left out by
        // the cap.
        await seedJourneyMemory(summaries[i], {
          createdAt: now - i * MINUTE_MS,
        });
      }
      await PlacesUtils.history.remove(journeyPageUrl(summaries[0], 0));

      await runJourneysTick(startScheduler, () =>
        answerJourneys(
          Array.from({ length: 5 }, (_, i) => ({
            headline: `Journey ${i}`,
            status: "In progress",
          }))
        )
      );

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 7,
        journey_memory_candidates: 5, // 6 selected by the cap, 1 with no page left
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 5,
        save_new: 5,
        journeys_eligible_after: 5,
        outcome: "success",
      });
    }
  );
});

/**
 * Tests stats save counts where 1 of 2 generated journeys is rejected, keeping the successful one.
 */
add_task(async function test_partial_save_rejection() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");
      await seedJourneyMemory("Shops for trekking poles");

      const sandbox = sinon.createSandbox();
      try {
        // Saves the first journey, then the store rejects the second
        sandbox
          .stub(JourneyStore, "addOrUpdateJourney")
          .callThrough()
          .onSecondCall()
          .rejects(new TypeError("Simulated journey store rejection"));
        await runJourneysTick(startScheduler, () =>
          answerJourneys([
            { headline: "Beginner hikes", status: "Comparing trails" },
            { headline: "Trekking poles", status: "Reading reviews" },
          ])
        );
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 2,
        journey_memory_candidates: 2,
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 2,
        save_new: 1, // Rejections show as journeys generated minus saved
        journeys_eligible_after: 1,
        outcome: "success",
      });
    }
  );
});

/**
 * Tests stats journeys_eligible_after counts only journeys that can be suggested:
 * dismissed and snoozed journeys are left out, an expired snooze counts.
 */
add_task(async function test_eligible_after_skips_suppressed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      const dismissed = await seedJourneyMemory("Dismissed journey memory");
      const snoozed = await seedJourneyMemory("Snoozed journey memory");
      const expired = await seedJourneyMemory("Expired snooze memory");
      await seedJourneyMemory("Plans beginner hikes near Denver");
      for (const [memory, fields] of [
        [dismissed, { dismissed: true }],
        [snoozed, { dismissed_until: Date.now() + HOUR_MS }],
        [expired, { dismissed_until: Date.now() - HOUR_MS }],
      ]) {
        await JourneyStore.addOrUpdateJourney({
          memoryId: memory.id,
          headline: "Stored journey",
          status: "In progress",
          ...fields,
        });
      }

      await runJourneysTick(startScheduler, () =>
        answerJourneys([
          { headline: "Beginner hikes", status: "Comparing trails" },
        ])
      );

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1, // Memories that already have a journey are skipped
        journey_memory_candidates: 1,
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 1,
        save_new: 1,
        journeys_eligible_after: 2, // The new journey and the expired snooze
        outcome: "success",
      });
    }
  );
});

/**
 * Tests stats save where no memory can have a journey: no model request is made.
 */
add_task(async function test_no_memories_none_eligible() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Sensitive memory", { sensitive: true });
      await seedJourneyMemory("Memory without history", { pages: 0 });
      const withJourney = await seedJourneyMemory("Memory with a journey");
      await JourneyStore.addOrUpdateJourney({
        memoryId: withJourney.id,
        headline: "Stored journey",
        status: "In progress",
      });

      await runJourneysTick(startScheduler, async () => {});

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 0,
        journey_memory_candidates: 0, // No batch was sent, so batch extras are absent
        journeys_eligible_after: 1, // The journey stored before the run
        outcome: "no_memories",
      });
    }
  );
});

/**
 * Tests stats save where the only eligible memory has no page left in history: no
 * model request is made.
 */
add_task(async function test_no_memories_pages_gone() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      const summary = "Plans beginner hikes near Denver";
      await seedJourneyMemory(summary);
      await PlacesUtils.history.remove(journeyPageUrl(summary, 0));

      await runJourneysTick(startScheduler, async () => {});

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1,
        journey_memory_candidates: 0, // Dropped when its pages didn't resolve
        journeys_eligible_after: 0,
        outcome: "no_memories",
      });
    }
  );
});

/**
 * Tests stats save when the model returned no journeys, either as an empty list, as a
 * response that can't be read, or as journeys that are blank once normalized.
 */
add_task(async function test_no_candidates() {
  for (const response of [
    "[]",
    "Not a JSON response",
    JSON.stringify([{ id: 0, headline: " ... ", status: "Comparing trails" }]),
    JSON.stringify([{ id: 0, headline: "Beginner hikes", status: "   " }]),
  ]) {
    info(`Journey response: ${response}`);
    await withGenerationRun(
      { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
      async ({ startScheduler }) => {
        await seedJourneyMemory("Plans beginner hikes near Denver");

        await runJourneysTick(startScheduler, () =>
          respondToJourneyRequest(response)
        );

        assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
          memories_eligible: 1,
          journey_memory_candidates: 1,
          generation_batches: 1,
          generation_batches_skipped: 0,
          journeys_generated: 0, // Nothing to save, so save_new is absent
          journeys_eligible_after: 0,
          outcome: "no_candidates",
        });
      }
    );
  }
});

/**
 * Tests stats save when no journeys event is recorded for
 * an unsupported locale, and nojourney request is made.
 */
add_task(async function test_no_event_unsupported_locale() {
  const originalAvailable = Services.locale.availableLocales;
  const originalRequested = Services.locale.requestedLocales;

  const unsetPrefs = [
    "intl.locale.requested",
    "extensions.getAddons.cache.lastLocale",
  ].filter(pref => !Services.prefs.prefHasUserValue(pref));
  try {
    Services.locale.availableLocales = ["en-US", "de"];
    Services.locale.requestedLocales = ["de"];

    await withGenerationRun(
      { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
      async ({ startScheduler }) => {
        await seedJourneyMemory("Plans beginner hikes near Denver");

        await runJourneysTick(startScheduler, async () => {});

        Assert.deepEqual(
          getGenerationRunEvents(Glean.smartWindow.journeysGenerationRun),
          [],
          "No journeys event is recorded for an unsupported locale."
        );
        Assert.equal(
          getGenerationRunEvents(Glean.smartWindow.memoriesGenerationRun)
            .length,
          1,
          "The memories run is still recorded."
        );
      }
    );
  } finally {
    Services.locale.requestedLocales = originalRequested;
    Services.locale.availableLocales = originalAvailable;
    for (const pref of unsetPrefs) {
      Services.prefs.clearUserPref(pref);
    }
  }
});

/**
 * Tests stats save when no journeys event is recorded when the memories tick
 * doesn't reach journey generation: generation wasn't triggered, or no memory
 * is stored.
 */
add_task(async function test_no_event_without_journey_generation() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");

      // No new activity, so memory generation isn't triggered.
      await startScheduler().runNowForTesting();

      Assert.deepEqual(
        getGenerationRunEvents(Glean.smartWindow.journeysGenerationRun),
        [],
        "No journeys event is recorded when generation isn't triggered."
      );
    }
  );

  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      // No memory is generated
      await runJourneysTick(startScheduler, async () => {});

      Assert.deepEqual(
        getGenerationRunEvents(Glean.smartWindow.journeysGenerationRun),
        [],
        "No journeys event is recorded when no memory is stored."
      );
    }
  );
});

/**
 * Tests stats save when the model rejects every batch in a way retrying won't fix.
 */
add_task(async function test_every_batch_skipped() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");

      await runJourneysTick(startScheduler, () =>
        rejectJourneyRequest(httpError(400))
      );

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1,
        journey_memory_candidates: 1,
        generation_batches: 1,
        generation_batches_skipped: 1,
        journeys_generated: 0, // An error, not no_candidates
        journeys_eligible_after: 0,
        outcome: "error",
        error_stage: "generation",
        error_type: "server_rejected", // Recorded as a server error
      });
      assertMemoriesRunUnaffected();
    }
  );
});

/**
 * Tests stats save when the store rejects every generated journey.
 */
add_task(async function test_all_saves_rejected() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");

      const sandbox = sinon.createSandbox();
      try {
        sandbox
          .stub(JourneyStore, "addOrUpdateJourney")
          .rejects(new TypeError("Simulated journey store rejection"));
        await runJourneysTick(startScheduler, () =>
          answerJourneys([
            { headline: "Beginner hikes", status: "Comparing trails" },
          ])
        );
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1,
        journey_memory_candidates: 1,
        generation_batches: 1,
        generation_batches_skipped: 0,
        journeys_generated: 1,
        save_new: 0, // Nothing saved
        journeys_eligible_after: 0,
        outcome: "error",
        error_stage: "save", // Flagged as error in save step
        error_type: "firefox_internal",
      });
      assertMemoriesRunUnaffected();
    }
  );
});

/**
 * Tests stats save a failure reading the chats linked to memories as a sources error.
 */
add_task(async function test_sources_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      // Linked to a chat, so building the request reads the chat store
      await seedJourneyMemory("Plans beginner hikes near Denver", {
        conversationIds: ["hiking-chat"],
      });
      const sandbox = sinon.createSandbox();
      try {
        // Stub findConversationById, so it raises an error
        sandbox
          .stub(ChatStore, "findConversationById")
          .rejects(new Error("Simulated chat store failure"));
        await runJourneysTick(startScheduler, async () => {});
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        memories_eligible: 1,
        journey_memory_candidates: 0, // The batch failed before it was counted
        journeys_eligible_after: 0,
        outcome: "error",
        error_stage: "sources", // Flag error in sources
        error_type: "firefox_internal",
      });
      assertMemoriesRunUnaffected();
    }
  );
});

/**
 * Tests stats save a failure outside the tagged steps is an other error.
 */
add_task(async function test_other_failed() {
  await withGenerationRun(
    { watermarkMs: deltaWatermarkMs(), conversationEnabled: false },
    async ({ startScheduler }) => {
      await seedJourneyMemory("Plans beginner hikes near Denver");
      const sandbox = sinon.createSandbox();
      try {
        // Stub to raise an error
        sandbox
          .stub(JourneyStore, "getAll")
          .rejects(new Error("Simulated journey store failure"));
        await runJourneysTick(startScheduler, async () => {});
      } finally {
        sandbox.restore();
      }

      assertGenerationRunEvent(Glean.smartWindow.journeysGenerationRun, {
        outcome: "error",
        error_stage: "other", // Errors flagged
        error_type: "firefox_internal",
      });
      assertMemoriesRunUnaffected();
    }
  );
});

/**
 * Seed two journey memories, the first one the most recent so it goes in the
 * first batch.
 *
 * @param {object} [firstOptions]  Extra seedJourneyMemory options for the first
 * @returns {Promise<Array<object>>} The stored memories, in batch order.
 */
async function seedTwoBatchMemories(firstOptions = {}) {
  const now = Date.now();
  return [
    await seedJourneyMemory("First batch memory", {
      createdAt: now,
      ...firstOptions,
    }),
    await seedJourneyMemory("Second batch memory", {
      createdAt: now - MINUTE_MS,
    }),
  ];
}

/**
 * Tests stats save when a batch keeps hitting the rate limit and is rethrown and recorded.
 * Called directly: the scheduler waits 12s between retries.
 */
add_task(async function test_batch_rate_limited() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    const memories = [await seedJourneyMemory("Plans beginner hikes")];

    const stats = {};
    const run = JourneyManager.generateAndPersistJourneys(memories, {
      stats,
      maxBatchAttempts: 1,
    });
    // Force a simulated rate limit
    await rejectJourneyRequest(httpError(429));
    await Assert.rejects(run, /429/, "The rate limit is rethrown.");

    assertGenerationRunEvent(
      Glean.smartWindow.journeysGenerationRun,
      {
        memories_eligible: 1,
        journey_memory_candidates: 1,
        generation_batches: 1,
        generation_batches_skipped: 0,
        outcome: "error",
        error_stage: "generation", // Error flagged as happening during journey generation
        error_type: "rate_limited", // Flagged as a rate limit type
      },
      { stats }
    );
  });
});

/**
 * Tests stats save when one of two batches fails in a way retrying won't fix: the other
 * batch's journey is still saved.
 */
add_task(async function test_one_batch_skipped() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    const memories = await seedTwoBatchMemories();

    const stats = {};
    const run = JourneyManager.generateAndPersistJourneys(memories, {
      stats,
      batchSize: 1,
    });
    await rejectJourneyRequest(new Error("Simulated generation failure"));
    await answerJourneys([{ headline: "Second", status: "In progress" }]);
    await run;

    assertGenerationRunEvent(
      Glean.smartWindow.journeysGenerationRun,
      {
        memories_eligible: 2,
        journey_memory_candidates: 2,
        generation_batches: 2, // Tried 2 batches
        generation_batches_skipped: 1, // Skipped 1
        journeys_generated: 1, // Get the results from the successful batch
        save_new: 1,
        outcome: "success",
      },
      { stats }
    );
  });
});

/**
 * Tests stats save when a batch whose memories all lost their pages sends no request and
 * isn't counted as a batch.
 */
add_task(async function test_empty_batch_not_counted() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    const memories = await seedTwoBatchMemories();
    // Clear 1 of the memory's sources
    await PlacesUtils.history.remove(journeyPageUrl("First batch memory", 0));

    const stats = {};
    const run = JourneyManager.generateAndPersistJourneys(memories, {
      stats,
      batchSize: 1,
    });
    await answerJourneys([{ headline: "Second", status: "In progress" }]);
    await run;

    assertGenerationRunEvent(
      Glean.smartWindow.journeysGenerationRun,
      {
        memories_eligible: 2, // Started wiht 2 memories
        journey_memory_candidates: 1, // Only process 1 of them that kept its sources
        generation_batches: 1, // Only the batch that sent a request
        generation_batches_skipped: 0,
        journeys_generated: 1,
        save_new: 1,
        outcome: "success",
      },
      { stats }
    );
  });
});

/**
 * Tests stats save journeys saved by an earlier batch when a later batch
 * stops the run.
 */
add_task(async function test_saves_counted_before_later_failure() {
  await withGenerationRun({ conversationEnabled: false }, async () => {
    const memories = await seedTwoBatchMemories();

    const stats = {};
    const run = JourneyManager.generateAndPersistJourneys(memories, {
      stats,
      batchSize: 1,
      maxBatchAttempts: 1,
    });
    await answerJourneys([{ headline: "First", status: "In progress" }]);
    await rejectJourneyRequest(httpError(429));
    await Assert.rejects(run, /429/, "The rate limit is rethrown.");

    // No journeys_generated count: generation did not finish. The first batch's save
    // is still counted.
    assertGenerationRunEvent(
      Glean.smartWindow.journeysGenerationRun,
      {
        memories_eligible: 2,
        journey_memory_candidates: 2,
        generation_batches: 2, // Send in 2 batches
        generation_batches_skipped: 0, // Skip none
        save_new: 1, // Save journeys from the first batch
        outcome: "error",
        error_stage: "generation", // Record the error that happend in the second batch
        error_type: "rate_limited",
      },
      { stats }
    );
  });
});
