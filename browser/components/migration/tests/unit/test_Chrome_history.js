/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChromeMigrationUtils } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/ChromeMigrationUtils.sys.mjs"
);

const DAY_MS = 24 * 60 * 60 * 1000;
const NOW = Date.now();
const PAGE_TRANSITION_CHAIN_START = 0x10000000;
const PAGE_TRANSITION_CHAIN_END = 0x20000000;

function chromeTimeDaysAgo(daysAgo) {
  return ChromeMigrationUtils.dateToChromeTime(NOW - daysAgo * DAY_MS);
}

let gSourceProfilePath;

const PROFILE = {
  id: "Default",
  name: "Person 1",
};

/**
 * TEST_URLS reflects the data stored in the profile's 'HistoryMaster' file.
 * The main object reflects the data in the 'urls' table. The visits property
 * reflects the associated data in the 'visits' table.
 */
const TEST_URLS = [
  {
    id: 1,
    url: "http://example.com/",
    title: "test",
    visit_count: 1,
    typed_count: 0,
    last_visit_time: 13193151310368000,
    hidden: 0,
    visits: [
      {
        id: 1,
        url: 1,
        visit_time: 13193151310368000,
        from_visit: 0,
        transition: 805306370, // Chrome PAGE_TRANSITION_AUTO_BOOKMARK = 2
        segment_id: 0,
        visit_duration: 10745006,
        incremented_omnibox_typed_score: 0,
      },
    ],
    expectedTransition: PlacesUtils.history.TRANSITIONS.BOOKMARK,
  },
  {
    id: 2,
    url: "http://invalid.com/",
    title: "test2",
    visit_count: 1,
    typed_count: 0,
    last_visit_time: 13193154948901000,
    hidden: 0,
    visits: [
      {
        id: 2,
        url: 2,
        visit_time: 13193154948901000,
        from_visit: 0,
        transition: 805306376, // Chrome PAGE_TRANSITION_RELOAD = 8
        segment_id: 0,
        visit_duration: 6568270,
        incremented_omnibox_typed_score: 0,
      },
    ],
    expectedTransition: PlacesUtils.history.TRANSITIONS.RELOAD,
  },
];

async function setVisitTimes(time) {
  let dbConn = await Sqlite.openConnection({
    path: PathUtils.join(gSourceProfilePath, "History"),
  });

  await dbConn.execute(`UPDATE urls SET last_visit_time = :last_visit_time`, {
    last_visit_time: time,
  });
  await dbConn.execute(`UPDATE visits SET visit_time = :visit_time`, {
    visit_time: time,
  });

  await dbConn.close();
}

/**
 * Replaces the contents of the History database with the given pages.
 *
 * @param {object[]} pages
 *   Each page has a url, optional typedCount, hidden and lastVisitDaysAgo, and
 *   visits: an array of { daysAgo, transition } where transition is a Chrome
 *   core transition type.
 */
async function populateHistoryDB(pages) {
  let db = await Sqlite.openConnection({
    path: PathUtils.join(gSourceProfilePath, "History"),
  });

  try {
    await db.executeTransaction(async () => {
      await db.execute("DELETE FROM visits");
      await db.execute("DELETE FROM urls");
      let visitId = 1;
      for (let [index, page] of pages.entries()) {
        let urlId = index + 1;
        let lastVisitDaysAgo =
          page.lastVisitDaysAgo ?? Math.min(...page.visits.map(v => v.daysAgo));
        await db.execute(
          `INSERT INTO urls (id, url, title, visit_count, typed_count, last_visit_time, hidden)
           VALUES (:urlId, :url, :title, :visitCount, :typedCount, :lastVisitTime, :hidden)`,
          {
            urlId,
            url: page.url,
            title: page.url,
            visitCount: page.visits.length,
            typedCount: page.typedCount ?? 0,
            lastVisitTime: chromeTimeDaysAgo(lastVisitDaysAgo),
            hidden: page.hidden ?? 0,
          }
        );
        for (let { daysAgo, transition } of page.visits) {
          await db.execute(
            `INSERT INTO visits (id, url, visit_time, from_visit, transition, segment_id, visit_duration)
             VALUES (:visitId, :urlId, :visitTime, 0, :transition, 0, 0)`,
            {
              visitId: visitId++,
              urlId,
              visitTime: chromeTimeDaysAgo(daysAgo),
              transition:
                transition |
                PAGE_TRANSITION_CHAIN_START |
                PAGE_TRANSITION_CHAIN_END,
            }
          );
        }
      }
    });
  } finally {
    await db.close();
  }
}

function setExpectedVisitTimes(time) {
  for (let urlInfo of TEST_URLS) {
    urlInfo.last_visit_time = time;
    urlInfo.visits[0].visit_time = time;
  }
}

function assertEntryMatches(entry, urlInfo, dateWasInFuture = false) {
  info(`Checking url: ${urlInfo.url}`);
  Assert.ok(entry, `Should have stored an entry`);

  Assert.equal(entry.url, urlInfo.url, "Should have the correct URL");
  Assert.equal(entry.title, urlInfo.title, "Should have the correct title");
  Assert.equal(
    entry.visits.length,
    urlInfo.visits.length,
    "Should have the correct number of visits"
  );

  for (let index in urlInfo.visits) {
    Assert.equal(
      entry.visits[index].transition,
      urlInfo.expectedTransition,
      "Should have the expected transition"
    );

    if (dateWasInFuture) {
      Assert.lessOrEqual(
        entry.visits[index].date.getTime(),
        new Date().getTime(),
        "Should have moved the date to no later than the current date."
      );
    } else {
      Assert.equal(
        entry.visits[index].date.getTime(),
        ChromeMigrationUtils.chromeTimeToDate(
          urlInfo.visits[index].visit_time,
          new Date()
        ).getTime(),
        "Should have the correct date"
      );
    }
  }
}

async function setupHistoryFile() {
  await IOUtils.copy(
    PathUtils.join(gSourceProfilePath, "HistoryMaster"),
    PathUtils.join(gSourceProfilePath, "History")
  );
}

add_setup(async function setup() {
  let rootDir = do_get_tempdir();
  let pathId;
  let dataSubDirs;
  if (AppConstants.platform === "macosx") {
    pathId = "ULibDir";
    dataSubDirs = ["Application Support", "Google", "Chrome"];
  } else if (AppConstants.platform === "win") {
    pathId = "LocalAppData";
    dataSubDirs = ["Google", "Chrome", "User Data"];
  } else {
    pathId = "Home";
    dataSubDirs = [".config", "google-chrome"];
  }
  registerFakePath(pathId, rootDir);

  let dataPath = PathUtils.join(rootDir.path, ...dataSubDirs);
  gSourceProfilePath = PathUtils.join(dataPath, PROFILE.id);
  await IOUtils.makeDirectory(gSourceProfilePath, { createAncestors: true });
  await IOUtils.writeJSON(PathUtils.join(dataPath, "Local State"), []);
  await IOUtils.copy(
    do_get_file(
      "Library/Application Support/Google/Chrome/Default/HistoryMaster"
    ).path,
    PathUtils.join(gSourceProfilePath, "HistoryMaster")
  );

  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_import() {
  await setupHistoryFile();
  await PlacesUtils.history.clear();
  // Update to ~10 days ago since the date can't be too old or Places may expire it.
  const pastDate = new Date(new Date().getTime() - 1000 * 60 * 60 * 24 * 10);
  const pastChromeTime = ChromeMigrationUtils.dateToChromeTime(pastDate);
  await setVisitTimes(pastChromeTime);
  setExpectedVisitTimes(pastChromeTime);

  let migrator = await MigrationUtils.getMigrator("chrome");
  Assert.ok(
    await migrator.isSourceAvailable(),
    "Sanity check the source exists"
  );

  let availableQuantity = await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY,
    PROFILE
  );

  for (let urlInfo of TEST_URLS) {
    let entry = await PlacesUtils.history.fetch(urlInfo.url, {
      includeVisits: true,
    });
    assertEntryMatches(entry, urlInfo);
  }
  Assert.equal(
    availableQuantity,
    TEST_URLS.length,
    "Available history entries reported correctly."
  );
  Assert.equal(
    MigrationUtils.getImportedCount("history"),
    TEST_URLS.length,
    "Migrated history entries counted correctly."
  );
});

add_task(async function test_import_future_date() {
  await setupHistoryFile();
  await PlacesUtils.history.clear();
  const futureDate = new Date().getTime() + 6000 * 60 * 24;
  await setVisitTimes(ChromeMigrationUtils.dateToChromeTime(futureDate));

  let migrator = await MigrationUtils.getMigrator("chrome");
  Assert.ok(
    await migrator.isSourceAvailable(),
    "Sanity check the source exists"
  );

  await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY,
    PROFILE
  );

  for (let urlInfo of TEST_URLS) {
    let entry = await PlacesUtils.history.fetch(urlInfo.url, {
      includeVisits: true,
    });
    assertEntryMatches(entry, urlInfo, true);
  }
});

async function assertVisits(url, expectedVisits) {
  let entry = await PlacesUtils.history.fetch(url, { includeVisits: true });
  Assert.ok(entry, `${url} should be imported`);
  let actual = entry.visits
    .sort((a, b) => b.date - a.date)
    .map(v => ({ date: v.date.getTime(), transition: v.transition }));
  let expected = expectedVisits.map(({ daysAgo, transition }) => ({
    date: ChromeMigrationUtils.chromeTimeToDate(
      chromeTimeDaysAgo(daysAgo),
      new Date()
    ).getTime(),
    transition,
  }));
  Assert.deepEqual(actual, expected, `Visits for ${url}`);
}

add_task(async function test_import_visits() {
  await setupHistoryFile();
  await PlacesUtils.history.clear();
  await populateHistoryDB([
    {
      url: "https://many-visits.example/",
      visits: [1, 2, 3, 4, 5, 6, 7, 8].map(daysAgo => ({
        daysAgo,
        transition: 0, // LINK
      })),
    },
    {
      url: "https://transitions_1_to_5.example/",
      visits: [1, 2, 3, 4, 5].map(num => ({
        daysAgo: num,
        transition: num,
      })),
    },
    {
      url: "https://transitions_6_to_10.example/",
      visits: [6, 7, 8, 9, 10].map(num => ({
        daysAgo: num,
        transition: num,
      })),
    },
    {
      url: "https://subframe-and-reload.example/",
      visits: [
        { daysAgo: 1, transition: 0 }, // LINK
        { daysAgo: 2, transition: 3 }, // AUTO_SUBFRAME
        { daysAgo: 3, transition: 4 }, // MANUAL_SUBFRAME
        { daysAgo: 4, transition: 8 }, // RELOAD
      ],
    },
    {
      url: "https://only-reload.example/",
      typedCount: 1,
      visits: [{ daysAgo: 1, transition: 8 }], // RELOAD
    },
    {
      url: "https://reloads-first.example/",
      visits: [
        ...[1, 2, 3, 4, 5, 6].map(daysAgo => ({ daysAgo, transition: 8 })), // RELOAD
        { daysAgo: 7, transition: 0 }, // LINK
        { daysAgo: 8, transition: 0 }, // LINK
      ],
    },
    {
      url: "https://links-fill-limit.example/",
      visits: [
        { daysAgo: 1, transition: 8 }, // RELOAD
        ...[2, 3, 4, 5, 6, 7].map(daysAgo => ({ daysAgo, transition: 0 })), // LINK
      ],
    },
    {
      url: "https://old-visit.example/",
      visits: [
        { daysAgo: 10, transition: 0 }, // LINK
        { daysAgo: 200, transition: 0 }, // LINK
      ],
    },
    {
      url: "https://too-old.example/",
      visits: [{ daysAgo: 200, transition: 0 }], // LINK
    },
    {
      url: "https://hidden.example/",
      hidden: 1,
      visits: [{ daysAgo: 1, transition: 0 }], // LINK
    },
  ]);

  let migrator = await MigrationUtils.getMigrator("chrome");
  await promiseMigration(
    migrator,
    MigrationUtils.resourceTypes.HISTORY,
    PROFILE
  );

  const { LINK, TYPED, BOOKMARK, FRAMED_LINK, RELOAD } =
    PlacesUtils.history.TRANSITIONS;
  // The visit limit defaults to 5
  await assertVisits("https://many-visits.example/", [
    { transition: LINK, daysAgo: 1 },
    { transition: LINK, daysAgo: 2 },
    { transition: LINK, daysAgo: 3 },
    { transition: LINK, daysAgo: 4 },
    { transition: LINK, daysAgo: 5 },
  ]);
  await assertVisits("https://transitions_1_to_5.example/", [
    { transition: TYPED, daysAgo: 1 },
    { transition: BOOKMARK, daysAgo: 2 },
    // AUTO_SUBFRAME is skipped.
    { transition: FRAMED_LINK, daysAgo: 4 },
    { transition: TYPED, daysAgo: 5 },
  ]);
  await assertVisits("https://transitions_6_to_10.example/", [
    { transition: LINK, daysAgo: 6 },
    { transition: LINK, daysAgo: 7 },
    { transition: RELOAD, daysAgo: 8 },
    { transition: TYPED, daysAgo: 9 },
    { transition: TYPED, daysAgo: 10 },
  ]);
  await assertVisits("https://subframe-and-reload.example/", [
    { transition: LINK, daysAgo: 1 },
    { transition: FRAMED_LINK, daysAgo: 3 },
    { transition: RELOAD, daysAgo: 4 },
  ]);
  await assertVisits("https://only-reload.example/", [
    { transition: RELOAD, daysAgo: 1 },
  ]);
  // Links are ranked before reloads, so the newest reloads only fill the
  // slots left over.
  await assertVisits("https://reloads-first.example/", [
    { transition: RELOAD, daysAgo: 1 },
    { transition: RELOAD, daysAgo: 2 },
    { transition: RELOAD, daysAgo: 3 },
    { transition: LINK, daysAgo: 7 },
    { transition: LINK, daysAgo: 8 },
  ]);
  // With enough links to reach the limit, no reloads are kept.
  await assertVisits("https://links-fill-limit.example/", [
    { transition: LINK, daysAgo: 2 },
    { transition: LINK, daysAgo: 3 },
    { transition: LINK, daysAgo: 4 },
    { transition: LINK, daysAgo: 5 },
    { transition: LINK, daysAgo: 6 },
  ]);
  await assertVisits("https://old-visit.example/", [
    { transition: LINK, daysAgo: 10 },
  ]);
  Assert.equal(
    await PlacesUtils.history.fetch("https://too-old.example/"),
    null
  );
  Assert.equal(
    await PlacesUtils.history.fetch("https://hidden.example/"),
    null
  );
});

add_task(async function test_import_visits_limit() {
  await setupHistoryFile();
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.migrate.chrome.history.visit.limit");
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
        "browser.migrate.chrome.history.visit.limit"
      );
    } else {
      Services.prefs.setIntPref(
        "browser.migrate.chrome.history.visit.limit",
        visitLimit
      );
    }

    await PlacesUtils.history.clear();
    await populateHistoryDB([
      {
        url: "https://visits.example/",
        visits: Array.from({ length: 15 }, (_, index) => ({
          daysAgo: index + 1,
          transition: 0, // LINK
        })),
      },
    ]);

    let migrator = await MigrationUtils.getMigrator("chrome");
    await promiseMigration(
      migrator,
      MigrationUtils.resourceTypes.HISTORY,
      PROFILE
    );

    const { LINK } = PlacesUtils.history.TRANSITIONS;
    await assertVisits(
      "https://visits.example/",
      Array.from({ length: expected }, (_, i) => ({
        daysAgo: i + 1,
        transition: LINK,
      }))
    );
  }
});
