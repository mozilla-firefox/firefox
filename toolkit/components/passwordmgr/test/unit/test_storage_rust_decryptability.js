/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the check of the Rust storage that counts the logins which do not
 * decrypt, and the telemetry it records.
 */

"use strict";

const { LoginManagerRustStorage } = ChromeUtils.importESModule(
  "resource://gre/modules/storage-rust.sys.mjs"
);
const { LoginManagerStorage } = ChromeUtils.importESModule(
  "resource://passwordmgr/passwordstorage.sys.mjs"
);
const { Sqlite } = ChromeUtils.importESModule(
  "resource://gre/modules/Sqlite.sys.mjs"
);

let rustStore;

function decryptabilityEvents() {
  return (Glean.pwmgr.rustStoreDecryptability.testGetValue() ?? []).map(
    event => event.extra
  );
}

async function corrupt(guid) {
  const db = await Sqlite.openConnection({
    path: PathUtils.join(PathUtils.profileDir, "logins.db"),
  });
  await db.execute(
    "UPDATE loginsL SET secFields = 'not-a-ciphertext' WHERE guid = :guid",
    { guid }
  );
  await db.close();
}

add_setup(async function () {
  Services.fog.initializeFOG();
  await Services.logins.initializationPromise;
  rustStore = new LoginManagerRustStorage();
  await rustStore.initialize();
  await rustStore.removeAllLoginsAsync();
});

add_task(async function test_all_logins_decrypt() {
  await rustStore.addLoginsAsync(TestData.loginList());
  Services.fog.testResetFOG();

  await rustStore.recordDecryptability();

  Assert.deepEqual(decryptabilityEvents(), [
    {
      result: "ok",
      total: String(TestData.loginList().length),
      undecryptable: "0",
    },
  ]);
  await rustStore.removeAllLoginsAsync();
});

add_task(async function test_one_login_does_not_decrypt() {
  await rustStore.addLoginsAsync(TestData.loginList());
  const [victim] = await rustStore.getAllLogins();
  await corrupt(victim.QueryInterface(Ci.nsILoginMetaInfo).guid);
  Services.fog.testResetFOG();

  await rustStore.recordDecryptability();

  Assert.deepEqual(decryptabilityEvents(), [
    {
      result: "ok",
      total: String(TestData.loginList().length),
      undecryptable: "1",
    },
  ]);
  await rustStore.removeAllLoginsAsync();
});

add_task(async function test_skipped_with_a_primary_password() {
  Services.ppmm.sharedData.set("isPrimaryPasswordSet", true);
  Services.fog.testResetFOG();

  await rustStore.recordDecryptability();

  Assert.deepEqual(decryptabilityEvents(), [], "Nothing is recorded");
  Services.ppmm.sharedData.set("isPrimaryPasswordSet", false);
});

add_task(
  {
    skip_if: () => LoginManagerStorage.getActiveStore() !== rustStore,
  },
  async function test_recorded_through_the_idle_task_entry_point() {
    await rustStore.addLoginsAsync(TestData.loginList());
    Services.fog.testResetFOG();

    await LoginManagerStorage.recordDecryptability();

    Assert.deepEqual(decryptabilityEvents(), [
      {
        result: "ok",
        total: String(TestData.loginList().length),
        undecryptable: "0",
      },
    ]);
    await rustStore.removeAllLoginsAsync();
  }
);

add_task(async function test_never_prompts_for_a_primary_password_set_later() {
  await rustStore.addLoginsAsync(TestData.loginList());
  await LoginTestUtils.primaryPassword.enable();
  Assert.strictEqual(
    Services.ppmm.sharedData.get("isPrimaryPasswordSet"),
    false,
    "The flag still says there is no primary password"
  );
  Services.fog.testResetFOG();

  await rustStore.recordDecryptability();

  Assert.deepEqual(decryptabilityEvents(), [], "Nothing is recorded");
  Assert.equal(
    Glean.pwmgr.primaryPasswordPrompt.testGetValue(),
    null,
    "No prompt was shown"
  );
  await Assert.rejects(
    rustStore.modifyLoginAsync(
      TestData.formLogin({ origin: "https://nowhere.example.com" }),
      {}
    ),
    /No matching logins/,
    "A declined prompt does not turn later errors into a cancelled prompt"
  );

  await LoginTestUtils.primaryPassword.disable();
  await rustStore.removeAllLoginsAsync();
});
