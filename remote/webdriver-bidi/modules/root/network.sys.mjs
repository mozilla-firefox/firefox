/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { RootBiDiModule } from "chrome://remote/content/webdriver-bidi/modules/RootBiDiModule.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NetworkHelper:
    "resource://devtools/shared/network-observer/NetworkHelper.sys.mjs",

  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  CacheBehavior: "chrome://remote/content/shared/NetworkCacheManager.sys.mjs",
  ContextDescriptorType:
    "chrome://remote/content/shared/messagehandler/MessageHandler.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  generateUUID: "chrome://remote/content/shared/UUID.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  matchURLPattern:
    "chrome://remote/content/shared/webdriver/URLPattern.sys.mjs",
  NavigableManager: "chrome://remote/content/shared/NavigableManager.sys.mjs",
  NetworkDataBytes: "chrome://remote/content/shared/NetworkDataBytes.sys.mjs",
  NetworkDecodedBodySizeMap:
    "chrome://remote/content/shared/NetworkDecodedBodySizeMap.sys.mjs",
  NetworkListener:
    "chrome://remote/content/shared/listeners/NetworkListener.sys.mjs",
  NetworkResponse: "chrome://remote/content/shared/NetworkResponse.sys.mjs",
  parseChallengeHeader:
    "chrome://remote/content/shared/ChallengeHeaderParser.sys.mjs",
  parseURLPattern:
    "chrome://remote/content/shared/webdriver/URLPattern.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  SessionDataCategory:
    "chrome://remote/content/shared/messagehandler/sessiondata/SessionData.sys.mjs",
  SessionDataMethod:
    "chrome://remote/content/shared/messagehandler/sessiondata/SessionData.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
  updateCacheBehavior:
    "chrome://remote/content/shared/NetworkCacheManager.sys.mjs",
  UserContextManager:
    "chrome://remote/content/shared/UserContextManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.WEBDRIVER_BIDI)
);

// WIP: See Bug 1994983, the invalid tests for network.setExtraHeaders expect
// the optional arguments not to accept null.
const NULL = Symbol("NULL");

/**
 * Defines the maximum total size as expected by the specification at
 * https://w3c.github.io/webdriver-bidi/#max-total-data-size
 */
const DEFAULT_MAX_TOTAL_SIZE = 200 * 1000 * 1000;
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "maxTotalDataSize",
  "remote.network.maxTotalDataSize",
  DEFAULT_MAX_TOTAL_SIZE
);

/**
 * @typedef {object} AuthChallenge
 * @property {string} scheme
 * @property {string} realm
 */

/**
 * @typedef {object} AuthCredentials
 * @property {'password'} type
 * @property {string} username
 * @property {string} password
 */

/**
 * @typedef {object} BaseParameters
 * @property {string=} context
 * @property {Array<string>?} intercepts
 * @property {boolean} isBlocked
 * @property {Navigation=} navigation
 * @property {number} redirectCount
 * @property {RequestData} request
 * @property {number} timestamp
 */

/**
 * @typedef {object} BlockedRequest
 * @property {NetworkEventRecord} networkEventRecord
 * @property {InterceptPhase} phase
 */

/**
 * Enum of possible BytesValue types.
 *
 * @readonly
 * @enum {BytesValueType}
 */
export const BytesValueType = {
  Base64: "base64",
  String: "string",
};

/**
 * @typedef {object} BytesValue
 * @property {BytesValueType} type
 * @property {string} value
 */

/**
 * Enum of possible network data collector types.
 *
 * @readonly
 * @enum {CollectorType}
 */
const CollectorType = {
  Blob: "blob",
};

/**
 * @typedef {object} Collector
 * @property {number} maxEncodedDataSize
 * @property {Set<DataType>} dataTypes
 * @property {string} collector
 * @property {CollectorType} collectorType
 * @property {Set<string>} userContexts
 */

/**
 * Enum of possible continueWithAuth actions.
 *
 * @readonly
 * @enum {ContinueWithAuthAction}
 */
const ContinueWithAuthAction = {
  Cancel: "cancel",
  Default: "default",
  ProvideCredentials: "provideCredentials",
};

/**
 * @typedef {object} Cookie
 * @property {string} domain
 * @property {number=} expires
 * @property {boolean} httpOnly
 * @property {string} name
 * @property {string} path
 * @property {SameSite} sameSite
 * @property {boolean} secure
 * @property {number} size
 * @property {BytesValue} value
 */

/**
 * @typedef {object} CookieHeader
 * @property {string} name
 * @property {BytesValue} value
 */

/**
 * Enum of possible network data types.
 *
 * @readonly
 * @enum {DataType}
 */
const DataType = {
  Request: "request",
  Response: "response",
};

/**
 * @typedef {object} Data
 * @property {BytesValue|null} bytes
 * @property {Array<Collector>} collectors
 * @property {boolean} pending
 * @property {string} request
 * @property {number} size
 * @property {DataType} type
 */

/**
 * @typedef {object} FetchTimingInfo
 * @property {number} timeOrigin
 * @property {number} requestTime
 * @property {number} redirectStart
 * @property {number} redirectEnd
 * @property {number} fetchStart
 * @property {number} dnsStart
 * @property {number} dnsEnd
 * @property {number} connectStart
 * @property {number} connectEnd
 * @property {number} tlsStart
 * @property {number} requestStart
 * @property {number} responseStart
 * @property {number} responseEnd
 */

/**
 * @typedef {object} Header
 * @property {string} name
 * @property {BytesValue} value
 */

/**
 * @typedef {string} InitiatorType
 */

/**
 * Enum of possible initiator types.
 *
 * @readonly
 * @enum {InitiatorType}
 */
const InitiatorType = {
  Other: "other",
  Parser: "parser",
  Preflight: "preflight",
  Script: "script",
};

/**
 * @typedef {object} Initiator
 * @property {InitiatorType} type
 * @property {number=} columnNumber
 * @property {number=} lineNumber
 * @property {string=} request
 * @property {StackTrace=} stackTrace
 */

/**
 * Enum of intercept phases.
 *
 * @readonly
 * @enum {InterceptPhase}
 */
const InterceptPhase = {
  AuthRequired: "authRequired",
  BeforeRequestSent: "beforeRequestSent",
  ResponseStarted: "responseStarted",
};

/**
 * @typedef {object} InterceptProperties
 * @property {Array<InterceptPhase>} phases
 * @property {Array<URLPattern>} urlPatterns
 */

/**
 * @typedef {object} RequestData
 * @property {number|null} bodySize
 *     Defaults to null.
 * @property {Array<Cookie>} cookies
 * @property {Array<Header>} headers
 * @property {number} headersSize
 * @property {string} method
 * @property {string} request
 * @property {FetchTimingInfo} timings
 * @property {string} url
 */

/**
 * @typedef {object} BeforeRequestSentParametersProperties
 * @property {Initiator} initiator
 */

/* eslint-disable jsdoc/valid-types */
/**
 * Parameters for the BeforeRequestSent event
 *
 * @typedef {BaseParameters & BeforeRequestSentParametersProperties} BeforeRequestSentParameters
 */
/* eslint-enable jsdoc/valid-types */

/**
 * @typedef {object} ResponseContent
 * @property {number|null} size
 *     Defaults to null.
 */

/**
 * @typedef {object} ResponseData
 * @property {string} url
 * @property {string} protocol
 * @property {number} status
 * @property {string} statusText
 * @property {boolean} fromCache
 * @property {Array<Header>} headers
 * @property {string} mimeType
 * @property {number} bytesReceived
 * @property {number|null} headersSize
 *     Defaults to null.
 * @property {number|null} bodySize
 *     Defaults to null.
 * @property {ResponseContent} content
 * @property {Array<AuthChallenge>=} authChallenges
 */

/**
 * @typedef {object} ResponseStartedParametersProperties
 * @property {ResponseData} response
 */

/* eslint-disable jsdoc/valid-types */
/**
 * Parameters for the ResponseStarted event
 *
 * @typedef {BaseParameters & ResponseStartedParametersProperties} ResponseStartedParameters
 */
/* eslint-enable jsdoc/valid-types */

/**
 * @typedef {object} ResponseCompletedParametersProperties
 * @property {ResponseData} response
 */

/**
 * Mapping from nsICookie sameSite constants to network.SameSite values.
 *
 * @readonly
 * @enum {SameSite}
 */
const NetworkCookieSameSiteType = {
  [Ci.nsICookie.SAMESITE_NONE]: "none",
  [Ci.nsICookie.SAMESITE_LAX]: "lax",
  [Ci.nsICookie.SAMESITE_STRICT]: "strict",
  [Ci.nsICookie.SAMESITE_UNSET]: "default",
};

/**
 * Enum of possible sameSite values.
 *
 * @readonly
 * @enum {SameSite}
 */
const SameSite = {
  Default: "default",
  Lax: "lax",
  None: "none",
  Strict: "strict",
};

/**
 * @typedef {object} SetCookieHeader
 * @property {string} name
 * @property {BytesValue} value
 * @property {string=} domain
 * @property {boolean=} httpOnly
 * @property {string=} expiry
 * @property {number=} maxAge
 * @property {string=} path
 * @property {SameSite=} sameSite
 * @property {boolean=} secure
 */

/**
 * @typedef {object} URLPatternPattern
 * @property {'pattern'} type
 * @property {string=} protocol
 * @property {string=} hostname
 * @property {string=} port
 * @property {string=} pathname
 * @property {string=} search
 */

/**
 * @typedef {object} URLPatternString
 * @property {'string'} type
 * @property {string} pattern
 */

/**
 * @typedef {(URLPatternPattern|URLPatternString)} URLPattern
 */

/* eslint-disable jsdoc/valid-types */
/**
 * Parameters for the ResponseCompleted event
 *
 * @typedef {BaseParameters & ResponseCompletedParametersProperties} ResponseCompletedParameters
 */
/* eslint-enable jsdoc/valid-types */

// @see https://searchfox.org/mozilla-central/rev/527d691a542ccc0f333e36689bd665cb000360b2/netwerk/protocol/http/HttpBaseChannel.cpp#2083-2088
const IMMUTABLE_RESPONSE_HEADERS = [
  "content-encoding",
  "content-length",
  "content-type",
  "trailer",
  "transfer-encoding",
];

const UNAVAILABLE_DATA_ERROR_REASON = {
  Aborted: "aborted",
  Evicted: "evicted",
};

class NetworkModule extends RootBiDiModule {
  #blockedRequests;
  #collectedNetworkData;
  #decodedBodySizeMap;
  #extraHeaders;
  #hasExtraHeaders;
  #hasNetworkConditionsOffline;
  #interceptMap;
  #networkCollectors;
  #networkListener;
  #redirectedRequests;
  #subscribedEvents;

  constructor(messageHandler) {
    super(messageHandler);

    // Map of request id to BlockedRequest
    this.#blockedRequests = new Map();

    // Map of collected network Data, from a composite key `${requestId}-${dataType}`
    // to a network Data struct.
    // https://w3c.github.io/webdriver-bidi/#collected-network-data
    // TODO: This is a property of the remote end per spec, not of the session.
    // At the moment, each network module starts its own network observer. This
    // makes it impossible to have a session agnostic step when receiving a new
    // network event.
    // Note: Implemented as a Map. A Map is guaranteed to iterate in the order
    // of insertion, but still provides fast lookup.
    this.#collectedNetworkData = new Map();

    // Implements the BiDi Session extra headers.
    // https://w3c.github.io/webdriver-bidi/#session-extra-headers
    this.#extraHeaders = {
      // Array of Header objects, initially empty.
      defaultHeaders: [],
      // WeakMap between navigables and arrays of Header objects.
      // Due to technical limitations, navigables are represented via the
      // BrowsingContextWebProgress of the top level browsing context.
      navigableHeaders: new WeakMap(),
      // Map between user context ids and arrays of Header objects.
      userContextHeaders: new Map(),
    };

    // Flags used to check if the internal listener should remain enabled even
    // when no public events are subscribed.
    this.#hasExtraHeaders = false;
    this.#hasNetworkConditionsOffline = false;

    // Map of intercept id to InterceptProperties
    this.#interceptMap = new Map();

    // Map of collector id to Collector
    this.#networkCollectors = new Map();

    // Set of request ids which are being redirected using continueRequest with
    // a url parameter. Those requests will lead to an additional beforeRequestSent
    // event which needs to be filtered out.
    this.#redirectedRequests = new Set();

    // Set of event names which have active subscriptions
    this.#subscribedEvents = new Set();

    this.#decodedBodySizeMap = new lazy.NetworkDecodedBodySizeMap();

    this.#networkListener = new lazy.NetworkListener(
      this.messageHandler.navigationManager,
      this.#decodedBodySizeMap
    );
    this.#networkListener.on("auth-required", this.#onAuthRequired);
    this.#networkListener.on("before-request-sent", this.#onBeforeRequestSent);
    this.#networkListener.on("fetch-error", this.#onFetchError);
    this.#networkListener.on("response-completed", this.#onResponseEvent);
    this.#networkListener.on("response-started", this.#onResponseEvent);

    lazy.UserContextManager.on(
      "user-context-deleted",
      this.#onUserContextDeleted
    );
  }

  destroy() {
    lazy.UserContextManager.off(
      "user-context-deleted",
      this.#onUserContextDeleted
    );

    this.#networkListener.off("auth-required", this.#onAuthRequired);
    this.#networkListener.off("before-request-sent", this.#onBeforeRequestSent);
    this.#networkListener.off("fetch-error", this.#onFetchError);
    this.#networkListener.off("response-completed", this.#onResponseEvent);
    this.#networkListener.off("response-started", this.#onResponseEvent);
    this.#networkListener.destroy();

    this.#decodedBodySizeMap.destroy();

    // Network related session cleanup steps
    // https://w3c.github.io/webdriver-bidi/#cleanup-the-session

    // Resume blocked requests
    for (const [, { request }] of this.#blockedRequests) {
      try {
        request.wrappedChannel.resume();
      } catch {
        lazy.logger.warn(
          `Failed to resume request "${request.requestId}" when ending the session`
        );
      }
    }

    // Remove collectors from collected data.
    // TODO: This step is unnecessary until we support multiple sessions, because
    // the collectedNetworkData is attached to the session and is cleaned up
    // afterwards.

    this.#blockedRequests = null;
    this.#collectedNetworkData = null;
    this.#decodedBodySizeMap = null;
    this.#extraHeaders = null;
    this.#interceptMap = null;
    this.#networkCollectors = null;
    this.#subscribedEvents = null;
  }

  /**
   * Adds a data collector to collect network data.
   *
   * @param {object=} options
   * @param {Array<DataType>} options.dataTypes
   *     Maximum size of data to collect in bytes.
   * @param {number} options.maxEncodedDataSize
   *     Maximum size of data to collect in bytes.
   * @param {CollectorType=} options.collectorType
   *     The type of data to collect. Optional, defaults to "blob".
   * @param {Array<string>=} options.contexts
   *     Optional list of browsing context ids.
   * @param {Array<string>=} options.userContexts
   *     Optional list of user context ids.
   *
   * @returns {object}
   *     An object with the following property:
   *     - collector {string} The unique id of the data collector.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {UnsupportedOperationError}
   *     Raised if neither "contexts" nor "userContexts" are provided in a
   *     session restricted to a single user context.
   */
  async addDataCollector(options = {}) {
    const {
      dataTypes,
      maxEncodedDataSize,
      collectorType = CollectorType.Blob,
      contexts: contextIds = null,
      userContexts: userContextIds = null,
    } = options;

    lazy.assert.positiveInteger(
      maxEncodedDataSize,
      lazy.pprint`Expected "maxEncodedDataSize" to be a positive integer, got ${maxEncodedDataSize}`
    );

    if (maxEncodedDataSize === 0) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "maxEncodedDataSize" to be greater than 0, got ${maxEncodedDataSize}`
      );
    }

    if (maxEncodedDataSize > lazy.maxTotalDataSize) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "maxEncodedDataSize" to be less than the max total data size available (${lazy.maxTotalDataSize}), got ${maxEncodedDataSize}`
      );
    }

    lazy.assert.isNonEmptyArray(
      dataTypes,
      `Expected "dataTypes" to be a non-empty array, got ${dataTypes}`
    );

    const supportedDataTypes = Object.values(DataType);
    for (const dataType of dataTypes) {
      if (!supportedDataTypes.includes(dataType)) {
        throw new lazy.error.InvalidArgumentError(
          `Expected "dataTypes" values to be one of ${supportedDataTypes},` +
            lazy.pprint` got ${dataType}`
        );
      }
    }

    const supportedCollectorTypes = Object.values(CollectorType);
    if (!supportedCollectorTypes.includes(collectorType)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "collectorType" to be one of ${supportedCollectorTypes},` +
          lazy.pprint` got ${collectorType}`
      );
    }

    if (contextIds !== null && userContextIds !== null) {
      throw new lazy.error.InvalidArgumentError(
        `Providing both "contexts" and "userContexts" arguments is not supported`
      );
    }

    if (
      contextIds === null &&
      userContextIds === null &&
      this._isSessionRestricted
    ) {
      throw new lazy.error.UnsupportedOperationError(
        `Adding a data collector without "contexts" or "userContexts" is not supported in a session restricted to a single user context`
      );
    }

    const navigables = new Set();
    const userContexts = new Set();
    if (contextIds !== null) {
      lazy.assert.isNonEmptyArray(
        contextIds,
        lazy.pprint`Expected "contexts" to be a non-empty array, got ${contextIds}`
      );

      for (const contextId of contextIds) {
        lazy.assert.string(
          contextId,
          lazy.pprint`Expected elements of "contexts" to be a string, got ${contextId}`
        );
        const context = this._getNavigable(contextId);

        lazy.assert.topLevel(
          context,
          lazy.pprint`Browsing context with id ${contextId} is not top-level`
        );

        navigables.add(contextId);
      }
    }

    if (userContextIds !== null) {
      lazy.assert.isNonEmptyArray(
        userContextIds,
        lazy.pprint`Expected "userContexts" to be a non-empty array, got ${userContextIds}`
      );

      for (const userContextId of userContextIds) {
        lazy.assert.string(
          userContextId,
          lazy.pprint`Expected elements of "userContexts" to be a string, got ${userContextId}`
        );

        this._assertUserContextId(userContextId);

        userContexts.add(userContextId);
      }
    }

    // Generate a unique collector ID
    const collectorId = lazy.generateUUID();

    const collector = {
      collector: collectorId,
      collectorType,
      contexts: navigables,
      dataTypes,
      maxEncodedDataSize,
      userContexts,
    };

    this.#networkCollectors.set(collectorId, collector);

    await this.#updateCollectorSessionData(
      collectorId,
      collector,
      lazy.SessionDataMethod.Add
    );

    return {
      collector: collectorId,
    };
  }

  /**
   * Adds a network intercept, which allows to intercept and modify network
   * requests and responses.
   *
   * The network intercept will be created for the provided phases
   * (InterceptPhase) and for specific url patterns. When a network event
   * corresponding to an intercept phase has a URL which matches any url pattern
   * of any intercept, the request will be suspended.
   *
   * @param {object=} options
   * @param {Array<string>=} options.contexts
   *     The list of browsing context ids where this intercept should be used.
   *     Optional, defaults to null.
   * @param {Array<InterceptPhase>} options.phases
   *     The phases where this intercept should be checked.
   * @param {Array<URLPattern>=} options.urlPatterns
   *     The URL patterns for this intercept. Optional, defaults to empty array.
   *
   * @returns {object}
   *     An object with the following property:
   *     - intercept {string} The unique id of the network intercept.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   */
  addIntercept(options = {}) {
    const { contexts = null, phases, urlPatterns = [] } = options;

    if (contexts !== null) {
      lazy.assert.isNonEmptyArray(
        contexts,
        `Expected "contexts" to be a non-empty array, got ${contexts}`
      );

      for (const contextId of contexts) {
        lazy.assert.string(
          contextId,
          `Expected elements of "contexts" to be a string, got ${contextId}`
        );
        const context = this._getNavigable(contextId);

        lazy.assert.topLevel(
          context,
          lazy.pprint`Browsing context with id ${contextId} is not top-level`
        );
      }
    }

    lazy.assert.isNonEmptyArray(
      phases,
      `Expected "phases" to be a non-empty array, got ${phases}`
    );

    const supportedInterceptPhases = Object.values(InterceptPhase);
    for (const phase of phases) {
      if (!supportedInterceptPhases.includes(phase)) {
        throw new lazy.error.InvalidArgumentError(
          `Expected "phases" values to be one of ${supportedInterceptPhases}, got ${phase}`
        );
      }
    }

    lazy.assert.array(
      urlPatterns,
      `Expected "urlPatterns" to be an array, got ${urlPatterns}`
    );

    const parsedPatterns = urlPatterns.map(urlPattern =>
      lazy.parseURLPattern(urlPattern)
    );

    const interceptId = lazy.generateUUID();
    this.#interceptMap.set(interceptId, {
      contexts,
      phases,
      urlPatterns: parsedPatterns,
    });

    return {
      intercept: interceptId,
    };
  }

  /**
   * Continues a request that is blocked by a network intercept at the
   * beforeRequestSent phase.
   *
   * @param {object=} options
   * @param {string} options.request
   *     The id of the blocked request that should be continued.
   * @param {BytesValue=} options.body
   *     Optional BytesValue to replace the body of the request.
   * @param {Array<CookieHeader>=} options.cookies
   *     Optional array of cookie header values to replace the cookie header of
   *     the request.
   * @param {Array<Header>=} options.headers
   *     Optional array of headers to replace the headers of the request.
   *     request.
   * @param {string=} options.method
   *     Optional string to replace the method of the request.
   * @param {string=} options.url
   *     Optional string to replace the url of the request. If the provided url
   *     is not a valid URL, an InvalidArgumentError will be thrown.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchRequestError}
   *     Raised if the request id does not match any request in the blocked
   *     requests map.
   */
  async continueRequest(options = {}) {
    const {
      body = null,
      cookies = null,
      headers = null,
      method = null,
      url = null,
      request: requestId,
    } = options;

    lazy.assert.string(
      requestId,
      `Expected "request" to be a string, got ${requestId}`
    );

    if (body !== null) {
      this.#assertBytesValue(
        body,
        lazy.truncate`Expected "body" to be a network.BytesValue, got ${body}`
      );
    }

    if (cookies !== null) {
      lazy.assert.array(
        cookies,
        `Expected "cookies" to be an array got ${cookies}`
      );

      for (const cookie of cookies) {
        this.#assertHeader(
          cookie,
          `Expected values in "cookies" to be network.CookieHeader, got ${cookie}`
        );
      }
    }

    let deserializedHeaders = [];
    if (headers !== null) {
      deserializedHeaders = this.#deserializeHeaders(headers);
    }

    if (method !== null) {
      lazy.assert.string(
        method,
        `Expected "method" to be a string, got ${method}`
      );
      lazy.assert.that(
        value => this.#isValidHttpToken(value),
        `Expected "method" to be a valid HTTP token, got ${method}`
      )(method);
    }

    if (url !== null) {
      lazy.assert.string(url, `Expected "url" to be a string, got ${url}`);

      if (!URL.canParse(url)) {
        throw new lazy.error.InvalidArgumentError(
          `Expected "url" to be a valid URL, got ${url}`
        );
      }
    }

    if (!this.#blockedRequests.has(requestId)) {
      throw new lazy.error.NoSuchRequestError(
        `Blocked request with id ${requestId} not found`
      );
    }

    const { phase, request, resolveBlockedEvent } =
      this.#blockedRequests.get(requestId);

    if (phase !== InterceptPhase.BeforeRequestSent) {
      throw new lazy.error.InvalidArgumentError(
        `Expected blocked request to be in "beforeRequestSent" phase, got ${phase}`
      );
    }

    if (method !== null) {
      request.setRequestMethod(method);
    }

    if (headers !== null) {
      // Delete all existing request headers.
      request.headers.forEach(([name]) => {
        request.clearRequestHeader(name);
      });

      // Set all headers specified in the headers parameter.
      for (const [name, value] of deserializedHeaders) {
        request.setRequestHeader(name, value, { merge: true });
      }
    }

    if (cookies !== null) {
      let cookieHeader = "";
      for (const cookie of cookies) {
        if (cookieHeader != "") {
          cookieHeader += ";";
        }
        cookieHeader += this.#serializeCookieHeader(cookie);
      }

      let foundCookieHeader = false;
      for (const [name] of request.headers) {
        if (name.toLowerCase() == "cookie") {
          // If there is already a cookie header, use merge: false to override
          // the value.
          request.setRequestHeader(name, cookieHeader, { merge: false });
          foundCookieHeader = true;
          break;
        }
      }

      if (!foundCookieHeader) {
        request.setRequestHeader("Cookie", cookieHeader, { merge: false });
      }
    }

    if (body !== null) {
      const value = deserializeBytesValue(body);
      request.setRequestBody(value);
    }

    if (url !== null) {
      // Store the requestId in the redirectedRequests set to skip the extra
      // beforeRequestSent event.
      this.#redirectedRequests.add(requestId);
      request.redirectTo(url);
    }

    request.wrappedChannel.resume();

    resolveBlockedEvent();
  }

  /**
   * Continues a response that is blocked by a network intercept at the
   * responseStarted or authRequired phase.
   *
   * @param {object=} options
   * @param {string} options.request
   *     The id of the blocked request that should be continued.
   * @param {Array<SetCookieHeader>=} options.cookies
   *     Optional array of set-cookie header values to replace the set-cookie
   *     headers of the response.
   * @param {AuthCredentials=} options.credentials
   *     Optional AuthCredentials to use.
   * @param {Array<Header>=} options.headers
   *     Optional array of header values to replace the headers of the response.
   * @param {string=} options.reasonPhrase
   *     Optional string to replace the status message of the response.
   * @param {number=} options.statusCode
   *     Optional number to replace the status code of the response.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchRequestError}
   *     Raised if the request id does not match any request in the blocked
   *     requests map.
   */
  async continueResponse(options = {}) {
    const {
      cookies = null,
      credentials = null,
      headers = null,
      reasonPhrase = null,
      request: requestId,
      statusCode = null,
    } = options;

    lazy.assert.string(
      requestId,
      `Expected "request" to be a string, got ${requestId}`
    );

    if (cookies !== null) {
      lazy.assert.array(
        cookies,
        `Expected "cookies" to be an array got ${cookies}`
      );

      for (const cookie of cookies) {
        this.#assertSetCookieHeader(cookie);
      }
    }

    if (credentials !== null) {
      this.#assertAuthCredentials(credentials);
    }

    let deserializedHeaders = [];
    if (headers !== null) {
      // For existing responses, are unable to update some response headers,
      // so we skip them for the time being and log a warning.
      // Bug 1914351 should remove this limitation.
      deserializedHeaders = this.#deserializeHeaders(headers).filter(
        ([name]) => {
          if (IMMUTABLE_RESPONSE_HEADERS.includes(name.toLowerCase())) {
            lazy.logger.warn(
              `network.continueResponse cannot currently modify the header "${name}", skipping (see Bug 1914351).`
            );
            return false;
          }
          return true;
        }
      );
    }

    if (reasonPhrase !== null) {
      lazy.assert.string(
        reasonPhrase,
        `Expected "reasonPhrase" to be a string, got ${reasonPhrase}`
      );
    }

    if (statusCode !== null) {
      lazy.assert.positiveInteger(
        statusCode,
        `Expected "statusCode" to be a positive integer, got ${statusCode}`
      );
    }

    if (!this.#blockedRequests.has(requestId)) {
      throw new lazy.error.NoSuchRequestError(
        `Blocked request with id ${requestId} not found`
      );
    }

    const { authCallbacks, phase, request, resolveBlockedEvent, response } =
      this.#blockedRequests.get(requestId);

    if (headers !== null) {
      // Delete all existing response headers.
      response.headers
        .filter(
          ([name]) =>
            // All headers in IMMUTABLE_RESPONSE_HEADERS cannot be changed and
            // will lead to a NS_ERROR_ILLEGAL_VALUE error.
            // Bug 1914351 should remove this limitation.
            !IMMUTABLE_RESPONSE_HEADERS.includes(name.toLowerCase())
        )
        .forEach(([name]) => response.clearResponseHeader(name));

      for (const [name, value] of deserializedHeaders) {
        response.setResponseHeader(name, value, { merge: true });
      }
    }

    if (cookies !== null) {
      for (const cookie of cookies) {
        const headerValue = this.#serializeSetCookieHeader(cookie);
        response.setResponseHeader("Set-Cookie", headerValue, { merge: true });
      }
    }

    if (statusCode !== null || reasonPhrase !== null) {
      response.setResponseStatus({
        status: statusCode,
        statusText: reasonPhrase,
      });
    }

    if (
      phase !== InterceptPhase.ResponseStarted &&
      phase !== InterceptPhase.AuthRequired
    ) {
      throw new lazy.error.InvalidArgumentError(
        `Expected blocked request to be in "responseStarted" or "authRequired" phase, got ${phase}`
      );
    }

    if (phase === InterceptPhase.AuthRequired) {
      // Requests blocked in the AuthRequired phase should be resumed using
      // authCallbacks.
      if (credentials !== null) {
        await authCallbacks.provideAuthCredentials(
          credentials.username,
          credentials.password
        );
      } else {
        await authCallbacks.provideAuthCredentials();
      }
    } else {
      request.wrappedChannel.resume();
    }

    resolveBlockedEvent();
  }

  /**
   * Continues a response that is blocked by a network intercept at the
   * authRequired phase.
   *
   * @param {object=} options
   * @param {string} options.request
   *     The id of the blocked request that should be continued.
   * @param {string} options.action
   *     The continueWithAuth action, one of ContinueWithAuthAction.
   * @param {AuthCredentials=} options.credentials
   *     The credentials to use for the ContinueWithAuthAction.ProvideCredentials
   *     action.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchRequestError}
   *     Raised if the request id does not match any request in the blocked
   *     requests map.
   */
  async continueWithAuth(options = {}) {
    const { action, credentials, request: requestId } = options;

    lazy.assert.string(
      requestId,
      `Expected "request" to be a string, got ${requestId}`
    );

    if (!Object.values(ContinueWithAuthAction).includes(action)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "action" to be one of ${Object.values(
          ContinueWithAuthAction
        )} got ${action}`
      );
    }

    if (action == ContinueWithAuthAction.ProvideCredentials) {
      this.#assertAuthCredentials(credentials);
    }

    if (!this.#blockedRequests.has(requestId)) {
      throw new lazy.error.NoSuchRequestError(
        `Blocked request with id ${requestId} not found`
      );
    }

    const { authCallbacks, phase, resolveBlockedEvent } =
      this.#blockedRequests.get(requestId);

    if (phase !== InterceptPhase.AuthRequired) {
      throw new lazy.error.InvalidArgumentError(
        `Expected blocked request to be in "authRequired" phase, got ${phase}`
      );
    }

    switch (action) {
      case ContinueWithAuthAction.Cancel: {
        authCallbacks.cancelAuthPrompt();
        break;
      }
      case ContinueWithAuthAction.Default: {
        authCallbacks.forwardAuthPrompt();
        break;
      }
      case ContinueWithAuthAction.ProvideCredentials: {
        await authCallbacks.provideAuthCredentials(
          credentials.username,
          credentials.password
        );

        break;
      }
    }

    resolveBlockedEvent();
  }

  /**
   * Releases a collected network data for a given collector and data type.
   *
   * @param {object=} options
   * @param {string} options.collector
   *     The collector from which the data should be disowned.
   * @param {string} options.dataType
   *     The data type of the data to disown.
   * @param {string} options.request
   *     The id of the request for which data should be disowned.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchNetworkCollectorError}
   *     Raised if the collector id could not be found in the internal collectors
   *     map.
   * @throws {NoSuchNetworkDataError}
   *     If the network data could not be found for the provided parameters.
   */
  async disownData(options = {}) {
    const { collector, dataType, request: requestId } = options;

    lazy.assert.string(
      requestId,
      lazy.pprint`Expected "request" to be a string, got ${requestId}`
    );

    const supportedDataTypes = Object.values(DataType);
    if (!supportedDataTypes.includes(dataType)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "dataType" to be one of ${supportedDataTypes},` +
          lazy.pprint` got ${dataType}`
      );
    }

    lazy.assert.string(
      collector,
      lazy.pprint`Expected "collector" to be a string, got ${collector}`
    );

    if (!this.#networkCollectors.has(collector)) {
      throw new lazy.error.NoSuchNetworkCollectorError(
        `Network data collector with id ${collector} not found`
      );
    }

    const collectedData = this.#getCollectedData(requestId, dataType);
    if (!collectedData) {
      throw new lazy.error.NoSuchNetworkDataError(
        `Network data for request id ${requestId} and DataType ${dataType} not found`
      );
    }

    if (!collectedData.collectors.has(collector)) {
      throw new lazy.error.NoSuchNetworkDataError(
        `Network data for request id ${requestId} and DataType ${dataType} does not match collector ${collector}`
      );
    }

    this.#removeCollectorFromData(collectedData, collector);
  }

  /**
   * An object that holds information about a network data content.
   *
   * @typedef NetworkGetDataResult
   *
   * @property {BytesValue} bytes
   *     The network data content as BytesValue.
   */

  /**
   * Retrieve a network data if available.
   *
   * @param {object} options
   * @param {string=} options.collector
   *     Optional id of a collector. If provided, data will only be returned if
   *     the collector is in the network data collectors.
   * @param {DataType} options.dataType
   *     The type of the data to retrieve.
   * @param {boolean=} options.disown
   *     Optional. If set to true, the collector parameter is mandatory and the
   *     collector will be removed from the network data collectors. Defaults to
   *     false.
   * @param {string} options.request
   *     The id of the request of the data to retrieve.
   *
   * @returns {NetworkGetDataResult}
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchNetworkCollectorError}
   *     Raised if the collector id could not be found in the internal collectors
   *     map.
   * @throws {NoSuchNetworkDataError}
   *     If the network data could not be found for the provided parameters.
   * @throws {UnavailableNetworkDataError}
   *     If the network data content is no longer available because it was
   *     evicted.
   */
  async getData(options = {}) {
    const {
      collector = null,
      dataType,
      disown = null,
      request: requestId,
    } = options;

    lazy.assert.string(
      requestId,
      lazy.pprint`Expected "request" to be a string, got ${requestId}`
    );

    const supportedDataTypes = Object.values(DataType);
    if (!supportedDataTypes.includes(dataType)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "dataType" to be one of ${supportedDataTypes},` +
          lazy.pprint` got ${dataType}`
      );
    }

    if (collector !== null) {
      lazy.assert.string(
        collector,
        lazy.pprint`Expected "collector" to be a string, got ${collector}`
      );

      if (!this.#networkCollectors.has(collector)) {
        throw new lazy.error.NoSuchNetworkCollectorError(
          `Network data collector with id ${collector} not found`
        );
      }
    }

    if (disown !== null) {
      lazy.assert.boolean(
        disown,
        lazy.pprint`Expected "disown" to be a boolean, got ${disown}`
      );

      if (disown && collector === null) {
        throw new lazy.error.InvalidArgumentError(
          `Expected "collector" to be provided when using "disown"=true`
        );
      }
    }

    const collectedData = this.#getCollectedData(requestId, dataType);
    if (!collectedData) {
      throw new lazy.error.NoSuchNetworkDataError(
        `Network data for request id ${requestId} and DataType ${dataType} not found`
      );
    }

    if (collectedData.pending) {
      await collectedData.networkDataCollected.promise;
    }

    if (collector !== null && !collectedData.collectors.has(collector)) {
      throw new lazy.error.NoSuchNetworkDataError(
        `Network data for request id ${requestId} and DataType ${dataType} does not match collector ${collector}`
      );
    }

    if (collectedData.bytes === null) {
      const reason = collectedData.unavailableReason;
      throw new lazy.error.UnavailableNetworkDataError(
        `Network data content for request id ${requestId} and DataType ${dataType} is unavailable (reason: ${reason})`
      );
    }

    const value = await collectedData.bytes.getBytesValue();

    if (disown) {
      this.#removeCollectorFromData(collectedData, collector);
    }

    const type = collectedData.bytes.isBase64
      ? BytesValueType.Base64
      : BytesValueType.String;

    // The data retrieved here is already either a UTF8-decoded string or a
    // base64 encoded binary. No need to re-apply the serializeAsBytesValue
    // algorithm.
    return {
      bytes: {
        type,
        value,
      },
    };
  }

  /**
   * Fails a request that is blocked by a network intercept.
   *
   * @param {object=} options
   * @param {string} options.request
   *     The id of the blocked request that should be continued.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchRequestError}
   *     Raised if the request id does not match any request in the blocked
   *     requests map.
   */
  async failRequest(options = {}) {
    const { request: requestId } = options;

    lazy.assert.string(
      requestId,
      `Expected "request" to be a string, got ${requestId}`
    );

    if (!this.#blockedRequests.has(requestId)) {
      throw new lazy.error.NoSuchRequestError(
        `Blocked request with id ${requestId} not found`
      );
    }

    const { phase, request, resolveBlockedEvent } =
      this.#blockedRequests.get(requestId);

    if (phase === InterceptPhase.AuthRequired) {
      throw new lazy.error.InvalidArgumentError(
        `Expected blocked request not to be in "authRequired" phase`
      );
    }

    request.wrappedChannel.resume();
    request.wrappedChannel.cancel(
      Cr.NS_ERROR_ABORT,
      Ci.nsILoadInfo.BLOCKING_REASON_WEBDRIVER_BIDI
    );

    resolveBlockedEvent();
  }

  /**
   * Continues a request that’s blocked by a network intercept, by providing a
   * complete response.
   *
   * @param {object=} options
   * @param {string} options.request
   *     The id of the blocked request for which the response should be
   *     provided.
   * @param {BytesValue=} options.body
   *     Optional BytesValue to replace the body of the response.
   *     For now, only supported for requests blocked in beforeRequestSent.
   * @param {Array<SetCookieHeader>=} options.cookies
   *     Optional array of set-cookie header values to use for the provided
   *     response.
   *     For now, only supported for requests blocked in beforeRequestSent.
   * @param {Array<Header>=} options.headers
   *     Optional array of header values to use for the provided
   *     response.
   *     For now, only supported for requests blocked in beforeRequestSent.
   * @param {string=} options.reasonPhrase
   *     Optional string to use as the status message for the provided response.
   *     For now, only supported for requests blocked in beforeRequestSent.
   * @param {number=} options.statusCode
   *     Optional number to use as the status code for the provided response.
   *     For now, only supported for requests blocked in beforeRequestSent.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchRequestError}
   *     Raised if the request id does not match any request in the blocked
   *     requests map.
   */
  async provideResponse(options = {}) {
    const {
      body = null,
      cookies = null,
      headers = null,
      reasonPhrase = null,
      request: requestId,
      statusCode = null,
    } = options;

    lazy.assert.string(
      requestId,
      `Expected "request" to be a string, got ${requestId}`
    );

    if (body !== null) {
      this.#assertBytesValue(
        body,
        `Expected "body" to be a network.BytesValue, got ${body}`
      );
    }

    if (cookies !== null) {
      lazy.assert.array(
        cookies,
        `Expected "cookies" to be an array got ${cookies}`
      );

      for (const cookie of cookies) {
        this.#assertSetCookieHeader(cookie);
      }
    }

    let deserializedHeaders = [];
    if (headers !== null) {
      deserializedHeaders = this.#deserializeHeaders(headers);
    }

    if (reasonPhrase !== null) {
      lazy.assert.string(
        reasonPhrase,
        `Expected "reasonPhrase" to be a string, got ${reasonPhrase}`
      );
    }

    if (statusCode !== null) {
      lazy.assert.positiveInteger(
        statusCode,
        `Expected "statusCode" to be a positive integer, got ${statusCode}`
      );
    }

    if (!this.#blockedRequests.has(requestId)) {
      throw new lazy.error.NoSuchRequestError(
        `Blocked request with id ${requestId} not found`
      );
    }

    const { authCallbacks, phase, request, resolveBlockedEvent } =
      this.#blockedRequests.get(requestId);

    // Handle optional arguments for the beforeRequestSent phase.
    // TODO: Support optional arguments in all phases, see Bug 1901055.
    if (phase === InterceptPhase.BeforeRequestSent) {
      // Create a new response.
      const replacedHttpResponse = Cc[
        "@mozilla.org/network/replaced-http-response;1"
      ].createInstance(Ci.nsIReplacedHttpResponse);

      if (statusCode !== null) {
        replacedHttpResponse.responseStatus = statusCode;
      }

      if (reasonPhrase !== null) {
        replacedHttpResponse.responseStatusText = reasonPhrase;
      }

      if (body !== null) {
        replacedHttpResponse.responseBody = deserializeBytesValue(body);
      }

      if (headers !== null) {
        for (const [name, value] of deserializedHeaders) {
          replacedHttpResponse.setResponseHeader(name, value, true);
        }
      }

      if (cookies !== null) {
        for (const cookie of cookies) {
          const headerValue = this.#serializeSetCookieHeader(cookie);
          replacedHttpResponse.setResponseHeader(
            "Set-Cookie",
            headerValue,
            true
          );
        }
      }

      request.setResponseOverride(replacedHttpResponse);
      request.wrappedChannel.resume();
    } else {
      if (body !== null) {
        throw new lazy.error.UnsupportedOperationError(
          `The "body" parameter is only supported for the beforeRequestSent phase at the moment`
        );
      }

      if (cookies !== null) {
        throw new lazy.error.UnsupportedOperationError(
          `The "cookies" parameter is only supported for the beforeRequestSent phase at the moment`
        );
      }

      if (headers !== null) {
        throw new lazy.error.UnsupportedOperationError(
          `The "headers" parameter is only supported for the beforeRequestSent phase at the moment`
        );
      }

      if (reasonPhrase !== null) {
        throw new lazy.error.UnsupportedOperationError(
          `The "reasonPhrase" parameter is only supported for the beforeRequestSent phase at the moment`
        );
      }

      if (statusCode !== null) {
        throw new lazy.error.UnsupportedOperationError(
          `The "statusCode" parameter is only supported for the beforeRequestSent phase at the moment`
        );
      }

      if (phase === InterceptPhase.AuthRequired) {
        // AuthRequired with no optional argument, resume the authentication.
        await authCallbacks.provideAuthCredentials();
      } else {
        // Any phase other than AuthRequired with no optional argument, resume the
        // request.
        request.wrappedChannel.resume();
      }
    }

    resolveBlockedEvent();
  }

  /**
   * Removes a data collector.
   *
   * @param {object=} options
   * @param {string} options.collector
   *     The id of the collector to remove.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchNetworkCollectorError}
   *     Raised if the collector id could not be found in the internal collectors
   *     map.
   */
  async removeDataCollector(options = {}) {
    const { collector } = options;

    lazy.assert.string(
      collector,
      lazy.pprint`Expected "collector" to be a string, got ${collector}`
    );

    if (!this.#networkCollectors.has(collector)) {
      throw new lazy.error.NoSuchNetworkCollectorError(
        `Network data collector with id ${collector} not found`
      );
    }

    const collectorData = this.#networkCollectors.get(collector);
    this.#networkCollectors.delete(collector);

    for (const [, collectedData] of this.#collectedNetworkData) {
      this.#removeCollectorFromData(collectedData, collector);
    }

    await this.#updateCollectorSessionData(
      collector,
      collectorData,
      lazy.SessionDataMethod.Remove
    );
  }

  /**
   * Removes an existing network intercept.
   *
   * @param {object=} options
   * @param {string} options.intercept
   *     The id of the intercept to remove.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchInterceptError}
   *     Raised if the intercept id could not be found in the internal intercept
   *     map.
   */
  removeIntercept(options = {}) {
    const { intercept } = options;

    lazy.assert.string(
      intercept,
      `Expected "intercept" to be a string, got ${intercept}`
    );

    if (!this.#interceptMap.has(intercept)) {
      throw new lazy.error.NoSuchInterceptError(
        `Network intercept with id ${intercept} not found`
      );
    }

    this.#interceptMap.delete(intercept);
  }

  /**
   * Configures the network cache behavior for certain requests.
   *
   * @param {object=} options
   * @param {CacheBehavior} options.cacheBehavior
   *     An enum value to set the network cache behavior.
   * @param {Array<string>=} options.contexts
   *     The list of browsing context ids where the network cache
   *     behavior should be updated.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  setCacheBehavior(options = {}) {
    const { cacheBehavior: behavior, contexts: contextIds = null } = options;

    if (!Object.values(lazy.CacheBehavior).includes(behavior)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "cacheBehavior" to be one of ${Object.values(
          lazy.CacheBehavior
        )}` + lazy.pprint` got ${behavior}`
      );
    }

    if (contextIds === null) {
      // Set the default behavior if no specific context is specified.
      lazy.updateCacheBehavior(behavior);
      return;
    }

    lazy.assert.isNonEmptyArray(
      contextIds,
      lazy.pprint`Expected "contexts" to be a non-empty array, got ${contextIds}`
    );

    const contexts = new Set();
    for (const contextId of contextIds) {
      lazy.assert.string(
        contextId,
        lazy.pprint`Expected elements of "contexts" to be a string, got ${contextId}`
      );
      const context = this._getNavigable(contextId);

      lazy.assert.topLevel(
        context,
        lazy.pprint`Browsing context with id ${contextId} is not top-level`
      );

      contexts.add(context);
    }

    lazy.updateCacheBehavior(behavior, contexts);
  }

  /**
   * Allows to specify headers that will extend, or overwrite, existing request
   * headers.
   *
   * @param {object=} options
   * @param {Array<Header>} options.headers
   *     Array of header values to replace the headers of the response.
   * @param {Array<string>=} options.contexts
   *     Optional list of browsing context ids.
   * @param {Array<string>=} options.userContexts
   *     Optional list of user context ids.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  setExtraHeaders(options = {}) {
    const {
      headers,
      contexts: contextIds = NULL,
      userContexts: userContextIds = NULL,
    } = options;

    lazy.assert.array(
      headers,
      lazy.pprint`Expected "headers" to be an array, got ${headers}`
    );

    const deserializedHeaders = this.#deserializeHeaders(headers);

    if (contextIds !== NULL && userContextIds !== NULL) {
      throw new lazy.error.InvalidArgumentError(
        `Providing both "contexts" and "userContexts" arguments is not supported`
      );
    }

    const navigables = new Set();
    const userContexts = new Set();
    if (userContextIds !== NULL) {
      lazy.assert.isNonEmptyArray(
        userContextIds,
        lazy.pprint`Expected "userContexts" to be a non-empty array, got ${userContextIds}`
      );

      for (const userContextId of userContextIds) {
        lazy.assert.string(
          userContextId,
          lazy.pprint`Expected elements of "userContexts" to be a string, got ${userContextId}`
        );

        this._assertUserContextId(userContextId);

        userContexts.add(userContextId);
      }
    }

    if (contextIds !== NULL) {
      lazy.assert.isNonEmptyArray(
        contextIds,
        lazy.pprint`Expected "contexts" to be a non-empty array, got ${contextIds}`
      );

      for (const contextId of contextIds) {
        lazy.assert.string(
          contextId,
          lazy.pprint`Expected elements of "contexts" to be a string, got ${contextId}`
        );
        const context = this._getNavigable(contextId);

        lazy.assert.topLevel(
          context,
          lazy.pprint`Browsing context with id ${contextId} is not top-level`
        );

        navigables.add(contextId);
      }
    }

    if (userContextIds !== NULL) {
      for (const userContextId of userContexts) {
        this.#extraHeaders.userContextHeaders.set(
          userContextId,
          deserializedHeaders
        );
      }
    } else if (contextIds !== NULL) {
      for (const contextId of navigables) {
        const context = this._getNavigable(contextId);
        this.#extraHeaders.navigableHeaders.set(
          context.webProgress,
          deserializedHeaders
        );
      }
    } else {
      this.#extraHeaders.defaultHeaders = deserializedHeaders;
    }

    if (!this.#hasExtraHeaders && headers.length) {
      this.#hasExtraHeaders = true;
      this.#networkListener.startListening();
    }
  }

  /**
   * Add a new request in the blockedRequests map.
   *
   * @param {string} requestId
   *     The request id.
   * @param {InterceptPhase} phase
   *     The phase where the request is blocked.
   * @param {object=} options
   * @param {object=} options.authCallbacks
   *     Only defined for requests blocked in the authRequired phase.
   *     Provides callbacks to handle the authentication.
   * @param {nsIChannel=} options.requestChannel
   *     The request channel.
   * @param {nsIChannel=} options.responseChannel
   *     The response channel.
   */
  #addBlockedRequest(requestId, phase, options = {}) {
    const { authCallbacks, request, response } = options;
    const { promise: blockedEventPromise, resolve: resolveBlockedEvent } =
      Promise.withResolvers();

    this.#blockedRequests.set(requestId, {
      authCallbacks,
      request,
      response,
      resolveBlockedEvent,
      phase,
    });

    blockedEventPromise.finally(() => {
      this.#blockedRequests.delete(requestId);
    });
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#allocate-size-to-record-data
   *
   * @param {number} size
   *     The size to allocate in bytes.
   */
  #allocateSizeToRecordData(size) {
    let availableSize = lazy.maxTotalDataSize;
    const alreadyCollectedData = [];
    for (const [, collectedData] of this.#collectedNetworkData) {
      if (collectedData.bytes !== null) {
        availableSize = availableSize - collectedData.size;
        alreadyCollectedData.push(collectedData);
      }
    }

    if (size > availableSize) {
      for (const collectedData of alreadyCollectedData) {
        availableSize = availableSize + collectedData.size;
        collectedData.bytes = null;
        collectedData.unavailableReason = UNAVAILABLE_DATA_ERROR_REASON.Evicted;
        collectedData.size = null;

        if (size <= availableSize) {
          return;
        }
      }
    }
  }

  #assertAuthCredentials(credentials) {
    lazy.assert.object(
      credentials,
      `Expected "credentials" to be an object, got ${credentials}`
    );

    if (credentials.type !== "password") {
      throw new lazy.error.InvalidArgumentError(
        `Expected credentials "type" to be "password" got ${credentials.type}`
      );
    }

    lazy.assert.string(
      credentials.username,
      `Expected credentials "username" to be a string, got ${credentials.username}`
    );
    lazy.assert.string(
      credentials.password,
      `Expected credentials "password" to be a string, got ${credentials.password}`
    );
  }

  #assertBytesValue(obj, msg) {
    lazy.assert.object(obj, msg);
    lazy.assert.string(obj.value, msg);
    lazy.assert.in(obj.type, Object.values(BytesValueType), msg);
  }

  #assertHeader(value, msg) {
    lazy.assert.object(value, msg);
    lazy.assert.string(value.name, msg);
    this.#assertBytesValue(value.value, msg);
  }

  #assertSetCookieHeader(setCookieHeader) {
    lazy.assert.object(
      setCookieHeader,
      `Expected set-cookie header to be an object, got ${setCookieHeader}`
    );

    const {
      name,
      value,
      domain = null,
      httpOnly = null,
      expiry = null,
      maxAge = null,
      path = null,
      sameSite = null,
      secure = null,
    } = setCookieHeader;

    lazy.assert.string(
      name,
      `Expected set-cookie header "name" to be a string, got ${name}`
    );

    this.#assertBytesValue(
      value,
      `Expected set-cookie header "value" to be a BytesValue, got ${name}`
    );

    if (domain !== null) {
      lazy.assert.string(
        domain,
        `Expected set-cookie header "domain" to be a string, got ${domain}`
      );
    }
    if (httpOnly !== null) {
      lazy.assert.boolean(
        httpOnly,
        `Expected set-cookie header "httpOnly" to be a boolean, got ${httpOnly}`
      );
    }
    if (expiry !== null) {
      lazy.assert.string(
        expiry,
        `Expected set-cookie header "expiry" to be a string, got ${expiry}`
      );
    }
    if (maxAge !== null) {
      lazy.assert.integer(
        maxAge,
        `Expected set-cookie header "maxAge" to be an integer, got ${maxAge}`
      );
    }
    if (path !== null) {
      lazy.assert.string(
        path,
        `Expected set-cookie header "path" to be a string, got ${path}`
      );
    }
    if (sameSite !== null) {
      lazy.assert.in(
        sameSite,
        Object.values(SameSite),
        `Expected set-cookie header "sameSite" to be one of ${Object.values(
          SameSite
        )}, got ${sameSite}`
      );
    }
    if (secure !== null) {
      lazy.assert.boolean(
        secure,
        `Expected set-cookie header "secure" to be a boolean, got ${secure}`
      );
    }
  }

  #cloneNetworkRequestBody(request) {
    if (!this.#networkCollectors.size) {
      return;
    }

    // If request body is missing or null, do not store any collected data.
    if (!request.postData || request.postData === null) {
      return;
    }

    const key = `${request.requestId}-${DataType.Request}`;
    if (this.#collectedNetworkData.has(key)) {
      // For redirected requests we might already be tracking the body.
      return;
    }

    const collectedData = {
      bytes: null,
      collectors: new Set(),
      pending: true,
      // This allows to implement the await/resume on "network data collected"
      // described in the specification.
      networkDataCollected: Promise.withResolvers(),
      request: request.requestId,
      size: null,
      type: DataType.Request,
    };

    // The actual cloning is already handled by the DevTools
    // NetworkResponseListener, here we just have to prepare the networkData and
    // add it to the array.
    this.#collectedNetworkData.set(key, collectedData);
  }

  #cloneNetworkResponseBody(request) {
    if (!this.#networkCollectors.size) {
      return;
    }

    const key = `${request.requestId}-${DataType.Response}`;
    if (this.#collectedNetworkData.has(key)) {
      // For redirected requests we might already be tracking the body.
      return;
    }

    const collectedData = {
      bytes: null,
      // The cloned body is fully handled by DevTools' NetworkResponseListener
      // so it will not explicitly be stored here.
      collectors: new Set(),
      pending: true,
      // This allows to implement the await/resume on "network data collected"
      // described in the specification.
      networkDataCollected: Promise.withResolvers(),
      request: request.requestId,
      size: null,
      type: DataType.Response,
      // Internal string used in the UnavailableNetworkData error message.
      unavailableReason: null,
    };

    // The actual cloning is already handled by the DevTools
    // NetworkResponseListener, here we just have to prepare the networkData and
    // add it to the array.
    this.#collectedNetworkData.set(key, collectedData);
  }

  #deserializeHeader(protocolHeader) {
    const name = protocolHeader.name;
    const value = deserializeBytesValue(protocolHeader.value);
    return [name, value];
  }

  #deserializeHeaders(headers) {
    const deserializedHeaders = [];
    lazy.assert.array(
      headers,
      lazy.pprint`Expected "headers" to be an array got ${headers}`
    );

    for (const header of headers) {
      this.#assertHeader(
        header,
        lazy.pprint`Expected values in "headers" to be network.Header, got ${header}`
      );

      // Deserialize headers immediately to validate the value
      const deserializedHeader = this.#deserializeHeader(header);
      lazy.assert.that(
        value => this.#isValidHttpToken(value),
        lazy.pprint`Expected "header" name to be a valid HTTP token, got ${deserializedHeader[0]}`
      )(deserializedHeader[0]);
      lazy.assert.that(
        value => this.#isValidHeaderValue(value),
        lazy.pprint`Expected "header" value to be a valid header value, got ${deserializedHeader[1]}`
      )(deserializedHeader[1]);

      deserializedHeaders.push(deserializedHeader);
    }

    return deserializedHeaders;
  }

  #extractChallenges(response) {
    let headerName;

    // Using case-insensitive match for header names, so we use the lowercase
    // version of the "WWW-Authenticate" / "Proxy-Authenticate" strings.
    if (response.status === 401) {
      headerName = "www-authenticate";
    } else if (response.status === 407) {
      headerName = "proxy-authenticate";
    } else {
      return null;
    }

    const challenges = [];
    for (const [name, value] of response.headers) {
      if (name.toLowerCase() === headerName) {
        // A single header can contain several challenges.
        const headerChallenges = lazy.parseChallengeHeader(value);
        for (const headerChallenge of headerChallenges) {
          const realmParam = headerChallenge.params.find(
            param => param.name == "realm"
          );
          const realm = realmParam ? realmParam.value : undefined;
          const challenge = {
            scheme: headerChallenge.scheme,
            realm,
          };
          challenges.push(challenge);
        }
      }
    }

    return challenges;
  }

  #getCollectedData(requestId, dataType) {
    return this.#collectedNetworkData.get(`${requestId}-${dataType}`) || null;
  }

  #getNetworkIntercepts(event, request, topContextId) {
    if (!request.supportsInterception) {
      // For requests which do not support interception (such as data URIs or
      // cached resources), do not attempt to match intercepts.
      return [];
    }

    const intercepts = [];

    let phase;
    switch (event) {
      case "network.beforeRequestSent":
        phase = InterceptPhase.BeforeRequestSent;
        break;
      case "network.responseStarted":
        phase = InterceptPhase.ResponseStarted;
        break;
      case "network.authRequired":
        phase = InterceptPhase.AuthRequired;
        break;
      case "network.responseCompleted":
        // The network.responseCompleted event does not match any interception
        // phase. Return immediately.
        return intercepts;
    }

    const url = request.serializedURL;
    for (const [interceptId, intercept] of this.#interceptMap) {
      if (
        intercept.contexts !== null &&
        !intercept.contexts.includes(topContextId)
      ) {
        // Skip this intercept if the event's context does not match the list
        // of contexts for this intercept.
        continue;
      }

      if (intercept.phases.includes(phase)) {
        const urlPatterns = intercept.urlPatterns;
        if (
          !urlPatterns.length ||
          urlPatterns.some(pattern => lazy.matchURLPattern(pattern, url))
        ) {
          intercepts.push(interceptId);
        }
      }
    }

    return intercepts;
  }

  #getCookiesForRequest(request) {
    if (!request.host) {
      return [];
    }

    const storeCookies = Services.cookies.getCookiesWithOriginAttributes(
      request.originAttributesString,
      request.host
    );

    const headerCookieNames = new Set();
    for (const [name, value] of request.headers) {
      if (name.toLowerCase() === "cookie") {
        for (const part of value.split(";")) {
          const eq = part.indexOf("=");
          if (eq !== -1) {
            headerCookieNames.add(unescape(part.substr(0, eq).trim()));
          }
        }
      }
    }

    const cookies = [];
    for (const cookie of storeCookies) {
      if (headerCookieNames.has(cookie.name)) {
        cookies.push(this.#serializeNetworkCookie(cookie));
      }
    }

    return cookies;
  }

  #getRequestData(request) {
    const requestId = request.requestId;

    // "Let url be the result of running the URL serializer with request’s URL"
    // request.serializedURL is already serialized.
    const url = request.serializedURL;
    const method = request.method;

    const bodySize = request.postDataSize;
    const headersSize = request.headersSize;
    const headers = [];

    for (const [name, value] of request.headers) {
      headers.push(this.#serializeHeader(name, value));
    }

    const cookies = this.#getCookiesForRequest(request);

    const destination = request.destination;
    const initiatorType = request.initiatorType;
    const timings = request.timings;

    return {
      request: requestId,
      url,
      method,
      bodySize,
      headersSize,
      headers,
      cookies,
      destination,
      initiatorType,
      timings,
    };
  }

  #getResponseContentInfo(response) {
    return {
      size: response.decodedBodySize,
    };
  }

  #getResponseData(response) {
    const url = response.serializedURL;
    const protocol = response.protocol;
    const status = response.status;
    const statusText = response.statusMessage;
    // TODO: Ideally we should have a `isCacheStateLocal` getter
    // const fromCache = response.isCacheStateLocal();
    const fromCache = response.fromCache;
    const mimeType = response.mimeType;
    const headers = [];
    for (const [name, value] of response.headers) {
      headers.push(this.#serializeHeader(name, value));
    }

    const bytesReceived = response.totalTransmittedSize;
    const headersSize = response.headersTransmittedSize;
    const bodySize = response.encodedBodySize;
    const content = this.#getResponseContentInfo(response);
    const authChallenges = this.#extractChallenges(response);

    const params = {
      url,
      protocol,
      status,
      statusText,
      fromCache,
      headers,
      mimeType,
      bytesReceived,
      headersSize,
      bodySize,
      content,
    };

    if (authChallenges !== null) {
      params.authChallenges = authChallenges;
    }

    return params;
  }

  #getSuspendMarkerText(requestData, phase) {
    return `Request (id: ${requestData.request}) suspended by WebDriver BiDi in ${phase} phase`;
  }

  #isValidHeaderValue(value) {
    if (!value.length) {
      return true;
    }

    // For non-empty strings check against:
    // - leading or trailing tabs & spaces
    // - new lines and null bytes
    const chars = value.split("");
    const tabOrSpace = [" ", "\t"];
    const forbiddenChars = ["\r", "\n", "\0"];
    return (
      !tabOrSpace.includes(chars.at(0)) &&
      !tabOrSpace.includes(chars.at(-1)) &&
      forbiddenChars.every(c => !chars.includes(c))
    );
  }

  /**
   * This helper is adapted from a C++ validation helper in nsHttp.cpp.
   *
   * @see https://searchfox.org/mozilla-central/rev/445a6e86233c733c5557ef44e1d33444adaddefc/netwerk/protocol/http/nsHttp.cpp#169
   */
  #isValidHttpToken(token) {
    // prettier-ignore
    // This array corresponds to all char codes between 0 and 127, which is the
    // range of supported char codes for HTTP tokens. Within this range,
    // accepted char codes are marked with a 1, forbidden char codes with a 0.
    const validTokenMap = [
      0, 0, 0, 0, 0, 0, 0, 0,  //   0
      0, 0, 0, 0, 0, 0, 0, 0,  //   8
      0, 0, 0, 0, 0, 0, 0, 0,  //  16
      0, 0, 0, 0, 0, 0, 0, 0,  //  24

      0, 1, 0, 1, 1, 1, 1, 1,  //  32
      0, 0, 1, 1, 0, 1, 1, 0,  //  40
      1, 1, 1, 1, 1, 1, 1, 1,  //  48
      1, 1, 0, 0, 0, 0, 0, 0,  //  56

      0, 1, 1, 1, 1, 1, 1, 1,  //  64
      1, 1, 1, 1, 1, 1, 1, 1,  //  72
      1, 1, 1, 1, 1, 1, 1, 1,  //  80
      1, 1, 1, 0, 0, 0, 1, 1,  //  88

      1, 1, 1, 1, 1, 1, 1, 1,  //  96
      1, 1, 1, 1, 1, 1, 1, 1,  // 104
      1, 1, 1, 1, 1, 1, 1, 1,  // 112
      1, 1, 1, 0, 1, 0, 1, 0   // 120
    ];

    if (!token.length) {
      return false;
    }
    return token
      .split("")
      .map(s => s.charCodeAt(0))
      .every(c => validTokenMap[c]);
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#match-collector-for-navigable
   *
   * @param {Collector} collector
   *     The collector to match
   * @param {BrowsingContext} navigable
   *     The navigable (BrowsingContext) to match
   * @returns {boolean}
   *     True if the collector corresponds to the provided navigable. False
   *     otherwise.
   */
  #matchCollectorForNavigable(collector, navigable) {
    if (collector.contexts.size) {
      const navigableId =
        lazy.NavigableManager.getIdForBrowsingContext(navigable);
      return collector.contexts.has(navigableId);
    }

    if (collector.userContexts.size) {
      const userContext =
        lazy.UserContextManager.getIdByBrowsingContext(navigable);
      return collector.userContexts.has(userContext);
    }

    // Return true.
    return true;
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#maybe-abort-network-response-body-collection
   *
   * @param {NetworkRequest} request
   *     The request object for which we want to abort the body collection.
   */
  #maybeAbortNetworkResponseBodyCollection(request) {
    const collectedData = this.#getCollectedData(
      request.requestId,
      DataType.Response
    );
    if (collectedData === null) {
      return;
    }

    lazy.logger.trace(
      `Network data not collected for request "${request.requestId}" and data type "${DataType.Response}"` +
        `: fetch error`
    );
    collectedData.pending = false;
    collectedData.unavailableReason = UNAVAILABLE_DATA_ERROR_REASON.Aborted;
    collectedData.networkDataCollected.resolve();
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#maybe-collect-network-request-body
   *
   * @param {NetworkRequest} request
   *     The request object for which we want to collect the body.
   */
  async #maybeCollectNetworkRequestBody(request) {
    const collectedData = this.#getCollectedData(
      request.requestId,
      DataType.Request
    );

    if (collectedData === null) {
      return;
    }

    this.#maybeCollectNetworkData({
      collectedData,
      dataType: DataType.Request,
      request,
      readAndProcessBodyFn: request.readAndProcessRequestBody,
      size: request.postDataSize,
    });
  }

  /**
   * Implements https://www.w3.org/TR/webdriver-bidi/#maybe-collect-network-response-body
   *
   * @param {NetworkRequest} request
   *     The request object for which we want to collect the body.
   * @param {NetworkResponse} response
   *     The response object for which we want to collect the body.
   */
  async #maybeCollectNetworkResponseBody(request, response) {
    if (response.willRedirect) {
      return;
    }

    const collectedData = this.#getCollectedData(
      request.requestId,
      DataType.Response
    );

    if (collectedData === null) {
      return;
    }

    if (
      !(response instanceof lazy.NetworkResponse) &&
      !response.isDataURL &&
      !response.hasCachedResponseBody
    ) {
      lazy.logger.trace(
        `Network data not collected for request "${request.requestId}" and data type "${DataType.Response}"` +
          `: unsupported response (read from memory cache)`
      );
      // Cached stencils do not return any response body.
      collectedData.pending = false;
      collectedData.networkDataCollected.resolve();
      this.#collectedNetworkData.delete(
        `${collectedData.request}-${collectedData.type}`
      );
      return;
    }

    let readAndProcessBodyFn, size;
    if (response.isDataURL) {
      // Handle data URLs as a special case since the response is not provided
      // by the DevTools ResponseListener in this case.
      const url = request.serializedURL;
      const body = url.substring(url.indexOf(",") + 1);
      const isText =
        response.mimeType &&
        lazy.NetworkHelper.isTextMimeType(response.mimeType);

      readAndProcessBodyFn = () =>
        new lazy.NetworkDataBytes({
          getBytesValue: () => body,
          isBase64: !isText,
        });
      size = body.length;
    } else if (response.hasCachedResponseBody) {
      readAndProcessBodyFn = () =>
        new lazy.NetworkDataBytes({
          getBytesValue: () => response.cachedResponseBody,
          isBase64: false,
        });
      size = response.cachedResponseBody.length;
    } else {
      readAndProcessBodyFn = response.readAndProcessResponseBody;
      size = response.encodedBodySize;
    }

    this.#maybeCollectNetworkData({
      collectedData,
      dataType: DataType.Response,
      request,
      readAndProcessBodyFn,
      size,
    });
  }

  /**
   * Implements https://www.w3.org/TR/webdriver-bidi/#maybe-collect-network-data
   *
   * @param {object} options
   * @param {Data} options.collectedData
   * @param {DataType} options.dataType
   * @param {NetworkRequest} options.request
   * @param {Function} options.readAndProcessBodyFn
   * @param {number} options.size
   */
  async #maybeCollectNetworkData(options) {
    const {
      collectedData,
      dataType,
      request,
      // Note: this parameter is not present in
      // https://www.w3.org/TR/webdriver-bidi/#maybe-collect-network-data
      // Each caller is responsible for providing a callable which will return
      // a NetworkDataBytes instance corresponding to the collected data.
      readAndProcessBodyFn,
      // Note: the spec assumes that in some cases the size can be computed
      // dynamically. But in practice we might be storing encoding data in a
      // format which makes it hard to get the size. So here we always expect
      // callers to provide a size.
      size,
    } = options;

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      lazy.logger.trace(
        `Network data not collected for request "${request.requestId}" and data type "${dataType}"` +
          `: navigable no longer available`
      );
      collectedData.pending = false;
      this.#collectedNetworkData.delete(
        `${collectedData.request}-${collectedData.type}`
      );
      collectedData.networkDataCollected.resolve();
      return;
    }

    const topNavigable = browsingContext.top;
    let collectors = [];
    for (const [, collector] of this.#networkCollectors) {
      if (
        collector.dataTypes.includes(dataType) &&
        this.#matchCollectorForNavigable(collector, topNavigable)
      ) {
        collectors.push(collector);
      }
    }

    if (!collectors.length) {
      lazy.logger.trace(
        `Network data not collected for request "${request.requestId}" and data type "${dataType}"` +
          `: no matching collector`
      );
      collectedData.pending = false;
      this.#collectedNetworkData.delete(
        `${collectedData.request}-${collectedData.type}`
      );
      collectedData.networkDataCollected.resolve();
      return;
    }

    let bytes = null;

    // At this point, the specification expects to processBody for the cloned
    // body. Here we do not explicitly clone the bodies.
    // For responses, DevTools' NetworkResponseListener clones the stream.
    // For requests, NetworkHelper.readPostTextFromRequest clones the stream on
    // the fly to read it as text.
    try {
      const bytesOrNull = await readAndProcessBodyFn();
      if (bytesOrNull !== null) {
        bytes = bytesOrNull;
      }
    } catch {
      // Let processBodyError be this step: Do nothing.
    }

    // If the network module was destroyed while waiting to read the response
    // body, the session has been destroyed. Resolve the promise and bail out.
    if (!this.#collectedNetworkData) {
      collectedData.networkDataCollected.resolve();
      return;
    }

    if (bytes !== null) {
      for (const collector of collectors) {
        if (size <= collector.maxEncodedDataSize) {
          collectedData.collectors.add(collector.collector);
        }
      }

      if (collectedData.collectors.size) {
        this.#allocateSizeToRecordData(size);
        collectedData.bytes = bytes;
        collectedData.size = size;
      }
    }

    // Note: specification flips `collectedData.pending` to false earlier, but
    // the implementation is async with `await response.readResponseBody()`.
    // `collectedData.pending` is only flipped before returning here - and in
    // early returns above.
    collectedData.pending = false;
    if (!collectedData.collectors.size) {
      this.#collectedNetworkData.delete(
        `${collectedData.request}-${collectedData.type}`
      );
    }
    collectedData.networkDataCollected.resolve();
  }

  #onAuthRequired = (name, data) => {
    const { authCallbacks, request, response } = data;

    let isBlocked = false;
    try {
      const browsingContext = lazy.NavigableManager.getBrowsingContextById(
        request.contextId
      );
      if (!browsingContext) {
        // Do not emit events if the context id does not match any existing
        // browsing context.
        return;
      }

      const protocolEventName = "network.authRequired";

      const isListening = this._hasListener(protocolEventName, {
        contextId: browsingContext.id,
      });
      if (!isListening) {
        // If there are no listeners subscribed to this event and this context,
        // bail out.
        return;
      }

      const baseParameters = this.#processNetworkEvent(
        protocolEventName,
        request
      );

      const responseData = this.#getResponseData(response);
      const authRequiredEvent = {
        ...baseParameters,
        response: responseData,
      };

      this._emitEventForBrowsingContext(
        browsingContext.id,
        protocolEventName,
        authRequiredEvent
      );

      if (authRequiredEvent.isBlocked) {
        isBlocked = true;

        // requestChannel.suspend() is not needed here because the request is
        // already blocked on the authentication prompt notification until
        // one of the authCallbacks is called.
        this.#addBlockedRequest(
          authRequiredEvent.request.request,
          InterceptPhase.AuthRequired,
          {
            authCallbacks,
            request,
            response,
          }
        );
      }
    } finally {
      if (!isBlocked) {
        // If the request was not blocked, forward the auth prompt notification
        // to the next consumer.
        authCallbacks.forwardAuthPrompt();
      }
    }
  };

  #onBeforeRequestSent = (name, data) => {
    const { request } = data;

    if (this.#redirectedRequests.has(request.requestId)) {
      // If this beforeRequestSent event corresponds to a request that has
      // just been redirected using continueRequest, skip the event and remove
      // it from the redirectedRequests set.
      this.#redirectedRequests.delete(request.requestId);
      return;
    }

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      // Do not emit events if the context id does not match any existing
      // browsing context.
      return;
    }

    // Make sure a collected data is created for the request.
    // Note: this is supposed to be triggered from fetch and doesn't depend on
    // whether network events are used or not.
    this.#cloneNetworkRequestBody(request);
    this.#maybeCollectNetworkRequestBody(request);

    const relatedNavigables = [browsingContext];
    this.#updateRequestHeaders(request, relatedNavigables);

    const protocolEventName = "network.beforeRequestSent";

    const isListening = this._hasListener(protocolEventName, {
      contextId: browsingContext.id,
    });

    if (isListening) {
      const baseParameters = this.#processNetworkEvent(
        protocolEventName,
        request
      );

      // Bug 1805479: Handle the initiator, including stacktrace details.
      const initiator = {
        type: InitiatorType.Other,
      };

      const beforeRequestSentEvent = {
        ...baseParameters,
        initiator,
      };

      this._emitEventForBrowsingContext(
        browsingContext.id,
        protocolEventName,
        beforeRequestSentEvent
      );
      if (beforeRequestSentEvent.isBlocked) {
        request.wrappedChannel.suspend(
          this.#getSuspendMarkerText(request, "beforeRequestSent")
        );

        this.#addBlockedRequest(
          beforeRequestSentEvent.request.request,
          InterceptPhase.BeforeRequestSent,
          {
            request,
          }
        );
      }
    }

    // If network conditions are set to "offline", most requests should be
    // prevented, but some are still sent (e.g. keep-alive).
    // Per https://w3c.github.io/webdriver-bidi/#webdriver-bidi-before-request-sent
    // this should be handled after emitting the beforeRequestSent event.
    if (browsingContext.top?.forceOffline) {
      request.wrappedChannel.cancel(
        Cr.NS_ERROR_OFFLINE,
        Ci.nsILoadInfo.BLOCKING_REASON_WEBDRIVER_BIDI
      );
    }
  };

  #onFetchError = (name, data) => {
    const { request } = data;

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      // Do not emit events if the context id does not match any existing
      // browsing context.
      return;
    }

    this.#maybeAbortNetworkResponseBodyCollection(request);

    const protocolEventName = "network.fetchError";

    const isListening = this._hasListener(protocolEventName, {
      contextId: browsingContext.id,
    });
    if (!isListening) {
      // If there are no listeners subscribed to this event and this context,
      // bail out.
      return;
    }

    const baseParameters = this.#processNetworkEvent(
      protocolEventName,
      request
    );

    const fetchErrorEvent = {
      ...baseParameters,
      errorText: request.errorText,
    };

    this._emitEventForBrowsingContext(
      browsingContext.id,
      protocolEventName,
      fetchErrorEvent
    );
  };

  #onResponseEvent = async (name, data) => {
    const { request, response } = data;

    const browsingContext = lazy.NavigableManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      // Do not emit events if the context id does not match any existing
      // browsing context.
      return;
    }

    const protocolEventName =
      name === "response-started"
        ? "network.responseStarted"
        : "network.responseCompleted";

    if (protocolEventName === "network.responseStarted") {
      this.#cloneNetworkResponseBody(request);
    } else {
      this.#maybeCollectNetworkResponseBody(request, response);
    }

    const isListening = this._hasListener(protocolEventName, {
      contextId: browsingContext.id,
    });
    if (!isListening) {
      // If there are no listeners subscribed to this event and this context,
      // bail out.
      return;
    }

    const baseParameters = this.#processNetworkEvent(
      protocolEventName,
      request
    );

    const responseData = this.#getResponseData(response);

    const responseEvent = {
      ...baseParameters,
      response: responseData,
    };

    this._emitEventForBrowsingContext(
      browsingContext.id,
      protocolEventName,
      responseEvent
    );

    if (
      protocolEventName === "network.responseStarted" &&
      responseEvent.isBlocked &&
      request.supportsInterception
    ) {
      request.wrappedChannel.suspend(
        this.#getSuspendMarkerText(request, "responseStarted")
      );

      this.#addBlockedRequest(
        responseEvent.request.request,
        InterceptPhase.ResponseStarted,
        {
          request,
          response,
        }
      );
    }
  };

  #onUserContextDeleted = (name, data) => {
    const userContextId = data.userContextId;
    if (this.#extraHeaders.userContextHeaders.has(userContextId)) {
      this.#extraHeaders.userContextHeaders.delete(userContextId);
    }
  };

  #processNetworkEvent(event, request) {
    const requestData = this.#getRequestData(request);
    const navigation = request.navigationId;
    let contextId = null;
    let topContextId = null;
    let userContextId = null;
    if (request.contextId) {
      // Retrieve the top browsing context id for this network event.
      contextId = request.contextId;
      const browsingContext =
        lazy.NavigableManager.getBrowsingContextById(contextId);
      topContextId = lazy.NavigableManager.getIdForBrowsingContext(
        browsingContext.top
      );
      userContextId =
        lazy.UserContextManager.getIdByBrowsingContext(browsingContext);
    }

    const intercepts = this.#getNetworkIntercepts(event, request, topContextId);
    const redirectCount = request.redirectCount;
    const timestamp = Date.now();
    const isBlocked = !!intercepts.length;
    const params = {
      context: contextId,
      isBlocked,
      navigation,
      redirectCount,
      request: requestData,
      timestamp,
      userContext: userContextId,
    };

    if (isBlocked) {
      params.intercepts = intercepts;
    }

    return params;
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#remove-collector-from-data
   *
   * @param {Data} collectedData
   *     The Data from which the collector should be removed.
   * @param {string} collectorId
   *     The collector id to remove.
   */
  #removeCollectorFromData(collectedData, collectorId) {
    if (collectedData.collectors.has(collectorId)) {
      collectedData.collectors.delete(collectorId);
      if (!collectedData.collectors.size) {
        this.#collectedNetworkData.delete(
          `${collectedData.request}-${collectedData.type}`
        );
      }
    }
  }

  #serializeCookieHeader(cookieHeader) {
    const name = cookieHeader.name;
    const value = deserializeBytesValue(cookieHeader.value);
    return `${name}=${value}`;
  }

  #serializeHeader(name, value) {
    return {
      name,
      value: serializeAsBytesValue(value),
    };
  }

  #serializeNetworkCookie(cookie) {
    const serialized = {
      domain: cookie.host,
      httpOnly: cookie.isHttpOnly,
      name: cookie.name,
      path: cookie.path,
      sameSite: NetworkCookieSameSiteType[cookie.sameSite],
      secure: cookie.isSecure,
      size: cookie.name.length + cookie.value.length,
      value: serializeAsBytesValue(cookie.value),
    };

    if (!cookie.isSession) {
      // expiry is in milliseconds, the spec expects seconds.
      serialized.expiry = Math.round(cookie.expiry / 1000);
    }

    return serialized;
  }

  #serializeSetCookieHeader(setCookieHeader) {
    const {
      name,
      value,
      domain = null,
      httpOnly = null,
      expiry = null,
      maxAge = null,
      path = null,
      sameSite = null,
      secure = null,
    } = setCookieHeader;

    let headerValue = `${name}=${deserializeBytesValue(value)}`;

    if (expiry !== null) {
      headerValue += `;Expires=${expiry}`;
    }
    if (maxAge !== null) {
      headerValue += `;Max-Age=${maxAge}`;
    }
    if (domain !== null) {
      headerValue += `;Domain=${domain}`;
    }
    if (path !== null) {
      headerValue += `;Path=${path}`;
    }
    if (secure === true) {
      headerValue += `;Secure`;
    }
    if (httpOnly === true) {
      headerValue += `;HttpOnly`;
    }
    if (sameSite !== null) {
      headerValue += `;SameSite=${sameSite}`;
    }
    return headerValue;
  }

  #startListening(event) {
    if (!this.#subscribedEvents.size) {
      this.#networkListener.startListening();
    }

    this.#subscribedEvents.add(event);
  }

  #stopListening(event) {
    this.#subscribedEvents.delete(event);

    if (this.#hasNetworkConditionsOffline || this.#hasExtraHeaders) {
      // If networkConditions or extraHeaders are set, the listener should
      // remain enabled even if no public events are emitted.
      return;
    }

    if (!this.#subscribedEvents.size) {
      this.#networkListener.stopListening();
    }
  }

  #subscribeEvent(event) {
    if (this.constructor.supportedEvents.includes(event)) {
      this.#startListening(event);
    }
  }

  #unsubscribeEvent(event) {
    if (this.constructor.supportedEvents.includes(event)) {
      this.#stopListening(event);
    }
  }

  /**
   * Update SessionData when a response data collector is added or removed.
   *
   * @param {string} collectorId
   *     The id of the collector.
   * @param {Collector} collector
   *     The collector object.
   * @param {SessionDataMethod} method
   *     Whether to add or remove the item.
   */
  async #updateCollectorSessionData(collectorId, collector, method) {
    const sessionDataItems = [];

    if (!collector.dataTypes.includes(DataType.Response)) {
      // windowglobal modules only need to know whether responses are collected
      // in order to capture cached content held in the content process.
      return;
    }

    const sessionDataItem = {
      category: lazy.SessionDataCategory.ResponseCollector,
      method,
      moduleName: "network",
      values: [collectorId],
    };

    if (collector.contexts.size) {
      for (const contextId of collector.contexts) {
        sessionDataItems.push({
          ...sessionDataItem,
          contextDescriptor: {
            type: lazy.ContextDescriptorType.TopBrowsingContext,
            id: contextId,
          },
        });
      }
    } else if (collector.userContexts.size) {
      for (const userContextId of collector.userContexts) {
        sessionDataItems.push({
          ...sessionDataItem,
          contextDescriptor: {
            type: lazy.ContextDescriptorType.UserContext,
            id: userContextId,
          },
        });
      }
    } else {
      sessionDataItems.push({
        ...sessionDataItem,
        contextDescriptor: {
          type: lazy.ContextDescriptorType.All,
        },
      });
    }

    await this.messageHandler.updateSessionData(sessionDataItems);
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#update-headers
   */
  #updateHeaders(request, headers) {
    for (const [name, value] of headers) {
      // Use merge: false to always override the value
      request.setRequestHeader(name, value, { merge: false });
    }
  }

  /**
   * Implements https://w3c.github.io/webdriver-bidi/#update-request-headers
   */
  #updateRequestHeaders(request, navigables) {
    for (const browsingContext of navigables) {
      this.#updateHeaders(request, this.#extraHeaders.defaultHeaders);

      const userContextHeaders = this.#extraHeaders.userContextHeaders;
      const userContext =
        lazy.UserContextManager.getIdByBrowsingContext(browsingContext);
      if (userContextHeaders.has(userContext)) {
        this.#updateHeaders(request, userContextHeaders.get(userContext));
      }

      const navigableHeaders = this.#extraHeaders.navigableHeaders;
      const topNavigableWebProgress = browsingContext.top.webProgress;
      if (navigableHeaders.has(topNavigableWebProgress)) {
        this.#updateHeaders(
          request,
          navigableHeaders.get(topNavigableWebProgress)
        );
      }
    }
  }

  /**
   * Internal commands
   */

  _applySessionData(params) {
    // TODO: Bug 1775231. Move this logic to a shared module or an abstract
    // class.
    const { category } = params;
    if (category === "event") {
      const filteredSessionData = params.sessionData.filter(item =>
        this.messageHandler.matchesContext(item.contextDescriptor)
      );
      for (const event of this.#subscribedEvents.values()) {
        const hasSessionItem = filteredSessionData.some(
          item => item.value === event
        );
        // If there are no session items for this context, we should unsubscribe from the event.
        if (!hasSessionItem) {
          this.#unsubscribeEvent(event);
        }
      }

      // Subscribe to all events, which have an item in SessionData.
      for (const { value } of filteredSessionData) {
        this.#subscribeEvent(value);
      }
    }
  }

  _sendEventsForWindowGlobalNetworkResource(params) {
    this.#onBeforeRequestSent("before-request-sent", params);
    this.#onResponseEvent("response-started", params);
    this.#onResponseEvent("response-completed", params);
  }

  _setDecodedBodySize(params) {
    const { channelId, decodedBodySize } = params;
    this.#decodedBodySizeMap.setDecodedBodySize(channelId, decodedBodySize);
  }

  _startListeningForNetworkConditionsOffline() {
    if (!this.#hasNetworkConditionsOffline) {
      this.#hasNetworkConditionsOffline = true;
      this.#networkListener.startListening();
    }
  }

  static get supportedCommandsInRestrictedSession() {
    return ["addDataCollector", "getData", "removeDataCollector"];
  }

  static get supportedEvents() {
    return [
      "network.authRequired",
      "network.beforeRequestSent",
      "network.fetchError",
      "network.responseCompleted",
      "network.responseStarted",
    ];
  }
}

/**
 * Deserialize a network BytesValue.
 *
 * @see https://w3c.github.io/webdriver-bidi/#deserialize-protocol-bytes
 *
 * @param {BytesValue} protocolBytes
 *     The BytesValue to deserialize.
 * @returns {string}
 *     The deserialized value.
 */
export function deserializeBytesValue(protocolBytes) {
  const { type, value } = protocolBytes;

  let bytes;
  if (type === BytesValueType.String) {
    // If protocol bytes matches the network.StringValue production
    // Encode values as UTF-8
    bytes = encodeAsUTF8(value);
  } else {
    // Otherwise if protocol bytes matches the network.Base64Value production
    // Let bytes be forgiving-base64 decode protocol bytes["value"].
    bytes = atob(value);
  }

  return bytes;
}

/**
 * Encode the provided value as UTF-8, working around argument limits in JS.
 *
 * @param {string} value
 *     The value to encode.
 * @return {string}
 *     The UTF-8 encoded string.
 */
function encodeAsUTF8(value) {
  const CHUNK_SIZE = 65536;
  let result = "";

  const encoder = new TextEncoder();
  const utf8Bytes = encoder.encode(value);
  for (let i = 0; i < utf8Bytes.length; i += CHUNK_SIZE) {
    const chunk = utf8Bytes.slice(i, i + CHUNK_SIZE);
    result += String.fromCharCode.apply(null, chunk);
  }
  return result;
}

/**
 * Serialize a value as BytesValue.
 *
 * @see https://w3c.github.io/webdriver-bidi/#serialize-protocol-bytes
 *
 * @param {string} bytes
 *     The value to serialize.
 * @return {BytesValue}
 *     The serialized value.
 */
function serializeAsBytesValue(bytes) {
  let text, type;
  try {
    type = BytesValueType.String;
    // Let text be UTF-8 decode without BOM or fail bytes.
    const decoder = new TextDecoder("utf-8", {
      fatal: true,
      ignoreBOM: true,
    });
    text = decoder.decode(Uint8Array.from(bytes, c => c.charCodeAt(0)));
  } catch (e) {
    if (e instanceof TypeError) {
      // If text is failure, return a map matching the network.Base64Value production,
      type = BytesValueType.Base64;
      // Set value to forgiving-base64 encode bytes.
      text = btoa(bytes);
    } else {
      // Errors other than TypeError are unexpected and should bubble up.
      throw e;
    }
  }

  return {
    type,
    value: text,
  };
}

export const network = NetworkModule;
