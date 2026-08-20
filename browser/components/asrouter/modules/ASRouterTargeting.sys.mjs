/* This Source Code Form is subject to the terms of the Mozilla PublicddonMa
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
const FXA_ENABLED_PREF = "identity.fxaccounts.enabled";
const TOPIC_SELECTION_MODAL_LAST_DISPLAYED_PREF =
  "browser.newtabpage.activity-stream.discoverystream.topicSelection.onboarding.lastDisplayed";
const NOTIFICATION_INTERVAL_AFTER_TOPIC_MODAL_MS = 60000; // Assuming avoid notification up to 1 minute after newtab Topic Notification Modal

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// XPCOMUtils, AppConstants, NewTabUtils and ShellService, and
// overrides importESModule to be a no-op (which can't be done
// for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { NewTabUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/NewTabUtils.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { ShellService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/shell/ShellService.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { ClientID } = ChromeUtils.importESModule(
  "resource://gre/modules/ClientID.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);

// eslint-disable-next-line mozilla/use-static-import
const { FirefoxRelay, RELAY_PROFILE_CACHE_INTERVAL } =
  ChromeUtils.importESModule("resource://gre/modules/FirefoxRelay.sys.mjs");

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTabResourceMapping:
    "resource:///modules/AboutNewTabResourceMapping.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AppProvidedConfigEngine:
    "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs",
  ASRouterPreferences:
    "resource:///modules/asrouter/ASRouterPreferences.sys.mjs",
  AttributionCode:
    "moz-src:///browser/components/attribution/AttributionCode.sys.mjs",
  BackupService: "resource:///modules/backup/BackupService.sys.mjs",
  BrowserInitState: "resource:///modules/BrowserGlue.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ClientEnvironment: "resource://normandy/lib/ClientEnvironment.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  FeatureCalloutBroker:
    "resource:///modules/asrouter/FeatureCalloutBroker.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  SessionStartup:
    "moz-src:///browser/components/sessionstore/SessionStartup.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  SmartTabGroupingManager:
    "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
  TargetingContext: "resource://messaging-system/targeting/Targeting.sys.mjs",
  TabNotes: "moz-src:///browser/components/tabnotes/TabNotes.sys.mjs",
  TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
  TelemetrySession: "resource://gre/modules/TelemetrySession.sys.mjs",
  LaunchOnLogin: "resource://gre/modules/LaunchOnLogin.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "cfrFeaturesUserPref",
  "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "cfrAddonsUserPref",
  "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasAccessedFxAPanel",
  "identity.fxaccounts.toolbar.accessed",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "clientsDevicesDesktop",
  "services.sync.clients.devices.desktop",
  0
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "clientsDevicesMobile",
  "services.sync.clients.devices.mobile",
  0
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "syncNumClients",
  "services.sync.numClients",
  0
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "devtoolsSelfXSSCount",
  "devtools.selfxss.count",
  0
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isFxAEnabled",
  FXA_ENABLED_PREF,
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isXPIInstallEnabled",
  "xpinstall.enabled",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasMigratedBookmarks",
  "browser.migrate.interactions.bookmarks",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasMigratedCSVPasswords",
  "browser.migrate.interactions.csvpasswords",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasMigratedHistory",
  "browser.migrate.interactions.history",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasMigratedPasswords",
  "browser.migrate.interactions.passwords",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "useEmbeddedMigrationWizard",
  "browser.migrate.content-modal.about-welcome-behavior",
  "default",
  null,
  behaviorString => {
    return behaviorString === "embedded";
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "totalSearches",
  "browser.search.totalSearches",
  0
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "newTabTopicModalLastSeen",
  TOPIC_SELECTION_MODAL_LAST_DISPLAYED_PREF,
  null,
  lastSeenString => {
    return Number.isInteger(parseInt(lastSeenString, 10))
      ? parseInt(lastSeenString, 10)
      : 0;
  }
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "profilesCreated",
  "browser.profiles.created",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "didHandleCampaignAction",
  "trailhead.firstrun.didHandleCampaignAction",
  false
);

XPCOMUtils.defineLazyServiceGetters(lazy, {
  AUS: [
    "@mozilla.org/updates/update-service;1",
    Ci.nsIApplicationUpdateService,
  ],
  BrowserHandler: ["@mozilla.org/browser/clh;1", Ci.nsIBrowserHandler],
  ExternalProtocolService: [
    "@mozilla.org/uriloader/external-protocol-service;1",
    Ci.nsIExternalProtocolService,
  ],
  ScreenManager: ["@mozilla.org/gfx/screenmanager;1", Ci.nsIScreenManager],
  TrackingDBService: [
    "@mozilla.org/tracking-db-service;1",
    Ci.nsITrackingDBService,
  ],
  UpdateCheckSvc: [
    "@mozilla.org/updates/update-checker;1",
    Ci.nsIUpdateChecker,
  ],
});

const FXA_USERNAME_PREF = "services.sync.username";

const { activityStreamProvider: asProvider } = NewTabUtils;

const FRECENT_SITES_UPDATE_INTERVAL = 6 * 60 * 60 * 1000; // Six hours
const FRECENT_SITES_IGNORE_BLOCKED = false;
const FRECENT_SITES_NUM_ITEMS = 25;
// 2 visits, 30 days ago.
const FRECENT_SITES_MIN_FRECENCY = PlacesUtils.history.pageFrecencyThreshold(
  30,
  2,
  false
);

const CACHE_EXPIRATION = 5 * 60 * 1000;
const jexlEvaluationCache = new Map();

function _extractMailDomainFromURI(uriTemplate) {
  if (!uriTemplate) {
    return null;
  }
  try {
    return Services.io.newURI(uriTemplate).host;
  } catch (e) {
    // Ignore parse errors
  }
  return null;
}

/**
 * CachedTargetingGetter
 *
 * @param property {string} Name of the method
 * @param options {any=} Options passed to the method
 * @param updateInterval {number?} Update interval for query. Defaults to FRECENT_SITES_UPDATE_INTERVAL
 */
export function CachedTargetingGetter(
  property,
  options = null,
  updateInterval = FRECENT_SITES_UPDATE_INTERVAL,
  getter = asProvider
) {
  return {
    _lastUpdated: 0,
    _value: null,
    // For testing
    expire() {
      this._lastUpdated = 0;
      this._value = null;
    },
    async get() {
      const now = Date.now();
      if (now - this._lastUpdated >= updateInterval) {
        this._value = await getter[property](options);
        this._lastUpdated = now;
      }
      return this._value;
    },
  };
}

function CacheUnhandledCampaignAction() {
  return {
    _lastUpdated: 0,
    _value: null,
    expire() {
      this._lastUpdated = 0;
      this._value = null;
    },
    get() {
      const now = Date.now();
      // Don't get cached value until the action has been handled to ensure
      // proper screen targeting in about:welcome
      if (
        now - this._lastUpdated >= FRECENT_SITES_UPDATE_INTERVAL ||
        !lazy.didHandleCampaignAction
      ) {
        this._value = null;
        if (!lazy.didHandleCampaignAction) {
          const attributionData =
            lazy.AttributionCode.getCachedAttributionData();
          const ALLOWED_CAMPAIGN_ACTIONS = [
            "PIN_AND_DEFAULT",
            "PIN_FIREFOX_TO_TASKBAR",
            "SET_DEFAULT_BROWSER",
          ];
          const campaign = attributionData?.campaign?.toUpperCase();
          if (campaign && ALLOWED_CAMPAIGN_ACTIONS.includes(campaign)) {
            this._value = campaign;
          }
        }
        this._lastUpdated = now;
      }
      return this._value;
    },
  };
}

function CheckBrowserNeedsUpdate(
  updateInterval = FRECENT_SITES_UPDATE_INTERVAL
) {
  const checker = {
    _lastUpdated: 0,
    _value: null,
    // For testing. Avoid update check network call.
    setUp(value) {
      this._lastUpdated = Date.now();
      this._value = value;
    },
    expire() {
      this._lastUpdated = 0;
      this._value = null;
    },
    async get() {
      const now = Date.now();
      if (
        !AppConstants.MOZ_UPDATER ||
        now - this._lastUpdated < updateInterval
      ) {
        return this._value;
      }
      if (!lazy.AUS.canCheckForUpdates) {
        return false;
      }
      this._lastUpdated = now;
      let check = lazy.UpdateCheckSvc.checkForUpdates(
        lazy.UpdateCheckSvc.FOREGROUND_CHECK
      );
      let result = await check.result;
      if (!result.succeeded) {
        lazy.ASRouterPreferences.console.error(
          "CheckBrowserNeedsUpdate failed :>> ",
          result.request
        );
        return false;
      }
      checker._value = !!result.updates.length;
      return checker._value;
    },
  };

  return checker;
}

export const QueryCache = {
  expireAll() {
    Object.keys(this.queries).forEach(query => {
      this.queries[query].expire();
    });
    Object.keys(this.getters).forEach(key => {
      this.getters[key].expire();
    });
  },
  queries: {
    TopFrecentSites: new CachedTargetingGetter("getTopFrecentSites", {
      ignoreBlocked: FRECENT_SITES_IGNORE_BLOCKED,
      numItems: FRECENT_SITES_NUM_ITEMS,
      topsiteFrecency: FRECENT_SITES_MIN_FRECENCY,
      onePerDomain: true,
      includeFavicon: false,
    }),
    TotalBookmarksCount: new CachedTargetingGetter("getTotalBookmarksCount"),
    CheckBrowserNeedsUpdate: new CheckBrowserNeedsUpdate(),
    RecentBookmarks: new CachedTargetingGetter("getRecentBookmarks"),
    UserMonthlyActivity: new CachedTargetingGetter("getUserMonthlyActivity"),
    UnhandledCampaignAction: new CacheUnhandledCampaignAction(),
  },
  getters: {
    doesAppNeedPin: new CachedTargetingGetter(
      "doesAppNeedPin",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    doesAppNeedPrivatePin: new CachedTargetingGetter(
      "doesAppNeedPin",
      true,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    doesAppNeedStartMenuPin: new CachedTargetingGetter(
      "doesAppNeedStartMenuPin",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    isDefaultBrowser: new CachedTargetingGetter(
      "isDefaultBrowser",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    isOneClickSetDefaultEnabled: new CachedTargetingGetter(
      "isOneClickSetDefaultEnabled",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    currentThemes: new CachedTargetingGetter(
      "getAddonsByTypes",
      ["theme"],
      FRECENT_SITES_UPDATE_INTERVAL,
      lazy.AddonManager // eslint-disable-line mozilla/valid-lazy
    ),
    isDefaultHTMLHandler: new CachedTargetingGetter(
      "isDefaultHandlerFor",
      [".html"],
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    isDefaultPDFHandler: new CachedTargetingGetter(
      "isDefaultHandlerFor",
      [".pdf"],
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    isDefaultMailtoHandler: new CachedTargetingGetter(
      "isDefaultHandlerFor",
      ["mailto"],
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    defaultPDFHandler: new CachedTargetingGetter(
      "getDefaultPDFHandler",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ShellService
    ),
    mailtoHandlerHost: new CachedTargetingGetter(
      "getMailtoHandlerHost",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      {
        getMailtoHandlerHost() {
          if (AppConstants.platform !== "win") {
            return null;
          }
          try {
            const handlerInfo =
              lazy.ExternalProtocolService.getProtocolHandlerInfo("mailto");
            if (
              handlerInfo.alwaysAskBeforeHandling ||
              handlerInfo.preferredAction !== Ci.nsIHandlerInfo.useHelperApp ||
              !(
                handlerInfo.preferredApplicationHandler instanceof
                Ci.nsIWebHandlerApp
              )
            ) {
              return null;
            }
            return _extractMailDomainFromURI(
              handlerInfo.preferredApplicationHandler.uriTemplate
            );
          } catch (e) {
            // No configured handler, or a non-web (local app) handler.
            return null;
          }
        },
      }
    ),
    profileGroupId: new CachedTargetingGetter(
      "getCachedProfileGroupID",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      ClientID
    ),
    profileGroupProfileCount: new CachedTargetingGetter(
      "getProfileGroupProfileCount",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      {
        getProfileGroupProfileCount() {
          if (
            !Services.prefs.getBoolPref("browser.profiles.enabled", false) ||
            !Services.prefs.getBoolPref("browser.profiles.created", false)
          ) {
            return 0;
          }

          return lazy.SelectableProfileService.getProfileCount();
        },
      }
    ),
    backupsInfo: new CachedTargetingGetter(
      "findBackupsInWellKnownLocations",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      {
        async findBackupsInWellKnownLocations() {
          let bs;
          try {
            bs = lazy.BackupService.get();
          } catch {
            bs = lazy.BackupService.init();
          }
          return bs.findBackupsInWellKnownLocations({
            source: "onboarding",
          });
        },
      }
    ),
    relayProfileInfo: new CachedTargetingGetter(
      "getRelayProfileInfo",
      null,
      RELAY_PROFILE_CACHE_INTERVAL,
      {
        async getRelayProfileInfo() {
          return FirefoxRelay.getRelayProfileInfo();
        },
      }
    ),
    crashData: new CachedTargetingGetter(
      "getCrashData",
      null,
      FRECENT_SITES_UPDATE_INTERVAL,
      {
        async getCrashData() {
          if (!Services.crashmanager) {
            return [];
          }
          const crashes = await Services.crashmanager.getCrashes();
          return crashes.map(crash => ({
            id: crash.id,
            date: crash.crashDate,
          }));
        },
      }
    ),
  },
};

/**
 * sortMessagesByWeightedRank
 *
 * Each message has an associated weight, which is guaranteed to be strictly
 * positive. Sort the messages so that higher weighted messages are more likely
 * to come first.
 *
 * Specifically, sort them so that the probability of message x_1 with weight
 * w_1 appearing before message x_2 with weight w_2 is (w_1 / (w_1 + w_2)).
 *
 * This is equivalent to requiring that x_1 appearing before x_2 is (w_1 / w_2)
 * "times" as likely as x_2 appearing before x_1.
 *
 * See Bug 1484996, Comment 2 for a justification of the method.
 *
 * @param {Array} messages - A non-empty array of messages to sort, all with
 *                           strictly positive weights
 * @returns the sorted array
 */
function sortMessagesByWeightedRank(messages) {
  return messages
    .map(message => ({
      message,
      rank: Math.pow(Math.random(), 1 / message.weight),
    }))
    .sort((a, b) => b.rank - a.rank)
    .map(({ message }) => message);
}

/**
 * getSortedMessages - Given an array of Messages, applies sorting and filtering rules
 *                     in expected order.
 *
 * @param {Array<Message>} messages
 * @param {{}} options
 * @param {boolean} options.ordered - Should .order be used instead of random weighted sorting?
 * @returns {Array<Message>}
 */
export function getSortedMessages(messages, options = {}) {
  let { ordered } = { ordered: false, ...options };
  let result = messages;

  if (!ordered) {
    result = sortMessagesByWeightedRank(result);
  }

  result.sort((a, b) => {
    // Next, sort by priority
    if (a.priority > b.priority || (!isNaN(a.priority) && isNaN(b.priority))) {
      return -1;
    }
    if (a.priority < b.priority || (isNaN(a.priority) && !isNaN(b.priority))) {
      return 1;
    }

    // Sort messages with targeting expressions higher than those with none
    if (a.targeting && !b.targeting) {
      return -1;
    }
    if (!a.targeting && b.targeting) {
      return 1;
    }

    // Next, sort by order *ascending* if ordered = true
    if (ordered) {
      if (a.order > b.order || (!isNaN(a.order) && isNaN(b.order))) {
        return 1;
      }
      if (a.order < b.order || (isNaN(a.order) && !isNaN(b.order))) {
        return -1;
      }
    }

    return 0;
  });

  return result;
}

/**
 * parseAboutPageURL - Parse a URL string retrieved from about:home and about:new, returns
 *                    its type (web extenstion or custom url) and the parsed url(s)
 *
 * @param {string} url - A URL string for home page or newtab page
 * @returns  {{isWebExt: boolean, isCustomUrl: boolean, urls: {url: string, host: string}[]}}
 */
function parseAboutPageURL(url) {
  let ret = {
    isWebExt: false,
    isCustomUrl: false,
    urls: [],
  };
  if (lazy.ExtensionUtils.isExtensionUrl(url)) {
    ret.isWebExt = true;
    ret.urls.push({ url, host: "" });
  } else {
    // The home page URL could be either a single URL or a list of "|" separated URLs.
    // Note that it should work with "about:home" and "about:blank", in which case the
    // "host" is set as an empty string.
    for (const _url of url.split("|")) {
      if (!["about:home", "about:newtab", "about:blank"].includes(_url)) {
        ret.isCustomUrl = true;
      }
      try {
        const parsedURL = new URL(_url);
        const host = parsedURL.hostname.replace(/^www\./i, "");
        ret.urls.push({ url: _url, host });
      } catch (e) {}
    }
    // If URL parsing failed, just return the given url with an empty host
    if (!ret.urls.length) {
      ret.urls.push({ url, host: "" });
    }
  }

  return ret;
}

/**
 * Get the number of records in autofill storage, e.g. credit cards/addresses.
 *
 * @param  {object} [data]
 * @param  {string} [data.collectionName]
 *         The name used to specify which collection to retrieve records.
 * @param  {string} [data.searchString]
 *         The typed string for filtering out the matched records.
 * @param  {string} [data.info]
 *         The input autocomplete property's information.
 * @returns {Promise<number>} The number of matched records.
 * @see FormAutofillParent._getRecords
 */
async function getAutofillRecords(data) {
  let actor;
  try {
    const win = Services.wm.getMostRecentBrowserWindow();
    actor =
      win.gBrowser.selectedBrowser.browsingContext.currentWindowGlobal.getActor(
        "FormAutofill"
      );
  } catch (error) {
    // If the actor is not available, we can't get the records. We could import
    // the records directly from FormAutofillStorage to avoid the messiness of
    // JSActors, but that would import a lot of code for a targeting attribute.
    return 0;
  }
  let records = await actor?.getRecords(data);
  return records?.length ?? 0;
}

// Attribution data can be encoded multiple times so we need this function to
// get a cleartext value.
function decodeAttributionValue(value) {
  if (!value) {
    return null;
  }

  let decodedValue = value;

  while (decodedValue.includes("%")) {
    try {
      const result = decodeURIComponent(decodedValue);
      if (result === decodedValue) {
        break;
      }
      decodedValue = result;
    } catch (e) {
      break;
    }
  }

  return decodedValue;
}

async function getPinStatus() {
  return await ShellService.doesAppNeedPin();
}

const TargetingGetters = {
  get locale() {
    return Services.locale.appLocaleAsBCP47;
  },
  get localeLanguageCode() {
    return (
      Services.locale.appLocaleAsBCP47 &&
      Services.locale.appLocaleAsBCP47.substr(0, 2)
    );
  },
  get browserSettings() {
    const { settings } = lazy.TelemetryEnvironment.currentEnvironment;
    return {
      update: settings.update,
    };
  },
  get attributionData() {
    // Attribution is determined at startup - so we can use the cached attribution at this point
    return lazy.AttributionCode.getCachedAttributionData();
  },
  get currentDate() {
    return new Date();
  },
  get canCreateSelectableProfiles() {
    if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
      return false;
    }
    return lazy.SelectableProfileService?.isEnabled ?? false;
  },
  get hasSelectableProfiles() {
    return lazy.profilesCreated;
  },
  get profileAgeCreated() {
    return lazy.ProfileAge().then(times => times.created);
  },
  get profileAgeReset() {
    return lazy.ProfileAge().then(times => times.reset);
  },
  get usesFirefoxSync() {
    return Services.prefs.prefHasUserValue(FXA_USERNAME_PREF);
  },
  get isFxAEnabled() {
    return lazy.isFxAEnabled;
  },
  get isFxASignedIn() {
    return new Promise(resolve => {
      if (!lazy.isFxAEnabled) {
        resolve(false);
      }
      if (Services.prefs.getStringPref(FXA_USERNAME_PREF, "")) {
        resolve(true);
      }
      lazy.fxAccounts
        .getSignedInUser()
        .then(data => resolve(!!data))
        .catch(() => resolve(false));
    });
  },
  get sync() {
    return {
      desktopDevices: lazy.clientsDevicesDesktop,
      mobileDevices: lazy.clientsDevicesMobile,
      totalDevices: lazy.syncNumClients,
    };
  },
  get xpinstallEnabled() {
    // This is needed for all add-on recommendations, to know if we allow xpi installs in the first place
    return lazy.isXPIInstallEnabled;
  },
  get addonsInfo() {
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    if (bts?.isBackgroundTaskMode) {
      return { addons: {}, isFullData: true };
    }

    return lazy.AddonManager.getActiveAddons(["extension", "service"]).then(
      ({ addons, fullData }) => {
        const info = {};
        let hasInstalledAddons = false;
        for (const addon of addons) {
          info[addon.id] = {
            version: addon.version,
            type: addon.type,
            isSystem: addon.isSystem,
            isWebExtension: addon.isWebExtension,
            hidden: addon.hidden,
            isBuiltin: addon.isBuiltin,
          };
          if (fullData) {
            Object.assign(info[addon.id], {
              name: addon.name,
              userDisabled: addon.userDisabled,
              installDate: addon.installDate,
            });
          }
          // special-powers and mochikit are addons installed in tests that
          // are not "isSystem" or "isBuiltin"
          const testAddons = [
            "special-powers@mozilla.org",
            "mochikit@mozilla.org",
          ];
          if (
            !addon.isSystem &&
            !addon.isBuiltin &&
            !testAddons.includes(addon.id)
          ) {
            hasInstalledAddons = true;
          }
        }
        return { addons: info, isFullData: fullData, hasInstalledAddons };
      }
    );
  },
  get searchEngines() {
    const NONE = { installed: [], current: "" };
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    if (bts?.isBackgroundTaskMode) {
      return Promise.resolve(NONE);
    }
    return new Promise(resolve => {
      // Note: calling getAppProvidedEngines, calls SearchService.init which
      // ensures this code is only executed after Search has been initialized.
      lazy.SearchService.getAppProvidedEngines()
        .then(engines => {
          let { defaultEngine } = lazy.SearchService;
          let hasEnteredSearchMode = Object.fromEntries(
            engines.map(e => [e.id, e.hasBeenUsed])
          );

          resolve({
            // Skip reporting the id for third party engines.
            current:
              defaultEngine instanceof lazy.AppProvidedConfigEngine
                ? defaultEngine.id
                : null,
            // We don't need to filter the id here, as getAppProvidedEngines has
            // already done that for us.
            installed: engines.map(engine => engine.id),
            hasEnteredSearchMode,
          });
        })
        .catch(() => resolve(NONE));
    });
  },
  get isDefaultBrowser() {
    return QueryCache.getters.isDefaultBrowser.get().catch(() => null);
  },
  get isDefaultBrowserUncached() {
    return ShellService.isDefaultBrowser();
  },
  get isOneClickSetDefaultEnabled() {
    return QueryCache.getters.isOneClickSetDefaultEnabled
      .get()
      .catch(() => null);
  },
  get devToolsOpenedCount() {
    return lazy.devtoolsSelfXSSCount;
  },
  get topFrecentSites() {
    return QueryCache.queries.TopFrecentSites.get().then(sites =>
      sites.map(site => ({
        url: site.url,
        host: new URL(site.url).hostname,
        frecency: site.frecency,
        lastVisitDate: site.lastVisitDate,
      }))
    );
  },
  get recentBookmarks() {
    return QueryCache.queries.RecentBookmarks.get();
  },
  get pinnedSites() {
    return NewTabUtils.pinnedLinks.links.map(site =>
      site
        ? {
            url: site.url,
            host: new URL(site.url).hostname,
            searchTopSite: site.searchTopSite,
          }
        : {}
    );
  },
  get providerCohorts() {
    return lazy.ASRouterPreferences.providers.reduce((prev, current) => {
      prev[current.id] = current.cohort || "";
      return prev;
    }, {});
  },
  get totalBookmarksCount() {
    return QueryCache.queries.TotalBookmarksCount.get();
  },
  get firefoxVersion() {
    return parseInt(AppConstants.MOZ_APP_VERSION.match(/\d+/), 10);
  },
  get region() {
    return lazy.Region.home || "";
  },
  get needsUpdate() {
    return QueryCache.queries.CheckBrowserNeedsUpdate.get();
  },
  get savedTabGroups() {
    return lazy.SessionStore.getSavedTabGroups().length;
  },
  get currentTabGroups() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    // If there's no window, there can't be any current tab groups.
    if (!win) {
      return 0;
    }
    let totalTabGroups = win.gBrowser.getAllTabGroups().length;
    return totalTabGroups;
  },
  get tabsOpenInTopWindow() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    if (!win) {
      return 0;
    }
    return win.gBrowser.tabs.length;
  },
  get installedWebAppsCount() {
    return lazy.TaskbarTabs.countTaskbarTabs();
  },
  get currentTabInstalledAsWebApp() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    if (!win) {
      // There is no active tab, so it isn't a web app.
      return false;
    }

    // Note: this is a promise!
    return (
      lazy.TaskbarTabs.findTaskbarTab(
        win.gBrowser.selectedBrowser.currentURI,
        win.gBrowser.selectedTab.userContextId
      )
        .then(aTaskbarTab => aTaskbarTab !== null)
        // If this is not an nsIURL (e.g. if it's about:blank), then this will
        // throw; in that case there isn't a matching web app.
        .catch(() => false)
    );
  },
  get hasPinnedTabs() {
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      if (win.closed || !win.gBrowser) {
        continue;
      }
      if (win.gBrowser.visibleTabs.filter(t => t.pinned).length) {
        return true;
      }
    }

    return false;
  },
  get hasActiveAIWindow() {
    return !!lazy.AIWindow?.hasActiveAIWindows?.();
  },
  get isSmartTabGroupingAllowed() {
    return !!lazy.SmartTabGroupingManager?.isAllowed;
  },
  get hasAccessedFxAPanel() {
    return lazy.hasAccessedFxAPanel;
  },
  get userPrefs() {
    return {
      cfrFeatures: lazy.cfrFeaturesUserPref,
      cfrAddons: lazy.cfrAddonsUserPref,
    };
  },
  get totalBlockedCount() {
    return lazy.TrackingDBService.sumAllEvents();
  },
  get blockedCountByType() {
    const idToTextMap = new Map([
      [Ci.nsITrackingDBService.TRACKERS_ID, "trackerCount"],
      [Ci.nsITrackingDBService.TRACKING_COOKIES_ID, "cookieCount"],
      [Ci.nsITrackingDBService.CRYPTOMINERS_ID, "cryptominerCount"],
      [Ci.nsITrackingDBService.FINGERPRINTERS_ID, "fingerprinterCount"],
      [Ci.nsITrackingDBService.SOCIAL_ID, "socialCount"],
    ]);

    const dateTo = new Date();
    const dateFrom = new Date(dateTo.getTime() - 42 * 24 * 60 * 60 * 1000);
    return lazy.TrackingDBService.getEventsByDateRange(dateFrom, dateTo).then(
      eventsByDate => {
        let totalEvents = {};
        for (let blockedType of idToTextMap.values()) {
          totalEvents[blockedType] = 0;
        }

        return eventsByDate.reduce((acc, day) => {
          const type = day.getResultByName("type");
          const count = day.getResultByName("count");
          acc[idToTextMap.get(type)] = acc[idToTextMap.get(type)] + count;
          return acc;
        }, totalEvents);
      }
    );
  },
  get attachedFxAOAuthClients() {
    return this.usesFirefoxSync
      ? new Promise(resolve =>
          lazy.fxAccounts
            .listAttachedOAuthClients()
            .then(clients => resolve(clients))
            .catch(() => resolve([]))
        )
      : [];
  },
  get relayProfileInfo() {
    return QueryCache.getters.relayProfileInfo.get();
  },
  get relayEmailMasksCount() {
    return QueryCache.getters.relayProfileInfo
      .get()
      .then(info => info?.masksCount || 0);
  },
  get isRelayFreeTier() {
    return QueryCache.getters.relayProfileInfo
      .get()
      .then(info => info !== null && !info.has_premium);
  },
  get platformName() {
    return AppConstants.platform;
  },
  get userId() {
    return lazy.ClientEnvironment.userId;
  },
  get profileRestartCount() {
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    if (bts?.isBackgroundTaskMode) {
      return 0;
    }
    // Counter starts at 1 when a profile is created, substract 1 so the value
    // returned matches expectations
    return (
      lazy.TelemetrySession.getMetadata("targeting").profileSubsessionCounter -
      1
    );
  },
  get homePageSettings() {
    const url = lazy.HomePage.get();
    const { isWebExt, isCustomUrl, urls } = parseAboutPageURL(url);

    return {
      isWebExt,
      isCustomUrl,
      urls,
      isDefault: lazy.HomePage.isDefault,
      isLocked: lazy.HomePage.locked,
    };
  },
  get newtabSettings() {
    const url = lazy.AboutNewTab.newTabURL;
    const { isWebExt, isCustomUrl, urls } = parseAboutPageURL(url);

    return {
      isWebExt,
      isCustomUrl,
      isDefault: lazy.AboutNewTab.activityStreamEnabled,
      url: urls[0].url,
      host: urls[0].host,
    };
  },
  get activeNotifications() {
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    if (bts?.isBackgroundTaskMode) {
      // This might need to hook into the alert service to enumerate relevant
      // persistent native notifications.
      return false;
    }

    let window = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });

    // Technically this doesn't mean we have active notifications,
    // but because we use !activeNotifications to check for conflicts, this should return true
    if (!window) {
      return true;
    }

    let duration = Date.now() - lazy.newTabTopicModalLastSeen;
    let isDialogShowing =
      window.gBrowser?.selectedBrowser.hasAttribute("tabDialogShowing") ||
      window.gDialogBox?.isOpen;
    let isFeatureCalloutShowing = lazy.FeatureCalloutBroker.isCalloutShowing;

    if (
      isDialogShowing ||
      isFeatureCalloutShowing ||
      window.gURLBar?.view.isOpen ||
      window.gNotificationBox?.currentNotification ||
      window.gBrowser.readNotificationBox()?.currentNotification ||
      // Avoid showing messages if the newtab Topic selection modal was shown in
      // the past 1 minute
      duration <= NOTIFICATION_INTERVAL_AFTER_TOPIC_MODAL_MS
    ) {
      return true;
    }
    // use observer service to query Newtab
    const subjectWithBrowser = {
      browser: window.gBrowser,
      activeNewtabMessage: false,
    };
    Services.obs.notifyObservers(subjectWithBrowser, "newtab-message-query");
    if (subjectWithBrowser.activeNewtabMessage) {
      return true;
    }
    return false;
  },

  get isMajorUpgrade() {
    return lazy.BrowserHandler.majorUpgrade;
  },

  get hasActiveEnterprisePolicies() {
    return Services.policies.status === Services.policies.ACTIVE;
  },

  get userMonthlyActivity() {
    return QueryCache.queries.UserMonthlyActivity.get();
  },

  get doesAppNeedPin() {
    return (async () => {
      return (
        (await QueryCache.getters.doesAppNeedPin.get()) ||
        (await QueryCache.getters.doesAppNeedStartMenuPin.get())
      );
    })();
  },

  get doesAppNeedPinUncached() {
    return getPinStatus();
  },

  get doesAppNeedPrivatePin() {
    return QueryCache.getters.doesAppNeedPrivatePin.get();
  },

  get launchOnLoginEnabled() {
    if (!lazy.LaunchOnLogin.isSupported()) {
      return false;
    }
    return lazy.LaunchOnLogin.isEnabled();
  },

  // Whether launch on login could be enabled, i.e. it isn't overridden by
  // Windows Settings or enterprise policy. Used to avoid offering launch on
  // login to users for whom enabling it would silently no-op.
  get launchOnLoginAllowedByPolicy() {
    if (!lazy.LaunchOnLogin.isSupported()) {
      return false;
    }
    return lazy.LaunchOnLogin.isAllowed();
  },

  get isMSIX() {
    if (AppConstants.platform !== "win") {
      return false;
    }
    // While we can write registry keys using external programs, we have no
    // way of cleanup on uninstall. If we are on an MSIX build
    // launch on login should never be enabled.
    // Default to false so that the feature isn't unnecessarily
    // disabled.
    // See Bug 1888263.
    return Services.sysinfo.getProperty("hasWinPackageId", false);
  },

  get packageFamilyName() {
    if (AppConstants.platform !== "win") {
      // PackageFamilyNames are an MSIX feature, so they won't be available on non-Windows platforms.
      return null;
    }

    let packageFamilyName = Services.sysinfo.getProperty(
      "winPackageFamilyName"
    );
    if (packageFamilyName === "") {
      return null;
    }

    return packageFamilyName;
  },

  /**
   * Is this invocation running in background task mode?
   *
   * @return {boolean} `true` if running in background task mode.
   */
  get isBackgroundTaskMode() {
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    return !!bts?.isBackgroundTaskMode;
  },

  /**
   * A non-empty task name if this invocation is running in background
   * task mode, or `null` if this invocation is not running in
   * background task mode.
   *
   * @return {string|null} background task name or `null`.
   */
  get backgroundTaskName() {
    let bts = Cc["@mozilla.org/backgroundtasks;1"]?.getService(
      Ci.nsIBackgroundTasks
    );
    return bts?.backgroundTaskName();
  },

  get userPrefersReducedMotion() {
    return Services.appinfo.prefersReducedMotion;
  },

  /**
   * The distribution id, if any.
   *
   * @return {string}
   */
  get distributionId() {
    return Services.prefs
      .getDefaultBranch(null)
      .getCharPref("distribution.id", "");
  },

  /**
   * Where the Firefox View button is shown, if at all.
   *
   * @return {string} container of the button if it is shown in the toolbar/overflow menu
   * @return {string} `null` if the button has been removed
   */
  get fxViewButtonAreaType() {
    let button = lazy.CustomizableUI.getWidget("firefox-view-button");
    return button.areaType;
  },

  get alltabsButtonAreaType() {
    let button = lazy.CustomizableUI.getWidget("alltabs-button");
    return button.areaType;
  },

  isDefaultHandler: {
    get html() {
      return QueryCache.getters.isDefaultHTMLHandler.get();
    },
    get pdf() {
      return QueryCache.getters.isDefaultPDFHandler.get();
    },
    get mailto() {
      return QueryCache.getters.isDefaultMailtoHandler.get();
    },
  },

  get defaultPDFHandler() {
    return QueryCache.getters.defaultPDFHandler.get();
  },

  get mailtoHandlerHost() {
    return QueryCache.getters.mailtoHandlerHost.get();
  },

  get creditCardsSaved() {
    return getAutofillRecords({ collectionName: "creditCards" });
  },

  get addressesSaved() {
    return getAutofillRecords({ collectionName: "addresses" });
  },

  /**
   * Has the user ever used the Migration Wizard to migrate bookmarks?
   *
   * @return {boolean} `true` if bookmark migration has occurred.
   */
  get hasMigratedBookmarks() {
    return lazy.hasMigratedBookmarks;
  },

  /**
   * Has the user ever used the Migration Wizard to migrate passwords from
   * a CSV file?
   *
   * @return {boolean} `true` if CSV passwords have been imported via the
   *   migration wizard.
   */
  get hasMigratedCSVPasswords() {
    return lazy.hasMigratedCSVPasswords;
  },

  /**
   * Has the user ever used the Migration Wizard to migrate history?
   *
   * @return {boolean} `true` if history migration has occurred.
   */
  get hasMigratedHistory() {
    return lazy.hasMigratedHistory;
  },

  /**
   * Has the user ever used the Migration Wizard to migrate passwords?
   *
   * @return {boolean} `true` if password migration has occurred.
   */
  get hasMigratedPasswords() {
    return lazy.hasMigratedPasswords;
  },

  /**
   * Returns true if the user is configured to use the embedded migration
   * wizard in about:welcome by having
   * "browser.migrate.content-modal.about-welcome-behavior" be equal to
   * "embedded".
   *
   * @return {boolean} `true` if the embedded migration wizard is enabled.
   */
  get useEmbeddedMigrationWizard() {
    return lazy.useEmbeddedMigrationWizard;
  },

  /**
   * Returns the version number of the New Tab built-in addon being used
   * by the build.
   *
   * @return {string}
   */
  get newtabAddonVersion() {
    return lazy.AboutNewTabResourceMapping.addonVersion;
  },

  /**
   * Whether the user installed Firefox via the RTAMO flow.
   *
   * @return {boolean} `true` when RTAMO has been used to download Firefox,
   * `false` otherwise.
   */
  get isRTAMO() {
    const { attributionData } = this;

    return (
      attributionData?.source === "addons.mozilla.org" &&
      !!decodeAttributionValue(attributionData?.content)?.startsWith("rta:")
    );
  },

  /**
   * Whether the user installed via the device migration flow.
   *
   * @return {boolean} `true` when the link to download the browser was part
   * of guidance for device migration. `false` otherwise.
   */
  get isDeviceMigration() {
    const { attributionData } = this;

    return attributionData?.campaign === "migration";
  },

  /**
   * Whether the user installed via the Smart Window marketing site.
   *
   * @return {boolean} `true` when the link to download the browser was part
   * of the Smart Window campaign. `false` otherwise.
   */
  get isSmartWindowOnboarding() {
    const { attributionData } = this;

    return attributionData?.campaign === "smart_window";
  },

  /**
   * Whether the user opted into a special message action represented by an
   * installer attribution campaign and this choice still needs to be honored.
   *
   * @return {string} A special message action to be executed on first-run. For
   * example, `"SET_DEFAULT_BROWSER"` when the user selected to set as default
   * via the install marketing page and set default has not yet been
   * automatically triggered, 'null' otherwise.
   */
  get unhandledCampaignAction() {
    return QueryCache.queries.UnhandledCampaignAction.get();
  },
  /**
   * The values of the height and width available to the browser to display
   * web content. The available height and width are each calculated taking
   * into account the presence of menu bars, docks, and other similar OS elements
   *
   * @returns {object} resolution The resolution object containing width and height
   * @returns {number} resolution.width The available width of the primary monitor
   * @returns {number} resolution.height The available height of the primary monitor
   */
  get primaryResolution() {
    const { primaryScreen } = lazy.ScreenManager;
    const { defaultCSSScaleFactor } = primaryScreen;
    let availDeviceLeft = {};
    let availDeviceTop = {};
    let availDeviceWidth = {};
    let availDeviceHeight = {};
    primaryScreen.GetAvailRect(
      availDeviceLeft,
      availDeviceTop,
      availDeviceWidth,
      availDeviceHeight
    );
    return {
      width: Math.floor(availDeviceWidth.value / defaultCSSScaleFactor),
      height: Math.floor(availDeviceHeight.value / defaultCSSScaleFactor),
    };
  },

  get archBits() {
    let bits = null;
    try {
      bits = Services.sysinfo.getProperty("archbits", null);
    } catch (_e) {
      // getProperty can throw if the memsize does not exist
    }
    if (bits) {
      bits = Number(bits);
    }
    return bits;
  },

  get systemArch() {
    try {
      return Services.sysinfo.get("arch");
    } catch (_e) {
      return null;
    }
  },

  get memoryMB() {
    let memory = null;
    try {
      memory = Services.sysinfo.getProperty("memsize", null);
    } catch (_e) {
      // getProperty can throw if the memsize does not exist
    }
    if (memory) {
      memory = Number(memory) / 1024 / 1024;
    }
    return memory;
  },

  get totalSearches() {
    return lazy.totalSearches;
  },

  get profileGroupId() {
    return QueryCache.getters.profileGroupId.get();
  },

  get currentProfileId() {
    if (!lazy.SelectableProfileService.currentProfile) {
      return "";
    }
    return lazy.SelectableProfileService.currentProfile.id.toString();
  },

  get profileGroupProfileCount() {
    return QueryCache.getters.profileGroupProfileCount.get();
  },

  get buildId() {
    return parseInt(AppConstants.MOZ_BUILDID, 10);
  },

  get backupsInfo() {
    // We're going to skip searching for backups on MacOS and as such not show the
    // restore screen in about:welcome - Bug 2033325
    if (AppConstants.platform === "macosx") {
      return Promise.resolve({
        found: false,
      });
    }

    return QueryCache.getters.backupsInfo.get().catch(() => null);
  },

  get backupArchiveEnabled() {
    let bs;
    try {
      bs = lazy.BackupService.get();
    } catch {
      bs = lazy.BackupService.init();
    }
    return bs.archiveEnabledStatus.enabled;
  },

  get backupRestoreEnabled() {
    let bs;
    try {
      bs = lazy.BackupService.get();
    } catch {
      bs = lazy.BackupService.init();
    }
    return bs.restoreEnabledStatus.enabled;
  },

  get isEncryptedBackup() {
    const isEncryptedBackup =
      Services.prefs.getStringPref(
        "messaging-system-action.backupChooser",
        null
      ) === "full";
    return isEncryptedBackup;
  },

  get isPrivateWindow() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    // If there's no window (like in backgroundTask mode), return false
    if (!win) {
      return false;
    }
    return lazy.PrivateBrowsingUtils.isWindowPrivate(win);
  },

  get isTaskbarTabWindow() {
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    if (!win) {
      return false;
    }
    return win.document.documentElement.hasAttribute("taskbartab");
  },

  get canRestoreLastSession() {
    return lazy.SessionStore.canRestoreLastSession;
  },

  // This is implemented as a targeting attribute because it is needed for
  // background task messages, which don't share preferences with the main
  // browser profile (aside from a short allowlist of synced prefs, but we don't
  // want to sync this pref and possibly affect behavior in the background
  // task).
  get autoRestoreSessionEnabled() {
    return Services.prefs.getIntPref("browser.startup.page") === 3;
  },

  /**
   * @returns {Promise<number>}
   *   The total number of tab notes the user has stored in their current profile.
   */
  get tabNotesCount() {
    return lazy.TabNotes.init().then(() => lazy.TabNotes.count());
  },

  // Number of weekdays in the past month the user was active
  get userWeekdaysActiveInLastMonth() {
    return QueryCache.queries.UserMonthlyActivity.get().then(activity => {
      return activity.filter(entry => {
        const [year, month, date] = String(entry[1]).split("-").map(Number);
        //JavaScript's Date constructor takes a 0-indexed month — January is 0, December is 11. So if the date string is "2024-01-08", splitting gives you month = 1, and you need to pass 0 to get January.
        const day = new Date(year, month - 1, date).getDay(); // 0 = Sun, 6 = Sat, local time avoids UTC shift
        return day !== 0 && day !== 6;
      }).length;
    });
  },

  // Number of days in the past month with 100+ site visits
  get userActiveDaysWithHundredPlusSites() {
    return QueryCache.queries.UserMonthlyActivity.get().then(activity => {
      return activity.filter(entry => entry[0] >= 100).length;
    });
  },

  /**
   * Whether Nimbus has loaded remote experiments at least once.
   *
   * @return {boolean}
   */
  get experimentsLoaded() {
    try {
      // If Nimbus experiments are disabled, we can consider them loaded
      if (!lazy.ExperimentAPI.enabled) {
        return true;
      }
      // Check if the loader has updated recipes at least once
      const hasUpdated = lazy.ExperimentAPI._rsLoader?._hasUpdatedOnce ?? false;
      return hasUpdated;
    } catch (e) {
      lazy.ASRouterPreferences.console.error(
        "nimbusExperimentsLoaded check failed",
        e
      );
      return false;
    }
  },

  /**
   * The total number of crashes the user has experienced, as recorded by the
   * crash manager at crash time (independent of report submission).
   *
   * @returns {Promise<number>}
   */
  get crashCount() {
    return QueryCache.getters.crashData.get().then(crashes => crashes.length);
  },

  /**
   * The number of days since the most recent crash, as recorded by the crash
   * manager at crash time (independent of report submission). If there are no
   * recorded crashes, returns `null`.
   *
   * @returns {Promise<number|null>}
   */
  get daysSinceLastCrash() {
    return QueryCache.getters.crashData.get().then(crashes => {
      if (!crashes.length) {
        return null;
      }
      const mostRecent = Math.max(...crashes.map(c => c.date));
      return Math.floor((Date.now() - mostRecent) / (24 * 60 * 60 * 1000));
    });
  },

  /**
   * The number of crashes the user has experienced in the last 24 hours, as
   * recorded by the crash manager at crash time (independent of report
   * submission).
   *
   * @returns {Promise<number>}
   */
  get crashCountInLastDay() {
    return QueryCache.getters.crashData.get().then(crashes => {
      const cutoff = Date.now() - 24 * 60 * 60 * 1000;
      return crashes.filter(c => c.date >= cutoff).length;
    });
  },

  /**
   * The number of crashes the user has experienced in the last 7 days, as
   * recorded by the crash manager at crash time (independent of report
   * submission).
   *
   * @returns {Promise<number>}
   */
  get crashCountInLastWeek() {
    return QueryCache.getters.crashData.get().then(crashes => {
      const cutoff = Date.now() - 7 * 24 * 60 * 60 * 1000;
      return crashes.filter(c => c.date >= cutoff).length;
    });
  },

  /**
   * Whether this Firefox launch was initiated by the OS on login.
   *
   * @returns {boolean}
   */
  get isLaunchOnLogin() {
    return lazy.BrowserInitState.isLaunchOnLogin;
  },

  /**
   * Whether the previous browser session ended in a crash.
   *
   * @returns {boolean}
   */
  get previousSessionCrashed() {
    return lazy.SessionStartup.previousSessionCrashed;
  },
};

function addAIWindowTargeting(targeting) {
  if (!targeting || targeting === "true") {
    // Default behavior: Classic-only if no targeting is specified
    return `!isAIWindow`;
  }

  if (/\bisAIWindow\b/.test(targeting)) {
    return targeting;
  }

  return `((${targeting}) && !isAIWindow)`;
}

/**
 * Sentinel rejection thrown by per-property promises in
 * `ASRouterTargeting.getEnvironmentSnapshot` when `quit-application`
 * fires while the property is still being awaited. Lets the caller
 * tell a shutdown-induced drop from a real per-property failure.
 */
class QuitDuringSnapshotError extends Error {}

export const ASRouterTargeting = {
  Environment: TargetingGetters,

  /**
   * Snapshot the current targeting environment.
   *
   * Asynchronous getters are handled.  Getters that throw or reject
   * are ignored.
   *
   * Leftward (earlier) targets supercede rightward (later) targets, just like
   * `TargetingContext.combineContexts`.
   *
   * @param {object} options - object containing:
   * @param {Array<object>|null} options.targets -
   *        targeting environments to snapshot; (default: `[ASRouterTargeting.Environment]`)
   * @return {object} snapshot of target with `environment` object and `version` integer.
   */
  async getEnvironmentSnapshot({
    targets = [ASRouterTargeting.Environment],
  } = {}) {
    // Each per-property promise races its resolution against this shared
    // `quit-application` observer. Without the race, a property whose
    // resolver waits on something that does not unblock until after
    // shutdown (notably `UpdateService.waitForOtherInstances`, which
    // can hang for hours when a second Firefox instance holds the update
    // lock) keeps the `targeting.snapshot` JSON store's
    // `IOUtils.profileBeforeChange` blocker pending until AsyncShutdown
    // crashes the process. See bug 1830551.
    let quitObserver;
    const quitApplication = new Promise((_unused, reject) => {
      quitObserver = {
        QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
        observe() {
          reject(
            new QuitDuringSnapshotError(
              "shutting down, so not querying targeting environment"
            )
          );
        },
      };
      Services.obs.addObserver(quitObserver, "quit-application");
    });
    // Absorb the rejection if no per-property race ever subscribes (eg.
    // short-lived snapshots that finish before `quit-application` fires).
    quitApplication.catch(() => {});

    async function resolve(object) {
      if (typeof object === "object" && object !== null) {
        if (Array.isArray(object)) {
          return Promise.all(object.map(async item => resolve(await item)));
        }

        if (object instanceof Date) {
          return object;
        }

        // One promise for each named property. Label promises with property name.
        const promises = Object.keys(object).map(async key => {
          // Fast path: if shutdown has already started by the time this
          // property is evaluated, bail before invoking the getter. The
          // race below handles the case where shutdown starts mid-await.
          if (Services.startup.shuttingDown) {
            throw new QuitDuringSnapshotError(
              "shutting down, so not querying targeting environment"
            );
          }

          const property = await Promise.race([object[key], quitApplication]);
          const value = await resolve(property);

          return [key, value];
        });

        const resolved = {};
        for (const result of await Promise.allSettled(promises)) {
          // Drop rejected properties. The sentinel `QuitDuringSnapshotError`
          // distinguishes a quit-induced drop from a property's own
          // resolver rejecting; both are silently ignored today, but the
          // distinction is preserved so future diagnostics can branch on it.
          if (result.status === "fulfilled") {
            const [key, value] = result.value;
            resolved[key] = value;
          }
        }

        return resolved;
      }

      return object;
    }

    try {
      // We would like to use `TargetingContext.combineContexts`, but `Proxy`
      // instances complicate iterating with `Object.keys`.  Instead, merge by
      // hand after resolving.
      const environment = {};
      for (let target of targets.toReversed()) {
        Object.assign(environment, await resolve(target));
      }

      // Should we need to migrate in the future.
      const snapshot = { environment, version: 1 };

      return snapshot;
    } finally {
      Services.obs.removeObserver(quitObserver, "quit-application");
    }
  },

  /**
   * Return the list of trigger objects for a message, normalizing the singular
   * `trigger` and plural `triggers` forms. When both are present, `triggers`
   * takes precedence.
   *
   * @param {object} message An AS router message
   * @returns {Array<object>} The message's triggers, or an empty array.
   */
  getMessageTriggers(message) {
    if (Array.isArray(message?.triggers)) {
      return message.triggers;
    }
    return message?.trigger ? [message.trigger] : [];
  },

  isTriggerMatch(trigger = {}, candidateMessageTrigger = {}) {
    if (trigger.id !== candidateMessageTrigger.id) {
      return false;
    } else if (
      !candidateMessageTrigger.params &&
      !candidateMessageTrigger.patterns
    ) {
      return true;
    }

    if (!trigger.param) {
      return false;
    }

    return (
      (candidateMessageTrigger.params &&
        trigger.param.host &&
        candidateMessageTrigger.params.includes(trigger.param.host)) ||
      (candidateMessageTrigger.params &&
        trigger.param.type &&
        candidateMessageTrigger.params.filter(t => t === trigger.param.type)
          .length) ||
      (candidateMessageTrigger.params &&
        trigger.param.type &&
        candidateMessageTrigger.params.filter(
          t => (t & trigger.param.type) === t
        ).length) ||
      (candidateMessageTrigger.patterns &&
        trigger.param.url &&
        new MatchPatternSet(candidateMessageTrigger.patterns).matches(
          trigger.param.url
        ))
    );
  },

  /**
   * getCachedEvaluation - Return a cached jexl evaluation if available
   *
   * @param {string} targeting JEXL expression to lookup
   * @returns {obj|null} Object with value result or null if not available
   */
  getCachedEvaluation(targeting) {
    if (jexlEvaluationCache.has(targeting)) {
      const { timestamp, value } = jexlEvaluationCache.get(targeting);
      if (Date.now() - timestamp <= CACHE_EXPIRATION) {
        return { value };
      }
      jexlEvaluationCache.delete(targeting);
    }

    return null;
  },

  /**
   * checkMessageTargeting - Checks is a message's targeting parameters are satisfied
   *
   * @param {*} message An AS router message
   * @param {obj} targetingContext a TargetingContext instance complete with eval environment
   * @param {func} onError A function to handle errors (takes two params; error, message)
   * @param {boolean} shouldCache Should the JEXL evaluations be cached and reused.
   * @returns
   */
  async checkMessageTargeting(message, targetingContext, onError, shouldCache) {
    lazy.ASRouterPreferences.console.debug(
      "in checkMessageTargeting, arguments = ",
      Array.from(arguments) // eslint-disable-line prefer-rest-params
    );

    let { targeting } = message;
    targeting = addAIWindowTargeting(targeting);

    let result;
    try {
      if (shouldCache) {
        result = this.getCachedEvaluation(targeting);
        if (result) {
          return result.value;
        }
      }
      // Used to report the source of the targeting error in the case of
      // undesired events
      targetingContext.setTelemetrySource(message.id);
      result = await targetingContext.evalWithDefault(targeting);
      if (shouldCache) {
        jexlEvaluationCache.set(targeting, {
          timestamp: Date.now(),
          value: result,
        });
      }
    } catch (error) {
      if (onError) {
        onError(error, message);
      }
      console.error(error);
      result = false;
    }
    return result;
  },

  _isMessageMatch(
    message,
    trigger,
    targetingContext,
    onError,
    shouldCache = false
  ) {
    const messageTriggers = this.getMessageTriggers(message);
    return (
      message &&
      // If a trigger with an id was passed, the message must have a matching
      // trigger. Otherwise, only untriggered messages are eligible.
      (trigger?.id
        ? messageTriggers.some(mt => this.isTriggerMatch(trigger, mt))
        : !messageTriggers.length) &&
      this.checkMessageTargeting(
        message,
        targetingContext,
        onError,
        shouldCache
      )
    );
  },

  /**
   * findMatchingMessage - Given an array of messages, returns one message
   *                       whos targeting expression evaluates to true
   *
   * @param {Array<Message>} messages An array of AS router messages
   * @param {trigger} string A trigger expression if a message for that trigger is desired
   * @param {obj|null} context A FilterExpression context. Defaults to TargetingGetters above.
   * @param {func} onError A function to handle errors (takes two params; error, message)
   * @param {func} ordered An optional param when true sort message by order specified in message
   * @param {boolean} shouldCache Should the JEXL evaluations be cached and reused.
   * @param {boolean} returnAll Should we return all matching messages, not just the first one found.
   * @returns {obj|Array<Message>} If returnAll is false, a single message. If returnAll is true, an array of messages.
   */
  async findMatchingMessage({
    messages,
    trigger = {},
    context = {},
    onError,
    ordered = false,
    shouldCache = false,
    returnAll = false,
  }) {
    const sortedMessages = getSortedMessages(messages, { ordered });
    lazy.ASRouterPreferences.console.debug(
      "in findMatchingMessage, sortedMessages = ",
      sortedMessages
    );
    const matching = returnAll ? [] : null;
    const targetingContext = new lazy.TargetingContext(
      lazy.TargetingContext.combineContexts(
        context,
        this.Environment,
        trigger.context || {}
      )
    );

    const isMatch = candidate =>
      this._isMessageMatch(
        candidate,
        trigger,
        targetingContext,
        onError,
        shouldCache
      );

    for (const candidate of sortedMessages) {
      if (await isMatch(candidate)) {
        // If not returnAll, we should return the first message we find that matches.
        if (!returnAll) {
          return candidate;
        }

        matching.push(candidate);
      }
    }
    return matching;
  },
};
