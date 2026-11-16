function dumpn(s) {
  dump(s + "\n");
}

const NS_APP_USER_PROFILE_50_DIR = "ProfD";
const NS_APP_USER_PROFILE_LOCAL_50_DIR = "ProfLD";

var {
  HTTP_400,
  HTTP_401,
  HTTP_402,
  HTTP_403,
  HTTP_404,
  HTTP_405,
  HTTP_406,
  HTTP_407,
  HTTP_408,
  HTTP_409,
  HTTP_410,
  HTTP_411,
  HTTP_412,
  HTTP_413,
  HTTP_414,
  HTTP_415,
  HTTP_417,
  HTTP_500,
  HTTP_501,
  HTTP_502,
  HTTP_503,
  HTTP_504,
  HTTP_505,
  HttpError,
  HttpServer,
} = ChromeUtils.importESModule("resource://testing-common/httpd.sys.mjs");

do_get_profile(true);

// Ensure PSM is initialized before the test
Cc["@mozilla.org/psm;1"].getService(Ci.nsISupports);

// Disable hashcompleter noise for tests
Services.prefs.setIntPref("urlclassifier.gethashnoise", 0);

// Enable malware/phishing checking for tests
Services.prefs.setBoolPref("browser.safebrowsing.malware.enabled", true);
Services.prefs.setBoolPref("browser.safebrowsing.blockedURIs.enabled", true);
Services.prefs.setBoolPref("browser.safebrowsing.phishing.enabled", true);
Services.prefs.setBoolPref(
  "browser.safebrowsing.provider.test.disableBackoff",
  true
);

// Add testing tables, we don't use moztest-* here because it doesn't support update
Services.prefs.setCharPref("urlclassifier.phishTable", "test-phish-simple");
Services.prefs.setCharPref(
  "urlclassifier.malwareTable",
  "test-harmful-simple,test-malware-simple,test-unwanted-simple"
);
Services.prefs.setCharPref("urlclassifier.blockedTable", "test-block-simple");
Services.prefs.setCharPref("urlclassifier.trackingTable", "test-track-simple");
Services.prefs.setCharPref(
  "urlclassifier.trackingWhitelistTable",
  "test-trackwhite-simple"
);

// Enable all completions for tests
Services.prefs.setCharPref("urlclassifier.disallow_completions", "");

// Hash completion timeout
Services.prefs.setIntPref("urlclassifier.gethash.timeout_ms", 5000);

function delFile(name) {
  try {
    // Delete a previously created sqlite file
    var file = Services.dirsvc.get("ProfLD", Ci.nsIFile);
    file.append(name);
    if (file.exists()) {
      file.remove(false);
    }
  } catch (e) {}
}

function cleanUp() {
  delFile("urlclassifier3.sqlite");
  delFile("safebrowsing/classifier.hashkey");
  delFile("safebrowsing/test-phish-simple.sbstore");
  delFile("safebrowsing/test-malware-simple.sbstore");
  delFile("safebrowsing/test-unwanted-simple.sbstore");
  delFile("safebrowsing/test-block-simple.sbstore");
  delFile("safebrowsing/test-harmful-simple.sbstore");
  delFile("safebrowsing/test-track-simple.sbstore");
  delFile("safebrowsing/test-trackwhite-simple.sbstore");
  delFile("safebrowsing/test-phish-simple.pset");
  delFile("safebrowsing/test-malware-simple.pset");
  delFile("safebrowsing/test-unwanted-simple.pset");
  delFile("safebrowsing/test-block-simple.pset");
  delFile("safebrowsing/test-harmful-simple.pset");
  delFile("safebrowsing/test-track-simple.pset");
  delFile("safebrowsing/test-trackwhite-simple.pset");
  delFile("safebrowsing/moz-phish-simple.sbstore");
  delFile("safebrowsing/moz-phish-simple.pset");
  delFile("testLarge.pset");
  delFile("testNoDelta.pset");
}

// Update uses allTables by default
var allTables =
  "test-phish-simple,test-malware-simple,test-unwanted-simple,test-track-simple,test-trackwhite-simple,test-block-simple";
var mozTables = "moz-phish-simple";

var dbservice = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
  Ci.nsIUrlClassifierDBService
);
var streamUpdater = Cc[
  "@mozilla.org/url-classifier/streamupdater;1"
].getService(Ci.nsIUrlClassifierStreamUpdater);

/*
 * Builds an update from an object that looks like:
 *{ "test-phish-simple" : [{
 *    "chunkType" : "a",  // 'a' is assumed if not specified
 *    "chunkNum" : 1,     // numerically-increasing chunk numbers are assumed
 *                        // if not specified
 *    "urls" : [ "foo.com/a", "foo.com/b", "bar.com/" ]
 * }
 */

function buildUpdate(update, hashSize) {
  if (!hashSize) {
    hashSize = 32;
  }
  var updateStr = "n:1000\n";

  for (var tableName in update) {
    if (tableName != "") {
      updateStr += "i:" + tableName + "\n";
    }
    var chunks = update[tableName];
    for (var j = 0; j < chunks.length; j++) {
      var chunk = chunks[j];
      var chunkType = chunk.chunkType ? chunk.chunkType : "a";
      var chunkNum = chunk.chunkNum ? chunk.chunkNum : j;
      updateStr += chunkType + ":" + chunkNum + ":" + hashSize;

      if (chunk.urls) {
        var chunkData = chunk.urls.join("\n");
        updateStr += ":" + chunkData.length + "\n" + chunkData;
      }

      updateStr += "\n";
    }
  }

  return updateStr;
}

function buildPhishingUpdate(chunks, hashSize) {
  return buildUpdate({ "test-phish-simple": chunks }, hashSize);
}

function buildMalwareUpdate(chunks, hashSize) {
  return buildUpdate({ "test-malware-simple": chunks }, hashSize);
}

function buildUnwantedUpdate(chunks, hashSize) {
  return buildUpdate({ "test-unwanted-simple": chunks }, hashSize);
}

function buildBlockedUpdate(chunks, hashSize) {
  return buildUpdate({ "test-block-simple": chunks }, hashSize);
}

function buildMozPhishingUpdate(chunks, hashSize) {
  return buildUpdate({ "moz-phish-simple": chunks }, hashSize);
}

function buildBareUpdate(chunks, hashSize) {
  return buildUpdate({ "": chunks }, hashSize);
}

/**
 * Performs an update of the dbservice manually, bypassing the stream updater
 */
function doSimpleUpdate(updateText, success, failure) {
  var listener = {
    QueryInterface: ChromeUtils.generateQI(["nsIUrlClassifierUpdateObserver"]),

    updateUrlRequested() {},
    streamFinished() {},
    updateError(errorCode) {
      failure(errorCode);
    },
    updateSuccess(tables, waitSeconds) {
      success(tables, waitSeconds);
    },
  };

  dbservice.beginUpdate(listener, allTables, "");
  dbservice.beginStream("", "");
  dbservice.updateStream(updateText);
  dbservice.finishStream();
  dbservice.finishUpdate();
}

/**
 * Simulates a failed database update.
 */
function doErrorUpdate(tables, success, failure) {
  var listener = {
    QueryInterface: ChromeUtils.generateQI(["nsIUrlClassifierUpdateObserver"]),

    updateUrlRequested() {},
    streamFinished() {},
    updateError(errorCode) {
      success(errorCode);
    },
    updateSuccess(tables, waitSeconds) {
      failure(tables, waitSeconds);
    },
  };

  dbservice.beginUpdate(listener, tables, "");
  dbservice.beginStream("", "");
  dbservice.cancelUpdate();
}

/**
 * Performs an update of the dbservice using the stream updater and a
 * data: uri
 */
function doStreamUpdate(updateText, success, failure, downloadFailure) {
  var dataUpdate = "data:," + encodeURIComponent(updateText);

  if (!downloadFailure) {
    downloadFailure = failure;
  }

  streamUpdater.downloadUpdates(
    allTables,
    "",
    "",
    true,
    "test",
    dataUpdate,
    success,
    failure,
    downloadFailure
  );
}

var gAssertions = {
  tableData(expectedTables, cb) {
    dbservice.getTables(function (tables) {
      // rebuild the tables in a predictable order.
      var parts = tables.split("\n");
      while (parts[parts.length - 1] == "") {
        parts.pop();
      }
      parts.sort();
      tables = parts.join("\n");

      Assert.equal(tables, expectedTables);
      cb();
    });
  },

  checkUrls(urls, expected, cb, useMoz = false) {
    // work with a copy of the list.
    urls = urls.slice(0);
    var doLookup = function () {
      if (urls.length) {
        var tables = useMoz ? mozTables : allTables;
        var fragment = urls.shift();
        var principal = Services.scriptSecurityManager.createContentPrincipal(
          Services.io.newURI("http://" + fragment),
          {}
        );
        dbservice.lookup(
          principal,
          tables,
          function (arg) {
            Assert.equal(expected, arg);
            doLookup();
          },
          true
        );
      } else {
        cb();
      }
    };
    doLookup();
  },

  checkTables(url, expected, cb) {
    var principal = Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("http://" + url),
      {}
    );
    dbservice.lookup(
      principal,
      allTables,
      function (tables) {
        // Rebuild tables in a predictable order.
        var parts = tables.split(",");
        while (parts[parts.length - 1] == "") {
          parts.pop();
        }
        parts.sort();
        tables = parts.join(",");
        Assert.equal(tables, expected);
        cb();
      },
      true
    );
  },

  urlsDontExist(urls, cb) {
    this.checkUrls(urls, "", cb);
  },

  urlsExist(urls, cb) {
    this.checkUrls(urls, "test-phish-simple", cb);
  },

  malwareUrlsExist(urls, cb) {
    this.checkUrls(urls, "test-malware-simple", cb);
  },

  unwantedUrlsExist(urls, cb) {
    this.checkUrls(urls, "test-unwanted-simple", cb);
  },

  blockedUrlsExist(urls, cb) {
    this.checkUrls(urls, "test-block-simple", cb);
  },

  mozPhishingUrlsExist(urls, cb) {
    this.checkUrls(urls, "moz-phish-simple", cb, true);
  },

  subsDontExist(urls, cb) {
    // XXX: there's no interface for checking items in the subs table
    cb();
  },

  subsExist(urls, cb) {
    // XXX: there's no interface for checking items in the subs table
    cb();
  },

  urlExistInMultipleTables(data, cb) {
    this.checkTables(data.url, data.tables, cb);
  },
};

/**
 * Check a set of assertions against the gAssertions table.
 */
function checkAssertions(assertions, doneCallback) {
  var checkAssertion = function () {
    for (var i in assertions) {
      var data = assertions[i];
      delete assertions[i];
      gAssertions[i](data, checkAssertion);
      return;
    }

    doneCallback();
  };

  checkAssertion();
}

function updateError(arg) {
  do_throw(arg);
}

/**
 * Utility functions
 */
ChromeUtils.defineESModuleGetters(this, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
});

function readFileToString(aFilename) {
  let f = do_get_file(aFilename);
  let stream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  stream.init(f, -1, 0, 0);
  let buf = NetUtil.readInputStreamToString(stream, stream.available());
  return buf;
}

// Runs a set of updates, and then checks a set of assertions.
function doUpdateTest(updates, assertions, successCallback, errorCallback) {
  var errorUpdate = function () {
    checkAssertions(assertions, errorCallback);
  };

  var runUpdate = function () {
    if (updates.length) {
      var update = updates.shift();
      doStreamUpdate(update, runUpdate, errorUpdate, null);
    } else {
      checkAssertions(assertions, successCallback);
    }
  };

  runUpdate();
}

var gTests;
var gNextTest = 0;

function runNextTest() {
  if (gNextTest >= gTests.length) {
    do_test_finished();
    return;
  }

  dbservice.resetDatabase();
  dbservice.setHashCompleter("test-phish-simple", null);

  let test = gTests[gNextTest++];
  dump("running " + test.name + "\n");
  test();
}

function runTests(tests) {
  gTests = tests;
  runNextTest();
}

var timerArray = [];

function Timer(delay, cb) {
  this.cb = cb;
  var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(this, delay, timer.TYPE_ONE_SHOT);
  timerArray.push(timer);
}

Timer.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsITimerCallback"]),
  notify() {
    this.cb();
  },
};

// LFSRgenerator is a 32-bit linear feedback shift register random number
// generator. It is highly predictable and is not intended to be used for
// cryptography but rather to allow easier debugging than a test that uses
// Math.random().
function LFSRgenerator(seed) {
  // Force |seed| to be a number.
  seed = +seed;
  // LFSR generators do not work with a value of 0.
  if (seed == 0) {
    seed = 1;
  }

  this._value = seed;
}
LFSRgenerator.prototype = {
  // nextNum returns a random unsigned integer of in the range [0,2^|bits|].
  nextNum(bits) {
    if (!bits) {
      bits = 32;
    }

    let val = this._value;
    // Taps are 32, 22, 2 and 1.
    let bit = ((val >>> 0) ^ (val >>> 10) ^ (val >>> 30) ^ (val >>> 31)) & 1;
    val = (val >>> 1) | (bit << 31);
    this._value = val;

    return val >>> (32 - bits);
  },
};

function waitUntilMetaDataSaved(expectedState, expectedChecksum, callback) {
  let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"].getService(
    Ci.nsIUrlClassifierDBService
  );

  dbService.getTables(metaData => {
    info("metadata: " + metaData);
    let didCallback = false;
    metaData.split("\n").some(line => {
      // Parse [tableName];[stateBase64]
      let p = line.indexOf(";");
      if (-1 === p) {
        return false; // continue.
      }
      let tableName = line.substring(0, p);
      let metadata = line.substring(p + 1).split(":");
      let stateBase64 = metadata[0];
      let checksumBase64 = metadata[1];

      if (tableName !== "test-phish-proto") {
        return false; // continue.
      }

      if (
        stateBase64 === btoa(expectedState) &&
        checksumBase64 === btoa(expectedChecksum)
      ) {
        info("State has been saved to disk!");

        // We slightly defer the callback to see if the in-memory
        // |getTables| caching works correctly.
        dbService.getTables(cachedMetadata => {
          equal(cachedMetadata, metaData);
          callback();
        });

        // Even though we haven't done callback at this moment
        // but we still claim "we have" in order to stop repeating
        // a new timer.
        didCallback = true;
      }

      return true; // break no matter whether the state is matching.
    });

    if (!didCallback) {
      do_timeout(
        1000,
        waitUntilMetaDataSaved.bind(
          null,
          expectedState,
          expectedChecksum,
          callback
        )
      );
    }
  });
}

var gUpdateFinishedObserverEnabled = false;
var gUpdateFinishedObserver = function (aSubject, aTopic, aData) {
  info("[" + aTopic + "] " + aData);
  if (!aData.startsWith("success")) {
    updateError(aData);
  }
};

function throwOnUpdateErrors() {
  Services.obs.addObserver(
    gUpdateFinishedObserver,
    "safebrowsing-update-finished"
  );
  gUpdateFinishedObserverEnabled = true;
}

function stopThrowingOnUpdateErrors() {
  if (gUpdateFinishedObserverEnabled) {
    Services.obs.removeObserver(
      gUpdateFinishedObserver,
      "safebrowsing-update-finished"
    );
    gUpdateFinishedObserverEnabled = false;
  }
}

cleanUp();

registerCleanupFunction(function () {
  cleanUp();
});
