/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests that extensions installed through the registry work as expected
createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

// Enable loading extensions from the user and system scopes
Services.prefs.setIntPref(
  "extensions.enabledScopes",
  AddonManager.SCOPE_PROFILE +
    AddonManager.SCOPE_USER +
    AddonManager.SCOPE_SYSTEM
);

Services.prefs.setIntPref("extensions.sideloadScopes", AddonManager.SCOPE_ALL);

const ID1 = "addon1@tests.mozilla.org";
const ID2 = "addon2@tests.mozilla.org";
let xpi1, xpi2;

let registry;

add_task(async function setup() {
  xpi1 = await createTempWebExtensionFile({
    manifest: { browser_specific_settings: { gecko: { id: ID1 } } },
  });

  xpi2 = await createTempWebExtensionFile({
    manifest: { browser_specific_settings: { gecko: { id: ID2 } } },
  });

  registry = new MockRegistry();
  registerCleanupFunction(() => {
    registry.shutdown();
  });
});

// Tests whether basic registry install works
add_task(async function test_1() {
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    xpi1.path
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID2,
    xpi2.path
  );

  await promiseStartupManager();

  let [a1, a2] = await AddonManager.getAddonsByIDs([ID1, ID2]);
  notEqual(a1, null);
  ok(a1.isActive);
  ok(!hasFlag(a1.permissions, AddonManager.PERM_CAN_UNINSTALL));
  equal(a1.scope, AddonManager.SCOPE_SYSTEM);

  notEqual(a2, null);
  ok(a2.isActive);
  ok(!hasFlag(a2.permissions, AddonManager.PERM_CAN_UNINSTALL));
  equal(a2.scope, AddonManager.SCOPE_USER);
});

// Tests whether uninstalling from the registry works
add_task(async function test_2() {
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    null
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID2,
    null
  );

  await promiseRestartManager();

  let [a1, a2] = await AddonManager.getAddonsByIDs([ID1, ID2]);
  equal(a1, null);
  equal(a2, null);
});

// Checks that the ID in the registry must match that in the install manifest
add_task(async function test_3() {
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    xpi2.path
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID2,
    xpi1.path
  );

  await promiseRestartManager();

  let [a1, a2] = await AddonManager.getAddonsByIDs([ID1, ID2]);
  equal(a1, null);
  equal(a2, null);
});

// Tests whether an extension's ID can change without its directory changing
add_task(async function test_4() {
  // Restarting with bad items in the registry should not force an EM restart
  await promiseRestartManager();

  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    null
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID2,
    null
  );

  await promiseRestartManager();

  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    xpi1.path
  );

  await promiseShutdownManager();

  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID1,
    null
  );
  registry.setValue(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    "SOFTWARE\\Mozilla\\XPCShell\\Extensions",
    ID2,
    xpi1.path
  );
  xpi2.copyTo(xpi1.parent, xpi1.leafName);

  await promiseStartupManager();

  let [a1, a2] = await AddonManager.getAddonsByIDs([ID1, ID2]);
  equal(a1, null);
  notEqual(a2, null);
});

// Tests that registry entries with unrecognized paths are ignored
add_task(async function test_unrecognized_paths() {
  const ID3 = "addon3@tests.mozilla.org";
  let xpi3 = await createTempWebExtensionFile({
    manifest: { browser_specific_settings: { gecko: { id: ID3 } } },
  });

  const KEY = "SOFTWARE\\Mozilla\\XPCShell\\Extensions";
  const { ROOT_KEY_CURRENT_USER, ROOT_KEY_LOCAL_MACHINE } = Ci.nsIWindowsRegKey;
  const entries = [
    [
      ROOT_KEY_LOCAL_MACHINE,
      "relative-path@tests.mozilla.org",
      "extensions\\relative.xpi",
    ],
    [
      ROOT_KEY_CURRENT_USER,
      "unexpanded-var@tests.mozilla.org",
      "%APPDATA%\\unexpanded.xpi",
    ],
    [ROOT_KEY_LOCAL_MACHINE, ID3, xpi3.path],
  ];
  const removeEntries = () => {
    for (let [root, id] of entries) {
      registry.setValue(root, KEY, id, null);
    }
  };
  registerCleanupFunction(removeEntries);

  for (let [root, id, path] of entries) {
    registry.setValue(root, KEY, id, path);
  }

  const { messages } = await AddonTestUtils.promiseConsoleOutput(() =>
    promiseRestartManager()
  );

  const expectedWarning = (id, path) => ({
    message: new RegExp(
      `Ignoring WinReg-registered add-on entry ${RegExp.escape(id)} with path "${RegExp.escape(path)}".*NS_ERROR_FILE_UNRECOGNIZED_PATH`
    ),
  });
  AddonTestUtils.checkMessages(
    messages,
    {
      expected: [
        expectedWarning(
          "unexpanded-var@tests.mozilla.org",
          "%APPDATA%\\unexpanded.xpi"
        ),
        expectedWarning(
          "relative-path@tests.mozilla.org",
          "extensions\\relative.xpi"
        ),
      ],
    },
    "Got the expected warnings for the ignored registry entries"
  );

  let [relativeAddon, unexpandedAddon, addon] =
    await AddonManager.getAddonsByIDs(entries.map(([, id]) => id));
  equal(relativeAddon, null, "Relative path registry entry is ignored");
  equal(unexpandedAddon, null, "Unexpanded %VAR% registry entry is ignored");
  notEqual(addon, null);
  ok(addon.isActive, "Valid registry add-on is still installed");

  removeEntries();
  await promiseRestartManager();

  addon = await AddonManager.getAddonByID(ID3);
  equal(addon, null, "Registry add-on is uninstalled once removed");
});
