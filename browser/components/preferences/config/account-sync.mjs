/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const { SCOPE_APP_SYNC } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);
const XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;
const lazy = XPCOMUtils.declareLazy({
  BackupService: "resource:///modules/backup/BackupService.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

Preferences.addAll([
  // sync
  { id: "services.sync.engine.bookmarks", type: "bool" },
  { id: "services.sync.engine.history", type: "bool" },
  { id: "services.sync.engine.tabs", type: "bool" },
  { id: "services.sync.engine.passwords", type: "bool" },
  { id: "services.sync.engine.addresses", type: "bool" },
  { id: "services.sync.engine.creditcards", type: "bool" },
  { id: "services.sync.engine.addons", type: "bool" },
  { id: "services.sync.engine.prefs", type: "bool" },
]);

/**
 * A helper class for managing sync related UI behavior.
 */
export var SyncHelpers = new (class SyncHelpers {
  /**
   * href for Connect another device link.
   *
   * @type {string}
   */
  connectAnotherDeviceHref = "";

  /**
   * Returns the current global UIState.
   *
   * @type {object}
   * @readonly
   */
  get uiState() {
    let state = window.UIState.get();
    return state;
  }

  /**
   * Retrieves the current UI state status from the global UIState.
   *
   * @type {string}
   * @readonly
   */
  get uiStateStatus() {
    return this.uiState.status;
  }

  /**
   * Whether Sync is currently enabled in the UIState.
   *
   * @type {boolean}
   * @readonly
   */
  get isSyncEnabled() {
    return this.uiState.syncEnabled;
  }

  /**
   * Extracts and sanitizes the `entrypoint` parameter from the current document URL.
   *
   * @returns {string} The sanitized entry point name.
   */
  getEntryPoint() {
    let params = URL.fromURI(document.documentURIObject).searchParams;
    let entryPoint = params.get("entrypoint") || "preferences";
    entryPoint = entryPoint.replace(/[^-.\w]/g, "");
    return entryPoint;
  }

  /**
   * Replace the current tab with the specified URL.
   *
   * @param {string} url
   */
  replaceTabWithUrl(url) {
    // Get the <browser> element hosting us.
    let browser = window.docShell.chromeEventHandler;
    // And tell it to load our URL.
    browser.loadURI(Services.io.newURI(url), {
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        {}
      ),
    });
  }

  /**
   * Opens the "Choose What to Sync" dialog and handles user interaction.
   *
   * @param {boolean} isSyncConfigured
   *        Whether Sync is already configured for this profile.
   * @param {string|null} [why=null]
   *        Optional reason or event name indicating why the dialog was opened.
   * @returns {Promise<void>}
   *          Resolves when the dialog flow and any post-actions have completed.
   */
  async _chooseWhatToSync(isSyncConfigured, why = null) {
    // Record the user opening the choose what to sync menu.
    window.fxAccounts.telemetry.recordOpenCWTSMenu(why).catch(err => {
      console.error("Failed to record open CWTS menu event", err);
    });

    // Assuming another device is syncing and we're not,
    // we update the engines selection so the correct
    // checkboxes are pre-filed.
    if (!isSyncConfigured) {
      try {
        await lazy.Weave.Service.updateLocalEnginesState();
      } catch (err) {
        console.error("Error updating the local engines state", err);
      }
    }
    let params = {};
    if (isSyncConfigured) {
      // If we are already syncing then we also offer to disconnect.
      params.disconnectFun = () => this.disconnectSync();
    }
    window.gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/syncChooseWhatToSync.xhtml",
      {
        closingCallback: event => {
          if (event.detail.button == "accept") {
            // Sync wasn't previously configured, but the user has accepted
            // so we want to now start syncing!
            if (!isSyncConfigured) {
              window.fxAccounts.telemetry
                .recordConnection(["sync"], "ui")
                .then(() => {
                  return lazy.Weave.Service.configure();
                })
                .catch(err => {
                  console.error("Failed to enable sync", err);
                });
            } else {
              // User is already configured and have possibly changed the engines they want to
              // sync, so we should let the server know immediately
              // if the user is currently syncing, we queue another sync after
              // to ensure we caught their updates
              Services.tm.dispatchToMainThread(() => {
                lazy.Weave.Service.queueSync("cwts");
              });
            }
          }
          // When the modal closes we want to remove any query params
          // so it doesn't open on subsequent visits (and will reload)
          const browser = window.docShell.chromeEventHandler;
          browser.loadURI(Services.io.newURI("about:preferences#sync"), {
            triggeringPrincipal:
              Services.scriptSecurityManager.getSystemPrincipal(),
          });
        },
      },
      params /* aParams */
    );
  }

  // Disconnect sync, leaving the account connected.
  disconnectSync() {
    return window.browsingContext.topChromeWindow.gSync.disconnect({
      confirm: true,
      disconnectAccount: false,
    });
  }

  async setupSync() {
    try {
      const hasKeys =
        await window.fxAccounts.keys.hasKeysForScope(SCOPE_APP_SYNC);
      if (hasKeys) {
        // User has keys - open the choose what to sync dialog
        this._chooseWhatToSync(false, "setupSync");
      } else {
        // User signed in via third-party auth without sync keys.
        // Redirect to FxA to create a password and generate sync keys.
        // canConnectAccount() checks if the Primary Password is locked and
        // prompts the user to unlock it. Returns false if the user cancels.
        if (!(await window.FxAccounts.canConnectAccount())) {
          return;
        }
        const url = await window.FxAccounts.config.promiseConnectAccountURI(
          this.getEntryPoint()
        );
        this.replaceTabWithUrl(url);
      }
    } catch (err) {
      console.error("Failed to check for sync keys", err);
      // Fallback to opening CWTS dialog
      this._chooseWhatToSync(false, "setupSync");
    }
  }

  async signIn() {
    if (!(await window.FxAccounts.canConnectAccount())) {
      return;
    }
    const url = await window.FxAccounts.config.promiseConnectAccountURI(
      this.getEntryPoint()
    );
    this.replaceTabWithUrl(url);
  }

  /**
   * Attempts to take the user through the sign in flow by opening the web content
   * with the given entrypoint as a query parameter
   *
   * @param {string} entrypoint
   *        An string appended to the query parameters, used in telemetry to differentiate
   *        different entrypoints to accounts
   */
  async reSignIn(entrypoint) {
    const url =
      await window.FxAccounts.config.promiseConnectAccountURI(entrypoint);
    this.replaceTabWithUrl(url);
  }

  async verifyFirefoxAccount() {
    return this.reSignIn("preferences-reverify");
  }

  /**
   * Disconnect the account, including everything linked.
   *
   * @param {boolean} showConfirm
   *        Whether to show a confirmation dialog before disconnecting
   */
  unlinkFirefoxAccount(showConfirm) {
    window.browsingContext.topChromeWindow.gSync.disconnect({
      showConfirm,
    });
  }

  // Check if the user is coming from a call to action
  // and show them the correct additional panel
  maybeShowSyncAction() {
    if (
      location.hash == "#sync" &&
      window.UIState.get().status == window.UIState.STATUS_SIGNED_IN
    ) {
      if (location.href.includes("action=pair")) {
        window.gSubDialog.open(
          "chrome://browser/content/preferences/fxaPairDevice.xhtml",

          { features: "resizable=no" }
        );
      } else if (location.href.includes("action=choose-what-to-sync")) {
        this._chooseWhatToSync(false, "callToAction");
      }
    }
  }
})();

// Expose SyncHelpers on the window object for tests and other code.
window.SyncHelpers = SyncHelpers;

// Listen for when sync pane is loaded to check for actions
window.addEventListener("paneshown", e => {
  if (e.detail.category == "paneSync") {
    SyncHelpers.maybeShowSyncAction();
  }
});

Preferences.addSetting({
  id: "uiStateUpdate",
  setup(emitChange) {
    lazy.Weave.Svc.Obs.add(window.UIState.ON_UPDATE, emitChange);
    return () =>
      lazy.Weave.Svc.Obs.remove(window.UIState.ON_UPDATE, emitChange);
  },
});

// Mozilla accounts section

// Logged out of Mozilla account
Preferences.addSetting({
  id: "noFxaAccountGroup",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus == window.UIState.STATUS_NOT_CONFIGURED;
  },
});
Preferences.addSetting({
  id: "noFxaAccount",
});
Preferences.addSetting({
  id: "noFxaSignIn",
  onUserClick: () => {
    SyncHelpers.signIn();
  },
});

// Logged in and verified and all is good
Preferences.addSetting({
  id: "fxaSignedInGroup",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus == window.UIState.STATUS_SIGNED_IN;
  },
});
Preferences.addSetting({
  id: "fxaLoginVerified",
  deps: ["uiStateUpdate"],
  _failedAvatarURLs: new Set(),
  getControlConfig(config, _, setting) {
    let state = SyncHelpers.uiState;

    if (state.displayName) {
      config.l10nId = "sync-account-signed-in-display-name";
      config.l10nArgs = {
        name: state.displayName,
        email: state.email || "",
      };
    } else {
      config.l10nId = "sync-account-signed-in";
      config.l10nArgs = {
        email: state.email || "",
      };
    }

    // Reset the image to default avatar if we encounter an error.
    if (this._failedAvatarURLs.has(state.avatarURL)) {
      config.iconSrc = "chrome://browser/skin/fxa/avatar-color.svg";
      return config;
    }

    if (state.avatarURL && !state.avatarIsDefault) {
      config.iconSrc = state.avatarURL;
      let img = new Image();
      img.onerror = () => {
        this._failedAvatarURLs.add(state.avatarURL);
        setting.onChange();
      };
      img.src = state.avatarURL;
    }
    return config;
  },
});
Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "verifiedManage";

    setup() {
      lazy.Weave.Svc.Obs.add(window.UIState.ON_UPDATE, this.emitChange);
      return () =>
        lazy.Weave.Svc.Obs.remove(window.UIState.ON_UPDATE, this.emitChange);
    }

    // The "manage account" link embeds the uid, so we need to update this
    // if the account state changes.
    async getControlConfig() {
      let href = await window.FxAccounts.config.promiseManageURI(
        SyncHelpers.getEntryPoint()
      );
      return {
        controlAttrs: {
          href: href ?? "https://accounts.firefox.com/settings",
        },
      };
    }
  }
);

Preferences.addSetting({
  id: "fxaUnlinkButton",
  onUserClick: () => {
    SyncHelpers.unlinkFirefoxAccount(true);
  },
});

// Logged in to an unverified account
Preferences.addSetting({
  id: "fxaUnverifiedGroup",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus == window.UIState.STATUS_NOT_VERIFIED;
  },
});
Preferences.addSetting({
  id: "fxaLoginUnverified",
  deps: ["uiStateUpdate"],
  getControlConfig(config) {
    let state = SyncHelpers.uiState;
    config.l10nArgs = {
      email: state.email || "",
    };
    return config;
  },
});
Preferences.addSetting({
  id: "verifyFxaAccount",
  onUserClick: () => {
    SyncHelpers.verifyFirefoxAccount();
  },
});
Preferences.addSetting({
  id: "unverifiedUnlinkFxaAccount",
  onUserClick: () => {
    /* no warning as account can't have previously synced */
    SyncHelpers.unlinkFirefoxAccount(false);
  },
});

// Logged in locally but server rejected credentials
Preferences.addSetting({
  id: "fxaLoginRejectedGroup",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus == window.UIState.STATUS_LOGIN_FAILED;
  },
});
Preferences.addSetting({
  id: "fxaLoginRejected",
  deps: ["uiStateUpdate"],
  getControlConfig(config) {
    let state = SyncHelpers.uiState;
    config.l10nArgs = {
      email: state.email || "",
    };
    return config;
  },
});
Preferences.addSetting({
  id: "rejectReSignIn",
  onUserClick: () => {
    SyncHelpers.reSignIn(SyncHelpers.getEntryPoint());
  },
});
Preferences.addSetting({
  id: "rejectUnlinkFxaAccount",
  onUserClick: () => {
    SyncHelpers.unlinkFirefoxAccount(true);
  },
});

//Sync section

//Sync section - no Firefox account
Preferences.addSetting({
  id: "syncNoFxaSignIn",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus === window.UIState.STATUS_NOT_CONFIGURED;
  },
  onUserClick: () => {
    SyncHelpers.signIn();
  },
});

// Sync section - Syncing is OFF
Preferences.addSetting({
  id: "syncNotConfigured",
  deps: ["uiStateUpdate"],
  visible() {
    return (
      SyncHelpers.uiStateStatus === window.UIState.STATUS_SIGNED_IN &&
      !SyncHelpers.isSyncEnabled
    );
  },
});
Preferences.addSetting({
  id: "syncSetup",
  onUserClick: () => SyncHelpers.setupSync(),
});

// Sync section - Syncing is ON
Preferences.addSetting({
  id: "syncConfigured",
  deps: ["uiStateUpdate"],
  visible() {
    return (
      SyncHelpers.uiStateStatus === window.UIState.STATUS_SIGNED_IN &&
      SyncHelpers.isSyncEnabled
    );
  },
});

Preferences.addSetting({
  id: "syncStatus",
});
Preferences.addSetting({
  id: "syncNow",
  deps: ["uiStateUpdate"],
  onUserClick() {
    lazy.Weave.Service.sync({ why: "aboutprefs" });
  },
  visible: () => !SyncHelpers.uiState.syncing,
  // Bug 2004864 - add tooltip
});
Preferences.addSetting({
  id: "syncing",
  deps: ["uiStateUpdate"],
  disabled: () => SyncHelpers.uiState.syncing,
  visible: () => SyncHelpers.uiState.syncing,
});

const SYNC_ENGINE_SETTINGS = [
  {
    id: "syncBookmarks",
    pref: "services.sync.engine.bookmarks",
    type: "bookmarks",
  },
  { id: "syncHistory", pref: "services.sync.engine.history", type: "history" },
  { id: "syncTabs", pref: "services.sync.engine.tabs", type: "tabs" },
  {
    id: "syncPasswords",
    pref: "services.sync.engine.passwords",
    type: "passwords",
  },
  {
    id: "syncAddresses",
    pref: "services.sync.engine.addresses",
    type: "addresses",
  },
  {
    id: "syncPayments",
    pref: "services.sync.engine.creditcards",
    type: "payments",
  },
  { id: "syncAddons", pref: "services.sync.engine.addons", type: "addons" },
  { id: "syncSettings", pref: "services.sync.engine.prefs", type: "settings" },
];

SYNC_ENGINE_SETTINGS.forEach(({ id, pref }) => {
  Preferences.addSetting({ id, pref });
});

Preferences.addSetting({
  id: "syncEngines",
  deps: SYNC_ENGINE_SETTINGS.map(({ id }) => id),
  get(_, deps) {
    return SYNC_ENGINE_SETTINGS.filter(({ id }) => deps[id]?.value).map(
      ({ type }) => type
    );
  },
});

Preferences.addSetting({
  id: "syncEnginesList",
  deps: ["syncEngines"],
  getControlConfig(config, { syncEngines }) {
    return {
      ...config,
      controlAttrs: {
        ...config.controlAttrs,
        ".engines": syncEngines.value,
      },
    };
  },
});

Preferences.addSetting({
  id: "syncChangeOptions",
  deps: ["syncEngines"],
  onUserClick: () => {
    SyncHelpers._chooseWhatToSync(true, "manageSyncSettings");
  },
  visible: ({ syncEngines }) => {
    return syncEngines.value && syncEngines.value.length;
  },
});

Preferences.addSetting({
  id: "syncDisconnect",
  onUserClick: () => {
    SyncHelpers.disconnectSync();
  },
});

// Sync section - Device name
Preferences.addSetting({
  id: "fxaDeviceNameSection",
  deps: ["uiStateUpdate"],
  visible() {
    return SyncHelpers.uiStateStatus !== window.UIState.STATUS_NOT_CONFIGURED;
  },
});
Preferences.addSetting({
  id: "fxaDeviceNameGroup",
});
Preferences.addSetting({
  id: "fxaDeviceName",
  deps: ["uiStateUpdate"],
  get() {
    return lazy.Weave.Service.clientsEngine?.localName;
  },
  set(val) {
    lazy.Weave.Service.clientsEngine.localName = val;
  },
  disabled() {
    return SyncHelpers.uiStateStatus !== window.UIState.STATUS_SIGNED_IN;
  },
  getControlConfig(config) {
    if (config.controlAttrs?.defaultvalue) {
      return config;
    }
    const deviceDefaultLocalName =
      window.fxAccounts?.device?.getDefaultLocalName();
    if (deviceDefaultLocalName) {
      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          defaultvalue: deviceDefaultLocalName,
        },
      };
    }
    return config;
  },
});
Preferences.addSetting({
  id: "fxaConnectAnotherDevice",
  getControlConfig(config) {
    if (SyncHelpers.connectAnotherDeviceHref) {
      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          href: SyncHelpers.connectAnotherDeviceHref,
        },
      };
    }
    return config;
  },
  setup(emitChange) {
    window.FxAccounts.config
      .promiseConnectDeviceURI(SyncHelpers.getEntryPoint())
      .then(connectURI => {
        SyncHelpers.connectAnotherDeviceHref = connectURI;
        emitChange();
      });
  },
});

// Import section

Preferences.addSetting({
  id: "data-migration",
  visible: () =>
    !Services.policies || Services.policies.isAllowed("profileImport"),
  onUserClick() {
    window.gMainPane.showMigrationWizardDialog();
  },
});

// Profiles section

Preferences.addSetting({
  id: "profilesPane",
  onUserClick(e) {
    e.preventDefault();
    window.gotoPref("paneProfiles");
  },
});
Preferences.addSetting({
  id: "profilesSettings",
  visible() {
    return lazy.SelectableProfileService.isEnabled;
  },
  onUserClick: e => {
    e.preventDefault();
    window.gotoPref("profiles");
  },
});
Preferences.addSetting({
  id: "manageProfiles",
  onUserClick: e => {
    e.preventDefault();
    // Using the existing function for now, since privacy.js also calls it
    window.gMainPane.manageProfiles();
  },
});
Preferences.addSetting({
  id: "copyProfile",
  deps: ["copyProfileSelect"],
  disabled: ({ copyProfileSelect }) => !copyProfileSelect.value,
  onUserClick: (e, { copyProfileSelect }) => {
    e.preventDefault();
    lazy.SelectableProfileService.getProfile(copyProfileSelect.value).then(
      profile => {
        profile?.copyProfile();
        copyProfileSelect.config.set("");
      }
    );
  },
});
Preferences.addSetting({
  id: "copyProfileBox",
  visible: () => lazy.SelectableProfileService.initialized,
});
Preferences.addSetting({
  id: "copyProfileError",
  _hasError: false,
  setup(emitChange) {
    this.emitChange = emitChange;
  },
  visible() {
    return this._hasError;
  },
  setError(value) {
    this._hasError = !!value;
    this.emitChange();
  },
});
Preferences.addSetting(
  class ProfileList extends Preferences.AsyncSetting {
    static id = "profileList";
    static PROFILE_UPDATED_OBS = "sps-profiles-updated";
    setup() {
      Services.obs.addObserver(
        this.emitChange,
        ProfileList.PROFILE_UPDATED_OBS
      );
      return () => {
        Services.obs.removeObserver(
          this.emitChange,
          ProfileList.PROFILE_UPDATED_OBS
        );
      };
    }

    async get() {
      let profiles = await lazy.SelectableProfileService.getAllProfiles();
      return profiles;
    }
  }
);
Preferences.addSetting({
  id: "copyProfileSelect",
  deps: ["profileList"],
  _selectedProfile: null,
  setup(emitChange) {
    this.emitChange = emitChange;
    document.l10n
      .formatValue("preferences-copy-profile-select")
      .then(result => (this.placeholderString = result));
  },
  get() {
    return this._selectedProfile;
  },
  set(inputVal) {
    this._selectedProfile = inputVal;
    this.emitChange();
  },
  getControlConfig(config, { profileList }) {
    config.options = profileList.value.map(profile => {
      return { controlAttrs: { label: profile.name }, value: profile.id };
    });

    // Put the placeholder at the front of the list.
    config.options.unshift({
      controlAttrs: { label: this.placeholderString },
      value: "",
    });

    return config;
  },
});
Preferences.addSetting({
  id: "copyProfileHeader",
  visible: () => lazy.SelectableProfileService.initialized,
});

// Backup section

Preferences.addSetting({
  id: "backupSettings",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "backup-service-status-updated");
    return () =>
      Services.obs.removeObserver(emitChange, "backup-service-status-updated");
  },
  visible: () => {
    let bs = lazy.BackupService.init();
    return bs.archiveEnabledStatus.enabled || bs.restoreEnabledStatus.enabled;
  },
});

let accountsEnabled = Services.prefs.getBoolPref("identity.fxaccounts.enabled");

SettingGroupManager.registerGroups({
  defaultBrowserSync: window.createDefaultBrowserConfig({
    includeIsDefaultPane: false,
    inProgress: true,
    hiddenFromSearch: true,
  }),
  account: {
    inProgress: true,
    l10nId: "account-group-label2",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/preferences/mozilla-logo.svg",
    hidden: !accountsEnabled,
    items: [
      {
        id: "noFxaAccountGroup",
        control: "moz-box-group",
        items: [
          {
            id: "noFxaAccount",
            control: "placeholder-message",
            l10nId: "account-placeholder2",
          },
          {
            id: "noFxaSignIn",
            control: "moz-box-link",
            l10nId: "sync-signedout-account-short",
          },
        ],
      },
      {
        id: "fxaSignedInGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginVerified",
            control: "moz-box-item",
            l10nId: "sync-account-signed-in",
            l10nArgs: { email: "" },
            iconSrc: "chrome://browser/skin/fxa/avatar-color.svg",
            controlAttrs: {
              layout: "large-icon",
            },
          },
          {
            id: "verifiedManage",
            control: "moz-box-link",
            l10nId: "sync-manage-account2",
            controlAttrs: {
              href: "https://accounts.firefox.com/settings",
            },
          },
          {
            id: "fxaUnlinkButton",
            control: "moz-box-button",
            l10nId: "sync-sign-out2",
          },
        ],
      },
      {
        id: "fxaUnverifiedGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginUnverified",
            control: "placeholder-message",
            l10nId: "sync-signedin-unverified2",
            l10nArgs: { email: "" },
          },
          {
            id: "verifyFxaAccount",
            control: "moz-box-link",
            l10nId: "sync-verify-account",
          },
          {
            id: "unverifiedUnlinkFxaAccount",
            control: "moz-box-button",
            l10nId: "sync-remove-account",
          },
        ],
      },
      {
        id: "fxaLoginRejectedGroup",
        control: "moz-box-group",
        items: [
          {
            id: "fxaLoginRejected",
            control: "placeholder-message",
            l10nId: "sync-signedin-login-failure2",
            l10nArgs: { email: "" },
          },
          {
            id: "rejectReSignIn",
            control: "moz-box-link",
            l10nId: "sync-sign-in",
          },
          {
            id: "rejectUnlinkFxaAccount",
            control: "moz-box-button",
            l10nId: "sync-remove-account",
          },
        ],
      },
    ],
  },
  sync: {
    inProgress: true,
    l10nId: "sync-group-label",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/sync.svg",
    hidden: !accountsEnabled,
    items: [
      {
        id: "syncNoFxaSignIn",
        l10nId: "sync-signedout-account-signin-4",
        control: "moz-box-link",
        iconSrc: "chrome://global/skin/icons/warning.svg",
        controlAttrs: {
          id: "noFxaSignIn",
        },
      },
      {
        id: "syncConfigured",
        control: "moz-box-group",
        items: [
          {
            id: "syncStatus",
            l10nId: "prefs-syncing-on-2",
            control: "moz-box-item",
            iconSrc: "chrome://global/skin/icons/check-filled.svg",
            items: [
              {
                id: "syncNow",
                control: "moz-button",
                l10nId: "prefs-sync-now-button-2",
                slot: "actions",
              },
              {
                id: "syncing",
                control: "moz-button",
                l10nId: "prefs-syncing-button-2",
                slot: "actions",
              },
            ],
          },
          {
            id: "syncEnginesList",
            control: "sync-engines-list",
          },
          {
            id: "syncChangeOptions",
            control: "moz-box-button",
            l10nId: "sync-manage-options-2",
          },
          {
            id: "syncDisconnect",
            control: "moz-box-button",
            l10nId: "settings-sync-disconnect-button",
          },
        ],
      },
      {
        id: "syncNotConfigured",
        l10nId: "prefs-syncing-off-2",
        control: "moz-box-item",
        iconSrc: "chrome://global/skin/icons/warning.svg",
        items: [
          {
            id: "syncSetup",
            control: "moz-button",
            l10nId: "prefs-sync-turn-on-syncing-2",
            slot: "actions",
          },
        ],
      },
      {
        id: "fxaDeviceNameSection",
        l10nId: "sync-device-name-header-2",
        control: "moz-fieldset",
        controlAttrs: {
          ".headingLevel": 3,
        },
        items: [
          {
            id: "fxaDeviceNameGroup",
            control: "moz-box-group",
            items: [
              {
                id: "fxaDeviceName",
                control: "sync-device-name",
              },
              {
                id: "fxaConnectAnotherDevice",
                l10nId: "sync-connect-another-device-2",
                control: "moz-box-link",
                iconSrc: "chrome://browser/skin/device-phone.svg",
                controlAttrs: {
                  id: "connect-another-device",
                  href: "https://accounts.firefox.com/pair",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  importBrowserData: {
    l10nId: "preferences-data-migration-group",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/import.svg",
    items: [
      {
        id: "data-migration",
        l10nId: "preferences-data-migration-button",
        control: "moz-box-button",
      },
    ],
  },
  profilePane: {
    headingLevel: 2,
    id: "browserProfilesGroupPane",
    l10nId: "preferences-profiles-subpane-description",
    supportPage: "profile-management",
    items: [
      {
        id: "manageProfiles",
        control: "moz-box-button",
        l10nId: "preferences-manage-profiles-button",
      },
      {
        id: "copyProfileHeader",
        l10nId: "preferences-copy-profile-header",
        headingLevel: 2,
        supportPage: "profile-management",
        control: "moz-fieldset",
        items: [
          {
            id: "copyProfileBox",
            l10nId: "preferences-profile-to-copy",
            control: "moz-box-item",
            items: [
              {
                id: "copyProfileSelect",
                control: "moz-select",
                slot: "actions",
              },
              {
                id: "copyProfile",
                l10nId: "preferences-copy-profile-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "primary",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  profiles: {
    id: "profilesGroup",
    l10nId: "preferences-profiles-section-header",
    headingLevel: 2,
    supportPage: "profile-management",
    items: [
      {
        id: "profilesSettings",
        control: "moz-box-button",
        l10nId: "preferences-profiles-settings-button",
      },
    ],
  },
  backup: {
    l10nId: "settings-data-backup-header2",
    headingLevel: 2,
    supportPage: "firefox-backup",
    iconSrc: "chrome://global/skin/icons/reload.svg",
    items: [
      {
        id: "backupSettings",
        control: "backup-settings",
      },
    ],
  },
});
