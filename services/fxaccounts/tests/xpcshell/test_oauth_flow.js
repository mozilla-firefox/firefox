/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* global crypto */

"use strict";

const {
  FxAccountsOAuth,
  ERROR_INVALID_SCOPED_KEYS,
  ERROR_INVALID_STATE,
  ERROR_OAUTH_FLOW_ABANDONED,
} = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsOAuth.sys.mjs"
);

const { SCOPE_PROFILE, OAUTH_CLIENT_ID } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  jwcrypto: "moz-src:///services/crypto/modules/jwcrypto.sys.mjs",
  FxAccountsKeys: "resource://gre/modules/FxAccountsKeys.sys.mjs",
});

initTestLogging("Trace");

add_task(function test_begin_oauth_flow() {
  const oauth = new FxAccountsOAuth();
  add_task(async function test_begin_oauth_flow_ok() {
    const scopes = [SCOPE_PROFILE, SCOPE_APP_SYNC];
    const queryParams = await oauth.beginOAuthFlow(scopes);

    // First verify default query parameters
    Assert.equal(queryParams.client_id, OAUTH_CLIENT_ID);
    Assert.equal(queryParams.action, "email");
    Assert.equal(queryParams.response_type, "code");
    Assert.equal(queryParams.access_type, "offline");
    Assert.equal(queryParams.scope, [SCOPE_PROFILE, SCOPE_APP_SYNC].join(" "));

    // Then, we verify that the state is a valid Base64 value
    const state = queryParams.state;
    ChromeUtils.base64URLDecode(state, { padding: "reject" });

    // Then, we verify that the codeVerifier, can be used to verify the code_challenge
    const code_challenge = queryParams.code_challenge;
    Assert.equal(queryParams.code_challenge_method, "S256");
    const oauthFlow = oauth.getFlow(state);
    const codeVerifierB64 = oauthFlow.verifier;
    const expectedChallenge = await crypto.subtle.digest(
      "SHA-256",
      new TextEncoder().encode(codeVerifierB64)
    );
    const expectedChallengeB64 = ChromeUtils.base64URLEncode(
      expectedChallenge,
      { pad: false }
    );
    Assert.equal(expectedChallengeB64, code_challenge);

    // Then, we verify that something encrypted with the `keys_jwk`, can be decrypted using the private key
    const keysJwk = queryParams.keys_jwk;
    const decodedKeysJwk = JSON.parse(
      new TextDecoder().decode(
        ChromeUtils.base64URLDecode(keysJwk, { padding: "reject" })
      )
    );
    const plaintext = "text to be encrypted and decrypted!";
    delete decodedKeysJwk.key_ops;
    const jwe = await jwcrypto.generateJWE(
      decodedKeysJwk,
      new TextEncoder().encode(plaintext)
    );
    const privateKey = oauthFlow.key;
    const decrypted = await jwcrypto.decryptJWE(jwe, privateKey);
    Assert.equal(new TextDecoder().decode(decrypted), plaintext);

    // Finally, we verify that we stored the requested scopes
    Assert.deepEqual(oauthFlow.requestedScopes, scopes.join(" "));
  });
});

add_task(function test_complete_oauth_flow() {
  add_task(async function test_invalid_state() {
    const oauth = new FxAccountsOAuth();
    const code = "foo";
    const state = "bar";
    const sessionToken = "01abcef12";
    try {
      await oauth.completeOAuthFlow(sessionToken, code, state);
      Assert.fail("Should have thrown an error");
    } catch (err) {
      Assert.equal(err.message, ERROR_INVALID_STATE);
    }
  });
  add_task(async function test_sync_scope_not_authorized() {
    const fxaClient = {
      oauthToken: () =>
        Promise.resolve({
          access_token: "access_token",
          refresh_token: "refresh_token",
          // Note that the scope does not include the sync scope
          scope: SCOPE_PROFILE,
        }),
    };
    const oauth = new FxAccountsOAuth(fxaClient);
    const scopes = [SCOPE_PROFILE, SCOPE_APP_SYNC];
    const sessionToken = "01abcef12";
    const queryParams = await oauth.beginOAuthFlow(scopes);
    try {
      await oauth.completeOAuthFlow(sessionToken, "foo", queryParams.state);
    } catch (err) {
      Assert.fail(
        "We should throw if we don't receive requested scope, we skip sync setup"
      );
    }
  });
  add_task(async function test_jwe_not_returned() {
    const scopes = [SCOPE_PROFILE, SCOPE_APP_SYNC];
    const fxaClient = {
      oauthToken: () =>
        Promise.resolve({
          access_token: "access_token",
          refresh_token: "refresh_token",
          scope: scopes.join(" "),
        }),
    };
    const oauth = new FxAccountsOAuth(fxaClient);
    const queryParams = await oauth.beginOAuthFlow(scopes);
    const sessionToken = "01abcef12";
    try {
      await oauth.completeOAuthFlow(sessionToken, "foo", queryParams.state);
    } catch (err) {
      Assert.fail(
        "Should not have thrown an error because we didn't get back a keys_jwe,we instead skip sync setup"
      );
    }
  });
  add_task(async function test_complete_oauth_ok() {
    // First, we initialize some fake values we would typically get
    // from outside our system
    const scopes = [SCOPE_PROFILE, SCOPE_APP_SYNC];
    const oauthCode = "fake oauth code";
    const sessionToken = "01abcef12";
    const plainTextScopedKeys = {
      [SCOPE_APP_SYNC]: {
        kty: "oct",
        kid: "1510726318123-IqQv4onc7VcVE1kTQkyyOw",
        k: "DW_ll5GwX6SJ5GPqJVAuMUP2t6kDqhUulc2cbt26xbTcaKGQl-9l29FHAQ7kUiJETma4s9fIpEHrt909zgFang",
        scope: SCOPE_APP_SYNC,
      },
    };
    const fakeAccessToken = "fake access token";
    const fakeRefreshToken = "fake refresh token";
    // Then, we initialize a fake http client, we'll add our fake oauthToken call
    // once we have started the oauth flow (so we have the public keys!)
    const fxaClient = {};
    const fxaKeys = new FxAccountsKeys(null);
    // Then, we initialize our oauth object with the given client and begin a new flow
    const oauth = new FxAccountsOAuth(fxaClient, fxaKeys);
    const queryParams = await oauth.beginOAuthFlow(scopes);
    // Now that we have the public keys in `keys_jwk`, we use it to generate a JWE
    // representing our scoped keys
    const keysJwk = queryParams.keys_jwk;
    const decodedKeysJwk = JSON.parse(
      new TextDecoder().decode(
        ChromeUtils.base64URLDecode(keysJwk, { padding: "reject" })
      )
    );
    delete decodedKeysJwk.key_ops;
    const jwe = await jwcrypto.generateJWE(
      decodedKeysJwk,
      new TextEncoder().encode(JSON.stringify(plainTextScopedKeys))
    );
    // We also grab the stored PKCE verifier that the oauth object stored internally
    // to verify that we correctly send it as a part of our HTTP request
    const storedVerifier = oauth.getFlow(queryParams.state).verifier;

    // To test what happens when more than one flow is completed simulatniously
    // We mimic a slow network call on the first oauthToken call and let the second
    // one win
    let callCount = 0;
    let slowResolve;
    const resolveFn = (payload, resolve) => {
      if (callCount === 1) {
        // This is the second call
        // lets resolve it so the second call wins
        resolve(payload);
      } else {
        callCount += 1;
        // This is the first call, let store our resolve function for later
        // it will be resolved once the fast flow is fully completed
        slowResolve = () => resolve(payload);
      }
    };

    // Now we initialize our mock of the HTTP request, it verifies we passed in all the correct
    // parameters and returns what we'd expect a healthy HTTP Response would look like
    fxaClient.oauthToken = (sessionTokenHex, code, verifier, clientId) => {
      Assert.equal(sessionTokenHex, sessionToken);
      Assert.equal(code, oauthCode);
      Assert.equal(verifier, storedVerifier);
      Assert.equal(clientId, queryParams.client_id);
      const response = {
        access_token: fakeAccessToken,
        refresh_token: fakeRefreshToken,
        scope: scopes.join(" "),
        keys_jwe: jwe,
      };
      return new Promise(resolve => {
        resolveFn(response, resolve);
      });
    };

    // Then, we call the completeOAuthFlow function, and get back our access token,
    // refresh token and scopedKeys

    // To test what happens when multiple flows race, we create two flows,
    // A slow one that will start first, but finish last
    // And a fast one that will beat the slow one
    const firstCompleteOAuthFlow = oauth
      .completeOAuthFlow(sessionToken, oauthCode, queryParams.state)
      .then(res => {
        // To mimic the slow network connection on the slowCompleteOAuthFlow
        // We resume the slow completeOAuthFlow once this one is complete
        slowResolve();
        return res;
      });
    const secondCompleteOAuthFlow = oauth
      .completeOAuthFlow(sessionToken, oauthCode, queryParams.state)
      .then(res => {
        // since we can't fully gaurentee which oauth flow finishes first, we also resolve here
        slowResolve();
        return res;
      });

    const { accessToken, refreshToken, scopedKeys } = await Promise.allSettled([
      firstCompleteOAuthFlow,
      secondCompleteOAuthFlow,
    ]).then(results => {
      let fast;
      let slow;
      for (const result of results) {
        if (result.status === "fulfilled") {
          fast = result.value;
        } else {
          slow = result.reason;
        }
      }
      // We make sure that we indeed have one slow flow that lost
      Assert.equal(slow.message, ERROR_OAUTH_FLOW_ABANDONED);
      return fast;
    });

    Assert.equal(accessToken, fakeAccessToken);
    Assert.equal(refreshToken, fakeRefreshToken);
    Assert.deepEqual(scopedKeys, plainTextScopedKeys);

    // Finally, we verify that all stored flows were cleared
    Assert.equal(oauth.numOfFlows(), 0);
  });
  add_task(async function test_complete_oauth_invalid_scoped_keys() {
    // First, we initialize some fake values we would typically get
    // from outside our system
    const scopes = [SCOPE_PROFILE, SCOPE_APP_SYNC];
    const oauthCode = "fake oauth code";
    const sessionToken = "01abcef12";
    const invalidScopedKeys = {
      [SCOPE_APP_SYNC]: {
        // ====== This is an invalid key type! Should be "oct", so we will raise an error once we realize
        kty: "EC",
        kid: "1510726318123-IqQv4onc7VcVE1kTQkyyOw",
        k: "DW_ll5GwX6SJ5GPqJVAuMUP2t6kDqhUulc2cbt26xbTcaKGQl-9l29FHAQ7kUiJETma4s9fIpEHrt909zgFang",
        scope: SCOPE_APP_SYNC,
      },
    };
    const fakeAccessToken = "fake access token";
    const fakeRefreshToken = "fake refresh token";
    // Then, we initialize a fake http client, we'll add our fake oauthToken call
    // once we have started the oauth flow (so we have the public keys!)
    const fxaClient = {};
    const fxaKeys = new FxAccountsKeys(null);
    // Then, we initialize our oauth object with the given client and begin a new flow
    const oauth = new FxAccountsOAuth(fxaClient, fxaKeys);
    const queryParams = await oauth.beginOAuthFlow(scopes);
    // Now that we have the public keys in `keys_jwk`, we use it to generate a JWE
    // representing our scoped keys
    const keysJwk = queryParams.keys_jwk;
    const decodedKeysJwk = JSON.parse(
      new TextDecoder().decode(
        ChromeUtils.base64URLDecode(keysJwk, { padding: "reject" })
      )
    );
    delete decodedKeysJwk.key_ops;
    const jwe = await jwcrypto.generateJWE(
      decodedKeysJwk,
      new TextEncoder().encode(JSON.stringify(invalidScopedKeys))
    );
    // We also grab the stored PKCE verifier that the oauth object stored internally
    // to verify that we correctly send it as a part of our HTTP request
    const storedVerifier = oauth.getFlow(queryParams.state).verifier;

    // Now we initialize our mock of the HTTP request, it verifies we passed in all the correct
    // parameters and returns what we'd expect a healthy HTTP Response would look like
    fxaClient.oauthToken = (sessionTokenHex, code, verifier, clientId) => {
      Assert.equal(sessionTokenHex, sessionToken);
      Assert.equal(code, oauthCode);
      Assert.equal(verifier, storedVerifier);
      Assert.equal(clientId, queryParams.client_id);
      const response = {
        access_token: fakeAccessToken,
        refresh_token: fakeRefreshToken,
        scope: scopes.join(" "),
        keys_jwe: jwe,
      };
      return Promise.resolve(response);
    };

    // Then, we call the completeOAuthFlow function, and get back our access token,
    // refresh token and scopedKeys
    try {
      await oauth.completeOAuthFlow(sessionToken, oauthCode, queryParams.state);
      Assert.fail(
        "Should have thrown an error because the scoped keys are not valid"
      );
    } catch (err) {
      Assert.equal(err.message, ERROR_INVALID_SCOPED_KEYS);
    }
  });
});

// `authorizeOAuthCode` is the authority half of pairing: it grants a code for
// parameters that some *other* client produced with `beginOAuthFlow`.
add_task(function test_authorize_oauth_code() {
  const SESSION_TOKEN = "01abcef12";
  const SYNC_KEY = {
    kty: "oct",
    kid: "1510726318123-IqQv4onc7VcVE1kTQkyyOw",
    k: "DW_ll5GwX6SJ5GPqJVAuMUP2t6kDqhUulc2cbt26xbTcaKGQl-9l29FHAQ7kUiJETma4s9fIpEHrt909zgFang",
    scope: SCOPE_APP_SYNC,
  };

  function mockClient(overrides = {}) {
    return {
      async getScopedKeyData() {
        return {
          [SCOPE_APP_SYNC]: {
            identifier: SCOPE_APP_SYNC,
            keyRotationTimestamp: 12345678,
          },
        };
      },
      async oauthAuthorize(sessionToken, params) {
        return { code: "fake code", state: params.state, params };
      },
      ...overrides,
    };
  }

  async function generateEphemeralKeypair() {
    const keypair = await crypto.subtle.generateKey(
      { name: "ECDH", namedCurve: "P-256" },
      true,
      ["deriveKey"]
    );
    const publicJWK = await crypto.subtle.exportKey("jwk", keypair.publicKey);
    delete publicJWK.key_ops;
    return { publicJWK, privateKey: keypair.privateKey };
  }

  add_task(async function test_authorize_wraps_scoped_keys_into_jwe() {
    const fxaClient = mockClient();
    const oauth = new FxAccountsOAuth(fxaClient, {
      getKeyForScope: () => SYNC_KEY,
    });

    // Stand in for the supplicant, which generated this keypair when it ran its
    // own `beginOAuthFlow` and sent us only the public half.
    const { publicJWK, privateKey } = await generateEphemeralKeypair();
    const result = await oauth.authorizeOAuthCode(SESSION_TOKEN, {
      client_id: "supplicant_client_id",
      access_type: "offline",
      state: "the-state",
      scope: SCOPE_APP_SYNC,
      code_challenge: "the-challenge",
      code_challenge_method: "S256",
      keys_jwk: ChromeUtils.base64URLEncode(
        new TextEncoder().encode(JSON.stringify(publicJWK)),
        { pad: false }
      ),
    });

    Assert.equal(result.code, "fake code");
    Assert.equal(result.state, "the-state");

    // The public JWK must be swapped for an encrypted JWE before the params
    // reach the server - the server must never see the raw key material.
    const params = result.params;
    Assert.ok(!params.keys_jwk, "keys_jwk should not be sent to the server");
    Assert.ok(params.keys_jwe, "keys_jwe should be sent to the server");

    // Only the holder of the supplicant's private key can read the keys back.
    const decrypted = JSON.parse(
      new TextDecoder().decode(
        await jwcrypto.decryptJWE(params.keys_jwe, privateKey)
      )
    );
    Assert.deepEqual(decrypted, { [SCOPE_APP_SYNC]: SYNC_KEY });

    // The remaining OAuth params should be forwarded untouched.
    Assert.equal(params.client_id, "supplicant_client_id");
    Assert.equal(params.access_type, "offline");
    Assert.equal(params.scope, SCOPE_APP_SYNC);
    Assert.equal(params.code_challenge, "the-challenge");
    Assert.equal(params.code_challenge_method, "S256");
  });

  add_task(async function test_authorize_without_keys_jwk() {
    let getScopedKeyDataCalled = false;
    const fxaClient = mockClient({
      async getScopedKeyData() {
        getScopedKeyDataCalled = true;
        return {};
      },
    });
    const oauth = new FxAccountsOAuth(fxaClient, {
      getKeyForScope: () => SYNC_KEY,
    });

    const result = await oauth.authorizeOAuthCode(SESSION_TOKEN, {
      client_id: "supplicant_client_id",
      access_type: "offline",
      state: "the-state",
      scope: SCOPE_PROFILE,
      code_challenge: "the-challenge",
      code_challenge_method: "S256",
    });

    Assert.equal(result.code, "fake code");
    Assert.ok(!result.params.keys_jwe, "No keys wrapped without a keys_jwk");
    Assert.ok(
      !getScopedKeyDataCalled,
      "Should not ask the server for key data when no keys were requested"
    );
  });

  add_task(async function test_authorize_missing_key_for_scope() {
    const fxaClient = mockClient();
    // The server says the client may have this scope's key, but we don't hold it.
    const oauth = new FxAccountsOAuth(fxaClient, {
      getKeyForScope: () => null,
    });

    const { publicJWK } = await generateEphemeralKeypair();
    await Assert.rejects(
      oauth.authorizeOAuthCode(SESSION_TOKEN, {
        client_id: "supplicant_client_id",
        state: "the-state",
        scope: SCOPE_APP_SYNC,
        keys_jwk: ChromeUtils.base64URLEncode(
          new TextEncoder().encode(JSON.stringify(publicJWK)),
          { pad: false }
        ),
      }),
      /Key not available for scope/
    );
  });
});
