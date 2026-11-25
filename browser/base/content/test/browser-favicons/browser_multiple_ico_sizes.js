/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { MAX_ICON_SIZE, MAX_ICO_IMAGE_SIZE, MAX_ICO_IMAGE_COUNT } =
  ChromeUtils.importESModule("moz-src:///toolkit/modules/FaviconUtils.sys.mjs");
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const TEST_ROOT =
  "http://mochi.test:8888/browser/browser/base/content/test/browser-favicons/";

// Opens a tab on a page without icons, once its root icon has been handled.
function withTestTab(callback) {
  return BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:blank" },
    async browser => {
      let rootIconPromise = waitForFaviconMessage(
        true,
        "http://mochi.test:8888/favicon.ico"
      );
      BrowserTestUtils.startLoadingURIString(browser, TEST_ROOT + "blank.html");
      await rootIconPromise;
      return callback(browser);
    }
  );
}

function loadIcoWithSizes(sizes) {
  let iconURL = `${TEST_ROOT}file_ico_sizes.sjs?sizes=${sizes.join(",")}`;
  return withTestTab(async browser => {
    let iconPromise = waitForFavicon(browser, iconURL);
    await SpecialPowers.spawn(browser, [iconURL], href => {
      let link = content.document.createElement("link");
      link.rel = "icon";
      link.href = href;
      content.document.head.appendChild(link);
    });
    await iconPromise;
    return browser.mIconURL;
  });
}

function createImage(size = 1) {
  return {
    blob: new Blob([new Uint8Array(size * size * 4).fill(255)]),
    format: "RGBA",
    displayWidth: size,
    displayHeight: size,
  };
}

// Passes the given images to the parent actor, as the content process would,
// and returns the URLs set as the tab icon for them.
function setIconFromImages(images) {
  let originalURL = TEST_ROOT + "icon.ico";
  return withTestTab(async browser => {
    let setIconSpy = sinon.spy(gBrowser, "setIcon");
    try {
      let actor =
        browser.browsingContext.currentWindowGlobal.getActor("LinkHandler");
      await actor.setIconFromLink(gBrowser, browser, {
        originalURL,
        images,
        canStoreIcon: false,
        isTabIcon: true,
      });
    } finally {
      setIconSpy.restore();
    }
    // The root icon of the page may be set concurrently, ignore it.
    return setIconSpy
      .getCalls()
      .filter(call => call.args[2] == originalURL)
      .map(call => call.args[1]);
  });
}

function icoEntriesCount(dataURL) {
  const prefix = "data:image/x-icon;base64,";
  Assert.ok(dataURL.startsWith(prefix), "Should have an ICO data URL");
  let bytes = atob(dataURL.slice(prefix.length));
  return bytes.charCodeAt(4) | (bytes.charCodeAt(5) << 8);
}

function pngSize(dataURL) {
  const prefix = "data:image/png;base64,";
  Assert.ok(dataURL.startsWith(prefix), "Should use a single image");
  let view = new DataView(
    Uint8Array.from(atob(dataURL.slice(prefix.length)), c => c.charCodeAt(0))
      .buffer
  );
  return { width: view.getUint32(16), height: view.getUint32(20) };
}

add_task(async function test_all_sizes_kept() {
  let sizes = Array.from({ length: MAX_ICO_IMAGE_COUNT }, (_, i) => i + 1);
  let dataURL = await loadIcoWithSizes(sizes);
  Assert.equal(
    icoEntriesCount(dataURL),
    sizes.length,
    "Should keep all the sizes"
  );
});

add_task(async function test_duplicate_sizes() {
  let sizes = Array.from(
    { length: MAX_ICO_IMAGE_COUNT + 2 },
    (_, i) => [2, 4, 8][i % 3]
  );
  let dataURL = await loadIcoWithSizes(sizes);
  Assert.equal(icoEntriesCount(dataURL), 3, "Should keep each size once");
});

add_task(async function test_single_size() {
  let dataURL = await loadIcoWithSizes([16]);
  Assert.deepEqual(
    pngSize(dataURL),
    { width: 16, height: 16 },
    "Should not upscale the image"
  );
});

add_task(async function test_too_many_sizes() {
  let sizes = Array.from({ length: MAX_ICO_IMAGE_COUNT + 1 }, (_, i) => i + 1);
  let dataURL = await loadIcoWithSizes(sizes);
  Assert.deepEqual(
    pngSize(dataURL),
    { width: MAX_ICO_IMAGE_COUNT + 1, height: MAX_ICO_IMAGE_COUNT + 1 },
    "Should use the largest image"
  );
});

add_task(async function test_too_many_sizes_downscaled() {
  let sizes = Array.from({ length: MAX_ICO_IMAGE_COUNT + 1 }, (_, i) => i + 1);
  sizes.push(MAX_ICO_IMAGE_SIZE + 1);
  let dataURL = await loadIcoWithSizes(sizes);
  Assert.deepEqual(
    pngSize(dataURL),
    { width: MAX_ICO_IMAGE_SIZE, height: MAX_ICO_IMAGE_SIZE },
    "Should downscale the largest image"
  );
});

add_task(async function test_large_size_downscaled() {
  let dataURL = await loadIcoWithSizes([16, 32, MAX_ICO_IMAGE_SIZE + 1]);
  Assert.equal(
    icoEntriesCount(dataURL),
    3,
    "Should keep all the images, downscaling the large one"
  );
});

add_task(async function test_parent_accepts_images() {
  let urls = await setIconFromImages([createImage(1), createImage(2)]);
  Assert.equal(urls.length, 1, "Should set the tab icon");
  Assert.equal(icoEntriesCount(urls[0]), 2, "Should keep both images");
});

add_task(async function test_parent_too_many_images() {
  let images = Array.from({ length: MAX_ICO_IMAGE_COUNT + 1 }, () =>
    createImage()
  );
  let urls = await setIconFromImages(images);
  Assert.deepEqual(urls, [], "Should not set the tab icon");
});

add_task(async function test_parent_image_too_large() {
  let image = createImage();
  image.displayWidth = MAX_ICON_SIZE + 1;
  let urls = await setIconFromImages([createImage(), image]);
  Assert.deepEqual(urls, [], "Should not set the tab icon");
});

add_task(async function test_parent_total_area_too_large() {
  let images = [createImage(), createImage()];
  for (let image of images) {
    image.displayWidth = image.displayHeight = MAX_ICON_SIZE;
  }
  let urls = await setIconFromImages(images);
  Assert.deepEqual(urls, [], "Should not set the tab icon");
});

add_task(async function test_parent_invalid_images() {
  let zeroSized = createImage();
  zeroSized.displayWidth = 0;
  for (let images of [[createImage(), zeroSized], [createImage(), null], []]) {
    let urls = await setIconFromImages(images);
    Assert.deepEqual(urls, [], "Should not set the tab icon");
  }
});
