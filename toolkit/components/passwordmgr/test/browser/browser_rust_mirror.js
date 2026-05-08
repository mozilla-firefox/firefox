/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Tests the AS RustLogins write-only mirror
 */
("use strict");

const { LoginManagerRustStorage } = ChromeUtils.importESModule(
  "resource://gre/modules/storage-rust.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { LoginCSVImport } = ChromeUtils.importESModule(
  "resource://gre/modules/LoginCSVImport.sys.mjs"
);

async function cleanupTest() {
  await LoginTestUtils.clearData();
  const rustStorage = new LoginManagerRustStorage();
  await rustStorage.clearAllPotentiallyVulnerablePasswords();
  await rustStorage.removeAllLoginsAsync();
  const migrationMayRun = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.flushPrefEnv();
  if (
    Services.prefs.getBoolPref("signon.rustMirror.enabled", false) &&
    Services.prefs.getBoolPref("signon.rustMirror.migrationNeeded", false)
  ) {
    await migrationMayRun;
  }
}

add_setup(async function () {
  registerCleanupFunction(async function () {
    SpecialPowers.clearUserPref("signon.rustMirror.migrationNeeded");
    SpecialPowers.clearUserPref("signon.rustMirror.poisoned");
  });
});

/**
 * Tests addLogin gets synced to Rust Storage
 */
add_task(async function test_mirror_addLogin() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "username",
    password: "password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  // note LoginManagerRustStorage is a singleton and already initialized when
  // Services.logins gets initialized.
  const rustStorage = new LoginManagerRustStorage();
  const storedLoginInfos = await Services.logins.getAllLogins();
  const rustStoredLoginInfos = await rustStorage.getAllLogins();
  LoginTestUtils.assertLoginListsEqual(storedLoginInfos, rustStoredLoginInfos);

  await cleanupTest();
});

/**
 * Tests modifyLogin gets synced to Rust Storage
 */
add_task(async function test_mirror_modifyLogin() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "username",
    password: "password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  const [storedLoginInfo] = await Services.logins.getAllLogins();

  const modifiedLoginInfo = LoginTestUtils.testData.formLogin({
    username: "username",
    password: "password",
    usernameField: "new_form_field_username",
    passwordField: "new_form_field_password",
  });
  const modifyLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.modifyLogin.finished"
  );
  await Services.logins.modifyLoginAsync(storedLoginInfo, modifiedLoginInfo);
  await modifyLoginFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const [storedModifiedLoginInfo] = await Services.logins.getAllLogins();
  const [rustStoredModifiedLoginInfo] = await rustStorage.searchLoginsAsync({
    guid: storedLoginInfo.guid,
  });
  LoginTestUtils.assertLoginListsEqual(
    [storedModifiedLoginInfo],
    [rustStoredModifiedLoginInfo]
  );

  await cleanupTest();
});

/**
 * Tests removeLogin gets synced to Rust Storage
 */
add_task(async function test_mirror_removeLogin() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "username",
    password: "password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  const [storedLoginInfo] = await Services.logins.getAllLogins();
  const removeLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.removeLogin.finished"
  );
  await Services.logins.removeLoginAsync(storedLoginInfo);
  await removeLoginFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(allLogins.length, 0);

  await cleanupTest();
});

/**
 * Tests CSV import: addition gets synced to Rust Storage
 */
add_task(async function test_mirror_csv_import_add() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  let csvFile = await LoginTestUtils.file.setupCsvFileWithLines([
    "url,username,password,httpRealm,formActionOrigin,guid,timeCreated,timeLastUsed,timePasswordChanged",
    `https://example.com,joe@example.com,qwerty,My realm,,{5ec0d12f-e194-4279-ae1b-d7d281bb46f0},1589617814635,1589710449871,1589617846802`,
  ]);
  const importLoginsFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.importLogins.finished"
  );
  await LoginCSVImport.importFromCSV(csvFile.path);
  // wait for the mirror to complete
  await importLoginsFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const storedLoginInfos = await Services.logins.getAllLogins();
  const rustStoredLoginInfos = await rustStorage.getAllLogins();
  LoginTestUtils.assertLoginListsEqual(storedLoginInfos, rustStoredLoginInfos);

  await cleanupTest();
});

/**
 * Tests CSV import: modification gets synced to Rust Storage
 */
add_task(async function test_mirror_csv_import_modify() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  // create a login
  const loginInfo = LoginTestUtils.testData.formLogin({
    origin: "https://example.com",
    username: "username",
    password: "password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  const login = await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  // and import it, so we update
  let csvFile = await LoginTestUtils.file.setupCsvFileWithLines([
    "url,username,password,httpRealm,formActionOrigin,guid,timeCreated,timeLastUsed,timePasswordChanged",
    `https://example.com,username,qwerty,My realm,,${login.guid},1589617814635,1589710449871,1589617846802`,
  ]);
  const importLoginsFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.importLogins.finished"
  );
  await LoginCSVImport.importFromCSV(csvFile.path);
  // wait for the mirror to complete
  await importLoginsFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const [storedLoginInfo] = await Services.logins.getAllLogins();
  const [rustStoredLoginInfo] = await rustStorage.getAllLogins();
  Assert.equal(
    storedLoginInfo.password,
    rustStoredLoginInfo.password,
    "password has been updated via csv import"
  );

  await cleanupTest();
});

/**
 * Verifies that the migration is triggered by according pref change
 */
add_task(async function test_migration_is_triggered_by_pref_change() {
  // enable rust mirror, triggering migration
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });

  Assert.equal(
    Services.prefs.getBoolPref("signon.rustMirror.migrationNeeded", false),
    true,
    "migrationNeeded is set to true"
  );

  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const prefChangePromise = TestUtils.waitForPrefChange(
    "signon.rustMirror.migrationNeeded"
  );

  // enable rust mirror, triggering migration
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  await prefChangePromise;
  Assert.equal(
    Services.prefs.getBoolPref("signon.rustMirror.migrationNeeded", false),
    false,
    "migrationNeeded is set to false"
  );

  await cleanupTest();
});

/**
 * Verifies that the migration is idempotent by ensuring that running
 * it multiple times does not create duplicate logins in the Rust store.
 */
add_task(async function test_migration_is_idempotent() {
  // Defensively clear state: in test-verification mode (10 runs in one browser)
  // previous iterations may have left data behind.
  LoginTestUtils.clearData();
  const rustStorage = new LoginManagerRustStorage();
  await rustStorage.removeAllLoginsAsync();

  // Add a login while the mirror is off so migration picks it up.
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });
  const login = LoginTestUtils.testData.formLogin({
    username: "test-user",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login);

  // Enable the mirror with migrationNeeded=true and wait until the login
  // appears in Rust. Using waitForCondition rather than topicObserved avoids
  // a race where a stale migration from the previous run's flushPrefEnv fires
  // migration.finished before our migration runs; with the re-queue fix in
  // #migrate(), our migration will run after the stale one completes.
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await BrowserTestUtils.waitForCondition(async () => {
    const logins = await rustStorage.getAllLogins();
    return logins.length === 1;
  }, "Login migrated to Rust after first migration");

  Assert.equal(
    (await rustStorage.getAllLogins()).length,
    1,
    "Rust store contains login after first migration"
  );

  // Trigger migration a second time and verify no duplicates.
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  Assert.equal(
    (await rustStorage.getAllLogins()).length,
    1,
    "No duplicate after second migration"
  );

  await cleanupTest();
});

/**
 * Verify that the migration:
 *  - continues when some rows fail (partial failure),
 *  - still migrates valid logins,
 */
add_task(async function test_migration_partial_failure() {
  // ensure mirror is off
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", false],
      ["signon.rustMirror.poisoned", false],
    ],
  });

  const rustStorage = new LoginManagerRustStorage();
  const origAddLoginsAsync = LoginManagerRustStorage.prototype.addLoginsAsync;
  sinon
    .stub(LoginManagerRustStorage.prototype, "addLoginsAsync")
    .callsFake(async function (logins, cont) {
      if (!cont) {
        return origAddLoginsAsync.call(this, logins, cont);
      }
      // Bulk migration pass: insert good logins for real, inject error for bad ones.
      const goodLogins = logins.filter(l => l.username !== "test-user-bad");
      if (goodLogins.length) {
        await origAddLoginsAsync.call(this, goodLogins, true);
      }
      return logins.map(login =>
        login.username === "test-user-bad"
          ? { error: { message: "row failed" } }
          : { login }
      );
    });

  const login_ok = LoginTestUtils.testData.formLogin({
    username: "test-user-ok",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login_ok);
  const login_bad = LoginTestUtils.testData.formLogin({
    username: "test-user-bad",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login_bad);

  // trigger again
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  const rustLogins = await rustStorage.getAllLogins();
  Assert.equal(rustLogins.length, 1, "only valid login migrated");

  Assert.equal(
    Services.prefs.getBoolPref("signon.rustMirror.poisoned", false),
    true,
    "poisoned pref is set to true on partial migration failure"
  );

  sinon.restore();
  await cleanupTest();
});

/**
 * Verify that when two JSON logins collapse onto the same Rust dedup key
 * after origin normalization, both are preserved: the winner keeps its
 * original origin and the loser is re-keyed under moz-pwmngr-fixed://.
 * The winner is chosen by highest timePasswordChanged.
 */
add_task(async function test_migration_merges_dupes() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", false],
      ["signon.rustMirror.poisoned", false],
    ],
  });
  Services.fog.testResetFOG();

  // timeLastUsed and timePasswordChanged are ordered opposite to verify
  // timePasswordChanged is the selection criterion.
  const stalePasswordLogin = LoginTestUtils.testData.formLogin({
    origin: "https://example.com/stale-pw",
    formActionOrigin: "https://example.com",
    username: "alice",
    password: "stale-password",
    timeCreated: 1000,
    timeLastUsed: 9000,
    timePasswordChanged: 1000,
    timesUsed: 50,
    timeLastBreachAlertDismissed: 1500,
  });
  const freshPasswordLogin = LoginTestUtils.testData.formLogin({
    origin: "https://example.com/fresh-pw",
    formActionOrigin: "https://example.com",
    username: "alice",
    password: "fresh-password",
    timeCreated: 5000,
    timeLastUsed: 6000,
    timePasswordChanged: 8000,
    timesUsed: 5,
    timeLastBreachAlertDismissed: 7000,
  });

  await Services.logins.addLoginAsync(stalePasswordLogin);
  await Services.logins.addLoginAsync(freshPasswordLogin);

  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const rustLogins = await rustStorage.getAllLogins();
  Assert.equal(rustLogins.length, 2, "both logins persisted in Rust");

  const winner = rustLogins.find(l => l.origin === "https://example.com");
  const rescued = rustLogins.find(l =>
    l.origin.startsWith("moz-pwmngr-fixed-")
  );
  Assert.ok(winner, "winner kept its original-scheme origin");
  Assert.ok(
    rescued,
    "duplicate origin rewritten to moz-pwmngr-fixed-<guid>:// scheme"
  );

  Assert.equal(
    winner.password,
    "fresh-password",
    "winner has most recent timePasswordChanged"
  );
  Assert.equal(winner.timeCreated, 5000);
  Assert.equal(winner.timeLastUsed, 6000);
  Assert.equal(winner.timePasswordChanged, 8000);
  Assert.equal(winner.timesUsed, 5);
  Assert.equal(winner.timeLastBreachAlertDismissed, 7000);

  Assert.equal(rescued.password, "stale-password");
  Assert.equal(rescued.timeCreated, 1000);
  Assert.equal(rescued.timeLastUsed, 9000);
  Assert.equal(rescued.timePasswordChanged, 1000);
  Assert.equal(rescued.timesUsed, 50);
  Assert.equal(rescued.timeLastBreachAlertDismissed, 1500);

  const [evt] = Glean.pwmgr.rustMigrationStatus.testGetValue();
  Assert.equal(evt.extra?.number_of_logins_to_migrate, "2");
  Assert.equal(evt.extra?.number_of_logins_migrated, "2");
  Assert.equal(evt.extra?.number_of_logins_quarantined, "1");

  LoginTestUtils.clearData();
  await rustStorage.removeAllLoginsAsync();
  await SpecialPowers.flushPrefEnv();
});

/**
 * Verify that when the bulk add operation rejects (hard failure),
 * the migration itself rejects.
 */
add_task(async function test_migration_rejects_when_bulk_add_rejects() {
  // turn mirror off
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", false],
      ["signon.rustMirror.poisoned", false],
    ],
  });

  const rustStorage = new LoginManagerRustStorage();
  sinon.stub(rustStorage, "addLoginsAsync").rejects(new Error("bulk failed"));

  const login = LoginTestUtils.testData.formLogin({
    username: "test-user",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login);

  // trigger again
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  const rustLogins = await rustStorage.getAllLogins();
  Assert.equal(rustLogins.length, 0, "zero logins migrated");

  const newPrefValue = Services.prefs.getBoolPref(
    "signon.rustMirror.migrationNeeded",
    false
  );
  Assert.equal(newPrefValue, true, "pref has not been reset");

  Assert.equal(
    Services.prefs.getBoolPref("signon.rustMirror.poisoned", false),
    true,
    "poisoned pref is set to true on hard migration failure"
  );

  sinon.restore();
  await cleanupTest();
});

/**
 * Tests that rust_migration_failure events are recorded
 * when a migration run encounters entry errors.
 */
add_task(async function test_rust_migration_failure_event() {
  // ensure mirror is off first
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });

  Services.fog.testResetFOG();

  const rustStorage = new LoginManagerRustStorage();

  const origAddLoginsAsync = LoginManagerRustStorage.prototype.addLoginsAsync;
  sinon
    .stub(rustStorage, "addLoginsAsync")
    .callsFake(async function (logins, cont) {
      if (!cont) {
        return origAddLoginsAsync.call(this, logins, cont);
      }
      // Bulk migration pass: insert good logins for real, inject error for bad ones.
      const goodLogins = logins.filter(l => l.username !== "bad-user");
      if (goodLogins.length) {
        await origAddLoginsAsync.call(this, goodLogins, true);
      }
      return logins.map(login =>
        login.username === "bad-user"
          ? { error: { message: "simulated migration failure" } }
          : { login }
      );
    });

  // Add two logins to JSON so migration has something to work on
  const login_ok = LoginTestUtils.testData.formLogin({
    username: "ok-user",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login_ok);

  const login_bad = LoginTestUtils.testData.formLogin({
    username: "bad-user",
    password: "secure-password",
  });
  await Services.logins.addLoginAsync(login_bad);

  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustWriteFailure.testGetValue()?.length == 1,
    "event has been emitted"
  );

  // Trigger migration
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  await waitForGleanEvent;

  const [evt] = Glean.pwmgr.rustWriteFailure.testGetValue();
  Assert.ok(evt.extra?.run_id, "event has a run_id");
  Assert.equal(
    evt.extra?.error_message,
    "simulated migration failure",
    "event has the expected error message"
  );

  sinon.restore();
  await cleanupTest();
});

/**
 * Ensures that migrating a large number of logins (100) from the JSON store to
 * the Rust store completes within a reasonable time frame (under 1 second).
 */
add_task(async function test_migration_time_under_threshold() {
  // ensure mirror is off
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });

  const numberOfLogins = 100;

  const logins = Array.from({ length: numberOfLogins }, (_, i) =>
    LoginTestUtils.testData.formLogin({
      origin: `https://www${i}.example.com`,
      username: `user${i}`,
    })
  );
  await Services.logins.addLogins(logins);
  await LoginTestUtils.reloadData();

  const rustStorage = new LoginManagerRustStorage();

  const start = Date.now();
  // using the migrationNeeded pref change as an indicator that the migration did run
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const prefChangePromise = TestUtils.waitForPrefChange(
    "signon.rustMirror.migrationNeeded"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await prefChangePromise;

  const duration = Date.now() - start;
  Assert.less(duration, 3000, "Migration should complete under 3s");
  Assert.equal(await rustStorage.countLoginsAsync("", "", ""), numberOfLogins);

  await cleanupTest();
});

/*
 * Tests that an error is logged when adding an invalid login to the Rust store.
 * The Rust store is stricter than the JSON store and rejects some formats,
 * such as single-dot origins.
 */
add_task(async function test_rust_mirror_addLogin_failure() {
  // ensure mirror is on, and reset poisoned flag
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", true],
      ["signon.rustMirror.poisoned", false],
    ],
  });
  Services.fog.testResetFOG();
  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
    "rust_mirror_status event has been emitted"
  );

  // This login will be accepted by JSON but rejected by Rust
  const badLogin = LoginTestUtils.testData.formLogin({
    origin: ".",
    passwordField: ".",
  });

  await Services.logins.addLoginAsync(badLogin);
  const allLoginsJson = await Services.logins.getAllLogins();
  Assert.equal(
    allLoginsJson.length,
    1,
    "single dot origin login saved to JSON"
  );

  await waitForGleanEvent;

  const rustStorage = new LoginManagerRustStorage();
  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(
    allLogins.length,
    0,
    "single dot origin login not saved to Rust"
  );

  const [evt] = Glean.pwmgr.rustMirrorStatus.testGetValue();
  Assert.equal(
    evt.extra?.operation,
    "add",
    "rust_mirror_status event has operation"
  );
  Assert.equal(
    evt.extra?.status,
    "failure",
    "rust_mirror_status event has status=failure"
  );

  const [evt1] = Glean.pwmgr.rustWriteFailure.testGetValue();
  Assert.equal(
    evt1.extra?.error_message,
    "Login has illegal origin: relative URL without a base",
    "event has error_message"
  );
  Assert.equal(
    evt1.extra?.poisoned,
    "false",
    "rust_write_failure event is not poisoned"
  );

  // produce another failure
  const waitForSecondGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 2,
    "two events have been emitted"
  );
  const badLogin2 = LoginTestUtils.testData.formLogin({
    username: "another-bad-login",
    origin: ".",
    passwordField: ".",
  });
  await Services.logins.addLoginAsync(badLogin2);

  await waitForSecondGleanEvent;

  // eslint-disable-next-line no-unused-vars
  const [_, evt3] = Glean.pwmgr.rustWriteFailure.testGetValue();
  Assert.equal(
    evt3.extra?.poisoned,
    "true",
    "rust_write_failure event is poisoned now"
  );

  await cleanupTest();
});

/*
 * Tests that we store non-ASCII origins get punycoded.
 */
add_task(async function test_punycode_origin_metric() {
  // ensure mirror is on
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  Services.fog.testResetFOG();

  const punicodeOrigin = "https://münich.example.com";
  const login = LoginTestUtils.testData.formLogin({
    origin: punicodeOrigin,
    formActionOrigin: "https://example.com",
    username: "user1",
    password: "pass1",
  });

  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
    "event has been emitted"
  );

  await Services.logins.addLoginAsync(login);

  await waitForGleanEvent;

  const rustStorage = new LoginManagerRustStorage();

  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(allLogins.length, 1, "punicode origin login saved to Rust");
  const [rustLogin] = allLogins;
  Assert.equal(
    rustLogin.origin,
    "https://xn--mnich-kva.example.com",
    "origin has been punicoded on the Rust side"
  );

  await cleanupTest();
});

/*
 * Tests that we store non-ASCII formorigins get punycoded.
 */
add_task(async function test_punycode_formActionOrigin_metric() {
  // ensure mirror is on
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  Services.fog.testResetFOG();

  const punicodeOrigin = "https://münich.example.com";
  const login = LoginTestUtils.testData.formLogin({
    formActionOrigin: punicodeOrigin,
    origin: "https://example.com",
    username: "user1",
    password: "pass1",
  });

  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
    "event has been emitted"
  );

  await Services.logins.addLoginAsync(login);

  await waitForGleanEvent;

  const rustStorage = new LoginManagerRustStorage();
  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(allLogins.length, 1, "punicode origin login saved to Rust");
  const [rustLogin] = allLogins;
  Assert.equal(
    rustLogin.formActionOrigin,
    "https://xn--mnich-kva.example.com",
    "origin has been punicoded on the Rust side"
  );

  await cleanupTest();
});

/*
 * Tests that origins accepted by JSON but rejected by Rust cause the mirror
 * to record a failure event and leave no login in Rust storage.
 */
const originsToTest = [
  "//example.com",
  "//example.com/path",
  "example.com/path",
  "hptts//example.com",
  "htp//example.com",
  "htpps//example.com",
  "http//example.com",
  "http//example.com/path",
  "https//example.com",
  "https//example.com:abc",
  "https:// example.com",
  "https:///",
  "https://exa mple.com",
  "htttp//example.com",
];
for (const origin of originsToTest) {
  add_task(async function () {
    await SpecialPowers.pushPrefEnv({
      set: [["signon.rustMirror.enabled", true]],
    });
    Services.fog.testResetFOG();

    const login = LoginTestUtils.testData.formLogin({
      origin,
      formActionOrigin: "https://example.com",
      username: "user1",
      password: "pass1",
    });

    const waitForGleanEvent = BrowserTestUtils.waitForCondition(
      () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
      "rust_mirror_status event has been emitted"
    );
    await Services.logins.addLoginAsync(login);
    await waitForGleanEvent;

    const [statusEvt] = Glean.pwmgr.rustMirrorStatus.testGetValue();
    Assert.equal(
      statusEvt.extra?.status,
      "failure",
      `${origin}: rust mirror reports failure`
    );

    const rustStorage = new LoginManagerRustStorage();
    const allLoginsRust = await rustStorage.getAllLogins();
    Assert.equal(allLoginsRust.length, 0, `${origin}: not stored in Rust`);

    LoginTestUtils.clearData();
    await rustStorage.removeAllLoginsAsync();
    await SpecialPowers.flushPrefEnv();
  });
}

/*
 * Tests that origins repaired by perform_additional_origin_fixups are accepted
 * and stored with the fixed-up origin value.
 */
const fixedUpOriginsToTest = {
  "example.com": "moz-pwmngr-fixed://example.com",
  "www.example.com": "moz-pwmngr-fixed://www.example.com",
  https: "moz-pwmngr-fixed://https",
  "https:": "https://moz.pwmngr.fixed",
  "https://": "https://moz.pwmngr.fixed",
  "1.2.3.4": "moz-pwmngr-fixed://1.2.3.4",
  "ftp.example.com": "ftp://ftp.example.com",
  "ftp.1.2.3.4": "ftp://1.2.3.4",
  "http://ftp.1.2.3.4": "ftp://1.2.3.4", // eslint-disable-line @microsoft/sdl/no-insecure-url
};
for (const origin in fixedUpOriginsToTest) {
  add_task(async function () {
    await SpecialPowers.pushPrefEnv({
      set: [["signon.rustMirror.enabled", true]],
    });

    Services.fog.testResetFOG();

    const login = LoginTestUtils.testData.formLogin({
      origin,
      formActionOrigin: "https://example.com",
      username: "user1",
      password: "pass1",
    });

    const waitForGleanEvent = BrowserTestUtils.waitForCondition(
      () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
      "event has been emitted"
    );
    await Services.logins.addLoginAsync(login);
    await waitForGleanEvent;

    const rustStorage = new LoginManagerRustStorage();
    const allLogins = await rustStorage.getAllLogins();
    Assert.equal(allLogins.length, 1, `${origin}: saved to Rust after fixup`);
    Assert.equal(
      allLogins[0].origin,
      fixedUpOriginsToTest[origin],
      `${origin}: origin was fixed up correctly`
    );

    LoginTestUtils.clearData();
    await rustStorage.removeAllLoginsAsync();
    await SpecialPowers.flushPrefEnv();
  });
}

/*
 * Tests that we collect telemetry if the username contains line breaks.
 */
add_task(async function test_username_linebreak_metric() {
  // ensure mirror is on
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  Services.fog.testResetFOG();

  const login = LoginTestUtils.testData.formLogin({
    origin: "https://example.com",
    formActionOrigin: "https://example.com",
    username: "user\nname",
    password: "pass1",
  });

  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
    "event has been emitted"
  );
  await Services.logins.addLoginAsync(login);
  await waitForGleanEvent;
  const rustStorage = new LoginManagerRustStorage();
  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(
    allLogins.length,
    1,
    "line break username origin login saved to Rust"
  );

  await cleanupTest();
});

/*
 * Tests that an error is logged when adding an invalid login to the Rust store,
 * and that time_created and time_last_used telemetry is recorded on failure.
 */
add_task(async function test_rust_mirror_addLogin_failure_with_time_metrics() {
  // ensure mirror is on, and reset poisoned flag
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", true],
      ["signon.rustMirror.poisoned", false],
    ],
  });
  Services.fog.testResetFOG();

  const waitForGleanEvent = BrowserTestUtils.waitForCondition(
    () => Glean.pwmgr.rustMirrorStatus.testGetValue()?.length == 1,
    "rust_mirror_status event has been emitted"
  );

  // This login will be accepted by JSON but rejected by Rust
  const badLogin = LoginTestUtils.testData.formLogin({
    origin: ".",
    passwordField: ".",
  });

  await Services.logins.addLoginAsync(badLogin);
  const allLoginsJson = await Services.logins.getAllLogins();
  Assert.equal(
    allLoginsJson.length,
    1,
    "single dot origin login saved to JSON"
  );

  await waitForGleanEvent;

  const rustStorage = new LoginManagerRustStorage();
  const allLogins = await rustStorage.getAllLogins();
  Assert.equal(
    allLogins.length,
    0,
    "single dot origin login not saved to Rust"
  );

  const [statusEvt] = Glean.pwmgr.rustMirrorStatus.testGetValue();
  Assert.equal(
    statusEvt.extra?.operation,
    "add",
    "rust_mirror_status event has operation"
  );
  Assert.equal(
    statusEvt.extra?.status,
    "failure",
    "rust_mirror_status event has status=failure"
  );

  const [failureEvt] = Glean.pwmgr.rustWriteFailure.testGetValue();

  Assert.notEqual(
    failureEvt.extra?.time_created,
    null,
    "time_created is recorded on rust write failure"
  );
  Assert.notEqual(
    failureEvt.extra?.time_last_used,
    null,
    "time_last_used is recorded on rust write failure"
  );

  const created = new Date(Number(failureEvt.extra.time_created));

  Assert.equal(
    created.getUTCDate(),
    1,
    "time_created is bucketed to month (UTC)"
  );

  await cleanupTest();
});

/**
 * Tests that a rust_migration_performance event is recorded after migration,
 * containing both duration and total number of migrated logins.
 */
add_task(async function test_migration_performance_probe() {
  // ensure mirror is off
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });
  Services.fog.testResetFOG();

  const login = LoginTestUtils.testData.formLogin({
    username: "perf-user",
    password: "perf-password",
  });
  await Services.logins.addLoginAsync(login);

  // using the migrationNeeded pref change as an indicator that the migration did run
  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const prefChangePromise = TestUtils.waitForPrefChange(
    "signon.rustMirror.migrationNeeded"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await prefChangePromise;

  const [evt] = Glean.pwmgr.rustMigrationStatus.testGetValue();
  Assert.ok(evt, "rustMigrationStatus event should have been emitted");
  Assert.equal(
    evt.extra?.number_of_logins_to_migrate,
    1,
    "event should record number of logins to migrate"
  );
  Assert.equal(
    evt.extra?.number_of_logins_migrated,
    1,
    "event should record number of logins migrated"
  );
  Assert.equal(
    evt.extra?.had_errors,
    "false",
    "event should record a boolean indicating migration errors"
  );
  Assert.greaterOrEqual(
    parseInt(evt.extra?.duration_ms, 10),
    0,
    "event should record non-negative duration in ms"
  );

  sinon.restore();
  await cleanupTest();
});

/**
 * Tests addPotentiallyVulnerablePassword gets synced to Rust Storage
 */
add_task(async function test_mirror_addPotentiallyVulnerablePassword() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "vuln-user",
    password: "vuln-password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  const addVulnFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addPotentiallyVulnerablePassword.finished"
  );
  const [storedLogin] = await Services.logins.getAllLogins();
  await Services.logins.addPotentiallyVulnerablePassword(storedLogin);
  await addVulnFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  Assert.ok(
    await rustStorage.isPotentiallyVulnerablePassword(storedLogin),
    "login should be vulnerable in Rust storage after mirror sync"
  );

  await cleanupTest();
});

/**
 * Tests that breach alert dismissals are migrated to Rust during migration
 */
add_task(async function test_migration_includes_breach_alert_dismissals() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", false]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "breach-dismissed-user",
    password: "breach-dismissed-password",
  });
  await Services.logins.addLoginAsync(loginInfo);
  const [storedLogin] = await Services.logins.getAllLogins();
  await Services.logins.recordBreachAlertDismissal(storedLogin.guid);

  const jsonDismissals =
    await Services.logins.getBreachAlertDismissalsByLoginGUID();
  Assert.ok(
    jsonDismissals[storedLogin.guid],
    "breach alert dismissal recorded in JSON storage"
  );

  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  const rustDismissals =
    await rustStorage.getBreachAlertDismissalsByLoginGUID();
  Assert.ok(
    rustDismissals[storedLogin.guid],
    "breach alert dismissal should be migrated to Rust storage"
  );
  Assert.greater(
    rustDismissals[storedLogin.guid].timeBreachAlertDismissed,
    0,
    "dismissal timestamp should be positive"
  );

  await cleanupTest();
});

/**
 * Tests clearAllPotentiallyVulnerablePasswords gets synced to Rust Storage
 */
add_task(async function test_mirror_clearAllPotentiallyVulnerablePasswords() {
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "vuln-user",
    password: "vuln-password",
  });
  const addLoginFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addLogin.finished"
  );
  await Services.logins.addLoginAsync(loginInfo);
  await addLoginFinishedPromise;

  const addVulnFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.addPotentiallyVulnerablePassword.finished"
  );
  const [storedLogin] = await Services.logins.getAllLogins();
  await Services.logins.addPotentiallyVulnerablePassword(storedLogin);
  await addVulnFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  Assert.ok(
    await rustStorage.isPotentiallyVulnerablePassword(storedLogin),
    "login should be vulnerable before clearing"
  );

  const clearVulnFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.event.clearAllPotentiallyVulnerablePasswords.finished"
  );
  await Services.logins.clearAllPotentiallyVulnerablePasswords();
  await clearVulnFinishedPromise;

  Assert.ok(
    !(await rustStorage.isPotentiallyVulnerablePassword(storedLogin)),
    "login should not be vulnerable in Rust storage after clearing"
  );

  await cleanupTest();
});

/**
 * Tests that vulnerable passwords are migrated to Rust during migration
 */
add_task(async function test_migration_includes_vulnerable_passwords() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["signon.rustMirror.enabled", false],
      ["signon.management.page.vulnerable-passwords.enabled", true],
    ],
  });

  const loginInfo = LoginTestUtils.testData.formLogin({
    username: "migrate-vuln-user",
    password: "migrate-vuln-password",
  });
  await Services.logins.addLoginAsync(loginInfo);
  const [storedLogin] = await Services.logins.getAllLogins();
  await Services.logins.addPotentiallyVulnerablePassword(storedLogin);

  Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  const migrationFinishedPromise = TestUtils.topicObserved(
    "rust-mirror.migration.finished"
  );
  await SpecialPowers.pushPrefEnv({
    set: [["signon.rustMirror.enabled", true]],
  });
  await migrationFinishedPromise;

  const rustStorage = new LoginManagerRustStorage();
  Assert.ok(
    await rustStorage.isPotentiallyVulnerablePassword(storedLogin),
    "vulnerable password should be migrated to Rust storage"
  );

  await cleanupTest();
});
