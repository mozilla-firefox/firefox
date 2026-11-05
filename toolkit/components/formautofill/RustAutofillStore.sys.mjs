/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  createAutofillStoreWithNssKeymanager:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs",
  initialize:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustInitRustComponents.sys.mjs",
});

// The prompt is the one passwordmgr shows for the same NSS token, so it uses
// passwordmgr's string.
ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["toolkit/passwordmgr/passwordmgr.ftl"])
);

// The authenticator below extends a class from this module, so it cannot be a
// lazy getter.
import {
  AuthenticationCanceled,
  PrimaryPasswordAuthenticator,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustDbCrypto.sys.mjs";

const logger = console.createInstance({
  logModule: "RustAutofillStore",
  maxLogLevelPref: "extensions.formautofill.loglevel",
});

// File name of the Application Services `autofill` SQLite store within the
// profile directory. It is the sibling of the JSON profile that backs
// addresses/credit cards (autofill-profiles.json).
const AUTOFILL_STORE_FILE_NAME = "autofill.db";

/**
 * Supplies the primary password when NSS needs to unwrap the store's key.
 *
 * Both stores keep their keys in the same key4.db and the token is global, so
 * a prompt either of them shows authenticates both, and two at once
 * authenticate it twice over. The guards below keep that apart.
 *
 * The store decrypts every card it reads, and asks for the key once per card,
 * so being asked is not a sign that anyone wants a number. Only an operation
 * run through allowingPrompt() is shown a prompt; anything else is declined,
 * and reads its cards back with no number, as it would while the token is
 * locked.
 */
export class RustAutofillStorageAuthenticator extends PrimaryPasswordAuthenticator {
  // How many operations allowingPrompt() is running.
  #allowed = 0;

  // Set by a declined prompt and cleared once no allowed operation is left, so
  // that one decline answers for every card the operation goes on to read.
  #declined = false;

  /**
   * Run `operation` with prompting allowed.
   *
   * @param {Function} operation An async function.
   * @returns {Promise<*>} What `operation` resolves to.
   */
  async allowingPrompt(operation) {
    this.#allowed++;
    try {
      return await operation();
    } finally {
      if (--this.#allowed == 0) {
        this.#declined = false;
      }
    }
  }

  // Called by Rust when the key needs unwrapping, with the store's lock held.
  async getPrimaryPassword() {
    if (!this.#allowed) {
      throw new AuthenticationCanceled("Nothing asked for a number");
    }
    if (this.#declined) {
      throw new AuthenticationCanceled("Already declined");
    }
    // LoginHelper.requestReauth logs the token out and re-authenticates it to
    // prove the user is present. Prompting on top of that would answer its
    // prompt for it.
    if (lazy.LoginHelper.primaryPasswordReauthInProgress) {
      throw new AuthenticationCanceled("Re-authentication already in progress");
    }
    const password = await this._prompt();
    if (password === null) {
      // The token is what was declined, not this store, and passwordmgr has
      // callers waiting on the pair of these to know how the prompt ended.
      Services.obs.notifyObservers(null, "passwordmgr-crypto-loginCanceled");
      this.#declined = true;
      throw new AuthenticationCanceled("User cancelled");
    }
    return password;
  }

  /**
   * Show the primary password dialog. Underscore-named so a test can answer it.
   *
   * @returns {Promise<?string>} The password entered, or null if the user
   *   declined.
   */
  async _prompt() {
    const win = Services.wm.getMostRecentBrowserWindow();
    // An empty title has Prompter fall back to "Password Required".
    const result = await Services.prompt.asyncPromptPassword(
      win?.browsingContext,
      Services.prompt.MODAL_TYPE_WINDOW,
      "",
      await lazy.l10n.formatValue("primary-password-prompt-message"),
      ""
    );
    return result.getProperty("ok") ? result.getProperty("pass") : null;
  }

  // Unlocking the token makes every key in it readable, logins' included.
  async onAuthenticationSuccess() {
    Services.obs.notifyObservers(null, "passwordmgr-crypto-login");
  }

  // Rust prompts again after a wrong password, with the same message.
  async onAuthenticationFailure() {
    logger.debug("The primary password was not accepted, prompting again");
  }
}

/**
 * Owns the lifecycle of the Application Services `autofill` SQLite store.
 *
 * There is one store per profile -- addresses, credit cards and passports are
 * collections within it -- so this is a singleton and the store itself is
 * opened lazily on first use. On first open it registers a shutdown blocker
 * that closes the store's rusqlite connection; otherwise the connection is
 * only closed when its JS wrapper is garbage-collected, which happens too late
 * in shutdown.
 */
export class RustAutofillStore {
  static _instance = null;

  _storePromise = null;

  #authenticator = new RustAutofillStorageAuthenticator();

  // A second handler would open a second connection to the same file and
  // register a second shutdown blocker, so constructing always hands back the
  // shared instance.
  constructor() {
    if (RustAutofillStore._instance) {
      return RustAutofillStore._instance;
    }
    RustAutofillStore._instance = this;
  }

  /**
   * Lazily open the store, registering the shutdown blocker on first open.
   *
   * @returns {Promise<Store>}
   */
  async ensureOpen() {
    if (!this._storePromise) {
      // Hold the promise rather than the opened store, so that callers racing
      // to open share one connection instead of each starting their own.
      this._storePromise = (async () => {
        // The store's key lives in NSS, which has to know the profile first.
        await lazy.initialize(PathUtils.profileDir);
        return lazy.createAutofillStoreWithNssKeymanager(
          PathUtils.join(PathUtils.profileDir, AUTOFILL_STORE_FILE_NAME),
          this.#authenticator
        );
      })();
      await this._registerShutdownBlocker();
    }
    return this._storePromise;
  }

  /**
   * Run `operation` with the primary password prompt allowed, for an operation
   * that needs a card's number: reading one to fill or compare, or writing one.
   * Outside of this the store's reads never prompt. See
   * RustAutofillStorageAuthenticator.
   *
   * @param {Function} operation An async function.
   * @returns {Promise<*>} What `operation` resolves to.
   */
  allowingPrompt(operation) {
    return this.#authenticator.allowingPrompt(operation);
  }

  /**
   * Whether NSS is withholding the store's key, which is what makes every
   * crypto operation fail at once.
   *
   * The store cannot say why it could not use its key: a write fails with the
   * same error for a key it cannot get as for data it cannot encrypt, and a
   * read hands a card back with no number either way. Callers owe
   * NS_ERROR_ABORT for the first -- what crypto-SDR and the JSON store report,
   * and what tells a caller to try again later rather than treat one record as
   * lost. Asked of the token rather than recorded when a prompt is declined: a
   * read that was not allowed to prompt fails on a locked token just the same,
   * and the state the token is in afterwards is what they all failed on.
   *
   * @returns {boolean}
   */
  static get primaryPasswordLocked() {
    const token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
      Ci.nsIPKCS11Token
    );
    return token.hasPassword && !token.isLoggedIn;
  }

  /**
   * Close the store's database connection. After this, store operations reject
   * with a DatabaseClosed error. A no-op if the store was never opened, and
   * tolerant of a store that failed to open.
   */
  async finalize() {
    if (!this._storePromise) {
      return;
    }
    let store;
    try {
      store = await this._storePromise;
    } catch {
      // The store never opened, so there is no connection to close.
      return;
    }
    await store.shutdown();
  }

  /**
   * Register a profile-teardown blocker that closes the store. If the teardown
   * phase has already passed by the time the store is opened, registering a
   * blocker would throw, so close it as soon as it is open instead.
   *
   * @param {object} phase An `AsyncShutdown` phase object. Exposed as a
   *   parameter for testing.
   */
  async _registerShutdownBlocker(
    phase = lazy.AsyncShutdown.profileChangeTeardown
  ) {
    if (phase.isClosed) {
      await this.finalize();
      return;
    }
    phase.addBlocker(
      "RustAutofillStore: Close the Application Services autofill store",
      () => this.finalize()
    );
  }
}
