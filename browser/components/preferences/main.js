/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from extensionControlled.js */
/* import-globals-from preferences.js */
/* import-globals-from /toolkit/mozapps/preferences/fontbuilder.js */
/* import-globals-from /browser/base/content/aboutDialog-appUpdater.js */
/* global MozXULElement */

ChromeUtils.defineESModuleGetters(this, {
  BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs",
  UpdateListener: "resource://gre/modules/UpdateListener.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  WindowsLaunchOnLogin: "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
});

// Constants & Enumeration Values
const TYPE_PDF = "application/pdf";

const PREF_PDFJS_DISABLED = "pdfjs.disabled";

// Pref for when containers is being controlled
const PREF_CONTAINERS_EXTENSION = "privacy.userContext.extension";

// Strings to identify ExtensionSettingsStore overrides
const CONTAINERS_KEY = "privacy.containers";

const PREF_CONTENT_APPEARANCE =
  "layout.css.prefers-color-scheme.content-override";
const FORCED_COLORS_QUERY = matchMedia("(forced-colors)");

const AUTO_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.auto"].observerTopic;
const BACKGROUND_UPDATE_CHANGED_TOPIC =
  UpdateUtils.PER_INSTALLATION_PREFS["app.update.background.enabled"]
    .observerTopic;

const ICON_URL_APP =
  AppConstants.platform == "linux"
    ? "moz-icon://dummy.exe?size=16"
    : "chrome://browser/skin/preferences/application.png";

// For CSS. Can be one of "ask", "save" or "handleInternally". If absent, the icon URL
// was set by us to a custom handler icon and CSS should not try to override it.
const APP_ICON_ATTR_NAME = "appHandlerIcon";

Preferences.addAll([
  // Startup
  { id: "browser.startup.page", type: "int" },
  { id: "browser.privatebrowsing.autostart", type: "bool" },

  // Downloads
  { id: "browser.download.useDownloadDir", type: "bool", inverted: true },
  { id: "browser.download.always_ask_before_handling_new_types", type: "bool" },
  { id: "browser.download.folderList", type: "int" },
  { id: "browser.download.dir", type: "file" },

  /* Tab preferences
  Preferences:

  browser.link.open_newwindow
      1 opens such links in the most recent window or tab,
      2 opens such links in a new window,
      3 opens such links in a new tab
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
  { id: "browser.tabs.loadInBackground", type: "bool", inverted: true },
  { id: "browser.tabs.warnOnClose", type: "bool" },
  { id: "browser.warnOnQuitShortcut", type: "bool" },
  { id: "browser.tabs.warnOnOpen", type: "bool" },
  { id: "browser.ctrlTab.sortByRecentlyUsed", type: "bool" },
  { id: "browser.tabs.hoverPreview.enabled", type: "bool" },
  { id: "browser.tabs.hoverPreview.showThumbnails", type: "bool" },
  { id: "browser.tabs.groups.smart.userEnabled", type: "bool" },

  { id: "sidebar.verticalTabs", type: "bool" },
  { id: "sidebar.revamp", type: "bool" },

  // CFR
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
    type: "bool",
  },
  {
    id: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
    type: "bool",
  },

  // High Contrast
  { id: "browser.display.document_color_use", type: "int" },

  // Fonts
  { id: "font.language.group", type: "wstring" },

  // Languages
  { id: "intl.regional_prefs.use_os_locales", type: "bool" },

  // General tab

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
  { id: "accessibility.blockautorefresh", type: "bool" },

  /* Browsing
   * general.autoScroll
     - when set to true, clicking the scroll wheel on the mouse activates a
       mouse mode where moving the mouse down scrolls the document downward with
       speed correlated with the distance of the cursor from the original
       position at which the click occurred (and likewise with movement upward);
       if false, this behavior is disabled
   * general.smoothScroll
     - set to true to enable finer page scrolling than line-by-line on page-up,
       page-down, and other such page movements */
  { id: "general.autoScroll", type: "bool" },
  { id: "general.smoothScroll", type: "bool" },
  { id: "widget.gtk.overlay-scrollbars.enabled", type: "bool", inverted: true },
  { id: "layout.css.always_underline_links", type: "bool" },
  { id: "layout.spellcheckDefault", type: "int" },
  { id: "accessibility.tabfocus", type: "int" },

  {
    id: "browser.preferences.defaultPerformanceSettings.enabled",
    type: "bool",
  },
  { id: "dom.ipc.processCount", type: "int" },
  { id: "dom.ipc.processCount.web", type: "int" },
  { id: "layers.acceleration.disabled", type: "bool", inverted: true },

  // Files and Applications
  { id: "pref.downloads.disable_button.edit_actions", type: "bool" },

  // DRM content
  { id: "media.eme.enabled", type: "bool" },

  // Update
  { id: "browser.preferences.advanced.selectedTabIndex", type: "int" },
  { id: "browser.search.update", type: "bool" },

  { id: "privacy.userContext.enabled", type: "bool" },
  {
    id: "privacy.userContext.newTabContainerOnLeftClick.enabled",
    type: "bool",
  },

  // Picture-in-Picture
  {
    id: "media.videocontrols.picture-in-picture.video-toggle.enabled",
    type: "bool",
  },

  // Media
  { id: "media.hardwaremediakeys.enabled", type: "bool" },
]);

if (AppConstants.HAVE_SHELL_SERVICE) {
  Preferences.addAll([
    { id: "browser.shell.checkDefaultBrowser", type: "bool" },
    { id: "pref.general.disable_button.default_browser", type: "bool" },
  ]);
}

if (AppConstants.platform === "win") {
  Preferences.addAll([
    { id: "browser.taskbar.previews.enable", type: "bool" },
    { id: "ui.osk.enabled", type: "bool" },
  ]);
}

if (AppConstants.MOZ_UPDATER) {
  Preferences.addAll([
    { id: "app.update.disable_button.showUpdateHistory", type: "bool" },
  ]);

  if (AppConstants.NIGHTLY_BUILD) {
    Preferences.addAll([{ id: "app.update.suppressPrompts", type: "bool" }]);
  }
}

Preferences.addSetting({
  id: "useAutoScroll",
  pref: "general.autoScroll",
});
Preferences.addSetting({
  id: "useSmoothScrolling",
  pref: "general.smoothScroll",
});
Preferences.addSetting({
  id: "useOverlayScrollbars",
  pref: "widget.gtk.overlay-scrollbars.enabled",
  visible: () => AppConstants.MOZ_WIDGET_GTK,
});
Preferences.addSetting({
  id: "useOnScreenKeyboard",
  pref: "ui.osk.enabled",
  visible: () => AppConstants.platform == "win",
});
Preferences.addSetting({
  id: "useCursorNavigation",
  pref: "accessibility.browsewithcaret",
});
Preferences.addSetting({
  id: "useFullKeyboardNavigation",
  pref: "accessibility.tabfocus",
  visible: () => AppConstants.platform == "macosx",
  /**
   * Returns true if any full keyboard nav is enabled and false otherwise, caching
   * the current value to enable proper pref restoration if the checkbox is
   * never changed.
   *
   * accessibility.tabfocus
   * - an integer controlling the focusability of:
   *     1  text controls
   *     2  form elements
   *     4  links
   *     7  all of the above
   */
  get(prefVal) {
    this._storedFullKeyboardNavigation = prefVal;
    return prefVal == 7;
  },
  /**
   * Returns the value of the full keyboard nav preference represented by UI,
   * preserving the preference's "hidden" value if the preference is
   * unchanged and represents a value not strictly allowed in UI.
   */
  set(checked) {
    if (checked) {
      return 7;
    }
    if (this._storedFullKeyboardNavigation != 7) {
      // 1/2/4 values set via about:config should persist
      return this._storedFullKeyboardNavigation;
    }
    // When the checkbox is unchecked, default to just text controls.
    return 1;
  },
});
Preferences.addSetting({
  id: "alwaysUnderlineLinks",
  pref: "layout.css.always_underline_links",
});
Preferences.addSetting({
  id: "searchStartTyping",
  pref: "accessibility.typeaheadfind",
});
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
  id: "mediaControlToggleEnabled",
  pref: "media.hardwaremediakeys.enabled",
  // For media control toggle button, we support it on Windows, macOS and
  // gtk-based Linux.
  visible: () =>
    AppConstants.platform == "win" ||
    AppConstants.platform == "macosx" ||
    AppConstants.MOZ_WIDGET_GTK,
});
Preferences.addSetting({
  id: "cfrRecommendations",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
});
Preferences.addSetting({
  id: "cfrRecommendations-features",
  pref: "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
});

let SETTINGS_CONFIG = {
  browsing: {
    items: [
      {
        id: "useAutoScroll",
        l10nId: "browsing-use-autoscroll",
      },
      {
        id: "useSmoothScrolling",
        l10nId: "browsing-use-smooth-scrolling",
      },
      {
        id: "useOverlayScrollbars",
        l10nId: "browsing-gtk-use-non-overlay-scrollbars",
      },
      {
        id: "useOnScreenKeyboard",
        l10nId: "browsing-use-onscreen-keyboard",
      },
      {
        id: "useCursorNavigation",
        l10nId: "browsing-use-cursor-navigation",
      },
      {
        id: "useFullKeyboardNavigation",
        l10nId: "browsing-use-full-keyboard-navigation",
      },
      {
        id: "alwaysUnderlineLinks",
        l10nId: "browsing-always-underline-links",
      },
      {
        id: "searchStartTyping",
        l10nId: "browsing-search-on-start-typing",
      },
      {
        id: "pictureInPictureToggleEnabled",
        l10nId: "browsing-picture-in-picture-toggle-enabled",
        supportPage: "picture-in-picture",
      },
      {
        id: "mediaControlToggleEnabled",
        l10nId: "browsing-media-control",
        supportPage: "media-keyboard-control",
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
    ],
  },
};

function initSettingGroup(id) {
  let group = document.querySelector(`setting-group[groupid=${id}]`);
  if (group && SETTINGS_CONFIG[id]) {
    group.config = SETTINGS_CONFIG[id];
    group.getSetting = Preferences.getSetting.bind(Preferences);
  }
}

ChromeUtils.defineLazyGetter(this, "gIsPackagedApp", () => {
  return Services.sysinfo.getProperty("isPackagedApp");
});

// A promise that resolves when the list of application handlers is loaded.
// We store this in a global so tests can await it.
var promiseLoadHandlersList;

// Load the preferences string bundle for other locales with fallbacks.
function getBundleForLocales(newLocales) {
  let locales = Array.from(
    new Set([
      ...newLocales,
      ...Services.locale.requestedLocales,
      Services.locale.lastFallbackLocale,
    ])
  );
  return new Localization(
    ["browser/preferences/preferences.ftl", "branding/brand.ftl"],
    false,
    undefined,
    locales
  );
}

var gNodeToObjectMap = new WeakMap();

var gMainPane = {
  // The set of types the app knows how to handle.  A hash of HandlerInfoWrapper
  // objects, indexed by type.
  _handledTypes: {},

  // The list of types we can show, sorted by the sort column/direction.
  // An array of HandlerInfoWrapper objects.  We build this list when we first
  // load the data and then rebuild it when users change a pref that affects
  // what types we can show or change the sort column/direction.
  // Note: this isn't necessarily the list of types we *will* show; if the user
  // provides a filter string, we'll only show the subset of types in this list
  // that match that string.
  _visibleTypes: [],

  // browser.startup.page values
  STARTUP_PREF_BLANK: 0,
  STARTUP_PREF_HOMEPAGE: 1,
  STARTUP_PREF_RESTORE_SESSION: 3,

  // Convenience & Performance Shortcuts

  get _list() {
    delete this._list;
    return (this._list = document.getElementById("handlersView"));
  },

  get _filter() {
    delete this._filter;
    return (this._filter = document.getElementById("filter"));
  },

  _backoffIndex: 0,

  /**
   * Initialization of gMainPane.
   */
  init() {
    function setEventListener(aId, aEventType, aCallback) {
      document
        .getElementById(aId)
        .addEventListener(aEventType, aCallback.bind(gMainPane));
    }

    if (AppConstants.HAVE_SHELL_SERVICE) {
      this.updateSetDefaultBrowser();
      let win = Services.wm.getMostRecentWindow("navigator:browser");

      // Exponential backoff mechanism will delay the polling times if user doesn't
      // trigger SetDefaultBrowser for a long time.
      let backoffTimes = [
        1000, 1000, 1000, 1000, 2000, 2000, 2000, 5000, 5000, 10000,
      ];

      let pollForDefaultBrowser = () => {
        let uri = win.gBrowser.currentURI.spec;

        if (
          (uri == "about:preferences" ||
            uri == "about:preferences#general" ||
            uri == "about:settings" ||
            uri == "about:settings#general") &&
          document.visibilityState == "visible"
        ) {
          this.updateSetDefaultBrowser();
        }

        // approximately a "requestIdleInterval"
        window.setTimeout(
          () => {
            window.requestIdleCallback(pollForDefaultBrowser);
          },
          backoffTimes[
            this._backoffIndex + 1 < backoffTimes.length
              ? this._backoffIndex++
              : backoffTimes.length - 1
          ]
        );
      };

      window.setTimeout(() => {
        window.requestIdleCallback(pollForDefaultBrowser);
      }, backoffTimes[this._backoffIndex]);
    }

    this.initBrowserContainers();
    this.buildContentProcessCountMenuList();

    this.updateDefaultPerformanceSettingsPref();

    let defaultPerformancePref = Preferences.get(
      "browser.preferences.defaultPerformanceSettings.enabled"
    );
    defaultPerformancePref.on("change", () => {
      this.updatePerformanceSettingsBox({ duringChangeEvent: true });
    });
    this.updatePerformanceSettingsBox({ duringChangeEvent: false });
    this.displayUseSystemLocale();
    this.updateProxySettingsUI();
    initializeProxyUI(gMainPane);

    if (Services.prefs.getBoolPref("intl.multilingual.enabled")) {
      gMainPane.initPrimaryBrowserLanguageUI();
    }

    // We call `initDefaultZoomValues` to set and unhide the
    // default zoom preferences menu, and to establish a
    // listener for future menu changes.
    gMainPane.initDefaultZoomValues();

    gMainPane.initTranslations();

    initSettingGroup("browsing");

    if (AppConstants.platform == "win") {
      // Functionality for "Show tabs in taskbar" on Windows 7 and up.
      try {
        let ver = parseFloat(Services.sysinfo.getProperty("version"));
        let showTabsInTaskbar = document.getElementById("showTabsInTaskbar");
        showTabsInTaskbar.hidden = ver < 6.1;
      } catch (ex) {}
    }

    let thumbsCheckbox = document.getElementById("tabPreviewShowThumbnails");
    let cardPreviewEnabledPref = Preferences.get(
      "browser.tabs.hoverPreview.enabled"
    );
    let maybeShowThumbsCheckbox = () =>
      (thumbsCheckbox.hidden = !cardPreviewEnabledPref.value);
    cardPreviewEnabledPref.on("change", maybeShowThumbsCheckbox);
    maybeShowThumbsCheckbox();

    const tabGroupSuggestionsCheckbox = document.getElementById(
      "tabGroupSuggestions"
    );
    const smartTabGroupFeatureEnabled = Services.prefs.getBoolPref(
      "browser.tabs.groups.smart.enabled",
      false
    );
    tabGroupSuggestionsCheckbox.hidden = !smartTabGroupFeatureEnabled;

    // The "opening multiple tabs might slow down Firefox" warning provides
    // an option for not showing this warning again. When the user disables it,
    // we provide checkboxes to re-enable the warning.
    if (!TransientPrefs.prefShouldBeVisible("browser.tabs.warnOnOpen")) {
      document.getElementById("warnOpenMany").hidden = true;
    }

    if (AppConstants.platform != "win") {
      let quitKeyElement =
        window.browsingContext.topChromeWindow.document.getElementById(
          "key_quitApplication"
        );
      if (quitKeyElement) {
        let quitKey = ShortcutUtils.prettifyShortcut(quitKeyElement);
        document.l10n.setAttributes(
          document.getElementById("warnOnQuitKey"),
          "ask-on-quit-with-key",
          { quitKey }
        );
      } else {
        // If the quit key element does not exist, then the quit key has
        // been disabled, so just hide the checkbox.
        document.getElementById("warnOnQuitKey").hidden = true;
      }
    }

    setEventListener("ctrlTabRecentlyUsedOrder", "command", function () {
      Services.prefs.clearUserPref("browser.ctrlTab.migrated");
    });
    setEventListener("manageBrowserLanguagesButton", "command", function () {
      gMainPane.showBrowserLanguagesSubDialog({ search: false });
    });
    if (AppConstants.MOZ_UPDATER) {
      // These elements are only compiled in when the updater is enabled
      setEventListener("checkForUpdatesButton", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("downloadAndInstallButton", "command", function () {
        gAppUpdater.startDownload();
      });
      setEventListener("updateButton", "command", function () {
        gAppUpdater.buttonRestartAfterDownload();
      });
      setEventListener("checkForUpdatesButton2", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("checkForUpdatesButton3", "command", function () {
        gAppUpdater.checkForUpdates();
      });
      setEventListener("checkForUpdatesButton4", "command", function () {
        gAppUpdater.checkForUpdates();
      });
    }

    // Startup pref
    setEventListener(
      "browserRestoreSession",
      "command",
      gMainPane.onBrowserRestoreSessionChange
    );
    if (AppConstants.platform == "win") {
      setEventListener(
        "windowsLaunchOnLogin",
        "command",
        gMainPane.onWindowsLaunchOnLoginChange
      );
      if (
        Services.prefs.getBoolPref(
          "browser.startup.windowsLaunchOnLogin.enabled",
          false
        )
      ) {
        document.getElementById("windowsLaunchOnLoginBox").hidden = false;
        NimbusFeatures.windowsLaunchOnLogin.recordExposureEvent({
          once: true,
        });
      }
    }
    gMainPane.updateBrowserStartupUI =
      gMainPane.updateBrowserStartupUI.bind(gMainPane);
    Preferences.get("browser.privatebrowsing.autostart").on(
      "change",
      gMainPane.updateBrowserStartupUI
    );
    Preferences.get("browser.startup.page").on(
      "change",
      gMainPane.updateBrowserStartupUI
    );
    Preferences.get("browser.startup.homepage").on(
      "change",
      gMainPane.updateBrowserStartupUI
    );
    gMainPane.updateBrowserStartupUI();

    if (AppConstants.HAVE_SHELL_SERVICE) {
      setEventListener(
        "setDefaultButton",
        "command",
        gMainPane.setDefaultBrowser
      );
    }
    setEventListener(
      "disableContainersExtension",
      "command",
      makeDisableControllingExtension(PREF_SETTING_TYPE, CONTAINERS_KEY)
    );
    setEventListener("chooseLanguage", "command", gMainPane.showLanguages);
    // TODO (Bug 1817084) Remove this code when we disable the extension
    setEventListener(
      "fxtranslateButton",
      "command",
      gMainPane.showTranslationExceptions
    );
    Preferences.get("font.language.group").on(
      "change",
      gMainPane._rebuildFonts.bind(gMainPane)
    );
    setEventListener("advancedFonts", "command", gMainPane.configureFonts);
    setEventListener("colors", "command", gMainPane.configureColors);
    Preferences.get("browser.display.document_color_use").on(
      "change",
      gMainPane.updateColorsButton.bind(gMainPane)
    );
    gMainPane.updateColorsButton();
    Preferences.get("layers.acceleration.disabled").on(
      "change",
      gMainPane.updateHardwareAcceleration.bind(gMainPane)
    );
    setEventListener(
      "connectionSettings",
      "command",
      gMainPane.showConnections
    );
    setEventListener(
      "browserContainersCheckbox",
      "command",
      gMainPane.checkBrowserContainers
    );
    setEventListener(
      "browserContainersSettings",
      "command",
      gMainPane.showContainerSettings
    );
    setEventListener(
      "data-migration",
      "command",
      gMainPane.onMigrationButtonCommand
    );

    document
      .getElementById("migrationWizardDialog")
      .addEventListener("MigrationWizard:Close", function (e) {
        e.currentTarget.close();
      });

    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      document.getElementById("dataMigrationGroup").remove();
    }

    if (
      Services.prefs.getBoolPref("browser.backup.preferences.ui.enabled", false)
    ) {
      let backupGroup = document.getElementById("dataBackupGroup");
      backupGroup.removeAttribute("data-hidden-from-search");
    }

    if (!SelectableProfileService.isEnabled) {
      // Don't want to rely on .hidden for the toplevel groupbox because
      // of the pane hiding/showing code potentially interfering:
      document
        .getElementById("profilesGroup")
        .setAttribute("style", "display: none !important");
    } else {
      setEventListener("manage-profiles", "command", gMainPane.manageProfiles);
    }

    // Initializes the fonts dropdowns displayed in this pane.
    this._rebuildFonts();

    // Firefox Translations settings panel
    // TODO (Bug 1817084) Remove this code when we disable the extension
    const fxtranslationsDisabledPrefName = "extensions.translations.disabled";
    if (!Services.prefs.getBoolPref(fxtranslationsDisabledPrefName, true)) {
      let fxtranslationRow = document.getElementById("fxtranslationsBox");
      fxtranslationRow.hidden = false;
    }

    let emeUIEnabled = Services.prefs.getBoolPref("browser.eme.ui.enabled");
    // Force-disable/hide on WinXP:
    if (navigator.platform.toLowerCase().startsWith("win")) {
      emeUIEnabled =
        emeUIEnabled && parseFloat(Services.sysinfo.get("version")) >= 6;
    }
    if (!emeUIEnabled) {
      // Don't want to rely on .hidden for the toplevel groupbox because
      // of the pane hiding/showing code potentially interfering:
      document
        .getElementById("drmGroup")
        .setAttribute("style", "display: none !important");
    }
    // Initialize the Firefox Updates section.
    let version = AppConstants.MOZ_APP_VERSION_DISPLAY;

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

    document.l10n.setAttributes(
      document.getElementById("updateAppInfo"),
      "update-application-version",
      { version }
    );

    // Show a release notes link if we have a URL.
    let relNotesLink = document.getElementById("releasenotes");
    let relNotesPrefType = Services.prefs.getPrefType("app.releaseNotesURL");
    if (relNotesPrefType != Services.prefs.PREF_INVALID) {
      let relNotesURL = Services.urlFormatter.formatURLPref(
        "app.releaseNotesURL"
      );
      if (relNotesURL != "about:blank") {
        relNotesLink.href = relNotesURL;
        relNotesLink.hidden = false;
      }
    }

    let defaults = Services.prefs.getDefaultBranch(null);
    let distroId = defaults.getCharPref("distribution.id", "");
    if (distroId) {
      let distroString = distroId;

      let distroVersion = defaults.getCharPref("distribution.version", "");
      if (distroVersion) {
        distroString += " - " + distroVersion;
      }

      let distroIdField = document.getElementById("distributionId");
      distroIdField.value = distroString;
      distroIdField.hidden = false;

      let distroAbout = defaults.getStringPref("distribution.about", "");
      if (distroAbout) {
        let distroField = document.getElementById("distribution");
        distroField.value = distroAbout;
        distroField.hidden = false;
      }
    }

    if (AppConstants.MOZ_UPDATER) {
      gAppUpdater = new appUpdater();
      setEventListener("showUpdateHistory", "command", gMainPane.showUpdates);

      let updateDisabled =
        Services.policies && !Services.policies.isAllowed("appUpdate");

      if (gIsPackagedApp) {
        // When we're running inside an app package, there's no point in
        // displaying any update content here, and it would get confusing if we
        // did, because our updater is not enabled.
        // We can't rely on the hidden attribute for the toplevel elements,
        // because of the pane hiding/showing code interfering.
        document
          .getElementById("updatesCategory")
          .setAttribute("style", "display: none !important");
        document
          .getElementById("updateApp")
          .setAttribute("style", "display: none !important");
      } else if (
        updateDisabled ||
        UpdateUtils.appUpdateAutoSettingIsLocked() ||
        gApplicationUpdateService.manualUpdateOnly
      ) {
        document.getElementById("updateAllowDescription").hidden = true;
        document.getElementById("updateSettingsContainer").hidden = true;
      } else {
        // Start with no option selected since we are still reading the value
        document.getElementById("autoDesktop").removeAttribute("selected");
        document.getElementById("manualDesktop").removeAttribute("selected");
        // Start reading the correct value from the disk
        this.readUpdateAutoPref();
        setEventListener("updateRadioGroup", "command", event => {
          if (event.target.id == "backgroundUpdate") {
            this.writeBackgroundUpdatePref();
          } else {
            this.writeUpdateAutoPref();
          }
        });
        if (this.isBackgroundUpdateUIAvailable()) {
          document.getElementById("backgroundUpdate").hidden = false;
          // Start reading the background update pref's value from the disk.
          this.readBackgroundUpdatePref();
        }
      }

      if (AppConstants.platform == "win") {
        // Check for a launch on login registry key
        // This accounts for if a user manually changes it in the registry
        // Disabling in Task Manager works outside of just deleting the registry key
        // in HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run
        // but it is not possible to change it back to enabled as the disabled value is just a random
        // hexadecimal number
        let launchOnLoginCheckbox = document.getElementById(
          "windowsLaunchOnLogin"
        );

        let startWithLastProfile = Cc[
          "@mozilla.org/toolkit/profile-service;1"
        ].getService(Ci.nsIToolkitProfileService).startWithLastProfile;

        // Grey out the launch on login checkbox if startWithLastProfile is false
        document.getElementById(
          "windowsLaunchOnLoginDisabledProfileBox"
        ).hidden = startWithLastProfile;
        launchOnLoginCheckbox.disabled = !startWithLastProfile;

        if (!startWithLastProfile) {
          launchOnLoginCheckbox.checked = false;
        } else {
          WindowsLaunchOnLogin.getLaunchOnLoginEnabled().then(enabled => {
            launchOnLoginCheckbox.checked = enabled;
          });

          WindowsLaunchOnLogin.getLaunchOnLoginApproved().then(
            approvedByWindows => {
              launchOnLoginCheckbox.disabled = !approvedByWindows;
              document.getElementById(
                "windowsLaunchOnLoginDisabledBox"
              ).hidden = approvedByWindows;
            }
          );
        }

        // On Windows, the Application Update setting is an installation-
        // specific preference, not a profile-specific one. Show a warning to
        // inform users of this.
        let updateContainer = document.getElementById(
          "updateSettingsContainer"
        );
        updateContainer.classList.add("updateSettingCrossUserWarningContainer");
        document.getElementById("updateSettingCrossUserWarningDesc").hidden =
          false;
      }
    }

    // Initilize Application section.

    // Observe preferences that influence what we display so we can rebuild
    // the view when they change.
    Services.obs.addObserver(this, AUTO_UPDATE_CHANGED_TOPIC);
    Services.obs.addObserver(this, BACKGROUND_UPDATE_CHANGED_TOPIC);

    setEventListener("filter", "command", gMainPane.filter);
    setEventListener("typeColumn", "click", gMainPane.sort);
    setEventListener("actionColumn", "click", gMainPane.sort);
    setEventListener("chooseFolder", "command", gMainPane.chooseFolder);
    Preferences.get("browser.download.folderList").on(
      "change",
      gMainPane.displayDownloadDirPref.bind(gMainPane)
    );
    Preferences.get("browser.download.dir").on(
      "change",
      gMainPane.displayDownloadDirPref.bind(gMainPane)
    );
    gMainPane.displayDownloadDirPref();

    // Listen for window unload so we can remove our preference observers.
    window.addEventListener("unload", this);

    // Figure out how we should be sorting the list.  We persist sort settings
    // across sessions, so we can't assume the default sort column/direction.
    // XXX should we be using the XUL sort service instead?
    if (document.getElementById("actionColumn").hasAttribute("sortDirection")) {
      this._sortColumn = document.getElementById("actionColumn");
      // The typeColumn element always has a sortDirection attribute,
      // either because it was persisted or because the default value
      // from the xul file was used.  If we are sorting on the other
      // column, we should remove it.
      document.getElementById("typeColumn").removeAttribute("sortDirection");
    } else {
      this._sortColumn = document.getElementById("typeColumn");
    }

    appendSearchKeywords(
      "browserContainersSettings",
      [
        "user-context-personal",
        "user-context-work",
        "user-context-banking",
        "user-context-shopping",
      ].map(ContextualIdentityService.formatContextLabel)
    );

    AppearanceChooser.init();

    // Notify observers that the UI is now ready
    Services.obs.notifyObservers(window, "main-pane-loaded");

    Preferences.addSyncFromPrefListener(
      document.getElementById("defaultFont"),
      element => FontBuilder.readFontSelection(element)
    );
    Preferences.addSyncFromPrefListener(
      document.getElementById("checkSpelling"),
      () => this.readCheckSpelling()
    );
    Preferences.addSyncToPrefListener(
      document.getElementById("checkSpelling"),
      () => this.writeCheckSpelling()
    );
    Preferences.addSyncFromPrefListener(
      document.getElementById("alwaysAsk"),
      () => this.readUseDownloadDir()
    );
    Preferences.addSyncFromPrefListener(
      document.getElementById("linkTargeting"),
      () => this.readLinkTarget()
    );
    Preferences.addSyncToPrefListener(
      document.getElementById("linkTargeting"),
      () => this.writeLinkTarget()
    );
    Preferences.addSyncFromPrefListener(
      document.getElementById("browserContainersCheckbox"),
      () => this.readBrowserContainersCheckbox()
    );

    this.setInitialized();
  },

  preInit() {
    promiseLoadHandlersList = new Promise((resolve, reject) => {
      // Load the data and build the list of handlers for applications pane.
      // By doing this after pageshow, we ensure it doesn't delay painting
      // of the preferences page.
      window.addEventListener(
        "pageshow",
        async () => {
          await this.initialized;
          try {
            this._initListEventHandlers();
            this._loadData();
            await this._rebuildVisibleTypes();
            await this._rebuildView();
            await this._sortListView();
            resolve();
          } catch (ex) {
            reject(ex);
          }
        },
        { once: true }
      );
    });
  },

  handleSubcategory(subcategory) {
    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      return false;
    }
    if (subcategory == "migrate") {
      this.showMigrationWizardDialog();
      return true;
    }

    if (subcategory == "migrate-autoclose") {
      this.showMigrationWizardDialog({ closeTabWhenDone: true });
    }

    return false;
  },

  // CONTAINERS

  /*
   * preferences:
   *
   * privacy.userContext.enabled
   * - true if containers is enabled
   */

  /**
   * Enables/disables the Settings button used to configure containers
   */
  readBrowserContainersCheckbox() {
    const pref = Preferences.get("privacy.userContext.enabled");
    const settings = document.getElementById("browserContainersSettings");

    settings.disabled = !pref.value;
    const containersEnabled = Services.prefs.getBoolPref(
      "privacy.userContext.enabled"
    );
    const containersCheckbox = document.getElementById(
      "browserContainersCheckbox"
    );
    containersCheckbox.checked = containersEnabled;
    handleControllingExtension(PREF_SETTING_TYPE, CONTAINERS_KEY).then(
      isControlled => {
        containersCheckbox.disabled = isControlled;
      }
    );
  },

  /**
   * Show the Containers UI depending on the privacy.userContext.ui.enabled pref.
   */
  initBrowserContainers() {
    if (!Services.prefs.getBoolPref("privacy.userContext.ui.enabled")) {
      // The browserContainersGroup element has its own internal padding that
      // is visible even if the browserContainersbox is visible, so hide the whole
      // groupbox if the feature is disabled to prevent a gap in the preferences.
      document
        .getElementById("browserContainersbox")
        .setAttribute("data-hidden-from-search", "true");
      return;
    }
    Services.prefs.addObserver(PREF_CONTAINERS_EXTENSION, this);

    document.getElementById("browserContainersbox").hidden = false;
    this.readBrowserContainersCheckbox();
  },

  async onGetStarted() {
    if (!AppConstants.MOZ_DEV_EDITION) {
      return;
    }
    const win = Services.wm.getMostRecentWindow("navigator:browser");
    if (!win) {
      return;
    }
    const user = await fxAccounts.getSignedInUser();
    if (user) {
      // We have a user, open Sync preferences in the same tab
      win.openTrustedLinkIn("about:preferences#sync", "current");
      return;
    }
    if (!(await FxAccounts.canConnectAccount())) {
      return;
    }
    let url =
      await FxAccounts.config.promiseConnectAccountURI("dev-edition-setup");
    let accountsTab = win.gBrowser.addWebTab(url);
    win.gBrowser.selectedTab = accountsTab;
  },

  // HOME PAGE
  /*
   * Preferences:
   *
   * browser.startup.page
   * - what page(s) to show when the user starts the application, as an integer:
   *
   *     0: a blank page (DEPRECATED - this can be set via browser.startup.homepage)
   *     1: the home page (as set by the browser.startup.homepage pref)
   *     2: the last page the user visited (DEPRECATED)
   *     3: windows and tabs from the last session (a.k.a. session restore)
   *
   *   The deprecated option is not exposed in UI; however, if the user has it
   *   selected and doesn't change the UI for this preference, the deprecated
   *   option is preserved.
   */

  /**
   * Utility function to enable/disable the button specified by aButtonID based
   * on the value of the Boolean preference specified by aPreferenceID.
   */
  updateButtons(aButtonID, aPreferenceID) {
    var button = document.getElementById(aButtonID);
    var preference = Preferences.get(aPreferenceID);
    button.disabled = !preference.value;
    return undefined;
  },

  /**
   * Hide/show the "Show my windows and tabs from last time" option based
   * on the value of the browser.privatebrowsing.autostart pref.
   */
  updateBrowserStartupUI() {
    const pbAutoStartPref = Preferences.get(
      "browser.privatebrowsing.autostart"
    );
    const startupPref = Preferences.get("browser.startup.page");

    let newValue;
    let checkbox = document.getElementById("browserRestoreSession");
    checkbox.disabled = pbAutoStartPref.value || startupPref.locked;
    newValue = pbAutoStartPref.value
      ? false
      : startupPref.value === this.STARTUP_PREF_RESTORE_SESSION;
    if (checkbox.checked !== newValue) {
      checkbox.checked = newValue;
    }
  },
  /**
   * Fetch the existing default zoom value, initialise and unhide
   * the preferences menu. This method also establishes a listener
   * to ensure handleDefaultZoomChange is called on future menu
   * changes.
   */
  async initDefaultZoomValues() {
    let win = window.browsingContext.topChromeWindow;
    let selected = await win.ZoomUI.getGlobalValue();
    let menulist = document.getElementById("defaultZoom");

    new SelectionChangedMenulist(menulist, event => {
      let parsedZoom = parseFloat((event.target.value / 100).toFixed(2));
      gMainPane.handleDefaultZoomChange(parsedZoom);
    });

    setEventListener("zoomText", "command", function () {
      win.ZoomManager.toggleZoom();
      document.getElementById("text-zoom-override-warning").hidden =
        !document.getElementById("zoomText").checked;
    });

    let zoomValues = win.ZoomManager.zoomValues.map(a => {
      return Math.round(a * 100);
    });

    let fragment = document.createDocumentFragment();
    for (let zoomLevel of zoomValues) {
      let menuitem = document.createXULElement("menuitem");
      document.l10n.setAttributes(menuitem, "preferences-default-zoom-value", {
        percentage: zoomLevel,
      });
      menuitem.setAttribute("value", zoomLevel);
      fragment.appendChild(menuitem);
    }

    let menupopup = menulist.querySelector("menupopup");
    menupopup.appendChild(fragment);
    menulist.value = Math.round(selected * 100);

    let checkbox = document.getElementById("zoomText");
    checkbox.checked = !win.ZoomManager.useFullZoom;
    document.getElementById("text-zoom-override-warning").hidden =
      !checkbox.checked;
    document.getElementById("zoomBox").hidden = false;
  },

  updateColorsButton() {
    document.getElementById("colors").disabled =
      Preferences.get("browser.display.document_color_use").value != 2;
  },

  /**
   * Initialize the translations view.
   */
  async initTranslations() {
    if (!Services.prefs.getBoolPref("browser.translations.enable")) {
      return;
    }

    /**
     * Which phase a language download is in.
     *
     * @typedef {"downloaded" | "loading" | "uninstalled"} DownloadPhase
     */

    // Immediately show the group so that the async load of the component does
    // not cause the layout to jump. The group will be empty initially.
    document.getElementById("translationsGroup").hidden = false;

    class TranslationsState {
      /**
       * The fully initialized state.
       *
       * @param {Object} supportedLanguages
       * @param {Array<{ langTag: string, displayName: string}} languageList
       * @param {Map<string, DownloadPhase>} downloadPhases
       */
      constructor(supportedLanguages, languageList, downloadPhases) {
        this.supportedLanguages = supportedLanguages;
        this.languageList = languageList;
        this.downloadPhases = downloadPhases;
      }

      /**
       * Handles all of the async initialization logic.
       */
      static async create() {
        const supportedLanguages =
          await TranslationsParent.getSupportedLanguages();
        const languageList =
          TranslationsParent.getLanguageList(supportedLanguages);
        const downloadPhases =
          await TranslationsState.createDownloadPhases(languageList);

        if (supportedLanguages.languagePairs.length === 0) {
          throw new Error(
            "The supported languages list was empty. RemoteSettings may not be available at the moment."
          );
        }

        return new TranslationsState(
          supportedLanguages,
          languageList,
          downloadPhases
        );
      }

      /**
       * Determine the download phase of each language file.
       *
       * @param {Array<{ langTag: string, displayName: string}} languageList.
       * @returns {Map<string, DownloadPhase>} Map the language tag to whether it is downloaded.
       */
      static async createDownloadPhases(languageList) {
        const downloadPhases = new Map();
        for (const { langTag } of languageList) {
          downloadPhases.set(
            langTag,
            (await TranslationsParent.hasAllFilesForLanguage(langTag))
              ? "downloaded"
              : "uninstalled"
          );
        }
        return downloadPhases;
      }
    }

    class TranslationsView {
      /** @type {Map<string, XULButton>} */
      deleteButtons = new Map();
      /** @type {Map<string, XULButton>} */
      downloadButtons = new Map();

      /**
       * @param {TranslationsState} state
       */
      constructor(state) {
        this.state = state;
        this.elements = {
          settingsButton: document.getElementById(
            "translations-manage-settings-button"
          ),
          installList: document.getElementById(
            "translations-manage-install-list"
          ),
          installAll: document.getElementById(
            "translations-manage-install-all"
          ),
          deleteAll: document.getElementById("translations-manage-delete-all"),
          error: document.getElementById("translations-manage-error"),
        };
        this.setup();
      }

      setup() {
        this.buildLanguageList();

        this.elements.settingsButton.addEventListener(
          "command",
          gMainPane.showTranslationsSettings
        );
        this.elements.installAll.addEventListener(
          "command",
          this.handleInstallAll
        );
        this.elements.deleteAll.addEventListener(
          "command",
          this.handleDeleteAll
        );

        Services.obs.addObserver(this, "intl:app-locales-changed");
      }

      destroy() {
        Services.obs.removeObserver(this, "intl:app-locales-changed");
      }

      handleInstallAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsParent.downloadAllFiles();
          this.markAllDownloadPhases("downloaded");
        } catch (error) {
          TranslationsView.showError(
            "translations-manage-error-download",
            error
          );
          await this.reloadDownloadPhases();
          this.updateAllButtons();
        }
        this.disableButtons(false);
      };

      handleDeleteAll = async () => {
        this.hideError();
        this.disableButtons(true);
        try {
          await TranslationsParent.deleteAllLanguageFiles();
          this.markAllDownloadPhases("uninstalled");
        } catch (error) {
          TranslationsView.showError("translations-manage-error-remove", error);
          // The download phases are invalidated with the error and must be reloaded.
          await this.reloadDownloadPhases();
          console.error(error);
        }
        this.disableButtons(false);
      };

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDownloadButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.downloadLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "downloaded");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-download",
              error
            );
            this.updateDownloadPhase(langTag, "uninstalled");
          }
        };
      }

      /**
       * @param {string} langTag
       * @returns {Function}
       */
      getDeleteButtonHandler(langTag) {
        return async () => {
          this.hideError();
          this.updateDownloadPhase(langTag, "loading");
          try {
            await TranslationsParent.deleteLanguageFiles(langTag);
            this.updateDownloadPhase(langTag, "uninstalled");
          } catch (error) {
            TranslationsView.showError(
              "translations-manage-error-remove",
              error
            );
            // The download phases are invalidated with the error and must be reloaded.
            await this.reloadDownloadPhases();
          }
        };
      }

      buildLanguageList() {
        const listFragment = document.createDocumentFragment();

        for (const { langTag, displayName } of this.state.languageList) {
          const hboxRow = document.createXULElement("hbox");
          hboxRow.classList.add("translations-manage-language");
          hboxRow.setAttribute("data-lang-tag", langTag);

          const languageLabel = document.createXULElement("label");
          languageLabel.textContent = displayName; // The display name is already localized.

          const downloadButton = document.createXULElement("button");
          const deleteButton = document.createXULElement("button");

          downloadButton.addEventListener(
            "command",
            this.getDownloadButtonHandler(langTag)
          );
          deleteButton.addEventListener(
            "command",
            this.getDeleteButtonHandler(langTag)
          );

          document.l10n.setAttributes(
            downloadButton,
            "translations-manage-language-download-button"
          );
          document.l10n.setAttributes(
            deleteButton,
            "translations-manage-language-remove-button"
          );

          downloadButton.hidden = true;
          deleteButton.hidden = true;

          this.deleteButtons.set(langTag, deleteButton);
          this.downloadButtons.set(langTag, downloadButton);

          hboxRow.appendChild(languageLabel);
          hboxRow.appendChild(downloadButton);
          hboxRow.appendChild(deleteButton);
          listFragment.appendChild(hboxRow);
        }
        this.updateAllButtons();
        this.elements.installList.appendChild(listFragment);
      }

      /**
       * Update the DownloadPhase for a single langTag.
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateDownloadPhase(langTag, downloadPhase) {
        this.state.downloadPhases.set(langTag, downloadPhase);
        this.updateButton(langTag, downloadPhase);
        this.updateHeaderButtons();
      }

      /**
       * Recreates the download map when the state is invalidated.
       */
      async reloadDownloadPhases() {
        this.state.downloadPhases =
          await TranslationsState.createDownloadPhases(this.state.languageList);
        this.updateAllButtons();
      }

      /**
       * Set all the downloads.
       * @param {DownloadPhase} downloadPhase
       */
      markAllDownloadPhases(downloadPhase) {
        const { downloadPhases } = this.state;
        for (const key of downloadPhases.keys()) {
          downloadPhases.set(key, downloadPhase);
        }
        this.updateAllButtons();
      }

      /**
       * If all languages are downloaded, or no languages are downloaded then
       * the visibility of the buttons need to change.
       */
      updateHeaderButtons() {
        let allDownloaded = true;
        let allUninstalled = true;
        for (const downloadPhase of this.state.downloadPhases.values()) {
          if (downloadPhase === "loading") {
            // Don't count loading towards this calculation.
            continue;
          }
          allDownloaded &&= downloadPhase === "downloaded";
          allUninstalled &&= downloadPhase === "uninstalled";
        }

        this.elements.installAll.hidden = allDownloaded;
        this.elements.deleteAll.hidden = allUninstalled;
      }

      /**
       * Update the buttons according to their download state.
       */
      updateAllButtons() {
        this.updateHeaderButtons();
        for (const [langTag, downloadPhase] of this.state.downloadPhases) {
          this.updateButton(langTag, downloadPhase);
        }
      }

      /**
       * @param {string} langTag
       * @param {DownloadPhase} downloadPhase
       */
      updateButton(langTag, downloadPhase) {
        const downloadButton = this.downloadButtons.get(langTag);
        const deleteButton = this.deleteButtons.get(langTag);
        switch (downloadPhase) {
          case "downloaded":
            downloadButton.hidden = true;
            deleteButton.hidden = false;
            downloadButton.removeAttribute("disabled");
            break;
          case "uninstalled":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.removeAttribute("disabled");
            break;
          case "loading":
            downloadButton.hidden = false;
            deleteButton.hidden = true;
            downloadButton.setAttribute("disabled", true);
            break;
        }
      }

      /**
       * @param {boolean} isDisabled
       */
      disableButtons(isDisabled) {
        this.elements.installAll.disabled = isDisabled;
        this.elements.deleteAll.disabled = isDisabled;
        for (const button of this.downloadButtons.values()) {
          button.disabled = isDisabled;
        }
        for (const button of this.deleteButtons.values()) {
          button.disabled = isDisabled;
        }
      }

      /**
       * This method is static in case an error happens during the creation of the
       * TranslationsState.
       *
       * @param {string} l10nId
       * @param {Error} error
       */
      static showError(l10nId, error) {
        console.error(error);
        const errorMessage = document.getElementById(
          "translations-manage-error"
        );
        errorMessage.hidden = false;
        document.l10n.setAttributes(errorMessage, l10nId);
      }

      hideError() {
        this.elements.error.hidden = true;
      }

      observe(_subject, topic, _data) {
        if (topic === "intl:app-locales-changed") {
          this.refreshLanguageListDisplay();
        }
      }

      refreshLanguageListDisplay() {
        try {
          const languageDisplayNames =
            TranslationsParent.createLanguageDisplayNames();

          for (const row of this.elements.installList.children) {
            const rowLangTag = row.getAttribute("data-lang-tag");
            if (!rowLangTag) {
              continue;
            }

            const label = row.querySelector("label");
            if (label) {
              const newDisplayName = languageDisplayNames.of(rowLangTag);
              if (label.textContent !== newDisplayName) {
                label.textContent = newDisplayName;
              }
            }
          }
        } catch (error) {
          console.error(error);
        }
      }
    }

    TranslationsState.create().then(
      state => {
        this._translationsView = new TranslationsView(state);
      },
      error => {
        // This error can happen when a user is not connected to the internet, or
        // RemoteSettings is down for some reason.
        TranslationsView.showError("translations-manage-error-list", error);
      }
    );
  },

  initPrimaryBrowserLanguageUI() {
    // This will register the "command" listener.
    let menulist = document.getElementById("primaryBrowserLocale");
    new SelectionChangedMenulist(menulist, event => {
      gMainPane.onPrimaryBrowserLanguageMenuChange(event);
    });

    gMainPane.updatePrimaryBrowserLanguageUI(Services.locale.appLocaleAsBCP47);
  },

  /**
   * Update the available list of locales and select the locale that the user
   * is "selecting". This could be the currently requested locale or a locale
   * that the user would like to switch to after confirmation.
   *
   * @param {string} selected - The selected BCP 47 locale.
   */
  async updatePrimaryBrowserLanguageUI(selected) {
    let available = await LangPackMatcher.getAvailableLocales();
    let localeNames = Services.intl.getLocaleDisplayNames(
      undefined,
      available,
      { preferNative: true }
    );
    let locales = available.map((code, i) => ({ code, name: localeNames[i] }));
    locales.sort((a, b) => a.name > b.name);

    let fragment = document.createDocumentFragment();
    for (let { code, name } of locales) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.setAttribute("value", code);
      menuitem.setAttribute("label", name);
      fragment.appendChild(menuitem);
    }

    // Add an option to search for more languages if downloading is supported.
    if (Services.prefs.getBoolPref("intl.multilingual.downloadEnabled")) {
      let menuitem = document.createXULElement("menuitem");
      menuitem.id = "primaryBrowserLocaleSearch";
      menuitem.setAttribute(
        "label",
        await document.l10n.formatValue("browser-languages-search")
      );
      menuitem.setAttribute("value", "search");
      fragment.appendChild(menuitem);
    }

    let menulist = document.getElementById("primaryBrowserLocale");
    let menupopup = menulist.querySelector("menupopup");
    menupopup.textContent = "";
    menupopup.appendChild(fragment);
    menulist.value = selected;

    document.getElementById("browserLanguagesBox").hidden = false;
  },

  /* Show the confirmation message bar to allow a restart into the new locales. */
  async showConfirmLanguageChangeMessageBar(locales) {
    let messageBar = document.getElementById("confirmBrowserLanguage");

    // Get the bundle for the new locale.
    let newBundle = getBundleForLocales(locales);

    // Find the messages and labels.
    let messages = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-description")
      )
    );
    let buttonLabels = await Promise.all(
      [newBundle, document.l10n].map(async bundle =>
        bundle.formatValue("confirm-browser-language-change-button")
      )
    );

    // If both the message and label are the same, just include one row.
    if (messages[0] == messages[1] && buttonLabels[0] == buttonLabels[1]) {
      messages.pop();
      buttonLabels.pop();
    }

    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";

    for (let i = 0; i < messages.length; i++) {
      let messageContainer = document.createXULElement("hbox");
      messageContainer.classList.add("message-bar-content");
      messageContainer.style.flex = "1 50%";
      messageContainer.setAttribute("align", "center");

      let description = document.createXULElement("description");
      description.classList.add("message-bar-description");

      if (i == 0 && Services.intl.getScriptDirection(locales[0]) === "rtl") {
        description.classList.add("rtl-locale");
      }
      description.setAttribute("flex", "1");
      description.textContent = messages[i];
      messageContainer.appendChild(description);

      let button = document.createXULElement("button");
      button.addEventListener(
        "command",
        gMainPane.confirmBrowserLanguageChange
      );
      button.classList.add("message-bar-button");
      button.setAttribute("locales", locales.join(","));
      button.setAttribute("label", buttonLabels[i]);
      messageContainer.appendChild(button);

      contentContainer.appendChild(messageContainer);
    }

    messageBar.hidden = false;
    gMainPane.selectedLocalesForRestart = locales;
  },

  hideConfirmLanguageChangeMessageBar() {
    let messageBar = document.getElementById("confirmBrowserLanguage");
    messageBar.hidden = true;
    let contentContainer = messageBar.querySelector(
      ".message-bar-content-container"
    );
    contentContainer.textContent = "";
    gMainPane.requestingLocales = null;
  },

  /* Confirm the locale change and restart the browser in the new locale. */
  confirmBrowserLanguageChange(event) {
    let localesString = (event.target.getAttribute("locales") || "").trim();
    if (!localesString || !localesString.length) {
      return;
    }
    let locales = localesString.split(",");
    Services.locale.requestedLocales = locales;

    // Record the change in telemetry before we restart.
    gMainPane.recordBrowserLanguagesTelemetry("apply");

    // Restart with the new locale.
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );
    if (!cancelQuit.data) {
      Services.startup.quit(
        Services.startup.eAttemptQuit | Services.startup.eRestart
      );
    }
  },

  /* Show or hide the confirm change message bar based on the new locale. */
  onPrimaryBrowserLanguageMenuChange(event) {
    let locale = event.target.value;

    if (locale == "search") {
      gMainPane.showBrowserLanguagesSubDialog({ search: true });
      return;
    } else if (locale == Services.locale.appLocaleAsBCP47) {
      this.hideConfirmLanguageChangeMessageBar();
      return;
    }

    let newLocales = Array.from(
      new Set([locale, ...Services.locale.requestedLocales]).values()
    );

    gMainPane.recordBrowserLanguagesTelemetry("reorder");

    switch (gMainPane.getLanguageSwitchTransitionType(newLocales)) {
      case "requires-restart":
        // Prepare to change the locales, as they were different.
        gMainPane.showConfirmLanguageChangeMessageBar(newLocales);
        gMainPane.updatePrimaryBrowserLanguageUI(newLocales[0]);
        break;
      case "live-reload":
        Services.locale.requestedLocales = newLocales;
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case "locales-match":
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  /**
   * Takes as newZoom a floating point value representing the
   * new default zoom. This value should not be a string, and
   * should not carry a percentage sign/other localisation
   * characteristics.
   */
  handleDefaultZoomChange(newZoom) {
    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );
    let nonPrivateLoadContext = Cu.createLoadContext();
    /* Because our setGlobal function takes in a browsing context, and
     * because we want to keep this property consistent across both private
     * and non-private contexts, we crate a non-private context and use that
     * to set the property, regardless of our actual context.
     */

    let win = window.browsingContext.topChromeWindow;
    cps2.setGlobal(win.FullZoom.name, newZoom, nonPrivateLoadContext);
  },

  onBrowserRestoreSessionChange(event) {
    const value = event.target.checked;
    const startupPref = Preferences.get("browser.startup.page");
    let newValue;

    if (value) {
      // We need to restore the blank homepage setting in our other pref
      if (startupPref.value === this.STARTUP_PREF_BLANK) {
        HomePage.safeSet("about:blank");
      }
      newValue = this.STARTUP_PREF_RESTORE_SESSION;
    } else {
      newValue = this.STARTUP_PREF_HOMEPAGE;
    }
    startupPref.value = newValue;
  },

  async onWindowsLaunchOnLoginChange(event) {
    if (AppConstants.platform !== "win") {
      return;
    }
    if (event.target.checked) {
      // windowsLaunchOnLogin has been checked: create registry key or shortcut
      // The shortcut is created with the same AUMID as Firefox itself. However,
      // this is not set during browser tests and the fallback of checking the
      // registry fails. As such we pass an arbitrary AUMID for the purpose
      // of testing.
      await WindowsLaunchOnLogin.createLaunchOnLogin();
      Services.prefs.setBoolPref(
        "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
        true
      );
    } else {
      // windowsLaunchOnLogin has been unchecked: delete registry key and shortcut
      await WindowsLaunchOnLogin.removeLaunchOnLogin();
    }
  },

  // TABS

  /*
   * Preferences:
   *
   * browser.link.open_newwindow - int
   *   Determines where links targeting new windows should open.
   *   Values:
   *     1 - Open in the current window or tab.
   *     2 - Open in a new window.
   *     3 - Open in a new tab in the most recent window.
   * browser.tabs.loadInBackground - bool
   *   True - Whether browser should switch to a new tab opened from a link.
   * browser.tabs.warnOnClose - bool
   *   True - If when closing a window with multiple tabs the user is warned and
   *          allowed to cancel the action, false to just close the window.
   * browser.warnOnQuitShortcut - bool
   *   True - If the keyboard shortcut (Ctrl/Cmd+Q) is pressed, the user should
   *          be warned, false to just quit without prompting.
   * browser.tabs.warnOnOpen - bool
   *   True - Whether the user should be warned when trying to open a lot of
   *          tabs at once (e.g. a large folder of bookmarks), allowing to
   *          cancel the action.
   * browser.taskbar.previews.enable - bool
   *   True - Tabs are to be shown in Windows 7 taskbar.
   *   False - Only the window is to be shown in Windows 7 taskbar.
   */

  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns |true| if such links should be opened in new tabs
   */
  readLinkTarget() {
    var openNewWindow = Preferences.get("browser.link.open_newwindow");
    return openNewWindow.value != 2;
  },

  /**
   * Determines where a link which opens a new window will open.
   *
   * @returns 2 if such links should be opened in new windows,
   *          3 if such links should be opened in new tabs
   */
  writeLinkTarget() {
    var linkTargeting = document.getElementById("linkTargeting");
    return linkTargeting.checked ? 3 : 2;
  },
  /*
   * Preferences:
   *
   * browser.shell.checkDefault
   * - true if a default-browser check (and prompt to make it so if necessary)
   *   occurs at startup, false otherwise
   */

  /**
   * Show button for setting browser as default browser or information that
   * browser is already the default browser.
   */
  updateSetDefaultBrowser() {
    if (AppConstants.HAVE_SHELL_SERVICE) {
      let shellSvc = getShellService();
      let defaultBrowserBox = document.getElementById("defaultBrowserBox");
      let isInFlatpak = gGIOService?.isRunningUnderFlatpak;
      // Flatpak does not support setting nor detection of default browser
      if (!shellSvc || isInFlatpak) {
        defaultBrowserBox.hidden = true;
        return;
      }
      let isDefault = shellSvc.isDefaultBrowser(false, true);
      let setDefaultPane = document.getElementById("setDefaultPane");
      setDefaultPane.classList.toggle("is-default", isDefault);
      let alwaysCheck = document.getElementById("alwaysCheckDefault");
      let alwaysCheckPref = Preferences.get(
        "browser.shell.checkDefaultBrowser"
      );
      alwaysCheck.disabled = alwaysCheckPref.locked || isDefault;
    }
  },

  /**
   * Set browser as the operating system default browser.
   */
  async setDefaultBrowser() {
    if (AppConstants.HAVE_SHELL_SERVICE) {
      let alwaysCheckPref = Preferences.get(
        "browser.shell.checkDefaultBrowser"
      );
      alwaysCheckPref.value = true;

      // Reset exponential backoff delay time in order to do visual update in pollForDefaultBrowser.
      this._backoffIndex = 0;

      let shellSvc = getShellService();
      if (!shellSvc) {
        return;
      }

      // Disable the set default button, so that the user doesn't try to hit it again
      // while awaiting on setDefaultBrowser
      let setDefaultButton = document.getElementById("setDefaultButton");
      setDefaultButton.disabled = true;

      try {
        await shellSvc.setDefaultBrowser(false);
      } catch (ex) {
        console.error(ex);
        return;
      } finally {
        // Make sure to re-enable the default button when we're finished, regardless of the outcome
        setDefaultButton.disabled = false;
      }

      let isDefault = shellSvc.isDefaultBrowser(false, true);
      let setDefaultPane = document.getElementById("setDefaultPane");
      setDefaultPane.classList.toggle("is-default", isDefault);
    }
  },

  /**
   *  Shows a subdialog containing the profile selector page.
   */
  manageProfiles() {
    SelectableProfileService.maybeSetupDataStore().then(() => {
      gSubDialog.open("about:profilemanager");
    });
  },

  /**
   * Shows a dialog in which the preferred language for web content may be set.
   */
  showLanguages() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/languages.xhtml"
    );
  },

  recordBrowserLanguagesTelemetry(method, value = null) {
    Glean.intlUiBrowserLanguage[method + "Main"].record(
      value ? { value } : undefined
    );
  },

  /**
   * Open the browser languages sub dialog in either the normal mode, or search mode.
   * The search mode is only available from the menu to change the primary browser
   * language.
   *
   * @param {{ search: boolean }}
   */
  showBrowserLanguagesSubDialog({ search }) {
    // Record the telemetry event with an id to associate related actions.
    let telemetryId = parseInt(
      Services.telemetry.msSinceProcessStart(),
      10
    ).toString();
    let method = search ? "search" : "manage";
    gMainPane.recordBrowserLanguagesTelemetry(method, telemetryId);

    let opts = {
      selectedLocalesForRestart: gMainPane.selectedLocalesForRestart,
      search,
      telemetryId,
    };
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/browserLanguages.xhtml",
      { closingCallback: this.browserLanguagesClosed },
      opts
    );
  },

  /**
   * Determine the transition strategy for switching the locale based on prefs
   * and the switched locales.
   *
   * @param {Array<string>} newLocales - List of BCP 47 locale identifiers.
   * @returns {"locales-match" | "requires-restart" | "live-reload"}
   */
  getLanguageSwitchTransitionType(newLocales) {
    const { appLocalesAsBCP47 } = Services.locale;
    if (appLocalesAsBCP47.join(",") === newLocales.join(",")) {
      // The selected locales match, the order matters.
      return "locales-match";
    }

    if (Services.prefs.getBoolPref("intl.multilingual.liveReload")) {
      if (
        Services.intl.getScriptDirection(newLocales[0]) !==
          Services.intl.getScriptDirection(appLocalesAsBCP47[0]) &&
        !Services.prefs.getBoolPref("intl.multilingual.liveReloadBidirectional")
      ) {
        // Bug 1750852: The directionality of the text changed, which requires a restart
        // until the quality of the switch can be improved.
        return "requires-restart";
      }

      return "live-reload";
    }

    return "requires-restart";
  },

  /* Show or hide the confirm change message bar based on the updated ordering. */
  browserLanguagesClosed() {
    // When the subdialog is closed, settings are stored on gBrowserLanguagesDialog.
    // The next time the dialog is opened, a new gBrowserLanguagesDialog is created.
    let { selected } = this.gBrowserLanguagesDialog;

    this.gBrowserLanguagesDialog.recordTelemetry(
      selected ? "accept" : "cancel"
    );

    if (!selected) {
      // No locales were selected. Cancel the operation.
      return;
    }

    // Track how often locale fallback order is changed.
    // Drop the first locale and filter to only include the overlapping set
    const prevLocales = Services.locale.requestedLocales.filter(
      lc => selected.indexOf(lc) > 0
    );
    const newLocales = selected.filter(
      (lc, i) => i > 0 && prevLocales.includes(lc)
    );
    if (prevLocales.some((lc, i) => newLocales[i] != lc)) {
      this.gBrowserLanguagesDialog.recordTelemetry("setFallback");
    }

    switch (gMainPane.getLanguageSwitchTransitionType(selected)) {
      case "requires-restart":
        gMainPane.showConfirmLanguageChangeMessageBar(selected);
        gMainPane.updatePrimaryBrowserLanguageUI(selected[0]);
        break;
      case "live-reload":
        Services.locale.requestedLocales = selected;

        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      case "locales-match":
        // They matched, so we can reset the UI.
        gMainPane.updatePrimaryBrowserLanguageUI(
          Services.locale.appLocaleAsBCP47
        );
        gMainPane.hideConfirmLanguageChangeMessageBar();
        break;
      default:
        throw new Error("Unhandled transition type.");
    }
  },

  displayUseSystemLocale() {
    let appLocale = Services.locale.appLocaleAsBCP47;
    let regionalPrefsLocales = Services.locale.regionalPrefsLocales;
    if (!regionalPrefsLocales.length) {
      return;
    }
    let systemLocale = regionalPrefsLocales[0];
    let localeDisplayname = Services.intl.getLocaleDisplayNames(
      undefined,
      [systemLocale],
      { preferNative: true }
    );
    if (!localeDisplayname.length) {
      return;
    }
    let localeName = localeDisplayname[0];
    if (appLocale.split("-u-")[0] != systemLocale.split("-u-")[0]) {
      let checkbox = document.getElementById("useSystemLocale");
      document.l10n.setAttributes(checkbox, "use-system-locale", {
        localeName,
      });
      checkbox.hidden = false;
    }
  },

  /**
   * Displays the translation exceptions dialog where specific site and language
   * translation preferences can be set.
   */
  // TODO (Bug 1817084) Remove this code when we disable the extension
  showTranslationExceptions() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/translationExceptions.xhtml"
    );
  },

  showTranslationsSettings() {
    if (
      Services.prefs.getBoolPref("browser.translations.newSettingsUI.enable")
    ) {
      const translationsSettings = document.getElementById(
        "translations-settings-page"
      );
      translationsSettings.setAttribute("data-hidden-from-search", "false");
      translationsSettings.hidden = false;
      gotoPref("translations");
    } else {
      gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/translations.xhtml"
      );
    }
  },

  /**
   * Displays the fonts dialog, where web page font names and sizes can be
   * configured.
   */
  configureFonts() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/fonts.xhtml",
      { features: "resizable=no" }
    );
  },

  /**
   * Displays the colors dialog, where default web page/link/etc. colors can be
   * configured.
   */
  configureColors() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/colors.xhtml",
      { features: "resizable=no" }
    );
  },

  // NETWORK
  /**
   * Displays a dialog in which proxy settings may be changed.
   */
  showConnections() {
    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/connection.xhtml",
      { closingCallback: this.updateProxySettingsUI.bind(this) }
    );
  },

  // Update the UI to show the proper description depending on whether an
  // extension is in control or not.
  async updateProxySettingsUI() {
    let controllingExtension = await getControllingExtension(
      PREF_SETTING_TYPE,
      PROXY_KEY
    );
    let description = document.getElementById("connectionSettingsDescription");

    if (controllingExtension) {
      setControllingExtensionDescription(
        description,
        controllingExtension,
        "proxy.settings"
      );
    } else {
      setControllingExtensionDescription(
        description,
        null,
        "network-proxy-connection-description"
      );
    }
  },

  async checkBrowserContainers() {
    let checkbox = document.getElementById("browserContainersCheckbox");
    if (checkbox.checked) {
      Services.prefs.setBoolPref("privacy.userContext.enabled", true);
      return;
    }

    let count = ContextualIdentityService.countContainerTabs();
    if (count == 0) {
      Services.prefs.setBoolPref("privacy.userContext.enabled", false);
      return;
    }

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
    if (rv == 0) {
      Services.prefs.setBoolPref("privacy.userContext.enabled", false);
      return;
    }

    checkbox.checked = true;
  },

  /**
   * Displays container panel for customising and adding containers.
   */
  showContainerSettings() {
    gotoPref("containers");
  },

  updateHardwareAcceleration() {
    // Placeholder for restart on change
  },

  // FONTS

  /**
   * Populates the default font list in UI.
   */
  _rebuildFonts() {
    var langGroupPref = Preferences.get("font.language.group");
    var isSerif =
      this._readDefaultFontTypeForLanguage(langGroupPref.value) == "serif";
    this._selectDefaultLanguageGroup(langGroupPref.value, isSerif);
  },

  /**
   * Returns the type of the current default font for the language denoted by
   * aLanguageGroup.
   */
  _readDefaultFontTypeForLanguage(aLanguageGroup) {
    const kDefaultFontType = "font.default.%LANG%";
    var defaultFontTypePref = kDefaultFontType.replace(
      /%LANG%/,
      aLanguageGroup
    );
    var preference = Preferences.get(defaultFontTypePref);
    if (!preference) {
      preference = Preferences.add({ id: defaultFontTypePref, type: "string" });
      preference.on("change", gMainPane._rebuildFonts.bind(gMainPane));
    }
    return preference.value;
  },

  _selectDefaultLanguageGroupPromise: Promise.resolve(),

  _selectDefaultLanguageGroup(aLanguageGroup, aIsSerif) {
    this._selectDefaultLanguageGroupPromise = (async () => {
      // Avoid overlapping language group selections by awaiting the resolution
      // of the previous one.  We do this because this function is re-entrant,
      // as inserting <preference> elements into the DOM sometimes triggers a call
      // back into this function.  And since this function is also asynchronous,
      // that call can enter this function before the previous run has completed,
      // which would corrupt the font menulists.  Awaiting the previous call's
      // resolution avoids that fate.
      await this._selectDefaultLanguageGroupPromise;

      const kFontNameFmtSerif = "font.name.serif.%LANG%";
      const kFontNameFmtSansSerif = "font.name.sans-serif.%LANG%";
      const kFontNameListFmtSerif = "font.name-list.serif.%LANG%";
      const kFontNameListFmtSansSerif = "font.name-list.sans-serif.%LANG%";
      const kFontSizeFmtVariable = "font.size.variable.%LANG%";

      var prefs = [
        {
          format: aIsSerif ? kFontNameFmtSerif : kFontNameFmtSansSerif,
          type: "fontname",
          element: "defaultFont",
          fonttype: aIsSerif ? "serif" : "sans-serif",
        },
        {
          format: aIsSerif ? kFontNameListFmtSerif : kFontNameListFmtSansSerif,
          type: "unichar",
          element: null,
          fonttype: aIsSerif ? "serif" : "sans-serif",
        },
        {
          format: kFontSizeFmtVariable,
          type: "int",
          element: "defaultFontSize",
          fonttype: null,
        },
      ];
      for (var i = 0; i < prefs.length; ++i) {
        var preference = Preferences.get(
          prefs[i].format.replace(/%LANG%/, aLanguageGroup)
        );
        if (!preference) {
          var name = prefs[i].format.replace(/%LANG%/, aLanguageGroup);
          preference = Preferences.add({ id: name, type: prefs[i].type });
        }

        if (!prefs[i].element) {
          continue;
        }

        var element = document.getElementById(prefs[i].element);
        if (element) {
          element.setAttribute("preference", preference.id);

          if (prefs[i].fonttype) {
            await FontBuilder.buildFontList(
              aLanguageGroup,
              prefs[i].fonttype,
              element
            );
          }

          preference.setElementValue(element);
        }
      }
    })().catch(console.error);
  },

  onMigrationButtonCommand() {
    // Even though we're going to be showing the migration wizard here in
    // about:preferences, we'll delegate the call to
    // `MigrationUtils.showMigrationWizard`, as this will allow us to
    // properly measure entering the dialog from the PREFERENCES entrypoint.
    const browserWindow = window.browsingContext.topChromeWindow;

    MigrationUtils.showMigrationWizard(browserWindow, {
      entrypoint: MigrationUtils.MIGRATION_ENTRYPOINTS.PREFERENCES,
    });
  },

  /**
   * Displays the migration wizard dialog in an HTML dialog.
   */
  async showMigrationWizardDialog({ closeTabWhenDone = false } = {}) {
    let migrationWizardDialog = document.getElementById(
      "migrationWizardDialog"
    );

    if (migrationWizardDialog.open) {
      return;
    }

    await customElements.whenDefined("migration-wizard");

    // If we've been opened before, remove the old wizard and insert a
    // new one to put it back into its starting state.
    if (!migrationWizardDialog.firstElementChild) {
      let wizard = document.createElement("migration-wizard");
      wizard.toggleAttribute("dialog-mode", true);
      migrationWizardDialog.appendChild(wizard);
    }
    migrationWizardDialog.firstElementChild.requestState();

    migrationWizardDialog.addEventListener(
      "close",
      () => {
        // Let others know that the wizard is closed -- potentially because of a
        // user action within the dialog that dispatches "MigrationWizard:Close"
        // but this also covers cases like hitting Escape.
        Services.obs.notifyObservers(
          migrationWizardDialog,
          "MigrationWizard:Closed"
        );
        if (closeTabWhenDone) {
          window.close();
        }
      },
      { once: true }
    );

    migrationWizardDialog.showModal();
  },

  /**
   * Stores the original value of the spellchecking preference to enable proper
   * restoration if unchanged (since we're mapping a tristate onto a checkbox).
   */
  _storedSpellCheck: 0,

  /**
   * Returns true if any spellchecking is enabled and false otherwise, caching
   * the current value to enable proper pref restoration if the checkbox is
   * never changed.
   *
   * layout.spellcheckDefault
   * - an integer:
   *     0  disables spellchecking
   *     1  enables spellchecking, but only for multiline text fields
   *     2  enables spellchecking for all text fields
   */
  readCheckSpelling() {
    var pref = Preferences.get("layout.spellcheckDefault");
    this._storedSpellCheck = pref.value;

    return pref.value != 0;
  },

  /**
   * Returns the value of the spellchecking preference represented by UI,
   * preserving the preference's "hidden" value if the preference is
   * unchanged and represents a value not strictly allowed in UI.
   */
  writeCheckSpelling() {
    var checkbox = document.getElementById("checkSpelling");
    if (checkbox.checked) {
      if (this._storedSpellCheck == 2) {
        return 2;
      }
      return 1;
    }
    return 0;
  },

  updateDefaultPerformanceSettingsPref() {
    let defaultPerformancePref = Preferences.get(
      "browser.preferences.defaultPerformanceSettings.enabled"
    );
    let processCountPref = Preferences.get("dom.ipc.processCount");
    let accelerationPref = Preferences.get("layers.acceleration.disabled");
    if (
      processCountPref.value != processCountPref.defaultValue ||
      accelerationPref.value != accelerationPref.defaultValue
    ) {
      defaultPerformancePref.value = false;
    }
  },

  updatePerformanceSettingsBox() {
    let defaultPerformancePref = Preferences.get(
      "browser.preferences.defaultPerformanceSettings.enabled"
    );
    let performanceSettings = document.getElementById("performanceSettings");
    let processCountPref = Preferences.get("dom.ipc.processCount");
    if (defaultPerformancePref.value) {
      let accelerationPref = Preferences.get("layers.acceleration.disabled");
      // Unset the value so process count will be decided by the platform.
      processCountPref.value = processCountPref.defaultValue;
      accelerationPref.value = accelerationPref.defaultValue;
      performanceSettings.hidden = true;
    } else {
      performanceSettings.hidden = false;
    }
  },

  buildContentProcessCountMenuList() {
    if (Services.appinfo.fissionAutostart) {
      document.getElementById("limitContentProcess").hidden = true;
      document.getElementById("contentProcessCount").hidden = true;
      document.getElementById("contentProcessCountEnabledDescription").hidden =
        true;
      document.getElementById("contentProcessCountDisabledDescription").hidden =
        true;
      return;
    }
    if (Services.appinfo.browserTabsRemoteAutostart) {
      let processCountPref = Preferences.get("dom.ipc.processCount");
      let defaultProcessCount = processCountPref.defaultValue;

      let contentProcessCount =
        document.querySelector(`#contentProcessCount > menupopup >
                                menuitem[value="${defaultProcessCount}"]`);

      document.l10n.setAttributes(
        contentProcessCount,
        "performance-default-content-process-count",
        { num: defaultProcessCount }
      );

      document.getElementById("limitContentProcess").disabled = false;
      document.getElementById("contentProcessCount").disabled = false;
      document.getElementById("contentProcessCountEnabledDescription").hidden =
        false;
      document.getElementById("contentProcessCountDisabledDescription").hidden =
        true;
    } else {
      document.getElementById("limitContentProcess").disabled = true;
      document.getElementById("contentProcessCount").disabled = true;
      document.getElementById("contentProcessCountEnabledDescription").hidden =
        true;
      document.getElementById("contentProcessCountDisabledDescription").hidden =
        false;
    }
  },

  _minUpdatePrefDisableTime: 1000,
  /**
   * Selects the correct item in the update radio group
   */
  async readUpdateAutoPref() {
    if (
      AppConstants.MOZ_UPDATER &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp
    ) {
      let radiogroup = document.getElementById("updateRadioGroup");

      radiogroup.disabled = true;
      let enabled = await UpdateUtils.getAppUpdateAutoEnabled();
      radiogroup.value = enabled;
      radiogroup.disabled = false;

      this.maybeDisableBackgroundUpdateControls();
    }
  },

  /**
   * Writes the value of the automatic update radio group to the disk
   */
  async writeUpdateAutoPref() {
    if (
      AppConstants.MOZ_UPDATER &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp
    ) {
      let radiogroup = document.getElementById("updateRadioGroup");
      let updateAutoValue = radiogroup.value == "true";
      let _disableTimeOverPromise = new Promise(r =>
        setTimeout(r, this._minUpdatePrefDisableTime)
      );
      radiogroup.disabled = true;
      try {
        await UpdateUtils.setAppUpdateAutoEnabled(updateAutoValue);
        await _disableTimeOverPromise;
        radiogroup.disabled = false;
      } catch (error) {
        console.error(error);
        await Promise.all([
          this.readUpdateAutoPref(),
          this.reportUpdatePrefWriteError(),
        ]);
        return;
      }

      this.maybeDisableBackgroundUpdateControls();

      // If the value was changed to false the user should be given the option
      // to discard an update if there is one.
      if (!updateAutoValue) {
        await this.checkUpdateInProgress();
      }
      // For tests:
      radiogroup.dispatchEvent(new CustomEvent("ProcessedUpdatePrefChange"));
    }
  },

  isBackgroundUpdateUIAvailable() {
    return (
      AppConstants.MOZ_UPDATE_AGENT &&
      // This UI controls a per-installation pref. It won't necessarily work
      // properly if per-installation prefs aren't supported.
      UpdateUtils.PER_INSTALLATION_PREFS_SUPPORTED &&
      (!Services.policies || Services.policies.isAllowed("appUpdate")) &&
      !gIsPackagedApp &&
      !UpdateUtils.appUpdateSettingIsLocked("app.update.background.enabled")
    );
  },

  maybeDisableBackgroundUpdateControls() {
    if (this.isBackgroundUpdateUIAvailable()) {
      let radiogroup = document.getElementById("updateRadioGroup");
      let updateAutoEnabled = radiogroup.value == "true";

      // This control is only active if auto update is enabled.
      document.getElementById("backgroundUpdate").disabled = !updateAutoEnabled;
    }
  },

  async readBackgroundUpdatePref() {
    const prefName = "app.update.background.enabled";
    if (this.isBackgroundUpdateUIAvailable()) {
      let backgroundCheckbox = document.getElementById("backgroundUpdate");

      // When the page first loads, the checkbox is unchecked until we finish
      // reading the config file from the disk. But, ideally, we don't want to
      // give the user the impression that this setting has somehow gotten
      // turned off and they need to turn it back on. We also don't want the
      // user interacting with the control, expecting a particular behavior, and
      // then have the read complete and change the control in an unexpected
      // way. So we disable the control while we are reading.
      // The only entry points for this function are page load and user
      // interaction with the control. By disabling the control to prevent
      // further user interaction, we prevent the possibility of entering this
      // function a second time while we are still reading.
      backgroundCheckbox.disabled = true;

      // If we haven't already done this, it might result in the effective value
      // of the Background Update pref changing. Thus, we should do it before
      // we tell the user what value this pref has.
      await BackgroundUpdate.ensureExperimentToRolloutTransitionPerformed();

      let enabled = await UpdateUtils.readUpdateConfigSetting(prefName);
      backgroundCheckbox.checked = enabled;
      this.maybeDisableBackgroundUpdateControls();
    }
  },

  async writeBackgroundUpdatePref() {
    const prefName = "app.update.background.enabled";
    if (this.isBackgroundUpdateUIAvailable()) {
      let backgroundCheckbox = document.getElementById("backgroundUpdate");
      backgroundCheckbox.disabled = true;
      let backgroundUpdateEnabled = backgroundCheckbox.checked;
      try {
        await UpdateUtils.writeUpdateConfigSetting(
          prefName,
          backgroundUpdateEnabled
        );
      } catch (error) {
        console.error(error);
        await this.readBackgroundUpdatePref();
        await this.reportUpdatePrefWriteError();
        return;
      }

      this.maybeDisableBackgroundUpdateControls();
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

  /**
   * Displays the history of installed updates.
   */
  showUpdates() {
    gSubDialog.open("chrome://mozapps/content/update/history.xhtml");
  },

  destroy() {
    window.removeEventListener("unload", this);
    Services.prefs.removeObserver(PREF_CONTAINERS_EXTENSION, this);
    Services.obs.removeObserver(this, AUTO_UPDATE_CHANGED_TOPIC);
    Services.obs.removeObserver(this, BACKGROUND_UPDATE_CHANGED_TOPIC);

    // Clean up the TranslationsView instance if it exists
    if (this._translationsView) {
      this._translationsView.destroy();
      this._translationsView = null;
    }

    AppearanceChooser.destroy();
  },

  // nsISupports

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  // nsIObserver

  async observe(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed") {
      if (aData == PREF_CONTAINERS_EXTENSION) {
        this.readBrowserContainersCheckbox();
        return;
      }
      // Rebuild the list when there are changes to preferences that influence
      // whether or not to show certain entries in the list.
      if (!this._storingAction) {
        await this._rebuildView();
      }
    } else if (aTopic == AUTO_UPDATE_CHANGED_TOPIC) {
      if (!AppConstants.MOZ_UPDATER) {
        return;
      }
      if (aData != "true" && aData != "false") {
        throw new Error("Invalid preference value for app.update.auto");
      }
      document.getElementById("updateRadioGroup").value = aData;
      this.maybeDisableBackgroundUpdateControls();
    } else if (aTopic == BACKGROUND_UPDATE_CHANGED_TOPIC) {
      if (!AppConstants.MOZ_UPDATE_AGENT) {
        return;
      }
      if (aData != "true" && aData != "false") {
        throw new Error(
          "Invalid preference value for app.update.background.enabled"
        );
      }
      document.getElementById("backgroundUpdate").checked = aData == "true";
    }
  },

  // EventListener

  handleEvent(aEvent) {
    if (aEvent.type == "unload") {
      this.destroy();
      if (AppConstants.MOZ_UPDATER) {
        onUnload();
      }
    }
  },

  // Composed Model Construction

  _loadData() {
    this._loadInternalHandlers();
    this._loadApplicationHandlers();
  },

  /**
   * Load higher level internal handlers so they can be turned on/off in the
   * applications menu.
   */
  _loadInternalHandlers() {
    let internalHandlers = [new PDFHandlerInfoWrapper()];

    let enabledHandlers = Services.prefs
      .getCharPref("browser.download.viewableInternally.enabledTypes", "")
      .trim();
    if (enabledHandlers) {
      for (let ext of enabledHandlers.split(",")) {
        internalHandlers.push(
          new ViewableInternallyHandlerInfoWrapper(null, ext.trim())
        );
      }
    }
    for (let internalHandler of internalHandlers) {
      if (internalHandler.enabled) {
        this._handledTypes[internalHandler.type] = internalHandler;
      }
    }
  },

  /**
   * Load the set of handlers defined by the application datastore.
   */
  _loadApplicationHandlers() {
    for (let wrappedHandlerInfo of gHandlerService.enumerate()) {
      let type = wrappedHandlerInfo.type;

      let handlerInfoWrapper;
      if (type in this._handledTypes) {
        handlerInfoWrapper = this._handledTypes[type];
      } else {
        if (DownloadIntegration.shouldViewDownloadInternally(type)) {
          handlerInfoWrapper = new ViewableInternallyHandlerInfoWrapper(type);
        } else {
          handlerInfoWrapper = new HandlerInfoWrapper(type, wrappedHandlerInfo);
        }
        this._handledTypes[type] = handlerInfoWrapper;
      }
    }
  },

  // View Construction

  selectedHandlerListItem: null,

  _initListEventHandlers() {
    this._list.addEventListener("select", event => {
      if (event.target != this._list) {
        return;
      }

      let handlerListItem =
        this._list.selectedItem &&
        HandlerListItem.forNode(this._list.selectedItem);
      if (this.selectedHandlerListItem == handlerListItem) {
        return;
      }

      if (this.selectedHandlerListItem) {
        this.selectedHandlerListItem.showActionsMenu = false;
      }
      this.selectedHandlerListItem = handlerListItem;
      if (handlerListItem) {
        this.rebuildActionsMenu();
        handlerListItem.showActionsMenu = true;
      }
    });
  },

  async _rebuildVisibleTypes() {
    this._visibleTypes = [];

    // Map whose keys are string descriptions and values are references to the
    // first visible HandlerInfoWrapper that has this description. We use this
    // to determine whether or not to annotate descriptions with their types to
    // distinguish duplicate descriptions from each other.
    let visibleDescriptions = new Map();
    for (let type in this._handledTypes) {
      // Yield before processing each handler info object to avoid monopolizing
      // the main thread, as the objects are retrieved lazily, and retrieval
      // can be expensive on Windows.
      await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

      let handlerInfo = this._handledTypes[type];

      // We couldn't find any reason to exclude the type, so include it.
      this._visibleTypes.push(handlerInfo);

      let key = JSON.stringify(handlerInfo.description);
      let otherHandlerInfo = visibleDescriptions.get(key);
      if (!otherHandlerInfo) {
        // This is the first type with this description that we encountered
        // while rebuilding the _visibleTypes array this time. Make sure the
        // flag is reset so we won't add the type to the description.
        handlerInfo.disambiguateDescription = false;
        visibleDescriptions.set(key, handlerInfo);
      } else {
        // There is at least another type with this description. Make sure we
        // add the type to the description on both HandlerInfoWrapper objects.
        handlerInfo.disambiguateDescription = true;
        otherHandlerInfo.disambiguateDescription = true;
      }
    }
  },

  async _rebuildView() {
    let lastSelectedType =
      this.selectedHandlerListItem &&
      this.selectedHandlerListItem.handlerInfoWrapper.type;
    this.selectedHandlerListItem = null;

    // Clear the list of entries.
    this._list.textContent = "";

    var visibleTypes = this._visibleTypes;

    let items = visibleTypes.map(
      visibleType => new HandlerListItem(visibleType)
    );
    let itemsFragment = document.createDocumentFragment();
    let lastSelectedItem;
    for (let item of items) {
      item.createNode(itemsFragment);
      if (item.handlerInfoWrapper.type == lastSelectedType) {
        lastSelectedItem = item;
      }
    }

    for (let item of items) {
      item.setupNode();
      this.rebuildActionsMenu(item.node, item.handlerInfoWrapper);
      item.refreshAction();
    }

    // If the user is filtering the list, then only show matching types.
    // If we filter, we need to first localize the fragment, to
    // be able to filter by localized values.
    if (this._filter.value) {
      await document.l10n.translateFragment(itemsFragment);

      this._filterView(itemsFragment);

      document.l10n.pauseObserving();
      this._list.appendChild(itemsFragment);
      document.l10n.resumeObserving();
    } else {
      // Otherwise we can just append the fragment and it'll
      // get localized via the Mutation Observer.
      this._list.appendChild(itemsFragment);
    }

    if (lastSelectedItem) {
      this._list.selectedItem = lastSelectedItem.node;
    }
  },

  /**
   * Whether or not the given handler app is valid.
   *
   * @param aHandlerApp {nsIHandlerApp} the handler app in question
   *
   * @returns {boolean} whether or not it's valid
   */
  isValidHandlerApp(aHandlerApp) {
    if (!aHandlerApp) {
      return false;
    }

    if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
      return this._isValidHandlerExecutable(aHandlerApp.executable);
    }

    if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
      return aHandlerApp.uriTemplate;
    }

    if (aHandlerApp instanceof Ci.nsIGIOMimeApp) {
      return aHandlerApp.command;
    }
    if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
      return aHandlerApp.id;
    }

    return false;
  },

  _isValidHandlerExecutable(aExecutable) {
    let leafName;
    if (AppConstants.platform == "win") {
      leafName = `${AppConstants.MOZ_APP_NAME}.exe`;
    } else if (AppConstants.platform == "macosx") {
      leafName = AppConstants.MOZ_MACBUNDLE_NAME;
    } else {
      leafName = `${AppConstants.MOZ_APP_NAME}-bin`;
    }
    return (
      aExecutable &&
      aExecutable.exists() &&
      aExecutable.isExecutable() &&
      // XXXben - we need to compare this with the running instance executable
      //          just don't know how to do that via script...
      // XXXmano TBD: can probably add this to nsIShellService
      aExecutable.leafName != leafName
    );
  },

  /**
   * Rebuild the actions menu for the selected entry.  Gets called by
   * the richlistitem constructor when an entry in the list gets selected.
   */
  rebuildActionsMenu(
    typeItem = this._list.selectedItem,
    handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper
  ) {
    var menu = typeItem.querySelector(".actionsMenu");
    var menuPopup = menu.menupopup;

    // Clear out existing items.
    while (menuPopup.hasChildNodes()) {
      menuPopup.removeChild(menuPopup.lastChild);
    }

    let internalMenuItem;
    // Add the "Open in Firefox" option for optional internal handlers.
    if (
      handlerInfo instanceof InternalHandlerInfoWrapper &&
      !handlerInfo.preventInternalViewing
    ) {
      internalMenuItem = document.createXULElement("menuitem");
      internalMenuItem.setAttribute(
        "action",
        Ci.nsIHandlerInfo.handleInternally
      );
      document.l10n.setAttributes(internalMenuItem, "applications-open-inapp");
      internalMenuItem.setAttribute(APP_ICON_ATTR_NAME, "handleInternally");
      menuPopup.appendChild(internalMenuItem);
    }

    var askMenuItem = document.createXULElement("menuitem");
    askMenuItem.setAttribute("action", Ci.nsIHandlerInfo.alwaysAsk);
    document.l10n.setAttributes(askMenuItem, "applications-always-ask");
    askMenuItem.setAttribute(APP_ICON_ATTR_NAME, "ask");
    menuPopup.appendChild(askMenuItem);

    // Create a menu item for saving to disk.
    // Note: this option isn't available to protocol types, since we don't know
    // what it means to save a URL having a certain scheme to disk.
    if (handlerInfo.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      var saveMenuItem = document.createXULElement("menuitem");
      saveMenuItem.setAttribute("action", Ci.nsIHandlerInfo.saveToDisk);
      document.l10n.setAttributes(saveMenuItem, "applications-action-save");
      saveMenuItem.setAttribute(APP_ICON_ATTR_NAME, "save");
      menuPopup.appendChild(saveMenuItem);
    }

    // Add a separator to distinguish these items from the helper app items
    // that follow them.
    let menuseparator = document.createXULElement("menuseparator");
    menuPopup.appendChild(menuseparator);

    // Create a menu item for the OS default application, if any.
    if (handlerInfo.hasDefaultHandler) {
      var defaultMenuItem = document.createXULElement("menuitem");
      defaultMenuItem.setAttribute(
        "action",
        Ci.nsIHandlerInfo.useSystemDefault
      );
      // If an internal option is available, don't show the application
      // name for the OS default to prevent two options from appearing
      // that may both say "Firefox".
      if (internalMenuItem) {
        document.l10n.setAttributes(
          defaultMenuItem,
          "applications-use-os-default"
        );
        defaultMenuItem.setAttribute("image", ICON_URL_APP);
      } else {
        document.l10n.setAttributes(
          defaultMenuItem,
          "applications-use-app-default",
          {
            "app-name": handlerInfo.defaultDescription,
          }
        );
        defaultMenuItem.setAttribute(
          "image",
          handlerInfo.iconURLForSystemDefault
        );
      }

      menuPopup.appendChild(defaultMenuItem);
    }

    // Create menu items for possible handlers.
    let preferredApp = handlerInfo.preferredApplicationHandler;
    var possibleAppMenuItems = [];
    for (let possibleApp of handlerInfo.possibleApplicationHandlers.enumerate()) {
      if (!this.isValidHandlerApp(possibleApp)) {
        continue;
      }

      let menuItem = document.createXULElement("menuitem");
      menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
      let label;
      if (possibleApp instanceof Ci.nsILocalHandlerApp) {
        label = getFileDisplayName(possibleApp.executable);
      } else {
        label = possibleApp.name;
      }
      document.l10n.setAttributes(menuItem, "applications-use-app", {
        "app-name": label,
      });
      menuItem.setAttribute(
        "image",
        this._getIconURLForHandlerApp(possibleApp)
      );

      // Attach the handler app object to the menu item so we can use it
      // to make changes to the datastore when the user selects the item.
      menuItem.handlerApp = possibleApp;

      menuPopup.appendChild(menuItem);
      possibleAppMenuItems.push(menuItem);
    }
    // Add gio handlers
    if (gGIOService) {
      var gioApps = gGIOService.getAppsForURIScheme(handlerInfo.type);
      let possibleHandlers = handlerInfo.possibleApplicationHandlers;
      for (let handler of gioApps.enumerate(Ci.nsIHandlerApp)) {
        // OS handler share the same name, it's most likely the same app, skipping...
        if (handler.name == handlerInfo.defaultDescription) {
          continue;
        }
        // Check if the handler is already in possibleHandlers
        let appAlreadyInHandlers = false;
        for (let i = possibleHandlers.length - 1; i >= 0; --i) {
          let app = possibleHandlers.queryElementAt(i, Ci.nsIHandlerApp);
          // nsGIOMimeApp::Equals is able to compare with nsILocalHandlerApp
          if (handler.equals(app)) {
            appAlreadyInHandlers = true;
            break;
          }
        }
        if (!appAlreadyInHandlers) {
          let menuItem = document.createXULElement("menuitem");
          menuItem.setAttribute("action", Ci.nsIHandlerInfo.useHelperApp);
          document.l10n.setAttributes(menuItem, "applications-use-app", {
            "app-name": handler.name,
          });
          menuItem.setAttribute(
            "image",
            this._getIconURLForHandlerApp(handler)
          );

          // Attach the handler app object to the menu item so we can use it
          // to make changes to the datastore when the user selects the item.
          menuItem.handlerApp = handler;

          menuPopup.appendChild(menuItem);
          possibleAppMenuItems.push(menuItem);
        }
      }
    }

    // Create a menu item for selecting a local application.
    let canOpenWithOtherApp = true;
    if (AppConstants.platform == "win") {
      // On Windows, selecting an application to open another application
      // would be meaningless so we special case executables.
      let executableType = Cc["@mozilla.org/mime;1"]
        .getService(Ci.nsIMIMEService)
        .getTypeFromExtension("exe");
      canOpenWithOtherApp = handlerInfo.type != executableType;
    }
    if (canOpenWithOtherApp) {
      let menuItem = document.createXULElement("menuitem");
      menuItem.className = "choose-app-item";
      menuItem.addEventListener("command", function (e) {
        gMainPane.chooseApp(e);
      });
      document.l10n.setAttributes(menuItem, "applications-use-other");
      menuPopup.appendChild(menuItem);
    }

    // Create a menu item for managing applications.
    if (possibleAppMenuItems.length) {
      let menuItem = document.createXULElement("menuseparator");
      menuPopup.appendChild(menuItem);
      menuItem = document.createXULElement("menuitem");
      menuItem.className = "manage-app-item";
      menuItem.addEventListener("command", function (e) {
        gMainPane.manageApp(e);
      });
      document.l10n.setAttributes(menuItem, "applications-manage-app");
      menuPopup.appendChild(menuItem);
    }

    // Select the item corresponding to the preferred action.  If the always
    // ask flag is set, it overrides the preferred action.  Otherwise we pick
    // the item identified by the preferred action (when the preferred action
    // is to use a helper app, we have to pick the specific helper app item).
    if (handlerInfo.alwaysAskBeforeHandling) {
      menu.selectedItem = askMenuItem;
    } else {
      // The nsHandlerInfoAction enumeration values in nsIHandlerInfo identify
      // the actions the application can take with content of various types.
      // But since we've stopped support for plugins, there's no value
      // identifying the "use plugin" action, so we use this constant instead.
      const kActionUsePlugin = 5;

      switch (handlerInfo.preferredAction) {
        case Ci.nsIHandlerInfo.handleInternally:
          if (internalMenuItem) {
            menu.selectedItem = internalMenuItem;
          } else {
            console.error("No menu item defined to set!");
          }
          break;
        case Ci.nsIHandlerInfo.useSystemDefault:
          // We might not have a default item if we're not aware of an
          // OS-default handler for this type:
          menu.selectedItem = defaultMenuItem || askMenuItem;
          break;
        case Ci.nsIHandlerInfo.useHelperApp:
          if (preferredApp) {
            let preferredItem = possibleAppMenuItems.find(v =>
              v.handlerApp.equals(preferredApp)
            );
            if (preferredItem) {
              menu.selectedItem = preferredItem;
            } else {
              // This shouldn't happen, but let's make sure we end up with a
              // selected item:
              let possible = possibleAppMenuItems
                .map(v => v.handlerApp && v.handlerApp.name)
                .join(", ");
              console.error(
                new Error(
                  `Preferred handler for ${handlerInfo.type} not in list of possible handlers!? (List: ${possible})`
                )
              );
              menu.selectedItem = askMenuItem;
            }
          }
          break;
        case kActionUsePlugin:
          // We no longer support plugins, select "ask" instead:
          menu.selectedItem = askMenuItem;
          break;
        case Ci.nsIHandlerInfo.saveToDisk:
          menu.selectedItem = saveMenuItem;
          break;
      }
    }
  },

  // Sorting & Filtering

  _sortColumn: null,

  /**
   * Sort the list when the user clicks on a column header.
   */
  sort(event) {
    if (event.button != 0) {
      return;
    }
    var column = event.target;

    // If the user clicked on a new sort column, remove the direction indicator
    // from the old column.
    if (this._sortColumn && this._sortColumn != column) {
      this._sortColumn.removeAttribute("sortDirection");
    }

    this._sortColumn = column;

    // Set (or switch) the sort direction indicator.
    if (column.getAttribute("sortDirection") == "ascending") {
      column.setAttribute("sortDirection", "descending");
    } else {
      column.setAttribute("sortDirection", "ascending");
    }

    this._sortListView();
  },

  async _sortListView() {
    if (!this._sortColumn) {
      return;
    }
    let comp = new Services.intl.Collator(undefined, {
      usage: "sort",
    });

    await document.l10n.translateFragment(this._list);
    let items = Array.from(this._list.children);

    let textForNode;
    if (this._sortColumn.getAttribute("value") === "type") {
      textForNode = n => n.querySelector(".typeDescription").textContent;
    } else {
      textForNode = n => n.querySelector(".actionsMenu").getAttribute("label");
    }

    let sortDir = this._sortColumn.getAttribute("sortDirection");
    let multiplier = sortDir == "descending" ? -1 : 1;
    items.sort(
      (a, b) => multiplier * comp.compare(textForNode(a), textForNode(b))
    );

    // Re-append items in the correct order:
    items.forEach(item => this._list.appendChild(item));
  },

  _filterView(frag = this._list) {
    const filterValue = this._filter.value.toLowerCase();
    for (let elem of frag.children) {
      const typeDescription =
        elem.querySelector(".typeDescription").textContent;
      const actionDescription = elem
        .querySelector(".actionDescription")
        .getAttribute("value");
      elem.hidden =
        !typeDescription.toLowerCase().includes(filterValue) &&
        !actionDescription.toLowerCase().includes(filterValue);
    }
  },

  /**
   * Filter the list when the user enters a filter term into the filter field.
   */
  filter() {
    this._rebuildView(); // FIXME: Should this be await since bug 1508156?
  },

  focusFilterBox() {
    this._filter.focus();
    this._filter.select();
  },

  // Changes

  // Whether or not we are currently storing the action selected by the user.
  // We use this to suppress notification-triggered updates to the list when
  // we make changes that may spawn such updates.
  // XXXgijs: this was definitely necessary when we changed feed preferences
  // from within _storeAction and its calltree. Now, it may still be
  // necessary, to avoid calling _rebuildView. bug 1499350 has more details.
  _storingAction: false,

  onSelectAction(aActionItem) {
    this._storingAction = true;

    try {
      this._storeAction(aActionItem);
    } finally {
      this._storingAction = false;
    }
  },

  _storeAction(aActionItem) {
    var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

    let action = parseInt(aActionItem.getAttribute("action"));

    // Set the preferred application handler.
    // We leave the existing preferred app in the list when we set
    // the preferred action to something other than useHelperApp so that
    // legacy datastores that don't have the preferred app in the list
    // of possible apps still include the preferred app in the list of apps
    // the user can choose to handle the type.
    if (action == Ci.nsIHandlerInfo.useHelperApp) {
      handlerInfo.preferredApplicationHandler = aActionItem.handlerApp;
    }

    // Set the "always ask" flag.
    if (action == Ci.nsIHandlerInfo.alwaysAsk) {
      handlerInfo.alwaysAskBeforeHandling = true;
    } else {
      handlerInfo.alwaysAskBeforeHandling = false;
    }

    // Set the preferred action.
    handlerInfo.preferredAction = action;

    handlerInfo.store();

    // Update the action label and image to reflect the new preferred action.
    this.selectedHandlerListItem.refreshAction();
  },

  manageApp(aEvent) {
    // Don't let the normal "on select action" handler get this event,
    // as we handle it specially ourselves.
    aEvent.stopPropagation();

    var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

    let onComplete = () => {
      // Rebuild the actions menu so that we revert to the previous selection,
      // or "Always ask" if the previous default application has been removed
      this.rebuildActionsMenu();

      // update the richlistitem too. Will be visible when selecting another row
      this.selectedHandlerListItem.refreshAction();
    };

    gSubDialog.open(
      "chrome://browser/content/preferences/dialogs/applicationManager.xhtml",
      { features: "resizable=no", closingCallback: onComplete },
      handlerInfo
    );
  },

  async chooseApp(aEvent) {
    // Don't let the normal "on select action" handler get this event,
    // as we handle it specially ourselves.
    aEvent.stopPropagation();

    var handlerApp;
    let chooseAppCallback = aHandlerApp => {
      // Rebuild the actions menu whether the user picked an app or canceled.
      // If they picked an app, we want to add the app to the menu and select it.
      // If they canceled, we want to go back to their previous selection.
      this.rebuildActionsMenu();

      // If the user picked a new app from the menu, select it.
      if (aHandlerApp) {
        let typeItem = this._list.selectedItem;
        let actionsMenu = typeItem.querySelector(".actionsMenu");
        let menuItems = actionsMenu.menupopup.childNodes;
        for (let i = 0; i < menuItems.length; i++) {
          let menuItem = menuItems[i];
          if (menuItem.handlerApp && menuItem.handlerApp.equals(aHandlerApp)) {
            actionsMenu.selectedIndex = i;
            this.onSelectAction(menuItem);
            break;
          }
        }
      }
    };

    if (AppConstants.platform == "win") {
      var params = {};
      var handlerInfo = this.selectedHandlerListItem.handlerInfoWrapper;

      params.mimeInfo = handlerInfo.wrappedHandlerInfo;
      params.title = await document.l10n.formatValue(
        "applications-select-helper"
      );
      if ("id" in handlerInfo.description) {
        params.description = await document.l10n.formatValue(
          handlerInfo.description.id,
          handlerInfo.description.args
        );
      } else {
        params.description = handlerInfo.typeDescription.raw;
      }
      params.filename = null;
      params.handlerApp = null;

      let onAppSelected = () => {
        if (this.isValidHandlerApp(params.handlerApp)) {
          handlerApp = params.handlerApp;

          // Add the app to the type's list of possible handlers.
          handlerInfo.addPossibleApplicationHandler(handlerApp);
        }

        chooseAppCallback(handlerApp);
      };

      gSubDialog.open(
        "chrome://global/content/appPicker.xhtml",
        { closingCallback: onAppSelected },
        params
      );
    } else {
      let winTitle = await document.l10n.formatValue(
        "applications-select-helper"
      );
      let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
      let fpCallback = aResult => {
        if (
          aResult == Ci.nsIFilePicker.returnOK &&
          fp.file &&
          this._isValidHandlerExecutable(fp.file)
        ) {
          handlerApp = Cc[
            "@mozilla.org/uriloader/local-handler-app;1"
          ].createInstance(Ci.nsILocalHandlerApp);
          handlerApp.name = getFileDisplayName(fp.file);
          handlerApp.executable = fp.file;

          // Add the app to the type's list of possible handlers.
          let handler = this.selectedHandlerListItem.handlerInfoWrapper;
          handler.addPossibleApplicationHandler(handlerApp);

          chooseAppCallback(handlerApp);
        }
      };

      // Prompt the user to pick an app.  If they pick one, and it's a valid
      // selection, then add it to the list of possible handlers.
      fp.init(window.browsingContext, winTitle, Ci.nsIFilePicker.modeOpen);
      fp.appendFilters(Ci.nsIFilePicker.filterApps);
      fp.open(fpCallback);
    }
  },

  _getIconURLForHandlerApp(aHandlerApp) {
    if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
      return this._getIconURLForFile(aHandlerApp.executable);
    }

    if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
      return this._getIconURLForWebApp(aHandlerApp.uriTemplate);
    }

    if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
      return this._getIconURLForAppId(aHandlerApp.id);
    }

    // We know nothing about other kinds of handler apps.
    return "";
  },

  _getIconURLForAppId(aAppId) {
    return "moz-icon://" + aAppId + "?size=16";
  },

  _getIconURLForFile(aFile) {
    var fph = Services.io
      .getProtocolHandler("file")
      .QueryInterface(Ci.nsIFileProtocolHandler);
    var urlSpec = fph.getURLSpecFromActualFile(aFile);

    return "moz-icon://" + urlSpec + "?size=16";
  },

  _getIconURLForWebApp(aWebAppURITemplate) {
    var uri = Services.io.newURI(aWebAppURITemplate);

    // Unfortunately we can't use the favicon service to get the favicon,
    // because the service looks in the annotations table for a record with
    // the exact URL we give it, and users won't have such records for URLs
    // they don't visit, and users won't visit the web app's URL template,
    // they'll only visit URLs derived from that template (i.e. with %s
    // in the template replaced by the URL of the content being handled).

    if (
      /^https?$/.test(uri.scheme) &&
      Services.prefs.getBoolPref("browser.chrome.site_icons")
    ) {
      return uri.prePath + "/favicon.ico";
    }

    return "";
  },

  // DOWNLOADS

  /*
   * Preferences:
   *
   * browser.download.useDownloadDir - bool
   *   True - Save files directly to the folder configured via the
   *   browser.download.folderList preference.
   *   False - Always ask the user where to save a file and default to
   *   browser.download.lastDir when displaying a folder picker dialog.
   * browser.download.always_ask_before_handling_new_types - bool
   *   Defines the default behavior for new file handlers.
   *   True - When downloading a file that doesn't match any existing
   *   handlers, ask the user whether to save or open the file.
   *   False - Save the file. The user can change the default action in
   *   the Applications section in the preferences UI.
   * browser.download.dir - local file handle
   *   A local folder the user may have selected for downloaded files to be
   *   saved. Migration of other browser settings may also set this path.
   *   This folder is enabled when folderList equals 2.
   * browser.download.lastDir - local file handle
   *   May contain the last folder path accessed when the user browsed
   *   via the file save-as dialog. (see contentAreaUtils.js)
   * browser.download.folderList - int
   *   Indicates the location users wish to save downloaded files too.
   *   It is also used to display special file labels when the default
   *   download location is either the Desktop or the Downloads folder.
   *   Values:
   *     0 - The desktop is the default download location.
   *     1 - The system's downloads folder is the default download location.
   *     2 - The default download location is elsewhere as specified in
   *         browser.download.dir.
   * browser.download.downloadDir
   *   deprecated.
   * browser.download.defaultFolder
   *   deprecated.
   */

  /**
   * Disables the downloads folder field and Browse button if the default
   * download directory pref is locked (e.g., by the DownloadDirectory or
   * DefaultDownloadDirectory policies)
   */
  readUseDownloadDir() {
    document.getElementById("downloadFolder").disabled =
      document.getElementById("chooseFolder").disabled =
      document.getElementById("saveTo").disabled =
        Preferences.get("browser.download.dir").locked ||
        Preferences.get("browser.download.folderList").locked;
    // don't override the preference's value in UI
    return undefined;
  },

  /**
   * Displays a file picker in which the user can choose the location where
   * downloads are automatically saved, updating preferences and UI in
   * response to the choice, if one is made.
   */
  chooseFolder() {
    return this.chooseFolderTask().catch(console.error);
  },
  async chooseFolderTask() {
    let [title] = await document.l10n.formatValues([
      { id: "choose-download-folder-title" },
    ]);
    let folderListPref = Preferences.get("browser.download.folderList");
    let currentDirPref = await this._indexToFolder(folderListPref.value);
    let defDownloads = await this._indexToFolder(1);
    let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);

    fp.init(window.browsingContext, title, Ci.nsIFilePicker.modeGetFolder);
    fp.appendFilters(Ci.nsIFilePicker.filterAll);
    // First try to open what's currently configured
    if (currentDirPref && currentDirPref.exists()) {
      fp.displayDirectory = currentDirPref;
    } else if (defDownloads && defDownloads.exists()) {
      // Try the system's download dir
      fp.displayDirectory = defDownloads;
    } else {
      // Fall back to Desktop
      fp.displayDirectory = await this._indexToFolder(0);
    }

    let result = await new Promise(resolve => fp.open(resolve));
    if (result != Ci.nsIFilePicker.returnOK) {
      return;
    }

    let downloadDirPref = Preferences.get("browser.download.dir");
    downloadDirPref.value = fp.file;
    folderListPref.value = await this._folderToIndex(fp.file);
    // Note, the real prefs will not be updated yet, so dnld manager's
    // userDownloadsDirectory may not return the right folder after
    // this code executes. displayDownloadDirPref will be called on
    // the assignment above to update the UI.
  },

  /**
   * Initializes the download folder display settings based on the user's
   * preferences.
   */
  displayDownloadDirPref() {
    this.displayDownloadDirPrefTask().catch(console.error);

    // don't override the preference's value in UI
    return undefined;
  },

  async displayDownloadDirPrefTask() {
    // We're async for localization reasons, and we can get called several
    // times in the same turn of the event loop (!) because of how the
    // preferences bindings work... but the speed of localization
    // shouldn't impact what gets displayed to the user in the end - the
    // last call should always win.
    // To accomplish this, store a unique object when we enter this function,
    // and if by the end of the function that stored object has been
    // overwritten, don't update the UI but leave it to the last
    // caller to this function to do.
    let token = {};
    this._downloadDisplayToken = token;

    var downloadFolder = document.getElementById("downloadFolder");

    let folderIndex = Preferences.get("browser.download.folderList").value;
    // For legacy users using cloudstorage pref with folderIndex as 3 (See bug 1751093),
    // compute folderIndex using the current directory pref
    if (folderIndex == 3) {
      let currentDirPref = Preferences.get("browser.download.dir");
      folderIndex = currentDirPref.value
        ? await this._folderToIndex(currentDirPref.value)
        : 1;
    }

    // Display a 'pretty' label or the path in the UI.
    let { folderDisplayName, file } =
      await this._getSystemDownloadFolderDetails(folderIndex);
    // Figure out an icon url:
    let fph = Services.io
      .getProtocolHandler("file")
      .QueryInterface(Ci.nsIFileProtocolHandler);
    let iconUrlSpec = fph.getURLSpecFromDir(file);

    // Ensure that the last entry to this function always wins
    // (see comment at the start of this method):
    if (this._downloadDisplayToken != token) {
      return;
    }
    // note: downloadFolder.value is not read elsewhere in the code, its only purpose is to display to the user
    downloadFolder.value = folderDisplayName;
    downloadFolder.style.backgroundImage =
      "url(moz-icon://" + iconUrlSpec + "?size=16)";
  },

  async _getSystemDownloadFolderDetails(folderIndex) {
    let downloadsDir = await this._getDownloadsFolder("Downloads");
    let desktopDir = await this._getDownloadsFolder("Desktop");
    let currentDirPref = Preferences.get("browser.download.dir");

    let file;
    let firefoxLocalizedName;
    if (folderIndex == 2 && currentDirPref.value) {
      file = currentDirPref.value;
      if (file.equals(downloadsDir)) {
        folderIndex = 1;
      } else if (file.equals(desktopDir)) {
        folderIndex = 0;
      }
    }
    switch (folderIndex) {
      case 2: // custom path, handled above.
        break;

      case 1: {
        // downloads
        file = downloadsDir;
        firefoxLocalizedName = await document.l10n.formatValues([
          { id: "downloads-folder-name" },
        ]);
        break;
      }

      case 0:
      // fall through
      default: {
        file = desktopDir;
        firefoxLocalizedName = await document.l10n.formatValues([
          { id: "desktop-folder-name" },
        ]);
      }
    }

    if (file) {
      let displayName = file.path;

      // Attempt to translate path to the path as exists on the host
      // in case the provided path comes from the document portal
      if (AppConstants.platform == "linux") {
        try {
          displayName = await file.hostPath();
        } catch (error) {
          /* ignored */
        }

        if (displayName) {
          if (displayName == downloadsDir.path) {
            firefoxLocalizedName = await document.l10n.formatValues([
              { id: "downloads-folder-name" },
            ]);
          } else if (displayName == desktopDir.path) {
            firefoxLocalizedName = await document.l10n.formatValues([
              { id: "desktop-folder-name" },
            ]);
          }
        }
      }

      if (firefoxLocalizedName) {
        let folderDisplayName, leafName;
        // Either/both of these can throw, so check for failures in both cases
        // so we don't just break display of the download pref:
        try {
          folderDisplayName = file.displayName;
        } catch (ex) {
          /* ignored */
        }
        try {
          leafName = file.leafName;
        } catch (ex) {
          /* ignored */
        }

        // If we found a localized name that's different from the leaf name,
        // use that:
        if (folderDisplayName && folderDisplayName != leafName) {
          return { file, folderDisplayName };
        }

        // Otherwise, check if we've got a localized name ourselves.
        if (firefoxLocalizedName) {
          // You can't move the system download or desktop dir on macOS,
          // so if those are in use just display them. On other platforms
          // only do so if the folder matches the localized name.
          if (
            AppConstants.platform == "macosx" ||
            leafName == firefoxLocalizedName
          ) {
            return { file, folderDisplayName: firefoxLocalizedName };
          }
        }
      }

      // If we get here, attempts to use a "pretty" name failed. Just display
      // the full path:
      // Force the left-to-right direction when displaying a custom path.
      return { file, folderDisplayName: `\u2066${displayName}\u2069` };
    }

    // Don't even have a file - fall back to desktop directory for the
    // use of the icon, and an empty label:
    file = desktopDir;
    return { file, folderDisplayName: "" };
  },

  /**
   * Returns the Downloads folder.  If aFolder is "Desktop", then the Downloads
   * folder returned is the desktop folder; otherwise, it is a folder whose name
   * indicates that it is a download folder and whose path is as determined by
   * the XPCOM directory service via the download manager's attribute
   * defaultDownloadsDirectory.
   *
   * @throws if aFolder is not "Desktop" or "Downloads"
   */
  async _getDownloadsFolder(aFolder) {
    switch (aFolder) {
      case "Desktop":
        return Services.dirsvc.get("Desk", Ci.nsIFile);
      case "Downloads": {
        let downloadsDir = await Downloads.getSystemDownloadsDirectory();
        return new FileUtils.File(downloadsDir);
      }
    }
    throw new Error(
      "ASSERTION FAILED: folder type should be 'Desktop' or 'Downloads'"
    );
  },

  /**
   * Determines the type of the given folder.
   *
   * @param   aFolder
   *          the folder whose type is to be determined
   * @returns integer
   *          0 if aFolder is the Desktop or is unspecified,
   *          1 if aFolder is the Downloads folder,
   *          2 otherwise
   */
  async _folderToIndex(aFolder) {
    if (!aFolder || aFolder.equals(await this._getDownloadsFolder("Desktop"))) {
      return 0;
    } else if (aFolder.equals(await this._getDownloadsFolder("Downloads"))) {
      return 1;
    }
    return 2;
  },

  /**
   * Converts an integer into the corresponding folder.
   *
   * @param   aIndex
   *          an integer
   * @returns the Desktop folder if aIndex == 0,
   *          the Downloads folder if aIndex == 1,
   *          the folder stored in browser.download.dir
   */
  _indexToFolder(aIndex) {
    switch (aIndex) {
      case 0:
        return this._getDownloadsFolder("Desktop");
      case 1:
        return this._getDownloadsFolder("Downloads");
    }
    var currentDirPref = Preferences.get("browser.download.dir");
    return currentDirPref.value;
  },
};

gMainPane.initialized = new Promise(res => {
  gMainPane.setInitialized = res;
});

// Utilities

function getFileDisplayName(file) {
  if (AppConstants.platform == "win") {
    if (file instanceof Ci.nsILocalFileWin) {
      try {
        return file.getVersionInfoField("FileDescription");
      } catch (e) {}
    }
  }
  if (AppConstants.platform == "macosx") {
    if (file instanceof Ci.nsILocalFileMac) {
      try {
        return file.bundleDisplayName;
      } catch (e) {}
    }
  }
  return file.leafName;
}

function getLocalHandlerApp(aFile) {
  var localHandlerApp = Cc[
    "@mozilla.org/uriloader/local-handler-app;1"
  ].createInstance(Ci.nsILocalHandlerApp);
  localHandlerApp.name = getFileDisplayName(aFile);
  localHandlerApp.executable = aFile;

  return localHandlerApp;
}

// eslint-disable-next-line no-undef
let gHandlerListItemFragment = MozXULElement.parseXULToFragment(`
  <richlistitem>
    <hbox class="typeContainer" flex="1" align="center">
      <image class="typeIcon" width="16" height="16"
              src="moz-icon://goat?size=16"/>
      <label class="typeDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionContainer" flex="1" align="center">
      <image class="actionIcon" width="16" height="16"/>
      <label class="actionDescription" flex="1" crop="end"/>
    </hbox>
    <hbox class="actionsMenuContainer" flex="1">
      <menulist class="actionsMenu" flex="1" crop="end" selectedIndex="1" aria-labelledby="actionColumn">
        <menupopup/>
      </menulist>
    </hbox>
  </richlistitem>
`);

/**
 * This is associated to <richlistitem> elements in the handlers view.
 */
class HandlerListItem {
  static forNode(node) {
    return gNodeToObjectMap.get(node);
  }

  constructor(handlerInfoWrapper) {
    this.handlerInfoWrapper = handlerInfoWrapper;
  }

  setOrRemoveAttributes(iterable) {
    for (let [selector, name, value] of iterable) {
      let node = selector ? this.node.querySelector(selector) : this.node;
      if (value) {
        node.setAttribute(name, value);
      } else {
        node.removeAttribute(name);
      }
    }
  }

  createNode(list) {
    list.appendChild(document.importNode(gHandlerListItemFragment, true));
    this.node = list.lastChild;
    gNodeToObjectMap.set(this.node, this);
  }

  setupNode() {
    this.node
      .querySelector(".actionsMenu")
      .addEventListener("command", event =>
        gMainPane.onSelectAction(event.originalTarget)
      );

    let typeDescription = this.handlerInfoWrapper.typeDescription;
    this.setOrRemoveAttributes([
      [null, "type", this.handlerInfoWrapper.type],
      [".typeIcon", "src", this.handlerInfoWrapper.smallIcon],
    ]);
    localizeElement(
      this.node.querySelector(".typeDescription"),
      typeDescription
    );
    this.showActionsMenu = false;
  }

  refreshAction() {
    let { actionIconClass } = this.handlerInfoWrapper;
    this.setOrRemoveAttributes([
      [null, APP_ICON_ATTR_NAME, actionIconClass],
      [
        ".actionIcon",
        "src",
        actionIconClass ? null : this.handlerInfoWrapper.actionIcon,
      ],
    ]);
    const selectedItem = this.node.querySelector("[selected=true]");
    if (!selectedItem) {
      console.error("No selected item for " + this.handlerInfoWrapper.type);
      return;
    }
    const { id, args } = document.l10n.getAttributes(selectedItem);
    const messageIDs = {
      "applications-action-save": "applications-action-save-label",
      "applications-always-ask": "applications-always-ask-label",
      "applications-open-inapp": "applications-open-inapp-label",
      "applications-use-app-default": "applications-use-app-default-label",
      "applications-use-app": "applications-use-app-label",
      "applications-use-os-default": "applications-use-os-default-label",
      "applications-use-other": "applications-use-other-label",
    };
    localizeElement(this.node.querySelector(".actionDescription"), {
      id: messageIDs[id],
      args,
    });
    localizeElement(this.node.querySelector(".actionsMenu"), { id, args });
  }

  set showActionsMenu(value) {
    this.setOrRemoveAttributes([
      [".actionContainer", "hidden", value],
      [".actionsMenuContainer", "hidden", !value],
    ]);
  }
}

/**
 * This API facilitates dual-model of some localization APIs which
 * may operate on raw strings of l10n id/args pairs.
 *
 * The l10n can be:
 *
 * {raw: string} - raw strings to be used as text value of the element
 * {id: string} - l10n-id
 * {id: string, args: object} - l10n-id + l10n-args
 */
function localizeElement(node, l10n) {
  if (l10n.hasOwnProperty("raw")) {
    node.removeAttribute("data-l10n-id");
    node.textContent = l10n.raw;
  } else {
    document.l10n.setAttributes(node, l10n.id, l10n.args);
  }
}

/**
 * This object wraps nsIHandlerInfo with some additional functionality
 * the Applications prefpane needs to display and allow modification of
 * the list of handled types.
 *
 * We create an instance of this wrapper for each entry we might display
 * in the prefpane, and we compose the instances from various sources,
 * including the handler service.
 *
 * We don't implement all the original nsIHandlerInfo functionality,
 * just the stuff that the prefpane needs.
 */
class HandlerInfoWrapper {
  constructor(type, handlerInfo) {
    this.type = type;
    this.wrappedHandlerInfo = handlerInfo;
    this.disambiguateDescription = false;
  }

  get description() {
    if (this.wrappedHandlerInfo.description) {
      return { raw: this.wrappedHandlerInfo.description };
    }

    if (this.primaryExtension) {
      var extension = this.primaryExtension.toUpperCase();
      return { id: "applications-file-ending", args: { extension } };
    }

    return { raw: this.type };
  }

  /**
   * Describe, in a human-readable fashion, the type represented by the given
   * handler info object.  Normally this is just the description, but if more
   * than one object presents the same description, "disambiguateDescription"
   * is set and we annotate the duplicate descriptions with the type itself
   * to help users distinguish between those types.
   */
  get typeDescription() {
    if (this.disambiguateDescription) {
      const description = this.description;
      if (description.id) {
        // Pass through the arguments:
        let { args = {} } = description;
        args.type = this.type;
        return {
          id: description.id + "-with-type",
          args,
        };
      }

      return {
        id: "applications-type-description-with-type",
        args: {
          "type-description": description.raw,
          type: this.type,
        },
      };
    }

    return this.description;
  }

  get actionIconClass() {
    if (this.alwaysAskBeforeHandling) {
      return "ask";
    }

    switch (this.preferredAction) {
      case Ci.nsIHandlerInfo.saveToDisk:
        return "save";

      case Ci.nsIHandlerInfo.handleInternally:
        if (this instanceof InternalHandlerInfoWrapper) {
          return "handleInternally";
        }
        break;
    }

    return "";
  }

  get actionIcon() {
    switch (this.preferredAction) {
      case Ci.nsIHandlerInfo.useSystemDefault:
        return this.iconURLForSystemDefault;

      case Ci.nsIHandlerInfo.useHelperApp: {
        let preferredApp = this.preferredApplicationHandler;
        if (gMainPane.isValidHandlerApp(preferredApp)) {
          return gMainPane._getIconURLForHandlerApp(preferredApp);
        }
      }
      // This should never happen, but if preferredAction is set to some weird
      // value, then fall back to the generic application icon.
      // Explicit fall-through
      default:
        return ICON_URL_APP;
    }
  }

  get iconURLForSystemDefault() {
    // Handler info objects for MIME types on some OSes implement a property bag
    // interface from which we can get an icon for the default app, so if we're
    // dealing with a MIME type on one of those OSes, then try to get the icon.
    if (
      this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
      this.wrappedHandlerInfo instanceof Ci.nsIPropertyBag
    ) {
      try {
        let url = this.wrappedHandlerInfo.getProperty(
          "defaultApplicationIconURL"
        );
        if (url) {
          return url + "?size=16";
        }
      } catch (ex) {}
    }

    // If this isn't a MIME type object on an OS that supports retrieving
    // the icon, or if we couldn't retrieve the icon for some other reason,
    // then use a generic icon.
    return ICON_URL_APP;
  }

  get preferredApplicationHandler() {
    return this.wrappedHandlerInfo.preferredApplicationHandler;
  }

  set preferredApplicationHandler(aNewValue) {
    this.wrappedHandlerInfo.preferredApplicationHandler = aNewValue;

    // Make sure the preferred handler is in the set of possible handlers.
    if (aNewValue) {
      this.addPossibleApplicationHandler(aNewValue);
    }
  }

  get possibleApplicationHandlers() {
    return this.wrappedHandlerInfo.possibleApplicationHandlers;
  }

  addPossibleApplicationHandler(aNewHandler) {
    for (let app of this.possibleApplicationHandlers.enumerate()) {
      if (app.equals(aNewHandler)) {
        return;
      }
    }
    this.possibleApplicationHandlers.appendElement(aNewHandler);
  }

  removePossibleApplicationHandler(aHandler) {
    var defaultApp = this.preferredApplicationHandler;
    if (defaultApp && aHandler.equals(defaultApp)) {
      // If the app we remove was the default app, we must make sure
      // it won't be used anymore
      this.alwaysAskBeforeHandling = true;
      this.preferredApplicationHandler = null;
    }

    var handlers = this.possibleApplicationHandlers;
    for (var i = 0; i < handlers.length; ++i) {
      var handler = handlers.queryElementAt(i, Ci.nsIHandlerApp);
      if (handler.equals(aHandler)) {
        handlers.removeElementAt(i);
        break;
      }
    }
  }

  get hasDefaultHandler() {
    return this.wrappedHandlerInfo.hasDefaultHandler;
  }

  get defaultDescription() {
    return this.wrappedHandlerInfo.defaultDescription;
  }

  // What to do with content of this type.
  get preferredAction() {
    // If the action is to use a helper app, but we don't have a preferred
    // handler app, then switch to using the system default, if any; otherwise
    // fall back to saving to disk, which is the default action in nsMIMEInfo.
    // Note: "save to disk" is an invalid value for protocol info objects,
    // but the alwaysAskBeforeHandling getter will detect that situation
    // and always return true in that case to override this invalid value.
    if (
      this.wrappedHandlerInfo.preferredAction ==
        Ci.nsIHandlerInfo.useHelperApp &&
      !gMainPane.isValidHandlerApp(this.preferredApplicationHandler)
    ) {
      if (this.wrappedHandlerInfo.hasDefaultHandler) {
        return Ci.nsIHandlerInfo.useSystemDefault;
      }
      return Ci.nsIHandlerInfo.saveToDisk;
    }

    return this.wrappedHandlerInfo.preferredAction;
  }

  set preferredAction(aNewValue) {
    this.wrappedHandlerInfo.preferredAction = aNewValue;
  }

  get alwaysAskBeforeHandling() {
    // If this is a protocol type and the preferred action is "save to disk",
    // which is invalid for such types, then return true here to override that
    // action.  This could happen when the preferred action is to use a helper
    // app, but the preferredApplicationHandler is invalid, and there isn't
    // a default handler, so the preferredAction getter returns save to disk
    // instead.
    if (
      !(this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) &&
      this.preferredAction == Ci.nsIHandlerInfo.saveToDisk
    ) {
      return true;
    }

    return this.wrappedHandlerInfo.alwaysAskBeforeHandling;
  }

  set alwaysAskBeforeHandling(aNewValue) {
    this.wrappedHandlerInfo.alwaysAskBeforeHandling = aNewValue;
  }

  // The primary file extension associated with this type, if any.
  get primaryExtension() {
    try {
      if (
        this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
        this.wrappedHandlerInfo.primaryExtension
      ) {
        return this.wrappedHandlerInfo.primaryExtension;
      }
    } catch (ex) {}

    return null;
  }

  store() {
    gHandlerService.store(this.wrappedHandlerInfo);
  }

  get smallIcon() {
    return this._getIcon(16);
  }

  _getIcon(aSize) {
    if (this.primaryExtension) {
      return "moz-icon://goat." + this.primaryExtension + "?size=" + aSize;
    }

    if (this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      return "moz-icon://goat?size=" + aSize + "&contentType=" + this.type;
    }

    // FIXME: consider returning some generic icon when we can't get a URL for
    // one (for example in the case of protocol schemes).  Filed as bug 395141.
    return null;
  }
}

/**
 * InternalHandlerInfoWrapper provides a basic mechanism to create an internal
 * mime type handler that can be enabled/disabled in the applications preference
 * menu.
 */
class InternalHandlerInfoWrapper extends HandlerInfoWrapper {
  constructor(mimeType, extension) {
    let type = gMIMEService.getFromTypeAndExtension(mimeType, extension);
    super(mimeType || type.type, type);
  }

  // Override store so we so we can notify any code listening for registration
  // or unregistration of this handler.
  store() {
    super.store();
  }

  get preventInternalViewing() {
    return false;
  }

  get enabled() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }
}

class PDFHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  constructor() {
    super(TYPE_PDF, null);
  }

  get preventInternalViewing() {
    return Services.prefs.getBoolPref(PREF_PDFJS_DISABLED);
  }

  // PDF is always shown in the list, but the 'show internally' option is
  // hidden when the internal PDF viewer is disabled.
  get enabled() {
    return true;
  }
}

class ViewableInternallyHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  get enabled() {
    return DownloadIntegration.shouldViewDownloadInternally(this.type);
  }
}

const AppearanceChooser = {
  // NOTE: This order must match the values of the
  // layout.css.prefers-color-scheme.content-override
  // preference.
  choices: ["dark", "light", "auto"],
  chooser: null,
  radios: null,
  warning: null,

  init() {
    this.chooser = document.getElementById("web-appearance-chooser");
    this.radios = [...this.chooser.querySelectorAll("input")];
    for (let radio of this.radios) {
      radio.addEventListener("change", e => {
        let index = this.choices.indexOf(e.target.value);
        // The pref change callback will update state if needed.
        if (index >= 0) {
          Services.prefs.setIntPref(PREF_CONTENT_APPEARANCE, index);
        } else {
          // Shouldn't happen but let's do something sane...
          Services.prefs.clearUserPref(PREF_CONTENT_APPEARANCE);
        }
      });
    }

    let webAppearanceSettings = document.getElementById(
      "webAppearanceSettings"
    );
    webAppearanceSettings.addEventListener("click", this);

    this.warning = document.getElementById("web-appearance-override-warning");

    FORCED_COLORS_QUERY.addEventListener("change", this);
    Services.obs.addObserver(this, "look-and-feel-changed");
    this._update();
  },

  _update() {
    this._updateWarning();
    this._updateOptions();
  },

  handleEvent(e) {
    if (e.type == "click") {
      switch (e.target.id) {
        case "web-appearance-manage-themes-link":
          window.browsingContext.topChromeWindow.BrowserAddonUI.openAddonsMgr(
            "addons://list/theme"
          );
          e.preventDefault();
          break;
        default:
          break;
      }
    }
    this._update();
  },

  observe() {
    this._update();
  },

  destroy() {
    Services.obs.removeObserver(this, "look-and-feel-changed");
    FORCED_COLORS_QUERY.removeEventListener("change", this);
  },

  _isValueDark(value) {
    switch (value) {
      case "light":
        return false;
      case "dark":
        return true;
      case "auto":
        return Services.appinfo.contentThemeDerivedColorSchemeIsDark;
    }
    throw new Error("Unknown value");
  },

  _updateOptions() {
    let index = Services.prefs.getIntPref(PREF_CONTENT_APPEARANCE);
    if (index < 0 || index >= this.choices.length) {
      index = Services.prefs
        .getDefaultBranch(null)
        .getIntPref(PREF_CONTENT_APPEARANCE);
    }
    let value = this.choices[index];
    for (let radio of this.radios) {
      let checked = radio.value == value;
      let isDark = this._isValueDark(radio.value);

      radio.checked = checked;
      radio.closest("label").classList.toggle("dark", isDark);
    }
  },

  _updateWarning() {
    this.warning.hidden = !FORCED_COLORS_QUERY.matches;
  },
};
