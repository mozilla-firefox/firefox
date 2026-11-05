/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "BackupService.ArchiveEncryption",
    maxLogLevel: Services.prefs.getBoolPref("browser.backup.log", false)
      ? "Debug"
      : "Warn",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  ArchiveUtils: "moz-src:///browser/components/backup/ArchiveUtils.sys.mjs",
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  BackupError: "moz-src:///browser/components/backup/BackupError.mjs",
  ERRORS: "chrome://browser/content/backup/backup-constants.mjs",
});

/**
 * ArchiveEncryptionState encapsulates key primitives and wrapped secrets that
 * can be safely serialized to the filesystem. An ArchiveEncryptionState is
 * used to compute the necessary keys for encrypting a backup archive.
 */
export class ArchiveEncryptionState {
  /**
   * A hack that lets us ensure that an ArchiveEncryptionState cannot be
   * constructed except via the ArchiveEncryptionState.initialize static
   * method.
   *
   * See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Classes/Private_properties#simulating_private_constructors
   */
  static #isInternalConstructing = false;

  /**
   * A reference to an object holding the current state of the
   * ArchiveEncryptionState instance. When this reference is null, encryption
   * is not considered enabled.
   */
  #state = null;

  /**
   * The current version number of the ArchiveEncryptionState. This is encoded
   * in the serialized state, and is also used during calculation of the salt
   * in enable().
   *
   * @type {number}
   */
  static get VERSION() {
    return 1;
  }

  /**
   * The number of characters to generate with a CSRNG (crypto.getRandomValues)
   * if no recovery code is passed in to enable();
   *
   * @type {number}
   */
  static get GENERATED_RECOVERY_CODE_LENGTH() {
    return 14;
  }

  /**
   * The RSA-OAEP public key that will be used to derive keys for encrypting
   * backups.
   *
   * @type {CryptoKey}
   */
  get publicKey() {
    return this.#state.publicKey;
  }

  /**
   * The AES-GCM key that will be used to authenticate the owner of the backup.
   *
   * @type {CryptoKey}
   */
  get backupAuthKey() {
    return this.#state.backupAuthKey;
  }

  /**
   * A salt computed for the PBKDF2 stretching of the recovery code.
   *
   * @type {Uint8Array}
   */
  get salt() {
    return this.#state.salt;
  }

  /**
   * A nonce computed when wrapping the private key and OSKeyStore secret.
   *
   * @type {Uint8Array}
   */
  get nonce() {
    return this.#state.nonce;
  }

  /**
   * The wrapped static secrets, including the RSA-OAEP private key, and the
   * OSKeyStore secret.
   *
   * @type {Uint8Array}
   */
  get wrappedSecrets() {
    return this.#state.wrappedSecrets;
  }

  constructor() {
    if (!ArchiveEncryptionState.#isInternalConstructing) {
      throw new lazy.BackupError(
        "ArchiveEncryptionState is not constructable.",
        lazy.ERRORS.UNKNOWN
      );
    }
    ArchiveEncryptionState.#isInternalConstructing = false;
  }

  /**
   * Calculates various encryption keys and other information necessary to
   * encrypt backups, based on the passed in recoveryCode.
   *
   * This will throw if encryption is already enabled for this
   * ArchiveEncryptionState.
   *
   * @throws {Exception}
   * @param {string} [recoveryCode=null]
   *   A recovery code that will be used to drive the various encryption keys
   *   and data for backup encryption. If not supplied by the caller, a
   *   recovery code will be generated.
   * @returns {Promise<string>}
   *   Resolves with the recovery code string. If callers did not pass the
   *   recovery code in as an argument, they should not store it. They should
   *   instead display this string to the user, and then forget it altogether.
   */
  async #enable(recoveryCode = null) {
    lazy.logConsole.debug("Creating new enabled ArchiveEncryptionState");

    lazy.logConsole.debug("Generating an RSA-OEAP keyPair");
    let keyPair = await crypto.subtle.generateKey(
      {
        name: "RSA-OAEP",
        modulusLength: 2048,
        publicExponent: new Uint8Array([1, 0, 1]),
        hash: { name: "SHA-256" },
      },
      true /* extractable */,
      ["encrypt", "decrypt"]
    );

    if (!recoveryCode) {
      // A recovery code wasn't provided, so we'll generate one using
      // getRandomValues, and make sure it's GENERATED_RECOVERY_CODE_LENGTH
      // characters long.
      recoveryCode = "";
      // We've intentionally replaced some lookalike characters (O, o, 0, l, I,
      // 1) with symbols.
      const charset =
        "ABCDEFGH#JKLMN@PQRSTUVWXYZabcdefgh=jklmn+pqrstuvwxyz%!23456789";
      // getRandomValues will return a value between 0-255. In order to not
      // gain a bias on any particular character (due to wrap-around), we'll
      // ensure that we only consider random values that are less than or
      // equal to the highest multiple of charset.length that is less than
      // 255.
      let highestMultiple =
        Math.floor((255 /* upper limit */ - 1) / charset.length) *
        charset.length;

      while (
        recoveryCode.length <
        ArchiveEncryptionState.GENERATED_RECOVERY_CODE_LENGTH
      ) {
        let randomValue = new Uint8Array(1);
        crypto.getRandomValues(randomValue);
        // If the random value is higher than highestMultiple, try again.
        if (randomValue > highestMultiple) {
          continue;
        }
        // Otherwise, we're within the highest multiple, meaning we can mod
        // the generated number to choose a character from charset.
        let randomIndex = randomValue % charset.length;
        recoveryCode += charset[randomIndex];
      }
    }

    // Next, we generate a 32-byte salt, and then concatenate a static suffix
    // to it, including the version number.
    lazy.logConsole.debug("Creating salt");
    let textEncoder = new TextEncoder();
    const SALT_SUFFIX = textEncoder.encode(
      "backupkey-v" + ArchiveEncryptionState.VERSION
    );
    let saltPrefix = new Uint8Array(32);
    crypto.getRandomValues(saltPrefix);

    let salt = new Uint8Array(saltPrefix.length + SALT_SUFFIX.length);
    salt.set(saltPrefix);
    salt.set(SALT_SUFFIX, saltPrefix.length);

    let { backupAuthKey, backupEncKey } =
      await lazy.ArchiveUtils.computeBackupKeys(recoveryCode, salt);

    lazy.logConsole.debug("Encrypting secrets with encKey");
    const NONCE_SIZE = 96;
    let nonce = crypto.getRandomValues(new Uint8Array(NONCE_SIZE));

    let secrets = JSON.stringify({
      privateKey: await crypto.subtle.exportKey("jwk", keyPair.privateKey),
      OSKeyStoreSecret: await lazy.OSKeyStore.exportRecoveryPhrase(),
    });
    let secretsBytes = textEncoder.encode(secrets);

    let wrappedSecrets = new Uint8Array(
      await crypto.subtle.encrypt(
        {
          name: "AES-GCM",
          iv: nonce,
        },
        backupEncKey,
        secretsBytes
      )
    );

    this.#state = {
      publicKey: keyPair.publicKey,
      salt,
      backupAuthKey,
      nonce,
      wrappedSecrets,
    };

    return recoveryCode;
  }

  /**
   * Serializes an ArchiveEncryptionState instance into an object that can be
   * safely persisted to disk.
   *
   * @returns {Promise<object>}
   */
  async serialize() {
    let publicKey = await crypto.subtle.exportKey("jwk", this.#state.publicKey);
    let salt = lazy.ArchiveUtils.arrayToBase64(this.#state.salt);
    let backupAuthKey = lazy.ArchiveUtils.arrayToBase64(
      this.#state.backupAuthKey
    );
    let nonce = lazy.ArchiveUtils.arrayToBase64(this.#state.nonce);
    let wrappedSecrets = lazy.ArchiveUtils.arrayToBase64(
      this.#state.wrappedSecrets
    );
    let result = {
      publicKey,
      salt,
      backupAuthKey,
      nonce,
      wrappedSecrets,
      version: ArchiveEncryptionState.VERSION,
    };

    return result;
  }

  /**
   * Deserializes an object created via serialize() and updates its internal
   * state to match the deserialization.
   *
   * @param {object} stateData
   *   The object generated via serialize()
   * @returns {Promise<undefined>}
   */
  async #deserialize(stateData) {
    lazy.logConsole.debug(
      "Deserializing from state with version ",
      stateData.version
    );

    // If we ever need to do a migration from one ArchiveEncryptionState
    // version to another, this is where we might do it. We don't currently
    // have any need to do migrations just yet though, so any version that
    // doesn't match the one that we can accept is rejected.
    if (stateData.version != ArchiveEncryptionState.VERSION) {
      throw new lazy.BackupError(
        "The ArchiveEncryptionState version is from a newer version.",
        lazy.ERRORS.UNSUPPORTED_BACKUP_VERSION
      );
    }

    let publicKey = await crypto.subtle.importKey(
      "jwk",
      stateData.publicKey,
      { name: "RSA-OAEP", hash: "SHA-256" },
      true /* extractable */,
      ["encrypt"]
    );
    let backupAuthKey = lazy.ArchiveUtils.stringToArray(
      stateData.backupAuthKey
    );
    let salt = lazy.ArchiveUtils.stringToArray(stateData.salt);
    let nonce = lazy.ArchiveUtils.stringToArray(stateData.nonce);
    let wrappedSecrets = lazy.ArchiveUtils.stringToArray(
      stateData.wrappedSecrets
    );

    this.#state = {
      publicKey,
      backupAuthKey,
      salt,
      nonce,
      wrappedSecrets,
    };
  }

  /**
   * @typedef {object} InitializationResult
   * @property {string|undefined} recoveryCode
   *   The generated recovery code if the initialization happened without
   *   deserialization.
   * @property {ArchiveEncryptionState} instance
   *   The constructed ArchiveEncryptionState.
   */

  /**
   * Constructs a new ArchiveEncryptionState. If a stateData object is passed,
   * the ArchiveEncryptionState will attempt to be deserialized from it -
   * otherwise, new state data will be generated automatically. This might
   * reject if the user is prompted to authenticate to their OSKeyStore, and
   * they cancel the authentication.
   *
   * @param {object|string|undefined} stateDataOrRecoveryCode
   *   Either the object generated via serialize(), a recovery code to be
   *   used to generate the state, or undefined.
   * @returns {Promise<InitializationResult>}
   */
  static async initialize(stateDataOrRecoveryCode) {
    ArchiveEncryptionState.#isInternalConstructing = true;
    let instance = new ArchiveEncryptionState();
    if (typeof stateDataOrRecoveryCode == "object") {
      await instance.#deserialize(stateDataOrRecoveryCode);
      return { instance };
    }
    let recoveryCode = await instance.#enable(stateDataOrRecoveryCode);
    return { instance, recoveryCode };
  }
}
