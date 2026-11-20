"use strict";

let h3Route;
let httpsOrigin;
let h3AltSvc;
let prefs;

let tests = [test_https_alt_svc, test_response_421, testsDone];
let current_test = 0;

function run_next_test() {
  if (current_test < tests.length) {
    dump("starting test number " + current_test + "\n");
    tests[current_test]();
    current_test++;
  }
}

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
    "/Response421": (req, res) => {
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("You Win! (by requesting/Response421)");
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

let Http3Listener = function () {};

Http3Listener.prototype = {
  onDataAvailableFired: false,
  buffer: "",
  routed: "",
  httpVersion: "",

  onStartRequest: function testOnStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.buffer = this.buffer.concat(read_stream(stream, cnt));
  },

  onStopRequest: function testOnStopRequest(request, status) {
    Assert.equal(status, Cr.NS_OK);
    Assert.equal(this.onDataAvailableFired, true);
    this.routed = "NA";
    try {
      this.routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump("routed is " + this.routed + "\n");

    this.httpVersion = "";
    try {
      this.httpVersion = request.protocolVersion;
    } catch (e) {}
    dump("httpVersion is " + this.httpVersion + "\n");
  },
};

let WaitForHttp3Listener = function () {};

WaitForHttp3Listener.prototype = new Http3Listener();

WaitForHttp3Listener.prototype.uri = "";

WaitForHttp3Listener.prototype.onStopRequest = function testOnStopRequest(
  request,
  status
) {
  Http3Listener.prototype.onStopRequest.call(this, request, status);

  if (this.routed == h3Route) {
    Assert.equal(this.httpVersion, "h3");
    run_next_test();
  } else {
    dump("poll later for alt svc mapping\n");
    do_test_pending();
    do_timeout(1000, () => {
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      do_timeout(500, () => {
        doTest(this.uri);
      });
    });
  }

  do_test_finished();
};

function doTest(uri) {
  let chan = makeChan(uri);
  let listener = new WaitForHttp3Listener();
  listener.uri = uri;
  chan.setRequestHeader("x-altsvc", h3AltSvc, false);
  chan.asyncOpen(listener);
}

// Test Alt-Svc for http3.
// H2 server returns alt-svc=h3=:h3port
function test_https_alt_svc() {
  dump("test_https_alt_svc()\n");

  do_test_pending();
  doTest(httpsOrigin + "http3-test");
}

let Resp421Listener = function () {};

Resp421Listener.prototype = new Http3Listener();

Resp421Listener.prototype.onStopRequest = function testOnStopRequest(
  request,
  status
) {
  Http3Listener.prototype.onStopRequest.call(this, request, status);

  Assert.equal(this.routed, "0");
  Assert.equal(this.httpVersion, "h2");
  Assert.ok(this.buffer.match("You Win! [(]by requesting/Response421[)]"));

  run_next_test();
  do_test_finished();
};

function test_response_421() {
  dump("test_response_421()\n");

  let listener = new Resp421Listener();
  let chan = makeChan(httpsOrigin + "Response421");
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
