"use strict";

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

var h2Port;
var prefs;
var http2pref;
var altsvcpref1;

// https://foo.example.com:(h2Port)
// https://bar.example.com:(h2Port) <- invalid for bar, but ok for foo

var otherServer; // server socket listening for other connection.

var h2FooRoute; // foo.example.com:H2PORT
var h2BarRoute; // bar.example.com:H2PORT
var httpsFooOrigin; // https://foo.exmaple.com:PORT/
var httpsBarOrigin; // https://bar.example.com:PORT/

function run_test() {
  do_get_profile();
  do_test_pending();
  startServer().then(() => {
    setup();
    do_test_finished();
  });
}

async function startServer() {
  let server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.registerPathHandler("/altsvc-test", (req, res) => {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Alt-Svc", "h2=" + req.headers["x-altsvc"]);
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end("ok");
  });
  h2Port = server.port();
}

function setup() {
  prefs = Services.prefs;

  http2pref = prefs.getBoolPref("network.http.http2.enabled");
  altsvcpref1 = prefs.getBoolPref("network.http.altsvc.enabled");

  prefs.setBoolPref("network.http.http2.enabled", true);
  prefs.setBoolPref("network.http.altsvc.enabled", true);
  prefs.setCharPref(
    "network.dns.localDomains",
    "foo.example.com, bar.example.com"
  );

  // The server cert is for foo.example.com and is signed by http2-ca.pem, which
  // NodeHTTP2Server installs. The same cert is used for both h2FooRoute and
  // h2BarRoute though it is only valid for the foo.example.com domain name.
  h2FooRoute = "foo.example.com:" + h2Port;
  h2BarRoute = "bar.example.com:" + h2Port;

  httpsFooOrigin = "https://" + h2FooRoute + "/";
  httpsBarOrigin = "https://" + h2BarRoute + "/";
  dump(
    "https foo - " +
      httpsFooOrigin +
      "\n" +
      "https bar - " +
      httpsBarOrigin +
      "\n"
  );

  doTest1();
}

function resetPrefs() {
  prefs.setBoolPref("network.http.http2.enabled", http2pref);
  prefs.setBoolPref("network.http.altsvc.enabled", altsvcpref1);
  prefs.clearUserPref("network.dns.localDomains");
  prefs.clearUserPref("network.security.ports.banned");
}

function makeChan(origin) {
  return NetUtil.newChannel({
    uri: origin + "altsvc-test",
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
}

var origin;
var xaltsvc;
var loadWithoutClearingMappings = false;
var nextTest;
var expectPass = true;
var waitFor = 0;

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

  onStopRequest: function testOnStopRequest(request, status) {
    var routed = "";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    // When a direct connection wins the Happy Eyeballs race, the alt-svc route
    // is dropped by setting "Alt-Used: 0" (see RemoveAlternateServiceUsedHeader),
    // which means the same thing as an absent header here.
    if (routed == "0") {
      routed = "";
    }
    dump("routed is " + routed + "\n");
    Assert.equal(Components.isSuccessCode(status), expectPass);

    if (waitFor != 0) {
      Assert.equal(routed, "");
      do_test_pending();
      loadWithoutClearingMappings = true;
      // With Happy Eyeballs the alt-svc connection shares the origin's
      // ConnectionEntry, so the retry would otherwise reuse the origin
      // connection and be wrongly tagged as alt-svc-routed. Drop connections so
      // the retry re-resolves the alt-svc from scratch.
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      Services.dns.clearCache(true);
      do_timeout(waitFor, doTest);
      waitFor = 0;
      xaltsvc = "NA";
    } else if (xaltsvc == "NA") {
      Assert.equal(routed, "");
      nextTest();
    } else if (routed == xaltsvc) {
      Assert.equal(routed, xaltsvc); // always true, but a useful log
      nextTest();
    } else {
      dump("poll later for alt svc mapping\n");
      do_test_pending();
      loadWithoutClearingMappings = true;
      do_timeout(500, doTest);
    }

    do_test_finished();
  },
};

function testsDone() {
  dump("testDone\n");
  resetPrefs();
  do_test_pending();
  otherServer.close();
}

function doTest() {
  dump("execute doTest " + origin + "\n");
  var chan = makeChan(origin);
  var listener = new Listener();
  if (xaltsvc != "NA") {
    chan.setRequestHeader("x-altsvc", xaltsvc, false);
  }
  if (loadWithoutClearingMappings) {
    chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  } else {
    chan.loadFlags =
      Ci.nsIRequest.LOAD_FRESH_CONNECTION |
      Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  }
  loadWithoutClearingMappings = false;
  chan.asyncOpen(listener);
}

// xaltsvc is overloaded to do two things..
// 1] it is sent in the x-altsvc request header, and the response uses the value in the Alt-Svc response header
// 2] the test polls until necko sets Alt-Used to that value (i.e. it uses that route)
//
// When xaltsvc is set to h2Route (i.e. :port with the implied hostname) it doesn't match the alt-used,
// which is always explicit, so it needs to be changed after the channel is created but before the
// listener is invoked

// https://bar should fail because host bar has cert for foo
function doTest1() {
  dump("doTest1()\n");
  origin = httpsBarOrigin;
  xaltsvc = "";
  expectPass = false;
  nextTest = doTest2;
  do_test_pending();
  doTest();
}

// https://foo no alt-svc (just check cert setup)
function doTest2() {
  dump("doTest2()\n");
  origin = httpsFooOrigin;
  xaltsvc = "NA";
  expectPass = true;
  nextTest = doTest3;
  do_test_pending();
  doTest();
}

// https://foo via bar (bar has cert for foo)
function doTest3() {
  dump("doTest3()\n");
  origin = httpsFooOrigin;
  xaltsvc = h2BarRoute;
  nextTest = doTest4;
  do_test_pending();
  doTest();
}

// check again https://bar should fail because host bar has cert for foo
function doTest4() {
  dump("doTest4()\n");
  origin = httpsBarOrigin;
  xaltsvc = "";
  expectPass = false;
  nextTest = doTest5;
  do_test_pending();
  doTest();
}

// check again https://bar should fail because host bar has cert for foo
function doTest5() {
  dump("doTest5()\n");
  origin = httpsBarOrigin;
  xaltsvc = "";
  expectPass = false;
  nextTest = doTest6;
  do_test_pending();
  doTest();
}

// Check we don't connect to blocked ports
function doTest6() {
  dump("doTest6()\n");
  origin = httpsFooOrigin;
  expectPass = true;
  nextTest = testsDone;
  otherServer = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  otherServer.init(-1, true, -1);
  xaltsvc = "localhost:" + otherServer.port;
  Services.prefs.setCharPref(
    "network.security.ports.banned",
    "" + otherServer.port
  );
  dump("Blocked port: " + otherServer.port);
  waitFor = 500;
  otherServer.asyncListen({
    onSocketAccepted() {
      Assert.ok(false, "Got connection to socket when we didn't expect it!");
    },
    onStopListening() {
      do_test_finished();
    },
  });
  nextTest = doTest7;
  do_test_pending();
  doTest();
}

// Check we don't connect to blocked ports
function doTest7() {
  dump("doTest7()\n");
  origin = httpsFooOrigin;
  nextTest = testsDone;
  otherServer = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  const BAD_PORT_U32 = 6667 + 65536;
  otherServer.init(BAD_PORT_U32, true, -1);
  Assert.equal(otherServer.port, 6667, "Trying to listen on port 6667");
  xaltsvc = "localhost:" + BAD_PORT_U32;
  dump("Blocked port: " + otherServer.port);
  waitFor = 500;
  otherServer.asyncListen({
    onSocketAccepted() {
      Assert.ok(false, "Got connection to socket when we didn't expect it!");
    },
    onStopListening() {
      do_test_finished();
    },
  });
  nextTest = doTest8;
  do_test_pending();
  doTest();
}
function doTest8() {
  dump("doTest8()\n");
  origin = httpsFooOrigin;
  nextTest = testsDone;
  otherServer = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  const BAD_PORT_U64 = 6666 + 429496729;
  otherServer.init(6666, true, -1);
  Assert.equal(otherServer.port, 6666, "Trying to listen on port 6666");
  xaltsvc = "localhost:" + BAD_PORT_U64;
  dump("Blocked port: " + otherServer.port);
  waitFor = 500;
  otherServer.asyncListen({
    onSocketAccepted() {
      Assert.ok(false, "Got connection to socket when we didn't expect it!");
    },
    onStopListening() {
      do_test_finished();
    },
  });
  nextTest = doTest9;
  do_test_pending();
  doTest();
}
// Port 65535 should be OK
function doTest9() {
  dump("doTest9()\n");
  origin = httpsFooOrigin;
  nextTest = testsDone;
  otherServer = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  const GOOD_PORT = 65535;
  otherServer.init(65535, true, -1);
  Assert.equal(otherServer.port, 65535, "Trying to listen on port 65535");
  xaltsvc = "localhost:" + GOOD_PORT;
  dump("Allowed port: " + otherServer.port);
  waitFor = 500;
  otherServer.asyncListen({
    onSocketAccepted(socket, transport) {
      Assert.ok(true, "Got connection to socket");

      let out = transport.openOutputStream(Ci.nsITransport.OPEN_BLOCKING, 0, 0);
      out.write("not-tls\n", 8);
      out.close();
    },
    onStopListening() {
      do_test_finished();
    },
  });
  do_test_pending();
  doTest();
}
