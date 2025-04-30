// Tests that AddonRepository doesn't download results for system add-ons

// Enable SCOPE_APPLICATION for builtin testing.  Default in tests is only SCOPE_PROFILE.
let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

const PREF_GETADDONS_CACHE_ENABLED = "extensions.getAddons.cache.enabled";

createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "0");

var gServer = new HttpServer();

add_setup(() => {
  gServer.start(-1);
  gServer.registerPathHandler("/get", () => {
    do_throw("Unexpected request to server.");
  });
  registerCleanupFunction(async () => {
    await new Promise(resolve => gServer.stop(resolve));
  });
});

add_task(async function test_app_addons_systembuiltin() {
  Services.prefs.setBoolPref(PREF_GETADDONS_CACHE_ENABLED, true);
  Services.prefs.setCharPref(
    PREF_GETADDONS_BYIDS,
    `http://localhost:${gServer.identity.primaryPort}/get?%IDS%`
  );

  await overrideBuiltIns({
    builtins: [
      await getSystemBuiltin(1),
      await getSystemBuiltin(2),
      await getSystemBuiltin(3),
    ],
  });

  await promiseStartupManager();

  await AddonRepository.cacheAddons([
    "system1@tests.mozilla.org",
    "system2@tests.mozilla.org",
    "system3@tests.mozilla.org",
  ]);

  let cached = await AddonRepository.getCachedAddonByID(
    "system1@tests.mozilla.org"
  );
  Assert.equal(cached, null);

  cached = await AddonRepository.getCachedAddonByID(
    "system2@tests.mozilla.org"
  );
  Assert.equal(cached, null);

  cached = await AddonRepository.getCachedAddonByID(
    "system3@tests.mozilla.org"
  );
  Assert.equal(cached, null);

  await promiseShutdownManager();
});
