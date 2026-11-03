// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

"use strict";

// Checks that OneCRL loads its remote settings bundle at startup by ensuring
// that each of the listed eMudhra intermediate certificates is revoked.

do_get_profile();

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { RemoteSecuritySettings } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/RemoteSecuritySettings.sys.mjs"
);

// DER encoding of the issuer name (CN=emSign Root TLS CA - G3), base64.
const ISSUER =
  "MFYxCzAJBgNVBAYTAklOMSUwIwYDVQQKDBxlTXVkaHJhIFRlY2hub2xvZ2llcyBMaW1pdGVkMSAwHgYDVQQDDBdlbVNpZ24gUm9vdCBUTFMgQ0EgLSBHMw==";

// Serial numbers are the contents of the DER INTEGER, in hex.
const SERIALS = {
  "EM DV TLS CA - G3B": "00a4aee678f23362b4c0e460b772f214",
  "EM OV TLS CA - G3B": "6cada367fb1c100088120ea202d568",
  "EM EV TLS CA - G3B": "00c7c41efe92d825f05eae841a4a82c1",
  "EM DV TLS CA - G3C": "588b2e3c2bb8cda13e1a1209254321",
  "EM OV TLS CA - G3C": "296926c227a3a1526392b19482a504",
  "EM EV TLS CA - G3C": "008009928a64107601af05fa2f19f109",
};

function base64ToBytes(base64) {
  return Array.from(atob(base64), c => c.charCodeAt(0));
}

function hexToBytes(hex) {
  return hex.match(/../g).map(byte => parseInt(byte, 16));
}

add_task(async function test_emsign_intermediates_are_revoked() {
  let loaded = TestUtils.topicObserved(
    "remote-security-settings:onecrl-bundle-loaded"
  );
  RemoteSecuritySettings.init();
  await loaded;

  let certStorage = Cc["@mozilla.org/security/certstorage;1"].getService(
    Ci.nsICertStorage
  );
  let issuer = base64ToBytes(ISSUER);

  for (let [name, serial] of Object.entries(SERIALS)) {
    Assert.equal(
      certStorage.getRevocationState(issuer, hexToBytes(serial), [], []),
      Ci.nsICertStorage.STATE_ENFORCE,
      `${name} should be revoked`
    );
  }
});
