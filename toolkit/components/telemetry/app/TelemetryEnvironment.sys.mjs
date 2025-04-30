/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Log } from "resource://gre/modules/Log.sys.mjs";

import { TelemetryUtils } from "resource://gre/modules/TelemetryUtils.sys.mjs";

import { ObjectUtils } from "resource://gre/modules/ObjectUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { UpdateUtils } from "resource://gre/modules/UpdateUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const Utils = TelemetryUtils;

import {
  AddonManager,
  AddonManagerPrivate,
} from "resource://gre/modules/AddonManager.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AttributionCode: "resource:///modules/AttributionCode.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
  WindowsVersionInfo:
    "resource://gre/modules/components-utils/WindowsVersionInfo.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

if (AppConstants.MOZ_UPDATER) {
  XPCOMUtils.defineLazyServiceGetter(
    lazy,
    "UpdateServiceStub",
    "@mozilla.org/updates/update-service-stub;1",
    "nsIApplicationUpdateServiceStub"
  );
}

// The maximum length of a string (e.g. description) in the addons section.
const MAX_ADDON_STRING_LENGTH = 100;
// The maximum length of a string value in the settings.attribution object.
const MAX_ATTRIBUTION_STRING_LENGTH = 100;
// The maximum lengths for the experiment id and branch in the experiments section.
const MAX_EXPERIMENT_ID_LENGTH = 100;
const MAX_EXPERIMENT_BRANCH_LENGTH = 100;
const MAX_EXPERIMENT_TYPE_LENGTH = 20;
const MAX_EXPERIMENT_ENROLLMENT_ID_LENGTH = 40;

/**
 * This is a policy object used to override behavior for testing.
 */
// eslint-disable-next-line no-unused-vars
export var Policy = {
  now: () => new Date(),
  _intlLoaded: false,
  _browserDelayedStartup() {
    if (Policy._intlLoaded) {
      return Promise.resolve();
    }
    return new Promise(resolve => {
      let startupTopic = "browser-delayed-startup-finished";
      Services.obs.addObserver(function observer(subject, topic) {
        if (topic == startupTopic) {
          Services.obs.removeObserver(observer, startupTopic);
          resolve();
        }
      }, startupTopic);
    });
  },
};

// This is used to buffer calls to setExperimentActive and friends, so that we
// don't prematurely initialize our environment if it is called early during
// startup.
var gActiveExperimentStartupBuffer = new Map();

// For Powering arewegleanyet.com (See bug 1944592)
// Legacy Count: 118
// Glean Count: 117

var gGlobalEnvironment;
function getGlobal() {
  if (!gGlobalEnvironment) {
    gGlobalEnvironment = new EnvironmentCache();
  }
  return gGlobalEnvironment;
}

export var TelemetryEnvironment = {
  get currentEnvironment() {
    return getGlobal().currentEnvironment;
  },

  onInitialized() {
    return getGlobal().onInitialized();
  },

  delayedInit() {
    return getGlobal().delayedInit();
  },

  registerChangeListener(name, listener) {
    return getGlobal().registerChangeListener(name, listener);
  },

  unregisterChangeListener(name) {
    return getGlobal().unregisterChangeListener(name);
  },

  /**
   * Add an experiment annotation to the environment.
   * If an annotation with the same id already exists, it will be overwritten.
   * This triggers a new subsession, subject to throttling.
   *
   * @param {String} id The id of the active experiment.
   * @param {String} branch The experiment branch.
   * @param {Object} [options] Optional object with options.
   * @param {String} [options.type=false] The specific experiment type.
   * @param {String} [options.enrollmentId=undefined] The id of the enrollment.
   */
  setExperimentActive(id, branch, options = {}) {
    if (gGlobalEnvironment) {
      gGlobalEnvironment.setExperimentActive(id, branch, options);
    } else {
      gActiveExperimentStartupBuffer.set(id, { branch, options });
    }
  },

  /**
   * Remove an experiment annotation from the environment.
   * If the annotation exists, a new subsession will triggered.
   *
   * @param {String} id The id of the active experiment.
   */
  setExperimentInactive(id) {
    if (gGlobalEnvironment) {
      gGlobalEnvironment.setExperimentInactive(id);
    } else {
      gActiveExperimentStartupBuffer.delete(id);
    }
  },

  /**
   * Returns an object containing the data for the active experiments.
   *
   * The returned object is of the format:
   *
   * {
   *   "<experiment id>": { branch: "<branch>" },
   *   // …
   * }
   */
  getActiveExperiments() {
    if (gGlobalEnvironment) {
      return gGlobalEnvironment.getActiveExperiments();
    }

    const result = {};
    for (const [id, { branch }] of gActiveExperimentStartupBuffer.entries()) {
      result[id] = branch;
    }
    return result;
  },

  shutdown() {
    return getGlobal().shutdown();
  },

  // Policy to use when saving preferences. Exported for using them in tests.
  // Reports "<user-set>" if there is a value set on the user branch
  RECORD_PREF_STATE: 1,

  // Reports the value set on the user branch, if one is set
  RECORD_PREF_VALUE: 2,

  // Reports the active value (set on either the user or default branch)
  // for this pref, if one is set
  RECORD_DEFAULTPREF_VALUE: 3,

  // Reports "<set>" if a value for this pref is defined on either the user
  // or default branch
  RECORD_DEFAULTPREF_STATE: 4,

  // Testing method
  async testWatchPreferences(prefMap) {
    return getGlobal()._watchPreferences(prefMap);
  },

  /**
   * Intended for use in tests only.
   *
   * In multiple tests we need a way to shut and re-start telemetry together
   * with TelemetryEnvironment. This is problematic due to the fact that
   * TelemetryEnvironment is a singleton. We, therefore, need this helper
   * method to be able to re-set TelemetryEnvironment.
   */
  testReset() {
    return getGlobal().reset();
  },

  /**
   * Intended for use in tests only.
   */
  testCleanRestart() {
    getGlobal().shutdownForTestCleanRestart();
    gGlobalEnvironment = null;
    gActiveExperimentStartupBuffer = new Map();
    return getGlobal();
  },
};

const RECORD_PREF_STATE = TelemetryEnvironment.RECORD_PREF_STATE;
const RECORD_PREF_VALUE = TelemetryEnvironment.RECORD_PREF_VALUE;
const RECORD_DEFAULTPREF_VALUE = TelemetryEnvironment.RECORD_DEFAULTPREF_VALUE;
const RECORD_DEFAULTPREF_STATE = TelemetryEnvironment.RECORD_DEFAULTPREF_STATE;
const DEFAULT_ENVIRONMENT_PREFS = new Map([
  ["app.feedback.baseURL", { what: RECORD_PREF_VALUE }],
  ["app.support.baseURL", { what: RECORD_PREF_VALUE }],
  ["accessibility.browsewithcaret", { what: RECORD_PREF_VALUE }],
  ["accessibility.force_disabled", { what: RECORD_PREF_VALUE }],
  ["app.normandy.test-prefs.bool", { what: RECORD_PREF_VALUE }],
  ["app.normandy.test-prefs.integer", { what: RECORD_PREF_VALUE }],
  ["app.normandy.test-prefs.string", { what: RECORD_PREF_VALUE }],
  ["app.shield.optoutstudies.enabled", { what: RECORD_PREF_VALUE }],
  ["app.update.interval", { what: RECORD_PREF_VALUE }],
  ["app.update.service.enabled", { what: RECORD_PREF_VALUE }],
  ["app.update.silent", { what: RECORD_PREF_VALUE }],
  ["browser.cache.disk.enable", { what: RECORD_PREF_VALUE }],
  ["browser.cache.disk.capacity", { what: RECORD_PREF_VALUE }],
  ["browser.cache.memory.enable", { what: RECORD_PREF_VALUE }],
  ["browser.formfill.enable", { what: RECORD_PREF_VALUE }],
  ["browser.migrate.interactions.bookmarks", { what: RECORD_PREF_VALUE }],
  ["browser.migrate.interactions.csvpasswords", { what: RECORD_PREF_VALUE }],
  ["browser.migrate.interactions.history", { what: RECORD_PREF_VALUE }],
  ["browser.migrate.interactions.passwords", { what: RECORD_PREF_VALUE }],
  ["browser.newtabpage.enabled", { what: RECORD_PREF_VALUE }],
  ["browser.privatebrowsing.autostart", { what: RECORD_PREF_VALUE }],
  ["browser.shell.checkDefaultBrowser", { what: RECORD_PREF_VALUE }],
  ["browser.search.region", { what: RECORD_PREF_VALUE }],
  ["browser.search.suggest.enabled", { what: RECORD_PREF_VALUE }],
  ["browser.startup.homepage", { what: RECORD_PREF_STATE }],
  ["browser.startup.page", { what: RECORD_PREF_VALUE }],
  ["browser.urlbar.autoFill", { what: RECORD_DEFAULTPREF_VALUE }],
  [
    "browser.urlbar.autoFill.adaptiveHistory.enabled",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  [
    "browser.urlbar.dnsResolveSingleWordsAfterSearch",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  [
    "browser.urlbar.quicksuggest.dataCollection.enabled",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  ["browser.urlbar.showSearchSuggestionsFirst", { what: RECORD_PREF_VALUE }],
  ["browser.urlbar.showSearchTerms.enabled", { what: RECORD_PREF_VALUE }],
  [
    "browser.urlbar.suggest.quicksuggest.nonsponsored",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  [
    "browser.urlbar.suggest.quicksuggest.sponsored",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  ["browser.urlbar.suggest.searches", { what: RECORD_PREF_VALUE }],
  ["devtools.chrome.enabled", { what: RECORD_PREF_VALUE }],
  ["devtools.debugger.enabled", { what: RECORD_PREF_VALUE }],
  ["devtools.debugger.remote-enabled", { what: RECORD_PREF_VALUE }],
  ["doh-rollout.doorhanger-decision", { what: RECORD_PREF_VALUE }],
  ["dom.ipc.processCount", { what: RECORD_PREF_VALUE }],
  ["dom.max_script_run_time", { what: RECORD_PREF_VALUE }],
  ["dom.popup_allowed_events", { what: RECORD_PREF_VALUE }],
  ["editor.truncate_user_pastes", { what: RECORD_PREF_VALUE }],
  ["extensions.InstallTrigger.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.InstallTriggerImpl.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.autoDisableScopes", { what: RECORD_PREF_VALUE }],
  ["extensions.blocklist.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.enabledScopes", { what: RECORD_PREF_VALUE }],
  ["extensions.eventPages.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.formautofill.addresses.enabled", { what: RECORD_PREF_VALUE }],
  [
    "extensions.formautofill.addresses.capture.enabled",
    { what: RECORD_PREF_VALUE },
  ],
  ["extensions.formautofill.creditCards.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.manifestV3.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.quarantinedDomains.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.strictCompatibility", { what: RECORD_PREF_VALUE }],
  ["extensions.update.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.update.url", { what: RECORD_PREF_VALUE }],
  ["extensions.update.background.url", { what: RECORD_PREF_VALUE }],
  ["extensions.screenshots.disabled", { what: RECORD_PREF_VALUE }],
  ["general.config.filename", { what: RECORD_DEFAULTPREF_STATE }],
  ["general.smoothScroll", { what: RECORD_PREF_VALUE }],
  ["gfx.direct2d.disabled", { what: RECORD_PREF_VALUE }],
  ["gfx.direct2d.force-enabled", { what: RECORD_PREF_VALUE }],
  ["gfx.webrender.all", { what: RECORD_PREF_VALUE }],
  ["layers.acceleration.disabled", { what: RECORD_PREF_VALUE }],
  ["layers.acceleration.force-enabled", { what: RECORD_PREF_VALUE }],
  ["layers.async-pan-zoom.enabled", { what: RECORD_PREF_VALUE }],
  ["layers.async-video-oop.enabled", { what: RECORD_PREF_VALUE }],
  ["layers.d3d11.disable-warp", { what: RECORD_PREF_VALUE }],
  ["layers.d3d11.force-warp", { what: RECORD_PREF_VALUE }],
  [
    "layers.offmainthreadcomposition.force-disabled",
    { what: RECORD_PREF_VALUE },
  ],
  ["layers.prefer-d3d9", { what: RECORD_PREF_VALUE }],
  ["layers.prefer-opengl", { what: RECORD_PREF_VALUE }],
  ["layout.css.devPixelsPerPx", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.enabled", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastInstallFailed", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastInstallFailReason", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastInstallStart", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastDownload", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastDownloadFailed", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastDownloadFailReason", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.lastUpdate", { what: RECORD_PREF_VALUE }],
  ["media.gmp-gmpopenh264.visible", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.enabled", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastInstallFailed", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastInstallFailReason", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastInstallStart", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastDownload", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastDownloadFailed", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastDownloadFailReason", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.lastUpdate", { what: RECORD_PREF_VALUE }],
  ["media.gmp-widevinecdm.visible", { what: RECORD_PREF_VALUE }],
  ["media.gmp-manager.lastCheck", { what: RECORD_PREF_VALUE }],
  ["media.gmp-manager.lastEmptyCheck", { what: RECORD_PREF_VALUE }],
  [
    "network.http.microsoft-entra-sso.enabled",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  ["network.http.windows-sso.enabled", { what: RECORD_PREF_VALUE }],
  ["network.proxy.autoconfig_url", { what: RECORD_PREF_STATE }],
  ["network.proxy.http", { what: RECORD_PREF_STATE }],
  ["network.proxy.ssl", { what: RECORD_PREF_STATE }],
  ["network.trr.mode", { what: RECORD_PREF_VALUE }],
  ["network.trr.strict_native_fallback", { what: RECORD_DEFAULTPREF_VALUE }],
  ["pdfjs.disabled", { what: RECORD_PREF_VALUE }],
  ["places.history.enabled", { what: RECORD_PREF_VALUE }],
  ["privacy.firstparty.isolate", { what: RECORD_PREF_VALUE }],
  ["privacy.resistFingerprinting", { what: RECORD_PREF_VALUE }],
  ["privacy.fingerprintingProtection", { what: RECORD_PREF_VALUE }],
  ["privacy.fingerprintingProtection.pbmode", { what: RECORD_PREF_VALUE }],
  ["privacy.trackingprotection.enabled", { what: RECORD_PREF_VALUE }],
  ["privacy.donottrackheader.enabled", { what: RECORD_PREF_VALUE }],
  ["security.enterprise_roots.auto-enabled", { what: RECORD_PREF_VALUE }],
  ["security.enterprise_roots.enabled", { what: RECORD_PREF_VALUE }],
  ["security.pki.mitm_detected", { what: RECORD_PREF_VALUE }],
  ["security.mixed_content.block_active_content", { what: RECORD_PREF_VALUE }],
  ["security.mixed_content.block_display_content", { what: RECORD_PREF_VALUE }],
  ["security.tls.version.enable-deprecated", { what: RECORD_PREF_VALUE }],
  ["signon.management.page.breach-alerts.enabled", { what: RECORD_PREF_VALUE }],
  ["signon.autofillForms", { what: RECORD_PREF_VALUE }],
  ["signon.generation.enabled", { what: RECORD_PREF_VALUE }],
  ["signon.rememberSignons", { what: RECORD_PREF_VALUE }],
  ["signon.firefoxRelay.feature", { what: RECORD_PREF_VALUE }],
  [
    "widget.content.gtk-high-contrast.enabled",
    { what: RECORD_DEFAULTPREF_VALUE },
  ],
  ["xpinstall.signatures.required", { what: RECORD_PREF_VALUE }],
  [
    "xpinstall.signatures.weakSignaturesTemporarilyAllowed",
    { what: RECORD_PREF_VALUE },
  ],
  ["nimbus.debug", { what: RECORD_PREF_VALUE }],
]);

if (AppConstants.platform == "linux" || AppConstants.platform == "macosx") {
  DEFAULT_ENVIRONMENT_PREFS.set(
    "intl.ime.use_composition_events_for_insert_text",
    { what: RECORD_PREF_VALUE }
  );
}

const LOGGER_NAME = "Toolkit.Telemetry";

const PREF_BLOCKLIST_ENABLED = "extensions.blocklist.enabled";
const PREF_DISTRIBUTION_ID = "distribution.id";
const PREF_DISTRIBUTION_VERSION = "distribution.version";
const PREF_DISTRIBUTOR = "app.distributor";
const PREF_DISTRIBUTOR_CHANNEL = "app.distributor.channel";
const PREF_APP_PARTNER_BRANCH = "app.partner.";
const PREF_PARTNER_ID = "mozilla.partner.id";

const COMPOSITOR_CREATED_TOPIC = "compositor:created";
const COMPOSITOR_PROCESS_ABORTED_TOPIC = "compositor:process-aborted";
const DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC =
  "distribution-customization-complete";
const GFX_FEATURES_READY_TOPIC = "gfx-features-ready";
const SEARCH_ENGINE_MODIFIED_TOPIC = "browser-search-engine-modified";
const SEARCH_SERVICE_TOPIC = "browser-search-service";
const SESSIONSTORE_WINDOWS_RESTORED_TOPIC = "sessionstore-windows-restored";
const PREF_CHANGED_TOPIC = "nsPref:changed";
const GMP_PROVIDER_REGISTERED_TOPIC = "gmp-provider-registered";
const AUTO_UPDATE_PREF_CHANGE_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.auto"].observerTopic;
const BACKGROUND_UPDATE_PREF_CHANGE_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.background.enabled"]
    .observerTopic;
const SERVICES_INFO_CHANGE_TOPIC = "sync-ui-state:update";

/**
 * Enforces the parameter to a boolean value.
 * @param aValue The input value.
 * @return {Boolean|Object} If aValue is a boolean or a number, returns its truthfulness
 *         value. Otherwise, return null.
 */
function enforceBoolean(aValue) {
  if (typeof aValue !== "number" && typeof aValue !== "boolean") {
    return null;
  }
  return Boolean(aValue);
}

/**
 * Get the current browser locale.
 * @return a string with the locale or null on failure.
 */
function getBrowserLocale() {
  try {
    return Services.locale.appLocaleAsBCP47;
  } catch (e) {
    return null;
  }
}

/**
 * Get the current OS locale.
 * @return a string with the OS locale or null on failure.
 */
function getSystemLocale() {
  try {
    return Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    ).systemLocale;
  } catch (e) {
    return null;
  }
}

/**
 * Get the current OS locales.
 * @return an array of strings with the OS locales or null on failure.
 */
function getSystemLocales() {
  try {
    return Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    ).systemLocales;
  } catch (e) {
    return null;
  }
}

/**
 * Get the current OS regional preference locales.
 * @return an array of strings with the OS regional preference locales or null on failure.
 */
function getRegionalPrefsLocales() {
  try {
    return Cc["@mozilla.org/intl/ospreferences;1"].getService(
      Ci.mozIOSPreferences
    ).regionalPrefsLocales;
  } catch (e) {
    return null;
  }
}

function getIntlSettings() {
  let intl = {
    requestedLocales: Services.locale.requestedLocales,
    availableLocales: Services.locale.availableLocales,
    appLocales: Services.locale.appLocalesAsBCP47,
    systemLocales: getSystemLocales(),
    regionalPrefsLocales: getRegionalPrefsLocales(),
    acceptLanguages: Services.prefs
      .getComplexValue("intl.accept_languages", Ci.nsIPrefLocalizedString)
      .data.split(",")
      .map(str => str.trim()),
  };
  Glean.intl.requestedLocales.set(intl.requestedLocales);
  Glean.intl.availableLocales.set(intl.availableLocales);
  Glean.intl.appLocales.set(intl.appLocales);
  Glean.intl.systemLocales.set(intl.systemLocales);
  Glean.intl.regionalPrefsLocales.set(intl.regionalPrefsLocales);
  Glean.intl.acceptLanguages.set(intl.acceptLanguages);
  return intl;
}

/**
 * Safely get a sysinfo property and return its value. If the property is not
 * available, return aDefault.
 *
 * @param aPropertyName the property name to get.
 * @param aDefault the value to return if aPropertyName is not available.
 * @return The property value, if available, or aDefault.
 */
function getSysinfoProperty(aPropertyName, aDefault) {
  try {
    // |getProperty| may throw if |aPropertyName| does not exist.
    return Services.sysinfo.getProperty(aPropertyName);
  } catch (e) {}

  return aDefault;
}

/**
 * Safely get a gfxInfo field and return its value. If the field is not available, return
 * aDefault.
 *
 * @param aPropertyName the property name to get.
 * @param aDefault the value to return if aPropertyName is not available.
 * @return The property value, if available, or aDefault.
 */
function getGfxField(aPropertyName, aDefault) {
  let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);

  try {
    // Accessing the field may throw if |aPropertyName| does not exist.
    let gfxProp = gfxInfo[aPropertyName];
    if (gfxProp !== undefined && gfxProp !== "") {
      return gfxProp;
    }
  } catch (e) {}

  return aDefault;
}

/**
 * Returns a substring of the input string.
 *
 * @param {String} aString The input string.
 * @param {Integer} aMaxLength The maximum length of the returned substring. If this is
 *        greater than the length of the input string, we return the whole input string.
 * @return {String} The substring or null if the input string is null.
 */
function limitStringToLength(aString, aMaxLength) {
  if (typeof aString !== "string") {
    return null;
  }
  return aString.substring(0, aMaxLength);
}

/**
 * Force a value to be a string.
 * Only if the value is null, null is returned instead.
 */
function forceToStringOrNull(aValue) {
  if (aValue === null) {
    return null;
  }

  return String(aValue);
}

/**
 * Get the information about a graphic adapter.
 *
 * @param aSuffix A suffix to add to the properties names.
 * @return An object containing the adapter properties.
 */
function getGfxAdapter(aSuffix = "") {
  // Note that gfxInfo, and so getGfxField, might return "Unknown" for the RAM on failures,
  // not null.
  let memoryMB = parseInt(getGfxField("adapterRAM" + aSuffix, null), 10);
  if (Number.isNaN(memoryMB)) {
    memoryMB = null;
  }

  return {
    description: getGfxField("adapterDescription" + aSuffix, null),
    vendorID: getGfxField("adapterVendorID" + aSuffix, null),
    deviceID: getGfxField("adapterDeviceID" + aSuffix, null),
    subsysID: getGfxField("adapterSubsysID" + aSuffix, null),
    RAM: memoryMB,
    driver: getGfxField("adapterDriver" + aSuffix, null),
    driverVendor: getGfxField("adapterDriverVendor" + aSuffix, null),
    driverVersion: getGfxField("adapterDriverVersion" + aSuffix, null),
    driverDate: getGfxField("adapterDriverDate" + aSuffix, null),
  };
}

/**
 * Encapsulates the asynchronous magic interfacing with the addon manager. The builder
 * is owned by a parent environment object and is an addon listener.
 */
function EnvironmentAddonBuilder(environment) {
  this._environment = environment;

  // The pending task blocks addon manager shutdown. It can either be the initial load
  // or a change load.
  this._pendingTask = null;

  // Have we added an observer to listen for blocklist changes that still needs to be
  // removed:
  this._gmpProviderObserverAdded = false;

  // Set to true once initial load is complete and we're watching for changes.
  this._loaded = false;

  // The state reported by the shutdown blocker if we hang shutdown.
  this._shutdownState = "Initial";
}
EnvironmentAddonBuilder.prototype = {
  /**
   * Get the initial set of addons.
   * @returns Promise<void> when the initial load is complete.
   */
  async init() {
    AddonManager.beforeShutdown.addBlocker(
      "EnvironmentAddonBuilder",
      () => this._shutdownBlocker(),
      { fetchState: () => this._shutdownState }
    );

    this._pendingTask = (async () => {
      try {
        this._shutdownState = "Awaiting _updateAddons";
        // Gather initial addons details
        await this._updateAddons();

        if (!this._environment._addonsAreFull) {
          // The addon database has not been loaded, wait for it to
          // initialize and gather full data as soon as it does.
          this._shutdownState = "Awaiting AddonManagerPrivate.databaseReady";
          await AddonManagerPrivate.databaseReady;

          // Now gather complete addons details.
          this._shutdownState = "Awaiting second _updateAddons";
          await this._updateAddons();
        }
      } catch (err) {
        this._environment._log.error("init - Exception in _updateAddons", err);
      } finally {
        this._pendingTask = null;
        this._shutdownState = "_pendingTask init complete. No longer blocking.";
      }
    })();

    return this._pendingTask;
  },

  /**
   * Register an addon listener and watch for changes.
   */
  watchForChanges() {
    this._loaded = true;
    AddonManager.addAddonListener(this);
  },

  // AddonListener
  onEnabled(addon) {
    this._onAddonChange(addon);
  },
  onDisabled(addon) {
    this._onAddonChange(addon);
  },
  onInstalled(addon) {
    this._onAddonChange(addon);
  },
  onUninstalling(addon) {
    this._onAddonChange(addon);
  },
  onUninstalled(addon) {
    this._onAddonChange(addon);
  },
  onPropertyChanged(addon, propertiesChanged) {
    // Avoid to update the telemetry environment for onPropertyChanged
    // calls that we are not actually interested in (and quarantineIgnoredByApp
    // is not expected to change at runtime, unless the entire active addons
    // entry is also replaced, e.g. on the extension being uninstalled and
    // installed again).
    if (!propertiesChanged.includes("quarantineIgnoredByUser")) {
      return;
    }
    this._onAddonChange(addon);
  },

  _onAddonChange(addon) {
    if (addon && addon.isBuiltin && !addon.isSystem) {
      return;
    }
    this._environment._log.trace("_onAddonChange");
    this._checkForChanges("addons-changed");
  },

  // nsIObserver
  observe(aSubject, aTopic) {
    this._environment._log.trace("observe - Topic " + aTopic);
    if (aTopic == GMP_PROVIDER_REGISTERED_TOPIC) {
      Services.obs.removeObserver(this, GMP_PROVIDER_REGISTERED_TOPIC);
      this._gmpProviderObserverAdded = false;
      let gmpPluginsPromise = this._getActiveGMPlugins();
      gmpPluginsPromise.then(
        gmpPlugins => {
          let { addons } = this._environment._currentEnvironment;
          addons.activeGMPlugins = gmpPlugins;
        },
        err => {
          this._environment._log.error(
            "blocklist observe: Error collecting plugins",
            err
          );
        }
      );
    }
  },

  _checkForChanges(changeReason) {
    if (this._pendingTask) {
      this._environment._log.trace(
        "_checkForChanges - task already pending, dropping change with reason " +
          changeReason
      );
      return;
    }

    this._shutdownState = "_checkForChanges awaiting _updateAddons";
    this._pendingTask = this._updateAddons().then(
      result => {
        this._pendingTask = null;
        this._shutdownState = "No longer blocking, _updateAddons resolved";
        if (result.changed) {
          this._environment._onEnvironmentChange(
            changeReason,
            result.oldEnvironment
          );
        }
      },
      err => {
        this._pendingTask = null;
        this._shutdownState = "No longer blocking, _updateAddons rejected";
        this._environment._log.error(
          "_checkForChanges: Error collecting addons",
          err
        );
      }
    );
  },

  _shutdownBlocker() {
    if (this._loaded) {
      AddonManager.removeAddonListener(this);
      if (this._gmpProviderObserverAdded) {
        Services.obs.removeObserver(this, GMP_PROVIDER_REGISTERED_TOPIC);
      }
    }

    // At startup, _pendingTask is set to a Promise that does not resolve
    // until the addons database has been read so complete details about
    // addons are available.  Returning it here will cause it to block
    // profileBeforeChange, guranteeing that full information will be
    // available by the time profileBeforeChangeTelemetry is fired.
    return this._pendingTask;
  },

  /**
   * Collect the addon data for the environment.
   *
   * This should only be called from _pendingTask; otherwise we risk
   * running this during addon manager shutdown.
   *
   * @returns Promise<Object> This returns a Promise resolved with a status object with the following members:
   *   changed - Whether the environment changed.
   *   oldEnvironment - Only set if a change occured, contains the environment data before the change.
   */
  async _updateAddons() {
    this._environment._log.trace("_updateAddons");

    let addons = {
      activeAddons: await this._getActiveAddons(),
      theme: await this._getActiveTheme(),
      activeGMPlugins: await this._getActiveGMPlugins(),
    };

    let result = {
      changed:
        !this._environment._currentEnvironment.addons ||
        !ObjectUtils.deepEqual(
          addons.activeAddons,
          this._environment._currentEnvironment.addons.activeAddons
        ),
    };

    if (result.changed) {
      this._environment._log.trace("_updateAddons: addons differ");
      result.oldEnvironment = Cu.cloneInto(
        this._environment._currentEnvironment,
        {}
      );
    }
    this._environment._currentEnvironment.addons = addons;

    // Convert into the appropriate schema and record the addon environment
    // data in Glean
    let activeAddonsGlean = Object.entries(addons.activeAddons).map(
      ([id, { type, ...rest }]) => ({ id, addonType: type, ...rest })
    );
    Glean.addons.activeAddons.set(activeAddonsGlean);
    Glean.addons.theme.set(addons.theme);
    Glean.addons.activeGMPlugins.set(
      Object.entries(addons.activeGMPlugins).map(([id, value]) => ({
        id,
        ...value,
      }))
    );

    return result;
  },

  /**
   * Get the addon data in object form.
   * @return Promise<object> containing the addon data.
   */
  async _getActiveAddons() {
    // Request addons, asynchronously.
    // "theme" is excluded because it is already handled by _getActiveTheme.
    let { addons: allAddons, fullData } = await AddonManager.getActiveAddons(
      AddonManagerPrivate.getAddonTypesByProvider("XPIProvider").filter(
        addonType => addonType != "theme"
      )
    );

    this._environment._addonsAreFull = fullData;
    let activeAddons = {};
    for (let addon of allAddons) {
      // Don't collect any information about the new built-in search webextensions
      if (addon.isBuiltin && !addon.isSystem) {
        continue;
      }
      // Weird addon data in the wild can lead to exceptions while collecting
      // the data.
      try {
        // Make sure to have valid dates (built-in add-ons are
        // expected to not have a valid update date).
        let updateDate = isNaN(addon.updateDate?.valueOf())
          ? new Date(0)
          : new Date(Math.max(0, addon.updateDate));

        activeAddons[addon.id] = {
          version: limitStringToLength(addon.version, MAX_ADDON_STRING_LENGTH),
          scope: addon.scope,
          type: addon.type,
          updateDay: Utils.millisecondsToDays(updateDate.getTime()),
          isSystem: addon.isSystem,
          isWebExtension: addon.isWebExtension,
          multiprocessCompatible: true,
        };

        // getActiveAddons() gives limited data during startup and full
        // data after the addons database is loaded.
        if (fullData) {
          // Make sure to have valid dates (built-in add-ons are
          // expected to not have a valid install date).
          let installDate = isNaN(addon.installDate?.valueOf())
            ? new Date(0)
            : new Date(Math.max(0, addon.installDate));

          Object.assign(activeAddons[addon.id], {
            blocklisted:
              addon.blocklistState !== Ci.nsIBlocklistService.STATE_NOT_BLOCKED,
            description: limitStringToLength(
              addon.description,
              MAX_ADDON_STRING_LENGTH
            ),
            name: limitStringToLength(addon.name, MAX_ADDON_STRING_LENGTH),
            userDisabled: enforceBoolean(addon.userDisabled),
            appDisabled: addon.appDisabled,
            foreignInstall: enforceBoolean(addon.foreignInstall),
            hasBinaryComponents: false,
            installDay: Utils.millisecondsToDays(installDate.getTime()),
            signedState: addon.signedState,
            signedTypes: JSON.stringify(addon.signedTypes),
            quarantineIgnoredByApp: enforceBoolean(
              addon.quarantineIgnoredByApp
            ),
            quarantineIgnoredByUser: enforceBoolean(
              addon.quarantineIgnoredByUser
            ),
          });
        }
      } catch (ex) {
        this._environment._log.error(
          "_getActiveAddons - An addon was discarded due to an error",
          ex
        );
        continue;
      }
    }

    return activeAddons;
  },

  /**
   * Get the currently active theme data in object form.
   * @return Promise<object> containing the active theme data.
   */
  async _getActiveTheme() {
    // Request themes, asynchronously.
    let { addons: themes } = await AddonManager.getActiveAddons(["theme"]);

    let activeTheme = {};
    // We only store information about the active theme.
    let theme = themes.find(theme => theme.isActive);
    if (theme) {
      // Make sure to have valid dates.
      let installDate = new Date(Math.max(0, theme.installDate));
      let updateDate = new Date(Math.max(0, theme.updateDate));

      activeTheme = {
        id: theme.id,
        blocklisted:
          theme.blocklistState !== Ci.nsIBlocklistService.STATE_NOT_BLOCKED,
        description: limitStringToLength(
          theme.description,
          MAX_ADDON_STRING_LENGTH
        ),
        name: limitStringToLength(theme.name, MAX_ADDON_STRING_LENGTH),
        userDisabled: enforceBoolean(theme.userDisabled),
        appDisabled: theme.appDisabled,
        version: limitStringToLength(theme.version, MAX_ADDON_STRING_LENGTH),
        scope: theme.scope,
        foreignInstall: enforceBoolean(theme.foreignInstall),
        hasBinaryComponents: false,
        installDay: Utils.millisecondsToDays(installDate.getTime()),
        updateDay: Utils.millisecondsToDays(updateDate.getTime()),
        signedState: theme.signedState,
        signedTypes: JSON.stringify(theme.signedTypes),
      };
    }

    return activeTheme;
  },

  /**
   * Get the GMPlugins data in object form.
   *
   * @return Object containing the GMPlugins data.
   *
   * This should only be called from _pendingTask; otherwise we risk
   * running this during addon manager shutdown.
   */
  async _getActiveGMPlugins() {
    // If we haven't yet loaded the blocklist, pass back dummy data for now,
    // and add an observer to update this data as soon as we get it.
    if (!AddonManager.hasProvider("GMPProvider")) {
      if (!this._gmpProviderObserverAdded) {
        Services.obs.addObserver(this, GMP_PROVIDER_REGISTERED_TOPIC);
        this._gmpProviderObserverAdded = true;
      }
      return {
        "dummy-gmp": {
          version: "0.1",
          userDisabled: false,
          applyBackgroundUpdates: 1,
        },
      };
    }
    // Request plugins, asynchronously.
    let allPlugins = await AddonManager.getAddonsByTypes(["plugin"]);

    let activeGMPlugins = {};
    for (let plugin of allPlugins) {
      // Only get info for active GMplugins.
      if (!plugin.isGMPlugin || !plugin.isActive) {
        continue;
      }

      try {
        activeGMPlugins[plugin.id] = {
          version: plugin.version,
          userDisabled: enforceBoolean(plugin.userDisabled),
          applyBackgroundUpdates: plugin.applyBackgroundUpdates,
        };
      } catch (ex) {
        this._environment._log.error(
          "_getActiveGMPlugins - A GMPlugin was discarded due to an error",
          ex
        );
        continue;
      }
    }

    return activeGMPlugins;
  },
};

function EnvironmentCache() {
  this._log = Log.repository.getLoggerWithMessagePrefix(
    LOGGER_NAME,
    "TelemetryEnvironment::"
  );
  this._log.trace("constructor");

  this._shutdown = false;
  // Don't allow querying the search service too early to prevent
  // impacting the startup performance.
  this._canQuerySearch = false;
  // To guard against slowing down startup, defer gathering heavy environment
  // entries until the session is restored.
  this._sessionWasRestored = false;

  // A map of listeners that will be called on environment changes.
  this._changeListeners = new Map();

  // A map of watched preferences which trigger an Environment change when
  // modified. Every entry contains a recording policy (RECORD_PREF_*).
  this._watchedPrefs = DEFAULT_ENVIRONMENT_PREFS;

  this._currentEnvironment = {
    build: this._getBuild(),
    partner: this._getPartner(),
    system: this._getSystem(),
  };

  this._addObservers();

  // Build the remaining asynchronous parts of the environment. Don't register change listeners
  // until the initial environment has been built.

  let p = [this._updateSettings()];
  this._addonBuilder = new EnvironmentAddonBuilder(this);
  p.push(this._addonBuilder.init());

  this._currentEnvironment.profile = {};
  p.push(this._updateProfile());
  if (AppConstants.MOZ_BUILD_APP == "browser") {
    p.push(this._loadAttributionAsync());
  }
  p.push(this._loadAsyncUpdateSettings());
  p.push(this._loadIntlData());

  for (const [
    id,
    { branch, options },
  ] of gActiveExperimentStartupBuffer.entries()) {
    this.setExperimentActive(id, branch, options);
  }
  gActiveExperimentStartupBuffer = null;

  let setup = () => {
    this._initTask = null;
    this._startWatchingPrefs();
    this._addonBuilder.watchForChanges();
    this._updateGraphicsFeatures();
    return this.currentEnvironment;
  };

  this._initTask = Promise.all(p).then(
    () => setup(),
    err => {
      // log errors but eat them for consumers
      this._log.error("EnvironmentCache - error while initializing", err);
      return setup();
    }
  );

  // Addons may contain partial or full data depending on whether the Addons DB
  // has had a chance to load. Do we have full data yet?
  this._addonsAreFull = false;
}
EnvironmentCache.prototype = {
  /**
   * The current environment data. The returned data is cloned to avoid
   * unexpected sharing or mutation.
   * @returns object
   */
  get currentEnvironment() {
    return Cu.cloneInto(this._currentEnvironment, {});
  },

  /**
   * Wait for the current enviroment to be fully initialized.
   * @returns Promise<object>
   */
  onInitialized() {
    if (this._initTask) {
      return this._initTask;
    }
    return Promise.resolve(this.currentEnvironment);
  },

  /**
   * This gets called when the delayed init completes.
   */
  async delayedInit() {
    this._processData = await Services.sysinfo.processInfo;
    let processData = await Services.sysinfo.processInfo;
    // Remove isWow64 and isWowARM64 from processData
    // to strip it down to just CPU info
    delete processData.isWow64;
    delete processData.isWowARM64;

    let oldEnv = null;
    if (!this._initTask) {
      oldEnv = this.currentEnvironment;
    }

    this._cpuData = this._getCPUData();
    // Augment the return value from the promises with cached values
    this._cpuData = { ...processData, ...this._cpuData };

    this._currentEnvironment.system.cpu = this._getCPUData();

    if (AppConstants.platform == "win") {
      this._hddData = await Services.sysinfo.diskInfo;
      for (const [name, disk] of Object.entries(this._hddData)) {
        if (!disk) {
          continue;
        }
        // Glean `object` metrics don't like `type` as it's a reserved word.
        let diskData = { ...disk, diskType: disk.type };
        delete diskData.type;
        Glean.hdd[name].set(diskData);
      }
      let osData = await Services.sysinfo.osInfo;

      if (!this._initTask) {
        // We've finished creating the initial env, so notify for the update
        // This is all a bit awkward because `currentEnvironment` clones
        // the object, which we need to pass to the notification, but we
        // should only notify once we've updated the current environment...
        // Ideally, _onEnvironmentChange should somehow deal with all this
        // instead of all the consumers.
        oldEnv = this.currentEnvironment;
      }

      this._osData = this._getOSData();

      // Augment the return values from the promises with cached values
      this._osData = Object.assign(osData, this._osData);

      this._currentEnvironment.system.os = this._getOSData();
      this._currentEnvironment.system.hdd = this._getHDDData();

      // Windows only values stored in processData
      this._currentEnvironment.system.isWow64 = this._getProcessData().isWow64;
      this._currentEnvironment.system.isWowARM64 =
        this._getProcessData().isWowARM64;
    }

    if (!this._initTask) {
      this._onEnvironmentChange("system-info", oldEnv);
    }
  },

  /**
   * Register a listener for environment changes.
   * @param name The name of the listener. If a new listener is registered
   *             with the same name, the old listener will be replaced.
   * @param listener function(reason, oldEnvironment) - Will receive a reason for
                     the change and the environment data before the change.
   */
  registerChangeListener(name, listener) {
    this._log.trace("registerChangeListener for " + name);
    if (this._shutdown) {
      this._log.warn("registerChangeListener - already shutdown");
      return;
    }
    this._changeListeners.set(name, listener);
  },

  /**
   * Unregister from listening to environment changes.
   * It's fine to call this on an unitialized TelemetryEnvironment.
   * @param name The name of the listener to remove.
   */
  unregisterChangeListener(name) {
    this._log.trace("unregisterChangeListener for " + name);
    if (this._shutdown) {
      this._log.warn("registerChangeListener - already shutdown");
      return;
    }
    this._changeListeners.delete(name);
  },

  setExperimentActive(id, branch, options) {
    this._log.trace(`setExperimentActive - id: ${id}, branch: ${branch}`);
    // Make sure both the id and the branch have sane lengths.
    const saneId = limitStringToLength(id, MAX_EXPERIMENT_ID_LENGTH);
    const saneBranch = limitStringToLength(
      branch,
      MAX_EXPERIMENT_BRANCH_LENGTH
    );
    if (!saneId || !saneBranch) {
      this._log.error(
        "setExperimentActive - the provided arguments are not strings."
      );
      return;
    }

    // Warn the user about any content truncation.
    if (saneId.length != id.length || saneBranch.length != branch.length) {
      this._log.warn(
        "setExperimentActive - the experiment id or branch were truncated."
      );
    }

    // Truncate the experiment type if present.
    if (options.hasOwnProperty("type")) {
      let type = limitStringToLength(options.type, MAX_EXPERIMENT_TYPE_LENGTH);
      if (type.length != options.type.length) {
        options.type = type;
        this._log.warn(
          "setExperimentActive - the experiment type was truncated."
        );
      }
    }

    // Truncate the enrollment id if present.
    if (options.hasOwnProperty("enrollmentId")) {
      let enrollmentId = limitStringToLength(
        options.enrollmentId,
        MAX_EXPERIMENT_ENROLLMENT_ID_LENGTH
      );
      if (enrollmentId.length != options.enrollmentId.length) {
        options.enrollmentId = enrollmentId;
        this._log.warn(
          "setExperimentActive - the enrollment id was truncated."
        );
      }
    }

    let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
    // Add the experiment annotation.
    let experiments = this._currentEnvironment.experiments || {};
    experiments[saneId] = { branch: saneBranch };
    if (options.hasOwnProperty("type")) {
      experiments[saneId].type = options.type;
    }
    if (options.hasOwnProperty("enrollmentId")) {
      experiments[saneId].enrollmentId = options.enrollmentId;
    }
    this._currentEnvironment.experiments = experiments;
    // Notify of the change.
    this._onEnvironmentChange("experiment-annotation-changed", oldEnvironment);
  },

  setExperimentInactive(id) {
    this._log.trace("setExperimentInactive");
    let experiments = this._currentEnvironment.experiments || {};
    if (id in experiments) {
      // Only attempt to notify if a previous annotation was found and removed.
      let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
      // Remove the experiment annotation.
      delete this._currentEnvironment.experiments[id];
      // Notify of the change.
      this._onEnvironmentChange(
        "experiment-annotation-changed",
        oldEnvironment
      );
    }
  },

  getActiveExperiments() {
    return Cu.cloneInto(this._currentEnvironment.experiments || {}, {});
  },

  shutdown() {
    this._log.trace("shutdown");
    this._shutdown = true;
  },

  shutdownForTestCleanRestart() {
    // The testcase will re-create a new EnvironmentCache instance.
    // The observer should be removed for the old instance.
    this._stopWatchingPrefs();
    this.shutdown();
  },

  /**
   * Only used in tests, set the preferences to watch.
   * @param aPreferences A map of preferences names and their recording policy.
   */
  _watchPreferences(aPreferences) {
    this._stopWatchingPrefs();
    this._watchedPrefs = aPreferences;
    this._updateSettings();
    this._startWatchingPrefs();
  },

  /**
   * Get an object containing the values for the watched preferences. Depending on the
   * policy, the value for a preference or whether it was changed by user is reported.
   *
   * @return An object containing the preferences values.
   */
  _getPrefData() {
    let prefData = {};
    for (let [pref, policy] of this._watchedPrefs.entries()) {
      let prefValue = this._getPrefValue(pref, policy.what);

      if (prefValue === undefined) {
        continue;
      }

      prefData[pref] = prefValue;
    }
    return prefData;
  },

  /**
   * Get the value of a preference given the preference name and the policy.
   * @param pref Name of the preference.
   * @param what Policy of the preference.
   *
   * @returns The value we need to store for this preference. It can be undefined
   *          or null if the preference is invalid or has a value set by the user.
   */
  _getPrefValue(pref, what) {
    // Check the policy for the preference and decide if we need to store its value
    // or whether it changed from the default value.
    let prefType = Services.prefs.getPrefType(pref);

    if (
      what == TelemetryEnvironment.RECORD_DEFAULTPREF_VALUE ||
      what == TelemetryEnvironment.RECORD_DEFAULTPREF_STATE
    ) {
      // For default prefs, make sure they exist
      if (prefType == Ci.nsIPrefBranch.PREF_INVALID) {
        return undefined;
      }
    } else if (!Services.prefs.prefHasUserValue(pref)) {
      // For user prefs, make sure they are set
      return undefined;
    }

    if (what == TelemetryEnvironment.RECORD_DEFAULTPREF_STATE) {
      return "<set>";
    } else if (what == TelemetryEnvironment.RECORD_PREF_STATE) {
      return "<user-set>";
    } else if (prefType == Ci.nsIPrefBranch.PREF_STRING) {
      return Services.prefs.getStringPref(pref);
    } else if (prefType == Ci.nsIPrefBranch.PREF_BOOL) {
      return Services.prefs.getBoolPref(pref);
    } else if (prefType == Ci.nsIPrefBranch.PREF_INT) {
      return Services.prefs.getIntPref(pref);
    } else if (prefType == Ci.nsIPrefBranch.PREF_INVALID) {
      return null;
    }
    throw new Error(
      `Unexpected preference type ("${prefType}") for "${pref}".`
    );
  },

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  /**
   * Start watching the preferences.
   */
  _startWatchingPrefs() {
    this._log.trace("_startWatchingPrefs - " + this._watchedPrefs);

    Services.prefs.addObserver("", this);
  },

  _onPrefChanged(aData) {
    this._log.trace("_onPrefChanged");
    let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
    this._currentEnvironment.settings.userPrefs[aData] = this._getPrefValue(
      aData,
      this._watchedPrefs.get(aData).what
    );
    this._onEnvironmentChange("pref-changed", oldEnvironment);
  },

  /**
   * Do not receive any more change notifications for the preferences.
   */
  _stopWatchingPrefs() {
    this._log.trace("_stopWatchingPrefs");

    Services.prefs.removeObserver("", this);
  },

  _addObservers() {
    // Watch the search engine change and service topics.
    Services.obs.addObserver(this, SESSIONSTORE_WINDOWS_RESTORED_TOPIC);
    Services.obs.addObserver(this, COMPOSITOR_CREATED_TOPIC);
    Services.obs.addObserver(this, COMPOSITOR_PROCESS_ABORTED_TOPIC);
    Services.obs.addObserver(this, DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC);
    Services.obs.addObserver(this, GFX_FEATURES_READY_TOPIC);
    Services.obs.addObserver(this, SEARCH_ENGINE_MODIFIED_TOPIC);
    Services.obs.addObserver(this, SEARCH_SERVICE_TOPIC);
    Services.obs.addObserver(this, AUTO_UPDATE_PREF_CHANGE_TOPIC);
    Services.obs.addObserver(this, BACKGROUND_UPDATE_PREF_CHANGE_TOPIC);
    Services.obs.addObserver(this, SERVICES_INFO_CHANGE_TOPIC);
  },

  _removeObservers() {
    Services.obs.removeObserver(this, SESSIONSTORE_WINDOWS_RESTORED_TOPIC);
    Services.obs.removeObserver(this, COMPOSITOR_CREATED_TOPIC);
    Services.obs.removeObserver(this, COMPOSITOR_PROCESS_ABORTED_TOPIC);
    try {
      Services.obs.removeObserver(
        this,
        DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC
      );
    } catch (ex) {}
    Services.obs.removeObserver(this, GFX_FEATURES_READY_TOPIC);
    Services.obs.removeObserver(this, SEARCH_ENGINE_MODIFIED_TOPIC);
    Services.obs.removeObserver(this, SEARCH_SERVICE_TOPIC);
    Services.obs.removeObserver(this, AUTO_UPDATE_PREF_CHANGE_TOPIC);
    Services.obs.removeObserver(this, BACKGROUND_UPDATE_PREF_CHANGE_TOPIC);
    Services.obs.removeObserver(this, SERVICES_INFO_CHANGE_TOPIC);
  },

  observe(aSubject, aTopic, aData) {
    this._log.trace("observe - aTopic: " + aTopic + ", aData: " + aData);
    switch (aTopic) {
      case SEARCH_ENGINE_MODIFIED_TOPIC:
        if (
          aData != "engine-default" &&
          aData != "engine-default-private" &&
          aData != "engine-changed"
        ) {
          return;
        }
        if (
          aData == "engine-changed" &&
          aSubject.QueryInterface(Ci.nsISearchEngine) &&
          Services.search.defaultEngine != aSubject
        ) {
          return;
        }
        // Record the new default search choice and send the change notification.
        this._onSearchEngineChange();
        break;
      case SEARCH_SERVICE_TOPIC:
        if (aData != "init-complete") {
          return;
        }
        // Now that the search engine init is complete, record the default search choice.
        this._canQuerySearch = true;
        this._updateSearchEngine();
        break;
      case GFX_FEATURES_READY_TOPIC:
      case COMPOSITOR_CREATED_TOPIC:
        // Full graphics information is not available until we have created at
        // least one off-main-thread-composited window. Thus we wait for the
        // first compositor to be created and then query nsIGfxInfo again.
        this._updateGraphicsFeatures();
        break;
      case COMPOSITOR_PROCESS_ABORTED_TOPIC:
        // Our compositor process has been killed for whatever reason, so refresh
        // our reported graphics features and trigger an environment change.
        this._onCompositorProcessAborted();
        break;
      case DISTRIBUTION_CUSTOMIZATION_COMPLETE_TOPIC:
        // Distribution customizations are applied after final-ui-startup. query
        // partner prefs again when they are ready.
        this._updatePartner();
        Services.obs.removeObserver(this, aTopic);
        break;
      case SESSIONSTORE_WINDOWS_RESTORED_TOPIC:
        this._sessionWasRestored = true;
        // Make sure to initialize the search service once we've done restoring
        // the windows, so that we don't risk loosing search data.
        Services.search.init();
        // The default browser check could take some time, so just call it after
        // the session was restored.
        this._updateDefaultBrowser();
        break;
      case PREF_CHANGED_TOPIC:
        let options = this._watchedPrefs.get(aData);
        if (options && !options.requiresRestart) {
          this._onPrefChanged(aData);
        }
        break;
      case AUTO_UPDATE_PREF_CHANGE_TOPIC:
        this._currentEnvironment.settings.update.autoDownload = aData == "true";
        Glean.updateSettings.autoDownload.set(
          this._currentEnvironment.settings.update.autoDownload
        );
        break;
      case BACKGROUND_UPDATE_PREF_CHANGE_TOPIC:
        this._currentEnvironment.settings.update.background = aData == "true";
        Glean.updateSettings.background.set(
          this._currentEnvironment.settings.update.background
        );
        break;
      case SERVICES_INFO_CHANGE_TOPIC:
        this._updateServicesInfo();
        break;
    }
  },

  /**
   * Update the default search engine value.
   */
  _updateSearchEngine() {
    if (!this._canQuerySearch) {
      this._log.trace("_updateSearchEngine - ignoring early call");
      return;
    }

    this._log.trace(
      "_updateSearchEngine - isInitialized: " + Services.search.isInitialized
    );
    if (!Services.search.isInitialized) {
      return;
    }

    // Make sure we have a settings section.
    this._currentEnvironment.settings = this._currentEnvironment.settings || {};

    // Update the search engine entry in the current environment.
    const defaultEngineInfo = Services.search.getDefaultEngineInfo();
    this._currentEnvironment.settings.defaultSearchEngine =
      defaultEngineInfo.defaultSearchEngine;
    this._currentEnvironment.settings.defaultSearchEngineData = {
      ...defaultEngineInfo.defaultSearchEngineData,
    };
    if ("defaultPrivateSearchEngine" in defaultEngineInfo) {
      this._currentEnvironment.settings.defaultPrivateSearchEngine =
        defaultEngineInfo.defaultPrivateSearchEngine;
    }
    if ("defaultPrivateSearchEngineData" in defaultEngineInfo) {
      this._currentEnvironment.settings.defaultPrivateSearchEngineData = {
        ...defaultEngineInfo.defaultPrivateSearchEngineData,
      };
    }
  },

  /**
   * Update the default search engine value and trigger the environment change.
   */
  _onSearchEngineChange() {
    this._log.trace("_onSearchEngineChange");

    // Finally trigger the environment change notification.
    let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
    this._updateSearchEngine();
    this._onEnvironmentChange("search-engine-changed", oldEnvironment);
  },

  /**
   * Refresh the Telemetry environment and trigger an environment change due to
   * a change in compositor process (normally this will mean we've fallen back
   * from out-of-process to in-process compositing).
   */
  _onCompositorProcessAborted() {
    this._log.trace("_onCompositorProcessAborted");

    // Trigger the environment change notification.
    let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
    this._updateGraphicsFeatures();
    this._onEnvironmentChange("gfx-features-changed", oldEnvironment);
  },

  /**
   * Update the graphics features object.
   */
  _updateGraphicsFeatures() {
    let gfxData = this._currentEnvironment.system.gfx;
    try {
      let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);
      gfxData.features = gfxInfo.getFeatures();
    } catch (e) {
      this._log.error("nsIGfxInfo.getFeatures() caught error", e);
    }
  },

  /**
   * Update the partner prefs.
   */
  _updatePartner() {
    this._currentEnvironment.partner = this._getPartner();
  },

  /**
   * Get the build data in object form.
   * @return Object containing the build data.
   */
  _getBuild() {
    let buildData = {
      applicationId: Services.appinfo.ID || null,
      applicationName: Services.appinfo.name || null,
      architecture: Services.sysinfo.get("arch"),
      buildId: Services.appinfo.appBuildID || null,
      version: Services.appinfo.version || null,
      vendor: Services.appinfo.vendor || null,
      displayVersion: AppConstants.MOZ_APP_VERSION_DISPLAY || null,
      platformVersion: Services.appinfo.platformVersion || null,
      xpcomAbi: Services.appinfo.XPCOMABI,
      updaterAvailable: AppConstants.MOZ_UPDATER,
    };

    Glean.xpcom.abi.set(Services.appinfo.XPCOMABI);
    Glean.updater.available.set(AppConstants.MOZ_UPDATER);

    return buildData;
  },

  /**
   * Determine if we're the default browser.
   * @returns null on error, true if we are the default browser, or false otherwise.
   */
  _isDefaultBrowser() {
    let isDefault = (service, ...args) => {
      try {
        return !!service.isDefaultBrowser(...args);
      } catch (ex) {
        this._log.error(
          "_isDefaultBrowser - Could not determine if default browser",
          ex
        );
        return null;
      }
    };

    if (!("@mozilla.org/browser/shell-service;1" in Cc)) {
      this._log.info(
        "_isDefaultBrowser - Could not obtain browser shell service"
      );
      return null;
    }

    try {
      let { ShellService } = ChromeUtils.importESModule(
        "resource:///modules/ShellService.sys.mjs"
      );
      // This uses the same set of flags used by the pref pane.
      return isDefault(ShellService, false, true);
    } catch (ex) {
      this._log.error("_isDefaultBrowser - Could not obtain shell service JSM");
    }

    try {
      let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
        Ci.nsIShellService
      );
      // This uses the same set of flags used by the pref pane.
      return isDefault(shellService, true);
    } catch (ex) {
      this._log.error("_isDefaultBrowser - Could not obtain shell service", ex);
      return null;
    }
  },

  _updateDefaultBrowser() {
    if (AppConstants.platform === "android") {
      return;
    }
    // Make sure to have a settings section.
    this._currentEnvironment.settings = this._currentEnvironment.settings || {};
    this._currentEnvironment.settings.isDefaultBrowser = this
      ._sessionWasRestored
      ? this._isDefaultBrowser()
      : null;
  },

  /**
   * Update the cached settings data.
   */
  _updateSettings() {
    let updateChannel = null;
    try {
      updateChannel = Utils.getUpdateChannel();
    } catch (e) {}

    this._currentEnvironment.settings = {
      blocklistEnabled: Services.prefs.getBoolPref(
        PREF_BLOCKLIST_ENABLED,
        true
      ),
      e10sEnabled: Services.appinfo.browserTabsRemoteAutostart,
      e10sMultiProcesses: Services.appinfo.maxWebProcessCount,
      fissionEnabled: Services.appinfo.fissionAutostart,
      locale: getBrowserLocale(),
      // We need to wait for browser-delayed-startup-finished to ensure that the locales
      // have settled, once that's happened we can get the intl data directly.
      intl: Policy._intlLoaded ? getIntlSettings() : {},
      update: {
        channel: updateChannel,
        enabled:
          AppConstants.MOZ_UPDATER && !lazy.UpdateServiceStub.updateDisabled,
      },
      userPrefs: this._getPrefData(),
      sandbox: this._getSandboxData(),
    };
    Glean.updateSettings.channel.set(updateChannel);
    Glean.updateSettings.enabled.set(
      this._currentEnvironment.settings.update.enabled
    );

    // Services.appinfo.launcherProcessState is not available in all build
    // configurations, in which case an exception may be thrown.
    try {
      this._currentEnvironment.settings.launcherProcessState =
        Services.appinfo.launcherProcessState;
    } catch (e) {}

    this._currentEnvironment.settings.addonCompatibilityCheckEnabled =
      AddonManager.checkCompatibility;

    if (AppConstants.MOZ_BUILD_APP == "browser") {
      this._updateAttribution();
    }
    this._updateDefaultBrowser();
    this._updateSearchEngine();
    this._loadAsyncUpdateSettingsFromCache();

    Glean.addonsManager.compatibilityCheckEnabled.set(
      this._currentEnvironment.settings.addonCompatibilityCheckEnabled
    );
    Glean.blocklist.enabled.set(
      this._currentEnvironment.settings.blocklistEnabled
    );
    Glean.browser.defaultAtLaunch.set(
      this._currentEnvironment.settings.isDefaultBrowser
    );
    Glean.launcherProcess.state.set(
      this._currentEnvironment.settings.launcherProcessState
    );
    Glean.e10s.enabled.set(this._currentEnvironment.settings.e10sEnabled);
    Glean.e10s.multiProcesses.set(
      this._currentEnvironment.settings.e10sMultiProcesses
    );
    Glean.fission.enabled.set(this._currentEnvironment.settings.fissionEnabled);
    let prefs = Object.entries(this._currentEnvironment.settings.userPrefs).map(
      ([k, v]) => {
        return { name: k, value: v.toString() };
      }
    );
    if (prefs.length) {
      Glean.preferences.userPrefs.set(prefs);
    }
  },

  _getSandboxData() {
    let effectiveContentProcessLevel = null;
    let contentWin32kLockdownState = null;
    try {
      let sandboxSettings = Cc[
        "@mozilla.org/sandbox/sandbox-settings;1"
      ].getService(Ci.mozISandboxSettings);
      effectiveContentProcessLevel =
        sandboxSettings.effectiveContentSandboxLevel;

      // The possible values for this are defined in the ContentWin32kLockdownState
      // enum in security/sandbox/common/SandboxSettings.h
      contentWin32kLockdownState = sandboxSettings.contentWin32kLockdownState;
    } catch (e) {}
    if (effectiveContentProcessLevel !== null) {
      Glean.sandbox.effectiveContentProcessLevel.set(
        effectiveContentProcessLevel
      );
    }
    if (contentWin32kLockdownState !== null) {
      Glean.sandbox.contentWin32kLockdownState.set(contentWin32kLockdownState);
    }
    return {
      effectiveContentProcessLevel,
      contentWin32kLockdownState,
    };
  },

  /**
   * Update the cached profile data.
   * @returns Promise<> resolved when the I/O is complete.
   */
  async _updateProfile() {
    let profileAccessor = await lazy.ProfileAge();

    let creationDate = await profileAccessor.created;
    let resetDate = await profileAccessor.reset;
    let firstUseDate = await profileAccessor.firstUse;
    let recoveredFromBackup = await profileAccessor.recoveredFromBackup;

    this._currentEnvironment.profile.creationDate =
      Utils.millisecondsToDays(creationDate);
    Glean.profiles.creationDate.set(
      this._currentEnvironment.profile.creationDate
    );
    if (resetDate) {
      this._currentEnvironment.profile.resetDate =
        Utils.millisecondsToDays(resetDate);
      Glean.profiles.resetDate.set(this._currentEnvironment.profile.resetDate);
    }
    if (firstUseDate) {
      this._currentEnvironment.profile.firstUseDate =
        Utils.millisecondsToDays(firstUseDate);
      Glean.profiles.firstUseDate.set(
        this._currentEnvironment.profile.firstUseDate
      );
    }
    if (recoveredFromBackup) {
      this._currentEnvironment.profile.recoveredFromBackup =
        Utils.millisecondsToDays(recoveredFromBackup);
      Glean.profiles.recoveredFromBackup.set(
        this._currentEnvironment.profile.recoveredFromBackup
      );
    }
  },

  /**
   * Load the attribution data object and updates the environment.
   * @returns Promise<> resolved when the I/O is complete.
   */
  async _loadAttributionAsync() {
    try {
      await lazy.AttributionCode.getAttrDataAsync();
    } catch (e) {
      // The AttributionCode.sys.mjs module might not be always available
      // (e.g. tests). Gracefully handle this.
      return;
    }
    this._updateAttribution();
  },

  /**
   * Update the environment with the cached attribution data.
   */
  _updateAttribution() {
    let data = null;
    try {
      data = lazy.AttributionCode.getCachedAttributionData();
    } catch (e) {
      // The AttributionCode.sys.mjs module might not be always available
      // (e.g. tests). Gracefully handle this.
    }

    if (!data || !Object.keys(data).length) {
      return;
    }

    let attributionData = {};
    for (let key in data) {
      attributionData[key] =
        // At least one of these may be boolean, and limitStringToLength
        // returns null for non-string inputs.
        typeof data[key] === "string"
          ? limitStringToLength(data[key], MAX_ATTRIBUTION_STRING_LENGTH)
          : data[key];
    }
    this._currentEnvironment.settings.attribution = attributionData;
    let extAttribution = {
      experiment: attributionData.experiment,
      variation: attributionData.variation,
      ua: attributionData.ua,
      dltoken: attributionData.dltoken,
      msstoresignedin: attributionData.msstoresignedin,
      dlsource: attributionData.dlsource,
    };
    Services.fog.updateAttribution(
      attributionData.source,
      attributionData.medium,
      attributionData.campaign,
      attributionData.term,
      attributionData.content
    );
    Glean.gleanAttribution.ext.set(extAttribution);
  },

  /**
   * Load the per-installation update settings, cache them, and add them to the
   * environment.
   */
  async _loadAsyncUpdateSettings() {
    if (AppConstants.MOZ_UPDATER) {
      this._updateAutoDownloadCache =
        await UpdateUtils.getAppUpdateAutoEnabled();
      this._updateBackgroundCache = await UpdateUtils.readUpdateConfigSetting(
        "app.update.background.enabled"
      );
    } else {
      this._updateAutoDownloadCache = false;
      this._updateBackgroundCache = false;
    }
    this._loadAsyncUpdateSettingsFromCache();
  },

  /**
   * Update the environment with the cached values for per-installation update
   * settings.
   */
  _loadAsyncUpdateSettingsFromCache() {
    if (this._updateAutoDownloadCache !== undefined) {
      this._currentEnvironment.settings.update.autoDownload =
        this._updateAutoDownloadCache;
      Glean.updateSettings.autoDownload.set(this._updateAutoDownloadCache);
    }
    if (this._updateBackgroundCache !== undefined) {
      this._currentEnvironment.settings.update.background =
        this._updateBackgroundCache;
      Glean.updateSettings.background.set(this._updateBackgroundCache);
    }
  },

  /**
   * Get i18n data about the system.
   * @return A promise of completion.
   */
  async _loadIntlData() {
    // Wait for the startup topic.
    await Policy._browserDelayedStartup();
    this._currentEnvironment.settings.intl = getIntlSettings();
    Policy._intlLoaded = true;
  },
  // This exists as a separate function for testing.
  async _getFxaSignedInUser() {
    return lazy.fxAccounts.getSignedInUser();
  },

  async _updateServicesInfo() {
    let syncEnabled = false;
    let accountEnabled = false;
    let weaveService =
      Cc["@mozilla.org/weave/service;1"].getService().wrappedJSObject;
    syncEnabled = weaveService && weaveService.enabled;
    if (syncEnabled) {
      // All sync users are account users, definitely.
      accountEnabled = true;
    } else {
      // Not all account users are sync users. See if they're signed into FxA.
      try {
        let user = await this._getFxaSignedInUser();
        if (user) {
          accountEnabled = true;
        }
      } catch (e) {
        // We don't know. This might be a transient issue which will clear
        // itself up later, but the information in telemetry is quite possibly stale
        // (this is called from a change listener), so clear it out to avoid
        // reporting data which might be wrong until we can figure it out.
        delete this._currentEnvironment.services;
        this._log.error("_updateServicesInfo() caught error", e);
        return;
      }
    }
    this._currentEnvironment.services = {
      accountEnabled,
      syncEnabled,
    };
    Glean.fxa.syncEnabled.set(syncEnabled);
    Glean.fxa.accountEnabled.set(accountEnabled);
  },

  /**
   * Get the partner data in object form.
   * @return Object containing the partner data.
   */
  _getPartner() {
    let defaults = Services.prefs.getDefaultBranch(null);
    let partnerData = {
      distributionId: defaults.getStringPref(PREF_DISTRIBUTION_ID, null),
      distributionVersion: defaults.getCharPref(
        PREF_DISTRIBUTION_VERSION,
        null
      ),
      partnerId: defaults.getCharPref(PREF_PARTNER_ID, null),
      distributor: defaults.getCharPref(PREF_DISTRIBUTOR, null),
      distributorChannel: defaults.getCharPref(PREF_DISTRIBUTOR_CHANNEL, null),
    };

    // Get the PREF_APP_PARTNER_BRANCH branch and append its children to partner data.
    let partnerBranch = Services.prefs.getDefaultBranch(
      PREF_APP_PARTNER_BRANCH
    );
    partnerData.partnerNames = partnerBranch.getChildList("");

    Services.fog.updateDistribution(partnerData.distributionId);
    Glean.gleanDistribution.ext.set({
      distributionVersion: partnerData.distributionVersion,
      partnerId: partnerData.partnerId,
      distributor: partnerData.distributor,
      distributorChannel: partnerData.distributorChannel,
      partnerNames: partnerData.partnerNames,
    });

    return partnerData;
  },

  _cpuData: null,
  /**
   * Get the CPU information.
   * @return Object containing the CPU information data.
   */
  _getCPUData() {
    if (this._cpuData) {
      return this._cpuData;
    }

    this._cpuData = {};

    const CPU_EXTENSIONS = [
      "hasMMX",
      "hasSSE",
      "hasSSE2",
      "hasSSE3",
      "hasSSSE3",
      "hasSSE4A",
      "hasSSE4_1",
      "hasSSE4_2",
      "hasAVX",
      "hasAVX2",
      "hasAES",
      "hasEDSP",
      "hasARMv6",
      "hasARMv7",
      "hasNEON",
      "hasUserCET",
    ];

    // Enumerate the available CPU extensions.
    let availableExts = [];
    for (let ext of CPU_EXTENSIONS) {
      if (getSysinfoProperty(ext, false)) {
        availableExts.push(ext);
      }
    }

    this._cpuData.extensions = availableExts;

    Glean.systemCpu.extensions.set(availableExts);

    return this._cpuData;
  },

  _processData: null,
  /**
   * Get the process information.
   * @return Object containing the process information data.
   */
  _getProcessData() {
    if (this._processData) {
      return this._processData;
    }
    return {};
  },

  _osData: null,
  /**
   * Get the OS information.
   * @return Object containing the OS data.
   */
  _getOSData() {
    if (this._osData) {
      return this._osData;
    }
    this._osData = {
      name: forceToStringOrNull(getSysinfoProperty("name", null)),
      version: forceToStringOrNull(getSysinfoProperty("version", null)),
      locale: forceToStringOrNull(getSystemLocale()),
    };
    Glean.systemOs.name.set(this._osData.name);
    Glean.systemOs.version.set(this._osData.version);
    Glean.systemOs.locale.set(this._osData.locale);

    if (AppConstants.platform == "android") {
      this._osData.kernelVersion = forceToStringOrNull(
        getSysinfoProperty("kernel_version", null)
      );
    } else if (AppConstants.platform == "linux") {
      this._osData.distro = forceToStringOrNull(
        getSysinfoProperty("distro", null)
      );
      this._osData.distroVersion = forceToStringOrNull(
        getSysinfoProperty("distroVersion", null)
      );
      Glean.systemOs.distro.set(this._osData.distro);
      Glean.systemOs.distroVersion.set(this._osData.distroVersion);
    } else if (AppConstants.platform === "win") {
      // The path to the "UBR" key, queried to get additional version details on Windows.
      const WINDOWS_UBR_KEY_PATH =
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

      let versionInfo = lazy.WindowsVersionInfo.get({ throwOnError: false });
      this._osData.servicePackMajor = versionInfo.servicePackMajor;
      this._osData.servicePackMinor = versionInfo.servicePackMinor;
      this._osData.windowsBuildNumber = versionInfo.buildNumber;
      Glean.systemOs.servicePackMajor.set(this._osData.servicePackMajor);
      Glean.systemOs.servicePackMinor.set(this._osData.servicePackMinor);
      Glean.systemOs.windowsBuildNumber.set(this._osData.windowsBuildNumber);
      // We only need the UBR if we're at or above Windows 10.
      if (
        typeof this._osData.version === "string" &&
        Services.vc.compare(this._osData.version, "10") >= 0
      ) {
        // Query the UBR key and only add it to the environment if it's available.
        // |readRegKey| doesn't throw, but rather returns 'undefined' on error.
        let ubr = lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
          WINDOWS_UBR_KEY_PATH,
          "UBR",
          Ci.nsIWindowsRegKey.WOW64_64
        );
        if (Number.isInteger(ubr)) {
          Glean.systemOs.windowsUbr.set(ubr);
        }
        this._osData.windowsUBR = ubr !== undefined ? ubr : null;
      }
    }

    return this._osData;
  },

  _hddData: null,
  /**
   * Get the HDD information.
   * @return Object containing the HDD data.
   */
  _getHDDData() {
    if (this._hddData) {
      return this._hddData;
    }
    let nullData = { model: null, revision: null, type: null };
    return { profile: nullData, binary: nullData, system: nullData };
  },

  /**
   * Get registered security product information.
   * @return Object containing the security product data
   */
  _getSecurityAppData() {
    const maxStringLength = 256;

    const keys = [
      ["registeredAntiVirus", "antivirus"],
      ["registeredAntiSpyware", "antispyware"],
      ["registeredFirewall", "firewall"],
    ];

    let result = {};

    for (let [inKey, outKey] of keys) {
      let prop = getSysinfoProperty(inKey, null);
      if (prop) {
        Glean.windowsSecurity[outKey].set(prop.split(";"));
        prop = limitStringToLength(prop, maxStringLength).split(";");
      }

      result[outKey] = prop;
    }

    return result;
  },

  /**
   * Get the GFX information.
   * @return Object containing the GFX data.
   */
  _getGFXData() {
    let gfxData = {
      D2DEnabled: getGfxField("D2DEnabled", null),
      DWriteEnabled: getGfxField("DWriteEnabled", null),
      ContentBackend: getGfxField("ContentBackend", null),
      Headless: getGfxField("isHeadless", null),
      TargetFrameRate: getGfxField("TargetFrameRate", null),
      textScaleFactor: getGfxField("textScaleFactor", null),

      adapters: [],
      monitors: [],
      features: {},
    };
    if (gfxData.D2DEnabled !== null) {
      Glean.gfx.d2dEnabled.set(gfxData.D2DEnabled);
    }
    if (gfxData.DWriteEnabled !== null) {
      Glean.gfx.dwriteEnabled.set(gfxData.DWriteEnabled);
    }
    if (gfxData.ContentBackend !== null) {
      Glean.gfx.contentBackend.set(gfxData.ContentBackend);
    }
    if (gfxData.Headless !== null) {
      Glean.gfx.headless.set(gfxData.Headless);
    }
    if (gfxData.TargetFrameRate !== null) {
      Glean.gfx.targetFrameRate.set(gfxData.TargetFrameRate);
    }
    if (gfxData.textScaleFactor !== null) {
      Glean.gfx.textScaleFactor.set(gfxData.textScaleFactor);
    }

    if (AppConstants.platform !== "android") {
      let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);
      try {
        gfxData.monitors = gfxInfo.getMonitors();
        // Special handling because floats need to become strings.
        let monitors = gfxInfo.getMonitors();
        for (let monitor of monitors) {
          if ("defaultCSSScaleFactor" in monitor) {
            monitor.defaultCSSScaleFactor =
              monitor.defaultCSSScaleFactor.toString();
          }
          if ("contentsScaleFactor" in monitor) {
            monitor.contentsScaleFactor =
              monitor.contentsScaleFactor.toString();
          }
        }
        Glean.gfx.monitors.set(monitors);
      } catch (e) {
        this._log.error("nsIGfxInfo.getMonitors() caught error", e);
      }
    }

    try {
      let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);
      gfxData.features = gfxInfo.getFeatures();
      for (const [name, value] of Object.entries(gfxData.features)) {
        Glean.gfxFeatures[name].set(value);
      }
    } catch (e) {
      this._log.error("nsIGfxInfo.getFeatures() caught error", e);
    }

    // GfxInfo does not yet expose a way to iterate through all the adapters.
    gfxData.adapters.push(getGfxAdapter(""));
    gfxData.adapters[0].GPUActive = true;

    // If we have a second adapter add it to the gfxData.adapters section.
    let hasGPU2 = getGfxField("adapterDeviceID2", null) !== null;
    if (!hasGPU2) {
      this._log.trace("_getGFXData - Only one display adapter detected.");
      Glean.gfx.adapters.set(gfxData.adapters);
      return gfxData;
    }

    this._log.trace("_getGFXData - Two display adapters detected.");

    gfxData.adapters.push(getGfxAdapter("2"));
    gfxData.adapters[1].GPUActive = getGfxField("isGPU2Active", null);

    Glean.gfx.adapters.set(gfxData.adapters);

    return gfxData;
  },

  /**
   * Get the system data in object form.
   * @return Object containing the system data.
   */
  _getSystem() {
    let memoryMB = getSysinfoProperty("memsize", null);
    if (memoryMB) {
      // Send RAM size in megabytes. Rounding because sysinfo doesn't
      // always provide RAM in multiples of 1024.
      memoryMB = Math.round(memoryMB / 1024 / 1024);
      Glean.system.memory.set(memoryMB);
    }

    let virtualMB = getSysinfoProperty("virtualmemsize", null);
    if (virtualMB) {
      // Send the total virtual memory size in megabytes. Rounding because
      // sysinfo doesn't always provide RAM in multiples of 1024.
      virtualMB = Math.round(virtualMB / 1024 / 1024);
      Glean.system.virtualMemory.set(virtualMB);
    }

    let data = {
      memoryMB,
      virtualMaxMB: virtualMB,
      cpu: this._getCPUData(),
      os: this._getOSData(),
      hdd: this._getHDDData(),
      gfx: this._getGFXData(),
      appleModelId: getSysinfoProperty("appleModelId", null),
      hasWinPackageId: getSysinfoProperty("hasWinPackageId", null),
    };
    Glean.system.appleModelId.set(data.appleModelId);
    if (data.hasWinPackageId !== null) {
      Glean.system.hasWinPackageId.set(data.hasWinPackageId);
    }

    if (AppConstants.platform === "win") {
      // This is only sent for Mozilla produced MSIX packages
      let winPackageFamilyName = getSysinfoProperty("winPackageFamilyName", "");
      if (
        winPackageFamilyName.startsWith("Mozilla.") ||
        winPackageFamilyName.startsWith("MozillaCorporation.")
      ) {
        data = { winPackageFamilyName, ...data };
        Glean.system.winPackageFamilyName.set(winPackageFamilyName);
      }
      data = { ...this._getProcessData(), ...data };
      Glean.system.isWow64.set(data.isWow64);
      Glean.system.isWowArm64.set(data.isWowARM64);
      data.sec = this._getSecurityAppData();
    }

    return data;
  },

  _onEnvironmentChange(what, oldEnvironment) {
    ChromeUtils.addProfilerMarker(
      "EnvironmentChange",
      { category: "Telemetry" },
      what
    );
    this._log.trace("_onEnvironmentChange for " + what);

    // We are already skipping change events in _checkChanges if there is a pending change task running.
    if (this._shutdown) {
      this._log.trace("_onEnvironmentChange - Already shut down.");
      return;
    }

    if (ObjectUtils.deepEqual(this._currentEnvironment, oldEnvironment)) {
      this._log.trace("_onEnvironmentChange - Environment didn't change");
      return;
    }

    for (let [name, listener] of this._changeListeners) {
      try {
        this._log.debug("_onEnvironmentChange - calling " + name);
        listener(what, oldEnvironment);
      } catch (e) {
        this._log.error(
          "_onEnvironmentChange - listener " + name + " caught error",
          e
        );
      }
    }
  },

  reset() {
    this._shutdown = false;
  },
};
