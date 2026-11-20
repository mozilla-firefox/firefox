/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that storing an interaction again updates it in place, and only marks
 * its page for frecency recalculation when the interaction becomes interesting.
 */

ChromeUtils.defineESModuleGetters(this, {
  Interactions: "moz-src:///browser/components/places/Interactions.sys.mjs",
});

const VIEWTIME_THRESHOLD_MS =
  Services.prefs.getIntPref(
    "places.frecency.pages.interactions.viewTimeSeconds"
  ) * 1000;

const URL = "https://interactions.example.com/";
const CREATED_AT = Date.now();

async function storeInteraction(totalViewTime) {
  Interactions.store.add({
    url: URL,
    referrer: "",
    created_at: CREATED_AT,
    updated_at: Date.now(),
    totalViewTime,
    typingTime: 0,
    keypresses: 0,
    scrollingTime: 0,
    scrollingDistance: 0,
  });
  await Interactions.store.flush();

  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(
    `SELECT m.id, m.total_view_time, h.recalc_frecency
     FROM moz_places_metadata m
     JOIN moz_places h ON h.id = m.place_id
     WHERE h.url_hash = hash(:url) AND h.url = :url`,
    { url: URL }
  );
  Assert.equal(rows.length, 1, "There should be a single interaction");
  return {
    id: rows[0].getResultByName("id"),
    totalViewTime: rows[0].getResultByName("total_view_time"),
    recalc: rows[0].getResultByName("recalc_frecency"),
  };
}

async function resetRecalc() {
  await PlacesUtils.withConnectionWrapper("resetRecalc", db =>
    db.execute(
      `UPDATE moz_places SET recalc_frecency = 0
       WHERE url_hash = hash(:url) AND url = :url`,
      { url: URL }
    )
  );
}

add_task(async function test_update_in_place() {
  await PlacesUtils.history.clear();
  await PlacesTestUtils.addVisits(URL);

  let first = await storeInteraction(1000);
  Assert.equal(first.recalc, 0, "Not interesting yet, no recalculation");

  let second = await storeInteraction(VIEWTIME_THRESHOLD_MS);
  Assert.equal(second.id, first.id, "The interaction was updated in place");
  Assert.equal(second.totalViewTime, VIEWTIME_THRESHOLD_MS);
  Assert.equal(second.recalc, 1, "Becoming interesting asks a recalculation");

  await resetRecalc();
  let third = await storeInteraction(VIEWTIME_THRESHOLD_MS * 2);
  Assert.equal(third.id, first.id, "The interaction was updated in place");
  Assert.equal(third.totalViewTime, VIEWTIME_THRESHOLD_MS * 2);
  Assert.equal(third.recalc, 0, "Staying interesting asks no recalculation");

  await PlacesUtils.history.clear();
});
