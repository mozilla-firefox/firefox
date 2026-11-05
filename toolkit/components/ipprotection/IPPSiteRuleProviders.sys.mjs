/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_SiteRuleProviders",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

const MATCH_PATTERN_OPTIONS = {
  ignorePath: true,
  restrictSchemes: false,
};

/** @typedef {"included"|"excluded"|null} IPPPrincipalRule */

/**
 * DEFAULT is null because that is what a SiteRuleManager returns when no
 * provider claimed the principal.
 */
export const IPPPrincipalRules = Object.freeze({
  INCLUDED: "included",
  EXCLUDED: "excluded",
  DEFAULT: null,
});

/**
 * Base class for site rule providers.
 *
 * A provider answers "which rule applies to this principal?" for exactly one
 * source of truth, so that adding a new source is adding a new subclass rather
 * than another branch in a shared if-chain.
 *
 * Subclasses override what they need. The defaults are safe no-ops: no opinion
 * and no writes. A provider that stores rules also overrides canSet and
 * setRule.
 *
 * Providers call notifyChange when their underlying data changes; the manager
 * listens for that and re-dispatches a single SiteRuleManager:RuleChanged to
 * consumers, so nothing outside has to subscribe to providers individually.
 */
export class SiteRuleProvider extends EventTarget {
  /**
   * Registers observers and builds any cached state. Called by the manager.
   */
  init() {}

  uninit() {}

  /**
   * This provider's opinion for a principal.
   *
   * @param {?nsIPrincipal} _principal
   * @param {number} [_browserId] - the tab the principal is loaded in.
   * @returns {?string}
   *  The rule, or null for "no opinion, ask the next provider".
   */
  getRule(_principal, _browserId) {
    return null;
  }

  /**
   * Whether a user-initiated write for this principal would be stored here.
   * Read-only providers keep the default.
   *
   * @param {?nsIPrincipal} _principal
   * @returns {boolean}
   */
  canSet(_principal) {
    return false;
  }

  /**
   * Stores a rule for a principal, or clears it when rule is null.
   *
   * @param {nsIPrincipal} _principal
   * @param {?string} _rule
   */
  setRule(_principal, _rule) {
    throw new Error("setRule() must be implemented by writable subclasses");
  }

  /**
   * How many sites this provider holds with the given rule.
   *
   * @param {?string} _rule
   * @returns {number}
   */
  count(_rule) {
    return 0;
  }

  /**
   * Tells the manager this provider's underlying data changed.
   */
  notifyChange() {
    this.dispatchEvent(new CustomEvent("change"));
  }
}

/**
 * Traffic that can never be proxied, whatever the user or the lists say.
 */
export class IPPProxyableRuleProvider extends SiteRuleProvider {
  getRule(principal) {
    // Exclude non-http(s) schemes (about:, file:, etc.), but NOT null
    // principals: a null principal's scheme is moz-nullprincipal even when it
    // backs real http(s) content (e.g. a sandboxed iframe), so its scheme says
    // nothing about whether the traffic should be proxied.
    if (
      !principal?.isNullPrincipal &&
      !principal?.schemeIs("http") &&
      !principal?.schemeIs("https")
    ) {
      return IPPPrincipalRules.EXCLUDED;
    }
    if (principal.isLoopbackHost || principal.isLocalIpAddress) {
      return IPPPrincipalRules.EXCLUDED;
    }
    return null;
  }
}

/**
 * Origins the VPN itself depends on (the guardian endpoint, captive portal
 * detection). Proxying these would break the VPN, so they beat inclusions.
 */
export class IPPInfrastructureRuleProvider extends SiteRuleProvider {
  static #DEFAULT_URL_PREFS = [
    "browser.ipProtection.guardian.endpoint",
    "captivedetect.canonicalURL",
  ];

  #origins = new MatchPatternSet([], MATCH_PATTERN_OPTIONS);
  #observedPrefs = [];
  #prefObserver = null;

  init() {
    // The excluded origins come from the default-excluded prefs plus the active
    // auth provider's prefs; observe each so the set stays current.
    this.#observedPrefs = [
      ...IPPInfrastructureRuleProvider.#DEFAULT_URL_PREFS,
      ...IPPInfrastructureRuleProvider.#authProviderPrefs(),
    ];
    this.#prefObserver = () => {
      this.#rebuild();
      this.notifyChange();
    };
    for (const pref of this.#observedPrefs) {
      Services.prefs.addObserver(pref, this.#prefObserver);
    }
    this.#rebuild();
  }

  uninit() {
    if (!this.#prefObserver) {
      return;
    }
    for (const pref of this.#observedPrefs) {
      Services.prefs.removeObserver(pref, this.#prefObserver);
    }
    this.#prefObserver = null;
    this.#observedPrefs = [];
  }

  getRule(principal) {
    const uri = principal?.URI;
    if (uri && this.#origins.matches(uri)) {
      return IPPPrincipalRules.EXCLUDED;
    }
    return null;
  }

  #rebuild() {
    const patterns = [];
    for (const pref of this.#observedPrefs) {
      const pattern = IPPInfrastructureRuleProvider.#toHostPattern(pref);
      if (pattern) {
        patterns.push(pattern);
      }
    }
    try {
      this.#origins = new MatchPatternSet(patterns, MATCH_PATTERN_OPTIONS);
    } catch (error) {
      // Keep the origins we already had: dropping an infrastructure exclusion
      // would send the VPN's own traffic through the proxy.
      lazy.logConsole.error("Cannot build the infrastructure origins:", error);
      Glean.ipprotection.error.record({
        source: "IPPInfrastructureRuleProvider:Origins",
      });
    }
  }

  /**
   * The active auth provider's excluded prefs, or an empty list if it has none
   * or cannot be reached.
   *
   * @returns {string[]}
   */
  static #authProviderPrefs() {
    try {
      const prefs = lazy.IPProtectionService.authProvider?.excludedUrlPrefs;
      return Array.isArray(prefs) ? prefs : [];
    } catch (error) {
      lazy.logConsole.error("Cannot read the auth provider's prefs:", error);
      Glean.ipprotection.error.record({
        source: "IPPInfrastructureRuleProvider:AuthProviderPrefs",
      });
      return [];
    }
  }

  /**
   * Converts the excluded page URL held by a pref to a host match pattern
   * (scheme://host/*), or null if the pref cannot be read or parsed.
   *
   * @param {string} pref
   * @returns {?string}
   */
  static #toHostPattern(pref) {
    try {
      const uri = Services.io.newURI(Services.prefs.getStringPref(pref, ""));
      return `${uri.scheme}://${uri.host}/*`;
    } catch (_) {
      return null;
    }
  }
}

/**
 * Applies one rule to every origin matching a MatchPatternSet, read from a
 * pref holding a JSON array of match patterns. Used for remotely controlled
 * lists, where the set changes without a restart.
 */
export class MatchPatternPrefRule extends SiteRuleProvider {
  #pref;
  #rule;
  #patterns = new MatchPatternSet([], MATCH_PATTERN_OPTIONS);
  #prefObserver = null;

  /**
   * @param {string} pref
   *  Pref holding a JSON array of match pattern strings.
   * @param {string} rule
   *  The rule to apply to a principal the patterns match.
   */
  constructor(pref, rule) {
    super();
    this.#pref = pref;
    this.#rule = rule;
  }

  init() {
    this.#prefObserver = () => {
      this.#rebuild();
      this.notifyChange();
    };
    Services.prefs.addObserver(this.#pref, this.#prefObserver);
    this.#rebuild();
  }

  uninit() {
    if (!this.#prefObserver) {
      return;
    }
    Services.prefs.removeObserver(this.#pref, this.#prefObserver);
    this.#prefObserver = null;
  }

  getRule(principal) {
    const uri = principal?.URI;
    if (uri && this.#patterns.matches(uri)) {
      return this.#rule;
    }
    return null;
  }

  count(rule) {
    return rule === this.#rule ? this.#patterns.patterns.length : 0;
  }

  #rebuild() {
    try {
      let arr = JSON.parse(Services.prefs.getStringPref(this.#pref, "[]"));
      if (!Array.isArray(arr)) {
        throw new TypeError(`${this.#pref} does not contain a JSON array`);
      }
      let patterns = arr.filter(s => typeof s === "string" && s.length);
      this.#patterns = new MatchPatternSet(patterns, MATCH_PATTERN_OPTIONS);
    } catch (error) {
      // An unreadable list means no opinion rather than a provider that cannot
      // init. The pref observer stays registered, so a later good value applies.
      this.#patterns = new MatchPatternSet([], MATCH_PATTERN_OPTIONS);
      lazy.logConsole.error(`Cannot read ${this.#pref}:`, error);
      Glean.ipprotection.error.record({
        source: "MatchPatternPrefRule:BadPref",
      });
    }
  }
}

/**
 * The user's own per-site choice, stored in the ipp-vpn permission type:
 * ALLOW for inclusions, DENY for exclusions. A site holds at most one of the
 * two, because addFromPrincipal replaces any existing ipp-vpn permission for
 * the principal.
 *
 * This is the only writable provider. Permissions related UI (eg. panels and
 * dialogs) already handles changes to ipp-vpn; this class exists so that
 * non-permissions related UI can update it too.
 *
 * While the site inclusions feature is on, every write goes through setRule,
 * so rules are kept on the https origin without a leading "www." and
 * "http://www.example.com" and "https://example.com" share one rule. Entries
 * stored before that are normalized once, the first time the feature is seen
 * on; turning it off lets exact entries back in, so the pass runs again the
 * next time it is turned on.
 */
export class IPPPermissionRuleProvider extends SiteRuleProvider {
  static #PERM_NAME = "ipp-vpn";
  static #SITE_INCLUSIONS_PREF = "browser.ipProtection.features.siteInclusions";
  static #NORMALIZED_PREF = "browser.ipProtection.siteRulesNormalized";

  #observer = null;
  #normalizing = false;

  init() {
    this.#maybeNormalizeStore();

    // ES6 classes that extend EventTarget cannot be coerced into nsIObserver.
    // Work around this by using a function as the observer.
    this.#observer = (subject, topic, data) => {
      this.observe(subject, topic, data);
    };
    Services.obs.addObserver(this.#observer, "perm-changed");
    Services.prefs.addObserver(
      IPPPermissionRuleProvider.#SITE_INCLUSIONS_PREF,
      this.#observer
    );
    this.#recordCounts();
  }

  uninit() {
    if (!this.#observer) {
      return;
    }
    Services.obs.removeObserver(this.#observer, "perm-changed");
    Services.prefs.removeObserver(
      IPPPermissionRuleProvider.#SITE_INCLUSIONS_PREF,
      this.#observer
    );
    this.#observer = null;
  }

  observe(subject, topic, data) {
    if (topic === "nsPref:changed") {
      this.#maybeNormalizeStore();
      return;
    }

    if (topic !== "perm-changed") {
      return;
    }

    let permission = subject.QueryInterface(Ci.nsIPermission);
    if (permission.type !== IPPPermissionRuleProvider.#PERM_NAME) {
      return;
    }

    if (data !== "added" && data !== "deleted" && data !== "changed") {
      return;
    }

    // "changed" carries the new permission, so replacing an exclusion with an
    // inclusion (or the reverse) is reported under the new capability.
    if (
      data === "added" &&
      permission.capability === Ci.nsIPermissionManager.DENY_ACTION
    ) {
      Glean.ipprotection.exclusionAdded.add(1);
    }

    this.#recordCounts();
    this.notifyChange();
  }

  getRule(principal) {
    return this.#ruleForCapability(
      this.getPermissionObject(principal)?.capability
    );
  }

  canSet(principal) {
    // Whether a write is worth making is the manager's call: it knows if a
    // higher-precedence provider would mask it. Here we only need a principal
    // to hang the permission on.
    return !!principal;
  }

  setRule(principal, rule) {
    if (!principal || rule === this.getRule(principal)) {
      return;
    }

    const normalized = this.#normalize(principal);
    if (rule === IPPPrincipalRules.DEFAULT) {
      Services.perms.removeFromPrincipal(
        normalized,
        IPPPermissionRuleProvider.#PERM_NAME
      );
      return;
    }

    Services.perms.addFromPrincipal(
      normalized,
      IPPPermissionRuleProvider.#PERM_NAME,
      rule === IPPPrincipalRules.INCLUDED
        ? Ci.nsIPermissionManager.ALLOW_ACTION
        : Ci.nsIPermissionManager.DENY_ACTION
    );
  }

  count(rule) {
    let count = 0;
    for (let perm of Services.perms.getAllByTypes([
      IPPPermissionRuleProvider.#PERM_NAME,
    ])) {
      if (this.#ruleForCapability(perm.capability) === rule) {
        count++;
      }
    }
    return count;
  }

  /**
   * Get the permission object for a site exception if it exists in ipp-vpn.
   *
   * Use exactHost=true to only match the specific origin, not the base domain.
   * This ensures that subdomains aren't implicitly excluded when entering
   * a site in the about:preferences dialog. It also avoids an issue where we
   * try to remove a subdomain as an exclusion when the site doesn't exist in ipp-vpn
   * (see Bug 2016676).
   *
   * Eg. if we enter "example.com" in the dialog, "subdomain.example.com" won't
   * be considered an exclusion.
   *
   * @param {nsIPrincipal} principal
   *  The principal that we want to check is saved in ipp-vpn.
   *
   * @returns {nsIPermission}
   *  The permission object for a site exception, or null if unavailable.
   */
  getPermissionObject(principal) {
    return Services.perms.getPermissionObject(
      this.#normalize(principal),
      IPPPermissionRuleProvider.#PERM_NAME,
      true /* exactHost */
    );
  }

  /**
   * Follows the site inclusions pref, normalizing the store the first time it
   * is seen on.
   */
  #maybeNormalizeStore() {
    this.#normalizing = Services.prefs.getBoolPref(
      IPPPermissionRuleProvider.#SITE_INCLUSIONS_PREF,
      false
    );
    if (!this.#normalizing) {
      Services.prefs.clearUserPref(IPPPermissionRuleProvider.#NORMALIZED_PREF);
      return;
    }
    if (
      !Services.prefs.getBoolPref(
        IPPPermissionRuleProvider.#NORMALIZED_PREF,
        false
      )
    ) {
      this.#normalizePermissionStore();
      Services.prefs.setBoolPref(
        IPPPermissionRuleProvider.#NORMALIZED_PREF,
        true
      );
    }
  }

  /**
   * Moves every stored rule onto its normalized principal. When several
   * collapse onto one, the most recently modified wins.
   */
  #normalizePermissionStore() {
    const winners = new Map();
    for (const perm of Services.perms.getAllByTypes([
      IPPPermissionRuleProvider.#PERM_NAME,
    ])) {
      const stored = perm.principal;
      const principal = this.#normalize(stored);
      if (principal !== stored) {
        Services.perms.removePermission(perm);
      }
      const winner = winners.get(principal.origin);
      if (!winner || winner.perm.modificationTime < perm.modificationTime) {
        winners.set(principal.origin, { principal, stored, perm });
      }
    }

    for (const { principal, stored, perm } of winners.values()) {
      if (principal !== stored) {
        Services.perms.addFromPrincipal(
          principal,
          IPPPermissionRuleProvider.#PERM_NAME,
          perm.capability,
          perm.expireType,
          perm.expireTime
        );
      }
    }
  }

  /**
   * The principal a rule for this one is stored on: while normalizing, https
   * with its host passed through #stripWww. Other principals, and every
   * principal while not normalizing, are returned as they are.
   *
   * @param {nsIPrincipal} principal
   * @returns {nsIPrincipal}
   */
  #normalize(principal) {
    if (
      !this.#normalizing ||
      !principal?.isContentPrincipal ||
      !(principal.schemeIs("http") || principal.schemeIs("https"))
    ) {
      return principal;
    }
    const host = IPPPermissionRuleProvider.#stripWww(principal.host);
    if (principal.schemeIs("https") && host === principal.host) {
      return principal;
    }
    const uri = principal.URI.mutate()
      .setScheme("https")
      .setHost(host)
      .finalize();
    return Services.scriptSecurityManager.createContentPrincipal(
      uri,
      principal.originAttributes
    );
  }

  /**
   * The host without a leading "www.", unless nothing or only a public suffix
   * would remain (e.g. "www.co.uk").
   *
   * @param {string} host
   * @returns {string}
   */
  static #stripWww(host) {
    if (!host.startsWith("www.")) {
      return host;
    }
    const bareHost = host.slice("www.".length);
    if (!bareHost) {
      return host;
    }
    const isPublicSuffix =
      Services.eTLD.getPublicSuffixFromHost(bareHost) === bareHost;
    return isPublicSuffix ? host : bareHost;
  }

  #recordCounts() {
    Glean.ipprotectionInclusions.count.set(
      this.count(IPPPrincipalRules.INCLUDED)
    );
    Glean.ipprotectionExclusions.count.set(
      this.count(IPPPrincipalRules.EXCLUDED)
    );
  }

  #ruleForCapability(capability) {
    switch (capability) {
      case Ci.nsIPermissionManager.ALLOW_ACTION:
        return IPPPrincipalRules.INCLUDED;
      case Ci.nsIPermissionManager.DENY_ACTION:
        return IPPPrincipalRules.EXCLUDED;
      default:
        return IPPPrincipalRules.DEFAULT;
    }
  }
}

/**
 * Rules that cover every site in a single tab, for as long as it is open.
 */
export class IPPTabRuleProvider extends SiteRuleProvider {
  /** @type {Map<number, string>} */
  #rules = new Map();
  #observer = null;

  init() {
    // ES6 classes that extend EventTarget cannot be coerced into nsIObserver.
    this.#observer = (subject, topic, data) => {
      this.observe(subject, topic, data);
    };
    Services.obs.addObserver(this.#observer, "browsing-context-discarded");
  }

  observe(browsingContext, topic, why) {
    // A replaced context hands its browserId to the next one: same tab.
    if (
      topic !== "browsing-context-discarded" ||
      browsingContext.parent ||
      why === "replace"
    ) {
      return;
    }
    if (this.#rules.delete(browsingContext.browserId)) {
      this.notifyChange();
    }
  }

  uninit() {
    if (!this.#observer) {
      return;
    }
    Services.obs.removeObserver(this.#observer, "browsing-context-discarded");
    this.#observer = null;
    this.#rules.clear();
  }

  getRule(_principal, browserId) {
    return this.#rules.get(browserId) ?? null;
  }

  /**
   * Sets the rule for every site in one tab until it closes.
   *
   * @param {string} rule
   *  An IPPPrincipalRules value; DEFAULT removes the rule.
   * @param {number} browserId
   *  The tab.
   */
  setTabRule(rule, browserId) {
    if (!browserId || (this.#rules.get(browserId) ?? null) === rule) {
      return;
    }
    if (rule) {
      this.#rules.set(browserId, rule);
    } else {
      this.#rules.delete(browserId);
    }
    this.notifyChange();
  }

  count(rule) {
    let count = 0;
    for (const tabRule of this.#rules.values()) {
      count += tabRule === rule ? 1 : 0;
    }
    return count;
  }
}
