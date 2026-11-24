const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// These tables have a different update URL (for v4).
const TEST_TABLE_DATA_V4 = {
  tableName: "test-phish-proto",
  providerName: "google4",
  updateUrl: "http://localhost:5555/safebrowsing/update?",
  gethashUrl: "http://localhost:5555/safebrowsing/gethash-v4?",
};

const PREF_NEXTUPDATETIME_V4 =
  "browser.safebrowsing.provider.google4.nextupdatetime";
const GETHASH_PATH = "/safebrowsing/gethash-v4";

// The protobuf binary represention of gethash response:
// minimumWaitDuration : 12 secs 10 nanosecs
// negativeCacheDuration : 120 secs 9 nanosecs
//
// { CompleteHash, ThreatType, CacheDuration { secs, nanos } };
// { nsCString("01234567890123456789012345678901"), SOCIAL_ENGINEERING_PUBLIC, { 8, 500 } },
// { nsCString("12345678901234567890123456789012"), SOCIAL_ENGINEERING_PUBLIC, { 7, 100} },
// { nsCString("23456789012345678901234567890123"), SOCIAL_ENGINEERING_PUBLIC, { 1, 20 } },

const GETHASH_RESPONSE_CONTENT =
  "\x0A\x2D\x08\x02\x1A\x22\x0A\x20\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x2A\x05\x08\x08\x10\xF4\x03\x0A\x2C\x08\x02\x1A\x22\x0A\x20\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x2A\x04\x08\x07\x10\x64\x0A\x2C\x08\x02\x1A\x22\x0A\x20\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x30\x31\x32\x33\x2A\x04\x08\x01\x10\x14\x12\x04\x08\x0C\x10\x0A\x1A\x04\x08\x78\x10\x09";

// The protobuf binary represention of update response:
//
// [
//   {
//     'threat_type': 2, // SOCIAL_ENGINEERING_PUBLIC
//     'response_type': 2, // FULL_UPDATE
//     'new_client_state': 'sta\x00te', // NEW_CLIENT_STATE
//     'checksum': { "sha256": CHECKSUM }, // CHECKSUM
//     'additions': { 'compression_type': RAW,
//                    'prefix_size': 4,
//                    'raw_hashes': "00000001000000020000000300000004"}
//   }
// ]
//
const UPDATE_RESPONSE_CONTENT =
  "\x0A\x4A\x08\x02\x20\x02\x2A\x18\x08\x01\x12\x14\x08\x04\x12\x10\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x03\x3A\x06\x73\x74\x61\x00\x74\x65\x42\x22\x0A\x20\x30\x67\xC7\x2C\x5E\x50\x1C\x31\xE3\xFE\xCA\x73\xF0\x47\xDC\x34\x1A\x95\x63\x99\xEC\x70\x5E\x0A\xEE\x9E\xFB\x17\xA1\x55\x35\x78\x12\x08\x08\x08\x10\x80\x94\xEB\xDC\x03";
const UPDATE_PATH = "/safebrowsing/update";

let gListManager = Cc["@mozilla.org/url-classifier/listmanager;1"].getService(
  Ci.nsIUrlListManager
);

let gCompleter = Cc["@mozilla.org/url-classifier/hashcompleter;1"].getService(
  Ci.nsIUrlClassifierHashCompleter
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "gUrlUtil",
  "@mozilla.org/url-classifier/utils;1",
  Ci.nsIUrlClassifierUtils
);

// Handles request for TEST_TABLE_DATA_V4.
let gHttpServV4 = null;

const NEW_CLIENT_STATE = "sta\0te";
const CHECKSUM =
  "\x30\x67\xc7\x2c\x5e\x50\x1c\x31\xe3\xfe\xca\x73\xf0\x47\xdc\x34\x1a\x95\x63\x99\xec\x70\x5e\x0a\xee\x9e\xfb\x17\xa1\x55\x35\x78";

Services.prefs.setBoolPref("browser.safebrowsing.debug", true);

// The "\xFF\xFF" is to generate a base64 string with "/".
Services.prefs.setCharPref("browser.safebrowsing.id", "Firefox\xFF\xFF");

// Register tables.
gListManager.registerTable(
  TEST_TABLE_DATA_V4.tableName,
  TEST_TABLE_DATA_V4.providerName,
  TEST_TABLE_DATA_V4.updateUrl,
  TEST_TABLE_DATA_V4.gethashUrl
);

// This is unfortunately needed since v4 gethash request
// requires the threat type (table name) as well as the
// state it's associated with. We have to run the update once
// to have the state written.
add_test(function test_update_v4() {
  gListManager.disableUpdate(TEST_TABLE_DATA_V4.tableName);
  gListManager.enableUpdate(TEST_TABLE_DATA_V4.tableName);

  // Force table update.
  Services.prefs.setCharPref(PREF_NEXTUPDATETIME_V4, "1");
  gListManager.maybeToggleUpdateChecking();
});

add_test(function test_getHashRequestV4() {
  let request = gUrlUtil.makeFindFullHashRequestV4(
    [TEST_TABLE_DATA_V4.tableName],
    [btoa(NEW_CLIENT_STATE)],
    [btoa("0123"), btoa("1234567"), btoa("1111")].sort()
  );
  registerHandlerGethashV4("&$req=" + request);
  let completeFinishedCnt = 0;

  gCompleter.complete(
    "0123",
    TEST_TABLE_DATA_V4.gethashUrl,
    TEST_TABLE_DATA_V4.tableName,
    false,
    {
      completionV4(hash, table, duration, fullhashes) {
        equal(hash, "0123");
        equal(table, TEST_TABLE_DATA_V4.tableName);
        equal(duration, 120);
        equal(fullhashes.length, 1);

        let match = fullhashes
          .QueryInterface(Ci.nsIArray)
          .queryElementAt(0, Ci.nsIFullHashMatch);

        equal(match.fullHash, "01234567890123456789012345678901");
        equal(match.cacheDuration, 8);
        info("completion: " + match.fullHash + ", " + table);
      },

      completionFinished(status) {
        equal(status, Cr.NS_OK);
        completeFinishedCnt++;
        if (3 === completeFinishedCnt) {
          run_next_test();
        }
      },
    }
  );

  gCompleter.complete(
    "1234567",
    TEST_TABLE_DATA_V4.gethashUrl,
    TEST_TABLE_DATA_V4.tableName,
    false,
    {
      completionV4(hash, table, duration, fullhashes) {
        equal(hash, "1234567");
        equal(table, TEST_TABLE_DATA_V4.tableName);
        equal(duration, 120);
        equal(fullhashes.length, 1);

        let match = fullhashes
          .QueryInterface(Ci.nsIArray)
          .queryElementAt(0, Ci.nsIFullHashMatch);

        equal(match.fullHash, "12345678901234567890123456789012");
        equal(match.cacheDuration, 7);
        info("completion: " + match.fullHash + ", " + table);
      },

      completionFinished(status) {
        equal(status, Cr.NS_OK);
        completeFinishedCnt++;
        if (3 === completeFinishedCnt) {
          run_next_test();
        }
      },
    }
  );

  gCompleter.complete(
    "1111",
    TEST_TABLE_DATA_V4.gethashUrl,
    TEST_TABLE_DATA_V4.tableName,
    false,
    {
      completionV4(hash, table, duration, fullhashes) {
        equal(hash, "1111");
        equal(table, TEST_TABLE_DATA_V4.tableName);
        equal(duration, 120);
        equal(fullhashes.length, 0);
      },

      completionFinished(status) {
        equal(status, Cr.NS_OK);
        completeFinishedCnt++;
        if (3 === completeFinishedCnt) {
          run_next_test();
        }
      },
    }
  );
});

add_test(function test_minWaitDuration() {
  let failedComplete = function () {
    gCompleter.complete(
      "0123",
      TEST_TABLE_DATA_V4.gethashUrl,
      TEST_TABLE_DATA_V4.tableName,
      false,
      {
        completionFinished(status) {
          equal(status, Cr.NS_ERROR_ABORT);
        },
      }
    );
  };

  let successComplete = function () {
    gCompleter.complete(
      "1234567",
      TEST_TABLE_DATA_V4.gethashUrl,
      TEST_TABLE_DATA_V4.tableName,
      false,
      {
        completionV4(hash, table, duration, fullhashes) {
          equal(hash, "1234567");
          equal(table, TEST_TABLE_DATA_V4.tableName);
          equal(fullhashes.length, 1);

          let match = fullhashes
            .QueryInterface(Ci.nsIArray)
            .queryElementAt(0, Ci.nsIFullHashMatch);

          equal(match.fullHash, "12345678901234567890123456789012");
          equal(match.cacheDuration, 7);
          info("completion: " + match.fullHash + ", " + table);
        },

        completionFinished(status) {
          equal(status, Cr.NS_OK);
          run_next_test();
        },
      }
    );
  };

  let request = gUrlUtil.makeFindFullHashRequestV4(
    [TEST_TABLE_DATA_V4.tableName],
    [btoa(NEW_CLIENT_STATE)],
    [btoa("1234567")]
  );
  registerHandlerGethashV4("&$req=" + request);

  // The last gethash response contained a min wait duration 12 secs 10 nano
  // So subsequent requests can happen only after the min wait duration
  do_timeout(1000, failedComplete);
  do_timeout(2000, failedComplete);
  do_timeout(4000, failedComplete);
  do_timeout(13000, successComplete);
});

function registerHandlerGethashV4(aExpectedQuery) {
  gHttpServV4.registerPathHandler(GETHASH_PATH, null);
  // V4 gethash handler.
  gHttpServV4.registerPathHandler(GETHASH_PATH, function (request, response) {
    equal(request.queryString, aExpectedQuery);

    response.setStatusLine(request.httpVersion, 200, "OK");
    response.bodyOutputStream.write(
      GETHASH_RESPONSE_CONTENT,
      GETHASH_RESPONSE_CONTENT.length
    );
  });
}

function registerHandlerUpdateV4() {
  // Update handler. Will respond a valid state to be verified in the
  // gethash handler.
  gHttpServV4.registerPathHandler(UPDATE_PATH, function (request, response) {
    response.setHeader(
      "Content-Type",
      "application/vnd.google.safebrowsing-update",
      false
    );
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.bodyOutputStream.write(
      UPDATE_RESPONSE_CONTENT,
      UPDATE_RESPONSE_CONTENT.length
    );

    waitUntilMetaDataSaved(NEW_CLIENT_STATE, CHECKSUM, () => {
      run_next_test();
    });
  });
}

function run_test() {
  throwOnUpdateErrors();

  gHttpServV4 = new HttpServer();
  gHttpServV4.registerDirectory("/", do_get_cwd());

  registerHandlerUpdateV4();
  gHttpServV4.start(5555);
  run_next_test();
}

registerCleanupFunction(function () {
  stopThrowingOnUpdateErrors();
});
