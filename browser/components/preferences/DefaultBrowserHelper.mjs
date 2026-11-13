/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// This module touches the preferences window directly (window timers,
// location.hash, document.visibilityState), so it must be imported with
// { global: "current" } from main.js so those globals resolve to the
// preferences window. ES module setting configs (e.g. browser-icon.mjs) are
// already loaded in that global and import it statically, sharing this single
// instance and its polling timer.

const lazy = {};

if (Cc["@mozilla.org/gio-service;1"]) {
  XPCOMUtils.defineLazyServiceGetter(
    lazy,
    "gGIOService",
    "@mozilla.org/gio-service;1",
    Ci.nsIGIOService
  );
} else {
  lazy.gGIOService = null;
}

/**
 * A helper object containing all logic related to detecting and setting the
 * browser as the user's default, including a shared poll that notifies
 * subscribers when the default-browser state changes.
 */
export const DefaultBrowserHelper = {
  /**
   * @type {number}
   */
  _backoffIndex: 0,

  /**
   * @type {number | undefined}
   */
  _pollingTimer: undefined,

  /**
   * Keeps track of the last known browser
   * default value set to compare while polling.
   *
   * @type {boolean | undefined}
   */
  _lastPolledIsDefault: undefined,

  /**
   * Callbacks notified when the default-browser state changes while polling.
   *
   * @type {Set<Function>}
   */
  _changeListeners: new Set(),

  /**
   * @type {typeof import('../shell/ShellService.sys.mjs').ShellService | undefined}
   */
  get shellSvc() {
    return (
      AppConstants.HAVE_SHELL_SERVICE &&
      // getShellService() is a utilityOverlay.js global on the preferences
      // window; using it (rather than importing ShellService directly) keeps
      // the mock seam that browser_defaultbrowser_alwayscheck.js relies on.
      // @ts-ignore from utilityOverlay.js
      window.getShellService()
    );
  },

  /**
   * Subscribes to default-browser state changes. The polling timer starts on
   * the first subscriber and every registered callback is invoked when the
   * state flips; polling stops once the last subscriber unsubscribes.
   *
   * @param {Function} hasChanged Called when the default-browser state changes.
   * @returns {() => void} Unsubscribe function.
   */
  pollForDefaultChanges(hasChanged) {
    this._changeListeners.add(hasChanged);

    if (!this._pollingTimer) {
      this._lastPolledIsDefault = this.isBrowserDefault;

      // Exponential backoff mechanism will delay the polling times if user doesn't
      // trigger SetDefaultBrowser for a long time.
      const backoffTimes = [
        1000, 1000, 1000, 1000, 2000, 2000, 2000, 5000, 5000, 10000,
      ];

      const pollForDefaultBrowser = () => {
        if (
          (location.hash == "" ||
            location.hash == "#general" ||
            location.hash == "#sync" ||
            location.hash == "#browserIcon") &&
          document.visibilityState == "visible"
        ) {
          const { isBrowserDefault } = this;
          if (isBrowserDefault !== this._lastPolledIsDefault) {
            this._lastPolledIsDefault = isBrowserDefault;
            for (let listener of this._changeListeners) {
              listener();
            }
          }
        }

        if (!this._pollingTimer) {
          return;
        }

        // approximately a "requestIdleInterval"
        this._pollingTimer = window.setTimeout(
          () => {
            window.requestIdleCallback(pollForDefaultBrowser);
          },
          backoffTimes[
            this._backoffIndex + 1 < backoffTimes.length
              ? this._backoffIndex++
              : backoffTimes.length - 1
          ]
        );
      };

      this._pollingTimer = window.setTimeout(() => {
        window.requestIdleCallback(pollForDefaultBrowser);
      }, backoffTimes[this._backoffIndex]);
    }

    return () => {
      this._changeListeners.delete(hasChanged);
      if (!this._changeListeners.size) {
        this.clearPollingForDefaultChanges();
      }
    };
  },

  /**
   * Stops timer for polling changes.
   */
  clearPollingForDefaultChanges() {
    if (this._pollingTimer) {
      clearTimeout(this._pollingTimer);
      this._pollingTimer = undefined;
    }
  },

  /**
   *  Checks if the browser is default through the shell service.
   */
  get isBrowserDefault() {
    if (!this.canCheck) {
      return false;
    }
    return this.shellSvc?.isDefaultBrowser(false, true);
  },

  /**
   * Attempts to set the browser as the user's
   * default through the shell service.
   *
   * @returns {Promise<void>}
   */
  async setDefaultBrowser() {
    // Reset exponential backoff delay time in order to do visual update in pollForDefaultBrowser.
    this._backoffIndex = 0;

    try {
      await this.shellSvc?.setDefaultBrowser(false);
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Checks whether the browser is capable of being made default.
   *
   * @type {boolean}
   */
  get canCheck() {
    return (
      this.shellSvc &&
      /**
       * Flatpak does not support setting nor detection of default browser
       */
      !lazy.gGIOService?.isRunningUnderFlatpak
    );
  },
};
