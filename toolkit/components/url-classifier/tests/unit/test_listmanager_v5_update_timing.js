/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// The V5 protocol carries a minimum wait duration per hash list, so the tables
// of a provider can be due at different times. Only the tables that are due
// take part in a given batched request.

const PROVIDER = "google5";
const UPDATE_URL = "http://localhost:5556/safebrowsing/update?";

// Both tables live on the same update URL, so they share one batched request.
const SHORT_TABLE = "test-google5-malware-proto";
const SHORT_SERVER_NAME = "test-4b";
const SHORT_WAIT_SEC = 30 * 60;

const LONG_TABLE = "test-globalcache-proto";
const LONG_SERVER_NAME = "test-32b";
const LONG_WAIT_SEC = 6 * 60 * 60;

// Mirrors defaultUpdateIntervalMs in UrlClassifierListManager.sys.mjs, which
// is what a table falls back to when the server gives it no wait duration.
const DEFAULT_WAIT_SEC = 30 * 60;

const PREF_NEXTUPDATETIME =
  "browser.safebrowsing.provider." + PROVIDER + ".nextupdatetime";
const PREF_NEXTUPDATETIME_SHORT = PREF_NEXTUPDATETIME + "." + SHORT_TABLE;
const PREF_NEXTUPDATETIME_LONG = PREF_NEXTUPDATETIME + "." + LONG_TABLE;

let gListManager = Cc["@mozilla.org/url-classifier/listmanager;1"].getService(
  Ci.nsIUrlListManager
);

let gTestUtils = Cc["@mozilla.org/url-classifier/test-utils;1"].getService(
  Ci.nsIUrlClassifierTestUtils
);

let gHttpServ = null;

// The server names requested by the last update, in the order they were sent.
let gRequestedNames = [];

// The lists the server answers with, and the wait duration of each.
let gResponseNames = [];
let gResponseWaits = [];

Services.prefs.setBoolPref("browser.safebrowsing.debug", true);

function waitForUpdateSuccess() {
  return new Promise((resolve, reject) => {
    let observer = function (subject, topic, data) {
      Services.obs.removeObserver(observer, "safebrowsing-update-finished");
      if (data.startsWith("success")) {
        resolve();
      } else {
        reject(new Error("Update failed: " + data));
      }
    };
    Services.obs.addObserver(observer, "safebrowsing-update-finished");
  });
}

// Makes every table due and runs one update, resolving once it succeeded.
function forceUpdate() {
  let updated = waitForUpdateSuccess();
  ok(
    gListManager.forceUpdates(SHORT_TABLE + "," + LONG_TABLE),
    "The update was not refused by the backoff algorithm"
  );
  return updated;
}

// Runs one update without making anything due, so that the list manager picks
// the tables itself.
function triggerScheduledUpdate() {
  let updated = waitForUpdateSuccess();

  // maybeToggleUpdateChecking() only arms a timer when there is none, so drop
  // the one the last update left behind first.
  gListManager.disableAllUpdates();
  gListManager.enableUpdate(SHORT_TABLE);
  gListManager.enableUpdate(LONG_TABLE);

  // Bring the provider-wide time forward. The tables with a next update time
  // of their own keep it, and so stay out of the request.
  Services.prefs.setCharPref(PREF_NEXTUPDATETIME, "1");
  gListManager.maybeToggleUpdateChecking();
  return updated;
}

add_setup(function () {
  gHttpServ = new HttpServer();
  gHttpServ.registerDirectory("/", do_get_cwd());
  gHttpServ.registerPathHandler(
    "/safebrowsing/update",
    function (request, response) {
      gRequestedNames = request.queryString
        .split("&")
        .filter(param => param.startsWith("names="))
        .map(param => param.substring("names=".length));

      let body = gTestUtils.makeUpdateResponseV5WithWaitDurations(
        gResponseNames,
        gResponseWaits
      );

      response.setHeader(
        "Content-Type",
        "application/vnd.google.safebrowsing-update",
        false
      );
      response.setStatusLine(request.httpVersion, 200, "OK");
      response.bodyOutputStream.write(body, body.length);
    }
  );
  gHttpServ.start(5556);

  gListManager.registerTable(SHORT_TABLE, PROVIDER, UPDATE_URL, "");
  gListManager.registerTable(LONG_TABLE, PROVIDER, UPDATE_URL, "");
  gListManager.enableUpdate(SHORT_TABLE);
  gListManager.enableUpdate(LONG_TABLE);

  registerCleanupFunction(async function () {
    gListManager.disableAllUpdates();
    gListManager.unregisterTable(SHORT_TABLE);
    gListManager.unregisterTable(LONG_TABLE);
    Services.prefs.clearUserPref(PREF_NEXTUPDATETIME);
    Services.prefs.clearUserPref("browser.safebrowsing.debug");
    await new Promise(resolve => gHttpServ.stop(resolve));
  });
});

// Differing wait durations: the provider-wide pref holds the shortest one and
// only the table that has to wait longer gets a pref of its own.
add_task(async function test_different_wait_durations_are_stored() {
  gResponseNames = [SHORT_SERVER_NAME, LONG_SERVER_NAME];
  gResponseWaits = [SHORT_WAIT_SEC, LONG_WAIT_SEC];

  let before = Date.now();
  await forceUpdate();
  let after = Date.now();

  deepEqual(
    gRequestedNames.sort(),
    [LONG_SERVER_NAME, SHORT_SERVER_NAME],
    "A forced update requests every table"
  );

  // The stored times are "now + wait", where "now" is read inside
  // #updateSuccess and so is somewhere between the samples taken either side
  // of the update. Bracket the expected value rather than guessing that clock
  // read. The provider-wide pref holds the shortest of the two waits.
  let earliest = parseInt(
    Services.prefs.getCharPref(PREF_NEXTUPDATETIME, ""),
    10
  );
  greaterOrEqual(earliest, before + SHORT_WAIT_SEC * 1000);
  lessOrEqual(earliest, after + SHORT_WAIT_SEC * 1000);

  ok(
    !Services.prefs.prefHasUserValue(PREF_NEXTUPDATETIME_SHORT),
    "The table updating at the shortest cadence has no pref of its own"
  );

  // Same bracket, against the longer wait this table was given.
  let longTime = parseInt(
    Services.prefs.getCharPref(PREF_NEXTUPDATETIME_LONG, ""),
    10
  );
  greaterOrEqual(longTime, before + LONG_WAIT_SEC * 1000);
  lessOrEqual(longTime, after + LONG_WAIT_SEC * 1000);
});

// Once the shortest table comes due, it is requested on its own: the table
// that was told to wait six hours stays out of the batch.
add_task(async function test_only_due_tables_are_requested() {
  gResponseNames = [SHORT_SERVER_NAME];
  gResponseWaits = [SHORT_WAIT_SEC];

  let longTimeBefore = Services.prefs.getCharPref(PREF_NEXTUPDATETIME_LONG, "");

  await triggerScheduledUpdate();

  deepEqual(
    gRequestedNames,
    [SHORT_SERVER_NAME],
    "Only the table that is due is requested"
  );

  equal(
    Services.prefs.getCharPref(PREF_NEXTUPDATETIME_LONG, ""),
    longTimeBefore,
    "The next update time of a table left out of the batch is untouched"
  );

  // The table left out of the batch keeps its time, and that time takes part
  // in the new provider-wide one. Check it does not pin the provider to the
  // moment in the past we just forced it to.
  greater(
    parseInt(Services.prefs.getCharPref(PREF_NEXTUPDATETIME, ""), 10),
    Date.now(),
    "The provider-wide time moved back into the future"
  );
});

// A table the server says nothing about falls back to the default interval
// rather than being requested again straight away.
add_task(async function test_table_missing_from_response() {
  gResponseNames = [LONG_SERVER_NAME];
  gResponseWaits = [LONG_WAIT_SEC];

  let before = Date.now();
  await forceUpdate();
  let after = Date.now();

  // The short table was requested but not answered for, so it falls back to
  // the default interval. That is shorter than the long table's six hours, so
  // it is what the provider-wide pref ends up holding.
  let earliest = parseInt(
    Services.prefs.getCharPref(PREF_NEXTUPDATETIME, ""),
    10
  );
  greaterOrEqual(earliest, before + DEFAULT_WAIT_SEC * 1000);
  lessOrEqual(earliest, after + DEFAULT_WAIT_SEC * 1000);

  ok(
    !Services.prefs.prefHasUserValue(PREF_NEXTUPDATETIME_SHORT),
    "The unanswered table sits at the shortest cadence, so has no pref"
  );

  // The table the server did answer for keeps the duration it was given.
  let longTime = parseInt(
    Services.prefs.getCharPref(PREF_NEXTUPDATETIME_LONG, ""),
    10
  );
  greaterOrEqual(longTime, before + LONG_WAIT_SEC * 1000);
  lessOrEqual(longTime, after + LONG_WAIT_SEC * 1000);
});

// When every table shares one wait duration, no per-table pref is written and
// the behaviour matches what it was before per-list scheduling.
add_task(async function test_equal_wait_durations() {
  gResponseNames = [SHORT_SERVER_NAME, LONG_SERVER_NAME];
  gResponseWaits = [SHORT_WAIT_SEC, SHORT_WAIT_SEC];

  await forceUpdate();

  ok(
    !Services.prefs.prefHasUserValue(PREF_NEXTUPDATETIME_SHORT),
    "No pref of its own for the short table"
  );
  ok(
    !Services.prefs.prefHasUserValue(PREF_NEXTUPDATETIME_LONG),
    "No pref of its own for the long table either"
  );
});
