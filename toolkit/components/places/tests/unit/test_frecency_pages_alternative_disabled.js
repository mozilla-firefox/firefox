/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests alternative pages frecency when the preference is disabled on startup.

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
  registerCleanupFunction(PlacesUtils.history.clear);
});

add_task(async function test_normal_init() {
  await restartRecalculator();

  // Avoid hitting the cache, we want to check the actual database value.
  Assert.ok(
    ObjectUtils.isEmpty(
      await PlacesUtils.metadata.get(
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
        Object.create(null)
      )
    ),
    "Check there's no variables stored"
  );
});

add_task(async function test_disabled() {
  let pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 1),
    "All the entries should require recalculation"
  );

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 1),
    "All the entries should still require recalculation"
  );

  // Avoid hitting the cache, we want to check the actual database value.
  PlacesUtils.metadata.cache.clear();
  Assert.equal(
    (
      await PlacesUtils.metadata.get(
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
        Object.create(null)
      )
    ).version,
    undefined,
    "Check the algorithm version has not been stored"
  );
});

add_task(async function test_pref_change_after_pref_change() {
  info("Turn alternative frecency on after the pref was loaded.");
  Services.prefs.setBoolPref(
    "places.frecency.pages.alternative.featureGate",
    true
  );

  let pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 1),
    "All the entries should require recalculation"
  );

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  pages = await getAllPages();
  Assert.ok(
    pages.every(p => p.recalc_alt_frecency == 1),
    "All the entries should still require recalculation"
  );

  Assert.equal(
    (
      await PlacesUtils.metadata.get(
        PlacesFrecencyRecalculator.alternativeFrecencyInfo.pages.metadataKey,
        Object.create(null)
      )
    ).version,
    undefined,
    "Check the algorithm version has still not been stored"
  );
});
