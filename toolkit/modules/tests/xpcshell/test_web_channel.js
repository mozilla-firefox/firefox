/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { WebChannel } = ChromeUtils.importESModule(
  "resource://gre/modules/WebChannel.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);

const ERROR_ID_ORIGIN_REQUIRED =
  "WebChannel id and originOrPermission are required.";
const VALID_WEB_CHANNEL_ID = "id";
const URL_STRING = "http://example.com";
const VALID_WEB_CHANNEL_ORIGIN = Services.io.newURI(URL_STRING);
const TEST_PERMISSION_NAME = "test-webchannel-permissions";
// The permission branch of the origin check only accepts https origins.
const SECURE_WEB_CHANNEL_ORIGIN = Services.io.newURI("https://example.com");
const TEST_USER_CONTEXT_ID = 1;

// Origin attribute sets that all describe the same site as
// SECURE_WEB_CHANNEL_ORIGIN with no origin attributes at all.
const ISOLATED_ORIGIN_ATTRIBUTES = [
  ["a container", { userContextId: TEST_USER_CONTEXT_ID }],
  ["private browsing", { privateBrowsingId: 1 }],
];

function securePrincipalFor(originAttributes) {
  return Services.scriptSecurityManager.createContentPrincipal(
    SECURE_WEB_CHANNEL_ORIGIN,
    originAttributes
  );
}

var MockWebChannelBroker = {
  _channelMap: new Map(),
  registerChannel(channel) {
    if (!this._channelMap.has(channel)) {
      this._channelMap.set(channel);
    }
  },
  unregisterChannel(channelToRemove) {
    this._channelMap.delete(channelToRemove);
  },
};

/**
 * Web channel tests
 */

/**
 * Test channel listening with originOrPermission being an nsIURI.
 */
add_task(function test_web_channel_listen() {
  return new Promise(resolve => {
    let channel = new WebChannel(
      VALID_WEB_CHANNEL_ID,
      VALID_WEB_CHANNEL_ORIGIN,
      {
        broker: MockWebChannelBroker,
      }
    );
    let delivered = 0;
    Assert.equal(channel.id, VALID_WEB_CHANNEL_ID);
    Assert.equal(
      channel._originOrPermission.spec,
      VALID_WEB_CHANNEL_ORIGIN.spec
    );
    Assert.equal(channel._deliverCallback, null);

    channel.listen(function (id, message, target) {
      Assert.equal(id, VALID_WEB_CHANNEL_ID);
      Assert.ok(message);
      Assert.ok(message.command);
      Assert.ok(target.sender);
      delivered++;
      // 2 messages should be delivered
      if (delivered === 2) {
        channel.stopListening();
        Assert.equal(channel._deliverCallback, null);
        resolve();
      }
    });

    // send two messages
    channel.deliver(
      {
        id: VALID_WEB_CHANNEL_ID,
        message: {
          command: "one",
        },
      },
      { sender: true }
    );

    channel.deliver(
      {
        id: VALID_WEB_CHANNEL_ID,
        message: {
          command: "two",
        },
      },
      { sender: true }
    );
  });
});

/**
 * Test channel listening with originOrPermission being a permission string.
 */
add_task(function test_web_channel_listen_permission() {
  return new Promise(resolve => {
    // add a new permission
    PermissionTestUtils.add(
      VALID_WEB_CHANNEL_ORIGIN,
      TEST_PERMISSION_NAME,
      Services.perms.ALLOW_ACTION
    );
    registerCleanupFunction(() =>
      PermissionTestUtils.remove(VALID_WEB_CHANNEL_ORIGIN, TEST_PERMISSION_NAME)
    );
    let channel = new WebChannel(VALID_WEB_CHANNEL_ID, TEST_PERMISSION_NAME, {
      broker: MockWebChannelBroker,
    });
    let delivered = 0;
    Assert.equal(channel.id, VALID_WEB_CHANNEL_ID);
    Assert.equal(channel._originOrPermission, TEST_PERMISSION_NAME);
    Assert.equal(channel._deliverCallback, null);

    channel.listen(function (id, message, target) {
      Assert.equal(id, VALID_WEB_CHANNEL_ID);
      Assert.ok(message);
      Assert.ok(message.command);
      Assert.ok(target.sender);
      delivered++;
      // 2 messages should be delivered
      if (delivered === 2) {
        channel.stopListening();
        Assert.equal(channel._deliverCallback, null);
        resolve();
      }
    });

    // send two messages
    channel.deliver(
      {
        id: VALID_WEB_CHANNEL_ID,
        message: {
          command: "one",
        },
      },
      { sender: true }
    );

    channel.deliver(
      {
        id: VALID_WEB_CHANNEL_ID,
        message: {
          command: "two",
        },
      },
      { sender: true }
    );
  });
});

/**
 * A channel restricted by a URI accepts that site whatever its origin
 * attributes, because a container or a private browsing window is not a
 * different site.
 */
add_task(function test_web_channel_origin_ignores_origin_attributes() {
  let channel = new WebChannel(VALID_WEB_CHANNEL_ID, SECURE_WEB_CHANNEL_ORIGIN);

  Assert.ok(
    channel._originCheckCallback(securePrincipalFor({})),
    "A channel restricted by URI accepts the default container"
  );

  for (let [name, originAttributes] of ISOLATED_ORIGIN_ATTRIBUTES) {
    Assert.notEqual(
      securePrincipalFor({}).origin,
      securePrincipalFor(originAttributes).origin,
      `The principal of ${name} is a distinct principal`
    );
    Assert.equal(
      securePrincipalFor({}).originNoSuffix,
      securePrincipalFor(originAttributes).originNoSuffix,
      `The principal origin without suffix of ${name} is the same`
    );
    Assert.ok(
      channel._originCheckCallback(securePrincipalFor(originAttributes)),
      `A channel restricted by URI accepts ${name}`
    );
  }
});

/**
 * A channel restricted by permission accepts the site the permission was
 * granted to whatever its origin attributes, the same way a channel restricted
 * by a URI does.
 */
add_task(
  { pref_set: [["permissions.isolateBy.userContext", true]] },
  function test_web_channel_permission_ignores_origin_attributes() {
    PermissionTestUtils.add(
      SECURE_WEB_CHANNEL_ORIGIN,
      TEST_PERMISSION_NAME,
      Services.perms.ALLOW_ACTION
    );
    registerCleanupFunction(() =>
      PermissionTestUtils.remove(
        SECURE_WEB_CHANNEL_ORIGIN,
        TEST_PERMISSION_NAME
      )
    );

    let channel = new WebChannel(VALID_WEB_CHANNEL_ID, TEST_PERMISSION_NAME);

    // A failing assertion aborts the whole file, so collect every case and
    // compare in one go to report all of them that are rejected.
    let accepted = {};
    let expected = {};
    for (let [name, originAttributes] of [
      ["the default container", {}],
      ...ISOLATED_ORIGIN_ATTRIBUTES,
    ]) {
      let key = `a channel restricted by permission accepts ${name}`;
      accepted[key] = channel._originCheckCallback(
        securePrincipalFor(originAttributes)
      );
      expected[key] = true;
    }

    Assert.deepEqual(
      accepted,
      expected,
      "The permission is granted to a site, not to a set of origin attributes"
    );
  }
);

/**
 * Test constructor
 */
add_test(function test_web_channel_constructor() {
  Assert.equal(constructorTester(), ERROR_ID_ORIGIN_REQUIRED);
  Assert.equal(constructorTester(undefined), ERROR_ID_ORIGIN_REQUIRED);
  Assert.equal(
    constructorTester(undefined, VALID_WEB_CHANNEL_ORIGIN),
    ERROR_ID_ORIGIN_REQUIRED
  );
  Assert.equal(
    constructorTester(VALID_WEB_CHANNEL_ID, undefined),
    ERROR_ID_ORIGIN_REQUIRED
  );
  Assert.ok(!constructorTester(VALID_WEB_CHANNEL_ID, VALID_WEB_CHANNEL_ORIGIN));

  run_next_test();
});

function constructorTester(id, origin) {
  try {
    new WebChannel(id, origin);
  } catch (e) {
    return e.message;
  }
  return false;
}
