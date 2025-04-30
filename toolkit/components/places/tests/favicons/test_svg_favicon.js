const PAGEURI = NetUtil.newURI("http://deliciousbacon.com/");

add_task(async function () {
  // First, add a history entry or else Places can't save a favicon.
  await PlacesTestUtils.addVisits({
    uri: PAGEURI,
    transition: TRANSITION_LINK,
    visitDate: Date.now() * 1000,
  });

  await PlacesUtils.favicons.setFaviconForPage(
    PAGEURI,
    SMALLSVG_DATA_URI,
    SMALLSVG_DATA_URI
  );

  let favicon = await PlacesTestUtils.getFaviconForPage(PAGEURI);
  Assert.equal(
    favicon.uri.spec,
    SMALLSVG_DATA_URI.spec,
    "setFavicon aURI check"
  );
  Assert.equal(favicon.rawData.length, 263, "setFavicon aDataLen check");
  Assert.equal(favicon.mimeType, "image/svg+xml", "setFavicon aMimeType check");
});
