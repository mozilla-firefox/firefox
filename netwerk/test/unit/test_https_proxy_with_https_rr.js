/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Asserts that an origin request tunneled through an HTTPS (HTTP/2 CONNECT)
// proxy - NOT a MASQUE proxy - still reaches the h2 origin when the proxy
// host's HTTPS RR advertises h3 and Happy Eyeballs establishes the outer
// connection (both network.http.happy_eyeballs_enabled and
// network.http.happy_eyeballs_proxy_enabled on). Companion to
// test_https_proxy_h3_outer_via_https_rr.js, which covers the h3 leg winning.
//
// Nothing serves h3 for this proxy, so the h3 leg of the race always loses and
// the h2/h1 CONNECT attempt on the proxy port should win and reach the h2
// origin. The two tasks differ in how the h3 leg loses: the first lets it hang
// unanswered, the second has UDP to the proxy refuse outright.
//
// Note the h3 attempt goes to the proxy port, not to the port the HTTPS RR's
// port SvcParam names -- that SvcParam is not honored for an outer proxy
// connection.

"use strict";

/* import-globals-from head_channels.js */
/* import-globals-from head_trr.js */

const { NodeHTTP2ProxyServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();

const mockController = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);

// Both hosts are SANs of the proxy/h2 test certs.
const proxyHost = "alt2.example.com";
const originHost = "foo.example.com";

let h2OriginPort;
let h3NoResponsePort;
let proxyServer;
let proxyPort;
let trrServer;
let proxyFilter;
let httpsRRName;

class ProxyFilter {
  constructor(type, host, port, flags) {
    this._type = type;
    this._host = host;
    this._port = port;
    this._flags = flags;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    // Only proxy the test origin. The TRR server answers the proxy host's own
    // records, so proxying its DoH requests would make resolving the proxy
    // depend on a request through that same unresolved proxy. It shares the
    // origin's hostname on another port, hence the port check.
    if (uri.host != originHost || uri.port != Number(h2OriginPort)) {
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

// Make an origin request tunneled through the proxy and check it succeeded.
// The h3 endpoint the proxy's HTTPS RR advertises is unusable in both tasks, so
// reaching the origin means the h2/h1 CONNECT on the real proxy port won.
async function checkOriginReachedThroughH2Proxy(path) {
  let chan = makeChan(`https://${originHost}:${h2OriginPort}/${path}`);
  let [req, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  Assert.equal(req.status, Cr.NS_OK, "channel succeeded");
  let proxied = req.QueryInterface(Ci.nsIProxiedChannel);
  Assert.equal(
    proxied.httpProxyConnectResponseCode,
    200,
    "proxy accepted the CONNECT tunnel"
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

  h3NoResponsePort = Services.env.get("MOZHTTP3_PORT_NO_RESPONSE");
  Assert.notEqual(h3NoResponsePort, null);
  Assert.notEqual(h3NoResponsePort, "");

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
  // Let the second task fail UDP to the h3 endpoint. HTTP/3 must go through
  // NSPR I/O for the mock network layer (and thus failUDPAddrIO) to see it.
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  Services.prefs.setBoolPref("network.http.http3.use_nspr_for_io", true);

  proxyServer = new NodeHTTP2ProxyServer();
  await proxyServer.start();
  proxyPort = proxyServer.port();
  Assert.notEqual(proxyPort, 0);
  httpsRRName = `_${proxyPort}._https.${proxyHost}`;

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  // The proxy host resolves (A) to localhost for the h2/h1 TCP attempt...
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

  // ...and its HTTPS RR advertises h3 on a DIFFERENT port (a black-hole h3
  // endpoint that never responds). The proxy is on a non-default port, so the
  // record has to live at the port-prefixed name (RFC 9460
  // _<port>._https.<host>), which is what Happy Eyeballs queries.
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
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3NoResponsePort },
          ],
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
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
    Services.prefs.clearUserPref("network.http.http3.use_nspr_for_io");
    mockController.clearFailedUDPAddr();
    pps.unregisterFilter(proxyFilter);
    await proxyServer.stop();
    await trrServer.stop();
  });
});

add_task(async function test_h2_target_via_https_proxy_with_h3_https_rr() {
  // Confirm the proxy host's HTTPS RR is what we expect: h3 on a port that is
  // NOT the proxy port.
  let { inRecord } = await new TRRDNSListener(httpsRRName, {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });
  let records = inRecord.QueryInterface(Ci.nsIDNSHTTPSSVCRecord).records;
  Assert.equal(records.length, 1, "one HTTPS RR for the proxy host");
  let portParam;
  for (let value of records[0].values) {
    try {
      portParam = value.QueryInterface(Ci.nsISVCParamPort);
      break;
    } catch (e) {}
  }
  Assert.ok(portParam, "HTTPS RR has a port SvcParam");
  Assert.equal(
    portParam.port,
    Number(h3NoResponsePort),
    "HTTPS RR advertises the black-hole h3 port, not the proxy port"
  );
  Assert.notEqual(
    Number(h3NoResponsePort),
    Number(proxyPort),
    "sanity: h3 HTTPS-RR port differs from the proxy port"
  );

  // Now make an origin request tunneled through the proxy and see whether it
  // succeeds over h2.
  await checkOriginReachedThroughH2Proxy("https-proxy-rr");
});

// Same fallback, but with the h3 endpoint actively refusing rather than
// black-holing: sendto() fails with PR_CONNECT_REFUSED_ERROR on the first
// datagram. The task above leaves the h3 attempt hanging until something gives
// up on it, so only this one covers an outer h3 attempt that reports an error.
add_task(async function test_h2_fallback_when_h3_is_refused() {
  resetConnections();

  // The outer h3 attempt goes to the proxy port over UDP -- the port SvcParam
  // does not move it -- so that is what has to refuse. TCP to the same port is
  // untouched, which is what the h2 CONNECT falls back to. The proxy host has
  // only an A record, so 127.0.0.1 is the one address in play.
  let refusedAddr = mockController.createScriptableNetAddr(
    "127.0.0.1",
    Number(proxyPort)
  );
  mockController.failUDPAddrIO(refusedAddr);

  await checkOriginReachedThroughH2Proxy("https-proxy-rr-h3-refused");
});
