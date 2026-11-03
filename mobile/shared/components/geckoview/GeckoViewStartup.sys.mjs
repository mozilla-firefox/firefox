/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { DelayedInit } from "resource://gre/modules/DelayedInit.sys.mjs";
import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ActorManagerParent: "resource://gre/modules/ActorManagerParent.sys.mjs",
  DoHController: "moz-src:///toolkit/components/doh/DoHController.sys.mjs",
  EventDispatcher: "resource://gre/modules/Messaging.sys.mjs",
  PdfJs: "resource://pdf.js/PdfJs.sys.mjs",
  GeckoViewPreferences:
    "moz-src:///mobile/shared/components/geckoview/GeckoViewPreferences.sys.mjs",
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
    safeForUntrustedWebProcess: true,
  },
  GeckoViewPush: {
    parent: {
      esModuleURI: "resource:///actors/GeckoViewPushParent.sys.mjs",
    },
    includeParent: true,
    safeForUntrustedWebProcess: true,
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
    safeForUntrustedWebProcess: true,
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
    safeForUntrustedWebProcess: true,
  },
  GeckoViewPrompt: {
    parent: {
      esModuleURI: "resource:///actors/GeckoViewPromptParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource:///actors/GeckoViewPromptChild.sys.mjs",
      events: {
        click: { capture: false, mozSystemGroup: true },
        mozshowdropdown: {},
        "mozshowdropdown-sourcetouch": {},
        MozOpenDateTimePicker: {},
        DOMPopupBlocked: { capture: false, mozSystemGroup: true },
        DOMRedirectBlocked: { capture: false, mozSystemGroup: true },
      },
    },
    allFrames: true,
    messageManagerGroups: ["browsers"],
    safeForUntrustedWebProcess: true,
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
    safeForUntrustedWebProcess: true,
  },
  GeckoViewPdfJs: {
    parent: {
      esModuleURI: "resource://pdf.js/GeckoViewPdfJsParent.sys.mjs",
    },
    child: {
      esModuleURI: "resource://pdf.js/GeckoViewPdfJsChild.sys.mjs",
    },
    allFrames: true,
    safeForUntrustedWebProcess: true,
  },
};

export class GeckoViewStartup {
  // Set while an ACCESS_LOCAL_NETWORK request is in flight. necko already asks
  // only once per foreground cycle, but the app being foregrounded resets that
  // while a prompt may still be up, so this stops a second overlapping request:
  // getAppPermissions() has no in-flight deduplication of its own.
  #localNetworkPermissionPending = false;

  /**
   * Asks the app to request the OS ACCESS_LOCAL_NETWORK permission, in response
   * to networking being about to connect to the local network. Fire and forget:
   * the connection that triggered this is not waiting on the answer, it will
   * fail if the permission is missing and the user can retry once granted.
   *
   * The answer is reported back to necko so it stops asking while the permission
   * is held.
   */
  async #requestLocalNetworkPermission() {
    // See: http://developer.android.com/reference/android/Manifest.permission.html
    const PERM_ACCESS_LOCAL_NETWORK = "android.permission.ACCESS_LOCAL_NETWORK";

    if (this.#localNetworkPermissionPending) {
      return;
    }
    this.#localNetworkPermissionPending = true;

    let granted = false;
    try {
      const window = Services.wm.getMostRecentWindow("navigator:geckoview");
      const actor = window?.browsingContext?.currentWindowGlobal?.getActor(
        "GeckoViewPermission"
      );
      // Already-granted permissions resolve without showing a prompt, so this
      // doubles as the check that tells necko to stop asking.
      granted = !!(await actor?.getAppPermissions([PERM_ACCESS_LOCAL_NETWORK]));
    } catch (error) {
      warn`Error requesting local network permission: ${error}`;
    } finally {
      this.#localNetworkPermissionPending = false;
      Services.obs.notifyObservers(
        null,
        "network:local-network-permission-result",
        granted ? "granted" : "denied"
      );
    }
  }

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
          ],
        });

        GeckoViewUtils.addLazyGetter(this, "GeckoViewTrackingDB", {
          module: "resource://gre/modules/GeckoViewTrackingDB.sys.mjs",
          ged: [
            "GeckoView:TrackingDB:GetEventsByDateRange",
            "GeckoView:TrackingDB:SumAllEvents",
            "GeckoView:TrackingDB:GetEarliestRecordedDate",
            "GeckoView:TrackingDB:ClearAll",
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

          GeckoViewUtils.addLazyGetter(this, "GeckoViewSessionStore", {
            module: "resource://gre/modules/GeckoViewSessionStore.sys.mjs",
            observers: [
              "browsing-context-did-set-embedder",
              "browsing-context-discarded",
            ],
          });

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

          GeckoViewUtils.addLazyGetter(this, "GeckoViewIPProtection", {
            module: "resource://gre/modules/GeckoViewIPProtection.sys.mjs",
            ged: [
              "GeckoView:IPProtection:Init",
              "GeckoView:IPProtection:Uninit",
              "GeckoView:IPProtection:IPProtectionService:GetState",
              "GeckoView:IPProtection:IPPProxyManager:GetState",
              "GeckoView:IPProtection:Activate",
              "GeckoView:IPProtection:Deactivate",
              "GeckoView:IPProtection:Enroll",
              "GeckoView:IPProtection:RefreshUsage",
              "GeckoView:IPProtection:ServerList:GetCountryList",
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
            "GeckoView:CrashPullController.Delegate:Attached",
          ]);

          // We don't register this using the LazyGetter because it needs to be
          // ready before the first call to the listener is received. The global
          // EventDispatcher instance is only available in the parent process, so
          // this must stay within the parent-process guard.
          lazy.EventDispatcher.instance.registerListener(
            lazy.GeckoViewPreferences,
            [
              "GeckoView:Preferences:GetPref",
              "GeckoView:Preferences:SetPref",
              "GeckoView:Preferences:ClearPref",
              "GeckoView:Preferences:RegisterObserver",
              "GeckoView:Preferences:UnregisterObserver",
            ]
          );
        }

        GeckoViewUtils.addLazyGetter(this, "GeckoViewAIFeatures", {
          module: "resource://gre/modules/GeckoViewAIFeatures.sys.mjs",
          ged: [
            "GeckoView:AIFeature:ListFeatures",
            "GeckoView:AIFeature:SetEnabled",
            "GeckoView:AIFeature:MakeAvailable",
          ],
        });

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

        GeckoViewUtils.addLazyGetter(this, "GeckoViewPageExtractor", {
          module: "resource://gre/modules/GeckoViewPageExtractor.sys.mjs",
          ged: ["GeckoView:PageExtractor:GetTextContent"],
        });

        GeckoViewUtils.addLazyGetter(this, "GeckoViewAutofillRuntime", {
          module: "resource://gre/modules/GeckoViewAutofill.sys.mjs",
          ged: ["GeckoView:Autofill:GetAddressStructure"],
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

        // Initialize the cookie service early so the DB is loaded by
        // the time we make the first HTTP request. Skip in xpcshell tests,
        // which expect to control when (and against which DB file) the
        // cookie service first opens the database.
        if (!Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
          Services.cookies;
        }

        GeckoViewUtils.addLazyGetter(this, "DownloadTracker", {
          module: "resource://gre/modules/GeckoViewWebExtension.sys.mjs",
          ged: ["GeckoView:WebExtension:DownloadChanged"],
        });

        // Listen for global EventDispatcher messages
        lazy.EventDispatcher.instance.registerListener(this, [
          "GeckoView:ResetUserPrefs",
          "GeckoView:SetDefaultPrefs",
          "GeckoView:SetLocale",
          "GeckoView:InitialForeground",
        ]);

        Services.obs.addObserver(this, "browser-idle-startup-tasks-finished");
        Services.obs.addObserver(this, "handlersvc-store-initialized");
        Services.obs.addObserver(
          this,
          "network:request-local-network-permission"
        );

        Services.obs.notifyObservers(null, "geckoview-startup-complete");
        break;
      }
      case "network:request-local-network-permission": {
        this.#requestLocalNetworkPermission();
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

        // This pref is set when during GeckoEngine initialization
        // and holds the value of FxNimbus.doh.autoselect-enabled (see nimbus.fml.yaml)
        // We check it here insead of using GeckoViewUtils.addLazyPrefObserver
        // because GeckoView:ResetUserPrefs also clears it during startup.
        if (
          Services.prefs.getBoolPref("network.android_doh.autoselect_enabled")
        ) {
          debug`init DoH controller`;
          lazy.DoHController.init();
        } else {
          // When the autoselect isn't enabled, these prefs should be
          // cleared. Otherwise doh-rollout.mode will override
          // network.trr.mode forever as DoHController isn't running.
          debug`cleanup DoH prefs`;
          lazy.DoHController.cleanupPrefs();
        }

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
      case "GeckoView:SetLocale": {
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
      }

      case "GeckoView:StorageDelegate:Attached":
        InitLater(() => {
          const loginDetection = Cc[
            "@mozilla.org/login-detection-service;1"
          ].getService(Ci.nsILoginDetectionService);
          loginDetection.init();
        });
        break;

      case "GeckoView:CrashPullController.Delegate:Attached":
        if (!this.RemoteSettingsCrashPull) {
          GeckoViewUtils.addLazyGetter(this, "RemoteSettingsCrashPull", {
            module: "resource://gre/modules/RemoteSettingsCrashPull.sys.mjs",
          });
          GeckoViewUtils.addLazyGetter(this, "crashPullCallback", {
            module: "resource://gre/modules/ChildCrashHandler.sys.mjs",
          });
          InitLater(() =>
            this.RemoteSettingsCrashPull.start(this.crashPullCallback)
          );
        }
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
