/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AddonManager } from "resource://gre/modules/AddonManager.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PDFJS_VERSION: "resource://pdf.js/PdfJsVersion.sys.mjs",
  PlacesDBUtils: "resource://gre/modules/PlacesDBUtils.sys.mjs",
});

// We use a list of prefs for display to make sure we only show prefs that
// are useful for support and won't compromise the user's privacy.  Note that
// entries are *prefixes*: for example, "accessibility." applies to all prefs
// under the "accessibility.*" branch.  To exclude entries, add them to
// PREF_REGEXES_NOT_TO_DISPLAY or PREFS_UNIMPORTANT_LOCKED.
const PREFS_FOR_DISPLAY = [
  "accessibility.",
  "apz.",
  "browser.cache.",
  "browser.contentblocking.category",
  "browser.contentanalysis.",
  "browser.display.",
  "browser.download.always_ask_before_handling_new_types",
  "browser.download.enable_spam_prevention",
  "browser.download.folderList",
  "browser.download.improvements_to_download_panel",
  "browser.download.lastDir.savePerSite",
  "browser.download.manager.addToRecentDocs",
  "browser.download.manager.resumeOnWakeDelay",
  "browser.download.open_pdf_attachments_inline",
  "browser.download.preferred.",
  "browser.download.skipConfirmLaunchExecutable",
  "browser.download.start_downloads_in_tmp_dir",
  "browser.download.useDownloadDir",
  "browser.fixup.",
  "browser.history_expire_",
  "browser.link.open_newwindow",
  "browser.places.",
  "browser.privatebrowsing.",
  "browser.search.context.loadInBackground",
  "browser.search.lastEngineIgnored",
  "browser.search.lastSettingsCorruptTime",
  "browser.search.log",
  "browser.search.openintab",
  "browser.search.param",
  "browser.search.region",
  "browser.search.searchEnginesURL",
  "browser.search.suggest.enabled",
  "browser.search.update",
  "browser.sessionstore.",
  "browser.startup.homepage",
  "browser.startup.page",
  "browser.tabs.",
  "browser.theme.",
  "browser.toolbars.",
  "browser.urlbar.",
  "browser.zoom.",
  "doh-rollout.",
  "dom.",
  "extensions.backgroundServiceWorker.enabled",
  "extensions.checkCompatibility",
  "extensions.eventPages.enabled",
  "extensions.formautofill.",
  "extensions.lastAppVersion",
  "extensions.quarantinedDomains.enabled",
  "extensions.InstallTrigger.enabled",
  "fission.autostart",
  "font.",
  "general.autoScroll",
  "general.useragent.",
  "gfx.",
  "html5.",
  "identity.fxaccounts.enabled",
  "idle.",
  "image.",
  "javascript.",
  "keyword.",
  "layers.",
  "layout.css.",
  "layout.display-list.",
  "layout.frame_rate",
  "media.",
  "mousewheel.",
  "network.",
  "permissions.default.image",
  "places.",
  "plugin.",
  "plugins.",
  "privacy.",
  "security.",
  "services.sync.declinedEngines",
  "services.sync.lastPing",
  "services.sync.lastSync",
  "services.sync.numClients",
  "services.sync.engine.",
  "signon.",
  "storage.vacuum.last.",
  "svg.",
  "toolkit.startup.recent_crashes",
  "ui.osk.enabled",
  "ui.osk.detect_physical_keyboard",
  "ui.osk.require_tablet_mode",
  "ui.osk.debug.keyboardDisplayReason",
  "webgl.",
  "widget.dmabuf",
  "widget.use-xdg-desktop-portal",
  "widget.use-xdg-desktop-portal.file-picker",
  "widget.use-xdg-desktop-portal.mime-handler",
  "widget.gtk.overlay-scrollbars.enabled",
  "widget.wayland",
];

// The list of prefs we don't display, unlike the list of prefs for display,
// is a list of regular expressions.
const PREF_REGEXES_NOT_TO_DISPLAY = [
  /^browser[.]fixup[.]domainwhitelist[.]/,
  /^dom[.]push[.]userAgentID/,
  /^media[.]webrtc[.]debug[.]aec_log_dir/,
  /^media[.]webrtc[.]debug[.]log_file/,
  /^print[.].*print_to_filename$/,
  /^network[.]proxy[.]/,
];

// Table of getters for various preference types.
const PREFS_GETTERS = {};

PREFS_GETTERS[Ci.nsIPrefBranch.PREF_STRING] = (prefs, name) =>
  prefs.getStringPref(name);
PREFS_GETTERS[Ci.nsIPrefBranch.PREF_INT] = (prefs, name) =>
  prefs.getIntPref(name);
PREFS_GETTERS[Ci.nsIPrefBranch.PREF_BOOL] = (prefs, name) =>
  prefs.getBoolPref(name);

// List of unimportant locked prefs (won't be shown on the troubleshooting
// session). You only need to add prefs here if they are matched by
// PREFS_FOR_DISPLAY yet not by PREF_REGEXES_NOT_TO_DISPLAY.
const PREFS_UNIMPORTANT_LOCKED = [
  "dom.postMessage.sharedArrayBuffer.bypassCOOP_COEP.insecure.enabled",
  "extensions.backgroundServiceWorker.enabled",
  "privacy.restrict3rdpartystorage.url_decorations",
  "security.storage.encryption.sqlite.enabled",
];

function getPref(name) {
  let type = Services.prefs.getPrefType(name);
  if (!(type in PREFS_GETTERS)) {
    throw new Error("Unknown preference type " + type + " for " + name);
  }
  return PREFS_GETTERS[type](Services.prefs, name);
}

// Return the preferences filtered by PREF_REGEXES_NOT_TO_DISPLAY and PREFS_FOR_DISPLAY
// and also by the custom 'filter'-ing function.
function getPrefList(filter, allowlist = PREFS_FOR_DISPLAY) {
  return allowlist.reduce(function (prefs, branch) {
    Services.prefs.getChildList(branch).forEach(function (name) {
      if (
        filter(name) &&
        !PREF_REGEXES_NOT_TO_DISPLAY.some(re => re.test(name))
      ) {
        prefs[name] = getPref(name);
      }
    });
    return prefs;
  }, {});
}

export var Troubleshoot = {
  /**
   * Captures a snapshot of data that may help troubleshooters troubleshoot
   * trouble.
   *
   * @returns {Promise}
   *   A promise that is resolved with the snapshot data.
   */
  snapshot() {
    return new Promise(resolve => {
      let snapshot = {};
      let numPending = Object.keys(dataProviders).length;
      function providerDone(providerName, providerData) {
        snapshot[providerName] = providerData;
        if (--numPending == 0) {
          // Ensure that done is always and truly called asynchronously.
          Services.tm.dispatchToMainThread(() => resolve(snapshot));
        }
      }
      for (let name in dataProviders) {
        try {
          dataProviders[name](providerDone.bind(null, name));
        } catch (err) {
          let msg = "Troubleshoot data provider failed: " + name + "\n" + err;
          console.error(msg);
          providerDone(name, msg);
        }
      }
    });
  },

  kMaxCrashAge: 3 * 24 * 60 * 60 * 1000, // 3 days
};

// Each data provider is a name => function mapping.  When a snapshot is
// captured, each provider's function is called, and it's the function's job to
// generate the provider's data.  The function is passed a "done" callback, and
// when done, it must pass its data to the callback.  The resulting snapshot
// object will contain a name => data entry for each provider.
var dataProviders = {
  application: async function application(done) {
    let data = {
      name: Services.appinfo.name,
      osVersion:
        Services.sysinfo.getProperty("name") +
        " " +
        Services.sysinfo.getProperty("version") +
        " " +
        Services.sysinfo.getProperty("build"),
      version: AppConstants.MOZ_APP_VERSION_DISPLAY,
      buildID: Services.appinfo.appBuildID,
      distributionID: Services.prefs
        .getDefaultBranch("")
        .getCharPref("distribution.id", ""),
      userAgent: Cc["@mozilla.org/network/protocol;1?name=http"].getService(
        Ci.nsIHttpProtocolHandler
      ).userAgent,
      safeMode: Services.appinfo.inSafeMode,
      memorySizeBytes: Services.sysinfo.getProperty("memsize"),
      diskAvailableBytes: Services.dirsvc.get("ProfD", Ci.nsIFile)
        .diskSpaceAvailable,
    };

    if (Services.sysinfo.getProperty("name") == "Windows_NT") {
      if ((await Services.sysinfo.processInfo).isWindowsSMode) {
        data.osVersion += " S";
      }
    }

    if (AppConstants.MOZ_UPDATER) {
      data.updateChannel = ChromeUtils.importESModule(
        "resource://gre/modules/UpdateUtils.sys.mjs"
      ).UpdateUtils.UpdateChannel;
    }

    // eslint-disable-next-line mozilla/use-default-preference-values
    try {
      data.vendor = Services.prefs.getCharPref("app.support.vendor");
    } catch (e) {}
    try {
      data.supportURL = Services.urlFormatter.formatURLPref(
        "app.support.baseURL"
      );
    } catch (e) {}

    data.osTheme = Services.sysinfo.getProperty("osThemeInfo");

    try {
      // MacOSX: Check for rosetta status, if it exists
      data.rosetta = Services.sysinfo.getProperty("rosettaStatus");
    } catch (e) {}

    try {
      // Windows - Get info about attached pointing devices
      data.pointingDevices = [];
      if (Services.sysinfo.getProperty("hasMouse")) {
        data.pointingDevices.push("pointing-device-mouse");
      }
      if (Services.sysinfo.getProperty("hasTouch")) {
        data.pointingDevices.push("pointing-device-touchscreen");
      }
      if (Services.sysinfo.getProperty("hasPen")) {
        data.pointingDevices.push("pointing-device-pen-digitizer");
      }
      if (!data.pointingDevices.length) {
        data.pointingDevices.push("pointing-device-none");
      }
    } catch (e) {}

    data.numTotalWindows = 0;
    data.numFissionWindows = 0;
    data.numRemoteWindows = 0;
    for (let { docShell } of Services.wm.getEnumerator(
      AppConstants.platform == "android"
        ? "navigator:geckoview"
        : "navigator:browser"
    )) {
      docShell.QueryInterface(Ci.nsILoadContext);
      data.numTotalWindows++;
      if (docShell.useRemoteSubframes) {
        data.numFissionWindows++;
      }
      if (docShell.useRemoteTabs) {
        data.numRemoteWindows++;
      }
    }

    try {
      data.launcherProcessState = Services.appinfo.launcherProcessState;
    } catch (e) {}

    data.fissionAutoStart = Services.appinfo.fissionAutostart;
    data.fissionDecisionStatus = Services.appinfo.fissionDecisionStatusString;

    data.remoteAutoStart = Services.appinfo.browserTabsRemoteAutostart;

    if (Services.policies) {
      data.policiesStatus = Services.policies.status;
    }

    const keyLocationServiceGoogle = Services.urlFormatter
      .formatURL("%GOOGLE_LOCATION_SERVICE_API_KEY%")
      .trim();
    data.keyLocationServiceGoogleFound =
      keyLocationServiceGoogle != "no-google-location-service-api-key" &&
      !!keyLocationServiceGoogle.length;

    const keySafebrowsingGoogle = Services.urlFormatter
      .formatURL("%GOOGLE_SAFEBROWSING_API_KEY%")
      .trim();
    data.keySafebrowsingGoogleFound =
      keySafebrowsingGoogle != "no-google-safebrowsing-api-key" &&
      !!keySafebrowsingGoogle.length;

    const keyMozilla = Services.urlFormatter
      .formatURL("%MOZILLA_API_KEY%")
      .trim();
    data.keyMozillaFound =
      keyMozilla != "no-mozilla-api-key" && !!keyMozilla.length;

    done(data);
  },

  addons: async function addons(done) {
    let addons = await AddonManager.getAddonsByTypes([
      "extension",
      "locale",
      "dictionary",
      "sitepermission",
      "theme",
    ]);
    addons.sort(function (a, b) {
      if (a.isActive != b.isActive) {
        return b.isActive ? 1 : -1;
      }

      if (a.type != b.type) {
        return a.type.localeCompare(b.type);
      }

      // In some unfortunate cases add-on names can be null.
      let aname = a.name || "";
      let bname = b.name || "";
      let lc = aname.localeCompare(bname);
      if (lc != 0) {
        return lc;
      }
      if (a.version != b.version) {
        return a.version > b.version ? 1 : -1;
      }
      return 0;
    });
    let props = ["name", "type", "version", "isActive", "id", "locationName"];
    done(
      addons.map(function (ext) {
        return props.reduce(function (extData, prop) {
          extData[prop] = ext[prop];
          return extData;
        }, {});
      })
    );
  },

  securitySoftware: function securitySoftware(done) {
    let data = {};

    const keys = [
      "registeredAntiVirus",
      "registeredAntiSpyware",
      "registeredFirewall",
    ];
    for (let key of keys) {
      let prop = "";
      try {
        prop = Services.sysinfo.getProperty(key);
      } catch (e) {}

      data[key] = prop;
    }

    done(data);
  },

  processes: async function processes(done) {
    let remoteTypes = {};
    const processInfo = await ChromeUtils.requestProcInfo();
    for (let i = 0; i < processInfo.children.length; i++) {
      let remoteType;
      try {
        remoteType = processInfo.children[i].type;
        // Workaround for bug 1790070, since requestProcInfo refers to the preallocated content
        // process as "preallocated", and the localization string mapping expects "prealloc".
        remoteType = remoteType === "preallocated" ? "prealloc" : remoteType;
      } catch (e) {}

      // We will split Utility by actor name, so do not do it now
      if (remoteType === "utility") {
        continue;
      }

      // The parent process is also managed by the ppmm (because
      // of non-remote tabs), but it doesn't have a remoteType.
      if (!remoteType) {
        continue;
      }

      if (remoteTypes[remoteType]) {
        remoteTypes[remoteType]++;
      } else {
        remoteTypes[remoteType] = 1;
      }
    }

    for (let i = 0; i < processInfo.children.length; i++) {
      if (processInfo.children[i].type === "utility") {
        for (let utilityWithActor of processInfo.children[i].utilityActors.map(
          e => `utility_${e.actorName}`
        )) {
          if (remoteTypes[utilityWithActor]) {
            remoteTypes[utilityWithActor]++;
          } else {
            remoteTypes[utilityWithActor] = 1;
          }
        }
      }
    }

    try {
      let winUtils = Services.wm.getMostRecentWindow("").windowUtils;
      if (winUtils.gpuProcessPid != -1) {
        remoteTypes.gpu = 1;
      }
    } catch (e) {}

    let data = {
      remoteTypes,
      maxWebContentProcesses: Services.appinfo.maxWebProcessCount,
    };

    done(data);
  },

  async legacyUserStylesheets(done) {
    if (AppConstants.platform == "android") {
      done({ active: false, types: [] });
      return;
    }

    let active = Services.prefs.getBoolPref(
      "toolkit.legacyUserProfileCustomizations.stylesheets"
    );
    let types = [];
    for (let name of ["userChrome.css", "userContent.css"]) {
      let path = PathUtils.join(PathUtils.profileDir, "chrome", name);
      if (await IOUtils.exists(path)) {
        types.push(name);
      }
    }
    done({ active, types });
  },

  async environmentVariables(done) {
    if (AppConstants.platform == "android") {
      // Subprocess is not available.
      done({});
      return;
    }
    const { Subprocess } = ChromeUtils.importESModule(
      "resource://gre/modules/Subprocess.sys.mjs"
    );

    let environment = Subprocess.getEnvironment();
    let filteredEnvironment = {};
    // Limit the environment variables to those that we
    // know may affect Firefox to reduce leaking PII.
    let filteredEnvironmentKeys = ["xre_", "moz_", "gdk", "display"];
    let exactEnvironmentKeys = ["sslkeylogfile"];
    for (let key of Object.keys(environment)) {
      let lowerKey = key.toLowerCase();
      if (
        filteredEnvironmentKeys.some(k => lowerKey.startsWith(k)) ||
        exactEnvironmentKeys.includes(lowerKey)
      ) {
        filteredEnvironment[key] = environment[key];
      }
    }
    done(filteredEnvironment);
  },

  modifiedPreferences: function modifiedPreferences(done) {
    done(getPrefList(name => Services.prefs.prefHasUserValue(name)));
  },

  lockedPreferences: function lockedPreferences(done) {
    done(
      getPrefList(
        name =>
          !PREFS_UNIMPORTANT_LOCKED.includes(name) &&
          Services.prefs.prefIsLocked(name)
      )
    );
  },

  places: async function places(done) {
    const data = {};

    if (AppConstants.MOZ_PLACES) {
      data.prefs = await lazy.PlacesDBUtils.getEntitiesStatsAndCounts();

      data.lastMaintenanceDate =
        Services.prefs.getIntPref("places.database.lastMaintenance", 0) * 1000;
      data.lastVacuumDate =
        Services.prefs.getIntPref("storage.vacuum.last.places.sqlite", 0) *
        1000;

      try {
        const corruptFilePath = PathUtils.join(
          PathUtils.profileDir,
          "places.sqlite.corrupt"
        );
        const fileInfo = await IOUtils.stat(corruptFilePath);
        data.lastIntegrityCorruptionDate = fileInfo.lastModified;
      } catch (e) {
        // Set 0 if failed such the file not found error.
        data.lastIntegrityCorruptionDate = 0;
      }
    }

    done(data);
  },

  printingPreferences: function printingPreferences(done) {
    let filter = name => Services.prefs.prefHasUserValue(name);
    let prefs = getPrefList(filter, ["print."]);

    // print_printer is special and is the only pref that is outside of the
    // "print." branch... Maybe we should change it to print.printer or
    // something...
    if (filter("print_printer")) {
      prefs.print_printer = getPref("print_printer");
    }

    done(prefs);
  },

  graphics: function graphics(done) {
    function statusMsgForFeature(feature) {
      // We return an object because in the try-newer-driver case we need to
      // include the suggested version, which the consumer likely needs to plug
      // into a format string from a localization file. Rather than returning
      // a string in some cases and an object in others, return an object always.
      let msg = { key: "" };
      try {
        var status = gfxInfo.getFeatureStatusStr(feature);
      } catch (e) {}
      switch (status) {
        case "BLOCKED_DEVICE":
        case "DISCOURAGED":
          msg = { key: "blocked-gfx-card" };
          break;
        case "BLOCKED_OS_VERSION":
          msg = { key: "blocked-os-version" };
          break;
        case "BLOCKED_DRIVER_VERSION":
          try {
            var driverVersion =
              gfxInfo.getFeatureSuggestedDriverVersionStr(feature);
          } catch (e) {}
          msg = driverVersion
            ? { key: "try-newer-driver", args: { driverVersion } }
            : { key: "blocked-driver" };
          break;
        case "BLOCKED_MISMATCHED_VERSION":
          msg = { key: "blocked-mismatched-version" };
          break;
      }
      return msg;
    }

    let data = {};

    try {
      // nsIGfxInfo may not be implemented on some platforms.
      var gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);
    } catch (e) {}

    data.desktopEnvironment = Services.appinfo.desktopEnvironment;
    data.numTotalWindows = 0;
    data.numAcceleratedWindows = 0;

    let devicePixelRatios = [];

    for (let win of Services.ww.getWindowEnumerator()) {
      let winUtils = win.windowUtils;
      try {
        // NOTE: windowless browser's windows should not be reported in the graphics troubleshoot report
        if (
          winUtils.layerManagerType == "None" ||
          !winUtils.layerManagerRemote
        ) {
          continue;
        }
        devicePixelRatios.push(win.devicePixelRatio);

        data.numTotalWindows++;
        data.windowLayerManagerType = winUtils.layerManagerType;
        data.windowLayerManagerRemote = winUtils.layerManagerRemote;
      } catch (e) {
        continue;
      }
      if (data.windowLayerManagerType != "Basic") {
        data.numAcceleratedWindows++;
      }
    }
    data.graphicsDevicePixelRatios = devicePixelRatios;

    // If we had no OMTC windows, report back Basic Layers.
    if (!data.windowLayerManagerType) {
      data.windowLayerManagerType = "Basic";
      data.windowLayerManagerRemote = false;
    }

    if (!data.numAcceleratedWindows && gfxInfo) {
      let win = AppConstants.platform == "win";
      let feature = win ? "DIRECT3D_9_LAYERS" : "OPENGL_LAYERS";
      data.numAcceleratedWindowsMessage = statusMsgForFeature(feature);
    }

    if (gfxInfo) {
      // keys are the names of attributes on nsIGfxInfo, values become the names
      // of the corresponding properties in our data object.  A null value means
      // no change.  This is needed so that the names of properties in the data
      // object are the same as the names of keys in aboutSupport.properties.
      let gfxInfoProps = {
        adapterDescription: null,
        adapterVendorID: null,
        adapterDeviceID: null,
        adapterSubsysID: null,
        adapterRAM: null,
        adapterDriver: "adapterDrivers",
        adapterDriverVendor: "driverVendor",
        adapterDriverVersion: "driverVersion",
        adapterDriverDate: "driverDate",

        adapterDescription2: null,
        adapterVendorID2: null,
        adapterDeviceID2: null,
        adapterSubsysID2: null,
        adapterRAM2: null,
        adapterDriver2: "adapterDrivers2",
        adapterDriverVendor2: "driverVendor2",
        adapterDriverVersion2: "driverVersion2",
        adapterDriverDate2: "driverDate2",
        isGPU2Active: null,

        DWriteEnabled: "directWriteEnabled",
        DWriteVersion: "directWriteVersion",
        cleartypeParameters: "clearTypeParameters",
        TargetFrameRate: "targetFrameRate",
        windowProtocol: null,
        fontVisibilityDeterminationStr: "supportFontDetermination",
      };

      for (let prop in gfxInfoProps) {
        try {
          data[gfxInfoProps[prop] || prop] = gfxInfo[prop];
        } catch (e) {}
      }
    }

    let doc = new DOMParser().parseFromString("<html/>", "text/html");

    function GetWebGLInfo(data, keyPrefix, contextType) {
      data[keyPrefix + "Renderer"] = "-";
      data[keyPrefix + "Version"] = "-";
      data[keyPrefix + "DriverExtensions"] = "-";
      data[keyPrefix + "Extensions"] = "-";
      data[keyPrefix + "WSIInfo"] = "-";

      // //

      let canvas = doc.createElement("canvas");
      canvas.width = 1;
      canvas.height = 1;

      // //

      let creationError = null;

      canvas.addEventListener(
        "webglcontextcreationerror",

        function (e) {
          creationError = e.statusMessage;
        }
      );

      let gl = null;
      try {
        gl = canvas.getContext(contextType);
      } catch (e) {
        if (!creationError) {
          creationError = e.toString();
        }
      }
      if (!gl) {
        data[keyPrefix + "Renderer"] =
          creationError || "(no creation error info)";
        return;
      }

      // //

      data[keyPrefix + "Extensions"] = gl.getSupportedExtensions().join(" ");

      // //

      let ext = gl.getExtension("MOZ_debug");
      // This extension is unconditionally available to chrome. No need to check.
      let vendor = ext.getParameter(gl.VENDOR);
      let renderer = ext.getParameter(gl.RENDERER);

      data[keyPrefix + "Renderer"] = vendor + " -- " + renderer;
      data[keyPrefix + "Version"] = ext.getParameter(gl.VERSION);
      data[keyPrefix + "DriverExtensions"] = ext.getParameter(ext.EXTENSIONS);
      data[keyPrefix + "WSIInfo"] = ext.getParameter(ext.WSI_INFO);

      // //

      // Eagerly free resources.
      let loseExt = gl.getExtension("WEBGL_lose_context");
      if (loseExt) {
        loseExt.loseContext();
      }
    }

    GetWebGLInfo(data, "webgl1", "webgl");
    GetWebGLInfo(data, "webgl2", "webgl2");

    if (gfxInfo) {
      let infoInfo = gfxInfo.getInfo();
      if (infoInfo) {
        data.info = infoInfo;
      }

      let failureIndices = {};

      let failures = gfxInfo.getFailures(failureIndices);
      if (failures.length) {
        data.failures = failures;
        if (failureIndices.value.length == failures.length) {
          data.indices = failureIndices.value;
        }
      }

      data.featureLog = gfxInfo.getFeatureLog();
      data.crashGuards = gfxInfo.getActiveCrashGuards();
    }

    function getNavigator() {
      for (let win of Services.ww.getWindowEnumerator()) {
        let winUtils = win.windowUtils;
        try {
          // NOTE: windowless browser's windows should not be reported in the graphics troubleshoot report
          if (
            winUtils.layerManagerType == "None" ||
            !winUtils.layerManagerRemote
          ) {
            continue;
          }
          const nav = win.navigator;
          if (nav) {
            return nav;
          }
        } catch (e) {
          continue;
        }
      }
      throw new Error("No window had window.navigator.");
    }

    const navigator = getNavigator();

    async function GetWebgpuInfo(adapterOpts) {
      const ret = {};
      if (!navigator.gpu) {
        ret["navigator.gpu"] = null;
        return ret;
      }

      const requestAdapterkey = `navigator.gpu.requestAdapter(${JSON.stringify(
        adapterOpts
      )})`;

      let adapter;
      try {
        adapter = await navigator.gpu.requestAdapter(adapterOpts);
      } catch (e) {
        // If WebGPU isn't supported or is blocked somehow, include
        // that in the report. Anything else is an error which should
        // have consequences (test failures, etc).
        if (DOMException.isInstance(e) && e.name == "NotSupportedError") {
          return { [requestAdapterkey]: { not_supported: e.message } };
        }
        throw e;
      }

      if (!adapter) {
        ret[requestAdapterkey] = null;
        return ret;
      }
      const desc = (ret[requestAdapterkey] = {});

      desc.isFallbackAdapter = adapter.isFallbackAdapter;

      const adapterInfo = adapter.info;
      // We can't directly enumerate properties of instances of `GPUAdapterInfo`s, so use the prototype instead.
      const adapterInfoObj = {};
      for (const k of Object.keys(Object.getPrototypeOf(adapterInfo)).sort()) {
        adapterInfoObj[k] = adapterInfo[k];
      }
      desc.info = adapterInfoObj;

      desc.features = Array.from(adapter.features).sort();

      desc.limits = {};
      const keys = Object.keys(Object.getPrototypeOf(adapter.limits)).sort(); // limits not directly enumerable?
      for (const k of keys) {
        desc.limits[k] = adapter.limits[k];
      }

      return ret;
    }

    // Webgpu info is going to need awaits.
    (async () => {
      data.webgpuDefaultAdapter = await GetWebgpuInfo({});
      data.webgpuFallbackAdapter = await GetWebgpuInfo({
        forceFallbackAdapter: true,
      });

      done(data);
    })();
  },

  media: function media(done) {
    function convertDevices(devices) {
      if (!devices) {
        return undefined;
      }
      let infos = [];
      for (let i = 0; i < devices.length; ++i) {
        let device = devices.queryElementAt(i, Ci.nsIAudioDeviceInfo);
        infos.push({
          name: device.name,
          groupId: device.groupId,
          vendor: device.vendor,
          type: device.type,
          state: device.state,
          preferred: device.preferred,
          supportedFormat: device.supportedFormat,
          defaultFormat: device.defaultFormat,
          maxChannels: device.maxChannels,
          defaultRate: device.defaultRate,
          maxRate: device.maxRate,
          minRate: device.minRate,
          maxLatency: device.maxLatency,
          minLatency: device.minLatency,
        });
      }
      return infos;
    }

    let data = {};
    let winUtils = Services.wm.getMostRecentWindow("").windowUtils;
    data.currentAudioBackend = winUtils.currentAudioBackend;
    data.currentMaxAudioChannels = winUtils.currentMaxAudioChannels;
    data.currentPreferredSampleRate = winUtils.currentPreferredSampleRate;
    data.audioOutputDevices = convertDevices(
      winUtils
        .audioDevices(Ci.nsIDOMWindowUtils.AUDIO_OUTPUT)
        .QueryInterface(Ci.nsIArray)
    );
    data.audioInputDevices = convertDevices(
      winUtils
        .audioDevices(Ci.nsIDOMWindowUtils.AUDIO_INPUT)
        .QueryInterface(Ci.nsIArray)
    );

    data.codecSupportInfo = "Unknown";

    // We initialize gfxInfo here in the same way as in the media
    // section -- should we break this out into a separate function?
    try {
      // nsIGfxInfo may not be implemented on some platforms.
      var gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);

      // Note: CodecSupportInfo is not populated until we have
      // actually instantiated a PDM. We may want to add a button
      // or some other means of allowing the user to manually
      // instantiate a PDM to ensure that the data is available.
      data.codecSupportInfo = gfxInfo.CodecSupportInfo;
    } catch (e) {}

    done(data);
  },

  accessibility: function accessibility(done) {
    let data = {};
    data.isActive = Services.appinfo.accessibilityEnabled;
    // eslint-disable-next-line mozilla/use-default-preference-values
    try {
      data.forceDisabled = Services.prefs.getIntPref(
        "accessibility.force_disabled"
      );
    } catch (e) {}
    data.instantiator = Services.appinfo.accessibilityInstantiator;
    done(data);
  },

  startupCache: function startupCache(done) {
    const startupInfo = Cc["@mozilla.org/startupcacheinfo;1"].getService(
      Ci.nsIStartupCacheInfo
    );
    done({
      DiskCachePath: startupInfo.DiskCachePath,
      IgnoreDiskCache: startupInfo.IgnoreDiskCache,
      FoundDiskCacheOnInit: startupInfo.FoundDiskCacheOnInit,
      WroteToDiskCache: startupInfo.WroteToDiskCache,
    });
  },

  pdfjs: function pdfjs(done) {
    done({
      version: lazy.PDFJS_VERSION,
      enabled: !Services.prefs.getBoolPref("pdfjs.disabled", false),
      annotationEditorEnabled:
        Services.prefs.getIntPref("pdfjs.annotationEditorMode", 0) !== -1,
      enableXfa: Services.prefs.getBoolPref("pdfjs.enableXfa", true),
      openPdfAttachmentsInline: Services.prefs.getBoolPref(
        "browser.download.open_pdf_attachments_inline",
        false
      ),
    });
  },

  libraryVersions: function libraryVersions(done) {
    let data = {};
    let verInfo = Cc["@mozilla.org/security/nssversion;1"].getService(
      Ci.nsINSSVersion
    );
    for (let prop in verInfo) {
      let match = /^([^_]+)_((Min)?Version)$/.exec(prop);
      if (match) {
        let verProp = match[2][0].toLowerCase() + match[2].substr(1);
        data[match[1]] = data[match[1]] || {};
        data[match[1]][verProp] = verInfo[prop];
      }
    }
    done(data);
  },

  userJS: function userJS(done) {
    let userJSFile = Services.dirsvc.get("PrefD", Ci.nsIFile);
    userJSFile.append("user.js");
    done({
      exists: userJSFile.exists() && userJSFile.fileSize > 0,
    });
  },

  intl: function intl(done) {
    const osPrefs = Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    );
    done({
      localeService: {
        requested: Services.locale.requestedLocales,
        available: Services.locale.availableLocales,
        supported: Services.locale.appLocalesAsBCP47,
        regionalPrefs: Services.locale.regionalPrefsLocales,
        defaultLocale: Services.locale.defaultLocale,
      },
      osPrefs: {
        systemLocales: osPrefs.systemLocales,
        regionalPrefsLocales: osPrefs.regionalPrefsLocales,
      },
    });
  },

  contentAnalysis: async function contentAnalysis(done) {
    const contentAnalysis = Cc["@mozilla.org/contentanalysis;1"].getService(
      Ci.nsIContentAnalysis
    );
    if (!contentAnalysis.isActive) {
      done({ active: false });
      return;
    }
    let info = await contentAnalysis.getDiagnosticInfo();
    done({
      active: true,
      connected: info.connectedToAgent,
      agentPath: info.agentPath,
      failedSignatureVerification: info.failedSignatureVerification,
      requestCount: info.requestCount,
    });
  },

  async normandy(done) {
    if (AppConstants.MOZ_BUILD_APP !== "browser") {
      done();
      return;
    }

    const { ExperimentAPI } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );

    // Get Nimbus data in parallel, and sort each group by slug.
    const [nimbusExperiments, nimbusRollouts] = await Promise.all(
      [
        ExperimentAPI.manager.store
          .ready()
          .then(() => ExperimentAPI.manager.store.getAllActiveExperiments()),
        ExperimentAPI.manager.store
          .ready()
          .then(() => ExperimentAPI.manager.store.getAllActiveRollouts()),
      ].map(promise =>
        promise
          .catch(error => {
            console.error(error);
            return [];
          })
          .then(items => items.sort((a, b) => a.slug.localeCompare(b.slug)))
      )
    );

    done({
      nimbusExperiments,
      nimbusRollouts,
    });
  },

  async remoteSettings(done) {
    const { RemoteSettings } = ChromeUtils.importESModule(
      "resource://services-settings/remote-settings.sys.mjs"
    );

    let inspected;
    try {
      inspected = await RemoteSettings.inspect({ localOnly: true });
    } catch (error) {
      console.error(error);
      done({ isSynchronizationBroken: true, history: { "settings-sync": [] } });
      return;
    }

    // Show last check in standard format.
    inspected.lastCheck = inspected.lastCheck
      ? new Date(inspected.lastCheck * 1000).toISOString()
      : "";
    // Trim history entries.
    for (let h of Object.values(inspected.history)) {
      h.splice(10, Infinity);
    }

    done(inspected);
  },
};

if (AppConstants.MOZ_CRASHREPORTER) {
  dataProviders.crashes = function crashes(done) {
    const { CrashReports } = ChromeUtils.importESModule(
      "moz-src:///toolkit/crashreporter/CrashReports.sys.mjs"
    );
    let reports = CrashReports.getReports();
    let now = new Date();
    let reportsNew = reports.filter(
      report => now - report.date < Troubleshoot.kMaxCrashAge
    );
    let reportsSubmitted = reportsNew.filter(report => !report.pending);
    let reportsPendingCount = reportsNew.length - reportsSubmitted.length;
    let data = { submitted: reportsSubmitted, pending: reportsPendingCount };
    done(data);
  };
}

if (AppConstants.MOZ_SANDBOX) {
  dataProviders.sandbox = function sandbox(done) {
    let data = {};
    if (AppConstants.unixstyle == "linux") {
      const keys = [
        "hasSeccompBPF",
        "hasSeccompTSync",
        "hasPrivilegedUserNamespaces",
        "hasUserNamespaces",
        "canSandboxContent",
        "canSandboxMedia",
      ];

      for (let key of keys) {
        if (Services.sysinfo.hasKey(key)) {
          data[key] = Services.sysinfo.getPropertyAsBool(key);
        }
      }

      let reporter = Cc["@mozilla.org/sandbox/syscall-reporter;1"].getService(
        Ci.mozISandboxReporter
      );
      const snapshot = reporter.snapshot();
      let syscalls = [];
      for (let index = snapshot.begin; index < snapshot.end; ++index) {
        let report = snapshot.getElement(index);
        let { msecAgo, pid, tid, procType, syscall } = report;
        let args = [];
        for (let i = 0; i < report.numArgs; ++i) {
          args.push(report.getArg(i));
        }
        syscalls.push({ index, msecAgo, pid, tid, procType, syscall, args });
      }
      data.syscallLog = syscalls;
    }

    if (AppConstants.MOZ_SANDBOX) {
      let sandboxSettings = Cc[
        "@mozilla.org/sandbox/sandbox-settings;1"
      ].getService(Ci.mozISandboxSettings);
      data.contentSandboxLevel = Services.prefs.getIntPref(
        "security.sandbox.content.level"
      );
      data.effectiveContentSandboxLevel =
        sandboxSettings.effectiveContentSandboxLevel;

      if (AppConstants.platform == "win") {
        data.contentWin32kLockdownState =
          sandboxSettings.contentWin32kLockdownStateString;

        data.supportSandboxGpuLevel = Services.prefs.getIntPref(
          "security.sandbox.gpu.level"
        );
      }
    }

    done(data);
  };
}

if (AppConstants.ENABLE_WEBDRIVER) {
  dataProviders.remoteAgent = function remoteAgent(done) {
    const { RemoteAgent } = ChromeUtils.importESModule(
      "chrome://remote/content/components/RemoteAgent.sys.mjs"
    );
    const { running, scheme, host, port } = RemoteAgent;
    let url = "";
    if (running) {
      url = `${scheme}://${host}:${port}/`;
    }
    done({ running, url });
  };
}
