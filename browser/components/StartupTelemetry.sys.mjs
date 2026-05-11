/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

let lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserInitState: "resource:///modules/BrowserGlue.sys.mjs",
  BrowserUsageTelemetry: "resource:///modules/BrowserUsageTelemetry.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  OsEnvironment: "resource://gre/modules/OsEnvironment.sys.mjs",
  WindowsLaunchOnLogin: "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs",
  PlacesDBUtils: "resource://gre/modules/PlacesDBUtils.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TelemetryReportingPolicy:
    "resource://gre/modules/TelemetryReportingPolicy.sys.mjs",
  UsageReporting: "resource://gre/modules/UsageReporting.sys.mjs",
});

/**
 * Used to collect various bits of telemetry during browser startup.
 *
 */
export let StartupTelemetry = {
  // Some tasks are expensive because they involve significant disk IO, and
  // may also write information to disk. If we submit the telemetry that may
  // happen anyway, but if we don't then this is undesirable, so those tasks are
  // only run if we will submit the results.
  // Why run any telemetry code at all if we don't submit the data? Because
  // local and autoland builds usually do not submit telemetry, but we still
  // want to be able to run automated tests to check the code _worked_.
  get _willUseExpensiveTelemetry() {
    return (
      AppConstants.MOZ_TELEMETRY_REPORTING &&
      Services.prefs.getBoolPref(
        "datareporting.healthreport.uploadEnabled",
        false
      )
    );
  },

  _runIdleTasks(tasks, profilerMarker) {
    for (let task of tasks) {
      ChromeUtils.idleDispatch(async () => {
        if (!Services.startup.shuttingDown) {
          let startTime = ChromeUtils.now();
          try {
            await task();
          } catch (ex) {
            console.error(ex);
          } finally {
            ChromeUtils.addProfilerMarker(
              profilerMarker,
              startTime,
              task.toSource()
            );
          }
        }
      });
    }
  },

  browserIdleStartup() {
    let tasks = [
      // FOG doesn't need to be initialized _too_ early because it has a pre-init buffer.
      () => this.initFOG(),

      () => this.contentBlocking(),
      () => this.dataSanitization(),
      () => this.pipEnabled(),
      () => this.sslKeylogFile(),
      () => this.osAuthEnabled(),
      () => this.startupConditions(),
      () => this.httpsOnlyState(),
      () => this.globalPrivacyControl(),
      () => this.aiControlBlocking(),
      () => this.launchOnLoginState(),
    ];
    if (this._willUseExpensiveTelemetry) {
      tasks.push(() => lazy.PlacesDBUtils.telemetry());
    }
    if (AppConstants.platform == "win") {
      tasks.push(
        () => this.pinningStatus(),
        () => this.isDefaultHandler()
      );
    } else if (AppConstants.platform == "macosx") {
      tasks.push(() => this.macDockStatus());
    } else if (AppConstants.platform == "linux") {
      tasks.push(() => this.desktopEntryStatus());
    }

    this._runIdleTasks(tasks, "startupTelemetryIdleTask");
  },

  /**
   * Use this function as an entry point to collect telemetry that we hope
   * to collect once per session, at any arbitrary point in time, and
   *
   * **which we are okay with sometimes not running at all.**
   *
   * See BrowserGlue.sys.mjs's _scheduleBestEffortUserIdleTasks for more
   * details.
   */
  bestEffortIdleStartup() {
    let tasks = [
      () => this.primaryPasswordEnabled(),
      () => lazy.OsEnvironment.reportAllowedAppSources(),
    ];
    if (AppConstants.platform == "win" && this._willUseExpensiveTelemetry) {
      tasks.push(
        () => lazy.BrowserUsageTelemetry.reportProfileCount(),
        () => lazy.BrowserUsageTelemetry.reportInstallationTelemetry()
      );
    }
    this._runIdleTasks(tasks, "startupTelemetryLateIdleTask");
  },

  /**
   * Initialize Firefox-on-Glean.
   *
   * This is at the top because it's a bit different from the other code here
   * which is strictly collecting specific metrics.
   */
  async initFOG() {
    // Handle Usage Profile ID.  Similar logic to what's happening in
    // `TelemetryControllerParent` for the client ID.  Must be done before
    // initializing FOG so that ping enabled/disabled states are correct
    // before Glean takes actions.
    await lazy.UsageReporting.ensureInitialized();

    // If needed, delay initializing FOG until policy interaction is
    // completed.  See comments in `TelemetryReportingPolicy`.
    await lazy.TelemetryReportingPolicy.ensureUserIsNotified();

    Services.fog.initializeFOG();

    // Register Glean to listen for experiment updates releated to the
    // "gleanInternalSdk" feature defined in the t/c/nimbus/FeatureManifest.yaml
    // This feature is intended for internal Glean use only. For features wishing
    // to set a remote metric configuration, please use the "glean" feature for
    // the purpose of setting the data-control-plane features via Server Knobs.
    lazy.NimbusFeatures.gleanInternalSdk.onUpdate(() => {
      let cfg = lazy.NimbusFeatures.gleanInternalSdk.getVariable(
        "gleanMetricConfiguration"
      );
      Services.fog.applyServerKnobsConfig(JSON.stringify(cfg));
    });

    // Register Glean to listen for experiment updates releated to the
    // "glean" feature defined in the t/c/nimbus/FeatureManifest.yaml
    lazy.NimbusFeatures.glean.onUpdate(() => {
      const enrollments = lazy.NimbusFeatures.glean.getAllEnrollments();
      for (const enrollment of enrollments) {
        const cfg = enrollment.value.gleanMetricConfiguration;
        if (typeof cfg === "object" && cfg !== null) {
          Services.fog.applyServerKnobsConfig(JSON.stringify(cfg));
        }
      }
    });
  },

  startupConditions() {
    let nowSeconds = Math.round(Date.now() / 1000);
    // Don't include cases where we don't have the pref. This rules out the first install
    // as well as the first run of a build since this was introduced. These could by some
    // definitions be referred to as "cold" startups, but probably not since we likely
    // just wrote many of the files we use to disk. This way we should approximate a lower
    // bound to the number of cold startups rather than an upper bound.
    let lastCheckSeconds = Services.prefs.getIntPref(
      "browser.startup.lastColdStartupCheck",
      nowSeconds
    );
    Services.prefs.setIntPref(
      "browser.startup.lastColdStartupCheck",
      nowSeconds
    );
    try {
      let secondsSinceLastOSRestart =
        Services.startup.secondsSinceLastOSRestart;
      let isColdStartup =
        nowSeconds - secondsSinceLastOSRestart > lastCheckSeconds;
      Glean.startup.isCold.set(isColdStartup);
      Glean.startup.secondsSinceLastOsRestart.set(secondsSinceLastOSRestart);
    } catch (ex) {
      if (ex.name !== "NS_ERROR_NOT_IMPLEMENTED") {
        console.error(ex);
      }
    }
  },

  contentBlocking() {
    let tpEnabled = Services.prefs.getBoolPref(
      "privacy.trackingprotection.enabled"
    );
    Glean.contentblocking.trackingProtectionEnabled[
      tpEnabled ? "true" : "false"
    ].add();

    let tpPBEnabled = Services.prefs.getBoolPref(
      "privacy.trackingprotection.pbmode.enabled"
    );
    Glean.contentblocking.trackingProtectionPbmDisabled[
      !tpPBEnabled ? "true" : "false"
    ].add();

    let cookieBehavior = Services.prefs.getIntPref(
      "network.cookie.cookieBehavior"
    );
    Glean.contentblocking.cookieBehavior.accumulateSingleSample(cookieBehavior);

    let fpEnabled = Services.prefs.getBoolPref(
      "privacy.trackingprotection.fingerprinting.enabled"
    );
    let cmEnabled = Services.prefs.getBoolPref(
      "privacy.trackingprotection.cryptomining.enabled"
    );
    let categoryPref;
    switch (
      Services.prefs.getStringPref("browser.contentblocking.category", null)
    ) {
      case "standard":
        categoryPref = 0;
        break;
      case "strict":
        categoryPref = 1;
        break;
      case "custom":
        categoryPref = 2;
        break;
      default:
        // Any other value is unsupported.
        categoryPref = 3;
        break;
    }

    Glean.contentblocking.fingerprintingBlockingEnabled.set(fpEnabled);
    Glean.contentblocking.cryptominingBlockingEnabled.set(cmEnabled);
    Glean.contentblocking.category.set(categoryPref);
  },

  dataSanitization() {
    Glean.datasanitization.privacySanitizeSanitizeOnShutdown.set(
      Services.prefs.getBoolPref("privacy.sanitize.sanitizeOnShutdown")
    );
    Glean.datasanitization.privacyClearOnShutdownCookies.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.cookies")
    );
    Glean.datasanitization.privacyClearOnShutdownHistory.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.history")
    );
    Glean.datasanitization.privacyClearOnShutdownFormdata.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.formdata")
    );
    Glean.datasanitization.privacyClearOnShutdownDownloads.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.downloads")
    );
    Glean.datasanitization.privacyClearOnShutdownCache.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.cache")
    );
    Glean.datasanitization.privacyClearOnShutdownSessions.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.sessions")
    );
    Glean.datasanitization.privacyClearOnShutdownOfflineApps.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.offlineApps")
    );
    Glean.datasanitization.privacyClearOnShutdownSiteSettings.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.siteSettings")
    );
    Glean.datasanitization.privacyClearOnShutdownOpenWindows.set(
      Services.prefs.getBoolPref("privacy.clearOnShutdown.openWindows")
    );

    let exceptions = 0;
    for (let permission of Services.perms.all) {
      // We consider just permissions set for http, https and file URLs.
      if (
        permission.type == "cookie" &&
        permission.capability == Ci.nsICookiePermission.ACCESS_SESSION &&
        ["http", "https", "file"].some(scheme =>
          permission.principal.schemeIs(scheme)
        )
      ) {
        exceptions++;
      }
    }
    Glean.datasanitization.sessionPermissionExceptions.set(exceptions);
  },

  httpsOnlyState() {
    const PREF_ENABLED = "dom.security.https_only_mode";
    const PREF_WAS_ENABLED = "dom.security.https_only_mode_ever_enabled";
    const _checkHTTPSOnlyPref = async () => {
      const enabled = Services.prefs.getBoolPref(PREF_ENABLED, false);
      const was_enabled = Services.prefs.getBoolPref(PREF_WAS_ENABLED, false);
      let value = 0;
      if (enabled) {
        value = 1;
        Services.prefs.setBoolPref(PREF_WAS_ENABLED, true);
      } else if (was_enabled) {
        value = 2;
      }
      Glean.security.httpsOnlyModeEnabled.set(value);
    };

    Services.prefs.addObserver(PREF_ENABLED, _checkHTTPSOnlyPref);
    _checkHTTPSOnlyPref();

    const PREF_PBM_WAS_ENABLED =
      "dom.security.https_only_mode_ever_enabled_pbm";
    const PREF_PBM_ENABLED = "dom.security.https_only_mode_pbm";

    const _checkHTTPSOnlyPBMPref = async () => {
      const enabledPBM = Services.prefs.getBoolPref(PREF_PBM_ENABLED, false);
      const was_enabledPBM = Services.prefs.getBoolPref(
        PREF_PBM_WAS_ENABLED,
        false
      );
      let valuePBM = 0;
      if (enabledPBM) {
        valuePBM = 1;
        Services.prefs.setBoolPref(PREF_PBM_WAS_ENABLED, true);
      } else if (was_enabledPBM) {
        valuePBM = 2;
      }
      Glean.security.httpsOnlyModeEnabledPbm.set(valuePBM);
    };

    Services.prefs.addObserver(PREF_PBM_ENABLED, _checkHTTPSOnlyPBMPref);
    _checkHTTPSOnlyPBMPref();
  },

  globalPrivacyControl() {
    const FEATURE_PREF_ENABLED = "privacy.globalprivacycontrol.enabled";
    const FUNCTIONALITY_PREF_ENABLED =
      "privacy.globalprivacycontrol.functionality.enabled";
    const PREF_WAS_ENABLED = "privacy.globalprivacycontrol.was_ever_enabled";
    const _checkGPCPref = async () => {
      const feature_enabled = Services.prefs.getBoolPref(
        FEATURE_PREF_ENABLED,
        false
      );
      const functionality_enabled = Services.prefs.getBoolPref(
        FUNCTIONALITY_PREF_ENABLED,
        false
      );
      const was_enabled = Services.prefs.getBoolPref(PREF_WAS_ENABLED, false);
      let value = 0;
      if (feature_enabled && functionality_enabled) {
        value = 1;
        Services.prefs.setBoolPref(PREF_WAS_ENABLED, true);
      } else if (was_enabled) {
        value = 2;
      }
      Glean.security.globalPrivacyControlEnabled.set(value);
    };

    Services.prefs.addObserver(FEATURE_PREF_ENABLED, _checkGPCPref);
    Services.prefs.addObserver(FUNCTIONALITY_PREF_ENABLED, _checkGPCPref);
    _checkGPCPref();
  },

  aiControlBlocking() {
    const GLOBAL_AI_PREF = "browser.ai.control.default";
    const AI_CONTROL_FEATURES = {
      "browser.ai.control.translations": "translations",
      "browser.ai.control.pdfjsAltText": "pdfjsAltText",
      "browser.ai.control.smartTabGroups": "smartTabGroups",
      "browser.ai.control.linkPreviewKeyPoints": "linkPreviewKeyPoints",
      "browser.ai.control.sidebarChatbot": "sidebarChatbot",
      "browser.ai.control.smartWindow": "smartWindow",
    };
    const _checkAiControlPrefs = async () => {
      const globalIsBlocked =
        Services.prefs.getStringPref(GLOBAL_AI_PREF, null) === "blocked";
      Glean.browser.globalAiControlIsBlocking.set(globalIsBlocked);

      for (let [pref, key] of Object.entries(AI_CONTROL_FEATURES)) {
        let controlState = Services.prefs.getStringPref(pref, "");
        let isBlocked =
          controlState === "blocked" ||
          (controlState == "default" && globalIsBlocked);
        Glean.browser.aiControlIsBlocking[key].set(isBlocked);
      }
    };

    Services.prefs.addObserver(GLOBAL_AI_PREF, _checkAiControlPrefs);
    for (let pref in AI_CONTROL_FEATURES) {
      Services.prefs.addObserver(pref, _checkAiControlPrefs);
    }
    _checkAiControlPrefs();
    return () => {
      Services.prefs.removeObserver(GLOBAL_AI_PREF, _checkAiControlPrefs);
      for (let pref in AI_CONTROL_FEATURES) {
        Services.prefs.removeObserver(pref, _checkAiControlPrefs);
      }
    };
  },

  // check if the launcher was used to open firefox
  isUsingLauncher() {
    if (Services.env.get("FIREFOX_LAUNCHED_BY_DESKTOP_LAUNCHER") == "TRUE") {
      return true;
    }

    return false;
  },

  async pinningStatus() {
    let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIWindowsShellService
    );
    let winTaskbar = Cc["@mozilla.org/windows-taskbar;1"].getService(
      Ci.nsIWinTaskbar
    );

    try {
      Glean.osEnvironment.isTaskbarPinned.set(
        await shellService.isCurrentAppPinnedToTaskbarAsync(
          winTaskbar.defaultGroupId
        )
      );
      // Bug 1911343: Pinning regular browsing on MSIX
      // causes false positives when checking for private
      // browsing.
      if (
        AppConstants.platform === "win" &&
        !Services.sysinfo.getProperty("hasWinPackageId")
      ) {
        Glean.osEnvironment.isTaskbarPinnedPrivate.set(
          await shellService.isCurrentAppPinnedToTaskbarAsync(
            winTaskbar.defaultPrivateGroupId
          )
        );
      }
    } catch (ex) {
      console.error(ex);
    }

    let classification;
    let shortcut;
    try {
      shortcut = Services.appinfo.processStartupShortcut;
      classification = shellService.classifyShortcut(shortcut);
    } catch (ex) {
      console.error(ex);
    }

    if (!classification) {
      if (lazy.BrowserInitState.isLaunchOnLogin) {
        classification = "Autostart";
      } else if (shortcut) {
        classification = "OtherShortcut";
      } else if (this.isUsingLauncher()) {
        classification = "DesktopLauncher";
      } else {
        classification = "Other";
      }
    }
    // Because of how taskbar tabs work, it may be classifed as a taskbar
    // shortcut, in which case we want to overwrite it.
    if (lazy.BrowserInitState.isTaskbarTab) {
      classification = "TaskbarTab";
    }
    Glean.osEnvironment.launchMethod.set(classification);
  },

  isDefaultHandler() {
    // Report whether Firefox is the default handler for various files types
    // and protocols, in particular, ".pdf" and "mailto"
    [".pdf", "mailto"].every(x => {
      Glean.osEnvironment.isDefaultHandler[x].set(
        lazy.ShellService.isDefaultHandlerFor(x)
      );
      return true;
    });
  },

  async launchOnLoginState() {
    let state;
    if (AppConstants.platform != "win") {
      state = "not_supported";
    } else {
      try {
        const enablementDetails =
          await lazy.WindowsLaunchOnLogin.getLaunchOnLoginEnablementDetails();
        if (enablementDetails.isEnabled) {
          state = "enabled";
        } else if (!enablementDetails.isSupported) {
          state = "not_supported";
        } else if (!enablementDetails.isAllowedByPolicy) {
          state = "disabled_by_settings";
        } else {
          state = "disabled";
        }
      } catch (ex) {
        console.error(ex);
        state = "error";
      }
    }
    Glean.osEnvironment.launchOnLoginState.set(state);
  },

  macDockStatus() {
    // Report macOS Dock status
    Glean.osEnvironment.isKeptInDock.set(
      Cc["@mozilla.org/widget/macdocksupport;1"].getService(
        Ci.nsIMacDockSupport
      ).isAppInDock
    );
  },

  desktopEntryStatus(gioServiceForTestingOnly) {
    // Get it here so it can be mocked out.
    let gioService =
      gioServiceForTestingOnly ??
      Cc["@mozilla.org/gio-service;1"].getService(Ci.nsIGIOService);
    if (gioService.isRunningUnderFlatpak || gioService.isRunningUnderSnap) {
      Glean.osEnvironment.desktopEntryExists.set("sandboxed");
      return;
    }

    let labels = {
      [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_ABSENT]: "absent",
      [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_INVISIBLE]: "invisible",
      [Ci.nsIGNOMEShellService.DESKTOP_ENTRY_VISIBLE]: "visible",
    };
    let status = lazy.ShellService.getDesktopEntryStatus(
      Services.appinfo.remotingName + ".desktop"
    );

    Glean.osEnvironment.desktopEntryExists.set(labels[status] ?? "other");
  },

  sslKeylogFile() {
    Glean.sslkeylogging.enabled.set(Services.env.exists("SSLKEYLOGFILE"));
  },

  osAuthEnabled() {
    const osAuthForCc = lazy.FormAutofillUtils.getOSAuthEnabled();
    const osAuthForPw = lazy.LoginHelper.getOSAuthEnabled();

    Glean.formautofill.osAuthEnabled.set(osAuthForCc);
    Glean.pwmgr.osAuthEnabled.set(osAuthForPw);
  },

  primaryPasswordEnabled() {
    let tokenDB = Cc["@mozilla.org/security/pk11tokendb;1"].getService(
      Ci.nsIPK11TokenDB
    );
    let token = tokenDB.getInternalKeyToken();
    Glean.primaryPassword.enabled.set(token.hasPassword);
  },

  pipEnabled() {
    const TOGGLE_ENABLED_PREF =
      "media.videocontrols.picture-in-picture.video-toggle.enabled";

    const observe = (subject, topic) => {
      const enabled = Services.prefs.getBoolPref(TOGGLE_ENABLED_PREF, false);
      Glean.pictureinpicture.toggleEnabled.set(enabled);

      // Record events when preferences change
      if (topic === "nsPref:changed") {
        if (enabled) {
          Glean.pictureinpictureSettings.enableSettings.record();
        }
      }
    };

    Services.prefs.addObserver(TOGGLE_ENABLED_PREF, observe);
    observe();
  },
};
