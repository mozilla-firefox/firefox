/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  IPPInfrastructureRuleProvider,
  IPPPermissionRuleProvider,
  IPPPrincipalRules,
  IPPProxyableRuleProvider,
  IPPTabRuleProvider,
  MatchPatternPrefRule,
} from "moz-src:///toolkit/components/ipprotection/IPPSiteRuleProviders.sys.mjs";

export { IPPPrincipalRules };

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_SiteRuleManager",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

/**
 * Resolves a principal to a rule by asking an ordered list of providers.
 *
 * The array order is the precedence: the first provider with an opinion wins,
 * and the ones after it are never consulted.
 */
export class SiteRuleManager extends EventTarget {
  #providers;
  #inited = false;
  #onProviderChange;

  /**
   * @param {SiteRuleProvider[]} providers
   *  Ordered most-important first.
   */
  constructor(providers) {
    super();
    this.#providers = providers;
    // The event carries no payload. Which provider changed does not map to
    // which principals changed rule: precedence means one provider's update
    // can start or stop masking another's.
    this.#onProviderChange = () => {
      this.dispatchEvent(new CustomEvent("SiteRuleManager:RuleChanged"));
    };
  }

  init() {
    if (this.#inited) {
      return;
    }
    // Set before initing the providers: a provider may notify from within
    // init(), and re-entering here would double-register the listeners.
    this.#inited = true;
    for (const provider of this.#providers) {
      provider.addEventListener("change", this.#onProviderChange);
      try {
        provider.init();
      } catch (error) {
        provider.uninit();
        lazy.logConsole.error(
          `${provider.constructor.name} failed to init:`,
          error
        );
        Glean.ipprotection.error.record({
          source: `SiteRuleManager:${provider.constructor.name}`,
        });
      }
    }
  }

  uninit() {
    if (!this.#inited) {
      return;
    }
    for (const provider of this.#providers) {
      provider.removeEventListener("change", this.#onProviderChange);
      provider.uninit();
    }
    this.#inited = false;
  }

  /**
   * The rule that applies to a principal.
   *
   * @param {?nsIPrincipal} principal
   * @param {number} [browserId]
   *  The tab the principal is loaded in, for rules that only hold in one tab.
   * @returns {?string}
   *  The first non-null opinion, or null if no provider has one.
   */
  getRule(principal, browserId) {
    if (!this.#inited) {
      this.init();
    }
    try {
      for (const provider of this.#providers) {
        const rule = provider.getRule(principal, browserId);
        if (rule != null) {
          return rule;
        }
      }
      return null;
    } catch (_) {
      return IPPPrincipalRules.EXCLUDED;
    }
  }

  /**
   * Whether any provider holds at least one site with the rule.
   *
   * @param {?string} rule
   * @returns {boolean}
   */
  hasRule(rule) {
    if (!this.#inited) {
      this.init();
    }
    return this.#providers.some(provider => provider.count(rule) > 0);
  }

  /**
   * Whether the user can change the rule for this principal. False when a
   * higher-precedence provider already claims it.
   *
   * @param {?nsIPrincipal} principal
   * @returns {boolean}
   */
  canManage(principal) {
    if (!this.#inited) {
      this.init();
    }
    try {
      for (const provider of this.#providers) {
        // canSet first, so a site the user already set stays manageable.
        if (provider.canSet(principal)) {
          return true;
        }
        if (provider.getRule(principal) != null) {
          return false;
        }
      }
      return false;
    } catch (_) {
      return false;
    }
  }
}

/**
 * The one writable rule store. Callers write here, after asking the manager
 * whether the principal is manageable at all.
 */
export const IPPPermissionRules = new IPPPermissionRuleProvider();

/**
 * Rules that cover a single tab, such as the VPN being skipped there after
 * the VPN error page.
 */
export const IPPTabRules = new IPPTabRuleProvider();

/**
 * The single source of truth for classifying a principal as
 * included/excluded/default for the proxy, shared by the channel filter and
 * the UI. The array order is the precedence.
 */
export const IPPSiteRuleManager = new SiteRuleManager([
  new IPPProxyableRuleProvider(),
  new IPPInfrastructureRuleProvider(),
  IPPTabRules,
  new MatchPatternPrefRule(
    "browser.ipProtection.inclusion.match_patterns",
    IPPPrincipalRules.INCLUDED
  ),
  IPPPermissionRules,
]);
