/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that network.ssl_token_resumption records the outcome of TCP
// connections that looked up a cached token, and nothing for those that didn't.

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const kHost = "ok.example.com";
const kDeclineHost = "decline-resumption.example.com";

function openFreshConnection(
  aHost = kHost,
  aFlags = CL_ALLOW_UNKNOWN_CL,
  aPort = 8443
) {
  let chan = makeChan(`https://${aHost}:${aPort}/`);
  chan.loadFlags |=
    Ci.nsIRequest.LOAD_FRESH_CONNECTION | Ci.nsIRequest.LOAD_BYPASS_CACHE;
  return channelOpenPromise(chan, aFlags);
}

// Bug 1805371: Tests that require FaultyServer can't currently be built
// with system NSS. A function, because add_task() modifies its properties.
function needsFaultyServer() {
  return { skip_if: () => AppConstants.MOZ_SYSTEM_NSS };
}

add_setup(needsFaultyServer(), async () => {
  do_get_profile();
  Services.fog.initializeFOG();
  let httpServer = new HttpServer();
  httpServer.start(-1);
  registerCleanupFunction(async () => {
    await httpServer.stop();
  });
  await asyncSetupFaultyServer(httpServer, { use0RTT: false });
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    [kHost, kDeclineHost].join(",")
  );
  // Without a racing IPv6 socket, the only socket looks up the token.
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.dns.disableIPv6");
  });
});

add_task(needsFaultyServer(), async function test_session_token_resumed() {
  await resetTokensAndTelemetry();

  let [, buf] = await openFreshConnection();
  ok(buf, "first connection succeeded");
  assertTokenResumption({});

  [, buf] = await openFreshConnection();
  ok(buf, "second connection succeeded");
  assertTokenResumption({ "tcp_session/resumed": 1 });
});

add_task(needsFaultyServer(), async function test_session_token_rejected() {
  await resetTokensAndTelemetry();
  // The record decodes, but its NSS token doesn't.
  getCacheServices().testCache.putSSLTokenForTest(
    `tlsflags0x00000000:${kHost}:8443`
  );

  let [, buf] = await openFreshConnection();
  ok(buf, "connection succeeded with a full handshake");
  assertTokenResumption({ "tcp_session/rejected": 1 });
});

add_task(needsFaultyServer(), async function test_session_token_not_resumed() {
  await resetTokensAndTelemetry();

  let [, buf] = await openFreshConnection(kDeclineHost);
  ok(buf, "first connection succeeded");
  assertTokenResumption({});

  [, buf] = await openFreshConnection(kDeclineHost);
  ok(buf, "second connection succeeded with a full handshake");
  assertTokenResumption({ "tcp_session/not_resumed": 1 });
});
