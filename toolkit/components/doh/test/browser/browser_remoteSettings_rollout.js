/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);
add_task(setupRegion);

add_task(async function testPrefFirstRollout() {
  let defaults = Services.prefs.getDefaultBranch("");

  setPassingHeuristics();

  is(
    DoHConfigController.currentConfig.enabled,
    false,
    "Rollout should not be enabled"
  );

  let configFlushedPromise = DoHTestUtils.waitForConfigFlush();
  defaults.setBoolPref(`${kRegionalPrefNamespace}.enabled`, true);
  await configFlushedPromise;

  is(
    DoHConfigController.currentConfig.enabled,
    true,
    "Rollout should be enabled"
  );
  await ensureTRRMode(2);

  await DoHTestUtils.loadRemoteSettingsProviders([
    {
      id: "provider1",
      uri: "https://example.org/1",
      autoDefault: true,
    },
  ]);

  await DoHTestUtils.loadRemoteSettingsConfig({
    id: kTestRegion.toLowerCase(),
    rolloutEnabled: true,
    providers: "provider1",
  });

  is(
    DoHConfigController.currentConfig.enabled,
    true,
    "Rollout should still be enabled"
  );

  defaults.deleteBranch(`${kRegionalPrefNamespace}.enabled`);
  await restartDoHController();

  is(
    DoHConfigController.currentConfig.enabled,
    true,
    "Rollout should still be enabled"
  );
  await ensureTRRMode(2);

  await DoHTestUtils.resetRemoteSettingsConfig();

  is(
    DoHConfigController.currentConfig.enabled,
    false,
    "Rollout should not be enabled"
  );
  await ensureTRRMode(undefined);
});
