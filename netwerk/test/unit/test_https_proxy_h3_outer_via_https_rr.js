/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Companion to test_https_proxy_with_https_rr.js.
//
// Scenario: a plain HTTPS (CONNECT) proxy - created with newProxyInfo("https"),
// NOT a MASQUE proxy - whose host has an HTTPS RR advertising alpn=h3. Happy
// Eyeballs (both network.http.happy_eyeballs_enabled and
// network.http.happy_eyeballs_proxy_enabled on) should establish the *outer*
// Firefox<->proxy connection over HTTP/3 based on that HTTPS RR, then tunnel an
// h2 origin through it via plain HTTP/3 CONNECT.
//
// The proxy is the test H3 CONNECT proxy (create_masque_proxy_server), which is
// a QUIC/UDP-only server: there is no TCP listener on its port. So an h2/h1 TCP
// CONNECT attempt to it is refused and can never succeed - a successful tunnel
// therefore proves the outer connection was established over HTTP/3.

"use strict";

/* import-globals-from head_channels.js */
/* import-globals-from head_trr.js */
/* import-globals-from head_http3.js */

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();

// All SANs of the proxy/h2 test certs. The TRR server is reached at
// foo.example.com; the proxy host and origin host are kept distinct from it so
// the filter below can proxy ONLY the origin request and leave the TRR DoH
// requests (and the proxy connection itself) direct.
const proxyHost = "alt2.example.com";
const originHost = "alt1.example.com";

let h2OriginPort;
let proxyPort;
let trrServer;
let proxyFilter;

class ProxyFilter {
  constructor(type, host, port, flags) {
    this._type = type;
    this._host = host;
    this._port = port;
    this._flags = flags;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    // Only proxy the test origin. Leave everything else (notably the TRR
    // server's DoH requests, which resolve the proxy host's HTTPS RR) direct,
    // otherwise resolving the proxy would depend on a request proxied through
    // that same not-yet-resolved proxy.
    if (uri.host != originHost) {
      cb.onProxyFilterResult(pi);
      return;
    }
    cb.onProxyFilterResult(
      pps.newProxyInfo(
        this._type,
        this._host,
        this._port,
        "",
        "",
        this._flags,
        1000,
        null
      )
    );
  }
}

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener((req, buf) => resolve([req, buf]), null, flags)
    );
  });
}

function resetConnections() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  Services.obs.notifyObservers(null, "net:prune-all-connections");
  Services.dns.clearCache(true);
}

// Make an origin request tunneled through the proxy. The proxy port is
// QUIC/UDP-only, so success is only possible if the outer connection was
// established over HTTP/3 (a TCP CONNECT to that port would be refused).
async function checkOriginReachedThroughH3Proxy(path) {
  let chan = makeChan(`https://${originHost}:${h2OriginPort}/${path}`);
  let [req, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  Assert.equal(req.status, Cr.NS_OK, "channel succeeded");
  let proxied = req.QueryInterface(Ci.nsIProxiedChannel);
  Assert.equal(
    proxied.httpProxyConnectResponseCode,
    200,
    "proxy accepted the CONNECT tunnel over h3"
  );
  let http = req.QueryInterface(Ci.nsIHttpChannel);
  Assert.equal(http.responseStatus, 200, "origin returned 200");
  Assert.equal(http.protocolVersion, "h2", "origin was reached over HTTP/2");
  Assert.ok(buf.includes("You Win!"), "got the origin's response body");
}

add_setup(async function setup() {
  trr_test_setup();

  h2OriginPort = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2OriginPort, null);
  Assert.notEqual(h2OriginPort, "");

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_proxy_enabled", true);
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  Services.prefs.setBoolPref("network.dns.use_https_rr_as_altsvc", true);

  // The H3 CONNECT proxy (QUIC/UDP-only). We reach it via a plain "https"
  // proxy filter, so its h3 capability is only known from the HTTPS RR below.
  ({ masqueProxyPort: proxyPort } = await create_masque_proxy_server());
  Assert.notEqual(proxyPort, null);
  Assert.notEqual(proxyPort, "");

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers(proxyHost, "A", {
    answers: [
      {
        name: proxyHost,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  // Advertise h3 for the proxy host. The proxy is on a non-default port, so the
  // HTTPS RR is queried at the port-prefixed name (RFC 9460 _<port>._https.<host>).
  // No port SvcParam, so the h3 endpoint stays on that same (proxy) port, where
  // the QUIC proxy actually listens.
  let httpsRRName = `_${proxyPort}._https.${proxyHost}`;
  await trrServer.registerDoHAnswers(httpsRRName, "HTTPS", {
    answers: [
      {
        name: httpsRRName,
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: proxyHost,
          values: [{ key: "alpn", value: "h3" }],
        },
      },
    ],
  });

  proxyFilter = new ProxyFilter("https", proxyHost, proxyPort, 0);
  pps.registerFilter(proxyFilter, 10);

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_proxy_enabled");
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref("network.http.http2.enabled");
    Services.prefs.clearUserPref("network.dns.use_https_rr_as_altsvc");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    pps.unregisterFilter(proxyFilter);
    await trrServer.stop();
  });
});

add_task(async function test_h3_outer_proxy_via_https_rr() {
  // Confirm the proxy host's HTTPS RR advertises h3. HE queries the
  // port-prefixed name because the proxy is on a non-default port.
  let { inRecord } = await new TRRDNSListener(
    `_${proxyPort}._https.${proxyHost}`,
    { type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC }
  );
  let records = inRecord.QueryInterface(Ci.nsIDNSHTTPSSVCRecord).records;
  Assert.equal(records.length, 1, "one HTTPS RR for the proxy host");
  let alpnParam;
  for (let value of records[0].values) {
    try {
      alpnParam = value.QueryInterface(Ci.nsISVCParamAlpn);
      break;
    } catch (e) {}
  }
  Assert.ok(alpnParam, "HTTPS RR has an alpn SvcParam");
  Assert.ok(alpnParam.alpn.includes("h3"), "HTTPS RR advertises h3");

  await checkOriginReachedThroughH3Proxy("h3-outer-proxy");
});

// Same scenario with speculative connections off, so the outer connection is
// established by the real transaction rather than by the speculative one. Those
// take different caps: nsHttpChannel sets NS_HTTP_DISALLOW_HTTPS_RR on every
// proxied channel, and SpeculativeConnect() happens to filter it out, so only
// this case exercises the real transaction being allowed to look up the proxy's
// own HTTPS RR.
add_task(async function test_h3_outer_proxy_via_https_rr_no_speculative() {
  resetConnections();
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  await checkOriginReachedThroughH3Proxy("h3-outer-proxy-no-speculative");
});
