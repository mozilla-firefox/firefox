/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

add_setup(
  /* on Android FOG is set up through head.js */
  { skip_if: () => AppConstants.platform == "android" },
  function test_setup() {
    // FOG needs a profile directory to put its data in.
    do_get_profile();

    // We need to ensure we're not artificially decelerating early pings.
    Services.prefs.clearUserPref("telemetry.fog.test.decelerate_early_events");

    Assert.equal(
      "",
      Services.prefs.getStringPref("telemetry.fog.backup.client_id")
    );

    // We need to initialize it once, otherwise operations will be stuck in the pre-init queue.
    Services.fog.initializeFOG();
  }
);

add_task(function test_fog_init_works() {
  if (new Date().getHours() >= 3 && new Date().getHours() <= 4) {
    // We skip this test if it's too close to 4AM, when we might send a
    // "metrics" ping between init and this test being run.
    Assert.ok(true, "Too close to 'metrics' ping send window. Skipping test.");
    return;
  }
  const snapshot = Glean.fog.initializations.testGetValue();
  Assert.equal(snapshot.count, 1, "FOG init happened once.");
  Assert.greater(snapshot.sum, 0, "FOG init's time was measured.");
});

add_task(function test_fog_initialized_with_correct_rate_limit() {
  Assert.greater(
    Glean.fog.maxPingsPerMinute.testGetValue(),
    0,
    "FOG has been initialized with a ping rate limit of greater than 0."
  );
});

add_task(
  /* on Android we can't unset telemetry.fog.test.decelerate_early_events before init */
  { skip_if: () => AppConstants.platform == "android" },
  function test_fog_inits_with_early_event_acceleration() {
    // Record far fewer than FOG's configured max event threshold (500),
    // but more than FOG's configured accelerated early events ping (20).
    const numEvents = 100;
    for (let i = 0; i < numEvents; i++) {
      Glean.testOnly.eventPingEvent.record();
    }
    Assert.less(
      Glean.testOnly.eventPingEvent.testGetValue()?.length || 0,
      numEvents,
      "At least one 'events' ping must've been sent early."
    );
  }
);

add_task(
  /* on Android we don't backup the client_id to prefs */
  { skip_if: () => AppConstants.platform == "android" },
  function test_fog_inits_setting_backup_client_id() {
    // Ensure Glean has finished init-ing:
    Glean.fog.initializations.testGetValue();

    Assert.notEqual(
      "",
      Services.prefs.getStringPref("telemetry.fog.backup.client_id")
    );

    Assert.equal(
      0,
      Glean.fog.unknownBackupKey.testGetValue() ?? 0,
      "No problems with backup."
    );
  }
);
