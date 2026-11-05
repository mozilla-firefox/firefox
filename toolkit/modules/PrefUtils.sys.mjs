/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const kPrefBranches = {
  user: Services.prefs,
  default: Services.prefs.getDefaultBranch(""),
};

export var PrefUtils = {
  /**
   * Get a preference of any type from the named branch.
   *
   * @param {string} pref
   * @param {object} [options]
   * @param {"default"|"user"} [options.branchName="user"] One of "default" or "user"
   * @param {string|boolean|integer|null} [options.defaultValue]
   *   The value to return if the preference does not exist. Defaults to null.
   */
  getPref(pref, { branch = "user", defaultValue = null } = {}) {
    const branchObj = kPrefBranches[branch];
    if (!branchObj) {
      throw new this.UnexpectedPreferenceBranch(
        `"${branch}" is not a valid preference branch`
      );
    }
    const type = branchObj.getPrefType(pref);

    try {
      switch (type) {
        case Services.prefs.PREF_BOOL: {
          return branchObj.getBoolPref(pref);
        }
        case Services.prefs.PREF_STRING: {
          return branchObj.getStringPref(pref);
        }
        case Services.prefs.PREF_INT: {
          return branchObj.getIntPref(pref);
        }
        case Services.prefs.PREF_INVALID: {
          return defaultValue;
        }
      }
    } catch (e) {
      if (branch === "default" && e.result === Cr.NS_ERROR_UNEXPECTED) {
        // There is a value for the pref on the user branch but not on the default branch. This is ok.
        return defaultValue;
      }
      // Unexpected error, re-throw it
      throw e;
    }

    // If `type` isn't any of the above, throw an error. Don't do this in a
    // default branch of switch so that error handling is easier.
    throw new TypeError(`Unknown preference type (${type}) for ${pref}.`);
  },

  /**
   * Get the value of the pref on the given branch.
   *
   * This function will always return null for an unset user branch value
   * instead of falling back to the default branch value (which is the default
   * behaviour of `Services.prefs.get{Bool,Int,String}Pref` and
   * `PrefUtils.getPref`).
   *
   * @param {string} pref The pref.
   * @param {"default" | "user"} branch The branch.
   *
   * @returns {PrefValue} The value of the pref, or null if it is not set on
   * that branch
   */
  getPrefStrict(pref, branch) {
    if (branch === "user" && !Services.prefs.prefHasUserValue(pref)) {
      return null;
    }

    return this.getPref(pref, { branch });
  },

  /**
   * Set a preference on the named branch
   *
   * @param {string} pref
   * @param {string|boolean|integer|null} value The value to set.
   * @param {object} options
   * @param {"user"|"default"} options.branchName The branch to make the change on.
   */
  setPref(pref, value, { branch = "user" } = {}) {
    if (value === null) {
      this.clearPref(pref, { branch });
      return;
    }
    const branchObj = kPrefBranches[branch];
    if (!branchObj) {
      throw new this.UnexpectedPreferenceBranch(
        `"${branch}" is not a valid preference branch`
      );
    }
    switch (typeof value) {
      case "boolean": {
        branchObj.setBoolPref(pref, value);
        break;
      }
      case "string": {
        branchObj.setStringPref(pref, value);
        break;
      }
      case "number": {
        branchObj.setIntPref(pref, value);
        break;
      }
      default: {
        throw new TypeError(
          `Unexpected value type (${typeof value}) for ${pref}.`
        );
      }
    }
  },

  /**
   * Remove a preference from a branch. Note that default branch preferences
   * cannot effectively be cleared. If "default" is passed for a branch, an
   * error will be logged and nothing else will happen.
   *
   * @param {string} pref
   * @param {object} options
   * @param {"user"|"default"} options.branchName The branch to clear
   */
  clearPref(pref, { branch = "user" } = {}) {
    if (branch === "user") {
      kPrefBranches.user.clearUserPref(pref);
    } else if (branch === "default") {
      const log = console.createInstance({
        logModule: "Toolkit.PrefUtils",
        maxLogLevel: "Warn",
      });
      log.warn(
        `Cannot reset pref ${pref} on the default branch. Pref will be cleared at next restart.`
      );
    } else {
      throw new this.UnexpectedPreferenceBranch(
        `"${branch}" is not a valid preference branch`
      );
    }
  },

  UnexpectedPreferenceType: class extends Error {},
  UnexpectedPreferenceBranch: class extends Error {},
};
