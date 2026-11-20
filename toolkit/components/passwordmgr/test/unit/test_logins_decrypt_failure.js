/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the case where there are logins that cannot be decrypted.
 */

"use strict";

// Decryption failures are transparent in Rust. Cleanup is done via
// `run_maintenance()` when idle.
// See Bug 2052523
const isRustBackend = Services.prefs.getBoolPref(
  "signon.storage.rust.enabled",
  false
);

// Globals

/**
 * Resets the token used to decrypt logins.  This is equivalent to resetting the
 * primary password when it is not known.
 */
async function resetPrimaryPassword() {
  let token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
    Ci.nsIPKCS11Token
  );
  await token.reset();
}

// Tests

/**
 * Resets the primary password after some logins were added to the database.
 */
add_task(async function test_logins_decrypt_failure() {
  let logins = TestData.loginList();
  await Services.logins.addLogins(logins);

  // This makes the existing logins non-decryptable.
  await resetPrimaryPassword();

  // These functions don't see the non-decryptable entries anymore.
  let savedLogins = await Services.logins.getAllLogins();
  Assert.equal(savedLogins.length, 0, "getAllLogins length");
  const result = await Services.logins.searchLoginsAsync({});
  Assert.equal(result.length, 0);
  await Assert.rejects(
    Services.logins.modifyLoginAsync(logins[0], newPropertyBag()),
    /No matching logins/
  );
  await Assert.rejects(
    Services.logins.removeLoginAsync(logins[0]),
    /No matching logins/
  );

  // The function that counts logins sees the non-decryptable entries also.
  Assert.equal(
    await Services.logins.countLoginsAsync("", "", ""),
    logins.length
  );

  // Equivalent logins can be added.
  await Services.logins.addLogins(logins);
  await LoginTestUtils.checkLogins(logins);
  Assert.equal(
    (await Services.logins.getAllLogins()).length,
    logins.length,
    "getAllLogins length"
  );
  Assert.equal(
    await Services.logins.countLoginsAsync("", "", ""),
    logins.length * 2
  );

  // Finding logins doesn't return the non-decryptable duplicates.
  Assert.equal(
    (
      await Services.logins.searchLoginsAsync({
        origin: "http://www.example.com",
      })
    ).length,
    1
  );
  let matchData = { origin: "http://www.example.com" };
  const result2 = await Services.logins.searchLoginsAsync(matchData);
  Assert.equal(result2.length, 1);

  // Removing single logins does not remove non-decryptable logins.
  for (let loginInfo of TestData.loginList()) {
    await Services.logins.removeLoginAsync(loginInfo);
  }
  Assert.equal((await Services.logins.getAllLogins()).length, 0);
  Assert.equal(
    await Services.logins.countLoginsAsync("", "", ""),
    logins.length
  );

  // Removing all logins removes the non-decryptable entries also.
  await Services.logins.removeAllUserFacingLoginsAsync();
  Assert.equal((await Services.logins.getAllLogins()).length, 0);
  Assert.equal(await Services.logins.countLoginsAsync("", "", ""), 0);
}).skip(isRustBackend);

// Bug 621846 - If a login has a GUID but can't be decrypted, a search for
// that GUID will (correctly) fail. Ensure we can add a new login with that
// same GUID.
add_task(async function test_add_logins_with_decrypt_failure() {
  // a login with a GUID.
  let login = new LoginInfo(
    "http://www.example2.com",
    "http://www.example2.com",
    null,
    "the username",
    "the password for www.example.com",
    "form_field_username",
    "form_field_password"
  );

  login.QueryInterface(Ci.nsILoginMetaInfo);
  login.guid = "{4bc50d2f-dbb6-4aa3-807c-c4c2065a2c35}";

  // A different login but with the same GUID.
  let loginDupeGuid = new LoginInfo(
    "http://www.example3.com",
    "http://www.example3.com",
    null,
    "the username",
    "the password",
    "form_field_username",
    "form_field_password"
  );
  loginDupeGuid.QueryInterface(Ci.nsILoginMetaInfo);
  loginDupeGuid.guid = login.guid;

  await Services.logins.addLoginAsync(login);

  // We can search for this login by GUID.
  const result = await Services.logins.searchLoginsAsync({ guid: login.guid });
  equal(result.length, 1);

  // We should fail to re-add it as it remains good.
  await Assert.rejects(
    Services.logins.addLoginAsync(login),
    /This login already exists./
  );
  // We should fail to re-add a different login with the same GUID.
  await Assert.rejects(
    Services.logins.addLoginAsync(loginDupeGuid),
    /specified GUID already exists/
  );

  // This makes the existing login non-decryptable.
  await resetPrimaryPassword();

  // We can no longer find it in our search.
  const result1 = await Services.logins.searchLoginsAsync({ guid: login.guid });
  equal(result1.length, 0);

  // So we should be able to re-add a login with that same GUID.
  await Services.logins.addLoginAsync(login);
  const result2 = await Services.logins.searchLoginsAsync({ guid: login.guid });
  equal(result2.length, 1);

  await Services.logins.removeAllUserFacingLoginsAsync();
}).skip(isRustBackend);

// Test the "syncID" metadata works as expected on decryption failure.
add_task(async function test_sync_metadata_with_decrypt_failure() {
  // And some sync metadata
  await Services.logins.setSyncID("sync-id");
  await Services.logins.setLastSync(123);
  equal(await Services.logins.getSyncID(), "sync-id");
  equal(await Services.logins.getLastSync(), 123);

  // This makes the existing login and syncID non-decryptable.
  await resetPrimaryPassword();

  // The syncID is now null.
  equal(await Services.logins.getSyncID(), null);
  // The sync timestamp isn't impacted.
  equal(await Services.logins.getLastSync(), 123);

  // But we should be able to set it again.
  await Services.logins.setSyncID("new-id");
  equal(await Services.logins.getSyncID(), "new-id");
}).skip(isRustBackend);

add_task(
  { skip_if: () => !isRustBackend },
  async function test_rust_undecryptable_login_is_recorded() {
    Services.fog.initializeFOG();
    Services.fog.testResetFOG();
    await Services.logins.addLogins(TestData.loginList());
    const [victim] = await Services.logins.getAllLogins();
    victim.QueryInterface(Ci.nsILoginMetaInfo);

    const { Sqlite } = ChromeUtils.importESModule(
      "resource://gre/modules/Sqlite.sys.mjs"
    );
    const db = await Sqlite.openConnection({
      path: PathUtils.join(PathUtils.profileDir, "logins.db"),
    });
    await db.execute(
      "UPDATE loginsL SET secFields = 'not-a-ciphertext' WHERE guid = :guid",
      { guid: victim.guid }
    );
    await db.close();

    await Assert.rejects(
      Services.logins.getAllLogins(),
      /Primary password locked/,
      "A login that does not decrypt still fails the whole list"
    );
    Assert.deepEqual(
      Glean.pwmgr.rustStorageCryptoError
        .testGetValue()
        .map(event => event.extra),
      [
        {
          operation: "list",
          error_kind: "undecryptable",
          direction: "decrypt",
        },
      ],
      "The failure is recorded as a login that does not decrypt"
    );

    await Services.logins.removeAllUserFacingLoginsAsync();
  }
);
