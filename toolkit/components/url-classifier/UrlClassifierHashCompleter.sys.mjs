/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// COMPLETE_LENGTH and PARTIAL_LENGTH copied from nsUrlClassifierDBService.h,
// they correspond to the length, in bytes, of a hash prefix and the total
// hash.
const COMPLETE_LENGTH = 32;
const PARTIAL_LENGTH = 4;

// Upper limit on the server response minimumWaitDuration
const MIN_WAIT_DURATION_MAX_VALUE = 24 * 60 * 60 * 1000;
const PREF_DEBUG_ENABLED = "browser.safebrowsing.debug";
const PREF_OHTTP_RELAY_URL = "browser.safebrowsing.realTime.ohttpRelayURL";
const PREF_GETHASH_TIMEOUT = "urlclassifier.gethash.timeout_ms";
const KEY_CHANGED_TOPIC = "safebrowsing-realtime-key-changed";
const OHTTP_KEY_PREFIX = "ohttp ";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { NetUtil } from "resource://gre/modules/NetUtil.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gDbService",
  "@mozilla.org/url-classifier/dbservice;1",
  Ci.nsIUrlClassifierDBService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gUrlUtil",
  "@mozilla.org/url-classifier/utils;1",
  Ci.nsIUrlClassifierUtils
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gObliviousHttpService",
  "@mozilla.org/network/oblivious-http-service;1",
  Ci.nsIObliviousHttpService
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "ohttpRelayURL",
  PREF_OHTTP_RELAY_URL,
  ""
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "gRealTimeKeyService",
  "@mozilla.org/url-classifier/realtime-key-service;1",
  Ci.nsIUrlClassifierRealTimeKeyService
);

let loggingEnabled = false;

function requestKey(aGethashUrl, aUseOHTTP) {
  return (aUseOHTTP ? OHTTP_KEY_PREFIX : "direct ") + aGethashUrl;
}

// Returns the HashCompleterRequest class that handles the table, and whether
// the table is a test table.
function requestTypeFor(aTableName) {
  switch (lazy.gUrlUtil.getProvider(aTableName)) {
    case "google4":
      return { RequestClass: HashCompleterRequestV4, isTesting: false };
    case "google5":
      return { RequestClass: HashCompleterRequestV5, isTesting: false };
    case "test":
      // If the table name ends with "-proto", use the V4 or V5 request.
      // Otherwise, we use the V2 request.
      if (!aTableName.endsWith("-proto")) {
        return { RequestClass: HashCompleterRequestV2, isTesting: true };
      }
      return {
        RequestClass: aTableName.includes("google5")
          ? HashCompleterRequestV5
          : HashCompleterRequestV4,
        isTesting: true,
      };
  }
  // We use the V2 request for the mozilla provider and as the fallback.
  return { RequestClass: HashCompleterRequestV2, isTesting: false };
}

// Log only if browser.safebrowsing.debug is true
function log(...stuff) {
  if (!loggingEnabled) {
    return;
  }

  var d = new Date();
  let msg = "hashcompleter: " + d.toTimeString() + ": " + stuff.join(" ");
  dump(Services.urlFormatter.trimSensitiveURLs(msg) + "\n");
}

// Map the HTTP response code to a Telemetry bucket
// https://developers.google.com/safe-browsing/developers_guide_v2?hl=en
// eslint-disable-next-line complexity
function httpStatusToBucket(httpStatus) {
  var statusBucket;
  switch (httpStatus) {
    case 100:
    case 101:
      // Unexpected 1xx return code
      statusBucket = 0;
      break;
    case 200:
      // OK - Data is available in the HTTP response body.
      statusBucket = 1;
      break;
    case 201:
    case 202:
    case 203:
    case 205:
    case 206:
      // Unexpected 2xx return code
      statusBucket = 2;
      break;
    case 204:
      // No Content - There are no full-length hashes with the requested prefix.
      statusBucket = 3;
      break;
    case 300:
    case 301:
    case 302:
    case 303:
    case 304:
    case 305:
    case 307:
    case 308:
      // Unexpected 3xx return code
      statusBucket = 4;
      break;
    case 400:
      // Bad Request - The HTTP request was not correctly formed.
      // The client did not provide all required CGI parameters.
      statusBucket = 5;
      break;
    case 401:
    case 402:
    case 405:
    case 406:
    case 407:
    case 409:
    case 410:
    case 411:
    case 412:
    case 414:
    case 415:
    case 416:
    case 417:
    case 421:
    case 426:
    case 428:
    case 429:
    case 431:
    case 451:
      // Unexpected 4xx return code
      statusBucket = 6;
      break;
    case 403:
      // Forbidden - The client id is invalid.
      statusBucket = 7;
      break;
    case 404:
      // Not Found
      statusBucket = 8;
      break;
    case 408:
      // Request Timeout
      statusBucket = 9;
      break;
    case 413:
      // Request Entity Too Large - Bug 1150334
      statusBucket = 10;
      break;
    case 500:
    case 501:
    case 510:
      // Unexpected 5xx return code
      statusBucket = 11;
      break;
    case 502:
    case 504:
    case 511:
      // Local network errors, we'll ignore these.
      statusBucket = 12;
      break;
    case 503:
      // Service Unavailable - The server cannot handle the request.
      // Clients MUST follow the backoff behavior specified in the
      // Request Frequency section.
      statusBucket = 13;
      break;
    case 505:
      // HTTP Version Not Supported - The server CANNOT handle the requested
      // protocol major version.
      statusBucket = 14;
      break;
    default:
      statusBucket = 15;
  }
  return statusBucket;
}

class FullHashMatch {
  constructor(table, hash, duration) {
    this.tableName = table;
    this.fullHash = hash;
    this.cacheDuration = duration;
  }

  QueryInterface = ChromeUtils.generateQI(["nsIFullHashMatch"]);
}

export class HashCompleter {
  // The current HashCompleterRequest in flight. Once it is started, it is set
  // to null. It may be used by multiple calls to |complete| in succession to
  // avoid creating multiple requests to the same gethash URL.
  #currentRequest = null;
  // An Array of ongoing gethash requests which is used to find requests for
  // the same hash prefix.
  #ongoingRequests = [];
  // A map of request keys to HashCompleterRequests that haven't yet begun.
  #pendingRequests = new Map();
  // A map of request keys to RequestBackoff objects.
  #backoffs = new Map();
  // Whether we have been informed of a shutdown by the shutdown event.
  #shuttingDown = false;
  // A map of request keys to next gethash time in miliseconds
  #nextGethashTimeMs = new Map();

  constructor() {
    Services.obs.addObserver(this, "quit-application");
    Services.obs.addObserver(this, KEY_CHANGED_TOPIC);
    Services.prefs.addObserver(PREF_DEBUG_ENABLED, this);

    loggingEnabled = Services.prefs.getBoolPref(PREF_DEBUG_ENABLED);
  }

  classID = Components.ID("{9111de73-9322-4bfc-8b65-2b727f3e6ec8}");
  QueryInterface = ChromeUtils.generateQI([
    "nsIUrlClassifierHashCompleter",
    "nsIRunnable",
    "nsIObserver",
    "nsISupportsWeakReference",
  ]);

  // This is mainly how the HashCompleter interacts with other components.
  // Even though it only takes one partial hash and callback, subsequent
  // calls are made into the same HTTP request by using a thread dispatch.
  complete(aPartialHash, aGethashUrl, aTableName, aUseOHTTP, aCallback) {
    if (!aGethashUrl) {
      throw Components.Exception("", Cr.NS_ERROR_NOT_INITIALIZED);
    }

    if (
      aUseOHTTP &&
      requestTypeFor(aTableName).RequestClass != HashCompleterRequestV5
    ) {
      log("OHTTP is not supported for " + aTableName + "\n");
      Services.tm.dispatchToMainThread(() =>
        aCallback.completionFinished(Cr.NS_ERROR_NOT_AVAILABLE)
      );
      return;
    }

    let key = requestKey(aGethashUrl, aUseOHTTP);

    // Check ongoing requests before creating a new HashCompleteRequest
    for (const r of this.#ongoingRequests) {
      if (r.find(aPartialHash, key, aTableName)) {
        log(
          "Merge gethash request in " +
            aTableName +
            " for prefix : " +
            btoa(aPartialHash)
        );
        r.add(aPartialHash, aCallback, aTableName);
        return;
      }
    }

    if (!this.#currentRequest) {
      this.#currentRequest = this.makeHashCompleterRequest(
        aTableName,
        aGethashUrl,
        aUseOHTTP
      );
    }
    if (this.#currentRequest.key == key) {
      this.#currentRequest.add(aPartialHash, aCallback, aTableName);
    } else {
      if (!this.#pendingRequests.has(key)) {
        this.#pendingRequests.set(
          key,
          this.makeHashCompleterRequest(aTableName, aGethashUrl, aUseOHTTP)
        );
      }
      this.#pendingRequests.get(key).add(aPartialHash, aCallback, aTableName);
    }

    if (!this.#backoffs.has(key)) {
      // Initialize request backoffs separately, since requests are deleted
      // after they are dispatched.
      var jslib =
        Cc["@mozilla.org/url-classifier/jslib;1"].getService().wrappedJSObject;

      // Using the V4 backoff algorithm for both V2 and V4. See bug 1273398.
      this.#backoffs.set(
        key,
        new jslib.RequestBackoffV4(
          10 /* keep track of max requests */,
          0 /* don't throttle on successful requests per time period */,
          lazy.gUrlUtil.getProvider(aTableName) /* used by testcase */
        )
      );
    }

    if (!this.#nextGethashTimeMs.has(key)) {
      this.#nextGethashTimeMs.set(key, 0);
    }

    // Start off this request. Without dispatching to a thread, every call to
    // complete makes an individual HTTP request.
    Services.tm.dispatchToMainThread(this);
  }

  // A helper function to create a HashCompleterRequest based on the table name.
  makeHashCompleterRequest(aTableName, aGethashUrl, aUseOHTTP) {
    let { RequestClass, isTesting } = requestTypeFor(aTableName);
    return new RequestClass(this, aGethashUrl, isTesting, aUseOHTTP);
  }

  get shuttingDown() {
    return this.#shuttingDown;
  }

  // This is called after several calls to |complete|, or after the
  // currentRequest has finished.  It starts off the HTTP request by making a
  // |begin| call to the HashCompleterRequest.
  run() {
    // Clear everything on shutdown
    if (this.#shuttingDown) {
      this.#currentRequest = null;
      this.#pendingRequests.clear();
      this.#nextGethashTimeMs.clear();
      this.#backoffs.clear();
      throw Components.Exception("", Cr.NS_ERROR_NOT_INITIALIZED);
    }

    // If we don't have an in-flight request, make one
    let pendingKeys = Array.from(this.#pendingRequests.keys());
    if (!this.#currentRequest && pendingKeys.length) {
      let nextKey = pendingKeys[0];
      this.#currentRequest = this.#pendingRequests.get(nextKey);
      this.#pendingRequests.delete(nextKey);
    }

    if (this.#currentRequest) {
      try {
        if (this.#currentRequest.begin()) {
          this.#ongoingRequests.push(this.#currentRequest);
        }
      } finally {
        // If |begin| fails, we should get rid of our request.
        this.#currentRequest = null;
      }
    }
  }

  // Pass the server response status to the RequestBackoff for the given
  // request key and fetch the next pending request, if there is one.
  finishRequest(aRequest, aStatus) {
    this.#ongoingRequests = this.#ongoingRequests.filter(v => v != aRequest);

    this.#backoffs.get(aRequest.key).noteServerResponse(aStatus);
    Services.tm.dispatchToMainThread(this);
  }

  // Returns true if we can make a request for the given key, false otherwise.
  canMakeRequest(aKey) {
    return (
      this.#backoffs.get(aKey).canMakeRequest() &&
      Date.now() >= this.#nextGethashTimeMs.get(aKey)
    );
  }

  // Notifies the RequestBackoff of a new request so we can throttle based on
  // max requests/time period. This must be called before a channel is opened,
  // and finishRequest must be called once the response is received.
  noteRequest(aKey) {
    return this.#backoffs.get(aKey).noteRequest();
  }

  // Remove a request from ongoing requests
  removeFromOngoingRequests(request) {
    this.#ongoingRequests = this.#ongoingRequests.filter(v => v != request);
  }

  // Set the next gethash time for a request key
  setNextGethashTime(aKey, time) {
    this.#nextGethashTimeMs.set(aKey, time);
  }

  observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "quit-application":
        this.#shuttingDown = true;
        Services.obs.removeObserver(this, "quit-application");
        break;
      case KEY_CHANGED_TOPIC:
        // OHTTP requests encrypted with a stale key fail and back off for at
        // least 15 minutes. Reset those backoffs when a new key arrives so the
        // requests can resume right away.
        for (let [key, backoff] of this.#backoffs) {
          if (key.startsWith(OHTTP_KEY_PREFIX)) {
            backoff.reset();
          }
        }
        break;
      case "nsPref:changed":
        if (aData == PREF_DEBUG_ENABLED) {
          loggingEnabled = Services.prefs.getBoolPref(PREF_DEBUG_ENABLED);
        }
        break;
    }
  }
}

class HashCompleterRequestBase {
  // HashCompleter object that created this HashCompleterRequestBase.
  completer;
  // nsIChannel that the hash completion query is transmitted over.
  channel = null;
  // The internal set of hashes and callbacks that this request corresponds to.
  requests = [];
  // Response body of hash completion. Created in onDataAvailable.
  response = "";
  // Whether we have been informed of a shutdown by the quit-application event.
  #shuttingDown = false;

  constructor(aCompleter, aGethashUrl, aUseOHTTP = false) {
    this.completer = aCompleter;
    this.gethashUrl = aGethashUrl;
    this.useOHTTP = aUseOHTTP;
    this.key = requestKey(aGethashUrl, aUseOHTTP);

    // Multiple partial hashes can be associated with the same tables
    // so we use a map here.
    this.tableNames = new Map();

    this.telemetryProvider = "";
    this.telemetryClockStart = 0;
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIRequestObserver",
    "nsIStreamListener",
    "nsIObserver",
    "nsITimerCallback",
  ]);

  // This is called by the HashCompleter to add a hash and callback to the
  // HashCompleterRequest. It must be called before calling |begin|.
  add(aPartialHash, aCallback, aTableName) {
    this.requests.push({
      partialHash: aPartialHash,
      callback: aCallback,
      tableName: aTableName,
      response: { matches: [] },
    });

    if (!aTableName) {
      return;
    }

    let providerFromTableName = lazy.gUrlUtil.getProvider(aTableName);
    if (providerFromTableName != this.provider) {
      log(
        "ERROR: Cannot mix tables with different providers within " +
          "the same gethash URL."
      );
    }

    if (!this.tableNames.has(aTableName)) {
      this.tableNames.set(aTableName);
    }

    // Get the telemetry provider from the table name.
    if (this.telemetryProvider == "") {
      this.telemetryProvider = lazy.gUrlUtil.getTelemetryProvider(aTableName);
    }
  }

  // This is called by the HashCompleter to find if a partial hash is already
  // added to the request.
  find(aPartialHash, aKey, aTableName) {
    if (this.key != aKey || !this.tableNames.has(aTableName)) {
      return false;
    }

    return this.requests.find(function (r) {
      return r.partialHash === aPartialHash;
    });
  }

  // This initiates the HTTP request. It can fail due to backoff timings and
  // will notify all callbacks as necessary. We notify the backoff object on
  // begin.
  begin() {
    if (!this.completer.canMakeRequest(this.key)) {
      log("Can't make request to " + this.gethashUrl + "\n");
      this.notifyFailure(Cr.NS_ERROR_ABORT);
      return false;
    }

    this.beginBuildChannel();

    return true;
  }

  // This should be implemented by the subclasses to build the channel for
  // completing the find full hash request.
  beginBuildChannel() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  notify() {
    // If we haven't gotten onStopRequest, just cancel. This will call us
    // with onStopRequest since we implement nsIStreamListener on the
    // channel.
    if (this.channel && this.channel.isPending()) {
      log("cancelling request to " + this.gethashUrl + " (timeout)\n");
      Glean.urlclassifier.completeTimeout
        .get(this.telemetryProvider, true)
        .add(1);
      this.channel.cancel(Cr.NS_BINDING_ABORTED);
    }
  }

  // Creates an nsIChannel for the request and fills the body. If aOHTTP is
  // given, the request is sent to aOHTTP.relayURI encrypted with
  // aOHTTP.keyConfig.
  // Enforce bypassing URL Classifier check because if the request is
  // blocked, it means SafeBrowsing is malfunction.
  openChannel(aOHTTP = null) {
    let loadFlags =
      Ci.nsIChannel.INHIBIT_CACHING |
      Ci.nsIChannel.LOAD_BYPASS_CACHE |
      Ci.nsIChannel.LOAD_BYPASS_URL_CLASSIFIER;

    this.request = {
      url: this.makeChannelURL(),
      body: "",
    };

    log("actualGethashUrl: " + this.request.url);

    let channel = aOHTTP
      ? lazy.gObliviousHttpService.newChannel(
          aOHTTP.relayURI,
          Services.io.newURI(this.request.url),
          aOHTTP.keyConfig
        )
      : NetUtil.newChannel({
          uri: this.request.url,
          loadUsingSystemPrincipal: true,
        });
    channel.loadFlags = aOHTTP
      ? loadFlags | Ci.nsIRequest.LOAD_ANONYMOUS
      : loadFlags;
    channel.loadInfo.originAttributes = {
      // The firstPartyDomain value should sync with NECKO_SAFEBROWSING_FIRST_PARTY_DOMAIN
      // defined in nsNetUtil.h.
      firstPartyDomain:
        "safebrowsing.86868755-6b82-4842-b301-72671a0db32e.mozilla",
    };

    // Disable keepalive.
    if (!aOHTTP) {
      let httpChannel = channel.QueryInterface(Ci.nsIHttpChannel);
      httpChannel.setRequestHeader("Connection", "close", false);
    }

    this.channel = channel;

    this.setupChannel(channel);

    // Set a timer that cancels the channel after timeout_ms in case we
    // don't get a gethash response.
    this.timer_ = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    // Ask the timer to use nsITimerCallback (.notify()) when ready
    let timeout = Services.prefs.getIntPref(PREF_GETHASH_TIMEOUT);
    this.timer_.initWithCallback(this, timeout, this.timer_.TYPE_ONE_SHOT);
    channel.asyncOpen(this);
    Services.obs.addObserver(this, "quit-application");
    this.telemetryClockStart = Date.now();
  }

  // This should be implemented by the subclasses to build the channel URL.
  makeChannelURL() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  // This should be implemented by the subclasses to setup the channel.
  setupChannel(_channel) {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  // This should be implemented by the subclasses to handle the response.
  handleResponse() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  // This adds a complete hash to any entry in |this._requests| that matches
  // the hash.
  handleItem(aData) {
    // Only perform provider check if the table name is provided.
    if (aData.tableName) {
      let provider = lazy.gUrlUtil.getProvider(aData.tableName);
      if (provider != this.provider) {
        log(
          "Ignoring table " +
            aData.tableName +
            " since it belongs to " +
            provider +
            " while the response came from " +
            this.provider +
            "."
        );
        return;
      }
    }

    for (const request of this.requests) {
      if (aData.completeHash.startsWith(request.partialHash)) {
        request.response.matches.push(aData);
      }
    }
  }

  // notifySuccess and notifyFailure are used to alert the callbacks with
  // results. notifySuccess makes |completion| and |completionFinished| calls
  // while notifyFailure only makes a |completionFinished| call with the error
  // code.

  // This should be implemented by the subclasses to notify the success.
  notifySuccess() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }

  notifyFailure(aStatus) {
    log("notifying failure\n");
    for (const request of this.requests) {
      request.callback.completionFinished(aStatus);
    }
  }

  onDataAvailable(aRequest, aInputStream, aOffset, aCount) {
    let sis = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
      Ci.nsIScriptableInputStream
    );
    sis.init(aInputStream);
    this.response += sis.readBytes(aCount);
  }

  onStartRequest() {
    // At this point no data is available for us and we have no reason to
    // terminate the connection, so we do nothing until |onStopRequest|.
    this.completer.setNextGethashTime(this.key, 0);

    if (this.telemetryClockStart > 0) {
      let msecs = Date.now() - this.telemetryClockStart;
      Glean.urlclassifier.completeServerResponseTime[
        this.telemetryProvider
      ].accumulateSingleSample(msecs);
    }
  }

  onStopRequest(aRequest, aStatusCode) {
    Services.obs.removeObserver(this, "quit-application");

    if (this.timer_) {
      this.timer_.cancel();
      this.timer_ = null;
    }

    this.telemetryClockStart = 0;

    if (this.#shuttingDown) {
      throw Components.Exception("", Cr.NS_ERROR_ABORT);
    }

    // Default HTTP status to service unavailable, in case we can't retrieve
    // the true status from the channel.
    let httpStatus = 503;
    if (Components.isSuccessCode(aStatusCode)) {
      let channel = aRequest.QueryInterface(Ci.nsIHttpChannel);
      let relayStatus = this.useOHTTP
        ? channel.QueryInterface(Ci.nsIObliviousHttpChannel).relayChannel
            .responseStatus
        : 200;
      if (relayStatus != 200) {
        httpStatus = relayStatus;
        aStatusCode = Cr.NS_ERROR_ABORT;
      } else {
        try {
          let success = channel.requestSucceeded;
          httpStatus = channel.responseStatus;
          if (!success) {
            aStatusCode = Cr.NS_ERROR_ABORT;
          }
        } catch (e) {
          aStatusCode = Cr.NS_ERROR_ABORT;
        }
      }
    }
    let success = Components.isSuccessCode(aStatusCode);
    log(
      "Received a " +
        httpStatus +
        " status code from the " +
        this.provider +
        " gethash server (success=" +
        success +
        "): " +
        btoa(this.response)
    );

    Glean.urlclassifier.completeRemoteStatus2[
      this.telemetryProvider
    ].accumulateSingleSample(httpStatusToBucket(httpStatus));
    if (httpStatus == 400) {
      dump(
        "Safe Browsing server returned a 400 during completion: request= " +
          this.request.url +
          ",payload= " +
          this.request.body +
          ",response= " +
          this.response +
          "\n"
      );
    }

    Glean.urlclassifier.completeTimeout
      .get(this.telemetryProvider, false)
      .add(1);

    // Notify the RequestBackoff once a response is received.
    this.completer.finishRequest(this, httpStatus);

    if (success) {
      try {
        this.handleResponse();
      } catch (err) {
        log(err.stack);
        aStatusCode = err.value;
        success = false;
      }
    }

    if (success) {
      this.notifySuccess();
    } else {
      this.notifyFailure(aStatusCode);
    }
  }

  observe(aSubject, aTopic) {
    if (aTopic == "quit-application") {
      this.#shuttingDown = true;
      if (this.channel) {
        this.channel.cancel(Cr.NS_ERROR_ABORT);
        this.telemetryClockStart = 0;
      }

      Services.obs.removeObserver(this, "quit-application");
    }
  }
}

class HashCompleterRequestV2 extends HashCompleterRequestBase {
  constructor(aCompleter, aGethashUrl, aIsTesting = false) {
    super(aCompleter, aGethashUrl);

    this.provider = aIsTesting ? "test" : "mozilla";
  }

  beginBuildChannel() {
    try {
      this.openChannel();
      // Notify the RequestBackoff if opening the channel succeeded. At this
      // point, finishRequest must be called.
      this.completer.noteRequest(this.key);
    } catch (err) {
      this.completer.removeFromOngoingRequests(this);
      this.notifyFailure(err);
      throw err;
    }
  }

  makeChannelURL() {
    return this.gethashUrl;
  }

  // Returns a string for the request body based on the contents of
  // this._requests.
  buildRequest() {
    // Sometimes duplicate entries are sent to HashCompleter but we do not need
    // to propagate these to the server. (bug 633644)
    let prefixes = [];

    for (let i = 0; i < this.requests.length; i++) {
      let request = this.requests[i];
      if (!prefixes.includes(request.partialHash)) {
        prefixes.push(request.partialHash);
      }
    }

    // Sort to make sure the entries are arbitrary mixed in a deterministic way
    prefixes.sort();

    let body;
    body =
      PARTIAL_LENGTH +
      ":" +
      PARTIAL_LENGTH * prefixes.length +
      "\n" +
      prefixes.join("");

    log(
      "Requesting completions for " +
        prefixes.length +
        " " +
        PARTIAL_LENGTH +
        "-byte prefixes: " +
        body
    );
    return body;
  }

  setupChannel(channel) {
    let body = this.buildRequest();
    let inputStream = Cc[
      "@mozilla.org/io/string-input-stream;1"
    ].createInstance(Ci.nsIStringInputStream);

    inputStream.setByteStringData(body);

    let uploadChannel = channel.QueryInterface(Ci.nsIUploadChannel);
    uploadChannel.setUploadStream(inputStream, "text/plain", -1);

    let httpChannel = channel.QueryInterface(Ci.nsIHttpChannel);
    httpChannel.requestMethod = "POST";
  }

  handleResponse() {
    if (this.response == "") {
      return;
    }

    let start = 0;

    let length = this.response.length;
    while (start != length) {
      start = this.handleTable(start);
    }
  }

  // This parses a table entry in the response body and calls |handleItem|
  // for complete hash in the table entry.
  handleTable(aStart) {
    let body = this.response.substring(aStart);

    // deal with new line indexes as there could be
    // new line characters in the data parts.
    let newlineIndex = body.indexOf("\n");
    if (newlineIndex == -1) {
      throw errorWithStack();
    }
    let header = body.substring(0, newlineIndex);
    let entries = header.split(":");
    if (entries.length != 3) {
      throw errorWithStack();
    }

    let list = entries[0];
    let addChunk = parseInt(entries[1]);
    let dataLength = parseInt(entries[2]);

    log("Response includes add chunks for " + list + ": " + addChunk);
    if (
      dataLength % COMPLETE_LENGTH != 0 ||
      dataLength == 0 ||
      dataLength > body.length - (newlineIndex + 1)
    ) {
      throw errorWithStack();
    }

    let data = body.substr(newlineIndex + 1, dataLength);
    for (let i = 0; i < dataLength / COMPLETE_LENGTH; i++) {
      this.handleItem({
        completeHash: data.substr(i * COMPLETE_LENGTH, COMPLETE_LENGTH),
        tableName: list,
        chunkId: addChunk,
      });
    }

    return aStart + newlineIndex + 1 + dataLength;
  }

  notifySuccess() {
    // V2 completion handler
    let completion = req => {
      req.response.matches.forEach(m => {
        req.callback.completionV2(m.completeHash, m.tableName, m.chunkId);
      });

      req.callback.completionFinished(Cr.NS_OK);
    };

    this.requests.forEach(req => {
      completion(req);
    });
  }
}

class HashCompleterRequestV4 extends HashCompleterRequestBase {
  constructor(aCompleter, aGethashUrl, aIsTesting = false) {
    super(aCompleter, aGethashUrl);

    this.provider = aIsTesting ? "test" : "google4";
  }

  fillTableStatesBase64(aCallback) {
    lazy.gDbService.getTables(aTableData => {
      aTableData.split("\n").forEach(line => {
        let p = line.indexOf(";");
        if (-1 === p) {
          return;
        }
        // [tableName];[stateBase64]:[checksumBase64]
        let tableName = line.substring(0, p);
        if (this.tableNames.has(tableName)) {
          let metadata = line.substring(p + 1).split(":");
          let stateBase64 = metadata[0];
          this.tableNames.set(tableName, stateBase64);
        }
      });

      aCallback();
    });
  }

  beginBuildChannel() {
    // V4 requires table states to build the request so we need
    // a async call to retrieve the table states from disk.
    // Note that |HCR_begin| is fine to be sync because
    // it doesn't appear in a sync call chain.
    this.fillTableStatesBase64(() => {
      try {
        this.openChannel();
        // Notify the RequestBackoff if opening the channel succeeded. At this
        // point, finishRequest must be called.
        this.completer.noteRequest(this.key);
      } catch (err) {
        this.completer.removeFromOngoingRequests(this);
        this.notifyFailure(err);
        throw err;
      }
    });
  }

  makeChannelURL() {
    return this.gethashUrl + "&$req=" + this.buildRequest();
  }

  buildRequest() {
    // Convert the "name to state" mapping to two equal-length arrays.
    let tableNameArray = [];
    let stateArray = [];
    this.tableNames.forEach((state, name) => {
      // We skip the table which is not associated with a state.
      if (state) {
        tableNameArray.push(name);
        stateArray.push(state);
      }
    });

    // Build the "distinct" prefix array.
    // The array is sorted to make sure the entries are arbitrary mixed in a
    // deterministic way
    let prefixSet = new Set();
    this.requests.forEach(r => prefixSet.add(btoa(r.partialHash)));
    let prefixArray = Array.from(prefixSet).sort();

    log(
      "Build v4 gethash request with " +
        JSON.stringify(tableNameArray) +
        ", " +
        JSON.stringify(stateArray) +
        ", " +
        JSON.stringify(prefixArray)
    );

    return lazy.gUrlUtil.makeFindFullHashRequestV4(
      tableNameArray,
      stateArray,
      prefixArray
    );
  }

  setupChannel(channel) {
    let httpChannel = channel.QueryInterface(Ci.nsIHttpChannel);
    httpChannel.setRequestHeader("X-HTTP-Method-Override", "POST", false);
  }

  handleResponse() {
    if (this.response == "") {
      return;
    }

    let callback = {
      // onCompleteHashFound will be called for each fullhash found in
      // FullHashResponse.
      onCompleteHashFound: (
        aCompleteHash,
        aTableNames,
        aPerHashCacheDuration
      ) => {
        log(
          "V4 fullhash response complete hash found callback: " +
            aTableNames +
            ", CacheDuration(" +
            aPerHashCacheDuration +
            ")"
        );

        // Filter table names which we didn't request.
        let filteredTables = aTableNames.split(",").filter(name => {
          return this.tableNames.has(name);
        });
        if (0 === filteredTables.length) {
          log("ERROR: Got complete hash which is from unknown table.");
          return;
        }
        if (filteredTables.length > 1) {
          log("WARNING: Got complete hash which has ambigious threat type.");
        }

        this.handleItem({
          completeHash: aCompleteHash,
          tableName: filteredTables[0],
          cacheDuration: aPerHashCacheDuration,
        });
      },

      // onResponseParsed will be called no matter if there is match in
      // FullHashResponse, the callback is mainly used to pass negative cache
      // duration and minimum wait duration.
      onResponseParsed: (aMinWaitDuration, aNegCacheDuration) => {
        log(
          "V4 fullhash response parsed callback: " +
            "MinWaitDuration(" +
            aMinWaitDuration +
            "), " +
            "NegativeCacheDuration(" +
            aNegCacheDuration +
            ")"
        );

        let minWaitDuration = aMinWaitDuration;

        if (aMinWaitDuration > MIN_WAIT_DURATION_MAX_VALUE) {
          log(
            "WARNING: Minimum wait duration too large, clamping it down " +
              "to a reasonable value."
          );
          minWaitDuration = MIN_WAIT_DURATION_MAX_VALUE;
        } else if (aMinWaitDuration < 0) {
          log("WARNING: Minimum wait duration is negative, reset it to 0");
          minWaitDuration = 0;
        }

        this.completer.setNextGethashTime(
          this.key,
          Date.now() + minWaitDuration
        );

        // A fullhash request may contain more than one prefix, so the negative
        // cache duration should be set for all the prefixes in the request.
        this.requests.forEach(request => {
          request.response.negCacheDuration = aNegCacheDuration;
        });
      },
    };

    lazy.gUrlUtil.parseFindFullHashResponseV4(this.response, callback);
  }

  notifySuccess() {
    let completion = req => {
      let matches = Cc["@mozilla.org/array;1"].createInstance(
        Ci.nsIMutableArray
      );

      req.response.matches.forEach(m => {
        matches.appendElement(
          new FullHashMatch(m.tableName, m.completeHash, m.cacheDuration)
        );
      });

      req.callback.completionV4(
        req.partialHash,
        req.tableName,
        req.response.negCacheDuration,
        matches
      );

      req.callback.completionFinished(Cr.NS_OK);
    };

    this.requests.forEach(req => {
      completion(req);
    });
  }
}

class HashCompleterRequestV5 extends HashCompleterRequestBase {
  constructor(aCompleter, aGethashUrl, aIsTesting = false, aUseOHTTP = false) {
    super(aCompleter, aGethashUrl, aUseOHTTP);

    this.provider = aIsTesting ? "test" : "google5";
  }

  beginBuildChannel() {
    if (!this.useOHTTP) {
      this.#openChannelAndNoteRequest(null);
      return;
    }

    this.#getOHTTPConfig().then(
      ohttp => {
        try {
          this.#openChannelAndNoteRequest(ohttp);
        } catch (err) {
          log("Can't open the OHTTP channel: " + err + "\n");
        }
      },
      err => {
        log("Can't send the request through OHTTP: " + err + "\n");
        this.completer.removeFromOngoingRequests(this);
        this.notifyFailure(Cr.NS_ERROR_NOT_AVAILABLE);
      }
    );
  }

  async #getOHTTPConfig() {
    let relayURL = lazy.ohttpRelayURL;
    if (!relayURL) {
      throw new Error("No OHTTP relay URL");
    }
    let timer;
    let timeout = new Promise((_, reject) => {
      timer = lazy.setTimeout(
        () => reject(new Error("Timed out waiting for the OHTTP key config")),
        Services.prefs.getIntPref(PREF_GETHASH_TIMEOUT)
      );
    });
    let keyConfig;
    try {
      keyConfig = await Promise.race([
        lazy.gRealTimeKeyService.getKeyConfig(),
        timeout,
      ]);
    } finally {
      lazy.clearTimeout(timer);
    }
    if (this.completer.shuttingDown) {
      throw new Error("Shutting down");
    }
    if (!keyConfig) {
      throw new Error("No OHTTP key config");
    }
    return { relayURI: Services.io.newURI(relayURL), keyConfig };
  }

  #openChannelAndNoteRequest(aOHTTP) {
    try {
      this.openChannel(aOHTTP);
      // Notify the RequestBackoff if opening the channel succeeded. At this
      // point, finishRequest must be called.
      this.completer.noteRequest(this.key);
    } catch (err) {
      this.completer.removeFromOngoingRequests(this);
      this.notifyFailure(err.result ?? Cr.NS_ERROR_FAILURE);
      throw err;
    }
  }

  makeChannelURL() {
    let prefixSet = new Set();
    // In V5, the prefix is sent using query parameters. We need to encode the
    // prefixes to avoid issues with special characters.
    this.requests.forEach(r =>
      prefixSet.add(encodeURIComponent(btoa(r.partialHash)))
    );
    let prefixArray = Array.from(prefixSet).sort();

    log("Build v5 gethash request URL with " + JSON.stringify(prefixArray));

    return (
      this.gethashUrl +
      "&" +
      lazy.gUrlUtil.makeFindFullHashRequestV5(prefixArray)
    );
  }

  // In V5, we don't need to do extra setup for the channel. The request uses
  // the default GET method.
  setupChannel(_channel) {}

  handleResponse() {
    if (this.response == "") {
      return;
    }

    let callback = {
      // onCompleteHashFound will be called for each fullhash found in
      // FullHashResponse.
      onCompleteHashFound: (
        aCompleteHash,
        aTableNames,
        aPerHashCacheDuration
      ) => {
        log(
          "V5 hashes::search response complete hash found callback: " +
            aTableNames +
            ", CacheDuration(" +
            aPerHashCacheDuration +
            ")"
        );

        // Filter table names which we didn't request.
        let filteredTables = aTableNames.split(",").filter(name => {
          return this.tableNames.has(name);
        });
        if (0 === filteredTables.length) {
          log("ERROR: Got complete hash which is from unknown table.");
          return;
        }
        if (filteredTables.length > 1) {
          log("WARNING: Got complete hash which has ambiguous threat type.");
        }

        this.handleItem({
          completeHash: aCompleteHash,
          tableName: filteredTables[0],
          cacheDuration: aPerHashCacheDuration,
        });
      },

      // onResponseParsed will be called no matter if there is match in
      // FullHashResponse, the callback is mainly used to pass negative cache
      // duration and minimum wait duration.
      onResponseParsed: (aMinWaitDuration, aNegCacheDuration) => {
        log(
          "V5 hashes::search response parsed callback: " +
            "MinWaitDuration(" +
            aMinWaitDuration +
            "), " +
            "NegativeCacheDuration(" +
            aNegCacheDuration +
            ")"
        );

        let minWaitDuration = aMinWaitDuration;

        if (aMinWaitDuration > MIN_WAIT_DURATION_MAX_VALUE) {
          log(
            "WARNING: Minimum wait duration too large, clamping it down " +
              "to a reasonable value."
          );
          minWaitDuration = MIN_WAIT_DURATION_MAX_VALUE;
        } else if (aMinWaitDuration < 0) {
          log("WARNING: Minimum wait duration is negative, reset it to 0");
          minWaitDuration = 0;
        }

        this.completer.setNextGethashTime(
          this.key,
          Date.now() + minWaitDuration
        );

        // A fullhash request may contain more than one prefix, so the negative
        // cache duration should be set for all the prefixes in the request.
        this.requests.forEach(request => {
          request.response.negCacheDuration = aNegCacheDuration;
        });
      },
    };

    lazy.gUrlUtil.parseFindFullHashResponseV5(this.response, callback);
  }

  notifySuccess() {
    let completion = req => {
      let matches = Cc["@mozilla.org/array;1"].createInstance(
        Ci.nsIMutableArray
      );

      req.response.matches.forEach(m => {
        matches.appendElement(
          new FullHashMatch(m.tableName, m.completeHash, m.cacheDuration)
        );
      });

      // We still use the V4 completion method for V5 because V5 uses the same
      // caching mechanism as V4.
      req.callback.completionV4(
        req.partialHash,
        req.tableName,
        req.response.negCacheDuration,
        matches
      );

      req.callback.completionFinished(Cr.NS_OK);
    };

    this.requests.forEach(req => {
      completion(req);
    });
  }
}

function errorWithStack() {
  let err = new Error();
  err.value = Cr.NS_ERROR_FAILURE;
  return err;
}
