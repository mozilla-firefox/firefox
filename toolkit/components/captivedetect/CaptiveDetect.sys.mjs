/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const DEBUG = false; // set to true to show debug messages

const kCAPTIVEPORTALDETECTOR_CID = Components.ID(
  "{d9cd00ba-aa4d-47b1-8792-b1fe0cd35060}"
);

const kOpenCaptivePortalLoginEvent = "captive-portal-login";
const kAbortCaptivePortalLoginEvent = "captive-portal-login-abort";
const kCaptivePortalLoginSuccessEvent = "captive-portal-login-success";
const kCaptivePortalCheckComplete = "captive-portal-check-complete";

const lazy = {};

// Both of these are read on every check rather than cached, so that a change
// to either - by a user, an enterprise policy or a Nimbus rollout - takes
// effect without a restart, and so the two can never be observed out of sync
// with each other. See bug 2052715.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "canonicalSiteURL",
  "captivedetect.canonicalURL",
  ""
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "canonicalSiteExpectedContent",
  "captivedetect.canonicalContent",
  ""
);

function URLFetcher(url, timeout) {
  let self = this;
  let xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  // Prevent the request from reading from the cache.
  xhr.channel.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
  // Prevent the request from writing to the cache.
  xhr.channel.loadFlags |= Ci.nsIRequest.INHIBIT_CACHING;
  // Prevent privacy leaks
  xhr.channel.loadFlags |= Ci.nsIRequest.LOAD_ANONYMOUS;
  // If the TCP flow is pinned to the captive portal then we need to use a
  // fresh connection for each check in order to detect logged in events.
  xhr.channel.loadFlags |= Ci.nsIRequest.LOAD_FRESH_CONNECTION;
  xhr.channel.loadFlags |= Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  // Use the system's resolver for this check
  xhr.channel.setTRRMode(Ci.nsIRequest.TRR_DISABLED_MODE);
  // We except this from being classified
  xhr.channel.loadFlags |= Ci.nsIChannel.LOAD_BYPASS_URL_CLASSIFIER;
  // Prevent HTTPS-Only Mode from upgrading the request.
  xhr.channel.loadInfo.httpsOnlyStatus |= Ci.nsILoadInfo.HTTPS_ONLY_EXEMPT;
  // Allow deprecated HTTP request from SystemPrincipal
  xhr.channel.loadInfo.allowDeprecatedSystemRequests = true;

  // We don't want to follow _any_ redirects
  xhr.channel.QueryInterface(Ci.nsIHttpChannel).redirectionLimit = 0;

  // bug 1666072 - firefox.com returns a HSTS header triggering a https upgrade
  // but the upgrade triggers an internal redirect causing an incorrect locked
  // portal notification. We exclude CP detection from STS.
  xhr.channel.QueryInterface(Ci.nsIHttpChannel).allowSTS = false;

  // The Cache-Control header is only interpreted by proxies and the
  // final destination. It does not help if a resource is already
  // cached locally.
  xhr.setRequestHeader("Cache-Control", "no-cache");
  // HTTP/1.0 servers might not implement Cache-Control and
  // might only implement Pragma: no-cache
  xhr.setRequestHeader("Pragma", "no-cache");

  xhr.timeout = timeout;
  xhr.ontimeout = function () {
    self.ontimeout();
  };
  xhr.onerror = function () {
    self.onerror();
  };
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4) {
      if (self._isAborted) {
        return;
      }
      if (xhr.status === 200 || xhr.status === 204) {
        self.onsuccess(xhr.status, xhr.responseText);
      } else if (xhr.status) {
        self.onredirectorerror(xhr.status);
      } else if (
        xhr.channel &&
        xhr.channel.status == Cr.NS_ERROR_REDIRECT_LOOP
      ) {
        // For some redirects we don't get a status, so we need to check it
        // this way. This only works because we set the redirectionLimit to 0.
        self.onredirectorerror(300);
        // No need to invoke the onerror callback, we handled it here.
        xhr.onerror = null;
      }
    }
  };
  xhr.send();
  this._xhr = xhr;
}

URLFetcher.prototype = {
  _isAborted: false,
  ontimeout() {},
  onerror() {},
  abort() {
    if (!this._isAborted) {
      this._isAborted = true;
      this._xhr.abort();
    }
  },
  usedProxy() {
    try {
      if (
        this._xhr &&
        this._xhr.channel &&
        this._xhr.channel.QueryInterface(Ci.nsIHttpChannelInternal).isProxyUsed
      ) {
        return true;
      }
    } catch (e) {}

    return false;
  },
};

function LoginObserver(captivePortalDetector) {
  const LOGIN_OBSERVER_STATE_DETACHED = 0; /* Should not monitor network activity since no ongoing login procedure */
  const LOGIN_OBSERVER_STATE_IDLE = 1; /* No network activity currently, waiting for a longer enough idle period */
  const LOGIN_OBSERVER_STATE_BURST = 2; /* Network activity is detected, probably caused by a login procedure */
  const LOGIN_OBSERVER_STATE_VERIFY_NEEDED = 3; /* Verifing network accessiblity is required after a long enough idle */
  const LOGIN_OBSERVER_STATE_VERIFYING = 4; /* LoginObserver is probing if public network is available */

  let state = LOGIN_OBSERVER_STATE_DETACHED;

  let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  let activityDistributor = Cc[
    "@mozilla.org/network/http-activity-distributor;1"
  ].getService(Ci.nsIHttpActivityDistributor);
  let urlFetcher = null;

  let pageCheckingDone = function pageCheckingDone() {
    if (state === LOGIN_OBSERVER_STATE_VERIFYING) {
      urlFetcher = null;
      // Finish polling the canonical site, switch back to idle state and
      // waiting for next burst
      state = LOGIN_OBSERVER_STATE_IDLE;
      timer.initWithCallback(
        observer,
        captivePortalDetector._pollingTime,
        timer.TYPE_ONE_SHOT
      );
    }
  };

  let checkPageContent = function checkPageContent() {
    debug("checking if public network is available after the login procedure");

    urlFetcher = new URLFetcher(
      captivePortalDetector._canonicalSiteURL,
      captivePortalDetector._maxWaitingTime
    );
    urlFetcher.ontimeout = pageCheckingDone;
    urlFetcher.onerror = pageCheckingDone;
    urlFetcher.onsuccess = function (status, content) {
      if (captivePortalDetector.validateContent(status, content)) {
        urlFetcher = null;
        captivePortalDetector.executeCallback(true);
      } else {
        pageCheckingDone();
      }
    };
    urlFetcher.onredirectorerror = pageCheckingDone;
  };

  // Public interface of LoginObserver
  let observer = {
    QueryInterface: ChromeUtils.generateQI([
      "nsIHttpActivityObserver",
      "nsITimerCallback",
    ]),

    attach: function attach() {
      if (state === LOGIN_OBSERVER_STATE_DETACHED) {
        activityDistributor.addObserver(this);
        state = LOGIN_OBSERVER_STATE_IDLE;
        timer.initWithCallback(
          this,
          captivePortalDetector._pollingTime,
          timer.TYPE_ONE_SHOT
        );
        debug("attach HttpObserver for login activity");
      }
    },

    detach: function detach() {
      if (state !== LOGIN_OBSERVER_STATE_DETACHED) {
        if (urlFetcher) {
          urlFetcher.abort();
          urlFetcher = null;
        }
        activityDistributor.removeObserver(this);
        timer.cancel();
        state = LOGIN_OBSERVER_STATE_DETACHED;
        debug("detach HttpObserver for login activity");
      }
    },

    /*
     * Treat all HTTP transactions as captive portal login activities.
     */
    observeActivity: function observeActivity(
      aHttpChannel,
      aActivityType,
      aActivitySubtype
    ) {
      if (
        aActivityType ===
          Ci.nsIHttpActivityObserver.ACTIVITY_TYPE_HTTP_TRANSACTION &&
        aActivitySubtype ===
          Ci.nsIHttpActivityObserver.ACTIVITY_SUBTYPE_RESPONSE_COMPLETE
      ) {
        switch (state) {
          case LOGIN_OBSERVER_STATE_IDLE:
          case LOGIN_OBSERVER_STATE_VERIFY_NEEDED:
            state = LOGIN_OBSERVER_STATE_BURST;
            break;
          default:
            break;
        }
      }
    },

    /*
     * Check if login activity is finished according to HTTP burst.
     */
    notify: function notify() {
      switch (state) {
        case LOGIN_OBSERVER_STATE_BURST:
          // Wait while network stays idle for a short period
          state = LOGIN_OBSERVER_STATE_VERIFY_NEEDED;
        // Fall through to start polling timer
        case LOGIN_OBSERVER_STATE_IDLE:
        // Just fall through to perform a captive portal check.
        case LOGIN_OBSERVER_STATE_VERIFY_NEEDED:
          // Polling the canonical website since network stays idle for a while
          state = LOGIN_OBSERVER_STATE_VERIFYING;
          checkPageContent();
          break;

        default:
          break;
      }
    },
  };

  return observer;
}

export function CaptivePortalDetector() {
  this._maxWaitingTime = Services.prefs.getIntPref(
    "captivedetect.maxWaitingTime"
  );
  this._pollingTime = Services.prefs.getIntPref("captivedetect.pollingTime");
  this._maxRetryCount = Services.prefs.getIntPref(
    "captivedetect.maxRetryCount"
  );
  debug(
    "Load Prefs {site=" +
      this._canonicalSiteURL +
      ",content=" +
      this._canonicalSiteExpectedContent +
      ",time=" +
      this._maxWaitingTime +
      "max-retry=" +
      this._maxRetryCount +
      "}"
  );

  // Create HttpObserver for monitoring the login procedure
  this._loginObserver = LoginObserver(this);

  this._nextRequestId = 0;
  this._runningRequest = null;
  this._requestQueue = []; // Maintain a progress table, store callbacks and the ongoing XHR
  this._interfaceNames = {}; // Maintain names of the requested network interfaces

  debug(
    "CaptiveProtalDetector initiated, waiting for network connection established"
  );
}

CaptivePortalDetector.prototype = {
  classID: kCAPTIVEPORTALDETECTOR_CID,
  QueryInterface: ChromeUtils.generateQI(["nsICaptivePortalDetector"]),

  get _canonicalSiteURL() {
    return lazy.canonicalSiteURL;
  },

  get _canonicalSiteExpectedContent() {
    return lazy.canonicalSiteExpectedContent;
  },

  // nsICaptivePortalDetector
  checkCaptivePortal: function checkCaptivePortal(aInterfaceName, aCallback) {
    if (!this._canonicalSiteURL) {
      throw Components.Exception("No canonical URL set up.");
    }

    // Prevent multiple requests on a single network interface
    if (this._interfaceNames[aInterfaceName]) {
      throw Components.Exception(
        "Do not allow multiple request on one interface: " + aInterfaceName
      );
    }

    let request = { interfaceName: aInterfaceName };
    if (aCallback) {
      let callback = aCallback.QueryInterface(Ci.nsICaptivePortalCallback);
      request.callback = callback;
      request.retryCount = 0;
    }
    this._addRequest(request);
  },

  abort: function abort(aInterfaceName) {
    debug("abort for " + aInterfaceName);
    this._removeRequest(aInterfaceName);
  },

  finishPreparation: function finishPreparation(aInterfaceName) {
    debug('finish preparation phase for interface "' + aInterfaceName + '"');
    if (
      !this._runningRequest ||
      this._runningRequest.interfaceName !== aInterfaceName
    ) {
      debug("invalid finishPreparation for " + aInterfaceName);
      throw Components.Exception(
        "only first request is allowed to invoke |finishPreparation|"
      );
    }

    this._startDetection();
  },

  cancelLogin: function cancelLogin(eventId) {
    debug('login canceled by user for request "' + eventId + '"');
    // Captive portal login procedure is canceled by user
    if (
      this._runningRequest &&
      this._runningRequest.hasOwnProperty("eventId")
    ) {
      let id = this._runningRequest.eventId;
      if (eventId === id) {
        this.executeCallback(false);
      }
    }
  },

  _applyDetection: function _applyDetection() {
    debug("enter applyDetection(" + this._runningRequest.interfaceName + ")");

    // Execute network interface preparation
    if (this._runningRequest.hasOwnProperty("callback")) {
      this._runningRequest.callback.prepare();
    } else {
      this._startDetection();
    }
  },

  _startDetection: function _startDetection() {
    debug(
      "startDetection {site=" +
        this._canonicalSiteURL +
        ",content=" +
        this._canonicalSiteExpectedContent +
        ",time=" +
        this._maxWaitingTime +
        "}"
    );
    let self = this;

    let urlFetcher = new URLFetcher(
      this._canonicalSiteURL,
      this._maxWaitingTime
    );

    let mayRetry = this._mayRetry.bind(this);

    urlFetcher.ontimeout = mayRetry;
    urlFetcher.onerror = mayRetry;
    urlFetcher.onsuccess = function (status, content) {
      if (urlFetcher.usedProxy()) {
        // Don't trigger if channel used proxy.
        self.executeCallback(true);
        return;
      }
      if (self.validateContent(status, content)) {
        self.executeCallback(true);
      } else {
        // Content of the canonical website has been overwrite
        self._startLogin();
      }
    };
    urlFetcher.onredirectorerror = function (status) {
      if (urlFetcher.usedProxy()) {
        // Don't trigger if channel used proxy.
        self.executeCallback(true);
        return;
      }
      // A canonical endpoint only ever answers 200 or 204, both of which are
      // handled by onsuccess. Any other 2xx is something intercepting us.
      if (status >= 200 && status <= 299) {
        self._startLogin();
      } else if (status >= 300 && status <= 399) {
        // The canonical website has been redirected to an unknown location
        self._startLogin();
      } else if (status === 511) {
        // Got a RFC 6585 "Network Authentication Required" error page
        self._startLogin();
      } else {
        mayRetry();
      }
    };

    this._runningRequest.urlFetcher = urlFetcher;
  },

  _startLogin: function _startLogin() {
    let id = this._allocateRequestId();
    let details = {
      type: kOpenCaptivePortalLoginEvent,
      id,
      url: this._canonicalSiteURL,
    };
    this._loginObserver.attach();
    this._runningRequest.eventId = id;
    this._sendEvent(kOpenCaptivePortalLoginEvent, details);
  },

  _mayRetry: function _mayRetry() {
    if (
      this._runningRequest &&
      this._runningRequest.retryCount++ < this._maxRetryCount
    ) {
      debug(
        "retry-Detection: " +
          this._runningRequest.retryCount +
          "/" +
          this._maxRetryCount
      );
      this._startDetection();
    } else {
      this.executeCallback(false);
    }
  },

  executeCallback: function executeCallback(success) {
    if (this._runningRequest) {
      debug("callback executed");
      if (this._runningRequest.hasOwnProperty("callback")) {
        this._runningRequest.callback.complete(success);
      }

      // Only when the request has a event id and |success| is true
      // do we need to notify the login-success event.
      if (this._runningRequest.hasOwnProperty("eventId") && success) {
        let details = {
          type: kCaptivePortalLoginSuccessEvent,
          id: this._runningRequest.eventId,
        };
        this._sendEvent(kCaptivePortalLoginSuccessEvent, details);
      }

      // Continue the following request
      this._runningRequest.complete = true;
      this._removeRequest(this._runningRequest.interfaceName);
    }
  },

  _sendEvent: function _sendEvent(topic, details) {
    debug('sendEvent "' + JSON.stringify(details) + '"');
    Services.obs.notifyObservers(this, topic, JSON.stringify(details));
  },

  /**
   * Decides whether a successful response came from the canonical endpoint
   * rather than from something intercepting the request.
   *
   * Both a 200 and a 204 are accepted, as long as the body is the expected
   * content. Since a 204 carries no body, it is only ever accepted when no
   * content is expected - which is how the default endpoint is configured.
   *
   * @param {number} status The HTTP status, either 200 or 204.
   * @param {string} content The response body.
   */
  validateContent: function validateContent(status, content) {
    debug("received status " + status + " content: " + content);
    let valid =
      (status === 200 || status === 204) &&
      content === this._canonicalSiteExpectedContent;
    // We need a way to indicate that a check has been performed, and if we are
    // still in a captive portal.
    this._sendEvent(kCaptivePortalCheckComplete, !valid);
    return valid;
  },

  _allocateRequestId: function _allocateRequestId() {
    let newId = this._nextRequestId++;
    return newId.toString();
  },

  _runNextRequest: function _runNextRequest() {
    let nextRequest = this._requestQueue.shift();
    if (nextRequest) {
      this._runningRequest = nextRequest;
      this._applyDetection();
    }
  },

  _addRequest: function _addRequest(request) {
    this._interfaceNames[request.interfaceName] = true;
    this._requestQueue.push(request);
    if (!this._runningRequest) {
      this._runNextRequest();
    }
  },

  _removeRequest: function _removeRequest(aInterfaceName) {
    if (!this._interfaceNames[aInterfaceName]) {
      return;
    }

    delete this._interfaceNames[aInterfaceName];

    if (
      this._runningRequest &&
      this._runningRequest.interfaceName === aInterfaceName
    ) {
      this._loginObserver.detach();

      if (!this._runningRequest.complete) {
        // Abort the user login procedure
        if (this._runningRequest.hasOwnProperty("eventId")) {
          let details = {
            type: kAbortCaptivePortalLoginEvent,
            id: this._runningRequest.eventId,
          };
          this._sendEvent(kAbortCaptivePortalLoginEvent, details);
        }

        // Abort the ongoing HTTP request
        if (this._runningRequest.hasOwnProperty("urlFetcher")) {
          this._runningRequest.urlFetcher.abort();
        }
      }

      debug("remove running request");
      this._runningRequest = null;

      // Continue next pending reqeust if the ongoing one has been aborted
      this._runNextRequest();
      return;
    }

    // Check if a pending request has been aborted
    for (let i = 0; i < this._requestQueue.length; i++) {
      if (this._requestQueue[i].interfaceName == aInterfaceName) {
        this._requestQueue.splice(i, 1);

        debug(
          "remove pending request #" +
            i +
            ", remaining " +
            this._requestQueue.length
        );
        break;
      }
    }
  },
};

var debug;
if (DEBUG) {
  // eslint-disable-next-line no-global-assign
  debug = function (s) {
    dump("-*- CaptivePortalDetector component: " + s + "\n");
  };
} else {
  // eslint-disable-next-line no-global-assign
  debug = function () {};
}
