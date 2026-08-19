/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ON_PROFILE_CHANGE_NOTIFICATION, WEBCHANNEL_ID, log } =
  ChromeUtils.importESModule("resource://gre/modules/FxAccountsCommon.sys.mjs");
const { CryptoUtils } = ChromeUtils.importESModule(
  "moz-src:///services/crypto/modules/utils.sys.mjs"
);
const { FxAccountsWebChannel, FxAccountsWebChannelHelpers } =
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccountsWebChannel.sys.mjs"
  );

const { PREF_LAST_FXA_USER_EMAIL, PREF_LAST_FXA_USER_UID } =
  ChromeUtils.importESModule("resource://gre/modules/FxAccountsCommon.sys.mjs");

const URL_STRING = "https://example.com";

const PREF_PAIRING_ENABLED = "identity.fxaccounts.pairing.enabled";
const PREF_PAIRING_VERSION = "identity.fxaccounts.pairing.version";

const mockSendingContext = {
  browsingContext: { top: { embedderElement: {} } },
  principal: {},
  eventTarget: {},
};

add_setup(function setup() {
  // The profile service requires the directory service to have been initialized.
  Cc["@mozilla.org/xre/directory-provider;1"].getService(Ci.nsIXREDirProvider);
});

add_test(function () {
  validationHelper(undefined, "Error: Missing configuration options");

  validationHelper(
    {
      channel_id: WEBCHANNEL_ID,
    },
    "Error: Missing 'content_uri' option"
  );

  validationHelper(
    {
      content_uri: "bad uri",
      channel_id: WEBCHANNEL_ID,
    },
    /NS_ERROR_MALFORMED_URI/
  );

  validationHelper(
    {
      content_uri: URL_STRING,
    },
    "Error: Missing 'channel_id' option"
  );

  run_next_test();
});

add_task(async function test_rejection_reporting() {
  Services.prefs.setBoolPref(
    "browser.tabs.remote.separatePrivilegedMozillaWebContentProcess",
    false
  );

  let mockMessage = {
    command: "fxaccounts:login",
    messageId: "1234",
    data: { email: "testuser@testuser.com", uid: "testuser" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      login(accountData) {
        equal(
          accountData.email,
          "testuser@testuser.com",
          "Should forward incoming message data to the helper"
        );
        return Promise.reject(new Error("oops"));
      },
    },
  });

  let promiseSend = new Promise(resolve => {
    channel._channel.send = (message, context) => {
      resolve({ message, context });
    };
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);

  let { message, context } = await promiseSend;

  equal(context, mockSendingContext, "Should forward the original context");
  equal(
    message.command,
    "fxaccounts:login",
    "Should include the incoming command"
  );
  equal(message.messageId, "1234", "Should include the message ID");
  equal(
    message.data.error.message,
    "Error: oops",
    "Should convert the error message to a string"
  );
  notStrictEqual(
    message.data.error.stack,
    null,
    "Should include the stack for JS error rejections"
  );
});

add_test(function test_exception_reporting() {
  let mockMessage = {
    command: "fxaccounts:sync_preferences",
    messageId: "5678",
    data: { entryPoint: "fxa:verification_complete" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      openSyncPreferences(browser, entryPoint) {
        equal(
          entryPoint,
          "fxa:verification_complete",
          "Should forward incoming message data to the helper"
        );
        throw new TypeError("splines not reticulated");
      },
    },
  });

  channel._channel.send = (message, context) => {
    equal(context, mockSendingContext, "Should forward the original context");
    equal(
      message.command,
      "fxaccounts:sync_preferences",
      "Should include the incoming command"
    );
    equal(message.messageId, "5678", "Should include the message ID");
    equal(
      message.data.error.message,
      "TypeError: splines not reticulated",
      "Should convert the exception to a string"
    );
    notStrictEqual(
      message.data.error.stack,
      null,
      "Should include the stack for JS exceptions"
    );

    run_next_test();
  };

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_error_message_remove_profile_path() {
  const errors = {
    windows: {
      err: new Error(
        "Win error 183 during operation rename on file C:\\Users\\Some Computer\\AppData\\Roaming\\" +
          "Mozilla\\Firefox\\Profiles\\dbzjmzxa.default\\signedInUser.json (Cannot create a file)"
      ),
      expected:
        "Error: Win error 183 during operation rename on file C:[REDACTED]signedInUser.json (Cannot create a file)",
    },
    unix: {
      err: new Error(
        "Unix error 28 during operation write on file /Users/someuser/Library/Application Support/" +
          "Firefox/Profiles/dbzjmzxa.default-release-7/signedInUser.json (No space left on device)"
      ),
      expected:
        "Error: Unix error 28 during operation write on file [REDACTED]signedInUser.json (No space left on device)",
    },
    netpath: {
      err: new Error(
        "Win error 32 during operation rename on file \\\\SVC.LOC\\HOMEDIRS$\\USERNAME\\Mozilla\\" +
          "Firefox\\Profiles\\dbzjmzxa.default-release-7\\signedInUser.json (No space left on device)"
      ),
      expected:
        "Error: Win error 32 during operation rename on file [REDACTED]signedInUser.json (No space left on device)",
    },
    mount: {
      err: new Error(
        "Win error 649 during operation rename on file C:\\SnapVolumes\\MountPoints\\" +
          "{9e399ec5-0000-0000-0000-100000000000}\\SVROOT\\Users\\username\\AppData\\Roaming\\Mozilla\\Firefox\\" +
          "Profiles\\dbzjmzxa.default-release\\signedInUser.json (The create operation failed)"
      ),
      expected:
        "Error: Win error 649 during operation rename on file C:[REDACTED]signedInUser.json " +
        "(The create operation failed)",
    },
  };
  const mockMessage = {
    command: "fxaccounts:sync_preferences",
    messageId: "1234",
  };
  const channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  let testNum = 0;
  const toTest = Object.keys(errors).length;
  for (const key in errors) {
    let error = errors[key];
    channel._channel.send = message => {
      equal(
        message.data.error.message,
        error.expected,
        "Should remove the profile path from the error message"
      );
      testNum++;
      if (testNum === toTest) {
        run_next_test();
      }
    };
    channel._sendError(error.err, mockMessage, mockSendingContext);
  }
});

add_test(function test_profile_image_change_message() {
  var mockMessage = {
    command: "profile:change",
    data: { uid: "foo" },
  };

  makeObserver(ON_PROFILE_CHANGE_NOTIFICATION, function (subject, topic, data) {
    Assert.equal(data, "foo");
    run_next_test();
  });

  var channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_login_message() {
  let mockMessage = {
    command: "fxaccounts:login",
    data: { email: "testuser@testuser.com" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      login(accountData) {
        Assert.equal(accountData.email, "testuser@testuser.com");
        run_next_test();
        return Promise.resolve();
      },
    },
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_oauth_login() {
  const mockData = {
    code: "oauth code",
    state: "state parameter",
    declinedSyncEngines: ["tabs", "creditcards"],
    offeredSyncEngines: ["tabs", "creditcards", "history"],
  };
  const mockMessage = {
    command: "fxaccounts:oauth_login",
    data: mockData,
  };
  const channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      oauthLogin(data) {
        Assert.deepEqual(data, mockData);
        run_next_test();
        return Promise.resolve();
      },
    },
  });
  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_logout_message() {
  let mockMessage = {
    command: "fxaccounts:logout",
    data: { uid: "foo" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      logout(uid) {
        Assert.equal(uid, "foo");
        run_next_test();
        return Promise.resolve();
      },
    },
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_delete_message() {
  let mockMessage = {
    command: "fxaccounts:delete",
    data: { uid: "foo" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      logout(uid) {
        Assert.equal(uid, "foo");
        run_next_test();
        return Promise.resolve();
      },
    },
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_can_link_account_message() {
  let mockMessage = {
    command: "fxaccounts:can_link_account",
    data: { email: "testuser@testuser.com", uid: "testuser" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      _selectableProfilesEnabled() {
        return false;
      },
      shouldAllowRelink(acctData) {
        Assert.deepEqual(acctData, mockMessage.data);
        run_next_test();
      },
    },
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_sync_preferences_message() {
  let mockMessage = {
    command: "fxaccounts:sync_preferences",
    data: { entryPoint: "fxa:verification_complete" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      openSyncPreferences(browser, entryPoint) {
        Assert.equal(entryPoint, "fxa:verification_complete");
        Assert.equal(
          browser,
          mockSendingContext.browsingContext.top.embedderElement
        );
        run_next_test();
      },
    },
  });

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_fxa_status_message() {
  let mockMessage = {
    command: "fxaccounts:fxa_status",
    messageId: 123,
    data: {
      service: "sync",
      context: "fx_desktop_v3",
    },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      async getFxaStatus(service, sendingContext, isPairing, context) {
        Assert.equal(service, "sync");
        Assert.equal(sendingContext, mockSendingContext);
        Assert.ok(!isPairing);
        Assert.equal(context, "fx_desktop_v3");
        return {
          signedInUser: {
            email: "testuser@testuser.com",
            sessionToken: "session-token",
            uid: "uid",
            verified: true,
          },
          capabilities: {
            engines: ["creditcards", "addresses"],
          },
        };
      },
    },
  });

  channel._channel = {
    send(response) {
      Assert.equal(response.command, "fxaccounts:fxa_status");
      Assert.equal(response.messageId, 123);

      let signedInUser = response.data.signedInUser;
      Assert.ok(!!signedInUser);
      Assert.equal(signedInUser.email, "testuser@testuser.com");
      Assert.equal(signedInUser.sessionToken, "session-token");
      Assert.equal(signedInUser.uid, "uid");
      Assert.equal(signedInUser.verified, true);

      deepEqual(response.data.capabilities.engines, [
        "creditcards",
        "addresses",
      ]);

      run_next_test();
    },
  };

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_respond_to_invalid_commands() {
  let mockMessageLogout = {
    command: "fxaccounts:lagaut", // intentional typo.
    messageId: 123,
    data: {},
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });
  channel._channel = {
    send(response) {
      Assert.equal("fxaccounts:lagaut", response.command);
      Assert.ok(!!response.data);
      Assert.ok(!!response.data.error);

      run_next_test();
    },
  };

  channel._channelCallback(
    WEBCHANNEL_ID,
    mockMessageLogout,
    mockSendingContext
  );
});

add_test(function test_unrecognized_message() {
  let mockMessage = {
    command: "fxaccounts:unrecognized",
    data: {},
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  // no error is expected.
  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
  run_next_test();
});

add_test(function test_helpers_should_allow_relink_same_account() {
  let helpers = new FxAccountsWebChannelHelpers();

  helpers.setPreviousAccountHashPref("testuser");
  Assert.ok(
    helpers.shouldAllowRelink({
      email: "testuser@testuser.com",
      uid: "testuser",
    })
  );

  run_next_test();
});

add_test(function test_helpers_should_allow_relink_different_email() {
  let helpers = new FxAccountsWebChannelHelpers();

  helpers.setPreviousAccountHashPref("testuser");

  helpers._promptForRelink = acctName => {
    return acctName === "allowed_to_relink@testuser.com";
  };

  Assert.ok(
    helpers.shouldAllowRelink({
      uid: "uid",
      email: "allowed_to_relink@testuser.com",
    })
  );
  Assert.ok(
    !helpers.shouldAllowRelink({
      uid: "uid",
      email: "not_allowed_to_relink@testuser.com",
    })
  );

  run_next_test();
});

add_task(async function test_helpers_login_without_customize_sync() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      getSignedInUser() {
        return Promise.resolve(null);
      },
      _internal: {
        setSignedInUser(accountData) {
          return new Promise(resolve => {
            // ensure fxAccounts is informed of the new user being signed in.
            Assert.equal(accountData.email, "testuser@testuser.com");

            // verifiedCanLinkAccount should be stripped in the data.
            Assert.equal(false, "verifiedCanLinkAccount" in accountData);

            resolve();
          });
        },
      },
      telemetry: {
        recordConnection: sinon.spy(),
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {
          configure() {},
        },
      },
    },
  });

  // ensure the previous account pref is overwritten.
  helpers.setPreviousAccountHashPref("lastuser");

  await helpers.login({
    uid: "testuser",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
});

add_task(async function test_helpers_login_set_previous_account_hash() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      getSignedInUser() {
        return Promise.resolve(null);
      },
      _internal: {
        setSignedInUser() {
          return new Promise(resolve => {
            // previously signed in user preference is updated.
            Assert.equal(
              Services.prefs.getStringPref(PREF_LAST_FXA_USER_UID),
              CryptoUtils.sha256Base64("new_uid")
            );
            Assert.equal(
              Services.prefs.getStringPref(PREF_LAST_FXA_USER_EMAIL, ""),
              ""
            );
            resolve();
          });
        },
      },
      telemetry: {
        recordConnection() {},
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {
          configure() {},
        },
      },
    },
  });

  // ensure the previous account pref is overwritten.
  helpers.setPreviousAccountHashPref("last_uid");

  await helpers.login({
    uid: "new_uid",
    email: "newuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
    verified: true,
  });
});

add_task(async function test_helpers_login_another_user_signed_in() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      getSignedInUser() {
        return Promise.resolve({ uid: "foo" });
      },
      _internal: {
        setSignedInUser(accountData) {
          return new Promise(resolve => {
            // ensure fxAccounts is informed of the new user being signed in.
            Assert.equal(accountData.email, "testuser@testuser.com");
            resolve();
          });
        },
      },
      telemetry: {
        recordConnection: sinon.spy(),
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {
          configure() {},
        },
      },
    },
  });
  helpers._disconnect = sinon.spy();

  await helpers.login({
    uid: "testuser",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
  Assert.ok(helpers._disconnect.called);
});

// The FxA server sends the `login` command after the user is signed in
// when upgrading from third-party auth to password + sync
add_task(async function test_helpers_login_same_user_signed_in() {
  let updateUserAccountDataCalled = false;
  let setSignedInUserCalled = false;

  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      getSignedInUser() {
        return Promise.resolve({
          uid: "testuser",
          email: "testuser@testuser.com",
        });
      },
      _internal: {
        updateUserAccountData(accountData) {
          updateUserAccountDataCalled = true;
          Assert.equal(accountData.email, "testuser@testuser.com");
          Assert.equal(accountData.uid, "testuser");
          return Promise.resolve();
        },
        setSignedInUser() {
          setSignedInUserCalled = true;
          return Promise.resolve();
        },
      },
      telemetry: {
        recordConnection: sinon.spy(),
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {
          configure() {},
        },
      },
    },
  });
  helpers._disconnect = sinon.spy();

  await helpers.login({
    uid: "testuser",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
  });

  Assert.ok(
    updateUserAccountDataCalled,
    "updateUserAccountData should be called"
  );
  Assert.ok(!setSignedInUserCalled, "setSignedInUser should not be called");
  Assert.ok(!helpers._disconnect.called, "_disconnect should not be called");
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
});

add_task(async function test_helpers_login_with_customize_sync() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        setSignedInUser(accountData) {
          return new Promise(resolve => {
            // ensure fxAccounts is informed of the new user being signed in.
            Assert.equal(accountData.email, "testuser@testuser.com");

            // customizeSync should be stripped in the data.
            Assert.equal(false, "customizeSync" in accountData);

            resolve();
          });
        },
      },
      getSignedInUser() {
        return Promise.resolve(null);
      },
      telemetry: {
        recordConnection: sinon.spy(),
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {
          configure() {},
        },
      },
    },
  });

  await helpers.login({
    uid: "testuser",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: true,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
});

add_task(async function test_helpers_persist_requested_services() {
  let accountData = null;
  const helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        async setSignedInUser(newAccountData) {
          accountData = newAccountData;
          return accountData;
        },
        async updateUserAccountData(updatedFields) {
          accountData = { ...accountData, ...updatedFields };
          return accountData;
        },
      },
      async getSignedInUser() {
        return accountData;
      },
      telemetry: {
        recordConnection() {},
      },
    },
    weaveXPCOM: {
      whenLoaded() {},
      Weave: {
        Service: {},
      },
    },
  });

  await helpers.login({
    uid: "auid",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    services: {
      first_only: { x: 10 }, // this data is not in the update below.
      sync: { important: true },
    },
  });

  Assert.deepEqual(JSON.parse(accountData.requestedServices), {
    first_only: { x: 10 },
    sync: { important: true },
  });
  // A second "login" message without the services.
  await helpers.login({
    uid: "auid",
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    services: {
      // the service is mentioned, but data is empty, so it's the old version of the data we want.
      sync: {},
      // a new service we never saw before, but we still want it.
      new: { name: "opted in" }, // not in original, but we want in the final.
    },
  });
  // the version with the data should remain.
  Assert.deepEqual(JSON.parse(accountData.requestedServices), {
    first_only: { x: 10 },
    sync: { important: true },
    new: { name: "opted in" },
  });
});

add_task(async function test_helpers_oauth_login_defers_sync_without_keys() {
  const accountState = {
    uid: "uid123",
    sessionToken: "session-token",
    email: "user@example.com",
    requestedServices: "",
  };
  const destroyOAuthToken = sinon.stub().resolves();
  const completeOAuthFlow = sinon
    .stub()
    .resolves({ scopedKeys: null, refreshToken: "refresh-token" });
  const setScopedKeys = sinon.spy();
  const setUserVerified = sinon.spy();
  const updateUserAccountData = sinon.stub().resolves();

  const helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        async getUserAccountData() {
          return accountState;
        },
        completeOAuthFlow,
        destroyOAuthToken,
        setScopedKeys,
        updateUserAccountData,
        setUserVerified,
      },
    },
  });

  await helpers.oauthLogin({ code: "code", state: "state" });

  Assert.ok(setScopedKeys.notCalled);
  Assert.ok(updateUserAccountData.calledOnce);
  Assert.deepEqual(
    JSON.parse(updateUserAccountData.firstCall.args[0].requestedServices),
    null
  );
});

add_test(function test_helpers_open_sync_preferences() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {},
  });

  let mockBrowser = {
    loadURI(uri) {
      Assert.equal(
        uri.spec,
        "about:preferences?entrypoint=fxa%3Averification_complete#sync"
      );
      run_next_test();
    },
  };

  helpers.openSyncPreferences(mockBrowser, "fxa:verification_complete");
});

add_task(async function test_helpers_getFxAStatus_engines_oauth() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        getUserAccountData() {
          return Promise.resolve({
            email: "testuser@testuser.com",
            sessionToken: "sessionToken",
            uid: "uid",
            verified: true,
          });
        },
      },
    },
    privateBrowsingUtils: {
      isBrowserPrivate: () => true,
    },
  });

  // disable the "addresses" engine.
  Services.prefs.setBoolPref("services.sync.engine.addresses.available", false);
  let fxaStatus = await helpers.getFxaStatus("sync", mockSendingContext);
  ok(!!fxaStatus);
  ok(!!fxaStatus.signedInUser);
  // in the oauth flows we expect all engines.
  deepEqual(fxaStatus.capabilities.engines.toSorted(), [
    "addons",
    "bookmarks",
    "creditcards",
    "history",
    "passwords",
    "prefs",
    "tabs",
  ]);

  // try again with addresses enabled.
  Services.prefs.setBoolPref("services.sync.engine.addresses.available", true);
  fxaStatus = await helpers.getFxaStatus("sync", mockSendingContext);
  deepEqual(fxaStatus.capabilities.engines.toSorted(), [
    "addons",
    "addresses",
    "bookmarks",
    "creditcards",
    "history",
    "passwords",
    "prefs",
    "tabs",
  ]);
});

add_task(async function test_helpers_getFxAStatus_pairing_capabilities() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        getUserAccountData() {
          return Promise.resolve(null);
        },
      },
    },
    privateBrowsingUtils: {
      isBrowserPrivate: () => false,
    },
  });

  Services.prefs.setBoolPref(PREF_PAIRING_ENABLED, true);
  Services.prefs.setIntPref(PREF_PAIRING_VERSION, 2);

  let { capabilities } = await helpers.getFxaStatus("sync", mockSendingContext);
  Assert.strictEqual(capabilities.pairing, true, "pairing is enabled");
  Assert.strictEqual(capabilities.pairingVersion, 2, "reports version 2");

  Services.prefs.setIntPref(PREF_PAIRING_VERSION, 1);
  ({ capabilities } = await helpers.getFxaStatus("sync", mockSendingContext));
  Assert.strictEqual(capabilities.pairing, true, "pairing is still enabled");
  Assert.strictEqual(capabilities.pairingVersion, 1, "reports version 1");

  Services.prefs.setBoolPref(PREF_PAIRING_ENABLED, false);
  ({ capabilities } = await helpers.getFxaStatus("sync", mockSendingContext));
  Assert.strictEqual(capabilities.pairing, false, "pairing is disabled");
  Assert.strictEqual(
    capabilities.pairingVersion,
    1,
    "reports the version even when pairing is disabled"
  );

  Services.prefs.clearUserPref(PREF_PAIRING_ENABLED);
  Services.prefs.clearUserPref(PREF_PAIRING_VERSION);
});

add_task(async function test_helpers_getFxaStatus_allowed_signedInUser() {
  let wasCalled = {
    getUserAccountData: false,
    shouldAllowFxaStatus: false,
  };

  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        getUserAccountData() {
          wasCalled.getUserAccountData = true;
          return Promise.resolve({
            email: "testuser@testuser.com",
            sessionToken: "sessionToken",
            uid: "uid",
            verified: true,
          });
        },
      },
    },
  });

  helpers.shouldAllowFxaStatus = (service, sendingContext) => {
    wasCalled.shouldAllowFxaStatus = true;
    Assert.equal(service, "sync");
    Assert.equal(sendingContext, mockSendingContext);

    return true;
  };

  return helpers.getFxaStatus("sync", mockSendingContext).then(fxaStatus => {
    Assert.ok(!!fxaStatus);
    Assert.ok(wasCalled.getUserAccountData);
    Assert.ok(wasCalled.shouldAllowFxaStatus);

    Assert.ok(!!fxaStatus.signedInUser);
    let { signedInUser } = fxaStatus;

    Assert.equal(signedInUser.email, "testuser@testuser.com");
    Assert.equal(signedInUser.sessionToken, "sessionToken");
    Assert.equal(signedInUser.uid, "uid");
    Assert.ok(signedInUser.verified);

    // These properties are filtered and should not
    // be returned to the requester.
    Assert.equal(false, "scopedKeys" in signedInUser);
  });
});

add_task(async function test_helpers_getFxaStatus_allowed_no_signedInUser() {
  let wasCalled = {
    getUserAccountData: false,
    shouldAllowFxaStatus: false,
  };

  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        getUserAccountData() {
          wasCalled.getUserAccountData = true;
          return Promise.resolve(null);
        },
      },
    },
  });

  helpers.shouldAllowFxaStatus = (service, sendingContext) => {
    wasCalled.shouldAllowFxaStatus = true;
    Assert.equal(service, "sync");
    Assert.equal(sendingContext, mockSendingContext);

    return true;
  };

  return helpers.getFxaStatus("sync", mockSendingContext).then(fxaStatus => {
    Assert.ok(!!fxaStatus);
    Assert.ok(wasCalled.getUserAccountData);
    Assert.ok(wasCalled.shouldAllowFxaStatus);

    Assert.equal(null, fxaStatus.signedInUser);
  });
});

add_task(async function test_helpers_getFxaStatus_not_allowed() {
  let wasCalled = {
    getUserAccountData: false,
    shouldAllowFxaStatus: false,
  };

  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        getUserAccountData() {
          wasCalled.getUserAccountData = true;
          return Promise.resolve(null);
        },
      },
    },
  });

  helpers.shouldAllowFxaStatus = (
    service,
    sendingContext,
    isPairing,
    context
  ) => {
    wasCalled.shouldAllowFxaStatus = true;
    Assert.equal(service, "sync");
    Assert.equal(sendingContext, mockSendingContext);
    Assert.ok(!isPairing);
    Assert.equal(context, "fx_desktop_v3");

    return false;
  };

  return helpers
    .getFxaStatus("sync", mockSendingContext, false, "fx_desktop_v3")
    .then(fxaStatus => {
      Assert.ok(!!fxaStatus);
      Assert.ok(!wasCalled.getUserAccountData);
      Assert.ok(wasCalled.shouldAllowFxaStatus);

      Assert.equal(null, fxaStatus.signedInUser);
    });
});

add_task(
  async function test_helpers_shouldAllowFxaStatus_sync_service_not_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return false;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "sync",
      mockSendingContext,
      false
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_desktop_context_not_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return false;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "",
      mockSendingContext,
      false,
      "fx_desktop_v3"
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_oauth_service_not_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return false;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "dcdb5ae7add825d2",
      mockSendingContext,
      false
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_no_service_not_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return false;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "",
      mockSendingContext,
      false
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_sync_service_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return true;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "sync",
      mockSendingContext,
      false
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_oauth_service_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return true;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "dcdb5ae7add825d2",
      mockSendingContext,
      false
    );
    Assert.ok(!shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_oauth_service_pairing_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return true;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "dcdb5ae7add825d2",
      mockSendingContext,
      true
    );
    Assert.ok(shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(
  async function test_helpers_shouldAllowFxaStatus_no_service_private_browsing() {
    let wasCalled = {
      isPrivateBrowsingMode: false,
    };
    let helpers = new FxAccountsWebChannelHelpers({});

    helpers.isPrivateBrowsingMode = sendingContext => {
      wasCalled.isPrivateBrowsingMode = true;
      Assert.equal(sendingContext, mockSendingContext);
      return true;
    };

    let shouldAllowFxaStatus = helpers.shouldAllowFxaStatus(
      "",
      mockSendingContext,
      false
    );
    Assert.ok(!shouldAllowFxaStatus);
    Assert.ok(wasCalled.isPrivateBrowsingMode);
  }
);

add_task(async function test_helpers_isPrivateBrowsingMode_private_browsing() {
  let wasCalled = {
    isBrowserPrivate: false,
  };
  let helpers = new FxAccountsWebChannelHelpers({
    privateBrowsingUtils: {
      isBrowserPrivate(browser) {
        wasCalled.isBrowserPrivate = true;
        Assert.equal(
          browser,
          mockSendingContext.browsingContext.top.embedderElement
        );
        return true;
      },
    },
  });

  let isPrivateBrowsingMode = helpers.isPrivateBrowsingMode(mockSendingContext);
  Assert.ok(isPrivateBrowsingMode);
  Assert.ok(wasCalled.isBrowserPrivate);
});

add_task(async function test_helpers_isPrivateBrowsingMode_private_browsing() {
  let wasCalled = {
    isBrowserPrivate: false,
  };
  let helpers = new FxAccountsWebChannelHelpers({
    privateBrowsingUtils: {
      isBrowserPrivate(browser) {
        wasCalled.isBrowserPrivate = true;
        Assert.equal(
          browser,
          mockSendingContext.browsingContext.top.embedderElement
        );
        return false;
      },
    },
  });

  let isPrivateBrowsingMode = helpers.isPrivateBrowsingMode(mockSendingContext);
  Assert.ok(!isPrivateBrowsingMode);
  Assert.ok(wasCalled.isBrowserPrivate);
});

add_task(async function test_helpers_change_password() {
  let wasCalled = {
    updateUserAccountData: false,
    updateDeviceRegistration: false,
  };
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        updateUserAccountData(credentials) {
          return new Promise(resolve => {
            Assert.ok(credentials.hasOwnProperty("email"));
            Assert.ok(credentials.hasOwnProperty("uid"));
            Assert.ok(credentials.hasOwnProperty("unwrapBKey"));
            Assert.ok(credentials.hasOwnProperty("device"));
            Assert.equal(null, credentials.device);
            Assert.equal(null, credentials.encryptedSendTabKeys);
            // "foo" isn't a field known by storage, so should be dropped.
            Assert.ok(!credentials.hasOwnProperty("foo"));
            wasCalled.updateUserAccountData = true;

            resolve();
          });
        },

        updateDeviceRegistration() {
          Assert.equal(arguments.length, 0);
          wasCalled.updateDeviceRegistration = true;
          return Promise.resolve();
        },
      },
    },
  });
  await helpers.changePassword({
    email: "email",
    uid: "uid",
    unwrapBKey: "unwrapBKey",
    foo: "foo",
  });
  Assert.ok(wasCalled.updateUserAccountData);
  Assert.ok(wasCalled.updateDeviceRegistration);
});

add_task(async function test_helpers_change_password_with_error() {
  let wasCalled = {
    updateUserAccountData: false,
    updateDeviceRegistration: false,
  };
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        updateUserAccountData() {
          wasCalled.updateUserAccountData = true;
          return Promise.reject();
        },

        updateDeviceRegistration() {
          wasCalled.updateDeviceRegistration = true;
          return Promise.resolve();
        },
      },
    },
  });
  try {
    await helpers.changePassword({});
    Assert.equal(false, "changePassword should have rejected");
  } catch (_) {
    Assert.ok(wasCalled.updateUserAccountData);
    Assert.ok(!wasCalled.updateDeviceRegistration);
  }
});

add_test(function test_oauth_flow_is_active() {
  let mockMessage = {
    command: "fxaccounts:oauth_flow_is_active",
    messageId: "12345",
    data: {},
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  channel._channel = {
    send(response, context) {
      Assert.equal(response.command, "fxaccounts:oauth_flow_is_active");
      Assert.equal(response.messageId, "12345");
      Assert.equal(response.data.isActive, false);
      Assert.equal(context, mockSendingContext);
      run_next_test();
    },
  };

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_test(function test_oauth_flow_begin() {
  let mockMessage = {
    command: "fxaccounts:oauth_flow_begin",
    messageId: "12347",
    data: {
      scopes: ["profile", "https://identity.mozilla.com/apps/oldsync"],
    },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  channel._channel = {
    send(response, context) {
      Assert.equal(response.command, "fxaccounts:oauth_flow_begin");
      Assert.equal(response.messageId, "12347");
      Assert.ok(response.data);

      Assert.equal(response.data.action, "email");
      Assert.equal(response.data.response_type, "code");
      Assert.equal(response.data.access_type, "offline");
      Assert.equal(
        response.data.scope,
        "profile https://identity.mozilla.com/apps/oldsync"
      );
      Assert.ok(response.data.client_id);
      Assert.ok(response.data.state);

      Assert.equal(context, mockSendingContext);

      // Now verify that a flow is in progress
      let inProgressMessage = {
        command: "fxaccounts:oauth_flow_is_active",
        messageId: "12348",
        data: {},
      };

      channel._channel = {
        send(inProgressResponse, inProgressContext) {
          Assert.equal(
            inProgressResponse.command,
            "fxaccounts:oauth_flow_is_active"
          );
          Assert.equal(inProgressResponse.messageId, "12348");
          Assert.equal(inProgressResponse.data.isActive, true);
          Assert.equal(inProgressContext, mockSendingContext);
          run_next_test();
        },
      };

      channel._channelCallback(
        WEBCHANNEL_ID,
        inProgressMessage,
        mockSendingContext
      );
    },
  };

  channel._channelCallback(WEBCHANNEL_ID, mockMessage, mockSendingContext);
});

add_task(async function test_pair_oauth_start() {
  Services.prefs.setBoolPref(PREF_PAIRING_ENABLED, true);
  Services.prefs.setIntPref(PREF_PAIRING_VERSION, 2);

  let response = await sendWebChannelCommand(
    "fxaccounts:pair_oauth_start",
    "12349"
  );

  Assert.equal(response.command, "fxaccounts:pair_oauth_start");
  Assert.equal(response.messageId, "12349");
  Assert.equal(
    response.data.scope,
    "https://identity.mozilla.com/apps/oldsync profile",
    "Defaults to the Sync scopes"
  );
  Assert.ok(response.data.state);
  Assert.ok(response.data.code_challenge);
  Assert.equal(response.data.code_challenge_method, "S256");
  Assert.ok(response.data.keys_jwk);
  Assert.deepEqual(
    Object.keys(response.data).sort(),
    ["code_challenge", "code_challenge_method", "keys_jwk", "scope", "state"],
    "Only the params FxA needs are exposed"
  );

  Services.prefs.clearUserPref(PREF_PAIRING_ENABLED);
  Services.prefs.clearUserPref(PREF_PAIRING_VERSION);
});

add_task(async function test_pair_oauth_start_disabled() {
  await assertPairingCommandDisabled("fxaccounts:pair_oauth_start", "12350");
});

add_task(async function test_pair_oauth_finish_disabled() {
  await assertPairingCommandDisabled("fxaccounts:pair_oauth_finish", "12351");
});

add_task(async function test_helpers_pair_oauth_finish() {
  let authorizeParams;
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        async authorizeOAuthCode(options) {
          authorizeParams = options;
          return { code: "thecode", state: options.state };
        },
      },
    },
  });

  let result = await helpers.pairOAuthFinish({
    client_id: "client_id",
    state: "thestate",
    scope: "profile",
    code_challenge: "challenge",
  });

  Assert.deepEqual(result, { code: "thecode", state: "thestate" });
  Assert.equal(authorizeParams.client_id, "client_id");
  Assert.equal(authorizeParams.scope, "profile");
  Assert.equal(authorizeParams.access_type, "offline");
  Assert.equal(
    authorizeParams.code_challenge_method,
    "S256",
    "Defaults to the method used by pairOAuthStart"
  );
});

add_task(async function test_helpers_pair_oauth_finish_state_mismatch() {
  let helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        async authorizeOAuthCode() {
          return { code: "thecode", state: "someotherstate" };
        },
      },
    },
  });

  await Assert.rejects(
    helpers.pairOAuthFinish({
      client_id: "client_id",
      state: "thestate",
      scope: "profile",
      code_challenge: "challenge",
      code_challenge_method: "S256",
    }),
    /OAuth state mismatch/
  );
});

async function sendWebChannelCommand(command, messageId, data = {}) {
  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });

  let promiseSend = new Promise(resolve => {
    channel._channel = { send: response => resolve(response) };
  });

  channel._channelCallback(
    WEBCHANNEL_ID,
    { command, messageId, data },
    mockSendingContext
  );
  return promiseSend;
}

// The OAuth pairing commands require both that pairing is enabled and that the
// pairing version is at least 2, so check that each is enforced separately.
async function assertPairingCommandDisabled(command, messageId) {
  Services.prefs.setBoolPref(PREF_PAIRING_ENABLED, false);
  Services.prefs.setIntPref(PREF_PAIRING_VERSION, 2);

  let response = await sendWebChannelCommand(command, messageId);
  Assert.ok(
    response.data.error.message.includes("Pairing is disabled"),
    "Should report an error rather than silently hanging when pairing is disabled"
  );

  Services.prefs.setBoolPref(PREF_PAIRING_ENABLED, true);
  Services.prefs.setIntPref(PREF_PAIRING_VERSION, 1);

  response = await sendWebChannelCommand(command, messageId);
  Assert.ok(
    response.data.error.message.includes("Pairing is disabled"),
    "Should report an error when the pairing version is older than 2"
  );

  Services.prefs.clearUserPref(PREF_PAIRING_ENABLED);
  Services.prefs.clearUserPref(PREF_PAIRING_VERSION);
}

function makeObserver(aObserveTopic, aObserveFunc) {
  let callback = function (aSubject, aTopic, aData) {
    log.debug("observed " + aTopic + " " + aData);
    if (aTopic == aObserveTopic) {
      removeMe();
      aObserveFunc(aSubject, aTopic, aData);
    }
  };

  function removeMe() {
    log.debug("removing observer for " + aObserveTopic);
    Services.obs.removeObserver(callback, aObserveTopic);
  }

  Services.obs.addObserver(callback, aObserveTopic);
  return removeMe;
}

function validationHelper(params, expected) {
  try {
    new FxAccountsWebChannel(params);
  } catch (e) {
    if (typeof expected === "string") {
      return Assert.equal(e.toString(), expected);
    }
    return Assert.ok(e.toString().match(expected));
  }
  throw new Error("Validation helper error");
}
