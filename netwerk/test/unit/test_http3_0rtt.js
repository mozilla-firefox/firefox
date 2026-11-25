/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

registerCleanupFunction(async () => {
  http3_clear_prefs();
});

add_task(async function setup() {
  await http3_setup_tests("h3");
});

let Http3Listener = function () {};

Http3Listener.prototype = {
  resumed: false,

  onStartRequest: function testOnStartRequest(request) {
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);

    let secinfo = request.securityInfo;
    Assert.equal(secinfo.resumed, this.resumed);
    Assert.notEqual(secinfo.serverCert, null);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request) {
    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    Assert.equal(httpVersion, "h3");

    this.finish();
  },
};

function chanPromise(chan, listener) {
  return new Promise(resolve => {
    function finish(result) {
      resolve(result);
    }
    listener.finish = finish;
    chan.asyncOpen(listener);
  });
}

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

async function test_first_conn_no_resumed() {
  let listener = new Http3Listener();
  listener.resumed = false;
  let chan = makeChan("https://foo.example.com/30");
  await chanPromise(chan, listener);
}

async function test_0RTT(enable_0rtt, resumed) {
  info(`enable_0rtt=${enable_0rtt} resumed=${resumed}`);
  Services.prefs.setBoolPref("network.http.http3.enable_0rtt", enable_0rtt);

  // Make sure the h3 connection created by the previous test is cleared.
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1000));

  // This connecion should be resumed.
  let listener = new Http3Listener();
  listener.resumed = resumed;
  let chan = makeChan("https://foo.example.com/30");
  await chanPromise(chan, listener);
}

add_task(async function test_0RTT_setups() {
  await test_first_conn_no_resumed();

  // http3.0RTT enabled
  await test_0RTT(true, true);

  // http3.0RTT disabled
  await test_0RTT(false, false);
});
