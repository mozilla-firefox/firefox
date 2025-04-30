/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that adding new icons for a page expired old ones.
 */

add_task(async function test_expire_associated() {
  const TEST_URL = "http://mozilla.com/";
  await PlacesTestUtils.addVisits(TEST_URL);
  const TEST_URL2 = "http://test.mozilla.com/";
  await PlacesTestUtils.addVisits(TEST_URL2);

  let favicons = [
    {
      name: "favicon-normal16.png",
      mimeType: "image/png",
      expired: true,
    },
    {
      name: "favicon-normal32.png",
      mimeType: "image/png",
    },
    {
      name: "favicon-big64.png",
      mimeType: "image/png",
    },
  ];

  for (let icon of favicons) {
    let dataURL = await readFileDataAsDataURL(
      do_get_file(icon.name),
      icon.mimeType
    );
    await PlacesTestUtils.setFaviconForPage(
      TEST_URL,
      TEST_URL + icon.name,
      dataURL
    );
    if (icon.expired) {
      await expireIconRelationsForPage(TEST_URL);
      // Add the same icon to another page.
      await PlacesTestUtils.setFaviconForPage(
        TEST_URL2,
        TEST_URL + icon.name,
        dataURL
      );
    }
  }

  // Only the second and the third icons should have survived.
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URL, 16)).uri.spec,
    TEST_URL + favicons[1].name,
    "Should retrieve the 32px icon, not the 16px one."
  );
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URL, 64)).uri.spec,
    TEST_URL + favicons[2].name,
    "Should retrieve the 64px icon"
  );

  // The expired icon for page 2 should have survived.
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(TEST_URL2, 16)).uri.spec,
    TEST_URL + favicons[0].name,
    "Should retrieve the expired 16px icon"
  );
});

add_task(async function test_expire_root() {
  async function countEntries(tablename) {
    await PlacesTestUtils.promiseAsyncUpdates();
    let db = await PlacesUtils.promiseDBConnection();
    let rows = await db.execute("SELECT * FROM " + tablename);
    return rows.length;
  }

  // Clear the database.
  let promise = promiseTopicObserved(PlacesUtils.TOPIC_FAVICONS_EXPIRED);
  PlacesUtils.favicons.expireAllFavicons();
  await promise;

  Assert.equal(await countEntries("moz_icons"), 0, "There should be no icons");

  let pageURI = NetUtil.newURI("http://root.mozilla.com/");
  await PlacesTestUtils.addVisits(pageURI);

  // Insert an expired icon.
  let iconURI = NetUtil.newURI(pageURI.spec + "favicon-normal16.png");
  await PlacesTestUtils.setFaviconForPage(pageURI, iconURI, SMALLPNG_DATA_URI);
  Assert.equal(
    await countEntries("moz_icons_to_pages"),
    1,
    "There should be 1 association"
  );
  // Set an expired time on the icon-page relation.
  await expireIconRelationsForPage(pageURI.spec);

  // Now insert a new root icon.
  let rootIconURI = NetUtil.newURI(pageURI.spec + "favicon.ico");
  await PlacesTestUtils.setFaviconForPage(
    pageURI,
    rootIconURI,
    SMALLPNG_DATA_URI
  );

  // Only the root icon should have survived.
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(pageURI, 16)).uri.spec,
    rootIconURI.spec,
    "Should retrieve the root icon."
  );
  Assert.equal(
    await countEntries("moz_icons_to_pages"),
    0,
    "There should be no associations"
  );
});

async function expireIconRelationsForPage(url) {
  // Set an expired time on the icon-page relation.
  await PlacesUtils.withConnectionWrapper("expireFavicon", async db => {
    await db.execute(
      `
      UPDATE moz_icons_to_pages SET expire_ms = 0
      WHERE page_id = (SELECT id FROM moz_pages_w_icons WHERE page_url = :url)
      `,
      { url }
    );
    // Also ensure the icon is not expired, here we should only replace entries
    // based on their association expiration, not the icon expiration.
    let count = (
      await db.execute(
        `
        SELECT count(*) FROM moz_icons
        WHERE expire_ms < strftime('%s','now','localtime','utc') * 1000
        `
      )
    )[0].getResultByIndex(0);
    Assert.equal(count, 0, "All the icons should have future expiration");
  });
}
