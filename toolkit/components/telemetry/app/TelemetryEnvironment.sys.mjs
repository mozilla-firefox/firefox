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
  TELEMETRY_ENVIRONMENT_ADDONS_CHANGED_TOPIC,
} from "resource://gre/modules/AddonManager.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AttributionCode:
    "moz-src:///browser/components/attribution/AttributionCode.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  WeaveService: "moz-src:///services/sync/Weave.sys.mjs",
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
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
    Ci.nsIApplicationUpdateServiceStub
  );
}

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
// Legacy Count: 63
// Glean Count: 63

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
   * @param {string} id The id of the active experiment.
   * @param {string} branch The experiment branch.
   * @param {object} [options] Optional object with options.
   * @param {string} [options.type=false] The specific experiment type.
   * @param {string} [options.enrollmentId=undefined] The id of the enrollment.
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
   * @param {string} id The id of the active experiment.
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

  /**
   * Intended for use in tests only.
   *
   * Returns the Promise for the most recent default-browser check.
   *
   */
  testIsDefaultBrowser() {
    return getGlobal()._isDefaultBrowserPromise;
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
  ["browser.urlbar.showSearchSuggestionsFirst", { what: RECORD_PREF_VALUE }],
  ["browser.urlbar.showSearchTerms.enabled", { what: RECORD_PREF_VALUE }],
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
  ["extensions.quarantinedDomains.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.strictCompatibility", { what: RECORD_PREF_VALUE }],
  ["extensions.update.enabled", { what: RECORD_PREF_VALUE }],
  ["extensions.update.url", { what: RECORD_PREF_VALUE }],
  ["extensions.update.background.url", { what: RECORD_PREF_VALUE }],
  ["general.config.filename", { what: RECORD_DEFAULTPREF_STATE }],
  ["general.smoothScroll", { what: RECORD_PREF_VALUE }],
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
  ["screenshots.browser.component.enabled", { what: RECORD_PREF_VALUE }],
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
const AUTO_UPDATE_PREF_CHANGE_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.auto"].observerTopic;
const BACKGROUND_UPDATE_PREF_CHANGE_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.background.enabled"]
    .observerTopic;
const SERVICES_INFO_CHANGE_TOPIC = "sync-ui-state:update";

/**
 * Get the current browser locale.
 *
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
 *
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
 *
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
 *
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

function recordIntlMetrics() {
  Glean.intl.requestedLocales.set(Services.locale.requestedLocales);
  Glean.intl.availableLocales.set(Services.locale.availableLocales);
  Glean.intl.appLocales.set(Services.locale.appLocalesAsBCP47);
  Glean.intl.systemLocales.set(getSystemLocales());
  Glean.intl.regionalPrefsLocales.set(getRegionalPrefsLocales());
  Glean.intl.acceptLanguages.set(
    Services.locale.acceptLanguages.split(/\s*,\s*/g)
  );
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
 * @param {string} aString The input string.
 * @param {Integer} aMaxLength The maximum length of the returned substring. If this is
 *        greater than the length of the input string, we return the whole input string.
 * @return {string} The substring or null if the input string is null.
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

  // Set when a default-browser check starts after session restore, allowing
  // tests to await its completion.
  this._isDefaultBrowserPromise = null;

  // A map of listeners that will be called on environment changes.
  this._changeListeners = new Map();

  // A map of watched preferences which trigger an Environment change when
  // modified. Every entry contains a recording policy (RECORD_PREF_*).
  this._watchedPrefs = DEFAULT_ENVIRONMENT_PREFS;

  this._currentEnvironment = {
    build: this._getBuild(), // required in environment.1.schema
    partner: this._getPartner(), // required in environment.1.schema
    system: this._getSystem(), // required in environment.1.schema
  };

  this._addObservers();

  // Build the remaining asynchronous parts of the environment. Don't register change listeners
  // until the initial environment has been built.

  let p = [this._updateSettings()];

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
}
EnvironmentCache.prototype = {
  /**
   * The current environment data. The returned data is cloned to avoid
   * unexpected sharing or mutation.
   *
   * @returns object
   */
  get currentEnvironment() {
    return Cu.cloneInto(this._currentEnvironment, {});
  },

  /**
   * Wait for the current enviroment to be fully initialized.
   *
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
    const allowed = [
      "count",
      "cores",
      "family",
      "model",
      "name",
      "stepping",
      "vendor",
    ];
    processData = Object.fromEntries(
      Object.entries(processData).filter(([k, _v]) => allowed.includes(k))
    );

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

      this._currentEnvironment.system.os = this._getOSData();

      // Windows only values stored in processData
      this._currentEnvironment.system.isWow64 = this._processData.isWow64;
      Glean.system.isWow64.set(this._processData.isWow64);
      Glean.system.isWowArm64.set(this._processData.isWowARM64);
    }

    if (!this._initTask) {
      this._onEnvironmentChange("system-info", oldEnv);
    }
  },

  /**
   * Register a listener for environment changes.
   *
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
   *
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
   *
   * @param aPreferences A map of preferences names and their recording policy.
   */
  async _watchPreferences(aPreferences) {
    this._stopWatchingPrefs();
    this._watchedPrefs = aPreferences;
    await this._updateSettings();
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
   *
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
    Services.obs.addObserver(this, TELEMETRY_ENVIRONMENT_ADDONS_CHANGED_TOPIC);
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
    Services.obs.removeObserver(
      this,
      TELEMETRY_ENVIRONMENT_ADDONS_CHANGED_TOPIC
    );
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
          lazy.SearchService.defaultEngine != aSubject.wrappedJSObject
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
        lazy.SearchService.init();
        // The default browser check could take some time, so just call it after
        // the session was restored.
        this._updateDefaultBrowser();
        break;
      case PREF_CHANGED_TOPIC: {
        let options = this._watchedPrefs.get(aData);
        if (options && !options.requiresRestart) {
          this._onPrefChanged(aData);
        }
        break;
      }
      case AUTO_UPDATE_PREF_CHANGE_TOPIC:
        Glean.updateSettings.autoDownload.set(aData == "true");
        break;
      case BACKGROUND_UPDATE_PREF_CHANGE_TOPIC:
        Glean.updateSettings.background.set(aData == "true");
        break;
      case SERVICES_INFO_CHANGE_TOPIC:
        this._updateServicesInfo();
        break;
      case TELEMETRY_ENVIRONMENT_ADDONS_CHANGED_TOPIC:
        this._onAddonsChanged();
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
      "_updateSearchEngine - isInitialized: " + lazy.SearchService.isInitialized
    );
    if (!lazy.SearchService.isInitialized) {
      return;
    }

    // Make sure we have a settings section as it's required by environment.1.schema.
    this._currentEnvironment.settings = this._currentEnvironment.settings || {};

    // Update the search engine entry in the current environment.
    const defaultEngineInfo = lazy.SearchService.getDefaultEngineInfo();
    this._currentEnvironment.settings.defaultSearchEngine =
      defaultEngineInfo.defaultSearchEngine;
    this._currentEnvironment.settings.defaultSearchEngineData = {
      ...defaultEngineInfo.defaultSearchEngineData,
    };
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
      for (const [name, value] of Object.entries(gfxData.features)) {
        Glean.gfxFeatures[name].set(value);
      }
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
   *
   * @return Object containing the build data.
   */
  _getBuild() {
    let buildData = {
      applicationId: "", // required string in environment.1.schema
      applicationName: "", // required string in environment.1.schema
      architecture: Services.sysinfo.get("arch"), // required in environment.1.schema
      buildId: Services.appinfo.appBuildID || null, // required in environment.1.schema
      version: "00.", // required string in environment.1.schema with pattern /^[0-9]{2,3}\\./`
      vendor: null, // required but can be null in environment.1.schema
      platformVersion: "00.", // required string in environment.1.schema with pattern /^[0-9]{2,3}\\./`
      xpcomAbi: Services.appinfo.XPCOMABI, // required in environment.1.schema
    };

    Glean.xpcom.abi.set(Services.appinfo.XPCOMABI);
    Glean.updater.available.set(AppConstants.MOZ_UPDATER);

    return buildData;
  },

  /**
   * Determine if we're the default browser.
   *
   * @async
   * @returns null on error, true if we are the default browser, or false otherwise.
   */
  async _isDefaultBrowser() {
    let isDefault = async (service, ...args) => {
      try {
        return await service.isDefaultBrowserAsync(...args);
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
        "moz-src:///browser/components/shell/ShellService.sys.mjs"
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

  async _updateDefaultBrowser() {
    if (AppConstants.platform === "android") {
      return;
    }

    if (!this._sessionWasRestored) {
      this._log.trace("_updateDefaultBrowser - ignoring early call");
      return;
    }

    // Make sure to have a settings section.
    this._currentEnvironment.settings = this._currentEnvironment.settings || {};

    this._isDefaultBrowserPromise = this._isDefaultBrowser();
    this._currentEnvironment.settings.isDefaultBrowser =
      await this._isDefaultBrowserPromise;

    Glean.browser.defaultAtLaunch.set(
      this._currentEnvironment.settings.isDefaultBrowser
    );
  },

  /**
   * Update the cached settings data.
   */
  async _updateSettings() {
    let updateChannel = null;
    try {
      updateChannel = Utils.getUpdateChannel();
    } catch (e) {}

    // We need to wait for browser-delayed-startup-finished to ensure that the locales
    // have settled, once that's happened we can get the intl data directly.
    if (Policy._intlLoaded) {
      recordIntlMetrics();
    }

    this._currentEnvironment.settings = {
      locale: getBrowserLocale(),
      update: {
        channel: updateChannel,
        enabled:
          AppConstants.MOZ_UPDATER && !lazy.UpdateServiceStub.updateDisabled,
      },
      userPrefs: this._getPrefData(),
    };
    this._recordSandboxMetrics();
    Glean.updateSettings.channel.set(updateChannel);
    Glean.updateSettings.enabled.set(
      this._currentEnvironment.settings.update.enabled
    );

    if (AppConstants.MOZ_BUILD_APP == "browser") {
      this._updateAttribution();
    }
    await this._updateDefaultBrowser();
    this._updateSearchEngine();
    this._loadAsyncUpdateSettingsFromCache();

    Glean.addonsManager.compatibilityCheckEnabled.set(
      AddonManager.checkCompatibility
    );
    Glean.blocklist.enabled.set(
      Services.prefs.getBoolPref(PREF_BLOCKLIST_ENABLED, true)
    );
    // Services.appinfo.launcherProcessState is not available in all build
    // configurations, in which case an exception may be thrown.
    try {
      Glean.launcherProcess.state.set(Services.appinfo.launcherProcessState);
    } catch (e) {}
    Glean.e10s.enabled.set(Services.appinfo.browserTabsRemoteAutostart);
    Glean.e10s.multiProcesses.set(Services.appinfo.maxWebProcessCount);
    Glean.fission.enabled.set(Services.appinfo.fissionAutostart);
    let prefs = Object.entries(this._currentEnvironment.settings.userPrefs).map(
      ([k, v]) => {
        return { name: k, value: v.toString() };
      }
    );
    if (prefs.length) {
      Glean.preferences.userPrefs.set(prefs);
    }
  },

  _recordSandboxMetrics() {
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
  },

  /**
   * Update the cached profile data.
   *
   * @returns Promise<> resolved when the I/O is complete.
   */
  async _updateProfile() {
    let profileAccessor = await lazy.ProfileAge();

    let creationDate = await profileAccessor.created;
    let resetDate = await profileAccessor.reset;
    let firstUseDate = await profileAccessor.firstUse;
    let recoveredFromBackup = await profileAccessor.recoveredFromBackup;
    let source = profileAccessor.source;

    this._currentEnvironment.profile.creationDate =
      Utils.millisecondsToDays(creationDate);
    Glean.profiles.creationDate.set(
      this._currentEnvironment.profile.creationDate
    );
    if (resetDate) {
      Glean.profiles.resetDate.set(Utils.millisecondsToDays(resetDate));
    }
    if (firstUseDate) {
      Glean.profiles.firstUseDate.set(Utils.millisecondsToDays(firstUseDate));
    }
    if (recoveredFromBackup) {
      this._currentEnvironment.profile.recoveredFromBackup =
        Utils.millisecondsToDays(recoveredFromBackup);
      Glean.profiles.recoveredFromBackup.set(
        this._currentEnvironment.profile.recoveredFromBackup
      );
    }
    if (source) {
      Glean.profiles.source.set(source);
    }
  },

  /**
   * Load the attribution data object and updates the environment.
   *
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
      msclkid: attributionData.msclkid,
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
      Glean.updateSettings.autoDownload.set(this._updateAutoDownloadCache);
    }
    if (this._updateBackgroundCache !== undefined) {
      Glean.updateSettings.background.set(this._updateBackgroundCache);
    }
  },

  /**
   * Get i18n data about the system.
   *
   * @return A promise of completion.
   */
  async _loadIntlData() {
    // Wait for the startup topic.
    await Policy._browserDelayedStartup();
    recordIntlMetrics();
    Policy._intlLoaded = true;
  },
  // This exists as a separate function for testing.
  async _getFxaSignedInUser() {
    return lazy.fxAccounts.getSignedInUser();
  },

  async _updateServicesInfo() {
    let syncEnabled = false;
    let accountEnabled = false;
    syncEnabled = lazy.WeaveService.enabled;
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
        this._log.error("_updateServicesInfo() caught error", e);
        return;
      }
    }
    Glean.fxa.syncEnabled.set(syncEnabled);
    Glean.fxa.accountEnabled.set(accountEnabled);
  },

  /**
   * Get the partner data in object form.
   *
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
   *
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

    Glean.systemCpu.extensions.set(availableExts);

    return this._cpuData;
  },

  _processData: null,
  /**
   * Get the process information.
   *
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
   *
   * @return Object containing the OS data.
   */
  _getOSData() {
    if (this._osData) {
      return this._osData;
    }
    this._osData = {
      name: forceToStringOrNull(getSysinfoProperty("name", null)),
      version: forceToStringOrNull(getSysinfoProperty("version", null)),
    };
    Glean.systemOs.name.set(this._osData.name);
    Glean.systemOs.version.set(this._osData.version);
    Glean.systemOs.locale.set(forceToStringOrNull(getSystemLocale()));

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
      const CURRENT_VERSION_PATH =
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

      // To make sure the telemetry data is as accurate as possible, use the
      // build number from the registry. This avoids a future compatibility shim
      // giving us the wrong value, and we aren't changing behaviour depending
      // on this so this doesn't circumvent any shim.
      // See: https://randomascii.wordpress.com/2022/01/06/determinism-bugs-part-two/
      // Its type is REG_SZ for some reason, so coerce it to a Number.
      let build = Number(
        lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
          CURRENT_VERSION_PATH,
          "CurrentBuild",
          Ci.nsIWindowsRegKey.WOW64_64
        )
      );
      this._osData.windowsBuildNumber = Number.isInteger(build) ? build : null;
      Glean.systemOs.windowsBuildNumber.set(this._osData.windowsBuildNumber);

      // The UBR value is already a REG_DWORD, so don't coerce it.
      let ubr = lazy.WindowsRegistry.readRegKey(
        Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
        CURRENT_VERSION_PATH,
        "UBR",
        Ci.nsIWindowsRegKey.WOW64_64
      );
      Glean.systemOs.windowsUbr.set(ubr);
    }

    return this._osData;
  },

  _hddData: null,
  /**
   * Get the HDD information.
   *
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
   * Record registered security product information.
   */
  _recordSecurityAppData() {
    const keys = [
      ["registeredAntiVirus", "antivirus"],
      ["registeredAntiSpyware", "antispyware"],
      ["registeredFirewall", "firewall"],
    ];

    for (let [inKey, outKey] of keys) {
      let prop = getSysinfoProperty(inKey, null);
      if (prop) {
        Glean.windowsSecurity[outKey].set(prop.split(";"));
      }
    }
  },

  /**
   * Get the GFX information.
   *
   * @return Object containing the GFX data.
   */
  _getGFXData() {
    let gfxData = {
      DWriteEnabled: getGfxField("DWriteEnabled", null),
      ContentBackend: getGfxField("ContentBackend", null),
      Headless: getGfxField("isHeadless", null),
      TargetFrameRate: getGfxField("TargetFrameRate", null),
      textScaleFactor: getGfxField("textScaleFactor", null),

      adapters: [],
      monitors: [],
      features: {},
    };
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
   *
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
      cpu: this._getCPUData(),
      os: this._getOSData(),
      gfx: this._getGFXData(),
    };
    Glean.system.appleModelId.set(getSysinfoProperty("appleModelId", null));
    const hasWinPackageId = getSysinfoProperty("hasWinPackageId", null);
    if (hasWinPackageId !== null) {
      Glean.system.hasWinPackageId.set(hasWinPackageId);
    }

    if (AppConstants.platform === "win") {
      // This is only sent for Mozilla produced MSIX packages
      let winPackageFamilyName = getSysinfoProperty("winPackageFamilyName", "");
      if (
        winPackageFamilyName.startsWith("Mozilla.") ||
        winPackageFamilyName.startsWith("MozillaCorporation.")
      ) {
        Glean.system.winPackageFamilyName.set(winPackageFamilyName);
      }
      const processData = this._getProcessData();
      data.isWow64 = processData.isWow64;
      Glean.system.isWow64.set(processData.isWow64);
      Glean.system.isWowArm64.set(processData.isWowARM64);
      this._recordSecurityAppData();
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

    // `environment.addons` is not part of the environment data anymore
    // and so ObjectUtils.deepEqual would be true and return earlier
    // but we still want changes to the active addons to keep triggering
    // `"environment-change"` main pings with the same frequence as before
    // `environment.addons` was removed from the environment.
    const forceNotifyListeners = what === "addons-changed";

    if (
      !forceNotifyListeners &&
      ObjectUtils.deepEqual(this._currentEnvironment, oldEnvironment)
    ) {
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

  /**
   * Trigger an environment change for a change in the set of active addons.
   *
   * NOTE: `environment.addons` no longer exists (dropped as part of Bug 2055613),
   * but we still need that addon installs/removals/updates keep triggering
   * `"environment-change"` main pings, at the same frequency as before
   * `environment.addons` data was removed from the environment.
   */
  _onAddonsChanged() {
    this._log.trace("_onAddonsChanged");
    let oldEnvironment = Cu.cloneInto(this._currentEnvironment, {});
    this._onEnvironmentChange("addons-changed", oldEnvironment);
  },

  reset() {
    this._shutdown = false;
  },
};
