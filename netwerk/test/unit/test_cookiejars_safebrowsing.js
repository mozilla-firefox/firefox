/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Description of the test:
 *   We show that we can separate the safebrowsing cookie by creating a custom
 *   OriginAttributes using a unique safebrowsing first-party domain. Setting this
 *   custom OriginAttributes on the loadInfo of the channel allows us to query the
 *   first-party domain and therefore separate the safebrowsing cookie in its own
 *   cookie-jar. For testing safebrowsing update we do >> NOT << emulate a response
 *   in the body, rather we only set the cookies in the header of the response
 *   and confirm that cookies are separated in their own cookie-jar.
 *
 * 1) We init safebrowsing and simulate an update (cookies are set for localhost)
 *
 * 2) We open a channel that should send regular cookies, but not the
 *    safebrowsing cookie.
 *
 * 3) We open a channel with a custom callback, simulating a safebrowsing cookie
 *    that should send this simulated safebrowsing cookie as well as the
 *    real safebrowsing cookies. (Confirming that the safebrowsing cookies
 *    actually get stored in the correct jar).
 */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

ChromeUtils.defineLazyGetter(this, "URL", function () {
  return "http://localhost:" + httpserver.identity.primaryPort;
});

var setCookiePath = "/setcookie";
var checkCookiePath = "/checkcookie";
var safebrowsingUpdatePath = "/safebrowsingUpdate";
var safebrowsingGethashPath = "/safebrowsingGethash";
var httpserver;

function inChildProcess() {
  return Services.appinfo.processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;
}

function cookieSetHandler(metadata, response) {
  var cookieName = metadata.getHeader("set-cookie");
  response.setStatusLine(metadata.httpVersion, 200, "Ok");
  response.setHeader("set-Cookie", cookieName + "=1; Path=/", false);
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write("Ok", "Ok".length);
}

function cookieCheckHandler(metadata, response) {
  var cookies = metadata.getHeader("Cookie");
  response.setStatusLine(metadata.httpVersion, 200, "Ok");
  response.setHeader("saw-cookies", cookies, false);
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write("Ok", "Ok".length);
}

function safebrowsingUpdateHandler(metadata, response) {
  var cookieName = "sb-update-cookie";
  response.setStatusLine(metadata.httpVersion, 200, "Ok");
  response.setHeader("set-Cookie", cookieName + "=1; Path=/", false);
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write("Ok", "Ok".length);
}

function safebrowsingGethashHandler(metadata, response) {
  var cookieName = "sb-gethash-cookie";
  response.setStatusLine(metadata.httpVersion, 200, "Ok");
  response.setHeader("set-Cookie", cookieName + "=1; Path=/", false);
  response.setHeader("Content-Type", "text/plain");

  let msg = "test-phish-simplea:1:32\n" + "a".repeat(32);
  response.bodyOutputStream.write(msg, msg.length);
}

function setupChannel(path, originAttributes) {
  var channel = NetUtil.newChannel({
    uri: URL + path,
    loadUsingSystemPrincipal: true,
  });
  channel.loadInfo.originAttributes = originAttributes;
  channel.QueryInterface(Ci.nsIHttpChannel);
  return channel;
}

function run_test() {
  // Set up a profile
  do_get_profile();

  // Allow all cookies if the pref service is available in this process.
  if (!inChildProcess()) {
    Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);
    Services.prefs.setBoolPref(
      "network.cookieJarSettings.unblocked_for_testing",
      true
    );
  }

  httpserver = new HttpServer();
  httpserver.registerPathHandler(setCookiePath, cookieSetHandler);
  httpserver.registerPathHandler(checkCookiePath, cookieCheckHandler);
  httpserver.registerPathHandler(
    safebrowsingUpdatePath,
    safebrowsingUpdateHandler
  );
  httpserver.registerPathHandler(
    safebrowsingGethashPath,
    safebrowsingGethashHandler
  );

  httpserver.start(-1);
  run_next_test();
}

// this test does not emulate a response in the body,
// rather we only set the cookies in the header of response.
add_test(function test_safebrowsing_update() {
  var streamUpdater = Cc[
    "@mozilla.org/url-classifier/streamupdater;1"
  ].getService(Ci.nsIUrlClassifierStreamUpdater);

  function onSuccess() {
    run_next_test();
  }
  function onUpdateError() {
    do_throw("ERROR: received onUpdateError!");
  }
  function onDownloadError() {
    do_throw("ERROR: received onDownloadError!");
  }

  streamUpdater.downloadUpdates(
    "test-phish-simple,test-malware-simple",
    "",
    "",
    true,
    "test",
    URL + safebrowsingUpdatePath,
    onSuccess,
    onUpdateError,
    onDownloadError
  );
});

add_test(function test_safebrowsing_gethash() {
  var hashCompleter = Cc[
    "@mozilla.org/url-classifier/hashcompleter;1"
  ].getService(Ci.nsIUrlClassifierHashCompleter);

  hashCompleter.complete(
    "aaaa",
    URL + safebrowsingGethashPath,
    "test-phish-simple",
    false,
    {
      completionV2() {},

      completionFinished(status) {
        Assert.equal(status, Cr.NS_OK);
        run_next_test();
      },
    }
  );
});

add_test(function test_non_safebrowsing_cookie() {
  var cookieName = "regCookie_id0";
  var originAttributes = new OriginAttributes(0, false, 0);

  function setNonSafeBrowsingCookie() {
    var channel = setupChannel(setCookiePath, originAttributes);
    channel.setRequestHeader("set-cookie", cookieName, false);
    channel.asyncOpen(new ChannelListener(checkNonSafeBrowsingCookie, null));
  }

  function checkNonSafeBrowsingCookie() {
    var channel = setupChannel(checkCookiePath, originAttributes);
    channel.asyncOpen(
      new ChannelListener(completeCheckNonSafeBrowsingCookie, null)
    );
  }

  function completeCheckNonSafeBrowsingCookie(request) {
    // Confirm that only the >> ONE << cookie is sent over the channel.
    var expectedCookie = cookieName + "=1";
    request.QueryInterface(Ci.nsIHttpChannel);
    var cookiesSeen = request.getResponseHeader("saw-cookies");
    Assert.equal(cookiesSeen, expectedCookie);
    run_next_test();
  }

  setNonSafeBrowsingCookie();
});

add_test(function test_safebrowsing_cookie() {
  var cookieName = "sbCookie_id4294967294";
  var originAttributes = new OriginAttributes(0, false, 0);
  originAttributes.firstPartyDomain =
    "safebrowsing.86868755-6b82-4842-b301-72671a0db32e.mozilla";

  function setSafeBrowsingCookie() {
    var channel = setupChannel(setCookiePath, originAttributes);
    channel.setRequestHeader("set-cookie", cookieName, false);
    channel.asyncOpen(new ChannelListener(checkSafeBrowsingCookie, null));
  }

  function checkSafeBrowsingCookie() {
    var channel = setupChannel(checkCookiePath, originAttributes);
    channel.asyncOpen(
      new ChannelListener(completeCheckSafeBrowsingCookie, null)
    );
  }

  function completeCheckSafeBrowsingCookie(request) {
    // Confirm that all >> THREE << cookies are sent back over the channel:
    //   a) the safebrowsing cookie set when updating
    //   b) the safebrowsing cookie set when sending gethash
    //   c) the regular cookie with custom loadcontext defined in this test.
    var expectedCookies = "sb-update-cookie=1; ";
    expectedCookies += "sb-gethash-cookie=1; ";
    expectedCookies += cookieName + "=1";
    request.QueryInterface(Ci.nsIHttpChannel);
    var cookiesSeen = request.getResponseHeader("saw-cookies");

    Assert.equal(cookiesSeen, expectedCookies);
    httpserver.stop(do_test_finished);
  }

  setSafeBrowsingCookie();
});
