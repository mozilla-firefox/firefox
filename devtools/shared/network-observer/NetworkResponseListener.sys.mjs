/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
    NetworkHelper:
      "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",
    NetworkTimings:
      "resource://devtools/shared/network-observer/NetworkTimings.sys.mjs",
    NetworkUtils:
      "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",
    getResponseCacheObject:
      "resource://devtools/shared/platform/CacheEntry.sys.mjs",
  },
  { global: "contextual" }
);

// Network logging

/**
 * The network response listener implements the nsIStreamListener and
 * nsIRequestObserver interfaces. This is used within the NetworkObserver feature
 * to get the response body of the request.
 *
 * The code is mostly based on code listings from:
 *
 *   http://www.softwareishard.com/blog/firebug/
 *      nsitraceablechannel-intercept-http-traffic/
 *
 * @constructor
 * @param {Object} httpActivity
 *        HttpActivity object associated with this request. See NetworkObserver
 *        more information.
 * @param {Map} decodedCertificateCache
 *        A Map of certificate fingerprints to decoded certificates, to avoid
 *        repeatedly decoding previously-seen certificates.
 */
export class NetworkResponseListener {
  /**
   * The compressed and encoded response body size. Will progressively increase
   * until the full response is received.
   *
   * @type {Number}
   */
  #bodySize = 0;
  /**
   * The uncompressed, decoded response body size.
   *
   * @type {Number}
   */
  #decodedBodySize = 0;
  /**
   * nsIStreamListener created by nsIStreamConverterService.asyncConvertData
   *
   * @type {nsIStreamListener}
   */
  #converter = null;
  /**
   * See constructor argument of the same name.
   *
   * @type {Map}
   */
  #decodedCertificateCache;
  /**
   * Is the channel from a service worker
   *
   * @type {boolean}
   */
  #fromServiceWorker;
  /**
   * See constructor argument of the same name.
   *
   * @type {Object}
   */
  #httpActivity;
  /**
   * Set from sink.inputStream, mainly to prevent GC.
   *
   * @type {nsIInputStream}
   */
  // eslint-disable-next-line no-unused-private-class-members
  #inputStream = null;
  /**
   * Internal promise used to hold the completion of #getSecurityInfo.
   *
   * @type {Promise}
   */
  #onSecurityInfo = null;
  /**
   * Offset for the onDataAvailable calls where we pass the data from our pipe
   * to the converter.
   *
   * @type {Number}
   */
  #offset = 0;
  /**
   * Stores the received data as a string.
   *
   * @type {string}
   */
  #receivedData = "";
  /**
   * The nsIRequest we are started for.
   *
   * @type {nsIRequest}
   */
  #request = null;
  /**
   * The response will be written into the outputStream of this nsIPipe.
   * Both ends of the pipe must be blocking.
   *
   * @type {nsIPipe}
   */
  #sink = null;
  /**
   * Indicates if the response had a size greater than response body limit.
   *
   * @type {boolean}
   */
  #truncated = false;
  /**
   * Backup for existing notificationCallbacks set on the monitored channel.
   * Initialized in the constructor.
   *
   * @type {Object}
   */
  #wrappedNotificationCallbacks;

  constructor(httpActivity, decodedCertificateCache, fromServiceWorker) {
    this.#httpActivity = httpActivity;
    this.#decodedCertificateCache = decodedCertificateCache;
    this.#fromServiceWorker = fromServiceWorker;

    // Note that this is really only needed for the non-e10s case.
    // See bug 1309523.
    const channel = this.#httpActivity.channel;
    // If the channel already had notificationCallbacks, hold them here
    // internally so that we can forward getInterface requests to that object.
    this.#wrappedNotificationCallbacks = channel.notificationCallbacks;
    channel.notificationCallbacks = this;
  }

  set inputStream(inputStream) {
    this.#inputStream = inputStream;
  }

  set sink(sink) {
    this.#sink = sink;
  }

  // nsIInterfaceRequestor implementation

  /**
   * This object implements nsIProgressEventSink, but also needs to forward
   * interface requests to the notification callbacks of other objects.
   */
  getInterface(iid) {
    if (iid.equals(Ci.nsIProgressEventSink)) {
      return this;
    }
    if (this.#wrappedNotificationCallbacks) {
      return this.#wrappedNotificationCallbacks.getInterface(iid);
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  }

  /**
   * Forward notifications for interfaces this object implements, in case other
   * objects also implemented them.
   */
  #forwardNotification(iid, method, args) {
    if (!this.#wrappedNotificationCallbacks) {
      return;
    }
    try {
      const impl = this.#wrappedNotificationCallbacks.getInterface(iid);
      impl[method].apply(impl, args);
    } catch (e) {
      if (e.result != Cr.NS_ERROR_NO_INTERFACE) {
        throw e;
      }
    }
  }

  /**
   * Set the async listener for the given nsIAsyncInputStream. This allows us to
   * wait asynchronously for any data coming from the stream.
   *
   * @param nsIAsyncInputStream stream
   *        The input stream from where we are waiting for data to come in.
   * @param nsIInputStreamCallback listener
   *        The input stream callback you want. This is an object that must have
   *        the onInputStreamReady() method. If the argument is null, then the
   *        current callback is removed.
   * @return void
   */
  setAsyncListener(stream, listener) {
    // Asynchronously wait for the stream to be readable or closed.
    stream.asyncWait(listener, 0, 0, Services.tm.mainThread);
  }

  /**
   * Stores the received data, if request/response body logging is enabled. It
   * also does limit the number of stored bytes, based on the
   * `devtools.netmonitor.responseBodyLimit` pref.
   *
   * Learn more about nsIStreamListener at:
   * https://developer.mozilla.org/en/XPCOM_Interface_Reference/nsIStreamListener
   *
   * @param nsIRequest request
   * @param nsISupports context
   * @param nsIInputStream inputStream
   * @param unsigned long offset
   * @param unsigned long count
   */
  onDataAvailable(request, inputStream, offset, count) {
    const data = lazy.NetUtil.readInputStreamToString(inputStream, count);

    this.#decodedBodySize += count;

    if (!this.#httpActivity.discardResponseBody) {
      const limit = Services.prefs.getIntPref(
        "devtools.netmonitor.responseBodyLimit"
      );
      if (this.#receivedData.length <= limit || limit == 0) {
        this.#receivedData += lazy.NetworkHelper.convertToUnicode(
          data,
          request.contentCharset
        );
      }
      if (this.#receivedData.length > limit && limit > 0) {
        this.#receivedData = this.#receivedData.substr(0, limit);
        this.#truncated = true;
      }
    }
  }

  /**
   * See documentation at
   * https://developer.mozilla.org/En/NsIRequestObserver
   *
   * @param nsIRequest request
   * @param nsISupports context
   */
  onStartRequest(request) {
    request = request.QueryInterface(Ci.nsIChannel);
    // Converter will call this again, we should just ignore that.
    if (this.#request) {
      return;
    }

    this.#request = request;
    this.#onSecurityInfo = this.#getSecurityInfo();
    // We need to track the offset for the onDataAvailable calls where
    // we pass the data from our pipe to the converter.
    this.#offset = 0;

    const channel = this.#request;

    // Bug 1372115 - We should load bytecode cached requests from cache as the actual
    // channel content is going to be optimized data that reflects platform internals
    // instead of the content user expects (i.e. content served by HTTP server)
    // Note that bytecode cached is one example, there may be wasm or other usecase in
    // future.
    let isOptimizedContent = false;
    try {
      if (channel instanceof Ci.nsICacheInfoChannel) {
        isOptimizedContent = channel.alternativeDataType;
      }
    } catch (e) {
      // Accessing `alternativeDataType` for some SW requests throws.
    }
    if (isOptimizedContent) {
      let charset;
      try {
        charset = this.#request.contentCharset;
      } catch (e) {
        // Accessing the charset sometimes throws NS_ERROR_NOT_AVAILABLE when
        // reloading the page
      }
      if (!charset) {
        charset = this.#httpActivity.charset;
      }
      lazy.NetworkHelper.loadFromCache(
        this.#httpActivity.url,
        charset,
        this.#onComplete.bind(this)
      );
      return;
    }

    // In the multi-process mode, the conversion happens on the child
    // side while we can only monitor the channel on the parent
    // side. If the content is gzipped, we have to unzip it
    // ourself. For that we use the stream converter services.  Do not
    // do that for Service workers as they are run in the child
    // process.
    if (
      !this.#fromServiceWorker &&
      channel instanceof Ci.nsIEncodedChannel &&
      channel.contentEncodings &&
      !channel.applyConversion &&
      !channel.hasContentDecompressed
    ) {
      const encodingHeader = channel.getResponseHeader("Content-Encoding");
      const scs = Cc["@mozilla.org/streamConverters;1"].getService(
        Ci.nsIStreamConverterService
      );
      const encodings = encodingHeader.split(/\s*\t*,\s*\t*/);
      let nextListener = this;
      const acceptedEncodings = [
        "gzip",
        "deflate",
        "br",
        "x-gzip",
        "x-deflate",
        "zstd",
      ];
      for (const i in encodings) {
        // There can be multiple conversions applied
        const enc = encodings[i].toLowerCase();
        if (acceptedEncodings.indexOf(enc) > -1) {
          this.#converter = scs.asyncConvertData(
            enc,
            "uncompressed",
            nextListener,
            null
          );
          nextListener = this.#converter;
        }
      }
      if (this.#converter) {
        this.#converter.onStartRequest(this.#request, null);
      }
    }
    // Asynchronously wait for the data coming from the request.
    this.setAsyncListener(this.#sink.inputStream, this);
  }

  /**
   * Parse security state of this request and report it to the client.
   */
  async #getSecurityInfo() {
    // Many properties of the securityInfo (e.g., the server certificate or HPKP
    // status) are not available in the content process and can't be even touched safely,
    // because their C++ getters trigger assertions. This function is called in content
    // process for synthesized responses from service workers, in the parent otherwise.
    if (Services.appinfo.processType == Ci.nsIXULRuntime.PROCESS_TYPE_CONTENT) {
      return;
    }

    // Take the security information from the original nsIHTTPChannel instead of
    // the nsIRequest received in onStartRequest. If response to this request
    // was a redirect from http to https, the request object seems to contain
    // security info for the https request after redirect.
    const secinfo = this.#httpActivity.channel.securityInfo;
    const info = await lazy.NetworkHelper.parseSecurityInfo(
      secinfo,
      this.#request.loadInfo.originAttributes,
      this.#httpActivity,
      this.#decodedCertificateCache
    );
    let isRacing = false;
    try {
      const channel = this.#httpActivity.channel;
      if (channel instanceof Ci.nsICacheInfoChannel) {
        isRacing = channel.isRacing();
      }
    } catch (err) {
      // See the following bug for more details:
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1582589
    }

    this.#httpActivity.owner.addSecurityInfo(info, isRacing);
  }

  /**
   * Fetches cache information from CacheEntry
   * @private
   */
  async #fetchCacheInformation() {
    // TODO: This method is async and #httpActivity is nullified in the #destroy
    // method of this class. Backup httpActivity to avoid errors here.
    const httpActivity = this.#httpActivity;
    const cacheEntry = await lazy.getResponseCacheObject(this.#request);
    httpActivity.owner.addResponseCache({
      responseCache: cacheEntry,
    });
  }

  /**
   * Handle the onStopRequest by closing the sink output stream.
   *
   * For more documentation about nsIRequestObserver go to:
   * https://developer.mozilla.org/En/NsIRequestObserver
   */
  onStopRequest() {
    // Bug 1429365: onStopRequest may be called after onComplete for resources loaded
    // from bytecode cache.
    if (!this.#httpActivity) {
      return;
    }
    this.#sink.outputStream.close();
  }

  // nsIProgressEventSink implementation

  /**
   * Handle progress event as data is transferred.  This is used to record the
   * size on the wire, which may be compressed / encoded.
   */
  onProgress(request, progress) {
    this.#bodySize = progress;

    // Need to forward as well to keep things like Download Manager's progress
    // bar working properly.
    this.#forwardNotification(Ci.nsIProgressEventSink, "onProgress", arguments);
  }

  onStatus() {
    this.#forwardNotification(Ci.nsIProgressEventSink, "onStatus", arguments);
  }

  /**
   * Clean up the response listener once the response input stream is closed.
   * This is called from onStopRequest() or from onInputStreamReady() when the
   * stream is closed.
   * @return void
   */
  onStreamClose() {
    if (!this.#httpActivity) {
      return;
    }
    // Remove our listener from the request input stream.
    this.setAsyncListener(this.#sink.inputStream, null);

    let responseStatus;
    try {
      responseStatus = this.#httpActivity.channel.responseStatus;
    } catch (e) {
      // Will throw NS_ERROR_NOT_AVAILABLE if the response has not been received
      // yet.
    }
    if (this.#request.fromCache || responseStatus == 304) {
      this.#fetchCacheInformation();
    }

    if (!this.#httpActivity.discardResponseBody && this.#receivedData.length) {
      this.#onComplete(this.#receivedData);
    } else if (
      !this.#httpActivity.discardResponseBody &&
      responseStatus == 304
    ) {
      // Response is cached, so we load it from cache.
      let charset;
      try {
        charset = this.#request.contentCharset;
      } catch (e) {
        // Accessing the charset sometimes throws NS_ERROR_NOT_AVAILABLE when
        // reloading the page
      }
      if (!charset) {
        charset = this.#httpActivity.charset;
      }
      lazy.NetworkHelper.loadFromCache(
        this.#httpActivity.url,
        charset,
        this.#onComplete.bind(this)
      );
    } else {
      this.#onComplete();
    }
  }

  /**
   * Handler for when the response completes. This function cleans up the
   * response listener.
   *
   * @param string [data]
   *        Optional, the received data coming from the response listener or
   *        from the cache.
   */
  #onComplete(data) {
    // Make sure all the security and response content info are sent
    this.#getResponseContent(data);
    this.#onSecurityInfo.then(() => this.#destroy());
  }

  /**
   * Create the response object and send it to the client.
   */
  #getResponseContent(data) {
    const response = {
      mimeType: "",
      text: data || "",
    };

    response.bodySize = this.#bodySize;
    response.decodedBodySize = this.#decodedBodySize;
    // TODO: Stop exposing the decodedBodySize as `size` which is ambiguous.
    // Consumers should use `decodedBodySize` instead. See Bug 1808560.
    response.size = this.#decodedBodySize;
    response.headersSize = this.#httpActivity.headersSize;
    response.transferredSize = this.#bodySize + this.#httpActivity.headersSize;

    let charset = "";

    try {
      response.mimeType = this.#request.contentType;
      charset = this.#request.contentCharset;
    } catch (ex) {
      // Ignore.
    }

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

    response.mimeType = lazy.NetworkHelper.addCharsetToMimeType(
      response.mimeType,
      charset
    );

    this.#receivedData = "";

    // Check any errors or blocking scenarios which happen late in the cycle
    // e.g If a host is not found (NS_ERROR_UNKNOWN_HOST) or CORS blocking.
    const { blockingExtension, blockedReason } =
      lazy.NetworkUtils.getBlockedReason(
        this.#httpActivity.channel,
        this.#httpActivity.fromCache
      );

    if (this.#httpActivity.isOverridden) {
      // For overridden scripts, we will not get the usual start notification
      // for the request, so we add event timings and response start here.
      const timings = lazy.NetworkTimings.extractHarTimings(this.#httpActivity);
      this.#httpActivity.owner.addEventTimings(
        timings.total,
        timings.timings,
        timings.offsets
      );

      this.#httpActivity.owner.addResponseStart({
        channel: this.#httpActivity.channel,
        fromCache: this.#httpActivity.fromCache,
        rawHeaders: "",
      });
    }

    this.#httpActivity.owner.addResponseContent(response, {
      discardResponseBody: this.#httpActivity.discardResponseBody,
      truncated: this.#truncated,
      blockedReason,
      blockingExtension,
    });
  }

  #destroy() {
    this.#wrappedNotificationCallbacks = null;
    this.#httpActivity = null;
    this.#sink = null;
    this.#inputStream = null;
    this.#converter = null;
    this.#request = null;
  }

  /**
   * The nsIInputStreamCallback for when the request input stream is ready -
   * either it has more data or it is closed.
   *
   * @param nsIAsyncInputStream stream
   *        The sink input stream from which data is coming.
   * @returns void
   */
  onInputStreamReady(stream) {
    if (!(stream instanceof Ci.nsIAsyncInputStream) || !this.#httpActivity) {
      return;
    }

    let available = -1;
    try {
      // This may throw if the stream is closed normally or due to an error.
      available = stream.available();
    } catch (ex) {
      // Ignore.
    }

    if (available != -1) {
      if (available != 0) {
        if (this.#converter) {
          this.#converter.onDataAvailable(
            this.#request,
            stream,
            this.#offset,
            available
          );
        } else {
          this.onDataAvailable(this.#request, stream, this.#offset, available);
        }
      }
      this.#offset += available;
      this.setAsyncListener(stream, this);
    } else {
      this.onStreamClose();
      this.#offset = 0;
    }
  }

  QueryInterface = ChromeUtils.generateQI([
    "nsIStreamListener",
    "nsIInputStreamCallback",
    "nsIRequestObserver",
    "nsIInterfaceRequestor",
  ]);
}
