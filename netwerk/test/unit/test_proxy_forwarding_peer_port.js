/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_channels.js */

// A non-CONNECT ("forwarding") request through a plain-HTTP proxy must open its
// socket to the *proxy*, not to the origin. Nothing else in the suite pins
// this: nsIHttpChannelInternal.isProxyUsed is derived from the channel's
// mProxyInfo rather than from the socket, so it reports true even if the
// connection bypassed the proxy, and on localhost a bypassed request still
// succeeds because the origin happily serves the proxy-style absolute-URI GET.
//
// remotePort comes from the connection's peer address, so it is the one value
// that actually distinguishes the two. This matters because Happy Eyeballs now
// handles forwarding proxy connections as well as CONNECT tunnels; it targets
// the proxy only because the connection info it is handed is already
// proxy-based (Origin()/OriginPort() name the proxy). Should that stop holding,
// this test fails instead of silently bypassing the proxy.

const { NodeHTTPServer, NodeHTTPProxyServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let proxy;
let server;

add_setup(async function setup() {
  // We don't normally allow localhost channels to be proxied.
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);

  server = new NodeHTTPServer();
  await server.start();
  await server.registerPathHandler("/test", (req, resp) => {
    resp.writeHead(200);
    resp.end("done");
  });

  proxy = new NodeHTTPProxyServer();
  await proxy.start();

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_proxy_enabled");
    await proxy.stop();
    await server.stop();
  });
});

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  // Never reuse a pooled connection: a forwarding connection is keyed by the
  // proxy, so without this the second run could ride the first run's socket and
  // assert nothing about how it was established.
  chan.loadFlags =
    Ci.nsIRequest.LOAD_FRESH_CONNECTION |
    Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener((req, buffer) => resolve([req, buffer]), null, flags)
    );
  });
}

async function checkForwardedThroughProxy(happyEyeballs) {
  Services.prefs.setBoolPref(
    "network.http.happy_eyeballs_enabled",
    happyEyeballs
  );
  Services.prefs.setBoolPref(
    "network.http.happy_eyeballs_proxy_enabled",
    happyEyeballs
  );

  let originPort = server.port();
  let proxyPort = proxy.port();
  Assert.notEqual(
    originPort,
    proxyPort,
    "sanity: origin and proxy are on different ports"
  );

  let chan = makeChan(`http://localhost:${originPort}/test`);
  let [req, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  Assert.equal(req.status, Cr.NS_OK, "channel succeeded");
  Assert.equal(
    req.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    200,
    "origin returned 200"
  );
  Assert.equal(buf, "done", "got the origin's response body");

  // The actual point of the test.
  let internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(
    internal.remotePort,
    proxyPort,
    "the socket was opened to the proxy"
  );
  Assert.notEqual(
    internal.remotePort,
    originPort,
    "the socket did not bypass the proxy and go straight to the origin"
  );
}

add_task(async function test_forwarding_peer_is_proxy_legacy() {
  await checkForwardedThroughProxy(false);
});

add_task(async function test_forwarding_peer_is_proxy_happy_eyeballs() {
  await checkForwardedThroughProxy(true);
});
