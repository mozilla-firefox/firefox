/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * What this is aimed to test:
 *
 * Expiration can be manually triggered through a debug topic, but that should
 * only expire orphan entries, unless -1 is passed as limit.
 */

const EXPIRE_DAYS = 90;
var gExpirableTime = getExpirablePRTime(EXPIRE_DAYS);
var gNonExpirableTime = getExpirablePRTime(EXPIRE_DAYS - 2);

add_task(async function test_expire_orphans() {
  // Add visits to 2 pages and force a orphan expiration. Visits should survive.
  await PlacesTestUtils.addVisits({
    uri: uri("http://page1.mozilla.org/"),
    visitDate: gExpirableTime++,
  });
  await PlacesTestUtils.addVisits({
    uri: uri("http://page2.mozilla.org/"),
    visitDate: gExpirableTime++,
  });
  // Create a orphan place.
  let bm = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "http://page3.mozilla.org/",
    title: "",
  });
  await PlacesUtils.bookmarks.remove(bm);

  // Expire now.
  await promiseForceExpirationStep(0);

  // Check that visits survived.
  Assert.equal(visits_in_database("http://page1.mozilla.org/"), 1);
  Assert.equal(visits_in_database("http://page2.mozilla.org/"), 1);
  Assert.ok(!page_in_database("http://page3.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_orphans_optionalarg() {
  // Add visits to 2 pages and force a orphan expiration. Visits should survive.
  await PlacesTestUtils.addVisits({
    uri: uri("http://page1.mozilla.org/"),
    visitDate: gExpirableTime++,
  });
  await PlacesTestUtils.addVisits({
    uri: uri("http://page2.mozilla.org/"),
    visitDate: gExpirableTime++,
  });
  // Create a orphan place.
  let bm = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "http://page3.mozilla.org/",
    title: "",
  });
  await PlacesUtils.bookmarks.remove(bm);

  // Expire now.
  await promiseForceExpirationStep();

  // Check that visits survived.
  Assert.equal(visits_in_database("http://page1.mozilla.org/"), 1);
  Assert.equal(visits_in_database("http://page2.mozilla.org/"), 1);
  Assert.ok(!page_in_database("http://page3.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_limited() {
  await PlacesTestUtils.addVisits([
    {
      // Should be expired cause it's the oldest visit
      uri: "http://old.mozilla.org/",
      visitDate: gExpirableTime++,
    },
    {
      // Should not be expired cause we limit 1
      uri: "http://new.mozilla.org/",
      visitDate: gExpirableTime++,
    },
  ]);

  // Expire now.
  await promiseForceExpirationStep(1);

  // Check that newer visit survived.
  Assert.equal(visits_in_database("http://new.mozilla.org/"), 1);
  // Other visits should have been expired.
  Assert.ok(!page_in_database("http://old.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_visitcount_longurl() {
  let longurl = "http://long.mozilla.org/" + "a".repeat(232);
  let longurl2 = "http://long2.mozilla.org/" + "a".repeat(232);
  await PlacesTestUtils.addVisits([
    {
      // Should be expired cause it's the oldest visit
      uri: "http://old.mozilla.org/",
      visitDate: gExpirableTime++,
    },
    {
      // Should not be expired cause it has 2 visits.
      uri: longurl,
      visitDate: gExpirableTime++,
    },
    {
      uri: longurl,
      visitDate: gNonExpirableTime,
    },
    {
      // Should be expired cause it has 1 old visit.
      uri: longurl2,
      visitDate: gExpirableTime++,
    },
  ]);

  await promiseForceExpirationStep(1);

  // Check that some visits survived.
  Assert.equal(visits_in_database(longurl), 2);
  // Check visit has been removed.
  Assert.equal(visits_in_database(longurl2), 0);

  // Other visits should have been expired.
  Assert.ok(!page_in_database("http://old.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_limited_exoticurl() {
  await PlacesTestUtils.addVisits([
    {
      // Should be expired cause it's the oldest visit
      uri: "http://old.mozilla.org/",
      visitDate: gExpirableTime++,
    },
    {
      // Should not be expired cause younger than EXPIRE_DAYS.
      uri: "http://nonexpirable-download.mozilla.org",
      visitDate: gNonExpirableTime,
      transition: PlacesUtils.history.TRANSITIONS.DOWNLOAD,
    },
    {
      // Should be expired cause it's a long url older than EXPIRE_DAYS.
      uri: "http://download.mozilla.org",
      visitDate: gExpirableTime++,
      transition: 7,
    },
  ]);

  await promiseForceExpirationStep(1);

  // Check that some visits survived.
  Assert.equal(
    visits_in_database("http://nonexpirable-download.mozilla.org/"),
    1
  );
  // The visits are gone, the url is not yet, cause we limited the expiration
  // to one entry, and we already removed http://old.mozilla.org/.
  // The page normally would be expired by the next expiration run.
  Assert.equal(visits_in_database("http://download.mozilla.org/"), 0);
  // Other visits should have been expired.
  Assert.ok(!page_in_database("http://old.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_exotic_hidden() {
  let visits = [
    {
      // Should be expired cause it's the oldest visit
      uri: "http://old.mozilla.org/",
      visitDate: gExpirableTime++,
      expectedCount: 0,
    },
    {
      // Expirable typed hidden url.
      uri: "https://typedhidden.mozilla.org/",
      visitDate: gExpirableTime++,
      transition: PlacesUtils.history.TRANSITIONS.FRAMED_LINK,
      expectedCount: 2,
    },
    {
      // Mark as typed.
      uri: "https://typedhidden.mozilla.org/",
      visitDate: gExpirableTime++,
      transition: PlacesUtils.history.TRANSITIONS.TYPED,
      expectedCount: 2,
    },
    {
      // Expirable non-typed hidden url.
      uri: "https://hidden.mozilla.org/",
      visitDate: gExpirableTime++,
      transition: PlacesUtils.history.TRANSITIONS.FRAMED_LINK,
      expectedCount: 0,
    },
  ];
  await PlacesTestUtils.addVisits(visits);
  for (let visit of visits) {
    Assert.greater(visits_in_database(visit.uri), 0);
  }

  await promiseForceExpirationStep(1);

  for (let visit of visits) {
    Assert.equal(
      visits_in_database(visit.uri),
      visit.expectedCount,
      `${visit.uri} should${
        visit.expectedCount == 0 ? " " : " not "
      }have been expired`
    );
  }
  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_unlimited() {
  let longurl = "http://long.mozilla.org/" + "a".repeat(232);
  await PlacesTestUtils.addVisits([
    {
      uri: "http://old.mozilla.org/",
      visitDate: gExpirableTime++,
    },
    {
      uri: "http://new.mozilla.org/",
      visitDate: gExpirableTime++,
    },
    // Add expirable visits.
    {
      uri: "http://download.mozilla.org/",
      visitDate: gExpirableTime++,
      transition: PlacesUtils.history.TRANSITION_DOWNLOAD,
    },
    {
      uri: longurl,
      visitDate: gExpirableTime++,
    },

    // Add non-expirable visits
    {
      uri: "http://nonexpirable.mozilla.org/",
      visitDate: getExpirablePRTime(5),
    },
    {
      uri: "http://nonexpirable-download.mozilla.org/",
      visitDate: getExpirablePRTime(5),
      transition: PlacesUtils.history.TRANSITION_DOWNLOAD,
    },
    {
      uri: longurl,
      visitDate: getExpirablePRTime(5),
    },
  ]);

  await promiseForceExpirationStep(-1);

  // Check that some visits survived.
  Assert.equal(visits_in_database("http://nonexpirable.mozilla.org/"), 1);
  Assert.equal(
    visits_in_database("http://nonexpirable-download.mozilla.org/"),
    1
  );
  Assert.equal(visits_in_database(longurl), 1);
  // Other visits should have been expired.
  Assert.ok(!page_in_database("http://old.mozilla.org/"));
  Assert.ok(!page_in_database("http://download.mozilla.org/"));
  Assert.ok(!page_in_database("http://new.mozilla.org/"));

  // Clean up.
  await PlacesUtils.history.clear();
});

add_task(async function test_expire_icons() {
  const dataUrl =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAA" +
    "AAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg==";

  const entries = [
    {
      desc: "Not expired because recent",
      page: "https://recent.notexpired.org/",
      icon: "https://recent.notexpired.org/test_icon.png",
      root: "https://recent.notexpired.org/favicon.ico",
      iconExpired: false,
      removed: false,
    },
    {
      desc: "Not expired because recent, no root",
      page: "https://recentnoroot.notexpired.org/",
      icon: "https://recentnoroot.notexpired.org/test_icon.png",
      iconExpired: false,
      removed: false,
    },
    {
      desc: "Expired because old with root",
      page: "https://oldroot.expired.org/",
      icon: "https://oldroot.expired.org/test_icon.png",
      root: "https://oldroot.expired.org/favicon.ico",
      iconExpired: true,
      removed: true,
    },
    {
      desc: "Not expired because bookmarked, even if old with root",
      page: "https://oldrootbm.notexpired.org/",
      icon: "https://oldrootbm.notexpired.org/test_icon.png",
      root: "https://oldrootbm.notexpired.org/favicon.ico",
      bookmarked: true,
      iconExpired: true,
      removed: false,
    },
    {
      desc: "Not Expired because old but has no root",
      page: "https://old.notexpired.org/",
      icon: "https://old.notexpired.org/test_icon.png",
      iconExpired: true,
      removed: false,
    },
    {
      desc: "Expired because it's an orphan page",
      page: "http://root.ref.org/#test",
      icon: undefined,
      iconExpired: false,
      removed: true,
    },
    {
      desc: "Expired because it's an orphan page",
      page: "http://root.ref.org/#test",
      icon: undefined,
      skipHistory: true,
      iconExpired: false,
      removed: true,
    },
  ];

  for (let entry of entries) {
    if (!entry.skipHistory) {
      await PlacesTestUtils.addVisits(entry.page);
    }
    if (entry.bookmarked) {
      await PlacesUtils.bookmarks.insert({
        url: entry.page,
        parentGuid: PlacesUtils.bookmarks.unfiledGuid,
      });
    }

    if (entry.icon) {
      await PlacesTestUtils.setFaviconForPage(entry.page, entry.icon, dataUrl);
      let favicon = await PlacesTestUtils.getFaviconForPage(entry.page);
      Assert.equal(
        favicon.uri.spec,
        entry.icon,
        "Sanity check the icon exists"
      );
    } else {
      // This is an orphan page entry.
      await PlacesUtils.withConnectionWrapper("addOrphanPage", async db => {
        await db.execute(
          `INSERT INTO moz_pages_w_icons (page_url, page_url_hash)
           VALUES (:url, hash(:url))`,
          { url: entry.page }
        );
      });
    }

    if (entry.root) {
      await PlacesTestUtils.setFaviconForPage(entry.page, entry.root, dataUrl);
    }

    if (entry.iconExpired) {
      // Set an expired time on the icon.
      await PlacesUtils.withConnectionWrapper("expireFavicon", async db => {
        await db.execute(
          `UPDATE moz_icons_to_pages SET expire_ms = 1
           WHERE icon_id = (SELECT id FROM moz_icons WHERE icon_url = :url)`,
          { url: entry.icon }
        );
        if (entry.root) {
          await db.execute(
            `UPDATE moz_icons SET expire_ms = 1 WHERE icon_url = :url`,
            { url: entry.root }
          );
        }
      });
    }
    if (entry.icon) {
      let favicon = await PlacesTestUtils.getFaviconForPage(entry.page);
      Assert.equal(
        favicon.uri.spec,
        entry.icon,
        "Sanity check the initial icon value"
      );
    }
  }

  info("Run expiration");
  await promiseForceExpirationStep(-1);

  info("Check expiration");
  for (let entry of entries) {
    Assert.ok(page_in_database(entry.page));

    let favicon = await PlacesTestUtils.getFaviconForPage(entry.page);
    if (!entry.removed) {
      Assert.equal(favicon.uri.spec, entry.icon, entry.desc);
      continue;
    }

    if (entry.root) {
      Assert.equal(favicon.uri.spec, entry.root, entry.desc);
      continue;
    }

    if (entry.icon) {
      await Assert.equal(favicon, null, entry.desc);
      continue;
    }

    // This was an orphan page entry.
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.execute(
      `SELECT count(*) FROM moz_pages_w_icons WHERE page_url_hash = hash(:url)`,
      { url: entry.page }
    );
    Assert.equal(rows[0].getResultByIndex(0), 0, "Orphan page was removed");
  }

  // Clean up.
  await PlacesUtils.history.clear();
});

add_setup(async function () {
  // Set interval to a large value so we don't expire on it.
  setInterval(3600); // 1h
  // Set maxPages to a low value, so it's easy to go over it.
  setMaxPages(1);
});
