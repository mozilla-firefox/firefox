/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Helpers for using OS Key Store.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
});
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "nativeOSKeyStore",
  "@mozilla.org/security/oskeystore;1",
  Ci.nsIOSKeyStore
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "osReauthenticator",
  "@mozilla.org/security/osreauthenticator;1",
  Ci.nsIOSReauthenticator
);

// Skip reauth during tests, only works in non-official builds.
const TEST_ONLY_REAUTH = "toolkit.osKeyStore.unofficialBuildOnlyLogin";

export var OSKeyStore = {
  /**
   * On macOS this becomes part of the name label visible on Keychain Acesss as
   * "Firefox Encrypted Storage" (where "Firefox" is the MOZ_APP_BASENAME).
   * Unfortunately, since this is the index into the keystore, we can't
   * localize it without some really unfortunate side effects, like users
   * losing access to stored information when they change their locale.
   * This is a limitation of the interface exposed by macOS. Notably, both
   * Chrome and Safari suffer the same shortcoming.
   */
  STORE_LABEL: AppConstants.MOZ_APP_BASENAME + " Encrypted Storage",

  /**
   * Consider the module is initialized as locked. OS might unlock without a
   * prompt.
   *
   * @type {boolean}
   */
  _isLocked: true,

  _pendingUnlockPromise: null,

  /**
   * @returns {boolean} True if logged in (i.e. decrypt(reauth = false) will
   *                    not retrigger a dialog) and false if not.
   *                    User might log out elsewhere in the OS, so even if this
   *                    is true a prompt might still pop up.
   */
  get isLoggedIn() {
    return !this._isLocked;
  },

  /**
   * @returns {boolean} True if there is another login dialog existing and false
   *                    otherwise.
   */
  get isUIBusy() {
    return !!this._pendingUnlockPromise;
  },

  canReauth() {
    // We have no support on linux (bug 1527745)
    if (AppConstants.platform == "win" || AppConstants.platform == "macosx") {
      lazy.log.debug(
        "canReauth, returning true, this._testReauth:",
        this._testReauth
      );
      return true;
    }
    lazy.log.debug("canReauth, returning false");
    return false;
  },

  /**
   * If the test pref exists, this method will dispatch a observer message and
   * resolves to simulate successful reauth, or rejects to simulate failed reauth.
   *
   * @returns {Promise<undefined>} Resolves when sucessful login, rejects when
   *                               login fails.
   */
  async _reauthInTests() {
    // Skip this reauth because there is no way to mock the
    // native dialog in the testing environment, for now.
    lazy.log.debug("_reauthInTests: _testReauth: ", this._testReauth);
    switch (this._testReauth) {
      case "pass":
        Services.obs.notifyObservers(
          null,
          "oskeystore-testonly-reauth",
          "pass"
        );
        return { authenticated: true, auth_details: "success" };
      case "cancel":
        Services.obs.notifyObservers(
          null,
          "oskeystore-testonly-reauth",
          "cancel"
        );
        throw new Components.Exception(
          "Simulating user cancelling login dialog",
          Cr.NS_ERROR_FAILURE
        );
      default:
        throw new Components.Exception(
          "Unknown test pref value",
          Cr.NS_ERROR_FAILURE
        );
    }
  },

  /**
   * Ensure the store in use is logged in. It will display the OS
   * login prompt or do nothing if it's logged in already. If an existing login
   * prompt is already prompted, the result from it will be used instead.
   *
   * Note: This method must set _pendingUnlockPromise before returning the
   * promise (i.e. the first |await|), otherwise we'll risk re-entry.
   * This is why there aren't an |await| in the method. The method is marked as
   * |async| to communicate that it's async.
   *
   * @param   {boolean|string} reauth If set to a string, prompt the reauth login dialog,
   *                                  showing the string on the native OS login dialog.
   *                                  Otherwise `false` will prevent showing the prompt.
   * @param   {string} dialogCaption  The string will be shown on the native OS
   *                                  login dialog as the dialog caption (usually Product Name).
   * @param   {Window?} parentWindow  The window of the caller, used to center the
   *                                  OS prompt in the middle of the application window.
   * @param   {boolean} generateKeyIfNotAvailable Makes key generation optional
   *                                  because it will currently cause more
   *                                  problems for us down the road on macOS since the application
   *                                  that creates the Keychain item is the only one that gets
   *                                  access to the key in the future and right now that key isn't
   *                                  specific to the channel or profile. This means if a user uses
   *                                  both DevEdition and Release on the same OS account (not
   *                                  unreasonable for a webdev.) then when you want to simply
   *                                  re-auth the user for viewing passwords you may also get a
   *                                  KeyChain prompt to allow the app to access the stored key even
   *                                  though that's not at all relevant for the re-auth. We skip the
   *                                  code here so that we can postpone deciding on how we want to
   *                                  handle this problem (multiple channels) until we actually use
   *                                  the key storage. If we start creating keys on macOS by running
   *                                  this code we'll potentially have to do extra work to cleanup
   *                                  the mess later.
   * @returns {Promise<object>}       Object with the following properties:
   *                                    authenticated: {boolean} Set to true if the user successfully authenticated.
   *                                    auth_details: {String?} Details of the authentication result.
   */
  async ensureLoggedIn(
    reauth = false,
    dialogCaption = "",
    parentWindow = null,
    generateKeyIfNotAvailable = true
  ) {
    if (
      (typeof reauth != "boolean" && typeof reauth != "string") ||
      reauth === true ||
      reauth === ""
    ) {
      throw new Error(
        "reauth is required to either be `false` or a non-empty string"
      );
    }

    if (this._pendingUnlockPromise) {
      lazy.log.debug("ensureLoggedIn: Has a pending unlock operation");
      return this._pendingUnlockPromise;
    }
    lazy.log.debug(
      "ensureLoggedIn: Creating new pending unlock promise. reauth: ",
      reauth
    );

    let unlockPromise;
    if (typeof reauth == "string") {
      // Only allow for local builds
      if (
        lazy.UpdateUtils.getUpdateChannel(false) == "default" &&
        this._testReauth
      ) {
        unlockPromise = this._reauthInTests();
      } else if (this.canReauth()) {
        // On Windows, this promise rejects when the user cancels login dialog, see bug 1502121.
        // On macOS this resolves to false, so we would need to check it.
        unlockPromise = lazy.osReauthenticator
          .asyncReauthenticateUser(reauth, dialogCaption, parentWindow)
          .then(reauthResult => {
            let auth_details_extra = {};
            if (reauthResult.length > 3) {
              auth_details_extra.auto_admin = "" + !!reauthResult[2];
              auth_details_extra.require_signon = "" + !!reauthResult[3];
            }
            if (!reauthResult[0]) {
              throw new Components.Exception(
                "User canceled OS reauth entry",
                Cr.NS_ERROR_FAILURE,
                null,
                auth_details_extra
              );
            }
            let result = {
              authenticated: true,
              auth_details: "success",
              auth_details_extra,
            };
            if (reauthResult.length > 1 && reauthResult[1]) {
              result.auth_details += "_no_password";
            }
            return result;
          });
      } else {
        lazy.log.debug(
          "ensureLoggedIn: Skipping reauth on unsupported platforms"
        );
        unlockPromise = Promise.resolve({
          authenticated: true,
          auth_details: "success_unsupported_platform",
        });
      }
    } else {
      unlockPromise = Promise.resolve({ authenticated: true });
    }

    if (generateKeyIfNotAvailable) {
      unlockPromise = unlockPromise.then(async reauthResult => {
        try {
          if (
            !(await lazy.nativeOSKeyStore.asyncSecretAvailable(
              this.STORE_LABEL
            ))
          ) {
            lazy.log.debug(
              "ensureLoggedIn: Secret unavailable, attempt to generate new secret."
            );
            let recoveryPhrase =
              await lazy.nativeOSKeyStore.asyncGenerateSecret(this.STORE_LABEL);
            // TODO We should somehow have a dialog to ask the user to write this down,
            // and another dialog somewhere for the user to restore the secret with it.
            // (Intentionally not printing it out in the console)
            lazy.log.debug(
              "ensureLoggedIn: Secret generated. Recovery phrase length: " +
                recoveryPhrase.length
            );
          }
        } catch (e) {
          lazy.log.debug(
            `ensureLoggedIn: asyncSecretAvailable failed: ${e.result}`
          );
          throw e;
        }
        return reauthResult;
      });
    }

    unlockPromise = unlockPromise.then(
      reauthResult => {
        lazy.log.debug("ensureLoggedIn: Logged in");
        this._pendingUnlockPromise = null;
        this._isLocked = false;

        return reauthResult;
      },
      err => {
        lazy.log.debug("ensureLoggedIn: Not logged in", err);
        this._pendingUnlockPromise = null;
        this._isLocked = true;

        return {
          authenticated: false,
          auth_details: "fail",
          auth_details_extra: err.data?.QueryInterface(Ci.nsISupports)
            .wrappedJSObject,
        };
      }
    );

    this._pendingUnlockPromise = unlockPromise;

    return this._pendingUnlockPromise;
  },

  /**
   * Decrypts cipherText.
   *
   * Note: In the event of an rejection, check the result property of the Exception
   *       object. Handles NS_ERROR_ABORT as user has cancelled the action (e.g.,
   *       don't show that dialog), apart from other errors (e.g., gracefully
   *       recover from that and still shows the dialog.)
   *
   * @param   {string}         cipherText Encrypted string including the algorithm details.
   * @param   {string}         trigger    Caller identifier for telemetry.
   * @param   {boolean|string} reauth     If set to a string, prompt the reauth login dialog.
   *                                      The string may be shown on the native OS
   *                                      login dialog. Empty strings and `true` are disallowed.
   * @returns {Promise<string>}           resolves to the decrypted string, or rejects otherwise.
   */
  async decrypt(cipherText, trigger, reauth = false) {
    let errorResult = 0;
    try {
      if (!(await this.ensureLoggedIn(reauth)).authenticated) {
        lazy.log.warn("User canceled encryption login");
        throw Components.Exception(
          "User canceled OS unlock entry",
          Cr.NS_ERROR_ABORT
        );
      }
      let bytes = await lazy.nativeOSKeyStore.asyncDecryptBytes(
        this.STORE_LABEL,
        cipherText
      );
      return String.fromCharCode.apply(String, bytes);
    } catch (e) {
      errorResult = e.result;
      lazy.log.warn(`Decryption failed with result: ${e.result}`);
      throw e;
    } finally {
      Glean.creditcard.osKeystoreDecrypt.record({
        isDecryptSuccess: errorResult === 0,
        errorResult,
        trigger,
      });
    }
  },

  /**
   * Encrypts a string and returns cipher text containing algorithm information used for decryption.
   *
   * @param   {string} plainText Original string without encryption.
   * @returns {Promise<string>} resolves to the encrypted string (with algorithm), otherwise rejects.
   */
  async encrypt(plainText) {
    if (!(await this.ensureLoggedIn()).authenticated) {
      throw Components.Exception(
        "User canceled OS unlock entry",
        Cr.NS_ERROR_ABORT
      );
    }

    // Convert plain text into a UTF-8 binary string
    plainText = unescape(encodeURIComponent(plainText));

    // Convert it to an array
    let textArr = [];
    for (let char of plainText) {
      textArr.push(char.charCodeAt(0));
    }

    let rawEncryptedText = await lazy.nativeOSKeyStore.asyncEncryptBytes(
      this.STORE_LABEL,
      textArr
    );

    // Mark the output with a version number.
    return rawEncryptedText;
  },

  /**
   * Exports the recovery phrase within the native OSKeyStore if authenticated
   * as a byte string.
   *
   * @returns {Promise<string>}
   */
  async exportRecoveryPhrase() {
    if (!(await this.ensureLoggedIn()).authenticated) {
      throw Components.Exception(
        "User canceled OS unlock entry",
        Cr.NS_ERROR_ABORT
      );
    }

    return await lazy.nativeOSKeyStore.asyncGetRecoveryPhrase(this.STORE_LABEL);
  },

  /**
   * Remove the store. For tests.
   */
  async cleanup() {
    return lazy.nativeOSKeyStore.asyncDeleteSecret(this.STORE_LABEL);
  },
};

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  return console.createInstance({
    maxLogLevelPref: "toolkit.osKeyStore.loglevel",
    logModule: "OSKeyStore",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  OSKeyStore,
  "_testReauth",
  TEST_ONLY_REAUTH,
  ""
);
