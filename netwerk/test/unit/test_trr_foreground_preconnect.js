/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

trr_test_setup();
registerCleanupFunction(async () => {
  trr_clear_prefs();
  Services.prefs.clearUserPref("network.http.debug-observations");
  Services.prefs.clearUserPref("network.trr.async_connInfo");
  Services.prefs.clearUserPref("network.dns.disableIPv6");
  Services.prefs.clearUserPref("network.trr.preconnect_allow_rfc1918");
  Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
});

const LOOKUP_HOST = "after.example.com";
const LOOKUP_IP = "1.2.3.5";

async function startTRRServer() {
  let server = new TRRServer();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.start();
  await server.registerDoHAnswers(LOOKUP_HOST, "A", {
    answers: [
      { name: LOOKUP_HOST, ttl: 55, type: "A", flush: false, data: LOOKUP_IP },
    ],
  });
  await server.execute(
    `global.gTLSPorts = []; global.gTLSConnected = new Promise(r => global.server.on("secureConnection", s => r(global.gTLSPorts.push(s.remotePort)))); null`
  );
  Services.prefs.setCharPref("network.trr.uri", trrURI(server));
  return server;
}

function trrURI(server) {
  return `https://foo.example.com:${server.port()}/dns-query?conncycle=true`;
}

// Completes after any proxy lookup for the TRR connection info started earlier.
function resolveProxy(uri) {
  let channel = NetUtil.newChannel({ uri, loadUsingSystemPrincipal: true });
  return new Promise(resolve => {
    Cc["@mozilla.org/network/protocol-proxy-service;1"]
      .getService(Ci.nsIProtocolProxyService)
      .asyncResolve(channel, 0, {
        QueryInterface: ChromeUtils.generateQI(["nsIProtocolProxyCallback"]),
        onProxyAvailable: resolve,
      });
  });
}

async function checkPreconnectIsReused(server, topic) {
  Services.dns.clearCache(true);
  let connectRequest = TestUtils.topicObserved(
    "speculative-connect-request",
    (_, hashKey) => hashKey.includes(`foo.example.com:${server.port()}`)
  );
  Services.obs.notifyObservers(null, topic);
  await connectRequest;
  Assert.equal(await server.execute("global.gTLSConnected"), 1);

  await new TRRDNSListener(LOOKUP_HOST, LOOKUP_IP);

  let tlsPorts = await server.execute("global.gTLSPorts");
  Assert.equal(tlsPorts.length, 1);
  let portLog = await server.execute("global.gDoHPortsLog");
  Assert.deepEqual(
    portLog.filter(([domain]) => domain == LOOKUP_HOST).map(([, p]) => p),
    tlsPorts
  );
}

async function speculativeConnectsDuring(fn) {
  let hashKeys = [];
  let observer = (_, __, hashKey) => hashKeys.push(hashKey);
  Services.obs.addObserver(observer, "speculative-connect-request");
  try {
    return [await fn(), hashKeys];
  } finally {
    Services.obs.removeObserver(observer, "speculative-connect-request");
  }
}

async function checkNoPreconnectWithOHTTP(topic) {
  Services.prefs.setBoolPref("network.trr.use_ohttp", true);
  let [, hashKeys] = await speculativeConnectsDuring(async () => {
    Services.obs.notifyObservers(null, topic);
    await resolveProxy(Services.prefs.getCharPref("network.trr.uri"));
  });
  Services.prefs.clearUserPref("network.trr.use_ohttp");
  Assert.deepEqual(hashKeys, []);
}

let trrServer;
add_task(async function setup() {
  trrServer = await startTRRServer();
  await trrServer.registerDoHAnswers("example.com", "A", {
    answers: [
      {
        name: "example.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "1.2.3.4",
      },
    ],
  });

  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setBoolPref("network.http.debug-observations", true);
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  Services.prefs.setBoolPref("network.trr.preconnect_allow_rfc1918", true);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 6);
});

add_task(async function test_preconnect_on_foreground_without_async_connInfo() {
  Services.prefs.setBoolPref("network.trr.async_connInfo", false);
  await checkPreconnectIsReused(trrServer, "application-foreground");
});

add_task(async function test_preconnect_on_foreground_with_async_connInfo() {
  Services.prefs.setBoolPref("network.trr.async_connInfo", true);
  // Create the cached connection info, so a URI change re-creates it.
  Services.dns.clearCache(true);
  await new TRRDNSListener("example.com", { expectedAnswer: "1.2.3.4" });
  await resolveProxy(trrURI(trrServer));

  let [server, hashKeys] = await speculativeConnectsDuring(async () => {
    let s = await startTRRServer();
    await resolveProxy(trrURI(s));
    return s;
  });
  Assert.deepEqual(hashKeys, []);
  await checkPreconnectIsReused(server, "application-foreground");
});

add_task(async function test_no_preconnect_on_foreground_with_ohttp() {
  await checkNoPreconnectWithOHTTP("application-foreground");
});

add_task(async function test_preconnect_on_initial_foreground() {
  Services.prefs.setBoolPref("network.trr.async_connInfo", false);
  let server = await startTRRServer();
  await checkPreconnectIsReused(server, "geckoview-initial-foreground");
});

add_task(async function test_no_preconnect_on_initial_foreground_with_ohttp() {
  await checkNoPreconnectWithOHTTP("geckoview-initial-foreground");
});

add_task(async function test_speculative_connect_on_foreground() {
  Services.prefs.setBoolPref("network.trr.async_connInfo", true);
  Services.prefs.setCharPref("network.trr.uri", trrURI(trrServer));
  Services.dns.clearCache(true);

  // Perform an initial DNS query to ensure the TRR connection info is
  // initialized before we fire application-foreground.
  await new TRRDNSListener("example.com", { expectedAnswer: "1.2.3.4" });

  let connectRequestPromise = TestUtils.topicObserved(
    "speculative-connect-request"
  );
  Services.obs.notifyObservers(null, "application-foreground");

  let [, hashKey] = await connectRequestPromise;
  let happyEyeballs = Services.prefs.getBoolPref(
    "network.http.happy_eyeballs_enabled"
  )
    ? "H"
    : ".";
  Assert.equal(
    hashKey,
    `.SA.......${happyEyeballs}[tlsflags0x00000000]foo.example.com:${trrServer.port()}[TRR:${Ci.nsIRequest.TRR_DISABLED_MODE}]`
  );
});

// 0.0.0.0 is a local IP literal that reaches the server (not on Windows or the
// Android emulator).
add_task(
  { skip_if: () => mozinfo.os == "win" || mozinfo.os == "android" },
  async function test_preconnect_to_local_ip_literal() {
    Services.prefs.setBoolPref("network.socket.ip_addr_any.disabled", false);
    let server = await startTRRServer();
    await server.execute(
      `global.gTCPConnected = new Promise(r => global.server.once("connection", () => r(1))); null`
    );
    Services.prefs.setCharPref(
      "network.trr.uri",
      `https://0.0.0.0:${server.port()}/dns-query`
    );
    Services.obs.notifyObservers(null, "application-foreground");
    Assert.equal(await server.execute("global.gTCPConnected"), 1);
    Services.prefs.clearUserPref("network.socket.ip_addr_any.disabled");
  }
);

// Without the pref, the Android bootstrap address 10.0.2.2 is refused.
add_task(
  { skip_if: () => mozinfo.os == "android" },
  async function test_preconnect_without_allow_rfc1918() {
    Services.prefs.clearUserPref("network.trr.preconnect_allow_rfc1918");
    Assert.equal(
      Services.prefs.getBoolPref("network.trr.preconnect_allow_rfc1918"),
      false
    );
    let server = await startTRRServer();
    await checkPreconnectIsReused(server, "application-foreground");
  }
);
