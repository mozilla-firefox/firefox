/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "obliviousHttpService",
  "@mozilla.org/network/oblivious-http-service;1",
  Ci.nsIObliviousHttpService
);

/**
 * For now, the configuration and relay we're using is the "common" one, but
 * the prefs are stored in a newtab-specific way. In the future, it's possible
 * that we'll have different OHTTP configurations and relays for different
 * types of resource requests, so we map the moz-cached-ohttp host to relay and
 * config prefs here.
 */
const HOST_MAP = new Map([
  [
    "newtab-image",
    {
      gatewayConfigURLPrefName:
        "browser.newtabpage.activity-stream.discoverystream.ohttp.configURL",
      relayURLPrefName:
        "browser.newtabpage.activity-stream.discoverystream.ohttp.relayURL",
    },
  ],
]);

/**
 * Protocol handler for the moz-cached-ohttp:// scheme. This handler enables
 * loading resources over Oblivious HTTP (OHTTP) from privileged about: content
 * processes, specifically for use for images in about:newtab. The handler
 * implements a cache-first strategy to minimize OHTTP requests while providing
 * fallback to OHTTP when resources are not available in the HTTP cache.
 */
export class MozCachedOHTTPProtocolHandler {
  /**
   * The protocol scheme handled by this handler.
   */
  scheme = "moz-cached-ohttp";

  /**
   * Injectable OHTTP service for testing. If null, uses the default service.
   */
  #injectedOHTTPService = null;

  /**
   * Determines whether a given port is allowed for this protocol.
   *
   * @param {number} _port
   *   The port number to check.
   * @param {string} _scheme
   *   The protocol scheme.
   * @returns {boolean}
   *   Always false as this protocol doesn't use ports.
   */
  allowPort(_port, _scheme) {
    return false;
  }

  /**
   * Creates a new channel for handling moz-cached-ohttp:// URLs.
   *
   * @param {nsIURI} uri
   *   The URI to create a channel for.
   * @param {nsILoadInfo} loadInfo
   *   Load information containing security context.
   * @returns {MozCachedOHTTPChannel}
   *   A new channel instance.
   * @throws {Components.Exception}
   *   If the request is not from a valid context
   */
  newChannel(uri, loadInfo) {
    // Check if we're in a privileged about content process
    if (
      Services.appinfo.remoteType == lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE
    ) {
      return new MozCachedOHTTPChannel(
        uri,
        loadInfo,
        this,
        this.#getOHTTPService(),
        !!this.#injectedOHTTPService /* inTestingMode */
      );
    }

    // In main process, allow system principals and check loading principal's remote type
    if (Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_DEFAULT) {
      const loadingPrincipal = loadInfo?.loadingPrincipal;
      if (loadingPrincipal) {
        // Allow system principals in parent process (for tests and internal usage)
        if (loadingPrincipal.isSystemPrincipal) {
          return new MozCachedOHTTPChannel(
            uri,
            loadInfo,
            this,
            this.#getOHTTPService(),
            !!this.#injectedOHTTPService /* inTestingMode */
          );
        }

        try {
          const remoteType =
            lazy.E10SUtils.getRemoteTypeForPrincipal(loadingPrincipal);
          if (remoteType === lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE) {
            return new MozCachedOHTTPChannel(
              uri,
              loadInfo,
              this,
              this.#getOHTTPService(),
              !!this.#injectedOHTTPService /* inTestingMode */
            );
          }
        } catch (e) {
          // E10SUtils might throw for invalid principals, fall through to rejection
        }
      }
    }

    throw Components.Exception(
      "moz-cached-ohttp protocol only accessible from privileged about content",
      Cr.NS_ERROR_INVALID_ARG
    );
  }

  /**
   * Gets both the gateway configuration and relay URI for making OHTTP
   * requests.
   *
   * @param {string} host
   *   A key for an entry in HOST_MAP that determines which OHTTP configuration
   *   and relay will be used for the request (e.g., "newtab-image").
   * @returns {Promise<{ohttpGatewayConfig: Uint8Array, relayURI: nsIURI}>}
   *   Promise that resolves to an object containing both OHTTP components:
   * @returns {Uint8Array} returns.ohttpGatewayConfig
   *   The binary OHTTP gateway configuration
   * @returns {nsIURI} returns.relayURI
   *   The nsIURI for the OHTTP relay endpoint
   * @throws {Error}
   *   If the host is unrecognized, or if either the gateway config URL or relay
   *   URL preferences are not configured, or if fetching the gateway
   *   configuration fails.
   */
  async getOHTTPGatewayConfigAndRelayURI(host) {
    let hostMapping = HOST_MAP.get(host);
    if (!hostMapping) {
      throw new Error(`Unrecognized host for OHTTP config: ${host}`);
    }
    if (
      hostMapping.gatewayConfigURL === undefined ||
      hostMapping.relayURL === undefined
    ) {
      XPCOMUtils.defineLazyPreferenceGetter(
        hostMapping,
        "gatewayConfigURL",
        hostMapping.gatewayConfigURLPrefName,
        ""
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        hostMapping,
        "relayURL",
        hostMapping.relayURLPrefName,
        ""
      );
    }
    if (!hostMapping.gatewayConfigURL) {
      throw new Error(
        `OHTTP Gateway config URL not configured for host: ${host}`
      );
    }
    if (!hostMapping.relayURL) {
      throw new Error(`OHTTP relay URL not configured for host: ${host}`);
    }

    const ohttpGatewayConfig = await lazy.ObliviousHTTP.getOHTTPConfig(
      hostMapping.gatewayConfigURL
    );

    return {
      ohttpGatewayConfig,
      relayURI: Services.io.newURI(hostMapping.relayURL),
    };
  }

  /**
   * Injects an OHTTP service for testing purposes.
   *
   * @param {nsIObliviousHttpService} service
   *   The service to inject, or null to use default.
   */
  injectOHTTPService(service) {
    this.#injectedOHTTPService = service;
  }

  /**
   * Gets the OHTTP service to use (injected or default).
   *
   * @returns {nsIObliviousHttpService}
   *   The OHTTP service to use.
   */
  #getOHTTPService() {
    return this.#injectedOHTTPService || lazy.obliviousHttpService;
  }

  QueryInterface = ChromeUtils.generateQI(["nsIProtocolHandler"]);
}

/**
 * Channel implementation for moz-cached-ohttp:// URLs. This channel first attempts
 * to load resources from the HTTP cache to avoid unnecessary OHTTP requests, and
 * falls back to loading via OHTTP if the resource is not cached.
 */
export class MozCachedOHTTPChannel {
  #uri;
  #loadInfo;
  #protocolHandler;
  #ohttpService;
  #listener = null;
  #loadFlags = 0;
  #status = Cr.NS_OK;
  #cancelled = false;
  #originalURI;
  #contentType = "";
  #contentCharset = "";
  #contentLength = -1;
  #owner = null;
  #securityInfo = null;
  #notificationCallbacks = null;
  #loadGroup = null;
  #pendingChannel = null;
  #startedRequest = false;
  #inTestingMode = false;

  /**
   * Constructs a new MozCachedOHTTPChannel.
   *
   * @param {nsIURI} uri
   *   The moz-cached-ohttp:// URI to handle.
   * @param {nsILoadInfo} loadInfo
   *   Load information for the request with security context.
   * @param {MozCachedOHTTPProtocolHandler} protocolHandler
   *   The protocol handler instance that created this channel.
   * @param {nsIObliviousHttpService} ohttpService
   *   The OHTTP service to use.
   * @param {boolean} inTestingMode
   *   True if the channel is in a mode that makes it easier to stub and
   *   mock things out while under test.
   */
  constructor(
    uri,
    loadInfo,
    protocolHandler,
    ohttpService,
    inTestingMode = false
  ) {
    this.#uri = uri;
    this.#loadInfo = loadInfo;
    this.#protocolHandler = protocolHandler;
    this.#ohttpService = ohttpService;
    this.#originalURI = uri;
    this.#inTestingMode = inTestingMode;
  }

  /**
   * Gets the URI for this channel.
   *
   * @returns {nsIURI}
   *   The channel's URI.
   */
  get URI() {
    return this.#uri;
  }

  /**
   * Gets or sets the original URI for this channel.
   *
   * @type {nsIURI}
   */
  get originalURI() {
    return this.#originalURI;
  }

  set originalURI(aURI) {
    this.#originalURI = aURI;
  }

  /**
   * Gets the current status of the channel.
   *
   * @returns {number}
   *   The channel status (nsresult).
   */
  get status() {
    return this.#status;
  }

  /**
   * Gets or sets the content type of the loaded resource.
   *
   * @type {string}
   */
  get contentType() {
    return this.#contentType;
  }

  set contentType(aContentType) {
    this.#contentType = aContentType;
  }

  /**
   * Gets or sets the content charset of the loaded resource.
   *
   * @type {string}
   */
  get contentCharset() {
    return this.#contentCharset;
  }

  set contentCharset(aContentCharset) {
    this.#contentCharset = aContentCharset;
  }

  /**
   * Gets or sets the content length of the loaded resource, in bytes, or -1
   * if unknown.
   *
   * @type {number}
   */
  get contentLength() {
    return this.#contentLength;
  }

  set contentLength(aContentLength) {
    this.#contentLength = aContentLength;
  }

  /**
   * Gets or sets the load flags for this channel.
   *
   * @type {number}
   */
  get loadFlags() {
    return this.#loadFlags;
  }

  set loadFlags(aLoadFlags) {
    this.#loadFlags = aLoadFlags;
  }

  /**
   * Gets or sets the load info for this channel.
   *
   * @type {nsILoadInfo}
   */
  get loadInfo() {
    return this.#loadInfo;
  }

  set loadInfo(aLoadInfo) {
    this.#loadInfo = aLoadInfo;
  }

  /**
   * Gets or sets the owner (principal) of this channel.
   *
   * @type {nsIPrincipal}
   */
  get owner() {
    return this.#owner;
  }

  set owner(aOwner) {
    this.#owner = aOwner;
  }

  /**
   * Gets the security info for this channel.
   *
   * @returns {nsITransportSecurityInfo}
   *   The security information for this channel.
   */
  get securityInfo() {
    return this.#securityInfo;
  }

  /**
   * Gets or sets the notification callbacks for this channel.
   *
   * @type {nsIInterfaceRequestor}
   */
  get notificationCallbacks() {
    return this.#notificationCallbacks;
  }

  set notificationCallbacks(aCallbacks) {
    this.#notificationCallbacks = aCallbacks;
  }

  /**
   * Gets or sets the load group for this channel.
   *
   * @type {nsILoadGroup}
   */
  get loadGroup() {
    return this.#loadGroup;
  }

  set loadGroup(aLoadGroup) {
    this.#loadGroup = aLoadGroup;
  }

  /**
   * Gets the name of this channel (its URI spec).
   *
   * @returns {string}
   *   The channel name.
   */
  get name() {
    return this.#uri.spec;
  }

  /**
   * Checks if this channel has a pending request.
   *
   * @returns {boolean}
   *   True if there is a pending request.
   */
  isPending() {
    return this.#pendingChannel ? this.#pendingChannel.isPending() : false;
  }

  /**
   * Cancels the channel with the given status.
   *
   * @param {number} status
   *   The status code to cancel with.
   */
  cancel(status) {
    this.#cancelled = true;
    this.#status = status;

    if (this.#pendingChannel) {
      this.#pendingChannel.cancel(status);
    }
  }

  /**
   * Suspends the channel if it has a pending request.
   */
  suspend() {
    if (this.#pendingChannel) {
      this.#pendingChannel.suspend();
    }
  }

  /**
   * Resumes the channel if it has a pending request.
   */
  resume() {
    if (this.#pendingChannel) {
      this.#pendingChannel.resume();
    }
  }

  /**
   * Opens the channel synchronously. Not supported for this protocol.
   *
   * @throws {Components.Exception}
   *   Always throws as sync open is not supported.
   */
  open() {
    throw Components.Exception(
      "moz-cached-ohttp protocol does not support synchronous open",
      Cr.NS_ERROR_NOT_IMPLEMENTED
    );
  }

  /**
   * Opens the channel asynchronously.
   *
   * @param {nsIStreamListener} listener
   *   The stream listener to notify.
   * @throws {Components.Exception}
   *   If the channel was already cancelled.
   */
  asyncOpen(listener) {
    if (this.#cancelled) {
      throw Components.Exception("Channel was cancelled", this.#status);
    }

    this.#listener = listener;

    this.#loadResource().catch(error => {
      console.error("moz-cached-ohttp channel error:", error);
      this.#notifyError(Cr.NS_ERROR_FAILURE);
    });
  }

  /**
   * Loads the requested resource using a cache-first strategy.
   * First attempts to load from HTTP cache, then falls back to OHTTP if not
   * cached.
   *
   * @returns {Promise<undefined>}
   *   Promise that resolves when loading is complete.
   */
  async #loadResource() {
    const { resourceURI, host } = this.#extractHostAndResourceURI();
    if (!resourceURI) {
      throw new Error("Invalid moz-cached-ohttp URL format");
    }

    // Try cache first (avoid network racing)
    const wasCached = await this.#tryCache(resourceURI);
    if (wasCached) {
      // Nothing to do - we'll be streaming the response from the cache.
      return;
    }

    // Fallback to OHTTP
    if (!HOST_MAP.has(host)) {
      throw new Error(`Unrecognized moz-cached-ohttp host: ${host}`);
    }

    await this.#loadViaOHTTP(resourceURI, host);
  }

  /**
   * Extracts both the host and target resource URL from the moz-cached-ohttp://
   * URL.
   *
   * Expected format: moz-cached-ohttp://newtab-image/?url=<encoded-https-url>
   *
   * @returns {{resourceURI: nsIURI, host: string}|null}
   *   Object containing extracted data, or null if invalid.
   *   Returns null if URL parsing fails, no 'url' parameter found,
   *   target URL is not HTTPS, or target URL is malformed.
   *   `resourceURI` is the decoded target image URI (HTTPS only).
   *   `host` is the moz-cached-ohttp host (e.g., "newtab-image").
   */
  #extractHostAndResourceURI() {
    try {
      const url = new URL(this.#uri.spec);
      const host = url.host;
      const searchParams = new URLSearchParams(url.search);
      const resourceURLString = searchParams.get("url");

      if (!resourceURLString) {
        return null;
      }

      // Validate that the extracted URL is a valid HTTP/HTTPS URL
      const resourceURL = new URL(resourceURLString);
      if (resourceURL.protocol !== "https:") {
        return null;
      }

      return { resourceURI: Services.io.newURI(resourceURLString), host };
    } catch (e) {
      return null;
    }
  }

  /**
   * Attempts to load the resource from the HTTP cache without making network
   * requests.
   *
   * @param {nsIURI} resourceURI
   *   The URI of the resource to load from cache
   * @returns {Promise<boolean>}
   *   Promise that resolves to true if loaded from cache
   */
  async #tryCache(resourceURI) {
    let result;

    // Bug 1977139: Transferring nsIInputStream currently causes crashes
    // from in-process child actors, so only use the MozCachedOHTTPChild
    // if we're running outside of the parent process. If we're running in the
    // parent process, we can just talk to the parent actor directly.
    if (
      Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT
    ) {
      const [parentProcess] = ChromeUtils.getAllDOMProcesses();
      const parentActor = parentProcess.getActor("MozCachedOHTTP");
      result = await parentActor.tryCache(resourceURI);
    } else {
      const childActor = ChromeUtils.domProcessChild.getActor("MozCachedOHTTP");
      result = await childActor.sendQuery("tryCache", {
        uriString: resourceURI.spec,
      });
    }

    if (result.success) {
      // Stream from parent-provided inputStream
      const pump = this.#createInputStreamPump(result.inputStream);
      let headers = new Headers(result.headersObj);
      this.#applyContentHeaders(headers);

      await this.#streamFromCache(pump, result.inputStream);
      return true;
    }

    return false;
  }

  /**
   * Examines any response headers that were included with the cache entry or
   * network response, and sets any internal state to represent those headers.
   * For now, this is only the Content-Type header.
   *
   * @param {Headers} headers
   *   A Headers object that may have some header key value pairs included.
   */
  #applyContentHeaders(headers) {
    let contentTypeHeader = headers.get("Content-Type");
    if (contentTypeHeader) {
      let charSet = {};
      let hadCharSet = {};
      this.#contentType = Services.io.parseResponseContentType(
        contentTypeHeader,
        charSet,
        hadCharSet
      );

      if (hadCharSet.value) {
        this.#contentCharset = charSet.value;
      }
    }
  }

  /**
   * Creates an input stream pump for efficient data streaming.
   *
   * @param {nsIInputStream} inputStream
   *   The input stream to pump.
   * @returns {nsIInputStreamPump}
   *   The configured pump.
   */
  #createInputStreamPump(inputStream) {
    const pump = Cc["@mozilla.org/network/input-stream-pump;1"].createInstance(
      Ci.nsIInputStreamPump
    );
    pump.init(inputStream, 0, 0, false);
    return pump;
  }

  /**
   * Streams data from cache to the client listener.
   *
   * @param {nsIInputStreamPump} pump
   *   The input stream pump.
   * @param {nsIInputStream} inputStream
   *   The input stream for cleanup.
   * @returns {Promise<boolean>}
   *   Promise that resolves to true on success.
   */
  #streamFromCache(pump, inputStream) {
    return new Promise((resolve, reject) => {
      const listener = this.#createCacheStreamListener(
        pump,
        inputStream,
        resolve
      );

      try {
        pump.asyncRead(listener);
      } catch (error) {
        this.#safeCloseStream(inputStream);
        reject(error);
      }
    });
  }

  /**
   * Creates a stream listener for cache data streaming.
   *
   * @param {nsIInputStreamPump} pump
   *   The pump for channel tracking.
   * @param {nsIInputStream} inputStream
   *   Input stream for cleanup.
   * @param {Function} resolve
   *   Promise resolve function.
   * @returns {object}
   *   Stream listener implementation.
   */
  #createCacheStreamListener(pump, inputStream, resolve) {
    return {
      onStartRequest: () => {
        this.#startedRequest = true;
        this.#pendingChannel = pump;
        this.#listener.onStartRequest(this);
      },

      onDataAvailable: (request, innerInputStream, offset, count) => {
        this.#listener.onDataAvailable(this, innerInputStream, offset, count);
      },

      onStopRequest: (request, status) => {
        this.#pendingChannel = null;
        this.#safeCloseStream(inputStream);

        this.#listener.onStopRequest(this, status);
        resolve(Components.isSuccessCode(status));
      },
    };
  }

  /**
   * Safely closes an input stream, ignoring errors.
   *
   * @param {nsIInputStream} stream
   *   The stream to close.
   */
  #safeCloseStream(stream) {
    try {
      stream.close();
    } catch (e) {
      // Ignore close errors
    }
  }

  /**
   * Loads the resource via Oblivious HTTP when it's not available in cache.
   * Manually writes the response to the HTTP cache for future requests.
   *
   * @param {nsIURI} resourceURI
   *   The URI of the resource to load via OHTTP
   * @param {string} host
   *   A host matching one of the entries in HOST_MAP
   * @returns {Promise<void>} Promise that resolves when OHTTP loading is complete
   */
  async #loadViaOHTTP(resourceURI, host) {
    const { ohttpGatewayConfig, relayURI } =
      await this.#protocolHandler.getOHTTPGatewayConfigAndRelayURI(host);

    const ohttpChannel = this.#createOHTTPChannel(
      relayURI,
      resourceURI,
      ohttpGatewayConfig
    );

    return this.#executeOHTTPRequest(ohttpChannel);
  }

  /**
   * Creates and configures an OHTTP channel.
   *
   * @param {nsIURI} relayURI
   *   The OHTTP relay URI.
   * @param {nsIURI} resourceURI
   *   The target resource URI.
   * @param {Uint8Array} ohttpConfig
   *   The OHTTP configuration.
   * @returns {nsIChannel}
   *   The configured OHTTP channel.
   */
  #createOHTTPChannel(relayURI, resourceURI, ohttpConfig) {
    ChromeUtils.releaseAssert(
      Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT ||
        Services.appinfo.remoteType ===
          lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE,
      "moz-cached-ohttp is only allowed in privileged content processes " +
        "or the main process"
    );

    const ohttpChannel = this.#ohttpService.newChannel(
      relayURI,
      resourceURI,
      ohttpConfig
    );

    // I'm not sure I love this, but in order for the privileged about content
    // process to make requests to the relay and not get dinged by
    // OpaqueResponseBlocking, we need to make the load come from the system
    // principal.
    const loadInfo = lazy.NetUtil.newChannel({
      uri: relayURI,
      loadUsingSystemPrincipal: true,
      contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
    }).loadInfo;

    // Copy relevant properties from our channel to the OHTTP channel
    ohttpChannel.loadInfo = loadInfo;
    ohttpChannel.loadFlags = this.#loadFlags | Ci.nsIRequest.LOAD_ANONYMOUS;

    if (this.#notificationCallbacks) {
      ohttpChannel.notificationCallbacks = this.#notificationCallbacks;
    }

    if (this.#loadGroup) {
      ohttpChannel.loadGroup = this.#loadGroup;
    }

    return ohttpChannel;
  }

  /**
   * Executes the OHTTP request with caching support.
   *
   * @param {nsIChannel} ohttpChannel
   *   The OHTTP channel to execute.
   * @returns {Promise<undefined, Error>}
   *   Promise that resolves when request is complete.
   */
  #executeOHTTPRequest(ohttpChannel) {
    this.#pendingChannel = ohttpChannel;

    let cachePipe = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
    cachePipe.init(
      true /* non-blocking input */,
      false /* blocking output */,
      0 /* segment size */,
      0 /* max segments */
    );
    let cacheStreamUpdate = new MessageChannel();

    return new Promise((resolve, reject) => {
      // Bug 1977139: Transferring nsIInputStream currently causes crashes
      // from in-process child actors, so only use the MozCachedOHTTPChild
      // if we're running outside of the parent process. If we're running in the
      // parent process, we can just talk to the parent actor directly.
      if (
        Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_DEFAULT
      ) {
        const [parentProcess] = ChromeUtils.getAllDOMProcesses();
        const parentActor = parentProcess.getActor("MozCachedOHTTP");
        parentActor.writeCache(
          ohttpChannel.URI,
          cachePipe.inputStream,
          cacheStreamUpdate.port2
        );
      } else {
        const childActor =
          ChromeUtils.domProcessChild.getActor("MozCachedOHTTP");
        childActor.sendAsyncMessage(
          "writeCache",
          {
            uriString: ohttpChannel.URI.spec,
            cacheInputStream: cachePipe.inputStream,
            cacheStreamUpdatePort: cacheStreamUpdate.port2,
          },
          [cacheStreamUpdate.port2]
        );
      }

      const originalListener = this.#createOHTTPResponseListener(
        cacheStreamUpdate.port1,
        cachePipe.outputStream,
        resolve,
        reject
      );

      const finalListener = this.#setupStreamTee(
        originalListener,
        cachePipe.outputStream
      );

      try {
        ohttpChannel.asyncOpen(finalListener);
      } catch (error) {
        this.#cleanupCacheOnError(
          cacheStreamUpdate.port1,
          cachePipe.outputStream
        );
        reject(error);
      }
    });
  }

  /**
   * Creates a response listener for OHTTP requests with cache handling.
   *
   * @param {MessageChannelPort} cacheStreamUpdatePort
   *   MessagePort for sending cache control messages to the parent actor
   *   for operations like setting expiration time and dooming cache entries.
   * @param {nsIOutputStream} cachePipeOutputStream
   *   Output stream of the pipe used to send response data to the cache
   *   writer in the parent process via the JSActor.
   * @param {Function} resolve
   *   Promise resolve function.
   * @param {Function} reject
   *   Promise reject function.
   * @returns {nsIStreamListener}
   *   Stream listener implementation
   */
  #createOHTTPResponseListener(
    cacheStreamUpdatePort,
    cachePipeOutputStream,
    resolve,
    reject
  ) {
    return {
      onStartRequest: request => {
        // Copy content type from the OHTTP response to our channel
        if (request instanceof Ci.nsIHttpChannel) {
          try {
            const contentType = request.getResponseHeader("content-type");
            if (contentType) {
              let headers = new Headers();
              headers.set("content-type", contentType);
              this.#applyContentHeaders(headers);
            }
          } catch (e) {
            // Content-Type header not available
          }
        }

        this.#startedRequest = true;
        this.#listener.onStartRequest(this);
        this.#processResponseHeaders(cacheStreamUpdatePort, request);
        this.#processCacheControl(cacheStreamUpdatePort, request);
      },

      onDataAvailable: (request, inputStream, offset, count) => {
        this.#listener.onDataAvailable(this, inputStream, offset, count);
      },

      onStopRequest: (request, status) => {
        this.#pendingChannel = null;
        this.#finalizeCacheEntry(
          cacheStreamUpdatePort,
          cachePipeOutputStream,
          status
        );
        this.#listener.onStopRequest(this, status);

        if (Components.isSuccessCode(status)) {
          resolve();
        } else {
          reject(new Error(`OHTTP request failed with status: ${status}`));
        }
      },
    };
  }

  /**
   * Sets up stream tee for cache writing if cache output stream is available.
   *
   * @param {nsIStreamListener} originalListener
   *   The original stream listener.
   * @param {nsIOutputStream} cacheOutputStream
   *   Cache output stream (may be null).
   * @returns {nsIStreamListener}
   *   Final listener (tee or original).
   */
  #setupStreamTee(originalListener, cacheOutputStream) {
    if (!cacheOutputStream) {
      return originalListener;
    }

    try {
      const tee = Cc[
        "@mozilla.org/network/stream-listener-tee;1"
      ].createInstance(Ci.nsIStreamListenerTee);
      tee.init(originalListener, cacheOutputStream, null);
      return tee;
    } catch (error) {
      console.warn(
        "Failed to create stream tee, proceeding without caching:",
        error
      );
      this.#safeCloseStream(cacheOutputStream);
      return originalListener;
    }
  }

  /**
   * Finalizes cache entry after response completion.
   *
   * @param {MessageChannelPort} cacheStreamUpdatePort
   *   MessagePort for sending cache finalization messages to the parent actor.
   *   Used to doom the cache entry if the response failed.
   * @param {nsIOutputStream} cacheOutputStream
   *   Output stream to close.
   * @param {number} status
   *   Response status code.
   */
  #finalizeCacheEntry(cacheStreamUpdatePort, cacheOutputStream, status) {
    try {
      if (cacheOutputStream) {
        cacheOutputStream.closeWithStatus(Cr.NS_BASE_STREAM_CLOSED);
      }

      if (!Components.isSuccessCode(status)) {
        cacheStreamUpdatePort.postMessage({ name: "DoomCacheEntry" });
      }
    } catch (error) {
      console.warn("Failed to finalize cache entry:", error);
    }
  }

  /**
   * Cleans up cache resources on error.
   *
   * @param {MessageChannelPort} cacheStreamUpdatePort
   *   MessagePort for sending error cleanup messages to the parent actor.
   *   Used to doom the cache entry when an error occurs during the request.
   * @param {boolean} cacheOutputStream
   */
  #cleanupCacheOnError(cacheStreamUpdatePort, cacheOutputStream) {
    try {
      if (cacheOutputStream) {
        cacheOutputStream.closeWithStatus(Cr.NS_BASE_STREAM_CLOSED);
      }
      cacheStreamUpdatePort.postMessage({ name: "DoomCacheEntry" });
    } catch (e) {
      // Ignore cleanup errors
    }
  }

  /**
   * Notifies the listener of an error condition.
   *
   * @param {number} status
   *   The error status code.
   */
  #notifyError(status) {
    this.#status = status;
    if (this.#listener) {
      // Depending on when the error occurred, we may have already started
      // the request - but if not, start it.
      if (!this.#startedRequest) {
        this.#listener.onStartRequest(this);
      }
      this.#listener.onStopRequest(this, status);
    }
  }

  /**
   * Attempts to write the response headers into the cache entry.
   *
   * @param {MessageChannelPort} cacheStreamUpdatePort
   *   MessagePort for sending cache response headers to the parent actor.
   * @param {nsIHttpChannel} httpChannel
   *   The HTTP channel with response headers.
   */
  #processResponseHeaders(cacheStreamUpdatePort, httpChannel) {
    let headersObj = {};
    // nsIHttpChannel is marked as a builtinclass, meaning that it cannot
    // be implemented by script, which makes mocking out channels a pain. We
    // work around this by assuming that if we're in testing mode, that we've
    // been passed an nsIChannel implemented in JS that also happens to have a
    // visitResponseHeaders method implemented on it.
    if (this.#inTestingMode) {
      httpChannel = httpChannel.wrappedJSObject;
    } else if (!(httpChannel instanceof Ci.nsIHttpChannel)) {
      return;
    }

    httpChannel.visitResponseHeaders({
      visitHeader(name, value) {
        headersObj[name] = value;
      },
    });

    cacheStreamUpdatePort.postMessage({
      name: "WriteOriginalResponseHeaders",
      headersObj,
    });
  }

  /**
   * Sets appropriate cache expiration metadata based on response headers.
   *
   * @param {MessageChannelPort} cacheStreamUpdatePort
   *   MessagePort for sending cache metadata messages to the parent actor.
   *   Used to set cache expiration time and handle cache-control directives.
   * @param {nsIHttpChannel} httpChannel
   *   The HTTP channel with response headers.
   */
  #processCacheControl(cacheStreamUpdatePort, httpChannel) {
    if (!(httpChannel instanceof Ci.nsIHttpChannel)) {
      return;
    }

    let expirationTime = null;

    // Check for Cache-Control header first (takes precedence)
    let cacheControl = null;
    try {
      cacheControl = httpChannel.getResponseHeader("cache-control");
    } catch (e) {
      // Cache-Control header not available, continue to check Expires
    }

    if (cacheControl) {
      let cacheControlParseResult =
        Services.io.parseCacheControlHeader(cacheControl);
      // Respect max-age directive
      if (cacheControlParseResult.maxAge) {
        expirationTime = Date.now() + cacheControlParseResult.maxAge * 1000;
      } else if (
        cacheControlParseResult.noCache ||
        cacheControlParseResult.noStore
      ) {
        // Don't cache if explicitly prohibited
        cacheStreamUpdatePort.postMessage({ name: "DoomCacheEntry" });
        return;
      }
    }

    // Fallback to Expires header if Cache-Control max-age not found
    if (!expirationTime) {
      try {
        const expires = httpChannel.getResponseHeader("expires");
        if (expires) {
          expirationTime = new Date(expires).getTime();
        }
      } catch (e) {
        // Expires header not available
      }
    }

    // Set default expiration if no headers found (24 hours)
    expirationTime ??= Date.now() + 24 * 60 * 60 * 1000; // 24 hours

    // Set the expiration time in cache metadata
    cacheStreamUpdatePort.postMessage({
      name: "WriteCacheExpiry",
      expiry: Math.floor(expirationTime / 1000),
    });
  }

  QueryInterface = ChromeUtils.generateQI(["nsIChannel"]);
}
