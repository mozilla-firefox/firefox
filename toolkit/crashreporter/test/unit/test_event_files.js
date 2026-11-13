/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_setup() {
  do_get_profile();
  await makeFakeAppDir();
});

add_task(async function test_main_process_crash() {
  let cm = Services.crashmanager;
  Assert.ok(cm, "CrashManager available.");

  let basename;
  let count = await new Promise((resolve, reject) => {
    do_crash(
      async function () {
        // TelemetryController.testSetup() will occasionally throw, so setup
        // everything synchronously before we call it, just in case.
        crashReporter.annotateCrashReport("ShutdownProgress", "event-test");
        crashType = CrashTestUtils.CRASH_MOZ_CRASH;

        // TelemetrySession setup will trigger the session annotation
        let { TelemetryController } = ChromeUtils.importESModule(
          "resource://gre/modules/TelemetryController.sys.mjs"
        );
        await TelemetryController.testSetup();
      },
      minidump => {
        basename = minidump.leafName;
        Object.defineProperty(cm, "_eventsDirs", { value: [getEventDir()] });
        cm.aggregateEventsFiles().then(resolve, reject);
      },
      true
    );
  });
  Assert.equal(count, 1, "A single crash event file was seen.");
  let crashes = await cm.getCrashes();
  Assert.equal(crashes.length, 1);
  let crash = crashes[0];
  Assert.ok(
    crash.isOfType(
      cm.processTypes[Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT],
      cm.CRASH_TYPE_CRASH
    )
  );
  Assert.equal(crash.id + ".dmp", basename, "ID recorded properly");
  Assert.equal(crash.metadata.ShutdownProgress, "event-test");
  Assert.ok("TelemetrySessionId" in crash.metadata);
  Assert.ok("UptimeTS" in crash.metadata);
  Assert.ok(
    /^[0-9a-f]{8}\-([0-9a-f]{4}\-){3}[0-9a-f]{12}$/.test(
      crash.metadata.TelemetrySessionId
    )
  );
  Assert.ok("CrashTime" in crash.metadata);
  Assert.ok(/^\d+$/.test(crash.metadata.CrashTime));
});
