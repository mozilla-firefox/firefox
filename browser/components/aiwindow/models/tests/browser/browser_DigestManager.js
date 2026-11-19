/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DigestManager } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestManager.sys.mjs"
);
const { DigestStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
);
const { DIGEST_STATUS, DIGEST_SCHEDULE_TYPE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);
const { MEMORY_FILTER_COMPARATOR } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs"
);
const { MemoryStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
);

const path = PathUtils.join(
  PathUtils.profileDir,
  "memories",
  "digests.json.lz4"
);

const WEEKLY_SCHEDULE = {
  type: DIGEST_SCHEDULE_TYPE.WEEKLY,
  weekday: 3,
  hour: 7,
  minute: 15,
};

/**
 * Builds the fields required to create a digest, with optional overrides.
 *
 * @param {object} overrides - Optional digest attribute overrides
 * @returns {object}
 */
function makeDigestPartial(overrides = {}) {
  return {
    memory_id: "mem.0000abcd",
    title: "Tourist news in Lisbon",
    description: "Give me general tourism news from Lisbon.",
    schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 9, minute: 30 },
    ...overrides,
  };
}

/**
 * Resets the DigestStore and deletes the DigestStore file
 */
async function resetStore() {
  await DigestStore.testOnlyFlushAndResetCache();
  await IOUtils.remove(path, { ignoreAbsent: true });
}

add_setup(async function setup() {
  registerCleanupFunction(resetStore);
  await resetStore();
});

/**
 * Tests getDigests passes digest IDs and attribute filters to the store
 */
add_task(async function test_get_digests_passes_options_through() {
  const first = await DigestManager.createDigest(makeDigestPartial());
  const second = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });
  const third = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  Assert.equal(
    (await DigestManager.getDigests()).length,
    3,
    "No options returns every digest"
  );
  Assert.deepEqual(
    await DigestManager.getDigests({ digestIds: new Set([first.id]) }),
    [first],
    "getDigests passes digest IDs through to the store for filtering"
  );
  Assert.deepEqual(
    await DigestManager.getDigests({
      attributeFilters: [
        {
          field: "status",
          comparator: MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: DIGEST_STATUS.ENABLED,
        },
      ],
    }),
    [second, third],
    "getDigests passes attribute filters through to the store for filtering"
  );
  Assert.deepEqual(
    await DigestManager.getDigests({
      digestIds: new Set([first.id, second.id]),
      attributeFilters: [
        {
          field: "status",
          comparator: MEMORY_FILTER_COMPARATOR.EQUAL_TO,
          value: DIGEST_STATUS.ENABLED,
        },
      ],
    }),
    [second],
    "getDigests passes digest IDs and attribute filters through to the store for filtering"
  );

  await resetStore();
});

/**
 * Tests getDigestById returns the matching digest or null
 */
add_task(async function test_get_digest_by_id() {
  const created = await DigestManager.createDigest(makeDigestPartial());
  await DigestManager.createDigest(makeDigestPartial());

  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    created,
    "Returns the digest with that id"
  );
  Assert.strictEqual(
    await DigestManager.getDigestById("unknown"),
    null,
    "Returns null for an unknown id"
  );

  await resetStore();
});

/**
 * Tests getSuggestedDigests returns only suggested digests
 */
add_task(async function test_get_suggested_digests() {
  const firstSuggested = await DigestManager.createDigest(makeDigestPartial());
  await DigestManager.createDigest(makeDigestPartial(), { enableNow: true });
  const disabled = await DigestManager.createDigest(makeDigestPartial());
  await DigestManager.disableDigest(disabled.id);
  const secondSuggested = await DigestManager.createDigest(makeDigestPartial());

  Assert.deepEqual(
    await DigestManager.getSuggestedDigests(),
    [firstSuggested, secondSuggested],
    "Returns only suggested digests in stored order"
  );

  await resetStore();
  Assert.deepEqual(
    await DigestManager.getSuggestedDigests(),
    [],
    "No suggested digests in an empty store"
  );
});

/**
 * Tests getEnabledDigests returns only enabled digests
 */
add_task(async function test_get_enabled_digests() {
  const firstEnabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });
  await DigestManager.createDigest(makeDigestPartial());
  const disabled = await DigestManager.createDigest(makeDigestPartial());
  await DigestManager.disableDigest(disabled.id);
  const secondEnabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  Assert.deepEqual(
    await DigestManager.getEnabledDigests(),
    [firstEnabled, secondEnabled],
    "Returns only enabled digests in stored order"
  );

  await resetStore();
  Assert.deepEqual(
    await DigestManager.getEnabledDigests(),
    [],
    "No enabled digests in an empty store"
  );
});

/**
 * Tests getUnreadDigests returns only unread enabled digests
 */
add_task(async function test_get_unread_digests() {
  await DigestManager.createDigest(makeDigestPartial());
  const firstUnread = await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      last_run_time: 1000,
    })
  );
  await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.DISABLED,
      unread: true,
      last_run_time: 1000,
    })
  );
  await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      unread: false,
      last_run_time: 1000,
    })
  );
  const secondUnread = await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      last_run_time: 1000,
    })
  );

  Assert.deepEqual(
    (await DigestManager.getUnreadDigests()).map(d => d.id),
    [firstUnread.id, secondUnread.id],
    "Returns only unread enabled digests, in stored order"
  );

  await resetStore();
  Assert.deepEqual(
    await DigestManager.getUnreadDigests(),
    [],
    "No unread digests in an empty store"
  );
});

/**
 * Tests getDigestsByMemoryId returns that memory's digests of any status
 */
add_task(async function test_get_digests_by_memory_id() {
  const suggested = await DigestManager.createDigest(
    makeDigestPartial({ memory_id: "mem.first" })
  );
  await DigestManager.createDigest(
    makeDigestPartial({ memory_id: "mem.second" })
  );
  const enabled = await DigestManager.createDigest(
    makeDigestPartial({ memory_id: "mem.first" }),
    { enableNow: true }
  );
  const disabled = await DigestManager.createDigest(
    makeDigestPartial({ memory_id: "mem.first" })
  );
  await DigestManager.disableDigest(disabled.id);

  Assert.deepEqual(
    (await DigestManager.getDigestsByMemoryId("mem.first")).map(d => d.id),
    [suggested.id, enabled.id, disabled.id],
    "Returns every digest for that memory, in stored order"
  );
  Assert.deepEqual(
    await DigestManager.getDigestsByMemoryId("mem.unknown"),
    [],
    "Returns no digests for a memory without any"
  );
  await Assert.rejects(
    DigestManager.getDigestsByMemoryId(undefined),
    /TypeError/,
    "Rejects a missing memory ID"
  );

  await resetStore();
});

/**
 * Tests createDigest stores a suggested digest with defaults
 */
add_task(async function test_create_digest() {
  const partial = makeDigestPartial({ domains: ["timeout.com"] });
  const created = await DigestManager.createDigest(partial);

  Assert.ok(
    created.id,
    "createDigest's call through to the store generates an id"
  );
  Assert.equal(
    created.status,
    DIGEST_STATUS.SUGGESTED,
    "Defaults to suggested"
  );
  Assert.strictEqual(
    created.next_run_time,
    null,
    "New suggested digest is not scheduled"
  );
  for (const [field, value] of Object.entries(partial)) {
    Assert.deepEqual(created[field], value, `Stores ${field}`);
  }
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [created],
    "The digest is stored"
  );

  await DigestStore.testOnlyFlushAndResetCache();
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [created],
    "The digest persists after reloading from the file"
  );

  await resetStore();
});

/**
 * Tests createDigest with enableNow stores an enabled, scheduled digest rather than a suggested one
 */
add_task(async function test_create_digest_enable_now() {
  const before = Date.now();
  const created = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  Assert.equal(created.status, DIGEST_STATUS.ENABLED, "Status is enabled");
  Assert.greater(created.next_run_time, before, "Next run is after now");
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [created],
    "Only the enabled digest is stored"
  );

  const onDemand = await DigestManager.createDigest(
    makeDigestPartial({ schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND } }),
    { enableNow: true }
  );
  Assert.equal(
    onDemand.status,
    DIGEST_STATUS.ENABLED,
    "On-demand status is enabled"
  );
  Assert.strictEqual(
    onDemand.next_run_time,
    null,
    "On-demand is not scheduled"
  );

  Assert.deepEqual(
    await DigestManager.getDigests(),
    [created, onDemand],
    "Both the enabled digests are stored"
  );

  await DigestStore.testOnlyFlushAndResetCache();
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [created, onDemand],
    "Both digests persist after reloading from the file"
  );

  await resetStore();
});

/**
 * Tests createDigest rejects fields that cannot be set on creation
 */
add_task(async function test_create_digest_rejects_other_fields() {
  const invalidCreationFields = {
    id: "3b241101-e2bb-4255-8caf-4136c566a962",
    status: DIGEST_STATUS.ENABLED,
    unread: true,
    ai_tab_slug: "lisbon-news",
    last_run_time: 1000,
    last_read_time: 1000,
    next_run_time: 2000,
    created_at: 500,
    updated_at: 500,
    unknown_field: true,
    "": true,
  };
  for (const [field, value] of Object.entries(invalidCreationFields)) {
    await Assert.rejects(
      DigestManager.createDigest(makeDigestPartial({ [field]: value })),
      /TypeError/,
      `createDigest rejects ${field}`
    );
  }
  const withoutMemoryId = makeDigestPartial();
  delete withoutMemoryId.memory_id;
  await Assert.rejects(
    DigestManager.createDigest(withoutMemoryId),
    /TypeError: A digest requires a memory_id/,
    "createDigest rejects a missing memory_id"
  );
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [],
    "Rejected creations store nothing"
  );
});

/**
 * Tests editDigest changes and stores content fields
 */
add_task(async function test_edit_digest_content() {
  const created = await DigestManager.createDigest(makeDigestPartial());

  const edits = {
    title: "Food news in Lisbon",
    description: "Give me restaurant news from Lisbon.",
    domains: ["timeout.com"],
  };
  const edited = await DigestManager.editDigest(created.id, edits);
  for (const [field, value] of Object.entries(edits)) {
    Assert.deepEqual(edited[field], value, `Edits ${field}`);
    Assert.notDeepEqual(
      edited[field],
      created[field],
      `${field} differs from the original created digest`
    );
  }
  Assert.equal(
    created.id,
    edited.id,
    "Created and edited digest copies reference the same ID"
  );
  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    edited,
    "The edited version is stored"
  );

  await resetStore();
});

/**
 * Tests editDigest rejects fields that cannot be user-edited
 */
add_task(async function test_edit_digest_rejects_other_fields() {
  const created = await DigestManager.createDigest(makeDigestPartial());

  const invalidEditFields = {
    id: "3b241101-e2bb-4255-8caf-4136c566a962",
    memory_id: "mem.other",
    status: DIGEST_STATUS.ENABLED,
    unread: true,
    ai_tab_slug: "lisbon-news",
    last_run_time: 1000,
    last_read_time: 1000,
    next_run_time: 2000,
    created_at: 500,
    updated_at: 500,
    unknown_field: true,
    "": true,
  };
  for (const [field, value] of Object.entries(invalidEditFields)) {
    await Assert.rejects(
      DigestManager.editDigest(created.id, { [field]: value }),
      /TypeError/,
      `editDigest rejects ${field}`
    );
  }
  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    created,
    "Rejected edits change nothing"
  );

  await resetStore();
});

/**
 * Tests editDigest rejects invalid values for editable fields
 */
add_task(async function test_edit_digest_rejects_invalid_values() {
  const created = await DigestManager.createDigest(makeDigestPartial());

  const invalidEditValues = [
    { title: "" },
    { description: "  " },
    { domains: "timeout.com" },
    { domains: [""] },
  ];
  for (const edits of invalidEditValues) {
    await Assert.rejects(
      DigestManager.editDigest(created.id, edits),
      /TypeError/,
      `editDigest rejects ${JSON.stringify(edits)}`
    );
  }
  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    created,
    "Rejected edits change nothing"
  );

  await resetStore();
});

/**
 * Tests editDigest returns null for an unknown id
 */
add_task(async function test_edit_digest_unknown_id() {
  const digests = [
    await DigestManager.createDigest(makeDigestPartial()),
    await DigestManager.createDigest(makeDigestPartial(), { enableNow: true }),
  ];

  Assert.strictEqual(
    await DigestManager.editDigest("unknown", { title: "Unknown" }),
    null,
    "Returns null for an unknown id"
  );
  Assert.deepEqual(
    await DigestManager.getDigests(),
    digests,
    "Existing digests are unchanged"
  );

  await resetStore();
});

/**
 * Tests editDigest replaces the schedule of a digest of any status, and
 * schedules a next run only for an enabled digest on a daily or weekly
 * schedule
 */
add_task(async function test_edit_digest_schedule_by_status() {
  const suggested = await DigestManager.createDigest(makeDigestPartial());
  const disabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });
  await DigestManager.disableDigest(disabled.id);
  const enabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  for (const [status, digest] of [
    ["suggested", suggested],
    ["disabled", disabled],
  ]) {
    const updated = await DigestManager.editDigest(digest.id, {
      schedule: WEEKLY_SCHEDULE,
    });
    Assert.deepEqual(
      updated.schedule,
      WEEKLY_SCHEDULE,
      `editDigest replaces a ${status} digest's schedule`
    );
    Assert.notDeepEqual(
      updated.schedule,
      digest.schedule,
      `A ${status} digest's schedule differs from the original`
    );
    Assert.strictEqual(
      updated.next_run_time,
      null,
      `A ${status} digest's next_run_time stays unscheduled`
    );
  }

  const onDemandSchedule = { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND };
  const onDemand = await DigestManager.editDigest(enabled.id, {
    schedule: onDemandSchedule,
  });
  Assert.deepEqual(
    onDemand.schedule,
    onDemandSchedule,
    "editDigest replaces an enabled digest's schedule with on-demand"
  );
  Assert.notDeepEqual(
    enabled.schedule,
    onDemand.schedule,
    "Original and updated schedules are different"
  );
  Assert.strictEqual(
    onDemand.next_run_time,
    null,
    "Switching an enabled digest to on-demand clears the next run"
  );

  await resetStore();
});

/**
 * Tests editDigest without a schedule keeps an enabled digest's next run
 */
add_task(async function test_edit_digest_content_keeps_next_run() {
  const enabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  const edited = await DigestManager.editDigest(enabled.id, {
    title: "Food news in Lisbon",
  });
  Assert.strictEqual(
    edited.next_run_time,
    enabled.next_run_time,
    "An edit without a schedule keeps the next run"
  );
  Assert.deepEqual(
    edited.schedule,
    enabled.schedule,
    "An edit without a schedule keeps the schedule"
  );

  await resetStore();
});

/**
 * Tests editDigest does not recompute next_run_time when the edits include
 * a schedule update that is the same as the digest's existing schedule.
 */
add_task(async function test_edit_digest_same_schedule_keeps_next_run() {
  const enabled = await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      last_run_time: 1000,
      next_run_time: 2000,
    })
  );

  // Set up an edit with the exact same schedule as the existing digest
  const edited = await DigestManager.editDigest(enabled.id, {
    title: "Food news in Lisbon",
    description: enabled.description,
    schedule: Object.fromEntries(Object.entries(enabled.schedule).reverse()),
    domains: enabled.domains,
  });
  Assert.equal(edited.title, "Food news in Lisbon", "Edits the title");
  Assert.strictEqual(
    edited.next_run_time,
    2000,
    "Resubmitting the same schedule keeps the due next run"
  );

  await resetStore();
});

/**
 * Tests editDigest reschedules an enabled digest on its new schedule
 */
add_task(async function test_edit_digest_schedule_reschedules_enabled() {
  const enabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  const before = Date.now();
  const updated = await DigestManager.editDigest(enabled.id, {
    schedule: WEEKLY_SCHEDULE,
  });
  Assert.deepEqual(
    updated.schedule,
    WEEKLY_SCHEDULE,
    "editDigest replaces an enabled digest's schedule"
  );
  Assert.notDeepEqual(
    enabled.schedule,
    updated.schedule,
    "Original and updated schedules are different"
  );
  const nextRun = new Date(updated.next_run_time);
  Assert.greater(nextRun.getTime(), before, "Next run is after now");
  Assert.deepEqual(
    [nextRun.getDay(), nextRun.getHours(), nextRun.getMinutes()],
    [3, 7, 15],
    "Next run follows the new schedule"
  );
  Assert.deepEqual(
    await DigestManager.getDigestById(enabled.id),
    updated,
    "The new schedule is stored"
  );

  await resetStore();
});

/**
 * Tests editDigest rejects an invalid schedule without applying any of the
 * edits
 */
add_task(async function test_edit_digest_rejects_invalid_schedule() {
  const enabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });

  await Assert.rejects(
    DigestManager.editDigest(enabled.id, {
      title: "Weather in Lisbon",
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 25, minute: 0 },
    }),
    /TypeError/,
    "Rejects an invalid schedule"
  );
  Assert.deepEqual(
    await DigestManager.getDigestById(enabled.id),
    enabled,
    "A rejected schedule changes nothing, including other edits"
  );

  await resetStore();
});

/**
 * Tests createDigest with enableNow and a schedule edit on an enabled digest
 * each write to the store once
 */
add_task(async function test_scheduling_writes_once() {
  const addSpy = sinon.spy(DigestStore, "addDigest");
  const updateSpy = sinon.spy(DigestStore, "updateDigest");

  const enabled = await DigestManager.createDigest(makeDigestPartial(), {
    enableNow: true,
  });
  Assert.equal(addSpy.callCount, 1, "createDigest adds the digest once");
  Assert.equal(
    updateSpy.callCount,
    0,
    "createDigest with enableNow does not update after adding"
  );

  await DigestManager.editDigest(enabled.id, {
    title: "Food news in Lisbon",
    schedule: WEEKLY_SCHEDULE,
  });
  Assert.equal(
    updateSpy.callCount,
    1,
    "A schedule edit on an enabled digest updates once"
  );
  Assert.ok(
    Object.hasOwn(updateSpy.firstCall.args[1], "next_run_time"),
    "The single update includes the recomputed next run because of the updated schedule"
  );

  sinon.restore();
  await resetStore();
});

/**
 * Tests deleteDigest removes a digest once
 */
add_task(async function test_delete_digest() {
  const created = await DigestManager.createDigest(makeDigestPartial());

  Assert.ok(
    await DigestManager.deleteDigest(created.id),
    "Returns true when a digest is removed"
  );
  Assert.strictEqual(
    await DigestManager.getDigestById(created.id),
    null,
    "The digest is gone"
  );
  Assert.ok(
    !(await DigestManager.deleteDigest(created.id)),
    "Returns false when no digest has that id"
  );

  await resetStore();
});

/**
 * Tests store validation errors reach the caller unchanged
 */
add_task(async function test_invalid_input_rejects() {
  await Assert.rejects(
    DigestManager.createDigest(makeDigestPartial({ title: "" })),
    /TypeError/,
    "createDigest rejects an invalid field"
  );
  await Assert.rejects(
    DigestManager.getDigests({ digestIds: ["not-a-set"] }),
    /TypeError/,
    "getDigests rejects a malformed option"
  );
  Assert.deepEqual(
    await DigestManager.getDigests(),
    [],
    "Rejected writes store nothing"
  );
});

/**
 * Tests deleting a digest or a memory leaves the other in place
 */
add_task(async function test_memories_and_digests_are_independent() {
  const memory = await MemoryStore.addMemory({
    memory_summary: "Planning a trip to Lisbon",
  });
  // Extra cleanup function for this test, the only one in this file that touches MemoryStore
  registerCleanupFunction(() => MemoryStore.hardDeleteMemory(memory.id));

  const first = await DigestManager.createDigest(
    makeDigestPartial({ memory_id: memory.id })
  );
  await DigestManager.deleteDigest(first.id);
  Assert.equal(
    (await MemoryStore.getMemories({ memoryIds: new Set([memory.id]) })).length,
    1,
    "Deleting a digest leaves its memory"
  );

  const second = await DigestManager.createDigest(
    makeDigestPartial({ memory_id: memory.id })
  );
  await MemoryStore.hardDeleteMemory(memory.id);
  Assert.deepEqual(
    await DigestManager.getDigestById(second.id),
    second,
    "Deleting a memory leaves its digests"
  );

  await resetStore();
});

/**
 * Tests computeNextRunTime for each schedule type, in local time
 */
add_task(function test_compute_next_run_time() {
  // Monday, January 5 2026
  const mondayAt = (hour, minute) =>
    new Date(2026, 0, 5, hour, minute).getTime();
  const daily = { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 9, minute: 30 };
  const cases = [
    {
      schedule: daily,
      after: mondayAt(8, 0),
      expected: new Date(2026, 0, 5, 9, 30).getTime(),
      message: "Daily runs later the same day",
    },
    {
      schedule: daily,
      after: mondayAt(9, 30),
      expected: new Date(2026, 0, 6, 9, 30).getTime(),
      message: "Daily runs the next day once the time is reached",
    },
    {
      schedule: {
        type: DIGEST_SCHEDULE_TYPE.WEEKLY,
        weekday: 3,
        hour: 9,
        minute: 30,
      },
      after: mondayAt(8, 0),
      expected: new Date(2026, 0, 7, 9, 30).getTime(),
      message: "Weekly runs on the next matching weekday",
    },
    {
      schedule: {
        type: DIGEST_SCHEDULE_TYPE.WEEKLY,
        weekday: 1,
        hour: 9,
        minute: 30,
      },
      after: mondayAt(10, 0),
      expected: new Date(2026, 0, 12, 9, 30).getTime(),
      message: "Weekly runs next week once today's time has passed",
    },
    {
      schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      after: mondayAt(8, 0),
      expected: null,
      message: "On-demand has no next run",
    },
  ];
  for (const { schedule, after, expected, message } of cases) {
    Assert.strictEqual(
      DigestManager.computeNextRunTime(schedule, after),
      expected,
      message
    );
  }
  Assert.throws(
    () =>
      DigestManager.computeNextRunTime({
        type: DIGEST_SCHEDULE_TYPE.DAILY,
        hour: 25,
        minute: 0,
      }),
    /TypeError: Invalid digest schedule/,
    "Rejects an invalid schedule"
  );
});

/**
 * Tests enableDigest enables a digest and schedules it after now
 */
add_task(async function test_enable_digest() {
  const daily = await DigestStore.addDigest(
    makeDigestPartial({ last_run_time: 1000 })
  );
  const onDemand = await DigestManager.createDigest(
    makeDigestPartial({ schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND } })
  );

  const before = Date.now();
  const enabled = await DigestManager.enableDigest(daily.id);
  const after = Date.now();
  Assert.equal(enabled.status, DIGEST_STATUS.ENABLED, "Digest is enabled");
  Assert.greater(
    enabled.next_run_time,
    before,
    "Next scheduled run is after now"
  );
  Assert.lessOrEqual(
    enabled.next_run_time,
    after + 24 * 60 * 60 * 1000,
    "Next run is within a day for a daily scheduled digest"
  );
  const nextRun = new Date(enabled.next_run_time);
  Assert.deepEqual(
    [nextRun.getHours(), nextRun.getMinutes()],
    [9, 30],
    "Next run is at the scheduled time"
  );
  Assert.deepEqual(
    await DigestManager.getDigestById(daily.id),
    enabled,
    "The enabled digest is stored"
  );

  const enabledOnDemand = await DigestManager.enableDigest(onDemand.id);
  Assert.equal(
    enabledOnDemand.status,
    DIGEST_STATUS.ENABLED,
    "On-demand status is enabled"
  );
  Assert.strictEqual(
    enabledOnDemand.next_run_time,
    null,
    "On-demand is not scheduled"
  );

  Assert.strictEqual(
    await DigestManager.enableDigest("unknown"),
    null,
    "Returns null for an unknown id"
  );

  await resetStore();
});

/**
 * Tests disableDigest disables a digest and clears its next run
 */
add_task(async function test_disable_digest() {
  const created = await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      last_run_time: 1000,
      next_run_time: 2000,
    })
  );

  const disabled = await DigestManager.disableDigest(created.id);
  Assert.equal(disabled.status, DIGEST_STATUS.DISABLED, "Digest is disabled");
  Assert.strictEqual(disabled.next_run_time, null, "Next run is cleared");
  Assert.ok(disabled.unread, "unread is unchanged");
  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    disabled,
    "The disabled digest is stored"
  );
  Assert.strictEqual(
    await DigestManager.disableDigest("unknown"),
    null,
    "Returns null for an unknown id"
  );
  Assert.deepEqual(
    created.schedule,
    disabled.schedule,
    "Disabling a digest doesn't clear its schedule"
  );

  await resetStore();
});

/**
 * Tests markDigestRead clears unread and records when it was read
 */
add_task(async function test_mark_digest_read() {
  const created = await DigestStore.addDigest(
    makeDigestPartial({
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      last_run_time: 1000,
    })
  );

  const before = Date.now();
  const read = await DigestManager.markDigestRead(created.id);
  const after = Date.now();
  Assert.ok(!read.unread, "unread is false");
  Assert.greaterOrEqual(
    read.last_read_time,
    before,
    "last_read_time is greater than or equal to the time before the digest was read"
  );
  Assert.lessOrEqual(
    read.last_read_time,
    after,
    "last_read_time is less than or equal to the time after the digest was read"
  );
  Assert.deepEqual(
    await DigestManager.getDigestById(created.id),
    read,
    "The read digest is stored"
  );
  Assert.equal(
    (await DigestManager.markDigestRead(created.id)).last_read_time,
    read.last_read_time,
    "Marking an already read digest keeps last_read_time"
  );
  Assert.strictEqual(
    await DigestManager.markDigestRead("unknown"),
    null,
    "Returns null for an unknown id"
  );

  await resetStore();
});
