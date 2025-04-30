/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";
add_task(async function testPrefFirstRollout() {
  await setup();
  await setupRegion();
  let defaults = Services.prefs.getDefaultBranch("");

  is(
    DoHConfigController.currentConfig.enabled,
    false,
    "Rollout should not be enabled"
  );
  setPassingHeuristics();

  let configFlushedPromise = DoHTestUtils.waitForConfigFlush();
  defaults.setBoolPref(`${kRegionalPrefNamespace}.enabled`, true);
  await configFlushedPromise;

  is(
    DoHConfigController.currentConfig.enabled,
    true,
    "Rollout should be enabled"
  );
  await ensureTRRMode(2);

  is(
    Preferences.get("doh-rollout.home-region"),
    "DE",
    "Initial region should be DE"
  );
  RegionTestUtils.setNetworkRegion("UK");
  await Region._fetchRegion();
  Region._setHomeRegion("UK");
  await ensureTRRMode(2); // Mode shouldn't change.

  // The idle-daily event will cause the new region to take effect.
  Services.obs.notifyObservers(null, "idle-daily");

  is(Preferences.get("doh-rollout.home-region"), "UK");

  RegionTestUtils.setNetworkRegion("FR");

  let promise = new Promise(resolve => {
    Services.obs.addObserver(function obs(subject, topic) {
      Services.obs.removeObserver(obs, topic);
      resolve();
    }, "doh-config-updated");
  });

  // For a timezone change, a region check should be performed,
  // and the region should change immediately.
  Services.obs.notifyObservers(null, "default-timezone-changed");

  await promise;
  is(Preferences.get("doh-rollout.home-region"), "FR");

  is(
    DoHConfigController.currentConfig.enabled,
    false,
    "Rollout should not be enabled for new region"
  );
  await ensureTRRMode(undefined); // restart of the controller should change the region.

  // Reset state to initial values.
  await setupRegion();
  defaults.deleteBranch(`doh-rollout.de`);
  Preferences.reset("doh-rollout.home-region-changed");
  Preferences.reset("doh-rollout.home-region");
});
