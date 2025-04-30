/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

// TODO:
// * find out how the Chrome implementation deals with conflicts

// TODO bug 1637465: Remove the Kinto-based storage implementation.

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const KINTO_PROD_SERVER_URL =
  "https://webextensions.settings.services.mozilla.com/v1";
const KINTO_DEFAULT_SERVER_URL = KINTO_PROD_SERVER_URL;

const STORAGE_SYNC_SERVER_URL_PREF = "webextensions.storage.sync.serverURL";
const STORAGE_SYNC_SCOPE = "sync:addon_storage";
const STORAGE_SYNC_CRYPTO_COLLECTION_NAME = "storage-sync-crypto";
const STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID = "keys";
const STORAGE_SYNC_CRYPTO_SALT_LENGTH_BYTES = 32;
const FXA_OAUTH_OPTIONS = {
  scope: STORAGE_SYNC_SCOPE,
};
// Default is 5sec, which seems a bit aggressive on the open internet
const KINTO_REQUEST_TIMEOUT = 30000;

import { Log } from "resource://gre/modules/Log.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BulkKeyBundle: "resource://services-sync/keys.sys.mjs",
  CollectionKeyManager: "resource://services-sync/record.sys.mjs",
  CommonUtils: "resource://services-common/utils.sys.mjs",
  CryptoUtils: "resource://services-crypto/utils.sys.mjs",
  ExtensionCommon: "resource://gre/modules/ExtensionCommon.sys.mjs",
  FirefoxAdapter: "resource://services-common/kinto-storage-adapter.sys.mjs",
  Kinto: "resource://services-common/kinto-offline-client.sys.mjs",
  KintoHttpClient: "resource://services-common/kinto-http-client.sys.mjs",
  Observers: "resource://services-common/observers.sys.mjs",
  Utils: "resource://services-sync/util.sys.mjs",
  prefStorageSyncServerURL: {
    pref: STORAGE_SYNC_SERVER_URL_PREF,
    default: KINTO_DEFAULT_SERVER_URL,
  },
  fxAccounts() {
    return ChromeUtils.importESModule(
      "resource://gre/modules/FxAccounts.sys.mjs"
    ).getFxAccountsSingleton();
  },
  WeaveCrypto() {
    let { WeaveCrypto } = ChromeUtils.importESModule(
      "resource://services-crypto/WeaveCrypto.sys.mjs"
    );
    return new WeaveCrypto();
  },
});

/**
 * @typedef {any} Collection
 * @typedef {any} CollectionKeyManager
 * @typedef {any} FXAccounts
 * @typedef {any} KeyBundle
 * @typedef {any} SyncResultObject
 */

const { DefaultMap } = ExtensionUtils;

// Map of Extensions to Set<Contexts> to track contexts that are still
// "live" and use storage.sync.
const extensionContexts = new DefaultMap(() => new Set());
// Borrow logger from Sync.
const log = Log.repository.getLogger("Sync.Engine.Extension-Storage");

// A global that is fxAccounts, or null if (as on android) fxAccounts
// isn't available.
let _fxaService = null;
if (AppConstants.platform != "android") {
  _fxaService = lazy.fxAccounts;
}

class ServerKeyringDeleted extends Error {
  constructor() {
    super(
      "server keyring appears to have disappeared; we were called to decrypt null"
    );
  }
}

/**
 * Check for FXA and throw an exception if we don't have access.
 *
 * @param {object} fxAccounts  The reference we were hoping to use to
 *     access FxA
 * @param {string} action  The thing we were doing when we decided to
 *     see if we had access to FxA
 */
function throwIfNoFxA(fxAccounts, action) {
  if (!fxAccounts) {
    throw new Error(
      `${action} is impossible because FXAccounts is not available; are you on Android?`
    );
  }
}

/**
 * Utility function to enforce an order of fields when computing an HMAC.
 *
 * @param {KeyBundle} keyBundle  The key bundle to use to compute the HMAC
 * @param {string}    id         The record ID to use when computing the HMAC
 * @param {string}    IV         The IV to use when computing the HMAC
 * @param {string}    ciphertext The ciphertext over which to compute the HMAC
 * @returns {Promise<string>} The computed HMAC
 */
async function ciphertextHMAC(keyBundle, id, IV, ciphertext) {
  const hmacKey = lazy.CommonUtils.byteStringToArrayBuffer(keyBundle.hmacKey);
  const encoder = new TextEncoder();
  const data = encoder.encode(id + IV + ciphertext);
  const hmac = await lazy.CryptoUtils.hmac("SHA-256", hmacKey, data);
  return lazy.CommonUtils.bytesAsHex(
    lazy.CommonUtils.arrayBufferToByteString(hmac)
  );
}

/**
 * Get the current user's hashed kB.
 *
 * @param {FXAccounts} fxaService  The service to use to get the
 *     current user.
 * @returns {Promise<string>} sha256 of the user's kB as a hex string
 */
const getKBHash = async function (fxaService) {
  const key = await fxaService.keys.getKeyForScope(STORAGE_SYNC_SCOPE);
  return fxaService.keys.kidAsHex(key);
};

/**
 * A "remote transformer" that the Kinto library will use to
 * encrypt/decrypt records when syncing.
 *
 * This is an "abstract base class". Subclass this and override
 * getKeys() to use it.
 */
class EncryptionRemoteTransformer {
  async encode(record) {
    const keyBundle = await this.getKeys();
    if (record.ciphertext) {
      throw new Error("Attempt to reencrypt??");
    }
    let id = await this.getEncodedRecordId(record);
    if (!id) {
      throw new Error("Record ID is missing or invalid");
    }

    let IV = lazy.WeaveCrypto.generateRandomIV();
    let ciphertext = await lazy.WeaveCrypto.encrypt(
      JSON.stringify(record),
      keyBundle.encryptionKeyB64,
      IV
    );
    let hmac = await ciphertextHMAC(keyBundle, id, IV, ciphertext);
    const encryptedResult = { ciphertext, IV, hmac, id };

    // Copy over the _status field, so that we handle concurrency
    // headers (If-Match, If-None-Match) correctly.
    // DON'T copy over "deleted" status, because then we'd leak
    // plaintext deletes.
    encryptedResult._status =
      record._status == "deleted" ? "updated" : record._status;
    if (record.hasOwnProperty("last_modified")) {
      encryptedResult.last_modified = record.last_modified;
    }

    return encryptedResult;
  }

  async decode(record) {
    if (!record.ciphertext) {
      // This can happen for tombstones if a record is deleted.
      if (record.deleted) {
        return record;
      }
      throw new Error("No ciphertext: nothing to decrypt?");
    }
    const keyBundle = await this.getKeys();
    // Authenticate the encrypted blob with the expected HMAC
    let computedHMAC = await ciphertextHMAC(
      keyBundle,
      record.id,
      record.IV,
      record.ciphertext
    );

    if (computedHMAC != record.hmac) {
      lazy.Utils.throwHMACMismatch(record.hmac, computedHMAC);
    }

    // Handle invalid data here. Elsewhere we assume that cleartext is an object.
    let cleartext = await lazy.WeaveCrypto.decrypt(
      record.ciphertext,
      keyBundle.encryptionKeyB64,
      record.IV
    );
    let jsonResult = JSON.parse(cleartext);
    if (!jsonResult || typeof jsonResult !== "object") {
      throw new Error(
        "Decryption failed: result is <" + jsonResult + ">, not an object."
      );
    }

    if (record.hasOwnProperty("last_modified")) {
      jsonResult.last_modified = record.last_modified;
    }

    // _status: deleted records were deleted on a client, but
    // uploaded as an encrypted blob so we don't leak deletions.
    // If we get such a record, flag it as deleted.
    if (jsonResult._status == "deleted") {
      jsonResult.deleted = true;
    }

    return jsonResult;
  }

  /**
   * Retrieve keys to use during encryption.
   *
   * @returns {Promise<KeyBundle>}
   */
  getKeys() {
    throw new Error("override getKeys in a subclass");
  }

  /**
   * Compute the record ID to use for the encoded version of the
   * record.
   *
   * The default version just re-uses the record's ID.
   *
   * @param {object} record The record being encoded.
   * @returns {Promise<string>} The ID to use.
   */
  getEncodedRecordId(record) {
    return Promise.resolve(record.id);
  }
}

/**
 * An EncryptionRemoteTransformer that provides a keybundle derived
 * from the user's kB, suitable for encrypting a keyring.
 */
class KeyRingEncryptionRemoteTransformer extends EncryptionRemoteTransformer {
  constructor(fxaService) {
    super();
    this._fxaService = fxaService;
  }

  getKeys() {
    throwIfNoFxA(this._fxaService, "encrypting chrome.storage.sync records");
    const self = this;
    return (async function () {
      let key = await self._fxaService.keys.getKeyForScope(STORAGE_SYNC_SCOPE);
      return lazy.BulkKeyBundle.fromJWK(key);
    })();
  }
  // Pass through the kbHash field from the unencrypted record. If
  // encryption fails, we can use this to try to detect whether we are
  // being compromised or if the record here was encoded with a
  // different kB.
  async encode(record) {
    const encoded = await super.encode(record);
    encoded.kbHash = record.kbHash;
    return encoded;
  }

  async decode(record) {
    try {
      return await super.decode(record);
    } catch (e) {
      if (lazy.Utils.isHMACMismatch(e)) {
        const currentKBHash = await getKBHash(this._fxaService);
        if (record.kbHash != currentKBHash) {
          // Some other client encoded this with a kB that we don't
          // have access to.
          KeyRingEncryptionRemoteTransformer.throwOutdatedKB(
            currentKBHash,
            record.kbHash
          );
        }
      }
      throw e;
    }
  }

  // Generator and discriminator for KB-is-outdated exceptions.
  static throwOutdatedKB(shouldBe, is) {
    throw new Error(
      `kB hash on record is outdated: should be ${shouldBe}, is ${is}`
    );
  }

  static isOutdatedKB(exc) {
    const kbMessage = "kB hash on record is outdated: ";
    return (
      exc &&
      exc.message &&
      exc.message.indexOf &&
      exc.message.indexOf(kbMessage) == 0
    );
  }
}

/**
 * A Promise that centralizes initialization of ExtensionStorageSyncKinto.
 *
 * This centralizes the use of the Sqlite database, to which there is
 * only one connection which is shared by all threads.
 *
 * Fields in the object returned by this Promise:
 *
 * - connection: a Sqlite connection. Meant for internal use only.
 * - kinto: a KintoBase object, suitable for using in Firefox. All
 *   collections in this database will use the same Sqlite connection.
 *
 * @returns {Promise<object>}
 */
async function storageSyncInit() {
  // Memoize the result to share the connection.
  if (storageSyncInit.promise === undefined) {
    const path = "storage-sync.sqlite";
    storageSyncInit.promise = lazy.FirefoxAdapter.openConnection({ path })
      .then(connection => {
        return {
          connection,
          kinto: new lazy.Kinto({
            adapter: lazy.FirefoxAdapter,
            adapterOptions: { sqliteHandle: connection },
            timeout: KINTO_REQUEST_TIMEOUT,
            retry: 0,
          }),
        };
      })
      .catch(e => {
        // Ensure one failure doesn't break us forever.
        Cu.reportError(e);
        storageSyncInit.promise = undefined;
        throw e;
      });
  }
  return storageSyncInit.promise;
}
storageSyncInit.promise = undefined;

// Kinto record IDs have two conditions:
//
// - They must contain only ASCII alphanumerics plus - and _. To fix
// this, we encode all non-letters using _C_, where C is the
// percent-encoded character, so space becomes _20_
// and underscore becomes _5F_.
//
// - They must start with an ASCII letter. To ensure this, we prefix
// all keys with "key-".
function keyToId(key) {
  function escapeChar(match) {
    return "_" + match.codePointAt(0).toString(16).toUpperCase() + "_";
  }
  return "key-" + key.replace(/[^a-zA-Z0-9]/g, escapeChar);
}

// Convert a Kinto ID back into a chrome.storage key.
// Returns null if a key couldn't be parsed.
function idToKey(id) {
  function unescapeNumber(match, group1) {
    return String.fromCodePoint(parseInt(group1, 16));
  }
  // An escaped ID should match this regex.
  // An escaped ID should consist of only letters and numbers, plus
  // code points escaped as _[0-9a-f]+_.
  const ESCAPED_ID_FORMAT = /^(?:[a-zA-Z0-9]|_[0-9A-F]+_)*$/;

  if (!id.startsWith("key-")) {
    return null;
  }
  const unprefixed = id.slice(4);
  // Verify that the ID is the correct format.
  if (!ESCAPED_ID_FORMAT.test(unprefixed)) {
    return null;
  }
  return unprefixed.replace(/_([0-9A-F]+)_/g, unescapeNumber);
}

// An "id schema" used to validate Kinto IDs and generate new ones.
const storageSyncIdSchema = {
  // We should never generate IDs; chrome.storage only acts as a
  // key-value store, so we should always have a key.
  generate() {
    throw new Error("cannot generate IDs");
  },

  // See keyToId and idToKey for more details.
  validate(id) {
    return idToKey(id) !== null;
  },
};

// An "id schema" used for the system collection, which doesn't
// require validation or generation of IDs.
const cryptoCollectionIdSchema = {
  generate() {
    throw new Error("cannot generate IDs for system collection");
  },

  validate() {
    return true;
  },
};

/**
 * Wrapper around the crypto collection providing some handy utilities.
 */
class CryptoCollection {
  constructor(fxaService) {
    this._fxaService = fxaService;
  }

  async getCollection() {
    throwIfNoFxA(this._fxaService, "tried to access cryptoCollection");
    const { kinto } = await storageSyncInit();
    return kinto.collection(STORAGE_SYNC_CRYPTO_COLLECTION_NAME, {
      idSchema: cryptoCollectionIdSchema,
      remoteTransformers: [
        new KeyRingEncryptionRemoteTransformer(this._fxaService),
      ],
    });
  }

  /**
   * Generate a new salt for use in hashing extension and record
   * IDs.
   *
   * @returns {string} A base64-encoded string of the salt
   */
  getNewSalt() {
    return btoa(
      lazy.CryptoUtils.generateRandomBytesLegacy(
        STORAGE_SYNC_CRYPTO_SALT_LENGTH_BYTES
      )
    );
  }

  /**
   * Retrieve the keyring record from the crypto collection.
   *
   * You can use this if you want to check metadata on the keyring
   * record rather than use the keyring itself.
   *
   * The keyring record, if present, should have the structure:
   *
   * - kbHash: a hash of the user's kB. When this changes, we will
   *   try to sync the collection.
   * - uuid: a record identifier. This will only change when we wipe
   *   the collection (due to kB getting reset).
   * - keys: a "WBO" form of a CollectionKeyManager.
   * - salts: a normal JS Object with keys being collection IDs and
   *   values being base64-encoded salts to use when hashing IDs
   *   for that collection.
   *
   * @returns {Promise<object>}
   */
  async getKeyRingRecord() {
    const collection = await this.getCollection();
    const cryptoKeyRecord = await collection.getAny(
      STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID
    );

    let data = cryptoKeyRecord.data;
    if (!data) {
      // This is a new keyring. Invent an ID for this record. If this
      // changes, it means a client replaced the keyring, so we need to
      // reupload everything.
      const uuid = Services.uuid.generateUUID().toString();
      data = { uuid, id: STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID };
    }
    return data;
  }

  async getSalts() {
    const cryptoKeyRecord = await this.getKeyRingRecord();
    return cryptoKeyRecord && cryptoKeyRecord.salts;
  }

  /**
   * Used for testing with a known salt.
   *
   * @param {string} extensionId  The extension ID for which to set a
   *     salt.
   * @param {string} salt  The salt to use for this extension, as a
   *     base64-encoded salt.
   */
  async _setSalt(extensionId, salt) {
    const cryptoKeyRecord = await this.getKeyRingRecord();
    cryptoKeyRecord.salts = cryptoKeyRecord.salts || {};
    cryptoKeyRecord.salts[extensionId] = salt;
    await this.upsert(cryptoKeyRecord);
  }

  /**
   * Hash an extension ID for a given user so that an attacker can't
   * identify the extensions a user has installed.
   *
   * The extension ID is assumed to be a string (i.e. series of
   * code points), and its UTF8 encoding is prefixed with the salt
   * for that collection and hashed.
   *
   * The returned hash must conform to the syntax for Kinto
   * identifiers, which (as of this writing) must match
   * [a-zA-Z0-9][a-zA-Z0-9_-]*. We thus encode the hash using
   * "base64-url" without padding (so that we don't get any equals
   * signs (=)). For fear that a hash could start with a hyphen
   * (-) or an underscore (_), prefix it with "ext-".
   *
   * @param {string} extensionId The extension ID to obfuscate.
   * @returns {Promise<bytestring>} A collection ID suitable for use to sync to.
   */
  extensionIdToCollectionId(extensionId) {
    return this.hashWithExtensionSalt(
      lazy.CommonUtils.encodeUTF8(extensionId),
      extensionId
    ).then(hash => `ext-${hash}`);
  }

  /**
   * Hash some value with the salt for the given extension.
   *
   * The value should be a "bytestring", i.e. a string whose
   * "characters" are values, each within [0, 255]. You can produce
   * such a bytestring using e.g. CommonUtils.encodeUTF8.
   *
   * @typedef {string} bytestring
   *
   * The returned value is a base64url-encoded string of the hash.
   *
   * @param {bytestring} value The value to be hashed.
   * @param {string} extensionId The ID of the extension whose salt
   *                             we should use.
   * @returns {Promise<bytestring>} The hashed value.
   */
  async hashWithExtensionSalt(value, extensionId) {
    const salts = await this.getSalts();
    const saltBase64 = salts && salts[extensionId];
    if (!saltBase64) {
      // This should never happen; salts should be populated before
      // we need them by ensureCanSync.
      throw new Error(
        `no salt available for ${extensionId}; how did this happen?`
      );
    }

    const hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
      Ci.nsICryptoHash
    );
    hasher.init(hasher.SHA256);

    const salt = atob(saltBase64);
    const message = `${salt}\x00${value}`;
    const hash = lazy.CryptoUtils.digestBytes(message, hasher);
    return lazy.CommonUtils.encodeBase64URL(hash, false);
  }

  /**
   * Retrieve the actual keyring from the crypto collection.
   *
   * @returns {Promise<CollectionKeyManager>}
   */
  async getKeyRing() {
    const cryptoKeyRecord = await this.getKeyRingRecord();
    const collectionKeys = new lazy.CollectionKeyManager();
    if (cryptoKeyRecord.keys) {
      collectionKeys.setContents(
        cryptoKeyRecord.keys,
        cryptoKeyRecord.last_modified
      );
    } else {
      // We never actually use the default key, so it's OK if we
      // generate one multiple times.
      await collectionKeys.generateDefaultKey();
    }
    // Pass through uuid field so that we can save it if we need to.
    collectionKeys.uuid = cryptoKeyRecord.uuid;
    return collectionKeys;
  }

  async updateKBHash(kbHash) {
    const coll = await this.getCollection();
    await coll.update(
      { id: STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID, kbHash: kbHash },
      { patch: true }
    );
  }

  async upsert(record) {
    const collection = await this.getCollection();
    await collection.upsert(record);
  }

  async sync(extensionStorageSyncKinto) {
    const collection = await this.getCollection();
    return extensionStorageSyncKinto._syncCollection(collection, {
      strategy: "server_wins",
    });
  }

  /**
   * Reset sync status for ALL collections by directly
   * accessing the FirefoxAdapter.
   */
  async resetSyncStatus() {
    const coll = await this.getCollection();
    await coll.db.resetSyncStatus();
  }

  // Used only for testing.
  async _clear() {
    const collection = await this.getCollection();
    await collection.clear();
  }
}

/**
 * An EncryptionRemoteTransformer for extension records.
 *
 * It uses the special "keys" record to find a key for a given
 * extension, thus its name
 * CollectionKeyEncryptionRemoteTransformer.
 *
 * Also, during encryption, it will replace the ID of the new record
 * with a hashed ID, using the salt for this collection.
 *
 * @param {string} extensionId The extension ID for which to find a key.
 */
let CollectionKeyEncryptionRemoteTransformer = class extends EncryptionRemoteTransformer {
  constructor(cryptoCollection, keyring, extensionId) {
    super();
    this.cryptoCollection = cryptoCollection;
    this.keyring = keyring;
    this.extensionId = extensionId;
  }

  async getKeys() {
    if (!this.keyring.hasKeysFor([this.extensionId])) {
      // This should never happen. Keys should be created (and
      // synced) at the beginning of the sync cycle.
      throw new Error(
        `tried to encrypt records for ${this.extensionId}, but key is not present`
      );
    }
    return this.keyring.keyForCollection(this.extensionId);
  }

  getEncodedRecordId(record) {
    // It isn't really clear whether kinto.js record IDs are
    // bytestrings or strings that happen to only contain ASCII
    // characters, so encode them to be sure.
    const id = lazy.CommonUtils.encodeUTF8(record.id);
    // Like extensionIdToCollectionId, the rules about Kinto record
    // IDs preclude equals signs or strings starting with a
    // non-alphanumeric, so prefix all IDs with a constant "id-".
    return this.cryptoCollection
      .hashWithExtensionSalt(id, this.extensionId)
      .then(hash => `id-${hash}`);
  }
};

/**
 * Clean up now that one context is no longer using this extension's collection.
 *
 * @param {Extension} extension
 *                    The extension whose context just ended.
 * @param {BaseContext} context
 *                  The context that just ended.
 */
function cleanUpForContext(extension, context) {
  const contexts = extensionContexts.get(extension);
  contexts.delete(context);
  if (contexts.size === 0) {
    // Nobody else is using this collection. Clean up.
    extensionContexts.delete(extension);
  }
}

/**
 * Generate a promise that produces the Collection for an extension.
 *
 * @param {Extension} extension
 *                    The extension whose collection needs to
 *                    be opened.
 * @param {object} options
 *                 Options to be passed to the call to `.collection()`.
 * @returns {Promise<Collection>}
 */
const openCollection = async function (extension, options = {}) {
  let collectionId = extension.id;
  const { kinto } = await storageSyncInit();
  const coll = kinto.collection(collectionId, {
    ...options,
    idSchema: storageSyncIdSchema,
  });
  return coll;
};

export class ExtensionStorageSyncKinto {
  /**
   * @param {FXAccounts} fxaService (Optional) If not
   *    present, trying to sync will fail.
   */
  constructor(fxaService) {
    this._fxaService = fxaService;
    this.cryptoCollection = new CryptoCollection(fxaService);
    this.listeners = new WeakMap();
    this.backend = "kinto";
  }

  /**
   * Get a set of extensions to sync (including the ones with an
   * active extension context that used the storage.sync API and
   * the extensions that are enabled and have been synced before).
   *
   * @returns {Promise<Set<Extension>>}
   *   A promise which resolves to the set of the extensions to sync.
   */
  async getExtensions() {
    // Start from the set of the extensions with an active
    // context that used the storage.sync APIs.
    const extensions = new Set(extensionContexts.keys());

    const allEnabledExtensions = await lazy.AddonManager.getAddonsByTypes([
      "extension",
    ]);

    // Get the existing extension collections salts.
    const keysRecord = await this.cryptoCollection.getKeyRingRecord();

    // Add any enabled extensions that have been synced before.
    for (const addon of allEnabledExtensions) {
      if (this.hasSaltsFor(keysRecord, [addon.id])) {
        const policy = WebExtensionPolicy.getByID(addon.id);
        if (policy && policy.extension) {
          extensions.add(policy.extension);
        }
      }
    }

    return extensions;
  }

  async syncAll() {
    const extensions = await this.getExtensions();
    const extIds = Array.from(extensions, extension => extension.id);
    log.debug(`Syncing extension settings for ${JSON.stringify(extIds)}`);
    if (!extIds.length) {
      // No extensions to sync. Get out.
      return;
    }
    await this.ensureCanSync(extIds);
    await this.checkSyncKeyRing();
    const keyring = await this.cryptoCollection.getKeyRing();
    const promises = Array.from(extensions, extension => {
      const remoteTransformers = [
        new CollectionKeyEncryptionRemoteTransformer(
          this.cryptoCollection,
          keyring,
          extension.id
        ),
      ];
      return openCollection(extension, { remoteTransformers }).then(coll => {
        return this.sync(extension, coll);
      });
    });
    await Promise.all(promises);
  }

  async sync(extension, collection) {
    throwIfNoFxA(this._fxaService, "syncing chrome.storage.sync");
    const isSignedIn = !!(await this._fxaService.getSignedInUser());
    if (!isSignedIn) {
      // FIXME: this should support syncing to self-hosted
      log.info("User was not signed into FxA; cannot sync");
      throw new Error("Not signed in to FxA");
    }
    const collectionId = await this.cryptoCollection.extensionIdToCollectionId(
      extension.id
    );
    let syncResults;
    try {
      syncResults = await this._syncCollection(collection, {
        strategy: "client_wins",
        collection: collectionId,
      });
    } catch (err) {
      log.warn("Syncing failed", err);
      throw err;
    }

    let changes = {};
    for (const record of syncResults.created) {
      changes[record.key] = {
        newValue: record.data,
      };
    }
    for (const record of syncResults.updated) {
      // N.B. It's safe to just pick old.key because it's not
      // possible to "rename" a record in the storage.sync API.
      const key = record.old.key;
      changes[key] = {
        oldValue: record.old.data,
        newValue: record.new.data,
      };
    }
    for (const record of syncResults.deleted) {
      changes[record.key] = {
        oldValue: record.data,
      };
    }
    for (const resolution of syncResults.resolved) {
      // FIXME: We can't send a "changed" notification because
      // kinto.js only provides the newly-resolved value. But should
      // we even send a notification? We use CLIENT_WINS so nothing
      // has really "changed" on this end. (The change will come on
      // the other end when it pulls down the update, which is handled
      // by the "updated" case above.) If we are going to send a
      // notification, what best values for "old" and "new"?  This
      // might violate client code's assumptions, since from their
      // perspective, we were in state L, but this diff is from R ->
      // L.
      const accepted = resolution.accepted;
      changes[accepted.key] = {
        newValue: accepted.data,
      };
    }
    if (Object.keys(changes).length) {
      this.notifyListeners(extension, changes);
    }
    log.info(`Successfully synced '${collection.name}'`);
  }

  /**
   * Utility function that handles the common stuff about syncing all
   * Kinto collections (including "meta" collections like the crypto
   * one).
   *
   * @param {Collection} collection
   * @param {object} options
   *                 Additional options to be passed to sync().
   * @returns {Promise<SyncResultObject>}
   */
  _syncCollection(collection, options) {
    // FIXME: this should support syncing to self-hosted
    return this._requestWithToken(
      `Syncing ${collection.name}`,
      function (token) {
        const allOptions = Object.assign(
          {},
          {
            remote: lazy.prefStorageSyncServerURL,
            headers: {
              Authorization: "Bearer " + token,
            },
          },
          options
        );

        return collection.sync(allOptions);
      }
    );
  }

  // Make a Kinto request with a current FxA token.
  // If the response indicates that the token might have expired,
  // retry the request.
  async _requestWithToken(description, f) {
    throwIfNoFxA(
      this._fxaService,
      "making remote requests from chrome.storage.sync"
    );
    const fxaToken = await this._fxaService.getOAuthToken(FXA_OAUTH_OPTIONS);
    try {
      return await f(fxaToken);
    } catch (e) {
      if (e && e.response && e.response.status == 401) {
        // Our token might have expired. Refresh and retry.
        log.info("Token might have expired");
        await this._fxaService.removeCachedOAuthToken({ token: fxaToken });
        const newToken =
          await this._fxaService.getOAuthToken(FXA_OAUTH_OPTIONS);

        // If this fails too, let it go.
        return f(newToken);
      }
      // Otherwise, we don't know how to handle this error, so just reraise.
      log.error(`${description}: request failed`, e);
      throw e;
    }
  }

  /**
   * Helper similar to _syncCollection, but for deleting the user's bucket.
   *
   * @returns {Promise<void>}
   */
  _deleteBucket() {
    log.error("Deleting default bucket and everything in it");
    return this._requestWithToken("Clearing server", function (token) {
      const headers = { Authorization: "Bearer " + token };
      const kintoHttp = new lazy.KintoHttpClient(
        lazy.prefStorageSyncServerURL,
        {
          headers: headers,
          timeout: KINTO_REQUEST_TIMEOUT,
        }
      );
      return kintoHttp.deleteBucket("default");
    });
  }

  async ensureSaltsFor(keysRecord, extIds) {
    const newSalts = Object.assign({}, keysRecord.salts);
    for (let collectionId of extIds) {
      if (newSalts[collectionId]) {
        continue;
      }

      newSalts[collectionId] = this.cryptoCollection.getNewSalt();
    }

    return newSalts;
  }

  /**
   * Check whether the keys record (provided) already has salts for
   * all the extensions given in extIds.
   *
   * @param {object} keysRecord A previously-retrieved keys record.
   * @param {Array<string>} extIds The IDs of the extensions which
   *                need salts.
   * @returns {boolean}
   */
  hasSaltsFor(keysRecord, extIds) {
    if (!keysRecord.salts) {
      return false;
    }

    for (let collectionId of extIds) {
      if (!keysRecord.salts[collectionId]) {
        return false;
      }
    }

    return true;
  }

  /**
   * Recursive promise that terminates when our local collectionKeys,
   * as well as that on the server, have keys for all the extensions
   * in extIds.
   *
   * @param {Array<string>} extIds
   *                        The IDs of the extensions which need keys.
   * @returns {Promise<CollectionKeyManager>}
   */
  async ensureCanSync(extIds) {
    const keysRecord = await this.cryptoCollection.getKeyRingRecord();
    const collectionKeys = await this.cryptoCollection.getKeyRing();
    if (
      collectionKeys.hasKeysFor(extIds) &&
      this.hasSaltsFor(keysRecord, extIds)
    ) {
      return collectionKeys;
    }

    log.info(`Need to create keys and/or salts for ${JSON.stringify(extIds)}`);
    const kbHash = await getKBHash(this._fxaService);
    const newKeys = await collectionKeys.ensureKeysFor(extIds);
    const newSalts = await this.ensureSaltsFor(keysRecord, extIds);
    const newRecord = {
      id: STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID,
      keys: newKeys.asWBO().cleartext,
      salts: newSalts,
      uuid: collectionKeys.uuid,
      // Add a field for the current kB hash.
      kbHash: kbHash,
    };
    await this.cryptoCollection.upsert(newRecord);
    const result = await this._syncKeyRing(newRecord);
    if (result.resolved.length) {
      // We had a conflict which was automatically resolved. We now
      // have a new keyring which might have keys for the
      // collections. Recurse.
      return this.ensureCanSync(extIds);
    }

    // No conflicts. We're good.
    return newKeys;
  }

  /**
   * Update the kB in the crypto record.
   */
  async updateKeyRingKB() {
    throwIfNoFxA(this._fxaService, 'use of chrome.storage.sync "keyring"');
    const isSignedIn = !!(await this._fxaService.getSignedInUser());
    if (!isSignedIn) {
      // Although this function is meant to be called on login,
      // it's not unreasonable to check any time, even if we aren't
      // logged in.
      //
      // If we aren't logged in, we don't have any information about
      // the user's kB, so we can't be sure that the user changed
      // their kB, so just return.
      return;
    }

    const thisKBHash = await getKBHash(this._fxaService);
    await this.cryptoCollection.updateKBHash(thisKBHash);
  }

  /**
   * Make sure the keyring is up to date and synced.
   *
   * This is called on syncs to make sure that we don't sync anything
   * to any collection unless the key for that collection is on the
   * server.
   */
  async checkSyncKeyRing() {
    await this.updateKeyRingKB();

    const cryptoKeyRecord = await this.cryptoCollection.getKeyRingRecord();
    if (cryptoKeyRecord && cryptoKeyRecord._status !== "synced") {
      // We haven't successfully synced the keyring since the last
      // change. This could be because kB changed and we touched the
      // keyring, or it could be because we failed to sync after
      // adding a key. Either way, take this opportunity to sync the
      // keyring.
      await this._syncKeyRing(cryptoKeyRecord);
    }
  }

  async _syncKeyRing(cryptoKeyRecord) {
    throwIfNoFxA(this._fxaService, 'syncing chrome.storage.sync "keyring"');
    try {
      // Try to sync using server_wins.
      //
      // We use server_wins here because whatever is on the server is
      // at least consistent with itself -- the crypto in the keyring
      // matches the crypto on the collection records. This is because
      // we generate and upload keys just before syncing data.
      //
      // It's possible that we can't decode the version on the server.
      // This can happen if a user is locked out of their account, and
      // does a "reset password" to get in on a new device. In this
      // case, we are in a bind -- we can't decrypt the record on the
      // server, so we can't merge keys. If this happens, we try to
      // figure out if we're the one with the correct (new) kB or if
      // we just got locked out because we have the old kB. If we're
      // the one with the correct kB, we wipe the server and reupload
      // everything, including a new keyring.
      //
      // If another device has wiped the server, we need to reupload
      // everything we have on our end too, so we detect this by
      // adding a UUID to the keyring. UUIDs are preserved throughout
      // the lifetime of a keyring, so the only time a keyring UUID
      // changes is when a new keyring is uploaded, which only happens
      // after a server wipe. So when we get a "conflict" (resolved by
      // server_wins), we check whether the server version has a new
      // UUID. If so, reset our sync status, so that we'll reupload
      // everything.
      const result = await this.cryptoCollection.sync(this);
      if (result.resolved.length) {
        // Automatically-resolved conflict. It should
        // be for the keys record.
        const resolutionIds = result.resolved.map(resolution => resolution.id);
        if (resolutionIds > 1) {
          // This should never happen -- there is only ever one record
          // in this collection.
          log.error(
            `Too many resolutions for sync-storage-crypto collection: ${JSON.stringify(
              resolutionIds
            )}`
          );
        }
        const keyResolution = result.resolved[0];
        if (keyResolution.id != STORAGE_SYNC_CRYPTO_KEYRING_RECORD_ID) {
          // This should never happen -- there should only ever be the
          // keyring in this collection.
          log.error(
            `Strange conflict in sync-storage-crypto collection: ${JSON.stringify(
              resolutionIds
            )}`
          );
        }

        // Due to a bug in the server-side code (see
        // https://github.com/Kinto/kinto/issues/1209), lots of users'
        // keyrings were deleted. We discover this by trying to push a
        // new keyring (because the user aded a new extension), and we
        // get a conflict. We have SERVER_WINS, so the client will
        // accept this deleted keyring and delete it locally. Discover
        // this and undo it.
        if (keyResolution.accepted === null) {
          log.error("Conflict spotted -- the server keyring was deleted");
          await this.cryptoCollection.upsert(keyResolution.rejected);
          // It's possible that the keyring on the server that was
          // deleted had keys for other extensions, which had already
          // encrypted data. For this to happen, another client would
          // have had to upload the keyring and then the delete happened
          // before this client did a sync (and got the new extension
          // and tried to sync the keyring again). Just to be safe,
          // let's signal that something went wrong and we should wipe
          // the bucket.
          throw new ServerKeyringDeleted();
        }

        if (keyResolution.accepted.uuid != cryptoKeyRecord.uuid) {
          log.info(
            `Detected a new UUID (${keyResolution.accepted.uuid}, was ${cryptoKeyRecord.uuid}). Resetting sync status for everything.`
          );
          await this.cryptoCollection.resetSyncStatus();

          // Server version is now correct. Return that result.
          return result;
        }
      }
      // No conflicts, or conflict was just someone else adding keys.
      return result;
    } catch (e) {
      if (
        KeyRingEncryptionRemoteTransformer.isOutdatedKB(e) ||
        e instanceof ServerKeyringDeleted ||
        // This is another way that ServerKeyringDeleted can
        // manifest; see bug 1350088 for more details.
        e.message.includes("Server has been flushed.")
      ) {
        // Check if our token is still valid, or if we got locked out
        // between starting the sync and talking to Kinto.
        const isSessionValid = await this._fxaService.checkAccountStatus();
        if (isSessionValid) {
          log.error(
            "Couldn't decipher old keyring; deleting the default bucket and resetting sync status"
          );
          await this._deleteBucket();
          await this.cryptoCollection.resetSyncStatus();

          // Reupload our keyring, which is the only new keyring.
          // We don't want client_wins here because another device
          // could have uploaded another keyring in the meantime.
          return this.cryptoCollection.sync(this);
        }
      }
      throw e;
    }
  }

  registerInUse(extension, context) {
    // Register that the extension and context are in use.
    const contexts = extensionContexts.get(extension);
    if (!contexts.has(context)) {
      // New context. Register it and make sure it cleans itself up
      // when it closes.
      contexts.add(context);
      context.callOnClose({
        close: () => cleanUpForContext(extension, context),
      });
    }
  }

  /**
   * Get the collection for an extension, and register the extension
   * as being "in use".
   *
   * @param {Extension} extension
   *                    The extension for which we are seeking
   *                    a collection.
   * @param {BaseContext} context
   *                  The context of the extension, so that we can
   *                  stop syncing the collection when the extension ends.
   * @returns {Promise<Collection>}
   */
  getCollection(extension, context) {
    this.registerInUse(extension, context);
    return openCollection(extension);
  }

  async set(extension, items, context) {
    const coll = await this.getCollection(extension, context);
    const keys = Object.keys(items);
    const ids = keys.map(keyToId);
    const changes = await coll.execute(
      txn => {
        let changes = {};
        for (let [i, key] of keys.entries()) {
          const id = ids[i];
          let item = items[key];
          let { oldRecord } = txn.upsert({
            id,
            key,
            data: item,
          });
          changes[key] = {
            newValue: item,
          };
          if (oldRecord) {
            // Extract the "data" field from the old record, which
            // represents the value part of the key-value store
            changes[key].oldValue = oldRecord.data;
          }
        }
        return changes;
      },
      { preloadIds: ids }
    );
    this.notifyListeners(extension, changes);
  }

  async remove(extension, keys, context) {
    const coll = await this.getCollection(extension, context);
    keys = [].concat(keys);
    const ids = keys.map(keyToId);
    let changes = {};
    await coll.execute(
      txn => {
        for (let [i, key] of keys.entries()) {
          const id = ids[i];
          const res = txn.deleteAny(id);
          if (res.deleted) {
            changes[key] = {
              oldValue: res.data.data,
            };
          }
        }
        return changes;
      },
      { preloadIds: ids }
    );
    if (Object.keys(changes).length) {
      this.notifyListeners(extension, changes);
    }
  }

  /* Wipe local data for all collections without causing the changes to be synced */
  async clearAll() {
    const extensions = await this.getExtensions();
    const extIds = Array.from(extensions, extension => extension.id);
    log.debug(`Clearing extension data for ${JSON.stringify(extIds)}`);
    if (extIds.length) {
      const promises = Array.from(extensions, extension => {
        return openCollection(extension).then(coll => {
          return coll.clear();
        });
      });
      await Promise.all(promises);
    }

    // and clear the crypto collection.
    const cc = await this.cryptoCollection.getCollection();
    await cc.clear();
  }

  async clear(extension, context) {
    // We can't call Collection#clear here, because that just clears
    // the local database. We have to explicitly delete everything so
    // that the deletions can be synced as well.
    const coll = await this.getCollection(extension, context);
    const res = await coll.list();
    const records = res.data;
    const keys = records.map(record => record.key);
    await this.remove(extension, keys, context);
  }

  async get(extension, spec, context) {
    const coll = await this.getCollection(extension, context);
    let keys, records;
    if (spec === null) {
      records = {};
      const res = await coll.list();
      for (let record of res.data) {
        records[record.key] = record.data;
      }
      return records;
    }
    if (typeof spec === "string") {
      keys = [spec];
      records = {};
    } else if (Array.isArray(spec)) {
      keys = spec;
      records = {};
    } else {
      keys = Object.keys(spec);
      records = Cu.cloneInto(spec, {});
    }

    for (let key of keys) {
      const res = await coll.getAny(keyToId(key));
      if (res.data && res.data._status != "deleted") {
        records[res.data.key] = res.data.data;
      }
    }

    return records;
  }

  async getBytesInUse(extension, keys, context) {
    // This is defined by the chrome spec as being the length of the key and
    // the length of the json repr of the value.
    let size = 0;
    let data = await this.get(extension, keys, context);
    for (const [key, value] of Object.entries(data)) {
      size += key.length + JSON.stringify(value).length;
    }
    return size;
  }

  addOnChangedListener(extension, listener, context) {
    let listeners = this.listeners.get(extension) || new Set();
    listeners.add(listener);
    this.listeners.set(extension, listeners);

    this.registerInUse(extension, context);
  }

  removeOnChangedListener(extension, listener) {
    let listeners = this.listeners.get(extension);
    listeners.delete(listener);
    if (listeners.size == 0) {
      this.listeners.delete(extension);
    }
  }

  notifyListeners(extension, changes) {
    lazy.Observers.notify("ext.storage.sync-changed");
    let listeners = this.listeners.get(extension) || new Set();
    if (listeners) {
      for (let listener of listeners) {
        lazy.ExtensionCommon.runSafeSyncWithoutClone(listener, changes);
      }
    }
  }
}

/**
 * Global ExtensionStorageSyncKinto instance that extensions and Fx Sync use.
 * On Android, because there's no FXAccounts instance, any syncing
 * operations will fail.
 */
export const extensionStorageSyncKinto = new ExtensionStorageSyncKinto(
  _fxaService
);

// For test use only.
export const KintoStorageTestUtils = {
  CollectionKeyEncryptionRemoteTransformer,
  CryptoCollection,
  EncryptionRemoteTransformer,
  KeyRingEncryptionRemoteTransformer,
  cleanUpForContext,
  idToKey,
  keyToId,
};
