/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AttributionCode:
    "moz-src:///browser/components/attribution/AttributionCode.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TelemetryArchive: "resource://gre/modules/TelemetryArchive.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
});

function getOsVersion() {
  let version = null;
  try {
    version = Services.sysinfo.getProperty("version", null);
  } catch (_e) {
    // getProperty can throw if the version does not exist
  }
  if (version) {
    version = version.toString();
  }
  return version;
}

/**
 * Create an object that provides general information about the client application.
 *
 * Components like Normandy RecipeRunner use this as part of the context for filter expressions,
 * so avoid adding non-getter functions as attributes, as filter expressions
 * cannot execute functions.
 *
 * Also note that, because filter expressions implicitly resolve promises, you
 * can add getter functions that return promises for async data.
 */
export class ClientEnvironmentBase {
  static get country() {
    // The home region can be null and is updated regularly.
    return lazy.Region.home;
  }

  static get distribution() {
    return Services.prefs
      .getDefaultBranch(null)
      .getCharPref("distribution.id", "default");
  }

  static get formFactor() {
    // TODO: distinguish Firefox running on "tablet"
    return ["android", "ios"].includes(AppConstants.platform)
      ? "phone"
      : "desktop";
  }

  static get telemetry() {
    return (async () => {
      const pings = await lazy.TelemetryArchive.promiseArchivedPingList();

      // get most recent ping per type
      const mostRecentPings = {};
      for (const ping of pings) {
        if (ping.type in mostRecentPings) {
          if (
            mostRecentPings[ping.type].timestampCreated < ping.timestampCreated
          ) {
            mostRecentPings[ping.type] = ping;
          }
        } else {
          mostRecentPings[ping.type] = ping;
        }
      }

      const telemetry = {};
      for (const key in mostRecentPings) {
        const ping = mostRecentPings[key];
        telemetry[ping.type] =
          await lazy.TelemetryArchive.promiseArchivedPingById(ping.id);
      }
      return telemetry;
    })();
  }

  static get liveTelemetry() {
    // Construct a proxy object that forwards access to the main ping, and
    // throws errors for other ping types. The intent is to allow using
    // `telemetry` and `liveTelemetry` in similar ways, but to fail fast if
    // the wrong telemetry types are accessed.
    let target = {};
    try {
      target.main = lazy.TelemetryController.getCurrentPingData();
    } catch (err) {
      console.error(err);
    }

    return new Proxy(target, {
      get(target, prop) {
        if (prop == "main") {
          return target.main;
        }
        if (prop == "then") {
          // this isn't a Promise, but it's not a problem to check
          return undefined;
        }
        throw new Error(
          `Live telemetry only includes the main ping, not the ${prop} ping`
        );
      },
      has(target, prop) {
        return prop == "main";
      },
    });
  }

  // Note that we intend to replace usages of this with client_id in https://bugzilla.mozilla.org/show_bug.cgi?id=1542955
  static get legacyRandomizationId() {
    let id = Services.prefs.getCharPref("app.normandy.user_id", "");
    if (!id) {
      id = Services.uuid.generateUUID().toString().slice(1, -1);
      Services.prefs.setCharPref("app.normandy.user_id", id);
    }
    return id;
  }

  static get version() {
    return AppConstants.MOZ_APP_VERSION_DISPLAY;
  }

  static get channel() {
    return lazy.UpdateUtils.getUpdateChannel(false);
  }

  static get isDefaultBrowser() {
    return lazy.ShellService.isDefaultBrowserAsync();
  }

  static get searchEngine() {
    return (async () => {
      const defaultEngineInfo = await lazy.SearchService.getDefault();
      return defaultEngineInfo.telemetryId;
    })();
  }

  static get syncSetup() {
    return Services.prefs.prefHasUserValue("services.sync.username");
  }

  static get syncDesktopDevices() {
    return Services.prefs.getIntPref(
      "services.sync.clients.devices.desktop",
      0
    );
  }

  static get syncMobileDevices() {
    return Services.prefs.getIntPref("services.sync.clients.devices.mobile", 0);
  }

  static get syncTotalDevices() {
    return this.syncDesktopDevices + this.syncMobileDevices;
  }

  static get addons() {
    return (async () => {
      const addons = await lazy.AddonManager.getAllAddons();
      return addons.reduce((acc, addon) => {
        const {
          id,
          isActive,
          name,
          type,
          version,
          installDate: installDateN,
        } = addon;
        const installDate = new Date(installDateN);
        acc[id] = { id, isActive, name, type, version, installDate };
        return acc;
      }, {});
    })();
  }

  static get plugins() {
    return (async () => {
      const plugins = await lazy.AddonManager.getAddonsByTypes(["plugin"]);
      return plugins.reduce((acc, plugin) => {
        const { name, description, version } = plugin;
        acc[name] = { name, description, version };
        return acc;
      }, {});
    })();
  }

  static get locale() {
    return Services.locale.appLocaleAsBCP47;
  }

  static get doNotTrack() {
    return Services.prefs.getBoolPref(
      "privacy.donottrackheader.enabled",
      false
    );
  }

  static get os() {
    function coerceToNumber(version) {
      if (!version) {
        return null;
      }
      const parts = version.split(".");
      return parseFloat(parts.slice(0, 2).join("."));
    }

    let osInfo = {
      isWindows: AppConstants.platform == "win",
      isMac: AppConstants.platform === "macosx",
      isLinux: AppConstants.platform === "linux",

      get name() {
        // Reuse value from `appinfo` to avoid confusion (eg. 'win' vs. 'WINNT', 'android' vs 'Android')
        return ClientEnvironmentBase.appinfo.OS;
      },

      get version() {
        return getOsVersion();
      },

      get windowsVersion() {
        if (!osInfo.isWindows) {
          return null;
        }
        return coerceToNumber(getOsVersion());
      },

      /**
       * Gets the windows build number by querying the OS directly. The initial
       * version was copied from toolkit/components/telemetry/app/TelemetryEnvironment.sys.mjs
       *
       * @returns {number | null} The build number, or null on non-Windows platform or if there is an error.
       */
      get windowsBuildNumber() {
        if (!osInfo.isWindows) {
          return null;
        }

        let build = Number(Services.sysinfo.get("build"));
        return Number.isInteger(build) ? build : null;
      },

      /**
       * Gets the Windows Update Build Revision (UBR), the minor build number
       * shown alongside the build number (eg. the `3693` in `19045.3693`). Only
       * present on Windows 10 and later.
       *
       * @returns {number | null} The UBR, or null on non-Windows platforms,
       *    older Windows versions, or if there is an error.
       */
      get windowsUBR() {
        if (!osInfo.isWindows) {
          return null;
        }

        const ubr = lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
          "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
          "UBR",
          Ci.nsIWindowsRegKey.WOW64_64
        );
        return Number.isInteger(ubr) ? ubr : null;
      },

      get macVersion() {
        const darwinVersion = osInfo.darwinVersion;
        // Versions of OSX with Darwin < 5 don't follow this pattern
        if (darwinVersion >= 5) {
          // OSX 10.1 used Darwin 5, OSX 10.2 used Darwin 6, and so on.
          const intPart = Math.floor(darwinVersion);
          return 10 + 0.1 * (intPart - 4);
        }
        return null;
      },

      get darwinVersion() {
        if (!osInfo.isMac) {
          return null;
        }
        return coerceToNumber(getOsVersion());
      },

      // Version information on linux is a lot harder and a lot less useful, so
      // don't do anything about it here.
    };

    return osInfo;
  }

  static get attribution() {
    return lazy.AttributionCode.getAttrDataAsync();
  }

  static get appinfo() {
    Services.appinfo.QueryInterface(Ci.nsIXULAppInfo);
    Services.appinfo.QueryInterface(Ci.nsIPlatformInfo);
    return Services.appinfo;
  }
}
