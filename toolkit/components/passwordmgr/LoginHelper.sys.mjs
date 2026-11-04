/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Contains functions shared by different Login Manager components.
 *
 * This JavaScript module exists in order to share code between the different
 * XPCOM components that constitute the Login Manager, including implementations
 * of nsILoginManager and nsILoginManagerStorage.
 */

import { Logic } from "resource://gre/modules/LoginManager.shared.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  NewPasswordModel: "resource://gre/modules/shared/NewPasswordModel.sys.mjs",
});

export class ParentAutocompleteOption {
  image;
  label;
  secondary;
  fillMessageName;
  fillMessageData;

  constructor(image, label, secondary, fillMessageName, fillMessageData) {
    this.image = image;
    this.label = label;
    this.secondary = secondary;
    this.fillMessageName = fillMessageName;
    this.fillMessageData = fillMessageData;
  }
}

/**
 * A helper class to deal with CSV import rows.
 */
class ImportRowProcessor {
  uniqueLoginIdentifiers = new Set();
  originToRows = new Map();
  summary = [];
  mandatoryFields = ["origin", "password"];

  /**
   * Validates if the login data contains a GUID that was already found in a previous row in the current import.
   * If this is the case, the summary will be updated with an error.
   *
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   * @returns {boolean} True if there is an error, false otherwise.
   */
  checkNonUniqueGuidError(loginData) {
    if (loginData.guid) {
      if (this.uniqueLoginIdentifiers.has(loginData.guid)) {
        this.addLoginToSummary({ ...loginData }, "error");
        return true;
      }
      this.uniqueLoginIdentifiers.add(loginData.guid);
    }
    return false;
  }

  /**
   * Validates if the login data contains invalid fields that are mandatory like origin and password.
   * If this is the case, the summary will be updated with an error.
   *
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   * @returns {boolean} True if there is an error, false otherwise.
   */
  checkMissingMandatoryFieldsError(loginData) {
    loginData.origin = LoginHelper.getLoginOrigin(loginData.origin);
    for (let mandatoryField of this.mandatoryFields) {
      if (!loginData[mandatoryField]) {
        const missingFieldRow = this.addLoginToSummary(
          { ...loginData },
          "error_missing_field"
        );
        missingFieldRow.field_name = mandatoryField;
        return true;
      }
    }
    return false;
  }

  /**
   * Validates if there is already an existing entry with similar values.
   * If there are similar values but not identical, a new "modified" entry will be added to the summary.
   * If there are identical values, a new "no_change" entry will be added to the summary
   * If either of these is the case, it will return true.
   *
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   * @returns {boolean} True if the entry is similar or identical to another previously processed entry, false otherwise.
   */
  async checkExistingEntry(loginData) {
    if (loginData.guid) {
      // First check for `guid` matches if it's set.
      // `guid` matches will allow every kind of update, including reverting
      // to older passwords which can be useful if the user wants to recover
      // an old password.
      let existingLogins = await Services.logins.searchLoginsAsync({
        guid: loginData.guid,
        origin: loginData.origin, // Ignored outside of GV.
      });

      if (existingLogins.length) {
        lazy.log.debug("maybeImportLogins: Found existing login with GUID.");
        // There should only be one `guid` match.
        let existingLogin = existingLogins[0].QueryInterface(
          Ci.nsILoginMetaInfo
        );

        if (
          loginData.username !== existingLogin.username ||
          loginData.password !== existingLogin.password ||
          loginData.httpRealm !== existingLogin.httpRealm ||
          loginData.formActionOrigin !== existingLogin.formActionOrigin ||
          `${loginData.timeCreated}` !== `${existingLogin.timeCreated}` ||
          `${loginData.timePasswordChanged}` !==
            `${existingLogin.timePasswordChanged}`
        ) {
          // Use a property bag rather than an nsILoginInfo so we don't clobber
          // properties that the import source doesn't provide.
          let propBag = LoginHelper.newPropertyBag(loginData);
          this.addLoginToSummary({ ...existingLogin }, "modified", propBag);
          return true;
        }
        this.addLoginToSummary({ ...existingLogin }, "no_change");
        return true;
      }
    }
    return false;
  }

  /**
   * Validates if there is a conflict with previous rows based on the origin.
   * We need to check the logins that we've already decided to add, to see if this is a duplicate.
   * If this is the case, we mark this one as "no_change" in the summary and return true.
   *
   * @param {object} login
   *        A login object.
   * @returns {boolean} True if the entry is similar or identical to another previously processed entry, false otherwise.
   */
  checkConflictingOriginWithPreviousRows(login) {
    let rowsPerOrigin = this.originToRows.get(login.origin);
    if (rowsPerOrigin) {
      if (
        rowsPerOrigin.some(r =>
          login.matches(r.login, false /* ignorePassword */)
        )
      ) {
        this.addLoginToSummary(login, "no_change");
        return true;
      }
      for (let row of rowsPerOrigin) {
        let newLogin = row.login;
        if (login.username == newLogin.username) {
          this.addLoginToSummary(login, "no_change");
          return true;
        }
      }
    }
    return false;
  }

  /**
   * Validates if there is a conflict with existing logins based on the origin.
   * If this is the case and there are some changes, we mark it as "modified" in the summary.
   * If it matches an existing login without any extra modifications, we mark it as "no_change".
   * For both cases we return true.
   *
   * @param {object} login
   *        A login object.
   * @returns {boolean} True if the entry is similar or identical to another previously processed entry, false otherwise.
   */
  async checkConflictingWithExistingLogins(login) {
    // While here we're passing formActionOrigin and httpRealm, they could be empty/null and get
    // ignored in that case, leading to multiple logins for the same username.
    let existingLogins = await Services.logins.searchLoginsAsync({
      origin: login.origin,
      formActionOrigin: login.formActionOrigin,
      httpRealm: login.httpRealm,
    });

    // Check for an existing login that matches *including* the password.
    // If such a login exists, we do not need to add a new login.
    if (
      existingLogins.some(l => login.matches(l, false /* ignorePassword */))
    ) {
      this.addLoginToSummary(login, "no_change");
      return true;
    }
    // Now check for a login with the same username, where it may be that we have an
    // updated password.
    let foundMatchingLogin = false;
    for (let existingLogin of existingLogins) {
      if (login.username == existingLogin.username) {
        foundMatchingLogin = true;
        existingLogin.QueryInterface(Ci.nsILoginMetaInfo);
        if (
          (login.password != existingLogin.password) &
          (login.timePasswordChanged > existingLogin.timePasswordChanged)
        ) {
          // if a login with the same username and different password already exists and it's older
          // than the current one, update its password and timestamp.
          let propBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
            Ci.nsIWritablePropertyBag
          );
          propBag.setProperty("password", login.password);
          propBag.setProperty("timePasswordChanged", login.timePasswordChanged);
          this.addLoginToSummary({ ...existingLogin }, "modified", propBag);
          return true;
        }
      }
    }
    // if the new login is an update or is older than an exiting login, don't add it.
    if (foundMatchingLogin) {
      this.addLoginToSummary(login, "no_change");
      return true;
    }
    return false;
  }

  /**
   * Validates if there are any invalid values using LoginHelper.checkLoginValues.
   * If this is the case we mark it as "error" and return true.
   *
   * @param {object} login
   *        A login object.
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   * @returns {boolean} True if there is a validation error we return true, false otherwise.
   */
  checkLoginValuesError(login, loginData) {
    try {
      // Ensure we only send checked logins through, since the validation is optimized
      // out from the bulk APIs below us.
      LoginHelper.checkLoginValues(login);
    } catch (e) {
      this.addLoginToSummary({ ...loginData }, "error");
      console.error(e);
      return true;
    }
    return false;
  }

  /**
   * Creates a new login from loginData.
   *
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   * @returns {object} A login object.
   */
  createNewLogin(loginData) {
    let login = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
      Ci.nsILoginInfo
    );
    login.init(
      loginData.origin,
      loginData.formActionOrigin,
      loginData.httpRealm,
      loginData.username,
      loginData.password,
      loginData.usernameElement || "",
      loginData.passwordElement || ""
    );

    login.QueryInterface(Ci.nsILoginMetaInfo);
    login.timeCreated = loginData.timeCreated;
    login.timeLastUsed = loginData.timeLastUsed || loginData.timeCreated;
    login.timePasswordChanged =
      loginData.timePasswordChanged || loginData.timeCreated;
    login.timesUsed = loginData.timesUsed || 1;
    login.timeLastBreachAlertDismissed =
      loginData.timeLastBreachAlertDismissed || null;
    login.guid = loginData.guid || null;
    return login;
  }

  /**
   * Cleans the action and realm field of the loginData.
   *
   * @param {object} loginData
   *        An vanilla object for the login without any methods.
   */
  cleanupActionAndRealmFields(loginData) {
    const cleanOrigin = loginData.formActionOrigin
      ? LoginHelper.getLoginOrigin(loginData.formActionOrigin, true)
      : "";
    loginData.formActionOrigin =
      cleanOrigin || (typeof loginData.httpRealm == "string" ? null : "");

    loginData.httpRealm =
      typeof loginData.httpRealm == "string" ? loginData.httpRealm : null;
  }

  /**
   * Adds a login to the summary.
   *
   * @param {object} login
   *        A login object.
   * @param {string} result
   *        The result type. One of "added", "modified", "error", "error_invalid_origin", "error_invalid_password" or "no_change".
   * @param {object} propBag
   *        An optional parameter with the properties bag.
   * @returns {object} The row that was added.
   */
  addLoginToSummary(login, result, propBag) {
    let rows = this.originToRows.get(login.origin) || [];
    if (rows.length === 0) {
      this.originToRows.set(login.origin, rows);
    }
    const newSummaryRow = { result, login, propBag };
    rows.push(newSummaryRow);
    this.summary.push(newSummaryRow);
    return newSummaryRow;
  }

  /**
   * Iterates over all then rows where more than two match the same origin. It mutates the internal state of the processor.
   * It makes sure that if the `timePasswordChanged` field is present it will be used to decide if it's a "no_change" or "added".
   * The entry with the oldest `timePasswordChanged` will be "added", the rest will be "no_change".
   */
  markLastTimePasswordChangedAsModified() {
    const originUserToRowMap = new Map();
    for (let currentRow of this.summary) {
      if (
        currentRow.result === "added" ||
        currentRow.result === "modified" ||
        currentRow.result === "no_change"
      ) {
        const originAndUser =
          currentRow.login.origin + currentRow.login.username;
        let lastTimeChangedRow = originUserToRowMap.get(originAndUser);
        if (lastTimeChangedRow) {
          if (
            (currentRow.login.password != lastTimeChangedRow.login.password) &
            (currentRow.login.timePasswordChanged >
              lastTimeChangedRow.login.timePasswordChanged)
          ) {
            lastTimeChangedRow.result = "no_change";
            currentRow.result = "added";
            originUserToRowMap.set(originAndUser, currentRow);
          }
        } else {
          originUserToRowMap.set(originAndUser, currentRow);
        }
      }
    }
  }

  /**
   * Iterates over all then rows where more than two match the same origin. It mutates the internal state of the processor.
   * It makes sure that if the `timePasswordChanged` field is present it will be used to decide if it's a "no_change" or "added".
   * The entry with the oldest `timePasswordChanged` will be "added", the rest will be "no_change".
   *
   * @returns {object[]} An entry for each processed row containing how the row was processed and the login data.
   */
  async processLoginsAndBuildSummary() {
    this.markLastTimePasswordChangedAsModified();
    for (let summaryRow of this.summary) {
      try {
        if (summaryRow.result === "added") {
          summaryRow.login = await Services.logins.addLoginAsync(
            summaryRow.login
          );
        } else if (summaryRow.result === "modified") {
          await Services.logins.modifyLoginAsync(
            summaryRow.login,
            summaryRow.propBag
          );
        }
      } catch (e) {
        console.error(e);
        summaryRow.result = "error";
      }
    }
    return this.summary;
  }
}

const OS_AUTH_FOR_PASSWORDS_BOOL_PREF =
  "signon.management.page.os-auth.locked.enabled";

// Removed by bug 2067167. Nesting depth of requestReauth() calls holding the
// internal key token logged out; a counter and not a boolean because
// token.login() spins a nested event loop a second requestReauth() can run in.
let gPrimaryPasswordReauthDepth = 0;

/**
 * Contains functions shared by different Login Manager components.
 */
export const LoginHelper = {
  debug: null,
  enabled: null,
  storageEnabled: null,
  formlessCaptureEnabled: null,
  formRemovalCaptureEnabled: null,
  generationAvailable: null,
  generationConfidenceThreshold: null,
  generationEnabled: null,
  improvedPasswordRulesEnabled: null,
  improvedPasswordRulesCollection: "password-rules",
  includeOtherSubdomainsInLookup: null,
  insecureAutofill: null,
  privateBrowsingCaptureEnabled: null,
  remoteRecipesEnabled: null,
  remoteRecipesCollection: "password-recipes",
  relatedRealmsEnabled: null,
  relatedRealmsCollection: "websites-with-shared-credential-backends",
  schemeUpgrades: null,
  showAutoCompleteFooter: null,
  showAutoCompleteImport: null,
  testOnlyUserHasInteractedWithDocument: null,
  testOnlyNotWaitForPaint: null,
  userInputRequiredToCapture: null,
  captureInputChanges: null,
  OS_AUTH_FOR_PASSWORDS_BOOL_PREF,

  // Removed by bug 2067167, with gPrimaryPasswordReauthDepth. True while
  // requestReauth() has the token logged out and is prompting, so logins
  // storage can decline to prompt on top of it.
  get primaryPasswordReauthInProgress() {
    return gPrimaryPasswordReauthDepth > 0;
  },

  init() {
    // Watch for pref changes to update cached pref values.
    Services.prefs.addObserver("signon.", () => this.updateSignonPrefs());
    this.updateSignonPrefs();

    // Watch for FXA Logout to reset signon.firefoxRelay to 'available'
    // Using hard-coded value for FxAccountsCommon.ONLOGOUT_NOTIFICATION because
    // importing FxAccountsCommon here caused hard-to-diagnose crash.
    Services.obs.addObserver(() => {
      Services.prefs.clearUserPref("signon.firefoxRelay.feature");
    }, "fxaccounts:onlogout");
  },

  updateSignonPrefs() {
    this.autofillForms = Services.prefs.getBoolPref("signon.autofillForms");
    this.autofillAutocompleteOff = Services.prefs.getBoolPref(
      "signon.autofillForms.autocompleteOff"
    );
    this.captureInputChanges = Services.prefs.getBoolPref(
      "signon.capture.inputChanges.enabled"
    );
    this.debug = Services.prefs.getBoolPref("signon.debug");
    this.enabled = Services.prefs.getBoolPref("signon.rememberSignons");
    Glean.pwmgr.savingEnabled.set(this.enabled);
    this.storageEnabled = Services.prefs.getBoolPref(
      "signon.storeSignons",
      true
    );
    this.formlessCaptureEnabled = Services.prefs.getBoolPref(
      "signon.formlessCapture.enabled"
    );
    this.formRemovalCaptureEnabled = Services.prefs.getBoolPref(
      "signon.formRemovalCapture.enabled"
    );
    this.generationAvailable = Services.prefs.getBoolPref(
      "signon.generation.available"
    );
    this.generationEnabled = Services.prefs.getBoolPref(
      "signon.generation.enabled"
    );
    this.improvedPasswordRulesEnabled = Services.prefs.getBoolPref(
      "signon.improvedPasswordRules.enabled"
    );
    this.insecureAutofill = Services.prefs.getBoolPref(
      "signon.autofillForms.http"
    );
    this.includeOtherSubdomainsInLookup = Services.prefs.getBoolPref(
      "signon.includeOtherSubdomainsInLookup"
    );
    this.passwordEditCaptureEnabled = Services.prefs.getBoolPref(
      "signon.passwordEditCapture.enabled"
    );
    this.privateBrowsingCaptureEnabled = Services.prefs.getBoolPref(
      "signon.privateBrowsingCapture.enabled"
    );
    this.schemeUpgrades = Services.prefs.getBoolPref("signon.schemeUpgrades");
    this.showAutoCompleteFooter = Services.prefs.getBoolPref(
      "signon.showAutoCompleteFooter"
    );

    this.showAutoCompleteImport = Services.prefs.getStringPref(
      "signon.showAutoCompleteImport",
      ""
    );

    this.storeWhenAutocompleteOff = Services.prefs.getBoolPref(
      "signon.storeWhenAutocompleteOff"
    );

    this.suggestImportCount = Services.prefs.getIntPref(
      "signon.suggestImportCount",
      0
    );

    if (
      Services.prefs.getBoolPref(
        "signon.testOnlyUserHasInteractedByPrefValue",
        false
      )
    ) {
      this.testOnlyUserHasInteractedWithDocument = Services.prefs.getBoolPref(
        "signon.testOnlyUserHasInteractedWithDocument",
        false
      );
      lazy.log.debug(
        `Using pref value for testOnlyUserHasInteractedWithDocument ${this.testOnlyUserHasInteractedWithDocument}.`
      );
    } else {
      this.testOnlyUserHasInteractedWithDocument = null;
    }

    this.userInputRequiredToCapture = Services.prefs.getBoolPref(
      "signon.userInputRequiredToCapture.enabled"
    );
    this.usernameOnlyFormEnabled = Services.prefs.getBoolPref(
      "signon.usernameOnlyForm.enabled"
    );
    this.usernameOnlyFormLookupThreshold = Services.prefs.getIntPref(
      "signon.usernameOnlyForm.lookupThreshold"
    );
    this.remoteRecipesEnabled = Services.prefs.getBoolPref(
      "signon.recipes.remoteRecipes.enabled"
    );
    this.relatedRealmsEnabled = Services.prefs.getBoolPref(
      "signon.relatedRealms.enabled"
    );
    // TODO: Remove this preference (Bug 1984225)
    // Used by geckoview junit test because no paint event is fired in junit test.
    this.testOnlyNotWaitForPaint = Services.prefs.getBoolPref(
      "signon.testOnlyNotWaitForPaint",
      false
    );
  },

  createLogger(aLogPrefix) {
    let getMaxLogLevel = () => {
      return this.debug ? "Debug" : "Warn";
    };

    // Create a new instance of the ConsoleAPI so we can control the maxLogLevel with a pref.
    let consoleOptions = {
      maxLogLevel: getMaxLogLevel(),
      prefix: aLogPrefix,
    };
    let logger = console.createInstance(consoleOptions);

    // Watch for pref changes and update this.debug and the maxLogLevel for created loggers
    Services.prefs.addObserver("signon.debug", () => {
      this.debug = Services.prefs.getBoolPref("signon.debug");
      if (logger) {
        logger.maxLogLevel = getMaxLogLevel();
      }
    });

    return logger;
  },

  /**
   * Due to the way the signons2.txt file is formatted, we need to make
   * sure certain field values or characters do not cause the file to
   * be parsed incorrectly.  Reject origins that we can't store correctly.
   *
   * @throws String with English message in case validation failed.
   */
  checkOriginValue(aOrigin) {
    // Nulls are invalid, as they don't round-trip well.  Newlines are also
    // invalid for any field stored as plaintext, and an origin made of a
    // single dot cannot be stored in the legacy format.
    if (
      aOrigin == "." ||
      aOrigin.includes("\r") ||
      aOrigin.includes("\n") ||
      aOrigin.includes("\0")
    ) {
      throw new Error("Invalid origin");
    }
  },

  /**
   * Due to the way the signons2.txt file was formatted, we needed to make
   * sure certain field values or characters do not cause the file to
   * be parsed incorrectly. These characters can cause problems in other
   * formats/languages too so reject logins that may not be stored correctly.
   *
   * @throws String with English message in case validation failed.
   */
  checkLoginValues(aLogin) {
    function badCharacterPresent(l, c) {
      return (
        (l.formActionOrigin && l.formActionOrigin.includes(c)) ||
        (l.httpRealm && l.httpRealm.includes(c)) ||
        l.origin.includes(c) ||
        l.usernameField.includes(c) ||
        l.passwordField.includes(c)
      );
    }

    // Nulls are invalid, as they don't round-trip well.
    // Mostly not a formatting problem, although ".\0" can be quirky.
    if (badCharacterPresent(aLogin, "\0")) {
      throw new Error("login values can't contain nulls");
    }

    if (!aLogin.password || typeof aLogin.password != "string") {
      throw new Error("passwords must be non-empty strings");
    }

    // In theory these nulls should just be rolled up into the encrypted
    // values, but nsISecretDecoderRing doesn't use nsStrings, so the
    // nulls cause truncation. Check for them here just to avoid
    // unexpected round-trip surprises.
    if (aLogin.username.includes("\0") || aLogin.password.includes("\0")) {
      throw new Error("login values can't contain nulls");
    }

    // Newlines are invalid for any field stored as plaintext.
    if (
      badCharacterPresent(aLogin, "\r") ||
      badCharacterPresent(aLogin, "\n")
    ) {
      throw new Error("login values can't contain newlines");
    }

    // A line with just a "." can have special meaning.
    if (aLogin.usernameField == "." || aLogin.formActionOrigin == ".") {
      throw new Error("login values can't be periods");
    }

    // An origin with "\ \(" won't roundtrip.
    // eg host="foo (", realm="bar" --> "foo ( (bar)"
    // vs host="foo", realm=" (bar" --> "foo ( (bar)"
    if (aLogin.origin.includes(" (")) {
      throw new Error("bad parens in origin");
    }
  },

  /**
   * Returns a new XPCOM property bag with the provided properties.
   *
   * @param {object} aProperties
   *        Each property of this object is copied to the property bag.  This
   *        parameter can be omitted to return an empty property bag.
   *
   * @return A new property bag, that is an instance of nsIWritablePropertyBag,
   *         nsIWritablePropertyBag2, nsIPropertyBag, and nsIPropertyBag2.
   */
  newPropertyBag(aProperties) {
    let propertyBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
      Ci.nsIWritablePropertyBag
    );
    if (aProperties) {
      for (let [name, value] of Object.entries(aProperties)) {
        propertyBag.setProperty(name, value);
      }
    }
    return propertyBag
      .QueryInterface(Ci.nsIPropertyBag)
      .QueryInterface(Ci.nsIPropertyBag2)
      .QueryInterface(Ci.nsIWritablePropertyBag2);
  },

  /**
   * @param {string} aURL
   * @returns {string} which is the hostPort of aURL if supported by the scheme
   *                   otherwise, returns the original aURL.
   */
  maybeGetHostPortForURL(aURL) {
    try {
      let uri = Services.io.newURI(aURL);
      return uri.hostPort;
    } catch (ex) {
      // No need to warn for javascript:/data:/about:/chrome:/etc.
    }
    return aURL;
  },

  /**
   * Get the parts of the URL we want for identification.
   * Strip out things like the userPass portion and handle javascript:.
   */
  getLoginOrigin(uriString, allowJS = false) {
    return Logic.getLoginOrigin(uriString, allowJS);
  },

  getFormActionOrigin(form) {
    return Logic.getFormActionOrigin(form);
  },

  /**
   * @param {string} aLoginOrigin - An origin value from a stored login's
   *                                origin or formActionOrigin properties.
   * @param {string} aSearchOrigin - The origin that was are looking to match
   *                                 with aLoginOrigin. This would normally come
   *                                 from a form or page that we are considering.
   * @param {nsILoginFindOptions} aOptions - Options to affect whether the origin
   *                                         from the login (aLoginOrigin) is a
   *                                         match for the origin we're looking
   *                                         for (aSearchOrigin).
   */
  isOriginMatching(
    aLoginOrigin,
    aSearchOrigin,
    aOptions = {
      schemeUpgrades: false,
      acceptWildcardMatch: false,
      acceptDifferentSubdomains: false,
      acceptRelatedRealms: false,
      relatedRealms: [],
    }
  ) {
    if (aLoginOrigin == aSearchOrigin) {
      return true;
    }

    if (!aOptions) {
      return false;
    }

    if (aOptions.acceptWildcardMatch && aLoginOrigin == "") {
      return true;
    }

    // We can only match logins now if either of these flags are true, so
    // avoid doing the work of constructing URL objects if neither is true.
    if (!aOptions.acceptDifferentSubdomains && !aOptions.schemeUpgrades) {
      return false;
    }

    try {
      let loginURI = Services.io.newURI(aLoginOrigin);
      let searchURI = Services.io.newURI(aSearchOrigin);
      let schemeMatches =
        loginURI.scheme == "http" && searchURI.scheme == "https";

      if (aOptions.acceptDifferentSubdomains) {
        let loginBaseDomain = Services.eTLD.getBaseDomain(loginURI);
        let searchBaseDomain = Services.eTLD.getBaseDomain(searchURI);
        if (
          loginBaseDomain == searchBaseDomain &&
          (loginURI.scheme == searchURI.scheme ||
            (aOptions.schemeUpgrades && schemeMatches))
        ) {
          return true;
        }
        if (
          aOptions.acceptRelatedRealms &&
          aOptions.relatedRealms.length &&
          (loginURI.scheme == searchURI.scheme ||
            (aOptions.schemeUpgrades && schemeMatches))
        ) {
          for (let relatedOrigin of aOptions.relatedRealms) {
            if (Services.eTLD.hasRootDomain(loginURI.host, relatedOrigin)) {
              return true;
            }
          }
        }
      }

      if (
        aOptions.schemeUpgrades &&
        loginURI.host == searchURI.host &&
        schemeMatches &&
        loginURI.port == searchURI.port
      ) {
        return true;
      }
    } catch (ex) {
      // newURI will throw for some values e.g. chrome://FirefoxAccounts
      // uri.host and uri.port will throw for some values e.g. javascript:
      return false;
    }

    return false;
  },

  doLoginsMatch(
    aLogin1,
    aLogin2,
    { ignorePassword = false, ignoreSchemes = false }
  ) {
    if (
      aLogin1.httpRealm != aLogin2.httpRealm ||
      aLogin1.username != aLogin2.username
    ) {
      return false;
    }

    if (!ignorePassword && aLogin1.password != aLogin2.password) {
      return false;
    }

    if (ignoreSchemes) {
      let login1HostPort = this.maybeGetHostPortForURL(aLogin1.origin);
      let login2HostPort = this.maybeGetHostPortForURL(aLogin2.origin);
      if (login1HostPort != login2HostPort) {
        return false;
      }

      if (
        aLogin1.formActionOrigin != "" &&
        aLogin2.formActionOrigin != "" &&
        this.maybeGetHostPortForURL(aLogin1.formActionOrigin) !=
          this.maybeGetHostPortForURL(aLogin2.formActionOrigin)
      ) {
        return false;
      }
    } else {
      if (aLogin1.origin != aLogin2.origin) {
        return false;
      }

      // If either formActionOrigin is blank (but not null), then match.
      if (
        aLogin1.formActionOrigin != "" &&
        aLogin2.formActionOrigin != "" &&
        aLogin1.formActionOrigin != aLogin2.formActionOrigin
      ) {
        return false;
      }
    }

    // The .usernameField and .passwordField values are ignored.

    return true;
  },

  /**
   * Creates a new login object that results by modifying the given object with
   * the provided data.
   *
   * @param {nsILoginInfo} aOldStoredLogin
   *        Existing login object to modify.
   * @param {nsILoginInfo|nsIProperyBag} aNewLoginData
   *        The new login values, either as an nsILoginInfo or nsIProperyBag.
   *
   * @return {nsILoginInfo} The newly created nsILoginInfo object.
   *
   * @throws {Error} With English message in case validation failed.
   */
  buildModifiedLogin(aOldStoredLogin, aNewLoginData) {
    function bagHasProperty(aPropName) {
      try {
        aNewLoginData.getProperty(aPropName);
        return true;
      } catch (ex) {}
      return false;
    }

    aOldStoredLogin.QueryInterface(Ci.nsILoginMetaInfo);

    let newLogin;
    if (aNewLoginData instanceof Ci.nsILoginInfo) {
      // Clone the existing login to get its nsILoginMetaInfo, then init it
      // with the replacement nsILoginInfo data from the new login.
      newLogin = aOldStoredLogin.clone();
      newLogin.init(
        aNewLoginData.origin,
        aNewLoginData.formActionOrigin,
        aNewLoginData.httpRealm,
        aNewLoginData.username,
        aNewLoginData.password,
        aNewLoginData.usernameField,
        aNewLoginData.passwordField
      );
      newLogin.unknownFields = aNewLoginData.unknownFields;
      newLogin.QueryInterface(Ci.nsILoginMetaInfo);

      // Automatically update metainfo when password is changed.
      if (newLogin.password != aOldStoredLogin.password) {
        newLogin.timePasswordChanged = Date.now();
      }
    } else if (aNewLoginData instanceof Ci.nsIPropertyBag) {
      // Clone the existing login, along with all its properties.
      newLogin = aOldStoredLogin.clone();
      newLogin.QueryInterface(Ci.nsILoginMetaInfo);

      // Automatically update metainfo when password is changed.
      // (Done before the main property updates, lest the caller be
      // explicitly updating both .password and .timePasswordChanged)
      if (bagHasProperty("password")) {
        let newPassword = aNewLoginData.getProperty("password");
        if (newPassword != aOldStoredLogin.password) {
          newLogin.timePasswordChanged = Date.now();
        }
      }

      for (let prop of aNewLoginData.enumerator) {
        switch (prop.name) {
          // nsILoginInfo (fall through)
          case "origin":
          case "httpRealm":
          case "formActionOrigin":
          case "username":
          case "password":
          case "usernameField":
          case "passwordField":
          case "unknownFields":
          // nsILoginMetaInfo (fall through)
          case "guid":
          case "timeCreated":
          case "timeLastUsed":
          case "timePasswordChanged":
          case "timesUsed":
          case "timeLastBreachAlertDismissed":
            newLogin[prop.name] = prop.value;
            break;

          // Fake property, allows easy incrementing.
          case "timesUsedIncrement":
            newLogin.timesUsed += prop.value;
            break;

          // Fail if caller requests setting an unknown property.
          default:
            throw new Error("Unexpected propertybag item: " + prop.name);
        }
      }
    } else {
      throw new Error("newLoginData needs an expected interface!");
    }

    // Sanity check the login
    if (newLogin.origin == null || !newLogin.origin.length) {
      throw new Error("Can't add a login with a null or empty origin.");
    }

    // For logins w/o a username, set to "", not null.
    if (newLogin.username == null) {
      throw new Error("Can't add a login with a null username.");
    }

    if (newLogin.password == null || !newLogin.password.length) {
      throw new Error("Can't add a login with a null or empty password.");
    }

    if (newLogin.formActionOrigin || newLogin.formActionOrigin == "") {
      // We have a form submit URL. Can't have a HTTP realm.
      if (newLogin.httpRealm != null) {
        throw new Error(
          "Can't add a login with both a httpRealm and formActionOrigin."
        );
      }
    } else if (newLogin.httpRealm || newLogin.httpRealm == "") {
      // We have a HTTP realm. Can't have a form submit URL.
      if (newLogin.formActionOrigin != null) {
        throw new Error(
          "Can't add a login with both a httpRealm and formActionOrigin."
        );
      }
    } else {
      // Need one or the other!
      throw new Error(
        "Can't add a login without a httpRealm or formActionOrigin."
      );
    }

    // Throws if there are bogus values.
    this.checkLoginValues(newLogin);

    return newLogin;
  },

  /**
   * Remove http: logins when there is an https: login with the same username and hostPort.
   * Sort order is preserved.
   *
   * @param {nsILoginInfo[]} logins
   *        A list of logins we want to process for shadowing.
   * @returns {nsILoginInfo[]} A subset of of the passed logins.
   */
  shadowHTTPLogins(logins) {
    /**
     * Map a (hostPort, username) to a boolean indicating whether `logins`
     * contains an https: login for that combo.
     */
    let hasHTTPSByHostPortUsername = new Map();
    for (let login of logins) {
      let key = this.getUniqueKeyForLogin(login, ["hostPort", "username"]);
      let hasHTTPSlogin = hasHTTPSByHostPortUsername.get(key) || false;
      let loginURI = Services.io.newURI(login.origin);
      hasHTTPSByHostPortUsername.set(
        key,
        loginURI.scheme == "https" || hasHTTPSlogin
      );
    }

    return logins.filter(login => {
      let key = this.getUniqueKeyForLogin(login, ["hostPort", "username"]);
      let loginURI = Services.io.newURI(login.origin);
      if (loginURI.scheme == "http" && hasHTTPSByHostPortUsername.get(key)) {
        // If this is an http: login and we have an https: login for the
        // (hostPort, username) combo then remove it.
        return false;
      }
      return true;
    });
  },

  /**
   * Generate a unique key string from a login.
   *
   * @param {nsILoginInfo} login
   * @param {string[]} uniqueKeys containing nsILoginInfo attribute names or "hostPort"
   * @returns {string} to use as a key in a Map
   */
  getUniqueKeyForLogin(login, uniqueKeys) {
    const KEY_DELIMITER = ":";
    return uniqueKeys.reduce((prev, key) => {
      let val = null;
      if (key == "hostPort") {
        val = Services.io.newURI(login.origin).hostPort;
      } else {
        val = login[key];
      }

      return prev + KEY_DELIMITER + val;
    }, "");
  },

  /**
   * Removes duplicates from a list of logins while preserving the sort order.
   *
   * @param {nsILoginInfo[]} logins
   *        A list of logins we want to deduplicate.
   * @param {string[]} [uniqueKeys = ["username", "password"]]
   *        A list of login attributes to use as unique keys for the deduplication.
   * @param {string[]} [resolveBy = ["timeLastUsed"]]
   *        Ordered array of keyword strings used to decide which of the
   *        duplicates should be used. "scheme" would prefer the login that has
   *        a scheme matching `preferredOrigin`'s if there are two logins with
   *        the same `uniqueKeys`. The default preference to distinguish two
   *        logins is `timeLastUsed`. If there is no preference between two
   *        logins, the first one found wins.
   * @param {string} [preferredOrigin = undefined]
   *        String representing the origin to use for preferring one login over
   *        another when they are dupes. This is used with "scheme" for
   *        `resolveBy` so the scheme from this origin will be preferred.
   * @param {string} [preferredFormActionOrigin = undefined]
   *        String representing the action origin to use for preferring one login over
   *        another when they are dupes. This is used with "actionOrigin" for
   *        `resolveBy` so the scheme from this action origin will be preferred.
   *
   * @returns {nsILoginInfo[]} list of unique logins.
   */
  dedupeLogins(
    logins,
    uniqueKeys = ["username", "password"],
    resolveBy = ["timeLastUsed"],
    preferredOrigin = undefined,
    preferredFormActionOrigin = undefined
  ) {
    if (!preferredOrigin) {
      if (resolveBy.includes("scheme")) {
        throw new Error(
          "dedupeLogins: `preferredOrigin` is required in order to " +
            "prefer schemes which match it."
        );
      }
      if (resolveBy.includes("subdomain")) {
        throw new Error(
          "dedupeLogins: `preferredOrigin` is required in order to " +
            "prefer subdomains which match it."
        );
      }
    }

    let preferredOriginScheme;
    if (preferredOrigin) {
      try {
        preferredOriginScheme = Services.io.newURI(preferredOrigin).scheme;
      } catch (ex) {
        // Handle strings that aren't valid URIs e.g. chrome://FirefoxAccounts
      }
    }

    if (!preferredOriginScheme && resolveBy.includes("scheme")) {
      lazy.log.warn(
        "Deduping with a scheme preference but couldn't get the preferred origin scheme."
      );
    }

    // We use a Map to easily lookup logins by their unique keys.
    let loginsByKeys = new Map();

    /**
     * @return {bool} whether `login` is preferred over its duplicate (considering `uniqueKeys`)
     *                `existingLogin`.
     *
     * `resolveBy` is a sorted array so we can return true the first time `login` is preferred
     * over the existingLogin.
     */
    function isLoginPreferred(existingLogin, login) {
      if (!resolveBy || !resolveBy.length) {
        // If there is no preference, prefer the existing login.
        return false;
      }

      for (let preference of resolveBy) {
        switch (preference) {
          case "actionOrigin": {
            if (!preferredFormActionOrigin) {
              break;
            }
            if (
              LoginHelper.isOriginMatching(
                existingLogin.formActionOrigin,
                preferredFormActionOrigin,
                { schemeUpgrades: LoginHelper.schemeUpgrades }
              ) &&
              !LoginHelper.isOriginMatching(
                login.formActionOrigin,
                preferredFormActionOrigin,
                { schemeUpgrades: LoginHelper.schemeUpgrades }
              )
            ) {
              return false;
            }
            break;
          }
          case "scheme": {
            if (!preferredOriginScheme) {
              break;
            }

            try {
              // Only `origin` is currently considered
              let existingLoginURI = Services.io.newURI(existingLogin.origin);
              let loginURI = Services.io.newURI(login.origin);
              // If the schemes of the two logins are the same or neither match the
              // preferredOriginScheme then we have no preference and look at the next resolveBy.
              if (
                loginURI.scheme == existingLoginURI.scheme ||
                (loginURI.scheme != preferredOriginScheme &&
                  existingLoginURI.scheme != preferredOriginScheme)
              ) {
                break;
              }

              return loginURI.scheme == preferredOriginScheme;
            } catch (e) {
              // Some URLs aren't valid nsIURI (e.g. chrome://FirefoxAccounts)
              lazy.log.debug(
                "dedupeLogins/shouldReplaceExisting: Error comparing schemes:",
                existingLogin.origin,
                login.origin,
                "preferredOrigin:",
                preferredOrigin,
                e.name
              );
            }
            break;
          }
          case "subdomain": {
            // Replace the existing login only if the new login is an exact match on the host.
            let existingLoginURI = Services.io.newURI(existingLogin.origin);
            let newLoginURI = Services.io.newURI(login.origin);
            let preferredOriginURI = Services.io.newURI(preferredOrigin);
            if (
              existingLoginURI.hostPort != preferredOriginURI.hostPort &&
              newLoginURI.hostPort == preferredOriginURI.hostPort
            ) {
              return true;
            }
            if (
              existingLoginURI.host != preferredOriginURI.host &&
              newLoginURI.host == preferredOriginURI.host
            ) {
              return true;
            }
            // if the existing login host *is* a match and the new one isn't
            // we explicitly want to keep the existing one
            if (
              existingLoginURI.host == preferredOriginURI.host &&
              newLoginURI.host != preferredOriginURI.host
            ) {
              return false;
            }
            break;
          }
          case "timeLastUsed":
          case "timePasswordChanged": {
            // If we find a more recent login for the same key, replace the existing one.
            let loginDate = login.QueryInterface(Ci.nsILoginMetaInfo)[
              preference
            ];
            let storedLoginDate = existingLogin.QueryInterface(
              Ci.nsILoginMetaInfo
            )[preference];
            if (loginDate == storedLoginDate) {
              break;
            }

            return loginDate > storedLoginDate;
          }
          default: {
            throw new Error(
              "dedupeLogins: Invalid resolveBy preference: " + preference
            );
          }
        }
      }

      return false;
    }

    for (let login of logins) {
      let key = this.getUniqueKeyForLogin(login, uniqueKeys);

      if (loginsByKeys.has(key)) {
        if (!isLoginPreferred(loginsByKeys.get(key), login)) {
          // If there is no preference for the new login, use the existing one.
          continue;
        }
      }
      loginsByKeys.set(key, login);
    }

    // Return the map values in the form of an array.
    return [...loginsByKeys.values()];
  },

  /**
   * Open the password manager in a new tab and bring the browser window
   * containing it to the foreground.
   *
   * @param {Window} window
   *                 the window from where we want to open the dialog
   *
   * @param {object?} args
   *                  params for opening the password manager
   * @param {string} [args.filterString=""]
   *                 the domain (not origin) to pass to the login manager dialog
   *                 to pre-filter the results
   * @param {string} args.entryPoint
   *                 The name of the entry point, used for telemetry
   */
  openPasswordManager(
    window,
    { filterString = "", entryPoint = "", loginGuid = null } = {}
  ) {
    // Get currently active tab's origin
    const openedFrom =
      window.gBrowser?.selectedTab.linkedBrowser.currentURI.spec;
    // If no loginGuid is set, get sanitized origin, this will return null for about:* uris
    const preselectedLogin = loginGuid ?? this.getLoginOrigin(openedFrom);

    const params = new URLSearchParams({
      ...(filterString && { filter: filterString }),
      ...(entryPoint && { entryPoint }),
    });

    const paramsPart = params.toString() ? `?${params}` : "";

    // bug 1985105 - should this only look for windows on the current workspace?
    let browserWindow = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    const browser = browserWindow.gBrowser ?? browserWindow.opener?.gBrowser;

    const tab = browser.addTrustedTab(`about:logins${paramsPart}`, {
      inBackground: false,
    });

    tab.setAttribute("preselect-login", preselectedLogin);

    // The caller may live in a different window (e.g. Page Info), so bring
    // the browser window showing about:logins to the foreground.
    browser.documentGlobal.focus();
  },

  /**
   * Checks if a field type is password compatible.
   *
   * @param {Element} element
   *                  the field we want to check.
   * @param {object} options
   * @param {bool} [options.ignoreConnect] - Whether to ignore checking isConnected
   *                                         of the element.
   *
   * @returns {boolean} true if the field can
   *                    be treated as a password input
   */
  isPasswordFieldType(element, { ignoreConnect = false } = {}) {
    return Logic.isPasswordFieldType(element, { ignoreConnect });
  },

  /**
   * Checks if a field type is username compatible.
   *
   * @param {Element} element
   *                  the field we want to check.
   * @param {object} options
   * @param {bool} [options.ignoreConnect] - Whether to ignore checking isConnected
   *                                         of the element.
   *
   * @returns {boolean} true if the field type is one
   *                    of the username types.
   */
  isUsernameFieldType(element, { ignoreConnect = false } = {}) {
    return Logic.isUsernameFieldType(element, { ignoreConnect });
  },

  /**
   * Infer whether a form is a sign-in form by searching keywords
   * in its attributes
   *
   * @param {Element} element
   *                  the form we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  isInferredLoginForm(formElement) {
    if (
      Logic.elementAttrsMatchRegex(
        formElement,
        lazy.NewPasswordModel.LoginRegex
      )
    ) {
      return true;
    }

    const buttons = Array.from(
      formElement.querySelectorAll("button[type=submit]")
    );
    // Limit to form with only one submit button to avoid false positives.
    if (
      buttons.length == 1 &&
      Logic.hasTextContentMatchingRegex(
        buttons[0],
        lazy.NewPasswordModel.LoginFormAttrRegex
      )
    ) {
      return true;
    }

    return false;
  },

  /**
   * Infer whether an input field is a username field by searching
   * 'username' keyword in its attributes
   *
   * @param {Element} element
   *                  the field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  isInferredUsernameField(element) {
    const expr = /username/i;

    let ac = element.getAutocompleteInfo()?.fieldName;
    if (ac && (ac == "username" || ac == "webauthn")) {
      return true;
    }

    if (
      Logic.elementAttrsMatchRegex(element, expr) ||
      Logic.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  },

  /**
   * Search for keywords that indicates the input field is not likely a
   * field of a username login form.
   *
   * @param {Element} element
   *                  the input field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  isInferredNonUsernameField(element) {
    const expr = /\b(search|code|add)\b/i;

    if (
      Logic.elementAttrsMatchRegex(element, expr) ||
      Logic.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  },

  /**
   * Infer whether an input field is an email field by searching
   * 'email' keyword in its attributes.
   *
   * @param {Element} element
   *                  the field we want to check.
   *
   * @returns {boolean} True if any of the rules matches
   */
  isInferredEmailField(element) {
    const expr = /email|邮箱/i;

    if (element.type == "email") {
      return true;
    }

    let ac = element.getAutocompleteInfo()?.fieldName;
    if (ac && ac == "email") {
      return true;
    }

    if (
      Logic.elementAttrsMatchRegex(element, expr) ||
      Logic.hasLabelMatchingRegex(element, expr)
    ) {
      return true;
    }

    return false;
  },

  /**
   * For each login, add the login to the password manager if a similar one
   * doesn't already exist. Merge it otherwise with the similar existing ones.
   *
   * @param {object[]} loginDatas - For each login, the data that needs to be added.
   * @returns {object[]} An entry for each processed row containing how the row was processed and the login data.
   */
  async maybeImportLogins(loginDatas) {
    // by setting this flag we ensure no events are submitted
    this.importing = true;
    const processor = new ImportRowProcessor();

    try {
      for (let rawLoginData of loginDatas) {
        // Do some sanitization on a clone of the loginData.
        let loginData = ChromeUtils.shallowClone(rawLoginData);
        if (processor.checkNonUniqueGuidError(loginData)) {
          continue;
        }
        if (processor.checkMissingMandatoryFieldsError(loginData)) {
          continue;
        }
        processor.cleanupActionAndRealmFields(loginData);
        if (await processor.checkExistingEntry(loginData)) {
          continue;
        }
        let login = processor.createNewLogin(loginData);
        if (processor.checkLoginValuesError(login, loginData)) {
          continue;
        }
        if (processor.checkConflictingOriginWithPreviousRows(login)) {
          continue;
        }
        if (await processor.checkConflictingWithExistingLogins(login)) {
          continue;
        }
        processor.addLoginToSummary(login, "added");
      }
      return await processor.processLoginsAndBuildSummary();
    } finally {
      this.importing = false;

      Services.obs.notifyObservers(null, "passwordmgr-reload-all");
      this.notifyStorageChanged("importLogins", []);
    }
  },

  /**
   * Convert an array of nsILoginInfo to vanilla JS objects suitable for
   * sending over IPC. Avoid using this in other cases.
   *
   * NB: All members of nsILoginInfo (not nsILoginMetaInfo) are strings.
   */
  loginsToVanillaObjects(logins) {
    return logins.map(this.loginToVanillaObject);
  },

  /**
   * Same as above, but for a single login.
   */
  loginToVanillaObject(login) {
    let obj = {};
    for (let i in login.QueryInterface(Ci.nsILoginMetaInfo)) {
      if (typeof login[i] !== "function") {
        obj[i] = login[i];
      }
    }
    return obj;
  },

  /**
   * Convert an object received from IPC into an nsILoginInfo (with guid).
   */
  vanillaObjectToLogin(login) {
    let formLogin = Cc["@mozilla.org/login-manager/loginInfo;1"].createInstance(
      Ci.nsILoginInfo
    );
    formLogin.init(
      login.origin,
      login.formActionOrigin,
      login.httpRealm,
      login.username,
      login.password,
      login.usernameField,
      login.passwordField
    );

    formLogin.QueryInterface(Ci.nsILoginMetaInfo);
    for (let prop of [
      "guid",
      "timeCreated",
      "timeLastUsed",
      "timePasswordChanged",
      "timesUsed",
      "timeLastBreachAlertDismissed",
    ]) {
      formLogin[prop] = login[prop];
    }
    return formLogin;
  },

  /**
   * As above, but for an array of objects.
   */
  vanillaObjectsToLogins(vanillaObjects) {
    const logins = [];
    for (const vanillaObject of vanillaObjects) {
      logins.push(this.vanillaObjectToLogin(vanillaObject));
    }
    return logins;
  },

  /**
   * Returns true if the user has a primary password set and false otherwise.
   */
  isPrimaryPasswordSet() {
    let token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
      Ci.nsIPKCS11Token
    );
    return token.hasPassword;
  },

  /**
   * Get whether the OSAuth is enabled or not.
   *
   * @returns {boolean} Whether or not OS Auth is enabled.
   */
  getOSAuthEnabled() {
    if (!lazy.OSKeyStore.canReauth()) {
      return false;
    }

    // We need to unlock the pref here to retrieve it's true value, otherwise
    // the default (false) will be returned.
    const prefName = OS_AUTH_FOR_PASSWORDS_BOOL_PREF;
    Services.prefs.unlockPref(prefName);
    const isEnabled = Services.prefs.getBoolPref(prefName, true);
    Services.prefs.lockPref(prefName);

    return isEnabled;
  },

  /**
   * Set whether the OSAuth is enabled or not.
   *
   * @param {string} prefName -> The pref to set.
   * @param {boolean} enable -> Whether the pref is to be enabled.
   */
  setOSAuthEnabled(enable) {
    const prefName = OS_AUTH_FOR_PASSWORDS_BOOL_PREF;
    Services.prefs.setBoolPref(prefName, enable);
    Services.prefs.lockPref(prefName);
  },

  async trySetOSAuthEnabled(win, checked, messageText, captionText) {
    // Calling OSKeyStore.ensureLoggedIn() instead of LoginHelper.verifyOSAuth()
    // since we want to authenticate user each time this setting is changed.

    // Note on Glean collection: because OSKeyStore.ensureLoggedIn() is not wrapped in
    // verifyOSAuth(), it will be documenting "success" for unsupported platforms
    // and won't record "fail_error", only "fail_user_canceled"
    let isAuthorized = (
      await lazy.OSKeyStore.ensureLoggedIn(messageText, captionText, win, false)
    ).authenticated;

    Glean.pwmgr.promptShownOsReauth.record({
      trigger: "toggle_pref_os_auth",
      result: isAuthorized ? "success" : "fail_user_canceled",
    });

    if (!isAuthorized) {
      return;
    }

    // If osReauthCheckbox is checked enable osauth.
    LoginHelper.setOSAuthEnabled(checked);
    Glean.pwmgr.requireOsReauthToggle.record({
      toggle_state: checked,
    });
  },

  async verifyUserOSAuth(
    prefName,
    promptMessage,
    captionDialog = "",
    parentWindow = null,
    generateKeyIfNotAvailable = true
  ) {
    if (!this.getOSAuthEnabled(prefName)) {
      promptMessage = false;
    }
    try {
      return (
        await lazy.OSKeyStore.ensureLoggedIn(
          promptMessage,
          captionDialog,
          parentWindow,
          generateKeyIfNotAvailable
        )
      ).authenticated;
    } catch (ex) {
      // Since Win throws an exception whereas Mac resolves to false upon cancelling.
      if (ex.result !== Cr.NS_ERROR_FAILURE) {
        throw ex;
      }
    }
    return false;
  },

  /**
   * Shows OS auth dialog if OS auth is enabled or the Primary Password dialog when
   * the token is locked or OS auth is disabled.
   *
   * @param {Element} browser
   *        The <browser> that the prompt should be shown on
   * @param expirationTime Optional timestamp indicating next required re-authentication
   * @param messageText Formatted and localized string to be displayed when the OS auth dialog is used.
   * @param captionText Formatted and localized string to be displayed when the OS auth dialog is used.
   * @param reason The reason for requesting reauthentication, used for telemetry.
   */
  async requestReauth(
    browser,
    expirationTime,
    messageText,
    captionText,
    reason
  ) {
    let isAuthorized = false;
    let telemetryEvent;

    // This does no harm if primary password isn't set.
    let token = Cc["@mozilla.org/security/internalkeytoken;1"].createInstance(
      Ci.nsIPKCS11Token
    );

    // Do we have a recent authorization?
    if (expirationTime && Date.now() < expirationTime) {
      isAuthorized = true;
      telemetryEvent = {
        name:
          "reauthenticate" + (token.hasPassword ? "MasterPassword" : "OsAuth"),
        value: "success_no_prompt",
      };
      return {
        isAuthorized,
        telemetryEvent,
      };
    }

    let isOSAuthEnabled = this.getOSAuthEnabled();

    // Default to true if there is no primary password and OS reauth is not available
    if (!token.hasPassword && !isOSAuthEnabled) {
      isAuthorized = true;
      telemetryEvent = {
        name: "reauthenticateOsAuth",
        value: lazy.OSKeyStore.canReauth()
          ? "success_disabled"
          : "success_unsupported_platform",
      };
      return {
        isAuthorized,
        telemetryEvent,
      };
    }
    // Use the OS auth dialog if there is no primary password
    // or if primary password is already unlocked and os auth is enabled.
    if (isOSAuthEnabled && (!token.hasPassword || token.isLoggedIn)) {
      let result;
      try {
        isAuthorized = await this.verifyUserOSAuth(
          OS_AUTH_FOR_PASSWORDS_BOOL_PREF,
          messageText,
          captionText,
          browser.documentGlobal,
          false
        );
        result = isAuthorized ? "success" : "fail_user_canceled";
      } catch (ex) {
        result = "fail_error";
        throw ex;
      } finally {
        Glean.pwmgr.promptShownOsReauth.record({
          trigger: reason,
          result,
        });
      }
      let value = lazy.OSKeyStore.canReauth()
        ? "success"
        : "success_unsupported_platform";

      telemetryEvent = {
        name: "reauthenticateOsAuth",
        value: isAuthorized ? value : "fail",
      };
      return {
        isAuthorized,
        telemetryEvent,
      };
    }
    // Removed by bug 2067167: this counter, and the try/finally around the
    // logout/login that maintains it, exist only so logins storage can tell
    // that the token is deliberately logged out and decline to prompt.
    gPrimaryPasswordReauthDepth++;
    try {
      // We'll attempt to re-auth via Primary Password, so log out.
      await token.logout();

      // If a primary password prompt is already open, just exit early and return false.
      // The user can re-trigger it after responding to the already open dialog.
      if (Services.logins.uiBusy) {
        isAuthorized = false;
        return {
          isAuthorized,
          telemetryEvent,
        };
      }

      try {
        // Log in again, which prompts for the primary password.
        await token.login();
      } catch (e) {
        // An exception will be thrown if the user cancels the login prompt
        // dialog. The user will still be logged out of Software Security Device
        // in this case.
      }
    } finally {
      gPrimaryPasswordReauthDepth--;
    }
    isAuthorized = token.isLoggedIn;
    // NSS keeps re-prompting until the password is right or the user gives up,
    // so a single event can stand for more than one dialog, and a dismissal is
    // indistinguishable from an internal failure.
    Glean.pwmgr.primaryPasswordPrompt.record({
      source: "reauth",
      trigger: reason ?? "",
      result: isAuthorized ? "success" : "cancel",
    });
    telemetryEvent = {
      name: "reauthenticateMasterPassword",
      value: isAuthorized ? "success" : "fail",
    };
    return {
      isAuthorized,
      telemetryEvent,
    };
  },

  /**
   * Records the event returned by `requestReauth`. There is none when the
   * re-authentication was not attempted, for example because a primary
   * password prompt was already open.
   *
   * @param {?object} telemetryEvent
   *        The `telemetryEvent` of a `requestReauth` result.
   */
  recordReauthTelemetryEvent(telemetryEvent) {
    if (!telemetryEvent) {
      return;
    }

    let { name, extra = {}, value = null } = telemetryEvent;
    if (value) {
      extra.value = value;
    }
    Glean.pwmgr[name].record(extra);
  },

  /**
   * Send a notification when stored data is changed.
   */
  notifyStorageChanged(changeType, data) {
    // do not emit individual events during csv import
    if (this.importing) {
      return;
    }

    let dataObject = data;
    // Can't pass a raw JS string or array though notifyObservers(). :-(
    if (Array.isArray(data)) {
      dataObject = Cc["@mozilla.org/array;1"].createInstance(
        Ci.nsIMutableArray
      );
      for (let i = 0; i < data.length; i++) {
        dataObject.appendElement(data[i]);
      }
    } else if (typeof data == "string") {
      dataObject = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      dataObject.data = data;
    }
    Services.obs.notifyObservers(
      dataObject,
      "passwordmgr-storage-changed",
      changeType
    );
  },

  isUserFacingLogin(login) {
    return login.origin != "chrome://FirefoxAccounts"; // FXA_PWDMGR_HOST
  },

  async getAllUserFacingLogins() {
    try {
      let logins = await Services.logins.getAllLogins();
      return logins.filter(this.isUserFacingLogin);
    } catch (e) {
      if (e.result == Cr.NS_ERROR_ABORT) {
        // If the user cancels the MP prompt then return no logins.
        return [];
      }
      throw e;
    }
  },

  createLoginAlreadyExistsError(guid) {
    // The GUID is stored in an nsISupportsString here because we cannot pass
    // raw JS objects within Components.Exception due to bug 743121.
    let guidSupportsString = Cc[
      "@mozilla.org/supports-string;1"
    ].createInstance(Ci.nsISupportsString);
    guidSupportsString.data = guid;
    return Components.Exception("This login already exists.", {
      data: guidSupportsString,
    });
  },

  /**
   * Determine the <browser> that a prompt should be shown on.
   *
   * Some sites pop up a temporary login window, which disappears
   * upon submission of credentials. We want to put the notification
   * prompt in the opener window if this seems to be happening.
   *
   * @param {Element} browser
   *        The <browser> that a prompt was triggered for
   * @returns {Element} The <browser> that the prompt should be shown on,
   *                    which could be in a different window.
   */
  getBrowserForPrompt(browser) {
    // The browser may have been torn down (e.g. its tab was closed) while an
    // async operation was in progress before we got here, in which case its
    // browsingContext is null and there is no prompt target left.
    if (!browser.browsingContext) {
      return browser;
    }
    let chromeWindow = browser.documentGlobal;
    let openerBrowsingContext = browser.browsingContext.opener;
    let openerBrowser = openerBrowsingContext
      ? openerBrowsingContext.top.embedderElement
      : null;
    if (openerBrowser) {
      let chromeDoc = chromeWindow.document.documentElement;

      // Check to see if the current window was opened with chrome
      // disabled, and if so use the opener window. But if the window
      // has been used to visit other pages (ie, has a history),
      // assume it'll stick around and *don't* use the opener.
      if (chromeDoc.hasAttribute("popup-window") && !browser.canGoBack) {
        lazy.log.debug("Using opener window for prompt.");
        return openerBrowser;
      }
    }

    return browser;
  },
};

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let processName =
    Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT
      ? "Main"
      : "Content";
  return LoginHelper.createLogger(`LoginHelper.${processName}`);
});

LoginHelper.init();

export class OptInFeature {
  implementation;
  #offered;
  #enabled;
  #disabled;
  #pref;

  static PREF_AVAILABLE_VALUE = "available";
  static PREF_OFFERED_VALUE = "offered";
  static PREF_ENABLED_VALUE = "enabled";
  static PREF_DISABLED_VALUE = "disabled";

  constructor(offered, enabled, disabled, pref) {
    this.#pref = pref;
    this.#offered = offered;
    this.#enabled = enabled;
    this.#disabled = disabled;

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "implementationPref",
      pref,
      undefined,
      (_preference, _prevValue, _newValue) => this.#updateImplementation()
    );

    this.#updateImplementation();
  }

  get #currentPrefValue() {
    // Read pref directly instead of relying on this.implementationPref because
    // there is an implementationPref value update lag that affects tests.
    return Services.prefs.getStringPref(this.#pref, undefined);
  }

  get isAvailable() {
    return [
      OptInFeature.PREF_AVAILABLE_VALUE,
      OptInFeature.PREF_OFFERED_VALUE,
      OptInFeature.PREF_ENABLED_VALUE,
      OptInFeature.PREF_DISABLED_VALUE,
    ].includes(this.#currentPrefValue);
  }

  get isEnabled() {
    return this.#currentPrefValue == OptInFeature.PREF_ENABLED_VALUE;
  }

  get isDisabled() {
    return this.#currentPrefValue == OptInFeature.PREF_DISABLED_VALUE;
  }

  markAsAvailable() {
    this.#markAs(OptInFeature.PREF_AVAILABLE_VALUE);
  }

  markAsOffered() {
    this.#markAs(OptInFeature.PREF_OFFERED_VALUE);
  }

  markAsEnabled() {
    this.#markAs(OptInFeature.PREF_ENABLED_VALUE);
  }

  markAsDisabled() {
    this.#markAs(OptInFeature.PREF_DISABLED_VALUE);
  }

  #markAs(value) {
    Services.prefs.setStringPref(this.#pref, value);
  }

  #updateImplementation() {
    switch (this.implementationPref) {
      case OptInFeature.PREF_ENABLED_VALUE:
        this.implementation = new this.#enabled();
        break;
      case OptInFeature.PREF_AVAILABLE_VALUE:
      case OptInFeature.PREF_OFFERED_VALUE:
        this.implementation = new this.#offered();
        break;
      case OptInFeature.PREF_DISABLED_VALUE:
      default:
        this.implementation = new this.#disabled();
        break;
    }
  }
}
