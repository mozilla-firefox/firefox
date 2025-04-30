/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { ReviewCheckerManager } from "resource:///modules/ReviewCheckerManager.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  isProductURL: "chrome://global/content/shopping/ShoppingProduct.mjs",
  getProductIdFromURL: "chrome://global/content/shopping/ShoppingProduct.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
});

const OPTED_IN_PREF = "browser.shopping.experience2023.optedIn";
const ACTIVE_PREF = "browser.shopping.experience2023.active";
const LAST_AUTO_ACTIVATE_PREF =
  "browser.shopping.experience2023.lastAutoActivate";
const AUTO_ACTIVATE_COUNT_PREF =
  "browser.shopping.experience2023.autoActivateCount";
const ADS_USER_ENABLED_PREF = "browser.shopping.experience2023.ads.userEnabled";
const AUTO_OPEN_ENABLED_PREF =
  "browser.shopping.experience2023.autoOpen.enabled";
const AUTO_OPEN_USER_ENABLED_PREF =
  "browser.shopping.experience2023.autoOpen.userEnabled";
const SIDEBAR_CLOSED_COUNT_PREF =
  "browser.shopping.experience2023.sidebarClosedCount";

const CFR_FEATURES_PREF =
  "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features";

const ENABLED_PREF = "browser.shopping.experience2023.enabled";
const INTEGRATED_SIDEBAR_PREF =
  "browser.shopping.experience2023.integratedSidebar";

export const ShoppingUtils = {
  initialized: false,
  registered: false,
  handledAutoActivate: false,
  enabled: false,
  integratedSidebar: false,
  everyWindowCallbackId: `shoppingutils-${Services.uuid.generateUUID()}`,
  managers: new WeakMap(),

  _updatePrefVariables() {
    this.integratedSidebar = Services.prefs.getBoolPref(
      INTEGRATED_SIDEBAR_PREF,
      false
    );
    this.enabled =
      this.integratedSidebar || Services.prefs.getBoolPref(ENABLED_PREF, false);
  },

  onPrefUpdate(_subject, topic) {
    if (topic !== "nsPref:changed") {
      return;
    }
    if (this.initialized) {
      ShoppingUtils.uninit(true);
      Glean.shoppingSettings.nimbusDisabledShopping.set(true);
    }
    this._updatePrefVariables();

    if (this.enabled) {
      ShoppingUtils.init();
      Glean.shoppingSettings.nimbusDisabledShopping.set(false);
    }
  },

  // Runs once per session:
  // * at application startup, with startup idle tasks,
  // * or after the user is enrolled in the Nimbus experiment.
  init() {
    if (this.initialized) {
      return;
    }
    this.onPrefUpdate = this.onPrefUpdate.bind(this);
    this.onActiveUpdate = this.onActiveUpdate.bind(this);
    this._addManagerForWindow = this._addManagerForWindow.bind(this);
    this._removeManagerForWindow = this._removeManagerForWindow.bind(this);

    if (!this.registered) {
      // Note (bug 1855545): we must set `this.registered` before calling
      // `onUpdate`, as it will immediately invoke `this.onPrefUpdate`,
      // which in turn calls `ShoppingUtils.init`, creating an infinite loop.
      this.registered = true;
      Services.prefs.addObserver(ENABLED_PREF, this.onPrefUpdate);
      Services.prefs.addObserver(INTEGRATED_SIDEBAR_PREF, this.onPrefUpdate);
      this._updatePrefVariables();
    }

    if (!this.enabled) {
      return;
    }

    // Do startup-time stuff here, like recording startup-time glean events
    // or adjusting onboarding-related prefs once per session.

    this.setOnUpdate(undefined, undefined, this.optedIn);
    this.recordUserAdsPreference();
    this.recordUserAutoOpenPreference();

    if (this.integratedSidebar) {
      this._addReviewCheckerManagers();
    } else {
      if (this.isAutoOpenEligible()) {
        Services.prefs.setBoolPref(ACTIVE_PREF, true);
      }
      Services.prefs.addObserver(ACTIVE_PREF, this.onActiveUpdate);
    }

    Services.prefs.setIntPref(SIDEBAR_CLOSED_COUNT_PREF, 0);

    this.initialized = true;
  },

  /**
   * Runs when:
   * - the shopping2023 enabled or integratedSidebar prefs are changed,
   * - the user is unenrolled from the Nimbus experiment,
   * - or at shutdown, after quit-application-granted.
   *
   * @param {boolean} soft
   *    If this is a soft uninit, for a pref change, we want to keep the
   *    pref listeners around incase they are changed again.
   */
  uninit(soft) {
    if (!this.initialized) {
      return;
    }

    // Do shutdown-time stuff here, like firing glean pings or modifying any
    // prefs for onboarding.

    Services.prefs.removeObserver(ACTIVE_PREF, this.onActiveUpdate);

    if (!soft) {
      this.registered = false;
      Services.prefs.removeObserver(ENABLED_PREF, this.onPrefUpdate);
      Services.prefs.removeObserver(INTEGRATED_SIDEBAR_PREF, this.onPrefUpdate);
    }

    if (this.managers.size) {
      this._removeReviewCheckerManagers();
    }

    this.initialized = false;
  },

  isProductPageNavigation(aLocationURI, aFlags) {
    if (!lazy.isProductURL(aLocationURI)) {
      return false;
    }

    // Ignore same-document navigation, except in the case of Walmart
    // as they use pushState to navigate between pages.
    let isWalmart = aLocationURI.host.includes("walmart");
    let isNewDocument = !aFlags;

    let isSameDocument =
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT;
    let isReload = aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD;
    let isSessionRestore =
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SESSION_STORE;

    // Unfortunately, Walmart sometimes double-fires history manipulation
    // events when navigating between product pages. To dedupe, cache the
    // last visited Walmart URL just for a few milliseconds, so we can avoid
    // double-counting such navigations.
    if (isWalmart) {
      if (
        this.lastWalmartURI &&
        aLocationURI.equalsExceptRef(this.lastWalmartURI)
      ) {
        return false;
      }
      this.lastWalmartURI = aLocationURI;
      lazy.setTimeout(() => {
        this.lastWalmartURI = null;
      }, 100);
    }

    return (
      // On initial visit to a product page, even from another domain, both a page
      // load and a pushState will be triggered by Walmart, so this will
      // capture only a single displayed event.
      (!isWalmart && !!(isNewDocument || isReload || isSessionRestore)) ||
      (isWalmart && !!isSameDocument)
    );
  },

  /**
   * Similar to isProductPageNavigation but compares the
   * current location URI to a previous location URI and
   * checks if the URI and product has changed.
   *
   * This lets us avoid issues with over-counting products
   * that have multiple loads or history changes.
   *
   * @param {nsIURI} aLocationURI
   *    The current location.
   * @param {integer} aFlags
   *    The load flags or null.
   * @param {nsIURI} aPreviousURI
   *    A previous product URI or null.
   * @returns {boolean} isNewProduct
   */
  hasLocationChanged(aLocationURI, aFlags, aPreviousURI) {
    let isReload = aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_RELOAD;
    let isSessionRestore =
      aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SESSION_STORE;

    // If we have reloaded, restored or there isn't a previous URI
    // this is a location change.
    if (isReload || isSessionRestore || !aPreviousURI) {
      return true;
    }

    let isCurrentLocationProduct = lazy.isProductURL(aLocationURI);
    let isPrevLocationProduct = lazy.isProductURL(aPreviousURI);

    // If the locations are not products, we can just compare URIs.
    if (!isCurrentLocationProduct && !isPrevLocationProduct) {
      return aLocationURI.equalsExceptRef(aPreviousURI);
    }

    // If one of the URIs is not a product url, but the other is
    // this is a location change.
    if (!isCurrentLocationProduct || !isPrevLocationProduct) {
      return true;
    }

    // If URIs are both products we will need to check,
    // if the product have changed by comparing them.
    let isSameProduct = this.isSameProduct(aLocationURI, aPreviousURI);
    return !isSameProduct;
  },

  // For enabled users, increment a
  // counter when they visit supported product pages.
  recordExposure() {
    if (this.enabled) {
      Glean.shopping.productPageVisits.add(1);
    }
  },

  setOnUpdate(_pref, _prev, current) {
    Glean.shoppingSettings.componentOptedOut.set(current === 2);
    Glean.shoppingSettings.hasOnboarded.set(current > 0);
  },

  recordUserAdsPreference() {
    Glean.shoppingSettings.disabledAds.set(!ShoppingUtils.adsUserEnabled);
  },

  recordUserAutoOpenPreference() {
    Glean.shoppingSettings.autoOpenUserDisabled.set(
      !ShoppingUtils.autoOpenUserEnabled
    );
  },

  /**
   * If the user has not opted in, automatically set the sidebar to `active` if:
   * 1. The sidebar has not already been automatically set to `active` twice.
   * 2. It's been at least 24 hours since the user last saw the sidebar because
   *    of this auto-activation behavior.
   * 3. This method has not already been called (handledAutoActivate is false)
   */
  handleAutoActivateOnProduct() {
    let shouldAutoActivate = false;
    if (!this.handledAutoActivate && !this.optedIn && this.cfrFeatures) {
      let autoActivateCount = Services.prefs.getIntPref(
        AUTO_ACTIVATE_COUNT_PREF,
        0
      );
      let lastAutoActivate = Services.prefs.getIntPref(
        LAST_AUTO_ACTIVATE_PREF,
        0
      );
      let now = Date.now() / 1000;
      // If we automatically set `active` to true in a previous session less
      // than 24 hours ago, set it to false now. This is done to prevent the
      // auto-activation state from persisting between sessions. Effectively,
      // the auto-activation will persist until either 1) the sidebar is closed,
      // or 2) Firefox restarts.
      if (now - lastAutoActivate < 24 * 60 * 60) {
        Services.prefs.setBoolPref(ACTIVE_PREF, false);
      }
      // Set active to true if we haven't done so recently nor more than twice.
      else if (autoActivateCount < 2) {
        Services.prefs.setBoolPref(ACTIVE_PREF, true);
        shouldAutoActivate = true;
        Services.prefs.setIntPref(
          AUTO_ACTIVATE_COUNT_PREF,
          autoActivateCount + 1
        );
        Services.prefs.setIntPref(LAST_AUTO_ACTIVATE_PREF, now);
      }
    }
    this.handledAutoActivate = true;
    return shouldAutoActivate;
  },

  /**
   * Send a Shopping-related trigger message to ASRouter.
   *
   * @param {object} trigger The trigger object to send to ASRouter.
   * @param {object} trigger.context Additional trigger properties to pass to
   *   the targeting context.
   * @param {string} trigger.id The id of the trigger.
   * @param {MozBrowser} trigger.browser The browser to associate with the
   *   trigger. (This can determine the tab/window the message is shown in,
   *   depending on the message surface)
   */
  async sendTrigger(trigger) {
    await lazy.ASRouter.waitForInitialized;
    await lazy.ASRouter.sendTriggerMessage(trigger);
  },

  onActiveUpdate(subject, topic, data) {
    if (data !== ACTIVE_PREF || topic !== "nsPref:changed") {
      return;
    }

    let newValue = Services.prefs.getBoolPref(ACTIVE_PREF);
    if (newValue === false) {
      ShoppingUtils.resetActiveOnNextProductPage = true;
    }
  },

  isAutoOpenEligible() {
    return (
      this.optedIn === 1 && this.autoOpenEnabled && this.autoOpenUserEnabled
    );
  },

  onLocationChange(aLocationURI, aFlags) {
    let isProductPageNavigation = this.isProductPageNavigation(
      aLocationURI,
      aFlags
    );

    if (isProductPageNavigation) {
      this.recordExposure();
    }

    if (
      !this.integratedSidebar &&
      this.isAutoOpenEligible() &&
      this.resetActiveOnNextProductPage &&
      isProductPageNavigation
    ) {
      this.resetActiveOnNextProductPage = false;
      Services.prefs.setBoolPref(ACTIVE_PREF, true);
    }
  },

  /**
   * Check if two URIs represent the same product by
   * comparing URLs and then parsed product ID.
   *
   * @param {nsIURI} aURI
   * @param {nsIURI} bURI
   *
   * @returns {boolean}
   */
  isSameProduct(aURI, bURI) {
    if (!aURI || !bURI) {
      return false;
    }

    // Check if the URIs are equal and are products.
    if (aURI.equalsExceptRef(bURI)) {
      return lazy.isProductURL(aURI);
    }

    // Check if the product ids are the same:
    let aProductID = lazy.getProductIdFromURL(aURI);
    let bProductID = lazy.getProductIdFromURL(bURI);

    if (!aProductID || !bProductID) {
      return false;
    }

    return aProductID === bProductID;
  },

  /**
   * Removes browser `reviewCheckerWasClosed` flag that indicates the
   * Review Checker sidebar was closed by a user action.
   *
   * @param {browser} browser
   */
  clearWasClosedFlag(browser) {
    if (browser.reviewCheckerWasClosed) {
      delete browser.reviewCheckerWasClosed;
    }
  },

  /**
   * Removes browser `isDistinctProductPageVisit` flag that indicates
   * a tab has an unhandled product navigation.
   *
   * @param {browser} browser
   */
  clearIsDistinctProductPageVisitFlag(browser) {
    if (browser.isDistinctProductPageVisit) {
      delete browser.isDistinctProductPageVisit;
    }
  },

  _addManagerForWindow(window) {
    let manager = new ReviewCheckerManager(window);
    this.managers.set(window, manager);
  },

  _removeManagerForWindow(window) {
    let manager = this.managers.get(window);
    if (manager) {
      manager.uninit();
      this.managers.delete(manager);
    }
  },

  _addReviewCheckerManagers() {
    lazy.EveryWindow.registerCallback(
      this.everyWindowCallbackId,
      this._addManagerForWindow,
      this._removeManagerForWindow
    );
  },

  _removeReviewCheckerManagers() {
    lazy.EveryWindow.unregisterCallback(this.everyWindowCallbackId);
    // Clear incase we missed unregistering any managers.
    this.managers.clear();
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  ShoppingUtils,
  "optedIn",
  OPTED_IN_PREF,
  0,
  ShoppingUtils.setOnUpdate
);

XPCOMUtils.defineLazyPreferenceGetter(
  ShoppingUtils,
  "cfrFeatures",
  CFR_FEATURES_PREF,
  true
);

XPCOMUtils.defineLazyPreferenceGetter(
  ShoppingUtils,
  "adsUserEnabled",
  ADS_USER_ENABLED_PREF,
  false,
  ShoppingUtils.recordUserAdsPreference
);

XPCOMUtils.defineLazyPreferenceGetter(
  ShoppingUtils,
  "autoOpenEnabled",
  AUTO_OPEN_ENABLED_PREF,
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  ShoppingUtils,
  "autoOpenUserEnabled",
  AUTO_OPEN_USER_ENABLED_PREF,
  false,
  ShoppingUtils.recordUserAutoOpenPreference
);
