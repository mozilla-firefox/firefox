/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for integrating interaction table data with interactions based
 * frecency. If the interaction data is considered "interesting", frecency
 * should experience a boost.
 *
 * Since we don't know the precise values of the score, the tests typically
 * have a pattern of caching a baseline without interactions and then checking
 * what happens when either interesting or un-interesting interactions are
 * inserted.
 */

"use strict";

// The Viewtime Threshold preference is stored in seconds. When recorded in
// places, the data is stored in milliseconds.
const VIEWTIME_THRESHOLD =
  Services.prefs.getIntPref(
    "places.frecency.pages.interactions.viewTimeSeconds"
  ) * 1000;
const MANY_KEYPRESSES_THRESHOLD = Services.prefs.getIntPref(
  "places.frecency.pages.interactions.manyKeypresses"
);

// The Viewtime Threshold for Keypresses preference is stored in seconds.
// When recorded in places, the data is stored in milliseconds.
const VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD =
  Services.prefs.getIntPref(
    "places.frecency.pages.interactions.viewTimeIfManyKeypressesSeconds"
  ) * 1000;

const SAMPLED_VISITS_THRESHOLD = Services.prefs.getIntPref(
  "places.frecency.pages.numSampledVisits"
);
const MAX_VISIT_GAP = Services.prefs.getIntPref(
  "places.frecency.pages.interactions.maxVisitGapSeconds"
);

async function insertIntoMozPlacesMetadata(
  place_id,
  {
    referrer_place_id = null,
    created_at = Date.now(),
    updated_at = Date.now(),
    total_view_time = 0,
    typing_time = 0,
    key_presses = 0,
    scrolling_time = 0,
    scrolling_distance = 0,
    document_type = 0,
    search_query_id = null,
  }
) {
  info("Inserting interaction into moz_places_metadata.");
  await PlacesUtils.withConnectionWrapper(
    "test_frecency_interactions::insertIntoMozPlacesMetadata",
    async db => {
      await db.execute(
        `
        INSERT INTO moz_places_metadata (
          place_id,
          referrer_place_id,
          created_at,
          updated_at,
          total_view_time,
          typing_time,
          key_presses,
          scrolling_time,
          scrolling_distance,
          document_type,
          search_query_id
        ) VALUES (
         :place_id,
         :referrer_place_id,
         :created_at,
         :updated_at,
         :total_view_time,
         :typing_time,
         :key_presses,
         :scrolling_time,
         :scrolling_distance,
         :document_type,
         :search_query_id
        )
      `,
        {
          place_id,
          referrer_place_id,
          created_at,
          updated_at,
          total_view_time,
          typing_time,
          key_presses,
          scrolling_time,
          scrolling_distance,
          document_type,
          search_query_id,
        }
      );
    }
  );
}

async function insertIntoMozPlaces({
  url,
  guid,
  url_hash,
  origin_id,
  frecency,
}) {
  await PlacesUtils.withConnectionWrapper(
    "test_frecency_interactions::insertIntoMozPlaces",
    async db => {
      await db.execute(
        `
        INSERT INTO moz_places (
          url,
          guid,
          url_hash,
          origin_id,
          frecency
        ) VALUES (
          :url,
          :guid,
          :url_hash,
          :origin_id,
          :frecency
        )
      `,
        {
          url,
          guid,
          url_hash,
          origin_id,
          frecency,
        }
      );
    }
  );
}

async function getPageWithUrl(url) {
  info(`Find ${url} in moz_places.`);
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(`SELECT * FROM moz_places WHERE url = :url`, {
    url,
  });
  Assert.equal(rows.length, 1, "Found one matching row in moz_places.");
  return rows.map(r => ({
    id: r.getResultByName("id"),
    url: r.getResultByName("url"),
    title: r.getResultByName("title"),
    frecency: r.getResultByName("frecency"),
    recalc_frecency: r.getResultByName("recalc_frecency"),
  }))[0];
}

add_setup(async function () {
  registerCleanupFunction(PlacesUtils.history.clear);
});

/**
 * Each of the interactions occur at the same time as the visit so they
 * are paired as one sample.
 */
add_task(async function one_visit_one_matching_interaction() {
  const TESTS = [
    {
      title: "View time is under threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD - 1,
      },
      expectIncrease: false,
    },
    {
      title: "View time exceeds threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
      },
      expectIncrease: true,
    },
    {
      title: "View time and key presses under threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD - 1,
        key_presses: MANY_KEYPRESSES_THRESHOLD,
      },
      expectIncrease: false,
    },
    {
      title: "View time and key presses under threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD,
        key_presses: MANY_KEYPRESSES_THRESHOLD - 1,
      },
      expectIncrease: false,
    },
    {
      title: "View time and key presses exceed threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD,
        key_presses: MANY_KEYPRESSES_THRESHOLD,
      },
      expectIncrease: true,
    },
  ];
  for (let test of TESTS) {
    info(`Running test: ${test.title}`);
    let url = "https://testdomain1.moz.org/";
    await PlacesTestUtils.addVisits([url]);

    let page = await getPageWithUrl(url);
    let oldFrecency = page.frecency;
    Assert.notEqual(
      oldFrecency,
      0,
      "Frecency with a visit but no interaction should not be zero."
    );

    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    Assert.equal(
      page.frecency,
      oldFrecency,
      `Frecency of ${url} should not have changed.`
    );

    await insertIntoMozPlacesMetadata(page.id, test.interactionData);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    page = await getPageWithUrl(url);
    if (test.expectIncrease) {
      Assert.greater(
        page.frecency,
        oldFrecency,
        `Frecency of ${url} should have increased.`
      );
    } else {
      Assert.equal(
        page.frecency,
        oldFrecency,
        `Frecency of ${url} should not have changed.`
      );
    }
    await PlacesUtils.history.clear();
  }
});

/**
 * Each of the interactions are one day before the visit so the interaction
 * and visit are separate samples.
 */
add_task(async function one_visit_one_non_matching_interaction() {
  const today = new Date();
  const yesterday = new Date();
  yesterday.setDate(today.getDate() - 1);
  const TESTS = [
    {
      title: "View time is under threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD - 1,
        created_at: yesterday.getTime(),
      },
      expectIncrease: false,
    },
    {
      title: "View time exceeds threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: yesterday.getTime(),
      },
      expectIncrease: true,
    },
    {
      title: "View time and key presses under threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD - 1,
        key_presses: MANY_KEYPRESSES_THRESHOLD,
        created_at: yesterday.getTime(),
      },
      expectIncrease: false,
    },
    {
      title: "View time and key presses under threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD,
        key_presses: MANY_KEYPRESSES_THRESHOLD - 1,
        created_at: yesterday.getTime(),
      },
      expectIncrease: false,
    },
    {
      title: "View time and key presses exceed threshold",
      interactionData: {
        total_view_time: VIEWTIME_IF_MANY_KEYPRESSES_THRESHOLD,
        key_presses: MANY_KEYPRESSES_THRESHOLD,
        created_at: yesterday.getTime(),
      },
      expectIncrease: true,
    },
  ];
  for (let test of TESTS) {
    info(test.title);
    let url = "https://testdomain1.moz.org/";
    await PlacesTestUtils.addVisits([url]);

    let page = await getPageWithUrl(url);
    let oldFrecency = page.frecency;

    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    Assert.equal(
      page.frecency,
      oldFrecency,
      `Frecency of ${url} should not have changed.`
    );

    await insertIntoMozPlacesMetadata(page.id, test.interactionData);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    page = await getPageWithUrl(url);
    if (test.expectIncrease) {
      Assert.greater(
        page.frecency,
        oldFrecency,
        `Frecency of ${url} should have increased.`
      );
    } else {
      Assert.equal(
        page.frecency,
        oldFrecency,
        `Frecency of ${url} should not have changed.`
      );
    }
    await PlacesUtils.history.clear();
  }
});

add_task(async function zero_visits_one_interaction() {
  const TESTS = [
    {
      title: "View time is under threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD - 1,
      },
    },
    {
      title: "View time is at threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
      },
    },
  ];

  for (let test of TESTS) {
    info(test.title);

    let url = "https://testdomain1.moz.org/";
    await insertIntoMozPlaces({
      url,
      url_hash: "1234567890",
      frecency: 0,
    });

    let page = await getPageWithUrl(url);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    Assert.equal(page.frecency, 0, "Frecency is zero");

    await insertIntoMozPlacesMetadata(page.id, test.interactionData);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    page = await getPageWithUrl(url);
    Assert.equal(
      page.frecency,
      0,
      "Frecency remains 0 because frecency was at zero."
    );

    await PlacesUtils.history.clear();
  }
});

// When the sample threshold is reached, check the correct samples are
// used. This is done by inserting the same number of visits as the sample
// threshold and adding an interaction that doesn't belong to any of the visits
// before or after the visits.
add_task(async function max_samples_threshold() {
  const today = new Date();
  const yesterday = new Date();
  const twoDaysAgo = new Date();
  const longAgo = new Date();
  yesterday.setDate(today.getDate() - 1);
  twoDaysAgo.setDate(today.getDate() - 2);
  longAgo.setDate(today.getDate() - 400);
  const TESTS = [
    {
      title: "Interactions are beyond sample threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: twoDaysAgo.getTime(),
      },
      expectIncrease: false,
    },
    {
      title: "Interactions are within sample threshold",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: today.getTime(),
      },
      expectIncrease: true,
    },
    {
      title: "Interactions are at the visit gap from the oldest sampled visit",
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: yesterday.getTime() - MAX_VISIT_GAP * 1000,
      },
      expectIncrease: true,
    },
    {
      title: "Interactions are at sampled visits older than reloads",
      reloadDate: today,
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: yesterday.getTime(),
      },
      expectIncrease: true,
    },
    {
      title: "Interactions are at old sampled visits",
      visitDate: longAgo,
      interactionData: {
        total_view_time: VIEWTIME_THRESHOLD,
        created_at: longAgo.getTime(),
      },
      expectIncrease: true,
    },
  ];

  for (let test of TESTS) {
    info(test.title);
    let url = "https://testdomain1.moz.org/";
    for (let i = 0; i < SAMPLED_VISITS_THRESHOLD; ++i) {
      await PlacesTestUtils.addVisits([
        {
          url,
          visitDate: (test.visitDate ?? yesterday).getTime() * 1000,
        },
      ]);
      if (test.reloadDate) {
        // Reloads are not sampled.
        await PlacesTestUtils.addVisits([
          {
            url,
            visitDate: test.reloadDate.getTime() * 1000,
            transition: PlacesUtils.history.TRANSITION_RELOAD,
          },
        ]);
      }
    }

    let page = await getPageWithUrl(url);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    Assert.notEqual(page.frecency, 0, "Frecency is non-zero");

    let oldFrecency = page.frecency;
    await insertIntoMozPlacesMetadata(page.id, test.interactionData);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    page = await getPageWithUrl(url);
    if (test.expectIncrease) {
      Assert.notEqual(page.frecency, 0, "Frecency is non zero");
      Assert.greater(
        page.frecency,
        oldFrecency,
        "Frecency greater than the old value."
      );
    } else {
      Assert.equal(page.frecency, oldFrecency, "Frecency didn't change");
    }

    await PlacesUtils.history.clear();
  }
});

// Verify that the number of interactions contributing to frecency
// is as expected. Insert an old visit to ensure the URL has a positive
// frecency. Ensure all interactions are treated as virtual visits by making
// them interesting and more recent than the oldest visit.
// Each virtual visit should increase the frecency score until
// the threshold is exceeded. Interactions are deliberately inserted
// sequentially in the past, as the date can affect the score.
add_task(async function max_interesting_interactions() {
  const today = new Date();

  // We deliberately set a visit that can't be grouped with an interaction.
  const yearAgo = new Date();
  yearAgo.setMonth(yearAgo.getMonth() - 12);
  let url = "https://testdomain1.moz.org/";
  await PlacesTestUtils.addVisits({
    url,
    visitDate: yearAgo.getTime() * 1000,
    transition: PlacesUtils.history.TRANSITIONS.LINK,
  });

  let page = await getPageWithUrl(url);
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  Assert.greater(page.frecency, 0, "Frecency is above zero");

  info("Insert interesting interactions.");
  for (let i = 0; i < SAMPLED_VISITS_THRESHOLD; ++i) {
    let pageBefore = await getPageWithUrl(url);
    await insertIntoMozPlacesMetadata(page.id, {
      total_view_time: VIEWTIME_THRESHOLD,
      created_at: today.getTime() - i,
    });
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
    let pageAfter = await getPageWithUrl(url);
    Assert.greater(
      pageAfter.frecency,
      pageBefore.frecency,
      "Frecency has increased."
    );
  }

  info("Insert an interesting interaction beyond the threshold.");
  let pageBefore = await getPageWithUrl(url);
  await insertIntoMozPlacesMetadata(page.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    // Choose a time that would make this below all other existing interactions.
    created_at: today.getTime() - 100,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  let pageAfter = await getPageWithUrl(url);
  Assert.equal(
    pageBefore.frecency,
    pageAfter.frecency,
    "Frecency is the same."
  );

  await PlacesUtils.history.clear();
});

add_task(async function temp_redirect_frecency() {
  const yesterday = new Date();

  let url1 = "http://testdomain1.moz.org/";
  let url2 = "https://testdomain2.moz.org/";
  let url3 = "https://testdomain2.moz.org/dashboard";

  const visitDate = yesterday * 1000;
  await PlacesTestUtils.addVisits([
    {
      url: url1,
      visitDate,
      transition: PlacesUtils.history.TRANSITIONS.TYPED,
    },
    {
      url: url2,
      visitDate,
      transition: PlacesUtils.history.TRANSITIONS.REDIRECT_PERMANENT,
      referrer: Services.io.newURI(url1),
    },
    {
      url: url3,
      visitDate,
      transition: PlacesUtils.history.TRANSITIONS.REDIRECT_TEMPORARY,
      referrer: Services.io.newURI(url2),
    },
  ]);

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  let page1 = await getPageWithUrl(url1);
  Assert.notEqual(page1.frecency, 0, "Frecency is non-zero");

  let page2 = await getPageWithUrl(url2);
  Assert.notEqual(page2.frecency, 0, "Frecency is non-zero");

  let page3 = await getPageWithUrl(url3);
  Assert.notEqual(page3.frecency, 0, "Frecency is non-zero");

  info("For each visit, add an interesting interaction.");
  const created_at = yesterday.getTime();
  const updated_at = yesterday.getTime();
  await insertIntoMozPlacesMetadata(page1.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    created_at,
    updated_at,
  });
  await insertIntoMozPlacesMetadata(page2.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    created_at,
    updated_at,
  });
  await insertIntoMozPlacesMetadata(page3.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    created_at,
    updated_at,
  });

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  let newPage1 = await getPageWithUrl(url1);
  Assert.equal(newPage1.frecency, page1.frecency, "Frecency is the same");

  let newPage2 = await getPageWithUrl(url2);
  Assert.equal(newPage2.frecency, page2.frecency, "Frecency is the same");

  let newPage3 = await getPageWithUrl(url3);
  Assert.greater(newPage3.frecency, page3.frecency, "Frecency has increased");

  await PlacesUtils.history.clear();
});

add_task(async function interaction_visit_gap() {
  let url = "https://testdomain1.moz.org/";
  let now = new Date();
  let maxVisitGapMs = MAX_VISIT_GAP * 1000;

  // Insert visits that match the number of sample threshold to avoid
  // interesting interactions becoming virtual visits.
  for (let i = 0; i < SAMPLED_VISITS_THRESHOLD; ++i) {
    await PlacesTestUtils.addVisits([
      {
        url,
        visitDate: now,
      },
    ]);
  }
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  let page = await getPageWithUrl(url);
  Assert.notEqual(page.frecency, 0, "Frecency is not zero");

  info("Add an interaction just below the visit gap.");
  await insertIntoMozPlacesMetadata(page.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    created_at: now.getTime() - maxVisitGapMs - 1,
    updated_at: now.getTime() - maxVisitGapMs - 1,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  let updatedPage = await getPageWithUrl(url);
  Assert.equal(updatedPage.frecency, page.frecency, "Frecency didn't change.");

  info("Add an interaction at the visit gap.");
  await insertIntoMozPlacesMetadata(page.id, {
    total_view_time: VIEWTIME_THRESHOLD,
    created_at: now.getTime() - maxVisitGapMs,
    updated_at: now.getTime() - maxVisitGapMs,
  });
  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

  updatedPage = await getPageWithUrl(url);
  Assert.greater(updatedPage.frecency, page.frecency, "Frecency increased.");

  await PlacesUtils.history.clear();
});

add_task(async function old_visits_not_zero() {
  let testCases = [
    { daysOld: 365, description: "1 year old" },
    { daysOld: 1825, description: "5 years old" },
    { daysOld: 3650, description: "10 years old" },
  ];

  for (let testCase of testCases) {
    let oldDate = new Date();
    oldDate.setDate(oldDate.getDate() - testCase.daysOld);

    let url = `https://www.example-${testCase.daysOld}.com/`;
    await PlacesTestUtils.addVisits([
      {
        url,
        visitDate: oldDate.getTime() * 1000,
      },
    ]);
    await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();

    let page = await getPageWithUrl(url);
    Assert.notEqual(
      page.frecency,
      0,
      `Frecency should not be zero for ${testCase.description}`
    );

    Assert.greater(
      page.frecency,
      0,
      `Frecency should be positive for ${testCase.description}`
    );
  }

  await PlacesUtils.history.clear();
});

add_task(async function old_visits_new_interactions() {
  let url = "https://testdomain1.moz.org/";
  let oldDate = new Date();
  oldDate.setDate(oldDate.getDate() - 5000);

  await PlacesTestUtils.addVisits([
    { url, visitDate: oldDate.getTime() * 1000 },
  ]);

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  let page = await getPageWithUrl(url);
  let baselineFrecency = page.frecency;

  Assert.notEqual(baselineFrecency, 0, "Baseline frecency should not be zero.");
  Assert.greater(
    baselineFrecency,
    0,
    "Baseline frecency should be greater than 0."
  );

  info("Add a recent interesting interaction.");
  await insertIntoMozPlacesMetadata(page.id, {
    total_view_time: VIEWTIME_THRESHOLD * 2,
  });

  await PlacesFrecencyRecalculator.recalculateAnyOutdatedFrecencies();
  page = await getPageWithUrl(url);

  Assert.greater(
    page.frecency,
    baselineFrecency,
    "Recent interaction should boost frecency of page with really old visit."
  );

  await PlacesUtils.history.clear();
});
