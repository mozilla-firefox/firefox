"use strict";

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
var httpserver = new HttpServer();

// Test the handling of a cache revalidation with mismatching last-modified
// headers. If we get such a revalidation the cache entry should be purged.
// see bug 717350

// In this test the wrong data is from 11-16-1994 with a value of 'A',
// and the right data is from 11-15-1994 with a value of 'B'.

// the same URL is requested 3 times. the first time the wrong data comes
// back, the second time that wrong data is revalidated with a 304 but
// a L-M header of the right data (this triggers a cache purge), and
// the third time the right data is returned.

var listener_3 = {
  // this listener is used to process the the request made after
  // the cache invalidation. it expects to see the 'right data'

  QueryInterface: ChromeUtils.generateQI([
    "nsIStreamListener",
    "nsIRequestObserver",
  ]),

  onStartRequest: function test_onStartR() {},

  onDataAvailable: function test_ODA(request, inputStream, offset, count) {
    var data = new BinaryInputStream(inputStream).readByteArray(count);

    Assert.equal(data[0], "B".charCodeAt(0));
  },

  onStopRequest: function test_onStopR() {
    httpserver.stop(do_test_finished);
  },
};

ChromeUtils.defineLazyGetter(this, "listener_2", function () {
  return {
    // this listener is used to process the revalidation of the
    // corrupted cache entry. its revalidation prompts it to be cleaned

    QueryInterface: ChromeUtils.generateQI([
      "nsIStreamListener",
      "nsIRequestObserver",
    ]),

    onStartRequest: function test_onStartR() {},

    onDataAvailable: function test_ODA(request, inputStream, offset, count) {
      var data = new BinaryInputStream(inputStream).readByteArray(count);

      // This is 'A' from a cache revalidation, but that reval will clean the cache
      // because of mismatched last-modified response headers

      Assert.equal(data[0], "A".charCodeAt(0));
    },

    onStopRequest: function test_onStopR(request) {
      request.QueryInterface(Ci.nsIHttpChannel);
      var chan = NetUtil.newChannel({
        uri: "http://localhost:" + httpserver.identity.primaryPort + "/test1",
        loadUsingSystemPrincipal: true,
      });
      chan.asyncOpen(listener_3);
    },
  };
});

ChromeUtils.defineLazyGetter(this, "listener_1", function () {
  return {
    // this listener processes the initial request from a empty cache.
    // the server responds with the wrong data ('A')

    QueryInterface: ChromeUtils.generateQI([
      "nsIStreamListener",
      "nsIRequestObserver",
    ]),

    onStartRequest: function test_onStartR() {},

    onDataAvailable: function test_ODA(request, inputStream, offset, count) {
      var data = new BinaryInputStream(inputStream).readByteArray(count);
      Assert.equal(data[0], "A".charCodeAt(0));
    },

    onStopRequest: function test_onStopR(request) {
      request.QueryInterface(Ci.nsIHttpChannel);
      var chan = NetUtil.newChannel({
        uri: "http://localhost:" + httpserver.identity.primaryPort + "/test1",
        loadUsingSystemPrincipal: true,
      });
      chan.asyncOpen(listener_2);
    },
  };
});

function run_test() {
  do_get_profile();

  // On Windows, dooming the disk cache entry intermittently fails because its
  // file is locked by another process, which leaves the stale entry in place.
  if (mozinfo.os == "win") {
    Services.prefs.setBoolPref("browser.cache.disk.enable", false);
    registerCleanupFunction(() => {
      Services.prefs.clearUserPref("browser.cache.disk.enable");
    });
  }

  evict_cache_entries();

  httpserver.registerPathHandler("/test1", handler);
  httpserver.start(-1);

  var port = httpserver.identity.primaryPort;
  var chan = NetUtil.newChannel({
    uri: "http://localhost:" + port + "/test1",
    loadUsingSystemPrincipal: true,
  });
  chan.asyncOpen(listener_1);

  do_test_pending();
}

var iter = 0;
function handler(metadata, response) {
  iter++;
  if (metadata.hasHeader("If-Modified-Since")) {
    response.setStatusLine(metadata.httpVersion, 304, "Not Modified");
    response.setHeader("Last-Modified", "Tue, 15 Nov 1994 12:45:26 GMT", false);
  } else {
    response.setStatusLine(metadata.httpVersion, 200, "OK");
    response.setHeader("Cache-Control", "max-age=0", false);
    if (iter == 1) {
      // simulated wrong response
      response.setHeader(
        "Last-Modified",
        "Wed, 16 Nov 1994 00:00:00 GMT",
        false
      );
      response.bodyOutputStream.write("A", 1);
    }
    if (iter == 3) {
      // 'correct' response
      response.setHeader(
        "Last-Modified",
        "Tue, 15 Nov 1994 12:45:26 GMT",
        false
      );
      response.bodyOutputStream.write("B", 1);
    }
  }
}
