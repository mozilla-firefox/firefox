/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

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

  Services.fog.initializeFOG();
  // Cheeky way to await the end of init:
  Glean.fog.initializations.testGetValue();

  // Storing the backup id is asynchronous, so we must wait.
  await TestUtils.waitForCondition(() => {
    return Services.prefs.getStringPref("telemetry.fog.backup.client_id") != "";
  }, "Init backs up a value.");
});

add_task(async function test_data_optout_clears_backup() {
  const backupClientId = Services.prefs.getStringPref(
    "telemetry.fog.backup.client_id"
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
  Assert.equal(
    backupClientId,
    ping.client_info.client_id,
    "The 'deletion-request' ping must be for the expected client ID."
  );

  // Clearing the backup id is asynchronous, so we must wait.
  await TestUtils.waitForCondition(() => {
    return Services.prefs.getStringPref("telemetry.fog.backup.client_id") == "";
  }, "Opt out must clear the backup.");
});
