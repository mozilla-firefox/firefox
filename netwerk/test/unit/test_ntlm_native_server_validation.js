/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// End-to-end native NTLM against a server that actually validates the
// response, rather than one that accepts any type 3 message.
//
// The server issues a real type 2 with a fresh random challenge, then
// recomputes the NTLMv2 proof from a password it knows and compares it against
// what the client sent:
//
//   NTHash      = MD4(UTF16LE(password))
//   NTLMv2Hash  = HMAC-MD5(NTHash, UTF16LE(uppercase(user) + domain))
//   NTProofStr  = HMAC-MD5(NTLMv2Hash, serverChallenge + blob)
//
// where blob is everything after the first 16 bytes of NtChallengeResponse.
// That makes the test sensitive to the bytes SSPI produces, not just to the
// message type, and it lets a wrong password be distinguished from a right one.
//
// The client is driven with explicit credentials: localhost is deliberately not
// in network.automatic-ntlm-auth.trusted-uris, so CanUseDefaultCredentials() is
// false and nsHttpNTLMAuth takes the Windows branch that uses the native module
// with identityInvalid = true. nsAuthSSPI::Init passes the prompted credentials
// to AcquireCredentialsHandle, so the response derives from a password the test
// chose. With default credentials SSPI has no usable secret on a machine that
// is not domain joined and fails with SEC_E_NO_CREDENTIALS.

"use strict";

/* globals require, global, Buffer */

const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const DOMAIN = "TESTDOMAIN";
const USER = "testuser";
const PASSWORD = "testpass";

// ---------------------------------------------------------------------------
// Code below runs inside the node child process. It is stringified and eval'd
// there, so it may only use require(), Buffer and global.
// ---------------------------------------------------------------------------

// Node's crypto still exposes MD5 but OpenSSL 3 moved MD4 to the legacy
// provider, and NTLM needs MD4 for the NT hash.
function nodeMd4(input) {
  const rotl = (v, n) => ((v << n) | (v >>> (32 - n))) >>> 0;
  const F = (x, y, z) => ((x & y) | (~x & z)) >>> 0;
  const G = (x, y, z) => ((x & y) | (x & z) | (y & z)) >>> 0;
  const H = (x, y, z) => (x ^ y ^ z) >>> 0;

  const total = Math.ceil((input.length + 9) / 64) * 64;
  const m = Buffer.alloc(total);
  input.copy(m);
  m[input.length] = 0x80;
  const bits = input.length * 8;
  m.writeUInt32LE(bits >>> 0, total - 8);
  m.writeUInt32LE(Math.floor(bits / 4294967296), total - 4);

  // Each step updates one of a,b,c,d in the rotating order a,d,c,b and reads
  // the other three in order starting after the target.
  const order = [0, 3, 2, 1];
  const state = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
  const words = new Array(16);

  const round2Index = [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15];
  const round3Index = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15];
  const round1Shift = [3, 7, 11, 19];
  const round2Shift = [3, 5, 9, 13];
  const round3Shift = [3, 9, 11, 15];

  for (let offset = 0; offset < total; offset += 64) {
    for (let i = 0; i < 16; i++) {
      words[i] = m.readUInt32LE(offset + i * 4);
    }
    const saved = state.slice();

    const step = (j, fn, word, shift, constant) => {
      const t = order[j % 4];
      const mixed = fn(
        state[(t + 1) % 4],
        state[(t + 2) % 4],
        state[(t + 3) % 4]
      );
      state[t] = rotl((state[t] + mixed + word + constant) >>> 0, shift);
    };

    for (let j = 0; j < 16; j++) {
      step(j, F, words[j], round1Shift[j % 4], 0);
    }
    for (let j = 0; j < 16; j++) {
      step(j, G, words[round2Index[j]], round2Shift[j % 4], 0x5a827999);
    }
    for (let j = 0; j < 16; j++) {
      step(j, H, words[round3Index[j]], round3Shift[j % 4], 0x6ed9eba1);
    }

    for (let i = 0; i < 4; i++) {
      state[i] = (state[i] + saved[i]) >>> 0;
    }
  }

  const out = Buffer.alloc(16);
  for (let i = 0; i < 4; i++) {
    out.writeUInt32LE(state[i], i * 4);
  }
  return out;
}

// Known answer tests, so a broken MD4 shows up as a bad digest rather than as a
// mysteriously rejected password. The third is the documented NT hash of
// "password" from MS-NLMP.
function nodeMd4SelfTest() {
  return [
    global.md4(Buffer.alloc(0)).toString("hex"),
    global.md4(Buffer.from("abc", "latin1")).toString("hex"),
    global.md4(Buffer.from("password", "utf16le")).toString("hex"),
  ].join(",");
}

// A type 2 message with a fresh challenge. TargetInfo has to be present and
// non-empty or the client answers with NTLMv1 instead of NTLMv2.
function nodeMakeChallenge() {
  const crypto = require("crypto");
  const serverChallenge = crypto.randomBytes(8);

  const avPair = (id, value) => {
    const encoded = Buffer.from(value, "utf16le");
    const header = Buffer.alloc(4);
    header.writeUInt16LE(id, 0);
    header.writeUInt16LE(encoded.length, 2);
    return Buffer.concat([header, encoded]);
  };

  const targetName = Buffer.from("DOMAIN", "utf16le");
  const targetInfo = Buffer.concat([
    avPair(2, "DOMAIN"), // MsvAvNbDomainName
    avPair(1, "SERVER"), // MsvAvNbComputerName
    avPair(4, "domain.com"), // MsvAvDnsDomainName
    avPair(3, "server.domain.com"), // MsvAvDnsComputerName
    Buffer.alloc(4), // MsvAvEOL
  ]);

  // UNICODE | REQUEST_TARGET | SIGN | NTLM | ALWAYS_SIGN | TARGET_TYPE_DOMAIN |
  // EXTENDED_SESSIONSECURITY | TARGET_INFO | 128 | KEY_EXCH | 56
  // Since KB5135557 SSPI offers SIGN and fails the type 2 with
  // SEC_E_UNSUPPORTED_FUNCTION unless the server echoes the signing flags.
  const flags = 0xe0898215;
  const header = Buffer.alloc(48);
  header.write("NTLMSSP\0", 0, "latin1");
  header.writeUInt32LE(2, 8);
  header.writeUInt16LE(targetName.length, 12);
  header.writeUInt16LE(targetName.length, 14);
  header.writeUInt32LE(48, 16);
  header.writeUInt32LE(flags, 20);
  serverChallenge.copy(header, 24);
  header.writeUInt16LE(targetInfo.length, 40);
  header.writeUInt16LE(targetInfo.length, 42);
  header.writeUInt32LE(48 + targetName.length, 44);

  return {
    message: Buffer.concat([header, targetName, targetInfo]),
    serverChallenge,
  };
}

function nodeValidateType3(message, serverChallenge, password) {
  const crypto = require("crypto");
  const field = (lengthAt, offsetAt) => {
    const length = message.readUInt16LE(lengthAt);
    const offset = message.readUInt32LE(offsetAt);
    return message.subarray(offset, offset + length);
  };

  const ntResponse = field(20, 24);
  const domain = field(28, 32).toString("utf16le");
  const user = field(36, 40).toString("utf16le");
  const workstation = field(44, 48).toString("utf16le");
  const result = {
    user,
    domain,
    workstation,
    ntResponseLength: ntResponse.length,
    valid: false,
    reason: "",
  };

  if (ntResponse.length <= 24) {
    result.reason = "expected an NTLMv2 response";
    return result;
  }

  const proof = ntResponse.subarray(0, 16);
  const blob = ntResponse.subarray(16);
  const ntHash = global.md4(Buffer.from(password, "utf16le"));
  const ntlmv2Hash = crypto
    .createHmac("md5", ntHash)
    .update(Buffer.from(user.toUpperCase() + domain, "utf16le"))
    .digest();
  const expected = crypto
    .createHmac("md5", ntlmv2Hash)
    .update(Buffer.concat([serverChallenge, blob]))
    .digest();

  result.valid = expected.equals(proof);
  if (!result.valid) {
    result.reason = `proof ${proof.toString("hex")} != ${expected.toString(
      "hex"
    )}`;
  }
  return result;
}

// NTLM is connection based, so the challenge is remembered on the socket that
// issued it and the type 3 has to arrive on that same connection.
function ntlmHandler(req, resp) {
  const finish = (status, headers, body) => {
    const payload = Buffer.from(body || "", "utf8");
    resp.writeHead(
      status,
      Object.assign({ "Content-Length": String(payload.length) }, headers)
    );
    resp.end(payload);
  };

  const header = req.headers.authorization;
  if (!header || !header.startsWith("NTLM ")) {
    global.ntlmRequests.push("no-credentials");
    finish(401, { "WWW-Authenticate": "NTLM" }, "");
    return;
  }

  const message = Buffer.from(header.substring("NTLM ".length), "base64");
  if (message.length < 12 || message.toString("latin1", 0, 7) != "NTLMSSP") {
    finish(400, {}, "not an NTLM message");
    return;
  }

  const type = message.readUInt32LE(8);
  if (type == 1) {
    global.ntlmRequests.push("type1");
    const challenge = global.ntlmMakeChallenge();
    req.socket.ntlmServerChallenge = challenge.serverChallenge;
    finish(
      401,
      { "WWW-Authenticate": "NTLM " + challenge.message.toString("base64") },
      ""
    );
    return;
  }

  if (type == 3) {
    global.ntlmRequests.push("type3");
    const serverChallenge = req.socket.ntlmServerChallenge;
    if (!serverChallenge) {
      global.ntlmResult = {
        valid: false,
        reason: "type 3 on a new connection",
      };
      finish(403, {}, "no challenge outstanding on this connection");
      return;
    }
    const result = global.ntlmValidateType3(
      message,
      serverChallenge,
      global.ntlmPassword
    );
    global.ntlmResult = result;
    if (!result.valid) {
      finish(403, {}, "invalid: " + result.reason);
      return;
    }
    finish(200, { "Content-Type": "text/plain" }, "welcome");
    return;
  }

  finish(400, {}, "unexpected NTLM message type " + type);
}

// ---------------------------------------------------------------------------
// Test side
// ---------------------------------------------------------------------------

function AuthPrompt(password) {
  this.password = password;
}
AuthPrompt.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIAuthPrompt2"]),
  promptAuth(channel, level, authInfo) {
    authInfo.domain = DOMAIN;
    authInfo.username = USER;
    authInfo.password = this.password;
    return true;
  },
  asyncPromptAuth() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
};

function Requestor(password) {
  this.password = password;
}
Requestor.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIInterfaceRequestor"]),
  getInterface(iid) {
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      if (!this.prompt) {
        this.prompt = new AuthPrompt(this.password);
      }
      return this.prompt;
    }
    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  },
  prompt: null,
};

function makeChan(url) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(url),
    {}
  );
  return NetUtil.newChannel({
    uri: url,
    loadingPrincipal: principal,
    securityFlags: Ci.nsILoadInfo.SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
  }).QueryInterface(Ci.nsIHttpChannel);
}

let server;

async function serverState() {
  return JSON.parse(
    await server.execute(
      "JSON.stringify({result: global.ntlmResult, requests: global.ntlmRequests})"
    )
  );
}

async function resetServer(password) {
  await server.execute(
    `global.ntlmPassword = ${JSON.stringify(password)};
     global.ntlmResult = null;
     global.ntlmRequests = [];`
  );
  Cc["@mozilla.org/network/http-auth-manager;1"]
    .getService(Ci.nsIHttpAuthManager)
    .clearAll();
}

async function openChannel(password) {
  let url = `http://localhost:${server.port()}/ntlm`;
  let chan = makeChan(url);
  chan.notificationCallbacks = new Requestor(password);
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (req, buffer) => resolve([req, buffer]),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

add_setup(async function setup() {
  Services.prefs.setIntPref("network.auth.subresource-http-auth-allow", 2);

  server = new NodeHTTPServer();
  await server.start();

  await server.execute(`global.md4 = ${nodeMd4.toString()}`);
  await server.execute(`global.md4SelfTest = ${nodeMd4SelfTest.toString()}`);
  await server.execute(
    `global.ntlmMakeChallenge = ${nodeMakeChallenge.toString()}`
  );
  await server.execute(
    `global.ntlmValidateType3 = ${nodeValidateType3.toString()}`
  );
  await server.execute("global.ntlmRequests = []; global.ntlmResult = null;");
  await server.registerPathHandler("/ntlm", ntlmHandler);

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.auth.subresource-http-auth-allow");
    await server.stop();
  });

  // Fail here rather than as an unexplained authentication rejection later.
  let vectors = await server.execute("global.md4SelfTest()");
  Assert.equal(
    vectors,
    "31d6cfe0d16ae931b73c59d7e0c089c0," +
      "a448017aaf21d8525fc10ae87aa6729d," +
      "8846f7eaee8fb117ad06bdd830b7586c",
    "MD4 known answer tests"
  );
});

add_task(async function test_server_validates_native_response() {
  await resetServer(PASSWORD);

  let [request, buffer] = await openChannel(PASSWORD);

  Assert.equal(request.status, Cr.NS_OK, "channel succeeded");
  Assert.equal(
    request.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    200,
    "server accepted the response it recomputed"
  );
  Assert.equal(buffer, "welcome", "got the protected body");

  let { result, requests } = await serverState();
  Assert.deepEqual(
    requests,
    ["no-credentials", "type1", "type3"],
    "full handshake"
  );
  Assert.ok(result.valid, `proof verified: ${result.reason}`);
  Assert.equal(result.user, USER, "username the client authenticated as");
  Assert.equal(result.domain, DOMAIN, "domain the client authenticated as");
  Assert.greater(result.ntResponseLength, 24, "NTLMv2 response");
});

// The same handshake with a password the server does not share must be
// rejected. Without this, a validator that accepted everything would look
// identical to one that works.
add_task(async function test_server_rejects_wrong_password() {
  await resetServer("a-different-password");

  let [request] = await openChannel(PASSWORD);

  Assert.equal(
    request.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    403,
    "server rejected a proof computed from the wrong password"
  );

  let { result } = await serverState();
  Assert.ok(!result.valid, "validation failed");
  Assert.ok(
    result.reason.startsWith("proof "),
    `failed on the proof comparison, not earlier: ${result.reason}`
  );
});
