/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Shared helpers for the Cache API mochitests that inspect quota usage.

/* global FileUtils */
SpecialPowers.addTaskImport(
  "FileUtils",
  "resource://testing-common/dom/quota/test/modules/FileUtils.sys.mjs"
);

// `caches` is only exposed on an insecure origin if dom.caches.testing.enabled
// is set when the window is created, so the Cache API is used from an iframe
// created after the prefs are pushed.
function setupTestIframe() {
  return new Promise(function (resolve) {
    var iframe = document.createElement("iframe");
    iframe.src = "empty.html";
    iframe.onload = function () {
      window.caches = iframe.contentWindow.caches;
      resolve();
    };
    document.body.appendChild(iframe);
  });
}

function clearStorage() {
  return new Promise(function (resolve) {
    var qms = SpecialPowers.Services.qms;
    var principal = SpecialPowers.wrap(document).nodePrincipal;
    var request = qms.clearStoragesForPrincipal(principal);
    var cb = SpecialPowers.wrapCallback(resolve);
    request.callback = cb;
  });
}

function resetStorage() {
  return new Promise(function (resolve) {
    var qms = SpecialPowers.Services.qms;
    var principal = SpecialPowers.wrap(document).nodePrincipal;
    var request = qms.resetStoragesForPrincipal(principal);
    var cb = SpecialPowers.wrapCallback(resolve);
    request.callback = cb;
  });
}

// Shuts the QuotaManager down. The next storage access initializes it again,
// and dom.quotaManager.loadQuotaFromCache must be false for the origins to be
// initialized from disk rather than from the cached usage in storage.sqlite.
// qms.reset() is only accepted from the parent process.
function resetQuotaManager() {
  return SpecialPowers.spawnChrome([], function () {
    return new Promise(resolve => {
      const request = Services.qms.reset();
      request.callback = () => resolve(request.resultCode);
    });
  });
}

function storageUsage() {
  return new Promise(function (resolve) {
    var qms = SpecialPowers.Services.qms;
    var principal = SpecialPowers.wrap(document).nodePrincipal;
    var cb = SpecialPowers.wrapCallback(function (request) {
      resolve(request.result.usage);
    });
    qms.getUsageForPrincipal(principal, cb);
  });
}

function cachedStorageUsage() {
  return new Promise(function (resolve) {
    var qms = SpecialPowers.Services.qms;
    var principal = SpecialPowers.wrap(document).nodePrincipal;
    var cb = SpecialPowers.wrapCallback(function (request) {
      resolve(request.result);
    });
    var request = qms.getCachedUsageForPrincipal(principal);
    request.callback = cb;
  });
}

function gc() {
  return new Promise(function (resolve) {
    SpecialPowers.exactGC(resolve);
  });
}

function fetchOpaqueResponse(url) {
  return fetch(url, { mode: "no-cors" });
}

// Name of the origin's directory under storage/default, as sanitized by
// QuotaManager.
function originDirectoryName() {
  return window.location.origin.replace("://", "+++").replace(/:/g, "+");
}

// Relative path of the origin's directory cache under storage/default
function originCacheDirPath() {
  return `storage/default/${originDirectoryName()}/cache`;
}

function randomString(bytes) {
  const chunkSize = 65536;
  const parts = [];
  for (let done = 0; done < bytes; done += chunkSize) {
    const buf = new Uint8Array(Math.min(chunkSize, bytes - done));
    crypto.getRandomValues(buf);
    parts.push(String.fromCharCode(...buf.map(b => 33 + (b % 94))));
  }
  return parts.join("");
}

// Whether the Cache API context marker file of the origin exists.
function markerFileExists() {
  return SpecialPowers.spawnChrome(
    [originCacheDirPath()],
    function (originCacheDir) {
      const marker = FileUtils.getFile(originCacheDir);
      marker.append("context_open.marker");
      return marker.exists();
    }
  );
}

// Path and size of the body files (morgue/<subdir>/{uuid}.final) of the origin.
function listBodyFiles() {
  return SpecialPowers.spawnChrome(
    [originCacheDirPath()],
    async function (originCacheDir) {
      // eslint-disable-next-line mozilla/reject-importGlobalProperties
      Cu.importGlobalProperties(["IOUtils", "PathUtils"]);

      const cacheDir = FileUtils.getFile(originCacheDir);
      const morgue = PathUtils.join(cacheDir.path, "morgue");
      if (!(await IOUtils.exists(morgue))) {
        return [];
      }

      const files = [];
      for (const subdir of await IOUtils.getChildren(morgue)) {
        for (const path of await IOUtils.getChildren(subdir)) {
          if (path.endsWith(".final")) {
            const { size } = await IOUtils.stat(path);
            files.push({ path, size });
          }
        }
      }
      return files;
    }
  );
}

// Size of the Cache API database file of the origin, plus its WAL file when
// present, i.e. the database files counted in the quota usage.
function cacheDatabaseSize(includeWal = false) {
  return SpecialPowers.spawnChrome(
    [originCacheDirPath(), includeWal],
    async function (originCacheDir, aIncludeWal) {
      // eslint-disable-next-line mozilla/reject-importGlobalProperties
      Cu.importGlobalProperties(["IOUtils", "PathUtils"]);

      const cacheDir = FileUtils.getFile(originCacheDir);
      const dbPath = PathUtils.join(cacheDir.path, "caches.sqlite");
      let { size } = await IOUtils.stat(dbPath);
      if (aIncludeWal && (await IOUtils.exists(dbPath + "-wal"))) {
        size += (await IOUtils.stat(dbPath + "-wal")).size;
      }
      return size;
    }
  );
}
