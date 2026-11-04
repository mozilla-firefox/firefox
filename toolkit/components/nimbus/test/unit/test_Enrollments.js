/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PendingWrites } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Enrollments.sys.mjs"
);
const {
  NimbusTelemetry: { EnrollmentStatus, EnrollmentStatusReason, UnenrollReason },
} = ChromeUtils.importESModule("resource://nimbus/lib/Telemetry.sys.mjs");

// PendingWrites does not need any actual detail in the recipes
// and it only needs the slug in the enrollment.
function newEnrollment(slug = "foo") {
  return { slug, active: true };
}
const RECIPE = {};

add_task(async function testPendingWritesEnrollments() {
  // Testing enroll -> unenroll -> delete
  {
    const pendingWrites = new PendingWrites();
    Assert.ok(!pendingWrites.hasPendingWrites);

    const activeEnrollment = newEnrollment();

    pendingWrites.recordEnrollment(activeEnrollment, RECIPE);

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.INSERT,
      enrollment: { slug: "foo", active: true },
      recipe: RECIPE,
    });
    Assert.notEqual(
      pendingWrites.enrollments.get("foo").enrollment,
      activeEnrollment,
      "enrollment is a structuredClone"
    );

    const inactiveEnrollment = { ...activeEnrollment, active: false };
    pendingWrites.recordUnenrollment(inactiveEnrollment);

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.INSERT,
      enrollment: { slug: "foo", active: false },
      recipe: RECIPE,
    });
    Assert.notEqual(
      pendingWrites.enrollments.get("foo").enrollment,
      inactiveEnrollment,
      "enrollment is a structuredClone"
    );

    pendingWrites.deleteEnrollment("foo");

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.DELETE,
    });
  }

  // Testing unenroll -> delete
  {
    const pendingWrites = new PendingWrites();

    Assert.ok(!pendingWrites.hasPendingWrites);

    const enrollment = { ...newEnrollment(), active: false };
    pendingWrites.recordUnenrollment(enrollment);

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.UPDATE,
      enrollment: { slug: "foo", active: false },
    });
    Assert.notEqual(
      pendingWrites.enrollments.get("foo").enrollment,
      enrollment,
      "enrollment is a different object"
    );

    pendingWrites.deleteEnrollment("foo");

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.DELETE,
    });
  }

  // Testing unenroll -> enroll
  {
    const pendingWrites = new PendingWrites();

    Assert.ok(!pendingWrites.hasPendingWrites);

    const inactiveEnrollment = { ...newEnrollment(), active: false };
    pendingWrites.recordUnenrollment(inactiveEnrollment);

    const activeEnrollment = newEnrollment();
    pendingWrites.recordEnrollment(activeEnrollment, RECIPE);

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.INSERT,
      enrollment: { slug: "foo", active: true },
      recipe: RECIPE,
    });
    Assert.notEqual(
      pendingWrites.enrollments.get("foo").enrollment,
      activeEnrollment,
      "enrollment is a different object"
    );
  }

  // Testing delete -> enroll
  {
    const pendingWrites = new PendingWrites();

    Assert.ok(!pendingWrites.hasPendingWrites);

    pendingWrites.deleteEnrollment("foo");

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.DELETE,
    });

    const activeEnrollment = newEnrollment();
    pendingWrites.recordEnrollment(activeEnrollment, RECIPE);

    Assert.ok(pendingWrites.hasPendingWrites);
    Assert.deepEqual(pendingWrites.enrollments.get("foo"), {
      kind: PendingWrites.Kind.INSERT,
      enrollment: { slug: "foo", active: true },
      recipe: RECIPE,
    });
    Assert.notEqual(
      pendingWrites.enrollments.get("foo").enrollment,
      activeEnrollment,
      "enrollment is a different object"
    );
  }
});

add_task(async function testPendingWritesEnrollmentStatuses() {
  const pendingWrites = new PendingWrites();

  Assert.ok(!pendingWrites.hasPendingWrites);

  pendingWrites.recordEnrollmentStatus("foo", { sentinel: 1 });

  Assert.ok(pendingWrites.hasPendingWrites);

  pendingWrites.recordEnrollmentStatus("foo", { sentinel: 2 });
  pendingWrites.recordEnrollmentStatus("foo", { sentinel: 3 });
  pendingWrites.recordEnrollmentStatus("bar", { sentinel: 4 });

  Assert.deepEqual(pendingWrites.enrollmentStatuses.get("foo"), [
    { sentinel: 1 },
    { sentinel: 2 },
    { sentinel: 3 },
  ]);
  Assert.deepEqual(pendingWrites.enrollmentStatuses.get("bar"), [
    { sentinel: 4 },
  ]);

  pendingWrites.deleteEnrollment("foo");
  Assert.ok(!pendingWrites.enrollmentStatuses.has("foo"));
});

add_task(async function testPendingWritesMergeEnrollments() {
  // unenroll -> unenroll and delete -> unenroll are not tested because they are
  // not possible transitions

  const a = new PendingWrites();
  a.recordEnrollment(
    { ...newEnrollment("insert-null"), sentinel: "a" },
    RECIPE
  );
  a.recordEnrollment(
    { ...newEnrollment("insert-insert"), sentinel: "a" },
    RECIPE
  );
  a.recordEnrollment(
    { ...newEnrollment("insert-update"), sentinel: "a" },
    RECIPE
  );
  a.recordEnrollment(
    { ...newEnrollment("insert-delete"), sentinel: "a" },
    RECIPE
  );

  a.recordUnenrollment(
    { ...newEnrollment("update-null"), sentinel: "a" },
    RECIPE
  );
  a.recordUnenrollment(
    { ...newEnrollment("update-insert"), sentinel: "a" },
    RECIPE
  );
  a.recordUnenrollment(
    { ...newEnrollment("update-update"), sentinel: "a" },
    RECIPE
  );
  a.recordUnenrollment(
    { ...newEnrollment("update-delete"), sentinel: "a" },
    RECIPE
  );

  a.deleteEnrollment("delete-null");
  a.deleteEnrollment("delete-insert");
  a.deleteEnrollment("delete-delete");

  const b = new PendingWrites();
  b.recordEnrollment(
    { ...newEnrollment("null-insert"), sentinel: "b" },
    RECIPE
  );
  b.recordEnrollment(
    { ...newEnrollment("insert-insert"), sentinel: "b" },
    RECIPE
  );
  b.recordEnrollment(
    { ...newEnrollment("update-insert"), sentinel: "b" },
    RECIPE
  );
  b.recordEnrollment(
    { ...newEnrollment("delete-insert"), sentinel: "b" },
    RECIPE
  );

  b.recordUnenrollment(
    { ...newEnrollment("null-update"), sentinel: "b" },
    RECIPE
  );
  b.recordUnenrollment(
    { ...newEnrollment("insert-update"), sentinel: "b" },
    RECIPE
  );

  b.deleteEnrollment("null-delete");
  b.deleteEnrollment("insert-delete");
  b.deleteEnrollment("update-delete");
  b.deleteEnrollment("delete-delete");

  const merged = PendingWrites.merge(a, b);
  Assert.deepEqual(merged.enrollments.get("null-insert"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "null-insert", active: true, sentinel: "b" },
    recipe: RECIPE,
  });
  Assert.deepEqual(merged.enrollments.get("null-update"), {
    kind: PendingWrites.Kind.UPDATE,
    enrollment: { slug: "null-update", active: true, sentinel: "b" },
  });
  Assert.deepEqual(merged.enrollments.get("null-delete"), {
    kind: PendingWrites.Kind.DELETE,
  });

  Assert.deepEqual(merged.enrollments.get("insert-insert"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "insert-insert", active: true, sentinel: "b" },
    recipe: RECIPE,
  });
  Assert.deepEqual(merged.enrollments.get("insert-update"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "insert-update", active: true, sentinel: "b" },
    recipe: RECIPE,
  });
  Assert.deepEqual(merged.enrollments.get("insert-delete"), {
    kind: PendingWrites.Kind.DELETE,
  });
  Assert.deepEqual(merged.enrollments.get("insert-null"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "insert-null", active: true, sentinel: "a" },
    recipe: RECIPE,
  });

  Assert.deepEqual(merged.enrollments.get("update-insert"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "update-insert", active: true, sentinel: "b" },
    recipe: RECIPE,
  });

  Assert.deepEqual(merged.enrollments.get("update-delete"), {
    kind: PendingWrites.Kind.DELETE,
  });
  Assert.deepEqual(merged.enrollments.get("update-null"), {
    kind: PendingWrites.Kind.UPDATE,
    enrollment: { slug: "update-null", active: true, sentinel: "a" },
  });

  Assert.deepEqual(merged.enrollments.get("delete-insert"), {
    kind: PendingWrites.Kind.INSERT,
    enrollment: { slug: "delete-insert", active: true, sentinel: "b" },
    recipe: RECIPE,
  });
  Assert.deepEqual(merged.enrollments.get("delete-delete"), {
    kind: PendingWrites.Kind.DELETE,
  });
  Assert.deepEqual(merged.enrollments.get("delete-null"), {
    kind: PendingWrites.Kind.DELETE,
  });
});

add_task(async function testPendingWritesMergeTimestamps() {
  const a = new PendingWrites();
  a.syncTimestamps.set("a", 1);
  a.syncTimestamps.set("b", 1);

  const b = new PendingWrites();
  b.syncTimestamps.set("b", 2);
  b.syncTimestamps.set("c", 3);

  const merged = PendingWrites.merge(a, b);

  Assert.deepEqual(merged.syncTimestamps.get("a"), 1);
  Assert.deepEqual(merged.syncTimestamps.get("b"), 2);
  Assert.deepEqual(merged.syncTimestamps.get("c"), 3);
});

add_task(async function testPendingWritesMergeStatuses() {
  const a = new PendingWrites();
  a.recordEnrollmentStatus("foo", { foo: 1 });
  a.recordEnrollmentStatus("foo", { foo: 2 });
  a.recordEnrollmentStatus("bar", { bar: 1 });
  a.recordEnrollmentStatus("baz", { baz: 1 });
  a.deleteEnrollment("qux");

  const b = new PendingWrites();
  b.recordEnrollmentStatus("foo", { foo: 3 });
  b.recordEnrollmentStatus("bar", { bar: 2 });
  b.recordEnrollmentStatus("bar", { bar: 3 });
  b.deleteEnrollment("baz");
  b.recordEnrollmentStatus("qux", { qux: 1 });

  const merged = PendingWrites.merge(a, b);

  Assert.deepEqual(merged.enrollmentStatuses.get("foo"), [
    { foo: 1 },
    { foo: 2 },
    { foo: 3 },
  ]);

  Assert.deepEqual(merged.enrollmentStatuses.get("bar"), [
    { bar: 1 },
    { bar: 2 },
    { bar: 3 },
  ]);

  Assert.ok(!merged.enrollmentStatuses.has("baz"));
  Assert.deepEqual(merged.enrollmentStatuses.get("qux"), [{ qux: 1 }]);
});

add_task(async function testEnrollments() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest({
    clearTelemetry: true,
  });
  const db = manager.store._db;
  sandbox.spy(db, "recordEnrollment");
  sandbox.spy(db, "recordUnenrollment");
  sandbox.spy(db, "deleteEnrollments");

  Assert.equal(db.pendingWrites, 0, "No pending writes");

  const recipe = NimbusTestUtils.factories.recipe("test");
  await manager.enroll(recipe, "test");
  const enrollment = manager.store.get(recipe.slug);

  Assert.equal(db.pendingWrites, 1, "1 pending write");
  Assert.ok(db.recordEnrollment.calledOnceWithExactly(enrollment, recipe));
  Assert.ok(db.recordUnenrollment.notCalled);
  Assert.ok(db.deleteEnrollments.notCalled);

  db.recordEnrollment.resetHistory();

  Assert.equal(
    await NimbusTestUtils.queryEnrollment(recipe.slug),
    null,
    "Enrollment does not exist in database yet"
  );

  await NimbusTestUtils.flushStore();

  Assert.equal(db.pendingWrites, 0, "No remaining pending writes");

  {
    const dbEnrollment = await NimbusTestUtils.queryEnrollment(recipe.slug);
    Assert.notEqual(dbEnrollment, null, "Enrollment exists in database");

    Assert.equal(
      dbEnrollment.branchSlug,
      enrollment.branch.slug,
      "Has the correct branch slug"
    );
    Assert.deepEqual(dbEnrollment.recipe, recipe, "Has the recipe attached");
  }

  manager.unenroll("test", { reason: "reason123" });

  Assert.equal(db.pendingWrites, 1, "1 pending write");
  Assert.ok(db.recordEnrollment.notCalled);
  Assert.ok(
    db.recordUnenrollment.calledOnceWithExactly(manager.store.get(recipe.slug))
  );
  Assert.ok(db.deleteEnrollments.notCalled);

  db.recordUnenrollment.resetHistory();

  {
    const dbEnrollment = await NimbusTestUtils.queryEnrollment(recipe.slug);
    Assert.notEqual(
      dbEnrollment,
      null,
      "Enrollment still exists in the database"
    );
    Assert.ok(dbEnrollment.active, "database change has not been flushed yet");
    Assert.equal(dbEnrollment.unenrollReason, null, "Unenroll reason");
  }

  await NimbusTestUtils.flushStore();

  {
    const dbEnrollment = await NimbusTestUtils.queryEnrollment(recipe.slug);
    Assert.notEqual(
      dbEnrollment,
      null,
      "Enrollment still exists in the database"
    );
    Assert.ok(!dbEnrollment.active, "Enrollment no longer active");
    Assert.equal(dbEnrollment.unenrollReason, "reason123", "Unenroll reason");
  }

  manager.store._deleteForTests(recipe.slug);

  Assert.equal(db.pendingWrites, 1, "1 pending write");
  Assert.ok(db.recordEnrollment.notCalled);
  Assert.ok(db.recordUnenrollment.notCalled);
  Assert.ok(db.deleteEnrollments.calledOnceWithExactly([recipe.slug]));

  Assert.notEqual(
    await NimbusTestUtils.queryEnrollment(recipe.slug),
    null,
    "Enrollment still exists"
  );

  await NimbusTestUtils.flushStore();

  Assert.equal(
    await NimbusTestUtils.queryEnrollment(recipe.slug),
    null,
    "Enrollment deleted from database"
  );

  await cleanup();
});

add_task(async function testCollapseWrites() {
  const { manager, cleanup } = await NimbusTestUtils.setupTest({
    clearTelemetry: true,
  });
  const db = manager.store._db;

  const recipe = NimbusTestUtils.factories.recipe("unenroll-immediately");

  await manager.enroll(recipe, "test");
  manager.unenroll(recipe.slug, { reason: "some reason" });

  Assert.equal(db.pendingWrites, 1, "1 pending write");

  await NimbusTestUtils.flushStore();

  Assert.equal(db.pendingWrites, 0, "No pending writes");

  const dbEnrollment = await NimbusTestUtils.queryEnrollment(recipe.slug);

  Assert.notEqual(dbEnrollment, null, "Enrollment exists in database");
  Assert.ok(!dbEnrollment.active, "Enrollment is not active");
  Assert.equal(dbEnrollment.unenrollReason, "some reason");

  await cleanup();
});

async function doTestStatuses({ reducedTelemetry = false } = {}) {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest({
    clearTelemetry: true,
  });

  if (reducedTelemetry) {
    await manager.enroll(
      NimbusTestUtils.factories.recipe.withFeatureConfig("nimbus-telemetry", {
        featureId: "nimbusTelemetry",
        value: {
          reducedEnrollmentStatusTelemetryEnabled: true,
        },
      }),
      "test"
    );
    // Submit the enrollment status event from enrolling in the above recipe.
    GleanPings.nimbusTargetingContext.submit();
  }

  const db = manager.store._db;

  const statuses = [
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.ERROR,
      error: "uh-oh",
    },
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.ERROR,
      error: "another error",
    },
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.NOT_SELECTED,
    },
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.NOT_TARGETED,
    },
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.FEATURE_CONFLICT,
      conflictSlugs: ["foo", "baz"].toSorted(),
    },
    {
      status: EnrollmentStatus.NOT_ENROLLED,
      reason: EnrollmentStatusReason.FEATURE_CONFLICT,
      conflictSlugs: ["foo", "bar"].toSorted(),
    },
    {
      status: EnrollmentStatus.ENROLLED,
      reason: EnrollmentStatusReason.QUALIFIED,
      branch: "control",
    },
    {
      status: EnrollmentStatus.ENROLLED,
      reason: EnrollmentStatusReason.QUALIFIED,
      branch: "treatment",
    },
    {
      status: EnrollmentStatus.DISQUALIFIED,
      reason: EnrollmentStatusReason.MIGRATION,
      migration: "foo",
      branch: "treatment",
    },
    {
      status: EnrollmentStatus.DISQUALIFIED,
      reason: EnrollmentStatusReason.MIGRATION,
      migration: "bar",
      branch: "treatment",
    },
    {
      status: EnrollmentStatus.WAS_ENROLLED,
    },
  ];

  {
    let i = 1;
    sandbox.stub(NimbusEnrollments, "now").callsFake(() => new Date(i++));
  }

  for (const [idx, status] of statuses.entries()) {
    // Recording the same status twice should only result in a single entry.
    db.recordEnrollmentStatus("slug", status);
    db.recordEnrollmentStatus("slug", status);

    await NimbusTestUtils.saveStore(manager.store);

    const latestEntry = await NimbusEnrollments.loadEnrollmentStatuses().then(
      statuses => statuses.get("slug")
    );

    const expectedEntry = {
      index: idx + 1,
      timestamp: new Date(idx + 1),
      status,
    };

    Assert.deepEqual(expectedEntry, db.getLatestEnrollmentStatus("slug"));
    Assert.deepEqual(expectedEntry, latestEntry);
  }

  const entries =
    await NimbusTestUtils.loadEnrollmentStatusEntriesForSlug("slug");

  const expectedEntries = statuses.map((status, idx) => ({
    timestamp: new Date(idx + 1),
    index: idx + 1,
    status,
  }));

  Assert.deepEqual(entries, expectedEntries);

  const expectedTelemetry = (
    reducedTelemetry ? expectedEntries : expectedEntries.flatMap(e => [e, e])
  ).map(
    ({ status: { status, reason, branch, error, conflictSlugs, migration } }) =>
      Object.assign(
        {
          slug: "slug",
          status,
        },
        typeof reason !== "undefined" ? { reason } : {},
        typeof branch !== "undefined" ? { branch } : {},
        typeof migration !== "undefined" ? { migration } : {},
        typeof error !== "undefined" ? { error_string: error } : {},
        typeof conflictSlugs !== "undefined"
          ? { conflict_slug: conflictSlugs.join(",") }
          : {}
      )
  );

  Assert.deepEqual(
    Glean.nimbusEvents.enrollmentStatus
      .testGetValue("nimbus-targeting-context")
      ?.map(ev => ev.extra) ?? [],
    expectedTelemetry
  );

  db.deleteEnrollments(["slug"]);
  Assert.equal(
    db.getLatestEnrollmentStatus("slug"),
    undefined,
    "Enrollment status removed from in-memory store immediately"
  );
  await NimbusTestUtils.saveStore(manager.store);

  Assert.deepEqual(
    await NimbusTestUtils.loadEnrollmentStatusEntriesForSlug("slug"),
    [],
    "Enrollment status entries removed from the database by a deletion"
  );

  if (reducedTelemetry) {
    await NimbusTestUtils.cleanupManager(["nimbus-telemetry"]);
  }

  await cleanup();
}

add_task(function testStatusesNormalTelemetry() {
  return doTestStatuses();
});

add_task(function testStatusesReducedTelemetry() {
  return doTestStatuses({ reducedTelemetry: true });
});

add_task(async function testStatusNotSubmittedAfterDelete() {
  const { manager, cleanup } = await NimbusTestUtils.setupTest();

  const db = manager.store._db;

  await manager.enroll(
    NimbusTestUtils.factories.recipe.withFeatureConfig("slug", {
      featureId: "no-feature-firefox-desktop",
    }),
    "rs-loader"
  );

  Assert.deepEqual(db.getLatestEnrollmentStatus("slug")?.status, {
    status: EnrollmentStatus.ENROLLED,
    reason: EnrollmentStatusReason.QUALIFIED,
    branch: "control",
  });
  Assert.equal(db.pendingWrites, 1);

  manager.unenroll("slug", { reason: UnenrollReason.INDIVIDUAL_OPT_OUT });
  Assert.deepEqual(db.getLatestEnrollmentStatus("slug")?.status, {
    status: EnrollmentStatus.DISQUALIFIED,
    reason: EnrollmentStatusReason.OPT_OUT,
    branch: "control",
  });
  Assert.equal(db.pendingWrites, 1);

  manager.store._deleteForTests("slug");
  Assert.equal(
    db.getLatestEnrollmentStatus("slug"),
    null,
    "No enrollment status"
  );

  // A delete still counts as a write.
  Assert.equal(db.pendingWrites, 1);

  await NimbusTestUtils.saveStore(manager.store);

  // The store is now empty.

  await cleanup();
});
