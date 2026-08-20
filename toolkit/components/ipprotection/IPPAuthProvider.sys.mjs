/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The error vocabulary of the authentication layer.
 */
export const AUTH_ERRORS = Object.freeze({
  NO_AUTH_PROVIDER: "no_auth_provider", // No provider was installed
  NO_GPI_PROVIDER: "no_gpi_provider", // No Play Integrity provider was installed
  NO_GPI_TOKEN: "no_gpi_token", // Play Integrity returned no token
  ENROLLMENT_FAILED: "enrollment_failed", // Enrolling the user failed
  LOGIN_NEEDED: "login_needed", // No usable credentials
  UNAUTHORIZED: "unauthorized", // The credentials were rejected
  NOT_ENTITLED: "not_entitled", // Valid credentials, but no entitlement assigned to the user
  QUOTA_EXCEEDED: "quota_exceeded", // The bandwidth limit was reached
  REGION_UNAVAILABLE: "region_unavailable", // Unavailable for legal reasons
  SERVER_ERROR: "server_error", // The service failed to answer
  INVALID_RESPONSE: "invalid_response", // The response did not match the schema
  PARSE_ERROR: "parse_error", // The response could not be parsed
  UNEXPECTED_STATUS: "unexpected_status", // The HTTP status has no known meaning
});

/**
 * @typedef {(typeof AUTH_ERRORS)[keyof typeof AUTH_ERRORS]} AuthError
 */

/**
 * Base class for IPProtection authentication providers.
 *
 * Subclasses should override all methods to provide a real implementation.
 * The default implementations are safe no-ops that keep the service in an
 * unauthenticated/inactive state.
 */
export class IPPAuthProvider extends EventTarget {
  /** Returns whether the user is authenticated and ready to use the proxy. */
  get isReady() {
    return false;
  }

  /** Returns whether the user has a VPN subscription. */
  get hasUpgraded() {
    return false;
  }

  /** Returns true while enrollment or entitlement checks are in progress. */
  get isEnrolling() {
    return false;
  }

  /** Returns the maximum bytes allowed for the current entitlement, or null if unknown. */
  get maxBytes() {
    return null;
  }

  /**
   * Checks whether the user has upgraded their subscription since the last
   * known state and updates accordingly.
   *
   * @returns {Promise<void>}
   */
  async checkForUpgrade() {}

  /**
   * Enrolls and entitles the user.
   *
   * @returns {Promise<{isEnrolledAndEntitled: boolean, error?: AuthError}>}
   */
  async enroll() {
    throw new Error("enroll() must be implemented by subclasses");
  }

  /**
   * Called before the proxy starts. Should resolve enrollment and verify
   * entitlement. Returns an error object if the proxy should not start,
   * or null if everything is in order.
   *
   * @returns {Promise<{error?: AuthError} | null>}
   */
  async aboutToStart() {
    return { error: AUTH_ERRORS.NO_AUTH_PROVIDER };
  }

  /**
   * Fetches a new VPN node pass from the auth provider.
   *
   * Implementations must report an error whenever the status is not 200, so
   * that callers never have to derive a failure from the status alone.
   *
   * @param {AbortSignal} [_abortSignal]
   * @returns {Promise<{pass?: import("./GuardianTypes.sys.mjs").ProxyPass, usage?: import("./GuardianTypes.sys.mjs").ProxyUsage|null, status?: number, error?: AuthError}>}
   */
  async fetchProxyPass(_abortSignal) {
    return { error: AUTH_ERRORS.NO_AUTH_PROVIDER };
  }

  /**
   * Fetches the current VPN node usage without requesting a new pass.
   *
   * @param {AbortSignal} [_abortSignal]
   * @returns {Promise<import("./GuardianTypes.sys.mjs").ProxyUsage | null>}
   */
  async fetchProxyUsage(_abortSignal) {
    return null;
  }

  /**
   * Preference names whose values are URLs that should bypass the proxy.
   * Subclasses should override this to add auth-provider-specific exclusions.
   *
   * @returns {string[]}
   */
  get excludedUrlPrefs() {
    return [];
  }
}
