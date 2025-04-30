/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {
  CLIENT_IS_THUNDERBIRD,
  ON_PROFILE_CHANGE_NOTIFICATION,
  WEBCHANNEL_ID,
  log,
} = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);
const { CryptoUtils } = ChromeUtils.importESModule(
  "resource://services-crypto/utils.sys.mjs"
);
const { FxAccountsWebChannel, FxAccountsWebChannelHelpers } =
  ChromeUtils.importESModule(
    "resource://gre/modules/FxAccountsWebChannel.sys.mjs"
  );

const URL_STRING = "https://example.com";

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
    data: { email: "testuser@testuser.com" },
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
    data: { email: "testuser@testuser.com" },
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
    helpers: {
      shouldAllowRelink(email) {
        Assert.equal(email, "testuser@testuser.com");
        run_next_test();
      },
      promptProfileSyncWarningIfNeeded(acctName) {
        Assert.equal(acctName, "testuser@testuser.com");
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

add_test(function test_respond_to_device_commands() {
  let mockMessageLoggedOut = {
    command: "fxaccounts:logout",
    messageId: 123,
    data: {},
  };
  let mockMessageLoggedIn = {
    command: "fxaccounts:login",
    messageId: 123,
    data: {},
  };

  let channel = new FxAccountsWebChannel({
    channel_id: WEBCHANNEL_ID,
    content_uri: URL_STRING,
  });
  channel._channel = {
    send(response) {
      Assert.ok(!!response.data);
      Assert.equal(response.data.ok, true);

      run_next_test();
    },
  };

  channel._channelCallback(
    WEBCHANNEL_ID,
    mockMessageLoggedOut,
    mockSendingContext
  );

  channel._channelCallback(
    WEBCHANNEL_ID,
    mockMessageLoggedIn,
    mockSendingContext
  );
});

add_test(function test_respond_to_incorrect_device_commands() {
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

add_test(function test_helpers_should_allow_relink_same_email() {
  let helpers = new FxAccountsWebChannelHelpers();

  helpers.setPreviousAccountNameHashPref("testuser@testuser.com");
  Assert.ok(helpers.shouldAllowRelink("testuser@testuser.com"));

  run_next_test();
});

add_test(function test_helpers_should_allow_relink_different_email() {
  let helpers = new FxAccountsWebChannelHelpers();

  helpers.setPreviousAccountNameHashPref("testuser@testuser.com");

  helpers._promptForRelink = acctName => {
    return acctName === "allowed_to_relink@testuser.com";
  };

  Assert.ok(helpers.shouldAllowRelink("allowed_to_relink@testuser.com"));
  Assert.ok(!helpers.shouldAllowRelink("not_allowed_to_relink@testuser.com"));

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
  helpers.setPreviousAccountNameHashPref("lastuser@testuser.com");

  await helpers.login({
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
});

add_task(async function test_helpers_login_set_previous_account_name_hash() {
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
              helpers.getPreviousAccountNameHashPref(),
              CryptoUtils.sha256Base64("newuser@testuser.com")
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
  helpers.setPreviousAccountNameHashPref("lastuser@testuser.com");

  await helpers.login({
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
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: false,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
  Assert.ok(helpers._disconnect.called);
});

add_task(
  async function test_helpers_login_dont_set_previous_account_name_hash_for_unverified_emails() {
    let helpers = new FxAccountsWebChannelHelpers({
      fxAccounts: {
        _internal: {
          setSignedInUser() {
            return new Promise(resolve => {
              // previously signed in user preference should not be updated.
              Assert.equal(
                helpers.getPreviousAccountNameHashPref(),
                CryptoUtils.sha256Base64("lastuser@testuser.com")
              );
              resolve();
            });
          },
        },
        getSignedInUser() {
          return Promise.resolve(null);
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
    helpers.setPreviousAccountNameHashPref("lastuser@testuser.com");

    await helpers.login({
      email: "newuser@testuser.com",
      verifiedCanLinkAccount: true,
      customizeSync: false,
    });
  }
);

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
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: true,
  });
  Assert.ok(
    helpers._fxAccounts.telemetry.recordConnection.calledWith([], "webchannel")
  );
});

add_task(
  { skip_if: () => CLIENT_IS_THUNDERBIRD },
  async function test_helpers_login_with_customize_sync_and_declined_engines() {
    ensureOauthNotConfigured();
    let configured = false;
    let helpers = new FxAccountsWebChannelHelpers({
      fxAccounts: {
        _internal: {
          setSignedInUser(accountData) {
            return new Promise(resolve => {
              // ensure fxAccounts is informed of the new user being signed in.
              Assert.equal(accountData.email, "testuser@testuser.com");

              // customizeSync should be stripped in the data.
              Assert.equal(false, "customizeSync" in accountData);
              Assert.equal(false, "services" in accountData);
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
            configure() {
              configured = true;
            },
          },
        },
      },
    });

    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.addons"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.bookmarks"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.history"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.passwords"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.prefs"),
      true
    );
    Assert.equal(Services.prefs.getBoolPref("services.sync.engine.tabs"), true);
    await helpers.login({
      email: "testuser@testuser.com",
      verifiedCanLinkAccount: true,
      customizeSync: true,
      services: {
        sync: {
          offeredEngines: [
            "addons",
            "bookmarks",
            "history",
            "passwords",
            "prefs",
          ],
          declinedEngines: ["addons", "prefs"],
        },
      },
    });
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.addons"),
      false
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.bookmarks"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.history"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.passwords"),
      true
    );
    Assert.equal(
      Services.prefs.getBoolPref("services.sync.engine.prefs"),
      false
    );
    Assert.equal(Services.prefs.getBoolPref("services.sync.engine.tabs"), true);
    Assert.ok(configured, "sync was configured");
    Assert.ok(
      helpers._fxAccounts.telemetry.recordConnection.calledWith(
        ["sync"],
        "webchannel"
      )
    );
  }
);

add_task(async function test_helpers_login_with_offered_sync_engines() {
  let helpers;
  let configured = false;
  const setSignedInUserCalled = new Promise(resolve => {
    helpers = new FxAccountsWebChannelHelpers({
      fxAccounts: {
        _internal: {
          async setSignedInUser(accountData) {
            resolve(accountData);
          },
        },
        getSignedInUser() {
          return Promise.resolve(null);
        },
        telemetry: {
          recordConnection() {},
        },
      },
      weaveXPCOM: {
        whenLoaded() {},
        Weave: {
          Service: {
            configure() {
              configured = true;
            },
          },
        },
      },
    });
  });

  Services.prefs.setBoolPref("services.sync.engine.creditcards", false);
  Services.prefs.setBoolPref("services.sync.engine.addresses", false);

  await helpers.login({
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    customizeSync: true,
    services: {
      sync: {
        declinedEngines: ["addresses"],
        offeredEngines: ["creditcards", "addresses"],
      },
    },
  });

  const accountData = await setSignedInUserCalled;

  // ensure fxAccounts is informed of the new user being signed in.
  equal(accountData.email, "testuser@testuser.com");

  // services should be stripped in the data.
  ok(!("services" in accountData));
  // credit cards was offered but not declined.
  equal(Services.prefs.getBoolPref("services.sync.engine.creditcards"), true);
  // addresses was offered and explicitely declined.
  equal(Services.prefs.getBoolPref("services.sync.engine.addresses"), false);
  ok(configured);
});

add_task(async function test_helpers_login_nothing_offered() {
  let helpers;
  let configured = false;
  const setSignedInUserCalled = new Promise(resolve => {
    helpers = new FxAccountsWebChannelHelpers({
      fxAccounts: {
        _internal: {
          async setSignedInUser(accountData) {
            resolve(accountData);
          },
        },
        getSignedInUser() {
          return Promise.resolve(null);
        },
        telemetry: {
          recordConnection() {},
        },
      },
      weaveXPCOM: {
        whenLoaded() {},
        Weave: {
          Service: {
            configure() {
              configured = true;
            },
          },
        },
      },
    });
  });

  // doesn't really matter if it's *all* engines...
  const allEngines = [
    "addons",
    "addresses",
    "bookmarks",
    "creditcards",
    "history",
    "passwords",
    "prefs",
  ];
  for (let name of allEngines) {
    Services.prefs.clearUserPref("services.sync.engine." + name);
  }

  await helpers.login({
    email: "testuser@testuser.com",
    verifiedCanLinkAccount: true,
    services: {
      sync: {},
    },
  });

  const accountData = await setSignedInUserCalled;
  // ensure fxAccounts is informed of the new user being signed in.
  equal(accountData.email, "testuser@testuser.com");

  for (let name of allEngines) {
    Assert.ok(!Services.prefs.prefHasUserValue("services.sync.engine." + name));
  }
  Assert.ok(configured);
});

add_task(async function test_helpers_persist_requested_services() {
  ensureOauthConfigured();
  let accountData = null;
  const helpers = new FxAccountsWebChannelHelpers({
    fxAccounts: {
      _internal: {
        async setSignedInUser(newAccountData) {
          accountData = newAccountData;
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

  resetOauthConfig();
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

add_task(async function test_helpers_getFxAStatus_extra_engines() {
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

  ensureOauthNotConfigured();
  Services.prefs.setBoolPref(
    "services.sync.engine.creditcards.available",
    true
  );
  // Not defining "services.sync.engine.addresses.available" on purpose.

  let fxaStatus = await helpers.getFxaStatus("sync", mockSendingContext);
  ok(!!fxaStatus);
  ok(!!fxaStatus.signedInUser);
  // in the non-oauth flows we only expect "extra" engines.
  deepEqual(fxaStatus.capabilities.engines, ["creditcards"]);
  resetOauthConfig();
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
  ensureOauthConfigured();
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

  resetOauthConfig();
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
