//
// Run test script in content process instead of chrome (xpcshell's default)
//

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.http.http3.priority");
  Services.prefs.clearUserPref("network.http.priority_header.enabled");
  http3_clear_prefs();
});

// setup will be called before the child process tests
add_task(async function setup() {
  await http3_setup_tests("h3");
});

async function run_test() {
  // Create the profile before the child process starts, so the permission
  // manager doesn't open its encrypted database during setup (bug 2093299).
  do_get_profile();

  // test priority urgency and incremental with priority enabled
  Services.prefs.setBoolPref("network.http.http3.priority", true);
  Services.prefs.setBoolPref("network.http.priority_header.enabled", true);
  run_test_in_child("../unit/test_http3_prio_enabled.js");
  run_next_test(); // only pumps next async task from this file
}
