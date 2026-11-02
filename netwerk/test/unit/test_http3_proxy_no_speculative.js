/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from http3_proxy_common.js */

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

add_setup(async function () {
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  await setup_http3_proxy();
});

add_task(test_http_connect);
add_task(test_http_connect_auth_failure);
add_task(test_http_connect_large_data);
add_task(test_http_connect_connection_refused);
add_task(test_http_connect_invalid_host);
add_task(test_concurrent_http_connect_tunnels);
// TODO: Proxy needs to close the stream properly when socket failures occur
// add_task(test_http_connect_stream_closure);
add_task(test_connect_udp);
add_task(test_http_connect_fallback);

async function closeAllConnections() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1000));
}

add_task(async function test_http_connect_fallback() {
  for (const ServerClass of [
    NodeHTTPServer,
    NodeHTTPSServer,
    NodeHTTP2Server,
  ]) {
    info(`Running inner-connection fallback with ${ServerClass.name}`);
    try {
      await test_inner_connection_fallback(ServerClass);
      info(`${ServerClass.name} passed`);
    } finally {
      await closeAllConnections();
    }
  }
});
