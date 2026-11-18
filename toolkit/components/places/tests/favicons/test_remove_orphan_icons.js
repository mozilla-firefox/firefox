/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that removing pages from history only removes the icons that were
 * related to them, or the root icons of origins that went away, and only once
 * they are no longer referenced.
 */

async function countTable(name) {
  await PlacesTestUtils.promiseAsyncUpdates();
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(`SELECT count(*) FROM ${name}`);
  return rows[0].getResultByIndex(0);
}

async function clearAll() {
  let promise = promiseTopicObserved(PlacesUtils.TOPIC_FAVICONS_EXPIRED);
  PlacesUtils.favicons.expireAllFavicons();
  await promise;
  await PlacesUtils.history.clear();
}

add_task(async function test_non_root_icon_removed_with_page() {
  await clearAll();

  let pageURI = Services.io.newURI("http://nonroot.places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let faviconURI = Services.io.newURI(
    "http://nonroot.places.test/page/icon.png"
  );
  await PlacesTestUtils.setFaviconForPage(
    pageURI,
    faviconURI,
    SMALLPNG_DATA_URI
  );

  Assert.equal(await countTable("moz_icons"), 1, "icon present");
  Assert.equal(await countTable("moz_icons_to_pages"), 1, "relation present");

  await PlacesUtils.history.remove(pageURI);

  Assert.equal(await countTable("moz_icons"), 0, "non-root icon was removed");
  Assert.equal(await countTable("moz_pages_w_icons"), 0, "page was removed");
});

add_task(async function test_root_icon_removed_with_origin() {
  await clearAll();

  let pageURI = Services.io.newURI("http://root.places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let rootIconURI = Services.io.newURI("http://root.places.test/favicon.ico");
  await PlacesTestUtils.setFaviconForPage(
    pageURI,
    rootIconURI,
    SMALLPNG_DATA_URI
  );

  let rows = await (
    await PlacesUtils.promiseDBConnection()
  ).execute("SELECT root FROM moz_icons");
  Assert.equal(rows.length, 1, "root icon present");
  Assert.equal(rows[0].getResultByName("root"), 1, "icon is a root icon");

  // Removing the only page for the host removes the origin, and its root icon.
  await PlacesUtils.history.remove(pageURI);
  Assert.equal(
    await countTable("moz_icons"),
    0,
    "root icon removed when origin is gone"
  );
});

add_task(async function test_root_icon_kept_when_other_pages_exist() {
  await clearAll();

  let page1 = Services.io.newURI("http://multi.places.test/one/");
  let page2 = Services.io.newURI("http://multi.places.test/two/");
  await PlacesTestUtils.addVisits([page1, page2]);
  let rootIconURI = Services.io.newURI("http://multi.places.test/favicon.ico");
  await PlacesTestUtils.setFaviconForPage(
    page1,
    rootIconURI,
    SMALLPNG_DATA_URI
  );

  Assert.equal(await countTable("moz_icons"), 1, "root icon present");

  // Removing only one page leaves the origin alive; the root icon must remain.
  await PlacesUtils.history.remove(page1);
  Assert.equal(
    await countTable("moz_icons"),
    1,
    "root icon kept while origin has other pages"
  );

  // Removing the last page should now drop the root icon.
  await PlacesUtils.history.remove(page2);
  Assert.equal(await countTable("moz_icons"), 0, "root icon removed");
});

add_task(async function test_shared_icon_kept_while_referenced() {
  await clearAll();

  // Two pages sharing the same non-root icon.
  let page1 = Services.io.newURI("http://shared.places.test/one/");
  let page2 = Services.io.newURI("http://shared.places.test/two/");
  await PlacesTestUtils.addVisits([page1, page2]);
  let sharedIcon = Services.io.newURI(
    "http://shared.places.test/icon-shared.png"
  );
  await PlacesTestUtils.setFaviconForPage(page1, sharedIcon, SMALLPNG_DATA_URI);
  await PlacesTestUtils.setFaviconForPage(page2, sharedIcon, SMALLPNG_DATA_URI);

  Assert.equal(await countTable("moz_icons"), 1, "1 shared icon");
  Assert.equal(await countTable("moz_icons_to_pages"), 2, "2 relations");

  await PlacesUtils.history.remove(page1);
  Assert.equal(
    await countTable("moz_icons"),
    1,
    "shared icon kept while still referenced"
  );
  Assert.equal(await countTable("moz_icons_to_pages"), 1, "1 relation left");

  await PlacesUtils.history.remove(page2);
  Assert.equal(await countTable("moz_icons"), 0, "shared icon removed");
});

add_task(async function test_root_icon_kept_when_www_origin_survives() {
  await clearAll();

  // The root icon covers both example.com and www.example.com, because
  // fixup_url normalizes both hosts to the same value.
  let pageNonWww = Services.io.newURI("http://example.places.test/one/");
  let pageWww = Services.io.newURI("http://www.example.places.test/two/");
  await PlacesTestUtils.addVisits([pageNonWww, pageWww]);

  let rootIconURI = Services.io.newURI(
    "http://example.places.test/favicon.ico"
  );
  await PlacesTestUtils.setFaviconForPage(
    pageNonWww,
    rootIconURI,
    SMALLPNG_DATA_URI
  );

  Assert.equal(await countTable("moz_icons"), 1, "root icon present");

  // The www origin still exists, so the root icon must be kept.
  await PlacesUtils.history.remove(pageNonWww);
  Assert.equal(
    await countTable("moz_icons"),
    1,
    "root icon kept while www origin survives"
  );

  await PlacesUtils.history.remove(pageWww);
  Assert.equal(await countTable("moz_icons"), 0, "root icon removed");
});

add_task(async function test_unrelated_orphans_left_to_expiration() {
  await clearAll();

  let removedPage = Services.io.newURI("http://removed.places.test/page/");
  let otherPage = Services.io.newURI("http://other.places.test/page/");
  await PlacesTestUtils.addVisits([removedPage, otherPage]);
  await PlacesTestUtils.setFaviconForPage(
    otherPage,
    Services.io.newURI("http://other.places.test/page/icon.png"),
    SMALLPNG_DATA_URI
  );
  // Make the other page's icon an orphan, bypassing history removal.
  await PlacesUtils.withConnectionWrapper("orphanIcon", async db => {
    await db.execute("DELETE FROM moz_icons_to_pages");
  });

  // Removing an unrelated page only checks its own icons, other orphans are
  // left to expiration.
  await PlacesUtils.history.remove(removedPage);
  Assert.equal(await countTable("moz_icons"), 1, "unrelated orphan kept");
});

async function expireAllRelations() {
  await PlacesUtils.withConnectionWrapper("expireRelations", async db => {
    await db.execute("UPDATE moz_icons_to_pages SET expire_ms = 0");
  });
}

async function iconUrls() {
  await PlacesTestUtils.promiseAsyncUpdates();
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute("SELECT icon_url FROM moz_icons ORDER BY 1");
  return rows.map(r => r.getResultByIndex(0));
}

add_task(async function test_replaced_icon_removed() {
  await clearAll();

  let pageURI = Services.io.newURI("http://replaced.places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let oldIcon = "http://replaced.places.test/old.png";
  let newIcon = "http://replaced.places.test/new.png";
  await PlacesTestUtils.setFaviconForPage(pageURI, oldIcon, SMALLPNG_DATA_URI);
  await expireAllRelations();

  // Setting a new icon expires the old relation, the old icon is now orphan.
  await PlacesTestUtils.setFaviconForPage(pageURI, newIcon, SMALLPNG_DATA_URI);
  Assert.deepEqual(await iconUrls(), [newIcon], "the old icon was removed");
});

add_task(async function test_refreshed_icon_kept() {
  await clearAll();

  let pageURI = Services.io.newURI("http://refreshed.places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let icon = "http://refreshed.places.test/icon.png";
  await PlacesTestUtils.setFaviconForPage(pageURI, icon, SMALLPNG_DATA_URI);
  await expireAllRelations();

  // Setting the same icon again replaces its expired relation.
  await PlacesTestUtils.setFaviconForPage(pageURI, icon, SMALLPNG_DATA_URI);
  Assert.deepEqual(await iconUrls(), [icon], "the icon was kept");
  Assert.equal(await countTable("moz_icons_to_pages"), 1, "relation present");
});

add_task(async function test_expireFaviconsForPage_removes_orphans() {
  await clearAll();

  let page1 = Services.io.newURI("http://expire.places.test/one/");
  let page2 = Services.io.newURI("http://expire.places.test/two/");
  await PlacesTestUtils.addVisits([page1, page2]);
  let ownIcon = "http://expire.places.test/own.png";
  let sharedIcon = "http://expire.places.test/shared.png";
  await PlacesTestUtils.setFaviconForPage(page1, ownIcon, SMALLPNG_DATA_URI);
  await PlacesTestUtils.setFaviconForPage(page1, sharedIcon, SMALLPNG_DATA_URI);
  await PlacesTestUtils.setFaviconForPage(page2, sharedIcon, SMALLPNG_DATA_URI);

  await PlacesUtils.favicons.expireFaviconsForPage(page1);
  Assert.deepEqual(
    await iconUrls(),
    [sharedIcon],
    "only the icon still used by another page was kept"
  );
});

add_task(async function test_root_icon_associated_to_another_host() {
  await clearAll();

  // A root icon for foo, that is also used by a page on bar, so it's both root
  // and associated.
  let fooPage = Services.io.newURI("http://foo.places.test/page/");
  let barPage = Services.io.newURI("http://bar.places.test/page/");
  await PlacesTestUtils.addVisits([fooPage, barPage]);
  let rootIcon = "http://foo.places.test/favicon.ico";
  await PlacesTestUtils.setFaviconForPage(fooPage, rootIcon, SMALLPNG_DATA_URI);
  await PlacesTestUtils.setFaviconForPage(barPage, rootIcon, SMALLPNG_DATA_URI);
  Assert.equal(await countTable("moz_icons_to_pages"), 1, "relation to bar");

  // The origin is gone, but the icon is still used by bar.
  await PlacesUtils.history.remove(fooPage);
  Assert.deepEqual(await iconUrls(), [rootIcon], "icon kept while associated");

  // Not associated anymore, and its origin is gone.
  await PlacesUtils.history.remove(barPage);
  Assert.deepEqual(await iconUrls(), [], "icon removed");
});

add_task(async function test_root_icon_removed_with_origin_without_pages() {
  await clearAll();

  let pageURI = Services.io.newURI("http://nopages.places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let rootIcon = "http://nopages.places.test/favicon.ico";
  await PlacesTestUtils.setFaviconForPage(pageURI, rootIcon, SMALLPNG_DATA_URI);
  // An origin may be kept without pages, it shouldn't keep root icons alive.
  await PlacesUtils.withConnectionWrapper("originWithoutPages", db =>
    db.execute(
      `INSERT INTO moz_origins (prefix, host, frecency)
       VALUES ('https://', 'www.nopages.places.test', 0)`
    )
  );
  registerCleanupFunction(() =>
    PlacesUtils.withConnectionWrapper("originWithoutPages", db =>
      db.execute(
        `DELETE FROM moz_origins WHERE host = 'www.nopages.places.test'`
      )
    )
  );

  await PlacesUtils.history.remove(pageURI);
  Assert.deepEqual(await iconUrls(), [], "root icon removed");
});

add_task(async function test_root_icon_of_removed_origin_replaced() {
  await clearAll();

  let fooPage = Services.io.newURI("http://foo.places.test/page/");
  let barPage = Services.io.newURI("http://bar.places.test/page/");
  await PlacesTestUtils.addVisits([fooPage, barPage]);
  let rootIcon = "http://foo.places.test/favicon.ico";
  let newIcon = "http://bar.places.test/new.png";
  await PlacesTestUtils.setFaviconForPage(fooPage, rootIcon, SMALLPNG_DATA_URI);
  await PlacesTestUtils.setFaviconForPage(barPage, rootIcon, SMALLPNG_DATA_URI);
  await PlacesUtils.history.remove(fooPage);
  Assert.deepEqual(await iconUrls(), [rootIcon], "icon kept while associated");

  // Replacing bar's icon leaves the root icon without relations and origin.
  await expireAllRelations();
  await PlacesTestUtils.setFaviconForPage(barPage, newIcon, SMALLPNG_DATA_URI);
  Assert.deepEqual(await iconUrls(), [newIcon], "root icon removed");
});
