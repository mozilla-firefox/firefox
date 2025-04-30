/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetworkUtils:
    "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",
});

/**
 * The NetworkResponse class is a wrapper around the internal channel which
 * provides getters and methods closer to fetch's response concept
 * (https://fetch.spec.whatwg.org/#concept-response).
 */
export class NetworkResponse {
  #channel;
  #decodedBodySize;
  #encodedBodySize;
  #fromCache;
  #fromServiceWorker;
  #isCachedResource;
  #isDataURL;
  #headersTransmittedSize;
  #status;
  #statusMessage;
  #totalTransmittedSize;
  #wrappedChannel;

  /**
   *
   * @param {nsIChannel} channel
   *     The channel for the response.
   * @param {object} params
   * @param {boolean} params.fromCache
   *     Whether the response was read from the cache or not.
   * @param {boolean} params.fromServiceWorker
   *     Whether the response is coming from a service worker or not.
   * @param {boolean} params.isCachedResource
   *     Whether the response is served by the stencil (image/CSS/JS) cache.
   * @param {string=} params.rawHeaders
   *     The response's raw (ie potentially compressed) headers
   */
  constructor(channel, params) {
    this.#channel = channel;
    const {
      fromCache,
      fromServiceWorker,
      isCachedResource,
      rawHeaders = "",
    } = params;
    this.#fromCache = fromCache;
    this.#fromServiceWorker = fromServiceWorker;
    this.#isCachedResource = isCachedResource;
    this.#isDataURL = this.#channel instanceof Ci.nsIDataChannel;
    this.#wrappedChannel = ChannelWrapper.get(channel);

    this.#decodedBodySize = 0;
    this.#encodedBodySize = 0;
    this.#headersTransmittedSize = rawHeaders.length;
    this.#totalTransmittedSize = rawHeaders.length;

    // See https://github.com/w3c/webdriver-bidi/issues/761
    // For 304 responses, the response will be replaced by the cached response
    // between responseStarted and responseCompleted, which will effectively
    // change the status and statusMessage.
    // Until the issue linked above has been discussed and closed, we will
    // cache the status/statusMessage in order to ensure consistent values
    // between responseStarted and responseCompleted.
    this.#status = this.#isDataURL ? 200 : this.#channel.responseStatus;
    this.#statusMessage =
      this.#isDataURL || this.#isCachedResource
        ? "OK"
        : this.#channel.responseStatusText;
  }

  get decodedBodySize() {
    return this.#decodedBodySize;
  }

  get encodedBodySize() {
    return this.#encodedBodySize;
  }

  get headers() {
    return this.#getHeadersList();
  }

  get headersTransmittedSize() {
    return this.#headersTransmittedSize;
  }

  get fromCache() {
    return this.#fromCache;
  }

  get fromServiceWorker() {
    return this.#fromServiceWorker;
  }

  get mimeType() {
    return this.#getComputedMimeType();
  }

  get protocol() {
    return lazy.NetworkUtils.getProtocol(this.#channel);
  }

  get serializedURL() {
    return this.#channel.URI.spec;
  }

  get status() {
    return this.#status;
  }

  get statusMessage() {
    return this.#statusMessage;
  }

  get totalTransmittedSize() {
    return this.#totalTransmittedSize;
  }

  /**
   * Clear a response header from the responses's headers list.
   *
   * @param {string} name
   *     The header's name.
   */
  clearResponseHeader(name) {
    this.#channel.setResponseHeader(
      name, // aName
      "", // aValue="" as an empty value
      false // aMerge=false to force clearing the header
    );
  }

  /**
   * Set a response header
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
  setResponseHeader(name, value, options) {
    const { merge = false } = options;
    this.#channel.setResponseHeader(name, value, merge);
  }

  setResponseStatus(options) {
    let { status, statusText } = options;
    if (status === null) {
      status = this.#channel.responseStatus;
    }

    if (statusText === null) {
      statusText = this.#channel.responseStatusText;
    }

    this.#channel.setResponseStatus(status, statusText);

    // Update the cached status and statusMessage.
    this.#status = this.#channel.responseStatus;
    this.#statusMessage = this.#channel.responseStatusText;
  }

  /**
   * Set the various response sizes for this response. Depending on how the
   * completion was monitored (DevTools NetworkResponseListener or ChannelWrapper
   * event), sizes need to be retrieved differently.
   * There this is a simple setter and the actual logic to retrieve sizes is in
   * NetworkEventRecord.
   *
   * @param {object} sizes
   * @param {number} sizes.decodedBodySize
   *     The decoded body size.
   * @param {number} sizes.encodedBodySize
   *     The encoded body size.
   * @param {number} sizes.totalTransmittedSize
   *     The total transmitted size.
   */
  setResponseSizes(sizes) {
    const { decodedBodySize, encodedBodySize, totalTransmittedSize } = sizes;
    this.#decodedBodySize = decodedBodySize;
    this.#encodedBodySize = encodedBodySize;
    this.#totalTransmittedSize = totalTransmittedSize;
  }

  /**
   * Return a static version of the class instance.
   * This method is used to prepare the data to be sent with the events for cached resources
   * generated from the content process but need to be sent to the parent.
   */
  toJSON() {
    return {
      decodedBodySize: this.decodedBodySize,
      headers: this.headers,
      headersTransmittedSize: this.headersTransmittedSize,
      encodedBodySize: this.encodedBodySize,
      fromCache: this.fromCache,
      mimeType: this.mimeType,
      protocol: this.protocol,
      serializedURL: this.serializedURL,
      status: this.status,
      statusMessage: this.statusMessage,
      totalTransmittedSize: this.totalTransmittedSize,
    };
  }

  #getComputedMimeType() {
    // TODO: DevTools NetworkObserver is computing a similar value in
    // addResponseContent, but uses an inconsistent implementation in
    // addResponseStart. This approach can only be used as early as in
    // addResponseHeaders. We should move this logic to the NetworkObserver and
    // expose mimeType in addResponseStart. Bug 1809670.
    let mimeType = "";

    try {
      if (this.#isDataURL || this.#isCachedResource) {
        mimeType = this.#channel.contentType;
      } else {
        mimeType = this.#wrappedChannel.contentType;
      }
      const contentCharset = this.#channel.contentCharset;
      if (contentCharset) {
        mimeType += `;charset=${contentCharset}`;
      }
    } catch (e) {
      // Ignore exceptions when reading contentType/contentCharset
    }

    return mimeType;
  }

  #getHeadersList() {
    const headers = [];

    // According to the fetch spec for data URLs we can just hardcode
    // "Content-Type" header.
    if (this.#isDataURL) {
      headers.push(["Content-Type", this.#channel.contentType]);
    } else {
      this.#channel.visitResponseHeaders({
        visitHeader(name, value) {
          headers.push([name, value]);
        },
      });
    }

    return headers;
  }
}
