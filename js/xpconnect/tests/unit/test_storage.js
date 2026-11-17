/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  do_get_profile();
});

add_task(function test_storage_in_sandbox() {
  const sb = new Cu.Sandbox("http://www.example.com", {
    wantGlobalProperties: ["storage"],
  });
  sb.ok = ok;
  Cu.evalInSandbox("ok(storage instanceof StorageManager);", sb);
});

/**
 * Privileged scopes get `storage` without importGlobalProperties, so that OPFS
 * is reachable from a system module without borrowing a window's navigator.
 */
add_task(function test_storage_is_a_global() {
  Assert.ok(storage instanceof StorageManager, "storage is a StorageManager");
});

/**
 * Nothing in the tree deletes this property; resolve just needs to keep
 * working if it ever does, identity across the delete isn't guaranteed.
 */
add_task(function test_storage_re_resolves_after_delete() {
  Assert.ok(delete globalThis.storage, "storage property was deleted");
  Assert.ok(storage instanceof StorageManager, "storage resolves again");
});

/**
 * OPFS resolves against the system principal, with no window involved.
 */
add_task(async function test_get_directory_without_a_window() {
  const root = await storage.getDirectory();
  Assert.ok(root, "Got the OPFS root directory handle");

  const fileHandle = await root.getFileHandle("test.txt", { create: true });
  const writable = await fileHandle.createWritable();
  await writable.write("chrome");
  await writable.close();

  const file = await fileHandle.getFile();
  Assert.equal(await file.text(), "chrome", "Read back what was written");

  await root.removeEntry("test.txt");

  // Clear the storage.
  await awaitRequest(
    Services.qms.clearStoragesForPrincipal(
      Services.scriptSecurityManager.getSystemPrincipal()
    )
  );
});

function awaitRequest(request) {
  return new Promise(function (resolve, reject) {
    request.callback = function () {
      if (request.resultCode == Cr.NS_OK) {
        resolve(request.result);
      } else {
        reject(new Error(request.resultName));
      }
    };
  });
}
