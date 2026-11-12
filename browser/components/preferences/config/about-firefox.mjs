/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global UpdateUtils, BackgroundUpdate, gApplicationUpdateService, gSubDialog, UpdateListener, appUpdater, gAppUpdater:writable */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const AUTO_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.auto"].observerTopic;
const BACKGROUND_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.background.enabled"]
    .observerTopic;

const NEWSLETTER_LOCALES = new Set(["en-US", "en-CA", "en-GB", "de", "fr"]);

if (AppConstants.MOZ_UPDATER) {
  Preferences.addAll([
    { id: "app.update.disable_button.showUpdateHistory", type: "bool" },
  ]);

  if (AppConstants.NIGHTLY_BUILD) {
    Preferences.addAll([{ id: "app.update.suppressPrompts", type: "bool" }]);
  }
}

// Firefox updates settings
const UpdatesHelpers = {
  get showUpdatesSettings() {
    // When we're running inside an app package, there's no point in
    // displaying any update content here, and it would get confusing if we
    // did, because our updater is not enabled.
    return (
      AppConstants.MOZ_UPDATER && !Services.sysinfo.getProperty("isPackagedApp")
    );
  },

  get showUpdatesInstallation() {
    let updateDisabled =
      Services.policies && !Services.policies.isAllowed("appUpdate");
    return (
      this.showUpdatesSettings &&
      !(
        updateDisabled ||
        UpdateUtils.appUpdateAutoSettingIsLocked() ||
        gApplicationUpdateService.manualUpdateOnly
      )
    );
  },

  get showBackgroundUpdate() {
    return (
      this.showUpdatesInstallation &&
      AppConstants.MOZ_UPDATE_AGENT &&
      // This UI controls a per-installation pref. It won't necessarily work
      // properly if per-installation prefs aren't supported.
      UpdateUtils.PER_INSTALLATION_PREFS_SUPPORTED &&
      !UpdateUtils.appUpdateSettingIsLocked("app.update.background.enabled")
    );
  },

  /**
   * Displays the history of installed updates.
   */
  showUpdates() {
    gSubDialog.open("chrome://mozapps/content/update/history.xhtml");
  },

  async checkUpdateInProgress() {
    const aus = Cc["@mozilla.org/updates/update-service;1"].getService(
      Ci.nsIApplicationUpdateService
    );
    let um = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    // We don't want to see an idle state just because the updater hasn't
    // initialized yet.
    await aus.init();
    if (aus.currentState == Ci.nsIApplicationUpdateService.STATE_IDLE) {
      return;
    }

    let [title, message, okButton, cancelButton] =
      await document.l10n.formatValues([
        { id: "update-in-progress-title" },
        { id: "update-in-progress-message" },
        { id: "update-in-progress-ok-button" },
        { id: "update-in-progress-cancel-button" },
      ]);

    // Continue is the cancel button which is BUTTON_POS_1 and is set as the
    // default so pressing escape or using a platform standard method of closing
    // the UI will not discard the update.
    let buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1 +
      Ci.nsIPrompt.BUTTON_POS_1_DEFAULT;

    let rv = Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      okButton,
      cancelButton,
      null,
      null,
      {}
    );
    if (rv != 1) {
      await aus.stopDownload();
      await um.cleanupActiveUpdates();
      UpdateListener.clearPendingAndActiveNotifications();
    }
  },

  async reportUpdatePrefWriteError() {
    let [title, message] = await document.l10n.formatValues([
      { id: "update-setting-write-failure-title2" },
      {
        id: "update-setting-write-failure-message2",
        args: { path: UpdateUtils.configFilePath },
      },
    ]);

    // Set up the Ok Button
    let buttonFlags =
      Services.prompt.BUTTON_POS_0 * Services.prompt.BUTTON_TITLE_OK;
    Services.prompt.confirmEx(
      window,
      title,
      message,
      buttonFlags,
      null,
      null,
      null,
      null,
      {}
    );
  },
};

Preferences.addSetting({
  id: "updatesManagedByOS",
  visible: () => !UpdatesHelpers.showUpdatesSettings,
});

Preferences.addSetting({
  id: "updateApp",
  visible: () => UpdatesHelpers.showUpdatesSettings,
});

if (AppConstants.MOZ_UPDATER && typeof appUpdater === "undefined") {
  Services.scriptloader.loadSubScript(
    "chrome://browser/content/aboutDialog-appUpdater.js",
    this
  );
}

Preferences.addSetting(
  /** @type {{ _panel: string, _options: {linkURL?: string, updateVersion?: string, transfer?: string} } & SettingConfig} */ ({
    id: "updateState",
    _panel: "",
    _options: {},
    setup(emitChange) {
      if (!AppConstants.MOZ_UPDATER) {
        return () => {};
      }
      if (gAppUpdater) {
        gAppUpdater.destroy();
      }
      gAppUpdater = new appUpdater({
        selectPanel: /** @param {string} panel */ (panel, options = {}) => {
          this._panel = panel;
          this._options = options;
          emitChange();
        },
      });
      return () => gAppUpdater.destroy();
    },
    get() {
      return this._panel;
    },
    getControlConfig(config) {
      config.controlAttrs = {
        ".linkURL": this._options.linkURL ?? "",
        ".updateVersion": this._options.updateVersion ?? "",
        ".transfer": this._options.transfer ?? "",
      };
      return config;
    },
  })
);

Preferences.addSetting({
  id: "updateAppInfo",
  getControlConfig(config) {
    let version = AppConstants.MOZ_APP_VERSION_DISPLAY;
    let distribution;
    let distributionId;
    let releaseNotesURL = "";

    // Include the build ID if this is an "a#" (nightly) build
    if (/a\d+$/.test(version)) {
      let buildID = Services.appinfo.appBuildID;
      let year = buildID.slice(0, 4);
      let month = buildID.slice(4, 6);
      let day = buildID.slice(6, 8);
      version += ` (${year}-${month}-${day})`;
    }

    // Append "(32-bit)" or "(64-bit)" build architecture to the version number:
    let bundle = Services.strings.createBundle(
      "chrome://browser/locale/browser.properties"
    );
    let archResource = Services.appinfo.is64Bit
      ? "aboutDialog.architecture.sixtyFourBit"
      : "aboutDialog.architecture.thirtyTwoBit";
    let arch = bundle.GetStringFromName(archResource);
    version += ` (${arch})`;

    // Show a release notes link if we have a URL.
    let relNotesPrefType = Services.prefs.getPrefType("app.releaseNotesURL");
    if (relNotesPrefType != Services.prefs.PREF_INVALID) {
      let relNotesURL = Services.urlFormatter.formatURLPref(
        "app.releaseNotesURL"
      );
      if (relNotesURL != "about:blank") {
        releaseNotesURL = relNotesURL;
      }
    }

    let defaults = Services.prefs.getDefaultBranch(null);
    let distroId = defaults.getCharPref("distribution.id", "");
    let distroAbout = defaults.getStringPref("distribution.about", "");
    // Only show distribution info when there is about text. An id-only
    // distribution is used for attribution and is shown in about:support.
    if (distroId && distroAbout) {
      let distroString = distroId;

      let distroVersion = defaults.getCharPref("distribution.version", "");
      if (distroVersion) {
        distroString += " - " + distroVersion;
      }

      distributionId = distroString;
      distribution = distroAbout;
    }

    config.controlAttrs = {
      ".version": version,
      ".distribution": distribution,
      ".distributionId": distributionId,
      ".releaseNotesURL": releaseNotesURL,
    };

    return config;
  },
});

Preferences.addSetting({
  id: "disableShowUpdateHistory",
  pref: AppConstants.MOZ_UPDATER
    ? "app.update.disable_button.showUpdateHistory"
    : undefined,
});
Preferences.addSetting({
  id: "showUpdateHistory",
  deps: ["disableShowUpdateHistory"],
  disabled: deps => deps.disableShowUpdateHistory.locked,
  onUserClick: () => UpdatesHelpers.showUpdates(),
});

Preferences.addSetting({
  id: "installationFieldset",
  visible: () => UpdatesHelpers.showUpdatesInstallation,
});

Preferences.addSetting({
  id: "updateSettingCrossUserWarning",
  visible: () => {
    // On Windows, the Application Update setting is an installation-
    // specific preference, not a profile-specific one. Show a warning to
    // inform users of this.
    return AppConstants.platform == "win";
  },
});

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "updateRadioGroup";

    defaultDisabled = true;

    /** @type {boolean | null} */
    _pendingValue = null;

    _minUpdatePrefDisableTime = 1000;

    get pendingValue() {
      return this._pendingValue;
    }

    set pendingValue(val) {
      this._pendingValue = val;
      this.emitChange();
    }

    async get() {
      if (this._pendingValue !== null) {
        return this._pendingValue;
      }

      let value = await UpdateUtils.getAppUpdateAutoEnabled();
      return value;
    }

    /**
     * @param {boolean} value - radio group value
     */
    async set(value) {
      //Disable radio group while new value is getting set
      this.pendingValue = value;
      this._disableTimeOverPromise = new Promise(r =>
        setTimeout(r, this._minUpdatePrefDisableTime)
      );

      try {
        await UpdateUtils.setAppUpdateAutoEnabled(value);

        // Wait for a second to prevent the disable/enable causing the
        // UI text to flicker.
        await this._disableTimeOverPromise;
      } catch (error) {
        console.error(error);
        UpdatesHelpers.reportUpdatePrefWriteError();
      } finally {
        this.pendingValue = null;
      }

      // If the value was changed to false the user should be given the option
      // to discard an update if there is one.
      if (!value) {
        await UpdatesHelpers.checkUpdateInProgress();
      }
    }

    setup() {
      Services.obs.addObserver(this.emitChange, AUTO_UPDATE_CHANGED_TOPIC);
      return () =>
        Services.obs.removeObserver(this.emitChange, AUTO_UPDATE_CHANGED_TOPIC);
    }

    async disabled() {
      return this.pendingValue !== null;
    }
  }
);

Preferences.addSetting(
  class extends Preferences.AsyncSetting {
    static id = "backgroundUpdate";

    defaultDisabled = true;

    prefName = "app.update.background.enabled";

    /** @type {Promise<void>} */
    _transitionPerformed;

    /** @type {boolean | null} */
    _pendingValue = null;

    /** @type {Setting | null} */
    _updateRadioSetting = null;

    defaultVisible = UpdatesHelpers.showBackgroundUpdate;

    async get() {
      if (this._pendingValue !== null) {
        return this._pendingValue;
      }

      // Ensure the experiment-to-rollout transition has completed before
      // reading the pref, as it may change the effective value.
      await this._transitionPerformed;

      // If auto updates are turned off, background updates checkbox should be unchecked
      let updateAutoEnabled = this._updateRadioSetting.value;
      if (!updateAutoEnabled) {
        return updateAutoEnabled;
      }

      let checked = await UpdateUtils.readUpdateConfigSetting(this.prefName);
      return checked;
    }

    /**
     * @param {boolean} checked - whether background updates are enabled
     */
    async set(checked) {
      //Disable checkbox while new value is getting set
      this._pendingValue = checked;
      this.emitChange();

      try {
        await UpdateUtils.writeUpdateConfigSetting(this.prefName, checked);
      } catch (error) {
        console.error(error);
        UpdatesHelpers.reportUpdatePrefWriteError();
      } finally {
        this._pendingValue = null;
        this.emitChange();
      }
    }

    async visible() {
      return UpdatesHelpers.showBackgroundUpdate;
    }

    async disabled() {
      return this._pendingValue !== null;
    }

    setup() {
      if (UpdatesHelpers.showBackgroundUpdate) {
        this._transitionPerformed =
          BackgroundUpdate.ensureExperimentToRolloutTransitionPerformed();
      }
      Services.obs.addObserver(
        this.emitChange,
        BACKGROUND_UPDATE_CHANGED_TOPIC
      );
      this._updateRadioSetting = Preferences.getSetting("updateRadioGroup");
      this._updateRadioSetting.on("change", this.emitChange);
      return () => {
        Services.obs.removeObserver(
          this.emitChange,
          BACKGROUND_UPDATE_CHANGED_TOPIC
        );
        this._updateRadioSetting.off("change", this.emitChange);
      };
    }
  }
);

Preferences.addSetting({
  id: "showUpdatePromptsHeader",
  visible: () =>
    UpdatesHelpers.showUpdatesSettings && AppConstants.NIGHTLY_BUILD,
});

Preferences.addSetting({
  id: "showUpdatePrompts",
  pref:
    AppConstants.MOZ_UPDATER && AppConstants.NIGHTLY_BUILD
      ? "app.update.suppressPrompts"
      : undefined,
});

// Firefox support settings
Preferences.addSetting({
  id: "supportLinksGroup",
});
Preferences.addSetting({
  id: "supportGetHelp",
});
Preferences.addSetting({
  id: "supportShareIdeas",
});

// Firefox newsletter settings
Preferences.addSetting({
  id: "newsletterSignUp",
  visible: () => NEWSLETTER_LOCALES.has(Services.locale.appLocaleAsBCP47),
});

SettingGroupManager.registerGroups({
  updates: {
    l10nId: "update-application-heading",
    iconSrc: "chrome://browser/skin/update-badge.svg",
    subcategory: "update-state",
    headingLevel: 2,
    items: [
      {
        id: "updatesManagedByOS",
        l10nId: "update-application-updates-managed-by-os",
        control: "moz-message-bar",
        controlAttrs: {
          role: "status",
        },
      },
      {
        id: "updateApp",
        control: "moz-box-group",
        items: [
          {
            id: "updateState",
            control: "update-state",
          },
          {
            id: "updateAppInfo",
            control: "update-information",
          },
          {
            id: "showUpdateHistory",
            l10nId: "update-history-2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids": "history-title,history-intro",
            },
          },
        ],
      },
      {
        id: "installationFieldset",
        control: "moz-fieldset",
        l10nId: "update-application-installation",
        items: [
          {
            id: "updateSettingCrossUserWarning",
            control: "moz-message-bar",
            l10nId: "update-application-warning-cross-user-setting-2",
            controlAttrs: {
              role: "status",
            },
          },
          {
            id: "updateRadioGroup",
            control: "moz-radio-group",
            l10nId: "update-application-radio-group",
            options: [
              {
                id: "autoDesktop",
                value: true,
                l10nId: "update-application-auto-2",
                items: [
                  {
                    id: "backgroundUpdate",
                    l10nId: "update-application-background-enabled",
                  },
                ],
              },
              {
                id: "manualDesktop",
                value: false,
                l10nId: "update-application-check-choose-2",
              },
            ],
          },
        ],
      },
      {
        id: "showUpdatePromptsHeader",
        control: "moz-fieldset",
        l10nId: "update-application-suppress-prompts-heading",
        items: [
          {
            id: "showUpdatePrompts",
            l10nId: "update-application-suppress-prompts-2",
          },
        ],
      },
    ],
  },
  support: {
    inProgress: true,
    l10nId: "support-application-heading",
    iconSrc: "chrome://global/skin/icons/help.svg",
    headingLevel: 2,
    items: [
      {
        id: "supportLinksGroup",
        control: "moz-box-group",
        items: [
          {
            id: "supportGetHelp",
            l10nId: "support-get-help",
            control: "moz-box-link",
            supportPage: "preferences",
          },
          {
            id: "supportShareIdeas",
            l10nId: "support-share-ideas",
            control: "moz-box-link",
            controlAttrs: {
              href: "https://connect.mozilla.org/",
            },
          },
        ],
      },
    ],
  },
  newsletter: {
    l10nId: "newsletter-settings-heading",
    iconSrc: "chrome://global/skin/icons/newsfeed.svg",
    headingLevel: 2,
    items: [
      {
        id: "newsletterSignUp",
        l10nId: "newsletter-sign-up-link",
        control: "moz-box-link",
        controlAttrs: {
          href: `https://www.firefox.com/newsletter/?${new URLSearchParams({
            utm_source: "firefox-settings",
            utm_campaign: "newsletter-signup",
            utm_medium: "referral",
            utm_content: "about-firefox",
          })}`,
        },
      },
    ],
  },
});
