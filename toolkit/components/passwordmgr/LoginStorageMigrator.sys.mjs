/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

const PREFS = {
  // Intent: Rust is the desired backend.
  RUST_ENABLED: "signon.storage.rust.enabled",
  // Reality: Rust is the active backend right now. Owned by the migrator.
  // Also drives Sync's engine selection (services/sync/modules/service.sys.mjs).
  RUST_ACTIVE: "signon.storage.rust.active",
  MIGRATION_ATTEMPTS: "signon.storage.rust.migrationAttempts",
  // Kill switch, so the restore can be turned off by pref rollout.
  RESTORE_ENABLED: "signon.storage.rust.restoreEnabled",
  RESTORE_ATTEMPTS: "signon.storage.rust.restoreAttempts",
  // The version RESTORE_ATTEMPTS was spent on; the budget is per version.
  RESTORE_ATTEMPTS_VERSION: "signon.storage.rust.restoreAttemptsVersion",
  // The restore version this profile has completed.
  RESTORE_VERSION: "signon.storage.rust.restoreVersion",
  // Raising this asks every profile for one more restore pass (bug 2065703).
  RESTORE_TARGET_VERSION: "signon.storage.rust.restoreTargetVersion",
  // How many startups in a row the Primary Password prompt for the migration
  // was canceled.
  PRIMARY_PASSWORD_DEFERRALS: "signon.storage.rust.primaryPasswordDeferrals",
  // After that many canceled prompts the migration stops prompting; 0 means no
  // limit.
  MAX_PRIMARY_PASSWORD_DEFERRALS:
    "signon.storage.rust.maxPrimaryPasswordDeferrals",
};

const MAX_MIGRATION_ATTEMPTS = 10;
const MAX_RUNTIME_RETRIES = 3;
const MAX_RESTORE_ATTEMPTS = 5;
const MAX_LOGINS_TO_RESTORE = 10000;

// Continues the rust mirror's telemetry version sequence (last was 8); the
// rust_migration_status event is shared with the former mirror.
const telemetryVersion = "10";

// Replace an origin's scheme with `moz-pwmngr-fixed-<prefix extracted from
// login guid>://`.
// Used during migration when two JSON logins collapse onto the same Rust dedup
// key after origin normalization: rewriting the loser's scheme gives it a
// distinct origin in Rust so both records can be persisted.
function rewriteOriginToFixedScheme(origin, guid) {
  const id = guid.replace(/\W/, "").split("-", 1)[0];
  const idx = origin.indexOf("://");
  if (idx === -1) {
    return `moz-pwmngr-fixed-${id}://${origin}`;
  }
  return `moz-pwmngr-fixed-${id}://${origin.slice(idx + 3)}`;
}

// Normalize different Rust storage error messages so similar failures group
// together in telemetry.
function normalizeRustStorageErrorMessage(error) {
  const message = error?.message || String(error);

  return message
    .replace(/^reason: /, "")
    .replace(/^Invalid login: /, "")
    .replace(/\{[0-9a-fA-F-]{36}\}/, "{UUID}");
}

// Records one summary event per migration run. `error` is the fatal error that
// aborted the run, or null if the run completed.
function recordMigrationStatus({
  runId,
  duration,
  numberOfLoginsToMigrate,
  numberOfLoginsMigrated,
  numberOfLoginsQuarantined,
  numberOfVulnerablePasswords,
  attempt,
  endState,
  primaryPasswordSet,
  primaryPasswordDeferrals,
  deferralReason,
  error,
}) {
  Glean.pwmgr.rustMigrationStatus.record({
    metric_version: telemetryVersion,
    run_id: runId,
    duration_ms: duration,
    number_of_logins_to_migrate: numberOfLoginsToMigrate,
    number_of_logins_migrated: numberOfLoginsMigrated,
    number_of_logins_quarantined: numberOfLoginsQuarantined,
    number_of_vulnerable_passwords: numberOfVulnerablePasswords,
    attempt,
    end_state: endState,
    primary_password_set: primaryPasswordSet,
    primary_password_deferrals: primaryPasswordDeferrals,
    deferral_reason: deferralReason,
    error_message: error ? normalizeRustStorageErrorMessage(error) : null,
  });
}

// Records a startup on which the migration did not run because the primary
// password was not entered.
function recordMigrationDeferred(deferralReason, primaryPasswordDeferrals) {
  recordMigrationStatus({
    runId: Services.uuid.generateUUID(),
    attempt: Services.prefs.getIntPref(PREFS.MIGRATION_ATTEMPTS, 0),
    endState: "Deferred",
    primaryPasswordSet: true,
    primaryPasswordDeferrals,
    deferralReason,
  });
}

// Restores the credentials onto a login that already exists in the JSON store.
// A property bag rather than an nsILoginInfo, so that nothing else is clobbered,
// mirroring what the CSV import does in LoginHelper's ImportRowProcessor.
function credentialPropertyBag(login) {
  const bag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
    Ci.nsIWritablePropertyBag
  );
  bag.setProperty("username", login.username);
  bag.setProperty("password", login.password);
  bag.setProperty("timePasswordChanged", login.timePasswordChanged);
  return bag;
}

// Records one event per login that could not be migrated.
function recordMigrationLoginError(runId, error) {
  Glean.pwmgr.rustMigrationLoginError.record({
    metric_version: telemetryVersion,
    run_id: runId,
    error_message: normalizeRustStorageErrorMessage(error),
  });
}

export class LoginStorageMigrator {
  #jsonStorage;
  #rustStorage;
  // Tracks execution progress within a single run() call. Never persisted.
  #retryCount = 0;
  #logger;

  constructor(jsonStorage, rustStorage) {
    this.#jsonStorage = jsonStorage;
    this.#rustStorage = rustStorage;
    this.#logger = lazy.LoginHelper.createLogger("LoginStorageMigrator");
  }

  // Returns the state derived from prefs.
  get #state() {
    const enabled = Services.prefs.getBoolPref(PREFS.RUST_ENABLED, false);
    const active = Services.prefs.getBoolPref(PREFS.RUST_ACTIVE, false);
    if (!enabled) {
      return active ? "RevertPending" : "JSONPrimary";
    }
    return active ? "RustPrimary" : "MigrationPending";
  }

  // Runs the migration if necessary and returns the active storage backend.
  async run() {
    const state = this.#state;
    this.#logger.log(`run: ${state}`);
    switch (state) {
      case "RustPrimary":
        return this.#rustStorage;

      case "MigrationPending":
        if (
          Services.prefs.getIntPref(PREFS.MIGRATION_ATTEMPTS, 0) >=
          MAX_MIGRATION_ATTEMPTS
        ) {
          return this.#exceedMigrationBudget();
        }
        return this.#startMigration();

      case "RevertPending":
        this.#restoreLoginsFromRust();
        return this.#deactivateRust();

      default:
        this.#restoreLoginsFromRust();
        return this.#jsonStorage;
    }
  }

  // If a PrP is set, prompt for it. Then start the migration.
  async #startMigration() {
    if (
      lazy.LoginHelper.isPrimaryPasswordSet() &&
      !this.#jsonStorage.isLoggedIn
    ) {
      const deferrals = Services.prefs.getIntPref(
        PREFS.PRIMARY_PASSWORD_DEFERRALS,
        0
      );
      const maxDeferrals = Services.prefs.getIntPref(
        PREFS.MAX_PRIMARY_PASSWORD_DEFERRALS,
        3
      );
      if (maxDeferrals > 0 && deferrals >= maxDeferrals) {
        this.#logger.log(
          "Migration deferred: primary password prompt canceled too often"
        );
        recordMigrationDeferred("primary_password_limit", deferrals);
        return this.#jsonStorage;
      }
      const token = Cc[
        "@mozilla.org/security/internalkeytoken;1"
      ].createInstance(Ci.nsIPKCS11Token);
      try {
        // Logs in, which prompts for the primary password.
        await token.login();
      } catch (e) {
        // An exception is thrown if the user cancels the prompt.
      }
      Glean.pwmgr.primaryPasswordPrompt.record({
        source: "migration",
        result: token.isLoggedIn ? "success" : "cancel",
      });
      if (!token.isLoggedIn) {
        this.#logger.log("Migration deferred: primary password locked");
        Services.prefs.setIntPref(
          PREFS.PRIMARY_PASSWORD_DEFERRALS,
          deferrals + 1
        );
        recordMigrationDeferred("primary_password_canceled", deferrals + 1);
        return this.#jsonStorage;
      }
    }
    return this.#executeMigration();
  }

  async #executeMigration() {
    this.#logger.log("Starting migration...");

    const t0 = ChromeUtils.now();
    const runId = Services.uuid.generateUUID();
    const attempt = Services.prefs.getIntPref(PREFS.MIGRATION_ATTEMPTS, 0);
    const primaryPasswordSet = lazy.LoginHelper.isPrimaryPasswordSet();
    const primaryPasswordDeferrals = Services.prefs.getIntPref(
      PREFS.PRIMARY_PASSWORD_DEFERRALS,
      0
    );
    let numberOfLoginsToMigrate = 0;
    let numberOfLoginsMigrated = 0;
    let numberOfLoginsQuarantined = 0;
    let numberOfVulnerablePasswords = 0;
    let fatalError = null;
    let endState;

    try {
      await this.#rustStorage.removeAllLoginsAsync();
      await this.#rustStorage.clearAllPotentiallyVulnerablePasswords();

      const logins = await this.#jsonStorage.getAllLogins(false);
      numberOfLoginsToMigrate = logins.length;

      // Sort by timePasswordChanged descending so the most recently changed
      // password wins origin-normalization collisions; older duplicates are
      // quarantined under moz-pwmngr-fixed:// below.
      const sortedLogins = [...logins].sort(
        (a, b) => (b.timePasswordChanged || 0) - (a.timePasswordChanged || 0)
      );

      const results =
        await this.#rustStorage.addLoginsWithResultsAsync(sortedLogins);
      const failedLogins = results
        .map(({ error }, i) => ({
          login: sortedLogins[i],
          error,
          isDuplicate: /Login already exists/i.test(error?.message || ""),
        }))
        .filter(({ error }) => error);

      numberOfLoginsMigrated += results.length - failedLogins.length;

      const duplicates = [];
      for (const { isDuplicate, error, login } of failedLogins) {
        if (isDuplicate) {
          const rescued = login.clone();
          rescued.QueryInterface(Ci.nsILoginMetaInfo);
          rescued.origin = rewriteOriginToFixedScheme(login.origin, login.guid);
          duplicates.push(rescued);
        } else {
          this.#logger.error("Migration error:", error.message);
          recordMigrationLoginError(runId, error);
        }
      }

      const duplicatesResults =
        await this.#rustStorage.addLoginsWithResultsAsync(duplicates);
      for (const { error } of duplicatesResults) {
        if (error) {
          this.#logger.error(
            "Migration error for rescued duplicate:",
            error.message
          );
          recordMigrationLoginError(runId, error);
        } else {
          numberOfLoginsMigrated++;
          numberOfLoginsQuarantined++;
        }
      }

      const potentiallyVulnerablePasswords =
        this.#jsonStorage.decryptedPotentiallyVulnerablePasswords;
      numberOfVulnerablePasswords = potentiallyVulnerablePasswords.length;
      try {
        await this.#rustStorage.addPotentiallyVulnerablePasswords(
          potentiallyVulnerablePasswords
        );
      } catch (e) {
        this.#logger.error("Vulnerable passwords migration error:", e);
      }

      this.#logger.log(
        `Migration complete: ${numberOfLoginsMigrated}/${numberOfLoginsToMigrate} logins` +
          (numberOfLoginsQuarantined
            ? `, ${numberOfLoginsQuarantined} quarantined`
            : "")
      );

      const activeStore = this.#completeMigration();
      endState = this.#state;
      return activeStore;
    } catch (e) {
      this.#logger.error("Migration failed:", e);
      fatalError = e;
      endState = this.#state;
      return this.#failMigration(e);
    } finally {
      recordMigrationStatus({
        runId,
        duration: Math.round(ChromeUtils.now() - t0),
        numberOfLoginsToMigrate,
        numberOfLoginsMigrated,
        numberOfLoginsQuarantined,
        numberOfVulnerablePasswords,
        attempt,
        endState,
        primaryPasswordSet,
        primaryPasswordDeferrals,
        error: fatalError,
      });
    }
  }

  #completeMigration() {
    Services.prefs.setBoolPref(PREFS.RUST_ACTIVE, true);
    Services.prefs.setIntPref(PREFS.MIGRATION_ATTEMPTS, 0);
    Services.prefs.setIntPref(PREFS.RESTORE_VERSION, 0);
    Services.prefs.setIntPref(PREFS.RESTORE_ATTEMPTS, 0);
    Services.prefs.setIntPref(PREFS.PRIMARY_PASSWORD_DEFERRALS, 0);
    return this.#rustStorage;
  }

  async #failMigration(_error) {
    if (this.#retryCount < MAX_RUNTIME_RETRIES) {
      return this.#retryMigration();
    }
    return this.#abortMigration();
  }

  async #retryMigration() {
    this.#retryCount++;
    this.#logger.log(
      `Retrying migration (${this.#retryCount}/${MAX_RUNTIME_RETRIES})`
    );
    return this.#executeMigration();
  }

  #abortMigration() {
    this.#logger.log("Migration aborted; will retry on next startup");
    Services.prefs.setIntPref(
      PREFS.MIGRATION_ATTEMPTS,
      Services.prefs.getIntPref(PREFS.MIGRATION_ATTEMPTS, 0) + 1
    );
    // rust.enabled is not reset — next startup retries automatically.
    return this.#jsonStorage;
  }

  #exceedMigrationBudget() {
    this.#logger.log("Migration budget exceeded; disabling Rust backend");
    Services.prefs.setBoolPref(PREFS.RUST_ENABLED, false);
    Services.prefs.setIntPref(PREFS.MIGRATION_ATTEMPTS, 0);
    return this.#jsonStorage;
  }

  // Rust is no longer the desired backend but is still active. Logins written
  // while Rust was active are restored by #restoreLoginsFromRust.
  #deactivateRust() {
    this.#logger.log("Deactivating Rust backend");
    Services.prefs.setBoolPref(PREFS.RUST_ACTIVE, false);
    return this.#jsonStorage;
  }

  // Copies logins the JSON store doesn't have out of the disabled Rust store.
  // The Rust database is left alone; the recorded restore version is what makes
  // this a real no-op from then on, without even the count.
  // Runs whether or not Sync is configured: leaving it to Sync only covers what
  // reached the server before the switch back, and nothing guarantees a sync
  // happened in between (bug 2065703).
  // Independent of the returned store and deliberately not awaited: it must not
  // hold up storage initialization, and if it fails nothing is lost - the
  // database stays and the next startup retries. Counting here does not
  // decrypt, so the common case of an empty store never touches the primary
  // password.
  async #restoreLoginsFromRust() {
    const targetVersion = Services.prefs.getIntPref(
      PREFS.RESTORE_TARGET_VERSION,
      0
    );
    if (
      Services.prefs.getIntPref(PREFS.RESTORE_VERSION, 0) >= targetVersion ||
      !Services.prefs.getBoolPref(PREFS.RESTORE_ENABLED, true)
    ) {
      return;
    }

    if (
      Services.prefs.getIntPref(PREFS.RESTORE_ATTEMPTS_VERSION, 0) <
      targetVersion
    ) {
      Services.prefs.setIntPref(PREFS.RESTORE_ATTEMPTS_VERSION, targetVersion);
      Services.prefs.setIntPref(PREFS.RESTORE_ATTEMPTS, 0);
    }

    const attempt = Services.prefs.getIntPref(PREFS.RESTORE_ATTEMPTS, 0);
    if (attempt >= MAX_RESTORE_ATTEMPTS) {
      return;
    }

    // Read before the first await
    const state = this.#state;
    const runId = Services.uuid.generateUUID();
    const startedAt = ChromeUtils.now();
    // The count the run is reported with, so the end states that give up
    // before reading the store still say how much they gave up on.
    let loginsInRust = 0;
    let added = 0;
    let updated = 0;
    let skipped = 0;
    let failed = 0;
    let toDelete = 0;
    let fatalError = null;
    // Stays null while there is nothing worth reporting, which is the case for
    // every profile that never had the Rust backend enabled.
    let endState = null;

    try {
      loginsInRust = await this.#rustStorage.countLoginsAsync("", "", "");
      if (!loginsInRust) {
        Services.prefs.setIntPref(PREFS.RESTORE_VERSION, targetVersion);
        return;
      }
      if (loginsInRust > MAX_LOGINS_TO_RESTORE) {
        Services.prefs.setIntPref(PREFS.RESTORE_VERSION, targetVersion);
        endState = "TooManyLogins";
        return;
      }
      if (!this.#jsonStorage.isLoggedIn) {
        // Decrypting would prompt for the primary password, so defer
        this.#restoreOnUnlock();
        endState = "Deferred";
        return;
      }

      // prevent retries on every startup forever
      Services.prefs.setIntPref(PREFS.RESTORE_ATTEMPTS, attempt + 1);

      const rustLogins = await this.#rustStorage.getAllLogins(false);

      // What carrying the deletions over would cost: the JSON logins Rust no
      // longer has, which is everything the user deleted while Rust was
      // primary plus anything the migration failed to copy. Only counted for
      // now, so that bug 2072278 can decide whether deleting them is safe.
      const rustGuids = new Set(
        rustLogins.map(login => login.QueryInterface(Ci.nsILoginMetaInfo).guid)
      );
      const jsonLogins = await this.#jsonStorage.getAllLogins(false);
      toDelete = jsonLogins.filter(
        login => !rustGuids.has(login.QueryInterface(Ci.nsILoginMetaInfo).guid)
      ).length;

      for (const rustLogin of rustLogins) {
        // Between two logins is the only point where stopping leaves both
        // stores in a state the next run can pick up from.
        if (Services.startup.shuttingDown) {
          endState = "Aborted";
          break;
        }
        try {
          rustLogin.QueryInterface(Ci.nsILoginMetaInfo);
          if (await this.#jsonStorage.loginIsDeletedAsync(rustLogin.guid)) {
            skipped++;
            continue;
          }
          const jsonLogin = await this.#findInJsonStore(rustLogin);

          if (!jsonLogin) {
            // addLoginsAsync rejects duplicates itself and returns nothing for
            // them, so even a missed lookup cannot create a second login.
            const [result] = await this.#jsonStorage.addLoginsAsync(
              [rustLogin],
              true
            );
            if (result) {
              added++;
            } else {
              skipped++;
            }
          } else if (
            (jsonLogin.username != rustLogin.username ||
              jsonLogin.password != rustLogin.password) &&
            rustLogin.timePasswordChanged > jsonLogin.timePasswordChanged
          ) {
            await this.#jsonStorage.modifyLoginAsync(
              jsonLogin,
              credentialPropertyBag(rustLogin)
            );
            updated++;
          } else {
            // Identical, or the JSON store holds the newer credentials because
            // the user changed them in the meantime. Leave it alone.
            skipped++;
          }
        } catch (e) {
          this.#logger.error("Restore error:", e.message);
          Glean.pwmgr.rustRestoreLoginError.record({
            metric_version: telemetryVersion,
            run_id: runId,
            error_message: normalizeRustStorageErrorMessage(e),
          });
          failed++;
        }
      }

      if (!endState) {
        // Only mark the store as restored if we're complete
        if (failed) {
          endState = "Incomplete";
        } else {
          Services.prefs.setIntPref(PREFS.RESTORE_VERSION, targetVersion);
          endState = "Restored";
        }
      }
    } catch (e) {
      this.#logger.error("Restoring logins from Rust failed:", e);
      fatalError = e;
      endState = "Failed";
    } finally {
      if (endState) {
        this.#logger.log(
          `Restore ${endState}: ${added} added, ${updated} updated, ` +
            `${skipped} skipped, ${failed} failed, ` +
            `${toDelete} to delete`
        );
        Glean.pwmgr.rustRestoreStatus.record({
          metric_version: telemetryVersion,
          run_id: runId,
          duration_ms: Math.round(ChromeUtils.now() - startedAt),
          restore_version: targetVersion,
          attempt,
          state,
          end_state: endState,
          number_of_logins_to_restore: loginsInRust,
          number_of_logins_added: added,
          number_of_logins_updated: updated,
          number_of_logins_skipped: skipped,
          number_of_logins_failed: failed,
          number_of_logins_to_delete: toDelete,
          primary_password_set: lazy.LoginHelper.isPrimaryPasswordSet(),
          error_message: fatalError
            ? normalizeRustStorageErrorMessage(fatalError)
            : null,
        });
      }
    }
  }

  // The JSON store's counterpart of a Rust login: the same record by guid, or,
  // if the user deleted and re-created it in the meantime, the login with the
  // same name. Mirrors the lookups LoginHelper's ImportRowProcessor does.
  async #findInJsonStore(rustLogin) {
    const [sameGuid] = await this.#jsonStorage.searchLoginsAsync({
      guid: rustLogin.guid,
    });
    if (sameGuid) {
      return sameGuid.QueryInterface(Ci.nsILoginMetaInfo);
    }
    // formActionOrigin and httpRealm may be empty or null, in which case they
    // are ignored by the search.
    const candidates = await this.#jsonStorage.searchLoginsAsync({
      origin: rustLogin.origin,
      formActionOrigin: rustLogin.formActionOrigin,
      httpRealm: rustLogin.httpRealm,
    });
    const sameName = candidates.find(
      login => login.username == rustLogin.username
    );
    return sameName?.QueryInterface(Ci.nsILoginMetaInfo);
  }

  // The observer is registered strongly, since nothing else keeps the migrator
  // alive, so it also has to be dropped when the profile goes away and the user
  // never unlocked.
  #restoreOnUnlock() {
    const topics = ["passwordmgr-crypto-login", "profile-before-change"];
    const observer = {
      observe: (_subject, topic) => {
        topics.forEach(t => Services.obs.removeObserver(observer, t));
        if (topic == "passwordmgr-crypto-login") {
          this.#restoreLoginsFromRust();
        }
      },
    };
    topics.forEach(t => Services.obs.addObserver(observer, t));
  }
}
