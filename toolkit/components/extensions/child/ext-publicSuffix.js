/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

XPCOMUtils.defineLazyServiceGetter(
  this,
  "idn",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);

var { ExtensionError } = ExtensionUtils;

const invalidHostnameError = hostname =>
  new ExtensionError(`Invalid hostname: ${hostname}`);

const removeIPv6Brackets = hostname =>
  hostname.startsWith("[") && hostname.endsWith("]") && hostname.includes(":")
    ? hostname.slice(1, -1)
    : hostname;

// Note: hostname should not be an IP address.
function ensureValidHostname(hostname) {
  // "*" is accepted by domainToASCII but should be invalid.
  if (hostname && !hostname.includes("*")) {
    try {
      return idn.domainToASCII(hostname);
    } catch {}
  }
  throw invalidHostnameError(hostname);
}

this.publicSuffix = class extends ExtensionAPI {
  getAPI() {
    return {
      publicSuffix: {
        isKnownSuffix: function (hostname) {
          let suffix;
          try {
            suffix = Services.eTLD.getKnownPublicSuffixFromHost(
              removeIPv6Brackets(hostname)
            );
          } catch (e) {
            if (e.result === Cr.NS_ERROR_HOST_IS_IP_ADDRESS) {
              if (hostname.includes(":") && !hostname.startsWith("[")) {
                // IPv6 address without brackets.
                throw invalidHostnameError(hostname);
              }
              return false;
            }
            throw invalidHostnameError(hostname);
          }

          return suffix === ensureValidHostname(hostname);
        },

        getKnownSuffix: function (hostname) {
          let suffix;
          try {
            suffix =
              Services.eTLD.getKnownPublicSuffixFromHost(
                removeIPv6Brackets(hostname)
              ) || null;
          } catch (e) {
            if (e.result === Cr.NS_ERROR_HOST_IS_IP_ADDRESS) {
              if (hostname.includes(":") && !hostname.startsWith("[")) {
                // IPv6 address without brackets.
                throw invalidHostnameError(hostname);
              }
              return null;
            }
            if (e.result !== Cr.NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
              throw invalidHostnameError(hostname);
            }
          }

          // Now that we know the hostname is not an IP address,
          // check for invalid characters
          ensureValidHostname(hostname);

          return suffix || null;
        },

        getDomain: function (hostname, options) {
          let domain;
          try {
            // Note: returned domain may have an unknown suffix
            domain =
              Services.eTLD.getBaseDomainFromHost(
                removeIPv6Brackets(hostname)
              ) || null;
          } catch (e) {
            if (e.result === Cr.NS_ERROR_HOST_IS_IP_ADDRESS) {
              if (hostname.includes(":") && !hostname.startsWith("[")) {
                // IPv6 address without brackets.
                throw invalidHostnameError(hostname);
              }
              return options.allowIPAddress ? hostname : null;
            }
            if (e.result !== Cr.NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
              throw invalidHostnameError(hostname);
            }
          }

          // Now that we know the hostname is not an IP address,
          // check for invalid characters
          const ascii = ensureValidHostname(hostname);
          if (!domain) {
            if (Services.eTLD.hasKnownPublicSuffixFromHost(ascii)) {
              if (!options.allowPlainSuffix) {
                return null;
              }
            } else if (!options.allowUnknownSuffix) {
              return null;
            }
            domain = ascii;
          } else if (
            !options.allowUnknownSuffix &&
            !Services.eTLD.hasKnownPublicSuffixFromHost(domain)
          ) {
            return null;
          }
          if (options.encoding === "display") {
            domain = idn.domainToDisplay(domain);
          }
          return domain;
        },
      },
    };
  }
};
