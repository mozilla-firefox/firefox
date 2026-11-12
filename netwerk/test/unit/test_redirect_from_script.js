/*
 * Test whether the rewrite-requests-from-script API implemented here:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=765934 is functioning
 * correctly
 *
 * The test has the following components:
 *
 * testViaXHR() checks that internal redirects occur correctly for requests
 * made with XMLHttpRequest objects.
 *
 * testViaAsyncOpen() checks that internal redirects occur correctly when made
 * with nsIHTTPChannel.asyncOpen().
 *
 * Both of the above functions tests four requests:
 *
 * Test 1: a simple case that redirects within a server;
 * Test 2: a second that redirects to a second webserver;
 * Test 3: internal script redirects in response to a server-side 302 redirect;
 * Test 4: one internal script redirects in response to another's redirect.
 *
 * The successful redirects are confirmed by the presence of a custom response
 * header.
 *
 */
"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

// the topic we observe to use the API.  http-on-opening-request might also
// work for some purposes.
let redirectHook = "http-on-modify-request";

var httpServer = null,
  httpServer2 = null;

ChromeUtils.defineLazyGetter(this, "port1", function () {
  return httpServer.identity.primaryPort;
});

ChromeUtils.defineLazyGetter(this, "port2", function () {
  return httpServer2.identity.primaryPort;
});

// Test Part 1: a cross-path redirect on a single HTTP server
// http://localhost:port1/bait -> http://localhost:port1/switch
var baitPath = "/bait";
ChromeUtils.defineLazyGetter(this, "baitURI", function () {
  return "http://localhost:" + port1 + baitPath;
});
var baitText = "you got the worm";

var redirectedPath = "/switch";
ChromeUtils.defineLazyGetter(this, "redirectedURI", function () {
  return "http://localhost:" + port1 + redirectedPath;
});
var redirectedText = "worms are not tasty";

// Test Part 2: Now, a redirect to a different server
// http://localhost:port1/bait2 -> http://localhost:port2/switch
var bait2Path = "/bait2";
ChromeUtils.defineLazyGetter(this, "bait2URI", function () {
  return "http://localhost:" + port1 + bait2Path;
});

ChromeUtils.defineLazyGetter(this, "redirected2URI", function () {
  return "http://localhost:" + port2 + redirectedPath;
});

// Test Part 3, begin with a serverside redirect that itself turns into an instance
// of Test Part 1
var bait3Path = "/bait3";
ChromeUtils.defineLazyGetter(this, "bait3URI", function () {
  return "http://localhost:" + port1 + bait3Path;
});

// Test Part 4, begin with this client-side redirect and which then redirects
// to an instance of Test Part 1
var bait4Path = "/bait4";
ChromeUtils.defineLazyGetter(this, "bait4URI", function () {
  return "http://localhost:" + port1 + bait4Path;
});

var testHeaderName = "X-Redirected-By-Script";
var testHeaderVal = "Success";
var testHeaderVal2 = "Success on server 2";

function make_channel(url) {
  return NetUtil.newChannel({ uri: url, loadUsingSystemPrincipal: true });
}

function baitHandler(metadata, response) {
  // Content-Type required: https://bugzilla.mozilla.org/show_bug.cgi?id=748117
  response.setHeader("Content-Type", "text/html", false);
  response.bodyOutputStream.write(baitText, baitText.length);
}

function redirectedHandler(metadata, response) {
  response.setHeader("Content-Type", "text/html", false);
  response.bodyOutputStream.write(redirectedText, redirectedText.length);
  response.setHeader(testHeaderName, testHeaderVal);
}

function redirected2Handler(metadata, response) {
  response.setHeader("Content-Type", "text/html", false);
  response.bodyOutputStream.write(redirectedText, redirectedText.length);
  response.setHeader(testHeaderName, testHeaderVal2);
}

function bait3Handler(metadata, response) {
  response.setHeader("Content-Type", "text/html", false);
  response.setStatusLine(metadata.httpVersion, 302, "Found");
  response.setHeader("Location", baitURI);
}

function inChildProcess() {
  return Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;
}

// Maps the spec of a request URI to the URI it gets redirected to.
function Redirector(redirects) {
  this.redirects = redirects;
  this.register();
}

Redirector.prototype = {
  // This class observes an event and uses that to
  // trigger a redirectTo(uri) redirect using the new API
  register() {
    Services.obs.addObserver(this, redirectHook);
  },

  QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

  observe(subject, topic) {
    if (topic == redirectHook) {
      if (!(subject instanceof Ci.nsIHttpChannel)) {
        do_throw(redirectHook + " observed a non-HTTP channel");
      }
      var channel = subject.QueryInterface(Ci.nsIHttpChannel);
      var target = this.redirects[channel.URI.spec];
      // if we have a target, redirect there
      if (target) {
        var tURI = Services.io.newURI(target);
        try {
          channel.redirectTo(tURI);
        } catch (e) {
          do_throw("Exception in redirectTo " + e + "\n");
        }
      }
    }
  },
};

function makeAsyncTest(uri, headerValue, nextTask) {
  // Make a test to check a redirect that is created with channel.asyncOpen()

  // Produce a callback function which checks for the presence of headerValue,
  // and then continues to the next async test task
  var verifier = function (req, buffer) {
    if (!(req instanceof Ci.nsIHttpChannel)) {
      do_throw(req + " is not an nsIHttpChannel, catastrophe imminent!");
    }

    var httpChannel = req.QueryInterface(Ci.nsIHttpChannel);
    Assert.equal(httpChannel.getResponseHeader(testHeaderName), headerValue);
    Assert.equal(buffer, redirectedText);
    nextTask();
  };

  // Produce a function to run an asyncOpen test using the above verifier
  var test = function () {
    var chan = make_channel(uri);
    chan.asyncOpen(new ChannelListener(verifier));
  };
  return test;
}

// will be defined in run_test because of the lazy getters,
// since the server's port is defined dynamically
var testViaAsyncOpen4 = null;
var testViaAsyncOpen3 = null;
var testViaAsyncOpen2 = null;
var testViaAsyncOpen = null;

async function testViaXHR() {
  await runXHRTest(baitURI, testHeaderVal);
  await runXHRTest(bait2URI, testHeaderVal2);
  await runXHRTest(bait3URI, testHeaderVal);
  await runXHRTest(bait4URI, testHeaderVal);
}

async function runXHRTest(uri, headerValue) {
  // Check that making an XHR request for uri winds up redirecting to a result with the
  // appropriate headerValue
  var req = new XMLHttpRequest();
  req.open("GET", uri);
  await new Promise(resolve => {
    req.onloadend = resolve;
    req.send();
  });
  Assert.equal(req.getResponseHeader(testHeaderName), headerValue);
  Assert.equal(req.response, redirectedText);
}

add_task(async function test_redirect_from_script() {
  httpServer = new HttpServer();
  httpServer.registerPathHandler(baitPath, baitHandler);
  httpServer.registerPathHandler(bait2Path, baitHandler);
  httpServer.registerPathHandler(bait3Path, bait3Handler);
  httpServer.registerPathHandler(bait4Path, baitHandler);
  httpServer.registerPathHandler(redirectedPath, redirectedHandler);
  httpServer.start(-1);
  httpServer2 = new HttpServer();
  httpServer2.registerPathHandler(redirectedPath, redirected2Handler);
  httpServer2.start(-1);

  const redirects = {
    [baitURI]: redirectedURI,
    [bait2URI]: redirected2URI,
    [bait4URI]: baitURI,
  };
  // http-on-* observers only work in the parent process.
  if (inChildProcess()) {
    do_send_remote_message("register-redirector", redirects);
    await do_await_remote_message("register-redirector-done");
  } else {
    new Redirector(redirects);
  }

  await testViaXHR();

  await new Promise(done => {
    // The tests depend on each other, and therefore need to be defined in the
    // reverse of the order they are called in.  It is therefore best to read
    // this stanza backwards!
    testViaAsyncOpen4 = makeAsyncTest(bait4URI, testHeaderVal, done);
    testViaAsyncOpen3 = makeAsyncTest(
      bait3URI,
      testHeaderVal,
      testViaAsyncOpen4
    );
    testViaAsyncOpen2 = makeAsyncTest(
      bait2URI,
      testHeaderVal2,
      testViaAsyncOpen3
    );
    testViaAsyncOpen = makeAsyncTest(baitURI, testHeaderVal, testViaAsyncOpen2);

    testViaAsyncOpen();
  });

  await httpServer.stop();
  await httpServer2.stop();
});
