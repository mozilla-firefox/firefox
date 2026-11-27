/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ZoomUI: "resource:///modules/ZoomUI.sys.mjs",
  cps2: {
    service: "@mozilla.org/content-pref/service;1",
    iid: Ci.nsIContentPrefService2,
  },
  siteSpecific: { pref: "browser.zoom.siteSpecific", default: true },
  updateBackgroundTabs: {
    pref: "browser.zoom.updateBackgroundTabs",
    default: true,
  },
});

// Identifies the setting in the content prefs database.
const CONTENT_PREF_NAME = "browser.content.full-zoom";

/**
 * Controls the "full zoom" setting and its site-specific preferences.
 */
export class FullZoom {
  // This maps the browser to monotonically increasing integer
  // tokens. #browserTokenMap[browser] is increased each time the zoom is
  // changed in the browser. See #getBrowserToken and #ignorePendingZoomAccesses.
  #browserTokenMap = new WeakMap();

  // Stores initial locations if we receive onLocationChange
  // events before we're initialized.
  /** @type {WeakMap<MozBrowser, [nsIURI, boolean]>} */
  #initialLocations = new WeakMap();

  #isNextContentPrefChangeInternal = false;

  /**
   * @param {BrowserWindow} window
   */
  constructor(window) {
    this.documentGlobal = window;
    this.document = window.document;
  }

  get name() {
    return CONTENT_PREF_NAME;
  }

  get siteSpecific() {
    return lazy.siteSpecific;
  }

  get updateBackgroundTabs() {
    return lazy.updateBackgroundTabs;
  }

  // nsISupports

  QueryInterface = ChromeUtils.generateQI([
    "nsIObserver",
    "nsIContentPrefObserver",
    "nsISupportsWeakReference",
  ]);

  // Initialization & Destruction

  init() {
    this.documentGlobal.gBrowser.addEventListener("DoZoomEnlarge", this);
    this.documentGlobal.gBrowser.addEventListener("DoZoomReduce", this);
    this.documentGlobal.addEventListener("MozScaleGestureComplete", this);

    // Register ourselves with the service so we know when our pref changes.
    lazy.cps2.addObserverForName(CONTENT_PREF_NAME, this);

    Services.prefs.addObserver("browser.zoom.full", this, true);

    // If we received onLocationChange events for any of the current browsers
    // before we were initialized we want to replay those upon initialization.
    for (let browser of this.documentGlobal.gBrowser.browsers) {
      if (this.#initialLocations.has(browser)) {
        this.onLocationChange(...this.#initialLocations.get(browser), browser);
      }
    }

    // This should be nulled after initialization.
    this.#initialLocations = null;
  }

  destroy() {
    Services.prefs.removeObserver("browser.zoom.full", this);
    lazy.cps2.removeObserverForName(CONTENT_PREF_NAME, this);
    this.documentGlobal.gBrowser.removeEventListener("DoZoomEnlarge", this);
    this.documentGlobal.gBrowser.removeEventListener("DoZoomReduce", this);
    this.documentGlobal.removeEventListener("MozScaleGestureComplete", this);
  }

  // Event Handlers

  // EventListener

  handleEvent(event) {
    switch (event.type) {
      case "DoZoomEnlarge":
        this.enlarge(this.#getTargetedBrowser(event));
        break;
      case "DoZoomReduce":
        this.reduce(this.#getTargetedBrowser(event));
        break;
      case "MozScaleGestureComplete": {
        let nonDefaultScalingZoom = event.detail != 1.0;
        this.updateCommands(nonDefaultScalingZoom);
        break;
      }
    }
  }

  // nsIObserver

  observe(aSubject, aTopic, aData) {
    if (aTopic == "nsPref:changed" && aData == "browser.zoom.full") {
      this.updateCommands();
    }
  }

  // nsIContentPrefObserver

  onContentPrefSet(aGroup, aName, aValue, aIsPrivate) {
    this.#onContentPrefChanged(aGroup, aValue, aIsPrivate);
  }

  onContentPrefRemoved(aGroup, aName, aIsPrivate) {
    this.#onContentPrefChanged(aGroup, undefined, aIsPrivate);
  }

  /**
   * Appropriately updates the zoom level after a content preference has
   * changed.
   *
   * @param aGroup  The group of the changed preference.
   * @param aValue  The new value of the changed preference.  Pass undefined to
   *                indicate the preference's removal.
   */
  #onContentPrefChanged(aGroup, aValue, aIsPrivate) {
    if (this.#isNextContentPrefChangeInternal) {
      // Ignore changes that FullZoom itself makes.  This works because the
      // content pref service calls callbacks before notifying observers, and it
      // does both in the same turn of the event loop.
      this.#isNextContentPrefChangeInternal = false;
      return;
    }

    let browser = this.documentGlobal.gBrowser.selectedBrowser;
    if (!browser.currentURI) {
      return;
    }

    if (this.#isPDFViewer(browser)) {
      return;
    }

    let ctxt = this.#loadContextFromBrowser(browser);
    let domain = lazy.cps2.extractDomain(browser.currentURI.spec);
    if (aGroup) {
      if (aGroup == domain && ctxt.usePrivateBrowsing == aIsPrivate) {
        this.#applyPrefToZoom(aValue, browser);
      }
      return;
    }

    // If the current page doesn't have a site-specific preference, then its
    // zoom should be set to the new global preference now that the global
    // preference has changed.
    let hasPref = false;
    let token = this.#getBrowserToken(browser);
    lazy.cps2.getByDomainAndName(
      browser.currentURI.spec,
      CONTENT_PREF_NAME,
      ctxt,
      {
        handleResult() {
          hasPref = true;
        },
        handleError() {},
        handleCompletion: () => {
          if (!hasPref && token.isCurrent) {
            this.#applyPrefToZoom(undefined, browser);
          }
        },
      }
    );
  }

  // location change observer

  /**
   * Called when the location of a tab changes.
   * When that happens, we need to update the current zoom level if appropriate.
   *
   * @param aURI
   *        A URI object representing the new location.
   * @param aIsTabSwitch
   *        Whether this location change has happened because of a tab switch.
   * @param aBrowser
   *        (optional) browser object displaying the document
   */
  onLocationChange(aURI, aIsTabSwitch, aBrowser) {
    let browser = aBrowser || this.documentGlobal.gBrowser.selectedBrowser;

    // If we haven't been initialized yet but receive an onLocationChange
    // notification then let's store and replay it upon initialization.
    if (this.#initialLocations) {
      this.#initialLocations.set(browser, [aURI, aIsTabSwitch]);
      return;
    }

    // Ignore all pending async zoom accesses in the browser.  Pending accesses
    // that started before the location change will be prevented from applying
    // to the new location.
    this.#ignorePendingZoomAccesses(browser);

    if (!aURI || (aIsTabSwitch && !this.siteSpecific)) {
      this.#notifyOnLocationChange(browser);
      return;
    }

    if (aURI.spec == "about:blank") {
      if (
        !browser.contentPrincipal ||
        browser.contentPrincipal.isNullPrincipal
      ) {
        // For an about:blank with a null principal, zooming any amount does not
        // make any sense - so simply do 100%.
        this.#applyPrefToZoom(
          1,
          browser,
          this.#notifyOnLocationChange.bind(this, browser)
        );
      } else {
        // If it's not a null principal, there may be content loaded into it,
        // so use the global pref. This will avoid a cps2 roundtrip if we've
        // already loaded the global pref once. Really, this should probably
        // use the contentPrincipal's origin if it's an http(s) principal.
        // (See bug 1457597)
        this.#applyPrefToZoom(
          undefined,
          browser,
          this.#notifyOnLocationChange.bind(this, browser)
        );
      }
      return;
    }

    // Media documents should always start at 1, and are not affected by prefs.
    if (!aIsTabSwitch && browser.isSyntheticDocument) {
      this.documentGlobal.ZoomManager.setZoomForBrowser(browser, 1);
      // #ignorePendingZoomAccesses already called above, so no need here.
      this.#notifyOnLocationChange(browser);
      return;
    }

    // The PDF viewer zooming isn't handled by `ZoomManager`, ensure that the
    // browser zoom level always gets reset to 100% on load (to prevent the
    // UI elements of the PDF viewer from being zoomed in/out on load).
    if (this.#isPDFViewer(browser)) {
      this.#applyPrefToZoom(
        1,
        browser,
        this.#notifyOnLocationChange.bind(this, browser)
      );
      return;
    }

    // See if the zoom pref is cached.
    let ctxt = this.#loadContextFromBrowser(browser);
    let pref = lazy.cps2.getCachedByDomainAndName(
      aURI.spec,
      CONTENT_PREF_NAME,
      ctxt
    );
    if (pref) {
      this.#applyPrefToZoom(
        pref.value,
        browser,
        this.#notifyOnLocationChange.bind(this, browser)
      );
      return;
    }

    // It's not cached, so we have to asynchronously fetch it.
    let value = undefined;
    let token = this.#getBrowserToken(browser);
    lazy.cps2.getByDomainAndName(aURI.spec, CONTENT_PREF_NAME, ctxt, {
      handleResult(resultPref) {
        value = resultPref.value;
      },
      handleError() {},
      handleCompletion: () => {
        if (!token.isCurrent) {
          this.#notifyOnLocationChange(browser);
          return;
        }
        this.#applyPrefToZoom(
          value,
          browser,
          this.#notifyOnLocationChange.bind(this, browser)
        );
      },
    });
  }

  // update state of zoom menu items

  /**
   * Updates the current windows Zoom commands for zooming in, zooming out
   * and resetting the zoom level.
   *
   * @param {boolean} [forceResetEnabled=false]
   *   Set to true if the zoom reset command should be enabled regardless of
   *   whether or not the ZoomManager.zoom level is at 1.0. This is specifically
   *   for when using scaling zoom via the pinch gesture which doesn't cause
   *   the ZoomManager.zoom level to change.
   * @returns {Promise<void>}
   */
  async updateCommands(forceResetEnabled = false) {
    let zoomLevel = this.documentGlobal.ZoomManager.zoom;
    let defaultZoomLevel = await lazy.ZoomUI.getGlobalValue();
    let reduceCmd = this.document.getElementById("cmd_fullZoomReduce");
    if (zoomLevel == this.documentGlobal.ZoomManager.MIN) {
      reduceCmd.setAttribute("disabled", "true");
    } else {
      reduceCmd.removeAttribute("disabled");
    }

    let enlargeCmd = this.document.getElementById("cmd_fullZoomEnlarge");
    if (zoomLevel == this.documentGlobal.ZoomManager.MAX) {
      enlargeCmd.setAttribute("disabled", "true");
    } else {
      enlargeCmd.removeAttribute("disabled");
    }

    let resetCmd = this.document.getElementById("cmd_fullZoomReset");
    if (zoomLevel == defaultZoomLevel && !forceResetEnabled) {
      resetCmd.setAttribute("disabled", "true");
    } else {
      resetCmd.removeAttribute("disabled");
    }

    let fullZoomCmd = this.document.getElementById("cmd_fullZoomToggle");
    fullZoomCmd.toggleAttribute(
      "checked",
      !this.documentGlobal.ZoomManager.useFullZoom
    );
  }

  // Setting & Pref Manipulation

  sendMessageToPDFViewer(browser, name) {
    try {
      browser.sendMessageToActor(name, {}, "PdfJs");
    } catch (ex) {
      console.error(ex);
    }
  }

  /**
   * If browser in reader mode sends message to reader in order to decrease font size,
   * Otherwise reduces the zoom level of the page in the passed browser.
   *
   * @param {MozBrowser} [aBrowser=gBrowser.selectedBrowser]
   *   The browser to reduce the zoom level on.
   * @returns {Promise<void>}
   */
  async reduce(aBrowser = this.documentGlobal.gBrowser.selectedBrowser) {
    if (aBrowser.currentURI.spec.startsWith("about:reader")) {
      aBrowser.sendMessageToActor("Reader:ZoomOut", {}, "AboutReader");
    } else if (this.#isPDFViewer(aBrowser)) {
      this.sendMessageToPDFViewer(aBrowser, "PDFJS:ZoomOut");
    } else {
      this.documentGlobal.ZoomManager.reduceForBrowser(aBrowser);
      this.#ignorePendingZoomAccesses(aBrowser);
      await this.#applyZoomToPref(aBrowser);
    }
  }

  /**
   * If browser in reader mode sends message to reader in order to increase font size,
   * Otherwise enlarges the zoom level of the page in the passed browser.
   *
   * @param {MozBrowser} [aBrowser=gBrowser.selectedBrowser]
   *   The browser to enlarge the zoom level on.
   * @returns {Promise<void>}
   */
  async enlarge(aBrowser = this.documentGlobal.gBrowser.selectedBrowser) {
    if (aBrowser.currentURI.spec.startsWith("about:reader")) {
      aBrowser.sendMessageToActor("Reader:ZoomIn", {}, "AboutReader");
    } else if (this.#isPDFViewer(aBrowser)) {
      this.sendMessageToPDFViewer(aBrowser, "PDFJS:ZoomIn");
    } else {
      this.documentGlobal.ZoomManager.enlargeForBrowser(aBrowser);
      this.#ignorePendingZoomAccesses(aBrowser);
      await this.#applyZoomToPref(aBrowser);
    }
  }

  /**
   * Sets the zoom level for the given browser to the given floating
   * point value, where 1 is the default zoom level.
   */
  setZoom(value, browser = this.documentGlobal.gBrowser.selectedBrowser) {
    if (this.#isPDFViewer(browser)) {
      return;
    }
    this.documentGlobal.ZoomManager.setZoomForBrowser(browser, value);
    this.#ignorePendingZoomAccesses(browser);
    this.#applyZoomToPref(browser);
  }

  /**
   * Sets the zoom level of the page in the given browser to the global zoom
   * level.
   *
   * @return A promise which resolves when the zoom reset has been applied.
   */
  reset(browser = this.documentGlobal.gBrowser.selectedBrowser) {
    let forceValue;
    if (browser.currentURI.spec.startsWith("about:reader")) {
      browser.sendMessageToActor("Reader:ResetZoom", {}, "AboutReader");
    } else if (this.#isPDFViewer(browser)) {
      this.sendMessageToPDFViewer(browser, "PDFJS:ZoomReset");
      // Ensure that the UI elements of the PDF viewer won't be zoomed in/out
      // on reset, even if/when browser default zoom value is not set to 100%.
      forceValue = 1;
    }
    let token = this.#getBrowserToken(browser);
    let result = lazy.ZoomUI.getGlobalValue().then(value => {
      if (token.isCurrent) {
        this.documentGlobal.ZoomManager.setZoomForBrowser(
          browser,
          forceValue || value
        );
        this.#ignorePendingZoomAccesses(browser);
      }
    });
    this.#removePref(browser);
    return result;
  }

  /**
   * Called from the URL bar's inline zoom reset indicator button.
   */
  resetFromURLBar() {
    this.reset();
    this.resetScalingZoom();
  }

  resetScalingZoom(browser = this.documentGlobal.gBrowser.selectedBrowser) {
    browser.browsingContext?.resetScalingZoom();
  }

  /**
   * Set the zoom level for a given browser.
   *
   * Per nsPresContext::setFullZoom, we can set the zoom to its current value
   * without significant impact on performance, as the setting is only applied
   * if it differs from the current setting.  In fact getting the zoom and then
   * checking ourselves if it differs costs more.
   *
   * And perhaps we should always set the zoom even if it was more expensive,
   * since nsDocumentViewer::SetTextZoom claims that child documents can have
   * a different text zoom (although it would be unusual), and it implies that
   * those child text zooms should get updated when the parent zoom gets set,
   * and perhaps the same is true for full zoom
   * (although nsDocumentViewer::SetFullZoom doesn't mention it).
   *
   * So when we apply new zoom values to the browser, we simply set the zoom.
   * We don't check first to see if the new value is the same as the current
   * one.
   *
   * @param aValue     The zoom level value.
   * @param aBrowser   The zoom is set in this browser.  Required.
   * @param aCallback  If given, it's asynchronously called when complete.
   */
  #applyPrefToZoom(aValue, aBrowser, aCallback) {
    // The browser is sometimes half-destroyed because this method is called
    // by content pref service callbacks, which themselves can be called at any
    // time, even after browsers are closed.
    if (
      !aBrowser.mInitialized ||
      aBrowser.isSyntheticDocument ||
      (!this.siteSpecific && aBrowser.tabHasCustomZoom)
    ) {
      this.#executeSoon(aCallback);
      return;
    }

    if (aValue !== undefined && this.siteSpecific) {
      this.documentGlobal.ZoomManager.setZoomForBrowser(
        aBrowser,
        this.#ensureValid(aValue)
      );
      this.#ignorePendingZoomAccesses(aBrowser);
      this.#executeSoon(aCallback);
      return;
    }

    // Above, we check if site-specific zoom is enabled before setting
    // the tab browser zoom, however global zoom should work independent
    // of the site-specific pref, so we do no checks here.
    let token = this.#getBrowserToken(aBrowser);
    lazy.ZoomUI.getGlobalValue().then(value => {
      if (token.isCurrent) {
        this.documentGlobal.ZoomManager.setZoomForBrowser(aBrowser, value);
        this.#ignorePendingZoomAccesses(aBrowser);
      }
      this.#executeSoon(aCallback);
    });
  }

  /**
   * Saves the zoom level of the page in the given browser to the content
   * prefs store.
   *
   * @param browser  The zoom of this browser will be saved.  Required.
   */
  #applyZoomToPref(browser) {
    if (!this.siteSpecific || browser.isSyntheticDocument) {
      // If site-specific zoom is disabled, we have called this function
      // to adjust our tab's zoom level. It is now considered "custom"
      // and we mark that here.
      browser.tabHasCustomZoom = !this.siteSpecific;
      return null;
    }

    return new Promise(resolve => {
      lazy.cps2.set(
        browser.currentURI.spec,
        CONTENT_PREF_NAME,
        /** @type {nsIVariant} */ (
          this.documentGlobal.ZoomManager.getZoomForBrowser(browser)
        ),
        this.#loadContextFromBrowser(browser),
        {
          handleResult() {},
          handleError() {},
          handleCompletion: () => {
            this.#isNextContentPrefChangeInternal = true;
            resolve();
          },
        }
      );
    });
  }

  /**
   * Removes from the content prefs store the zoom level of the given browser.
   *
   * @param browser  The zoom of this browser will be removed.  Required.
   */
  #removePref(browser) {
    if (browser.isSyntheticDocument) {
      return;
    }
    let ctxt = this.#loadContextFromBrowser(browser);
    lazy.cps2.removeByDomainAndName(
      browser.currentURI.spec,
      CONTENT_PREF_NAME,
      ctxt,
      {
        handleResult() {},
        handleError() {},
        handleCompletion: () => {
          this.#isNextContentPrefChangeInternal = true;
        },
      }
    );
  }

  // Utilities

  /**
   * Returns the zoom change token of the given browser.  Asynchronous
   * operations that access the given browser's zoom should use this method to
   * capture the token before starting and use token.isCurrent to determine if
   * it's safe to access the zoom when done.  If token.isCurrent is false, then
   * after the async operation started, either the browser's zoom was changed or
   * the browser was destroyed, and depending on what the operation is doing, it
   * may no longer be safe to set and get its zoom.
   *
   * @param browser  The token of this browser will be returned.
   * @return  An object with an "isCurrent" getter.
   */
  #getBrowserToken(browser) {
    let map = this.#browserTokenMap;
    if (!map.has(browser)) {
      map.set(browser, 0);
    }
    return {
      token: map.get(browser),
      get isCurrent() {
        // At this point, the browser may have been destructed and unbound but
        // its outer ID not removed from the map because outer-window-destroyed
        // hasn't been received yet.  In that case, the browser is unusable, it
        // has no properties, so return false.  Check for this case by getting a
        // property, say, docShell.
        return map.get(browser) === this.token && browser.mInitialized;
      },
    };
  }

  /**
   * Returns the browser that the supplied zoom event is associated with.
   *
   * @param event  The zoom event.
   * @return  The associated browser element, if one exists, otherwise null.
   */
  #getTargetedBrowser(event) {
    let target = event.originalTarget;

    // With remote content browsers, the event's target is the browser
    // we're looking for.
    const XUL_NS =
      "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    if (
      this.documentGlobal.XULElement.isInstance(target) &&
      target.localName == "browser" &&
      target.namespaceURI == XUL_NS
    ) {
      return target;
    }

    // With in-process content browsers, the event's target is the content
    // document.
    if (target.nodeType == Node.DOCUMENT_NODE) {
      return target.documentGlobal.docShell.chromeEventHandler;
    }

    throw new Error("Unexpected zoom event source");
  }

  /**
   * Increments the zoom change token for the given browser so that pending
   * async operations know that it may be unsafe to access they zoom when they
   * finish.
   *
   * @param browser  Pending accesses in this browser will be ignored.
   */
  #ignorePendingZoomAccesses(browser) {
    let map = this.#browserTokenMap;
    map.set(browser, (map.get(browser) || 0) + 1);
  }

  #ensureValid(aValue) {
    // Note that undefined is a valid value for aValue that indicates a known-
    // not-to-exist value.
    if (isNaN(aValue)) {
      return 1;
    }

    if (aValue < this.documentGlobal.ZoomManager.MIN) {
      return this.documentGlobal.ZoomManager.MIN;
    }

    if (aValue > this.documentGlobal.ZoomManager.MAX) {
      return this.documentGlobal.ZoomManager.MAX;
    }

    return aValue;
  }

  /**
   * Gets the load context from the given Browser.
   *
   * @param Browser  The Browser whose load context will be returned.
   * @return        The nsILoadContext of the given Browser.
   */
  #loadContextFromBrowser(browser) {
    return browser.loadContext;
  }

  /**
   * Asynchronously broadcasts "browser-fullZoom:location-change" so that
   * listeners can be notified when the zoom levels on those pages change.
   * The notification is always asynchronous so that observers are guaranteed a
   * consistent behavior.
   */
  #notifyOnLocationChange(browser) {
    this.#executeSoon(function () {
      Services.obs.notifyObservers(browser, "browser-fullZoom:location-change");
    });
  }

  #executeSoon(callback) {
    if (!callback) {
      return;
    }
    Services.tm.dispatchToMainThread(callback);
  }

  #isPDFViewer(browser) {
    return !!(
      browser.contentPrincipal.spec == "resource://pdf.js/web/viewer.html"
    );
  }
}
