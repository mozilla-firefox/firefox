/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for the alpnChanged=false path in ZeroRttHandle::Finish0RTT.
//
// When H1 0-RTT early data is rejected by the server (NSS reports
// earlyDataAccepted=false) but the ALPN is unchanged (H1→H1), the H1
// connection is still fully usable.  ZeroRttHandle::Finish0RTT must NOT
// close the HET; it must fall through to InvokeCallback(NS_OK) so HE
// declares the winner and the real transaction is adopted onto the live
// connection.
//
// Observable: the second request has resumed=true — the TLS session was
// resumed via PSK even though early data was rejected.  If the fix were
// absent (alpnChanged check missing), the PSK token would be evicted and
// the retry would open a fresh connection with resumed=false.
//
// ZeroRttAcceptServer's "0rtt-reject-h1.example.com" host omits the NSS
// anti-replay context so early data is always rejected without closing the
// connection.

"use strict";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const kHost = "0rtt-reject-h1.example.com";
const kPort = 8443;

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

    Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setBoolPref("network.http.http3.enable", false);
    // Single address to keep the HE race to one HCA.
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);

    override.addIPOverride(kHost, "127.0.0.1");

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
      Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
      Services.prefs.clearUserPref("network.http.http3.enable");
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      override.clearOverrides();
      if (callbackServer) {
        await callbackServer.stop();
        callbackServer = null;
      }
    });
  }
);

function fetchResult(url) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: url,
      loadUsingSystemPrincipal: true,
    }).QueryInterface(Ci.nsIHttpChannel);
    chan.loadFlags =
      Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI | Ci.nsIRequest.LOAD_BYPASS_CACHE;
    chan.asyncOpen({
      onStartRequest() {},
      onDataAvailable(_req, stream, _offset, count) {
        read_stream(stream, count);
      },
      onStopRequest(req, status) {
        let resumed = false;
        try {
          resumed = req.securityInfo.resumed;
        } catch (_e) {}
        resolve({
          ok: Components.isSuccessCode(status),
          status: Components.isSuccessCode(status)
            ? req.QueryInterface(Ci.nsIHttpChannel).responseStatus
            : 0,
          resumed,
        });
      },
      QueryInterface: ChromeUtils.generateQI(["nsIStreamListener"]),
    });
  });
}

add_task(
  {
    skip_if: () =>
      AppConstants.MOZ_SYSTEM_NSS || !gServerStarted || mozinfo.os == "android",
  },
  async function test_he_h1_0rtt_early_data_rejected_reuses_connection() {
    const url = `https://${kHost}:${kPort}/`;

    let nss = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
      Ci.nsISSLTokensCache
    );
    await nss.asyncClearSSLExternalAndInternalSessionCache();

    // ── Warm-up: full H1 handshake, PSK ticket written to SSLTokensCache ──
    const wu = await fetchResult(url);
    Assert.ok(wu.ok, "warm-up must succeed");
    Assert.equal(wu.status, 200, "warm-up must return 200");
    Assert.equal(wu.resumed, false, "warm-up must be a fresh handshake");

    // Give NSS time to persist the session ticket, then clear connections
    // so the next request opens a new one (triggering 0-RTT).
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(r => setTimeout(r, 500));
    Services.obs.notifyObservers(null, "net:cancel-all-connections");
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(r => setTimeout(r, 200));

    // ── Test: H1 0-RTT, early data rejected (alpnChanged=false) ──────────
    // The server omits SSL_SetAntiReplayContext so NSS rejects early data,
    // but the ALPN stays H1.  ZeroRttHandle::Finish0RTT(restart=1,
    // alpnChanged=0) must fall through to InvokeCallback(NS_OK): the H1
    // connection is still alive and the real transaction is retried on it.
    //
    // resumed=true confirms the PSK ticket was used (TLS session resumed)
    // and the connection was not discarded.  Without the alpnChanged guard,
    // the HET would be closed, the token evicted, and the retry would open
    // a fresh connection with resumed=false.
    const r = await fetchResult(url);
    Assert.ok(r.ok, "request must succeed after 0-RTT early-data rejection");
    Assert.equal(r.status, 200, "request must return 200");
    Assert.equal(
      r.resumed,
      true,
      "PSK must have been used — connection reused after early-data rejection"
    );

    Services.obs.notifyObservers(null, "net:cancel-all-connections");
  }
);
