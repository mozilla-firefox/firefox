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
  },

  AboutPocket: {
    parent: {
      esModuleURI: "resource:///actors/AboutPocketParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutPocketChild.sys.mjs",

      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    remoteTypes: ["privilegedabout"],
    matches: [
      "about:pocket-saved*",
      "about:pocket-signup*",
      "about:pocket-home*",
      "about:pocket-style-guide*",
    ],
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
  },

  AboutWelcomeShopping: {
    parent: {
      esModuleURI: "resource:///actors/AboutWelcomeParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/AboutWelcomeChild.sys.mjs",
      events: {
        Update: {},
      },
    },
    matches: ["about:shoppingsidebar"],
    remoteTypes: ["privilegedabout"],
    messageManagerGroups: ["shopping-sidebar", "browsers", "review-checker"],
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

  BackupUI: {
    parent: {
      esModuleURI: "resource:///actors/BackupUIParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/BackupUIChild.sys.mjs",
      events: {
        "BackupUI:InitWidget": { wantUntrusted: true },
        "BackupUI:EnableScheduledBackups": { wantUntrusted: true },
        "BackupUI:DisableScheduledBackups": { wantUntrusted: true },
        "BackupUI:ShowFilepicker": { wantUntrusted: true },
        "BackupUI:GetBackupFileInfo": { wantUntrusted: true },
        "BackupUI:RestoreFromBackupFile": { wantUntrusted: true },
        "BackupUI:RestoreFromBackupChooseFile": { wantUntrusted: true },
        "BackupUI:EnableEncryption": { wantUntrusted: true },
        "BackupUI:DisableEncryption": { wantUntrusted: true },
        "BackupUI:RerunEncryption": { wantUntrusted: true },
        "BackupUI:ShowBackupLocation": { wantUntrusted: true },
        "BackupUI:EditBackupLocation": { wantUntrusted: true },
      },
    },
    matches: ["about:preferences*", "about:settings*"],
    enablePreference: "browser.backup.preferences.ui.enabled",
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
  },

  BrowserTab: {
    child: {
      esModuleURI: "resource:///actors/BrowserTabChild.sys.mjs",
    },

    messageManagerGroups: ["browsers"],
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
      },
    },

    messageManagerGroups: ["browsers"],
    allFrames: true,
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

      // Register the actor if we have a provider set and not yet registered
      const maybeRegister = () => {
        if (Services.prefs.getCharPref("browser.ml.chat.provider", "")) {
          if (!isRegistered) {
            register();
            isRegistered = true;
          }
        } else if (isRegistered) {
          unregister();
          isRegistered = false;
        }
      };

      Services.prefs.addObserver("browser.ml.chat.provider", maybeRegister);
      maybeRegister();
    },
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
      "about:firefoxview",
      "about:editprofile",
      "about:deleteprofile",
      "about:newprofile",
    ],
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
  },

  PageInfo: {
    child: {
      esModuleURI: "resource:///actors/PageInfoChild.sys.mjs",
    },

    allFrames: true,
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
  },

  Pdfjs: {
    parent: {
      esModuleURI: "resource://pdf.js/PdfjsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://pdf.js/PdfjsChild.sys.mjs",
    },
    allFrames: true,
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

  Prompt: {
    parent: {
      esModuleURI: "resource:///actors/PromptParent.sys.mjs",
    },
    includeChrome: true,
    allFrames: true,
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
  },

  ReviewChecker: {
    parent: {
      esModuleURI: "resource:///actors/ReviewCheckerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ReviewCheckerChild.sys.mjs",
      events: {
        ContentReady: { wantUntrusted: true },
        PolledRequestMade: { wantUntrusted: true },
        // This is added so the actor instantiates immediately and makes
        // methods available to the page js on load.
        DOMDocElementInserted: {},
        ReportProductAvailable: { wantUntrusted: true },
        AdClicked: { wantUntrusted: true },
        AdImpression: { wantUntrusted: true },
        DisableShopping: { wantUntrusted: true },
        CloseShoppingSidebar: { wantUntrusted: true },
        MoveSidebarToLeft: { wantUntrusted: true },
        MoveSidebarToRight: { wantUntrusted: true },
        ShowSidebarSettings: { wantUntrusted: true },
      },
    },
    matches: ["about:shoppingsidebar"],
    remoteTypes: ["privilegedabout"],
    messageManagerGroups: ["review-checker", "browsers"],
    enablePreference: "browser.shopping.experience2023.integratedSidebar",
  },

  ScreenshotsComponent: {
    parent: {
      esModuleURI: "resource:///modules/ScreenshotsUtils.sys.mjs",
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
  },

  ScreenshotsHelper: {
    parent: {
      esModuleURI: "resource:///modules/ScreenshotsUtils.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///modules/ScreenshotsHelperChild.sys.mjs",
    },
    allFrames: true,
    enablePreference: "screenshots.browser.component.enabled",
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
  },

  ShoppingSidebar: {
    parent: {
      esModuleURI: "resource:///actors/ShoppingSidebarParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/ShoppingSidebarChild.sys.mjs",
      events: {
        ContentReady: { wantUntrusted: true },
        PolledRequestMade: { wantUntrusted: true },
        // This is added so the actor instantiates immediately and makes
        // methods available to the page js on load.
        DOMDocElementInserted: {},
        ReportProductAvailable: { wantUntrusted: true },
        AdClicked: { wantUntrusted: true },
        AdImpression: { wantUntrusted: true },
        DisableShopping: { wantUntrusted: true },
      },
    },
    matches: ["about:shoppingsidebar"],
    remoteTypes: ["privilegedabout"],
    messageManagerGroups: ["shopping-sidebar", "browsers"],
    enablePreference: "browser.shopping.experience2023.enabled",
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
    matches: ["about:asrouter*", "about:welcome*", "about:privatebrowsing*"],
    remoteTypes: ["privilegedabout"],
  },

  SwitchDocumentDirection: {
    child: {
      esModuleURI: "resource:///actors/SwitchDocumentDirectionChild.sys.mjs",
    },

    allFrames: true,
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
  },

  WebRTC: {
    parent: {
      esModuleURI: "resource:///actors/WebRTCParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/WebRTCChild.sys.mjs",
    },

    allFrames: true,
  },
};

export let DesktopActorRegistry = {
  init() {
    ActorManagerParent.addJSProcessActors(JSPROCESSACTORS);
    ActorManagerParent.addJSWindowActors(JSWINDOWACTORS);
  },
};
