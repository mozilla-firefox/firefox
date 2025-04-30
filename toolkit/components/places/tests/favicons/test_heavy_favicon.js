/* Any copyright is dedicated to the Public Domain.
 *    http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests a png with a large file size that can't fit MAX_FAVICON_BUFFER_SIZE,
 * it should be downsized until it can be stored, rather than thrown away.
 */

add_task(async function () {
  let file = do_get_file("noise.png");
  let icon = {
    file,
    uri: NetUtil.newURI(file),
    data: readFileData(file),
    mimetype: "image/png",
  };

  // If this should fail, it means MAX_FAVICON_BUFFER_SIZE has been made bigger
  // than this icon. For this test to make sense the icon shoul always be
  // bigger than MAX_FAVICON_BUFFER_SIZE. Please update the icon!
  Assert.ok(
    icon.data.length > Ci.nsIFaviconService.MAX_FAVICON_BUFFER_SIZE,
    "The test icon file size must be larger than Ci.nsIFaviconService.MAX_FAVICON_BUFFER_SIZE"
  );

  let pageURI = uri("http://foo.bar/");
  await PlacesTestUtils.addVisits(pageURI);

  let dataURI = await PlacesTestUtils.fileDataToDataURL(
    icon.data,
    icon.mimetype
  );
  await PlacesTestUtils.setFaviconForPage(pageURI.spec, icon.uri.spec, dataURI);
  Assert.equal(
    (await PlacesTestUtils.getFaviconForPage(pageURI)).uri.spec,
    icon.uri.spec,
    "A resampled version of the icon should be stored"
  );
});
