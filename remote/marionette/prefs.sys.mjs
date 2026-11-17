/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const { PREF_BOOL, PREF_INT, PREF_INVALID, PREF_STRING } = Ci.nsIPrefBranch;

export class Branch {
  /**
   * @param {string=} branch
   *     Preference subtree.  Uses root tree given `null`.
   */
  constructor(branch) {
    this._branch = Services.prefs.getBranch(branch);
  }

  /**
   * Gets value of `pref` in its known type.
   *
   * @param {string} pref
   *     Preference name.
   * @param {*=} fallback
   *     Fallback value to return if `pref` does not exist.
   *
   * @returns {(string|boolean|number)}
   *     Value of `pref`, or the `fallback` value if `pref` does
   *     not exist.
   *
   * @throws {TypeError}
   *     If `pref` is not a recognised preference and no `fallback`
   *     value has been provided.
   */
  get(pref, fallback = null) {
    switch (this._branch.getPrefType(pref)) {
      case PREF_STRING:
        return this._branch.getStringPref(pref);

      case PREF_BOOL:
        return this._branch.getBoolPref(pref);

      case PREF_INT:
        return this._branch.getIntPref(pref);

      case PREF_INVALID:
      default:
        if (fallback != null) {
          return fallback;
        }
        throw new TypeError(`Unrecognised preference: ${pref}`);
    }
  }

  /**
   * Sets the value of `pref`.
   *
   * @param {string} pref
   *     Preference name.
   * @param {(string|boolean|number)} value
   *     `pref`'s new value.
   *
   * @throws {TypeError}
   *     If `value` is not the correct type for `pref`.
   */
  set(pref, value) {
    let typ;
    if (typeof value != "undefined" && value != null) {
      typ = value.constructor.name;
    }

    switch (typ) {
      case "String":
        // Unicode compliant
        return this._branch.setStringPref(pref, value);

      case "Boolean":
        return this._branch.setBoolPref(pref, value);

      case "Number":
        return this._branch.setIntPref(pref, value);

      default:
        throw new TypeError(`Illegal preference type value: ${typ}`);
    }
  }
}

/**
 * Provides shortcuts for lazily getting and setting typed Marionette
 * preferences.
 *
 * Some of Marionette's preferences are stored using primitive values
 * that internally are represented by complex types.
 *
 * Because we cannot trust the input of many of these preferences,
 * this class provides abstraction that lets us safely deal with
 * potentially malformed input.
 *
 * A further complication is that we cannot rely on `Preferences.sys.mjs`
 * in Marionette.  See https://bugzilla.mozilla.org/show_bug.cgi?id=1357517
 * for further details.
 */
class MarionetteBranch extends Branch {
  constructor(branch = "marionette.") {
    super(branch);
  }

  /**
   * The `marionette.debugging.clicktostart` preference delays
   * server startup until a modal dialogue has been clicked to allow
   * time for user to set breakpoints in the Browser Toolbox.
   *
   * @returns {boolean}
   */
  get clickToStart() {
    return this.get("debugging.clicktostart", false);
  }

  /**
   * The `marionette.navigate-after-click.enabled` preference enables the
   * fallback logic for Selenium clients to wait for a potential navigation
   * after clicking an element via "WebDriver:ElementClick".
   *
   * @returns {boolean}
   */
  get navigateAfterClickEnabled() {
    return this.get("navigate-after-click.enabled", true);
  }

  /**
   * The `marionette.navigate-after-click.timeout` preference defines the
   * amount of milliseconds to wait for a potential navigation after a click
   * event was dispatched via "WebDriver:ElementClick".
   *
   * @returns {number}
   */
  get navigateAfterClickTimeout() {
    return this.get("navigate-after-click.timeout", 50);
  }

  /**
   * The `marionette.port` preference, detailing which port
   * the TCP server should listen on.
   *
   * @returns {number}
   */
  get port() {
    return this.get("port", 2828);
  }

  set port(newPort) {
    this.set("port", newPort);
  }
}

/** Reads a JSON serialised blob stored in the environment. */
export class EnvironmentPrefs {
  /**
   * Reads the environment variable `key` and tries to parse it as
   * JSON Object, then provides an iterator over its keys and values.
   *
   * If the environment variable is not set, this function returns empty.
   *
   * @param {string} key
   *     Environment variable.
   *
   * @returns {Iterable.<string, (string|boolean|number)>}
   */
  static *from(key) {
    if (!Services.env.exists(key)) {
      return;
    }

    let prefs;
    try {
      prefs = JSON.parse(Services.env.get(key));
    } catch (e) {
      throw new TypeError(`Unable to parse prefs from ${key}`, e);
    }

    for (let prefName of Object.keys(prefs)) {
      yield [prefName, prefs[prefName]];
    }
  }
}

// There is a future potential of exposing this as Marionette.prefs.port
// if we introduce a Marionette.sys.mjs module.
export const MarionettePrefs = new MarionetteBranch();
