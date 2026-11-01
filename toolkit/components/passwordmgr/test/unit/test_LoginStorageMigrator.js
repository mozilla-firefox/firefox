/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Logic/unit tests for LoginStorageMigrator.
 *
 * These exercise the migrator's state machine, error handling and telemetry
 * against in-memory fake storages, so every test is deterministic: the migrator
 * is a one-shot `await run()` with no observers or polling. Real end-to-end
 * data movement against the actual Rust store is covered by
 * browser/browser_login_storage_migrator.js.
 */

"use strict";

const { LoginStorageMigrator } = ChromeUtils.importESModule(
  "resource://gre/modules/LoginStorageMigrator.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const PREF_ENABLED = "signon.storage.rust.enabled";
const PREF_ACTIVE = "signon.storage.rust.active";
const PREF_ATTEMPTS = "signon.storage.rust.migrationAttempts";
const PREF_RESTORE_ENABLED = "signon.storage.rust.restoreEnabled";
const PREF_RESTORE_ATTEMPTS = "signon.storage.rust.restoreAttempts";
const PREF_RESTORE_ATTEMPTS_VERSION =
  "signon.storage.rust.restoreAttemptsVersion";
const PREF_RESTORE_VERSION = "signon.storage.rust.restoreVersion";
const PREF_RESTORE_TARGET_VERSION = "signon.storage.rust.restoreTargetVersion";
const PREF_SYNC_USERNAME = "services.sync.username";
const PREF_PRIMARY_PASSWORD_DEFERRALS =
  "signon.storage.rust.primaryPasswordDeferrals";
const PREF_MAX_PRIMARY_PASSWORD_DEFERRALS =
  "signon.storage.rust.maxPrimaryPasswordDeferrals";
const MAX_RESTORE_ATTEMPTS = 5;

// The version a completed restore records, which is whatever target was asked
// for. Read from the pref so the tests do not duplicate the shipping default.
function restoreTarget() {
  return Services.prefs.getIntPref(PREF_RESTORE_TARGET_VERSION);
}

// Brings the shared state (prefs + telemetry) back to a known-clean baseline.
// Called at the start of every test so a previously failed test can't bleed in.
// signon.storage.rust.enabled is enabled by default, so the enabled/active prefs
// are set explicitly (not cleared) to make every test start from the
// JSON-primary state.
function resetState() {
  Services.prefs.setBoolPref(PREF_ENABLED, false);
  Services.prefs.setBoolPref(PREF_ACTIVE, false);
  Services.prefs.clearUserPref(PREF_ATTEMPTS);
  Services.prefs.clearUserPref(PREF_RESTORE_ENABLED);
  Services.prefs.clearUserPref(PREF_RESTORE_ATTEMPTS);
  Services.prefs.clearUserPref(PREF_RESTORE_ATTEMPTS_VERSION);
  Services.prefs.clearUserPref(PREF_RESTORE_VERSION);
  Services.prefs.clearUserPref(PREF_RESTORE_TARGET_VERSION);
  Services.prefs.clearUserPref(PREF_SYNC_USERNAME);
  Services.prefs.clearUserPref(PREF_PRIMARY_PASSWORD_DEFERRALS);
  Services.prefs.clearUserPref(PREF_MAX_PRIMARY_PASSWORD_DEFERRALS);
  Services.fog.testResetFOG();
}

function makeJsonStorage({
  logins = [],
  vulnerable = [],
  isLoggedIn = true,
  throwOnAdd = null,
  deletedGuids = [],
} = {}) {
  return {
    getAllLoginsCalls: 0,
    isLoggedIn,
    get decryptedPotentiallyVulnerablePasswords() {
      return vulnerable;
    },
    async getAllLogins(_includeDeleted) {
      this.getAllLoginsCalls++;
      return logins;
    },
    // Tombstones are not in `logins`: the real store hides them from
    // searchLoginsAsync too, and this is the only way to see them.
    async loginIsDeletedAsync(guid) {
      return deletedGuids.includes(guid);
    },
    async searchLoginsAsync(matchData) {
      return logins.filter(login => {
        login.QueryInterface(Ci.nsILoginMetaInfo);
        return matchData.guid
          ? login.guid == matchData.guid
          : login.origin == matchData.origin;
      });
    },
    addedLogins: [],
    modifiedLogins: [],
    async addLoginsAsync(newLogins, _continueOnDuplicates) {
      if (throwOnAdd) {
        throw new Error(throwOnAdd);
      }
      this.addedLogins.push(...newLogins);
      return newLogins;
    },
    async modifyLoginAsync(oldLogin, newLoginData) {
      this.modifiedLogins.push([oldLogin, newLoginData]);
    },
  };
}

// `addResults(logins, batchIndex)` lets a test decide per-login success/failure;
// `throwOnRemoveAll` makes the first N removeAllLoginsAsync() calls throw (a
// fatal error); pass Infinity to always throw.
function makeRustStorage({
  addResults = null,
  throwOnRemoveAll = 0,
  throwOnVulnerable = false,
  logins = [],
  loginCount = null,
} = {}) {
  return {
    calls: [],
    // Not recorded in `calls`: counting is the cheap probe the restore starts
    // with, not storage work a test cares about.
    async countLoginsAsync() {
      return loginCount ?? logins.length;
    },
    async getAllLogins(_includeDeleted) {
      this.calls.push("getAllLogins");
      return logins;
    },
    addedBatches: [],
    added: [],
    vulnerable: [],
    removeAllCount: 0,
    async removeAllLoginsAsync() {
      this.removeAllCount++;
      this.calls.push("removeAll");
      this.added = [];
      if (this.removeAllCount <= throwOnRemoveAll) {
        throw new Error("removeAll failed");
      }
    },
    async clearAllPotentiallyVulnerablePasswords() {
      this.calls.push("clearVulnerable");
    },
    async addLoginsWithResultsAsync(logins) {
      this.calls.push("addLogins");
      this.addedBatches.push(logins);
      const results = addResults
        ? addResults(logins, this.addedBatches.length - 1)
        : logins.map(() => ({}));
      logins.forEach((login, i) => {
        if (!results[i] || !results[i].error) {
          this.added.push(login);
        }
      });
      return results;
    },
    async addPotentiallyVulnerablePasswords(passwords) {
      this.calls.push("addVulnerable");
      if (throwOnVulnerable) {
        throw new Error("vulnerable failed");
      }
      this.vulnerable.push(...passwords);
    },
  };
}

// Stands in for the primary password dialog. `accept` decides whether the user
// submits the correct password or cancels. Returns the CID to unregister.
function mockPrimaryPasswordPrompt(accept, onPrompt = () => {}) {
  const prompt = {
    promptPassword(_dialogTitle, _text, password) {
      onPrompt();
      if (accept) {
        password.value = LoginTestUtils.primaryPassword.primaryPassword;
      }
      return accept;
    },
    QueryInterface: ChromeUtils.generateQI(["nsIPrompt"]),
  };
  const windowWatcher = {
    getNewPrompter: () => prompt,
    QueryInterface: ChromeUtils.generateQI(["nsIWindowWatcher"]),
  };
  return MockRegistrar.register(
    "@mozilla.org/embedcomp/window-watcher;1",
    windowWatcher
  );
}

add_setup(function () {
  Services.fog.initializeFOG();
  registerCleanupFunction(resetState);
});

// ---------------------------------------------------------------------------
// State routing
// ---------------------------------------------------------------------------

add_task(async function test_jsonPrimary_returns_json_without_migrating() {
  resetState();
  // enabled defaults to false => JSONPrimary
  const json = makeJsonStorage({ logins: [TestData.formLogin({})] });
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, json, "JSONPrimary returns the JSON store");
  Assert.equal(rust.calls.length, 0, "no migration performed");
  Assert.equal(json.getAllLoginsCalls, 0, "JSON store not read");
});

add_task(async function test_rustPrimary_returns_rust_without_migrating() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage();
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust, "RustPrimary returns the Rust store");
  Assert.equal(rust.calls.length, 0, "no migration performed");
  Assert.equal(json.getAllLoginsCalls, 0, "JSON store not read");
});

add_task(async function test_revertPending_deactivates_rust() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, false);
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage();
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, json, "RevertPending returns the JSON store");
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ACTIVE),
    false,
    "rust deactivated"
  );
  Assert.equal(rust.calls.length, 0, "no migration performed");
});

// A decrypted login as the stores hand it out, with its nsILoginMetaInfo
// fields filled in.
function loginWithMeta({
  guid,
  username = "user",
  password = "pass",
  timePasswordChanged = 1000,
}) {
  const login = TestData.formLogin({ username, password });
  login.QueryInterface(Ci.nsILoginMetaInfo);
  login.guid = guid;
  login.timeCreated = 1000;
  login.timeLastUsed = timePasswordChanged;
  login.timePasswordChanged = timePasswordChanged;
  login.timesUsed = 1;
  return login;
}

add_task(async function test_revert_restores_added_and_changed_logins() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage({
    logins: [
      loginWithMeta({ guid: "{keep}" }),
      loginWithMeta({ guid: "{changed}", password: "old" }),
    ],
  });
  const rust = makeRustStorage({
    logins: [
      loginWithMeta({ guid: "{keep}" }),
      loginWithMeta({
        guid: "{changed}",
        password: "new",
        timePasswordChanged: 2000,
      }),
      loginWithMeta({ guid: "{added}", username: "fresh" }),
    ],
  });

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, json, "RevertPending returns the JSON store");
  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the Rust store is wiped once the logins are restored"
  );
  Assert.deepEqual(
    json.addedLogins.map(login => login.guid),
    ["{added}"],
    "only the login missing from JSON is added"
  );
  Assert.equal(json.modifiedLogins.length, 1, "one login updated");
  Assert.equal(
    json.modifiedLogins[0][1].getProperty("password"),
    "new",
    "the changed password is restored"
  );

  const events = Glean.pwmgr.rustRestoreStatus.testGetValue();
  Assert.equal(events.length, 1, "one restore status event");
  const { extra } = events[0];
  Assert.equal(extra.state, "RevertPending");
  Assert.equal(extra.end_state, "Restored");
  Assert.equal(extra.number_of_logins_to_restore, "3");
  Assert.equal(extra.number_of_logins_added, "1");
  Assert.equal(extra.number_of_logins_updated, "1");
  Assert.equal(extra.number_of_logins_skipped, "1");
  Assert.equal(extra.number_of_logins_failed, "0");
  Assert.equal(extra.number_of_logins_to_delete, "0");
  Assert.equal(extra.attempt, "0", "first attempt");
  Assert.equal(
    extra.restore_version,
    String(restoreTarget()),
    "the pass the record belongs to"
  );
  Assert.ok(!("error_message" in extra), "no error_message on success");
  Assert.greaterOrEqual(Number(extra.duration_ms), 0, "duration_ms recorded");
  Assert.equal(
    Glean.pwmgr.rustRestoreLoginError.testGetValue(),
    null,
    "no login errors recorded"
  );
});

add_task(async function test_restore_keeps_the_newer_json_password() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage({
    logins: [
      loginWithMeta({
        guid: "{a}",
        password: "fixed-by-user",
        timePasswordChanged: 3000,
      }),
    ],
  });
  const rust = makeRustStorage({
    logins: [
      loginWithMeta({
        guid: "{a}",
        password: "old",
        timePasswordChanged: 2000,
      }),
    ],
  });

  await new LoginStorageMigrator(json, rust).run();
  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );

  Assert.equal(json.modifiedLogins.length, 0, "the newer password is kept");
  Assert.equal(json.addedLogins.length, 0, "nothing is added");

  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.end_state, "Restored");
  Assert.equal(extra.number_of_logins_skipped, "1");
});

add_task(async function test_restore_matches_a_recreated_login_by_name() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  // Same origin and username as the Rust login, but re-created by the user, so
  // it has a different guid and a newer password.
  const json = makeJsonStorage({
    logins: [
      loginWithMeta({
        guid: "{recreated}",
        password: "fixed-by-user",
        timePasswordChanged: 3000,
      }),
    ],
  });
  const rust = makeRustStorage({
    logins: [
      loginWithMeta({
        guid: "{a}",
        password: "old",
        timePasswordChanged: 2000,
      }),
    ],
  });

  await new LoginStorageMigrator(json, rust).run();
  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );

  Assert.equal(
    json.addedLogins.length,
    0,
    "no duplicate is added for a login the user re-created"
  );
  Assert.equal(json.modifiedLogins.length, 0, "the newer password is kept");

  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.number_of_logins_skipped, "1");
});

add_task(async function test_restore_leaves_a_deleted_login_deleted() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  // An earlier run restored the login, the user deleted it afterwards, so all
  // the JSON store has left is a tombstone.
  const json = makeJsonStorage({ deletedGuids: ["{a}"] });
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );
  Assert.equal(json.addedLogins.length, 0, "the login is not brought back");
  Assert.equal(json.modifiedLogins.length, 0, "and not modified either");

  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.end_state, "Restored");
  Assert.equal(extra.number_of_logins_skipped, "1");
});

// Logins deleted while Rust was primary are counted, not acted on: the JSON
// store keeps its stale copy and the login survives the revert.
add_task(async function test_restore_counts_the_logins_it_would_delete() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage({
    logins: [
      loginWithMeta({ guid: "{kept}" }),
      loginWithMeta({ guid: "{deleted-in-rust}", username: "gone" }),
    ],
  });
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{kept}" })] });

  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );
  Assert.equal(json.addedLogins.length, 0, "nothing to add");
  Assert.equal(
    json.modifiedLogins.length,
    0,
    "the login Rust no longer has is left alone in the JSON store"
  );

  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.number_of_logins_to_delete, "1");
  Assert.equal(
    extra.number_of_logins_skipped,
    "1",
    "the login both stores still have"
  );
});

add_task(async function test_restore_is_skipped_for_an_empty_rust_store() {
  resetState();
  const json = makeJsonStorage();
  const rust = makeRustStorage();

  await new LoginStorageMigrator(json, rust).run();

  Assert.equal(rust.calls.length, 0, "the Rust store is not read or wiped");
  Assert.equal(json.getAllLoginsCalls, 0, "the JSON store is not decrypted");
  Assert.equal(
    Glean.pwmgr.rustRestoreStatus.testGetValue(),
    null,
    "nothing is reported for the profiles that have no logins to restore"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0),
    restoreTarget(),
    "marked done, so later startups do not even count again"
  );
});

add_task(async function test_restore_keeps_the_rust_store_on_a_login_error() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage({ throwOnAdd: "Invalid login: bad data" });
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  const errors = await TestUtils.waitForCondition(
    () => Glean.pwmgr.rustRestoreLoginError.testGetValue(),
    "the failed login is reported"
  );
  Assert.equal(errors.length, 1, "one login error recorded");
  Assert.equal(
    errors[0].extra.error_message,
    "bad data",
    "error message is normalized"
  );

  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.end_state, "Incomplete");
  Assert.equal(extra.number_of_logins_failed, "1");
  Assert.notEqual(
    Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0),
    restoreTarget(),
    "the restore is not marked done, so the next startup retries"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_RESTORE_ATTEMPTS),
    1,
    "the failed run spent an attempt"
  );
});

// Bug 2065703: a login written while Rust was active may never have reached the
// server, and the JSON store has been frozen since the migration, so Sync being
// configured is not a reason to leave the Rust store where it is.
add_task(async function test_restore_runs_while_sync_is_configured() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  Services.prefs.setStringPref(PREF_SYNC_USERNAME, "user@example.com");
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );
  Assert.equal(json.addedLogins.length, 1, "the login is restored");
  const { extra } = Glean.pwmgr.rustRestoreStatus.testGetValue()[0];
  Assert.equal(extra.end_state, "Restored");
});

// The restored login has to end up queued for upload, or it stays on this
// device only - which is the same data loss one step later.
add_task(async function test_restored_logins_are_marked_for_upload() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  Services.prefs.setStringPref(PREF_SYNC_USERNAME, "user@example.com");
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () => json.addedLogins.length,
    "the login is restored"
  );
  const [restored] = json.addedLogins;
  restored.QueryInterface(Ci.nsILoginMetaInfo);
  Assert.ok(
    !restored.everSynced,
    "the login is not marked as already synced, so the JSON store counts it " +
      "as a local change and Sync uploads it"
  );
});

add_task(async function test_restore_stops_once_the_attempts_are_spent() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  // Spent on the version being asked for, so the budget applies.
  Services.prefs.setIntPref(PREF_RESTORE_ATTEMPTS, MAX_RESTORE_ATTEMPTS);
  Services.prefs.setIntPref(PREF_RESTORE_ATTEMPTS_VERSION, restoreTarget());
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  Assert.equal(rust.calls.length, 0, "the Rust store is not even counted");
  Assert.equal(
    Glean.pwmgr.rustRestoreStatus.testGetValue(),
    null,
    "a restore that is no longer attempted is not reported over and over"
  );
});

// The attempt budget is what makes a raised target a no-op on exactly the
// profiles that need it most, unless it is scoped per version.
add_task(async function test_spent_attempts_do_not_block_a_raised_target() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const previousTarget = restoreTarget();
  Services.prefs.setIntPref(PREF_RESTORE_ATTEMPTS, MAX_RESTORE_ATTEMPTS);
  Services.prefs.setIntPref(PREF_RESTORE_ATTEMPTS_VERSION, previousTarget);
  Services.prefs.setIntPref(PREF_RESTORE_TARGET_VERSION, previousTarget + 1);
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () => json.addedLogins.length,
    "the restore runs on the budget of the new version"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_RESTORE_ATTEMPTS_VERSION),
    previousTarget + 1,
    "the attempt count now belongs to the version being asked for"
  );
});

add_task(async function test_restore_can_be_turned_off_by_pref() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  Services.prefs.setBoolPref(PREF_RESTORE_ENABLED, false);
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  Assert.equal(rust.calls.length, 0, "the kill switch stops everything");
  Assert.equal(Glean.pwmgr.rustRestoreStatus.testGetValue(), null);
});

add_task(async function test_restore_refuses_an_implausibly_large_store() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage();
  const rust = makeRustStorage({
    logins: [loginWithMeta({ guid: "{a}" })],
    loginCount: 10001,
  });

  await new LoginStorageMigrator(json, rust).run();

  const events = await TestUtils.waitForCondition(
    () => Glean.pwmgr.rustRestoreStatus.testGetValue(),
    "the refusal is reported"
  );
  Assert.equal(events[0].extra.end_state, "TooManyLogins");
  Assert.equal(
    events[0].extra.number_of_logins_to_restore,
    "10001",
    "the size of the store it refused is what makes the refusal actionable"
  );
  Assert.ok(
    !rust.calls.includes("getAllLogins"),
    "decrypting the whole store is what we are avoiding, so it is not read"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0),
    restoreTarget(),
    "marked done, so the refusal is not reported on every startup"
  );
});

add_task(async function test_restore_waits_for_the_primary_password() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage({ isLoggedIn: false });
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();

  Assert.ok(!rust.calls.includes("getAllLogins"), "nothing is decrypted");
  const deferred = await TestUtils.waitForCondition(
    () => Glean.pwmgr.rustRestoreStatus.testGetValue(),
    "the deferred restore is reported"
  );
  Assert.equal(deferred[0].extra.end_state, "Deferred");
  Assert.equal(deferred[0].extra.state, "RevertPending");
  Assert.notEqual(
    Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0),
    restoreTarget(),
    "not done, it has not run yet"
  );

  json.isLoggedIn = true;
  Services.obs.notifyObservers(null, "passwordmgr-crypto-login");

  await TestUtils.waitForCondition(
    () => json.addedLogins.length,
    "unlocking restores the login"
  );
  Assert.equal(json.addedLogins[0].guid, "{a}", "the login is restored");

  const events = await TestUtils.waitForCondition(
    () =>
      Glean.pwmgr.rustRestoreStatus.testGetValue()?.length == 2 &&
      Glean.pwmgr.rustRestoreStatus.testGetValue(),
    "the restore after the unlock is reported separately"
  );
  Assert.equal(events[1].extra.end_state, "Restored");
  Assert.equal(
    events[1].extra.state,
    "JSONPrimary",
    "Rust was already deactivated when the user unlocked"
  );
  Assert.notEqual(
    events[0].extra.run_id,
    events[1].extra.run_id,
    "the two runs are told apart by their run_id"
  );
});

add_task(async function test_restore_runs_only_once() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });

  await new LoginStorageMigrator(json, rust).run();
  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === restoreTarget(),
    "the restore ran"
  );
  Assert.equal(json.addedLogins.length, 1, "the login is restored");

  // The Rust database still holds the login, so the pref is the only thing
  // standing between it and a second restore.
  rust.calls.length = 0;
  await new LoginStorageMigrator(json, rust).run();

  Assert.equal(rust.calls.length, 0, "the Rust store is not read again");
  Assert.equal(json.addedLogins.length, 1, "the login is restored just once");
});

// Raising the target is how a profile that already completed the restore is
// asked to run it again, so that a later fix to what the restore recovers can
// reach the profiles the earlier one left behind.
add_task(async function test_raising_the_target_runs_the_restore_again() {
  resetState();
  Services.prefs.setBoolPref(PREF_ACTIVE, true);
  const json = makeJsonStorage();
  const rust = makeRustStorage({ logins: [loginWithMeta({ guid: "{a}" })] });
  const firstTarget = restoreTarget();

  await new LoginStorageMigrator(json, rust).run();
  await TestUtils.waitForCondition(
    () => Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === firstTarget,
    "the restore ran"
  );

  Services.prefs.setIntPref(PREF_RESTORE_TARGET_VERSION, firstTarget + 1);
  rust.calls.length = 0;
  await new LoginStorageMigrator(json, rust).run();

  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0) === firstTarget + 1,
    "the restore ran again and recorded the new target"
  );
  Assert.ok(
    rust.calls.includes("getAllLogins"),
    "the Rust store is read again"
  );

  const events = Glean.pwmgr.rustRestoreStatus.testGetValue();
  Assert.deepEqual(
    events.map(event => event.extra.restore_version),
    [String(firstTarget), String(firstTarget + 1)],
    "each record says which pass it belongs to"
  );
});

add_task(async function test_activating_rust_clears_the_restore_state() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  // Left over from the revert that came before this migration.
  Services.prefs.setIntPref(PREF_RESTORE_VERSION, restoreTarget());
  Services.prefs.setIntPref(PREF_RESTORE_ATTEMPTS, MAX_RESTORE_ATTEMPTS);
  const json = makeJsonStorage({ logins: [TestData.formLogin({})] });
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust, "the migration completed");
  Assert.notEqual(
    Services.prefs.getIntPref(PREF_RESTORE_VERSION, 0),
    restoreTarget(),
    "whatever Rust collects from now on has to be restored by a later revert"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_RESTORE_ATTEMPTS),
    0,
    "and it gets a fresh attempt budget to do it with"
  );
});

add_task(async function test_exceedMigrationBudget_falls_back_to_json() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setBoolPref(PREF_ACTIVE, false);
  Services.prefs.setIntPref(PREF_ATTEMPTS, 10);
  const json = makeJsonStorage({ logins: [TestData.formLogin({})] });
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, json, "exceeded budget returns the JSON store");
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ENABLED),
    false,
    "rust disabled"
  );
  Assert.equal(Services.prefs.getIntPref(PREF_ATTEMPTS), 0, "attempts reset");
  Assert.equal(rust.calls.length, 0, "no migration performed");
});

// ---------------------------------------------------------------------------
// Successful migration
// ---------------------------------------------------------------------------

add_task(async function test_migration_completes_and_reports_status() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true); // active defaults false => Pending
  const logins = [
    TestData.formLogin({ username: "a" }),
    TestData.formLogin({ username: "b" }),
  ];
  const json = makeJsonStorage({ logins, vulnerable: ["vuln1"] });
  const rust = makeRustStorage();

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust, "completed migration returns the Rust store");
  Assert.equal(Services.prefs.getBoolPref(PREF_ACTIVE), true, "rust activated");
  Assert.equal(Services.prefs.getIntPref(PREF_ATTEMPTS), 0, "attempts reset");

  Assert.deepEqual(
    rust.calls.slice(0, 3),
    ["removeAll", "clearVulnerable", "addLogins"],
    "Rust store is cleared before logins are written"
  );
  Assert.equal(rust.added.length, 2, "both logins written to Rust");
  Assert.deepEqual(rust.vulnerable, ["vuln1"], "vulnerable password migrated");

  const events = Glean.pwmgr.rustMigrationStatus.testGetValue();
  Assert.equal(events.length, 1, "one status event");
  const { extra } = events[0];
  Assert.equal(extra.end_state, "RustPrimary");
  Assert.equal(extra.number_of_logins_to_migrate, "2");
  Assert.equal(extra.number_of_logins_migrated, "2");
  Assert.equal(extra.number_of_logins_quarantined, "0");
  Assert.equal(extra.number_of_vulnerable_passwords, "1");
  Assert.equal(extra.attempt, "0");
  Assert.ok(!("error_message" in extra), "no error_message on success");
  Assert.greaterOrEqual(Number(extra.duration_ms), 0, "duration_ms recorded");
});

add_task(async function test_migration_sorts_by_timePasswordChanged_desc() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  const older = TestData.formLogin({
    username: "older",
    timePasswordChanged: 100,
  });
  const newer = TestData.formLogin({
    username: "newer",
    timePasswordChanged: 200,
  });
  const json = makeJsonStorage({ logins: [older, newer] });
  const rust = makeRustStorage();

  await new LoginStorageMigrator(json, rust).run();

  const firstBatch = rust.addedBatches[0];
  Assert.equal(
    firstBatch[0].username,
    "newer",
    "most recently changed password is written first"
  );
  Assert.equal(firstBatch[1].username, "older");
});

// ---------------------------------------------------------------------------
// Per-login failures
// ---------------------------------------------------------------------------

add_task(async function test_migration_quarantines_duplicates() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  const login = TestData.formLogin({
    origin: "https://example.com",
    username: "dup",
  });
  login.QueryInterface(Ci.nsILoginMetaInfo);
  login.guid = "{11111111-1111-1111-1111-111111111111}";
  const json = makeJsonStorage({ logins: [login] });
  // First add reports the login as a duplicate; the rescued retry succeeds.
  const rust = makeRustStorage({
    addResults: (logins, batchIndex) =>
      batchIndex === 0
        ? logins.map(() => ({ error: { message: "Login already exists" } }))
        : logins.map(() => ({})),
  });

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust);
  const rescued = rust.addedBatches[1];
  Assert.equal(rescued.length, 1, "one duplicate rescued");
  Assert.ok(
    rescued[0].origin.startsWith("moz-pwmngr-fixed-"),
    "rescued duplicate origin rewritten to fixed scheme"
  );

  const { extra } = Glean.pwmgr.rustMigrationStatus.testGetValue()[0];
  Assert.equal(extra.number_of_logins_to_migrate, "1");
  Assert.equal(extra.number_of_logins_migrated, "1");
  Assert.equal(extra.number_of_logins_quarantined, "1");
});

add_task(async function test_migration_partial_failure_records_login_error() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  const ok = TestData.formLogin({ username: "ok" });
  const bad = TestData.formLogin({ username: "bad" });
  const json = makeJsonStorage({ logins: [ok, bad] });
  const rust = makeRustStorage({
    addResults: (logins, batchIndex) =>
      batchIndex === 0
        ? [{}, { error: { message: "Invalid login: bad data" } }]
        : logins.map(() => ({})),
  });

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust, "partial failure still completes the migration");
  Assert.equal(rust.addedBatches[1].length, 0, "non-duplicate is not rescued");

  const status = Glean.pwmgr.rustMigrationStatus.testGetValue()[0].extra;
  Assert.equal(status.number_of_logins_migrated, "1");
  Assert.equal(status.number_of_logins_to_migrate, "2");

  const errors = Glean.pwmgr.rustMigrationLoginError.testGetValue();
  Assert.equal(errors.length, 1, "one login error recorded");
  Assert.equal(
    errors[0].extra.error_message,
    "bad data",
    "error message is normalized"
  );
});

// ---------------------------------------------------------------------------
// Fatal failure, retry and abort
// ---------------------------------------------------------------------------

add_task(async function test_migration_fatal_aborts_and_increments_attempts() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setIntPref(PREF_ATTEMPTS, 0);
  const json = makeJsonStorage({ logins: [TestData.formLogin({})] });
  const rust = makeRustStorage({ throwOnRemoveAll: Infinity });

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, json, "fatal migration falls back to the JSON store");
  Assert.equal(
    Services.prefs.getBoolPref(PREF_ACTIVE),
    false,
    "rust not activated"
  );
  Assert.equal(
    Services.prefs.getIntPref(PREF_ATTEMPTS),
    1,
    "attempt counter +1"
  );
  Assert.greater(
    rust.removeAllCount,
    1,
    "migration was retried within the session"
  );

  const events = Glean.pwmgr.rustMigrationStatus.testGetValue();
  Assert.greaterOrEqual(events.length, 1, "status event(s) recorded");
  const { extra } = events.at(-1);
  Assert.equal(extra.end_state, "MigrationPending");
  Assert.ok("error_message" in extra, "fatal error message recorded");
});

add_task(async function test_migration_retries_then_completes() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  const json = makeJsonStorage({ logins: [TestData.formLogin({})] });
  const rust = makeRustStorage({ throwOnRemoveAll: 2 }); // fail twice, then succeed

  const result = await new LoginStorageMigrator(json, rust).run();

  Assert.equal(result, rust, "completes after transient failures");
  Assert.equal(Services.prefs.getBoolPref(PREF_ACTIVE), true, "rust activated");
  Assert.equal(
    Services.prefs.getIntPref(PREF_ATTEMPTS),
    0,
    "attempts reset on success"
  );
});

// ---------------------------------------------------------------------------
// Primary Password
// ---------------------------------------------------------------------------

add_task(async function test_primaryPassword_prompt_accepted_migrates() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setIntPref(PREF_PRIMARY_PASSWORD_DEFERRALS, 2);
  await LoginTestUtils.primaryPassword.enable();
  const cid = mockPrimaryPasswordPrompt(true);
  try {
    const json = makeJsonStorage({
      logins: [TestData.formLogin({})],
      isLoggedIn: false,
    });
    const rust = makeRustStorage();

    const result = await new LoginStorageMigrator(json, rust).run();

    Assert.equal(
      result,
      rust,
      "accepting the Primary Password prompt proceeds with migration"
    );
    Assert.equal(
      Services.prefs.getBoolPref(PREF_ACTIVE),
      true,
      "rust activated"
    );
    const { extra } = Glean.pwmgr.rustMigrationStatus.testGetValue()[0];
    Assert.equal(extra.primary_password_set, "true");
    Assert.equal(
      extra.primary_password_deferrals,
      "2",
      "the run reports the prompts canceled before it"
    );
    Assert.deepEqual(
      Glean.pwmgr.primaryPasswordPrompt.testGetValue().map(e => e.extra),
      [{ source: "migration", result: "success" }],
      "the prompt is recorded"
    );
    Assert.equal(
      Services.prefs.getIntPref(PREF_PRIMARY_PASSWORD_DEFERRALS),
      0,
      "a completed migration resets the canceled prompts"
    );
  } finally {
    MockRegistrar.unregister(cid);
    await LoginTestUtils.primaryPassword.disable();
  }
});

add_task(async function test_primaryPassword_prompt_canceled_defers() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setIntPref(PREF_ATTEMPTS, 3);
  await LoginTestUtils.primaryPassword.enable();
  const cid = mockPrimaryPasswordPrompt(false);
  try {
    const json = makeJsonStorage({
      logins: [TestData.formLogin({})],
      isLoggedIn: false,
    });
    const rust = makeRustStorage();

    const result = await new LoginStorageMigrator(json, rust).run();

    Assert.equal(
      result,
      json,
      "canceling the Primary Password prompt defers to the JSON store"
    );
    Assert.equal(rust.calls.length, 0, "no migration performed");
    Assert.equal(
      Services.prefs.getBoolPref(PREF_ACTIVE),
      false,
      "rust not activated"
    );
    Assert.equal(
      Services.prefs.getIntPref(PREF_ATTEMPTS),
      3,
      "canceling does not consume the attempt budget"
    );
    Assert.equal(
      Services.prefs.getIntPref(PREF_PRIMARY_PASSWORD_DEFERRALS),
      1,
      "the canceled prompt is counted"
    );
    const [deferred] = Glean.pwmgr.rustMigrationStatus.testGetValue();
    Assert.equal(
      deferred.extra.end_state,
      "Deferred",
      "the deferral is recorded"
    );
    Assert.equal(deferred.extra.deferral_reason, "primary_password_canceled");
    Assert.equal(deferred.extra.primary_password_deferrals, "1");
    Assert.equal(deferred.extra.attempt, "3");
    Assert.deepEqual(
      Glean.pwmgr.primaryPasswordPrompt.testGetValue().map(e => e.extra),
      [{ source: "migration", result: "cancel" }],
      "the canceled prompt is recorded"
    );
  } finally {
    MockRegistrar.unregister(cid);
    await LoginTestUtils.primaryPassword.disable();
  }
});

add_task(async function test_primaryPassword_prompt_stops_after_cancels() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  Services.prefs.setIntPref(PREF_MAX_PRIMARY_PASSWORD_DEFERRALS, 2);
  await LoginTestUtils.primaryPassword.enable();
  let prompts = 0;
  const cid = mockPrimaryPasswordPrompt(false, () => prompts++);
  try {
    const json = makeJsonStorage({
      logins: [TestData.formLogin({})],
      isLoggedIn: false,
    });
    const rust = makeRustStorage();

    for (let startup = 0; startup < 4; startup++) {
      const result = await new LoginStorageMigrator(json, rust).run();
      Assert.equal(result, json, "the JSON store keeps serving");
    }

    Assert.equal(prompts, 2, "only prompted until the limit is reached");
    Assert.deepEqual(
      Glean.pwmgr.rustMigrationStatus
        .testGetValue()
        .map(e => [
          e.extra.deferral_reason,
          e.extra.primary_password_deferrals,
        ]),
      [
        ["primary_password_canceled", "1"],
        ["primary_password_canceled", "2"],
        ["primary_password_limit", "2"],
        ["primary_password_limit", "2"],
      ],
      "every startup records why the migration was deferred"
    );
    Assert.equal(
      Services.prefs.getIntPref(PREF_PRIMARY_PASSWORD_DEFERRALS),
      2,
      "the count stops at the limit"
    );
    Assert.equal(rust.calls.length, 0, "no migration performed");
    Assert.equal(
      Services.prefs.getBoolPref(PREF_ENABLED),
      true,
      "rust.enabled is left alone"
    );
    Assert.equal(
      Services.prefs.getIntPref(PREF_ATTEMPTS, 0),
      0,
      "the attempt budget is not touched"
    );

    Services.prefs.setIntPref(PREF_MAX_PRIMARY_PASSWORD_DEFERRALS, 3);
    await new LoginStorageMigrator(json, rust).run();
    Assert.equal(prompts, 3, "raising the limit prompts again");

    Services.prefs.setIntPref(PREF_MAX_PRIMARY_PASSWORD_DEFERRALS, 0);
    for (let startup = 0; startup < 2; startup++) {
      await new LoginStorageMigrator(json, rust).run();
    }
    Assert.equal(prompts, 5, "a limit of 0 prompts on every startup");
  } finally {
    MockRegistrar.unregister(cid);
    await LoginTestUtils.primaryPassword.disable();
  }
});

add_task(async function test_primaryPassword_unlocked_migrates() {
  resetState();
  Services.prefs.setBoolPref(PREF_ENABLED, true);
  const sandbox = sinon.createSandbox();
  sandbox.stub(LoginHelper, "isPrimaryPasswordSet").returns(true);
  try {
    const json = makeJsonStorage({
      logins: [TestData.formLogin({})],
      isLoggedIn: true,
    });
    const rust = makeRustStorage();

    const result = await new LoginStorageMigrator(json, rust).run();

    Assert.equal(result, rust, "unlocked Primary Password migrates");
    Assert.equal(
      Services.prefs.getBoolPref(PREF_ACTIVE),
      true,
      "rust activated"
    );
    const { extra } = Glean.pwmgr.rustMigrationStatus.testGetValue()[0];
    Assert.equal(extra.primary_password_set, "true");
  } finally {
    sandbox.restore();
  }
});
