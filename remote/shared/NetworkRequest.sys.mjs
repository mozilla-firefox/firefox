/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetworkHelper:
    "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",
  NetworkUtils:
    "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",

  generateUUID: "chrome://remote/content/shared/UUID.sys.mjs",
  NavigableManager: "chrome://remote/content/shared/NavigableManager.sys.mjs",
  NavigationState: "chrome://remote/content/shared/NavigationManager.sys.mjs",
  NetworkDataBytes: "chrome://remote/content/shared/NetworkDataBytes.sys.mjs",
  notifyNavigationStarted:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
});

/**
 * The NetworkRequest class is a wrapper around the internal channel which
 * provides getters and methods closer to fetch's response concept
 * (https://fetch.spec.whatwg.org/#concept-response).
 */
export class NetworkRequest {
  #alreadyCompleted;
  #channel;
  #contextId;
  #eventRecord;
  #isDataURL;
  #navigationId;
  #navigationManager;
  #postData;
  #postDataSize;
  #rawHeaders;
  #redirectCount;
  #requestId;
  #timedChannel;
  #wrappedChannel;

  /**
   * NetworkRequest relies on WrappedChannel's id to identify requests. However
   * this id is generated based on a counter in each process. Therefore we can
   * have overlaps for network requests handled in different processes
   */
  static UNIQUE_ID_SUFFIX = lazy.generateUUID();

  /**
   *
   * @param {nsIChannel} channel
   *     The channel for the request.
   * @param {object} params
   * @param {NetworkEventRecord} params.networkEventRecord
   *     The NetworkEventRecord owning this NetworkRequest.
   * @param {NavigationManager} params.navigationManager
   *     The NavigationManager where navigations for the current session are
   *     monitored.
   * @param {string=} params.rawHeaders
   *     The request's raw (ie potentially compressed) headers
   */
  constructor(channel, params) {
    const { eventRecord, navigationManager, rawHeaders = "" } = params;

    this.#channel = channel;
    this.#eventRecord = eventRecord;
    this.#isDataURL = this.#channel instanceof Ci.nsIDataChannel;
    this.#navigationManager = navigationManager;
    this.#rawHeaders = rawHeaders;

    // Platform timestamp is in microseconds.
    const currentTimeStamp = Date.now() * 1000;
    this.#timedChannel =
      this.#channel instanceof Ci.nsITimedChannel
        ? this.#channel.QueryInterface(Ci.nsITimedChannel)
        : {
            redirectCount: 0,
            initiatorType: "",
            asyncOpenTime: currentTimeStamp,
            redirectStartTime: 0,
            redirectEndTime: 0,
            domainLookupStartTime: currentTimeStamp,
            domainLookupEndTime: currentTimeStamp,
            connectStartTime: currentTimeStamp,
            connectEndTime: currentTimeStamp,
            secureConnectionStartTime: currentTimeStamp,
            requestStartTime: currentTimeStamp,
            responseStartTime: currentTimeStamp,
            responseEndTime: currentTimeStamp,
          };
    this.#wrappedChannel = ChannelWrapper.get(channel);

    this.#redirectCount = this.#timedChannel.redirectCount;

    // The wrappedChannel id remains identical across redirects, whereas
    // nsIChannel.channelId is different for each and every request.
    // Add a suffix unique to the process where the event is handled.
    this.#requestId = `${this.#wrappedChannel.id.toString()}-${NetworkRequest.UNIQUE_ID_SUFFIX}`;

    this.#contextId = this.#getContextId();
    this.#navigationId = this.#getNavigationId();

    // The postData will no longer be available after the channel is closed.
    // Compute the postData and postDataSize properties, to be updated later if
    // `setRequestBody` is used.
    this.#updatePostData();
  }

  get alreadyCompleted() {
    return this.#alreadyCompleted;
  }

  get channel() {
    return this.#channel;
  }

  get contextId() {
    return this.#contextId;
  }

  get destination() {
    return this.#channel.loadInfo?.fetchDestination;
  }

  get errorText() {
    // TODO: Update with a proper error text. Bug 1873037.
    return ChromeUtils.getXPCOMErrorName(this.#channel.status);
  }

  get headers() {
    return this.#getHeadersList();
  }

  get headersSize() {
    // TODO: rawHeaders will not be updated after modifying the headers via
    // request interception. Need to find another way to retrieve the
    // information dynamically.
    return this.#rawHeaders.length;
  }

  get host() {
    if (this.isHttpChannel) {
      return this.#channel.URI.host;
    }
    return null;
  }

  get initiatorType() {
    const initiatorType = this.#timedChannel.initiatorType;
    if (initiatorType === "") {
      return null;
    }

    if (this.#isTopLevelDocumentLoad()) {
      return null;
    }

    return initiatorType;
  }

  get isHttpChannel() {
    return this.#channel instanceof Ci.nsIHttpChannel;
  }

  get method() {
    return this.#isDataURL ? "GET" : this.#channel.requestMethod;
  }

  get navigationId() {
    return this.#navigationId;
  }

  get originAttributesString() {
    return JSON.stringify(this.#channel.loadInfo?.originAttributes ?? {});
  }

  get postData() {
    return this.#postData;
  }

  get postDataSize() {
    return this.#postDataSize;
  }

  get redirectCount() {
    return this.#redirectCount;
  }

  get requestId() {
    return this.#requestId;
  }

  get serializedURL() {
    return this.#channel.URI.spec;
  }

  get supportsInterception() {
    // The request which doesn't have `wrappedChannel` can not be intercepted.
    return !!this.#wrappedChannel;
  }

  get timings() {
    return this.#getFetchTimings();
  }

  get wrappedChannel() {
    return this.#wrappedChannel;
  }

  set alreadyCompleted(value) {
    this.#alreadyCompleted = value;
  }

  /**
   * Add information about raw headers, collected from NetworkObserver events.
   *
   * @param {string} rawHeaders
   *     The raw headers.
   */
  addRawHeaders(rawHeaders) {
    this.#rawHeaders = rawHeaders || "";
  }

  /**
   * Clear a request header from the request's headers list.
   *
   * @param {string} name
   *     The header's name.
   */
  clearRequestHeader(name) {
    this.#channel.setRequestHeader(
      name, // aName
      "", // aValue="" as an empty value
      false // aMerge=false to force clearing the header
    );
  }

  /**
   * Returns the NetworkDataBytes instance representing the request body for
   * this request.
   *
   * @returns {NetworkDataBytes}
   */
  readAndProcessRequestBody = () => {
    return new lazy.NetworkDataBytes({
      getBytesValue: () => this.#postData.text,
      isBase64: this.#postData.isBase64,
    });
  };

  /**
   * Redirect the request to another provided URL.
   *
   * @param {string} url
   *     The URL to redirect to.
   */
  redirectTo(url) {
    this.#channel.transparentRedirectTo(Services.io.newURI(url));
  }

  /**
   * Set the request post body
   *
   * @param {string} body
   *     The body to set.
   */
  setRequestBody(body) {
    // Update the requestObserversCalled flag to allow modifying the request,
    // and reset once done.
    this.#channel.requestObserversCalled = false;

    try {
      this.#channel.QueryInterface(Ci.nsIUploadChannel2);
      const bodyStream = Cc[
        "@mozilla.org/io/string-input-stream;1"
      ].createInstance(Ci.nsIStringInputStream);
      bodyStream.setByteStringData(body);
      this.clearRequestHeader("Content-Length");
      this.#channel.explicitSetUploadStream(
        bodyStream,
        null,
        -1,
        this.#channel.requestMethod,
        false
      );
    } finally {
      // Make sure to reset the flag once the modification was attempted.
      this.#channel.requestObserversCalled = true;
      this.#updatePostData();
    }
  }

  /**
   * Set a request header
   *
   * @param {string} name
   *     The header's name.
   * @param {string} value
   *     The header's value.
   * @param {object} options
   * @param {boolean} options.merge
   *     True if the value should be merged with the existing value, false if it
   *     should override it. Defaults to false.
   */
  setRequestHeader(name, value, options) {
    const { merge = false } = options;
    this.#channel.setRequestHeader(name, value, merge);
  }

  /**
   * Update the request's method.
   *
   * @param {string} method
   *     The method to set.
   */
  setRequestMethod(method) {
    // Update the requestObserversCalled flag to allow modifying the request,
    // and reset once done.
    this.#channel.requestObserversCalled = false;

    try {
      this.#channel.requestMethod = method;
    } finally {
      // Make sure to reset the flag once the modification was attempted.
      this.#channel.requestObserversCalled = true;
    }
  }

  /**
   * Allows to bypass the actual network request and immediately respond with
   * the provided nsIReplacedHttpResponse.
   *
   * @param {nsIReplacedHttpResponse} replacedHttpResponse
   *     The replaced response to use.
   */
  setResponseOverride(replacedHttpResponse) {
    this.wrappedChannel.channel
      .QueryInterface(Ci.nsIHttpChannelInternal)
      .setResponseOverride(replacedHttpResponse);

    const rawHeaders = [];
    replacedHttpResponse.visitResponseHeaders({
      visitHeader(name, value) {
        rawHeaders.push(`${name}: ${value}`);
      },
    });

    // Setting an override bypasses the usual codepath for network responses.
    // There will be no notification about receiving a response.
    // However, there will be a notification about the end of the response.
    // Therefore, simulate a addResponseStart here to make sure we handle
    // addResponseContent properly.
    this.#eventRecord.prepareResponseStart({
      channel: this.#channel,
      fromCache: false,
      rawHeaders: rawHeaders.join("\n"),
    });
  }

  /**
   * Return a static version of the class instance.
   * This method is used to prepare the data to be sent with the events for cached resources
   * generated from the content process but need to be sent to the parent.
   */
  toJSON() {
    return {
      destination: this.destination,
      headers: this.headers,
      headersSize: this.headersSize,
      host: this.host,
      initiatorType: this.initiatorType,
      method: this.method,
      navigationId: this.navigationId,
      originAttributesString: this.originAttributesString,
      postData: this.postData,
      postDataSize: this.postDataSize,
      redirectCount: this.redirectCount,
      requestId: this.requestId,
      serializedURL: this.serializedURL,
      // Since this data is meant to be sent to the parent process
      // it will not be possible to intercept such request.
      supportsInterception: false,
      timings: this.timings,
    };
  }

  /**
   * Convert the provided request timing to a timing relative to the beginning
   * of the request. Note that https://w3c.github.io/resource-timing/#dfn-convert-fetch-timestamp
   * only expects high resolution timestamps (double in milliseconds) as inputs
   * of this method, but since platform timestamps are integers in microseconds,
   * they will be converted on the fly in this helper.
   *
   * @param {number} timing
   *     Platform TimeStamp for a request timing relative from the time origin
   *     in microseconds.
   * @param {number} requestTime
   *     Platform TimeStamp for the request start time relative from the time
   *     origin, in microseconds.
   *
   * @returns {number}
   *     High resolution timestamp (https://www.w3.org/TR/hr-time-3/#dom-domhighrestimestamp)
   *     for the request timing relative to the start time of the request, or 0
   *     if the provided timing was 0.
   */
  #convertTimestamp(timing, requestTime) {
    if (timing == 0) {
      return 0;
    }

    // Convert from platform timestamp to high resolution timestamp.
    return (timing - requestTime) / 1000;
  }

  #getContextId() {
    const id = lazy.NetworkUtils.getChannelBrowsingContextID(this.#channel);
    const browsingContext = BrowsingContext.get(id);
    return lazy.NavigableManager.getIdForBrowsingContext(browsingContext);
  }

  /**
   * Retrieve the Fetch timings for the NetworkRequest.
   *
   * @returns {object}
   *     Object with keys corresponding to fetch timing names, and their
   *     corresponding values.
   */
  #getFetchTimings() {
    const {
      asyncOpenTime,
      redirectStartTime,
      redirectEndTime,
      dispatchFetchEventStartTime,
      cacheReadStartTime,
      domainLookupStartTime,
      domainLookupEndTime,
      connectStartTime,
      connectEndTime,
      secureConnectionStartTime,
      requestStartTime,
      responseStartTime,
      responseEndTime,
    } = this.#timedChannel;

    // fetchStart should be the post-redirect start time, which should be the
    // first non-zero timing from: dispatchFetchEventStart, cacheReadStart and
    // domainLookupStart. See https://www.w3.org/TR/navigation-timing-2/#processing-model
    const fetchStartTime =
      dispatchFetchEventStartTime ||
      cacheReadStartTime ||
      domainLookupStartTime;

    // Bug 1805478: Per spec, the origin time should match Performance API's
    // timeOrigin for the global which initiated the request. This is not
    // available in the parent process, so for now we will use 0.
    const timeOrigin = 0;

    return {
      timeOrigin,
      requestTime: this.#convertTimestamp(asyncOpenTime, timeOrigin),
      redirectStart: this.#convertTimestamp(redirectStartTime, timeOrigin),
      redirectEnd: this.#convertTimestamp(redirectEndTime, timeOrigin),
      fetchStart: this.#convertTimestamp(fetchStartTime, timeOrigin),
      dnsStart: this.#convertTimestamp(domainLookupStartTime, timeOrigin),
      dnsEnd: this.#convertTimestamp(domainLookupEndTime, timeOrigin),
      connectStart: this.#convertTimestamp(connectStartTime, timeOrigin),
      connectEnd: this.#convertTimestamp(connectEndTime, timeOrigin),
      tlsStart: this.#convertTimestamp(secureConnectionStartTime, timeOrigin),
      tlsEnd: this.#convertTimestamp(connectEndTime, timeOrigin),
      requestStart: this.#convertTimestamp(requestStartTime, timeOrigin),
      responseStart: this.#convertTimestamp(responseStartTime, timeOrigin),
      responseEnd: this.#convertTimestamp(responseEndTime, timeOrigin),
    };
  }

  /**
   * Retrieve the list of headers for the NetworkRequest.
   *
   * @returns {Array.Array}
   *     Array of (name, value) tuples.
   */
  #getHeadersList() {
    const headers = [];

    if (this.#channel instanceof Ci.nsIHttpChannel) {
      this.#channel.visitRequestHeaders({
        visitHeader(name, value) {
          // The `Proxy-Authorization` header even though it appears on the channel is not
          // actually sent to the server for non CONNECT requests after the HTTP/HTTPS tunnel
          // is setup by the proxy.
          if (name == "Proxy-Authorization") {
            return;
          }
          headers.push([name, value]);
        },
      });
    }

    if (this.#channel instanceof Ci.nsIDataChannel) {
      // Data channels have no request headers.
      return [];
    }

    if (this.#channel instanceof Ci.nsIFileChannel) {
      // File channels have no request headers.
      return [];
    }

    return headers;
  }

  #getNavigationId() {
    if (!this.#channel.isDocument) {
      return null;
    }

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      this.#contextId
    );

    let navigation =
      this.#navigationManager.getNavigationForBrowsingContext(browsingContext);

    // `onBeforeRequestSent` might be too early for the NavigationManager.
    // If there is no ongoing navigation, create one ourselves.
    // TODO: Bug 1835704 to detect navigations earlier and avoid this.
    if (!navigation || navigation.state !== lazy.NavigationState.Started) {
      navigation = lazy.notifyNavigationStarted({
        contextDetails: { context: browsingContext },
        url: this.serializedURL,
      });
    }

    return navigation ? navigation.navigationId : null;
  }

  #isTopLevelDocumentLoad() {
    if (!this.#channel.isDocument) {
      return false;
    }

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      this.#contextId
    );
    return !browsingContext.parent;
  }

  #readPostDataFromRequestAsUTF8() {
    const postData = lazy.NetworkHelper.readPostDataFromRequest(
      this.#channel,
      "UTF-8"
    );

    if (postData === null || postData.data === null) {
      return null;
    }

    return {
      text: postData.isDecodedAsText ? postData.data : btoa(postData.data),
      isBase64: !postData.isDecodedAsText,
    };
  }

  #updatePostData() {
    const sentBody = this.#readPostDataFromRequestAsUTF8();
    if (sentBody) {
      this.#postData = sentBody;
      this.#postDataSize = sentBody.text.length;
    } else {
      this.#postData = null;
      this.#postDataSize = 0;
    }
  }
}
