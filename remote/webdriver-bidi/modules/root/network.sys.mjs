/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RootBiDiModule } from "chrome://remote/content/webdriver-bidi/modules/RootBiDiModule.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  CacheBehavior: "chrome://remote/content/shared/NetworkCacheManager.sys.mjs",
  NetworkDecodedBodySizeMap:
    "chrome://remote/content/shared/NetworkDecodedBodySizeMap.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  generateUUID: "chrome://remote/content/shared/UUID.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  matchURLPattern:
    "chrome://remote/content/shared/webdriver/URLPattern.sys.mjs",
  NetworkListener:
    "chrome://remote/content/shared/listeners/NetworkListener.sys.mjs",
  parseChallengeHeader:
    "chrome://remote/content/shared/ChallengeHeaderParser.sys.mjs",
  parseURLPattern:
    "chrome://remote/content/shared/webdriver/URLPattern.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
  updateCacheBehavior:
    "chrome://remote/content/shared/NetworkCacheManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.WEBDRIVER_BIDI)
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
 * Enum of possible sameSite values.
 *
 * @readonly
 * @enum {SameSite}
 */
const SameSite = {
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

class NetworkModule extends RootBiDiModule {
  #blockedRequests;
  #decodedBodySizeMap;
  #interceptMap;
  #networkListener;
  #redirectedRequests;
  #subscribedEvents;

  constructor(messageHandler) {
    super(messageHandler);

    // Map of request id to BlockedRequest
    this.#blockedRequests = new Map();

    // Map of intercept id to InterceptProperties
    this.#interceptMap = new Map();

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
  }

  destroy() {
    this.#networkListener.off("auth-required", this.#onAuthRequired);
    this.#networkListener.off("before-request-sent", this.#onBeforeRequestSent);
    this.#networkListener.off("fetch-error", this.#onFetchError);
    this.#networkListener.off("response-completed", this.#onResponseEvent);
    this.#networkListener.off("response-started", this.#onResponseEvent);
    this.#networkListener.destroy();

    this.#decodedBodySizeMap.destroy();

    this.#decodedBodySizeMap = null;
    this.#blockedRequests = null;
    this.#interceptMap = null;
    this.#subscribedEvents = null;
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
        const context = this.#getBrowsingContext(contextId);

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
   * @param {Array<SetCookieHeader>=} options.cookies [unsupported]
   *     Optional array of set-cookie header values to replace the set-cookie
   *     headers of the response.
   * @param {AuthCredentials=} options.credentials
   *     Optional AuthCredentials to use.
   * @param {Array<Header>=} options.headers [unsupported]
   *     Optional array of header values to replace the headers of the response.
   * @param {string=} options.reasonPhrase [unsupported]
   *     Optional string to replace the status message of the response.
   * @param {number=} options.statusCode [unsupported]
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
      const context = this.#getBrowsingContext(contextId);

      lazy.assert.topLevel(
        context,
        lazy.pprint`Browsing context with id ${contextId} is not top-level`
      );

      contexts.add(context);
    }

    lazy.updateCacheBehavior(behavior, contexts);
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

  #getBrowsingContext(contextId) {
    const context = lazy.TabManager.getBrowsingContextById(contextId);
    if (context === null) {
      throw new lazy.error.NoSuchFrameError(
        `Browsing Context with id ${contextId} not found`
      );
    }

    if (!context.currentWindowGlobal) {
      throw new lazy.error.NoSuchFrameError(
        `No window found for BrowsingContext with id ${contextId}`
      );
    }

    return context;
  }

  #getNetworkIntercepts(event, request, topContextId) {
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

  #getRequestData(request) {
    const requestId = request.requestId;

    // "Let url be the result of running the URL serializer with request’s URL"
    // request.serializedURL is already serialized.
    const url = request.serializedURL;
    const method = request.method;

    const bodySize = request.postDataSize;
    const headersSize = request.headersSize;
    const headers = [];
    const cookies = [];

    for (const [name, value] of request.headers) {
      headers.push(this.#serializeHeader(name, value));
      if (name.toLowerCase() == "cookie") {
        // TODO: Retrieve the actual cookies from the cookie store.
        const headerCookies = value.split(";");
        for (const cookie of headerCookies) {
          const equal = cookie.indexOf("=");
          const cookieName = cookie.substr(0, equal);
          const cookieValue = cookie.substr(equal + 1);
          const serializedCookie = this.#serializeHeader(
            unescape(cookieName.trim()),
            unescape(cookieValue.trim())
          );
          cookies.push(serializedCookie);
        }
      }
    }

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

  #onAuthRequired = (name, data) => {
    const { authCallbacks, request, response } = data;

    let isBlocked = false;
    try {
      const browsingContext = lazy.TabManager.getBrowsingContextById(
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

    const browsingContext = lazy.TabManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      // Do not emit events if the context id does not match any existing
      // browsing context.
      return;
    }

    const protocolEventName = "network.beforeRequestSent";

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
    if (beforeRequestSentEvent.isBlocked && request.supportsInterception) {
      // TODO: Requests suspended in beforeRequestSent still reach the server at
      // the moment. https://bugzilla.mozilla.org/show_bug.cgi?id=1849686
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
  };

  #onFetchError = (name, data) => {
    const { request } = data;

    const browsingContext = lazy.TabManager.getBrowsingContextById(
      request.contextId
    );
    if (!browsingContext) {
      // Do not emit events if the context id does not match any existing
      // browsing context.
      return;
    }

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

    const browsingContext = lazy.TabManager.getBrowsingContextById(
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

  #processNetworkEvent(event, request) {
    const requestData = this.#getRequestData(request);
    const navigation = request.navigationId;
    let contextId = null;
    let topContextId = null;
    if (request.contextId) {
      // Retrieve the top browsing context id for this network event.
      contextId = request.contextId;
      const browsingContext = lazy.TabManager.getBrowsingContextById(contextId);
      topContextId = lazy.TabManager.getIdForBrowsingContext(
        browsingContext.top
      );
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
    };

    if (isBlocked) {
      params.intercepts = intercepts;
    }

    return params;
  }

  #serializeCookieHeader(cookieHeader) {
    const name = cookieHeader.name;
    const value = deserializeBytesValue(cookieHeader.value);
    return `${name}=${value}`;
  }

  #serializeHeader(name, value) {
    return {
      name,
      value: this.#serializeStringAsBytesValue(value),
    };
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

  /**
   * Serialize a string value as BytesValue.
   *
   * Note: This does not attempt to fully implement serialize protocol bytes
   * (https://w3c.github.io/webdriver-bidi/#serialize-protocol-bytes) as the
   * header values read from the Channel are already serialized as strings at
   * the moment.
   *
   * @param {string} value
   *     The value to serialize.
   */
  #serializeStringAsBytesValue(value) {
    // TODO: For now, we handle all headers and cookies with the "string" type.
    // See Bug 1835216 to add support for "base64" type and handle non-utf8
    // values.
    return {
      type: BytesValueType.String,
      value,
    };
  }

  #startListening(event) {
    if (this.#subscribedEvents.size == 0) {
      this.#networkListener.startListening();
    }
    this.#subscribedEvents.add(event);
  }

  #stopListening(event) {
    this.#subscribedEvents.delete(event);
    if (this.#subscribedEvents.size == 0) {
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
 * @param {BytesValue} bytesValue
 *     The BytesValue to deserialize.
 * @returns {string}
 *     The deserialized value.
 */
export function deserializeBytesValue(bytesValue) {
  const { type, value } = bytesValue;

  if (type === BytesValueType.String) {
    return value;
  }

  // For type === BytesValueType.Base64.
  return atob(value);
}

export const network = NetworkModule;
