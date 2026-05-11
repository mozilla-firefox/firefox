ChromeUtils.defineESModuleGetters(this, {
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
});

const kRegPath = `Software\\Mozilla\\${AppConstants.MOZ_APP_BASENAME}\\PreXULSkeletonUISettings`;

function getFirefoxExecutableFile() {
  let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file = Services.dirsvc.get("GreBinD", Ci.nsIFile);

  file.append(AppConstants.MOZ_APP_NAME + ".exe");
  return file;
}

// This is copied from WindowsRegistry.sys.mjs, but extended to support
// TYPE_BINARY, as that is how we represent doubles in the registry for
// the skeleton UI. However, we didn't extend WindowsRegistry.sys.mjs itself,
// because TYPE_BINARY is kind of a footgun for javascript callers - our
// use case is just trivial (checking that the value is non-zero).
function readRegKeyExtended(aRoot, aPath, aKey, aRegistryNode = 0) {
  const kRegMultiSz = 7;
  const kMode = Ci.nsIWindowsRegKey.ACCESS_READ | aRegistryNode;
  let registry = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
    Ci.nsIWindowsRegKey
  );
  try {
    registry.open(aRoot, aPath, kMode);
    if (registry.hasValue(aKey)) {
      let type = registry.getValueType(aKey);
      switch (type) {
        case kRegMultiSz: {
          // nsIWindowsRegKey doesn't support REG_MULTI_SZ type out of the box.
          let str = registry.readStringValue(aKey);
          return str.split("\0").filter(v => v);
        }
        case Ci.nsIWindowsRegKey.TYPE_STRING:
          return registry.readStringValue(aKey);
        case Ci.nsIWindowsRegKey.TYPE_INT:
          return registry.readIntValue(aKey);
        case Ci.nsIWindowsRegKey.TYPE_BINARY:
          return registry.readBinaryValue(aKey);
        default:
          throw new Error("Unsupported registry value.");
      }
    }
  } catch (ex) {
  } finally {
    registry.close();
  }
  return undefined;
}

add_task(async function testWritesEnabledOnPrefChange() {
  Services.prefs.setBoolPref("browser.startup.preXulSkeletonUI", true);

  const win = await BrowserTestUtils.openNewBrowserWindow();

  const firefoxPath = getFirefoxExecutableFile().path;
  let enabled = WindowsRegistry.readRegKey(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    kRegPath,
    `${firefoxPath}|Enabled`
  );
  is(enabled, 1, "Pre-XUL skeleton UI is enabled in the Windows registry");

  Services.prefs.setBoolPref("browser.startup.preXulSkeletonUI", false);
  enabled = WindowsRegistry.readRegKey(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    kRegPath,
    `${firefoxPath}|Enabled`
  );
  is(enabled, 0, "Pre-XUL skeleton UI is disabled in the Windows registry");

  Services.prefs.setBoolPref("browser.startup.preXulSkeletonUI", true);
  Services.prefs.setIntPref("browser.tabs.inTitlebar", 0);
  enabled = WindowsRegistry.readRegKey(
    Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
    kRegPath,
    `${firefoxPath}|Enabled`
  );
  is(enabled, 0, "Pre-XUL skeleton UI is disabled in the Windows registry");

  await BrowserTestUtils.closeWindow(win);
});

add_task(async function testPersistsNecessaryValuesOnChange() {
  // Enable the skeleton UI, since if it's disabled we won't persist the size values
  await SpecialPowers.pushPrefEnv({
    set: [["browser.startup.preXulSkeletonUI", true]],
  });

  const regKeys = [
    "Width",
    "Height",
    "ScreenX",
    "ScreenY",
    "UrlbarCSSSpan",
    "CssToDevPixelScaling",
    "SpringsCSSSpan",
    "SearchbarCSSSpan",
    "Theme",
    "Flags",
    "Progress",
  ];

  // Remove all of the registry values to ensure old tests aren't giving us false
  // positives
  for (let key of regKeys) {
    WindowsRegistry.removeRegKey(
      Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
      kRegPath,
      key
    );
  }

  const win = await BrowserTestUtils.openNewBrowserWindow();
  const firefoxPath = getFirefoxExecutableFile().path;
  for (let key of regKeys) {
    let value = readRegKeyExtended(
      Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER,
      kRegPath,
      `${firefoxPath}|${key}`
    );
    isnot(
      typeof value,
      "undefined",
      `Skeleton UI registry values should have a defined value for ${key}`
    );
    if (value.length) {
      let hasNonZero = false;
      for (var i = 0; i < value.length; i++) {
        hasNonZero = hasNonZero || value[i];
      }
      ok(hasNonZero, `Value should have non-zero components for ${key}`);
    }
  }

  await BrowserTestUtils.closeWindow(win);
});
