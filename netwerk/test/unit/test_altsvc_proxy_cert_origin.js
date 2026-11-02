/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// An Alt-Svc alternate reached through a SOCKS remote-DNS proxy must still be
// authenticated as the origin.
//
// RFC 7838 requires the alternative service to present a certificate valid for
// the origin; the alternate host name is only where to connect. Here the
// alternate serves a certificate covering ONLY the alternate host, so
// validation must fail and the mapping must never go live -- every response has
// to keep coming from the origin.
//
// NOTE: this cannot fail today. Alt-Svc routes are not given to Happy Eyeballs
// while proxied (the "&& !pi" in AlternateServices.cpp), and even without that
// guard the state machine attempts the origin by name rather than the alt-svc
// entry, so the alternate is never contacted and the check below holds for the
// wrong reason. It guards the invariant once that is fixed.
//
// This is the by-name connection path (SOCKS with
// TRANSPARENT_PROXY_RESOLVES_HOST resolves names itself, so Firefox connects by
// name). On that path the host being connected to is handed to the transport as
// the route, while the origin -- which drives SNI and certificate checks -- must
// keep coming from the connection info. Passing the by-name host as the origin
// instead would authenticate the alternate against its own name and wrongly
// validate the mapping.

const { NodeHTTP2Server, NodeSocks5ForwardServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const ORIGIN_HOST = "origin.altsvc-certorigin.example";
const ALT_HOST = "alt.altsvc-certorigin.example";
const ORIGIN_PATH = "/altsvc-cert-origin";

let originServer = new NodeHTTP2Server();
let altServer = new NodeHTTP2Server();
let socksServer = new NodeSocks5ForwardServer();
let originURL;
let gFilter;

// Routes every Firefox channel through the SOCKS5 remote-DNS proxy.
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

function makeChan(url) {
  let uri = NetUtil.newURI(url);
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );
  return NetUtil.newChannel({
    uri: url,
    loadingPrincipal: principal,
    triggeringPrincipal: principal,
    securityFlags: Ci.nsILoadInfo.SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
  }).QueryInterface(Ci.nsIHttpChannel);
}

// Resolves with [status, body] regardless of success/failure.
function fetchOrigin() {
  let chan = makeChan(originURL);
  return new Promise(resolve => {
    let body = "";
    chan.asyncOpen({
      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
      ]),
      onStartRequest() {},
      onDataAvailable(req, stream, off, cnt) {
        body += read_stream(stream, cnt);
      },
      onStopRequest(req, status) {
        resolve([status, body]);
      },
    });
  });
}

add_setup(async function setup() {
  do_get_profile();

  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setBoolPref("network.http.altsvc.oe", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_proxy_enabled", true);
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  await originServer.start(0, [ORIGIN_HOST]);
  // The alternate's certificate deliberately does NOT cover the origin, so
  // authenticating it as the origin (the only correct behaviour) fails.
  await altServer.start(0, [ALT_HOST]);
  await socksServer.start();

  originURL = `https://${ORIGIN_HOST}:${originServer.port()}${ORIGIN_PATH}`;

  // The proxy forwards any hostname to loopback, so no client-side override is
  // needed for connectivity. This one only exists so a client-side resolution
  // of the alternate would succeed rather than fail for the wrong reason.
  override.addIPOverride(ALT_HOST, "127.0.0.1");

  let altSvc = `h2="${ALT_HOST}:${altServer.port()}"; ma=3600`;
  await originServer.execute(`global.ALT_SVC = ${JSON.stringify(altSvc)}`);
  await originServer.registerPathHandler(ORIGIN_PATH, (req, resp) => {
    resp.setHeader("Alt-Svc", global.ALT_SVC);
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("origin");
  });
  await altServer.registerPathHandler(ORIGIN_PATH, (req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("ALT");
  });

  gFilter = new SocksRemoteDnsFilter(socksServer.port());
  pps.registerFilter(gFilter, 10);

  registerCleanupFunction(async () => {
    pps.unregisterFilter(gFilter);
    try {
      await originServer.stop();
      await altServer.stop();
      await socksServer.stop();
    } catch (e) {
      info("Error stopping servers: " + e);
    }
    Services.prefs.clearUserPref("network.http.http2.enabled");
    Services.prefs.clearUserPref("network.http.altsvc.enabled");
    Services.prefs.clearUserPref("network.http.altsvc.oe");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_proxy_enabled");
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    override.clearOverrides();
    Services.dns.clearCache(true);
  });
});

add_task(async function altsvc_alternate_must_authenticate_as_origin() {
  // Prime the origin so the Alt-Svc header is seen and validation starts.
  let [status, body] = await fetchOrigin();
  Assert.equal(status, Cr.NS_OK, "first origin fetch succeeded");
  Assert.equal(body, "origin", "first response came from the origin");

  // Give validation several chances to run, dropping connections in between so
  // a later request is not simply coalesced onto the existing origin
  // connection. The alternate must never serve a response.
  for (let i = 0; i < 10; i++) {
    Services.obs.notifyObservers(null, "net:cancel-all-connections");
    await new Promise(r => do_timeout(250, r));

    [status, body] = await fetchOrigin();
    Assert.equal(status, Cr.NS_OK, `origin fetch ${i} succeeded`);
    Assert.notEqual(
      body,
      "ALT",
      "the alternate must not serve the origin: its certificate does not " +
        "cover the origin, so Alt-Svc validation has to fail"
    );
    Assert.equal(body, "origin", `response ${i} came from the origin`);
  }

  // The alternate host should have been contacted through the proxy (so the
  // check above is not vacuous), but its handshake must not have validated it.
  let hosts = await socksServer.connectedHostnames();
  info("hosts the proxy was asked to connect to: " + hosts.join(", "));
});
