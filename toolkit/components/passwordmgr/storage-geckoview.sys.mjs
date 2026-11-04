/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * LoginManagerStorage implementation for GeckoView
 */

import { LoginManagerStorage_json } from "resource://gre/modules/storage-json.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewAutocomplete: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  LoginEntry: "resource://gre/modules/GeckoViewAutocomplete.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

export class LoginManagerStorage extends LoginManagerStorage_json {
  static #storage = null;

  static create() {
    if (!LoginManagerStorage.#storage) {
      LoginManagerStorage.#storage = new LoginManagerStorage();
      LoginManagerStorage.#storage.initialize();
    }

    return LoginManagerStorage.#storage;
  }

  get _crypto() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  get backendName() {
    return "geckoview";
  }

  initialize() {
    try {
      return Promise.resolve();
    } catch (e) {
      this.log("Initialization failed:", e);
      throw new Error("Initialization failed");
    }
  }

  /**
   * Internal method used by tests only. It is called before replacing
   * this storage module with a new instance.
   */
  testSaveForReplace() {}

  async addLoginsAsync(_logins, _continueOnDuplicates = false) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  removeLogin(_login) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async removeLoginAsync(_login) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  modifyLogin(_oldLogin, _newLoginData) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async modifyLoginAsync(_oldLogin, _newLoginData) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  recordPasswordUse(login) {
    lazy.GeckoViewAutocomplete.onLoginPasswordUsed(
      lazy.LoginEntry.fromLoginInfo(login)
    );
  }

  async recordPasswordUseAsync(login) {
    let result = this.recordPasswordUse(login);
    // Emulate being async:
    return Promise.resolve(result);
  }

  /**
   * Returns a promise resolving to an array of all saved logins that can be decrypted.
   *
   * @returns {Promise<nsILoginInfo[]>}
   */
  getAllLogins(includeDeleted) {
    return this._getLoginsAsync({}, includeDeleted);
  }

  async searchLoginsAsync(matchData, includeDeleted) {
    this.log(
      `Searching for matching saved logins for origin: ${matchData.origin}`
    );
    return this._getLoginsAsync(matchData, includeDeleted);
  }

  _baseHostnameFromOrigin(origin) {
    if (!origin) {
      return null;
    }

    let originURI = Services.io.newURI(origin);
    try {
      return Services.eTLD.getBaseDomain(originURI);
    } catch (ex) {
      if (ex.result == Cr.NS_ERROR_HOST_IS_IP_ADDRESS) {
        // `getBaseDomain` cannot handle IP addresses and `nsIURI` cannot return
        // IPv6 hostnames with the square brackets so use `URL.hostname`.
        return new URL(origin).hostname;
      } else if (ex.result == Cr.NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
        return originURI.asciiHost;
      }
      throw ex;
    }
  }

  async _getLoginsAsync(matchData, includeDeleted) {
    let baseHostname = this._baseHostnameFromOrigin(matchData.origin);

    // Query all logins for the eTLD+1 and then filter the logins in _searchLogins
    // so that we can handle the logic for scheme upgrades, subdomains, etc.
    // Convert from the new shape to one which supports the legacy getters used
    // by _searchLogins.
    let candidateLogins = await lazy.GeckoViewAutocomplete.fetchLogins(
      baseHostname
    ).catch(_ => {
      // No GV delegate is attached.
    });

    if (!candidateLogins) {
      // May be undefined if there is no delegate attached to handle the request.
      // Ignore the request.
      return [];
    }

    let realMatchData = {};
    let options = {};

    if (matchData.guid) {
      // Enforce GUID-based filtering when available, since the origin of the
      // login may not match the origin of the form in the case of scheme
      // upgrades.
      realMatchData = { guid: matchData.guid };
    } else {
      for (let [name, value] of Object.entries(matchData)) {
        switch (name) {
          // Some property names aren't field names but are special options to
          // affect the search.
          case "acceptDifferentSubdomains":
          case "schemeUpgrades": {
            options[name] = value;
            break;
          }
          default: {
            realMatchData[name] = value;
            break;
          }
        }
      }
    }

    const [logins] = this._searchLogins(
      realMatchData,
      includeDeleted,
      options,
      candidateLogins.map(this._vanillaLoginToStorageLogin)
    );
    return logins;
  }

  /**
   * Convert a modern decrypted vanilla login object to one expected from logins.json.
   *
   * The storage login is usually encrypted but not in this case, this aligns
   * with the `_decryptLogins` method being a no-op.
   *
   * @param {object} vanillaLogin using `origin`/`formActionOrigin`/`username` properties.
   * @returns {object} a vanilla login for logins.json using
   *                   `hostname`/`formSubmitURL`/`encryptedUsername`.
   */
  _vanillaLoginToStorageLogin(vanillaLogin) {
    return {
      ...vanillaLogin,
      hostname: vanillaLogin.origin,
      formSubmitURL: vanillaLogin.formActionOrigin,
      encryptedUsername: vanillaLogin.username,
      encryptedPassword: vanillaLogin.password,
    };
  }

  /**
   * Use `searchLoginsAsync` instead.
   */
  searchLogins(_matchData) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  /**
   * Use `removeAllLoginsAsync` instead.
   */
  removeAllLogins() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  /**
   * Removes all logins from storage.
   */
  removeAllLoginsAsync() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  countLogins(_origin, _formActionOrigin, _httpRealm) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async countLoginsAsync(_origin, _formActionOrigin, _httpRealm) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  get uiBusy() {
    return false;
  }

  get isLoggedIn() {
    return true;
  }

  /**
   * GeckoView will encrypt the login itself.
   */
  _encryptLogin(login) {
    return login;
  }

  /**
   * GeckoView logins are already decrypted before this component receives them
   * so this method is a no-op for this backend.
   *
   * @see _vanillaLoginToStorageLogin
   */
  _decryptLogins(logins) {
    return logins;
  }

  /**
   * Sync metadata, which isn't supported by GeckoView.
   */
  async getSyncID() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async setSyncID(_syncID) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async getLastSync() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async setLastSync(_timestamp) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }
}

ChromeUtils.defineLazyGetter(LoginManagerStorage.prototype, "log", () => {
  let logger = lazy.LoginHelper.createLogger("LoginStorage");
  return logger.log.bind(logger);
});
