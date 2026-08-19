/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  jwcrypto: "moz-src:///services/crypto/modules/jwcrypto.sys.mjs",
});

import {
  ERROR_AUTH_ERROR,
  OAUTH_CLIENT_ID,
  SCOPE_APP_SYNC,
  log,
} from "resource://gre/modules/FxAccountsCommon.sys.mjs";

export const ERROR_INVALID_STATE = "INVALID_STATE";
export const ERROR_SYNC_SCOPE_NOT_GRANTED = "ERROR_SYNC_SCOPE_NOT_GRANTED";
export const ERROR_NO_KEYS_JWE = "ERROR_NO_KEYS_JWE";
export const ERROR_OAUTH_FLOW_ABANDONED = "ERROR_OAUTH_FLOW_ABANDONED";
export const ERROR_INVALID_SCOPED_KEYS = "ERROR_INVALID_SCOPED_KEYS";

/**
 * Handles all logic and state related to initializing, and completing OAuth flows
 * with FxA
 * It's possible to start multiple OAuth flow, but only one can be completed, and once one flow is completed
 * all the other in-flight flows will be concluded, and attempting to complete those flows will result in errors.
 */
export class FxAccountsOAuth {
  #flow;
  #fxaClient;
  #fxaKeys;
  /**
   * Creates a new FxAccountsOAuth
   *
   * @param {object} fxaClient: The fxa client used to send http request to the oauth server
   */
  constructor(fxaClient, fxaKeys) {
    this.#flow = {};
    this.#fxaClient = fxaClient;
    this.#fxaKeys = fxaKeys;
  }

  /**
   * Stores a flow in-memory
   *
   * @param { string } state: A base-64 URL-safe string represnting a random value created at the start of the flow
   * @param {object} value: The data needed to complete a flow, once the oauth code is available.
   * in practice, `value` is:
   *  - `verifier`: A base=64 URL-safe string representing the PKCE code verifier
   *  - `key`: The private key need to decrypt the JWE we recieve from the auth server
   *  - `requestedScopes`: The scopes the caller requested, meant to be compared against the scopes the server authorized
   */
  addFlow(state, value) {
    this.#flow[state] = value;
  }

  /**
   * Clears all started flows
   */
  clearAllFlows() {
    this.#flow = {};
  }

  /**
   * Gets a stored flow
   *
   * @param { string } state: The base-64 URL-safe state string that was created at the start of the flow
   * @returns {object}: The values initially stored when startign th eoauth flow
   * in practice, the return value is:
   *  - `verifier`: A base=64 URL-safe string representing the PKCE code verifier
   *  - `key`: The private key need to decrypt the JWE we recieve from the auth server
   *  - ``requestedScopes`: The scopes the caller requested, meant to be compared against the scopes the server authorized
   */
  getFlow(state) {
    return this.#flow[state];
  }

  /* Returns the number of flows, used by tests
   *
   */
  numOfFlows() {
    return Object.keys(this.#flow).length;
  }

  /**
   * Begins an OAuth flow, to be completed with a an OAuth code and state.
   *
   * This function stores needed information to complete the flow. You must call `completeOAuthFlow`
   * on the same instance of `FxAccountsOAuth`, otherwise the completing of the oauth flow will fail.
   *
   * @param { string[] } scopes: The OAuth scopes the client should request from FxA
   *
   * @returns {object}: Returns an object representing the query parameters that should be
   *     added to the FxA authorization URL to initialize an oAuth flow.
   *     In practice, the query parameters are:
   *       - `client_id`: The OAuth client ID for Firefox Desktop
   *       - `scope`: The scopes given by the caller, space seperated
   *       - `action`: This will always be `email`
   *       - `response_type`: This will always be `code`
   *       - `access_type`: This will always be `offline`
   *       - `state`: A URL-safe base-64 string randomly generated
   *       - `code_challenge`: A URL-safe base-64 string representing the PKCE challenge
   *       - `code_challenge_method`: This will always be `S256`
   *          For more informatio about PKCE, read https://datatracker.ietf.org/doc/html/rfc7636
   *       - `keys_jwk`: A URL-safe base-64 representing a JWK to be used as a public key by the server
   *          to generate a JWE
   */
  async beginOAuthFlow(scopes) {
    const queryParams = {
      client_id: OAUTH_CLIENT_ID,
      action: "email",
      response_type: "code",
      access_type: "offline",
      scope: scopes.join(" "),
    };

    // Generate a random, 16 byte value to represent a state that we verify
    // once we complete the oauth flow, to ensure that we only conclude
    // an oauth flow that we started
    const state = new Uint8Array(16);
    crypto.getRandomValues(state);
    const stateB64 = ChromeUtils.base64URLEncode(state, { pad: false });
    queryParams.state = stateB64;

    // Generate a 43 byte code verifier for PKCE, in accordance with
    // https://datatracker.ietf.org/doc/html/rfc7636#section-7.1 which recommends a
    // 43-octet URL safe string
    // The byte array is 32 bytes
    const codeVerifier = new Uint8Array(32);
    crypto.getRandomValues(codeVerifier);
    // When base64 encoded, it is 43 bytes
    const codeVerifierB64 = ChromeUtils.base64URLEncode(codeVerifier, {
      pad: false,
    });
    const challenge = await crypto.subtle.digest(
      "SHA-256",
      new TextEncoder().encode(codeVerifierB64)
    );
    const challengeB64 = ChromeUtils.base64URLEncode(challenge, { pad: false });
    queryParams.code_challenge = challengeB64;
    queryParams.code_challenge_method = "S256";

    // Generate a public, private key pair to be used during the oauth flow
    // to encrypt scoped-keys as they roundtrip through the auth server
    const ECDH_KEY = { name: "ECDH", namedCurve: "P-256" };
    const key = await crypto.subtle.generateKey(ECDH_KEY, false, ["deriveKey"]);
    const publicKey = await crypto.subtle.exportKey("jwk", key.publicKey);
    const privateKey = key.privateKey;

    // We encode the public key as URL-safe base64 to be included in the query parameters
    const encodedPublicKey = ChromeUtils.base64URLEncode(
      new TextEncoder().encode(JSON.stringify(publicKey)),
      { pad: false }
    );
    queryParams.keys_jwk = encodedPublicKey;

    // We store the state in-memory, to verify once the oauth flow is completed
    this.addFlow(stateB64, {
      key: privateKey,
      verifier: codeVerifierB64,
      requestedScopes: scopes.join(" "),
    });
    return queryParams;
  }

  /**
   * Completes an OAuth flow and invalidates any other ongoing flows
   *
   * @param { string } sessionTokenHex: The session token encoded in hexadecimal
   * @param { string } code: OAuth authorization code provided by running an OAuth flow
   * @param { string } state: The state first provided by `beginOAuthFlow`, then roundtripped through the server
   *
   * @returns {object}: Returns an object representing the result of completing the oauth flow.
   *   The object includes the following:
   *     - 'scopedKeys': The encryption keys provided by the server, already decrypted
   *     - 'refreshToken': The refresh token provided by the server
   *     - 'accessToken': The access token provided by the server
   */
  async completeOAuthFlow(sessionTokenHex, code, state) {
    const flow = this.getFlow(state);
    if (!flow) {
      throw new Error(ERROR_INVALID_STATE);
    }
    const { key, verifier, requestedScopes } = flow;
    const { keys_jwe, refresh_token, access_token, scope } =
      await this.#fxaClient.oauthToken(
        sessionTokenHex,
        code,
        verifier,
        OAUTH_CLIENT_ID
      );
    const requestedSync = requestedScopes.includes(SCOPE_APP_SYNC);
    const grantedSync = scope.includes(SCOPE_APP_SYNC);
    // This is not necessarily unexpected as the user could be using
    // third-party auth but sent the sync scope, we shouldn't error here
    if (requestedSync && !grantedSync) {
      log.info("Requested Sync scope but was not granted sync!");
    }
    let scopedKeys;
    if (keys_jwe) {
      scopedKeys = JSON.parse(
        new TextDecoder().decode(await lazy.jwcrypto.decryptJWE(keys_jwe, key))
      );
      if (!this.#fxaKeys.validScopedKeys(scopedKeys)) {
        throw new Error(ERROR_INVALID_SCOPED_KEYS);
      }
    }

    // We make sure no other flow snuck in, and completed before we did
    if (!this.getFlow(state)) {
      throw new Error(ERROR_OAUTH_FLOW_ABANDONED);
    }

    // Clear all flows, so any in-flight or future flows trigger an error as the browser
    // would have been signed in
    this.clearAllFlows();
    return {
      scopedKeys,
      refreshToken: refresh_token,
      accessToken: access_token,
    };
  }

  /**
   * Grants an OAuth authorization code for another client.
   *
   * This is the counterpart to `beginOAuthFlow`: where that starts a flow for
   * this browser to complete, this authorizes the parameters some *other*
   * client produced by running its own `beginOAuthFlow`. It is used by the
   * pairing authority, which is already signed in and so holds both the session
   * token and the scoped keys the connecting client is asking for.
   *
   * If the caller supplied a `keys_jwk`, the requested scoped keys are wrapped
   * into a `keys_jwe` so they can be delivered to that client through the auth
   * server without the server itself being able to read them.
   *
   * @param { string } sessionToken: The session token encoded in hexadecimal
   * @param {object} options: The OAuth parameters to authorize
   *   - `client_id`: The OAuth client ID of the client being granted the code
   *   - `state`: The state created by the other client's `beginOAuthFlow`
   *   - `scope`: Space separated scopes being requested
   *   - `access_type`: Typically `offline`
   *   - `code_challenge`: The PKCE challenge
   *   - `code_challenge_method`: The PKCE challenge method, ie `S256`
   *   - `keys_jwk`: Optional public JWK to encrypt scoped keys to
   *
   * @returns {Promise<object>}: Object containing "code" and "state" properties.
   */
  async authorizeOAuthCode(sessionToken, options) {
    const params = { ...options };
    if (params.keys_jwk) {
      const jwk = JSON.parse(
        new TextDecoder().decode(
          ChromeUtils.base64URLDecode(params.keys_jwk, { padding: "reject" })
        )
      );
      params.keys_jwe = await this.#createKeysJWE(
        sessionToken,
        params.client_id,
        params.scope,
        jwk
      );
      delete params.keys_jwk;
    }
    return this.#fxaClient.oauthAuthorize(sessionToken, params);
  }

  /**
   * Create a JWE to deliver keys to another client via the OAuth scoped-keys flow.
   *
   * This is used to transfer key material to another client, by providing an
   * appropriately-encrypted value for the `keys_jwe` OAuth response parameter.
   * Since we're transferring keys from one client to another, two things must be
   * true:
   *
   *   * This client must actually have the key.
   *   * The other client must be allowed to request that key.
   *
   * @param {string} sessionToken the sessionToken to use when fetching key metadata
   * @param {string} clientId the client requesting access to our keys
   * @param {string} scopes Space separated requested scopes being requested
   * @param {object} jwk Ephemeral JWK provided by the client for secure key transfer
   */
  async #createKeysJWE(sessionToken, clientId, scopes, jwk) {
    // This checks with the FxA server about what scopes the client is allowed.
    // Note that we pass the requesting client_id here, not our own client_id.
    const clientKeyData = await this.#fxaClient.getScopedKeyData(
      sessionToken,
      clientId,
      scopes
    );
    const scopedKeys = {};
    for (const scope of Object.keys(clientKeyData)) {
      const key = await this.#fxaKeys.getKeyForScope(scope);
      if (!key) {
        throw new Error(`Key not available for scope "${scope}"`);
      }
      scopedKeys[scope] = key;
    }
    return lazy.jwcrypto.generateJWE(
      jwk,
      new TextEncoder().encode(JSON.stringify(scopedKeys))
    );
  }

  /**
   * Obtain an OAuth access token for the given scopes, minted from the stored
   * session token.
   *
   * Note that access-token caching and in-flight de-duplication are handled by
   * the caller (`FxAccountsInternal.getOAuthToken`).
   *
   * @param {object} accountState: The current AccountState
   * @param {string[]|string} scopes: The requested scopes
   * @param {number} [ttl]: Optional token time-to-live
   * @returns {Promise<{token: string, expiresAt: number|null}>}
   */
  async getAccessToken(accountState, scopes, ttl) {
    return this.#getAccessTokenWithSessionToken(accountState, scopes, ttl);
  }

  /**
   * Obtain an OAuth access token minted directly from the session token.
   *
   * @param {object} accountState: The current AccountState
   * @param {string[]|string} scopes: The requested scopes
   * @param {number} [ttl]: Optional token time-to-live
   * @returns {Promise<{token: string, expiresAt: number|null}>}
   */
  async #getAccessTokenWithSessionToken(accountState, scopes, ttl) {
    const data = await accountState.getUserAccountData(["sessionToken"]);
    if (!data || !data.sessionToken) {
      throw new Error(ERROR_AUTH_ERROR);
    }
    const scopeString = this.#normalizeScopes(scopes).join(" ");
    const result = await this.#fxaClient.accessTokenWithSessionToken(
      data.sessionToken,
      OAUTH_CLIENT_ID,
      scopeString,
      ttl
    );
    return {
      token: result.access_token,
      expiresAt: result.expires_in
        ? Math.floor(Date.now() / 1000) + result.expires_in
        : null,
    };
  }

  // Normalizes scopes (accepting a space-separated string or an array) to a
  // sorted, de-duplicated, lower-cased array.
  #normalizeScopes(scopes) {
    const arr = typeof scopes === "string" ? scopes.split(/\s+/) : scopes;
    const set = new Set(arr.filter(Boolean).map(s => s.toLowerCase()));
    return [...set].sort();
  }
}
