/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

registerCleanupFunction(async () => {
  http3_clear_prefs();
});

let httpsUri;

add_task(async function pre_setup() {
  let h2Server = await create_h2_server();
  httpsUri = "https://foo.example.com:" + h2Server.port() + "/";
  Services.prefs.setBoolPref("network.http.http3.support_version1", true);
});

add_task(async function setup() {
  await http3_setup_tests("h3");
});

function chanPromise(chan, listener) {
  return new Promise(resolve => {
    function finish() {
      resolve();
    }
    listener.finish = finish;
    chan.asyncOpen(listener);
  });
}

function makeH2Chan() {
  let chan = NetUtil.newChannel({
    uri: httpsUri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

let Http3Listener = function () {};

Http3Listener.prototype = {
  version1enabled: "",

  onStartRequest: function testOnStartRequest() {},

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request) {
    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    if (this.version1enabled) {
      Assert.equal(httpVersion, "h3");
    } else {
      Assert.equal(httpVersion, "h2");
    }

    this.finish();
  },
};

add_task(async function test_version1_enabled_1() {
  Services.prefs.setBoolPref("network.http.http3.support_version1", true);
  let listener = new Http3Listener();
  listener.version1enabled = true;
  let chan = makeH2Chan("https://foo.example.com/");
  await chanPromise(chan, listener);
});

add_task(async function test_version1_disabled() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  Services.prefs.setBoolPref("network.http.http3.support_version1", false);
  let listener = new Http3Listener();
  listener.version1enabled = false;
  let chan = makeH2Chan("https://foo.example.com/");
  await chanPromise(chan, listener);
});

add_task(async function test_version1_enabled_2() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  Services.prefs.setBoolPref("network.http.http3.support_version1", true);
  let listener = new Http3Listener();
  listener.version1enabled = true;
  let chan = makeH2Chan("https://foo.example.com/");
  await chanPromise(chan, listener);
});
