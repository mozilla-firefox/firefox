/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AddonTestUtils: "resource://testing-common/AddonTestUtils.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

const DEFAULT_CHUNK_SIZE = 50;

// Each scenario is measured this many times, after a discarded warmup pass, so
// that a single outlier cannot dominate the reported average. The first
// execution of a scenario also pays statement preparation and cold cache costs
// that are not what this test is trying to measure.
const WARMUP_ITERATIONS = 1;
const MEASURED_ITERATIONS = 10;

const HOUR_MS = 1000 * 60 * 60;
const DAY_MS = HOUR_MS * 24;

// XXX: We can consolidate this once bug 1930955 lands
// eslint-disable-next-line no-unused-vars
const perfMetadata = {
  owner: "Places",
  name: "Calculate Frecency Speed",
  description: "Audits the speed of running calculate_frecency.",
  options: {
    default: {
      extra_args: ["headless"],
      manifest: "perftest.toml",
      manifest_flavor: "browser-chrome",
      perfherder: true,
      perfherder_metrics: [
        // Timing metrics.
        {
          name: "PlacesCalculateFrecencyHighFrequency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyLowFrequency",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyChunked",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyBookmarkOnly",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyRedirectChain",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyInteractions",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyManyVisits",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyVeryManyVisits",
          unit: "ms",
          shouldAlert: false,
        },
        {
          name: "PlacesCalculateFrecencyWorstCase",
          unit: "ms",
          shouldAlert: false,
        },
      ],
      try_platform: ["linux", "mac"],
      verbose: true,
    },
  },
};

function createTimer(metricName) {
  let accumulatedTime = 0;
  let iterations = 0;
  let now = 0;
  return {
    start() {
      now = ChromeUtils.now();
    },
    stop() {
      accumulatedTime += ChromeUtils.now() - now;
      iterations++;
    },
    reportMetrics() {
      const metrics = {};
      metrics[metricName] = accumulatedTime / iterations;
      info(`perfMetrics | ${JSON.stringify(metrics)}`);
    },
  };
}

/**
 * Builds a list of visits spread backwards from now, one per day.
 *
 * @param {number} now the reference time, in milliseconds, shared by every
 *   date in the task so visits and interactions stay aligned.
 * @param {number} count how many visits to generate.
 * @param {number} transition the TRANSITIONS value to use for each visit.
 * @returns {object[]} visit descriptors for the scenario's `urls` entries.
 */
function dailyVisits(
  now,
  count,
  transition = PlacesUtils.history.TRANSITIONS.LINK
) {
  return Array.from({ length: count }, (_, i) => ({
    date: new Date(now - (i + 1) * DAY_MS),
    transition,
  }));
}

/**
 * Builds interaction times for a page whose visits came from dailyVisits.
 *
 * @param {number} now the same reference time passed to dailyVisits.
 * @param {number} matched how many interactions should line up with a visit.
 * @param {number} unmatched how many interactions should have no visit nearby.
 * @returns {number[]} interaction times, in milliseconds.
 */
function dailyInteractions(now, matched, unmatched) {
  const daysAgo = days => now - days * DAY_MS;
  let interactions = [];
  // Same days as dailyVisits, so each one lands on a visit.
  for (let day = 1; day <= matched; day++) {
    interactions.push(daysAgo(day));
  }
  // Half a day between visits, far outside maxVisitGapSeconds.
  for (let day = 1; day <= unmatched; day++) {
    interactions.push(daysAgo(day + 0.5));
  }
  return interactions;
}

/**
 * Inserts an interaction for a page, so the interactions CTE and the boosted
 * visit weights are tested.
 *
 * @param {object} db the writable connection to insert through.
 * @param {number} placeId the moz_places id to attach the interaction to.
 * @param {number} createdAt interaction time, in milliseconds.
 */
async function insertInteraction(db, placeId, createdAt) {
  // Interactions are only sampled when they look "interesting", which means
  // either a long view time or a shorter one paired with many keypresses. Keep
  // these above the thresholds the prefs define, so the interaction rows
  // actually reach the frecency computation. The prefs are read here rather
  // than at the top level, where mozperftest evaluates the file without
  // Services to extract perfMetadata.
  const viewTime =
    Services.prefs.getIntPref(
      "places.frecency.pages.interactions.viewTimeSeconds"
    ) *
      1000 +
    1000;
  const keyPresses =
    Services.prefs.getIntPref(
      "places.frecency.pages.interactions.manyKeypresses"
    ) + 1;
  await db.execute(
    `INSERT INTO moz_places_metadata (
       place_id, created_at, updated_at, total_view_time, key_presses
     ) VALUES (:placeId, :createdAt, :createdAt, :viewTime, :keyPresses)`,
    {
      placeId,
      createdAt,
      viewTime,
      keyPresses,
    }
  );
}

add_task(async function measure_frecency_speed() {
  const now = Date.now();
  let testData = [
    // High frequency, recent visits.
    {
      timerName: "PlacesCalculateFrecencyHighFrequency",
      urls: [
        {
          url: "https://www.mozilla.org/",
          visits: [
            {
              // 1 hour ago
              date: new Date(now - HOUR_MS),
              transition: PlacesUtils.history.TRANSITIONS.TYPED,
            },
            {
              // 1 day ago
              date: new Date(now - DAY_MS),
              transition: PlacesUtils.history.TRANSITIONS.TYPED,
            },
            {
              // 7 days ago
              date: new Date(now - DAY_MS * 7),
              transition: PlacesUtils.history.TRANSITIONS.TYPED,
            },
            {
              // 14 days ago
              date: new Date(now - DAY_MS * 14),
              transition: PlacesUtils.history.TRANSITIONS.TYPED,
            },
            {
              // 30 days ago
              date: new Date(now - DAY_MS * 30),
              transition: PlacesUtils.history.TRANSITIONS.TYPED,
            },
          ],
        },
      ],
    },
    // Low frequency, old visit.
    {
      timerName: "PlacesCalculateFrecencyLowFrequency",
      urls: [
        {
          url: "https://old.mozilla.org/",
          visits: [
            {
              // 365 days ago
              date: new Date(now - DAY_MS * 365),
              transition: PlacesUtils.history.TRANSITIONS.LINK,
            },
          ],
        },
      ],
    },
    // Many different URLs.
    {
      timerName: "PlacesCalculateFrecencyChunked",
      urls: [
        ...Array.from({ length: DEFAULT_CHUNK_SIZE }, (_, i) => ({
          url: `https://www.example${i}.com/`,
          visits: [
            {
              date: new Date(now),
            },
          ],
        })),
      ],
    },
    // A bookmark with no visits at all. The bookmark's dateAdded becomes the
    // only sample, which is the one case where the visits CTE is empty.
    {
      timerName: "PlacesCalculateFrecencyBookmarkOnly",
      urls: [{ url: "https://bookmarked.mozilla.org/", visits: [] }],
      bookmarkedUrls: ["https://bookmarked.mozilla.org/"],
    },
    // A redirect chain, so both of the self joins on moz_historyvisits that
    // resolve redirect sources and targets have rows to match. Each day the
    // source is visited and redirects to the target a second later, so every
    // target visit has its own source visit as from_visit.
    {
      timerName: "PlacesCalculateFrecencyRedirectChain",
      urls: [
        {
          url: "https://redirect-source.mozilla.org/",
          visits: dailyVisits(now, 5, PlacesUtils.history.TRANSITIONS.LINK),
        },
        {
          url: "https://redirect-target.mozilla.org/",
          visits: dailyVisits(
            now,
            5,
            PlacesUtils.history.TRANSITIONS.REDIRECT_PERMANENT
          ).map(visit => ({
            ...visit,
            date: new Date(visit.date.getTime() + 1000),
            referrer: "https://redirect-source.mozilla.org/",
          })),
        },
      ],
    },
    // Visits paired with interactions, which adds the interactions CTE and the
    // virtual visits built from interactions that no visit matches.
    {
      timerName: "PlacesCalculateFrecencyInteractions",
      urls: [
        {
          url: "https://interactive.mozilla.org/",
          visits: dailyVisits(now, 10),
          interactions: dailyInteractions(now, 5, 5),
        },
      ],
    },
    // A page with many more visits than numSampledVisits, where the per visit
    // weighting joins rather than the per call setup dominate the cost.
    {
      timerName: "PlacesCalculateFrecencyManyVisits",
      urls: [
        {
          url: "https://frequent.mozilla.org/",
          visits: dailyVisits(now, 50),
        },
      ],
    },
    // The same shape an order of magnitude larger, approximating a page a
    // heavy user has accumulated over years.
    {
      timerName: "PlacesCalculateFrecencyVeryManyVisits",
      urls: [
        {
          url: "https://veryfrequent.mozilla.org/",
          visits: dailyVisits(now, 200),
        },
      ],
    },
    // Every CTE active at once: many visits, interactions and a bookmark.
    {
      timerName: "PlacesCalculateFrecencyWorstCase",
      urls: [
        {
          url: "https://worstcase.mozilla.org/",
          visits: dailyVisits(now, 50),
          interactions: dailyInteractions(now, 10, 10),
        },
      ],
      bookmarkedUrls: ["https://worstcase.mozilla.org/"],
    },
  ];

  for (let item of testData) {
    await PlacesUtils.bookmarks.eraseEverything();
    await PlacesUtils.history.clear();

    let db = await PlacesUtils.promiseDBConnection();
    let initialIds = await db.executeCached(`
      SELECT id FROM moz_places
      ORDER BY id
    `);
    Assert.equal(initialIds.length, 0, "Places should be empty.");

    let visits = item.urls
      .flatMap(urlData => urlData.visits.map(visit => ({ ...visit, urlData })))
      .sort((a, b) => a.date - b.date);
    for (let visit of visits) {
      await PlacesTestUtils.addVisits({
        uri: visit.urlData.url,
        visitDate: visit.date,
        transition: visit.transition,
        referrer: visit.referrer,
      });
    }

    for (let url of item.bookmarkedUrls || []) {
      await PlacesUtils.bookmarks.insert({
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
        url,
        title: `Bookmark for ${url}`,
      });
    }

    let placeRows = await db.executeCached(`
      SELECT id, url FROM moz_places
    `);
    // Places are created in visit order rather than in item.urls order, so
    // look them up by URL.
    let placeIdByUrl = new Map(
      placeRows.map(row => [
        row.getResultByName("url"),
        row.getResultByName("id"),
      ])
    );
    Assert.equal(
      placeRows.length,
      item.urls.length,
      "Places should match the number of URLs in the test."
    );

    let expectedVisits = item.urls.reduce(
      (sum, urlData) => sum + urlData.visits.length,
      0
    );
    let visitRows = await db.executeCached(
      `SELECT COUNT(*) AS count FROM moz_historyvisits`
    );
    Assert.equal(
      visitRows[0].getResultByName("count"),
      expectedVisits,
      "Visits should match the number requested by the test."
    );

    let expectedRedirects = visits.filter(visit => visit.referrer).length;
    let redirectRows = await db.executeCached(
      `SELECT COUNT(DISTINCT from_visit) AS count
       FROM moz_historyvisits
       WHERE from_visit > 0`
    );
    Assert.equal(
      redirectRows[0].getResultByName("count"),
      expectedRedirects,
      "Each visit with a referrer should come from a distinct referrer visit."
    );

    await PlacesUtils.withConnectionWrapper(
      "Insert interactions",
      async writableDb => {
        for (let urlData of item.urls) {
          let placeId = placeIdByUrl.get(urlData.url);
          for (let createdAt of urlData.interactions || []) {
            await insertInteraction(writableDb, placeId, createdAt);
          }
        }
      }
    );

    let expectedInteractions = item.urls.reduce(
      (sum, urlData) =>
        sum + ((urlData.interactions && urlData.interactions.length) || 0),
      0
    );
    let interactionRows = await db.executeCached(
      `SELECT COUNT(*) AS count FROM moz_places_metadata`
    );
    Assert.equal(
      interactionRows[0].getResultByName("count"),
      expectedInteractions,
      "Interactions should match the number requested by the test."
    );

    // Mirrors the virtual_visits CTE: interactions with no visit to the same
    // page within maxVisitGapSeconds.
    const maxVisitGapMs =
      Services.prefs.getIntPref(
        "places.frecency.pages.interactions.maxVisitGapSeconds"
      ) * 1000;
    let expectedVirtualVisits = 0;
    for (let urlData of item.urls) {
      for (let createdAt of urlData.interactions || []) {
        let hasNearbyVisit = urlData.visits.some(
          visit => Math.abs(visit.date - createdAt) <= maxVisitGapMs
        );
        if (!hasNearbyVisit) {
          expectedVirtualVisits++;
        }
      }
    }
    let virtualVisitRows = await db.executeCached(
      `SELECT COUNT(*) AS count
       FROM moz_places_metadata i
       WHERE NOT EXISTS (
         SELECT 1 FROM moz_historyvisits vs
         WHERE vs.place_id = i.place_id
           AND vs.visit_date BETWEEN
             i.created_at * 1000 - :maxVisitGapMs * 1000
             AND i.created_at * 1000 + :maxVisitGapMs * 1000
       )`,
      { maxVisitGapMs }
    );
    Assert.equal(
      virtualVisitRows[0].getResultByName("count"),
      expectedVirtualVisits,
      "Interactions without a nearby visit should match the test's intent."
    );

    let frecencyTimer = createTimer(item.timerName);
    await PlacesUtils.withConnectionWrapper("Calculate Frecency", async db => {
      for (let i = 0; i < WARMUP_ITERATIONS + MEASURED_ITERATIONS; i++) {
        let measured = i >= WARMUP_ITERATIONS;
        if (measured) {
          frecencyTimer.start();
        }
        await db.executeCached(
          `
          UPDATE moz_places
          SET frecency = CALCULATE_FRECENCY(id)
        `
        );
        if (measured) {
          frecencyTimer.stop();
        }
      }
    });

    frecencyTimer.reportMetrics();
  }
});
