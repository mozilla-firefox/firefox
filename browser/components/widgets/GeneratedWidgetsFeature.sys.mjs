/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";

const CONTROL_PREF = "browser.ai.control.generatedWidgets";
const DEFAULT_PREF = "browser.ai.control.default";
const LABS_PREF = "browser.widgets.generated.enabled";

/**
 * Custom widgets, as exposed to AI Controls. Firefox Labs turns them on by
 * setting LABS_PREF. Blocking leaves it alone, so people stay in Labs and
 * widgets already installed keep running.
 */
export class GeneratedWidgetsFeature extends AIFeature {
  /**
   * @returns {string}
   */
  static get id() {
    return "widget-generation";
  }

  /**
   * @returns {boolean}
   */
  static get hasDistinctEnabledState() {
    return true;
  }

  /**
   * @returns {boolean} True: generation runs on the server, so any device can
   *   use it.
   */
  static get canRunOnDevice() {
    return true;
  }

  /**
   * @returns {boolean}
   */
  static get isAllowed() {
    return Services.prefs.getBoolPref(LABS_PREF, false);
  }

  /**
   * Firefox Labs is what turns custom widgets on, so people in Labs are
   * enabled unless blocked or the control pref is set to "available".
   *
   * @returns {boolean}
   */
  static get isEnabled() {
    return (
      this.isAllowed &&
      !this.isBlocked &&
      Services.prefs.getStringPref(CONTROL_PREF, "") !== "available"
    );
  }

  /**
   * @returns {boolean}
   */
  static get isBlocked() {
    return this.#resolvedControlState === "blocked";
  }

  /**
   * @returns {boolean}
   */
  static get isManagedByPolicy() {
    return Services.prefs.prefIsLocked(CONTROL_PREF);
  }

  /**
   * Does nothing: the control pref that OnDeviceModelManager sets is the whole
   * blocked state.
   *
   * @returns {Promise<void>}
   */
  static async block() {}

  /**
   * @returns {Promise<void>}
   */
  static async makeAvailable() {
    Services.prefs.setStringPref(CONTROL_PREF, "available");
  }

  /**
   * Does nothing: only Firefox Labs turns custom widgets on.
   *
   * @returns {Promise<void>}
   */
  static async enable() {}

  /**
   * @returns {string}
   */
  static get #resolvedControlState() {
    const state = Services.prefs.getStringPref(CONTROL_PREF, "");
    if (!state || state === "default") {
      return Services.prefs.getStringPref(DEFAULT_PREF, "available");
    }
    return state;
  }
}
