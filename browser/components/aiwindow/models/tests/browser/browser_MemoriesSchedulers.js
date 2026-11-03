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
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { ChatStore, ChatConversation, ChatMessage, MESSAGE_ROLE } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
  );
const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { AIWindowTestUtils, MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

const PREF_FIRSTRUN_HAS_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";
const PREF_TOU_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const PREF_LAST_USAGE_TIME = "browser.smartwindow.lastSmartWindowUsageTime";

const HOUR_MS = 60 * 60 * 1000;
const DAY_MS = 24 * HOUR_MS;

// Mirrors the MEMORIES_SCHEDULER_COOLDOWN_MS and MEMORIES_MAINTENANCE_INTERVAL_MS
// defaults. Both are read into module-level consts at import time, so tests
// steer the clocks with the scheduler's testing helpers rather than prefs.
const COOLDOWN_MS = 4 * HOUR_MS;
const MAINTENANCE_INTERVAL_MS = 4 * HOUR_MS;

let gAIWindow;
let gMemorySeq = 0;
let gMockEngineManager;

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
 * Store a memory that is due for maintenance (last aged two days ago) and
 * strong enough to survive the decay check, so a completed maintenance pass is
 * observable as a bumped `updated_at` rather than a deletion.
 *
 * @returns {Promise<{id: string, updatedAt: number}>}  The stored memory's id and updated_at.
 */
async function addDueMemory() {
  const twoDaysAgo = Date.now() - 2 * DAY_MS;
  const memory = await MemoryStore.addMemory({
    memory_summary: `Scheduler test memory ${gMemorySeq++}`,
    created_at: twoDaysAgo,
    updated_at: twoDaysAgo,
    last_accessed: twoDaysAgo,
    lifetime_accessed_count: 100,
    merge_count: 5,
  });
  return { id: memory.id, updatedAt: memory.updated_at };
}

/**
 * Read a memory's maintenance clock after a tick.
 *
 * @param {string} id            The memory ID
 * @returns {Promise<?number>}   `updated_at`, or null if the memory is gone.
 */
async function getUpdatedAt(id) {
  const [memory] = await MemoryStore.getMemories({
    includeSoftDeleted: true,
    memoryIds: new Set([id]),
  });
  return memory?.updated_at ?? null;
}

/**
 * Store a user chat message at an exact `created_date` so tasks can place
 * messages on either side of the watermark boundary. Goes through the real
 * ChatStore, so the scheduler's chat trigger reads it back over real SQL.
 *
 * @param {number} createdDate  Message creation time in ms since epoch
 * @param {string} body         Message body
 */
async function addChatMessage(createdDate, body) {
  await ChatStore.updateConversation(
    new ChatConversation({
      title: body,
      description: "",
      pageUrl: new URL("https://example.com/chat"),
      pageMeta: {},
      messages: [
        new ChatMessage({
          createdDate,
          ordinal: 0,
          role: MESSAGE_ROLE.USER,
          content: { body },
          pageUrl: new URL("https://example.com/chat"),
        }),
      ],
    })
  );
}

/**
 * Store a page visit at an exact visit date so tasks can place visits on either
 * side of the watermark boundary.
 *
 * `getRecentHistory` reads page visits from moz_places_metadata rather than
 * moz_historyvisits, so the metadata row has to be written by hand. Its
 * `total_view_time` must exceed the source's own minimum viewtime filter for the
 * visit to be returned at all.
 *
 * @param {string} url            URL to record a visit for
 * @param {string} title          Page title, which cannot be null or the source
 *                                filters the visit out
 * @param {number} visitDateMs    Visit time in ms since epoch
 */
async function writePageVisitToMozPlaces(url, title, visitDateMs) {
  await PlacesUtils.history.insertMany([
    { url, title, visits: [{ date: new Date(visitDateMs) }] },
  ]);

  await PlacesUtils.withConnectionWrapper(
    "browser_MemoriesSchedulers-metadata",
    async db => {
      const rows = await db.execute(
        "SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url",
        { url }
      );
      await db.execute(
        `INSERT INTO moz_places_metadata
           (place_id, created_at, updated_at, total_view_time,
            typing_time, key_presses, scrolling_time, scrolling_distance, document_type)
         VALUES
           (:place_id, :created_at, :created_at, 30000, 0, 0, 0, 0, 0)`,
        {
          place_id: rows[0].getResultByName("id"),
          created_at: visitDateMs,
        }
      );
    }
  );
}

/**
 * Record a page visit and wait for the resulting "page-visited" notification to
 * have been dispatched, arming the scheduler's browsing trigger.
 *
 * @param {string} url  The URL to record a visit for
 */
async function visitPage(url) {
  const visited = new Promise(resolve => {
    const listener = () => {
      PlacesUtils.observers.removeListener(["page-visited"], listener);
      resolve();
    };
    PlacesUtils.observers.addListener(["page-visited"], listener);
  });
  await PlacesTestUtils.addVisits(url);
  await visited;
}

add_setup(async function () {
  await clearMemories();
  await PlacesUtils.history.clear();
  await ChatStore.deleteAllConversations();

  // The scheduler is a singleton shared with the rest of the browser. Drop any
  // instance an earlier test file left behind so each task here builds its own.
  MemoriesSchedulers.stop();

  // One mock for the whole file: MemoriesManager caches the generation
  // Conversation, so a per-task mock would leave later runs routed to an engine
  // whose _createEngine stub had already been restored.
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
    await clearMemories();
    await PlacesUtils.history.clear();
    await ChatStore.deleteAllConversations();
    // Stamped by AIWindow when the window becomes active.
    Services.prefs.clearUserPref(PREF_LAST_USAGE_TIME);
  });
});

/**
 * Run a task against a freshly constructed scheduler with every enablement gate
 * genuinely satisfied: a real AI Window is open, and the source, firstrun and
 * ToU consent prefs are really set.
 *
 * Maintenance is spied rather than stubbed, so it runs for real against the
 * MemoryStore and tasks can assert both that it was reached and what it did.
 * Generation and merging are the only stubs for now. Both are tested in
 * browser_MemoriesManager.js.
 *
 * Each task gets a new scheduler, so `#lastMaintenanceMs` always starts at 0
 * and the first tick is due for maintenance unless the task says otherwise.
 *
 * @param {object} options
 * @param {number} options.watermarkMs            Session-memory watermark to persist
 * @param {number} [options.lastGenerationMs]     Persisted cooldown clock. Left at 0
 *                                                so it seeds from `watermarkMs`; set
 *                                                it to drive the cooldown directly.
 * @param {boolean} [options.conversationEnabled] Whether the chat source is on
 * @param {boolean} [options.historyEnabled]      Whether the browsing source is on.
 *                                                Turn it off to attribute a run to
 *                                                the chat trigger alone.
 * @param {boolean} [options.stubGeneration]      Whether to stub generation out.
 *                                                Pass false to run the real pipeline,
 *                                                which a task must keep clear of the
 *                                                model. `generate` is then a spy.
 * @param {Function} task   Receives `{ scheduler, sandbox, maintenance, generate, merge,
 *                          journeyMaintenance, journeys }`
 */
async function withScheduler(
  {
    watermarkMs,
    lastGenerationMs = 0,
    conversationEnabled = false,
    historyEnabled = true,
    stubGeneration = true,
  },
  task
) {
  // Clear the browser state and set prefs clean
  await clearMemories();
  await JourneyStore.clear();
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_GENERATE_MEMORIES_FROM_HISTORY, historyEnabled],
      [PREF_GENERATE_MEMORIES_FROM_CONVERSATION, conversationEnabled],
      [PREF_FIRSTRUN_HAS_COMPLETED, true],
      [PREF_TOU_CONSENT_TIME, Math.floor(Date.now() / 1000)],
    ],
  });
  await MemoriesManager.setLastSessionMemoryTimestamp(watermarkMs);
  await MemoriesManager.setLastGenerationRunTimestamp(lastGenerationMs);

  const sandbox = sinon.createSandbox();
  const maintenance = sandbox.spy(MemoriesManager, "runMemoryMaintenance");
  const generate = stubGeneration
    ? sandbox
        .stub(MemoriesManager, "generateMemoriesFromSessions")
        .resolves([{}])
    : sandbox.spy(MemoriesManager, "generateMemoriesFromSessions");
  const merge = sandbox.stub(MemoriesManager, "mergeMemories").resolves();
  const journeyMaintenance = sandbox.spy(
    JourneyManager,
    "runJourneyMaintenance"
  );
  const journeys = sandbox.spy(JourneyManager, "generateAndPersistJourneys");

  // Restart the memories scheduler clean
  MemoriesSchedulers.stop();
  const scheduler = MemoriesSchedulers.maybeRunAndSchedule();
  Assert.ok(
    scheduler,
    "Scheduler should start with a real AI Window open and every pref set."
  );

  // Run the test task and reset after completed
  try {
    await task({
      scheduler,
      sandbox,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    });
  } finally {
    MemoriesSchedulers.stop();
    sandbox.restore();
    await JourneyStore.clear();
    await SpecialPowers.popPrefEnv();
    await MemoriesManager.setLastSessionMemoryTimestamp(0);
    await MemoriesManager.setLastGenerationRunTimestamp(0);
    await PlacesUtils.history.clear();
    await ChatStore.deleteAllConversations();
  }
}

/**
 * A full tick with the cooldown cleared and a real page visit pending runs
 * maintenance, journey maintenance, generation, merging and journey generation,
 * in that order, and the maintenance pass actually ages the store.
 */
add_task(async function test_full_run_order() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({
      scheduler,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    }) => {
      const memory = await addDueMemory();
      await visitPage("https://example.com/scheduler-tick");

      await scheduler.runNowForTesting();

      Assert.ok(maintenance.calledOnce, "Maintenance should have run.");
      Assert.ok(generate.calledOnce, "Generation should have run.");
      Assert.ok(merge.calledOnce, "Merging should have run.");
      Assert.ok(journeys.calledOnce, "Journey generation should have run.");
      sinon.assert.callOrder(
        maintenance,
        journeyMaintenance,
        generate,
        merge,
        journeys
      );
      Assert.ok(
        journeys.firstCall.args[0].some(({ id }) => id === memory.id),
        "Journey generation should receive the live memories."
      );

      Assert.greater(
        await getUpdatedAt(memory.id),
        memory.updatedAt,
        "Maintenance should have aged the memory for real."
      );
    }
  );
});

/**
 * Maintenance is on its own clock, so it runs even when the generation cooldown
 * has not elapsed.
 */
add_task(async function test_maintenance_runs_when_cooldown_unmet() {
  await withScheduler(
    // Generation watermark set to `now`, so generation should not run, only maintenance
    { watermarkMs: Date.now() },
    async ({
      scheduler,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    }) => {
      const memory = await addDueMemory();
      await visitPage("https://example.com/cooldown-unmet");

      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should run despite the generation cooldown."
      );
      Assert.ok(
        generate.notCalled,
        "Generation should be held by the cooldown."
      );
      Assert.ok(merge.notCalled, "Merging should be held by the cooldown.");
      Assert.ok(
        journeyMaintenance.calledOnce,
        "Journey maintenance should run despite the generation cooldown."
      );
      Assert.ok(
        journeys.notCalled,
        "Journey generation should be held by the cooldown."
      );

      Assert.greater(
        await getUpdatedAt(memory.id),
        memory.updatedAt,
        "The memory should have been aged while generation was held."
      );
    }
  );
});

/**
 * Maintenance also ignores the evidence accumulation check that gates
 * generation and merging.
 */
add_task(async function test_maintenance_runs_when_no_trigger_met() {
  await withScheduler(
    // Generation watermark set long enough that the gate should pass
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({
      scheduler,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    }) => {
      const memory = await addDueMemory();
      await JourneyStore.addOrUpdateJourney({
        memoryId: "orphan",
        headline: "Orphan",
        status: "Started",
      });

      // No page visited and the chat source is off, so generation shouldn't run
      // despite the watermark being old enough
      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should run with no generation trigger met."
      );
      Assert.ok(generate.notCalled, "Generation needs a trigger.");
      Assert.ok(merge.notCalled, "Merging needs a trigger.");
      Assert.ok(journeys.notCalled, "Journey generation needs a trigger.");
      Assert.ok(
        journeyMaintenance.calledOnce,
        "Journey maintenance should run with no generation trigger met."
      );

      Assert.greater(
        await getUpdatedAt(memory.id),
        memory.updatedAt,
        "The memory should have been aged with no trigger armed."
      );
      Assert.equal(
        await JourneyStore.get("orphan"),
        null,
        "The orphaned journey should have been removed with no trigger armed."
      );
    }
  );
});

/**
 * Maintenance should run based on its own clock across scheduler ticks. Make
 * sure maintenance doesn't trigger on ticks within its cooldown.
 */
add_task(async function test_maintenance_skipped_within_interval() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({ scheduler, maintenance, journeyMaintenance }) => {
      await addDueMemory();

      await scheduler.runNowForTesting();
      await scheduler.runNowForTesting();
      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should run once across three back-to-back ticks."
      );
      Assert.ok(
        journeyMaintenance.calledOnce,
        "Journey maintenance should run once across three back-to-back ticks."
      );
    }
  );
});

/**
 * Once the maintenance interval has elapsed, the next tick runs it again.
 */
add_task(async function test_maintenance_runs_again_after_interval() {
  await withScheduler(
    { watermarkMs: Date.now() },
    async ({ scheduler, maintenance, journeyMaintenance }) => {
      await addDueMemory();

      await scheduler.runNowForTesting();
      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should run on first tick."
      );

      // Set maintenance watermark beyond check threshold, so it runs again
      scheduler.setLastMaintenanceMsForTesting(
        Date.now() - MAINTENANCE_INTERVAL_MS - HOUR_MS
      );
      await scheduler.runNowForTesting();

      Assert.equal(
        maintenance.callCount,
        2,
        "Maintenance should run again once its interval has elapsed."
      );
      Assert.equal(
        journeyMaintenance.callCount,
        2,
        "Journey maintenance should run again once its interval has elapsed."
      );
    }
  );
});

/**
 * Maintenance stays behind the LLM 429 backoff gate. It makes no LLM call, but a
 * backed-off scheduler is deliberately left fully idle.
 */
add_task(async function test_maintenance_skipped_during_backoff() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({
      scheduler,
      maintenance,
      generate,
      journeyMaintenance,
      journeys,
    }) => {
      const memory = await addDueMemory();
      await visitPage("https://example.com/backoff");

      // Set the 429 backoff so maintenance, generation, and merging should not run
      scheduler.setBackoffUntilMsForTesting(Date.now() + HOUR_MS);

      await scheduler.runNowForTesting();

      Assert.ok(maintenance.notCalled, "Backoff should skip maintenance.");
      Assert.ok(
        journeyMaintenance.notCalled,
        "Backoff should skip journey maintenance."
      );
      Assert.ok(generate.notCalled, "Backoff should skip generation.");
      Assert.ok(journeys.notCalled, "Backoff should skip journey generation.");

      Assert.equal(
        await getUpdatedAt(memory.id),
        memory.updatedAt,
        "The memory should be untouched while backed off."
      );

      scheduler.setBackoffUntilMsForTesting(0);
      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should resume once the backoff clears."
      );
      Assert.ok(
        journeyMaintenance.calledOnce,
        "Journey maintenance should resume once the backoff clears."
      );
    }
  );
});

/**
 * The watermark is the last timestamp already processed, so the chat trigger must
 * read strictly past it or pull messages that were included in the last processed
 * session.
 */
add_task(async function test_chat_pull_excludes_watermark_boundary() {
  const boundaryMs = Date.now() - 10 * HOUR_MS;
  const laterMs = boundaryMs + HOUR_MS;
  const boundaryBody = "Planning a trip to Lisbon in April";
  const laterBody = "Looking for hotels near Alfama";

  await withScheduler(
    {
      watermarkMs: boundaryMs,
      conversationEnabled: true,
      // Chat is the only armed trigger, so the pull can only come from it.
      historyEnabled: false,
      stubGeneration: false,
    },
    async ({ scheduler, sandbox, generate }) => {
      await addChatMessage(boundaryMs, boundaryBody);
      await addChatMessage(laterMs, laterBody);

      const findMessagesByDate = sandbox.spy(ChatStore, "findMessagesByDate");

      const tick = scheduler.runNowForTesting();

      const turn = await gMockEngineManager.captureRequest({
        purpose: PURPOSES.MEMORY_GENERATION,
      });

      Assert.ok(
        generate.calledOnce,
        "A genuinely new message should still trigger generation."
      );

      Assert.equal(
        findMessagesByDate.callCount,
        2,
        "The trigger and generation should each have pulled chats."
      );
      for (const call of findMessagesByDate.getCalls()) {
        Assert.equal(
          call.args[0].getTime(),
          boundaryMs + 1,
          "Every chat pull should start one ms past the watermark."
        );
        Assert.deepEqual(
          (await call.returnValue).map(message => message.content?.body),
          [laterBody],
          "Only the message past the watermark should be pulled; the one exactly on the watermark was already consumed."
        );
      }

      const serializedArgs = JSON.stringify(turn.request.args);
      Assert.ok(
        serializedArgs.includes(laterBody),
        "The prompt should carry the message past the watermark."
      );
      Assert.ok(
        !serializedArgs.includes(boundaryBody),
        "The prompt should not carry the message sitting exactly on the watermark."
      );

      // No candidates, so the pipeline returns before the filter step and makes
      // no further request.
      turn.respond("[]");
      await tick;

      Assert.equal(
        await MemoriesManager.getLastSessionMemoryTimestamp(),
        laterMs,
        "The watermark should have advanced to exactly the last processed message."
      );

      gMockEngineManager.assertAllRequestsHandled();
    }
  );
});

/**
 * The watermark is the last timestamp already processed, so the browsing trigger must
 * read strictly past it or pull webpages that were included in the last processed
 * session.
 */
add_task(async function test_history_pull_excludes_watermark_boundary() {
  const boundaryMs = Date.now() - 10 * HOUR_MS;
  const laterMs = boundaryMs + 60 * 1000;
  const boundaryUrl = "https://example.com/sourdough-starter";
  const laterUrl = "https://example.com/cast-iron-skillet";
  const boundaryTitle = "Sourdough starter guide";
  const laterTitle = "Cast iron skillet care";

  await withScheduler(
    {
      watermarkMs: boundaryMs,
      conversationEnabled: false,
      historyEnabled: true,
      stubGeneration: false,
    },
    async ({ scheduler, sandbox, generate }) => {
      await writePageVisitToMozPlaces(boundaryUrl, boundaryTitle, boundaryMs);
      await writePageVisitToMozPlaces(laterUrl, laterTitle, laterMs);

      const getRecentHistory = sandbox.spy(
        MemoriesManager,
        "_getRecentHistory"
      );

      // Arm the browsing trigger. This page gets no moz_places_metadata row, so
      // it cannot turn up in the pull being asserted on.
      await visitPage("https://example.com/history-boundary-trigger");

      const tick = scheduler.runNowForTesting();

      const turn = await gMockEngineManager.captureRequest({
        purpose: PURPOSES.MEMORY_GENERATION,
      });

      Assert.ok(generate.calledOnce, "Generation should have run for real.");
      Assert.ok(
        getRecentHistory.calledOnce,
        "The run should have made exactly one history pull."
      );

      const pulled = await getRecentHistory.firstCall.returnValue;
      Assert.deepEqual(
        pulled.map(row => row.url),
        [laterUrl],
        "Only the visit past the watermark should be pulled; the one exactly on the watermark was already processed."
      );

      Assert.equal(
        getRecentHistory.firstCall.args[0].sinceMicros,
        (boundaryMs + 1) * 1000,
        "The pull should start one ms past the watermark."
      );

      const serializedArgs = JSON.stringify(turn.request.args);
      Assert.ok(
        serializedArgs.includes(laterTitle),
        "The prompt should carry the visit past the watermark."
      );
      Assert.ok(
        !serializedArgs.includes(boundaryTitle),
        "The prompt should not carry the visit sitting exactly on the watermark."
      );

      // No candidates, so the pipeline returns before the filter step and makes
      // no further request.
      turn.respond("[]");
      await tick;

      Assert.equal(
        await MemoriesManager.getLastSessionMemoryTimestamp(),
        laterMs,
        "The watermark should have advanced to exactly the last processed visit."
      );

      gMockEngineManager.assertAllRequestsHandled();
    }
  );
});

/**
 * A failing maintenance pass is contained: generation still runs, and the
 * attempt is stamped so the next tick does not immediately retry.
 */
add_task(async function test_maintenance_failure_is_contained() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({
      scheduler,
      sandbox,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    }) => {
      // Stub getMemories to simulate a broken maintenance run
      sandbox
        .stub(MemoryStore, "getMemories")
        .rejects(new Error("Memory store unavailable"));
      await visitPage("https://example.com/maintenance-failure");

      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "Maintenance should have been attempted."
      );
      Assert.ok(
        generate.calledOnce,
        "A maintenance failure should not block generation."
      );
      Assert.ok(
        merge.calledOnce,
        "A maintenance failure should not block merging."
      );
      Assert.ok(
        journeyMaintenance.calledOnce,
        "A memory maintenance failure should not block journey maintenance."
      );
      // The broken store also fails the memory read that feeds journey generation.
      Assert.ok(
        journeys.notCalled,
        "Journey generation should not run when the memory store is unavailable."
      );

      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "A failed maintenance pass should not retry on the next tick but wait until enough time has elapsed based on its watermark."
      );
    }
  );
});

/**
 * A failed journey maintenance pass is contained: memory maintenance,
 * generation, merging and journey generation still run, and the failed attempt
 * is stamped so the next tick does not immediately retry.
 */
add_task(async function test_journey_maintenance_failure_is_contained() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({
      scheduler,
      sandbox,
      maintenance,
      generate,
      merge,
      journeyMaintenance,
      journeys,
    }) => {
      // Replace the spy with a stub to simulate a broken journey maintenance run
      journeyMaintenance.restore();
      const failingJourneyMaintenance = sandbox
        .stub(JourneyManager, "runJourneyMaintenance")
        .rejects(new Error("Journey store unavailable"));
      await addDueMemory();
      await visitPage("https://example.com/journey-maintenance-failure");

      await scheduler.runNowForTesting();

      Assert.ok(
        failingJourneyMaintenance.calledOnce,
        "Journey maintenance should have been attempted."
      );
      Assert.ok(
        maintenance.calledOnce,
        "A journey maintenance failure should not block memory maintenance."
      );
      Assert.ok(
        generate.calledOnce,
        "A journey maintenance failure should not block generation."
      );
      Assert.ok(
        merge.calledOnce,
        "A journey maintenance failure should not block merging."
      );
      Assert.ok(
        journeys.calledOnce,
        "A journey maintenance failure should not block journey generation."
      );

      await scheduler.runNowForTesting();

      Assert.ok(
        failingJourneyMaintenance.calledOnce,
        "A failed journey maintenance pass should not retry on the next tick."
      );
    }
  );
});

/**
 * The cooldown clock is persisted, so a scheduler standing up fresh the way it
 * would after a browser restart is still held by a run from before it existed.
 */
add_task(async function test_cooldown_survives_a_new_scheduler() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({ scheduler, generate, merge }) => {
      await visitPage("https://example.com/persisted-cooldown");

      await scheduler.runNowForTesting();
      Assert.ok(generate.calledOnce, "The first tick should generate.");

      Assert.greater(
        await MemoriesManager.getLastGenerationRunTimestamp(),
        await MemoriesManager.getLastSessionMemoryTimestamp(),
        "The run should be stamped at wall clock, ahead of the watermark the stubbed run left behind."
      );

      MemoriesSchedulers.stop();
      const restarted = MemoriesSchedulers.maybeRunAndSchedule();
      // Arm the new instance's own trigger, so only the cooldown can hold it.
      await visitPage("https://example.com/persisted-cooldown-restart");
      await restarted.runNowForTesting();

      Assert.ok(
        generate.calledOnce,
        "A restarted scheduler should still be held by the persisted cooldown, and generation should not be called again."
      );
      Assert.ok(
        merge.calledOnce,
        "Merging should be held with generation, and merge should not be called again."
      );
    }
  );
});

/**
 * Generation resumes once the persisted run time has aged out, even though the
 * watermark says the last processed event was a moment ago.
 */
add_task(async function test_generation_resumes_when_persisted_clock_elapses() {
  await withScheduler(
    {
      watermarkMs: Date.now(),
      lastGenerationMs: Date.now() - COOLDOWN_MS - HOUR_MS,
    },
    async ({ scheduler, generate }) => {
      await visitPage("https://example.com/cooldown-elapsed");

      await scheduler.runNowForTesting();

      Assert.ok(
        generate.calledOnce,
        "A fresh watermark should not hold generation once the run time has aged out."
      );
    }
  );
});

/**
 * A tick that finds no evidence stamps the cooldown clock, so the scheduler
 * waits out a full cooldown before looking again instead of re-checking every
 * two minutes. Accumulated browsing evidence survives that wait.
 */
add_task(async function test_no_trigger_stamps_the_cooldown_clock() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({ scheduler, generate }) => {
      const beforeTickMs = Date.now();

      // Cooldown elapsed, but nothing visited and the chat source is off.
      await scheduler.runNowForTesting();

      Assert.ok(generate.notCalled, "Generation needs a trigger.");
      Assert.greaterOrEqual(
        await MemoriesManager.getLastGenerationRunTimestamp(),
        beforeTickMs,
        "A tick with no trigger met should stamp the persisted cooldown clock, so the wait survives a restart."
      );

      // Add a visit to check the cooldown clock is respected
      await visitPage("https://example.com/no-trigger-then-visit");
      await scheduler.runNowForTesting();

      Assert.ok(
        generate.notCalled,
        "Evidence arriving right after a no-trigger tick should be held by the freshly stamped cooldown. Generation should not trigger."
      );

      // Age the clock out the way four hours of ticks would.
      scheduler.setLastGenerationMsForTesting(
        Date.now() - COOLDOWN_MS - HOUR_MS
      );
      await scheduler.runNowForTesting();

      Assert.ok(
        generate.calledOnce,
        "The page visited during the wait should still be pending, and generation should run once the cooldown clears."
      );
    }
  );
});

/**
 * A retryable generation failure leaves the clock unstamped, so the run stays
 * governed by the short transient backoff instead of the four-hour cooldown.
 */
add_task(async function test_retryable_failure_leaves_the_clock_unstamped() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({ scheduler, generate, merge, journeys }) => {
      generate.rejects({ status: 503 });
      await addDueMemory();
      await visitPage("https://example.com/retryable-failure");

      await scheduler.runNowForTesting();

      Assert.ok(generate.calledOnce, "Generation should have been attempted.");
      Assert.ok(merge.notCalled, "A failed generation should not merge.");
      Assert.ok(
        journeys.notCalled,
        "A failed generation should not generate journeys."
      );
      Assert.equal(
        (await MemoryStore.getMeta()).last_generation_run_ts,
        0,
        "A retryable failure should not stamp the persisted cooldown clock."
      );

      generate.resolves([{}]);
      scheduler.setBackoffUntilMsForTesting(0);
      await visitPage("https://example.com/retryable-retry");
      await scheduler.runNowForTesting();

      Assert.equal(
        generate.callCount,
        2,
        "Generation should retry once the transient backoff clears rather than wait out the cooldown."
      );
      Assert.ok(
        journeys.calledOnce,
        "Journey generation should run once generation succeeds."
      );
    }
  );
});

/**
 * A retryable journey generation failure puts the scheduler into the transient
 * backoff, but the generation clock is already stamped so memories are not
 * regenerated when the backoff clears.
 */
add_task(async function test_retryable_journey_failure_backs_off() {
  await withScheduler(
    { watermarkMs: Date.now() - COOLDOWN_MS - HOUR_MS },
    async ({ scheduler, sandbox, maintenance, generate, merge, journeys }) => {
      // Replace the spy with a stub to simulate a retryable journey failure
      journeys.restore();
      const failingJourneys = sandbox
        .stub(JourneyManager, "generateAndPersistJourneys")
        .rejects({ status: 503 });
      await addDueMemory();
      await visitPage("https://example.com/retryable-journey-failure");

      await scheduler.runNowForTesting();

      Assert.ok(generate.calledOnce, "Generation should have run.");
      Assert.ok(merge.calledOnce, "Merging should have run.");
      Assert.ok(
        failingJourneys.calledOnce,
        "Journey generation should have been attempted."
      );
      Assert.greater(
        (await MemoryStore.getMeta()).last_generation_run_ts,
        0,
        "A journey failure should not leave the generation clock unstamped."
      );

      scheduler.setLastMaintenanceMsForTesting(0);
      await scheduler.runNowForTesting();

      Assert.ok(
        maintenance.calledOnce,
        "The transient backoff should skip the next tick."
      );

      scheduler.setBackoffUntilMsForTesting(0);
      await scheduler.runNowForTesting();

      Assert.ok(
        generate.calledOnce,
        "Memories should not be regenerated after a journey failure."
      );
    }
  );
});
