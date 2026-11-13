/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Bug 2084553: when the HTTPS RR of the origin doesn't offer a usable HTTP/3
// endpoint (here it only advertises http/1.1, in the bug HTTP/3 had been
// excluded for the origin), the WebTransport transaction adopts the record and
// is sent as a plain request over TCP. A response body must not reach
// WebTransportSessionProxy::OnDataAvailable; the session should just fail.

"use strict";

/* import-globals-from head_trr.js */
/* import-globals-from head_webtransport.js */

const { NodeHTTPSServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

// Not foo.example.com, which the TRR server uses and which is resolved through
// the bootstrap address instead.
const HOST = "alt1.example.com";

let trrServer;
let server;

add_setup(async function setup() {
  trr_test_setup();

  Services.prefs.setBoolPref("network.dns.use_https_rr_as_altsvc", true);
  Services.prefs.setBoolPref("network.dns.echconfig.enabled", false);
  Services.prefs.setBoolPref("network.dns.port_prefixed_qname_https_rr", false);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);

  server = new NodeHTTPSServer();
  await server.start();
  await server.registerPathHandler("/", (req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/html" });
    resp.end("<html><body>" + "a".repeat(64 * 1024) + "</body></html>");
  });

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers(HOST, "HTTPS", {
    answers: [
      {
        name: HOST,
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: HOST,
          values: [
            { key: "alpn", value: "http/1.1" },
            { key: "port", value: server.port() },
          ],
        },
      },
    ],
  });
  await trrServer.registerDoHAnswers(HOST, "A", {
    answers: [
      { name: HOST, ttl: 55, type: "A", flush: false, data: "127.0.0.1" },
    ],
  });

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.dns.use_https_rr_as_altsvc");
    Services.prefs.clearUserPref("network.dns.echconfig.enabled");
    Services.prefs.clearUserPref("network.dns.port_prefixed_qname_https_rr");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.trr.mode");
    Services.prefs.clearUserPref("network.trr.uri");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    await server.stop();
    await trrServer.stop();
  });
});

async function connectWebTransport() {
  Services.dns.clearCache(true);
  Services.obs.notifyObservers(null, "net:cancel-all-connections");

  let url = `https://${HOST}:${server.port()}/`;
  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );
  let closed = new Promise(resolve => {
    listener.closed = resolve;
  });
  listener.ready = () => {
    Assert.ok(false, "The session must not be established");
  };

  let webTransport = NetUtil.newWebTransport();
  webTransport.asyncConnect(
    NetUtil.newURI(url),
    true,
    [],
    // HTTPS RR is not used for requests from the system principal.
    Services.scriptSecurityManager.createContentPrincipal(
      NetUtil.newURI(url),
      {}
    ),
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    listener
  );

  await closed;
  Assert.ok(true, "The session failed without crashing");
}

add_task(async function test_without_happy_eyeballs() {
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", false);
  await connectWebTransport();
});

add_task(async function test_with_happy_eyeballs() {
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  await connectWebTransport();
});
