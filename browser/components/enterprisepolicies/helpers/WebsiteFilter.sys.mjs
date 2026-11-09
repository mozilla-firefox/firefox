/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module implements the policy to block websites from being visited,
 * or to only allow certain websites to be visited.
 *
 * The blocklist takes as input an array of MatchPattern strings, as documented
 * at https://developer.mozilla.org/en-US/Add-ons/WebExtensions/Match_patterns.
 *
 * The exceptions list takes the same as input. This list opens up
 * exceptions for rules on the blocklist that might be too strict.
 *
 * In addition to that, this allows the user to create an allowlist approach,
 * by using the special "<all_urls>" pattern for the blocklist, and then
 * adding all allowlisted websites on the exceptions list.
 *
 * Note that this module only blocks top-level website navigations and embeds.
 * It does not block any other accesses to these urls: image tags, scripts, XHR, etc.,
 * because that could cause unexpected breakage. This is a policy to block
 * users from visiting certain websites, and not from blocking any network
 * connections to those websites. If the admin is looking for that, the recommended
 * way is to configure that with extensions or through a company firewall.
 */

const LIST_LENGTH_LIMIT = 1000;

const PREF_LOGLEVEL = "browser.policies.loglevel";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PolicyFailures: "resource://gre/modules/PoliciesHelpers.sys.mjs",
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  SitePolicyUtils: "resource://gre/modules/SitePolicyUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "WebsiteFilter Policy",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

/**
 * Reports an operation of a policy that failed, so that the policy is flagged
 * as only partially applied in about:policies.
 *
 * @param {string} message A description of what failed.
 */
function reportFailure(message) {
  lazy.log.error(message);
  lazy.PolicyFailures.report("WebsiteFilter", message);
}

export let WebsiteFilter = {
  _observerAdded: false,

  init(blocklist, exceptionlist) {
    let blockArray = [],
      exceptionArray = [];

    for (let i = 0; i < blocklist.length && i < LIST_LENGTH_LIMIT; i++) {
      try {
        let pattern = new MatchPattern(blocklist[i].toLowerCase());
        blockArray.push(pattern);
        lazy.log.debug(
          `Pattern added to WebsiteFilter. Block: ${blocklist[i]}`
        );
      } catch (e) {
        reportFailure(
          `Invalid pattern on WebsiteFilter. Block: ${blocklist[i]}`
        );
      }
    }

    this._blockPatterns = new MatchPatternSet(blockArray);

    for (let i = 0; i < exceptionlist.length && i < LIST_LENGTH_LIMIT; i++) {
      try {
        let pattern = new MatchPattern(exceptionlist[i].toLowerCase());
        exceptionArray.push(pattern);
        lazy.log.debug(
          `Pattern added to WebsiteFilter. Exception: ${exceptionlist[i]}`
        );
      } catch (e) {
        reportFailure(
          `Invalid pattern on WebsiteFilter. Exception: ${exceptionlist[i]}`
        );
      }
    }

    if (exceptionArray.length) {
      this._exceptionsPatterns = new MatchPatternSet(exceptionArray);
    }

    let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);

    if (!registrar.isContractIDRegistered(this.contractID)) {
      registrar.registerFactory(
        this.classID,
        this.classDescription,
        this.contractID,
        this
      );

      Services.catMan.addCategoryEntry(
        "content-policy",
        this.contractID,
        this.contractID,
        false,
        true
      );

      // Backstop for redirects the observers below never see.
      Services.catMan.addCategoryEntry(
        "net-channel-event-sinks",
        this.contractID,
        this.contractID,
        false,
        true
      );
    }
    // Cancelling here, rather than in the event sink, is what gives the user
    // the error page.
    // See bug 456957.
    if (!this._observerAdded) {
      this._observerAdded = true;
      // We rely on weak references, so we never remove these observers.
      // A 30X can also come from the cache, so all three topics matter.
      Services.obs.addObserver(this, "http-on-examine-response", true);
      Services.obs.addObserver(this, "http-on-examine-cached-response", true);
      Services.obs.addObserver(this, "http-on-examine-merged-response", true);
    }
  },

  asyncOnChannelRedirect(oldChannel, newChannel, flags, callback) {
    let contentType = newChannel.loadInfo.externalContentPolicyType;
    if (
      (contentType == Ci.nsIContentPolicy.TYPE_DOCUMENT ||
        contentType == Ci.nsIContentPolicy.TYPE_SUBDOCUMENT ||
        contentType == Ci.nsIContentPolicy.TYPE_OBJECT) &&
      !this.isAllowed(newChannel.URI.spec)
    ) {
      oldChannel.cancel(Cr.NS_ERROR_BLOCKED_BY_POLICY);
      callback.onRedirectVerifyCallback(Cr.NS_ERROR_BLOCKED_BY_POLICY);
      return;
    }
    callback.onRedirectVerifyCallback(Cr.NS_OK);
  },

  shouldLoad(contentLocation, loadInfo) {
    let contentType = loadInfo.externalContentPolicyType;
    let url = contentLocation.spec;
    if (contentLocation.scheme == "view-source") {
      url = contentLocation.pathQueryRef;
    } else if (url.toLowerCase().startsWith("about:reader?")) {
      url = lazy.ReaderMode.getOriginalUrl("about:reader?" + url.substring(13));
    }
    if (
      contentType == Ci.nsIContentPolicy.TYPE_DOCUMENT ||
      contentType == Ci.nsIContentPolicy.TYPE_SUBDOCUMENT ||
      contentType == Ci.nsIContentPolicy.TYPE_OBJECT
    ) {
      if (!url || !this.isAllowed(url)) {
        return Ci.nsIContentPolicy.REJECT_POLICY;
      }
    }
    return Ci.nsIContentPolicy.ACCEPT;
  },
  shouldProcess() {
    return Ci.nsIContentPolicy.ACCEPT;
  },
  observe(subject) {
    try {
      let channel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (channel.responseStatus < 300 || channel.responseStatus >= 400) {
        return;
      }
      // isDocument alone misses document loads whose channel has had
      // LOAD_DOCUMENT_URI cleared.
      let contentType = channel.loadInfo.externalContentPolicyType;
      if (
        !channel.isDocument &&
        contentType != Ci.nsIContentPolicy.TYPE_DOCUMENT &&
        contentType != Ci.nsIContentPolicy.TYPE_SUBDOCUMENT &&
        contentType != Ci.nsIContentPolicy.TYPE_OBJECT
      ) {
        return;
      }
      let location = channel.getResponseHeader("location");
      // location might not be a fully qualified URL
      let url = URL.parse(location);
      if (!url) {
        url = URL.parse(location, channel.URI.spec);
      }
      if (url && !this.isAllowed(url.href)) {
        channel.cancel(Cr.NS_ERROR_BLOCKED_BY_POLICY);
      }
    } catch (e) {}
  },
  classDescription: "Policy Engine File Content Policy",
  contractID: "@mozilla-org/policy-engine-file-content-policy-service;1",
  classID: Components.ID("{c0bbb557-813e-4e25-809d-b46a531a258f}"),
  QueryInterface: ChromeUtils.generateQI([
    "nsIContentPolicy",
    "nsIChannelEventSink",
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),
  createInstance(iid) {
    return this.QueryInterface(iid);
  },
  isAllowed(url) {
    let normalizedURL = this.normalizeURL(url);
    // A URL we are about to load should always parse, so this is unexpected.
    // Block it rather than let an unparseable URL skip the filter.
    if (normalizedURL == null) {
      return false;
    }
    if (this._blockPatterns?.matches(normalizedURL)) {
      if (
        !this._exceptionsPatterns ||
        !this._exceptionsPatterns.matches(normalizedURL)
      ) {
        return false;
      }
    }
    return true;
  },
  normalizeURL(url) {
    let parsed = URL.parse(url);
    if (!parsed) {
      return null;
    }
    parsed.hostname = lazy.SitePolicyUtils.stripTrailingDots(parsed.hostname);
    return parsed.href.toLowerCase();
  },
};
