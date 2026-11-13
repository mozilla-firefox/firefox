/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* import-globals-from head_websocket.js */

const {
  NodeWebSocketServer,
  NodeHTTPSProxyServer,
  WebSocketConnection,
  NodeProxyFilter,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

// We don't normally allow localhost channels to be proxied, but this
// is easier than updating all the certs and/or domains.
Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
});

var CC = Components.Constructor;
const ServerSocket = CC(
  "@mozilla.org/network/server-socket;1",
  "nsIServerSocket",
  "init"
);

let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
  Ci.nsIX509CertDB
);

add_setup(() => {
  Services.prefs.setBoolPref("network.http.http2.websockets", true);
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.http.http2.websockets");
});

// TLS handshake to the end server fails - no proxy
async function test_tls_fail_on_direct_ws_server_handshake() {
  // no cert and no proxy
  let wss = new NodeWebSocketServer();
  wss._skipCert = true;
  await wss.start();
  registerCleanupFunction(async () => {
    await wss.stop();
  });

  Assert.notEqual(wss.port(), null);

  let chan = makeWebSocketChan();
  let url = `wss://localhost:${wss.port()}`;
  const msg = "test tls handshake with direct ws server fails";
  let [status] = await openWebSocketChannelPromise(chan, url, msg);

  // can be two errors, seems to be a race between:
  // * overwriting the WebSocketChannel status with NS_ERROR_NET_RESET and
  // * getting the original 805A1FF3 // SEC_ERROR_UNKNOWN_ISSUER
  if (status == 2152398930) {
    Assert.equal(status, 0x804b0052); // NS_ERROR_NET_INADEQUATE_SECURITY
  } else {
    // occasionally this happens
    Assert.equal(status, 0x804b0057); // NS_ERROR_WEBSOCKET_CONNECTION_REFUSED
  }
}

// TLS handshake to proxy fails
async function test_tls_fail_on_proxy_handshake() {
  // we have ws cert, but no proxy cert
  let proxy = new NodeHTTPSProxyServer();
  proxy._skipCert = true;
  await proxy.start();

  let wss = new NodeWebSocketServer();
  await wss.start();

  registerCleanupFunction(async () => {
    await wss.stop();
    await proxy.stop();
  });

  Assert.notEqual(wss.port(), null);

  let chan = makeWebSocketChan();
  let url = `wss://localhost:${wss.port()}`;
  const msg = "test tls failure on proxy handshake";
  let [status] = await openWebSocketChannelPromise(chan, url, msg);

  // see above test for details on why 2 cases here
  if (status == 2152398930) {
    Assert.equal(status, 0x804b0052); // NS_ERROR_NET_INADEQUATE_SECURITY
  } else {
    Assert.equal(status, 0x804b0057); // NS_ERROR_WEBSOCKET_CONNECTION_REFUSED
  }

  await proxy.stop();
}

// the ws server does not respond (closed port)
async function test_non_responsive_ws_server_closed_port() {
  // ws server cert already added in previous test

  // no ws server listening (closed port)
  let randomPort = 666; // "random" port
  let chan = makeWebSocketChan();
  let url = `wss://localhost:${randomPort}`;
  const msg = "test non-responsive ws server closed port";
  let [status] = await openWebSocketChannelPromise(chan, url, msg);
  Assert.equal(status, 0x804b0057); // NS_ERROR_WEBSOCKET_CONNECTION_REFUSED
}

// no ws response from server (ie. no ws server, use tcp server to open port)
async function test_non_responsive_ws_server_open_port() {
  // we are expecting the timeout in this test, so lets shorten to 1s
  Services.prefs.setIntPref("network.websocket.timeout.open", 1);

  // ws server cert already added in previous test

  // use a tcp server to test open port, not a ws server
  var server = ServerSocket(-1, true, -1); // port, loopback, default-backlog
  var port = server.port;
  info("server: listening on " + server.port);
  server.asyncListen({});

  // queue cleanup after all tests
  registerCleanupFunction(() => {
    server.close();
    Services.prefs.clearUserPref("network.websocket.timeout.open");
  });

  // try ws connection
  let chan = makeWebSocketChan();
  let url = `wss://localhost:${port}`;
  const msg = "test non-responsive ws server open port";
  let [status] = await openWebSocketChannelPromise(chan, url, msg);
  Assert.equal(status, Cr.NS_ERROR_NET_TIMEOUT_EXTERNAL); // we will timeout
  Services.prefs.clearUserPref("network.websocket.timeout.open");
}

// proxy does not respond
async function test_proxy_doesnt_respond() {
  Services.prefs.setBoolPref("network.http.http2.websockets", false);
  // ws cert added in previous test, add proxy cert
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  // Bind and immediately close a socket to get a port that is not listening.
  let closedSocket = ServerSocket(-1, true, -1);
  let closedPort = closedSocket.port;
  closedSocket.close();

  // route traffic through non-existant proxy
  const pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
  let filter = new NodeProxyFilter("https", "localhost", closedPort, 0);
  pps.registerFilter(filter, 10);
  registerCleanupFunction(() => {
    pps.unregisterFilter(filter);
  });

  // setup the websocket server
  info("spinning up websocket server");
  let wss = new NodeWebSocketServer();
  await wss.start();
  registerCleanupFunction(() => {
    wss.stop();
  });
  Assert.notEqual(wss.port(), null);
  await wss.registerMessageHandler((data, ws) => {
    ws.send(data);
  });

  info("creating and connecting websocket");
  let url = `wss://localhost:${wss.port()}`;
  let conn = new WebSocketConnection();
  conn.open(url); // do not await, we don't expect a fully opened channel

  // check proxy info
  info("checking proxy info");
  let proxyInfoPromise = conn.getProxyInfo();
  let proxyInfo = await proxyInfoPromise;
  Assert.equal(proxyInfo.type, "https"); // let's be sure that failure is not "direct"

  // we fail to connect via proxy, as expected
  let { status } = await conn.finished();
  info("stats: " + status);
  Assert.equal(status, 0x804b0057); // NS_ERROR_WEBSOCKET_CONNECTION_REFUSED
}

add_task(test_tls_fail_on_direct_ws_server_handshake);
add_task(test_tls_fail_on_proxy_handshake);
add_task(test_non_responsive_ws_server_closed_port);
add_task(test_non_responsive_ws_server_open_port);
add_task(test_proxy_doesnt_respond);
