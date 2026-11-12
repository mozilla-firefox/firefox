/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module handles JavaScript-implemented JSWindowActors, registered through DOM IPC
 * infrastructure, and are fission-compatible.
 */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * Fission-compatible JSProcess implementations.
 * Each actor options object takes the form of a ProcessActorOptions dictionary.
 * Detailed documentation of these options is in dom/docs/ipc/jsactors.rst,
 * available at https://firefox-source-docs.mozilla.org/dom/ipc/jsactors.html
 */
let JSPROCESSACTORS = {
  AsyncPrefs: {
    parent: {
      esModuleURI: "resource://gre/modules/AsyncPrefs.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/AsyncPrefs.sys.mjs",
    },
    safeForUntrustedWebProcess: true,
  },

  // Runs the content-analysis DLP WebAssembly module in the privilegedabout
  // process. The module is compiled there under a non-system principal
  // because the parent process forbids wasm/eval regardless of principal.
  ContentAnalysisWasm: {
    remoteTypes: ["privilegedabout"],
    parent: {
      esModuleURI: "resource://gre/modules/ContentAnalysisWasmParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/ContentAnalysisWasmChild.sys.mjs",
    },
  },

  ContentPrefs: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/contentprefs/ContentPrefServiceParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/contentprefs/ContentPrefServiceChild.sys.mjs",
    },
    safeForUntrustedWebProcess: true,
  },

  ExtensionContent: {
    child: {
      esModuleURI: "resource://gre/modules/ExtensionContent.sys.mjs",
    },
    includeParent: true,
    safeForUntrustedWebProcess: true,
  },

  HPKEConfigManager: {
    remoteTypes: ["privilegedabout"],
    parent: {
      esModuleURI: "resource://gre/modules/HPKEConfigManager.sys.mjs",
    },
  },

  // A single process (shared with translations) that manages machine learning engines.
  MLEngine: {
    remoteTypes: ["inference"],
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/ml/actors/MLEngineChild.sys.mjs",
    },
    enablePreference: "browser.ml.enable",
  },

  ProcessConduits: {
    // "parent" remoteTypes is currently needed to support MV3 background service workers
    // also when extensions.webextensions.remote is set to false.
    remoteTypes: ["parent", "extension"],
    parent: {
      esModuleURI: "resource://gre/modules/ConduitsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/ConduitsChild.sys.mjs",
    },
    // This actor is only meant to be used when MV3 background service worker
    // implementation is enabled (which is currently only allowed in Nightly
    // and gated by this about:config preference).
    enablePreference: "extensions.backgroundServiceWorker.enabled",
  },

  // A single process (shared with MLEngine) that controls all of the translations.
  TranslationsEngine: {
    remoteTypes: ["inference"],
    parent: {
      esModuleURI: "resource://gre/actors/TranslationsEngineParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/TranslationsEngineChild.sys.mjs",
    },
    enablePreference: "browser.translations.enable",
  },
};

/**
 * Fission-compatible JSWindowActor implementations.
 * Each actor options object takes the form of a WindowActorOptions dictionary.
 * Detailed documentation of these options is in dom/docs/ipc/jsactors.rst,
 * available at https://firefox-source-docs.mozilla.org/dom/ipc/jsactors.html
 */
let JSWINDOWACTORS = {
  AboutCertViewer: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/certviewer/AboutCertViewerParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/certviewer/AboutCertViewerChild.sys.mjs",

      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    matches: ["about:certificate"],
    remoteTypes: ["privilegedabout"],
  },

  AboutHttpsOnlyError: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/actors/AboutHttpsOnlyErrorParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/AboutHttpsOnlyErrorChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:httpsonlyerror?*"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  AboutRestricted: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AboutRestrictedParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/AboutRestrictedChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:restricted?*"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  AudioPlayback: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AudioPlaybackParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/AudioPlaybackChild.sys.mjs",
      observers: ["audio-playback"],
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  AutoComplete: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs",
      // These two messages are also used, but are currently synchronous calls
      // through the per-process message manager.
      // "AutoComplete:GetSelectedIndex",
      // "AutoComplete:SelectBy"
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/AutoCompleteChild.sys.mjs",
      // On GeckoView the autocomplete popup is a delegated native prompt; we
      // listen for pagehide (which also fires for bfcache) to tear it down so
      // it can't outlive its document. Other platforms close the popup via
      // nsFormFillController, so the listener is GeckoView-only to avoid
      // instantiating the actor on every navigation elsewhere.
      ...(AppConstants.MOZ_GECKOVIEW && {
        events: {
          pagehide: { mozSystemGroup: true },
        },
      }),
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Autoplay: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AutoplayParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/AutoplayChild.sys.mjs",
      events: {
        GloballyAutoplayBlocked: {},
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  AutoScroll: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AutoScrollParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/AutoScrollChild.sys.mjs",
      events: {
        mousedown: { capture: true, mozSystemGroup: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  BackgroundThumbnails: {
    child: {
      esModuleURI:
        "moz-src:///toolkit/actors/BackgroundThumbnailsChild.sys.mjs",
      events: {
        DOMDocElementInserted: { capture: true },
      },
    },
    messageManagerGroups: ["thumbnails"],
    safeForUntrustedWebProcess: true,
  },

  BrowserElement: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/BrowserElementParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/BrowserElementChild.sys.mjs",
      events: {
        DOMWindowClose: {},
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Conduits: {
    parent: {
      esModuleURI: "resource://gre/modules/ConduitsParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/modules/ConduitsChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Controllers: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/ControllersParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/ControllersChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  CaptchaDetection: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/captchadetection/CaptchaDetectionParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/captchadetection/CaptchaDetectionChild.sys.mjs",
      events: {
        DOMContentLoaded: { capture: true },
        pageshow: {},
        pagehide: {},
      },
    },
    matches: [
      // Google reCAPTCHA v2
      "https://www.google.com/recaptcha/api2/*",
      "https://www.google.com/recaptcha/enterprise/*",
      // CF Turnstile
      "https://challenges.cloudflare.com/cdn-cgi/challenge-platform/*",
      // DataDome Captcha
      "https://geo.captcha-delivery.com/captcha/*",
      // hCaptcha
      "https://newassets.hcaptcha.com/captcha/v1/*",
      // Arkose Labs Captcha
      "https://client-api.arkoselabs.com/fc/assets/ec-game-core/game-core/*",
      // Mochitest
      ...(Cu.isInAutomation
        ? [
            "https://example.com/tests/toolkit/components/captchadetection/tests/mochitest/*",
            "https://example.org/tests/toolkit/components/captchadetection/tests/mochitest/*",
          ]
        : []),
    ],
    messageManagerGroups: ["browsers"],
    allFrames: true,
    enablePreference: "captchadetection.actor.enabled",
    safeForUntrustedWebProcess: true,
  },

  CaptchaDetectionCommunication: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/captchadetection/CaptchaDetectionParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/captchadetection/CaptchaDetectionCommunicationChild.sys.mjs",
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  ExtFind: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/ExtFindChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  FindBar: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/FindBarParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/FindBarChild.sys.mjs",
      events: {
        keypress: { mozSystemGroup: true },
      },
    },

    allFrames: true,
    messageManagerGroups: ["browsers", "test"],
    safeForUntrustedWebProcess: true,
  },

  // This is the actor that responds to requests from the find toolbar and
  // searches for matches and highlights them.
  Finder: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/FinderChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  FormHistory: {
    parent: {
      esModuleURI: "resource://gre/actors/FormHistoryParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/FormHistoryChild.sys.mjs",
      events: {
        DOMFormBeforeSubmit: {},
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  FormHandler: {
    parent: {
      esModuleURI: "resource://gre/actors/FormHandlerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/FormHandlerChild.sys.mjs",
      events: {
        DOMFormBeforeSubmit: { createActor: false },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  InlineSpellChecker: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/InlineSpellCheckerParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/InlineSpellCheckerChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  KeyPressEventModelChecker: {
    child: {
      esModuleURI:
        "moz-src:///toolkit/actors/KeyPressEventModelCheckerChild.sys.mjs",
      events: {
        CheckKeyPressEventModel: { capture: true, mozSystemGroup: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  LoginManager: {
    parent: {
      esModuleURI: "resource://gre/modules/LoginManagerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/LoginManagerChild.sys.mjs",
      events: {
        "form-submission-detected": { createActor: false },
        "before-form-submission": { createActor: false },
        DOMFormHasPassword: {},
        DOMPossibleUsernameInputAdded: {},
        DOMInputPasswordAdded: {},
      },
    },

    allFrames: true,
    messageManagerGroups: [
      "browsers",
      "webext-browsers",
      "chatbot-browser",
      "",
    ],
    safeForUntrustedWebProcess: true,
  },

  ManifestMessages: {
    child: {
      esModuleURI: "moz-src:///dom/ipc/ManifestMessagesChild.sys.mjs",
    },
    safeForUntrustedWebProcess: true,
  },

  NetError: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/NetErrorParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/NetErrorChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
        click: {},
      },
    },

    matches: ["about:certerror?*", "about:neterror?*"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  OpenSearchLoader: {
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/search/OpenSearchLoaderChild.sys.mjs",
    },
    matches: ["about:blank"],
    messageManagerGroups: ["opensearch"],
    safeForUntrustedWebProcess: true,
  },

  PageExtractor: {
    parent: {
      esModuleURI: "resource://gre/actors/PageExtractorParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/PageExtractorChild.sys.mjs",
    },
    matches: [
      "http://*/*",
      "https://*/*",
      "file:///*",
      "moz-extension://*",
      "data:text/html,*",
      "about:reader?*",
      "about:smartpage?*",
    ],
    messageManagerGroups: ["browsers", "headless-browsers"],
    safeForUntrustedWebProcess: true,
  },

  PopupAndRedirectBlocking: {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/actors/PopupAndRedirectBlockingParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "moz-src:///toolkit/actors/PopupAndRedirectBlockingChild.sys.mjs",
      events: {
        DOMPopupBlocked: { capture: true },
        DOMRedirectBlocked: { capture: true },
        // Only listen for the `pageshow` event after the actor has already been
        // created for some other reason.
        pageshow: { createActor: false },
      },
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Printing: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/PrintingParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/PrintingChild.sys.mjs",
      events: {
        PrintingError: { capture: true },
        printPreviewUpdate: { capture: true },
      },
    },
    safeForUntrustedWebProcess: true,
  },

  PrintingSelection: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/PrintingSelectionChild.sys.mjs",
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  ReportBrokenSite: {
    parent: {
      esModuleURI: "resource://gre/actors/ReportBrokenSiteParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/ReportBrokenSiteChild.sys.mjs",
    },
    matches: [
      "http://*/*",
      "https://*/*",
      "about:certerror?*",
      "about:neterror?*",
    ],
    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  TLSCertificateBinding: {
    child: {
      esModuleURI:
        "moz-src:///toolkit/actors/TLSCertificateBindingChild.sys.mjs",
    },

    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  },

  // This actor is available for all pages that one can
  // view the source of, however it won't be created until a
  // request to view the source is made via the message
  // 'ViewSource:LoadSource' or 'ViewSource:LoadSourceWithSelection'.
  ViewSource: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/ViewSourceChild.sys.mjs",
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  // This actor is for the view-source page itself.
  ViewSourcePage: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/ViewSourcePageParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/ViewSourcePageChild.sys.mjs",
      events: {
        pageshow: { capture: true },
        click: {},
      },
    },

    matches: ["view-source:*"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  Thumbnails: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/ThumbnailsChild.sys.mjs",
    },
    safeForUntrustedWebProcess: true,
  },

  // Determines if a page can be translated, and coordinates communication with the
  // translations engine.
  Translations: {
    parent: {
      esModuleURI: "resource://gre/actors/TranslationsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/TranslationsChild.sys.mjs",
    },
    matches: [
      "about:blank",
      "about:srcdoc",
      "file:///*",
      "http://*/*",
      "https://*/*",
      "moz-extension://*",
    ],
    messageManagerGroups: ["browsers"],
    allFrames: true,
    enablePreference: "browser.translations.enable",
    onPreferenceChanged(isEnabled) {
      const { TranslationsParent } = ChromeUtils.importESModule(
        "resource://gre/actors/TranslationsParent.sys.mjs"
      );
      TranslationsParent.onIsEnabledChanged(isEnabled);
    },
    safeForUntrustedWebProcess: true,
  },

  UAWidgets: {
    child: {
      esModuleURI: "moz-src:///toolkit/actors/UAWidgetsChild.sys.mjs",
      events: {
        UAWidgetSetupOrChange: {},
        UAWidgetTeardown: {},
      },
    },

    includeChrome: true,
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },

  UnselectedTabHover: {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/UnselectedTabHoverParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/UnselectedTabHoverChild.sys.mjs",
      events: {
        "UnselectedTabHover:Enable": {},
        "UnselectedTabHover:Disable": {},
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  },
};

/**
 * Note that turning on page data collection for snapshots currently disables
 * collection of generic page info for normal history entries. See bug 1740234.
 */
if (!Services.prefs.getBoolPref("browser.pagedata.enabled", false)) {
  JSWINDOWACTORS.ContentMeta = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/ContentMetaParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/ContentMetaChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        DOMMetaAdded: { createActor: false },
      },
    },

    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
  };
}

if (AppConstants.platform != "android") {
  // The stream converter sends this message from the scriptless fallback page,
  // so no child module is needed.
  JSWINDOWACTORS.PdfEmbedFallback = {
    parent: {
      esModuleURI: "resource://pdf.js/PdfEmbedFallbackParent.sys.mjs",
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  // Note that GeckoView has another implementation in mobile/android/actors.
  JSWINDOWACTORS.Select = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/SelectParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/SelectChild.sys.mjs",
      events: {
        mozshowdropdown: {},
        "mozshowdropdown-sourcetouch": {},
        mozhidedropdown: { mozSystemGroup: true },
      },
    },

    includeChrome: true,
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  // Note that GeckoView handles MozOpenDateTimePicker in GeckoViewPrompt.
  JSWINDOWACTORS.DateTimePicker = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/DateTimePickerParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/DateTimePickerChild.sys.mjs",
      events: {
        MozOpenDateTimePicker: {},
        MozCloseDateTimePicker: {},
      },
    },

    includeChrome: true,
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  JSWINDOWACTORS.PictureInPictureLauncher = {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/pictureinpicture/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/PictureInPictureChild.sys.mjs",
      events: {
        MozTogglePictureInPicture: { capture: true },
      },
    },
    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  JSWINDOWACTORS.PictureInPicture = {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/pictureinpicture/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/PictureInPictureChild.sys.mjs",
    },
    messageManagerGroups: ["browsers", "pip-player"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  JSWINDOWACTORS.PictureInPictureToggle = {
    parent: {
      esModuleURI:
        "moz-src:///toolkit/components/pictureinpicture/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/PictureInPictureChild.sys.mjs",
      events: {
        UAWidgetSetupOrChange: {},
        contextmenu: { capture: true },
      },
    },
    messageManagerGroups: ["browsers"],
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  JSWINDOWACTORS.AboutPDF = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/AboutPDFParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/AboutPDFChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:pdf", "about:pdf?*", "about:pdf#*"],
    remoteTypes: ["privilegedabout"],
  };

  JSWINDOWACTORS.AboutTranslations = {
    parent: {
      esModuleURI: "resource://gre/actors/AboutTranslationsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/AboutTranslationsChild.sys.mjs",
      events: {
        // Run the actor before any content of the page appears to inject functions.
        DOMDocElementInserted: {},
        DOMContentLoaded: {},
      },
    },
    matches: ["about:translations"],
    remoteTypes: ["privilegedabout"],
  };

  JSWINDOWACTORS.ColorPicker = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/ColorPickerParent.sys.mjs",
    },

    child: {
      esModuleURI: "moz-src:///toolkit/actors/ColorPickerChild.sys.mjs",
      events: {
        MozOpenColorPicker: {},
        MozCloseColorPicker: {},
      },
    },

    includeChrome: true,
    allFrames: true,
    safeForUntrustedWebProcess: true,
  };

  // GeckoView implements WebChannel communication at the embedder-level.
  JSWINDOWACTORS.WebChannel = {
    parent: {
      esModuleURI: "moz-src:///toolkit/actors/WebChannelParent.sys.mjs",
    },
    child: {
      esModuleURI: "moz-src:///toolkit/actors/WebChannelChild.sys.mjs",
      events: {
        WebChannelMessageToChrome: { capture: true, wantUntrusted: true },
      },
    },

    allFrames: true,
    safeForUntrustedWebProcess: true,
  };
}

export var ActorManagerParent = {
  _addActors(actors, kind) {
    let register, unregister;
    switch (kind) {
      case "JSProcessActor":
        register = ChromeUtils.registerProcessActor;
        unregister = ChromeUtils.unregisterProcessActor;
        break;
      case "JSWindowActor":
        register = ChromeUtils.registerWindowActor;
        unregister = ChromeUtils.unregisterWindowActor;
        break;
      default:
        throw new Error("Invalid JSActor kind " + kind);
    }
    for (let [actorName, actor] of Object.entries(actors)) {
      let actorRegistered = false;
      const registerActor = () => {
        if (!actorRegistered) {
          register(actorName, actor);
          actorRegistered = true;
        }
      };
      const unregisterActor = () => {
        if (actorRegistered) {
          unregister(actorName, actor);
          actorRegistered = false;
        }
      };

      // The actor defines its own register/unregister logic.
      if (actor.onAddActor) {
        actor.onAddActor(registerActor, unregisterActor);
        continue;
      }

      // If enablePreference is set, only register the actor while the
      // preference is set to true.
      if (actor.enablePreference) {
        Services.prefs.addObserver(actor.enablePreference, () => {
          const isEnabled = Services.prefs.getBoolPref(
            actor.enablePreference,
            false
          );
          if (isEnabled) {
            registerActor();
          } else {
            unregisterActor();
          }
          if (actor.onPreferenceChanged) {
            actor.onPreferenceChanged(isEnabled);
          }
        });

        if (!Services.prefs.getBoolPref(actor.enablePreference, false)) {
          continue;
        }
      }

      registerActor();
    }
  },

  addJSProcessActors(actors) {
    this._addActors(actors, "JSProcessActor");
  },
  addJSWindowActors(actors) {
    this._addActors(actors, "JSWindowActor");
  },
};

ActorManagerParent.addJSProcessActors(JSPROCESSACTORS);
ActorManagerParent.addJSWindowActors(JSWINDOWACTORS);
