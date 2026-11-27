"use strict";

ChromeUtils.defineESModuleGetters(this, {
  MockRegistry: "resource://testing-common/MockRegistry.sys.mjs",
  ctypes: "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs",
});

do_get_profile();

let tmpDir;
let baseDir;
let slug =
  AppConstants.platform === "linux" ? "pkcs11-modules" : "PKCS11Modules";

add_task(async function setupTest() {
  tmpDir = await IOUtils.createUniqueDirectory(
    Services.dirsvc.get("TmpD", Ci.nsIFile).path,
    "PKCS11"
  );

  baseDir = PathUtils.join(tmpDir, slug);
  await IOUtils.makeDirectory(baseDir);
});

registerCleanupFunction(async () => {
  await IOUtils.remove(tmpDir, { recursive: true });
});

const testmodule = PathUtils.join(
  PathUtils.parent(Services.dirsvc.get("CurWorkD", Ci.nsIFile).path, 5),
  "security",
  "manager",
  "ssl",
  "tests",
  "unit",
  "pkcs11testmodule",
  ctypes.libraryName("pkcs11testmodule")
);

// This function was inspired by the native messaging test under
// toolkit/components/extensions

async function setupManifests(modules) {
  async function writeManifest(module) {
    let manifest = {
      name: module.name,
      description: module.description,
      path: module.path,
      type: "pkcs11",
      allowed_extensions: [module.id],
    };

    let manifestPath = PathUtils.join(baseDir, `${module.name}.json`);
    await IOUtils.writeJSON(manifestPath, manifest);

    return manifestPath;
  }

  switch (AppConstants.platform) {
    case "macosx":
    case "linux": {
      let dirProvider = {
        getFile(property) {
          if (
            property == "XREUserNativeManifests" ||
            property == "XRESysNativeManifests"
          ) {
            return new FileUtils.File(tmpDir);
          }
          return null;
        },
      };

      Services.dirsvc.registerProvider(dirProvider);
      registerCleanupFunction(() => {
        Services.dirsvc.unregisterProvider(dirProvider);
      });

      for (let module of modules) {
        await writeManifest(module);
      }
      break;
    }

    case "win": {
      const REGKEY = String.raw`Software\Mozilla\PKCS11Modules`;

      let registry = new MockRegistry();
      registerCleanupFunction(() => {
        registry.shutdown();
      });

      for (let module of modules) {
        let manifestPath = await writeManifest(module);
        registry.setValue(
          Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
          `${REGKEY}\\${module.name}`,
          "",
          manifestPath
        );
      }
      break;
    }

    default:
      ok(
        false,
        `Loading of PKCS#11 modules is not supported on ${AppConstants.platform}`
      );
  }
}

add_task(async function test_pkcs11() {
  async function background() {
    try {
      const { os } = await browser.runtime.getPlatformInfo();
      if (os !== "win") {
        // Expect this call to not throw (explicitly cover regression fixed in Bug 1759162).
        let isInstalledNonAbsolute = await browser.pkcs11.isModuleInstalled(
          "testmoduleNonAbsolutePath"
        );
        browser.test.assertFalse(
          isInstalledNonAbsolute,
          "PKCS#11 module with non absolute path expected to not be installed"
        );
      }
      let isInstalled = await browser.pkcs11.isModuleInstalled("testmodule");
      browser.test.assertFalse(
        isInstalled,
        "PKCS#11 module is not installed before we install it"
      );
      await browser.pkcs11.installModule("testmodule", 0);
      isInstalled = await browser.pkcs11.isModuleInstalled("testmodule");
      browser.test.assertTrue(
        isInstalled,
        "PKCS#11 module is installed after we install it"
      );
      let slots = await browser.pkcs11.getModuleSlots("testmodule");
      browser.test.assertEq(
        "Test PKCS11 Slot",
        slots[0].name,
        "The first slot name matches the expected name"
      );
      browser.test.assertEq(
        "Test PKCS11 Slot 二",
        slots[1].name,
        "The second slot name matches the expected name"
      );
      browser.test.assertTrue(slots[1].token, "The second slot has a token");
      browser.test.assertFalse(slots[2].token, "The third slot has no token");
      browser.test.assertEq(
        "Test PKCS11 Tokeñ 2 Label",
        slots[1].token.name,
        "The token name matches the expected name"
      );
      browser.test.assertEq(
        "Test PKCS11 Manufacturer ID",
        slots[1].token.manufacturer,
        "The token manufacturer matches the expected manufacturer"
      );
      browser.test.assertEq(
        "0.0",
        slots[1].token.HWVersion,
        "The token hardware version matches the expected version"
      );
      browser.test.assertEq(
        "0.0",
        slots[1].token.FWVersion,
        "The token firmware version matches the expected version"
      );
      browser.test.assertEq(
        "0000000000000000",
        slots[1].token.serial,
        "The token serial number matches the expected value"
      );
      browser.test.assertFalse(
        slots[1].token.isLoggedIn,
        "The token is not logged in"
      );
      await browser.pkcs11.uninstallModule("testmodule");
      isInstalled = await browser.pkcs11.isModuleInstalled("testmodule");
      browser.test.assertFalse(
        isInstalled,
        "PKCS#11 module is no longer installed after we uninstall it"
      );
      await browser.pkcs11.installModule("testmodule");
      isInstalled = await browser.pkcs11.isModuleInstalled("testmodule");
      browser.test.assertTrue(
        isInstalled,
        "Installing the PKCS#11 module without flags parameter succeeds"
      );
      await browser.pkcs11.uninstallModule("testmodule");
      await browser.test.assertRejects(
        browser.pkcs11.isModuleInstalled("nonexistingmodule"),
        /No such PKCS#11 module nonexistingmodule/,
        "We cannot access modules if no JSON file exists"
      );
      await browser.test.assertRejects(
        browser.pkcs11.isModuleInstalled("othermodule"),
        /No such PKCS#11 module othermodule/,
        "We cannot access modules if we're not listed in the module's manifest file's allowed_extensions key"
      );
      await browser.test.assertRejects(
        browser.pkcs11.uninstallModule("internalmodule"),
        /No such PKCS#11 module internalmodule/,
        "We cannot uninstall the NSS Builtin Roots Module"
      );
      await browser.test.assertRejects(
        browser.pkcs11.installModule("osclientcerts", 0),
        /No such PKCS#11 module osclientcerts/,
        "installModule should not work on the built-in osclientcerts module"
      );
      await browser.test.assertRejects(
        browser.pkcs11.uninstallModule("osclientcerts"),
        /No such PKCS#11 module osclientcerts/,
        "uninstallModule should not work on the built-in osclientcerts module"
      );
      await browser.test.assertRejects(
        browser.pkcs11.isModuleInstalled("osclientcerts"),
        /No such PKCS#11 module osclientcerts/,
        "isModuleLoaded should not work on the built-in osclientcerts module"
      );
      await browser.test.assertRejects(
        browser.pkcs11.getModuleSlots("osclientcerts"),
        /No such PKCS#11 module osclientcerts/,
        "getModuleSlots should not work on the built-in osclientcerts module"
      );
      browser.test.notifyPass("pkcs11");
    } catch (e) {
      browser.test.fail(`Error: ${String(e)} :: ${e.stack}`);
      browser.test.notifyFail("pkcs11 failed");
    }
  }

  let libDir = FileUtils.getDir("GreBinD", []);
  await setupManifests([
    {
      name: "testmodule",
      description: "PKCS#11 Test Module",
      path: testmodule,
      id: "pkcs11@tests.mozilla.org",
    },
    {
      name: "testmoduleNonAbsolutePath",
      description: "PKCS#11 Test Module",
      path: ctypes.libraryName("pkcs11testmodule"),
      id: "pkcs11@tests.mozilla.org",
    },
    {
      name: "othermodule",
      description: "PKCS#11 Test Module",
      path: testmodule,
      id: "other@tests.mozilla.org",
    },
    {
      name: "internalmodule",
      description: "Builtin Roots Module",
      path: PathUtils.join(
        Services.dirsvc.get("CurWorkD", Ci.nsIFile).path,
        ctypes.libraryName("nssckbi")
      ),
      id: "pkcs11@tests.mozilla.org",
    },
    {
      name: "osclientcerts",
      description: "OS Client Cert Module",
      path: PathUtils.join(libDir.path, ctypes.libraryName("osclientcerts")),
      id: "pkcs11@tests.mozilla.org",
    },
  ]);

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["pkcs11"],
      browser_specific_settings: { gecko: { id: "pkcs11@tests.mozilla.org" } },
    },
    background: background,
  });
  await extension.startup();
  await extension.awaitFinish("pkcs11");
  await extension.unload();
});
