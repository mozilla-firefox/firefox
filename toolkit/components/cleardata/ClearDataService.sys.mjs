/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Downloads: "resource://gre/modules/Downloads.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  NetworkGeolocationCache:
    "moz-src:///dom/system/NetworkGeolocationCache.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  ServiceWorkerCleanUp:
    "moz-src:///toolkit/components/cleardata/ServiceWorkerCleanUp.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "sas",
  "@mozilla.org/storage/activity-service;1",
  Ci.nsIStorageActivityService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IdentityCredentialStorageService",
  "@mozilla.org/browser/identity-credential-storage-service;1",
  Ci.nsIIdentityCredentialStorageService
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "bounceTrackingProtection",
  "@mozilla.org/bounce-tracking-protection;1",
  Ci.nsIBounceTrackingProtection
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "sslTokensCache",
  "@mozilla.org/network/ssl-tokens-cache;1",
  Ci.nsISSLTokensCache
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "bounceTrackingProtectionMode",
  "privacy.bounceTrackingProtection.mode",
  Ci.nsIBounceTrackingProtection.MODE_DISABLED
);

/**
 * Implements nsIPBMCleanupCollector. Passed as the subject of
 * "last-pb-context-exited" when initiated by clearPrivateBrowsingData().
 * Async observers call addPendingCleanup() to register their operations;
 * the collector's promise resolves when all registered callbacks complete.
 */
class PBMCleanupCollector {
  #promises = [];

  addPendingCleanup() {
    let { promise, resolve } = Promise.withResolvers();
    this.#promises.push(promise);
    return {
      complete(aStatus) {
        resolve(aStatus);
      },
      QueryInterface: ChromeUtils.generateQI(["nsIPBMCleanupCallback"]),
    };
  }

  get promise() {
    return Promise.allSettled(this.#promises).then(results => {
      let dominated = false;
      for (let r of results) {
        if (r.status === "fulfilled" && r.value !== Cr.NS_OK) {
          dominated = true;
          break;
        }
        if (r.status === "rejected") {
          dominated = true;
          break;
        }
      }
      return dominated;
    });
  }

  QueryInterface = ChromeUtils.generateQI(["nsIPBMCleanupCollector"]);
}

let gPBMCleanupInProgress = false;

/**
 * Adds brackets to a host if it's an IPv6 address.
 *
 * @param {string} host - Host which may be an IPv6.
 * @returns {string} bracketed IPv6 or host if host is not an IPv6.
 */
function maybeFixupIpv6(host) {
  if (!host?.includes(":")) {
    return host;
  }

  // don't fixup an ipv6 that already has [...]
  if (host.startsWith("[") && host.endsWith("]")) {
    return host;
  }

  return `[${host}]`;
}

/**
 * Test if (host, OriginAttributes) or principal belong to a (schemeless) site.
 * Also considers partitioned storage by inspecting OriginAttributes
 * partitionKey.
 *
 * @param options
 * @param {string} [options.host] - Optional host to compare to site.
 * @param {object} [options.originAttributes] - Optional origin attributes to
 * inspect for aSchemelessSite. If omitted, partitionKey and
 * aOriginAttributesPattern will not be matched.
 * @param {nsIPrincipal} [options.principal] - Optional principal to match with
 * aSchemelessSite and aOriginAttributesPattern.
 * @param {string} aSchemelessSite - Domain to check for. Must be a valid,
 * non-empty baseDomain string.
 * @param {object} [aOriginAttributesPattern] - Additional OriginAttributes
 * filtering using an OriginAttributesPattern. Defaults to {} which matches all.
 * @returns {boolean} Whether the (host, originAttributes) or principal matches
 * the site.
 */
function hasSite(
  { host = null, originAttributes = null, principal = null },
  aSchemelessSite,
  aOriginAttributesPattern = {}
) {
  if (!aSchemelessSite) {
    throw new Error("Missing aSchemelessSite.");
  }
  if (!host && !originAttributes && !principal) {
    throw new Error(
      "Missing host, originAttributes or principal to match with aSchemelessSite."
    );
  }
  if (principal && (host || originAttributes)) {
    throw new Error(
      "Can only pass either principal or host and originAttributes."
    );
  }

  // If aSchemelessSite is an IPV6 host it will have brackets. Ensure that the
  // passed host has brackets too before comparing.
  host = maybeFixupIpv6(host);

  // If passed a host check if it belongs ot the given site.
  // originAttributes is optional. Only check for match if it's passed.
  if (
    host &&
    Services.eTLD.hasRootDomain(host, aSchemelessSite) &&
    (!originAttributes ||
      ChromeUtils.originAttributesMatchPattern(
        originAttributes,
        aOriginAttributesPattern
      ))
  ) {
    return true;
  }

  // If passed a principal check if it belongs to the given site. Also
  // check if the principal's OriginAttributes match our pattern.
  if (
    maybeFixupIpv6(principal?.baseDomain) == aSchemelessSite &&
    ChromeUtils.originAttributesMatchPattern(
      principal.originAttributes,
      aOriginAttributesPattern
    )
  ) {
    return true;
  }

  // Additionally check for partitioned state under the top level
  // aSchemelessSite. We need to inspect the OriginAttributes partitionKey for
  // that.
  let oa = originAttributes ?? principal?.originAttributes;
  if (oa == null) {
    // No OriginAttributes passed in to compare with.
    return false;
  }

  // For matching partitioned state under aSchemelessSite we use a
  // PartitionKeyPattern. Merge it with the aOriginAttributesPattern from the
  // caller.
  let patternWithPartitionKey = {
    ...aOriginAttributesPattern,
    partitionKeyPattern: { baseDomain: aSchemelessSite },
  };

  return ChromeUtils.originAttributesMatchPattern(oa, patternWithPartitionKey);
}

// Here is a list of methods cleaners may implement. These methods must return a
// Promise object.
// * deleteAll() - this method _must_ exist. When called, it deletes all the
//                 data owned by the cleaner.
// * deleteByPrincipal() -  this method _must_ exist.
// * deleteBySite() - this method _must_ exist.
// * deleteByHost() - this method is implemented only if the cleaner knows
//                    how to delete data by host + originAttributes pattern. If
//                    not implemented, deleteAll() will be used as fallback.
// * deleteByRange() - this method is implemented only if the cleaner knows how
//                    to delete data by time range. It receives 2 time range
//                    parameters: aFrom/aTo. If not implemented, deleteAll() is
//                    used as fallback.
// * deleteByLocalFiles() - this method removes data held for local files and
//                          other hostless origins. If not implemented,
//                          **no fallback is used**, as for a number of
//                          cleaners, no such data will ever exist and
//                          therefore clearing it does not make sense.
// * deleteByOriginAttributes() - this method is implemented only if the cleaner
//                                knows how to delete data by originAttributes
//                                pattern.
// * cleanupAfterDeletionAtShutdown() - this method is implemented only if the
//                                      cleaner needs a separate step after
//                                      deletion. No-op if not implemented.
//                                      Currently called via
//                                      Sanitizer.sanitizeOnShutdown() and
//                                      Sanitizer.onStartup()

// IETF spec for compression dictionaries requires clearing them when cookies
// are cleared for the site - Section 10 of
// https://datatracker.ietf.org/doc/draft-ietf-httpbis-compression-dictionary/
const CookieCleaner = {
  deleteByLocalFiles(aOriginAttributes) {
    return new Promise(aResolve => {
      Services.cookies.removeCookiesFromExactHost(
        "",
        JSON.stringify(aOriginAttributes)
      );
      aResolve();
    });
  },

  deleteByHost(aHost, aOriginAttributes) {
    return new Promise(aResolve => {
      Services.cookies.removeCookiesFromExactHost(
        aHost,
        JSON.stringify(aOriginAttributes)
      );
      // Compression dictionaries are https only
      // Note that IPV6 urls require [...], but aPrincipal.host (passed
      // to this) doesn't include the [].
      if (aHost.includes(":") && aHost[0] != "[") {
        aHost = "https://[" + aHost + "]";
      } else {
        aHost = "https://" + aHost;
      }
      let httpsURI = Services.io.newURI(aHost);
      Services.cache2.clearOriginDictionary(httpsURI);
      aResolve();
    });
  },

  deleteByPrincipal(aPrincipal) {
    // Fall back to clearing by host and OA pattern. This will over-clear, since
    // any properties that are not explicitly set in aPrincipal.originAttributes
    // will be wildcard matched.
    // Note that we clear cookies for all ports, because apparently
    // cookies historically have ignored ports based on sameSite rules:
    // https://html.spec.whatwg.org/#same-site
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    Services.cookies.cookies
      .filter(({ rawHost, originAttributes }) =>
        hasSite(
          { host: rawHost, originAttributes },
          aSchemelessSite,
          aOriginAttributesPattern
        )
      )
      .forEach(cookie => {
        Services.cookies.removeCookiesFromExactHost(
          cookie.rawHost,
          JSON.stringify(cookie.originAttributes)
        );
      });
    // Compression dictionaries are https only
    let httpsURI = Services.io.newURI("https://" + aSchemelessSite);
    Services.cache2.clearOriginDictionary(httpsURI);
  },

  deleteByRange(aFrom) {
    return Services.cookies.removeAllSince(aFrom);
  },

  deleteByOriginAttributes(aOriginAttributesString) {
    return new Promise(aResolve => {
      try {
        Services.cookies.removeCookiesWithOriginAttributes(
          aOriginAttributesString
        );
      } catch (ex) {}
      // XXX Bug 1984198 we need to clear dictionaries here (probably has
      // to be in CookieService::RemoveCookiesWithOriginAttributes()
      aResolve();
    });
  },

  deleteAll() {
    return new Promise(aResolve => {
      Services.cookies.removeAll();
      Services.cache2.clearAllOriginDictionaries();
      aResolve();
    });
  },
};

// A cleaner for cleaning fingerprinting protection states.
const FingerprintingProtectionStateCleaner = {
  async _maybeClearSiteSpecificZoom(aSchemelessSite, aOriginAttributes = {}) {
    if (
      !ChromeUtils.shouldResistFingerprinting("SiteSpecificZoom", null, true)
    ) {
      return;
    }

    const cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );
    const ZOOM_PREF_NAME = "browser.content.full-zoom";

    await new Promise((aResolve, aReject) => {
      aOriginAttributes =
        ChromeUtils.fillNonDefaultOriginAttributes(aOriginAttributes);

      let loadContext;
      if (
        aOriginAttributes.privateBrowsingId ==
        Services.scriptSecurityManager.DEFAULT_PRIVATE_BROWSING_ID
      ) {
        loadContext = Cu.createLoadContext();
      } else {
        loadContext = Cu.createPrivateLoadContext();
      }

      cps2.removeBySubdomainAndName(
        aSchemelessSite,
        ZOOM_PREF_NAME,
        loadContext,
        {
          handleCompletion: aReason => {
            if (aReason === cps2.COMPLETE_ERROR) {
              aReject();
            } else {
              aResolve();
            }
          },
        }
      );
    });
  },

  async deleteAll() {
    Services.rfp.cleanAllRandomKeys();
  },

  async deleteByPrincipal(aPrincipal) {
    Services.rfp.cleanRandomKeyByPrincipal(aPrincipal);

    await this._maybeClearSiteSpecificZoom(
      aPrincipal.host,
      aPrincipal.originAttributes
    );
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    Services.rfp.cleanRandomKeyBySite(
      aSchemelessSite,
      aOriginAttributesPattern
    );

    await this._maybeClearSiteSpecificZoom(
      aSchemelessSite,
      aOriginAttributesPattern
    );
  },

  async deleteByHost(aHost, aOriginAttributesPattern) {
    Services.rfp.cleanRandomKeyByHost(
      aHost,
      JSON.stringify(aOriginAttributesPattern)
    );

    await this._maybeClearSiteSpecificZoom(aHost, aOriginAttributesPattern);
  },

  async deleteByOriginAttributes(aOriginAttributesString) {
    Services.rfp.cleanRandomKeyByOriginAttributesPattern(
      aOriginAttributesString
    );

    // For deleteByOriginAttributes, we only receive userContextId which is not enough to target specific
    // site-specific zooms. So we don't clear site-specific zooms here.
  },
};

const CertCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    let overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(
      Ci.nsICertOverrideService
    );

    overrideService.clearValidityOverride(aHost, -1, aOriginAttributes);
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    let overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(
      Ci.nsICertOverrideService
    );
    overrideService
      .getOverrides()
      .filter(({ asciiHost, originAttributes }) =>
        hasSite(
          { host: asciiHost, originAttributes },
          aSchemelessSite,
          aOriginAttributesPattern
        )
      )
      .forEach(({ asciiHost, port }) =>
        overrideService.clearValidityOverride(asciiHost, port, {})
      );
  },

  async deleteAll() {
    let overrideService = Cc["@mozilla.org/security/certoverride;1"].getService(
      Ci.nsICertOverrideService
    );

    overrideService.clearAllOverrides();
  },
};

const NetworkCacheCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    // Delete data from both HTTP and HTTPS sites.
    let httpURI = Services.io.newURI("http://" + aHost);
    let httpsURI = Services.io.newURI("https://" + aHost);
    let httpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpURI,
      aOriginAttributes
    );
    let httpsPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpsURI,
      aOriginAttributes
    );

    Services.cache2.clearOriginsByPrincipal(httpPrincipal);
    Services.cache2.clearOriginsByPrincipal(httpsPrincipal);
  },

  async deleteBySite(aSchemelessSite, _aOriginAttributesPattern) {
    // TODO: aOriginAttributesPattern
    Services.cache2.clearBaseDomain(aSchemelessSite);
  },

  deleteByPrincipal(aPrincipal) {
    return new Promise(aResolve => {
      Services.cache2.clearOriginsByPrincipal(aPrincipal);
      aResolve();
    });
  },

  deleteByOriginAttributes(aOriginAttributesString) {
    return new Promise(aResolve => {
      Services.cache2.clearOriginsByOriginAttributes(aOriginAttributesString);
      aResolve();
    });
  },

  deleteAll() {
    return new Promise(aResolve => {
      Services.cache2.clear();
      aResolve();
    });
  },
};

const createResourceCleaner = type => ({
  async deleteByHost(aHost, aOriginAttributes) {
    // Delete data from both HTTP and HTTPS sites.
    let httpURI = Services.io.newURI("http://" + aHost);
    let httpsURI = Services.io.newURI("https://" + aHost);
    let httpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpURI,
      aOriginAttributes
    );
    let httpsPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpsURI,
      aOriginAttributes
    );

    this.deleteByPrincipal(httpPrincipal);
    this.deleteByPrincipal(httpsPrincipal);
  },

  async deleteByPrincipal(aPrincipal) {
    ChromeUtils.clearResourceCache({
      types: [type],
      principal: aPrincipal,
    });
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    ChromeUtils.clearResourceCache({
      types: [type],
      schemelessSite: aSchemelessSite,
      pattern: aOriginAttributesPattern,
    });
  },

  async deleteAll() {
    ChromeUtils.clearResourceCache({
      types: [type],
    });
  },
});

const CSSCacheCleaner = createResourceCleaner("stylesheet");
const JSCacheCleaner = createResourceCleaner("script");
const ImageCacheCleaner = createResourceCleaner("image");

const MessagingLayerSecurityStateCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    // Delete data from both HTTP and HTTPS sites.
    let httpURI = Services.io.newURI("http://" + aHost);
    let httpsURI = Services.io.newURI("https://" + aHost);
    let httpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpURI,
      aOriginAttributes
    );
    let httpsPrincipal = Services.scriptSecurityManager.createContentPrincipal(
      httpsURI,
      aOriginAttributes
    );
    ChromeUtils.clearMessagingLayerSecurityStateByPrincipal(httpsPrincipal);
    // The WebAPI doesn't allow for non-secure contexts but
    // we are keeping this out of caution.
    ChromeUtils.clearMessagingLayerSecurityStateByPrincipal(httpPrincipal);
  },
  async deleteByPrincipal(aPrincipal) {
    ChromeUtils.clearMessagingLayerSecurityStateByPrincipal(aPrincipal);
  },
  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    ChromeUtils.clearMessagingLayerSecurityStateBySite(
      aSchemelessSite,
      aOriginAttributesPattern
    );
  },
  async deleteAll() {
    ChromeUtils.clearMessagingLayerSecurityState();
  },
};

const DownloadsCleaner = {
  async _deleteInternal({ host, principal, originAttributes }) {
    originAttributes = originAttributes || principal?.originAttributes || {};

    let list = await lazy.Downloads.getList(lazy.Downloads.ALL);
    list.removeFinished(({ source }) => {
      if (
        "userContextId" in originAttributes &&
        "userContextId" in source &&
        originAttributes.userContextId != source.userContextId
      ) {
        return false;
      }
      if (
        "privateBrowsingId" in originAttributes &&
        !!originAttributes.privateBrowsingId != source.isPrivate
      ) {
        return false;
      }

      let entryURI = Services.io.newURI(source.url);
      if (host) {
        return Services.eTLD.hasRootDomain(entryURI.host, host);
      }
      if (principal) {
        return principal.equalsURI(entryURI);
      }
      return false;
    });
  },

  async deleteByHost(aHost, aOriginAttributes) {
    // Clearing by host also clears associated subdomains.
    return this._deleteInternal({
      host: aHost,
      originAttributes: aOriginAttributes,
    });
  },

  deleteByPrincipal(aPrincipal) {
    return this._deleteInternal({ principal: aPrincipal });
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    let list = await lazy.Downloads.getList(lazy.Downloads.ALL);
    list.removeFinished(({ source }) => {
      if (
        "userContextId" in aOriginAttributesPattern &&
        "userContextId" in source &&
        aOriginAttributesPattern.userContextId != source.userContextId
      ) {
        return false;
      }
      if (
        "privateBrowsingId" in aOriginAttributesPattern &&
        !!aOriginAttributesPattern.privateBrowsingId != source.isPrivate
      ) {
        return false;
      }

      let entryURI = Services.io.newURI(source.url);
      return Services.eTLD.getSchemelessSite(entryURI) == aSchemelessSite;
    });
  },

  deleteByRange(aFrom, aTo) {
    // Convert microseconds back to milliseconds for date comparisons.
    let rangeBeginMs = aFrom / 1000;
    let rangeEndMs = aTo / 1000;

    return lazy.Downloads.getList(lazy.Downloads.ALL).then(aList => {
      aList.removeFinished(
        aDownload =>
          aDownload.startTime >= rangeBeginMs &&
          aDownload.startTime <= rangeEndMs
      );
    });
  },

  deleteAll() {
    return lazy.Downloads.getList(lazy.Downloads.ALL).then(aList => {
      aList.removeFinished(null);
    });
  },
};

const MediaDevicesCleaner = {
  async deleteByRange(aFrom) {
    let mediaMgr = Cc["@mozilla.org/mediaManagerService;1"].getService(
      Ci.nsIMediaManagerService
    );
    mediaMgr.sanitizeDeviceIds(aFrom);
  },

  // TODO: We should call the MediaManager to clear by principal, rather than
  // over-clearing for user requests or bailing out for programmatic calls.
  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  // TODO: Same as above, but for site.
  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteAll() {
    let mediaMgr = Cc["@mozilla.org/mediaManagerService;1"].getService(
      Ci.nsIMediaManagerService
    );
    mediaMgr.sanitizeDeviceIds(null);
  },
};

const QuotaCleaner = {
  /**
   * Clear quota storage for matching principals.
   *
   * @param {function} filterFn - Filter function which is passed a principal.
   * Return true to clear storage for given principal or false to skip it.
   * @returns {Promise} - Resolves once all matching items have been cleared.
   * Rejects on error.
   */
  async _qmsClearStoragesForPrincipalsMatching(filterFn) {
    // Clearing quota storage by first getting all entry origins and then
    // iterating over them is not ideal, since we can not ensure an entirely
    // consistent clearing state. Between fetching the origins and clearing
    // them, additional entries could be added. This means we could end up with
    // stray entries after the clearing operation. To fix this we would need to
    // move the clearing code to the QuotaManager itself which could either
    // prevent new writes while clearing or clean up any additional entries
    // which get written during the clearing operation.
    // Performance is also not ideal, since we iterate over storage multiple
    // times for this two step process.
    // See Bug 1719195.
    let origins = await new Promise((resolve, reject) => {
      Services.qms.listOrigins().callback = request => {
        if (request.resultCode != Cr.NS_OK) {
          reject({ message: "Deleting quota storages failed" });
          return;
        }
        resolve(request.result);
      };
    });

    let clearPromises = origins
      // Parse origins into principals.
      .map(Services.scriptSecurityManager.createContentPrincipalFromOrigin)
      // Filter out principals that don't match the filterFn.
      .filter(filterFn)
      // Clear quota storage by principal and collect the promises.
      .map(
        principal =>
          new Promise((resolve, reject) => {
            let clearRequest =
              Services.qms.clearStoragesForPrincipal(principal);
            clearRequest.callback = () => {
              if (clearRequest.resultCode != Cr.NS_OK) {
                reject({ message: "Deleting quota storages failed" });
                return;
              }
              resolve();
            };
          })
      );
    return Promise.all(clearPromises);
  },

  deleteByPrincipal(aPrincipal) {
    // localStorage: The legacy LocalStorage implementation that will
    // eventually be removed depends on this observer notification to clear by
    // principal.
    Services.obs.notifyObservers(
      null,
      "extension:purge-localStorage",
      aPrincipal.host
    );

    // Clear sessionStorage
    Services.sessionStorage.clearStoragesForOrigin(aPrincipal);

    // ServiceWorkers: they must be removed before cleaning QuotaManager.
    return lazy.ServiceWorkerCleanUp.removeFromPrincipal(aPrincipal)
      .then(
        _ => /* exceptionThrown = */ false,
        _ => /* exceptionThrown = */ true
      )
      .then(exceptionThrown => {
        // QuotaManager: In the event of a failure, we call reject to propagate
        // the error upwards.
        return new Promise((aResolve, aReject) => {
          let req = Services.qms.clearStoragesForPrincipal(aPrincipal);
          req.callback = () => {
            if (exceptionThrown || req.resultCode != Cr.NS_OK) {
              aReject({ message: "Delete by principal failed" });
            } else {
              aResolve();
            }
          };
        });
      });
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    // localStorage: The legacy LocalStorage implementation that will
    // eventually be removed depends on this observer notification to clear by
    // host.  Some other subsystems like Reporting headers depend on this too.
    // TODO: aOriginAttributesPattern
    Services.obs.notifyObservers(
      null,
      "extension:purge-localStorage",
      aSchemelessSite
    );

    // Clear sessionStorage
    let entry = Cc["@mozilla.org/clear-by-site-entry;1"].createInstance(
      Ci.nsIClearBySiteEntry
    );
    entry.schemelessSite = aSchemelessSite;
    // Convert the pattern to a JSON string.
    entry.patternJSON = JSON.stringify(aOriginAttributesPattern);

    Services.obs.notifyObservers(entry, "browser:purge-sessionStorage");

    // Clear third-party storage partitioned under aSchemelessSite.
    // This notification is forwarded via the StorageObserver and consumed only
    // by the SessionStorageManager and (legacy) LocalStorageManager.
    // There is a similar (legacy) notification "clear-origin-attributes-data"
    // which additionally clears data across various other storages unrelated to
    // the QuotaCleaner.
    Services.obs.notifyObservers(
      null,
      "dom-storage:clear-origin-attributes-data",
      JSON.stringify({
        ...aOriginAttributesPattern,
        partitionKeyPattern: { baseDomain: aSchemelessSite },
      })
    );

    // ServiceWorkers must be removed before cleaning QuotaManager. We store
    // potential errors so we can re-throw later, once all operations have
    // completed.
    let swCleanupError;
    try {
      await lazy.ServiceWorkerCleanUp.removeFromSite(
        aSchemelessSite,
        aOriginAttributesPattern
      );
    } catch (error) {
      swCleanupError = error;
    }

    await this._qmsClearStoragesForPrincipalsMatching(principal =>
      hasSite({ principal }, aSchemelessSite, aOriginAttributesPattern)
    );

    // Re-throw any service worker cleanup errors.
    if (swCleanupError) {
      throw swCleanupError;
    }
  },

  async deleteByHost(aHost) {
    // XXX: The aOriginAttributes is expected to always be empty({}). Maybe have
    // a debug assertion here to ensure that?

    // localStorage: The legacy LocalStorage implementation that will
    // eventually be removed depends on this observer notification to clear by
    // host.  Some other subsystems like Reporting headers depend on this too.
    Services.obs.notifyObservers(null, "extension:purge-localStorage", aHost);

    // Clear sessionStorage
    Services.obs.notifyObservers(null, "browser:purge-sessionStorage", aHost);

    // ServiceWorkers must be removed before cleaning QuotaManager. We store any
    // errors so we can re-throw later once all operations have completed.
    let swCleanupError;
    try {
      await lazy.ServiceWorkerCleanUp.removeFromHost(aHost);
    } catch (error) {
      swCleanupError = error;
    }

    await this._qmsClearStoragesForPrincipalsMatching(principal => {
      try {
        // deleteByHost has the semantics that "foo.example.com" should be
        // wiped if we are provided an aHost of "example.com".
        return Services.eTLD.hasRootDomain(principal.host, aHost);
      } catch (e) {
        // There is no host for the given principal.
        return false;
      }
    });

    // Re-throw any service worker cleanup errors.
    if (swCleanupError) {
      throw swCleanupError;
    }
  },

  deleteByRange(aFrom, aTo) {
    let principals = lazy.sas
      .getActiveOrigins(aFrom, aTo)
      .QueryInterface(Ci.nsIArray);

    let promises = [];
    for (let i = 0; i < principals.length; ++i) {
      let principal = principals.queryElementAt(i, Ci.nsIPrincipal);

      if (
        !principal.schemeIs("http") &&
        !principal.schemeIs("https") &&
        !principal.schemeIs("file")
      ) {
        continue;
      }

      promises.push(this.deleteByPrincipal(principal));
    }

    return Promise.all(promises);
  },

  deleteByOriginAttributes(aOriginAttributesString) {
    // The legacy LocalStorage implementation that will eventually be removed.
    // And it should've been cleared while notifying observers with
    // clear-origin-attributes-data.

    return lazy.ServiceWorkerCleanUp.removeFromOriginAttributes(
      aOriginAttributesString
    )
      .then(
        _ => /* exceptionThrown = */ false,
        _ => /* exceptionThrown = */ true
      )
      .then(() => {
        // QuotaManager: In the event of a failure, we call reject to propagate
        // the error upwards.
        return new Promise((aResolve, aReject) => {
          let req = Services.qms.clearStoragesForOriginAttributesPattern(
            aOriginAttributesString
          );
          req.callback = () => {
            if (req.resultCode == Cr.NS_OK) {
              aResolve();
            } else {
              aReject({ message: "Delete by origin attributes failed" });
            }
          };
        });
      });
  },

  async deleteAll() {
    // localStorage
    Services.obs.notifyObservers(null, "extension:purge-localStorage");

    // sessionStorage
    Services.obs.notifyObservers(null, "browser:purge-sessionStorage");

    // ServiceWorkers must be removed before cleaning QuotaManager. We store any
    // errors so we can re-throw later once all operations have completed.
    let swCleanupError;
    try {
      await lazy.ServiceWorkerCleanUp.removeAll();
    } catch (error) {
      swCleanupError = error;
    }

    await this._qmsClearStoragesForPrincipalsMatching(
      principal =>
        principal.schemeIs("http") ||
        principal.schemeIs("https") ||
        principal.schemeIs("file")
    );

    // Re-throw any service worker cleanup errors.
    if (swCleanupError) {
      throw swCleanupError;
    }
  },

  async cleanupAfterDeletionAtShutdown() {
    const tobeRemoveDirName = "to-be-removed";
    const storageName = Services.prefs.getStringPref(
      "dom.quotaManager.storageName"
    );

    if (!storageName) {
      throw new Error("storage name must not be empty");
    }

    const toBeRemovedDir = PathUtils.join(
      PathUtils.profileDir,
      storageName,
      tobeRemoveDirName
    );

    if (
      !AppConstants.MOZ_BACKGROUNDTASKS ||
      !Services.prefs.getBoolPref("dom.quotaManager.backgroundTask.enabled")
    ) {
      // Our behavior in this case differs from our use of the background-task below because
      // while the background-task will only try to empty the contents of the directory but
      // leave the directory itself intact, our call here will remove the directory. We
      // remove the directory here for reasons of implementation simplicity and because
      // we do not have to worry about the same race that the background task has to worry
      // about. Specifically, the background task needs to worry about gecko restarting and
      // racing on QM trying to move directories into the to-be-removed directory. But as long
      // as we are confident QM has fully processed its I/O thread, we know it should not be
      // trying to move new files into it because we are in the same process.

      await IOUtils.remove(toBeRemovedDir, { recursive: true });
      return;
    }
    // return early if directory does not exist or empty
    if (!(await IOUtils.hasChildren(toBeRemovedDir, { ignoreAbsent: true }))) {
      return;
    }

    const runner = Cc["@mozilla.org/backgroundtasksrunner;1"].getService(
      Ci.nsIBackgroundTasksRunner
    );

    runner.removeDirectoryInDetachedProcess(
      toBeRemovedDir,
      "",
      "0",
      "*", // wildcard
      "Quota"
    );
  },
};

const PushNotificationsCleaner = {
  /**
   * Clear entries for aDomain including subdomains of aDomain.
   *
   * @param {string} aDomain - Domain to clear data for.
   * @param {object} aOriginAttributesPattern - Optional pattern to filter OriginAttributes.
   * @returns {Promise} a promise which resolves once data has been cleared.
   */
  _deleteByRootDomain(aDomain, aOriginAttributesPattern = null) {
    if (!Services.prefs.getBoolPref("dom.push.enabled", false)) {
      return Promise.resolve();
    }

    return new Promise((aResolve, aReject) => {
      let push = Cc["@mozilla.org/push/Service;1"].getService(
        Ci.nsIPushService
      );
      // ClearForDomain also clears subdomains.
      push.clearForDomain(aDomain, aOriginAttributesPattern, aStatus => {
        if (!Components.isSuccessCode(aStatus)) {
          aReject();
        } else {
          aResolve();
        }
      });
    });
  },

  deleteByHost(aHost) {
    // Will also clear entries for subdomains of aHost. Data is cleared across
    // all origin attributes.
    return this._deleteByRootDomain(aHost);
  },

  deleteByPrincipal(aPrincipal) {
    if (!Services.prefs.getBoolPref("dom.push.enabled", false)) {
      return Promise.resolve();
    }

    return new Promise((aResolve, aReject) => {
      let push = Cc["@mozilla.org/push/Service;1"].getService(
        Ci.nsIPushService
      );
      push.clearForPrincipal(aPrincipal, aStatus => {
        if (!Components.isSuccessCode(aStatus)) {
          aReject();
        } else {
          aResolve();
        }
      });
    });
  },

  deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    return this._deleteByRootDomain(aSchemelessSite, aOriginAttributesPattern);
  },

  deleteAll() {
    if (!Services.prefs.getBoolPref("dom.push.enabled", false)) {
      return Promise.resolve();
    }

    return new Promise((aResolve, aReject) => {
      let push = Cc["@mozilla.org/push/Service;1"].getService(
        Ci.nsIPushService
      );
      push.clearForDomain("*", null, aStatus => {
        if (!Components.isSuccessCode(aStatus)) {
          aReject();
        } else {
          aResolve();
        }
      });
    });
  },
};

const StorageAccessCleaner = {
  // This is a special function to implement deleteUserInteractionForClearingHistory.
  async deleteExceptPrincipals(aPrincipalsWithStorage, aFrom) {
    // We compare by base domain in order to simulate the behavior
    // from purging, Consider a scenario where the user is logged
    // into sub.example.com but the cookies are on example.com. In this
    // case, we will remove the user interaction for sub.example.com
    // because its principal does not match the one with storage.
    let baseDomainsWithStorage = new Set();
    for (let principal of aPrincipalsWithStorage) {
      baseDomainsWithStorage.add(principal.baseDomain);
    }
    Services.perms.removePermissionsExceptDefaults(
      Services.perms
        .getAllByTypeSince(
          "storageAccessAPI",
          // The permission manager uses milliseconds instead of microseconds
          aFrom / 1000
        )
        .filter(perm => !baseDomainsWithStorage.has(perm.principal.baseDomain))
    );
  },

  async deleteByPrincipal(aPrincipal) {
    return Services.perms.removeFromPrincipal(aPrincipal, "storageAccessAPI");
  },

  _deleteInternal(filter, aOriginAttributesPattern = {}) {
    Services.perms.removePermissionsExceptDefaults(
      Services.perms
        .getAllForOriginAttributesPattern(
          JSON.stringify(aOriginAttributesPattern),
          ["storageAccessAPI"]
        )
        .filter(filter)
    );
  },

  async deleteByHost(aHost) {
    // Clearing by host also clears associated subdomains.
    this._deleteInternal(({ principal }) => {
      let toBeRemoved = false;
      try {
        toBeRemoved = Services.eTLD.hasRootDomain(principal.host, aHost);
      } catch (ex) {}
      return toBeRemoved;
    });
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    this._deleteInternal(
      ({ principal }) => hasSite({ principal }, aSchemelessSite),
      aOriginAttributesPattern
    );
  },

  async deleteByRange(aFrom) {
    Services.perms.removeByTypeSince("storageAccessAPI", aFrom / 1000);
  },

  async deleteAll() {
    Services.perms.removeByType("storageAccessAPI");
  },
};

const HistoryCleaner = {
  deleteByHost(aHost) {
    if (!AppConstants.MOZ_PLACES) {
      return Promise.resolve();
    }
    return lazy.PlacesUtils.history.removeByFilter({ host: "." + aHost });
  },

  deleteByPrincipal(aPrincipal) {
    if (!AppConstants.MOZ_PLACES) {
      return Promise.resolve();
    }
    return lazy.PlacesUtils.history.removeByFilter({ host: aPrincipal.host });
  },

  deleteBySite(aSchemelessSite) {
    return this.deleteByHost(aSchemelessSite);
  },

  deleteByRange(aFrom, aTo) {
    if (!AppConstants.MOZ_PLACES) {
      return Promise.resolve();
    }
    return lazy.PlacesUtils.history.removeVisitsByFilter({
      beginDate: new Date(aFrom / 1000),
      endDate: new Date(aTo / 1000),
    });
  },

  deleteAll() {
    if (!AppConstants.MOZ_PLACES) {
      return Promise.resolve();
    }
    return lazy.PlacesUtils.history.clear();
  },
};

const SessionHistoryCleaner = {
  async deleteByHost(aHost) {
    // Session storage and history also clear subdomains of aHost.
    Services.obs.notifyObservers(null, "browser:purge-sessionStorage", aHost);
    Services.obs.notifyObservers(
      null,
      "browser:purge-session-history-for-domain",
      aHost
    );
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  deleteBySite(aSchemelessSite, _aOriginAttributesPattern) {
    // TODO: aOriginAttributesPattern.
    return this.deleteByHost(aSchemelessSite, {});
  },

  async deleteByRange(aFrom) {
    Services.obs.notifyObservers(
      null,
      "browser:purge-session-history",
      String(aFrom)
    );
  },

  async deleteAll() {
    Services.obs.notifyObservers(null, "browser:purge-session-history");
  },
};

const AuthTokensCleaner = {
  // TODO: Bug 1726742
  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  // TODO: Bug 1726742
  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteAll() {
    let sdr = Cc["@mozilla.org/security/sdr;1"].getService(
      Ci.nsISecretDecoderRing
    );
    sdr.logoutAndTeardown();
  },
};

const AuthCacheCleaner = {
  // TODO: Bug 1726743
  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  // TODO: Bug 1726743
  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  deleteAll() {
    return new Promise(aResolve => {
      Services.obs.notifyObservers(null, "net:clear-active-logins");
      aResolve();
    });
  },
};

// Type of the shutdown exception permission.
const SHUTDOWN_EXCEPTION_PERMISSION = "persist-data-on-shutdown";

const ShutdownExceptionsCleaner = {
  async _deleteInternal(filter, aOriginAttributesPattern = {}) {
    Services.perms.removePermissionsExceptDefaults(
      Services.perms
        .getAllForOriginAttributesPattern(
          JSON.stringify(aOriginAttributesPattern),
          [SHUTDOWN_EXCEPTION_PERMISSION]
        )
        .filter(filter)
    );
  },

  async deleteByHost(aHost) {
    await this._deleteInternal(({ principal }) => {
      let { host: principalHost } = principal;
      if (!principalHost?.length) {
        return false;
      }
      return Services.eTLD.hasRootDomain(principal.host, aHost);
    });
  },

  async deleteByPrincipal(aPrincipal) {
    Services.perms.removeFromPrincipal(
      aPrincipal,
      SHUTDOWN_EXCEPTION_PERMISSION
    );
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    await this._deleteInternal(
      ({ principal }) => hasSite({ principal }, aSchemelessSite),
      aOriginAttributesPattern
    );
  },

  async deleteByRange(aFrom) {
    Services.perms.removeByTypeSince(
      SHUTDOWN_EXCEPTION_PERMISSION,
      aFrom / 1000
    );
  },

  async deleteByOriginAttributes(aOriginAttributesString) {
    Services.perms.removePermissionsWithAttributes(
      aOriginAttributesString,
      [SHUTDOWN_EXCEPTION_PERMISSION],
      []
    );
  },

  async deleteAll() {
    Services.perms.removeByType(SHUTDOWN_EXCEPTION_PERMISSION);
  },
};

const PermissionsCleaner = {
  async _deleteInternal(filter, aOriginAttributesPattern = {}) {
    try {
      Services.perms.removePermissionsExceptDefaults(
        Services.perms
          .getAllForOriginAttributesPattern(
            JSON.stringify(aOriginAttributesPattern),
            []
          )
          // Skip persist-data-on-shutdown because it is handled by
          // ShutdownExceptionsCleaner.
          .filter(({ type }) => type != SHUTDOWN_EXCEPTION_PERMISSION)
          .filter(filter)
      );
    } finally {
      // Clean up interaction tracking data for removed permissions.
      await Services.perms.removeOrphanedInteractionRecords();
    }
  },

  _thirdPartyStoragePermissionMatchesHost(permissionType, aHost) {
    if (
      !permissionType.startsWith("3rdPartyStorage^") &&
      !permissionType.startsWith("3rdPartyFrameStorage^")
    ) {
      return false;
    }
    let [, site] = permissionType.split("^");
    let uri;
    try {
      uri = Services.io.newURI(site);
    } catch (ex) {
      return false;
    }
    return Services.eTLD.hasRootDomain(uri.host, aHost);
  },

  _getPrincipalHost(principal) {
    try {
      return principal.host;
    } catch (e) {
      return null;
    }
  },

  async deleteByHost(aHost) {
    await this._deleteInternal(({ principal, type }) => {
      let principalHost = this._getPrincipalHost(principal);
      if (!principalHost?.length) {
        return false;
      }
      if (Services.eTLD.hasRootDomain(principalHost, aHost)) {
        return true;
      }

      return this._thirdPartyStoragePermissionMatchesHost(type, aHost);
    });
  },

  async deleteByPrincipal(aPrincipal) {
    await this._deleteInternal(({ principal, type }) => {
      if (principal.equals(aPrincipal)) {
        return true;
      }
      let principalHost = this._getPrincipalHost(aPrincipal);
      if (!principalHost?.length) {
        return false;
      }
      return this._thirdPartyStoragePermissionMatchesHost(type, principalHost);
    });
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    await this._deleteInternal(
      ({ principal, type }) =>
        hasSite({ principal }, aSchemelessSite) ||
        this._thirdPartyStoragePermissionMatchesHost(type, aSchemelessSite),
      aOriginAttributesPattern
    );
  },

  async deleteByRange(aFrom) {
    Services.perms.removeAllSinceWithTypeExceptions(aFrom / 1000, [
      SHUTDOWN_EXCEPTION_PERMISSION,
    ]);
  },

  async deleteByOriginAttributes(aOriginAttributesString) {
    Services.perms.removePermissionsWithAttributes(
      aOriginAttributesString,
      [],
      [SHUTDOWN_EXCEPTION_PERMISSION]
    );
  },

  async deleteAll() {
    Services.perms.removeAllExceptTypes([SHUTDOWN_EXCEPTION_PERMISSION]);
  },
};

const PreferencesCleaner = {
  deleteByHost(aHost, aOriginAttributes = {}) {
    aOriginAttributes =
      ChromeUtils.fillNonDefaultOriginAttributes(aOriginAttributes);

    let loadContext;
    if (
      aOriginAttributes.privateBrowsingId ==
      Services.scriptSecurityManager.DEFAULT_PRIVATE_BROWSING_ID
    ) {
      loadContext = Cu.createLoadContext();
    } else {
      loadContext = Cu.createPrivateLoadContext();
    }

    // Also clears subdomains of aHost.
    return new Promise((aResolve, aReject) => {
      let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
        Ci.nsIContentPrefService2
      );
      cps2.removeBySubdomain(aHost, loadContext, {
        handleCompletion: aReason => {
          if (aReason === cps2.COMPLETE_ERROR) {
            aReject();
          } else {
            aResolve();
          }
        },
      });
    });
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    // If aOriginAttributesPattern does not specify private or normal browsing
    // clear both.
    let loadContext = null;

    // If the pattern filters by normal or private browsing mode only clear that mode.
    if (aOriginAttributesPattern.privateBrowsingId != null) {
      // The default private browsing ID is 0 which is non private browsing mode
      // / normal mode.
      let isPrivateBrowsing =
        aOriginAttributesPattern.privateBrowsingId !=
        Ci.nsIScriptSecurityManager.DEFAULT_PRIVATE_BROWSING_ID;
      loadContext = isPrivateBrowsing
        ? Cu.createPrivateLoadContext()
        : Cu.createLoadContext();
    }

    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );

    await new Promise((aResolve, aReject) => {
      cps2.removeBySubdomain(aSchemelessSite, loadContext, {
        handleCompletion: aReason => {
          if (aReason === cps2.COMPLETE_ERROR) {
            aReject();
          } else {
            aResolve();
          }
        },
      });
    });
  },

  async deleteByRange(aFrom) {
    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );

    await new Promise((aResolve, aReject) => {
      cps2.removeAllDomainsSince(aFrom / 1000, null, {
        handleCompletion: aReason => {
          if (aReason === cps2.COMPLETE_ERROR) {
            aReject();
          } else {
            aResolve();
          }
        },
      });
    });
  },

  async deleteAll() {
    let cps2 = Cc["@mozilla.org/content-pref/service;1"].getService(
      Ci.nsIContentPrefService2
    );

    await new Promise((aResolve, aReject) => {
      cps2.removeAllDomains(null, {
        handleCompletion: aReason => {
          if (aReason === cps2.COMPLETE_ERROR) {
            aReject();
          } else {
            aResolve();
          }
        },
      });
    });
  },
};

const TlsTokenCacheCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    // Only propagate partitionKey into the OA pattern: TLS tokens are keyed
    // by host:port + full OA suffix, but we want to clear all tokens for a
    // given host regardless of container (userContextId) or other attributes.
    // The partitionKey is included so that clearing from a given first-party
    // site only affects tokens partitioned under that site.
    let pattern = {};
    if (aOriginAttributes.partitionKey) {
      pattern.partitionKey = aOriginAttributes.partitionKey;
    }
    lazy.sslTokensCache.removeSSLTokensByHostAndOriginAttributesPattern(
      aHost,
      JSON.stringify(pattern)
    );
  },

  async deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    lazy.sslTokensCache.removeSSLTokensBySiteAndOriginAttributesPattern(
      aSchemelessSite,
      JSON.stringify(aOriginAttributesPattern)
    );
  },

  async deleteAll() {
    lazy.sslTokensCache.clearSSLExternalAndInternalSessionCache();
  },
};

const ClientAuthRememberCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    let cars = Cc[
      "@mozilla.org/security/clientAuthRememberService;1"
    ].getService(Ci.nsIClientAuthRememberService);

    cars.deleteDecisionsByHost(aHost, aOriginAttributes);
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    let cars = Cc[
      "@mozilla.org/security/clientAuthRememberService;1"
    ].getService(Ci.nsIClientAuthRememberService);

    cars
      .getDecisions()
      .filter(({ asciiHost, entryKey }) => {
        // Get the origin attributes which are in the third component of the
        // entryKey. ',' is used as the delimiter.
        let originSuffixEncoded = entryKey.split(",")[2];
        let originAttributes;

        if (originSuffixEncoded) {
          try {
            // Decoding the suffix or parsing the origin attributes can fail. In
            // this case we won't match the partitionKey, but we can still match
            // the asciiHost.
            let originSuffix = decodeURIComponent(originSuffixEncoded);
            originAttributes =
              ChromeUtils.CreateOriginAttributesFromOriginSuffix(originSuffix);
          } catch (e) {
            console.error(e);
          }
        }

        return hasSite(
          {
            host: asciiHost,
            originAttributes,
          },
          aSchemelessSite,
          aOriginAttributesPattern
        );
      })
      .forEach(({ entryKey }) => cars.forgetRememberedDecision(entryKey));
  },

  async deleteAll() {
    let cars = Cc[
      "@mozilla.org/security/clientAuthRememberService;1"
    ].getService(Ci.nsIClientAuthRememberService);
    cars.clearRememberedDecisions();
  },
};

const HSTSCleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    let sss = Cc["@mozilla.org/ssservice;1"].getService(
      Ci.nsISiteSecurityService
    );
    let uri = Services.io.newURI("https://" + aHost);
    sss.resetState(
      uri,
      aOriginAttributes,
      Ci.nsISiteSecurityService.RootDomain
    );
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, _aOriginAttributesPattern) {
    // TODO: aOriginAttributesPattern.
    let sss = Cc["@mozilla.org/ssservice;1"].getService(
      Ci.nsISiteSecurityService
    );

    // Add brackets to IPv6 sites to ensure URI creation succeeds.
    let uri = Services.io.newURI("https://" + aSchemelessSite);
    sss.resetState(uri, {}, Ci.nsISiteSecurityService.BaseDomain);
  },

  async deleteAll() {
    // Clear site security settings - no support for ranges in this
    // interface either, so we clearAll().
    let sss = Cc["@mozilla.org/ssservice;1"].getService(
      Ci.nsISiteSecurityService
    );
    sss.clearAll();
  },
};

const EMECleaner = {
  async deleteByHost(aHost, aOriginAttributes) {
    let mps = Cc["@mozilla.org/gecko-media-plugin-service;1"].getService(
      Ci.mozIGeckoMediaPluginChromeService
    );
    mps.forgetThisSite(aHost, JSON.stringify(aOriginAttributes));
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  async deleteBySite(aSchemelessSite, _aOriginAttributesPattern) {
    // TODO: aOriginAttributesPattern.
    let mps = Cc["@mozilla.org/gecko-media-plugin-service;1"].getService(
      Ci.mozIGeckoMediaPluginChromeService
    );
    mps.forgetThisBaseDomain(aSchemelessSite);
  },

  deleteAll() {
    // Not implemented.
    return Promise.resolve();
  },
};

const ReportsCleaner = {
  deleteByHost(aHost) {
    // Also clears subdomains of aHost.
    return new Promise(aResolve => {
      Services.obs.notifyObservers(null, "reporting:purge-host", aHost);
      aResolve();
    });
  },

  deleteByPrincipal(aPrincipal) {
    return this.deleteByHost(aPrincipal.host, aPrincipal.originAttributes);
  },

  deleteBySite(aSchemelessSite, _aOriginAttributesPattern) {
    // TODO: aOriginAttributesPattern.
    return this.deleteByHost(aSchemelessSite, {});
  },

  deleteAll() {
    return new Promise(aResolve => {
      Services.obs.notifyObservers(null, "reporting:purge-all");
      aResolve();
    });
  },
};

const ContentBlockingCleaner = {
  deleteAll() {
    return lazy.TrackingDBService.clearAll();
  },

  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  deleteByRange(aFrom) {
    return lazy.TrackingDBService.clearSince(aFrom);
  },
};

/**
 * The about:home startup cache, if it exists, might contain information
 * about where the user has been, or what they've downloaded.
 */
const AboutHomeStartupCacheCleaner = {
  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  deleteAll() {
    // This cleaner only makes sense on Firefox desktop, which is the only
    // application that uses the about:home startup cache.
    if (!AppConstants.MOZ_BUILD_APP == "browser") {
      return Promise.resolve();
    }

    return new Promise((aResolve, aReject) => {
      let lci = Services.loadContextInfo.default;
      let storage = Services.cache2.diskCacheStorage(lci);
      let uri = Services.io.newURI("about:home");
      try {
        storage.asyncDoomURI(uri, "", {
          onCacheEntryDoomed(aResult) {
            if (
              Components.isSuccessCode(aResult) ||
              aResult == Cr.NS_ERROR_NOT_AVAILABLE
            ) {
              aResolve();
            } else {
              aReject({
                message: "asyncDoomURI for about:home failed",
              });
            }
          },
        });
      } catch (e) {
        aReject({
          message: "Failed to doom about:home startup cache entry",
        });
      }
    });
  },
};

const PreflightCacheCleaner = {
  // TODO: Bug 1727141: We should call the cache to clear by principal, rather
  // than over-clearing for user requests or bailing out for programmatic calls.
  async deleteByPrincipal(aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  // TODO: Bug 1727141 (see deleteByPrincipal).
  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteAll() {
    Cc[`@mozilla.org/network/protocol;1?name=http`]
      .getService(Ci.nsIHttpProtocolHandler)
      .clearCORSPreflightCache();
  },
};

const GeolocationCacheCleaner = {
  async deleteByPrincipal(_aPrincipal, aIsUserRequest) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteBySite(
    _aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    if (!aIsUserRequest) {
      return;
    }
    await this.deleteAll();
  },

  async deleteAll() {
    lazy.NetworkGeolocationCache.clear();
  },
};

const IdentityCredentialStorageCleaner = {
  async deleteAll() {
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      lazy.IdentityCredentialStorageService.clear();
    }
  },

  async deleteByPrincipal(aPrincipal) {
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      lazy.IdentityCredentialStorageService.deleteFromPrincipal(aPrincipal);
    }
  },

  async deleteBySite(
    aSchemelessSite,
    _aOriginAttributesPattern,
    aIsUserRequest
  ) {
    // TODO: aOriginAttributesPattern.
    if (!aIsUserRequest) {
      return;
    }
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      lazy.IdentityCredentialStorageService.deleteFromBaseDomain(
        aSchemelessSite
      );
    }
  },

  async deleteByRange(aFrom, aTo) {
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      lazy.IdentityCredentialStorageService.deleteFromTimeRange(aFrom, aTo);
    }
  },

  async deleteByHost(aHost, aOriginAttributes) {
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      // Delete data from both HTTP and HTTPS sites.
      let httpURI = Services.io.newURI("http://" + aHost);
      let httpsURI = Services.io.newURI("https://" + aHost);
      let httpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
        httpURI,
        aOriginAttributes
      );
      let httpsPrincipal =
        Services.scriptSecurityManager.createContentPrincipal(
          httpsURI,
          aOriginAttributes
        );
      lazy.IdentityCredentialStorageService.deleteFromPrincipal(httpPrincipal);
      lazy.IdentityCredentialStorageService.deleteFromPrincipal(httpsPrincipal);
    }
  },

  async deleteByOriginAttributes(aOriginAttributesString) {
    if (
      Services.prefs.getBoolPref(
        "dom.security.credentialmanagement.identity.enabled",
        false
      )
    ) {
      lazy.IdentityCredentialStorageService.deleteFromOriginAttributesPattern(
        aOriginAttributesString
      );
    }
  },
};

const BounceTrackingProtectionStateCleaner = {
  async deleteAll() {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    lazy.bounceTrackingProtection.clearAll();
  },

  async deleteByPrincipal(aPrincipal) {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    let { baseDomain, originAttributes } = aPrincipal;
    lazy.bounceTrackingProtection.clearBySiteHostAndOriginAttributes(
      baseDomain,
      originAttributes
    );
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    lazy.bounceTrackingProtection.clearBySiteHostAndOriginAttributesPattern(
      aSchemelessSite,
      aOriginAttributesPattern
    );
  },

  async deleteByRange(aFrom, aTo) {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    lazy.bounceTrackingProtection.clearByTimeRange(aFrom, aTo);
  },

  async deleteByHost(aHost, aOriginAttributesPattern = {}) {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    let baseDomain = Services.eTLD.getSchemelessSiteFromHost(aHost);
    lazy.bounceTrackingProtection.clearBySiteHostAndOriginAttributesPattern(
      baseDomain,
      aOriginAttributesPattern
    );
  },

  async deleteByOriginAttributes(aOriginAttributesPatternString) {
    if (
      lazy.bounceTrackingProtectionMode ==
      Ci.nsIBounceTrackingProtection.MODE_DISABLED
    ) {
      return;
    }
    lazy.bounceTrackingProtection.clearByOriginAttributesPattern(
      aOriginAttributesPatternString
    );
  },
};

const StoragePermissionsCleaner = {
  async deleteByRange(aFrom) {
    // We lack the ability to clear by range, but can clear from a certain time to now
    // Convert aFrom from microseconds to ms
    Services.perms.removeByTypeSince("storage-access", aFrom / 1000);

    await this._removePermissions(
      Services.perms
        .getAllByTypeSince("persistent-storage", aFrom / 1000)
        // If it is an Addon Principal, do nothing.
        // We want their persistant-storage permissions to remain (Bug 1907732)
        .filter(perm => !this._isAddonPrincipal(perm.principal))
    );
  },

  async deleteByPrincipal(aPrincipal) {
    Services.perms.removeFromPrincipal(aPrincipal, "storage-access");

    // Only remove persistent-storage if it is not an extension principal (Bug 1907732)
    if (!this._isAddonPrincipal(aPrincipal)) {
      Services.perms.removeFromPrincipal(aPrincipal, "persistent-storage");
    }
  },

  async deleteByHost(aHost) {
    await this._removePermissions(
      this._getStoragePermissions().filter(perm =>
        Services.eTLD.hasRootDomain(perm.principal.host, aHost)
      )
    );
  },

  async deleteBySite(aSchemelessSite, aOriginAttributesPattern) {
    await this._removePermissions(
      this._getStoragePermissions(aOriginAttributesPattern).filter(
        ({ principal }) => hasSite({ principal }, aSchemelessSite)
      )
    );
  },

  async deleteByLocalFiles() {
    await this._removePermissions(
      this._getStoragePermissions().filter(perm =>
        perm.principal.schemeIs("file")
      )
    );
  },

  async deleteAll() {
    Services.perms.removeByType("storage-access");

    // We don't want to clear the persistent-storage permission from addons (Bug 1907732)
    await this._removePermissions(
      Services.perms
        .getAllByTypes(["persistent-storage"])
        .filter(perm => !this._isAddonPrincipal(perm.principal))
    );
  },

  async _removePermissions(aPermissions) {
    try {
      Services.perms.removePermissionsExceptDefaults(aPermissions);
    } finally {
      // Clean up interaction tracking data for removed permissions.
      await Services.perms.removeOrphanedInteractionRecords();
    }
  },

  _getStoragePermissions(aOriginAttributesPattern = {}) {
    let storagePermissions = Services.perms.getAllForOriginAttributesPattern(
      JSON.stringify(aOriginAttributesPattern),
      ["storage-access", "persistent-storage"]
    );

    return storagePermissions.filter(
      permission =>
        !this._isAddonPrincipal(permission.principal) ||
        permission.type == "storage-access"
    );
  },

  _isAddonPrincipal(aPrincipal) {
    return (
      // AddonPolicy() returns a WebExtensionPolicy that has been registered before,
      // typically during extension startup. Since Disabled or uninstalled add-ons
      // don't appear there, we should use schemeIs instead
      lazy.ExtensionUtils.isExtensionUrl(aPrincipal)
    );
  },
};

const BfcacheCleaner = {
  async deleteAll() {
    // TODO(Bug 1959477)
  },

  async deleteBySite(_aSchemelessSite, _aOriginAttributesPattern) {
    // TODO(Bug 1959477)
  },

  async deleteByPrincipal(aPrincipal) {
    ChromeUtils.clearBfcacheByPrincipal(aPrincipal);
  },
};

// Here the map of Flags-Cleaners.
const FLAGS_MAP = [
  {
    flag: Ci.nsIClearDataService.CLEAR_CERT_EXCEPTIONS,
    cleaners: [CertCleaner],
  },

  { flag: Ci.nsIClearDataService.CLEAR_COOKIES, cleaners: [CookieCleaner] },

  {
    flag: Ci.nsIClearDataService.CLEAR_NETWORK_CACHE,
    cleaners: [NetworkCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_IMAGE_CACHE,
    cleaners: [ImageCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_CSS_CACHE,
    cleaners: [CSSCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_MESSAGING_LAYER_SECURITY_STATE,
    cleaners: [MessagingLayerSecurityStateCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_JS_CACHE,
    cleaners: [JSCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_CLIENT_AUTH_REMEMBER_SERVICE,
    cleaners: [ClientAuthRememberCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_TLS_TOKEN_CACHE,
    cleaners: [TlsTokenCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_DOWNLOADS,
    cleaners: [DownloadsCleaner, AboutHomeStartupCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_MEDIA_DEVICES,
    cleaners: [MediaDevicesCleaner],
  },

  { flag: Ci.nsIClearDataService.CLEAR_DOM_QUOTA, cleaners: [QuotaCleaner] },

  {
    flag: Ci.nsIClearDataService.CLEAR_DOM_PUSH_NOTIFICATIONS,
    cleaners: [PushNotificationsCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_HISTORY,
    cleaners: [
      HistoryCleaner,
      SessionHistoryCleaner,
      AboutHomeStartupCacheCleaner,
    ],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_AUTH_TOKENS,
    cleaners: [AuthTokensCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_AUTH_CACHE,
    cleaners: [AuthCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_SITE_PERMISSIONS,
    cleaners: [PermissionsCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_CONTENT_PREFERENCES,
    cleaners: [PreferencesCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_HSTS,
    cleaners: [HSTSCleaner],
  },

  { flag: Ci.nsIClearDataService.CLEAR_EME, cleaners: [EMECleaner] },

  { flag: Ci.nsIClearDataService.CLEAR_REPORTS, cleaners: [ReportsCleaner] },

  {
    flag: Ci.nsIClearDataService.CLEAR_STORAGE_ACCESS,
    cleaners: [StorageAccessCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_CONTENT_BLOCKING_RECORDS,
    cleaners: [ContentBlockingCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_PREFLIGHT_CACHE,
    cleaners: [PreflightCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_GEOLOCATION_CACHE,
    cleaners: [GeolocationCacheCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_CREDENTIAL_MANAGER_STATE,
    cleaners: [IdentityCredentialStorageCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_FINGERPRINTING_PROTECTION_STATE,
    cleaners: [FingerprintingProtectionStateCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE,
    cleaners: [BounceTrackingProtectionStateCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_STORAGE_PERMISSIONS,
    cleaners: [StoragePermissionsCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_SHUTDOWN_EXCEPTIONS,
    cleaners: [ShutdownExceptionsCleaner],
  },

  {
    flag: Ci.nsIClearDataService.CLEAR_BFCACHE,
    cleaners: [BfcacheCleaner],
  },
];

// Returns the nsIClearDataService.CLEAR_* constant name matching a single flag
// value, for use in log messages. Falls back to the hexadecimal value.
function flagToName(aFlag) {
  return (
    Object.entries(Ci.nsIClearDataService).find(
      ([key, value]) => key.startsWith("CLEAR_") && value === aFlag
    )?.[0] ?? `0x${aFlag.toString(16)}`
  );
}

export function ClearDataService() {
  this._initialize();
}

ClearDataService.prototype = Object.freeze({
  classID: Components.ID("{0c06583d-7dd8-4293-b1a5-912205f779aa}"),
  QueryInterface: ChromeUtils.generateQI(["nsIClearDataService"]),

  _initialize() {
    // Let's start all the service we need to cleanup data.

    // This is mainly needed for GeckoView that doesn't start QMS on startup
    // time.
    if (!Services.qms) {
      console.error("Failed initializiation of QuotaManagerService.");
    }
  },

  deleteDataFromLocalFiles(aIsUserRequest, aFlags, aCallback) {
    if (!aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner => {
      // Some of the 'Cleaners' do not support clearing data for
      // local files. Ignore those.
      if (aCleaner.deleteByLocalFiles) {
        // A generic originAttributes dictionary.
        return aCleaner.deleteByLocalFiles({});
      }
      return Promise.resolve();
    });
  },

  deleteDataFromHost(aHost, aIsUserRequest, aFlags, aCallback) {
    if (!aHost || !aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner => {
      // Some of the 'Cleaners' do not support to delete by principal. Let's
      // use deleteAll() as fallback.
      if (aCleaner.deleteByHost) {
        // A generic originAttributes dictionary.
        return aCleaner.deleteByHost(aHost, {});
      }
      // The user wants to delete data. Let's remove as much as we can.
      if (aIsUserRequest) {
        return aCleaner.deleteAll();
      }
      // We don't want to delete more than what is strictly required.
      return Promise.resolve();
    });
  },

  deleteDataFromSite(
    aSchemelessSite,
    aOriginAttributesPattern,
    aIsUserRequest,
    aFlags,
    aCallback
  ) {
    if (!aSchemelessSite?.length || !aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    // For debug builds validate aSchemelessSite.
    if (AppConstants.DEBUG) {
      let schemelessSiteComputed =
        Services.eTLD.getSchemelessSiteFromHost(aSchemelessSite);
      if (schemelessSiteComputed != aSchemelessSite) {
        throw new Error(
          `deleteDataFromSite called with invalid aSchemelessSite '${aSchemelessSite}'. Expected site is '${schemelessSiteComputed}'`
        );
      }
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner =>
      aCleaner.deleteBySite(
        aSchemelessSite,
        aOriginAttributesPattern,
        aIsUserRequest
      )
    );
  },

  deleteDataFromSiteAndOriginAttributesPatternString(
    aSchemelessSite,
    aOriginAttributesPatternString,
    aIsUserRequest,
    aFlags,
    aCallback
  ) {
    if (!aSchemelessSite || !aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    // Parse the pattern string.
    let originAttributesPattern = {};
    if (aOriginAttributesPatternString?.length) {
      originAttributesPattern = JSON.parse(aOriginAttributesPatternString);
    }

    // Call the other variant which expects a OriginAttributesPattern object.
    return this.deleteDataFromSite(
      aSchemelessSite,
      originAttributesPattern,
      aIsUserRequest,
      aFlags,
      aCallback
    );
  },

  deleteDataFromPrincipal(aPrincipal, aIsUserRequest, aFlags, aCallback) {
    if (!aPrincipal || !aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner =>
      aCleaner.deleteByPrincipal(aPrincipal, aIsUserRequest)
    );
  },

  deleteDataInTimeRange(aFrom, aTo, aIsUserRequest, aFlags, aCallback) {
    if (aFrom > aTo || !aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner => {
      // Some of the 'Cleaners' do not support to delete by range. Let's use
      // deleteAll() as fallback.
      if (aCleaner.deleteByRange) {
        return aCleaner.deleteByRange(aFrom, aTo);
      }
      // The user wants to delete data. Let's remove as much as we can.
      if (aIsUserRequest) {
        return aCleaner.deleteAll();
      }
      // We don't want to delete more than what is strictly required.
      return Promise.resolve();
    });
  },

  deleteData(aFlags, aCallback) {
    if (!aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    return this._deleteInternal(aFlags, aCallback, aCleaner => {
      return aCleaner.deleteAll();
    });
  },

  deleteDataFromOriginAttributesPattern(aPattern, aCallback) {
    if (!aPattern) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    let patternString = JSON.stringify(aPattern);
    // XXXtt remove clear-origin-attributes-data entirely
    Services.obs.notifyObservers(
      null,
      "clear-origin-attributes-data",
      patternString
    );

    if (!aCallback) {
      aCallback = {
        onDataDeleted: () => {},
      };
    }
    return this._deleteInternal(
      Ci.nsIClearDataService.CLEAR_ALL,
      aCallback,
      aCleaner => {
        if (aCleaner.deleteByOriginAttributes) {
          return aCleaner.deleteByOriginAttributes(patternString);
        }

        // We don't want to delete more than what is strictly required.
        return Promise.resolve();
      }
    );
  },

  deleteUserInteractionForClearingHistory(
    aPrincipalsWithStorage,
    aFrom,
    aCallback
  ) {
    if (!aCallback) {
      return Cr.NS_ERROR_INVALID_ARG;
    }

    StorageAccessCleaner.deleteExceptPrincipals(aPrincipalsWithStorage, aFrom)
      .then(() => {
        aCallback.onDataDeleted(0);
      })
      .catch(() => {
        // This is part of clearing storageAccessAPI permissions, thus return
        // an appropriate error flag.
        aCallback.onDataDeleted(Ci.nsIClearDataService.CLEAR_PERMISSIONS);
      });
    return Cr.NS_OK;
  },

  cleanupAfterDeletionAtShutdown(aFlags, aCallback) {
    return this._deleteInternal(aFlags, aCallback, async aCleaner => {
      if (aCleaner.cleanupAfterDeletionAtShutdown) {
        await aCleaner.cleanupAfterDeletionAtShutdown();
      }
    });
  },

  clearPrivateBrowsingData(aCallback) {
    if (gPBMCleanupInProgress) {
      throw Components.Exception(
        "PBM cleanup already in progress",
        Cr.NS_ERROR_ABORT
      );
    }

    if (!aCallback) {
      aCallback = {
        onDataDeleted() {},
      };
    }

    gPBMCleanupInProgress = true;

    let timerId = Glean.privateBrowsingCleanup.duration.start();
    let collector = new PBMCleanupCollector();

    // Fire the notification with collector as subject. Sync observers
    // complete immediately. Async observers (Quota Manager, Downloads)
    // QI the subject and call addPendingCleanup() to register their
    // async operations. When the natural PBM exit path fires this
    // notification with null subject, observers fire-and-forget as before.
    Services.obs.notifyObservers(collector, "last-pb-context-exited");

    collector.promise
      .then(hadFailures => {
        gPBMCleanupInProgress = false;
        Glean.privateBrowsingCleanup.duration.stopAndAccumulate(timerId);
        Glean.privateBrowsingCleanup.errorRate.addToDenominator(1);
        if (hadFailures) {
          Glean.privateBrowsingCleanup.errorRate.addToNumerator(1);
          console.error("PBM cleanup: one or more observers reported failure");
        }
        aCallback.onDataDeleted(hadFailures ? 1 : 0);
      })
      .catch(e => {
        gPBMCleanupInProgress = false;
        Glean.privateBrowsingCleanup.duration.cancel(timerId);
        Glean.privateBrowsingCleanup.errorRate.addToDenominator(1);
        Glean.privateBrowsingCleanup.errorRate.addToNumerator(1);
        console.error("PBM cleanup error:", e);
        aCallback.onDataDeleted(1);
      });

    return Cr.NS_OK;
  },

  hostMatchesSite(
    aHost,
    aOriginAttributes,
    aSchemelessSite,
    aOriginAttributesPattern = {}
  ) {
    return hasSite(
      { host: aHost, originAttributes: aOriginAttributes },
      aSchemelessSite,
      aOriginAttributesPattern
    );
  },

  // This internal method uses aFlags against FLAGS_MAP in order to retrieve a
  // list of 'Cleaners'. For each of them, the aHelper callback retrieves a
  // promise object. All these promise objects are resolved before calling
  // onDataDeleted.
  _deleteInternal(aFlags, aCallback, aHelper) {
    let resultFlags = 0;
    let promises = FLAGS_MAP.filter(c => aFlags & c.flag).map(c => {
      return Promise.all(
        c.cleaners.map(cleaner => {
          return aHelper(cleaner).catch(e => {
            console.error(
              `ClearDataService failed to clear ${flagToName(c.flag)}:`,
              e
            );
            resultFlags |= c.flag;
          });
        })
      );
      // Let's collect the failure in resultFlags.
    });
    Promise.all(promises).then(() => {
      aCallback.onDataDeleted(resultFlags);
    });
    return Cr.NS_OK;
  },
});
