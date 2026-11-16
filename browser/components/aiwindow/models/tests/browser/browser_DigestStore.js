/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { DigestStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
);
const { DIGEST_STATUS, DIGEST_SCHEDULE_TYPE } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/memories/digests/DigestConstants.sys.mjs"
);

const { MEMORY_FILTER_COMPARATOR } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs"
);

const path = PathUtils.join(
  PathUtils.profileDir,
  "memories",
  "digests.json.lz4"
);

/**
 * Builds a valid digest using defaults with optional overrides.
 *
 * @param {object} overrides - Optional digest attribute overrides
 * @returns {object} Digest with every field set
 */
function makeDigest(overrides = {}) {
  return {
    id: "3b241101-e2bb-4255-8caf-4136c566a962",
    status: DIGEST_STATUS.ENABLED,
    unread: true,
    memory_id: "mem.0000abcd",
    ai_tab_slug: "lisbon-news",
    title: "Tourist news in Lisbon",
    description: "Give me general tourism news from Lisbon.",
    domains: ["visitlisboa.com", "timeout.com"],
    schedule: {
      type: DIGEST_SCHEDULE_TYPE.WEEKLY,
      weekday: 1,
      hour: 9,
      minute: 30,
    },
    last_run_time: null,
    last_read_time: null,
    next_run_time: 1000,
    created_at: 500,
    updated_at: 500,
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
 * Tests DigestStore starts empty
 */
add_task(async function test_init_empty_state() {
  Assert.deepEqual(
    await DigestStore.getDigests(),
    [],
    "A missing file starts empty"
  );
});

/**
 * Tests DigestStore is a singleton in memory
 */
add_task(function test_singleton_identity() {
  const imported = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/memories/digests/DigestStore.sys.mjs"
  );
  Assert.strictEqual(
    imported.DigestStore,
    DigestStore,
    "Repeated imports return the same DigestStore instance"
  );
});

/**
 * Tests first use of DigestStore loads from disk
 */
add_task(async function test_loads_digests_from_disk() {
  try {
    await resetStore();
    const digests = [
      makeDigest(),
      makeDigest({
        id: "9c858901-8a57-4791-81fe-4c455b099bc9",
        status: DIGEST_STATUS.SUGGESTED,
        unread: false,
        ai_tab_slug: null,
        schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
        next_run_time: null,
      }),
    ];
    // Save the file directly without relying on DigestStore to save
    // This makes sure the file ends up where we want, so we can test loading
    await IOUtils.writeJSON(path, { version: 1, digests }, { compress: true });
    Assert.deepEqual(
      await DigestStore.getDigests(),
      digests,
      "Loading returns the digests stored on disk"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digests with null fields that aren't required load whether null or set
 */
add_task(async function test_loads_nullable_fields() {
  try {
    await resetStore();
    const allNull = makeDigest({
      id: "all-null",
      ai_tab_slug: null,
      last_run_time: null,
      last_read_time: null,
      next_run_time: null,
    });
    const allSet = makeDigest({
      id: "all-set",
      ai_tab_slug: "lisbon-news",
      last_run_time: 2000,
      last_read_time: 2500,
      next_run_time: 3000,
    });
    await IOUtils.writeJSON(
      path,
      { version: 1, digests: [allNull, allSet] },
      { compress: true }
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [allNull, allSet],
      "ai_tab_slug, last_run_time, last_read_time, and next_run_time load unchanged whether null or set"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests the full round trip for a new digest:
 * 1. Added to the store
 * 2. Read back from the store
 * 3. Reloaded from disk after the store saves and drops cached data
 */
add_task(async function test_add_digest_round_trip() {
  try {
    await resetStore();
    const digest = makeDigest();
    Assert.deepEqual(
      await DigestStore.addDigest(digest),
      digest,
      "addDigest returns the stored digest"
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [digest],
      "getDigests returns the added digest"
    );

    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [digest],
      "Flush + reset reloads the added digest from disk"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests defaults for fields that aren't required are correctly added to a new digest
 */
add_task(async function test_add_digest_defaults() {
  try {
    await resetStore();
    const required = {
      memory_id: "mem.0000abcd",
      title: "Tourist news in Lisbon",
      description: "Give me general tourism news from Lisbon.",
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 8, minute: 0 },
    };
    const before = Date.now();
    const digest = await DigestStore.addDigest(required);
    const after = Date.now();

    Assert.stringMatches(
      digest.id,
      /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/,
      "A missing id defaults to a UUID"
    );
    Assert.greaterOrEqual(
      digest.created_at,
      before,
      "created_at is greater than or equal to the time before the digest was created"
    );
    Assert.lessOrEqual(
      digest.created_at,
      after,
      "created_at is is less than or equal to the time after the digest was created"
    );
    Assert.deepEqual(
      digest,
      {
        ...required,
        id: digest.id,
        status: DIGEST_STATUS.SUGGESTED,
        unread: false,
        ai_tab_slug: null,
        domains: [],
        last_run_time: null,
        last_read_time: null,
        next_run_time: null,
        created_at: digest.created_at,
        updated_at: digest.created_at,
      },
      "Omitted fields get default values"
    );

    // Adding the same fields again without an ID creates a separate digest with a new ID
    const second = await DigestStore.addDigest(required);
    Assert.notEqual(second.id, digest.id, "Each digest gets a new id");
  } finally {
    await resetStore();
  }
});

/**
 * Tests making in-place edits to a digest doesn't affect the store
 */
add_task(async function test_add_digest_stores_copies() {
  try {
    await resetStore();
    const input = makeDigest();
    const expected = structuredClone(input);
    const returned = await DigestStore.addDigest(input);

    input.title = "Changed outside the store";
    input.schedule.hour = 23;
    input.domains.push("changed.example");
    returned.title = "Changed outside the store";
    returned.schedule.hour = 23;
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "Changing the input or the returned digest does not change stored digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests different invalidity conditions when adding a new digest
 */
add_task(async function test_add_digest_validates_before_storing() {
  try {
    await resetStore();
    const existing = makeDigest();
    await DigestStore.addDigest(existing);

    const assertRejected = async (input, message) => {
      await Assert.rejects(DigestStore.addDigest(input), /TypeError/, message);
      Assert.deepEqual(
        await DigestStore.getDigests(),
        [existing],
        `Rejected input does not change stored digests: ${message}`
      );
    };

    // Check addDigest rejects different non-Object data types
    for (const invalid of [null, undefined, [], "digest", 42]) {
      await assertRejected(invalid, `Invalid argument: ${String(invalid)}`);
    }

    // Check addDigest rejects when specific required fields are missing
    const newDigest = makeDigest({
      id: "a7c9e0a4-7f0a-4a40-9d8b-1c2a3e4f5a6b",
    });
    for (const field of ["memory_id", "title", "description", "schedule"]) {
      const input = { ...newDigest };
      delete input[field];
      await assertRejected(input, `Missing required field: ${field}`);
    }

    // Check addDigest rejects when fields contain the incorrect data type
    const invalidFields = [
      { id: "" },
      { id: 42 },
      { status: "paused" },
      { status: undefined },
      { unread: "true" },
      { memory_id: "   " },
      { ai_tab_slug: "   " },
      { ai_tab_slug: 42 },
      { domains: "visitlisboa.com" },
      { domains: null },
      { domains: [""] },
      { domains: ["visitlisboa.com", 42] },
      { title: "" },
      { description: 42 },
      { last_run_time: -1 },
      { last_run_time: 1.5 },
      { last_run_time: undefined },
      { last_read_time: -1 },
      { last_read_time: "1000" },
      { next_run_time: "1000" },
      { next_run_time: NaN },
      { created_at: null },
      { updated_at: Infinity },
      { unknown: true },
    ];
    for (const [index, invalid] of invalidFields.entries()) {
      await assertRejected(
        { ...newDigest, ...invalid },
        `Invalid fields case ${index}`
      );
    }

    // Check addDigest rejects when the schedule object is malformed
    const { WEEKLY, DAILY, ON_DEMAND } = DIGEST_SCHEDULE_TYPE;
    const invalidSchedules = [
      null,
      [],
      "weekly",
      {},
      { type: "interval", hours: 4 },
      { type: WEEKLY, hour: 9, minute: 30 },
      { type: WEEKLY, weekday: 7, hour: 9, minute: 30 },
      { type: WEEKLY, weekday: "1", hour: 9, minute: 30 },
      { type: DAILY, hour: 24, minute: 0 },
      { type: DAILY, hour: 9, minute: 60 },
      { type: DAILY, hour: 9.5, minute: 0 },
      { type: DAILY, hour: 9 },
      { type: DAILY, weekday: 1, hour: 9, minute: 0 },
      { type: ON_DEMAND, hour: 9, minute: 0 },
    ];
    for (const [index, schedule] of invalidSchedules.entries()) {
      await assertRejected(
        { ...newDigest, schedule },
        `Invalid schedule case ${index}`
      );
    }

    // Check the rejcted adds didn't change the saved file
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [existing],
      "Rejected input does not change persisted digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests requested saves write to disk
 */
add_task(async function test_request_save_writes_file() {
  try {
    // Clear, save clean, flush, and read back immediately
    await resetStore();
    await DigestStore.requestSave();
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await IOUtils.readJSON(path, { decompress: true }),
      { version: 1, digests: [] },
      "requestSave writes the current state to disk"
    );

    // Add a digest, flush, and read back.
    const digest = makeDigest();
    await DigestStore.addDigest(digest);
    await DigestStore.requestSave();
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await IOUtils.readJSON(path, { decompress: true }),
      { version: 1, digests: [digest] },
      "The file on disk contains the added digest"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digests can be updated through the updateDigest API
 */
add_task(async function test_update_digest() {
  try {
    await resetStore();

    // Create a starter digest
    const digest = makeDigest();
    await DigestStore.addDigest(digest);

    // Update 3 attributes
    const before = Date.now();
    const updated = await DigestStore.updateDigest(digest.id, {
      unread: false,
      last_run_time: 2000,
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 7, minute: 15 },
    });
    const after = Date.now();

    // Check the updated_at timestamp updates
    Assert.greaterOrEqual(
      updated.updated_at,
      before,
      "updated_at is greater than or equal to the time before the digest was updated"
    );
    Assert.lessOrEqual(
      updated.updated_at,
      after,
      "updated_at is is less than or equal to the time after the digest was updated"
    );

    // Check each of the 3 fields updates as expected
    const expected = {
      ...digest,
      unread: false,
      last_run_time: 2000,
      last_read_time: updated.updated_at,
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 7, minute: 15 },
      updated_at: updated.updated_at,
    };
    Assert.deepEqual(
      updated,
      expected,
      "Updated fields change, omitted fields are kept, and the schedule is replaced"
    );

    // Check getDigests returns the updated one
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "getDigests returns the updated digest"
    );

    // Checks updateDigest allows updated_at to change explictly
    expected.updated_at = 3000;
    Assert.deepEqual(
      await DigestStore.updateDigest(digest.id, { updated_at: 3000 }),
      expected,
      "An explicit updated_at is kept"
    );

    // Checks new domains replace the existing list rather than merging into it
    expected.domains = ["publico.pt"];
    Assert.deepEqual(
      await DigestStore.updateDigest(digest.id, {
        domains: ["publico.pt"],
        updated_at: 3000,
      }),
      expected,
      "New domains replace the existing domains"
    );

    // Flush and check the updated digest persists
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "Updates persist after reopening"
    );

    Assert.strictEqual(
      await DigestStore.updateDigest("absent", { unread: true }),
      null,
      "Updating an absent digest returns null"
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "Updating an absent digest does not add one"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests updateDigest sets last_read_time only when unread changes from true to
 * false, unless an explicit last_read_time is provided
 */
add_task(async function test_update_digest_last_read_time() {
  try {
    await resetStore();
    const digest = makeDigest({ unread: true, last_read_time: null });
    await DigestStore.addDigest(digest);

    // Check unread changing from true to false sets last_read_time to updated_at
    const before = Date.now();
    const read = await DigestStore.updateDigest(digest.id, { unread: false });
    const after = Date.now();
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
    Assert.equal(
      read.last_read_time,
      read.updated_at,
      "last_read_time matches updated_at"
    );

    // Check updates that don't change unread from true to false keep last_read_time
    for (const updates of [
      { unread: false },
      { unread: true },
      { unread: true },
      { title: "Tourist news in Porto" },
    ]) {
      Assert.equal(
        (await DigestStore.updateDigest(digest.id, updates)).last_read_time,
        read.last_read_time,
        `last_read_time is unchanged by ${JSON.stringify(updates)}`
      );
    }

    // Check an explicit last_read_time is kept, whether or not unread changes
    Assert.equal(
      (
        await DigestStore.updateDigest(digest.id, {
          unread: false,
          last_read_time: 1500,
        })
      ).last_read_time,
      1500,
      "An explicit last_read_time is kept when unread changes from true to false"
    );
    Assert.strictEqual(
      (await DigestStore.updateDigest(digest.id, { last_read_time: null }))
        .last_read_time,
      null,
      "last_read_time can be explicitly reset to null"
    );

    // Check addDigest with an existing id also sets last_read_time
    await DigestStore.updateDigest(digest.id, { unread: true });
    const readAgain = await DigestStore.addDigest({
      id: digest.id,
      unread: false,
    });
    Assert.equal(
      readAgain.last_read_time,
      readAgain.updated_at,
      "addDigest with an existing id sets last_read_time when unread changes from true to false"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests addDigest and updateDigest remove duplicate domains, keeping the first
 * occurrence of each in order
 */
add_task(async function test_domains_are_deduplicated() {
  try {
    await resetStore();
    const added = await DigestStore.addDigest(
      makeDigest({
        domains: [
          "visitlisboa.com",
          "timeout.com",
          "visitlisboa.com",
          "publico.pt",
          "timeout.com",
        ],
      })
    );
    Assert.deepEqual(
      added.domains,
      ["visitlisboa.com", "timeout.com", "publico.pt"],
      "addDigest removes duplicate domains"
    );

    const updated = await DigestStore.updateDigest(added.id, {
      domains: ["publico.pt", "publico.pt", "visitporto.travel"],
    });
    Assert.deepEqual(
      updated.domains,
      ["publico.pt", "visitporto.travel"],
      "updateDigest removes duplicate domains"
    );

    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      (await DigestStore.getDigests())[0].domains,
      ["publico.pt", "visitporto.travel"],
      "The deduplicated domains are saved"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests in-place digest updates do not persist through to DigestStore
 */
add_task(async function test_in_place_updates_do_not_affect_store() {
  try {
    // Create a digest
    await resetStore();
    const digest = makeDigest();
    await DigestStore.addDigest(digest);

    // Update it through the proper API
    const updates = {
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 7, minute: 15 },
      updated_at: 3000,
    };
    const returned = await DigestStore.updateDigest(digest.id, updates);
    const expected = structuredClone(returned);

    // Make in-place updates and check the store doesn't save them
    // Updates need to go through the proper API
    updates.schedule.hour = 23;
    returned.title = "Changed outside the store";
    returned.schedule.minute = 59;
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "Changing the updates or the returned digest does not change stored digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests adding a digest with an ID that already exists updates the existing object
 */
add_task(async function test_add_digest_with_existing_id_updates() {
  try {
    await resetStore();
    const digest = makeDigest();
    await DigestStore.addDigest(digest);

    // Add a digest with the original's ID
    const updated = await DigestStore.addDigest({
      id: digest.id,
      title: "Tourist news in Porto",
      updated_at: 3000,
    });
    const expected = {
      ...digest,
      title: "Tourist news in Porto",
      updated_at: 3000,
    };
    Assert.deepEqual(
      updated,
      expected,
      "addDigest with an existing id updates that digest"
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "addDigest with an existing id does not add a digest"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Test updateDigest validates provided updates before storing anything
 */
add_task(async function test_update_digest_validates_before_storing() {
  try {
    await resetStore();
    const existing = makeDigest();
    await DigestStore.addDigest(existing);

    const invalidUpdates = [
      null,
      undefined,
      [],
      "updates",
      { id: "9c858901-8a57-4791-81fe-4c455b099bc9" },
      { id: existing.id },
      { status: "paused" },
      { unread: undefined },
      { ai_tab_slug: "" },
      { domains: ["   "] },
      { title: "   " },
      { last_read_time: 1.5 },
      { next_run_time: -1 },
      { updated_at: null },
      { schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 7 } },
      { unknown: true },
      { unread: true, unknown: true },
    ];
    for (const [index, updates] of invalidUpdates.entries()) {
      await Assert.rejects(
        DigestStore.updateDigest(existing.id, updates),
        /TypeError/,
        `Invalid updates case ${index}`
      );
      Assert.deepEqual(
        await DigestStore.getDigests(),
        [existing],
        `Rejected updates do not change stored digests: case ${index}`
      );
    }

    // Check all the invalid updates above didn't change the store state
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [existing],
      "Rejected updates do not change persisted digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Test getDigests filters by digest ID when provided a set
 */
add_task(async function test_get_digests_filters_by_ids() {
  try {
    await resetStore();
    const digests = ["first", "second", "third"].map(id => makeDigest({ id }));
    for (const digest of digests) {
      await DigestStore.addDigest(digest);
    }
    const [first, second, third] = digests;

    Assert.deepEqual(
      await DigestStore.getDigests({ digestIds: new Set(["third", "first"]) }),
      [first, third],
      "digestIds returns the matching digests in stored order"
    );
    Assert.deepEqual(
      await DigestStore.getDigests({
        digestIds: new Set(["second", "absent"]),
      }),
      [second],
      "Absent ids in digestIds are ignored"
    );
    Assert.deepEqual(
      await DigestStore.getDigests({ digestIds: new Set(["absent"]) }),
      [],
      "digestIds with no matching digests returns nothing"
    );
    Assert.deepEqual(
      await DigestStore.getDigests({ digestIds: new Set() }),
      [first, second, third],
      "An empty digestIds returns all digests"
    );
    Assert.deepEqual(
      await DigestStore.getDigests({}),
      [first, second, third],
      "Omitting digestIds returns all digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests an empty attributeFilters returns all digests and keeps the digestIds
 * filter
 */
add_task(async function test_get_digests_with_empty_attribute_filters() {
  try {
    await resetStore();
    const digests = ["first", "second"].map(id => makeDigest({ id }));
    for (const digest of digests) {
      await DigestStore.addDigest(digest);
    }

    Assert.deepEqual(
      await DigestStore.getDigests({ attributeFilters: [] }),
      digests,
      "No attribute filters returns all digests"
    );
    Assert.deepEqual(
      await DigestStore.getDigests({
        digestIds: new Set(["second"]),
        attributeFilters: [],
      }),
      [digests[1]],
      "No attribute filters leaves the digestIds filter in place"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests getDigests EQUAL_TO and IN comparators return the expected digests
 */
add_task(async function test_get_digests_filters_by_equality() {
  try {
    await resetStore();
    const enabledUnreadWeekly = makeDigest({
      id: "enabled-unread-weekly",
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      memory_id: "mem.shared",
      ai_tab_slug: "enabled-weekly-tab",
      last_run_time: 2000,
      next_run_time: 3000,
    });
    const enabledReadDaily = makeDigest({
      id: "enabled-read-daily",
      status: DIGEST_STATUS.ENABLED,
      unread: false,
      memory_id: "mem.daily-only",
      ai_tab_slug: "enabled-daily-tab",
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 8, minute: 0 },
      last_run_time: 1000,
      last_read_time: 1500,
      next_run_time: 4000,
    });
    const suggestedNeverRun = makeDigest({
      id: "suggested-never-run",
      status: DIGEST_STATUS.SUGGESTED,
      unread: false,
      memory_id: "mem.suggested-only",
      ai_tab_slug: null,
      schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      last_run_time: null,
      next_run_time: null,
    });
    const disabledUnscheduled = makeDigest({
      id: "disabled-unscheduled",
      status: DIGEST_STATUS.DISABLED,
      unread: false,
      memory_id: "mem.shared",
      ai_tab_slug: "disabled-tab",
      schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      last_run_time: 2000,
      next_run_time: null,
    });
    for (const digest of [
      enabledUnreadWeekly,
      enabledReadDaily,
      suggestedNeverRun,
      disabledUnscheduled,
    ]) {
      await DigestStore.addDigest(digest);
    }

    const { EQUAL_TO, IN } = MEMORY_FILTER_COMPARATOR;
    const { DAILY, WEEKLY, ON_DEMAND } = DIGEST_SCHEDULE_TYPE;
    const cases = [
      [
        { field: "status", comparator: EQUAL_TO, value: DIGEST_STATUS.ENABLED },
        [enabledUnreadWeekly, enabledReadDaily],
      ],
      [
        {
          field: "status",
          comparator: IN,
          value: [DIGEST_STATUS.SUGGESTED, DIGEST_STATUS.DISABLED],
        },
        [suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "unread", comparator: EQUAL_TO, value: false },
        [enabledReadDaily, suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "memory_id", comparator: EQUAL_TO, value: "mem.shared" },
        [enabledUnreadWeekly, disabledUnscheduled],
      ],
      [
        {
          field: "memory_id",
          comparator: IN,
          value: ["mem.suggested-only", "mem.absent"],
        },
        [suggestedNeverRun],
      ],
      [
        { field: "ai_tab_slug", comparator: EQUAL_TO, value: null },
        [suggestedNeverRun],
      ],
      [
        { field: "ai_tab_slug", comparator: IN, value: ["disabled-tab", null] },
        [suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "next_run_time", comparator: EQUAL_TO, value: null },
        [suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "last_run_time", comparator: EQUAL_TO, value: 2000 },
        [enabledUnreadWeekly, disabledUnscheduled],
      ],
      [
        { field: "last_read_time", comparator: EQUAL_TO, value: null },
        [enabledUnreadWeekly, suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "schedule.type", comparator: EQUAL_TO, value: ON_DEMAND },
        [suggestedNeverRun, disabledUnscheduled],
      ],
      [
        { field: "schedule.type", comparator: IN, value: [DAILY, WEEKLY] },
        [enabledUnreadWeekly, enabledReadDaily],
      ],
      [
        { field: "schedule.type", comparator: EQUAL_TO, value: WEEKLY },
        [enabledUnreadWeekly],
      ],
      [
        { field: "schedule.type", comparator: EQUAL_TO, value: DAILY },
        [enabledReadDaily],
      ],
      [{ field: "memory_id", comparator: EQUAL_TO, value: "mem.absent" }, []],
      [{ field: "status", comparator: IN, value: [] }, []],
    ];
    for (const [filter, expected] of cases) {
      Assert.deepEqual(
        (await DigestStore.getDigests({ attributeFilters: [filter] })).map(
          digest => digest.id
        ),
        expected.map(digest => digest.id),
        `${filter.field} ${filter.comparator} ${JSON.stringify(filter.value)}`
      );
    }
  } finally {
    await resetStore();
  }
});

/**
 * Tests getDigests ordering comparators return the expected digests
 */
add_task(async function test_get_digests_filters_by_order() {
  try {
    await resetStore();
    const suggestedUnscheduled = makeDigest({
      id: "suggested-unscheduled",
      status: DIGEST_STATUS.SUGGESTED,
      unread: false,
      ai_tab_slug: null,
      last_run_time: null,
      next_run_time: null,
      created_at: 100,
      updated_at: 100,
    });
    const ranAt2000DueAt3000 = makeDigest({
      id: "ran-at-2000-due-at-3000",
      last_run_time: 2000,
      next_run_time: 3000,
      created_at: 200,
      updated_at: 2000,
    });
    const ranAt4000DueAt5000 = makeDigest({
      id: "ran-at-4000-due-at-5000",
      last_run_time: 4000,
      last_read_time: 4500,
      next_run_time: 5000,
      created_at: 300,
      updated_at: 4000,
    });
    for (const digest of [
      suggestedUnscheduled,
      ranAt2000DueAt3000,
      ranAt4000DueAt5000,
    ]) {
      await DigestStore.addDigest(digest);
    }

    const {
      GREATER_THAN,
      LESS_THAN,
      GREATER_THAN_OR_EQUAL_TO,
      LESS_THAN_OR_EQUAL_TO,
    } = MEMORY_FILTER_COMPARATOR;
    const cases = [
      [
        {
          field: "next_run_time",
          comparator: LESS_THAN_OR_EQUAL_TO,
          value: 3000,
        },
        [ranAt2000DueAt3000],
      ],
      [{ field: "next_run_time", comparator: LESS_THAN, value: 3000 }, []],
      [
        {
          field: "next_run_time",
          comparator: GREATER_THAN_OR_EQUAL_TO,
          value: 3000,
        },
        [ranAt2000DueAt3000, ranAt4000DueAt5000],
      ],
      [
        { field: "next_run_time", comparator: GREATER_THAN, value: 3000 },
        [ranAt4000DueAt5000],
      ],
      [
        { field: "last_run_time", comparator: LESS_THAN, value: 10000 },
        [ranAt2000DueAt3000, ranAt4000DueAt5000],
      ],
      [
        {
          field: "last_run_time",
          comparator: GREATER_THAN_OR_EQUAL_TO,
          value: 0,
        },
        [ranAt2000DueAt3000, ranAt4000DueAt5000],
      ],
      [
        { field: "last_read_time", comparator: GREATER_THAN, value: 0 },
        [ranAt4000DueAt5000],
      ],
      [
        { field: "created_at", comparator: GREATER_THAN, value: 150 },
        [ranAt2000DueAt3000, ranAt4000DueAt5000],
      ],
      [
        { field: "updated_at", comparator: LESS_THAN_OR_EQUAL_TO, value: 2000 },
        [suggestedUnscheduled, ranAt2000DueAt3000],
      ],
    ];
    for (const [filter, expected] of cases) {
      Assert.deepEqual(
        (await DigestStore.getDigests({ attributeFilters: [filter] })).map(
          digest => digest.id
        ),
        expected.map(digest => digest.id),
        `${filter.field} ${filter.comparator} ${filter.value}`
      );
    }
  } finally {
    await resetStore();
  }
});

/**
 * Tests getDigests throws when provided invalid filters
 */
add_task(async function test_get_digests_rejects_invalid_filters() {
  try {
    await resetStore();
    await DigestStore.addDigest(makeDigest());

    const { EQUAL_TO, IN, LESS_THAN_OR_EQUAL_TO, LIKE, INCLUDES, SOME, ALL } =
      MEMORY_FILTER_COMPARATOR;
    const invalidOptions = [
      ["digestIds is an array", { digestIds: ["id"] }],
      ["digestIds is null", { digestIds: null }],
      ["attributeFilters is an object", { attributeFilters: {} }],
      ["attributeFilters is null", { attributeFilters: null }],
      ["filter is null", { attributeFilters: [null] }],
      ["filter is a string", { attributeFilters: ["status"] }],
      [
        "semantic comparator LIKE",
        {
          attributeFilters: [
            { field: "title", comparator: LIKE, value: "Tourist news" },
          ],
        },
      ],
      [
        "made-up comparator",
        {
          attributeFilters: [
            {
              field: "status",
              comparator: "NOT_A_COMPARATOR",
              value: "enabled",
            },
          ],
        },
      ],
      [
        "missing comparator",
        { attributeFilters: [{ field: "status", value: "enabled" }] },
      ],
      [
        "list comparator INCLUDES",
        {
          attributeFilters: [
            { field: "status", comparator: INCLUDES, value: "enabled" },
          ],
        },
      ],
      [
        "list comparator SOME",
        {
          attributeFilters: [
            { field: "status", comparator: SOME, value: ["enabled"] },
          ],
        },
      ],
      [
        "list comparator ALL",
        {
          attributeFilters: [
            { field: "status", comparator: ALL, value: ["enabled"] },
          ],
        },
      ],
      [
        "EQUAL_TO on an unsupported field",
        {
          attributeFilters: [
            {
              field: "title",
              comparator: EQUAL_TO,
              value: "Tourist news in Lisbon",
            },
          ],
        },
      ],
      [
        "EQUAL_TO on a nested field other than schedule.type",
        {
          attributeFilters: [
            { field: "schedule.hour", comparator: EQUAL_TO, value: 9 },
          ],
        },
      ],
      [
        "EQUAL_TO on a missing field",
        {
          attributeFilters: [{ comparator: EQUAL_TO, value: "enabled" }],
        },
      ],
      [
        "aggregate field",
        {
          attributeFilters: [
            {
              field: "recent_accessed_counts.[0-2]",
              comparator: EQUAL_TO,
              value: 1,
            },
          ],
        },
      ],
      [
        "IN on a time field",
        {
          attributeFilters: [
            { field: "next_run_time", comparator: IN, value: [1000] },
          ],
        },
      ],
      [
        "ordering comparator on a non-time field",
        {
          attributeFilters: [
            {
              field: "status",
              comparator: LESS_THAN_OR_EQUAL_TO,
              value: 1000,
            },
          ],
        },
      ],
      [
        "EQUAL_TO with an object value",
        {
          attributeFilters: [
            { field: "status", comparator: EQUAL_TO, value: { status: 1 } },
          ],
        },
      ],
      [
        "EQUAL_TO with an array value",
        {
          attributeFilters: [
            { field: "status", comparator: EQUAL_TO, value: ["enabled"] },
          ],
        },
      ],
      [
        "EQUAL_TO with a missing value",
        { attributeFilters: [{ field: "status", comparator: EQUAL_TO }] },
      ],
      [
        "IN with a non-array value",
        {
          attributeFilters: [
            { field: "status", comparator: IN, value: "enabled" },
          ],
        },
      ],
      [
        "ordering comparator with a string value",
        {
          attributeFilters: [
            {
              field: "next_run_time",
              comparator: LESS_THAN_OR_EQUAL_TO,
              value: "1000",
            },
          ],
        },
      ],
      [
        "ordering comparator with a null value",
        {
          attributeFilters: [
            {
              field: "next_run_time",
              comparator: LESS_THAN_OR_EQUAL_TO,
              value: null,
            },
          ],
        },
      ],
      [
        "ordering comparator with NaN",
        {
          attributeFilters: [
            {
              field: "next_run_time",
              comparator: LESS_THAN_OR_EQUAL_TO,
              value: NaN,
            },
          ],
        },
      ],
      [
        "one invalid filter among valid ones",
        {
          attributeFilters: [
            { field: "status", comparator: EQUAL_TO, value: "enabled" },
            {
              field: "title",
              comparator: EQUAL_TO,
              value: "Tourist news in Lisbon",
            },
          ],
        },
      ],
    ];
    for (const [name, options] of invalidOptions) {
      await Assert.rejects(
        DigestStore.getDigests(options),
        /TypeError/,
        `Reject ${name}`
      );
    }
  } finally {
    await resetStore();
  }
});

/**
 * Tests getDigests applies multiple attribute filters in the same call
 */
add_task(async function test_get_digests_combines_filters() {
  try {
    await resetStore();
    const now = 2000;
    const dueEnabledWeekly = makeDigest({
      id: "due-enabled-weekly",
      status: DIGEST_STATUS.ENABLED,
      unread: false,
      memory_id: "mem.lisbon",
      last_run_time: 500,
      next_run_time: 1000,
    });
    const upcomingEnabledDailyUnread = makeDigest({
      id: "upcoming-enabled-daily-unread",
      status: DIGEST_STATUS.ENABLED,
      unread: true,
      memory_id: "mem.lisbon",
      schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 8, minute: 0 },
      last_run_time: 1500,
      next_run_time: 5000,
    });
    const suggestedForLisbon = makeDigest({
      id: "suggested-for-lisbon",
      status: DIGEST_STATUS.SUGGESTED,
      unread: false,
      memory_id: "mem.lisbon",
      ai_tab_slug: null,
      schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      last_run_time: null,
      next_run_time: null,
    });
    const disabledPorto = makeDigest({
      id: "disabled-porto",
      status: DIGEST_STATUS.DISABLED,
      unread: false,
      memory_id: "mem.porto",
      schedule: { type: DIGEST_SCHEDULE_TYPE.ON_DEMAND },
      last_run_time: 800,
      next_run_time: null,
    });
    for (const digest of [
      dueEnabledWeekly,
      upcomingEnabledDailyUnread,
      suggestedForLisbon,
      disabledPorto,
    ]) {
      await DigestStore.addDigest(digest);
    }

    const { EQUAL_TO, GREATER_THAN, LESS_THAN, LESS_THAN_OR_EQUAL_TO } =
      MEMORY_FILTER_COMPARATOR;
    const enabled = {
      field: "status",
      comparator: EQUAL_TO,
      value: DIGEST_STATUS.ENABLED,
    };
    const cases = [
      [
        "Due to run",
        {
          attributeFilters: [
            enabled,
            {
              field: "next_run_time",
              comparator: LESS_THAN_OR_EQUAL_TO,
              value: now,
            },
          ],
        },
        [dueEnabledWeekly],
      ],
      [
        "Enabled digests for a memory",
        {
          attributeFilters: [
            enabled,
            { field: "memory_id", comparator: EQUAL_TO, value: "mem.lisbon" },
          ],
        },
        [dueEnabledWeekly, upcomingEnabledDailyUnread],
      ],
      [
        "Unread enabled digests",
        {
          attributeFilters: [
            enabled,
            { field: "unread", comparator: EQUAL_TO, value: true },
          ],
        },
        [upcomingEnabledDailyUnread],
      ],
      [
        "Next run between two times",
        {
          attributeFilters: [
            { field: "next_run_time", comparator: GREATER_THAN, value: 1000 },
            { field: "next_run_time", comparator: LESS_THAN, value: 6000 },
          ],
        },
        [upcomingEnabledDailyUnread],
      ],
      [
        "digestIds and a filter",
        {
          digestIds: new Set(["due-enabled-weekly", "suggested-for-lisbon"]),
          attributeFilters: [enabled],
        },
        [dueEnabledWeekly],
      ],
      [
        "Contradictory filters",
        {
          attributeFilters: [
            enabled,
            {
              field: "status",
              comparator: EQUAL_TO,
              value: DIGEST_STATUS.DISABLED,
            },
          ],
        },
        [],
      ],
    ];
    for (const [name, options, expected] of cases) {
      Assert.deepEqual(
        (await DigestStore.getDigests(options)).map(digest => digest.id),
        expected.map(digest => digest.id),
        name
      );
    }
  } finally {
    await resetStore();
  }
});

/**
 * Tests getDigests returns copies rather than the same in-memory digests from the store
 */
add_task(async function test_get_digests_returns_copies() {
  try {
    await resetStore();
    const digest = makeDigest();
    await DigestStore.addDigest(digest);

    const digests = await DigestStore.getDigests();
    digests[0].title = "Changed outside the store";
    digests[0].schedule.hour = 23;
    digests[0].domains.push("changed.example");
    digests.length = 0;
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [digest],
      "Changing getDigests results, nested objects, or the array does not change stored digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests deleting a digest removes it from the store
 */
add_task(async function test_delete_digest() {
  try {
    await resetStore();
    const ids = [
      "3b241101-e2bb-4255-8caf-4136c566a962",
      "9c858901-8a57-4791-81fe-4c455b099bc9",
      "a7c9e0a4-7f0a-4a40-9d8b-1c2a3e4f5a6b",
    ];
    const digests = ids.map(id => makeDigest({ id }));
    for (const digest of digests) {
      await DigestStore.addDigest(digest);
    }
    await DigestStore.testOnlyFlushAndResetCache();

    Assert.strictEqual(
      await DigestStore.deleteDigest(ids[0]),
      true,
      "Deleting an existing digest returns true"
    );
    Assert.strictEqual(
      await DigestStore.deleteDigest(ids[0]),
      false,
      "Deleting an absent digest returns false"
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      digests.slice(1),
      "Deleting one digest leaves the others unchanged"
    );

    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      digests.slice(1),
      "Deletion persists after reopening"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Writes the given invalid digests between two valid digests, then checks that
 * loading keeps only the valid digests, unchanged.
 *
 * @param {Array} invalidDigests
 * @param {string} message
 */
async function assertLoadDropsInvalidDigests(invalidDigests, message) {
  await resetStore();
  // Create 2 valid digests to sandwich the invalid ones between
  const first = makeDigest({ id: "valid-first" });
  const last = makeDigest({
    id: "valid-last",
    schedule: { type: DIGEST_SCHEDULE_TYPE.DAILY, hour: 7, minute: 0 },
  });

  // Write the valid and invalid ones
  await IOUtils.writeJSON(
    path,
    { version: 1, digests: [first, ...invalidDigests, last] },
    { compress: true }
  );

  // Load the store and check only the valid ones exist
  const loaded = await DigestStore.getDigests();
  Assert.deepEqual(
    loaded.map(digest => digest.id),
    [first.id, last.id],
    `${message}: only the valid digests load`
  );
  Assert.deepEqual(
    loaded,
    [first, last],
    `${message}: the valid digests load unchanged`
  );
}

/**
 * Tests digests that are the wrong data type aren't loaded
 */
add_task(async function test_drops_non_object_digests_on_load() {
  try {
    await assertLoadDropsInvalidDigests(
      [null, "digest", 42, true, []],
      "Non-object digests"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digests that are missing rqeuired fields aren't loaded
 */
add_task(async function test_drops_digests_missing_fields_on_load() {
  try {
    const invalidDigests = Object.keys(makeDigest()).map(field => {
      const digest = makeDigest({ id: `missing-${field}` });
      delete digest[field];
      return digest;
    });
    await assertLoadDropsInvalidDigests(
      invalidDigests,
      "Digests missing a field"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digest objects that have valid fields with invalid values aren't loaded
 */
add_task(async function test_drops_digests_with_invalid_fields_on_load() {
  try {
    const invalidFields = [
      { id: "" },
      { status: "paused" },
      { unread: "true" },
      { memory_id: "   " },
      { ai_tab_slug: "" },
      { title: 42 },
      { description: null },
      { domains: [42] },
      { last_run_time: -1 },
      { last_read_time: "1000" },
      { next_run_time: "1000" },
      { created_at: 1.5 },
      { updated_at: null },
    ];
    const invalidDigests = invalidFields.map(invalid => {
      const [field] = Object.keys(invalid);
      return makeDigest({ id: `invalid-${field}`, ...invalid });
    });
    await assertLoadDropsInvalidDigests(
      invalidDigests,
      "Digests with an invalid field"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digests with invalid schedules aren't loaded
 */
add_task(async function test_drops_digests_with_invalid_schedules_on_load() {
  try {
    const { WEEKLY, DAILY, ON_DEMAND } = DIGEST_SCHEDULE_TYPE;
    const invalidSchedules = [
      null,
      "weekly",
      { type: "interval", hours: 4 },
      { type: WEEKLY, hour: 9, minute: 30 },
      { type: WEEKLY, weekday: 7, hour: 9, minute: 30 },
      { type: DAILY, hour: 9 },
      { type: DAILY, hour: 24, minute: 0 },
      { type: DAILY, weekday: 1, hour: 9, minute: 0 },
      { type: ON_DEMAND, hour: 9, minute: 0 },
    ];
    const invalidDigests = invalidSchedules.map(schedule =>
      makeDigest({
        id: `invalid-schedule-${JSON.stringify(schedule)}`,
        schedule,
      })
    );
    await assertLoadDropsInvalidDigests(
      invalidDigests,
      "Digests with an invalid schedule"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests digests with invalid fields only loads with its valid fields
 */
add_task(async function test_drops_unknown_fields_on_load() {
  try {
    await resetStore();
    const expected = makeDigest();
    await IOUtils.writeJSON(
      path,
      {
        version: 1,
        digests: [{ ...expected, unknown: true, nested: { value: 1 } }],
      },
      { compress: true }
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [expected],
      "Loading keeps the digest and removes unknown fields"
    );

    await DigestStore.requestSave();
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await IOUtils.readJSON(path, { decompress: true }),
      { version: 1, digests: [expected] },
      "The next save writes the digest without unknown fields"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests when 2 digests share an ID, only the first is loaded
 */
add_task(async function test_drops_duplicate_ids_on_load() {
  try {
    await resetStore();
    const first = makeDigest({ id: "duplicate", title: "First" });
    const other = makeDigest({ id: "other" });
    const duplicate = makeDigest({ id: "duplicate", title: "Duplicate" });
    await IOUtils.writeJSON(
      path,
      { version: 1, digests: [first, other, duplicate] },
      { compress: true }
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [first, other],
      "Loading keeps the first digest with an id and drops later duplicates"
    );

    Assert.strictEqual(
      await DigestStore.deleteDigest("duplicate"),
      true,
      "Deleting the duplicated id returns true"
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [other],
      "Deleting the duplicated id leaves no copy behind"
    );
    await DigestStore.testOnlyFlushAndResetCache();
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [other],
      "No copy of the duplicated id comes back after reopening"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests attempting to add an invalid digest doesn't block saving or loading valid ones
 */
add_task(async function test_invalid_digest_does_not_block_valid_duplicate() {
  try {
    await resetStore();
    const invalid = makeDigest({ id: "duplicate", status: "paused" });
    const valid = makeDigest({ id: "duplicate" });
    await IOUtils.writeJSON(
      path,
      { version: 1, digests: [invalid, valid] },
      { compress: true }
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [valid],
      "An invalid digest does not count as the first digest with its id"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests loading a mix of valid and invalid digests only keeps the valid ones
 */
add_task(async function test_keeps_only_valid_unique_digests_on_load() {
  try {
    await resetStore();
    const valid = makeDigest();
    await IOUtils.writeJSON(
      path,
      {
        version: 1,
        digests: [
          valid,
          null,
          makeDigest({ id: "invalid", status: "paused" }),
          makeDigest({ title: "Duplicate" }),
        ],
      },
      { compress: true }
    );
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [valid],
      "Loading a mix of invalid and duplicate digests keeps only the valid digest"
    );
  } finally {
    await resetStore();
  }
});

/**
 * Tests loading stores with bad shapes clears the store
 */
add_task(async function test_rejects_unsupported_store_shapes() {
  try {
    const cases = [
      {
        name: "Unsupported version",
        contents: { version: 2, digests: [makeDigest()] },
      },
      {
        name: "Digests are an object instead of an array",
        contents: { version: 1, digests: {} },
      },
      { name: "Missing digests", contents: { version: 1 } },
      { name: "Top-level array", contents: [makeDigest()] },
      { name: "Top-level string", contents: "not an object" },
    ];
    for (const { name, contents } of cases) {
      await resetStore();
      await IOUtils.writeJSON(path, contents, { compress: true });
      Assert.deepEqual(
        await DigestStore.getDigests(),
        [],
        `${name} loads as an empty store`
      );
    }
  } finally {
    await resetStore();
  }
});

/**
 * Test attempting to load a corrupted store clears the store
 */
add_task(async function test_corrupt_file_loads_as_empty_store() {
  const memoriesDir = PathUtils.parent(path);
  const removeCorruptBackups = async () => {
    for (const child of await IOUtils.getChildren(memoriesDir)) {
      if (PathUtils.filename(child).startsWith("digests.json.lz4.corrupt")) {
        await IOUtils.remove(child);
      }
    }
  };
  try {
    await resetStore();
    await IOUtils.writeUTF8(path, "not lz4 compressed json");
    Assert.deepEqual(
      await DigestStore.getDigests(),
      [],
      "An unreadable file loads as an empty store"
    );
  } finally {
    await resetStore();
    await removeCorruptBackups();
  }
});
