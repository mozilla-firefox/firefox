/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-nocheck Defer for now.

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExtensionDNR: "resource://gre/modules/ExtensionDNR.sys.mjs",
  ExtensionDocumentId: "resource://gre/modules/ExtensionDocumentId.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  SecurityInfo: "resource://gre/modules/SecurityInfo.sys.mjs",
  WebRequestUpload: "resource://gre/modules/WebRequestUpload.sys.mjs",
});

// WebRequest.sys.mjs's only consumer is ext-webRequest.js, so we can depend on
// the apiManager.global being initialized.
ChromeUtils.defineLazyGetter(lazy, "tabTracker", () => {
  return lazy.ExtensionParent.apiManager.global.tabTracker;
});
ChromeUtils.defineLazyGetter(
  lazy,
  "getCookieStoreIdForOriginAttributes",
  () => {
    return lazy.ExtensionParent.apiManager.global
      .getCookieStoreIdForOriginAttributes;
  }
);

// URI schemes that service workers are allowed to load scripts from (any other
// scheme is not allowed by the specs and it is not expected by the service workers
// internals neither, which would likely trigger unexpected behaviors).
const ALLOWED_SERVICEWORKER_SCHEMES = ["https", "http", "moz-extension"];

// Response HTTP Headers matching the following patterns are restricted for changes
// applied by MV3 extensions.
const MV3_RESTRICTED_HEADERS_PATTERNS = [
  /^cross-origin-embedder-policy$/,
  /^cross-origin-opener-policy$/,
  /^cross-origin-resource-policy$/,
  /^x-frame-options$/,
  /^access-control-/,
];

// Classes of requests that should be sent immediately instead of batched.
// Covers basically anything that can delay first paint or DOMContentLoaded:
// top frame HTML, <head> blocking CSS, fonts preflight, sync JS and XHR.
const URGENT_CLASSES =
  Ci.nsIClassOfService.Leader |
  Ci.nsIClassOfService.Unblocked |
  Ci.nsIClassOfService.UrgentStart |
  Ci.nsIClassOfService.TailForbidden;

function runLater(job) {
  Services.tm.dispatchToMainThread(job);
}

function parseFilter(filter) {
  if (!filter) {
    filter = {};
  }

  return {
    urls: filter.urls || null,
    types: filter.types || null,
    tabId: filter.tabId ?? null,
    windowId: filter.windowId ?? null,
    incognito: filter.incognito ?? null,
  };
}

function parseExtra(extra, allowed = [], optionsObj = {}) {
  if (extra) {
    for (let ex of extra) {
      if (!allowed.includes(ex)) {
        throw new lazy.ExtensionUtils.ExtensionError(`Invalid option ${ex}`);
      }
    }
  }

  let result = Object.assign({}, optionsObj);
  for (let al of allowed) {
    if (extra && extra.includes(al)) {
      result[al] = true;
    }
  }
  // From a parent process perspective an asyncBlocking listener
  // will be the same as a blocking listener that returned a
  // promise in the child process (because the callback used
  // by asyncBlocking listeners is a difference only visible
  // in the child process where the listener runs).
  if (result.asyncBlocking) {
    result.blocking = true;
  }
  return result;
}

function isThenable(value) {
  return value && typeof value === "object" && typeof value.then === "function";
}

// Verify a requested redirect and throw a more explicit error.
function verifyRedirect(channel, redirectUri, finalUrl, addonId) {
  const { isServiceWorkerScript } = channel;

  if (
    isServiceWorkerScript &&
    channel.loadInfo?.internalContentPolicyType ===
      Ci.nsIContentPolicy.TYPE_INTERNAL_SERVICE_WORKER
  ) {
    throw new Error(
      `Invalid redirectUrl ${redirectUri?.spec} on service worker main script ${finalUrl} requested by ${addonId}`
    );
  }

  if (
    isServiceWorkerScript &&
    (channel.loadInfo?.internalContentPolicyType ===
      Ci.nsIContentPolicy.TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS ||
      channel.loadInfo?.internalContentPolicyType ===
        Ci.nsIContentPolicy.TYPE_INTERNAL_WORKER_STATIC_MODULE) &&
    !ALLOWED_SERVICEWORKER_SCHEMES.includes(redirectUri?.scheme)
  ) {
    throw new Error(
      `Invalid redirectUrl ${redirectUri?.spec} on service worker imported script ${finalUrl} requested by ${addonId}`
    );
  }
}

class HeaderChanger {
  constructor(channel) {
    this.channel = channel;

    this.array = this.readHeaders();
  }

  getMap() {
    if (!this.map) {
      this.map = new Map();
      for (let header of this.array) {
        this.map.set(header.name.toLowerCase(), header);
      }
    }
    return this.map;
  }

  toArray() {
    return this.array;
  }

  validateHeaders(headers) {
    // We should probably use schema validation for this.

    if (!Array.isArray(headers)) {
      return false;
    }

    return headers.every(header => {
      if (typeof header !== "object" || header === null) {
        return false;
      }

      if (typeof header.name !== "string") {
        return false;
      }

      return (
        typeof header.value === "string" || Array.isArray(header.binaryValue)
      );
    });
  }

  applyChanges(headers, opts = {}) {
    if (!this.validateHeaders(headers)) {
      Cu.reportError(`Invalid header array: ${uneval(headers)}`);
      return;
    }

    let newHeaders = new Set(headers.map(({ name }) => name.toLowerCase()));

    // Remove missing headers.
    let origHeaders = this.getMap();
    for (let name of origHeaders.keys()) {
      if (!newHeaders.has(name)) {
        this.setHeader(name, "", false, opts, name);
      }
    }

    // Set new or changed headers.  If there are multiple headers with the same
    // name (e.g. Set-Cookie), merge them, instead of having new values
    // overwrite previous ones.
    //
    // When the new value of a header is equal the existing value of the header
    // (e.g. the initial response set "Set-Cookie: examplename=examplevalue",
    // and an extension also added the header
    // "Set-Cookie: examplename=examplevalue") then the header value is not
    // re-set, but subsequent headers of the same type will be merged in.
    //
    // Multiple addons will be able to provide modifications to any headers
    // listed in the default set.
    let headersAlreadySet = new Set();
    for (let { name, value, binaryValue } of headers) {
      if (binaryValue) {
        value = String.fromCharCode(...binaryValue);
      }

      let lowerCaseName = name.toLowerCase();
      let original = origHeaders.get(lowerCaseName);

      if (!original || value !== original.value) {
        let shouldMerge = headersAlreadySet.has(lowerCaseName);
        this.setHeader(name, value, shouldMerge, opts, lowerCaseName);
      }

      headersAlreadySet.add(lowerCaseName);
    }
  }
}

const checkRestrictedHeaderValue = (value, opts = {}) => {
  let uri = Services.io.newURI(`https://${value}/`);
  let { policy } = opts;

  if (policy && !policy.allowedOrigins.matches(uri)) {
    throw new Error(`Unable to set host header, url missing from permissions.`);
  }

  if (WebExtensionPolicy.isRestrictedURI(uri)) {
    throw new Error(`Unable to set host header to restricted url.`);
  }

  if (policy?.checkGuarded(uri)) {
    throw new Error(`Unable to set host header to guarded url.`);
  }
};

class RequestHeaderChanger extends HeaderChanger {
  setHeader(name, value, merge, opts, lowerCaseName) {
    try {
      if (value && lowerCaseName === "host") {
        checkRestrictedHeaderValue(value, opts);
      }
      this.channel.setRequestHeader(name, value, merge);
    } catch (e) {
      Cu.reportError(new Error(`Error setting request header ${name}: ${e}`));
    }
  }

  readHeaders() {
    return this.channel.getRequestHeaders();
  }
}

class ResponseHeaderChanger extends HeaderChanger {
  didModifyCSP = false;

  setHeader(name, value, merge, opts, lowerCaseName) {
    if (lowerCaseName === "content-security-policy") {
      // When multiple add-ons change the CSP, enforce the combined (strictest)
      // policy - see bug 1462989 for motivation.
      // When value is unset, don't force the header to be merged, to allow
      // add-ons to clear the header if wanted.
      if (value) {
        merge = merge || this.didModifyCSP;
      }

      // For manifest_version 3 extension, we are currently only allowing to
      // merge additional CSP strings to the existing ones, which will be initially
      // stricter than currently allowed to manifest_version 2 extensions, then
      // following up with either a new permission and/or some more changes to the
      // APIs (and possibly making the behavior more deterministic than it is for
      // manifest_version 2 at the moment).
      if (opts.policy.manifestVersion > 2) {
        if (value) {
          // If the given CSP header value is non empty, then it should be
          // merged to the existing one.
          merge = true;
        } else {
          // If the given CSP header value is empty (which would be clearing the
          // CSP header), it should be considered a no-op and this.didModifyCSP
          // shouldn't be changed to true.
          return;
        }
      }

      this.didModifyCSP = true;
    } else if (
      opts.policy.manifestVersion > 2 &&
      this.isResponseHeaderRestricted(lowerCaseName)
    ) {
      // TODO (Bug 1787155 and Bug 1273281) open this up to MV3 extensions,
      // locked behind manifest.json declarative permission and a separate
      // explicit user-controlled permission (and ideally also check for
      // changes that would lead to security downgrades).
      Cu.reportError(
        `Disallowed change restricted response header ${name} on ${this.channel.finalURL} from ${opts.policy.debugName}`
      );
      return;
    }

    try {
      this.channel.setResponseHeader(name, value, merge);
    } catch (e) {
      Cu.reportError(new Error(`Error setting response header ${name}: ${e}`));
    }
  }

  isResponseHeaderRestricted(lowerCaseHeaderName) {
    return MV3_RESTRICTED_HEADERS_PATTERNS.some(regex =>
      regex.test(lowerCaseHeaderName)
    );
  }

  readHeaders() {
    return this.channel.getResponseHeaders();
  }
}

const MAYBE_CACHED_EVENTS = new Set([
  "onResponseStarted",
  "onHeadersReceived",
  "onBeforeRedirect",
  "onCompleted",
  "onErrorOccurred",
]);

const OPTIONAL_PROPERTIES = [
  "requestHeaders",
  "responseHeaders",
  "statusCode",
  "statusLine",
  "error",
  "redirectUrl",
  "requestBody",
  "scheme",
  "realm",
  "isProxy",
  "challenger",
  "proxyInfo",
  "ip",
  "frameAncestors",
  "urlClassification",
  "requestSize",
  "responseSize",
  "documentId",
  "parentDocumentId",
];

function serializeRequestData(eventName) {
  let data = {
    requestId: this.requestId,
    url: this.url,
    originUrl: this.originUrl,
    documentUrl: this.documentUrl,
    method: this.method,
    type: this.type,
    timeStamp: Date.now(),
    tabId: this.tabId,
    frameId: this.frameId,
    parentFrameId: this.parentFrameId,
    incognito: this.incognito,
    thirdParty: this.thirdParty,
    cookieStoreId: this.cookieStoreId,
    urgentSend: this.urgentSend,
  };

  if (MAYBE_CACHED_EVENTS.has(eventName)) {
    data.fromCache = !!this.fromCache;
  }

  for (let opt of OPTIONAL_PROPERTIES) {
    if (typeof this[opt] !== "undefined") {
      data[opt] = this[opt];
    }
  }

  if (this.urlClassification) {
    data.urlClassification = {
      firstParty: this.urlClassification.firstParty.filter(
        c => !c.startsWith("socialtracking_")
      ),
      thirdParty: this.urlClassification.thirdParty.filter(
        c => !c.startsWith("socialtracking_")
      ),
    };
  }

  return data;
}

var HttpObserverManager;

var ChannelEventSink = {
  _classDescription: "WebRequest channel event sink",
  _classID: Components.ID("115062f8-92f1-11e5-8b7f-080027b0f7ec"),
  _contractID: "@mozilla.org/webrequest/channel-event-sink;1",

  QueryInterface: ChromeUtils.generateQI(["nsIChannelEventSink", "nsIFactory"]),

  init() {
    Components.manager
      .QueryInterface(Ci.nsIComponentRegistrar)
      .registerFactory(
        this._classID,
        this._classDescription,
        this._contractID,
        this
      );
  },

  register() {
    Services.catMan.addCategoryEntry(
      "net-channel-event-sinks",
      this._contractID,
      this._contractID,
      false,
      true
    );
  },

  unregister() {
    Services.catMan.deleteCategoryEntry(
      "net-channel-event-sinks",
      this._contractID,
      false
    );
  },

  // nsIChannelEventSink implementation
  asyncOnChannelRedirect(oldChannel, newChannel, flags, redirectCallback) {
    runLater(() => redirectCallback.onRedirectVerifyCallback(Cr.NS_OK));
    try {
      HttpObserverManager.onChannelReplaced(oldChannel, newChannel);
    } catch (e) {
      // we don't wanna throw: it would abort the redirection
      Cu.reportError(e);
    }
  },

  // nsIFactory implementation
  createInstance(iid) {
    return this.QueryInterface(iid);
  },
};

ChannelEventSink.init();

// nsIAuthPrompt2 implementation for onAuthRequired
class AuthRequestor {
  constructor(channel, httpObserver) {
    this.notificationCallbacks = channel.notificationCallbacks;
    this.loadGroupCallbacks =
      channel.loadGroup && channel.loadGroup.notificationCallbacks;
    this.httpObserver = httpObserver;
  }

  getInterface(iid) {
    if (iid.equals(Ci.nsIAuthPromptProvider) || iid.equals(Ci.nsIAuthPrompt2)) {
      return this;
    }
    try {
      return this.notificationCallbacks.getInterface(iid);
    } catch (e) {}
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  }

  _getForwardedInterface(iid) {
    try {
      return this.notificationCallbacks.getInterface(iid);
    } catch (e) {
      return this.loadGroupCallbacks.getInterface(iid);
    }
  }

  // nsIAuthPromptProvider getAuthPrompt
  getAuthPrompt(reason, iid) {
    // This should never get called without getInterface having been called first.
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      return this;
    }
    return this._getForwardedInterface(Ci.nsIAuthPromptProvider).getAuthPrompt(
      reason,
      iid
    );
  }

  // nsIAuthPrompt2 promptAuth
  promptAuth(channel, level, authInfo) {
    this._getForwardedInterface(Ci.nsIAuthPrompt2).promptAuth(
      channel,
      level,
      authInfo
    );
  }

  _getForwardPrompt(data) {
    let reason = data.isProxy
      ? Ci.nsIAuthPromptProvider.PROMPT_PROXY
      : Ci.nsIAuthPromptProvider.PROMPT_NORMAL;
    for (let callbacks of [
      this.notificationCallbacks,
      this.loadGroupCallbacks,
    ]) {
      try {
        return callbacks
          .getInterface(Ci.nsIAuthPromptProvider)
          .getAuthPrompt(reason, Ci.nsIAuthPrompt2);
      } catch (e) {}
      try {
        return callbacks.getInterface(Ci.nsIAuthPrompt2);
      } catch (e) {}
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  }

  // nsIAuthPrompt2 asyncPromptAuth
  asyncPromptAuth(channel, callback, context, level, authInfo) {
    let wrapper = ChannelWrapper.get(channel);

    let uri = channel.URI;
    let proxyInfo;
    let isProxy = !!(authInfo.flags & authInfo.AUTH_PROXY);
    if (isProxy && channel instanceof Ci.nsIProxiedChannel) {
      proxyInfo = channel.proxyInfo;
    }
    let data = {
      scheme: authInfo.authenticationScheme,
      realm: authInfo.realm,
      isProxy,
      challenger: {
        host: proxyInfo ? proxyInfo.host : uri.host,
        port: proxyInfo ? proxyInfo.port : uri.port,
      },
    };

    // In the case that no listener provides credentials, we fallback to the
    // previously set callback class for authentication.
    wrapper.authPromptForward = () => {
      try {
        let prompt = this._getForwardPrompt(data);
        prompt.asyncPromptAuth(channel, callback, context, level, authInfo);
      } catch (e) {
        Cu.reportError(`webRequest asyncPromptAuth failure ${e}`);
        callback.onAuthCancelled(context, false);
      }
      wrapper.authPromptForward = null;
      wrapper.authPromptCallback = null;
    };
    wrapper.authPromptCallback = authCredentials => {
      // The API allows for canceling the request, providing credentials or
      // doing nothing, so we do not provide a way to call onAuthCanceled.
      // Canceling the request will result in canceling the authentication.
      if (
        authCredentials &&
        typeof authCredentials.username === "string" &&
        typeof authCredentials.password === "string"
      ) {
        authInfo.username = authCredentials.username;
        authInfo.password = authCredentials.password;
        try {
          callback.onAuthAvailable(context, authInfo);
        } catch (e) {
          Cu.reportError(`webRequest onAuthAvailable failure ${e}`);
        }
        // At least one addon has responded, so we won't forward to the regular
        // prompt handlers.
        wrapper.authPromptForward = null;
        wrapper.authPromptCallback = null;
      }
    };

    this.httpObserver.runChannelListener(wrapper, "onAuthRequired", data);

    return {
      QueryInterface: ChromeUtils.generateQI(["nsICancelable"]),
      cancel() {
        try {
          callback.onAuthCancelled(context, false);
        } catch (e) {
          Cu.reportError(`webRequest onAuthCancelled failure ${e}`);
        }
        wrapper.authPromptForward = null;
        wrapper.authPromptCallback = null;
      },
    };
  }
}

AuthRequestor.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIInterfaceRequestor",
  "nsIAuthPromptProvider",
  "nsIAuthPrompt2",
]);

// Most WebRequest events are implemented via the observer services, but
// a few use custom xpcom interfaces.  This class (HttpObserverManager)
// serves two main purposes:
//  1. It abstracts away the names and details of the underlying
//     implementation (e.g., onBeforeBeforeRequest is dispatched from
//     the http-on-modify-request observable).
//  2. It aggregates multiple listeners so that a single observer or
//     handler can serve multiple webRequest listeners.
HttpObserverManager = {
  listeners: {
    // onBeforeRequest uses http-on-modify observer for HTTP(S).
    onBeforeRequest: new Map(),

    // onBeforeSendHeaders and onSendHeaders correspond to the
    // http-on-before-connect observer.
    onBeforeSendHeaders: new Map(),
    onSendHeaders: new Map(),

    // onHeadersReceived corresponds to the http-on-examine-* obserservers.
    onHeadersReceived: new Map(),

    // onAuthRequired is handled via the nsIAuthPrompt2 xpcom interface
    // which is managed here by AuthRequestor.
    onAuthRequired: new Map(),

    // onBeforeRedirect is handled by the nsIChannelEVentSink xpcom interface
    // which is managed here by ChannelEventSink.
    onBeforeRedirect: new Map(),

    // onResponseStarted, onErrorOccurred, and OnCompleted correspond
    // to events dispatched by the ChannelWrapper EventTarget.
    onResponseStarted: new Map(),
    onErrorOccurred: new Map(),
    onCompleted: new Map(),
  },
  // Whether there are any registered declarativeNetRequest rules. These DNR
  // rules may match new requests and result in request modifications.
  dnrActive: false,

  openingInitialized: false,
  beforeConnectInitialized: false,
  examineInitialized: false,
  redirectInitialized: false,
  hasRedirects: false,

  getWrapper(nativeChannel) {
    let wrapper = ChannelWrapper.get(nativeChannel);
    if (!wrapper._addedListeners) {
      /* eslint-disable mozilla/balanced-listeners */
      if (this.listeners.onErrorOccurred.size) {
        wrapper.addEventListener("error", this);
      }
      if (this.listeners.onResponseStarted.size) {
        wrapper.addEventListener("start", this);
      }
      if (this.listeners.onCompleted.size) {
        wrapper.addEventListener("stop", this);
      }
      /* eslint-enable mozilla/balanced-listeners */

      wrapper._addedListeners = true;
    }
    return wrapper;
  },

  // This method is called whenever webRequest listeners are added or removed.
  // It reconciles the set of listeners with underlying observers, event
  // handlers, etc. by adding new low-level handlers for any newly added
  // webRequest listeners and removing those that are no longer needed if
  // there are no more listeners for corresponding webRequest events.
  addOrRemove() {
    // onErrorOccurred needs http-on-modify-request so the ChannelWrapper is
    // created at channel start, before any failure that would prevent
    // http-on-examine-response from firing (e.g. pre-header connection failure).
    let needOpening =
      this.listeners.onBeforeRequest.size ||
      this.listeners.onErrorOccurred.size ||
      this.dnrActive;
    let needBeforeConnect =
      this.listeners.onBeforeSendHeaders.size ||
      this.listeners.onSendHeaders.size ||
      this.dnrActive;
    if (needOpening && !this.openingInitialized) {
      this.openingInitialized = true;
      Services.obs.addObserver(this, "http-on-modify-request");
    } else if (!needOpening && this.openingInitialized) {
      this.openingInitialized = false;
      Services.obs.removeObserver(this, "http-on-modify-request");
    }
    if (needBeforeConnect && !this.beforeConnectInitialized) {
      this.beforeConnectInitialized = true;
      Services.obs.addObserver(this, "http-on-before-connect");
    } else if (!needBeforeConnect && this.beforeConnectInitialized) {
      this.beforeConnectInitialized = false;
      Services.obs.removeObserver(this, "http-on-before-connect");
    }

    let haveBlocking = Object.values(this.listeners).some(listeners =>
      Array.from(listeners.values()).some(listener => listener.blockingAllowed)
    );

    let needTracing =
      this.listeners.onResponseStarted.size ||
      this.listeners.onErrorOccurred.size ||
      this.listeners.onCompleted.size ||
      haveBlocking;

    let needExamine =
      needTracing ||
      this.listeners.onHeadersReceived.size ||
      this.listeners.onAuthRequired.size ||
      this.dnrActive;

    if (needExamine && !this.examineInitialized) {
      this.examineInitialized = true;
      Services.obs.addObserver(this, "http-on-examine-response");
      Services.obs.addObserver(this, "http-on-examine-cached-response");
      Services.obs.addObserver(this, "http-on-examine-merged-response");
    } else if (!needExamine && this.examineInitialized) {
      this.examineInitialized = false;
      Services.obs.removeObserver(this, "http-on-examine-response");
      Services.obs.removeObserver(this, "http-on-examine-cached-response");
      Services.obs.removeObserver(this, "http-on-examine-merged-response");
    }

    // If we have any listeners, we need the channelsink so the channelwrapper is
    // updated properly. Otherwise events for channels that are redirected will not
    // happen correctly.  If we have no listeners, shut it down.
    this.hasRedirects = this.listeners.onBeforeRedirect.size > 0;
    let needRedirect =
      this.hasRedirects || needExamine || needOpening || needBeforeConnect;
    if (needRedirect && !this.redirectInitialized) {
      this.redirectInitialized = true;
      ChannelEventSink.register();
    } else if (!needRedirect && this.redirectInitialized) {
      this.redirectInitialized = false;
      ChannelEventSink.unregister();
    }
  },

  addListener(kind, callback, opts) {
    this.listeners[kind].set(callback, opts);
    this.addOrRemove();
  },

  removeListener(kind, callback) {
    this.listeners[kind].delete(callback);
    this.addOrRemove();
  },

  setDNRHandlingEnabled(dnrActive) {
    this.dnrActive = dnrActive;
    this.addOrRemove();
  },

  observe(subject, topic, data) {
    let channel = this.getWrapper(subject);
    switch (topic) {
      case "http-on-modify-request":
        this.runChannelListener(channel, "onBeforeRequest");
        break;
      case "http-on-before-connect":
        this.runChannelListener(channel, "onBeforeSendHeaders");
        break;
      case "http-on-examine-cached-response":
      case "http-on-examine-merged-response":
        channel.fromCache = true;
      // falls through
      case "http-on-examine-response":
        this.examine(channel, topic, data);
        break;
    }
  },

  getRequestData(channel, extraData) {
    let originAttributes = channel.loadInfo?.originAttributes;
    let cos = channel.channel.QueryInterface(Ci.nsIClassOfService);

    let data = {
      requestId: String(channel.id),
      url: channel.finalURL,
      method: channel.method,
      type: channel.type,
      fromCache: channel.fromCache,
      incognito: originAttributes?.privateBrowsingId > 0,
      thirdParty: channel.thirdParty,

      originUrl: channel.originURL || undefined,
      documentUrl: channel.documentURL || undefined,

      tabId: this.getBrowserData(channel).tabId,
      frameId: channel.frameId,
      parentFrameId: channel.parentFrameId,
      documentId: lazy.ExtensionDocumentId.getDocumentId(
        channel.documentInnerWindowId
      ),
      parentDocumentId: lazy.ExtensionDocumentId.getDocumentId(
        channel.parentDocumentInnerWindowId
      ),

      frameAncestors: channel.frameAncestors || undefined,

      ip: channel.remoteAddress,

      proxyInfo: channel.proxyInfo,

      serialize: serializeRequestData,
      requestSize: channel.requestSize,
      responseSize: channel.responseSize,
      urlClassification: channel.urlClassification,

      // Figure out if this is an urgent request that shouldn't be batched.
      urgentSend: (cos.classFlags & URGENT_CLASSES) > 0,
    };

    if (originAttributes) {
      data.cookieStoreId =
        lazy.getCookieStoreIdForOriginAttributes(originAttributes);
    }

    return Object.assign(data, extraData);
  },

  handleEvent(event) {
    let channel = event.currentTarget;
    switch (event.type) {
      case "error":
        this.runChannelListener(channel, "onErrorOccurred", {
          error: channel.errorString,
        });
        break;
      case "start":
        this.runChannelListener(channel, "onResponseStarted");
        break;
      case "stop":
        this.runChannelListener(channel, "onCompleted");
        break;
    }
  },

  STATUS_TYPES: new Set([
    "onHeadersReceived",
    "onAuthRequired",
    "onBeforeRedirect",
    "onResponseStarted",
    "onCompleted",
  ]),
  FILTER_TYPES: new Set([
    "onBeforeRequest",
    "onBeforeSendHeaders",
    "onSendHeaders",
    "onHeadersReceived",
    "onAuthRequired",
    "onBeforeRedirect",
  ]),

  getBrowserData(wrapper) {
    let browserData = wrapper._browserData;
    if (!browserData) {
      if (wrapper.browserElement) {
        browserData = lazy.tabTracker.getBrowserData(wrapper.browserElement);
      } else {
        browserData = { tabId: -1, windowId: -1 };
      }
      wrapper._browserData = browserData;
    }
    return browserData;
  },

  runChannelListener(channel, kind, extraData = null) {
    let handlerResults = [];
    let requestHeaders;
    let responseHeaders;

    try {
      if (kind !== "onErrorOccurred" && channel.errorString) {
        return;
      }
      if (this.dnrActive) {
        // DNR may modify (but not cancel) the request at this stage.
        lazy.ExtensionDNR.beforeWebRequestEvent(channel, kind);
      }

      let registerFilter = this.FILTER_TYPES.has(kind);
      let commonData = null;
      let requestBody;
      this.listeners[kind].forEach((opts, callback) => {
        if (opts.filter.tabId !== null || opts.filter.windowId !== null) {
          const { tabId, windowId } = this.getBrowserData(channel);
          if (
            (opts.filter.tabId !== null && tabId != opts.filter.tabId) ||
            (opts.filter.windowId !== null && windowId != opts.filter.windowId)
          ) {
            return;
          }
        }
        // ChannelWrapper::Matches does not only match the filter, it also
        // checks whether the extension is allowed to modify the request.
        // Regardless of extension permissions, if channel.canModify is false,
        // then the request is not matched.
        // There is one exception: When opts.isProxy is set (in onAuthRequired)
        // AND the extension has the "proxy" permission, then the extension is
        // allowed to handle the request regardless of channel.canModify.
        if (!channel.matches(opts.filter, opts.policy, extraData)) {
          return;
        }

        let extension = opts.policy?.extension;
        // TODO: Move this logic to ChannelWrapper::matches, see bug 1699481
        if (
          extension?.userContextIsolation &&
          !extension.canAccessContainer(
            channel.loadInfo?.originAttributes.userContextId
          )
        ) {
          return;
        }

        if (!commonData) {
          commonData = this.getRequestData(channel, extraData);
          if (this.STATUS_TYPES.has(kind)) {
            commonData.statusCode = channel.statusCode;
            commonData.statusLine = channel.statusLine;
          }
        }
        let data = Object.create(commonData);
        data.urgentSend = data.urgentSend && opts.blocking;

        if (registerFilter && opts.blocking && opts.policy) {
          channel.registerTraceableChannel(opts.policy);
        }

        if (opts.requestHeaders) {
          requestHeaders = requestHeaders || new RequestHeaderChanger(channel);
          data.requestHeaders = requestHeaders.toArray();
        }

        if (opts.responseHeaders) {
          try {
            responseHeaders =
              responseHeaders || new ResponseHeaderChanger(channel);
            data.responseHeaders = responseHeaders.toArray();
          } catch (e) {
            /* headers may not be available on some redirects */
          }
        }

        if (opts.requestBody) {
          requestBody =
            requestBody ||
            lazy.WebRequestUpload.createRequestBody(channel.channel);
          data.requestBody = requestBody;
        }

        try {
          let result = callback(data);

          if (typeof result === "object" && opts.blocking) {
            handlerResults.push({ opts, result });
          }
        } catch (e) {
          Cu.reportError(e);
        }
      });
    } catch (e) {
      Cu.reportError(e);
    }

    if (this.dnrActive && lazy.ExtensionDNR.handleRequest(channel, kind)) {
      return;
    }

    return this.applyChanges(
      kind,
      channel,
      handlerResults,
      requestHeaders,
      responseHeaders
    );
  },

  async applyChanges(
    kind,
    channel,
    handlerResults,
    requestHeaders,
    responseHeaders
  ) {
    const { finalURL, id: chanId } = channel;
    let shouldResume = !channel.suspended;
    // NOTE: if a request has been suspended before the GeckoProfiler
    // has been activated and then resumed while the GeckoProfiler is active
    // and collecting data, the resulting "Extension Suspend" marker will be
    // recorded with an empty marker text (and so without url, chan id and
    // the supenders addon ids).
    let markerText = "";
    if (Services.profiler.IsActive()) {
      const suspenders = handlerResults
        .filter(({ result }) => isThenable(result))
        .map(({ opts }) => opts.addonId)
        .join(", ");
      markerText = `${kind} ${finalURL} by ${suspenders} (chanId: ${chanId})`;
    }
    try {
      for (let { opts, result } of handlerResults) {
        if (isThenable(result)) {
          channel.suspend(markerText);
          try {
            result = await result;
          } catch (e) {
            let error;

            if (e instanceof Error) {
              error = e;
            } else if (typeof e === "object" && e.message) {
              error = new Error(e.message, e.fileName, e.lineNumber);
            }

            Cu.reportError(error);
            continue;
          }
          if (!result || typeof result !== "object") {
            continue;
          }
        }

        if (kind === "onAuthRequired") {
          if (result.authCredentials && channel.authPromptCallback) {
            channel.authPromptCallback(result.authCredentials);
          }

          // We allow proxy auth to cancel or handle authCredentials regardless
          // of canModify, but ensure we do nothing else.
          if (!channel.canModify) {
            continue;
          }
        }

        if (result.cancel) {
          channel.resume();
          channel.cancel(
            Cr.NS_ERROR_ABORT,
            Ci.nsILoadInfo.BLOCKING_REASON_EXTENSION_WEBREQUEST
          );
          ChromeUtils.addProfilerMarker(
            "Extension Canceled",
            { category: "Network" },
            `${kind} ${finalURL} canceled by ${opts.addonId} (chanId: ${chanId})`
          );
          if (opts.policy) {
            let properties = channel.channel.QueryInterface(
              Ci.nsIWritablePropertyBag
            );
            properties.setProperty("cancelledByExtension", opts.policy.id);
          }
          return;
        }

        if (result.redirectUrl) {
          try {
            const { redirectUrl } = result;
            channel.resume();
            const redirectUri = Services.io.newURI(redirectUrl);
            verifyRedirect(channel, redirectUri, finalURL, opts.addonId);
            channel.redirectTo(redirectUri);
            ChromeUtils.addProfilerMarker(
              "Extension Redirected",
              { category: "Network" },
              `${kind} ${finalURL} redirected to ${redirectUrl} by ${opts.addonId} (chanId: ${chanId})`
            );
            if (opts.policy) {
              let properties = channel.channel.QueryInterface(
                Ci.nsIWritablePropertyBag
              );
              properties.setProperty("redirectedByExtension", opts.policy.id);
            }

            // Web Extensions using the WebRequest API are allowed
            // to redirect a channel to a data: URI, hence we mark
            // the channel to let the redirect blocker know. Please
            // note that this marking needs to happen after the
            // channel.redirectTo is called because the channel's
            // RedirectTo() implementation explicitly drops the flag
            // to avoid additional redirects not caused by the
            // Web Extension.
            channel.loadInfo.allowInsecureRedirectToDataURI = true;

            // To pass CORS checks, we pretend the current request's
            // response allows the triggering origin to access.
            let origin = channel.getRequestHeader("Origin");
            if (origin) {
              channel.setResponseHeader("Access-Control-Allow-Origin", origin);
              channel.setResponseHeader(
                "Access-Control-Allow-Credentials",
                "true"
              );

              // Compute an arbitrary 'Access-Control-Allow-Headers'
              // for the  internal Redirect

              let allowHeaders = channel
                .getRequestHeaders()
                .map(header => header.name)
                .join();
              channel.setResponseHeader(
                "Access-Control-Allow-Headers",
                allowHeaders
              );

              channel.setResponseHeader(
                "Access-Control-Allow-Methods",
                channel.method
              );
            }

            return;
          } catch (e) {
            Cu.reportError(e);
          }
        }

        if (result.upgradeToSecure && kind === "onBeforeRequest") {
          try {
            channel.upgradeToSecure();
          } catch (e) {
            Cu.reportError(e);
          }
        }

        if (opts.requestHeaders && result.requestHeaders && requestHeaders) {
          requestHeaders.applyChanges(result.requestHeaders, opts);
        }

        if (opts.responseHeaders && result.responseHeaders && responseHeaders) {
          responseHeaders.applyChanges(result.responseHeaders, opts);
        }
      }

      // If a listener did not cancel the request or provide credentials, we
      // forward the auth request to the base handler.
      if (kind === "onAuthRequired" && channel.authPromptForward) {
        channel.authPromptForward();
      }

      if (kind === "onBeforeSendHeaders" && this.listeners.onSendHeaders.size) {
        this.runChannelListener(channel, "onSendHeaders");
      } else if (kind !== "onErrorOccurred") {
        channel.errorCheck();
      }
    } catch (e) {
      Cu.reportError(e);
    }

    // Only resume the channel if it was suspended by this call.
    if (shouldResume) {
      channel.resume();
    }
  },

  shouldHookListener(listener, channel, extraData) {
    if (listener.size == 0) {
      return false;
    }

    for (let opts of listener.values()) {
      if (channel.matches(opts.filter, opts.policy, extraData)) {
        return true;
      }
    }
    return false;
  },

  examine(channel) {
    if (this.listeners.onHeadersReceived.size || this.dnrActive) {
      this.runChannelListener(channel, "onHeadersReceived");
    }

    if (
      !channel.hasAuthRequestor &&
      this.shouldHookListener(this.listeners.onAuthRequired, channel, {
        isProxy: true,
      })
    ) {
      channel.channel.notificationCallbacks = new AuthRequestor(
        channel.channel,
        this
      );
      channel.hasAuthRequestor = true;
    }
  },

  onChannelReplaced(oldChannel, newChannel) {
    let channel = this.getWrapper(oldChannel);

    // We want originalURI, this will provide a moz-ext rather than jar or file
    // uri on redirects.
    if (this.hasRedirects) {
      this.runChannelListener(channel, "onBeforeRedirect", {
        redirectUrl: newChannel.originalURI.spec,
      });
    }
    channel.channel = newChannel;
  },
};

function HttpEvent(internalEvent, options) {
  this.internalEvent = internalEvent;
  this.options = options;
}

HttpEvent.prototype = {
  addListener(callback, filter = null, options = null, optionsObject = null) {
    let opts = parseExtra(options, this.options, optionsObject);
    opts.filter = parseFilter(filter);
    HttpObserverManager.addListener(this.internalEvent, callback, opts);
  },

  removeListener(callback) {
    HttpObserverManager.removeListener(this.internalEvent, callback);
  },
};

var onBeforeRequest = new HttpEvent("onBeforeRequest", [
  "blocking",
  "requestBody",
]);
var onBeforeSendHeaders = new HttpEvent("onBeforeSendHeaders", [
  "requestHeaders",
  "blocking",
]);
var onSendHeaders = new HttpEvent("onSendHeaders", ["requestHeaders"]);
var onHeadersReceived = new HttpEvent("onHeadersReceived", [
  "blocking",
  "responseHeaders",
]);
var onAuthRequired = new HttpEvent("onAuthRequired", [
  "asyncBlocking",
  "blocking",
  "responseHeaders",
]);
var onBeforeRedirect = new HttpEvent("onBeforeRedirect", ["responseHeaders"]);
var onResponseStarted = new HttpEvent("onResponseStarted", ["responseHeaders"]);
var onCompleted = new HttpEvent("onCompleted", ["responseHeaders"]);
var onErrorOccurred = new HttpEvent("onErrorOccurred");

export var WebRequest = {
  setDNRHandlingEnabled: dnrActive => {
    HttpObserverManager.setDNRHandlingEnabled(dnrActive);
  },
  getTabIdForChannelWrapper: channel => {
    // Warning: This method should only be called after the initialization of
    // ExtensionParent.apiManager.global. Generally, this means that this method
    // should only be used by implementations of extension API methods (which
    // themselves are loaded in ExtensionParent.apiManager.global and therefore
    // imply the initialization of ExtensionParent.apiManager.global).
    return HttpObserverManager.getBrowserData(channel).tabId;
  },

  onBeforeRequest,
  onBeforeSendHeaders,
  onSendHeaders,
  onHeadersReceived,
  onAuthRequired,
  onBeforeRedirect,
  onResponseStarted,
  onCompleted,
  onErrorOccurred,

  getSecurityInfo: details => {
    let channel = ChannelWrapper.getRegisteredChannel(
      details.id,
      details.policy,
      details.remoteTab
    );
    if (channel) {
      return lazy.SecurityInfo.getSecurityInfo(
        channel.channel,
        details.options
      );
    }
  },
};
