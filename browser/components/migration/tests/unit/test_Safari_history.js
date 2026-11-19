/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { SafariProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/SafariProfileMigrator.sys.mjs"
);

const HISTORY_TEMPLATE_FILE_PATH = "Library/Safari/HistoryTemplate.db";

let gHistoryFilePath;

// We want this to be some recent time, so we'll always add some time to our
// dates to keep them ~ five days ago.
const MS_FROM_REFERENCE_TIME =
  new Date() - new Date("May 31, 2023 00:00:00 UTC");

const TEST_URLS = [
  {
    url: "http://example.com/",
    title: "Example Domain",
    time: 706743588.04751,
    jsTime: 1685050788047 + MS_FROM_REFERENCE_TIME,
    visits: 1,
  },
  {
    url: "https://www.mozilla.org/en-CA/",
    title: "Internet for people, not profit - Mozilla",
    time: 706743581.133679,
    jsTime: 1685050781133 + MS_FROM_REFERENCE_TIME,
    visits: 1,
  },
];

async function setupHistoryFile() {
  await IOUtils.copy(
    do_get_file(HISTORY_TEMPLATE_FILE_PATH).path,
    gHistoryFilePath
  );
  await updateVisitTimes();
}

async function updateVisitTimes() {
  let cocoaDifference = MS_FROM_REFERENCE_TIME / 1000;
  let dbConn = await Sqlite.openConnection({ path: gHistoryFilePath });
  await dbConn.execute(
    "UPDATE history_visits SET visit_time = visit_time + :difference;",
    { difference: cocoaDifference }
  );
  await dbConn.close();
}

add_setup(async function setup() {
  let libDir = await copyFixtureDir("Library/");
  registerFakePath("ULibDir", libDir);
  gHistoryFilePath = PathUtils.join(libDir.path, "Safari", "History.db");
  await setupHistoryFile();
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function testHistoryImport() {
  await PlacesUtils.history.clear();

  let migrator = await MigrationUtils.getMigrator("safari");
  let availableQuantity = await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY
  );
  Assert.equal(
    availableQuantity,
    3,
    "Available history pages should include the redirect source"
  );

  Assert.ok(
    !(await PlacesUtils.history.hasVisits("http://mozilla.org/")),
    "Redirect source should not be imported"
  );

  for (let urlInfo of TEST_URLS) {
    let entry = await PlacesUtils.history.fetch(urlInfo.url, {
      includeVisits: true,
    });

    Assert.equal(entry.url, urlInfo.url, "Should have the correct URL");
    Assert.equal(entry.title, urlInfo.title, "Should have the correct title");
    Assert.equal(
      entry.visits.length,
      urlInfo.visits,
      "Should have the correct number of visits"
    );
    Assert.equal(
      entry.visits[0].date.getTime(),
      urlInfo.jsTime,
      "Should have the correct date"
    );
    Assert.equal(
      MigrationUtils.getImportedHistoryAgeInDays(),
      6,
      "Imported history age is based on the oldest imported visit."
    );
  }
});

const DAY_MS = 24 * 60 * 60 * 1000;
const NOW = Date.now();
const NS_DATE_EPOCH_MS = new Date("2001-01-01T00:00:00-00:00").getTime();

function nsDateDaysAgo(daysAgo) {
  return (NOW - daysAgo * DAY_MS - NS_DATE_EPOCH_MS) / 1000;
}

/**
 * Replaces the contents of the History database with the given pages.
 *
 * @param {object[]} pages
 *   Each page has a url and visits: an array of
 *   { daysAgo, title, failed, redirectsTo, score }, where failed marks the
 *   load as unsuccessful, redirectsTo is the url of another page whose first
 *   visit this visit redirected to, and score defaults to 100.
 */
async function populateHistoryDB(pages) {
  let db = await Sqlite.openConnection({ path: gHistoryFilePath });
  try {
    await db.executeTransaction(async () => {
      await db.execute("DELETE FROM history_visits");
      await db.execute("DELETE FROM history_items");
      let visitId = 1;
      let firstVisitIds = new Map();
      let redirects = [];
      for (let [index, page] of pages.entries()) {
        let itemId = index + 1;
        await db.execute(
          `INSERT INTO history_items (id, url, visit_count, daily_visit_counts, should_recompute_derived_visit_counts, visit_count_score)
           VALUES (:itemId, :url, :visitCount, x'', 0, 100)`,
          { itemId, url: page.url, visitCount: page.visits.length }
        );
        firstVisitIds.set(page.url, visitId);
        for (let {
          daysAgo,
          title,
          failed,
          redirectsTo,
          score,
        } of page.visits) {
          await db.execute(
            `INSERT INTO history_visits (id, history_item, visit_time, title, load_successful, score)
             VALUES (:visitId, :itemId, :visitTime, :title, :loadSuccessful, :score)`,
            {
              visitId,
              itemId,
              visitTime: nsDateDaysAgo(daysAgo),
              title: title ?? null,
              loadSuccessful: failed ? 0 : 1,
              score: score ?? 100,
            }
          );
          if (redirectsTo) {
            redirects.push([visitId, redirectsTo]);
          }
          visitId++;
        }
      }
      for (let [sourceId, url] of redirects) {
        let destinationId = firstVisitIds.get(url);
        await db.execute(
          "UPDATE history_visits SET redirect_destination = :destinationId WHERE id = :sourceId",
          { sourceId, destinationId }
        );
        await db.execute(
          "UPDATE history_visits SET redirect_source = :sourceId WHERE id = :destinationId",
          { sourceId, destinationId }
        );
      }
    });
  } finally {
    await db.close();
  }
}

async function migrateHistory() {
  let migrator = await MigrationUtils.getMigrator("safari");
  return promiseMigration(migrator, MigrationUtils.resourceTypes.HISTORY);
}

/**
 * @param {string} url
 * @param {Array<number|{daysAgo: number, transition: number}>} expectedVisits
 *   Most recent first. A number is the daysAgo of a typed visit.
 */
async function assertVisits(url, expectedVisits) {
  let entry = await PlacesUtils.history.fetch(url, { includeVisits: true });
  Assert.ok(entry, `${url} should be imported`);
  let actual = entry.visits
    .sort((a, b) => b.date - a.date)
    .map(v => ({ date: v.date.getTime(), transition: v.transition }));
  let expected = expectedVisits.map(visit => {
    let { daysAgo, transition = PlacesUtils.history.TRANSITIONS.TYPED } =
      typeof visit == "number" ? { daysAgo: visit } : visit;
    return { date: NOW - daysAgo * DAY_MS, transition };
  });
  Assert.equal(actual.length, expected.length, `Visit count for ${url}`);
  for (let [i, visit] of actual.entries()) {
    Assert.less(
      Math.abs(visit.date - expected[i].date),
      1000,
      `Visit ${i} date for ${url}`
    );
    Assert.equal(
      visit.transition,
      expected[i].transition,
      `Visit ${i} transition for ${url}`
    );
  }
  return entry;
}

add_task(async function test_import_visits() {
  await PlacesUtils.history.clear();
  await populateHistoryDB([
    {
      url: "https://many-visits.example/",
      visits: [1, 2, 3, 4, 5, 6, 7, 8].map(daysAgo => ({ daysAgo })),
    },
    {
      url: "https://titles.example/",
      visits: [
        { daysAgo: 1, title: "" },
        { daysAgo: 2, title: "Newest title" },
        { daysAgo: 3, title: "Older title" },
      ],
    },
    {
      url: "https://old-visit.example/",
      visits: [{ daysAgo: 10 }, { daysAgo: 200 }],
    },
    {
      url: "https://too-old.example/",
      visits: [{ daysAgo: 200 }],
    },
    {
      url: "https://redirect-target.example/",
      visits: [{ daysAgo: 1 }],
    },
    {
      url: "http://redirect-target.example/",
      visits: [{ daysAgo: 1, redirectsTo: "https://redirect-target.example/" }],
    },
    {
      url: "https://some-redirects.example/",
      visits: [
        { daysAgo: 1, redirectsTo: "https://titles.example/" },
        { daysAgo: 2 },
      ],
    },
    {
      url: "https://failed.example/",
      visits: [{ daysAgo: 1, failed: true }],
    },
    {
      url: "https://score-zero.example/",
      visits: [
        { daysAgo: 1, score: 0 },
        { daysAgo: 2, score: 60 },
        { daysAgo: 3, score: 0 },
        { daysAgo: 4, score: 0 },
        { daysAgo: 5, score: 0 },
        { daysAgo: 6 },
        { daysAgo: 7, score: 0 },
      ],
    },
    {
      url: "https://score-zero-ranked-last.example/",
      visits: [
        { daysAgo: 1, score: 0 },
        ...[2, 3, 4, 5, 6, 7].map(daysAgo => ({ daysAgo, score: 25 })),
      ],
    },
    {
      url: "https://some-failed.example/",
      visits: [
        { daysAgo: 1, failed: true },
        ...[2, 3, 4, 5, 6, 7].map(daysAgo => ({ daysAgo })),
      ],
    },
  ]);

  let available = await migrateHistory();
  Assert.equal(
    available,
    11,
    "Available count should include pages that aren't imported"
  );

  // The visit limit defaults to 5.
  await assertVisits("https://many-visits.example/", [1, 2, 3, 4, 5]);
  let entry = await assertVisits("https://titles.example/", [1, 2, 3]);
  Assert.equal(entry.title, "Newest title", "Should use the newest title");
  await assertVisits("https://old-visit.example/", [10]);
  Assert.equal(
    await PlacesUtils.history.fetch("https://too-old.example/"),
    null,
    "Visits older than the max age shouldn't be imported"
  );
  await assertVisits("https://redirect-target.example/", [1]);
  Assert.equal(
    await PlacesUtils.history.fetch("http://redirect-target.example/"),
    null,
    "Pages that only redirected shouldn't be imported"
  );
  await assertVisits("https://some-redirects.example/", [2]);
  Assert.equal(
    await PlacesUtils.history.fetch("https://failed.example/"),
    null,
    "Pages that only failed to load shouldn't be imported"
  );
  // Visits with a score of 0 are imported as reloads, after other visits.
  const { RELOAD } = PlacesUtils.history.TRANSITIONS;
  await assertVisits("https://score-zero.example/", [
    { daysAgo: 1, transition: RELOAD },
    2,
    { daysAgo: 3, transition: RELOAD },
    { daysAgo: 4, transition: RELOAD },
    6,
  ]);
  await assertVisits(
    "https://score-zero-ranked-last.example/",
    [2, 3, 4, 5, 6]
  );
  // Failed loads don't take up slots in the visit limit.
  await assertVisits("https://some-failed.example/", [2, 3, 4, 5, 6]);
});

add_task(async function test_import_visits_limit() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.migrate.safari.history.visit.limit");
  });

  const visitLimits = [
    [-1, 1],
    [0, 1],
    [null, 5],
    [9, 9],
    [100000, 10], // places.frecency.pages.numSampledVisits default is 10
  ];
  for (let [visitLimit, expected] of visitLimits) {
    if (visitLimit == null) {
      Services.prefs.clearUserPref(
        "browser.migrate.safari.history.visit.limit"
      );
    } else {
      Services.prefs.setIntPref(
        "browser.migrate.safari.history.visit.limit",
        visitLimit
      );
    }

    await PlacesUtils.history.clear();
    await populateHistoryDB([
      {
        url: "https://visits.example/",
        visits: Array.from({ length: 15 }, (_, i) => ({ daysAgo: i + 1 })),
      },
    ]);
    await migrateHistory();

    await assertVisits(
      "https://visits.example/",
      Array.from({ length: expected }, (_, i) => i + 1)
    );
  }
});

add_task(async function test_import_history_limit() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.migrate.safari.history.limit");
  });

  // Pages are ranked by their most recent visit.
  let pages = [
    { url: "https://oldest.example/", visits: [{ daysAgo: 9 }] },
    {
      url: "https://newest.example/",
      visits: [{ daysAgo: 2 }, { daysAgo: 20 }],
    },
    { url: "https://middle.example/", visits: [{ daysAgo: 8 }] },
  ];

  for (let [historyLimit, expectedUrls] of [
    [
      0,
      [
        "https://newest.example/",
        "https://middle.example/",
        "https://oldest.example/",
      ],
    ],
    [2, ["https://newest.example/", "https://middle.example/"]],
  ]) {
    Services.prefs.setIntPref(
      "browser.migrate.safari.history.limit",
      historyLimit
    );
    await PlacesUtils.history.clear();
    await populateHistoryDB(pages);
    await migrateHistory();

    for (let page of pages) {
      let entry = await PlacesUtils.history.fetch(page.url);
      Assert.equal(
        !!entry,
        expectedUrls.includes(page.url),
        `${page.url} imported with history limit ${historyLimit}`
      );
    }
    await assertVisits("https://newest.example/", [2, 20]);
  }
});

add_task(async function test_migrate_reports_failure_on_exception() {
  await PlacesUtils.history.clear();
  await populateHistoryDB([
    { url: "https://example.com/", visits: [{ daysAgo: 1 }] },
  ]);

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());
  let insertStub = sandbox
    .stub(MigrationUtils, "insertVisitsWrapper")
    .rejects(new Error("Simulated failure"));

  let migrator = await MigrationUtils.getMigrator("safari");
  await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY,
    null,
    false
  );

  Assert.ok(insertStub.calledOnce, "insertVisitsWrapper should be called");
  sandbox.restore();
});

add_task(async function test_migrate_fails_when_no_url_parses() {
  await PlacesUtils.history.clear();
  await populateHistoryDB([
    { url: "not a url", visits: [{ daysAgo: 1 }] },
    { url: "also not a url", visits: [{ daysAgo: 2 }] },
  ]);

  let sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());
  let insertSpy = sandbox.spy(MigrationUtils, "insertVisitsWrapper");

  let migrator = await MigrationUtils.getMigrator("safari");
  await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY,
    null,
    false
  );

  Assert.ok(insertSpy.notCalled, "insertVisitsWrapper shouldn't be called");
  sandbox.restore();
});

add_task(async function test_history_not_offered_without_importable_visits() {
  await populateHistoryDB([
    { url: "https://too-old.example/", visits: [{ daysAgo: 200 }] },
    { url: "https://failed.example/", visits: [{ daysAgo: 1, failed: true }] },
    { url: "https://target.example/", visits: [{ daysAgo: 200 }] },
    {
      url: "http://target.example/",
      visits: [{ daysAgo: 1, redirectsTo: "https://target.example/" }],
    },
  ]);

  let migrator = new SafariProfileMigrator();
  let types = await migrator.getMigrateData();
  Assert.equal(
    types & MigrationUtils.resourceTypes.HISTORY,
    0,
    "History shouldn't be offered when no visits would be imported"
  );
});
