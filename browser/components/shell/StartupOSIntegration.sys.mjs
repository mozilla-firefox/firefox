/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const PRIVATE_BROWSING_BINARY = "private_browsing.exe";
// Index of Private Browsing icon in private_browsing.exe
// Must line up with IDI_PBICON_PB_PB_EXE in nsNativeAppSupportWin.h.
const PRIVATE_BROWSING_EXE_ICON_INDEX = 1;
const PREF_PRIVATE_BROWSING_SHORTCUT_CREATED =
  "browser.privacySegmentation.createdShortcut";

const lazy = {};

XPCOMUtils.defineLazyServiceGetters(lazy, {
  BrowserHandler: ["@mozilla.org/browser/clh;1", "nsIBrowserHandler"],
  profileService: [
    "@mozilla.org/toolkit/profile-service;1",
    "nsIToolkitProfileService",
  ],
});

ChromeUtils.defineESModuleGetters(lazy, {
  FirefoxBridgeExtensionUtils:
    "resource:///modules/FirefoxBridgeExtensionUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ShellService: "resource:///modules/ShellService.sys.mjs",
  WindowsLaunchOnLogin: "resource://gre/modules/WindowsLaunchOnLogin.sys.mjs",
  WindowsGPOParser: "resource://gre/modules/policies/WindowsGPOParser.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  let consoleOptions = {
    // tip: set maxLogLevel to "debug" and use lazy.log.debug() to create
    // detailed messages during development. See LOG_LEVELS in Console.sys.mjs
    // for details.
    maxLogLevel: "error",
    maxLogLevelPref: "browser.policies.loglevel",
    prefix: "StartupOSIntegration.sys.mjs",
  };
  return new ConsoleAPI(consoleOptions);
});

function WindowsRegPoliciesGetter(wrk, root, regLocation) {
  wrk.open(root, regLocation, wrk.ACCESS_READ);
  let policies;
  if (wrk.hasChild("Mozilla\\" + Services.appinfo.name)) {
    policies = lazy.WindowsGPOParser.readPolicies(wrk, policies);
  }
  wrk.close();
  return policies;
}

export let StartupOSIntegration = {
  isPrivateBrowsingAllowedInRegistry() {
    // If there is an attempt to open Private Browsing before
    // EnterprisePolicies are initialized the Windows registry
    // can be checked to determine if it is enabled
    if (Services.policies.status > Ci.nsIEnterprisePolicies.UNINITIALIZED) {
      // Yield to policies engine if initialized
      let privateAllowed = Services.policies.isAllowed("privatebrowsing");
      lazy.log.debug(
        `Yield to initialized policies engine: Private Browsing Allowed = ${privateAllowed}`
      );
      return privateAllowed;
    }
    if (AppConstants.platform !== "win") {
      // Not using Windows so no registry, return true
      lazy.log.debug(
        "AppConstants.platform is not 'win': Private Browsing allowed"
      );
      return true;
    }
    // If all other checks fail only then do we check registry
    let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );
    let regLocation = "SOFTWARE\\Policies";
    let userPolicies, machinePolicies;
    // Only check HKEY_LOCAL_MACHINE if not in testing
    if (!Cu.isInAutomation) {
      machinePolicies = WindowsRegPoliciesGetter(
        wrk,
        wrk.ROOT_KEY_LOCAL_MACHINE,
        regLocation
      );
    }
    // Check machine policies before checking user policies
    // HKEY_LOCAL_MACHINE supersedes HKEY_CURRENT_USER so only check
    // HKEY_CURRENT_USER if the registry key is not present in
    // HKEY_LOCAL_MACHINE at all
    if (machinePolicies && "DisablePrivateBrowsing" in machinePolicies) {
      lazy.log.debug(
        `DisablePrivateBrowsing in HKEY_LOCAL_MACHINE is ${machinePolicies.DisablePrivateBrowsing}`
      );
      return !(machinePolicies.DisablePrivateBrowsing === 1);
    }
    userPolicies = WindowsRegPoliciesGetter(
      wrk,
      wrk.ROOT_KEY_CURRENT_USER,
      regLocation
    );
    if (userPolicies && "DisablePrivateBrowsing" in userPolicies) {
      lazy.log.debug(
        `DisablePrivateBrowsing in HKEY_CURRENT_USER is ${userPolicies.DisablePrivateBrowsing}`
      );
      return !(userPolicies.DisablePrivateBrowsing === 1);
    }
    // Private browsing allowed if no registry entry exists
    lazy.log.debug(
      "No DisablePrivateBrowsing registry entry: Private Browsing allowed"
    );
    return true;
  },

  checkForLaunchOnLogin() {
    // We only support launch on login on Windows at the moment.
    if (AppConstants.platform != "win") {
      return;
    }
    let launchOnLoginPref = "browser.startup.windowsLaunchOnLogin.enabled";
    if (!lazy.profileService.startWithLastProfile) {
      // If we don't start with last profile, the user
      // likely sees the profile selector on launch.
      if (Services.prefs.getBoolPref(launchOnLoginPref)) {
        Glean.launchOnLogin.lastProfileDisableStartup.record();
        // Disable launch on login messaging if we are disabling the
        // feature.
        Services.prefs.setBoolPref(
          "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
          true
        );
      }
      // To reduce confusion when running multiple Gecko profiles,
      // delete launch on login shortcuts and registry keys so that
      // users are not presented with the outdated profile selector
      // dialog.
      lazy.WindowsLaunchOnLogin.removeLaunchOnLogin();
    }
  },

  // Note: currently only invoked on Windows and macOS.
  async onStartupIdle() {
    // Catch and report exceptions, including async rejections:
    let safeCall = async fn => {
      try {
        await fn();
      } catch (ex) {
        console.error(ex);
      }
    };
    // Note that we explicitly do not await calls to `safeCall` as
    // these individual calls are independent and can run without
    // waiting for each other.

    // Currently we only support Firefox bridge on Windows and macOS.
    safeCall(() => this.ensureBridgeRegistered());

    if (AppConstants.platform == "win") {
      if (Services.sysinfo.getProperty("hasWinPackageId")) {
        safeCall(() => this.maybePinMSIXToStartMenu());
      }
      safeCall(() => this.ensurePrivateBrowsingShortcutExists());
    }
  },

  async ensureBridgeRegistered() {
    if (!Services.prefs.getBoolPref("browser.firefoxbridge.enabled", false)) {
      return;
    }
    let { defaultProfile, currentProfile } = lazy.profileService;
    if (defaultProfile && currentProfile == defaultProfile) {
      await lazy.FirefoxBridgeExtensionUtils.ensureRegistered();
    } else {
      lazy.log.debug(
        "FirefoxBridgeExtensionUtils failed to register due to non-default current profile."
      );
    }
  },

  // Silently pin Firefox to the start menu on first run when using MSIX on a
  // new profile.
  // If not first run, check if Firefox is no longer pinned to the Start Menu
  // when it previously was and send telemetry.
  async maybePinMSIXToStartMenu() {
    if (!Services.sysinfo.getProperty("hasWinPackageId")) {
      return;
    }
    if (
      lazy.BrowserHandler.firstRunProfile &&
      (await lazy.ShellService.doesAppNeedStartMenuPin())
    ) {
      await lazy.ShellService.pinToStartMenu();
      return;
    }
    await lazy.ShellService.recordWasPreviouslyPinnedToStartMenu();
  },

  // Ensure a Private Browsing Shortcut exists. This is needed in case
  // a user tries to use Windows functionality to pin our Private Browsing
  // mode icon to the Taskbar (eg: the "Pin to Taskbar" context menu item).
  // This is also created by the installer, but it's possible that a user
  // has removed it, or is running out of a zip build. The consequences of not
  // having a Shortcut for this are that regular Firefox will be pinned instead
  // of the Private Browsing version -- so it's quite important we do our best
  // to make sure one is available.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1762994 for additional
  // background.
  async ensurePrivateBrowsingShortcutExists() {
    if (
      // If the feature is disabled, don't do this.
      !Services.prefs.getBoolPref(
        "browser.privateWindowSeparation.enabled",
        true
      ) ||
      // We don't want a shortcut if it's been disabled, eg: by enterprise policy.
      !lazy.PrivateBrowsingUtils.enabled ||
      // Private Browsing shortcuts for packaged builds come with the package,
      // if they exist at all. We shouldn't try to create our own.
      Services.sysinfo.getProperty("hasWinPackageId") ||
      // If we've ever done this successfully before, don't try again. The
      // user may have deleted the shortcut, and we don't want to force it
      // on them.
      Services.prefs.getBoolPref(PREF_PRIVATE_BROWSING_SHORTCUT_CREATED, false)
    ) {
      return;
    }

    let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIWindowsShellService
    );
    let winTaskbar = Cc["@mozilla.org/windows-taskbar;1"].getService(
      Ci.nsIWinTaskbar
    );

    if (
      !(await shellService.hasPinnableShortcut(
        winTaskbar.defaultPrivateGroupId,
        true
      ))
    ) {
      let appdir = Services.dirsvc.get("GreD", Ci.nsIFile);
      let exe = appdir.clone();
      exe.append(PRIVATE_BROWSING_BINARY);
      let strings = new Localization(
        ["branding/brand.ftl", "browser/browser.ftl"],
        true
      );
      let [desc] = await strings.formatValues([
        "private-browsing-shortcut-text-2",
      ]);
      await shellService.createShortcut(
        exe,
        [],
        desc,
        exe,
        // The code we're calling indexes from 0 instead of 1
        PRIVATE_BROWSING_EXE_ICON_INDEX - 1,
        winTaskbar.defaultPrivateGroupId,
        "Programs",
        desc + ".lnk",
        appdir
      );
    }
    // We always set this as long as no exception has been thrown. This
    // ensure that it is `true` both if we created one because it didn't
    // exist, or if it already existed (most likely because it was created
    // by the installer). This avoids the need to call `hasPinnableShortcut`
    // again, which necessarily does pointless I/O.
    Services.prefs.setBoolPref(PREF_PRIVATE_BROWSING_SHORTCUT_CREATED, true);
  },
};
