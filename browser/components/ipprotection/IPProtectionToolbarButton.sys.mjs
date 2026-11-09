/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  IPPMode:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  IPPPrincipalRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPSiteRuleManager:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  ERRORS: "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
});

import { getSitePrincipal } from "chrome://browser/content/ipprotection/ipprotection-utils.mjs";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "siteExceptionsFeaturePref",
  "browser.ipProtection.features.siteExceptions",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "siteInclusionsFeaturePref",
  "browser.ipProtection.features.siteInclusions",
  false
);

/**
 * IPProtectionToolbarButton manages the IP Protection toolbar button
 * for a single browser window.
 *
 * Each instance:
 * - Tracks location changes via a progress listener
 * - Updates the button icon according to the proxy state, proxy errors,
 *  offline status, and site exclusions
 * - Handles the visual state of the toolbar button
 */
export class IPProtectionToolbarButton {
  #window = null;
  #progressListener = null;
  #widgetId = null;
  #previousIsExcluded = null;
  #previousIsIncluded = null;
  #visitedExcludedSites = new Set();
  #visitedIncludedSites = new Set();
  // Origin of an included site navigated to while the proxy was activating,
  // so the hint can be shown once the connection is active.
  #pendingIncludedSiteOrigin = null;

  static EXCLUDED_CONFIRMATION_HINT_MESSAGE_ID =
    "confirmation-hint-ipprotection-navigated-to-excluded-site";
  static INCLUDED_CONFIRMATION_HINT_MESSAGE_ID =
    "confirmation-hint-ipprotection-navigated-to-included-site";

  // Non-default icon states rendered as always-painted overlay layers in the
  // toolbar so switching states never triggers a fresh image decode
  // (bug 2034698). The default "off" state is the base .toolbarbutton-icon.
  static ICON_LAYER_STATES = [
    "on",
    "network-error",
    "error",
    "excluded",
    "included",
    "paused",
  ];

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
   * Gets the value of the pref
   * browser.ipProtection.features.siteExceptions.
   *
   * @returns {boolean}
   *  True if site exceptions support is enabled, false otherwise.
   */
  get isExceptionsFeatureEnabled() {
    return lazy.siteExceptionsFeaturePref;
  }

  /**
   * Gets the value of the pref
   * browser.ipProtection.features.siteInclusions.
   *
   * @returns {boolean}
   *  True if site inclusions support is enabled, false otherwise.
   */
  get isInclusionsFeatureEnabled() {
    return lazy.siteInclusionsFeaturePref;
  }

  /**
   * Gets the toolbaritem for this window.
   *
   * @returns {XULElement|null}
   *  The toolbaritem element, or null if not available.
   */
  get toolbaritem() {
    const win = this.#window.get();
    if (!win) {
      return null;
    }

    return lazy.CustomizableUI.getWidget(this.#widgetId)?.forWindow(win).node;
  }

  constructor(window, widgetId, toolbaritem = null) {
    this.#window = Cu.getWeakReference(window);
    this.#widgetId = widgetId;
    this.handleEvent = this.#handleEvent.bind(this);

    this.#addProgressListener();
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
    lazy.IPPSiteRuleManager.addEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleEvent
    );

    if (this.gBrowser?.tabContainer) {
      this.gBrowser.tabContainer.addEventListener("TabSelect", this);
    }

    if (toolbaritem) {
      toolbaritem.classList.add("subviewbutton-nav"); // adds the right arrow in overflow menu
      this.updateState(toolbaritem);
    }
  }

  /**
   * Creates and registers a progress listener for the window.
   */
  #addProgressListener() {
    if (!this.gBrowser) {
      return;
    }

    this.#progressListener = {
      onLocationChange: (
        aBrowser,
        aWebProgress,
        _aRequest,
        aLocationURI,
        aFlags
      ) => {
        if (!aWebProgress.isTopLevel) {
          return;
        }

        // Only update if on the currently selected tab
        if (aBrowser !== this.gBrowser?.selectedBrowser) {
          return;
        }

        if (!aLocationURI) {
          return;
        }

        const isReload =
          aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD;

        this.updateState(null, {
          showConfirmationHint: !isReload,
          isNavigation: true,
        });
      },
    };

    this.gBrowser.addTabsProgressListener(this.#progressListener);
  }

  /**
   * Event handler for document-level events.
   *
   * @param {Event} event
   *  The event to handle.
   */
  #handleEvent(event) {
    if (
      event.type !== "IPProtectionService:StateChanged" &&
      event.type !== "IPPProxyManager:StateChanged" &&
      event.type !== "IPPProxyManager:ModeChanged" &&
      event.type !== "SiteRuleManager:RuleChanged" &&
      event.type !== "TabSelect"
    ) {
      return;
    }

    let ruleChanged = event.type === "SiteRuleManager:RuleChanged";

    if (
      event.type === "IPPProxyManager:StateChanged" &&
      lazy.IPPProxyManager.state !== lazy.IPPProxyStates.ACTIVE
    ) {
      this.#visitedExcludedSites.clear();
      this.#visitedIncludedSites.clear();
    }

    this.updateState(null, {
      showConfirmationHint: !ruleChanged,
      isNavigation: event.type === "TabSelect",
    });
  }

  /**
   * Updates the button to reflect the current state.
   *
   * This method is called under these circumstances:
   * 1. After creating the toolbar button, to set up the initial icon
   * 2. After an IPProtectionService or IPPProxyManager state change
   * 3. After pressing the site exclusion toggle on the panel and the
   *    exclusion state for a site has changed in ipp-vpn
   * 4. After a location change / page navigation
   * 5. After tab switching
   * 7. After an IPPProxyManager error event occurs
   * 8. The panel opens or closes.
   *
   * @param {XULElement|null} [toolbaritem]
   *  Optional toolbaritem to update directly.
   *  If not provided, looks up the toolbaritem via CustomizableUI.
   *  If provided, but toolbaritem is null, this means the toolbaritem isn't available yet.
   * @param {object} [options]
   *  Optional options object
   * @param {boolean} [options.showConfirmationHint=true]
   *  Whether to show confirmation hints for navigation to excluded or
   *  included sites
   * @param {boolean} [options.isNavigation=false]
   *  Whether the update is caused by a location change or tab switch
   * @param {string} [options.error=undefined]
   *  Error type to show.
   */
  updateState(
    toolbaritem = null,
    options = { showConfirmationHint: true, error: undefined }
  ) {
    const win = this.#window.get();
    if (!win) {
      return;
    }

    toolbaritem ??= this.toolbaritem;

    if (!toolbaritem) {
      return;
    }

    let principal = getSitePrincipal(this.gBrowser);
    let browserId = this.gBrowser.selectedBrowser.browserId;
    // Only surface an exclusion for pages the user can manage (normal content
    // pages), matching the panel: about:/chrome:/system pages are never shown
    // excluded.
    let isExcluded =
      !!principal &&
      lazy.IPPSiteRuleManager.canManage(principal) &&
      lazy.IPPSiteRuleManager.getRule(principal, browserId) ===
        lazy.IPPPrincipalRules.EXCLUDED;
    let isIncluded =
      !!principal &&
      lazy.IPPSiteRuleManager.canManage(principal) &&
      lazy.IPPSiteRuleManager.getRule(principal, browserId) ===
        lazy.IPPPrincipalRules.INCLUDED;
    let isActive = lazy.IPPProxyManager.isDefaultModeActive;
    let isProxyActive =
      lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVE;
    let isProxyActivating =
      lazy.IPPProxyManager.state === lazy.IPPProxyStates.ACTIVATING;
    let isPaused = lazy.IPPProxyManager.state === lazy.IPPProxyStates.PAUSED;

    // Show error icon when proxy manager is in ERROR state, unless the
    // connection only carries included sites and this tab is not one of them.
    let hasProxyError =
      lazy.IPPProxyManager.state === lazy.IPPProxyStates.ERROR &&
      (lazy.IPPProxyManager.mode !== lazy.IPPMode.MODE_INCLUSION ||
        lazy.IPPSiteRuleManager.getRule(principal, browserId) ===
          lazy.IPPPrincipalRules.INCLUDED);

    let isNetworkError =
      options?.error === lazy.ERRORS.NETWORK ||
      (hasProxyError && lazy.IPPProxyManager.errorType === lazy.ERRORS.NETWORK);

    let isError = hasProxyError || !!options.error;

    const showConfirmationHint = options.showConfirmationHint ?? true;
    if (showConfirmationHint) {
      this.updateConfirmationHint(win.ConfirmationHint, toolbaritem, {
        isActive,
        isProxyActive,
        isProxyActivating,
        isNavigation: !!options.isNavigation,
        isError,
        isExcluded,
        isIncluded,
      });
    }

    // Null principals reset the previous state to false if
    // the state was initially true. To avoid this, only set
    // the previous state if not a null principal.
    if (principal && !principal.isNullPrincipal) {
      this.#previousIsExcluded = isExcluded;
      this.#previousIsIncluded = isIncluded;
    }

    this.updateIconStatus(toolbaritem, {
      isActive,
      isProxyActive,
      isError,
      isNetworkError,
      isExcluded,
      isIncluded: isIncluded && isProxyActive,
      isPaused,
    });

    this.#updateBadge(toolbaritem);
  }

  /**
   * Updates the badge on the toolbar button based on whether the user has
   * opened the panel since location controls were introduced.
   * The badge is not shown when the button is in the customize toolbar palette.
   *
   * @param {XULElement|null} [toolbaritem]
   */
  #updateBadge(toolbaritem = null) {
    toolbaritem ??= this.toolbaritem;

    if (!toolbaritem) {
      return;
    }

    // Disabling notification until there is a new feature- Bug 2057313
    let newFeatureRelease = false;

    let inPalette = !lazy.CustomizableUI.getPlacementOfWidget(this.#widgetId);

    let badge = toolbaritem.querySelector(".toolbarbutton-badge");

    if (!newFeatureRelease || inPalette) {
      toolbaritem.removeAttribute("badged");
      badge?.classList.remove("feature-callout");
    } else {
      toolbaritem.setAttribute("badged", "true");
      badge?.classList.add("feature-callout");
    }
  }

  /**
   * Shows a confirmation hint after navigating to a site with a site rule:
   * - from a protected site to an excluded site while the VPN is on.
   * - from an unprotected site to an included site while the VPN is off
   *   and the proxy is connected for included sites only. If the proxy is
   *   still activating during a navigation, the hint is deferred until it is
   *   active.
   * Ignore the message if there is an error, or if we already showed the
   * message for a site during the VPN session.
   *
   * @param {object} confirmationHint
   *  The current window's confirmation hint instance
   * @param {XULElement} toolbaritem
   *  The toolbaritem to anchor the confirmation hint to
   * @param {object} status
   *  VPN connection status
   */
  updateConfirmationHint(
    confirmationHint,
    toolbaritem,
    status = {
      isActive: false,
      isProxyActive: false,
      isProxyActivating: false,
      isNavigation: false,
      isError: false,
      isExcluded: false,
      isIncluded: false,
    }
  ) {
    let siteOrigin = getSitePrincipal(this.gBrowser)?.origin;
    let isPendingIncludedSite =
      !!siteOrigin && this.#pendingIncludedSiteOrigin === siteOrigin;
    this.#pendingIncludedSiteOrigin = null;

    if (!confirmationHint || status.isError || !siteOrigin) {
      return;
    }

    let messageId;
    let visitedSites;
    let hideCheckmark;
    if (
      this.isExceptionsFeatureEnabled &&
      status.isActive &&
      status.isExcluded &&
      !this.#previousIsExcluded
    ) {
      messageId =
        IPProtectionToolbarButton.EXCLUDED_CONFIRMATION_HINT_MESSAGE_ID;
      visitedSites = this.#visitedExcludedSites;
      hideCheckmark = true;
    } else if (
      this.isInclusionsFeatureEnabled &&
      !status.isActive &&
      status.isIncluded &&
      (!this.#previousIsIncluded || isPendingIncludedSite)
    ) {
      if (status.isProxyActivating) {
        if (status.isNavigation || isPendingIncludedSite) {
          this.#pendingIncludedSiteOrigin = siteOrigin;
        }
        return;
      }
      if (!status.isProxyActive) {
        return;
      }
      messageId =
        IPProtectionToolbarButton.INCLUDED_CONFIRMATION_HINT_MESSAGE_ID;
      visitedSites = this.#visitedIncludedSites;
      hideCheckmark = false;
    } else {
      return;
    }

    if (visitedSites.has(siteOrigin)) {
      return;
    }

    visitedSites.add(siteOrigin);
    confirmationHint.show(toolbaritem, messageId, {
      position: "bottomright topright", // panel anchor, message anchor
      hideCheckmark,
    });
  }

  /**
   * Updates the toolbar button icon to reflect the VPN connection status
   *
   * @param {XULElement} toolbaritem
   *  The toolbaritem to update
   * @param {object} status
   *  VPN connection status. isActive is true when the VPN is on for all
   *  sites, isProxyActive whenever the proxy is connected in any mode.
   */
  updateIconStatus(
    toolbaritem,
    status = {
      isActive: false,
      isProxyActive: false,
      isError: false,
      isExcluded: false,
      isIncluded: false,
      isPaused: false,
      isNetworkError: false,
    }
  ) {
    if (!toolbaritem) {
      return;
    }

    this.#buildIconLayers(toolbaritem);

    let isActive = status.isActive;
    let isNetworkError = status.isNetworkError;
    let isError = status.isError && !isNetworkError;
    let isExcluded = status.isExcluded && this.isExceptionsFeatureEnabled;
    let isIncluded =
      status.isIncluded &&
      status.isProxyActive &&
      this.isInclusionsFeatureEnabled;
    let isPaused = status.isPaused;
    let l10nId =
      isError || isNetworkError
        ? "ipprotection-button-error"
        : "ipprotection-button";

    toolbaritem.classList.remove(
      "ipprotection-on",
      "ipprotection-network-error",
      "ipprotection-error",
      "ipprotection-excluded",
      "ipprotection-included",
      "ipprotection-paused"
    );

    if (isNetworkError) {
      toolbaritem.classList.add("ipprotection-network-error");
    } else if (isError) {
      toolbaritem.classList.add("ipprotection-error");
    } else if (isPaused) {
      toolbaritem.classList.add("ipprotection-paused");
    } else if (isExcluded && isActive) {
      toolbaritem.classList.add("ipprotection-excluded");
    } else if (isIncluded && !isActive) {
      toolbaritem.classList.add("ipprotection-included");
    } else if (isActive) {
      toolbaritem.classList.add("ipprotection-on");
    }

    toolbaritem.setAttribute("data-l10n-id", l10nId);
  }

  /**
   * Wraps the toolbar button's icon in a <stack> and renders one overlay
   * <image> layer per non-default icon state on top of it. Keeps every
   * state's artwork painted and updates opacity via CSS.
   * This approach prevents flickers between initial state changes since
   * there is no fresh image decode - Bug 2034698.
   *
   * @param {XULElement} toolbaritem
   *  The toolbaritem to add the icon layers to.
   */
  #buildIconLayers(toolbaritem) {
    if (toolbaritem.querySelector(".ipprotection-icon-stack")) {
      return;
    }

    let icon = toolbaritem.querySelector(".toolbarbutton-icon");
    if (!icon) {
      // The button may not be connected yet, so force render() to create its
      // children before building stack
      toolbaritem.render();
      icon = toolbaritem.querySelector(".toolbarbutton-icon");
    }

    let doc = toolbaritem.ownerDocument;
    let stack = doc.createXULElement("stack");
    stack.classList.add("ipprotection-icon-stack", "toolbarbutton-badge-stack");

    // Move the existing icon into the stack as the base (off) layer, then
    // stack the remaining states on top of it.
    icon.replaceWith(stack);
    stack.appendChild(icon);
    for (let state of IPProtectionToolbarButton.ICON_LAYER_STATES) {
      let layer = doc.createXULElement("image");
      layer.classList.add("ipprotection-icon-layer");
      layer.setAttribute("data-state", state);
      // Purely presentational; the button itself carries the accessible name.
      layer.setAttribute("aria-hidden", "true");
      stack.appendChild(layer);
    }
  }

  /**
   * Cleans up listeners and observers when the button is destroyed.
   */
  uninit() {
    if (this.gBrowser && this.#progressListener) {
      this.gBrowser.removeTabsProgressListener(this.#progressListener);
    }
    this.#progressListener = null;

    if (this.gBrowser?.tabContainer) {
      this.gBrowser.tabContainer.removeEventListener("TabSelect", this);
    }

    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
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
    lazy.IPPSiteRuleManager.removeEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleEvent
    );
  }
}
