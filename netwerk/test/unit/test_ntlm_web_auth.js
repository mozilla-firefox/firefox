// Unit tests for a NTLM authenticated web server.
//
// Currently the tests do not determine whether the Authentication dialogs have
// been displayed.
//

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "URL", function () {
  return "http://localhost:" + httpserver.identity.primaryPort;
});

function AuthPrompt() {}

AuthPrompt.prototype = {
  user: "guest",
  pass: "guest",

  QueryInterface: ChromeUtils.generateQI(["nsIAuthPrompt2"]),

  promptAuth: function ap_promptAuth(channel, level, authInfo) {
    authInfo.username = this.user;
    authInfo.password = this.pass;

    return true;
  },

  asyncPromptAuth: function ap_async() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
};

function Requestor() {}

Requestor.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIInterfaceRequestor"]),

  getInterface: function requestor_gi(iid) {
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      // Allow the prompt to store state by caching it here
      if (!this.prompt) {
        this.prompt = new AuthPrompt();
      }
      return this.prompt;
    }

    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  },

  prompt: null,
};

function makeChan(url, loadingUrl) {
  var principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(loadingUrl),
    {}
  );
  return NetUtil.newChannel({
    uri: url,
    loadingPrincipal: principal,
    securityFlags: Ci.nsILoadInfo.SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
  });
}

function TestListener() {}
TestListener.prototype.onStartRequest = function (request) {
  // Need to do the instanceof to allow request.responseStatus
  // to be read.
  if (!(request instanceof Ci.nsIHttpChannel)) {
    dump("Expecting an HTTP channel");
  }

  Assert.equal(expectedResponse, request.responseStatus, "HTTP Status code");
};
TestListener.prototype.onStopRequest = function () {
  Assert.equal(expectedRequests, requestsMade, "Number of requests made ");

  if (current_test < tests.length - 1) {
    current_test++;
    tests[current_test]();
  } else {
    do_test_pending();
    httpserver.stop(do_test_finished);
  }

  do_test_finished();
};
TestListener.prototype.onDataAvaiable = function (
  request,
  context,
  stream,
  offset,
  count
) {
  read_stream(stream, count);
};

// NTLM Messages, for the received type 1 and 3 messages only check that they
// are of the expected type.
const NTLM_TYPE1_PREFIX = "NTLM TlRMTVNTUAABAAAA";
const NTLM_TYPE2_PREFIX = "NTLM TlRMTVNTUAACAAAA";
const NTLM_TYPE3_PREFIX = "NTLM TlRMTVNTUAADAAAA";
const NTLM_PREFIX_LEN = 21;

const NTLM_CHALLENGE =
  NTLM_TYPE2_PREFIX +
  "DAAMADAAAAAVgongASNFZ4mrze8AAAAAAAAAAGIAYgA8AAAAR" +
  "ABPAE0AQQBJAE4AAgAMAEQATwBNAEEASQBOAAEADABTAEUAUg" +
  "BWAEUAUgAEABQAZABvAG0AYQBpAG4ALgBjAG8AbQADACIAcwB" +
  "lAHIAdgBlAHIALgBkAG8AbQBhAGkAbgAuAGMAbwBtAAAAAAA=";

// Web server responses for the happy path scenario.
// i.e. successful web server auth
//
function successfulAuth(metadata, response) {
  let authorization;
  let authPrefix;
  switch (requestsMade) {
    case 0:
      // Web Server - Initial request
      // Will respond with a 401 to start web server auth sequence
      response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
      response.setHeader("WWW-Authenticate", "NTLM", false);
      break;
    case 1:
      // Web Server - Expecting a type 1 negotiate message from the client
      authorization = metadata.getHeader("Authorization");
      authPrefix = authorization.substring(0, NTLM_PREFIX_LEN);
      Assert.equal(NTLM_TYPE1_PREFIX, authPrefix, "Expecting a Type 1 message");
      response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
      response.setHeader("WWW-Authenticate", NTLM_CHALLENGE, false);
      break;
    case 2:
      // Web Server - Expecting a type 3 Authenticate message from the client
      authorization = metadata.getHeader("Authorization");
      authPrefix = authorization.substring(0, NTLM_PREFIX_LEN);
      Assert.equal(NTLM_TYPE3_PREFIX, authPrefix, "Expecting a Type 3 message");
      response.setStatusLine(metadata.httpVersion, 200, "Successful");
      break;
    default:
      // We should be authenticated and further requests are permitted
      authorization = metadata.getHeader("Authorization");
      Assert.isnull(authorization);
      response.setStatusLine(metadata.httpVersion, 200, "Successful");
  }
  requestsMade++;
}

// web server responses simulating an unsuccessful web server auth
function failedAuth(metadata, response) {
  let authorization;
  let authPrefix;
  switch (requestsMade) {
    case 0:
      // Web Server - First request return a 401 to start auth sequence
      response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
      response.setHeader("WWW-Authenticate", "NTLM", false);
      break;
    case 1:
      // Web Server - Expecting a type 1 negotiate message from the client
      authorization = metadata.getHeader("Authorization");
      authPrefix = authorization.substring(0, NTLM_PREFIX_LEN);
      Assert.equal(NTLM_TYPE1_PREFIX, authPrefix, "Expecting a Type 1 message");
      response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
      response.setHeader("WWW-Authenticate", NTLM_CHALLENGE, false);
      break;
    case 2:
      // Web Server - Expecting a type 3 Authenticate message from the client
      // Respond with a 401 to restart the auth sequence.
      authorization = metadata.getHeader("Authorization");
      authPrefix = authorization.substring(0, NTLM_PREFIX_LEN);
      Assert.equal(NTLM_TYPE3_PREFIX, authPrefix, "Expecting a Type 1 message");
      response.setStatusLine(metadata.httpVersion, 401, "Unauthorized");
      break;
    default:
      // We should not get called past step 2
      // Strictly speaking the connection should not be used again
      // commented out for testing
      // dump( "ERROR: NTLM Auth failed connection should not be reused");
      //Assert.fail();
      response.setHeader("WWW-Authenticate", "NTLM", false);
  }
  requestsMade++;
}

var tests = [test_happy_path, test_failed_auth];
var current_test = 0;

var httpserver = null;
function run_test() {
  httpserver = new HttpServer();
  httpserver.start(-1);

  tests[0]();
}

var expectedRequests = 0; // Number of HTTP requests that are expected
var requestsMade = 0; // The number of requests that were made
var expectedResponse = 0; // The response code
// Note that any test failures in the HTTP handler
// will manifest as a 500 response code

// Common test setup
// Parameters:
//    path       - path component of the URL
//    handler    - http handler function for the httpserver
//    requests   - expected number oh http requests
//    response   - expected http response code
//    clearCache - clear the authentication cache before running the test
function setupTest(path, handler, requests, response, clearCache) {
  requestsMade = 0;
  expectedRequests = requests;
  expectedResponse = response;

  // clear the auth cache if requested
  if (clearCache) {
    dump("Clearing auth cache");
    Cc["@mozilla.org/network/http-auth-manager;1"]
      .getService(Ci.nsIHttpAuthManager)
      .clearAll();
  }

  var chan = makeChan(URL + path, URL);
  httpserver.registerPathHandler(path, handler);
  chan.notificationCallbacks = new Requestor();
  chan.asyncOpen(new TestListener());

  return chan;
}

// Happy code path
// Succesful web server auth.
function test_happy_path() {
  dump("RUNNING TEST: test_happy_path");
  setupTest("/auth", successfulAuth, 3, 200, 1);

  do_test_pending();
}

// Unsuccessful web server sign on
function test_failed_auth() {
  dump("RUNNING TEST: test_failed_auth");
  setupTest("/auth", failedAuth, 3, 401, 1);

  do_test_pending();
}
