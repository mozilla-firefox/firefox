/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_database_reopened_after_connection_closed() {
  Services.prefs.setBoolPref("dom.quotaManager.testing", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("dom.quotaManager.testing");
  });

  const db = new Database("main/closed-connection");
  await db.importChanges({}, 42, [{ id: "a" }]);
  Assert.equal(await db.getLastModified(), 42, "The data was stored");

  // Shutting down the storage closes every IndexedDB connection.
  await new Promise(resolve => {
    const request = Services.qms.reset();
    request.callback = () => {
      Assert.equal(
        request.resultCode,
        Cr.NS_OK,
        "The storage was reset successfully"
      );
      resolve();
    };
  });

  await TestUtils.waitForCondition(async () => {
    try {
      return (await db.getLastModified()) == 42;
    } catch (e) {
      info(`Database not usable yet: ${e}`);
      return false;
    }
  }, "The database can be used again after its connection was closed");
});
