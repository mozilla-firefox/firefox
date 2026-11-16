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
const {
  PREF_GENERATE_MEMORIES_FROM_HISTORY,
  PREF_GENERATE_MEMORIES_FROM_CONVERSATION,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/MemoriesConstants.sys.mjs"
);
const { AIWindowTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

const PREF_FIRSTRUN_HAS_COMPLETED = "browser.smartwindow.firstrun.hasCompleted";
const PREF_TOU_CONSENT_TIME = "browser.smartwindow.tos.consentTime";
const PREF_LAST_USAGE_TIME = "browser.smartwindow.lastSmartWindowUsageTime";

const HOUR_MS = 60 * 60 * 1000;
const DAY_MS = 24 * HOUR_MS;

let gAIWindow;
let gMemorySeq = 0;

/**
 * Empty every store a maintenance run reads from or writes to.
 */
async function resetStores() {
  const memories = await MemoryStore.getMemories({ includeSoftDeleted: true });
  for (const memory of memories) {
    await MemoryStore.hardDeleteMemory(memory.id);
  }
  await JourneyStore.clear();
}

/**
 * Store a memory with the attributes maintenance reads.
 *
 * Maintenance ages a memory when `agedDaysAgo` >= 1, then recomputes its
 * strength, mostly from `accessCount` and `mergeCount` (both decaying with
 * days since last use and since creation), and sets its type from strength
 * and age (MEMORY_TYPE_TIERS). It decays when exp(-t / strength) <=
 * MEMORY_DECAY_THRESHOLD, t being days since last use, or since creation
 * when never used.
 *
 * @param {object} [options]
 * @param {number} [options.ageDays]              Days since the memory was created
 * @param {number} [options.agedDaysAgo]          Days since maintenance last aged it
 * @param {?number} [options.lastAccessedDaysAgo] Days since it was last used, or
 *                                                null if never used
 * @param {string} [options.type]                 Stored type before maintenance
 * @param {number} [options.accessCount]          Lifetime access count
 * @param {number} [options.mergeCount]           Times it was merged into
 * @param {boolean} [options.isDeleted]           Whether the user deleted it
 * @returns {Promise<object>} The stored memory.
 */
async function seedMemory({
  ageDays = 30,
  agedDaysAgo = ageDays,
  lastAccessedDaysAgo = null,
  type,
  accessCount = 0,
  mergeCount = 0,
  isDeleted = false,
} = {}) {
  const now = Date.now();
  return MemoryStore.addMemory({
    memory_summary: `Maintenance test memory ${gMemorySeq++}`,
    created_at: now - ageDays * DAY_MS,
    updated_at: now - agedDaysAgo * DAY_MS,
    last_accessed:
      lastAccessedDaysAgo === null ? null : now - lastAccessedDaysAgo * DAY_MS,
    type,
    lifetime_accessed_count: accessCount,
    merge_count: mergeCount,
    is_deleted: isDeleted,
  });
}

/**
 * Store a journey for a memory.
 *
 * @param {string} memoryId                 ID of the memory the journey belongs
 *                                          to, which doesn't have to exist
 * @param {object} [options]
 * @param {boolean} [options.dismissed]     Whether the user dismissed it for good
 * @param {number} [options.dismissedUntil] Time it is snoozed until, in ms
 */
async function seedJourney(memoryId, { dismissed, dismissedUntil } = {}) {
  await JourneyStore.addOrUpdateJourney({
    memoryId,
    headline: "Pick up where you left off",
    status: "Started",
    ...(dismissed === undefined ? {} : { dismissed }),
    ...(dismissedUntil === undefined
      ? {}
      : { dismissed_until: dismissedUntil }),
  });
}

add_setup(async function () {
  await resetStores();

  // The scheduler is a singleton shared with the rest of the browser. Stop
  // it in case an earlier test or file left it running.
  MemoriesSchedulers.stop();

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
 * Run a task against a fresh scheduler whose ticks run maintenance only.
 *
 * Generation is kept out of the way by a generation cooldown that has not
 * elapsed, so no model request is made. A fresh scheduler is due for
 * maintenance on its first tick.
 *
 * @param {Function} task   Receives `{ scheduler }`
 */
async function withMaintenanceRun(task) {
  await resetStores();

  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_GENERATE_MEMORIES_FROM_HISTORY, true],
      [PREF_GENERATE_MEMORIES_FROM_CONVERSATION, false],
      [PREF_FIRSTRUN_HAS_COMPLETED, true],
      [PREF_TOU_CONSENT_TIME, Math.floor(Date.now() / 1000)],
    ],
  });

  // A non-zero watermark keeps the scheduler from generating as soon as it
  // starts, and a generation run just now keeps the cooldown from elapsing.
  await MemoriesManager.setLastSessionMemoryTimestamp(Date.now() - HOUR_MS);
  await MemoriesManager.setLastGenerationRunTimestamp(Date.now());
  Services.fog.testResetFOG();

  MemoriesSchedulers.stop();
  const scheduler = MemoriesSchedulers.maybeRunAndSchedule();
  Assert.ok(
    scheduler,
    "Scheduler should start with a real AI Window open and every pref set."
  );

  try {
    await task({ scheduler });
    Assert.equal(
      Glean.smartWindow.memoriesGenerationRun.testGetValue(),
      null,
      "Generation should not have been reached."
    );
  } finally {
    MemoriesSchedulers.stop();
    await SpecialPowers.popPrefEnv();
    await MemoriesManager.setLastSessionMemoryTimestamp(0);
    await MemoriesManager.setLastGenerationRunTimestamp(0);
    await resetStores();
  }
}

/**
 * Tests a run over an empty store records every count as zero.
 */
add_task(async function test_empty_store() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await scheduler.runNowForTesting();

    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_before: 0,
      soft_deleted_removed: 0,
      memories_skipped: 0,
      memories_aged: 0,
      promoted_to_long_term: 0,
      promoted_to_durable: 0,
      demoted_to_long_term: 0,
      demoted_to_short_term: 0,
      memories_decayed: 0,
      memories_after: 0,
      outcome: "success",
    });
  });
});

// Seed values for memories maintenance treats predictably. Strengths are
// before the tier caps for memories under MEMORY_TYPE_TIERS' minimum ages.
// Used a day ago with no accesses: strength 4, short term, kept.
const KEPT = { lastAccessedDaysAgo: 1 };
// Never used in 30 days: strength 4, decays.
const DECAYED = { ageDays: 30 };
// Used a day ago with 25 accesses: strength ~28, long term from 14 days old.
const LONG_TERM_STRENGTH = { lastAccessedDaysAgo: 1, accessCount: 25 };
// Used a day ago with 100 accesses: strength ~52, durable from 45 days old.
const DURABLE_STRENGTH = { lastAccessedDaysAgo: 1, accessCount: 100 };

/**
 * Tests a run over one memory of each kind records every count.
 */
add_task(async function test_full_run() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await seedMemory({ isDeleted: true }); // Purged
    await seedMemory({ ...KEPT, agedDaysAgo: 0.5 }); // Skipped
    await seedMemory(KEPT); // Aged, no type change
    await seedMemory(DECAYED); // Aged, then decayed
    await seedMemory({ ...LONG_TERM_STRENGTH, ageDays: 20 }); // Promoted to long term

    await scheduler.runNowForTesting();

    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_before: 5, // 5 memories seeded
      soft_deleted_removed: 1, // 1 flagged for soft deletion, so removed
      memories_skipped: 1, // 1 maintained already today
      memories_aged: 3, // Remaining 3 memories aged in this run
      promoted_to_long_term: 1, // 1 promoted to long term
      promoted_to_durable: 0,
      demoted_to_long_term: 0,
      demoted_to_short_term: 0,
      memories_decayed: 1, // 1 deleted due to decay
      memories_after: 3, // 3 memories left after the 1 soft deletion and 1 decay
      outcome: "success",
    });
    Assert.equal(
      Glean.smartWindow.memoryRemovedPanel.testGetValue(),
      null,
      "Maintenance deletions should not be recorded as user removals."
    );
  });
});

/**
 * Tests every type change is counted under its direction and target type.
 */
add_task(async function test_type_changes() {
  await withMaintenanceRun(async ({ scheduler }) => {
    const seeded = [
      [{ ...LONG_TERM_STRENGTH, ageDays: 20 }, "long_term_memory"], // Promotion: Short term to long term
      [{ ...DURABLE_STRENGTH, ageDays: 60 }, "durable_memory"], // Promotion: Short term to durable
      [
        { ...DURABLE_STRENGTH, ageDays: 60, type: "long_term_memory" },
        "durable_memory",
      ], // Promotion: Long term to durable
      [
        { ...LONG_TERM_STRENGTH, ageDays: 60, type: "durable_memory" },
        "long_term_memory",
      ], // Demotion: Durable to long term
      [{ ...KEPT, ageDays: 60, type: "durable_memory" }, "short_term_memory"], // Demotion: Durable to short term
      [{ ...KEPT, ageDays: 60, type: "long_term_memory" }, "short_term_memory"], // Demotion: Long term to short term
    ];
    const ids = [];
    for (const [options] of seeded) {
      ids.push((await seedMemory(options)).id);
    }

    await scheduler.runNowForTesting();

    const types = new Map(
      (await MemoryStore.getMemories()).map(memory => [memory.id, memory.type])
    );
    Assert.deepEqual(
      ids.map(id => types.get(id)),
      seeded.map(([, expectedType]) => expectedType),
      "Every memory should have moved to its expected type."
    );
    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_before: 6, // 6 memories seeded
      soft_deleted_removed: 0,
      memories_skipped: 0,
      memories_aged: 6,
      promoted_to_long_term: 1,
      promoted_to_durable: 2,
      demoted_to_long_term: 1,
      demoted_to_short_term: 2,
      memories_decayed: 0,
      memories_after: 6, // No memories deleted, just type changes
      outcome: "success",
    });
  });
});

/**
 * Tests a memory whose type changes but which then decays is counted as
 * decayed only.
 */
add_task(async function test_decayed_type_change_not_counted() {
  await withMaintenanceRun(async ({ scheduler }) => {
    // Would be demoted from durable to short term, but never used in 60 days.
    await seedMemory({ ageDays: 60, type: "durable_memory" });

    await scheduler.runNowForTesting();

    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_before: 1, // 1 memory seeded
      soft_deleted_removed: 0,
      memories_skipped: 0,
      memories_aged: 1, // Memory is aged
      promoted_to_long_term: 0,
      promoted_to_durable: 0,
      demoted_to_long_term: 0,
      demoted_to_short_term: 0, // Memory would be counted here, but is decayed instead
      memories_decayed: 1, // Counted as a decay rather than a demotion
      memories_after: 0,
      outcome: "success",
    });
  });
});

/**
 * Tests a failure partway through ageing keeps the counts of memories reached
 * and leaves the results of ageing absent.
 */
add_task(async function test_ageing_failed_partway() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await seedMemory({ isDeleted: true });
    await seedMemory({ ...KEPT, agedDaysAgo: 0.5 });
    await seedMemory({ ...DECAYED, agedDaysAgo: 30 });
    await seedMemory({ ...KEPT, ageDays: 40, agedDaysAgo: 40 });

    // The purge deletes for real; the decay delete fails by stubbing the 2nd call.
    const sandbox = sinon.createSandbox();
    const hardDelete = sandbox.stub(MemoryStore, "hardDeleteMemory");
    hardDelete.callThrough();
    hardDelete.onSecondCall().rejects(new Error("simulated delete failure"));
    try {
      await scheduler.runNowForTesting();
    } finally {
      sandbox.restore();
    }

    Assert.equal(hardDelete.callCount, 2, "The run should stop on the decay.");
    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_before: 4, // 4 memories seeded
      soft_deleted_removed: 1, // 1st memory soft deletes
      memories_skipped: 1, // 2nd memory already aged today
      memories_aged: 1, // 2nd memory ages now
      memories_after: 3, // 3 memories left
      outcome: "error", // Record an error because the 3rd memory failed its deletion
      error_type: "firefox_internal",
    });
  });
});

/**
 * Tests the scheduler records an error maintenance never got to record itself.
 */
add_task(async function test_error_outside_maintenance() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await seedMemory(KEPT);

    // Fails before maintenance fills in any stats.
    const sandbox = sinon.createSandbox();
    const maintenance = sandbox
      .stub(MemoriesManager, "runMemoryMaintenance")
      .rejects(new Error("simulated failure outside maintenance"));
    try {
      await scheduler.runNowForTesting();
    } finally {
      sandbox.restore();
    }

    Assert.ok(maintenance.calledOnce, "Maintenance should have been called.");
    assertGenerationRunEvent(Glean.smartWindow.memoriesMaintenanceRun, {
      memories_after: 1, // The seeded memory, untouched
      outcome: "error", // Set by the scheduler's fallback
      error_type: "firefox_internal",
    });
  });
});

/**
 * Tests a journey maintenance run with no stored journeys records every
 * count as zero.
 */
add_task(async function test_journeys_empty() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await scheduler.runNowForTesting();

    assertGenerationRunEvent(Glean.smartWindow.journeysMaintenanceRun, {
      journeys_before: 0,
      journeys_removed: 0,
      journeys_after: 0,
      journeys_eligible_after: 0,
      outcome: "success",
    });
  });
});

/**
 * Tests journeys whose memory is gone are removed, including one whose memory
 * decays in the memories maintenance run just before.
 */
add_task(async function test_journeys_removed() {
  await withMaintenanceRun(async ({ scheduler }) => {
    const live = await seedMemory(KEPT);
    const dismissed = await seedMemory(KEPT);
    const snoozed = await seedMemory(KEPT);
    const decayed = await seedMemory(DECAYED);
    await seedJourney(live.id); // Kept and can be suggested
    await seedJourney(dismissed.id, { dismissed: true }); // Kept, suppressed
    await seedJourney(snoozed.id, { dismissedUntil: Date.now() + DAY_MS }); // Kept, suppressed
    await seedJourney("missing-memory"); // Removed: no such memory
    await seedJourney(decayed.id); // Removed: memory decayed this tick

    await scheduler.runNowForTesting();

    assertGenerationRunEvent(Glean.smartWindow.journeysMaintenanceRun, {
      journeys_before: 5, // 5 journeys added
      journeys_removed: 2, // 2 removed because the memories are gone (dismissed & decayed)
      journeys_after: 3, // 3 journeys left
      journeys_eligible_after: 1, // 1 remaining as eligible to be suggested (live)
      outcome: "success",
    });
  });
});

/**
 * Tests no journey maintenance run is recorded for a locale journeys don't
 * support, though maintenance still runs.
 */
add_task(async function test_journeys_unsupported_locale() {
  const originalAvailable = Services.locale.availableLocales;
  const originalRequested = Services.locale.requestedLocales;

  const unsetPrefs = [
    "intl.locale.requested",
    "extensions.getAddons.cache.lastLocale",
  ].filter(pref => !Services.prefs.prefHasUserValue(pref));
  try {
    Services.locale.availableLocales = ["en-US", "de"];
    Services.locale.requestedLocales = ["de"];

    await withMaintenanceRun(async ({ scheduler }) => {
      await seedJourney("missing-memory");

      await scheduler.runNowForTesting();

      Assert.equal(
        Glean.smartWindow.journeysMaintenanceRun.testGetValue(),
        null,
        "No journeys maintenance run is recorded for an unsupported locale."
      );
      Assert.deepEqual(
        await JourneyStore.getAll(),
        [],
        "Journey maintenance should still have removed the orphaned journey."
      );
      Assert.equal(
        getGenerationRunEvents(Glean.smartWindow.memoriesMaintenanceRun).length,
        1,
        "The memories maintenance run is still recorded."
      );
    });
  } finally {
    Services.locale.requestedLocales = originalRequested;
    Services.locale.availableLocales = originalAvailable;
    for (const pref of unsetPrefs) {
      Services.prefs.clearUserPref(pref);
    }
  }
});

/**
 * Tests a failure deleting a journey keeps the count read before it and
 * leaves the removed count absent.
 */
add_task(async function test_journeys_delete_failed() {
  await withMaintenanceRun(async ({ scheduler }) => {
    const live = await seedMemory(KEPT);
    await seedJourney(live.id); // Kept and can be suggested
    await seedJourney("missing-memory"); // Orphaned, so maintenance deletes it

    // The only delete, the orphan's, fails.
    const sandbox = sinon.createSandbox();
    const deleteJourney = sandbox
      .stub(JourneyStore, "deleteJourney")
      .rejects(new Error("simulated delete failure"));
    try {
      await scheduler.runNowForTesting();
    } finally {
      sandbox.restore();
    }

    Assert.ok(deleteJourney.calledOnce, "Deletion should be attempted.");
    assertGenerationRunEvent(Glean.smartWindow.journeysMaintenanceRun, {
      journeys_before: 2,
      journeys_after: 2, // Nothing was deleted
      journeys_eligible_after: 1, // Only 1 eligible journey left over
      outcome: "error",
      error_type: "firefox_internal",
    });
  });
});

/**
 * Tests a failure reading journeys leaves every count absent.
 */
add_task(async function test_journeys_read_failed() {
  await withMaintenanceRun(async ({ scheduler }) => {
    await seedJourney("missing-memory");

    // The after counts read the journey store too, so they fail as well.
    const sandbox = sinon.createSandbox();
    const getAll = sandbox
      .stub(JourneyStore, "getAll")
      .rejects(new Error("simulated read failure"));
    try {
      await scheduler.runNowForTesting();
    } finally {
      sandbox.restore();
    }

    Assert.ok(getAll.called, "Reading journeys should have been attempted.");
    assertGenerationRunEvent(Glean.smartWindow.journeysMaintenanceRun, {
      outcome: "error", // Top level throws, so no metrics can be recorded
      error_type: "firefox_internal",
    });
  });
});
