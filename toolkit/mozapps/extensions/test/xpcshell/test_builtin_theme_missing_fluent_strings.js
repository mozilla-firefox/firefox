let scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
Services.prefs.setIntPref("extensions.enabledScopes", scopes);

AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

const createMockThemeManifest = (id, version) => ({
  name: "A mock theme",
  author: "Mozilla",
  version,
  icons: { 32: "icon.svg" },
  theme: {
    colors: {
      toolbar: "red",
    },
  },
  browser_specific_settings: {
    gecko: { id },
  },
});

add_setup(async () => {
  await AddonTestUtils.promiseStartupManager();
});

// This test case ensures that when a builtin theme does not have any fluent
// strings for the manifest properties expected to be localized through fluent
// (e.g. the addon name) then the AddonWrapper will fallback to use the manifest
// field and it doesn't break the AddonWrapper getter.
//
// This scenario covers the case where a builtin theme has been removed from the tree
// but some users may still have it selected as an active theme from a previous Firefox
// version.
add_task(async function test_builtin_theme_missing_fluent_strings() {
  const THEME_ID = "a-fake-builtin-theme@mozilla.org";
  const manifest = createMockThemeManifest(THEME_ID, "1.0.0");
  await installBuiltinExtension({ manifest }, false /* waitForStartup */);
  const theme = await AddonManager.getAddonByID(THEME_ID);
  Assert.equal(
    theme.isBuiltin,
    true,
    "Expect the test theme to be detected as builtin"
  );
  Assert.equal(
    theme.name,
    manifest.name,
    "Expect the test theme name to match manifest name property"
  );
});
