/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const PREF_LOGLEVEL = "browser.policies.loglevel";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "macOSPoliciesParser",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

const ENABLED_KEY = "EnterprisePoliciesEnabled";
const DISABLE_USER_POLICIES = "DisableMacOSUserPolicies";

export var macOSPoliciesParser = {
  readPolicies(reader, options) {
    let nativePolicies = this.selectPolicies(
      reader.readPolicySources(),
      options
    );
    if (!nativePolicies) {
      return null;
    }

    // Need an extra check here so we don't
    // JSON.stringify if we aren't in debug mode
    if (lazy.log.maxLogLevel == "debug") {
      lazy.log.debug(JSON.stringify(nativePolicies, null, 2));
    }

    return nativePolicies;
  },

  /**
   * Splits the sources returned by nsIMacPreferencesReader.readPolicySources()
   * into the policies an administrator controls and the ones a standard user
   * can set. Device-scope configuration profiles win over /Library/Preferences.
   *
   * @param {object} sources { managed, system, user } preference objects.
   * @param {object} [options]
   * @param {boolean} [options.allowUserPolicies] whether user policies may be
   *        used. Defaults to true.
   * @returns {{policies: object, userPolicies: object|null}|null} the
   *          policies, or null if policies aren't enabled.
   */
  selectPolicies(sources, { allowUserPolicies = true } = {}) {
    let [managed, system, user] = ["managed", "system", "user"].map(name => {
      try {
        return this.removeUnknownPolicies(
          this.unflatten(sources?.[name] ?? {})
        );
      } catch (e) {
        lazy.log.error(`Ignoring macOS ${name} policies: ${e}`);
        return {};
      }
    });

    delete user[DISABLE_USER_POLICIES];
    let policies = { ...system, ...managed };
    let enabled = allowUserPolicies ? { ...user, ...policies } : policies;
    if (enabled[ENABLED_KEY] !== true) {
      return null;
    }
    delete policies[ENABLED_KEY];
    delete user[ENABLED_KEY];

    if (!allowUserPolicies) {
      let ignored = Object.keys(user).filter(name => !(name in policies));
      if (ignored.length) {
        lazy.log.warn(`Ignoring macOS user policies: ${ignored.join(", ")}`);
      }
      return { policies, userPolicies: null };
    }
    return { policies, userPolicies: user };
  },

  removeUnknownPolicies(policies) {
    let { schema } = ChromeUtils.importESModule(
      "resource:///modules/policies/schema.sys.mjs"
    );

    for (let policyName of Object.keys(policies)) {
      if (
        policyName != ENABLED_KEY &&
        !schema.properties.hasOwnProperty(policyName)
      ) {
        lazy.log.debug(`Removing unknown policy: ${policyName}`);
        delete policies[policyName];
      }
    }

    return policies;
  },

  unflatten(input, delimiter = "__") {
    let ret = {};

    for (let key of Object.keys(input)) {
      if (!key.includes(delimiter)) {
        // Short-circuit for policies that are not specified in
        // the flat format.
        ret[key] = input[key];
        continue;
      }

      lazy.log.debug(`Unflattening policy key "${key}".`);

      let subkeys = key.split(delimiter);

      // `obj`: is the intermediate step into the unflattened
      // return object. For example, for an input:
      //
      // Foo__Bar__Baz: 5,
      //
      // when the subkey being iterated is Bar, then `obj` will be
      // the Bar object being constructed, as represented below:
      //
      // ret = {
      //   Foo = {
      //     Bar = {   <---- obj
      //       Baz: 5,
      //     }
      //   }
      // }
      let obj = ret;

      // Iterate until the second to last subkey, as the last one
      // needs special handling afterwards.
      for (let i = 0; i < subkeys.length - 1; i++) {
        let subkey = subkeys[i];

        if (!isValidSubkey(subkey)) {
          lazy.log.error(
            `Error in key ${key}: can't use indexes bigger than 50.`
          );
          continue;
        }

        if (!obj[subkey]) {
          // if this subkey hasn't been seen yet, create the object
          // for it, which could be an array if the next subkey is
          // a number.
          //
          // For example, in the following examples:
          // A)
          // Foo__Bar__0
          // Foo__Bar__1
          //
          // B)
          // Foo__Bar__Baz
          // Foo__Bar__Qux
          //
          // If the subkey being analysed right now is Bar, then in example A
          // we'll create an array to accomodate the numeric entries.
          // Otherwise, if it's example B, we'll create an object to host all
          // the named keys.
          if (Number.isInteger(Number(subkeys[i + 1]))) {
            obj[subkey] = [];
          } else {
            obj[subkey] = {};
          }
        }

        obj = obj[subkey];
      }

      let lastSubkey = subkeys[subkeys.length - 1];
      if (!isValidSubkey(lastSubkey)) {
        lazy.log.error(
          `Error in key ${key}: can't use indexes bigger than 50.`
        );
        continue;
      }

      // In the last subkey, we assign it the value by accessing the input
      // object again with the full key. For example, in the case:
      //
      // input = {"Foo__Bar__Baz": 5}
      //
      // what we're doing in practice is:
      //
      // ret["Foo"]["Bar"]["Baz"] = input["Foo__Bar__Baz"];
      // \_______ _______/   |
      //         v           |
      //        obj      last subkey

      obj[lastSubkey] = input[key];
    }

    return ret;
  },
};

function isValidSubkey(subkey) {
  let valueAsNumber = Number(subkey);
  return Number.isNaN(valueAsNumber) || valueAsNumber <= 50;
}
