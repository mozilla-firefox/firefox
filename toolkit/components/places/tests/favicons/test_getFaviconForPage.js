/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests for getFaviconForPage()
 */

add_task(async function test_basic() {
  const favicon = await createFavicon("favicon.png");

  const pageURI = uri("http://example.com/");
  const faviconURI = uri("http://example.com/favicon.png");
  const faviconDataURI = await createDataURLForFavicon(favicon);

  await doTestGetFaviconForPage({
    pageURI,
    faviconURI,
    faviconDataURI,
    expectedFavicon: {
      uri: faviconURI,
      dataURI: faviconDataURI,
      rawData: favicon.data,
      mimeType: "image/png",
      width: 16,
    },
  });

  await IOUtils.remove(favicon.file.path);
});

add_task(async function test_svg_basic() {
  const favicon = "<svg><rect width='1px' height='1px'/></svg>";

  const pageURI = uri("http://example.com/");
  const faviconURI = uri("http://example.com/favicon.svg");
  const faviconDataURI = uri(`data:image/svg+xml;utf8,${favicon}`);

  await doTestGetFaviconForPage({
    pageURI,
    faviconURI,
    faviconDataURI,
    expectedFavicon: {
      uri: faviconURI,
      dataURI: uri(`data:image/svg+xml;base64,${base64EncodeString(favicon)}`),
      rawData: Array.from(favicon).map(s => s.charCodeAt(0)),
      mimeType: "image/svg+xml",
      width: 65535,
    },
  });
});

add_task(async function test_userpass() {
  const favicon = await createFavicon("favicon.png");

  const pageURI = uri("http://user:pass@example.com/");
  const faviconURI = uri("http://user:pass@example.com/favicon.png");
  const faviconDataURI = await createDataURLForFavicon(favicon);

  await doTestGetFaviconForPage({
    pageURI,
    faviconURI,
    faviconDataURI,
    expectedFavicon: {
      uri: uri("http://example.com/favicon.png"),
      dataURI: faviconDataURI,
      rawData: favicon.data,
      mimeType: "image/png",
      width: 16,
    },
  });

  await IOUtils.remove(favicon.file.path);
});

async function doTestGetFaviconForPage({
  pageURI,
  faviconURI,
  faviconDataURI,
  expectedFavicon,
}) {
  await PlacesTestUtils.addVisits(pageURI);
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    faviconURI,
    faviconDataURI
  );

  const result = await PlacesUtils.favicons.getFaviconForPage(pageURI);
  Assert.equal(result.uri.spec, expectedFavicon.uri.spec);
  Assert.equal(result.dataURI.spec, expectedFavicon.dataURI.spec);
  Assert.equal(result.rawData.join(","), expectedFavicon.rawData.join(","));
  Assert.equal(result.mimeType, expectedFavicon.mimeType);
  Assert.equal(result.width, expectedFavicon.width);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
}

add_task(async function test_fallback() {
  const rootPageURI = uri("http://example.com/");
  const rootFaviconURI = uri("http://example.com/favicon.ico");
  const favicon16File = do_get_file("favicon-normal16.png");
  const favicon16DataURI = await createDataURLForFavicon({
    data: readFileData(favicon16File),
    mimeType: "image/png",
  });

  info("Set icon for the root");
  await PlacesTestUtils.addVisits(rootPageURI);
  await PlacesUtils.favicons.setFaviconForPage(
    rootPageURI,
    rootFaviconURI,
    favicon16DataURI
  );

  info("Check fallback icons");
  const subPageURI = uri("http://example.com/missing");
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(subPageURI)).uri.spec,
    rootFaviconURI.spec
  );

  info("Now add a new icon for the subpage");
  await PlacesTestUtils.addVisits(subPageURI);
  const subFaviconURI = uri("http://example.com/favicon.png");
  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  await PlacesTestUtils.setFaviconForPage(
    subPageURI,
    subFaviconURI,
    favicon32DataURI
  );

  info("Check no fallback icons");
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(rootPageURI)).uri.spec,
    rootFaviconURI.spec
  );
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(subPageURI)).uri.spec,
    subFaviconURI.spec
  );

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_fallback_no_root_icon() {
  const rootPageURI = uri("http://example.com/");
  const subPageURIs = [
    uri("http://example.com/page"),
    uri("http://example.com/about"),
    uri("http://example.com/home"),
  ];
  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });

  for (let i = 0; i < 10; i++) {
    await PlacesTestUtils.addVisits(subPageURIs[0]);
  }
  await PlacesTestUtils.addVisits(subPageURIs[1]);
  await PlacesTestUtils.addVisits(subPageURIs[2]);

  for (let uri of subPageURIs) {
    await PlacesTestUtils.setFaviconForPage(
      uri,
      uri.spec + "/favicon.ico",
      favicon32DataURI
    );
  }

  await PlacesTestUtils.addVisits(rootPageURI);

  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(rootPageURI)).uri.spec,
    subPageURIs[0].spec + "/favicon.ico",
    "No root icon, should use icon from most frecent subpage"
  );
});

add_task(async function test_fallback_no_root_icon_with_port() {
  const pageURI1 = uri("http://example.com:3000");
  const subPageURI1 = uri("http://example.com:3000/subpage");
  const subFaviconURI1 = uri("http://example.com:3000/subpage/favicon.ico");
  const pageURI2 = uri("http://example.com:5000");
  const subPageURI2 = uri("http://example.com:5000/subpage");
  const subFaviconURI2 = uri("http://example.com:5000/subpage/favicon.ico");

  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });

  await PlacesTestUtils.addVisits(subPageURI1);
  await PlacesTestUtils.addVisits(Array(10).fill(subPageURI2));

  await PlacesTestUtils.setFaviconForPage(
    subPageURI1,
    subFaviconURI1,
    favicon32DataURI
  );

  await PlacesTestUtils.addVisits(subPageURI1);
  await PlacesTestUtils.addVisits(subPageURI2);

  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(pageURI1)).uri.spec,
    subFaviconURI1.spec,
    "No root icon, should use icon from most frecent subpage"
  );

  Assert.equal(
    await PlacesUtils.favicons.getFaviconForPage(pageURI2),
    null,
    "Should return null since no icons exist for root or its subpages"
  );

  await PlacesTestUtils.setFaviconForPage(
    subPageURI2,
    subFaviconURI2,
    favicon32DataURI
  );

  await PlacesTestUtils.addVisits("http://localhost:5000/other_subpage");

  await PlacesTestUtils.setFaviconForPage(
    "http://localhost:5000/other_subpage",
    "http://localhost:5000/other_subpage/favicon.ico",
    favicon32DataURI
  );

  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(subPageURI2)).uri.spec,
    subFaviconURI2.spec,
    "No root icon, should use icon from most frecent subpage"
  );

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_rich_priority_below_threshold() {
  const pageURI = uri("http://example.com/");
  await PlacesTestUtils.addVisits(pageURI);

  const favicon16File = do_get_file("favicon-normal16.png");
  const favicon16URI = uri("http://example.com/favicon16.png");
  const favicon16DataURI = await createDataURLForFavicon({
    data: readFileData(favicon16File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon16URI,
    favicon16DataURI,
    0,
    true // Rich
  );

  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32URI = uri("http://example.com/favicon32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon32URI,
    favicon32DataURI,
    0,
    false // Non-rich
  );

  // Non-rich icon should be prioritieze for preferred width <= 64px.
  const result = await PlacesUtils.favicons.getFaviconForPage(pageURI, 8);
  Assert.equal(result.uri.spec, favicon32URI.spec);
  Assert.equal(result.dataURI.spec, favicon32DataURI.spec);
  Assert.equal(result.mimeType, "image/png");
  Assert.equal(result.width, 32);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_rich_priority_above_threshold() {
  const pageURI = uri("http://example.com/");
  await PlacesTestUtils.addVisits(pageURI);

  const favicon16File = do_get_file("favicon-normal16.png");
  const favicon16URI = uri("http://example.com/favicon16.png");
  const favicon16DataURI = await createDataURLForFavicon({
    data: readFileData(favicon16File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon16URI,
    favicon16DataURI,
    0,
    false // Non-rich
  );

  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32URI = uri("http://example.com/favicon32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon32URI,
    favicon32DataURI,
    0,
    true // Rich
  );

  // Non-rich icon should be prioritized for preferred width > 64px.
  const result = await PlacesUtils.favicons.getFaviconForPage(pageURI, 72);
  Assert.equal(result.uri.spec, favicon32URI.spec);
  Assert.equal(result.dataURI.spec, favicon32DataURI.spec);
  Assert.equal(result.mimeType, "image/png");
  Assert.equal(result.width, 32);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_size_selection() {
  const pageURI = uri("http://example.com/");
  await PlacesTestUtils.addVisits(pageURI);

  // Icons set:
  //  - 16px non-rich
  //  - 32px rich
  //  - 64px non-rich
  const favicon16File = do_get_file("favicon-normal16.png");
  const favicon16URI = uri("http://example.com/favicon16.png");
  const favicon16DataURI = await createDataURLForFavicon({
    data: readFileData(favicon16File),
    mimeType: "image/png",
  });
  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32URI = uri("http://example.com/favicon32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  const favicon64File = do_get_file("favicon-big64.png");
  const favicon64URI = uri("http://example.com/favicon64.png");
  const favicon64DataURI = await createDataURLForFavicon({
    data: readFileData(favicon64File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon16URI,
    favicon16DataURI,
    0,
    false // Non-rich
  );
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon32URI,
    favicon32DataURI,
    0,
    true // Rich
  );
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon64URI,
    favicon64DataURI,
    0,
    false // Non-rich
  );

  // Should select 16px icon, non-rich icons are prioritized for preferred
  // size <= 64px, and (24 - 16) = 8 <= (64 - 24) / 4 = 10.
  // Therefore smaller icon is selected.
  const faviconFor24 = await PlacesUtils.favicons.getFaviconForPage(
    pageURI,
    24
  );
  Assert.equal(faviconFor24.uri.spec, favicon16URI.spec);
  Assert.equal(faviconFor24.dataURI.spec, favicon16DataURI.spec);
  Assert.equal(faviconFor24.mimeType, "image/png");
  Assert.equal(faviconFor24.width, 16);

  // Should select 64px icon, non-rich icons are prioritized for preferred
  // size <= 64px, and (64 - 32) / 4 = 8 < (32 - 16) = 16.
  // Therefore, larger icon is selected.
  const faviconFor32 = await PlacesUtils.favicons.getFaviconForPage(
    pageURI,
    32
  );
  Assert.equal(faviconFor32.uri.spec, favicon64URI.spec);
  Assert.equal(faviconFor32.dataURI.spec, favicon64DataURI.spec);
  Assert.equal(faviconFor32.mimeType, "image/png");
  Assert.equal(faviconFor32.width, 64);

  // Should select 64px icon, no discrimination between rich/non-rich for
  // preferred size > 64px.
  const faviconFor80 = await PlacesUtils.favicons.getFaviconForPage(
    pageURI,
    80
  );
  Assert.equal(faviconFor80.uri.spec, favicon64URI.spec);
  Assert.equal(faviconFor80.dataURI.spec, favicon64DataURI.spec);
  Assert.equal(faviconFor80.mimeType, "image/png");
  Assert.equal(faviconFor80.width, 64);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_size_selection_rich_only() {
  const pageURI = uri("http://example.com/");
  await PlacesTestUtils.addVisits(pageURI);

  const favicon16File = do_get_file("favicon-normal16.png");
  const favicon16URI = uri("http://example.com/favicon16.png");
  const favicon16DataURI = await createDataURLForFavicon({
    data: readFileData(favicon16File),
    mimeType: "image/png",
  });
  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32URI = uri("http://example.com/favicon32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  const favicon64File = do_get_file("favicon-big64.png");
  const favicon64URI = uri("http://example.com/favicon64.png");
  const favicon64DataURI = await createDataURLForFavicon({
    data: readFileData(favicon64File),
    mimeType: "image/png",
  });
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon16URI,
    favicon16DataURI,
    0,
    true
  );
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon32URI,
    favicon32DataURI,
    0,
    true
  );
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon64URI,
    favicon64DataURI,
    0,
    true
  );

  // Should select 16px icon, since there are no non-rich icons found,
  // we return the best-sized rich icon.
  const faviconFor17 = await PlacesUtils.favicons.getFaviconForPage(
    pageURI,
    17
  );
  Assert.equal(faviconFor17.uri.spec, favicon16URI.spec);
  Assert.equal(faviconFor17.dataURI.spec, favicon16DataURI.spec);
  Assert.equal(faviconFor17.mimeType, "image/png");
  Assert.equal(faviconFor17.width, 16);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_svg_selection() {
  const pageURI = uri("http://example.com/");

  // SVG
  const faviconSVG = "<svg><rect width='1px' height='1px'/></svg>";
  const faviconSVGURI = uri("http://example.com/favicon.svg");
  const faviconSVGDataURI = uri(`data:image/svg+xml;utf8,${faviconSVG}`);
  const faviconSVGDataURIBase64 = uri(
    `data:image/svg+xml;base64,${base64EncodeString(faviconSVG)}`
  );
  // 32px png
  const favicon32File = do_get_file("favicon-normal32.png");
  const favicon32URI = uri("http://example.com/favicon32.png");
  const favicon32DataURI = await createDataURLForFavicon({
    data: readFileData(favicon32File),
    mimeType: "image/png",
  });
  // 64px png
  const favicon64File = do_get_file("favicon-big64.png");
  const favicon64URI = uri("http://example.com/favicon64.png");
  const favicon64DataURI = await createDataURLForFavicon({
    data: readFileData(favicon64File),
    mimeType: "image/png",
  });

  info("Selected non-svg is not a perfect fit, so we prefer SVG");
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: false,
    },
    favicon2: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      isRich: false,
    },
    preferredSize: 31,
    expected: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURIBase64,
      mimeType: "image/svg+xml",
      width: 65535,
    },
  });

  info("Selected non-svg is a perfect fit, so we prefer it");
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: false,
    },
    favicon2: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      isRich: false,
    },
    preferredSize: 32,
    expected: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      mimeType: "image/png",
      width: 32,
    },
  });

  info("Selected non-svg is a perfect fit, but it is rich so we prefer SVG");
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: false,
    },
    favicon2: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      isRich: true,
    },
    preferredSize: 32,
    expected: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURIBase64,
      mimeType: "image/svg+xml",
      width: 65535,
    },
  });

  info(
    "Selected non-svg is not a perfect fit, but SVG is rich, so we prefer non-SVG"
  );
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: true,
    },
    favicon2: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      isRich: false,
    },
    preferredSize: 31,
    expected: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      mimeType: "image/png",
      width: 32,
    },
  });

  info(
    "Selected non-SVG is a perfect fit and it is rich, and SVG is also rich, so we prefer the original non-SVG selection"
  );
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: true,
    },
    favicon2: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      isRich: true,
    },
    preferredSize: 32,
    expected: {
      uri: favicon32URI,
      dataURI: favicon32DataURI,
      mimeType: "image/png",
      width: 32,
    },
  });

  info(
    "When requested size is above threshold we have no preference when it comes to richness"
  );
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: true,
    },
    favicon2: {
      uri: favicon64URI,
      dataURI: favicon64DataURI,
      isRich: false,
    },
    preferredSize: 65,
    expected: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURIBase64,
      mimeType: "image/svg+xml",
      width: 65535,
    },
  });

  info("Prefer non-rich SVG when requested size is below threshold");
  await doTestSVGSelection({
    pageURI,
    favicon1: {
      uri: uri("http://example.com/favicon.svg#2"),
      dataURI: faviconSVGDataURI,
      isRich: true,
    },
    favicon2: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURI,
      isRich: false,
    },
    preferredSize: 32,
    expected: {
      uri: faviconSVGURI,
      dataURI: faviconSVGDataURIBase64,
      mimeType: "image/svg+xml",
      width: 65535,
    },
  });
});

async function doTestSVGSelection({
  pageURI,
  favicon1,
  favicon2,
  preferredSize,
  expected,
}) {
  await PlacesTestUtils.addVisits(pageURI);

  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon1.uri,
    favicon1.dataURI,
    0,
    favicon1.isRich
  );
  await PlacesUtils.favicons.setFaviconForPage(
    pageURI,
    favicon2.uri,
    favicon2.dataURI,
    0,
    favicon2.isRich
  );

  const result = await PlacesUtils.favicons.getFaviconForPage(
    pageURI,
    preferredSize
  );
  Assert.equal(result.uri.spec, expected.uri.spec);
  Assert.equal(result.dataURI.spec, expected.dataURI.spec);
  Assert.equal(result.mimeType, expected.mimeType);
  Assert.equal(result.width, expected.width);

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
}

add_task(async function test_port() {
  const pageURI = uri("http://example.com/");
  const faviconURI = uri("http://example.com/favicon.ico");
  const faviconFile = do_get_file("favicon-normal32.png");
  const faviconDataURI = await createDataURLForFavicon({
    data: readFileData(faviconFile),
    mimeType: "image/png",
  });

  const pageURIWithPort = uri("http://example.com:5000/");
  const faviconURIWithPort = uri("http://example.com:5000/favicon.ico");
  const faviconFileWithPort = do_get_file("favicon-normal16.png");
  const faviconDataURIWithPort = await createDataURLForFavicon({
    data: readFileData(faviconFileWithPort),
    mimeType: "image/png",
  });

  info("Set icon for the URL with the port");
  await PlacesTestUtils.addVisits(pageURIWithPort);
  await PlacesTestUtils.setFaviconForPage(
    pageURIWithPort,
    faviconURIWithPort,
    faviconDataURIWithPort
  );
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(pageURIWithPort)).uri.spec,
    faviconURIWithPort.spec,
    "The favicon of the URL with the port should be chosen"
  );

  info("Set icon for the URL without the port");
  await PlacesTestUtils.addVisits(pageURI);
  await PlacesTestUtils.setFaviconForPage(pageURI, faviconURI, faviconDataURI);
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(pageURIWithPort)).uri.spec,
    faviconURIWithPort.spec,
    "The favicon of the URL with the port should still be chosen when both are defined"
  );
  Assert.equal(
    (await PlacesUtils.favicons.getFaviconForPage(pageURI)).uri.spec,
    faviconURI.spec,
    "The favicon of the URL without the port should be chosen correctly when there is an icon defined for the url with a port"
  );

  await PlacesUtils.history.clear();
  await PlacesTestUtils.clearFavicons();
});

add_task(async function test_no_favicon() {
  const pageURI = uri("http://example.com/");
  const result = await PlacesUtils.favicons.getFaviconForPage(pageURI);
  Assert.equal(result, null);
});

add_task(async function test_invalid_page_uri() {
  const TEST_DATA = [
    {
      pageURI: null,
      expected: "NS_ERROR_ILLEGAL_VALUE",
    },
    {
      pageURI: "",
      expected: "NS_ERROR_XPC_BAD_CONVERT_JS",
    },
    {
      pageURI: "http://example.com",
      expected: "NS_ERROR_XPC_BAD_CONVERT_JS",
    },
  ];

  for (let { pageURI, expected } of TEST_DATA) {
    try {
      info(`Invalid page URI test for [${pageURI}]`);
      await PlacesUtils.favicons.getFaviconForPage(pageURI);
      Assert.ok(false, "Error should happened");
    } catch (e) {
      Assert.equal(
        e.name,
        expected,
        `Expected error happened for [${pageURI}]`
      );
    }
  }
});
