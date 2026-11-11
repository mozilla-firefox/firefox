/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  // This helper contains the platform-specific bits of browsingData.
  BrowsingDataDelegate: "resource:///modules/ExtensionBrowsingData.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  ServiceWorkerCleanUp:
    "moz-src:///toolkit/components/cleardata/ServiceWorkerCleanUp.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

/**
 * A number of iterations after which to yield time back
 * to the system.
 */
const YIELD_PERIOD = 10;

/**
 * Convert a Date object to a PRTime (microseconds).
 *
 * @param {Date} date
 *        the Date object to convert.
 * @returns {integer} microseconds from the epoch.
 */
const toPRTime = date => {
  if (typeof date != "number" && date.constructor.name != "Date") {
    throw new Error("Invalid value passed to toPRTime");
  }
  return date * 1000;
};

const makeRange = options => {
  return options.since == null
    ? null
    : [toPRTime(options.since), toPRTime(Date.now())];
};
global.makeRange = makeRange;

// General implementation for clearing data using Services.clearData.
// Currently Sanitizer.items uses this under the hood.
async function clearData(options, flags) {
  if (options.hostnames) {
    await Promise.all(
      options.hostnames.map(
        host =>
          new Promise(resolve => {
            // Set aIsUserRequest to true. This means when the ClearDataService
            // "Cleaner" implementation doesn't support clearing by host
            // it will delete all data instead.
            // This is appropriate for cases like |cache|, which doesn't
            // support clearing by a time range.
            // In future when we use this for other data types, we have to
            // evaluate if that behavior is still acceptable.
            Services.clearData.deleteDataFromHost(host, true, flags, resolve);
          })
      )
    );
    return;
  }

  if (options.since) {
    const range = makeRange(options);
    await new Promise(resolve => {
      Services.clearData.deleteDataInTimeRange(...range, true, flags, resolve);
    });
    return;
  }

  // Don't return the promise here and above to prevent leaking the resolved
  // value.
  await new Promise(resolve => Services.clearData.deleteData(flags, resolve));
}

const clearCache = options => {
  return clearData(options, Ci.nsIClearDataService.CLEAR_ALL_CACHES);
};

const clearCookies = async function (options) {
  let cookieMgr = Services.cookies;
  // This code has been borrowed from Sanitizer.sys.mjs.
  let yieldCounter = 0;

  if (options.since || options.hostnames || options.cookieStoreId) {
    // Iterate through the cookies and delete any created after our cutoff.
    let cookies = cookieMgr.cookies;
    if (
      !options.cookieStoreId ||
      isPrivateCookieStoreId(options.cookieStoreId)
    ) {
      // By default nsICookieManager.cookies doesn't contain private cookies.
      const privateCookies = cookieMgr.getCookiesWithOriginAttributes(
        JSON.stringify({
          privateBrowsingId: 1,
        })
      );
      cookies = cookies.concat(privateCookies);
    }
    for (const cookie of cookies) {
      if (
        (!options.since || cookie.creationTime >= toPRTime(options.since)) &&
        (!options.hostnames ||
          options.hostnames.includes(cookie.host.replace(/^\./, ""))) &&
        (!options.cookieStoreId ||
          getCookieStoreIdForOriginAttributes(cookie.originAttributes) ===
            options.cookieStoreId)
      ) {
        // This cookie was created after our cutoff, clear it.
        cookieMgr.remove(
          cookie.host,
          cookie.name,
          cookie.path,
          cookie.originAttributes
        );

        if (++yieldCounter % YIELD_PERIOD == 0) {
          await new Promise(resolve => setTimeout(resolve, 0)); // Don't block the main thread too long.
        }
      }
    }
  } else {
    // Remove everything.
    cookieMgr.removeAll();
  }
};

// Ideally we could reuse the logic in Sanitizer.sys.mjs or nsIClearDataService,
// but this API exposes an ability to wipe data at a much finger granularity
// than those APIs. (See also Bug 1531276)
async function clearQuotaManager(options, dataType) {
  // Can not clear localStorage/indexedDB in private browsing mode,
  // just ignore.
  if (options.cookieStoreId == PRIVATE_STORE) {
    return;
  }

  let promises = [];
  await new Promise((resolve, reject) => {
    Services.qms.getUsage(request => {
      if (request.resultCode != Cr.NS_OK) {
        reject({ message: `Clear ${dataType} failed` });
        return;
      }

      for (let item of request.result) {
        let principal =
          Services.scriptSecurityManager.createContentPrincipalFromOrigin(
            item.origin
          );

        // Consistently to removeIndexedDB and the API documentation for
        // removeLocalStorage, we should only clear the data stored by
        // regular websites, on the contrary we shouldn't clear data stored
        // by browser components (like about:newtab) or other extensions.
        if (!["http", "https", "file"].includes(principal.scheme)) {
          continue;
        }

        let host = principal.hostPort;
        if (
          (!options.hostnames || options.hostnames.includes(host)) &&
          (!options.cookieStoreId ||
            getCookieStoreIdForOriginAttributes(principal.originAttributes) ===
              options.cookieStoreId)
        ) {
          promises.push(
            new Promise((resolve, reject) => {
              let clearRequest;
              if (dataType === "indexedDB") {
                clearRequest = Services.qms.clearStoragesForClient(
                  principal,
                  "idb"
                );
              } else {
                clearRequest = Services.qms.clearStoragesForClient(
                  principal,
                  "ls",
                  "default"
                );
              }

              clearRequest.callback = () => {
                if (clearRequest.resultCode == Cr.NS_OK) {
                  resolve();
                } else {
                  reject({ message: `Clear ${dataType} failed` });
                }
              };
            })
          );
        }
      }

      resolve();
    });
  });

  return Promise.all(promises);
}

const clearIndexedDB = async function (options) {
  return clearQuotaManager(options, "indexedDB");
};

const clearLocalStorage = async function (options) {
  if (options.since) {
    return Promise.reject({
      message: "Firefox does not support clearing localStorage with 'since'.",
    });
  }
  const notifySessionStorage = function (hostname, cookieStoreId) {
    if (hostname || cookieStoreId) {
      const entry = Cc["@mozilla.org/clear-by-site-entry;1"].createInstance(
        Ci.nsIClearBySiteEntry
      );

      entry.schemelessSite = hostname || "";
      entry.patternJSON = cookieStoreId
        ? JSON.stringify(
            getOriginAttributesPatternForCookieStoreId(cookieStoreId)
          )
        : "";

      Services.obs.notifyObservers(entry, "extension:purge-sessionStorage");
    } else {
      Services.obs.notifyObservers(null, "extension:purge-sessionStorage");
    }
  };

  // The legacy LocalStorage implementation that will eventually be removed
  // depends on this observer notification.  Some other subsystems like
  // Reporting headers depend on this too.
  // When NextGenLocalStorage is enabled these notifications are ignored.
  if (options.hostnames) {
    for (let hostname of options.hostnames) {
      Services.obs.notifyObservers(
        null,
        "extension:purge-localStorage",
        hostname
      );

      notifySessionStorage(hostname, options.cookieStoreId);
    }
  } else {
    Services.obs.notifyObservers(null, "extension:purge-localStorage");
    notifySessionStorage(null, options.cookieStoreId);
  }

  if (Services.domStorageManager.nextGenLocalStorageEnabled) {
    return clearQuotaManager(options, "localStorage");
  }
};

const clearPasswords = async function (options) {
  let yieldCounter = 0;

  // Iterate through the logins and delete any updated after our cutoff.
  for (let login of await LoginHelper.getAllUserFacingLogins()) {
    login.QueryInterface(Ci.nsILoginMetaInfo);
    if (!options.since || login.timePasswordChanged >= options.since) {
      await Services.logins.removeLoginAsync(login);
      // TODO Bug 2014169: cleanup clearPassword workaround introduced to prevent Services.logins.removeLogin from blocking the main thread for too long
      if (++yieldCounter % YIELD_PERIOD == 0) {
        await new Promise(resolve => setTimeout(resolve, 0)); // Don't block the main thread too long.
      }
    }
  }
};

const clearServiceWorkers = options => {
  if (!options.hostnames) {
    return ServiceWorkerCleanUp.removeAll();
  }

  return Promise.all(
    options.hostnames.map(host => {
      return ServiceWorkerCleanUp.removeFromHost(host);
    })
  );
};

class BrowsingDataImpl {
  constructor(extension) {
    this.extension = extension;
    // Some APIs cannot implement in a platform-independent way and they are
    // delegated to a platform-specific delegate.
    this.platformDelegate = new BrowsingDataDelegate(extension);
  }

  handleRemoval(dataType, options) {
    // First, let's see if the platform implements this
    let result = this.platformDelegate.handleRemoval(dataType, options);
    if (result !== undefined) {
      return result;
    }

    // ... if not, run the default behavior.
    switch (dataType) {
      case "cache":
        return clearCache(options);
      case "cookies":
        return clearCookies(options);
      case "indexedDB":
        return clearIndexedDB(options);
      case "localStorage":
        return clearLocalStorage(options);
      case "passwords":
        return clearPasswords(options);
      case "pluginData":
        this.extension?.logger.warn(
          "pluginData has been deprecated (along with Flash plugin support)"
        );
        return Promise.resolve();
      case "serviceWorkers":
        return clearServiceWorkers(options);
      default:
        return undefined;
    }
  }

  doRemoval(options, dataToRemove) {
    if (
      options.originTypes &&
      (options.originTypes.protectedWeb || options.originTypes.extension)
    ) {
      return Promise.reject({
        message:
          "Firefox does not support protectedWeb or extension as originTypes.",
      });
    }

    if (options.cookieStoreId) {
      const SUPPORTED_TYPES = ["cookies", "indexedDB"];
      if (Services.domStorageManager.nextGenLocalStorageEnabled) {
        // Only the next-gen storage supports removal by cookieStoreId.
        SUPPORTED_TYPES.push("localStorage");
      }

      for (let dataType in dataToRemove) {
        if (dataToRemove[dataType] && !SUPPORTED_TYPES.includes(dataType)) {
          return Promise.reject({
            message: `Firefox does not support clearing ${dataType} with 'cookieStoreId'.`,
          });
        }
      }

      if (
        !isPrivateCookieStoreId(options.cookieStoreId) &&
        !isDefaultCookieStoreId(options.cookieStoreId) &&
        !getContainerForCookieStoreId(options.cookieStoreId)
      ) {
        return Promise.reject({
          message: `Invalid cookieStoreId: ${options.cookieStoreId}`,
        });
      }
    }

    let removalPromises = [];
    let invalidDataTypes = [];
    for (let dataType in dataToRemove) {
      if (dataToRemove[dataType]) {
        let result = this.handleRemoval(dataType, options);
        if (result === undefined) {
          invalidDataTypes.push(dataType);
        } else {
          removalPromises.push(result);
        }
      }
    }
    if (invalidDataTypes.length) {
      this.extension.logger.warn(
        `Firefox does not support dataTypes: ${invalidDataTypes.toString()}.`
      );
    }
    return Promise.all(removalPromises);
  }

  settings() {
    return this.platformDelegate.settings();
  }
}

this.browsingData = class extends ExtensionAPI {
  getAPI(context) {
    const impl = new BrowsingDataImpl(context.extension);
    return {
      browsingData: {
        settings() {
          return impl.settings();
        },
        remove(options, dataToRemove) {
          return impl.doRemoval(options, dataToRemove);
        },
        removeCache(options) {
          return impl.doRemoval(options, { cache: true });
        },
        removeCookies(options) {
          return impl.doRemoval(options, { cookies: true });
        },
        removeDownloads(options) {
          return impl.doRemoval(options, { downloads: true });
        },
        removeFormData(options) {
          return impl.doRemoval(options, { formData: true });
        },
        removeHistory(options) {
          return impl.doRemoval(options, { history: true });
        },
        removeIndexedDB(options) {
          return impl.doRemoval(options, { indexedDB: true });
        },
        removeLocalStorage(options) {
          return impl.doRemoval(options, { localStorage: true });
        },
        removePasswords(options) {
          return impl.doRemoval(options, { passwords: true });
        },
        removePluginData(options) {
          return impl.doRemoval(options, { pluginData: true });
        },
      },
    };
  }
};
