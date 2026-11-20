/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Bug 2052534: under Happy Eyeballs, an origin that advertises an unreachable
// Alt-Svc alternate used to be routed to that alternate and, when it could not
// be reached, the load failed without falling back to the origin. Happy
// Eyeballs now attempts the alt-svc host as an alt-svc entry and falls back to
// the origin when the alternate is unreachable.
//
// Each test first establishes a validated HTTP/2 origin -> HTTP/3 alternate
// alt-svc mapping (the alternate, served by its own HTTP/3 server on a distinct
// port, resolves and validates), then makes the alternate unreachable and
// reloads the origin, expecting a successful fall back to the origin. The two
// tests cover the two ways the alternate can be unreachable: it no longer
// resolves (DNS failure), and it resolves but its HTTP/3 connection fails
// (connection failure).

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const { HTTP3Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const mockController = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);

// The Alt-Svc alternate. Reachable in phase 1 via the override, unresolvable
// in phase 2. It lives on its own server (a different port from the origin) so
// the fallback in phase 2 is unambiguously the origin.
const ALT_HOST = "unreachable-alt.example.com";

let h2Port;
let httpsOrigin;
let altServer;
let altPort;
let altRoute; // ALT_HOST:altPort, i.e. the Alt-Used value once routed.

add_setup(async function () {
  do_get_profile();

  let h2Server = await create_h2_server({
    "/http3-test": (req, res) => {
      res.setHeader("Cache-Control", "no-cache");
      res.setHeader("Alt-Svc", "h3=" + req.headers["x-altsvc"]);
      res.writeHead(200, { "Content-Type": "text/html", "Content-Length": 2 });
      res.end("ok");
    },
  });
  h2Port = h2Server.port();

  Services.prefs.setBoolPref("network.http.http2.enabled", true);
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  // Lets a test fail traffic to the alternate's address. HTTP/3 must go through
  // NSPR I/O for the mock network layer (and thus failUDPAddrIO) to see it.
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  Services.prefs.setBoolPref("network.http.http3.use_nspr_for_io", true);

  // The alternate is served by its own HTTP/3 server on a distinct port. It
  // uses a cert valid for the origin (foo.example.com), so alt-svc validation
  // against it succeeds.
  altServer = new HTTP3Server();
  await altServer.start(
    Services.env.get("MOZ_HTTP3_SERVER_PATH"),
    Services.env.get("MOZ_HTTP3_CERT_DB_PATH")
  );
  altPort = altServer.port();

  httpsOrigin = `https://foo.example.com:${h2Port}/`;
  altRoute = `${ALT_HOST}:${altPort}`;

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.http2.enabled");
    Services.prefs.clearUserPref("network.http.altsvc.enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
    Services.prefs.clearUserPref("network.http.http3.use_nspr_for_io");
    Services.prefs.clearUserPref("network.http.http3.enable");
    override.clearOverrides();
    mockController.clearFailedUDPAddr();
    await altServer.stop();
  });
});

function openChan(path, { altSvc, flags } = {}) {
  let chan = NetUtil.newChannel({
    uri: httpsOrigin + path,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  if (altSvc) {
    chan.setRequestHeader("x-altsvc", altSvc, false);
  }

  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        req => {
          let altUsed = "";
          try {
            altUsed = req
              .QueryInterface(Ci.nsIHttpChannel)
              .getRequestHeader("Alt-Used");
          } catch (e) {}
          // A direct connection winning the Happy Eyeballs race drops the
          // alt-svc route by setting "Alt-Used: 0"; treat that as "not routed".
          if (altUsed == "0") {
            altUsed = "";
          }
          let httpVersion = "";
          try {
            httpVersion = req.protocolVersion;
          } catch (e) {}
          resolve({ status: req.status, altUsed, httpVersion });
        },
        null,
        flags
      )
    );
  });
}

function resetConnections() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  Services.dns.clearCache(true);
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  return new Promise(resolve => setTimeout(resolve, 500));
}

// Establishes a validated origin -> alternate alt-svc mapping in the cache.
// The alternate is made reachable (resolves to its server, no blocked connect)
// so alt-svc validation succeeds and the mapping is marked validated. The
// mapping is validated+applied once necko logs that it routes the origin to the
// alternate; poll until we see it. We must NOT drop connections while waiting:
// under Happy Eyeballs the h3 alternate is validated eagerly through an
// AltSvcTransaction (bug 2051272), and cancelling connections each round would
// abort that in-flight validation before it can mark the mapping validated.
async function ensureAltSvcMapping() {
  override.clearHostOverride(ALT_HOST);
  override.addIPOverride(ALT_HOST, "127.0.0.1");
  mockController.clearFailedUDPAddr();

  let sawMapping = false;
  let mappingObserver = {
    QueryInterface: ChromeUtils.generateQI(["nsIConsoleListener"]),
    observe(msg) {
      if (
        msg.message.includes("Alternate Service Mapping found") &&
        msg.message.includes(altRoute)
      ) {
        sawMapping = true;
      }
    },
  };
  Services.console.registerListener(mappingObserver);

  for (let i = 0; i < 10 && !sawMapping; i++) {
    let r = await openChan("http3-test", {
      altSvc: altRoute,
      flags: CL_ALLOW_UNKNOWN_CL,
    });
    info(`attempt ${i}: status=${r.status} Alt-Used="${r.altUsed}"`);
    if (!sawMapping) {
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      await new Promise(resolve => setTimeout(resolve, 500));
    }
  }

  Services.console.unregisterListener(mappingObserver);
  Assert.ok(
    sawMapping,
    "a validated origin -> alternate alt-svc mapping is established and applied"
  );
}

// Fallback when the alternate no longer resolves (DNS failure).
add_task(async function reload_falls_back_on_dns_failure() {
  await ensureAltSvcMapping();

  // "N/A" makes the alternate resolve to no addresses -> it is unreachable.
  override.clearHostOverride(ALT_HOST);
  override.addIPOverride(ALT_HOST, "N/A");
  await resetConnections();

  let { status, httpVersion } = await openChan("http3-test", {
    flags: CL_ALLOW_UNKNOWN_CL,
  });
  info(
    `reload status=0x${(status >>> 0).toString(16)} httpVersion=${httpVersion}`
  );
  Assert.equal(
    status,
    Cr.NS_OK,
    "reload falls back to the origin when the alt-svc alternate does not resolve"
  );
  Assert.equal(
    httpVersion,
    "h2",
    "the successful connection is the HTTP/2 origin"
  );
});

// Fallback when the alternate resolves but its connection fails.
add_task(async function reload_falls_back_on_connection_failure() {
  await ensureAltSvcMapping();

  // The alternate still resolves, but fail UDP I/O to it so the HTTP/3 (QUIC)
  // connection attempt fails at the connection (not DNS) stage.
  let blockedAddr = mockController.createScriptableNetAddr(
    "127.0.0.1",
    altPort
  );
  mockController.failUDPAddrIO(blockedAddr);
  await resetConnections();

  let { status, httpVersion } = await openChan("http3-test", {
    flags: CL_ALLOW_UNKNOWN_CL,
  });
  info(
    `reload status=0x${(status >>> 0).toString(16)} httpVersion=${httpVersion}`
  );
  Assert.equal(
    status,
    Cr.NS_OK,
    "reload falls back to the origin when the alt-svc HTTP/3 connection fails"
  );
  Assert.equal(
    httpVersion,
    "h2",
    "the successful connection is the HTTP/2 origin"
  );
});
