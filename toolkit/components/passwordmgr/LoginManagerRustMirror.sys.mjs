/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

const rustMirrorTelemetryVersion = "7";

/* Normalize different errors */
function normalizeRustStorageErrorMessage(error) {
  const message = error?.message || String(error);

  return message
    .replace(/^reason: /, "")
    .replace(/^Invalid login: /, "")
    .replace(/\{[0-9a-fA-F-]{36}\}/, "{UUID}");
}

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

//Normalize a Unix timestamp (ms) to the first day of its month at 00:00 UTC
function roundToMonthUTC(timestampMs) {
  if (!timestampMs) {
    return null;
  }

  const d = new Date(timestampMs);
  return Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), 1, 0, 0, 0, 0);
}

function recordMirrorFailure(runId, operation, error, login = null) {
  // lookup poisoned status
  const poisoned = Services.prefs.getBoolPref(
    "signon.rustMirror.poisoned",
    false
  );

  const data = {
    metric_version: rustMirrorTelemetryVersion,
    run_id: runId,
    operation,
    poisoned,

    error_message: normalizeRustStorageErrorMessage(error),

    is_deleted: false,

    has_origin: false,
    has_form_action_origin: false,
    has_http_realm: false,

    time_created: null,
    time_last_used: null,
  };

  if (login) {
    const timeCreated = roundToMonthUTC(login.timeCreated);
    const timeLastUsed = roundToMonthUTC(login.timeLastUsed);
    data.is_deleted = login.deleted;

    data.has_origin = !!login.origin;
    data.has_form_action_origin = !!login.formActionOrigin;
    data.has_http_realm = !!login.httpRealm;

    data.time_created = timeCreated;
    data.time_last_used = timeLastUsed;
  }

  Glean.pwmgr.rustWriteFailure.record(data);

  // set poisoned status on error
  if (!poisoned) {
    Services.prefs.setBoolPref("signon.rustMirror.poisoned", true);
  }
}

function recordMirrorStatus(runId, operation, status) {
  Glean.pwmgr.rustMirrorStatus.record({
    metric_version: rustMirrorTelemetryVersion,
    run_id: runId,
    operation,
    status,
  });
}

function recordMigrationStatus(
  runId,
  duration,
  numberOfLoginsToMigrate,
  numberOfLoginsMigrated,
  numberOfLoginsQuarantined
) {
  const had_errors = numberOfLoginsMigrated < numberOfLoginsToMigrate;

  Glean.pwmgr.rustMigrationStatus.record({
    metric_version: rustMirrorTelemetryVersion,
    run_id: runId,
    duration_ms: duration,
    number_of_logins_to_migrate: numberOfLoginsToMigrate,
    number_of_logins_migrated: numberOfLoginsMigrated,
    number_of_logins_quarantined: numberOfLoginsQuarantined,
    had_errors,
  });
}

export class LoginManagerRustMirror {
  #logger = null;
  #jsonStorage = null;
  #rustStorage = null;
  #isEnabled = false;
  #migrationInProgress = false;
  #observer = null;

  constructor(jsonStorage, rustStorage) {
    this.#logger = lazy.LoginHelper.createLogger("LoginManagerRustMirror");
    this.#jsonStorage = jsonStorage;
    this.#rustStorage = rustStorage;

    Services.prefs.addObserver("signon.rustMirror.enabled", () =>
      this.#maybeEnable(this)
    );

    this.#logger.log("Rust Mirror is ready.");

    this.#maybeEnable();
  }

  #removeJsonStoreObserver() {
    if (this.#observer) {
      Services.obs.removeObserver(
        this.#observer,
        "passwordmgr-storage-changed"
      );
      this.#observer = null;
    }
  }

  #addJsonStoreObserver() {
    if (!this.#observer) {
      this.#observer = (subject, _, eventName) =>
        this.#onJsonStorageChanged(eventName, subject);
      Services.obs.addObserver(this.#observer, "passwordmgr-storage-changed");
    }
  }

  #maybeEnable() {
    const enabled =
      Services.prefs.getBoolPref("signon.rustMirror.enabled", true) &&
      !lazy.LoginHelper.isPrimaryPasswordSet();

    return enabled ? this.enable() : this.disable();
  }

  async enable() {
    if (this.#isEnabled) {
      return;
    }

    this.#removeJsonStoreObserver();
    this.#isEnabled = true;

    try {
      await this.#maybeRunMigration();
      this.#addJsonStoreObserver();
      this.#logger.log("Rust Mirror is enabled.");
    } catch (e) {
      this.#logger.error("Login migration failed", e);
    }
  }

  disable() {
    if (!this.#isEnabled) {
      return;
    }

    this.#removeJsonStoreObserver();

    this.#isEnabled = false;
    this.#logger.log("Rust Mirror is disabled.");

    // Since we'll miss updates we'll need to migrate again once disabled
    Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
  }

  async #onJsonStorageChanged(eventName, subject) {
    this.#logger.log(`received change event ${eventName}...`);

    // eg in case a primary password has been set after enabling
    if (!this.#isEnabled || lazy.LoginHelper.isPrimaryPasswordSet()) {
      this.#logger.log("Mirror is not active. Change will not be mirrored.");
      return;
    }

    if (this.#migrationInProgress) {
      this.#logger.log(`Migration in progress, skipping event ${eventName}`);
      return;
    }

    const runId = Services.uuid.generateUUID();
    let loginToModify;
    let newLoginData;
    let status = "success";

    switch (eventName) {
      case "addLogin":
        this.#logger.log(`adding login ${subject.guid}...`);
        try {
          await this.#rustStorage.addLoginsAsync([subject]);
          this.#logger.log(`added login ${subject.guid}.`);
        } catch (e) {
          status = "failure";
          recordMirrorFailure(runId, "add", e, subject);
          this.#logger.error("mirror-error:", e);
        }
        recordMirrorStatus(runId, "add", status);
        break;

      case "modifyLogin":
        loginToModify = subject.queryElementAt(0, Ci.nsILoginInfo);
        newLoginData = subject.queryElementAt(1, Ci.nsILoginInfo);
        this.#logger.log(`modifying login ${loginToModify.guid}...`);
        try {
          await this.#rustStorage.modifyLoginAsync(loginToModify, newLoginData);
          this.#logger.log(`modified login ${loginToModify.guid}.`);
        } catch (e) {
          status = "failure";
          recordMirrorFailure(runId, "modify", e, newLoginData);
          this.#logger.error("error: modifyLogin:", e);
        }
        recordMirrorStatus(runId, "modify", status);
        break;

      case "removeLogin":
        this.#logger.log(`removing login ${subject.guid}...`);
        try {
          await this.#rustStorage.removeLoginAsync(subject);
          this.#logger.log(`removed login ${subject.guid}.`);
        } catch (e) {
          status = "failure";
          recordMirrorFailure(runId, "remove", e, subject);
          this.#logger.error("error: removeLogin:", e);
        }
        recordMirrorStatus(runId, "remove", status);
        break;

      case "removeAllLogins":
        this.#logger.log("removing all logins...");
        try {
          await this.#rustStorage.removeAllLoginsAsync();
          this.#logger.log("removed all logins.");
        } catch (e) {
          status = "failure";
          this.#logger.error("error: removeAllLogins:", e);
          recordMirrorFailure(runId, "remove-all", e);
        }
        recordMirrorStatus(runId, "remove-all", status);
        break;

      // re-migrate on importLogins event
      case "importLogins":
        this.#logger.log("re-migrating logins after import...");
        await this.#migrate();
        break;

      case "addPotentiallyVulnerablePassword":
        this.#logger.log(
          `adding ${subject.guid} to potentially vulnerable passwords...`
        );
        try {
          await this.#rustStorage.addPotentiallyVulnerablePassword(subject);
          this.#logger.log(
            `added ${subject.guid} to potentially vulnerable passwords.`
          );
        } catch (e) {
          status = "failure";
          recordMirrorFailure(
            runId,
            "addPotentiallyVulnerablePassword",
            e,
            subject
          );
          this.#logger.error("mirror-error:", e);
        }
        recordMirrorStatus(runId, "addPotentiallyVulnerablePassword", status);
        break;

      case "clearAllPotentiallyVulnerablePasswords":
        this.#logger.log("clearing all potentially vulnerable passwords");
        try {
          await this.#rustStorage.clearAllPotentiallyVulnerablePasswords();
          this.#logger.log("cleared all potentially vulnerable passwords");
        } catch (e) {
          status = "failure";
          recordMirrorFailure(
            runId,
            "clearAllPotentiallyVulnerablePasswords",
            e
          );
          this.#logger.error("mirror-error:", e);
        }
        recordMirrorStatus(
          runId,
          "clearAllPotentiallyVulnerablePasswords",
          status
        );
        break;

      default:
        this.#logger.error(`error: received unhandled event "${eventName}"`);
        break;
    }

    Services.obs.notifyObservers(
      null,
      `rust-mirror.event.${eventName}.finished`
    );
  }

  async #maybeRunMigration() {
    if (!this.#isEnabled || lazy.LoginHelper.isPrimaryPasswordSet()) {
      this.#logger.log("Mirror is not active. Migration will not run.");
      return;
    }

    const migrationNeeded = Services.prefs.getBoolPref(
      "signon.rustMirror.migrationNeeded",
      false
    );

    // eg in case a primary password has been set after enabling
    if (!migrationNeeded) {
      this.#logger.log("No migration needed.");
      return;
    }

    this.#logger.log("Migration is needed");

    await this.#migrate();
  }

  /**
   * Migrates logins from JSON storage to Rust storage.
   *
   * This migration is run once per profile (and can be re-run via
   * ProfileDataUpgrader.sys.mjs).
   *
   * Note: This will perform encryption operations; therefore can trigger
   * primary password UI. However, by now primary password is excluded,
   * as it only runs if no primary password is set.
   */
  async #migrate() {
    if (this.#migrationInProgress) {
      this.#logger.log("Migration already in progress.");
      return;
    }

    this.#logger.log("Starting migration...");

    // We ignore events during migration run. Once we switch the
    // stores over, we will run an initial migration again to ensure
    // consistancy.
    this.#migrationInProgress = true;

    const t0 = Date.now();
    const runId = Services.uuid.generateUUID();
    let numberOfLoginsToMigrate = 0;
    let numberOfLoginsMigrated = 0;
    let numberOfLoginsQuarantined = 0;

    try {
      await this.#rustStorage.removeAllLoginsAsync();
      await this.#rustStorage.clearAllPotentiallyVulnerablePasswords();

      this.#logger.log("Cleared existing Rust logins.");

      Services.prefs.setBoolPref("signon.rustMirror.poisoned", false);

      // get all logins; exclude deletion stubs
      const logins = await this.#jsonStorage.getAllLogins(false);
      numberOfLoginsToMigrate = logins.length;

      // AS Logins's origins are normalized, so they get implicitely merged,
      // because origin is part of the key.
      // Sort by timePasswordChanged descending so the login with the most
      // recently changed password wins the bulk-add race; older-password
      // duplicates are quarantined under moz-pwmngr-fixed:// later.
      const sortedLogins = [...logins].sort(
        (a, b) => (b.timePasswordChanged || 0) - (a.timePasswordChanged || 0)
      );

      const results = await this.#rustStorage.addLoginsAsync(
        sortedLogins,
        true
      );
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
          this.#logger.error("error during migration:", error.message);
          recordMirrorFailure(runId, "migration", error, login);
        }
      }

      const duplicatesResults = await this.#rustStorage.addLoginsAsync(
        duplicates,
        true
      );

      for (const [i, { error }] of duplicatesResults.entries()) {
        if (error) {
          this.#logger.error(
            "error during migration of rescued duplicate:",
            error.message
          );
          recordMirrorFailure(runId, "migration", error, duplicates[i]);
        } else {
          numberOfLoginsMigrated += 1;
          numberOfLoginsQuarantined += 1;
        }
      }

      if (numberOfLoginsQuarantined) {
        this.#logger.log(
          `Quarantined ${numberOfLoginsQuarantined} duplicate logins during migration.`
        );
      }

      this.#logger.log(
        `Successfully migrated ${numberOfLoginsMigrated}/${numberOfLoginsToMigrate} logins.`
      );

      const potentiallyVulnerablePasswords =
        this.#jsonStorage.decryptedPotentiallyVulnerablePasswords;

      try {
        await this.#rustStorage.addPotentiallyVulnerablePasswords(
          potentiallyVulnerablePasswords
        );

        this.#logger.log(
          `Successfully migrated ${potentiallyVulnerablePasswords.length} potentially vulnerable passwords.`
        );
      } catch (e) {
        this.#logger.error(
          "potentially vulnerable passwords migration error:",
          e
        );
      }

      // Migration complete, don't run again
      Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", false);

      this.#logger.log("Migration complete.");
    } catch (e) {
      Services.prefs.setBoolPref("signon.rustMirror.poisoned", true);
      this.#logger.error("migration error:", e);
    } finally {
      const duration = Date.now() - t0;
      recordMigrationStatus(
        runId,
        duration,
        numberOfLoginsToMigrate,
        numberOfLoginsMigrated,
        numberOfLoginsQuarantined
      );
      this.#migrationInProgress = false;

      // Notify about the finished migration. This is used in tests.
      Services.obs.notifyObservers(null, "rust-mirror.migration.finished");
    }
  }
}
