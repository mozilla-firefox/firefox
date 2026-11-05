/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FilterAdult: "resource:///modules/FilterAdult.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
  UrlbarUtils: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    logModule: "InteractionsBlocklist",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.places.interactions.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

// A blocklist of regular expressions. Maps base hostnames to a list regular
// expressions for URLs with that base hostname. In this context, "base
// hostname" means the hostname without any subdomains or a public suffix. For
// example, the base hostname for "https://www.maps.google.com/a/place" is
// "google". We do this mapping to improve performance; otherwise we'd have to
// check all URLs against a long list of regular expressions. The regexes are
// defined as escaped strings so that we build them lazily.
// We may want to migrate this list to Remote Settings in the future.
let HOST_BLOCKLIST = {
  auth0: [
    // Auth0 OAuth.
    // XXX: Used alone this could produce false positives where an auth0 URL
    // appears after another valid domain and TLD, but since we limit this to
    // the auth0 hostname those occurrences will be filtered out.
    "^https:\\/\\/.*\\.auth0\\.com\\/login",
  ],
  baidu: [
    // Baidu SERP
    "^(https?:\\/\\/)?(www\\.)?baidu\\.com\\/s.*(\\?|&)wd=.*",
  ],
  bing: [
    // Bing SERP
    "^(https?:\\/\\/)?(www\\.)?bing\\.com\\/search.*(\\?|&)q=.*",
  ],
  duckduckgo: [
    // DuckDuckGo SERP
    "^(https?:\\/\\/)?(www\\.)?duckduckgo\\.com\\/.*(\\?|&)q=.*",
  ],
  google: [
    // Google SERP
    "^(https?:\\/\\/)?(www\\.)?google\\.(\\w|\\.){2,}\\/search.*(\\?|&)q=.*",
    // Google OAuth
    "^https:\\/\\/accounts\\.google\\.com\\/o\\/oauth2\\/v2\\/auth",
    "^https:\\/\\/accounts\\.google\\.com\\/signin\\/oauth\\/consent",
  ],
  microsoftonline: [
    // Microsoft OAuth
    "^https:\\/\\/login\\.microsoftonline\\.com\\/common\\/oauth2\\/v2\\.0\\/authorize",
  ],
  yandex: [
    // Yandex SERP
    "^(https?:\\/\\/)?(www\\.)?yandex\\.(\\w|\\.){2,}\\/search.*(\\?|&)text=.*",
  ],
  zoom: [
    // Zoom meeting interstitial
    "^(https?:\\/\\/)?(www\\.)?.*\\.zoom\\.us\\/j\\/\\d+",
  ],
};

HOST_BLOCKLIST = new Proxy(HOST_BLOCKLIST, {
  get(target, property) {
    let regexes = target[property];
    if (!regexes || !Array.isArray(regexes)) {
      return null;
    }

    for (let i = 0; i < regexes.length; i++) {
      let regex = regexes[i];
      if (typeof regex === "string") {
        regex = new RegExp(regex, "i");
        if (regex) {
          regexes[i] = regex;
        } else {
          throw new Error("Blocklist contains invalid regex.");
        }
      }
    }
    return regexes;
  },
});

/**
 * A class that maintains a blocklist of URLs. The class exposes a method to
 * check if a particular URL is contained on the blocklist.
 */
class _InteractionsBlocklist {
  constructor() {
    // Load custom blocklist items from pref.
    try {
      let customBlocklist = JSON.parse(
        Services.prefs.getStringPref(
          "places.interactions.customBlocklist",
          "[]"
        )
      );
      if (!Array.isArray(customBlocklist)) {
        throw new Error();
      }
      let parsedBlocklist = customBlocklist.map(
        regexStr => new RegExp(regexStr)
      );
      HOST_BLOCKLIST["*"] = parsedBlocklist;
    } catch (ex) {
      lazy.logConsole.warn("places.interactions.customBlocklist is corrupted.");
    }
  }

  /**
   * Only certain urls can be added as Interactions, either manually or
   * automatically.
   *
   * @returns {Map} A Map keyed by protocol, for each protocol an object may
   *          define stricter requirements, like extension.
   */
  get urlRequirements() {
    return new Map([
      ["http:", {}],
      ["https:", {}],
      ["file:", { extension: "pdf" }],
    ]);
  }

  /**
   * Whether to record interactions for a given URL.
   * The rules are defined in InteractionsBlocklist.urlRequirements.
   *
   * @param {string|URL|nsIURI} url The URL to check.
   * @returns {boolean} whether the url can be recorded.
   */
  canRecordUrl(url) {
    let protocol, pathname;
    if (typeof url == "string") {
      url = new URL(url);
    }
    if (url instanceof Ci.nsIURI) {
      protocol = url.scheme + ":";
      pathname = url.filePath;
    } else {
      protocol = url.protocol;
      pathname = url.pathname;
    }
    let requirements = InteractionsBlocklist.urlRequirements.get(protocol);
    return (
      requirements &&
      (!requirements.extension || pathname.endsWith(requirements.extension))
    );
  }

  /**
   * Checks a URL against a blocklist of URLs. If the URL is blocklisted, we
   * should not record an interaction.
   *
   * @param {string} urlToCheck
   *   The URL we are looking for on the blocklist.
   * @returns {boolean}
   *  True if `url` is on a blocklist. False otherwise.
   */
  isUrlBlocklisted(urlToCheck) {
    if (lazy.FilterAdult.isAdultUrl(urlToCheck)) {
      return true;
    }

    if (!this.canRecordUrl(urlToCheck)) {
      return true;
    }

    // First, find the URL's base host: the hostname without any subdomains or a
    // public suffix.
    let url = URL.parse(urlToCheck);
    if (!url) {
      lazy.logConsole.warn(
        `Invalid URL passed to InteractionsBlocklist.isUrlBlocklisted: ${urlToCheck}`
      );
      return false;
    }

    if (url.protocol == "file:") {
      return false;
    }

    let hostWithoutSuffix = lazy.UrlbarUtils.stripPublicSuffixFromHost(
      url.host
    );
    let [hostWithSubdomains] = lazy.UrlbarShared.stripPrefixAndTrim(
      hostWithoutSuffix,
      {
        stripWww: true,
        trimTrailingDot: true,
      }
    );
    let baseHost = hostWithSubdomains.substring(
      hostWithSubdomains.lastIndexOf(".") + 1
    );
    // Then fetch blocked regexes for that baseHost and compare them to the full
    // URL. Also check the URL against the custom blocklist.
    let regexes = HOST_BLOCKLIST[baseHost.toLocaleLowerCase()] || [];
    regexes.push(...(HOST_BLOCKLIST["*"] || []));
    if (!regexes) {
      return false;
    }

    return regexes.some(r => r.test(url.href));
  }

  /**
   * Adds a regex to HOST_BLOCKLIST. Since we can't parse the base host from
   * the regex, we add it to a list of wildcard regexes. All URLs are checked
   * against these wildcard regexes. Currently only exposed for tests and use in
   * the console. In the future we could hook this up to a UI component.
   *
   * @param {string|RegExp} regexToAdd
   *   The regular expression to add to our blocklist.
   */
  addRegexToBlocklist(regexToAdd) {
    let regex;
    try {
      regex = new RegExp(regexToAdd, "i");
    } catch (ex) {
      lazy.logConsole.warn("Invalid regex passed to addRegexToBlocklist.");
      return;
    }

    if (!HOST_BLOCKLIST["*"]) {
      HOST_BLOCKLIST["*"] = [];
    }
    HOST_BLOCKLIST["*"].push(regex);
    Services.prefs.setStringPref(
      "places.interactions.customBlocklist",
      JSON.stringify(HOST_BLOCKLIST["*"].map(reg => reg.toString()))
    );
  }

  /**
   * Removes a regex from HOST_BLOCKLIST. If `regexToRemove` is not in the
   * blocklist, this is a no-op. Currently only exposed for tests and use in the
   * console. In the future we could hook this up to a UI component.
   *
   * @param {string|RegExp} regexToRemove
   *   The regular expression to add to our blocklist.
   */
  removeRegexFromBlocklist(regexToRemove) {
    let regex;
    try {
      regex = new RegExp(regexToRemove, "i");
    } catch (ex) {
      lazy.logConsole.warn("Invalid regex passed to addRegexToBlocklist.");
      return;
    }

    if (!HOST_BLOCKLIST["*"] || !Array.isArray(HOST_BLOCKLIST["*"])) {
      return;
    }
    HOST_BLOCKLIST["*"] = HOST_BLOCKLIST["*"].filter(
      curr => curr.source != regex.source
    );
    Services.prefs.setStringPref(
      "places.interactions.customBlocklist",
      JSON.stringify(HOST_BLOCKLIST["*"].map(reg => reg.toString()))
    );
  }
}

export const InteractionsBlocklist = new _InteractionsBlocklist();
