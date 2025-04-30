/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * NetworkObserver is the main class in DevTools to observe network requests
 * out of many events fired by the platform code.
 */

// Enable logging all platform events this module listen to
const DEBUG_PLATFORM_EVENTS = false;
// Enables defining criteria to filter the logs
const DEBUG_PLATFORM_EVENTS_FILTER = () => {
  // e.g return eventName == "HTTP_TRANSACTION:REQUEST_HEADER" && channel.URI.spec == "http://foo.com";
  return true;
};

const lazy = {};

import { DevToolsInfaillibleUtils } from "resource://devtools/shared/DevToolsInfaillibleUtils.sys.mjs";

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    ChannelMap:
      "resource://devtools/shared/network-observer/ChannelMap.sys.mjs",
    NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
    NetworkAuthListener:
      "resource://devtools/shared/network-observer/NetworkAuthListener.sys.mjs",
    NetworkHelper:
      "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",
    NetworkOverride:
      "resource://devtools/shared/network-observer/NetworkOverride.sys.mjs",
    NetworkResponseListener:
      "resource://devtools/shared/network-observer/NetworkResponseListener.sys.mjs",
    NetworkTimings:
      "resource://devtools/shared/network-observer/NetworkTimings.sys.mjs",
    NetworkThrottleManager:
      "resource://devtools/shared/network-observer/NetworkThrottleManager.sys.mjs",
    NetworkUtils:
      "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",
    wildcardToRegExp:
      "resource://devtools/shared/network-observer/WildcardToRegexp.sys.mjs",
  },
  { global: "contextual" }
);

const gActivityDistributor = Cc[
  "@mozilla.org/network/http-activity-distributor;1"
].getService(Ci.nsIHttpActivityDistributor);

function logPlatformEvent(eventName, channel, message = "") {
  if (!DEBUG_PLATFORM_EVENTS) {
    return;
  }
  if (DEBUG_PLATFORM_EVENTS_FILTER(eventName, channel)) {
    dump(
      `[netmonitor] ${channel.channelId} - ${eventName} ${message} - ${channel.URI.spec}\n`
    );
  }
}

// The maximum uint32 value.
const PR_UINT32_MAX = 4294967295;

const HTTP_TRANSACTION_CODES = {
  0x5001: "REQUEST_HEADER",
  0x5002: "REQUEST_BODY_SENT",
  0x5003: "RESPONSE_START",
  0x5004: "RESPONSE_HEADER",
  0x5005: "RESPONSE_COMPLETE",
  0x5006: "TRANSACTION_CLOSE",
  0x500c: "EARLYHINT_RESPONSE_HEADER",

  0x4b0003: "STATUS_RESOLVING",
  0x4b000b: "STATUS_RESOLVED",
  0x4b0007: "STATUS_CONNECTING_TO",
  0x4b0004: "STATUS_CONNECTED_TO",
  0x4b0005: "STATUS_SENDING_TO",
  0x4b000a: "STATUS_WAITING_FOR",
  0x4b0006: "STATUS_RECEIVING_FROM",
  0x4b000c: "STATUS_TLS_STARTING",
  0x4b000d: "STATUS_TLS_ENDING",
};

const HTTP_DOWNLOAD_ACTIVITIES = [
  gActivityDistributor.ACTIVITY_SUBTYPE_RESPONSE_START,
  gActivityDistributor.ACTIVITY_SUBTYPE_RESPONSE_HEADER,
  gActivityDistributor.ACTIVITY_SUBTYPE_PROXY_RESPONSE_HEADER,
  gActivityDistributor.ACTIVITY_SUBTYPE_EARLYHINT_RESPONSE_HEADER,
  gActivityDistributor.ACTIVITY_SUBTYPE_RESPONSE_COMPLETE,
  gActivityDistributor.ACTIVITY_SUBTYPE_TRANSACTION_CLOSE,
];

/**
 * The network monitor uses the nsIHttpActivityDistributor to monitor network
 * requests. The nsIObserverService is also used for monitoring
 * http-on-examine-response notifications. All network request information is
 * routed to the remote Web Console.
 *
 * @constructor
 * @param {Object} options
 * @param {Function(nsIChannel): boolean} options.ignoreChannelFunction
 *        This function will be called for every detected channel to decide if it
 *        should be monitored or not.
 * @param {Function(NetworkEvent): owner} options.onNetworkEvent
 *        This method is invoked once for every new network request with two
 *        arguments:
 *        - {Object} networkEvent: object created by NetworkUtils:createNetworkEvent,
 *          containing initial network request information as an argument.
 *        - {nsIChannel} channel: the channel for which the request was detected
 *
 *        `onNetworkEvent()` must return an "owner" object which holds several add*()
 *        methods which are used to add further network request/response information.
 */
export class NetworkObserver {
  /**
   * Map of URL patterns to RegExp
   *
   * @type {Map}
   */
  #blockedURLs = new Map();

  /**
   * Map of URL to local file path in order to redirect URL
   * to local file overrides.
   *
   * This will replace the content of some request with the content of local files.
   */
  #overrides = new Map();

  /**
   * Used by NetworkHelper.parseSecurityInfo to skip decoding known certificates.
   *
   * @type {Map}
   */
  #decodedCertificateCache = new Map();
  /**
   * Whether the consumer supports listening and handling auth prompts.
   *
   * @type {boolean}
   */
  #authPromptListenerEnabled = false;
  /**
   * See constructor argument of the same name.
   *
   * @type {Function}
   */
  #ignoreChannelFunction;
  /**
   * Used to store channels intercepted for service-worker requests.
   *
   * @type {WeakSet}
   */
  #interceptedChannels = new WeakSet();
  /**
   * Explicit flag to check if this observer was already destroyed.
   *
   * @type {boolean}
   */
  #isDestroyed = false;
  /**
   * See constructor argument of the same name.
   *
   * @type {Function}
   */
  #onNetworkEvent;
  /**
   * Object that holds the activity objects for ongoing requests.
   *
   * @type {ChannelMap}
   */
  #openRequests = new lazy.ChannelMap();
  /**
   * Network response bodies are piped through a buffer of the given size
   * (in bytes).
   *
   * @type {Number}
   */
  #responsePipeSegmentSize = Services.prefs.getIntPref(
    "network.buffer.cache.size"
  );
  /**
   * Whether to save the bodies of network requests and responses.
   *
   * @type {boolean}
   */
  #saveRequestAndResponseBodies = true;
  /**
   * Throttling configuration, see constructor of NetworkThrottleManager
   *
   * @type {Object}
   */
  #throttleData = null;
  /**
   * NetworkThrottleManager instance, created when a valid throttleData is set.
   * @type {NetworkThrottleManager}
   */
  #throttler = null;

  constructor(options = {}) {
    const { ignoreChannelFunction, onNetworkEvent } = options;
    if (typeof ignoreChannelFunction !== "function") {
      throw new Error(
        `Expected "ignoreChannelFunction" to be a function, got ${ignoreChannelFunction} (${typeof ignoreChannelFunction})`
      );
    }

    if (typeof onNetworkEvent !== "function") {
      throw new Error(
        `Expected "onNetworkEvent" to be a function, got ${onNetworkEvent} (${typeof onNetworkEvent})`
      );
    }

    this.#ignoreChannelFunction = ignoreChannelFunction;
    this.#onNetworkEvent = onNetworkEvent;

    // Start all platform observers.
    if (Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_CONTENT) {
      gActivityDistributor.addObserver(this);
      gActivityDistributor.observeProxyResponse = true;

      Services.obs.addObserver(
        this.#httpResponseExaminer,
        "http-on-examine-response"
      );
      Services.obs.addObserver(
        this.#httpResponseExaminer,
        "http-on-examine-cached-response"
      );
      Services.obs.addObserver(
        this.#httpModifyExaminer,
        "http-on-modify-request"
      );
      Services.obs.addObserver(
        this.#fileChannelExaminer,
        "file-channel-opened"
      );
      Services.obs.addObserver(
        this.#dataChannelExaminer,
        "data-channel-opened"
      );
      Services.obs.addObserver(
        this.#httpBeforeConnect,
        "http-on-before-connect"
      );

      Services.obs.addObserver(this.#httpStopRequest, "http-on-stop-request");
    } else {
      Services.obs.addObserver(
        this.#httpFailedOpening,
        "http-on-failed-opening-request"
      );
    }
    // In child processes, only watch for service worker requests
    // everything else only happens in the parent process
    Services.obs.addObserver(
      this.#serviceWorkerRequest,
      "service-worker-synthesized-response"
    );
  }

  setAuthPromptListenerEnabled(enabled) {
    this.#authPromptListenerEnabled = enabled;
  }

  setSaveRequestAndResponseBodies(save) {
    this.#saveRequestAndResponseBodies = save;
  }

  getThrottleData() {
    return this.#throttleData;
  }

  setThrottleData(value) {
    this.#throttleData = value;
    // Clear out any existing throttlers
    this.#throttler = null;
  }

  #getThrottler() {
    if (this.#throttleData !== null && this.#throttler === null) {
      this.#throttler = new lazy.NetworkThrottleManager(this.#throttleData);
    }
    return this.#throttler;
  }

  #serviceWorkerRequest = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      const channel = subject.QueryInterface(Ci.nsIHttpChannel);

      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(topic, channel);

      this.#interceptedChannels.add(subject);

      // Service workers never fire http-on-examine-cached-response, so fake one.
      this.#httpResponseExaminer(channel, "http-on-examine-cached-response");
    }
  );

  /**
   * Observes for http-on-failed-opening-request notification to catch any
   * channels for which asyncOpen has synchronously failed.  This is the only
   * place to catch early security check failures.
   */
  #httpFailedOpening = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      if (
        this.#isDestroyed ||
        topic != "http-on-failed-opening-request" ||
        !(subject instanceof Ci.nsIHttpChannel)
      ) {
        return;
      }

      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(topic, channel);

      // Ignore preload requests to avoid duplicity request entries in
      // the Network panel. If a preload fails (for whatever reason)
      // then the platform kicks off another 'real' request.
      if (lazy.NetworkUtils.isPreloadRequest(channel)) {
        return;
      }

      this.#httpResponseExaminer(subject, topic);
    }
  );

  #httpBeforeConnect = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      if (
        this.#isDestroyed ||
        topic != "http-on-before-connect" ||
        !(subject instanceof Ci.nsIHttpChannel)
      ) {
        return;
      }

      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      // Here we create the network event from an early platform notification.
      // Additional details about the event will be provided using the various
      // callbacks on the network event owner.
      const httpActivity = this.#createOrGetActivityObject(channel);
      this.#createNetworkEvent(httpActivity);

      // Handle overrides in http-on-before-connect because we need to redirect
      // the request to the override before reaching the server.
      this.#checkForContentOverride(httpActivity);
    }
  );

  #httpStopRequest = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      if (
        this.#isDestroyed ||
        topic != "http-on-stop-request" ||
        !(subject instanceof Ci.nsIHttpChannel)
      ) {
        return;
      }

      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(topic, channel);

      const httpActivity = this.#createOrGetActivityObject(channel);
      if (httpActivity.owner) {
        // Try extracting server timings. Note that they will be sent to the client
        // in the `_onTransactionClose` method together with network event timings.
        const serverTimings =
          lazy.NetworkTimings.extractServerTimings(httpActivity);
        httpActivity.owner.addServerTimings(serverTimings);

        // If the owner isn't set we need to create the network event and send
        // it to the client. This happens in case where:
        // - the request has been blocked (e.g. CORS) and "http-on-stop-request" is the first notification.
        // - the NetworkObserver is start *after* the request started and we only receive the http-stop notification,
        //   but that doesn't mean the request is blocked, so check for its status.
      } else if (Components.isSuccessCode(channel.status)) {
        // Do not pass any blocked reason, as this request is just fine.
        // Bug 1489217 - Prevent watching for this request response content,
        // as this request is already running, this is too late to watch for it.
        this.#createNetworkEvent(httpActivity, {
          inProgressRequest: true,
        });
      } else {
        // Handles any early blockings e.g by Web Extensions or by CORS
        const { blockingExtension, blockedReason } =
          lazy.NetworkUtils.getBlockedReason(channel, httpActivity.fromCache);
        this.#createNetworkEvent(httpActivity, {
          blockedReason,
          blockingExtension,
        });
      }
    }
  );

  /**
   * Check if the current channel has its content being overriden
   * by the content of some local file.
   */
  #checkForContentOverride(httpActivity) {
    const channel = httpActivity.channel;
    const overridePath = this.#overrides.get(channel.URI.spec);
    if (!overridePath) {
      return false;
    }

    dump(" Override " + channel.URI.spec + " to " + overridePath + "\n");
    try {
      lazy.NetworkOverride.overrideChannelWithFilePath(channel, overridePath);
      // Handle the activity as being from the cache to avoid looking up
      // typical information from the http channel, which would error for
      // overridden channels.
      httpActivity.fromCache = true;
      httpActivity.isOverridden = true;
    } catch (e) {
      dump("Exception while trying to override request content: " + e + "\n");
    }

    return true;
  }

  /**
   * Observe notifications for the http-on-examine-response topic, coming from
   * the nsIObserverService.
   *
   * @private
   * @param nsIHttpChannel subject
   * @param string topic
   * @returns void
   */
  #httpResponseExaminer = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      // The httpResponseExaminer is used to retrieve the uncached response
      // headers.
      if (
        this.#isDestroyed ||
        (topic != "http-on-examine-response" &&
          topic != "http-on-examine-cached-response" &&
          topic != "http-on-failed-opening-request") ||
        !(subject instanceof Ci.nsIHttpChannel) ||
        !(subject instanceof Ci.nsIClassifiedChannel)
      ) {
        return;
      }

      const blockedOrFailed = topic === "http-on-failed-opening-request";

      subject.QueryInterface(Ci.nsIClassifiedChannel);
      const channel = subject.QueryInterface(Ci.nsIHttpChannel);

      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(
        topic,
        subject,
        blockedOrFailed
          ? "blockedOrFailed:" + channel.loadInfo.requestBlockingReason
          : channel.responseStatus
      );

      channel.QueryInterface(Ci.nsIHttpChannelInternal);

      // Retrieve or create the http activity.
      const httpActivity = this.#createOrGetActivityObject(channel);

      if (topic === "http-on-examine-cached-response") {
        this.#handleExamineCachedResponse(httpActivity);
      } else if (topic === "http-on-failed-opening-request") {
        this.#handleFailedOpeningRequest(httpActivity);
      }

      if (httpActivity.owner) {
        httpActivity.owner.addResponseStart({
          channel: httpActivity.channel,
          fromCache: httpActivity.fromCache || httpActivity.fromServiceWorker,
          fromServiceWorker: httpActivity.fromServiceWorker,
          rawHeaders: httpActivity.responseRawHeaders,
          proxyResponseRawHeaders: httpActivity.proxyResponseRawHeaders,
          earlyHintsResponseRawHeaders:
            httpActivity.earlyHintsResponseRawHeaders,
        });
      }
    }
  );

  #handleExamineCachedResponse(httpActivity) {
    const channel = httpActivity.channel;

    const fromServiceWorker = this.#interceptedChannels.has(channel);
    const fromCache = !fromServiceWorker;

    // Set the cache flags on the httpActivity object, they will be used later
    // on during the lifecycle of the channel.
    httpActivity.fromCache = fromCache;
    httpActivity.fromServiceWorker = fromServiceWorker;

    // Service worker requests emits cached-response notification on non-e10s,
    // and we fake one on e10s.
    this.#interceptedChannels.delete(channel);

    if (!httpActivity.owner) {
      // If this is a cached response (which are also emitted by service worker requests),
      // there never was a request event so we need to construct one here
      // so the frontend gets all the expected events.
      this.#createNetworkEvent(httpActivity);
    }

    httpActivity.owner.addCacheDetails({
      fromCache: httpActivity.fromCache,
      fromServiceWorker: httpActivity.fromServiceWorker,
    });

    // We need to send the request body to the frontend for
    // the faked (cached/service worker request) event.
    this.#prepareRequestBody(httpActivity);
    this.#sendRequestBody(httpActivity);

    // There also is never any timing events, so we can fire this
    // event with zeroed out values.
    const timings = lazy.NetworkTimings.extractHarTimings(httpActivity);
    const serverTimings =
      lazy.NetworkTimings.extractServerTimings(httpActivity);
    const serviceWorkerTimings =
      lazy.NetworkTimings.extractServiceWorkerTimings(httpActivity);

    httpActivity.owner.addServerTimings(serverTimings);
    httpActivity.owner.addServiceWorkerTimings(serviceWorkerTimings);
    httpActivity.owner.addEventTimings(
      timings.total,
      timings.timings,
      timings.offsets
    );
  }

  #handleFailedOpeningRequest(httpActivity) {
    const channel = httpActivity.channel;
    const { blockedReason } = lazy.NetworkUtils.getBlockedReason(
      channel,
      httpActivity.fromCache
    );

    this.#createNetworkEvent(httpActivity, {
      blockedReason,
    });
  }

  /**
   * Observe notifications for the http-on-modify-request topic, coming from
   * the nsIObserverService.
   *
   * @private
   * @param nsIHttpChannel aSubject
   * @returns void
   */
  #httpModifyExaminer = DevToolsInfaillibleUtils.makeInfallible(subject => {
    const throttler = this.#getThrottler();
    if (throttler) {
      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      if (this.#ignoreChannelFunction(channel)) {
        return;
      }
      logPlatformEvent("http-on-modify-request", channel);

      // Read any request body here, before it is throttled.
      const httpActivity = this.#createOrGetActivityObject(channel);
      this.#prepareRequestBody(httpActivity);
      throttler.manageUpload(channel);
    }
  });

  #dataChannelExaminer = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      if (
        topic != "data-channel-opened" ||
        !(subject instanceof Ci.nsIDataChannel)
      ) {
        return;
      }
      const channel = subject.QueryInterface(Ci.nsIDataChannel);
      channel.QueryInterface(Ci.nsIIdentChannel);
      channel.QueryInterface(Ci.nsIChannel);

      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(topic, channel);

      const networkEventActor = this.#onNetworkEvent({}, channel, true);
      lazy.NetworkUtils.handleDataChannel(channel, networkEventActor);
    }
  );

  /**
   * Observe notifications for the file-channel-opened topic
   *
   * @private
   * @param nsIFileChannel subject
   * @param string topic
   * @returns void
   */
  #fileChannelExaminer = DevToolsInfaillibleUtils.makeInfallible(
    (subject, topic) => {
      if (
        this.#isDestroyed ||
        topic != "file-channel-opened" ||
        !(subject instanceof Ci.nsIFileChannel)
      ) {
        return;
      }
      const channel = subject.QueryInterface(Ci.nsIFileChannel);
      channel.QueryInterface(Ci.nsIIdentChannel);
      channel.QueryInterface(Ci.nsIChannel);

      if (this.#ignoreChannelFunction(channel)) {
        return;
      }

      logPlatformEvent(topic, channel);
      const owner = this.#onNetworkEvent({}, channel, true);

      owner.addResponseStart({
        channel,
        fromCache: false,
        rawHeaders: "",
      });

      // For file URLs we can not set up a stream listener as for http,
      // so we have to create a response manually and complete it.
      const response = {
        contentCharset: channel.contentCharset,
        contentLength: channel.contentLength,
        contentType: channel.contentType,
        mimeType: lazy.NetworkHelper.addCharsetToMimeType(
          channel.contentType,
          channel.contentCharset
        ),
        // Same as for cached responses, the transferredSize for file URLs
        // should be 0 regardless of the actual size of the response.
        transferredSize: 0,
      };

      // For file URIs all timings can be set to zero.
      const result = lazy.NetworkTimings.getEmptyHARTimings();
      owner.addEventTimings(result.total, result.timings, result.offsets);

      const fstream = Cc[
        "@mozilla.org/network/file-input-stream;1"
      ].createInstance(Ci.nsIFileInputStream);
      fstream.init(channel.file, -1, 0, 0);
      response.text = lazy.NetUtil.readInputStreamToString(
        fstream,
        fstream.available()
      );
      fstream.close();

      // Set the bodySize to the current response.text.length
      response.bodySize = response.text.length;

      if (
        !response.mimeType ||
        !lazy.NetworkHelper.isTextMimeType(response.mimeType)
      ) {
        response.encoding = "base64";
        try {
          response.text = btoa(response.text);
        } catch (err) {
          // Ignore.
        }
      }

      // Set the size/decodedBodySize to the updated response.text.length, after
      // potentially decoding the data.
      // NB: `size` is used by DevTools, while WebDriverBiDi relies on
      // decodedBodySize, because the name is more explicit.
      response.decodedBodySize = response.text.length;
      response.size = response.decodedBodySize;

      // Security information is not relevant for file channel, but it should
      // not be considered as insecure either. Set empty string as security
      // state.
      owner.addSecurityInfo({ state: "" });
      owner.addResponseContent(response, {});
    }
  );

  /**
   * A helper function for observeActivity.  This does whatever work
   * is required by a particular http activity event.  Arguments are
   * the same as for observeActivity.
   */
  #dispatchActivity(
    httpActivity,
    channel,
    activityType,
    activitySubtype,
    timestamp,
    extraSizeData,
    extraStringData
  ) {
    // Store the time information for this activity subtype.
    if (activitySubtype in HTTP_TRANSACTION_CODES) {
      const stage = HTTP_TRANSACTION_CODES[activitySubtype];
      if (stage in httpActivity.timings) {
        httpActivity.timings[stage].last = timestamp;
      } else {
        httpActivity.timings[stage] = {
          first: timestamp,
          last: timestamp,
        };
      }
    }
    switch (activitySubtype) {
      case gActivityDistributor.ACTIVITY_SUBTYPE_REQUEST_BODY_SENT:
        this.#prepareRequestBody(httpActivity);
        this.#sendRequestBody(httpActivity);
        break;
      case gActivityDistributor.ACTIVITY_SUBTYPE_RESPONSE_HEADER:
        httpActivity.responseRawHeaders = extraStringData;
        httpActivity.headersSize = extraStringData.length;
        break;
      case gActivityDistributor.ACTIVITY_SUBTYPE_PROXY_RESPONSE_HEADER:
        httpActivity.proxyResponseRawHeaders = extraStringData;
        break;
      case gActivityDistributor.ACTIVITY_SUBTYPE_EARLYHINT_RESPONSE_HEADER:
        httpActivity.earlyHintsResponseRawHeaders = extraStringData;
        httpActivity.headersSize = extraStringData.length;
        break;
      case gActivityDistributor.ACTIVITY_SUBTYPE_TRANSACTION_CLOSE:
        this.#onTransactionClose(httpActivity);
        break;
      default:
        break;
    }
  }

  getActivityTypeString(activityType, activitySubtype) {
    if (
      activityType === Ci.nsIHttpActivityObserver.ACTIVITY_TYPE_SOCKET_TRANSPORT
    ) {
      for (const name in Ci.nsISocketTransport) {
        if (Ci.nsISocketTransport[name] === activitySubtype) {
          return "SOCKET_TRANSPORT:" + name;
        }
      }
    } else if (
      activityType === Ci.nsIHttpActivityObserver.ACTIVITY_TYPE_HTTP_TRANSACTION
    ) {
      for (const name in Ci.nsIHttpActivityObserver) {
        if (Ci.nsIHttpActivityObserver[name] === activitySubtype) {
          return "HTTP_TRANSACTION:" + name.replace("ACTIVITY_SUBTYPE_", "");
        }
      }
    }
    return "unexpected-activity-types:" + activityType + ":" + activitySubtype;
  }

  /**
   * Begin observing HTTP traffic that originates inside the current tab.
   *
   * @see https://developer.mozilla.org/en/XPCOM_Interface_Reference/nsIHttpActivityObserver
   *
   * @param nsIHttpChannel channel
   * @param number activityType
   * @param number activitySubtype
   * @param number timestamp
   * @param number extraSizeData
   * @param string extraStringData
   */
  observeActivity = DevToolsInfaillibleUtils.makeInfallible(
    function (
      channel,
      activityType,
      activitySubtype,
      timestamp,
      extraSizeData,
      extraStringData
    ) {
      if (
        this.#isDestroyed ||
        (activityType != gActivityDistributor.ACTIVITY_TYPE_HTTP_TRANSACTION &&
          activityType != gActivityDistributor.ACTIVITY_TYPE_SOCKET_TRANSPORT)
      ) {
        return;
      }

      if (
        !(channel instanceof Ci.nsIHttpChannel) ||
        !(channel instanceof Ci.nsIClassifiedChannel)
      ) {
        return;
      }

      channel = channel.QueryInterface(Ci.nsIHttpChannel);
      channel = channel.QueryInterface(Ci.nsIClassifiedChannel);

      if (DEBUG_PLATFORM_EVENTS) {
        logPlatformEvent(
          this.getActivityTypeString(activityType, activitySubtype),
          channel
        );
      }

      if (
        activitySubtype == gActivityDistributor.ACTIVITY_SUBTYPE_REQUEST_HEADER
      ) {
        this.#onRequestHeader(channel, timestamp, extraStringData);
        return;
      }

      // Iterate over all currently ongoing requests. If channel can't
      // be found within them, then exit this function.
      const httpActivity = this.#findActivityObject(channel);
      if (!httpActivity) {
        return;
      }

      // If we're throttling, we must not report events as they arrive
      // from platform, but instead let the throttler emit the events
      // after some time has elapsed.
      if (
        httpActivity.downloadThrottle &&
        HTTP_DOWNLOAD_ACTIVITIES.includes(activitySubtype)
      ) {
        const callback = this.#dispatchActivity.bind(this);
        httpActivity.downloadThrottle.addActivityCallback(
          callback,
          httpActivity,
          channel,
          activityType,
          activitySubtype,
          timestamp,
          extraSizeData,
          extraStringData
        );
      } else {
        this.#dispatchActivity(
          httpActivity,
          channel,
          activityType,
          activitySubtype,
          timestamp,
          extraSizeData,
          extraStringData
        );
      }
    }
  );

  /**
   * Craft the "event" object passed to the Watcher class in order
   * to instantiate the NetworkEventActor.
   *
   * /!\ This method does many other important things:
   * - Cancel requests blocked by DevTools
   * - Fetch request headers/cookies
   * - Set a few attributes on http activity object
   * - Set a few attributes on file activity object
   * - Register listener to record response content
   */
  #createNetworkEvent(
    httpActivity,
    { timestamp, blockedReason, blockingExtension, inProgressRequest } = {}
  ) {
    if (
      blockedReason === undefined &&
      this.#shouldBlockChannel(httpActivity.channel)
    ) {
      // Check the request URL with ones manually blocked by the user in DevTools.
      // If it's meant to be blocked, we cancel the request and annotate the event.
      httpActivity.channel.cancel(Cr.NS_BINDING_ABORTED);
      blockedReason = "devtools";
    }

    httpActivity.owner = this.#onNetworkEvent(
      {
        timestamp,
        blockedReason,
        blockingExtension,
        discardRequestBody: !this.#saveRequestAndResponseBodies,
        discardResponseBody: !this.#saveRequestAndResponseBodies,
      },
      httpActivity.channel
    );

    // Bug 1489217 - Avoid watching for response content for blocked or in-progress requests
    // as it can't be observed and would throw if we try.
    if (blockedReason === undefined && !inProgressRequest) {
      this.#setupResponseListener(httpActivity);
    }

    const wrapper = ChannelWrapper.get(httpActivity.channel);
    if (this.#authPromptListenerEnabled && !wrapper.hasNetworkAuthListener) {
      new lazy.NetworkAuthListener(httpActivity.channel, httpActivity.owner);
      wrapper.hasNetworkAuthListener = true;
    }
  }

  /**
   * Handler for ACTIVITY_SUBTYPE_REQUEST_HEADER. When a request starts the
   * headers are sent to the server. This method creates the |httpActivity|
   * object where we store the request and response information that is
   * collected through its lifetime.
   *
   * @private
   * @param nsIHttpChannel channel
   * @param number timestamp
   * @param string rawHeaders
   * @return void
   */
  #onRequestHeader(channel, timestamp, rawHeaders) {
    if (this.#ignoreChannelFunction(channel)) {
      return;
    }

    const httpActivity = this.#createOrGetActivityObject(channel);
    if (timestamp) {
      httpActivity.timings.REQUEST_HEADER = {
        first: timestamp,
        last: timestamp,
      };
    }

    // TODO: In theory httpActivity.owner should not be missing here because
    // the network event should have been created in http-on-before-connect.
    // However, there is a scenario in DevTools where this can still happen:
    // if NetworkObserver clear() is called after the event was detected, the
    // activity will be deleted again have an ownerless notification here.
    if (!httpActivity.owner) {
      // If we are not creating events using the early platform notification
      // this should be the first time we are notified about this channel.
      this.#createNetworkEvent(httpActivity, {
        timestamp,
      });
    }

    httpActivity.owner.addRawHeaders({
      channel,
      rawHeaders,
    });
  }

  /**
   * Check if the provided channel should be blocked given the current
   * blocked URLs configured for this network observer.
   */
  #shouldBlockChannel(channel) {
    for (const regexp of this.#blockedURLs.values()) {
      if (regexp.test(channel.URI.spec)) {
        return true;
      }
    }
    return false;
  }

  /**
   * Find an HTTP activity object for the channel.
   *
   * @param nsIHttpChannel channel
   *        The HTTP channel whose activity object we want to find.
   * @return object
   *        The HTTP activity object, or null if it is not found.
   */
  #findActivityObject(channel) {
    return this.#openRequests.get(channel);
  }

  /**
   * Find an existing activity object, or create a new one. This
   * object is used for storing all the request and response
   * information.
   *
   * This is a HAR-like object. Conformance to the spec is not guaranteed at
   * this point.
   *
   * @see http://www.softwareishard.com/blog/har-12-spec
   * @param {nsIChannel} channel
   *        The channel for which the activity object is created.
   * @return object
   *         The new HTTP activity object.
   */
  #createOrGetActivityObject(channel) {
    let activity = this.#findActivityObject(channel);
    if (!activity) {
      const isHttpChannel = channel instanceof Ci.nsIHttpChannel;

      if (isHttpChannel) {
        // Most of the data needed from the channel is only available via the
        // nsIHttpChannelInternal interface.
        channel.QueryInterface(Ci.nsIHttpChannelInternal);
      } else {
        channel.QueryInterface(Ci.nsIChannel);
      }

      activity = {
        // The nsIChannel for which this activity object was created.
        channel,
        // See #prepareRequestBody()
        charset: isHttpChannel ? lazy.NetworkUtils.getCharset(channel) : null,
        // The postData sent by this request.
        sentBody: null,
        // The URL for the current channel.
        url: channel.URI.spec,
        // The encoded response body size.
        bodySize: 0,
        // The response headers size.
        headersSize: 0,
        // needed for host specific security info but file urls do not have hostname
        hostname: isHttpChannel ? channel.URI.host : null,
        discardRequestBody: isHttpChannel
          ? !this.#saveRequestAndResponseBodies
          : false,
        discardResponseBody: isHttpChannel
          ? !this.#saveRequestAndResponseBodies
          : false,
        // internal timing information, see observeActivity()
        timings: {},
        // the activity owner which is notified when changes happen
        owner: null,
      };

      this.#openRequests.set(channel, activity);
    }

    return activity;
  }

  /**
   * Block a request based on certain filtering options.
   *
   * Currently, exact URL match or URL patterns are supported.
   */
  blockRequest(filter) {
    if (!filter || !filter.url) {
      // In the future, there may be other types of filters, such as domain.
      // For now, ignore anything other than URL.
      return;
    }

    this.#addBlockedUrl(filter.url);
  }

  /**
   * Unblock a request based on certain filtering options.
   *
   * Currently, exact URL match or URL patterns are supported.
   */
  unblockRequest(filter) {
    if (!filter || !filter.url) {
      // In the future, there may be other types of filters, such as domain.
      // For now, ignore anything other than URL.
      return;
    }

    this.#blockedURLs.delete(filter.url);
  }

  /**
   * Updates the list of blocked request strings
   *
   * This match will be a (String).includes match, not an exact URL match
   */
  setBlockedUrls(urls) {
    urls = urls || [];
    this.#blockedURLs = new Map();
    urls.forEach(url => this.#addBlockedUrl(url));
  }

  #addBlockedUrl(url) {
    this.#blockedURLs.set(url, lazy.wildcardToRegExp(url));
  }

  /**
   * Returns a list of blocked requests
   * Useful as blockedURLs is mutated by both console & netmonitor
   */
  getBlockedUrls() {
    return this.#blockedURLs.keys();
  }

  override(url, path) {
    this.#overrides.set(url, path);

    // Clear in-memory cache, so that the subsequent request reaches the
    // http handling and the override works.
    ChromeUtils.clearResourceCache({ url });
  }

  removeOverride(url) {
    this.#overrides.delete(url);

    ChromeUtils.clearResourceCache({ url });
  }

  /**
   * Setup the network response listener for the given HTTP activity. The
   * NetworkResponseListener is responsible for storing the response body.
   *
   * @private
   * @param object httpActivity
   *        The HTTP activity object we are tracking.
   */
  #setupResponseListener(httpActivity) {
    const channel = httpActivity.channel;
    channel.QueryInterface(Ci.nsITraceableChannel);

    if (!httpActivity.fromCache) {
      const throttler = this.#getThrottler();
      if (throttler) {
        httpActivity.downloadThrottle = throttler.manage(channel);
      }
    }

    // The response will be written into the outputStream of this pipe.
    // This allows us to buffer the data we are receiving and read it
    // asynchronously.
    // Both ends of the pipe must be blocking.
    const sink = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);

    // The streams need to be blocking because this is required by the
    // stream tee.
    sink.init(false, false, this.#responsePipeSegmentSize, PR_UINT32_MAX, null);

    // Add listener for the response body.
    const newListener = new lazy.NetworkResponseListener(
      httpActivity,
      this.#decodedCertificateCache,
      httpActivity.fromServiceWorker
    );

    // Remember the input stream, so it isn't released by GC.
    newListener.inputStream = sink.inputStream;
    newListener.sink = sink;

    const tee = Cc["@mozilla.org/network/stream-listener-tee;1"].createInstance(
      Ci.nsIStreamListenerTee
    );

    const originalListener = channel.setNewListener(tee);

    tee.init(originalListener, sink.outputStream, newListener);
  }

  /**
   * Handler for ACTIVITY_SUBTYPE_REQUEST_BODY_SENT. Read and record the request
   * body here. It will be available in addResponseStart.
   *
   * @private
   * @param object httpActivity
   *        The HTTP activity object we are working with.
   */
  #prepareRequestBody(httpActivity) {
    // Return early if we don't need the request body, or if we've
    // already found it.
    if (httpActivity.discardRequestBody || httpActivity.sentBody !== null) {
      return;
    }

    const sentBody = lazy.NetworkHelper.readPostTextFromRequest(
      httpActivity.channel,
      httpActivity.charset
    );

    if (sentBody !== null) {
      httpActivity.sentBody = sentBody;
    }
  }

  /**
   * Handler for ACTIVITY_SUBTYPE_TRANSACTION_CLOSE. This method updates the HAR
   * timing information on the HTTP activity object and clears the request
   * from the list of known open requests.
   *
   * @private
   * @param object httpActivity
   *        The HTTP activity object we work with.
   */
  #onTransactionClose(httpActivity) {
    if (httpActivity.owner) {
      const result = lazy.NetworkTimings.extractHarTimings(httpActivity);
      const serverTimings =
        lazy.NetworkTimings.extractServerTimings(httpActivity);

      httpActivity.owner.addServerTimings(serverTimings);
      httpActivity.owner.addEventTimings(
        result.total,
        result.timings,
        result.offsets
      );
    }
  }

  #sendRequestBody(httpActivity) {
    if (httpActivity.sentBody !== null) {
      const limit = Services.prefs.getIntPref(
        "devtools.netmonitor.requestBodyLimit"
      );
      const size = httpActivity.sentBody.length;
      if (size > limit && limit > 0) {
        httpActivity.sentBody = httpActivity.sentBody.substr(0, limit);
      }
      httpActivity.owner.addRequestPostData({
        text: httpActivity.sentBody,
        size,
      });
      httpActivity.sentBody = null;
    }
  }

  /*
   * Clears the open requests channel map.
   */
  clear() {
    this.#openRequests.clear();
  }

  /**
   * Suspend observer activity. This is called when the Network monitor actor stops
   * listening.
   */
  destroy() {
    if (this.#isDestroyed) {
      return;
    }

    if (Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_CONTENT) {
      gActivityDistributor.removeObserver(this);
      Services.obs.removeObserver(
        this.#httpResponseExaminer,
        "http-on-examine-response"
      );
      Services.obs.removeObserver(
        this.#httpResponseExaminer,
        "http-on-examine-cached-response"
      );
      Services.obs.removeObserver(
        this.#httpModifyExaminer,
        "http-on-modify-request"
      );
      Services.obs.removeObserver(
        this.#fileChannelExaminer,
        "file-channel-opened"
      );
      Services.obs.removeObserver(
        this.#dataChannelExaminer,
        "data-channel-opened"
      );

      Services.obs.removeObserver(
        this.#httpStopRequest,
        "http-on-stop-request"
      );
      Services.obs.removeObserver(
        this.#httpBeforeConnect,
        "http-on-before-connect"
      );
    } else {
      Services.obs.removeObserver(
        this.#httpFailedOpening,
        "http-on-failed-opening-request"
      );
    }

    Services.obs.removeObserver(
      this.#serviceWorkerRequest,
      "service-worker-synthesized-response"
    );

    this.#ignoreChannelFunction = null;
    this.#onNetworkEvent = null;
    this.#throttler = null;
    this.#decodedCertificateCache.clear();
    this.clear();

    this.#isDestroyed = true;
  }
}
