/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  FaviconProvider: "resource:///modules/topsites/TopSites.sys.mjs",
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

let TEST_FAVICON_FILE;
let TEST_DOMAIN;
let TEST_PAGE_URL;
let TEST_FAVICON_URL;

const TEST_SVG_DATA_URL = Services.io.newURI(
  "data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy5" +
    "3My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMDAgMTAwIiBmaWxs" +
    "PSIjNDI0ZTVhIj4NCiAgPGNpcmNsZSBjeD0iNTAiIGN5PSI1MCIgcj0iN" +
    "DQiIHN0cm9rZT0iIzQyNGU1YSIgc3Ryb2tlLXdpZHRoPSIxMSIgZmlsbD" +
    "0ibm9uZSIvPg0KICA8Y2lyY2xlIGN4PSI1MCIgY3k9IjI0LjYiIHI9IjY" +
    "uNCIvPg0KICA8cmVjdCB4PSI0NSIgeT0iMzkuOSIgd2lkdGg9IjEwLjEi" +
    "IGhlaWdodD0iNDEuOCIvPg0KPC9zdmc%2BDQo%3D"
);

add_setup(async function setup() {
  info("Setup profile to use Places DB");
  do_get_profile(true);

  info("Setup favicon data");
  TEST_FAVICON_FILE = do_get_file("favicon.png");

  info("Setup http server that returns favicon content");
  const httpServer = new HttpServer();
  httpServer.registerPathHandler("/favicon.png", (request, response) => {
    const inputStream = Cc[
      "@mozilla.org/network/file-input-stream;1"
    ].createInstance(Ci.nsIFileInputStream);
    inputStream.init(TEST_FAVICON_FILE, 0x01, -1, null);
    const size = inputStream.available();
    const faviconData = NetUtil.readInputStreamToString(inputStream, size);

    response.setStatusLine(request.httpVersion, 200, "Ok");
    response.setHeader("Content-Type", "image/png", false);
    response.bodyOutputStream.write(faviconData, faviconData.length);
  });
  httpServer.start(-1);

  TEST_DOMAIN = "localhost";
  TEST_PAGE_URL = Services.io.newURI(
    `http://${TEST_DOMAIN}:${httpServer.identity.primaryPort}`
  );
  TEST_FAVICON_URL = Services.io.newURI(
    `http://${TEST_DOMAIN}:${httpServer.identity.primaryPort}/favicon.png`
  );

  info("Setup visit data in DB");
  await PlacesTestUtils.addVisits(TEST_PAGE_URL);

  // Save the original favicon service
  let originalFaviconService = PlacesUtils.favicons;

  registerCleanupFunction(async () => {
    // Restore the original favicon service
    PlacesUtils.favicons = originalFaviconService;
    await new Promise(resolve => httpServer.stop(resolve));
    await PlacesUtils.history.clear();
  });
});

// Test for getFaviconDataURLFromNetwork() function.
add_task(async function test_getFaviconDataURLFromNetwork() {
  const feed = new FaviconProvider();

  info("Get favicon data via FaviconProvider");
  const result = await feed.getFaviconDataURLFromNetwork(TEST_FAVICON_URL);
  Assert.equal(
    result.spec,
    // eslint-disable-next-line no-use-before-define
    await readFileDataAsDataURL(TEST_FAVICON_FILE, "image/png"),
    "getFaviconDataURLFromNetwork returns correct data url"
  );
});

// Test for fetchIcon() function. If there is valid favicon data for the page in
// DB, not update.
add_task(async function test_fetchIcon_with_valid_favicon() {
  const feed = new FaviconProvider();

  info("Setup stub to use dummy site data from FaviconProvider.getSite()");
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(feed, "getSite")
    .resolves({ domain: TEST_DOMAIN, image_url: TEST_FAVICON_URL.spec });

  info("Setup valid favicon data in DB");
  await PlacesUtils.favicons.setFaviconForPage(
    TEST_PAGE_URL,
    TEST_FAVICON_URL,
    TEST_SVG_DATA_URL
  );

  info("Call FaviconProvider.fetchIcon()");
  await feed.fetchIcon(TEST_PAGE_URL.spec);

  info("Check the database");
  const result = await PlacesUtils.favicons.getFaviconForPage(TEST_PAGE_URL);
  Assert.equal(result.mimeType, "image/svg+xml");
  Assert.equal(result.width, 65535);

  info("Clean up");
  await PlacesTestUtils.clearFavicons();
});

// Test for fetchIcon() function. If there is favicon data in DB but invalid or
// is not in DB, get favicon data from network, and update the DB with it.
add_task(async function test_fetchIcon_with_invalid_favicon() {
  for (const dummyFaviconInfo of [
    null,
    { iconUri: TEST_PAGE_URL, faviconSize: 0 },
  ]) {
    info(`Test for ${dummyFaviconInfo}`);
    const feed = new FaviconProvider();

    info("Setup stub to use dummy site data from FaviconProvider.getSite()");
    const sandbox = sinon.createSandbox();
    sandbox
      .stub(feed, "getSite")
      .resolves({ domain: TEST_DOMAIN, image_url: TEST_FAVICON_URL.spec });

    info("Setup stub to simulate invalid favicon");
    sandbox.stub(feed, "getFaviconInfo").resolves(dummyFaviconInfo);

    info("Call FaviconProvider.fetchIcon()");
    await feed.fetchIcon(TEST_PAGE_URL.spec);

    info("Check the database");
    const result = await PlacesUtils.favicons.getFaviconForPage(TEST_PAGE_URL);
    // eslint-disable-next-line no-use-before-define
    const expectedFaviconData = readFileData(TEST_FAVICON_FILE);
    Assert.equal(result.uri.spec, `${TEST_FAVICON_URL.spec}#tippytop`);
    Assert.deepEqual(result.rawData, expectedFaviconData);
    Assert.equal(result.mimeType, "image/png");
    Assert.equal(result.width, 16);

    info("Clean up");
    await PlacesTestUtils.clearFavicons();
    sandbox.restore();
  }
});

// Test for fetchIconFromRedirects() function. If there is valid favicon data
// for the redirected page in DB, copy the favicon data to the destination page as well.
add_task(async function test_fetchIconFromRedirects_with_valid_favicon() {
  const feed = new FaviconProvider();

  info("Setup stub to use dummy site data from FaviconProvider.getSite()");
  const sandbox = sinon.createSandbox();
  sandbox
    .stub(NewTabUtils.activityStreamProvider, "executePlacesQuery")
    .resolves([
      { visit_id: 1, url: TEST_DOMAIN },
      { visit_id: 2, url: TEST_PAGE_URL.spec },
    ]);

  info("Setup valid favicon data in DB");
  await PlacesUtils.favicons.setFaviconForPage(
    TEST_PAGE_URL,
    TEST_FAVICON_URL,
    TEST_SVG_DATA_URL
  );

  info("Setup destination");
  const destination = Services.io.newURI("http://destination.localhost/");
  await PlacesTestUtils.addVisits(destination);

  info("Call FaviconProvider.fetchIconFromRedirects()");
  await feed.fetchIconFromRedirects(destination.spec);

  info("Check the database");
  await TestUtils.waitForCondition(async () => {
    const result = await PlacesUtils.favicons.getFaviconForPage(destination);
    return !!result;
  });
  const sourceResult =
    await PlacesUtils.favicons.getFaviconForPage(TEST_PAGE_URL);
  const destinationResult =
    await PlacesUtils.favicons.getFaviconForPage(destination);
  Assert.deepEqual(destinationResult.rawData, sourceResult.rawData);
  Assert.equal(destinationResult.mimeType, sourceResult.mimeType);
  Assert.equal(destinationResult.width, sourceResult.width);

  info("Clean up");
  await PlacesTestUtils.clearFavicons();
  sandbox.restore();
});

// Copy from toolkit/components/places/tests/head_common.js
function readInputStreamData(aStream) {
  let bistream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  try {
    bistream.setInputStream(aStream);
    let expectedData = [];
    let avail;
    while ((avail = bistream.available())) {
      expectedData = expectedData.concat(bistream.readByteArray(avail));
    }
    return expectedData;
  } finally {
    bistream.close();
  }
}

function readFileData(aFile) {
  let inputStream = Cc[
    "@mozilla.org/network/file-input-stream;1"
  ].createInstance(Ci.nsIFileInputStream);
  // init the stream as RD_ONLY, -1 == default permissions.
  inputStream.init(aFile, 0x01, -1, null);

  // Check the returned size versus the expected size.
  let size = inputStream.available();
  let bytes = readInputStreamData(inputStream);
  if (size !== bytes.length) {
    throw new Error("Didn't read expected number of bytes");
  }
  return bytes;
}

async function fileDataToDataURL(data, mimeType) {
  const dataURL = await new Promise(resolve => {
    const buffer = new Uint8ClampedArray(data);
    const blob = new Blob([buffer], { type: mimeType });
    const reader = new FileReader();
    reader.onload = e => {
      resolve(e.target.result);
    };
    reader.readAsDataURL(blob);
  });
  return dataURL;
}

async function readFileDataAsDataURL(file, mimeType) {
  const data = readFileData(file);
  return fileDataToDataURL(data, mimeType);
}

const FAKE_SMALLPNG_DATA_URI =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg==";

// Test: fetchIcon should fetch favicon from network if no data in DB
add_task(async function test_fetchIcon_withNetworkFetch() {
  const sandbox = sinon.createSandbox();
  let feed = new FaviconProvider();
  let url = "https://mozilla.org/";

  // Set up mocks
  PlacesUtils.favicons = {
    getFaviconForPage: sandbox.stub().returns(Promise.resolve(null)),
    setFaviconForPage: sandbox.spy(),
    copyFavicons: sandbox.spy(),
  };
  feed.getSite = sandbox
    .stub()
    .returns(
      Promise.resolve({ domain: "mozilla.org", image_url: `${url}/icon.png` })
    );

  // Mock the getFaviconDataURLFromNetwork method
  sandbox
    .stub(feed, "getFaviconDataURLFromNetwork")
    .resolves({ spec: FAKE_SMALLPNG_DATA_URI });

  await feed.fetchIcon(url);

  // Assertions
  Assert.equal(PlacesUtils.favicons.setFaviconForPage.calledOnce, true);
  Assert.equal(
    PlacesUtils.favicons.setFaviconForPage.firstCall.args[2].spec,
    FAKE_SMALLPNG_DATA_URI
  );

  sandbox.restore();
});

// Test: fetchIcon should fetch favicon from network if invalid data in DB
add_task(async function test_fetchIcon_withInvalidDataInDb() {
  const sandbox = sinon.createSandbox();
  // Set up mocks
  PlacesUtils.favicons = {
    // Invalid since no width.
    getFaviconForPage: sandbox
      .stub()
      .returns(Promise.resolve({ iconUri: { spec: FAKE_SMALLPNG_DATA_URI } })),
    setFaviconForPage: sandbox.spy(),
    copyFavicons: sandbox.spy(),
  };

  let feed = new FaviconProvider();
  let url = "https://mozilla.org/";
  feed.getSite = sandbox
    .stub()
    .returns(
      Promise.resolve({ domain: "mozilla.org", image_url: `${url}/icon.png` })
    );

  // Mock the getFaviconDataURLFromNetwork method
  sandbox
    .stub(feed, "getFaviconDataURLFromNetwork")
    .resolves({ spec: FAKE_SMALLPNG_DATA_URI });

  await feed.fetchIcon(url);

  // Assertions
  Assert.equal(PlacesUtils.favicons.setFaviconForPage.calledOnce, true);
  Assert.equal(
    PlacesUtils.favicons.setFaviconForPage.firstCall.args[2].spec,
    FAKE_SMALLPNG_DATA_URI
  );

  sandbox.restore();
});

// Test: fetchIcon should not set favicon if valid data exists in DB
add_task(async function test_fetchIcon_withValidDataInDb() {
  const sandbox = sinon.createSandbox();
  // Set up mocks
  PlacesUtils.favicons = {
    getFaviconForPage: sandbox.stub().returns(
      Promise.resolve({
        iconUri: { spec: FAKE_SMALLPNG_DATA_URI },
        width: 100,
      })
    ),
    setFaviconForPage: sandbox.spy(),
    copyFavicons: sandbox.spy(),
  };
  let feed = new FaviconProvider();
  let url = "https://mozilla.org/";
  feed.getSite = sandbox
    .stub()
    .returns(
      Promise.resolve({ domain: "mozilla.org", image_url: `${url}/icon.png` })
    );

  await feed.fetchIcon(url);

  // Assertions
  Assert.equal(PlacesUtils.favicons.setFaviconForPage.called, false);

  sandbox.restore();
});

// Test: fetchIcon should not set favicon if the URL is not in TippyTop data
add_task(async function test_fetchIcon_withNoTippyTopData() {
  const sandbox = sinon.createSandbox();
  let feed = new FaviconProvider();
  // Set up mocks
  PlacesUtils.favicons = {
    getFaviconForPage: sandbox.stub().returns(Promise.resolve(null)),
    setFaviconForPage: sandbox.spy(),
    copyFavicons: sandbox.spy(),
  };
  feed.getSite = sandbox.stub().returns(Promise.resolve(null));

  await feed.fetchIcon("https://example.com");

  // Assertions
  Assert.equal(PlacesUtils.favicons.setFaviconForPage.called, false);

  sandbox.restore();
});
