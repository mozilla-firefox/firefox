/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetters(lazy, {
  gCertDB: ["@mozilla.org/security/x509certdb;1", Ci.nsIX509CertDB],
  gExternalProtocolService: [
    "@mozilla.org/uriloader/external-protocol-service;1",
    Ci.nsIExternalProtocolService,
  ],
  gHandlerService: [
    "@mozilla.org/uriloader/handler-service;1",
    Ci.nsIHandlerService,
  ],
  gMIMEService: ["@mozilla.org/mime;1", Ci.nsIMIMEService],
});

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
  BookmarksPolicies: "resource:///modules/policies/BookmarksPolicies.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  setEnterpriseGuards: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  ProxyPolicies: "resource:///modules/policies/ProxyPolicies.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  QuickSuggest: "moz-src:///browser/components/urlbar/QuickSuggest.sys.mjs",
  WebsiteFilter: "resource:///modules/policies/WebsiteFilter.sys.mjs",
  addPolicyPermission: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  describePreferenceFailure: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  reportFailure: "resource://gre/modules/PoliciesHelpers.sys.mjs",
});

const PREF_LOGLEVEL = "browser.policies.loglevel";
const BROWSER_DOCUMENT_URL = AppConstants.BROWSER_CHROME_URL;
const ABOUT_CONTRACT = "@mozilla.org/network/protocol/about;1?what=";

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
  let { ConsoleAPI } = ChromeUtils.importESModule(
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
        console.log("_cleanup from onBeforeAddons");
        clearBlockedAboutPages();
      }
    },
    onProfileAfterChange() {
      if (Cu.isInAutomation) {
        console.log("_cleanup from onProfileAfterChange");
      }
    },
    onBeforeUIStartup() {
      if (Cu.isInAutomation) {
        console.log("_cleanup from onBeforeUIStartup");
      }
    },
    onAllWindowsRestored() {
      if (Cu.isInAutomation) {
        console.log("_cleanup from onAllWindowsRestored");
      }
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
      ];

      const defaultItem = param.Default;
      const defaultLocked = defaultItem?.Locked ?? false;

      for (const [key, prefs, aiControlPref] of features) {
        let item = param[key] ?? defaultItem;
        if (!item) {
          continue;
        }
        let value = item.Value;
        let locked = item.Locked ?? defaultLocked;
        PoliciesUtils.setDefaultPref(aiControlPref, value, locked);
        for (const pref of prefs) {
          PoliciesUtils.setDefaultPref(pref, value === "available", locked);
        }
      }

      if (defaultItem) {
        PoliciesUtils.setDefaultPref(
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
        let channel = subject.QueryInterface(Ci.nsIHttpChannel);
        if (channel.URI.host.endsWith(".google.com")) {
          channel.setRequestHeader("X-GoogApps-Allowed-Domains", param, true);
        }
      }, "http-on-modify-request");
    },
  },

  AllowFileSelectionDialogs: {
    onBeforeUIStartup(manager, param) {
      if (!param) {
        setAndLockPref("widget.disable_file_pickers", true);
        setAndLockPref("browser.download.useDownloadDir", true);
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

      let pinParts = param.split(".");

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
        PoliciesUtils.setDefaultPref(
          "network.negotiate-auth.trusted-uris",
          param.SPNEGO.join(", "),
          locked
        );
      }
      if ("Delegated" in param) {
        PoliciesUtils.setDefaultPref(
          "network.negotiate-auth.delegation-uris",
          param.Delegated.join(", "),
          locked
        );
      }
      if ("NTLM" in param) {
        PoliciesUtils.setDefaultPref(
          "network.automatic-ntlm-auth.trusted-uris",
          param.NTLM.join(", "),
          locked
        );
      }
      if ("AllowNonFQDN" in param) {
        if ("NTLM" in param.AllowNonFQDN) {
          PoliciesUtils.setDefaultPref(
            "network.automatic-ntlm-auth.allow-non-fqdn",
            param.AllowNonFQDN.NTLM,
            locked
          );
        }
        if ("SPNEGO" in param.AllowNonFQDN) {
          PoliciesUtils.setDefaultPref(
            "network.negotiate-auth.allow-non-fqdn",
            param.AllowNonFQDN.SPNEGO,
            locked
          );
        }
      }
      if ("AllowProxies" in param) {
        if ("NTLM" in param.AllowProxies) {
          PoliciesUtils.setDefaultPref(
            "network.automatic-ntlm-auth.allow-proxies",
            param.AllowProxies.NTLM,
            locked
          );
        }
        if ("SPNEGO" in param.AllowProxies) {
          PoliciesUtils.setDefaultPref(
            "network.negotiate-auth.allow-proxies",
            param.AllowProxies.SPNEGO,
            locked
          );
        }
      }
      if ("PrivateBrowsing" in param) {
        PoliciesUtils.setDefaultPref(
          "network.auth.private-browsing-sso",
          param.PrivateBrowsing,
          locked
        );
      }
    },
  },

  AutofillAddressEnabled: {
    onBeforeAddons(manager, param) {
      setAndLockPref("extensions.formautofill.addresses.enabled", param);
    },
  },

  AutofillCreditCardEnabled: {
    onBeforeAddons(manager, param) {
      setAndLockPref("extensions.formautofill.creditCards.enabled", param);
    },
  },

  AutoLaunchProtocolsFromOrigins: {
    onBeforeAddons(manager, param) {
      for (let info of param) {
        addAllowDenyPermissions(
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
        blockAboutPage(manager, "about:addons", true);
      }
    },
  },

  BlockAboutConfig: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        blockAboutPage(manager, "about:config");
        setAndLockPref("devtools.chrome.enabled", false);
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
        blockAboutPage(manager, "about:profiles");
        blockAboutPage(manager, "about:profilemanager");
        blockAboutPage(manager, "about:editprofile");
        blockAboutPage(manager, "about:deleteprofile");
        blockAboutPage(manager, "about:newprofile");
      }
    },
  },

  BlockAboutSupport: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        blockAboutPage(manager, "about:support");
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
        setAndLockPref("browser.backup.enabled", param);
        setAndLockPref("browser.backup.archive.enabled", param);
        setAndLockPref("browser.backup.restore.enabled", param);
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
          PoliciesUtils.setDefaultPref(
            "browser.backup.enabled",
            serviceValue,
            true
          );
        }

        if (hasBackup) {
          PoliciesUtils.setDefaultPref(
            "browser.backup.archive.enabled",
            param.AllowBackup,
            true
          );
        }

        if (hasRestore) {
          PoliciesUtils.setDefaultPref(
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
      setAndLockPref("network.captive-portal-service.enabled", param);
    },
  },

  Certificates: {
    onBeforeAddons(manager, param) {
      if ("ImportEnterpriseRoots" in param) {
        setAndLockPref(
          "security.enterprise_roots.enabled",
          param.ImportEnterpriseRoots
        );
      }
      if ("Install" in param) {
        (async () => {
          let dirs = [];
          let platform = AppConstants.platform;
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
          for (let certfilename of param.Install) {
            let certfile;
            try {
              certfile = Cc["@mozilla.org/file/local;1"].createInstance(
                Ci.nsIFile
              );
              certfile.initWithPath(certfilename);
            } catch (e) {
              for (let dir of dirs) {
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
            let reader = new FileReader();
            reader.onloadend = function () {
              if (reader.readyState != reader.DONE) {
                lazy.reportFailure(
                  "Certificates",
                  `Unable to read certificate - ${certfile.path}`
                );
                return;
              }
              let certFile = reader.result;
              let certFileArray = [];
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
                    pemToBase64(certFile)
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
                      pemToBase64(certFile),
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
        setAndLockPref("privacy.sanitize.sanitizeOnShutdown", param);
        for (const pref of Object.values(CLEAR_ON_SHUTDOWN_PREFS)) {
          setAndLockPref(pref, param);
        }
        return;
      }

      // Named categories are enforced; the rest are left to the user.
      setAndLockPref("privacy.sanitize.sanitizeOnShutdown", true);
      for (const [member, pref] of Object.entries(CLEAR_ON_SHUTDOWN_PREFS)) {
        if (member in param) {
          setAndLockPref(pref, param[member]);
        }
      }
    },
  },

  Containers: {
    // Queried directly by ContextualIdentityService.sys.mjs
  },

  ContentAnalysis: {
    onBeforeAddons(manager, param) {
      // For security reasons, all of the Content Analysis related prefs should be locked in
      // this method, even if the values aren't specified in Enterprise Policies.
      setPrefIfPresentAndLock(
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
          setAndLockPref(
            "browser.contentanalysis.agent_timeout",
            param.AgentTimeout
          );
        }
      } else {
        Services.prefs.lockPref("browser.contentanalysis.agent_timeout");
      }
      setPrefIfPresentAndLock(
        param,
        "AllowUrlRegexList",
        "browser.contentanalysis.allow_url_regex_list"
      );
      setPrefIfPresentAndLock(
        param,
        "DenyUrlRegexList",
        "browser.contentanalysis.deny_url_regex_list"
      );
      setPrefIfPresentAndLock(
        param,
        "AgentName",
        "browser.contentanalysis.agent_name"
      );
      setPrefIfPresentAndLock(
        param,
        "ClientSignature",
        "browser.contentanalysis.client_signature"
      );
      setPrefIfPresentAndLock(
        param,
        "MaxConnectionsCount",
        "browser.contentanalysis.max_connections"
      );
      let resultPrefs = [
        ["DefaultResult", "default_result"],
        ["TimeoutResult", "timeout_result"],
      ];
      for (let pref of resultPrefs) {
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
            setAndLockPref(
              `browser.contentanalysis.${pref[1]}`,
              param[pref[0]]
            );
          }
        } else {
          Services.prefs.lockPref(`browser.contentanalysis.${pref[1]}`);
        }
      }
      let boolPrefs = [
        ["IsPerUser", "is_per_user"],
        ["ShowBlockedResult", "show_blocked_result"],
        ["BypassForSameTabOperations", "bypass_for_same_tab_operations"],
      ];
      for (let pref of boolPrefs) {
        if (pref[0] in param) {
          setAndLockPref(
            `browser.contentanalysis.${pref[1]}`,
            !!param[pref[0]]
          );
        } else {
          Services.prefs.lockPref(`browser.contentanalysis.${pref[1]}`);
        }
      }
      let interceptionPointPrefs = [
        ["Clipboard", "clipboard"],
        ["Download", "download"],
        ["DragAndDrop", "drag_and_drop"],
        ["FileUpload", "file_upload"],
        ["Print", "print"],
      ];
      if ("InterceptionPoints" in param) {
        for (let pref of interceptionPointPrefs) {
          let value = true;
          if (pref[0] in param.InterceptionPoints) {
            if ("Enabled" in param.InterceptionPoints[pref[0]]) {
              value = !!param.InterceptionPoints[pref[0]].Enabled;
            }
          }
          setAndLockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.enabled`,
            value
          );
        }
      } else {
        for (let pref of interceptionPointPrefs) {
          Services.prefs.lockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.enabled`
          );
        }
      }
      let plainTextOnlyPrefs = [
        ["Clipboard", "clipboard"],
        ["DragAndDrop", "drag_and_drop"],
      ];
      if ("InterceptionPoints" in param) {
        for (let pref of plainTextOnlyPrefs) {
          // Need to set and lock this value even if the enterprise
          // policy isn't set so users can't change it
          let value = true;
          if ("InterceptionPoints" in param) {
            if (pref[0] in param.InterceptionPoints) {
              if ("PlainTextOnly" in param.InterceptionPoints[pref[0]]) {
                value = !!param.InterceptionPoints[pref[0]].PlainTextOnly;
              }
            }
          }
          setAndLockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.plain_text_only`,
            value
          );
        }
      } else {
        for (let pref of plainTextOnlyPrefs) {
          Services.prefs.lockPref(
            `browser.contentanalysis.interception_point.${pref[1]}.plain_text_only`
          );
        }
      }
      if ("Enabled" in param) {
        let enabled = !!param.Enabled;
        setAndLockPref("browser.contentanalysis.enabled", enabled);
        let ca = Cc["@mozilla.org/contentanalysis;1"].getService(
          Ci.nsIContentAnalysis
        );
        ca.isSetByEnterprisePolicy = true;
      } else {
        // Probably not strictly necessary, but let's lock everything
        // to be consistent.
        Services.prefs.lockPref("browser.contentanalysis.enabled");
      }
    },
  },

  Cookies: {
    onBeforeUIStartup(manager, param) {
      addAllowDenyPermissions("cookie", param.Allow, param.Block);

      if (param.AllowSession) {
        for (let origin of param.AllowSession) {
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

      if (param.Block) {
        const hosts = param.Block.map(url => url.hostname)
          .sort()
          .join("\n");
        runOncePerModification("clearCookiesForBlockedHosts", hosts, () => {
          for (let blocked of param.Block) {
            Services.cookies.removeCookiesWithOriginAttributes(
              "{}",
              blocked.hostname
            );
          }
        });
      }

      if (param.ExpireAtSessionEnd != undefined) {
        lazy.log.error(
          "'ExpireAtSessionEnd' has been deprecated and it has no effect anymore."
        );
      }

      // New Cookie Behavior option takes precendence
      let defaultPref = Services.prefs.getDefaultBranch("");
      let newCookieBehavior = defaultPref.getIntPref(
        "network.cookie.cookieBehavior"
      );
      let newCookieBehaviorPB = defaultPref.getIntPref(
        "network.cookie.cookieBehavior.pbmode"
      );
      if ("Behavior" in param || "BehaviorPrivateBrowsing" in param) {
        let behaviors = {
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
      PoliciesUtils.setDefaultPref(
        "network.cookie.cookieBehavior",
        newCookieBehavior,
        param.Locked
      );
      PoliciesUtils.setDefaultPref(
        "network.cookie.cookieBehavior.pbmode",
        newCookieBehaviorPB,
        param.Locked
      );
    },
  },

  DefaultBrowserSettingEnabled: {
    onBeforeAddons(manager, param) {
      if (param) {
        setAndLockPref("browser.shell.checkDefaultBrowser", true);
      } else {
        manager.disallowFeature("setDefaultBrowser");
        setAndLockPref("browser.shell.checkDefaultBrowser", false);
      }
    },
  },

  DefaultDownloadDirectory: {
    onBeforeAddons(manager, param) {
      PoliciesUtils.setDefaultPref(
        "browser.download.dir",
        replacePathVariables(param)
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
        PoliciesUtils.setDefaultPref("dom.webserial.enabled", true, false);
      } else if (param == 2) {
        // do not allow access to serial ports
        setAndLockPref("dom.webserial.enabled", false);
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
        setAndLockPref("identity.fxaccounts.enabled", false);
        setAndLockPref("browser.aboutwelcome.enabled", false);
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
      let policies = Services.policies.getActivePolicies();
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
        runOncePerModification("disableBuiltinPDFViewer", "false", () => {
          let pdfMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
            "application/pdf",
            "pdf"
          );
          processMIMEInfo(
            { action: "handleInternally" },
            pdfMIMEInfo,
            "DisableBuiltinPDFViewer"
          );
        });
        return;
      }
      let pdfMIMEInfo = lazy.gMIMEService.getFromTypeAndExtension(
        "application/pdf",
        "pdf"
      );
      processMIMEInfo(
        { action: "useSystemDefault" },
        pdfMIMEInfo,
        "DisableBuiltinPDFViewer"
      );
    },
  },

  DisabledCiphers: {
    onBeforeAddons(manager, param) {
      let cipherPrefs = {
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

      for (let cipher in param) {
        setAndLockPref(cipherPrefs[cipher], !param[cipher]);
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
        setAndLockPref("devtools.policy.disabled", true);
        setAndLockPref("devtools.chrome.enabled", false);
        // Covers Marionette and the Remote Agent.
        setAndLockPref("remote.policy.disabled", true);

        manager.disallowFeature("devtools");
        blockAboutPage(manager, "about:debugging");
        blockAboutPage(manager, "about:devtools-toolbox");
        blockAboutPage(manager, "about:profiling");
      }
    },
  },

  DisableEncryptedClientHello: {
    onBeforeAddons(manager, param) {
      if (param) {
        setAndLockPref("network.dns.echconfig.enabled", false);
        setAndLockPref("network.dns.http3_echconfig.enabled", false);
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
        setAndLockPref("identity.fxaccounts.enabled", false);
        setAndLockPref("browser.aboutwelcome.enabled", false);
      }
    },
  },

  DisableFirefoxScreenshots: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        setAndLockPref("screenshots.browser.component.enabled", false);
      }
    },
  },

  DisableFirefoxStudies: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("Shield");
        setAndLockPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
          false
        );
        setAndLockPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
          false
        );
      }
    },
  },

  DisableForgetButton: {
    onProfileAfterChange(manager, param) {
      if (param) {
        setAndLockPref("privacy.panicButton.enabled", false);
      }
    },
  },

  DisableFormHistory: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        setAndLockPref("browser.formfill.enable", false);
      }
    },
  },

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
        setAndLockPref("layout.forms.reveal-password-button.enabled", false);
      }
    },
  },

  DisablePrivateBrowsing: {
    onBeforeAddons(manager, param) {
      if (param) {
        manager.disallowFeature("privatebrowsing");
        blockAboutPage(manager, "about:privatebrowsing", true);
        setAndLockPref("browser.privatebrowsing.autostart", false);
      }
    },
  },

  DisableProfileImport: {
    onBeforeUIStartup(manager, param) {
      if (param) {
        manager.disallowFeature("profileImport");
        setAndLockPref(
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
        setAndLockPref("browser.disableResetPrompt", true);
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
        setAndLockPref(
          "security.certerror.hideAddException",
          param.InvalidCertificate
        );
      }

      if ("SafeBrowsing" in param) {
        setAndLockPref(
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
        setAndLockPref("datareporting.healthreport.uploadEnabled", false);
        setAndLockPref("datareporting.policy.dataSubmissionEnabled", false);
        setAndLockPref("toolkit.telemetry.archive.enabled", false);
        setAndLockPref("datareporting.usage.uploadEnabled", false);
        blockAboutPage(manager, "about:telemetry");
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
      runOncePerModification("displayBookmarksToolbar", visibility, () => {
        let visibilityPref = "browser.toolbars.bookmarks.visibility";
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
        runOncePerModification("displayMenuBar", value, () => {
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
            setAndLockPref("ui.key.menuAccessKeyFocuses", false);
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
        PoliciesUtils.setDefaultPref("network.trr.mode", mode, param.Locked);
      }
      if ("ProviderURL" in param) {
        PoliciesUtils.setDefaultPref(
          "network.trr.uri",
          param.ProviderURL.href,
          param.Locked
        );
      }
      if ("ExcludedDomains" in param) {
        PoliciesUtils.setDefaultPref(
          "network.trr.excluded-domains",
          param.ExcludedDomains.join(","),
          param.Locked
        );
      }
    },
  },

  DontCheckDefaultBrowser: {
    onBeforeUIStartup(manager, param) {
      setAndLockPref("browser.shell.checkDefaultBrowser", !param);
    },
  },

  DownloadDirectory: {
    onBeforeAddons(manager, param) {
      setAndLockPref("browser.download.dir", replacePathVariables(param));
      // If a custom download directory is being used, just lock folder list to 2.
      setAndLockPref("browser.download.folderList", 2);
      // Per Chrome spec, user can't choose to download every time
      // if this is set.
      setAndLockPref("browser.download.useDownloadDir", true);
    },
  },

  EnableTrackingProtection: {
    onAllWindowsRestored(manager, param) {
      if (param.Category) {
        // browser.contentblocking.category only works as a default pref if
        // it is locked.
        PoliciesUtils.setDefaultPref(
          "browser.contentblocking.category",
          param.Category,
          true
        );
        let { ContentBlockingPrefs } = ChromeUtils.importESModule(
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
        addAllowDenyPermissions("trackingprotection", param.Exceptions);
      }
      if ("BaselineExceptions" in param) {
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.allow_list.baseline.enabled",
          param.BaselineExceptions,
          param.Locked
        );
      }
      if ("ConvenienceExceptions" in param) {
        PoliciesUtils.setDefaultPref(
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
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.enabled",
          true,
          param.Locked
        );
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.pbmode.enabled",
          true,
          param.Locked
        );
      } else {
        setAndLockPref("privacy.trackingprotection.enabled", false);
        setAndLockPref("privacy.trackingprotection.pbmode.enabled", false);
      }
      if ("Cryptomining" in param) {
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.cryptomining.enabled",
          param.Cryptomining,
          param.Locked
        );
      }
      if ("Fingerprinting" in param) {
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.fingerprinting.enabled",
          param.Fingerprinting,
          param.Locked
        );
      }
      if ("EmailTracking" in param) {
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.emailtracking.enabled",
          param.EmailTracking,
          param.Locked
        );
        PoliciesUtils.setDefaultPref(
          "privacy.trackingprotection.emailtracking.pbmode.enabled",
          param.EmailTracking,
          param.Locked
        );
      }
      if ("SuspectedFingerprinting" in param) {
        PoliciesUtils.setDefaultPref(
          "privacy.fingerprintingProtection",
          param.SuspectedFingerprinting,
          param.Locked
        );
        PoliciesUtils.setDefaultPref(
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
        PoliciesUtils.setDefaultPref(
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
        uninstallingPromise = uninstallListedAddons(
          param.Uninstall,
          param.Install
        );
      }
      if ("Install" in param) {
        installingPromise = uninstallingPromise.then(uninstallListChanged => {
          if (uninstallListChanged) {
            // Re-run the install even if its list hasn't changed, which is how
            // an add-on listed in both Uninstall and Install gets updated.
            clearRunOnceModification("extensionsInstall");
          }
          return runOncePerModification(
            "extensionsInstall",
            JSON.stringify(param.Install),
            () => {
              for (let location of param.Install) {
                let uri;
                try {
                  // We need to try as a file first because
                  // Windows paths are valid URIs.
                  // This is done for legacy support (old API)
                  let xpiFile = new lazy.FileUtils.File(location);
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
                installAddonFromURL(uri.spec, null, null, "Extensions");
              }
            }
          );
        });
      }
      if ("Locked" in param) {
        for (let ID of param.Locked) {
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
      try {
        manager.setExtensionSettings(param);
      } catch (e) {
        lazy.reportFailure(
          "ExtensionSettings",
          `Some ExtensionSettings could not be applied: ${e.message}`
        );
      }
      try {
        applyExtensionGuards(param);
      } catch (e) {
        lazy.reportFailure(
          "ExtensionSettings",
          `Invalid runtime_blocked_hosts/runtime_allowed_hosts in ` +
            `ExtensionSettings: ${e.message}`
        );
      }
    },
    async onBeforeUIStartup(manager, param) {
      let extensionSettings = param;
      let blockAllExtensions = false;
      if ("*" in extensionSettings) {
        if (
          "installation_mode" in extensionSettings["*"] &&
          extensionSettings["*"].installation_mode == "blocked"
        ) {
          blockAllExtensions = true;
          // Turn off discovery pane in about:addons
          setAndLockPref("extensions.getAddons.showPane", false);
          // Turn off recommendations
          setAndLockPref(
            "extensions.htmlaboutaddons.recommendations.enabled",
            false
          );
          manager.disallowFeature("installTemporaryAddon");
        }
        if ("restricted_domains" in extensionSettings["*"]) {
          let restrictedDomains = Services.prefs
            .getCharPref("extensions.webextensions.restrictedDomains")
            .split(",");
          setAndLockPref(
            "extensions.webextensions.restrictedDomains",
            restrictedDomains
              .concat(extensionSettings["*"].restricted_domains)
              .join(",")
          );
        }
      }
      let addons = new Map();
      for (let a of await lazy.AddonManager.getAllAddons()) {
        addons.set(a.id, a);
      }
      let allowedExtensions = [];
      for (let extensionID in extensionSettings) {
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
              installAddonFromURL(
                extensionSettings[extensionID].install_url,
                extensionID,
                existingAddon,
                "ExtensionSettings"
              );
            } else if (!existingAddon) {
              installAddonFromRepository(extensionID, "ExtensionSettings");
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
              let addon = await lazy.AddonManager.getAddonByID(extensionID);
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
      let allowedTypes = extensionSettings["*"]?.allowed_types;
      if (blockAllExtensions || allowedTypes) {
        for (let addon of addons.values()) {
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
              let addonToUninstall = await lazy.AddonManager.getAddonByID(
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
      for (let addon of addons.values()) {
        if (
          addon.isSystem ||
          addon.isBuiltin ||
          !(addon.scope & lazy.AddonManager.SCOPE_PROFILE)
        ) {
          continue;
        }
        let blockedPerms =
          Services.policies.getExtensionSettings(addon.id)
            ?.blocked_permissions ?? [];
        if (!blockedPerms.length) {
          continue;
        }
        try {
          let granted = await lazy.ExtensionPermissions.get(addon.id);
          let toRemove = granted.permissions.filter(perm =>
            blockedPerms.includes(perm)
          );
          if (toRemove.length) {
            let extension = WebExtensionPolicy.getByID(addon.id)?.extension;
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
        setAndLockPref("extensions.update.enabled", param);
      }
    },
  },

  FirefoxHome: {
    onBeforeAddons(manager, param) {
      if ("Search" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSearch",
          param.Search,
          param.Locked
        );
      }
      if ("Weather" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showWeather",
          param.Weather,
          param.Locked
        );
        // The Nova layout (enabled by default) binds the weather toggle to a
        // different pref, so set it too.
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.widgets.weather.enabled",
          param.Weather,
          param.Locked
        );
      }
      if ("TopSites" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.topsites",
          param.TopSites,
          param.Locked
        );
      }
      if ("SponsoredTopSites" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsoredTopSites",
          param.SponsoredTopSites,
          param.Locked
        );
      }
      if ("Highlights" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.highlights",
          param.Highlights,
          param.Locked
        );
      }
      if ("Pocket" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.topstories",
          param.Pocket,
          param.Locked
        );
      }
      if ("Stories" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.feeds.section.topstories",
          param.Stories,
          param.Locked
        );
      }
      if ("SponsoredPocket" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsored",
          param.SponsoredPocket,
          param.Locked
        );
      }
      if ("SponsoredStories" in param) {
        PoliciesUtils.setDefaultPref(
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
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.showSponsoredCheckboxes",
          param.SponsoredTopSites || param.SponsoredStories,
          param.Locked
        );
      }
      if (param.Widgets) {
        if ("Enabled" in param.Widgets) {
          PoliciesUtils.setDefaultPref(
            "browser.newtabpage.activity-stream.widgets.enabled",
            param.Widgets.Enabled,
            param.Locked
          );
        }
        // Blocked always locks, regardless of the Locked property: the lock is
        // what blocks the widget, and it is enough on its own whatever the
        // widget's system pref is set to. IDs aren't validated, so one policy
        // file stays valid across versions that add or remove widgets.
        for (let id of param.Widgets.Blocked ?? []) {
          PoliciesUtils.setDefaultPref(
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
          PoliciesUtils.setDefaultPref(
            "browser.urlbar.suggest.quicksuggest.all",
            param.WebSuggestions,
            param.Locked
          );
        }
        if ("SponsoredSuggestions" in param) {
          PoliciesUtils.setDefaultPref(
            "browser.urlbar.suggest.quicksuggest.sponsored",
            param.SponsoredSuggestions,
            param.Locked
          );
        }
        // `ImproveSuggest` is deprecated and replaced with `OnlineEnabled`.
        if ("OnlineEnabled" in param || "ImproveSuggest" in param) {
          PoliciesUtils.setDefaultPref(
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
      let policies = Services.policies.getActivePolicies();
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
            PoliciesUtils.setDefaultPref(pref, value, param.Locked);
          }
          PoliciesUtils.setDefaultPref(
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
      setAndLockPref("browser.fixup.dns_first_for_single_words", param);
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
            processMIMEInfo(mimeInfo, realMIMEInfo, "Handlers");
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
            processMIMEInfo(mimeInfo, realMIMEInfo, "Handlers");
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
            processMIMEInfo(handlerInfo, realHandlerInfo, "Handlers");
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
        setAndLockPref("layers.acceleration.disabled", true);
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
        PoliciesUtils.setDefaultPref(
          "browser.startup.homepage",
          homepages,
          param.Locked
        );
        if (param.Locked) {
          setAndLockPref(
            "pref.browser.homepage.disable_button.current_page",
            true
          );
          setAndLockPref(
            "pref.browser.homepage.disable_button.bookmark_page",
            true
          );
          setAndLockPref(
            "pref.browser.homepage.disable_button.restore_default",
            true
          );
        } else {
          // Clear out old run once modification that is no longer used.
          clearRunOnceModification("setHomepage");
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
        PoliciesUtils.setDefaultPref(
          "browser.startup.page",
          prefValue,
          param.StartPage == "homepage-locked"
        );
      }
      if ("NewTabOnRestore" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.sessionstore.newTabOnRestore",
          param.NewTabOnRestore
        );
        // Make the UI visible so the user can see what's been configured
        // on their behalf, regardless of the configured value.
        PoliciesUtils.setDefaultPref(
          "browser.sessionstore.newTabOnRestore.showSetting",
          true
        );
      }
    },
  },

  HttpAllowlist: {
    onBeforeAddons(manager, param) {
      addAllowDenyPermissions("https-only-load-insecure", param);
    },
  },

  HttpsOnlyMode: {
    onBeforeAddons(manager, param) {
      switch (param) {
        case "disallowed":
          setAndLockPref("dom.security.https_only_mode", false);
          break;
        case "enabled":
          PoliciesUtils.setDefaultPref("dom.security.https_only_mode", true);
          break;
        case "force_enabled":
          setAndLockPref("dom.security.https_only_mode", true);
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
        addAllowDenyPermissions("install", param.Allow, null);
      }
      if ("Default" in param) {
        setAndLockPref("xpinstall.enabled", param.Default);
        if (!param.Default) {
          manager.disallowFeature("installTemporaryAddon");
          setAndLockPref(
            "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
            false
          );
          setAndLockPref(
            "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features",
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
        setAndLockPref("browser.ipProtection.enabled", false);
      }
    },
  },

  LegacyProfiles: {
    // Handled in nsToolkitProfileService.cpp (Windows only)
  },

  LegacySameSiteCookieBehaviorEnabled: {
    onBeforeAddons(manager, param) {
      PoliciesUtils.setDefaultPref(
        "network.cookie.sameSite.laxByDefault",
        !param
      );
    },
  },

  LegacySameSiteCookieBehaviorEnabledForDomainList: {
    onBeforeAddons(manager, param) {
      PoliciesUtils.setDefaultPref(
        "network.cookie.sameSite.laxByDefault.disabledHosts",
        param.join(",")
      );
    },
  },

  LocalFileLinks: {
    onBeforeAddons(manager, param) {
      // If there are existing capabilities, lock them with the policy pref.
      let policyNames = Services.prefs
        .getCharPref("capability.policy.policynames", "")
        .split(" ");
      policyNames.push("localfilelinks_policy");
      setAndLockPref("capability.policy.policynames", policyNames.join(" "));
      setAndLockPref(
        "capability.policy.localfilelinks_policy.checkloaduri.enabled",
        "allAccess"
      );
      setAndLockPref(
        "capability.policy.localfilelinks_policy.sites",
        param.join(" ")
      );
    },
  },

  LocalNetworkAccess: {
    onBeforeAddons(manager, param) {
      // Only process if "Enabled" is explicitly specified
      if ("Enabled" in param) {
        PoliciesUtils.setDefaultPref(
          "network.lna.enabled",
          param.Enabled,
          param.Locked
        );

        if (param.Enabled === false) {
          // If LNA is explicitly disabled, disable other features too
          PoliciesUtils.setDefaultPref(
            "network.lna.block_trackers",
            false,
            param.Locked
          );
          PoliciesUtils.setDefaultPref(
            "network.lna.blocking",
            false,
            param.Locked
          );
        } else {
          // LNA is enabled - handle fine-grained controls
          // For backward compatibility, default to true if not specified
          let blockTrackers =
            "BlockTrackers" in param ? param.BlockTrackers : true;
          let enablePrompting =
            "EnablePrompting" in param ? param.EnablePrompting : true;

          PoliciesUtils.setDefaultPref(
            "network.lna.block_trackers",
            blockTrackers,
            param.Locked
          );
          PoliciesUtils.setDefaultPref(
            "network.lna.blocking",
            enablePrompting,
            param.Locked
          );
        }
      }

      // Handle SkipDomains separately (can be set independently of Enabled)
      if ("SkipDomains" in param && Array.isArray(param.SkipDomains)) {
        let skipDomainsValue = param.SkipDomains.join(",");
        PoliciesUtils.setDefaultPref(
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
      setAndLockPref("network.http.microsoft-entra-sso.enabled", param);
    },
  },

  NetworkPrediction: {
    onBeforeAddons(manager, param) {
      setAndLockPref("network.dns.disablePrefetch", !param);
      setAndLockPref("network.dns.disablePrefetchFromHTTPS", !param);
    },
  },

  NewTabPage: {
    onBeforeAddons(manager, param) {
      setAndLockPref("browser.newtabpage.enabled", param);
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
      setAndLockPref("signon.rememberSignons", param);
      setAndLockPref("services.passwordSavingEnabled", param);
    },
  },

  OfferToSaveLoginsDefault: {
    onBeforeUIStartup(manager, param) {
      let policies = Services.policies.getActivePolicies();
      if ("OfferToSaveLogins" in policies) {
        lazy.log.error(
          `OfferToSaveLoginsDefault ignored because OfferToSaveLogins is present.`
        );
      } else {
        PoliciesUtils.setDefaultPref("signon.rememberSignons", param);
      }
    },
  },

  OverrideFirstRunPage: {
    onProfileAfterChange(manager, param) {
      let url = param ? param : "";
      setAndLockPref("startup.homepage_welcome_url", url);
      setAndLockPref("browser.aboutwelcome.enabled", false);
    },
  },

  OverridePostUpdatePage: {
    onProfileAfterChange(manager, param) {
      let url = param ? param.href : "";
      setAndLockPref("startup.homepage_override_url", url);
      // The pref startup.homepage_override_url is only used
      // as a fallback when the update.xml file hasn't provided
      // a specific post-update URL.
      manager.disallowFeature("postUpdateCustomPage");
    },
  },

  PasswordManagerEnabled: {
    onBeforeUIStartup(manager, param) {
      if (!param) {
        blockAboutPage(manager, "about:logins", true);
        setAndLockPref("pref.privacy.disable_button.view_passwords", true);
        setAndLockPref("browser.contextual-password-manager.enabled", false);
      }
      setAndLockPref("signon.rememberSignons", param);
    },
  },

  PasswordManagerExceptions: {
    onBeforeUIStartup(manager, param) {
      addAllowDenyPermissions("login-saving", null, param);
    },
  },

  PDFjs: {
    onBeforeAddons(manager, param) {
      if ("Enabled" in param) {
        setAndLockPref("pdfjs.disabled", !param.Enabled);
      }
      if ("EnablePermissions" in param) {
        setAndLockPref("pdfjs.enablePermissions", param.EnablePermissions);
      }
    },
  },

  Permissions: {
    onBeforeUIStartup(manager, param) {
      if (param.Camera) {
        addAllowDenyPermissions(
          "camera",
          param.Camera.Allow,
          param.Camera.Block
        );
        setDefaultPermission("camera", param.Camera);
      }

      if (param.Microphone) {
        addAllowDenyPermissions(
          "microphone",
          param.Microphone.Allow,
          param.Microphone.Block
        );
        setDefaultPermission("microphone", param.Microphone);
      }

      if (param.Autoplay) {
        addAllowDenyPermissions(
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
          PoliciesUtils.setDefaultPref(
            "media.autoplay.default",
            prefValue,
            param.Autoplay.Locked
          );
        }
      }

      if (param.Location) {
        addAllowDenyPermissions(
          "geo",
          param.Location.Allow,
          param.Location.Block
        );
        setDefaultPermission("geo", param.Location);
      }

      if (param.Notifications) {
        addAllowDenyPermissions(
          "desktop-notification",
          param.Notifications.Allow,
          param.Notifications.Block
        );
        setDefaultPermission("desktop-notification", param.Notifications);
      }

      if ("VirtualReality" in param) {
        addAllowDenyPermissions(
          "xr",
          param.VirtualReality.Allow,
          param.VirtualReality.Block
        );
        setDefaultPermission("xr", param.VirtualReality);
      }

      if ("ScreenShare" in param) {
        addAllowDenyPermissions(
          "screen",
          param.ScreenShare.Allow,
          param.ScreenShare.Block
        );
        setDefaultPermission("screen", param.ScreenShare);
      }
    },
  },

  PictureInPicture: {
    onBeforeAddons(manager, param) {
      if ("Enabled" in param) {
        PoliciesUtils.setDefaultPref(
          "media.videocontrols.picture-in-picture.video-toggle.enabled",
          param.Enabled
        );
      }
      if (param.Locked) {
        Services.prefs.lockPref(
          "media.videocontrols.picture-in-picture.video-toggle.enabled"
        );
      }
    },
  },

  PopupBlocking: {
    onBeforeUIStartup(manager, param) {
      addAllowDenyPermissions("popup", param.Allow, null);

      if (param.Locked) {
        let blockValue = true;
        if (param.Default !== undefined && !param.Default) {
          blockValue = false;
        }
        setAndLockPref("dom.disable_open_during_load", blockValue);
        setAndLockPref(
          "dom.security.framebusting_intervention.enabled",
          blockValue
        );
      } else if (param.Default !== undefined) {
        PoliciesUtils.setDefaultPref(
          "dom.disable_open_during_load",
          !!param.Default
        );
        PoliciesUtils.setDefaultPref(
          "dom.security.framebusting_intervention.enabled",
          !!param.Default
        );
      }
    },
  },

  PostQuantumKeyAgreementEnabled: {
    onBeforeAddons(manager, param) {
      setAndLockPref("network.http.http3.enable_kyber", param);
      setAndLockPref("security.tls.enable_kyber", param);
      setAndLockPref("media.webrtc.enable_pq_hybrid_kex", param);
    },
  },

  Preferences: {
    onBeforeAddons(manager, param) {
      let allowedPrefixes = [
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

      for (let preference in param) {
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
            setAndLockPref(preference, param[preference]);
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
          let prefWasLocked = Services.prefs.prefIsLocked(preference);
          if (prefWasLocked) {
            Services.prefs.unlockPref(preference);
          }
          try {
            let prefType =
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
      setAndLockPref("print.enabled", param);
    },
  },

  PrivateBrowsingModeAvailability: {
    onBeforeAddons(manager, param) {
      switch (param) {
        // Private Browsing mode disabled
        case 1:
          manager.disallowFeature("privatebrowsing");
          blockAboutPage(manager, "about:privatebrowsing", true);
          setAndLockPref("browser.privatebrowsing.autostart", false);
          break;
        // Private Browsing mode forced
        case 2:
          setAndLockPref("browser.privatebrowsing.autostart", true);
          break;
        // Private Browsing mode available
        case 0:
          break;
      }
    },
  },

  PromptForDownloadLocation: {
    onBeforeAddons(manager, param) {
      setAndLockPref("browser.download.useDownloadDir", !param);
    },
  },

  Proxy: {
    onBeforeAddons(manager, param) {
      if (param.Locked) {
        manager.disallowFeature("changeProxySettings");
      }
      lazy.ProxyPolicies.configureProxySettings(
        param,
        PoliciesUtils.setDefaultPref
      );
    },
  },

  RelaunchRequired: {
    onBeforeUIStartup(_manager, param) {
      let notificationPeriodHours = 24;
      let restartTimeOfDay = { Hour: 12, Minute: 0 };
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
      setAndLockPref(
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
      runOncePerModification(
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
        setAndLockPref("privacy.sanitize.sanitizeOnShutdown", param);
        setAndLockPref("privacy.clearOnShutdown.cache", param);
        setAndLockPref("privacy.clearOnShutdown.cookies", param);
        setAndLockPref("privacy.clearOnShutdown.downloads", param);
        setAndLockPref("privacy.clearOnShutdown.formdata", param);
        setAndLockPref("privacy.clearOnShutdown.history", param);
        setAndLockPref("privacy.clearOnShutdown.sessions", param);
        setAndLockPref("privacy.clearOnShutdown.siteSettings", param);
        setAndLockPref("privacy.clearOnShutdown.offlineApps", param);
        setAndLockPref(
          "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
          param
        );
        setAndLockPref("privacy.clearOnShutdown_v2.cookiesAndStorage", param);
        setAndLockPref("privacy.clearOnShutdown_v2.cache", param);
        setAndLockPref("privacy.clearOnShutdown_v2.siteSettings", param);
        setAndLockPref("privacy.clearOnShutdown_v2.formdata", param);
      } else {
        let locked = true;
        // Needed to preserve original behavior in perpetuity.
        let lockDefaultPrefs = true;
        if ("Locked" in param) {
          locked = param.Locked;
          lockDefaultPrefs = false;
        }
        PoliciesUtils.setDefaultPref(
          "privacy.sanitize.sanitizeOnShutdown",
          true,
          locked
        );
        if ("Cache" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cache",
            param.Cache,
            locked
          );
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cache",
            param.Cache,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cache",
            false,
            lockDefaultPrefs
          );
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cache",
            false,
            lockDefaultPrefs
          );
        }
        if ("Cookies" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cookies",
            param.Cookies,
            locked
          );

          // We set cookiesAndStorage to follow lock and pref
          // settings for cookies, and deprecate offlineApps
          // and sessions in the new clear on shutdown dialog - Bug 1853996
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cookiesAndStorage",
            param.Cookies,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.cookies",
            false,
            lockDefaultPrefs
          );
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.cookiesAndStorage",
            false,
            lockDefaultPrefs
          );
        }
        if ("Downloads" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.downloads",
            param.Downloads,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.downloads",
            false,
            lockDefaultPrefs
          );
        }
        if ("FormData" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.formdata",
            param.FormData,
            locked
          );

          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.formdata",
            param.FormData,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.formdata",
            false,
            lockDefaultPrefs
          );

          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.formdata",
            false,
            lockDefaultPrefs
          );
        }
        if ("History" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.history",
            param.History,
            locked
          );

          // We set browsingHistoryAndDownloads to follow lock and pref
          // settings for history, and deprecate downloads
          // in the new clear on shutdown dialog - Bug 1853996
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
            param.History,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.history",
            false,
            lockDefaultPrefs
          );
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads",
            false,
            lockDefaultPrefs
          );
        }
        if ("Sessions" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.sessions",
            param.Sessions,
            locked
          );
        } else {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.sessions",
            false,
            lockDefaultPrefs
          );
        }
        if ("SiteSettings" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.siteSettings",
            param.SiteSettings,
            locked
          );
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown_v2.siteSettings",
            param.SiteSettings,
            locked
          );
        }
        if ("OfflineApps" in param) {
          PoliciesUtils.setDefaultPref(
            "privacy.clearOnShutdown.offlineApps",
            param.OfflineApps,
            locked
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
      runOncePerModification("searchInNavBar", param, () => {
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
          await runOncePerModification(
            "removeSearchEngines",
            JSON.stringify(param.Remove),
            async function () {
              for (let engineName of param.Remove) {
                let engine = lazy.SearchService.getEngineByName(engineName);
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
          await runOncePerModification(
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
          await runOncePerModification(
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
      setAndLockPref("browser.urlbar.suggest.searches", param);
      setAndLockPref("browser.search.suggest.enabled", param);
    },
  },

  SecurityDevices: {
    async _onProfileAfterChangeImpl(manager, param) {
      let pkcs11db = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
        Ci.nsIPKCS11ModuleDB
      );
      let securityDevices;
      if (param.Add || param.Delete) {
        // We're using the new syntax.
        securityDevices = param.Add;
        if (param.Delete) {
          for (let deviceName of param.Delete) {
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
      for (let deviceName in securityDevices) {
        let foundModule = false;
        for (let module of await pkcs11db.listModules()) {
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
        let homeButtonPlacement =
          lazy.CustomizableUI.getPlacementOfWidget("home-button");
        if (!homeButtonPlacement) {
          let placement =
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
      let site = Services.eTLD.getBaseDomainFromHost(base);
      if (site != base) {
        console.warn(
          `SitePolicies: Pattern ${pattern} is too specific. Using *.${site} instead.`
        );
      }

      return new MatchPattern(`*://*.${site}/*`);
    },

    validate(params) {
      // The schema will have validated the general structure, we need to validate the patterns
      for (let param of params) {
        for (let patterns of [param.Match ?? [], param.Exceptions ?? []]) {
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
      let features = {};

      if ("DisableJit" in policies) {
        features.jit = !policies.DisableJit;
      }

      return features;
    },

    onBeforeAddons(manager, params) {
      let sitePolicies = [];

      for (let policies of params) {
        let matches = policies.Match ?? [];
        let exceptions = policies.Exceptions ?? [];

        // If the entire web is an exception then this policy can never apply so
        // ignore it.
        if (exceptions.includes("*")) {
          continue;
        }

        let exceptionPatterns = exceptions.map(this.intoMatchPattern);
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

        sitePolicies.push({
          match: new MatchPatternSet(matchPatterns),
          exceptions: new MatchPatternSet(exceptionPatterns),
          features: this.featuresForPolicies(policies.Policies),
        });
      }

      manager.updateSitePolicies(sitePolicies);
    },
  },

  SkipTermsOfUse: {
    onBeforeAddons(manager, param) {
      if (param) {
        setAndLockPref("termsofuse.acceptedVersion", 999);
        setAndLockPref("termsofuse.acceptedDate", Date.now().toString());
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
      setAndLockPref("security.tls.version.max", tlsVersion);
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
      setAndLockPref("security.tls.version.min", tlsVersion);
    },
  },

  StartDownloadsInTempDirectory: {
    onBeforeAddons(manager, param) {
      setAndLockPref("browser.download.start_downloads_in_tmp_dir", param);
    },
  },

  SupportMenu: {
    onProfileAfterChange(manager, param) {
      manager.setSupportMenu(param);
    },
  },

  TranslateEnabled: {
    onBeforeAddons(manager, param) {
      let policies = Services.policies.getActivePolicies();
      if (policies.AIControls?.Translations || policies.AIControls?.Default) {
        lazy.log.warn(
          "Ignoring TranslateEnabled policy in favor of AIControls"
        );
        return;
      }
      setAndLockPref("browser.translations.enable", param);
      setAndLockPref(
        "browser.ai.control.translations",
        param ? "enabled" : "blocked"
      );
    },
  },

  UserMessaging: {
    onBeforeAddons(manager, param) {
      if ("ExtensionRecommendations" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons",
          param.ExtensionRecommendations,
          param.Locked
        );
      }
      if ("FeatureRecommendations" in param) {
        PoliciesUtils.setDefaultPref(
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

        PoliciesUtils.setDefaultPref(
          "browser.translations.mostRecentTargetLanguages",
          param.FeatureRecommendations ? "" : preferredLanguage,
          param.Locked
        );
      }
      if ("UrlbarInterventions" in param && !param.UrlbarInterventions) {
        manager.disallowFeature("urlbarinterventions");
      }
      if ("SkipOnboarding" in param) {
        PoliciesUtils.setDefaultPref(
          "browser.aboutwelcome.enabled",
          !param.SkipOnboarding,
          param.Locked
        );
      }
      if ("MoreFromMozilla" in param) {
        PoliciesUtils.setDefaultPref(
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
      setAndLockPref("print.prefer_system_dialog", param);
    },
  },

  VisualSearchEnabled: {
    onBeforeAddons(manager, param) {
      setAndLockPref("browser.search.visualSearch.featureGate", param);
    },
  },

  WebsiteFilter: {
    onBeforeUIStartup(manager, param) {
      lazy.WebsiteFilter.init(param.Block || [], param.Exceptions || []);
    },
  },

  WindowsSSO: {
    onBeforeAddons(manager, param) {
      setAndLockPref("network.http.windows-sso.enabled", param);
    },
  },

  XSLTEnabled: {
    onBeforeAddons(manager, param) {
      setAndLockPref("dom.xslt.enabled", param);
    },
  },
};

/*
 * ====================
 * = HELPER FUNCTIONS =
 * ====================
 *
 * The functions below are helpers to be used by several policies.
 */

/**
 * setAndLockPref
 *
 * Sets the _default_ value of a pref, and locks it (meaning that
 * the default value will always be returned, independent from what
 * is stored as the user value).
 * The value is only changed in memory, and not stored to disk.
 *
 * @param {string} prefName
 *        The pref to be changed
 * @param {boolean|number|string} prefValue
 *        The value to set and lock
 */
export function setAndLockPref(prefName, prefValue) {
  PoliciesUtils.setDefaultPref(prefName, prefValue, true);
}

/**
 *
 * setPrefIfPresentAndLock
 *
 * Sets the pref to the value param[paramKey] if that exists. Either
 * way, the pref is locked.
 *
 * @param {object} param
 *        Object with pref values
 * @param {string} paramKey
 *        The key to look up the value in param
 * @param {string} prefName
 *        The pref to be changed
 */
function setPrefIfPresentAndLock(param, paramKey, prefName) {
  if (paramKey in param) {
    setAndLockPref(prefName, param[paramKey]);
  } else {
    Services.prefs.lockPref(prefName);
  }
}

/**
 * setDefaultPref
 *
 * Sets the _default_ value of a pref and optionally locks it.
 * The value is only changed in memory, and not stored to disk.
 *
 * @param {string} prefName
 *        The pref to be changed
 * @param {boolean|number|string} prefValue
 *        The value to set
 * @param {boolean} locked
 *        Optionally lock the pref
 */

export var PoliciesUtils = {
  setDefaultPref(prefName, prefValue, locked) {
    let prefWasLocked = Services.prefs.prefIsLocked(prefName);
    if (prefWasLocked) {
      Services.prefs.unlockPref(prefName);
    }

    let defaults = Services.prefs.getDefaultBranch("");

    switch (typeof prefValue) {
      case "boolean":
        defaults.setBoolPref(prefName, prefValue);
        break;

      case "number":
        if (!Number.isInteger(prefValue)) {
          throw new Error(`Non-integer value for ${prefName}`);
        }

        // This is ugly, but necessary. On Windows GPO and macOS
        // configs, booleans are converted to 0/1. In the previous
        // Preferences implementation, the schema took care of
        // automatically converting these values to booleans.
        // Since we allow arbitrary prefs now, we have to do
        // something different. See bug 1666836.
        if (
          defaults.getPrefType(prefName) == defaults.PREF_INT ||
          ![0, 1].includes(prefValue)
        ) {
          defaults.setIntPref(prefName, prefValue);
        } else {
          defaults.setBoolPref(prefName, !!prefValue);
        }
        break;

      case "string":
        defaults.setStringPref(prefName, prefValue);
        break;
    }

    // Prefs can only be unlocked explicitly.
    // If they were locked before, they stay locked.
    if (locked || (prefWasLocked && locked !== false)) {
      Services.prefs.lockPref(prefName);
    }
  },
};

/**
 * setDefaultPermission
 *
 * Helper function to set preferences appropriately for the policy
 *
 * @param {string} policyName
 *        The name of the policy to set
 * @param {object} policyParam
 *        The object containing param for the policy
 */
function setDefaultPermission(policyName, policyParam) {
  if ("BlockNewRequests" in policyParam) {
    let prefName = "permissions.default." + policyName;

    if (policyParam.BlockNewRequests) {
      PoliciesUtils.setDefaultPref(prefName, 2, policyParam.Locked);
    } else {
      PoliciesUtils.setDefaultPref(prefName, 0, policyParam.Locked);
    }
  }
}

/**
 * addAllowDenyPermissions
 *
 * Helper function to call the permissions manager (Services.perms.addFromPrincipal)
 * for two arrays of URLs.
 *
 * @param {string} permissionName
 *        The name of the permission to change
 * @param {Array} allowList
 *        The list of URLs to be set as ALLOW_ACTION for the chosen permission.
 * @param {Array} blockList
 *        The list of URLs to be set as DENY_ACTION for the chosen permission.
 */
function addAllowDenyPermissions(permissionName, allowList, blockList) {
  allowList = allowList || [];
  blockList = blockList || [];

  for (let origin of allowList) {
    try {
      lazy.addPolicyPermission(
        origin,
        permissionName,
        Ci.nsIPermissionManager.ALLOW_ACTION
      );
    } catch (ex) {
      // It's possible if the origin was invalid, we'll have a string instead of an origin.
      lazy.log.error(
        `Unable to add ${permissionName} permission for ${
          origin.href || origin
        }`
      );
    }
  }

  for (let origin of blockList) {
    lazy.addPolicyPermission(
      origin,
      permissionName,
      Ci.nsIPermissionManager.DENY_ACTION
    );
  }
}

/**
 * runOnce
 *
 * Helper function to run a callback only once per policy.
 *
 * @param {string} actionName
 *        A given name which will be used to track if this callback has run.
 * @param {Functon} callback
 *        The callback to run only once.
 */
// eslint-disable-next-line no-unused-vars
export function runOnce(actionName, callback) {
  let prefName = `browser.policies.runonce.${actionName}`;
  if (Services.prefs.getBoolPref(prefName, false)) {
    lazy.log.debug(
      `Not running action ${actionName} again because it has already run.`
    );
    return;
  }
  Services.prefs.setBoolPref(prefName, true);
  callback();
}

/**
 * runOncePerModification
 *
 * Helper function similar to runOnce. The difference is that runOnce runs the
 * callback once when the policy is set, then never again.
 * runOncePerModification runs the callback once each time the policy value
 * changes from its previous value.
 * If the callback that was passed is an async function, you can await on this
 * function to await for the callback.
 *
 * The last applied value is stored in an unlocked user pref, so this helper
 * only suits settings the user is allowed to change afterwards. A policy that
 * enforces something must instead check the state it controls at every
 * startup.
 *
 * @param {string} actionName
 *        A given name which will be used to track if this callback has run.
 *        This string will be part of a pref name.
 * @param {string} policyValue
 *        The current value of the policy. This will be compared to previous
 *        values given to this function to determine if the policy value has
 *        changed. Regardless of the data type of the policy, this must be a
 *        string.
 * @param {Function} callback
 *        The callback to be run when the pref value changes
 * @returns {Promise}
 *        A promise that will resolve once the callback finishes running.
 */
export async function runOncePerModification(
  actionName,
  policyValue,
  callback
) {
  // Stringify the value so that it matches what we'd get from getStringPref.
  policyValue = policyValue + "";
  if (isRunOnceModificationApplied(actionName, policyValue)) {
    lazy.log.debug(
      `Not running action ${actionName} again because the policy's value is unchanged`
    );
    return Promise.resolve();
  }
  setRunOnceModificationApplied(actionName, policyValue);
  return callback();
}

/**
 * setRunOnceModificationApplied
 *
 * Records a value as the one runOncePerModification last applied for this
 * action, without running anything. A policy that has to act on every
 * startup uses this to record its list while still doing the work itself.
 *
 * @param {string} actionName
 *        The name given to runOncePerModification for the action.
 * @param {string} policyValue
 *        The value to record, in the same form that is given to
 *        runOncePerModification.
 */
export function setRunOnceModificationApplied(actionName, policyValue) {
  let prefName = `browser.policies.runOncePerModification.${actionName}`;
  Services.prefs.setStringPref(prefName, policyValue + "");
}

/**
 * isRunOnceModificationApplied
 *
 * Whether runOncePerModification has already applied this value for this
 * action, which is when it would skip its callback. A policy can use this to
 * find out whether one of its lists changed before it reaches the
 * runOncePerModification step for that list.
 *
 * @param {string} actionName
 *        The name given to runOncePerModification for the action.
 * @param {string} policyValue
 *        The value to compare with the last applied one, in the same form
 *        that is given to runOncePerModification.
 * @returns {boolean}
 *        Whether the value is the last applied one.
 */
export function isRunOnceModificationApplied(actionName, policyValue) {
  let prefName = `browser.policies.runOncePerModification.${actionName}`;
  return Services.prefs.getStringPref(prefName, undefined) === policyValue + "";
}

/**
 * clearRunOnceModification
 *
 * Helper function that clears a runOnce policy.
 */
export function clearRunOnceModification(actionName) {
  let prefName = `browser.policies.runOncePerModification.${actionName}`;
  Services.prefs.clearUserPref(prefName);
}

function replacePathVariables(path) {
  if (path.includes("${home}")) {
    return path.replace(
      "${home}",
      Services.dirsvc.get("Home", Ci.nsIFile).path
    );
  }
  return path;
}

/**
 * Validates a list of host patterns intended for runtime_blocked_hosts or
 * runtime_allowed_hosts. These accept match patterns without a path
 * component (e.g. `*://*.example.com` or `<all_urls>`), matching Chrome's
 * ExtensionSettings policy format. Throws on the first invalid pattern.
 */
function validateExtensionGuardPatterns(patterns) {
  for (let pattern of patterns) {
    if (typeof pattern !== "string") {
      throw new Error(`Expected a string, got ${typeof pattern}`);
    }
    if (pattern === "<all_urls>") {
      continue;
    }
    if (!/^[a-z*][a-z0-9*+.-]*:\/\/[^/]+$/i.test(pattern)) {
      throw new Error(`Host pattern must not include a path: ${pattern}`);
    }
  }
  // MatchPatternSet throws on any other malformed pattern (bad scheme,
  // malformed host, etc.). ignorePath:true mirrors the backend's parser
  // in setEnterpriseGuards; restrictSchemes defaults to true.
  new MatchPatternSet(patterns, { ignorePath: true });
}

/**
 * Build the enterprise guards map from an ExtensionSettings policy value and
 * apply it via setEnterpriseGuards from ExtensionPermissions.sys.mjs.
 *
 * Throws if any pattern is malformed; the caller logs and skips applying
 * guards in that case.
 *
 * @param {object} extensionSettings
 *        An object mapping extension ID to settings. Settings are defining
 *        runtime_blocked_hosts and runtime_allowed_hosts properties.
 */
function applyExtensionGuards(extensionSettings) {
  let guards = {};
  for (let [extensionID, settings] of Object.entries(extensionSettings)) {
    let blocked = settings.runtime_blocked_hosts;
    let allowed = settings.runtime_allowed_hosts;
    if (!blocked && !allowed) {
      continue;
    }
    validateExtensionGuardPatterns(blocked ?? []);
    validateExtensionGuardPatterns(allowed ?? []);
    guards[extensionID] = {
      runtime_blocked_hosts: blocked ?? [],
      runtime_allowed_hosts: allowed ?? [],
    };
  }
  lazy.setEnterpriseGuards(guards);
}

/**
 * installAddonFromRepository
 *
 * Helper function that installs an addon from addons.mozilla.org.
 *
 * @param {string} extensionID The extension ID that is to be installed.
 * @param {string} [policyName] The policy requesting the installation.
 */
function installAddonFromRepository(extensionID, policyName) {
  lazy.AddonRepository.getAddonsByIDs([extensionID])
    .then(repoAddons => {
      if (!repoAddons[0]?.sourceURI) {
        lazy.reportFailure(
          policyName,
          `No XPI URL found on AMO for ${extensionID}. Please use install_url for add-ons not listed on addons.mozilla.org.`
        );
        return;
      }
      installAddonFromURL(
        repoAddons[0].sourceURI.spec,
        extensionID,
        null,
        policyName
      );
    })
    .catch(err => {
      lazy.reportFailure(
        policyName,
        `Failed to retrieve ${extensionID} from AMO: ${err}`
      );
    });
}

/**
 * installAddonFromURL
 *
 * Helper function that installs an addon from a URL
 * and verifies that the addon ID matches.
 *
 * @param {string} url The URL to install from.
 * @param {string} extensionID The extension ID that is to be installed.
 * @param {object|null} addon Object representing the addon.
 * @param {string} [policyName] The policy requesting the installation.
 */
function installAddonFromURL(url, extensionID, addon, policyName) {
  if (
    addon &&
    addon.sourceURI &&
    addon.sourceURI.spec == url &&
    !addon.sourceURI.schemeIs("file")
  ) {
    // It's the same addon, don't reinstall.
    return;
  }
  lazy.AddonManager.getInstallForURL(url, {
    telemetryInfo: { source: "enterprise-policy" },
  })
    .then(install => {
      if (!install) {
        lazy.reportFailure(policyName, `Unable to install add-on from ${url}`);
        return;
      }
      if (install.addon && install.addon.appDisabled) {
        lazy.reportFailure(
          policyName,
          `Incompatible add-on - ${install.addon.id}`
        );
        install.cancel();
        return;
      }
      let listener = {
        /* eslint-disable-next-line no-shadow */
        onDownloadEnded: install => {
          // Install failed, error will be reported elsewhere.
          if (!install.addon) {
            return;
          }
          if (extensionID && install.addon.id != extensionID) {
            lazy.reportFailure(
              policyName,
              `Add-on downloaded from ${url} had unexpected id (got ${install.addon.id} expected ${extensionID})`
            );
            install.removeListener(listener);
            install.cancel();
            return;
          }
          if (install.addon.appDisabled) {
            lazy.reportFailure(policyName, `Incompatible add-on - ${url}`);
            install.removeListener(listener);
            install.cancel();
            return;
          }
          if (
            addon &&
            Services.vc.compare(addon.version, install.addon.version) == 0
          ) {
            lazy.log.debug(
              "Installation cancelled because versions are the same"
            );
            install.removeListener(listener);
            install.cancel();
            return;
          }

          // Cancel install if the addon version downloaded is detected
          // to be a downgrade compared to the version already installed.
          if (
            addon &&
            Services.vc.compare(addon.version, install.addon.version) > 0
          ) {
            lazy.log.warn(
              `Installation cancelled because installed version ${addon.version} is greater than ${install.addon.version} downloaded from ${url}`
            );
            install.removeListener(listener);
            install.cancel();
          }
        },
        onDownloadFailed: () => {
          install.removeListener(listener);
          lazy.reportFailure(
            policyName,
            `Download failed - ${lazy.AddonManager.errorToString(
              install.error
            )} - ${url}`
          );
          clearRunOnceModification("extensionsInstall");
        },
        onInstallFailed: () => {
          install.removeListener(listener);
          lazy.reportFailure(
            policyName,
            `Installation failed - ${lazy.AddonManager.errorToString(
              install.error
            )} - ${url}`
          );
        },
        /* eslint-disable-next-line no-shadow */
        onInstallEnded: (install, addon) => {
          if (addon.type == "theme") {
            addon.enable();
          }
          install.removeListener(listener);
          lazy.log.debug(`Installation succeeded - ${url}`);
        },
      };
      // If it's a local file install, onDownloadEnded is never called.
      // So we call it manually, to handle some error cases.
      if (url.startsWith("file:")) {
        listener.onDownloadEnded(install);
        if (install.state == lazy.AddonManager.STATE_CANCELLED) {
          return;
        }
      }
      install.addListener(listener);
      install.install();
    })
    .catch(e => {
      lazy.reportFailure(
        policyName,
        `Unable to install add-on from ${url}: ${e.message ?? e}`
      );
    });
}

/**
 * Uninstalls the add-ons an Extensions.Uninstall list names whenever they are
 * present, so a run-once marker pre-seeded in the profile cannot keep one
 * installed. An ID that ExtensionSettings installs is reported as a conflict
 * and skipped, whether or not it is installed yet.
 *
 * While neither the Uninstall list nor the Install list has changed since it
 * was last applied, an add-on that a policy installed is left alone. That is
 * what lets an administrator update an add-on by listing it in both Uninstall
 * and Install without it being downloaded again at every startup. As soon as
 * either list changes, everything listed is uninstalled, so an add-on the
 * Install list no longer covers does not linger. Install records URLs rather
 * than IDs, so which policy installed an add-on cannot be told: one that
 * ExtensionSettings installed stays protected after its entry is removed.
 *
 * @param {string[]} ids
 *        The IDs of the add-ons to uninstall.
 * @param {string[]} [installList]
 *        The policy's Install list, if it has one.
 * @returns {Promise<boolean>}
 *        Whether the Uninstall list changed since it was last applied.
 */
async function uninstallListedAddons(ids, installList = []) {
  let uninstallList = JSON.stringify(ids);
  let listChanged = !isRunOnceModificationApplied(
    "extensionsUninstall",
    uninstallList
  );
  setRunOnceModificationApplied("extensionsUninstall", uninstallList);
  let keepPolicyInstalls =
    !listChanged &&
    !!installList.length &&
    isRunOnceModificationApplied(
      "extensionsInstall",
      JSON.stringify(installList)
    );
  let candidates = [...new Set(ids)].filter(id => {
    let mode = Services.policies.getExtensionSettings(id)?.installation_mode;
    if (mode == "force_installed" || mode == "normal_installed") {
      lazy.reportFailure(
        "Extensions",
        `Not uninstalling ${id} because ExtensionSettings installs it`
      );
      return false;
    }
    return true;
  });
  let addons = await lazy.AddonManager.getAddonsByIDs(candidates);
  for (let addon of addons) {
    if (!addon) {
      continue;
    }
    if (
      keepPolicyInstalls &&
      addon.installTelemetryInfo?.source == "enterprise-policy"
    ) {
      continue;
    }
    try {
      await addon.uninstall();
    } catch (e) {
      lazy.reportFailure(
        "Extensions",
        `Add-on ID (${addon.id}) couldn't be uninstalled: ${e.message ?? e}`
      );
    }
  }
  return listChanged;
}

let gBlockedAboutPages = [];

function clearBlockedAboutPages() {
  gBlockedAboutPages = [];
}

function blockAboutPage(manager, feature) {
  addChromeURLBlocker();
  gBlockedAboutPages.push(feature);

  try {
    let aboutModule = Cc[ABOUT_CONTRACT + feature.split(":")[1]].getService(
      Ci.nsIAboutModule
    );
    let chromeURL = aboutModule.getChromeURI(Services.io.newURI(feature)).spec;
    gBlockedAboutPages.push(chromeURL);
  } catch (e) {
    // Some about pages don't have chrome URLS (compat)
  }
}

let ChromeURLBlockPolicy = {
  shouldLoad(contentLocation, loadInfo) {
    let contentType = loadInfo.externalContentPolicyType;
    if (
      (contentLocation.scheme != "chrome" &&
        contentLocation.scheme != "about") ||
      (contentType != Ci.nsIContentPolicy.TYPE_DOCUMENT &&
        contentType != Ci.nsIContentPolicy.TYPE_SUBDOCUMENT)
    ) {
      return Ci.nsIContentPolicy.ACCEPT;
    }
    let contentLocationSpec = contentLocation.spec.toLowerCase();
    if (
      gBlockedAboutPages.some(function (aboutPage) {
        return contentLocationSpec.startsWith(aboutPage.toLowerCase());
      })
    ) {
      return Ci.nsIContentPolicy.REJECT_POLICY;
    }
    return Ci.nsIContentPolicy.ACCEPT;
  },
  shouldProcess() {
    return Ci.nsIContentPolicy.ACCEPT;
  },
  classDescription: "Policy Engine Content Policy",
  contractID: "@mozilla-org/policy-engine-content-policy-service;1",
  classID: Components.ID("{ba7b9118-cabc-4845-8b26-4215d2a59ed7}"),
  QueryInterface: ChromeUtils.generateQI(["nsIContentPolicy"]),
  createInstance(iid) {
    return this.QueryInterface(iid);
  },
};

function addChromeURLBlocker() {
  if (Cc[ChromeURLBlockPolicy.contractID]) {
    return;
  }

  let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
  registrar.registerFactory(
    ChromeURLBlockPolicy.classID,
    ChromeURLBlockPolicy.classDescription,
    ChromeURLBlockPolicy.contractID,
    ChromeURLBlockPolicy
  );

  Services.catMan.addCategoryEntry(
    "content-policy",
    ChromeURLBlockPolicy.contractID,
    ChromeURLBlockPolicy.contractID,
    false,
    true
  );
}

function pemToBase64(pem) {
  return pem
    .replace(/(.*)-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----(.*)/, "")
    .replace(/[\r\n]/g, "");
}

function processMIMEInfo(mimeInfo, realMIMEInfo, policyName) {
  if ("handlers" in mimeInfo) {
    let firstHandler = true;
    for (let handler of mimeInfo.handlers) {
      // handler can be null which means they don't
      // want a preferred handler.
      if (handler) {
        let handlerApp;
        if ("path" in handler) {
          try {
            let file = new lazy.FileUtils.File(handler.path);
            handlerApp = Cc[
              "@mozilla.org/uriloader/local-handler-app;1"
            ].createInstance(Ci.nsILocalHandlerApp);
            handlerApp.executable = file;
          } catch (ex) {
            lazy.reportFailure(
              policyName,
              `Unable to create handler executable (${handler.path})`
            );
            continue;
          }
        } else if ("uriTemplate" in handler) {
          let templateURL;
          try {
            templateURL = new URL(handler.uriTemplate);
          } catch (ex) {
            lazy.reportFailure(
              policyName,
              `Invalid web handler URL (${handler.uriTemplate})`
            );
            continue;
          }
          if (templateURL.protocol != "https:") {
            lazy.reportFailure(
              policyName,
              `Web handler must be https (${handler.uriTemplate})`
            );
            continue;
          }
          if (
            !templateURL.pathname.includes("%s") &&
            !templateURL.search.includes("%s")
          ) {
            lazy.reportFailure(
              policyName,
              `Web handler must contain %s (${handler.uriTemplate})`
            );
            continue;
          }
          handlerApp = Cc[
            "@mozilla.org/uriloader/web-handler-app;1"
          ].createInstance(Ci.nsIWebHandlerApp);
          handlerApp.uriTemplate = handler.uriTemplate;
        } else {
          lazy.reportFailure(policyName, "Invalid handler");
          continue;
        }
        if ("name" in handler) {
          handlerApp.name = handler.name;
        }
        realMIMEInfo.possibleApplicationHandlers.appendElement(handlerApp);
        if (firstHandler) {
          realMIMEInfo.preferredApplicationHandler = handlerApp;
        }
      }
      firstHandler = false;
    }
  }
  if ("action" in mimeInfo) {
    let action = realMIMEInfo[mimeInfo.action];
    if (
      action == realMIMEInfo.useHelperApp &&
      !realMIMEInfo.possibleApplicationHandlers.length
    ) {
      lazy.reportFailure(policyName, "useHelperApp requires a handler");
      return;
    }
    realMIMEInfo.preferredAction = action;
  }
  if ("ask" in mimeInfo) {
    realMIMEInfo.alwaysAskBeforeHandling = mimeInfo.ask;
  }
  lazy.gHandlerService.store(realMIMEInfo);
}
