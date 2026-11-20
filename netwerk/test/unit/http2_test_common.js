// test HTTP/2

"use strict";

/* import-globals-from head_channels.js */
/* import-globals-from http2_test_server.js */

// Generate a small and a large post with known pre-calculated md5 sums
function generateContent(size) {
  var content = "";
  for (var i = 0; i < size; i++) {
    content += "0";
  }
  return content;
}

var posts = [];
posts.push(generateContent(10));
posts.push(generateContent(250000));
posts.push(generateContent(128000));

// pre-calculated md5sums (in hex) of the above posts
var md5s = [
  "f1b708bba17f1ce948dc979f4d7092bc",
  "2ef8d3b6c8f329318eb1a119b12622b6",
];

var bigListenerData = generateContent(128 * 1024);
var bigListenerMD5 = "8f607cfdd2c87d6a7eedb657dafbd836";

function checkIsHttp2(request) {
  try {
    if (request.getResponseHeader("X-Firefox-Spdy") == "h2") {
      if (request.getResponseHeader("X-Connection-Http2") == "yes") {
        return true;
      }
      return false; // Weird case, but the server disagrees with us
    }
  } catch (e) {
    // Nothing to do here
  }
  return false;
}

var Http2CheckListener = function () {};

Http2CheckListener.prototype = {
  onStartRequestFired: false,
  onDataAvailableFired: false,
  isHttp2Connection: false,
  shouldBeHttp2: true,
  accum: 0,
  expected: -1,
  shouldSucceed: true,

  onStartRequest: function testOnStartRequest(request) {
    this.onStartRequestFired = true;
    if (this.shouldSucceed && !Components.isSuccessCode(request.status)) {
      do_throw("Channel should have a success code! (" + request.status + ")");
    } else if (
      !this.shouldSucceed &&
      Components.isSuccessCode(request.status)
    ) {
      do_throw("Channel succeeded unexpectedly!");
    }

    Assert.ok(request instanceof Ci.nsIHttpChannel);
    if (this.noResponseStatus) {
      Assert.throws(
        () => request.responseStatus,
        /NS_ERROR_NOT_AVAILABLE/,
        "getting response status should throw"
      );
      return;
    }
    Assert.equal(request.requestSucceeded, this.shouldSucceed);
    if (this.shouldSucceed) {
      Assert.equal(request.responseStatus, 200);
    }
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.isHttp2Connection = checkIsHttp2(request);
    this.accum += cnt;
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request, status) {
    Assert.ok(this.onStartRequestFired);
    if (this.expected != -1) {
      Assert.equal(this.accum, this.expected);
    }

    if (this.shouldSucceed) {
      Assert.ok(Components.isSuccessCode(status));
      Assert.ok(this.onDataAvailableFired);
      Assert.equal(this.isHttp2Connection, this.shouldBeHttp2);
    } else {
      Assert.ok(!Components.isSuccessCode(status));
    }
    request.QueryInterface(Ci.nsIProxiedChannel);
    var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
    this.finish({ httpProxyConnectResponseCode });
  },
};

/*
 * Support for testing valid multiplexing of streams
 */

var multiplexContent = generateContent(30 * 1024);

/* Listener class to control the testing of multiplexing */
var Http2MultiplexListener = function () {};

Http2MultiplexListener.prototype = new Http2CheckListener();

Http2MultiplexListener.prototype.streamID = 0;
Http2MultiplexListener.prototype.buffer = "";

Http2MultiplexListener.prototype.onDataAvailable = function (
  request,
  stream,
  off,
  cnt
) {
  this.onDataAvailableFired = true;
  this.isHttp2Connection = checkIsHttp2(request);
  this.streamID = parseInt(request.getResponseHeader("X-Http2-StreamID"));
  var data = read_stream(stream, cnt);
  this.buffer = this.buffer.concat(data);
};

Http2MultiplexListener.prototype.onStopRequest = function (request) {
  Assert.ok(this.onStartRequestFired);
  Assert.ok(this.onDataAvailableFired);
  Assert.ok(this.isHttp2Connection);
  Assert.equal(this.buffer, multiplexContent);

  request.QueryInterface(Ci.nsIProxiedChannel);
  // This is what does most of the hard work for us
  var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
  var streamID = this.streamID;
  this.finish({ httpProxyConnectResponseCode, streamID });
};

// Does the appropriate checks for header gatewaying
var Http2HeaderListener = function (name, callback) {
  this.name = name;
  this.callback = callback;
};

Http2HeaderListener.prototype = new Http2CheckListener();
Http2HeaderListener.prototype.value = "";

Http2HeaderListener.prototype.onDataAvailable = function (
  request,
  stream,
  off,
  cnt
) {
  this.onDataAvailableFired = true;
  this.isHttp2Connection = checkIsHttp2(request);
  var hvalue = request.getResponseHeader(this.name);
  Assert.notEqual(hvalue, "");
  this.callback(hvalue);
  read_stream(stream, cnt);
};

const pushHdrTxt =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const pullHdrTxt = pushHdrTxt.split("").reverse().join("");

function checkContinuedHeaders(getHeader, headerPrefix, headerText) {
  for (var i = 0; i < 265; i++) {
    Assert.equal(getHeader(headerPrefix + 1), headerText);
  }
}

// Does the appropriate checks for a large GET response
var Http2BigListener = function () {};

Http2BigListener.prototype = new Http2CheckListener();
Http2BigListener.prototype.buffer = "";

Http2BigListener.prototype.onDataAvailable = function (
  request,
  stream,
  off,
  cnt
) {
  this.onDataAvailableFired = true;
  this.isHttp2Connection = checkIsHttp2(request);
  this.buffer = this.buffer.concat(read_stream(stream, cnt));
  // We know the server should send us the same data as our big post will be,
  // so the md5 should be the same
  Assert.equal(bigListenerMD5, request.getResponseHeader("X-Expected-MD5"));
};

Http2BigListener.prototype.onStopRequest = function (request) {
  Assert.ok(this.onStartRequestFired);
  Assert.ok(this.onDataAvailableFired);
  Assert.ok(this.isHttp2Connection);

  // Don't want to flood output, so don't use do_check_eq
  Assert.equal(this.buffer, bigListenerData);

  request.QueryInterface(Ci.nsIProxiedChannel);
  var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
  this.finish({ httpProxyConnectResponseCode });
};

var Http2HugeSuspendedListener = function () {};

Http2HugeSuspendedListener.prototype = new Http2CheckListener();
Http2HugeSuspendedListener.prototype.count = 0;

Http2HugeSuspendedListener.prototype.onDataAvailable = function (
  request,
  stream,
  off,
  cnt
) {
  this.onDataAvailableFired = true;
  this.isHttp2Connection = checkIsHttp2(request);
  this.count += cnt;
  read_stream(stream, cnt);
};

Http2HugeSuspendedListener.prototype.onStopRequest = function (request) {
  Assert.ok(this.onStartRequestFired);
  Assert.ok(this.onDataAvailableFired);
  Assert.ok(this.isHttp2Connection);
  Assert.equal(this.count, 1024 * 1024 * 1); // 1mb of data expected
  request.QueryInterface(Ci.nsIProxiedChannel);
  var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
  this.finish({ httpProxyConnectResponseCode });
};

// Does the appropriate checks for POSTs
var Http2PostListener = function (expected_md5) {
  this.expected_md5 = expected_md5;
};

Http2PostListener.prototype = new Http2CheckListener();
Http2PostListener.prototype.expected_md5 = "";

Http2PostListener.prototype.onDataAvailable = function (
  request,
  stream,
  off,
  cnt
) {
  this.onDataAvailableFired = true;
  this.isHttp2Connection = checkIsHttp2(request);
  read_stream(stream, cnt);
  Assert.equal(
    this.expected_md5,
    request.getResponseHeader("X-Calculated-MD5")
  );
};

var ResumeStalledChannelListener = function () {};

ResumeStalledChannelListener.prototype = {
  onStartRequestFired: false,
  onDataAvailableFired: false,
  isHttp2Connection: false,
  shouldBeHttp2: true,
  resumable: null,

  onStartRequest: function testOnStartRequest(request) {
    this.onStartRequestFired = true;
    if (!Components.isSuccessCode(request.status)) {
      do_throw("Channel should have a success code! (" + request.status + ")");
    }

    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(request.responseStatus, 200);
    Assert.equal(request.requestSucceeded, true);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.isHttp2Connection = checkIsHttp2(request);
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request, status) {
    Assert.ok(this.onStartRequestFired);
    Assert.ok(Components.isSuccessCode(status));
    Assert.ok(this.onDataAvailableFired);
    Assert.equal(this.isHttp2Connection, this.shouldBeHttp2);
    this.resumable.resume();
  },
};

// test a large download that creates stream flow control and
// confirm we can do another independent stream while the download
// stream is stuck
async function test_http2_blocking_download(serverPort) {
  var chan = makeHTTPChannel(`https://localhost:${serverPort}/bigdownload`);
  var internalChannel = chan.QueryInterface(Ci.nsIHttpChannelInternal);
  internalChannel.initialRwin = 500000; // make the stream.suspend push back in h2
  var p = new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.expected = 3 * 1024 * 1024;
    chan.asyncOpen(listener);
    chan.suspend();
  });
  // wait 5 seconds so that stream flow control kicks in and then see if we
  // can do a basic transaction (i.e. session not blocked). afterwards resume
  // channel
  do_timeout(5000, function () {
    var simpleChannel = makeHTTPChannel(`https://localhost:${serverPort}/`);
    var sl = new ResumeStalledChannelListener();
    sl.resumable = chan;
    simpleChannel.asyncOpen(sl);
  });
  return p;
}

// Make sure we make a HTTP2 connection and both us and the server mark it as such
async function test_http2_basic(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/`);
  var p = new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
  return p;
}

async function test_http2_basic_unblocked_dep(
  serverPort,
  origin = "localhost"
) {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/basic_unblocked_dep`
  );
  var cos = chan.QueryInterface(Ci.nsIClassOfService);
  cos.addClassFlags(Ci.nsIClassOfService.Unblocked);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// make sure we don't use h2 when disallowed
async function test_http2_nospdy(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/`);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    var internalChannel = chan.QueryInterface(Ci.nsIHttpChannelInternal);
    internalChannel.allowSpdy = false;
    listener.shouldBeHttp2 = false;
    chan.asyncOpen(listener);
  });
}

// Support for making sure XHR works over SPDY
function checkXhr(xhr, finish) {
  if (xhr.readyState != 4) {
    return;
  }

  Assert.equal(xhr.status, 200);
  Assert.equal(checkIsHttp2(xhr), true);
  finish();
}

// Fires off an XHR request over h2
async function test_http2_xhr(serverPort, origin = "localhost") {
  return new Promise(resolve => {
    var req = new XMLHttpRequest();
    req.open("GET", `https://${origin}:${serverPort}/`, true);
    req.addEventListener("readystatechange", function () {
      checkXhr(req, resolve);
    });
    req.send(null);
  });
}

var Http2ConcurrentListener = function () {};

Http2ConcurrentListener.prototype = new Http2CheckListener();
Http2ConcurrentListener.prototype.count = 0;
Http2ConcurrentListener.prototype.target = 0;
Http2ConcurrentListener.prototype.reset = 0;
Http2ConcurrentListener.prototype.recvdHdr = 0;

Http2ConcurrentListener.prototype.onStopRequest = function (request) {
  this.count++;
  Assert.ok(this.isHttp2Connection);
  if (this.recvdHdr > 0) {
    Assert.equal(request.getResponseHeader("X-Recvd"), this.recvdHdr);
  }

  if (this.count == this.target) {
    if (this.reset > 0) {
      Services.prefs.setIntPref(
        "network.http.http2.default-concurrent",
        this.reset
      );
    }
    request.QueryInterface(Ci.nsIProxiedChannel);
    var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
    this.finish({ httpProxyConnectResponseCode });
  }
};

async function test_http2_concurrent(
  concurrent_channels,
  serverPort,
  origin = "localhost"
) {
  var p = new Promise(resolve => {
    var concurrent_listener = new Http2ConcurrentListener();
    concurrent_listener.finish = resolve;
    concurrent_listener.target = 201;
    concurrent_listener.reset = Services.prefs.getIntPref(
      "network.http.http2.default-concurrent"
    );
    Services.prefs.setIntPref("network.http.http2.default-concurrent", 100);

    for (var i = 0; i < concurrent_listener.target; i++) {
      concurrent_channels[i] = makeHTTPChannel(
        `https://${origin}:${serverPort}/750ms`
      );
      concurrent_channels[i].loadFlags = Ci.nsIRequest.LOAD_BYPASS_CACHE;
      concurrent_channels[i].asyncOpen(concurrent_listener);
    }
  });
  return p;
}

async function test_http2_concurrent_post(
  concurrent_channels,
  serverPort,
  origin = "localhost"
) {
  return new Promise(resolve => {
    var concurrent_listener = new Http2ConcurrentListener();
    concurrent_listener.finish = resolve;
    concurrent_listener.target = 8;
    concurrent_listener.recvdHdr = posts[2].length;
    concurrent_listener.reset = Services.prefs.getIntPref(
      "network.http.http2.default-concurrent"
    );
    Services.prefs.setIntPref("network.http.http2.default-concurrent", 3);

    for (var i = 0; i < concurrent_listener.target; i++) {
      concurrent_channels[i] = makeHTTPChannel(
        `https://${origin}:${serverPort}/750msPost`
      );
      concurrent_channels[i].loadFlags = Ci.nsIRequest.LOAD_BYPASS_CACHE;
      var stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
        Ci.nsIStringInputStream
      );
      stream.setByteStringData(posts[2]);
      var uchan = concurrent_channels[i].QueryInterface(Ci.nsIUploadChannel);
      uchan.setUploadStream(stream, "text/plain", stream.available());
      concurrent_channels[i].requestMethod = "POST";
      concurrent_channels[i].asyncOpen(concurrent_listener);
    }
  });
}

// Test to make sure we get multiplexing right
async function test_http2_multiplex(serverPort) {
  let chan1 = makeHTTPChannel(`https://localhost:${serverPort}/multiplex1`);
  let chan2 = makeHTTPChannel(`https://localhost:${serverPort}/multiplex2`);
  let listener1 = new Http2MultiplexListener();
  let listener2 = new Http2MultiplexListener();

  let promises = [];
  let p1 = new Promise(resolve => {
    listener1.finish = resolve;
  });
  promises.push(p1);
  let p2 = new Promise(resolve => {
    listener2.finish = resolve;
  });
  promises.push(p2);

  chan1.asyncOpen(listener1);
  chan2.asyncOpen(listener2);
  return Promise.all(promises);
}

// Test to make sure we gateway non-standard headers properly
async function test_http2_header(serverPort, origin = "localhost") {
  let chan = makeHTTPChannel(`https://${origin}:${serverPort}/header`);
  let hvalue = "Headers are fun";
  chan.setRequestHeader("X-Test-Header", hvalue, false);
  return new Promise(resolve => {
    let listener = new Http2HeaderListener("X-Received-Test-Header", function (
      received_hvalue
    ) {
      Assert.equal(received_hvalue, hvalue);
    });
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// Test to make sure headers with invalid characters in the name are rejected
async function test_http2_invalid_response_header(
  serverPort,
  invalid_kind,
  origin = "localhost"
) {
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldSucceed = false;
    var chan = makeHTTPChannel(
      `https://${origin}:${serverPort}/invalid_response_header/${invalid_kind}`
    );
    chan.asyncOpen(listener);
  });
}

// Test to make sure cookies are split into separate fields before compression
async function test_http2_cookie_crumbling(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/cookie_crumbling`
  );
  var cookiesSent = ["a=b", "c=d01234567890123456789", "e=f"].sort();
  chan.setRequestHeader("Cookie", cookiesSent.join("; "), false);
  return new Promise(resolve => {
    var listener = new Http2HeaderListener("X-Received-Header-Pairs", function (
      pairsReceived
    ) {
      var cookiesReceived = JSON.parse(pairsReceived)
        .filter(function (pair) {
          return pair[0] == "cookie";
        })
        .map(function (pair) {
          return pair[1];
        })
        .sort();
      Assert.equal(cookiesReceived.length, cookiesSent.length);
      cookiesReceived.forEach(function (cookieReceived, index) {
        Assert.equal(cookiesSent[index], cookieReceived);
      });
    });
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// this is a basic test where the server sends a simple document with 2 header
// blocks. bug 1027364
async function test_http2_doubleheader(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/doubleheader`);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// Make sure we handle GETs that cover more than 2 frames properly
async function test_http2_big(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/big`);
  return new Promise(resolve => {
    var listener = new Http2BigListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

async function test_http2_huge_suspended(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/huge`);
  return new Promise(resolve => {
    var listener = new Http2HugeSuspendedListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
    chan.suspend();
    do_timeout(500, chan.resume);
  });
}

// Support for doing a POST
function do_post(content, chan, listener, method) {
  var stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stream.setByteStringData(content);

  var uchan = chan.QueryInterface(Ci.nsIUploadChannel);
  uchan.setUploadStream(stream, "text/plain", stream.available());

  chan.requestMethod = method;

  chan.asyncOpen(listener);
}

// Make sure we can do a simple POST
async function test_http2_post(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/post`);
  var p = new Promise(resolve => {
    var listener = new Http2PostListener(md5s[0]);
    listener.finish = resolve;
    do_post(posts[0], chan, listener, "POST");
  });
  return p;
}

async function test_http2_empty_post(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/post`);
  var p = new Promise(resolve => {
    var listener = new Http2PostListener("0");
    listener.finish = resolve;
    do_post("", chan, listener, "POST");
  });
  return p;
}

// Make sure we can do a simple PATCH
async function test_http2_patch(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/patch`);
  return new Promise(resolve => {
    var listener = new Http2PostListener(md5s[0]);
    listener.finish = resolve;
    do_post(posts[0], chan, listener, "PATCH");
  });
}

// Make sure we can do a POST that covers more than 2 frames
async function test_http2_post_big(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/post`);
  return new Promise(resolve => {
    var listener = new Http2PostListener(md5s[1]);
    listener.finish = resolve;
    do_post(posts[1], chan, listener, "POST");
  });
}

var WrongSuiteListener = function () {};

WrongSuiteListener.prototype = new Http2CheckListener();
WrongSuiteListener.prototype.shouldBeHttp2 = false;
WrongSuiteListener.prototype.onStopRequest = function (request, status) {
  Services.prefs.setBoolPref(
    "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256",
    true
  );
  Services.prefs.clearUserPref("security.tls.version.max");
  Http2CheckListener.prototype.onStopRequest.call(this, request, status);
};

// test that we use h1 without the mandatory cipher suite available when
// offering at most tls1.2
async function test_http2_wrongsuite_tls12(serverPort, origin = "localhost") {
  Services.prefs.setBoolPref(
    "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256",
    false
  );
  Services.prefs.setIntPref("security.tls.version.max", 3);
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/wrongsuite`);
  chan.loadFlags =
    Ci.nsIRequest.LOAD_FRESH_CONNECTION |
    Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return new Promise(resolve => {
    var listener = new WrongSuiteListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// test that we use h2 when offering tls1.3 or higher regardless of if the
// mandatory cipher suite is available
async function test_http2_wrongsuite_tls13(serverPort, origin = "localhost") {
  Services.prefs.setBoolPref(
    "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256",
    false
  );
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/wrongsuite`);
  chan.loadFlags =
    Ci.nsIRequest.LOAD_FRESH_CONNECTION |
    Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return new Promise(resolve => {
    var listener = new WrongSuiteListener();
    listener.finish = resolve;
    listener.shouldBeHttp2 = true;
    chan.asyncOpen(listener);
  });
}

async function test_http2_h11required_stream(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/h11required_stream`
  );
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldBeHttp2 = false;
    chan.asyncOpen(listener);
  });
}

function H11RequiredSessionListener() {}

H11RequiredSessionListener.prototype = new Http2CheckListener();

H11RequiredSessionListener.prototype.onStopRequest = function (request) {
  var streamReused = request.getResponseHeader("X-H11Required-Stream-Ok");
  Assert.equal(streamReused, "yes");

  Assert.ok(this.onStartRequestFired);
  Assert.ok(this.onDataAvailableFired);
  Assert.equal(this.isHttp2Connection, this.shouldBeHttp2);

  request.QueryInterface(Ci.nsIProxiedChannel);
  var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
  this.finish({ httpProxyConnectResponseCode });
};

async function test_http2_h11required_session(
  serverPort,
  origin = "localhost"
) {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/h11required_session`
  );
  return new Promise(resolve => {
    var listener = new H11RequiredSessionListener();
    listener.finish = resolve;
    listener.shouldBeHttp2 = false;
    chan.asyncOpen(listener);
  });
}

async function test_http2_retry_rst(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/rstonce`);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

// Server resets the first H2 request with RST_STREAM CANCEL; the retry succeeds.
async function test_http2_unknown_rst(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/unknown_rst_once`
  );
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

async function test_http2_continuations_over_max_response_limit(
  loadGroup,
  serverPort,
  origin = "localhost"
) {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/hugecontinuedheaders?size=385`
  );
  chan.loadGroup = loadGroup;
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldSucceed = false;
    listener.noResponseStatus = true;
    chan.asyncOpen(listener);
  });
}

function Http2IllegalHpackValidationListener() {}

Http2IllegalHpackValidationListener.prototype = new Http2CheckListener();
Http2IllegalHpackValidationListener.prototype.shouldGoAway = false;

Http2IllegalHpackValidationListener.prototype.onStopRequest = function (
  request
) {
  var wentAway = request.getResponseHeader("X-Did-Goaway") === "yes";
  Assert.equal(wentAway, this.shouldGoAway);

  Assert.ok(this.onStartRequestFired);
  Assert.ok(this.onDataAvailableFired);
  Assert.equal(this.isHttp2Connection, this.shouldBeHttp2);

  request.QueryInterface(Ci.nsIProxiedChannel);
  var httpProxyConnectResponseCode = request.httpProxyConnectResponseCode;
  this.finish({ httpProxyConnectResponseCode });
};

function Http2IllegalHpackListener() {}
Http2IllegalHpackListener.prototype = new Http2CheckListener();
Http2IllegalHpackListener.prototype.shouldGoAway = false;

Http2IllegalHpackListener.prototype.onStopRequest = function () {
  var chan = makeHTTPChannel(
    `https://${this.origin}:${this.serverPort}/illegalhpack_validate`
  );
  var listener = new Http2IllegalHpackValidationListener();
  listener.finish = this.finish;
  listener.shouldGoAway = this.shouldGoAway;
  chan.asyncOpen(listener);
};

async function test_http2_illegalhpacksoft(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/illegalhpacksoft`
  );
  return new Promise(resolve => {
    var listener = new Http2IllegalHpackListener();
    listener.finish = resolve;
    listener.serverPort = serverPort;
    listener.origin = origin;
    listener.shouldGoAway = false;
    listener.shouldSucceed = false;
    chan.asyncOpen(listener);
  });
}

async function test_http2_illegalhpackhard(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/illegalhpackhard`
  );
  return new Promise(resolve => {
    var listener = new Http2IllegalHpackListener();
    listener.finish = resolve;
    listener.serverPort = serverPort;
    listener.origin = origin;
    listener.shouldGoAway = true;
    listener.shouldSucceed = false;
    chan.asyncOpen(listener);
  });
}

async function test_http2_folded_header(
  loadGroup,
  serverPort,
  origin = "localhost"
) {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/foldedheader`);
  chan.loadGroup = loadGroup;
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldSucceed = false;
    chan.asyncOpen(listener);
  });
}

async function test_http2_empty_data(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/emptydata`);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    chan.asyncOpen(listener);
  });
}

async function test_http2_continuation_stream_zero(
  serverPort,
  origin = "localhost"
) {
  var chan = makeHTTPChannel(
    `https://${origin}:${serverPort}/continuation_stream_zero`
  );
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldSucceed = false;
    chan.asyncOpen(listener);
  });
}

async function test_http2_status_phrase(serverPort, origin = "localhost") {
  var chan = makeHTTPChannel(`https://${origin}:${serverPort}/statusphrase`);
  return new Promise(resolve => {
    var listener = new Http2CheckListener();
    listener.finish = resolve;
    listener.shouldSucceed = false;
    chan.asyncOpen(listener);
  });
}

var PulledDiskCacheListener = function () {};
PulledDiskCacheListener.prototype = new Http2CheckListener();
PulledDiskCacheListener.prototype.EXPECTED_DATA = "this was pulled via h2";
PulledDiskCacheListener.prototype.readData = "";
PulledDiskCacheListener.prototype.onDataAvailable =
  function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.isHttp2Connection = checkIsHttp2(request);
    this.accum += cnt;
    this.readData += read_stream(stream, cnt);
  };
PulledDiskCacheListener.prototype.onStopRequest = function testOnStopRequest(
  request,
  status
) {
  Assert.equal(this.EXPECTED_DATA, this.readData);
  Http2CheckListener.prorotype.onStopRequest.call(this, request, status);
};

const DISK_CACHE_DATA = "this is from disk cache";

var FromDiskCacheListener = function (finish, loadGroup, serverPort) {
  this.finish = finish;
  this.loadGroup = loadGroup;
  this.serverPort = serverPort;
};
FromDiskCacheListener.prototype = {
  onStartRequestFired: false,
  onDataAvailableFired: false,
  readData: "",

  onStartRequest: function testOnStartRequest(request) {
    this.onStartRequestFired = true;
    if (!Components.isSuccessCode(request.status)) {
      do_throw("Channel should have a success code! (" + request.status + ")");
    }

    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.ok(request.requestSucceeded);
    Assert.equal(request.responseStatus, 200);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.readData += read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request, status) {
    Assert.ok(this.onStartRequestFired);
    Assert.ok(Components.isSuccessCode(status));
    Assert.ok(this.onDataAvailableFired);
    Assert.equal(this.readData, DISK_CACHE_DATA);

    evict_cache_entries("disk");
    syncWithCacheIOThread(() => {
      // Now that we know the entry is out of the disk cache, check to make sure
      // we don't have this hiding in the push cache somewhere - if we do, it
      // didn't get cancelled, and we have a bug.
      var chan = makeHTTPChannel(
        `https://localhost:${this.serverPort}/diskcache`
      );
      var listener = new PulledDiskCacheListener();
      listener.finish = this.finish;
      chan.loadGroup = this.loadGroup;
      chan.asyncOpen(listener);
    });
  },
};

// Starts a server for the HTTP/2 tests above and returns its port.
async function start_http2_test_server() {
  const { NodeLegacyHTTP2Server } = ChromeUtils.importESModule(
    "resource://testing-common/NodeServer.sys.mjs"
  );
  let server = new NodeLegacyHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.execute(Http2TestServerCode);
  await server.execute("Http2TestServerCode.install()");
  await server.execute(
    "global.request_handler = Http2TestServerCode.handleRequest"
  );
  return server.port();
}
