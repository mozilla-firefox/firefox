/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("security.tls.version.max");
  http3_clear_prefs();
});

let httpsUri;

add_task(async function setup() {
  let h2Server = await create_h2_server();
  httpsUri = "https://foo.example.com:" + h2Server.port() + "/";

  await http3_setup_tests("h3", true);
});

let Listener = function () {};

Listener.prototype = {
  resumed: false,

  onStartRequest: function testOnStartRequest(request) {
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request) {
    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    if (this.expect_http3) {
      Assert.equal(httpVersion, "h3");
    } else {
      Assert.notEqual(httpVersion, "h3");
    }

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

async function test_http3_used(expect_http3) {
  let listener = new Listener();
  listener.expect_http3 = expect_http3;
  let chan = makeChan(httpsUri);
  await chanPromise(chan, listener);
}

add_task(async function test_tls13_pref() {
  await test_http3_used(true);
  // Try one more time.
  await test_http3_used(true);

  // Disable TLS1.3
  Services.prefs.setIntPref("security.tls.version.max", 3);
  await test_http3_used(false);
  // Try one more time.
  await test_http3_used(false);

  // Enable TLS1.3
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  Services.prefs.setIntPref("security.tls.version.max", 4);
  await test_http3_used(true);
  // Try one more time.
  await test_http3_used(true);
});
