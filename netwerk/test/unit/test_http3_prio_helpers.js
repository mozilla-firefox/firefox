/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// uses head_http3.js, which uses http2-ca.pem
"use strict";

/* exported inChildProcess, test_flag_priority */
function inChildProcess() {
  return Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;
}

let Http3Listener = function (
  closure,
  expected_priority,
  expected_incremental,
  context
) {
  this._closure = closure;
  this._expected_priority = expected_priority;
  this._expected_incremental = expected_incremental;
  this._context = context;
};

// string -> [string, bool]
// "u=3,i" -> ["u=3", true]
function parse_priority_response_header(priority) {
  const priority_array = priority.split(",").map(element => element.trim());

  // parse for urgency string
  const urgency = priority_array.find(element => element.includes("u="));

  // parse for incremental bool
  const incremental = !!priority_array.find(element => element == "i");

  return [urgency ? urgency : null, incremental];
}

Http3Listener.prototype = {
  resumed: false,

  onStartRequest: function testOnStartRequest(request) {
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);

    let secinfo = request.securityInfo;
    Assert.equal(secinfo.resumed, this.resumed);
    Assert.notEqual(secinfo.serverCert, null);

    // check priority urgency and incremental from response header
    let priority_urgency = null;
    let incremental = null;
    try {
      const prh = request.getResponseHeader("priority-mirror");
      [priority_urgency, incremental] = parse_priority_response_header(prh);
    } catch (e) {
      console.log("Failed to get priority-mirror from response header");
    }
    Assert.equal(priority_urgency, this._expected_priority, this._context);
    Assert.equal(incremental, this._expected_incremental, this._context);
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

    try {
      this._closure();
    } catch (ex) {
      do_throw("Error in closure function: " + ex);
    }
  },
};

function make_channel(url) {
  var request = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
  });
  request.QueryInterface(Ci.nsIHttpChannel);
  return request;
}

async function test_flag_priority(
  context,
  flag,
  expected_priority,
  incremental,
  expected_incremental
) {
  var chan = make_channel("https://foo.example.com/priority_mirror");
  var cos = chan.QueryInterface(Ci.nsIClassOfService);

  // configure the channel with flags
  if (flag != null) {
    cos.addClassFlags(flag);
  }

  // configure the channel with incremental
  if (incremental != null) {
    cos.incremental = incremental;
  }

  await new Promise(resolve =>
    chan.asyncOpen(
      new Http3Listener(
        resolve,
        expected_priority,
        expected_incremental,
        context
      )
    )
  );
}
