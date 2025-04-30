/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { XPIExports } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/XPIExports.sys.mjs"
);

// NOTE: Only constants can be extracted from XPIExports.
const {
  XPIInternal: {
    KEY_APP_PROFILE,
    KEY_APP_SYSTEM_BUILTINS,
    KEY_APP_SYSTEM_PROFILE,
  },
} = XPIExports;

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();

// Disable "xpc::IsInAutomation()", since it would override the behavior
// we're testing for.
Services.prefs.setBoolPref(
  "security.turn_off_all_security_so_that_viruses_can_take_over_this_computer",
  false
);

Services.prefs.setIntPref(
  "extensions.enabledScopes",
  // SCOPE_PROFILE is enabled by default,
  // SCOPE_APPLICATION is to enable KEY_APP_SYSTEM_PROFILE, which we need to
  // test the combination (isSystem && !isBuiltin) in test_system_location.
  AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION
);
// test_builtin_system_location tests the (isSystem && isBuiltin) combination
// (i.e. KEY_APP_SYSTEM_DEFAULTS). That location only exists if this directory
// is found:
const distroDir = FileUtils.getDir("ProfD", ["sysfeatures"]);
distroDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
registerDirectory("XREAppFeat", distroDir);

function getInstallLocation({
  isBuiltin = false,
  isSystem = false,
  isTemporary = false,
}) {
  if (isTemporary) {
    // Temporary installation. Signatures will not be verified.
    return XPIExports.XPIInternal.TemporaryInstallLocation; // KEY_APP_TEMPORARY
  }
  let location;
  if (isSystem) {
    if (isBuiltin) {
      location = XPIExports.XPIInternal.XPIStates.getLocation(
        KEY_APP_SYSTEM_BUILTINS
      );
    } else {
      // Normandy installations. Signatures will be verified.
      location = XPIExports.XPIInternal.XPIStates.getLocation(
        KEY_APP_SYSTEM_PROFILE
      );
    }
  } else if (isBuiltin) {
    // Packaged with the application. Signatures will not be verified.
    location = XPIExports.XPIInternal.BuiltInLocation; // KEY_APP_BUILTINS
  } else {
    // By default - The profile directory. Signatures will be verified.
    location = XPIExports.XPIInternal.XPIStates.getLocation(KEY_APP_PROFILE);
  }
  // Sanity checks to make sure that the flags match the expected values.
  if (location.isSystem !== isSystem) {
    ok(false, `${location.name}, unexpected isSystem=${location.isSystem}`);
  }
  if (location.isBuiltin !== isBuiltin) {
    ok(false, `${location.name}, unexpected isBuiltin=${location.isBuiltin}`);
  }
  return location;
}

async function testLoadManifest({ location, expectPrivileged }) {
  location ??= getInstallLocation({});
  let xpi = await AddonTestUtils.createTempWebExtensionFile({
    manifest: {
      browser_specific_settings: { gecko: { id: "@with-privileged-perm" } },
      permissions: ["mozillaAddons", "cookies"],
    },
  });
  let actualPermissions;
  let { messages } = await AddonTestUtils.promiseConsoleOutput(async () => {
    if (location.isTemporary && !expectPrivileged) {
      ExtensionTestUtils.failOnSchemaWarnings(false);
      await Assert.rejects(
        XPIExports.XPIInstall.loadManifestFromFile(xpi, location),
        /Extension is invalid/,
        "load manifest failed with privileged permission"
      );
      ExtensionTestUtils.failOnSchemaWarnings(true);
      return;
    }
    let addon = await XPIExports.XPIInstall.loadManifestFromFile(xpi, location);
    actualPermissions = addon.userPermissions;
    equal(addon.isPrivileged, expectPrivileged, "addon.isPrivileged");
  });
  if (expectPrivileged) {
    AddonTestUtils.checkMessages(messages, {
      expected: [],
      forbidden: [
        {
          message: /Reading manifest: Invalid extension permission/,
        },
      ],
    });
    Assert.deepEqual(
      actualPermissions,
      {
        origins: [],
        permissions: ["mozillaAddons", "cookies"],
        data_collection: [],
      },
      "Privileged permission should exist"
    );
  } else if (location.isTemporary) {
    AddonTestUtils.checkMessages(messages, {
      expected: [
        {
          message:
            /Using the privileged permission 'mozillaAddons' requires a privileged add-on/,
        },
      ],
      forbidden: [],
    });
  } else {
    AddonTestUtils.checkMessages(messages, {
      expected: [
        {
          message:
            /Reading manifest: Invalid extension permission: mozillaAddons/,
        },
      ],
      forbidden: [],
    });
    Assert.deepEqual(
      actualPermissions,
      { origins: [], permissions: ["cookies"], data_collection: [] },
      "Privileged permission should be ignored"
    );
  }
}

add_task(async function setup() {
  await ExtensionTestUtils.startAddonManager();
});

add_task(async function test_regular_addon() {
  AddonTestUtils.usePrivilegedSignatures = false;
  await testLoadManifest({
    expectPrivileged: false,
  });
});

add_task(async function test_privileged_signature() {
  AddonTestUtils.usePrivilegedSignatures = true;
  await testLoadManifest({
    expectPrivileged: true,
  });
});

add_task(async function test_system_signature() {
  AddonTestUtils.usePrivilegedSignatures = "system";
  await testLoadManifest({
    expectPrivileged: true,
  });
});

add_task(async function test_builtin_location() {
  AddonTestUtils.usePrivilegedSignatures = false;
  await testLoadManifest({
    expectPrivileged: true,
    location: getInstallLocation({ isBuiltin: true }),
  });
});

add_task(async function test_system_location() {
  AddonTestUtils.usePrivilegedSignatures = false;
  await testLoadManifest({
    expectPrivileged: false,
    location: getInstallLocation({ isSystem: true }),
  });
});

add_task(async function test_builtin_system_location() {
  AddonTestUtils.usePrivilegedSignatures = false;
  await testLoadManifest({
    expectPrivileged: true,
    location: getInstallLocation({
      isSystem: true,
      isBuiltin: true,
    }),
  });
});

add_task(async function test_temporary_regular() {
  AddonTestUtils.usePrivilegedSignatures = false;
  Services.prefs.setBoolPref("extensions.experiments.enabled", false);
  await testLoadManifest({
    expectPrivileged: false,
    location: getInstallLocation({ isTemporary: true }),
  });
});

add_task(async function test_temporary_privileged_signature() {
  AddonTestUtils.usePrivilegedSignatures = true;
  Services.prefs.setBoolPref("extensions.experiments.enabled", false);
  await testLoadManifest({
    expectPrivileged: true,
    location: getInstallLocation({ isTemporary: true }),
  });
});

add_task(async function test_temporary_experiments_enabled() {
  AddonTestUtils.usePrivilegedSignatures = false;
  Services.prefs.setBoolPref("extensions.experiments.enabled", true);

  // Experiments can only be used if AddonSettings.EXPERIMENTS_ENABLED is true.
  // This is the condition behind the flag, minus Cu.isInAutomation. Currently
  // that flag is false despite this being a test (see bug 1598804), but that
  // is desired in this case because we want the test to confirm the real-world
  // behavior instead of test-specific behavior.
  const areTemporaryExperimentsAllowed =
    !AppConstants.MOZ_REQUIRE_SIGNING ||
    AppConstants.NIGHTLY_BUILD ||
    AppConstants.MOZ_DEV_EDITION;

  await testLoadManifest({
    expectPrivileged: areTemporaryExperimentsAllowed,
    location: getInstallLocation({ isTemporary: true }),
  });
});
