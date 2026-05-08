/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint no-shadow: error, mozilla/no-aArgs: error */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

/**
 * @import {SearchEngine} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 */

const lazy = XPCOMUtils.declareLazy({
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  logConsole: () =>
    console.createInstance({
      prefix: "SearchUtils",
      maxLogLevel: SearchUtils.loggingEnabled ? "Debug" : "Warn",
    }),
});

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

const BROWSER_SEARCH_PREF = "browser.search.";

/**
 * Load listener
 *
 * @implements {nsIRequestObserver}
 * @implements {nsIStreamListener}
 * @implements {nsIChannelEventSink}
 * @implements {nsIInterfaceRequestor}
 * @implements {nsIProgressEventSink}
 */
class LoadListener {
  _bytes = [];
  _callback = null;
  _channel = null;
  _countRead = 0;
  _expectedContentType = null;
  _stream = null;
  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIRequestObserver,
    Ci.nsIStreamListener,
    Ci.nsIChannelEventSink,
    Ci.nsIInterfaceRequestor,
    Ci.nsIProgressEventSink,
  ]);

  /**
   * Constructor
   *
   * @param {nsIChannel} channel
   *   The initial channel to load from.
   * @param {RegExp} expectedContentType
   *   A regular expression to match the expected content type to.
   * @param {Function} callback
   *   A callback to receive the loaded data. The callback is passed the bytes
   *   (array) and the content type received. The bytes argument may be null if
   *   no data could be loaded.
   */
  constructor(channel, expectedContentType, callback) {
    this._channel = channel;
    this._callback = callback;
    this._expectedContentType = expectedContentType;
  }

  // nsIRequestObserver
  onStartRequest(request) {
    lazy.logConsole.debug("loadListener: Starting request:", request.name);
    this._stream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
  }

  onStopRequest(request, statusCode) {
    lazy.logConsole.debug("loadListener: Stopping request:", request.name);

    var requestFailed = !Components.isSuccessCode(statusCode);
    if (!requestFailed && request instanceof Ci.nsIHttpChannel) {
      requestFailed = !request.requestSucceeded;
    }

    if (requestFailed || this._countRead == 0) {
      lazy.logConsole.debug("loadListener: request failed!");
      // send null so the callback can deal with the failure
      this._bytes = null;
    } else if (!this._expectedContentType.test(this._channel.contentType)) {
      lazy.logConsole.warn(
        "loadListener: Content type does not match expected",
        this._channel.contentType
      );
      this._bytes = null;
    }
    this._callback(this._bytes, this._bytes ? this._channel.contentType : "");
    this._channel = null;
  }

  // nsIStreamListener
  onDataAvailable(request, inputStream, offset, count) {
    this._stream.setInputStream(inputStream);

    // Get a byte array of the data
    this._bytes = this._bytes.concat(this._stream.readByteArray(count));
    this._countRead += count;
  }

  // nsIChannelEventSink
  asyncOnChannelRedirect(oldChannel, newChannel, flags, callback) {
    this._channel = newChannel;
    callback.onRedirectVerifyCallback(Cr.NS_OK);
  }

  /**
   * nsIInterfaceRequestor
   *
   * @template {nsIID} T
   * @param {T} iid
   * @returns {nsQIResult<T>}
   */
  getInterface(iid) {
    return this.QueryInterface(iid);
  }

  // nsIProgressEventSink
  onProgress() {}
  onStatus() {}
}

/**
 * Describes reasons why a search engine might have failed installation. Mainly
 * used by OpenSearch engines, but may be raised for other engines as well.
 */
export class SearchEngineInstallError extends Error {
  /**
   * @param {"duplicate-title"|"corrupted"|"download-failure"} type
   * @param {Parameters<ErrorConstructor>} params
   */
  constructor(type, ...params) {
    super(...params);
    this.type = type;
  }
}

export var SearchUtils = {
  BROWSER_SEARCH_PREF,

  /**
   * This is the Remote Settings key that we use to get the ignore lists for
   * engines.
   *
   * @readonly
   */
  SETTINGS_IGNORELIST_KEY: "hijack-blocklists",

  /**
   * This is the Remote Settings key that we use to get the allow lists for
   * overriding the default engines.
   *
   * @readonly
   */
  SETTINGS_ALLOWLIST_KEY: "search-default-override-allowlist",

  /**
   * This is the Remote Settings key that we use to get the search engine
   * configurations.
   *
   * @readonly
   */
  SETTINGS_KEY: "search-config-v2",

  /**
   * This is the Remote Settings key that we use to get the search engine
   * configuration overrides.
   *
   * @readonly
   */
  SETTINGS_OVERRIDES_KEY: "search-config-overrides-v2",

  /**
   * Topic used for observer events involving the service itself.
   *
   * @readonly
   */
  TOPIC_SEARCH_SERVICE: "browser-search-service",

  /**
   * Topic used for observer events involving search engines.
   *
   * @readonly
   */
  TOPIC_ENGINE_MODIFIED: "browser-search-engine-modified",

  /**
   * The data sent with ``TOPIC_ENGINE_MODIFIED`` to identify the type of
   * change to the search engine.
   *
   * @readonly
   */
  MODIFIED_TYPE: Object.freeze({
    CHANGED: "engine-changed",
    ICON_CHANGED: "engine-icon-changed",
    REMOVED: "engine-removed",
    ADDED: "engine-added",
    DEFAULT: "engine-default",
    DEFAULT_PRIVATE: "engine-default-private",
  }),

  /**
   * The possible URL types that are used in the search service. Not all of
   * these are available on ``getSubmission``.
   *
   * @readonly
   */
  URL_TYPE: Object.freeze({
    SUGGEST_JSON: "application/x-suggestions+json",
    SEARCH: "text/html",
    OPENSEARCH: "application/opensearchdescription+xml",
    TRENDING_JSON: "application/x-trending+json",
    SEARCH_FORM: "searchform",
    VISUAL_SEARCH: "application/x-visual-search+html",
  }),

  /**
   * An arbitrary cap on the maximum icon size. Without this, large icons can
   * cause big delays when loading them at startup. The unit is the number of
   * bytes.
   *
   * @readonly
   */
  MAX_ICON_SIZE: 20000,

  /**
   * The default character set for search queries.
   *
   * @readonly
   */
  get DEFAULT_QUERY_CHARSET() {
    return "UTF-8";
  },

  LoadListener,

  /**
   * Notifies watchers of SEARCH_ENGINE_TOPIC about changes to an engine or to
   * the state of the search service.
   *
   * @param {SearchEngine} engine
   *   The engine to which the change applies.
   * @param {string} verb
   *   A verb describing the change.
   */
  notifyAction(engine, verb) {
    if (lazy.SearchService.isInitialized) {
      lazy.logConsole.debug("NOTIFY: Engine:", engine.name, "Verb:", verb);
      // @ts-ignore XPConnect will automatically wrap the object, so it does
      // not needs to support nsISupports.
      Services.obs.notifyObservers(engine, this.TOPIC_ENGINE_MODIFIED, verb);
    }
  },

  /**
   * Wrapper function for nsIIOService::newURI.
   *
   * @param {string} urlSpec
   *        The URL string from which to create an nsIURI.
   * @returns {?nsIURI} an nsIURI object, or null if the creation of the URI failed.
   */
  makeURI(urlSpec) {
    try {
      return Services.io.newURI(urlSpec);
    } catch (ex) {}

    return null;
  },

  /**
   * Wrapper function for nsIIOService::newChannel.
   *
   * @param {string|nsIURI} url
   *   The URL string from which to create an nsIChannel.
   * @param {nsContentPolicyType} contentPolicyType
   *   The type of document being loaded.
   * @param {object} [originAttributes]
   *   The origin attributes to associate to this channel.
   * @returns {nsIChannel}
   *   an nsIChannel object, or null if the url is invalid.
   */
  makeChannel(url, contentPolicyType, originAttributes = null) {
    if (!contentPolicyType) {
      throw new Error("makeChannel called with invalid content policy type");
    }
    try {
      let uri = typeof url == "string" ? Services.io.newURI(url) : url;
      let principal;
      if (uri.scheme == "moz-extension") {
        principal = Services.scriptSecurityManager.createContentPrincipal(
          uri,
          {}
        );
      } else {
        if (!originAttributes) {
          originAttributes = {};
          try {
            originAttributes.firstPartyDomain =
              Services.eTLD.getSchemelessSite(uri);
          } catch {}
        }
        principal =
          Services.scriptSecurityManager.createNullPrincipal(originAttributes);
      }

      return Services.io.newChannelFromURI(
        uri,
        null /* loadingNode */,
        principal,
        null /* triggeringPrincipal */,
        Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
        contentPolicyType
      );
    } catch (ex) {}

    return null;
  },

  /**
   * Tests whether this a partner distribution.
   *
   * @returns {boolean}
   *   Whether this is a partner distribution.
   */
  isPartnerBuild() {
    return SearchUtils.distroID && !SearchUtils.distroID.startsWith("mozilla");
  },

  /**
   * Current settings version. This should be incremented if the format of the
   * settings file is modified.
   *
   * @returns {number}
   *   The current settings version.
   */
  get SETTINGS_VERSION() {
    return 13;
  },

  /**
   * Indicates the channel that the build is on, with added hardening for ESR
   * since some ESR builds may be self-built or not on the same channel.
   *
   * @returns {string}
   *   Returns the modified channel, with a focus on ESR if the application
   *   version is indicating ESR.
   */
  get MODIFIED_APP_CHANNEL() {
    return AppConstants.IS_ESR ? "esr" : AppConstants.MOZ_UPDATE_CHANNEL;
  },

  /**
   * Sanitizes a name so that it can be used as an engine name. If it cannot be
   * sanitized (e.g. no valid characters), then it returns a random name.
   *
   * @param {string} name
   *  The name to be sanitized.
   * @returns {string}
   *  The sanitized name.
   */
  sanitizeName(name) {
    const maxLength = 60;
    const minLength = 1;
    var result = name.toLowerCase();
    result = result.replace(/\s+/g, "-");
    result = result.replace(/[^-a-z0-9]/g, "");

    // Use a random name if our input had no valid characters.
    if (result.length < minLength) {
      result = Math.random().toString(36).replace(/^.*\./, "");
    }

    // Force max length.
    return result.substring(0, maxLength);
  },

  getVerificationHash(name, profileDir = PathUtils.profileDir) {
    let disclaimer =
      "By modifying this file, I agree that I am doing so " +
      "only within $appName itself, using official, user-driven search " +
      "engine selection processes, and in a way which does not circumvent " +
      "user consent. I acknowledge that any attempt to change this file " +
      "from outside of $appName is a malicious act, and will be responded " +
      "to accordingly.";

    let salt =
      PathUtils.filename(profileDir) +
      name +
      disclaimer.replace(/\$appName/g, Services.appinfo.name);

    let data = new TextEncoder().encode(salt);
    let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
      Ci.nsICryptoHash
    );
    hasher.init(hasher.SHA256);
    hasher.update(data, data.length);

    return hasher.finish(true);
  },

  /**
   * Tests whether the given URI is a secure OpenSearch submission URI or a
   * secure OpenSearch update URI.
   *
   * Note: We don't want to count something served via localhost as insecure.
   * We also don't want to count sites with .onion as their top-level domain
   * as insecure because .onion URLs actually can't use https and are secured
   * in other ways.
   *
   * @param {nsIURI} uri
   *  The URI to be tested.
   * @returns {boolean}
   *  Whether the URI is secure for OpenSearch purposes.
   */
  isSecureURIForOpenSearch(uri) {
    const loopbackAddresses = ["127.0.0.1", "[::1]", "localhost"];

    return (
      uri.schemeIs("https") ||
      loopbackAddresses.includes(uri.host) ||
      uri.host.toLowerCase().endsWith(".onion")
    );
  },

  /**
   * Sorts engines by the default settings. The sort order is:
   *
   * Application Default Engine
   * Application Private Default Engine (if specified)
   * Engines sorted by orderHint (if specified)
   * Remaining engines in alphabetical order by locale.
   *
   * This is implemented here as it is used in searchengine-devtools as well as
   * the search service.
   *
   * @template {SearchEngine} T
   * @param {object} options
   *   The options for this function.
   * @param {T[]} options.engines
   *   An array of engine objects to sort. These should have the `name` and
   *   `orderHint` fields as top-level properties.
   * @param {SearchEngine} options.appDefaultEngine
   *   The application default engine.
   * @param {SearchEngine} [options.appPrivateDefaultEngine]
   *   The application private default engine, if any.
   * @param {string} [options.locale]
   *   The current application locale, or the locale to use for the sorting.
   * @returns {T[]}
   *   The sorted array of engine objects.
   */
  sortEnginesByDefaults({
    engines,
    appDefaultEngine,
    appPrivateDefaultEngine,
    locale = Services.locale.appLocaleAsBCP47,
  }) {
    /** @type {T[]} */
    const sortedEngines = [];
    /** @type {Set<string>} */
    const addedEngines = new Set();

    function maybeAddEngineToSort(engine) {
      if (!engine || addedEngines.has(engine.name)) {
        return;
      }

      sortedEngines.push(engine);
      addedEngines.add(engine.name);
    }

    // The app default engine should always be first in the list (except
    // for distros, that we should respect).
    const appDefault = appDefaultEngine;
    maybeAddEngineToSort(appDefault);

    // If there's a private default, and it is different to the normal
    // default, then it should be second in the list.
    const appPrivateDefault = appPrivateDefaultEngine;
    if (appPrivateDefault && appPrivateDefault != appDefault) {
      maybeAddEngineToSort(appPrivateDefault);
    }

    const collator = new Intl.Collator(locale);

    let remainingEngines = engines.filter(e => !addedEngines.has(e.name));

    // We sort by highest orderHint first, then alphabetically by name.
    remainingEngines.sort((a, b) => {
      if (a.orderHint && b.orderHint) {
        if (a.orderHint == b.orderHint) {
          return collator.compare(a.name, b.name);
        }
        return b.orderHint - a.orderHint;
      }
      if (a.orderHint) {
        return -1;
      }
      if (b.orderHint) {
        return 1;
      }
      return collator.compare(a.name, b.name);
    });

    return [...sortedEngines, ...remainingEngines];
  },

  /**
   * Chooses the best size out of an array of sizes. If there is no exact match,
   * chooses the next smaller icon if the difference of the preferred size
   * to the larger icon is more than 4 times the difference to the the smaller
   * icon. Otherwise chooses the next larger one.
   *
   * @param {number} preferredSize
   *   The preferred size. Must not be 0.
   * @param {number[]} availableSizes
   *   Array of available sizes. Must not be empty.
   * @returns {number}
   *   The element of availableSizes chosen by the algorithm.
   */
  chooseIconSize(preferredSize, availableSizes) {
    availableSizes = availableSizes.toSorted((a, b) => b - a);
    let bestSize = availableSizes.shift();
    for (let currentSize of availableSizes) {
      if (currentSize >= preferredSize) {
        bestSize = currentSize;
      } else {
        if (
          bestSize > preferredSize &&
          preferredSize - currentSize < (bestSize - preferredSize) / 4
        ) {
          bestSize = currentSize;
        }
        break;
      }
    }

    return bestSize;
  },

  /**
   * Fetches an icon without sending cookies to the page and returns
   * the data and the mime type.
   *
   * @param {string|nsIURI} uri
   *  The URI to the icon.
   * @param {object} [originAttributes]
   *   The origin attributes to download the icon.
   * @returns {Promise<[Uint8Array, string]>}
   *   Resolves to an array containing the data and the mime type.
   *   Rejects if the icon cannot be fetched.
   */
  async fetchIcon(uri, originAttributes = null) {
    return new Promise((resolve, reject) => {
      let chan = SearchUtils.makeChannel(
        uri,
        Ci.nsIContentPolicy.TYPE_IMAGE,
        originAttributes
      );
      let listener = new SearchUtils.LoadListener(
        chan,
        /^image\//,
        (byteArray, contentType) => {
          if (!byteArray) {
            reject(new Error("Unable to fetch icon."));
            return;
          }
          resolve([Uint8Array.from(byteArray), contentType]);
        }
      );
      chan.notificationCallbacks = listener;
      chan.asyncOpen(listener);
    });
  },

  /**
   * Decodes the image to extract the size. Returns `fallbackSize`
   * if the image is not square or there is a decoding error.
   *
   * @param {Uint8Array} byteArray the raw image data
   * @param {string} contentType the contentType
   * @param {?number} fallbackSize fallback if size cannot be determined
   * @returns {?number} the size of the image
   */
  decodeSize(byteArray, contentType, fallbackSize = null) {
    if (contentType == "image/svg+xml") {
      let svgString;
      try {
        svgString = new TextDecoder("UTF-8", { fatal: true }).decode(byteArray);
      } catch {
        return fallbackSize;
      }
      let parser = new DOMParser();
      let doc = parser.parseFromString(svgString, contentType);
      if (doc.querySelector("parsererror")) {
        return fallbackSize;
      }
      if (SVGSVGElement.isInstance(doc.documentElement)) {
        let width = doc.documentElement.width.baseVal.value;
        let height = doc.documentElement.height.baseVal.value;
        if (width != height) {
          return fallbackSize;
        }
        return width;
      }
      return fallbackSize;
    }

    let imageTools = Cc["@mozilla.org/image/tools;1"].getService(Ci.imgITools);
    let imgDecoded;
    try {
      imgDecoded = imageTools.decodeImageFromArrayBuffer(
        byteArray.buffer,
        contentType
      );
    } catch {
      return fallbackSize;
    }
    if (imgDecoded.width != imgDecoded.height) {
      return fallbackSize;
    }

    return imgDecoded.width;
  },

  /**
   * Tries to rescale an icon to a given size.
   *
   * @param {Uint8Array} byteArray
   *   Byte array containing the icon payload.
   * @param {string} contentType
   *   Mime type of the payload.
   * @param {number} [size]
   *   Desired icon size.
   * @returns {[Uint8Array<ArrayBuffer>, string]}
   *   An array of two elements - an array containing the rescaled icon
   *   and a string for the content type.
   * @throws if the icon cannot be rescaled or the rescaled icon is too big.
   */
  rescaleIcon(byteArray, contentType, size = 32) {
    if (contentType == "image/svg+xml") {
      throw new Error("Cannot rescale SVG image");
    }

    let imgTools = Cc["@mozilla.org/image/tools;1"].getService(Ci.imgITools);
    let container = imgTools.decodeImageFromArrayBuffer(
      byteArray.buffer,
      contentType
    );
    let stream = imgTools.encodeScaledImage(container, "image/png", size, size);
    let streamSize = stream.available();
    if (streamSize > SearchUtils.MAX_ICON_SIZE) {
      throw new Error("Rescaled icon still is too big");
    }

    let bis = new BinaryInputStream(stream);
    let newByteArray = new Uint8Array(streamSize);
    bis.readArrayBuffer(streamSize, newByteArray.buffer);
    return [newByteArray, "image/png"];
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  SearchUtils,
  "loggingEnabled",
  BROWSER_SEARCH_PREF + "log",
  false
);

// Can't use defineLazyPreferenceGetter because we want the value
// from the default branch
ChromeUtils.defineLazyGetter(SearchUtils, "distroID", () => {
  return Services.prefs.getDefaultBranch("distribution.").getCharPref("id", "");
});
