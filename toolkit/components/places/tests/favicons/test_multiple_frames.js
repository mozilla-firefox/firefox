/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This file tests support for icons with multiple frames (like .ico files).
 */

add_task(async function () {
  //  in: 48x48 ico, 56646 bytes.
  // (howstuffworks.com icon, contains 13 icons with sizes from 16x16 to
  // 48x48 in varying depths)
  let pageURI = NetUtil.newURI("http://places.test/page/");
  await PlacesTestUtils.addVisits(pageURI);
  let faviconURI = NetUtil.newURI("http://places.test/icon/favicon-multi.ico");
  // Fake window.
  let win = { devicePixelRatio: 1.0 };
  let icoDataURL = await readFileDataAsDataURL(
    do_get_file("favicon-multi.ico"),
    "image/x-icon"
  );
  await PlacesTestUtils.setFaviconForPage(
    pageURI.spec,
    faviconURI.spec,
    icoDataURL
  );

  for (let size of [16, 32, 64]) {
    let allowMissing = AppConstants.USE_LIBZ_RS;
    let file = do_get_file(
      `favicon-multi-frame${size}` +
        (AppConstants.USE_LIBZ_RS ? ".libz-rs.png" : ".png"),
      allowMissing
    );
    if (!file.exists()) {
      file = do_get_file(`favicon-multi-frame${size}.png`);
    }

    let data = readFileData(file);

    info("Check getFaviconDataForPage");
    let icon = await PlacesTestUtils.getFaviconForPage(pageURI, size);
    Assert.equal(icon.mimeType, "image/png");
    Assert.deepEqual(icon.rawData, data);

    info("Check cached-favicon protocol");
    await compareFavicons(
      Services.io.newFileURI(file),
      PlacesUtils.urlWithSizeRef(
        win,
        PlacesUtils.favicons.getFaviconLinkForIcon(faviconURI).spec,
        size
      )
    );

    info("Check page-icon protocol");
    await compareFavicons(
      Services.io.newFileURI(file),
      PlacesUtils.urlWithSizeRef(win, "page-icon:" + pageURI.spec, size)
    );
  }
});
