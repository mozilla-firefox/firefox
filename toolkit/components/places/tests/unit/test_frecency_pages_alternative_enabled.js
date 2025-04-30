/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests alternative pages frecency when the preference is enabled on startup.
// Note: the order of the tests here matters, since we are emulating subsquent
// starts of the recalculator component with different initial conditions.

async function restartRecalculator() {
  let subject = {};
  PlacesFrecencyRecalculator.observe(
    subject,
    "test-alternative-frecency-init",
    ""
  );
  await subject.promise;
}

async function getAllPages() {
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(`SELECT * FROM moz_places`);
  Assert.greater(rows.length, 0);
  return rows.map(r => ({
    url: r.getResultByName("url"),
    frecency: r.getResultByName("frecency"),
    recalc_frecency: r.getResultByName("recalc_frecency"),
    alt_frecency: r.getResultByName("alt_frecency"),
    recalc_alt_frecency: r.getResultByName("recalc_alt_frecency"),
  }));
}

add_setup(async function () {
  await PlacesTestUtils.addVisits([
    "https://testdomain1.moz.org",
    "https://testdomain2.moz.org",
    "https://testdomain3.moz.org",
  ]);

  Assert.ok(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.enabled,
    "Check the pref is enabled"
  );

  registerCleanupFunction(PlacesUtils.history.clear);
});

add_task(async function test_init() {
  // Set alt_frecency to NULL and recalc_alt_frecency = 0 for the entries in
  // moz_places to verify they are recalculated.
  await PlacesTestUtils.updateDatabaseValues("moz_places", {
    alt_frecency: null,
    recalc_alt_frecency: 1,
  });

  // Upon startup of this test file, the constructor sets PlacesUtils.metadata
  // which will cause subsequent calls to restartRecalculator to never
  // immediately trigger a frecency recalculation. Thus, we deliberately reset
  // metadata for restartRecalculator() to simulate construction of the class
  // and trigger routines that should happen on startup.
  await PlacesTestUtils.clearMetadata();
  PlacesUtils.metadata.cache.clear();

  Assert.equal(
    await PlacesUtils.metadata.get(
      PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
      Object.create(null)
    ).version,
    undefined,
    "Check the algorithm version has not been stored"
  );

  await restartRecalculator();

  // Avoid hitting the cache, we want to check the actual database value.
  Assert.equal(
    (
      await PlacesUtils.metadata.get(
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
        Object.create(null)
      )
    ).version,
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.variables.version,
    "Check the algorithm version has been stored"
  );

  // Check all alternative frecencies have been calculated, since we just have
  // a few.
  let pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    pages.every(p => p.alt_frecency > 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    PlacesFrecencyRecalculator.isRecalculationPending,
    "Recalculation should be pending"
  );
});

add_task(async function test_different_version() {
  let pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 0),
    "All the entries should not need recalculation"
  );

  // Set alt_frecency to NULL to verify all the entries are recalculated.
  await PlacesTestUtils.updateDatabaseValues("moz_places", {
    alt_frecency: null,
  });

  // It doesn't matter that the version is, it just have to be different.
  let variables = await PlacesUtils.metadata.get(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
    Object.create(null)
  );
  variables.version = 999;
  await PlacesUtils.metadata.set(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
    variables
  );

  await restartRecalculator();

  // Check alternative frecency has been marked for recalculation.
  // Note just after init we reculate a chunk, and this test code is expected
  // to run before that... though we can't be sure, so if this starts failing
  // intermittently we'll have to add more synchronization test code.
  pages = await getAllPages();
  // Ensure moz_meta has been updated.
  Assert.ok(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.enabled,
    "Check the pref is enabled"
  );
  // Avoid hitting the cache, we want to check the actual database value.
  PlacesUtils.metadata.cache.clear();
  Assert.deepEqual(
    (
      await PlacesUtils.metadata.get(
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
        Object.create(null)
      )
    ).version,
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.variables.version,
    "Check the algorithm version has been stored"
  );

  // Check all alternative frecencies have been calculated, since we just have
  // a few.
  pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    pages.every(p => p.alt_frecency > 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    PlacesFrecencyRecalculator.isRecalculationPending,
    "Recalculation should be pending"
  );
});

add_task(async function test_different_variables() {
  let pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 0),
    "All the entries should not need recalculation"
  );

  // Set alt_frecency to NULL to verify all the entries are recalculated.
  await PlacesTestUtils.updateDatabaseValues("moz_places", {
    alt_frecency: null,
  });

  // Change variables.
  let variables = await PlacesUtils.metadata.get(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
    Object.create(null)
  );
  Assert.greater(Object.keys(variables).length, 1);
  Assert.ok("version" in variables, "At least the version is always present");
  await PlacesUtils.metadata.set(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
    {
      version:
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.variables
          .version,
      someVar: 1,
    }
  );

  await restartRecalculator();

  // Ensure moz_meta has been updated.
  Assert.ok(
    PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.enabled,
    "Check the pref is enabled"
  );
  // Avoid hitting the cache, we want to check the actual database value.
  PlacesUtils.metadata.cache.clear();
  Assert.deepEqual(
    await PlacesUtils.metadata.get(
      PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
      Object.create(null)
    ),
    variables,
    "Check the algorithm variables have been stored"
  );

  // Check all alternative frecencies have been calculated, since we just have
  // a few.
  pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    pages.every(p => p.alt_frecency > 0),
    "All the entries have been recalculated"
  );
  Assert.ok(
    PlacesFrecencyRecalculator.isRecalculationPending,
    "Recalculation should be pending"
  );
});

add_task(async function test_score() {
  await restartRecalculator();

  // This is not intended to cover the algorithm as a whole, but just as a
  // sanity check for scores.

  // Add before visits to properly set visit source.
  await PlacesUtils.bookmarks.insert({
    url: "https://visitedbookmark.moz.org",
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
  });

  await PlacesTestUtils.addVisits([
    {
      url: "https://low.moz.org",
      transition: PlacesUtils.history.TRANSITIONS.FRAMED_LINK,
    },
    {
      url: "https://visitedbookmark.moz.org",
      transition: PlacesUtils.history.TRANSITIONS.FRAMED_LINK,
    },
    {
      url: "https://old.moz.org",
      visitDate: (Date.now() - 2 * 86400000) * 1000,
    },
    { url: "https://base.moz.org" },
    { url: "https://manyvisits.moz.org" },
    { url: "https://manyvisits.moz.org" },
    { url: "https://manyvisits.moz.org" },
    { url: "https://manyvisits.moz.org" },
  ]);
  await PlacesUtils.bookmarks.insert({
    url: "https://unvisitedbookmark.moz.org",
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  let getFrecency = url =>
    PlacesTestUtils.getDatabaseValue("moz_places", "alt_frecency", {
      url,
    });
  let low = await getFrecency("https://low.moz.org/");
  let old = await getFrecency("https://old.moz.org/");
  Assert.greater(old, low);
  let base = await getFrecency("https://base.moz.org/");
  Assert.greater(base, old);
  let unvisitedBm = await getFrecency("https://unvisitedbookmark.moz.org/");
  Assert.greater(unvisitedBm, base);
  let manyVisits = await getFrecency("https://manyvisits.moz.org/");
  Assert.greater(manyVisits, unvisitedBm);
  let visitedBm = await getFrecency("https://visitedbookmark.moz.org/");
  Assert.greater(visitedBm, base);
});
