const TEST_URI1 = Services.io.newURI("http://mozilla.com/");
const TEST_URI2 = Services.io.newURI("http://places.com/");
const TEST_URI3 = Services.io.newURI("http://bookmarked.com/");
const TEST_LOCAL_URI = Services.io.newURI(
  "chrome://branding/content/icon32.png"
);
const LOAD_NON_PRIVATE = PlacesUtils.favicons.FAVICON_LOAD_NON_PRIVATE;
const LOAD_PRIVATE = PlacesUtils.favicons.FAVICON_LOAD_PRIVATE;

function copyFavicons(source, dest, inPrivate) {
  return new Promise(resolve => {
    PlacesUtils.favicons.copyFavicons(
      source,
      dest,
      inPrivate ? LOAD_PRIVATE : LOAD_NON_PRIVATE,
      resolve
    );
  });
}

function promisePageChanged(url) {
  return PlacesTestUtils.waitForNotification("favicon-changed", events =>
    events.some(e => e.url == url)
  );
}

add_task(async function test_copyFavicons_inputcheck() {
  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(null, TEST_URI2, LOAD_PRIVATE),
    /NS_ERROR_ILLEGAL_VALUE/
  );
  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(TEST_URI1, null, LOAD_PRIVATE),
    /NS_ERROR_ILLEGAL_VALUE/
  );
  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(TEST_URI1, TEST_URI2, 3),
    /NS_ERROR_ILLEGAL_VALUE/
  );
  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(TEST_URI1, TEST_URI2, -1),
    /NS_ERROR_ILLEGAL_VALUE/
  );
  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(TEST_URI1, TEST_URI2, null),
    /NS_ERROR_ILLEGAL_VALUE/
  );
});

add_task(async function test_copyFavicons_noop() {
  info("Unknown uris");
  Assert.equal(
    await copyFavicons(TEST_URI1, TEST_URI2, false),
    null,
    "Icon should not have been copied"
  );

  info("Unknown dest uri");
  await PlacesTestUtils.addVisits(TEST_URI1);
  Assert.equal(
    await copyFavicons(TEST_URI1, TEST_URI2, false),
    null,
    "Icon should not have been copied"
  );

  info("Unknown dest uri");
  await PlacesTestUtils.addVisits(TEST_URI1);
  Assert.equal(
    await copyFavicons(TEST_URI1, TEST_URI2, false),
    null,
    "Icon should not have been copied"
  );

  info("Unknown dest uri, source has icon");
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  Assert.equal(
    await copyFavicons(TEST_URI1, TEST_URI2, false),
    null,
    "Icon should not have been copied"
  );

  info("Known uris, source has icon, private");
  await PlacesTestUtils.addVisits(TEST_URI2);
  Assert.equal(
    await copyFavicons(TEST_URI1, TEST_URI2, true),
    null,
    "Icon should not have been copied"
  );

  PlacesUtils.favicons.expireAllFavicons();
  await PlacesUtils.history.clear();
});

add_task(async function test_copyFavicons() {
  info("Normal copy across 2 pages");
  await PlacesTestUtils.addVisits(TEST_URI1);
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLSVG_DATA_URI,
    SMALLSVG_DATA_URI
  );
  await PlacesTestUtils.addVisits(TEST_URI2);
  let promiseChange = promisePageChanged(TEST_URI2.spec);
  Assert.equal(
    (await copyFavicons(TEST_URI1, TEST_URI2, false)).spec,
    SMALLSVG_DATA_URI.spec,
    "Icon should have been copied"
  );
  await promiseChange;
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI2, 1)).uri.spec,
    SMALLPNG_DATA_URI.spec,
    "Small icon found"
  );
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI2)).uri.spec,
    SMALLSVG_DATA_URI.spec,
    "Large icon found"
  );

  info("Private copy to a bookmarked page");
  await PlacesUtils.bookmarks.insert({
    url: TEST_URI3,
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
  });
  promiseChange = promisePageChanged(TEST_URI3.spec);
  Assert.equal(
    (await copyFavicons(TEST_URI1, TEST_URI3, true)).spec,
    SMALLSVG_DATA_URI.spec,
    "Icon should have been copied"
  );
  await promiseChange;
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI3, 1)).uri.spec,
    SMALLPNG_DATA_URI.spec,
    "Small icon found"
  );
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI3)).uri.spec,
    SMALLSVG_DATA_URI.spec,
    "Large icon found"
  );

  PlacesUtils.favicons.expireAllFavicons();
  await PlacesUtils.history.clear();
});

add_task(async function test_copyFavicons_overlap() {
  info("Copy to a page that has one of the favicons already");
  await PlacesTestUtils.addVisits(TEST_URI1);
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLSVG_DATA_URI,
    SMALLSVG_DATA_URI
  );
  await PlacesTestUtils.addVisits(TEST_URI2);
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI2,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  let promiseChange = promisePageChanged(TEST_URI2.spec);
  Assert.equal(
    (await copyFavicons(TEST_URI1, TEST_URI2, false)).spec,
    SMALLSVG_DATA_URI.spec,
    "Icon should have been copied"
  );
  await promiseChange;
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI2, 1)).uri.spec,
    SMALLPNG_DATA_URI.spec,
    "Small icon found"
  );
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URI2)).uri.spec,
    SMALLSVG_DATA_URI.spec,
    "Large icon found"
  );
});

add_task(async function test_copyFavicons_local_uri() {
  await PlacesTestUtils.addVisits(TEST_URI1);
  await PlacesTestUtils.setFaviconForPage(
    TEST_URI1,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );

  Assert.throws(
    () => PlacesUtils.favicons.copyFavicons(TEST_URI1, TEST_LOCAL_URI, false),
    /NS_ERROR_ILLEGAL_VALUE/
  );
});
