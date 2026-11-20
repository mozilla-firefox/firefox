/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPPAutoActivation:
    "moz-src:///toolkit/components/ipprotection/IPPAutoActivation.sys.mjs",
  IPPMode:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  IPPPrincipalRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPPSiteRuleManager:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPTabRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
});

/**
 * The principal IPPChannelFilter attributes a document load to: the embedding
 * document for a subframe, the loaded URL for a top-level page.
 *
 * @param {BrowsingContext} browsingContext
 * @returns {nsIPrincipal}
 */
function principalForDocumentLoad(browsingContext) {
  const embedder = browsingContext.parent?.currentWindowGlobal;
  if (embedder) {
    return embedder.documentPrincipal;
  }
  return Services.scriptSecurityManager.createContentPrincipal(
    browsingContext.currentURI,
    browsingContext.originAttributes
  );
}

/**
 * Helpers for the VPN failure page in about:neterror.
 */
export const IPPNetErrorUtils = {
  /**
   * Whether the failed load in this browsing context went through IP
   * Protection, so a proxy failure there is the VPN's.
   *
   * @param {BrowsingContext} browsingContext - context showing the error page
   * @returns {boolean}
   */
  isVPNError(browsingContext) {
    if (!lazy.IPPProxyManager.active) {
      return false;
    }
    const rule = lazy.IPPSiteRuleManager.getRule(
      principalForDocumentLoad(browsingContext),
      browsingContext.top.browserId
    );
    if (rule === lazy.IPPPrincipalRules.INCLUDED) {
      return true;
    }
    if (rule === lazy.IPPPrincipalRules.EXCLUDED) {
      return false;
    }
    switch (lazy.IPPProxyManager.mode) {
      case lazy.IPPMode.MODE_FULL:
        return true;
      case lazy.IPPMode.MODE_PB:
        return browsingContext.usePrivateBrowsing;
      default:
        return false;
    }
  },

  /**
   * Skips the VPN for the failed site in this tab until the tab leaves it,
   * and reloads the failed page.
   *
   * @param {BrowsingContext} browsingContext - context showing the error page
   */
  continueWithoutVPN(browsingContext) {
    lazy.IPPTabRules.setTabRule(
      lazy.IPPPrincipalRules.EXCLUDED,
      browsingContext.top.browserId,
      {
        principal: principalForDocumentLoad(browsingContext),
        removeOnLeave: true,
      }
    );
    browsingContext.reload(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  },

  /**
   * Retries the failed connection in the mode it had, and reloads the failed
   * page once it is up. A failed retry leaves the error page as it is.
   *
   * @param {BrowsingContext} browsingContext - context showing the error page
   */
  async reconnectAndReload(browsingContext) {
    if (lazy.IPPProxyManager.state !== lazy.IPPProxyStates.ERROR) {
      browsingContext.reload(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
      return;
    }
    const { started } = await lazy.IPPProxyManager.start(
      true,
      browsingContext.usePrivateBrowsing,
      lazy.IPProtectionServerlist.resolveEgressCountry(
        lazy.IPPAutoActivation.isPremium
      ),
      lazy.IPPProxyManager.mode
    );
    if (!started || browsingContext.isDiscarded) {
      return;
    }
    browsingContext.reload(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
  },
};
