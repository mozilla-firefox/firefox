/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { BuiltInThemes } = ChromeUtils.importESModule(
  "resource:///modules/BuiltInThemes.sys.mjs"
);
const { XPIExports } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/XPIExports.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const BUILT_IN_THEME_IDS = Array.from(BuiltInThemes.builtInThemeMap.keys());

let maybeInstallBuiltinAddonCallArgs = [];
add_setup(() => {
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => {
    sandbox.restore();
  });

  // Stub XPIProvider maybeInstallBuiltinAddon method and
  // collect the last call arguments received (which each
  // of the test task in this test file will be verifying).
  sandbox
    .stub(XPIExports.XPIProvider, "maybeInstallBuiltinAddon")
    .callsFake((...args) => {
      info(`Got AddonManager.maybeInstallBuiltinAddon call: ${args}`);
      maybeInstallBuiltinAddonCallArgs.push(args);
    });
});

add_task(async function test_maybeInstallActiveBuiltInTheme() {
  for (const themeId of BUILT_IN_THEME_IDS) {
    const themeConfig = BuiltInThemes.builtInThemeMap.get(themeId);
    await SpecialPowers.pushPrefEnv({
      set: [["extensions.activeThemeID", themeId]],
    });
    maybeInstallBuiltinAddonCallArgs = [];
    BuiltInThemes.maybeInstallActiveBuiltInTheme();
    Assert.deepEqual(
      maybeInstallBuiltinAddonCallArgs.pop(),
      [themeId, themeConfig.version, themeConfig.path],
      "AddonManager.maybeInstallBuiltinAddon got the expected arguments"
    );
    Assert.deepEqual(
      maybeInstallBuiltinAddonCallArgs,
      [],
      "No remaining unchecked maybeInstallBuiltinAddon call args"
    );
    info(
      `Verify fetching ${themeId} manifest from ${themeConfig.path}manifest.json`
    );
    const themeManifest = await fetch(`${themeConfig.path}manifest.json`).then(
      res => res.json()
    );
    Assert.ok(themeManifest, "Got builtin theme manifest.json");
    Assert.equal(
      themeManifest.browser_specific_settings?.gecko.id ??
        themeManifest.applications?.gecko.id,
      themeId,
      "theme id from manifest.json should match the BuiltInThemes theme config"
    );
    Assert.equal(
      themeManifest.version,
      themeConfig.version,
      "theme version from manifest.json should match the BuiltInThemes theme config"
    );
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_ensureBuiltInThemes() {
  maybeInstallBuiltinAddonCallArgs = [];

  await BuiltInThemes.ensureBuiltInThemes();

  const now = new Date();
  for (const themeId of BUILT_IN_THEME_IDS) {
    const themeConfig = BuiltInThemes.builtInThemeMap.get(themeId);
    // Skip expired themes (for which BuiltInThemes.ensureBuiltInThemes
    // is expected to not be calling maybeInstallBuiltinAddon).
    if (themeConfig.expiry && new Date(themeConfig.expiry) <= now) {
      continue;
    }
    // This test task does only assert the maybeInstallBuiltinAddon
    // call arguments are the expected ones, the previous test task
    // (test_maybeInstallActiveBuiltInTheme is also verifying the
    // theme config retrieved from the BuiltInThemes points to
    // an existing theme manifest).
    Assert.deepEqual(
      maybeInstallBuiltinAddonCallArgs.shift(),
      [themeId, themeConfig.version, themeConfig.path],
      "AddonManager.maybeInstallBuiltinAddon got the expected arguments"
    );
  }
  Assert.deepEqual(
    maybeInstallBuiltinAddonCallArgs,
    [],
    "No remaining unchecked maybeInstallBuiltinAddon call args"
  );
});
