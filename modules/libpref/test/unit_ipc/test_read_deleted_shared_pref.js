/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// A pref that is in the shared pref map must accept a default value again
// after it has been deleted.

const kPrefName = "test.libpref.readdDeletedSharedPref";

add_task(async function test_readd_default_after_delete() {
  const defaults = Services.prefs.getDefaultBranch("");
  defaults.setIntPref(kPrefName, 1);

  // Launching the content process builds the shared pref map.
  await new Promise(resolve => sendCommand("1;", resolve));

  defaults.deleteBranch(kPrefName);
  defaults.setIntPref(kPrefName, 2);
  Assert.equal(Services.prefs.getIntPref(kPrefName), 2);
});
