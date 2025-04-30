/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-check

/**
 * This file contains all of the background logic for controlling the state and
 * configuration of the profiler. It is in a JSM so that the logic can be shared
 * with both the popup client, and the keyboard shortcuts. The shortcuts don't need
 * access to any UI, and need to be loaded independent of the popup.
 */

// The following are not lazily loaded as they are needed during initialization.

import { createLazyLoaders } from "resource://devtools/client/performance-new/shared/typescript-lazy-load.sys.mjs";

/**
 * @typedef {import("../@types/perf").PerformancePref} PerformancePref
 * @typedef {import("../@types/perf").ProfilerWebChannel} ProfilerWebChannel
 * @typedef {import("../@types/perf").PageContext} PageContext
 * @typedef {import("../@types/perf").MessageFromFrontend} MessageFromFrontend
 * @typedef {import("../@types/perf").RequestFromFrontend} RequestFromFrontend
 * @typedef {import("../@types/perf").ResponseToFrontend} ResponseToFrontend
 * @typedef {import("../@types/perf").SymbolicationService} SymbolicationService
 * @typedef {import("../@types/perf").ProfilerBrowserInfo} ProfilerBrowserInfo
 * @typedef {import("../@types/perf").ProfileCaptureResult} ProfileCaptureResult
 * @typedef {import("../@types/perf").ProfilerFaviconData} ProfilerFaviconData
 */

/** @type {PerformancePref["PopupFeatureFlag"]} */
const POPUP_FEATURE_FLAG_PREF = "devtools.performance.popup.feature-flag";

// The version of the profiler WebChannel.
// This is reported from the STATUS_QUERY message, and identifies the
// capabilities of the WebChannel. The front-end can handle old WebChannel
// versions and has a full list of versions and capabilities here:
// https://github.com/firefox-devtools/profiler/blob/main/src/app-logic/web-channel.js
const CURRENT_WEBCHANNEL_VERSION = 5;

const lazyRequire = {};
// eslint-disable-next-line mozilla/lazy-getter-object-name
ChromeUtils.defineESModuleGetters(lazyRequire, {
  require: "resource://devtools/shared/loader/Loader.sys.mjs",
});
// Lazily load the require function, when it's needed.
// Avoid using ChromeUtils.defineESModuleGetters for now as:
// * we can't replace createLazyLoaders as we still load commonjs+jsm+esm
//   It will be easier once we only load sys.mjs files.
// * we would need to find a way to accomodate typescript to this special function.
// @ts-ignore:next-line
function require(path) {
  // @ts-ignore:next-line
  return lazyRequire.require(path);
}

// The following utilities are lazily loaded as they are not needed when controlling the
// global state of the profiler, and only are used during specific funcationality like
// symbolication or capturing a profile.
const lazy = createLazyLoaders({
  BrowserModule: () =>
    require("resource://devtools/client/performance-new/shared/browser.js"),
  Errors: () =>
    ChromeUtils.importESModule(
      "resource://devtools/shared/performance-new/errors.sys.mjs"
    ),
  PrefsPresets: () =>
    ChromeUtils.importESModule(
      "resource://devtools/shared/performance-new/prefs-presets.sys.mjs"
    ),
  RecordingUtils: () =>
    ChromeUtils.importESModule(
      "resource://devtools/shared/performance-new/recording-utils.sys.mjs"
    ),
  CustomizableUI: () =>
    ChromeUtils.importESModule("resource:///modules/CustomizableUI.sys.mjs"),
  PerfSymbolication: () =>
    ChromeUtils.importESModule(
      "resource://devtools/client/performance-new/shared/symbolication.sys.mjs"
    ),
  ProfilerMenuButton: () =>
    ChromeUtils.importESModule(
      "resource://devtools/client/performance-new/popup/menu-button.sys.mjs"
    ),
  PlacesUtils: () =>
    ChromeUtils.importESModule("resource://gre/modules/PlacesUtils.sys.mjs")
      .PlacesUtils,
});

/**
 * This function is called when the profile is captured with the shortcut keys,
 * with the profiler toolbarbutton, with the button inside the popup, or with
 * the about:logging page.
 * @param {PageContext} pageContext
 * @return {Promise<void>}
 */
export async function captureProfile(pageContext) {
  if (!Services.profiler.IsActive()) {
    // The profiler is not active, ignore.
    return;
  }
  if (Services.profiler.IsPaused()) {
    // The profiler is already paused for capture, ignore.
    return;
  }

  const { profileCaptureResult, additionalInformation } = await lazy
    .RecordingUtils()
    .getProfileDataAsGzippedArrayBufferThenStop();
  const profilerViewMode = lazy
    .PrefsPresets()
    .getProfilerViewModeForCurrentPreset(pageContext);
  const sharedLibraries = additionalInformation?.sharedLibraries
    ? additionalInformation.sharedLibraries
    : Services.profiler.sharedLibraries;
  const objdirs = lazy.PrefsPresets().getObjdirPrefValue();

  const { createLocalSymbolicationService } = lazy.PerfSymbolication();
  const symbolicationService = createLocalSymbolicationService(
    sharedLibraries,
    objdirs
  );

  const { openProfilerTab } = lazy.BrowserModule();
  const browser = await openProfilerTab({ profilerViewMode });
  registerProfileCaptureForBrowser(
    browser,
    profileCaptureResult,
    symbolicationService
  );
}

/**
 * This function is called when the profiler is started with the shortcut
 * keys, with the profiler toolbarbutton, or with the button inside the
 * popup.
 * @param {PageContext} pageContext
 */
export function startProfiler(pageContext) {
  const { entries, interval, features, threads, duration } = lazy
    .PrefsPresets()
    .getRecordingSettings(pageContext, Services.profiler.GetFeatures());

  // Get the active Browser ID from browser.
  const { getActiveBrowserID } = lazy.RecordingUtils();
  const activeTabID = getActiveBrowserID();

  Services.profiler.StartProfiler(
    entries,
    interval,
    features,
    threads,
    activeTabID,
    duration
  );
}

/**
 * This function is called directly by devtools/startup/DevToolsStartup.jsm when
 * using the shortcut keys to capture a profile.
 * @type {() => void}
 */
export function stopProfiler() {
  Services.profiler.StopProfiler();
}

/**
 * This function is called directly by devtools/startup/DevToolsStartup.jsm when
 * using the shortcut keys to start and stop the profiler.
 * @param {PageContext} pageContext
 * @return {void}
 */
export function toggleProfiler(pageContext) {
  if (Services.profiler.IsPaused()) {
    // The profiler is currently paused, which means that the user is already
    // attempting to capture a profile. Ignore this request.
    return;
  }
  if (Services.profiler.IsActive()) {
    stopProfiler();
  } else {
    startProfiler(pageContext);
  }
}

/**
 * @param {PageContext} pageContext
 */
export function restartProfiler(pageContext) {
  stopProfiler();
  startProfiler(pageContext);
}

/**
 * This map stores information that is associated with a "profile capturing"
 * action, so that we can look up this information for WebChannel messages
 * from the profiler tab.
 * Most importantly, this stores the captured profile. When the profiler tab
 * requests the profile, we can respond to the message with the correct profile.
 * This works even if the request happens long after the tab opened. It also
 * works for an "old" tab even if new profiles have been captured since that
 * tab was opened.
 * Supporting tab refresh is important because the tab sometimes reloads itself:
 * If an old version of the front-end is cached in the service worker, and the
 * browser supplies a profile with a newer format version, then the front-end
 * updates its service worker and reloads itself, so that the updated version
 * can parse the profile.
 *
 * This is a WeakMap so that the profile can be garbage-collected when the tab
 * is closed.
 *
 * @type {WeakMap<MockedExports.Browser, ProfilerBrowserInfo>}
 */
const infoForBrowserMap = new WeakMap();

/**
 * This handler computes the response for any messages coming
 * from the WebChannel from profiler.firefox.com.
 *
 * @param {RequestFromFrontend} request
 * @param {MockedExports.Browser} browser - The tab's browser.
 * @return {Promise<ResponseToFrontend>}
 */
async function getResponseForMessage(request, browser) {
  switch (request.type) {
    case "STATUS_QUERY": {
      // The content page wants to know if this channel exists. It does, so respond
      // back to the ping.
      const { ProfilerMenuButton } = lazy.ProfilerMenuButton();
      return {
        version: CURRENT_WEBCHANNEL_VERSION,
        menuButtonIsEnabled: ProfilerMenuButton.isInNavbar(),
      };
    }
    case "ENABLE_MENU_BUTTON": {
      const { ownerDocument } = browser;
      if (!ownerDocument) {
        throw new Error(
          "Could not find the owner document for the current browser while enabling " +
            "the profiler menu button"
        );
      }
      // Ensure the widget is enabled.
      Services.prefs.setBoolPref(POPUP_FEATURE_FLAG_PREF, true);

      // Force the preset to be "firefox-platform" if we enable the menu button
      // via web channel. If user goes through profiler.firefox.com to enable
      // it, it means that either user is a platform developer or filing a bug
      // report for performance engineers to look at.
      const supportedFeatures = Services.profiler.GetFeatures();
      lazy
        .PrefsPresets()
        .changePreset("aboutprofiling", "firefox-platform", supportedFeatures);

      // Enable the profiler menu button.
      const { ProfilerMenuButton } = lazy.ProfilerMenuButton();
      ProfilerMenuButton.addToNavbar();

      // Dispatch the change event manually, so that the shortcuts will also be
      // added.
      const { CustomizableUI } = lazy.CustomizableUI();
      CustomizableUI.dispatchToolboxEvent("customizationchange");

      // Open the popup with a message.
      ProfilerMenuButton.openPopup(ownerDocument);

      // There is no response data for this message.
      return undefined;
    }
    case "GET_PROFILE": {
      const infoForBrowser = infoForBrowserMap.get(browser);
      if (infoForBrowser === undefined) {
        throw new Error("Could not find a profile for this tab.");
      }
      const { profileCaptureResult } = infoForBrowser;
      switch (profileCaptureResult.type) {
        case "SUCCESS":
          return profileCaptureResult.profile;
        case "ERROR":
          throw profileCaptureResult.error;
        default: {
          const { UnhandledCaseError } = lazy.Errors();
          throw new UnhandledCaseError(
            profileCaptureResult,
            "profileCaptureResult"
          );
        }
      }
    }
    case "GET_SYMBOL_TABLE": {
      const { debugName, breakpadId } = request;
      const symbolicationService = getSymbolicationServiceForBrowser(browser);
      return symbolicationService.getSymbolTable(debugName, breakpadId);
    }
    case "QUERY_SYMBOLICATION_API": {
      const { path, requestJson } = request;
      const symbolicationService = getSymbolicationServiceForBrowser(browser);
      return symbolicationService.querySymbolicationApi(path, requestJson);
    }
    case "GET_EXTERNAL_POWER_TRACKS": {
      const { startTime, endTime } = request;
      const externalPowerUrl = Services.prefs.getCharPref(
        "devtools.performance.recording.power.external-url",
        ""
      );
      if (externalPowerUrl) {
        const response = await fetch(
          `${externalPowerUrl}?start=${startTime}&end=${endTime}`
        );
        return response.json();
      }
      return [];
    }
    case "GET_EXTERNAL_MARKERS": {
      const { startTime, endTime } = request;
      const externalMarkersUrl = Services.prefs.getCharPref(
        "devtools.performance.recording.markers.external-url",
        ""
      );
      if (externalMarkersUrl) {
        const response = await fetch(
          `${externalMarkersUrl}?start=${startTime}&end=${endTime}`
        );
        return response.json();
      }
      return [];
    }
    case "GET_PAGE_FAVICONS": {
      const { pageUrls } = request;
      return getPageFavicons(pageUrls);
    }
    case "OPEN_SCRIPT_IN_DEBUGGER": {
      // This webchannel message type is added with version 5.
      const { tabId, scriptUrl, line, column } = request;
      const { openScriptInDebugger } = lazy.BrowserModule();
      return openScriptInDebugger(tabId, scriptUrl, line, column);
    }

    default: {
      console.error(
        "An unknown message type was received by the profiler's WebChannel handler.",
        request
      );
      const { UnhandledCaseError } = lazy.Errors();
      throw new UnhandledCaseError(request, "WebChannel request");
    }
  }
}

/**
 * Get the symbolicationService for the capture that opened this browser's
 * tab, or a fallback service for browsers from tabs opened by the user.
 *
 * @param {MockedExports.Browser} browser
 * @return {SymbolicationService}
 */
function getSymbolicationServiceForBrowser(browser) {
  // We try to serve symbolication requests that come from tabs that we
  // opened when a profile was captured, and for tabs that the user opened
  // independently, for example because the user wants to load an existing
  // profile from a file.
  const infoForBrowser = infoForBrowserMap.get(browser);
  if (infoForBrowser !== undefined) {
    // We opened this tab when a profile was captured. Use the symbolication
    // service for that capture.
    return infoForBrowser.symbolicationService;
  }

  // For the "foreign" tabs, we provide a fallback symbolication service so that
  // we can find symbols for any libraries that are loaded in this process. This
  // means that symbolication will work if the existing file has been captured
  // from the same build.
  const { createLocalSymbolicationService } = lazy.PerfSymbolication();
  return createLocalSymbolicationService(
    Services.profiler.sharedLibraries,
    lazy.PrefsPresets().getObjdirPrefValue()
  );
}

/**
 * This handler handles any messages coming from the WebChannel from profiler.firefox.com.
 *
 * @param {ProfilerWebChannel} channel
 * @param {string} id
 * @param {any} message
 * @param {MockedExports.WebChannelTarget} target
 */
export async function handleWebChannelMessage(channel, id, message, target) {
  if (typeof message !== "object" || typeof message.type !== "string") {
    console.error(
      "An malformed message was received by the profiler's WebChannel handler.",
      message
    );
    return;
  }
  const messageFromFrontend = /** @type {MessageFromFrontend} */ (message);
  const { requestId } = messageFromFrontend;

  try {
    const response = await getResponseForMessage(
      messageFromFrontend,
      target.browser
    );
    channel.send(
      {
        type: "SUCCESS_RESPONSE",
        requestId,
        response,
      },
      target
    );
  } catch (error) {
    let errorMessage;
    if (error instanceof Error) {
      errorMessage = `${error.name}: ${error.message}`;
    } else {
      errorMessage = `${error}`;
    }
    channel.send(
      {
        type: "ERROR_RESPONSE",
        requestId,
        error: errorMessage,
      },
      target
    );
  }
}

/**
 * @param {MockedExports.Browser} browser - The tab's browser.
 * @param {ProfileCaptureResult} profileCaptureResult - The Gecko profile.
 * @param {SymbolicationService} symbolicationService - An object which implements the
 *   SymbolicationService interface, whose getSymbolTable method will be invoked
 *   when profiler.firefox.com sends GET_SYMBOL_TABLE WebChannel messages to us. This
 *   method should obtain a symbol table for the requested binary and resolve the
 *   returned promise with it.
 */
export function registerProfileCaptureForBrowser(
  browser,
  profileCaptureResult,
  symbolicationService
) {
  infoForBrowserMap.set(browser, {
    profileCaptureResult,
    symbolicationService,
  });
}

/**
 * Get page favicons data and return them.
 *
 * @param {Array<string>} pageUrls
 *
 * @returns {Promise<Array<ProfilerFaviconData | null>>} favicon data as binary array.
 */
async function getPageFavicons(pageUrls) {
  if (!pageUrls || pageUrls.length === 0) {
    // Return early if the pages are not provided.
    return [];
  }

  // Get the data of favicons and return them.
  const { favicons, toURI } = lazy.PlacesUtils();

  const promises = pageUrls.map(pageUrl =>
    favicons
      .getFaviconForPage(toURI(pageUrl), /* preferredWidth = */ 32)
      .then(favicon => {
        // Check if data is found in the database and return it if so.
        if (favicon.rawData.length) {
          return {
            // PlacesUtils returns a number array for the data. Converting it to
            // the Uint8Array here to send it to the tab more efficiently.
            data: new Uint8Array(favicon.rawData).buffer,
            mimeType: favicon.mimeType,
          };
        }

        return null;
      })
      .catch(() => {
        // Couldn't find a favicon for this page, return null explicitly.
        return null;
      })
  );

  return Promise.all(promises);
}
