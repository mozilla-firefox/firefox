/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { FxAccountsPairingFlow } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsPairing.sys.mjs"
);
const { EventEmitter } = ChromeUtils.importESModule(
  "resource://gre/modules/EventEmitter.sys.mjs"
);
const { FxAccountsOAuth } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsOAuth.sys.mjs"
);
ChromeUtils.defineESModuleGetters(this, {
  jwcrypto: "moz-src:///services/crypto/modules/jwcrypto.sys.mjs",
});

const CHANNEL_ID = "sW-UA97Q6Dljqen7XRlYPw";
const CHANNEL_KEY = crypto.getRandomValues(new Uint8Array(32));

const SENDER_SUPP = {
  ua: "Firefox Supp",
  city: "Nice",
  region: "PACA",
  country: "France",
  remote: "127.0.0.1",
};
const UID = "abcd";
const EMAIL = "foo@bar.com";
const AVATAR = "https://foo.bar/avatar";
const DISPLAY_NAME = "Foo bar";
const DEVICE_NAME = "Foo's computer";

const PAIR_URI = "https://foo.bar/pair";
const OAUTH_URI = "https://foo.bar/oauth";
const KSYNC = "myksync";
const SESSION = "mysession";
const fxaConfig = {
  promisePairingURI() {
    return PAIR_URI;
  },
  promiseOAuthURI() {
    return OAUTH_URI;
  },
};
const fxaClient = {
  async getScopedKeyData() {
    return {
      [SCOPE_APP_SYNC]: {
        identifier: SCOPE_APP_SYNC,
        keyRotationTimestamp: 12345678,
      },
    };
  },
  async oauthAuthorize() {
    return { code: "mycode", state: "mystate" };
  },
};
const fxaKeys = {
  getKeyForScope() {
    return {
      kid: "123456",
      k: KSYNC,
      kty: "oct",
    };
  },
};
// The flow reaches authorization through FxAccountsInternal, which delegates to
// FxAccountsOAuth. Use a real FxAccountsOAuth over the mock client and keys so
// the keys_jwe generation is genuinely exercised rather than stubbed out.
const fxaOAuth = new FxAccountsOAuth(fxaClient, fxaKeys);
const fxAccounts = {
  getSignedInUser() {
    return {
      uid: UID,
      email: EMAIL,
      avatar: AVATAR,
      displayName: DISPLAY_NAME,
    };
  },
  _internal: {
    keys: fxaKeys,
    fxAccountsClient: fxaClient,
    oauth: fxaOAuth,
    authorizeOAuthCode(options) {
      return fxaOAuth.authorizeOAuthCode(SESSION, options);
    },
  },
};
const weave = {
  Service: { clientsEngine: { localName: DEVICE_NAME } },
};

class MockPairingChannel extends EventTarget {
  get channelId() {
    return CHANNEL_ID;
  }

  get channelKey() {
    return CHANNEL_KEY;
  }

  send(data) {
    this.dispatchEvent(
      new CustomEvent("send", {
        detail: { data },
      })
    );
  }

  simulateIncoming(data) {
    this.dispatchEvent(
      new CustomEvent("message", {
        detail: { data, sender: SENDER_SUPP },
      })
    );
  }

  close() {
    this.closed = true;
  }
}

add_task(async function testFullFlow() {
  const emitter = new EventEmitter();
  const pairingChannel = new MockPairingChannel();
  const pairingUri = await FxAccountsPairingFlow.start({
    emitter,
    pairingChannel,
    fxAccounts,
    fxaConfig,
    weave,
  });
  Assert.equal(
    pairingUri,
    `${PAIR_URI}#channel_id=${CHANNEL_ID}&channel_key=${ChromeUtils.base64URLEncode(
      CHANNEL_KEY,
      { pad: false }
    )}`
  );

  const flow = FxAccountsPairingFlow.get(CHANNEL_ID);

  const promiseSwitchToWebContent = emitter.once("view:SwitchToWebContent");
  const promiseMetadataSent = promiseOutgoingMessage(pairingChannel);
  const epk = await generateEphemeralKeypair();

  pairingChannel.simulateIncoming({
    message: "pair:supp:request",
    data: {
      client_id: "client_id_1",
      state: "mystate",
      keys_jwk: ChromeUtils.base64URLEncode(
        new TextEncoder().encode(JSON.stringify(epk.publicJWK)),
        { pad: false }
      ),
      scope: `profile ${SCOPE_APP_SYNC}`,
      code_challenge: "chal",
      code_challenge_method: "S256",
    },
  });
  const sentAuthMetadata = await promiseMetadataSent;
  Assert.deepEqual(sentAuthMetadata, {
    message: "pair:auth:metadata",
    data: {
      email: EMAIL,
      avatar: AVATAR,
      displayName: DISPLAY_NAME,
      deviceName: DEVICE_NAME,
    },
  });
  const oauthUrl = await promiseSwitchToWebContent;
  Assert.equal(
    oauthUrl,
    `${OAUTH_URI}?client_id=client_id_1&scope=profile+${encodeURIComponent(
      SCOPE_APP_SYNC
    )}&email=foo%40bar.com&uid=abcd&channel_id=${CHANNEL_ID}&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob%3Apair-auth-webchannel`
  );

  let pairSuppMetadata = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_supplicant_metadata"
  );
  Assert.deepEqual(
    {
      ua: "Firefox Supp",
      city: "Nice",
      region: "PACA",
      country: "France",
      ipAddress: "127.0.0.1",
    },
    pairSuppMetadata
  );

  const generateJWE = sinon.spy(jwcrypto, "generateJWE");
  const oauthAuthorize = sinon.spy(
    fxAccounts._internal.fxAccountsClient,
    "oauthAuthorize"
  );
  const promiseOAuthParamsMsg = promiseOutgoingMessage(pairingChannel);
  await simulateIncomingWebChannel(flow, "fxaccounts:pair_authorize");
  // We should have generated the expected JWE.
  Assert.ok(generateJWE.calledOnce);
  const generateArgs = generateJWE.firstCall.args;
  Assert.deepEqual(generateArgs[0], epk.publicJWK);
  Assert.deepEqual(JSON.parse(new TextDecoder().decode(generateArgs[1])), {
    [SCOPE_APP_SYNC]: {
      kid: "123456",
      k: KSYNC,
      kty: "oct",
    },
  });
  // We should have authorized an oauth code with expected parameters.
  Assert.ok(oauthAuthorize.calledOnce);
  const oauthCodeArgs = oauthAuthorize.firstCall.args[1];
  console.log(oauthCodeArgs);
  Assert.ok(!oauthCodeArgs.keys_jwk);
  Assert.deepEqual(
    oauthCodeArgs.keys_jwe,
    await generateJWE.firstCall.returnValue
  );
  Assert.equal(oauthCodeArgs.client_id, "client_id_1");
  Assert.equal(oauthCodeArgs.access_type, "offline");
  Assert.equal(oauthCodeArgs.state, "mystate");
  Assert.equal(oauthCodeArgs.scope, `profile ${SCOPE_APP_SYNC}`);
  Assert.equal(oauthCodeArgs.code_challenge, "chal");
  Assert.equal(oauthCodeArgs.code_challenge_method, "S256");

  const oAuthParams = await promiseOAuthParamsMsg;
  Assert.deepEqual(oAuthParams, {
    message: "pair:auth:authorize",
    data: { code: "mycode", state: "mystate" },
  });

  let heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(!heartbeat.suppAuthorized);

  await pairingChannel.simulateIncoming({
    message: "pair:supp:authorize",
  });

  heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(heartbeat.suppAuthorized);

  await simulateIncomingWebChannel(flow, "fxaccounts:pair_complete");
  // The flow should have been destroyed!
  Assert.ok(!FxAccountsPairingFlow.get(CHANNEL_ID));
  Assert.ok(pairingChannel.closed);
  generateJWE.restore();
  oauthAuthorize.restore();
});

add_task(async function testUnknownPairingMessage() {
  const emitter = new EventEmitter();
  const pairingChannel = new MockPairingChannel();
  await FxAccountsPairingFlow.start({
    emitter,
    pairingChannel,
    fxAccounts,
    fxaConfig,
    weave,
  });
  const flow = FxAccountsPairingFlow.get(CHANNEL_ID);
  const viewErrorObserved = emitter.once("view:Error");
  pairingChannel.simulateIncoming({
    message: "pair:boom",
  });
  await viewErrorObserved;
  let heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(heartbeat.err);
});

add_task(async function testUnknownWebChannelCommand() {
  const emitter = new EventEmitter();
  const pairingChannel = new MockPairingChannel();
  await FxAccountsPairingFlow.start({
    emitter,
    pairingChannel,
    fxAccounts,
    fxaConfig,
    weave,
  });
  const flow = FxAccountsPairingFlow.get(CHANNEL_ID);
  const viewErrorObserved = emitter.once("view:Error");
  await simulateIncomingWebChannel(flow, "fxaccounts:boom");
  await viewErrorObserved;
  let heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(heartbeat.err);
});

add_task(async function testPairingChannelFailure() {
  const emitter = new EventEmitter();
  const pairingChannel = new MockPairingChannel();
  await FxAccountsPairingFlow.start({
    emitter,
    pairingChannel,
    fxAccounts,
    fxaConfig,
    weave,
  });
  const flow = FxAccountsPairingFlow.get(CHANNEL_ID);
  const viewErrorObserved = emitter.once("view:Error");
  sinon.stub(pairingChannel, "send").callsFake(() => {
    throw new Error("Boom!");
  });
  pairingChannel.simulateIncoming({
    message: "pair:supp:request",
    data: {
      client_id: "client_id_1",
      state: "mystate",
      scope: `profile ${SCOPE_APP_SYNC}`,
      code_challenge: "chal",
      code_challenge_method: "S256",
    },
  });
  await viewErrorObserved;

  let heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(heartbeat.err);
});

add_task(async function testFlowTimeout() {
  const emitter = new EventEmitter();
  const pairingChannel = new MockPairingChannel();
  const viewErrorObserved = emitter.once("view:Error");
  await FxAccountsPairingFlow.start({
    emitter,
    pairingChannel,
    fxAccounts,
    fxaConfig,
    weave,
    flowTimeout: 1,
  });
  const flow = FxAccountsPairingFlow.get(CHANNEL_ID);
  await viewErrorObserved;

  let heartbeat = await simulateIncomingWebChannel(
    flow,
    "fxaccounts:pair_heartbeat"
  );
  Assert.ok(heartbeat.err.match(/Timeout/));
});

async function simulateIncomingWebChannel(flow, command) {
  return flow.onWebChannelMessage(command);
}

async function promiseOutgoingMessage(pairingChannel) {
  return new Promise(res => {
    const onMessage = event => {
      pairingChannel.removeEventListener("send", onMessage);
      res(event.detail.data);
    };
    pairingChannel.addEventListener("send", onMessage);
  });
}

async function generateEphemeralKeypair() {
  const keypair = await crypto.subtle.generateKey(
    { name: "ECDH", namedCurve: "P-256" },
    true,
    ["deriveKey"]
  );
  const publicJWK = await crypto.subtle.exportKey("jwk", keypair.publicKey);
  const privateJWK = await crypto.subtle.exportKey("jwk", keypair.privateKey);
  delete publicJWK.key_ops;
  return {
    publicJWK,
    privateJWK,
  };
}
