/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that when a server sends a fatal TLS alert on a session resumption
// attempt over an HTTPS-RR-routed connection, Firefox retries on the SAME
// alt-route (with a fresh handshake, no PSK) instead of stripping the route
// and falling back to the bare origin port.
//
// Without route preservation, PrepareConnInfoForRetry strips the alt-route
// (because echConfig is empty) and the retry hits the bare origin port,
// which has no listener in this test rig — surfacing CONNECTION_REFUSED to
// the user as "Unable to connect" instead of recovering with a fresh
// handshake on the alt-port.
//
// Both common alert variants are covered:
//   - SSL_ERROR_ILLEGAL_PARAMETER_ALERT (e.g. PSK binder verification failure)
//   - SSL_ERROR_DECRYPT_ERROR_ALERT     (e.g. server STEK rotation)
// Both map to NS_ERROR_MODULE_SECURITY, so ShouldRestartOnResumptionError
// treats them identically.
//
// Companion: test_retry_illegal_parameter.js / test_retry_decrypt_error.js
// (no HTTPS RR — verify the fix is a no-op when mOrigConnInfo is null).

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// FaultyServer listens on a fixed port (TLSServer.cpp LISTEN_PORT) and
// matches behavior by SNI; these hostnames trigger the resumption-time
// alerts we want to exercise.
const kAltPort = 8443;
const kIllegalParameterHost = "illegal-parameter-on-resume.example.com";
const kDecryptErrorHost = "decrypt-error-on-resume.example.com";
// URL port used by test channels. Nothing must be listening here so a
// route-stripping fallback fails clearly with CONNECTION_REFUSED.
const kOriginPort = 8765;

// Fresh object per call: xpcshell's add_test mutates the options it receives
// (sets isTask/isSetup), so a shared const would fail on its second use.
const skipIfSystemNSS = () => ({ skip_if: () => AppConstants.MOZ_SYSTEM_NSS });

let httpServer;
let trrServer;
let resumeCallbackCount = 0;

async function registerHTTPSRR(host) {
  await trrServer.registerDoHAnswers(
    `_${kOriginPort}._https.${host}`,
    "HTTPS",
    {
      answers: [
        {
          name: `_${kOriginPort}._https.${host}`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 1,
            name: host,
            values: [
              { key: "alpn", value: "h2" },
              { key: "port", value: kAltPort },
            ],
          },
        },
      ],
    }
  );
  await trrServer.registerDoHAnswers(host, "A", {
    answers: [
      {
        name: host,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });
}

// Local makeChan: HTTPS-RR routing is disallowed for system-principal
// channels whose content policy type is not TYPE_DOCUMENT (see
// nsHttpChannel.cpp httpsRRAllowed). The shared head_channels.js makeChan
// doesn't set contentPolicyType, so we need our own.
function makeDocChan(url) {
  return NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
}

// Opens an HTTPS channel to host:kOriginPort and asserts it succeeded and
// that FaultyServer fired its callback `expectedFaults` times during this
// connection.
async function connectAndAssert(host, expectedFaults, label) {
  const before = resumeCallbackCount;
  const chan = makeDocChan(`https://${host}:${kOriginPort}/`);
  const [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  ok(buf, `${host}: ${label}: connection succeeded`);
  equal(
    resumeCallbackCount - before,
    expectedFaults,
    `${host}: ${label}: FaultyServer fired ${expectedFaults} time(s)`
  );
}

add_setup(skipIfSystemNSS(), async () => {
  httpServer = new HttpServer();
  httpServer.registerPathHandler("/callback/1", () => {
    resumeCallbackCount++;
  });
  httpServer.start(-1);
  registerCleanupFunction(async () => httpServer.stop());
  await asyncSetupFaultyServer(httpServer);

  trr_test_setup();
  for (const [name, value] of [
    ["network.dns.upgrade_with_https_rr", true],
    ["network.dns.use_https_rr_as_altsvc", true],
    ["network.dns.echconfig.enabled", false],
  ]) {
    Services.prefs.setBoolPref(name, value);
    registerCleanupFunction(() => Services.prefs.clearUserPref(name));
  }

  trrServer = new TRRServer();
  await trrServer.start();
  registerCleanupFunction(async () => trrServer.stop());
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  registerCleanupFunction(() => trr_clear_prefs());

  await registerHTTPSRR(kIllegalParameterHost);
  await registerHTTPSRR(kDecryptErrorHost);
});

// One round: fresh handshake (populates the session-ticket cache, no fault),
// then a resumption attempt that the server rejects (fault count +1). With
// the fix, the retry stays on the alt-route and succeeds; without it, the
// retry hits the bare origin port (no listener) and CONNECTION_REFUSED
// surfaces as a channel error.
async function testResumptionRetry(host) {
  await connectAndAssert(host, 0, "fresh handshake");
  // The server's anti-replay window prohibits accepting 0-RTT immediately
  // after issuing a ticket.
  await sleep(1);
  await connectAndAssert(host, 1, "resumption rejected, retry on alt-route");
}

add_task(skipIfSystemNSS(), async function test_illegal_parameter() {
  await testResumptionRetry(kIllegalParameterHost);
});

add_task(skipIfSystemNSS(), async function test_decrypt_error() {
  await testResumptionRetry(kDecryptErrorHost);
});
