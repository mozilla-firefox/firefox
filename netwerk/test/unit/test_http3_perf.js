"use strict";

// This test is run as part of the perf tests which require the metadata.
/* exported perfMetadata */
var perfMetadata = {
  owner: "Network Team",
  name: "http3 raw",
  description:
    "XPCShell tests that verifies the lib integration against a local server",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        {
          name: "speed",
          unit: "bps",
        },
      ],
      xpcshell_cycles: 13,
      verbose: true,
      try_platform: ["linux", "mac"],
    },
  },
  tags: ["network", "http3", "quic"],
};

var performance = performance || {};
performance.now = (function () {
  return (
    performance.now ||
    performance.mozNow ||
    performance.msNow ||
    performance.oNow ||
    performance.webkitNow ||
    Date.now
  );
})();

let h3Route;
let httpsOrigin;
let h3AltSvc;

let prefs;

let tests = [
  // This test must be the first because it setsup alt-svc connection, that
  // other tests use.
  test_https_alt_svc,
  test_download,
  testsDone,
];

let current_test = 0;

function run_next_test() {
  if (current_test < tests.length) {
    dump("starting test number " + current_test + "\n");
    tests[current_test]();
    current_test++;
  }
}

// eslint-disable-next-line no-unused-vars
function run_test() {
  do_get_profile();
  do_test_pending();
  startServer().then(h2Port => {
    setup(h2Port);
    do_test_finished();
  });
}

async function startServer() {
  let server = await create_h2_server({
    "/http3-test": (req, res) => {
      res.setHeader("Cache-Control", "no-cache");
      res.setHeader("Alt-Svc", "h3=" + req.headers["x-altsvc"]);
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    },
  });
  return server.port();
}

function setup(h2Port) {
  let h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");
  h3AltSvc = ":" + h3Port;

  h3Route = "foo.example.com:" + h3Port;
  prefs = Services.prefs;

  prefs.setBoolPref("network.http.http3.enable", true);
  prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  // We always resolve elements of localDomains as it's hardcoded without the
  // following pref:
  prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);

  // The certificate for the http3server server is for foo.example.com and
  // is signed by http2-ca.pem so add that cert to the trust list as a
  // signing cert.
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  httpsOrigin = "https://foo.example.com:" + h2Port + "/";

  run_next_test();
}

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

let Http3CheckListener = function () {};

Http3CheckListener.prototype = {
  onDataAvailableFired: false,
  expectedRoute: "",

  onStartRequest: function testOnStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request, status) {
    dump("status is " + status + "\n");
    // Assert.equal(status, Cr.NS_OK);
    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump("routed is " + routed + "\n");

    Assert.equal(routed, this.expectedRoute);

    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    Assert.equal(httpVersion, "h3");
    Assert.equal(this.onDataAvailableFired, true);
  },
};

let WaitForHttp3Listener = function () {};

WaitForHttp3Listener.prototype = new Http3CheckListener();

WaitForHttp3Listener.prototype.uri = "";
WaitForHttp3Listener.prototype.h3AltSvc = "";

WaitForHttp3Listener.prototype.onStopRequest = function testOnStopRequest(
  request,
  status
) {
  Assert.equal(status, Cr.NS_OK);
  let routed = "NA";
  try {
    routed = request.getRequestHeader("Alt-Used");
  } catch (e) {}
  dump("routed is " + routed + "\n");

  if (routed == this.expectedRoute) {
    Assert.equal(routed, this.expectedRoute); // always true, but a useful log

    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    Assert.equal(httpVersion, "h3");
    run_next_test();
  } else {
    dump("poll later for alt svc mapping\n");
    do_test_pending();
    do_timeout(1000, () => {
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      do_timeout(500, () => {
        doTest(this.uri, this.expectedRoute, this.h3AltSvc);
      });
    });
  }

  do_test_finished();
};

function doTest(uri, expectedRoute, altSvc) {
  let chan = makeChan(uri);
  let listener = new WaitForHttp3Listener();
  listener.uri = uri;
  listener.expectedRoute = expectedRoute;
  listener.h3AltSvc = altSvc;
  chan.setRequestHeader("x-altsvc", altSvc, false);
  chan.asyncOpen(listener);
}

// Test Alt-Svc for http3.
// H2 server returns alt-svc=h3=:h3port
function test_https_alt_svc() {
  dump("test_https_alt_svc()\n");

  do_test_pending();
  doTest(httpsOrigin + "http3-test", h3Route, h3AltSvc);
}

let PerfHttp3Listener = function () {};

PerfHttp3Listener.prototype = new Http3CheckListener();
PerfHttp3Listener.prototype.amount = 0;
PerfHttp3Listener.prototype.bytesRead = 0;
PerfHttp3Listener.prototype.startTime = 0;

PerfHttp3Listener.prototype.onStartRequest = function testOnStartRequest(
  request
) {
  this.startTime = performance.now();
  Http3CheckListener.prototype.onStartRequest.call(this, request);
};

PerfHttp3Listener.prototype.onDataAvailable = function testOnStopRequest(
  request,
  stream,
  off,
  cnt
) {
  this.bytesRead += cnt;
  Http3CheckListener.prototype.onDataAvailable.call(
    this,
    request,
    stream,
    off,
    cnt
  );
};

PerfHttp3Listener.prototype.onStopRequest = function testOnStopRequest(
  request,
  status
) {
  let stopTime = performance.now();
  Http3CheckListener.prototype.onStopRequest.call(this, request, status);
  if (this.bytesRead != this.amount) {
    dump("Wrong number of bytes...");
  } else {
    let speed = (this.bytesRead * 1000) / (stopTime - this.startTime);
    info("perfMetrics", { speed });
  }
  run_next_test();
  do_test_finished();
};

function test_download() {
  dump("test_download()\n");

  let listener = new PerfHttp3Listener();
  listener.expectedRoute = h3Route;
  listener.amount = 1024 * 1024;
  let chan = makeChan(httpsOrigin + listener.amount.toString());
  chan.asyncOpen(listener);
  do_test_pending();
}

function testsDone() {
  prefs.clearUserPref("network.http.http3.enable");
  prefs.clearUserPref("network.dns.localDomains");
  prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
  dump("testDone\n");
  do_test_pending();
  do_test_finished();
}
