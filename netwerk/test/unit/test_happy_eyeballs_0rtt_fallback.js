/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test: TCP 0-RTT with alpnChanged=1 (H2 ticket, server responds H1)
// must not declare the dying connection the HE winner and insert it into
// mActiveConns.  With mUsingSpdy=true on the CM entry, RestrictConnections()
// would permanently block all new connection attempts for the host.
//
// Setup: warm-up over IPv4-only establishes mUsingSpdy=true and leaves an H2
// session ticket; the race (IPv4 0 ms, IPv6 100 ms) fires 0-RTT with that
// ticket while the server now negotiates H1, triggering alpnChanged=1.

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
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
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
    // Make the H2 idle timeout very short so the warm-up H2 session is
    // reclaimed quickly, leaving the CM entry alive (mUsingSpdy=true) but
    // mActiveConns empty before the race starts.
    Services.prefs.setIntPref("network.http.http2.timeout", 2);

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
      Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
      Services.prefs.clearUserPref("network.http.http3.enable");
      Services.prefs.clearUserPref("network.http.http2.timeout");
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      override.clearOverrides();
      if (callbackServer) {
        await callbackServer.stop();
        callbackServer = null;
      }
    });
  }
);

// Mutable proxy: constant port (same ConnectionInfo key throughout) with
// configurable per-family delay.
async function startMutableProxy(node) {
  return node.execute(`
    (function() {
      const net = require("net");
      global.__proxy = { ipv6Ms: 0, ipv4Ms: 0, p6: null, p4: null };
      function fwd(client, family) {
        const ms = global.__proxy[family + "Ms"];
        let buf = [], dead = false;
        client.on("data", c => buf.push(c));
        ["error","end","close"].forEach(e => client.on(e, () => { dead = true; }));
        setTimeout(() => {
          if (dead) { try { client.destroy(); } catch(_) {} return; }
          const b = net.connect(8443, "127.0.0.1", () => {
            buf.forEach(c => b.write(c)); buf = null;
            client.removeAllListeners("data");
            client.on("data", c => b.write(c));
            b.on("data", c => { try { client.write(c); } catch(_) {} });
            b.on("end", () => { try { client.end(); } catch(_) {} });
            client.on("end", () => b.end());
            b.on("error", () => client.destroy());
            client.on("error", () => b.destroy());
          });
          b.on("error", () => client.destroy());
        }, ms);
      }
      const p6 = net.createServer(s => fwd(s, "ipv6"));
      const p4 = net.createServer(s => fwd(s, "ipv4"));
      global.__proxy.p6 = p6; global.__proxy.p4 = p4;
      return new Promise((res, rej) => {
        p6.once("error", rej);
        p6.listen(0, "::1", () => {
          const port = p6.address().port;
          p4.once("error", rej);
          p4.listen(port, "127.0.0.1", () => res(port));
        });
      });
    })()
  `);
}

async function setProxyDelay(node, ipv6Ms, ipv4Ms) {
  await node.execute(
    `global.__proxy.ipv6Ms=${ipv6Ms}; global.__proxy.ipv4Ms=${ipv4Ms};`
  );
}

async function stopProxy(node) {
  await node.execute(`
    if (global.__proxy) {
      global.__proxy.p6.close(); global.__proxy.p4.close();
      global.__proxy = null;
    }
  `);
}

function fetchNoThrow(url) {
  const chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
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
  async function test_he_0rtt_alpn_switch_dead_winner_blocks_host() {
    // Warm-up (IPv4-only) sets mUsingSpdy=true on the CM entry and issues an
    // H2 session ticket.  After the idle timeout, the CM entry stays alive
    // (mUsingSpdy=true, mActiveConns empty).  The race fires 0-RTT with that
    // ticket; the server responds H1 → alpnChanged=1 → Finish0RTT fires.

    const host = "0rtt-alpn-switch.example.com";
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

      // One proxy, constant port, so warm-up and race share the same
      // ConnectionInfo key (and therefore the same CM entry / mUsingSpdy flag).
      const port = await startMutableProxy(node);
      const url = `https://${host}:${port}/`;

      // ── Warm-up (IPv4 only, 0 ms delay) ──────────────────────────────────
      // Server handles connection #0 as H2, issues a session ticket.
      // ReportSpdyConnection sets mUsingSpdy=true on the CM entry.
      Services.prefs.setBoolPref("network.dns.disableIPv6", true);
      const wu = fetchNoThrow(url);
      const wuResult = await wu.promise;
      Assert.ok(wuResult.ok, "warm-up must succeed");
      Services.prefs.setBoolPref("network.dns.disableIPv6", false);

      // Let the H2 idle timeout (2 s) fire so the warm-up H2 session is
      // reclaimed and mActiveConns becomes empty. The CM entry stays alive
      // (mUsingSpdy=true) — DO NOT call net:cancel-all-connections, which
      // would remove the entry from mCT and lose mUsingSpdy=true.
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 3000));

      // ── Race (IPv6 100 ms, IPv4 0 ms) ────────────────────────────────────
      // Server uses H1 for all subsequent connections (gAlpnSwitchCount >= 1).
      // Client's H2 ticket → 0-RTT; server responds H1 → alpnChanged=1 →
      // Finish0RTT(restart=1) fires.
      //
      // Without the fix: HE declares the dying connection the winner, inserting
      // it into mActiveConns.  npnPending=true (only Start0RTTSpdy ran) →
      // RestrictConnections() permanently blocks the re-queued transaction.
      //
      // Only one request: with multiple requests, other HCAs (no ticket, plain
      // H1) would win and remove the dead winner via MakeAllDontReuseExcept,
      // self-healing the deadlock before the timeout fires.
      await setProxyDelay(node, 100, 0);

      const TIMEOUT_MS = 10000;
      let timedOut = false;

      const f1 = fetchNoThrow(url);

      const result = await Promise.race([
        f1.promise,
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
          f1.chan.cancel(Cr.NS_ERROR_ABORT);
        } catch (_) {}
        await f1.promise;
      }

      Assert.ok(
        !timedOut,
        "Request must complete — without the fix, Finish0RTT(alpnChanged=1) " +
          "inserts the dead connection into mActiveConns and " +
          "RestrictConnections() permanently blocks new connections for this host."
      );
      if (!timedOut) {
        Assert.ok(result.ok, "request should return HTTP 200");
      }
    } finally {
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));
      await stopProxy(node);
      await node.stop();
      override.clearOverrides();
    }
  }
);

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_0rtt_h2_txn_gone_closes_connection() {
    const host = "0rtt-reject-h2.example.com";
    const TLS_PORT = 8443;
    const url = `https://${host}:${TLS_PORT}/`;

    let nss = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nss.asyncClearSSLExternalAndInternalSessionCache();
    let nssTest = nss.QueryInterface(Ci.nsISSLTokensCacheTest);

    override.clearOverrides();
    override.addIPOverride(host, "127.0.0.1");
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);

    try {
      // ── Warm-up: full H2 handshake, session ticket issued ──────────────
      const wu = fetchNoThrow(url);
      const wuResult = await wu.promise;
      Assert.ok(wuResult.ok, "H2 warm-up must succeed");

      // Wait for the ticket to land in SSLTokensCache instead of sleeping;
      // chaos mode can delay this independently of wall-clock time.
      await TestUtils.waitForCondition(
        () => nssTest.countSSLTokens() >= 1,
        "waiting for warm-up session ticket to be cached"
      );

      // Deterministically reclaim the warm-up connection so the next
      // request opens fresh and races 0-RTT with the cached ticket.
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));

      // ── Test: H2 0-RTT rejected, pref forces the null-realTxn path ────
      Services.prefs.setBoolPref(
        "network.http.0rtt_force_txn_gone_for_testing",
        true
      );

      const f = fetchNoThrow(url);
      const result = await f.promise;

      Assert.ok(!result.ok, "request should fail when null-txn path fires");
      Assert.equal(
        result.error,
        Cr.NS_ERROR_ABORT,
        "TCP connection should be closed cleanly with NS_ERROR_ABORT"
      );

      Services.prefs.clearUserPref(
        "network.http.0rtt_force_txn_gone_for_testing"
      );

      // ── Recovery: networking stack must still be functional ────────────
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));

      const r3 = fetchNoThrow(url);
      const r3Result = await r3.promise;
      Assert.ok(r3Result.ok, "recovery request should succeed");
      Assert.equal(r3Result.status, 200, "recovery request should return 200");
    } finally {
      Services.prefs.clearUserPref(
        "network.http.0rtt_force_txn_gone_for_testing"
      );
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));
      override.clearOverrides();
    }
  }
);

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_0rtt_h1_reject_txn_gone_no_assert() {
    // Regression test for bug 2047480: an H1 0-RTT request whose real
    // transaction is gone by the time 0-RTT finishes (early data rejected)
    // must fail cleanly.

    const host = "0rtt-reject-h1.example.com";
    const TLS_PORT = 8443;
    const url = `https://${host}:${TLS_PORT}/`;

    let nss = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nss.asyncClearSSLExternalAndInternalSessionCache();

    override.clearOverrides();
    override.addIPOverride(host, "127.0.0.1");
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);

    try {
      // Warm-up: full H1 handshake, PSK ticket written to SSLTokensCache.
      const wu = fetchNoThrow(url);
      const wuResult = await wu.promise;
      Assert.ok(wuResult.ok, "H1 warm-up must succeed");

      // Let NSS persist the ticket, then drop the connection so the next
      // request opens a fresh one and attempts 0-RTT.
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 500));
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));

      // Force the real transaction to be gone when 0-RTT finishes.
      Services.prefs.setBoolPref(
        "network.http.0rtt_force_txn_gone_for_testing",
        true
      );

      const TIMEOUT_MS = 10000;
      let timedOut = false;
      const f = fetchNoThrow(url);
      const result = await Promise.race([
        f.promise,
        new Promise(r =>
          // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
          setTimeout(() => {
            timedOut = true;
            r(null);
          }, TIMEOUT_MS)
        ),
      ]);

      Services.prefs.clearUserPref(
        "network.http.0rtt_force_txn_gone_for_testing"
      );

      Assert.ok(!timedOut, "request must complete (no hang / no crash)");
      if (timedOut) {
        try {
          f.chan.cancel(Cr.NS_ERROR_ABORT);
        } catch (_) {}
        await f.promise;
      } else {
        Assert.ok(!result.ok, "request should fail when the real txn is gone");
      }

      // Recovery: the networking stack must still work.
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));

      const r3 = fetchNoThrow(url);
      const r3Result = await r3.promise;
      Assert.ok(r3Result.ok, "recovery request should succeed");
      Assert.equal(r3Result.status, 200, "recovery request should return 200");
    } finally {
      Services.prefs.clearUserPref(
        "network.http.0rtt_force_txn_gone_for_testing"
      );
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(r => setTimeout(r, 200));
      override.clearOverrides();
    }
  }
);
