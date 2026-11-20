/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test: when a losing HCA is abandoned while it already locked its
// real transaction out of the pending queue (via LockInRealTxnFromPendingQueue
// during Do0RTT), the transaction must be re-queued.
//
// Root cause:
//   Two concurrent requests each trigger a separate HappyEyeballsConnectionAttempt
//   (HCA_A and HCA_B). Both HCAs enter the 0-RTT flow — each calling Do0RTT
//   which calls LockInRealTxnFromPendingQueue, removing their respective real
//   transactions from the CM pending queue.
//
//   When HCA_B wins TLS it calls MakeAllDontReuseExcept →
//   CloseAllConnectionAttempts → HCA_A->Abandon().  HCA_A's Abandon() now
//   finds its real transaction: not on any connection and not in the pending
//   queue.  Without the fix the transaction is silently dropped.  With the fix
//   it is re-queued via AddTransaction so the CM can dispatch it on the
//   winning H2 session.
//
// Setup:
//   ZeroRttAcceptServer now sends TWO NewSessionTickets per connection.
//   SSLTokensCache::Get is single-use, so both concurrent connections can each
//   consume one ticket and start 0-RTT independently.
//
//   • Warm-up fetch (IPv4 only) → two session tickets stored in the cache
//   • Two concurrent requests: both start 0-RTT, one wins, other is re-queued
//   • Without fix: second request hangs (transaction dropped) → TIMEOUT → FAIL
//   • With fix: second request is re-queued and served → HTTP 200 → PASS

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);
var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

let callbackServer;
let gServerStarted = false;

add_setup(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async () => {
    callbackServer = new HttpServer();
    callbackServer.registerPrefixHandler("/callback/", () => {});
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

    let nss = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nss.asyncClearSSLExternalAndInternalSessionCache();

    Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setBoolPref("network.http.http3.enable", false);
    // Raise the per-host connection limit so two new HCAs can race
    // simultaneously even while the warm-up H2 session is still alive.
    // With the default of 2, the warm-up H2 (1 active) + 1 new HCA = 2,
    // which is the limit; the second concurrent request would be dispatched
    // on the warm-up H2 instead of creating a second HCA. With 3, both
    // concurrent requests that cannot reuse the warm-up H2 get their own HCA.
    Services.prefs.setIntPref(
      "network.http.max-persistent-connections-per-server",
      3
    );

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
      Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
      Services.prefs.clearUserPref("network.http.http3.enable");
      Services.prefs.clearUserPref(
        "network.http.max-persistent-connections-per-server"
      );
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      override.clearOverrides();
      if (callbackServer) {
        await callbackServer.stop();
        callbackServer = null;
      }
    });
  }
);

// Reverse TCP proxy shared on ::1 and 127.0.0.1 at the same ephemeral port,
// forwarding raw TLS bytes to ZeroRttAcceptServer at 127.0.0.1:8443.
async function startRaceProxy(node, ipv6Ms, ipv4Ms) {
  return node.execute(`
    (function() {
      const net = require("net");
      function forward(client, delayMs) {
        let buf = [], dead = false;
        client.on("data", c => buf.push(c));
        ["error","end","close"].forEach(e => client.on(e, () => { dead = true; }));
        setTimeout(() => {
          if (dead) { try { client.destroy(); } catch(_) {} return; }
          const backend = net.connect(8443, "127.0.0.1", () => {
            for (const c of buf) backend.write(c);
            buf = null;
            client.removeAllListeners("data");
            client.on("data", c => backend.write(c));
            backend.on("data", c => { try { client.write(c); } catch(_) {} });
            backend.on("end", () => { try { client.end(); } catch(_) {} });
            client.on("end", () => backend.end());
            backend.on("error", () => client.destroy());
            client.on("error", () => backend.destroy());
          });
          backend.on("error", () => client.destroy());
        }, delayMs);
      }
      const p6 = net.createServer(s => forward(s, ${ipv6Ms}));
      const p4 = net.createServer(s => forward(s, ${ipv4Ms}));
      return new Promise((res, rej) => {
        p6.once("error", rej);
        p6.listen(0, "::1", () => {
          const port = p6.address().port;
          p4.once("error", rej);
          p4.listen(port, "127.0.0.1", () => {
            global.__raceProxy = { p6, p4 };
            res(port);
          });
        });
      });
    })()
  `);
}

async function stopRaceProxy(node) {
  await node.execute(`
    if (global.__raceProxy) {
      global.__raceProxy.p6.close();
      global.__raceProxy.p4.close();
      global.__raceProxy = null;
    }
  `);
}

// Channel listener that does not call do_throw on failure, so we can detect
// a hang via timeout rather than crashing.
function fetchNoThrow(url) {
  const chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  // LOAD_BYPASS_CACHE prevents the cache from serializing concurrent requests
  // by locking the same cache entry. Without it, the second and third channels
  // do not reach AddTransaction until the first channel's cache validation
  // completes (~100 ms), leaving only one HCA in the race.
  chan.loadFlags =
    Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI | Ci.nsIRequest.LOAD_BYPASS_CACHE;

  const promise = new Promise(resolve => {
    chan.asyncOpen({
      onStartRequest() {},
      onDataAvailable(_req, stream, _offset, count) {
        read_stream(stream, count);
      },
      onStopRequest(req, status) {
        if (Components.isSuccessCode(status)) {
          resolve({
            ok: true,
            status: req.QueryInterface(Ci.nsIHttpChannel).responseStatus,
          });
        } else {
          resolve({ ok: false, status: 0, error: status });
        }
      },
      QueryInterface: ChromeUtils.generateQI(["nsIStreamListener"]),
    });
  });

  return { chan, promise };
}

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_abandoned_0rtt_hca_requeuees_real_transaction() {
    const host = "0rtt-accept-h2.example.com";
    const node = new NodeHTTPServer();
    await node.start();

    try {
      let nss = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
        Ci.nsISSLTokensCache
      );
      await nss.asyncClearSSLExternalAndInternalSessionCache();

      override.clearOverrides();
      override.addIPOverride(host, "::1");
      override.addIPOverride(host, "127.0.0.1");

      // IPv6 proxy delays forwarding by 100 ms; IPv4 proxy is immediate.
      // This ensures the IPv4 HCA (HCA_B) always wins TLS before the IPv6
      // HCA (HCA_A) has a chance to complete its handshake on the backend.
      // HCA_A is therefore always the losing/abandoned HCA.
      const port = await startRaceProxy(node, 100, 0);
      const url = `https://${host}:${port}/`;

      // Warm-up: single-family (IPv4) TLS handshake to populate the session
      // cache.  ZeroRttAcceptServer emits TWO NewSessionTickets per
      // connection, so the cache ends up with two tokens for this host:port.
      // SSLTokensCache::Get is single-use, so each concurrent connection
      // below consumes its own token and starts 0-RTT independently.
      Services.prefs.setBoolPref("network.dns.disableIPv6", true);
      const warmup = fetchNoThrow(url);
      const wu = await warmup.promise;
      Assert.ok(wu.ok, "warm-up fetch should succeed");
      Assert.equal(wu.status, 200, "warm-up should return 200");
      Services.prefs.setBoolPref("network.dns.disableIPv6", false);

      // Wait for both NewSessionTickets to propagate and the anti-replay window
      // to open, then drop the warm-up connection.
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 1500));
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));

      // Three concurrent requests. The warm-up H2 session is still alive
      // and absorbs one of them immediately. The other two must open fresh
      // connections and race as HCA_A (IPv6, 100 ms proxy delay) and
      // HCA_B (IPv4, 0 ms proxy delay).
      //
      // Both HCAs find a session ticket in the cache and call
      // LockInRealTxnFromPendingQueue immediately (client-side, before any
      // bytes reach the backend).  The IPv4 HCA wins TLS first, which calls
      // ReportSpdyConnection → MakeAllDontReuseExcept →
      // CloseAllConnectionAttempts → Abandon() on the IPv6 HCA.
      //
      // The IPv6 HCA already removed its real transaction from the CM pending
      // queue but that transaction was never adopted onto a connection.
      // Without the fix the transaction is silently dropped.  With the fix
      // Abandon() re-queues it and the CM dispatches it on the winning H2
      // session.
      const TIMEOUT_MS = 10000;
      let timedOut = false;

      const fA = fetchNoThrow(url);
      const fB = fetchNoThrow(url);
      const fC = fetchNoThrow(url);

      const results = await Promise.race([
        Promise.all([fA.promise, fB.promise, fC.promise]),
        new Promise(r =>
          // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
          setTimeout(() => {
            timedOut = true;
            r(null);
          }, TIMEOUT_MS)
        ),
      ]);

      if (timedOut) {
        try {
          fA.chan.cancel(Cr.NS_ERROR_ABORT);
        } catch (_) {}
        try {
          fB.chan.cancel(Cr.NS_ERROR_ABORT);
        } catch (_) {}
        try {
          fC.chan.cancel(Cr.NS_ERROR_ABORT);
        } catch (_) {}
        await Promise.all([fA.promise, fB.promise, fC.promise]);
      }

      Assert.ok(
        !timedOut,
        "All three concurrent 0-RTT requests must complete — " +
          "HappyEyeballsConnectionAttempt::Abandon must re-queue the " +
          "real transaction when LockInRealTxnFromPendingQueue already " +
          "removed it from the pending queue"
      );
      if (!timedOut) {
        const [rA, rB, rC] = results;
        Assert.ok(rA.ok, "first concurrent request should succeed");
        Assert.equal(rA.status, 200, "first concurrent request: 200");
        Assert.ok(rB.ok, "second concurrent request should succeed");
        Assert.equal(rB.status, 200, "second concurrent request: 200");
        Assert.ok(rC.ok, "third concurrent request should succeed");
        Assert.equal(rC.status, 200, "third concurrent request: 200");
      }
    } finally {
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));
      await stopRaceProxy(node);
      await node.stop();
      override.clearOverrides();
    }
  }
);
