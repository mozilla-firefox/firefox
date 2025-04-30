/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Import common head.
{
  /* import-globals-from ../head_common.js */
  let commonFile = do_get_file("../head_common.js", false);
  let uri = Services.io.newFileURI(commonFile);
  Services.scriptloader.loadSubScript(uri.spec, this);
}

// Put any other stuff relative to this test folder below.

const systemPrincipal = Services.scriptSecurityManager.getSystemPrincipal();

// Used in createFavicon().
let uniqueFaviconId = 0;

/**
 * Checks that the favicon for the given page matches the provided data.
 *
 * @param aPageURI
 *        nsIURI object for the page to check.
 * @param aExpectedMimeType
 *        Expected MIME type of the icon, for example "image/png".
 * @param aExpectedData
 *        Expected icon data, expressed as an array of byte values.
 *        If set null, skip the test for the favicon data.
 */
async function checkFaviconDataForPage(
  aPageURI,
  aExpectedMimeType,
  aExpectedData
) {
  let favicon = await PlacesTestUtils.getFaviconForPage(aPageURI);
  Assert.equal(aExpectedMimeType, favicon.mimeType);
  if (aExpectedData) {
    Assert.ok(compareArrays(aExpectedData, favicon.rawData));
  }
  await check_guid_for_uri(aPageURI);
}

/**
 * Checks that the given page has no associated favicon.
 *
 * @param aPageURI
 *        nsIURI object for the page to check.
 */
async function checkFaviconMissingForPage(aPageURI) {
  let favicon = await PlacesTestUtils.getFaviconForPage(aPageURI);
  Assert.ok(!favicon);
}

function promiseFaviconChanged(aExpectedPageURI, aExpectedFaviconURI) {
  return new Promise(resolve => {
    PlacesTestUtils.waitForNotification("favicon-changed", async events => {
      for (let e of events) {
        if (e.url == aExpectedPageURI.spec) {
          Assert.equal(e.faviconUrl, aExpectedFaviconURI.spec);
          await check_guid_for_uri(aExpectedPageURI, e.pageGuid);
          resolve();
        }
      }
    });
  });
}

/**
 * Create favicon file to temp directory.
 *
 * @param {string} aFileName
 *        File name that will be created in temp directory.
 * @returns {object}
 *          {
 *            file: nsIFile,
 *            uri: nsIURI,
 *            data: byte Array,
 *            mimetype: String,
 *          }
 */
async function createFavicon(aFileName) {
  // Copy the favicon file we have to the specified file in temp directory.
  let originalFaviconFile = do_get_file("favicon-normal16.png");
  let tempDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  let faviconFile = tempDir.clone();
  faviconFile.append(aFileName);
  await IOUtils.copy(originalFaviconFile.path, faviconFile.path);

  // Append some data that sniffers/encoders will ignore that will distinguish
  // the different favicons we'll create.
  let stream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
    Ci.nsIFileOutputStream
  );
  const WRONLY_PERMISSION = 0o600;
  stream.init(
    faviconFile,
    FileUtils.MODE_WRONLY | FileUtils.MODE_APPEND,
    WRONLY_PERMISSION,
    0
  );
  uniqueFaviconId++;
  let uniqueStr = "uid:" + uniqueFaviconId;
  stream.write(uniqueStr, uniqueStr.length);
  stream.close();

  Assert.equal(faviconFile.leafName.substr(0, aFileName.length), aFileName);

  return {
    file: faviconFile,
    uri: uri(faviconFile),
    data: readFileData(faviconFile),
    mimeType: "image/png",
  };
}

/**
 * Create nsIURI for given favicon object.
 *
 * @param {object} aFavicon
 *        Favicon object created by createFavicon().
 * @returns {nsIURI}
 */
async function createDataURLForFavicon(aFavicon) {
  let dataURL = await toDataURL(aFavicon.data, aFavicon.mimeType);
  return uri(dataURL);
}

/**
 * Create data URL string from given byte array and type.
 *
 * @param {Array} data
 *        Byte array.
 * @param {string} type
 *        The type of this data.
 * @returns {string}
 */
function toDataURL(data, type) {
  let blob = new Blob([new Uint8Array(data)], { type });
  return new Promise((resolve, reject) => {
    let reader = new FileReader();
    reader.addEventListener("load", () => resolve(reader.result));
    reader.addEventListener("error", reject);
    reader.readAsDataURL(blob);
  });
}
