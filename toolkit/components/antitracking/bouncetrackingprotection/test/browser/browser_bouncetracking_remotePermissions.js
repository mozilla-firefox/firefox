/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);

const COLLECTION_NAME = "remote-permissions";

let bounceTrackingProtection = Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection);

const TRACKER_PRINCIPAL =
  Services.scriptSecurityManager.createContentPrincipalFromOrigin(
    ORIGIN_TRACKER
  );

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
      ["privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec", 0],
    ],
  });

  let rps = Cc["@mozilla.org/remote-permission-service;1"].getService(
    Ci.nsIRemotePermissionService
  );
  await rps.isInitialized;
});

/**
 * Run a bounce test.
 *
 * @param {boolean} expectTrackerPurged - Whether the bounce tracker is expected
 * to be purged.
 */
async function runPurgeTest(expectTrackerPurged) {
  ok(!SiteDataTestUtils.hasCookies(ORIGIN_TRACKER), "No cookies initially.");

  await runTestBounce({
    bounceType: "client",
    setState: "cookie-client",
    postBounceCallback: () => {
      ok(
        SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
        "Cookie added in bounce."
      );
    },
    skipSiteDataCleanup: true,
    expectPurge: expectTrackerPurged,
  });

  is(
    SiteDataTestUtils.hasCookies(ORIGIN_TRACKER),
    !expectTrackerPurged,
    `Cookies should ${expectTrackerPurged ? "" : "not "}be purged.`
  );

  bounceTrackingProtection.clearAll();
  await SiteDataTestUtils.clear();
}

/**
 * Dispatch a RemoteSettings "sync" event for the remote permissions
 * collection.
 *
 * @param {object} data - The event's data payload.
 * @param {object[]} [data.created] - Records that were created.
 * @param {object[]} [data.deleted] - Records that were removed.
 */
async function remoteSettingsSync({ created, deleted }) {
  await RemoteSettings(COLLECTION_NAME).emit("sync", {
    data: { created, deleted },
  });
}

add_task(async function test_remote_exception_permission() {
  info(
    "Without a bounce-tracking-protection-exception permission the tracker gets purged."
  );
  await runPurgeTest(true);

  let record = {
    origin: ORIGIN_TRACKER,
    type: "bounce-tracking-protection-exception",
    capability: Ci.nsIPermissionManager.ALLOW_ACTION,
  };
  registerCleanupFunction(() => remoteSettingsSync({ deleted: [record] }));
  await remoteSettingsSync({ created: [record] });
  is(
    Services.perms.testExactPermissionFromPrincipal(
      TRACKER_PRINCIPAL,
      "bounce-tracking-protection-exception"
    ),
    Ci.nsIPermissionManager.ALLOW_ACTION,
    "bounce-tracking-protection-exception permission was imported from remote settings."
  );

  info(
    "With a bounce-tracking-protection-exception permission the tracker is not purged."
  );
  await runPurgeTest(false);

  await remoteSettingsSync({ deleted: [record] });
  is(
    Services.perms.testExactPermissionFromPrincipal(
      TRACKER_PRINCIPAL,
      "bounce-tracking-protection-exception"
    ),
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    "bounce-tracking-protection-exception permission was removed via remote settings."
  );

  info(
    "After removing the bounce-tracking-protection-exception permission the tracker is purged again."
  );
  await runPurgeTest(true);
});

add_task(async function test_remote_exception_permission_only_allow() {
  let record = {
    origin: ORIGIN_TRACKER,
    type: "bounce-tracking-protection-exception",
    capability: Ci.nsIPermissionManager.DENY_ACTION,
  };
  await remoteSettingsSync({ created: [record] });
  is(
    Services.perms.testExactPermissionFromPrincipal(
      TRACKER_PRINCIPAL,
      "bounce-tracking-protection-exception"
    ),
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    "Remote settings may only set bounce-tracking-protection-exception to ALLOW_ACTION."
  );
});
