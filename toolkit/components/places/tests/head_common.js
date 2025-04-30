/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const NS_APP_USER_PROFILE_50_DIR = "ProfD";

// Shortcuts to transitions type.
const TRANSITION_LINK = Ci.nsINavHistoryService.TRANSITION_LINK;
const TRANSITION_TYPED = Ci.nsINavHistoryService.TRANSITION_TYPED;
const TRANSITION_BOOKMARK = Ci.nsINavHistoryService.TRANSITION_BOOKMARK;
const TRANSITION_EMBED = Ci.nsINavHistoryService.TRANSITION_EMBED;
const TRANSITION_FRAMED_LINK = Ci.nsINavHistoryService.TRANSITION_FRAMED_LINK;
const TRANSITION_REDIRECT_PERMANENT =
  Ci.nsINavHistoryService.TRANSITION_REDIRECT_PERMANENT;
const TRANSITION_REDIRECT_TEMPORARY =
  Ci.nsINavHistoryService.TRANSITION_REDIRECT_TEMPORARY;
const TRANSITION_DOWNLOAD = Ci.nsINavHistoryService.TRANSITION_DOWNLOAD;
const TRANSITION_RELOAD = Ci.nsINavHistoryService.TRANSITION_RELOAD;

const TITLE_LENGTH_MAX = 4096;

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var { PlacesSyncUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSyncUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  BookmarkHTMLUtils: "resource://gre/modules/BookmarkHTMLUtils.sys.mjs",
  BookmarkJSONUtils: "resource://gre/modules/BookmarkJSONUtils.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  ObjectUtils: "resource://gre/modules/ObjectUtils.sys.mjs",
  PlacesBackups: "resource://gre/modules/PlacesBackups.sys.mjs",
  PlacesDBUtils: "resource://gre/modules/PlacesDBUtils.sys.mjs",
  PlacesTestUtils: "resource://testing-common/PlacesTestUtils.sys.mjs",
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "SMALLPNG_DATA_URI", function () {
  return NetUtil.newURI(
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAA" +
      "AAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg=="
  );
});
const SMALLPNG_DATA_LEN = 67;

ChromeUtils.defineLazyGetter(this, "SMALLSVG_DATA_URI", function () {
  return NetUtil.newURI(
    "data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy5" +
      "3My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMDAgMTAwIiBmaWxs" +
      "PSIjNDI0ZTVhIj4NCiAgPGNpcmNsZSBjeD0iNTAiIGN5PSI1MCIgcj0iN" +
      "DQiIHN0cm9rZT0iIzQyNGU1YSIgc3Ryb2tlLXdpZHRoPSIxMSIgZmlsbD" +
      "0ibm9uZSIvPg0KICA8Y2lyY2xlIGN4PSI1MCIgY3k9IjI0LjYiIHI9IjY" +
      "uNCIvPg0KICA8cmVjdCB4PSI0NSIgeT0iMzkuOSIgd2lkdGg9IjEwLjEi" +
      "IGhlaWdodD0iNDEuOCIvPg0KPC9zdmc%2BDQo%3D"
  );
});

ChromeUtils.defineLazyGetter(this, "PlacesFrecencyRecalculator", () => {
  return Cc["@mozilla.org/places/frecency-recalculator;1"].getService(
    Ci.nsIObserver
  ).wrappedJSObject;
});

var gTestDir = do_get_cwd();

// Initialize profile.
var gProfD = do_get_profile(true);

// Remove any old database.
clearDB();

/**
 * Shortcut to create a nsIURI.
 *
 * @param aSpec
 *        URLString of the uri.
 */
function uri(aSpec) {
  return NetUtil.newURI(aSpec);
}

/**
 * Gets the database connection.  If the Places connection is invalid it will
 * try to create a new connection.
 *
 * @param [optional] aForceNewConnection
 *        Forces creation of a new connection to the database.  When a
 *        connection is asyncClosed it cannot anymore schedule async statements,
 *        though connectionReady will keep returning true (Bug 726990).
 *
 * @return The database connection or null if unable to get one.
 */
var gDBConn;
function DBConn(aForceNewConnection) {
  if (!aForceNewConnection) {
    let db = PlacesUtils.history.DBConnection;
    if (db.connectionReady) {
      return db;
    }
  }

  // If the Places database connection has been closed, create a new connection.
  if (!gDBConn || aForceNewConnection) {
    let file = Services.dirsvc.get("ProfD", Ci.nsIFile);
    file.append("places.sqlite");
    let dbConn = (gDBConn = Services.storage.openDatabase(file));

    // Be sure to cleanly close this connection.
    promiseTopicObserved("profile-before-change").then(() =>
      dbConn.asyncClose()
    );
  }

  return gDBConn.connectionReady ? gDBConn : null;
}

/**
 * Reads data from the provided inputstream.
 *
 * @return an array of bytes.
 */
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

/**
 * Reads the data from the specified nsIFile.
 *
 * @param aFile
 *        The nsIFile to read from.
 * @return an array of bytes.
 */
function readFileData(aFile) {
  let inputStream = Cc[
    "@mozilla.org/network/file-input-stream;1"
  ].createInstance(Ci.nsIFileInputStream);
  // init the stream as RD_ONLY, -1 == default permissions.
  inputStream.init(aFile, 0x01, -1, null);

  // Check the returned size versus the expected size.
  let size = inputStream.available();
  let bytes = readInputStreamData(inputStream);
  if (size != bytes.length) {
    throw new Error("Didn't read expected number of bytes");
  }
  return bytes;
}
/**
 * Reads the data from the named file, verifying the expected file length.
 *
 * @param aFileName
 *        This file should be located in the same folder as the test.
 * @param aExpectedLength
 *        Expected length of the file.
 *
 * @return The array of bytes read from the file.
 */
function readFileOfLength(aFileName, aExpectedLength) {
  let data = readFileData(do_get_file(aFileName));
  Assert.equal(data.length, aExpectedLength);
  return data;
}

/**
 * Reads the data from the specified nsIFile, then returns it as data URL.
 *
 * @param file
 *        The nsIFile to read from.
 * @param mimeType
 *        The mime type of the file content.
 * @return Promise that retunes data URL.
 */
async function readFileDataAsDataURL(file, mimeType) {
  const data = readFileData(file);
  return PlacesTestUtils.fileDataToDataURL(data, mimeType);
}

/**
 * Returns the base64-encoded version of the given string.  This function is
 * similar to window.btoa, but is available to xpcshell tests also.
 *
 * @param aString
 *        Each character in this string corresponds to a byte, and must be a
 *        code point in the range 0-255.
 *
 * @return The base64-encoded string.
 */
function base64EncodeString(aString) {
  var stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stream.setByteStringData(aString);
  var encoder = Cc["@mozilla.org/scriptablebase64encoder;1"].createInstance(
    Ci.nsIScriptableBase64Encoder
  );
  return encoder.encodeToString(stream, aString.length);
}

/**
 * Compares two arrays, and returns true if they are equal.
 *
 * @param aArray1
 *        First array to compare.
 * @param aArray2
 *        Second array to compare.
 */
function compareArrays(aArray1, aArray2) {
  if (aArray1.length != aArray2.length) {
    print("compareArrays: array lengths differ\n");
    return false;
  }

  for (let i = 0; i < aArray1.length; i++) {
    if (aArray1[i] != aArray2[i]) {
      print(
        "compareArrays: arrays differ at index " +
          i +
          ": " +
          "(" +
          aArray1[i] +
          ") != (" +
          aArray2[i] +
          ")\n"
      );
      return false;
    }
  }

  return true;
}

/**
 * Deletes a previously created sqlite file from the profile folder.
 */
function clearDB() {
  try {
    let file = Services.dirsvc.get("ProfD", Ci.nsIFile);
    file.append("places.sqlite");
    if (file.exists()) {
      file.remove(false);
    }
  } catch (ex) {
    dump("Exception: " + ex);
  }
}

/**
 * Dumps the rows of a table out to the console.
 *
 * @param aName
 *        The name of the table or view to output.
 */
function dump_table(aName, dbConn) {
  if (!dbConn) {
    dbConn = DBConn();
  }
  let stmt = dbConn.createStatement("SELECT * FROM " + aName);

  print("\n*** Printing data from " + aName);
  let count = 0;
  while (stmt.executeStep()) {
    let columns = stmt.numEntries;

    if (count == 0) {
      // Print the column names.
      for (let i = 0; i < columns; i++) {
        dump(stmt.getColumnName(i) + "\t");
      }
      dump("\n");
    }

    // Print the rows.
    for (let i = 0; i < columns; i++) {
      switch (stmt.getTypeOfIndex(i)) {
        case Ci.mozIStorageValueArray.VALUE_TYPE_NULL:
          dump("NULL\t");
          break;
        case Ci.mozIStorageValueArray.VALUE_TYPE_INTEGER:
          dump(stmt.getInt64(i) + "\t");
          break;
        case Ci.mozIStorageValueArray.VALUE_TYPE_FLOAT:
          dump(stmt.getDouble(i) + "\t");
          break;
        case Ci.mozIStorageValueArray.VALUE_TYPE_TEXT:
          dump(stmt.getString(i) + "\t");
          break;
      }
    }
    dump("\n");

    count++;
  }
  print("*** There were a total of " + count + " rows of data.\n");

  stmt.finalize();
}

/**
 * Checks if an address is found in the database.
 * @param aURI
 *        nsIURI or address to look for.
 * @return place id of the page or 0 if not found
 */
function page_in_database(aURI) {
  let url = aURI instanceof Ci.nsIURI ? aURI.spec : aURI;
  let stmt = DBConn().createStatement(
    "SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url"
  );
  stmt.params.url = url;
  try {
    if (!stmt.executeStep()) {
      return 0;
    }
    return stmt.getInt64(0);
  } finally {
    stmt.finalize();
  }
}

/**
 * Checks how many visits exist for a specified page.
 * @param aURI
 *        nsIURI or address to look for.
 * @return number of visits found.
 */
function visits_in_database(aURI) {
  let url = aURI instanceof Ci.nsIURI ? aURI.spec : aURI;
  let stmt = DBConn().createStatement(
    `SELECT count(*) FROM moz_historyvisits v
     JOIN moz_places h ON h.id = v.place_id
     WHERE url_hash = hash(:url) AND url = :url`
  );
  stmt.params.url = url;
  try {
    if (!stmt.executeStep()) {
      return 0;
    }
    return stmt.getInt64(0);
  } finally {
    stmt.finalize();
  }
}

/**
 * Allows waiting for an observer notification once.
 *
 * @param aTopic
 *        Notification topic to observe.
 *
 * @return {Promise}
 * @resolves The array [aSubject, aData] from the observed notification.
 * @rejects Never.
 */
function promiseTopicObserved(aTopic) {
  return new Promise(resolve => {
    Services.obs.addObserver(function observe(
      aObsSubject,
      aObsTopic,
      aObsData
    ) {
      Services.obs.removeObserver(observe, aObsTopic);
      resolve([aObsSubject, aObsData]);
    }, aTopic);
  });
}

/**
 * Simulates a Places shutdown.
 */
var shutdownPlaces = function () {
  info("shutdownPlaces: starting");
  let promise = new Promise(resolve => {
    Services.obs.addObserver(resolve, "places-connection-closed");
  });
  let hs = PlacesUtils.history.QueryInterface(Ci.nsIObserver);
  hs.observe(null, "profile-change-teardown", null);
  info("shutdownPlaces: sent profile-change-teardown");
  hs.observe(null, "test-simulate-places-shutdown", null);
  info("shutdownPlaces: sent test-simulate-places-shutdown");
  return promise.then(() => {
    info("shutdownPlaces: complete");
  });
};

const FILENAME_BOOKMARKS_HTML = "bookmarks.html";
const FILENAME_BOOKMARKS_JSON =
  "bookmarks-" + PlacesBackups.toISODateString(new Date()) + ".json";

/**
 * Creates a bookmarks.html file in the profile folder from a given source file.
 *
 * @param aFilename
 *        Name of the file to copy to the profile folder.  This file must
 *        exist in the directory that contains the test files.
 *
 * @return nsIFile object for the file.
 */
function create_bookmarks_html(aFilename) {
  if (!aFilename) {
    do_throw("you must pass a filename to create_bookmarks_html function");
  }
  remove_bookmarks_html();
  let bookmarksHTMLFile = gTestDir.clone();
  bookmarksHTMLFile.append(aFilename);
  Assert.ok(bookmarksHTMLFile.exists());
  bookmarksHTMLFile.copyTo(gProfD, FILENAME_BOOKMARKS_HTML);
  let profileBookmarksHTMLFile = gProfD.clone();
  profileBookmarksHTMLFile.append(FILENAME_BOOKMARKS_HTML);
  Assert.ok(profileBookmarksHTMLFile.exists());
  return profileBookmarksHTMLFile;
}

/**
 * Remove bookmarks.html file from the profile folder.
 */
function remove_bookmarks_html() {
  let profileBookmarksHTMLFile = gProfD.clone();
  profileBookmarksHTMLFile.append(FILENAME_BOOKMARKS_HTML);
  if (profileBookmarksHTMLFile.exists()) {
    profileBookmarksHTMLFile.remove(false);
    Assert.ok(!profileBookmarksHTMLFile.exists());
  }
}

/**
 * Check bookmarks.html file exists in the profile folder.
 *
 * @return nsIFile object for the file.
 */
function check_bookmarks_html() {
  let profileBookmarksHTMLFile = gProfD.clone();
  profileBookmarksHTMLFile.append(FILENAME_BOOKMARKS_HTML);
  Assert.ok(profileBookmarksHTMLFile.exists());
  return profileBookmarksHTMLFile;
}

/**
 * Creates a JSON backup in the profile folder folder from a given source file.
 *
 * @param aFilename
 *        Name of the file to copy to the profile folder.  This file must
 *        exist in the directory that contains the test files.
 *
 * @return nsIFile object for the file.
 */
function create_JSON_backup(aFilename) {
  if (!aFilename) {
    do_throw("you must pass a filename to create_JSON_backup function");
  }
  let bookmarksBackupDir = gProfD.clone();
  bookmarksBackupDir.append("bookmarkbackups");
  if (!bookmarksBackupDir.exists()) {
    bookmarksBackupDir.create(Ci.nsIFile.DIRECTORY_TYPE, parseInt("0755", 8));
    Assert.ok(bookmarksBackupDir.exists());
  }
  let profileBookmarksJSONFile = bookmarksBackupDir.clone();
  profileBookmarksJSONFile.append(FILENAME_BOOKMARKS_JSON);
  if (profileBookmarksJSONFile.exists()) {
    profileBookmarksJSONFile.remove();
  }
  let bookmarksJSONFile = gTestDir.clone();
  bookmarksJSONFile.append(aFilename);
  Assert.ok(bookmarksJSONFile.exists());
  bookmarksJSONFile.copyTo(bookmarksBackupDir, FILENAME_BOOKMARKS_JSON);
  profileBookmarksJSONFile = bookmarksBackupDir.clone();
  profileBookmarksJSONFile.append(FILENAME_BOOKMARKS_JSON);
  Assert.ok(profileBookmarksJSONFile.exists());
  return profileBookmarksJSONFile;
}

/**
 * Remove bookmarksbackup dir and all backups from the profile folder.
 */
function remove_all_JSON_backups() {
  let bookmarksBackupDir = gProfD.clone();
  bookmarksBackupDir.append("bookmarkbackups");
  if (bookmarksBackupDir.exists()) {
    bookmarksBackupDir.remove(true);
    Assert.ok(!bookmarksBackupDir.exists());
  }
}

/**
 * Check a JSON backup file for today exists in the profile folder.
 *
 * @param aIsAutomaticBackup The boolean indicates whether it's an automatic
 *        backup.
 * @return nsIFile object for the file.
 */
function check_JSON_backup(aIsAutomaticBackup) {
  let profileBookmarksJSONFile;
  if (aIsAutomaticBackup) {
    let bookmarksBackupDir = gProfD.clone();
    bookmarksBackupDir.append("bookmarkbackups");
    let files = bookmarksBackupDir.directoryEntries;
    while (files.hasMoreElements()) {
      let entry = files.nextFile;
      if (PlacesBackups.filenamesRegex.test(entry.leafName)) {
        profileBookmarksJSONFile = entry;
        break;
      }
    }
  } else {
    profileBookmarksJSONFile = gProfD.clone();
    profileBookmarksJSONFile.append("bookmarkbackups");
    profileBookmarksJSONFile.append(FILENAME_BOOKMARKS_JSON);
  }
  Assert.ok(profileBookmarksJSONFile.exists());
  return profileBookmarksJSONFile;
}

/**
 * Returns the hidden status of a url.
 *
 * @param aURI
 *        The URI or spec to get hidden for.
 * @return @return true if the url is hidden, false otherwise.
 */
function isUrlHidden(aURI) {
  let url = aURI instanceof Ci.nsIURI ? aURI.spec : aURI;
  let stmt = DBConn().createStatement(
    "SELECT hidden FROM moz_places WHERE url_hash = hash(?1) AND url = ?1"
  );
  stmt.bindByIndex(0, url);
  if (!stmt.executeStep()) {
    throw new Error("No result for hidden.");
  }
  let hidden = stmt.getInt32(0);
  stmt.finalize();

  return !!hidden;
}

/**
 * Compares two times in usecs, considering eventual platform timers skews.
 *
 * @param aTimeBefore
 *        The older time in usecs.
 * @param aTimeAfter
 *        The newer time in usecs.
 * @return true if times are ordered, false otherwise.
 */
function is_time_ordered(before, after) {
  // Windows has an estimated 16ms timers precision, since Date.now() and
  // PR_Now() use different code atm, the results can be unordered by this
  // amount of time.  See bug 558745 and bug 557406.
  let isWindows = "@mozilla.org/windows-registry-key;1" in Cc;
  // Just to be safe we consider 20ms.
  let skew = isWindows ? 20000000 : 0;
  return after - before > -skew;
}

/**
 * Shutdowns Places, invoking the callback when the connection has been closed.
 *
 * @param aCallback
 *        Function to be called when done.
 */
function waitForConnectionClosed(aCallback) {
  promiseTopicObserved("places-connection-closed").then(aCallback);
  shutdownPlaces();
}

/**
 * Tests if a given guid is valid for use in Places or not.
 *
 * @param aGuid
 *        The guid to test.
 * @param [optional] aStack
 *        The stack frame used to report the error.
 */
function do_check_valid_places_guid(aGuid) {
  Assert.ok(/^[a-zA-Z0-9\-_]{12}$/.test(aGuid), "Should be a valid GUID");
}

/**
 * Tests that a guid was set in moz_places for a given uri.
 *
 * @param aURI
 *        The uri to check.
 * @param [optional] aGUID
 *        The expected guid in the database.
 */
async function check_guid_for_uri(aURI, aGUID) {
  let guid = await PlacesTestUtils.getDatabaseValue("moz_places", "guid", {
    url: aURI,
  });
  if (aGUID) {
    do_check_valid_places_guid(aGUID);
    Assert.equal(guid, aGUID, "Should have a guid in moz_places for the URI");
  }
}

/**
 * Tests that a guid was set in moz_places for a given bookmark.
 *
 * @param aId
 *        The bookmark id to check.
 * @param [optional] aGUID
 *        The expected guid in the database.
 */
async function check_guid_for_bookmark(aId, aGUID) {
  let guid = await PlacesTestUtils.getDatabaseValue("moz_bookmarks", "guid", {
    id: aId,
  });
  if (aGUID) {
    do_check_valid_places_guid(aGUID);
    Assert.equal(guid, aGUID, "Should have the correct GUID for the bookmark");
  }
}

/**
 * Compares 2 arrays returning whether they contains the same elements.
 *
 * @param a1
 *        First array to compare.
 * @param a2
 *        Second array to compare.
 * @param [optional] sorted
 *        Whether the comparison should take in count position of the elements.
 * @return true if the arrays contain the same elements, false otherwise.
 */
function do_compare_arrays(a1, a2, sorted) {
  if (a1.length != a2.length) {
    return false;
  }

  if (sorted) {
    return a1.every((e, i) => e == a2[i]);
  }
  return (
    !a1.filter(e => !a2.includes(e)).length &&
    !a2.filter(e => !a1.includes(e)).length
  );
}

/**
 * Generic nsINavHistoryResultObserver that doesn't implement anything, but
 * provides dummy methods to prevent errors about an object not having a certain
 * method.
 */
function NavHistoryResultObserver() {}

NavHistoryResultObserver.prototype = {
  batching() {},
  containerStateChanged() {},
  invalidateContainer() {},
  nodeDateAddedChanged() {},
  nodeHistoryDetailsChanged() {},
  nodeIconChanged() {},
  nodeInserted() {},
  nodeKeywordChanged() {},
  nodeLastModifiedChanged() {},
  nodeMoved() {},
  nodeRemoved() {},
  nodeTagsChanged() {},
  nodeTitleChanged() {},
  nodeURIChanged() {},
  sortingChanged() {},
  QueryInterface: ChromeUtils.generateQI(["nsINavHistoryResultObserver"]),
};

function checkBookmarkObject(info) {
  do_check_valid_places_guid(info.guid);
  do_check_valid_places_guid(info.parentGuid);
  Assert.ok(typeof info.index == "number", "index should be a number");
  Assert.ok(
    info.dateAdded.constructor.name == "Date",
    "dateAdded should be a Date"
  );
  Assert.ok(
    info.lastModified.constructor.name == "Date",
    "lastModified should be a Date"
  );
  Assert.ok(
    info.lastModified >= info.dateAdded,
    "lastModified should never be smaller than dateAdded"
  );
  Assert.ok(typeof info.type == "number", "type should be a number");
}

/**
 * Reads foreign_count value for a given url.
 */
async function foreign_count(url) {
  if (url instanceof Ci.nsIURI) {
    url = url.spec;
  }
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.executeCached(
    `SELECT foreign_count FROM moz_places
     WHERE url_hash = hash(:url) AND url = :url
    `,
    { url }
  );
  return !rows.length ? 0 : rows[0].getResultByName("foreign_count");
}

function compareAscending(a, b) {
  if (a > b) {
    return 1;
  }
  if (a < b) {
    return -1;
  }
  return 0;
}

function sortBy(array, prop) {
  return array.sort((a, b) => compareAscending(a[prop], b[prop]));
}

/**
 * Asynchronously compares contents from 2 favicon urls.
 */
async function compareFavicons(icon1, icon2, msg) {
  icon1 = new URL(icon1 instanceof Ci.nsIURI ? icon1.spec : icon1);
  icon2 = new URL(icon2 instanceof Ci.nsIURI ? icon2.spec : icon2);

  function getIconData(icon) {
    return new Promise((resolve, reject) => {
      NetUtil.asyncFetch(
        {
          uri: icon.href,
          loadUsingSystemPrincipal: true,
          contentPolicyType: Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE_FAVICON,
        },
        function (inputStream, status) {
          if (!Components.isSuccessCode(status)) {
            reject();
          }
          let size = inputStream.available();
          resolve(NetUtil.readInputStreamToString(inputStream, size));
        }
      );
    });
  }

  let data1 = await getIconData(icon1);
  Assert.ok(!!data1.length, "Should fetch icon data");
  let data2 = await getIconData(icon2);
  Assert.ok(!!data2.length, "Should fetch icon data");
  Assert.deepEqual(data1, data2, msg);
}

/**
 * Get the internal "root" folder name for an item, specified by its itemGuid.
 * If the itemGuid does not point to a root folder, null is returned.
 *
 * @param itemGuid
 *        the item guid.
 * @return the internal-root name for the root folder, if itemGuid points
 * to such folder, null otherwise.
 */
function mapItemGuidToInternalRootName(itemGuid) {
  switch (itemGuid) {
    case PlacesUtils.bookmarks.rootGuid:
      return "placesRoot";
    case PlacesUtils.bookmarks.menuGuid:
      return "bookmarksMenuFolder";
    case PlacesUtils.bookmarks.toolbarGuid:
      return "toolbarFolder";
    case PlacesUtils.bookmarks.unfiledGuid:
      return "unfiledBookmarksFolder";
    case PlacesUtils.bookmarks.mobileGuid:
      return "mobileFolder";
  }
  return null;
}

const DB_FILENAME = "places.sqlite";

/**
 * Sets the database to use for the given test.  This should be the very first
 * thing in the test, otherwise this database will not be used!
 *
 * @param {string|string[]} path
 *        A filename or path to a database. The database must exist.
 *        If this is a string, then this is assumed to be a filename in the
 *        directory where the test calling this is located.
 *        If this is an array, this is assumed to be a path relative to the
 *        directory that this file, head_common.js, is located.
 * @param {string} destFileName
 *        The destination filename to copy the database to.
 * @return {Promise} the final path to the database
 */
async function setupPlacesDatabase(path, destFileName = DB_FILENAME) {
  let currentDir = do_get_cwd().path;

  if (typeof path == "string") {
    path = [path];
  } else {
    currentDir = PathUtils.parent(currentDir);
  }
  let src = PathUtils.join(currentDir, ...path);
  Assert.ok(await IOUtils.exists(src), "Database file found");

  // Ensure that our database doesn't already exist.
  let dest = PathUtils.join(PathUtils.profileDir, destFileName);
  Assert.ok(
    !(await IOUtils.exists(dest)),
    "Database file should not exist yet"
  );

  await IOUtils.copy(src, dest);
  return dest;
}

/**
 * Gets the URLs of pages that have a particular annotation.
 *
 * @param {String} name The name of the annotation to search for.
 * @return An array of URLs found.
 */
function getPagesWithAnnotation(name) {
  return PlacesUtils.promiseDBConnection().then(async db => {
    let rows = await db.execute(
      `
      SELECT h.url FROM moz_anno_attributes n
      JOIN moz_annos a ON n.id = a.anno_attribute_id
      JOIN moz_places h ON h.id = a.place_id
      WHERE n.name = :name
    `,
      { name }
    );

    return rows.map(row => row.getResultByName("url"));
  });
}

/**
 * Checks there are no orphan page annotations in the database, and no
 * orphan anno attribute names.
 */
async function assertNoOrphanPageAnnotations() {
  let db = await PlacesUtils.promiseDBConnection();

  let rows = await db.execute(`
    SELECT place_id FROM moz_annos
    WHERE place_id NOT IN (SELECT id FROM moz_places)
  `);

  Assert.equal(rows.length, 0, "Should not have any orphan page annotations");

  rows = await db.execute(`
    SELECT id FROM moz_anno_attributes
    WHERE id NOT IN (SELECT anno_attribute_id FROM moz_annos) AND
          id NOT IN (SELECT anno_attribute_id FROM moz_items_annos)`);
}
