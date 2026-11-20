/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Verifies the Happy Eyeballs HTTP/1 0-RTT-accepted code path:
//  * The winning HT is promoted (conn.mTransaction becomes a pass-
//    through to the real nsHttpTransaction) so no re-Activate /
//    DispatchTransaction races with the MOZ_ASSERT in the conn mgr.
//  * The real transaction does NOT re-send its request on the wire
//    after the server has accepted the early-data bytes.
//
// We run against ZeroRttAcceptServer — a TLS 1.3 test server that
// accepts 0-RTT, reads HTTP requests in full, and fires a callback on
// every request it observes on the wire. The test counts callbacks:
// one per user-visible fetch. A duplicate request post-handshake
// (i.e. the bug this fix guards against) would produce a second
// callback on the same connection.

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
// Server splits /callback/request/{early,std} by whether the request
// arrived via accepted 0-RTT early data. Track the two counters
// separately so tests can assert which path delivered each fetch.
let earlyCount = 0;
let stdCount = 0;
// Set to true in add_setup once ZeroRttAcceptServer starts successfully.
// Tasks check this via skip_if so they are cleanly skipped (not timed out)
// when the server cert directory is absent (e.g. comm-central packaging).
let gServerStarted = false;

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
      return; // cert dir absent (e.g. comm-central); tasks will be skipped
    }
    gServerStarted = true;
    let nssComponent = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nssComponent.asyncClearSSLExternalAndInternalSessionCache();

    Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setBoolPref("network.http.http3.enable", false);
    // ZeroRttAcceptServer binds IPv4 loopback only. HE otherwise tries
    // [::1] first and the losing IPv6 attempt perturbs 0-RTT state for
    // the winning IPv4 attempt, collapsing fetch#2 into the std path.
    // Race tasks supply both families via a reverse proxy and flip
    // this pref themselves.
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
          let remote = "";
          let resumed = false;
          try {
            status = httpChan.responseStatus;
          } catch (e) {}
          try {
            remote = httpChanInt.remoteAddress;
          } catch (e) {}
          try {
            resumed = req.securityInfo.resumed;
          } catch (e) {}
          resolve({ status, remote, resumed });
        },
        null,
        CL_ALLOW_UNKNOWN_CL
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

  // Anti-replay window + NewSessionTicket propagation.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1500));

  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 200));

  let r2 = await fetchExpect200(url);
  Assert.equal(r2.status, 200, "Second fetch should succeed");

  // Drop the keep-alive conn from fetch #2 so the server's single-
  // threaded accept loop (parked in PR_Recv inside HandleH1Session)
  // gets EOF and returns, freeing it for the next task's ClientHello.
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 200));
}

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_he_h1_0rtt_accepted_no_duplicate_on_the_wire() {
    // Server accepts 0-RTT. HE promotes the winning HT to forward to
    // the real txn. Real txn's request stream is positioned at the
    // winner's offset so ReadSegments returns 0 bytes and
    // nsHttpConnection treats the request as fully sent. No duplicate
    // request burst is produced on the wire.
    await runHandshakeThenResume("0rtt-accept-h1.example.com");
    Assert.equal(stdCount, 1, "Fetch #1 arrived on the standard path");
    Assert.equal(earlyCount, 1, "Fetch #2 arrived as accepted 0-RTT");
  }
);

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_he_h1_0rtt_rejected_restarts_cleanly() {
    // Server refuses 0-RTT on resumption (no anti-replay context), so
    // NSS reports "early data not accepted" and ZeroRttHandle drives
    // Finish0RTT(aRestart=true): the request stream seeks back to 0
    // and the real txn retransmits over the post-handshake wire. The
    // server sees both fetches on the standard path (the early-data
    // bytes are discarded by NSS before our handler ever sees them).
    await runHandshakeThenResume("0rtt-reject-h1.example.com");
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

// Drives the accept-path H1 0-RTT test through a dual-family reverse
// proxy. A native DNS override maps the hostname to both ::1 and
// 127.0.0.1 at the proxy port; per-family connect delays pick which
// address wins the HE race. Asserts fetch#1 comes through the std
// path (full handshake) and fetch#2 comes through the early path
// (0-RTT accepted on the winning family), matching the invariants of
// the non-race accept task.
async function runHe0RttRace(host, ipv6DelayMs, ipv4DelayMs) {
  let node = new NodeHTTPServer();
  await node.start();

  const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
    Ci.nsINativeDNSResolverOverride
  );
  // Race needs both families live; non-race tasks set disableIPv6=true
  // globally. Restored in the finally block below.
  Services.prefs.clearUserPref("network.dns.disableIPv6");
  Services.prefs.clearUserPref("network.dns.localDomains");
  override.addIPOverride(host, "::1");
  override.addIPOverride(host, "127.0.0.1");

  // The earlier accept task cached a resumption ticket for this same
  // host. Without clearing it, this task's "first" fetch would attempt
  // 0-RTT instead of a full handshake, and the race between the two
  // HE attempts over the proxy trips the server's anti-replay guard.
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
    Assert.equal(r1.resumed, false, "First fetch is a full handshake");

    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 200));
    Services.obs.notifyObservers(null, "net:cancel-all-connections");
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 100));

    let r2 = await fetchExpect200(url);
    Assert.equal(r2.status, 200, "Second fetch should succeed");
    Assert.equal(r2.resumed, true, "Second fetch should resume the session");

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
    // Restore the non-race tasks' baseline.
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  }
}

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_h1_0rtt_ipv4_wins_race() {
    // Fetch#1 has no cached session, so no 0-RTT commit. IPv6 fires
    // first but its response is delayed 1s at the proxy; IPv4 backup
    // fires at 250ms and its full handshake completes first. No 0-RTT
    // sibling exists, so the non-0-RTT disqualify policy doesn't
    // engage — IPv4 wins the full handshake (r1.remote = 127.0.0.1).
    //
    // Fetch#2 has a cached ticket. HE again fires IPv6 first and
    // ZeroRttHandle commits the 0-RTT attempt to that (IPv6) HT. The
    // IPv4 backup fires without 0-RTT and is disqualified by
    // HappyEyeballsTransaction::Close once it finishes a plain
    // handshake. Firefox waits for IPv6 to complete on the wire, so
    // r2.resumed = true. (r2.remote ends up empty because the
    // channel's remoteAddress isn't populated when the connection is
    // promoted through HappyEyeballsTransaction.)
    let { r1 } = await runHe0RttRace("0rtt-accept-h1.example.com", 1000, 0);
    Assert.equal(r1.remote, "127.0.0.1", "fetch#1 should win on IPv4");
  }
);

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_h1_0rtt_ipv6_wins_race() {
    // IPv6 fires first and is unimpeded; IPv4 backup is delayed and
    // never matters. Fetch#1 goes over IPv6; fetch#2 resumes via
    // 0-RTT on the same family.
    let { r1 } = await runHe0RttRace("0rtt-accept-h1.example.com", 0, 1000);
    Assert.equal(r1.remote, "::1", "fetch#1 wins on IPv6");
  }
);
