/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const lazy = {};

const { FxAccounts } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccounts.sys.mjs"
);

const { CLIENT_IS_THUNDERBIRD } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

ChromeUtils.defineESModuleGetters(lazy, {
  FxAccountsConfig: "resource://gre/modules/FxAccountsConfig.sys.mjs",
});

add_task(async function test_non_https_with_requireHttps_false() {
  Services.prefs.setBoolPref("identity.fxaccounts.allowHttp", true);

  FxAccounts.config.ensureHTTPS("http://");
  Assert.ok(true, "`ensureHTTPS did not throw an error");

  Services.prefs.clearUserPref("identity.fxaccounts.allowHttp");
});

add_task(async function test_non_https_with_requireHttps_true() {
  try {
    FxAccounts.config.ensureHTTPS("http://");
  } catch (error) {
    Assert.ok(error.message, "Firefox Accounts server must use HTTPS");
  }
});

add_task(async function test_is_production_config() {
  // should start with no auto-config URL.
  Assert.ok(!FxAccounts.config.getAutoConfigURL());
  // which means we are using prod.
  Assert.ok(FxAccounts.config.isProductionConfig());

  // Set an auto-config URL.
  Services.prefs.setStringPref(
    "identity.fxaccounts.autoconfig.uri",
    "http://x"
  );
  Assert.equal(FxAccounts.config.getAutoConfigURL(), "http://x");
  Assert.ok(!FxAccounts.config.isProductionConfig());

  // Clear the auto-config URL, but set one of the other config params.
  Services.prefs.clearUserPref("identity.fxaccounts.autoconfig.uri");
  Services.prefs.setStringPref("identity.sync.tokenserver.uri", "http://t");
  Assert.ok(!FxAccounts.config.isProductionConfig());
  Services.prefs.clearUserPref("identity.sync.tokenserver.uri");
});

add_task(async function test_reset_fxAccounts_singleton_client() {
  Services.prefs.setStringPref("identity.fxaccounts.autoconfig.uri", "");
  let defaultHostUri = "https://api.accounts.firefox.com/v1";
  let alt = "x.net";
  let altHostUri = `https://api-accounts.${alt}/v1`;

  let fetchConfig = lazy.FxAccountsConfig.fetchConfigDocument;
  lazy.FxAccountsConfig.fetchConfigDocument = function () {
    return {
      auth_server_base_url: altHostUri,
      oauth_server_base_url: `https://oauth.${alt}`,
      pairing_server_base_uri: `wss://x.channelserver.nonprod.cloudops.mozgcp.net`,
      profile_server_base_url: `https://profile.${alt}`,
      sync_tokenserver_base_url: `https://token.${alt}`,
    };
  };

  // Check that the host points to the default host
  Assert.equal(lazy.fxAccounts._internal.fxAccountsClient.host, defaultHostUri);

  Services.prefs.setStringPref(
    "identity.fxaccounts.autoconfig.uri",
    "http://x"
  );
  await lazy.FxAccountsConfig.updateConfigURLs();

  // Check that the host points to the preset auth server URL
  Assert.equal(lazy.fxAccounts._internal.fxAccountsClient.host, altHostUri);

  lazy.FxAccountsConfig.resetConfigURLs();

  // Check that `resetConfigURLs` clears prefs
  let clearedRootPref = Services.prefs.getStringPref(
    "identity.fxaccounts.remote.root"
  );
  Assert.equal(clearedRootPref, "https://accounts.firefox.com/");

  let clearedAuthPref = Services.prefs.getStringPref(
    "identity.fxaccounts.auth.uri"
  );
  Assert.equal(clearedAuthPref, "https://api.accounts.firefox.com/v1");

  let clearedOauthPref = Services.prefs.getStringPref(
    "identity.fxaccounts.remote.oauth.uri"
  );
  Assert.equal(clearedOauthPref, "https://oauth.accounts.firefox.com/v1");

  let clearedProfilePref = Services.prefs.getStringPref(
    "identity.fxaccounts.remote.profile.uri"
  );
  Assert.equal(clearedProfilePref, "https://profile.accounts.firefox.com/v1");

  let clearedPairingPref = Services.prefs.getStringPref(
    "identity.fxaccounts.remote.pairing.uri"
  );
  Assert.equal(clearedPairingPref, "wss://channelserver.services.mozilla.com");

  let clearedTokenServerPref = Services.prefs.getStringPref(
    "identity.sync.tokenserver.uri"
  );
  Assert.equal(
    clearedTokenServerPref,
    "https://token.services.mozilla.com/1.0/sync/1.5"
  );

  lazy.FxAccountsConfig.fetchConfigDocument = fetchConfig;
  Services.prefs.clearUserPref("identity.fxaccounts.autoconfig.uri");
});

add_task(async function test_promise_account_service_param() {
  Services.prefs.setStringPref("identity.fxaccounts.autoconfig.uri", "");
  Services.prefs.setStringPref(
    "identity.fxaccounts.remote.root",
    "https://accounts.firefox.com/"
  );

  let url = new URL(await FxAccounts.config.promiseConnectAccountURI("test"));
  Assert.equal(url.searchParams.get("context"), "oauth_webchannel_v1");
  Assert.equal(
    url.searchParams.get("client_id"),
    CLIENT_IS_THUNDERBIRD ? "8269bacd7bbc7f80" : "5882386c6d801776"
  );
  Assert.equal(url.searchParams.get("service"), "sync");

  let url2 = new URL(
    await FxAccounts.config.promiseConnectAccountURI("test", {
      service: "custom-service",
    })
  );
  Assert.equal(url2.searchParams.get("context"), "oauth_webchannel_v1");
  Assert.equal(
    url2.searchParams.get("client_id"),
    CLIENT_IS_THUNDERBIRD ? "8269bacd7bbc7f80" : "5882386c6d801776"
  );

  Assert.equal(url2.searchParams.get("service"), "custom-service");
});

add_task(async function test_promise_connect_device_uri_pairing_version() {
  Services.prefs.setStringPref("identity.fxaccounts.autoconfig.uri", "");
  Services.prefs.setStringPref(
    "identity.fxaccounts.remote.root",
    "https://accounts.firefox.com/"
  );

  const getSignedInUser = FxAccounts.config.getSignedInUser;
  FxAccounts.config.getSignedInUser = async () => ({
    uid: "abcd",
    email: "test@example.com",
  });

  try {
    let url = new URL(await FxAccounts.config.promiseConnectDeviceURI("test"));
    Assert.equal(url.searchParams.get("v"), "1", "defaults to version 1");

    Services.prefs.setIntPref("identity.fxaccounts.pairing.version", 2);
    let url2 = new URL(await FxAccounts.config.promiseConnectDeviceURI("test"));
    Assert.equal(url2.searchParams.get("v"), "2", "reflects the pref");
  } finally {
    Services.prefs.clearUserPref("identity.fxaccounts.pairing.version");
    FxAccounts.config.getSignedInUser = getSignedInUser;
  }
});
