/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Verifies the Happy Eyeballs HTTP/2 0-RTT code paths against
// ZeroRttAcceptServer. Mirrors test_happy_eyeballs_h1_0rtt.js:
//
//  * accepted case: 0rtt-accept-h2.example.com — the server is
//    configured with NSS's anti-replay context so it accepts the
//    client's early-data HEADERS on resumption. HE promotes the
//    winning HT and no duplicate HEADERS frame reaches the wire.
//
//  * rejected case: 0rtt-reject-h2.example.com — the server skips
//    anti-replay context on purpose, so NSS refuses the early data
//    and the client takes Finish0RTT(aRestart=true): the request
//    stream seeks back to 0 and the real txn retransmits after
//    Finished.
//
// The server splits its request callback path by outcome so each
// task can assert which code path actually delivered the fetch.

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let callbackServer;
let gServerStarted = false;
let earlyCount = 0;
let stdCount = 0;

function callbackHandler(metadata) {
  if (metadata.path === "/callback/request/early") {
    earlyCount++;
  } else if (metadata.path === "/callback/request/std") {
    stdCount++;
  }
}

function resetCounts() {
  earlyCount = 0;
  stdCount = 0;
}

add_setup(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async () => {
    callbackServer = new HttpServer();
    callbackServer.registerPrefixHandler("/callback/", callbackHandler);
    callbackServer.start(-1);

    Services.env.set(
      "MOZ_ZERORTT_ACCEPT_CALLBACK_PORT",
      callbackServer.identity.primaryPort
    );
    Services.env.set("MOZ_TLS_SERVER_0RTT", "1");
    const started = await asyncStartTLSTestServer(
      "ZeroRttAcceptServer",
      "../../../security/manager/ssl/tests/unit/test_faulty_server"
    );
    if (!started) {
      return;
    }
    gServerStarted = true;
    let nssComponent = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nssComponent.asyncClearSSLExternalAndInternalSessionCache();

    Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setBoolPref("network.http.http3.enable", false);
    // Mirror H1 rationale: the test server listens on IPv4 only, and
    // letting HE try IPv6 first will burn the single-use session ticket.
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
      Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
      Services.prefs.clearUserPref("network.http.http3.enable");
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      if (callbackServer) {
        await callbackServer.stop();
      }
    });
  }
);

function fetchExpect200(url) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: url,
      loadUsingSystemPrincipal: true,
    }).QueryInterface(Ci.nsIHttpChannel);
    chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
    chan.asyncOpen(
      new ChannelListener(
        req => {
          let httpChan = req.QueryInterface(Ci.nsIHttpChannel);
          let httpChanInt = req.QueryInterface(Ci.nsIHttpChannelInternal);
          let status = 0;
          let protocol = "";
          let remote = "";
          let resumed = false;
          try {
            status = httpChan.responseStatus;
            protocol = httpChan.protocolVersion;
          } catch (e) {}
          try {
            remote = httpChanInt.remoteAddress;
          } catch (e) {}
          try {
            resumed = req.securityInfo.resumed;
          } catch (e) {}
          resolve({ status, protocol, remote, resumed });
        },
        null,
        CL_ALLOW_UNKNOWN_CL | CL_EXPECT_GZIP
      )
    );
  });
}

async function runHandshakeThenResume(host) {
  Services.prefs.setCharPref("network.dns.localDomains", host);
  const url = `https://${host}:8443/`;

  resetCounts();

  let r1 = await fetchExpect200(url);
  Assert.equal(r1.status, 200, "First fetch should succeed");
  Assert.equal(r1.protocol, "h2", "First fetch should be h2");

  // Anti-replay window + NewSessionTicket propagation.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1500));

  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 200));

  let r2 = await fetchExpect200(url);
  Assert.equal(r2.status, 200, "Second fetch should succeed");
  Assert.equal(r2.protocol, "h2", "Second fetch should be h2");

  // Drop the keep-alive conn from fetch #2 so the server's single-
  // threaded accept loop (parked in PR_Recv inside HandleH2Session)
  // gets EOF and returns, freeing it for the next task's ClientHello.
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 200));
}

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_he_h2_0rtt_accepted_no_duplicate_on_the_wire() {
    await runHandshakeThenResume("0rtt-accept-h2.example.com");
    Assert.equal(stdCount, 1, "Fetch #1 arrived on the standard path");
    Assert.equal(earlyCount, 1, "Fetch #2 arrived as accepted 0-RTT");
  }
);

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_he_h2_0rtt_rejected_restarts_cleanly() {
    // Server refuses 0-RTT on resumption (no anti-replay context). NSS
    // reports "early data not accepted"; ZeroRttHandle drives
    // Finish0RTT(aRestart=true), the request stream seeks back to 0,
    // and the H2 stream sends HEADERS for the real txn post-Finished.
    // Both fetches reach the handler on the standard path.
    await runHandshakeThenResume("0rtt-reject-h2.example.com");
    Assert.equal(earlyCount, 0, "No request should arrive as 0-RTT");
    Assert.equal(stdCount, 2, "Both fetches arrived on the standard path");
  }
);

// Reverse TCP proxy that listens on ::1 and 127.0.0.1 at the same
// port and forwards raw bytes to the TLS server on 127.0.0.1:8443.
// Each family can be artificially delayed so HE picks a specific
// winner in the 0-RTT resumption race. TLS is not terminated — the
// ClientHello's SNI and early data pass through intact, so the
// backend still decides ALPN / 0-RTT accept per our sHosts entries.
async function startFamilyDelayProxy(node, ipv6DelayMs, ipv4DelayMs) {
  return node.execute(`
    (function() {
      const net = require("net");
      function pipeToBackend(clientSocket, delay) {
        let buffered = [];
        let clientDead = false;
        clientSocket.on("data", (chunk) => buffered.push(chunk));
        clientSocket.on("error", () => { clientDead = true; });
        // Track client-side teardown up front: the HE race deliberately
        // cancels the losing attempt mid-delay, so without these early
        // listeners the deferred net.connect below would forward a
        // stale ClientHello to the single-threaded backend and park its
        // accept loop on a dead conn.
        clientSocket.on("end", () => { clientDead = true; });
        clientSocket.on("close", () => { clientDead = true; });

        setTimeout(() => {
          if (clientDead) {
            try { clientSocket.destroy(); } catch(e) {}
            return;
          }
          const backendSocket = net.connect(8443, "127.0.0.1", () => {
            for (const chunk of buffered) {
              backendSocket.write(chunk);
            }
            buffered = null;
            clientSocket.removeAllListeners("data");
            clientSocket.on("data", (chunk) => backendSocket.write(chunk));
            backendSocket.on("data", (chunk) => {
              try { clientSocket.write(chunk); } catch(e) {}
            });
            backendSocket.on("end", () => {
              try { clientSocket.end(); } catch(e) {}
            });
            clientSocket.on("end", () => backendSocket.end());
            backendSocket.on("error", () => clientSocket.destroy());
            clientSocket.on("error", () => backendSocket.destroy());
          });
          backendSocket.on("error", () => clientSocket.destroy());
        }, delay);
      }
      function makeProxy(delay) {
        return net.createServer((clientSocket) => {
          pipeToBackend(clientSocket, delay);
        });
      }
      const proxy6 = makeProxy(${ipv6DelayMs});
      const proxy4 = makeProxy(${ipv4DelayMs});
      return new Promise((resolve, reject) => {
        proxy6.once("error", reject);
        proxy6.listen(0, "::1", () => {
          const port = proxy6.address().port;
          proxy4.once("error", reject);
          proxy4.listen(port, "127.0.0.1", () => {
            global.delayProxy6 = proxy6;
            global.delayProxy4 = proxy4;
            resolve(port);
          });
        });
      });
    })()
  `);
}

async function stopFamilyDelayProxy(node) {
  await node.execute(`
    if (global.delayProxy6) { global.delayProxy6.close(); global.delayProxy6 = null; }
    if (global.delayProxy4) { global.delayProxy4.close(); global.delayProxy4 = null; }
  `);
}

// Drives the accept-path H2 0-RTT test through a dual-family reverse
// proxy. A native DNS override maps the hostname to both ::1 and
// 127.0.0.1 at the proxy port; per-family connect delays pick which
// address wins the HE race. Same structure as the H1 race helper —
// see that file for a full walkthrough of the lifecycle.
async function runHe0RttRace(host, ipv6DelayMs, ipv4DelayMs) {
  let node = new NodeHTTPServer();
  await node.start();

  const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
    Ci.nsINativeDNSResolverOverride
  );
  Services.prefs.clearUserPref("network.dns.disableIPv6");
  Services.prefs.clearUserPref("network.dns.localDomains");
  override.addIPOverride(host, "::1");
  override.addIPOverride(host, "127.0.0.1");

  let nssComponent = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
    Ci.nsISSLTokensCache
  );
  await nssComponent.asyncClearSSLExternalAndInternalSessionCache();

  let proxyPort = await startFamilyDelayProxy(node, ipv6DelayMs, ipv4DelayMs);
  const url = `https://${host}:${proxyPort}/`;

  try {
    resetCounts();

    let r1 = await fetchExpect200(url);
    Assert.equal(r1.status, 200, "First fetch should succeed");
    Assert.equal(r1.protocol, "h2", "First fetch should be h2");
    Assert.equal(r1.resumed, false, "First fetch is a full handshake");

    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 200));
    Services.obs.notifyObservers(null, "net:cancel-all-connections");
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 100));

    let r2 = await fetchExpect200(url);
    Assert.equal(r2.status, 200, "Second fetch should succeed");
    Assert.equal(r2.protocol, "h2", "Second fetch should be h2");
    // r2.resumed is observed to vary here: under H2, whether the
    // channel's securityInfo reports resumed depends on which HE
    // attempt ended up driving the promoted Http2Stream, so we log
    // it for visibility rather than assert.

    info(`fetch#1 remote=${r1.remote} resumed=${r1.resumed}`);
    info(`fetch#2 remote=${r2.remote} resumed=${r2.resumed}`);
    return { r1, r2 };
  } finally {
    Services.obs.notifyObservers(null, "net:cancel-all-connections");
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 200));
    await stopFamilyDelayProxy(node);
    await node.stop();
    override.clearOverrides();
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  }
}

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_h2_0rtt_ipv4_wins_race() {
    // See H1 ipv4_wins_race: fetch#1 falls to IPv4 because IPv6's
    // response is proxy-delayed past the HE backup window; fetch#2
    // resumes the TLS session, with the 0-RTT commit going to the
    // first-fired (IPv6) HE attempt.
    let { r1 } = await runHe0RttRace("0rtt-accept-h2.example.com", 1000, 0);
    Assert.equal(r1.remote, "127.0.0.1", "fetch#1 should win on IPv4");
  }
);

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_h2_0rtt_ipv6_wins_race() {
    // IPv6 is unimpeded and wins both fetches; fetch#2 resumes via
    // 0-RTT on the same family.
    let { r1 } = await runHe0RttRace("0rtt-accept-h2.example.com", 0, 1000);
    Assert.equal(r1.remote, "::1", "fetch#1 wins on IPv6");
  }
);
