/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from browser-siteProtections.js */

ChromeUtils.defineESModuleGetters(this, {
  BreachAlertStorage: "resource://gre/modules/BreachAlertStore.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ContentBlockingAllowList:
    "resource://gre/modules/ContentBlockingAllowList.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  FX_MONITOR_OAUTH_CLIENT_ID: "resource://gre/modules/FxAccountsCommon.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  QWACs: "resource://gre/modules/psm/QWACs.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SiteDataManager: "resource:///modules/SiteDataManager.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "insecureConnectionTextEnabled",
  "security.insecure_connection_text.enabled"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "insecureConnectionTextPBModeEnabled",
  "security.insecure_connection_text.pbmode.enabled"
);
// [pref-trie-audit] "dom.security.https_only_mode" is an ambiguous prefix of
// "dom.security.https_only_mode_break_upgrade_downgrade_endless_loop",
// "dom.security.https_only_mode_error_page_user_suggestions",
// "dom.security.https_only_mode_ever_enabled", "dom.security.https_only_mode_ever_enabled_pbm",
// "dom.security.https_only_mode_pbm", "dom.security.https_only_mode_send_http_background_request";
// triggers only for the exact pref (all siblings have their own registrations).
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "httpsOnlyModeEnabled",
  "dom.security.https_only_mode"
);
// [pref-trie-audit] "dom.security.https_first" is an ambiguous prefix of
// "dom.security.https_first_add_exception_on_failure", "dom.security.https_first_exception_lifetime",
// "dom.security.https_first_for_custom_ports", "dom.security.https_first_for_local_addresses",
// "dom.security.https_first_for_unknown_suffixes", "dom.security.https_first_schemeless";
// triggers only for the exact pref (all siblings have their own registrations).
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "httpsFirstModeEnabled",
  "dom.security.https_first"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "schemelessHttpsFirstModeEnabled",
  "dom.security.https_first_schemeless"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "httpsFirstModeEnabledPBM",
  "dom.security.https_first_pbm"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "httpsOnlyModeEnabledPBM",
  "dom.security.https_only_mode_pbm"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "popupClickjackDelay",
  "security.notification_enable_delay",
  500
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "smartblockEmbedsEnabledPref",
  "extensions.webcompat.smartblockEmbeds.enabled",
  false
);

const ETP_ENABLED_ASSETS = {
  label: "trustpanel-etp-label-enabled",
  description: "trustpanel-etp-description-enabled",
  header: "trustpanel-header-enabled",
  innerDescription: "trustpanel-description-enabled2",
};

const ETP_DISABLED_ASSETS = {
  label: "trustpanel-etp-label-disabled",
  description: "trustpanel-etp-description-disabled",
  header: "trustpanel-header-disabled",
  innerDescription: "trustpanel-description-disabled",
};

const SMARTBLOCK_EMBED_INFO = [
  {
    matchPatterns: ["https://itisatracker.org/*"],
    shimId: "EmbedTestShim",
    displayName: "Test",
  },
  {
    matchPatterns: [
      "https://www.instagram.com/*",
      "https://platform.instagram.com/*",
    ],
    shimId: "InstagramEmbed",
    displayName: "Instagram",
  },
  {
    matchPatterns: ["https://www.tiktok.com/*"],
    shimId: "TikTokEmbed",
    displayName: "TikTok",
  },
  {
    matchPatterns: ["https://platform.twitter.com/*"],
    shimId: "TwitterEmbed",
    displayName: "X",
  },
  {
    matchPatterns: ["https://*.disqus.com/*"],
    shimId: "DisqusEmbed",
    displayName: "Disqus",
  },
];

class TrustPanel {
  #state = null;
  #secInfo = null;
  /**
   * We read the OAuth clients attached to the user's FxA account to know if
   * they're a Mozilla Monitor user, and cache those in memory. When the user
   * logs out, we flip this boolean to indicate that list needs refreshing.
   */
  #clearFxaOauthClientCache = false;
  #breachAlertStoragePromise = null;
  #trackerCount = null;
  // In-flight promise from #computeTrackerCount, so concurrent callers (the
  // toolbar update and the blocker subview) for the same content-blocking event
  // share a single query of each blocker instead of both querying.
  #trackerCountPromise = null;
  #isFirstVisit = false;
  // False until the blocker check completes; while false a secure page shows the
  // neutral "scanning" shield rather than the check-mark.
  #blockersChecked = false;
  // Guards against a stale run overwriting a fresher count.
  #toolbarTrackerCountUpdateId = 0;
  // True while navigating within the same site, so the icon stays static.
  #sameSiteNavigation = false;
  /**
   * We don't want to add the `has-blocked-trackers` class before we add the `first-visit` class,
   * because otherwise the animation choreography gets out of sync (specifically, the
   * `show-shortform-tracker-count` animation will start, causing the short form of the
   * "blocked trackers" pill to show up too soon). Thus, we'll have to await this Promise
   * before we update the tracker count.
   */
  #firstVisitPromise = Promise.resolve();

  /**
   * If the document is using a qualified website authentication certificate
   * (QWAC), this may eventually be an nsIX509Cert corresponding to it.
   */
  #qwac = null;

  #breachedStatus = null;

  /**
   * Promise that will resolve when determining if the document is using a QWAC
   * has resolved.
   */
  #qwacStatusPromise = null;

  #uri = null;
  #uriHasHost = null;
  #pageExtensionPolicy = null;

  #lastEvent = null;

  // The browser element that was active during the last updateIdentity() call.
  // Used to distinguish a real same-site navigation within one tab from a tab
  // switch to another tab on the same domain, so the tracker count from the
  // previous tab is not incorrectly inherited.
  #lastBrowser = null;

  // Monotonic tag for #updateBlockerView runs so a stale (slower) run can't
  // overwrite a fresher one's result. See the method for details.
  #blockerViewUpdateId = 0;

  #popupToggleDelayTimer = null;
  #openingReason = null;

  #blockers = {
    SocialTracking,
    ThirdPartyCookies,
    TrackingProtection,
    Fingerprinting,
    Cryptomining,
  };

  init() {
    for (let blocker of Object.values(this.#blockers)) {
      if (blocker.init) {
        blocker.init();
      }
    }

    // Add an observer to listen to requests to open the protections panel
    Services.obs.addObserver(this, "smartblock:open-protections-panel");

    // Add an observer to listen for FxAccounts logout to clear OAuth cache
    Services.obs.addObserver(this, "fxaccounts:onlogout");

    customElements.whenDefined("breach-alert-panel").then(() => {
      const breachAlertElement = document.getElementById(
        "trustpanel-breach-alert-section"
      );
      if (breachAlertElement) {
        breachAlertElement.addEventListener(
          "dismissBreachAlert",
          this.dismissBreachAlert.bind(this)
        );
      }
    });
  }

  uninit() {
    for (let blocker of Object.values(this.#blockers)) {
      if (blocker.uninit) {
        blocker.uninit();
      }
    }

    Services.obs.removeObserver(this, "smartblock:open-protections-panel");
    Services.obs.removeObserver(this, "fxaccounts:onlogout");
  }

  get #popup() {
    return document.getElementById("trustpanel-popup");
  }

  get #enabled() {
    return UrlbarPrefs.get("trustPanel.featureGate");
  }

  handleProtectionsButtonEvent(event) {
    event.stopPropagation();
    if (
      (event.type == "click" && event.button != 0) ||
      (event.type == "keypress" &&
        event.charCode != KeyEvent.DOM_VK_SPACE &&
        event.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      return; // Left click, space or enter only
    }

    this.showPopup({ event, reason: "shieldButtonClicked" });
  }

  async onContentBlockingEvent(
    event,
    _webProgress,
    _isSimulated,
    _previousState
  ) {
    // Only accept contentblocking events for uris that we initialised `updateIdentity`
    // with, this can go wrong if trustpanel is enabled mid page load.
    if (!this.#enabled || !this.#uri) {
      return;
    }

    // First update all our internal state based on the allowlist and the
    // different blockers:
    this.anyDetected = false;
    this.#lastEvent = event;
    // Recompute the count, but keep the last displayed value so it doesn't
    // briefly drop to 0 during a same-site navigation.
    this.#trackerCountPromise = null;

    // Check whether the user has added an exception for this site.
    this.hasException =
      ContentBlockingAllowList.canHandle(window.gBrowser.selectedBrowser) &&
      ContentBlockingAllowList.includes(window.gBrowser.selectedBrowser);

    // Update blocker state and find if they detected or blocked anything.
    for (let blocker of Object.values(this.#blockers)) {
      // Store data on whether the blocker is activated for reporting it
      // using the "report breakage" dialog. Under normal circumstances this
      // dialog should only be able to open in the currently selected tab
      // and onSecurityChange runs on tab switch, so we can avoid associating
      // the data with the document directly.
      blocker.activated = blocker.isBlocking(event);
      this.anyDetected = this.anyDetected || blocker.isDetected(event);
    }

    void this.#updateToolbarTrackerCount();
    if (this.#popup) {
      await this.#updatePopup();
    }
  }

  #initializePopup() {
    if (!this.#popup) {
      let wrapper = document.getElementById("template-trustpanel-popup");
      wrapper.replaceWith(wrapper.content);

      document
        .getElementById("trustpanel-popup-connection")
        .addEventListener("click", event =>
          this.#openSecurityInformationSubview(event)
        );
      document
        .getElementById("trustpanel-blocker-see-all")
        .addEventListener("click", event => this.#openBlockerSubview(event));
      document
        .getElementById("trustpanel-privacy-link")
        .addEventListener("click", () => {
          this.#hidePopup();
          window.openTrustedLinkIn("about:preferences#privacy", "tab");
        });
      document
        .getElementById("trustpanel-clear-cookies-button")
        .addEventListener("command", event =>
          this.#showClearCookiesSubview(event)
        );
      document
        .getElementById("trustpanel-siteinformation-morelink")
        .addEventListener("click", () => this.#showSecurityPopup());
      document
        .getElementById("trustpanel-clear-cookie-cancel")
        .addEventListener("click", () => this.#hidePopup());
      document
        .getElementById("trustpanel-clear-cookie-clear")
        .addEventListener("click", () => this.#clearSiteData());
      document
        .getElementById("trustpanel-toggle")
        .addEventListener("click", () => this.#toggleTrackingProtection());
      document
        .getElementById("identity-popup-remove-cert-exception")
        .addEventListener("click", () => this.#removeCertException());
      document
        .getElementById("trustpanel-popup-security-httpsonlymode-menulist")
        .addEventListener("command", () => this.#changeHttpsOnlyPermission());

      this.#popup.addEventListener("popupshown", this);
      this.#popup.addEventListener("popuphidden", this);
    }
  }

  async showPopup(opts = {}) {
    this.#initializePopup();

    // Kick off background determination of QWAC status.
    if (this.#isSecureContext && !this.#qwacStatusPromise) {
      let qwacStatusPromise = QWACs.determineQWACStatus(
        this.#secInfo,
        this.#uri,
        gBrowser.selectedBrowser.browsingContext
      ).then(result => {
        // Check that when this promise resolves, we're still on the same
        // document as when it was created.
        if (qwacStatusPromise == this.#qwacStatusPromise && result) {
          this.#qwac = result;
          this.#updateSecurityInformationSubview();
        }
      });
      this.#qwacStatusPromise = qwacStatusPromise;
    }

    await this.#updatePopup();

    this.#openingReason = opts.reason;

    PanelMultiView.openPopup(this.#popup, this.#anchor(), {
      position: "bottomleft topleft",
      triggerEvent: opts.event,
    });

    const applicableBreaches = await this.#getApplicableBreaches(this.#host);
    const [hasMonitorAccountOrStoredPasswords, blockedTrackersCount] =
      await Promise.all([
        this.#hasMonitorAccountOrStoredPasswords(),
        this.#computeTrackerCount(),
      ]);
    Glean.trustpanel.opened.record({
      breach_status: getBreachedStatus({
        breaches: applicableBreaches,
        hasMonitorAccountOrStoredPasswords,
      }),
      trackers_blocked: blockedTrackersCount > 0,
    });
  }

  async #hidePopup() {
    let hidden = new Promise(c => {
      this.#popup.addEventListener("popuphidden", c, { once: true });
    });
    PanelMultiView.hidePopup(this.#popup);
    await hidden;
  }

  /**
   * Whether the current page is http(s) web content (as opposed to an internal
   * about:/chrome page or an extension page). Only web content is scanned for
   * trackers, so the scanning shield is scoped to it.
   *
   * @returns {boolean}
   */
  #isWebPage() {
    return (
      !!this.#uri && (this.#uri.schemeIs("http") || this.#uri.schemeIs("https"))
    );
  }

  /**
   * Whether two URIs are on the same site — i.e. share a base domain (eTLD+1),
   * so subdomains (e.g. www.example.com and example.com) count as the same
   * site. Guards against URIs with no base domain (IP hosts, about:, etc.).
   *
   * @param {nsIURI} a
   * @param {nsIURI} b
   * @returns {boolean}
   */
  #isSameSite(a, b) {
    try {
      return (
        !!a &&
        !!b &&
        Services.eTLD.getBaseDomain(a) === Services.eTLD.getBaseDomain(b)
      );
    } catch (ex) {
      return false;
    }
  }

  /**
   * Called at navigation start (before the security state is known) so the icon
   * drops to the "scanning" shield immediately, rather than showing the previous
   * page's secure check until updateIdentity runs on the security change.
   */
  resetIconForNavigation(targetURI) {
    if (!this.#enabled) {
      return;
    }
    let sameSite = this.#isSameSite(targetURI, this.#uri);
    // Set this early (before the new page's content-blocking events) to avoid a
    // race with updateIdentity. STATE_START can fire without a URI.
    if (targetURI) {
      this.#sameSiteNavigation = sameSite;
    }
    // A same-site navigation keeps the resolved icon static; only cross-site
    // resets and scans.
    if (sameSite) {
      return;
    }
    this.#blockersChecked = false;
    if (
      !UrlbarPrefs.get("trackerCountFeatureGate") ||
      !UrlbarPrefs.get("trackerCount.enabled")
    ) {
      return;
    }
    // Set the shield directly: #uri isn't set this early, so #updateUrlbarIcon
    // can't run yet. It resolves once a tracker is blocked or the load completes.
    let icon = document.getElementById("trust-icon-container");
    for (let cls of [...icon.classList]) {
      icon.classList.remove(cls);
    }
    icon.classList.add("scanning");
  }

  /**
   * Called when the top-level document finishes loading. If the scanning shield
   * is still up (no tracker was blocked during the load), resolve it now to the
   * final secure/insecure state. A tracker blocked later still increments the
   * count via #updateToolbarTrackerCount.
   */
  async onNavigationComplete() {
    if (!this.#enabled || !this.#uri || this.#blockersChecked) {
      return;
    }
    if (
      !UrlbarPrefs.get("trackerCountFeatureGate") ||
      !UrlbarPrefs.get("trackerCount.enabled")
    ) {
      return;
    }
    // Resolve the count first: if a tracker was blocked it already set
    // #blockersChecked, so we only fall through to the plain check when none was.
    const uri = this.#uri;
    await this.#updateToolbarTrackerCount();
    if (this.#uri !== uri || this.#blockersChecked) {
      return;
    }
    this.#blockersChecked = true;
    this.#updateUrlbarIcon();
  }

  updateIdentity(state, uri) {
    if (!this.#enabled) {
      return;
    }
    try {
      // Account for file: urls and catch when "" is the value
      this.#uriHasHost = !!uri.host;
    } catch (ex) {
      this.#uriHasHost = false;
    }

    const browser = gBrowser.selectedBrowser;
    this.#sameSiteNavigation =
      // If the user visits the same site in different tabs, then switches between those tabs,
      // that results in two `updateIdentity` calls with the same site, but that should not
      // be considered a same-site navigation:
      browser === this.#lastBrowser && this.#isSameSite(uri, this.#uri);
    this.#lastBrowser = browser;

    this.#state = state;
    this.#uri = uri;

    this.#secInfo = gBrowser.securityUI.secInfo;
    // Clear any previously-determined QWAC information.
    this.#qwac = null;
    this.#qwacStatusPromise = null;
    this.#pageExtensionPolicy = WebExtensionPolicy.getByURI(uri);
    this.#breachedStatus = null;
    if (this.#sameSiteNavigation) {
      // Keep the count so the synchronous #updateUrlbarIcon() below doesn't
      // strip .has-blocked-trackers and blink the pill off mid-navigation;
      // clear first-visit so it renders statically. #sameSiteNavigation is
      // now tab-aware, so this no longer inherits the count across tab switches.
      this.#isFirstVisit = false;
    } else {
      this.#trackerCount = null;
      this.#trackerCountPromise = null;
      this.#isFirstVisit = false;
    }
    // #blockersChecked is reset in resetIconForNavigation, not here, so tab
    // switches and re-fired security changes don't re-enter scanning.
    this.#updateUrlbarIcon();

    // We want to make sure the URL bar icon updates immediately to a neutral state
    // after navigation, which is why `this.#updateUrlbarIcon` is synchronous.
    // However, to determine whether we need to show an animation to highlight a breach alert,
    // we need to make a couple of async calls. Thus, we accept that we might be showing the
    // "wrong" icon initially, and then redraw the icon again when the async calls complete
    // and we need to display the breach animation.
    // (This function will update the icon by itself when it resolves, so we don't have to await it here.)
    void this.#checkForBreaches(uri);

    // Only re-check first-visit on a cross-site navigation.
    if (!this.#sameSiteNavigation) {
      this.#firstVisitPromise = this.#markFirstVisit();
    }
    void this.#updateToolbarTrackerCount();
  }

  /** Asynchronous check for the current page's breached status, updating the address bar icon if the page was breached */
  async #checkForBreaches(uri) {
    const capturedUri = uri;
    const [applicableBreaches, hasMonitorAccountOrStoredPasswords] =
      await Promise.all([
        this.#getApplicableBreaches(this.#host),
        this.#hasMonitorAccountOrStoredPasswords(),
      ]);

    // Ensure we're still looking at the page we checked the breach status for:
    if (this.#uri !== capturedUri) {
      return;
    }
    const breachedStatus = getBreachedStatus({
      breaches: applicableBreaches,
      hasMonitorAccountOrStoredPasswords,
    });
    this.#breachedStatus = breachedStatus;
    if (breachedStatus !== "disabled" && breachedStatus !== "not-breached") {
      this.#updateUrlbarIcon();
    }
  }

  /**
   * The trust icon may be hidden, in that case the identity box
   * should be shown so use that as anchor.
   *
   * @returns {DOMElement}
   */
  #anchor() {
    let anchors = [
      document.getElementById("trust-icon-container"),
      document.getElementById("identity-icon-box"),
    ];
    return anchors.find(element => element.checkVisibility());
  }

  #updateUrlbarIcon() {
    let icon = document.getElementById("trust-icon-container");
    let targetClasses = new Set();
    targetClasses.add(this.#isSecurePage() ? "secure" : "insecure");

    if (this.#isSecurePage() && this.#breachedStatus === "breached") {
      targetClasses.add("breached");
    }
    if (!this.#trackingProtectionEnabled) {
      targetClasses.add("inactive");
    }
    if (this.#isAboutNetErrorPage || this.#isCertUserOverridden) {
      targetClasses.add("warning");
    }
    if (this.#isFirstVisit) {
      targetClasses.add("first-visit");
    }
    // Added after "first-visit" so the tracker-count pill animation stays in sync.
    if (this.#trackerCount > 0) {
      targetClasses.add("has-blocked-trackers");
    }

    // Before the blocker check resolves, show the scanning shield instead of the
    // check-mark. Only affects secure http(s) pages; definitive states still show.
    if (
      !this.#blockersChecked &&
      this.#isWebPage() &&
      targetClasses.has("secure") &&
      !targetClasses.has("breached") &&
      !targetClasses.has("warning") &&
      UrlbarPrefs.get("trackerCountFeatureGate") &&
      UrlbarPrefs.get("trackerCount.enabled")
    ) {
      targetClasses = new Set(["scanning"]);
    }

    // Suppresses the reveal animation so the pill stays static across a
    // same-site navigation.
    if (this.#sameSiteNavigation && !targetClasses.has("scanning")) {
      targetClasses.add("same-site-nav");
    }

    // A breach supersedes scanning: resolve so the shield doesn't mask it.
    if (targetClasses.has("breached")) {
      this.#blockersChecked = true;
    }

    const browser = gBrowser.selectedBrowser;
    // Handle the breach animation guard (restart only on fresh URI).
    if (targetClasses.has("breached")) {
      if (browser.lastAnimatedBreachURI !== this.#uri?.spec) {
        // This is a fresh visit: trigger the animation.
        targetClasses.add("breach-animating");
        browser.lastAnimatedBreachURI = this.#uri?.spec;

        Glean.trustpanel.breachAlertShieldAnimated.record({
          breach_status: this.#breachedStatus,
        });
        // Logic will re-add breached, and since it's the first time for
        // breach-animating, the CSS animation will play.
      }
    }

    // Remove any class currently on the icon that's no longer wanted, then apply
    // the target set — keeping targetClasses the single source of truth with no
    // separate member to maintain. (chickletShown is re-toggled below.)
    let appliedIconClasses = [...icon.classList];

    if (
      targetClasses.has("has-blocked-trackers") &&
      browser.lastTrackerCountShownURI !== this.#uri?.spec
    ) {
      browser.lastTrackerCountShownURI = this.#uri?.spec;
      Glean.trustpanel.trackerCountShown.record({
        first_visit: targetClasses.has("first-visit"),
      });
    }

    for (let cls of appliedIconClasses) {
      if (!targetClasses.has(cls)) {
        icon.classList.remove(cls);
      }
    }
    icon.classList.add(...targetClasses);

    icon.setAttribute("tooltiptext", this.#tooltipText());
    icon.classList.toggle("chickletShown", this.#isInternalSecurePage);
  }

  async #updatePopup() {
    this.#popup.setAttribute("connection", this.#connectionState());
    this.#popup.toggleAttribute("customroot", this.#hasCustomRoot());
    this.#popup.toggleAttribute("tlskeylogging", this.#tlsKeyLoggingEnabled());
    this.#popup.setAttribute(
      "tracking-protection",
      this.#trackingProtectionStatus()
    );

    await this.#updateMainView();
  }

  async #updateMainView() {
    let assets = this.#trackingProtectionEnabled
      ? ETP_ENABLED_ASSETS
      : ETP_DISABLED_ASSETS;

    const graphicSection = document.getElementById(
      "trustpanel-graphic-section"
    );
    const breachAlertGraphicSection = document.getElementById(
      "trustpanel-breach-alert-section"
    );

    const applicableBreaches = await this.#getApplicableBreaches(this.#host);
    const hasMonitorAccountOrStoredPasswords =
      await this.#hasMonitorAccountOrStoredPasswords();
    const breachedStatus = getBreachedStatus({
      breaches: applicableBreaches,
      hasMonitorAccountOrStoredPasswords,
    });
    if (breachedStatus !== "disabled" && breachedStatus !== "not-breached") {
      graphicSection.hidden = true;
      breachAlertGraphicSection.hidden = false;
      breachAlertGraphicSection.breachStatus = breachedStatus;
      breachAlertGraphicSection.breachNames = applicableBreaches.map(
        breach => breach.Name
      );
    } else {
      graphicSection.hidden = false;
      breachAlertGraphicSection.hidden = true;
    }

    if (this.#uri) {
      let favicon = await PlacesUtils.favicons.getFaviconForPage(this.#uri);
      document.getElementById("trustpanel-popup-icon").src =
        favicon?.uri.spec ?? "";
    }

    let toggle = document.getElementById("trustpanel-toggle");
    toggle.toggleAttribute("pressed", this.#trackingProtectionEnabled);
    document.l10n.setAttributes(
      toggle,
      this.#trackingProtectionEnabled
        ? "trustpanel-etp-toggle-on"
        : "trustpanel-etp-toggle-off",
      { host: this.#host }
    );

    let hostElement = document.getElementById("trustpanel-popup-host");
    hostElement.setAttribute("value", this.#host);
    hostElement.setAttribute("tooltiptext", this.#host);

    document.l10n.setAttributes(
      document.getElementById("trustpanel-etp-label"),
      assets.label
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-etp-description"),
      assets.description
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-header"),
      assets.header
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-description"),
      assets.innerDescription
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-connection-label"),
      this.#connectionLabel()
    );

    this.#updateAttribute(
      document.getElementById("trustpanel-toggle-section"),
      "disabled",
      !ContentBlockingAllowList.canHandle(window.gBrowser.selectedBrowser)
    );

    // TODO(Bug 1751045): Show the clear site data footer in private browsing
    // when clearing data in private browsing is supported.
    const isPrivate = PrivateBrowsingUtils.isWindowPrivate(window);
    this.#updateAttribute(
      document.getElementById("trustpanel-clear-cookies-footer"),
      "hidden",
      isPrivate
    );
    if (!isPrivate) {
      try {
        let baseDomain = SiteDataManager.getBaseDomainFromHost(this.#uri.host);
        SiteDataManager.hasSiteData(baseDomain).then(hasSiteData => {
          this.#updateAttribute(
            document.getElementById("trustpanel-clear-cookies-button"),
            "disabled",
            !hasSiteData
          );
        });
      } catch (e) {}
    }

    this.#updateAttribute(
      document.getElementById("trustpanel-toggle"),
      "disabled",
      !ContentBlockingAllowList.canHandle(window.gBrowser.selectedBrowser)
    );

    await this.#updateBlockerView();
  }

  #computeTrackerCount() {
    if (this.#trackerCountPromise) {
      return this.#trackerCountPromise;
    }
    this.#trackerCountPromise = (async () => {
      let count = this.#fetchSmartBlocked().length;
      for (let blocker of Object.values(this.#blockers)) {
        if (blocker.isBlocking(this.#lastEvent)) {
          count += await blocker.getBlockerCount();
        }
      }
      return count;
    })();
    return this.#trackerCountPromise;
  }

  async #markFirstVisit() {
    if (!this.#uriHasHost) {
      this.#isFirstVisit = false;
      this.#updateUrlbarIcon();
      return;
    }
    const uri = this.#uri;
    const revHost = uri.host.split("").reverse().join("") + ".";
    const conn = await PlacesUtils.promiseDBConnection();
    const rows = await conn.executeCached(
      // Check if the current host was visited before,
      // but not in the last 20 seconds.
      // (So that we don't show the long-form tracker count
      // after the user already got the chance to see it on this site.)
      `SELECT 1 FROM moz_historyvisits v
         JOIN moz_places h ON h.id = v.place_id
         WHERE h.rev_host = :revHost
         AND v.visit_date < ((strftime('%s', 'now') - 20) * 1000000)
         LIMIT 1`,
      {
        revHost,
      }
    );
    if (!this.#uriHasHost || this.#uri.host !== uri.host) {
      // If the URL has changed while executing the query above, abort.
      return;
    }
    // Also treat as a first visit if the tracker count has never been shown,
    // so the user gets the long-form UI even on a return visit.
    const browser = gBrowser.selectedBrowser;
    this.#isFirstVisit =
      (rows.length === 0 || !UrlbarPrefs.get("trackerCountShown")) &&
      browser.lastFirstVisitURI !== uri.spec;
    if (this.#isFirstVisit) {
      browser.lastFirstVisitURI = uri.spec;
    }
    this.#updateUrlbarIcon();
  }

  async #updateToolbarTrackerCount() {
    if (
      !UrlbarPrefs.get("trackerCountFeatureGate") ||
      !UrlbarPrefs.get("trackerCount.enabled")
    ) {
      return;
    }
    const uri = this.#uri;
    // Tag this run so that if a newer one starts while we're awaiting below,
    // this now-stale run bails instead of clobbering the fresher count.
    const updateId = ++this.#toolbarTrackerCountUpdateId;
    let [count] = await Promise.all([
      this.#computeTrackerCount(),
      this.#firstVisitPromise,
    ]);
    if (this.#uri !== uri || this.#toolbarTrackerCountUpdateId !== updateId) {
      return;
    }

    // On a same-site navigation, keep the last positive count until the new page
    // reports its own, so the pill doesn't flash "0" mid-load.
    if (this.#sameSiteNavigation && count === 0 && this.#trackerCount > 0) {
      count = this.#trackerCount;
    }
    this.#trackerCount = count;
    // A blocked tracker resolves the scanning shield straight into the reveal.
    if (count > 0) {
      this.#blockersChecked = true;
    }
    const iconContainer = document.getElementById("trust-icon-container");
    if (count > 0 && !UrlbarPrefs.get("trackerCountShown")) {
      // The very first time we show the count of trackers, we set this pref so
      // that we can trigger a feature callout in response.
      UrlbarPrefs.set("trackerCountShown", true);
    }
    const trackerCountLongform = document.getElementById(
      "trust-icon-tracker-count-longform"
    );
    if (trackerCountLongform) {
      document.l10n.setArgs(trackerCountLongform, { count });
    }
    const trackerCountShortform = document.getElementById(
      "trust-icon-tracker-count-shortform"
    );
    if (trackerCountShortform) {
      trackerCountShortform.textContent = count;
    }
    document.l10n.setArgs(iconContainer, { count });
    this.#updateUrlbarIcon();
  }

  async #updateBlockerView() {
    // Snapshot the event so this run stays internally consistent across the
    // awaits below, and tag the run so that if a newer run starts while we're
    // awaiting, this (now stale) one bails out instead of writing its result.
    // Without this guard, concurrent runs — a burst of content-blocking events
    // on a tracker-heavy site (e.g. Meta) plus opening the subview — race on
    // the final write, and a stale run can finish last and clobber a fresher
    // count with 0, producing the intermittent "0 trackers blocked".
    const event = this.#lastEvent;
    const updateId = ++this.#blockerViewUpdateId;

    let blocked = [];
    let detected = [];
    for (let blocker of Object.values(this.#blockers)) {
      if (blocker.isBlocking(event)) {
        blocked.push(blocker);
      } else if (blocker.isDetected(event)) {
        detected.push(blocker);
      }
    }

    // Share the single per-event count computation with the toolbar update so a
    // content-blocking event only queries each blocker once.
    const count = await this.#computeTrackerCount();

    // A newer run started while we were awaiting; let it own the DOM update.
    if (updateId !== this.#blockerViewUpdateId) {
      return;
    }

    this.#addButtons("trustpanel-blocked", blocked, true);
    this.#addButtons("trustpanel-detected", detected, false);

    document
      .getElementById("trustpanel-smartblock-section")
      .toggleAttribute("hidden", !this.#addSmartblockEmbedToggles());

    this.#updateAttribute(
      document.getElementById("trustpanel-blocker-section"),
      "hidden",
      // avoids a misleading "0 trackers blocked" for trackers that are detected
      // but not blocked, which happens transiently during page load, since
      // detection events can arrive before the blocking ones.
      count === 0
    );

    // This element is in the main view but updated in case
    // any content blocking events were missed.
    document.l10n.setArgs(
      document.getElementById("trustpanel-blocker-section-header"),
      { count }
    );
  }

  async #showSecurityPopup() {
    await this.#hidePopup();
    window.BrowserCommands.pageInfo(null, "securityTab");
  }

  #removeCertException() {
    let overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(
      Ci.nsICertOverrideService
    );
    overrideService.clearValidityOverride(
      this.#uri.host,
      this.#uri.port > 0 ? this.#uri.port : 443,
      gBrowser.contentPrincipal.originAttributes
    );
    BrowserCommands.reloadSkipCache();
    PanelMultiView.hidePopup(this.#popup);
  }

  #trackingProtectionStatus() {
    if (!this.#isSecurePage()) {
      return "warning";
    }
    return this.#trackingProtectionEnabled ? "enabled" : "disabled";
  }

  #updateSecurityInformationSubview() {
    document.l10n.setAttributes(
      document.getElementById("trustpanel-securityInformationView"),
      "trustpanel-site-information-header",
      { host: this.#host }
    );

    let connection = this.#connectionState();
    let mixedcontent = this.#mixedContentState();
    let ciphers = this.#ciphersState();
    let httpsOnlyStatus = this.#httpsOnlyState();

    // Update all elements.
    let elementIDs = [
      "trustpanel-popup",
      "identity-popup-securityView-extended-info",
    ];

    for (let id of elementIDs) {
      let element = document.getElementById(id);
      this.#updateAttribute(element, "connection", connection);
      this.#updateAttribute(element, "ciphers", ciphers);
      this.#updateAttribute(element, "mixedcontent", mixedcontent);
      this.#updateAttribute(element, "isbroken", this.#isBrokenConnection);
      element.toggleAttribute("customroot", this.#hasCustomRoot());
      element.toggleAttribute("tlskeylogging", this.#tlsKeyLoggingEnabled());
      this.#updateAttribute(element, "httpsonlystatus", httpsOnlyStatus);
    }

    let { supplemental, owner, verifier } = this.#supplementalText();
    document.getElementById("identity-popup-content-supplemental").textContent =
      supplemental;
    document.getElementById("identity-popup-content-verifier").textContent =
      verifier;
    document.getElementById("identity-popup-content-owner").textContent = owner;
  }

  #openSecurityInformationSubview(event) {
    this.#updateSecurityInformationSubview();
    document
      .getElementById("trustpanel-popup-multiView")
      .showSubView("trustpanel-securityInformationView", event.target);
  }

  async #openBlockerSubview(event) {
    document.l10n.setAttributes(
      document.getElementById("trustpanel-blockerView"),
      "trustpanel-blocker-header",
      { host: this.#host }
    );
    await this.#updateBlockerView();
    document
      .getElementById("trustpanel-popup-multiView")
      .showSubView("trustpanel-blockerView", event.target);
  }

  async #openBlockerDetailsSubview(event, blocker, blocking) {
    let count = await blocker.getBlockerCount();
    let blockingKey = blocking ? "blocking" : "not-blocking";
    document.l10n.setAttributes(
      document.getElementById("trustpanel-blockerDetailsView"),
      blocker.l10nKeys.title[blockingKey]
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-blocker-details-header"),
      `trustpanel-${blocker.l10nKeys.general}-${blockingKey}-tab-header`,
      { count }
    );
    document.l10n.setAttributes(
      document.getElementById("trustpanel-blocker-details-content"),
      `protections-panel-${blocker.l10nKeys.content}`
    );

    let listHeaderId;
    if (blocker.l10nKeys.general == "fingerprinter") {
      listHeaderId = "trustpanel-fingerprinter-list-header";
    } else if (blocker.l10nKeys.general == "cryptominer") {
      listHeaderId = "trustpanel-cryptominer-tab-list-header";
    } else {
      listHeaderId = "trustpanel-tracking-content-tab-list-header";
    }

    document.l10n.setAttributes(
      document.getElementById("trustpanel-blocker-details-list-header"),
      listHeaderId
    );

    let { items } = await blocker._generateSubViewListItems();
    document.getElementById("trustpanel-blocker-items").replaceChildren(items);
    document
      .getElementById("trustpanel-popup-multiView")
      .showSubView("trustpanel-blockerDetailsView", event.target);
  }

  async #showClearCookiesSubview(event) {
    document.l10n.setAttributes(
      document.getElementById("trustpanel-clearcookiesView"),
      "trustpanel-clear-cookies-header",
      { host: this.#host }
    );
    document
      .getElementById("trustpanel-popup-multiView")
      .showSubView("trustpanel-clearcookiesView", event.target);
  }

  async #addButtons(section, blockers, blocking) {
    let sectionElement = document.getElementById(section);

    if (!blockers.length) {
      sectionElement.hidden = true;
      return;
    }

    let children = blockers.map(async blocker => {
      let button = document.createElement("moz-button");
      button.classList.add("moz-button-subviewbutton-nav");
      button.setAttribute("iconsrc", blocker.iconSrc);
      button.setAttribute("type", "ghost icon");
      document.l10n.setAttributes(
        button,
        `trustpanel-list-label-${blocker.l10nKeys.general}`,
        { count: await blocker.getBlockerCount() }
      );
      button.addEventListener("click", event =>
        this.#openBlockerDetailsSubview(event, blocker, blocking)
      );
      return button;
    });

    sectionElement.hidden = false;
    sectionElement
      .querySelector(".trustpanel-blocker-buttons")
      .replaceChildren(...(await Promise.all(children)));
  }

  get #trackingProtectionEnabled() {
    return (
      !ContentBlockingAllowList.canHandle(window.gBrowser.selectedBrowser) ||
      !ContentBlockingAllowList.includes(window.gBrowser.selectedBrowser)
    );
  }

  async #hasMonitorAccount() {
    const state = UIState.get();
    if (state.status !== UIState.STATUS_SIGNED_IN) {
      return false;
    }
    try {
      const attachedClients =
        (await fxAccounts.listAttachedOAuthClients(
          this.#clearFxaOauthClientCache
        )) ?? [];
      this.#clearFxaOauthClientCache = false;

      const hasMonitorClient = attachedClients.some(
        client => client.id === FX_MONITOR_OAUTH_CLIENT_ID
      );

      return hasMonitorClient;
    } catch (error) {
      console.warn("Failed to fetch attached OAuth clients:", error);
      return false;
    }
  }

  async #hasStoredPasswords() {
    try {
      const count = await Services.logins.countLoginsAsync("", "", "");
      return count > 0;
    } catch (error) {
      console.warn("Failed to check stored passwords:", error);
      return false;
    }
  }

  async #hasMonitorAccountOrStoredPasswords() {
    return (
      (await this.#hasMonitorAccount()) || (await this.#hasStoredPasswords())
    );
  }

  #isSecurePage() {
    if (this.#isInternalSecurePage) {
      return true;
    }
    if (this.#isCertErrorPage || this.#isCertUserOverridden) {
      return false;
    }
    if (this.#isSecureConnection) {
      return true;
    }
    if (this.#isBrokenConnection) {
      return false;
    }
    if (this.#isPotentiallyTrustworthy) {
      return true;
    }
    return false;
  }

  get #isInternalSecurePage() {
    if (this.#uri?.schemeIs("about")) {
      let module = E10SUtils.getAboutModule(this.#uri);
      if (module) {
        let flags = module.getURIFlags(this.#uri);
        if (flags & Ci.nsIAboutModule.IS_SECURE_CHROME_UI) {
          return true;
        }
      }
    }
    return false;
  }

  #clearSiteData() {
    let baseDomain = SiteDataManager.getBaseDomainFromHost(this.#uri.host);
    SiteDataManager.remove(baseDomain);
    this.#hidePopup();
  }

  #toggleTrackingProtection() {
    if (this.#trackingProtectionEnabled) {
      ContentBlockingAllowList.add(window.gBrowser.selectedBrowser);
    } else {
      ContentBlockingAllowList.remove(window.gBrowser.selectedBrowser);
    }

    PanelMultiView.hidePopup(this.#popup);
    window.BrowserCommands.reload();
  }

  #isHttpsOnlyModeActive(isWindowPrivate) {
    return httpsOnlyModeEnabled || (isWindowPrivate && httpsOnlyModeEnabledPBM);
  }

  #isHttpsFirstModeActive(isWindowPrivate) {
    return (
      !this.#isHttpsOnlyModeActive(isWindowPrivate) &&
      (httpsFirstModeEnabled || (isWindowPrivate && httpsFirstModeEnabledPBM))
    );
  }
  #isSchemelessHttpsFirstModeActive(isWindowPrivate) {
    return (
      !this.#isHttpsOnlyModeActive(isWindowPrivate) &&
      !this.#isHttpsFirstModeActive(isWindowPrivate) &&
      schemelessHttpsFirstModeEnabled
    );
  }
  /**
   * Helper to parse out the important parts of _secInfo (of the SSL cert in
   * particular) for use in constructing identity UI strings
   */
  #getIdentityData(cert = this.#secInfo.serverCert) {
    var result = {};

    // Human readable name of Subject
    result.subjectOrg = cert.organization;

    // SubjectName fields, broken up for individual access
    if (cert.subjectName) {
      result.subjectNameFields = {};
      cert.subjectName.split(",").forEach(function (v) {
        var field = v.split("=");
        this[field[0]] = field[1];
      }, result.subjectNameFields);

      // Call out city, state, and country specifically
      result.city = result.subjectNameFields.L;
      result.state = result.subjectNameFields.ST;
      result.country = result.subjectNameFields.C;
    }

    // Human readable name of Certificate Authority
    result.caOrg = cert.issuerOrganization || cert.issuerCommonName;
    result.cert = cert;

    return result;
  }

  get #isSecureContext() {
    if (gBrowser.contentPrincipal?.originNoSuffix != "resource://pdf.js") {
      return gBrowser.securityUI.isSecureContext;
    }

    // For PDF viewer pages (pdf.js) we can't rely on the isSecureContext field.
    // The backend will return isSecureContext = true, because the content
    // principal has a resource:// URI. Instead use the URI of the selected
    // browser to perform the isPotentiallyTrustWorthy check.

    let principal;
    try {
      principal = Services.scriptSecurityManager.createContentPrincipal(
        gBrowser.selectedBrowser.documentURI,
        {}
      );
      return principal.isOriginPotentiallyTrustworthy;
    } catch (error) {
      console.error(
        "Error while computing isPotentiallyTrustWorthy for pdf viewer page: ",
        error
      );
      return false;
    }
  }

  /**
   * Returns whether the issuer of the current certificate chain is
   * built-in (returns false) or imported (returns true).
   * Can only be true for secure connections and where there isn't a
   * user-added error override.
   */
  #hasCustomRoot() {
    return (
      this.#isSecureConnection &&
      !this.#isCertUserOverridden &&
      this.#secInfo &&
      !this.#secInfo.isBuiltCertChainRootBuiltInRoot
    );
  }

  /**
   * Returns true if TLS key logging has been enabled via the environment
   * variable "SSLKEYLOGFILE".
   * Can only be true for secure connections and where there isn't a
   * user-added error override.
   */
  #tlsKeyLoggingEnabled() {
    return (
      Services.env.exists("SSLKEYLOGFILE") &&
      this.#isSecureConnection &&
      !this.#isCertUserOverridden
    );
  }

  /**
   * Whether the established HTTPS connection is considered "broken".
   * This could have several reasons, such as mixed content or weak
   * cryptography. If this is true, _isSecureConnection is false.
   */
  get #isBrokenConnection() {
    return this.#state & Ci.nsIWebProgressListener.STATE_IS_BROKEN;
  }

  /**
   * Whether the connection to the current site was done via secure
   * transport. Note that this attribute is not true in all cases that
   * the site was accessed via HTTPS, i.e. _isSecureConnection will
   * be false when _isBrokenConnection is true, even though the page
   * was loaded over HTTPS.
   */
  get #isSecureConnection() {
    // If a <browser> is included within a chrome document, then this._state
    // will refer to the security state for the <browser> and not the top level
    // document. In this case, don't upgrade the security state in the UI
    // with the secure state of the embedded <browser>.
    return (
      !this.#isURILoadedFromFile &&
      this.#state & Ci.nsIWebProgressListener.STATE_IS_SECURE
    );
  }

  // Using a getter rather than a method reduces call-site noise (this.#host vs
  // this.#host()) and avoids churn when the implementation changes. Semantically,
  // this behaves like a property derived from #uri, so a getter is the right fit.
  get #host() {
    if (!this.#uri) {
      return null;
    }
    return BrowserUtils.formatURIForDisplay(this.#uri, {
      onlyBaseDomain: true,
    });
  }

  get #isEV() {
    // If a <browser> is included within a chrome document, then this._state
    // will refer to the security state for the <browser> and not the top level
    // document. In this case, don't upgrade the security state in the UI
    // with the EV state of the embedded <browser>.
    return (
      !this.#isURILoadedFromFile &&
      this.#state & Ci.nsIWebProgressListener.STATE_IDENTITY_EV_TOPLEVEL
    );
  }

  get #isAssociatedIdentity() {
    return this.#state & Ci.nsIWebProgressListener.STATE_IDENTITY_ASSOCIATED;
  }

  get #isMixedActiveContentLoaded() {
    return (
      this.#state & Ci.nsIWebProgressListener.STATE_LOADED_MIXED_ACTIVE_CONTENT
    );
  }

  get #isMixedActiveContentBlocked() {
    return (
      this.#state & Ci.nsIWebProgressListener.STATE_BLOCKED_MIXED_ACTIVE_CONTENT
    );
  }

  get #isMixedPassiveContentLoaded() {
    return (
      this.#state & Ci.nsIWebProgressListener.STATE_LOADED_MIXED_DISPLAY_CONTENT
    );
  }

  get #isContentHttpsOnlyModeUpgraded() {
    return (
      this.#state & Ci.nsIWebProgressListener.STATE_HTTPS_ONLY_MODE_UPGRADED
    );
  }

  get #isContentHttpsOnlyModeUpgradeFailed() {
    return (
      this.#state &
      Ci.nsIWebProgressListener.STATE_HTTPS_ONLY_MODE_UPGRADE_FAILED
    );
  }

  get #isContentHttpsFirstModeUpgraded() {
    return (
      this.#state &
      Ci.nsIWebProgressListener.STATE_HTTPS_ONLY_MODE_UPGRADED_FIRST
    );
  }

  get #isCertUserOverridden() {
    return this.#state & Ci.nsIWebProgressListener.STATE_CERT_USER_OVERRIDDEN;
  }

  get #isCertErrorPage() {
    let { documentURI } = gBrowser.selectedBrowser;
    if (documentURI?.scheme != "about") {
      return false;
    }

    return (
      documentURI.filePath == "certerror" ||
      (documentURI.filePath == "neterror" &&
        new URLSearchParams(documentURI.query).get("e") == "nssFailure2")
    );
  }

  get #isSecurelyConnectedAboutNetErrorPage() {
    let { documentURI } = gBrowser.selectedBrowser;
    if (documentURI?.scheme != "about" || documentURI.filePath != "neterror") {
      return false;
    }

    let error = new URLSearchParams(documentURI.query).get("e");

    // Bug 1944993 - A list of neterrors without connection issues
    return error === "httpErrorPage" || error === "serverError";
  }

  get #isAboutNetErrorPage() {
    let { documentURI } = gBrowser.selectedBrowser;
    return documentURI?.scheme == "about" && documentURI.filePath == "neterror";
  }

  get #isAboutHttpsOnlyErrorPage() {
    let { documentURI } = gBrowser.selectedBrowser;
    return (
      documentURI?.scheme == "about" && documentURI.filePath == "httpsonlyerror"
    );
  }

  get #isPotentiallyTrustworthy() {
    return (
      !this.#isBrokenConnection &&
      (this.#isSecureContext ||
        gBrowser.selectedBrowser.documentURI?.scheme == "chrome")
    );
  }

  get #isAboutBlockedPage() {
    let { documentURI } = gBrowser.selectedBrowser;
    return documentURI?.scheme == "about" && documentURI.filePath == "blocked";
  }

  get #isURILoadedFromFile() {
    return this.#uri.schemeIs("file");
  }

  /**
   * Returns a promise that will resolve when determining QWAC status has
   * finished. Primarily intended for tests.
   */
  get qwacStatusPromise() {
    return this.#qwacStatusPromise;
  }

  #supplementalText() {
    let supplemental = "";
    let verifier = "";
    let owner = "";

    // Fill in the CA name if we have a valid TLS certificate.
    if (this.#isSecureConnection) {
      verifier = this.#getIdentityData().caOrg;
    }

    // Fill in organization information if we have a valid EV certificate or
    // QWAC.
    if (this.#isEV || this.#qwac) {
      let identityData = this.#getIdentityData(
        this.#qwac || this.#secInfo.serverCert
      );
      owner = identityData.subjectOrg;
      verifier = identityData.caOrg;

      // Build an appropriate supplemental block out of whatever location data we have
      if (identityData.city) {
        supplemental += identityData.city + "\n";
      }
      if (identityData.state && identityData.country) {
        supplemental += gNavigatorBundle.getFormattedString(
          "identity.identified.state_and_country",
          [identityData.state, identityData.country]
        );
      } else if (identityData.state) {
        // State only
        supplemental += identityData.state;
      } else if (identityData.country) {
        // Country only
        supplemental += identityData.country;
      }
    }
    return { supplemental, verifier, owner };
  }

  #tooltipText() {
    let tooltip = "";
    let warnTextOnInsecure =
      insecureConnectionTextEnabled ||
      (insecureConnectionTextPBModeEnabled &&
        PrivateBrowsingUtils.isWindowPrivate(window));

    if (this.#uriHasHost && this.#isSecureConnection) {
      // This is a secure connection.
      if (!this.#isCertUserOverridden) {
        // It's a normal cert, verifier is the CA Org.
        tooltip = gNavigatorBundle.getFormattedString(
          "identity.identified.verifier",
          [this.#getIdentityData().caOrg]
        );
      }
    } else if (this.#isBrokenConnection) {
      if (this.#isMixedActiveContentLoaded) {
        if (
          UrlbarPrefs.getScotchBonnetPref("trimHttps") &&
          warnTextOnInsecure
        ) {
          tooltip = gNavigatorBundle.getString("identity.notSecure.tooltip");
        }
      }
    } else if (!this.#isPotentiallyTrustworthy) {
      tooltip = gNavigatorBundle.getString("identity.notSecure.tooltip");
    }

    if (this.#isCertUserOverridden) {
      // Cert is trusted because of a security exception, verifier is a special string.
      tooltip = gNavigatorBundle.getString(
        "identity.identified.verified_by_you"
      );
    }
    return tooltip;
  }

  #connectionState() {
    // Determine connection security information.
    let connection = "not-secure";
    if (this.#isInternalSecurePage) {
      connection = "chrome";
    } else if (this.#pageExtensionPolicy) {
      connection = "extension";
    } else if (this.#isURILoadedFromFile) {
      connection = "file";
    } else if (this.#qwac) {
      connection = "secure-etsi";
    } else if (this.#isEV) {
      connection = "secure-ev";
    } else if (this.#isCertUserOverridden) {
      connection = "secure-cert-user-overridden";
    } else if (this.#isSecureConnection) {
      connection = "secure";
    } else if (this.#isCertErrorPage) {
      connection = "cert-error-page";
    } else if (this.#isAboutHttpsOnlyErrorPage) {
      connection = "https-only-error-page";
    } else if (this.#isAboutBlockedPage) {
      connection = "not-secure";
    } else if (this.#isSecurelyConnectedAboutNetErrorPage) {
      connection = "secure";
    } else if (this.#isAboutNetErrorPage) {
      connection = "net-error-page";
    } else if (this.#isAssociatedIdentity) {
      connection = "associated";
    } else if (this.#isPotentiallyTrustworthy) {
      connection = "file";
    }
    return connection;
  }

  #connectionLabel() {
    if (this.#isAboutNetErrorPage) {
      return "identity-connection-failure";
    }
    if (this.#isSecurePage()) {
      return "trustpanel-connection-label-secure";
    }
    return "trustpanel-connection-label-insecure";
  }

  #mixedContentState() {
    let mixedcontent = [];
    if (this.#isMixedPassiveContentLoaded) {
      mixedcontent.push("passive-loaded");
    }
    if (this.#isMixedActiveContentLoaded) {
      mixedcontent.push("active-loaded");
    } else if (this.#isMixedActiveContentBlocked) {
      mixedcontent.push("active-blocked");
    }
    return mixedcontent;
  }

  #ciphersState() {
    // We have no specific flags for weak ciphers (yet). If a connection is
    // broken and we can't detect any mixed content loaded then it's a weak
    // cipher.
    if (
      this.#isBrokenConnection &&
      !this.#isMixedActiveContentLoaded &&
      !this.#isMixedPassiveContentLoaded
    ) {
      return "weak";
    }
    return "";
  }

  #httpsOnlyState() {
    // If HTTPS-Only Mode is enabled, check the permission status
    const privateBrowsingWindow = PrivateBrowsingUtils.isWindowPrivate(window);
    const isHttpsOnlyModeActive = this.#isHttpsOnlyModeActive(
      privateBrowsingWindow
    );
    const isHttpsFirstModeActive = this.#isHttpsFirstModeActive(
      privateBrowsingWindow
    );
    const isSchemelessHttpsFirstModeActive =
      this.#isSchemelessHttpsFirstModeActive(privateBrowsingWindow);

    let httpsOnlyStatus = "";

    if (
      isHttpsFirstModeActive ||
      isHttpsOnlyModeActive ||
      isSchemelessHttpsFirstModeActive
    ) {
      // Note: value and permission association is laid out
      //       in _getHttpsOnlyPermission
      let value = this.#getHttpsOnlyPermission();

      // We do not want to display the exception ui for schemeless
      // HTTPS-First, but we still want the "Upgraded to HTTPS" label.
      document.getElementById(
        "trustpanel-popup-security-httpsonlymode"
      ).hidden = isSchemelessHttpsFirstModeActive;

      document.getElementById(
        "trustpanel-popup-security-menulist-off-item"
      ).hidden = privateBrowsingWindow && value != 1;
      document.getElementById(
        "trustpanel-popup-security-httpsonlymode-menulist"
      ).value = value;

      if (value > 0) {
        httpsOnlyStatus = "exception";
      } else if (
        this.#isAboutHttpsOnlyErrorPage ||
        (isHttpsFirstModeActive && this.#isContentHttpsOnlyModeUpgradeFailed)
      ) {
        httpsOnlyStatus = "failed-top";
      } else if (this.#isContentHttpsOnlyModeUpgradeFailed) {
        httpsOnlyStatus = "failed-sub";
      } else if (
        this.#isContentHttpsOnlyModeUpgraded ||
        this.#isContentHttpsFirstModeUpgraded
      ) {
        httpsOnlyStatus = "upgraded";
      }
    }
    return httpsOnlyStatus;
  }

  /**
   * Gets the current HTTPS-Only mode permission for the current page.
   * Values are the same as in #identity-popup-security-httpsonlymode-menulist,
   * -1 indicates a incompatible scheme on the current URI.
   */
  #getHttpsOnlyPermission() {
    let uri = gBrowser.currentURI;
    if (uri instanceof Ci.nsINestedURI) {
      uri = uri.QueryInterface(Ci.nsINestedURI).innermostURI;
    }
    if (!uri.schemeIs("http") && !uri.schemeIs("https")) {
      return -1;
    }
    uri = uri.mutate().setScheme("http").finalize();
    const principal = Services.scriptSecurityManager.createContentPrincipal(
      uri,
      gBrowser.contentPrincipal.originAttributes
    );
    const { state } = SitePermissions.getForPrincipal(
      principal,
      "https-only-load-insecure"
    );
    switch (state) {
      case Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION:
        return 2; // Off temporarily
      case Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW:
        return 1; // Off
      default:
        return 0; // On
    }
  }

  /**
   * Sets/removes HTTPS-Only Mode exception and possibly reloads the page.
   */
  #changeHttpsOnlyPermission() {
    // Get the new value from the menulist and the current value
    // Note: value and permission association is laid out
    //       in _getHttpsOnlyPermission
    const oldValue = this.#getHttpsOnlyPermission();
    if (oldValue < 0) {
      console.error(
        "Did not update HTTPS-Only permission since scheme is incompatible"
      );
      return;
    }

    let menulist = document.getElementById(
      "trustpanel-popup-security-httpsonlymode-menulist"
    );
    let newValue = parseInt(menulist.selectedItem.value, 10);

    // If nothing changed, just return here
    if (newValue === oldValue) {
      return;
    }

    // We always want to set the exception for the HTTP version of the current URI,
    // since when we check wether we should upgrade a request, we are checking permissons
    // for the HTTP principal (Bug 1757297).
    let newURI = gBrowser.currentURI;
    if (newURI instanceof Ci.nsINestedURI) {
      newURI = newURI.QueryInterface(Ci.nsINestedURI).innermostURI;
    }
    newURI = newURI.mutate().setScheme("http").finalize();
    const principal = Services.scriptSecurityManager.createContentPrincipal(
      newURI,
      gBrowser.contentPrincipal.originAttributes
    );

    // Set or remove the permission
    if (newValue === 0) {
      SitePermissions.removeFromPrincipal(
        principal,
        "https-only-load-insecure"
      );
    } else if (newValue === 1) {
      SitePermissions.setForPrincipal(
        principal,
        "https-only-load-insecure",
        Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW,
        SitePermissions.SCOPE_PERSISTENT
      );
    } else {
      SitePermissions.setForPrincipal(
        principal,
        "https-only-load-insecure",
        Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION,
        SitePermissions.SCOPE_SESSION
      );
    }

    // If we're on the error-page, we have to redirect the user
    // from HTTPS to HTTP. Otherwise we can just reload the page.
    if (this.#isAboutHttpsOnlyErrorPage) {
      gBrowser.loadURI(newURI, {
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
        loadFlags: Ci.nsIWebNavigation.LOAD_FLAGS_REPLACE_HISTORY,
      });
      PanelMultiView.hidePopup(this.#popup);
      return;
    }
    // The page only needs to reload if we switch between allow and block
    // Because "off" is 1 and "off temporarily" is 2, we can just check if the
    // sum of newValue and oldValue is 3.
    if (newValue + oldValue !== 3) {
      BrowserCommands.reloadSkipCache();
      PanelMultiView.hidePopup(this.#popup);
      // Ensure the browser is focused again, otherwise we may not trigger the
      // security delay on a potential error page following this reload.
      gBrowser.selectedBrowser.focus();
    }
  }

  /**
   * Adds the toggles into the smartblock toggle container. Clears existing toggles first, then
   * searches through the contentBlockingLog for smartblock-compatible content.
   *
   * @returns {boolean} true if a smartblock compatible resource is blocked or shimmed, false otherwise
   */
  #addSmartblockEmbedToggles() {
    if (!smartblockEmbedsEnabledPref) {
      // Do not insert toggles if feature is disabled.
      return false;
    }

    let container = document.getElementById(
      "trustpanel-smartblock-toggle-container"
    );
    container.replaceChildren();

    // check that there is an allowed or replaced flag present
    let contentBlockingEvents =
      gBrowser.selectedBrowser.getContentBlockingEvents();

    // In the future, we should add a flag specifically for smartblock embeds so that
    // these checks do not trigger when a non-embed-related shim is shimming
    // a smartblock compatible site, see Bug 1926461
    let somethingAllowedOrReplaced =
      contentBlockingEvents &
        Ci.nsIWebProgressListener.STATE_ALLOWED_TRACKING_CONTENT ||
      contentBlockingEvents &
        Ci.nsIWebProgressListener.STATE_REPLACED_TRACKING_CONTENT;

    if (!somethingAllowedOrReplaced) {
      // return early if there is no content that is allowed or replaced
      return false;
    }

    let blocked = this.#fetchSmartBlocked();
    if (!blocked.length) {
      return false;
    }

    // search through content log for compatible blocked origins
    for (let { shimAllowed, shimInfo } of blocked) {
      const { shimId, displayName } = shimInfo;

      // check that a toggle doesn't already exist
      let existingToggle = document.getElementById(
        `trustpanel-smartblock-${shimId.toLowerCase()}-toggle`
      );
      if (existingToggle) {
        // make sure toggle state is allowed if ANY of the sites are allowed
        if (shimAllowed) {
          existingToggle.setAttribute("pressed", true);
        }
        // skip adding a new toggle
        continue;
      }

      // create the toggle element
      let toggle = document.createElement("moz-toggle");
      toggle.setAttribute(
        "id",
        `trustpanel-smartblock-${shimId.toLowerCase()}-toggle`
      );
      toggle.setAttribute("data-l10n-attrs", "label");
      document.l10n.setAttributes(
        toggle,
        "protections-panel-smartblock-blocking-toggle",
        {
          trackername: displayName,
        }
      );

      // set toggle to correct position
      toggle.toggleAttribute("pressed", !!shimAllowed);

      // add functionality to toggle
      toggle.addEventListener("toggle", event => {
        if (event.target.pressed) {
          this.#sendUnblockMessageToSmartblock(shimId);
        } else {
          this.#sendReblockMessageToSmartblock(shimId);
        }
        PanelMultiView.hidePopup(this.#popup);
      });

      container.insertAdjacentElement("beforeend", toggle);
    }
    return true;
  }

  #fetchSmartBlocked() {
    let blocked = [];
    let contentBlockingLog = JSON.parse(
      gBrowser.selectedBrowser.getContentBlockingLog()
    );
    // search through content log for compatible blocked origins
    for (let [origin, actions] of Object.entries(contentBlockingLog)) {
      let shimAllowed = actions.some(
        ([flag]) =>
          (flag & Ci.nsIWebProgressListener.STATE_ALLOWED_TRACKING_CONTENT) != 0
      );

      let shimDetected = actions.some(
        ([flag]) =>
          (flag & Ci.nsIWebProgressListener.STATE_REPLACED_TRACKING_CONTENT) !=
          0
      );

      if (!shimAllowed && !shimDetected) {
        // origin is not being shimmed or allowed
        continue;
      }

      let shimInfo = SMARTBLOCK_EMBED_INFO.find(element => {
        let matchPatternSet = new MatchPatternSet(element.matchPatterns);
        return matchPatternSet.matches(origin);
      });
      if (!shimInfo) {
        // origin not relevant to smartblock
        continue;
      }

      blocked.push({ shimAllowed, shimInfo });
    }
    return blocked;
  }

  async observe(subject, topic) {
    if (!this.#enabled) {
      return;
    }
    switch (topic) {
      case "fxaccounts:onlogout": {
        // Clear OAuth clients cache when user signs out
        this.#clearFxaOauthClientCache = true;
        break;
      }
      case "smartblock:open-protections-panel": {
        if (gBrowser.selectedBrowser.browserId !== subject.browserId) {
          break;
        }
        this.#initializePopup();
        let multiview = document.getElementById("trustpanel-popup-multiView");
        // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1999928
        // This currently opens as a standalone panel, we would like to open
        // the panel with a back button and title the same way as if it
        // were accessed via the urlbar icon.
        let initialMainViewId = multiview.getAttribute("mainViewId");
        this.#popup.addEventListener(
          "popuphidden",
          () => {
            multiview.setAttribute("mainViewId", initialMainViewId);
          },
          { once: true }
        );
        multiview.setAttribute("mainViewId", "trustpanel-blockerView");
        this.showPopup({ reason: "embedPlaceholderButton" });
        break;
      }
    }
  }

  // We handle focus here when the panel is shown.
  handleEvent(event) {
    switch (event.type) {
      case "focus": {
        let elem = document.activeElement;
        let position = elem.compareDocumentPosition(this.#popup);
        if (
          !(
            position &
            (Node.DOCUMENT_POSITION_CONTAINS |
              Node.DOCUMENT_POSITION_CONTAINED_BY)
          ) &&
          !this.#popup.hasAttribute("noautohide")
        ) {
          // Hide the panel when focusing an element that is
          // neither an ancestor nor descendant unless the panel has
          // @noautohide (e.g. for a tour).
          PanelMultiView.hidePopup(this.#popup);
        }
        break;
      }
      case "popupshown":
        this.onPopupShown(event);
        break;
      case "popuphidden":
        this.onPopupHidden(event);
        break;
    }
  }

  onPopupShown() {
    window.addEventListener("focus", this, true);
    PopupNotifications.suppressWhileOpen(this.#popup);
    // Disable the toggles for a short time after opening via SmartBlock placeholder button
    // to prevent clickjacking.
    if (this.#openingReason == "embedPlaceholderButton") {
      this.#disablePopupToggles();
      this.#popupToggleDelayTimer = setTimeout(() => {
        this.#enablePopupToggles();
      }, popupClickjackDelay);
    }
  }

  onPopupHidden() {
    window.removeEventListener("focus", this, true);
  }

  /**
   * Sends a message to webcompat extension to unblock content and remove placeholders
   *
   * @param {string} shimId - the id of the shim blocking the content
   */
  #sendUnblockMessageToSmartblock(shimId) {
    Services.obs.notifyObservers(
      gBrowser.selectedTab,
      "smartblock:unblock-embed",
      shimId
    );
  }

  /**
   * Sends a message to webcompat extension to reblock content
   *
   * @param {string} shimId - the id of the shim blocking the content
   */
  #sendReblockMessageToSmartblock(shimId) {
    Services.obs.notifyObservers(
      gBrowser.selectedTab,
      "smartblock:reblock-embed",
      shimId
    );
  }

  #resetToggleSecDelay() {
    clearTimeout(this.#popupToggleDelayTimer);
    this.#popupToggleDelayTimer = setTimeout(() => {
      this.#enablePopupToggles();
    }, popupClickjackDelay);
  }

  #disablePopupToggles() {
    // Disables all toggles in the protections panel
    this.#popup.querySelectorAll("moz-toggle").forEach(toggle => {
      toggle.setAttribute("disabled", true);
      toggle.addEventListener("pointerdown", this.#resetToggleReference);
    });
  }

  #resetToggleReference = this.#resetToggleSecDelay.bind(this);
  #enablePopupToggles() {
    // Enables all toggles in the protections panel
    this.#popup.querySelectorAll("moz-toggle").forEach(toggle => {
      if (
        toggle.id != "trustpanel-toggle" ||
        ContentBlockingAllowList.canHandle(window.gBrowser.selectedBrowser)
      ) {
        toggle.removeAttribute("disabled");
      }
      toggle.removeEventListener("pointerdown", this.#resetToggleReference);
    });
  }

  #updateAttribute(elem, attr, value) {
    if (value) {
      elem.setAttribute(attr, value);
    } else {
      elem.removeAttribute(attr);
    }
  }

  async #getBreachAlertStorage() {
    if (this.#breachAlertStoragePromise === null) {
      const initializeStorage = async () => {
        const storage = new BreachAlertStorage();
        await storage.initialize();
        return storage;
      };
      this.#breachAlertStoragePromise = initializeStorage();
    }
    return this.#breachAlertStoragePromise;
  }

  async #getBreachedWebsites() {
    const REMOTE_SETTINGS_COLLECTION = "fxmonitor-breaches";

    try {
      const breaches = await RemoteSettings(REMOTE_SETTINGS_COLLECTION).get();
      return breaches;
    } catch (ex) {
      console.error("Could not get breach data from Remote Settings:", ex);
      return [];
    }
  }

  async #getApplicableBreaches(site) {
    const breaches = await this.#getBreachedWebsites();

    if (!site || !breaches.length) {
      return [];
    }

    // Get breaches applying to the current domain...
    const breachesForSite = breaches.filter(breach => {
      return breach.Domain && Services.eTLD.hasRootDomain(site, breach.Domain);
    });

    // ...filter them down to those that occurred in the last year...
    const recentBreaches = breachesForSite.filter(isRecentBreach);

    // ...and then load dismissals for those breaches,
    // and filter out those that the user has dismissed:
    const breachAlertStorage = await this.#getBreachAlertStorage();
    const dismissedBreachNames = (
      await breachAlertStorage.getBreachAlertDismissals(
        recentBreaches.map(breach => breach.Name)
      )
    ).map(breachDismissal => breachDismissal.breachName);
    const undismissedBreaches = recentBreaches.filter(
      recentBreach => !dismissedBreachNames.includes(recentBreach.Name)
    );

    return undismissedBreaches;
  }

  async dismissBreachAlert(event) {
    const breachNames = event.detail.breachNames;
    if (!breachNames) {
      return;
    }

    try {
      const timeDismissed = Date.now();
      const dismissals = breachNames.map(breachName => ({
        breachName,
        timeDismissed,
      }));
      const breachAlertStorage = await this.#getBreachAlertStorage();
      await breachAlertStorage.setBreachAlertDismissals(dismissals);
      this.#breachedStatus = "disabled";
    } catch (ex) {
      console.error("Failed to store breach dismissal:", ex);
    }
    this.#updateMainView();
    this.#updateUrlbarIcon();
  }
}

/**
 * @param {{ Name: string, Domain: string, BreachDate: string, AddedDate: string }} breach
 * @returns boolean
 */
function isRecentBreach(breach) {
  const currentDate = Temporal.Now.plainDateISO();
  // Although `breach.AddedDate` is an ISO8601 string,
  // `breach.BreachDate` is just `YYYY-MM-DD`:
  const breachedDate = Temporal.PlainDate.from(breach.BreachDate);

  const oneYearAgo = currentDate.subtract({ years: 1 });
  // Return `true` if the breach happened at most a year ago
  return Temporal.PlainDate.compare(breachedDate, oneYearAgo) !== -1;
}

function getBreachedStatus(inputData) {
  const { breaches, hasMonitorAccountOrStoredPasswords } = inputData;
  if (!UrlbarPrefs.get("trustPanel.breachAlerts")) {
    return "disabled";
  }

  if (hasMonitorAccountOrStoredPasswords) {
    // Currently, we only show a Call To Action to get a Monitor account,
    // which is not very useful to users who already have one.
    // Later, we'll provide functionality for Monitor users too.
    return "disabled";
  }

  // The plan is to eventually add other conditions in which to show the
  // breach alert with different content. Currently, we just show an alert
  // when the site is breached, but we could e.g. add a more pressing warning
  // if we know the user's credentials specifically have been included.
  if (breaches.length) {
    return "breached";
  }

  return "not-breached";
}

var gTrustPanelHandler = new TrustPanel();
