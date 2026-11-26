/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { WebChannel } = ChromeUtils.importESModule(
  "resource://gre/modules/WebChannel.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);

const TEST_URL_TAIL =
  "example.com/browser/browser/base/content/test/browser-general/test_remoteTroubleshoot.html";
const TEST_URI_GOOD = Services.io.newURI("https://" + TEST_URL_TAIL);
const TEST_URI_BAD = Services.io.newURI("http://" + TEST_URL_TAIL);

// Loads the specified URI in a new tab and waits for it to send us data via
// the SpecialPowers.spawn() call and resolves with that data.
async function promiseNewChannelResponse(uri, tabOptions = {}) {
  let tab = gBrowser.addTab(uri.spec, {
    inBackground: false,
    triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    ...tabOptions,
  });
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  let data = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async function () {
      return Cu.waiveXrays(content).remoteTroubleShootingResult;
    }
  );

  await gBrowser.removeTab(tab);

  return data;
}

add_task(async function () {
  // We haven't set a permission yet - so even the "good" URI should fail.
  let got = await promiseNewChannelResponse(TEST_URI_GOOD);
  // Should return an error.
  Assert.strictEqual(
    got.errno,
    2,
    "should have failed with errno 2, no such channel"
  );

  // Add a permission manager entry for our URI.
  PermissionTestUtils.add(
    TEST_URI_GOOD,
    "remote-troubleshooting",
    Services.perms.ALLOW_ACTION
  );
  registerCleanupFunction(() => {
    PermissionTestUtils.remove(TEST_URI_GOOD, "remote-troubleshooting");
  });

  // Try again - now we are expecting a response with the actual data.
  got = await promiseNewChannelResponse(TEST_URI_GOOD);

  // Check some keys we expect to always get.
  Assert.ok(got.addons, "should have addons");
  Assert.ok(got.graphics, "should have graphics");

  // Check we have channel and build ID info:
  Assert.equal(
    got.application.buildID,
    Services.appinfo.appBuildID,
    "should have correct build ID"
  );

  let updateChannel = null;
  try {
    updateChannel = ChromeUtils.importESModule(
      "resource://gre/modules/UpdateUtils.sys.mjs"
    ).UpdateUtils.UpdateChannel;
  } catch (ex) {}
  if (!updateChannel) {
    Assert.ok(
      !("updateChannel" in got.application),
      "should not have update channel where not available."
    );
  } else {
    Assert.equal(
      got.application.updateChannel,
      updateChannel,
      "should have correct update channel."
    );
  }

  // And check some keys we know we decline to return.
  Assert.ok(
    !got.modifiedPreferences,
    "should not have a modifiedPreferences key"
  );
  Assert.ok(
    !got.printingPreferences,
    "should not have a printingPreferences key"
  );
  Assert.ok(!got.crashes, "should not have crash info");

  // Now a http:// URI - should receive an error
  got = await promiseNewChannelResponse(TEST_URI_BAD);
  Assert.strictEqual(
    got.errno,
    2,
    "should have failed with errno 2, no such channel"
  );
});

// Test that troubleshooting works in a container tab
add_task(async function test_container_tab() {
  await SpecialPowers.pushPrefEnv({
    set: [["permissions.isolateBy.userContext", true]],
  });

  PermissionTestUtils.add(
    TEST_URI_GOOD,
    "remote-troubleshooting",
    Services.perms.ALLOW_ACTION
  );
  registerCleanupFunction(() => {
    PermissionTestUtils.remove(TEST_URI_GOOD, "remote-troubleshooting");
  });

  let fromDefaultContainer = await promiseNewChannelResponse(TEST_URI_GOOD);
  Assert.ok(
    fromDefaultContainer.addons,
    "the default container is answered with the troubleshooting data"
  );

  let fromContainer = await promiseNewChannelResponse(TEST_URI_GOOD, {
    userContextId: 1,
  });
  Assert.strictEqual(
    fromContainer.errno,
    undefined,
    "a container is not answered with an error"
  );
  Assert.ok(
    fromContainer.addons,
    "a container is answered with the troubleshooting data"
  );
  Assert.equal(
    fromContainer.application.buildID,
    Services.appinfo.appBuildID,
    "a container is answered with the data of this build"
  );
});
