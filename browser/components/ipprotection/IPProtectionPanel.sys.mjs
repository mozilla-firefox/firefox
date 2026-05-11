/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  IPPExceptionsManager:
    "moz-src:///toolkit/components/ipprotection/IPPExceptionsManager.sys.mjs",
  IPPOnboardingMessage:
    "moz-src:///browser/components/ipprotection/IPPOnboardingMessageHelper.sys.mjs",
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
});
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import {
  BANDWIDTH,
  ONBOARDING_PREF_FLAGS,
  LINKS,
  SIGNIN_DATA,
} from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";

const BANDWIDTH_THRESHOLD_PREF = "browser.ipProtection.bandwidthThreshold";
const BANDWIDTH_WARNING_DISMISSED_PREF =
  "browser.ipProtection.bandwidthWarningDismissedThreshold";
const BANDWIDTH_RESET_DATE_PREF = "browser.ipProtection.bandwidthResetDate";
const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";
const USER_OPENED_PREF = "browser.ipProtection.everOpenedPanel";
const OPENED_WITH_LOCATION_PREF =
  "browser.ipProtection.openedPanelWithLocation";
const LOCATION_BADGE_DISMISSED_PREF =
  "browser.ipProtection.locationButtonBadgeDismissed";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "BANDWIDTH_USAGE_ENABLED",
  "browser.ipProtection.bandwidth.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "EGRESS_LOCATION",
  EGRESS_LOCATION_PREF,
  ""
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
        async: true,
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
   * @property {"generic-error" | "network-error" | ""} error
   *  The error type as a string if an error occurred, or empty string if there are no errors.
   * @property {boolean} hasUpgraded
   *  True if a Mozilla VPN subscription is linked to the user's Mozilla account.
   * @property {string} onboardingMessage
   * Continuous onboarding message to display in-panel, empty string if none applicable
   * @property {boolean} paused
   * True if the VPN service has been paused due to bandwidth limits
   * @property {boolean} isSiteExceptionsEnabled
   * True if site exceptions support is enabled, else false.
   * @property {object} siteData
   * Data about the currently loaded site, including "isExclusion".
   * @property {object} bandwidthUsage
   *  An object containing the current and max usage
   * @property {boolean} isActivating
   *  True if the VPN service is in the process of connecting, else false.
   * @property {boolean} showLocationButtonBadge
   *  True if the "new" badge on the location selection button should be visible.
   */

  /**
   * @type {State}
   */
  state = {};
  panel = null;
  components = new WeakSet();
  initiatedUpgrade = false;
  #window = null;
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
      ? Array.from(
          locationsList.querySelectorAll(".location-item:not([disabled])")
        )
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
      return;
    }

    if (!view.contains(focused)) {
      return;
    }

    const isOnListItemForFocus = listItems.includes(focused);

    // Tab key handling
    const tabOnlyElements = [backButton, listItems[0], promoButton].filter(
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
      isProtectionEnabled:
        lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE,
      location: lazy.EGRESS_LOCATION || null,
      locationsList: lazy.IPProtectionServerlist.countries,
      error: "",
      hasUpgraded: lazy.IPProtectionService.authProvider.hasUpgraded,
      onboardingMessage: "",
      bandwidthWarning: false,
      paused: lazy.IPPProxyManager.state === lazy.IPPProxyStates.PAUSED,
      isSiteExceptionsEnabled: this.isExceptionsFeatureEnabled,
      siteData: this.#getSiteData(),
      bandwidthUsage: this.#getBandwidthUsage(),
      isActivating:
        lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVATING,
      isEnrolling: lazy.IPProtectionService.authProvider.isEnrolling,
      showLocationButtonBadge: !Services.prefs.getBoolPref(
        LOCATION_BADGE_DISMISSED_PREF,
        false
      ),
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
    const { error } = await lazy.IPPProxyManager.start(
      true,
      inPrivateBrowsing,
      this.state.location
    );
    if (error && error !== lazy.ERRORS.CANCELED) {
      const errorMessage =
        error == lazy.ERRORS.NETWORK
          ? lazy.ERRORS.NETWORK
          : lazy.ERRORS.GENERIC;
      this.setState({
        error: errorMessage,
      });
      this.toolbarButton?.updateState(null, { error: errorMessage });
    }
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
  showing(panelView) {
    if (this.initiatedUpgrade) {
      lazy.IPProtectionService.authProvider.checkForUpgrade();
      this.initiatedUpgrade = false;
    }

    this.#updateSiteData();

    this.setState({
      isSiteExceptionsEnabled: this.isExceptionsFeatureEnabled,
      bandwidthWarning: this.#shouldShowBandwidthWarning(),
    });

    if (this.state.bandwidthWarning) {
      lazy.IPProtectionInfobarManager.hideInfobars();
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

    let hasOpenedPanelWithLocation = Services.prefs.getBoolPref(
      OPENED_WITH_LOCATION_PREF
    );
    if (!hasOpenedPanelWithLocation) {
      Services.prefs.setBoolPref(OPENED_WITH_LOCATION_PREF, true);
    }
  }

  /**
   * Called when the panel elements will be hidden.
   *
   * Disables updates to the panel.
   */
  hiding() {
    if (this.state.showLocationButtonBadge) {
      Services.prefs.setBoolPref(LOCATION_BADGE_DISMISSED_PREF, true);
      this.state.showLocationButtonBadge = false;
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
    if (!this.active) {
      await this.open();
    }
    const result = await enrolling;
    Glean.ipprotection.enrollment.record({
      enrolled: result?.isEnrolledAndEntitled,
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

    // On keyboard activation, focus the first list item
    if (keyboardActivated) {
      view.querySelector(".location-item:not([disabled])")?.focus();
    }

    view.addEventListener("keydown", this.#locationsKeyListener, {
      capture: true,
    });
  }

  /**
   * Remove added elements and listeners.
   */
  destroy() {
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
    lazy.IPPExceptionsManager.addEventListener(
      "IPPExceptionsManager:ExclusionChanged",
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
    lazy.IPPExceptionsManager.removeEventListener(
      "IPPExceptionsManager:ExclusionChanged",
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
    }
  }

  #removeProgressListener() {
    if (this.gBrowser) {
      this.gBrowser.removeTabsProgressListener(this.progressListener);
    }
  }

  #addPrefObserver() {
    Services.prefs.addObserver(EGRESS_LOCATION_PREF, this.handlePrefChange);
    Services.prefs.addObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.handlePrefChange
    );
  }

  #removePrefObserver() {
    Services.prefs.removeObserver(EGRESS_LOCATION_PREF, this.handlePrefChange);
    Services.prefs.removeObserver(
      BANDWIDTH_WARNING_DISMISSED_PREF,
      this.handlePrefChange
    );
  }

  #handlePrefChange(_subject, _topic, data) {
    if (data === EGRESS_LOCATION_PREF) {
      const value = Services.prefs.getStringPref(EGRESS_LOCATION_PREF, "");
      this.setState({
        location: value || null,
      });
    } else if (data === BANDWIDTH_WARNING_DISMISSED_PREF) {
      if (!this.#shouldShowBandwidthWarning()) {
        this.setState({ bandwidthWarning: false });
      }
    }
  }

  /**
   * Gets siteData by reading the current content principal.
   *
   * @returns {object|null}
   *  An object with data relevant to a site (eg. isExclusion),
   *  or null otherwise if invalid.
   *
   * @see State.siteData
   */

  #getSiteData() {
    const principal = this.gBrowser?.contentPrincipal;

    if (!principal) {
      return null;
    }

    const isExclusion = lazy.IPPExceptionsManager.hasExclusion(principal);
    const isPrivileged = this._isPrivilegedPage(principal);

    let siteData = !isPrivileged ? { isExclusion } : null;
    return siteData;
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
   * Checks if the given principal represents a privileged page.
   *
   * @param {nsIPrincipal} principal
   *  The principal to evaluate.
   * @returns {boolean}
   *  True if the page is privileged (about: pages or system principal).
   */
  _isPrivilegedPage(principal) {
    // Ignore about: pages for automated tests, which load in about:blank pages by default.
    // Do not register this method as private though so that we can stub it.
    return (
      (principal.schemeIs("about") || principal.isSystemPrincipal) &&
      !Cu.isInAutomation
    );
  }

  /**
   * Updates the siteData state property.
   */
  #updateSiteData() {
    const siteData = this.#getSiteData();
    this.setState({ siteData });
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
    } else if (event.type == "IPProtection:UserDisable") {
      this.#stopProxy();
      Services.prefs.setBoolPref("browser.ipProtection.userEnabled", false);
    } else if (event.type == "IPProtection:ClickUpgrade") {
      // Let the service know that we tried upgrading at least once
      this.initiatedUpgrade = true;
      this.close();
    } else if (event.type == "IPProtection:OptIn") {
      this.enroll({ entrypoint: "vpn_integration_panel" });
    } else if (
      event.type == "IPPProxyManager:StateChanged" ||
      event.type == "IPProtectionService:StateChanged" ||
      event.type === "IPPAuthProvider:StateChanged"
    ) {
      let errorType = "";
      if (lazy.IPPProxyManager.state === lazy.IPPProxyStates.ERROR) {
        errorType = lazy.ERRORS.GENERIC;
      }

      this.setState({
        unauthenticated:
          lazy.IPProtectionService.state ===
          lazy.IPProtectionStates.UNAUTHENTICATED,
        isProtectionEnabled:
          lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE,
        hasUpgraded: lazy.IPProtectionService.authProvider.hasUpgraded,
        error: errorType,
        isActivating:
          lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVATING,
        isEnrolling: lazy.IPProtectionService.authProvider.isEnrolling,
        bandwidthUsage: this.#getBandwidthUsage(),
        bandwidthWarning:
          lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
            ? this.#shouldShowBandwidthWarning()
            : false,
        paused: lazy.IPPProxyManager.state === lazy.IPPProxyStates.PAUSED,
      });
    } else if (event.type == "IPPExceptionsManager:ExclusionChanged") {
      this.#updateSiteData();
    } else if (event.type == "IPProtectionServerlist:ListChanged") {
      this.setState({
        locationsList: lazy.IPProtectionServerlist.countries,
      });
    } else if (event.type == "IPProtection:UserEnableVPNForSite") {
      const win = event.target.documentGlobal;
      const principal = win?.gBrowser.contentPrincipal;

      lazy.IPPExceptionsManager.setExclusion(principal, false);
      Glean.ipprotection.exclusionToggled.record({ excluded: false });
    } else if (event.type == "IPProtection:UserDisableVPNForSite") {
      const win = event.target.documentGlobal;
      const principal = win?.gBrowser.contentPrincipal;

      lazy.IPPExceptionsManager.setExclusion(principal, true);
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
      if (
        !usage ||
        usage.max == null ||
        usage.remaining == null ||
        !usage.reset
      ) {
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

      if (lazy.BANDWIDTH_USAGE_ENABLED) {
        this.setState({
          bandwidthUsage: {
            remaining: Number(usage.remaining),
            max: Number(usage.max),
            reset: usage.reset,
          },
        });
      }
    } else if (event.type == "IPPUsageHelper:StateChanged") {
      this.setState({ bandwidthWarning: this.#shouldShowBandwidthWarning() });
    } else if (event.type == "IPProtection:UserShowLocations") {
      if (this.state.showLocationButtonBadge) {
        Services.prefs.setBoolPref(LOCATION_BADGE_DISMISSED_PREF, true);
        this.setState({ showLocationButtonBadge: false });
      }
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
