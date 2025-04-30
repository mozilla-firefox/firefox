/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const gDashboard = Cc["@mozilla.org/network/dashboard;1"].getService(
  Ci.nsIDashboard
);

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "ProfilerPopupBackground", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/client/performance-new/shared/background.sys.mjs"
  );
});

ChromeUtils.defineLazyGetter(lazy, "ProfilerPrefsPresets", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/shared/performance-new/prefs-presets.sys.mjs"
  );
});

ChromeUtils.defineESModuleGetters(lazy, {
  ProfilerMenuButton:
    "resource://devtools/client/performance-new/popup/menu-button.sys.mjs",
  RecordingUtils:
    "resource://devtools/shared/performance-new/recording-utils.sys.mjs",
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
});

const $ = document.querySelector.bind(document);
const $$ = document.querySelectorAll.bind(document);

function moduleEnvVarPresent() {
  return Services.env.get("MOZ_LOG") || Services.env.get("NSPR_LOG");
}

/**
 * All the information associated with a logging presets:
 * - `modules` is the list of log modules and option, the same that would have
 *   been set as a MOZ_LOG environment variable
 * - l10nIds.label and l10nIds.description are the Ids of the strings that
 *   appear in the dropdown selector, and a one-liner describing the purpose of
 *   a particular logging preset
 * - profilerPreset is the name of a Firefox Profiler preset [1]. In general,
 *   the profiler preset will have the correct set of threads for a particular
 *   logging preset, so that all logging statements are recorded in the profile
 *   as markers.
 *
 * [1]: The keys of the `presets` object defined in
 * https://searchfox.org/mozilla-central/source/devtools/client/performance-new/shared/background.sys.mjs
 */

const gOsSpecificLoggingPresets = (() => {
  // Microsoft Windows
  if (navigator.platform.startsWith("Win")) {
    return {
      windows: {
        modules:
          "timestamp,sync,Widget:5,BaseWidget:5,WindowsEvent:4,TaskbarConcealer:5,FileDialog:5",
        l10nIds: {
          label: "about-logging-preset-windows-label",
          description: "about-logging-preset-windows-description",
        },
      },
    };
  }

  return {};
})();

const gLoggingPresets = {
  networking: {
    modules:
      "timestamp,sync,nsHttp:5,cache2:5,nsSocketTransport:5,nsHostResolver:5,EarlyHint:5",
    l10nIds: {
      label: "about-logging-preset-networking-label",
      description: "about-logging-preset-networking-description",
    },
    profilerPreset: "networking",
  },
  cookie: {
    modules: "timestamp,sync,nsHttp:5,cache2:5,cookie:5",
    l10nIds: {
      label: "about-logging-preset-networking-cookie-label",
      description: "about-logging-preset-networking-cookie-description",
    },
  },
  websocket: {
    modules:
      "timestamp,sync,nsHttp:5,nsWebSocket:5,nsSocketTransport:5,nsHostResolver:5",
    l10nIds: {
      label: "about-logging-preset-networking-websocket-label",
      description: "about-logging-preset-networking-websocket-description",
    },
  },
  http3: {
    modules:
      "timestamp,sync,nsHttp:5,nsSocketTransport:5,nsHostResolver:5,neqo_http3::*:5,neqo_transport::*:5",
    l10nIds: {
      label: "about-logging-preset-networking-http3-label",
      description: "about-logging-preset-networking-http3-description",
    },
  },
  "http3-upload-speed": {
    modules: "timestamp,neqo_transport::*:3",
    l10nIds: {
      label: "about-logging-preset-networking-http3-upload-speed-label",
      description:
        "about-logging-preset-networking-http3-upload-speed-description",
    },
  },
  "media-playback": {
    modules:
      "HTMLMediaElement:4,HTMLMediaElementEvents:4,cubeb:5,PlatformDecoderModule:5,AudioSink:5,AudioSinkWrapper:5,MediaDecoderStateMachine:4,MediaDecoder:4,MediaFormatReader:5,GMP:5,EME:5,MediaSource:5,MediaSourceSamples:5,Autoplay:5,MFMediaEngine:5",
    l10nIds: {
      label: "about-logging-preset-media-playback-label",
      description: "about-logging-preset-media-playback-description",
    },
    profilerPreset: "media",
  },
  /*
    Please update the WebRTC debugging docs with any changes to this preset.
    See: https://searchfox.org/mozilla-central/source/docs/contributing/debugging/debugging_webrtc_calls.rst
  */
  webrtc: {
    modules:
      "jsep:5,sdp:5,signaling:5,mtransport:5,nicer:5,RTCRtpReceiver:5,RTCRtpSender:5,RTCDMTFSender:5,WebrtcTCPSocket:5,CamerasChild:5,CamerasParent:5,VideoEngine:5,ShmemPool:5,TabShare:5,MediaChild:5,MediaParent:5,MediaManager:5,MediaTrackGraph:5,cubeb:5,MediaStream:5,MediaStreamTrack:5,DriftCompensator:5,ForwardInputTrack:5,MediaRecorder:5,MediaEncoder:5,TrackEncoder:5,VP8TrackEncoder:5,Muxer:5,GetUserMedia:5,MediaPipeline:5,WebAudioAPI:5,webrtc_trace:5,RTCRtpTransceiver:5,ForwardedInputTrack:5,HTMLMediaElement:5,HTMLMediaElementEvents:5",
    l10nIds: {
      label: "about-logging-preset-webrtc-label",
      description: "about-logging-preset-webrtc-description",
    },
    profilerPreset: "media",
  },
  webcodecs: {
    modules:
      "WebCodecs:5,PlatformDecoderModule:5,PlatformEncoderModule:5,MediaDecoder:5,FFmpegVideo:5,FFmpegAudio:5,GMP:5,ImageUtils:5",
    l10nIds: {
      label: "about-logging-preset-webcodecs-label",
      description: "about-logging-preset-webcodecs-description",
    },
    profilerPreset: "media",
  },
  webgpu: {
    modules:
      "wgpu_core::*:5,wgpu_hal::*:5,wgpu_types::*:5,naga::*:5,wgpu_bindings::*:5,WebGPU:5",
    l10nIds: {
      label: "about-logging-preset-webgpu-label",
      description: "about-logging-preset-webgpu-description",
    },
  },
  gfx: {
    modules:
      "webrender::*:5,webrender_bindings::*:5,webrender_types::*:5,gfx2d:5,WebRenderBridgeParent:5,DcompSurface:5,apz.displayport:5,layout:5,dl.content:5,dl.parent:5,nsRefreshDriver:5,fontlist:5,fontinit:5,textrun:5,textrunui:5,textperf:5,printing:5",
    l10nIds: {
      label: "about-logging-preset-gfx-label",
      description: "about-logging-preset-gfx-description",
    },
    // The graphics profiler preset enables the threads we want but loses the screenshots.
    // We could add an extra preset for that if we miss it.
    profilerPreset: "graphics",
  },
  ...gOsSpecificLoggingPresets,
  custom: {
    modules: "",
    l10nIds: {
      label: "about-logging-preset-custom-label",
      description: "about-logging-preset-custom-description",
    },
  },
};

const gLoggingSettings = {
  // Possible values: "profiler" and "file".
  loggingOutputType: "profiler",
  running: false,
  // If non-null, the profiler preset to use. If null, the preset selected in
  // the dropdown is going to be used. It is also possible to use a "custom"
  // preset and an explicit list of modules.
  loggingPreset: null,
  // If non-null, the profiler preset to use. If a logging preset is being used,
  // and this is null, the profiler preset associated to the logging preset is
  // going to be used. Otherwise, a generic profiler preset is going to be used
  // ("firefox-platform").
  profilerPreset: null,
  // If non-null, the threads that will be recorded by the Firefox Profiler. If
  // null, the threads from the profiler presets are going to be used.
  profilerThreads: null,
  // If non-null, stack traces will be recorded for MOZ_LOG profiler markers.
  // This is set only when coming from the URL, not when the user changes the UI.
  profilerStacks: null,
};

// When the profiler has been started, this holds the promise the
// Services.profiler.StartProfiler returns, to ensure the profiler has
// effectively started.
let gProfilerPromise = null;

// These functions are used in tests
globalThis.presets = () => gLoggingPresets;
globalThis.settings = () => gLoggingSettings;
globalThis.profilerPromise = () => gProfilerPromise;

function populatePresets() {
  let dropdown = $("#logging-preset-dropdown");
  for (let presetName in gLoggingPresets) {
    let preset = gLoggingPresets[presetName];
    let option = document.createElement("option");
    document.l10n.setAttributes(option, preset.l10nIds.label);
    option.value = presetName;
    dropdown.appendChild(option);
    if (option.value === gLoggingSettings.loggingPreset) {
      option.setAttribute("selected", true);
    }
  }

  function setPresetAndDescription(preset) {
    document.l10n.setAttributes(
      $("#logging-preset-description"),
      gLoggingPresets[preset].l10nIds.description
    );
    gLoggingSettings.loggingPreset = preset;
  }

  dropdown.onchange = function () {
    // When switching to custom, leave the existing module list, to allow
    // editing.
    if (dropdown.value != "custom") {
      $("#log-modules").value = gLoggingPresets[dropdown.value].modules;
    }
    setPresetAndDescription(dropdown.value);
    Services.prefs.setCharPref("logging.config.preset", dropdown.value);
  };

  $("#log-modules").value = gLoggingPresets[dropdown.value].modules;
  setPresetAndDescription(dropdown.value);
  // When changing the list switch to custom.
  $("#log-modules").oninput = () => {
    dropdown.value = "custom";
  };
}

function updateLoggingOutputType(profilerOutputType) {
  gLoggingSettings.loggingOutputType = profilerOutputType;
  Services.prefs.setCharPref("logging.config.output_type", profilerOutputType);
  $(`input[type=radio][value=${profilerOutputType}]`).checked = true;

  switch (profilerOutputType) {
    case "profiler":
      if (!gLoggingSettings.profilerStacks) {
        // If this value is set from the URL, do not allow to change it.
        $("#with-profiler-stacks-checkbox").disabled = false;
      }
      // hide options related to file output for clarity
      $("#log-file-configuration").hidden = true;
      break;
    case "file":
      $("#with-profiler-stacks-checkbox").disabled = true;
      $("#log-file-configuration").hidden = false;
      $("#no-log-file").hidden = !!$("#current-log-file").innerText.length;
      break;
  }
}

function displayErrorMessage(error) {
  var err = $("#error");
  err.hidden = false;

  var errorDescription = $("#error-description");
  if (error instanceof ParseError) {
    document.l10n.setAttributes(errorDescription, error.l10nId, {
      k: error.key,
      v: error.value,
    });
  } else {
    document.l10n.setAttributes(
      errorDescription,
      "about-logging-unknown-error",
      {
        errorText: String(error),
      }
    );
  }
}

class ParseError extends Error {
  constructor(l10nId, key, value) {
    super(name);
    this.l10nId = l10nId;
    this.key = key;
    this.value = value;
  }
  name = "ParseError";
  l10nId;
  key;
  value;
}

function parseURL() {
  let options = new URL(document.location.href).searchParams;

  if (!options.size) {
    return;
  }

  let modulesOverriden = null,
    outputTypeOverriden = null,
    loggingPresetOverriden = null,
    threadsOverriden = null,
    profilerPresetOverriden = null,
    profilerStacksOverriden = null;
  try {
    for (let [k, v] of options) {
      switch (k) {
        case "modules":
        case "module":
          modulesOverriden = v;
          break;
        case "output":
        case "output-type":
          if (v !== "profiler" && v !== "file") {
            throw new ParseError("about-logging-invalid-output", k, v);
          }
          outputTypeOverriden = v;
          break;
        case "preset":
        case "logging-preset":
          if (!Object.keys(gLoggingPresets).includes(v)) {
            throw new ParseError("about-logging-unknown-logging-preset", k, v);
          }
          loggingPresetOverriden = v;
          break;
        case "threads":
        case "thread":
          threadsOverriden = v;
          break;
        case "profiler-preset":
          if (!Object.keys(lazy.ProfilerPrefsPresets.presets).includes(v)) {
            throw new Error(["about-logging-unknown-profiler-preset", k, v]);
          }
          profilerPresetOverriden = v;
          break;
        case "profilerstacks":
          profilerStacksOverriden = true;
          break;
        default:
          throw new ParseError("about-logging-unknown-option", k, v);
      }
    }
  } catch (e) {
    displayErrorMessage(e);
    return;
  }

  // Detect combinations that don't make sense
  if (
    (profilerPresetOverriden || threadsOverriden) &&
    outputTypeOverriden == "file"
  ) {
    displayErrorMessage(
      new ParseError("about-logging-file-and-profiler-override")
    );
    return;
  }

  // Configuration is deemed at least somewhat valid, override each setting in
  // turn
  let someElementsDisabled = false;

  if (modulesOverriden || loggingPresetOverriden) {
    // Don't allow changing those if set by the URL
    let logModules = $("#log-modules");
    var dropdown = $("#logging-preset-dropdown");
    if (loggingPresetOverriden) {
      dropdown.value = loggingPresetOverriden;
      dropdown.onchange();
    }
    if (modulesOverriden) {
      logModules.value = modulesOverriden;
      dropdown.value = "custom";
      dropdown.onchange();
      dropdown.disabled = true;
      someElementsDisabled = true;
    }
    logModules.disabled = true;
    $("#set-log-modules-button").disabled = true;
    $("#logging-preset-dropdown").disabled = true;
    someElementsDisabled = true;
    updateLogModules();
  }
  if (outputTypeOverriden) {
    $$("input[type=radio]").forEach(e => (e.disabled = true));
    someElementsDisabled = true;
    updateLoggingOutputType(outputTypeOverriden);
  }
  if (profilerStacksOverriden) {
    const checkbox = $("#with-profiler-stacks-checkbox");
    checkbox.disabled = true;
    someElementsDisabled = true;
    Services.prefs.setBoolPref("logging.config.profilerstacks", true);
    gLoggingSettings.profilerStacks = true;
  }

  if (loggingPresetOverriden) {
    gLoggingSettings.loggingPreset = loggingPresetOverriden;
  }
  if (profilerPresetOverriden) {
    gLoggingSettings.profilerPreset = profilerPresetOverriden;
  }
  if (threadsOverriden) {
    gLoggingSettings.profilerThreads = threadsOverriden;
  }

  $("#some-elements-unavailable").hidden = !someElementsDisabled;
}

let gInited = false;
function init() {
  if (gInited) {
    return;
  }
  gInited = true;
  gDashboard.enableLogging = true;

  populatePresets();
  parseURL();

  $("#log-file-configuration").addEventListener("submit", e => {
    e.preventDefault();
    setLogFile();
  });

  $("#log-modules-form").addEventListener("submit", e => {
    e.preventDefault();
    setLogModules();
  });

  let toggleLoggingButton = $("#toggle-logging-button");
  toggleLoggingButton.addEventListener("click", startStopLogging);

  $$("input[type=radio]").forEach(radio => {
    radio.onchange = e => {
      updateLoggingOutputType(e.target.value);
    };
  });

  $("#with-profiler-stacks-checkbox").addEventListener("change", e => {
    Services.prefs.setBoolPref(
      "logging.config.profilerstacks",
      e.target.checked
    );
    updateLogModules();
  });

  let loggingOutputType = Services.prefs.getCharPref(
    "logging.config.output_type",
    "profiler"
  );
  if (loggingOutputType.length) {
    updateLoggingOutputType(loggingOutputType);
  }

  $("#with-profiler-stacks-checkbox").checked = Services.prefs.getBoolPref(
    "logging.config.profilerstacks",
    false
  );

  try {
    let loggingPreset = Services.prefs.getCharPref("logging.config.preset");
    gLoggingSettings.loggingPreset = loggingPreset;
  } catch {}

  try {
    let running = Services.prefs.getBoolPref("logging.config.running");
    gLoggingSettings.running = running;
    document.l10n.setAttributes(
      $("#toggle-logging-button"),
      `about-logging-${gLoggingSettings.running ? "stop" : "start"}-logging`
    );
  } catch {}

  try {
    let file = Services.dirsvc.get("TmpD", Ci.nsIFile);
    file.append("log.txt");
    $("#log-file").value = file.path;
  } catch (e) {
    console.error(e);
  }

  // Update the value of the log file.
  updateLogFile();

  // Update the active log modules
  updateLogModules();

  // If we can't set the file and the modules at runtime,
  // the start and stop buttons wouldn't really do anything.
  if (
    ($("#set-log-file-button").disabled ||
      $("#set-log-modules-button").disabled) &&
    moduleEnvVarPresent()
  ) {
    $("#buttons-disabled").hidden = false;
    toggleLoggingButton.disabled = true;
  }
}

function maybeEnsureButtonInNavbar() {
  if (lazy.AppConstants.platform !== "android") {
    // Force displaying the profiler button in the navbar if not preset, so
    // that there is a visual indication profiling is in progress.
    lazy.ProfilerMenuButton.ensureButtonInNavbar();
  }
}

let gProfileSaveOrUpload = null;
async function captureProfile() {
  const shouldUploadToCloud = Services.prefs.getBoolPref(
    "toolkit.aboutLogging.uploadProfileToCloud",
    false
  );
  if (shouldUploadToCloud) {
    const { profileCaptureResult } =
      await lazy.RecordingUtils.getProfileDataAsGzippedArrayBufferThenStop();
    if (profileCaptureResult.type === "ERROR") {
      throw profileCaptureResult.error;
    }
    if (!gProfileSaveOrUpload) {
      const { ProfileSaveOrUploadDialog } = await import(
        "chrome://global/content/aboutLogging/profileSaveUploadLogic.mjs"
      );
      gProfileSaveOrUpload = new ProfileSaveOrUploadDialog();
    }
    gProfileSaveOrUpload.init(new Uint8Array(profileCaptureResult.profile));
  } else {
    // Open the profiler in a new tab
    await lazy.ProfilerPopupBackground.captureProfile("aboutlogging");
  }
}

function updateLogFile(file) {
  let logPath = "";

  // Try to get the environment variable for the log file
  logPath =
    Services.env.get("MOZ_LOG_FILE") || Services.env.get("NSPR_LOG_FILE");
  let currentLogFile = $("#current-log-file");
  let setLogFileButton = $("#set-log-file-button");

  // If the log file was set from an env var, we disable the ability to set it
  // at runtime.
  if (logPath.length) {
    currentLogFile.innerText = logPath;
    setLogFileButton.disabled = true;
  } else if (gDashboard.getLogPath() != ".moz_log") {
    // There may be a value set by a pref.
    currentLogFile.innerText = gDashboard.getLogPath();
  } else if (file !== undefined) {
    currentLogFile.innerText = file;
  } else {
    try {
      let file = Services.dirsvc.get("TmpD", Ci.nsIFile);
      file.append("log.txt");
      $("#log-file").value = file.path;
    } catch (e) {
      console.error(e);
    }
    // Fall back to the temp dir
    currentLogFile.innerText = $("#log-file").value;
  }

  let openLogFileButton = $("#open-log-file-button");
  openLogFileButton.disabled = true;

  if (currentLogFile.innerText.length) {
    let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    file.initWithPath(currentLogFile.innerText);

    if (file.exists()) {
      openLogFileButton.disabled = false;
      openLogFileButton.onclick = function () {
        file.reveal();
      };
    }
  }
  $("#no-log-file").hidden = !!currentLogFile.innerText.length;
  $("#current-log-file").hidden = !currentLogFile.innerText.length;
}

function updateLogModules() {
  // Try to get the environment variable for the log file
  let logModules =
    Services.env.get("MOZ_LOG") ||
    Services.env.get("MOZ_LOG_MODULES") ||
    Services.env.get("NSPR_LOG_MODULES");
  let currentLogModules = $("#current-log-modules");
  let setLogModulesButton = $("#set-log-modules-button");
  if (logModules.length) {
    currentLogModules.innerText = logModules;
    // If the log modules are set by an environment variable at startup, do not
    // allow changing them throught a pref. It would be difficult to figure out
    // which ones are enabled and which ones are not. The user probably knows
    // what he they are doing.
    setLogModulesButton.disabled = true;
  } else {
    let activeLogModules = [];
    let children = Services.prefs.getBranch("logging.").getChildList("");

    for (let pref of children) {
      if (pref.startsWith("config.")) {
        continue;
      }

      try {
        let value = Services.prefs.getIntPref(`logging.${pref}`);
        activeLogModules.push(`${pref}:${value}`);
      } catch (e) {
        console.error(e);
      }
    }

    if (activeLogModules.length) {
      // Add some options only if some modules are present.
      if (Services.prefs.getBoolPref("logging.config.add_timestamp", false)) {
        activeLogModules.push("timestamp");
      }
      if (Services.prefs.getBoolPref("logging.config.sync", false)) {
        activeLogModules.push("sync");
      }
      if (Services.prefs.getBoolPref("logging.config.profilerstacks", false)) {
        activeLogModules.push("profilerstacks");
      }
    }

    if (activeLogModules.length !== 0) {
      currentLogModules.innerText = activeLogModules.join(",");
      currentLogModules.hidden = false;
      $("#no-log-modules").hidden = true;
    } else {
      currentLogModules.innerText = "";
      currentLogModules.hidden = true;
      $("#no-log-modules").hidden = false;
    }
  }
}

function setLogFile() {
  let setLogButton = $("#set-log-file-button");
  if (setLogButton.disabled) {
    // There's no point trying since it wouldn't work anyway.
    return;
  }
  let logFile = $("#log-file").value.trim();
  Services.prefs.setCharPref("logging.config.LOG_FILE", logFile);
  updateLogFile(logFile);
}

function clearLogModules() {
  // Turn off all the modules.
  let children = Services.prefs.getBranch("logging.").getChildList("");
  for (let pref of children) {
    if (!pref.startsWith("config.")) {
      Services.prefs.clearUserPref(`logging.${pref}`);
    }
  }
  Services.prefs.clearUserPref("logging.config.add_timestamp");
  Services.prefs.clearUserPref("logging.config.sync");
  updateLogModules();
}

function setLogModules() {
  if (moduleEnvVarPresent()) {
    // The modules were set via env var, so we shouldn't try to change them.
    return;
  }

  let modules = $("#log-modules").value.trim();

  // Clear previously set log modules.
  clearLogModules();

  if (modules.length !== 0) {
    let logModules = modules.split(",");
    for (let module of logModules) {
      if (module == "timestamp") {
        Services.prefs.setBoolPref("logging.config.add_timestamp", true);
      } else if (module == "rotate") {
        // XXX: rotate is not yet supported.
      } else if (module == "append") {
        // XXX: append is not yet supported.
      } else if (module == "sync") {
        Services.prefs.setBoolPref("logging.config.sync", true);
      } else if (module == "profilerstacks") {
        Services.prefs.setBoolPref("logging.config.profilerstacks", true);
      } else {
        let lastColon = module.lastIndexOf(":");
        let key = module.slice(0, lastColon);
        let value = parseInt(module.slice(lastColon + 1), 10);
        Services.prefs.setIntPref(`logging.${key}`, value);
      }
    }
  }

  updateLogModules();
}

function isLogging() {
  try {
    return Services.prefs.getBoolPref("logging.config.running");
  } catch {
    return false;
  }
}

function startStopLogging() {
  if (isLogging()) {
    document.l10n.setAttributes(
      $("#toggle-logging-button"),
      "about-logging-start-logging"
    );
    stopLogging();
  } else {
    document.l10n.setAttributes(
      $("#toggle-logging-button"),
      "about-logging-stop-logging"
    );
    startLogging();
  }
}

function startLogging() {
  setLogModules();
  if (gLoggingSettings.loggingOutputType === "profiler") {
    if (gProfileSaveOrUpload) {
      gProfileSaveOrUpload.reset();
    }

    const supportedFeatures = Services.profiler.GetFeatures();
    if (
      gLoggingSettings.loggingPreset != "custom" ||
      gLoggingSettings.profilerPreset
    ) {
      // Change the preset before starting the profiler, so that the
      // underlying profiler code picks up the right configuration.
      // If a profiler preset has been explicitely provided (via URL parameters),
      // pick it. Otherwise, pick the preset for this particular logging preset.
      const profilerPreset =
        gLoggingSettings.profilerPreset ??
        gLoggingPresets[gLoggingSettings.loggingPreset].profilerPreset;
      lazy.ProfilerPrefsPresets.changePreset(
        "aboutlogging",
        profilerPreset,
        supportedFeatures
      );
    } else {
      // a baseline set of threads, and possibly others, overriden by the URL
      lazy.ProfilerPrefsPresets.changePreset(
        "aboutlogging",
        "firefox-platform",
        supportedFeatures
      );
    }
    let { entries, interval, features, threads, duration } =
      lazy.ProfilerPrefsPresets.getRecordingSettings(
        "aboutlogging",
        Services.profiler.GetFeatures()
      );

    if (gLoggingSettings.profilerThreads) {
      threads.push(...gLoggingSettings.profilerThreads.split(","));
      // Small hack: if cubeb is being logged, it's almost always necessary (and
      // never harmful) to enable audio callback tracing, otherwise, no log
      // statements will be recorded from real-time threads.
      if (gLoggingSettings.profilerThreads.includes("cubeb")) {
        features.push("audiocallbacktracing");
      }
    }

    maybeEnsureButtonInNavbar();

    gProfilerPromise = Services.profiler.StartProfiler(
      entries,
      interval,
      features,
      threads,
      lazy.RecordingUtils.getActiveBrowserID(),
      duration
    );
  } else {
    setLogFile();
  }
  Services.prefs.setBoolPref("logging.config.running", true);
}

async function stopLogging() {
  try {
    if (gLoggingSettings.loggingOutputType === "profiler") {
      await captureProfile();
    } else {
      Services.prefs.clearUserPref("logging.config.LOG_FILE");
      updateLogFile();
    }
    Services.prefs.setBoolPref("logging.config.running", false);
    clearLogModules();
  } catch (e) {
    console.error("Unknown error when stopping the logging", e);
    displayErrorMessage(e);
  }
}

// We use the pageshow event instead of onload. This is needed because sometimes
// the page is loaded via session-restore/bfcache. In such cases we need to call
// init() to keep the page behaviour consistent with the ticked checkboxes.
// Mostly the issue is with the autorefresh checkbox.
window.addEventListener("pageshow", function () {
  init();
});
