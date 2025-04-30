/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    NetworkHelper:
      "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",
    NetworkTimings:
      "resource://devtools/shared/network-observer/NetworkTimings.sys.mjs",
  },
  { global: "contextual" }
);

ChromeUtils.defineLazyGetter(lazy, "tpFlagsMask", () => {
  const trackingProtectionLevel2Enabled = Services.prefs
    .getStringPref("urlclassifier.trackingTable")
    .includes("content-track-digest256");

  return trackingProtectionLevel2Enabled
    ? ~Ci.nsIClassifiedChannel.CLASSIFIED_ANY_BASIC_TRACKING &
        ~Ci.nsIClassifiedChannel.CLASSIFIED_ANY_STRICT_TRACKING
    : ~Ci.nsIClassifiedChannel.CLASSIFIED_ANY_BASIC_TRACKING &
        Ci.nsIClassifiedChannel.CLASSIFIED_ANY_STRICT_TRACKING;
});

/**
 * Convert a nsIContentPolicy constant to a display string
 */
const LOAD_CAUSE_STRINGS = {
  [Ci.nsIContentPolicy.TYPE_INVALID]: "invalid",
  [Ci.nsIContentPolicy.TYPE_OTHER]: "other",
  [Ci.nsIContentPolicy.TYPE_SCRIPT]: "script",
  [Ci.nsIContentPolicy.TYPE_IMAGE]: "img",
  [Ci.nsIContentPolicy.TYPE_STYLESHEET]: "stylesheet",
  [Ci.nsIContentPolicy.TYPE_OBJECT]: "object",
  [Ci.nsIContentPolicy.TYPE_DOCUMENT]: "document",
  [Ci.nsIContentPolicy.TYPE_SUBDOCUMENT]: "subdocument",
  [Ci.nsIContentPolicy.TYPE_PING]: "ping",
  [Ci.nsIContentPolicy.TYPE_XMLHTTPREQUEST]: "xhr",
  [Ci.nsIContentPolicy.TYPE_OBJECT_SUBREQUEST]: "objectSubdoc",
  [Ci.nsIContentPolicy.TYPE_DTD]: "dtd",
  [Ci.nsIContentPolicy.TYPE_FONT]: "font",
  [Ci.nsIContentPolicy.TYPE_MEDIA]: "media",
  [Ci.nsIContentPolicy.TYPE_WEBSOCKET]: "websocket",
  [Ci.nsIContentPolicy.TYPE_CSP_REPORT]: "csp",
  [Ci.nsIContentPolicy.TYPE_XSLT]: "xslt",
  [Ci.nsIContentPolicy.TYPE_BEACON]: "beacon",
  [Ci.nsIContentPolicy.TYPE_FETCH]: "fetch",
  [Ci.nsIContentPolicy.TYPE_IMAGESET]: "imageset",
  [Ci.nsIContentPolicy.TYPE_WEB_MANIFEST]: "webManifest",
  [Ci.nsIContentPolicy.TYPE_WEB_IDENTITY]: "webidentity",
};

function causeTypeToString(causeType, loadFlags, internalContentPolicyType) {
  let prefix = "";
  if (
    (causeType == Ci.nsIContentPolicy.TYPE_IMAGESET ||
      internalContentPolicyType == Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE) &&
    loadFlags & Ci.nsIRequest.LOAD_BACKGROUND
  ) {
    prefix = "lazy-";
  }

  return prefix + LOAD_CAUSE_STRINGS[causeType] || "unknown";
}

function stringToCauseType(value) {
  return Object.keys(LOAD_CAUSE_STRINGS).find(
    key => LOAD_CAUSE_STRINGS[key] === value
  );
}

function isChannelFromSystemPrincipal(channel) {
  let principal;

  if (channel.isDocument) {
    // The loadingPrincipal is the principal where the request will be used.
    principal = channel.loadInfo.loadingPrincipal;
  } else {
    // The triggeringPrincipal is the principal of the resource which triggered
    // the request. Except for document loads, this is normally the best way
    // to know if a request is done on behalf of a chrome resource.
    // For instance if a chrome stylesheet loads a resource which is used in a
    // content page, the loadingPrincipal will be a content principal, but the
    // triggeringPrincipal will be the system principal.
    principal = channel.loadInfo.triggeringPrincipal;
  }

  return !!principal?.isSystemPrincipal;
}

function isChromeFileChannel(channel) {
  if (!(channel instanceof Ci.nsIFileChannel)) {
    return false;
  }

  return (
    channel.originalURI.spec.startsWith("chrome://") ||
    channel.originalURI.spec.startsWith("resource://")
  );
}

function isPrivilegedChannel(channel) {
  return (
    isChannelFromSystemPrincipal(channel) ||
    isChromeFileChannel(channel) ||
    channel.loadInfo.isInDevToolsContext
  );
}

/**
 * Get the browsing context id for the channel.
 *
 * @param {*} channel
 * @returns {number}
 */
function getChannelBrowsingContextID(channel) {
  // `frameBrowsingContextID` is non-0 if the channel is loading an iframe.
  // If available, use it instead of `browsingContextID` which is exceptionally
  // set to the parent's BrowsingContext id for such channels.
  if (channel.loadInfo.frameBrowsingContextID) {
    return channel.loadInfo.frameBrowsingContextID;
  }

  if (channel.loadInfo.browsingContextID) {
    return channel.loadInfo.browsingContextID;
  }
  // At least WebSocket channel aren't having a browsingContextID set on their loadInfo
  // We fallback on top frame element, which works, but will be wrong for WebSocket
  // in same-process iframes...
  const topFrame = lazy.NetworkHelper.getTopFrameForRequest(channel);
  // topFrame is typically null for some chrome requests like favicons
  if (topFrame && topFrame.browsingContext) {
    return topFrame.browsingContext.id;
  }
  return null;
}

/**
 * Get the innerWindowId for the channel.
 *
 * @param {*} channel
 * @returns {number}
 */
function getChannelInnerWindowId(channel) {
  if (channel.loadInfo.innerWindowID) {
    return channel.loadInfo.innerWindowID;
  }
  // At least WebSocket channel aren't having a browsingContextID set on their loadInfo
  // We fallback on top frame element, which works, but will be wrong for WebSocket
  // in same-process iframes...
  const topFrame = lazy.NetworkHelper.getTopFrameForRequest(channel);
  // topFrame is typically null for some chrome requests like favicons
  if (topFrame?.browsingContext?.currentWindowGlobal) {
    return topFrame.browsingContext.currentWindowGlobal.innerWindowId;
  }
  return null;
}

/**
 * Does this channel represent a Preload request.
 *
 * @param {*} channel
 * @returns {boolean}
 */
function isPreloadRequest(channel) {
  const type = channel.loadInfo.internalContentPolicyType;
  return (
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_SCRIPT_PRELOAD ||
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_MODULE_PRELOAD ||
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_IMAGE_PRELOAD ||
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_STYLESHEET_PRELOAD ||
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_FONT_PRELOAD ||
    type == Ci.nsIContentPolicy.TYPE_INTERNAL_JSON_PRELOAD
  );
}

/**
 * Get the channel cause details.
 *
 * @param {nsIChannel} channel
 * @returns {Object}
 *          - loadingDocumentUri {string} uri of the document which created the
 *            channel
 *          - type {string} cause type as string
 */
function getCauseDetails(channel) {
  // Determine the cause and if this is an XHR request.
  let causeType = Ci.nsIContentPolicy.TYPE_OTHER;
  let causeUri = null;

  if (channel.loadInfo) {
    causeType = channel.loadInfo.externalContentPolicyType;
    const { loadingPrincipal } = channel.loadInfo;
    if (loadingPrincipal) {
      causeUri = loadingPrincipal.spec;
    }
  }

  return {
    loadingDocumentUri: causeUri,
    type: causeTypeToString(
      causeType,
      channel.loadFlags,
      channel.loadInfo.internalContentPolicyType
    ),
  };
}

/**
 * Get the channel priority. Priority is a number which typically ranges from
 * -20 (lowest priority) to 20 (highest priority). Can be null if the channel
 * does not implement nsISupportsPriority.
 *
 * @param {nsIChannel} channel
 * @returns {number|undefined}
 */
function getChannelPriority(channel) {
  if (channel instanceof Ci.nsISupportsPriority) {
    return channel.priority;
  }

  return null;
}

/**
 * Get the channel HTTP version as an uppercase string starting with "HTTP/"
 * (eg "HTTP/2").
 *
 * @param {nsIChannel} channel
 * @returns {string}
 */
function getHttpVersion(channel) {
  if (!(channel instanceof Ci.nsIHttpChannelInternal)) {
    return null;
  }

  // Determine the HTTP version.
  const httpVersionMaj = {};
  const httpVersionMin = {};

  channel.QueryInterface(Ci.nsIHttpChannelInternal);
  channel.getResponseVersion(httpVersionMaj, httpVersionMin);

  // The official name HTTP version 2.0 and 3.0 are HTTP/2 and HTTP/3, omit the
  // trailing `.0`.
  if (httpVersionMin.value == 0) {
    return "HTTP/" + httpVersionMaj.value;
  }

  return "HTTP/" + httpVersionMaj.value + "." + httpVersionMin.value;
}

const UNKNOWN_PROTOCOL_STRINGS = ["", "unknown"];
const HTTP_PROTOCOL_STRINGS = ["http", "https"];
/**
 * Get the protocol for the provided httpActivity. Either the ALPN negotiated
 * protocol or as a fallback a protocol computed from the scheme and the
 * response status.
 *
 * TODO: The `protocol` is similar to another response property called
 * `httpVersion`. `httpVersion` is uppercase and purely computed from the
 * response status, whereas `protocol` uses nsIHttpChannel.protocolVersion by
 * default and otherwise falls back on `httpVersion`. Ideally we should merge
 * the two properties.
 *
 * @param {Object} httpActivity
 *     The httpActivity object for which we need to get the protocol.
 *
 * @returns {string}
 *     The protocol as a string.
 */
function getProtocol(channel) {
  let protocol = "";
  try {
    const httpChannel = channel.QueryInterface(Ci.nsIHttpChannel);
    // protocolVersion corresponds to ALPN negotiated protocol.
    protocol = httpChannel.protocolVersion;
  } catch (e) {
    // Ignore errors reading protocolVersion.
  }

  if (UNKNOWN_PROTOCOL_STRINGS.includes(protocol)) {
    protocol = channel.URI.scheme;
    const httpVersion = getHttpVersion(channel);
    if (
      typeof httpVersion == "string" &&
      HTTP_PROTOCOL_STRINGS.includes(protocol)
    ) {
      protocol = httpVersion.toLowerCase();
    }
  }

  return protocol;
}

/**
 * Get the channel referrer policy as a string
 * (eg "strict-origin-when-cross-origin").
 *
 * @param {nsIChannel} channel
 * @returns {string}
 */
function getReferrerPolicy(channel) {
  return channel.referrerInfo
    ? channel.referrerInfo.getReferrerPolicyString()
    : "";
}

/**
 * Check if the channel is private.
 *
 * @param {nsIChannel} channel
 * @returns {boolean}
 */
function isChannelPrivate(channel) {
  channel.QueryInterface(Ci.nsIPrivateBrowsingChannel);
  return channel.isChannelPrivate;
}

/**
 * Check if the channel data is loaded from the cache or not.
 *
 * @param {nsIChannel} channel
 *     The channel for which we need to check the cache status.
 *
 * @returns {boolean}
 *     True if the channel data is loaded from the cache, false otherwise.
 */
function isFromCache(channel) {
  if (channel instanceof Ci.nsICacheInfoChannel) {
    return channel.isFromCache();
  }

  return false;
}

const REDIRECT_STATES = [
  301, // HTTP Moved Permanently
  302, // HTTP Found
  303, // HTTP See Other
  307, // HTTP Temporary Redirect
];
/**
 * Check if the channel's status corresponds to a known redirect status.
 *
 * @param {nsIChannel} channel
 *     The channel for which we need to check the redirect status.
 *
 * @returns {boolean}
 *     True if the channel data is a redirect, false otherwise.
 */
function isRedirectedChannel(channel) {
  try {
    return REDIRECT_STATES.includes(channel.responseStatus);
  } catch (e) {
    // Throws NS_ERROR_NOT_AVAILABLE if the request was not sent yet.
  }
  return false;
}

/**
 * isNavigationRequest is true for the one request used to load a new top level
 * document of a given tab, or top level window. It will typically be false for
 * navigation requests of iframes, i.e. the request loading another document in
 * an iframe.
 *
 * @param {nsIChannel} channel
 * @return {boolean}
 */
function isNavigationRequest(channel) {
  return channel.isMainDocumentChannel && channel.loadInfo.isTopLevelLoad;
}

/**
 * Returns true  if the channel has been processed by URL-Classifier features
 * and is considered third-party with the top window URI, and if it has loaded
 * a resource that is classified as a tracker.
 *
 * @param {nsIChannel} channel
 * @return {boolean}
 */
function isThirdPartyTrackingResource(channel) {
  // Only consider channels classified as level-1 to be trackers if our preferences
  // would not cause such channels to be blocked in strict content blocking mode.
  // Make sure the value produced here is a boolean.
  return !!(
    channel instanceof Ci.nsIClassifiedChannel &&
    channel.isThirdPartyTrackingResource() &&
    (channel.thirdPartyClassificationFlags & lazy.tpFlagsMask) == 0
  );
}

/**
 * Retrieve the websocket channel for the provided channel, if available.
 * Returns null otherwise.
 *
 * @param {nsIChannel} channel
 * @returns {nsIWebSocketChannel|null}
 */
function getWebSocketChannel(channel) {
  let wsChannel = null;
  if (channel.notificationCallbacks) {
    try {
      wsChannel = channel.notificationCallbacks.QueryInterface(
        Ci.nsIWebSocketChannel
      );
    } catch (e) {
      // Not all channels implement nsIWebSocketChannel.
    }
  }
  return wsChannel;
}

/**
 * For a given channel, fetch the request's headers and cookies.
 *
 * @param {nsIChannel} channel
 * @return {Object}
 *     An object with two properties:
 *     @property {Array<Object>} cookies
 *         Array of { name, value } objects.
 *     @property {Array<Object>} headers
 *         Array of { name, value } objects.
 */
function fetchRequestHeadersAndCookies(channel) {
  const headers = [];
  let cookies = [];
  let cookieHeader = null;

  // Copy the request header data.
  channel.visitRequestHeaders({
    visitHeader(name, value) {
      // The `Proxy-Authorization` header even though it appears on the channel is not
      // actually sent to the server for non CONNECT requests after the HTTP/HTTPS tunnel
      // is setup by the proxy.
      if (name == "Proxy-Authorization") {
        return;
      }
      if (name == "Cookie") {
        cookieHeader = value;
      }
      headers.push({ name, value });
    },
  });

  if (cookieHeader) {
    cookies = lazy.NetworkHelper.parseCookieHeader(cookieHeader);
  }

  return { cookies, headers };
}

/**
 * Parse the early hint raw headers string to an
 * array of name/value object header pairs
 *
 * @param {String} rawHeaders
 * @returns {Array}
 */
function parseEarlyHintsResponseHeaders(rawHeaders) {
  const headers = rawHeaders.split("\r\n");
  // Remove the line with the HTTP version and the status
  headers.shift();
  return headers
    .map(header => {
      const [name, value] = header.split(":");
      return { name, value };
    })
    .filter(header => header.name.length);
}

/**
 * For a given channel, fetch the response's headers and cookies.
 *
 * @param {nsIChannel} channel
 * @return {Object}
 *     An object with two properties:
 *     @property {Array<Object>} cookies
 *         Array of { name, value } objects.
 *     @property {Array<Object>} headers
 *         Array of { name, value } objects.
 */
function fetchResponseHeadersAndCookies(channel) {
  // Read response headers and cookies.
  const headers = [];
  const setCookieHeaders = [];

  const SET_COOKIE_REGEXP = /set-cookie/i;
  channel.visitOriginalResponseHeaders({
    visitHeader(name, value) {
      if (SET_COOKIE_REGEXP.test(name)) {
        setCookieHeaders.push(value);
      }
      headers.push({ name, value });
    },
  });

  return {
    cookies: lazy.NetworkHelper.parseSetCookieHeaders(setCookieHeaders),
    headers,
  };
}

/**
 * Check if a given network request should be logged by a network monitor
 * based on the specified filters.
 *
 * @param {(nsIHttpChannel|nsIFileChannel)} channel
 *        Request to check.
 * @param filters
 *        NetworkObserver filters to match against. An object with one of the following attributes:
 *        - sessionContext: When inspecting requests from the parent process, pass the WatcherActor's session context.
 *          This helps know what is the overall debugged scope.
 *          See watcher actor constructor for more info.
 *        - targetActor: When inspecting requests from the content process, pass the WindowGlobalTargetActor.
 *          This helps know what exact subset of request we should accept.
 *          This is especially useful to behave correctly regarding EFT, where we should include or not
 *          iframes requests.
 *        - browserId, addonId, window: All these attributes are legacy.
 *          Only browserId attribute is still used by the legacy WebConsoleActor startListener API.
 * @return boolean
 *         True if the network request should be logged, false otherwise.
 */
function matchRequest(channel, filters) {
  // NetworkEventWatcher should now pass a session context for the parent process codepath
  if (filters.sessionContext) {
    const { type } = filters.sessionContext;
    if (type == "all") {
      return true;
    }

    // Ignore requests from chrome or add-on code when we don't monitor the whole browser
    if (
      channel.loadInfo?.loadingDocument === null &&
      isPrivilegedChannel(channel)
    ) {
      return false;
    }

    // When a page fails loading in top level or in iframe, an error page is shown
    // which will trigger a request to about:neterror (which is translated into a file:// URI request).
    // Ignore this request in regular toolbox (but not in the browser toolbox).
    if (channel.loadInfo?.loadErrorPage) {
      return false;
    }

    if (type == "browser-element") {
      if (!channel.loadInfo.browsingContext) {
        const topFrame = lazy.NetworkHelper.getTopFrameForRequest(channel);
        // `topFrame` is typically null for some chrome requests like favicons
        // And its `browsingContext` attribute might be null if the request happened
        // while the tab is being closed.
        return (
          topFrame?.browsingContext?.browserId ==
          filters.sessionContext.browserId
        );
      }
      return (
        channel.loadInfo.browsingContext.browserId ==
        filters.sessionContext.browserId
      );
    }
    if (type == "webextension") {
      return (
        channel.loadInfo?.loadingPrincipal?.addonId ===
        filters.sessionContext.addonId
      );
    }
    throw new Error("Unsupported session context type: " + type);
  }

  // NetworkEventContentWatcher and NetworkEventStackTraces pass a target actor instead, from the content processes
  // Because of EFT, we can't use session context as we have to know what exact windows the target actor covers.
  if (filters.targetActor) {
    // Ignore requests from chrome or add-on code when we don't monitor the whole browser
    if (
      filters.targetActor.sessionContext?.type !== "all" &&
      isPrivilegedChannel(channel)
    ) {
      return false;
    }

    // Bug 1769982 the target actor might be destroying and accessing windows will throw.
    // Ignore all further request when this happens.
    let windows;
    try {
      windows = filters.targetActor.windows;
    } catch (e) {
      return false;
    }
    const win = lazy.NetworkHelper.getWindowForRequest(channel);
    return windows.includes(win);
  }

  // This is fallback code for the legacy WebConsole.startListeners codepath,
  // which may still pass individual browserId/window/addonId attributes.
  // This should be removable once we drop the WebConsole codepath for network events
  // (bug 1721592 and followups)
  return legacyMatchRequest(channel, filters);
}

function legacyMatchRequest(channel, filters) {
  // Log everything if no filter is specified
  if (!filters.browserId && !filters.window && !filters.addonId) {
    return true;
  }

  // Ignore requests from chrome or add-on code when we are monitoring
  // content.
  if (
    channel.loadInfo?.loadingDocument === null &&
    (isChannelFromSystemPrincipal(channel) ||
      channel.loadInfo.isInDevToolsContext)
  ) {
    return false;
  }

  if (filters.window) {
    let win = lazy.NetworkHelper.getWindowForRequest(channel);
    if (filters.matchExactWindow) {
      return win == filters.window;
    }

    // Since frames support, this.window may not be the top level content
    // frame, so that we can't only compare with win.top.
    while (win) {
      if (win == filters.window) {
        return true;
      }
      if (win.parent == win) {
        break;
      }
      win = win.parent;
    }
    return false;
  }

  if (filters.browserId) {
    const topFrame = lazy.NetworkHelper.getTopFrameForRequest(channel);
    // `topFrame` is typically null for some chrome requests like favicons
    // And its `browsingContext` attribute might be null if the request happened
    // while the tab is being closed.
    if (topFrame?.browsingContext?.browserId == filters.browserId) {
      return true;
    }

    // If we couldn't get the top frame BrowsingContext from the loadContext,
    // look for it on channel.loadInfo instead.
    if (channel.loadInfo?.browsingContext?.browserId == filters.browserId) {
      return true;
    }
  }

  if (
    filters.addonId &&
    channel.loadInfo?.loadingPrincipal?.addonId === filters.addonId
  ) {
    return true;
  }

  return false;
}

function getBlockedReason(channel, fromCache = false) {
  let blockingExtension, blockedReason;
  const { status } = channel;

  try {
    const request = channel.QueryInterface(Ci.nsIHttpChannel);
    const properties = request.QueryInterface(Ci.nsIPropertyBag);

    blockedReason = request.loadInfo.requestBlockingReason;
    blockingExtension = properties.getProperty("cancelledByExtension");

    // WebExtensionPolicy is not available for workers
    if (typeof WebExtensionPolicy !== "undefined") {
      blockingExtension = WebExtensionPolicy.getByID(blockingExtension).name;
    }
  } catch (err) {
    // "cancelledByExtension" doesn't have to be available.
  }
  // These are platform errors which are not exposed to the users,
  // usually the requests (with these errors) might be displayed with various
  // other status codes.
  const ignoreList = [
    // These are emited when the request is already in the cache.
    "NS_ERROR_PARSED_DATA_CACHED",
    // This is emited when there is some issues around images e.g When the img.src
    // links to a non existent url. This is typically shown as a 404 request.
    "NS_IMAGELIB_ERROR_FAILURE",
    // This is emited when there is a redirect. They are shown as 301 requests.
    "NS_BINDING_REDIRECTED",
    // E.g Emited by send beacon requests.
    "NS_ERROR_ABORT",
    // This is emmited when browser.http.blank_page_with_error_response.enabled
    // is set to false, and a 404 or 500 request has no content.
    // They are shown as 404 or 500 requests.
    "NS_ERROR_NET_EMPTY_RESPONSE",
  ];

  // NS_BINDING_ABORTED are emmited when request are abruptly halted, these are valid and should not be ignored.
  // They can also be emmited for requests already cache which have the `cached` status, these should be ignored.
  if (fromCache) {
    ignoreList.push("NS_BINDING_ABORTED");
  }

  // If the request has not failed or is not blocked by a web extension, check for
  // any errors not on the ignore list. e.g When a host is not found (NS_ERROR_UNKNOWN_HOST).
  if (
    blockedReason == 0 &&
    !Components.isSuccessCode(status) &&
    !ignoreList.includes(ChromeUtils.getXPCOMErrorName(status))
  ) {
    blockedReason = ChromeUtils.getXPCOMErrorName(status);
  }

  return { blockingExtension, blockedReason };
}

function getCharset(channel) {
  const win = lazy.NetworkHelper.getWindowForRequest(channel);
  return win ? win.document.characterSet : null;
}

/**
 * Data channels are either handled in the parent process NetworkObserver for
 * navigation requests, or in content processes for any other request.
 *
 * This function allows to apply the same logic to build the network event actor
 * in both cases.
 *
 * @param {nsIDataChannel} channel
 *     The data channel for which we are creating a network event actor.
 * @param {object} networkEventActor
 *     The network event actor owning this resource.
 */
function handleDataChannel(channel, networkEventActor) {
  networkEventActor.addResponseStart({
    channel,
    fromCache: false,
    // According to the fetch spec for data URLs we can just hardcode
    // "Content-Type" header.
    rawHeaders: "content-type: " + channel.contentType,
  });

  // For data URLs we can not set up a stream listener as for http,
  // so we have to create a response manually and complete it.
  const response = {
    // TODO: Bug 1903807. Re-evaluate if it's correct to just return
    // zero for `bodySize` and `decodedBodySize`.
    bodySize: 0,
    decodedBodySize: 0,
    contentCharset: channel.contentCharset,
    contentLength: channel.contentLength,
    contentType: channel.contentType,
    mimeType: lazy.NetworkHelper.addCharsetToMimeType(
      channel.contentType,
      channel.contentCharset
    ),
    transferredSize: 0,
  };

  // For data URIs all timings can be set to zero.
  const result = lazy.NetworkTimings.getEmptyHARTimings();
  networkEventActor.addEventTimings(
    result.total,
    result.timings,
    result.offsets
  );

  const url = channel.URI.spec;
  response.text = url.substring(url.indexOf(",") + 1);
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

  // Note: `size`` is only used by DevTools, WebDriverBiDi relies on
  // `bodySize` and `decodedBodySize`. Waiting on Bug 1903807 to decide
  // if those fields should have non-0 values as well.
  response.size = response.text.length;

  // Security information is not relevant for data channel, but it should
  // not be considered as insecure either. Set empty string as security
  // state.
  networkEventActor.addSecurityInfo({ state: "" });
  networkEventActor.addResponseContent(response, {});
}

export const NetworkUtils = {
  causeTypeToString,
  fetchRequestHeadersAndCookies,
  fetchResponseHeadersAndCookies,
  getBlockedReason,
  getCauseDetails,
  getChannelBrowsingContextID,
  getChannelInnerWindowId,
  getChannelPriority,
  getCharset,
  getHttpVersion,
  getProtocol,
  getReferrerPolicy,
  getWebSocketChannel,
  handleDataChannel,
  isChannelFromSystemPrincipal,
  isChannelPrivate,
  isFromCache,
  isNavigationRequest,
  isPreloadRequest,
  isRedirectedChannel,
  isThirdPartyTrackingResource,
  matchRequest,
  parseEarlyHintsResponseHeaders,
  stringToCauseType,
};
