/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

export function LoginManagerCrypto_SDR() {
  // Ensure NSS is initialized.
  Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);
}

LoginManagerCrypto_SDR.prototype = {
  classID: Components.ID("{dc6c2976-0f73-4f1f-b9ff-3d72b4e28309}"),
  QueryInterface: ChromeUtils.generateQI(["nsILoginManagerCrypto"]),

  __decoderRing: null, // nsSecretDecoderRing service
  get _decoderRing() {
    if (!this.__decoderRing) {
      this.__decoderRing = Cc["@mozilla.org/security/sdr;1"].getService(
        Ci.nsISecretDecoderRing
      );
    }
    return this.__decoderRing;
  },

  __utfConverter: null, // UCS2 <--> UTF8 string conversion
  get _utfConverter() {
    if (!this.__utfConverter) {
      this.__utfConverter = Cc[
        "@mozilla.org/intl/scriptableunicodeconverter"
      ].createInstance(Ci.nsIScriptableUnicodeConverter);
      this.__utfConverter.charset = "UTF-8";
    }
    return this.__utfConverter;
  },

  _utfConverterReset() {
    this.__utfConverter = null;
  },

  _uiBusy: false,

  /*
   * encrypt
   *
   * Encrypts the specified string, using the SecretDecoderRing.
   *
   * Returns the encrypted string, or throws an exception if there was a
   * problem.
   */
  encrypt(plainText) {
    let cipherText = null;

    let wasLoggedIn = this.isLoggedIn;
    let canceledMP = false;

    this._uiBusy = !wasLoggedIn;
    try {
      let plainOctet = this._utfConverter.ConvertFromUnicode(plainText);
      plainOctet += this._utfConverter.Finish();
      cipherText = this._decoderRing.encryptString(plainOctet);
    } catch (e) {
      this.log(`Failed to encrypt string with error ${e.name}.`);
      // If the user clicks Cancel, we get NS_ERROR_FAILURE.
      // (unlike decrypting, which gets NS_ERROR_NOT_AVAILABLE).
      if (e.result == Cr.NS_ERROR_FAILURE) {
        canceledMP = true;
        throw Components.Exception(
          "User canceled primary password entry",
          Cr.NS_ERROR_ABORT
        );
      } else {
        throw Components.Exception(
          "Couldn't encrypt string",
          Cr.NS_ERROR_FAILURE
        );
      }
    } finally {
      this._uiBusy = false;
      this._primaryPasswordPromptFinished(wasLoggedIn, canceledMP, "encrypt");
    }
    return cipherText;
  },

  /*
   * encryptMany
   *
   * Encrypts the specified strings, using the SecretDecoderRing.
   *
   * Returns a promise which resolves with the the encrypted strings,
   * or throws/rejects with an error if there was a problem.
   */
  async encryptMany(plaintexts) {
    if (!Array.isArray(plaintexts) || !plaintexts.length) {
      throw Components.Exception(
        "Need at least one plaintext to encrypt",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let cipherTexts;

    let wasLoggedIn = this.isLoggedIn;
    let canceledMP = false;

    this._uiBusy = !wasLoggedIn;
    try {
      cipherTexts = await this._decoderRing.asyncEncryptStrings(plaintexts);
    } catch (e) {
      this.log(`Failed to encrypt strings with error ${e.name}.`);
      // If the user clicks Cancel, we get NS_ERROR_FAILURE.
      // (unlike decrypting, which gets NS_ERROR_NOT_AVAILABLE).
      if (e.result == Cr.NS_ERROR_FAILURE) {
        canceledMP = true;
        throw Components.Exception(
          "User canceled primary password entry",
          Cr.NS_ERROR_ABORT
        );
      } else {
        throw Components.Exception(
          "Couldn't encrypt strings",
          Cr.NS_ERROR_FAILURE
        );
      }
    } finally {
      this._uiBusy = false;
      this._primaryPasswordPromptFinished(wasLoggedIn, canceledMP, "encrypt");
    }
    return cipherTexts;
  },

  /*
   * decrypt
   *
   * Decrypts the specified string, using the SecretDecoderRing.
   *
   * Returns the decrypted string, or throws an exception if there was a
   * problem.
   */
  decrypt(cipherText) {
    let plainText = null;

    let wasLoggedIn = this.isLoggedIn;
    let canceledMP = false;

    this._uiBusy = !wasLoggedIn;
    try {
      let plainOctet;
      plainOctet = this._decoderRing.decryptString(cipherText);
      plainText = this._utfConverter.ConvertToUnicode(plainOctet);
    } catch (e) {
      this.log(
        `Failed to decrypt cipher text of length ${cipherText.length} with error ${e.name}.`
      );

      // In the unlikely event the converter threw, reset it.
      this._utfConverterReset();

      // If the user clicks Cancel, we get NS_ERROR_NOT_AVAILABLE.
      // If the cipherText is bad / wrong key, we get NS_ERROR_FAILURE
      // Wrong passwords are handled by the decoderRing reprompting;
      // we get no notification.
      if (e.result == Cr.NS_ERROR_NOT_AVAILABLE) {
        canceledMP = true;
        throw Components.Exception(
          "User canceled primary password entry",
          Cr.NS_ERROR_ABORT
        );
      } else {
        throw Components.Exception(
          "Couldn't decrypt string",
          Cr.NS_ERROR_FAILURE
        );
      }
    } finally {
      this._uiBusy = false;
      this._primaryPasswordPromptFinished(wasLoggedIn, canceledMP, "decrypt");
    }

    return plainText;
  },

  /**
   * Decrypts the specified strings, using the SecretDecoderRing.
   *
   * @returns {Promise<string[]>}
   *   Resolved to the decrypted strings. If a string cannot be decrypted, the
   *   empty string is returned for that instance. Callers will need to use
   *   decrypt() to determine if the encrypted string is invalid or intentionally
   *   empty.
   *   Throws/rejects with an error if there was a problem.
   */
  async decryptMany(cipherTexts) {
    if (!Array.isArray(cipherTexts) || !cipherTexts.length) {
      throw Components.Exception(
        "Need at least one ciphertext to decrypt",
        Cr.NS_ERROR_INVALID_ARG
      );
    }

    let plainTexts = [];

    let wasLoggedIn = this.isLoggedIn;
    let canceledMP = false;

    this._uiBusy = !wasLoggedIn;
    try {
      plainTexts = await this._decoderRing.asyncDecryptStrings(cipherTexts);
    } catch (e) {
      this.log(`Failed to decrypt strings with error ${e.name}.`);
      // If the user clicks Cancel, we get NS_ERROR_NOT_AVAILABLE.
      // If the cipherText is bad / wrong key, we get NS_ERROR_FAILURE
      // Wrong passwords are handled by the decoderRing reprompting;
      // we get no notification.
      if (e.result == Cr.NS_ERROR_NOT_AVAILABLE) {
        canceledMP = true;
        throw Components.Exception(
          "User canceled primary password entry",
          Cr.NS_ERROR_ABORT
        );
      } else {
        throw Components.Exception(
          "Couldn't decrypt strings: " + e.result,
          Cr.NS_ERROR_FAILURE
        );
      }
    } finally {
      this._uiBusy = false;
      this._primaryPasswordPromptFinished(wasLoggedIn, canceledMP, "decrypt");
    }
    return plainTexts;
  },

  /*
   * uiBusy
   */
  get uiBusy() {
    return this._uiBusy;
  },

  /*
   * isLoggedIn
   */
  get isLoggedIn() {
    let token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
      Ci.nsIPKCS11Token
    );
    return !token.hasPassword || token.isLoggedIn;
  },

  /*
   * defaultEncType
   */
  get defaultEncType() {
    return Ci.nsILoginManagerCrypto.ENCTYPE_SDR;
  },

  /**
   * Wraps up an SDR operation that may have prompted the user for their
   * primary password, notifying observers and recording telemetry if it did.
   *
   * @param {boolean} wasLoggedIn
   *        Whether the token was already unlocked when the operation started.
   * @param {boolean} canceledMP
   *        Whether the operation failed because the user dismissed the prompt.
   * @param {string} operation
   *        The operation that needed the key, "encrypt" or "decrypt".
   */
  _primaryPasswordPromptFinished(wasLoggedIn, canceledMP, operation) {
    // A locked token always prompts, and a cancellation can only come from a
    // prompt. NSS retries a wrong password without telling us, so a single
    // event can stand for more than one dialog. Returning here also keeps the
    // unlocked path from asking the token for its state.
    if (wasLoggedIn && !canceledMP) {
      return;
    }

    let isLoggedIn = this.isLoggedIn;

    // If we triggered a primary password prompt, notify observers.
    if (!wasLoggedIn && isLoggedIn) {
      this._notifyObservers("passwordmgr-crypto-login");
    } else if (canceledMP) {
      this._notifyObservers("passwordmgr-crypto-loginCanceled");
    }

    let result = "error";
    if (isLoggedIn) {
      result = "success";
    } else if (canceledMP) {
      result = "cancel";
    }

    Glean.pwmgr.primaryPasswordPrompt.record({
      source: "crypto_sdr",
      trigger: operation,
      result,
    });
  },

  /*
   * _notifyObservers
   */
  _notifyObservers(topic) {
    this.log(`Prompted for a primary password, notifying for ${topic}`);
    Services.obs.notifyObservers(null, topic);
  },
}; // end of nsLoginManagerCrypto_SDR implementation

ChromeUtils.defineLazyGetter(LoginManagerCrypto_SDR.prototype, "log", () => {
  let logger = lazy.LoginHelper.createLogger("LoginCrypto");
  return logger.log.bind(logger);
});
