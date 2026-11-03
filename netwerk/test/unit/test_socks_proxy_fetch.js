/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// An HTTP fetch through a real SOCKS5 proxy that resolves hostnames itself
// (TRANSPARENT_PROXY_RESOLVES_HOST, i.e. network.proxy.socks_remote_dns).
// Happy Eyeballs connects with ResolutionMode::ByName: no client-side DNS, the
// host is handed to the SOCKS layer. test_socks.js covers nsSOCKSIOLayer with
// raw transports; this covers the channel path end to end.

const { NodeHTTPServer, NodeSocks5ForwardServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();

const ORIGIN_HOST = "origin.socks-fetch.example";
const PATH = "/socks-fetch";
const BODY = "through the proxy";

let originServer = new NodeHTTPServer();
let socksServer = new NodeSocks5ForwardServer();
let originURL;
let gFilter;

// Routes every channel through the SOCKS5 proxy, with remote DNS.
class SocksRemoteDnsFilter {
  constructor(port) {
    this._port = port;
    this.QueryInterface = ChromeUtils.generateQI(["nsIProtocolProxyFilter"]);
  }
  applyFilter(uri, pi, cb) {
    cb.onProxyFilterResult(
      pps.newProxyInfo(
        "socks",
        "127.0.0.1",
        this._port,
        "",
        "",
        Ci.nsIProxyInfo.TRANSPARENT_PROXY_RESOLVES_HOST,
        1000,
        null
      )
    );
  }
}

add_setup(async function setup() {
  do_get_profile();
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_proxy_enabled", true);

  await originServer.start();
  await socksServer.start();

  originURL = `http://${ORIGIN_HOST}:${originServer.port()}${PATH}`;
  await originServer.execute(`global.BODY = ${JSON.stringify(BODY)}`);
  await originServer.registerPathHandler(PATH, (req, resp) => {
    resp.writeHead(200, {
      "Content-Type": "text/plain",
      "Content-Length": String(global.BODY.length),
    });
    resp.end(global.BODY);
  });

  // No addIPOverride for ORIGIN_HOST on purpose: the proxy forwards to
  // loopback, so a client-side resolve fails the fetch rather than leaking.
  gFilter = new SocksRemoteDnsFilter(socksServer.port());
  pps.registerFilter(gFilter, 10);

  registerCleanupFunction(async () => {
    pps.unregisterFilter(gFilter);
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_proxy_enabled");
    await socksServer.stop();
    await originServer.stop();
  });
});

function fetchThroughProxy() {
  let chan = NetUtil.newChannel({
    uri: originURL,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  // Force a real connection rather than a pooled one.
  chan.loadFlags =
    Ci.nsIRequest.LOAD_FRESH_CONNECTION |
    Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (req, buffer) => resolve([req, buffer]),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

add_task(async function socks_fetch_happy_eyeballs() {
  let [req, buffer] = await fetchThroughProxy();

  Assert.equal(req.status, Cr.NS_OK, "fetch through the SOCKS proxy succeeded");
  Assert.equal(
    req.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    200,
    "origin returned 200"
  );
  Assert.equal(buffer, BODY, "got the origin's body back through the proxy");
  Assert.ok(
    req.QueryInterface(Ci.nsIHttpChannelInternal).isProxyUsed,
    "the channel used a proxy"
  );
});
