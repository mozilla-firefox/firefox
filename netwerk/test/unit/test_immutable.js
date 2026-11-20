"use strict";

var prefs;
var http2pref;
var origin;

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
    "/immutable-test-without-attribute": (req, res) => {
      res.setHeader("Cache-Control", "max-age=100000");
      res.setHeader("Etag", "1");
      if (req.headers["if-none-match"]) {
        res.setHeader("x-conditional", "true");
      }
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    },
    "/immutable-test-with-attribute": (req, res) => {
      res.setHeader("Cache-Control", "max-age=100000, immutable");
      res.setHeader("Etag", "2");
      if (req.headers["if-none-match"]) {
        res.setHeader("x-conditional", "true");
      }
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    },
    "/immutable-test-expired-with-Expires-header": (req, res) => {
      res.setHeader("Cache-Control", "immutable");
      res.setHeader("Expires", "Mon, 01 Jan 1990 00:00:00 GMT");
      res.setHeader("Etag", "3");
      if (req.headers["if-none-match"]) {
        res.setHeader("x-conditional", "true");
      }
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    },
    "/immutable-test-expired-with-last-modified-header": (req, res) => {
      res.setHeader("Cache-Control", "public, max-age=3600, immutable");
      res.setHeader("Date", "Mon, 01 Jan 1990 00:00:00 GMT");
      res.setHeader("Last-modified", "Mon, 01 Jan 1990 00:00:00 GMT");
      res.setHeader("Etag", "4");
      if (req.headers["if-none-match"]) {
        res.setHeader("x-conditional", "true");
      }
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    },
  });
  return server.port();
}

function setup(h2Port) {
  prefs = Services.prefs;

  http2pref = prefs.getBoolPref("network.http.http2.enabled");

  prefs.setBoolPref("network.http.http2.enabled", true);
  prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, bar.example.com"
  );

  origin = "https://foo.example.com:" + h2Port;
  dump("origin - " + origin + "\n");
  doTest1();
}

function resetPrefs() {
  prefs.setBoolPref("network.http.http2.enabled", http2pref);
  prefs.clearUserPref("network.dns.localDomains");
}

function makeChan(path) {
  return NetUtil.newChannel({
    uri: origin + path,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
}

var nextTest;
var expectPass = true;
var expectConditional = false;

var Listener = function () {};
Listener.prototype = {
  onStartRequest: function testOnStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);

    if (expectPass) {
      if (!Components.isSuccessCode(request.status)) {
        do_throw(
          "Channel should have a success code! (" + request.status + ")"
        );
      }
      Assert.equal(request.responseStatus, 200);
    } else {
      Assert.equal(Components.isSuccessCode(request.status), false);
    }
  },

  onDataAvailable: function testOnDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  },

  onStopRequest: function testOnStopRequest(request) {
    if (expectConditional) {
      Assert.equal(request.getResponseHeader("x-conditional"), "true");
    } else {
      try {
        Assert.notEqual(request.getResponseHeader("x-conditional"), "true");
      } catch (e) {
        Assert.ok(true);
      }
    }
    nextTest();
    do_test_finished();
  },
};

function testsDone() {
  dump("testDone\n");
  resetPrefs();
}

function doTest1() {
  dump("execute doTest1 - resource without immutable. initial request\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-without-attribute");
  var listener = new Listener();
  nextTest = doTest2;
  chan.asyncOpen(listener);
}

function doTest2() {
  dump("execute doTest2 - resource without immutable. reload\n");
  do_test_pending();
  expectConditional = true;
  var chan = makeChan("/immutable-test-without-attribute");
  var listener = new Listener();
  nextTest = doTest3;
  chan.loadFlags = Ci.nsIRequest.VALIDATE_ALWAYS;
  chan.asyncOpen(listener);
}

function doTest3() {
  dump("execute doTest3 - resource without immutable. shift reload\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-without-attribute");
  var listener = new Listener();
  nextTest = doTest4;
  chan.loadFlags = Ci.nsIRequest.LOAD_BYPASS_CACHE;
  chan.asyncOpen(listener);
}

function doTest4() {
  dump("execute doTest4 - resource with immutable. initial request\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-with-attribute");
  var listener = new Listener();
  nextTest = doTest5;
  chan.asyncOpen(listener);
}

function doTest5() {
  dump("execute doTest5 - resource with immutable. reload\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-with-attribute");
  var listener = new Listener();
  nextTest = doTest6;
  chan.loadFlags = Ci.nsIRequest.VALIDATE_ALWAYS;
  chan.asyncOpen(listener);
}

function doTest6() {
  dump("execute doTest6 - resource with immutable. shift reload\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-with-attribute");
  var listener = new Listener();
  nextTest = doTest7;
  chan.loadFlags = Ci.nsIRequest.LOAD_BYPASS_CACHE;
  chan.asyncOpen(listener);
}

function doTest7() {
  dump("execute doTest7 - expired resource with immutable. initial request\n");
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-expired-with-Expires-header");
  var listener = new Listener();
  nextTest = doTest8;
  chan.asyncOpen(listener);
}

function doTest8() {
  dump("execute doTest8 - expired resource with immutable. reload\n");
  do_test_pending();
  expectConditional = true;
  var chan = makeChan("/immutable-test-expired-with-Expires-header");
  var listener = new Listener();
  nextTest = doTest9;
  chan.loadFlags = Ci.nsIRequest.VALIDATE_ALWAYS;
  chan.asyncOpen(listener);
}

function doTest9() {
  dump(
    "execute doTest9 - expired resource with immutable cache extension and Last modified header. initial request\n"
  );
  do_test_pending();
  expectConditional = false;
  var chan = makeChan("/immutable-test-expired-with-last-modified-header");
  var listener = new Listener();
  nextTest = doTest10;
  chan.asyncOpen(listener);
}

function doTest10() {
  dump(
    "execute doTest10 - expired resource with immutable cache extension and Last modified heder. reload\n"
  );
  do_test_pending();
  expectConditional = true;
  var chan = makeChan("/immutable-test-expired-with-last-modified-header");
  var listener = new Listener();
  nextTest = testsDone;
  chan.loadFlags = Ci.nsIRequest.VALIDATE_ALWAYS;
  chan.asyncOpen(listener);
}
