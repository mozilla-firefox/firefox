/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-check

/**
 * @typedef {import("perf").PageContext} PageContext
 * @typedef {import("perf").PerformancePref} PerformancePref
 * @typedef {import("perf").PrefObserver} PrefObserver
 * @typedef {import("perf").PrefPostfix} PrefPostfix
 * @typedef {import("perf").Presets} Presets
 * @typedef {import("perf").ProfilerViewMode} ProfilerViewMode
 * @typedef {import("perf").RecordingSettings} RecordingSettings
 */

/** @type {PerformancePref["Entries"]} */
const ENTRIES_PREF = "devtools.performance.recording.entries";
/** @type {PerformancePref["Interval"]} */
const INTERVAL_PREF = "devtools.performance.recording.interval";
/** @type {PerformancePref["Features"]} */
const FEATURES_PREF = "devtools.performance.recording.features";
/** @type {PerformancePref["Threads"]} */
const THREADS_PREF = "devtools.performance.recording.threads";
/** @type {PerformancePref["ObjDirs"]} */
const OBJDIRS_PREF = "devtools.performance.recording.objdirs";
/** @type {PerformancePref["Duration"]} */
const DURATION_PREF = "devtools.performance.recording.duration";
/** @type {PerformancePref["Preset"]} */
const PRESET_PREF = "devtools.performance.recording.preset";
/** @type {PerformancePref["PopupFeatureFlag"]} */
const POPUP_FEATURE_FLAG_PREF = "devtools.performance.popup.feature-flag";
/* This will be used to observe all profiler-related prefs. */
const PREF_PREFIX = "devtools.performance.recording.";

// The presets that we find in all interfaces are defined here.

// The property l10nIds contain all FTL l10n IDs for these cases:
// - properties in "popup" are used in the popup's select box.
// - properties in "devtools" are used in other UIs (about:profiling and devtools panels).
//
// Properties for both cases have the same values, but because they're not used
// in the same way we need to duplicate them.
// Their values for the en-US locale are in the files:
//   devtools/client/locales/en-US/perftools.ftl
//   browser/locales/en-US/browser/appmenu.ftl
//
// IMPORTANT NOTE: Please keep the existing profiler presets in sync with their
// Fenix counterparts and consider adding any new presets to Fenix:
// https://searchfox.org/firefox-main/rev/d87eb30d610a3032111f9ee47441b53927de63d3/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/perf/ProfilerUtils.kt

/** @type {Presets} */
export const presets = {
  "web-developer": {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: ["screenshots", "js", "memory"],
    threads: ["GeckoMain", "Compositor", "Renderer", "DOM Worker"],
    duration: 0,
    profilerViewMode: "active-tab",
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-web-developer-label",
        description: "profiler-popup-presets-web-developer-description",
      },
      devtools: {
        label: "perftools-presets-web-developer-label",
        description: "perftools-presets-web-developer-description",
      },
    },
  },
  "firefox-platform": {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: [
      "screenshots",
      "js",
      "stackwalk",
      "java",
      "processcpu",
      "memory",
    ],
    threads: [
      "GeckoMain",
      "Compositor",
      "Renderer",
      "SwComposite",
      "DOM Worker",
      // On-device machine learning threads:
      "onnx_worker",
      "llama.cpp",
      "Parakeet",
      "TextGenerator",
    ],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-firefox-label",
        description: "profiler-popup-presets-firefox-description",
      },
      devtools: {
        label: "perftools-presets-firefox-label",
        description: "perftools-presets-firefox-description",
      },
    },
  },
  graphics: {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: ["stackwalk", "js", "java", "processcpu", "memory"],
    threads: [
      "GeckoMain",
      "Compositor",
      "Renderer",
      "SwComposite",
      "RenderBackend",
      "GlyphRasterizer",
      "SceneBuilder",
      "WrWorker",
      "TextureUpdate",
      // Image decoding threads: most formats decode on the TaskController
      // thread pool, JPEG XL additionally uses its own pool.
      "TaskController",
      "JxlDecode",
    ],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-graphics-label",
        description: "profiler-popup-presets-graphics-description",
      },
      devtools: {
        label: "perftools-presets-graphics-label",
        description: "perftools-presets-graphics-description",
      },
    },
  },
  media: {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: [
      "js",
      "stackwalk",
      "audiocallbacktracing",
      "ipcmessages",
      "processcpu",
      "memory",
    ],
    threads: [
      "BackgroundThreadPool",
      "Compositor",
      "DOM Worker",
      "GeckoMain",
      "IPDL Background",
      "InotifyEventThread",
      "ModuleProcessThread",
      "PacerThread",
      "RemVidChild",
      "RenderBackend",
      "Renderer",
      "Socket Thread",
      "SwComposite",
      "TextureUpdate",
      "audio",
      "camera",
      "capture",
      "cubeb",
      "decoder",
      "gmp",
      "graph",
      "grph",
      "media",
      "webrtc",
    ],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-media-label",
        description: "profiler-popup-presets-media-description2",
      },
      devtools: {
        label: "perftools-presets-media-label",
        description: "perftools-presets-media-description2",
      },
    },
  },
  ml: {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: ["js", "stackwalk", "ipcmessages", "processcpu", "memory"],
    threads: [
      "BackgroundThreadPool",
      "DOM Worker",
      "GeckoMain",
      "IPDL Background",
      "onnx_worker",
      "llama.cpp",
      "Parakeet",
      "TextGenerator",
    ],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-ml-label",
        description: "profiler-popup-presets-ml-description",
      },
      devtools: {
        label: "perftools-presets-ml-label",
        description: "perftools-presets-ml-description2",
      },
    },
  },
  networking: {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: [
      "screenshots",
      "js",
      "stackwalk",
      "java",
      "processcpu",
      "bandwidth",
      "memory",
    ],
    threads: [
      "Cache2 I/O",
      "Compositor",
      "DNS Resolver",
      "DOM Worker",
      "GeckoMain",
      "Renderer",
      "Socket Thread",
      "StreamTrans",
      "SwComposite",
      "TRR Background",
    ],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-networking-label",
        description: "profiler-popup-presets-networking-description",
      },
      devtools: {
        label: "perftools-presets-networking-label",
        description: "perftools-presets-networking-description",
      },
    },
  },
  "networking-with-logs": {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: [
      "screenshots",
      "js",
      "stackwalk",
      "cpu",
      "java",
      "processcpu",
      "bandwidth",
      "memory",
    ],
    threads: [
      "Cache2 I/O",
      "Compositor",
      "DNS Resolver",
      "DOM Worker",
      "GeckoMain",
      "Renderer",
      "Socket Thread",
      "StreamTrans",
      "SwComposite",
      "TRR Background",
    ],
    // Keep these MOZ_LOG modules in sync with the "networking" logging preset
    // in toolkit/content/aboutLogging/aboutLogging.mjs (the profiler does not
    // need the "timestamp" and "sync" modules used there).
    mozLogs:
      "nsHttp:5,cache2:5,nsSocketTransport:5,nsHostResolver:5,EarlyHint:5",
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-networking-with-logs-label",
        description: "profiler-popup-presets-networking-with-logs-description",
      },
      devtools: {
        label: "perftools-presets-networking-with-logs-label",
        description: "perftools-presets-networking-with-logs-description",
      },
    },
  },
  power: {
    entries: 128 * 1024 * 1024,
    interval: 10,
    features: [
      "screenshots",
      "js",
      "stackwalk",
      "processcpu",
      "nostacksampling",
      "ipcmessages",
      "markersallthreads",
      "power",
      "bandwidth",
      "memory",
    ],
    threads: ["GeckoMain", "Renderer"],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-power-label",
        description: "profiler-popup-presets-power-description",
      },
      devtools: {
        label: "perftools-presets-power-label",
        description: "perftools-presets-power-description",
      },
    },
  },
  debug: {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: [
      "ipcmessages",
      "js",
      "markersallthreads",
      "processcpu",
      "samplingallthreads",
      "stackwalk",
      "unregisteredthreads",
      "flows",
    ],
    threads: ["*"],
    duration: 0,
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-debug-label",
        description: "profiler-popup-presets-debug-description",
      },
      devtools: {
        label: "perftools-presets-debug-label",
        description: "perftools-presets-debug-description",
      },
    },
  },
  "web-compat": {
    entries: 128 * 1024 * 1024,
    interval: 1,
    features: ["screenshots", "js", "stackwalk", "nostacksampling", "tracing"],
    threads: ["GeckoMain", "DOM Worker"],
    mozLogs: "console: 5, PageMessages: 5",
    duration: 0,
    profilerViewMode: "active-tab",
    l10nIds: {
      popup: {
        label: "profiler-popup-presets-web-compat-label",
        description: "profiler-popup-presets-web-compat-description",
      },
      devtools: {
        label: "perftools-presets-web-compat-label",
        description: "perftools-presets-web-compat-description",
      },
    },
  },
};

/**
 * @param {string} prefName
 * @return {string[]}
 */
function _getArrayOfStringsPref(prefName) {
  const text = Services.prefs.getCharPref(prefName);
  return JSON.parse(text);
}

/**
 * The profiler recording workflow uses two different pref paths. One set of prefs
 * is stored for local profiling, and another for remote profiling. This function
 * decides which to use. The remote prefs have ".remote" appended to the end of
 * their pref names.
 *
 * @param {PageContext} pageContext
 * @returns {PrefPostfix}
 */
export function getPrefPostfix(pageContext) {
  switch (pageContext) {
    case "devtools":
    case "aboutprofiling":
      // Don't use any postfix on the prefs.
      return "";
    case "devtools-remote":
    case "aboutprofiling-remote":
      return ".remote";
    case "aboutlogging":
      // about:logging uses its own set of recording prefs, so that using it
      // doesn't clobber the settings used for normal profiling (the popup,
      // about:profiling).
      return ".aboutlogging";
    default: {
      const { UnhandledCaseError } = ChromeUtils.importESModule(
        "resource://devtools/shared/performance-new/errors.sys.mjs",
        { global: "contextual" }
      );
      throw new UnhandledCaseError(pageContext, "Page Context");
    }
  }
}

/**
 * @param {string[]} objdirs
 */
function setObjdirPrefValue(objdirs) {
  Services.prefs.setCharPref(OBJDIRS_PREF, JSON.stringify(objdirs));
}

/**
 * Before Firefox 92, the objdir lists for local and remote profiling were
 * stored in separate lists. In Firefox 92 those two prefs were merged into
 * one. This function performs the migration.
 */
function migrateObjdirsPrefsIfNeeded() {
  const OLD_REMOTE_OBJDIRS_PREF = OBJDIRS_PREF + ".remote";
  const remoteString = Services.prefs.getCharPref(OLD_REMOTE_OBJDIRS_PREF, "");
  if (remoteString === "") {
    // No migration necessary.
    return;
  }

  const remoteList = JSON.parse(remoteString);
  const localList = _getArrayOfStringsPref(OBJDIRS_PREF);

  // Merge the two lists, eliminating any duplicates.
  const mergedList = [...new Set(localList.concat(remoteList))];
  setObjdirPrefValue(mergedList);
  Services.prefs.clearUserPref(OLD_REMOTE_OBJDIRS_PREF);
}

/**
 * @returns {string[]}
 */
export function getObjdirPrefValue() {
  migrateObjdirsPrefsIfNeeded();
  return _getArrayOfStringsPref(OBJDIRS_PREF);
}

/**
 * @param {string[]} supportedFeatures
 * @param {string[]} objdirs
 * @param {PrefPostfix} prefPostfix
 * @return {RecordingSettings}
 */
export function getRecordingSettingsFromPrefs(
  supportedFeatures,
  objdirs,
  prefPostfix
) {
  // If you add a new preference here, please do not forget to update
  // `revertRecordingSettings` as well.

  const entries = Services.prefs.getIntPref(ENTRIES_PREF + prefPostfix);
  const intervalInMicroseconds = Services.prefs.getIntPref(
    INTERVAL_PREF + prefPostfix
  );
  const interval = intervalInMicroseconds / 1000;
  const features = _getArrayOfStringsPref(FEATURES_PREF + prefPostfix);
  const threads = _getArrayOfStringsPref(THREADS_PREF + prefPostfix);
  const duration = Services.prefs.getIntPref(DURATION_PREF + prefPostfix);

  return {
    presetName: "custom",
    entries,
    interval,
    // Validate the features before passing them to the profiler.
    features: features.filter(feature => supportedFeatures.includes(feature)),
    threads,
    objdirs,
    duration,
  };
}

/**
 * @param {PageContext} pageContext
 * @param {RecordingSettings} prefs
 */
export function setRecordingSettings(pageContext, prefs) {
  const prefPostfix = getPrefPostfix(pageContext);
  Services.prefs.setCharPref(PRESET_PREF + prefPostfix, prefs.presetName);
  Services.prefs.setIntPref(ENTRIES_PREF + prefPostfix, prefs.entries);
  // The interval pref stores the value in microseconds for extra precision.
  const intervalInMicroseconds = prefs.interval * 1000;
  Services.prefs.setIntPref(
    INTERVAL_PREF + prefPostfix,
    intervalInMicroseconds
  );
  Services.prefs.setCharPref(
    FEATURES_PREF + prefPostfix,
    JSON.stringify(prefs.features)
  );
  Services.prefs.setCharPref(
    THREADS_PREF + prefPostfix,
    JSON.stringify(prefs.threads)
  );
  setObjdirPrefValue(prefs.objdirs);
}

/**
 * Revert the recording prefs for both local and remote profiling.
 *
 * @return {void}
 */
export function revertRecordingSettings() {
  for (const prefPostfix of ["", ".remote", ".aboutlogging"]) {
    Services.prefs.clearUserPref(PRESET_PREF + prefPostfix);
    Services.prefs.clearUserPref(ENTRIES_PREF + prefPostfix);
    Services.prefs.clearUserPref(INTERVAL_PREF + prefPostfix);
    Services.prefs.clearUserPref(FEATURES_PREF + prefPostfix);
    Services.prefs.clearUserPref(THREADS_PREF + prefPostfix);
    Services.prefs.clearUserPref(DURATION_PREF + prefPostfix);
  }
  Services.prefs.clearUserPref(OBJDIRS_PREF);
  Services.prefs.clearUserPref(POPUP_FEATURE_FLAG_PREF);
}

/**
 * Add an observer for the profiler-related preferences.
 *
 * @param {PrefObserver} observer
 * @return {void}
 */
export function addPrefObserver(observer) {
  Services.prefs.addObserver(PREF_PREFIX, observer);
}

/**
 * Removes an observer for the profiler-related preferences.
 *
 * @param {PrefObserver} observer
 * @return {void}
 */
export function removePrefObserver(observer) {
  Services.prefs.removeObserver(PREF_PREFIX, observer);
}
/**
 * Return the proper view mode for the Firefox Profiler front-end timeline by
 * looking at the proper preset that is selected.
 * Return value can be undefined when the preset is unknown or custom.
 *
 * @param {PageContext} pageContext
 * @return {ProfilerViewMode | undefined}
 */
export function getProfilerViewModeForCurrentPreset(pageContext) {
  const prefPostfix = getPrefPostfix(pageContext);
  const presetName = Services.prefs.getCharPref(PRESET_PREF + prefPostfix);

  if (presetName === "custom") {
    return undefined;
  }

  const preset = presets[presetName];
  if (!preset) {
    console.error(`Unknown profiler preset was encountered: "${presetName}"`);
    return undefined;
  }
  return preset.profilerViewMode;
}

/**
 * @param {string} presetName
 * @param {string[]} supportedFeatures
 * @param {string[]} objdirs
 * @return {RecordingSettings | null}
 */
export function getRecordingSettingsFromPreset(
  presetName,
  supportedFeatures,
  objdirs
) {
  if (presetName === "custom") {
    return null;
  }

  const preset = presets[presetName];
  if (!preset) {
    console.error(`Unknown profiler preset was encountered: "${presetName}"`);
    return null;
  }

  return {
    presetName,
    entries: preset.entries,
    interval: preset.interval,
    // Validate the features before passing them to the profiler.
    features: preset.features.filter(feature =>
      supportedFeatures.includes(feature)
    ),
    threads: preset.threads,
    mozLogs: preset.mozLogs,
    objdirs,
    duration: preset.duration,
  };
}

/**
 * @param {PageContext} pageContext
 * @param {string[]} supportedFeatures
 * @returns {RecordingSettings}
 */
export function getRecordingSettings(pageContext, supportedFeatures) {
  const objdirs = getObjdirPrefValue();
  const prefPostfix = getPrefPostfix(pageContext);
  const presetName = Services.prefs.getCharPref(PRESET_PREF + prefPostfix);

  // First try to get the values from a preset. If the preset is "custom" or
  // unrecognized, getRecordingSettingsFromPreset will return null and we will
  // get the settings from individual prefs instead.
  return (
    getRecordingSettingsFromPreset(presetName, supportedFeatures, objdirs) ??
    getRecordingSettingsFromPrefs(supportedFeatures, objdirs, prefPostfix)
  );
}

/**
 * Change the prefs based on a preset. This mechanism is used by the popup to
 * easily switch between different settings.
 *
 * @param {string} presetName
 * @param {PageContext} pageContext
 * @param {string[]} supportedFeatures
 * @return {void}
 */
export function changePreset(pageContext, presetName, supportedFeatures) {
  const prefPostfix = getPrefPostfix(pageContext);
  const objdirs = getObjdirPrefValue();
  let recordingSettings = getRecordingSettingsFromPreset(
    presetName,
    supportedFeatures,
    objdirs
  );

  if (!recordingSettings) {
    // No recordingSettings were found for that preset. Most likely this means this
    // is a custom preset, or it's one that we dont recognize for some reason.
    // Get the preferences from the individual preference values.
    Services.prefs.setCharPref(PRESET_PREF + prefPostfix, presetName);
    recordingSettings = getRecordingSettingsFromPrefs(
      supportedFeatures,
      objdirs,
      prefPostfix
    );
  }

  setRecordingSettings(pageContext, recordingSettings);
}
