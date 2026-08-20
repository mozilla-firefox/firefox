/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ActorManagerParent } from "resource://gre/modules/ActorManagerParent.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
});

/**
 * Fission-compatible JSProcess implementations.
 * Each actor options object takes the form of a ProcessActorOptions dictionary.
 * Detailed documentation of these options is in dom/docs/ipc/jsactors.rst,
 * available at https://firefox-source-docs.mozilla.org/dom/ipc/jsactors.html
 */
let JSPROCESSACTORS = {
  // Miscellaneous stuff that needs to be initialized per process.
  BrowserProcess: {
    child: {
      esModuleURI: "resource:///actors/BrowserProcessChild.sys.mjs",
      observers: [
        // WebRTC related notifications. They are here to avoid loading WebRTC
        // components when not needed.
        "getUserMedia:request",
        "recording-device-stopped",
        "PeerConnection:request",
        "recording-device-events",
        "recording-window-ended",
      ],
    },
    safeForUntrustedWebProcess: true,
  },

  MozCachedOHTTP: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/mozcachedohttp/actors/MozCachedOHTTPParent.sys.mjs",
    },
    includeParent: true,
    remoteTypes: ["parent", "privilegedabout"],
  },

  RefreshBlockerObserver: {
    child: {
      esModuleURI: "resource:///actors/RefreshBlockerChild.sys.mjs",
      observers: [
        "webnavigation-create",
        "chrome-webnavigation-create",
        "webnavigation-destroy",
        "chrome-webnavigation-destroy",
      ],
    },

    enablePreference: "accessibility.blockautorefresh",
    onPreferenceChanged: isEnabled => {
      lazy.BrowserWindowTracker.orderedWindows.forEach(win => {
        for (let browser of win.gBrowser.browsers) {
          try {
            browser.sendMessageToActor(
              "PreferenceChanged",
              { isEnabled },
              "RefreshBlocker",
              "all"
            );
          } catch (ex) {}
        }
      });
    },
    safeForUntrustedWebProcess: true,
  },
};

/**
 * Fission-compatible JSWindowActor implementations.
 * Detailed documentation of these options is in dom/docs/ipc/jsactors.rst,
 * available at https://firefox-source-docs.mozilla.org/dom/ipc/jsactors.html
 */
let JSWINDOWACTORS = {
  Megalist: {
    parent: {
      esModuleURI: "resource://gre/actors/MegalistParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/MegalistChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
      },
    },
    includeChrome: true,
    matches: ["chrome://global/content/megalist/megalist.html"],
    allFrames: true,
    enablePreference: "browser.contextual-password-manager.enabled",
    remoteTypes: ["parent"],
  },

  AboutLogins: {
    parent: {
      esModuleURI: "resource:///actors/AboutLoginsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutLoginsChild.sys.mjs",
      events: {
        AboutLoginsCopyLoginDetail: { wantUntrusted: true },
        AboutLoginsCreateLogin: { wantUntrusted: true },
        AboutLoginsDeleteLogin: { wantUntrusted: true },
        AboutLoginsDismissBreachAlert: { wantUntrusted: true },
        AboutLoginsImportFromBrowser: { wantUntrusted: true },
        AboutLoginsImportFromFile: { wantUntrusted: true },
        AboutLoginsImportReportInit: { wantUntrusted: true },
        AboutLoginsImportReportReady: { wantUntrusted: true },
        AboutLoginsInit: { wantUntrusted: true },
        AboutLoginsGetHelp: { wantUntrusted: true },
        AboutLoginsOpenPreferences: { wantUntrusted: true },
        AboutLoginsOpenSite: { wantUntrusted: true },
        AboutLoginsRecordTelemetryEvent: { wantUntrusted: true },
        AboutLoginsRemoveAllLogins: { wantUntrusted: true },
        AboutLoginsSortChanged: { wantUntrusted: true },
        AboutLoginsSyncEnable: { wantUntrusted: true },
        AboutLoginsUpdateLogin: { wantUntrusted: true },
        AboutLoginsExportPasswords: { wantUntrusted: true },
      },
    },
    matches: ["about:logins", "about:logins?*", "about:loginsimportreport"],
    allFrames: true,
    remoteTypes: ["privilegedabout"],
  },

  AboutMessagePreview: {
    parent: {
      esModuleURI: "resource:///actors/AboutMessagePreviewParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutMessagePreviewChild.sys.mjs",
      events: {
        DOMDocElementInserted: { capture: true },
      },
    },
    matches: ["about:messagepreview", "about:messagepreview?*"],
    remoteTypes: ["privilegedabout"],
    enablePreference:
      "browser.newtabpage.activity-stream.asrouter.devtoolsEnabled",
  },

  AboutPrivateBrowsing: {
    parent: {
      esModuleURI: "resource:///actors/AboutPrivateBrowsingParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutPrivateBrowsingChild.sys.mjs",

      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    matches: ["about:privatebrowsing*"],
    remoteTypes: ["privilegedabout"],
  },

  AboutProtections: {
    parent: {
      esModuleURI: "resource:///actors/AboutProtectionsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutProtectionsChild.sys.mjs",

      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    matches: ["about:protections", "about:protections?*"],
    remoteTypes: ["privilegedabout"],
  },

  AboutReader: {
    parent: {
      esModuleURI: "resource:///actors/AboutReaderParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutReaderChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        pageshow: { mozSystemGroup: true },
        // Don't try to create the actor if only the pagehide event fires.
        // This can happen with the initial about:blank documents.
        pagehide: { mozSystemGroup: true, createActor: false },
      },
    },
    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  },

  AboutTabCrashed: {
    parent: {
      esModuleURI: "resource:///actors/AboutTabCrashedParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutTabCrashedChild.sys.mjs",
      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    matches: ["about:tabcrashed*"],
    remoteTypes: ["parent"],
  },

  AboutWelcome: {
    parent: {
      esModuleURI: "resource:///actors/AboutWelcomeParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutWelcomeChild.sys.mjs",
      events: {
        // This is added so the actor instantiates immediately and makes
        // methods available to the page js on load.
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:welcome"],
    remoteTypes: ["privilegedabout"],

    // See Bug 1618306
    // Remove this preference check when we turn on separate about:welcome for all users.
    enablePreference: "browser.aboutwelcome.enabled",
  },

  AIChatContent: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/AIChatContentParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/AIChatContentChild.sys.mjs",
      events: {
        "AIChatContent:DispatchFollowUp": { wantUntrusted: true },
        "AIChatContent:Ready": { wantUntrusted: true },
        "AIChatContent:DispatchAction": { wantUntrusted: true },
        "AIChatContent:OpenLink": { wantUntrusted: true },
        "AIChatContent:DispatchNewChat": { wantUntrusted: true },
        "AIChatContent:AccountSignIn": { wantUntrusted: true },
        "AIChatContent:ToolUIUpdate": { wantUntrusted: true },
        "AIChatContent:RequestAssets": { wantUntrusted: true },
        "AIChatContent:HistoryGridRender": { wantUntrusted: true },
        "AIChatContent:HistoryGridItemClick": { wantUntrusted: true },
        "AIChatContent:ClientError": { wantUntrusted: true },
      },
    },
    allFrames: true,
    matches: ["about:aichatcontent"],
    remoteTypes: ["privilegedabout"],
    enablePreference: "browser.smartwindow.enabled",
  },

  AISmartBar: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/AISmartBarParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/AISmartBarChild.sys.mjs",
    },
    matches: ["chrome://browser/content/aiwindow/aiWindow.html"],
    includeChrome: true,
    allFrames: true,
    enablePreference: "browser.smartwindow.enabled",
    remoteTypes: ["parent"],
  },

  SmartWindowTasks: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/SmartWindowTasksParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/SmartWindowTasksChild.sys.mjs",
      events: {
        "SmartWindowTasks:RequestListMonitors": { wantUntrusted: true },
        "SmartWindowTasks:RequestCreateMonitor": { wantUntrusted: true },
        "SmartWindowTasks:RequestDeleteMonitor": { wantUntrusted: true },
        "SmartWindowTasks:RequestUpdateMonitor": { wantUntrusted: true },
        "SmartWindowTasks:RequestRunMonitor": { wantUntrusted: true },
        "SmartWindowTasks:RequestPauseMonitor": { wantUntrusted: true },
        "SmartWindowTasks:RequestConstants": { wantUntrusted: true },
      },
    },
    allFrames: true,
    // TODO: Remove chrome:// URL and "parent" remoteType before landing
    // These are only for development while about:smartwindowtasks registration lands
    matches: ["about:smartwindowtasks"],
    includeChrome: true,
    remoteTypes: ["privilegedabout"],
    enablePreference: "browser.smartwindow.enabled",
  },

  BackupUI: {
    parent: {
      esModuleURI: "resource:///actors/BackupUIParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/BackupUIChild.sys.mjs",
      events: {
        "BackupUI:InitWidget": { wantUntrusted: true },
        "BackupUI:TriggerCreateBackup": { wantUntrusted: true },
        "BackupUI:EnableScheduledBackups": { wantUntrusted: true },
        "BackupUI:DisableScheduledBackups": { wantUntrusted: true },
        "BackupUI:ShowFilepicker": { wantUntrusted: true },
        "BackupUI:GetBackupFileInfo": { wantUntrusted: true },
        "BackupUI:RestoreFromBackupFile": { wantUntrusted: true },
        "BackupUI:RestoreFromBackupChooseFile": { wantUntrusted: true },
        "BackupUI:EnableEncryption": { wantUntrusted: true },
        "BackupUI:DisableEncryption": { wantUntrusted: true },
        "BackupUI:ShowBackupLocation": { wantUntrusted: true },
        "BackupUI:SetEmbeddedComponentPersistentData": { wantUntrusted: true },
        "BackupUI:FlushEmbeddedComponentPersistentData": {
          wantUntrusted: true,
        },
        "BackupUI:ErrorBarDismissed": { wantUntrusted: true },
        "BackupUI:FindBackupsInWellKnownLocations": { wantUntrusted: true },
        "BackupUI:ProbeDefaultBackupDir": { wantUntrusted: true },
        "BackupUI:PrepareRestoreDialog": { wantUntrusted: true },
      },
    },
    includeChrome: true,
    allFrames: true,
    matches: [
      "about:preferences*",
      "about:settings*",
      "about:welcome*",
      "chrome://browser/content/spotlight.html",
      "about:newtab*",
      "about:home*",
    ],
    remoteTypes: ["parent", "privilegedabout"],
  },

  BlockedSite: {
    parent: {
      esModuleURI: "resource:///actors/BlockedSiteParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/BlockedSiteChild.sys.mjs",
      events: {
        AboutBlockedLoaded: { wantUntrusted: true },
        click: {},
      },
    },
    matches: ["about:blocked?*"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  BrowserTab: {
    child: {
      esModuleURI: "resource:///actors/BrowserTabChild.sys.mjs",
    },

    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  },

  CanonicalURL: {
    parent: {
      esModuleURI: "resource:///actors/CanonicalURLParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/CanonicalURLChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        pageshow: {},
        // `popstate` does not bubble, so it needs to be captured.
        popstate: { capture: true },
      },
    },
    matches: ["http://*/*", "https://*/*"],
    messageManagerGroups: ["browsers"],
    enablePreference: "browser.tabs.notes.enabled",
    onPreferenceChanged: isEnabled => {
      if (isEnabled) {
        Services.obs.notifyObservers(undefined, "CanonicalURL:ActorRegistered");
      } else {
        Services.obs.notifyObservers(
          undefined,
          "CanonicalURL:ActorUnregistered"
        );
      }
    },
    safeForUntrustedWebProcess: true,
  },

  ClickHandler: {
    parent: {
      esModuleURI: "resource:///actors/ClickHandlerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ClickHandlerChild.sys.mjs",
      events: {
        chromelinkclick: { capture: true, mozSystemGroup: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  /* Note: this uses the same JSMs as ClickHandler, but because it
   * relies on "normal" click events anywhere on the page (not just
   * links) and is expensive, and only does something for the
   * small group of people who have the feature enabled, it is its
   * own actor which is only registered if the pref is enabled.
   */
  MiddleMousePasteHandler: {
    parent: {
      esModuleURI: "resource:///actors/ClickHandlerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ClickHandlerChild.sys.mjs",
      events: {
        auxclick: { capture: true, mozSystemGroup: true },
      },
    },
    enablePreference: "middlemouse.contentLoadURL",

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  ContentSearch: {
    parent: {
      esModuleURI: "resource:///actors/ContentSearchParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ContentSearchChild.sys.mjs",
      events: {
        ContentSearchClient: { capture: true, wantUntrusted: true },
      },
    },
    matches: [
      "about:home",
      "about:welcome",
      "about:newtab",
      "about:privatebrowsing",
      "about:test-about-content-search-ui",
    ],
    remoteTypes: ["privilegedabout"],
  },

  ContextMenu: {
    parent: {
      esModuleURI: "resource:///actors/ContextMenuParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/ContextMenuChild.sys.mjs",
      events: {
        contextmenu: { mozSystemGroup: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  CustomKeys: {
    parent: {
      esModuleURI: "resource:///actors/CustomKeysParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/CustomKeysChild.sys.mjs",
      events: {
        DOMDocElementInserted: { wantUntrusted: true },
      },
    },
    matches: ["about:keyboard"],
    remoteTypes: ["privilegedabout"],
  },

  DecoderDoctor: {
    parent: {
      esModuleURI: "resource:///actors/DecoderDoctorParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/DecoderDoctorChild.sys.mjs",
      observers: ["decoder-doctor-notification"],
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  DOMFullscreen: {
    parent: {
      esModuleURI: "resource:///actors/DOMFullscreenParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/DOMFullscreenChild.sys.mjs",
      events: {
        "MozDOMFullscreen:Request": {},
        "MozDOMFullscreen:Entered": {},
        "MozDOMFullscreen:NewOrigin": {},
        "MozDOMFullscreen:Exit": {},
        "MozDOMFullscreen:Exited": {},
        "MozDOMFullscreen:WarnAboutKeyboardLock": {},
        "MozDOMFullscreen:UpdateKeyboardLock": {},
      },
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  EncryptedMedia: {
    parent: {
      esModuleURI: "resource:///actors/EncryptedMediaParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/EncryptedMediaChild.sys.mjs",
      observers: ["mediakeys-request"],
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  FormValidation: {
    parent: {
      esModuleURI: "resource:///actors/FormValidationParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/FormValidationChild.sys.mjs",
      events: {
        MozInvalidForm: {},
        // Listening to ‘pageshow’ event is only relevant if an invalid form
        // popup was open, so don't create the actor when fired.
        pageshow: { createActor: false },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  GenAI: {
    parent: {
      esModuleURI: "resource:///actors/GenAIParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/GenAIChild.sys.mjs",
      events: {
        mousedown: {},
        mouseup: {},
      },
    },
    allFrames: true,
    onAddActor(register, unregister) {
      let isRegistered = false;

      // Register the actor if an external chat provider is set, page summarization is enabled, or shortcuts are enabled
      const maybeRegister = () => {
        if (
          Services.prefs.getCharPref("browser.ml.chat.provider", "") ||
          Services.prefs.getBoolPref("browser.ml.chat.page") ||
          Services.prefs.getBoolPref("browser.ml.chat.shortcuts") ||
          Services.prefs.getBoolPref("browser.ml.chat.shortcuts.smartwindow")
        ) {
          if (!isRegistered) {
            register();
            isRegistered = true;
          }
        } else if (isRegistered) {
          unregister();
          isRegistered = false;
        }
      };

      Services.prefs.addObserver("browser.ml.chat.page", maybeRegister);
      Services.prefs.addObserver("browser.ml.chat.provider", maybeRegister);
      Services.prefs.addObserver("browser.ml.chat.shortcuts", maybeRegister);
      Services.prefs.addObserver(
        "browser.ml.chat.shortcuts.smartwindow",
        maybeRegister
      );
      maybeRegister();
    },
    safeForUntrustedWebProcess: true,
  },

  LightweightTheme: {
    child: {
      esModuleURI: "resource:///actors/LightweightThemeChild.sys.mjs",
      events: {
        pageshow: { mozSystemGroup: true },
        DOMContentLoaded: {},
      },
    },
    includeChrome: true,
    allFrames: true,
    matches: [
      "about:asrouter",
      "about:home",
      "about:newtab",
      "about:welcome",
      "chrome://browser/content/syncedtabs/sidebar.xhtml",
      "chrome://browser/content/places/historySidebar.xhtml",
      "chrome://browser/content/places/bookmarksSidebar.xhtml",
      "chrome://browser/content/sidebar/sidebar-bookmarks.html",
      "chrome://browser/content/sidebar/sidebar-history.html",
      "chrome://browser/content/sidebar/sidebar-customize.html",
      "chrome://browser/content/sidebar/sidebar-syncedtabs.html",
      "chrome://browser/content/sidebar/sidebar-opentabs.html",
      "chrome://browser/content/genai/chat.html",
      "about:firefoxview",
      "about:editprofile",
      "about:deleteprofile",
      "about:newprofile",
      "about:opentabs",
      "about:aichatcontent",
    ],
    remoteTypes: ["parent", "privilegedabout"],
  },

  LinkHandler: {
    parent: {
      esModuleURI: "resource:///actors/LinkHandlerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/LinkHandlerChild.sys.mjs",
      events: {
        DOMHeadElementParsed: {},
        DOMLinkAdded: {},
        DOMLinkChanged: {},
        pageshow: {},
        // The `pagehide` event is only used to clean up state which will not be
        // present if the actor hasn't been created.
        pagehide: { createActor: false },
      },
    },

    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  },

  LinkPreview: {
    parent: {
      esModuleURI: "resource:///actors/LinkPreviewParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/LinkPreviewChild.sys.mjs",
    },
    includeChrome: true,
    enablePreference: "browser.ml.linkPreview.enabled",
    safeForUntrustedWebProcess: true,
  },

  PageAssist: {
    parent: {
      esModuleURI: "resource:///actors/PageAssistParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/PageAssistChild.sys.mjs",
    },
    includeChrome: true,
    enablePreference: "browser.ml.pageAssist.enabled",
    safeForUntrustedWebProcess: true,
  },

  PageInfo: {
    child: {
      esModuleURI: "resource:///actors/PageInfoChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  PageInfoPreview: {
    child: {
      esModuleURI: "resource:///actors/PageInfoPreviewChild.sys.mjs",
    },
    safeForUntrustedWebProcess: true,
  },

  PageStyle: {
    parent: {
      esModuleURI: "resource:///actors/PageStyleParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/PageStyleChild.sys.mjs",
      events: {
        pageshow: { createActor: false },
      },
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Pdfjs: {
    parent: {
      esModuleURI: "resource://pdf.js/PdfjsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://pdf.js/PdfjsChild.sys.mjs",
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  // GMP crash reporting
  Plugin: {
    parent: {
      esModuleURI: "resource:///actors/PluginParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/PluginChild.sys.mjs",
      events: {
        PluginCrashed: { capture: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  PointerLock: {
    parent: {
      esModuleURI: "resource:///actors/PointerLockParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/PointerLockChild.sys.mjs",
      events: {
        "MozDOMPointerLock:Entered": {},
        "MozDOMPointerLock:Exited": {},
      },
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Profiles: {
    parent: {
      esModuleURI: "resource:///actors/ProfilesParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ProfilesChild.sys.mjs",
      events: {
        DOMDocElementInserted: { wantUntrusted: true },
      },
    },
    matches: ["about:editprofile", "about:deleteprofile", "about:newprofile"],
    remoteTypes: ["privilegedabout"],
  },

  ThemePicker: {
    parent: {
      esModuleURI: "resource:///actors/ThemePickerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ThemePickerChild.sys.mjs",
      events: {
        ThemePickerGetInitialState: { wantUntrusted: true },
        ThemePickerUpdateTheme: { wantUntrusted: true },
        ThemePickerUpdateAppearance: { wantUntrusted: true },
        ThemePickerUpdateNativeTheme: { wantUntrusted: true },
      },
    },
    matches: [
      "about:editprofile",
      "about:newprofile",
      "about:newtab",
      "about:home",
      "about:welcome",
    ],
    remoteTypes: ["privilegedabout"],
  },

  Prompt: {
    parent: {
      esModuleURI: "resource:///actors/PromptParent.sys.mjs",
    },
    includeChrome: true,
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  RefreshBlocker: {
    parent: {
      esModuleURI: "resource:///actors/RefreshBlockerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/RefreshBlockerChild.sys.mjs",
    },

    messageManagerGroups: ["browsers"],
    enablePreference: "accessibility.blockautorefresh",
    safeForUntrustedWebProcess: true,
  },

  ScreenshotsComponent: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ScreenshotsComponentChild.sys.mjs",
      events: {
        "Screenshots:Close": {},
        "Screenshots:Copy": {},
        "Screenshots:Download": {},
        "Screenshots:HidePanel": {},
        "Screenshots:OverlaySelection": {},
        "Screenshots:RecordEvent": {},
        "Screenshots:ShowPanel": {},
        "Screenshots:FocusPanel": {},
      },
    },
    enablePreference: "screenshots.browser.component.enabled",
    safeForUntrustedWebProcess: true,
  },

  ScreenshotsHelper: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///browser/components/screenshots/ScreenshotsHelperChild.sys.mjs",
    },
    allFrames: true,
    enablePreference: "screenshots.browser.component.enabled",
    safeForUntrustedWebProcess: true,
  },

  SearchSERPTelemetry: {
    parent: {
      esModuleURI: "resource:///actors/SearchSERPTelemetryParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/SearchSERPTelemetryChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        pageshow: { mozSystemGroup: true },
        // The 'pagehide' event is only used to clean up state, and should not
        // force actor creation.
        pagehide: { createActor: false },
        load: { mozSystemGroup: true, capture: true },
      },
    },
    matches: ["https://*/*"],
    safeForUntrustedWebProcess: true,
  },

  ShieldFrame: {
    parent: {
      esModuleURI: "resource://normandy-content/ShieldFrameParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://normandy-content/ShieldFrameChild.sys.mjs",
      events: {
        pageshow: {},
        pagehide: {},
        ShieldPageEvent: { wantUntrusted: true },
      },
    },
    matches: ["about:studies*"],
    safeForUntrustedWebProcess: true,
  },

  SmartFormFill: {
    parent: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/SmartFormFillParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///browser/components/aiwindow/ui/actors/SmartFormFillChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
      },
    },
    safeForUntrustedWebProcess: true,

    // SmartFormFill should only work on HTTPS urls
    matches: ["https://*/*"],
    messageManagerGroups: ["browsers"],

    onAddActor(register, unregister) {
      let isRegistered = false;

      const maybeRegister = () => {
        if (
          Services.prefs.getBoolPref("browser.smartwindow.enabled") &&
          Services.prefs.getBoolPref(
            "browser.smartwindow.smartformfill.enabled",
            false
          )
        ) {
          if (!isRegistered) {
            register();
            isRegistered = true;
          }
        } else if (isRegistered) {
          unregister();
          isRegistered = false;
        }
      };

      Services.prefs.addObserver("browser.smartwindow.enabled", maybeRegister);
      Services.prefs.addObserver(
        "browser.smartwindow.smartformfill.enabled",
        maybeRegister
      );
      maybeRegister();
    },
  },

  SpeechDispatcher: {
    parent: {
      esModuleURI: "resource:///actors/SpeechDispatcherParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource:///actors/SpeechDispatcherChild.sys.mjs",
      observers: ["chrome-synth-voices-error"],
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  ASRouter: {
    parent: {
      esModuleURI: "resource:///actors/ASRouterParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ASRouterChild.sys.mjs",
      events: {
        // This is added so the actor instantiates immediately and makes
        // methods available to the page js on load.
        DOMDocElementInserted: {},
      },
    },
    matches: [
      "about:asrouter*",
      "about:welcome*",
      "about:privatebrowsing*",
      "about:newtab*",
      "about:home*",
    ],
    remoteTypes: ["privilegedabout"],
  },

  SwitchDocumentDirection: {
    child: {
      esModuleURI: "resource:///actors/SwitchDocumentDirectionChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  UITour: {
    parent: {
      esModuleURI: "moz-src:///browser/components/uitour/UITourParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///browser/components/uitour/UITourChild.sys.mjs",
      events: {
        mozUITour: { wantUntrusted: true },
      },
    },

    enablePreference: "browser.uitour.enabled",
    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  },

  Urlbar: {
    parent: {
      esModuleURI: "resource:///actors/UrlbarParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/UrlbarChild.sys.mjs",
      events: {
        // A content-realm `<moz-urlbar>` reads `window.UrlbarActorPort`
        // synchronously as it connects, and can't create the actor itself, so
        // the actor has to exist before page script runs.
        DOMDocElementInserted: {},
      },
    },
    includeChrome: true,
    matches: [
      "chrome://browser/content/browser.xhtml",
      "about:home",
      "about:newtab",
    ],
    // The actor must never load in a web content process: it exposes the
    // urlbar's full result set and navigation surface, and it deliberately
    // doesn't set `safeForUntrustedWebProcess`.
    remoteTypes: ["parent", "privilegedabout"],
  },

  WebRTC: {
    parent: {
      esModuleURI: "resource:///actors/WebRTCParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/WebRTCChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },
};

export let DesktopActorRegistry = {
  init() {
    ActorManagerParent.addJSProcessActors(JSPROCESSACTORS);
    ActorManagerParent.addJSWindowActors(JSWINDOWACTORS);
  },
};
