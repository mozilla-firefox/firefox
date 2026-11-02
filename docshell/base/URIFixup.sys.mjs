/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This component handles fixing up URIs, by correcting obvious typos and adding
 * missing schemes.
 * URI references:
 *   http://www.faqs.org/rfcs/rfc1738.html
 *   http://www.faqs.org/rfcs/rfc2396.html
 */

// TODO (Bug 1641220) getFixupURIInfo has a complex logic, that likely could be
// simplified, but the risk of regressing its behavior is high.
/* eslint complexity: ["error", 46] */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "externalProtocolService",
  "@mozilla.org/uriloader/external-protocol-service;1",
  Ci.nsIExternalProtocolService
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "defaultProtocolHandler",
  "@mozilla.org/network/protocol;1?name=default",
  Ci.nsIProtocolHandler
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "fileProtocolHandler",
  "@mozilla.org/network/protocol;1?name=file",
  Ci.nsIFileProtocolHandler
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "fixupSchemeTypos",
  "browser.fixup.typo.scheme",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "dnsFirstForSingleWords",
  "browser.fixup.dns_first_for_single_words",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "keywordEnabled",
  "keyword.enabled",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "alternateProtocol",
  "browser.fixup.alternate.protocol",
  "https"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "dnsResolveFullyQualifiedNames",
  "browser.urlbar.dnsResolveFullyQualifiedNames",
  true
);

const {
  FIXUP_FLAG_NONE,
  FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP,
  FIXUP_FLAGS_MAKE_ALTERNATE_URI,
  FIXUP_FLAG_PRIVATE_CONTEXT,
  FIXUP_FLAG_FIX_SCHEME_TYPOS,
  FIXUP_FLAG_FORCE_KEYWORD_LOOKUP,
} = Ci.nsIURIFixup;

const COMMON_PROTOCOLS = ["http", "https", "file"];

const HTTPISH = new Set(["http", "https"]);

// Regex used to identify user:password tokens in url strings.
// This is not a strict valid characters check, because we try to fixup this
// part of the url too.
ChromeUtils.defineLazyGetter(
  lazy,
  "userPasswordRegex",
  () => /^([a-z+.-]+:\/{0,3})*([^\/@]+@).+/i
);

// Regex used to identify the string that starts with port expression.
ChromeUtils.defineLazyGetter(lazy, "portRegex", () => /^:\d{1,5}([?#/]|$)/);

// Regex used to identify numbers.
ChromeUtils.defineLazyGetter(lazy, "numberRegex", () => /^[0-9]+(\.[0-9]+)?$/);

// Regex used to identify tab separated content (having at least 2 tabs).
ChromeUtils.defineLazyGetter(lazy, "maxOneTabRegex", () => /^[^\t]*\t?[^\t]*$/);

// Regex used to test if a string with a protocol might instead be a url
// without a protocol but with a port:
//
//   <hostname>:<port> or
//   <hostname>:<port>/
//
// Where <hostname> is a string of alphanumeric characters and dashes
// separated by dots.
// and <port> is a 5 or less digits. This actually breaks the rfc2396
// definition of a scheme which allows dots in schemes.
//
// Note:
//   People expecting this to work with
//   <user>:<password>@<host>:<port>/<url-path> will be disappointed!
//
// Note: Parser could be a lot tighter, tossing out silly hostnames
//       such as those containing consecutive dots and so on.
ChromeUtils.defineLazyGetter(
  lazy,
  "possiblyHostPortRegex",
  () => /^[a-z0-9-]+(\.[a-z0-9-]+)*:[0-9]{1,5}([/?#]|$)/i
);

// Regex used to strip newlines.
ChromeUtils.defineLazyGetter(lazy, "newLinesRegex", () => /[\r\n]/g);

// Regex used to match a possible protocol.
// This resembles the logic in Services.io.extractScheme, thus \t is admitted
// and stripped later. We don't use Services.io.extractScheme because of
// performance bottleneck caused by crossing XPConnect.
ChromeUtils.defineLazyGetter(
  lazy,
  "possibleProtocolRegex",
  () => /^([a-z][a-z0-9.+\t-]*)(:|;)?(\/\/)?/i
);

// Regex used to match IPs. Note that these are not made to validate IPs, but
// just to detect strings that look like an IP. They also skip protocol.
// For IPv4 this also accepts a shorthand format with just 2 dots.
ChromeUtils.defineLazyGetter(
  lazy,
  "IPv4LikeRegex",
  () => /^(?:[a-z+.-]+:\/*(?!\/))?(?:\d{1,3}\.){2,3}\d{1,3}(?::\d+|\/)?/i
);
ChromeUtils.defineLazyGetter(
  lazy,
  "IPv6LikeRegex",
  () =>
    /^(?:[a-z+.-]+:\/*(?!\/))?\[(?:[0-9a-f]{0,4}:){0,7}[0-9a-f]{0,4}\]?(?::\d+|\/)?/i
);

// Cache of known domains.
ChromeUtils.defineLazyGetter(lazy, "knownDomains", () => {
  const branch = "browser.fixup.domainwhitelist.";
  let domains = new Set(
    Services.prefs
      .getChildList(branch)
      .filter(p => Services.prefs.getBoolPref(p, false))
      .map(p => p.substring(branch.length))
  );
  // Hold onto the observer to avoid it being GC-ed.
  domains._observer = {
    observe(subject, topic, data) {
      let domain = data.substring(branch.length);
      if (Services.prefs.getBoolPref(data, false)) {
        domains.add(domain);
      } else {
        domains.delete(domain);
      }
    },
    QueryInterface: ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]),
  };
  Services.prefs.addObserver(branch, domains._observer, true);
  return domains;
});

// Cache of known suffixes.
// This works differently from the known domains, because when we examine a
// domain we can't tell how many dot-separated parts constitute the suffix.
// We create a Map keyed by the last dotted part, containing a Set of
// all the suffixes ending with that part:
//   "two" => ["two"]
//   "three" => ["some.three", "three"]
// When searching we can restrict the linear scan based on the last part.
// The ideal structure for this would be a Directed Acyclic Word Graph, but
// since we expect this list to be small it's not worth the complication.
ChromeUtils.defineLazyGetter(lazy, "knownSuffixes", () => {
  const branch = "browser.fixup.domainsuffixwhitelist.";
  let suffixes = new Map();
  let prefs = Services.prefs
    .getChildList(branch)
    .filter(p => Services.prefs.getBoolPref(p, false));
  for (let pref of prefs) {
    let suffix = pref.substring(branch.length);
    let lastPart = suffix.substr(suffix.lastIndexOf(".") + 1);
    if (lastPart) {
      let entries = suffixes.get(lastPart);
      if (!entries) {
        entries = new Set();
        suffixes.set(lastPart, entries);
      }
      entries.add(suffix);
    }
  }
  // Hold onto the observer to avoid it being GC-ed.
  suffixes._observer = {
    observe(subject, topic, data) {
      let suffix = data.substring(branch.length);
      let lastPart = suffix.substr(suffix.lastIndexOf(".") + 1);
      let entries = suffixes.get(lastPart);
      if (Services.prefs.getBoolPref(data, false)) {
        // Add the suffix.
        if (!entries) {
          entries = new Set();
          suffixes.set(lastPart, entries);
        }
        entries.add(suffix);
      } else if (entries) {
        // Remove the suffix.
        entries.delete(suffix);
        if (!entries.size) {
          suffixes.delete(lastPart);
        }
      }
    },
    QueryInterface: ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]),
  };
  Services.prefs.addObserver(branch, suffixes._observer, true);
  return suffixes;
});

export function URIFixup() {}

URIFixup.prototype = {
  get FIXUP_FLAG_NONE() {
    return FIXUP_FLAG_NONE;
  },
  get FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP() {
    return FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
  },
  get FIXUP_FLAGS_MAKE_ALTERNATE_URI() {
    return FIXUP_FLAGS_MAKE_ALTERNATE_URI;
  },
  get FIXUP_FLAG_PRIVATE_CONTEXT() {
    return FIXUP_FLAG_PRIVATE_CONTEXT;
  },
  get FIXUP_FLAG_FIX_SCHEME_TYPOS() {
    return FIXUP_FLAG_FIX_SCHEME_TYPOS;
  },

  getFixupURIInfo(uriString, fixupFlags = FIXUP_FLAG_NONE) {
    let isPrivateContext = fixupFlags & FIXUP_FLAG_PRIVATE_CONTEXT;
    let untrimmedURIString = uriString;

    // Eliminate embedded newlines, which single-line text fields now allow,
    // and cleanup the empty spaces and tabs that might be on each end.
    uriString = uriString.trim().replace(lazy.newLinesRegex, "");

    if (!uriString) {
      throw new Components.Exception(
        "Should pass a non-null uri",
        Cr.NS_ERROR_FAILURE
      );
    }

    let info = new URIFixupInfo(uriString);

    const { scheme, fixedSchemeUriString, fixupChangedProtocol } =
      extractScheme(uriString, fixupFlags);
    uriString = fixedSchemeUriString;
    info.fixupChangedProtocol = fixupChangedProtocol;

    if (scheme == "view-source") {
      let { preferredURI, postData } = fixupViewSource(uriString, fixupFlags);
      info.preferredURI = info.fixedURI = preferredURI;
      info.postData = postData;
      return info;
    }

    if (scheme.length < 2) {
      // Check if it is a file path. We skip most schemes because the only case
      // where a file path may look like having a scheme is "X:" on Windows.
      let fileURI = fileURIFixup(uriString);
      if (fileURI) {
        info.preferredURI = info.fixedURI = fileURI;
        info.fixupChangedProtocol = true;
        return info;
      }
    }

    const isCommonProtocol = COMMON_PROTOCOLS.includes(scheme);

    let canHandleProtocol =
      scheme &&
      (isCommonProtocol ||
        Services.io.getProtocolHandler(scheme) != lazy.defaultProtocolHandler ||
        this._isKnownExternalProtocol(scheme));

    if (
      canHandleProtocol ||
      // If it's an unknown handler and the given URL looks like host:port or
      // has a user:password we can't pass it to the external protocol handler.
      // We'll instead try fixing it with http later.
      (!lazy.possiblyHostPortRegex.test(uriString) &&
        !lazy.userPasswordRegex.test(uriString))
    ) {
      // Just try to create a URL out of it.
      try {
        info.fixedURI = makeURIWithFixedLocalHosts(uriString, fixupFlags);
      } catch (ex) {
        if (ex.result != Cr.NS_ERROR_MALFORMED_URI) {
          throw ex;
        }
      }
    }

    // We're dealing with a theoretically valid URI but we have no idea how to
    // load it. (e.g. "christmas:humbug")
    // It's more likely the user wants to search, and so we chuck this over to
    // their preferred search provider.
    // TODO (Bug 1588118): Should check FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP
    // instead of FIXUP_FLAG_FIX_SCHEME_TYPOS.
    if (
      info.fixedURI &&
      keywordFixupEnabled(fixupFlags) &&
      fixupFlags & FIXUP_FLAG_FIX_SCHEME_TYPOS &&
      scheme &&
      !canHandleProtocol
    ) {
      tryKeywordFixupForURIInfo(uriString, info, isPrivateContext);
    }

    if (info.fixedURI) {
      if (!info.preferredURI) {
        maybeSetAlternateFixedURI(info, fixupFlags);
        info.preferredURI = info.fixedURI;
      }
      fixupConsecutiveDotsHost(info);
      return info;
    }

    // Fix up protocol string before calling KeywordURIFixup, because
    // it cares about the hostname of such URIs.
    // Prune duff protocol schemes:
    //   ://totallybroken.url.com
    //   //shorthand.url.com
    let inputHadDuffProtocol =
      uriString.startsWith("://") || uriString.startsWith("//");
    if (inputHadDuffProtocol) {
      uriString = uriString.replace(/^:?\/\//, "");
    }

    let detectSpaceInCredentials = val => {
      // Only search the first 512 chars for performance reasons.
      let firstChars = val.slice(0, 512);
      if (!firstChars.includes("@")) {
        return false;
      }
      let credentials = firstChars.split("@")[0];
      return !credentials.includes("/") && /\s/.test(credentials);
    };

    // Avoid fixing up content that looks like tab-separated values.
    // Assume that 1 tab is accidental, but more than 1 implies this is
    // supposed to be tab-separated content.
    if (
      !isCommonProtocol &&
      lazy.maxOneTabRegex.test(uriString) &&
      !detectSpaceInCredentials(untrimmedURIString)
    ) {
      let uriWithProtocol = fixupURIProtocol(uriString, fixupFlags);
      if (uriWithProtocol) {
        info.fixedURI = uriWithProtocol;
        info.fixupChangedProtocol = true;
        info.schemelessInput = Ci.nsILoadInfo.SchemelessInputTypeSchemeless;
        maybeSetAlternateFixedURI(info, fixupFlags);
        info.preferredURI = info.fixedURI;
        // Check if it's a forced visit. The user can enforce a visit by
        // appending a slash, but the string must be in a valid uri format.
        if (uriString.endsWith("/")) {
          fixupConsecutiveDotsHost(info);
          return info;
        }
        // " and * are accepted by the URL parser but are not valid DNS name
        // characters. When keyword search is available, prefer it over
        // attempting a connection that will fail at DNS resolution.
        if (
          keywordLookupRequested(fixupFlags) &&
          /["*]/.test(info.fixedURI.host) &&
          tryKeywordFixupForURIInfo(info.originalInput, info, isPrivateContext)
        ) {
          fixupConsecutiveDotsHost(info);
          return info;
        }
      }
    }

    // Handle "www.<something>" as a URI.
    if (isWwwOnlyHostname(info.fixedURI?.asciiHost)) {
      return info;
    }

    // Memoize the public suffix check, since it may be expensive and should
    // only run once when necessary.
    let suffixInfo;
    function checkSuffix(i) {
      if (!suffixInfo) {
        suffixInfo = checkAndFixPublicSuffix(i);
      }
      return suffixInfo;
    }

    // See if it is a keyword and whether a keyword must be fixed up.
    if (
      keywordLookupRequested(fixupFlags) &&
      !inputHadDuffProtocol &&
      !checkSuffix(info).suffix &&
      keywordURIFixup(uriString, info, isPrivateContext)
    ) {
      fixupConsecutiveDotsHost(info);
      return info;
    }

    if (
      info.fixedURI &&
      (!info.fixupChangedProtocol || !checkSuffix(info).hasUnknownSuffix)
    ) {
      fixupConsecutiveDotsHost(info);
      return info;
    }

    // If we still haven't been able to construct a valid URI, try to force a
    // keyword match.
    if (keywordLookupRequested(fixupFlags)) {
      tryKeywordFixupForURIInfo(info.originalInput, info, isPrivateContext);
    }

    if (!info.preferredURI) {
      // We couldn't salvage anything.
      throw new Components.Exception(
        "Couldn't build a valid uri",
        Cr.NS_ERROR_MALFORMED_URI
      );
    }

    fixupConsecutiveDotsHost(info);
    return info;
  },

  webNavigationFlagsToFixupFlags(href, navigationFlags) {
    try {
      Services.io.newURI(href);
      // Remove LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP for valid uris.
      navigationFlags &=
        ~Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
    } catch (ex) {}

    let fixupFlags = FIXUP_FLAG_NONE;
    if (
      navigationFlags & Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP
    ) {
      fixupFlags |= FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
    }
    if (navigationFlags & Ci.nsIWebNavigation.LOAD_FLAGS_FIXUP_SCHEME_TYPOS) {
      fixupFlags |= FIXUP_FLAG_FIX_SCHEME_TYPOS;
    }
    return fixupFlags;
  },

  keywordToURI(keyword, isPrivateContext) {
    if (Services.appinfo.processType == Ci.nsIXULRuntime.PROCESS_TYPE_CONTENT) {
      // There's no search service in the content process, thus all the calls
      // from it that care about keywords conversion should go through the
      // parent process.
      throw new Components.Exception(
        "Can't invoke URIFixup in the content process",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }
    let info = new URIFixupInfo(keyword);

    // Strip leading "?" and leading/trailing spaces from aKeyword
    if (keyword.startsWith("?")) {
      keyword = keyword.substring(1);
    }
    keyword = keyword.trim();

    if (!lazy.SearchService.hasSuccessfullyInitialized) {
      return info;
    }

    // Try falling back to the search service's default search engine
    // We must use an appropriate search engine depending on the private
    // context.
    let engine = isPrivateContext
      ? lazy.SearchService.defaultPrivateEngine
      : lazy.SearchService.defaultEngine;

    // We allow default search plugins to specify alternate parameters that are
    // specific to keyword searches.
    let responseType = null;
    if (engine.supportsResponseType("application/x-moz-keywordsearch")) {
      responseType = "application/x-moz-keywordsearch";
    }
    let submission = engine.getSubmission(keyword, responseType, "urlbar");
    if (
      !submission ||
      // For security reasons (avoid redirecting to file, data, or other unsafe
      // protocols) we only allow fixup to http/https search engines.
      !HTTPISH.has(submission.uri.scheme)
    ) {
      throw new Components.Exception(
        "Invalid search submission uri",
        Cr.NS_ERROR_NOT_AVAILABLE
      );
    }
    let submissionPostDataStream = submission.postData;
    if (submissionPostDataStream) {
      info.postData = submissionPostDataStream;
    }

    info.keywordProviderId = engine.id;
    info.keywordAsSent = keyword;
    info.keywordPartnerCode = submission.partnerCode;
    info.keywordTelemetryId = submission.telemetryId;
    info.preferredURI = submission.uri;
    return info;
  },

  forceHttpFixup(uriString) {
    if (!uriString) {
      throw new Components.Exception(
        "Should pass a non-null uri",
        Cr.NS_ERROR_FAILURE
      );
    }

    let info = new URIFixupInfo(uriString);
    let { scheme, fixedSchemeUriString, fixupChangedProtocol } = extractScheme(
      uriString,
      FIXUP_FLAG_FIX_SCHEME_TYPOS
    );

    if (!HTTPISH.has(scheme)) {
      throw new Components.Exception(
        "Scheme should be either http or https",
        Cr.NS_ERROR_FAILURE
      );
    }

    info.fixupChangedProtocol = fixupChangedProtocol;
    info.fixedURI = Services.io.newURI(fixedSchemeUriString);

    let host = info.fixedURI.host;
    if (!HTTPISH.has(host) && host != "localhost") {
      let modifiedHostname = maybeAddPrefixAndSuffix(host);
      updateHostAndScheme(info, modifiedHostname);
      info.preferredURI = info.fixedURI;
    }

    return info;
  },

  checkHost(uri, listener, originAttributes) {
    let { displayHost, asciiHost } = uri;
    if (!displayHost) {
      throw new Components.Exception(
        "URI must have displayHost",
        Cr.NS_ERROR_FAILURE
      );
    }
    if (!asciiHost) {
      throw new Components.Exception(
        "URI must have asciiHost",
        Cr.NS_ERROR_FAILURE
      );
    }

    let isIPv4Address = host => {
      let parts = host.split(".");
      if (parts.length != 4) {
        return false;
      }
      return parts.every(part => {
        let n = parseInt(part, 10);
        return n >= 0 && n <= 255;
      });
    };

    // Avoid showing fixup information if we're suggesting an IP. Note that
    // decimal representations of IPs are normalized to a 'regular'
    // dot-separated IP address by network code, but that only happens for
    // numbers that don't overflow. Longer numbers do not get normalized,
    // but still work to access IP addresses. So for instance,
    // 1097347366913 (ff7f000001) gets resolved by using the final bytes,
    // making it the same as 7f000001, which is 127.0.0.1 aka localhost.
    // While 2130706433 would get normalized by network, 1097347366913
    // does not, and we have to deal with both cases here:
    if (isIPv4Address(asciiHost) || /^(?:\d+|0x[a-f0-9]+)$/i.test(asciiHost)) {
      return;
    }

    // For dotless hostnames, we want to ensure this ends with a '.' but don't
    // want the . showing up in the UI if we end up notifying the user, so we
    // use a separate variable.
    let lookupName = displayHost;
    if (lazy.dnsResolveFullyQualifiedNames && !lookupName.includes(".")) {
      lookupName += ".";
    }

    Services.obs.notifyObservers(null, "uri-fixup-check-dns");
    Services.dns.asyncResolve(
      lookupName,
      Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
      0,
      null,
      listener,
      Services.tm.mainThread,
      originAttributes
    );
  },

  isDomainKnown,

  _isKnownExternalProtocol(scheme) {
    if (AppConstants.platform == "android") {
      // On Android, externalProtocolHandlerExists ~always returns true (see
      // nsOSHelperAppService::OSProtocolHandlerExists). For now, this
      // preserves behavior from before bug 1966666.
      return false;
    }
    return lazy.externalProtocolService.externalProtocolHandlerExists(scheme);
  },

  classID: Components.ID("{c6cf88b7-452e-47eb-bdc9-86e3561648ef}"),
  QueryInterface: ChromeUtils.generateQI(["nsIURIFixup"]),
};

export function URIFixupInfo(originalInput = "") {
  this._originalInput = originalInput;
}

URIFixupInfo.prototype = {
  set consumer(consumer) {
    this._consumer = consumer || null;
  },
  get consumer() {
    return this._consumer || null;
  },

  set preferredURI(uri) {
    this._preferredURI = uri;
  },
  get preferredURI() {
    return this._preferredURI || null;
  },

  set fixedURI(uri) {
    this._fixedURI = uri;
  },
  get fixedURI() {
    return this._fixedURI || null;
  },

  set keywordProviderId(id) {
    this._keywordProviderId = id;
  },
  get keywordProviderId() {
    return this._keywordProviderId || "";
  },

  set keywordAsSent(keyword) {
    this._keywordAsSent = keyword;
  },
  get keywordAsSent() {
    return this._keywordAsSent || "";
  },

  set keywordPartnerCode(code) {
    this._keywordPartnerCode = code;
  },
  get keywordPartnerCode() {
    return this._keywordPartnerCode || "";
  },

  set keywordTelemetryId(id) {
    this._keywordTelemetryId = id;
  },
  get keywordTelemetryId() {
    return this._keywordTelemetryId || "";
  },

  set schemelessInput(changed) {
    this._schemelessInput = changed;
  },
  get schemelessInput() {
    return this._schemelessInput ?? Ci.nsILoadInfo.SchemelessInputTypeUnset;
  },

  set fixupChangedProtocol(changed) {
    this._fixupChangedProtocol = changed;
  },
  get fixupChangedProtocol() {
    return !!this._fixupChangedProtocol;
  },

  set fixupCreatedAlternateURI(changed) {
    this._fixupCreatedAlternateURI = changed;
  },
  get fixupCreatedAlternateURI() {
    return !!this._fixupCreatedAlternateURI;
  },

  set originalInput(input) {
    this._originalInput = input;
  },
  get originalInput() {
    return this._originalInput || "";
  },

  set postData(postData) {
    this._postData = postData;
  },
  get postData() {
    return this._postData || null;
  },

  classID: Components.ID("{33d75835-722f-42c0-89cc-44f328e56a86}"),
  QueryInterface: ChromeUtils.generateQI(["nsIURIFixupInfo"]),
};

// Helpers

/**
 * Implementation of isDomainKnown, so we don't have to go through the
 * service.
 *
 * @param {string} asciiHost
 * @returns {boolean} whether the domain is known
 */
function isDomainKnown(asciiHost) {
  if (lazy.dnsFirstForSingleWords) {
    return true;
  }
  // Check if this domain is known as an actual
  // domain (which will prevent a keyword query)
  // Note that any processing of the host here should stay in sync with
  // code in the front-end(s) that set the pref.
  let lastDotIndex = asciiHost.lastIndexOf(".");
  if (lastDotIndex == asciiHost.length - 1) {
    asciiHost = asciiHost.substring(0, asciiHost.length - 1);
    lastDotIndex = asciiHost.lastIndexOf(".");
  }
  if (lazy.knownDomains.has(asciiHost.toLowerCase())) {
    return true;
  }
  // If there's no dot or only a leading dot we are done, otherwise we'll check
  // against the known suffixes.
  if (lastDotIndex <= 0) {
    return false;
  }
  // Don't use getPublicSuffix here, since the suffix is not in the PSL,
  // thus it couldn't tell if the suffix is made up of one or multiple
  // dot-separated parts.
  let lastPart = asciiHost.substr(lastDotIndex + 1);
  let suffixes = lazy.knownSuffixes.get(lastPart);
  if (suffixes) {
    return Array.from(suffixes).some(s => asciiHost.endsWith(s));
  }
  return false;
}

/**
 * Checks the suffix of ``info.fixedURI`` against the Public Suffix List.
 * If the suffix is unknown due to a typo this will try to fix it up in-place,
 * by modifying the public suffix of ``info.fixedURI``.
 *
 * @param {URIFixupInfo} info about the uri to check.
 * @returns {object} result The lookup result.
 * @returns {string} result.suffix The public suffix if one can be identified.
 * @returns {boolean} result.hasUnknownSuffix True when the suffix is not in the
 *     Public Suffix List and it's not in knownSuffixes. False in the other cases.
 */
function checkAndFixPublicSuffix(info) {
  let uri = info.fixedURI;
  let asciiHost = uri?.asciiHost;

  // If the original input ends in a "。" character (U+3002), we consider the
  // input a search query if there is no valid suffix.
  // While the "。" character is equivalent to a period in domains, it's more
  // commonly used to terminate search phrases. We're preserving the historical
  // behavior of the ascii period for now, as that may be more commonly expected
  // by technical users.
  if (
    !asciiHost ||
    !asciiHost.includes(".") ||
    (asciiHost.endsWith(".") && !info.originalInput.endsWith("。")) ||
    isDomainKnown(asciiHost)
  ) {
    return { suffix: "", hasUnknownSuffix: false };
  }

  // Quick bailouts for most common cases, according to Alexa Top 1 million.
  if (
    /^\w/.test(asciiHost) &&
    (asciiHost.endsWith(".com") ||
      asciiHost.endsWith(".net") ||
      asciiHost.endsWith(".org") ||
      asciiHost.endsWith(".ru") ||
      asciiHost.endsWith(".de"))
  ) {
    return {
      suffix: asciiHost.substring(asciiHost.lastIndexOf(".") + 1),
      hasUnknownSuffix: false,
    };
  }
  try {
    let suffix = Services.eTLD.getKnownPublicSuffix(uri);
    if (suffix) {
      return { suffix, hasUnknownSuffix: false };
    }
  } catch (ex) {
    return { suffix: "", hasUnknownSuffix: false };
  }
  // Suffix is unknown, try to fix most common 3 chars TLDs typos.
  // .com is the most commonly mistyped tld, so it has more cases.
  let suffix = Services.eTLD.getPublicSuffix(uri);
  if (!suffix || lazy.numberRegex.test(suffix)) {
    return { suffix: "", hasUnknownSuffix: false };
  }
  for (let [typo, fixed] of [
    ["ocm", "com"],
    ["con", "com"],
    ["cmo", "com"],
    ["xom", "com"],
    ["vom", "com"],
    ["cpm", "com"],
    ["com'", "com"],
    ["ent", "net"],
    ["ner", "net"],
    ["nte", "net"],
    ["met", "net"],
    ["rog", "org"],
    ["ogr", "org"],
    ["prg", "org"],
    ["orh", "org"],
  ]) {
    if (suffix == typo) {
      let host = uri.host.substring(0, uri.host.length - typo.length) + fixed;
      let updatePreferredURI = info.preferredURI == info.fixedURI;
      info.fixedURI = uri.mutate().setHost(host).finalize();
      if (updatePreferredURI) {
        info.preferredURI = info.fixedURI;
      }
      return { suffix: fixed, hasUnknownSuffix: false };
    }
  }
  return { suffix: "", hasUnknownSuffix: true };
}

function tryKeywordFixupForURIInfo(uriString, fixupInfo, isPrivateContext) {
  try {
    let keywordInfo = Services.uriFixup.keywordToURI(
      uriString,
      isPrivateContext
    );
    fixupInfo.keywordProviderId = keywordInfo.keywordProviderId;
    fixupInfo.keywordAsSent = keywordInfo.keywordAsSent;
    fixupInfo.keywordPartnerCode = keywordInfo.keywordPartnerCode;
    fixupInfo.keywordTelemetryId = keywordInfo.keywordTelemetryId;
    fixupInfo.preferredURI = keywordInfo.preferredURI;
    return true;
  } catch (ex) {}
  return false;
}

/**
 * This generates an alternate fixedURI, by adding a prefix and a suffix to
 * the fixedURI host, if and only if the protocol is http. It should _never_
 * modify URIs with other protocols.
 *
 * @param {URIFixupInfo} info an URIInfo object
 * @param {integer} fixupFlags the fixup flags
 * @returns {boolean} Whether an alternate uri was generated
 */
function maybeSetAlternateFixedURI(info, fixupFlags) {
  let uri = info.fixedURI;
  if (
    !(fixupFlags & FIXUP_FLAGS_MAKE_ALTERNATE_URI) ||
    // Code only works for http. Not for any other protocol including https!
    !uri.schemeIs("http") ||
    // Security - URLs with user / password info should NOT be fixed up
    uri.userPass ||
    // Don't fix up hosts with ports
    uri.port != -1
  ) {
    return false;
  }

  let oldHost = uri.host;
  // Don't create an alternate uri for localhost, because it would be confusing.
  // Ditto for 'http' and 'https' as these are frequently the result of typos, e.g.
  // 'https//foo' (note missing : ).
  if (oldHost == "localhost" || HTTPISH.has(oldHost)) {
    return false;
  }

  // Get the prefix and suffix to stick onto the new hostname. By default these
  // are www. & .com but they could be any other value, e.g. www. & .org
  let newHost = maybeAddPrefixAndSuffix(oldHost);

  if (newHost == oldHost) {
    return false;
  }

  return updateHostAndScheme(info, newHost);
}

/**
 * Try to fixup a file URI.
 *
 * @param {string} uriString The file URI to fix.
 * @returns {?nsIURI} a fixed uri if it has to add the file: protocol or null.
 */
function fileURIFixup(uriString) {
  let attemptFixup = false;
  let path = uriString;
  if (AppConstants.platform == "win") {
    // Check for "\"" in the url-string, just a drive (e.g. C:),
    // or 'A:/...' where the "protocol" is also a single letter.
    attemptFixup =
      uriString.includes("\\") ||
      (uriString[1] == ":" && (uriString.length == 2 || uriString[2] == "/"));
    if (uriString[1] == ":" && uriString[2] == "/") {
      path = uriString.replace(/\//g, "\\");
    }
  } else {
    // UNIX: Check if it starts with "/" or "~".
    attemptFixup = /^[~/]/.test(uriString);
  }
  if (attemptFixup) {
    try {
      // Test if this is a valid path by trying to create a local file
      // object. The URL of that is returned if successful.
      let file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      file.initWithPath(path);
      return Services.io.newURI(
        lazy.fileProtocolHandler.getURLSpecFromActualFile(file)
      );
    } catch (ex) {
      // Not a file uri.
    }
  }
  return null;
}

/**
 * Tries to fixup a string to an nsIURI by adding the default protocol.
 *
 * Should fix things like:
 *    no-scheme.com
 *    ftp.no-scheme.com
 *    ftp4.no-scheme.com
 *    no-scheme.com/query?foo=http://www.foo.com
 *    user:pass@no-scheme.com
 *
 * @param {string} uriString The string to fixup.
 * @param {number} fixupFlags The fixup flags to use.
 * @returns {nsIURI} an nsIURI built adding the default protocol to the string,
 *          or null if fixing was not possible.
 */
function fixupURIProtocol(uriString, fixupFlags) {
  // The longest URI scheme on the IANA list is 36 chars + 3 for ://
  let schemeChars = uriString.slice(0, 39);

  let schemePos = schemeChars.indexOf("://");
  if (schemePos == -1 || schemePos > schemeChars.search(/[:\/]/)) {
    uriString = "http://" + uriString;
  }
  try {
    return makeURIWithFixedLocalHosts(uriString, fixupFlags);
  } catch (ex) {
    // We generated an invalid uri.
  }
  return null;
}

/**
 * A thin wrapper around `newURI` that fixes up the host if it's
 * 0.0.0.0 or ::, which are no longer valid. Aims to facilitate
 * user typos and/or "broken" links output by commandline tools.
 *
 * @param {string} uriString The string to make into a URI.
 * @param {number} fixupFlags The fixup flags to use.
 * @throws NS_ERROR_MALFORMED_URI if the uri is invalid.
 */
function makeURIWithFixedLocalHosts(uriString, fixupFlags) {
  let uri = Services.io.newURI(uriString);

  // We only want to fix up 0.0.0.0 if the URL came from the user, either
  // from the address bar or as a commandline argument (ie clicking links
  // in other applications, terminal, etc.). We can't use
  // FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP for this as that isn't normally allowed
  // for external links, and the other flags are sometimes used for
  // web-provided content. So we cheat and use the scheme typo flag.
  if (fixupFlags & FIXUP_FLAG_FIX_SCHEME_TYPOS && HTTPISH.has(uri.scheme)) {
    if (uri.host == "0.0.0.0") {
      uri = uri.mutate().setHost("127.0.0.1").finalize();
    } else if (uri.host == "::") {
      uri = uri.mutate().setHost("[::1]").finalize();
    }
  }
  return uri;
}

function keywordLookupRequested(fixupFlags) {
  return (
    keywordFixupEnabled(fixupFlags) &&
    !!(
      fixupFlags &
      (FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP | FIXUP_FLAG_FORCE_KEYWORD_LOOKUP)
    )
  );
}

function isWwwOnlyHostname(host) {
  return (
    host?.length > 4 && host.startsWith("www.") && host.lastIndexOf(".") == 3
  );
}

/**
 * Whether keyword lookup is enabled, either by the keyword.enabled pref or
 * because it's forced via fixup flags.
 *
 * @param {number} fixupFlags The fixup flags.
 * @returns {boolean} Whether keyword lookup is enabled.
 */
function keywordFixupEnabled(fixupFlags) {
  return (
    lazy.keywordEnabled || !!(fixupFlags & FIXUP_FLAG_FORCE_KEYWORD_LOOKUP)
  );
}

/**
 * Tries to fixup a string to a search url.
 *
 * @param {string} uriString the string to fixup.
 * @param {URIFixupInfo} fixupInfo The fixup info object, modified in-place.
 * @param {boolean} isPrivateContext Whether this happens in a private context.
 * @param {nsIInputStream} postData optional POST data for the search
 * @returns {boolean} Whether the keyword fixup was succesful.
 */
function keywordURIFixup(uriString, fixupInfo, isPrivateContext) {
  // Here is a few examples of strings that should be searched:
  // "what is mozilla"
  // "what is mozilla?"
  // "docshell site:mozilla.org" - has a space in the origin part
  // "?site:mozilla.org - anything that begins with a question mark
  // "mozilla'.org" - Things that have a quote before the first dot/colon
  // "mozilla/test" - unknown host
  // ".mozilla", "mozilla." - starts or ends with a dot ()
  // "user@nonQualifiedHost"

  // These other strings should not be searched, because they could be URIs:
  // "www.blah.com" - Domain with a standard or known suffix
  // "knowndomain" - known domain
  // "nonQualifiedHost:8888?something" - has a port
  // "user:pass@nonQualifiedHost"
  // "blah.com."

  // We do keyword lookups if the input starts with a question mark.
  if (uriString.startsWith("?")) {
    return tryKeywordFixupForURIInfo(
      fixupInfo.originalInput,
      fixupInfo,
      isPrivateContext
    );
  }

  // Check for IPs.
  const userPassword = lazy.userPasswordRegex.exec(uriString);
  const ipString = userPassword
    ? uriString.replace(userPassword[2], "")
    : uriString;
  if (lazy.IPv4LikeRegex.test(ipString) || lazy.IPv6LikeRegex.test(ipString)) {
    return false;
  }

  // Avoid keyword lookup if we can identify a host and it's known, or ends
  // with a dot and has some path.
  // Note that if dnsFirstForSingleWords is true isDomainKnown will always
  // return true, so we can avoid checking dnsFirstForSingleWords after this.
  let asciiHost = fixupInfo.fixedURI?.asciiHost;
  if (
    asciiHost &&
    (isDomainKnown(asciiHost) ||
      (asciiHost.endsWith(".") &&
        asciiHost.indexOf(".") != asciiHost.length - 1))
  ) {
    return false;
  }

  // Avoid keyword lookup if the url seems to have password.
  if (fixupInfo.fixedURI?.password) {
    return false;
  }

  // Even if the host is unknown, avoid keyword lookup if the string has
  // uri-like characteristics, unless it looks like "user@unknownHost".
  // Note we already excluded passwords at this point.
  if (
    !isURILike(uriString, fixupInfo.fixedURI?.displayHost) ||
    (fixupInfo.fixedURI?.userPass && fixupInfo.fixedURI?.pathQueryRef === "/")
  ) {
    return tryKeywordFixupForURIInfo(
      fixupInfo.originalInput,
      fixupInfo,
      isPrivateContext
    );
  }

  return false;
}

/**
 * Mimics the logic in Services.io.extractScheme, but avoids crossing XPConnect.
 * This also tries to fixup the scheme if it was clearly mistyped.
 *
 * @param {string} uriString the string to examine
 * @param {integer} fixupFlags The original fixup flags
 * @returns {object}
 *          scheme: a typo fixed scheme or empty string if one could not be identified
 *          fixedSchemeUriString: uri string with a typo fixed scheme
 *          fixupChangedProtocol: true if the scheme is fixed up
 */
function extractScheme(uriString, fixupFlags = FIXUP_FLAG_NONE) {
  const matches = uriString.match(lazy.possibleProtocolRegex);
  const hasColon = matches?.[2] === ":";
  const hasSlash2 = matches?.[3] === "//";

  const isFixupSchemeTypos =
    lazy.fixupSchemeTypos && fixupFlags & FIXUP_FLAG_FIX_SCHEME_TYPOS;

  if (
    !matches ||
    (!hasColon && !hasSlash2) ||
    (!hasColon && !isFixupSchemeTypos)
  ) {
    return {
      scheme: "",
      fixedSchemeUriString: uriString,
      fixupChangedProtocol: false,
    };
  }

  let scheme = matches[1].replace("\t", "").toLowerCase();
  let fixedSchemeUriString = uriString;

  if (isFixupSchemeTypos && hasSlash2) {
    // Fix up typos for string that user would have intented as protocol.
    const afterProtocol = uriString.substring(matches[0].length);
    fixedSchemeUriString = `${scheme}://${afterProtocol}`;
  }

  let fixupChangedProtocol = false;

  if (isFixupSchemeTypos) {
    // Fix up common scheme typos.
    // TODO: Use levenshtein distance here?
    fixupChangedProtocol = [
      ["ttp", "http"],
      ["htp", "http"],
      ["ttps", "https"],
      ["tps", "https"],
      ["ps", "https"],
      ["htps", "https"],
      ["ile", "file"],
      ["le", "file"],
    ].some(([typo, fixed]) => {
      if (scheme === typo) {
        scheme = fixed;
        fixedSchemeUriString =
          scheme + fixedSchemeUriString.substring(typo.length);
        return true;
      }
      return false;
    });
  }

  return {
    scheme,
    fixedSchemeUriString,
    fixupChangedProtocol,
  };
}

/**
 * View-source is a pseudo scheme. We're interested in fixing up the stuff
 * after it. The easiest way to do that is to call this method again with
 * the "view-source:" lopped off and then prepend it again afterwards.
 *
 * @param {string} uriString The original string to fixup
 * @param {integer} fixupFlags The original fixup flags
 * @param {nsIInputStream} postData Optional POST data for the search
 * @returns {object} {preferredURI, postData} The fixed URI and relative postData
 * @throws if it's not possible to fixup the url
 */
function fixupViewSource(uriString, fixupFlags) {
  // We disable keyword lookup and alternate URIs so that small typos don't
  // cause us to look at very different domains.
  let newFixupFlags =
    fixupFlags &
    ~(FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP | FIXUP_FLAG_FORCE_KEYWORD_LOOKUP);
  let innerURIString = uriString.substring(12).trim();

  // Prevent recursion.
  const { scheme: innerScheme } = extractScheme(innerURIString);
  if (innerScheme == "view-source") {
    throw new Components.Exception(
      "Prevent view-source recursion",
      Cr.NS_ERROR_FAILURE
    );
  }

  let info = Services.uriFixup.getFixupURIInfo(innerURIString, newFixupFlags);
  if (!info.preferredURI) {
    throw new Components.Exception(
      "Couldn't build a valid uri",
      Cr.NS_ERROR_MALFORMED_URI
    );
  }
  return {
    preferredURI: Services.io.newURI("view-source:" + info.preferredURI.spec),
    postData: info.postData,
  };
}

/**
 * Fixup the host of fixedURI if it contains consecutive dots.
 *
 * @param {URIFixupInfo} info an URIInfo object
 */
function fixupConsecutiveDotsHost(fixupInfo) {
  const uri = fixupInfo.fixedURI;

  try {
    if (!uri?.host.includes("..")) {
      return;
    }
  } catch (e) {
    return;
  }

  try {
    const isPreferredEqualsToFixed = fixupInfo.preferredURI?.equals(uri);

    fixupInfo.fixedURI = uri
      .mutate()
      .setHost(uri.host.replace(/\.+/g, "."))
      .finalize();

    if (isPreferredEqualsToFixed) {
      fixupInfo.preferredURI = fixupInfo.fixedURI;
    }
  } catch (e) {
    if (e.result !== Cr.NS_ERROR_MALFORMED_URI) {
      throw e;
    }
  }
}

/**
 * Return whether or not given string is uri like.
 * This function returns true like following strings.
 * - ":8080"
 * - "localhost:8080" (if given host is "localhost")
 * - "/foo?bar"
 * - "/foo#bar"
 *
 * @param {string} uriString.
 * @param {string} host.
 * @param {boolean} true if uri like.
 */
function isURILike(uriString, host) {
  const indexOfSlash = uriString.indexOf("/");
  if (
    indexOfSlash >= 0 &&
    (indexOfSlash < uriString.indexOf("?", indexOfSlash) ||
      indexOfSlash < uriString.indexOf("#", indexOfSlash))
  ) {
    return true;
  }

  if (uriString.startsWith(host)) {
    uriString = uriString.substring(host.length);
  }

  return lazy.portRegex.test(uriString);
}

/**
 * Add prefix and suffix to a hostname if both are missing.
 *
 * If the host does not start with the prefix, add the prefix to
 * the hostname.
 *
 * By default the prefix and suffix are www. and .com but they could
 * be any value e.g. www. and .org as they use the preferences
 * "browser.fixup.alternate.prefix" and "browser.fixup.alternative.suffix"
 *
 * If no changes were made, it returns an empty string.
 *
 * @param {string} oldHost.
 * @return {string} Fixed up hostname or an empty string.
 */
function maybeAddPrefixAndSuffix(oldHost) {
  let prefix = Services.prefs.getCharPref(
    "browser.fixup.alternate.prefix",
    "www."
  );
  let suffix = Services.locale.urlFixupSuffix;
  let newHost = "";
  let numDots = (oldHost.match(/\./g) || []).length;
  if (numDots == 0) {
    newHost = prefix + oldHost + suffix;
  } else if (numDots == 1) {
    if (prefix && oldHost == prefix) {
      newHost = oldHost + suffix;
    } else if (suffix && !oldHost.startsWith(prefix)) {
      newHost = prefix + oldHost;
    }
  }
  return newHost ? newHost : oldHost;
}

/**
 * Given an instance of URIFixupInfo, update its fixedURI.
 *
 * First, change the protocol to the one stored in
 * "browser.fixup.alternate.protocol".
 *
 * Then, try to update fixedURI's host to newHost.
 *
 * @param {URIFixupInfo} info.
 * @param {string} newHost.
 * @return {boolean}
 *          True, if info was updated without any errors.
 *          False, if NS_ERROR_MALFORMED_URI error.
 * @throws If a non-NS_ERROR_MALFORMED_URI error occurs.
 */
function updateHostAndScheme(info, newHost) {
  let oldHost = info.fixedURI.host;
  let oldScheme = info.fixedURI.scheme;
  try {
    info.fixedURI = info.fixedURI
      .mutate()
      .setScheme(lazy.alternateProtocol)
      .setHost(newHost)
      .finalize();
  } catch (ex) {
    if (ex.result != Cr.NS_ERROR_MALFORMED_URI) {
      throw ex;
    }
    return false;
  }
  if (oldScheme != info.fixedURI.scheme) {
    info.fixupChangedProtocol = true;
  }
  if (oldHost != info.fixedURI.host) {
    info.fixupCreatedAlternateURI = true;
  }
  return true;
}
