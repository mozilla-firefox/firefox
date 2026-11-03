/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewModule } from "resource://gre/modules/GeckoViewModule.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "OverrideService",
  "@mozilla.org/security/certoverride;1",
  Ci.nsICertOverrideService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserTelemetryUtils: "resource://gre/modules/BrowserTelemetryUtils.sys.mjs",
  GleanStopwatch: "resource://gre/modules/GeckoViewTelemetry.sys.mjs",
  QWACs: "moz-src:///security/manager/ssl/QWACs.sys.mjs",
});

var IdentityHandler = {
  // The definitions below should be kept in sync with those in GeckoView.ProgressListener.SecurityInformation
  // No trusted identity information. No site identity icon is shown.
  IDENTITY_MODE_UNKNOWN: 0,

  // Domain-Validation SSL CA-signed domain verification (DV).
  IDENTITY_MODE_IDENTIFIED: 1,

  // Extended-Validation SSL CA-signed identity information (EV). A more rigorous validation process.
  IDENTITY_MODE_VERIFIED: 2,

  // The following mixed content modes are only used if "security.mixed_content.block_active_content"
  // is enabled. Our Java frontend coalesces them into one indicator.

  // No mixed content information. No mixed content icon is shown.
  MIXED_MODE_UNKNOWN: 0,

  // Blocked active mixed content.
  MIXED_MODE_CONTENT_BLOCKED: 1,

  // Loaded active mixed content.
  MIXED_MODE_CONTENT_LOADED: 2,

  /**
   * Determines the identity mode corresponding to the icon we show in the urlbar.
   */
  getIdentityMode: function getIdentityMode(aState) {
    if (aState & Ci.nsIWebProgressListener.STATE_IDENTITY_EV_TOPLEVEL) {
      return this.IDENTITY_MODE_VERIFIED;
    }

    if (aState & Ci.nsIWebProgressListener.STATE_IS_SECURE) {
      return this.IDENTITY_MODE_IDENTIFIED;
    }

    return this.IDENTITY_MODE_UNKNOWN;
  },

  getMixedDisplayMode: function getMixedDisplayMode(aState) {
    if (aState & Ci.nsIWebProgressListener.STATE_LOADED_MIXED_DISPLAY_CONTENT) {
      return this.MIXED_MODE_CONTENT_LOADED;
    }

    if (
      aState & Ci.nsIWebProgressListener.STATE_BLOCKED_MIXED_DISPLAY_CONTENT
    ) {
      return this.MIXED_MODE_CONTENT_BLOCKED;
    }

    return this.MIXED_MODE_UNKNOWN;
  },

  getMixedActiveMode: function getActiveDisplayMode(aState) {
    // Only show an indicator for loaded mixed content if the pref to block it is enabled
    if (
      aState & Ci.nsIWebProgressListener.STATE_LOADED_MIXED_ACTIVE_CONTENT &&
      !Services.prefs.getBoolPref("security.mixed_content.block_active_content")
    ) {
      return this.MIXED_MODE_CONTENT_LOADED;
    }

    if (aState & Ci.nsIWebProgressListener.STATE_BLOCKED_MIXED_ACTIVE_CONTENT) {
      return this.MIXED_MODE_CONTENT_BLOCKED;
    }

    return this.MIXED_MODE_UNKNOWN;
  },

  /**
   * Determine the identity of the page being displayed by examining its SSL cert
   * (if available). Return the data needed to update the UI.
   */
  checkIdentity: function checkIdentity(aState, aBrowser) {
    const identityMode = this.getIdentityMode(aState);
    const mixedDisplay = this.getMixedDisplayMode(aState);
    const mixedActive = this.getMixedActiveMode(aState);
    const result = {
      mode: {
        identity: identityMode,
        mixed_display: mixedDisplay,
        mixed_active: mixedActive,
      },
    };

    if (aBrowser.contentPrincipal) {
      result.origin = aBrowser.contentPrincipal.originNoSuffix;
    }

    // Don't show identity data for pages with an unknown identity or if any
    // mixed content is loaded (mixed display content is loaded by default).
    if (
      identityMode === this.IDENTITY_MODE_UNKNOWN ||
      aState & Ci.nsIWebProgressListener.STATE_IS_BROKEN ||
      aState & Ci.nsIWebProgressListener.STATE_IS_INSECURE
    ) {
      result.secure = false;
      return result;
    }

    result.secure = true;

    let uri = aBrowser.currentURI || {};
    try {
      uri = Services.io.createExposableURI(uri);
    } catch (e) {}

    try {
      result.host = lazy.IDNService.convertToDisplayIDN(uri.host);
    } catch (e) {
      result.host = uri.host;
    }

    const cert = aBrowser.securityUI.secInfo.serverCert;

    result.certificate =
      aBrowser.securityUI.secInfo.serverCert.getBase64DERString();

    try {
      result.securityException = lazy.OverrideService.hasMatchingOverride(
        uri.host,
        uri.port,
        {},
        cert,
        {}
      );

      // If an override exists, the connection is being allowed but should not
      // be considered secure.
      result.secure = !result.securityException;
    } catch (e) {}

    return result;
  },

  qwacStatus(aState, aBrowser, aCallback) {
    if (
      aState != Ci.nsIWebProgressListener.STATE_IS_SECURE ||
      !aBrowser.securityUI.secInfo
    ) {
      aCallback.onSuccess(null);
      return;
    }
    lazy.QWACs.determineQWACStatus(
      aBrowser.securityUI.secInfo,
      aBrowser.currentURI,
      aBrowser.browsingContext
    ).then(qwac => {
      aCallback.onSuccess(qwac?.getBase64DERString());
    });
  },
};

class Tracker {
  constructor(aModule) {
    this._module = aModule;
  }

  get eventDispatcher() {
    return this._module.eventDispatcher;
  }

  get browser() {
    return this._module.browser;
  }

  QueryInterface = ChromeUtils.generateQI(["nsIWebProgressListener"]);
}

class ProgressTracker extends Tracker {
  constructor(aModule) {
    super(aModule);

    this.pageLoadStopwatch = new lazy.GleanStopwatch(
      Glean.geckoview.pageLoadTime
    );
    this.pageReloadStopwatch = new lazy.GleanStopwatch(
      Glean.geckoview.pageReloadTime
    );
    this.pageLoadProgressStopwatch = new lazy.GleanStopwatch(
      Glean.geckoview.pageLoadProgressTime
    );

    this.clear();
    this._eventReceived = null;
  }

  start(aUri) {
    debug`ProgressTracker start ${aUri}`;

    if (this._eventReceived) {
      // A request was already in process, let's cancel it
      this.stop(/* isSuccess */ false);
    }

    this._eventReceived = new Set();
    this.clear();
    const data = this._data;

    if (aUri === "about:blank") {
      data.uri = null;
      return;
    }

    this.pageLoadProgressStopwatch.start();

    data.uri = aUri;
    data.pageStart = true;
    this.updateProgress();
  }

  changeLocation(aUri) {
    debug`ProgressTracker changeLocation ${aUri}`;

    const data = this._data;
    data.locationChange = true;
    data.uri = aUri;
  }

  stop(aIsSuccess) {
    debug`ProgressTracker stop`;

    if (!this._eventReceived) {
      // No request in progress
      return;
    }

    if (aIsSuccess) {
      this.pageLoadProgressStopwatch.finish();
    } else {
      this.pageLoadProgressStopwatch.cancel();
    }

    const data = this._data;
    data.pageStop = true;
    this.updateProgress();
    this._eventReceived = null;
  }

  onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
    debug`ProgressTracker onStateChange: isTopLevel=${aWebProgress.isTopLevel},
                          flags=${aStateFlags}, status=${aStatus}`;

    if (!aWebProgress || !aWebProgress.isTopLevel) {
      return;
    }

    const { displaySpec } = aRequest.QueryInterface(Ci.nsIChannel).URI;

    if (aRequest.URI.schemeIs("about")) {
      return;
    }

    debug`ProgressTracker onStateChange: uri=${displaySpec}`;

    const isPageReload =
      (aWebProgress.loadType & Ci.nsIDocShell.LOAD_CMD_RELOAD) != 0;
    const stopwatch = isPageReload
      ? this.pageReloadStopwatch
      : this.pageLoadStopwatch;

    const isStart = (aStateFlags & Ci.nsIWebProgressListener.STATE_START) != 0;
    const isStop = (aStateFlags & Ci.nsIWebProgressListener.STATE_STOP) != 0;
    const isRedirecting =
      (aStateFlags & Ci.nsIWebProgressListener.STATE_REDIRECTING) != 0;

    if (isStart) {
      stopwatch.start();
      this.start(displaySpec);
    } else if (isStop && !aWebProgress.isLoadingDocument) {
      stopwatch.finish();
      this.stop(aStatus == Cr.NS_OK);
    } else if (isRedirecting) {
      stopwatch.start();
      this.start(displaySpec);
    }

    // During history naviation, global window is recycled, so pagetitlechanged isn't fired
    // Although Firefox Desktop always set title by onLocationChange, to reduce title change call,
    // we only send title during history navigation.
    if ((aStateFlags & Ci.nsIWebProgressListener.STATE_RESTORING) != 0) {
      this.eventDispatcher.sendRequest("GeckoView:PageTitleChanged", {
        title: this.browser.contentTitle,
      });
    }
  }

  onLocationChange(aWebProgress, aRequest, aLocationURI, aFlags) {
    if (
      !aWebProgress ||
      !aWebProgress.isTopLevel ||
      !aLocationURI ||
      aLocationURI.schemeIs("about")
    ) {
      return;
    }

    debug`ProgressTracker onLocationChange: location=${aLocationURI.displaySpec},
                             flags=${aFlags}`;

    if (aFlags & Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE) {
      this.stop(/* isSuccess */ false);
    } else {
      this.changeLocation(aLocationURI.displaySpec);
    }
  }

  handleEvent(aEvent) {
    if (!this._eventReceived || this._eventReceived.has(aEvent.name)) {
      // Either we're not tracking or we have received this event already
      return;
    }

    const data = this._data;

    if (!data.uri || data.uri !== aEvent.data?.uri) {
      return;
    }

    debug`ProgressTracker handleEvent: ${aEvent.name}`;

    let needsUpdate = false;

    switch (aEvent.name) {
      case "DOMContentLoaded":
        needsUpdate = needsUpdate || !data.parsed;
        // if page_load.progressbar_completion is set to 1, we complete the page load progress when
        // DOMContentLoaded is received.
        if (this._progressBarCompletion == 1) {
          data.completeProgress = true;
        } else {
          data.parsed = true;
        }
        break;
      case "MozAfterPaint":
        needsUpdate = needsUpdate || !data.firstPaint;
        // if page_load.progressbar_completion is set to 2, we complete the page load progress at
        // the first MozAfterPaint after DOMContentLoaded is received.
        if (this._progressBarCompletion == 2 && data.parsed) {
          data.completeProgress = true;
        } else {
          data.firstPaint = true;
        }
        break;
      case "pageshow":
        needsUpdate = needsUpdate || !data.pageShow;
        data.pageShow = true;
        break;
    }

    this._eventReceived.add(aEvent.name);

    if (needsUpdate) {
      this.updateProgress();
    }
  }

  clear() {
    this._data = {
      prev: 0,
      uri: null,
      locationChange: false,
      pageStart: false,
      pageStop: false,
      firstPaint: false,
      pageShow: false,
      parsed: false,
      completeProgress: false,
    };
    this._progressBarCompletion = Services.prefs.getIntPref(
      "page_load.progressbar_completion"
    );
  }

  _debugData() {
    return {
      prev: this._data.prev,
      uri: this._data.uri,
      locationChange: this._data.locationChange,
      pageStart: this._data.pageStart,
      pageStop: this._data.pageStop,
      firstPaint: this._data.firstPaint,
      pageShow: this._data.pageShow,
      parsed: this._data.parsed,
    };
  }

  updateProgress() {
    debug`ProgressTracker updateProgress`;

    const data = this._data;

    if (!this._eventReceived || !data.uri) {
      return;
    }

    let progress = 0;
    if (data.pageStop || data.pageShow || data.completeProgress) {
      progress = 100;
    } else if (data.firstPaint) {
      progress = 80;
    } else if (data.parsed) {
      progress = 55;
    } else if (data.locationChange) {
      progress = 30;
    } else if (data.pageStart) {
      progress = 15;
    }

    if (data.prev >= progress) {
      return;
    }

    debug`ProgressTracker updateProgress data=${this._debugData()}
           progress=${progress}`;

    this.eventDispatcher.sendRequest("GeckoView:ProgressChanged", {
      progress,
    });

    data.prev = progress;
  }
}

class StateTracker extends Tracker {
  constructor(aModule) {
    super(aModule);
    this._inProgress = false;
    this._uri = null;
  }

  start(aUri) {
    this._inProgress = true;
    this._uri = aUri;
    this.eventDispatcher.sendRequest("GeckoView:PageStart", {
      uri: aUri,
    });
  }

  stop(aIsSuccess) {
    if (!this._inProgress) {
      // No request in progress
      return;
    }

    this._inProgress = false;
    this._uri = null;

    this.eventDispatcher.sendRequest("GeckoView:PageStop", {
      success: aIsSuccess,
    });

    lazy.BrowserTelemetryUtils.recordSiteOriginTelemetry(
      Services.wm.getEnumerator("navigator:geckoview"),
      true
    );
  }

  onStateChange(aWebProgress, aRequest, aStateFlags, aStatus) {
    debug`StateTracker onStateChange: isTopLevel=${aWebProgress.isTopLevel},
                          flags=${aStateFlags}, status=${aStatus}
                          loadType=${aWebProgress.loadType}`;

    if (!aWebProgress.isTopLevel) {
      return;
    }

    const { displaySpec } = aRequest.QueryInterface(Ci.nsIChannel).URI;
    const isStart = (aStateFlags & Ci.nsIWebProgressListener.STATE_START) != 0;
    const isStop = (aStateFlags & Ci.nsIWebProgressListener.STATE_STOP) != 0;

    if (isStart) {
      this.start(displaySpec);
    } else if (isStop && !aWebProgress.isLoadingDocument) {
      this.stop(aStatus == Cr.NS_OK);
    }
  }
}

class SecurityTracker extends Tracker {
  constructor(aModule) {
    super(aModule);
    this._hostChanged = false;
  }

  onLocationChange(aWebProgress, aRequest, aLocationURI, aFlags) {
    debug`SecurityTracker onLocationChange: location=${aLocationURI.displaySpec},
                             flags=${aFlags}`;

    this._hostChanged = true;
  }

  onSecurityChange(aWebProgress, aRequest, aState) {
    debug`onSecurityChange`;

    // Don't need to do anything if the data we use to update the UI hasn't changed
    if (this._state === aState && !this._hostChanged) {
      return;
    }

    this._state = aState;
    this._hostChanged = false;

    const identity = IdentityHandler.checkIdentity(aState, this.browser);

    this.eventDispatcher.sendRequest("GeckoView:SecurityChanged", {
      identity,
    });
  }

  qwacStatus(aCallback) {
    IdentityHandler.qwacStatus(this._state, this.browser, aCallback);
  }
}

export class GeckoViewProgress extends GeckoViewModule {
  onEnable() {
    debug`onEnable`;

    this._fireInitialLoad();
    this._initialAboutBlank = true;

    this._progressTracker = new ProgressTracker(this);
    this._securityTracker = new SecurityTracker(this);
    this._stateTracker = new StateTracker(this);

    const flags =
      Ci.nsIWebProgress.NOTIFY_STATE_NETWORK |
      Ci.nsIWebProgress.NOTIFY_SECURITY |
      Ci.nsIWebProgress.NOTIFY_LOCATION;
    this.progressFilter = Cc[
      "@mozilla.org/appshell/component/browser-status-filter;1"
    ].createInstance(Ci.nsIWebProgress);
    this.progressFilter.addProgressListener(this, flags);
    this.browser.addProgressListener(this.progressFilter, flags);
    Services.obs.addObserver(this, "oop-frameloader-crashed");
    this.registerListener("GeckoView:GetQWACStatus");
  }

  onDisable() {
    debug`onDisable`;

    if (this.progressFilter) {
      this.progressFilter.removeProgressListener(this);
      this.browser.removeProgressListener(this.progressFilter);
    }

    Services.obs.removeObserver(this, "oop-frameloader-crashed");
    this.unregisterListener("GeckoView:GetQWACStatus");
  }

  receiveMessage(aMsg) {
    debug`receiveMessage: ${aMsg.name}`;

    switch (aMsg.name) {
      case "DOMContentLoaded": // fall-through
      case "MozAfterPaint": // fall-through
      case "pageshow": {
        this._progressTracker?.handleEvent(aMsg);
        if (aMsg.name === "pageshow") {
          const nav = this.moduleManager._applinkNavigation;
          if (nav) {
            this.moduleManager._applinkNavigation = null;
            nav.resolve();
          }
        }
        break;
      }
    }
  }

  onEvent(aEvent, aData, aCallback) {
    debug`onEvent: event=${aEvent}, data=${aData}`;

    switch (aEvent) {
      case "GeckoView:GetQWACStatus":
        this._securityTracker.qwacStatus(aCallback);
        break;
    }
  }

  onStateChange(...args) {
    // GeckoView never gets PageStart or PageStop for about:blank because we
    // set nodefaultsrc to true unconditionally so we can assume here that
    // we're starting a page load for a non-blank page (or a consumer-initiated
    // about:blank load).
    this._initialAboutBlank = false;

    this._progressTracker.onStateChange(...args);
    this._stateTracker.onStateChange(...args);
  }

  onSecurityChange(...args) {
    // We don't report messages about the initial about:blank
    if (this._initialAboutBlank) {
      return;
    }

    this._securityTracker.onSecurityChange(...args);
  }

  onLocationChange(...args) {
    this._securityTracker.onLocationChange(...args);
    this._progressTracker.onLocationChange(...args);
  }

  // The initial about:blank load events are unreliable because docShell starts
  // up concurrently with loading geckoview.js so we're never guaranteed to get
  // the events.
  // What we do instead is ignore all initial about:blank events and fire them
  // manually once the child process has booted up.
  _fireInitialLoad() {
    this.eventDispatcher.sendRequest("GeckoView:PageStart", {
      uri: "about:blank",
    });
    this.eventDispatcher.sendRequest("GeckoView:LocationChange", {
      uri: "about:blank",
      canGoBack: false,
      canGoForward: false,
      isTopLevel: true,
      hasUserGesture: false,
    });
    this.eventDispatcher.sendRequest("GeckoView:PageStop", {
      success: true,
    });
  }

  // nsIObserver event handler
  observe(aSubject, aTopic) {
    debug`observe: topic=${aTopic}`;

    switch (aTopic) {
      case "oop-frameloader-crashed": {
        const browser = aSubject.ownerElement;
        if (!browser || browser != this.browser) {
          return;
        }

        this._progressTracker?.stop(/* isSuccess */ false);
        this._stateTracker?.stop(/* isSuccess */ false);
      }
    }
  }
}

const { debug, warn } = GeckoViewProgress.initLogging("GeckoViewProgress");
