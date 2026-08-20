/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// CacheFileIOManager::OnIdleDaily is the fallback that removes cache folders the
// purge background task failed to delete. A single read-only entry makes both
// DeleteFileW and RemoveDirectoryW fail with ACCESS_DENIED, so without forcing
// the removal the folder stays around forever (bug 1882163).

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const RESIDUAL_LEAF = "cache2.2024-01-01-00-00-00.purge.bg_rm";

function clearReadOnlyRecursive(aFile) {
  try {
    if (aFile.isDirectory()) {
      for (let child of aFile.directoryEntries) {
        clearReadOnlyRecursive(child);
      }
    }
    aFile.QueryInterface(Ci.nsILocalFileWin).readOnly = false;
  } catch (e) {}
}

// Mirrors the layout reported in bug 1848542 comment #30: two empty
// subdirectories and three zero-length context eviction files.
function createReadOnlyResidualFolder() {
  let dir = do_get_profile();
  dir.append(RESIDUAL_LEAF);
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o744);

  let children = [];
  for (let sub of ["doomed", "entries"]) {
    let d = dir.clone();
    d.append(sub);
    d.create(Ci.nsIFile.DIRECTORY_TYPE, 0o744);
    children.push(d);
  }
  for (let leaf of [
    "ce_Kg==",
    "ce_T151c2VyQ29udGV4dElkPTUs",
    "ce_T151c2VyQ29udGV4dElkPTUsYSw=",
  ]) {
    let f = dir.clone();
    f.append(leaf);
    f.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
    children.push(f);
  }

  for (let child of children) {
    child.QueryInterface(Ci.nsILocalFileWin).readOnly = true;
  }

  return dir;
}

add_task(
  { skip_if: () => mozinfo.os != "win" },
  async function test_readonly_residual_folder_removed_on_idle_daily() {
    do_get_profile();

    // OnIdleDaily only sweeps when the cache is cleared at shutdown via the
    // background task.
    Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", true);
    Services.prefs.setBoolPref("privacy.clearOnShutdown_v2.cache", true);
    Services.prefs.setBoolPref(
      "network.cache.shutdown_purge_in_background_task",
      true
    );

    // Write an entry so the disk cache, and therefore mCacheDirectory, exists.
    await new Promise(resolve => {
      asyncOpenCacheEntry(
        "http://a/",
        "disk",
        Ci.nsICacheStorage.OPEN_NORMALLY,
        null,
        new OpenCallback(NEW, "a1m", "a1d", resolve)
      );
    });

    let dir = createReadOnlyResidualFolder();
    Assert.ok(dir.exists(), "residual folder was created");

    Services.obs.notifyObservers(null, "idle-daily");

    try {
      await TestUtils.waitForCondition(
        () => !dir.exists(),
        "read-only residual cache folder should be removed by the daily sweep"
      );
    } finally {
      clearReadOnlyRecursive(dir);
      if (dir.exists()) {
        dir.remove(true);
      }
    }
  }
);
