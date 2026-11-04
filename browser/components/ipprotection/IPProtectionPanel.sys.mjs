/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  IPPPermissionRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPPrincipalRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPSiteRuleManager:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPOnboardingMessage:
    "moz-src:///browser/components/ipprotection/IPPOnboardingMessageHelper.sys.mjs",
  IPPMode:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  ERRORS: "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPUsageHelper:
    "moz-src:///browser/components/ipprotection/IPPUsageHelper.sys.mjs",
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionInfobarManager:
    "moz-src:///browser/components/ipprotection/IPProtectionInfobarManager.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import {
  BANDWIDTH,
  ONBOARDING_PREF_FLAGS,
  LINKS,
  SIGNIN_DATA,
} from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";
import { getSitePrincipal } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

const BANDWIDTH_THRESHOLD_PREF = "browser.ipProtection.bandwidthThreshold";
const BANDWIDTH_WARNING_DISMISSED_PREF =
  "browser.ipProtection.bandwidthWarningDismissedThreshold";
const BANDWIDTH_RESET_DATE_PREF = "browser.ipProtection.bandwidthResetDate";
const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";
const USER_OPENED_PREF = "browser.ipProtection.everOpenedPanel";
const USER_OPENED_SINCE_160_PREF = "browser.ipProtection.openedPanelSince160";
const UPGRADE_NOT_AVAILABLE_PREF = "browser.ipProtection.upgradeNotAvailable";
const SITE_INCLUSION_PROMPT_SHOWN_PREF =
  "browser.ipProtection.siteInclusionPromptShown";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "BANDWIDTH_USAGE_ENABLED",
  "browser.ipProtection.bandwidth.enabled",
  true
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "EGRESS_LOCATION",
  EGRESS_LOCATION_PREF,
  ""
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "UPGRADE_NOT_AVAILABLE",
  UPGRADE_NOT_AVAILABLE_PREF,
  false
);

let hasCustomElements = new WeakSet();

/**
 * Manages updates for a IP Protection panelView in a given browser window.
 */
export class IPProtectionPanel {
  static MAIN_PANELVIEW = "PanelUI-ipprotection";
  static LOCATIONS_PANELVIEW = "PanelUI-ipprotection-locations";
  static CONTENT_TAGNAME = "ipprotection-content";
  static LOCATIONS_TAGNAME = "ipprotection-locations";
  static CUSTOM_ELEMENTS_SCRIPT =
    "chrome://browser/content/ipprotection/ipprotection-customelements.js";
  static WIDGET_ID = "ipprotection-button";
  static TITLE_L10N_ID = "ipprotection-title";
  static HEADER_AREA_ID = "PanelUI-ipprotection-header";
  static CONTENT_AREA_ID = "PanelUI-ipprotection-content";
  static HEADER_BUTTON_ID = "ipprotection-header-button";

  /**
   * Loads the ipprotection custom element script
   * into a given window.
   *
   * Called on IPProtection.init for a new browser window.
   *
   * @param {Window} window
   */
  static loadCustomElements(window) {
    if (hasCustomElements.has(window)) {
      // Don't add the elements again for the same window.
      return;
    }
    Services.scriptloader.loadSubScriptWithOptions(
      IPProtectionPanel.CUSTOM_ELEMENTS_SCRIPT,
      {
        target: window,
      }
    );
    hasCustomElements.add(window);
  }

  /**
   * @typedef {object} State
   * @property {boolean} isProtectionEnabled
   *  The timestamp in milliseconds since IP Protection was enabled
   * @property {string} location
   *  The location country code
   * @property {Array<{code: string, available: boolean}>} locationsList
   *  Countries available as egress locations, from IPProtectionServerlist.
   * @property {"generic-error" | "network-error" | "vpn-unavailable" | ""} error
   *  The error type as a string if an error occurred, or empty string if there are no errors.
   * @property {boolean} hasUpgraded
   *  True if a Mozilla VPN subscription is linked to the user's Mozilla account.
   * @property {boolean} upgradeNotAvailable
   *  True if upgrade-related messaging should be suppressed regardless of subscription state.
   * @property {string} onboardingMessage
   * Continuous onboarding message to display in-panel, empty string if none applicable
   * @property {boolean} paused
   * True if the VPN service has been paused due to bandwidth limits
   * @property {boolean} isSiteExceptionsEnabled
   * True if site exceptions support is enabled, else false.
   * @property {boolean} isSiteInclusionsEnabled
   * True if site inclusions support is enabled, else false.
   * @property {object} siteData
   * Data about the currently loaded site, including "isExclusion".
   * @property {object} bandwidthUsage
   *  An object containing the current and max usage
   * @property {boolean} isActivating
   *  True if the VPN service is in the process of connecting, else false.
   * @property {boolean} isActive
   *  True if the VPN proxy is active, else false.
   * @property {boolean} isPremium
   * True if the current device has Firefox set as the default browser or the user has a Mozilla VPN subscription
   * Initially false; recomputed asynchronously before the panel is shown.
   * @property {boolean} showSiteInclusionPrompt
   * True if the prompt offering to always turn on VPN for the current site
   * should be shown, else false.
   */

  /**
   * @type {State}
   */
  state = {};
  panel = null;
  components = new WeakSet();
  initiatedUpgrade = false;
  #window = null;
  #showToken = null;
  #panelView = null;
  #headerButtons = [];
  #locationsClosedByKeyboard = false;
  #locationsKeyListener = e => {
    if (
      e.code !== "Tab" &&
      e.code !== "ArrowDown" &&
      e.code !== "ArrowUp" &&
      e.code !== "ArrowLeft" &&
      e.code !== "ArrowRight" &&
      e.code !== "Enter" &&
      e.code !== "NumpadEnter" &&
      e.code !== "Space"
    ) {
      return;
    }

    const view = this.locationsView;
    if (!view) {
      return;
    }

    const backButton = view.querySelector(".subviewbutton-back");
    const locationsList = view.querySelector("locations-list");
    const listItems = locationsList
      ? Array.from(locationsList.querySelectorAll(".location-item"))
      : [];
    const promoButton = view.querySelector("moz-promo moz-button");
    const focused = view.ownerDocument.activeElement;

    const isRTL = Services.locale.isAppLocaleRTL;
    const isBackArrow = isRTL
      ? e.code === "ArrowRight"
      : e.code === "ArrowLeft";

    if (e.code === "ArrowRight" || e.code === "ArrowLeft") {
      e.preventDefault();
      e.stopPropagation();
      if (isBackArrow) {
        this.#locationsClosedByKeyboard = true;
        this.panelMultiView?.goBack();
      }
      return;
    }

    // Make up/down arrow keys only work for the locations list
    if (e.code === "ArrowDown" || e.code === "ArrowUp") {
      e.preventDefault();
      e.stopPropagation();

      if (!view.contains(focused)) {
        return;
      }

      const isOnListItemForArrows = listItems.includes(focused);
      if (!isOnListItemForArrows) {
        return;
      }
      const arrowFocusedIndex = listItems.indexOf(focused);
      const nextListItem =
        e.code === "ArrowDown"
          ? listItems[(arrowFocusedIndex + 1) % listItems.length]
          : listItems[
              (arrowFocusedIndex - 1 + listItems.length) % listItems.length
            ];
      nextListItem?.focus();
      return;
    }

    if (e.code === "Enter" || e.code === "NumpadEnter" || e.code === "Space") {
      if (view.contains(focused) && focused === backButton) {
        e.preventDefault();
        e.stopPropagation();
        this.#locationsClosedByKeyboard = true;
        this.panelMultiView?.goBack();
      }

      if (focused?.hasAttribute("aria-disabled")) {
        e.preventDefault();
        e.stopPropagation();
      }

      return;
    }

    if (!view.contains(focused)) {
      return;
    }

    const isOnListItemForFocus = listItems.includes(focused);

    // Tab key handling. The list is a single tab stop: enter it on whichever
    // item currently carries the roving tabindex (the selected option when the
    // subview was shown, or the option focused since), falling back to the
    // first item.
    const listTabStop =
      listItems.find(item => item.tabIndex === 0) ?? listItems[0];
    const tabOnlyElements = [backButton, listTabStop, promoButton].filter(
      el => el != null
    );

    e.preventDefault();
    e.stopPropagation();

    // Force focus out of locations list if on a list item
    if (isOnListItemForFocus) {
      if (e.shiftKey) {
        backButton?.focus();
      } else {
        (promoButton ?? backButton)?.focus();
      }
      return;
    }

    const tabOnlyFocusedIndex = tabOnlyElements.indexOf(focused);
    const nextTabOnlyElement = e.shiftKey
      ? tabOnlyElements[
          (tabOnlyFocusedIndex - 1 + tabOnlyElements.length) %
            tabOnlyElements.length
        ]
      : tabOnlyElements[(tabOnlyFocusedIndex + 1) % tabOnlyElements.length];
    nextTabOnlyElement?.focus();
  };

  // Bug 2020733: Adds a key listener at the panel level
  //  since moz-button (header button) traps key events in its shadow DOM.
  //  This also avoids duplicate listeners across panel components.
  #panelKeyListener = e => {
    let { code } = e;
    if (code !== "ArrowDown" && code !== "ArrowUp") {
      return;
    }
    e.stopPropagation();
    e.preventDefault();
    let direction =
      code === "ArrowDown"
        ? Services.focus.MOVEFOCUS_FORWARD
        : Services.focus.MOVEFOCUS_BACKWARD;
    Services.focus.moveFocus(
      e.target.documentGlobal,
      null,
      direction,
      Services.focus.FLAG_BYKEY
    );
  };

  /**
   * Gets the gBrowser from the weak reference to the window.
   *
   * @returns {object|undefined}
   *  The gBrowser object, or undefined if the window has been garbage collected.
   */
  get gBrowser() {
    const win = this.#window.get();
    return win?.gBrowser;
  }

  /**
   * Gets the toolbar for this panel's window
   *
   * @return {IPProtectionToolbarButton|undefined}
   *  The toolbarbutton element, or undefined if the window has been garbage collected.
   */
  get toolbarButton() {
    const win = this.#window.get();
    return lazy.IPProtection.getToolbarButton(win);
  }

  /**
   * Check the state of the enclosing panel to see if
   * it is active (open or showing).
   */
  get active() {
    if (!this.panel) {
      return false;
    }
    return this.panel.state == "open" || this.panel.state == "showing";
  }

  get locationsView() {
    if (!this.panel) {
      return null;
    }
    return lazy.PanelMultiView.getViewNode(
      this.panel.ownerDocument,
      IPProtectionPanel.LOCATIONS_PANELVIEW
    );
  }

  get panelMultiView() {
    return this.panel?.querySelector("panelmultiview");
  }

  /**
   * Gets the value of the pref
   * browser.ipProtection.features.siteExceptions.
   */
  get isExceptionsFeatureEnabled() {
    return Services.prefs.getBoolPref(
      "browser.ipProtection.features.siteExceptions",
      false
    );
  }

  /**
   * Gets the value of the pref
   * browser.ipProtection.features.siteInclusions.
   */
  get isInclusionsFeatureEnabled() {
    return Services.prefs.getBoolPref(
      "browser.ipProtection.features.siteInclusions",
      false
    );
  }

  /**
   * Whether the user may use locked egress locations. Doesn't depend on any
   * window, so it is shared with IPPAutoActivation.
   *
   * @returns {Promise<boolean>}
   */
  static async isPremium() {
    let isPremium =
      lazy.IPProtectionService.authProvider.hasUpgraded ||
      (await lazy.ShellService.isDefaultBrowserAsync());
    return isPremium;
  }

  async isPremium() {
    return IPProtectionPanel.isPremium();
  }

  /**
   * Creates an instance of IPProtectionPanel for a specific browser window.
   *
   * Inserts the panel component customElements registry script.
   *
   * @param {Window} window
   *   Window containing the panelView to manage.
   */
  constructor(window) {
    this.#window = Cu.getWeakReference(window);

    this.handleEvent = this.#handleEvent.bind(this);
    this.handlePrefChange = this.#handlePrefChange.bind(this);

    this.state = {
      unauthenticated:
        lazy.IPProtectionService.state ===
        lazy.IPProtectionStates.UNAUTHENTICATED,
      isProtectionEnabled: lazy.IPPProxyManager.isDefaultModeActive,
      location: lazy.EGRESS_LOCATION || null,
      locationsList: lazy.IPProtectionServerlist.countries,
      error: "",
      hasUpgraded: lazy.IPProtectionService.authProvider.hasUpgraded,
      upgradeNotAvailable: lazy.UPGRADE_NOT_AVAILABLE,
      onboardingMessage: "",
      bandwidthWarning: false,
      paused: lazy.IPPProxyManager.state === lazy.IPPProxyStates.PAUSED,
      isSiteExceptionsEnabled: this.isExceptionsFeatureEnabled,
      isSiteInclusionsEnabled: this.isInclusionsFeatureEnabled,
      siteData: this.#getSiteData(),
      bandwidthUsage: this.#getBandwidthUsage(),
      isActivating:
        lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVATING,
      isActive: lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE,
      isEnrolling: lazy.IPProtectionService.authProvider.isEnrolling,
      isPremium: false,
      showSiteInclusionPrompt: false,
    };

    // The progress listener to listen for page navigations.
    // Used to update the siteData state property for site exclusions.
    this.progressListener = {
      onLocationChange: (
        aBrowser,
        aWebProgress,
        _aRequest,
        aLocationURI,
        _aFlags
      ) => {
        if (!aWebProgress.isTopLevel) {
          return;
        }

        // Only update if on the currently selected tab
        if (aBrowser !== this.gBrowser?.selectedBrowser) {
          return;
        }

        if (this.active && aLocationURI) {
          this.#updateSiteData();
          this.#updateProxyError();
        }
      },
    };

    const win = this.#window.get();
    if (win) {
      IPProtectionPanel.loadCustomElements(win);
    }

    this.#addProxyListeners();
    this.#addProgressListener();
    this.#addPrefObserver();
  }

  /**
   * Set the state for this panel.
   *
   * Updates the current panel component state,
   * if the panel is currently active (showing or not hiding).
   *
   * @example
   * panel.setState({
   *  isSomething: true,
   * });
   *
   * @param {object} state
   *    The state object from IPProtectionPanel.
   */
  setState(state) {
    Object.assign(this.state, state);

    if (this.active) {
      this.updateState();
    }
  }

  /**
   * Updates the state of all components.
   *
   * @param {object} [state]
   *   The state object from IPProtectionPanel.
   */
  updateState(state = this.state) {
    for (let component of ChromeUtils.nondeterministicGetWeakSetKeys(
      this.components
    )) {
      this.updateComponentState(component, state);
    }
  }

  /**
   * Updates the state of a single component.
   *
   * @param {Element} element
   *   The target element to update the state on.
   * @param {object} [state]
   *   The state object from IPProtectionPanel.
   */
  updateComponentState(element, state = this.state) {
    if (!element?.isConnected || !element.state) {
      return;
    }

    if (!this.components.has(element)) {
      this.components.add(element);
    }

    element.state = state;
    element.requestUpdate();
  }

  async #startProxy() {
    const win = this.#window.get();
    const inPrivateBrowsing =
      !!win && lazy.PrivateBrowsingUtils.isWindowPrivate(win);

    const country = await lazy.IPProtectionServerlist.resolveEgressCountry(() =>
      this.isPremium()
    );

    // A connection kept up for the always-on sites is widened, not restarted.
    if (lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE) {
      if (country) {
        lazy.IPPProxyManager.switch(country);
      }
      lazy.IPPProxyManager.setMode(lazy.IPPMode.getDefault());
      return;
    }

    const { error } = await lazy.IPPProxyManager.start(
      true,
      inPrivateBrowsing,
      country
    );
    // Cancellation, an exhausted quota and a not-ready proxy are already
    // represented elsewhere in the UI, so they must not raise an error message.
    const handledElsewhere = [
      lazy.ERRORS.CANCELED,
      lazy.ERRORS.QUOTA_EXHAUSTED,
      lazy.ERRORS.NOT_READY,
    ];
    if (error && !handledElsewhere.includes(error)) {
      const errorMessage = this.#errorMessage(error);
      this.setState({
        error: errorMessage,
        showSiteInclusionPrompt: false,
      });
      this.toolbarButton?.updateState(null, { error: errorMessage });
    }
  }

  #errorMessage(error) {
    if ([lazy.ERRORS.NETWORK, lazy.ERRORS.VPN_UNAVAILABLE].includes(error)) {
      return error;
    }
    return lazy.ERRORS.GENERIC;
  }

  async #stopProxy() {
    await lazy.IPPProxyManager.stop();
  }

  /**
   * Opens the help page in a new tab and closes the panel.
   *
   * @param {Event} e
   */
  static showHelpPage(e) {
    let win = e.target?.documentGlobal;
    if (win) {
      win.openWebLinkIn(
        Services.urlFormatter.formatURLPref("app.support.baseURL") +
          LINKS.SUPPORT_SLUG,
        "tab"
      );
    }

    let panelParent = e.target?.closest("panel");
    if (panelParent) {
      panelParent.hidePopup();
    }
  }

  #handleHeaderButtonKeypress(e) {
    if (e.code == "Space" || e.code == "Enter") {
      IPProtectionPanel.showHelpPage(e);
    }
  }

  /**
   * Updates the visibility of the panel components before they will shown.
   *
   * - If the panel component has already been created, updates the state.
   * - Creates a panel component if need, state will be updated on once it has
   *   been connected.
   *
   * @param {XULElement} panelView
   *   The panelView element from the CustomizableUI widget callback.
   */
  async showing(panelView) {
    const token = (this.#showToken = {});

    const isPremium = await this.isPremium();

    if (this.#showToken !== token) {
      return;
    }

    if (this.initiatedUpgrade) {
      lazy.IPProtectionService.authProvider.checkForUpgrade();
      this.initiatedUpgrade = false;
    }

    this.#updateSiteData();
    this.#updateProxyError();
    lazy.IPProtectionServerlist.maybeFetchList();

    if (this.state.paused) {
      lazy.IPPProxyManager.refreshUsage();
    }

    this.setState({
      isPremium,
      isSiteExceptionsEnabled: this.isExceptionsFeatureEnabled,
      isSiteInclusionsEnabled: this.isInclusionsFeatureEnabled,
      bandwidthWarning: this.#shouldShowBandwidthWarning(),
    });

    if (this.state.bandwidthWarning) {
      lazy.IPProtectionInfobarManager.hideInfobars({ triggeredByPanel: true });
    }

    if (this.panel) {
      this.updateState();
    } else {
      this.panel = panelView.closest("panel");
      this.#addPanelListeners(panelView.ownerDocument);
      let contentEl = this.#createPanel(
        panelView,
        IPProtectionPanel.CONTENT_TAGNAME
      );
      if (contentEl) {
        contentEl.dataset.capturesFocus = "true";
        this.#panelView = panelView;
        panelView.addEventListener("keydown", this.#panelKeyListener, {
          capture: true,
        });
      }
    }

    let hasUserEverOpenedPanel = Services.prefs.getBoolPref(USER_OPENED_PREF);
    if (!hasUserEverOpenedPanel) {
      Services.prefs.setBoolPref(USER_OPENED_PREF, true);
    }

    let openedPanelSince160 = Services.prefs.getBoolPref(
      USER_OPENED_SINCE_160_PREF
    );
    if (!openedPanelSince160) {
      Services.prefs.setBoolPref(USER_OPENED_SINCE_160_PREF, true);
    }

    // The function has fully completed, until now hiding() treats the panel
    // as never shown. Keep this last, there must be no early return or await
    // between the premium check and here.
    this.#showToken = null;
  }

  /**
   * Called when the panel elements will be hidden.
   *
   * Disables updates to the panel.
   */
  hiding() {
    if (this.#showToken) {
      // Hidden or cancelled before showing() completed, the panel was never
      // shown, so only clean up.
      this.destroy();
      return;
    }

    const mask = lazy.IPPOnboardingMessage.readPrefMask();
    const hasUsedSiteExceptions = !!(
      mask & ONBOARDING_PREF_FLAGS.EVER_USED_SITE_EXCEPTIONS
    );
    const browser = this.gBrowser.selectedBrowser;
    lazy.ASRouter.sendTriggerMessage({
      browser,
      id: "ipProtectionPanelClosed",
      context: {
        hasUsedSiteExceptions,
      },
    });

    this.destroy();
  }

  /**
   * Create a content element and header for a panel view.
   *
   * @param {MozBrowser} panelView
   * @param {string} contentTagName
   * @returns {Element|null} The newly-appended content element, or null if the
   *   content area was already populated.
   */
  #createPanel(panelView, contentTagName) {
    let { ownerDocument } = panelView;
    let contentArea = panelView.querySelector(".panel-subview-body");
    if (!contentArea || contentArea.children.length) {
      return null;
    }

    let headerButton = panelView.querySelector(".panel-info-button");
    if (headerButton) {
      headerButton.addEventListener("click", IPProtectionPanel.showHelpPage);
      headerButton.addEventListener(
        "keypress",
        this.#handleHeaderButtonKeypress
      );
      // Reset the tab index to ensure it is focusable.
      headerButton.setAttribute("tabindex", "0");
      this.#headerButtons.push(headerButton);
    }

    let contentEl = ownerDocument.createElement(contentTagName);
    contentArea.appendChild(contentEl);
    this.components.add(contentEl);
    return contentEl;
  }

  /**
   * Open the IP Protection panel in the given window.
   *
   * @param {Window} window - which window to open the panel in.
   * @returns {Promise<void>}
   */
  async open(window = this.#window.get()) {
    if (!lazy.IPProtection.created || !window?.PanelUI || this.active) {
      return;
    }

    let widget = lazy.CustomizableUI.getWidget(IPProtectionPanel.WIDGET_ID);
    let anchor = widget.forWindow(window).anchor;
    await window.PanelUI.showSubView(IPProtectionPanel.MAIN_PANELVIEW, anchor);
  }

  /**
   * Close the containing panel popup.
   */
  close() {
    this.panel?.hidePopup();
  }

  /**
   * Start flow for signing in and then opening the panel on success
   *
   * @param {object} options
   * @param {string} options.entrypoint
   *  The entrypoint to pass for the sign in flow
   * @param {string} options.utm_source
   *  The utm_source to pass for the sign in flow
   */
  async startLoginFlow({
    entrypoint = "vpn_integration_panel",
    utm_source = "panel",
  } = {}) {
    let window = this.#window.get();
    let browser = window.gBrowser;

    // Close the panel if the user will need to sign in.
    this.close();

    const signedIn = await lazy.SpecialMessageActions.fxaSignInFlow(
      {
        ...SIGNIN_DATA,
        entrypoint,
        extraParams: { ...SIGNIN_DATA.extraParams, utm_source },
      },
      browser
    );
    return signedIn;
  }

  /**
   * Ensure there is a signed in account and then open the panel after enrolling.
   *
   * @param {object} options
   */
  async enroll(options = {}) {
    Glean.ipprotection.getStarted.record();
    const signedIn = await this.startLoginFlow(options);
    if (!signedIn) {
      return;
    }

    // Temporarily set the main panel view to show if enrolling.
    this.setState({
      unauthenticated: false,
    });

    // Asynchronously enroll and entitle the user.
    // It will only need to finish before the proxy can start.
    const enrolling = lazy.IPProtectionService.authProvider.enroll();
    // Only auto-open the panel if the toolbar widget is placed in a visible
    // area.
    let placement = lazy.CustomizableUI.getPlacementOfWidget(
      IPProtectionPanel.WIDGET_ID
    );
    if (placement && !this.active) {
      await this.open();
    }
    const result = await enrolling;
    Glean.ipprotection.enrollment.record({
      enrolled: result?.isEnrolledAndEntitled,
      reason: result?.isEnrolledAndEntitled ? "" : (result?.error ?? ""),
    });
  }

  /**
   * Show the Locations subview and create its components if necessary.
   *
   * @param {boolean} [keyboardActivated]
   *   True if the subview was opened via keyboard, false if via mouse.
   * @param {HTMLElement|null} [locationButton]
   *   The button that opened the subview, to restore focus when the subview closes.
   */
  async showLocationSelector(keyboardActivated = false, locationButton = null) {
    let view = this.locationsView;
    if (!view) {
      return;
    }

    let viewShown = new Promise(resolve => {
      view.addEventListener("ViewShown", resolve, { once: true });
    });

    view.addEventListener(
      "ViewHiding",
      () => {
        view.removeEventListener("keydown", this.#locationsKeyListener, {
          capture: true,
        });
        if (this.#locationsClosedByKeyboard) {
          this.#locationsClosedByKeyboard = false;
          locationButton?.focus();
        }
      },
      { once: true }
    );

    await lazy.IPProtectionServerlist.maybeFetchList();
    this.panelMultiView?.showSubView(IPProtectionPanel.LOCATIONS_PANELVIEW);
    this.#createPanel(view, IPProtectionPanel.LOCATIONS_TAGNAME);

    await viewShown;

    // Allow back button, list items and promo button to manage focus and override
    // the PanelMultiView keydown listener.
    for (let el of view.querySelectorAll(
      ".subviewbutton-back, .location-item, moz-promo moz-button"
    )) {
      el.dataset.capturesFocus = "true";
    }

    // On keyboard activation, focus the list's tab stop. The roving tabindex is
    // reset to the selected option when the subview is shown, so entry lands on
    // the current selection.
    if (keyboardActivated) {
      const listTabStop =
        view.querySelector('.location-item[tabindex="0"]') ??
        view.querySelector(".location-item");
      listTabStop?.focus();
    }

    view.addEventListener("keydown", this.#locationsKeyListener, {
      capture: true,
    });
  }

  /**
   * Remove added elements and listeners.
   */
  destroy() {
    this.#showToken = null;

    if (!this.panel) {
      return;
    }

    this.#panelView?.removeEventListener("keydown", this.#panelKeyListener, {
      capture: true,
    });
    this.#panelView = null;

    for (let button of this.#headerButtons) {
      button.removeEventListener("click", IPProtectionPanel.showHelpPage);
      button.removeEventListener("keypress", this.#handleHeaderButtonKeypress);
    }
    this.#headerButtons = [];

    this.#removePanelListeners(this.panel.ownerDocument);

    for (let component of ChromeUtils.nondeterministicGetWeakSetKeys(
      this.components
    )) {
      component.remove();
      this.components.delete(component);
    }

    if (this.state.error) {
      this.setState({
        error: "",
      });
      this.toolbarButton?.updateState(null, { error: "" });
    }

    this.panel = null;
  }

  uninit() {
    this.destroy();
    this.#removeProxyListeners();
    this.#removeProgressListener();
    this.#removePrefObserver();
  }

  #addPanelListeners(doc) {
    doc.addEventListener("IPProtection:Init", this.handleEvent);
    doc.addEventListener("IPProtection:ClickUpgrade", this.handleEvent);
    doc.addEventListener("IPProtection:Close", this.handleEvent);
    doc.addEventListener("IPProtection:UserEnable", this.handleEvent);
    doc.addEventListener("IPProtection:UserDisable", this.handleEvent);
    doc.addEventListener("IPProtection:OptIn", this.handleEvent);
    doc.addEventListener("IPProtection:UserEnableVPNForSite", this.handleEvent);
    doc.addEventListener(
      "IPProtection:UserDisableVPNForSite",
      this.handleEvent
    );
    doc.addEventListener(
      "IPProtection:DismissBandwidthWarning",
      this.handleEvent
    );
    doc.addEventListener("IPProtection:UserShowLocations", this.handleEvent);
    doc.addEventListener("IPProtection:UserSelectLocation", this.handleEvent);
    doc.addEventListener(
      "IPProtection:DismissSiteInclusionPrompt",
      this.handleEvent
    );
  }

  #removePanelListeners(doc) {
    doc.removeEventListener("IPProtection:Init", this.handleEvent);
    doc.removeEventListener("IPProtection:ClickUpgrade", this.handleEvent);
    doc.removeEventListener("IPProtection:Close", this.handleEvent);
    doc.removeEventListener("IPProtection:UserEnable", this.handleEvent);
    doc.removeEventListener("IPProtection:UserDisable", this.handleEvent);
    doc.removeEventListener("IPProtection:OptIn", this.handleEvent);
    doc.removeEventListener(
      "IPProtection:UserEnableVPNForSite",
      this.handleEvent
    );
    doc.removeEventListener(
      "IPProtection:UserDisableVPNForSite",
      this.handleEvent
    );
    doc.removeEventListener(
      "IPProtection:DismissBandwidthWarning",
      this.handleEvent
    );
    doc.removeEventListener("IPProtection:UserShowLocations", this.handleEvent);
    doc.removeEventListener(
      "IPProtection:UserSelectLocation",
      this.handleEvent
    );
    doc.removeEventListener(
      "IPProtection:DismissSiteInclusionPrompt",
      this.handleEvent
    );
  }

  #addProxyListeners() {
    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:ModeChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:UsageChanged",
      this.handleEvent
    );
    lazy.IPPUsageHelper.addEventListener(
      "IPPUsageHelper:StateChanged",
      this.handleEvent
    );
    lazy.IPProtectionService.authProvider.addEventListener(
      "IPPAuthProvider:StateChanged",
      this.handleEvent
    );
    lazy.IPPSiteRuleManager.addEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleEvent
    );
    lazy.IPProtectionServerlist.addEventListener(
      "IPProtectionServerlist:ListChanged",
      this.handleEvent
    );
  }

  #removeProxyListeners() {
    lazy.IPProtectionService.authProvider.removeEventListener(
      "IPPAuthProvider:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:ModeChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:UsageChanged",
      this.handleEvent
    );
    lazy.IPPUsageHelper.removeEventListener(
      "IPPUsageHelper:StateChanged",
      this.handleEvent
    );
    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
    lazy.IPPSiteRuleManager.removeEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleEvent
    );
    lazy.IPProtectionServerlist.removeEventListener(
      "IPProtectionServerlist:ListChanged",
      this.handleEvent
    );
  }

  #shouldShowBandwidthWarning() {
    const state = lazy.IPPUsageHelper.state;
    let threshold = 0;
    if (state === "warning-75-percent") {
      threshold = 75;
    } else if (state === "warning-90-percent") {
      threshold = 90;
    } else {
      return false;
    }
    return lazy.IPPUsageHelper.getDismissedThresholds().panel < threshold;
  }

  #addProgressListener() {
    if (this.gBrowser) {
      this.gBrowser.addTabsProgressListener(this.progressListener);
      this.gBrowser.tabContainer.addEventListener(
        "TabSelect",
        this.handleEvent
      );
    }
  }

  #removeProgressListener() {
    if (this.gBrowser) {
      this.gBrowser.removeTabsProgressListener(this.progressListener);
      this.gBrowser.tabContainer.removeEventListener(
        "TabSelect",
        this.handleEvent
      );
    }
  }

  #addPrefObserver() {
    Services.prefs.addObserver(
      UPGRADE_NOT_AVAILABLE_PREF,
      this.handlePrefChange
    );
    Services.prefs.addObserver(EGRESS_LOCATION_PREF, this.handlePrefChange);
    Services.prefs.addObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.handlePrefChange
    );
  }

  #removePrefObserver() {
    Services.prefs.removeObserver(
      UPGRADE_NOT_AVAILABLE_PREF,
      this.handlePrefChange
    );
    Services.prefs.removeObserver(EGRESS_LOCATION_PREF, this.handlePrefChange);
    Services.prefs.removeObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.handlePrefChange
    );
  }

  #handlePrefChange(_subject, _topic, data) {
    switch (data) {
      case EGRESS_LOCATION_PREF:
        this.setState({
          location:
            Services.prefs.getStringPref(EGRESS_LOCATION_PREF, "") || null,
        });
        return;
      case BANDWIDTH_WARNING_DISMISSED_PREF:
        if (!this.#shouldShowBandwidthWarning()) {
          this.setState({ bandwidthWarning: false });
        }
        return;
      case UPGRADE_NOT_AVAILABLE_PREF:
        this.setState({
          upgradeNotAvailable: Services.prefs.getBoolPref(
            UPGRADE_NOT_AVAILABLE_PREF,
            false
          ),
        });
    }
  }

  /**
   * Gets siteData by reading the current URL bar's URI.
   *
   * @returns {object|null}
   *  An object with data relevant to a site (eg. isExclusion, hasSiteRule,
   *  host without a leading "www."), or null otherwise if invalid.
   *
   * @see State.siteData
   */

  #getSiteData() {
    const principal = getSitePrincipal(this.gBrowser);
    if (!principal || !lazy.IPPSiteRuleManager.canManage(principal)) {
      return null;
    }
    const rule = lazy.IPPSiteRuleManager.getRule(
      principal,
      this.gBrowser.selectedBrowser.browserId
    );
    const isExclusion = rule === lazy.IPPPrincipalRules.EXCLUDED;
    const isInclusion = rule === lazy.IPPPrincipalRules.INCLUDED;
    const hasSiteRule = rule !== lazy.IPPPrincipalRules.DEFAULT;
    const host = principal.host.replace(/^www\./, "");
    return { isExclusion, isInclusion, hasSiteRule, host };
  }

  /**
   * BigInts throw when using JSON.stringify or when using arithmetic with
   * numbers so we convert them to numbers here so they max and remaining can
   * be safely used. Check usageInfo first, if that doesn't exist, check the
   * entitlement. If neither exist, return null.
   *
   * @returns {object} An object with max and remaining as numbers
   */
  #getBandwidthUsage() {
    if (
      lazy.BANDWIDTH_USAGE_ENABLED &&
      lazy.IPPProxyManager.usageInfo?.max != null
    ) {
      return {
        max: Number(lazy.IPPProxyManager.usageInfo.max),
        remaining: Number(lazy.IPPProxyManager.usageInfo.remaining),
        reset: lazy.IPPProxyManager.usageInfo.reset,
      };
    } else if (
      lazy.BANDWIDTH_USAGE_ENABLED &&
      lazy.IPProtectionService.authProvider.maxBytes != null
    ) {
      // Usage info doesn't exist yet. Check the entitlement
      return {
        max: Number(lazy.IPProtectionService.authProvider.maxBytes),
        remaining: Number(lazy.IPProtectionService.authProvider.maxBytes),
        reset: null,
      };
    }

    return null;
  }

  /**
   * Whether the site inclusion prompt should be shown when the user turns on
   * the VPN: only the first time, and only on a site that isn't already
   * included.
   *
   * @returns {boolean}
   */
  #shouldShowSiteInclusionPrompt() {
    if (
      !this.isInclusionsFeatureEnabled ||
      Services.prefs.getBoolPref(SITE_INCLUSION_PROMPT_SHOWN_PREF, false)
    ) {
      return false;
    }
    const siteData = this.#getSiteData();
    return !!siteData && !siteData.isInclusion;
  }

  /**
   * Updates the siteData state property.
   */
  #updateSiteData() {
    const siteData = this.#getSiteData();
    this.setState({ siteData });
  }

  /**
   * The error to show for the proxy state. A failed connection that only
   * carries included sites is not shown on a tab it does not carry.
   *
   * @returns {string}
   */
  #getProxyErrorType() {
    if (lazy.IPPProxyManager.state !== lazy.IPPProxyStates.ERROR) {
      return "";
    }
    if (
      lazy.IPPProxyManager.mode === lazy.IPPMode.MODE_INCLUSION &&
      lazy.IPPSiteRuleManager.getRule(
        getSitePrincipal(this.gBrowser),
        this.gBrowser?.selectedBrowser.browserId
      ) !== lazy.IPPPrincipalRules.INCLUDED
    ) {
      return "";
    }
    return this.#errorMessage(lazy.IPPProxyManager.errorType);
  }

  /**
   * Re-evaluates a proxy state error for the selected tab. Errors from a
   * failed start in this panel are left alone.
   */
  #updateProxyError() {
    if (lazy.IPPProxyManager.state === lazy.IPPProxyStates.ERROR) {
      this.setState({ error: this.#getProxyErrorType() });
    }
  }

  #handleEvent(event) {
    if (event.type == "IPProtection:Init") {
      this.updateComponentState(event.target);
    } else if (event.type == "IPProtection:Close") {
      this.close();
    } else if (event.type == "IPProtection:UserEnable") {
      this.#startProxy();
      Services.prefs.setBoolPref("browser.ipProtection.userEnabled", true);
      let userEnableCount = Services.prefs.getIntPref(
        "browser.ipProtection.userEnableCount",
        0
      );
      if (userEnableCount < 3) {
        Services.prefs.setIntPref(
          "browser.ipProtection.userEnableCount",
          userEnableCount + 1
        );
      }
      if (this.#shouldShowSiteInclusionPrompt()) {
        Services.prefs.setBoolPref(SITE_INCLUSION_PROMPT_SHOWN_PREF, true);
        this.setState({ showSiteInclusionPrompt: true });
      }
    } else if (event.type == "IPProtection:UserDisable") {
      this.#stopProxy();
      Services.prefs.setBoolPref("browser.ipProtection.userEnabled", false);
      this.setState({ showSiteInclusionPrompt: false });
    } else if (event.type == "IPProtection:DismissSiteInclusionPrompt") {
      this.setState({ showSiteInclusionPrompt: false });
    } else if (event.type == "IPProtection:ClickUpgrade") {
      // Let the service know that we tried upgrading at least once
      this.initiatedUpgrade = true;
      this.close();
    } else if (event.type == "IPProtection:OptIn") {
      this.enroll({ entrypoint: "vpn_integration_panel" });
    } else if (
      event.type == "IPPProxyManager:StateChanged" ||
      event.type == "IPPProxyManager:ModeChanged" ||
      event.type == "IPProtectionService:StateChanged" ||
      event.type === "IPPAuthProvider:StateChanged"
    ) {
      const errorType = this.#getProxyErrorType();
      const paused = lazy.IPPProxyManager.state === lazy.IPPProxyStates.PAUSED;

      this.setState({
        unauthenticated:
          lazy.IPProtectionService.state ===
          lazy.IPProtectionStates.UNAUTHENTICATED,
        isProtectionEnabled: lazy.IPPProxyManager.isDefaultModeActive,
        hasUpgraded: lazy.IPProtectionService.authProvider.hasUpgraded,
        error: errorType,
        isActivating:
          lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVATING,
        isActive: lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE,
        isEnrolling: lazy.IPProtectionService.authProvider.isEnrolling,
        bandwidthUsage: this.#getBandwidthUsage(),
        bandwidthWarning:
          lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
            ? this.#shouldShowBandwidthWarning()
            : false,
        paused,
        showSiteInclusionPrompt:
          this.state.showSiteInclusionPrompt && !errorType && !paused,
      });
    } else if (
      event.type == "SiteRuleManager:RuleChanged" ||
      event.type == "TabSelect"
    ) {
      this.#updateSiteData();
      this.#updateProxyError();
    } else if (event.type == "IPProtectionServerlist:ListChanged") {
      this.setState({
        locationsList: lazy.IPProtectionServerlist.countries,
      });
    } else if (event.type == "IPProtection:UserEnableVPNForSite") {
      const win = event.target.documentGlobal;
      const principal = getSitePrincipal(win?.gBrowser);

      lazy.IPPPermissionRules.setRule(
        principal,
        lazy.IPPPrincipalRules.DEFAULT
      );
      Glean.ipprotection.exclusionToggled.record({ excluded: false });
    } else if (event.type == "IPProtection:UserDisableVPNForSite") {
      const win = event.target.documentGlobal;
      const principal = getSitePrincipal(win?.gBrowser);

      lazy.IPPPermissionRules.setRule(
        principal,
        lazy.IPPPrincipalRules.EXCLUDED
      );
      Glean.ipprotection.exclusionToggled.record({ excluded: true });
    } else if (event.type == "IPProtection:DismissBandwidthWarning") {
      const state = lazy.IPPUsageHelper.state;
      let threshold = 0;
      if (state === "warning-75-percent") {
        threshold = 75;
      } else if (state === "warning-90-percent") {
        threshold = 90;
      }
      if (threshold > 0) {
        const current = lazy.IPPUsageHelper.getDismissedThresholds();
        if (threshold > current.panel) {
          lazy.IPPUsageHelper.setDismissedThresholds({
            ...current,
            panel: threshold,
          });
        }
      }
      this.setState({ bandwidthWarning: false });
    } else if (event.type == "IPPProxyManager:UsageChanged") {
      const usage = event.detail.usage;
      if (!usage) {
        return;
      }

      if (usage.unlimited) {
        Services.prefs.clearUserPref(BANDWIDTH_THRESHOLD_PREF);
        Services.prefs.clearUserPref(BANDWIDTH_RESET_DATE_PREF);
        this.setState({ bandwidthUsage: null, bandwidthWarning: false });
        return;
      }

      if (usage.max == null || usage.remaining == null || !usage.reset) {
        return;
      }

      const remainingPercent = Number(usage.remaining) / Number(usage.max);
      const upsellThreshold = (1 - BANDWIDTH.FIRST_THRESHOLD) * 100;
      const firstWarning = (1 - BANDWIDTH.SECOND_THRESHOLD) * 100;
      const secondWarning = (1 - BANDWIDTH.THIRD_THRESHOLD) * 100;

      let threshold = 0;
      if (
        remainingPercent <= BANDWIDTH.FIRST_THRESHOLD &&
        remainingPercent > BANDWIDTH.SECOND_THRESHOLD
      ) {
        threshold = upsellThreshold;
      } else if (
        remainingPercent <= BANDWIDTH.SECOND_THRESHOLD &&
        remainingPercent > BANDWIDTH.THIRD_THRESHOLD
      ) {
        threshold = firstWarning;
      } else if (
        remainingPercent > 0 &&
        remainingPercent <= BANDWIDTH.THIRD_THRESHOLD
      ) {
        threshold = secondWarning;
      } else if (remainingPercent === 0) {
        threshold = 100;
      }

      const lastRecordedThreshold = Services.prefs.getIntPref(
        BANDWIDTH_THRESHOLD_PREF,
        threshold
      );
      Services.prefs.setIntPref(BANDWIDTH_THRESHOLD_PREF, threshold);

      if (lastRecordedThreshold !== threshold) {
        this.#measureBandwidthThreshold(threshold, lastRecordedThreshold);
      }

      const resetDate = usage.reset.toString();
      const lastResetDate = Services.prefs.getStringPref(
        BANDWIDTH_RESET_DATE_PREF,
        ""
      );
      Services.prefs.setStringPref(BANDWIDTH_RESET_DATE_PREF, resetDate);

      if (threshold === 0 && lastResetDate && resetDate !== lastResetDate) {
        this.#sendBandwidthResetTrigger();
      }

      this.setState({
        bandwidthUsage: {
          remaining: Number(usage.remaining),
          max: Number(usage.max),
          reset: usage.reset,
        },
      });
    } else if (event.type == "IPPUsageHelper:StateChanged") {
      this.setState({ bandwidthWarning: this.#shouldShowBandwidthWarning() });
    } else if (event.type == "IPProtection:UserShowLocations") {
      Glean.ipprotection.locationSelectorButtonClicked.record();
      this.showLocationSelector(
        event.detail?.keyboardActivated,
        event.detail?.locationButton
      );
    } else if (event.type == "IPProtection:UserSelectLocation") {
      const { code } = event.detail;
      Glean.ipprotection.locationChanged.record({ location: code });
      Services.prefs.setStringPref(EGRESS_LOCATION_PREF, code);
      if (lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE) {
        lazy.IPPProxyManager.switch(code === "REC" ? undefined : code);
      }
      this.panelMultiView?.goBack();
    }
  }

  async #sendBandwidthResetTrigger() {
    await lazy.ASRouter.waitForInitialized;
    const win = Services.wm.getMostRecentBrowserWindow();
    const browser = win?.gBrowser?.selectedBrowser;
    await lazy.ASRouter.sendTriggerMessage({
      browser,
      id: "ipProtectionBandwidthReset",
    });
  }

  #measureBandwidthThreshold(threshold, lastRecordedThreshold) {
    if (!threshold || threshold == lastRecordedThreshold) {
      return;
    }

    Glean.ipprotection.bandwidthUsedThreshold.record({
      percentage: threshold,
    });
  }
}
