/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const SitePolicyUtils = {
  stripTrailingDots(host) {
    if (!host.endsWith(".")) {
      return host;
    }

    return host.replace(/\.+$/, "");
  },

  // Strips trailing dots from the URI host so that the fully-qualified form
  // of a hostname (e.g. "www.example.com.") matches the same policy rules as
  // the bare form ("www.example.com").
  normalizeURI(uri) {
    try {
      let host = uri.host;
      let bareHost = this.stripTrailingDots(host);
      if (bareHost == host) {
        return uri;
      }
      return uri.mutate().setHost(bareHost).finalize();
    } catch (e) {
      return uri;
    }
  },

  isAllowedForURI(manager, sitePolicies, feature, uri) {
    let normalized = this.normalizeURI(uri);

    // The first site policy that matches the domain and includes a setting
    // for the feature wins.
    for (let policies of sitePolicies) {
      if (
        policies.exceptions.matches(normalized) ||
        // This is necessary to correctly match moz-nullprincipal URIs.
        policies.exceptions.matchesAllWebUrls
      ) {
        continue;
      }

      if (
        !policies.match.matches(normalized) &&
        // This is necessary to correctly match moz-nullprincipal URIs.
        !policies.match.matchesAllWebUrls
      ) {
        continue;
      }

      if (feature in policies.features) {
        return policies.features[feature];
      }
    }

    // No site specific setting, fall back to the global setting.
    return manager.isAllowed(feature);
  },
};
