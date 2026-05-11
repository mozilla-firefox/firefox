/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;

const lazy = XPCOMUtils.declareLazy({
  AppConstants: "resource://gre/modules/AppConstants.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  LinkPreview: "moz-src:///browser/components/genai/LinkPreview.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  TransientPrefs: "resource:///modules/TransientPrefs.sys.mjs",
});

Preferences.addAll([
  /* Tab preferences
  Preferences:

  browser.link.open_newwindow
      1 opens such links in the most recent window or tab,
      2 opens such links in a new window,
      3 opens such links in a new tab
  browser.link.open_newwindow.override.external
    - this setting overrides `browser.link.open_newwindow` for externally
      opened links.
    - see `nsIBrowserDOMWindow` constants for the meaning of each value.
  browser.tabs.loadInBackground
  - true if display should switch to a new tab which has been opened from a
    link, false if display shouldn't switch
  browser.tabs.warnOnClose
  - true if when closing a window with multiple tabs the user is warned and
    allowed to cancel the action, false to just close the window
  browser.tabs.warnOnOpen
  - true if the user should be warned if he attempts to open a lot of tabs at
    once (e.g. a large folder of bookmarks), false otherwise
  browser.warnOnQuitShortcut
  - true if the user should be warned if they quit using the keyboard shortcut
  browser.taskbar.previews.enable
  - true if tabs are to be shown in the Windows 7 taskbar
  */

  { id: "browser.link.open_newwindow", type: "int" },
  { id: "browser.link.open_newwindow.override.external", type: "int" },
  { id: "browser.tabs.loadInBackground", type: "bool", inverted: true },
  { id: "browser.tabs.warnOnClose", type: "bool" },
  { id: "browser.warnOnQuitShortcut", type: "bool" },
  { id: "browser.tabs.warnOnOpen", type: "bool" },
  { id: "browser.ctrlTab.sortByRecentlyUsed", type: "bool" },
  { id: "browser.tabs.hoverPreview.enabled", type: "bool" },
  { id: "browser.tabs.hoverPreview.showThumbnails", type: "bool" },
  { id: "browser.tabs.dragDrop.createGroup.enabled", type: "bool" },
  { id: "browser.tabs.groups.enabled", type: "bool" },
  { id: "browser.tabs.groups.smart.userEnabled", type: "bool" },
  { id: "browser.tabs.groups.smart.enabled", type: "bool" },
  { id: "privacy.userContext.ui.enabled", type: "bool" },

  { id: "privacy.userContext.enabled", type: "bool" },

  // Picture-in-Picture
  {
    id: "media.videocontrols.picture-in-picture.video-toggle.enabled",
    type: "bool",
  },
  {
    id: "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled",
    type: "bool",
  },

  // DRM content
  { id: "media.eme.enabled", type: "bool" },

  // Performance
  {
    id: "browser.preferences.defaultPerformanceSettings.enabled",
    type: "bool",
  },
  { id: "dom.ipc.processCount", type: "int" },
  { id: "dom.ipc.processCount.web", type: "int" },
  { id: "layers.acceleration.disabled", type: "bool", inverted: true },

  // Link previews
  { id: "browser.ml.linkPreview.enabled", type: "bool" },
  { id: "browser.ml.linkPreview.optin", type: "bool" },
  { id: "browser.ml.linkPreview.longPress", type: "bool" },

  // CFR
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
    type: "bool",
  },
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
    type: "bool",
  },
]);

if (lazy.AppConstants.platform === "win") {
  Preferences.addAll([{ id: "browser.taskbar.previews.enable", type: "bool" }]);
}

let srdEnabled = Services.prefs.getBoolPref(
  "browser.settings-redesign.enabled",
  false
);

if (srdEnabled) {
  Preferences.addAll([
    /* Accessibility
   * accessibility.browsewithcaret
     - true enables keyboard navigation and selection within web pages using a
       visible caret, false uses normal keyboard navigation with no caret
   * accessibility.typeaheadfind
     - when set to true, typing outside text areas and input boxes will
       automatically start searching for what's typed within the current
       document; when set to false, no search action happens */
    { id: "accessibility.browsewithcaret", type: "bool" },
    { id: "accessibility.typeaheadfind", type: "bool" },
  ]);
}

// Tabs settings

// "Opening" tabs settings
Preferences.addSetting({
  id: "tabsOpening",
});
/**
 * browser.link.open_newwindow - int
 *   Determines where links targeting new windows should open.
 *   Values:
 *     1 - Open in the current window or tab.
 *     2 - Open in a new window.
 *     3 - Open in a new tab in the most recent window.
 */
Preferences.addSetting({
  id: "linkTargeting",
  pref: "browser.link.open_newwindow",
  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns |true| if such links should be opened in new tabs
   */
  get: prefVal => {
    return prefVal != 2;
  },
  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns 2 if such links should be opened in new windows,
   *          3 if such links should be opened in new tabs
   */
  set: checked => {
    return checked ? 3 : 2;
  },
});
/**
 * browser.tabs.loadInBackground - bool
 *  True - Whether browser should switch to a new tab opened from a link.
 */
Preferences.addSetting({
  id: "switchToNewTabs",
  pref: "browser.tabs.loadInBackground",
});
Preferences.addSetting({
  id: "openAppLinksNextToActiveTab",
  pref: "browser.link.open_newwindow.override.external",
  /**
   * @returns {boolean}
   *   Whether the "Open links in tabs instead of new windows" settings
   *   checkbox should be checked. Should only be checked if the
   *   `browser.link.open_newwindow.override.external` pref is set to the
   *   value of 7 (nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT).
   */
  get: prefVal => {
    return prefVal == Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT;
  },
  /**
   * This pref has at least 8 valid values but we are offering a checkbox
   * to set one specific value (`7`).
   *
   * @param {boolean} checked
   * @returns {number}
   *   - `7` (`nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT`) if checked
   *   - the default value of
   *     `browser.link.open_newwindow.override.external` if not checked
   */
  set: (checked, _, setting) => {
    return checked
      ? Ci.nsIBrowserDOMWindow.OPEN_NEWTAB_AFTER_CURRENT
      : setting.pref.defaultValue;
  },
  onUserChange: checked => {
    Glean.linkHandling.openNextToActiveTabSettingsEnabled.set(checked);
    Glean.linkHandling.openNextToActiveTabSettingsChange.record({
      checked,
    });
  },
});
/**
 * browser.tabs.warnOnOpen - bool
 *   True - Whether the user should be warned when trying to open a lot of
 *          tabs at once (e.g. a large folder of bookmarks), allowing to
 *          cancel the action.
 */
Preferences.addSetting({
  id: "warnOpenMany",
  pref: "browser.tabs.warnOnOpen",
  // The "opening multiple tabs might slow down Firefox" warning provides
  // an option for not showing this warning again. When the user disables it,
  // we provide checkboxes to re-enable the warning.
  visible: () =>
    lazy.TransientPrefs.prefShouldBeVisible("browser.tabs.warnOnOpen"),
});

// "Interaction" tabs settings
Preferences.addSetting({
  id: "tabsInteraction",
});
Preferences.addSetting({
  id: "ctrlTabRecentlyUsedOrder",
  pref: "browser.ctrlTab.sortByRecentlyUsed",
  onUserClick: () => {
    Services.prefs.clearUserPref("browser.ctrlTab.migrated");
  },
});
Preferences.addSetting({
  id: "tabHoverPreview",
  pref: "browser.tabs.hoverPreview.enabled",
});
Preferences.addSetting({
  id: "tabPreviewShowThumbnails",
  pref: "browser.tabs.hoverPreview.showThumbnails",
  deps: ["tabHoverPreview"],
  visible: ({ tabHoverPreview }) => !!tabHoverPreview.value,
});
Preferences.addSetting({
  id: "tabGroups",
  pref: "browser.tabs.groups.enabled",
});
Preferences.addSetting({
  id: "smartTabGroups",
  pref: "browser.tabs.groups.smart.enabled",
});
Preferences.addSetting({
  id: "tabGroupSuggestions",
  pref: "browser.tabs.groups.smart.userEnabled",
  deps: [
    "tabGroups",
    "smartTabGroups",
    "aiControlDefault",
    "aiControlSmartTabGroups",
  ],
  visible: ({
    smartTabGroups,
    tabGroups,
    aiControlDefault,
    aiControlSmartTabGroups,
  }) => {
    return (
      window.canShowAiFeature(aiControlSmartTabGroups, aiControlDefault) &&
      !!tabGroups.value &&
      !!smartTabGroups.value &&
      Services.locale.appLocaleAsBCP47.startsWith("en")
    );
  },
});
Preferences.addSetting({
  id: "tabGroupDragToCreate",
  pref: "browser.tabs.dragDrop.createGroup.enabled",
});
if (lazy.AppConstants.platform === "win") {
  /**
   * browser.taskbar.previews.enable - bool
   *   True - Tabs are to be shown in Windows 7 taskbar.
   *   False - Only the window is to be shown in Windows 7 taskbar.
   */
  Preferences.addSetting({
    id: "showTabsInTaskbar",
    pref: "browser.taskbar.previews.enable",
    // Functionality for "Show tabs in taskbar" on Windows 7 and up.
    visible: () => {
      try {
        let ver = parseFloat(Services.sysinfo.getProperty("version"));
        return ver >= 6.1;
      } catch (ex) {
        return false;
      }
    },
  });
} else {
  // Not supported unless we're on Windows
  Preferences.addSetting({ id: "showTabsInTaskbar", visible: () => false });
}

// "Containers" tabs settings
Preferences.addSetting({
  id: "privacyUserContextUI",
  pref: "privacy.userContext.ui.enabled",
});
Preferences.addSetting({
  id: "browserContainersbox",
  deps: ["privacyUserContextUI"],
  visible: ({ privacyUserContextUI }) => !!privacyUserContextUI.value,
});
Preferences.addSetting({
  id: "browserContainersCheckbox",
  pref: "privacy.userContext.enabled",
  controllingExtensionInfo: {
    storeId: "privacy.containers",
    l10nId: "extension-controlling-privacy-containers",
  },
  async promptToCloseTabsAndDisable(count, setting) {
    let [title, message, okButton, cancelButton] =
      await document.l10n.formatValues([
        { id: "containers-disable-alert-title" },
        { id: "containers-disable-alert-desc", args: { tabCount: count } },
        { id: "containers-disable-alert-ok-button", args: { tabCount: count } },
        { id: "containers-disable-alert-cancel-button" },
      ]);

    let buttonFlags =
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0 +
      Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1;

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

    // User confirmed - disable containers and close container tabs.
    if (rv == 0) {
      await lazy.ContextualIdentityService.closeContainerTabs();
      setting.pref.value = false;
    }

    // Keep the checkbox checked when the user opts not to close tabs.
    return true;
  },
  set(val, _, setting) {
    // When enabling container tabs, just set the pref value.
    if (val) {
      return val;
    }

    // When disabling container tabs, check if there are container tabs currently
    // open. If there aren't, then proceed with disabling.
    let count = lazy.ContextualIdentityService.countContainerTabs();
    if (count == 0) {
      return false;
    }

    // When disabling container tabs with container tabs currently open show a
    // dialog to determine whether or not the tabs should be closed.
    return this.promptToCloseTabsAndDisable(count, setting);
  },
});
Preferences.addSetting({
  id: "browserContainersSettings",
  deps: ["browserContainersCheckbox"],
  /**
   * Displays container panel for customising and adding containers.
   */
  onUserClick: () => {
    window.gotoPref("containers");
  },
  getControlConfig: config => {
    let searchKeywords = [
      "user-context-personal",
      "user-context-work",
      "user-context-banking",
      "user-context-shopping",
    ]
      .map(lazy.ContextualIdentityService.formatContextLabel)
      .join(" ");
    config.controlAttrs.searchkeywords = searchKeywords;
    return config;
  },
  disabled: ({ browserContainersCheckbox }) => !browserContainersCheckbox.value,
});

// "Closing" tabs settings
Preferences.addSetting({
  id: "tabsClosing",
});
/**
 * browser.tabs.warnOnClose - bool
 *   True - If when closing a window with multiple tabs the user is warned and
 *          allowed to cancel the action, false to just close the window.
 */
Preferences.addSetting({
  id: "warnCloseMultiple",
  pref: "browser.tabs.warnOnClose",
});
/**
 * browser.warnOnQuitShortcut - bool
 *   True - If the keyboard shortcut (Ctrl/Cmd+Q) is pressed, the user should
 *          be warned, false to just quit without prompting.
 */
Preferences.addSetting({
  id: "warnOnQuitKey",
  pref: "browser.warnOnQuitShortcut",
  setup() {
    let quitKeyElement =
      window.browsingContext.topChromeWindow.document.getElementById(
        "key_quitApplication"
      );
    if (quitKeyElement) {
      this.quitKey = lazy.ShortcutUtils.prettifyShortcut(quitKeyElement);
    }
  },
  visible() {
    return lazy.AppConstants.platform !== "win" && this.quitKey;
  },
  getControlConfig(config) {
    return {
      ...config,
      l10nArgs: { quitKey: this.quitKey },
    };
  },
});

// Page navigation settings
Preferences.addSetting({
  id: "useCursorNavigation",
  pref: "accessibility.browsewithcaret",
});

Preferences.addSetting({
  id: "searchStartTyping",
  pref: "accessibility.typeaheadfind",
});

Preferences.addSetting({
  id: "linkPreviewEnabled",
  pref: "browser.ml.linkPreview.enabled",
  deps: ["aiControlDefault", "aiControlLinkPreviews"],
  visible: ({ aiControlDefault, aiControlLinkPreviews }) => {
    return (
      window.canShowAiFeature(aiControlLinkPreviews, aiControlDefault) &&
      // @ts-ignore bug 1996860
      lazy.LinkPreview.canShowPreferences
    );
  },
});
Preferences.addSetting({
  id: "linkPreviewKeyPoints",
  pref: "browser.ml.linkPreview.optin",
  // LinkPreview.canShowKeyPoints depends on the global genai pref.
  // @ts-ignore bug 1996860
  visible: () => lazy.LinkPreview.canShowKeyPoints,
});
Preferences.addSetting({
  id: "linkPreviewLongPress",
  pref: "browser.ml.linkPreview.longPress",
});

// Media settings
Preferences.addSetting({
  id: "pictureInPictureToggleEnabled",
  pref: "media.videocontrols.picture-in-picture.video-toggle.enabled",
  visible: () =>
    Services.prefs.getBoolPref(
      "media.videocontrols.picture-in-picture.enabled"
    ),
  onUserChange(checked) {
    if (!checked) {
      Glean.pictureinpictureSettings.disableSettings.record();
    }
  },
});
Preferences.addSetting({
  id: "pictureInPictureEnableWhenSwitchingTabs",
  pref: "media.videocontrols.picture-in-picture.enable-when-switching-tabs.enabled",
  deps: ["pictureInPictureToggleEnabled"],
  onUserChange(checked) {
    if (checked) {
      Glean.pictureinpictureSettings.enableAutotriggerSettings.record();
    }
  },
});
Preferences.addSetting({
  id: "playDRMContent",
  pref: "media.eme.enabled",
  visible: () => {
    if (!Services.prefs.getBoolPref("browser.eme.ui.enabled", false)) {
      return false;
    }
    if (lazy.AppConstants.platform == "win") {
      try {
        return parseFloat(Services.sysinfo.get("version")) >= 6;
      } catch (ex) {
        return false;
      }
    }
    return true;
  },
});

// Performance settings
Preferences.addSetting({
  id: "contentProcessCount",
  pref: "dom.ipc.processCount",
});
Preferences.addSetting({
  id: "allowHWAccel",
  pref: "layers.acceleration.disabled",
  deps: ["useRecommendedPerformanceSettings"],
  visible({ useRecommendedPerformanceSettings }) {
    return !useRecommendedPerformanceSettings.value;
  },
});
Preferences.addSetting({
  id: "useRecommendedPerformanceSettings",
  pref: "browser.preferences.defaultPerformanceSettings.enabled",
  deps: ["contentProcessCount", "allowHWAccel"],
  get(val, { allowHWAccel, contentProcessCount }) {
    if (
      allowHWAccel.value != allowHWAccel.pref.defaultValue ||
      contentProcessCount.value != contentProcessCount.pref.defaultValue
    ) {
      return false;
    }
    return val;
  },
  set(val, { allowHWAccel, contentProcessCount }) {
    if (val) {
      contentProcessCount.value = contentProcessCount.pref.defaultValue;
      allowHWAccel.value = allowHWAccel.pref.defaultValue;
    }
    return val;
  },
});

// Recommendations settings
Preferences.addSetting({
  id: "cfrRecommendations",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
});
Preferences.addSetting({
  id: "cfrRecommendations-features",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
});

SettingGroupManager.registerGroups({
  tabs: {
    l10nId: "tabs-group-header2",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/tabs.svg",
    items: [
      {
        id: "tabsOpening",
        control: "moz-fieldset",
        l10nId: "tabs-opening-heading",
        headingLevel: 3,
        items: [
          {
            id: "linkTargeting",
            l10nId: "open-new-link-as-tabs",
          },
          {
            id: "switchToNewTabs",
            l10nId: "switch-to-new-tabs-2",
          },
          {
            id: "openAppLinksNextToActiveTab",
            l10nId: "open-external-link-next-to-active-tab",
          },
          {
            id: "warnOpenMany",
            l10nId: "warn-on-open-many-tabs",
          },
        ],
      },
      {
        id: "tabsInteraction",
        control: "moz-fieldset",
        l10nId: "tabs-interaction-heading",
        headingLevel: 3,
        items: [
          {
            id: "ctrlTabRecentlyUsedOrder",
            l10nId: "ctrl-tab-recently-used-order",
          },
          {
            id: "tabPreviewShowThumbnails",
            l10nId: "settings-tabs-show-image-in-preview",
          },
          {
            id: "tabGroupSuggestions",
            l10nId: "settings-tabs-show-group-and-tab-suggestions",
          },
          {
            id: "tabGroupDragToCreate",
            l10nId: "settings-tabs-drag-to-create-tab-groups",
          },
          {
            id: "showTabsInTaskbar",
            l10nId: "show-tabs-in-taskbar",
          },
        ],
      },
      {
        id: "browserContainersbox",
        control: "moz-fieldset",
        l10nId: "tabs-containers-heading",
        headingLevel: 3,
        items: [
          {
            id: "browserContainersCheckbox",
            l10nId: "browser-containers-enabled-2",
            supportPage: "containers",
          },
          {
            id: "browserContainersSettings",
            l10nId: "browser-containers-settings-2",
            control: "moz-box-button",
            controlAttrs: {
              "search-l10n-ids":
                "containers-add-button.label, containers-settings-button.label, containers-remove-button.label, containers-new-tab-check.label",
            },
          },
        ],
      },
      {
        id: "tabsClosing",
        control: "moz-fieldset",
        l10nId: "tabs-closing-heading",
        headingLevel: 3,
        items: [
          {
            id: "warnCloseMultiple",
            l10nId: "ask-on-close-multiple-tabs",
          },
          {
            id: "warnOnQuitKey",
            l10nId: "ask-on-quit-with-key",
          },
        ],
      },
    ],
  },
  pageNavigation: {
    l10nId: "page-navigation-group",
    headingLevel: 2,
    iconSrc: "chrome://global/skin/icons/cursor-arrow.svg",
    items: [
      { id: "useCursorNavigation", l10nId: "browsing-use-cursor-navigation" },
      { id: "searchStartTyping", l10nId: "browsing-search-on-start-typing" },
      {
        id: "linkPreviewEnabled",
        l10nId: "link-preview-settings-enable",
        subcategory: "link-preview",
        items: [
          {
            id: "linkPreviewKeyPoints",
            l10nId: "link-preview-settings-key-points",
          },
          {
            id: "linkPreviewLongPress",
            l10nId: "link-preview-settings-long-press",
          },
        ],
      },
    ],
  },
  media: {
    l10nId: "settings-media-group",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/notification-icons/camera.svg",
    items: [
      {
        id: "pictureInPictureToggleEnabled",
        l10nId: "browsing-picture-in-picture-toggle-enabled-2",
        supportPage: "picture-in-picture",
        items: [
          {
            id: "pictureInPictureEnableWhenSwitchingTabs",
            l10nId: "browsing-picture-in-picture-enable-when-switching-tabs",
          },
        ],
      },
      {
        id: "playDRMContent",
        l10nId: "play-drm-content",
        supportPage: "drm-content",
      },
    ],
  },
  performance: {
    l10nId: "performance-group",
    headingLevel: 2,
    iconSrc: "chrome://global/skin/icons/chevron.svg",
    items: [
      {
        id: "useRecommendedPerformanceSettings",
        l10nId: "performance-use-recommended-settings-checkbox-2",
        supportPage: "performance",
      },
      {
        id: "allowHWAccel",
        l10nId: "performance-allow-hw-accel",
      },
    ],
  },
  recommendations: {
    l10nId: "recommendations-group",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/trending.svg",
    items: [
      {
        id: "cfrRecommendations",
        l10nId: "browsing-cfr-recommendations",
        supportPage: "extensionrecommendations",
        subcategory: "cfraddons",
      },
      {
        id: "cfrRecommendations-features",
        l10nId: "browsing-cfr-features",
        supportPage: "extensionrecommendations",
        subcategory: "cfrfeatures",
      },
    ],
  },
  // Bug 2028609: the following sections are needed to ensure settings keep
  // working in legacy views, and can be removed when the pref is flipped
  drm: {
    l10nId: "drm-group",
    headingLevel: 2,
    subcategory: "drm",
    hidden: srdEnabled,
    items: [
      {
        id: "playDRMContent",
        l10nId: "play-drm-content",
        supportPage: "drm-content",
      },
    ],
  },
  browsing: {
    l10nId: "browsing-group",
    headingLevel: 1,
    hidden: srdEnabled,
    items: [
      {
        id: "pictureInPictureToggleEnabled",
        l10nId: "browsing-picture-in-picture-toggle-enabled-2",
        supportPage: "picture-in-picture",
        items: [
          {
            id: "pictureInPictureEnableWhenSwitchingTabs",
            l10nId: "browsing-picture-in-picture-enable-when-switching-tabs",
          },
        ],
      },
      {
        id: "cfrRecommendations",
        l10nId: "browsing-cfr-recommendations",
        supportPage: "extensionrecommendations",
        subcategory: "cfraddons",
      },
      {
        id: "cfrRecommendations-features",
        l10nId: "browsing-cfr-features",
        supportPage: "extensionrecommendations",
        subcategory: "cfrfeatures",
      },
      {
        id: "linkPreviewEnabled",
        l10nId: "link-preview-settings-enable",
        subcategory: "link-preview",
        items: [
          {
            id: "linkPreviewKeyPoints",
            l10nId: "link-preview-settings-key-points",
          },
          {
            id: "linkPreviewLongPress",
            l10nId: "link-preview-settings-long-press",
          },
        ],
      },
    ],
  },
});
