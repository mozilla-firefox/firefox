/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Bug 2089206: the outer connection to an HTTP/3 MASQUE proxy is raced with
// Happy Eyeballs. When the IPv6 attempt to the proxy fails with
// NS_ERROR_NET_RESET (as Windows reports a UDP port with no listener), the
// origin's validated alt-svc mapping must be kept, so that later requests are
// still routed to the HTTP/3 alternate service.

"use strict";

/* import-globals-from http3_common.js */

const { Http3ProxyFilter } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);
const mockController = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);
const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();

const H3_SERVER_DOMAIN = "foo.example.com";
const PROXY_HOST = "alt1.example.com";

let h3Route;
let h3AltSvc;
let httpsOrigin;
let proxyPort;
let proxyFilter;

add_setup(async function () {
  let h2Port = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2Port, null);
  Assert.notEqual(h2Port, "");
  let h3Port = await create_h3_server();
  h3AltSvc = ":" + h3Port;
  h3Route = `${H3_SERVER_DOMAIN}:${h3Port}`;
  httpsOrigin = `https://${H3_SERVER_DOMAIN}:${h2Port}/`;
  do_get_profile();

  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_proxy_enabled", true);
  // Without speculative connections, the outer proxy connection is made for
  // the request itself, as in Thunderbird.
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  Services.prefs.setBoolPref("network.dns.disableIPv6", false);
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setCharPref("network.dns.localDomains", H3_SERVER_DOMAIN);
  override.addIPOverride(PROXY_HOST, "127.0.0.1");

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  proxyPort = (await create_masque_proxy_server()).masqueProxyPort;
  Assert.notEqual(proxyPort, null);
  Assert.notEqual(proxyPort, "");
  proxyFilter = new Http3ProxyFilter(
    PROXY_HOST,
    proxyPort,
    0,
    "/.well-known/masque/udp/{target_host}/{target_port}/",
    ""
  );
  pps.registerFilter(proxyFilter, 10);

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_proxy_enabled");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
    Services.prefs.clearUserPref("network.dns.disableIPv6");
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref("network.dns.localDomains");
    pps.unregisterFilter(proxyFilter);
    override.clearOverrides();
    mockController.clearResetUDPAddr();
  });
});

function openChan(uri) {
  let chan = makeChan(uri);
  return new Promise(resolve => {
    chan.asyncOpen({
      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
      ]),
      onStartRequest() {},
      onDataAvailable(request, stream, offset, count) {
        read_stream(stream, count);
      },
      onStopRequest(request, status) {
        let routed = "";
        try {
          routed = request.getRequestHeader("Alt-Used");
        } catch (e) {}
        resolve({ status, routed });
      },
    });
  });
}

add_task(async function test_proxy_ipv6_reset_keeps_alt_svc_mapping() {
  await waitForHttp3Route(httpsOrigin + "http3-test", h3Route, h3AltSvc);

  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  await new Promise(resolve => do_timeout(1000, resolve));

  // The proxy now also resolves to IPv6, which Happy Eyeballs tries first.
  override.addIPOverride(PROXY_HOST, "::1");
  Services.dns.clearCache(true);
  mockController.resetUDPAddrIO(
    mockController.createScriptableNetAddr("::1", proxyPort)
  );

  let first = await openChan(httpsOrigin + "1");
  Assert.equal(first.status, Cr.NS_OK, "first request succeeds");
  Assert.equal(first.routed, h3Route, "first request uses the alt-svc route");

  let second = await openChan(httpsOrigin + "2");
  Assert.equal(second.status, Cr.NS_OK, "second request succeeds");
  Assert.equal(
    second.routed,
    h3Route,
    "the alt-svc mapping survives the failed IPv6 proxy attempt"
  );
});
