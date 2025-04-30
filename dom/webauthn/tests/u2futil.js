// Used by local_addTest() / local_completeTest()
var _countCompletions = 0;
var _expectedCompletions = 0;

const flag_TUP = 0x01;
const flag_UV = 0x04;
const flag_AT = 0x40;

const cose_kty = 1;
const cose_kty_ec2 = 2;
const cose_alg = 3;
const cose_alg_ECDSA_w_SHA256 = -7;
const cose_alg_ECDSA_w_SHA512 = -36;
const cose_crv = -1;
const cose_crv_P256 = 1;
const cose_crv_x = -2;
const cose_crv_y = -3;

var { AppConstants } = SpecialPowers.ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

async function addVirtualAuthenticator(
  protocol = "ctap2_1",
  transport = "internal",
  hasResidentKey = true,
  hasUserVerification = true,
  isUserConsenting = true,
  isUserVerified = true
) {
  let id = await SpecialPowers.spawnChrome(
    [
      protocol,
      transport,
      hasResidentKey,
      hasUserVerification,
      isUserConsenting,
      isUserVerified,
    ],
    (
      protocol,
      transport,
      hasResidentKey,
      hasUserVerification,
      isUserConsenting,
      isUserVerified
    ) => {
      let webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
        Ci.nsIWebAuthnService
      );
      let id = webauthnService.addVirtualAuthenticator(
        protocol,
        transport,
        hasResidentKey,
        hasUserVerification,
        isUserConsenting,
        isUserVerified
      );
      return id;
    }
  );

  SimpleTest.registerCleanupFunction(async () => {
    await SpecialPowers.spawnChrome([id], id => {
      let webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
        Ci.nsIWebAuthnService
      );
      webauthnService.removeVirtualAuthenticator(id);
    });
  });

  return id;
}

async function setUserVerified(authenticatorId, isUserVerified) {
  await SpecialPowers.spawnChrome(
    [authenticatorId, isUserVerified],
    (authenticatorId, isUserVerified) => {
      let webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
        Ci.nsIWebAuthnService
      );
      webauthnService.setUserVerified(authenticatorId, isUserVerified);
    }
  );
}

function handleEventMessage(event) {
  if ("test" in event.data) {
    let summary = event.data.test + ": " + event.data.msg;
    log(event.data.status + ": " + summary);
    ok(event.data.status, summary);
  } else if ("done" in event.data) {
    SimpleTest.finish();
  } else {
    ok(false, "Unexpected message in the test harness: " + event.data);
  }
}

function log(msg) {
  console.log(msg);
  let logBox = document.getElementById("log");
  if (logBox) {
    logBox.textContent += "\n" + msg;
  }
}

function local_is(value, expected, message) {
  if (value === expected) {
    local_ok(true, message);
  } else {
    local_ok(false, message + " unexpectedly: " + value + " !== " + expected);
  }
}

function local_isnot(value, expected, message) {
  if (value !== expected) {
    local_ok(true, message);
  } else {
    local_ok(false, message + " unexpectedly: " + value + " === " + expected);
  }
}

function local_ok(expression, message) {
  let body = { test: this.location.pathname, status: expression, msg: message };
  parent.postMessage(body, "http://mochi.test:8888");
}

function local_doesThrow(fn, name) {
  let gotException = false;
  try {
    fn();
  } catch (ex) {
    gotException = true;
  }
  local_ok(gotException, name);
}

function local_expectThisManyTests(count) {
  if (_expectedCompletions > 0) {
    local_ok(
      false,
      "Error: local_expectThisManyTests should only be called once."
    );
  }
  _expectedCompletions = count;
}

function local_completeTest() {
  _countCompletions += 1;
  if (_countCompletions == _expectedCompletions) {
    log("All tests completed.");
    local_finished();
  }
  if (_countCompletions > _expectedCompletions) {
    local_ok(
      false,
      "Error: local_completeTest called more than local_addTest."
    );
  }
}

function local_finished() {
  parent.postMessage({ done: true }, "http://mochi.test:8888");
}

function string2buffer(str) {
  return new Uint8Array(str.length).map((x, i) => str.charCodeAt(i));
}

function buffer2string(buf) {
  let str = "";
  if (!(buf.constructor === Uint8Array)) {
    buf = new Uint8Array(buf);
  }
  buf.map(function (x) {
    return (str += String.fromCharCode(x));
  });
  return str;
}

function bytesToBase64(u8a) {
  let CHUNK_SZ = 0x8000;
  let c = [];
  let array = new Uint8Array(u8a);
  for (let i = 0; i < array.length; i += CHUNK_SZ) {
    c.push(String.fromCharCode.apply(null, array.subarray(i, i + CHUNK_SZ)));
  }
  return window.btoa(c.join(""));
}

function base64ToBytes(b64encoded) {
  return new Uint8Array(
    window
      .atob(b64encoded)
      .split("")
      .map(function (c) {
        return c.charCodeAt(0);
      })
  );
}

function bytesToBase64UrlSafe(buf) {
  return bytesToBase64(buf)
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=/g, "");
}

function base64ToBytesUrlSafe(str) {
  if (str.length % 4 == 1) {
    throw "Improper b64 string";
  }

  var b64 = str.replace(/\-/g, "+").replace(/\_/g, "/");
  while (b64.length % 4 != 0) {
    b64 += "=";
  }
  return base64ToBytes(b64);
}

function hexEncode(buf) {
  return Array.from(buf)
    .map(x => ("0" + x.toString(16)).substr(-2))
    .join("");
}

function hexDecode(str) {
  return new Uint8Array(str.match(/../g).map(x => parseInt(x, 16)));
}

function hasOnlyKeys(obj, ...keys) {
  let okeys = new Set(Object.keys(obj));
  return keys.length == okeys.size && keys.every(k => okeys.has(k));
}

function webAuthnDecodeCBORAttestation(aCborAttBuf) {
  let attObj = CBOR.decode(aCborAttBuf);
  console.log(":: Attestation CBOR Object ::");
  if (!hasOnlyKeys(attObj, "authData", "fmt", "attStmt")) {
    return Promise.reject("Invalid CBOR Attestation Object");
  }
  if (attObj.fmt == "fido-u2f" && !hasOnlyKeys(attObj.attStmt, "sig", "x5c")) {
    return Promise.reject("Invalid CBOR Attestation Statement");
  }
  if (
    attObj.fmt == "packed" &&
    !(
      hasOnlyKeys(attObj.attStmt, "alg", "sig") ||
      hasOnlyKeys(attObj.attStmt, "alg", "sig", "x5c")
    )
  ) {
    return Promise.reject("Invalid CBOR Attestation Statement");
  }
  if (attObj.fmt == "none" && Object.keys(attObj.attStmt).length) {
    return Promise.reject("Invalid CBOR Attestation Statement");
  }

  return webAuthnDecodeAuthDataArray(new Uint8Array(attObj.authData)).then(
    function (aAuthDataObj) {
      attObj.authDataObj = aAuthDataObj;
      return Promise.resolve(attObj);
    }
  );
}

function webAuthnDecodeAuthDataArray(aAuthData) {
  let rpIdHash = aAuthData.slice(0, 32);
  let flags = aAuthData.slice(32, 33);
  let counter = aAuthData.slice(33, 37);

  console.log(":: Authenticator Data ::");
  console.log("RP ID Hash: " + hexEncode(rpIdHash));
  console.log("Counter: " + hexEncode(counter) + " Flags: " + flags);

  if ((flags & flag_AT) == 0x00) {
    // No Attestation Data, so we're done.
    return Promise.resolve({
      rpIdHash,
      flags,
      counter,
    });
  }

  if (aAuthData.length < 38) {
    return Promise.reject(
      "Authenticator Data flag was set, but not enough data passed in!"
    );
  }

  let attData = {};
  attData.aaguid = aAuthData.slice(37, 53);
  attData.credIdLen = (aAuthData[53] << 8) + aAuthData[54];
  attData.credId = aAuthData.slice(55, 55 + attData.credIdLen);

  console.log(":: Authenticator Data ::");
  console.log("AAGUID: " + hexEncode(attData.aaguid));

  let cborPubKey = aAuthData.slice(55 + attData.credIdLen);
  var pubkeyObj = CBOR.decode(cborPubKey.buffer);
  if (
    !(
      cose_kty in pubkeyObj &&
      cose_alg in pubkeyObj &&
      cose_crv in pubkeyObj &&
      cose_crv_x in pubkeyObj &&
      cose_crv_y in pubkeyObj
    )
  ) {
    throw "Invalid CBOR Public Key Object";
  }
  if (pubkeyObj[cose_kty] != cose_kty_ec2) {
    throw "Unexpected key type";
  }
  if (pubkeyObj[cose_alg] != cose_alg_ECDSA_w_SHA256) {
    throw "Unexpected public key algorithm";
  }
  if (pubkeyObj[cose_crv] != cose_crv_P256) {
    throw "Unexpected curve";
  }

  let pubKeyBytes = assemblePublicKeyBytesData(
    pubkeyObj[cose_crv_x],
    pubkeyObj[cose_crv_y]
  );
  console.log(":: CBOR Public Key Object Data ::");
  console.log("kty: " + pubkeyObj[cose_kty] + " (EC2)");
  console.log("alg: " + pubkeyObj[cose_alg] + " (ES256)");
  console.log("crv: " + pubkeyObj[cose_crv] + " (P256)");
  console.log("X: " + pubkeyObj[cose_crv_x]);
  console.log("Y: " + pubkeyObj[cose_crv_y]);
  console.log("Uncompressed (hex): " + hexEncode(pubKeyBytes));

  return importPublicKey(pubKeyBytes).then(function (aKeyHandle) {
    return Promise.resolve({
      rpIdHash,
      flags,
      counter,
      attestationAuthData: attData,
      publicKeyBytes: pubKeyBytes,
      publicKeyHandle: aKeyHandle,
    });
  });
}

function importPublicKey(keyBytes) {
  if (keyBytes[0] != 0x04 || keyBytes.byteLength != 65) {
    throw "Bad public key octet string";
  }
  var jwk = {
    kty: "EC",
    crv: "P-256",
    x: bytesToBase64UrlSafe(keyBytes.slice(1, 33)),
    y: bytesToBase64UrlSafe(keyBytes.slice(33)),
  };
  return crypto.subtle.importKey(
    "jwk",
    jwk,
    { name: "ECDSA", namedCurve: "P-256" },
    true,
    ["verify"]
  );
}

function deriveAppAndChallengeParam(appId, clientData, attestation) {
  var appIdBuf = string2buffer(appId);
  return Promise.all([
    crypto.subtle.digest("SHA-256", appIdBuf),
    crypto.subtle.digest("SHA-256", clientData),
  ]).then(function (digests) {
    return {
      appParam: new Uint8Array(digests[0]),
      challengeParam: new Uint8Array(digests[1]),
      attestation,
    };
  });
}

function assemblePublicKeyBytesData(xCoord, yCoord) {
  // Produce an uncompressed EC key point. These start with 0x04, and then
  // two 32-byte numbers denoting X and Y.
  if (xCoord.length != 32 || yCoord.length != 32) {
    throw "Coordinates must be 32 bytes long";
  }
  let keyBytes = new Uint8Array(65);
  keyBytes[0] = 0x04;
  xCoord.map((x, i) => (keyBytes[1 + i] = x));
  yCoord.map((x, i) => (keyBytes[33 + i] = x));
  return keyBytes;
}

function assembleSignedData(appParam, flags, counter, challengeParam) {
  let signedData = new Uint8Array(32 + 1 + 4 + 32);
  new Uint8Array(appParam).map((x, i) => (signedData[0 + i] = x));
  signedData[32] = new Uint8Array(flags)[0];
  new Uint8Array(counter).map((x, i) => (signedData[33 + i] = x));
  new Uint8Array(challengeParam).map((x, i) => (signedData[37 + i] = x));
  return signedData;
}

function assembleRegistrationSignedData(
  appParam,
  challengeParam,
  keyHandle,
  pubKey
) {
  let signedData = new Uint8Array(1 + 32 + 32 + keyHandle.length + 65);
  signedData[0] = 0x00;
  new Uint8Array(appParam).map((x, i) => (signedData[1 + i] = x));
  new Uint8Array(challengeParam).map((x, i) => (signedData[33 + i] = x));
  new Uint8Array(keyHandle).map((x, i) => (signedData[65 + i] = x));
  new Uint8Array(pubKey).map(
    (x, i) => (signedData[65 + keyHandle.length + i] = x)
  );
  return signedData;
}

function sanitizeSigArray(arr) {
  // ECDSA signature fields into WebCrypto must be exactly 32 bytes long, so
  // this method strips leading padding bytes, if added, and also appends
  // padding zeros, if needed.
  if (arr.length > 32) {
    arr = arr.slice(arr.length - 32);
  }
  let ret = new Uint8Array(32);
  ret.set(arr, ret.length - arr.length);
  return ret;
}

function verifySignature(key, data, derSig) {
  if (derSig.byteLength < 68) {
    return Promise.reject(
      "Invalid signature (length=" +
        derSig.byteLength +
        "): " +
        hexEncode(new Uint8Array(derSig))
    );
  }

  // Copy signature data into the current context.
  let derSigCopy = new ArrayBuffer(derSig.byteLength);
  new Uint8Array(derSigCopy).set(new Uint8Array(derSig));

  let sigAsn1 = org.pkijs.fromBER(derSigCopy);

  // pkijs.asn1 seems to erroneously set an error code when calling some
  // internal function. The test suite doesn't like dangling globals.
  delete window.error;

  let sigR = new Uint8Array(
    sigAsn1.result.value_block.value[0].value_block.value_hex
  );
  let sigS = new Uint8Array(
    sigAsn1.result.value_block.value[1].value_block.value_hex
  );

  // The resulting R and S values from the ASN.1 Sequence must be fit into 32
  // bytes. Sometimes they have leading zeros, sometimes they're too short, it
  // all depends on what lib generated the signature.
  let R = sanitizeSigArray(sigR);
  let S = sanitizeSigArray(sigS);

  console.log("Verifying these bytes: " + bytesToBase64UrlSafe(data));

  let sigData = new Uint8Array(R.length + S.length);
  sigData.set(R);
  sigData.set(S, R.length);

  let alg = { name: "ECDSA", hash: "SHA-256" };
  return crypto.subtle.verify(alg, key, sigData, data);
}

async function addCredential(authenticatorId, rpId) {
  let keyPair = await crypto.subtle.generateKey(
    {
      name: "ECDSA",
      namedCurve: "P-256",
    },
    true,
    ["sign"]
  );

  let credId = new Uint8Array(32);
  crypto.getRandomValues(credId);
  credId = bytesToBase64UrlSafe(credId);

  let privateKey = await crypto.subtle
    .exportKey("pkcs8", keyPair.privateKey)
    .then(privateKey => bytesToBase64UrlSafe(privateKey));

  await SpecialPowers.spawnChrome(
    [authenticatorId, credId, rpId, privateKey],
    (authenticatorId, credId, rpId, privateKey) => {
      let webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
        Ci.nsIWebAuthnService
      );

      webauthnService.addCredential(
        authenticatorId,
        credId,
        true, // resident key
        rpId,
        privateKey,
        "VGVzdCBVc2Vy", // "Test User"
        0 // sign count
      );
    }
  );

  return credId;
}

async function removeCredential(authenticatorId, credId) {
  await SpecialPowers.spawnChrome(
    [authenticatorId, credId],
    (authenticatorId, credId) => {
      let webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
        Ci.nsIWebAuthnService
      );

      webauthnService.removeCredential(authenticatorId, credId);
    }
  );
}
