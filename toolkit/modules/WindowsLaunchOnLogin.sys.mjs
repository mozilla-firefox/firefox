/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const LAUNCH_ON_LOGIN_TASKID = "LaunchOnLogin";

/**
 * "Launch on Login" is a Firefox feature automatically launches Firefox when the
 * user logs in to Windows. The technical mechanism is simply writing a registry
 * key to `Software\Microsoft\Windows\CurrentVersion\Run`, but there is an issue:
 * when disabled in the Windows UI, additional registry keys are written under
 * `Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run`. Any
 * keys stored here should be seen as a user override and should never be modified.
 * When such keys are present, the launch on login feature should be considered
 * disabled and not available from within Firefox. This module provides the
 * functionality to access and modify these registry keys.
 *
 * MSIX installs cannot write to the registry so we instead use the MSIX-exclusive
 * Windows StartupTask APIs. The difference here is that the startup task is always
 * "registered", we control whether it's enabled or disabled. As such some
 * functions such as getLaunchOnLoginApproved() have different behavior on MSIX installs.
 */
export var WindowsLaunchOnLogin = {
  /**
   * Accepts another function as an argument and provides an open Windows
   * launch on login registry key for the passed-in function to manipulate.
   *
   * @param func
   *        The function to use.
   */
  async withLaunchOnLoginRegistryKey(func) {
    let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );
    wrk.open(
      wrk.ROOT_KEY_CURRENT_USER,
      "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
      wrk.ACCESS_ALL
    );
    try {
      await func(wrk);
    } finally {
      wrk.close();
    }
  },

  /**
   * Safely creates a Windows launch on login registry key
   */
  async createLaunchOnLoginRegistryKey() {
    try {
      await this.withLaunchOnLoginRegistryKey(async wrk => {
        // Added launch option -os-autostart for telemetry
        // Add quote marks around path to account for spaces in path
        let autostartPath =
          this.quoteString(Services.dirsvc.get("XREExeF", Ci.nsIFile).path) +
          " -os-autostart";
        try {
          wrk.writeStringValue(
            this.getLaunchOnLoginRegistryName(),
            autostartPath
          );
        } catch (e) {
          console.error("Could not write value to registry", e);
        }
      });
    } catch (e) {
      // We should only end up here if we fail to open the registry
      console.error("Failed to open Windows registry", e);
    }
  },

  /**
   * Either creates a Windows launch on login registry key on regular installs
   * or enables the startup task within the app manifest due to
   * restrictions on writing to the registry in MSIX.
   */
  async createLaunchOnLogin() {
    if (Services.sysinfo.getProperty("hasWinPackageId")) {
      await this.enableLaunchOnLoginMSIX();
    } else {
      await this.createLaunchOnLoginRegistryKey();
    }
  },

  /**
   * Stop launching on login by removing our registry key (on non-MSIX builds) or disabling
   * our startup task (on MSIX builds).
   * If the user has disabled launch on login via the Windows settings, this method will
   * no-op as we do not want to change the (separate) keys Windows uses to manage user
   * settings, and removing our own launch on login registry key but leaving the Windows
   * keys alone would leave the registry in a dubious state.
   */
  async removeLaunchOnLogin() {
    if (await this.getLaunchOnLoginApproved()) {
      if (Services.sysinfo.getProperty("hasWinPackageId")) {
        await this._disableLaunchOnLoginMSIX();
      } else {
        await this._removeLaunchOnLoginRegistryKey();
        await this._removeLaunchOnLoginShortcuts();
      }
    }
  },

  /**
   * Safely removes a Windows launch on login registry key
   */
  async _removeLaunchOnLoginRegistryKey() {
    try {
      await this.withLaunchOnLoginRegistryKey(async wrk => {
        let registryName = this.getLaunchOnLoginRegistryName();
        if (wrk.hasValue(registryName)) {
          try {
            wrk.removeValue(registryName);
          } catch (e) {
            console.error("Failed to remove Windows registry value", e);
          }
        }
      });
    } catch (e) {
      // We should only end up here if we fail to open the registry
      console.error("Failed to open Windows registry", e);
    }
  },

  /**
   * Enables launch on login on MSIX installs by using the
   * StartupTask APIs. A task called "LaunchOnLogin" exists
   * in the packaged application manifest.
   *
   * @returns {Promise<bool>}
   *          Whether the enable operation was successful.
   */
  async enableLaunchOnLoginMSIX() {
    if (!Services.sysinfo.getProperty("hasWinPackageId")) {
      throw Components.Exception(
        "Called on non-MSIX build",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }
    let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIWindowsShellService
    );
    return shellService.enableLaunchOnLoginMSIXAsync(LAUNCH_ON_LOGIN_TASKID);
  },

  /**
   * Disables launch on login on MSIX installs by using the
   * StartupTask APIs. A task called "LaunchOnLogin" exists
   * in the packaged application manifest.
   *
   * @returns {Promise<bool>}
   *          Whether the disable operation was successful.
   */
  async _disableLaunchOnLoginMSIX() {
    if (!Services.sysinfo.getProperty("hasWinPackageId")) {
      throw Components.Exception(
        "Called on non-MSIX build",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }
    let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIWindowsShellService
    );
    return shellService.disableLaunchOnLoginMSIXAsync(LAUNCH_ON_LOGIN_TASKID);
  },

  /**
   * Gets a list of all launch on login shortcuts in the
   * %USERNAME%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup folder
   * that point to the current Firefox executable.
   */
  getLaunchOnLoginShortcutList() {
    let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
      Ci.nsIWindowsShellService
    );
    return shellService.getLaunchOnLoginShortcuts();
  },

  /**
   * Safely removes all launch on login shortcuts in the
   * %USERNAME%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup folder
   * that point to the current Firefox executable.
   */
  async _removeLaunchOnLoginShortcuts() {
    let shortcuts = this.getLaunchOnLoginShortcutList();
    for (let i = 0; i < shortcuts.length; i++) {
      await IOUtils.remove(shortcuts[i]);
    }
  },

  /**
   * Checks if Windows launch on login was independently enabled or disabled
   * by the user in the Windows Startup Apps menu. The registry key that
   * stores this information should not be modified.
   *
   * If the state is set to disabled from the Windows UI on MSIX our API calls to
   * re-enable it will fail so report false.
   *
   * @returns {Promise<bool>}
   *          Report whether launch on login is allowed on Windows. On MSIX
   *          it's possible to set a startup app through policy making us
   *          unable to modify it so we should account for that here.
   */
  async getLaunchOnLoginApproved() {
    const enablementDetails = await this.getLaunchOnLoginEnablementDetails();
    return enablementDetails.isAllowedByPolicy;
  },

  /**
   * Checks if Windows launch on login has an existing registry key or user-created shortcut in
   * %USERNAME%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup. The registry key that
   * stores this information should not be modified.
   *
   * On MSIX installs we instead query whether the StartupTask is enabled or disabled
   *
   * @returns {Promise<bool>}
   *          Whether launch on login is enabled.
   */
  async getLaunchOnLoginEnabled() {
    const enablementDetails = await this.getLaunchOnLoginEnablementDetails();
    return enablementDetails.isEnabled;
  },

  /**
   * Returns detailed information about the launch-on-login enablement state.
   * This is the authoritative source of truth for telemetry and UI alike.
   *
   * On MSIX installs, queries the StartupTask API.
   * On non-MSIX installs, checks the registry key and startup shortcuts,
   * plus the StartupApproved registry for Windows Settings overrides.
   *
   * @returns {Promise<object>}
   *          An object with:
   *            isEnabled {bool} - Whether launch on login is currently active.
   *            isSupported {bool} - Whether the platform supports this feature.
   *            isAllowedByPolicy {bool} - Whether the user is free to toggle
   *              the setting (false when Windows Settings or policy override).
   */
  async getLaunchOnLoginEnablementDetails() {
    if (Services.sysinfo.getProperty("hasWinPackageId")) {
      let shellService = Cc["@mozilla.org/browser/shell-service;1"].getService(
        Ci.nsIWindowsShellService
      );
      let state = await shellService.getLaunchOnLoginEnabledMSIXAsync(
        LAUNCH_ON_LOGIN_TASKID
      );
      return {
        isEnabled:
          state == shellService.LAUNCH_ON_LOGIN_ENABLED ||
          state == shellService.LAUNCH_ON_LOGIN_ENABLED_BY_POLICY,
        isSupported: true,
        isAllowedByPolicy: !(
          state == shellService.LAUNCH_ON_LOGIN_DISABLED_BY_SETTINGS ||
          state == shellService.LAUNCH_ON_LOGIN_ENABLED_BY_POLICY
        ),
      };
    }

    // First check if launch on login is currently enabled
    let registryName = this.getLaunchOnLoginRegistryName();
    let regExists = false;
    let shortcutExists = false;
    // Start by checking if the registry key exists
    try {
      await this.withLaunchOnLoginRegistryKey(wrk => {
        regExists = wrk.hasValue(registryName);
      });
    } catch {
      // There will be an error when the registry key doesn't exist,
      // but to us this just means launch on login is not currently enabled.
    }
    // If the registry key doesn't exist, check if there are any shortcuts
    // to Firefox in the startup folder
    if (!regExists) {
      shortcutExists = !!this.getLaunchOnLoginShortcutList().length;
    }
    let isEnabled = regExists || shortcutExists;
    // Even if a user disables it later on we want the launch on login
    // infobar to remain disabled as the user is aware of the option.
    if (isEnabled) {
      Services.prefs.setBoolPref(
        "browser.startup.windowsLaunchOnLogin.disableLaunchOnLoginPrompt",
        true
      );
    }
    // Check whether Windows Settings has overridden the user's ability to
    // toggle launch-on-login (the StartupApproved\Run registry key).
    let isAllowedByPolicy = true;
    try {
      let wrkApproved = Cc[
        "@mozilla.org/windows-registry-key;1"
      ].createInstance(Ci.nsIWindowsRegKey);
      wrkApproved.open(
        wrkApproved.ROOT_KEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
        wrkApproved.ACCESS_READ
      );
      let approvedName = this.getLaunchOnLoginRegistryName();
      if (wrkApproved.hasValue(approvedName)) {
        // There's very little consistency with these binary values aside from
        // if the first byte is even it's enabled and odd is disabled. There's
        // also no published specification.
        isAllowedByPolicy =
          wrkApproved.readBinaryValue(approvedName).charCodeAt(0) % 2 == 0;
      }
      wrkApproved.close();
    } catch {
      // There will be an error when the registry key doesn't exist,
      // but to us this just means there is no policy override in place.
    }
    return { isEnabled, isSupported: true, isAllowedByPolicy };
  },

  /**
   * Quotes a string for use as a single command argument, using Windows quoting
   * conventions.
   *
   * @see https://msdn.microsoft.com/en-us/library/17w5ykft(v=vs.85).aspx
   *
   * @param {string} str
   *        The argument string to quote.
   * @returns {string}
   */
  quoteString(str) {
    if (!/[\s"]/.test(str)) {
      return str;
    }

    let escaped = str.replace(/(\\*)("|$)/g, (m0, m1, m2) => {
      if (m2) {
        m2 = `\\${m2}`;
      }
      return `${m1}${m1}${m2}`;
    });

    return `"${escaped}"`;
  },

  /**
   * Generates a unique registry name for the current application
   * like "Mozilla-Firefox-71AE18FE3142402B".
   */
  getLaunchOnLoginRegistryName() {
    let xreDirProvider = Cc[
      "@mozilla.org/xre/directory-provider;1"
    ].createInstance(Ci.nsIXREDirProvider);
    let registryName = `${Services.appinfo.vendor}-${
      Services.appinfo.name
    }-${xreDirProvider.getInstallHash()}`;
    return registryName;
  },
};
