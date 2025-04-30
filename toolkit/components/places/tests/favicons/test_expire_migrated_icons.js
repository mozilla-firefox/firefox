/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file tests that favicons migrated from a previous profile, having a 0
 * expiration, will be properly expired when fetching new ones.
 */

add_setup(() => {
  registerCleanupFunction(async () => {
    PlacesUtils.favicons.expireAllFavicons();
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_storing_a_normal_16x16_icon() {
  const PAGE_URL = Services.io.newURI("http://places.test");
  await PlacesTestUtils.addVisits(PAGE_URL);
  await PlacesTestUtils.setFaviconForPage(
    PAGE_URL,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );

  // Now set expiration to 0 and change the payload.
  info("Set expiration to 0 and replace favicon data");
  await PlacesUtils.withConnectionWrapper("Change favicons payload", db => {
    return db.execute(`UPDATE moz_icons SET expire_ms = 0, data = "test"`);
  });

  let favicon = await PlacesTestUtils.getFaviconForPage(PAGE_URL);
  Assert.equal(favicon.mimeType, "image/png");
  Assert.deepEqual(
    favicon.rawData,
    "test".split("").map(c => c.charCodeAt(0))
  );

  info("Refresh favicon");
  await PlacesTestUtils.setFaviconForPage(
    PAGE_URL,
    SMALLPNG_DATA_URI,
    SMALLPNG_DATA_URI
  );
  await compareFavicons("page-icon:" + PAGE_URL.spec, SMALLPNG_DATA_URI);
});
