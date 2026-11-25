/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests the token telemetry of HTTP/3 connections: rejected and failed tokens in
// network.ssl_token_resumption, and network.ssl_token_cache_wrapped_not_persisted
// for the tokens HTTP/3 stores, whose secrets are never exported in plaintext.

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const kUrl = "https://foo.example.com/30";

registerCleanupFunction(async () => {
  http3_clear_prefs();
  Services.prefs.clearUserPref("network.ssl_tokens_cache_persistence");
  Services.prefs.clearUserPref("network.http.http3.0rtt_timeout");
  Services.prefs.clearUserPref("network.http.http3.idle_timeout");
  Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
  Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
  Services.prefs.clearUserPref("network.http.http3.use_nspr_for_io");
  mockController.clearFailedUDPAddr();
  mockController.clearBlockedUDPAddr();
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

const mockController = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);

// Caches a token, then returns the h3 server address for the mock network
// layer, which sees HTTP/3 traffic only with NSPR I/O.
async function setupMockedResumption() {
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  Services.prefs.setBoolPref("network.http.http3.use_nspr_for_io", true);
  await resetTokensAndTelemetry();
  await openFreshH3Connection();
  assertTokenResumption({});
  await closeConnections();
  return mockController.createScriptableNetAddr(
    "127.0.0.1",
    Number(Services.env.get("MOZHTTP3_PORT"))
  );
}

add_task(async function test_session_token_rejected() {
  await resetTokensAndTelemetry();
  // The record decodes, but neqo can't use its token.
  getCacheServices().testCache.putSSLTokenForTest("foo.example.com:443");

  await openFreshH3Connection();
  assertTokenResumption({ "h3_session/rejected": 1 });
});

add_task(async function test_session_token_failed_on_socket_error() {
  let addr = await setupMockedResumption();
  mockController.failUDPAddrIO(addr);
  await openChannel(CL_ALLOW_UNKNOWN_CL | CL_EXPECT_FAILURE);
  mockController.clearFailedUDPAddr();
  assertTokenResumption({ "h3_session/failed": 1 });
});

add_task(async function test_session_token_failed_on_idle_timeout() {
  let addr = await setupMockedResumption();
  // Leave the close to neqo's idle timeout, not necko's 0-RTT timeout.
  Services.prefs.setIntPref("network.http.http3.0rtt_timeout", 0);
  Services.prefs.setIntPref("network.http.http3.idle_timeout", 1);
  mockController.blockUDPAddrIO(addr);
  await openChannel(CL_ALLOW_UNKNOWN_CL | CL_EXPECT_FAILURE);
  mockController.clearBlockedUDPAddr();
  Services.prefs.clearUserPref("network.http.http3.0rtt_timeout");
  Services.prefs.clearUserPref("network.http.http3.idle_timeout");
  assertTokenResumption({ "h3_session/failed": 1 });
});

// Without Happy Eyeballs, the restart drops the h3 route and TCP is refused.
add_task(
  { pref_set: [["network.http.happy_eyeballs_enabled", true]] },
  async function test_session_token_failed_on_zero_rtt_timeout() {
    let addr = await setupMockedResumption();
    // A speculative connection has no real transaction, so it can't send 0-RTT.
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setIntPref("network.http.http3.0rtt_timeout", 100);
    mockController.blockUDPAddrIO(addr);
    let opened = openChannel();
    await TestUtils.waitForCondition(
      () =>
        Glean.netwerk.http30rttState.conn_closed_by_necko.testGetValue() == 1,
      "necko closed the 0-RTT connection"
    );
    mockController.clearBlockedUDPAddr();
    Services.prefs.clearUserPref("network.http.http3.0rtt_timeout");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    // The restarted transaction connects without a token.
    let [req] = await opened;
    Assert.equal(req.protocolVersion, "h3");
    assertTokenResumption({ "h3_session/failed": 1 });
  }
);

add_task(async function test_wrapped_token_counted_only_with_persistence() {
  for (let [persist, expected] of [
    [false, null],
    [true, 1],
  ]) {
    await resetTokensAndTelemetry();
    Services.prefs.setBoolPref("network.ssl_tokens_cache_persistence", persist);
    await openFreshH3Connection();
    Assert.equal(
      Glean.network.sslTokenCacheWrappedNotPersisted.testGetValue(),
      expected
    );
  }
});
