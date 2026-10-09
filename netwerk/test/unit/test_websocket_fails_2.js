/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* import-globals-from head_websocket.js */

const { NodeHTTPSProxyServer, NodeWebSocketServer } =
  ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

// We don't normally allow localhost channels to be proxied, but this
// is easier than updating all the certs and/or domains.
Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
});

add_setup(() => {
  Services.prefs.setBoolPref("network.http.http2.websockets", true);
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.http.http2.websockets");
});

// TLS handshake to the end server fails with proxy
async function test_tls_fail_on_ws_server_over_proxy() {
  let proxy = new NodeHTTPSProxyServer();
  await proxy.start();

  let wss = new NodeWebSocketServer();
  // no cert to ws server
  wss._skipCert = true;
  await wss.start();

  registerCleanupFunction(async () => {
    await wss.stop();
    await proxy.stop();
  });

  Assert.notEqual(wss.port(), null);
  await wss.registerMessageHandler((data, ws) => {
    ws.send(data);
  });

  let chan = makeWebSocketChan();
  let url = `wss://localhost:${wss.port()}`;
  const msg = "test tls fail on ws server over proxy";
  let [status] = await openWebSocketChannelPromise(chan, url, msg);

  // The origin's certificate is rejected inside the CONNECT tunnel.
  // WebSocketChannel recognises the status as a TLS failure and reports
  // NS_ERROR_NET_INADEQUATE_SECURITY so the close code becomes 1015.
  Assert.equal(status, 0x804b0052); // NS_ERROR_NET_INADEQUATE_SECURITY
}
add_task(test_tls_fail_on_ws_server_over_proxy);
