/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";
import { PromptUtils } from "resource://gre/modules/PromptUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gPrompterService",
  "@mozilla.org/login-manager/prompter;1",
  Ci.nsILoginManagerPrompter
);

/* eslint-disable block-scoped-var, no-var */

ChromeUtils.defineESModuleGetters(lazy, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

const LoginInfo = Components.Constructor(
  "@mozilla.org/login-manager/loginInfo;1",
  "nsILoginInfo",
  "init"
);

/**
 * A helper module to prevent modal auth prompt abuse.
 */
const PromptAbuseHelper = {
  getBaseDomainOrFallback(hostname) {
    try {
      return Services.eTLD.getBaseDomainFromHost(hostname);
    } catch (e) {
      return hostname;
    }
  },

  incrementPromptAbuseCounter(baseDomain, browser) {
    if (!browser) {
      return;
    }

    if (!browser.authPromptAbuseCounter) {
      browser.authPromptAbuseCounter = {};
    }

    if (!browser.authPromptAbuseCounter[baseDomain]) {
      browser.authPromptAbuseCounter[baseDomain] = 0;
    }

    browser.authPromptAbuseCounter[baseDomain] += 1;
  },

  resetPromptAbuseCounter(baseDomain, browser) {
    if (!browser || !browser.authPromptAbuseCounter) {
      return;
    }

    browser.authPromptAbuseCounter[baseDomain] = 0;
  },

  hasReachedAbuseLimit(baseDomain, browser) {
    if (!browser || !browser.authPromptAbuseCounter) {
      return false;
    }

    let abuseCounter = browser.authPromptAbuseCounter[baseDomain];
    // Allow for setting -1 to turn the feature off.
    if (this.abuseLimit < 0) {
      return false;
    }
    return !!abuseCounter && abuseCounter >= this.abuseLimit;
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  PromptAbuseHelper,
  "abuseLimit",
  "prompts.authentication_dialog_abuse_limit"
);

/**
 * Implements nsIPromptFactory
 *
 * Invoked by [toolkit/components/prompts/src/Prompter.sys.mjs]
 */
export function LoginManagerAuthPromptFactory() {
  Services.obs.addObserver(this, "passwordmgr-crypto-login", true);
}

LoginManagerAuthPromptFactory.prototype = {
  classID: Components.ID("{749e62f4-60ae-4569-a8a2-de78b649660e}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIPromptFactory",
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  // Tracks pending auth prompts per top level browser and hash key.
  // browser -> hashkey -> prompt
  // This enables us to consolidate auth prompts with the same browser and
  // hashkey (level, origin, realm).
  _pendingPrompts: new WeakMap(),
  _pendingSavePrompts: new WeakMap(),
  // We use a separate bucket for when we don't have a browser.
  // _noBrowser -> hashkey -> prompt
  _noBrowser: {},
  // Promise used to defer prompts if the password manager isn't ready when
  // they're called.
  _uiBusyPromise: null,
  _uiBusyResolve: null,

  observe(_subject, topic, _data) {
    this.log(`Observed topic: ${topic}.`);
    if (topic == "passwordmgr-crypto-login") {
      // Show the deferred prompters.
      this._uiBusyResolve?.();
    }
  },

  getPrompt(aWindow, aIID) {
    var prompt = new LoginManagerAuthPrompter().QueryInterface(aIID);
    prompt.init(aWindow, this);
    return prompt;
  },

  getPendingPrompt(browser, hashKey) {
    // If there is already a matching auth prompt which has no browser
    // associated we can reuse it. This way we avoid showing tab level prompts
    // when there is already a pending window prompt.
    let pendingNoBrowserPrompt = this._pendingPrompts
      .get(this._noBrowser)
      ?.get(hashKey);
    if (pendingNoBrowserPrompt) {
      return pendingNoBrowserPrompt;
    }
    return this._pendingPrompts.get(browser)?.get(hashKey);
  },

  _dismissPendingSavePrompt(browser) {
    this._pendingSavePrompts.get(browser)?.dismiss();
    this._pendingSavePrompts.delete(browser);
  },

  _setPendingSavePrompt(browser, prompt) {
    this._pendingSavePrompts.set(browser, prompt);
  },

  _setPendingPrompt(browser, prompt, hashKey) {
    let hashToPrompt = this._pendingPrompts.get(browser);
    if (!hashToPrompt) {
      hashToPrompt = new Map();
      this._pendingPrompts.set(browser, hashToPrompt);
    }
    hashToPrompt.set(hashKey, prompt);
  },

  _removePendingPrompt(browser, hashKey) {
    let hashToPrompt = this._pendingPrompts.get(browser);
    if (!hashToPrompt) {
      return;
    }
    hashToPrompt.delete(hashKey);
    if (!hashToPrompt.size) {
      this._pendingPrompts.delete(browser);
    }
  },

  async _waitForLoginsUI(prompt) {
    await this._uiBusyPromise;

    const { displayHost, realm } = PromptUtils.getAuthTarget(
      prompt.channel,
      prompt.authInfo
    );

    // No UI to wait for.
    if (!Services.logins.uiBusy) {
      return;
    }

    let hasLogins =
      (await Services.logins.countLoginsAsync(displayHost, null, realm)) > 0;
    if (
      !hasLogins &&
      lazy.LoginHelper.schemeUpgrades &&
      displayHost.startsWith("https://")
    ) {
      let httpDisplayHost = displayHost.replace(/^https:\/\//, "http://");
      hasLogins =
        (await Services.logins.countLoginsAsync(httpDisplayHost, null, realm)) >
        0;
    }
    // We don't depend on saved logins.
    if (!hasLogins) {
      return;
    }

    this.log("Waiting for primary password UI.");

    this._uiBusyPromise = new Promise(resolve => {
      this._uiBusyResolve = resolve;
    });
    await this._uiBusyPromise;
  },

  async _doAsyncPrompt(prompt, hashKey) {
    // The prompter's browser can change while the prompt is pending, so the
    // entry has to be removed from the bucket it was added to.
    let browser = prompt.prompter.browser || this._noBrowser;
    this._setPendingPrompt(browser, prompt, hashKey);

    // UI might be busy due to the primary password dialog. Wait for it to close.
    await this._waitForLoginsUI(prompt);

    let ok = false;
    let promptAborted = false;
    try {
      this.log(`Performing the prompt for ${hashKey}.`);
      ok = await prompt.prompter.promptAuthInternal(
        prompt.channel,
        prompt.level,
        prompt.authInfo
      );
    } catch (e) {
      if (
        e instanceof Components.Exception &&
        e.result == Cr.NS_ERROR_NOT_AVAILABLE
      ) {
        this.log("Bypassed, UI is not available in this context.");
        // Prompts throw NS_ERROR_NOT_AVAILABLE if they're aborted.
        promptAborted = true;
      } else {
        console.error("LoginManagerAuthPrompter: _doAsyncPrompt", e);
      }
    }

    this._removePendingPrompt(browser, hashKey);

    // Handle callbacks
    for (var consumer of prompt.consumers) {
      if (!consumer.callback) {
        // Not having a callback means that consumer didn't provide it
        // or canceled the notification
        continue;
      }

      this.log(`Calling back to callback: ${consumer.callback} ok: ${ok}.`);
      try {
        if (ok) {
          consumer.callback.onAuthAvailable(consumer.context, prompt.authInfo);
        } else {
          consumer.callback.onAuthCancelled(consumer.context, !promptAborted);
        }
      } catch (e) {
        /* Throw away exceptions caused by callback */
      }
    }
  },
}; // end of LoginManagerAuthPromptFactory implementation

ChromeUtils.defineLazyGetter(
  LoginManagerAuthPromptFactory.prototype,
  "log",
  () => {
    let logger = lazy.LoginHelper.createLogger("LoginManagerAuthPromptFactory");
    return logger.log.bind(logger);
  }
);

/* ==================== LoginManagerAuthPrompter ==================== */

/**
 * Implements interfaces for prompting the user to enter/save/change auth info.
 *
 * nsIAuthPrompt: Used by SeaMonkey, Thunderbird, but not Firefox.
 *
 * Note this implementation no longer provides `nsIAuthPrompt.promptPassword()`
 * and `nsIAuthPrompt.promptUsernameAndPassword()`. Use their async
 * counterparts `asyncPromptPassword` and `asyncPromptUsernameAndPassword`
 * instead.
 *
 * nsIAuthPrompt2: Is invoked by a channel for protocol-based authentication
 * (eg HTTP Authenticate, FTP login).
 *
 * nsILoginManagerAuthPrompter: Used by consumers to indicate which tab/window a
 * prompt should appear on.
 */
export function LoginManagerAuthPrompter() {}

LoginManagerAuthPrompter.prototype = {
  classID: Components.ID("{8aa66d77-1bbb-45a6-991e-b8f47751c291}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIAuthPrompt",
    "nsIAuthPrompt2",
    "nsILoginManagerAuthPrompter",
  ]),

  _factory: null,
  _chromeWindow: null,
  _browser: null,

  __strBundle: null, // String bundle for L10N
  get _strBundle() {
    if (!this.__strBundle) {
      this.__strBundle = Services.strings.createBundle(
        "chrome://passwordmgr/locale/passwordmgr.properties"
      );
      if (!this.__strBundle) {
        throw new Error("String bundle for Login Manager not present!");
      }
    }

    return this.__strBundle;
  },

  // Whether we are in private browsing mode
  get _inPrivateBrowsing() {
    if (this._chromeWindow) {
      return PrivateBrowsingUtils.isWindowPrivate(this._chromeWindow);
    }
    // If we don't that we're in private browsing mode if the caller did
    // not provide a window.  The callers which really care about this
    // will indeed pass down a window to us, and for those who don't,
    // we can just assume that we don't want to save the entered login
    // information.
    this.log("We have no chromeWindow so assume we're in a private context.");
    return true;
  },

  get _allowRememberLogin() {
    if (!this._inPrivateBrowsing) {
      return true;
    }
    return lazy.LoginHelper.privateBrowsingCaptureEnabled;
  },

  /* ---------- nsIAuthPrompt prompts ---------- */

  /**
   * Wrapper around the prompt service prompt. Saving random fields here
   * doesn't really make sense and therefore isn't implemented.
   */
  prompt(
    aDialogTitle,
    aText,
    aPasswordRealm,
    aSavePassword,
    aDefaultText,
    aResult
  ) {
    if (aSavePassword != Ci.nsIAuthPrompt.SAVE_PASSWORD_NEVER) {
      throw new Components.Exception(
        "prompt only supports SAVE_PASSWORD_NEVER",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }

    if (aDefaultText) {
      aResult.value = aDefaultText;
    }

    return Services.prompt.prompt(
      this._chromeWindow,
      aDialogTitle,
      aText,
      aResult,
      null,
      {}
    );
  },

  /**
   * Looks up a username and password in the database. Will prompt the user
   * with a dialog, even if a username and password are found.
   *
   * Returns a promise, resolving to an object containing the result as well as
   * the password and username:
   * Eg:
   * {
   *    ok: true,
   *    username: "user",
   *    password: "secure",
   * }
   */
  async asyncPromptUsernameAndPassword(
    aDialogTitle,
    aText,
    aPasswordRealm,
    aSavePassword,
    aUsername,
    aPassword
  ) {
    if (aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_FOR_SESSION) {
      throw new Components.Exception(
        "asyncPromptUsernameAndPassword doesn't support SAVE_PASSWORD_FOR_SESSION",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }

    let foundLogins = null;
    let canRememberLogin = false;
    var selectedLogin = null;
    var [origin, realm] = this._getRealmInfo(aPasswordRealm);

    // If origin is null, we can't save this login.
    if (origin) {
      if (this._allowRememberLogin) {
        canRememberLogin =
          aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY &&
          Services.logins.getLoginSavingEnabled(origin);
      }

      // Look for existing logins.
      foundLogins = await Services.logins.searchLoginsAsync({
        origin,
        httpRealm: realm,
      });

      // XXX Like the original code, we can't deal with multiple
      // account selection. (bug 227632)
      if (foundLogins.length) {
        selectedLogin = foundLogins[0];

        // If the caller provided a username, try to use it. If they
        // provided only a password, this will try to find a password-only
        // login (or return null if none exists).
        if (aUsername.value) {
          selectedLogin = this._repickSelectedLogin(
            foundLogins,
            aUsername.value
          );
        }

        if (selectedLogin) {
          aUsername.value = selectedLogin.username;
          // If the caller provided a password, prefer it.
          if (!aPassword.value) {
            aPassword.value = selectedLogin.password;
          }
        }
      }
    }

    let autofilled = !!aPassword.value;
    var ok = Services.prompt.promptUsernameAndPassword(
      this._chromeWindow,
      aDialogTitle,
      aText,
      aUsername,
      aPassword
    );

    if (!ok || !canRememberLogin) {
      return {
        ok,
        username: aUsername.value,
        password: aPassword.value,
      };
    }

    if (!aPassword.value) {
      this.log("No password entered, so won't offer to save.");
      return {
        ok,
        username: aUsername.value,
        password: aPassword.value,
      };
    }

    // XXX We can't prompt with multiple logins yet (bug 227632), so
    // the entered login might correspond to an existing login
    // other than the one we originally selected.
    selectedLogin = this._repickSelectedLogin(foundLogins, aUsername.value);

    // If we didn't find an existing login, or if the username
    // changed, save as a new login.
    let newLogin = new LoginInfo(
      origin,
      null,
      realm,
      aUsername.value,
      aPassword.value
    );
    if (!selectedLogin) {
      // add as new
      this.log(`New login seen for: ${realm}.`);
      await Services.logins.addLoginAsync(newLogin);
    } else if (aPassword.value != selectedLogin.password) {
      // update password
      this.log(`Updating password for ${realm}.`);
      await this._updateLogin(selectedLogin, newLogin);
    } else {
      this.log("Login unchanged, no further action needed.");
      await Services.logins.recordPasswordUseAsync(
        selectedLogin,
        this._inPrivateBrowsing,
        "PromptLogin",
        autofilled
      );
    }

    return {
      ok,
      username: aUsername.value,
      password: aPassword.value,
    };
  },

  /**
   * If a password is found in the database for the password realm, it is
   * returned straight away without displaying a dialog.
   *
   * If a password is not found in the database, the user will be prompted
   * with a dialog with a text field and ok/cancel buttons. If the user
   * allows it, then the password will be saved in the database.
   *
   * Returns a promise, resolving to an object containing the result as well as
   * the password:
   * Eg:
   * {
   *    ok: true,
   *    password: "secure",
   * }
   */
  async asyncPromptPassword(
    aDialogTitle,
    aText,
    aPasswordRealm,
    aSavePassword,
    aPassword
  ) {
    if (aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_FOR_SESSION) {
      throw new Components.Exception(
        "promptPassword doesn't support SAVE_PASSWORD_FOR_SESSION",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }

    var [origin, realm, username] = this._getRealmInfo(aPasswordRealm);

    username = decodeURIComponent(username);

    let canRememberLogin = false;
    // If origin is null, we can't save this login.
    if (origin && !this._inPrivateBrowsing) {
      canRememberLogin =
        aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY &&
        Services.logins.getLoginSavingEnabled(origin);
      if (!aPassword.value) {
        // Look for existing logins.
        let foundLogins = await Services.logins.searchLoginsAsync({
          origin,
          httpRealm: realm,
        });

        // XXX Like the original code, we can't deal with multiple
        // account selection (bug 227632). We can deal with finding the
        // account based on the supplied username - but in this case we'll
        // just return the first match.
        for (var i = 0; i < foundLogins.length; ++i) {
          if (foundLogins[i].username == username) {
            // wallet returned straight away, so this mimics that code
            aPassword.value = foundLogins[i].password;
            // returning the found password
            return {
              ok: true,
              password: aPassword.value,
            };
          }
        }
      }
    }

    var ok = Services.prompt.promptPassword(
      this._chromeWindow,
      aDialogTitle,
      aText,
      aPassword
    );

    if (ok && canRememberLogin && aPassword.value) {
      let newLogin = new LoginInfo(
        origin,
        null,
        realm,
        username,
        aPassword.value
      );

      this.log(`New login seen for ${realm}.`);

      await Services.logins.addLoginAsync(newLogin);
    }

    // returning the provided password
    return {
      ok,
      password: aPassword.value,
    };
  },

  /* ---------- nsIAuthPrompt helpers ---------- */

  /**
   * Given aRealmString, such as "http://user@example.com/foo", returns an
   * array of:
   *   - the formatted origin
   *   - the realm (origin + path)
   *   - the username, if present
   *
   * If aRealmString is in the format produced by NS_GetAuthKey for HTTP[S]
   * channels, e.g. "example.com:80 (httprealm)", null is returned for all
   * arguments to let callers know the login can't be saved because we don't
   * know whether it's http or https.
   */
  _getRealmInfo(aRealmString) {
    var httpRealm = /^.+ \(.+\)$/;
    if (httpRealm.test(aRealmString)) {
      return [null, null, null];
    }

    var uri = Services.io.newURI(aRealmString);
    var pathname = "";

    if (uri.pathQueryRef != "/") {
      pathname = uri.pathQueryRef;
    }

    var formattedOrigin = this._getFormattedOrigin(uri);

    return [formattedOrigin, formattedOrigin + pathname, uri.username];
  },

  async promptAuthInternal(aChannel, aLevel, aAuthInfo) {
    var selectedLogin = null;
    var epicfail = false;
    var canAutologin = false;
    var foundLogins;
    let autofilled = false;
    let origin, httpRealm;

    try {
      // If the user submits a login but it fails, we need to remove the
      // notification prompt that was displayed. Conveniently, the user will
      // be prompted for authentication again, which brings us here.
      this._factory._dismissPendingSavePrompt(this._browser);

      ({ displayHost: origin, realm: httpRealm } = PromptUtils.getAuthTarget(
        aChannel,
        aAuthInfo
      ));

      // Looks for existing logins to prefill the prompt with.
      foundLogins = await Services.logins.searchLoginsAsync({
        origin,
        httpRealm,
        schemeUpgrades: lazy.LoginHelper.schemeUpgrades,
      });
      this.log(`Found ${foundLogins.length} matching logins.`);
      let resolveBy = ["scheme", "timePasswordChanged"];
      foundLogins = lazy.LoginHelper.dedupeLogins(
        foundLogins,
        ["username"],
        resolveBy,
        origin
      );
      this.log(`${foundLogins.length} matching logins remain after deduping.`);

      // XXX Can't select from multiple accounts yet. (bug 227632)
      if (foundLogins.length) {
        selectedLogin = foundLogins[0];
        this._SetAuthInfo(
          aAuthInfo,
          selectedLogin.username,
          selectedLogin.password
        );
        autofilled = true;

        // Allow automatic proxy login
        if (
          aAuthInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY &&
          !(aAuthInfo.flags & Ci.nsIAuthInformation.PREVIOUS_FAILED) &&
          Services.prefs.getBoolPref("signon.autologin.proxy") &&
          !PrivateBrowsingUtils.permanentPrivateBrowsing
        ) {
          this.log("Autologin enabled, skipping auth prompt.");
          canAutologin = true;
        }
      }

      var canRememberLogin = Services.logins.getLoginSavingEnabled(origin);
      if (!this._allowRememberLogin) {
        canRememberLogin = false;
      }
    } catch (e) {
      // Ignore any errors and display the prompt anyway.
      epicfail = true;
      console.error("LoginManagerAuthPrompter: Epic fail in promptAuth:", e);
    }

    var ok = canAutologin;
    let browser = this._browser;
    let baseDomain;

    // We might not have a browser or browser.currentURI.host could fail
    // (e.g. on about:blank). Fall back to the subresource hostname in that case.
    try {
      let topLevelHost = browser.currentURI.host;
      baseDomain = PromptAbuseHelper.getBaseDomainOrFallback(topLevelHost);
    } catch (e) {
      baseDomain = PromptAbuseHelper.getBaseDomainOrFallback(origin);
    }

    if (!ok) {
      if (PromptAbuseHelper.hasReachedAbuseLimit(baseDomain, browser)) {
        this.log("Blocking auth dialog, due to exceeding dialog bloat limit.");
        return false;
      }

      // Set up a counter for ensuring that the basic auth prompt can not
      // be abused for DOS-style attacks. With this counter, each eTLD+1
      // per browser will get a limited number of times a user can
      // cancel the prompt until we stop showing it.
      PromptAbuseHelper.incrementPromptAbuseCounter(baseDomain, browser);

      if (this._chromeWindow) {
        PromptUtils.fireDialogEvent(
          this._chromeWindow,
          "DOMWillOpenModalDialog",
          this._browser
        );
      }

      ok = await Services.prompt.asyncPromptAuth(
        this._browser?.browsingContext,
        Ci.nsIPrompt.MODAL_TYPE_TAB,
        aChannel,
        aLevel,
        aAuthInfo
      );
    }

    let [username, password] = this._GetAuthInfo(aAuthInfo);

    // Reset the counter state if the user replied to a prompt and actually
    // tried to login (vs. simply clicking any button to get out).
    if (ok && (username || password)) {
      PromptAbuseHelper.resetPromptAbuseCounter(baseDomain, browser);
    }

    if (!ok || !canRememberLogin || epicfail) {
      return ok;
    }

    try {
      if (!password) {
        this.log("No password entered, so won't offer to save.");
        return ok;
      }

      // XXX We can't prompt with multiple logins yet (bug 227632), so
      // the entered login might correspond to an existing login
      // other than the one we originally selected.
      selectedLogin = this._repickSelectedLogin(foundLogins, username);

      // If we didn't find an existing login, or if the username
      // changed, save as a new login.
      let newLogin = new LoginInfo(origin, null, httpRealm, username, password);
      if (!selectedLogin) {
        this.log(`New login seen for origin: ${origin}.`);

        let promptBrowser = lazy.LoginHelper.getBrowserForPrompt(browser);
        let savePrompt = lazy.gPrompterService.promptToSavePassword(
          promptBrowser,
          newLogin
        );
        this._factory._setPendingSavePrompt(promptBrowser, savePrompt);
      } else if (password != selectedLogin.password) {
        this.log(`Updating password for origin: ${origin}.`);

        let promptBrowser = lazy.LoginHelper.getBrowserForPrompt(browser);
        let savePrompt = lazy.gPrompterService.promptToChangePassword(
          promptBrowser,
          selectedLogin,
          newLogin
        );
        this._factory._setPendingSavePrompt(promptBrowser, savePrompt);
      } else {
        this.log("Login unchanged, no further action needed.");
        await Services.logins.recordPasswordUseAsync(
          selectedLogin,
          this._inPrivateBrowsing,
          "AuthLogin",
          autofilled
        );
      }
    } catch (e) {
      console.error("LoginManagerAuthPrompter: Fail2 in promptAuth:", e);
    }

    return ok;
  },

  /* ---------- nsIAuthPrompt2 prompts ---------- */

  /**
   * Implementation of nsIAuthPrompt2.
   *
   * @param {nsIChannel} aChannel
   * @param {int}        aLevel
   * @param {nsIAuthInformation} aAuthInfo
   */
  promptAuth(aChannel, aLevel, aAuthInfo) {
    let closed = false;
    let result = false;
    this.promptAuthInternal(aChannel, aLevel, aAuthInfo)
      .then(ok => (result = ok))
      .finally(() => (closed = true));
    Services.tm.spinEventLoopUntilOrQuit(
      "LoginManagerAuthPrompter.sys.mjs:promptAuth",
      () => closed
    );
    return result;
  },

  asyncPromptAuth(aChannel, aCallback, aContext, aLevel, aAuthInfo) {
    var cancelable = null;

    try {
      // If the user submits a login but it fails, we need to remove the
      // notification prompt that was displayed. Conveniently, the user will
      // be prompted for authentication again, which brings us here.
      this._factory._dismissPendingSavePrompt(this._browser);

      cancelable = this._newAsyncPromptConsumer(aCallback, aContext);

      const { displayHost: origin, realm: httpRealm } =
        PromptUtils.getAuthTarget(aChannel, aAuthInfo);

      let hashKey = aLevel + "|" + origin + "|" + httpRealm;
      let pendingPrompt = this._factory.getPendingPrompt(
        this._browser,
        hashKey
      );
      if (pendingPrompt) {
        this.log(
          `Prompt bound to an existing one in the queue, callback: ${aCallback}.`
        );
        pendingPrompt.consumers.push(cancelable);
        return cancelable;
      }

      this.log(`Adding new async prompt, callback: ${aCallback}.`);
      let asyncPrompt = {
        consumers: [cancelable],
        channel: aChannel,
        authInfo: aAuthInfo,
        level: aLevel,
        prompter: this,
      };

      this._factory._doAsyncPrompt(asyncPrompt, hashKey);
    } catch (e) {
      console.error("LoginManagerAuthPrompter: asyncPromptAuth:", e);
      console.error("Falling back to promptAuth");
      // Fail the prompt operation to let the consumer fall back
      // to synchronous promptAuth method
      throw e;
    }

    return cancelable;
  },

  /* ---------- nsILoginManagerAuthPrompter prompts ---------- */

  init(aWindow = null, aFactory = null) {
    if (!aWindow) {
      // There may be no applicable window e.g. in a Sandbox or JSM.
      this._chromeWindow = null;
      this._browser = null;
    } else if (aWindow.isChromeWindow) {
      this._chromeWindow = aWindow;
      // needs to be set explicitly using setBrowser
      this._browser = null;
    } else {
      let { win, browser } = this._getChromeWindow(aWindow);
      this._chromeWindow = win;
      this._browser = browser;
    }
    this._factory = aFactory || null;
  },

  set browser(aBrowser) {
    this._browser = aBrowser;
  },

  get browser() {
    return this._browser;
  },

  /* ---------- Internal Methods ---------- */

  async _updateLogin(login, aNewLogin) {
    var now = Date.now();
    var propBag = Cc["@mozilla.org/hash-property-bag;1"].createInstance(
      Ci.nsIWritablePropertyBag
    );
    propBag.setProperty("formActionOrigin", aNewLogin.formActionOrigin);
    propBag.setProperty("origin", aNewLogin.origin);
    propBag.setProperty("password", aNewLogin.password);
    propBag.setProperty("username", aNewLogin.username);
    // Explicitly set the password change time here (even though it would
    // be changed automatically), to ensure that it's exactly the same
    // value as timeLastUsed.
    propBag.setProperty("timePasswordChanged", now);
    propBag.setProperty("timeLastUsed", now);
    propBag.setProperty("timesUsedIncrement", 1);
    // Note that we don't call `recordPasswordUseAsync` so we won't potentially record
    // both a use and a save/update. See bug 1640096.
    await Services.logins.modifyLoginAsync(login, propBag);
  },

  /**
   * Given a content DOM window, returns the chrome window and browser it's in.
   */
  _getChromeWindow(aWindow) {
    let browser = aWindow.docShell.chromeEventHandler;
    if (!browser) {
      return null;
    }

    let chromeWin = browser.documentGlobal;
    if (!chromeWin) {
      return null;
    }

    return { win: chromeWin, browser };
  },

  /**
   * The user might enter a login that isn't the one we prefilled, but
   * is the same as some other existing login. So, pick a login with a
   * matching username, or return null.
   */
  _repickSelectedLogin(foundLogins, username) {
    for (var i = 0; i < foundLogins.length; i++) {
      if (foundLogins[i].username == username) {
        return foundLogins[i];
      }
    }
    return null;
  },

  /**
   * Can be called as:
   *   _getLocalizedString("key1");
   *   _getLocalizedString("key2", ["arg1"]);
   *   _getLocalizedString("key3", ["arg1", "arg2"]);
   *   (etc)
   *
   * Returns the localized string for the specified key,
   * formatted if required.
   *
   */
  _getLocalizedString(key, formatArgs) {
    if (formatArgs) {
      return this._strBundle.formatStringFromName(key, formatArgs);
    }
    return this._strBundle.GetStringFromName(key);
  },

  /**
   * Sanitizes the specified username, by stripping quotes and truncating if
   * it's too long. This helps prevent an evil site from messing with the
   * "save password?" prompt too much.
   */
  _sanitizeUsername(username) {
    if (username.length > 30) {
      username = username.substring(0, 30) + Services.locale.ellipsis;
    }
    return username.replace(/['"]/g, "");
  },

  /**
   * The aURI parameter may either be a string uri, or an nsIURI instance.
   *
   * Returns the origin to use in a nsILoginInfo object (for example,
   * "http://example.com").
   */
  _getFormattedOrigin(aURI) {
    let uri;
    if (aURI instanceof Ci.nsIURI) {
      uri = aURI;
    } else {
      uri = Services.io.newURI(aURI);
    }

    return uri.scheme + "://" + uri.displayHostPort;
  },

  /**
   * Converts a login's origin field (a URL) to a short string for
   * prompting purposes. Eg, "http://foo.com" --> "foo.com", or
   * "ftp://www.site.co.uk" --> "site.co.uk".
   */
  _getShortDisplayHost(aURIString) {
    var displayHost;

    var idnService = Cc["@mozilla.org/network/idn-service;1"].getService(
      Ci.nsIIDNService
    );
    try {
      var uri = Services.io.newURI(aURIString);
      var baseDomain = Services.eTLD.getBaseDomain(uri);
      displayHost = idnService.convertToDisplayIDN(baseDomain);
    } catch (e) {
      this.log(`Couldn't process supplied URIString ${aURIString}.`);
    }

    if (!displayHost) {
      displayHost = aURIString;
    }

    return displayHost;
  },

  /**
   * Returns [username, password] as extracted from aAuthInfo (which
   * holds this info after having prompted the user).
   *
   * If the authentication was for a Windows domain, we'll prepend the
   * return username with the domain. (eg, "domain\user")
   */
  _GetAuthInfo(aAuthInfo) {
    var username, password;

    var flags = aAuthInfo.flags;
    if (flags & Ci.nsIAuthInformation.NEED_DOMAIN && aAuthInfo.domain) {
      username = aAuthInfo.domain + "\\" + aAuthInfo.username;
    } else {
      username = aAuthInfo.username;
    }

    password = aAuthInfo.password;

    return [username, password];
  },

  /**
   * Given a username (possibly in DOMAIN\user form) and password, parses the
   * domain out of the username if necessary and sets domain, username and
   * password on the auth information object.
   */
  _SetAuthInfo(aAuthInfo, username, password) {
    var flags = aAuthInfo.flags;
    if (flags & Ci.nsIAuthInformation.NEED_DOMAIN) {
      // Domain is separated from username by a backslash
      var idx = username.indexOf("\\");
      if (idx == -1) {
        aAuthInfo.username = username;
      } else {
        aAuthInfo.domain = username.substring(0, idx);
        aAuthInfo.username = username.substring(idx + 1);
      }
    } else {
      aAuthInfo.username = username;
    }
    aAuthInfo.password = password;
  },

  _newAsyncPromptConsumer(aCallback, aContext) {
    return {
      QueryInterface: ChromeUtils.generateQI(["nsICancelable"]),
      callback: aCallback,
      context: aContext,
      cancel() {
        this.callback.onAuthCancelled(this.context, false);
        this.callback = null;
        this.context = null;
      },
    };
  },
}; // end of LoginManagerAuthPrompter implementation

ChromeUtils.defineLazyGetter(LoginManagerAuthPrompter.prototype, "log", () => {
  let logger = lazy.LoginHelper.createLogger("LoginManagerAuthPrompter");
  return logger.log.bind(logger);
});
