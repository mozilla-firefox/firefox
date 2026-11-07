/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * LoginManagerStorage implementation for the Rust logins storage back-end.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",

  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () => new Localization(["toolkit/passwordmgr/passwordmgr.ftl"])
);

import { initialize as initRustComponents } from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustInitRustComponents.sys.mjs";

import {
  LoginEntry,
  LoginMeta,
  LoginEntryWithMeta,
  BulkResultEntry,
  createLoginStoreWithNssKeymanager,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustLogins.sys.mjs";

// AuthenticationCanceled must come from db_crypto, not logins: it is the error
// PrimaryPasswordAuthenticator declares, and the two namespaces export unrelated
// classes under that name.
import {
  PrimaryPasswordAuthenticator,
  AuthenticationCanceled,
} from "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustDbCrypto.sys.mjs";

const LoginInfo = Components.Constructor(
  "@mozilla.org/login-manager/loginInfo;1",
  "nsILoginInfo",
  "init"
);

// Convert a LoginInfo, as known to the JS world, to a LoginEntry, the Rust
// Login type.
// This could be an instance method implemented in
// toolkit/components/passwordmgr/LoginInfo.sys.mjs
// but I'd like to decouple from as many components as possible by now
const loginInfoToLoginEntry = loginInfo =>
  new LoginEntry({
    origin: loginInfo.origin,
    httpRealm: loginInfo.httpRealm,
    formActionOrigin: loginInfo.formActionOrigin,
    usernameField: loginInfo.usernameField,
    passwordField: loginInfo.passwordField,
    username: loginInfo.username,
    password: loginInfo.password,
  });

const MAX_DATE_MS = 8640000000000000;

const validTimestamp = time =>
  time > 0 && time <= MAX_DATE_MS ? time : undefined;

// Convert a LoginInfo to a LoginEntryWithMeta, to be used for migrating
// records between legacy and Rust storage.
const loginInfoToLoginEntryWithMeta = loginInfo => {
  const now = Date.now();
  return new LoginEntryWithMeta({
    entry: loginInfoToLoginEntry(loginInfo),
    meta: new LoginMeta({
      id: loginInfo.guid || Services.uuid.generateUUID().toString(),
      timesUsed: loginInfo.timesUsed || 1,
      timeCreated: validTimestamp(loginInfo.timeCreated) || now,
      timeLastUsed: validTimestamp(loginInfo.timeLastUsed) || now,
      timePasswordChanged: validTimestamp(loginInfo.timePasswordChanged) || now,
      timeLastBreachAlertDismissed:
        validTimestamp(loginInfo.timeLastBreachAlertDismissed) || null,
    }),
  });
};

// Convert a Login instance, as returned from Rust Logins, to a LoginInfo
const loginToLoginInfo = login => {
  const loginInfo = new LoginInfo(
    login.origin,
    login.formActionOrigin,
    login.httpRealm,
    login.username,
    login.password,
    login.usernameField,
    login.passwordField
  );

  // add meta information
  loginInfo.QueryInterface(Ci.nsILoginMetaInfo);
  // Rust Login ids are guids
  loginInfo.guid = login.id;
  loginInfo.timeCreated = login.timeCreated;
  loginInfo.timeLastUsed = login.timeLastUsed;
  loginInfo.timePasswordChanged = login.timePasswordChanged;
  loginInfo.timesUsed = login.timesUsed;
  loginInfo.timeLastBreachAlertDismissed =
    login.timeLastBreachAlertDismissed || null;

  /* These fields are not attributes on the Rust Login class
  loginInfo.syncCounter = login.syncCounter;
  loginInfo.everSynced = login.everSynced;
  loginInfo.unknownFields = login.encryptedUnknownFields;
  */

  return loginInfo;
};

// The fields a LoginCandidate carries, under the storage field names that
// #searchLogins matches on. A search restricted to these fields can be answered
// without the encryption key, i.e. without prompting for the primary password.
const candidateToMatchable = candidate => ({
  hostname: candidate.origin,
  formSubmitURL: candidate.formActionOrigin,
  httpRealm: candidate.httpRealm,
  usernameField: candidate.usernameField,
  passwordField: candidate.passwordField,
  guid: candidate.id,
  timeCreated: candidate.timeCreated,
  timeLastUsed: candidate.timeLastUsed,
  timePasswordChanged: candidate.timePasswordChanged,
  timesUsed: candidate.timesUsed,
});

const CANDIDATE_MATCH_FIELDS = new Set([
  "origin",
  "formActionOrigin",
  "httpRealm",
  "usernameField",
  "passwordField",
  "guid",
  "timeCreated",
  "timeLastUsed",
  "timePasswordChanged",
  "timesUsed",
]);

// The origins and domains for listCandidatesByOrigin() to return every login
// LoginHelper.isOriginMatching() can accept for `origin` with `options`. The
// result may hold more, which matchLogin() then drops.
const originPrefilter = (origin, options) => {
  const origins = [origin];
  if (options.schemeUpgrades && origin.startsWith("https://")) {
    origins.push("http://" + origin.slice("https://".length));
  }
  const domains = [];
  if (options.acceptDifferentSubdomains) {
    try {
      domains.push(Services.eTLD.getBaseDomain(Services.io.newURI(origin)));
    } catch {
      // isOriginMatching() accepts no other subdomain for this origin either.
    }
    if (options.acceptRelatedRealms && options.relatedRealms) {
      domains.push(...options.relatedRealms);
    }
  }
  return [origins, domains];
};

// Whether aLoginItem, a LoginInfo or the result of candidateToMatchable(),
// matches matchData under the options of LoginHelper.isOriginMatching().
const matchLogin = (aLoginItem, matchData, aOptions) => {
  for (const field in matchData) {
    const wantedValue = matchData[field];

    // Override the storage field name for some fields due to backwards
    // compatibility with Sync/storage.
    let storageFieldName = field;
    switch (field) {
      case "formActionOrigin": {
        storageFieldName = "formSubmitURL";
        break;
      }
      case "origin": {
        storageFieldName = "hostname";
        break;
      }
    }

    switch (field) {
      case "formActionOrigin":
        if (wantedValue != null) {
          // Historical compatibility requires this special case
          if (
            aLoginItem.formSubmitURL == "" ||
            (wantedValue == "" && Object.keys(matchData).length != 1)
          ) {
            break;
          }
          if (
            !lazy.LoginHelper.isOriginMatching(
              aLoginItem[storageFieldName],
              wantedValue,
              aOptions
            )
          ) {
            return false;
          }
          break;
        }
      // fall through
      case "origin":
        if (wantedValue != null) {
          // needed for formActionOrigin fall through
          if (
            !lazy.LoginHelper.isOriginMatching(
              aLoginItem[storageFieldName],
              wantedValue,
              aOptions
            )
          ) {
            return false;
          }
          break;
        }
      // Normal cases.
      // fall through
      case "httpRealm":
      case "id":
      case "usernameField":
      case "passwordField":
      case "encryptedUsername":
      case "encryptedPassword":
      case "guid":
      case "encType":
      case "timeCreated":
      case "timeLastUsed":
      case "timePasswordChanged":
      case "timesUsed":
      case "syncCounter":
      case "everSynced":
        if (wantedValue == null && aLoginItem[storageFieldName]) {
          return false;
        } else if (aLoginItem[storageFieldName] != wantedValue) {
          return false;
        }
        break;
      // Fail if caller requests an unknown property.
      default:
        throw new Error("Unexpected field: " + field);
    }
  }
  return true;
};

// Build a bare LoginInfo carrying only the given guid. Used to report the ids
// of removed logins in "removeAllLogins" notifications, since the bulk deletion
// APIs only return guids rather than full logins.
const guidToLoginInfo = guid => {
  const loginInfo = new LoginInfo("", "", null, "", "", "", "");
  loginInfo.QueryInterface(Ci.nsILoginMetaInfo);
  loginInfo.guid = guid;
  return loginInfo;
};

// An adapter which talks to the Rust Logins Store via LoginInfo objects
class RustLoginsStoreAdapter {
  #store = null;

  constructor(store) {
    this.#store = store;
  }

  async get(id) {
    const login = await this.#store.get(id);
    return login && loginToLoginInfo(login);
  }

  async list() {
    const logins = await this.#store.list();
    return logins.map(loginToLoginInfo);
  }

  async listCandidates() {
    return this.#store.listCandidates();
  }

  async listCandidatesByOrigin(origins, domains) {
    return this.#store.listCandidatesByOrigin(origins, domains);
  }

  // getMany() returns the rows in whatever order the query gives it, so sort
  // them back into the order of `ids`. Callers hand the logins on in the order
  // they receive them, and both the deduping in LoginManagerParent and the
  // username suggestions in the prompter pick by position among equal
  // candidates.
  async getMany(ids) {
    const order = new Map(ids.map((id, index) => [id, index]));
    const logins = await this.#store.getMany(ids);
    logins.sort((a, b) => order.get(a.id) - order.get(b.id));
    return logins.map(loginToLoginInfo);
  }

  async update(id, loginInfo) {
    const loginEntry = loginInfoToLoginEntry(loginInfo);
    const login = await this.#store.update(id, loginEntry);
    return loginToLoginInfo(login);
  }

  async add(loginInfo) {
    const loginEntry = loginInfoToLoginEntry(loginInfo);
    const login = await this.#store.add(loginEntry);
    return loginToLoginInfo(login);
  }

  async addWithMeta(loginInfo) {
    const loginEntryWithMeta = loginInfoToLoginEntryWithMeta(loginInfo);
    const login = await this.#store.addWithMeta(loginEntryWithMeta);
    return loginToLoginInfo(login);
  }

  async addManyWithMeta(loginInfos, continueOnDuplicates) {
    const loginEntriesWithMeta = loginInfos.map(loginInfoToLoginEntryWithMeta);
    const results = await this.#store.addManyWithMeta(loginEntriesWithMeta);

    // on continuous mode, return result objects, which could be either a login
    // or an error containing the error message
    if (continueOnDuplicates) {
      return results.map(l => {
        if (l instanceof BulkResultEntry.Error) {
          return {
            error: { message: l.message },
          };
        }
        return {
          login: loginToLoginInfo(l.login),
        };
      });
    }

    // otherwise throw first error
    const error = results.find(l => l instanceof BulkResultEntry.Error);
    if (error) {
      throw new Error(error.message);
    }
    // and return login info objects
    return results
      .filter(l => l instanceof BulkResultEntry.Success)
      .map(({ login }) => loginToLoginInfo(login));
  }

  async delete(id) {
    await this.#store.delete(id);
  }

  async deleteAll() {
    return await this.#store.deleteAll();
  }

  async deleteAllExceptFxa() {
    return await this.#store.deleteAllExceptFxa();
  }

  // reset() {
  //   return this.#store.reset()
  // }

  async wipeLocal() {
    return await this.#store.wipeLocal();
  }

  async wipeLocalExceptFxa() {
    return await this.#store.wipeLocalExceptFxa();
  }

  async count() {
    return await this.#store.count();
  }

  async touch(id) {
    return await this.#store.touch(id);
  }

  async findLoginToUpdate(loginInfo) {
    const loginEntry = loginInfoToLoginEntry(loginInfo);
    const login = await this.#store.findLoginToUpdate(loginEntry);
    return login && loginToLoginInfo(login);
  }

  async recordPotentiallyVulnerablePasswords(passwords) {
    await this.#store.recordPotentiallyVulnerablePasswords(passwords);
  }

  async isPotentiallyVulnerablePassword(id) {
    return this.#store.isPotentiallyVulnerablePassword(id);
  }

  async recordBreachAlertDismissal(id) {
    await this.#store.recordBreachAlertDismissal(id);
  }

  async clearAllPotentiallyVulnerablePasswords() {
    await this.#store.resetAllBreaches();
  }

  async arePotentiallyVulnerablePasswords(ids) {
    return this.#store.arePotentiallyVulnerablePasswords(ids);
  }

  async getBreachAlertDismissalsByLoginGUID() {
    const result = {};
    for (const {
      id,
      timeLastBreachAlertDismissed: timeBreachAlertDismissed,
    } of await this.#store.list()) {
      if (timeBreachAlertDismissed) {
        result[id] = {
          timeBreachAlertDismissed,
        };
      }
    }
    return result;
  }

  bridgedEngine() {
    return this.#store.bridgedEngine();
  }

  shutdown() {
    return this.#store.shutdown();
  }
}

class RustLoginStorageAuthenticator extends PrimaryPasswordAuthenticator {
  #logger = null;

  // True while a primary-password prompt is displayed, so the storage can
  // report `uiBusy` like the SDR crypto does.
  uiBusy = false;

  // Set when the user cancels the prompt. The storage uses this to translate
  // the resulting store failure into NS_ERROR_ABORT (matching crypto-SDR).
  authCanceled = false;

  constructor() {
    super();
    this.#logger = lazy.LoginHelper.createLogger(
      "RustLoginStorageAuthenticator"
    );
  }

  // Called by Rust when the NSS key needs to be unlocked. Concurrent calls are not
  // possible: all store operations hold the store's internal Mutex<LoginDb> while
  // calling get_key(), so this method is always invoked serially.
  async getPrimaryPassword() {
    this.#logger.log("getPrimaryPassword called");
    // Removed by bug 2067167. Declining is a workaround for requestReauth()
    // locking the token out from under in-flight store operations; once re-auth
    // stops relocking the token there is nothing left to decline.
    if (lazy.LoginHelper.primaryPasswordReauthInProgress) {
      this.#logger.log("declining, a re-authentication is already in progress");
      this.authCanceled = true;
      throw new AuthenticationCanceled("Re-authentication already in progress");
    }
    this.authCanceled = false;
    this.uiBusy = true;
    try {
      const win = Services.wm.getMostRecentBrowserWindow();
      // Empty title causes Prompter.sys.mjs to fall back to the localised
      // "PromptPassword3" string ("Password Required - <AppName>").
      const message = await lazy.l10n.formatValue(
        "primary-password-prompt-message"
      );
      const result = await Services.prompt.asyncPromptPassword(
        win?.browsingContext,
        Services.prompt.MODAL_TYPE_WINDOW,
        "",
        message,
        ""
      );

      if (!result.getProperty("ok")) {
        this.authCanceled = true;
        this.#recordPrompt("cancel");
        Services.obs.notifyObservers(null, "passwordmgr-crypto-loginCanceled");
        throw new AuthenticationCanceled("User cancelled");
      }

      this.#logger.log("got a password");
      return result.getProperty("pass");
    } catch (e) {
      if (!(e instanceof AuthenticationCanceled)) {
        this.#recordPrompt("error");
      }
      throw e;
    } finally {
      this.uiBusy = false;
    }
  }

  async onAuthenticationSuccess() {
    this.#recordPrompt("success");
    Services.obs.notifyObservers(null, "passwordmgr-crypto-login");
    this.#logger.log("authenticated with success");
  }

  // Rust prompts again after a failure, so every attempt gets its own event.
  async onAuthenticationFailure() {
    this.#recordPrompt("wrong_password");
    this.#logger.log("failed to authenticate");
  }

  #recordPrompt(result) {
    Glean.pwmgr.primaryPasswordPrompt.record({
      source: "rust_storage",
      result,
    });
  }
}

export class LoginManagerRustStorage {
  #storageAdapter = null;
  #authenticator = null;
  #initializationPromise = null;
  #initTimings = null;
  // Only the active backend fires storage-changed events to avoid duplicates
  // when both JSON and Rust stores are initialized.
  // Default is false (json is active)
  #isActive = false;
  set isActive(v) {
    this.#isActive = v;
  }

  get backendName() {
    return "rust";
  }

  get initTimings() {
    return this.#initTimings;
  }

  // have it a singleton
  constructor() {
    if (LoginManagerRustStorage._instance) {
      return LoginManagerRustStorage._instance;
    }
    this.__crypto = null; // nsILoginManagerCrypto service
    LoginManagerRustStorage._instance = this;
  }

  initialize() {
    if (this.#initializationPromise) {
      this.log("rust storage already initialized");
    } else {
      this.#initializationPromise = this.#doInitialize();
    }

    return this.#initializationPromise;
  }

  async #doInitialize() {
    try {
      // If we are already in the “Profile-Change-Teardown” phase or have
      // already completed it, “false” is returned and initialization is
      // aborted.
      if (!(await this._registerShutdownBlocker())) {
        throw new Error(
          "Shutdown is past profile-change-teardown, not opening the store"
        );
      }

      const profilePath = Services.dirsvc.get("ProfD", Ci.nsIFile).path;
      const path = `${profilePath}/logins.db`;
      this.log(`Initializing Rust login storage at ${path}`);

      const startedAt = ChromeUtils.now();
      await initRustComponents(profilePath);
      const componentsReadyAt = ChromeUtils.now();

      this.#authenticator = new RustLoginStorageAuthenticator();
      const store = await createLoginStoreWithNssKeymanager(
        path,
        this.#authenticator
      );
      this.#initTimings = {
        initRustComponentsMs: Math.round(componentsReadyAt - startedAt),
        createRustStoreMs: Math.round(ChromeUtils.now() - componentsReadyAt),
      };
      this.#storageAdapter = new RustLoginsStoreAdapter(store);

      this.log("Rust login storage ready.");
      return this;
    } catch (e) {
      this.log(`Initialization failed: ${e}`);
      // Failing after the store was created leaves it holding the only
      // reference to the authenticator, and UniFFI asserts at xpcom-shutdown
      // that no callback object is still registered. Let it go, and report the
      // original failure rather than anything shutdown runs into.
      try {
        await this.#storageAdapter?.shutdown();
      } catch (shutdownError) {
        this.log(`Shutdown after failed initialization: ${shutdownError}`);
      }
      this.#storageAdapter = null;
      this.#authenticator = null;
      throw e;
    }
  }

  /**
   * Terminate all pending writes. After this call, the store can't be used.
   */
  async finalize() {
    // TODO: Currently we do not mark the instance as closed, not sure if later
    // calls would be rejected elsewhere.

    // Null when initialization failed, which shuts the store down itself.
    await this.#storageAdapter?.shutdown();
  }

  /**
   * Ensure the storage is finalized at shutdown. All LoginManager storage
   * backends must have their own shutdown blocker to finalize properly.
   *
   * Called before the store is opened, so the blocker waits for
   * initialization to settle before finalizing: the store holds the only
   * reference to the authenticator, and UniFFI asserts at `xpcom-shutdown`
   * that no callback object is still registered.
   *
   * In the corner case where the shutdown phase has already passed by the time
   * we get here, registering a blocker would throw, so we call `finalize()`
   * immediately instead and report that nothing is in place to close a store.
   *
   * @param {object} phase An `AsyncShutdown` phase object. Exposed as a
   *   parameter for testing.
   * @returns {Promise<boolean>} Whether a blocker was registered.
   */
  async _registerShutdownBlocker(
    phase = lazy.AsyncShutdown.profileChangeTeardown
  ) {
    if (phase.isClosed) {
      await this.finalize();
      return false;
    }
    phase.addBlocker(
      "LoginManagerRustStorage: Interrupt IO operations on login store",
      async () => {
        await this.initialize().catch(() => {});
        await this.finalize();
      }
    );
    return true;
  }

  /**
   * Internal method used by tests only. It is called before replacing
   * this storage module with a new instance.
   */
  testSaveForReplace() {}

  /**
   * Returns the Sync bridged engine backed by the same Rust store instance the
   * storage layer uses, so that Sync and data access share one database handle.
   * Assumes the storage is already initialized; the sync engine only reaches it
   * via the active store, which is set after initialization completes.
   *
   */
  bridgedEngine() {
    return this.#storageAdapter.bridgedEngine();
  }

  /**
   * Returns the "sync id" used by Sync to know whether the store is current with
   * respect to the sync servers. It is stored encrypted, but only so we
   * can detect failure to decrypt (for example, a "reset" of the primary
   * password will leave all logins alone, but they will fail to decrypt. We
   * also want this metadata to be unavailable in that scenario)
   *
   * Returns null if the data doesn't exist or if the data can't be
   * decrypted (including if the primary-password prompt is cancelled). This is
   * OK for Sync as it can't even begin syncing if the primary-password is
   * locked as the sync encrytion keys are stored in this login manager.
   */
  async getSyncID() {
    throw Components.Exception("getSyncID", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async setSyncID(_syncID) {
    throw Components.Exception("setSyncID", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async getLastSync() {
    throw Components.Exception("getLastSync", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  async setLastSync(_timestamp) {
    throw Components.Exception("setLastSync", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  loginIsDeleted(_guid) {
    throw Components.Exception("loginIsDeleted", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  loginIsDeletedAsync(_guid) {
    throw Components.Exception(
      "loginIsDeletedAsync",
      Cr.NS_ERROR_NOT_IMPLEMENTED
    );
  }

  async addLoginsAsync(logins, continueOnDuplicates = false) {
    let result;
    try {
      result = await this.#addLoginsWithResults(logins, continueOnDuplicates);
    } catch (e) {
      throw this.#translateLockedError(e);
    }

    // Return only successfully added logins, matching the JSON backend.
    return continueOnDuplicates
      ? result.filter(item => !item.error).map(item => item.login)
      : result;
  }

  /**
   * Adds multiple logins and returns the raw per-login results, with each
   * entry being either { login } or { error } and aligned by index with the
   * input. Used by the migrator to detect and rescue duplicates; most callers
   * want addLoginsAsync instead.
   */
  async addLoginsWithResultsAsync(logins) {
    return this.#addLoginsWithResults(logins, true);
  }

  async #addLoginsWithResults(logins, continueOnDuplicates) {
    if (logins.length === 0) {
      return logins;
    }

    // The Rust backend only treats an exact formActionOrigin match as a
    // duplicate, whereas desktop treats an empty formActionOrigin as a wildcard
    // (see LoginHelper.doLoginsMatch). Reproduce the JSON backend's check up
    // front so a manually-added login (formActionOrigin === "") is rejected as
    // a duplicate, and so callers such as about:logins get the existing login's
    // GUID to link to and select the conflicting login.
    if (!continueOnDuplicates) {
      const existing = await this.#findExistingDuplicate(logins);
      if (existing) {
        throw lazy.LoginHelper.createLoginAlreadyExistsError(existing.guid);
      }
    }

    const result = await this.#storageAdapter.addManyWithMeta(
      logins,
      continueOnDuplicates
    );

    if (this.#isActive) {
      Glean.pwmgr.numSavedPasswords.set(
        await this.countLoginsAsync("", "", "")
      );
      for (const item of result) {
        const login = continueOnDuplicates ? item.login : item;
        if (login) {
          lazy.LoginHelper.notifyStorageChanged("addLogin", login);
        }
      }
    }

    return result;
  }

  // Finds the stored login that a to-be-added login duplicates, so the existing
  // login's GUID can be reported in the "already exists" error. Mirrors the
  // duplicate lookup in modifyLoginAsync.
  async #findExistingDuplicate(logins) {
    for (const login of logins) {
      const matchData = {};
      for (const field of ["origin", "formActionOrigin", "httpRealm"]) {
        if (login[field] != "") {
          matchData[field] = login[field];
        }
      }
      const [matches] = await this.#searchLogins(matchData);
      const match = matches.find(l => login.matches(l, true));
      if (match) {
        return match;
      }
    }
    return null;
  }

  /**
   * Resolves a stored login from a login passed by value, so callers may pass
   * a login without a guid. findLoginToUpdate() has dedupe semantics: having
   * already matched on origin, it picks by username alone, ignoring the
   * password, and falls back to an entry with an empty username. Require the
   * credentials to agree too, so a caller never acts on a record it did not
   * describe.
   *
   * This deliberately does not use nsILoginInfo.equals() the way the JSON
   * backend's _getIdForLogin() does: the Rust store normalizes origin and
   * formActionOrigin on write, so a stored login is frequently not
   * field-for-field identical to the one the caller holds.
   */
  async #findLoginWithSameCredentials(login) {
    const storedLogin = await this.#storageAdapter.findLoginToUpdate(login);
    if (
      !storedLogin ||
      storedLogin.username !== login.username ||
      storedLogin.password !== login.password
    ) {
      return null;
    }
    return storedLogin;
  }

  async modifyLoginAsync(oldLogin, newLoginData, _fromSync) {
    try {
      const oldStoredLogin = await this.#findLoginWithSameCredentials(oldLogin);

      if (!oldStoredLogin) {
        throw new Error("No matching logins");
      }

      const idToModify = oldStoredLogin.guid;

      const newLogin = lazy.LoginHelper.buildModifiedLogin(
        oldStoredLogin,
        newLoginData
      );

      // Check if the new GUID is duplicate.
      if (
        newLogin.guid != idToModify &&
        (await this.#storageAdapter.get(newLogin.guid))
      ) {
        throw new Error("specified GUID already exists");
      }

      // Look for an existing entry in case key properties changed.
      if (!newLogin.matches(oldLogin, true)) {
        const matchData = {};
        for (const field of ["origin", "formActionOrigin", "httpRealm"]) {
          if (newLogin[field] != "") {
            matchData[field] = newLogin[field];
          }
        }
        const [logins] = await this.#searchLogins(matchData);

        const matchingLogin = logins.find(login =>
          newLogin.matches(login, true)
        );
        if (matchingLogin) {
          throw lazy.LoginHelper.createLoginAlreadyExistsError(
            matchingLogin.guid
          );
        }
      }

      const updatedLogin = await this.#storageAdapter.update(
        idToModify,
        newLogin
      );

      // The Rust `update` deliberately treats an edit as "not a use" and leaves
      // timesUsed/timeLastUsed unchanged (bug 2045032). Honor a requested
      // increment (e.g. timesUsedIncrement) by recording the use(s) via `touch`.
      let finalLogin = updatedLogin;
      const increment = newLogin.timesUsed - oldStoredLogin.timesUsed;
      for (let i = 0; i < increment; i++) {
        await this.#storageAdapter.touch(updatedLogin.guid);
      }
      if (increment > 0) {
        finalLogin = await this.#storageAdapter.get(updatedLogin.guid);
      }

      if (this.#isActive) {
        lazy.LoginHelper.notifyStorageChanged("modifyLogin", [
          oldStoredLogin,
          finalLogin,
        ]);
      }

      return finalLogin;
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  async recordPasswordUseAsync(login) {
    try {
      const oldStoredLogin = await this.#findLoginWithSameCredentials(login);

      if (!oldStoredLogin) {
        throw new Error("No matching logins");
      }

      await this.#storageAdapter.touch(oldStoredLogin.guid);
      const updatedLogin = await this.#storageAdapter.get(oldStoredLogin.guid);

      if (this.#isActive) {
        lazy.LoginHelper.notifyStorageChanged("modifyLogin", [
          oldStoredLogin,
          updatedLogin,
        ]);
      }
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  async recordBreachAlertDismissal(loginGUID) {
    await this.#storageAdapter.recordBreachAlertDismissal(loginGUID);
  }

  async getBreachAlertDismissalsByLoginGUID() {
    try {
      return await this.#storageAdapter.getBreachAlertDismissalsByLoginGUID();
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  /**
   * Returns an array of nsILoginInfo. If decryption of a login
   * fails due to a corrupt entry, the login is not included in
   * the resulting array.
   *
   * @returns {Promise<nsILoginInfo[]>}
   */
  async getAllLogins(includeDeleted) {
    // `includeDeleted` is currentlty unsupported
    if (includeDeleted) {
      throw Components.Exception(
        "getAllLogins with includeDeleted",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }
    try {
      return await this.#storageAdapter.list();
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  /**
   * Store operations fail to decrypt whenever the primary password is not
   * available: the user cancelled the prompt, or the authenticator declined to
   * prompt because NSS already has one on screen. Callers expect
   * NS_ERROR_ABORT for that, the way crypto-SDR reports it, rather than a raw
   * Rust error.
   *
   * @param {Error} e The error the store threw.
   * @returns {Error} NS_ERROR_ABORT if the primary password was unavailable,
   *   otherwise `e` unchanged, for the caller to rethrow.
   */
  #translateLockedError(e) {
    if (
      !this.#authenticator?.authCanceled &&
      !/decryption failed/i.test(e.message)
    ) {
      return e;
    }
    if (this.#authenticator) {
      this.#authenticator.authCanceled = false;
    }
    return Components.Exception("Primary password locked", Cr.NS_ERROR_ABORT);
  }

  async searchLoginsAsync(matchData, includeDeleted) {
    const realMatchData = {};
    const options = {};

    if ("guid" in matchData) {
      realMatchData.guid = matchData.guid;
    } else {
      for (const name in matchData) {
        switch (name) {
          // Some property names aren't field names but are special options to
          // affect the search.
          case "acceptDifferentSubdomains":
          case "schemeUpgrades":
          case "acceptRelatedRealms":
          case "relatedRealms": {
            options[name] = matchData[name];
            break;
          }
          default: {
            realMatchData[name] = matchData[name];
            break;
          }
        }
      }
    }

    try {
      const [logins] = await this.#searchLogins(
        realMatchData,
        includeDeleted,
        options
      );
      return logins;
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  /**
   * The ids of the logins matching matchData, which may only name
   * CANDIDATE_MATCH_FIELDS. Never needs the encryption key.
   */
  async #findCandidateIds(matchData, aOptions) {
    const candidates =
      typeof matchData.origin == "string" && matchData.origin
        ? await this.#storageAdapter.listCandidatesByOrigin(
            ...originPrefilter(matchData.origin, aOptions)
          )
        : await this.#storageAdapter.listCandidates();
    return candidates
      .filter(candidate =>
        matchLogin(candidateToMatchable(candidate), matchData, aOptions)
      )
      .map(candidate => candidate.id);
  }

  async #searchLogins(
    matchData,
    includeDeleted = false,
    aOptions = {
      schemeUpgrades: false,
      acceptDifferentSubdomains: false,
      acceptRelatedRealms: false,
      relatedRealms: [],
    }
  ) {
    const fields = Object.keys(matchData);
    let candidateLogins;
    if (
      fields.length == 1 &&
      fields[0] == "guid" &&
      typeof matchData.guid == "string"
    ) {
      // searchLoginsAsync() drops every other field when given a guid, so a
      // guid search arrives here alone. Other callers fall through, so that
      // their other fields are matched before anything is decrypted.
      // matchLogin() keeps a login on guid only on equality, and guid is unique.
      candidateLogins = await this.#storageAdapter.getMany([matchData.guid]);
    } else if (fields.every(field => CANDIDATE_MATCH_FIELDS.has(field))) {
      // Matching the cleartext fields first means that a search without a hit
      // never needs the encryption key, and so never prompts for the primary
      // password. Only the logins that are actually returned get decrypted.
      const ids = await this.#findCandidateIds(matchData, aOptions);
      candidateLogins = ids.length
        ? await this.#storageAdapter.getMany(ids)
        : [];
    } else {
      // Matching on a field the candidates don't carry needs everything.
      candidateLogins = await this.#storageAdapter.list();
    }

    const foundLogins = [];
    const foundIds = [];

    for (const login of candidateLogins) {
      if (login.deleted && !includeDeleted) {
        continue; // skip deleted items
      }

      if (matchLogin(login, matchData, aOptions)) {
        foundLogins.push(login);
        foundIds.push(login.guid);
      }
    }

    this.log(
      `Returning ${foundLogins.length} logins for specified origin with options ${aOptions}`
    );
    return [foundLogins, foundIds];
  }

  async removeLoginAsync(login, _fromSync) {
    const storedLogin = await this.#findLoginWithSameCredentials(login);
    if (!storedLogin) {
      throw new Error("No matching logins");
    }

    await this.#storageAdapter.delete(storedLogin.guid);

    if (this.#isActive) {
      Glean.pwmgr.numSavedPasswords.set(
        await this.countLoginsAsync("", "", "")
      );
      lazy.LoginHelper.notifyStorageChanged("removeLogin", storedLogin);
    }
  }

  /**
   * Removes all logins from local storage, including the FxA Sync key. The
   * logins are marked as deleted so the deletions propagate to Sync.
   *
   * NOTE: You probably want removeAllUserFacingLoginsAsync instead of this
   * function.
   */
  async removeAllLoginsAsync() {
    this.log("Removing all logins.");
    const removedIds = await this.#storageAdapter.deleteAll();

    if (this.#isActive) {
      lazy.LoginHelper.notifyStorageChanged(
        "removeAllLogins",
        removedIds.map(guidToLoginInfo)
      );
    }
  }

  /**
   * Removes all user facing logins from storage, i.e. all logins except the
   * FxA Sync key.
   *
   * If you need to remove the FxA key, use `removeAllLoginsAsync` instead.
   *
   * @param fullyRemove remove the logins rather than mark them deleted.
   */
  async removeAllUserFacingLoginsAsync(fullyRemove) {
    this.log("Removing all user facing logins.");
    let removedIds = [];
    if (fullyRemove) {
      // wipeLocalExceptFxa() does not return the removed ids.
      await this.#storageAdapter.wipeLocalExceptFxa();
    } else {
      removedIds = await this.#storageAdapter.deleteAllExceptFxa();
    }

    if (this.#isActive) {
      lazy.LoginHelper.notifyStorageChanged(
        "removeAllLogins",
        removedIds.map(guidToLoginInfo)
      );
    }
  }

  async countLoginsAsync(origin, formActionOrigin, httpRealm) {
    // The optimized adapter path only applies when all fields are the empty
    // string wildcard. Origin and formActionOrigin must go through the generic
    // search below so that count and search share the same strict matching
    // semantics; the native countByOrigin/countByFormActionOrigin paths
    // normalize the origin (e.g. stripping a trailing slash) and would count
    // logins that searchLoginsAsync does not return.
    if (origin === "" && formActionOrigin === "" && httpRealm === "") {
      return await this.#storageAdapter.count();
    }

    const loginData = {
      origin,
      formActionOrigin,
      httpRealm,
    };

    const matchData = {};
    for (const field of ["origin", "formActionOrigin", "httpRealm"]) {
      if (loginData[field] != "") {
        matchData[field] = loginData[field];
      }
    }
    // Counting the matching candidates, rather than the logins #searchLogins
    // returns, spares decrypting them, and so prompting for the primary
    // password.
    const ids = await this.#findCandidateIds(matchData, {});

    this.log(`Counted ${ids.length} logins.`);
    return ids.length;
  }

  async addPotentiallyVulnerablePassword(login) {
    await this.#storageAdapter.recordPotentiallyVulnerablePasswords([
      login.password,
    ]);
    if (this.#isActive) {
      lazy.LoginHelper.notifyStorageChanged(
        "addPotentiallyVulnerablePassword",
        login
      );
    }
  }

  // adding multiple potentially vulnerable passwords during migration
  async addPotentiallyVulnerablePasswords(passwords) {
    await this.#storageAdapter.recordPotentiallyVulnerablePasswords(passwords);
  }

  async isPotentiallyVulnerablePassword(login) {
    try {
      return await this.#storageAdapter.isPotentiallyVulnerablePassword(
        login.QueryInterface(Ci.nsILoginMetaInfo).guid
      );
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  async arePotentiallyVulnerablePasswords(logins) {
    // Logins without a guid aren't stored, so they can't be recorded as
    // vulnerable. Filter them out to avoid passing null guids to the Rust API.
    const ids = logins
      .map(
        l =>
          (typeof l.QueryInterface === "function"
            ? l.QueryInterface(Ci.nsILoginMetaInfo)
            : l
          ).guid
      )
      .filter(Boolean);
    try {
      return await this.#storageAdapter.arePotentiallyVulnerablePasswords(ids);
    } catch (e) {
      throw this.#translateLockedError(e);
    }
  }

  async clearAllPotentiallyVulnerablePasswords() {
    await this.#storageAdapter.clearAllPotentiallyVulnerablePasswords();
    if (this.#isActive) {
      lazy.LoginHelper.notifyStorageChanged(
        "clearAllPotentiallyVulnerablePasswords"
      );
    }
  }

  get _crypto() {
    if (!this.__crypto) {
      this.__crypto = Cc["@mozilla.org/login-manager/crypto/SDR;1"].getService(
        Ci.nsILoginManagerCrypto
      );
    }
    return this.__crypto;
  }

  get uiBusy() {
    // The Rust store shows its own primary-password prompt via the
    // authenticator, which the SDR crypto is unaware of.
    return this.#authenticator?.uiBusy || this._crypto.uiBusy;
  }

  get isLoggedIn() {
    return this._crypto.isLoggedIn;
  }
}

ChromeUtils.defineLazyGetter(LoginManagerRustStorage.prototype, "log", () => {
  const logger = lazy.LoginHelper.createLogger("RustLogins");
  return logger.log.bind(logger);
});
