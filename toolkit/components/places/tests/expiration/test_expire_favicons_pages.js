/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// Timed expiration removes the icon pages of the pages it expires, while the
// full orphan sweep only runs on idle and debug steps.

const DATA_URL =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAA" +
  "AAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg==";

async function iconPageExists(url) {
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(
    `SELECT count(*) FROM moz_pages_w_icons WHERE page_url_hash = hash(:url)`,
    { url }
  );
  return rows[0].getResultByIndex(0) > 0;
}

function promiseTimedExpirationStep() {
  let promise = promiseTopicObserved(PlacesUtils.TOPIC_EXPIRATION_FINISHED);
  Cc["@mozilla.org/places/expiration;1"]
    .getService(Ci.nsITimerCallback)
    .notify(null);
  return promise;
}

add_setup(async function () {
  // Set interval to a large value so we don't expire on it.
  setInterval(3600);
});

add_task(async function test_expire_favicons_pages() {
  const expiredUrl = "https://expired.example.com/";
  let bm = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: expiredUrl,
  });
  await PlacesTestUtils.setFaviconForPage(
    expiredUrl,
    "https://expired.example.com/icon.png",
    DATA_URL
  );
  await PlacesUtils.bookmarks.remove(bm);

  const orphanUrl = "https://orphan.example.com/";
  await PlacesUtils.withConnectionWrapper("addOrphanPage", async db => {
    await db.execute(
      `INSERT INTO moz_pages_w_icons (page_url, page_url_hash)
       VALUES (:url, hash(:url))`,
      { url: orphanUrl }
    );
  });
  Assert.ok(await iconPageExists(expiredUrl), "Sanity check the icon page");

  await promiseTimedExpirationStep();
  Assert.ok(!(await page_in_database(expiredUrl)), "The page was expired");
  Assert.ok(
    !(await iconPageExists(expiredUrl)),
    "The icon page of the expired page was removed"
  );
  Assert.ok(
    await iconPageExists(orphanUrl),
    "Timed expiration doesn't sweep all the icon pages"
  );

  await promiseForceExpirationStep(0);
  Assert.ok(
    !(await iconPageExists(orphanUrl)),
    "The orphan icon page was removed"
  );
});
