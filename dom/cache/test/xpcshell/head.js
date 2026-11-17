/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * All images in schema_15_profile.zip are from https://github.com/mdn/sw-test/
 * and are CC licensed by https://www.flickr.com/photos/legofenris/.
 */

// testSteps is expected to be defined by the file including this file.
/* global testSteps */

const NS_APP_USER_PROFILE_50_DIR = "ProfD";
const osWindowsName = "WINNT";
const pathDelimiter = "/";

const persistentPersistence = "persistent";
const defaultPersistence = "default";

const storageDirName = "storage";
const persistentPersistenceDirName = "permanent";
const defaultPersistenceDirName = "default";

function cacheClientDirName() {
  return "cache";
}

const { Sqlite } = ChromeUtils.importESModule(
  "resource://gre/modules/Sqlite.sys.mjs"
);

// services required be initialized in order to run CacheStorage
var ss = Services.storage;
var sts = Cc["@mozilla.org/network/stream-transport-service;1"].getService(
  Ci.nsIStreamTransportService
);
var hash = Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);

class RequestError extends Error {
  constructor(resultCode, resultName) {
    super(`Request failed (code: ${resultCode}, name: ${resultName})`);
    this.name = "RequestError";
    this.resultCode = resultCode;
    this.resultName = resultName;
  }
}

add_setup(function () {
  do_get_profile();

  enableTesting();

  Cu.importGlobalProperties(["caches"]);

  registerCleanupFunction(resetTesting);
});

function enableTesting() {
  Services.prefs.setBoolPref("dom.caches.testing.enabled", true);
  Services.prefs.setBoolPref("dom.simpleDB.enabled", true);
  Services.prefs.setBoolPref("dom.quotaManager.testing", true);
}

function resetTesting() {
  Services.prefs.clearUserPref("dom.quotaManager.testing");
  Services.prefs.clearUserPref("dom.simpleDB.enabled");
  Services.prefs.clearUserPref("dom.caches.testing.enabled");
}

function initStorage() {
  return Services.qms.init();
}

function initTemporaryStorage() {
  return Services.qms.initTemporaryStorage();
}

function initPersistentOrigin(principal) {
  return Services.qms.initializePersistentOrigin(principal);
}

function initTemporaryOrigin(principal, createIfNonExistent = true) {
  return Services.qms.initializeTemporaryOrigin(
    "default",
    principal,
    createIfNonExistent
  );
}

function clearOrigin(principal, persistence) {
  let request = Services.qms.clearStoragesForPrincipal(principal, persistence);

  return request;
}

function reset() {
  return Services.qms.reset();
}

async function requestFinished(request) {
  await new Promise(function (resolve) {
    request.callback = function () {
      resolve();
    };
  });

  if (request.resultCode !== Cr.NS_OK) {
    throw new RequestError(request.resultCode, request.resultName);
  }

  return request.result;
}

// Extract a zip file into the profile
function create_test_profile(zipFileName) {
  var directoryService = Services.dirsvc;

  var profileDir = directoryService.get(NS_APP_USER_PROFILE_50_DIR, Ci.nsIFile);
  var currentDir = directoryService.get("CurWorkD", Ci.nsIFile);

  var packageFile = currentDir.clone();
  packageFile.append(zipFileName);

  var zipReader = Cc["@mozilla.org/libjar/zip-reader;1"].createInstance(
    Ci.nsIZipReader
  );
  zipReader.open(packageFile);

  var entryNames = Array.from(zipReader.findEntries(null));
  entryNames.sort();

  for (var entryName of entryNames) {
    var zipentry = zipReader.getEntry(entryName);

    var file = profileDir.clone();
    entryName.split(pathDelimiter).forEach(function (part) {
      file.append(part);
    });

    if (zipentry.isDirectory) {
      file.create(Ci.nsIFile.DIRECTORY_TYPE, parseInt("0755", 8));
    } else {
      var istream = zipReader.getInputStream(entryName);

      var ostream = Cc[
        "@mozilla.org/network/file-output-stream;1"
      ].createInstance(Ci.nsIFileOutputStream);
      ostream.init(file, -1, parseInt("0644", 8), 0);

      var bostream = Cc[
        "@mozilla.org/network/buffered-output-stream;1"
      ].createInstance(Ci.nsIBufferedOutputStream);
      bostream.init(ostream, 32 * 1024);

      bostream.writeFrom(istream, istream.available());

      istream.close();
      bostream.close();
    }
  }

  zipReader.close();
}

function getCacheDir() {
  return getRelativeFile(
    `${storageDirName}/${defaultPersistenceDirName}/chrome/${cacheClientDirName()}`
  );
}

function getMorgueDir() {
  const morgueDir = getCacheDir();
  morgueDir.append("morgue");
  return morgueDir;
}

function openCacheDatabase() {
  const dbFile = getCacheDir();
  dbFile.append("caches.sqlite");
  return Sqlite.openConnection({ path: dbFile.path, readOnly: true });
}

// Returns the total disk usage tracked in the database, read through the given
// Sqlite.sys.mjs connection to caches.sqlite.
async function getTrackedTotalDiskUsage(conn) {
  const [row] = await conn.execute(
    "SELECT total_disk_usage FROM usage_info WHERE id = 1"
  );
  return row.getResultByIndex(0);
}

function getBodyFiles() {
  const files = [];
  for (const dir of getMorgueDir().directoryEntries) {
    let entries;
    try {
      entries = dir.directoryEntries;
    } catch (e) {
      // A race condition can happen if the body files were deleted at the same
      // time: if this is the case, just continue (ignoring files that don't
      // exist anymore).
      if (e.result == Cr.NS_ERROR_FILE_NOT_FOUND) {
        continue;
      }
      throw e;
    }
    for (const file of entries) {
      if (file.leafName.endsWith(".final") || file.leafName.endsWith(".tmp")) {
        files.push(file);
      }
    }
  }
  return files;
}

function getPrincipal(url, attrs) {
  let uri = Services.io.newURI(url);
  if (!attrs) {
    attrs = {};
  }
  return Services.scriptSecurityManager.createContentPrincipal(uri, attrs);
}

function getDefaultPrincipal() {
  return getPrincipal("http://example.com");
}

function getRelativeFile(relativePath) {
  let file = Services.dirsvc
    .get(NS_APP_USER_PROFILE_50_DIR, Ci.nsIFile)
    .clone();

  if (Services.appinfo.OS === osWindowsName) {
    let winFile = file.QueryInterface(Ci.nsILocalFileWin);
    winFile.useDOSDevicePathSyntax = true;
  }

  relativePath.split(pathDelimiter).forEach(function (component) {
    if (component == "..") {
      file = file.parent;
    } else {
      file.append(component);
    }
  });

  return file;
}

function getSimpleDatabase(principal, persistence) {
  let connection = Cc["@mozilla.org/dom/sdb-connection;1"].createInstance(
    Ci.nsISDBConnection
  );

  if (!principal) {
    principal = getDefaultPrincipal();
  }

  connection.init(principal, persistence);

  return connection;
}
