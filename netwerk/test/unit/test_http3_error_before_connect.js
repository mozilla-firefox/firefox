/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

let httpsUri;

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.http.http3.enable");
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.dns.disableIPv6");
  Services.prefs.clearUserPref(
    "network.http.http3.alt-svc-mapping-for-testing"
  );
  Services.prefs.clearUserPref("network.http.http3.backup_timer_delay");
  dump("cleanup done\n");
});

function makeChan() {
  let chan = NetUtil.newChannel({
    uri: httpsUri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

add_task(async function test_setup() {
  let h2Port = (await create_h2_server()).port();
  let h3Port = Services.env.get("MOZHTTP3_PORT_NO_RESPONSE");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  // Set AltSvc to point to not existing HTTP3 server on port 443
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    "foo.example.com;h3=:" + h3Port
  );
  Services.prefs.setIntPref("network.http.http3.backup_timer_delay", 0);

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  httpsUri = "https://foo.example.com:" + h2Port + "/";
});

add_task(async function test_fatal_stream_error() {
  let result = 1;
  // We need to loop here because we need to wait for AltSvc storage to
  // to be started.
  // We also do not have a way to verify that HTTP3 has been tried, because
  // the fallback is automatic, so try a couple of times.
  do {
    // We need to close HTTP2 connections, otherwise our connection pooling
    // will dispatch the request over already existing HTTP2 connection.
    Services.obs.notifyObservers(null, "net:prune-all-connections");
    let chan = makeChan();
    let listener = new CheckOnlyHttp2Listener();
    await altsvcSetupPromise(chan, listener);
    result++;
  } while (result < 5);
});

let CheckOnlyHttp2Listener = function () {};

CheckOnlyHttp2Listener.prototype = {
  onStartRequest: function testOnStartRequest() {},

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request, status) {
    Assert.equal(status, Cr.NS_OK);
    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    Assert.equal(httpVersion, "h2");

    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump("routed is " + routed + "\n");
    Assert.ok(routed === "0" || routed === "NA");
    this.finish();
  },
};

add_task(
  {
    // Happy Eyeballs currently doesn't exclude H3.
    skip_if: () =>
      Services.prefs.getBoolPref("network.http.happy_eyeballs_enabled", false),
  },
  async function test_no_http3_after_error() {
    let chan = makeChan();
    let listener = new CheckOnlyHttp2Listener();
    await altsvcSetupPromise(chan, listener);
  }
);

// also after all connections are closed.
add_task(
  {
    // Happy Eyeballs currently doesn't exclude H3.
    skip_if: () =>
      Services.prefs.getBoolPref("network.http.happy_eyeballs_enabled", false),
  },
  async function test_no_http3_after_error2() {
    Services.obs.notifyObservers(null, "net:prune-all-connections");
    let chan = makeChan();
    let listener = new CheckOnlyHttp2Listener();
    await altsvcSetupPromise(chan, listener);
  }
);
