/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  WindowsLaunchOnLogin:
    "resource://gre/modules/launchonlogin/WindowsLaunchOnLogin.sys.mjs",
  MacOSLaunchOnLogin:
    "resource://gre/modules/launchonlogin/MacOSLaunchOnLogin.sys.mjs",
  UnsupportedLaunchOnLogin:
    "resource://gre/modules/launchonlogin/UnsupportedLaunchOnLogin.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "gImpl", () => {
  if (AppConstants.platform == "win") {
    return lazy.WindowsLaunchOnLogin;
  }

  if (AppConstants.platform == "macosx") {
    return lazy.MacOSLaunchOnLogin;
  }

  return lazy.UnsupportedLaunchOnLogin;
});

ChromeUtils.defineLazyGetter(lazy, "isSupported", () => {
  return AppConstants.platform == "win" || AppConstants.platform == "macosx";
});

export var LaunchOnLogin = {
  /**
   * If LaunchOnLogin is supported on this platform
   *
   * @returns {bool}
   *          Whether LaunchOnLogin is supported.
   */
  isSupported() {
    return lazy.isSupported;
  },

  /**
   * Enables LaunchOnLogin
   *
   * @returns {Promise<bool>}
   *          Whether LaunchOnLogin was enabled.
   */
  async enable() {
    if (Services.policies && !Services.policies.isAllowed("launchOnLogin")) {
      return Promise.resolve(false);
    }

    return await lazy.gImpl.createLaunchOnLogin();
  },

  /**
   * Disables LaunchOnLogin.
   *
   * @returns {Promise<bool>}
   *          Whether LaunchOnLogin was disabled.
   */
  async disable() {
    return await lazy.gImpl.removeLaunchOnLogin();
  },

  /**
   * Whether LaunchOnLogin is allowed by OS or Firefox policy.
   *
   * @returns {Promise<bool>}
   *          Whether LaunchOnLogin is allowed.
   */
  async isAllowed() {
    if (Services.policies && !Services.policies.isAllowed("launchOnLogin")) {
      return Promise.resolve(false);
    }

    return await lazy.gImpl.getLaunchOnLoginApproved();
  },

  /**
   * Whether LaunchOnLogin is enabled
   *
   * @returns {Promise<bool>}
   *          Whether LaunchOnLogin is allowed.
   */
  async isEnabled() {
    if (Services.policies && !Services.policies.isAllowed("launchOnLogin")) {
      return Promise.resolve(false);
    }

    return await lazy.gImpl.getLaunchOnLoginEnabled();
  },

  /**
   * Get the enablementDetails
   *
   * @returns {Promise<object>}
   *          An object containing
   *            - isEnabled,
   *            - isSupported,
   *            - isAllowedByPolicy
   */
  async enablementDetails() {
    let details = await lazy.gImpl.getLaunchOnLoginEnablementDetails();
    if (Services.policies && !Services.policies.isAllowed("launchOnLogin")) {
      details.isAllowedByPolicy = false;
    }
    return details;
  },
};
