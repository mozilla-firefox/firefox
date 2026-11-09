/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SitePolicyUtils: "resource://gre/modules/SitePolicyUtils.sys.mjs",
});

const PREF_LOGLEVEL = "browser.policies.loglevel";

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

/**
 * Records the operations that failed while a policy was being applied, so that
 * about:policies can report that policy as only partially applied.
 *
 * A policy whose operation failed stays in getActivePolicies(): those are the
 * parameters the administrator asked for, and most of the policy may well have
 * been applied. What failed is recorded here instead.
 */
export const PolicyFailures = {
  /** @type {Map<string, string[]>} */
  _failures: new Map(),

  /**
   * Records a failed operation of a policy. Callers log the message
   * themselves, with their own logger.
   *
   * @param {string} policyName
   *        The policy the failed operation belongs to.
   * @param {string} message
   *        A description of what failed.
   */
  report(policyName, message) {
    let messages = this._failures.get(policyName);
    if (!messages) {
      messages = [];
      this._failures.set(policyName, messages);
    }
    if (!messages.includes(message)) {
      messages.push(message);
    }
  },

  /**
   * @returns {object}
   *          The failures of every policy, keyed by policy name.
   */
  getAll() {
    const failures = {};
    for (const [policyName, messages] of this._failures) {
      failures[policyName] = [...messages];
    }
    return failures;
  },

  /**
   * Forgets the failures of a policy, which happens when the policy is
   * applied again or removed.
   *
   * @param {string} policyName policy name
   */
  clear(policyName) {
    this._failures.delete(policyName);
  },

  clearAll() {
    this._failures.clear();
  },
};

/**
 * Logs a failed operation of a policy and records it against that policy, so
 * that about:policies reports the policy as only partially applied. Add-on
 * installations happen asynchronously, long after the policy callback
 * returned, so their failures have to be reported here explicitly to be
 * visible at all.
 *
 * @param {string} policyName
 *        The policy the failed operation belongs to. When it isn't known, the
 *        failure is only logged.
 * @param {string} message
 *        A description of what failed.
 */
export function reportFailure(policyName, message) {
  lazy.log.error(message);
  if (policyName) {
    PolicyFailures.report(policyName, message);
  }
}

const PREF_TYPE_NAMES = {
  [Ci.nsIPrefBranch.PREF_BOOL]: "boolean",
  [Ci.nsIPrefBranch.PREF_INT]: "number",
  [Ci.nsIPrefBranch.PREF_STRING]: "string",
};

/**
 * Describes why a preference could not be set, in terms an administrator can
 * act on: which type the preference takes, and what the policy provided.
 *
 * @param {string} preference
 *        The preference that could not be set.
 * @param {boolean|number|string} value
 *        The value the policy asked for.
 * @param {Error} ex
 *        The failure raised while setting it.
 * @returns {string}
 *        A description of what to correct.
 */
export function describePreferenceFailure(preference, value, ex) {
  const expected =
    PREF_TYPE_NAMES[
      Services.prefs.getDefaultBranch("").getPrefType(preference)
    ];
  if (expected && expected != typeof value) {
    return (
      `Unable to set preference ${preference}: it takes a ${expected}, ` +
      `but the policy provided a ${typeof value}.`
    );
  }
  if (expected == "number" && !Number.isInteger(value)) {
    return `Unable to set preference ${preference}: it takes a whole number.`;
  }
  const reason = ex?.result
    ? ChromeUtils.getXPCOMErrorName(ex.result)
    : (ex?.message ?? ex);
  return `Unable to set preference ${preference}: ${reason}`;
}

/**
 * addPolicyPermission
 *
 * Sets a permission from a policy site list. A host and its trailing dot form
 * are distinct permission origins, so both get the permission.
 *
 * @param {URL|string} origin
 *        The origin the permission applies to.
 * @param {string} permissionName
 *        The name of the permission to set.
 * @param {number} permission
 *        The permission value to set, for example ALLOW_ACTION.
 */
export function addPolicyPermission(origin, permissionName, permission) {
  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(origin);

  for (let prin of [principal, trailingDotPrincipal(principal)]) {
    if (prin) {
      Services.perms.addFromPrincipal(
        prin,
        permissionName,
        permission,
        Ci.nsIPermissionManager.EXPIRE_POLICY
      );
    }
  }
}

/**
 * Returns the principal for aPrincipal's other host form: bare for a host with
 * trailing dots, one trailing dot for a bare host. Null when there is no such
 * principal: no host, an IP address host, or a host setHost rejects.
 *
 * @param {nsIPrincipal} aPrincipal
 * @returns {nsIPrincipal?}
 */
function trailingDotPrincipal(aPrincipal) {
  let host = principalHost(aPrincipal);

  if (!host || aPrincipal.isIpAddress) {
    return null;
  }

  let bareHost = lazy.SitePolicyUtils.stripTrailingDots(host);
  let otherHost = bareHost == host ? `${host}.` : bareHost;

  try {
    return Services.scriptSecurityManager.createContentPrincipal(
      aPrincipal.URI.mutate().setHost(otherHost).finalize(),
      aPrincipal.originAttributes
    );
  } catch (ex) {
    return null;
  }
}

// nsIPrincipal.host throws for a URI with no host, such as an about: URI.
function principalHost(aPrincipal) {
  try {
    return aPrincipal.host;
  } catch (ex) {
    return "";
  }
}

/**
 * isTrailingDotPolicyDuplicate
 *
 * True when aPermission is a policy permission for a trailing dot host and the
 * same policy permission exists for the bare host. A permission list UI lists
 * the site once, so it skips these. A trailing dot host with a permission of
 * its own is not a duplicate and stays listed.
 *
 * Callers check expireType first, so a profile with no policy permissions
 * never imports this module.
 *
 * @param {nsIPermission} aPermission
 * @returns {boolean}
 */
export function isTrailingDotPolicyDuplicate(aPermission) {
  if (
    aPermission.expireType != Ci.nsIPermissionManager.EXPIRE_POLICY ||
    !principalHost(aPermission.principal).endsWith(".")
  ) {
    return false;
  }

  let barePrincipal = trailingDotPrincipal(aPermission.principal);
  let barePermission =
    barePrincipal &&
    Services.perms.getPermissionObject(barePrincipal, aPermission.type, true);

  return (
    barePermission?.expireType == Ci.nsIPermissionManager.EXPIRE_POLICY &&
    barePermission.capability == aPermission.capability
  );
}
