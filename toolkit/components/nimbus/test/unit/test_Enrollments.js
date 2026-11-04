/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PendingWrites } = ChromeUtils.importESModule(
  "resource://nimbus/lib/Enrollments.sys.mjs"
);

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

add_task(async function testPendingEnrollmentsMergeTimestamps() {
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

add_task(async function testEnrollments() {
  const { sandbox, manager, cleanup } = await NimbusTestUtils.setupTest();
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
  const { manager, cleanup } = await NimbusTestUtils.setupTest();
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
