"use strict";

var { MigrationUtils } = ChromeUtils.importESModule(
  "resource:///modules/MigrationUtils.sys.mjs"
);
var { LoginHelper } = ChromeUtils.importESModule(
  "resource://gre/modules/LoginHelper.sys.mjs"
);
var { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
var { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);
var { Preferences } = ChromeUtils.importESModule(
  "resource://gre/modules/Preferences.sys.mjs"
);
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
var { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
var { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
});

// Initialize profile.
var gProfD = do_get_profile();

var { getAppInfo, newAppInfo, updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
updateAppInfo();

/**
 * Migrates the requested resource and waits for the migration to be complete.
 *
 * @param {MigratorBase} migrator
 *   The migrator being used to migrate the data.
 * @param {number} resourceType
 *   This is a bitfield with bits from nsIBrowserProfileMigrator flipped to indicate what
 *   resources should be migrated.
 * @param {object|string|null} [aProfile=null]
 *   The profile to be migrated. If set to null, the default profile will be
 *   migrated.
 * @param {boolean} succeeds
 *   True if this migration is expected to succeed.
 * @returns {Promise<Array<string[]>>}
 *   An array of the results from each nsIObserver topics being observed to
 *   verify if the migration succeeded or failed. Those results are 2-element
 *   arrays of [subject, data].
 */
async function promiseMigration(
  migrator,
  resourceType,
  aProfile = null,
  succeeds = null
) {
  // Ensure resource migration is available.
  let availableSources = await migrator.getMigrateData(aProfile);
  Assert.ok(
    (availableSources & resourceType) > 0,
    "Resource supported by migrator"
  );
  let promises = [TestUtils.topicObserved("Migration:Ended")];

  if (succeeds !== null) {
    // Check that the specific resource type succeeded
    promises.push(
      TestUtils.topicObserved(
        succeeds ? "Migration:ItemAfterMigrate" : "Migration:ItemError",
        (_, data) => data == resourceType
      )
    );
  }

  // Start the migration.
  migrator.migrate(resourceType, null, aProfile);

  return Promise.all(promises);
}

/**
 * Takes an array of page URIs and checks that the favicon was imported for each page URI
 *
 * @param {Array} pageURIs An array of page URIs
 */
async function assertFavicons(pageURIs) {
  for (let uri of pageURIs) {
    let favicon = await PlacesUtils.favicons.getFaviconForPage(uri);
    Assert.ok(favicon, `Got favicon for ${favicon.uri.spec}`);
  }
}

/**
 * Check the image data for favicon of given page uri.
 *
 * @param {string} pageURI
 *                 The page URI to which the favicon belongs.
 * @param {Array} expectedImageData
 *                 Expected image data of the favicon.
 * @param {string} expectedMimeType
 *                 Expected mime type of the favicon.
 */
async function assertFavicon(pageURI, expectedImageData, expectedMimeType) {
  let result = await PlacesUtils.favicons.getFaviconForPage(
    Services.io.newURI(pageURI)
  );
  Assert.ok(!!result, `Got favicon for ${pageURI}`);
  Assert.equal(
    result.rawData.join(","),
    expectedImageData.join(","),
    "Image data is correct"
  );
  Assert.equal(result.mimeType, expectedMimeType, "Mime type is correct");
}

/**
 * Replaces a directory service entry with a given nsIFile.
 *
 * @param {string} key
 *   The nsIDirectoryService directory key to register a fake path for.
 *   For example: "AppData", "ULibDir".
 * @param {nsIFile} file
 *   The nsIFile to map the key to. Note that this nsIFile should represent
 *   a directory and not an individual file.
 * @see nsDirectoryServiceDefs.h for the list of directories that can be
 *   overridden.
 */
function registerFakePath(key, file) {
  let dirsvc = Services.dirsvc.QueryInterface(Ci.nsIProperties);
  let originalFile;
  try {
    // If a file is already provided save it and undefine, otherwise set will
    // throw for persistent entries (ones that are cached).
    originalFile = dirsvc.get(key, Ci.nsIFile);
    dirsvc.undefine(key);
  } catch (e) {
    // dirsvc.get will throw if nothing provides for the key and dirsvc.undefine
    // will throw if it's not a persistent entry, in either case we don't want
    // to set the original file in cleanup.
    originalFile = undefined;
  }

  dirsvc.set(key, file);
  registerCleanupFunction(() => {
    dirsvc.undefine(key);
    if (originalFile) {
      dirsvc.set(key, originalFile);
    }
  });
}

function getRootPath() {
  let dirKey;
  if (AppConstants.platform == "win") {
    dirKey = "LocalAppData";
  } else if (AppConstants.platform == "macosx") {
    dirKey = "ULibDir";
  } else {
    dirKey = "Home";
  }
  return Services.dirsvc.get(dirKey, Ci.nsIFile).path;
}

/**
 * Returns a PRTime value for the current date minus daysAgo number
 * of days.
 *
 * @param {number} daysAgo
 *   How many days in the past from now the returned date should be.
 * @returns {number}
 */
function PRTimeDaysAgo(daysAgo) {
  return PlacesUtils.toPRTime(Date.now() - daysAgo * 24 * 60 * 60 * 1000);
}

/**
 * Returns a Date value for the current date minus daysAgo number
 * of days.
 *
 * @param {number} daysAgo
 *   How many days in the past from now the returned date should be.
 * @returns {Date}
 */
function dateDaysAgo(daysAgo) {
  return new Date(Date.now() - daysAgo * 24 * 60 * 60 * 1000);
}

/**
 * Constructs and returns a data structure consistent with the Chrome
 * browsers bookmarks storage. This data structure can then be serialized
 * to JSON and written to disk to simulate a Chrome browser's bookmarks
 * database.
 *
 * @param {number} [totalBookmarks=100]
 *   How many bookmarks to create.
 * @returns {object}
 */
function createChromeBookmarkStructure(totalBookmarks = 100) {
  let bookmarksData = {
    roots: {
      bookmark_bar: { children: [] },
      other: { children: [] },
      synced: { children: [] },
    },
  };
  const MAX_BMS = totalBookmarks;
  let barKids = bookmarksData.roots.bookmark_bar.children;
  let menuKids = bookmarksData.roots.other.children;
  let syncedKids = bookmarksData.roots.synced.children;
  let currentMenuKids = menuKids;
  let currentBarKids = barKids;
  let currentSyncedKids = syncedKids;
  for (let i = 0; i < MAX_BMS; i++) {
    currentBarKids.push({
      url: "https://www.chrome-bookmark-bar-bookmark" + i + ".com",
      name: "bookmark " + i,
      type: "url",
    });
    currentMenuKids.push({
      url: "https://www.chrome-menu-bookmark" + i + ".com",
      name: "bookmark for menu " + i,
      type: "url",
    });
    currentSyncedKids.push({
      url: "https://www.chrome-synced-bookmark" + i + ".com",
      name: "bookmark for synced " + i,
      type: "url",
    });
    if (i % 20 == 19) {
      let nextFolder = {
        name: "toolbar folder " + Math.ceil(i / 20),
        type: "folder",
        children: [],
      };
      currentBarKids.push(nextFolder);
      currentBarKids = nextFolder.children;

      nextFolder = {
        name: "menu folder " + Math.ceil(i / 20),
        type: "folder",
        children: [],
      };
      currentMenuKids.push(nextFolder);
      currentMenuKids = nextFolder.children;

      nextFolder = {
        name: "synced folder " + Math.ceil(i / 20),
        type: "folder",
        children: [],
      };
      currentSyncedKids.push(nextFolder);
      currentSyncedKids = nextFolder.children;
    }
  }
  return bookmarksData;
}
