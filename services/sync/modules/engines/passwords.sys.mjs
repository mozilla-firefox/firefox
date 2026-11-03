/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { CryptoWrapper } from "resource://services-sync/record.sys.mjs";

import {
  MASTER_PASSWORD_LOCKED,
  SCORE_INCREMENT_XLARGE,
} from "resource://services-sync/constants.sys.mjs";
import { CollectionValidator } from "resource://services-sync/collection_validator.sys.mjs";
import { BridgedEngine } from "resource://services-sync/bridged_engine.sys.mjs";
import {
  Changeset,
  Store,
  SyncEngine,
  Tracker,
} from "resource://services-sync/engines.sys.mjs";
import { Svc, Utils } from "resource://services-sync/util.sys.mjs";

// These are valid fields the server could have for a logins record
// we mainly use this to detect if there are any unknownFields and
// store (but don't process) those fields to roundtrip them back
const VALID_LOGIN_FIELDS = [
  "id",
  "displayOrigin",
  "formSubmitURL",
  "formActionOrigin",
  "httpRealm",
  "hostname",
  "origin",
  "password",
  "passwordField",
  "timeCreated",
  "timeLastUsed",
  "timePasswordChanged",
  "timesUsed",
  "username",
  "usernameField",
  "everSynced",
  "syncCounter",
  "unknownFields",
];

import { LoginManagerStorage } from "resource://passwordmgr/passwordstorage.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  setupLoggerForTarget: "resource://gre/modules/AppServicesTracing.sys.mjs",
});

// Sync and many tests rely on having an time that is rounded to the nearest
// 100th of a second otherwise tests can fail intermittently.
function roundTimeForSync(time) {
  return Math.round(time / 10) / 100;
}

export function LoginRec(collection, id) {
  CryptoWrapper.call(this, collection, id);
}

LoginRec.prototype = {
  _logName: "Sync.Record.Login",

  cleartextToString() {
    let o = Object.assign({}, this.cleartext);
    if (o.password) {
      o.password = "X".repeat(o.password.length);
    }
    return JSON.stringify(o);
  },
};
Object.setPrototypeOf(LoginRec.prototype, CryptoWrapper.prototype);

Utils.deferGetSet(LoginRec, "cleartext", [
  "hostname",
  "formSubmitURL",
  "httpRealm",
  "username",
  "password",
  "usernameField",
  "passwordField",
  "timeCreated",
  "timePasswordChanged",
]);

export function PasswordEngine(service) {
  SyncEngine.call(this, "Passwords", service);
}

PasswordEngine.prototype = {
  _storeObj: PasswordStore,
  _trackerObj: PasswordTracker,
  _recordObj: LoginRec,

  syncPriority: 2,

  emptyChangeset() {
    return new PasswordsChangeset();
  },

  async initialize() {
    await SyncEngine.prototype.initialize.call(this);
    await Services.logins.initializationPromise;
  },

  async ensureCurrentSyncID(newSyncID) {
    return Services.logins.ensureCurrentSyncID(newSyncID);
  },

  async getLastSync() {
    let legacyValue = await super.getLastSync();
    if (legacyValue) {
      await this.setLastSync(legacyValue);
      Svc.PrefBranch.clearUserPref(this.name + ".lastSync");
      this._log.debug(
        `migrated timestamp of ${legacyValue} to the logins store`
      );
      return legacyValue;
    }
    return this._store.storage.getLastSync();
  },

  async setLastSync(timestamp) {
    await this._store.storage.setLastSync(timestamp);
  },

  // Testing function to emulate that a login has been synced.
  async markSynced(guid) {
    this._store.storage.resetSyncCounter(guid, 0);
  },

  async pullAllChanges() {
    return this._getChangedIDs(true);
  },

  async getChangedIDs() {
    return this._getChangedIDs(false);
  },

  async _getChangedIDs(getAll) {
    let changes = {};

    let logins = await this._store.storage.getAllLogins(true);
    for (let login of logins) {
      if (getAll || login.syncCounter > 0) {
        if (Utils.getSyncCredentialsHosts().has(login.origin)) {
          continue;
        }

        changes[login.guid] = {
          counter: login.syncCounter, // record the initial counter value
          modified: roundTimeForSync(login.timePasswordChanged),
          deleted: await this._store.storage.loginIsDeletedAsync(login.guid),
        };
      }
    }

    return changes;
  },

  async trackRemainingChanges() {
    // Reset the syncCounter on the items that were changed.
    for (let [guid, { counter, synced }] of Object.entries(
      this._modified.changes
    )) {
      if (synced) {
        this._store.storage.resetSyncCounter(guid, counter);
      }
    }
  },

  async _findDupe(item) {
    let login = this._store._nsLoginInfoFromRecord(item);
    if (!login) {
      return null;
    }

    let logins = await this._store.storage.searchLoginsAsync({
      origin: login.origin,
      formActionOrigin: login.formActionOrigin,
      httpRealm: login.httpRealm,
    });

    // Look for existing logins that match the origin, but ignore the password.
    for (let local of logins) {
      if (login.matches(local, true) && local instanceof Ci.nsILoginMetaInfo) {
        return local.guid;
      }
    }

    return null;
  },

  _deleteId(id) {
    this._noteDeletedId(id);
  },

  getValidator() {
    return new PasswordValidator();
  },
};
Object.setPrototypeOf(PasswordEngine.prototype, SyncEngine.prototype);

// Needs to be kept in-sync with the Rust logins storage version.
const STORAGE_VERSION = 1;

// A "bridged engine" backed by the Rust logins component. Used instead of
// PasswordEngine when `signon.storage.rust.active` is set.
// Uses RustPasswordTracker for change scoring.
export function RustPasswordEngine(service) {
  BridgedEngine.call(this, "Passwords", service);
}

RustPasswordEngine.prototype = {
  _trackerObj: RustPasswordTracker,

  syncPriority: 2,

  // Distinguishes the Rust-backed engine from the legacy JS engine in telemetry.
  overrideTelemetryName: "rust-logins",

  async initialize() {
    await SyncEngine.prototype.initialize.call(this);
    await Services.logins.initializationPromise;

    // Forward the Rust logins component's tracing output to this engine's log.
    lazy.setupLoggerForTarget("logins", this._log);

    this._rustStore = LoginManagerStorage.getActiveStore();
    this._bridge = await this._rustStore.bridgedEngine();

    this._bridge.storageVersion = STORAGE_VERSION;
    this._bridge.allowSkippedRecord = true;
    this._bridge.getSyncId = async () => this._bridge.syncId();

    this._log.info("Got a bridged engine!");
    this._tracker.modified = true;
  },

  // A background sync must never trigger an interactive Primary Password prompt
  // from the Rust logins store. When the Primary Password is locked, record the
  // master-password-locked login state so the scheduler backs off (matching the
  // legacy PasswordEngine) and skip this sync without prompting. Syncing resumes
  // automatically once the Primary Password is unlocked.
  async _sync() {
    if (!Services.logins.isLoggedIn) {
      this._log.info(
        "Skipping passwords sync because the Primary Password is locked."
      );
      this.service.status.login = MASTER_PASSWORD_LOCKED;
      return;
    }
    await super._sync();
  },
};
Object.setPrototypeOf(RustPasswordEngine.prototype, BridgedEngine.prototype);

function PasswordStore(name, engine) {
  Store.call(this, name, engine);
  this._nsLoginInfo = new Components.Constructor(
    "@mozilla.org/login-manager/loginInfo;1",
    Ci.nsILoginInfo,
    "init"
  );
}
PasswordStore.prototype = {
  _newPropertyBag() {
    return Cc["@mozilla.org/hash-property-bag;1"].createInstance(
      Ci.nsIWritablePropertyBag2
    );
  },

  // Returns an stringified object of any fields not "known" by this client
  // mainly used to to prevent data loss for other clients by roundtripping
  // these fields without processing them
  _processUnknownFields(record) {
    let unknownFields = {};
    let keys = Object.keys(record);
    keys
      .filter(key => !VALID_LOGIN_FIELDS.includes(key))
      .forEach(key => {
        unknownFields[key] = record[key];
      });
    // If we found some unknown fields, we stringify it to be able
    // to properly encrypt it for roundtripping since we can't know if
    // it contained sensitive fields or not
    if (Object.keys(unknownFields).length) {
      return JSON.stringify(unknownFields);
    }
    return null;
  },

  /**
   * Return an instance of nsILoginInfo (and, implicitly, nsILoginMetaInfo).
   */
  _nsLoginInfoFromRecord(record) {
    function nullUndefined(x) {
      return x == undefined ? null : x;
    }

    function stringifyNullUndefined(x) {
      return x == undefined || x == null ? "" : x;
    }

    if (record.formSubmitURL && record.httpRealm) {
      this._log.warn(
        "Record " +
          record.id +
          " has both formSubmitURL and httpRealm. Skipping."
      );
      return null;
    }

    // Passing in "undefined" results in an empty string, which later
    // counts as a value. Explicitly `|| null` these fields according to JS
    // truthiness. Records with empty strings or null will be unmolested.
    let info = new this._nsLoginInfo(
      record.hostname,
      nullUndefined(record.formSubmitURL),
      nullUndefined(record.httpRealm),
      stringifyNullUndefined(record.username),
      record.password,
      record.usernameField,
      record.passwordField
    );

    info.QueryInterface(Ci.nsILoginMetaInfo);
    info.guid = record.id;
    if (record.timeCreated && !isNaN(new Date(record.timeCreated).getTime())) {
      info.timeCreated = record.timeCreated;
    }
    if (
      record.timePasswordChanged &&
      !isNaN(new Date(record.timePasswordChanged).getTime())
    ) {
      info.timePasswordChanged = record.timePasswordChanged;
    }

    // Check the record if there are any unknown fields from other clients
    // that we want to roundtrip during sync to prevent data loss
    let unknownFields = this._processUnknownFields(record.cleartext);
    if (unknownFields) {
      info.unknownFields = unknownFields;
    }
    return info;
  },

  async _getLoginFromGUID(guid) {
    let logins = await this.storage.searchLoginsAsync({ guid }, true);
    if (logins.length) {
      this._log.trace(logins.length + " items matching " + guid + " found.");
      return logins[0];
    }

    this._log.trace("No items matching " + guid + " found. Ignoring");
    return null;
  },

  async applyIncoming(record) {
    if (record.deleted) {
      // Need to supply the sourceSync flag.
      await this.remove(record, { sourceSync: true });
      return;
    }

    await super.applyIncoming(record);
  },

  async getAllIDs() {
    let items = {};
    let logins = await this.storage.getAllLogins(true);

    for (let i = 0; i < logins.length; i++) {
      // Skip over Weave password/passphrase entries.
      let metaInfo = logins[i].QueryInterface(Ci.nsILoginMetaInfo);
      if (Utils.getSyncCredentialsHosts().has(metaInfo.origin)) {
        continue;
      }

      items[metaInfo.guid] = metaInfo;
    }

    return items;
  },

  async changeItemID(oldID, newID) {
    this._log.trace("Changing item ID: " + oldID + " to " + newID);

    if (!(await this.itemExists(oldID))) {
      this._log.trace("Can't change item ID: item doesn't exist");
      return;
    }
    if (await this._getLoginFromGUID(newID)) {
      this._log.trace("Can't change item ID: new ID already in use");
      return;
    }

    let prop = this._newPropertyBag();
    prop.setPropertyAsAUTF8String("guid", newID);

    let oldLogin = await this._getLoginFromGUID(oldID);
    await this.storage.modifyLoginAsync(oldLogin, prop, true);
  },

  async itemExists(id) {
    let login = await this._getLoginFromGUID(id);
    let isDeleted = await this.storage.loginIsDeletedAsync(id);
    return login && !isDeleted;
  },

  async createRecord(id, collection) {
    let record = new LoginRec(collection, id);
    let login = await this._getLoginFromGUID(id);

    if (!login || (await this.storage.loginIsDeletedAsync(id))) {
      record.deleted = true;
      return record;
    }

    record.hostname = login.origin;
    record.formSubmitURL = login.formActionOrigin;
    record.httpRealm = login.httpRealm;
    record.username = login.username;
    record.password = login.password;
    record.usernameField = login.usernameField;
    record.passwordField = login.passwordField;

    // Optional fields.
    login.QueryInterface(Ci.nsILoginMetaInfo);
    record.timeCreated = login.timeCreated;
    record.timePasswordChanged = login.timePasswordChanged;

    // put the unknown fields back to the top-level record
    // during upload
    if (login.unknownFields) {
      let unknownFields = JSON.parse(login.unknownFields);
      if (unknownFields) {
        Object.keys(unknownFields).forEach(key => {
          // We have to manually add it to the cleartext since that's
          // what gets processed during upload
          record.cleartext[key] = unknownFields[key];
        });
      }
    }

    return record;
  },

  async create(record) {
    let login = this._nsLoginInfoFromRecord(record);
    if (!login) {
      return;
    }

    login.everSynced = true;

    this._log.trace("Adding login for " + record.hostname);
    this._log.trace(
      "httpRealm: " +
        JSON.stringify(login.httpRealm) +
        "; " +
        "formSubmitURL: " +
        JSON.stringify(login.formActionOrigin)
    );
    await Services.logins.addLoginAsync(login);
  },

  async remove(record, { sourceSync = false } = {}) {
    this._log.trace("Removing login " + record.id);

    let loginItem = await this._getLoginFromGUID(record.id);
    if (!loginItem) {
      this._log.trace("Asked to remove record that doesn't exist, ignoring");
      return;
    }

    await this.storage.removeLoginAsync(loginItem, sourceSync);
  },

  async update(record) {
    let loginItem = await this._getLoginFromGUID(record.id);
    if (!loginItem || (await this.storage.loginIsDeletedAsync(record.id))) {
      this._log.trace("Skipping update for unknown item: " + record.hostname);
      return;
    }

    this._log.trace("Updating " + record.hostname);
    let newinfo = this._nsLoginInfoFromRecord(record);
    if (!newinfo) {
      return;
    }

    loginItem.everSynced = true;

    await this.storage.modifyLoginAsync(loginItem, newinfo, true);
  },

  async wipe() {
    await this.storage.removeAllUserFacingLoginsAsync(true);
  },
};
Object.setPrototypeOf(PasswordStore.prototype, Store.prototype);
Object.defineProperty(PasswordStore.prototype, "storage", {
  get() {
    return LoginManagerStorage.getActiveStore();
  },
});

function PasswordTracker(name, engine) {
  Tracker.call(this, name, engine);
}
PasswordTracker.prototype = {
  onStart() {
    Svc.Obs.add("passwordmgr-storage-changed", this.asyncObserver);
  },

  onStop() {
    Svc.Obs.remove("passwordmgr-storage-changed", this.asyncObserver);
  },

  async observe(subject, topic, data) {
    if (this.ignoreAll) {
      return;
    }

    switch (data) {
      case "modifyLogin":
        // The syncCounter should have been incremented only for
        // those items that need to be sycned.
        if (
          subject.QueryInterface(Ci.nsIArrayExtensions).GetElementAt(1)
            .syncCounter > 0
        ) {
          this.score += SCORE_INCREMENT_XLARGE;
        }
        break;

      case "addLogin":
      case "removeLogin":
      case "importLogins":
        this.score += SCORE_INCREMENT_XLARGE;
        break;

      case "removeAllLogins":
        this.score +=
          SCORE_INCREMENT_XLARGE *
          (subject.QueryInterface(Ci.nsIArrayExtensions).Count() + 1);
        break;
    }
  },
};
Object.setPrototypeOf(PasswordTracker.prototype, Tracker.prototype);

// Tracker for the Rust-backed engine. Change detection happens in Rust, so this
// only bumps the score to schedule syncs.
function RustPasswordTracker(name, engine) {
  PasswordTracker.call(this, name, engine);
}
RustPasswordTracker.prototype = {
  async observe(subject, topic, data) {
    if (this.ignoreAll) {
      return;
    }

    switch (data) {
      case "modifyLogin":
      case "addLogin":
      case "removeLogin":
      case "importLogins":
        this.score += SCORE_INCREMENT_XLARGE;
        break;

      case "removeAllLogins":
        this.score +=
          SCORE_INCREMENT_XLARGE *
          (subject.QueryInterface(Ci.nsIArrayExtensions).Count() + 1);
        break;
    }
  },
};
Object.setPrototypeOf(RustPasswordTracker.prototype, PasswordTracker.prototype);

export class PasswordValidator extends CollectionValidator {
  constructor() {
    super("passwords", "id", [
      "hostname",
      "formSubmitURL",
      "httpRealm",
      "password",
      "passwordField",
      "username",
      "usernameField",
    ]);
  }

  async getClientItems() {
    let logins = await Services.logins.getAllLogins();
    let syncHosts = Utils.getSyncCredentialsHosts();
    let result = logins
      .map(l => l.QueryInterface(Ci.nsILoginMetaInfo))
      .filter(l => !syncHosts.has(l.origin));
    return Promise.resolve(result);
  }

  normalizeClientItem(item) {
    return {
      id: item.guid,
      guid: item.guid,
      hostname: item.hostname,
      formSubmitURL: item.formSubmitURL,
      httpRealm: item.httpRealm,
      password: item.password,
      passwordField: item.passwordField,
      username: item.username,
      usernameField: item.usernameField,
      original: item,
    };
  }

  async normalizeServerItem(item) {
    return Object.assign({ guid: item.id }, item);
  }
}

export class PasswordsChangeset extends Changeset {
  getModifiedTimestamp(id) {
    return this.changes[id].modified;
  }

  has(id) {
    let change = this.changes[id];
    if (change) {
      return !change.synced;
    }
    return false;
  }

  delete(id) {
    let change = this.changes[id];
    if (change) {
      // Mark the change as synced without removing it from the set.
      // This allows the sync counter to be reset when sync is complete
      // within trackRemainingChanges.
      change.synced = true;
    }
  }
}
