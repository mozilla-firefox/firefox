/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { DeferredTask } from "resource://gre/modules/DeferredTask.sys.mjs";
import { EventEmitter } from "resource://gre/modules/EventEmitter.sys.mjs";
import { ProfilesDatastoreService } from "moz-src:///toolkit/profile/ProfilesDatastoreService.sys.mjs";
import { SelectableProfile } from "resource:///modules/profiles/SelectableProfile.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

// This is used to keep the icon controllers alive for as long as their windows are alive.
const TASKBAR_ICON_CONTROLLERS = new WeakMap();
const PROFILES_PREF_NAME = "browser.profiles.enabled";
const GROUPID_PREF_NAME = "toolkit.telemetry.cachedProfileGroupID";
const DEFAULT_THEME_ID = "default-theme@mozilla.org";
const PROFILES_CREATED_PREF_NAME = "browser.profiles.created";
const DAU_GROUPID_PREF_NAME = "datareporting.dau.cachedUsageProfileGroupID";

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  LightweightThemeManager:
    "resource://gre/modules/LightweightThemeManager.sys.mjs",
  CryptoUtils: "moz-src:///services/crypto/modules/utils.sys.mjs",
  DownloadPaths: "resource://gre/modules/DownloadPaths.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  MigrationUtils:
    "moz-src:///browser/components/migration/MigrationUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  TelemetryUtils: "resource://gre/modules/TelemetryUtils.sys.mjs",
  PushNotificationHelper:
    "resource://gre/modules/PushNotificationHelper.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "profilesLocalization", () => {
  return new Localization(["browser/profiles.ftl"]);
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PROFILES_ENABLED",
  PROFILES_PREF_NAME,
  false,
  () => SelectableProfileService.updateEnabledState()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "PROFILES_CREATED",
  PROFILES_CREATED_PREF_NAME,
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "NOVA_ENABLED",
  "browser.nova.enabled",
  true
);

const PROFILES_CRYPTO_SALT_LENGTH_BYTES = 16;

const COMMAND_LINE_UPDATE = "profiles-updated";
const COMMAND_LINE_ACTIVATE = "profiles-activate";

const gSupportsBadging = "nsIMacDockSupport" in Ci || "nsIWinTaskbar" in Ci;

// Bug 1922374: Move themes to remote settings
export const PROFILE_THEMES_MAP = new Map([
  [
    "firefox-compact-light@mozilla.org",
    {
      dataL10nId: "profiles-gray-theme",
      dataL10nTitle: "profiles-gray-theme-title",
      colors: {
        light: {
          chromeColor: "rgb(234, 234, 237)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "#F9F9FB",
        },
      },
      isDark: false,
      useInAutomation: true,
    },
  ],
  [
    "firefox-compact-dark@mozilla.org",
    {
      dataL10nId: "profiles-gray-theme",
      dataL10nTitle: "profiles-gray-theme-title",
      colors: {
        dark: {
          chromeColor: "rgb(28, 27, 34)",
          toolbarColor: "rgb(28, 27, 34)",
          contentColor: "rgb(43, 42, 51)",
        },
      },
      isDark: true,
      useInAutomation: true,
    },
  ],
  [
    "{cd6791f7-4b6d-47b4-8877-1d4c82c6699d}",
    {
      dataL10nId: "profiles-yellow-theme",
      dataL10nTitle: "profiles-yellow-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552782/profiles_yellow-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(255, 230, 153)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(255, 244, 208)",
        },
        dark: {
          chromeColor: "rgb(39, 16, 0)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(66, 27, 0)",
        },
      },
    },
  ],
  [
    "{7a301b7b-c3e2-40bf-a06b-6d517bbf138b}",
    {
      dataL10nId: "profiles-orange-theme",
      dataL10nTitle: "profiles-orange-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552788/profiles_orange-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(255, 205, 158)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(255, 237, 214)",
        },
        dark: {
          chromeColor: "rgb(39, 15, 0)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(72, 18, 0)",
        },
      },
    },
  ],
  [
    "{8de5f8c3-bfc2-443b-9913-7bbadbd1ba0d}",
    {
      dataL10nId: "profiles-red-theme",
      dataL10nTitle: "profiles-red-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552785/profiles_red-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(255, 195, 201)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(255, 232, 234)",
        },
        dark: {
          chromeColor: "rgb(41, 11, 15)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(76, 5, 22)",
        },
      },
    },
  ],
  [
    "{2b0fadbf-238d-43db-aa9d-e06c9a7e000b}",
    {
      dataL10nId: "profiles-pink-theme",
      dataL10nTitle: "profiles-pink-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552787/profiles_pink-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(255, 194, 219)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(255, 232, 244)",
        },
        dark: {
          chromeColor: "rgb(39, 11, 21)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(73, 6, 36)",
        },
      },
    },
  ],
  [
    "{1d73a1eb-128d-4e9e-83f8-c0c51f8c5fd3}",
    {
      dataL10nId: "profiles-purple-theme",
      dataL10nTitle: "profiles-purple-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552786/profiles_purple-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(247, 202, 255)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(255, 236, 255)",
        },
        dark: {
          chromeColor: "rgb(30, 14, 37)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(56, 17, 71)",
        },
      },
    },
  ],
  [
    "{aab1adac-5449-47fd-b836-c2f43dc28f3f}",
    {
      dataL10nId: "profiles-violet-theme",
      dataL10nTitle: "profiles-violet-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552784/profiles_violet-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(221, 207, 255)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(244, 240, 255)",
        },
        dark: {
          chromeColor: "rgb(22, 17, 43)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(40, 25, 83)",
        },
      },
    },
  ],
  [
    "{4223a94a-d3f9-40e9-95dd-99aca80ea04b}",
    {
      dataL10nId: "profiles-blue-theme",
      dataL10nTitle: "profiles-blue-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4551961/profiles_blue-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(171, 223, 255)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(226, 247, 255)",
        },
        dark: {
          chromeColor: "rgb(8, 21, 44)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(4, 35, 86)",
        },
      },
    },
  ],
  [
    "{7063abff-a690-4b87-a548-fc32d3ce5708}",
    {
      dataL10nId: "profiles-green-theme",
      dataL10nTitle: "profiles-green-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552789/profiles_green-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(181, 240, 181)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(225, 255, 225)",
        },
        dark: {
          chromeColor: "rgb(5, 28, 7)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(0, 50, 0)",
        },
      },
    },
  ],
  [
    "{0683b144-0d4a-4815-963e-55a8ec8d386b}",
    {
      dataL10nId: "profiles-cyan-theme",
      dataL10nTitle: "profiles-cyan-theme-title",
      downloadURL:
        "https://addons.mozilla.org/firefox/downloads/file/4552790/profiles_cyan-1.0.xpi",
      version: "1.0",
      colors: {
        light: {
          chromeColor: "rgb(166, 236, 244)",
          toolbarColor: "rgb(255, 255, 255)",
          contentColor: "rgb(207, 255, 255)",
        },
        dark: {
          chromeColor: "rgb(0, 31, 43)",
          toolbarColor: "rgb(22, 22, 22)",
          contentColor: "rgb(0, 50, 61)",
        },
      },
    },
  ],
  [
    "default-theme@mozilla.org",
    {
      dataL10nId: "profiles-system-theme",
      dataL10nTitle: "profiles-system-theme-title",
      colors: {},
    },
  ],
]);

async function loadImage(profile) {
  let uri;

  if (profile.hasCustomAvatar) {
    const file = await IOUtils.getFile(profile.getAvatarPath(48));
    uri = Services.io.newFileURI(file);
  } else {
    uri = Services.io.newURI(profile.getAvatarPath(48));
  }

  const channel = Services.io.newChannelFromURI(
    uri,
    null,
    Services.scriptSecurityManager.getSystemPrincipal(),
    null,
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    Ci.nsIContentPolicy.TYPE_IMAGE
  );

  return ChromeUtils.fetchDecodedImage(uri, channel);
}

/**
 * The service that manages selectable profiles
 */
class SelectableProfileServiceClass extends EventEmitter {
  #profileService = null;
  #connection = null;
  #initialized = false;
  #storeID = null;
  #currentProfile = null;
  #everyWindowCallbackId = "SelectableProfileService";
  #defaultAvatars = [
    "book",
    "briefcase",
    "flower",
    "heart",
    "shopping",
    "star",
  ];
  #initPromise = null;
  #observedPrefs = null;
  #badge = null;
  #windowActivated = null;
  #isEnabled = false;
  // This is a rough number of the current profiles. It is not always correct.
  #cachedProfileCount = null;
  #shutdownBlocker = null;

  // The preferences that must be permanently stored in the database and kept
  // consistent amongst profiles.
  static permanentSharedPrefs = [
    "app.shield.optoutstudies.enabled",
    "browser.crashReports.unsubmittedCheck.autoSubmit2",
    "browser.discovery.enabled",
    "browser.shell.checkDefaultBrowser",
    // Profiles on one install share a taskbar button, so whether the custom
    // icon feature works is a property of the install, not of the profile.
    "browser.shell.customIcon.enabled",
    "browser.shell.customIcon.id",
    "browser.shell.customIcon.perUserStartMenuShortcutCreated",
    "browser.backup.enabled_on.profiles",
    DAU_GROUPID_PREF_NAME,
    "datareporting.healthreport.uploadEnabled",
    "datareporting.policy.currentPolicyVersion",
    "datareporting.policy.dataSubmissionEnabled",
    "datareporting.policy.dataSubmissionPolicyAcceptedVersion",
    "datareporting.policy.dataSubmissionPolicyBypassNotification",
    "datareporting.policy.dataSubmissionPolicyNotifiedTime",
    "datareporting.policy.minimumPolicyVersion",
    "datareporting.policy.minimumPolicyVersion.channel-beta",
    "datareporting.usage.uploadEnabled",
    "termsofuse.acceptedDate",
    "termsofuse.firstAcceptedDate",
    "termsofuse.acceptedVersion",
    "termsofuse.bypassNotification",
    "termsofuse.currentVersion",
    "termsofuse.minimumVersion",
    GROUPID_PREF_NAME,
  ];

  // Preferences that were previously shared but should now be ignored.
  static ignoredSharedPrefs = [
    "browser.profiles.enabled",
    "browser.urlbar.quicksuggest.dataCollection.enabled",
    "toolkit.profiles.storeID",
  ];

  constructor() {
    super();

    this.themeObserver = this.themeObserver.bind(this);
    this.matchMediaObserver = this.matchMediaObserver.bind(this);
    this.lookAndFeelChanged = this.lookAndFeelChanged.bind(this);
    this.prefObserver = (subject, topic, prefName) =>
      this.flushSharedPrefToDatabase(prefName);

    this.#observedPrefs = new Set();

    this.#profileService = ProfilesDatastoreService.toolkitProfileService;
    this.#isEnabled = this.#getEnabledState();

    // We have to check the state again after the policy service may have disabled us.
    Services.obs.addObserver(
      () => this.updateEnabledState(),
      "profile-after-change"
    );

    Services.prefs.addObserver(PROFILES_CREATED_PREF_NAME, () =>
      Services.obs.notifyObservers(
        null,
        "sps-profile-created",
        lazy.PROFILES_CREATED ? "true" : "false"
      )
    );
  }

  // Migrate any early users who created profiles before the datastore service
  // was split out, and the PROFILES_CREATED pref replaced storeID as our check
  // for whether the profiles feature had been used.
  migrateToProfilesCreatedPref() {
    if (this.groupToolkitProfile?.storeID && !lazy.PROFILES_CREATED) {
      Services.prefs.setBoolPref(PROFILES_CREATED_PREF_NAME, true);
    }
  }

  hasCreatedSelectableProfiles() {
    return Services.prefs.getBoolPref(PROFILES_CREATED_PREF_NAME, false);
  }

  #getEnabledState() {
    if (!Services.policies.isAllowed("profileManagement")) {
      return false;
    }

    this.migrateToProfilesCreatedPref();

    // If a storeID has been assigned then profiles may have been created so force us on. Also
    // covers the case when the selector is shown at startup and we don't have preferences
    // available.
    if (this.groupToolkitProfile?.storeID) {
      return true;
    }

    return lazy.PROFILES_ENABLED && !!this.groupToolkitProfile;
  }

  updateEnabledState() {
    let newState = this.#getEnabledState();
    if (newState != this.#isEnabled) {
      this.#isEnabled = newState;
      this.emit("enableChanged", newState);
    }
  }

  get isEnabled() {
    return this.#isEnabled;
  }

  #setOverlayIcon({ win }) {
    if (!this.#badge || !("nsIWinTaskbar" in Ci)) {
      return;
    }

    let iconController = null;
    if (!TASKBAR_ICON_CONTROLLERS.has(win)) {
      iconController = Cc["@mozilla.org/windows-taskbar;1"]
        .getService(Ci.nsIWinTaskbar)
        .getOverlayIconController(win.docShell);
      TASKBAR_ICON_CONTROLLERS.set(win, iconController);
    } else {
      iconController = TASKBAR_ICON_CONTROLLERS.get(win);
    }

    if (this.#currentProfile.hasCustomAvatar) {
      iconController?.setOverlayIcon(
        this.#badge.image,
        this.#badge.description
      );
    } else {
      iconController?.setOverlayIcon(
        this.#badge.image,
        this.#badge.description,
        this.#badge.iconPaintContext
      );
    }
  }

  async #attemptFlushProfileService() {
    try {
      await this.#profileService.asyncFlushCurrentProfile();
    } catch (e) {
      try {
        await this.#profileService.asyncFlush();
      } catch (ex) {
        console.error(
          `Failed to flush changes to the profiles database: ${ex}`
        );
      }
    }
  }

  get storeID() {
    return this.#storeID;
  }

  get groupToolkitProfile() {
    return this.#profileService.currentProfile;
  }

  get currentProfile() {
    return this.#currentProfile;
  }

  get initialized() {
    return this.#initialized;
  }

  async initProfilesData() {
    if (lazy.PROFILES_CREATED) {
      return;
    }

    if (!this.groupToolkitProfile) {
      throw new Error("Cannot create a store without a toolkit profile.");
    }

    Services.prefs.setBoolPref(PROFILES_CREATED_PREF_NAME, true);

    let storeID = await ProfilesDatastoreService.storeID;

    this.groupToolkitProfile.storeID = storeID;
    this.#storeID = storeID;
    await this.#attemptFlushProfileService();
  }

  /**
   * The public API to initialize the service.
   *
   * @returns {Promise}
   */
  init() {
    return this.#init();
  }

  /**
   * At startup, store the nsToolkitProfile for the group.
   * Get the groupDBPath from the nsToolkitProfile, and connect to it.
   *
   * @param {boolean} isInitial true if this is an init prior to creating a new profile.
   *
   * @returns {Promise}
   */
  #init(isInitial = false) {
    if (!this.#initPromise) {
      this.#initPromise = this.#initialize(isInitial).finally(
        () => (this.#initPromise = null)
      );
    }

    return this.#initPromise;
  }

  async #initialize(isInitial = false) {
    if (this.#initialized) {
      return;
    }

    this.#profileService = ProfilesDatastoreService.toolkitProfileService;

    this.#storeID = await ProfilesDatastoreService.storeID;

    this.updateEnabledState();
    if (!this.isEnabled) {
      return;
    }

    Glean.profiles.active.set(lazy.PROFILES_CREATED);

    if (!lazy.PROFILES_CREATED) {
      return;
    }

    this.#connection = await ProfilesDatastoreService.getConnection();
    if (!this.#connection) {
      return;
    }

    this.#shutdownBlocker = () => this.uninit();
    ProfilesDatastoreService.shutdown.addBlocker(
      "SelectableProfileService: waiting for pending operations",
      this.#shutdownBlocker
    );

    const resetProfilePath = Services.env.get("SELECTABLE_PROFILE_RESET_PATH");
    if (resetProfilePath) {
      await this.#updateProfilePath(
        resetProfilePath,
        ProfilesDatastoreService.constructor.getDirectory("ProfD").path
      );

      Services.env.set("SELECTABLE_PROFILE_RESET_PATH", "");
      Services.env.set("SELECTABLE_PROFILE_RESET_STORE_ID", "");
    }

    // When we launch into the startup window, the `ProfD` is not defined so
    // getting the directory will throw. Leaving the `currentProfile` as null
    // is fine for the startup window.
    // The current profile will be null now that we are eagerly initing the db.
    try {
      // Get the SelectableProfile by the profile directory
      this.#currentProfile = await this.getProfileByPath(
        ProfilesDatastoreService.constructor.getDirectory("ProfD")
      );
    } catch {}

    if (resetProfilePath && this.#currentProfile) {
      let { themeBg, themeFg } = this.getColorsForDefaultTheme();

      this.currentProfile.theme = {
        themeId: DEFAULT_THEME_ID,
        themeFg,
        themeBg,
      };
    }

    this.#cachedProfileCount = await this.getProfileCount();

    // If this isn't the first init prior to creating the first new profile and
    // the app is started up we should have found a current profile.
    if (!isInitial && !Services.startup.startingUp && !this.#currentProfile) {
      Glean.profiles.currentMissing.record({
        profile_count: this.#cachedProfileCount,
      });

      if (this.#cachedProfileCount) {
        // There are other profiles, re-create the current profile.
        this.#currentProfile = await this.#createProfile(
          ProfilesDatastoreService.constructor.getDirectory("ProfD")
        );

        this.#cachedProfileCount++;
      } else {
        // No other profiles. Reset our state.
        this.groupToolkitProfile.storeID = null;
        await this.#attemptFlushProfileService();
        Services.prefs.setBoolPref(PROFILES_CREATED_PREF_NAME, false);

        this.#connection = null;
        ProfilesDatastoreService.shutdown.removeBlocker(this.#shutdownBlocker);
        this.#shutdownBlocker = null;
        this.updateEnabledState();

        Glean.profiles.active.set(false);

        return;
      }
    }

    Glean.profiles.profileCount.set(this.#cachedProfileCount);

    // This can happen if profiles.ini has been reset by a version of Firefox
    // prior to 67 and the current profile is not the current default for the
    // group. We can recover by overwriting this.groupToolkitProfile.storeID
    // with the current storeID.
    if (this.groupToolkitProfile.storeID != this.storeID) {
      this.groupToolkitProfile.storeID = this.storeID;
      await this.#attemptFlushProfileService();
    }

    // On macOS when other applications request we open a url the most recent
    // window becomes activated first. This would cause the default profile to
    // change before we determine which profile to open the url in. By
    // introducing a small delay we can process the urls before changing the
    // default profile.
    this.#windowActivated = new DeferredTask(
      async () => this.setDefaultProfileForGroup(),
      500
    );

    // The 'activate' event listeners use #currentProfile, so this line has
    // to come after #currentProfile has been set.
    this.initWindowTracker();

    // We must also set the current profile as default during startup.
    await this.setDefaultProfileForGroup();

    Services.obs.addObserver(
      this.themeObserver,
      "lightweight-theme-styling-update"
    );

    let window = Services.wm.getMostRecentBrowserWindow();
    let prefersDarkQuery = window?.matchMedia("(prefers-color-scheme: dark)");
    prefersDarkQuery?.addEventListener("change", this.matchMediaObserver);

    Services.obs.addObserver(this.lookAndFeelChanged, "look-and-feel-changed");

    Services.obs.addObserver(this, "pds-datastore-changed");
    Services.obs.addObserver(this, "taskbar-buttons-refreshed");

    this.#initialized = true;

    // this.#currentProfile is unset in the case that the database has only just been created. We
    // don't need to import from the database in this case.
    if (this.#currentProfile) {
      // Assume that settings in the database may have changed while we weren't running.
      await this.databaseChanged("startup");

      // We only need to migrate if we are in an existing profile group.
      await this.#maybeAddDAUGroupIDToDB();
    }
  }

  async startupMigrationInit() {
    if (this.#initialized) {
      return;
    }

    if (!lazy.MigrationUtils.isStartupMigration) {
      return;
    }

    this.#connection =
      await ProfilesDatastoreService.getStartupMigrationConnection();
    if (!this.#connection) {
      return;
    }

    this.#storeID = await ProfilesDatastoreService.storeID;
  }

  async uninit() {
    if (this.#initPromise) {
      await this.#initPromise.catch(() => {});
    }

    if (this.#shutdownBlocker) {
      ProfilesDatastoreService.shutdown.removeBlocker(this.#shutdownBlocker);
      this.#shutdownBlocker = null;
    }

    if (!this.#initialized) {
      return;
    }

    Services.obs.removeObserver(
      this.themeObserver,
      "lightweight-theme-styling-update"
    );

    Services.obs.removeObserver(
      this.lookAndFeelChanged,
      "look-and-feel-changed"
    );

    this.#currentProfile = null;
    this.#badge = null;
    this.#connection = null;

    this.clearPrefObservers();

    lazy.EveryWindow.unregisterCallback(this.#everyWindowCallbackId);

    Services.obs.removeObserver(this, "pds-datastore-changed");
    Services.obs.removeObserver(this, "taskbar-buttons-refreshed");

    this.#initialized = false;
  }

  initWindowTracker() {
    lazy.EveryWindow.registerCallback(
      this.#everyWindowCallbackId,
      window => {
        this.#setOverlayIcon({ win: window });

        // Update the window title because the currentProfile, needed in the
        // .*-with-profile titles, didn't exist when the title was initially set.
        window.gBrowser.updateTitlebar();

        let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
        if (isPBM) {
          return;
        }

        window.addEventListener("activate", this);
      },
      window => {
        window.gBrowser.updateTitlebar();

        let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
        if (isPBM) {
          return;
        }

        window.removeEventListener("activate", this);
      }
    );
  }

  async handleEvent(event) {
    switch (event.type) {
      case "activate": {
        this.#windowActivated.arm();
        this.#setOverlayIcon({ win: event.target });
        break;
      }
    }
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "pds-datastore-changed": {
        this.databaseChanged(data);
        break;
      }
      case "lightweight-theme-styling-update": {
        this.themeObserver(subject, topic);
        break;
      }
      case "taskbar-buttons-refreshed": {
        // WinTaskbar::RefreshTaskbarButtons cycles DeleteTab/AddTab on each button,
        // which creates fresh taskbar buttons with no overlay state, wiping the profile
        // badge. Re-apply it.
        if (this.#badge && "nsIWinTaskbar" in Ci) {
          for (let win of lazy.EveryWindow.readyWindows) {
            this.#setOverlayIcon({ win });
          }
        }
        break;
      }
    }
  }

  /**
   * When the last selectable profile in a group is deleted,
   * also remove the profile group's named profile entry from profiles.ini
   * and set the profiles created pref to false.
   */
  async deleteProfileGroup() {
    if ((await this.getAllProfiles()).length) {
      return;
    }

    Services.prefs.setBoolPref(PROFILES_CREATED_PREF_NAME, false);
    this.groupToolkitProfile.storeID = null;
    await this.#attemptFlushProfileService();
  }

  // App session lifecycle methods and multi-process support

  /*
   * Helper that executes a new Firefox process. Mostly useful for mocking in
   * unit testing.
   */
  execProcess(aArgs) {
    let executable =
      ProfilesDatastoreService.constructor.getDirectory("XREExeF");

    if (AppConstants.platform == "macosx") {
      // Use the application bundle if possible.
      let appBundle = executable.parent.parent.parent;
      if (appBundle.path.endsWith(".app")) {
        executable = appBundle;

        Cc["@mozilla.org/widget/macdocksupport;1"]
          .getService(Ci.nsIMacDockSupport)
          .launchAppBundle(appBundle, aArgs, { addsToRecentItems: false });
        return;
      }
    }

    let process = Cc["@mozilla.org/process/util;1"].createInstance(
      Ci.nsIProcess
    );
    process.init(executable);
    process.runw(false, aArgs, aArgs.length);
  }

  /**
   * Sends a command line via the remote service. Useful for mocking from automated tests.
   *
   * @param {...any} args Arguments to pass to nsIRemoteService.sendCommandLine.
   */
  sendCommandLine(...args) {
    Cc["@mozilla.org/remote;1"]
      .getService(Ci.nsIRemoteService)
      .sendCommandLine(...args);
  }

  /**
   * Launch a new Firefox instance using the given selectable profile.
   *
   * @param {SelectableProfile} aProfile The profile to launch
   * @param {Array<string>} aUrls An array of urls to open in launched profile
   */
  launchInstance(aProfile, aUrls) {
    let args = [];

    if (aUrls?.length) {
      // See https://wiki.mozilla.org/Firefox/CommandLineOptions#-url_URL
      // Use '-new-tab' instead of '-url' because when opening multiple URLs,
      // Firefox always opens them as tabs in a new window and we want to
      // attempt opening these tabs in an existing window.
      args.push(...aUrls.flatMap(url => ["-new-tab", url]));
    } else {
      args.push(`--${COMMAND_LINE_ACTIVATE}`);
    }

    // If the other instance is already running we can just use the remoting
    // service directly.
    try {
      this.sendCommandLine(aProfile.path, args, true);

      return;
    } catch (e) {
      // This is expected to fail if no instance is running with the profile.
    }

    args.unshift("--profile", aProfile.path);
    if (Services.appinfo.OS === "Darwin") {
      args.unshift("-foreground");
    }

    this.execProcess(args);
  }

  /**
   * When the group DB has been updated, either changes to prefs or profiles,
   * ask the remoting service to notify other running instances that they should
   * check for updates and refresh their UI accordingly.
   */
  async #notifyRunningInstances() {
    let profiles = await this.getAllProfiles();
    for (let profile of profiles) {
      // The current profile was notified above.
      if (profile.id === this.currentProfile?.id) {
        continue;
      }

      try {
        this.sendCommandLine(profile.path, [`--${COMMAND_LINE_UPDATE}`], false);
      } catch (e) {
        // This is expected to fail if no instance is running with the profile.
      }
    }
  }

  async #updateTaskbar() {
    try {
      // We don't want the startup profile selector to badge the dock icon.
      if (!gSupportsBadging || Services.startup.startingUp) {
        return;
      }

      let count = await this.getProfileCount();

      if (count > 1 && !this.#badge) {
        this.#badge = {
          image: await loadImage(this.#currentProfile),
          iconPaintContext: this.#currentProfile.iconPaintContext,
          description: this.#currentProfile.name,
        };

        if ("nsIMacDockSupport" in Ci) {
          Cc["@mozilla.org/widget/macdocksupport;1"]
            .getService(Ci.nsIMacDockSupport)
            .setBadgeImage(this.#badge.image, this.#badge.iconPaintContext);
        } else if ("nsIWinTaskbar" in Ci) {
          for (let win of lazy.EveryWindow.readyWindows) {
            this.#setOverlayIcon({ win });
          }
        }
      } else if (count <= 1 && this.#badge) {
        this.#badge = null;

        if ("nsIMacDockSupport" in Ci) {
          Cc["@mozilla.org/widget/macdocksupport;1"]
            .getService(Ci.nsIMacDockSupport)
            .setBadgeImage(null);
        } else if ("nsIWinTaskbar" in Ci) {
          for (let win of lazy.EveryWindow.readyWindows) {
            let iconController = TASKBAR_ICON_CONTROLLERS.get(win);
            iconController?.setOverlayIcon(null, null);
          }
        }
      }
    } catch (e) {
      console.error(e);
    }
  }

  async #updateTitlebar() {
    let previousCount = this.#cachedProfileCount;
    this.#cachedProfileCount = await this.getProfileCount();
    Glean.profiles.profileCount.set(this.#cachedProfileCount);

    // We only need to update the titles if transitioning to or from a single profile.
    if (previousCount <= 1 || this.#cachedProfileCount <= 1) {
      for (let win of lazy.EveryWindow.readyWindows) {
        win.gBrowser.updateTitlebar();
      }
    }
  }

  /**
   * Invoked when changes have been made to the database. Sends the observer
   * notification "sps-profiles-updated" indicating that something has changed.
   *
   * @param {"local"|"remote"|"startup"|"shutdown"} source The source of the
   *   notification. Either "local" meaning that the change was made in this
   *   process, "remote" meaning the change was made by a different Firefox
   *   instance, "startup" meaning the application has just launched and we may
   *   need to reload changes from the database, or "shutdown" meaning we are
   *   closing the connection and shutting down.
   */
  async databaseChanged(source) {
    if (source === "local" || source === "shutdown") {
      this.#notifyRunningInstances();
    }

    if (source === "shutdown") {
      return;
    }

    if (source != "local") {
      await this.loadSharedPrefsFromDatabase();
    }

    await this.#updateTitlebar();
    await this.#updateTaskbar();

    if (source != "startup") {
      Services.obs.notifyObservers(null, "sps-profiles-updated", source);
    }
  }

  /**
   * The default theme uses `light-dark` color function which doesn't apply
   * correctly to the taskbar avatar icon. We use `InspectorUtils.colorToRGBA`
   * to get the current rgba values for a theme. This way the color values can
   * be correctly applied to the taskbar avatar icon.
   *
   * @returns {object}
   *  themeBg {string}: the background color in rgba(r, g, b, a) format
   *  themeFg {string}: the foreground color in rgba(r, g, b, a) format
   */
  getColorsForDefaultTheme() {
    let window = Services.wm.getMostRecentBrowserWindow();
    // The computedStyles object is a live CSSStyleDeclaration.
    let computedStyles = window.getComputedStyle(
      window.document.documentElement
    );

    let themeFgColor = computedStyles.getPropertyValue("--toolbar-text-color");
    let themeBgColor = computedStyles.getPropertyValue(
      lazy.NOVA_ENABLED
        ? "--background-color-information"
        : "--toolbar-background-color"
    );

    let bg = window.InspectorUtils.colorToRGBA(themeBgColor);
    let themeBg = `rgba(${bg.r}, ${bg.g}, ${bg.b}, ${bg.a})`;

    let fg = window.InspectorUtils.colorToRGBA(themeFgColor);
    let themeFg = `rgba(${fg.r}, ${fg.g}, ${fg.b}, ${fg.a})`;

    return { themeBg, themeFg };
  }

  async enableTheme(themeId, telemetryInfo) {
    let theme = await lazy.AddonManager.getAddonByID(themeId);
    if (!theme) {
      let themeEntry = PROFILE_THEMES_MAP.get(themeId);
      if (themeEntry?.downloadURL) {
        let themeInstall = await lazy.AddonManager.getInstallForURL(
          themeEntry.downloadURL,
          {
            id: themeId,
            telemetryInfo,
            type: "theme",
            version: themeEntry.version,
          }
        );
        await themeInstall.install();
        theme = await lazy.AddonManager.getAddonByID(themeId);
      }
    }

    if (theme) {
      await theme.enable();
    } else {
      console.warn(`enableTheme: could not find or install theme ${themeId}`);
    }

    // If the theme was already active, theme.enable() is a no-op and the
    // themeObserver won't fire. Re-send the notification so that the
    // observer picks up the correct colors.
    let data = lazy.LightweightThemeManager.themeData;
    if (data?.theme) {
      Services.obs.notifyObservers(
        { wrappedJSObject: data },
        "lightweight-theme-styling-update"
      );
    }
  }

  /**
   * Extract theme colors from theme data.
   *
   * @param {object} theme The theme object
   * @returns {{ themeFg: string, themeBg: string }}
   */
  extractThemeColors(theme) {
    let themeFg = theme.icon_color || theme.textcolor || theme.toolbar_text;
    let themeBg = theme.accentcolor || theme.toolbarColor;

    if (theme.id === DEFAULT_THEME_ID || !themeFg || !themeBg) {
      ({ themeBg, themeFg } = this.getColorsForDefaultTheme());
    }

    return { themeFg, themeBg };
  }

  /**
   * Updates the current profile's theme colors based on theme data.
   *
   * @param {object} data The theme data object containing theme and darkTheme
   */
  updateProfileThemeColors(data) {
    if (!data?.theme) {
      return;
    }

    let window = Services.wm.getMostRecentBrowserWindow();
    let isDark = window.matchMedia("(-moz-system-dark-theme)").matches;

    let theme = isDark && !!data.darkTheme ? data.darkTheme : data.theme;

    let { themeFg, themeBg } = this.extractThemeColors(theme);

    if (theme.id === DEFAULT_THEME_ID || !themeFg || !themeBg) {
      window.addEventListener(
        "windowlwthemeupdate",
        () => {
          ({ themeBg, themeFg } = this.getColorsForDefaultTheme());

          this.currentProfile.theme = {
            themeId: theme.id,
            themeFg,
            themeBg,
          };
        },
        {
          once: true,
        }
      );
    } else {
      this.currentProfile.theme = {
        themeId: theme.id,
        themeFg,
        themeBg,
      };
    }
  }

  /**
   * The observer function that watches for theme changes and updates the
   * current profile of a theme change.
   *
   * @param {object} aSubject The theme data
   * @param {string} aTopic Should be "lightweight-theme-styling-update"
   */
  themeObserver(aSubject, aTopic) {
    if (aTopic !== "lightweight-theme-styling-update") {
      return;
    }

    let data = aSubject.wrappedJSObject;
    this.updateProfileThemeColors(data);
  }

  /**
   * The observer function that watches for OS theme changes and updates the
   * current profile of a theme change.
   */
  matchMediaObserver() {
    // If the current theme isn't the default theme, we can just return because
    // we already got the theme colors from the theme change in `themeObserver`
    if (this.currentProfile.theme.themeId !== DEFAULT_THEME_ID) {
      return;
    }

    let { themeBg, themeFg } = this.getColorsForDefaultTheme();

    this.currentProfile.theme = {
      themeId: this.currentProfile.theme.themeId,
      themeFg,
      themeBg,
    };
  }

  /**
   * The observer function that watches for look-and-feel changes (including
   * pref-driven appearance changes from theme-picker) and updates the current
   * profile colors.
   */
  lookAndFeelChanged() {
    let data = lazy.LightweightThemeManager.themeData;
    this.updateProfileThemeColors(data);
  }

  async flushAllSharedPrefsToDatabase() {
    for (let prefName of SelectableProfileServiceClass.permanentSharedPrefs) {
      await this.flushSharedPrefToDatabase(prefName);
    }
  }

  /**
   * Flushes the value of a preference to the database.
   *
   * @param {string} prefName the name of the preference.
   */
  async flushSharedPrefToDatabase(prefName) {
    if (!this.#observedPrefs.has(prefName)) {
      Services.prefs.addObserver(prefName, this.prefObserver);
      this.#observedPrefs.add(prefName);
    }

    if (
      !SelectableProfileServiceClass.permanentSharedPrefs.includes(prefName) &&
      !Services.prefs.prefHasUserValue(prefName)
    ) {
      await this.#deleteDBPref(prefName);
      return;
    }

    let value;

    switch (Services.prefs.getPrefType(prefName)) {
      case Ci.nsIPrefBranch.PREF_BOOL:
        value = Services.prefs.getBoolPref(prefName);
        break;
      case Ci.nsIPrefBranch.PREF_INT:
        value = Services.prefs.getIntPref(prefName);
        break;
      case Ci.nsIPrefBranch.PREF_STRING:
        value = Services.prefs.getCharPref(prefName);
        break;
    }

    await this.#setDBPref(prefName, value);
  }

  clearPrefObservers() {
    for (let prefName of this.#observedPrefs) {
      Services.prefs.removeObserver(prefName, this.prefObserver);
    }
    this.#observedPrefs.clear();
  }

  /**
   * The "datareporting.dau.cachedUsageProfileGroupID" pref is different in
   * every profile before this migration was created. We now need the entire
   * group of profiles to share one group id. To migrate to one shared
   * group id, we need to get the pref into the db for existing group. This
   * function handles this by adding the pref to the db if it doesn't
   * already exist OR if our pref value is better than the value from the db.
   * Consolidation on one group id is also handled in `#maybeSetDAUGroupID`
   * where we overwrite the pref value if the db value is better.
   *
   * New profile groups will automatically start tracking this pref and keep
   * the UUID from the original profile. We need to migrate because the db in
   * existing profile groups will not contain the pref and every profile will
   * have a different group id.
   */
  async #maybeAddDAUGroupIDToDB() {
    let writeToDB = false;
    let prefValue = Services.prefs.getStringPref(DAU_GROUPID_PREF_NAME, "");
    try {
      let dbValue = await this.getDBPref(DAU_GROUPID_PREF_NAME);

      // We found a DAU group id in the db. If our pref value is smaller
      // alphanumerically, we will overwrite the db value.
      if (prefValue < dbValue) {
        // Pref value is smaller alphanumerically so overwrite the db.
        writeToDB = true;
      }
    } catch {
      // The pref is not in the db
      writeToDB = true;
    } finally {
      if (writeToDB) {
        // The pref is not in the db
        // OR
        // our pref value is better so overwrite the db.
        this.#setDBPref(DAU_GROUPID_PREF_NAME, prefValue);
      }
    }
  }

  /**
   * To consolidate on one group id, we compare the pref value from the db and
   * this profiles pref value alphanumerically to converge on the smallest
   * alphanumeric UUID. The `#maybeAddDAUGroupIDToDB` function handles the
   * initial tracking of the "datareporting.dau.cachedUsageProfileGroupID" pref
   * for an existing profile group. New profile groups will keep the original
   * profiles group id.
   *
   * @param {string} dbValue The pref value of
   *   "datareporting.dau.cachedUsageProfileGroupID" from the db
   */
  async #maybeSetDAUGroupID(dbValue) {
    if (dbValue < Services.prefs.getStringPref(DAU_GROUPID_PREF_NAME, "")) {
      try {
        // The value from the db is better so we overwrite our group id.
        await lazy.ClientID.setUsageProfileGroupID(dbValue); // Sets the pref for us.
      } catch (e) {
        // This may throw if the group ID is invalid. This happens in some tests.
        console.error(e);
      }
    }
  }

  /**
   * Fetch all prefs from the DB and write to the current instance.
   */
  async loadSharedPrefsFromDatabase() {
    // This stops us from observing the change during the load and means we stop observing any prefs
    // no longer in the database.
    this.clearPrefObservers();

    for (let { name, value, type } of await this.getAllDBPrefs()) {
      if (SelectableProfileServiceClass.ignoredSharedPrefs.includes(name)) {
        continue;
      }

      // If the user has disabled then re-enabled data collection in another
      // profile in the group, an extra step is needed to ensure each profile
      // uses the same profile group ID.
      if (
        name === GROUPID_PREF_NAME &&
        value !== lazy.TelemetryUtils.knownProfileGroupID &&
        value !== Services.prefs.getCharPref(GROUPID_PREF_NAME, "")
      ) {
        try {
          await lazy.ClientID.setProfileGroupID(value); // Sets the pref for us.
        } catch (e) {
          // This may throw if the group ID is invalid. This happens in some tests.
          console.error(e);
        }
        continue;
      }

      if (name === DAU_GROUPID_PREF_NAME) {
        await this.#maybeSetDAUGroupID(value);

        Services.prefs.addObserver(name, this.prefObserver);
        this.#observedPrefs.add(name);
        continue;
      }

      if (value === null) {
        Services.prefs.clearUserPref(name);
      } else {
        switch (type) {
          case "boolean":
            Services.prefs.setBoolPref(name, value);
            break;
          case "string":
            Services.prefs.setCharPref(name, value);
            break;
          case "number":
            Services.prefs.setIntPref(name, value);
            break;
          case "null":
            Services.prefs.clearUserPref(name);
            break;
        }
      }

      Services.prefs.addObserver(name, this.prefObserver);
      this.#observedPrefs.add(name);
    }

    // Add shared prefs not already in the db to the db
    const permanentSharedPrefsSet = new Set(
      SelectableProfileServiceClass.permanentSharedPrefs
    );
    for (let prefName of permanentSharedPrefsSet.difference(
      this.#observedPrefs
    )) {
      await this.flushSharedPrefToDatabase(prefName);
    }
  }

  /**
   * Update the default profile by setting the selectable profile's path
   * as the path of the nsToolkitProfile for the group. Defaults to the current
   * selectable profile.
   *
   * @param {SelectableProfile} aProfile The SelectableProfile to be
   * set as the default.
   */
  async setDefaultProfileForGroup(aProfile = this.currentProfile) {
    if (!aProfile) {
      return;
    }

    let newRootDir = await aProfile.rootDir;

    // If the profile directory for the group is not changing and no other
    // instance has updated profiles.ini then we don't need to do anything.
    if (
      newRootDir.equals(this.groupToolkitProfile.rootDir) &&
      !this.#profileService.isListOutdated
    ) {
      return;
    }

    this.groupToolkitProfile.rootDir = newRootDir;
    Glean.profilesDefault.updated.record();
    await this.#attemptFlushProfileService();
  }

  /**
   * Update whether to show the selectable profile selector window at startup.
   * Set on the nsToolkitProfile instance for the group.
   *
   * @param {boolean} shouldShow Whether or not we should show the profile selector
   */
  async setShowProfileSelectorWindow(shouldShow) {
    this.groupToolkitProfile.showProfileSelector = shouldShow;
    await this.#attemptFlushProfileService();
  }

  // SelectableProfile lifecycle

  /**
   * Create the profile directory for new profile. The profile name is combined
   * with a salt string to ensure the directory is unique. The format of the
   * directory is salt + "." + profileName. (Ex. c7IZaLu7.testProfile)
   *
   * @param {string} aProfileName The name of the profile to be created
   * @returns {string} The path for the given profile
   */
  async createProfileDirs(aProfileName) {
    const salt = btoa(
      lazy.CryptoUtils.generateRandomBytesLegacy(
        PROFILES_CRYPTO_SALT_LENGTH_BYTES
      )
    );
    // Sometimes the string from CryptoUtils.generateRandomBytesLegacy will
    // contain non-word characters that we don't want to include in the profile
    // directory name. So we match only word characters for the directory name.
    const safeSalt = salt.match(/\w/g).join("").slice(0, 8);

    const profileDir = lazy.DownloadPaths.sanitize(
      `${safeSalt}.${aProfileName}`,
      {
        compressWhitespaces: false,
        allowDirectoryNames: true,
      }
    );

    // Handle errors in bug 1909919
    await Promise.all([
      IOUtils.makeDirectory(
        PathUtils.join(
          ProfilesDatastoreService.constructor.getDirectory("DefProfRt").path,
          profileDir
        ),
        {
          permissions: 0o700,
        }
      ),
      IOUtils.makeDirectory(
        PathUtils.join(
          ProfilesDatastoreService.constructor.getDirectory("DefProfLRt").path,
          profileDir
        ),
        {
          permissions: 0o700,
        }
      ),
    ]);

    return IOUtils.getDirectory(
      PathUtils.join(
        ProfilesDatastoreService.constructor.getDirectory("DefProfRt").path,
        profileDir
      )
    );
  }

  /**
   * Create the times.json file and write the "created" timestamp and
   * "firstUse" as null.
   * Create the prefs.js file and write all shared prefs to the file.
   *
   * @param {nsIFile} profileDir The root dir of the newly created profile
   * @param {string} source The entry point that is creating the profile
   */
  async createProfileInitialFiles(profileDir, source) {
    if (!source) {
      console.error("No source passed for new profile");
    }

    let timesJsonFilePath = await IOUtils.createUniqueFile(
      profileDir.path,
      "times.json",
      0o700
    );

    await IOUtils.writeJSON(timesJsonFilePath, {
      created: Date.now(),
      firstUse: null,
      source: source ?? "unknown",
    });

    let prefsJsFilePath = await IOUtils.createUniqueFile(
      profileDir.path,
      "prefs.js",
      0o600
    );
    await IOUtils.writeUTF8(prefsJsFilePath, Services.prefs.prefsJsPreamble);

    await this.addSelectableProfilePrefs(profileDir.path);
  }

  /**
   * Adds the preferences needed for a selectable profile to work as intended.
   *
   * @param {string} profileDirPath
   */
  async addSelectableProfilePrefs(profileDirPath) {
    const sharedPrefs = await this.getAllDBPrefs();

    let prefsToAdd = new Map(
      sharedPrefs
        .filter(
          pref =>
            !SelectableProfileServiceClass.ignoredSharedPrefs.includes(
              pref.name
            )
        )
        .map(({ name, value }) => [name, value])
    );

    // Preferences that must be set for selectable profiles.
    prefsToAdd.set("browser.profiles.enabled", true);
    prefsToAdd.set("browser.profiles.created", true);
    prefsToAdd.set("toolkit.profiles.storeID", this.storeID);

    const LINEBREAK = AppConstants.platform === "win" ? "\r\n" : "\n";
    await IOUtils.writeUTF8(
      PathUtils.join(profileDirPath, "prefs.js"),
      Array.from(
        prefsToAdd,
        ([name, value]) =>
          `user_pref(${JSON.stringify(name)}, ${typeof value === "string" ? JSON.stringify(value) : value});`
      ).join(LINEBREAK) + LINEBREAK,
      { mode: "appendOrCreate" }
    );
  }

  /**
   * Get a relative to the Profiles directory for the given profile directory.
   *
   * @param {nsIFile} aProfilePath Path to profile directory.
   *
   * @returns {string} A relative path of the profile directory.
   */
  getRelativeProfilePath(aProfilePath) {
    let relativePath = aProfilePath.getRelativePath(
      ProfilesDatastoreService.constructor.getDirectory("UAppData")
    );

    if (AppConstants.platform === "win") {
      relativePath = relativePath.replaceAll("/", "\\");
    }

    return relativePath;
  }

  /**
   * Create a Selectable Profile and add to the datastore.
   *
   * If path is not included, new profile directories will be created.
   *
   * @param {nsIFile} existingProfilePath Optional. The path of an existing profile.
   * @param {string} source The entry point that is creating the profile
   *
   * @returns {SelectableProfile} The newly created profile object.
   */
  async #createProfile(existingProfilePath, source) {
    let nextProfileNumber = Math.max(
      0,
      ...(await this.getAllProfiles()).map(p => p.id)
    );
    let [defaultName, originalName] =
      await lazy.profilesLocalization.formatMessages([
        { id: "default-profile-name", args: { number: nextProfileNumber } },
        { id: "original-profile-name" },
      ]);

    let window = Services.wm.getMostRecentBrowserWindow();
    let isDark = window?.matchMedia("(-moz-system-dark-theme)").matches;

    let randomIndex = Math.floor(Math.random() * this.#defaultAvatars.length);
    let profileData = {
      // The original toolkit profile is added first and is assigned a
      // different name.
      name: nextProfileNumber == 0 ? originalName.value : defaultName.value,
      avatar: this.#defaultAvatars[randomIndex],
      themeId: DEFAULT_THEME_ID,
      themeFg: isDark ? "rgb(255,255,255)" : "rgb(21,20,26)",
      themeBg: isDark ? "rgb(28,27,34)" : "rgb(240,240,244)",
    };

    let path =
      existingProfilePath || (await this.createProfileDirs(profileData.name));
    if (!existingProfilePath) {
      await this.createProfileInitialFiles(path, source);
    }
    profileData.path = this.getRelativeProfilePath(path);

    let profile = await this.insertProfile(profileData);
    return profile;
  }

  /**
   * If the user has never created a SelectableProfile before, the currently
   * running toolkit profile will be added to the datastore and will finish
   * initing the service for profiles.
   */
  async maybeSetupDataStore() {
    if (this.#connection) {
      return;
    }

    await this.initProfilesData();
    await this.#init(true);

    await this.flushAllSharedPrefsToDatabase();

    // If this is the first time the user has created a selectable profile,
    // add the current toolkit profile to the datastore.
    if (!this.#currentProfile) {
      let path = this.groupToolkitProfile.rootDir;
      this.#currentProfile = await this.#createProfile(path, null);

      // And also set the profile selector window to show at startup (bug 1933911).
      await this.setShowProfileSelectorWindow(true);

      // For first-run dark mode macOS users, the original profile's dock icon
      // disappears after creating and launching an additional profile for the
      // first time. Here we hack around this problem.
      //
      // Wait a full second, which seems to be enough time for the newly-
      // launched second Firefox instance's dock animation to complete. Then
      // trigger redrawing the original profile's badged icon (by setting the
      // avatar to its current value, a no-op change which redraws the dock
      // icon as a side effect).
      //
      // Shorter timeouts don't work, perhaps because they trigger the update
      // before the dock bouncing animation completes for the other instance?
      //
      // We haven't figured out the lower-level bug that's causing this, but
      // hope to someday find that better solution (bug 1952338).
      if (Services.appinfo.OS === "Darwin") {
        lazy.setTimeout(() => {
          // To avoid displeasing the linter, assign to a temporary variable.
          let avatar = SelectableProfileService.currentProfile.avatar;
          SelectableProfileService.currentProfile.setAvatar(avatar);
        }, 1000);
      }
    }
  }

  /**
   * Add a profile to the profile group datastore.
   *
   * This function assumes the service is initialized and the datastore has
   * been created.
   *
   * @param {object} profileData A plain object that contains a name, avatar,
   *                 themeId, themeFg, themeBg, and relative path as string.
   *
   * @returns {SelectableProfile} The newly created profile object.
   */
  async insertProfile(profileData) {
    // Verify all fields are present.
    let keys = ["avatar", "name", "path", "themeBg", "themeFg", "themeId"];
    let missing = [];
    keys.forEach(key => {
      if (!(key in profileData)) {
        missing.push(key);
      }
    });
    if (missing.length) {
      throw new Error(
        `Unable to insertProfile due to missing keys: ${missing.join(",")}`
      );
    }
    const rows = await this.#connection.execute(
      `INSERT INTO Profiles
       VALUES (NULL, :path, :name, :avatar, :themeId, :themeFg, :themeBg)
       RETURNING id;`,
      profileData
    );
    const profileId = rows[0].getResultByName("id");
    if (!profileId) {
      throw new Error(`Unable to insertProfile with values: ${profileData}`);
    }

    ProfilesDatastoreService.notify();

    return this.getProfile(profileId);
  }

  async deleteProfile(aProfile) {
    if (aProfile.id == this.currentProfile.id) {
      throw new Error(
        "Use `deleteCurrentProfile` to delete the current profile."
      );
    }

    if (AppConstants.MOZ_PUSH_NOTIFICATION_HELPER) {
      // The helper has to be stopped before the profile directory is removed
      await lazy.PushNotificationHelper.stopForProfile(aProfile.path);
    }

    // First attempt to remove the profile's directories. This will attempt to
    // locate the directories and so will throw an exception if the profile is
    // currently in use.
    await this.#profileService.removeProfileFilesByPath(
      await aProfile.rootDir,
      null,
      0
    );

    // Then we can remove from the database.
    await this.#connection.execute("DELETE FROM Profiles WHERE id = :id;", {
      id: aProfile.id,
    });

    ProfilesDatastoreService.notify();
  }

  /**
   * Schedule deletion of the current SelectableProfile as a background task.
   */
  async deleteCurrentProfile() {
    let profiles = await this.getAllProfiles();

    if (profiles.length <= 1) {
      await this.createNewProfile(true, null, "replace-last");
      await this.setShowProfileSelectorWindow(false);

      profiles = await this.getAllProfiles();
    }

    // TODO: (Bug 1923980) How should we choose the new default profile?
    let newDefault = profiles.find(p => p.id !== this.currentProfile.id);
    await this.setDefaultProfileForGroup(newDefault);

    await this.currentProfile.removeDesktopShortcut();

    if (AppConstants.MOZ_PUSH_NOTIFICATION_HELPER) {
      // The helper has to be stopped before the profile directory is removed
      await lazy.PushNotificationHelper.stopForProfile(
        this.currentProfile.path
      );
    }

    await this.#connection.executeBeforeShutdown(
      "SelectableProfileService: deleteCurrentProfile",
      async db => {
        await db.execute("DELETE FROM Profiles WHERE id = :id;", {
          id: this.currentProfile.id,
        });

        // TODO(bug 1969488): Make this less tightly coupled so consumers of the
        // ProfilesDatastoreService can register cleanup actions to occur during
        // profile deletion.
        await db.execute(
          "DELETE FROM NimbusEnrollments WHERE profileId = :profileId;",
          {
            profileId: lazy.ExperimentAPI.profileId,
          }
        );

        await db.execute(
          "DELETE FROM NimbusSyncTimestamps WHERE profileId = :profileId;",
          {
            profileId: lazy.ExperimentAPI.profileId,
          }
        );

        await db.execute(
          "DELETE FROM NimbusEnrollmentStatuses WHERE profileId = :profileId;",
          {
            profileId: lazy.ExperimentAPI.profileId,
          }
        );
      }
    );

    if (AppConstants.MOZ_BACKGROUNDTASKS) {
      // Schedule deletion of the profile directories.
      const runner = Cc["@mozilla.org/backgroundtasksrunner;1"].getService(
        Ci.nsIBackgroundTasksRunner
      );
      let rootDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
      let localDir = Services.dirsvc.get("ProfLD", Ci.nsIFile);
      runner.runInDetachedProcess("removeProfileFiles", [
        rootDir.path,
        localDir.path,
        180,
      ]);
    }
  }

  /**
   * Write an updated profile to the DB.
   *
   * @param {SelectableProfile} aSelectableProfile The SelectableProfile to be updated
   */
  async updateProfile(aSelectableProfile) {
    let profileObj = aSelectableProfile.toDbObject();

    await this.#connection.execute(
      `UPDATE Profiles
       SET path = :path, name = :name, avatar = :avatar, themeId = :themeId, themeFg = :themeFg, themeBg = :themeBg
       WHERE id = :id;`,
      profileObj
    );

    if (aSelectableProfile.id == this.#currentProfile.id) {
      // Force a rebuild of the taskbar icon.
      this.#badge = null;
      this.#currentProfile = aSelectableProfile;
    }

    ProfilesDatastoreService.notify();
  }

  /**
   * Update the profile path in the db.
   *
   * @param {string} aProfilePath The absolute path of the selectable profile
   * to be updated
   * @param {string} aUpdatedPath The new absolute path for the selectable
   * profile
   */
  async #updateProfilePath(aProfilePath, aUpdatedPath) {
    let aProfileDir = Cc["@mozilla.org/file/local;1"].createInstance(
      Ci.nsIFile
    );
    aProfileDir.initWithPath(aProfilePath);
    let relativePath = this.getRelativeProfilePath(aProfileDir);

    let aUpdatedDir = Cc["@mozilla.org/file/local;1"].createInstance(
      Ci.nsIFile
    );
    aUpdatedDir.initWithPath(aUpdatedPath);
    let updatedRelativePath = this.getRelativeProfilePath(aUpdatedDir);

    await this.#connection.execute(
      `UPDATE Profiles SET path = :path WHERE path = :current;`,
      { current: relativePath, path: updatedRelativePath }
    );
  }

  /**
   * Create and launch a new SelectableProfile and add it to the group datastore.
   * This is an unmanaged profile from the nsToolkitProfile perspective.
   *
   * If the user has never created a SelectableProfile before, the currently
   * running toolkit profile will be added to the datastore along with the
   * newly created profile.
   *
   * Launches the new SelectableProfile in a new instance after creating it.
   *
   * @param {boolean} [launchProfile=true] Whether or not this should launch
   * the newly created profile.
   * @param {nsIFile} [existingProfilePath=null] Optional path to use for the
   * profile instead of creating new directories in the default location.
   * @param {string} source The entry point that is creating the profile
   *
   * @returns {SelectableProfile} The profile just created.
   */
  async createNewProfile(
    launchProfile = true,
    existingProfilePath = null,
    source
  ) {
    await this.maybeSetupDataStore();

    let profile = await this.#createProfile(existingProfilePath, source);
    if (launchProfile) {
      this.launchInstance(profile, ["about:newprofile"]);
    }
    return profile;
  }

  /**
   * Get the complete list of profiles in the group.
   *
   * @returns {Array<SelectableProfile>}
   *   An array of profiles in the group.
   */
  async getAllProfiles() {
    if (!this.#connection) {
      return [];
    }

    return (await this.#connection.executeCached("SELECT * FROM Profiles;"))
      .map(row => {
        return new SelectableProfile(row);
      })
      .sort((p1, p2) => p1.name.localeCompare(p2.name));
  }

  /**
   * Synchronously gets a cached value for the number of profiles in the group.
   * This will be incorrect in the event that another instance has added or
   * removed profiles recently.
   *
   * @returns {number}
   *   The cached number of profiles in the group.
   */
  getCachedProfileCount() {
    return this.#cachedProfileCount;
  }

  /**
   * Get the number of profiles in the group.
   *
   * @returns {number}
   *   The number of profiles in the group.
   */
  async getProfileCount() {
    if (!this.#connection) {
      return 0;
    }

    let rows = await this.#connection.executeCached(
      'SELECT COUNT(*) AS "count" FROM "Profiles";'
    );

    return rows[0]?.getResultByName("count") ?? 0;
  }

  /**
   * Get a specific profile by its internal ID.
   *
   * @param {number} aProfileID The internal id of the profile
   * @returns {SelectableProfile}
   *   The specific profile.
   */
  async getProfile(aProfileID) {
    if (!this.#connection) {
      return null;
    }

    let row = (
      await this.#connection.executeCached(
        "SELECT * FROM Profiles WHERE id = :id;",
        {
          id: aProfileID,
        }
      )
    )[0];

    return row ? new SelectableProfile(row) : null;
  }

  /**
   * Get a specific profile by its name.
   *
   * @param {string} aProfileName The name of the profile
   * @returns {SelectableProfile}
   *   The specific profile.
   */
  async getProfileByName(aProfileName) {
    if (!this.#connection) {
      return null;
    }

    let row = (
      await this.#connection.execute(
        "SELECT * FROM Profiles WHERE name = :name;",
        {
          name: aProfileName,
        }
      )
    )[0];

    return row ? new SelectableProfile(row) : null;
  }

  /**
   * Get a specific profile by its absolute path.
   *
   * @param {nsIFile} aProfilePath The path of the profile
   * @returns {SelectableProfile|null}
   */
  async getProfileByPath(aProfilePath) {
    if (!this.#connection) {
      return null;
    }

    let relativePath = this.getRelativeProfilePath(aProfilePath);
    let row = (
      await this.#connection.execute(
        "SELECT * FROM Profiles WHERE path = :path;",
        {
          path: relativePath,
        }
      )
    )[0];

    return row ? new SelectableProfile(row) : null;
  }

  // Shared Prefs management

  getPrefValueFromRow(row) {
    let value = row.getResultByName("value");
    if (row.getResultByName("isBoolean")) {
      return value === 1;
    }

    return value;
  }

  /**
   * Get all shared prefs as a list.
   *
   * @returns {{name: string, value: *, type: string}}
   */
  async getAllDBPrefs() {
    return (
      await this.#connection.executeCached("SELECT * FROM SharedPrefs;")
    ).map(row => {
      let value = this.getPrefValueFromRow(row);
      return {
        name: row.getResultByName("name"),
        value,
        type: typeof value,
      };
    });
  }

  /**
   * Get the value of a specific shared pref from the database.
   *
   * @param {string} aPrefName The name of the pref to get
   *
   * @returns {any} Value of the pref
   */
  async getDBPref(aPrefName) {
    let rows = await this.#connection.execute(
      "SELECT value, isBoolean FROM SharedPrefs WHERE name = :name;",
      {
        name: aPrefName,
      }
    );

    if (!rows.length) {
      throw new Error(`Unknown preference '${aPrefName}'`);
    }

    return this.getPrefValueFromRow(rows[0]);
  }

  async setDBPref(aPrefName, aPrefValue) {
    if (!Cu.isInAutomation) {
      return;
    }

    await this.#setDBPref(aPrefName, aPrefValue);
  }

  /**
   * Insert or update a pref value in the database, then notify() other running instances.
   *
   * @param {string} aPrefName The name of the pref
   * @param {any} aPrefValue The value of the pref
   */
  async #setDBPref(aPrefName, aPrefValue) {
    await this.#connection.execute(
      "INSERT INTO SharedPrefs(id, name, value, isBoolean) VALUES (NULL, :name, :value, :isBoolean) ON CONFLICT(name) DO UPDATE SET value=excluded.value, isBoolean=excluded.isBoolean;",
      {
        name: aPrefName,
        value: aPrefValue,
        isBoolean: typeof aPrefValue === "boolean",
      }
    );

    ProfilesDatastoreService.notify();
  }

  // Starts tracking a new shared pref across the profiles.
  async trackPref(aPrefName) {
    await this.flushSharedPrefToDatabase(aPrefName);
  }

  async deleteDBPref(aPrefName) {
    if (!Cu.isInAutomation) {
      return;
    }

    await this.#deleteDBPref(aPrefName);
  }

  /**
   * Remove a shared pref from the database, then notify() other running instances.
   *
   * @param {string} aPrefName The name of the pref to delete
   */
  async #deleteDBPref(aPrefName) {
    // We mark the value as null if it already exists in the database so other profiles know what
    // preference to remove.
    await this.#connection.executeCached(
      "UPDATE SharedPrefs SET value=NULL, isBoolean=FALSE WHERE name=:name;",
      {
        name: aPrefName,
      }
    );

    ProfilesDatastoreService.notify();
  }
}

const SelectableProfileService = new SelectableProfileServiceClass();
export { SelectableProfileService };

/**
 * A command line handler for receiving notifications from other instances that
 * the profiles database has been updated.
 */
export class CommandLineHandler {
  static classID = Components.ID("{38971986-c834-4f52-bf17-5123fbc9dde5}");
  static contractID = "@mozilla.org/browser/selectable-profiles-service-clh;1";

  QueryInterface = ChromeUtils.generateQI([Ci.nsICommandLineHandler]);

  /**
   * Finds the current default profile path for the current profile group.
   *
   * @returns {Promise<string|null>}
   */
  async findDefaultProfilePath() {
    try {
      let profilesRoot =
        ProfilesDatastoreService.constructor.getDirectory("UAppData");

      let iniPath = PathUtils.join(profilesRoot.path, "profiles.ini");

      let iniData = await IOUtils.readUTF8(iniPath);

      let iniParser = Cc["@mozilla.org/xpcom/ini-parser-factory;1"]
        .getService(Ci.nsIINIParserFactory)
        .createINIParser(null);
      iniParser.initFromString(iniData);

      // loop is guaranteed to exit once it finds a profile section with no path.
      // eslint-disable-next-line no-constant-condition
      for (let i = 0; true; i++) {
        let section = `Profile${i}`;

        let path;
        try {
          path = iniParser.getString(section, "Path");
        } catch (e) {
          // No path means this section doesn't exist so we've seen them all.
          break;
        }

        try {
          let storeID = iniParser.getString(section, "StoreID");

          if (storeID != SelectableProfileService.storeID) {
            continue;
          }

          let isRelative = iniParser.getString(section, "IsRelative") == "1";
          if (isRelative) {
            let profileDir = Cc["@mozilla.org/file/local;1"].createInstance(
              Ci.nsIFile
            );
            profileDir.setRelativeDescriptor(profilesRoot, path);
            path = profileDir.path;
          }

          return path;
        } catch (e) {
          // Ignore missing keys and just continue to the next section.
          continue;
        }
      }
    } catch (e) {
      console.error(e);
    }

    return null;
  }

  /**
   * Attempts to parse the arguments expected when opening URLs from other
   * applications on macOS.
   *
   * @param {Array<string>} args The command line arguments.
   * @returns {boolean} True if the arguments matched the expected form.
   */
  openUrls(args) {
    // Arguments are expected to be URL flag and URL pairs.
    if (args.length % 2 != 0) {
      return false;
    }

    for (let i = 0; i < args.length; i += 2) {
      if (args[i] != "-url" && args[i] != "-url-background") {
        return false;
      }
    }

    // Now the arguments are verified to only be URL arguments we can pass
    // them directly to the only handler for those arguments.
    let workingDir = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
    let cmdLine = Cu.createCommandLine(
      args,
      workingDir,
      Ci.nsICommandLine.STATE_REMOTE_EXPLICIT
    );

    try {
      let handler = Cc["@mozilla.org/browser/final-clh;1"].createInstance(
        Ci.nsICommandLineHandler
      );
      handler.handle(cmdLine);
    } catch (e) {
      console.error(e);
      return false;
    }

    return true;
  }

  async redirectCommandLine(args) {
    let shouldActivate =
      !args.length ||
      args.length % 2 != 0 ||
      args.some((arg, index) => index % 2 == 0 && arg != "-url-background");
    let defaultPath = await this.findDefaultProfilePath();

    if (defaultPath) {
      if (
        defaultPath == SelectableProfileService.currentProfile?.path &&
        this.openUrls(args)
      ) {
        return;
      }

      // Attempt to use the remoting service to send the arguments to any
      // existing instance of this profile (this even works for the current
      // instance on macOS which is the only platform we call this for).
      try {
        SelectableProfileService.sendCommandLine(
          defaultPath,
          args,
          shouldActivate
        );

        return;
      } catch (e) {
        // This is expected to fail if no instance is running with the profile.
      }
    }

    // Fall back to re-launching.
    SelectableProfileService.execProcess([
      ...(shouldActivate ? ["-foreground"] : []),
      ...args,
    ]);
  }

  handle(cmdLine) {
    // This is only ever sent when the application is already running.
    if (cmdLine.handleFlag(COMMAND_LINE_UPDATE, true)) {
      if (SelectableProfileService.initialized) {
        SelectableProfileService.databaseChanged("remote").catch(console.error);
      }
      cmdLine.preventDefault = true;
      return;
    }

    // Sent from the profiles UI to launch a profile if it doesn't exist or bring it to the front
    // if it is already running. In the case where this instance is already running we want to block
    // the normal action of opening a new empty window and instead raise the application to the
    // front manually.
    if (
      cmdLine.handleFlag(COMMAND_LINE_ACTIVATE, true) &&
      cmdLine.state != Ci.nsICommandLine.STATE_INITIAL_LAUNCH
    ) {
      let win = Services.wm.getMostRecentBrowserWindow();
      if (win) {
        win.focus();
        cmdLine.preventDefault = true;
        return;
      }
    }

    // On macOS requests to open URLs from other applications in an already running Firefox are
    // passed directly to the running instance via the
    // [MacApplicationDelegate::openURLs](https://searchfox.org/mozilla-central/rev/b0b003e992b199fd8e13999bd5d06d06c84a3fd2/toolkit/xre/MacApplicationDelegate.mm#323-326)
    // API. This means it skips over the step in startup where we choose the correct profile to open
    // the link in. Here we intercept such requests.
    if (
      cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_EXPLICIT &&
      Services.appinfo.OS === "Darwin"
    ) {
      // If we aren't enabled or initialized there can't be other profiles.
      if (
        !SelectableProfileService.isEnabled ||
        !SelectableProfileService.initialized
      ) {
        return;
      }

      if (!cmdLine.length) {
        return;
      }

      // We need to parse profiles.ini to determine whether this profile is the
      // current default and this requires async I/O. So we're just going to
      // tell other command line handlers that this command line has been handled
      // as we can't wait for the async operation to complete.
      let args = [];
      for (let i = 0; i < cmdLine.length; i++) {
        args.push(cmdLine.getArgument(i));
      }

      this.redirectCommandLine(args).catch(console.error);

      cmdLine.removeArguments(0, cmdLine.length - 1);
      cmdLine.preventDefault = true;
    }
  }
}
