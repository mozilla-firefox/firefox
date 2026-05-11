/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";
import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";

const XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;

const lazy = XPCOMUtils.declareLazy({
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  AlertsServiceDND: () => {
    try {
      let alertsService = Cc["@mozilla.org/alerts-service;1"]
        .getService(Ci.nsIAlertsService)
        .QueryInterface(Ci.nsIAlertsDoNotDisturb);
      // This will throw if manualDoNotDisturb isn't implemented.
      alertsService.manualDoNotDisturb;
      return alertsService;
    } catch (ex) {
      return undefined;
    }
  },
});

export const PRIVACY_SEGMENTATION_PREF =
  "browser.privacySegmentation.preferences.show";
const BACKUP_ENABLED_ON_PROFILES_PREF_NAME =
  "browser.backup.enabled_on.profiles";
const PREF_UPLOAD_ENABLED = "datareporting.healthreport.uploadEnabled";
const PREF_ADDON_RECOMMENDATIONS_ENABLED = "browser.discovery.enabled";
const PREF_NORMANDY_ENABLED = "app.normandy.enabled";
const PREF_OPT_OUT_STUDIES_ENABLED = "app.shield.optoutstudies.enabled";

Preferences.addAll([
  // Local Network Access
  { id: "network.lna.blocking", type: "bool" },

  // Permissions
  { id: "media.setsinkid.enabled", type: "bool" },

  // Popups
  { id: "dom.disable_open_during_load", type: "bool" },
  { id: "dom.security.framebusting_intervention.enabled", type: "bool" },

  // Add-ons, malware, phishing
  { id: "xpinstall.whitelist.required", type: "bool" },
  { id: PRIVACY_SEGMENTATION_PREF, type: "bool" },
  { id: BACKUP_ENABLED_ON_PROFILES_PREF_NAME, type: "string" },

  // Data Choices tab
  { id: "browser.crashReports.unsubmittedCheck.autoSubmit2", type: "bool" },

  { id: PREF_NORMANDY_ENABLED, type: "bool" },
  { id: "nimbus.rollouts.enabled", type: "bool" },
]);

// Study opt out
if (lazy.AppConstants.MOZ_DATA_REPORTING) {
  Preferences.addAll([
    // Preference instances for prefs that we need to monitor while the page is open.
    { id: PREF_OPT_OUT_STUDIES_ENABLED, type: "bool" },
    { id: PREF_ADDON_RECOMMENDATIONS_ENABLED, type: "bool" },
    { id: PREF_UPLOAD_ENABLED, type: "bool" },
    { id: "datareporting.usage.uploadEnabled", type: "bool" },
    { id: "dom.private-attribution.submission.enabled", type: "bool" },
  ]);
}

/**
 * Displays a dialog for managing permission exceptions for a specific permission type.
 *
 * @param {object} options
 * @param {string} options.permissionType - The type of permission to manage.
 * @param {string} [options.dialogType="site"] - The dialog type: "site" or "permission".
 */
export function showPermissionExceptions({
  permissionType,
  dialogType = "site",
}) {
  if (dialogType === "site") {
    window.gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml",
      { features: "resizable=yes" },
      { permissionType }
    );
  } else {
    window.gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/permissions.xhtml",
      { features: "resizable=yes" },
      {
        blockVisible: false,
        sessionVisible: false,
        allowVisible: true,
        prefilledHost: "",
        permissionType,
      }
    );
  }
}

Preferences.addSetting({
  id: "enabledLNA",
  pref: "network.lna.blocking",
});
Preferences.addSetting({
  id: "enabledSpeakerControl",
  pref: "media.setsinkid.enabled",
});
Preferences.addSetting({
  id: "permissionBox",
});
/**
 * Displays the location exceptions dialog where specific site location
 * preferences can be set.
 */
Preferences.addSetting({
  id: "locationSettingsButton",
  onUserClick: () => showPermissionExceptions({ permissionType: "geo" }),
});
/**
 * Displays the camera exceptions dialog where specific site camera
 * preferences can be set.
 */
Preferences.addSetting({
  id: "cameraSettingsButton",
  onUserClick: () => showPermissionExceptions({ permissionType: "camera" }),
});
/**
 * Displays the loopback network exceptions dialog where specific site loopback network
 * preferences can be set.
 */
Preferences.addSetting({
  id: "loopbackNetworkSettingsButton",
  onUserClick: () =>
    showPermissionExceptions({
      permissionType: "loopback-network",
    }),
  deps: ["enabledLNA"],
  visible: deps => {
    return deps.enabledLNA.value;
  },
});
/**
 * Displays the local network exceptions dialog where specific site local network
 * preferences can be set.
 */
Preferences.addSetting({
  id: "localNetworkSettingsButton",
  onUserClick: () =>
    showPermissionExceptions({ permissionType: "local-network" }),
  deps: ["enabledLNA"],
  visible: deps => {
    return deps.enabledLNA.value;
  },
});
/**
 * Displays the microphone exceptions dialog where specific site microphone
 * preferences can be set.
 */
Preferences.addSetting({
  id: "microphoneSettingsButton",
  onUserClick: () => showPermissionExceptions({ permissionType: "microphone" }),
});
/**
 * Displays the speaker exceptions dialog where specific site speaker
 * preferences can be set.
 */
Preferences.addSetting({
  id: "speakerSettingsButton",
  onUserClick: () => showPermissionExceptions({ permissionType: "speaker" }),
  deps: ["enabledSpeakerControl"],
  visible: ({ enabledSpeakerControl }) => {
    return enabledSpeakerControl.value;
  },
});
/**
 * Displays the notifications exceptions dialog where specific site notification
 * preferences can be set.
 */
Preferences.addSetting({
  id: "notificationSettingsButton",
  onUserClick: () =>
    showPermissionExceptions({
      permissionType: "desktop-notification",
    }),
});
Preferences.addSetting({
  id: "autoplaySettingsButton",
  onUserClick: () =>
    showPermissionExceptions({ permissionType: "autoplay-media" }),
});
/**
 * Displays the XR exceptions dialog where specific site XR
 * preferences can be set.
 */
Preferences.addSetting({
  id: "xrSettingsButton",
  onUserClick: () => showPermissionExceptions({ permissionType: "xr" }),
});
Preferences.addSetting({
  id: "popupPolicy",
  pref: "dom.disable_open_during_load",
});
Preferences.addSetting({
  id: "redirectPolicy",
  pref: "dom.security.framebusting_intervention.enabled",
});
// This button controls both the pop-up and framebusting prefs. They are split
// up for testing reasons, but user-facing, they can only be modified together.
// Thus, we need some special handling here. We only consider the checkbox to be
// checked if both prefs are enabled, otherwise it is unchecked. In the special
// case that one of the prefs is locked, the checkbox should only control the
// other pref.
Preferences.addSetting({
  id: "popupAndRedirectPolicy",
  deps: ["popupPolicy", "redirectPolicy"],
  get: (_val, deps) => {
    if (deps.popupPolicy.locked && !deps.redirectPolicy.locked) {
      return deps.redirectPolicy.value;
    }
    if (!deps.popupPolicy.locked && deps.redirectPolicy.locked) {
      return deps.popupPolicy.value;
    }
    return deps.popupPolicy.value && deps.redirectPolicy.value;
  },
  set: (val, deps) => {
    if (!deps.popupPolicy.locked) {
      deps.popupPolicy.value = val;
    }
    if (!deps.redirectPolicy.locked) {
      deps.redirectPolicy.value = val;
    }
  },
  disabled: ({ popupPolicy, redirectPolicy }) =>
    popupPolicy.locked && redirectPolicy.locked,
});
/**
 * Displays the popup exceptions dialog where specific site popup preferences
 * can be set.
 */
Preferences.addSetting({
  id: "popupAndRedirectPolicyButton",
  deps: ["popupPolicy", "redirectPolicy"],
  onUserClick: () =>
    showPermissionExceptions({
      permissionType: "popup",
      dialogType: "permission",
    }),
  disabled: ({ popupPolicy, redirectPolicy }) =>
    !popupPolicy.value ||
    !redirectPolicy.value ||
    (popupPolicy.locked && redirectPolicy.locked),
});
Preferences.addSetting({
  id: "warnAddonInstall",
  pref: "xpinstall.whitelist.required",
});
/**
 * Displays the exceptions lists for add-on installation warnings.
 */
Preferences.addSetting({
  id: "addonExceptions",
  deps: ["warnAddonInstall"],
  onUserClick: () =>
    showPermissionExceptions({
      permissionType: "install",
      dialogType: "permission",
    }),
  disabled: ({ warnAddonInstall }) => {
    return !warnAddonInstall.value || warnAddonInstall.locked;
  },
});
Preferences.addSetting({
  id: "notificationsDoNotDisturb",
  get: () => {
    return lazy.AlertsServiceDND?.manualDoNotDisturb ?? false;
  },
  set: value => {
    if (lazy.AlertsServiceDND) {
      lazy.AlertsServiceDND.manualDoNotDisturb = value;
    }
  },
  visible: () => {
    return lazy.AlertsServiceDND != undefined;
  },
});

Preferences.addSetting({
  id: "privacySegmentation",
  pref: PRIVACY_SEGMENTATION_PREF,
});
Preferences.addSetting({
  id: "dataCollectionCategory",
  deps: ["privacySegmentation"],
  visible: ({ privacySegmentation }) =>
    lazy.AppConstants.MOZ_DATA_REPORTING || privacySegmentation.value,
});
Preferences.addSetting({
  id: "dataCollectionLink",
  visible: () => {
    const url = Services.urlFormatter.formatURLPref(
      "toolkit.datacollection.infoURL"
    );
    if (url) {
      return true;
    }
    return false;
  },
  getControlConfig(config) {
    // Set up or hides the Privacy notice link with the correct URL for various data collection options
    const url = Services.urlFormatter.formatURLPref(
      "toolkit.datacollection.infoURL"
    );
    return {
      ...config,
      controlAttrs: {
        ...config.controlAttrs,
        href: url,
      },
    };
  },
});
Preferences.addSetting({
  id: "preferencesPrivacyProfiles",
  visible: () => lazy.SelectableProfileService.isEnabled,
});
Preferences.addSetting({
  id: "privacyProfilesLink",
  onUserClick: () => window.gMainPane.manageProfiles(),
});
Preferences.addSetting({
  id: "telemetryContainer",
  deps: ["submitHealthReportBox"],
  visible: deps => {
    if (!lazy.AppConstants.MOZ_DATA_REPORTING) {
      return false;
    }
    return !deps.submitHealthReportBox.value;
  },
});
Preferences.addSetting({
  id: "profilesBackupEnabled",
  pref: BACKUP_ENABLED_ON_PROFILES_PREF_NAME,
});
Preferences.addSetting({
  id: "submitHealthReportBox",
  pref: PREF_UPLOAD_ENABLED,
  getControlConfig(config, _, setting) {
    if (!setting.value) {
      return {
        ...config,
        l10nId: "data-collection-health-report-disabled",
      };
    }
    return {
      ...config,
      l10nId: "data-collection-health-report",
    };
  },
});
Preferences.addSetting({
  id: "addonRecommendationEnabled",
  pref: PREF_ADDON_RECOMMENDATIONS_ENABLED,
  deps: ["submitHealthReportBox"],
  visible: () => lazy.AppConstants.MOZ_DATA_REPORTING,
  get: (value, deps) => {
    return value && deps.submitHealthReportBox.pref.value;
  },
});
Preferences.addSetting({
  id: "normandyEnabled",
  pref: PREF_NORMANDY_ENABLED,
});

Preferences.addSetting({
  id: "optOutStudiesEnabled",
  visible: () => lazy.AppConstants.MOZ_NORMANDY,
  pref: PREF_OPT_OUT_STUDIES_ENABLED,
  deps: ["submitHealthReportBox", "normandyEnabled"],
  disabled: ({ submitHealthReportBox, normandyEnabled }) => {
    /**
    *  The checkbox should be disabled if any of the below are true. This
    prevents the user from changing the value in the box.
    * 2. telemetry upload is disabled
    * 3. Normandy is disabled
    */
    const allowedByPolicy = Services.policies.isAllowed("Shield");
    return (
      !allowedByPolicy || !submitHealthReportBox.value || !normandyEnabled.value
    );
  },
  get: (value, { submitHealthReportBox, normandyEnabled }) => {
    /**
     * The checkbox should match the value of the preference only if all the below are true:
     *
     * 1. the policy allows Shield
     * 2. telemetry upload is enabled
     * 3. Normandy is enabled
     *
     * Otherwise, the checkbox should remain unchecked. This
     * is because in these situations, Shield studies are always disabled, and
     * so showing a checkbox would be confusing.
     */
    const allowedByPolicy = Services.policies.isAllowed("Shield");

    if (
      !allowedByPolicy ||
      !submitHealthReportBox.value ||
      !normandyEnabled.value
    ) {
      return false;
    }
    return value;
  },
});
Preferences.addSetting({
  id: "viewShieldStudies",
});
Preferences.addSetting({
  id: "enableNimbusRollouts",
  pref: "nimbus.rollouts.enabled",
  visible: () =>
    lazy.AppConstants.MOZ_DATA_REPORTING && lazy.AppConstants.MOZ_NORMANDY,
  disabled: () => !Services.policies.isAllowed("NimbusRollouts"),
  get: value => {
    if (!Services.policies.isAllowed("NimbusRollouts")) {
      return false;
    }
    return value;
  },
});
Preferences.addSetting({
  id: "submitUsagePingBox",
  pref: "datareporting.usage.uploadEnabled",
  visible: () => lazy.AppConstants.MOZ_DATA_REPORTING,
});
Preferences.addSetting({
  id: "automaticallySubmitCrashesBox",
  pref: "browser.crashReports.unsubmittedCheck.autoSubmit2",
  visible: () =>
    lazy.AppConstants.MOZ_DATA_REPORTING && lazy.AppConstants.MOZ_CRASHREPORTER,
});
Preferences.addSetting(
  /** @type {{ _originalStateOfDataCollectionPrefs: Map<string, any>} & SettingConfig} */ ({
    id: "backup-multi-profile-warning-message-bar",
    _originalStateOfDataCollectionPrefs: new Map(),
    deps: [
      "addonRecommendationEnabled",
      "optOutStudiesEnabled",
      "submitHealthReportBox",
      "submitUsagePingBox",
      "automaticallySubmitCrashesBox",
      "profilesBackupEnabled",
    ],
    setup(emitChange, dataCollectionPrefDeps) {
      for (let pref in dataCollectionPrefDeps) {
        const value = dataCollectionPrefDeps[pref].value;
        this._originalStateOfDataCollectionPrefs.set(pref, value);
      }
      emitChange();
    },
    visible(dataCollectionPrefDeps) {
      const { currentProfile } = lazy.SelectableProfileService;
      if (!currentProfile) {
        return false;
      }
      let anyPrefChanged = false;
      for (let pref in dataCollectionPrefDeps) {
        if (pref === "profilesBackupEnabled") {
          continue;
        }
        const originalValue =
          this._originalStateOfDataCollectionPrefs.get(pref);
        const updatedValue = dataCollectionPrefDeps[pref].value;
        if (updatedValue !== originalValue) {
          anyPrefChanged = true;
          break;
        }
      }

      const profilesBackupEnabledValue = /** @type {string} */ (
        dataCollectionPrefDeps.profilesBackupEnabled.value
      );
      let profilesEnabledOn = JSON.parse(profilesBackupEnabledValue || "{}");
      let currentId = currentProfile.id;
      let otherProfilesEnabled = Object.keys(profilesEnabledOn).some(
        id => id != currentId
      );
      return otherProfilesEnabled && anyPrefChanged;
    },
  })
);

SettingGroupManager.registerGroups({
  permissions: {
    id: "permissions",
    l10nId: "permissions-header3",
    headingLevel: 2,
    items: [
      {
        id: "permissionBox",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
        items: [
          {
            id: "locationSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-location2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/geo.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-location-window2.title,permissions-site-location-desc,permissions-site-location-disable-label,permissions-site-location-disable-desc",
            },
          },
          {
            id: "cameraSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-camera2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/camera.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-camera-window2.title,permissions-site-camera-desc,permissions-site-camera-disable-label,permissions-site-camera-disable-desc,",
            },
          },
          {
            id: "loopbackNetworkSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-localhost2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/local-host.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-localhost-window.title,permissions-site-localhost-desc,permissions-site-localhost-disable-label,permissions-site-localhost-disable-desc,",
            },
          },
          {
            id: "localNetworkSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-local-network2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/local-network.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-local-network-window.title,permissions-site-local-network-desc,permissions-site-local-network-disable-label,permissions-site-local-network-disable-desc,",
            },
          },
          {
            id: "microphoneSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-microphone2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/microphone.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-microphone-window2.title,permissions-site-microphone-desc,permissions-site-microphone-disable-label,permissions-site-microphone-disable-desc,",
            },
          },
          {
            id: "speakerSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-speaker2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/speaker.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-speaker-window.title,permissions-site-speaker-desc,",
            },
          },
          {
            id: "notificationSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-notification2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/desktop-notification.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-notification-window2.title,permissions-site-notification-desc,permissions-site-notification-disable-label,permissions-site-notification-disable-desc,",
            },
          },
          {
            id: "autoplaySettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-autoplay2",
            controlAttrs: {
              ".iconSrc":
                "chrome://browser/skin/notification-icons/autoplay-media.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-autoplay-window2.title,permissions-site-autoplay-desc,",
            },
          },
          {
            id: "xrSettingsButton",
            control: "moz-box-button",
            l10nId: "permissions-xr2",
            controlAttrs: {
              ".iconSrc": "chrome://browser/skin/notification-icons/xr.svg",
              "search-l10n-ids":
                "permissions-remove.label,permissions-remove-all.label,permissions-site-xr-window2.title,permissions-site-xr-desc,permissions-site-xr-disable-label,permissions-site-xr-disable-desc,",
            },
          },
        ],
      },
      {
        id: "popupAndRedirectPolicy",
        l10nId: "permissions-block-popups2",
        subcategory: "permissions-block-popups",
        items: [
          {
            id: "popupAndRedirectPolicyButton",
            l10nId: "permissions-block-popups-exceptions-button4",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "permissions-address,permissions-exceptions-popup-window3.title,permissions-exceptions-popup-desc2,permissions-block-popups-exceptions-button4.searchkeywords",
            },
          },
        ],
      },
      {
        id: "warnAddonInstall",
        l10nId: "permissions-addon-install-warning3",
        items: [
          {
            id: "addonExceptions",
            l10nId: "permissions-addon-exceptions2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "permissions-address,permissions-allow.label,permissions-remove.label,permissions-remove-all.label,permissions-exceptions-addons-window2.title,permissions-exceptions-addons-desc",
            },
          },
        ],
      },
      {
        id: "notificationsDoNotDisturb",
        l10nId: "permissions-notification-pause",
      },
    ],
  },
  dataCollection: {
    items: [
      {
        id: "dataCollectionCategory",
        l10nId: "data-collection",
        control: "moz-fieldset",
        iconSrc: "chrome://global/skin/icons/trending.svg",
        controlAttrs: {
          headinglevel: 2,
          "data-l10n-attrs": "searchkeywords",
        },
        items: [
          {
            id: "dataCollectionLink",
            control: "a",
            l10nId: "data-collection-link",
            slot: "support-link",
            controlAttrs: {
              id: "dataCollectionPrivacyNoticeLink",
              target: "_blank",
            },
          },
          {
            id: "preferencesPrivacyProfiles",
            control: "moz-message-bar",
            l10nId: "data-collection-preferences-across-profiles",
            items: [
              {
                id: "privacyProfilesLink",
                control: "a",
                l10nId: "data-collection-profiles-link",
                slot: "support-link",
                controlAttrs: {
                  id: "dataCollectionViewProfiles",
                  target: "_blank",
                  href: "",
                },
              },
            ],
          },
          {
            id: "telemetryContainer",
            control: "moz-message-bar",
            l10nId: "data-collection-health-report-telemetry-disabled",
            supportPage: "telemetry-clientid",
          },
          {
            id: "backup-multi-profile-warning-message-bar",
            control: "moz-message-bar",
            l10nId: "backup-multi-profile-warning-message",
            controlAttrs: {
              dismissable: true,
            },
          },
          {
            id: "submitHealthReportBox",
            supportPage: "technical-and-interaction-data",
            subcategory: "reports",
            items: [
              {
                id: "addonRecommendationEnabled",
                l10nId: "addon-recommendations3",
                supportPage: "personalized-addons",
              },
              {
                id: "optOutStudiesEnabled",
                l10nId: "data-collection-run-studies",
                items: [
                  {
                    id: "viewShieldStudies",
                    control: "moz-box-link",
                    l10nId: "data-collection-studies-link",
                    controlAttrs: {
                      href: "about:studies",
                    },
                  },
                ],
              },
            ],
          },

          {
            id: "enableNimbusRollouts",
            l10nId: "nimbus-rollouts",
            supportPage: "remote-improvements",
          },
          {
            id: "submitUsagePingBox",
            l10nId: "data-collection-usage-ping",
            subcategory: "reports",
            supportPage: "usage-ping-settings",
          },
          {
            id: "automaticallySubmitCrashesBox",
            l10nId: "data-collection-backlogged-crash-reports",
            subcategory: "reports",
            supportPage: "crash-report",
          },
        ],
      },
    ],
  },
});
