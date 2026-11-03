/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const CRYPTO_COLLECTION = "crypto";
const KEYS_WBO = "keys";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";

import { Async } from "resource://services-common/async.sys.mjs";
import { CommonUtils } from "resource://services-common/utils.sys.mjs";

import {
  CLIENT_NOT_CONFIGURED,
  CREDENTIALS_CHANGED,
  HMAC_EVENT_INTERVAL,
  LOGIN_FAILED,
  LOGIN_FAILED_INVALID_PASSPHRASE,
  LOGIN_FAILED_NETWORK_ERROR,
  LOGIN_FAILED_NO_PASSPHRASE,
  LOGIN_FAILED_NO_USERNAME,
  LOGIN_FAILED_SERVER_ERROR,
  LOGIN_SUCCEEDED,
  MASTER_PASSWORD_LOCKED,
  METARECORD_DOWNLOAD_FAIL,
  NO_SYNC_NODE_FOUND,
  PREFS_BRANCH,
  STATUS_DISABLED,
  STATUS_OK,
  STORAGE_VERSION,
  VERSION_OUT_OF_DATE,
  WEAVE_VERSION,
  kFirefoxShuttingDown,
  kFirstSyncChoiceNotMade,
  kSyncBackoffNotMet,
  kSyncMasterPasswordLocked,
  kSyncNetworkOffline,
  kSyncNotConfigured,
  kSyncWeaveDisabled,
} from "resource://services-sync/constants.sys.mjs";

import { EngineManager } from "resource://services-sync/engines.sys.mjs";
import { ClientEngine } from "resource://services-sync/engines/clients.sys.mjs";
import { Weave } from "resource://services-sync/main.sys.mjs";
import {
  ErrorHandler,
  SyncScheduler,
} from "resource://services-sync/policies.sys.mjs";
import {
  CollectionKeyManager,
  CryptoWrapper,
  RecordManager,
  WBORecord,
} from "resource://services-sync/record.sys.mjs";
import { Resource } from "resource://services-sync/resource.sys.mjs";
import { EngineSynchronizer } from "resource://services-sync/stages/enginesync.sys.mjs";
import { DeclinedEngines } from "resource://services-sync/stages/declined.sys.mjs";
import { Status } from "resource://services-sync/status.sys.mjs";

ChromeUtils.importESModule("resource://services-sync/telemetry.sys.mjs");
import { Svc, Utils } from "resource://services-sync/util.sys.mjs";

import { getFxAccountsSingleton } from "resource://gre/modules/FxAccounts.sys.mjs";
import { SCOPE_APP_SYNC } from "resource://gre/modules/FxAccountsCommon.sys.mjs";

const fxAccounts = getFxAccountsSingleton();

function getEngineModules() {
  let result = {
    Addons: { module: "addons.sys.mjs", symbol: "AddonsEngine" },
    Passwords: {
      module: "passwords.sys.mjs",
      controllingPref: "signon.storage.rust.active",
      whenTrue: "RustPasswordEngine",
      whenFalse: "PasswordEngine",
    },
    Prefs: { module: "prefs.sys.mjs", symbol: "PrefsEngine" },
  };
  if (AppConstants.MOZ_APP_NAME != "thunderbird") {
    result.Bookmarks = {
      module: "bookmarks.sys.mjs",
      symbol: "BookmarksEngine",
    };
    result.Form = { module: "forms.sys.mjs", symbol: "FormEngine" };
    result.History = { module: "history.sys.mjs", symbol: "HistoryEngine" };
    result.Tab = { module: "tabs.sys.mjs", symbol: "TabEngine" };
  }
  if (Svc.PrefBranch.getBoolPref("engine.addresses.available", false)) {
    result.Addresses = {
      module: "resource://autofill/FormAutofillSync.sys.mjs",
      controllingPref: "extensions.formautofill.addresses.storage.rust.active",
      whenTrue: "BridgedAddressesEngine",
      whenFalse: "AddressesEngine",
    };
  }
  if (Svc.PrefBranch.getBoolPref("engine.creditcards.available", false)) {
    result.CreditCards = {
      module: "resource://autofill/FormAutofillSync.sys.mjs",
      controllingPref:
        "extensions.formautofill.creditCards.storage.rust.active",
      whenTrue: "BridgedCreditCardsEngine",
      whenFalse: "CreditCardsEngine",
    };
  }
  result["Extension-Storage"] = {
    module: "extension-storage.sys.mjs",
    controllingPref: "webextensions.storage.sync.kinto",
    whenTrue: "ExtensionStorageEngineKinto",
    whenFalse: "ExtensionStorageEngineBridge",
  };
  return result;
}

const lazy = {};

// A unique identifier for this browser session. Used for logging so
// we can easily see whether 2 logs are in the same browser session or
// after the browser restarted.
ChromeUtils.defineLazyGetter(lazy, "browserSessionID", Utils.makeGUID);

function Sync11Service() {
  this._notify = Utils.notify("weave:service:");
  Utils.defineLazyIDProperty(this, "syncID", "services.sync.client.syncID");
}
Sync11Service.prototype = {
  _lock: Utils.lock,
  _locked: false,
  _loggedIn: false,
  // There are some scenarios where we want to kick off another sync immediately
  // after the current sync
  _queuedSyncReason: null,

  infoURL: null,
  storageURL: null,
  metaURL: null,
  cryptoKeyURL: null,
  // The cluster URL comes via the identity object, which in the FxA
  // world is ebbedded in the token returned from the token server.
  _clusterURL: null,

  get clusterURL() {
    return this._clusterURL || "";
  },
  set clusterURL(value) {
    if (value != null && typeof value != "string") {
      throw new Error("cluster must be a string, got " + typeof value);
    }
    this._clusterURL = value;
    this._updateCachedURLs();
  },

  get isLoggedIn() {
    return this._loggedIn;
  },

  get locked() {
    return this._locked;
  },
  lock: function lock() {
    if (this._locked) {
      return false;
    }
    this._locked = true;
    return true;
  },
  unlock: function unlock() {
    this._locked = false;
  },

  // A specialized variant of Utils.catch.
  // This provides a more informative error message when we're already syncing:
  // see Bug 616568.
  _catch(func) {
    function lockExceptions(ex) {
      if (Utils.isLockException(ex)) {
        // This only happens if we're syncing already.
        this._log.info("Cannot start sync: already syncing?");
      }
    }

    return Utils.catch.call(this, func, lockExceptions);
  },

  get userBaseURL() {
    // The user URL is the cluster URL.
    return this.clusterURL;
  },

  _updateCachedURLs: function _updateCachedURLs() {
    // Nothing to cache yet if we don't have the building blocks
    if (!this.clusterURL) {
      // Also reset all other URLs used by Sync to ensure we aren't accidentally
      // using one cached earlier - if there's no cluster URL any cached ones
      // are invalid.
      this.infoURL = undefined;
      this.storageURL = undefined;
      this.metaURL = undefined;
      this.cryptoKeysURL = undefined;
      return;
    }

    this._log.debug(
      "Caching URLs under storage user base: " + this.userBaseURL
    );

    // Generate and cache various URLs under the storage API for this user
    this.infoURL = this.userBaseURL + "info/collections";
    this.storageURL = this.userBaseURL + "storage/";
    this.metaURL = this.storageURL + "meta/global";
    this.cryptoKeysURL = this.storageURL + CRYPTO_COLLECTION + "/" + KEYS_WBO;
  },

  _checkCrypto: function _checkCrypto() {
    let ok = false;

    try {
      let iv = Weave.Crypto.generateRandomIV();
      if (iv.length == 24) {
        ok = true;
      }
    } catch (e) {
      this._log.debug("Crypto check failed: " + e);
    }

    return ok;
  },

  /**
   * Here is a disgusting yet reasonable way of handling HMAC errors deep in
   * the guts of Sync. The astute reader will note that this is a hacky way of
   * implementing something like continuable conditions.
   *
   * A handler function is glued to each engine. If the engine discovers an
   * HMAC failure, we fetch keys from the server and update our keys, just as
   * we would on startup.
   *
   * If our key collection changed, we signal to the engine (via our return
   * value) that it should retry decryption.
   *
   * If our key collection did not change, it means that we already had the
   * correct keys... and thus a different client has the wrong ones. Reupload
   * the bundle that we fetched, which will bump the modified time on the
   * server and (we hope) prompt a broken client to fix itself.
   *
   * We keep track of the time at which we last applied this reasoning, because
   * thrashing doesn't solve anything. We keep a reasonable interval between
   * these remedial actions.
   */
  lastHMACEvent: 0,

  /*
   * Returns whether to try again.
   */
  async handleHMACEvent() {
    let now = Date.now();

    // Leave a sizable delay between HMAC recovery attempts. This gives us
    // time for another client to fix themselves if we touch the record.
    if (now - this.lastHMACEvent < HMAC_EVENT_INTERVAL) {
      return false;
    }

    this._log.info(
      "Bad HMAC event detected. Attempting recovery " +
        "or signaling to other clients."
    );

    // Set the last handled time so that we don't act again.
    this.lastHMACEvent = now;

    // Fetch keys.
    let cryptoKeys = new CryptoWrapper(CRYPTO_COLLECTION, KEYS_WBO);
    try {
      let cryptoResp = (
        await cryptoKeys.fetch(this.resource(this.cryptoKeysURL))
      ).response;

      // Save out the ciphertext for when we reupload. If there's a bug in
      // CollectionKeyManager, this will prevent us from uploading junk.
      let cipherText = cryptoKeys.ciphertext;

      if (!cryptoResp.success) {
        this._log.warn("Failed to download keys.");
        return false;
      }

      let keysChanged = await this.handleFetchedKeys(
        this.identity.syncKeyBundle,
        cryptoKeys,
        true
      );
      if (keysChanged) {
        // Did they change? If so, carry on.
        this._log.info("Suggesting retry.");
        return true; // Try again.
      }

      // If not, reupload them and continue the current sync.
      cryptoKeys.ciphertext = cipherText;
      cryptoKeys.cleartext = null;

      let uploadResp = await this._uploadCryptoKeys(
        cryptoKeys,
        cryptoResp.obj.modified
      );
      if (uploadResp.success) {
        this._log.info("Successfully re-uploaded keys. Continuing sync.");
      } else {
        this._log.warn(
          "Got error response re-uploading keys. " +
            "Continuing sync; let's try again later."
        );
      }

      return false; // Don't try again: same keys.
    } catch (ex) {
      this._log.warn(
        "Got exception fetching and handling crypto keys. " +
          "Will try again later.",
        ex
      );
      return false;
    }
  },

  async handleFetchedKeys(syncKey, cryptoKeys, skipReset) {
    // Don't want to wipe if we're just starting up!
    let wasBlank = this.collectionKeys.isClear;
    let keysChanged = await this.collectionKeys.updateContents(
      syncKey,
      cryptoKeys
    );

    if (keysChanged && !wasBlank) {
      this._log.debug("Keys changed: " + JSON.stringify(keysChanged));

      if (!skipReset) {
        this._log.info("Resetting client to reflect key change.");

        if (keysChanged.length) {
          // Collection keys only. Reset individual engines.
          await this.resetClient(keysChanged);
        } else {
          // Default key changed: wipe it all.
          await this.resetClient();
        }

        this._log.info("Downloaded new keys, client reset. Proceeding.");
      }
      return true;
    }
    return false;
  },

  /**
   * Prepare to initialize the rest of Weave after waiting a little bit
   */
  async onStartup() {
    this.status = Status;
    this.identity = Status._authManager;
    this.collectionKeys = new CollectionKeyManager();

    this.scheduler = new SyncScheduler(this);
    this.errorHandler = new ErrorHandler(this);

    this._log = Log.repository.getLogger("Sync.Service");
    this._log.manageLevelFromPref("services.sync.log.logger.service.main");

    this._log.info("Loading Weave " + WEAVE_VERSION);

    this.recordManager = new RecordManager(this);

    this.enabled = true;

    await this._registerEngines();

    let ua = Cc["@mozilla.org/network/protocol;1?name=http"].getService(
      Ci.nsIHttpProtocolHandler
    ).userAgent;
    this._log.info(ua);

    if (!this._checkCrypto()) {
      this.enabled = false;
      this._log.info(
        "Could not load the Weave crypto component. Disabling " +
          "Weave, since it will not work correctly."
      );
    }

    Svc.Obs.add("weave:service:setup-complete", this);
    Svc.Obs.add("weave:service:sync:finish", this);
    Svc.Obs.add("sync:collection_changed", this); // Pulled from FxAccountsCommon
    Svc.Obs.add("fxaccounts:device_disconnected", this);
    Services.prefs.addObserver(PREFS_BRANCH + "engine.", this);

    if (!this.enabled) {
      this._log.info("Firefox Sync disabled.");
    }

    this._updateCachedURLs();

    let status = this._checkSetup();
    if (status != STATUS_DISABLED && status != CLIENT_NOT_CONFIGURED) {
      this._startTracking();
    }

    // Send an event now that Weave service is ready.  We don't do this
    // synchronously so that observers can import this module before
    // registering an observer.
    CommonUtils.nextTick(() => {
      this.status.ready = true;

      // UI code uses the flag on the XPCOM service so it doesn't have
      // to load a bunch of modules.
      let xps = Cc["@mozilla.org/weave/service;1"].getService(
        Ci.nsISupports
      ).wrappedJSObject;
      xps.ready = true;

      Svc.Obs.notify("weave:service:ready");
    });
  },

  _checkSetup: function _checkSetup() {
    if (!this.enabled) {
      return (this.status.service = STATUS_DISABLED);
    }
    return this.status.checkSetup();
  },

  /**
   * Register the built-in engines for certain applications
   */
  async _registerEngines() {
    this.engineManager = new EngineManager(this);

    let engineModules = getEngineModules();

    let engines = [];
    // We allow a pref, which has no default value, to limit the engines
    // which are registered. We expect only tests will use this.
    if (
      Svc.PrefBranch.getPrefType("registerEngines") !=
      Ci.nsIPrefBranch.PREF_INVALID
    ) {
      engines = Svc.PrefBranch.getStringPref("registerEngines").split(",");
      this._log.info("Registering custom set of engines", engines);
    } else {
      // default is all engines.
      engines = Object.keys(engineModules);
    }

    let declined = [];
    let pref = Svc.PrefBranch.getStringPref("declinedEngines", null);
    if (pref) {
      declined = pref.split(",");
    }

    let clientsEngine = new ClientEngine(this);
    // Ideally clientsEngine should not exist
    // (or be a promise that calls initialize() before returning the engine)
    await clientsEngine.initialize();
    this.clientsEngine = clientsEngine;

    for (let name of engines) {
      if (!(name in engineModules)) {
        this._log.info("Do not know about engine: " + name);
        continue;
      }
      let modInfo = engineModules[name];
      if (!modInfo.module.includes(":")) {
        modInfo.module = "resource://services-sync/engines/" + modInfo.module;
      }
      try {
        let ns = ChromeUtils.importESModule(modInfo.module);
        if (modInfo.symbol) {
          let symbol = modInfo.symbol;
          if (!(symbol in ns)) {
            this._log.warn(
              "Could not find exported engine instance: " + symbol
            );
            continue;
          }
          await this.engineManager.register(ns[symbol]);
        } else {
          let { whenTrue, whenFalse, controllingPref } = modInfo;
          if (!(whenTrue in ns) || !(whenFalse in ns)) {
            this._log.warn("Could not find all exported engine instances", {
              whenTrue,
              whenFalse,
            });
            continue;
          }
          await this.engineManager.registerAlternatives(
            name.toLowerCase(),
            controllingPref,
            ns[whenTrue],
            ns[whenFalse]
          );
        }
      } catch (ex) {
        this._log.warn("Could not register engine " + name, ex);
      }
    }

    this.engineManager.setDeclined(declined);
  },

  /**
   * This method updates the local engines state from an existing meta/global
   * when Sync is disabled.
   * Running this code if sync is enabled would end up in very weird results
   * (but we're nice and we check before doing anything!).
   */
  async updateLocalEnginesState() {
    await this.promiseInitialized;

    // Sanity check, this method is not meant to be run if Sync is enabled!
    if (Svc.PrefBranch.getStringPref("username", "")) {
      throw new Error("Sync is enabled!");
    }

    // For historical reasons the behaviour of setCluster() is bizarre,
    // so just check what we care about - the meta URL.
    if (!this.metaURL) {
      await this.identity.setCluster();
      if (!this.metaURL) {
        this._log.warn("Could not find a cluster.");
        return;
      }
    }
    // Clear the cache so we always fetch the latest meta/global.
    this.recordManager.clearCache();
    let meta = await this.recordManager.get(this.metaURL);
    if (!meta) {
      this._log.info("Meta record is null, aborting engine state update.");
      return;
    }
    const declinedEngines = meta.payload.declined;
    const allEngines = this.engineManager.getAll().map(e => e.name);
    // We don't want our observer of the enabled prefs to treat the change as
    // a user-change, otherwise we will do the wrong thing with declined etc.
    this._ignorePrefObserver = true;
    try {
      for (const engine of allEngines) {
        Svc.PrefBranch.setBoolPref(
          `engine.${engine}`,
          !declinedEngines.includes(engine)
        );
      }
    } finally {
      this._ignorePrefObserver = false;
    }
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  observe(subject, topic, data) {
    switch (topic) {
      // Ideally this observer should be in the SyncScheduler, but it would require
      // some work to know about the sync specific engines. We should move this there once it does.
      case "sync:collection_changed":
        // We check if we're running TPS here to avoid TPS failing because it
        // couldn't get to get the sync lock, due to us currently syncing the
        // clients engine.
        if (
          data.includes("clients") &&
          !Svc.PrefBranch.getBoolPref("testing.tps", false)
        ) {
          // Sync in the background (it's fine not to wait on the returned promise
          // because sync() has a lock).
          // [] = clients collection only
          this.sync({ why: "collection_changed", engines: [] }).catch(e => {
            this._log.error(e);
          });
        }
        break;
      case "fxaccounts:device_disconnected":
        data = JSON.parse(data);
        if (!data.isLocalDevice) {
          // Refresh the known stale clients list in the background.
          this.clientsEngine.updateKnownStaleClients().catch(e => {
            this._log.error(e);
          });
        }
        break;
      case "weave:service:setup-complete": {
        let status = this._checkSetup();
        if (status != STATUS_DISABLED && status != CLIENT_NOT_CONFIGURED) {
          this._startTracking();
        }
        break;
      }
      case "nsPref:changed": {
        if (this._ignorePrefObserver) {
          return;
        }
        const engine = data.slice((PREFS_BRANCH + "engine.").length);
        if (engine.includes(".")) {
          // A sub-preference of the engine was changed. For example
          // `services.sync.engine.bookmarks.validation.percentageChance`.
          return;
        }
        this._handleEngineStatusChanged(engine);
        break;
      }
      case "weave:service:sync:finish":
        if (this._queuedSyncReason) {
          this.sync({ why: this._queuedSyncReason });
          this._queuedSyncReason = null;
        }
        break;
    }
  },

  _handleEngineStatusChanged(engine) {
    this._log.trace("Status for " + engine + " engine changed.");
    if (Svc.PrefBranch.getBoolPref("engineStatusChanged." + engine, false)) {
      // The enabled status being changed back to what it was before.
      Svc.PrefBranch.clearUserPref("engineStatusChanged." + engine);
    } else {
      // Remember that the engine status changed locally until the next sync.
      Svc.PrefBranch.setBoolPref("engineStatusChanged." + engine, true);
    }
  },

  _startTracking() {
    const engines = [this.clientsEngine, ...this.engineManager.getAll()];
    for (let engine of engines) {
      try {
        engine.startTracking();
      } catch (e) {
        this._log.error(`Could not start ${engine.name} engine tracker`, e);
      }
    }
    // This is for TPS. We should try to do better.
    Svc.Obs.notify("weave:service:tracking-started");
  },

  async _stopTracking() {
    const engines = [this.clientsEngine, ...this.engineManager.getAll()];
    for (let engine of engines) {
      try {
        await engine.stopTracking();
      } catch (e) {
        this._log.error(`Could not stop ${engine.name} engine tracker`, e);
      }
    }
    Svc.Obs.notify("weave:service:tracking-stopped");
  },

  /**
   * Obtain a Resource instance with authentication credentials.
   */
  resource: function resource(url) {
    let res = new Resource(url);
    res.authenticator = this.identity.getResourceAuthenticator();

    return res;
  },

  /**
   * Perform the info fetch as part of a login or key fetch, or
   * inside engine sync.
   */
  async _fetchInfo(url) {
    let infoURL = url || this.infoURL;

    this._log.trace("In _fetchInfo: " + infoURL);
    let info;
    try {
      info = await this.resource(infoURL).get();
    } catch (ex) {
      this.errorHandler.checkServerError(ex);
      throw ex;
    }

    // Always check for errors.
    this.errorHandler.checkServerError(info);
    if (!info.success) {
      this._log.error("Aborting sync: failed to get collections.");
      throw info;
    }
    return info;
  },

  async verifyAndFetchSymmetricKeys(infoResponse) {
    this._log.debug(
      "Fetching and verifying -- or generating -- symmetric keys."
    );

    let syncKeyBundle = this.identity.syncKeyBundle;
    if (!syncKeyBundle) {
      this.status.login = LOGIN_FAILED_NO_PASSPHRASE;
      this.status.sync = CREDENTIALS_CHANGED;
      return false;
    }

    try {
      if (!infoResponse) {
        infoResponse = await this._fetchInfo(); // Will throw an exception on failure.
      }

      // This only applies when the server is already at version 4.
      if (infoResponse.status != 200) {
        this._log.warn(
          "info/collections returned non-200 response. Failing key fetch."
        );
        this.status.login = LOGIN_FAILED_SERVER_ERROR;
        this.errorHandler.checkServerError(infoResponse);
        return false;
      }

      let infoCollections = infoResponse.obj;

      this._log.info(
        "Testing info/collections: " + JSON.stringify(infoCollections)
      );

      if (this.collectionKeys.updateNeeded(infoCollections)) {
        this._log.info("collection keys reports that a key update is needed.");

        // Don't always set to CREDENTIALS_CHANGED -- we will probably take care of this.

        // Fetch storage/crypto/keys.
        let cryptoKeys;

        if (infoCollections && CRYPTO_COLLECTION in infoCollections) {
          try {
            cryptoKeys = new CryptoWrapper(CRYPTO_COLLECTION, KEYS_WBO);
            let cryptoResp = (
              await cryptoKeys.fetch(this.resource(this.cryptoKeysURL))
            ).response;

            if (cryptoResp.success) {
              await this.handleFetchedKeys(syncKeyBundle, cryptoKeys);
              return true;
            } else if (cryptoResp.status == 404) {
              // On failure, ask to generate new keys and upload them.
              // Fall through to the behavior below.
              this._log.warn(
                "Got 404 for crypto/keys, but 'crypto' in info/collections. Regenerating."
              );
              cryptoKeys = null;
            } else {
              // Some other problem.
              this.status.login = LOGIN_FAILED_SERVER_ERROR;
              this.errorHandler.checkServerError(cryptoResp);
              this._log.warn(
                "Got status " + cryptoResp.status + " fetching crypto keys."
              );
              return false;
            }
          } catch (ex) {
            this._log.warn("Got exception fetching cryptoKeys.", ex);
            // TODO: Um, what exceptions might we get here? Should we re-throw any?

            // One kind of exception: HMAC failure.
            if (Utils.isHMACMismatch(ex)) {
              this.status.login = LOGIN_FAILED_INVALID_PASSPHRASE;
              this.status.sync = CREDENTIALS_CHANGED;
            } else {
              // In the absence of further disambiguation or more precise
              // failure constants, just report failure.
              this.status.login = LOGIN_FAILED;
            }
            return false;
          }
        } else {
          this._log.info(
            "... 'crypto' is not a reported collection. Generating new keys."
          );
        }

        if (!cryptoKeys) {
          this._log.info("No keys! Generating new ones.");

          // Better make some and upload them, and wipe the server to ensure
          // consistency. This is all achieved via _freshStart.
          // If _freshStart fails to clear the server or upload keys, it will
          // throw.
          await this._freshStart();
          return true;
        }

        // Last-ditch case.
        return false;
      }
      // No update needed: we're good!
      return true;
    } catch (ex) {
      // This means no keys are present, or there's a network error.
      this._log.debug("Failed to fetch and verify keys", ex);
      this.errorHandler.checkServerError(ex);
      return false;
    }
  },

  getMaxRecordPayloadSize() {
    let config = this.serverConfiguration;
    if (!config || !config.max_record_payload_bytes) {
      this._log.warn(
        "No config or incomplete config in getMaxRecordPayloadSize." +
          " Are we running tests?"
      );
      // should stay in sync with MAX_PAYLOAD_SIZE in the Rust tabs engine.
      return 2 * 1024 * 1024;
    }
    let payloadMax = config.max_record_payload_bytes;
    if (config.max_post_bytes && payloadMax <= config.max_post_bytes) {
      return config.max_post_bytes - 4096;
    }
    return payloadMax;
  },

  getMemcacheMaxRecordPayloadSize() {
    // Collections stored in memcached ("tabs", "clients" or "meta") have a
    // different max size than ones stored in the normal storage server db.
    // In practice, the real limit here is 1M (bug 1300451 comment 40), but
    // there's overhead involved that is hard to calculate on the client, so we
    // use 512k to be safe (at the recommendation of the server team). Note
    // that if the server reports a lower limit (via info/configuration), we
    // respect that limit instead. See also bug 1403052.
    return Math.min(512 * 1024, this.getMaxRecordPayloadSize());
  },

  async verifyLogin(allow40XRecovery = true) {
    // Attaching auth credentials to a request requires access to
    // passwords, which means that Resource.get can throw MP-related
    // exceptions!
    // So we ask the identity to verify the login state after unlocking the
    // master password (ie, this call is expected to prompt for MP unlock
    // if necessary) while we still have control.
    this.status.login = await this.identity.unlockAndVerifyAuthState();
    this._log.debug(
      "Fetching unlocked auth state returned " + this.status.login
    );
    if (this.status.login != STATUS_OK) {
      return false;
    }

    try {
      // Make sure we have a cluster to verify against.
      // This is a little weird, if we don't get a node we pretend
      // to succeed, since that probably means we just don't have storage.
      if (this.clusterURL == "" && !(await this.identity.setCluster())) {
        this.status.sync = NO_SYNC_NODE_FOUND;
        return true;
      }

      // Fetch collection info on every startup.
      let test = await this.resource(this.infoURL).get();

      switch (test.status) {
        case 200:
          // The user is authenticated.

          // We have no way of verifying the passphrase right now,
          // so wait until remoteSetup to do so.
          // Just make the most trivial checks.
          if (!this.identity.syncKeyBundle) {
            this._log.warn("No passphrase in verifyLogin.");
            this.status.login = LOGIN_FAILED_NO_PASSPHRASE;
            return false;
          }

          // Go ahead and do remote setup, so that we can determine
          // conclusively that our passphrase is correct.
          if (await this._remoteSetup(test)) {
            // Username/password verified.
            this.status.login = LOGIN_SUCCEEDED;
            return true;
          }

          this._log.warn("Remote setup failed.");
          // Remote setup must have failed.
          return false;

        case 401:
          this._log.warn("401: login failed.");
        // Fall through to the 404 case.

        case 404:
          // Check that we're verifying with the correct cluster
          if (allow40XRecovery && (await this.identity.setCluster())) {
            return await this.verifyLogin(false);
          }

          // We must have the right cluster, but the server doesn't expect us.
          // For FxA this almost certainly means "transient error fetching token".
          this.status.login = LOGIN_FAILED_NETWORK_ERROR;
          return false;

        default:
          // Server didn't respond with something that we expected
          this.status.login = LOGIN_FAILED_SERVER_ERROR;
          this.errorHandler.checkServerError(test);
          return false;
      }
    } catch (ex) {
      // Must have failed on some network issue
      this._log.debug("verifyLogin failed", ex);
      this.status.login = LOGIN_FAILED_NETWORK_ERROR;
      this.errorHandler.checkServerError(ex);
      return false;
    }
  },

  async generateNewSymmetricKeys() {
    this._log.info("Generating new keys WBO...");
    let wbo = await this.collectionKeys.generateNewKeysWBO();
    this._log.info("Encrypting new key bundle.");
    await wbo.encrypt(this.identity.syncKeyBundle);

    let uploadRes = await this._uploadCryptoKeys(wbo, 0);
    if (uploadRes.status != 200) {
      this._log.warn(
        "Got status " +
          uploadRes.status +
          " uploading new keys. What to do? Throw!"
      );
      this.errorHandler.checkServerError(uploadRes);
      throw new Error("Unable to upload symmetric keys.");
    }
    this._log.info("Got status " + uploadRes.status + " uploading keys.");
    let serverModified = uploadRes.obj; // Modified timestamp according to server.
    this._log.debug("Server reports crypto modified: " + serverModified);

    // Now verify that info/collections shows them!
    this._log.debug("Verifying server collection records.");
    let info = await this._fetchInfo();
    this._log.debug("info/collections is: " + info.data);

    if (info.status != 200) {
      this._log.warn("Non-200 info/collections response. Aborting.");
      throw new Error("Unable to upload symmetric keys.");
    }

    info = info.obj;
    if (!(CRYPTO_COLLECTION in info)) {
      this._log.error(
        "Consistency failure: info/collections excludes " +
          "crypto after successful upload."
      );
      throw new Error("Symmetric key upload failed.");
    }

    // Can't check against local modified: clock drift.
    if (info[CRYPTO_COLLECTION] < serverModified) {
      this._log.error(
        "Consistency failure: info/collections crypto entry " +
          "is stale after successful upload."
      );
      throw new Error("Symmetric key upload failed.");
    }

    // Doesn't matter if the timestamp is ahead.

    // Download and install them.
    let cryptoKeys = new CryptoWrapper(CRYPTO_COLLECTION, KEYS_WBO);
    let cryptoResp = (await cryptoKeys.fetch(this.resource(this.cryptoKeysURL)))
      .response;
    if (cryptoResp.status != 200) {
      this._log.warn("Failed to download keys.");
      throw new Error("Symmetric key download failed.");
    }
    let keysChanged = await this.handleFetchedKeys(
      this.identity.syncKeyBundle,
      cryptoKeys,
      true
    );
    if (keysChanged) {
      this._log.info("Downloaded keys differed, as expected.");
    }
  },

  // configures/enabled/turns-on sync. There must be an FxA user signed in.
  async configure() {
    // We don't, and must not, throw if sync is already configured, because we
    // might end up being called as part of a "reconnect" flow. We also want to
    // avoid checking the FxA user is the same as the pref because the email
    // address for the FxA account can change - we'd need to use the uid.
    let user = await fxAccounts.getSignedInUser();
    if (!user) {
      throw new Error("No FxA user is signed in");
    }
    // Check if the user has sync keys. With OAuth-based authentication,
    // keys cannot be fetched on demand - they must exist locally.
    let hasKeys = await fxAccounts.keys.hasKeysForScope(SCOPE_APP_SYNC);
    if (!hasKeys) {
      throw new Error("User does not have sync keys");
    }
    this._log.info("Configuring sync with current FxA user");
    Svc.PrefBranch.setStringPref("username", user.email);
    Svc.Obs.notify("weave:connected");
  },

  // resets/turns-off sync.
  async startOver() {
    this._log.trace("Invoking Service.startOver.");
    await this._stopTracking();
    this.status.resetSync();

    // Deletion doesn't make sense if we aren't set up yet!
    if (this.clusterURL != "") {
      // Clear client-specific data from the server, including disabled engines.
      const engines = [this.clientsEngine, ...this.engineManager.getAll()];
      for (let engine of engines) {
        try {
          await engine.removeClientData();
        } catch (ex) {
          this._log.warn(`Deleting client data for ${engine.name} failed`, ex);
        }
      }
      this._log.debug("Finished deleting client data.");
    } else {
      this._log.debug("Skipping client data removal: no cluster URL.");
    }

    this.identity.resetCredentials();
    this.status.login = LOGIN_FAILED_NO_USERNAME;
    this.logout();
    Svc.Obs.notify("weave:service:start-over");

    // Reset all engines and clear keys.
    await this.resetClient();
    this.collectionKeys.clear();
    this.status.resetBackoff();

    // Reset Weave prefs.
    this._ignorePrefObserver = true;
    for (const pref of Svc.PrefBranch.getChildList("")) {
      Svc.PrefBranch.clearUserPref(pref);
    }
    this._ignorePrefObserver = false;
    this.clusterURL = null;

    Svc.PrefBranch.setStringPref("lastversion", WEAVE_VERSION);

    try {
      this.identity.finalize();
      this.status.__authManager = null;
      this.identity = Status._authManager;
      Svc.Obs.notify("weave:service:start-over:finish");
    } catch (err) {
      this._log.error(
        "startOver failed to re-initialize the identity manager",
        err
      );
      // Still send the observer notification so the current state is
      // reflected in the UI.
      Svc.Obs.notify("weave:service:start-over:finish");
    }
  },

  async login() {
    async function onNotify() {
      this._loggedIn = false;
      if (this.scheduler.offline) {
        this.status.login = LOGIN_FAILED_NETWORK_ERROR;
        throw new Error("Application is offline, login should not be called");
      }

      this._log.info("User logged in successfully - verifying login.");
      if (!(await this.verifyLogin())) {
        // verifyLogin sets the failure states here.
        throw new Error(`Login failed: ${this.status.login}`);
      }

      this._updateCachedURLs();

      this._loggedIn = true;

      return true;
    }

    let notifier = this._notify("login", "", onNotify.bind(this));
    return this._catch(this._lock("service.js: login", notifier))();
  },

  logout: function logout() {
    // If we failed during login, we aren't going to have this._loggedIn set,
    // but we still want to ask the identity to logout, so it doesn't try and
    // reuse any old credentials next time we sync.
    this._log.info("Logging out");
    this.identity.logout();
    this._loggedIn = false;

    Svc.Obs.notify("weave:service:logout:finish");
  },

  // Note: returns false if we failed for a reason other than the server not yet
  // supporting the api.
  async _fetchServerConfiguration() {
    // This is similar to _fetchInfo, but with different error handling.

    let infoURL = this.userBaseURL + "info/configuration";
    this._log.debug("Fetching server configuration", infoURL);
    let configResponse;
    try {
      configResponse = await this.resource(infoURL).get();
    } catch (ex) {
      // This is probably a network or similar error.
      this._log.warn("Failed to fetch info/configuration", ex);
      this.errorHandler.checkServerError(ex);
      return false;
    }

    if (configResponse.status == 404) {
      // This server doesn't support the URL yet - that's OK.
      this._log.debug(
        "info/configuration returned 404 - using default upload semantics"
      );
    } else if (configResponse.status != 200) {
      this._log.warn(
        `info/configuration returned ${configResponse.status} - using default configuration`
      );
      this.errorHandler.checkServerError(configResponse);
      return false;
    } else {
      this.serverConfiguration = configResponse.obj;
    }
    this._log.trace(
      "info/configuration for this server",
      this.serverConfiguration
    );
    return true;
  },

  // Stuff we need to do after login, before we can really do
  // anything (e.g. key setup).
  async _remoteSetup(infoResponse, fetchConfig = true) {
    if (fetchConfig && !(await this._fetchServerConfiguration())) {
      return false;
    }

    this._log.debug("Fetching global metadata record");
    let meta = await this.recordManager.get(this.metaURL);

    // Checking modified time of the meta record.
    if (
      infoResponse &&
      infoResponse.obj.meta != this.metaModified &&
      (!meta || !meta.isNew)
    ) {
      // Delete the cached meta record...
      this._log.debug(
        "Clearing cached meta record. metaModified is " +
          JSON.stringify(this.metaModified) +
          ", setting to " +
          JSON.stringify(infoResponse.obj.meta)
      );

      this.recordManager.del(this.metaURL);

      // ... fetch the current record from the server, and COPY THE FLAGS.
      let newMeta = await this.recordManager.get(this.metaURL);

      // If we got a 401, we do not want to create a new meta/global - we
      // should be able to get the existing meta after we get a new node.
      if (this.recordManager.response.status == 401) {
        this._log.debug(
          "Fetching meta/global record on the server returned 401."
        );
        this.errorHandler.checkServerError(this.recordManager.response);
        return false;
      }

      if (this.recordManager.response.status == 404) {
        this._log.debug("No meta/global record on the server. Creating one.");
        try {
          await this._uploadNewMetaGlobal();
        } catch (uploadRes) {
          this._log.warn(
            "Unable to upload new meta/global. Failing remote setup."
          );
          this.errorHandler.checkServerError(uploadRes);
          return false;
        }
      } else if (!newMeta) {
        this._log.warn("Unable to get meta/global. Failing remote setup.");
        this.errorHandler.checkServerError(this.recordManager.response);
        return false;
      } else {
        // If newMeta, then it stands to reason that meta != null.
        newMeta.isNew = meta.isNew;
        newMeta.changed = meta.changed;
      }

      // Switch in the new meta object and record the new time.
      meta = newMeta;
      this.metaModified = infoResponse.obj.meta;
    }

    let remoteVersion =
      meta && meta.payload.storageVersion ? meta.payload.storageVersion : "";

    this._log.debug(
      [
        "Weave Version:",
        WEAVE_VERSION,
        "Local Storage:",
        STORAGE_VERSION,
        "Remote Storage:",
        remoteVersion,
      ].join(" ")
    );

    // Check for cases that require a fresh start. When comparing remoteVersion,
    // we need to convert it to a number as older clients used it as a string.
    if (
      !meta ||
      !meta.payload.storageVersion ||
      !meta.payload.syncID ||
      STORAGE_VERSION > parseFloat(remoteVersion)
    ) {
      this._log.info(
        "One of: no meta, no meta storageVersion, or no meta syncID. Fresh start needed."
      );

      // abort the server wipe if the GET status was anything other than 404 or 200
      let status = this.recordManager.response.status;
      if (status != 200 && status != 404) {
        this.status.sync = METARECORD_DOWNLOAD_FAIL;
        this.errorHandler.checkServerError(this.recordManager.response);
        this._log.warn(
          "Unknown error while downloading metadata record. Aborting sync."
        );
        return false;
      }

      if (!meta) {
        this._log.info("No metadata record, server wipe needed");
      }
      if (meta && !meta.payload.syncID) {
        this._log.warn("No sync id, server wipe needed");
      }

      this._log.info("Wiping server data");
      await this._freshStart();

      if (status == 404) {
        this._log.info(
          "Metadata record not found, server was wiped to ensure " +
            "consistency."
        );
      } else {
        // 200
        this._log.info("Wiped server; incompatible metadata: " + remoteVersion);
      }
      return true;
    } else if (remoteVersion > STORAGE_VERSION) {
      this.status.sync = VERSION_OUT_OF_DATE;
      this._log.warn("Upgrade required to access newer storage version.");
      return false;
    } else if (meta.payload.syncID != this.syncID) {
      this._log.info(
        "Sync IDs differ. Local is " +
          this.syncID +
          ", remote is " +
          meta.payload.syncID
      );
      await this.resetClient();
      this.collectionKeys.clear();
      this.syncID = meta.payload.syncID;
      this._log.debug("Clear cached values and take syncId: " + this.syncID);

      if (!(await this.verifyAndFetchSymmetricKeys(infoResponse))) {
        this._log.warn("Failed to fetch symmetric keys. Failing remote setup.");
        return false;
      }

      // bug 545725 - re-verify creds and fail sanely
      if (!(await this.verifyLogin())) {
        this.status.sync = CREDENTIALS_CHANGED;
        this._log.info(
          "Credentials have changed, aborting sync and forcing re-login."
        );
        return false;
      }

      return true;
    }
    if (!(await this.verifyAndFetchSymmetricKeys(infoResponse))) {
      this._log.warn("Failed to fetch symmetric keys. Failing remote setup.");
      return false;
    }

    return true;
  },

  /**
   * Return whether we should attempt login at the start of a sync.
   *
   * Note that this function has strong ties to _checkSync: callers
   * of this function should typically use _checkSync to verify that
   * any necessary login took place.
   */
  _shouldLogin: function _shouldLogin() {
    return (
      this.enabled &&
      !this.scheduler.offline &&
      !this.isLoggedIn &&
      Async.isAppReady()
    );
  },

  /**
   * Determine if a sync should run.
   *
   * @param ignore [optional]
   *        array of reasons to ignore when checking
   *
   * @return Reason for not syncing; not-truthy if sync should run
   */
  _checkSync: function _checkSync(ignore) {
    let reason = "";
    // Ideally we'd call _checkSetup() here but that has too many side-effects.
    if (Status.service == CLIENT_NOT_CONFIGURED) {
      reason = kSyncNotConfigured;
    } else if (Status.service == STATUS_DISABLED || !this.enabled) {
      reason = kSyncWeaveDisabled;
    } else if (this.scheduler.offline) {
      reason = kSyncNetworkOffline;
    } else if (this.status.minimumNextSync > Date.now()) {
      reason = kSyncBackoffNotMet;
    } else if (
      this.status.login == MASTER_PASSWORD_LOCKED &&
      Utils.mpLocked()
    ) {
      reason = kSyncMasterPasswordLocked;
    } else if (Svc.PrefBranch.getStringPref("firstSync", null) == "notReady") {
      reason = kFirstSyncChoiceNotMade;
    } else if (!Async.isAppReady()) {
      reason = kFirefoxShuttingDown;
    }

    if (ignore && ignore.includes(reason)) {
      return "";
    }

    return reason;
  },

  /**
   * Perform a full sync (or of the given engines). While a sync is in progress,
   * this call is ignored; to guarantee a follow-up you must call queueSync().
   *
   * @param {object} options
   * @param {Array<string>} [options.engines] — names of engines to sync
   * @param {string} [options.why] — reason for the sync
   * @returns {Promise<void>}
   */
  async sync({ engines, why } = {}) {
    let dateStr = Utils.formatTimestamp(new Date());
    this._log.debug("User-Agent: " + Utils.userAgent);
    await this.promiseInitialized;
    this._log.info(
      `Starting sync at ${dateStr} in browser session ${lazy.browserSessionID}`
    );
    return this._catch(async function () {
      // Make sure we're logged in.
      if (this._shouldLogin()) {
        this._log.debug("In sync: should login.");
        if (!(await this.login())) {
          this._log.debug("Not syncing: login returned false.");
          return;
        }
      } else {
        this._log.trace("In sync: no need to login.");
      }
      await this._lockedSync(engines, why);
    })();
  },

  /**
   * Sync up engines with the server.
   */
  async _lockedSync(engineNamesToSync, why) {
    return this._lock(
      "service.js: sync",
      this._notify("sync", JSON.stringify({ why }), async function onNotify() {
        let synchronizer = new EngineSynchronizer(this);
        await synchronizer.sync(engineNamesToSync, why); // Might throw!

        // We successfully synchronized.
        // Check if the identity wants to pre-fetch a migration sentinel from
        // the server.
        // If we have no clusterURL, we are probably doing a node reassignment
        // so don't attempt to get it in that case.
        if (this.clusterURL) {
          this.identity.prefetchMigrationSentinel(this);
        }

        // Now let's update our declined engines
        await this._maybeUpdateDeclined();
      })
    )();
  },

  /**
   * Kick off a sync after the current one finishes, or immediately if idle.
   *
   * @param {string} why — reason for calling the sync
   */
  queueSync(why) {
    if (this._locked) {
      // A sync is already in flight; queue a follow-up.
      this._queuedSyncReason = why;
    } else {
      // No sync right now, go ahead immediately.
      this.sync({ why });
    }
  },

  /**
   * Update the "declined" information in meta/global if necessary.
   */
  async _maybeUpdateDeclined() {
    // if Sync failed due to no node we will not have a meta URL, so can't
    // update anything.
    if (!this.metaURL) {
      return;
    }
    let meta = await this.recordManager.get(this.metaURL);
    if (!meta) {
      this._log.warn("No meta/global; can't update declined state.");
      return;
    }

    let declinedEngines = new DeclinedEngines(this);
    let didChange = declinedEngines.updateDeclined(meta, this.engineManager);
    if (!didChange) {
      this._log.info(
        "No change to declined engines. Not reuploading meta/global."
      );
      return;
    }

    await this.uploadMetaGlobal(meta);
  },

  /**
   * Upload a fresh meta/global record
   *
   * @throws the response object if the upload request was not a success
   */
  async _uploadNewMetaGlobal() {
    let meta = new WBORecord("meta", "global");
    meta.payload.syncID = this.syncID;
    meta.payload.storageVersion = STORAGE_VERSION;
    meta.payload.declined = this.engineManager.getDeclined();
    meta.modified = 0;
    meta.isNew = true;

    await this.uploadMetaGlobal(meta);
  },

  /**
   * Upload meta/global, throwing the response on failure
   *
   * @param {WBORecord} meta meta/global record
   * @throws the response object if the request was not a success
   */
  async uploadMetaGlobal(meta) {
    this._log.debug("Uploading meta/global", meta);
    let res = this.resource(this.metaURL);
    res.setHeader("X-If-Unmodified-Since", meta.modified);
    let response = await res.put(meta);
    if (!response.success) {
      throw response;
    }
    // From https://docs.services.mozilla.com/storage/apis-1.5.html:
    // "Successful responses will return the new last-modified time for the collection."
    meta.modified = response.obj;
    this.recordManager.set(this.metaURL, meta);
  },

  /**
   * Upload crypto/keys
   *
   * @param {WBORecord} cryptoKeys crypto/keys record
   * @param {number} lastModified known last modified timestamp (in decimal seconds),
   *                 will be used to set the X-If-Unmodified-Since header
   */
  async _uploadCryptoKeys(cryptoKeys, lastModified) {
    this._log.debug(`Uploading crypto/keys (lastModified: ${lastModified})`);
    let res = this.resource(this.cryptoKeysURL);
    res.setHeader("X-If-Unmodified-Since", lastModified);
    return res.put(cryptoKeys);
  },

  async _freshStart() {
    this._log.info("Fresh start. Resetting client.");
    await this.resetClient();
    this.collectionKeys.clear();

    // Wipe the server.
    await this.wipeServer();

    // Upload a new meta/global record.
    // _uploadNewMetaGlobal throws on failure -- including race conditions.
    // If we got into a race condition, we'll abort the sync this way, too.
    // That's fine. We'll just wait till the next sync. The client that we're
    // racing is probably busy uploading stuff right now anyway.
    await this._uploadNewMetaGlobal();

    // Wipe everything we know about except meta because we just uploaded it
    // TODO: there's a bug here. We should be calling resetClient, no?

    // Generate, upload, and download new keys. Do this last so we don't wipe
    // them...
    await this.generateNewSymmetricKeys();
  },

  /**
   * Wipe user data from the server.
   *
   * @param collections [optional]
   *        Array of collections to wipe. If not given, all collections are
   *        wiped by issuing a DELETE request for `storageURL`.
   *
   * @return the server's timestamp of the (last) DELETE.
   */
  async wipeServer(collections) {
    let response;
    if (!collections) {
      // Strip the trailing slash.
      let res = this.resource(this.storageURL.slice(0, -1));
      res.setHeader("X-Confirm-Delete", "1");
      try {
        response = await res.delete();
      } catch (ex) {
        this._log.debug("Failed to wipe server", ex);
        throw ex;
      }
      if (response.status != 200 && response.status != 404) {
        this._log.debug(
          "Aborting wipeServer. Server responded with " +
            response.status +
            " response for " +
            this.storageURL
        );
        throw response;
      }
      return response.headers["x-weave-timestamp"];
    }

    let timestamp;
    for (let name of collections) {
      let url = this.storageURL + name;
      try {
        response = await this.resource(url).delete();
      } catch (ex) {
        this._log.debug("Failed to wipe '" + name + "' collection", ex);
        throw ex;
      }

      if (response.status != 200 && response.status != 404) {
        this._log.debug(
          "Aborting wipeServer. Server responded with " +
            response.status +
            " response for " +
            url
        );
        throw response;
      }

      if ("x-weave-timestamp" in response.headers) {
        timestamp = response.headers["x-weave-timestamp"];
      }
    }
    return timestamp;
  },

  /**
   * Wipe all local user data.
   *
   * @param engines [optional]
   *        Array of engine names to wipe. If not given, all engines are used.
   */
  async wipeClient(engines) {
    // If we don't have any engines, reset the service and wipe all engines
    if (!engines) {
      // Clear out any service data
      await this.resetService();

      engines = [this.clientsEngine, ...this.engineManager.getAll()];
    } else {
      // Convert the array of names into engines
      engines = this.engineManager.get(engines);
    }

    // Fully wipe each engine if it's able to decrypt data
    for (let engine of engines) {
      if (await engine.canDecrypt()) {
        await engine.wipeClient();
      }
    }
  },

  /**
   * Wipe all remote user data by wiping the server then telling each remote
   * client to wipe itself.
   *
   * @param engines
   *        Array of engine names to wipe.
   */
  async wipeRemote(engines) {
    try {
      // Make sure stuff gets uploaded.
      await this.resetClient(engines);

      // Clear out any server data.
      await this.wipeServer(engines);

      // Only wipe the engines provided.
      let extra = { reason: "wipe-remote" };
      for (const e of engines) {
        await this.clientsEngine.sendCommand("wipeEngine", [e], null, extra);
      }

      // Make sure the changed clients get updated.
      await this.clientsEngine.sync();
    } catch (ex) {
      this.errorHandler.checkServerError(ex);
      throw ex;
    }
  },

  /**
   * Reset local service information like logs, sync times, caches.
   */
  async resetService() {
    return this._catch(async function reset() {
      this._log.info("Service reset.");

      // Pretend we've never synced to the server and drop cached data
      this.syncID = "";
      this.recordManager.clearCache();
    })();
  },

  /**
   * Reset the client by getting rid of any local server data and client data.
   *
   * @param engines [optional]
   *        Array of engine names to reset. If not given, all engines are used.
   */
  async resetClient(engines) {
    return this._catch(async function doResetClient() {
      // If we don't have any engines, reset everything including the service
      if (!engines) {
        // Clear out any service data
        await this.resetService();

        engines = [this.clientsEngine, ...this.engineManager.getAll()];
      } else {
        // Convert the array of names into engines
        engines = this.engineManager.get(engines);
      }

      // Have each engine drop any temporary meta data
      for (let engine of engines) {
        await engine.resetClient();
      }
    })();
  },
};

export var Service = new Sync11Service();
Service.promiseInitialized = new Promise(resolve => {
  Service.onStartup().then(resolve);
});
