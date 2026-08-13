/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RESTRequest } from "resource://services-common/rest.sys.mjs";

import {
  log,
  SCOPE_APP_SYNC,
  SCOPE_PROFILE,
} from "resource://gre/modules/FxAccountsCommon.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

ChromeUtils.defineESModuleGetters(lazy, {
  EnsureFxAccountsWebChannel:
    "resource://gre/modules/FxAccountsWebChannel.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "ROOT_URL",
  "identity.fxaccounts.remote.root"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "CONTEXT_PARAM",
  "identity.fxaccounts.contextParam"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PAIRING_VERSION",
  "identity.fxaccounts.pairing.version",
  1
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "REQUIRES_HTTPS",
  "identity.fxaccounts.allowHttp",
  false,
  null,
  val => !val
);

const CONFIG_PREFS = [
  "identity.fxaccounts.remote.root",
  "identity.fxaccounts.auth.uri",
  "identity.fxaccounts.remote.oauth.uri",
  "identity.fxaccounts.remote.profile.uri",
  "identity.fxaccounts.remote.pairing.uri",
  "identity.sync.tokenserver.uri",
];
const SYNC_PARAM = "sync";

export var FxAccountsConfig = {
  async promiseEmailURI(email, entrypoint, extraParams = {}) {
    return this._buildURL("", {
      includeAuthParams: true,
      extraParams: {
        entrypoint,
        email,
        service: SYNC_PARAM,
        ...extraParams,
      },
    });
  },

  async promiseConnectAccountURI(entrypoint, extraParams = {}) {
    return this._buildURL("", {
      includeAuthParams: true,
      extraParams: {
        entrypoint,
        action: "email",
        service: SYNC_PARAM,
        ...extraParams,
      },
    });
  },

  async promiseManageURI(entrypoint, extraParams = {}) {
    return this._buildURL("settings", {
      extraParams: { entrypoint, ...extraParams },
      addAccountIdentifiers: true,
    });
  },

  async promiseChangeAvatarURI(entrypoint, extraParams = {}) {
    return this._buildURL("settings/avatar/change", {
      extraParams: { entrypoint, ...extraParams },
      addAccountIdentifiers: true,
    });
  },

  async promiseManageDevicesURI(entrypoint, extraParams = {}) {
    return this._buildURL("settings/clients", {
      extraParams: { entrypoint, ...extraParams },
      addAccountIdentifiers: true,
    });
  },

  async promiseConnectDeviceURI(entrypoint, extraParams = {}) {
    return this._buildURL("connect_another_device", {
      extraParams: {
        entrypoint,
        service: SYNC_PARAM,
        v: lazy.PAIRING_VERSION,
        ...extraParams,
      },
      addAccountIdentifiers: true,
    });
  },

  async promiseSetPasswordURI(entrypoint, extraParams = {}) {
    const authParams = await this._getAuthParams();
    return this._buildURL("post_verify/third_party_auth/set_password", {
      extraParams: {
        entrypoint,
        ...authParams,
        ...extraParams,
      },
      addAccountIdentifiers: true,
    });
  },

  async promisePairingURI(extraParams = {}) {
    return this._buildURL("pair", {
      extraParams,
      includeDefaultParams: false,
    });
  },

  async promiseOAuthURI(extraParams = {}) {
    return this._buildURL("oauth", {
      extraParams,
      includeDefaultParams: false,
    });
  },

  async promiseMetricsFlowURI(entrypoint, extraParams = {}) {
    return this._buildURL("metrics-flow", {
      extraParams: { entrypoint, ...extraParams },
      includeDefaultParams: false,
    });
  },

  get defaultParams() {
    return { context: lazy.CONTEXT_PARAM };
  },

  /**
   * @param path should be parsable by the URL constructor first parameter.
   * @param {bool} [options.includeDefaultParams] If true include the default search params.
   * @param {bool} [options.includeAuthParams] If true include the auth params.
   * @param {[key: string]: string} [options.extraParams] Additionnal search params.
   * @param {bool} [options.addAccountIdentifiers] if true we add the current logged-in user uid and email to the search params.
   */
  async _buildURL(
    path,
    {
      includeDefaultParams = true,
      includeAuthParams = false,
      extraParams = {},
      addAccountIdentifiers = false,
    }
  ) {
    await this.ensureConfigured();
    const url = new URL(path, lazy.ROOT_URL);
    this.ensureHTTPS(url.protocol);
    const authParams = includeAuthParams ? await this._getAuthParams() : {};
    const params = {
      ...(includeDefaultParams ? this.defaultParams : null),
      ...extraParams,
      ...authParams,
    };
    for (let [k, v] of Object.entries(params)) {
      url.searchParams.append(k, v);
    }
    if (addAccountIdentifiers) {
      const accountData = await this.getSignedInUser();
      if (!accountData) {
        return null;
      }
      url.searchParams.append("uid", accountData.uid);
      url.searchParams.append("email", accountData.email);
    }
    return url.href;
  },

  ensureHTTPS(protocol) {
    if (lazy.REQUIRES_HTTPS && protocol != "https:") {
      throw new Error("Firefox Accounts server must use HTTPS");
    }
  },

  async _buildURLFromString(href, extraParams = {}) {
    const url = new URL(href);
    for (let [k, v] of Object.entries(extraParams)) {
      url.searchParams.append(k, v);
    }
    return url.href;
  },

  resetConfigURLs() {
    // We unconditionally reset all the prefs, which will point them at prod. If the autoconfig URL is not set,
    // these will be used next sign in. If the autoconfig pref *is* set then as we start the signin flow we
    // will reconfigure all the prefs we just restored to whereever that autoconfig pref points now.
    for (let pref of CONFIG_PREFS) {
      Services.prefs.clearUserPref(pref);
    }
    // Reset FxAccountsClient
    lazy.fxAccounts.resetFxAccountsClient();

    // Reset the webchannel.
    lazy.EnsureFxAccountsWebChannel();
  },

  getAutoConfigURL() {
    let pref = Services.prefs.getStringPref(
      "identity.fxaccounts.autoconfig.uri",
      ""
    );
    if (!pref) {
      // no pref / empty pref means we don't bother here.
      return "";
    }
    let rootURL = Services.urlFormatter.formatURL(pref);
    if (rootURL.endsWith("/")) {
      rootURL = rootURL.slice(0, -1);
    }
    return rootURL;
  },

  async ensureConfigured() {
    // We don't want to update any configuration if we are already signed in,
    // or in the process of signing in.
    let isSignedIn = !!(await this.getSignedInUser());
    if (!isSignedIn) {
      await this.updateConfigURLs();
    }
  },

  // Returns true if this user is using the FxA "production" systems, false
  // if using any other configuration, including self-hosting or the FxA
  // non-production systems such as "dev" or "staging".
  // It's typically used as a proxy for "is this likely to be a self-hosted
  // user?", but it's named this way to make the implementation less
  // surprising. As a result, it's fairly conservative and would prefer to have
  // a false-negative than a false-position as it determines things which users
  // might consider sensitive (notably, telemetry).
  // Note also that while it's possible to self-host just sync and not FxA, we
  // don't make that distinction - that's a self-hoster from the POV of this
  // function.
  isProductionConfig() {
    // Specifically, if the autoconfig URLs, or *any* of the URLs that
    // we consider configurable are modified, we assume self-hosted.
    if (this.getAutoConfigURL()) {
      return false;
    }
    for (let pref of CONFIG_PREFS) {
      if (Services.prefs.prefHasUserValue(pref)) {
        return false;
      }
    }
    return true;
  },

  // Read expected client configuration from the fxa auth server
  // (from `identity.fxaccounts.autoconfig.uri`/.well-known/fxa-client-configuration)
  // and replace all the relevant our prefs with the information found there.
  // This is only done before sign-in and sign-up, and even then only if the
  // `identity.fxaccounts.autoconfig.uri` preference is set.
  async updateConfigURLs() {
    let rootURL = this.getAutoConfigURL();
    if (!rootURL) {
      return;
    }
    const config = await this.fetchConfigDocument(rootURL);
    try {
      // Update the prefs directly specified by the config.
      let authServerBase = config.auth_server_base_url;
      if (!authServerBase.endsWith("/v1")) {
        authServerBase += "/v1";
      }
      Services.prefs.setStringPref(
        "identity.fxaccounts.auth.uri",
        authServerBase
      );
      Services.prefs.setStringPref(
        "identity.fxaccounts.remote.oauth.uri",
        config.oauth_server_base_url + "/v1"
      );
      // At the time of landing this, our servers didn't yet answer with pairing_server_base_uri.
      // Remove this condition check once Firefox 68 is stable.
      if (config.pairing_server_base_uri) {
        Services.prefs.setStringPref(
          "identity.fxaccounts.remote.pairing.uri",
          config.pairing_server_base_uri
        );
      }
      Services.prefs.setStringPref(
        "identity.fxaccounts.remote.profile.uri",
        config.profile_server_base_url + "/v1"
      );
      Services.prefs.setStringPref(
        "identity.sync.tokenserver.uri",
        config.sync_tokenserver_base_url + "/1.0/sync/1.5"
      );
      Services.prefs.setStringPref("identity.fxaccounts.remote.root", rootURL);

      // Reset FxAccountsClient
      lazy.fxAccounts.resetFxAccountsClient();

      // Ensure the webchannel is pointed at the correct uri
      lazy.EnsureFxAccountsWebChannel();
    } catch (e) {
      log.error(
        "Failed to initialize configuration preferences from autoconfig object",
        e
      );
      throw e;
    }
  },

  // Read expected client configuration from the fxa auth server
  // (or from the provided rootURL, if present) and return it as an object.
  async fetchConfigDocument(rootURL = null) {
    if (!rootURL) {
      rootURL = lazy.ROOT_URL;
    }
    let configURL = rootURL + "/.well-known/fxa-client-configuration";
    let request = new RESTRequest(configURL);
    request.setHeader("Accept", "application/json");

    // Catch and rethrow the error inline.
    let resp = await request.get().catch(e => {
      log.error(`Failed to get configuration object from "${configURL}"`, e);
      throw e;
    });
    if (!resp.success) {
      // Note: 'resp.body' is included with the error log below as we are not concerned
      // that the body will contain PII, but if that changes it should be excluded.
      log.error(
        `Received HTTP response code ${resp.status} from configuration object request:
        ${resp.body}`
      );
      throw new Error(
        `HTTP status ${resp.status} from configuration object request`
      );
    }
    log.debug("Got successful configuration response", resp.body);
    try {
      return JSON.parse(resp.body);
    } catch (e) {
      log.error(
        `Failed to parse configuration preferences from ${configURL}`,
        e
      );
      throw e;
    }
  },

  // For test purposes, returns a Promise.
  getSignedInUser() {
    return lazy.fxAccounts.getSignedInUser();
  },

  async _getAuthParams() {
    let params = {};
    const scopes = [SCOPE_APP_SYNC, SCOPE_PROFILE];
    Object.assign(
      params,
      await lazy.fxAccounts._internal.beginOAuthFlow(scopes)
    );
    return params;
  },
};
