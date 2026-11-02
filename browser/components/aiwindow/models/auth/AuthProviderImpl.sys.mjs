/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Base class for the account backends used by AuthProvider. Subclasses
 * implement every method below.
 *
 * @fires AuthProviderImpl#"AuthProviderImpl:Logout"
 *  When the user signs out of their account.
 * @fires AuthProviderImpl#"AuthProviderImpl:FallbackNeeded"
 *  When this backend can no longer be used and AuthProvider should switch to
 *  another one.
 */
export class AuthProviderImpl extends EventTarget {
  /**
   * @returns {Promise<string|null>} The access token, or null if none could
   *   be obtained
   */
  async getToken() {
    throw new Error("Not implemented");
  }

  /**
   * Drops a token the server rejected, so the next getToken call fetches a
   * fresh one.
   *
   * @param {string} _token
   */
  async invalidateToken(_token) {
    throw new Error("Not implemented");
  }

  /**
   * @returns {Promise<boolean>} Whether the user is signed in
   */
  async isSignedIn() {
    throw new Error("Not implemented");
  }

  /**
   * @returns {Promise<boolean>} Whether the sign-in flow can be shown at all
   */
  async canSignIn() {
    throw new Error("Not implemented");
  }

  /**
   * Shows the sign-in flow.
   *
   * @param {Browser} _browser
   * @returns {Promise<boolean>} Whether the user signed in successfully
   */
  async signIn(_browser) {
    throw new Error("Not implemented");
  }
}
