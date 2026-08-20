/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  BackgroundPageThumbs: "resource://gre/modules/BackgroundPageThumbs.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PageThumbsStorage: "resource://gre/modules/PageThumbs.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "PageThumbsStorageService",
  "@mozilla.org/thumbnails/pagethumbs-service;1",
  Ci.nsIPageThumbsStorageService
);

var oldEnabledPref = Services.prefs.getBoolPref(
  "browser.pagethumbnails.capturing_disabled"
);
Services.prefs.setBoolPref("browser.pagethumbnails.capturing_disabled", false);

registerCleanupFunction(function () {
  while (gBrowser.tabs.length > 1) {
    gBrowser.removeTab(gBrowser.tabs[1]);
  }
  Services.prefs.setBoolPref(
    "browser.pagethumbnails.capturing_disabled",
    oldEnabledPref
  );
});

/**
 * Captures a screenshot for the currently selected tab, stores it in the cache,
 * retrieves it from the cache and compares pixel color values.
 *
 * @param aRed The red component's intensity.
 * @param aGreen The green component's intensity.
 * @param aBlue The blue component's intensity.
 * @param aMessage The info message to print when comparing the pixel color.
 */
async function captureAndCheckColor(aRed, aGreen, aBlue, aMessage) {
  let browser = gBrowser.selectedBrowser;
  // We'll get oranges if the expiration filter removes the file during the
  // test.
  dontExpireThumbnailURLs([browser.currentURI.spec]);

  // Capture the screenshot.
  await PageThumbs.captureAndStore(browser);
  let [r, g, b] = await retrieveImageDataForURL(browser.currentURI.spec);
  is("" + [r, g, b], "" + [aRed, aGreen, aBlue], aMessage);
}

/**
 * For a given URL, loads the corresponding thumbnail
 * to a canvas and passes its image data to the callback.
 * Note, not compat with e10s!
 *
 * @param aURL The url associated with the thumbnail.
 * @returns Promise
 */
async function retrieveImageDataForURL(aURL) {
  let width = 100,
    height = 100;
  let thumb = PageThumbs.getThumbnailURL(aURL, width, height);

  let htmlns = "http://www.w3.org/1999/xhtml";
  let img = document.createElementNS(htmlns, "img");
  img.setAttribute("src", thumb);
  await BrowserTestUtils.waitForEvent(img, "load", true);

  let canvas = document.createElementNS(htmlns, "canvas");
  canvas.setAttribute("width", width);
  canvas.setAttribute("height", height);

  // Draw the image to a canvas and compare the pixel color values.
  let ctx = canvas.getContext("2d");
  ctx.drawImage(img, 0, 0, width, height);
  return ctx.getImageData(0, 0, 100, 100).data;
}

/**
 * Returns the file of the thumbnail with the given URL.
 *
 * @param aURL The URL of the thumbnail.
 */
function thumbnailFile(aURL) {
  return new FileUtils.File(PageThumbsStorageService.getFilePathForURL(aURL));
}

/**
 * Checks if a thumbnail for the given URL exists.
 *
 * @param aURL The url associated to the thumbnail.
 */
function thumbnailExists(aURL) {
  let file = thumbnailFile(aURL);
  return file.exists() && file.fileSize;
}

/**
 * Removes the thumbnail for the given URL.
 *
 * @param aURL The URL associated with the thumbnail.
 */
function removeThumbnail(aURL) {
  let file = thumbnailFile(aURL);
  file.remove(false);
}

/**
 * Calls addVisits, and then forces the newtab module to repopulate its links.
 * See addVisits for parameter descriptions.
 */
async function promiseAddVisitsAndRepopulateNewTabLinks(aPlaceInfo) {
  await PlacesTestUtils.addVisits(makeURI(aPlaceInfo));
  await new Promise(resolve => {
    NewTabUtils.links.populateCache(resolve, true);
  });
}

/**
 * Resolves a Promise when the thumbnail for a given URL has been found
 * on disk. Keeps trying until the thumbnail has been created.
 *
 * @param aURL The URL of the thumbnail's page.
 * @returns Promise
 */
function whenFileExists(aURL) {
  return TestUtils.waitForCondition(
    () => {
      return thumbnailExists(aURL);
    },
    `Waiting for ${aURL} to exist.`,
    1000,
    50
  );
}

/**
 * Resolves a Promise when the given file has been removed.
 * Keeps trying until the file is removed.
 *
 * @param aFile The file that is being removed
 * @returns Promise
 */
function whenFileRemoved(aFile) {
  return TestUtils.waitForCondition(
    () => {
      return !aFile.exists();
    },
    `Waiting for ${aFile.leafName} to not exist.`,
    1000,
    50
  );
}

/**
 * Makes sure that a given list of URLs is not implicitly expired.
 *
 * @param aURLs The list of URLs that should not be expired.
 */
function dontExpireThumbnailURLs(aURLs) {
  let dontExpireURLs = cb => cb(aURLs);
  PageThumbs.addExpirationFilter(dontExpireURLs);

  registerCleanupFunction(function () {
    PageThumbs.removeExpirationFilter(dontExpireURLs);
  });
}

function bgCapture(aURL, aOptions) {
  return bgCaptureWithMethod("capture", aURL, aOptions);
}

function bgCaptureIfMissing(aURL, aOptions) {
  return bgCaptureWithMethod("captureIfMissing", aURL, aOptions);
}

/**
 * Queues a BackgroundPageThumbs capture with the supplied method.
 *
 * @param {string} aMethodName One of the method names on BackgroundPageThumbs
 * for capturing thumbnails. Example: "capture", "captureIfMissing".
 * @param {string} aURL The URL of the page to capture.
 * @param {object} aOptions The options object to pass to BackgroundPageThumbs.
 *
 * @returns {Promise<Array>}
 *   Resolves once the capture has completed with an Array of results. The
 *   first element of the Array is the URL of the captured page, and the second
 *   element is the completion reason from the BackgroundPageThumbs module.
 */
function bgCaptureWithMethod(aMethodName, aURL, aOptions = {}) {
  // We'll get oranges if the expiration filter removes the file during the
  // test.
  dontExpireThumbnailURLs([aURL]);

  return new Promise(resolve => {
    let wrappedDoneFn = aOptions.onDone;
    aOptions.onDone = (url, doneReason) => {
      if (wrappedDoneFn) {
        wrappedDoneFn(url, doneReason);
      }
      resolve([url, doneReason]);
    };

    BackgroundPageThumbs[aMethodName](aURL, aOptions);
  });
}

function bgTestPageURL(aOpts = {}) {
  let TEST_PAGE_URL =
    "http://mochi.test:8888/browser/toolkit/components/thumbnails/test/thumbnails_background.sjs";
  return TEST_PAGE_URL + "?" + encodeURIComponent(JSON.stringify(aOpts));
}

function bgAddPageThumbObserver(url) {
  return new Promise((resolve, reject) => {
    function observe(subject, topic, data) {
      if (data === url) {
        switch (topic) {
          case "page-thumbnail:create":
            resolve();
            break;
          case "page-thumbnail:error":
            reject(new Error("page-thumbnail:error"));
            break;
        }
        Services.obs.removeObserver(observe, "page-thumbnail:create");
        Services.obs.removeObserver(observe, "page-thumbnail:error");
      }
    }
    Services.obs.addObserver(observe, "page-thumbnail:create");
    Services.obs.addObserver(observe, "page-thumbnail:error");
  });
}
