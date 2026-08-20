/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {
  getRemoteClient,
  getRemoteRecords,
  parseVersion,
  FEATURE_MAJOR_VERSIONS,
  _setRemoteClientForTesting,
  _clearRemoteClientForTesting,
  _clearRecordsCacheForTesting,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

registerCleanupFunction(() => _clearRemoteClientForTesting());

add_task(async function test_feature_major_versions_in_dump() {
  const client = getRemoteClient();
  const records = await client.get();
  for (const [feature, majorVersion] of Object.entries(
    FEATURE_MAJOR_VERSIONS
  )) {
    const found = records.some(r => {
      const parsed = parseVersion(r.version);
      return r.feature === feature && parsed?.major === majorVersion;
    });
    Assert.ok(
      found,
      `WARNING - UPDATE THE DUMP: no records for feature "${feature}" at major version ${majorVersion} in ` +
        `services/settings/dumps/main/ai-window-prompts.json. ` +
        `Either update the dump or revert FEATURE_MAJOR_VERSIONS["${feature}"].`
    );
  }
});

add_task(async function test_getRemoteRecords_reads_collection_once() {
  let reads = 0;
  const records = [{ id: "one" }];
  _setRemoteClientForTesting({
    get: async () => {
      reads++;
      return records;
    },
  });

  const first = await getRemoteRecords();
  const second = await getRemoteRecords();

  Assert.equal(
    reads,
    1,
    "Repeat callers should share a single collection read"
  );
  Assert.equal(first, second, "Both callers should get the same records array");
  Assert.equal(first[0].id, "one", "Records should come from the client");
});

add_task(async function test_getRemoteRecords_dedupes_concurrent_reads() {
  let reads = 0;
  let resolveRead;
  _setRemoteClientForTesting({
    get: () => {
      reads++;
      return new Promise(resolve => {
        resolveRead = resolve;
      });
    },
  });

  // Both callers arrive before the first read resolves, which is the shape a
  // chat submit produces: engine build and prompt assembly resolve records
  // without awaiting each other.
  const first = getRemoteRecords();
  const second = getRemoteRecords();
  Assert.equal(reads, 1, "An in-flight read should be shared, not reissued");

  resolveRead([{ id: "concurrent" }]);
  const [firstRecords, secondRecords] = await Promise.all([first, second]);
  Assert.equal(firstRecords, secondRecords, "Both should resolve to one array");
  Assert.equal(reads, 1, "Still only one read after both resolved");
});

add_task(async function test_getRemoteRecords_does_not_cache_failures() {
  let reads = 0;
  _setRemoteClientForTesting({
    get: async () => {
      reads++;
      if (reads === 1) {
        throw new Error("transient read failure");
      }
      return [{ id: "recovered" }];
    },
  });

  await Assert.rejects(
    getRemoteRecords(),
    /transient read failure/,
    "The failing read should propagate to its caller"
  );

  const records = await getRemoteRecords();
  Assert.equal(
    records[0].id,
    "recovered",
    "A later call should retry rather than replay the failure"
  );
  Assert.equal(reads, 2, "The failed read should not have been cached");
});

add_task(async function test_getRemoteRecords_rereads_after_invalidation() {
  let reads = 0;
  let published = [{ id: "before" }];
  _setRemoteClientForTesting({
    get: async () => {
      reads++;
      return published;
    },
  });

  await getRemoteRecords();
  published = [{ id: "after" }];

  Assert.equal(
    (await getRemoteRecords())[0].id,
    "before",
    "Records should stay cached until something invalidates them"
  );

  _clearRecordsCacheForTesting();

  Assert.equal(
    (await getRemoteRecords())[0].id,
    "after",
    "Invalidating should pick up newly published records"
  );
  Assert.equal(reads, 2, "Exactly one read before and one after invalidation");
});

add_task(async function test_parseVersion_with_v_prefix() {
  const result = parseVersion("v1.0");
  Assert.ok(result, "Should parse version with v prefix");
  Assert.equal(result.major, 1, "Major version should be 1");
  Assert.equal(result.minor, 0, "Minor version should be 0");
  Assert.equal(result.original, "v1.0", "Original should be preserved");
});

add_task(async function test_parseVersion_without_v_prefix() {
  const result = parseVersion("1.0");
  Assert.ok(result, "Should parse version without v prefix");
  Assert.equal(result.major, 1, "Major version should be 1");
  Assert.equal(result.minor, 0, "Minor version should be 0");
  Assert.equal(result.original, "1.0", "Original should be preserved");
});

add_task(async function test_parseVersion_with_higher_numbers() {
  const result = parseVersion("2.15");
  Assert.ok(result, "Should parse version with higher numbers");
  Assert.equal(result.major, 2, "Major version should be 2");
  Assert.equal(result.minor, 15, "Minor version should be 15");
  Assert.equal(result.original, "2.15", "Original should be preserved");
});

add_task(async function test_parseVersion_invalid_format() {
  Assert.equal(
    parseVersion("v1"),
    null,
    "Should return null for version without minor"
  );
  Assert.equal(parseVersion("1"), null, "Should return null for single number");
  Assert.equal(
    parseVersion("v1.0.0"),
    null,
    "Should return null for three part version"
  );
  Assert.equal(
    parseVersion("invalid"),
    null,
    "Should return null for non-numeric version"
  );
});

add_task(async function test_parseVersion_edge_cases() {
  Assert.equal(parseVersion(""), null, "Should return null for empty string");
  Assert.equal(parseVersion(null), null, "Should return null for null");
  Assert.equal(
    parseVersion(undefined),
    null,
    "Should return null for undefined"
  );
  Assert.equal(
    parseVersion("v1.0extra"),
    null,
    "Should return null for version with extra text after"
  );
});
