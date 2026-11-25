/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests the token telemetry of HTTP/3 connections: rejected tokens in
// network.ssl_token_resumption.

"use strict";

const kUrl = "https://foo.example.com/30";

registerCleanupFunction(async () => {
  http3_clear_prefs();
});

add_setup(async function () {
  await http3_setup_tests("h3");
  Services.fog.initializeFOG();
});

async function closeConnections() {
  // As in test_http3_0rtt.js: close the existing h3 connection first.
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  await sleep(1);
}

function openChannel(aFlags = CL_ALLOW_UNKNOWN_CL) {
  let chan = makeChan(kUrl);
  chan.loadFlags = Ci.nsIRequest.LOAD_BYPASS_CACHE;
  return channelOpenPromise(chan, aFlags);
}

async function openFreshH3Connection() {
  await closeConnections();
  // Without Happy Eyeballs, an earlier failed h3 connection excludes the origin.
  Services.obs.notifyObservers(null, "network:reset-http3-excluded-list");
  let [req] = await openChannel();
  Assert.equal(req.protocolVersion, "h3");
}

add_task(async function test_session_token_rejected() {
  await resetTokensAndTelemetry();
  // The record decodes, but neqo can't use its token.
  getCacheServices().testCache.putSSLTokenForTest("foo.example.com:443");

  await openFreshH3Connection();
  assertTokenResumption({ "h3_session/rejected": 1 });
});
