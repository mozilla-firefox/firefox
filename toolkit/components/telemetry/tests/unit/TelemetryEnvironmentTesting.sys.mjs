/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  Assert: "resource://testing-common/Assert.sys.mjs",
  // AttributionCode is only needed for Firefox
  AttributionCode: "resource:///modules/AttributionCode.sys.mjs",

  MockRegistrar: "resource://testing-common/MockRegistrar.sys.mjs",
});

const gIsWindows = AppConstants.platform == "win";
const gIsMac = AppConstants.platform == "macosx";
const gIsAndroid = AppConstants.platform == "android";
const gIsLinux = AppConstants.platform == "linux";

const MILLISECONDS_PER_MINUTE = 60 * 1000;
const MILLISECONDS_PER_HOUR = 60 * MILLISECONDS_PER_MINUTE;
const MILLISECONDS_PER_DAY = 24 * MILLISECONDS_PER_HOUR;

const PLATFORM_VERSION = "1.9.2";
const APP_VERSION = "1";
const APP_ID = "xpcshell@tests.mozilla.org";
const APP_NAME = "XPCShell";

const DISTRIBUTION_ID = "distributor-id";
const DISTRIBUTION_VERSION = "4.5.6b";
const DISTRIBUTOR_NAME = "Some Distributor";
const DISTRIBUTOR_CHANNEL = "A Channel";
const PARTNER_NAME = "test";
const PARTNER_ID = "NicePartner-ID-3785";

// The profile reset date, in milliseconds (Today)
const PROFILE_RESET_DATE_MS = Date.now();
// The profile creation date, in milliseconds (Yesterday).
const PROFILE_FIRST_USE_MS = PROFILE_RESET_DATE_MS - MILLISECONDS_PER_DAY;
const PROFILE_CREATION_DATE_MS = PROFILE_FIRST_USE_MS - MILLISECONDS_PER_DAY;
const PROFILE_RECOVERED_FROM_BACKUP =
  PROFILE_RESET_DATE_MS - MILLISECONDS_PER_HOUR;

const GFX_VENDOR_ID = "0xabcd";
const GFX_DEVICE_ID = "0x1234";

const EXPECTED_HDD_FIELDS = ["profile", "binary", "system"];

// Valid attribution code to write so that settings.attribution can be tested.
const ATTRIBUTION_CODE = "source%3Dgoogle.com%26dlsource%3Dunittest";

function truncateToDays(aMsec) {
  return Math.floor(aMsec / MILLISECONDS_PER_DAY);
}

var SysInfo = {
  overrides: {},

  getProperty(name) {
    // Assert.ok(false, "Mock SysInfo: " + name + ", " + JSON.stringify(this.overrides));
    if (name in this.overrides) {
      return this.overrides[name];
    }

    return this._genuine.QueryInterface(Ci.nsIPropertyBag).getProperty(name);
  },

  getPropertyAsACString(name) {
    return this.get(name);
  },

  getPropertyAsUint32(name) {
    return this.get(name);
  },

  get(name) {
    return this._genuine.QueryInterface(Ci.nsIPropertyBag2).get(name);
  },

  get diskInfo() {
    return this._genuine.QueryInterface(Ci.nsISystemInfo).diskInfo;
  },

  get osInfo() {
    return this._genuine.QueryInterface(Ci.nsISystemInfo).osInfo;
  },

  get processInfo() {
    return this._genuine.QueryInterface(Ci.nsISystemInfo).processInfo;
  },

  QueryInterface: ChromeUtils.generateQI(["nsIPropertyBag2", "nsISystemInfo"]),
};

/**
 * TelemetryEnvironmentTesting - tools for testing the telemetry environment
 * reporting.
 */
export var TelemetryEnvironmentTesting = {
  EXPECTED_HDD_FIELDS,

  init(appInfo) {
    this.appInfo = appInfo;
  },

  setSysInfoOverrides(overrides) {
    SysInfo.overrides = overrides;
  },

  spoofGfxAdapter() {
    try {
      let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(
        Ci.nsIGfxInfoDebug
      );
      gfxInfo.spoofVendorID(GFX_VENDOR_ID);
      gfxInfo.spoofDeviceID(GFX_DEVICE_ID);
    } catch (x) {
      // If we can't test gfxInfo, that's fine, we'll note it later.
    }
  },

  spoofProfileReset() {
    return IOUtils.writeJSON(
      PathUtils.join(PathUtils.profileDir, "times.json"),
      {
        created: PROFILE_CREATION_DATE_MS,
        reset: PROFILE_RESET_DATE_MS,
        firstUse: PROFILE_FIRST_USE_MS,
        recoveredFromBackup: PROFILE_RECOVERED_FROM_BACKUP,
      }
    );
  },

  spoofPartnerInfo() {
    let prefsToSpoof = {};
    prefsToSpoof["distribution.id"] = DISTRIBUTION_ID;
    prefsToSpoof["distribution.version"] = DISTRIBUTION_VERSION;
    prefsToSpoof["app.distributor"] = DISTRIBUTOR_NAME;
    prefsToSpoof["app.distributor.channel"] = DISTRIBUTOR_CHANNEL;
    prefsToSpoof["app.partner.test"] = PARTNER_NAME;
    prefsToSpoof["mozilla.partner.id"] = PARTNER_ID;

    // Spoof the preferences.
    for (let pref in prefsToSpoof) {
      Services.prefs
        .getDefaultBranch(null)
        .setStringPref(pref, prefsToSpoof[pref]);
    }
  },

  async spoofAttributionData() {
    if (gIsWindows) {
      lazy.AttributionCode._clearCache();
      await lazy.AttributionCode.writeAttributionFile(ATTRIBUTION_CODE);
    } else if (gIsMac) {
      lazy.AttributionCode._clearCache();
      const { MacAttribution } = ChromeUtils.importESModule(
        "resource:///modules/MacAttribution.sys.mjs"
      );
      await MacAttribution.setAttributionString(ATTRIBUTION_CODE);
    }
  },

  async cleanupAttributionData() {
    if (gIsWindows) {
      lazy.AttributionCode.attributionFile.remove(false);
      lazy.AttributionCode._clearCache();
    } else if (gIsMac) {
      const { MacAttribution } = ChromeUtils.importESModule(
        "resource:///modules/MacAttribution.sys.mjs"
      );
      await MacAttribution.delAttributionString();
    }
  },

  registerFakeSysInfo() {
    lazy.MockRegistrar.register("@mozilla.org/system-info;1", SysInfo);
  },

  /**
   * Check that a value is a string and not empty.
   *
   * @param aValue The variable to check.
   * @return True if |aValue| has type "string" and is not empty, False otherwise.
   */
  checkString(aValue) {
    return typeof aValue == "string" && aValue != "";
  },

  /**
   * If value is non-null, check if it's a valid string.
   *
   * @param aValue The variable to check.
   * @return True if it's null or a valid string, false if it's non-null and an invalid
   *         string.
   */
  checkNullOrString(aValue) {
    if (aValue) {
      return this.checkString(aValue);
    } else if (aValue === null) {
      return true;
    }

    return false;
  },

  /**
   * If value is non-null, check if it's a boolean.
   *
   * @param aValue The variable to check.
   * @return True if it's null or a valid boolean, false if it's non-null and an invalid
   *         boolean.
   */
  checkNullOrBool(aValue) {
    return aValue === null || typeof aValue == "boolean";
  },

  checkBuildSection(data) {
    const expectedInfo = {
      applicationId: APP_ID,
      applicationName: APP_NAME,
      buildId: this.appInfo.appBuildID,
      version: APP_VERSION,
      vendor: "Mozilla",
      platformVersion: PLATFORM_VERSION,
      xpcomAbi: "noarch-spidermonkey",
    };

    lazy.Assert.ok(
      "build" in data,
      "There must be a build section in Environment."
    );

    for (let f in expectedInfo) {
      lazy.Assert.ok(
        this.checkString(data.build[f]),
        f + " must be a valid string."
      );
      lazy.Assert.equal(
        data.build[f],
        expectedInfo[f],
        f + " must have the correct value."
      );
    }

    // Make sure architecture is in the environment.
    lazy.Assert.ok(this.checkString(data.build.architecture));

    lazy.Assert.equal(
      data.build.updaterAvailable,
      AppConstants.MOZ_UPDATER,
      "build.updaterAvailable must equal AppConstants.MOZ_UPDATER"
    );

    // Check Glean's values
    lazy.Assert.equal(Glean.xpcom.abi.testGetValue(), expectedInfo.xpcomAbi);
    lazy.Assert.equal(
      Glean.updater.available.testGetValue(),
      AppConstants.MOZ_UPDATER
    );
  },

  checkSettingsSection(data) {
    const EXPECTED_FIELDS_TYPES = {
      blocklistEnabled: "boolean",
      e10sEnabled: "boolean",
      e10sMultiProcesses: "number",
      fissionEnabled: "boolean",
      intl: "object",
      locale: "string",
      update: "object",
      userPrefs: "object",
    };

    lazy.Assert.ok(
      "settings" in data,
      "There must be a settings section in Environment."
    );

    for (let f in EXPECTED_FIELDS_TYPES) {
      lazy.Assert.equal(
        typeof data.settings[f],
        EXPECTED_FIELDS_TYPES[f],
        f + " must have the correct type."
      );
    }
    lazy.Assert.equal(typeof Glean.blocklist.enabled.testGetValue(), "boolean");
    lazy.Assert.equal(typeof Glean.e10s.enabled.testGetValue(), "boolean");
    lazy.Assert.equal(
      typeof Glean.e10s.multiProcesses.testGetValue(),
      "number"
    );
    lazy.Assert.equal(typeof Glean.fission.enabled.testGetValue(), "boolean");
    lazy.Assert.equal(
      typeof Glean.preferences.userPrefs.testGetValue(),
      "object"
    );

    // This property is not always present, but when it is, it must be a number.
    if ("launcherProcessState" in data.settings) {
      lazy.Assert.equal(typeof data.settings.launcherProcessState, "number");
      lazy.Assert.equal(
        typeof Glean.launcherProcess.state.testGetValue(),
        "number"
      );
    }

    // Check "addonCompatibilityCheckEnabled" separately.
    lazy.Assert.equal(
      data.settings.addonCompatibilityCheckEnabled,
      lazy.AddonManager.checkCompatibility
    );
    lazy.Assert.equal(
      Glean.addonsManager.compatibilityCheckEnabled.testGetValue(),
      lazy.AddonManager.checkCompatibility
    );

    // Check "isDefaultBrowser" separately, as it is not available on Android an can either be
    // null or boolean on other platforms.
    if (gIsAndroid) {
      lazy.Assert.ok(
        !("isDefaultBrowser" in data.settings),
        "Must not be available on Android."
      );
      lazy.Assert.equal(null, Glean.browser.defaultAtLaunch.testGetValue());
    } else if ("isDefaultBrowser" in data.settings) {
      // isDefaultBrowser might not be available in the payload, since it's
      // gathered after the session was restored.
      lazy.Assert.ok(this.checkNullOrBool(data.settings.isDefaultBrowser));
      lazy.Assert.ok(
        this.checkNullOrBool(Glean.browser.defaultAtLaunch.testGetValue())
      );
    }

    // Check "channel" separately, as it can either be null or string.
    let update = data.settings.update;
    lazy.Assert.ok(this.checkNullOrString(update.channel));
    lazy.Assert.equal(typeof update.enabled, "boolean");
    lazy.Assert.equal(typeof update.autoDownload, "boolean");
    lazy.Assert.equal(typeof update.background, "boolean");
    lazy.Assert.equal(
      update.channel,
      Glean.updateSettings.channel.testGetValue()
    );
    lazy.Assert.equal(
      update.enabled,
      Glean.updateSettings.enabled.testGetValue()
    );
    lazy.Assert.equal(
      update.autoDownload,
      Glean.updateSettings.autoDownload.testGetValue()
    );
    lazy.Assert.equal(
      update.background,
      Glean.updateSettings.background.testGetValue()
    );

    // Check sandbox settings exist and make sense
    if (data.settings.sandbox.effectiveContentProcessLevel !== null) {
      lazy.Assert.equal(
        typeof data.settings.sandbox.effectiveContentProcessLevel,
        "number",
        "sandbox.effectiveContentProcessLevel must have the correct type"
      );
      lazy.Assert.equal(
        data.settings.sandbox.effectiveContentProcessLevel,
        Glean.sandbox.effectiveContentProcessLevel.testGetValue()
      );
    }

    if (data.settings.sandbox.contentWin32kLockdownState !== null) {
      lazy.Assert.equal(
        typeof data.settings.sandbox.contentWin32kLockdownState,
        "number",
        "sandbox.contentWin32kLockdownState must have the correct type"
      );

      let win32kLockdownState =
        data.settings.sandbox.contentWin32kLockdownState;
      lazy.Assert.ok(win32kLockdownState >= 1 && win32kLockdownState <= 17);

      lazy.Assert.equal(
        win32kLockdownState,
        Glean.sandbox.contentWin32kLockdownState.testGetValue()
      );
    }

    // Check "defaultSearchEngine" separately, as it can either be undefined or string.
    if ("defaultSearchEngine" in data.settings) {
      this.checkString(data.settings.defaultSearchEngine);
      lazy.Assert.equal(typeof data.settings.defaultSearchEngineData, "object");
    }

    if ("defaultPrivateSearchEngineData" in data.settings) {
      lazy.Assert.equal(
        typeof data.settings.defaultPrivateSearchEngineData,
        "object"
      );
    }

    if ((gIsWindows || gIsMac) && AppConstants.MOZ_BUILD_APP == "browser") {
      lazy.Assert.equal(typeof data.settings.attribution, "object");
      lazy.Assert.equal(data.settings.attribution.source, "google.com");
      lazy.Assert.equal(data.settings.attribution.dlsource, "unittest");
      let attr = Services.fog.testGetAttribution();
      lazy.Assert.equal(
        attr.source,
        "google.com",
        "Must have correct attribution.source."
      );
      let attrExt = Glean.gleanAttribution.ext.testGetValue();
      lazy.Assert.equal(
        attrExt.dlsource,
        "unittest",
        "Must have correct dlsource."
      );
    }

    this.checkIntlSettings(data.settings);
  },

  checkIntlSettings({ intl }) {
    let fields = [
      "requestedLocales",
      "availableLocales",
      "appLocales",
      "acceptLanguages",
    ];

    for (let field of fields) {
      lazy.Assert.ok(Array.isArray(intl[field]), `${field} is an array`);
      lazy.Assert.deepEqual(intl[field], Glean.intl[field].testGetValue());
    }

    // These fields may be null if they aren't ready yet. This is mostly to deal
    // with test failures on Android, but they aren't guaranteed to exist.
    let optionalFields = ["systemLocales", "regionalPrefsLocales"];

    for (let field of optionalFields) {
      let isArray = Array.isArray(intl[field]);
      let isNull = intl[field] === null;
      lazy.Assert.ok(isArray || isNull, `${field} is an array or null`);
      lazy.Assert.deepEqual(intl[field], Glean.intl[field].testGetValue());
    }
  },

  checkProfileSection(data) {
    lazy.Assert.ok(
      "profile" in data,
      "There must be a profile section in Environment."
    );
    lazy.Assert.equal(
      data.profile.creationDate,
      truncateToDays(PROFILE_CREATION_DATE_MS)
    );
    lazy.Assert.equal(
      data.profile.resetDate,
      truncateToDays(PROFILE_RESET_DATE_MS)
    );
    lazy.Assert.equal(
      data.profile.firstUseDate,
      truncateToDays(PROFILE_FIRST_USE_MS)
    );
    lazy.Assert.equal(
      data.profile.recoveredFromBackup,
      truncateToDays(PROFILE_RECOVERED_FROM_BACKUP)
    );
    lazy.Assert.equal(
      data.profile.creationDate,
      Glean.profiles.creationDate.testGetValue()
    );
    lazy.Assert.equal(
      data.profile.resetDate,
      Glean.profiles.resetDate.testGetValue()
    );
    lazy.Assert.equal(
      data.profile.firstUseDate,
      Glean.profiles.firstUseDate.testGetValue()
    );
    lazy.Assert.equal(
      data.profile.recoveredFromBackup,
      Glean.profiles.recoveredFromBackup.testGetValue()
    );
  },

  checkPartnerSection(data, isInitial) {
    const EXPECTED_FIELDS = {
      distributionId: DISTRIBUTION_ID,
      distributionVersion: DISTRIBUTION_VERSION,
      partnerId: PARTNER_ID,
      distributor: DISTRIBUTOR_NAME,
      distributorChannel: DISTRIBUTOR_CHANNEL,
    };

    lazy.Assert.ok(
      "partner" in data,
      "There must be a partner section in Environment."
    );

    let dist = Services.fog.testGetDistribution();
    let distExt = Glean.gleanDistribution.ext.testGetValue();
    for (let f in EXPECTED_FIELDS) {
      let expected = isInitial ? null : EXPECTED_FIELDS[f];
      lazy.Assert.strictEqual(
        data.partner[f],
        expected,
        f + " must have the correct value."
      );
      if (f == "distributionId") {
        lazy.Assert.strictEqual(
          dist.name,
          expected,
          "Core Glean distribution must be correct."
        );
      } else {
        lazy.Assert.equal(
          distExt[f],
          expected,
          `Extended Glean distribution field "${f}" must be correct.`
        );
      }
    }

    // Check that "partnerNames" exists and contains the correct element.
    lazy.Assert.ok(Array.isArray(data.partner.partnerNames));
    if (isInitial) {
      lazy.Assert.equal(data.partner.partnerNames.length, 0);
      lazy.Assert.equal(distExt.partnerNames, null);
    } else {
      lazy.Assert.ok(data.partner.partnerNames.includes(PARTNER_NAME));
      lazy.Assert.ok(
        distExt.partnerNames.includes(PARTNER_NAME),
        "Glean partner names contain expected partner name."
      );
    }
  },

  checkGfxAdapter(data) {
    const EXPECTED_ADAPTER_FIELDS_TYPES = {
      description: "string",
      vendorID: "string",
      deviceID: "string",
      subsysID: "string",
      RAM: "number",
      driver: "string",
      driverVendor: "string",
      driverVersion: "string",
      driverDate: "string",
      GPUActive: "boolean",
    };

    for (let f in EXPECTED_ADAPTER_FIELDS_TYPES) {
      lazy.Assert.ok(f in data, f + " must be available.");

      if (data[f]) {
        // Since we have a non-null value, check if it has the correct type.
        lazy.Assert.equal(
          typeof data[f],
          EXPECTED_ADAPTER_FIELDS_TYPES[f],
          f + " must have the correct type."
        );
      }
    }
  },

  checkSystemSection(data, assertProcessData) {
    const EXPECTED_FIELDS = [
      "memoryMB",
      "cpu",
      "os",
      "hdd",
      "gfx",
      "appleModelId",
    ];

    lazy.Assert.ok(
      "system" in data,
      "There must be a system section in Environment."
    );

    // Make sure we have all the top level sections and fields.
    for (let f of EXPECTED_FIELDS) {
      lazy.Assert.ok(f in data.system, f + " must be available.");
    }

    lazy.Assert.ok(
      Number.isFinite(data.system.memoryMB),
      "MemoryMB must be a number."
    );
    lazy.Assert.equal(data.system.memoryMB, Glean.system.memory.testGetValue());

    if (assertProcessData) {
      if (gIsWindows || gIsMac || gIsLinux) {
        let EXTRA_CPU_FIELDS = [
          "cores",
          "model",
          "family",
          "stepping",
          "l2cacheKB",
          "l3cacheKB",
          "speedMHz",
          "vendor",
          "name",
        ];

        for (let f of EXTRA_CPU_FIELDS) {
          // Note this is testing TelemetryEnvironment.js only, not that the
          // values are valid - null is the fallback.
          lazy.Assert.ok(
            f in data.system.cpu,
            f + " must be available under cpu."
          );
        }

        if (gIsWindows) {
          lazy.Assert.equal(
            typeof data.system.isWow64,
            "boolean",
            "isWow64 must be available on Windows and have the correct type."
          );
          lazy.Assert.equal(
            typeof data.system.isWowARM64,
            "boolean",
            "isWowARM64 must be available on Windows and have the correct type."
          );
          lazy.Assert.equal(
            typeof data.system.hasWinPackageId,
            "boolean",
            "hasWinPackageId must be available on Windows and have the correct type."
          );
          // This is only sent for Mozilla produced MSIX packages
          lazy.Assert.ok(
            !("winPackageFamilyName" in data.system) ||
              data.system.winPackageFamilyName === null ||
              typeof data.system.winPackageFamilyName === "string",
            "winPackageFamilyName must be a string if non null"
          );
          lazy.Assert.ok(
            "virtualMaxMB" in data.system,
            "virtualMaxMB must be available."
          );
          lazy.Assert.ok(
            Number.isFinite(data.system.virtualMaxMB),
            "virtualMaxMB must be a number."
          );
          lazy.Assert.equal(
            data.system.isWow64,
            Glean.system.isWow64.testGetValue()
          );
          lazy.Assert.equal(
            data.system.isWowARM64,
            Glean.system.isWowArm64.testGetValue()
          );
          lazy.Assert.equal(
            data.system.hasWinPackageId,
            Glean.system.hasWinPackageId.testGetValue()
          );
          if (data.system.winPackageFamilyName) {
            lazy.Assert.equal(
              data.system.winPackageFamilyName,
              Glean.system.winPackageFamilyName.testGetValue()
            );
          }
          lazy.Assert.equal(
            data.system.virtualMaxMB,
            Glean.system.virtualMemory.testGetValue()
          );

          for (let f of [
            "count",
            "model",
            "family",
            "stepping",
            "l2cacheKB",
            "l3cacheKB",
            "speedMHz",
          ]) {
            lazy.Assert.ok(
              Number.isFinite(data.system.cpu[f]),
              f + " must be a number if non null."
            );
          }
        }

        // These should be numbers if they are not null
        for (let f of [
          "count",
          "model",
          "family",
          "stepping",
          "l2cacheKB",
          "l3cacheKB",
          "speedMHz",
        ]) {
          lazy.Assert.ok(
            !(f in data.system.cpu) ||
              data.system.cpu[f] === null ||
              Number.isFinite(data.system.cpu[f]),
            f + " must be a number if non null."
          );
        }

        // We insist these are available
        for (let f of ["cores"]) {
          lazy.Assert.ok(
            !(f in data.system.cpu) || Number.isFinite(data.system.cpu[f]),
            f + " must be a number if non null."
          );
        }
      }
    }

    let cpuData = data.system.cpu;

    lazy.Assert.ok(
      Array.isArray(cpuData.extensions),
      "CPU extensions must be available."
    );
    lazy.Assert.deepEqual(
      cpuData.extensions,
      Glean.systemCpu.extensions.testGetValue()
    );

    let osData = data.system.os;
    lazy.Assert.ok(this.checkNullOrString(osData.name));
    lazy.Assert.ok(this.checkNullOrString(osData.version));
    lazy.Assert.ok(this.checkNullOrString(osData.locale));
    if (osData.name !== null) {
      lazy.Assert.equal(osData.name, Glean.systemOs.name.testGetValue());
    }
    if (osData.version !== null) {
      lazy.Assert.equal(osData.version, Glean.systemOs.version.testGetValue());
    }
    if (osData.locale !== null) {
      lazy.Assert.equal(osData.locale, Glean.systemOs.locale.testGetValue());
    }

    // Service pack is only available on Windows.
    if (gIsWindows) {
      lazy.Assert.ok(
        Number.isFinite(osData.servicePackMajor),
        "ServicePackMajor must be a number."
      );
      lazy.Assert.ok(
        Number.isFinite(osData.servicePackMinor),
        "ServicePackMinor must be a number."
      );
      lazy.Assert.equal(
        osData.servicePackMajor,
        Glean.systemOs.servicePackMajor.testGetValue()
      );
      lazy.Assert.equal(
        osData.servicePackMinor,
        Glean.systemOs.servicePackMinor.testGetValue()
      );
      if ("windowsBuildNumber" in osData) {
        // This might not be available on all Windows platforms.
        lazy.Assert.ok(
          Number.isFinite(osData.windowsBuildNumber),
          "windowsBuildNumber must be a number."
        );
        lazy.Assert.equal(
          osData.windowsBuildNumber,
          Glean.systemOs.windowsBuildNumber.testGetValue()
        );
      }
      if ("windowsUBR" in osData) {
        // This might not be available on all Windows platforms.
        lazy.Assert.ok(
          osData.windowsUBR === null || Number.isFinite(osData.windowsUBR),
          "windowsUBR must be null or a number."
        );
        lazy.Assert.equal(
          osData.windowsUBR,
          Glean.systemOs.windowsUbr.testGetValue()
        );
      }
    } else if (gIsLinux) {
      lazy.Assert.ok(this.checkNullOrString(osData.distro));
      lazy.Assert.ok(this.checkNullOrString(osData.distroVersion));
      lazy.Assert.equal(osData.distro, Glean.systemOs.distro.testGetValue());
      lazy.Assert.equal(
        osData.distroVersion,
        Glean.systemOs.distroVersion.testGetValue()
      );
    }

    for (let disk of EXPECTED_HDD_FIELDS) {
      let diskData = Glean.hdd[disk].testGetValue();
      lazy.Assert.ok(this.checkNullOrString(data.system.hdd[disk].model));
      lazy.Assert.ok(this.checkNullOrString(data.system.hdd[disk].revision));
      lazy.Assert.ok(this.checkNullOrString(data.system.hdd[disk].type));
      if (data.system.hdd[disk].model !== null) {
        lazy.Assert.equal(data.system.hdd[disk].model, diskData.model);
      }
      if (data.system.hdd[disk].revision !== null) {
        lazy.Assert.equal(data.system.hdd[disk].revision, diskData.revision);
      }
      if (data.system.hdd[disk].type !== null) {
        lazy.Assert.equal(data.system.hdd[disk].type, diskData.diskType);
      }
    }

    let gfxData = data.system.gfx;
    lazy.Assert.ok("D2DEnabled" in gfxData);
    lazy.Assert.equal(gfxData.D2DEnabled, Glean.gfx.d2dEnabled.testGetValue());
    lazy.Assert.ok("DWriteEnabled" in gfxData);
    lazy.Assert.equal(
      gfxData.DWriteEnabled,
      Glean.gfx.dwriteEnabled.testGetValue()
    );
    lazy.Assert.ok("Headless" in gfxData);
    lazy.Assert.equal(gfxData.Headless, Glean.gfx.headless.testGetValue());
    lazy.Assert.ok("TargetFrameRate" in gfxData);
    lazy.Assert.equal(typeof gfxData.TargetFrameRate, "number");
    lazy.Assert.equal(
      gfxData.TargetFrameRate,
      Glean.gfx.targetFrameRate.testGetValue()
    );
    lazy.Assert.ok("textScaleFactor" in gfxData);
    lazy.Assert.equal(
      gfxData.textScaleFactor,
      Glean.gfx.textScaleFactor.testGetValue()
    );
    if (gIsWindows) {
      lazy.Assert.equal(typeof gfxData.D2DEnabled, "boolean");
      lazy.Assert.equal(typeof gfxData.DWriteEnabled, "boolean");
    }

    lazy.Assert.ok("adapters" in gfxData);
    lazy.Assert.ok(
      !!gfxData.adapters.length,
      "There must be at least one GFX adapter."
    );
    for (let adapter of gfxData.adapters) {
      this.checkGfxAdapter(adapter);
    }
    lazy.Assert.equal(typeof gfxData.adapters[0].GPUActive, "boolean");
    lazy.Assert.ok(
      gfxData.adapters[0].GPUActive,
      "The first GFX adapter must be active."
    );
    const adapters = Glean.gfx.adapters.testGetValue();
    gfxData.adapters.forEach((adapter, index) => {
      for (const [k, v] of Object.entries(adapter)) {
        if (v === null) {
          // Glean doesn't bother with `null`s
          continue;
        }
        lazy.Assert.equal(v, adapters[index][k]);
      }
    });

    lazy.Assert.ok(Array.isArray(gfxData.monitors));
    if (gIsWindows || gIsMac || gIsLinux) {
      lazy.Assert.ok(
        gfxData.monitors.length >= 1,
        "There is at least one monitor."
      );
      lazy.Assert.equal(typeof gfxData.monitors[0].screenWidth, "number");
      lazy.Assert.equal(typeof gfxData.monitors[0].screenHeight, "number");
      lazy.Assert.equal(
        typeof gfxData.monitors[0].defaultCSSScaleFactor,
        "number"
      );
      lazy.Assert.equal(
        typeof gfxData.monitors[0].contentsScaleFactor,
        "number"
      );
      if (gIsWindows) {
        lazy.Assert.equal(typeof gfxData.monitors[0].refreshRate, "number");
        lazy.Assert.equal(typeof gfxData.monitors[0].pseudoDisplay, "boolean");
      }
    }
    lazy.Assert.deepEqual(gfxData.monitors, Glean.gfx.monitors.testGetValue());

    lazy.Assert.equal(typeof gfxData.features, "object");
    lazy.Assert.equal(typeof gfxData.features.compositor, "string");
    lazy.Assert.ok(!!Glean.gfxFeatures.compositor.testGetValue());

    lazy.Assert.equal(typeof gfxData.features.gpuProcess, "object");
    lazy.Assert.equal(typeof gfxData.features.gpuProcess.status, "string");
    lazy.Assert.ok(!!Glean.gfxFeatures.gpuProcess.testGetValue().status);

    try {
      // If we've not got nsIGfxInfoDebug, then this will throw and stop us doing
      // this test.
      let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(
        Ci.nsIGfxInfoDebug
      );

      if (gIsWindows || gIsMac) {
        lazy.Assert.equal(GFX_VENDOR_ID, gfxData.adapters[0].vendorID);
        lazy.Assert.equal(GFX_DEVICE_ID, gfxData.adapters[0].deviceID);
      }

      let features = gfxInfo.getFeatures();
      lazy.Assert.equal(features.compositor, gfxData.features.compositor);
      lazy.Assert.equal(
        features.gpuProcess.status,
        gfxData.features.gpuProcess.status
      );
      lazy.Assert.equal(
        features.compositor,
        Glean.gfxFeatures.compositor.testGetValue()
      );
      lazy.Assert.equal(
        features.gpuProcess.status,
        Glean.gfxFeatures.gpuProcess.testGetValue().status
      );
    } catch (e) {}

    if (gIsMac) {
      lazy.Assert.ok(this.checkString(data.system.appleModelId));
      lazy.Assert.equal(
        data.system.appleModelId,
        Glean.system.appleModelId.testGetValue()
      );
    } else {
      lazy.Assert.ok(this.checkNullOrString(data.system.appleModelId));
      lazy.Assert.equal(null, Glean.system.appleModelId.testGetValue());
    }

    // This feature is only available on Windows
    if (AppConstants.platform == "win") {
      lazy.Assert.ok(
        "sec" in data.system,
        "sec must be available under data.system"
      );

      let SEC_FIELDS = ["antivirus", "antispyware", "firewall"];
      for (let f of SEC_FIELDS) {
        let products = Glean.windowsSecurity[f].testGetValue();
        lazy.Assert.ok(
          f in data.system.sec,
          f + " must be available under data.system.sec"
        );

        let value = data.system.sec[f];
        // value is null on Windows Server
        lazy.Assert.ok(
          value === null || Array.isArray(value),
          f + " must be either null or an array"
        );
        if (Array.isArray(value)) {
          for (let product of value) {
            // It is posssible that this will fail if either the Legacy or
            // Glean string limits are hit. If the Glean string_list limits are
            // hit, `testGetValue` above will throw, though.
            lazy.Assert.ok(products.includes(product), `${f} data must match.`);
            lazy.Assert.equal(
              typeof product,
              "string",
              "Each element of " + f + " must be a string"
            );
          }
        }
      }
    }
  },

  checkActiveAddon(id, data, partialRecord) {
    let signedState = "number";
    // system add-ons have an undefined signState
    if (data.isSystem) {
      signedState = "undefined";
    }

    const EXPECTED_ADDON_FIELDS_TYPES = {
      version: "string",
      scope: "number",
      type: "string",
      updateDay: "number",
      isSystem: "boolean",
      isWebExtension: "boolean",
      multiprocessCompatible: "boolean",
    };

    const FULL_ADDON_FIELD_TYPES = {
      blocklisted: "boolean",
      name: "string",
      userDisabled: "boolean",
      appDisabled: "boolean",
      foreignInstall: "boolean",
      hasBinaryComponents: "boolean",
      installDay: "number",
      signedState,
    };

    let fields = EXPECTED_ADDON_FIELDS_TYPES;
    if (!partialRecord) {
      fields = Object.assign({}, fields, FULL_ADDON_FIELD_TYPES);
    }

    for (let [name, type] of Object.entries(fields)) {
      lazy.Assert.ok(name in data, name + " must be available.");
      lazy.Assert.equal(
        typeof data[name],
        type,
        name + " must have the correct type."
      );
    }

    // Retrieve the Glean `addons.activeAddons` from the test API
    let gleanData = Glean.addons.activeAddons.testGetValue();
    // gleanData has all of the addons in it so we need to find the right one
    let gleanObject = gleanData.find(entry => entry.id == id);
    // Check the Glean properties of `addons.activeAddons`
    for (let [field] of Object.entries(fields)) {
      // Glean cannot use "type" as a field name so it is named "addonType"
      // We account for that difference here in order to test the data
      let gleanField = field;
      if (field == "type") {
        gleanField = "addonType";
      }

      lazy.Assert.equal(
        data[field],
        gleanObject[gleanField],
        field + " must match what is recorded in Glean."
      );
    }

    if (!partialRecord) {
      // We check "description" separately, as it can be null.
      lazy.Assert.ok(this.checkNullOrString(data.description));
    }
  },

  checkTheme(data) {
    const EXPECTED_THEME_FIELDS_TYPES = {
      id: "string",
      blocklisted: "boolean",
      name: "string",
      userDisabled: "boolean",
      appDisabled: "boolean",
      version: "string",
      scope: "number",
      foreignInstall: "boolean",
      installDay: "number",
      updateDay: "number",
    };

    for (let f in EXPECTED_THEME_FIELDS_TYPES) {
      lazy.Assert.ok(f in data, f + " must be available.");
      lazy.Assert.equal(
        typeof data[f],
        EXPECTED_THEME_FIELDS_TYPES[f],
        f + " must have the correct type."
      );
    }

    // Retrieve the Glean `addons.theme` from the test API
    let gleanData = Glean.addons.theme.testGetValue();

    // Check the Glean properties of `addons.theme`
    for (let field in EXPECTED_THEME_FIELDS_TYPES) {
      lazy.Assert.equal(
        data[field],
        gleanData[field],
        field + " must match what is recorded in Glean."
      );
    }

    // We check "description" separately, as it can be null.
    lazy.Assert.ok(this.checkNullOrString(data.description));
  },

  checkActiveGMPlugin(data) {
    // GMP plugin version defaults to null until GMPDownloader runs to update it.
    if (data.version) {
      lazy.Assert.equal(typeof data.version, "string");
    }
    lazy.Assert.equal(typeof data.userDisabled, "boolean");
    lazy.Assert.equal(typeof data.applyBackgroundUpdates, "number");

    // Retrieve the Glean `addons.activeGMPlugins` from the test API
    let gleanData = Glean.addons.activeGMPlugins.testGetValue()[0];
    lazy.Assert.equal(data.version, gleanData.version);
    lazy.Assert.equal(data.userDisabled, gleanData.userDisabled);
    lazy.Assert.equal(
      data.applyBackgroundUpdates,
      gleanData.applyBackgroundUpdates
    );
  },

  checkAddonsSection(data, expectBrokenAddons, partialAddonsRecords) {
    const EXPECTED_FIELDS = ["activeAddons", "theme", "activeGMPlugins"];

    lazy.Assert.ok(
      "addons" in data,
      "There must be an addons section in Environment."
    );
    for (let f of EXPECTED_FIELDS) {
      lazy.Assert.ok(f in data.addons, f + " must be available.");
    }

    // Check the active addons, if available.
    if (!expectBrokenAddons) {
      let activeAddons = data.addons.activeAddons;
      for (let addon in activeAddons) {
        this.checkActiveAddon(addon, activeAddons[addon], partialAddonsRecords);
      }
    }

    // Check "theme" structure.
    if (Object.keys(data.addons.theme).length !== 0) {
      this.checkTheme(data.addons.theme);
    }

    // Check active GMPlugins
    let activeGMPlugins = data.addons.activeGMPlugins;
    for (let gmPlugin in activeGMPlugins) {
      this.checkActiveGMPlugin(activeGMPlugins[gmPlugin]);
    }
  },

  checkEnvironmentData(data, options = {}) {
    const {
      isInitial = false,
      expectBrokenAddons = false,
      assertProcessData = false,
    } = options;

    this.checkBuildSection(data);
    this.checkSettingsSection(data);
    this.checkProfileSection(data);
    this.checkPartnerSection(data, isInitial);
    this.checkSystemSection(data, assertProcessData);
    this.checkAddonsSection(data, expectBrokenAddons);
  },
};
