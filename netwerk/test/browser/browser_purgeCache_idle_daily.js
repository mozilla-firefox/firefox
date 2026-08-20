/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_task(async function test_idle_cleanup() {
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(
    "network.cache.shutdown_purge_in_background_task",
    true
  );
  Services.prefs.setBoolPref("privacy.clearOnShutdown.cache", true);
  Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", true);
  let dir = Services.dirsvc.get("ProfD", Ci.nsIFile);
  dir.append("cache2.2021-11-25-08-47-04.purge.bg_rm");
  Assert.equal(dir.exists(), false, `Folder ${dir.path} should not exist`);
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o744);
  Assert.equal(
    dir.exists(),
    true,
    `Folder ${dir.path} should have been created`
  );

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(() => {
    return !dir.exists();
  });

  Assert.equal(
    dir.exists(),
    false,
    `Folder ${dir.path} should have been purged by background task`
  );
  Assert.equal(
    await Glean.networking.residualCacheFolderCount.testGetValue(),
    1
  );
  Assert.equal(
    await Glean.networking.residualCacheFolderRemoval.success.testGetValue(),
    1
  );
  Assert.equal(
    await Glean.networking.residualCacheFolderRemoval.failure.testGetValue(),
    null
  );

  // Making folders readonly only works on windows
  if (AppConstants.platform == "win") {
    // A readonly folder used to be left behind forever. The daily sweep now
    // clears the attribute and retries, so it must be removed (bug 1882163).
    dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o744);
    dir.QueryInterface(Ci.nsILocalFileWin).readOnly = true;

    Services.obs.notifyObservers(null, "idle-daily");

    await TestUtils.waitForCondition(() => {
      return !dir.exists();
    });

    Assert.equal(
      await Glean.networking.residualCacheFolderCount.testGetValue(),
      2
    );
    Assert.equal(
      await Glean.networking.residualCacheFolderRemoval.success.testGetValue(),
      2
    );
    Assert.equal(
      await Glean.networking.residualCacheFolderRemoval.failure.testGetValue(),
      null
    );

    // Check that telemetry properly detects folders failing to be deleted.
    // An open handle without FILE_SHARE_DELETE makes DeleteFileW fail with
    // ERROR_SHARING_VIOLATION, which clearing the readonly attribute can't
    // recover from.
    dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o744);
    let lockedFile = dir.clone();
    lockedFile.append("locked");
    let stream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
      Ci.nsIFileOutputStream
    );
    stream.init(lockedFile, -1, -1, 0);

    Services.obs.notifyObservers(null, "idle-daily");

    await TestUtils.waitForCondition(async () => {
      return (
        (await Glean.networking.residualCacheFolderRemoval.failure.testGetValue()) ==
        1
      );
    });

    Assert.equal(
      await Glean.networking.residualCacheFolderCount.testGetValue(),
      3
    );
    Assert.equal(
      await Glean.networking.residualCacheFolderRemoval.success.testGetValue(),
      2
    );
    Assert.equal(
      await Glean.networking.residualCacheFolderRemoval.failure.testGetValue(),
      1
    );
    Assert.ok(dir.exists(), `Folder ${dir.path} should not have been removed`);

    stream.close();
    dir.remove(true);
  }

  Services.prefs.clearUserPref(
    "network.cache.shutdown_purge_in_background_task"
  );
  Services.prefs.clearUserPref("privacy.clearOnShutdown.cache");
  Services.prefs.clearUserPref("privacy.sanitize.sanitizeOnShutdown");
});
