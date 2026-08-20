/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This file defines the add-on sync functionality.
 *
 * There are currently a number of known limitations:
 *  - We only sync XPI extensions and themes available from addons.mozilla.org.
 *    We hope to expand support for other add-ons eventually.
 *  - We only attempt syncing of add-ons between applications of the same type.
 *    This means add-ons will not synchronize between Firefox desktop and
 *    Firefox mobile, for example. This is because of significant add-on
 *    incompatibility between application types.
 *
 * Add-on records exist for each known {add-on, app-id} pair in the Sync client
 * set. Each record has a randomly chosen GUID. The records then contain
 * basic metadata about the add-on.
 *
 * We currently synchronize:
 *
 *  - Installations
 *  - Uninstallations
 *  - User enabling and disabling
 *
 * Synchronization is influenced by the following preferences:
 *
 *  - services.sync.addons.ignoreUserEnabledChanges
 *  - services.sync.addons.trustedSourceHostnames
 *
 *  and also influenced by whether addons have repository caching enabled and
 *  whether they allow installation of addons from insecure options (both of
 *  which are themselves influenced by the "extensions." pref branch)
 *
 * See the documentation in all.js for the behavior of these prefs.
 */

import { AddonUtils } from "resource://services-sync/addonutils.sys.mjs";
import { AddonsReconciler } from "resource://services-sync/addonsreconciler.sys.mjs";
import {
  Store,
  SyncEngine,
  LegacyTracker,
} from "resource://services-sync/engines.sys.mjs";
import { CryptoWrapper } from "resource://services-sync/record.sys.mjs";
import { Svc, Utils } from "resource://services-sync/util.sys.mjs";

import { SCORE_INCREMENT_XLARGE } from "resource://services-sync/constants.sys.mjs";
import { CollectionValidator } from "resource://services-sync/collection_validator.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
});

// 7 days in milliseconds.
const PRUNE_ADDON_CHANGES_THRESHOLD = 60 * 60 * 24 * 7 * 1000;

/**
 * AddonRecord represents the state of an add-on in an application.
 *
 * Each add-on has its own record for each application ID it is installed
 * on.
 *
 * The ID of add-on records is a randomly-generated GUID. It is random instead
 * of deterministic so the URIs of the records cannot be guessed and so
 * compromised server credentials won't result in disclosure of the specific
 * add-ons present in a Sync account.
 *
 * The record contains the following fields:
 *
 *  addonID
 *    ID of the add-on. This correlates to the "id" property on an Addon type.
 *
 *  applicationID
 *    The application ID this record is associated with.
 *
 *  enabled
 *    Boolean stating whether add-on is enabled or disabled by the user.
 *
 *  source
 *    String indicating where an add-on is from. Currently, we only support
 *    the value "amo" which indicates that the add-on came from the official
 *    add-ons repository, addons.mozilla.org. In the future, we may support
 *    installing add-ons from other sources. This provides a future-compatible
 *    mechanism for clients to only apply records they know how to handle.
 */
function AddonRecord(collection, id) {
  CryptoWrapper.call(this, collection, id);
}
AddonRecord.prototype = {
  _logName: "Record.Addon",
};
Object.setPrototypeOf(AddonRecord.prototype, CryptoWrapper.prototype);

Utils.deferGetSet(AddonRecord, "cleartext", [
  "addonID",
  "applicationID",
  "enabled",
  "source",
]);

/**
 * The AddonsEngine handles synchronization of add-ons between clients.
 *
 * The engine maintains an instance of an AddonsReconciler, which is the entity
 * maintaining state for add-ons. It provides the history and tracking APIs
 * that AddonManager doesn't.
 *
 * The engine instance overrides a handful of functions on the base class. The
 * rationale for each is documented by that function.
 */
export function AddonsEngine(service) {
  SyncEngine.call(this, "Addons", service);

  this._reconciler = new AddonsReconciler(this._tracker.asyncObserver);
}

AddonsEngine.prototype = {
  _storeObj: AddonsStore,
  _trackerObj: AddonsTracker,
  _recordObj: AddonRecord,
  version: 1,

  syncPriority: 5,

  _reconciler: null,

  async initialize() {
    await SyncEngine.prototype.initialize.call(this);
    await this._reconciler.ensureStateLoaded();
  },

  /**
   * Override parent method to find add-ons by their public ID, not Sync GUID.
   */
  async _findDupe(item) {
    let id = item.addonID;

    // The reconciler should have been updated at the top of the sync, so we
    // can assume it is up to date when this function is called.
    let addons = this._reconciler.addons;
    if (!(id in addons)) {
      return null;
    }

    let addon = addons[id];
    if (addon.guid != item.id) {
      return addon.guid;
    }

    return null;
  },

  /**
   * Override getChangedIDs to pull in tracker changes plus changes from the
   * reconciler log.
   */
  async getChangedIDs() {
    let changes = {};
    const changedIDs = await this._tracker.getChangedIDs();
    for (let [id, modified] of Object.entries(changedIDs)) {
      changes[id] = modified;
    }

    let lastSync = await this.getLastSync();
    let lastSyncDate = new Date(lastSync * 1000);

    // The reconciler should have been refreshed at the beginning of a sync and
    // we assume this function is only called from within a sync.
    let reconcilerChanges = this._reconciler.getChangesSinceDate(lastSyncDate);
    let addons = this._reconciler.addons;
    for (let change of reconcilerChanges) {
      let changeTime = change[0];
      let id = change[2];

      if (!(id in addons)) {
        continue;
      }

      // Keep newest modified time.
      if (id in changes && changeTime < changes[id]) {
        continue;
      }

      if (!(await this.isAddonSyncable(addons[id]))) {
        continue;
      }

      this._log.debug("Adding changed add-on from changes log: " + id);
      let addon = addons[id];
      changes[addon.guid] = changeTime.getTime() / 1000;
    }

    return changes;
  },

  /**
   * Override start of sync function to refresh reconciler.
   *
   * Many functions in this class assume the reconciler is refreshed at the
   * top of a sync. If this ever changes, those functions should be revisited.
   *
   * Technically speaking, we don't need to refresh the reconciler on every
   * sync since it is installed as an AddonManager listener. However, add-ons
   * are complicated and we force a full refresh, just in case the listeners
   * missed something.
   */
  async _syncStartup() {
    // We refresh state before calling parent because syncStartup in the parent
    // looks for changed IDs, which is dependent on add-on state being up to
    // date.
    await this._refreshReconcilerState();
    return SyncEngine.prototype._syncStartup.call(this);
  },

  /**
   * Override end of sync to perform a little housekeeping on the reconciler.
   *
   * We prune changes to prevent the reconciler state from growing without
   * bound. Even if it grows unbounded, there would have to be many add-on
   * changes (thousands) for it to slow things down significantly. This is
   * highly unlikely to occur. Still, we exercise defense just in case.
   */
  async _syncCleanup() {
    let lastSync = await this.getLastSync();
    let ms = 1000 * lastSync - PRUNE_ADDON_CHANGES_THRESHOLD;
    this._reconciler.pruneChangesBeforeDate(new Date(ms));
    return SyncEngine.prototype._syncCleanup.call(this);
  },

  /**
   * Helper function to ensure reconciler is up to date.
   *
   * This will load the reconciler's state from the file
   * system (if needed) and refresh the state of the reconciler.
   */
  async _refreshReconcilerState() {
    this._log.debug("Refreshing reconciler state");
    return this._reconciler.refreshGlobalState();
  },

  // Returns a promise
  isAddonSyncable(addon, ignoreRepoCheck) {
    return this._store.isAddonSyncable(addon, ignoreRepoCheck);
  },
};
Object.setPrototypeOf(AddonsEngine.prototype, SyncEngine.prototype);

/**
 * This is the primary interface between Sync and the Addons Manager.
 *
 * In addition to the core store APIs, we provide convenience functions to wrap
 * Add-on Manager APIs with Sync-specific semantics.
 */
function AddonsStore(name, engine) {
  Store.call(this, name, engine);
}
AddonsStore.prototype = {
  // Define the add-on types (.type) that we support.
  _syncableTypes: ["extension", "theme"],

  _extensionsPrefs: Services.prefs.getBranch("extensions."),

  get reconciler() {
    return this.engine._reconciler;
  },

  /**
   * Override applyIncoming to filter out records we can't handle.
   */
  async applyIncoming(record) {
    // The fields we look at aren't present when the record is deleted.
    if (!record.deleted) {
      // Ignore records not belonging to our application ID because that is the
      // current policy.
      if (record.applicationID != Services.appinfo.ID) {
        this._log.info(
          "Ignoring incoming record from other App ID: " + record.id
        );
        return;
      }

      // Ignore records that aren't from the official add-on repository, as that
      // is our current policy.
      if (record.source != "amo") {
        this._log.info(
          "Ignoring unknown add-on source (" +
            record.source +
            ")" +
            " for " +
            record.id
        );
        return;
      }
    }

    // Ignore incoming records for which an existing non-syncable addon
    // exists. Note that we do not insist that the addon manager already have
    // metadata for this addon - it's possible our reconciler previously saw the
    // addon but the addon-manager cache no longer has it - which is fine for a
    // new incoming addon.
    // (Note that most other cases where the addon-manager cache is invalid
    // doesn't get this treatment because that cache self-repairs after some
    // time - but it only re-populates addons which are currently installed.)
    let existingMeta = this.reconciler.addons[record.addonID];
    if (
      existingMeta &&
      !(await this.isAddonSyncable(existingMeta, /* ignoreRepoCheck */ true))
    ) {
      this._log.info(
        "Ignoring incoming record for an existing but non-syncable addon",
        record.addonID
      );
      return;
    }

    await Store.prototype.applyIncoming.call(this, record);
  },

  /**
   * Provides core Store API to create/install an add-on from a record.
   */
  async create(record) {
    // This will throw if there was an error. This will get caught by the sync
    // engine and the record will try to be applied later.
    const results = await AddonUtils.installAddons([
      {
        id: record.addonID,
        syncGUID: record.id,
        enabled: record.enabled,
        requireSecureURI: this._extensionsPrefs.getBoolPref(
          "install.requireSecureOrigin",
          true
        ),
      },
    ]);

    if (results.skipped.includes(record.addonID)) {
      this._log.info("Add-on skipped: " + record.addonID);
      // Just early-return for skipped addons - we don't want to arrange to
      // try again next time because the condition that caused up to skip
      // will remain true for this addon forever.
      return;
    }

    let addon;
    for (let a of results.addons) {
      if (a.id == record.addonID) {
        addon = a;
        break;
      }
    }

    // This should never happen, but is present as a fail-safe.
    if (!addon) {
      throw new Error("Add-on not found after install: " + record.addonID);
    }

    this._log.info("Add-on installed: " + record.addonID);
  },

  /**
   * Provides core Store API to remove/uninstall an add-on from a record.
   */
  async remove(record) {
    // If this is called, the payload is empty, so we have to find by GUID.
    let addon = await this.getAddonByGUID(record.id);
    if (!addon) {
      // We don't throw because if the add-on could not be found then we assume
      // it has already been uninstalled and there is nothing for this function
      // to do.
      return;
    }

    this._log.info("Uninstalling add-on: " + addon.id);
    await AddonUtils.uninstallAddon(addon);
  },

  /**
   * Provides core Store API to update an add-on from a record.
   */
  async update(record) {
    let addon = await this.getAddonByID(record.addonID);

    // update() is called if !this.itemExists. And, since itemExists consults
    // the reconciler only, we need to take care of some corner cases.
    //
    // First, the reconciler could know about an add-on that was uninstalled
    // and no longer present in the add-ons manager.
    if (!addon) {
      await this.create(record);
      return;
    }

    // It's also possible that the add-on is non-restartless and has pending
    // install/uninstall activity.
    //
    // We wouldn't get here if the incoming record was for a deletion. So,
    // check for pending uninstall and cancel if necessary.
    if (addon.pendingOperations & lazy.AddonManager.PENDING_UNINSTALL) {
      addon.cancelUninstall();

      // We continue with processing because there could be state or ID change.
    }

    await this.updateUserDisabled(addon, !record.enabled);
  },

  /**
   * Provide core Store API to determine if a record exists.
   */
  async itemExists(guid) {
    let addon = this.reconciler.getAddonStateFromSyncGUID(guid);

    return !!addon;
  },

  /**
   * Create an add-on record from its GUID.
   *
   * @param guid
   *        Add-on GUID (from extensions DB)
   * @param collection
   *        Collection to add record to.
   *
   * @return AddonRecord instance
   */
  async createRecord(guid, collection) {
    let record = new AddonRecord(collection, guid);
    record.applicationID = Services.appinfo.ID;

    let addon = this.reconciler.getAddonStateFromSyncGUID(guid);

    // If we don't know about this GUID or if it has been uninstalled, we mark
    // the record as deleted.
    if (!addon || !addon.installed) {
      record.deleted = true;
      return record;
    }

    record.modified = addon.modified.getTime() / 1000;

    record.addonID = addon.id;
    record.enabled = addon.enabled;

    // This needs to be dynamic when add-ons don't come from AddonRepository.
    record.source = "amo";

    return record;
  },

  /**
   * Changes the id of an add-on.
   *
   * This implements a core API of the store.
   */
  async changeItemID(oldID, newID) {
    // We always update the GUID in the reconciler because it will be
    // referenced later in the sync process.
    let state = this.reconciler.getAddonStateFromSyncGUID(oldID);
    if (state) {
      state.guid = newID;
      await this.reconciler.saveState();
    }

    let addon = await this.getAddonByGUID(oldID);
    if (!addon) {
      this._log.debug(
        "Cannot change item ID (" +
          oldID +
          ") in Add-on " +
          "Manager because old add-on not present: " +
          oldID
      );
      return;
    }

    addon.syncGUID = newID;
  },

  /**
   * Obtain the set of all syncable add-on Sync GUIDs.
   *
   * This implements a core Store API.
   */
  async getAllIDs() {
    let ids = {};

    let addons = this.reconciler.addons;
    for (let id in addons) {
      let addon = addons[id];
      if (await this.isAddonSyncable(addon)) {
        ids[addon.guid] = true;
      }
    }

    return ids;
  },

  /**
   * Wipe engine data.
   *
   * This uninstalls all syncable addons from the application. In case of
   * error, it logs the error and keeps trying with other add-ons.
   */
  async wipe() {
    this._log.info("Processing wipe.");

    await this.engine._refreshReconcilerState();

    // We only wipe syncable add-ons. Wipe is a Sync feature not a security
    // feature.
    let ids = await this.getAllIDs();
    for (let guid in ids) {
      let addon = await this.getAddonByGUID(guid);
      if (!addon) {
        this._log.debug(
          "Ignoring add-on because it couldn't be obtained: " + guid
        );
        continue;
      }

      if (!AddonUtils.canUninstallAddon(addon.id)) {
        continue;
      }

      this._log.info("Uninstalling add-on as part of wipe: " + addon.id);
      await Utils.catch.call(this, () => addon.uninstall())();
    }
  },

  /***************************************************************************
   * Functions below are unique to this store and not part of the Store API  *
   ***************************************************************************/

  /**
   * Obtain an add-on from its public ID.
   *
   * @param id
   *        Add-on ID
   * @return Addon or undefined if not found
   */
  async getAddonByID(id) {
    return lazy.AddonManager.getAddonByID(id);
  },

  /**
   * Obtain an add-on from its Sync GUID.
   *
   * @param  guid
   *         Add-on Sync GUID
   * @return DBAddonInternal or null
   */
  async getAddonByGUID(guid) {
    return lazy.AddonManager.getAddonBySyncGUID(guid);
  },

  /**
   * Determines whether an add-on is suitable for Sync.
   *
   * @param  addon
   *         Addon instance
   * @param ignoreRepoCheck
   *         Should we skip checking the Addons repository (primarially useful
   *         for testing and validation).
   * @return Boolean indicating whether it is appropriate for Sync
   */
  async isAddonSyncable(addon, ignoreRepoCheck = false) {
    // Currently, we limit syncable add-ons to those that are:
    //   1) In a well-defined set of types
    //   2) Installed in the current profile
    //   3) Not installed by a foreign entity (i.e. installed by the app)
    //      since they act like global extensions.
    //   4) Is not a hotfix.
    //   5) The addons XPIProvider doesn't veto it (i.e not being installed in
    //      the profile directory, or any other reasons it says the addon can't
    //      be synced)
    //   6) Are installed from AMO

    // We could represent the test as a complex boolean expression. We go the
    // verbose route so the failure reason is logged.
    if (!addon) {
      this._log.debug("Null object passed to isAddonSyncable.");
      return false;
    }

    if (!this._syncableTypes.includes(addon.type)) {
      this._log.debug(
        addon.id + " not syncable: type not in allowed list: " + addon.type
      );
      return false;
    }

    if (!(addon.scope & lazy.AddonManager.SCOPE_PROFILE)) {
      this._log.debug(addon.id + " not syncable: not installed in profile.");
      return false;
    }

    // If the addon manager says it's not syncable, we skip it.
    if (!addon.isSyncable) {
      this._log.debug(addon.id + " not syncable: vetoed by the addon manager.");
      return false;
    }

    // This may be too aggressive. If an add-on is downloaded from AMO and
    // manually placed in the profile directory, foreignInstall will be set.
    // Arguably, that add-on should be syncable.
    // TODO Address the edge case and come up with more robust heuristics.
    if (addon.foreignInstall) {
      this._log.debug(addon.id + " not syncable: is foreign install.");
      return false;
    }

    // If the AddonRepository's cache isn't enabled (which it typically isn't
    // in tests), getCachedAddonByID always returns null - so skip the check
    // in that case. We also provide a way to specifically opt-out of the check
    // even if the cache is enabled, which is used by the validators.
    if (ignoreRepoCheck || !lazy.AddonRepository.cacheEnabled) {
      return true;
    }

    let result = await new Promise(res => {
      lazy.AddonRepository.getCachedAddonByID(addon.id, res);
    });

    if (!result) {
      this._log.debug(
        addon.id + " not syncable: add-on not found in add-on repository."
      );
      return false;
    }

    return this.isSourceURITrusted(result.sourceURI);
  },

  /**
   * Determine whether an add-on's sourceURI field is trusted and the add-on
   * can be installed.
   *
   * This function should only ever be called from isAddonSyncable(). It is
   * exposed as a separate function to make testing easier.
   *
   * @param  uri
   *         nsIURI instance to validate
   * @return bool
   */
  isSourceURITrusted: function isSourceURITrusted(uri) {
    // For security reasons, we currently limit synced add-ons to those
    // installed from trusted hostname(s). We additionally require TLS with
    // the add-ons site to help prevent forgeries.
    let trustedHostnames = Svc.PrefBranch.getStringPref(
      "addons.trustedSourceHostnames",
      ""
    ).split(",");

    if (!uri) {
      this._log.debug("Undefined argument to isSourceURITrusted().");
      return false;
    }

    // Scheme is validated before the hostname because uri.host may not be
    // populated for certain schemes. It appears to always be populated for
    // https, so we avoid the potential NS_ERROR_FAILURE on field access.
    if (uri.scheme != "https") {
      this._log.debug("Source URI not HTTPS: " + uri.spec);
      return false;
    }

    if (!trustedHostnames.includes(uri.host)) {
      this._log.debug("Source hostname not trusted: " + uri.host);
      return false;
    }

    return true;
  },

  /**
   * Update the userDisabled flag on an add-on.
   *
   * This will enable or disable an add-on. It has no return value and does
   * not catch or handle exceptions thrown by the addon manager. If no action
   * is needed it will return immediately.
   *
   * @param addon
   *        Addon instance to manipulate.
   * @param value
   *        Boolean to which to set userDisabled on the passed Addon.
   */
  async updateUserDisabled(addon, value) {
    if (addon.userDisabled == value) {
      return;
    }

    // A pref allows changes to the enabled flag to be ignored.
    if (Svc.PrefBranch.getBoolPref("addons.ignoreUserEnabledChanges", false)) {
      this._log.info(
        "Ignoring enabled state change due to preference: " + addon.id
      );
      return;
    }

    AddonUtils.updateUserDisabled(addon, value);
    // updating this flag doesn't send a notification for appDisabled addons,
    // meaning the reconciler will not update its state and may resync the
    // addon - so explicitly rectify the state (bug 1366994)
    if (addon.appDisabled) {
      await this.reconciler.rectifyStateFromAddon(addon);
    }
  },
};

Object.setPrototypeOf(AddonsStore.prototype, Store.prototype);

/**
 * The add-ons tracker keeps track of real-time changes to add-ons.
 *
 * It hooks up to the reconciler and receives notifications directly from it.
 */
function AddonsTracker(name, engine) {
  LegacyTracker.call(this, name, engine);
}
AddonsTracker.prototype = {
  get reconciler() {
    return this.engine._reconciler;
  },

  get store() {
    return this.engine._store;
  },

  /**
   * This callback is executed whenever the AddonsReconciler sends out a change
   * notification. See AddonsReconciler.addChangeListener().
   */
  async changeListener(date, change, addon) {
    this._log.debug("changeListener invoked: " + change + " " + addon.id);
    // Ignore changes that occur during sync.
    if (this.ignoreAll) {
      return;
    }

    if (!(await this.store.isAddonSyncable(addon))) {
      this._log.debug(
        "Ignoring change because add-on isn't syncable: " + addon.id
      );
      return;
    }

    const added = await this.addChangedID(addon.guid, date.getTime() / 1000);
    if (added) {
      this.score += SCORE_INCREMENT_XLARGE;
    }
  },

  onStart() {
    this.reconciler.startListening();
    this.reconciler.addChangeListener(this);
  },

  onStop() {
    this.reconciler.removeChangeListener(this);
    this.reconciler.stopListening();
  },
};

Object.setPrototypeOf(AddonsTracker.prototype, LegacyTracker.prototype);

export class AddonValidator extends CollectionValidator {
  constructor(engine = null) {
    super("addons", "id", ["addonID", "enabled", "applicationID", "source"]);
    this.engine = engine;
  }

  async getClientItems() {
    return lazy.AddonManager.getAllAddons();
  }

  normalizeClientItem(item) {
    let enabled = !item.userDisabled;
    if (item.pendingOperations & lazy.AddonManager.PENDING_ENABLE) {
      enabled = true;
    } else if (item.pendingOperations & lazy.AddonManager.PENDING_DISABLE) {
      enabled = false;
    }
    return {
      enabled,
      id: item.syncGUID,
      addonID: item.id,
      applicationID: Services.appinfo.ID,
      source: "amo", // check item.foreignInstall?
      original: item,
    };
  }

  async normalizeServerItem(item) {
    let guid = await this.engine._findDupe(item);
    if (guid) {
      item.id = guid;
    }
    return item;
  }

  clientUnderstands(item) {
    return item.applicationID === Services.appinfo.ID;
  }

  async syncedByClient(item) {
    return (
      !item.original.hidden &&
      !item.original.isSystem &&
      !(
        item.original.pendingOperations & lazy.AddonManager.PENDING_UNINSTALL
      ) &&
      // No need to await the returned promise explicitely:
      // |expr1 && expr2| evaluates to expr2 if expr1 is true.
      this.engine.isAddonSyncable(item.original, true)
    );
  }
}
