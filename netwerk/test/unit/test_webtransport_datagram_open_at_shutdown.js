/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_webtransport.js */

// A WebTransport session that sent a datagram and is still open at shutdown
// must not keep its Http3Session, and the NSS objects it holds, alive past
// NSS_Shutdown. The failure is a crash at shutdown in debug builds.

var host;

add_task(async function setup() {
  await http3_setup_tests("h3");
  host = "foo.example.com:" + Services.env.get("MOZHTTP3_PORT");
});

add_task(async function test_datagram_session_open_at_shutdown() {
  let webTransport = NetUtil.newWebTransport();
  let listener = new WebTransportListener().QueryInterface(
    Ci.WebTransportSessionEventListener
  );
  let pSize = new Promise(resolve => {
    listener.onMaxDatagramSize = resolve;
  });
  await new Promise(resolve => {
    listener.ready = resolve;
    webTransport.asyncConnect(
      NetUtil.newURI(`https://${host}/success`),
      true,
      [],
      Services.scriptSecurityManager.getSystemPrincipal(),
      Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      listener
    );
  });
  webTransport.getMaxDatagramSize();
  let rawData = new Uint8Array(await pSize);
  rawData.fill(42);
  webTransport.sendDatagram(rawData, 0, 0);
  Assert.ok(true, "Sent a datagram and left the session open");
});
