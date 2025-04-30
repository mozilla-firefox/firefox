/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DelayedInit } from "resource://gre/modules/DelayedInit.sys.mjs";
import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ActorManagerParent: "resource://gre/modules/ActorManagerParent.sys.mjs",
  DoHController: "resource://gre/modules/DoHController.sys.mjs",
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  PdfJs: "resource://pdf.js/PdfJs.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging("Startup");

function InitLater(fn, object, name) {
  return DelayedInit.schedule(fn, object, name, 15000 /* 15s max wait */);
}

const JSPROCESSACTORS = {
  GeckoViewPermissionProcess: {
    parent: {
      esModuleURI:
        "resource:///actors/GeckoViewPermissionProcessParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/GeckoViewPermissionProcessChild.sys.mjs",
      observers: [
        "getUserMedia:ask-device-permission",
        "getUserMedia:request",
        "recording-device-events",
        "PeerConnection:request",
      ],
    },
  },
};

const JSWINDOWACTORS = {
  LoadURIDelegate: {
    parent: {
      esModuleURI: "resource:///actors/LoadURIDelegateParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/LoadURIDelegateChild.sys.mjs",
    },
    messageManagerGroups: ["browsers"],
  },
  GeckoViewPermission: {
    parent: {
      esModuleURI: "resource:///actors/GeckoViewPermissionParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/GeckoViewPermissionChild.sys.mjs",
    },
    allFrames: true,
    includeChrome: true,
  },
  GeckoViewPrompt: {
    child: {
      esModuleURI: "resource:///actors/GeckoViewPromptChild.sys.mjs",
      events: {
        click: { capture: false, mozSystemGroup: true },
        contextmenu: { capture: false, mozSystemGroup: true },
        mozshowdropdown: {},
        "mozshowdropdown-sourcetouch": {},
        MozOpenDateTimePicker: {},
        DOMPopupBlocked: { capture: false, mozSystemGroup: true },
      },
    },
    allFrames: true,
    messageManagerGroups: ["browsers"],
  },
  GeckoViewFormValidation: {
    child: {
      esModuleURI: "resource:///actors/GeckoViewFormValidationChild.sys.mjs",
      events: {
        MozInvalidForm: {},
      },
    },
    allFrames: true,
    messageManagerGroups: ["browsers"],
  },
  GeckoViewPdfjs: {
    parent: {
      esModuleURI: "resource://pdf.js/GeckoViewPdfjsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://pdf.js/GeckoViewPdfjsChild.sys.mjs",
    },
    allFrames: true,
  },
};

export class GeckoViewStartup {
  /* ----------  nsIObserver  ---------- */
  observe(aSubject, aTopic) {
    debug`observe: ${aTopic}`;
    switch (aTopic) {
      case "content-process-ready-for-script":
      case "app-startup": {
        GeckoViewUtils.addLazyGetter(this, "GeckoViewConsole", {
          module: "resource://gre/modules/GeckoViewConsole.sys.mjs",
        });

        GeckoViewUtils.addLazyGetter(this, "GeckoViewStorageController", {
          module: "resource://gre/modules/GeckoViewStorageController.sys.mjs",
          ged: [
            "GeckoView:ClearData",
            "GeckoView:ClearSessionContextData",
            "GeckoView:ClearHostData",
            "GeckoView:ClearBaseDomainData",
            "GeckoView:GetAllPermissions",
            "GeckoView:GetPermissionsByURI",
            "GeckoView:SetPermission",
            "GeckoView:SetPermissionByURI",
            "GeckoView:GetCookieBannerModeForDomain",
            "GeckoView:SetCookieBannerModeForDomain",
            "GeckoView:RemoveCookieBannerModeForDomain",
          ],
        });

        GeckoViewUtils.addLazyGetter(this, "GeckoViewPushController", {
          module: "resource://gre/modules/GeckoViewPushController.sys.mjs",
          ged: ["GeckoView:PushEvent", "GeckoView:PushSubscriptionChanged"],
        });

        GeckoViewUtils.addLazyPrefObserver(
          {
            name: "geckoview.console.enabled",
            default: false,
          },
          {
            handler: _ => this.GeckoViewConsole,
          }
        );

        // Parent process only
        if (
          Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_DEFAULT
        ) {
          lazy.ActorManagerParent.addJSWindowActors(JSWINDOWACTORS);
          lazy.ActorManagerParent.addJSProcessActors(JSPROCESSACTORS);

          if (Services.appinfo.sessionHistoryInParent) {
            GeckoViewUtils.addLazyGetter(this, "GeckoViewSessionStore", {
              module: "resource://gre/modules/GeckoViewSessionStore.sys.mjs",
              observers: [
                "browsing-context-did-set-embedder",
                "browsing-context-discarded",
              ],
            });
          }

          GeckoViewUtils.addLazyGetter(this, "GeckoViewWebExtension", {
            module: "resource://gre/modules/GeckoViewWebExtension.sys.mjs",
            ged: [
              "GeckoView:ActionDelegate:Attached",
              "GeckoView:BrowserAction:Click",
              "GeckoView:PageAction:Click",
              "GeckoView:RegisterWebExtension",
              "GeckoView:UnregisterWebExtension",
              "GeckoView:WebExtension:CancelInstall",
              "GeckoView:WebExtension:Disable",
              "GeckoView:WebExtension:Enable",
              "GeckoView:WebExtension:EnsureBuiltIn",
              "GeckoView:WebExtension:Get",
              "GeckoView:WebExtension:Install",
              "GeckoView:WebExtension:InstallBuiltIn",
              "GeckoView:WebExtension:List",
              "GeckoView:WebExtension:PortDisconnect",
              "GeckoView:WebExtension:PortMessageFromApp",
              "GeckoView:WebExtension:SetPBAllowed",
              "GeckoView:WebExtension:AddOptionalPermissions",
              "GeckoView:WebExtension:RemoveOptionalPermissions",
              "GeckoView:WebExtension:Uninstall",
              "GeckoView:WebExtension:Update",
              "GeckoView:WebExtension:EnableProcessSpawning",
              "GeckoView:WebExtension:DisableProcessSpawning",
            ],
            observers: [
              "devtools-installed-addon",
              "testing-installed-addon",
              "testing-uninstalled-addon",
            ],
          });

          GeckoViewUtils.addLazyGetter(this, "ChildCrashHandler", {
            module: "resource://gre/modules/ChildCrashHandler.sys.mjs",
            observers: [
              "compositor:process-aborted",
              "ipc:content-created",
              "ipc:content-shutdown",
              "process-type-set",
            ],
          });

          lazy.EventDispatcher.instance.registerListener(this, [
            "GeckoView:StorageDelegate:Attached",
          ]);
        }

        GeckoViewUtils.addLazyGetter(this, "GeckoViewTranslationsSettings", {
          module: "resource://gre/modules/GeckoViewTranslations.sys.mjs",
          ged: [
            "GeckoView:Translations:IsTranslationEngineSupported",
            "GeckoView:Translations:PreferredLanguages",
            "GeckoView:Translations:ManageModel",
            "GeckoView:Translations:TranslationInformation",
            "GeckoView:Translations:ModelInformation",
            "GeckoView:Translations:GetLanguageSetting",
            "GeckoView:Translations:GetLanguageSettings",
            "GeckoView:Translations:SetLanguageSettings",
            "GeckoView:Translations:GetNeverTranslateSpecifiedSites",
            "GeckoView:Translations:SetNeverTranslateSpecifiedSite",
            "GeckoView:Translations:GetTranslateDownloadSize",
          ],
        });

        break;
      }

      case "profile-after-change": {
        GeckoViewUtils.addLazyGetter(this, "GeckoViewRemoteDebugger", {
          module: "resource://gre/modules/GeckoViewRemoteDebugger.sys.mjs",
          init: gvrd => gvrd.onInit(),
        });

        GeckoViewUtils.addLazyPrefObserver(
          {
            name: "devtools.debugger.remote-enabled",
            default: false,
          },
          {
            handler: _ => this.GeckoViewRemoteDebugger,
          }
        );

        GeckoViewUtils.addLazyGetter(this, "DownloadTracker", {
          module: "resource://gre/modules/GeckoViewWebExtension.sys.mjs",
          ged: ["GeckoView:WebExtension:DownloadChanged"],
        });

        ChromeUtils.importESModule(
          "resource://gre/modules/MemoryNotificationDB.sys.mjs"
        );

        ChromeUtils.importESModule(
          "resource://gre/modules/NotificationDB.sys.mjs"
        );

        // Listen for global EventDispatcher messages
        lazy.EventDispatcher.instance.registerListener(this, [
          "GeckoView:ResetUserPrefs",
          "GeckoView:SetDefaultPrefs",
          "GeckoView:SetLocale",
          "GeckoView:InitialForeground",
        ]);

        Services.obs.addObserver(this, "browser-idle-startup-tasks-finished");
        Services.obs.addObserver(this, "handlersvc-store-initialized");

        Services.obs.notifyObservers(null, "geckoview-startup-complete");
        break;
      }
      case "browser-idle-startup-tasks-finished": {
        // TODO bug 1730026: when an alternative is introduced that runs once,
        // replace this observer topic with that alternative.
        // This only needs to happen once during startup.
        Services.obs.removeObserver(this, aTopic);
        // Notify the start up crash tracker that the browser has successfully
        // started up so the startup cache isn't rebuilt on next startup.
        Services.startup.trackStartupCrashEnd();

        lazy.DoHController.init();
        break;
      }
      case "handlersvc-store-initialized": {
        // Initialize PdfJs when running in-process and remote. This only
        // happens once since PdfJs registers global hooks. If the PdfJs
        // extension is installed the init method below will be overridden
        // leaving initialization to the extension.
        // parent only: configure default prefs, set up pref observers, register
        // pdf content handler, and initializes parent side message manager
        // shim for privileged api access.
        try {
          lazy.PdfJs.init(this._isNewProfile);
        } catch {}
        break;
      }
    }
  }

  onEvent(aEvent, aData) {
    debug`onEvent ${aEvent}`;

    switch (aEvent) {
      case "GeckoView:InitialForeground": {
        // ExtensionProcessCrashObserver observes this topic to determine when
        // the app goes into the foreground for the first time. This could be useful
        // when the app is initially created in the background because, in this case,
        // the "application-foreground" topic isn't notified when the application is
        // moved into the foreground later. That is because "application-foreground"
        // is only going to be notified when the application was first paused.
        Services.obs.notifyObservers(null, "geckoview-initial-foreground");
        break;
      }
      case "GeckoView:ResetUserPrefs": {
        for (const name of aData.names) {
          Services.prefs.clearUserPref(name);
        }
        break;
      }
      case "GeckoView:SetDefaultPrefs": {
        const prefs = Services.prefs.getDefaultBranch("");
        for (const [name, value] of Object.entries(aData)) {
          try {
            switch (typeof value) {
              case "string":
                prefs.setStringPref(name, value);
                break;
              case "number":
                prefs.setIntPref(name, value);
                break;
              case "boolean":
                prefs.setBoolPref(name, value);
                break;
              default:
                throw new Error(
                  `Can't set ${name} to ${value}. Type ${typeof value} is not supported.`
                );
            }
          } catch (e) {
            warn`Failed to set preference ${name}: ${e}`;
          }
        }
        break;
      }
      case "GeckoView:SetLocale":
        if (aData.requestedLocales) {
          Services.locale.requestedLocales = aData.requestedLocales;
        }
        const pls = Cc["@mozilla.org/pref-localizedstring;1"].createInstance(
          Ci.nsIPrefLocalizedString
        );
        pls.data = aData.acceptLanguages;
        Services.prefs.setComplexValue(
          "intl.accept_languages",
          Ci.nsIPrefLocalizedString,
          pls
        );
        break;

      case "GeckoView:StorageDelegate:Attached":
        InitLater(() => {
          const loginDetection = Cc[
            "@mozilla.org/login-detection-service;1"
          ].createInstance(Ci.nsILoginDetectionService);
          loginDetection.init();
        });
        break;
    }
  }
}

GeckoViewStartup.prototype.classID = Components.ID(
  "{8e993c34-fdd6-432c-967e-f995d888777f}"
);
GeckoViewStartup.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIObserver",
]);
