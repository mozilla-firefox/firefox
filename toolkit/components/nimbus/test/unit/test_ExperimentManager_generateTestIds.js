"use strict";

const { ExperimentManager } = ChromeUtils.importESModule(
  "resource://nimbus/lib/ExperimentManager.sys.mjs"
);

const TEST_CONFIG = {
  slug: "test-experiment",
  branches: [
    {
      slug: "control",
      ratio: 1,
    },
    {
      slug: "branchA",
      ratio: 1,
    },
    {
      slug: "branchB",
      ratio: 1,
    },
  ],
  namespace: "test-namespace",
  start: 0,
  count: 2000,
  total: 10000,
};

add_task(async function test_generateTestIds() {
  let result = await ExperimentManager.generateTestIds(TEST_CONFIG);

  Assert.ok(result, "should return object");
  Assert.ok(result.notInExperiment, "should have a id for no experiment");
  Assert.ok(result.control, "should have id for control");
  Assert.ok(result.branchA, "should have id for branchA");
  Assert.ok(result.branchB, "should have id for branchB");
});

add_task(async function test_generateTestIds_bucketConfig() {
  const { slug, branches, namespace, start, count, total } = TEST_CONFIG;
  const result = await ExperimentManager.generateTestIds({
    slug,
    branches,
    bucketConfig: { namespace, start, count, total },
  });

  Assert.ok(result, "should return object");
  Assert.ok(result.notInExperiment, "should have a id for no experiment");
  Assert.ok(result.control, "should have id for control");
  Assert.ok(result.branchA, "should have id for branchA");
  Assert.ok(result.branchB, "should have id for branchB");
});

add_task(async function test_generateTestIds_withoutNot() {
  const result = await ExperimentManager.generateTestIds({
    ...TEST_CONFIG,
    count: TEST_CONFIG.total,
  });

  Assert.ok(result, "should return object");
  Assert.equal(
    result.notInExperiment,
    undefined,
    "should not have a id for no experiment"
  );
  Assert.ok(result.control, "should have id for control");
  Assert.ok(result.branchA, "should have id for branchA");
  Assert.ok(result.branchB, "should have id for branchB");
});

add_task(async function test_generateTestIds_input_errors() {
  const { slug, branches, namespace, start, count, total } = TEST_CONFIG;
  await Assert.rejects(
    ExperimentManager.generateTestIds({
      branches,
      namespace,
      start,
      count,
      total,
    }),
    /slug, namespace not in expected format/,
    "should throw because of missing slug"
  );

  await Assert.rejects(
    ExperimentManager.generateTestIds({ slug, branches, start, count, total }),
    /slug, namespace not in expected format/,
    "should throw because of missing namespace"
  );

  await Assert.rejects(
    ExperimentManager.generateTestIds({
      slug,
      branches,
      namespace,
      count,
      total,
    }),
    /Must include start, count, and total as integers/,
    "should throw beause of missing start"
  );

  await Assert.rejects(
    ExperimentManager.generateTestIds({
      slug,
      branches,
      namespace,
      start,
      total,
    }),
    /Must include start, count, and total as integers/,
    "should throw beause of missing count"
  );

  await Assert.rejects(
    ExperimentManager.generateTestIds({
      slug,
      branches,
      namespace,
      count,
      start,
    }),
    /Must include start, count, and total as integers/,
    "should throw beause of missing total"
  );

  // Intentionally misspelled slug
  let invalidBranches = [
    { slug: "a", ratio: 1 },
    { slugG: "b", ratio: 1 },
  ];

  await Assert.rejects(
    ExperimentManager.generateTestIds({
      slug,
      branches: invalidBranches,
      namespace,
      start,
      count,
      total,
    }),
    /branches parameter not in expected format/,
    "should throw because of invalid format for branches"
  );
});
