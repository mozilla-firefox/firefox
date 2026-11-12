/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// this test file can be run directly as a part of parent/main process
// or indirectly from the wrapper test file as a part of child/content process

// need to get access to helper functions/structures
// load ensures
// * testing environment is available (ie Assert.ok())
/*global inChildProcess, test_flag_priority */
load("../unit/test_http3_prio_helpers.js");

// direct call to this test file should cleanup after itself
// otherwise the wrapper will handle
if (!inChildProcess()) {
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.http3.priority");
    Services.prefs.clearUserPref("network.http.priority_header.enabled");
    http3_clear_prefs();
  });
}

// setup once, before tests
add_task(async function setup() {
  // wrapper handles when testing as content process for pref change
  if (!inChildProcess()) {
    await http3_setup_tests("h3");
  }
});

// tests various flags when priority has been enabled on variable incremental
// this function should only be called the preferences priority disabled
async function test_http3_prio_enabled(incremental) {
  await test_flag_priority("enabled (none)", null, "u=4", null, false); // default-test
  await test_flag_priority(
    "enabled (urgent_start)",
    Ci.nsIClassOfService.UrgentStart,
    "u=0",
    incremental,
    incremental
  );
  await test_flag_priority(
    "enabled (leader)",
    Ci.nsIClassOfService.Leader,
    "u=2",
    incremental,
    incremental
  );

  // if priority-urgency and incremental are both default values
  // then we shouldn't expect to see the priority header at all
  // hence when:
  //  incremental=true  -> we expect incremental
  //  incremental=false -> we expect null
  await test_flag_priority(
    "enabled (unblocked)",
    Ci.nsIClassOfService.Unblocked,
    null,
    incremental,
    incremental ? incremental : null
  );

  await test_flag_priority(
    "enabled (follower)",
    Ci.nsIClassOfService.Follower,
    "u=4",
    incremental,
    incremental
  );
  await test_flag_priority(
    "enabled (speculative)",
    Ci.nsIClassOfService.Speculative,
    "u=6",
    incremental,
    incremental
  );
  await test_flag_priority(
    "enabled (background)",
    Ci.nsIClassOfService.Background,
    "u=6",
    incremental,
    incremental
  );
  await test_flag_priority(
    "enabled (tail)",
    Ci.nsIClassOfService.Tail,
    "u=6",
    incremental,
    incremental
  );
}

// with priority enabled: test urgency flags with both incremental enabled and disabled
add_task(async function test_http3_prio_enabled_incremental_true() {
  // wrapper handles when testing as content process for pref change
  if (!inChildProcess()) {
    Services.prefs.setBoolPref("network.http.http3.priority", true);
    Services.prefs.setBoolPref("network.http.priority_header.enabled", true);
  }
  await test_http3_prio_enabled(true);
});

add_task(async function test_http3_prio_enabled_incremental_false() {
  // wrapper handles when testing as content process for pref change
  if (!inChildProcess()) {
    Services.prefs.setBoolPref("network.http.http3.priority", true);
    Services.prefs.setBoolPref("network.http.priority_header.enabled", true);
  }
  await test_http3_prio_enabled(false);
});
