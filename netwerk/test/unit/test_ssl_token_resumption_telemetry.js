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
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { NodeHTTPProxyServer, NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const kHost = "ok.example.com";
const kDeclineHost = "decline-resumption.example.com";
const kDeclineRequestCertHost = "decline-resumption-request-cert.example.com";
const kStallOnResumeHost = "stall-on-resume.example.com";

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
    [kHost, kDeclineHost, kDeclineRequestCertHost, kStallOnResumeHost].join(",")
  );
  // Without a racing IPv6 socket, the only socket looks up the token.
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.dns.disableIPv6");
    Services.prefs.clearUserPref("network.dns.preferIPv6");
    Services.prefs.clearUserPref("network.http.connection-timeout");
    Services.prefs.clearUserPref("network.http.tls-handshake-timeout");
    Services.prefs.clearUserPref("network.ssl_tokens_cache_persistence");
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

add_task(
  needsFaultyServer(),
  async function test_session_token_rejected_through_proxy() {
    let origin = new NodeHTTPSServer();
    await origin.start(0, [kHost]);
    await origin.registerPathHandler("/", (req, resp) => {
      resp.writeHead(200);
      resp.end("ok");
    });
    let proxy = new NodeHTTPProxyServer();
    await proxy.start();
    try {
      // The proxy's own resolver doesn't know the test hostnames.
      await proxy.registerConnectHandler((req, clientSocket, head) => {
        const net = require("net");
        const { port } = new URL(`https://${req.url}`);
        const serverSocket = net.connect({ port, host: "127.0.0.1" }, () => {
          clientSocket.write("HTTP/1.1 200 Connection Established\r\n\r\n");
          serverSocket.write(head);
          serverSocket.pipe(clientSocket);
          clientSocket.pipe(serverSocket);
        });
      });
      await resetTokensAndTelemetry();
      getCacheServices().testCache.putSSLTokenForTest(
        `tlsflags0x00000000:${kHost}:${origin.port()}`
      );

      let [req, buf] = await openFreshConnection(
        kHost,
        CL_ALLOW_UNKNOWN_CL,
        origin.port()
      );
      ok(buf, "connection through the proxy succeeded");
      Assert.equal(
        req.QueryInterface(Ci.nsIProxiedChannel).httpProxyConnectResponseCode,
        200
      );
      assertTokenResumption({ "tcp_session/rejected": 1 });
    } finally {
      await proxy.stop();
      await origin.stop();
    }
  }
);

add_task(needsFaultyServer(), async function test_session_token_not_resumed() {
  await resetTokensAndTelemetry();

  let [, buf] = await openFreshConnection(kDeclineHost);
  ok(buf, "first connection succeeded");
  assertTokenResumption({});

  [, buf] = await openFreshConnection(kDeclineHost);
  ok(buf, "second connection succeeded with a full handshake");
  assertTokenResumption({ "tcp_session/not_resumed": 1 });
});

add_task(
  {
    ...needsFaultyServer(),
    pref_set: [["network.http.speculative-parallel-limit", 6]],
  },
  async function test_canceled_speculative_connection_not_counted() {
    await resetTokensAndTelemetry();
    let { testCache: tokens } = getCacheServices();

    let [, buf] = await openFreshConnection(kDeclineRequestCertHost);
    ok(buf, "first connection succeeded");
    Assert.equal(tokens.countSSLTokens(), 1);
    let certRequests = () =>
      Glean.security.clientAuthCertUsage.requested.testGetValue() ?? 0;
    let requestsBefore = certRequests();

    // The server declines the token and requests a client certificate, so the
    // unclaimed connection is canceled after looking it up.
    let uri = Services.io.newURI(`https://${kDeclineRequestCertHost}:8443/`);
    // Use the channel's origin attributes so the token cache key matches.
    Services.io
      .QueryInterface(Ci.nsISpeculativeConnect)
      .speculativeConnectWithOriginAttributes(uri, {}, null, false);
    await TestUtils.waitForCondition(
      () => certRequests() == requestsBefore + 1,
      "speculative connection got a certificate request"
    );
    Assert.equal(tokens.countSSLTokens(), 0);

    // FaultyServer serves one connection at a time, so this one completes only
    // after the speculative connection has been closed.
    [, buf] = await openFreshConnection(kDeclineRequestCertHost);
    ok(buf, "third connection succeeded");
    assertTokenResumption({});
  }
);

add_task(
  needsFaultyServer(),
  async function test_plaintext_token_not_counted_as_wrapped() {
    await resetTokensAndTelemetry();
    Services.prefs.setBoolPref("network.ssl_tokens_cache_persistence", true);

    let [, buf] = await openFreshConnection();
    ok(buf, "connection succeeded");
    Assert.equal(
      Glean.network.sslTokenCacheWrappedNotPersisted.testGetValue(),
      null
    );
    Services.prefs.clearUserPref("network.ssl_tokens_cache_persistence");
  }
);

add_task(
  needsFaultyServer(),
  async function test_session_token_failed_on_handshake_timeout() {
    await resetTokensAndTelemetry();
    let [, buf] = await openFreshConnection(kStallOnResumeHost);
    ok(buf, "first connection succeeded");

    // The server stalls the handshake for longer than either bound: Happy
    // Eyeballs' connection timeout, or the TLS handshake timeout without it.
    Services.prefs.setIntPref("network.http.connection-timeout", 1);
    Services.prefs.setIntPref("network.http.tls-handshake-timeout", 1);
    let [req] = await openFreshConnection(
      kStallOnResumeHost,
      CL_ALLOW_UNKNOWN_CL | CL_EXPECT_FAILURE
    );
    Assert.equal(req.status, Cr.NS_ERROR_NET_TIMEOUT);
    Services.prefs.clearUserPref("network.http.connection-timeout");
    Services.prefs.clearUserPref("network.http.tls-handshake-timeout");
    assertTokenResumption({ "tcp_session/failed": 1 });
  }
);

add_task(
  needsFaultyServer(),
  async function test_refused_connection_not_counted() {
    await resetTokensAndTelemetry();
    let [, buf] = await openFreshConnection();
    ok(buf, "first connection succeeded");

    // The server only listens on IPv4, so the IPv6 attempt looks up the token and
    // is refused before its handshake starts.
    Services.prefs.setBoolPref("network.dns.disableIPv6", false);
    Services.prefs.setBoolPref("network.dns.preferIPv6", true);
    Services.dns.clearCache(true);
    let [req] = await openFreshConnection();
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);
    Services.prefs.clearUserPref("network.dns.preferIPv6");
    Services.dns.clearCache(true);
    Assert.equal(req.securityInfo.resumed, false);
    assertTokenResumption({});
  }
);
