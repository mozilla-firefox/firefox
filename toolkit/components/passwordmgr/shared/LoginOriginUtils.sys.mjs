/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Utilities for working with login origins.
 * Shared across Firefox Desktop, Firefox Android, and Firefox iOS.
 * Used in both parent and child processes.
 */

class LoginOriginUtils {
  /**
   * Get the parts of the URL we want for identification.
   * Strip out things like the userPass portion and handle javascript:.
   */
  static getLoginOrigin(uriString, allowJS = false) {
    try {
      const mozProxyRegex = /^moz-proxy:\/\//i;
      if (mozProxyRegex.test(uriString)) {
        // Special handling for moz-proxy URIs
        const uri = new URL(uriString.replace(mozProxyRegex, "https://"));
        return `moz-proxy://${uri.host}`;
      }

      const uri = new URL(uriString);
      if (uri.protocol === "javascript:") {
        return allowJS ? "javascript:" : null;
      }

      // Ensure the URL has a host
      // Exception: file URIs See Bug 1651186
      return uri.host || uri.protocol === "file:"
        ? `${uri.protocol}//${uri.host}`
        : null;
    } catch {
      return null;
    }
  }
}

export { LoginOriginUtils };
