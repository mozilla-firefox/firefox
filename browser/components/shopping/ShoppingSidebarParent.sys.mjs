/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  isProductURL: "chrome://global/content/shopping/ShoppingProduct.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  ShoppingUtils: "resource:///modules/ShoppingUtils.sys.mjs",
});

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "AUTO_OPEN_SIDEBAR_ENABLED",
  "browser.shopping.experience2023.autoOpen.enabled",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "AUTO_OPEN_SIDEBAR_USER_ENABLED",
  "browser.shopping.experience2023.autoOpen.userEnabled",
  true
);

export class ShoppingSidebarParent extends JSWindowActorParent {
  static SHOPPING_ENABLED_PREF = "browser.shopping.experience2023.enabled";
  static SHOPPING_ACTIVE_PREF = "browser.shopping.experience2023.active";
  static SHOPPING_OPTED_IN_PREF = "browser.shopping.experience2023.optedIn";
  static SIDEBAR_CLOSED_COUNT_PREF =
    "browser.shopping.experience2023.sidebarClosedCount";
  static SHOW_KEEP_SIDEBAR_CLOSED_MESSAGE_PREF =
    "browser.shopping.experience2023.showKeepSidebarClosedMessage";
  static INTEGRATED_SIDEBAR_PANEL_PREF =
    "browser.shopping.experience2023.integratedSidebar";

  updateCurrentURL(uri, flags) {
    this.sendAsyncMessage("ShoppingSidebar:UpdateProductURL", {
      url: uri?.spec ?? null,
      isReload: !!(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD),
    });
  }

  async receiveMessage(message) {
    if (this.browsingContext.usePrivateBrowsing) {
      throw new Error("We should never be invoked in PBM.");
    }
    switch (message.name) {
      case "GetProductURL":
        return this.getCurrentURL();
      case "DisableShopping":
        Services.prefs.setBoolPref(
          ShoppingSidebarParent.SHOPPING_ACTIVE_PREF,
          false
        );
        Services.prefs.setIntPref(
          ShoppingSidebarParent.SHOPPING_OPTED_IN_PREF,
          2
        );
        break;
    }
    return null;
  }

  /**
   * Gets the URL of the current tab.
   */
  getCurrentURL() {
    let sidebarBrowser = this.browsingContext.top.embedderElement;
    let panel = sidebarBrowser.closest(".browserSidebarContainer");
    let associatedTabbedBrowser = panel.querySelector(
      "browser[messagemanagergroup=browsers]"
    );
    return associatedTabbedBrowser.currentURI?.spec ?? null;
  }

  /**
   * Called when the user clicks the URL bar button.
   */
  static async urlbarButtonClick(event) {
    if (
      lazy.AUTO_OPEN_SIDEBAR_ENABLED &&
      lazy.AUTO_OPEN_SIDEBAR_USER_ENABLED &&
      event.target.getAttribute("shoppingsidebaropen") === "true"
    ) {
      let gBrowser = event.target.ownerGlobal.gBrowser;
      let shoppingBrowser = gBrowser
        .getPanel(gBrowser.selectedBrowser)
        .querySelector(".shopping-sidebar");
      let actor =
        shoppingBrowser.browsingContext.currentWindowGlobal.getActor(
          "ShoppingSidebar"
        );

      let isKeepClosedMessageShowing = await actor.sendQuery(
        "ShoppingSidebar:IsKeepClosedMessageShowing"
      );

      let sidebarClosedCount = Services.prefs.getIntPref(
        ShoppingSidebarParent.SIDEBAR_CLOSED_COUNT_PREF,
        0
      );
      if (
        !isKeepClosedMessageShowing &&
        sidebarClosedCount >= 4 &&
        Services.prefs.getBoolPref(
          ShoppingSidebarParent.SHOW_KEEP_SIDEBAR_CLOSED_MESSAGE_PREF,
          true
        )
      ) {
        actor.sendAsyncMessage("ShoppingSidebar:ShowKeepClosedMessage");
        return;
      }

      actor.sendAsyncMessage("ShoppingSidebar:HideKeepClosedMessage");

      if (sidebarClosedCount >= 6) {
        Services.prefs.setBoolPref(
          ShoppingSidebarParent.SHOW_KEEP_SIDEBAR_CLOSED_MESSAGE_PREF,
          false
        );
      } else {
        Services.prefs.setIntPref(
          ShoppingSidebarParent.SIDEBAR_CLOSED_COUNT_PREF,
          sidebarClosedCount + 1
        );
      }
    }

    this.toggleAllSidebars("urlBar");
  }

  /**
   * Toggles opening or closing all Shopping sidebars.
   * Sets the active pref value for all windows to respond to.
   * params:
   *
   *  @param {string?} source
   *  Optional value, describes where the call came from.
   */
  static toggleAllSidebars(source) {
    let activeState = Services.prefs.getBoolPref(
      ShoppingSidebarParent.SHOPPING_ACTIVE_PREF
    );
    Services.prefs.setBoolPref(
      ShoppingSidebarParent.SHOPPING_ACTIVE_PREF,
      !activeState
    );

    let optedIn = Services.prefs.getIntPref(
      ShoppingSidebarParent.SHOPPING_OPTED_IN_PREF
    );
    // If the user was opted out, then clicked the button, reset the optedIn
    // pref so they see onboarding.
    if (optedIn == 2) {
      Services.prefs.setIntPref(
        ShoppingSidebarParent.SHOPPING_OPTED_IN_PREF,
        0
      );
    }
    if (source == "urlBar") {
      if (activeState) {
        Glean.shopping.surfaceClosed.record({ source: "addressBarIcon" });
        Glean.shopping.addressBarIconClicked.record({ action: "closed" });
      } else {
        Glean.shopping.addressBarIconClicked.record({ action: "opened" });
      }
    }
  }
}

class ShoppingSidebarManagerClass {
  #initialized = false;
  #everyWindowCallbackId = `shopping-${Services.uuid.generateUUID()}`;

  // Public API methods - these check that we are not in private browsing
  // mode. (It might be nice to eventually shift pref checks to the public
  // API, too.)
  //
  // Note that any refactoring should preserve the PBM checks in public APIs.

  ensureInitialized() {
    if (this.#initialized) {
      return;
    }

    this.updateSidebarVisibility = this.updateSidebarVisibility.bind(this);
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "isEnabled",
      ShoppingSidebarParent.SHOPPING_ENABLED_PREF,
      false,
      this.updateSidebarVisibility
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "optedInPref",
      "browser.shopping.experience2023.optedIn",
      null,
      this.updateSidebarVisibility
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "isActive",
      ShoppingSidebarParent.SHOPPING_ACTIVE_PREF,
      true,
      this.updateSidebarVisibility
    );
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "isIntegratedSidebarPanel",
      ShoppingSidebarParent.INTEGRATED_SIDEBAR_PANEL_PREF,
      false,
      this.updateSidebarVisibility
    );
    this.updateSidebarVisibility();

    lazy.EveryWindow.registerCallback(
      this.#everyWindowCallbackId,
      window => {
        let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
        if (isPBM) {
          return;
        }

        window.gBrowser.tabContainer.addEventListener("TabSelect", this);
        window.addEventListener("visibilitychange", this);
      },
      window => {
        let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
        if (isPBM) {
          return;
        }

        window.gBrowser.tabContainer.removeEventListener("TabSelect", this);
        window.removeEventListener("visibilitychange", this);
      }
    );

    this.#initialized = true;
  }

  updateSidebarVisibility() {
    this.enabled = this.isEnabled && !this.isIntegratedSidebarPanel;
    for (let window of lazy.BrowserWindowTracker.orderedWindows) {
      let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
      if (isPBM) {
        continue;
      }
      this.updateSidebarVisibilityForWindow(window);
    }
  }

  updateSidebarVisibilityForWindow(window) {
    if (window.closed) {
      return;
    }

    if (!window.gBrowser) {
      return;
    }

    let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(window);
    if (isPBM) {
      return;
    }

    let document = window.document;

    if (!this.isActive) {
      document.querySelectorAll("shopping-sidebar").forEach(sidebar => {
        sidebar.hidden = true;
      });
      document
        .querySelectorAll(".shopping-sidebar-splitter")
        .forEach(splitter => {
          splitter.hidden = true;
        });
    }

    this._maybeToggleButton(window.gBrowser);

    if (!this.enabled) {
      document.querySelectorAll("shopping-sidebar").forEach(sidebar => {
        sidebar.remove();
      });
      document
        .querySelectorAll(".shopping-sidebar-splitter")
        .forEach(splitter => {
          splitter.remove();
        });
      let button = document.getElementById("shopping-sidebar-button");
      if (button) {
        button.hidden = true;
        // Reset attributes to defaults.
        button.setAttribute("shoppingsidebaropen", false);
        document.l10n.setAttributes(button, "shopping-sidebar-open-button2");
      }
      return;
    }

    let { selectedBrowser, currentURI } = window.gBrowser;
    this._maybeToggleSidebar(selectedBrowser, currentURI, 0, false);
  }

  /**
   * Called by TabsProgressListener whenever any browser navigates from one
   * URL to another.
   * Note that this includes hash changes / pushState navigations, because
   * those can be significant for us.
   */
  onLocationChange(aBrowser, aLocationURI, aFlags) {
    let isPBM = lazy.PrivateBrowsingUtils.isWindowPrivate(aBrowser.ownerGlobal);
    if (isPBM || !this.enabled) {
      return;
    }

    lazy.ShoppingUtils.onLocationChange(aLocationURI, aFlags);

    this._maybeToggleButton(aBrowser.getTabBrowser());
    this._maybeToggleSidebar(aBrowser, aLocationURI, aFlags, true);
  }

  handleEvent(event) {
    switch (event.type) {
      case "TabSelect": {
        if (!this.enabled) {
          return;
        }
        this.updateSidebarVisibility();
        if (event.detail?.previousTab.linkedBrowser) {
          this._updateBCActiveness(event.detail.previousTab.linkedBrowser);
        }
        break;
      }
      case "visibilitychange": {
        if (!this.enabled) {
          return;
        }
        let { gBrowser } = event.target.ownerGlobal.top;
        if (!gBrowser) {
          return;
        }
        this.updateSidebarVisibilityForWindow(event.target.ownerGlobal.top);
        this._updateBCActiveness(gBrowser.selectedBrowser);
      }
    }
  }

  // Private API methods - these assume we are not in private browsing
  // mode. (It might be nice to eventually shift pref checks to the public
  // API, too.)

  _maybeToggleSidebar(aBrowser, aLocationURI, aFlags, aIsNavigation) {
    let gBrowser = aBrowser.getTabBrowser();
    let document = aBrowser.ownerDocument;
    if (!this.enabled) {
      return;
    }

    let browserPanel = gBrowser.getPanel(aBrowser);
    let sidebar = browserPanel.querySelector("shopping-sidebar");
    let actor;
    if (sidebar) {
      let { browsingContext } = sidebar.querySelector("browser");
      let global = browsingContext.currentWindowGlobal;
      actor = global.getExistingActor("ShoppingSidebar");
    }
    let isProduct = lazy.isProductURL(aLocationURI);
    if (isProduct && this.isActive) {
      if (!sidebar) {
        sidebar = document.createXULElement("shopping-sidebar");
        sidebar.hidden = false;
        let splitter = document.createXULElement("splitter");
        splitter.classList.add("sidebar-splitter", "shopping-sidebar-splitter");
        browserPanel.appendChild(splitter);
        browserPanel.appendChild(sidebar);
      } else {
        actor?.updateCurrentURL(aLocationURI, aFlags);
        sidebar.hidden = false;
        let splitter = browserPanel.querySelector(".shopping-sidebar-splitter");
        splitter.hidden = false;
      }
    } else if (sidebar && !sidebar.hidden) {
      actor?.updateCurrentURL(null);
      sidebar.hidden = true;
      let splitter = browserPanel.querySelector(".shopping-sidebar-splitter");
      splitter.hidden = true;
    }

    this._updateBCActiveness(aBrowser);
    this._setShoppingButtonState(aBrowser);

    // Note: (bug 1868602) only record surface displayed telemetry if:
    // - the foregrounded tab navigates to a product page with sidebar visible,
    // - a product page tab loaded in the background is foregrounded, or
    // - a foregrounded product page tab was loaded with the sidebar hidden and
    //   now the sidebar has been shown.
    if (
      this.enabled &&
      lazy.ShoppingUtils.isProductPageNavigation(aLocationURI, aFlags)
    ) {
      if (
        this.isActive &&
        aBrowser === gBrowser.selectedBrowser &&
        (aIsNavigation || aBrowser.isDistinctProductPageVisit)
      ) {
        Glean.shopping.surfaceDisplayed.record();
        delete aBrowser.isDistinctProductPageVisit;
      } else if (aIsNavigation) {
        aBrowser.isDistinctProductPageVisit = true;
      }
    }

    if (isProduct) {
      // This is the auto-enable behavior that toggles the `active` pref. It
      // must be at the end of this function, or 2 sidebars could be created.
      lazy.ShoppingUtils.handleAutoActivateOnProduct();

      if (!this.isActive) {
        lazy.ShoppingUtils.sendTrigger({
          browser: aBrowser,
          id: "shoppingProductPageWithSidebarClosed",
          context: { isSidebarClosing: !aIsNavigation && !!sidebar },
        });
      }
    }
  }

  _maybeToggleButton(gBrowser) {
    let optedOut = this.optedInPref === 2;
    if (this.enabled && optedOut) {
      this._setShoppingButtonState(gBrowser.selectedBrowser);
    }
  }

  _updateBCActiveness(aBrowser) {
    let gBrowser = aBrowser.getTabBrowser();
    let document = aBrowser.ownerDocument;
    let browserPanel = gBrowser.getPanel(aBrowser);
    let sidebar = browserPanel.querySelector("shopping-sidebar");
    if (!sidebar) {
      return;
    }
    try {
      // Tell Gecko when the sidebar visibility changes to avoid background
      // sidebars taking more CPU / energy than needed.
      sidebar.querySelector("browser").docShellIsActive =
        !document.hidden &&
        aBrowser == gBrowser.selectedBrowser &&
        !sidebar.hidden;
    } catch (ex) {
      // The setter can throw and we do need to run the rest of this
      // code in that case.
      console.error(ex);
    }
  }

  _setShoppingButtonState(aBrowser) {
    let gBrowser = aBrowser.getTabBrowser();
    let document = aBrowser.ownerDocument;
    if (aBrowser !== gBrowser.selectedBrowser) {
      return;
    }

    let button = document.getElementById("shopping-sidebar-button");

    let isCurrentBrowserProduct = lazy.isProductURL(
      gBrowser.selectedBrowser.currentURI
    );

    // Only record if the state of the icon will change from hidden to visible.
    if (button.hidden && isCurrentBrowserProduct) {
      Glean.shopping.addressBarIconDisplayed.record();
    }

    button.hidden = !isCurrentBrowserProduct;
    button.setAttribute("shoppingsidebaropen", !!this.isActive);
    let l10nId = this.isActive
      ? "shopping-sidebar-close-button2"
      : "shopping-sidebar-open-button2";
    document.l10n.setAttributes(button, l10nId);
  }
}

const ShoppingSidebarManager = new ShoppingSidebarManagerClass();
export { ShoppingSidebarManager };
