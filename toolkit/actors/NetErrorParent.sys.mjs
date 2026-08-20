/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const PREF_SSL_IMPACT_ROOTS = [
  "security.tls.version.",
  "security.ssl3.",
  "security.tls13.",
];

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchUIUtils: "moz-src:///browser/components/search/SearchUIUtils.sys.mjs",
  SEARCH_CTA_ACTIONS: "resource://gre/modules/URLKeywordAnalyzer.sys.mjs",
  analyzeURL: "resource://gre/modules/URLKeywordAnalyzer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

// Reasons this decision layer adds on top of the ones URLKeywordAnalyzer can
// reach on its own. The analyzer cannot see these, because they depend on the
// frame tree or the search service instead of the failed URL.
const DECISION_REASONS = Object.freeze({
  NOT_TOP_LEVEL: "not-top-level",
  SEARCH_UNAVAILABLE: "search-unavailable",
  ENGINE_NOT_GENERAL: "engine-not-general",
  CONNECTIVITY_UNCONFIRMED: "connectivity-unconfirmed",
});

// The search access point this page reports to search telemetry. The source is
// added to BrowserSearchTelemetry's KNOWN_SEARCH_SOURCES in bug 2061005, which
// must land before the CTA is enabled anywhere. Until then recordSearch()
// rejects the unknown source and simply records nothing, which is deliberate:
// borrowing an existing source would file these searches under someone else's
// access point.
const SEARCH_CTA_SAP_SOURCE = "errorpage";

// Observer topic fired whenever a captive-portal check completes, used to await
// the search CTA's authoritative connectivity re-check (bug 2055712).
const NS_CAPTIVE_PORTAL_CONNECTIVITY = "network:captive-portal-connectivity";

class CaptivePortalObserver {
  constructor(actor) {
    this.actor = actor;
    Services.obs.addObserver(this, "captive-portal-login-abort");
    Services.obs.addObserver(this, "captive-portal-login-success");
  }

  stop() {
    Services.obs.removeObserver(this, "captive-portal-login-abort");
    Services.obs.removeObserver(this, "captive-portal-login-success");
  }

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "captive-portal-login-abort":
      case "captive-portal-login-success":
        // Send a message to the content when a captive portal is freed
        // so that error pages can refresh themselves.
        this.actor.sendAsyncMessage("AboutNetErrorCaptivePortalFreed");
        break;
    }
  }
}

export class EscapablePageParent extends JSWindowActorParent {
  /**
   * Re-direct the browser to the previous page or a known-safe page if no
   * previous page is found in history.  This function is used when the user
   * browses to a secure page with certificate issues and is presented with
   * about:certerror.  The "Go Back" button should take the user to the previous
   * or a default start page so that even when their own homepage is on a server
   * that has certificate errors, we can get them somewhere safe.
   */
  leaveErrorPage(browser, allowGoingBack = true) {
    if (!browser.canGoBack || !allowGoingBack) {
      // If the unsafe page is the first or the only one in history, we need to
      // go somewhere:
      let safePage = "about:blank";

      // Ideally we use the homepage...
      if (AppConstants.MOZ_BUILD_APP == "browser") {
        safePage = lazy.HomePage.getForErrorPage(browser.documentGlobal);
      }
      browser.fixupAndLoadURIString(safePage, {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
    } else {
      browser.goBack();
    }
  }
}

export class NetErrorParent extends EscapablePageParent {
  constructor() {
    super();
    this.captivePortalObserver = new CaptivePortalObserver(this);
  }

  didDestroy() {
    if (this.captivePortalObserver) {
      this.captivePortalObserver.stop();
    }
  }

  get browser() {
    return this.browsingContext.top.embedderElement;
  }

  hasChangedCertPrefs() {
    let prefSSLImpact = PREF_SSL_IMPACT_ROOTS.reduce((prefs, root) => {
      return prefs.concat(Services.prefs.getChildList(root));
    }, []);
    for (let prefName of prefSSLImpact) {
      if (Services.prefs.prefHasUserValue(prefName)) {
        return true;
      }
    }

    return false;
  }

  /**
   * This function does a canary request to a reliable, maintained endpoint, in
   * order to help network code detect a system-wide man-in-the-middle.
   */
  primeMitm(browser) {
    // If we already have a mitm canary issuer stored, then don't bother with the
    // extra request. This will be cleared on every update ping.
    if (Services.prefs.getStringPref("security.pki.mitm_canary_issuer", null)) {
      return;
    }

    let url = Services.prefs.getStringPref(
      "security.certerrors.mitm.priming.endpoint"
    );
    let request = new XMLHttpRequest({ mozAnon: true });
    request.open("HEAD", url);
    request.channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
    request.channel.loadFlags |= Ci.nsIRequest.INHIBIT_CACHING;

    request.addEventListener("error", () => {
      // Make sure the user is still on the cert error page.
      if (!browser.documentURI.spec.startsWith("about:certerror")) {
        return;
      }

      let secInfo = request.channel.securityInfo;
      if (secInfo.errorCodeString != "SEC_ERROR_UNKNOWN_ISSUER") {
        return;
      }

      // When we get to this point there's already something deeply wrong, it's very likely
      // that there is indeed a system-wide MitM.
      if (secInfo.serverCert && secInfo.serverCert.issuerName) {
        // Grab the issuer of the certificate used in the exchange and store it so that our
        // network-level MitM detection code has a comparison baseline.
        Services.prefs.setStringPref(
          "security.pki.mitm_canary_issuer",
          secInfo.serverCert.issuerName
        );

        // MitM issues are sometimes caused by software not registering their root certs in the
        // Firefox root store. We might opt for using third party roots from the system root store.
        if (
          Services.prefs.getBoolPref(
            "security.certerrors.mitm.auto_enable_enterprise_roots"
          )
        ) {
          if (
            !Services.prefs.getBoolPref("security.enterprise_roots.enabled")
          ) {
            // Loading enterprise roots happens on a background thread, so wait for import to finish.
            lazy.BrowserUtils.promiseObserved(
              "psm:enterprise-certs-imported"
            ).then(() => {
              if (browser.documentURI.spec.startsWith("about:certerror")) {
                browser.reload();
              }
            });

            Services.prefs.setBoolPref(
              "security.enterprise_roots.enabled",
              true
            );
            // Record that this pref was automatically set.
            Services.prefs.setBoolPref(
              "security.enterprise_roots.auto-enabled",
              true
            );
          }
        } else {
          // Need to reload the page to make sure network code picks up the canary issuer pref.
          browser.reload();
        }
      }
    });

    request.send(null);
  }

  displayOfflineSupportPage(supportPageSlug) {
    const AVAILABLE_PAGES = ["connection-not-secure", "time-errors"];
    if (!AVAILABLE_PAGES.includes(supportPageSlug)) {
      console.log(
        `[Not supported] Offline support is not yet available for ${supportPageSlug} errors.`
      );
      return;
    }

    let offlinePagePath = `chrome://global/content/neterror/supportpages/${supportPageSlug}.html`;
    let triggeringPrincipal =
      Services.scriptSecurityManager.getSystemPrincipal();
    this.browser.loadURI(Services.io.newURI(offlinePagePath), {
      triggeringPrincipal,
    });
  }

  /**
   * The default search engine the CTA would search with, honoring the separate
   * private default in private browsing windows so a CTA click in a private tab
   * never reaches the normal-mode engine. Split out as a seam so tests can
   * substitute an engine. (bug 2055637)
   *
   * @returns {Promise<?nsISearchEngine>} The default engine, or null when none
   *   is configured.
   */
  getDefaultSearchEngine() {
    return this.browsingContext.usePrivateBrowsing
      ? lazy.SearchService.getDefaultPrivate()
      : lazy.SearchService.getDefault();
  }

  /**
   * Whether the default search engine is one the CTA supports: a general-purpose
   * web search engine (Google, Bing, DuckDuckGo, ...). Special-purpose defaults
   * a user may have set (Wikipedia, Amazon, ...) are excluded, since the derived
   * query only makes sense against a general web search. Split out as a seam so
   * tests can force the decision. (bug 2055637)
   *
   * @param {nsISearchEngine} engine The default search engine.
   * @returns {boolean}
   */
  isSupportedSearchEngine(engine) {
    return engine.isGeneralPurposeEngine;
  }

  /**
   * Resolve the data the online dnsNotFound Search CTA needs from the failed
   * URL: the derived search action/query/reason (from the query-derivation
   * module), the registrable domain shown in the page intro, and whether a
   * usable default search engine exists. Content-free and computed locally.
   * Nothing is sent to the network here.
   *
   * A document inside a frame stops here, before any of that work happens.
   *
   * @param {string} failedURL The address that failed to resolve.
   * @returns {Promise<object>} { action, query, reason, domain, hasEngine }
   */
  async getSearchCTAInfo(failedURL) {
    // Most eligible dnsNotFound loads sit in frames the user never sees. Check
    // that first, so we skip the rest of the work and a frame never lands on a
    // reason about the URL. We ask the frame tree, not the page (bug 2063091).
    if (this.browsingContext.parent) {
      this.recordSearchCTADecision(
        lazy.SEARCH_CTA_ACTIONS.NONE,
        DECISION_REASONS.NOT_TOP_LEVEL,
        false
      );
      return {
        action: lazy.SEARCH_CTA_ACTIONS.NONE,
        query: "",
        reason: DECISION_REASONS.NOT_TOP_LEVEL,
        domain: null,
        hasEngine: false,
      };
    }

    const { action, query, reason } = lazy.analyzeURL(failedURL);

    // The intro names the registrable domain, so "xyz.example.com" is shown as
    // "example.com", even when the derived query is keyword-based.
    let domain = null;
    try {
      domain = Services.eTLD.getBaseDomain(Services.io.newURI(failedURL));
    } catch (e) {
      // Unparseable URL, IP literal, single-label host, and so on. Content
      // falls back to the hostname it already has, and the analyzer blocks the
      // CTA for these hosts anyway.
      domain = null;
    }

    // Connectivity freshness guard (bug 2055712): a stale "online" reading can
    // show the CTA while the user is actually offline. When the CTA would
    // otherwise show, require a recent captive-portal check; if it is stale,
    // run one authoritative, time-bounded re-check and suppress the CTA unless
    // we come back affirmatively online.
    if (
      action !== lazy.SEARCH_CTA_ACTIONS.NONE &&
      !(await this.ensureFreshConnectivity())
    ) {
      this.recordSearchCTADecision(
        lazy.SEARCH_CTA_ACTIONS.NONE,
        DECISION_REASONS.CONNECTIVITY_UNCONFIRMED,
        false
      );
      return {
        action: lazy.SEARCH_CTA_ACTIONS.NONE,
        query: "",
        reason: DECISION_REASONS.CONNECTIVITY_UNCONFIRMED,
        domain,
        hasEngine: false,
      };
    }

    let hasEngine = false;
    let engineUnsupported = false;
    if (AppConstants.MOZ_BUILD_APP == "browser") {
      try {
        let engine = await this.getDefaultSearchEngine();
        if (engine && this.isSupportedSearchEngine(engine)) {
          hasEngine = true;
        } else if (engine) {
          // A default exists but is special-purpose (e.g. Wikipedia); the CTA
          // only searches general-purpose engines.
          engineUnsupported = true;
        }
      } catch (e) {}
    }

    this.recordSearchCTADecision(action, reason, hasEngine, engineUnsupported);

    return { action, query, reason, domain, hasEngine };
  }

  /**
   * Record the content-free decision outcome: one action count, one reason
   * count, and a shown count when a CTA is displayed. The query-derivation
   * module owns SEARCH_CTA_ACTIONS and SEARCH_CTA_REASONS. This layer maps a
   * usable host with no engine onto NONE, and owns DECISION_REASONS.
   *
   * @param {string} action The action from the query-derivation module.
   * @param {string} reason The reason from the query-derivation module.
   * @param {boolean} hasEngine Whether a supported default search engine exists.
   * @param {boolean} [engineUnsupported] Whether a default engine exists but is
   *   not a general-purpose web search engine (e.g. Wikipedia).
   */
  recordSearchCTADecision(
    action,
    reason,
    hasEngine,
    engineUnsupported = false
  ) {
    let finalAction = action;
    let finalReason = reason;
    if (action !== lazy.SEARCH_CTA_ACTIONS.NONE && !hasEngine) {
      finalAction = lazy.SEARCH_CTA_ACTIONS.NONE;
      finalReason = engineUnsupported
        ? DECISION_REASONS.ENGINE_NOT_GENERAL
        : DECISION_REASONS.SEARCH_UNAVAILABLE;
    }
    Glean.securityUiNeterror.searchCtaAction[finalAction].add(1);
    // Reasons are hyphenated, Glean labels are not.
    Glean.securityUiNeterror.searchCtaReason[
      finalReason.replace(/-/g, "_")
    ].add(1);
    if (finalAction !== lazy.SEARCH_CTA_ACTIONS.NONE) {
      Glean.securityUiNeterror.searchCtaShown.add(1);
    }
  }

  /**
   * The captive-portal service, split out as a seam so tests can substitute a
   * fake with a controllable state and lastChecked.
   *
   * @returns {nsICaptivePortalService}
   */
  getCaptivePortalService() {
    return Cc["@mozilla.org/network/captive-portal-service;1"].getService(
      Ci.nsICaptivePortalService
    );
  }

  /**
   * @param {nsICaptivePortalService} cps The captive-portal service.
   * @returns {boolean} Whether we are online and not behind a captive portal.
   */
  isAffirmativelyOnline(cps) {
    return (
      cps.state === Ci.nsICaptivePortalService.NOT_CAPTIVE &&
      Services.io.connectivity
    );
  }

  /**
   * Whether the search CTA may be shown from a connectivity standpoint. Trusts
   * a fresh captive-portal reading immediately (the content-side gate already
   * excluded the offline and captive states); on a stale reading, runs one
   * authoritative, time-bounded re-check and resolves to whether we came back
   * affirmatively online. (bug 2055712)
   *
   * @returns {Promise<boolean>} True when the CTA may be shown, meaning we are
   *   confident we are online. False only when a stale reading was re-checked
   *   and the re-check did not come back affirmatively online.
   */
  async ensureFreshConnectivity() {
    // Captive-portal detection off: no reading to be stale about, so trust the
    // content-side connectivity gate that already ran.
    if (
      !Services.prefs.getBoolPref(
        "network.captive-portal-service.enabled",
        false
      )
    ) {
      return true;
    }
    let cps;
    try {
      cps = this.getCaptivePortalService();
    } catch (e) {
      // No captive-portal service available: don't block the CTA on it.
      return true;
    }
    const freshnessMs = Services.prefs.getIntPref(
      "browser.netError.searchCTA.connectivityFreshnessMs",
      60000
    );
    // A recent reading means the content-side gate (which already excluded the
    // offline and captive-portal states) was based on fresh data; trust it.
    if (cps.lastChecked <= freshnessMs) {
      return true;
    }
    // Stale reading: re-verify authoritatively before showing the CTA.
    return this.recheckConnectivity(cps);
  }

  /**
   * Trigger an authoritative captive-portal re-check and resolve to whether we
   * are affirmatively online once it completes, or false if it does not
   * complete within the bounded timeout so the CTA can never hang.
   *
   * @param {nsICaptivePortalService} cps The captive-portal service.
   * @returns {Promise<boolean>} True when the completed re-check says we are
   *   online and not behind a captive portal. False if it says otherwise or
   *   does not complete in time.
   */
  recheckConnectivity(cps) {
    const timeoutMs = Services.prefs.getIntPref(
      "browser.netError.searchCTA.connectivityRecheckTimeoutMs",
      3000
    );
    return new Promise(resolve => {
      let settled = false;
      let timer;
      const observer = {
        observe: () => finish(this.isAffirmativelyOnline(cps)),
      };
      const finish = result => {
        if (settled) {
          return;
        }
        settled = true;
        Services.obs.removeObserver(observer, NS_CAPTIVE_PORTAL_CONNECTIVITY);
        lazy.clearTimeout(timer);
        resolve(result);
      };
      Services.obs.addObserver(observer, NS_CAPTIVE_PORTAL_CONNECTIVITY);
      timer = lazy.setTimeout(() => finish(false), timeoutMs);
      try {
        cps.recheckCaptivePortal();
      } catch (e) {
        finish(false);
      }
    });
  }

  /**
   * Record a CTA click that was aborted because connectivity dropped between
   * showing the CTA and the click (bug 2055712). This is a click-time outcome,
   * not a page-load decision, so it gets its own counter rather than a
   * search_cta_reason label: reasons stay one-per-eligible-page-load and keep
   * summing to search_cta_action. The click still counts as a click.
   */
  recordSearchCTAAbort() {
    Glean.securityUiNeterror.searchCtaClicked.add(1);
    Glean.securityUiNeterror.searchCtaClickAborted.connectivity_lost.add(1);
  }

  /**
   * Perform a default-engine search for the given query in a new tab. Called
   * only on an explicit CTA click, so this is the first time any part of the
   * failed address leaves the device.
   *
   * @param {string} query The search query (registrable domain in bug 2055637).
   */
  async performSearchCTASearch(query) {
    if (AppConstants.MOZ_BUILD_APP != "browser" || !query) {
      return;
    }
    Glean.securityUiNeterror.searchCtaClicked.add(1);
    let engine;
    try {
      engine = await this.getDefaultSearchEngine();
    } catch (e) {
      return;
    }
    let win = this.browser?.documentGlobal;
    if (!engine || !win) {
      return;
    }
    // SearchUIUtils.loadSearch is the supported entry point for a UI-initiated
    // search: it builds the submission (partner code included, as for any other
    // access point) and records the SAP telemetry that a hand-rolled
    // openTrustedLinkIn cannot.
    //
    // Always foreground: the search is the recovery path the user just asked
    // for, so honouring browser.tabs.loadInBackground (true by default) would
    // leave them staring at the error page they were trying to escape.
    try {
      await lazy.SearchUIUtils.loadSearch({
        window: win,
        searchText: query,
        where: "tab",
        inBackground: false,
        engine,
        sapSource: SEARCH_CTA_SAP_SOURCE,
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      });
    } catch (e) {
      // loadSearch throws if the engine has no submission URL, where the
      // previous hand-rolled path returned silently.
      console.error("Search CTA: could not run the search", e);
    }
  }

  receiveMessage(message) {
    switch (message.name) {
      case "Browser:EnableOnlineMode":
        // Reset network state and refresh the page.
        Services.io.offline = false;
        this.browser.reload();
        break;
      case "Browser:OpenCaptivePortalPage":
        this.browser.documentGlobal.CaptivePortalWatcher.ensureCaptivePortalTab();
        break;
      case "Browser:PrimeMitm":
        this.primeMitm(this.browser);
        break;
      case "Browser:ResetEnterpriseRootsPref":
        Services.prefs.clearUserPref("security.enterprise_roots.enabled");
        Services.prefs.clearUserPref("security.enterprise_roots.auto-enabled");
        break;
      case "Browser:ResetSSLPreferences": {
        let prefSSLImpact = PREF_SSL_IMPACT_ROOTS.reduce((prefs, root) => {
          return prefs.concat(Services.prefs.getChildList(root));
        }, []);
        for (let prefName of prefSSLImpact) {
          Services.prefs.clearUserPref(prefName);
        }
        this.browser.reload();
        break;
      }
      case "Browser:SSLErrorGoBack":
        this.leaveErrorPage(this.browser);
        break;
      case "GetChangedCertPrefs": {
        let hasChangedCertPrefs = this.hasChangedCertPrefs();
        this.sendAsyncMessage("HasChangedCertPrefs", {
          hasChangedCertPrefs,
        });
        break;
      }
      case "DisplayOfflineSupportPage":
        this.displayOfflineSupportPage(message.data.supportPageSlug);
        break;
      case "Browser:CertExceptionError":
        switch (message.data.elementId) {
          case "viewCertificate": {
            let certs = message.data.handshakeCertificates.map(certBase64 =>
              encodeURIComponent(certBase64)
            );
            let certsStringURL = certs.map(elem => `cert=${elem}`);
            certsStringURL = certsStringURL.join("&");
            let url = `about:certificate?${certsStringURL}`;

            let window = this.browser.documentGlobal;
            if (AppConstants.MOZ_BUILD_APP === "browser") {
              window.switchToTabHavingURI(url, true, {});
            } else {
              window.open(url, "_blank");
            }
            break;
          }
        }
        break;
      case "Browser:AddTRRExcludedDomain": {
        let uri = this.browsingContext.currentURI;
        if (uri instanceof Ci.nsINestedURI) {
          uri = uri.QueryInterface(Ci.nsINestedURI).innermostURI;
        }
        let excludedDomains = Services.prefs.getStringPref(
          "network.trr.excluded-domains"
        );
        excludedDomains += `, ${uri.asciiHost}`;
        Services.prefs.setStringPref(
          "network.trr.excluded-domains",
          excludedDomains
        );
        break;
      }
      case "OpenTRRPreferences": {
        let browser = this.browsingContext.top.embedderElement;
        if (!browser) {
          break;
        }

        let win = browser.documentGlobal;
        win.openPreferences("privacy-doh");
        break;
      }
      case "SearchCTA:GetInfo":
        return this.getSearchCTAInfo(message.data.url);
      case "SearchCTA:Search":
        this.performSearchCTASearch(message.data.query);
        break;
      case "SearchCTA:SearchAborted":
        this.recordSearchCTAAbort();
        break;
    }
    return undefined;
  }
}
