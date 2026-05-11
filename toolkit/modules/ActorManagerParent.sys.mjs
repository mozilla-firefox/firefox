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
  },

  ContentPrefs: {
    parent: {
      esModuleURI: "resource://gre/modules/ContentPrefServiceParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/ContentPrefServiceChild.sys.mjs",
    },
  },

  ExtensionContent: {
    child: {
      esModuleURI: "resource://gre/modules/ExtensionContent.sys.mjs",
    },
    includeParent: true,
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
      esModuleURI: "resource://gre/actors/MLEngineParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/MLEngineChild.sys.mjs",
    },
    enablePreference: "browser.ml.enable",
  },

  ProcessConduits: {
    parent: {
      esModuleURI: "resource://gre/modules/ConduitsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/ConduitsChild.sys.mjs",
    },
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
      esModuleURI: "resource://gre/modules/AboutCertViewerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/modules/AboutCertViewerChild.sys.mjs",

      events: {
        DOMDocElementInserted: { capture: true },
      },
    },

    matches: ["about:certificate"],
    remoteTypes: ["privilegedabout"],
  },

  AboutHttpsOnlyError: {
    parent: {
      esModuleURI: "resource://gre/actors/AboutHttpsOnlyErrorParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/AboutHttpsOnlyErrorChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:httpsonlyerror?*"],
    allFrames: true,
  },

  AboutRestricted: {
    parent: {
      esModuleURI: "resource://gre/actors/AboutRestrictedParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/AboutRestrictedChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
      },
    },
    matches: ["about:restricted?*"],
    allFrames: true,
  },

  AudioPlayback: {
    parent: {
      esModuleURI: "resource://gre/actors/AudioPlaybackParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/AudioPlaybackChild.sys.mjs",
      observers: ["audio-playback"],
    },

    allFrames: true,
  },

  AutoComplete: {
    parent: {
      esModuleURI: "resource://gre/actors/AutoCompleteParent.sys.mjs",
      // These two messages are also used, but are currently synchronous calls
      // through the per-process message manager.
      // "AutoComplete:GetSelectedIndex",
      // "AutoComplete:SelectBy"
    },

    child: {
      esModuleURI: "resource://gre/actors/AutoCompleteChild.sys.mjs",
    },

    allFrames: true,
  },

  Autoplay: {
    parent: {
      esModuleURI: "resource://gre/actors/AutoplayParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/AutoplayChild.sys.mjs",
      events: {
        GloballyAutoplayBlocked: {},
      },
    },

    allFrames: true,
  },

  AutoScroll: {
    parent: {
      esModuleURI: "resource://gre/actors/AutoScrollParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/AutoScrollChild.sys.mjs",
      events: {
        mousedown: { capture: true, mozSystemGroup: true },
      },
    },

    allFrames: true,
  },

  BackgroundThumbnails: {
    child: {
      esModuleURI: "resource://gre/actors/BackgroundThumbnailsChild.sys.mjs",
      events: {
        DOMDocElementInserted: { capture: true },
      },
    },
    messageManagerGroups: ["thumbnails"],
  },

  BrowserElement: {
    parent: {
      esModuleURI: "resource://gre/actors/BrowserElementParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/BrowserElementChild.sys.mjs",
      events: {
        DOMWindowClose: {},
      },
    },

    allFrames: true,
  },

  Conduits: {
    parent: {
      esModuleURI: "resource://gre/modules/ConduitsParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/modules/ConduitsChild.sys.mjs",
    },

    allFrames: true,
  },

  Controllers: {
    parent: {
      esModuleURI: "resource://gre/actors/ControllersParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/ControllersChild.sys.mjs",
    },

    allFrames: true,
  },

  CaptchaDetection: {
    parent: {
      esModuleURI: "resource://gre/actors/CaptchaDetectionParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/CaptchaDetectionChild.sys.mjs",
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
  },

  CaptchaDetectionCommunication: {
    parent: {
      esModuleURI: "resource://gre/actors/CaptchaDetectionParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "resource://gre/actors/CaptchaDetectionCommunicationChild.sys.mjs",
    },
    allFrames: true,
  },

  CookieBanner: {
    parent: {
      esModuleURI: "resource://gre/actors/CookieBannerParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/CookieBannerChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        load: { capture: true },
      },
    },
    // Only need handle cookie banners for HTTP/S scheme.
    matches: ["https://*/*", "http://*/*"],
    // Only handle banners for browser tabs (including sub-frames).
    messageManagerGroups: ["browsers"],
    // Cookie banners can be shown in sub-frames so we need to include them.
    allFrames: true,
    onAddActor(register, unregister) {
      let isRegistered = false;

      const maybeRegister = () => {
        const isEnabled = Services.prefs.getBoolPref(
          "cookiebanners.bannerClicking.enabled",
          false
        );
        const mode = Services.prefs.getIntPref("cookiebanners.service.mode", 0);
        const privateBrowsing = Services.prefs.getIntPref(
          "cookiebanners.service.mode.privateBrowsing"
        );
        if (isEnabled && (mode != 0 || privateBrowsing != 0)) {
          if (!isRegistered) {
            register();
            isRegistered = true;
          }
        } else if (isRegistered) {
          unregister();
          isRegistered = false;
        }
      };

      [
        "cookiebanners.bannerClicking.enabled",
        "cookiebanners.service.mode",
        "cookiebanners.service.mode.privateBrowsing",
      ].forEach(prefName => {
        Services.prefs.addObserver(prefName, maybeRegister);
      });

      maybeRegister();
    },
  },

  ExtFind: {
    child: {
      esModuleURI: "resource://gre/actors/ExtFindChild.sys.mjs",
    },

    allFrames: true,
  },

  FindBar: {
    parent: {
      esModuleURI: "resource://gre/actors/FindBarParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/FindBarChild.sys.mjs",
      events: {
        keypress: { mozSystemGroup: true },
      },
    },

    allFrames: true,
    messageManagerGroups: ["browsers", "test"],
  },

  // This is the actor that responds to requests from the find toolbar and
  // searches for matches and highlights them.
  Finder: {
    child: {
      esModuleURI: "resource://gre/actors/FinderChild.sys.mjs",
    },

    allFrames: true,
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
  },

  InlineSpellChecker: {
    parent: {
      esModuleURI: "resource://gre/actors/InlineSpellCheckerParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/InlineSpellCheckerChild.sys.mjs",
    },

    allFrames: true,
  },

  KeyPressEventModelChecker: {
    child: {
      esModuleURI:
        "resource://gre/actors/KeyPressEventModelCheckerChild.sys.mjs",
      events: {
        CheckKeyPressEventModel: { capture: true, mozSystemGroup: true },
      },
    },

    allFrames: true,
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
  },

  ManifestMessages: {
    child: {
      esModuleURI: "resource://gre/modules/ManifestMessagesChild.sys.mjs",
    },
  },

  NetError: {
    parent: {
      esModuleURI: "resource://gre/actors/NetErrorParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/NetErrorChild.sys.mjs",
      events: {
        DOMDocElementInserted: {},
        click: {},
      },
    },

    matches: ["about:certerror?*", "about:neterror?*"],
    allFrames: true,
  },

  OpenSearchLoader: {
    child: {
      esModuleURI:
        "moz-src:///toolkit/components/search/OpenSearchLoaderChild.sys.mjs",
    },
    matches: ["about:blank"],
    messageManagerGroups: ["opensearch"],
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
    ],
    messageManagerGroups: ["browsers", "headless-browsers"],
  },

  PopupAndRedirectBlocking: {
    parent: {
      esModuleURI:
        "resource://gre/actors/PopupAndRedirectBlockingParent.sys.mjs",
    },
    child: {
      esModuleURI:
        "resource://gre/actors/PopupAndRedirectBlockingChild.sys.mjs",
      events: {
        DOMPopupBlocked: { capture: true },
        DOMRedirectBlocked: { capture: true },
        // Only listen for the `pageshow` event after the actor has already been
        // created for some other reason.
        pageshow: { createActor: false },
      },
    },
    allFrames: true,
  },

  Printing: {
    parent: {
      esModuleURI: "resource://gre/actors/PrintingParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/PrintingChild.sys.mjs",
      events: {
        PrintingError: { capture: true },
        printPreviewUpdate: { capture: true },
      },
    },
  },

  PrintingSelection: {
    child: {
      esModuleURI: "resource://gre/actors/PrintingSelectionChild.sys.mjs",
    },
    allFrames: true,
  },

  PurgeSessionHistory: {
    child: {
      esModuleURI: "resource://gre/actors/PurgeSessionHistoryChild.sys.mjs",
    },
    allFrames: true,
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
  },

  TLSCertificateBinding: {
    child: {
      esModuleURI: "resource://gre/actors/TLSCertificateBindingChild.sys.mjs",
    },

    messageManagerGroups: ["browsers"],
  },

  // This actor is available for all pages that one can
  // view the source of, however it won't be created until a
  // request to view the source is made via the message
  // 'ViewSource:LoadSource' or 'ViewSource:LoadSourceWithSelection'.
  ViewSource: {
    child: {
      esModuleURI: "resource://gre/actors/ViewSourceChild.sys.mjs",
    },

    allFrames: true,
  },

  // This actor is for the view-source page itself.
  ViewSourcePage: {
    parent: {
      esModuleURI: "resource://gre/actors/ViewSourcePageParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/ViewSourcePageChild.sys.mjs",
      events: {
        pageshow: { capture: true },
        click: {},
      },
    },

    matches: ["view-source:*"],
    allFrames: true,
  },

  WebChannel: {
    parent: {
      esModuleURI: "resource://gre/actors/WebChannelParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/WebChannelChild.sys.mjs",
      events: {
        WebChannelMessageToChrome: { capture: true, wantUntrusted: true },
      },
    },

    allFrames: true,
  },

  Thumbnails: {
    child: {
      esModuleURI: "resource://gre/actors/ThumbnailsChild.sys.mjs",
    },
  },

  // Determines if a page can be translated, and coordinates communication with the
  // translations engine.
  Translations: {
    parent: {
      esModuleURI: "resource://gre/actors/TranslationsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/TranslationsChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        load: {
          // Once the page is loaded, it's important that we react to the page's
          // language tag as soon as possible in order to give a good response time
          // for showing the translations panel, or for auto-translating, etc.
          capture: true,
          createActor: false,
        },
      },
    },
    matches: ["http://*/*", "https://*/*", "file:///*", "moz-extension://*"],
    messageManagerGroups: ["browsers"],
    enablePreference: "browser.translations.enable",
    onPreferenceChanged(isEnabled) {
      const { TranslationsParent } = ChromeUtils.importESModule(
        "resource://gre/actors/TranslationsParent.sys.mjs"
      );
      TranslationsParent.onIsEnabledChanged(isEnabled);
    },
  },

  UAWidgets: {
    child: {
      esModuleURI: "resource://gre/actors/UAWidgetsChild.sys.mjs",
      events: {
        UAWidgetSetupOrChange: {},
        UAWidgetTeardown: {},
      },
    },

    includeChrome: true,
    allFrames: true,
  },

  UnselectedTabHover: {
    parent: {
      esModuleURI: "resource://gre/actors/UnselectedTabHoverParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/UnselectedTabHoverChild.sys.mjs",
      events: {
        "UnselectedTabHover:Enable": {},
        "UnselectedTabHover:Disable": {},
      },
    },

    allFrames: true,
  },
};

/**
 * Note that turning on page data collection for snapshots currently disables
 * collection of generic page info for normal history entries. See bug 1740234.
 */
if (!Services.prefs.getBoolPref("browser.pagedata.enabled", false)) {
  JSWINDOWACTORS.ContentMeta = {
    parent: {
      esModuleURI: "resource://gre/actors/ContentMetaParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/ContentMetaChild.sys.mjs",
      events: {
        DOMContentLoaded: {},
        DOMMetaAdded: { createActor: false },
      },
    },

    messageManagerGroups: ["browsers"],
  };
}

if (AppConstants.platform != "android") {
  // Note that GeckoView has another implementation in mobile/android/actors.
  JSWINDOWACTORS.Select = {
    parent: {
      esModuleURI: "resource://gre/actors/SelectParent.sys.mjs",
    },

    child: {
      esModuleURI: "resource://gre/actors/SelectChild.sys.mjs",
      events: {
        mozshowdropdown: {},
        "mozshowdropdown-sourcetouch": {},
        mozhidedropdown: { mozSystemGroup: true },
      },
    },

    includeChrome: true,
    allFrames: true,
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
  };

  JSWINDOWACTORS.PictureInPictureLauncher = {
    parent: {
      esModuleURI: "resource://gre/modules/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/PictureInPictureChild.sys.mjs",
      events: {
        MozTogglePictureInPicture: { capture: true },
      },
    },
    messageManagerGroups: ["browsers"],
    allFrames: true,
  };

  JSWINDOWACTORS.PictureInPicture = {
    parent: {
      esModuleURI: "resource://gre/modules/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/PictureInPictureChild.sys.mjs",
    },
    messageManagerGroups: ["browsers", "pip-player"],
    allFrames: true,
  };

  JSWINDOWACTORS.PictureInPictureToggle = {
    parent: {
      esModuleURI: "resource://gre/modules/PictureInPicture.sys.mjs",
    },
    child: {
      esModuleURI: "resource://gre/actors/PictureInPictureChild.sys.mjs",
      events: {
        UAWidgetSetupOrChange: {},
        contextmenu: { capture: true },
      },
    },
    messageManagerGroups: ["browsers"],
    allFrames: true,
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
