/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const { NimbusFeatures } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

NimbusTestUtils.init(this);

// The "4" is to identify it as a UUIDv4.
const VALID_BACKUP_CLIENT_ID = "00000000-0000-4000-0000-000000000000";

add_setup(async function test_setup() {
  // FOG needs a profile directory to put its data in.
  do_get_profile();

  PingServer.start();

  registerCleanupFunction(async () => {
    await PingServer.stop();
  });

  Services.prefs.setIntPref(
    "telemetry.fog.test.localhost_port",
    PingServer.port
  );

  Assert.equal(
    "",
    Services.prefs.getStringPref("telemetry.fog.backup.client_id"),
    "Ensure no one's set the pref before init."
  );

  Services.prefs.setStringPref(
    "telemetry.fog.backup.client_id",
    VALID_BACKUP_CLIENT_ID
  );

  // For now, we only conditionally enforce the use of the backup id:
  const { cleanup } = await NimbusTestUtils.setupTest();
  let nimbusCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: NimbusFeatures.gleanInternalSdk.featureId,
    value: {
      applyStoredClientID: false,
    },
  });

  // Sanity: FOG checks the value of the nimbus feature via this pref:
  Assert.equal(
    Services.prefs.getBoolPref("telemetry.fog.apply_stored_client_id"),
    false
  );

  Services.fog.initializeFOG();
  // Cheeky way to await the end of init:
  Glean.fog.initializations.testGetValue();

  // Storing the backup id is asynchronous, so we must wait.
  await TestUtils.waitForCondition(() => {
    return Services.prefs.getStringPref("telemetry.fog.backup.client_id") != "";
  }, "Init backs up a value.");

  // Tidy up.
  await nimbusCleanup();
  await cleanup();
});

add_task(async function test_data_optout_clears_backup() {
  const backupClientId = Services.prefs.getStringPref(
    "telemetry.fog.backup.client_id"
  );
  Assert.notEqual(
    backupClientId,
    VALID_BACKUP_CLIENT_ID,
    "FOG didn't overwrite the backup id"
  );
  Services.prefs.setBoolPref("datareporting.healthreport.uploadEnabled", false);

  let ping;
  while (true) {
    // Scan through pings until we find the "deletion-request" one.
    // This will both confirm that we're deleting the correct id
    // _and_ give time for the backup to clear.
    let request = await PingServer.promiseNextRequest();
    if (request.path.includes("deletion-request")) {
      ping = await decodeRequestPayload(request);
      break;
    }
  }
  Assert.notEqual(
    VALID_BACKUP_CLIENT_ID,
    ping.client_info.client_id,
    "The 'deletion-request' ping must not be for the backup client ID."
  );

  // Clearing the backup id is asynchronous, so we must wait.
  await TestUtils.waitForCondition(() => {
    return Services.prefs.getStringPref("telemetry.fog.backup.client_id") == "";
  }, "Opt out must clear the backup.");

  Assert.equal(
    0,
    Glean.fog.unknownBackupKey.testGetValue() ?? 0,
    "No problems with backup."
  );
});
