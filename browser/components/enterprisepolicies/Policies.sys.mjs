/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint prefer-const: ["error", { destructuring: "all" }] */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetters(lazy, {
  gCertDB: ["@mozilla.org/security/x509certdb;1", Ci.nsIX509CertDB],
  gExternalProtocolService: [
    "@mozilla.org/uriloader/external-protocol-service;1",
    Ci.nsIExternalProtocolService,
  ],
  gMIMEService: ["@mozilla.org/mime;1", Ci.nsIMIMEService],
});

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  BookmarksPolicies: "resource:///modules/policies/BookmarksPolicies.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  ProxyPolicies: "resource:///modules/policies/ProxyPolicies.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  EphemeralContainerWatcher:
    "resource:///modules/policies/EphemeralContainerWatcher.sys.mjs",
  WebsiteFilter: "resource:///modules/policies/WebsiteFilter.sys.mjs",
  LaunchOnLogin: "resource://gre/modules/LaunchOnLogin.sys.mjs",

  PoliciesUtils: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  addAllowDenyPermissions: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  addPolicyPermission: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  applyExtensionGuards: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  blockAboutPage: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  clearBlockedAboutPages: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  clearRunOnceModification: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  describePreferenceFailure: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  discardAMOUpdateURLs: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  installAddonFromURL: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  installAddonFromRepository: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  installAddonFromUpdateURL: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  pemToBase64: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  processMIMEInfo: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  replacePathVariables: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  reportFailure: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  setDefaultPermission: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  runOncePerModification: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  uninstallListedAddons: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  SitePolicyUtils: "resource://gre/modules/SitePolicyUtils.sys.mjs",
});

const PREF_LOGLEVEL = "browser.policies.loglevel";
const BROWSER_DOCUMENT_URL = AppConstants.BROWSER_CHROME_URL;

// The only prefs read when clearing at shutdown.
const CLEAR_ON_SHUTDOWN_PREFS = {
  BrowsingHistoryAndDownloads:
    "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
  CookiesAndStorage: "privacy.clearOnShutdown_v2.cookiesAndStorage",
  Cache: "privacy.clearOnShutdown_v2.cache",
  FormData: "privacy.clearOnShutdown_v2.formdata",
  SiteSettings: "privacy.clearOnShutdown_v2.siteSettings",
};

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "Policies",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "warn",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

/*
 * ============================
 * = POLICIES IMPLEMENTATIONS =
 * ============================
 *
 * The Policies object below is where the implementation for each policy
 * happens. An object for each policy should be defined, containing
 * callback functions that will be called by the engine.
 *
 * See the _callbacks object in EnterprisePoliciesParent.sys.mjs for the list of
 * possible callbacks and an explanation of each.
 *
 * Each callback will be called with two parameters:
 * - manager
 *   This is the EnterprisePoliciesManager singleton object from
 *   EnterprisePoliciesParent.sys.mjs
 *
 * - param
 *   The parameter defined for this policy in policies-schema.json.
 *   It will be different for each policy. It could be a boolean,
 *   a string, an array or a complex object. All parameters have
 *   been validated according to the schema, and no unknown
 *   properties will be present on them.
 *
 * The callbacks will be bound to their parent policy object.
 */
export var Policies = {
  // Used for cleaning up policies.
  // Use the same timing that you used for setting up the policy.
  _cleanup: {
    onBeforeAddons() {
      if (Cu.isInAutomation) {
        lazy.clearBlockedAboutPages();
      }
      Services.obs.notifyObservers(
        null,
        "EnterprisePolicies:Cleanup",
        "onBeforeAddons"
      );
    },
    onProfileAfterChange() {
      Services.obs.notifyObservers(
        null,
        "EnterprisePolicies:Cleanup",
        "onProfileAfterChange"
      );
    },
    onBeforeUIStartup() {
      Services.obs.notifyObservers(
        null,
        "EnterprisePolicies:Cleanup",
        "onBeforeUIStartup"
      );
    },
    onAllWindowsRestored() {
      Services.obs.notifyObservers(
        null,
        "EnterprisePolicies:Cleanup",
        "onAllWindowsRestored"
      );
    },
  },

  "3rdparty": {
    onBeforeAddons(manager, param) {
      manager.setExtensionPolicies(param.Extensions);
    },
  },

  AIControls: {
    onBeforeAddons(manager, param) {
      const features = [
        [
          "SidebarChatbot",
          ["browser.ml.chat.enabled", "browser.ml.chat.page"],
          "browser.ai.control.sidebarChatbot",
        ],
        [
          "Translations",
          ["browser.translations.enable"],
          "browser.ai.control.translations",
        ],
        [
          "PDFAltText",
          ["pdfjs.enableAltText"],
          "browser.ai.control.pdfjsAltText",
        ],
        [
          "LinkPreviewKeyPoints",
          ["browser.ml.linkPreview.enabled"],
          "browser.ai.control.linkPreviewKeyPoints",
        ],
        [
          "SmartTabGroups",
          ["browser.tabs.groups.smart.userEnabled"],
          "browser.ai.control.smartTabGroups",
        ],
        ["SmartWindow", [], "browser.ai.control.smartWindow"],
        ["SpeechRecognition", [], "browser.ai.control.speechRecognition"],
        ["CustomWidgets", [], "browser.ai.control.generatedWidgets"],
      ];

      const defaultItem = param.Default;
      const defaultLocked = defaultItem?.Locked ?? false;

      for (const [key, prefs, aiControlPref] of features) {
        const item = param[key] ?? defaultItem;
        if (!item) {
          continue;
        }
        const value = item.Value;
        const locked = item.Locked ?? defaultLocked;
        lazy.PoliciesUtils.setDefaultPref(aiControlPref, value, locked);
        for (const pref of prefs) {
          lazy.PoliciesUtils.setDefaultPref(
            pref,
            value === "available",
            locked
          );
        }
      }

      if (defaultItem) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.ai.control.default",
          defaultItem.Value,
          defaultLocked
        );
      }
    },
  },

  AllowedDomainsForApps: {
    onBeforeAddons(manager, param) {
      Services.obs.addObserver(function (subject) {
        const channel = subject.QueryInterface(Ci.nsIHttpChannel);
        if (
          lazy.SitePolicyUtils.stripTrailingDots(channel.URI.host).endsWith(
            ".google.com"
          )
        ) {
          channel.setRequestHeader("X-GoogApps-Allowed-Domains", param, true);
        }
      }, "http-on-modify-request");
    },
  },

  AllowFileSelectionDialogs: {
    onBeforeUIStartup(manager, param) {
      if (!param) {
        lazy.PoliciesUtils.setAndLockPref("widget.disable_file_pickers", true);
        lazy.PoliciesUtils.setAndLockPref(
          "browser.download.useDownloadDir",
          true
        );
        manager.disallowFeature("filepickers");
      }
    },
  },

  AppAutoUpdate: {
    onBeforeUIStartup(manager, param) {
      // Logic feels a bit reversed here, but it's correct. If AppAutoUpdate is
      // true, we disallow turning off auto updating, and visa versa.
      if (param) {
        manager.disallowFeature("app-auto-updates-off");
      } else {
        manager.disallowFeature("app-auto-updates-on");
      }
    },
  },

  AppUpdatePin: {
    validate(param) {
      // This is the version when pinning was introduced. Attempting to set a
      // pin before this will not work, because Balrog's pinning table will
      // never have the necessary entry.
      const earliestPinMajorVersion = 102;
      const earliestPinMinorVersion = 0;

      const pinParts = param.split(".");

      if (pinParts.length < 2) {
        lazy.log.error("AppUpdatePin has too few dots.");
        return false;
      }
      if (pinParts.length > 3) {
        lazy.log.error("AppUpdatePin has too many dots.");
        return false;
      }

      const trailingPinPart = pinParts.pop();
      if (trailingPinPart != "") {
        lazy.log.error("AppUpdatePin does not end with a trailing dot.");
        return false;
      }

      const pinMajorVersionStr = pinParts.shift();
      if (!pinMajorVersionStr.length) {
        lazy.log.error("AppUpdatePin's major version is empty.");
        return false;
      }
      if (!/^\d+$/.test(pinMajorVersionStr)) {
        lazy.log.error(
          "AppUpdatePin's major version contains a non-numeric character."
        );
        return false;
      }
      if (/^0/.test(pinMajorVersionStr)) {
        lazy.log.error("AppUpdatePin's major version contains a leading 0.");
        return false;
      }
      const pinMajorVersionInt = parseInt(pinMajorVersionStr, 10);
      if (isNaN(pinMajorVersionInt)) {
        lazy.log.error(
          "AppUpdatePin's major version could not be parsed to an integer."
        );
        return false;
      }
      if (pinMajorVersionInt < earliestPinMajorVersion) {
        lazy.log.error(
          `AppUpdatePin must not be earlier than '${earliestPinMajorVersion}.${earliestPinMinorVersion}.'.`
        );
        return false;
      }

      if (pinParts.length) {
        const pinMinorVersionStr = pinParts.shift();
        if (!pinMinorVersionStr.length) {
          lazy.log.error("AppUpdatePin's minor version is empty.");
          return false;
        }
        if (!/^\d+$/.test(pinMinorVersionStr)) {
          lazy.log.error(
            "AppUpdatePin's minor version contains a non-numeric character."
          );
          return false;
        }
        if (/^0\d/.test(pinMinorVersionStr)) {
          lazy.log.error("AppUpdatePin's minor version contains a leading 0.");
          return false;
        }
        const pinMinorVersionInt = parseInt(pinMinorVersionStr, 10);
        if (isNaN(pinMinorVersionInt)) {
          lazy.log.error(
            "AppUpdatePin's minor version could not be parsed to an integer."
          );
          return false;
        }
        if (
          pinMajorVersionInt == earliestPinMajorVersion &&
          pinMinorVersionInt < earliestPinMinorVersion
        ) {
          lazy.log.error(
            `AppUpdatePin must not be earlier than '${earliestPinMajorVersion}.${earliestPinMinorVersion}.'.`
          );
          return false;
        }
      }

      return true;
    },
    // No additional implementation needed here. UpdateService.sys.mjs will check
    // for this policy directly when determining the update URL.
  },

  AppUpdateURL: {
    // No implementation needed here. UpdateService.sys.mjs will check for this
    // policy directly when determining the update URL.
  },

  Authentication: {
    onBeforeAddons(manager, param) {
      // When Authentication was originally implemented, it was always
      // locked, so it defaults to locked.
      let locked = true;
      if ("Locked" in param) {
        locked = param.Locked;
      }
      if ("SPNEGO" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.negotiate-auth.trusted-uris",
          param.SPNEGO.join(", "),
          locked
        );
      }
      if ("Delegated" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.negotiate-auth.delegation-uris",
          param.Delegated.join(", "),
          locked
        );
      }
      if ("NTLM" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.automatic-ntlm-auth.trusted-uris",
          param.NTLM.join(", "),
          locked
        );
      }
      if ("AllowNonFQDN" in param) {
        if ("NTLM" in param.AllowNonFQDN) {
          lazy.PoliciesUtils.setDefaultPref(
            "network.automatic-ntlm-auth.allow-non-fqdn",
            param.AllowNonFQDN.NTLM,
            locked
          );
        }
        if ("SPNEGO" in param.AllowNonFQDN) {
          lazy.PoliciesUtils.setDefaultPref(
            "network.negotiate-auth.allow-non-fqdn",
            param.AllowNonFQDN.SPNEGO,
            locked
          );
        }
      }
      if ("AllowProxies" in param) {
        if ("NTLM" in param.AllowProxies) {
          lazy.PoliciesUtils.setDefaultPref(
            "network.automatic-ntlm-auth.allow-proxies",
            param.AllowProxies.NTLM,
            locked
          );
        }
        if ("SPNEGO" in param.AllowProxies) {
          lazy.PoliciesUtils.setDefaultPref(
            "network.negotiate-auth.allow-proxies",
            param.AllowProxies.SPNEGO,
            locked
          );
        }
      }
      if ("PrivateBrowsing" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.auth.private-browsing-sso",
          param.PrivateBrowsing,
          locked
        );
      }
    },
  },

  AutofillAddressEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "extensions.formautofill.addresses.enabled",
        param
      );
    },
  },

  AutofillCreditCardEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "extensions.formautofill.creditCards.enabled",
        param
      );
    },
  },

  AutoLaunchProtocolsFromOrigins: {
    onBeforeAddons(manager, param) {
      for (const info of param) {
        lazy.addAllowDenyPermissions(
          `open-protocol-handler^${info.protocol}`,
          info.allowed_origins
        );
      }
    },
  },

  BackgroundAppUpdate: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("app-background-update-off");
      } else {
        manager.disallowFeature("app-background-update-on");
      }
    },
  },

  BlockAboutAddons: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.blockAboutPage(manager, "about:addons", true);
      }
    },
  },

  BlockAboutConfig: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.blockAboutPage(manager, "about:config");
        lazy.PoliciesUtils.setAndLockPref("devtools.chrome.enabled", false);
      }
    },
  },

  BlockAboutProfiles: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("profileManagement");
      }
    },
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.blockAboutPage(manager, "about:profiles");
        lazy.blockAboutPage(manager, "about:profilemanager");
        lazy.blockAboutPage(manager, "about:editprofile");
        lazy.blockAboutPage(manager, "about:deleteprofile");
        lazy.blockAboutPage(manager, "about:newprofile");
      }
    },
  },

  BlockAboutSupport: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.blockAboutPage(manager, "about:support");
        manager.disallowFeature("aboutSupport");
      }
    },
  },

  Bookmarks: {
    onAllWindowsRestored(manager, param) {
      // Returned so that the engine can report a failure of the bookmark
      // processing against this policy.
      return lazy.BookmarksPolicies.processBookmarks(param);
    },
  },

  BrowserDataBackup: {
    onBeforeUIStartup(manager, param) {
      if (typeof param === "boolean") {
        lazy.PoliciesUtils.setAndLockPref("browser.backup.enabled", param);
        lazy.PoliciesUtils.setAndLockPref(
          "browser.backup.archive.enabled",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "browser.backup.restore.enabled",
          param
        );
      } else {
        const hasBackup = "AllowBackup" in param;
        const hasRestore = "AllowRestore" in param;
        let serviceValue;

        if (hasBackup && hasRestore) {
          // both present but could be set to false
          serviceValue = param.AllowBackup || param.AllowRestore;
        } else if (hasBackup && param.AllowBackup) {
          // only AllowBackup is true
          serviceValue = true;
        } else if (hasRestore && param.AllowRestore) {
          // only AllowRestore is true
          serviceValue = true;
        }

        if (serviceValue !== undefined) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.backup.enabled",
            serviceValue,
            true
          );
        }

        if (hasBackup) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.backup.archive.enabled",
            param.AllowBackup,
            true
          );
        }

        if (hasRestore) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.backup.restore.enabled",
            param.AllowRestore,
            true
          );
        }
      }
    },
  },

  CaptivePortal: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "network.captive-portal-service.enabled",
        param
      );
    },
  },

  Certificates: {
    onBeforeAddons(manager, param) {
      if ("ImportEnterpriseRoots" in param) {
        lazy.PoliciesUtils.setAndLockPref(
          "security.enterprise_roots.enabled",
          param.ImportEnterpriseRoots
        );
      }
      if ("Install" in param) {
        (async () => {
          let dirs = [];
          const platform = AppConstants.platform;
          if (platform == "win") {
            dirs = [
              // Ugly, but there is no official way to get %USERNAME\AppData\Roaming\Mozilla.
              Services.dirsvc.get("XREUSysExt", Ci.nsIFile).parent,
              // Even more ugly, but there is no official way to get %USERNAME\AppData\Local\Mozilla.
              Services.dirsvc.get("DefProfLRt", Ci.nsIFile).parent.parent,
            ];
          } else if (platform == "macosx" || platform == "linux") {
            dirs = [
              // These two keys are named wrong. They return the Mozilla directory.
              Services.dirsvc.get("XREUserNativeManifests", Ci.nsIFile),
              Services.dirsvc.get("XRESysNativeManifests", Ci.nsIFile),
            ];
          }
          dirs.unshift(Services.dirsvc.get("XREAppDist", Ci.nsIFile));
          for (const certfilename of param.Install) {
            let certfile;
            try {
              certfile = Cc["@mozilla.org/file/local;1"].createInstance(
                Ci.nsIFile
              );
              certfile.initWithPath(certfilename);
            } catch (e) {
              for (const dir of dirs) {
                certfile = dir.clone();
                certfile.append(
                  platform == "linux" ? "certificates" : "Certificates"
                );
                certfile.append(certfilename);
                if (certfile.exists()) {
                  break;
                }
              }
            }
            let file;
            try {
              file = await File.createFromNsIFile(certfile);
            } catch (e) {
              lazy.reportFailure(
                "Certificates",
                `Unable to find certificate - ${certfilename}`
              );
              continue;
            }
            const reader = new FileReader();
            reader.onloadend = function () {
              if (reader.readyState != reader.DONE) {
                lazy.reportFailure(
                  "Certificates",
                  `Unable to read certificate - ${certfile.path}`
                );
                return;
              }
              const certFile = reader.result;
              const certFileArray = [];
              for (let i = 0; i < certFile.length; i++) {
                certFileArray.push(certFile.charCodeAt(i));
              }
              let cert;
              try {
                cert = lazy.gCertDB.constructX509(certFileArray);
              } catch (e) {
                lazy.log.debug(
                  `constructX509 failed with error '${e}' - trying constructX509FromBase64.`
                );
                try {
                  // It might be PEM instead of DER.
                  cert = lazy.gCertDB.constructX509FromBase64(
                    lazy.pemToBase64(certFile)
                  );
                } catch (ex) {
                  lazy.reportFailure(
                    "Certificates",
                    `Unable to add certificate - ${certfile.path} - ${ex}`
                  );
                }
              }
              if (cert) {
                if (
                  lazy.gCertDB.isCertTrusted(
                    cert,
                    Ci.nsIX509Cert.CA_CERT,
                    Ci.nsIX509CertDB.TRUSTED_SSL
                  )
                ) {
                  // Certificate is already installed.
                  return;
                }
                try {
                  lazy.gCertDB.addCert(certFile, "CT,CT,");
                } catch (e) {
                  try {
                    // It might be PEM instead of DER.
                    lazy.gCertDB.addCertFromBase64(
                      lazy.pemToBase64(certFile),
                      "CT,CT,"
                    );
                  } catch (ex) {
                    lazy.reportFailure(
                      "Certificates",
                      `Unable to add certificate - ${certfile.path} - ${ex}`
                    );
                  }
                }
              }
            };
            reader.readAsBinaryString(file);
          }
        })().catch(e =>
          lazy.reportFailure(
            "Certificates",
            `Unable to import certificates - ${e}`
          )
        );
      }
    },
  },

  ClearOnShutdown: {
    onBeforeUIStartup(manager, param) {
      if (typeof param === "boolean") {
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.sanitize.sanitizeOnShutdown",
          param
        );
        for (const pref of Object.values(CLEAR_ON_SHUTDOWN_PREFS)) {
          lazy.PoliciesUtils.setAndLockPref(pref, param);
        }
        return;
      }

      // Named categories are enforced; the rest are left to the user.
      lazy.PoliciesUtils.setAndLockPref(
        "privacy.sanitize.sanitizeOnShutdown",
        true
      );
      for (const [member, pref] of Object.entries(CLEAR_ON_SHUTDOWN_PREFS)) {
        if (member in param) {
          lazy.PoliciesUtils.setAndLockPref(pref, param[member]);
        }
      }

      if (param.Exceptions) {
        lazy.addAllowDenyPermissions(
          "persist-data-on-shutdown",
          param.Exceptions
        );
      }
    },
  },

  CNSA2KeyAgreementEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("security.tls.enable_mlkem1024", param);
    },
  },

  Containers: {
    // Queried directly by ContextualIdentityService.sys.mjs
  },

  ContentAnalysis: {
    onBeforeAddons(manager, param) {
      // For security reasons, all of the Content Analysis related prefs should be locked in
      // this method, even if the values aren't specified in Enterprise Policies.
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "PipePathName",
        "browser.contentanalysis.pipe_path_name"
      );
      if ("AgentTimeout" in param) {
        if (!Number.isInteger(param.AgentTimeout)) {
          lazy.log.error(
            `Non-integer value for AgentTimeout: ${param.AgentTimeout}`
          );
        } else {
          lazy.PoliciesUtils.setAndLockPref(
            "browser.contentanalysis.agent_timeout",
            param.AgentTimeout
          );
        }
      } else {
        Services.prefs.lockPref("browser.contentanalysis.agent_timeout");
      }
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "AllowUrlRegexList",
        "browser.contentanalysis.allow_url_regex_list"
      );
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "DenyUrlRegexList",
        "browser.contentanalysis.deny_url_regex_list"
      );
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "AgentName",
        "browser.contentanalysis.agent_name"
      );
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "ClientSignature",
        "browser.contentanalysis.client_signature"
      );
      lazy.PoliciesUtils.setPrefIfPresentAndLock(
        param,
        "MaxConnectionsCount",
        "browser.contentanalysis.max_connections"
      );
      const resultPrefs = [
        ["DefaultResult", "default_result"],
        ["TimeoutResult", "timeout_result"],
      ];
      for (const pref of resultPrefs) {
        if (pref[0] in param) {
          if (
            !Number.isInteger(param[pref[0]]) ||
            param[pref[0]] < 0 ||
            param[pref[0]] > 2
          ) {
            lazy.log.error(
              `Non-integer or out of range value for ${pref[0]}: ${param[pref[0]]}`
            );
            Services.prefs.lockPref(`browser.contentanalysis.${pref[1]}`);
          } else {
            lazy.PoliciesUtils.setAndLockPref(
              `browser.contentanalysis.${pref[1]}`,
              param[pref[0]]
            );
          }
        } else {
          Services.prefs.lockPref(`browser.contentanalysis.${pref[1]}`);
        }
      }
      const boolPrefs = [
        ["IsPerUser", "is_per_user"],
        ["ShowBlockedResult", "show_blocked_result"],
        ["BypassForSameTabOperations", "bypass_for_same_tab_operations"],
      ];
      for (const pref of boolPrefs) {
        if (pref[0] in param) {
          lazy.PoliciesUtils.setAndLockPref(
            `browser.contentanalysis.${pref[1]}`,
            !!param[pref[0]]
          );
        } else {
          Services.prefs.lockPref(`browser.contentanalysis.${pref[1]}`);
        }
      }
      // The third element is the value to use when InterceptionPoints is
      // present but this interception point is not mentioned in it.  It is
      // true for everything that predates per-entry defaults; new
      // interception points that are off by default must use false here so
      // that policy files written before they existed don't silently opt in,
      // and similarly we should not change any of these values to avoid
      // changing existing clients.
      const interceptionPointPrefs = [
        ["Clipboard", "clipboard", true],
        ["ClipboardCopy", "clipboard_copy", false],
        ["Download", "download", true],
        ["DragAndDrop", "drag_and_drop", true],
        ["FileUpload", "file_upload", true],
        ["Print", "print", true],
      ];
      if ("InterceptionPoints" in param) {
        for (const pref of interceptionPointPrefs) {
          let value = pref[2];
          if (pref[0] in param.InterceptionPoints) {
            if ("Enabled" in param.InterceptionPoints[pref[0]]) {
              value = !!param.InterceptionPoints[pref[0]].Enabled;
            }
          }
          lazy.PoliciesUtils.setAndLockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.enabled`,
            value
          );
        }
      } else {
        for (const pref of interceptionPointPrefs) {
          Services.prefs.lockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.enabled`
          );
        }
      }
      const plainTextOnlyPrefs = [
        ["Clipboard", "clipboard"],
        ["ClipboardCopy", "clipboard_copy"],
        ["DragAndDrop", "drag_and_drop"],
      ];
      if ("InterceptionPoints" in param) {
        for (const pref of plainTextOnlyPrefs) {
          // Need to set and lock this value even if the enterprise
          // policy isn't set so users can't change it
          let value = true;
          if (pref[0] in param.InterceptionPoints) {
            if ("PlainTextOnly" in param.InterceptionPoints[pref[0]]) {
              value = !!param.InterceptionPoints[pref[0]].PlainTextOnly;
            }
          }
          lazy.PoliciesUtils.setAndLockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.plain_text_only`,
            value
          );
        }
      } else {
        for (const pref of plainTextOnlyPrefs) {
          Services.prefs.lockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.plain_text_only`
          );
        }
      }
      if ("Enabled" in param) {
        const enabled = !!param.Enabled;
        lazy.PoliciesUtils.setAndLockPref(
          "browser.contentanalysis.enabled",
          enabled
        );
        const ca = Cc["@mozilla.org/contentanalysis;1"].getService(
          Ci.nsIContentAnalysis
        );
        ca.isSetByEnterprisePolicy = true;
      } else {
        // Probably not strictly necessary, but let's lock everything
        // to be consistent.
        Services.prefs.lockPref("browser.contentanalysis.enabled");
      }
      // This will eventually be set by policy.
      lazy.PoliciesUtils.setAndLockPref(
        "browser.contentanalysis.use_wasm_backend",
        false
      );
      lazy.PoliciesUtils.setAndLockPref(
        "browser.contentanalysis.wasm_module_extension_require_signature",
        true
      );
    },
  },

  Cookies: {
    onBeforeUIStartup(manager, param) {
      lazy.addAllowDenyPermissions("cookie", param.Allow, param.Block);

      // Backwards-compat shim (Bug 2051574): before Bug 1767271, Cookies.Allow
      // doubled as the clear-on-shutdown exception list. Sites are now exempted
      // via the dedicated SanitizeOnShutdown.Exceptions key. If an admin hasn't
      // adopted that key yet, treat Cookies.Allow entries as shutdown exceptions
      // too. Remove this shim once admins have had a couple of releases to
      // migrate.
      if (
        param.Allow?.length &&
        !manager.getActivePolicies()?.SanitizeOnShutdown?.Exceptions?.length
      ) {
        lazy.log.warn(
          "Using Cookies.Allow to exempt sites from clear-on-shutdown is " +
            "deprecated and will stop working in a future release. Use the " +
            "SanitizeOnShutdown.Exceptions policy instead."
        );
        lazy.addAllowDenyPermissions("persist-data-on-shutdown", param.Allow);
      }

      if (param.AllowSession) {
        for (const origin of param.AllowSession) {
          try {
            lazy.addPolicyPermission(
              origin,
              "cookie",
              Ci.nsICookiePermission.ACCESS_SESSION
            );
          } catch (ex) {
            lazy.reportFailure(
              "Cookies",
              `Unable to add cookie session permission - ${origin.href}`
            );
          }
        }
      }

      lazy.clearRunOnceModification("clearCookiesForBlockedHosts");
      for (const { hostname } of param.Block ?? []) {
        try {
          for (const cookie of Services.cookies.getCookiesWithOriginAttributes(
            "{}",
            hostname
          )) {
            if (
              cookie.rawHost == hostname ||
              cookie.rawHost.endsWith(`.${hostname}`)
            ) {
              Services.cookies.remove(
                cookie.host,
                cookie.name,
                cookie.path,
                cookie.originAttributes
              );
            }
          }
        } catch (ex) {
          lazy.reportFailure(
            "Cookies",
            `Unable to clear cookies for blocked host - ${hostname}`
          );
        }
      }

      if (param.ExpireAtSessionEnd != undefined) {
        lazy.log.error(
          "'ExpireAtSessionEnd' has been deprecated and it has no effect anymore."
        );
      }

      // New Cookie Behavior option takes precendence
      const defaultPref = Services.prefs.getDefaultBranch("");
      let newCookieBehavior = defaultPref.getIntPref(
        "network.cookie.cookieBehavior"
      );
      let newCookieBehaviorPB = defaultPref.getIntPref(
        "network.cookie.cookieBehavior.pbmode"
      );
      if ("Behavior" in param || "BehaviorPrivateBrowsing" in param) {
        const behaviors = {
          accept: Ci.nsICookieService.BEHAVIOR_ACCEPT,
          "reject-foreign": Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN,
          reject: Ci.nsICookieService.BEHAVIOR_REJECT,
          "limit-foreign": Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN,
          "reject-tracker": Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
          "reject-tracker-and-partition-foreign":
            Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
          "partition-foreign": Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
        };
        if ("Behavior" in param) {
          newCookieBehavior = behaviors[param.Behavior];
        }
        if ("BehaviorPrivateBrowsing" in param) {
          newCookieBehaviorPB = behaviors[param.BehaviorPrivateBrowsing];
        }
      } else {
        // Default, AcceptThirdParty, and RejectTracker are being
        // deprecated in favor of Behavior. They will continue
        // to be supported, though.
        if (
          param.Default !== undefined ||
          param.AcceptThirdParty !== undefined ||
          param.RejectTracker !== undefined ||
          param.Locked
        ) {
          newCookieBehavior = Ci.nsICookieService.BEHAVIOR_ACCEPT;
          if (param.Default !== undefined && !param.Default) {
            newCookieBehavior = Ci.nsICookieService.BEHAVIOR_REJECT;
          } else if (param.AcceptThirdParty) {
            if (param.AcceptThirdParty == "never") {
              newCookieBehavior = Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN;
            } else if (param.AcceptThirdParty == "from-visited") {
              newCookieBehavior = Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN;
            }
          } else if (param.RejectTracker) {
            newCookieBehavior = Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER;
          }
        }
        // With the old cookie policy, we made private browsing the same.
        newCookieBehaviorPB = newCookieBehavior;
      }
      // We set the values no matter what just in case the policy was only used to lock.
      lazy.PoliciesUtils.setDefaultPref(
        "network.cookie.cookieBehavior",
        newCookieBehavior,
        param.Locked
      );
      lazy.PoliciesUtils.setDefaultPref(
        "network.cookie.cookieBehavior.pbmode",
        newCookieBehaviorPB,
        param.Locked
      );
    },
  },

  DefaultBrowserSettingEnabled: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref(
          "browser.shell.checkDefaultBrowser",
          true
        );
      } else {
        manager.disallowFeature("setDefaultBrowser");
        lazy.PoliciesUtils.setAndLockPref(
          "browser.shell.checkDefaultBrowser",
          false
        );
      }
    },
  },

  DefaultDownloadDirectory: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setDefaultPref(
        "browser.download.dir",
        lazy.replacePathVariables(param)
      );
    },
  },

  DefaultSerialGuardSetting: {
    onBeforeAddons(manager, param) {
      if (!Number.isInteger(param)) {
        lazy.log.error(
          `Non-integer value for DefaultSerialGuardSetting: ${param}`
        );
        return;
      }
      if (param == 3) {
        // allow access to serial ports, but don't lock the
        // pref so it can be manually disabled
        lazy.PoliciesUtils.setDefaultPref("dom.webserial.enabled", true, false);
      } else if (param == 2) {
        // do not allow access to serial ports
        lazy.PoliciesUtils.setAndLockPref("dom.webserial.enabled", false);
      } else {
        lazy.log.error(
          `Unrecognized value for DefaultSerialGuardSetting: ${param}`
        );
      }
    },
  },

  DisableAccounts: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref("identity.fxaccounts.enabled", false);
        lazy.PoliciesUtils.setAndLockPref(
          "browser.aboutwelcome.enabled",
          false
        );
      }
    },
  },

  DisableAppUpdate: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("appUpdate");
      }
    },
  },

  DisableBuiltinPDFViewer: {
    onBeforeAddons(manager, param) {
      const policies = Services.policies.getActivePolicies();
      if (
        policies.Handlers?.mimeTypes?.["application/pdf"] ||
        policies.Handlers?.extensions?.pdf
      ) {
        // If there is an existing Handlers policy modifying PDF behavior,
        // don't do anything.
        return;
      }
      if (!param) {
        // Ensure PDF.js is not blocked by the pref (no UI exists for this pref).
        Services.prefs.clearUserPref("pdfjs.disabled");
        // Only set handleInternally once per policy value; don't override the
        // user's handler choice on every subsequent startup.
        lazy.runOncePerModification("disableBuiltinPDFViewer", "false", () => {
          const pdfMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
            "application/pdf",
            "pdf"
          );
          lazy.processMIMEInfo(
            { action: "handleInternally" },
            pdfMIMEInfo,
            "DisableBuiltinPDFViewer"
          );
        });
        return;
      }
      const pdfMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
        "application/pdf",
        "pdf"
      );
      lazy.processMIMEInfo(
        { action: "useSystemDefault" },
        pdfMIMEInfo,
        "DisableBuiltinPDFViewer"
      );
    },
  },

  DisabledCiphers: {
    onBeforeAddons(manager, param) {
      const cipherPrefs = {
        TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
          "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256",
        TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
          "security.ssl3.ecdhe_ecdsa_aes_128_gcm_sha256",
        TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256:
          "security.ssl3.ecdhe_ecdsa_chacha20_poly1305_sha256",
        TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
          "security.ssl3.ecdhe_rsa_chacha20_poly1305_sha256",
        TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
          "security.ssl3.ecdhe_ecdsa_aes_256_gcm_sha384",
        TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
          "security.ssl3.ecdhe_rsa_aes_256_gcm_sha384",
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA:
          "security.ssl3.ecdhe_rsa_aes_128_sha",
        TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA:
          "security.ssl3.ecdhe_ecdsa_aes_128_sha",
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
          "security.ssl3.ecdhe_rsa_aes_256_sha",
        TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
          "security.ssl3.ecdhe_ecdsa_aes_256_sha",
        TLS_DHE_RSA_WITH_AES_128_CBC_SHA: "security.ssl3.dhe_rsa_aes_128_sha",
        TLS_DHE_RSA_WITH_AES_256_CBC_SHA: "security.ssl3.dhe_rsa_aes_256_sha",
        TLS_RSA_WITH_AES_128_GCM_SHA256: "security.ssl3.rsa_aes_128_gcm_sha256",
        TLS_RSA_WITH_AES_256_GCM_SHA384: "security.ssl3.rsa_aes_256_gcm_sha384",
        TLS_RSA_WITH_AES_128_CBC_SHA: "security.ssl3.rsa_aes_128_sha",
        TLS_RSA_WITH_AES_256_CBC_SHA: "security.ssl3.rsa_aes_256_sha",
        TLS_RSA_WITH_3DES_EDE_CBC_SHA:
          "security.ssl3.deprecated.rsa_des_ede3_sha",
        TLS_CHACHA20_POLY1305_SHA256: "security.tls13.chacha20_poly1305_sha256",
        TLS_AES_128_GCM_SHA256: "security.tls13.aes_128_gcm_sha256",
        TLS_AES_256_GCM_SHA384: "security.tls13.aes_256_gcm_sha384",
      };

      for (const cipher in param) {
        lazy.PoliciesUtils.setAndLockPref(cipherPrefs[cipher], !param[cipher]);
      }
    },
  },

  DisableDefaultBrowserAgent: {
    // The implementation of this policy is in the default browser agent itself
    // (/toolkit/mozapps/defaultagent); we need an entry for it here so that it
    // shows up in about:policies as a real policy and not as an error.
  },

  DisableDeveloperTools: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref("devtools.policy.disabled", true);
        lazy.PoliciesUtils.setAndLockPref("devtools.chrome.enabled", false);
        // Covers Marionette and the Remote Agent.
        lazy.PoliciesUtils.setAndLockPref("remote.policy.disabled", true);

        manager.disallowFeature("devtools");
        lazy.blockAboutPage(manager, "about:debugging");
        lazy.blockAboutPage(manager, "about:devtools-toolbox");
        lazy.blockAboutPage(manager, "about:profiling");
      }
    },
  },

  DisableEncryptedClientHello: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref(
          "network.dns.echconfig.enabled",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "network.dns.http3_echconfig.enabled",
          false
        );
      }
    },
  },

  DisableFeedbackCommands: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("feedbackCommands");
      }
    },
  },

  DisableFirefoxAccounts: {
    onBeforeAddons(manager, param) {
      // If DisableAccounts is set, let it take precedence.
      if ("DisableAccounts" in manager.getActivePolicies()) {
        return;
      }

      if (param) {
        lazy.PoliciesUtils.setAndLockPref("identity.fxaccounts.enabled", false);
        lazy.PoliciesUtils.setAndLockPref(
          "browser.aboutwelcome.enabled",
          false
        );
      }
    },
  },

  DisableFirefoxScreenshots: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref(
          "screenshots.browser.component.enabled",
          false
        );
      }
    },
  },

  DisableFirefoxStudies: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("Shield");
        lazy.PoliciesUtils.setAndLockPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
          false
        );
      }
    },
  },

  DisableForgetButton: {
    onProfileAfterChange(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref("privacy.panicButton.enabled", false);
      }
    },
  },

  DisableFormHistory: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref("browser.formfill.enable", false);
      }
    },
  },

  DisableLaunchOnLogin: {
    onBeforeAddons(manager, param) {
      if (!param) {
        return;
      }
      manager.disallowFeature("launchOnLogin");
      lazy.PoliciesUtils.setAndLockPref(
        "browser.startup.windowsLaunchOnLogin.enabled",
        false
      );
      lazy.LaunchOnLogin.disable();
    },
  },

  DisableMacOSUserPolicies: {},

  DisableMasterPasswordCreation: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("createMasterPassword");
      }
    },
  },

  DisablePasswordReveal: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("passwordReveal");
        lazy.PoliciesUtils.setAndLockPref(
          "layout.forms.reveal-password-button.enabled",
          false
        );
      }
    },
  },

  DisablePrivateBrowsing: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("privatebrowsing");
        lazy.blockAboutPage(manager, "about:privatebrowsing", true);
        lazy.PoliciesUtils.setAndLockPref(
          "browser.privatebrowsing.autostart",
          false
        );
      }
    },
  },

  DisableProfileImport: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("profileImport");
        lazy.PoliciesUtils.setAndLockPref(
          "browser.newtabpage.activity-stream.migrationExpired",
          true
        );
      }
    },
  },

  DisableProfileRefresh: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("profileRefresh");
        lazy.PoliciesUtils.setAndLockPref("browser.disableResetPrompt", true);
      }
    },
  },

  DisableRemoteImprovements: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("NimbusRollouts");
      }
    },
  },

  DisableRemoteSettingsAndAcceptSecurityConsequences: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("remoteSettings");
      }
    },
  },

  DisableSafeMode: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("safeMode");
      }
    },
  },

  DisableSecurityBypass: {
    onBeforeUIStartup(manager, param) {
      if ("InvalidCertificate" in param) {
        lazy.PoliciesUtils.setAndLockPref(
          "security.certerror.hideAddException",
          param.InvalidCertificate
        );
      }

      if ("SafeBrowsing" in param) {
        lazy.PoliciesUtils.setAndLockPref(
          "browser.safebrowsing.allowOverride",
          !param.SafeBrowsing
        );
      }
    },
  },

  DisableSetDesktopBackground: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("setDesktopBackground");
      }
    },
  },

  DisableSystemAddonUpdate: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("SysAddonUpdate");
      }
    },
  },

  DisableTelemetry: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref(
          "datareporting.healthreport.uploadEnabled",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "datareporting.policy.dataSubmissionEnabled",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "toolkit.telemetry.archive.enabled",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "datareporting.usage.uploadEnabled",
          false
        );
        lazy.blockAboutPage(manager, "about:telemetry");
      }
    },
  },

  DisableThirdPartyModuleBlocking: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("thirdPartyModuleBlocking");
      }
    },
  },

  DisplayBookmarksToolbar: {
    onBeforeUIStartup(manager, param) {
      let visibility;
      if (typeof param === "boolean") {
        visibility = param ? "always" : "newtab";
      } else {
        visibility = param;
      }
      // This policy is meant to change the default behavior, not to force it.
      // If this policy was already applied and the user chose to re-hide the
      // bookmarks toolbar, do not show it again.
      lazy.runOncePerModification("displayBookmarksToolbar", visibility, () => {
        const visibilityPref = "browser.toolbars.bookmarks.visibility";
        Services.prefs.setCharPref(visibilityPref, visibility);
      });
    },
  },

  DisplayMenuBar: {
    onBeforeUIStartup(manager, param) {
      let value;
      if (
        typeof param === "boolean" ||
        param == "default-on" ||
        param == "default-off"
      ) {
        switch (param) {
          case "default-on":
            value = false;
            break;
          case "default-off":
            value = true;
            break;
          default:
            value = !param;
            break;
        }
        // This policy is meant to change the default behavior, not to force it.
        // If this policy was already applied and the user chose to re-hide the
        // menu bar, do not show it again.
        lazy.runOncePerModification("displayMenuBar", String(value), () => {
          Services.xulStore.setValue(
            BROWSER_DOCUMENT_URL,
            "toolbar-menubar",
            "autohide",
            value ? "" : "-moz-missing\n"
          );
        });
      } else {
        switch (param) {
          case "always":
            value = false;
            break;
          case "never":
            // Make sure Alt key doesn't show the menubar
            lazy.PoliciesUtils.setAndLockPref(
              "ui.key.menuAccessKeyFocuses",
              false
            );
            value = true;
            break;
        }
        Services.xulStore.setValue(
          BROWSER_DOCUMENT_URL,
          "toolbar-menubar",
          "autohide",
          value ? "" : "-moz-missing\n"
        );
        manager.disallowFeature("hideShowMenuBar");
      }
    },
  },

  DNSOverHTTPS: {
    onBeforeAddons(manager, param) {
      if ("Enabled" in param) {
        let mode = param.Enabled ? 2 : 5;
        // Fallback only matters if DOH is enabled.
        if (param.Fallback === false) {
          mode = 3;
        }
        lazy.PoliciesUtils.setDefaultPref(
          "network.trr.mode",
          mode,
          param.Locked
        );
      }
      if ("ProviderURL" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.trr.uri",
          param.ProviderURL.href,
          param.Locked
        );
      }
      if ("ExcludedDomains" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.trr.excluded-domains",
          param.ExcludedDomains.join(","),
          param.Locked
        );
      }
    },
  },

  DontCheckDefaultBrowser: {
    onBeforeUIStartup(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.shell.checkDefaultBrowser",
        !param
      );
    },
  },

  DownloadDirectory: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.download.dir",
        lazy.replacePathVariables(param)
      );
      // If a custom download directory is being used, just lock folder list to 2.
      lazy.PoliciesUtils.setAndLockPref("browser.download.folderList", 2);
      // Per Chrome spec, user can't choose to download every time
      // if this is set.
      lazy.PoliciesUtils.setAndLockPref(
        "browser.download.useDownloadDir",
        true
      );
    },
  },

  EnableTrackingProtection: {
    onAllWindowsRestored(manager, param) {
      if (param.Category) {
        // browser.contentblocking.category only works as a default pref if
        // it is locked.
        lazy.PoliciesUtils.setDefaultPref(
          "browser.contentblocking.category",
          param.Category,
          true
        );
        const { ContentBlockingPrefs } = ChromeUtils.importESModule(
          "moz-src:///browser/components/protections/ContentBlockingPrefs.sys.mjs"
        );
        // These are always locked because they would reset at
        // startup anyway.
        ContentBlockingPrefs.setPrefsToCategory(
          param.Category,
          true, // locked
          false // preserveAllowListSettings
        );
        ContentBlockingPrefs.matchCBCategory();
        // We don't want to lock the new exceptions UI unless
        // that policy was explicitly set.
        if (param.Category == "strict" && !param.Locked) {
          Services.prefs.unlockPref(
            "privacy.trackingprotection.allow_list.baseline.enabled"
          );
          Services.prefs.unlockPref(
            "privacy.trackingprotection.allow_list.convenience.enabled"
          );
        }
      }
    },
    onBeforeUIStartup(manager, param) {
      if ("Exceptions" in param) {
        lazy.addAllowDenyPermissions("trackingprotection", param.Exceptions);
      }
      if ("BaselineExceptions" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.allow_list.baseline.enabled",
          param.BaselineExceptions,
          param.Locked
        );
      }
      if ("ConvenienceExceptions" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.allow_list.convenience.enabled",
          param.ConvenienceExceptions,
          param.Locked
        );
      }
      if (param.Category) {
        // If a category is set, we ignore everything except exceptions
        // and the allow lists.
        return;
      }
      if (param.Value) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.enabled",
          true,
          param.Locked
        );
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.pbmode.enabled",
          true,
          param.Locked
        );
      } else {
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.trackingprotection.enabled",
          false
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.trackingprotection.pbmode.enabled",
          false
        );
      }
      if ("Cryptomining" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.cryptomining.enabled",
          param.Cryptomining,
          param.Locked
        );
      }
      if ("Fingerprinting" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.fingerprinting.enabled",
          param.Fingerprinting,
          param.Locked
        );
      }
      if ("EmailTracking" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.emailtracking.enabled",
          param.EmailTracking,
          param.Locked
        );
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.emailtracking.pbmode.enabled",
          param.EmailTracking,
          param.Locked
        );
      }
      if ("SuspectedFingerprinting" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.fingerprintingProtection",
          param.SuspectedFingerprinting,
          param.Locked
        );
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.fingerprintingProtection.pbmode",
          param.SuspectedFingerprinting,
          param.Locked
        );
      }
    },
  },

  EncryptedMediaExtensions: {
    onBeforeAddons(manager, param) {
      if ("Enabled" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "media.eme.enabled",
          param.Enabled,
          param.Locked
        );
      }
    },
  },

  ExemptDomainFileTypePairsFromFileTypeDownloadWarnings: {
    // This policy is handled directly in EnterprisePoliciesParent.sys.mjs
    // and requires no validation (It's done by the schema).
  },

  Extensions: {
    onBeforeUIStartup(manager, param) {
      let uninstallingPromise = Promise.resolve(false);
      let installingPromise = Promise.resolve();
      if ("Uninstall" in param) {
        uninstallingPromise = lazy.uninstallListedAddons(
          param.Uninstall,
          param.Install
        );
      }
      if ("Install" in param) {
        installingPromise = uninstallingPromise.then(uninstallListChanged => {
          if (uninstallListChanged) {
            // Re-run the install even if its list hasn't changed, which is how
            // an add-on listed in both Uninstall and Install gets updated.
            lazy.clearRunOnceModification("extensionsInstall");
          }
          return lazy.runOncePerModification(
            "extensionsInstall",
            JSON.stringify(param.Install),
            () => {
              for (const location of param.Install) {
                let uri;
                try {
                  // We need to try as a file first because
                  // Windows paths are valid URIs.
                  // This is done for legacy support (old API)
                  const xpiFile = new lazy.FileUtils.File(location);
                  uri = Services.io.newFileURI(xpiFile);
                } catch (e) {
                  try {
                    uri = Services.io.newURI(location);
                  } catch (ex) {
                    // Keep going so that one bad location doesn't discard the
                    // add-ons that come after it.
                    lazy.reportFailure(
                      "Extensions",
                      `Invalid add-on location (${location})`
                    );
                    continue;
                  }
                }
                lazy.installAddonFromURL(uri.spec, {}, "Extensions");
              }
            }
          );
        });
      }
      if ("Locked" in param) {
        for (const ID of param.Locked) {
          manager.disallowFeature(`uninstall-extension:${ID}`);
          manager.disallowFeature(`disable-extension:${ID}`);
        }
      }
      // Returned so that the engine can report a failure of the
      // uninstall/install steps against this policy.
      return Promise.all([uninstallingPromise, installingPromise]);
    },
  },

  ExtensionSettings: {
    onBeforeAddons(manager, param) {
      lazy.discardAMOUpdateURLs(param, "ExtensionSettings");
      try {
        manager.setExtensionSettings(param);
      } catch (e) {
        lazy.reportFailure(
          "ExtensionSettings",
          `Some ExtensionSettings could not be applied: ${e.message}`
        );
      }
      try {
        lazy.applyExtensionGuards(param);
      } catch (e) {
        lazy.reportFailure(
          "ExtensionSettings",
          `Invalid runtime_blocked_hosts/runtime_allowed_hosts in ` +
            `ExtensionSettings: ${e.message}`
        );
      }
    },
    async onBeforeUIStartup(manager, param) {
      const extensionSettings = param;
      let blockAllExtensions = false;
      if ("*" in extensionSettings) {
        if (
          "installation_mode" in extensionSettings["*"] &&
          extensionSettings["*"].installation_mode == "blocked"
        ) {
          blockAllExtensions = true;
          // Turn off discovery pane in about:addons
          lazy.PoliciesUtils.setAndLockPref(
            "extensions.getAddons.showPane",
            false
          );
          // Turn off recommendations
          lazy.PoliciesUtils.setAndLockPref(
            "extensions.htmlaboutaddons.recommendations.enabled",
            false
          );
          manager.disallowFeature("installTemporaryAddon");
        }
        if ("restricted_domains" in extensionSettings["*"]) {
          const restrictedDomains = Services.prefs
            .getCharPref("extensions.webextensions.restrictedDomains")
            .split(",");
          lazy.PoliciesUtils.setAndLockPref(
            "extensions.webextensions.restrictedDomains",
            restrictedDomains
              .concat(extensionSettings["*"].restricted_domains)
              .join(",")
          );
        }
      }
      const addons = new Map();
      for (const a of await lazy.AddonManager.getAllAddons()) {
        addons.set(a.id, a);
      }
      const allowedExtensions = [];
      for (const extensionID in extensionSettings) {
        if (extensionID == "*") {
          // Ignore global settings
          continue;
        }
        if ("installation_mode" in extensionSettings[extensionID]) {
          if (
            extensionSettings[extensionID].installation_mode ==
              "force_installed" ||
            extensionSettings[extensionID].installation_mode ==
              "normal_installed"
          ) {
            const existingAddon = addons.get(extensionID);
            if (extensionSettings[extensionID].install_url) {
              lazy.installAddonFromURL(
                extensionSettings[extensionID].install_url,
                {
                  id: extensionID,
                  existingAddon,
                },
                "ExtensionSettings"
              );
            } else if (!existingAddon) {
              // An unusable update_url is an error, not a reason to install a
              // different build of the add-on from AMO.
              if (extensionSettings[extensionID].update_url) {
                lazy.installAddonFromUpdateURL(
                  extensionSettings[extensionID].update_url,
                  extensionID,
                  "ExtensionSettings"
                );
              } else {
                lazy.installAddonFromRepository(
                  extensionID,
                  "ExtensionSettings"
                );
              }
            }
            manager.disallowFeature(`uninstall-extension:${extensionID}`);
            if (
              extensionSettings[extensionID].installation_mode ==
              "force_installed"
            ) {
              manager.disallowFeature(`disable-extension:${extensionID}`);
            }
            allowedExtensions.push(extensionID);
          } else if (
            extensionSettings[extensionID].installation_mode == "allowed"
          ) {
            allowedExtensions.push(extensionID);
          } else if (
            extensionSettings[extensionID].installation_mode == "blocked"
          ) {
            if (addons.has(extensionID)) {
              // Can't use the addon from getActiveAddons since it doesn't have uninstall.
              const addon = await lazy.AddonManager.getAddonByID(extensionID);
              try {
                await addon.uninstall();
                addons.delete(extensionID);
              } catch (e) {
                // This can fail for add-ons that can't be uninstalled.
                lazy.log.debug(
                  `Add-on ID (${addon.id}) couldn't be uninstalled.`
                );
              }
            }
          }
        }
      }
      const allowedTypes = extensionSettings["*"]?.allowed_types;
      if (blockAllExtensions || allowedTypes) {
        for (const addon of addons.values()) {
          if (
            addon.isSystem ||
            addon.isBuiltin ||
            !(addon.scope & lazy.AddonManager.SCOPE_PROFILE)
          ) {
            continue;
          }
          // Match Chrome: any per-id ExtensionSettings entry (even empty)
          // shadows the "*" defaults entirely, so an addon with its own
          // entry is exempt from blockAllExtensions.
          if (
            !allowedExtensions.includes(addon.id) &&
            !(blockAllExtensions && addon.id in extensionSettings) &&
            (blockAllExtensions || !allowedTypes.includes(addon.type))
          ) {
            try {
              // Can't use the addon from getActiveAddons since it doesn't have uninstall.
              const addonToUninstall = await lazy.AddonManager.getAddonByID(
                addon.id
              );
              await addonToUninstall.uninstall();
              addons.delete(addon.id);
            } catch (e) {
              // This can fail for add-ons that can't be uninstalled.
              lazy.log.debug(
                `Add-on ID (${addon.id}) couldn't be uninstalled.`
              );
            }
          }
        }
      }

      // Revoke any granted optional permissions that are now blocked. The
      // appDisabled refresh below handles addons whose required permissions
      // are blocked (via mayInstallAddon -> isUsableAddon).
      for (const addon of addons.values()) {
        if (
          addon.isSystem ||
          addon.isBuiltin ||
          !(addon.scope & lazy.AddonManager.SCOPE_PROFILE)
        ) {
          continue;
        }
        const blockedPerms =
          Services.policies.getExtensionSettings(addon.id)
            ?.blocked_permissions ?? [];
        if (!blockedPerms.length) {
          continue;
        }
        try {
          const granted = await lazy.ExtensionPermissions.get(addon.id);
          const toRemove = granted.permissions.filter(perm =>
            blockedPerms.includes(perm)
          );
          if (toRemove.length) {
            const extension = WebExtensionPolicy.getByID(addon.id)?.extension;
            await lazy.ExtensionPermissions.remove(
              addon.id,
              { permissions: toRemove, origins: [], data_collection: [] },
              extension
            );
          }
        } catch (e) {
          lazy.log.debug(
            `Could not revoke blocked optional permissions for ${addon.id}: ${e}`
          );
        }
      }
      // Recompute appDisabled across all addons against the new policy. This
      // catches addons whose required permissions are now blocked (via
      // mayInstallAddon) without persisting userDisabled, so an update that
      // drops the blocked permission re-enables the addon automatically.
      lazy.AddonManagerPrivate.updateAddonAppDisabledStates();
    },
  },

  ExtensionUpdate: {
    onBeforeAddons(manager, param) {
      if (!param) {
        lazy.PoliciesUtils.setAndLockPref("extensions.update.enabled", param);
      }
    },
  },

  FirefoxHome: {
    onBeforeAddons(manager, param) {
      if ("Search" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSearch",
          param.Search,
          param.Locked
        );
      }
      if ("Weather" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showWeather",
          param.Weather,
          param.Locked
        );
        // The Nova layout (enabled by default) binds the weather toggle to a
        // different pref, so set it too.
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.widgets.weather.enabled",
          param.Weather,
          param.Locked
        );
      }
      if ("TopSites" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.topsites",
          param.TopSites,
          param.Locked
        );
      }
      if ("SponsoredTopSites" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsoredTopSites",
          param.SponsoredTopSites,
          param.Locked
        );
      }
      if ("Highlights" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.highlights",
          param.Highlights,
          param.Locked
        );
      }
      if ("Pocket" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.topstories",
          param.Pocket,
          param.Locked
        );
      }
      if ("Stories" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.topstories",
          param.Stories,
          param.Locked
        );
      }
      if ("SponsoredPocket" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsored",
          param.SponsoredPocket,
          param.Locked
        );
      }
      if ("SponsoredStories" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsored",
          param.SponsoredStories,
          param.Locked
        );
      }
      // The "Support Firefox" toggle in the settings UI is a parent control for
      // the two sponsored child settings. When both are locked by policy, lock
      // it to their combined value so it can't be toggled to no effect.
      if (
        param.Locked &&
        "SponsoredTopSites" in param &&
        "SponsoredStories" in param
      ) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsoredCheckboxes",
          param.SponsoredTopSites || param.SponsoredStories,
          param.Locked
        );
      }
      if (param.Widgets) {
        if ("Enabled" in param.Widgets) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.newtabpage.activity-stream.widgets.enabled",
            param.Widgets.Enabled,
            param.Locked
          );
        }
        // Blocked always locks, regardless of the Locked property: the lock is
        // what blocks the widget, and it is enough on its own whatever the
        // widget's system pref is set to. IDs aren't validated, so one policy
        // file stays valid across versions that add or remove widgets.
        for (const id of param.Widgets.Blocked ?? []) {
          lazy.PoliciesUtils.setDefaultPref(
            `browser.newtabpage.activity-stream.widgets.${id}.enabled`,
            false,
            true
          );
        }
      }
    },
  },

  FirefoxSuggest: {
    onBeforeAddons(manager, param) {
      (async () => {
        await lazy.QuickSuggest.initPromise;
        if ("WebSuggestions" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.urlbar.suggest.quicksuggest.all",
            param.WebSuggestions,
            param.Locked
          );
        }
        if ("SponsoredSuggestions" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.urlbar.suggest.quicksuggest.sponsored",
            param.SponsoredSuggestions,
            param.Locked
          );
        }
        // `ImproveSuggest` is deprecated and replaced with `OnlineEnabled`.
        if ("OnlineEnabled" in param || "ImproveSuggest" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "browser.urlbar.quicksuggest.online.enabled",
            param.OnlineEnabled ?? param.ImproveSuggest,
            param.Locked
          );
        }
      })();
    },
  },

  GenerativeAI: {
    onBeforeAddons(manager, param) {
      const policies = Services.policies.getActivePolicies();
      if (policies.AIControls) {
        lazy.log.warn("Ignoring GenerativeAI policy in favor of AIControls");
        return;
      }

      const defaultValue = "Enabled" in param ? param.Enabled : undefined;

      const features = [
        [
          "Chatbot",
          ["browser.ml.chat.enabled", "browser.ml.chat.page"],
          "browser.ai.control.sidebarChatbot",
        ],
        [
          "LinkPreviews",
          ["browser.ml.linkPreview.optin"],
          "browser.ai.control.linkPreviewKeyPoints",
        ],
        [
          "SmartWindow",
          ["browser.smartwindow.enabled"],
          "browser.ai.control.smartWindow",
        ],
        [
          "TabGroups",
          ["browser.tabs.groups.smart.userEnabled"],
          "browser.ai.control.smartTabGroups",
        ],
      ];

      for (const [key, prefs, aiControlPref] of features) {
        const value = key in param ? param[key] : defaultValue;
        if (value !== undefined) {
          for (const pref of prefs) {
            lazy.PoliciesUtils.setDefaultPref(pref, value, param.Locked);
          }
          lazy.PoliciesUtils.setDefaultPref(
            aiControlPref,
            value ? "enabled" : "blocked",
            param.Locked
          );
        }
      }
    },
  },

  GoToIntranetSiteForSingleWordEntryInAddressBar: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.fixup.dns_first_for_single_words",
        param
      );
    },
  },

  Handlers: {
    onBeforeAddons(manager, param) {
      if ("mimeTypes" in param) {
        for (const mimeType in param.mimeTypes) {
          const mimeInfo = param.mimeTypes[mimeType];
          if (!mimeType) {
            lazy.reportFailure("Handlers", "Invalid MIME type (empty)");
            continue;
          }
          try {
            const realMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
              mimeType,
              ""
            );
            lazy.processMIMEInfo(mimeInfo, realMIMEInfo, "Handlers");
          } catch (e) {
            lazy.reportFailure(
              "Handlers",
              `Invalid MIME type (${mimeType}): ${e}`
            );
          }
        }
      }
      if ("extensions" in param) {
        for (const extension in param.extensions) {
          const mimeInfo = param.extensions[extension];
          if (!extension) {
            lazy.reportFailure("Handlers", "Invalid file extension (empty)");
            continue;
          }
          try {
            const realMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
              "",
              extension
            );
            lazy.processMIMEInfo(mimeInfo, realMIMEInfo, "Handlers");
          } catch (e) {
            lazy.reportFailure(
              "Handlers",
              `Invalid file extension (${extension}): ${e}`
            );
          }
        }
      }
      if ("schemes" in param) {
        for (const scheme in param.schemes) {
          const handlerInfo = param.schemes[scheme];
          if (!scheme) {
            lazy.reportFailure("Handlers", "Invalid scheme (empty)");
            continue;
          }
          try {
            const realHandlerInfo =
              lazy.gExternalProtocolService.getProtocolHandlerInfo(scheme);
            lazy.processMIMEInfo(handlerInfo, realHandlerInfo, "Handlers");
          } catch (e) {
            lazy.reportFailure("Handlers", `Invalid scheme (${scheme}): ${e}`);
          }
        }
      }
    },
  },

  HardwareAcceleration: {
    onBeforeAddons(manager, param) {
      if (!param) {
        lazy.PoliciesUtils.setAndLockPref("layers.acceleration.disabled", true);
      }
    },
  },

  Homepage: {
    // People set URL to a pipe-separated list, the way
    // |browser.startup.homepage| stores it. Additional does the same thing,
    // so move the extra ones there.
    migrateLegacySyntax(param) {
      if (typeof param?.URL != "string" || !param.URL.includes("|")) {
        return param;
      }
      const [url, ...additional] = lazy.HomePage.parseCustomHomepageURLs(
        param.URL
      );
      if (!url) {
        return param;
      }
      return {
        ...param,
        URL: url,
        Additional: [...additional, ...(param.Additional ?? [])],
      };
    },

    onBeforeUIStartup(manager, param) {
      if ("StartPage" in param && param.StartPage == "none") {
        // For blank startpage, we use about:blank rather
        // than messing with browser.startup.page
        param.URL = new URL("about:blank");
      }
      // |homepages| will be a string containing a pipe-separated ('|') list of
      // URLs because that is what the "Home page" section of about:preferences
      // (and therefore what the pref |browser.startup.homepage|) accepts.
      if ("URL" in param) {
        let homepages = param.URL.href;
        if (param.Additional && param.Additional.length) {
          homepages += "|" + param.Additional.map(url => url.href).join("|");
        }
        lazy.PoliciesUtils.setDefaultPref(
          "browser.startup.homepage",
          homepages,
          param.Locked
        );
        if (param.Locked) {
          lazy.PoliciesUtils.setAndLockPref(
            "pref.browser.homepage.disable_button.current_page",
            true
          );
          lazy.PoliciesUtils.setAndLockPref(
            "pref.browser.homepage.disable_button.bookmark_page",
            true
          );
          lazy.PoliciesUtils.setAndLockPref(
            "pref.browser.homepage.disable_button.restore_default",
            true
          );
        } else {
          // Clear out old run once modification that is no longer used.
          lazy.clearRunOnceModification("setHomepage");
        }
        // If a homepage has been set via policy, show the home button
        if (param.URL != "about:blank") {
          manager.disallowFeature("removeHomeButtonByDefault");
        }
      }
      if (param.StartPage) {
        let prefValue;
        switch (param.StartPage) {
          case "homepage":
          case "homepage-locked":
          case "none":
            prefValue = 1;
            break;
          case "previous-session":
            prefValue = 3;
            break;
        }
        lazy.PoliciesUtils.setDefaultPref(
          "browser.startup.page",
          prefValue,
          param.StartPage == "homepage-locked"
        );
      }
      if ("NewTabOnRestore" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.sessionstore.newTabOnRestore",
          param.NewTabOnRestore
        );
        // Make the UI visible so the user can see what's been configured
        // on their behalf, regardless of the configured value.
        lazy.PoliciesUtils.setDefaultPref(
          "browser.sessionstore.newTabOnRestore.showSetting",
          true
        );
      }
    },
  },

  HttpAllowlist: {
    onBeforeAddons(manager, param) {
      lazy.addAllowDenyPermissions("https-only-load-insecure", param);
    },
  },

  HttpsOnlyMode: {
    onBeforeAddons(manager, param) {
      switch (param) {
        case "disallowed":
          lazy.PoliciesUtils.setAndLockPref(
            "dom.security.https_only_mode",
            false
          );
          break;
        case "enabled":
          lazy.PoliciesUtils.setDefaultPref(
            "dom.security.https_only_mode",
            true
          );
          break;
        case "force_enabled":
          lazy.PoliciesUtils.setAndLockPref(
            "dom.security.https_only_mode",
            true
          );
          break;
        case "allowed":
          // The default case.
          break;
      }
    },
  },

  InstallAddonsPermission: {
    onBeforeUIStartup(manager, param) {
      if ("Allow" in param) {
        lazy.addAllowDenyPermissions("install", param.Allow, null);
      }
      if ("Default" in param) {
        lazy.PoliciesUtils.setAndLockPref("xpinstall.enabled", param.Default);
        if (!param.Default) {
          manager.disallowFeature("installTemporaryAddon");
          lazy.PoliciesUtils.setAndLockPref(
            "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
            false
          );
          lazy.PoliciesUtils.setAndLockPref(
            "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
            false
          );
          lazy.PoliciesUtils.setAndLockPref(
            "extensions.getAddons.showPane",
            false
          );
          lazy.PoliciesUtils.setAndLockPref(
            "extensions.htmlaboutaddons.recommendations.enabled",
            false
          );
          manager.disallowFeature("xpinstall");
        }
      }
    },
  },

  IPProtectionAvailable: {
    onBeforeAddons(manager, param) {
      if (!param) {
        lazy.PoliciesUtils.setAndLockPref(
          "browser.ipProtection.enabled",
          false
        );
      }
    },
  },

  LegacyProfiles: {
    // Handled in nsToolkitProfileService.cpp (Windows only)
  },

  LegacySameSiteCookieBehaviorEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setDefaultPref(
        "network.cookie.sameSite.laxByDefault",
        !param
      );
    },
  },

  LegacySameSiteCookieBehaviorEnabledForDomainList: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setDefaultPref(
        "network.cookie.sameSite.laxByDefault.disabledHosts",
        param.join(",")
      );
    },
  },

  LocalFileLinks: {
    onBeforeAddons(manager, param) {
      // If there are existing capabilities, lock them with the policy pref.
      const policyNames = Services.prefs
        .getCharPref("capability.policy.policynames", "")
        .split(" ");
      policyNames.push("localfilelinks_policy");
      lazy.PoliciesUtils.setAndLockPref(
        "capability.policy.policynames",
        policyNames.join(" ")
      );
      lazy.PoliciesUtils.setAndLockPref(
        "capability.policy.localfilelinks_policy.checkloaduri.enabled",
        "allAccess"
      );
      lazy.PoliciesUtils.setAndLockPref(
        "capability.policy.localfilelinks_policy.sites",
        param.join(" ")
      );
    },
  },

  LocalNetworkAccess: {
    onBeforeAddons(manager, param) {
      // Only process if "Enabled" is explicitly specified
      if ("Enabled" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "network.lna.enabled",
          param.Enabled,
          param.Locked
        );

        if (param.Enabled === false) {
          // If LNA is explicitly disabled, disable other features too
          lazy.PoliciesUtils.setDefaultPref(
            "network.lna.block_trackers",
            false,
            param.Locked
          );
          lazy.PoliciesUtils.setDefaultPref(
            "network.lna.blocking",
            false,
            param.Locked
          );
        } else {
          // LNA is enabled - handle fine-grained controls
          // For backward compatibility, default to true if not specified
          const blockTrackers =
            "BlockTrackers" in param ? param.BlockTrackers : true;
          const enablePrompting =
            "EnablePrompting" in param ? param.EnablePrompting : true;

          lazy.PoliciesUtils.setDefaultPref(
            "network.lna.block_trackers",
            blockTrackers,
            param.Locked
          );
          lazy.PoliciesUtils.setDefaultPref(
            "network.lna.blocking",
            enablePrompting,
            param.Locked
          );
        }
      }

      // Handle SkipDomains separately (can be set independently of Enabled)
      if ("SkipDomains" in param && Array.isArray(param.SkipDomains)) {
        const skipDomainsValue = param.SkipDomains.join(",");
        lazy.PoliciesUtils.setDefaultPref(
          "network.lna.skip-domains",
          skipDomainsValue,
          param.Locked
        );
      }
    },
  },

  ManagedBookmarks: {},

  ManualAppUpdateOnly: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("autoAppUpdateChecking");
      }
    },
  },

  MicrosoftEntraSSO: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "network.http.microsoft-entra-sso.enabled",
        param
      );
    },
  },

  NetworkPrediction: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("network.dns.disablePrefetch", !param);
      lazy.PoliciesUtils.setAndLockPref(
        "network.dns.disablePrefetchFromHTTPS",
        !param
      );
    },
  },

  NewTabPage: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("browser.newtabpage.enabled", param);
    },
  },

  NoDefaultBookmarks: {
    onProfileAfterChange(manager, param) {
      if (param) {
        manager.disallowFeature("defaultBookmarks");
      }
    },
  },

  OfferToSaveLogins: {
    onBeforeUIStartup(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("signon.rememberSignons", param);
      lazy.PoliciesUtils.setAndLockPref(
        "services.passwordSavingEnabled",
        param
      );
    },
  },

  OfferToSaveLoginsDefault: {
    onBeforeUIStartup(manager, param) {
      const policies = Services.policies.getActivePolicies();
      if ("OfferToSaveLogins" in policies) {
        lazy.log.error(
          `OfferToSaveLoginsDefault ignored because OfferToSaveLogins is present.`
        );
      } else {
        lazy.PoliciesUtils.setDefaultPref("signon.rememberSignons", param);
      }
    },
  },

  OverrideFirstRunPage: {
    onProfileAfterChange(manager, param) {
      const url = param ? param : "";
      lazy.PoliciesUtils.setAndLockPref("startup.homepage_welcome_url", url);
      lazy.PoliciesUtils.setAndLockPref("browser.aboutwelcome.enabled", false);
    },
  },

  OverridePostUpdatePage: {
    onProfileAfterChange(manager, param) {
      const url = param ? param.href : "";
      lazy.PoliciesUtils.setAndLockPref("startup.homepage_override_url", url);
      // The pref startup.homepage_override_url is only used
      // as a fallback when the update.xml file hasn't provided
      // a specific post-update URL.
      manager.disallowFeature("postUpdateCustomPage");
    },
  },

  PasswordManagerEnabled: {
    onBeforeUIStartup(manager, param) {
      if (!param) {
        lazy.blockAboutPage(manager, "about:logins", true);
        lazy.PoliciesUtils.setAndLockPref(
          "pref.privacy.disable_button.view_passwords",
          true
        );
        lazy.PoliciesUtils.setAndLockPref(
          "browser.contextual-password-manager.enabled",
          false
        );
      }
      lazy.PoliciesUtils.setAndLockPref("signon.rememberSignons", param);
    },
  },

  PasswordManagerExceptions: {
    onBeforeUIStartup(manager, param) {
      lazy.addAllowDenyPermissions("login-saving", null, param);
    },
  },

  PDFjs: {
    onBeforeAddons(manager, param) {
      if ("Enabled" in param) {
        lazy.PoliciesUtils.setAndLockPref("pdfjs.disabled", !param.Enabled);
      }
      if ("EnablePermissions" in param) {
        lazy.PoliciesUtils.setAndLockPref(
          "pdfjs.enablePermissions",
          param.EnablePermissions
        );
      }
    },
  },

  Permissions: {
    onBeforeUIStartup(manager, param) {
      if (param.Camera) {
        lazy.addAllowDenyPermissions(
          "camera",
          param.Camera.Allow,
          param.Camera.Block
        );
        lazy.setDefaultPermission("camera", param.Camera);
      }

      if (param.Microphone) {
        lazy.addAllowDenyPermissions(
          "microphone",
          param.Microphone.Allow,
          param.Microphone.Block
        );
        lazy.setDefaultPermission("microphone", param.Microphone);
      }

      if (param.Autoplay) {
        lazy.addAllowDenyPermissions(
          "autoplay-media",
          param.Autoplay.Allow,
          param.Autoplay.Block
        );
        if ("Default" in param.Autoplay) {
          let prefValue;
          switch (param.Autoplay.Default) {
            case "allow-audio-video":
              prefValue = 0;
              break;
            case "block-audio":
              prefValue = 1;
              break;
            case "block-audio-video":
              prefValue = 5;
              break;
          }
          lazy.PoliciesUtils.setDefaultPref(
            "media.autoplay.default",
            prefValue,
            param.Autoplay.Locked
          );
        }
      }

      if (param.Location) {
        lazy.addAllowDenyPermissions(
          "geo",
          param.Location.Allow,
          param.Location.Block
        );
        lazy.setDefaultPermission("geo", param.Location);
      }

      if (param.Notifications) {
        lazy.addAllowDenyPermissions(
          "desktop-notification",
          param.Notifications.Allow,
          param.Notifications.Block
        );
        lazy.setDefaultPermission("desktop-notification", param.Notifications);
      }

      if ("VirtualReality" in param) {
        lazy.addAllowDenyPermissions(
          "xr",
          param.VirtualReality.Allow,
          param.VirtualReality.Block
        );
        lazy.setDefaultPermission("xr", param.VirtualReality);
      }

      if ("ScreenShare" in param) {
        lazy.addAllowDenyPermissions(
          "screen",
          param.ScreenShare.Allow,
          param.ScreenShare.Block
        );
        lazy.setDefaultPermission("screen", param.ScreenShare);
      }
    },
  },

  PictureInPicture: {
    onBeforeAddons(manager, param) {
      for (const pref of [
        "media.videocontrols.picture-in-picture.enabled",
        "media.videocontrols.picture-in-picture.video-toggle.enabled",
      ]) {
        if ("Enabled" in param) {
          lazy.PoliciesUtils.setDefaultPref(pref, param.Enabled);
        }
        if (param.Locked) {
          Services.prefs.lockPref(pref);
        }
      }
    },
  },

  PopupBlocking: {
    onBeforeUIStartup(manager, param) {
      lazy.addAllowDenyPermissions("popup", param.Allow, null);

      if (param.Locked) {
        let blockValue = true;
        if (param.Default !== undefined && !param.Default) {
          blockValue = false;
        }
        lazy.PoliciesUtils.setAndLockPref(
          "dom.disable_open_during_load",
          blockValue
        );
        lazy.PoliciesUtils.setAndLockPref(
          "dom.security.framebusting_intervention.enabled",
          blockValue
        );
      } else if (param.Default !== undefined) {
        lazy.PoliciesUtils.setDefaultPref(
          "dom.disable_open_during_load",
          !!param.Default
        );
        lazy.PoliciesUtils.setDefaultPref(
          "dom.security.framebusting_intervention.enabled",
          !!param.Default
        );
      }
    },
  },

  PostQuantumKeyAgreementEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "network.http.http3.enable_kyber",
        param
      );
      lazy.PoliciesUtils.setAndLockPref("security.tls.enable_kyber", param);
      lazy.PoliciesUtils.setAndLockPref(
        "media.webrtc.enable_pq_hybrid_kex",
        param
      );
    },
  },

  Preferences: {
    onBeforeAddons(manager, param) {
      const allowedPrefixes = [
        "accessibility.",
        "alerts.",
        "app.update.",
        "browser.",
        "datareporting.policy.",
        "devtools.",
        "dom.",
        "extensions.",
        "general.autoScroll",
        "general.smoothScroll",
        "geo.",
        "gfx.",
        "identity.fxaccounts.toolbar.",
        "intl.",
        "keyword.enabled",
        "layers.",
        "layout.",
        "mathml.disabled",
        "media.",
        "network.",
        "pdfjs.",
        "places.",
        "pref.",
        "print.",
        "privacy.baselineFingerprintingProtection",
        "privacy.fingerprintingProtection",
        "privacy.globalprivacycontrol.enabled",
        "privacy.userContext.enabled",
        "privacy.userContext.ui.enabled",
        "sidebar.",
        "signon.",
        "spellchecker.",
        "svg.context-properties.content.enabled",
        "svg.disabled",
        "toolkit.legacyUserProfileCustomizations.stylesheets",
        "ui.",
        "webgl.disabled",
        "webgl.force-enabled",
        "widget.",
        "xpinstall.enabled",
        "xpinstall.whitelist.required",
      ];
      if (!AppConstants.MOZ_REQUIRE_SIGNING) {
        allowedPrefixes.push("xpinstall.signatures.required");
      }
      const allowedSecurityPrefs = [
        "security.block_fileuri_script_with_wrong_mime",
        "security.csp.reporting.enabled",
        "security.default_personal_cert",
        "security.disable_button.openCertManager",
        "security.disable_button.openDeviceManager",
        "security.insecure_connection_text.enabled",
        "security.insecure_connection_text.pbmode.enabled",
        "security.mixed_content.block_active_content",
        "security.mixed_content.block_display_content",
        "security.mixed_content.upgrade_display_content",
        "security.osclientcerts.autoload",
        "security.OCSP.enabled",
        "security.OCSP.require",
        "security.pki.certificate_transparency.disable_for_hosts",
        "security.pki.certificate_transparency.disable_for_spki_hashes",
        "security.pki.certificate_transparency.mode",
        "security.ssl.enable_ocsp_stapling",
        "security.ssl.errorReporting.enabled",
        "security.ssl.require_safe_negotiation",
        "security.storage.encryption.sqlite.enabled",
        "security.tls.enable_0rtt_data",
        "security.tls.hello_downgrade_check",
        "security.tls.version.enable-deprecated",
        "security.warn_submit_secure_to_insecure",
        "security.webauthn.always_allow_direct_attestation",
      ];
      const blockedPrefs = [
        "app.update.channel",
        "app.update.lastUpdateTime",
        "app.update.migrated",
        "browser.vpn_promo.disallowed_regions",
      ];

      for (const preference in param) {
        if (blockedPrefs.includes(preference)) {
          lazy.reportFailure(
            "Preferences",
            `Unable to set preference ${preference}. Preference not allowed for security reasons.`
          );
          continue;
        }
        if (preference.startsWith("security.")) {
          if (!allowedSecurityPrefs.includes(preference)) {
            lazy.reportFailure(
              "Preferences",
              `Unable to set preference ${preference}. Preference not allowed for security reasons.`
            );
            continue;
          }
        } else if (
          !allowedPrefixes.some(prefix => preference.startsWith(prefix))
        ) {
          lazy.reportFailure(
            "Preferences",
            `Unable to set preference ${preference}. Preference not allowed for stability reasons.`
          );
          continue;
        }
        if (typeof param[preference] != "object") {
          // Legacy policy preferences
          try {
            lazy.PoliciesUtils.setAndLockPref(preference, param[preference]);
          } catch (e) {
            // Keep going so that one bad preference doesn't discard the
            // preferences that come after it.
            lazy.reportFailure(
              "Preferences",
              lazy.describePreferenceFailure(preference, param[preference], e)
            );
          }
        } else {
          if (param[preference].Status == "clear") {
            Services.prefs.clearUserPref(preference);
            continue;
          }

          let prefBranch;
          if (param[preference].Status == "user") {
            prefBranch = Services.prefs;
          } else {
            prefBranch = Services.prefs.getDefaultBranch("");
          }

          // Prefs that were previously locked should stay locked,
          // but policy can update the value.
          const prefWasLocked = Services.prefs.prefIsLocked(preference);
          if (prefWasLocked) {
            Services.prefs.unlockPref(preference);
          }
          try {
            const prefType =
              param[preference].Type || typeof param[preference].Value;
            switch (prefType) {
              case "boolean":
                prefBranch.setBoolPref(preference, param[preference].Value);
                break;

              case "number":
                if (!Number.isInteger(param[preference].Value)) {
                  throw new Error(`Non-integer value for ${preference}`);
                }

                // This is ugly, but necessary. On Windows GPO and macOS
                // configs, booleans are converted to 0/1. In the previous
                // Preferences implementation, the schema took care of
                // automatically converting these values to booleans.
                // Since we allow arbitrary prefs now, we have to do
                // something different. See bug 1666836, 1668374, and 1872267.

                // We only set something as int if it was explicit in policy,
                // the same type as the default pref, or NOT 0/1. Otherwise
                // we set it as bool.
                if (
                  param[preference].Type == "number" ||
                  prefBranch.getPrefType(preference) == prefBranch.PREF_INT ||
                  ![0, 1].includes(param[preference].Value)
                ) {
                  prefBranch.setIntPref(preference, param[preference].Value);
                } else {
                  prefBranch.setBoolPref(preference, !!param[preference].Value);
                }
                break;

              case "string":
                prefBranch.setStringPref(preference, param[preference].Value);
                break;
            }
          } catch (e) {
            lazy.reportFailure(
              "Preferences",
              lazy.describePreferenceFailure(
                preference,
                param[preference].Value,
                e
              )
            );
          }

          if (param[preference].Status == "locked" || prefWasLocked) {
            Services.prefs.lockPref(preference);
          }
        }
      }
    },
  },

  PrimaryPassword: {
    onAllWindowsRestored(manager, param) {
      if (param) {
        manager.disallowFeature("removeMasterPassword");
      } else {
        manager.disallowFeature("createMasterPassword");
      }
    },
  },

  PrintingEnabled: {
    onBeforeUIStartup(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("print.enabled", param);
    },
  },

  PrivateBrowsingModeAvailability: {
    onBeforeAddons(manager, param) {
      switch (param) {
        // Private Browsing mode disabled
        case 1:
          manager.disallowFeature("privatebrowsing");
          lazy.blockAboutPage(manager, "about:privatebrowsing", true);
          lazy.PoliciesUtils.setAndLockPref(
            "browser.privatebrowsing.autostart",
            false
          );
          break;
        // Private Browsing mode forced
        case 2:
          lazy.PoliciesUtils.setAndLockPref(
            "browser.privatebrowsing.autostart",
            true
          );
          break;
        // Private Browsing mode available
        case 0:
          break;
      }
    },
  },

  PromptForDownloadLocation: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.download.useDownloadDir",
        !param
      );
    },
  },

  Proxy: {
    onBeforeAddons(manager, param) {
      if (param.Locked) {
        manager.disallowFeature("changeProxySettings");
      }
      lazy.ProxyPolicies.configureProxySettings(
        param,
        lazy.PoliciesUtils.setDefaultPref
      );
    },
  },

  RelaunchRequired: {
    onBeforeUIStartup(_manager, param) {
      let notificationPeriodHours = 24;
      const restartTimeOfDay = { Hour: 12, Minute: 0 };
      if (
        typeof param.NotificationPeriodHours === "number" &&
        param.NotificationPeriodHours >= 0
      ) {
        notificationPeriodHours = param.NotificationPeriodHours;
      }
      if (typeof param.RestartTimeOfDay === "object") {
        try {
          const timeOfDay = Temporal.PlainTime.from({
            hour: param.RestartTimeOfDay.Hour,
            minute: param.RestartTimeOfDay.Minute,
          });
          // Looks like it's a valid time.
          restartTimeOfDay.Hour = timeOfDay.hour;
          restartTimeOfDay.Minute = timeOfDay.minute;
        } catch (ex) {
          lazy.reportFailure(
            "RelaunchRequired",
            "Incorrect format for RestartTimeOfDay"
          );
        }
      }
      lazy.PoliciesUtils.setAndLockPref(
        "app.update.compulsory_restart",
        JSON.stringify({
          NotificationPeriodHours: notificationPeriodHours,
          RestartTimeOfDay: restartTimeOfDay,
        })
      );
    },
  },

  RequestedLocales: {
    onBeforeAddons(manager, param) {
      let requestedLocales;
      if (Array.isArray(param)) {
        requestedLocales = param;
      } else if (param) {
        requestedLocales = param.split(",");
      } else {
        requestedLocales = [];
      }
      lazy.runOncePerModification(
        "requestedLocales",
        JSON.stringify(requestedLocales),
        () => {
          Services.locale.requestedLocales = requestedLocales;
        }
      );
    },
  },

  SanitizeOnShutdown: {
    onBeforeUIStartup(manager, param) {
      if (manager.getActivePolicies().ClearOnShutdown) {
        lazy.log.error(
          "SanitizeOnShutdown is ignored when ClearOnShutdown is also set."
        );
        return;
      }
      if (typeof param === "boolean") {
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.sanitize.sanitizeOnShutdown",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.cache",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.cookies",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.downloads",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.formdata",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.history",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.sessions",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.siteSettings",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown.offlineApps",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown_v2.cookiesAndStorage",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown_v2.cache",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown_v2.siteSettings",
          param
        );
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.clearOnShutdown_v2.formdata",
          param
        );
      } else {
        let locked = true;
        // Needed to preserve original behavior in perpetuity.
        let lockDefaultPrefs = true;
        if ("Locked" in param) {
          locked = param.Locked;
          lockDefaultPrefs = false;
        }
        lazy.PoliciesUtils.setDefaultPref(
          "privacy.sanitize.sanitizeOnShutdown",
          true,
          locked
        );
        if ("Cache" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cache",
            param.Cache,
            locked
          );
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cache",
            param.Cache,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cache",
            false,
            lockDefaultPrefs
          );
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cache",
            false,
            lockDefaultPrefs
          );
        }
        if ("Cookies" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cookies",
            param.Cookies,
            locked
          );

          // We set cookiesAndStorage to follow lock and pref
          // settings for cookies, and deprecate offlineApps
          // and sessions in the new clear on shutdown dialog - Bug 1853996
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cookiesAndStorage",
            param.Cookies,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cookies",
            false,
            lockDefaultPrefs
          );
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cookiesAndStorage",
            false,
            lockDefaultPrefs
          );
        }
        if ("Downloads" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.downloads",
            param.Downloads,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.downloads",
            false,
            lockDefaultPrefs
          );
        }
        if ("FormData" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.formdata",
            param.FormData,
            locked
          );

          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.formdata",
            param.FormData,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.formdata",
            false,
            lockDefaultPrefs
          );

          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.formdata",
            false,
            lockDefaultPrefs
          );
        }
        if ("History" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.history",
            param.History,
            locked
          );

          // We set browsingHistoryAndDownloads to follow lock and pref
          // settings for history, and deprecate downloads
          // in the new clear on shutdown dialog - Bug 1853996
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
            param.History,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.history",
            false,
            lockDefaultPrefs
          );
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
            false,
            lockDefaultPrefs
          );
        }
        if ("Sessions" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.sessions",
            param.Sessions,
            locked
          );
        } else {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.sessions",
            false,
            lockDefaultPrefs
          );
        }
        if ("SiteSettings" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.siteSettings",
            param.SiteSettings,
            locked
          );
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.siteSettings",
            param.SiteSettings,
            locked
          );
        }
        if ("OfflineApps" in param) {
          lazy.PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.offlineApps",
            param.OfflineApps,
            locked
          );
        }
        if (param.Exceptions) {
          lazy.addAllowDenyPermissions(
            "persist-data-on-shutdown",
            param.Exceptions
          );
        }
      }
    },
  },

  SearchBar: {
    onAllWindowsRestored(manager, param) {
      // This policy is meant to change the default behavior, not to force it.
      // If this policy was already applied and the user chose move the search
      // bar, don't move it again.
      lazy.runOncePerModification("searchInNavBar", param, () => {
        if (param == "separate") {
          lazy.CustomizableUI.addWidgetToArea(
            "search-container",
            lazy.CustomizableUI.AREA_NAVBAR,
            lazy.CustomizableUI.getPlacementOfWidget("urlbar-container")
              .position + 1
          );
        } else if (param == "unified") {
          lazy.CustomizableUI.removeWidgetFromArea("search-container");
        }
      });
    },
  },

  SearchEngines: {
    onBeforeUIStartup(manager, param) {
      if (param.PreventInstalls) {
        manager.disallowFeature("installSearchEngine", true);
      }
    },
    onAllWindowsRestored(manager, param) {
      // Returned so that the engine can report a failure of any of these
      // steps against this policy.
      return lazy.SearchService.init().then(async () => {
        // Adding of engines is handled by the SearchService in the init().
        // Remove can happen after those are added - no engines are allowed
        // to replace the application provided engines, even if they have been
        // removed.
        if (param.Remove) {
          // Only rerun if the list of engine names has changed.
          await lazy.runOncePerModification(
            "removeSearchEngines",
            JSON.stringify(param.Remove),
            async function () {
              for (const engineName of param.Remove) {
                const engine = lazy.SearchService.getEngineByName(engineName);
                if (engine) {
                  try {
                    await lazy.SearchService.removeEngine(
                      engine,
                      lazy.SearchService.CHANGE_REASON.ENTERPRISE
                    );
                  } catch (ex) {
                    lazy.reportFailure(
                      "SearchEngines",
                      `Unable to remove the search engine ${engineName} - ${ex}`
                    );
                  }
                }
              }
            }
          );
        }
        if (param.Default) {
          await lazy.runOncePerModification(
            "setDefaultSearchEngine",
            param.Default,
            async () => {
              let defaultEngine;
              try {
                defaultEngine = lazy.SearchService.getEngineByName(
                  param.Default
                );
                if (!defaultEngine) {
                  throw new Error("No engine by that name could be found");
                }
              } catch (ex) {
                lazy.reportFailure(
                  "SearchEngines",
                  `Search engine lookup failed when attempting to set ` +
                    `the default engine. Requested engine was ` +
                    `"${param.Default}" - ${ex}`
                );
              }
              if (defaultEngine) {
                try {
                  await lazy.SearchService.setDefault(
                    defaultEngine,
                    lazy.SearchService.CHANGE_REASON.ENTERPRISE
                  );
                } catch (ex) {
                  lazy.reportFailure(
                    "SearchEngines",
                    `Unable to set the default search engine - ${ex}`
                  );
                }
              }
            }
          );
        }
        if (param.DefaultPrivate) {
          await lazy.runOncePerModification(
            "setDefaultPrivateSearchEngine",
            param.DefaultPrivate,
            async () => {
              let defaultPrivateEngine;
              try {
                defaultPrivateEngine = lazy.SearchService.getEngineByName(
                  param.DefaultPrivate
                );
                if (!defaultPrivateEngine) {
                  throw new Error("No engine by that name could be found");
                }
              } catch (ex) {
                lazy.reportFailure(
                  "SearchEngines",
                  `Search engine lookup failed when attempting to set ` +
                    `the default private engine. Requested engine was ` +
                    `"${param.DefaultPrivate}" - ${ex}`
                );
              }
              if (defaultPrivateEngine) {
                try {
                  await lazy.SearchService.setDefaultPrivate(
                    defaultPrivateEngine,
                    lazy.SearchService.CHANGE_REASON.ENTERPRISE
                  );
                } catch (ex) {
                  lazy.reportFailure(
                    "SearchEngines",
                    `Unable to set the default private search engine - ${ex}`
                  );
                }
              }
            }
          );
        }
      });
    },
  },

  SearchSuggestEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.urlbar.suggest.searches",
        param
      );
      lazy.PoliciesUtils.setAndLockPref(
        "browser.search.suggest.enabled",
        param
      );
    },
  },

  SecurityDevices: {
    async _onProfileAfterChangeImpl(manager, param) {
      const pkcs11db = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
        Ci.nsIPKCS11ModuleDB
      );
      let securityDevices;
      if (param.Add || param.Delete) {
        // We're using the new syntax.
        securityDevices = param.Add;
        if (param.Delete) {
          for (const deviceName of param.Delete) {
            try {
              await pkcs11db.deleteModule(deviceName);
            } catch (e) {
              // Ignoring errors here since it might stick around in policy
              // after removing. Alternative would be to listModules and
              // make sure it's there before removing, but that seems
              // like unnecessary work.
            }
          }
        }
      } else {
        securityDevices = param;
      }
      if (!securityDevices) {
        return;
      }
      for (const deviceName in securityDevices) {
        let foundModule = false;
        for (const module of await pkcs11db.listModules()) {
          if (module && module.libName === securityDevices[deviceName]) {
            foundModule = true;
            break;
          }
        }
        if (foundModule) {
          continue;
        }
        try {
          await pkcs11db.addModule(
            deviceName,
            securityDevices[deviceName],
            0,
            0
          );
        } catch (ex) {
          lazy.reportFailure(
            "SecurityDevices",
            `Unable to add security device ${deviceName}`
          );
          lazy.log.debug(ex);
        }
      }
    },

    onProfileAfterChange(manager, param) {
      // Returned so that the engine can report a failure of the impl
      // against this policy.
      return this._onProfileAfterChangeImpl(manager, param).then(() => {
        Services.obs.notifyObservers(
          null,
          "test-enterprisepolicies-securitydevices"
        );
      });
    },
  },

  ShowHomeButton: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("removeHomeButtonByDefault");
      }
    },
    onAllWindowsRestored(manager, param) {
      if (param) {
        const homeButtonPlacement =
          lazy.CustomizableUI.getPlacementOfWidget("home-button");
        if (!homeButtonPlacement) {
          const placement =
            lazy.CustomizableUI.getPlacementOfWidget("forward-button");
          lazy.CustomizableUI.addWidgetToArea(
            "home-button",
            lazy.CustomizableUI.AREA_NAVBAR,
            placement.position + 2
          );
        }
      } else {
        lazy.CustomizableUI.removeWidgetFromArea("home-button");
      }
    },
  },

  SitePolicies: {
    /**
     * Converts a wildcard domain into a match pattern.
     *
     * Any `*.` prefix is stripped and then the remaining domain converted to a base
     * domain (ETLD + 1). If the pattern isn't long enough to be a base domain then an exception is
     * thrown. If the pattern is too long then a warning is logged and the base domain is used.
     *
     * @param {string} pattern The wildcard domain pattern to convert.
     * @returns {MatchPattern} The corresponding match pattern.
     */
    intoMatchPattern(pattern) {
      let base = pattern;
      if (base.startsWith("*.")) {
        base = base.substring(2);
      }

      // This will throw an exception if the domain is not long enough to make a site.
      const site = Services.eTLD.getBaseDomainFromHost(base);
      if (site != base) {
        console.warn(
          `SitePolicies: Pattern ${pattern} is too specific. Using *.${site} instead.`
        );
      }

      return new MatchPattern(`*://*.${site}/*`);
    },

    validate(params) {
      // The schema will have validated the general structure, we need to validate the patterns
      for (const param of params) {
        for (const patterns of [param.Match ?? [], param.Exceptions ?? []]) {
          try {
            patterns.forEach(p => {
              if (p != "*") {
                this.intoMatchPattern(p);
              }
            });
          } catch (e) {
            return false;
          }
        }
      }

      return true;
    },

    featuresForPolicies(policies) {
      const features = {};

      if ("DisableJit" in policies) {
        features.jit = !policies.DisableJit;
      }

      if ("HttpsOnly" in policies) {
        features.http = !policies.HttpsOnly;
      }

      if ("DisableServiceWorkers" in policies) {
        features.serviceworkers = !policies.DisableServiceWorkers;
      }

      return features;
    },

    // When no policy is provided we just default to an empty set which allows us
    // to clean up containers.
    onMissing() {
      return [];
    },

    onBeforeAddons(manager, params) {
      const policyContainerMap = new Map();
      const cis = lazy.ContextualIdentityService;

      for (const identity of cis.getPolicyIdentities()) {
        policyContainerMap.set(identity.policyId, identity.userContextId);
      }

      const unseenContainers = new Set(policyContainerMap.values());

      const sitePolicies = [];
      let hasContainerPolicy = false;
      const ephemeralUserContextIds = new Set();

      for (const policies of params) {
        const matches = policies.Match ?? [];
        const exceptions = policies.Exceptions ?? [];

        // If the entire web is an exception then this policy can never apply so
        // ignore it.
        if (exceptions.includes("*")) {
          continue;
        }

        const exceptionPatterns = exceptions.map(this.intoMatchPattern);
        let matchPatterns;

        if (!matches.length || matches.includes("*")) {
          matchPatterns = [new MatchPattern("<all_urls>")];
        } else {
          matchPatterns = matches.map(this.intoMatchPattern);

          // Filter out any match patterns that are also exceptions.
          matchPatterns = matchPatterns.filter(matchPattern => {
            return !exceptionPatterns.some(
              exceptionPattern =>
                exceptionPattern.pattern == matchPattern.pattern
            );
          });

          if (!matchPatterns.length) {
            // If there are no match patterns left then this policy can never
            // apply so ignore it.
            continue;
          }
        }

        const features = this.featuresForPolicies(policies.Policies);

        if ("Container" in policies.Policies) {
          const containerId = policies.Policies.Container.id;
          let userContextId = policyContainerMap.get(containerId);

          if (!userContextId) {
            userContextId = cis.createForPolicy(containerId).userContextId;
            policyContainerMap.set(containerId, userContextId);
          } else {
            unseenContainers.delete(userContextId);
          }

          features.container = userContextId;
          hasContainerPolicy = true;

          if (policies.Policies.Container.ephemeral) {
            ephemeralUserContextIds.add(userContextId);
          }
        }

        sitePolicies.push({
          match: new MatchPatternSet(matchPatterns),
          exceptions: new MatchPatternSet(exceptionPatterns),
          features,
        });
      }

      manager.updateSitePolicies(sitePolicies);

      for (const userContextId of unseenContainers) {
        cis.removePolicyIdentity(userContextId);
      }

      if (hasContainerPolicy) {
        lazy.PoliciesUtils.setAndLockPref("privacy.userContext.enabled", true);
        lazy.PoliciesUtils.setAndLockPref(
          "privacy.containers.switchDuringNavigation.enabled",
          true
        );

        if (ephemeralUserContextIds.size > 0) {
          lazy.EphemeralContainerWatcher.init(ephemeralUserContextIds);
        } else {
          lazy.EphemeralContainerWatcher.destroy();
        }
      } else {
        lazy.EphemeralContainerWatcher.destroy();
      }
    },
  },

  SkipTermsOfUse: {
    onBeforeAddons(manager, param) {
      if (param) {
        lazy.PoliciesUtils.setAndLockPref("termsofuse.acceptedVersion", 999);
        lazy.PoliciesUtils.setAndLockPref(
          "termsofuse.acceptedDate",
          Date.now().toString()
        );
      }
    },
  },

  SSLVersionMax: {
    onBeforeAddons(manager, param) {
      let tlsVersion;
      switch (param) {
        case "tls1":
          tlsVersion = 1;
          break;
        case "tls1.1":
          tlsVersion = 2;
          break;
        case "tls1.2":
          tlsVersion = 3;
          break;
        case "tls1.3":
          tlsVersion = 4;
          break;
      }
      lazy.PoliciesUtils.setAndLockPref("security.tls.version.max", tlsVersion);
    },
  },

  SSLVersionMin: {
    onBeforeAddons(manager, param) {
      let tlsVersion;
      switch (param) {
        case "tls1":
          tlsVersion = 1;
          break;
        case "tls1.1":
          tlsVersion = 2;
          break;
        case "tls1.2":
          tlsVersion = 3;
          break;
        case "tls1.3":
          tlsVersion = 4;
          break;
      }
      lazy.PoliciesUtils.setAndLockPref("security.tls.version.min", tlsVersion);
    },
  },

  StartDownloadsInTempDirectory: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.download.start_downloads_in_tmp_dir",
        param
      );
    },
  },

  SupportMenu: {
    onProfileAfterChange(manager, param) {
      manager.setSupportMenu(param);
    },
  },

  TranslateEnabled: {
    onBeforeAddons(manager, param) {
      const policies = Services.policies.getActivePolicies();
      if (policies.AIControls?.Translations || policies.AIControls?.Default) {
        lazy.log.warn(
          "Ignoring TranslateEnabled policy in favor of AIControls"
        );
        return;
      }
      lazy.PoliciesUtils.setAndLockPref("browser.translations.enable", param);
      lazy.PoliciesUtils.setAndLockPref(
        "browser.ai.control.translations",
        param ? "enabled" : "blocked"
      );
    },
  },

  UserMessaging: {
    onBeforeAddons(manager, param) {
      if ("ExtensionRecommendations" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
          param.ExtensionRecommendations,
          param.Locked
        );
      }
      if ("FeatureRecommendations" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
          param.FeatureRecommendations,
          param.Locked
        );

        // We use the mostRecentTargetLanguages pref to control the
        // translations panel intro. Setting a language value simulates a
        // first translation, which skips the intro panel for users with
        // FeatureRecommendations disabled.
        const topWebPreferredLanguage = Services.locale.acceptLanguages
          .split(",")[0]
          .trim();

        const preferredLanguage = topWebPreferredLanguage.length
          ? topWebPreferredLanguage
          : Services.locale.appLocaleAsBCP47;

        lazy.PoliciesUtils.setDefaultPref(
          "browser.translations.mostRecentTargetLanguages",
          param.FeatureRecommendations ? "" : preferredLanguage,
          param.Locked
        );
      }
      if ("UrlbarInterventions" in param && !param.UrlbarInterventions) {
        manager.disallowFeature("urlbarinterventions");
      }
      if ("SkipOnboarding" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.aboutwelcome.enabled",
          !param.SkipOnboarding,
          param.Locked
        );
      }
      if ("MoreFromMozilla" in param) {
        lazy.PoliciesUtils.setDefaultPref(
          "browser.preferences.moreFromMozilla",
          param.MoreFromMozilla,
          param.Locked
        );
      }
      if ("FirefoxLabs" in param && !param.FirefoxLabs) {
        manager.disallowFeature("FirefoxLabs");
      }
    },
  },

  UseSystemPrintDialog: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("print.prefer_system_dialog", param);
    },
  },

  VisualSearchEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "browser.search.visualSearch.featureGate",
        param
      );
    },
  },

  WebsiteFilter: {
    onBeforeUIStartup(manager, param) {
      lazy.WebsiteFilter.init(param.Block || [], param.Exceptions || []);
    },
  },

  WindowsSSO: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref(
        "network.http.windows-sso.enabled",
        param
      );
    },
  },

  XSLTEnabled: {
    onBeforeAddons(manager, param) {
      lazy.PoliciesUtils.setAndLockPref("dom.xslt.enabled", param);
    },
  },
};
