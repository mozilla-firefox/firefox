/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from trr_common.js */
/* import-globals-from head_trr.js */

const { HTTP3Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const mockNetwork = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);

let h2Port;
let h3ReverseProxyPort;

add_setup(async function setup() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  h2Port = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2Port, null);
  Assert.notEqual(h2Port, "");

  let h3ServerPath = Services.env.get("MOZ_HTTP3_SERVER_PATH");
  let h3DBPath = Services.env.get("MOZ_HTTP3_CERT_DB_PATH");

  let server = new HTTP3Server();
  await server.start(h3ServerPath, h3DBPath);

  h3ReverseProxyPort = server.reverse_proxy_port();

  Services.prefs.setBoolPref("network.dns.preferIPv6", true);
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, alt1.example.com, alt2.example.com"
  );

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.trr.force_http3_first");
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
    Services.prefs.clearUserPref("network.http.http3.use_nspr_for_io");
    Services.prefs.clearUserPref("network.trr.http3_stall_detection.enabled");
    mockNetwork.clearBlockedUDPAddr();
  });
});

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
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

add_task(async function test_doh_http3_first() {
  Services.dns.clearCache(true);
  let host = "foo.example.com";

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `${host};h3=:${h3ReverseProxyPort}`
  );

  // Our HTTP/3 reverse proxy only supports forwarding plain HTTP traffic,
  // so the backend TRR server must also run over plain HTTP.
  let trrServer = new PlainHttpTRRServer();
  await trrServer.start();

  await trrServer.registerDoHAnswers("example.org", "A", {
    answers: [
      {
        name: "example.org",
        ttl: 55,
        type: "A",
        flush: false,
        data: "2.2.2.2",
      },
    ],
  });

  // Tell the Http/3 server which port to forward requests.
  let chan = makeChan(
    `https://${host}:${h3ReverseProxyPort}/port?${trrServer.port()}`
  );
  await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    ""
  );

  Services.prefs.setBoolPref("network.trr.force_http3_first", true);

  Services.prefs.setCharPref("network.trr.confirmationNS", "skip");
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://${host}:${h3ReverseProxyPort}/dns-query`
  );

  await new TRRDNSListener("example.org", "2.2.2.2");
  await trrServer.stop();
});

function timeout(ms) {
  return new Promise((_, reject) => {
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(() => reject(new Error(`timeout after ${ms}ms`)), ms);
  });
}

// Test when HTTP/3 TRR server is blocked, we fallback to HTTP/2.
add_task(async function test_doh_http3_first_fallback_http2() {
  Services.dns.clearCache(true);
  let host = "alt1.example.com";

  let trrServer = new TRRServer();
  await trrServer.startWithNoResponseUdpPort();

  await trrServer.registerDoHAnswers("example.net", "A", {
    answers: [
      {
        name: "example.net",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
  });

  Services.prefs.setBoolPref("network.trr.force_http3_first", true);

  Services.prefs.setCharPref("network.trr.confirmationNS", "skip");
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://${host}:${trrServer.port()}/dns-query`
  );

  let dnsRequest = new TRRDNSListener("example.net", "1.2.3.4");
  try {
    await Promise.race([dnsRequest, timeout(1500)]);
  } catch (err) {
    Assert.ok(false, "dnsRequest takes too long");
  } finally {
    await trrServer.stop();
  }
});

// An established HTTP/3 TRR connection that silently stops delivering packets
// must not stall lookups until the HTTP/3 idle timeout or the TRR-only request
// timeout fires; after network.trr.ping_timeout of silence they should move to
// HTTP/2.
add_task(async function test_doh_http3_blackholed_after_connect() {
  Services.prefs.setBoolPref("network.trr.http3_stall_detection.enabled", true);
  Services.dns.clearCache(true);
  let host = "alt2.example.com";

  // The reverse proxy only accepts one /port? request per process, and the
  // shared one is already bound to the first task's backend.
  let h3Server = new HTTP3Server();
  await h3Server.start(
    Services.env.get("MOZ_HTTP3_SERVER_PATH"),
    Services.env.get("MOZ_HTTP3_CERT_DB_PATH")
  );
  let proxyPort = h3Server.reverse_proxy_port();

  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  Services.prefs.setBoolPref("network.http.http3.use_nspr_for_io", true);

  // The HTTP/3 path answers 2.2.2.2 and the HTTP/2 path answers 3.3.3.3, so
  // each answer tells us which protocol carried the query.
  const names = ["warmup.example.org", "a.example.org", "b.example.org"];
  let h3Backend = new PlainHttpTRRServer();
  await h3Backend.start();
  for (let name of names) {
    await h3Backend.registerDoHAnswers(name, "A", {
      answers: [{ name, ttl: 55, type: "A", flush: false, data: "2.2.2.2" }],
    });
  }

  // Point the reverse proxy at the new backend before anything listens on the
  // same TCP port, so this request can only be served over HTTP/3.
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `${host};h3=:${proxyPort}`
  );
  let chan = makeChan(`https://${host}:${proxyPort}/port?${h3Backend.port()}`);
  let [req] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.QueryInterface(Ci.nsIHttpChannel).protocolVersion, "h3");
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    ""
  );

  let h2Server = new TRRServer();
  await h2Server.start(proxyPort);
  for (let name of names) {
    await h2Server.registerDoHAnswers(name, "A", {
      answers: [{ name, ttl: 55, type: "A", flush: false, data: "3.3.3.3" }],
    });
  }

  Services.prefs.setBoolPref("network.trr.force_http3_first", true);
  Services.prefs.setCharPref("network.trr.confirmationNS", "skip");
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://${host}:${proxyPort}/dns-query`
  );

  await new TRRDNSListener("warmup.example.org", "2.2.2.2");

  for (let ip of ["127.0.0.1", "::1"]) {
    mockNetwork.blockUDPAddrIO(
      mockNetwork.createScriptableNetAddr(ip, proxyPort)
    );
  }

  let start = Date.now();
  try {
    await Promise.race([
      Promise.all([
        new TRRDNSListener("a.example.org", "3.3.3.3"),
        new TRRDNSListener("b.example.org", "3.3.3.3"),
      ]),
      timeout(10000),
    ]);
    info(`Lookups recovered after ${Date.now() - start}ms`);
  } catch (err) {
    Assert.ok(false, `Lookups stalled on the dead HTTP/3 connection: ${err}`);
  } finally {
    mockNetwork.clearBlockedUDPAddr();
    await h2Server.stop();
    await h3Backend.stop();
    await h3Server.stop();
  }
});
