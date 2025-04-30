/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
ChromeUtils.importESModule(
  "resource://gre/modules/MemoryNotificationDB.sys.mjs"
);
ChromeUtils.importESModule("resource://gre/modules/NotificationDB.sys.mjs");

// lazy module getters

ChromeUtils.defineESModuleGetters(this, {
  AMTelemetry: "resource://gre/modules/AddonManager.sys.mjs",
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AboutReaderParent: "resource:///actors/AboutReaderParent.sys.mjs",
  ActionsProviderContextualSearch:
    "resource:///modules/ActionsProviderContextualSearch.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BrowserTelemetryUtils: "resource://gre/modules/BrowserTelemetryUtils.sys.mjs",
  BrowserUIUtils: "resource:///modules/BrowserUIUtils.sys.mjs",
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CFRPageActions: "resource:///modules/asrouter/CFRPageActions.sys.mjs",
  Color: "resource://gre/modules/Color.sys.mjs",
  ContentAnalysis: "resource:///modules/ContentAnalysis.sys.mjs",
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  CustomizableUI: "resource:///modules/CustomizableUI.sys.mjs",
  DevToolsSocketStatus:
    "resource://devtools/shared/security/DevToolsSocketStatus.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  DownloadsCommon: "resource:///modules/DownloadsCommon.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  ExtensionsUI: "resource:///modules/ExtensionsUI.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  LightweightThemeConsumer:
    "resource://gre/modules/LightweightThemeConsumer.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  LoginManagerParent: "resource://gre/modules/LoginManagerParent.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  NewTabPagePreloading:
    "moz-src:///browser/components/tabbrowser/NewTabPagePreloading.sys.mjs",
  NewTabUtils: "resource://gre/modules/NewTabUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  nsContextMenu: "chrome://browser/content/nsContextMenu.sys.mjs",
  OpenInTabsUtils:
    "moz-src:///browser/components/tabbrowser/OpenInTabsUtils.sys.mjs",
  OpenSearchManager:
    "moz-src:///browser/components/search/OpenSearchManager.sys.mjs",
  PageActions: "resource:///modules/PageActions.sys.mjs",
  PageThumbs: "resource://gre/modules/PageThumbs.sys.mjs",
  PanelMultiView: "resource:///modules/PanelMultiView.sys.mjs",
  PanelView: "resource:///modules/PanelMultiView.sys.mjs",
  PictureInPicture: "resource://gre/modules/PictureInPicture.sys.mjs",
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUIUtils: "resource:///modules/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  Pocket: "chrome://pocket/content/Pocket.sys.mjs",
  PopupBlockerObserver: "resource:///modules/PopupBlockerObserver.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ProcessHangMonitor: "resource:///modules/ProcessHangMonitor.sys.mjs",
  ProfilesDatastoreService:
    "resource:///modules/profiles/ProfilesDatastoreService.sys.mjs",
  PromptUtils: "resource://gre/modules/PromptUtils.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  ResetPBMPanel: "resource:///modules/ResetPBMPanel.sys.mjs",
  SafeBrowsing: "resource://gre/modules/SafeBrowsing.sys.mjs",
  Sanitizer: "resource:///modules/Sanitizer.sys.mjs",
  SaveToPocket: "chrome://pocket/content/SaveToPocket.sys.mjs",
  ScreenshotsUtils: "resource:///modules/ScreenshotsUtils.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  SessionStartup: "resource:///modules/sessionstore/SessionStartup.sys.mjs",
  SessionStore: "resource:///modules/sessionstore/SessionStore.sys.mjs",
  SharingUtils: "resource:///modules/SharingUtils.sys.mjs",
  ShoppingSidebarParent: "resource:///actors/ShoppingSidebarParent.sys.mjs",
  ShoppingSidebarManager: "resource:///actors/ShoppingSidebarParent.sys.mjs",
  ShortcutUtils: "resource://gre/modules/ShortcutUtils.sys.mjs",
  SiteDataManager: "resource:///modules/SiteDataManager.sys.mjs",
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
  SubDialog: "resource://gre/modules/SubDialog.sys.mjs",
  SubDialogManager: "resource://gre/modules/SubDialog.sys.mjs",
  TabCrashHandler: "resource:///modules/ContentCrashHandlers.sys.mjs",
  TabsSetupFlowManager:
    "resource:///modules/firefox-view-tabs-setup-manager.sys.mjs",
  TaskbarTabUI: "resource:///modules/taskbartabs/TaskbarTabUI.sys.mjs",
  TelemetryEnvironment: "resource://gre/modules/TelemetryEnvironment.sys.mjs",
  ToolbarContextMenu: "resource:///modules/ToolbarContextMenu.sys.mjs",
  ToolbarDropHandler: "resource:///modules/ToolbarDropHandler.sys.mjs",
  ToolbarIconColor: "moz-src:///browser/themes/ToolbarIconColor.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
  UITour: "moz-src:///browser/components/uitour/UITour.sys.mjs",
  UpdateUtils: "resource://gre/modules/UpdateUtils.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
  UrlbarInput: "resource:///modules/UrlbarInput.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProviderSearchTips:
    "resource:///modules/UrlbarProviderSearchTips.sys.mjs",
  UrlbarTokenizer: "resource:///modules/UrlbarTokenizer.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
  UrlbarValueFormatter: "resource:///modules/UrlbarValueFormatter.sys.mjs",
  Weave: "resource://services-sync/main.sys.mjs",
  WebNavigationFrames: "resource://gre/modules/WebNavigationFrames.sys.mjs",
  webrtcUI: "resource:///modules/webrtcUI.sys.mjs",
  WebsiteFilter: "resource:///modules/policies/WebsiteFilter.sys.mjs",
  ZoomUI: "resource:///modules/ZoomUI.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

XPCOMUtils.defineLazyScriptGetter(
  this,
  ["BrowserCommands", "kSkipCacheFlags"],
  "chrome://browser/content/browser-commands.js"
);

XPCOMUtils.defineLazyScriptGetter(
  this,
  "PlacesTreeView",
  "chrome://browser/content/places/treeView.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["PlacesInsertionPoint", "PlacesController", "PlacesControllerDragHelper"],
  "chrome://browser/content/places/controller.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "PrintUtils",
  "chrome://global/content/printUtils.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "ZoomManager",
  "chrome://global/content/viewZoomOverlay.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "FullZoom",
  "chrome://browser/content/tabbrowser/browser-fullZoom.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "PanelUI",
  "chrome://browser/content/customizableui/panelUI.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gViewSourceUtils",
  "chrome://global/content/viewSourceUtils.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gTabsPanel",
  "chrome://browser/content/tabbrowser/browser-allTabsMenu.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  [
    "BrowserAddonUI",
    "gExtensionsNotifications",
    "gUnifiedExtensions",
    "gXPInstallObserver",
  ],
  "chrome://browser/content/browser-addons.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "ctrlTab",
  "chrome://browser/content/tabbrowser/browser-ctrlTab.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["CustomizationHandler", "AutoHideMenubar"],
  "chrome://browser/content/browser-customization.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["PointerLock", "FullScreen"],
  "chrome://browser/content/browser-fullScreenAndPointerLock.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gIdentityHandler",
  "chrome://browser/content/browser-siteIdentity.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gPermissionPanel",
  "chrome://browser/content/browser-sitePermissionPanel.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "SelectTranslationsPanel",
  "chrome://browser/content/translations/selectTranslationsPanel.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "FullPageTranslationsPanel",
  "chrome://browser/content/translations/fullPageTranslationsPanel.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gProtectionsHandler",
  "chrome://browser/content/browser-siteProtections.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["gGestureSupport", "gHistorySwipeAnimation"],
  "chrome://browser/content/browser-gestureSupport.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gSafeBrowsing",
  "chrome://browser/content/browser-safebrowsing.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gSync",
  "chrome://browser/content/browser-sync.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gBrowserThumbnails",
  "chrome://browser/content/browser-thumbnails.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  [
    "DownloadsPanel",
    "DownloadsOverlayLoader",
    "DownloadsView",
    "DownloadsViewUI",
    "DownloadsViewController",
    "DownloadsSummary",
    "DownloadsFooter",
    "DownloadsBlockedSubview",
  ],
  "chrome://browser/content/downloads/downloads.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["DownloadsButton", "DownloadsIndicatorView"],
  "chrome://browser/content/downloads/indicator.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gEditItemOverlay",
  "chrome://browser/content/places/editBookmark.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gGfxUtils",
  "chrome://browser/content/browser-graphics-utils.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "pktUI",
  "chrome://pocket/content/pktUI.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "ToolbarKeyboardNavigator",
  "chrome://browser/content/browser-toolbarKeyNav.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "A11yUtils",
  "chrome://browser/content/browser-a11yUtils.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gSharedTabWarning",
  "chrome://browser/content/browser-webrtc.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gPageStyleMenu",
  "chrome://browser/content/browser-pagestyle.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  "gProfiles",
  "chrome://browser/content/browser-profiles.js"
);

// lazy service getters

XPCOMUtils.defineLazyServiceGetters(this, {
  ContentPrefService2: [
    "@mozilla.org/content-pref/service;1",
    "nsIContentPrefService2",
  ],
  classifierService: [
    "@mozilla.org/url-classifier/dbservice;1",
    "nsIURIClassifier",
  ],
  Favicons: ["@mozilla.org/browser/favicon-service;1", "nsIFaviconService"],
  WindowsUIUtils: ["@mozilla.org/windows-ui-utils;1", "nsIWindowsUIUtils"],
  BrowserHandler: ["@mozilla.org/browser/clh;1", "nsIBrowserHandler"],
});

if (AppConstants.ENABLE_WEBDRIVER) {
  XPCOMUtils.defineLazyServiceGetter(
    this,
    "Marionette",
    "@mozilla.org/remote/marionette;1",
    "nsIMarionette"
  );

  XPCOMUtils.defineLazyServiceGetter(
    this,
    "RemoteAgent",
    "@mozilla.org/remote/agent;1",
    "nsIRemoteAgent"
  );
} else {
  this.Marionette = { running: false };
  this.RemoteAgent = { running: false };
}

ChromeUtils.defineLazyGetter(this, "RTL_UI", () => {
  return Services.locale.isAppLocaleRTL;
});
function gLocaleChangeObserver() {
  delete window.RTL_UI;
  window.RTL_UI = Services.locale.isAppLocaleRTL;
}

ChromeUtils.defineLazyGetter(this, "gBrandBundle", () => {
  return Services.strings.createBundle(
    "chrome://branding/locale/brand.properties"
  );
});

ChromeUtils.defineLazyGetter(this, "gBrowserBundle", () => {
  return Services.strings.createBundle(
    "chrome://browser/locale/browser.properties"
  );
});

ChromeUtils.defineLazyGetter(this, "gCustomizeMode", () => {
  let { CustomizeMode } = ChromeUtils.importESModule(
    "resource:///modules/CustomizeMode.sys.mjs"
  );
  return new CustomizeMode(window);
});

ChromeUtils.defineLazyGetter(this, "gNavToolbox", () => {
  return document.getElementById("navigator-toolbox");
});

ChromeUtils.defineLazyGetter(this, "gURLBar", () => {
  let urlbar = new UrlbarInput({
    textbox: document.getElementById("urlbar"),
    eventTelemetryCategory: "urlbar",
  });

  let beforeFocusOrSelect = event => {
    // In customize mode, the url bar is disabled. If a new tab is opened or the
    // user switches to a different tab, this function gets called before we've
    // finished leaving customize mode, and the url bar will still be disabled.
    // We can't focus it when it's disabled, so we need to re-run ourselves when
    // we've finished leaving customize mode.
    if (
      CustomizationHandler.isCustomizing() ||
      CustomizationHandler.isExitingCustomizeMode
    ) {
      gNavToolbox.addEventListener(
        "aftercustomization",
        () => {
          if (event.type == "beforeselect") {
            gURLBar.select();
          } else {
            gURLBar.focus();
          }
        },
        {
          once: true,
        }
      );
      event.preventDefault();
      return;
    }

    if (window.fullScreen) {
      FullScreen.showNavToolbox();
    }
  };
  urlbar.addEventListener("beforefocus", beforeFocusOrSelect);
  urlbar.addEventListener("beforeselect", beforeFocusOrSelect);

  return urlbar;
});

// High priority notification bars shown at the top of the window.
ChromeUtils.defineLazyGetter(this, "gNotificationBox", () => {
  let securityDelayMS = Services.prefs.getIntPref(
    "security.notification_enable_delay"
  );

  return new MozElements.NotificationBox(element => {
    element.classList.add("global-notificationbox");
    element.setAttribute("notificationside", "top");
    element.setAttribute("prepend-notifications", true);
    // We want this before the tab notifications.
    document.getElementById("notifications-toolbar").prepend(element);
  }, securityDelayMS);
});

ChromeUtils.defineLazyGetter(this, "InlineSpellCheckerUI", () => {
  let { InlineSpellChecker } = ChromeUtils.importESModule(
    "resource://gre/modules/InlineSpellChecker.sys.mjs"
  );
  return new InlineSpellChecker();
});

ChromeUtils.defineLazyGetter(this, "PopupNotifications", () => {
  // eslint-disable-next-line no-shadow
  let { PopupNotifications } = ChromeUtils.importESModule(
    "resource://gre/modules/PopupNotifications.sys.mjs"
  );
  try {
    // Hide all PopupNotifications while the the address bar has focus,
    // including the virtual focus in the results popup, and the URL is being
    // edited or the page proxy state is invalid while async tab switching.
    let shouldSuppress = () => {
      // "Blank" pages, like about:welcome, have a pageproxystate of "invalid", but
      // popups like CFRs should not automatically be suppressed when the address
      // bar has focus on these pages as it disrupts user navigation using FN+F6.
      // See `UrlbarInput.setURI()` where pageproxystate is set to "invalid" for
      // all pages that the "isBlankPageURL" method returns true for.
      const urlBarEdited = isBlankPageURL(gBrowser.currentURI.spec)
        ? gURLBar.hasAttribute("usertyping")
        : gURLBar.getAttribute("pageproxystate") != "valid";
      return (
        (urlBarEdited && gURLBar.focused) ||
        (gURLBar.getAttribute("pageproxystate") != "valid" &&
          gBrowser.selectedBrowser._awaitingSetURI) ||
        shouldSuppressPopupNotifications()
      );
    };

    // Before a Popup is shown, check that its anchor is visible.
    // If the anchor is not visible, use one of the fallbacks.
    // If no fallbacks are visible, return null.
    const getVisibleAnchorElement = anchorElement => {
      // If the anchor element is present in the Urlbar,
      // ensure that both the anchor and page URL are visible.
      gURLBar.maybeHandleRevertFromPopup(anchorElement);
      if (anchorElement?.checkVisibility()) {
        return anchorElement;
      }
      let fallback = [
        document.getElementById("searchmode-switcher-icon"),
        document.getElementById("identity-icon"),
        gURLBar.querySelector(".urlbar-search-button"),
        document.getElementById("remote-control-icon"),
      ];
      return fallback.find(element => element?.checkVisibility()) ?? null;
    };

    return new PopupNotifications(
      gBrowser,
      document.getElementById("notification-popup"),
      document.getElementById("notification-popup-box"),
      { shouldSuppress, getVisibleAnchorElement }
    );
  } catch (ex) {
    console.error(ex);
    return null;
  }
});

ChromeUtils.defineLazyGetter(this, "MacUserActivityUpdater", () => {
  if (AppConstants.platform != "macosx") {
    return null;
  }

  return Cc["@mozilla.org/widget/macuseractivityupdater;1"].getService(
    Ci.nsIMacUserActivityUpdater
  );
});

ChromeUtils.defineLazyGetter(this, "Win7Features", () => {
  if (AppConstants.platform != "win") {
    return null;
  }

  const WINTASKBAR_CONTRACTID = "@mozilla.org/windows-taskbar;1";
  if (
    WINTASKBAR_CONTRACTID in Cc &&
    Cc[WINTASKBAR_CONTRACTID].getService(Ci.nsIWinTaskbar).available
  ) {
    let { AeroPeek } = ChromeUtils.importESModule(
      "resource:///modules/WindowsPreviewPerTab.sys.mjs"
    );
    return {
      onOpenWindow() {
        AeroPeek.onOpenWindow(window);
        this.handledOpening = true;
      },
      onCloseWindow() {
        if (this.handledOpening) {
          AeroPeek.onCloseWindow(window);
        }
      },
      handledOpening: false,
    };
  }
  return null;
});

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gToolbarKeyNavEnabled",
  "browser.toolbars.keyboard_navigation",
  false,
  (aPref, aOldVal, aNewVal) => {
    if (window.closed) {
      return;
    }
    if (aNewVal) {
      ToolbarKeyboardNavigator.init();
    } else {
      ToolbarKeyboardNavigator.uninit();
    }
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gBookmarksToolbarVisibility",
  "browser.toolbars.bookmarks.visibility",
  "newtab"
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gFxaToolbarEnabled",
  "identity.fxaccounts.toolbar.enabled",
  false,
  (aPref, aOldVal, aNewVal) => {
    updateFxaToolbarMenu(aNewVal);
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gFxaToolbarAccessed",
  "identity.fxaccounts.toolbar.accessed",
  false,
  () => {
    updateFxaToolbarMenu(gFxaToolbarEnabled);
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gAddonAbuseReportEnabled",
  "extensions.abuseReport.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gMiddleClickNewTabUsesPasteboard",
  "browser.tabs.searchclipboardfor.middleclick",
  true
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gScreenshotsDisabled",
  "extensions.screenshots.disabled",
  false,
  () => {
    Services.obs.notifyObservers(
      window,
      "toggle-screenshot-disable",
      gScreenshots.shouldScreenshotsButtonBeDisabled()
    );
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gPrintEnabled",
  "print.enabled",
  false,
  (aPref, aOldVal, aNewVal) => {
    updatePrintCommands(aNewVal);
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gScreenshotsComponentEnabled",
  "screenshots.browser.component.enabled",
  false,
  () => {
    Services.obs.notifyObservers(
      window,
      "toggle-screenshot-disable",
      gScreenshots.shouldScreenshotsButtonBeDisabled()
    );
  }
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gTranslationsEnabled",
  "browser.translations.enable",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gUseFeltPrivacyUI",
  "browser.privatebrowsing.felt-privacy-v1",
  false
);

customElements.setElementCreationCallback("screenshots-buttons", () => {
  Services.scriptloader.loadSubScript(
    "chrome://browser/content/screenshots/screenshots-buttons.js",
    window
  );
});

customElements.setElementCreationCallback("fxa-menu-message", () => {
  ChromeUtils.importESModule(
    "chrome://browser/content/asrouter/components/fxa-menu-message.mjs",
    { global: "current" }
  );
});

var gBrowser;
var gContextMenu = null; // nsContextMenu instance
var gMultiProcessBrowser = window.docShell.QueryInterface(
  Ci.nsILoadContext
).useRemoteTabs;
var gFissionBrowser = window.docShell.QueryInterface(
  Ci.nsILoadContext
).useRemoteSubframes;

var gBrowserAllowScriptsToCloseInitialTabs = false;

if (AppConstants.platform != "macosx") {
  var gEditUIVisible = true;
}

Object.defineProperty(this, "gReduceMotion", {
  enumerable: true,
  get() {
    return typeof gReduceMotionOverride == "boolean"
      ? gReduceMotionOverride
      : gReduceMotionSetting;
  },
});
// Reduce motion during startup. The setting will be reset later.
let gReduceMotionSetting = true;
// This is for tests to set.
var gReduceMotionOverride;

// Smart getter for the findbar.  If you don't wish to force the creation of
// the findbar, check gFindBarInitialized first.

Object.defineProperty(this, "gFindBar", {
  enumerable: true,
  get() {
    return gBrowser.getCachedFindBar();
  },
});

Object.defineProperty(this, "gFindBarInitialized", {
  enumerable: true,
  get() {
    return gBrowser.isFindBarInitialized();
  },
});

Object.defineProperty(this, "gFindBarPromise", {
  enumerable: true,
  get() {
    return gBrowser.getFindBar();
  },
});

function shouldSuppressPopupNotifications() {
  // We have to hide notifications explicitly when the window is
  // minimized because of the effects of the "noautohide" attribute on Linux.
  // This can be removed once bug 545265 and bug 1320361 are fixed.
  // Hide popup notifications when system tab prompts are shown so they
  // don't cover up the prompt.
  return (
    window.windowState == window.STATE_MINIMIZED ||
    gBrowser?.selectedBrowser.hasAttribute("tabDialogShowing") ||
    gDialogBox?.isOpen
  );
}

async function gLazyFindCommand(cmd, ...args) {
  let fb = await gFindBarPromise;
  // We could be closed by now, or the tab with XBL binding could have gone away:
  if (fb && fb[cmd]) {
    fb[cmd].apply(fb, args);
  }
}

var gPageIcons = {
  "about:home": "chrome://branding/content/icon32.png",
  "about:newtab": "chrome://branding/content/icon32.png",
  "about:welcome": "chrome://branding/content/icon32.png",
  "about:privatebrowsing": "chrome://browser/skin/privatebrowsing/favicon.svg",
};

var gInitialPages = [
  "about:blank",
  "about:home",
  "about:firefoxview",
  "about:newtab",
  "about:privatebrowsing",
  "about:sessionrestore",
  "about:welcome",
  "about:welcomeback",
  "chrome://browser/content/blanktab.html",
];

function isInitialPage(url) {
  if (!(url instanceof Ci.nsIURI)) {
    try {
      url = Services.io.newURI(url);
    } catch (ex) {
      return false;
    }
  }

  let nonQuery = url.prePath + url.filePath;
  return gInitialPages.includes(nonQuery) || nonQuery == BROWSER_NEW_TAB_URL;
}

function browserWindows() {
  return Services.wm.getEnumerator("navigator:browser");
}

function updateBookmarkToolbarVisibility() {
  BookmarkingUI.updateEmptyToolbarMessage();
  setToolbarVisibility(
    BookmarkingUI.toolbar,
    gBookmarksToolbarVisibility,
    false,
    false
  );
}

// This is a stringbundle-like interface to gBrowserBundle, formerly a getter for
// the "bundle_browser" element.
var gNavigatorBundle = {
  getString(key) {
    return gBrowserBundle.GetStringFromName(key);
  },
  getFormattedString(key, array) {
    return gBrowserBundle.formatStringFromName(key, array);
  },
};

var gScreenshots = {
  shouldScreenshotsButtonBeDisabled() {
    // About pages other than about:reader are not currently supported by
    // the screenshots extension (see Bug 1620992).
    let uri = gBrowser.selectedBrowser.currentURI;
    let shouldBeDisabled =
      gScreenshotsDisabled ||
      (!gScreenshotsComponentEnabled &&
        uri.scheme === "about" &&
        !uri.spec.startsWith("about:reader"));

    return shouldBeDisabled;
  },
};

function updateFxaToolbarMenu(enable, isInitialUpdate = false) {
  // We only show the Firefox Account toolbar menu if the feature is enabled and
  // if sync is enabled.
  const syncEnabled = Services.prefs.getBoolPref(
    "identity.fxaccounts.enabled",
    false
  );

  const mainWindowEl = document.documentElement;
  const fxaPanelEl = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  const taskbarTab = mainWindowEl.hasAttribute("taskbartab");

  // To minimize the toolbar button flickering or appearing/disappearing during startup,
  // we use this pref to anticipate the likely FxA status.
  const statusGuess = !!Services.prefs.getStringPref(
    "identity.fxaccounts.account.device.name",
    ""
  );
  mainWindowEl.setAttribute(
    "fxastatus",
    statusGuess ? "signed_in" : "not_configured"
  );

  fxaPanelEl.addEventListener("ViewShowing", gSync.updateSendToDeviceTitle);

  if (enable && syncEnabled && !taskbarTab) {
    mainWindowEl.setAttribute("fxatoolbarmenu", "visible");

    // We have to manually update the sync state UI when toggling the FxA toolbar
    // because it could show an invalid icon if the user is logged in and no sync
    // event was performed yet.
    if (!isInitialUpdate) {
      gSync.maybeUpdateUIState();
    }
  } else {
    mainWindowEl.removeAttribute("fxatoolbarmenu");
  }
}

function UpdateBackForwardCommands(aWebNavigation) {
  var backCommand = document.getElementById("Browser:Back");
  var forwardCommand = document.getElementById("Browser:Forward");

  // Avoid setting attributes on commands if the value hasn't changed!
  // Remember, guys, setting attributes on elements is expensive!  They
  // get inherited into anonymous content, broadcast to other widgets, etc.!
  // Don't do it if the value hasn't changed! - dwh

  var backDisabled = backCommand.hasAttribute("disabled");
  var forwardDisabled = forwardCommand.hasAttribute("disabled");
  if (backDisabled == aWebNavigation.canGoBack) {
    if (backDisabled) {
      backCommand.removeAttribute("disabled");
    } else {
      backCommand.setAttribute("disabled", true);
    }
  }

  if (forwardDisabled == aWebNavigation.canGoForward) {
    if (forwardDisabled) {
      forwardCommand.removeAttribute("disabled");
    } else {
      forwardCommand.setAttribute("disabled", true);
    }
  }
}

function updatePrintCommands(enabled) {
  var printCommand = document.getElementById("cmd_print");
  var printPreviewCommand = document.getElementById("cmd_printPreviewToggle");

  if (enabled) {
    printCommand.removeAttribute("disabled");
    printPreviewCommand.removeAttribute("disabled");
  } else {
    printCommand.setAttribute("disabled", "true");
    printPreviewCommand.setAttribute("disabled", "true");
  }
}

/**
 * Click-and-Hold implementation for the Back and Forward buttons
 * XXXmano: should this live in toolbarbutton.js?
 */
function SetClickAndHoldHandlers() {
  // Bug 414797: Clone the back/forward buttons' context menu into both buttons.
  let popup = document.getElementById("backForwardMenu").cloneNode(true);
  popup.removeAttribute("id");
  // Prevent the back/forward buttons' context attributes from being inherited.
  popup.setAttribute("context", "");

  function backForwardMenuCommand(event) {
    BrowserCommands.gotoHistoryIndex(event);
    // event.stopPropagation is here for the cloned version
    // to prevent already-handled clicks on menu items from
    // propagating to the back or forward button.
    event.stopPropagation();
  }

  let backButton = document.getElementById("back-button");
  backButton.setAttribute("type", "menu");
  popup.addEventListener("command", backForwardMenuCommand);
  popup.addEventListener("popupshowing", FillHistoryMenu);
  backButton.prepend(popup);
  gClickAndHoldListenersOnElement.add(backButton);

  let forwardButton = document.getElementById("forward-button");
  popup = popup.cloneNode(true);
  forwardButton.setAttribute("type", "menu");
  popup.addEventListener("command", backForwardMenuCommand);
  popup.addEventListener("popupshowing", FillHistoryMenu);
  forwardButton.prepend(popup);
  gClickAndHoldListenersOnElement.add(forwardButton);
}

const gClickAndHoldListenersOnElement = {
  _timers: new Map(),

  _mousedownHandler(aEvent) {
    if (
      aEvent.button != 0 ||
      aEvent.currentTarget.open ||
      aEvent.currentTarget.disabled
    ) {
      return;
    }

    // Prevent the menupopup from opening immediately
    aEvent.currentTarget.menupopup.hidden = true;

    aEvent.currentTarget.addEventListener("mouseout", this);
    aEvent.currentTarget.addEventListener("mouseup", this);
    this._timers.set(
      aEvent.currentTarget,
      setTimeout(b => this._openMenu(b), 500, aEvent.currentTarget)
    );
  },

  _clickHandler(aEvent) {
    if (
      aEvent.button == 0 &&
      aEvent.target == aEvent.currentTarget &&
      !aEvent.currentTarget.open &&
      !aEvent.currentTarget.disabled &&
      // When menupopup is not hidden and we receive
      // a click event, it means the mousedown occurred
      // on aEvent.currentTarget and mouseup occurred on
      // aEvent.currentTarget.menupopup, we don't
      // need to handle the click event as menupopup
      // handled mouseup event already.
      aEvent.currentTarget.menupopup.hidden
    ) {
      let cmdEvent = document.createEvent("xulcommandevent");
      cmdEvent.initCommandEvent(
        "command",
        true,
        true,
        window,
        0,
        aEvent.ctrlKey,
        aEvent.altKey,
        aEvent.shiftKey,
        aEvent.metaKey,
        0,
        null,
        aEvent.inputSource
      );
      aEvent.currentTarget.dispatchEvent(cmdEvent);

      // This is here to cancel the XUL default event
      // dom.click() triggers a command even if there is a click handler
      // however this can now be prevented with preventDefault().
      aEvent.preventDefault();
    }
  },

  _openMenu(aButton) {
    this._cancelHold(aButton);
    aButton.firstElementChild.hidden = false;
    aButton.open = true;
  },

  _mouseoutHandler(aEvent) {
    let buttonRect = aEvent.currentTarget.getBoundingClientRect();
    if (
      aEvent.clientX >= buttonRect.left &&
      aEvent.clientX <= buttonRect.right &&
      aEvent.clientY >= buttonRect.bottom
    ) {
      this._openMenu(aEvent.currentTarget);
    } else {
      this._cancelHold(aEvent.currentTarget);
    }
  },

  _mouseupHandler(aEvent) {
    this._cancelHold(aEvent.currentTarget);
  },

  _cancelHold(aButton) {
    clearTimeout(this._timers.get(aButton));
    aButton.removeEventListener("mouseout", this);
    aButton.removeEventListener("mouseup", this);
  },

  _keypressHandler(aEvent) {
    if (aEvent.key == " " || aEvent.key == "Enter") {
      aEvent.preventDefault();
      // Normally, command events get fired for keyboard activation. However,
      // we've set type="menu", so that doesn't happen. Handle this the same
      // way we handle clicks.
      aEvent.target.click();
    }
  },

  handleEvent(e) {
    switch (e.type) {
      case "mouseout":
        this._mouseoutHandler(e);
        break;
      case "mousedown":
        this._mousedownHandler(e);
        break;
      case "click":
        this._clickHandler(e);
        break;
      case "mouseup":
        this._mouseupHandler(e);
        break;
      case "keypress":
        // Note that we might not be the only ones dealing with keypresses.
        // See bug 1921772 for more context.
        if (!e.defaultPrevented) {
          this._keypressHandler(e);
        }
        break;
    }
  },

  remove(aButton) {
    aButton.removeEventListener("mousedown", this, true);
    aButton.removeEventListener("click", this, true);
    aButton.removeEventListener("keypress", this, true);
  },

  add(aElm) {
    this._timers.delete(aElm);

    aElm.addEventListener("mousedown", this, true);
    aElm.addEventListener("click", this, true);
    aElm.addEventListener("keypress", this, true);
  },
};

const gSessionHistoryObserver = {
  observe(subject, topic) {
    if (topic != "browser:purge-session-history") {
      return;
    }

    var backCommand = document.getElementById("Browser:Back");
    backCommand.setAttribute("disabled", "true");
    var fwdCommand = document.getElementById("Browser:Forward");
    fwdCommand.setAttribute("disabled", "true");

    // Clear undo history of the URL bar
    gURLBar.editor.clearUndoRedo();
  },
};

const gStoragePressureObserver = {
  _lastNotificationTime: -1,

  async observe(subject, topic) {
    if (topic != "QuotaManager::StoragePressure") {
      return;
    }

    const NOTIFICATION_VALUE = "storage-pressure-notification";
    if (gNotificationBox.getNotificationWithValue(NOTIFICATION_VALUE)) {
      // Do not display the 2nd notification when there is already one
      return;
    }

    // Don't display notification twice within the given interval.
    // This is because
    //   - not to annoy user
    //   - give user some time to clean space.
    //     Even user sees notification and starts acting, it still takes some time.
    const MIN_NOTIFICATION_INTERVAL_MS = Services.prefs.getIntPref(
      "browser.storageManager.pressureNotification.minIntervalMS"
    );
    let duration = Date.now() - this._lastNotificationTime;
    if (duration <= MIN_NOTIFICATION_INTERVAL_MS) {
      return;
    }
    this._lastNotificationTime = Date.now();

    MozXULElement.insertFTLIfNeeded("branding/brand.ftl");
    MozXULElement.insertFTLIfNeeded("browser/preferences/preferences.ftl");

    const BYTES_IN_GIGABYTE = 1073741824;
    const USAGE_THRESHOLD_BYTES =
      BYTES_IN_GIGABYTE *
      Services.prefs.getIntPref(
        "browser.storageManager.pressureNotification.usageThresholdGB"
      );
    let messageFragment = document.createDocumentFragment();
    let message = document.createElement("span");

    let buttons = [{ supportPage: "storage-permissions" }];
    let usage = subject.QueryInterface(Ci.nsISupportsPRUint64).data;
    if (usage < USAGE_THRESHOLD_BYTES) {
      // The firefox-used space < 5GB, then warn user to free some disk space.
      // This is because this usage is small and not the main cause for space issue.
      // In order to avoid the bad and wrong impression among users that
      // firefox eats disk space a lot, indicate users to clean up other disk space.
      document.l10n.setAttributes(message, "space-alert-under-5gb-message2");
    } else {
      // The firefox-used space >= 5GB, then guide users to about:preferences
      // to clear some data stored on firefox by websites.
      document.l10n.setAttributes(message, "space-alert-over-5gb-message2");
      buttons.push({
        "l10n-id": "space-alert-over-5gb-settings-button",
        callback() {
          // The advanced subpanes are only supported in the old organization, which will
          // be removed by bug 1349689.
          openPreferences("privacy-sitedata");
        },
      });
    }
    messageFragment.appendChild(message);

    await gNotificationBox.appendNotification(
      NOTIFICATION_VALUE,
      {
        label: messageFragment,
        priority: gNotificationBox.PRIORITY_WARNING_HIGH,
      },
      buttons
    );

    // This seems to be necessary to get the buttons to display correctly
    // See: https://bugzilla.mozilla.org/show_bug.cgi?id=1504216
    document.l10n.translateFragment(gNotificationBox.currentNotification);
  },
};

var gKeywordURIFixup = {
  check(browser, { fixedURI, keywordProviderName, preferredURI }) {
    // We get called irrespective of whether we did a keyword search, or
    // whether the original input would be vaguely interpretable as a URL,
    // so figure that out first.
    if (
      !keywordProviderName ||
      !fixedURI ||
      !fixedURI.host ||
      UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") ||
      UrlbarPrefs.get("dnsResolveSingleWordsAfterSearch") == 0
    ) {
      return;
    }

    let contentPrincipal = browser.contentPrincipal;

    // At this point we're still only just about to load this URI.
    // When the async DNS lookup comes back, we may be in any of these states:
    // 1) still on the previous URI, waiting for the preferredURI (keyword
    //    search) to respond;
    // 2) at the keyword search URI (preferredURI)
    // 3) at some other page because the user stopped navigation.
    // We keep track of the currentURI to detect case (1) in the DNS lookup
    // callback.
    let previousURI = browser.currentURI;

    // now swap for a weak ref so we don't hang on to browser needlessly
    // even if the DNS query takes forever
    let weakBrowser = Cu.getWeakReference(browser);
    browser = null;

    // Additionally, we need the host of the parsed url
    let hostName = fixedURI.displayHost;
    // and the ascii-only host for the pref:
    let asciiHost = fixedURI.asciiHost;

    let onLookupCompleteListener = {
      async onLookupComplete(request, record, status) {
        let browserRef = weakBrowser.get();
        if (!Components.isSuccessCode(status) || !browserRef) {
          return;
        }

        let currentURI = browserRef.currentURI;
        // If we're in case (3) (see above), don't show an info bar.
        if (
          !currentURI.equals(previousURI) &&
          !currentURI.equals(preferredURI)
        ) {
          return;
        }

        // show infobar offering to visit the host
        let notificationBox = gBrowser.getNotificationBox(browserRef);
        if (notificationBox.getNotificationWithValue("keyword-uri-fixup")) {
          return;
        }

        let displayHostName = "http://" + hostName + "/";
        let message = gNavigatorBundle.getFormattedString(
          "keywordURIFixup.message",
          [displayHostName]
        );
        let yesMessage = gNavigatorBundle.getFormattedString(
          "keywordURIFixup.goTo",
          [displayHostName]
        );

        let buttons = [
          {
            label: yesMessage,
            accessKey: gNavigatorBundle.getString(
              "keywordURIFixup.goTo.accesskey"
            ),
            callback() {
              // Do not set this preference while in private browsing.
              if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
                let prefHost = asciiHost;
                // Normalize out a single trailing dot - NB: not using endsWith/lastIndexOf
                // because we need to be sure this last dot is the *only* dot, too.
                // More generally, this is used for the pref and should stay in sync with
                // the code in URIFixup::KeywordURIFixup .
                if (prefHost.indexOf(".") == prefHost.length - 1) {
                  prefHost = prefHost.slice(0, -1);
                }
                let pref = "browser.fixup.domainwhitelist." + prefHost;
                Services.prefs.setBoolPref(pref, true);
              }
              openTrustedLinkIn(fixedURI.spec, "current");
            },
          },
        ];
        let notification = await notificationBox.appendNotification(
          "keyword-uri-fixup",
          {
            label: message,
            priority: notificationBox.PRIORITY_INFO_HIGH,
          },
          buttons
        );
        notification.persistence = 1;
      },
    };

    try {
      Services.uriFixup.checkHost(
        fixedURI,
        onLookupCompleteListener,
        contentPrincipal.originAttributes
      );
    } catch (ex) {
      // Ignore errors.
    }
  },

  observe(fixupInfo) {
    fixupInfo.QueryInterface(Ci.nsIURIFixupInfo);

    let browser = fixupInfo.consumer?.top?.embedderElement;
    if (!browser || browser.ownerGlobal != window) {
      return;
    }

    this.check(browser, fixupInfo);
  },
};

/* Creates a null principal using the userContextId
   from the current selected tab or a passed in tab argument */
function _createNullPrincipalFromTabUserContextId(tab = gBrowser.selectedTab) {
  let userContextId;
  if (tab.hasAttribute("usercontextid")) {
    userContextId = tab.getAttribute("usercontextid");
  }
  return Services.scriptSecurityManager.createNullPrincipal({
    userContextId,
  });
}

function HandleAppCommandEvent(evt) {
  switch (evt.command) {
    case "Back":
      BrowserCommands.back();
      break;
    case "Forward":
      BrowserCommands.forward();
      break;
    case "Reload":
      BrowserCommands.reloadSkipCache();
      break;
    case "Stop":
      if (XULBrowserWindow.stopCommand.getAttribute("disabled") != "true") {
        BrowserCommands.stop();
      }
      break;
    case "Search":
      SearchUIUtils.webSearch(window);
      break;
    case "Bookmarks":
      SidebarController.toggle("viewBookmarksSidebar");
      break;
    case "Home":
      BrowserCommands.home();
      break;
    case "New":
      BrowserCommands.openTab();
      break;
    case "Close":
      BrowserCommands.closeTabOrWindow();
      break;
    case "Find":
      gLazyFindCommand("onFindCommand");
      break;
    case "Help":
      openHelpLink("firefox-help");
      break;
    case "Open":
      BrowserCommands.openFileWindow();
      break;
    case "Print":
      PrintUtils.startPrintWindow(gBrowser.selectedBrowser.browsingContext);
      break;
    case "Save":
      saveBrowser(gBrowser.selectedBrowser);
      break;
    case "SendMail":
      MailIntegration.sendLinkForBrowser(gBrowser.selectedBrowser);
      break;
    default:
      return;
  }
  evt.stopPropagation();
  evt.preventDefault();
}

function loadOneOrMoreURIs(aURIString, aTriggeringPrincipal, aCsp) {
  // we're not a browser window, pass the URI string to a new browser window
  if (window.location.href != AppConstants.BROWSER_CHROME_URL) {
    window.openDialog(
      AppConstants.BROWSER_CHROME_URL,
      "_blank",
      "all,dialog=no",
      aURIString
    );
    return;
  }

  // This function throws for certain malformed URIs, so use exception handling
  // so that we don't disrupt startup
  try {
    gBrowser.loadTabs(aURIString.split("|"), {
      inBackground: false,
      replace: true,
      triggeringPrincipal: aTriggeringPrincipal,
      csp: aCsp,
    });
  } catch (e) {}
}

function openLocation(event) {
  if (window.location.href == AppConstants.BROWSER_CHROME_URL) {
    gURLBar.select();
    gURLBar.view.autoOpen({ event });
    return;
  }

  // If there's an open browser window, redirect the command there.
  let win = URILoadingHelper.getTargetWindow(window);
  if (win) {
    win.focus();
    win.openLocation();
    return;
  }

  // There are no open browser windows; open a new one.
  window.openDialog(
    AppConstants.BROWSER_CHROME_URL,
    "_blank",
    "chrome,all,dialog=no",
    BROWSER_NEW_TAB_URL
  );
}

var gLastOpenDirectory = {
  _lastDir: null,
  get path() {
    if (!this._lastDir || !this._lastDir.exists()) {
      try {
        this._lastDir = Services.prefs.getComplexValue(
          "browser.open.lastDir",
          Ci.nsIFile
        );
        if (!this._lastDir.exists()) {
          this._lastDir = null;
        }
      } catch (e) {}
    }
    return this._lastDir;
  },
  set path(val) {
    try {
      if (!val || !val.isDirectory()) {
        return;
      }
    } catch (e) {
      return;
    }
    this._lastDir = val.clone();

    // Don't save the last open directory pref inside the Private Browsing mode
    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      Services.prefs.setComplexValue(
        "browser.open.lastDir",
        Ci.nsIFile,
        this._lastDir
      );
    }
  },
  reset() {
    this._lastDir = null;
  },
};

function getLoadContext() {
  return window.docShell.QueryInterface(Ci.nsILoadContext);
}

function readFromClipboard() {
  var url;

  try {
    // Create transferable that will transfer the text.
    var trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    trans.init(getLoadContext());

    trans.addDataFlavor("text/plain");

    // If available, use selection clipboard, otherwise global one
    let clipboard = Services.clipboard;
    if (clipboard.isClipboardTypeSupported(clipboard.kSelectionClipboard)) {
      clipboard.getData(trans, clipboard.kSelectionClipboard);
    } else {
      clipboard.getData(trans, clipboard.kGlobalClipboard);
    }

    var data = {};
    trans.getTransferData("text/plain", data);

    if (data) {
      data = data.value.QueryInterface(Ci.nsISupportsString);
      url = data.data;
    }
  } catch (ex) {}

  return url;
}

function UpdateUrlbarSearchSplitterState() {
  var splitter = document.getElementById("urlbar-search-splitter");
  var urlbar = document.getElementById("urlbar-container");
  var searchbar = document.getElementById("search-container");

  if (document.documentElement.hasAttribute("customizing")) {
    if (splitter) {
      splitter.remove();
    }
    return;
  }

  // If the splitter is already in the right place, we don't need to do anything:
  if (
    splitter &&
    ((splitter.nextElementSibling == searchbar &&
      splitter.previousElementSibling == urlbar) ||
      (splitter.nextElementSibling == urlbar &&
        splitter.previousElementSibling == searchbar))
  ) {
    return;
  }

  let ibefore = null;
  let resizebefore = "none";
  let resizeafter = "none";
  if (urlbar && searchbar) {
    if (urlbar.nextElementSibling == searchbar) {
      resizeafter = "sibling";
      ibefore = searchbar;
    } else if (searchbar.nextElementSibling == urlbar) {
      resizebefore = "sibling";
      ibefore = urlbar;
    }
  }

  if (ibefore) {
    if (!splitter) {
      splitter = document.createXULElement("splitter");
      splitter.id = "urlbar-search-splitter";
      splitter.setAttribute("resizebefore", resizebefore);
      splitter.setAttribute("resizeafter", resizeafter);
      splitter.setAttribute("skipintoolbarset", "true");
      splitter.setAttribute("overflows", "false");
      splitter.className = "chromeclass-toolbar-additional";
    }
    urlbar.parentNode.insertBefore(splitter, ibefore);
  } else if (splitter) {
    splitter.remove();
  }
}

function UpdatePopupNotificationsVisibility() {
  // Only need to update PopupNotifications if it has already been initialized
  // for this window (i.e. its getter no longer exists).
  if (!Object.getOwnPropertyDescriptor(window, "PopupNotifications").get) {
    // Notify PopupNotifications that the visible anchors may have changed. This
    // also checks the suppression state according to the "shouldSuppress"
    // function defined earlier in this file.
    PopupNotifications.anchorVisibilityChange();
  }

  // This is similar to the above, but for notifications attached to the
  // hamburger menu icon (such as update notifications and add-on install
  // notifications.)
  PanelUI?.updateNotifications();
}

function PageProxyClickHandler(aEvent) {
  if (aEvent.button == 1 && Services.prefs.getBoolPref("middlemouse.paste")) {
    middleMousePaste(aEvent);
  }
}

function CreateContainerTabMenu(event) {
  // Do not open context menus within menus.
  // Note that triggerNode is null if we're opened by long press.
  if (event.target.triggerNode?.closest("menupopup")) {
    event.preventDefault();
    return;
  }
  createUserContextMenu(event, {
    useAccessKeys: false,
    showDefaultTab: true,
  });
}

function FillHistoryMenu(event) {
  let parent = event.target;

  // Lazily add the hover listeners on first showing and never remove them
  if (!parent.hasStatusListener) {
    // Show history item's uri in the status bar when hovering, and clear on exit
    parent.addEventListener("DOMMenuItemActive", function (aEvent) {
      // Only the current page should have the checked attribute, so skip it
      if (!aEvent.target.hasAttribute("checked")) {
        XULBrowserWindow.setOverLink(aEvent.target.getAttribute("uri"));
      }
    });
    parent.addEventListener("DOMMenuItemInactive", function () {
      XULBrowserWindow.setOverLink("");
    });

    parent.hasStatusListener = true;
  }

  // Remove old entries if any
  let children = parent.children;
  for (var i = children.length - 1; i >= 0; --i) {
    if (children[i].hasAttribute("index")) {
      parent.removeChild(children[i]);
    }
  }

  const MAX_HISTORY_MENU_ITEMS = 15;

  const tooltipBack = gNavigatorBundle.getString("tabHistory.goBack");
  const tooltipCurrent = gNavigatorBundle.getString("tabHistory.reloadCurrent");
  const tooltipForward = gNavigatorBundle.getString("tabHistory.goForward");

  function updateSessionHistory(sessionHistory, initial, ssInParent) {
    let count = ssInParent
      ? sessionHistory.count
      : sessionHistory.entries.length;

    if (!initial) {
      if (count <= 1) {
        // if there is only one entry now, close the popup.
        parent.hidePopup();
        return;
      } else if (parent.id != "backForwardMenu" && !parent.parentNode.open) {
        // if the popup wasn't open before, but now needs to be, reopen the menu.
        // It should trigger FillHistoryMenu again. This might happen with the
        // delay from click-and-hold menus but skip this for the context menu
        // (backForwardMenu) rather than figuring out how the menu should be
        // positioned and opened as it is an extreme edgecase.
        parent.parentNode.open = true;
        return;
      }
    }

    let index = sessionHistory.index;
    let half_length = Math.floor(MAX_HISTORY_MENU_ITEMS / 2);
    let start = Math.max(index - half_length, 0);
    let end = Math.min(
      start == 0 ? MAX_HISTORY_MENU_ITEMS : index + half_length + 1,
      count
    );
    if (end == count) {
      start = Math.max(count - MAX_HISTORY_MENU_ITEMS, 0);
    }

    let existingIndex = 0;

    for (let j = end - 1; j >= start; j--) {
      let entry = ssInParent
        ? sessionHistory.getEntryAtIndex(j)
        : sessionHistory.entries[j];
      // Explicitly check for "false" to stay backwards-compatible with session histories
      // from before the hasUserInteraction was implemented.
      if (
        BrowserUtils.navigationRequireUserInteraction &&
        entry.hasUserInteraction === false &&
        // Always list the current and last navigation points.
        j != end - 1 &&
        j != index
      ) {
        continue;
      }
      let uri = ssInParent ? entry.URI.spec : entry.url;

      let item =
        existingIndex < children.length
          ? children[existingIndex]
          : document.createXULElement("menuitem");

      item.setAttribute("uri", uri);
      item.setAttribute("label", entry.title || uri);
      item.setAttribute("index", j);

      // Cache this so that BrowserCommands.gotoHistoryIndex doesn't need the
      // original index
      item.setAttribute("historyindex", j - index);

      if (j != index) {
        // Use list-style-image rather than the image attribute in order to
        // allow CSS to override this.
        item.style.listStyleImage = `url(page-icon:${uri})`;
      }

      if (j < index) {
        item.className =
          "unified-nav-back menuitem-iconic menuitem-with-favicon";
        item.setAttribute("tooltiptext", tooltipBack);
      } else if (j == index) {
        item.setAttribute("type", "radio");
        item.setAttribute("checked", "true");
        item.className = "unified-nav-current";
        item.setAttribute("tooltiptext", tooltipCurrent);
      } else {
        item.className =
          "unified-nav-forward menuitem-iconic menuitem-with-favicon";
        item.setAttribute("tooltiptext", tooltipForward);
      }

      if (!item.parentNode) {
        parent.appendChild(item);
      }

      existingIndex++;
    }

    if (!initial) {
      let existingLength = children.length;
      while (existingIndex < existingLength) {
        parent.removeChild(parent.lastElementChild);
        existingIndex++;
      }
    }
  }

  // If session history in parent is available, use it. Otherwise, get the session history
  // from session store.
  let sessionHistory = gBrowser.selectedBrowser.browsingContext.sessionHistory;
  if (sessionHistory?.count) {
    // Don't show the context menu if there is only one item.
    if (sessionHistory.count <= 1) {
      event.preventDefault();
      return;
    }

    updateSessionHistory(sessionHistory, true, true);
  } else {
    sessionHistory = SessionStore.getSessionHistory(
      gBrowser.selectedTab,
      updateSessionHistory
    );
    updateSessionHistory(sessionHistory, true, false);
  }
}

function toOpenWindowByType(inType, uri, features) {
  var topWindow = Services.wm.getMostRecentWindow(inType);

  if (topWindow) {
    topWindow.focus();
  } else if (features) {
    window.open(uri, "_blank", features);
  } else {
    window.open(
      uri,
      "_blank",
      "chrome,extrachrome,menubar,resizable,scrollbars,status,toolbar"
    );
  }
}

/**
 * Open a new browser window. See `BrowserWindowTracker.openWindow` for
 * options.
 *
 * @return a reference to the new window.
 */
function OpenBrowserWindow(options = {}) {
  let timerId = Glean.browserTimings.newWindow.start();

  let win = BrowserWindowTracker.openWindow({
    openerWindow: window,
    ...options,
  });

  win.addEventListener(
    "MozAfterPaint",
    () => {
      Glean.browserTimings.newWindow.stopAndAccumulate(timerId);
    },
    { once: true }
  );

  return win;
}

/**
 * Update the global flag that tracks whether or not any edit UI (the Edit menu,
 * edit-related items in the context menu, and edit-related toolbar buttons
 * is visible, then update the edit commands' enabled state accordingly.  We use
 * this flag to skip updating the edit commands on focus or selection changes
 * when no UI is visible to improve performance (including pageload performance,
 * since focus changes when you load a new page).
 *
 * If UI is visible, we use goUpdateGlobalEditMenuItems to set the commands'
 * enabled state so the UI will reflect it appropriately.
 *
 * If the UI isn't visible, we enable all edit commands so keyboard shortcuts
 * still work and just lazily disable them as needed when the user presses a
 * shortcut.
 *
 * This doesn't work on Mac, since Mac menus flash when users press their
 * keyboard shortcuts, so edit UI is essentially always visible on the Mac,
 * and we need to always update the edit commands.  Thus on Mac this function
 * is a no op.
 */
function updateEditUIVisibility() {
  if (AppConstants.platform == "macosx") {
    return;
  }

  let editMenuPopupState = document.getElementById("menu_EditPopup").state;
  let contextMenuPopupState = document.getElementById(
    "contentAreaContextMenu"
  ).state;
  let placesContextMenuPopupState =
    document.getElementById("placesContext").state;

  let oldVisible = gEditUIVisible;

  // The UI is visible if the Edit menu is opening or open, if the context menu
  // is open, or if the toolbar has been customized to include the Cut, Copy,
  // or Paste toolbar buttons.
  gEditUIVisible =
    editMenuPopupState == "showing" ||
    editMenuPopupState == "open" ||
    contextMenuPopupState == "showing" ||
    contextMenuPopupState == "open" ||
    placesContextMenuPopupState == "showing" ||
    placesContextMenuPopupState == "open";
  const kOpenPopupStates = ["showing", "open"];
  if (!gEditUIVisible) {
    // Now check the edit-controls toolbar buttons.
    let placement = CustomizableUI.getPlacementOfWidget("edit-controls");
    let areaType = placement ? CustomizableUI.getAreaType(placement.area) : "";
    if (areaType == CustomizableUI.TYPE_PANEL) {
      let customizablePanel = PanelUI.overflowPanel;
      gEditUIVisible = kOpenPopupStates.includes(customizablePanel.state);
    } else if (
      areaType == CustomizableUI.TYPE_TOOLBAR &&
      window.toolbar.visible
    ) {
      // The edit controls are on a toolbar, so they are visible,
      // unless they're in a panel that isn't visible...
      if (placement.area == "nav-bar") {
        let editControls = document.getElementById("edit-controls");
        gEditUIVisible =
          !editControls.hasAttribute("overflowedItem") ||
          kOpenPopupStates.includes(
            document.getElementById("widget-overflow").state
          );
      } else {
        gEditUIVisible = true;
      }
    }
  }

  // Now check the main menu panel
  if (!gEditUIVisible) {
    gEditUIVisible = kOpenPopupStates.includes(PanelUI.panel.state);
  }

  // No need to update commands if the edit UI visibility has not changed.
  if (gEditUIVisible == oldVisible) {
    return;
  }

  // If UI is visible, update the edit commands' enabled state to reflect
  // whether or not they are actually enabled for the current focus/selection.
  if (gEditUIVisible) {
    goUpdateGlobalEditMenuItems();
  } else {
    // Otherwise, enable all commands, so that keyboard shortcuts still work,
    // then lazily determine their actual enabled state when the user presses
    // a keyboard shortcut.
    goSetCommandEnabled("cmd_undo", true);
    goSetCommandEnabled("cmd_redo", true);
    goSetCommandEnabled("cmd_cut", true);
    goSetCommandEnabled("cmd_copy", true);
    goSetCommandEnabled("cmd_paste", true);
    goSetCommandEnabled("cmd_selectAll", true);
    goSetCommandEnabled("cmd_delete", true);
    goSetCommandEnabled("cmd_switchTextDirection", true);
  }
}

let gFileMenu = {
  /**
   * Updates User Context Menu Item UI visibility depending on
   * privacy.userContext.enabled pref state.
   */
  updateUserContextUIVisibility() {
    let menu = document.getElementById("menu_newUserContext");
    menu.hidden = !Services.prefs.getBoolPref(
      "privacy.userContext.enabled",
      false
    );
    // Visibility of File menu item shouldn't change frequently.
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      menu.setAttribute("disabled", "true");
    }
  },

  /**
   * Updates the enabled state of the "Import From Another Browser" command
   * depending on the DisableProfileImport policy.
   */
  updateImportCommandEnabledState() {
    if (!Services.policies.isAllowed("profileImport")) {
      document
        .getElementById("cmd_file_importFromAnotherBrowser")
        .setAttribute("disabled", "true");
    }
  },

  /**
   * Updates the "Close tab" command to reflect the number of selected tabs,
   * when applicable.
   */
  updateTabCloseCountState() {
    document.l10n.setAttributes(
      document.getElementById("menu_close"),
      "menu-file-close-tab",
      { tabCount: gBrowser.selectedTabs.length }
    );
  },

  onPopupShowing(event) {
    // We don't care about submenus:
    if (event.target.id != "menu_FilePopup") {
      return;
    }
    this.updateUserContextUIVisibility();
    this.updateImportCommandEnabledState();
    this.updateTabCloseCountState();
    if (AppConstants.platform == "macosx") {
      SharingUtils.updateShareURLMenuItem(
        gBrowser.selectedBrowser,
        document.getElementById("menu_savePage")
      );
    }
    PrintUtils.updatePrintSetupMenuHiddenState();
  },
};

/**
 * Opens a new tab with the userContextId specified as an attribute of
 * sourceEvent. This attribute is propagated to the top level originAttributes
 * living on the tab's docShell.
 *
 * @param event
 *        A click event on a userContext File Menu option
 */
function openNewUserContextTab(event) {
  openTrustedLinkIn(BROWSER_NEW_TAB_URL, "tab", {
    userContextId: parseInt(event.target.getAttribute("data-usercontextid")),
  });
}

var XULBrowserWindow = {
  // Stored Status, Link and Loading values
  status: "",
  defaultStatus: "",
  overLink: "",
  startTime: 0,
  isBusy: false,
  busyUI: false,

  QueryInterface: ChromeUtils.generateQI([
    "nsIWebProgressListener",
    "nsIWebProgressListener2",
    "nsISupportsWeakReference",
    "nsIXULBrowserWindow",
  ]),

  get stopCommand() {
    delete this.stopCommand;
    return (this.stopCommand = document.getElementById("Browser:Stop"));
  },
  get reloadCommand() {
    delete this.reloadCommand;
    return (this.reloadCommand = document.getElementById("Browser:Reload"));
  },
  get _elementsForTextBasedTypes() {
    delete this._elementsForTextBasedTypes;
    return (this._elementsForTextBasedTypes = [
      document.getElementById("pageStyleMenu"),
      document.getElementById("context-viewpartialsource-selection"),
      document.getElementById("context-print-selection"),
    ]);
  },
  get _elementsForFind() {
    delete this._elementsForFind;
    return (this._elementsForFind = [
      document.getElementById("cmd_find"),
      document.getElementById("cmd_findAgain"),
      document.getElementById("cmd_findPrevious"),
    ]);
  },
  get _elementsForViewSource() {
    delete this._elementsForViewSource;
    return (this._elementsForViewSource = [
      document.getElementById("context-viewsource"),
      document.getElementById("View:PageSource"),
    ]);
  },
  get _menuItemForRepairTextEncoding() {
    delete this._menuItemForRepairTextEncoding;
    return (this._menuItemForRepairTextEncoding = document.getElementById(
      "repair-text-encoding"
    ));
  },
  get _menuItemForTranslations() {
    delete this._menuItemForTranslations;
    return (this._menuItemForTranslations =
      document.getElementById("cmd_translate"));
  },

  setDefaultStatus(status) {
    this.defaultStatus = status;
    StatusPanel.update();
  },

  /**
   * Tells the UI what link we are currently over.
   *
   * @param {String} url
   *   The URL of the link.
   * @param {Object} [options]
   *   This is an extension of nsIXULBrowserWindow for JS callers, will be
   *   passed on to LinkTargetDisplay.
   */
  setOverLink(url, options = undefined) {
    window.dispatchEvent(
      new CustomEvent("OverLink", {
        detail: { url },
      })
    );

    if (url) {
      url = Services.textToSubURI.unEscapeURIForUI(url);

      // Encode bidirectional formatting characters.
      // (RFC 3987 sections 3.2 and 4.1 paragraph 6)
      url = url.replace(
        /[\u200e\u200f\u202a\u202b\u202c\u202d\u202e]/g,
        encodeURIComponent
      );

      if (UrlbarPrefs.get("trimURLs")) {
        url = BrowserUIUtils.trimURL(url);
      }
    }

    this.overLink = url;
    LinkTargetDisplay.update(options);
  },

  onEnterDOMFullscreen() {
    // Clear the status panel.
    this.status = "";
    this.setDefaultStatus("");
    this.setOverLink("", { hideStatusPanelImmediately: true });
  },

  showTooltip(xDevPix, yDevPix, tooltip, direction, _browser) {
    if (
      Cc["@mozilla.org/widget/dragservice;1"]
        .getService(Ci.nsIDragService)
        .getCurrentSession()
    ) {
      return;
    }

    if (!document.hasFocus()) {
      return;
    }

    let elt = document.getElementById("remoteBrowserTooltip");
    elt.label = tooltip;
    elt.style.direction = direction;
    elt.openPopupAtScreen(
      xDevPix / window.devicePixelRatio,
      yDevPix / window.devicePixelRatio,
      false,
      null
    );
  },

  hideTooltip() {
    let elt = document.getElementById("remoteBrowserTooltip");
    elt.hidePopup();
  },

  getTabCount() {
    return gBrowser.tabs.length;
  },

  onProgressChange() {
    // Do nothing.
  },

  onProgressChange64(
    aWebProgress,
    aRequest,
    aCurSelfProgress,
    aMaxSelfProgress,
    aCurTotalProgress,
    aMaxTotalProgress
  ) {
    return this.onProgressChange(
      aWebProgress,
      aRequest,
      aCurSelfProgress,
      aMaxSelfProgress,
      aCurTotalProgress,
      aMaxTotalProgress
    );
  },

  // This function fires only for the currently selected tab.
  onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
    const nsIWebProgressListener = Ci.nsIWebProgressListener;

    let browser = gBrowser.selectedBrowser;
    gProtectionsHandler.onStateChange(aWebProgress, aStateFlags);

    if (
      aStateFlags & nsIWebProgressListener.STATE_START &&
      aStateFlags & nsIWebProgressListener.STATE_IS_NETWORK
    ) {
      if (aRequest && aWebProgress.isTopLevel) {
        OpenSearchManager.clearEngines(browser);
      }

      this.isBusy = true;

      if (
        !(aStateFlags & nsIWebProgressListener.STATE_RESTORING) &&
        aWebProgress.isTopLevel
      ) {
        this.busyUI = true;

        if (this.spinCursorWhileBusy) {
          window.setCursor("progress");
        }

        // XXX: This needs to be based on window activity...
        this.stopCommand.removeAttribute("disabled");
        CombinedStopReload.switchToStop(aRequest, aWebProgress);
      }
    } else if (aStateFlags & nsIWebProgressListener.STATE_STOP) {
      // This (thanks to the filter) is a network stop or the last
      // request stop outside of loading the document, stop throbbers
      // and progress bars and such
      if (aRequest) {
        let msg = "";
        let location;
        let canViewSource = true;
        // Get the URI either from a channel or a pseudo-object
        if (aRequest instanceof Ci.nsIChannel || "URI" in aRequest) {
          location = aRequest.URI;

          // For keyword URIs clear the user typed value since they will be changed into real URIs
          if (location.scheme == "keyword" && aWebProgress.isTopLevel) {
            gBrowser.userTypedValue = null;
          }

          canViewSource = location.scheme != "view-source";

          if (location.spec != "about:blank") {
            switch (aStatus) {
              case Cr.NS_ERROR_NET_TIMEOUT:
                msg = gNavigatorBundle.getString("nv_timeout");
                break;
            }
          }
        }

        this.status = "";
        this.setDefaultStatus(msg);

        // Disable View Source menu entries for images, enable otherwise
        let isText =
          browser.documentContentType &&
          BrowserUtils.mimeTypeIsTextBased(browser.documentContentType);
        for (let element of this._elementsForViewSource) {
          if (canViewSource && isText) {
            element.removeAttribute("disabled");
          } else {
            element.setAttribute("disabled", "true");
          }
        }

        this._updateElementsForContentType();

        // Update Override Text Encoding state.
        // Can't cache the button, because the presence of the element in the DOM
        // may change over time.
        let button = document.getElementById("characterencoding-button");
        if (browser.mayEnableCharacterEncodingMenu) {
          this._menuItemForRepairTextEncoding.removeAttribute("disabled");
          button?.removeAttribute("disabled");
        } else {
          this._menuItemForRepairTextEncoding.setAttribute("disabled", "true");
          button?.setAttribute("disabled", "true");
        }
      }

      this.isBusy = false;

      if (this.busyUI && aWebProgress.isTopLevel) {
        this.busyUI = false;

        if (this.spinCursorWhileBusy) {
          window.setCursor("auto");
        }

        this.stopCommand.setAttribute("disabled", "true");
        CombinedStopReload.switchToReload(aRequest, aWebProgress);
      }
    }
  },

  /**
   * An nsIWebProgressListener method called by tabbrowser.  The `aIsSimulated`
   * parameter is extra and not declared in nsIWebProgressListener, however; see
   * below.
   *
   * @param {nsIWebProgress} aWebProgress
   *   The nsIWebProgress instance that fired the notification.
   * @param {nsIRequest} aRequest
   *   The associated nsIRequest.  This may be null in some cases.
   * @param {nsIURI} aLocationURI
   *   The URI of the location that is being loaded.
   * @param {integer} aFlags
   *   Flags that indicate the reason the location changed.  See the
   *   nsIWebProgressListener.LOCATION_CHANGE_* values.
   * @param {boolean} aIsSimulated
   *   True when this is called by tabbrowser due to switching tabs and
   *   undefined otherwise.  This parameter is not declared in
   *   nsIWebProgressListener.onLocationChange; see bug 1478348.
   */
  onLocationChange(aWebProgress, aRequest, aLocationURI, aFlags, aIsSimulated) {
    var location = aLocationURI ? aLocationURI.spec : "";

    UpdateBackForwardCommands(gBrowser.webNavigation);

    Services.obs.notifyObservers(
      aWebProgress,
      "touchbar-location-change",
      location
    );

    // For most changes we only need to update the browser UI if the primary
    // content area was navigated or the selected tab was changed. We don't need
    // to do anything else if there was a subframe navigation.

    if (!aWebProgress.isTopLevel) {
      return;
    }

    this.setOverLink("", { hideStatusPanelImmediately: true });

    let isSameDocument =
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT;
    if (
      (location == "about:blank" &&
        BrowserUIUtils.checkEmptyPageOrigin(gBrowser.selectedBrowser)) ||
      location == ""
    ) {
      // Second condition is for new tabs, otherwise
      // reload function is enabled until tab is refreshed.
      this.reloadCommand.setAttribute("disabled", "true");
    } else {
      this.reloadCommand.removeAttribute("disabled");
    }

    let isSessionRestore = !!(
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SESSION_STORE
    );

    // We want to update the popup visibility if we received this notification
    // via simulated locationchange events such as switching between tabs, however
    // if this is a document navigation then PopupNotifications will be updated
    // via TabsProgressListener.onLocationChange and we do not want it called twice
    gURLBar.setURI(
      aLocationURI,
      aIsSimulated,
      isSessionRestore,
      false,
      isSameDocument
    );

    BookmarkingUI.onLocationChange();
    // If we've actually changed document, update the toolbar visibility.
    if (!isSameDocument) {
      updateBookmarkToolbarVisibility();
    }

    let closeOpenPanels = selector => {
      for (let panel of document.querySelectorAll(selector)) {
        if (panel.state != "closed") {
          panel.hidePopup();
        }
      }
    };

    // If the location is changed due to switching tabs,
    // ensure we close any open tabspecific popups.
    if (aIsSimulated) {
      closeOpenPanels(":is(panel, menupopup)[tabspecific='true']");
    }

    // Ensure we close any remaining open locationspecific panels
    if (!isSameDocument) {
      closeOpenPanels("panel[locationspecific='true']");
    }

    let screenshotsButtonsDisabled =
      gScreenshots.shouldScreenshotsButtonBeDisabled();
    Services.obs.notifyObservers(
      window,
      "toggle-screenshot-disable",
      screenshotsButtonsDisabled
    );

    gPermissionPanel.onLocationChange();

    gProtectionsHandler.onLocationChange();

    BrowserPageActions.onLocationChange();

    SaveToPocket.onLocationChange(window);

    UrlbarProviderSearchTips.onLocationChange(
      window,
      aLocationURI,
      aWebProgress,
      aFlags
    );

    if (aLocationURI.scheme.startsWith("http")) {
      ActionsProviderContextualSearch.onLocationChange(
        window,
        aLocationURI,
        aWebProgress,
        aFlags
      );
    }

    this._updateElementsForContentType();

    this._updateMacUserActivity(window, aLocationURI, aWebProgress);

    // Unconditionally disable the Text Encoding button during load to
    // keep the UI calm when navigating from one modern page to another and
    // the toolbar button is visible.
    // Can't cache the button, because the presence of the element in the DOM
    // may change over time.
    let button = document.getElementById("characterencoding-button");
    this._menuItemForRepairTextEncoding.setAttribute("disabled", "true");
    button?.setAttribute("disabled", "true");

    // Try not to instantiate gCustomizeMode as much as possible,
    // so don't use CustomizeMode.sys.mjs to check for URI or customizing.
    if (
      location == "about:blank" &&
      gBrowser.selectedTab.hasAttribute("customizemode")
    ) {
      gCustomizeMode.enter();
    } else if (
      CustomizationHandler.isEnteringCustomizeMode ||
      CustomizationHandler.isCustomizing()
    ) {
      gCustomizeMode.exit();
    }

    CFRPageActions.updatePageActions(gBrowser.selectedBrowser);

    AboutReaderParent.updateReaderButton(gBrowser.selectedBrowser);
    TranslationsParent.onLocationChange(gBrowser.selectedBrowser);

    PictureInPicture.updateUrlbarToggle(gBrowser.selectedBrowser);

    if (!gMultiProcessBrowser) {
      // Bug 1108553 - Cannot rotate images with e10s
      gGestureSupport.restoreRotationState();
    }

    // See bug 358202, when tabs are switched during a drag operation,
    // timers don't fire on windows (bug 203573)
    if (aRequest) {
      setTimeout(function () {
        XULBrowserWindow.asyncUpdateUI();
      }, 0);
    } else {
      this.asyncUpdateUI();
    }

    if (AppConstants.MOZ_CRASHREPORTER && aLocationURI) {
      let uri = aLocationURI;
      try {
        // If the current URI contains a username/password, remove it.
        uri = aLocationURI.mutate().setUserPass("").finalize();
      } catch (ex) {
        /* Ignore failures on about: URIs. */
      }

      try {
        Services.appinfo.annotateCrashReport("URL", uri.spec);
      } catch (ex) {
        // Don't make noise when the crash reporter is built but not enabled.
        if (ex.result != Cr.NS_ERROR_NOT_INITIALIZED) {
          throw ex;
        }
      }
    }
  },

  _updateElementsForContentType() {
    let browser = gBrowser.selectedBrowser;

    let isText =
      browser.documentContentType &&
      BrowserUtils.mimeTypeIsTextBased(browser.documentContentType);
    for (let element of this._elementsForTextBasedTypes) {
      if (isText) {
        element.removeAttribute("disabled");
      } else {
        element.setAttribute("disabled", "true");
      }
    }

    // Always enable find commands in PDF documents, otherwise do it only for
    // text documents whose location is not in the blacklist.
    let enableFind =
      browser.contentPrincipal?.spec == "resource://pdf.js/web/viewer.html" ||
      (isText && BrowserUtils.canFindInPage(gBrowser.currentURI.spec));
    for (let element of this._elementsForFind) {
      if (enableFind) {
        element.removeAttribute("disabled");
      } else {
        element.setAttribute("disabled", "true");
      }
    }

    if (TranslationsParent.isFullPageTranslationsRestrictedForPage(gBrowser)) {
      this._menuItemForTranslations.setAttribute("disabled", "true");
    } else {
      this._menuItemForTranslations.removeAttribute("disabled");
    }
    if (gTranslationsEnabled) {
      if (TranslationsParent.getIsTranslationsEngineSupported()) {
        this._menuItemForTranslations.removeAttribute("hidden");
      } else {
        this._menuItemForTranslations.setAttribute("hidden", "true");
      }
    } else {
      this._menuItemForTranslations.setAttribute("hidden", "true");
    }
  },

  /**
   * Updates macOS platform code with the current URI and page title.
   * From there, we update the current NSUserActivity, enabling Handoff to other
   * Apple devices.
   * @param {Window} window
   *   The window in which the navigation occurred.
   * @param {nsIURI} uri
   *   The URI pointing to the current page.
   * @param {nsIWebProgress} webProgress
   *   The nsIWebProgress instance that fired a onLocationChange notification.
   */
  _updateMacUserActivity(win, uri, webProgress) {
    if (!webProgress.isTopLevel || AppConstants.platform != "macosx") {
      return;
    }

    let url = uri.spec;
    if (PrivateBrowsingUtils.isWindowPrivate(win)) {
      // Passing an empty string to MacUserActivityUpdater will invalidate the
      // current user activity.
      url = "";
    }
    let baseWin = win.docShell.treeOwner.QueryInterface(Ci.nsIBaseWindow);
    MacUserActivityUpdater.updateLocation(
      url,
      win.gBrowser.contentTitle,
      baseWin
    );
  },

  /**
   * Potentially gets a URI for a MozBrowser to be shown to the user in the
   * identity panel. For browsers whose content does not have a principal,
   * this tries the precursor. If this is null, we should not override the
   * browser's currentURI.
   * @param {MozBrowser} browser
   *   The browser that we need a URI to show the user in the
   *   identity panel.
   * @return nsIURI of the principal for the browser's content if
   *   the browser's currentURI should not be used, null otherwise.
   */
  _securityURIOverride(browser) {
    let uri = browser.currentURI;
    if (!uri) {
      return null;
    }

    // If the browser's currentURI is sufficiently good that we
    // do not require an override, bail out here.
    // browser.currentURI should be used.
    let { URI_INHERITS_SECURITY_CONTEXT } = Ci.nsIProtocolHandler;
    if (
      !(doGetProtocolFlags(uri) & URI_INHERITS_SECURITY_CONTEXT) &&
      !(uri.scheme == "about" && uri.filePath == "srcdoc") &&
      !(uri.scheme == "about" && uri.filePath == "blank")
    ) {
      return null;
    }

    let principal = browser.contentPrincipal;

    if (principal.isNullPrincipal) {
      principal = principal.precursorPrincipal;
    }

    if (!principal) {
      return null;
    }

    // Can't get the original URI for a PDF viewer principal yet.
    if (principal.originNoSuffix == "resource://pdf.js") {
      return null;
    }

    return principal.URI;
  },

  asyncUpdateUI() {
    OpenSearchManager.updateOpenSearchBadge(window);
  },

  onStatusChange(aWebProgress, aRequest, aStatus, aMessage) {
    this.status = aMessage;
    StatusPanel.update();
  },

  // Properties used to cache security state used to update the UI
  _event: null,
  _lastLocationForEvent: null,

  // This is called in multiple ways:
  //  1. Due to the nsIWebProgressListener.onContentBlockingEvent notification.
  //  2. Called by tabbrowser.xml when updating the current browser.
  //  3. Called directly during this object's initializations.
  //  4. Due to the nsIWebProgressListener.onLocationChange notification.
  // aRequest will be null always in case 2 and 3, and sometimes in case 1 (for
  // instance, there won't be a request when STATE_BLOCKED_TRACKING_CONTENT or
  // other blocking events are observed).
  onContentBlockingEvent(aWebProgress, aRequest, aEvent, aIsSimulated) {
    // Don't need to do anything if the data we use to update the UI hasn't
    // changed
    let uri = gBrowser.currentURI;
    let spec = uri.spec;
    if (this._event == aEvent && this._lastLocationForEvent == spec) {
      return;
    }
    this._lastLocationForEvent = spec;

    if (
      typeof aIsSimulated != "boolean" &&
      typeof aIsSimulated != "undefined"
    ) {
      throw new Error(
        "onContentBlockingEvent: aIsSimulated receieved an unexpected type"
      );
    }

    gProtectionsHandler.onContentBlockingEvent(
      aEvent,
      aWebProgress,
      aIsSimulated,
      this._event // previous content blocking event
    );

    // We need the state of the previous content blocking event, so update
    // event after onContentBlockingEvent is called.
    this._event = aEvent;
  },

  // This is called in multiple ways:
  //  1. Due to the nsIWebProgressListener.onSecurityChange notification.
  //  2. Called by tabbrowser.xml when updating the current browser.
  //  3. Called directly during this object's initializations.
  // aRequest will be null always in case 2 and 3, and sometimes in case 1.
  onSecurityChange(aWebProgress, aRequest, aState, _aIsSimulated) {
    // Make sure the "https" part of the URL is striked out or not,
    // depending on the current mixed active content blocking state.
    gURLBar.formatValue();

    // Update the identity panel, making sure we use the precursorPrincipal's
    // URI where appropriate, for example about:blank windows.
    let uri = gBrowser.currentURI;
    let uriOverride = this._securityURIOverride(gBrowser.selectedBrowser);
    if (uriOverride) {
      uri = uriOverride;
      aState |= Ci.nsIWebProgressListener.STATE_IDENTITY_ASSOCIATED;
    }

    try {
      uri = Services.io.createExposableURI(uri);
    } catch (e) {}
    gIdentityHandler.updateIdentity(aState, uri);
  },

  // simulate all change notifications after switching tabs
  onUpdateCurrentBrowser: function XWB_onUpdateCurrentBrowser(
    aStateFlags,
    aStatus,
    aMessage,
    _aTotalProgress
  ) {
    if (FullZoom.updateBackgroundTabs) {
      FullZoom.onLocationChange(gBrowser.currentURI, true);
    }

    CombinedStopReload.onTabSwitch();

    // Docshell should normally take care of hiding the tooltip, but we need to do it
    // ourselves for tabswitches.
    this.hideTooltip();

    // Also hide tooltips for content loaded in the parent process:
    document.getElementById("aHTMLTooltip").hidePopup();

    var nsIWebProgressListener = Ci.nsIWebProgressListener;
    var loadingDone = aStateFlags & nsIWebProgressListener.STATE_STOP;
    // use a pseudo-object instead of a (potentially nonexistent) channel for getting
    // a correct error message - and make sure that the UI is always either in
    // loading (STATE_START) or done (STATE_STOP) mode
    this.onStateChange(
      gBrowser.webProgress,
      { URI: gBrowser.currentURI },
      loadingDone
        ? nsIWebProgressListener.STATE_STOP
        : nsIWebProgressListener.STATE_START,
      aStatus
    );
    // status message and progress value are undefined if we're done with loading
    if (loadingDone) {
      return;
    }
    this.onStatusChange(gBrowser.webProgress, null, 0, aMessage);
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  XULBrowserWindow,
  "spinCursorWhileBusy",
  "browser.spin_cursor_while_busy"
);

var LinkTargetDisplay = {
  get DELAY_SHOW() {
    delete this.DELAY_SHOW;
    return (this.DELAY_SHOW = Services.prefs.getIntPref(
      "browser.overlink-delay"
    ));
  },

  DELAY_HIDE: 250,
  _timer: 0,

  get _contextMenu() {
    delete this._contextMenu;
    return (this._contextMenu = document.getElementById(
      "contentAreaContextMenu"
    ));
  },

  update({ hideStatusPanelImmediately = false } = {}) {
    if (
      this._contextMenu.state == "open" ||
      this._contextMenu.state == "showing"
    ) {
      this._contextMenu.addEventListener("popuphidden", () => this.update(), {
        once: true,
      });
      return;
    }

    clearTimeout(this._timer);
    window.removeEventListener("mousemove", this, true);

    if (!XULBrowserWindow.overLink) {
      if (hideStatusPanelImmediately) {
        this._hide();
      } else {
        this._timer = setTimeout(this._hide.bind(this), this.DELAY_HIDE);
      }
      return;
    }

    if (StatusPanel.isVisible) {
      StatusPanel.update();
    } else {
      // Let the display appear when the mouse doesn't move within the delay
      this._showDelayed();
      window.addEventListener("mousemove", this, true);
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "mousemove":
        // Restart the delay since the mouse was moved
        clearTimeout(this._timer);
        this._showDelayed();
        break;
    }
  },

  _showDelayed() {
    this._timer = setTimeout(
      function (self) {
        StatusPanel.update();
        window.removeEventListener("mousemove", self, true);
      },
      this.DELAY_SHOW,
      this
    );
  },

  _hide() {
    clearTimeout(this._timer);

    StatusPanel.update();
  },
};

var CombinedStopReload = {
  // Try to initialize. Returns whether initialization was successful, which
  // may mean we had already initialized.
  ensureInitialized() {
    if (this._initialized) {
      return true;
    }
    if (this._destroyed) {
      return false;
    }

    let reload = document.getElementById("reload-button");
    let stop = document.getElementById("stop-button");
    // It's possible the stop/reload buttons have been moved to the palette.
    // They may be reinserted later, so we will retry initialization if/when
    // we get notified of document loads.
    if (!stop || !reload) {
      return false;
    }

    this._initialized = true;
    if (XULBrowserWindow.stopCommand.getAttribute("disabled") != "true") {
      reload.setAttribute("displaystop", "true");
    }
    stop.addEventListener("click", this);

    // Removing attributes based on the observed command doesn't happen if the button
    // is in the palette when the command's attribute is removed (cf. bug 309953)
    for (let button of [stop, reload]) {
      if (button.hasAttribute("disabled")) {
        let command = document.getElementById(button.getAttribute("command"));
        if (!command.hasAttribute("disabled")) {
          button.removeAttribute("disabled");
        }
      }
    }

    this.reload = reload;
    this.stop = stop;
    this.stopReloadContainer = this.reload.parentNode;
    this.timeWhenSwitchedToStop = 0;

    this.stopReloadContainer.addEventListener("animationend", this);
    this.stopReloadContainer.addEventListener("animationcancel", this);

    return true;
  },

  uninit() {
    this._destroyed = true;

    if (!this._initialized) {
      return;
    }

    this._cancelTransition();
    this.stop.removeEventListener("click", this);
    this.stopReloadContainer.removeEventListener("animationend", this);
    this.stopReloadContainer.removeEventListener("animationcancel", this);
    this.stopReloadContainer = null;
    this.reload = null;
    this.stop = null;
  },

  handleEvent(event) {
    switch (event.type) {
      case "click":
        if (event.button == 0 && !this.stop.disabled) {
          this._stopClicked = true;
        }
        break;
      case "animationcancel":
      case "animationend": {
        if (
          event.target.classList.contains("toolbarbutton-animatable-image") &&
          (event.animationName == "reload-to-stop" ||
            event.animationName == "stop-to-reload")
        ) {
          this.stopReloadContainer.removeAttribute("animate");
        }
      }
    }
  },

  onTabSwitch() {
    // Reset the time in the event of a tabswitch since the stored time
    // would have been associated with the previous tab, so the animation will
    // still run if the page has been loading until long after the tab switch.
    this.timeWhenSwitchedToStop = window.performance.now();
  },

  switchToStop(aRequest, aWebProgress) {
    if (
      !this.ensureInitialized() ||
      !this._shouldSwitch(aRequest, aWebProgress)
    ) {
      return;
    }

    // Store the time that we switched to the stop button only if a request
    // is active. Requests are null if the switch is related to a tabswitch.
    // This is used to determine if we should show the stop->reload animation.
    if (aRequest instanceof Ci.nsIRequest) {
      this.timeWhenSwitchedToStop = window.performance.now();
    }

    let shouldAnimate =
      aRequest instanceof Ci.nsIRequest &&
      aWebProgress.isTopLevel &&
      aWebProgress.isLoadingDocument &&
      !gBrowser.tabAnimationsInProgress &&
      !gReduceMotion &&
      this.stopReloadContainer.closest("#nav-bar-customization-target");

    this._cancelTransition();
    if (shouldAnimate) {
      this.stopReloadContainer.setAttribute("animate", "true");
    } else {
      this.stopReloadContainer.removeAttribute("animate");
    }
    this.reload.setAttribute("displaystop", "true");
  },

  switchToReload(aRequest, aWebProgress) {
    if (!this.ensureInitialized() || !this.reload.hasAttribute("displaystop")) {
      return;
    }

    let shouldAnimate =
      aRequest instanceof Ci.nsIRequest &&
      aWebProgress.isTopLevel &&
      !aWebProgress.isLoadingDocument &&
      !gBrowser.tabAnimationsInProgress &&
      !gReduceMotion &&
      this._loadTimeExceedsMinimumForAnimation() &&
      this.stopReloadContainer.closest("#nav-bar-customization-target");

    if (shouldAnimate) {
      this.stopReloadContainer.setAttribute("animate", "true");
    } else {
      this.stopReloadContainer.removeAttribute("animate");
    }

    this.reload.removeAttribute("displaystop");

    if (!shouldAnimate || this._stopClicked) {
      this._stopClicked = false;
      this._cancelTransition();
      this.reload.disabled =
        XULBrowserWindow.reloadCommand.getAttribute("disabled") == "true";
      return;
    }

    if (this._timer) {
      return;
    }

    // Temporarily disable the reload button to prevent the user from
    // accidentally reloading the page when intending to click the stop button
    this.reload.disabled = true;
    this._timer = setTimeout(
      function (self) {
        self._timer = 0;
        self.reload.disabled =
          XULBrowserWindow.reloadCommand.getAttribute("disabled") == "true";
      },
      650,
      this
    );
  },

  _loadTimeExceedsMinimumForAnimation() {
    // If the time between switching to the stop button then switching to
    // the reload button exceeds 150ms, then we will show the animation.
    // If we don't know when we switched to stop (switchToStop is called
    // after init but before switchToReload), then we will prevent the
    // animation from occuring.
    return (
      this.timeWhenSwitchedToStop &&
      window.performance.now() - this.timeWhenSwitchedToStop > 150
    );
  },

  _shouldSwitch(aRequest, aWebProgress) {
    if (
      aRequest &&
      aRequest.originalURI &&
      (aRequest.originalURI.schemeIs("chrome") ||
        (aRequest.originalURI.schemeIs("about") &&
          aWebProgress.isTopLevel &&
          !aRequest.originalURI.spec.startsWith("about:reader")))
    ) {
      return false;
    }

    return true;
  },

  _cancelTransition() {
    if (this._timer) {
      clearTimeout(this._timer);
      this._timer = 0;
    }
  },
};

var TabsProgressListener = {
  onStateChange(aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
    // Collect telemetry data about tab load times.
    if (
      aWebProgress.isTopLevel &&
      (!aRequest.originalURI || aRequest.originalURI.scheme != "about")
    ) {
      let metricName = "pageLoad";

      if (aWebProgress.loadType & Ci.nsIDocShell.LOAD_CMD_RELOAD) {
        // loadType is constructed by shifting loadFlags, this is why we need to
        // do the same shifting here.
        // https://searchfox.org/mozilla-central/rev/11cfa0462a6b5d8c5e2111b8cfddcf78098f0141/docshell/base/nsDocShellLoadTypes.h#22
        if (aWebProgress.loadType & (kSkipCacheFlags << 16)) {
          metricName = "pageReloadSkipCache";
        } else if (aWebProgress.loadType == Ci.nsIDocShell.LOAD_CMD_RELOAD) {
          metricName = "pageReloadNormal";
        } else {
          metricName = "";
        }
      }

      const timerIdField = `_${metricName}TimerId`;
      if (aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW) {
        if (aStateFlags & Ci.nsIWebProgressListener.STATE_START) {
          if (metricName) {
            if (aBrowser[timerIdField]) {
              // Oops, we're seeing another start without having noticed the previous stop.
              Glean.browserTimings[metricName].cancel(aBrowser[timerIdField]);
            }
            aBrowser[timerIdField] = Glean.browserTimings[metricName].start();
          }
          Glean.browserEngagement.totalTopVisits.true.add();
        } else if (
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          /* we won't see STATE_START events for pre-rendered tabs */
          metricName &&
          aBrowser[timerIdField]
        ) {
          Glean.browserTimings[metricName].stopAndAccumulate(
            aBrowser[timerIdField]
          );
          aBrowser[timerIdField] = null;
          BrowserTelemetryUtils.recordSiteOriginTelemetry(browserWindows());
        }
      } else if (
        aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
        /* we won't see STATE_START events for pre-rendered tabs */
        aStatus == Cr.NS_BINDING_ABORTED &&
        metricName &&
        aBrowser[timerIdField]
      ) {
        Glean.browserTimings[metricName].cancel(aBrowser[timerIdField]);
        aBrowser[timerIdField] = null;
      }
    }
  },

  onLocationChange(aBrowser, aWebProgress, aRequest, aLocationURI, aFlags) {
    // Filter out location changes in sub documents.
    if (!aWebProgress.isTopLevel) {
      return;
    }

    // Some shops use pushState to move between individual products, so
    // the shopping code needs to be told about all of these.
    ShoppingSidebarManager.onLocationChange(aBrowser, aLocationURI, aFlags);

    // Filter out location changes caused by anchor navigation
    // or history.push/pop/replaceState.
    if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
      // Reader mode cares about history.pushState and friends.
      // FIXME: The content process should manage this directly (bug 1445351).
      aBrowser.sendMessageToActor(
        "Reader:PushState",
        {
          isArticle: aBrowser.isArticle,
        },
        "AboutReader"
      );
      return;
    }

    // Only need to call locationChange if the PopupNotifications object
    // for this window has already been initialized (i.e. its getter no
    // longer exists)
    if (!Object.getOwnPropertyDescriptor(window, "PopupNotifications").get) {
      PopupNotifications.locationChange(aBrowser);
    }

    let tab = gBrowser.getTabForBrowser(aBrowser);
    if (tab && tab._sharingState) {
      gBrowser.resetBrowserSharing(aBrowser);
    }

    gBrowser.readNotificationBox(aBrowser)?.removeTransientNotifications();

    // Notify the mailto notification creation code _after_ clearing transient
    // notifications, so its notification does not immediately get removed.
    Services.obs.notifyObservers(aBrowser, "mailto::onLocationChange", aFlags);

    FullZoom.onLocationChange(aLocationURI, false, aBrowser);
    CaptivePortalWatcher.onLocationChange(aBrowser);
  },

  onLinkIconAvailable(browser, dataURI, iconURI) {
    if (!iconURI) {
      return;
    }
    if (browser == gBrowser.selectedBrowser) {
      // If the "Add Search Engine" page action is in the urlbar, its image
      // needs to be set to the new icon, so call updateOpenSearchBadge.
      OpenSearchManager.updateOpenSearchBadge(window);
    }
  },
};

function showFullScreenViewContextMenuItems(popup) {
  for (let node of popup.querySelectorAll('[contexttype="fullscreen"]')) {
    node.hidden = !window.fullScreen;
  }
  let autoHide = popup.querySelector(".fullscreen-context-autohide");
  if (autoHide) {
    FullScreen.updateAutohideMenuitem(autoHide);
  }
}

function onViewToolbarCommand(aEvent) {
  let node = aEvent.originalTarget;
  let menuId;
  let toolbarId;
  let isVisible;
  if (node.dataset.bookmarksToolbarVisibility) {
    isVisible = node.dataset.visibilityEnum;
    toolbarId = "PersonalToolbar";
    menuId = node.parentNode.parentNode.parentNode.id;
    Services.prefs.setCharPref(
      "browser.toolbars.bookmarks.visibility",
      isVisible
    );
  } else {
    menuId = node.parentNode.id;
    toolbarId = node.getAttribute("toolbarId");
    isVisible = node.getAttribute("checked") == "true";
  }
  CustomizableUI.setToolbarVisibility(toolbarId, isVisible);
  BrowserUsageTelemetry.recordToolbarVisibility(toolbarId, isVisible, menuId);
}

function setToolbarVisibility(
  toolbar,
  isVisible,
  persist = true,
  animated = true
) {
  let hidingAttribute;
  if (toolbar.getAttribute("type") == "menubar") {
    hidingAttribute = "autohide";
    if (AppConstants.platform == "linux") {
      Services.prefs.setBoolPref("ui.key.menuAccessKeyFocuses", !isVisible);
    }
  } else {
    hidingAttribute = "collapsed";
  }

  if (toolbar == BookmarkingUI.toolbar) {
    // For the bookmarks toolbar, we need to persist state before toggling
    // the visibility in this window, because the state can be different
    // (newtab vs never or always) even when that won't change visibility
    // in this window.
    if (persist) {
      let prefValue;
      if (typeof isVisible == "string") {
        prefValue = isVisible;
      } else {
        prefValue = isVisible ? "always" : "never";
      }
      Services.prefs.setCharPref(
        "browser.toolbars.bookmarks.visibility",
        prefValue
      );
    }

    switch (isVisible) {
      case true:
      case "always":
        isVisible = true;
        break;
      case false:
      case "never":
        isVisible = false;
        break;
      case "newtab":
      default: {
        let currentURI;
        if (!gBrowserInit.domContentLoaded) {
          let uriToLoad = gBrowserInit.uriToLoadPromise;
          if (uriToLoad) {
            if (Array.isArray(uriToLoad)) {
              // We only care about the first tab being loaded
              uriToLoad = uriToLoad[0];
            }
            currentURI = URL.parse(uriToLoad)?.URI;
            if (!currentURI) {
              currentURI = gBrowser?.currentURI;
            }
          }
        } else {
          currentURI = gBrowser.currentURI;
        }
        isVisible = BookmarkingUI.isOnNewTabPage(currentURI);
        break;
      }
    }
  }

  if (toolbar.getAttribute(hidingAttribute) == (!isVisible).toString()) {
    // If this call will not result in a visibility change, return early
    // since dispatching toolbarvisibilitychange will cause views to get rebuilt.
    return;
  }

  toolbar.classList.toggle("instant", !animated);
  toolbar.setAttribute(hidingAttribute, !isVisible);
  // For the bookmarks toolbar, we will have saved state above. For other
  // toolbars, we need to do it after setting the attribute, or we might
  // save the wrong state.
  if (persist && toolbar.id != "PersonalToolbar") {
    Services.xulStore.persist(toolbar, hidingAttribute);
  }

  let eventParams = {
    detail: {
      visible: isVisible,
    },
    bubbles: true,
  };
  let event = new CustomEvent("toolbarvisibilitychange", eventParams);
  toolbar.dispatchEvent(event);
}

function updateToggleControlLabel(control) {
  if (!control.hasAttribute("label-checked")) {
    return;
  }

  if (!control.hasAttribute("label-unchecked")) {
    control.setAttribute("label-unchecked", control.getAttribute("label"));
  }
  let prefix = control.getAttribute("checked") == "true" ? "" : "un";
  control.setAttribute("label", control.getAttribute(`label-${prefix}checked`));
}

// Propagates Win10's tablet mode into the browser CSS. (Win11's tablet mode is
// more like non-tablet mode and has no need for this.)
const Win10TabletModeUpdater = {
  init() {
    if (AppConstants.platform == "win") {
      this.update(WindowsUIUtils.inWin10TabletMode);
      Services.obs.addObserver(this, "tablet-mode-change");
    }
  },

  uninit() {
    if (AppConstants.platform == "win") {
      Services.obs.removeObserver(this, "tablet-mode-change");
    }
  },

  observe(subject, topic, data) {
    this.update(data == "win10-tablet-mode");
  },

  update(isInTabletMode) {
    if (isInTabletMode) {
      document.documentElement.setAttribute("win10-tablet-mode", "true");
    } else {
      document.documentElement.removeAttribute("win10-tablet-mode");
    }
  },
};

function displaySecurityInfo() {
  BrowserCommands.pageInfo(null, "securityTab");
}

// Updates the UI density (for touch and compact mode) based on the uidensity pref.
var gUIDensity = {
  MODE_NORMAL: 0,
  MODE_COMPACT: 1,
  MODE_TOUCH: 2,
  uiDensityPref: "browser.uidensity",
  autoTouchModePref: "browser.touchmode.auto",
  knownPrefs: new Set(["browser.uidensity", "browser.touchmode.auto"]),

  init() {
    this.update();
    Services.obs.addObserver(this, "tablet-mode-change");
    Services.prefs.addObserver(this.uiDensityPref, this);
    Services.prefs.addObserver(this.autoTouchModePref, this);
  },

  uninit() {
    Services.obs.removeObserver(this, "tablet-mode-change");
    Services.prefs.removeObserver(this.uiDensityPref, this);
    Services.prefs.removeObserver(this.autoTouchModePref, this);
  },

  observe(aSubject, aTopic, aPrefName) {
    const ok = (() => {
      if (aTopic == "tablet-mode-change") {
        return true;
      }
      if (aTopic == "nsPref:changed" && this.knownPrefs.has(aPrefName)) {
        return true;
      }
      return false;
    })();
    if (!ok) {
      return;
    }

    this.update();
  },

  getCurrentDensity() {
    // Automatically override the uidensity to touch in Windows tablet mode
    // (either Win10 or Win11).
    if (AppConstants.platform == "win") {
      const inTablet =
        WindowsUIUtils.inWin10TabletMode || WindowsUIUtils.inWin11TabletMode;
      if (inTablet && Services.prefs.getBoolPref(this.autoTouchModePref)) {
        return { mode: this.MODE_TOUCH, overridden: true };
      }
    }
    return {
      mode: Services.prefs.getIntPref(this.uiDensityPref),
      overridden: false,
    };
  },

  update(mode) {
    if (mode == null) {
      mode = this.getCurrentDensity().mode;
    }

    let docs = [document.documentElement];
    let shouldUpdateSidebar =
      SidebarController.initialized && SidebarController.isOpen;
    if (shouldUpdateSidebar) {
      docs.push(SidebarController.browser.contentDocument.documentElement);
    }
    for (let doc of docs) {
      switch (mode) {
        case this.MODE_COMPACT:
          doc.setAttribute("uidensity", "compact");
          break;
        case this.MODE_TOUCH:
          doc.setAttribute("uidensity", "touch");
          break;
        default:
          doc.removeAttribute("uidensity");
          break;
      }
    }
    if (shouldUpdateSidebar) {
      let tree = SidebarController.browser.contentDocument.querySelector(
        ".sidebar-placesTree"
      );
      if (tree) {
        // Tree items don't update their styles without changing some property on the
        // parent tree element, like background-color or border. See bug 1407399.
        tree.style.border = "1px";
        tree.style.border = "";
      }
    }

    gBrowser.tabContainer.uiDensityChanged();
    gURLBar.uiDensityChanged();
  },
};

const nodeToTooltipMap = {
  "bookmarks-menu-button": "bookmarksMenuButton.tooltip",
  "context-reload": "reloadButton.tooltip",
  "context-stop": "stopButton.tooltip",
  "downloads-button": "downloads.tooltip",
  "fullscreen-button": "fullscreenButton.tooltip",
  "appMenu-fullscreen-button2": "fullscreenButton.tooltip",
  "new-window-button": "newWindowButton.tooltip",
  "new-tab-button": "newTabButton.tooltip",
  "tabs-newtab-button": "newTabButton.tooltip",
  "reload-button": "reloadButton.tooltip",
  "stop-button": "stopButton.tooltip",
  "urlbar-zoom-button": "urlbar-zoom-button.tooltip",
  "appMenu-zoomEnlarge-button2": "zoomEnlarge-button.tooltip",
  "appMenu-zoomReset-button2": "zoomReset-button.tooltip",
  "appMenu-zoomReduce-button2": "zoomReduce-button.tooltip",
  "reader-mode-button": "reader-mode-button.tooltip",
  "reader-mode-button-icon": "reader-mode-button.tooltip",
  "vertical-tabs-newtab-button": "newTabButton.tooltip",
};
const nodeToShortcutMap = {
  "bookmarks-menu-button": "manBookmarkKb",
  "context-reload": "key_reload",
  "context-stop": "key_stop",
  "downloads-button": "key_openDownloads",
  "fullscreen-button": "key_enterFullScreen",
  "appMenu-fullscreen-button2": "key_enterFullScreen",
  "new-window-button": "key_newNavigator",
  "new-tab-button": "key_newNavigatorTab",
  "tabs-newtab-button": "key_newNavigatorTab",
  "reload-button": "key_reload",
  "stop-button": "key_stop",
  "urlbar-zoom-button": "key_fullZoomReset",
  "appMenu-zoomEnlarge-button2": "key_fullZoomEnlarge",
  "appMenu-zoomReset-button2": "key_fullZoomReset",
  "appMenu-zoomReduce-button2": "key_fullZoomReduce",
  "reader-mode-button": "key_toggleReaderMode",
  "reader-mode-button-icon": "key_toggleReaderMode",
  "vertical-tabs-newtab-button": "key_newNavigatorTab",
};

const gDynamicTooltipCache = new Map();
function GetDynamicShortcutTooltipText(nodeId) {
  if (!gDynamicTooltipCache.has(nodeId) && nodeId in nodeToTooltipMap) {
    let strId = nodeToTooltipMap[nodeId];
    let args = [];
    if (nodeId in nodeToShortcutMap) {
      let shortcutId = nodeToShortcutMap[nodeId];
      let shortcut = document.getElementById(shortcutId);
      if (shortcut) {
        args.push(ShortcutUtils.prettifyShortcut(shortcut));
      }
    }
    gDynamicTooltipCache.set(
      nodeId,
      gNavigatorBundle.getFormattedString(strId, args)
    );
  }
  return gDynamicTooltipCache.get(nodeId);
}

function UpdateDynamicShortcutTooltipText(aTooltip) {
  let nodeId =
    aTooltip.triggerNode.id || aTooltip.triggerNode.getAttribute("anonid");
  aTooltip.setAttribute("label", GetDynamicShortcutTooltipText(nodeId));
}

/*
 * - [ Dependencies ] ---------------------------------------------------------
 *  utilityOverlay.js:
 *    - gatherTextUnder
 */

/**
 * Extracts linkNode and href for the current click target.
 *
 * @param event
 *        The click event.
 * @return [href, linkNode].
 *
 * @note linkNode will be null if the click wasn't on an anchor
 *       element (or XLink).
 */
function hrefAndLinkNodeForClickEvent(event) {
  function isHTMLLink(aNode) {
    // Be consistent with what nsContextMenu.js does.
    return (
      (HTMLAnchorElement.isInstance(aNode) && aNode.href) ||
      (HTMLAreaElement.isInstance(aNode) && aNode.href) ||
      HTMLLinkElement.isInstance(aNode)
    );
  }

  let node = event.composedTarget;
  while (node && !isHTMLLink(node)) {
    node = node.flattenedTreeParentNode;
  }

  if (node) {
    return [node.href, node];
  }

  // If there is no linkNode, try simple XLink.
  let href, baseURI;
  node = event.composedTarget;
  while (node && !href) {
    if (
      node.nodeType == Node.ELEMENT_NODE &&
      (node.localName == "a" ||
        node.namespaceURI == "http://www.w3.org/1998/Math/MathML")
    ) {
      href =
        node.getAttribute("href") ||
        node.getAttributeNS("http://www.w3.org/1999/xlink", "href");

      if (href) {
        baseURI = node.baseURI;
        break;
      }
    }
    node = node.flattenedTreeParentNode;
  }

  // In case of XLink, we don't return the node we got href from since
  // callers expect <a>-like elements.
  return [href ? makeURLAbsolute(baseURI, href) : null, null];
}

/**
 * Called whenever the user clicks in the content area.
 *
 * @param event
 *        The click event.
 * @param isPanelClick
 *        Whether the event comes from an extension panel.
 * @note default event is prevented if the click is handled.
 */
function contentAreaClick(event, isPanelClick) {
  if (!event.isTrusted || event.defaultPrevented || event.button != 0) {
    return;
  }

  let [href, linkNode] = hrefAndLinkNodeForClickEvent(event);
  if (!href) {
    // Not a link, handle middle mouse navigation.
    if (
      event.button == 1 &&
      Services.prefs.getBoolPref("middlemouse.contentLoadURL") &&
      !Services.prefs.getBoolPref("general.autoScroll")
    ) {
      middleMousePaste(event);
      event.preventDefault();
    }
    return;
  }

  // This code only applies if we have a linkNode (i.e. clicks on real anchor
  // elements, as opposed to XLink).
  if (
    linkNode &&
    event.button == 0 &&
    !event.ctrlKey &&
    !event.shiftKey &&
    !event.altKey &&
    !event.metaKey
  ) {
    // An extension panel's links should target the main content area.  Do this
    // if no modifier keys are down and if there's no target or the target
    // equals _main (the IE convention) or _content (the Mozilla convention).
    let target = linkNode.target;
    let mainTarget = !target || target == "_content" || target == "_main";
    if (isPanelClick && mainTarget) {
      // javascript and data links should be executed in the current browser.
      if (
        linkNode.getAttribute("onclick") ||
        href.startsWith("javascript:") ||
        href.startsWith("data:")
      ) {
        return;
      }

      try {
        urlSecurityCheck(href, linkNode.ownerDocument.nodePrincipal);
      } catch (ex) {
        // Prevent loading unsecure destinations.
        event.preventDefault();
        return;
      }

      openLinkIn(href, "current", {
        allowThirdPartyFixup: false,
      });
      event.preventDefault();
      return;
    }
  }

  handleLinkClick(event, href, linkNode);

  // Mark the page as a user followed link.  This is done so that history can
  // distinguish automatic embed visits from user activated ones.  For example
  // pages loaded in frames are embed visits and lost with the session, while
  // visits across frames should be preserved.
  try {
    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      PlacesUIUtils.markPageAsFollowedLink(href);
    }
  } catch (ex) {
    /* Skip invalid URIs. */
  }
}

/**
 * Handles clicks on links.
 *
 * @return true if the click event was handled, false otherwise.
 */
function handleLinkClick(event, href, linkNode) {
  if (event.button == 2) {
    // right click
    return false;
  }

  var where = BrowserUtils.whereToOpenLink(event);
  if (where == "current") {
    return false;
  }

  var doc = event.target.ownerDocument;
  let referrerInfo = Cc["@mozilla.org/referrer-info;1"].createInstance(
    Ci.nsIReferrerInfo
  );
  if (linkNode) {
    referrerInfo.initWithElement(linkNode);
  } else {
    referrerInfo.initWithDocument(doc);
  }

  if (where == "save") {
    saveURL(
      href,
      null,
      linkNode ? gatherTextUnder(linkNode) : "",
      null,
      true,
      true,
      referrerInfo,
      doc.cookieJarSettings,
      doc
    );
    event.preventDefault();
    return true;
  }

  let frameID = WebNavigationFrames.getFrameId(doc.defaultView);

  urlSecurityCheck(href, doc.nodePrincipal);
  let params = {
    charset: doc.characterSet,
    referrerInfo,
    originPrincipal: doc.nodePrincipal,
    originStoragePrincipal: doc.effectiveStoragePrincipal,
    triggeringPrincipal: doc.nodePrincipal,
    csp: doc.csp,
    frameID,
  };

  // The new tab/window must use the same userContextId
  if (doc.nodePrincipal.originAttributes.userContextId) {
    params.userContextId = doc.nodePrincipal.originAttributes.userContextId;
  }

  openLinkIn(href, where, params);
  event.preventDefault();
  return true;
}

/**
 * Handles paste on middle mouse clicks.
 *
 * @param event {Event | Object} Event or JSON object.
 */
function middleMousePaste(event) {
  let clipboard = readFromClipboard();
  if (!clipboard) {
    return;
  }

  // Strip embedded newlines and surrounding whitespace, to match the URL
  // bar's behavior (stripsurroundingwhitespace)
  clipboard = clipboard.replace(/\s*\n\s*/g, "");

  clipboard = UrlbarUtils.stripUnsafeProtocolOnPaste(clipboard);

  // if it's not the current tab, we don't need to do anything because the
  // browser doesn't exist.
  let where = BrowserUtils.whereToOpenLink(event, true, false);
  let lastLocationChange;
  if (where == "current") {
    lastLocationChange = gBrowser.selectedBrowser.lastLocationChange;
  }

  UrlbarUtils.getShortcutOrURIAndPostData(clipboard).then(data => {
    try {
      makeURI(data.url);
    } catch (ex) {
      // Not a valid URI.
      return;
    }

    try {
      UrlbarUtils.addToUrlbarHistory(data.url, window);
    } catch (ex) {
      // Things may go wrong when adding url to session history,
      // but don't let that interfere with the loading of the url.
      console.error(ex);
    }

    if (
      where != "current" ||
      lastLocationChange == gBrowser.selectedBrowser.lastLocationChange
    ) {
      openUILink(data.url, event, {
        ignoreButton: true,
        allowInheritPrincipal: data.mayInheritPrincipal,
        triggeringPrincipal: gBrowser.selectedBrowser.contentPrincipal,
        csp: gBrowser.selectedBrowser.csp,
      });
    }
  });

  if (Event.isInstance(event)) {
    event.stopPropagation();
  }
}

// handleDroppedLink has the following 2 overloads:
//   handleDroppedLink(event, url, name, triggeringPrincipal)
//   handleDroppedLink(event, links, triggeringPrincipal)
function handleDroppedLink(
  event,
  urlOrLinks,
  nameOrTriggeringPrincipal,
  triggeringPrincipal
) {
  let links;
  if (Array.isArray(urlOrLinks)) {
    links = urlOrLinks;
    triggeringPrincipal = nameOrTriggeringPrincipal;
  } else {
    links = [{ url: urlOrLinks, nameOrTriggeringPrincipal, type: "" }];
  }

  let lastLocationChange = gBrowser.selectedBrowser.lastLocationChange;

  let userContextId = gBrowser.selectedBrowser.getAttribute("usercontextid");

  // event is null if links are dropped in content process.
  // inBackground should be false, as it's loading into current browser.
  let inBackground = false;
  if (event) {
    inBackground = Services.prefs.getBoolPref("browser.tabs.loadInBackground");
    if (event.shiftKey) {
      inBackground = !inBackground;
    }
  }

  (async function () {
    if (
      links.length >=
      Services.prefs.getIntPref("browser.tabs.maxOpenBeforeWarn")
    ) {
      // Sync dialog cannot be used inside drop event handler.
      let answer = await OpenInTabsUtils.promiseConfirmOpenInTabs(
        links.length,
        window
      );
      if (!answer) {
        return;
      }
    }

    let urls = [];
    let postDatas = [];
    for (let link of links) {
      let data = await UrlbarUtils.getShortcutOrURIAndPostData(link.url);
      urls.push(data.url);
      postDatas.push(data.postData);
    }
    if (lastLocationChange == gBrowser.selectedBrowser.lastLocationChange) {
      gBrowser.loadTabs(urls, {
        inBackground,
        replace: true,
        allowThirdPartyFixup: false,
        postDatas,
        userContextId,
        triggeringPrincipal,
      });
    }
  })();

  // If links are dropped in content process, event.preventDefault() should be
  // called in content process.
  if (event) {
    // Keep the event from being handled by the dragDrop listeners
    // built-in to gecko if they happen to be above us.
    event.preventDefault();
  }
}

// Note that this is also called from non-browser windows on OSX, which do
// share menu items but not much else. See nonbrowser-mac.js.
var BrowserOffline = {
  _inited: false,

  // BrowserOffline Public Methods
  init() {
    if (!this._uiElement) {
      this._uiElement = document.getElementById("cmd_toggleOfflineStatus");
    }

    Services.obs.addObserver(this, "network:offline-status-changed");

    this._updateOfflineUI(Services.io.offline);

    this._inited = true;
  },

  uninit() {
    if (this._inited) {
      Services.obs.removeObserver(this, "network:offline-status-changed");
    }
  },

  toggleOfflineStatus() {
    var ioService = Services.io;

    if (!ioService.offline && !this._canGoOffline()) {
      this._updateOfflineUI(false);
      return;
    }

    ioService.offline = !ioService.offline;
  },

  // nsIObserver
  observe(aSubject, aTopic) {
    if (aTopic != "network:offline-status-changed") {
      return;
    }

    // This notification is also received because of a loss in connectivity,
    // which we ignore by updating the UI to the current value of io.offline
    this._updateOfflineUI(Services.io.offline);
  },

  // BrowserOffline Implementation Methods
  _canGoOffline() {
    try {
      var cancelGoOffline = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
        Ci.nsISupportsPRBool
      );
      Services.obs.notifyObservers(cancelGoOffline, "offline-requested");

      // Something aborted the quit process.
      if (cancelGoOffline.data) {
        return false;
      }
    } catch (ex) {}

    return true;
  },

  _uiElement: null,
  _updateOfflineUI(aOffline) {
    var offlineLocked = Services.prefs.prefIsLocked("network.online");
    if (offlineLocked) {
      this._uiElement.setAttribute("disabled", "true");
    }

    this._uiElement.setAttribute("checked", aOffline);
  },
};

var CanvasPermissionPromptHelper = {
  _permissionsPrompt: "canvas-permissions-prompt",
  _permissionsPromptHideDoorHanger: "canvas-permissions-prompt-hide-doorhanger",
  _notificationIcon: "canvas-notification-icon",

  init() {
    Services.obs.addObserver(this, this._permissionsPrompt);
    Services.obs.addObserver(this, this._permissionsPromptHideDoorHanger);
  },

  uninit() {
    Services.obs.removeObserver(this, this._permissionsPrompt);
    Services.obs.removeObserver(this, this._permissionsPromptHideDoorHanger);
  },

  // aSubject is an nsIBrowser (e10s) or an nsIDOMWindow (non-e10s).
  // aData is an Origin string.
  observe(aSubject, aTopic, aData) {
    if (
      aTopic != this._permissionsPrompt &&
      aTopic != this._permissionsPromptHideDoorHanger
    ) {
      return;
    }

    let browser;
    if (aSubject instanceof Ci.nsIDOMWindow) {
      browser = aSubject.docShell.chromeEventHandler;
    } else {
      browser = aSubject;
    }

    if (browser?.ownerGlobal !== window) {
      // Must belong to some other window.
      return;
    }

    let message = gNavigatorBundle.getFormattedString(
      "canvas.siteprompt2",
      ["<>"],
      1
    );

    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(aData);

    function setCanvasPermission(aPerm, aPersistent) {
      Services.perms.addFromPrincipal(
        principal,
        "canvas",
        aPerm,
        aPersistent
          ? Ci.nsIPermissionManager.EXPIRE_NEVER
          : Ci.nsIPermissionManager.EXPIRE_SESSION
      );
    }

    let mainAction = {
      label: gNavigatorBundle.getString("canvas.allow2"),
      accessKey: gNavigatorBundle.getString("canvas.allow2.accesskey"),
      callback(state) {
        setCanvasPermission(
          Ci.nsIPermissionManager.ALLOW_ACTION,
          state && state.checkboxChecked
        );
      },
    };

    let secondaryActions = [
      {
        label: gNavigatorBundle.getString("canvas.block"),
        accessKey: gNavigatorBundle.getString("canvas.block.accesskey"),
        callback(state) {
          setCanvasPermission(
            Ci.nsIPermissionManager.DENY_ACTION,
            state && state.checkboxChecked
          );
        },
      },
    ];

    let checkbox = {
      // In PB mode, we don't want the "always remember" checkbox
      show: !PrivateBrowsingUtils.isWindowPrivate(window),
    };
    if (checkbox.show) {
      checkbox.checked = true;
      checkbox.label = gBrowserBundle.GetStringFromName("canvas.remember2");
    }

    let options = {
      checkbox,
      name: principal.host,
      learnMoreURL:
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
        "fingerprint-permission",
      dismissed: aTopic == this._permissionsPromptHideDoorHanger,
      eventCallback(e) {
        if (e == "showing") {
          this.browser.ownerDocument.getElementById(
            "canvas-permissions-prompt-warning"
          ).textContent = gBrowserBundle.GetStringFromName(
            "canvas.siteprompt2.warning"
          );
        }
      },
    };
    PopupNotifications.show(
      browser,
      this._permissionsPrompt,
      message,
      this._notificationIcon,
      mainAction,
      secondaryActions,
      options
    );
  },
};

var WebAuthnPromptHelper = {
  _icon: "webauthn-notification-icon",
  _topic: "webauthn-prompt",

  // The current notification, if any. The U2F manager is a singleton, we will
  // never allow more than one active request. And thus we'll never have more
  // than one notification either.
  _current: null,

  // The current transaction ID. Will be checked when we're notified of the
  // cancellation of an ongoing WebAuthhn request.
  _tid: 0,

  // Translation object
  _l10n: null,

  init() {
    this._l10n = new Localization(["browser/webauthnDialog.ftl"], true);
    Services.obs.addObserver(this, this._topic);
  },

  uninit() {
    Services.obs.removeObserver(this, this._topic);
  },

  observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "fullscreen-nav-toolbox":
        // Prevent the navigation toolbox from being hidden while a WebAuthn
        // prompt is visible.
        if (aData == "hidden" && this._tid != 0) {
          FullScreen.showNavToolbox();
        }
        return;
      case "fullscreen-painted":
        // Prevent DOM elements from going fullscreen while a WebAuthn
        // prompt is shown.
        if (this._tid != 0) {
          FullScreen.exitDomFullScreen();
        }
        return;
      case this._topic:
        break;
      default:
        return;
    }
    // aTopic is equal to this._topic

    let data = JSON.parse(aData);

    // If we receive a cancel, it might be a WebAuthn prompt starting in another
    // window, and the other window's browsing context will send out the
    // cancellations, so any cancel action we get should prompt us to cancel.
    if (data.prompt.type == "cancel") {
      this.cancel(data);
      return;
    }

    if (
      data.browsingContextId !== gBrowser.selectedBrowser.browsingContext.id
    ) {
      // Must belong to some other window.
      return;
    }

    let mgr = Cc["@mozilla.org/webauthn/service;1"].getService(
      Ci.nsIWebAuthnService
    );

    if (data.prompt.type == "presence") {
      this.presence_required(mgr, data);
    } else if (data.prompt.type == "attestation-consent") {
      this.attestation_consent(mgr, data);
    } else if (data.prompt.type == "pin-required") {
      this.pin_required(mgr, false, data);
    } else if (data.prompt.type == "pin-invalid") {
      this.pin_required(mgr, true, data);
    } else if (data.prompt.type == "select-sign-result") {
      this.select_sign_result(mgr, data);
    } else if (data.prompt.type == "already-registered") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "alreadyRegistered",
        "webauthn.alreadyRegisteredPrompt"
      );
    } else if (data.prompt.type == "select-device") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "selectDevice",
        "webauthn.selectDevicePrompt"
      );
    } else if (data.prompt.type == "pin-auth-blocked") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "pinAuthBlocked",
        "webauthn.pinAuthBlockedPrompt"
      );
    } else if (data.prompt.type == "uv-blocked") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "uvBlocked",
        "webauthn.uvBlockedPrompt"
      );
    } else if (data.prompt.type == "uv-invalid") {
      let retriesLeft = data.prompt.retries;
      let dialogText;
      if (retriesLeft === 0) {
        // We can skip that because it will either be replaced
        // by uv-blocked or by PIN-prompt
        return;
      } else if (retriesLeft == null || retriesLeft < 0) {
        dialogText = this._l10n.formatValueSync(
          "webauthn-uv-invalid-short-prompt"
        );
      } else {
        dialogText = this._l10n.formatValueSync(
          "webauthn-uv-invalid-long-prompt",
          { retriesLeft }
        );
      }
      let mainAction = this.buildCancelAction(mgr, data.tid);
      this.show_formatted_msg(data.tid, "uvInvalid", dialogText, mainAction);
    } else if (data.prompt.type == "device-blocked") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "deviceBlocked",
        "webauthn.deviceBlockedPrompt"
      );
    } else if (data.prompt.type == "pin-not-set") {
      this.show_info(
        mgr,
        data.origin,
        data.tid,
        "pinNotSet",
        "webauthn.pinNotSetPrompt"
      );
    }
  },

  prompt_for_password(origin, wasInvalid, retriesLeft, aPassword) {
    this.reset();
    let dialogText;
    if (!wasInvalid) {
      dialogText = this._l10n.formatValueSync("webauthn-pin-required-prompt");
    } else if (retriesLeft == null || retriesLeft < 0 || retriesLeft > 3) {
      // The token will need to be power cycled after three incorrect attempts,
      // so we show a short error message that does not include retriesLeft. It
      // would be confusing to display retriesLeft at this point, as the user
      // will feel that they only get three attempts.
      // We also only show the short prompt in the case the token doesn't
      // support/send a retries-counter. Then we simply don't know how many are left.
      dialogText = this._l10n.formatValueSync(
        "webauthn-pin-invalid-short-prompt"
      );
    } else {
      // The user is close to having their PIN permanently blocked. Show a more
      // severe warning that includes the retriesLeft counter.
      dialogText = this._l10n.formatValueSync(
        "webauthn-pin-invalid-long-prompt",
        { retriesLeft }
      );
    }

    let res = Services.prompt.promptPasswordBC(
      gBrowser.selectedBrowser.browsingContext,
      Services.prompt.MODAL_TYPE_TAB,
      origin,
      dialogText,
      aPassword
    );
    return res;
  },

  select_sign_result(mgr, { origin, tid, prompt: { entities } }) {
    let unknownAccount = this._l10n.formatValueSync(
      "webauthn-select-sign-result-unknown-account"
    );
    let secondaryActions = [];
    for (let i = 0; i < entities.length; i++) {
      let label = entities[i].name ?? unknownAccount;
      secondaryActions.push({
        label,
        accessKey: i.toString(),
        callback() {
          mgr.selectionCallback(tid, i);
        },
      });
    }
    let mainAction = this.buildCancelAction(mgr, tid);
    let options = { escAction: "buttoncommand" };
    this.show(
      tid,
      "select-sign-result",
      "webauthn.selectSignResultPrompt",
      origin,
      mainAction,
      secondaryActions,
      options
    );
  },

  pin_required(mgr, wasInvalid, { origin, tid, prompt: { retries } }) {
    let aPassword = Object.create(null); // create a "null" object
    let res = this.prompt_for_password(origin, wasInvalid, retries, aPassword);
    if (res) {
      mgr.pinCallback(tid, aPassword.value);
    } else {
      mgr.cancel(tid);
    }
  },

  presence_required(mgr, { origin, tid }) {
    let mainAction = this.buildCancelAction(mgr, tid);
    let options = { escAction: "buttoncommand" };
    let secondaryActions = [];
    let message = "webauthn.userPresencePrompt";
    this.show(
      tid,
      "presence",
      message,
      origin,
      mainAction,
      secondaryActions,
      options
    );
  },

  attestation_consent(mgr, { origin, tid }) {
    let mainAction = {
      label: gNavigatorBundle.getString("webauthn.allow"),
      accessKey: gNavigatorBundle.getString("webauthn.allow.accesskey"),
      callback(_state) {
        mgr.setHasAttestationConsent(tid, true);
      },
    };
    let secondaryActions = [
      {
        label: gNavigatorBundle.getString("webauthn.block"),
        accessKey: gNavigatorBundle.getString("webauthn.block.accesskey"),
        callback(_state) {
          mgr.setHasAttestationConsent(tid, false);
        },
      },
    ];

    let learnMoreURL =
      Services.urlFormatter.formatURLPref("app.support.baseURL") +
      "webauthn-direct-attestation";

    let options = {
      learnMoreURL,
      hintText: "webauthn.registerDirectPromptHint",
    };
    this.show(
      tid,
      "register-direct",
      "webauthn.registerDirectPrompt3",
      origin,
      mainAction,
      secondaryActions,
      options
    );
  },

  show_info(mgr, origin, tid, id, stringId) {
    let mainAction = this.buildCancelAction(mgr, tid);
    this.show(tid, id, stringId, origin, mainAction);
  },

  show(
    tid,
    id,
    stringId,
    origin,
    mainAction,
    secondaryActions = [],
    options = {}
  ) {
    let brandShortName = document
      .getElementById("bundle_brand")
      .getString("brandShortName");
    let message = gNavigatorBundle.getFormattedString(stringId, [
      "<>",
      brandShortName,
    ]);

    try {
      origin = Services.io.newURI(origin).asciiHost;
    } catch (e) {
      /* Might fail for arbitrary U2F RP IDs. */
    }
    options.name = origin;
    this.show_formatted_msg(
      tid,
      id,
      message,
      mainAction,
      secondaryActions,
      options
    );
  },

  show_formatted_msg(
    tid,
    id,
    message,
    mainAction,
    secondaryActions = [],
    options = {}
  ) {
    this.reset();
    this._tid = tid;

    // We need to prevent some fullscreen transitions while WebAuthn prompts
    // are shown. The `fullscreen-painted` topic is notified when DOM elements
    // go fullscreen.
    Services.obs.addObserver(this, "fullscreen-painted");

    // The `fullscreen-nav-toolbox` topic is notified when the nav toolbox is
    // hidden.
    Services.obs.addObserver(this, "fullscreen-nav-toolbox");

    // Ensure that no DOM elements are already fullscreen.
    FullScreen.exitDomFullScreen();

    // Ensure that the nav toolbox is being shown.
    if (window.fullScreen) {
      FullScreen.showNavToolbox();
    }

    let brandShortName = document
      .getElementById("bundle_brand")
      .getString("brandShortName");
    if (options.hintText) {
      options.hintText = gNavigatorBundle.getFormattedString(options.hintText, [
        brandShortName,
      ]);
    }

    options.hideClose = true;
    options.persistent = true;
    options.eventCallback = event => {
      if (event == "removed") {
        Services.obs.removeObserver(this, "fullscreen-painted");
        Services.obs.removeObserver(this, "fullscreen-nav-toolbox");
        this._current = null;
        this._tid = 0;
      }
    };

    this._current = PopupNotifications.show(
      gBrowser.selectedBrowser,
      `webauthn-prompt-${id}`,
      message,
      this._icon,
      mainAction,
      secondaryActions,
      options
    );
  },

  cancel({ tid }) {
    if (this._tid == tid) {
      this.reset();
    }
  },

  reset() {
    if (this._current) {
      this._current.remove();
    }
  },

  buildCancelAction(mgr, tid) {
    return {
      label: gNavigatorBundle.getString("webauthn.cancel"),
      accessKey: gNavigatorBundle.getString("webauthn.cancel.accesskey"),
      callback() {
        mgr.cancel(tid);
      },
    };
  },
};

function CanCloseWindow() {
  // Avoid redundant calls to canClose from showing multiple
  // PermitUnload dialogs.
  if (Services.startup.shuttingDown || window.skipNextCanClose) {
    return true;
  }

  for (let browser of gBrowser.browsers) {
    // Don't instantiate lazy browsers.
    if (!browser.isConnected) {
      continue;
    }

    let { permitUnload } = browser.permitUnload();
    if (!permitUnload) {
      return false;
    }
  }
  return true;
}

function WindowIsClosing(event) {
  let source;
  if (event) {
    let target = event.sourceEvent?.target;
    if (target?.id?.startsWith("menu_")) {
      source = "menuitem";
    } else if (target?.nodeName == "toolbarbutton") {
      source = "close-button";
    } else {
      let key = AppConstants.platform == "macosx" ? "metaKey" : "ctrlKey";
      source = event[key] ? "shortcut" : "OS";
    }
  }
  if (!closeWindow(false, warnAboutClosingWindow, source)) {
    return false;
  }

  // In theory we should exit here and the Window's internal Close
  // method should trigger canClose on BrowserDOMWindow. However, by
  // that point it's too late to be able to show a prompt for
  // PermitUnload. So we do it here, when we still can.
  if (CanCloseWindow()) {
    // This flag ensures that the later canClose call does nothing.
    // It's only needed to make tests pass, since they detect the
    // prompt even when it's not actually shown.
    window.skipNextCanClose = true;
    return true;
  }

  return false;
}

/**
 * Checks if this is the last full *browser* window around. If it is, this will
 * be communicated like quitting. Otherwise, we warn about closing multiple tabs.
 *
 * @returns true if closing can proceed, false if it got cancelled.
 */
function warnAboutClosingWindow() {
  // Popups aren't considered full browser windows; we also ignore private windows.
  let isPBWindow =
    PrivateBrowsingUtils.isWindowPrivate(window) &&
    !PrivateBrowsingUtils.permanentPrivateBrowsing;

  if (!isPBWindow && !toolbar.visible) {
    return gBrowser.warnAboutClosingTabs(
      gBrowser.openTabs.length,
      gBrowser.closingTabsEnum.ALL
    );
  }

  // Figure out if there's at least one other browser window around.
  let otherPBWindowExists = false;
  let otherWindowExists = false;
  for (let win of browserWindows()) {
    if (!win.closed && win != window) {
      otherWindowExists = true;
      if (isPBWindow && PrivateBrowsingUtils.isWindowPrivate(win)) {
        otherPBWindowExists = true;
      }
      // If the current window is not in private browsing mode we don't need to
      // look for other pb windows, we can leave the loop when finding the
      // first non-popup window. If however the current window is in private
      // browsing mode then we need at least one other pb and one non-popup
      // window to break out early.
      if (!isPBWindow || otherPBWindowExists) {
        break;
      }
    }
  }

  if (isPBWindow && !otherPBWindowExists) {
    let exitingCanceled = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    exitingCanceled.data = false;
    Services.obs.notifyObservers(exitingCanceled, "last-pb-context-exiting");
    if (exitingCanceled.data) {
      return false;
    }
  }

  if (otherWindowExists) {
    return (
      isPBWindow ||
      gBrowser.warnAboutClosingTabs(
        gBrowser.openTabs.length,
        gBrowser.closingTabsEnum.ALL
      )
    );
  }

  let os = Services.obs;

  let closingCanceled = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
    Ci.nsISupportsPRBool
  );
  os.notifyObservers(closingCanceled, "browser-lastwindow-close-requested");
  if (closingCanceled.data) {
    return false;
  }

  os.notifyObservers(null, "browser-lastwindow-close-granted");

  // OS X doesn't quit the application when the last window is closed, but keeps
  // the session alive. Hence don't prompt users to save tabs, but warn about
  // closing multiple tabs.
  return (
    AppConstants.platform != "macosx" ||
    isPBWindow ||
    gBrowser.warnAboutClosingTabs(
      gBrowser.openTabs.length,
      gBrowser.closingTabsEnum.ALL
    )
  );
}

var MailIntegration = {
  sendLinkForBrowser(aBrowser) {
    this.sendMessage(
      gURLBar.makeURIReadable(aBrowser.currentURI).displaySpec,
      aBrowser.contentTitle
    );
  },

  sendMessage(aBody, aSubject) {
    // generate a mailto url based on the url and the url's title
    var mailtoUrl = "mailto:";
    if (aBody) {
      mailtoUrl += "?body=" + encodeURIComponent(aBody);
      mailtoUrl += "&subject=" + encodeURIComponent(aSubject);
    }

    var uri = makeURI(mailtoUrl);

    // now pass this uri to the operating system
    this._launchExternalUrl(uri);
  },

  // a generic method which can be used to pass arbitrary urls to the operating
  // system.
  // aURL --> a nsIURI which represents the url to launch
  _launchExternalUrl(aURL) {
    var extProtocolSvc = Cc[
      "@mozilla.org/uriloader/external-protocol-service;1"
    ].getService(Ci.nsIExternalProtocolService);
    if (extProtocolSvc) {
      extProtocolSvc.loadURI(
        aURL,
        Services.scriptSecurityManager.getSystemPrincipal()
      );
    }
  },
};

/**
 * Applies only to the cmd|ctrl + shift + T keyboard shortcut
 * Undo the last action that was taken - either closing the last tab or closing the last window;
 * If none of those were the last actions, restore the last session if possible.
 */
function restoreLastClosedTabOrWindowOrSession() {
  let lastActionTaken = SessionStore.popLastClosedAction();

  if (lastActionTaken) {
    switch (lastActionTaken.type) {
      case SessionStore.LAST_ACTION_CLOSED_TAB: {
        undoCloseTab();
        break;
      }
      case SessionStore.LAST_ACTION_CLOSED_WINDOW: {
        undoCloseWindow();
        break;
      }
    }
  } else {
    let closedTabCount = SessionStore.getLastClosedTabCount(window);
    if (SessionStore.canRestoreLastSession) {
      SessionStore.restoreLastSession();
    } else if (closedTabCount) {
      // we need to support users who have automatic session restore enabled
      undoCloseTab();
    }
  }
}

/**
 * Re-open a closed tab into the current window.
 * @param [aIndex]
 *        The index of the tab (via SessionStore.getClosedTabData).
 *        When undefined, the first n closed tabs will be re-opened, where n is provided by getLastClosedTabCount.
 * @param {string} [sourceWindowSSId]
 *        An optional sessionstore id to identify the source window for the tab.
 *        I.e. the window the tab belonged to when closed.
 *        When undefined we'll use the current window
 * @returns a reference to the reopened tab.
 */
function undoCloseTab(aIndex, sourceWindowSSId) {
  // the window we'll open the tab into
  let targetWindow = window;
  // the window the tab was closed from
  let sourceWindow;
  if (sourceWindowSSId) {
    sourceWindow = SessionStore.getWindowById(sourceWindowSSId);
    if (!sourceWindow) {
      throw new Error(
        "sourceWindowSSId argument to undoCloseTab didn't resolve to a window"
      );
    }
  } else {
    sourceWindow = window;
  }

  // wallpaper patch to prevent an unnecessary blank tab (bug 343895)
  let blankTabToRemove = null;
  if (
    targetWindow.gBrowser.visibleTabs.length == 1 &&
    targetWindow.gBrowser.selectedTab.isEmpty
  ) {
    blankTabToRemove = targetWindow.gBrowser.selectedTab;
  }

  let tabsRemoved = false;
  let tab = null;
  const lastClosedTabGroupId =
    SessionStore.getLastClosedTabGroupId(sourceWindow);
  if (aIndex === undefined && lastClosedTabGroupId) {
    let group;
    if (SessionStore.getSavedTabGroup(lastClosedTabGroupId)) {
      group = SessionStore.openSavedTabGroup(
        lastClosedTabGroupId,
        targetWindow,
        {
          source: "recent",
        }
      );
    } else {
      group = SessionStore.undoCloseTabGroup(
        window,
        lastClosedTabGroupId,
        targetWindow
      );
    }
    tabsRemoved = true;
    tab = group.tabs.at(-1);
  } else {
    // We are specifically interested in the lastClosedTabCount for the source window.
    // When aIndex is undefined, we restore all the lastClosedTabCount tabs.
    let lastClosedTabCount = SessionStore.getLastClosedTabCount(sourceWindow);
    // aIndex is undefined if the function is called without a specific tab to restore.
    let tabsToRemove =
      aIndex !== undefined ? [aIndex] : new Array(lastClosedTabCount).fill(0);
    for (let index of tabsToRemove) {
      if (SessionStore.getClosedTabCountForWindow(sourceWindow) > index) {
        tab = SessionStore.undoCloseTab(sourceWindow, index, targetWindow);
        tabsRemoved = true;
      }
    }
  }

  if (tabsRemoved && blankTabToRemove) {
    targetWindow.gBrowser.removeTab(blankTabToRemove);
  }

  return tab;
}

/**
 * Re-open a closed window.
 * @param aIndex
 *        The index of the window (via SessionStore.getClosedWindowData)
 * @returns a reference to the reopened window.
 */
function undoCloseWindow(aIndex) {
  let window = null;
  if (SessionStore.getClosedWindowCount() > (aIndex || 0)) {
    window = SessionStore.undoCloseWindow(aIndex || 0);
  }

  return window;
}

/**
 * When the browser is being controlled from out-of-process,
 * e.g. when Marionette or the remote debugging protocol is used,
 * we add a visual hint to the browser UI to indicate to the user
 * that the browser session is under remote control.
 *
 * This is called when the content browser initialises (from gBrowserInit.onLoad())
 * and when the "remote-listening" system notification fires.
 */
const gRemoteControl = {
  observe() {
    gRemoteControl.updateVisualCue();
  },

  updateVisualCue() {
    // Disable updating the remote control cue for performance tests,
    // because these could fail due to an early initialization of Marionette.
    const disableRemoteControlCue = Services.prefs.getBoolPref(
      "browser.chrome.disableRemoteControlCueForTests",
      false
    );
    if (disableRemoteControlCue && Cu.isInAutomation) {
      return;
    }

    const mainWindow = document.documentElement;
    const remoteControlComponent = this.getRemoteControlComponent();
    if (remoteControlComponent) {
      mainWindow.setAttribute("remotecontrol", "true");
      const remoteControlIcon = document.getElementById("remote-control-icon");
      document.l10n.setAttributes(
        remoteControlIcon,
        "urlbar-remote-control-notification-anchor2",
        { component: remoteControlComponent }
      );
    } else {
      mainWindow.removeAttribute("remotecontrol");
    }
  },

  getRemoteControlComponent() {
    // For DevTools sockets, only show the remote control cue if the socket is
    // not coming from a regular Browser Toolbox debugging session.
    if (
      DevToolsSocketStatus.hasSocketOpened({
        excludeBrowserToolboxSockets: true,
      })
    ) {
      return "DevTools";
    }

    if (Marionette.running) {
      return "Marionette";
    }

    if (RemoteAgent.running) {
      return "RemoteAgent";
    }

    return null;
  },
};

// Note that this is also called from non-browser windows on OSX, which do
// share menu items but not much else. See nonbrowser-mac.js.
var gPrivateBrowsingUI = {
  init: function PBUI_init() {
    // Do nothing for normal windows
    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }

    // Disable the Clear Recent History... menu item when in PB mode
    // temporary fix until bug 463607 is fixed
    document.getElementById("Tools:Sanitize").setAttribute("disabled", "true");

    if (window.location.href != AppConstants.BROWSER_CHROME_URL) {
      return;
    }

    // Adjust the window's title
    let docElement = document.documentElement;
    docElement.setAttribute(
      "privatebrowsingmode",
      PrivateBrowsingUtils.permanentPrivateBrowsing ? "permanent" : "temporary"
    );

    gBrowser.updateTitlebar();

    // Bug 1846583 - hide pocket button in PBM
    if (gUseFeltPrivacyUI) {
      const saveToPocketButton = document.getElementById(
        "save-to-pocket-button"
      );
      if (saveToPocketButton) {
        saveToPocketButton.remove();
        document.documentElement.setAttribute("pocketdisabled", "true");
      }
    }

    if (PrivateBrowsingUtils.permanentPrivateBrowsing) {
      let hideNewWindowItem = (windowItem, privateWindowItem) => {
        // In permanent browsing mode command "cmd_newNavigator" should act the
        // same as "Tools:PrivateBrowsing".
        // So we hide the redundant private window item. But we also rename the
        // "new window" item to be "new private window".
        // NOTE: We choose to hide privateWindowItem rather than windowItem so
        // that we still show the "key" for "cmd_newNavigator" (Ctrl+N) rather
        // than (Ctrl+Shift+P).
        privateWindowItem.hidden = true;
        windowItem.setAttribute(
          "data-l10n-id",
          privateWindowItem.getAttribute("data-l10n-id")
        );
      };

      // Adjust the File menu items.
      hideNewWindowItem(
        document.getElementById("menu_newNavigator"),
        document.getElementById("menu_newPrivateWindow")
      );
      // Adjust the App menu items.
      hideNewWindowItem(
        PanelMultiView.getViewNode(document, "appMenu-new-window-button2"),
        PanelMultiView.getViewNode(
          document,
          "appMenu-new-private-window-button2"
        )
      );
    }
  },
};

/**
 * Switch to a tab that has a given URI, and focuses its browser window.
 * If a matching tab is in this window, it will be switched to. Otherwise, other
 * windows will be searched.
 *
 * @param aURI
 *        URI to search for
 * @param aOpenNew
 *        True to open a new tab and switch to it, if no existing tab is found.
 *        If no suitable window is found, a new one will be opened.
 * @param aOpenParams
 *        If switching to this URI results in us opening a tab, aOpenParams
 *        will be the parameter object that gets passed to openTrustedLinkIn. Please
 *        see the documentation for openTrustedLinkIn to see what parameters can be
 *        passed via this object.
 *        This object also allows:
 *        - 'ignoreFragment' property to be set to true to exclude fragment-portion
 *        matching when comparing URIs.
 *          If set to "whenComparing", the fragment will be unmodified.
 *          If set to "whenComparingAndReplace", the fragment will be replaced.
 *        - 'ignoreQueryString' boolean property to be set to true to exclude query string
 *        matching when comparing URIs.
 *        - 'replaceQueryString' boolean property to be set to true to exclude query string
 *        matching when comparing URIs and overwrite the initial query string with
 *        the one from the new URI.
 *        - 'adoptIntoActiveWindow' boolean property to be set to true to adopt the tab
 *        into the current window.
 * @param aUserContextId
 *        If not null, will switch to the first found tab having the provided
 *        userContextId.
 * @return True if an existing tab was found, false otherwise
 */
function switchToTabHavingURI(
  aURI,
  aOpenNew,
  aOpenParams = {},
  aUserContextId = null
) {
  // Certain URLs can be switched to irrespective of the source or destination
  // window being in private browsing mode:
  const kPrivateBrowsingWhitelist = new Set(["about:addons"]);

  let ignoreFragment = aOpenParams.ignoreFragment;
  let ignoreQueryString = aOpenParams.ignoreQueryString;
  let replaceQueryString = aOpenParams.replaceQueryString;
  let adoptIntoActiveWindow = aOpenParams.adoptIntoActiveWindow;

  // These properties are only used by switchToTabHavingURI and should
  // not be used as a parameter for the new load.
  delete aOpenParams.ignoreFragment;
  delete aOpenParams.ignoreQueryString;
  delete aOpenParams.replaceQueryString;
  delete aOpenParams.adoptIntoActiveWindow;

  let isBrowserWindow = !!window.gBrowser;

  // This will switch to the tab in aWindow having aURI, if present.
  function switchIfURIInWindow(aWindow) {
    // We can switch tab only if if both the source and destination windows have
    // the same private-browsing status.
    if (
      !kPrivateBrowsingWhitelist.has(aURI.spec) &&
      PrivateBrowsingUtils.isWindowPrivate(window) !==
        PrivateBrowsingUtils.isWindowPrivate(aWindow)
    ) {
      return false;
    }

    // Remove the query string, fragment, both, or neither from a given url.
    function cleanURL(url, removeQuery, removeFragment) {
      let ret = url;
      if (removeFragment) {
        ret = ret.split("#")[0];
        if (removeQuery) {
          // This removes a query, if present before the fragment.
          ret = ret.split("?")[0];
        }
      } else if (removeQuery) {
        // This is needed in case there is a fragment after the query.
        let fragment = ret.split("#")[1];
        ret = ret
          .split("?")[0]
          .concat(fragment != undefined ? "#".concat(fragment) : "");
      }
      return ret;
    }

    // Need to handle nsSimpleURIs here too (e.g. about:...), which don't
    // work correctly with URL objects - so treat them as strings
    let ignoreFragmentWhenComparing =
      typeof ignoreFragment == "string" &&
      ignoreFragment.startsWith("whenComparing");
    let requestedCompare = cleanURL(
      aURI.displaySpec,
      ignoreQueryString || replaceQueryString,
      ignoreFragmentWhenComparing
    );
    let browsers = aWindow.gBrowser.browsers;
    for (let i = 0; i < browsers.length; i++) {
      let browser = browsers[i];
      let browserCompare = cleanURL(
        browser.currentURI.displaySpec,
        ignoreQueryString || replaceQueryString,
        ignoreFragmentWhenComparing
      );
      let browserUserContextId = browser.getAttribute("usercontextid") || "";
      if (aUserContextId != null && aUserContextId != browserUserContextId) {
        continue;
      }
      if (requestedCompare == browserCompare) {
        // If adoptIntoActiveWindow is set, and this is a cross-window switch,
        // adopt the tab into the current window, after the active tab.
        let doAdopt =
          adoptIntoActiveWindow && isBrowserWindow && aWindow != window;

        if (doAdopt) {
          const newTab = window.gBrowser.adoptTab(
            aWindow.gBrowser.getTabForBrowser(browser),
            {
              tabIndex: window.gBrowser.tabContainer.selectedIndex + 1,
              selectTab: true,
            }
          );
          if (!newTab) {
            doAdopt = false;
          }
        }
        if (!doAdopt) {
          aWindow.focus();
        }

        if (ignoreFragment == "whenComparingAndReplace" || replaceQueryString) {
          browser.loadURI(aURI, {
            triggeringPrincipal:
              aOpenParams.triggeringPrincipal ||
              _createNullPrincipalFromTabUserContextId(),
          });
        }

        if (!doAdopt) {
          aWindow.gBrowser.tabContainer.selectedIndex = i;
        }

        return true;
      }
    }
    return false;
  }

  // This can be passed either nsIURI or a string.
  if (!(aURI instanceof Ci.nsIURI)) {
    aURI = Services.io.newURI(aURI);
  }

  // Prioritise this window.
  if (isBrowserWindow && switchIfURIInWindow(window)) {
    return true;
  }

  for (let browserWin of browserWindows()) {
    // Skip closed (but not yet destroyed) windows,
    // and the current window (which was checked earlier).
    if (browserWin.closed || browserWin == window) {
      continue;
    }
    if (switchIfURIInWindow(browserWin)) {
      return true;
    }
  }

  // No opened tab has that url.
  if (aOpenNew) {
    if (
      UrlbarPrefs.get("switchTabs.searchAllContainers") &&
      aUserContextId != null
    ) {
      aOpenParams.userContextId = aUserContextId;
    }
    if (isBrowserWindow && gBrowser.selectedTab.isEmpty) {
      openTrustedLinkIn(aURI.spec, "current", aOpenParams);
    } else {
      openTrustedLinkIn(aURI.spec, "tab", aOpenParams);
    }
  }

  return false;
}

var RestoreLastSessionObserver = {
  init() {
    if (
      SessionStore.canRestoreLastSession &&
      !PrivateBrowsingUtils.isWindowPrivate(window)
    ) {
      Services.obs.addObserver(this, "sessionstore-last-session-cleared", true);
      Services.obs.addObserver(
        this,
        "sessionstore-last-session-re-enable",
        true
      );
      goSetCommandEnabled("Browser:RestoreLastSession", true);
    } else if (SessionStore.willAutoRestore) {
      document.getElementById("Browser:RestoreLastSession").hidden = true;
    }
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "sessionstore-last-session-cleared":
        goSetCommandEnabled("Browser:RestoreLastSession", false);
        break;
      case "sessionstore-last-session-re-enable":
        goSetCommandEnabled("Browser:RestoreLastSession", true);
        break;
    }
  },

  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),
};

/* Observes menus and adjusts their size for better
 * usability when opened via a touch screen. */
var MenuTouchModeObserver = {
  init() {
    window.addEventListener("popupshowing", this, true);
  },

  handleEvent(event) {
    let target = event.originalTarget;
    if (event.inputSource == MouseEvent.MOZ_SOURCE_TOUCH) {
      target.setAttribute("touchmode", "true");
    } else {
      target.removeAttribute("touchmode");
    }
  },

  uninit() {
    window.removeEventListener("popupshowing", this, true);
  },
};

// Prompt user to restart the browser in safe mode
function safeModeRestart() {
  if (Services.appinfo.inSafeMode) {
    let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
      Ci.nsISupportsPRBool
    );
    Services.obs.notifyObservers(
      cancelQuit,
      "quit-application-requested",
      "restart"
    );

    if (cancelQuit.data) {
      return;
    }

    Services.startup.quit(
      Ci.nsIAppStartup.eRestart | Ci.nsIAppStartup.eAttemptQuit
    );
    return;
  }

  Services.obs.notifyObservers(window, "restart-in-safe-mode");
}

/* duplicateTabIn duplicates tab in a place specified by the parameter |where|.
 *
 * |where| can be:
 *  "tab"         new tab
 *  "tabshifted"  same as "tab" but in background if default is to select new
 *                tabs, and vice versa
 *  "window"      new window
 *
 * delta is the offset to the history entry that you want to load.
 */
function duplicateTabIn(aTab, where, delta) {
  switch (where) {
    case "window": {
      let otherWin = OpenBrowserWindow({
        private: PrivateBrowsingUtils.isBrowserPrivate(aTab.linkedBrowser),
      });
      let delayedStartupFinished = (subject, topic) => {
        if (
          topic == "browser-delayed-startup-finished" &&
          subject == otherWin
        ) {
          Services.obs.removeObserver(delayedStartupFinished, topic);
          let otherGBrowser = otherWin.gBrowser;
          let otherTab = otherGBrowser.selectedTab;
          SessionStore.duplicateTab(otherWin, aTab, delta);
          otherGBrowser.removeTab(otherTab, { animate: false });
        }
      };

      Services.obs.addObserver(
        delayedStartupFinished,
        "browser-delayed-startup-finished"
      );
      break;
    }
    case "tabshifted":
      SessionStore.duplicateTab(window, aTab, delta);
      // A background tab has been opened, nothing else to do here.
      break;
    case "tab":
      SessionStore.duplicateTab(window, aTab, delta, true, {
        inBackground: false,
      });
      break;
  }
  if (aTab.group) {
    Glean.tabgroup.tabInteractions.duplicate.add();
  }
}

var MousePosTracker = {
  _listeners: new Set(),
  _x: 0,
  _y: 0,

  /**
   * Registers a listener.
   *
   * @param listener (object)
   *        A listener is expected to expose the following properties:
   *
   *        getMouseTargetRect (function)
   *          Returns the rect that the MousePosTracker needs to alert
   *          the listener about if the mouse happens to be within it.
   *
   *        onMouseEnter (function, optional)
   *          The function to be called if the mouse enters the rect
   *          returned by getMouseTargetRect. MousePosTracker always
   *          runs this inside of a requestAnimationFrame, since it
   *          assumes that the notification is used to update the DOM.
   *
   *        onMouseLeave (function, optional)
   *          The function to be called if the mouse exits the rect
   *          returned by getMouseTargetRect. MousePosTracker always
   *          runs this inside of a requestAnimationFrame, since it
   *          assumes that the notification is used to update the DOM.
   */
  addListener(listener) {
    if (this._listeners.has(listener)) {
      return;
    }

    listener._hover = false;
    this._listeners.add(listener);

    this._callListener(listener);
  },

  removeListener(listener) {
    this._listeners.delete(listener);
  },

  handleEvent(event) {
    if (event.type === "mouseout" && event.currentTarget !== window) {
      return;
    }

    this._x = event.screenX - window.mozInnerScreenX;
    this._y = event.screenY - window.mozInnerScreenY;

    this._listeners.forEach(listener => {
      try {
        this._callListener(listener);
      } catch (e) {
        console.error(e);
      }
    });
  },

  _callListener(listener) {
    let rect = listener.getMouseTargetRect();
    let hover =
      this._x >= rect.left &&
      this._x <= rect.right &&
      this._y >= rect.top &&
      this._y <= rect.bottom;

    if (hover == listener._hover) {
      return;
    }

    listener._hover = hover;

    if (hover) {
      if (listener.onMouseEnter) {
        listener.onMouseEnter();
      }
    } else if (listener.onMouseLeave) {
      listener.onMouseLeave();
    }
  },
};

var PanicButtonNotifier = {
  init() {
    this._initialized = true;
    if (window.PanicButtonNotifierShouldNotify) {
      delete window.PanicButtonNotifierShouldNotify;
      this.notify();
    }
  },
  createPanelIfNeeded() {
    // Lazy load the panic-button-success-notification panel the first time we need to display it.
    if (!document.getElementById("panic-button-success-notification")) {
      let template = document.getElementById("panicButtonNotificationTemplate");
      template.replaceWith(template.content);
    }
  },
  notify() {
    if (!this._initialized) {
      window.PanicButtonNotifierShouldNotify = true;
      return;
    }
    // Display notification panel here...
    try {
      this.createPanelIfNeeded();
      let popup = document.getElementById("panic-button-success-notification");
      popup.hidden = false;

      // To close the popup in 3 seconds after the popup is shown but left uninteracted.
      let closePopup = () => {
        popup.hidePopup();
      };
      popup.addEventListener("popupshown", function () {
        PanicButtonNotifier.timer = setTimeout(closePopup, 3000);
      });

      let closeButton = document.getElementById(
        "panic-button-success-closebutton"
      );
      closeButton.addEventListener("command", closePopup);

      // To prevent the popup from closing when user tries to interact with the
      // popup using mouse or keyboard.
      let onUserInteractsWithPopup = () => {
        clearTimeout(PanicButtonNotifier.timer);
      };
      popup.addEventListener("mouseover", onUserInteractsWithPopup);
      window.addEventListener("keydown", onUserInteractsWithPopup);

      let removeListeners = () => {
        popup.removeEventListener("mouseover", onUserInteractsWithPopup);
        window.removeEventListener("keydown", onUserInteractsWithPopup);
        popup.removeEventListener("popuphidden", removeListeners);
        closeButton.removeEventListener("command", closePopup);
        clearTimeout(PanicButtonNotifier.timer);
      };
      popup.addEventListener("popuphidden", removeListeners);

      let widget = CustomizableUI.getWidget("panic-button").forWindow(window);
      let anchor = widget.anchor.icon;
      popup.openPopup(anchor, popup.getAttribute("position"));
    } catch (ex) {
      console.error(ex);
    }
  },
};

/**
 * The TabDialogBox supports opening window dialogs as SubDialogs on the tab and content
 * level. Both tab and content dialogs have their own separate managers.
 * Dialogs will be queued FIFO and cover the web content.
 * Dialogs are closed when the user reloads or leaves the page.
 * While a dialog is open PopupNotifications, such as permission prompts, are
 * suppressed.
 */
class TabDialogBox {
  static _containerFor(browser) {
    return browser.closest(".browserStack, .webextension-popup-stack");
  }

  constructor(browser) {
    this._weakBrowserRef = Cu.getWeakReference(browser);

    // Create parent element for tab dialogs
    let template = document.getElementById("dialogStackTemplate");
    let dialogStack = template.content.cloneNode(true).firstElementChild;
    dialogStack.classList.add("tab-prompt-dialog");

    TabDialogBox._containerFor(browser).appendChild(dialogStack);

    // Initially the stack only contains the template
    let dialogTemplate = dialogStack.firstElementChild;

    // Create dialog manager for prompts at the tab level.
    this._tabDialogManager = new SubDialogManager({
      dialogStack,
      dialogTemplate,
      orderType: SubDialogManager.ORDER_QUEUE,
      allowDuplicateDialogs: true,
      dialogOptions: {
        consumeOutsideClicks: false,
      },
    });
  }

  /**
   * Open a dialog on tab or content level.
   * @param {String} aURL - URL of the dialog to load in the tab box.
   * @param {Object} [aOptions]
   * @param {String} [aOptions.features] - Comma separated list of window
   * features.
   * @param {Boolean} [aOptions.allowDuplicateDialogs] - Whether to allow
   * showing multiple dialogs with aURL at the same time. If false calls for
   * duplicate dialogs will be dropped.
   * @param {String} [aOptions.sizeTo] - Pass "available" to stretch dialog to
   * roughly content size. Any max-width or max-height style values on the document root
   * will also be applied to the dialog box.
   * @param {Boolean} [aOptions.keepOpenSameOriginNav] - By default dialogs are
   * aborted on any navigation.
   * Set to true to keep the dialog open for same origin navigation.
   * @param {Number} [aOptions.modalType] - The modal type to create the dialog for.
   * By default, we show the dialog for tab prompts.
   * @param {Boolean} [aOptions.hideContent] - When true, we are about to show a prompt that is requesting the
   * users credentials for a toplevel load of a resource from a base domain different from the base domain of the currently loaded page.
   * To avoid auth prompt spoofing (see bug 791594) we hide the current sites content
   * (among other protection mechanisms, that are not handled here, see the bug for reference).
   * @returns {Object} [result] Returns an object { closedPromise, dialog }.
   * @returns {Promise} [result.closedPromise] Resolves once the dialog has been closed.
   * @returns {SubDialog} [result.dialog] A reference to the opened SubDialog.
   */
  open(
    aURL,
    {
      features = null,
      allowDuplicateDialogs = true,
      sizeTo,
      keepOpenSameOriginNav,
      modalType = null,
      allowFocusCheckbox = false,
      hideContent = false,
    } = {},
    ...aParams
  ) {
    let resolveClosed;
    let closedPromise = new Promise(resolve => (resolveClosed = resolve));
    // Get the dialog manager to open the prompt with.
    let dialogManager =
      modalType === Ci.nsIPrompt.MODAL_TYPE_CONTENT
        ? this.getContentDialogManager()
        : this._tabDialogManager;

    let hasDialogs = () =>
      this._tabDialogManager.hasDialogs ||
      this._contentDialogManager?.hasDialogs;

    if (!hasDialogs()) {
      this._onFirstDialogOpen();
    }

    let closingCallback = event => {
      if (!hasDialogs()) {
        this._onLastDialogClose();
      }

      if (allowFocusCheckbox && !event.detail?.abort) {
        this.maybeSetAllowTabSwitchPermission(event.target);
      }
    };

    if (modalType == Ci.nsIPrompt.MODAL_TYPE_CONTENT) {
      sizeTo = "limitheight";
    }

    // Open dialog and resolve once it has been closed
    let dialog = dialogManager.open(
      aURL,
      {
        features,
        allowDuplicateDialogs,
        sizeTo,
        closingCallback,
        closedCallback: resolveClosed,
        hideContent,
      },
      ...aParams
    );

    // Marking the dialog externally, instead of passing it as an option.
    // The SubDialog(Manager) does not care about navigation.
    // dialog can be null here if allowDuplicateDialogs = false.
    if (dialog) {
      dialog._keepOpenSameOriginNav = keepOpenSameOriginNav;
    }
    return { closedPromise, dialog };
  }

  _onFirstDialogOpen() {
    // Hide PopupNotifications to prevent them from covering up dialogs.
    this.browser.setAttribute("tabDialogShowing", true);
    UpdatePopupNotificationsVisibility();

    // Register listeners
    this._lastPrincipal = this.browser.contentPrincipal;
    this.browser.addProgressListener(this, Ci.nsIWebProgress.NOTIFY_LOCATION);

    this.tab?.addEventListener("TabClose", this);
  }

  _onLastDialogClose() {
    // Show PopupNotifications again.
    this.browser.removeAttribute("tabDialogShowing");
    UpdatePopupNotificationsVisibility();

    // Clean up listeners
    this.browser.removeProgressListener(this);
    this._lastPrincipal = null;

    this.tab?.removeEventListener("TabClose", this);
  }

  _buildContentPromptDialog() {
    let template = document.getElementById("dialogStackTemplate");
    let contentDialogStack = template.content.cloneNode(true).firstElementChild;
    contentDialogStack.classList.add("content-prompt-dialog");

    // Create a dialog manager for content prompts.
    let browserContainer = TabDialogBox._containerFor(this.browser);
    let tabPromptDialog = browserContainer.querySelector(".tab-prompt-dialog");
    browserContainer.insertBefore(contentDialogStack, tabPromptDialog);

    let contentDialogTemplate = contentDialogStack.firstElementChild;
    this._contentDialogManager = new SubDialogManager({
      dialogStack: contentDialogStack,
      dialogTemplate: contentDialogTemplate,
      orderType: SubDialogManager.ORDER_QUEUE,
      allowDuplicateDialogs: true,
      dialogOptions: {
        consumeOutsideClicks: false,
      },
    });
  }

  handleEvent(event) {
    if (event.type !== "TabClose") {
      return;
    }
    this.abortAllDialogs();
  }

  abortAllDialogs() {
    this._tabDialogManager.abortDialogs();
    this._contentDialogManager?.abortDialogs();
  }

  focus() {
    // Prioritize focusing the dialog manager for tab prompts
    if (this._tabDialogManager._dialogs.length) {
      this._tabDialogManager.focusTopDialog();
      return;
    }
    this._contentDialogManager?.focusTopDialog();
  }

  /**
   * If the user navigates away or refreshes the page, close all dialogs for
   * the current browser.
   */
  onLocationChange(aWebProgress, aRequest, aLocation, aFlags) {
    if (
      !aWebProgress.isTopLevel ||
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT
    ) {
      return;
    }

    // Dialogs can be exempt from closing on same origin location change.
    let filterFn;

    // Test for same origin location change
    if (
      this._lastPrincipal?.isSameOrigin(
        aLocation,
        this.browser.browsingContext.usePrivateBrowsing
      )
    ) {
      filterFn = dialog => !dialog._keepOpenSameOriginNav;
    }

    this._lastPrincipal = this.browser.contentPrincipal;

    this._tabDialogManager.abortDialogs(filterFn);
    this._contentDialogManager?.abortDialogs(filterFn);
  }

  get tab() {
    return gBrowser.getTabForBrowser(this.browser);
  }

  get browser() {
    let browser = this._weakBrowserRef.get();
    if (!browser) {
      throw new Error("Stale dialog box! The associated browser is gone.");
    }
    return browser;
  }

  getTabDialogManager() {
    return this._tabDialogManager;
  }

  getContentDialogManager() {
    if (!this._contentDialogManager) {
      this._buildContentPromptDialog();
    }
    return this._contentDialogManager;
  }

  onNextPromptShowAllowFocusCheckboxFor(principal) {
    this._allowTabFocusByPromptPrincipal = principal;
  }

  /**
   * Sets the "focus-tab-by-prompt" permission for the dialog.
   */
  maybeSetAllowTabSwitchPermission(dialog) {
    let checkbox = dialog.querySelector("checkbox");

    if (checkbox.checked) {
      Services.perms.addFromPrincipal(
        this._allowTabFocusByPromptPrincipal,
        "focus-tab-by-prompt",
        Services.perms.ALLOW_ACTION
      );
    }

    // Don't show the "allow tab switch checkbox" for subsequent prompts.
    this._allowTabFocusByPromptPrincipal = null;
  }
}

TabDialogBox.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIWebProgressListener",
  "nsISupportsWeakReference",
]);

// Handle window-modal prompts that we want to display with the same style as
// tab-modal prompts.
var gDialogBox = {
  _dialog: null,
  _nextOpenJumpsQueue: false,
  _queued: [],

  // Used to wait for a `close` event from the HTML
  // dialog. The  event is fired asynchronously, which means
  // that if we open another dialog immediately after the
  // previous one, we might be confused into thinking a
  // `close` event for the old dialog is for the new one.
  // As they have the same event target, we have no way of
  // distinguishing them. So we wait for the `close` event
  // to have happened before allowing another dialog to open.
  _didCloseHTMLDialog: null,
  // Whether we managed to open the dialog we tried to open.
  // Used to avoid waiting for the above callback in case
  // of an error opening the dialog.
  _didOpenHTMLDialog: false,

  get dialog() {
    return this._dialog;
  },

  get isOpen() {
    return !!this._dialog;
  },

  replaceDialogIfOpen() {
    this._dialog?.close();
    this._nextOpenJumpsQueue = true;
  },

  async open(uri, args) {
    // If we need to queue, some callers indicate they should go first.
    const queueMethod = this._nextOpenJumpsQueue ? "unshift" : "push";
    this._nextOpenJumpsQueue = false;

    // If we already have a dialog opened and are trying to open another,
    // queue the next one to be opened later.
    if (this.isOpen) {
      return new Promise((resolve, reject) => {
        this._queued[queueMethod]({ resolve, reject, uri, args });
      });
    }

    // We're not open. If we're in a modal state though, we can't
    // show the dialog effectively. To avoid hanging by deadlock,
    // just return immediately for sync prompts:
    if (window.windowUtils.isInModalState() && !args.getProperty("async")) {
      throw Components.Exception(
        "Prompt could not be shown.",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }

    // Indicate if we should wait for the dialog to close.
    this._didOpenHTMLDialog = false;
    let haveClosedPromise = new Promise(resolve => {
      this._didCloseHTMLDialog = resolve;
    });

    // Bring the window to the front in case we're minimized or occluded:
    window.focus();

    try {
      // Prevent URL bar from showing on top of modal
      gURLBar.incrementBreakoutBlockerCount();
    } catch (ex) {
      console.error(ex);
    }

    try {
      await this._open(uri, args);
    } catch (ex) {
      console.error(ex);
    } finally {
      let dialog = document.getElementById("window-modal-dialog");
      if (dialog.open) {
        dialog.close();
      }
      // If the dialog was opened successfully, then we can wait for it
      // to close before trying to open any others.
      if (this._didOpenHTMLDialog) {
        await haveClosedPromise;
      }
      dialog.style.visibility = "hidden";
      dialog.style.height = "0";
      dialog.style.width = "0";
      document.documentElement.removeAttribute("window-modal-open");
      dialog.removeEventListener("dialogopen", this);
      dialog.removeEventListener("close", this);
      this._updateMenuAndCommandState(true /* to enable */);
      this._dialog = null;
      UpdatePopupNotificationsVisibility();
      // Restores URL bar breakout if needed
      gURLBar.decrementBreakoutBlockerCount();
    }
    if (this._queued.length) {
      setTimeout(() => this._openNextDialog(), 0);
    }
    return args;
  },

  _openNextDialog() {
    if (!this.isOpen) {
      let { resolve, reject, uri, args } = this._queued.shift();
      this.open(uri, args).then(resolve, reject);
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "dialogopen":
        this._dialog.focus(true);
        break;
      case "close":
        this._didCloseHTMLDialog();
        this._dialog.close();
        break;
    }
  },

  _open(uri, args) {
    // Get this offset before we touch style below, as touching style seems
    // to reset the cached layout bounds.
    let offset = window.windowUtils.getBoundsWithoutFlushing(
      gBrowser.selectedBrowser
    ).top;
    let parentElement = document.getElementById("window-modal-dialog");
    parentElement.style.setProperty("--chrome-offset", offset + "px");
    parentElement.style.removeProperty("visibility");
    parentElement.style.removeProperty("width");
    parentElement.style.removeProperty("height");
    document.documentElement.setAttribute("window-modal-open", true);
    // Call this first so the contents show up and get layout, which is
    // required for SubDialog to work.
    parentElement.showModal();
    this._didOpenHTMLDialog = true;

    // Disable menus and shortcuts.
    this._updateMenuAndCommandState(false /* to disable */);

    // Now actually set up the dialog contents:
    let template = document.getElementById("window-modal-dialog-template")
      .content.firstElementChild;
    parentElement.addEventListener("dialogopen", this);
    parentElement.addEventListener("close", this);
    this._dialog = new SubDialog({
      template,
      parentElement,
      id: "window-modal-dialog-subdialog",
      options: {
        consumeOutsideClicks: false,
      },
    });
    let closedPromise = new Promise(resolve => {
      this._closedCallback = function () {
        PromptUtils.fireDialogEvent(window, "DOMModalDialogClosed");
        resolve();
      };
    });
    this._dialog.open(
      uri,
      {
        features: "resizable=no",
        modalType: Ci.nsIPrompt.MODAL_TYPE_INTERNAL_WINDOW,
        closedCallback: () => {
          this._closedCallback();
        },
      },
      args
    );
    UpdatePopupNotificationsVisibility();
    return closedPromise;
  },

  _nonUpdatableElements: new Set([
    // Make an exception for debugging tools, for developer ease of use.
    "key_browserConsole",
    "key_browserToolbox",

    // Don't touch the editing keys/commands which we might want inside the dialog.
    "key_undo",
    "key_redo",

    "key_cut",
    "key_copy",
    "key_paste",
    "key_delete",
    "key_selectAll",
  ]),

  _updateMenuAndCommandState(shouldBeEnabled) {
    let editorCommands = document.getElementById("editMenuCommands");
    // For the following items, set or clear disabled state:
    // - toplevel menubar items (will affect inner items on macOS)
    // - command elements
    // - key elements not connected to command elements.
    for (let element of document.querySelectorAll(
      "menubar > menu, command, key:not([command])"
    )) {
      if (
        editorCommands?.contains(element) ||
        (element.id && this._nonUpdatableElements.has(element.id))
      ) {
        continue;
      }
      if (element.nodeName == "key" && element.command) {
        continue;
      }
      if (!shouldBeEnabled) {
        if (element.getAttribute("disabled") != "true") {
          element.setAttribute("disabled", true);
        } else {
          element.setAttribute("wasdisabled", true);
        }
      } else if (element.getAttribute("wasdisabled") != "true") {
        element.removeAttribute("disabled");
      } else {
        element.removeAttribute("wasdisabled");
      }
    }
  },
};

// browser.js loads in the library window, too, but we can only show prompts
// in the main browser window:
if (window.location.href != AppConstants.BROWSER_CHROME_URL) {
  gDialogBox = null;
}

var ConfirmationHint = {
  _timerID: null,

  /**
   * Shows a transient, non-interactive confirmation hint anchored to an
   * element, usually used in response to a user action to reaffirm that it was
   * successful and potentially provide extra context. Examples for such hints:
   * - "Saved to bookmarks" after bookmarking a page
   * - "Sent!" after sending a tab to another device
   * - "Queued (offline)" when attempting to send a tab to another device
   *   while offline
   *
   * @param  anchor (DOM node, required)
   *         The anchor for the panel.
   * @param  messageId (string, required)
   *         For getting the message string from confirmationHints.ftl
   * @param  options (object, optional)
   *         An object with the following optional properties:
   *         - event (DOM event): The event that triggered the feedback
   *         - descriptionId (string): message ID of the description text
   *         - position (string): position of the panel relative to the anchor.
   *         - l10nArgs (object): l10n arguments for the messageId.
   */
  show(anchor, messageId, options = {}) {
    this._reset();

    MozXULElement.insertFTLIfNeeded("toolkit/branding/brandings.ftl");
    MozXULElement.insertFTLIfNeeded("browser/confirmationHints.ftl");
    document.l10n.setAttributes(this._message, messageId, options.l10nArgs);
    if (options.descriptionId) {
      document.l10n.setAttributes(this._description, options.descriptionId);
      this._description.hidden = false;
      this._panel.classList.add("with-description");
    } else {
      this._description.hidden = true;
      this._panel.classList.remove("with-description");
    }

    this._panel.setAttribute("data-message-id", messageId);

    // The timeout value used here allows the panel to stay open for
    // 3s after the text transition (duration=120ms) has finished.
    // If there is a description, we show for 6s after the text transition.
    const DURATION = options.showDescription ? 6000 : 3000;
    this._panel.addEventListener(
      "popupshown",
      () => {
        this._animationBox.setAttribute("animate", "true");
        this._timerID = setTimeout(() => {
          this._panel.hidePopup(true);
        }, DURATION + 120);
      },
      { once: true }
    );

    this._panel.addEventListener(
      "popuphidden",
      () => {
        // reset the timerId in case our timeout wasn't the cause of the popup being hidden
        this._reset();
      },
      { once: true }
    );

    this._panel.openPopup(anchor, {
      position: options.position ?? "bottomleft topleft",
      triggerEvent: options.event,
    });
  },

  _reset() {
    if (this._timerID) {
      clearTimeout(this._timerID);
      this._timerID = null;
    }
    if (this.__panel) {
      this._animationBox.removeAttribute("animate");
      this._panel.removeAttribute("data-message-id");
    }
  },

  get _panel() {
    this._ensurePanel();
    return this.__panel;
  },

  get _animationBox() {
    this._ensurePanel();
    delete this._animationBox;
    return (this._animationBox = document.getElementById(
      "confirmation-hint-checkmark-animation-container"
    ));
  },

  get _message() {
    this._ensurePanel();
    delete this._message;
    return (this._message = document.getElementById(
      "confirmation-hint-message"
    ));
  },

  get _description() {
    this._ensurePanel();
    delete this._description;
    return (this._description = document.getElementById(
      "confirmation-hint-description"
    ));
  },

  _ensurePanel() {
    if (!this.__panel) {
      let wrapper = document.getElementById("confirmation-hint-wrapper");
      wrapper.replaceWith(wrapper.content);
      this.__panel = document.getElementById("confirmation-hint");
    }
  },
};

var FirefoxViewHandler = {
  tab: null,
  BUTTON_ID: "firefox-view-button",
  get button() {
    return document.getElementById(this.BUTTON_ID);
  },
  init() {
    CustomizableUI.addListener(this);

    ChromeUtils.defineESModuleGetters(this, {
      SyncedTabs: "resource://services-sync/SyncedTabs.sys.mjs",
    });
  },
  uninit() {
    CustomizableUI.removeListener(this);
  },
  onWidgetRemoved(aWidgetId) {
    if (aWidgetId == this.BUTTON_ID && this.tab) {
      gBrowser.removeTab(this.tab);
    }
  },
  onWidgetAdded(aWidgetId) {
    if (aWidgetId === this.BUTTON_ID) {
      this.button.removeAttribute("open");
    }
  },
  openTab(section) {
    if (!CustomizableUI.getPlacementOfWidget(this.BUTTON_ID)) {
      CustomizableUI.addWidgetToArea(
        this.BUTTON_ID,
        CustomizableUI.AREA_TABSTRIP,
        CustomizableUI.getPlacementOfWidget("tabbrowser-tabs").position
      );
    }
    let viewURL = "about:firefoxview";
    if (section) {
      viewURL = `${viewURL}#${section}`;
    }
    // Need to account for navigation to Firefox View pages
    if (
      this.tab &&
      this.tab.linkedBrowser.currentURI.spec.split("#")[0] != viewURL
    ) {
      gBrowser.removeTab(this.tab);
      this.tab = null;
    }
    if (!this.tab) {
      this.tab = gBrowser.addTrustedTab(viewURL);
      this.tab.addEventListener("TabClose", this, { once: true });
      gBrowser.tabContainer.addEventListener("TabSelect", this);
      window.addEventListener("activate", this);
      gBrowser.hideTab(this.tab);
      this.button.setAttribute("aria-controls", this.tab.linkedPanel);
    }
    // we put this here to avoid a race condition that would occur
    // if this was called in response to "TabSelect"
    this._closeDeviceConnectedTab();
    gBrowser.selectedTab = this.tab;
  },
  openToolbarMouseEvent(event, section) {
    if (event?.type == "mousedown" && event?.button != 0) {
      return;
    }
    this.openTab(section);
  },
  handleEvent(e) {
    switch (e.type) {
      case "TabSelect": {
        const selected = e.target == this.tab;
        this.button?.toggleAttribute("open", selected);
        this.button?.setAttribute("aria-pressed", selected);
        this._recordViewIfTabSelected();
        this._onTabForegrounded();
        // If Fx View is opened, add temporary style to make first available tab focusable
        // When Fx View is closed, remove temporary -moz-user-focus style from first available tab
        gBrowser.visibleTabs[0].style.MozUserFocus =
          e.target == this.tab ? "normal" : "";
        break;
      }
      case "TabClose":
        this.tab = null;
        gBrowser.tabContainer.removeEventListener("TabSelect", this);
        this.button?.removeAttribute("aria-controls");
        break;
      case "activate":
        this._onTabForegrounded();
        break;
    }
  },
  _closeDeviceConnectedTab() {
    if (!TabsSetupFlowManager.didFxaTabOpen) {
      return;
    }
    // close the tab left behind after a user pairs a device and
    // is redirected back to the Firefox View tab
    const fxaRoot = Services.prefs.getCharPref(
      "identity.fxaccounts.remote.root"
    );
    const fxDeviceConnectedTab = gBrowser.tabs.find(tab =>
      tab.linkedBrowser.currentURI.displaySpec.startsWith(
        `${fxaRoot}pair/auth/complete`
      )
    );

    if (!fxDeviceConnectedTab) {
      return;
    }

    if (gBrowser.tabs.length <= 2) {
      // if its the only tab besides the Firefox View tab,
      // open a new tab first so the browser doesn't close
      gBrowser.addTrustedTab("about:newtab");
    }
    gBrowser.removeTab(fxDeviceConnectedTab);
    TabsSetupFlowManager.didFxaTabOpen = false;
  },
  _onTabForegrounded() {
    if (this.tab?.selected) {
      this.SyncedTabs.syncTabs();
    }
  },
  _recordViewIfTabSelected() {
    if (this.tab?.selected) {
      const PREF_NAME = "browser.firefox-view.view-count";
      const MAX_VIEW_COUNT = 10;
      let viewCount = Services.prefs.getIntPref(PREF_NAME, 0);

      // Record telemetry
      Glean.firefoxviewNext.tabSelectedToolbarbutton.record();

      if (viewCount < MAX_VIEW_COUNT) {
        Services.prefs.setIntPref(PREF_NAME, viewCount + 1);
      }
    }
  },
};
