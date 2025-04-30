/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/
*/
/* This testcase triggers two telemetry pings.
 *
 * Telemetry code keeps histograms of past telemetry pings. The first
 * ping populates these histograms. One of those histograms is then
 * checked in the second request.
 */

const { ClientID } = ChromeUtils.importESModule(
  "resource://gre/modules/ClientID.sys.mjs"
);
const { TelemetrySession } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetrySession.sys.mjs"
);
const { TelemetryEnvironment } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryEnvironment.sys.mjs"
);
const { TelemetryReportingPolicy } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryReportingPolicy.sys.mjs"
);

const PING_FORMAT_VERSION = 4;
const PING_TYPE_MAIN = "main";
const PING_TYPE_SAVED_SESSION = "saved-session";

const REASON_ABORTED_SESSION = "aborted-session";
const REASON_SAVED_SESSION = "saved-session";
const REASON_SHUTDOWN = "shutdown";
const REASON_TEST_PING = "test-ping";
const REASON_DAILY = "daily";
const REASON_ENVIRONMENT_CHANGE = "environment-change";

const IGNORE_CLONED_HISTOGRAM = "test::ignore_me_also";
// Add some unicode characters here to ensure that sending them works correctly.
const SHUTDOWN_TIME = 10000;
const FAILED_PROFILE_LOCK_ATTEMPTS = 2;

// Constants from prio.h for nsIFileOutputStream.init
const PR_WRONLY = 0x2;
const PR_CREATE_FILE = 0x8;
const PR_TRUNCATE = 0x20;
const RW_OWNER = parseInt("0600", 8);

const MS_IN_ONE_HOUR = 60 * 60 * 1000;
const MS_IN_ONE_DAY = 24 * MS_IN_ONE_HOUR;

const DATAREPORTING_DIR = "datareporting";
const ABORTED_PING_FILE_NAME = "aborted-session-ping";
const ABORTED_SESSION_UPDATE_INTERVAL_MS = 5 * 60 * 1000;

ChromeUtils.defineLazyGetter(this, "DATAREPORTING_PATH", function () {
  return PathUtils.join(PathUtils.profileDir, DATAREPORTING_DIR);
});

var gClientID = null;
var gProfileGroupID = null;
var gMonotonicNow = 0;

function sendPing() {
  TelemetrySession.gatherStartup();
  if (PingServer.started) {
    TelemetrySend.setServer("http://localhost:" + PingServer.port);
    return TelemetrySession.testPing();
  }
  TelemetrySend.setServer("http://doesnotexist");
  return TelemetrySession.testPing();
}

function fakeGenerateUUID(sessionFunc, subsessionFunc) {
  const { Policy } = ChromeUtils.importESModule(
    "resource://gre/modules/TelemetrySession.sys.mjs"
  );
  Policy.generateSessionUUID = sessionFunc;
  Policy.generateSubsessionUUID = subsessionFunc;
}

function fakeIdleNotification(topic) {
  return TelemetryScheduler.observe(null, topic, null);
}

function setupTestData() {
  Glean.testOnlyIpc.aCounterForHgram.add();
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.b.add(1);
}

function checkPingFormat(aPing, aType, aHasClientId, aHasEnvironment) {
  const MANDATORY_PING_FIELDS = [
    "type",
    "id",
    "creationDate",
    "version",
    "application",
    "payload",
  ];

  const APPLICATION_TEST_DATA = {
    buildId: gAppInfo.appBuildID,
    name: APP_NAME,
    version: APP_VERSION,
    vendor: "Mozilla",
    platformVersion: PLATFORM_VERSION,
    xpcomAbi: "noarch-spidermonkey",
  };

  // Check that the ping contains all the mandatory fields.
  for (let f of MANDATORY_PING_FIELDS) {
    Assert.ok(f in aPing, f + " must be available.");
  }

  Assert.equal(aPing.type, aType, "The ping must have the correct type.");
  Assert.equal(
    aPing.version,
    PING_FORMAT_VERSION,
    "The ping must have the correct version."
  );

  // Test the application section.
  for (let f in APPLICATION_TEST_DATA) {
    Assert.equal(
      aPing.application[f],
      APPLICATION_TEST_DATA[f],
      f + " must have the correct value."
    );
  }

  // We can't check the values for channel and architecture. Just make
  // sure they are in.
  Assert.ok(
    "architecture" in aPing.application,
    "The application section must have an architecture field."
  );
  Assert.ok(
    "channel" in aPing.application,
    "The application section must have a channel field."
  );

  // Check the clientId and environment fields, as needed.
  Assert.equal("clientId" in aPing, aHasClientId);
  Assert.equal("profileGroupId" in aPing, aHasClientId);
  Assert.equal("environment" in aPing, aHasEnvironment);
}

function checkPayloadInfo(data, reason) {
  const ALLOWED_REASONS = [
    "environment-change",
    "shutdown",
    "daily",
    "saved-session",
    "test-ping",
  ];
  let numberCheck = arg => {
    return typeof arg == "number";
  };
  let positiveNumberCheck = arg => {
    return numberCheck(arg) && arg >= 0;
  };
  let stringCheck = arg => {
    return typeof arg == "string" && arg != "";
  };
  let revisionCheck = arg => {
    return AppConstants.MOZILLA_OFFICIAL
      ? stringCheck(arg)
      : typeof arg == "string";
  };
  let uuidCheck = arg => {
    return UUID_REGEX.test(arg);
  };
  let isoDateCheck = arg => {
    // We expect use of this version of the ISO format:
    // 2015-04-12T18:51:19.1+00:00
    const isoDateRegEx =
      /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+[+-]\d{2}:\d{2}$/;
    return (
      stringCheck(arg) &&
      !Number.isNaN(Date.parse(arg)) &&
      isoDateRegEx.test(arg)
    );
  };

  const EXPECTED_INFO_FIELDS_TYPES = {
    reason: stringCheck,
    revision: revisionCheck,
    timezoneOffset: numberCheck,
    sessionId: uuidCheck,
    subsessionId: uuidCheck,
    // Special cases: previousSessionId and previousSubsessionId are null on first run.
    previousSessionId: arg => {
      return arg ? uuidCheck(arg) : true;
    },
    previousSubsessionId: arg => {
      return arg ? uuidCheck(arg) : true;
    },
    subsessionCounter: positiveNumberCheck,
    profileSubsessionCounter: positiveNumberCheck,
    sessionStartDate: isoDateCheck,
    subsessionStartDate: isoDateCheck,
    subsessionLength: positiveNumberCheck,
  };

  for (let f in EXPECTED_INFO_FIELDS_TYPES) {
    Assert.ok(f in data, f + " must be available.");

    let checkFunc = EXPECTED_INFO_FIELDS_TYPES[f];
    Assert.ok(
      checkFunc(data[f]),
      f + " must have the correct type and valid data " + data[f]
    );
  }

  // Check for a valid revision.
  if (data.revision != "") {
    const revisionUrlRegEx =
      /^http[s]?:\/\/hg.mozilla.org(\/[a-z\S]+)+(\/rev\/[0-9a-z]+)$/g;
    Assert.ok(revisionUrlRegEx.test(data.revision));
  }

  // Previous buildId is not mandatory.
  if (data.previousBuildId) {
    Assert.ok(stringCheck(data.previousBuildId));
  }

  Assert.ok(
    ALLOWED_REASONS.find(r => r == data.reason),
    "Payload must contain an allowed reason."
  );
  Assert.equal(data.reason, reason, "Payload reason must match expected.");

  Assert.ok(
    Date.parse(data.subsessionStartDate) >= Date.parse(data.sessionStartDate)
  );
  Assert.ok(data.profileSubsessionCounter >= data.subsessionCounter);

  // According to https://en.wikipedia.org/wiki/List_of_UTC_time_offsets,
  // UTC offsets range from -12 to +14 hours.
  // Don't think the extremes of the range are affected by further
  // daylight-savings adjustments, but it is possible.
  Assert.ok(
    data.timezoneOffset >= -12 * 60,
    "The timezone must be in a valid range."
  );
  Assert.ok(
    data.timezoneOffset <= 14 * 60,
    "The timezone must be in a valid range."
  );
}

function checkScalars(processes) {
  // Check that the scalars section is available in the ping payload.
  const parentProcess = processes.parent;
  Assert.ok(
    "scalars" in parentProcess,
    "The scalars section must be available in the parent process."
  );
  Assert.ok(
    "keyedScalars" in parentProcess,
    "The keyedScalars section must be available in the parent process."
  );
  Assert.equal(
    typeof parentProcess.scalars,
    "object",
    "The scalars entry must be an object."
  );
  Assert.equal(
    typeof parentProcess.keyedScalars,
    "object",
    "The keyedScalars entry must be an object."
  );

  let checkScalar = function (scalar, name) {
    // Check if the value is of a supported type.
    const valueType = typeof scalar;
    switch (valueType) {
      case "string":
        Assert.ok(
          scalar.length <= 50,
          "String values can't have more than 50 characters"
        );
        break;
      case "number":
        Assert.ok(
          scalar >= 0,
          "We only support unsigned integer values in scalars."
        );
        break;
      case "boolean":
        Assert.ok(true, "Boolean scalar found.");
        break;
      default:
        Assert.ok(
          false,
          name + " contains an unsupported value type (" + valueType + ")"
        );
    }
  };

  // Check that we have valid scalar entries.
  const scalars = parentProcess.scalars;
  for (let name in scalars) {
    Assert.equal(typeof name, "string", "Scalar names must be strings.");
    checkScalar(scalars[name], name);
  }

  // Check that we have valid keyed scalar entries.
  const keyedScalars = parentProcess.keyedScalars;
  for (let name in keyedScalars) {
    Assert.equal(typeof name, "string", "Scalar names must be strings.");
    Assert.ok(
      Object.keys(keyedScalars[name]).length,
      "The reported keyed scalars must contain at least 1 key."
    );
    for (let key in keyedScalars[name]) {
      Assert.equal(typeof key, "string", "Keyed scalar keys must be strings.");
      Assert.ok(
        key.length <= 70,
        "Keyed scalar keys can't have more than 70 characters."
      );
      checkScalar(scalars[name][key], name);
    }
  }
}

function checkPayload(payload, reason, successfulPings) {
  Assert.ok("info" in payload, "Payload must contain an info section.");
  checkPayloadInfo(payload.info, reason);

  Assert.ok(payload.simpleMeasurements.totalTime >= 0);
  Assert.equal(payload.simpleMeasurements.shutdownDuration, SHUTDOWN_TIME);

  let activeTicks = payload.simpleMeasurements.activeTicks;
  Assert.ok(activeTicks >= 0);

  if ("browser.timings.last_shutdown" in payload.processes.parent.scalars) {
    Assert.equal(
      payload.processes.parent.scalars["browser.timings.last_shutdown"],
      SHUTDOWN_TIME
    );
  }

  Assert.equal(
    payload.simpleMeasurements.failedProfileLockCount,
    FAILED_PROFILE_LOCK_ATTEMPTS
  );
  let profileDirectory = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let failedProfileLocksFile = profileDirectory.clone();
  failedProfileLocksFile.append("Telemetry.FailedProfileLocks.txt");
  Assert.ok(!failedProfileLocksFile.exists());

  let isWindows = "@mozilla.org/windows-registry-key;1" in Cc;
  if (isWindows) {
    Assert.ok(payload.simpleMeasurements.startupSessionRestoreReadBytes > 0);
    Assert.ok(payload.simpleMeasurements.startupSessionRestoreWriteBytes > 0);
  }

  const TELEMETRY_SEND_SUCCESS = "TELEMETRY_SEND_SUCCESS";
  const TELEMETRY_SUCCESS = "TELEMETRY_SUCCESS";
  const TELEMETRY_TEST_COUNT = "TELEMETRY_TEST_COUNT";
  const TELEMETRY_TEST_KEYED_COUNT = "TELEMETRY_TEST_KEYED_COUNT";

  if (successfulPings > 0) {
    Assert.ok(TELEMETRY_SEND_SUCCESS in payload.histograms);
  }
  Assert.ok(TELEMETRY_TEST_COUNT in payload.histograms);

  Assert.ok(!(IGNORE_CLONED_HISTOGRAM in payload.histograms));

  // We should have a test count.
  const expected_count = {
    range: [1, 2],
    bucket_count: 3,
    histogram_type: 4,
    values: { 0: 1, 1: 0 },
    sum: 1,
  };
  let count = payload.histograms[TELEMETRY_TEST_COUNT];
  Assert.deepEqual(count, expected_count);

  // There should be one successful report from the previous telemetry ping.
  if (successfulPings > 0) {
    const expected_tc = {
      range: [1, 2],
      bucket_count: 3,
      histogram_type: 2,
      values: { 0: 2, 1: successfulPings, 2: 0 },
      sum: successfulPings,
    };
    let tc = payload.histograms[TELEMETRY_SUCCESS];
    Assert.deepEqual(tc, expected_tc);
  }

  // The ping should include data from memory reporters.  We can't check that
  // this data is correct, because we can't control the values returned by the
  // memory reporters.  But we can at least check that the data is there.
  //
  // It's important to check for the presence of reporters with a mix of units,
  // because MemoryTelemetry has separate logic for each one.  But we can't
  // currently check UNITS_COUNT_CUMULATIVE or UNITS_PERCENTAGE because
  // Telemetry doesn't touch a memory reporter with these units that's
  // available on all platforms.

  Assert.ok("MEMORY_TOTAL" in payload.histograms); // UNITS_BYTES
  Assert.ok("MEMORY_JS_GC_HEAP" in payload.histograms); // UNITS_BYTES
  Assert.ok("MEMORY_JS_COMPARTMENTS_SYSTEM" in payload.histograms); // UNITS_COUNT

  Assert.ok(
    "mainThread" in payload.slowSQL && "otherThreads" in payload.slowSQL
  );

  // Check keyed histogram payload.

  Assert.ok("keyedHistograms" in payload);
  let keyedHistograms = payload.keyedHistograms;
  Assert.ok(TELEMETRY_TEST_KEYED_COUNT in keyedHistograms);

  const expected_keyed_count = {
    a: {
      range: [1, 2],
      bucket_count: 3,
      histogram_type: 4,
      values: { 0: 2, 1: 0 },
      sum: 2,
    },
    b: {
      range: [1, 2],
      bucket_count: 3,
      histogram_type: 4,
      values: { 0: 1, 1: 0 },
      sum: 1,
    },
  };
  Assert.deepEqual(
    expected_keyed_count,
    keyedHistograms[TELEMETRY_TEST_KEYED_COUNT]
  );

  Assert.ok(
    "processes" in payload,
    "The payload must have a processes section."
  );
  Assert.ok(
    "parent" in payload.processes,
    "There must be at least a parent process."
  );

  checkScalars(payload.processes);
}

function writeStringToFile(file, contents) {
  let ostream = Cc[
    "@mozilla.org/network/safe-file-output-stream;1"
  ].createInstance(Ci.nsIFileOutputStream);
  ostream.init(
    file,
    PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
    RW_OWNER,
    ostream.DEFER_OPEN
  );
  ostream.write(contents, contents.length);
  ostream.QueryInterface(Ci.nsISafeOutputStream).finish();
  ostream.close();
}

function write_fake_shutdown_file() {
  let profileDirectory = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let file = profileDirectory.clone();
  file.append("Telemetry.ShutdownTime.txt");
  let contents = "" + SHUTDOWN_TIME;
  writeStringToFile(file, contents);
}

function write_fake_failedprofilelocks_file() {
  let profileDirectory = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let file = profileDirectory.clone();
  file.append("Telemetry.FailedProfileLocks.txt");
  let contents = "" + FAILED_PROFILE_LOCK_ATTEMPTS;
  writeStringToFile(file, contents);
}

add_task(async function test_setup() {
  // Addon manager needs a profile directory
  do_get_profile();
  await loadAddonManager(APP_ID, APP_NAME, APP_VERSION, PLATFORM_VERSION);
  finishAddonManagerStartup();
  fakeIntlReady();
  // Make sure we don't generate unexpected pings due to pref changes.
  await setEmptyPrefWatchlist();

  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FhrUploadEnabled, true);

  // Make it look like we've previously failed to lock a profile a couple times.
  write_fake_failedprofilelocks_file();

  // Make it look like we've shutdown before.
  write_fake_shutdown_file();

  await new Promise(resolve =>
    Telemetry.asyncFetchTelemetryData(wrapWithExceptionHandler(resolve))
  );
});

add_task(async function asyncSetup() {
  await TelemetryController.testSetup();
  // Load the client ID from the client ID provider to check for pings sanity.
  gClientID = await ClientID.getClientID();
  gProfileGroupID = await ClientID.getProfileGroupID();
});

// Ensures that expired histograms are not part of the payload.
add_task(async function test_expiredHistogram() {
  Glean.testOnly.expiredHist.accumulateSingleSample(1);

  Assert.equal(
    TelemetrySession.getPayload().histograms.TELEMETRY_TEST_EXPIRED,
    undefined
  );
});

add_task(async function sessionTimeExcludingAndIncludingSuspend() {
  if (gIsAndroid) {
    // We don't support this new probe on android at the moment.
    return;
  }
  Services.prefs.setBoolPref(
    "toolkit.telemetry.testing.overrideProductsCheck",
    true
  );
  await TelemetryController.testReset();
  let subsession = TelemetrySession.getPayload("environment-change", true);
  let parentScalars = subsession.processes.parent.scalars;

  let withSuspend =
    parentScalars["browser.engagement.session_time_including_suspend"];
  let withoutSuspend =
    parentScalars["browser.engagement.session_time_excluding_suspend"];

  Assert.ok(
    withSuspend > 0,
    "The session time including suspend should be positive"
  );

  Assert.ok(
    withoutSuspend > 0,
    "The session time excluding suspend should be positive"
  );

  // Two things about the next assertion:
  // 1. The two calls to get the two different uptime values are made
  //    separately, so we can't guarantee equality, even if we know the machine
  //    has not been suspended (for example because it's running in infra and
  //    was just booted). In this case the value should be close to each other.
  // 2. This test will fail if the device running this has been suspended in
  //    between booting the Firefox process running this test, and doing the
  //    following assertion test, but that's unlikely in practice.
  const max_delta_ms = 100;

  Assert.ok(
    withSuspend - withoutSuspend <= max_delta_ms,
    "In test condition, the two uptimes should be close to each other"
  );

  // This however should always hold.
  Assert.greaterOrEqual(
    withSuspend,
    withoutSuspend,
    `The uptime with suspend must always been greater or equal to the uptime
     without suspend`
  );

  Services.prefs.setBoolPref(
    "toolkit.telemetry.testing.overrideProductsCheck",
    false
  );
});

// Sends a ping to a non existing server. If we remove this test, we won't get
// all the histograms we need in the main ping.
add_task(async function test_noServerPing() {
  await sendPing();
  // We need two pings in order to make sure STARTUP_MEMORY_STORAGE_SQLIE histograms
  // are initialised. See bug 1131585.
  await sendPing();
  // Allowing Telemetry to persist unsent pings as pending. If omitted may cause
  // problems to the consequent tests.
  await TelemetryController.testShutdown();
});

// Checks that a sent ping is correctly received by a dummy http server.
add_task(async function test_simplePing() {
  await TelemetryStorage.testClearPendingPings();
  PingServer.start();
  Services.prefs.setStringPref(
    TelemetryUtils.Preferences.Server,
    "http://localhost:" + PingServer.port
  );

  let now = new Date(2020, 1, 1, 12, 5, 6);
  let expectedDate = new Date(2020, 1, 1, 12, 0, 0);
  fakeNow(now);
  gMonotonicNow = fakeMonotonicNow(gMonotonicNow + 5000);

  const expectedSessionUUID = "bd314d15-95bf-4356-b682-b6c4a8942202";
  const expectedSubsessionUUID = "3e2e5f6c-74ba-4e4d-a93f-a48af238a8c7";
  fakeGenerateUUID(
    () => expectedSessionUUID,
    () => expectedSubsessionUUID
  );
  await TelemetryController.testReset();

  // Session and subsession start dates are faked during TelemetrySession setup. We can
  // now fake the session duration.
  const SESSION_DURATION_IN_MINUTES = 15;
  fakeNow(new Date(2020, 1, 1, 12, SESSION_DURATION_IN_MINUTES, 0));
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + SESSION_DURATION_IN_MINUTES * 60 * 1000
  );

  await sendPing();
  let ping = await PingServer.promiseNextPing();

  checkPingFormat(ping, PING_TYPE_MAIN, true, true);

  // Check that we get the data we expect.
  let payload = ping.payload;
  Assert.equal(payload.info.sessionId, expectedSessionUUID);
  Assert.equal(payload.info.subsessionId, expectedSubsessionUUID);
  let sessionStartDate = new Date(payload.info.sessionStartDate);
  Assert.equal(sessionStartDate.toISOString(), expectedDate.toISOString());
  let subsessionStartDate = new Date(payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), expectedDate.toISOString());
  Assert.equal(payload.info.subsessionLength, SESSION_DURATION_IN_MINUTES * 60);

  // Restore the UUID generator so we don't mess with other tests.
  fakeGenerateUUID(TelemetryUtils.generateUUID, TelemetryUtils.generateUUID);

  await TelemetryController.testShutdown();
});

// Saves the current session histograms, reloads them, performs a ping
// and checks that the dummy http server received both the previously
// saved ping and the new one.
add_task(async function test_saveLoadPing() {
  // Let's start out with a defined state.
  await TelemetryStorage.testClearPendingPings();
  await TelemetryController.testReset();
  PingServer.clearRequests();

  // Setup test data and trigger pings.
  setupTestData();
  await TelemetrySession.testSavePendingPing();
  await sendPing();

  // Get requests received by dummy server.
  const requests = await PingServer.promiseNextRequests(2);

  for (let req of requests) {
    Assert.equal(
      req.getHeader("content-type"),
      "application/json; charset=UTF-8",
      "The request must have the correct content-type."
    );
  }

  // We decode both requests to check for the |reason|.
  let pings = Array.from(requests, decodeRequestPayload);

  // Check we have the correct two requests. Ordering is not guaranteed. The ping type
  // is encoded in the URL.
  if (pings[0].type != PING_TYPE_MAIN) {
    pings.reverse();
  }

  checkPingFormat(pings[0], PING_TYPE_MAIN, true, true);
  checkPayload(pings[0].payload, REASON_TEST_PING, 0);
  checkPingFormat(pings[1], PING_TYPE_SAVED_SESSION, true, true);
  checkPayload(pings[1].payload, REASON_SAVED_SESSION, 0);

  await TelemetryController.testShutdown();
});

add_task(async function test_checkSubsessionScalars() {
  if (gIsAndroid) {
    // We don't support subsessions yet on Android.
    return;
  }

  // Clear the scalars.
  Telemetry.clearScalars();
  await TelemetryController.testReset();

  // Set some scalars.
  const UINT_SCALAR = "telemetry.test.mirror_for_quantity";
  const STRING_SCALAR = "telemetry.test.string_kind";
  const BOOLEAN_SCALAR = "telemetry.test.mirror_for_unordered_bool";
  const KEYED_UINT_SCALAR = "telemetry.test.mirror_for_labeled_quantity";
  let expectedUint = 37;
  let expectedString = "decafdec-afde-cafd-ecaf-decafdecafde";
  Glean.testOnly.meaningOfLife.set(expectedUint);
  Glean.testOnlyIpc.aUuid.set(expectedString);
  Glean.testOnlyIpc.anUnorderedBool.set(false);
  Glean.testOnly.buttonJars.some_random_key.set(12);

  // Check that scalars are not available in classic pings but are in subsession
  // pings. Also clear the subsession.
  let classic = TelemetrySession.getPayload();
  let subsession = TelemetrySession.getPayload("environment-change", true);

  const TEST_SCALARS = [
    UINT_SCALAR,
    STRING_SCALAR,
    BOOLEAN_SCALAR,
    KEYED_UINT_SCALAR,
  ];
  for (let name of TEST_SCALARS) {
    // Scalar must be reported in subsession pings (e.g. main).
    Assert.ok(
      name in subsession.processes.parent.scalars ||
        name in subsession.processes.parent.keyedScalars,
      name + " must be reported in a subsession ping."
    );
  }
  // No scalar must be reported in classic pings (e.g. saved-session).
  Assert.ok(
    !Object.keys(classic.processes.parent.scalars).length,
    "Scalars must not be reported in a classic ping."
  );

  // And make sure that we're getting the right values in the
  // subsession ping.
  Assert.equal(
    subsession.processes.parent.scalars[UINT_SCALAR],
    expectedUint,
    UINT_SCALAR + " must contain the expected value."
  );
  Assert.equal(
    subsession.processes.parent.scalars[STRING_SCALAR],
    expectedString,
    STRING_SCALAR + " must contain the expected value."
  );
  Assert.equal(
    subsession.processes.parent.scalars[BOOLEAN_SCALAR],
    false,
    BOOLEAN_SCALAR + " must contain the expected value."
  );
  Assert.deepEqual(
    subsession.processes.parent.keyedScalars[KEYED_UINT_SCALAR],
    { some_random_key: 12 },
    KEYED_UINT_SCALAR + " must contain the expected value."
  );

  // Since we cleared the subsession in the last getPayload(), check that
  // breaking subsessions clears the scalars.
  subsession = TelemetrySession.getPayload("environment-change");
  for (let name of TEST_SCALARS) {
    Assert.ok(
      !(name in subsession.processes.parent.scalars),
      name + " must be cleared with the new subsession."
    );
  }

  // Check if setting the scalars again works as expected.
  expectedUint = 85;
  expectedString = "A creative different value";
  Glean.testOnly.meaningOfLife.set(expectedUint);
  Glean.testOnlyIpc.aUuid.set(expectedString);
  Glean.testOnlyIpc.anUnorderedBool.set(false);
  Glean.testOnly.buttonJars.some_random_key.set(12);
  subsession = TelemetrySession.getPayload("environment-change");
  Assert.equal(
    subsession.processes.parent.scalars[UINT_SCALAR],
    expectedUint,
    UINT_SCALAR + " must contain the expected value."
  );
  Assert.equal(
    subsession.processes.parent.scalars[STRING_SCALAR],
    expectedString,
    STRING_SCALAR + " must contain the expected value."
  );
  Assert.equal(
    subsession.processes.parent.scalars[BOOLEAN_SCALAR],
    false,
    BOOLEAN_SCALAR + " must contain the expected value."
  );
  Assert.deepEqual(
    subsession.processes.parent.keyedScalars[KEYED_UINT_SCALAR],
    { some_random_key: 12 },
    KEYED_UINT_SCALAR + " must contain the expected value."
  );

  await TelemetryController.testShutdown();
});

add_task(async function test_dailyCollection() {
  if (gIsAndroid) {
    // We don't do daily collections yet on Android.
    return;
  }

  let now = new Date(2030, 1, 1, 12, 0, 0);
  let nowHour = new Date(2030, 1, 1, 12, 0, 0);
  let schedulerTickCallback = null;

  PingServer.clearRequests();

  fakeNow(now);

  // Fake scheduler functions to control daily collection flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );

  // Init and check timer.
  await TelemetryStorage.testClearPendingPings();
  await TelemetryController.testReset();
  TelemetrySend.setServer("http://localhost:" + PingServer.port);

  // Set histograms to expected state.
  const COUNT_ID = "TELEMETRY_TEST_COUNT";
  const KEYED_ID = "TELEMETRY_TEST_KEYED_COUNT";
  const count = Telemetry.getHistogramById(COUNT_ID);
  const keyed = Telemetry.getKeyedHistogramById(KEYED_ID);

  count.clear();
  keyed.clear();
  Glean.testOnlyIpc.aCounterForHgram.add();
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.b.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.b.add(1);

  // Make sure the daily ping gets triggered.
  let expectedDate = nowHour;
  now = futureDate(nowHour, MS_IN_ONE_DAY);
  fakeNow(now);

  Assert.ok(!!schedulerTickCallback);
  // Run a scheduler tick: it should trigger the daily ping.
  await schedulerTickCallback();

  // Collect the daily ping.
  let ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.payload.info.reason, REASON_DAILY);
  let subsessionStartDate = new Date(ping.payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), expectedDate.toISOString());

  Assert.equal(ping.payload.histograms[COUNT_ID].sum, 1);
  Assert.equal(ping.payload.keyedHistograms[KEYED_ID].a.sum, 1);
  Assert.equal(ping.payload.keyedHistograms[KEYED_ID].b.sum, 2);

  // The daily ping is rescheduled for "tomorrow".
  expectedDate = futureDate(expectedDate, MS_IN_ONE_DAY);
  now = futureDate(now, MS_IN_ONE_DAY);
  fakeNow(now);

  // Run a scheduler tick. Trigger and collect another ping. The histograms should be reset.
  await schedulerTickCallback();

  ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.payload.info.reason, REASON_DAILY);
  subsessionStartDate = new Date(ping.payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), expectedDate.toISOString());

  Assert.ok(!(COUNT_ID in ping.payload.histograms));
  Assert.ok(!(KEYED_ID in ping.payload.keyedHistograms));

  // Trigger and collect another daily ping, with the histograms being set again.
  Glean.testOnlyIpc.aCounterForHgram.add();
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.b.add(1);

  // The daily ping is rescheduled for "tomorrow".
  expectedDate = futureDate(expectedDate, MS_IN_ONE_DAY);
  now = futureDate(now, MS_IN_ONE_DAY);
  fakeNow(now);

  await schedulerTickCallback();
  ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.payload.info.reason, REASON_DAILY);
  subsessionStartDate = new Date(ping.payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), expectedDate.toISOString());

  Assert.equal(ping.payload.histograms[COUNT_ID].sum, 1);
  Assert.equal(ping.payload.keyedHistograms[KEYED_ID].a.sum, 1);
  Assert.equal(ping.payload.keyedHistograms[KEYED_ID].b.sum, 1);

  // Shutdown to cleanup the aborted-session if it gets created.
  await TelemetryController.testShutdown();
});

add_task(async function test_dailyDuplication() {
  if (gIsAndroid) {
    // We don't do daily collections yet on Android.
    return;
  }

  await TelemetrySend.reset();
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  let schedulerTickCallback = null;
  let now = new Date(2030, 1, 1, 0, 0, 0);
  fakeNow(now);
  // Fake scheduler functions to control daily collection flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  // Make sure the daily ping gets triggered at midnight.
  // We need to make sure that we trigger this after the period where we wait for
  // the user to become idle.
  let firstDailyDue = new Date(2030, 1, 2, 0, 0, 0);
  fakeNow(firstDailyDue);

  // Run a scheduler tick: it should trigger the daily ping.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();

  // Get the first daily ping.
  let ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.payload.info.reason, REASON_DAILY);

  // We don't expect to receive any other daily ping in this test, so assert if we do.
  PingServer.registerPingHandler(() => {
    Assert.ok(
      false,
      "No more daily pings should be sent/received in this test."
    );
  });

  // Set the current time to a bit after midnight.
  let secondDailyDue = new Date(firstDailyDue);
  secondDailyDue.setHours(0);
  secondDailyDue.setMinutes(15);
  fakeNow(secondDailyDue);

  // Run a scheduler tick: it should NOT trigger the daily ping.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();

  // Shutdown to cleanup the aborted-session if it gets created.
  PingServer.resetPingHandler();
  await TelemetryController.testShutdown();
});

add_task(async function test_dailyOverdue() {
  if (gIsAndroid) {
    // We don't do daily collections yet on Android.
    return;
  }

  let schedulerTickCallback = null;
  let now = new Date(2030, 1, 1, 11, 0, 0);
  fakeNow(now);
  // Fake scheduler functions to control daily collection flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryStorage.testClearPendingPings();
  await TelemetryController.testReset();

  // Skip one hour ahead: nothing should be due.
  now.setHours(now.getHours() + 1);
  fakeNow(now);

  // Assert if we receive something!
  PingServer.registerPingHandler(() => {
    Assert.ok(false, "No daily ping should be received if not overdue!.");
  });

  // This tick should not trigger any daily ping.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();

  // Restore the non asserting ping handler.
  PingServer.resetPingHandler();
  PingServer.clearRequests();

  // Simulate an overdue ping: we're not close to midnight, but the last daily ping
  // time is too long ago.
  let dailyOverdue = new Date(2030, 1, 2, 13, 0, 0);
  fakeNow(dailyOverdue);

  // Run a scheduler tick: it should trigger the daily ping.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();

  // Get the first daily ping.
  let ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.payload.info.reason, REASON_DAILY);

  // Shutdown to cleanup the aborted-session if it gets created.
  await TelemetryController.testShutdown();
});

add_task(async function test_environmentChange() {
  if (gIsAndroid) {
    // We don't split subsessions on environment changes yet on Android.
    return;
  }

  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  let now = fakeNow(2040, 1, 1, 12, 0, 0);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );

  const PREF_TEST = "toolkit.telemetry.test.pref1";
  Services.prefs.clearUserPref(PREF_TEST);

  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_VALUE }],
  ]);

  // Setup.
  await TelemetryController.testReset();
  TelemetrySend.setServer("http://localhost:" + PingServer.port);
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);

  // Set histograms to expected state.
  const COUNT_ID = "TELEMETRY_TEST_COUNT";
  const KEYED_ID = "TELEMETRY_TEST_KEYED_COUNT";
  const count = Telemetry.getHistogramById(COUNT_ID);
  const keyed = Telemetry.getKeyedHistogramById(KEYED_ID);

  count.clear();
  keyed.clear();
  Glean.testOnlyIpc.aCounterForHgram.add();
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.a.add(1);
  Glean.testOnlyIpc.aLabeledCounterForKeyedCountHgram.b.add(1);

  // Trigger and collect environment-change ping.
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  let startHour = TelemetryUtils.truncateToHours(now);
  now = fakeNow(futureDate(now, 10 * MILLISECONDS_PER_MINUTE));

  Services.prefs.setIntPref(PREF_TEST, 1);
  let ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.environment.settings.userPrefs[PREF_TEST], undefined);
  Assert.equal(ping.payload.info.reason, REASON_ENVIRONMENT_CHANGE);
  let subsessionStartDate = new Date(ping.payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), startHour.toISOString());

  Assert.equal(ping.payload.histograms[COUNT_ID].sum, 1);
  Assert.equal(ping.payload.keyedHistograms[KEYED_ID].a.sum, 1);

  // Trigger and collect another ping. The histograms should be reset.
  startHour = TelemetryUtils.truncateToHours(now);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  now = fakeNow(futureDate(now, 10 * MILLISECONDS_PER_MINUTE));

  Services.prefs.setIntPref(PREF_TEST, 2);
  ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping);

  Assert.equal(ping.type, PING_TYPE_MAIN);
  Assert.equal(ping.environment.settings.userPrefs[PREF_TEST], 1);
  Assert.equal(ping.payload.info.reason, REASON_ENVIRONMENT_CHANGE);
  subsessionStartDate = new Date(ping.payload.info.subsessionStartDate);
  Assert.equal(subsessionStartDate.toISOString(), startHour.toISOString());

  Assert.ok(!(COUNT_ID in ping.payload.histograms));
  Assert.ok(!(KEYED_ID in ping.payload.keyedHistograms));

  // Trigger and collect another ping. The histograms should be reset.
  startHour = TelemetryUtils.truncateToHours(now);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  now = fakeNow(futureDate(now, 10 * MILLISECONDS_PER_MINUTE));

  await TelemetryController.testShutdown();
});

add_task(async function test_experimentAnnotations_subsession() {
  if (gIsAndroid) {
    // We don't split subsessions on environment changes yet on Android.
    return;
  }

  const EXPERIMENT1 = "experiment-1";
  const EXPERIMENT1_BRANCH = "nice-branch";
  const EXPERIMENT2 = "experiment-2";
  const EXPERIMENT2_BRANCH = "other-branch";

  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  let now = fakeNow(2040, 1, 1, 12, 0, 0);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );

  // Setup.
  await TelemetryController.testReset();
  TelemetrySend.setServer("http://localhost:" + PingServer.port);
  Assert.equal(TelemetrySession.getPayload().info.subsessionCounter, 1);

  // Trigger a subsession split with a telemetry annotation.
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  let futureTestDate = futureDate(now, 10 * MILLISECONDS_PER_MINUTE);
  now = fakeNow(futureTestDate);
  TelemetryEnvironment.setExperimentActive(EXPERIMENT1, EXPERIMENT1_BRANCH);

  let ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping, "A ping must be received.");

  Assert.equal(
    ping.type,
    PING_TYPE_MAIN,
    "The received ping must be a 'main' ping."
  );
  Assert.equal(
    ping.payload.info.reason,
    REASON_ENVIRONMENT_CHANGE,
    "The 'main' ping must be triggered by a change in the environment."
  );
  // We expect the current experiments to be reported in the next ping, not this
  // one.
  Assert.ok(
    !("experiments" in ping.environment),
    "The old environment must contain no active experiments."
  );
  // Since this change wasn't throttled, the subsession counter must increase.
  Assert.equal(
    TelemetrySession.getPayload().info.subsessionCounter,
    2,
    "The experiment annotation must trigger a new subsession."
  );

  // Add another annotation to the environment. We're not advancing the fake
  // timer, so no subsession split should happen due to throttling.
  TelemetryEnvironment.setExperimentActive(EXPERIMENT2, EXPERIMENT2_BRANCH);
  Assert.equal(
    TelemetrySession.getPayload().info.subsessionCounter,
    2,
    "The experiment annotation must not trigger a new subsession " +
      "if throttling happens."
  );
  let oldExperiments = TelemetryEnvironment.getActiveExperiments();

  // Fake the timer and remove an annotation, we expect a new subsession split.
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  now = fakeNow(futureDate(now, 10 * MILLISECONDS_PER_MINUTE));
  TelemetryEnvironment.setExperimentInactive(EXPERIMENT1, EXPERIMENT1_BRANCH);

  ping = await PingServer.promiseNextPing();
  Assert.ok(!!ping, "A ping must be received.");

  Assert.equal(
    ping.type,
    PING_TYPE_MAIN,
    "The received ping must be a 'main' ping."
  );
  Assert.equal(
    ping.payload.info.reason,
    REASON_ENVIRONMENT_CHANGE,
    "The 'main' ping must be triggered by a change in the environment."
  );
  // We expect both experiments to be in this environment.
  Assert.deepEqual(
    ping.environment.experiments,
    oldExperiments,
    "The environment must contain both the experiments."
  );
  Assert.equal(
    TelemetrySession.getPayload().info.subsessionCounter,
    3,
    "The removing an experiment annotation must trigger a new subsession."
  );

  await TelemetryController.testShutdown();
});

add_task(async function test_savedPingsOnShutdown() {
  await TelemetryController.testReset();

  // Assure that we store the ping properly when saving sessions on shutdown.
  // We make the TelemetryController shutdown to trigger a session save.
  const dir = TelemetryStorage.pingDirectoryPath;
  await IOUtils.remove(dir, { ignoreAbsent: true, recursive: true });
  await IOUtils.makeDirectory(dir);
  await TelemetryController.testShutdown();

  PingServer.clearRequests();
  await TelemetryController.testReset();

  const ping = await PingServer.promiseNextPing();

  let expectedType = gIsAndroid ? PING_TYPE_SAVED_SESSION : PING_TYPE_MAIN;
  let expectedReason = gIsAndroid ? REASON_SAVED_SESSION : REASON_SHUTDOWN;

  checkPingFormat(ping, expectedType, true, true);
  Assert.equal(ping.payload.info.reason, expectedReason);
  Assert.equal(ping.clientId, gClientID);
  Assert.equal(ping.profileGroupId, gProfileGroupID);
});

add_task(async function test_sendShutdownPing() {
  if (
    gIsAndroid ||
    (AppConstants.platform == "linux" && !Services.appinfo.is64Bit)
  ) {
    // We don't support the pingsender on Android, yet, see bug 1335917.
    // We also don't suppor the pingsender testing on Treeherder for
    // Linux 32 bit (due to missing libraries). So skip it there too.
    // See bug 1310703 comment 78.
    return;
  }

  let checkPendingShutdownPing = async function () {
    let pendingPings = await TelemetryStorage.loadPendingPingList();
    Assert.equal(pendingPings.length, 1, "We expect 1 pending ping: shutdown.");
    // Load the pings off the disk.
    const shutdownPing = await TelemetryStorage.loadPendingPing(
      pendingPings[0].id
    );
    Assert.ok(shutdownPing, "The 'shutdown' ping must be saved to disk.");
    Assert.equal(
      "shutdown",
      shutdownPing.payload.info.reason,
      "The 'shutdown' ping must be saved to disk."
    );
  };

  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSender,
    true
  );
  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, false);
  // Make sure the reporting policy picks up the updated pref.
  TelemetryReportingPolicy.testUpdateFirstRun();
  PingServer.clearRequests();
  Telemetry.clearScalars();

  // Shutdown telemetry and wait for an incoming ping.
  let nextPing = PingServer.promiseNextPing();
  await TelemetryController.testShutdown();
  let ping = await nextPing;

  // Check that we received a shutdown ping.
  checkPingFormat(ping, ping.type, true, true);
  Assert.equal(ping.payload.info.reason, REASON_SHUTDOWN);
  Assert.equal(ping.clientId, gClientID);
  Assert.equal(ping.profileGroupId, gProfileGroupID);
  // Try again, this time disable ping upload. The PingSender
  // should not be sending any ping!
  PingServer.registerPingHandler(() =>
    Assert.ok(false, "Telemetry must not send pings if not allowed to.")
  );
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.FhrUploadEnabled,
    false
  );
  await TelemetryController.testReset();
  await TelemetryController.testShutdown();

  // Make sure we have no pending pings between the runs.
  await TelemetryStorage.testClearPendingPings();

  // Enable ping upload and signal an OS shutdown. The pingsender
  // will not be spawned and no ping will be sent.
  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FhrUploadEnabled, true);
  await TelemetryController.testReset();
  Services.obs.notifyObservers(null, "quit-application-forced");
  await TelemetryController.testShutdown();
  // After re-enabling FHR, wait for the new client ID
  gClientID = await ClientID.getClientID();
  gProfileGroupID = await ClientID.getProfileGroupID();

  // Check that the "shutdown" ping was correctly saved to disk.
  await checkPendingShutdownPing();

  // Make sure we have no pending pings between the runs.
  await TelemetryStorage.testClearPendingPings();
  Telemetry.clearScalars();

  await TelemetryController.testReset();
  Services.obs.notifyObservers(
    null,
    "quit-application-granted",
    "syncShutdown"
  );
  await TelemetryController.testShutdown();
  await checkPendingShutdownPing();

  // Make sure we have no pending pings between the runs.
  await TelemetryStorage.testClearPendingPings();

  // Disable the "submission policy". The shutdown ping must not be sent.
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.BypassNotification,
    false
  );
  await TelemetryController.testReset();
  await TelemetryController.testShutdown();

  // Make sure we have no pending pings between the runs.
  await TelemetryStorage.testClearPendingPings();

  // We cannot reset the BypassNotification pref, as we need it to be
  // |true| in tests.
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.BypassNotification,
    true
  );

  // With both upload enabled and the policy shown, make sure we don't
  // send the shutdown ping using the pingsender on the first
  // subsession.
  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, true);
  // Make sure the reporting policy picks up the updated pref.
  TelemetryReportingPolicy.testUpdateFirstRun();

  await TelemetryController.testReset();
  await TelemetryController.testShutdown();

  // Clear the state and prepare for the next test.
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();
  PingServer.resetPingHandler();

  // Check that we're able to send the shutdown ping using the pingsender
  // from the first session if the related pref is on.
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSenderFirstSession,
    true
  );
  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, true);
  TelemetryReportingPolicy.testUpdateFirstRun();

  // Restart/shutdown telemetry and wait for an incoming ping.
  await TelemetryController.testReset();
  await TelemetryController.testShutdown();
  ping = await PingServer.promiseNextPing();

  // Check that we received a shutdown ping.
  checkPingFormat(ping, ping.type, true, true);
  Assert.equal(ping.payload.info.reason, REASON_SHUTDOWN);
  Assert.equal(ping.clientId, gClientID);
  Assert.equal(ping.profileGroupId, gProfileGroupID);

  // Reset the pref and restart Telemetry.
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSender,
    false
  );
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSenderFirstSession,
    false
  );
  Services.prefs.clearUserPref(TelemetryUtils.Preferences.FirstRun);
  PingServer.resetPingHandler();
});

add_task(async function test_sendFirstShutdownPing() {
  if (
    gIsAndroid ||
    (AppConstants.platform == "linux" && !Services.appinfo.is64Bit)
  ) {
    // We don't support the pingsender on Android, yet, see bug 1335917.
    // We also don't suppor the pingsender testing on Treeherder for
    // Linux 32 bit (due to missing libraries). So skip it there too.
    // See bug 1310703 comment 78.
    return;
  }

  let storageContainsFirstShutdown = async function () {
    let pendingPings = await TelemetryStorage.loadPendingPingList();
    let pings = await Promise.all(
      pendingPings.map(async p => {
        return TelemetryStorage.loadPendingPing(p.id);
      })
    );
    return pings.find(p => p.type == "first-shutdown");
  };

  let checkShutdownNotSent = async function () {
    // The failure-mode of the ping-sender is used to check that a ping was
    // *not* sent. This can be combined with the state of the storage to infer
    // the appropriate behavior from the preference flags.

    // Assert failure if we recive a ping.
    PingServer.registerPingHandler(req => {
      const receivedPing = decodeRequestPayload(req);
      Assert.ok(
        false,
        `No ping should be received in this test (got ${receivedPing.id}).`
      );
    });

    // Assert that pings are sent on first run, forcing a forced application
    // quit. This should be equivalent to the first test in this suite.
    Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, true);
    TelemetryReportingPolicy.testUpdateFirstRun();

    await TelemetryController.testReset();
    Services.obs.notifyObservers(null, "quit-application-forced");
    await TelemetryController.testShutdown();
    Assert.ok(
      await storageContainsFirstShutdown(),
      "The 'first-shutdown' ping must be saved to disk."
    );

    await TelemetryStorage.testClearPendingPings();

    // Assert that it's not sent during subsequent runs
    Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, false);
    TelemetryReportingPolicy.testUpdateFirstRun();

    await TelemetryController.testReset();
    Services.obs.notifyObservers(null, "quit-application-forced");
    await TelemetryController.testShutdown();
    Assert.ok(
      !(await storageContainsFirstShutdown()),
      "The 'first-shutdown' ping should only be written during first run."
    );

    await TelemetryStorage.testClearPendingPings();

    // Assert that the the ping is only sent if the flag is enabled.
    Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, true);
    Services.prefs.setBoolPref(
      TelemetryUtils.Preferences.FirstShutdownPingEnabled,
      false
    );
    TelemetryReportingPolicy.testUpdateFirstRun();

    await TelemetryController.testReset();
    await TelemetryController.testShutdown();
    Assert.ok(
      !(await storageContainsFirstShutdown()),
      "The 'first-shutdown' ping should only be written if enabled"
    );

    await TelemetryStorage.testClearPendingPings();

    // Assert that the the ping is not collected when the ping-sender is disabled.
    // The information would be made irrelevant by the main-ping in the second session.
    Services.prefs.setBoolPref(
      TelemetryUtils.Preferences.FirstShutdownPingEnabled,
      true
    );
    Services.prefs.setBoolPref(
      TelemetryUtils.Preferences.ShutdownPingSender,
      false
    );
    TelemetryReportingPolicy.testUpdateFirstRun();

    await TelemetryController.testReset();
    await TelemetryController.testShutdown();
    Assert.ok(
      !(await storageContainsFirstShutdown()),
      "The 'first-shutdown' ping should only be written if ping-sender is enabled"
    );

    // Clear the state and prepare for the next test.
    await TelemetryStorage.testClearPendingPings();
    PingServer.clearRequests();
    PingServer.resetPingHandler();
  };

  // Remove leftover pending pings from other tests
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();
  Telemetry.clearScalars();

  // Set testing invariants for FirstShutdownPingEnabled
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSender,
    true
  );
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSenderFirstSession,
    false
  );

  // Set primary conditions of the 'first-shutdown' ping
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.FirstShutdownPingEnabled,
    true
  );
  Services.prefs.setBoolPref(TelemetryUtils.Preferences.FirstRun, true);
  TelemetryReportingPolicy.testUpdateFirstRun();

  // Assert general 'first-shutdown' use-case.
  await TelemetryController.testReset();
  await TelemetryController.testShutdown();
  let ping = await PingServer.promiseNextPing();
  checkPingFormat(ping, "first-shutdown", true, true);
  Assert.equal(ping.payload.info.reason, REASON_SHUTDOWN);
  Assert.equal(ping.clientId, gClientID);
  Assert.equal(ping.profileGroupId, gProfileGroupID);

  await TelemetryStorage.testClearPendingPings();

  // Assert that the shutdown is not sent under various conditions
  await checkShutdownNotSent();

  // Reset the pref and restart Telemetry.
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSender,
    false
  );
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.ShutdownPingSenderFirstSession,
    false
  );
  Services.prefs.setBoolPref(
    TelemetryUtils.Preferences.FirstShutdownPingEnabled,
    false
  );
  Services.prefs.clearUserPref(TelemetryUtils.Preferences.FirstRun);
  PingServer.resetPingHandler();
});

add_task(async function test_savedSessionData() {
  // Create the directory which will contain the data file, if it doesn't already
  // exist.
  await IOUtils.makeDirectory(DATAREPORTING_PATH);

  // Write test data to the session data file.
  const dataFilePath = PathUtils.join(DATAREPORTING_PATH, "session-state.json");
  const sessionState = {
    sessionId: null,
    subsessionId: null,
    profileSubsessionCounter: 3785,
  };
  await IOUtils.writeJSON(dataFilePath, sessionState);

  const PREF_TEST = "toolkit.telemetry.test.pref1";
  Services.prefs.clearUserPref(PREF_TEST);
  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_VALUE }],
  ]);

  // We expect one new subsession when starting TelemetrySession and one after triggering
  // an environment change.
  const expectedSubsessions = sessionState.profileSubsessionCounter + 2;
  const expectedSessionUUID = "ff602e52-47a1-b7e8-4c1a-ffffffffc87a";
  const expectedSubsessionUUID = "009fd1ad-b85e-4817-b3e5-000000003785";
  fakeGenerateUUID(
    () => expectedSessionUUID,
    () => expectedSubsessionUUID
  );

  if (gIsAndroid) {
    // We don't support subsessions yet on Android, so skip the next checks.
    return;
  }

  // Start TelemetrySession so that it loads the session data file.
  await TelemetryController.testReset();

  // Watch a test preference, trigger and environment change and wait for it to propagate.
  // _watchPreferences triggers a subsession notification
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  fakeNow(new Date(2050, 1, 1, 12, 0, 0));
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);
  let changePromise = new Promise(resolve =>
    TelemetryEnvironment.registerChangeListener("test_fake_change", resolve)
  );
  Services.prefs.setIntPref(PREF_TEST, 1);
  await changePromise;
  TelemetryEnvironment.unregisterChangeListener("test_fake_change");

  let payload = TelemetrySession.getPayload();
  Assert.equal(payload.info.profileSubsessionCounter, expectedSubsessions);
  await TelemetryController.testShutdown();

  // Restore the UUID generator so we don't mess with other tests.
  fakeGenerateUUID(TelemetryUtils.generateUUID, TelemetryUtils.generateUUID);

  // Load back the serialised session data.
  let data = await IOUtils.readJSON(dataFilePath);
  Assert.equal(data.profileSubsessionCounter, expectedSubsessions);
  Assert.equal(data.sessionId, expectedSessionUUID);
  Assert.equal(data.subsessionId, expectedSubsessionUUID);
});

add_task(async function test_sessionData_ShortSession() {
  if (gIsAndroid) {
    // We don't support subsessions yet on Android, so skip the next checks.
    return;
  }

  const SESSION_STATE_PATH = PathUtils.join(
    DATAREPORTING_PATH,
    "session-state.json"
  );

  // Remove the session state file.
  await IOUtils.remove(SESSION_STATE_PATH, { ignoreAbsent: true });

  const expectedSessionUUID = "ff602e52-47a1-b7e8-4c1a-ffffffffc87a";
  const expectedSubsessionUUID = "009fd1ad-b85e-4817-b3e5-000000003785";
  fakeGenerateUUID(
    () => expectedSessionUUID,
    () => expectedSubsessionUUID
  );

  // We intentionally don't wait for the setup to complete and shut down to simulate
  // short sessions. We expect the profile subsession counter to be 1.
  TelemetryController.testReset();
  await TelemetryController.testShutdown();

  // Restore the UUID generation functions.
  fakeGenerateUUID(TelemetryUtils.generateUUID, TelemetryUtils.generateUUID);

  // Start TelemetryController so that it loads the session data file. We expect the profile
  // subsession counter to be incremented by 1 again.
  await TelemetryController.testReset();

  // We expect 2 profile subsession counter updates.
  let payload = TelemetrySession.getPayload();
  Assert.equal(payload.info.profileSubsessionCounter, 2);
  Assert.equal(payload.info.previousSessionId, expectedSessionUUID);
  Assert.equal(payload.info.previousSubsessionId, expectedSubsessionUUID);

  await TelemetryController.testShutdown();
});

add_task(async function test_invalidSessionData() {
  // Create the directory which will contain the data file, if it doesn't already
  // exist.
  await IOUtils.makeDirectory(DATAREPORTING_PATH);

  // Write test data to the session data file. This should fail to parse.
  const dataFilePath = PathUtils.join(DATAREPORTING_PATH, "session-state.json");
  const unparseableData = "{asdf:@äü";
  await IOUtils.writeUTF8(dataFilePath, unparseableData, {
    tmpPath: `${dataFilePath}.tmp`,
  });

  // Start TelemetryController so that it loads the session data file.
  await TelemetryController.testReset();

  // Write test data to the session data file. This should fail validation.
  const sessionState = {
    profileSubsessionCounter: "not-a-number?",
    someOtherField: 12,
  };
  await IOUtils.writeJSON(dataFilePath, sessionState);

  // The session data file should not load. Only expect the current subsession.
  const expectedSubsessions = 1;
  const expectedSessionUUID = "ff602e52-47a1-b7e8-4c1a-ffffffffc87a";
  const expectedSubsessionUUID = "009fd1ad-b85e-4817-b3e5-000000003785";
  fakeGenerateUUID(
    () => expectedSessionUUID,
    () => expectedSubsessionUUID
  );

  // Start TelemetryController so that it loads the session data file.
  await TelemetryController.testShutdown();
  await TelemetryController.testReset();

  let payload = TelemetrySession.getPayload();
  Assert.equal(payload.info.profileSubsessionCounter, expectedSubsessions);

  await TelemetryController.testShutdown();

  // Restore the UUID generator so we don't mess with other tests.
  fakeGenerateUUID(TelemetryUtils.generateUUID, TelemetryUtils.generateUUID);

  // Load back the serialised session data.
  let data = await IOUtils.readJSON(dataFilePath);
  Assert.equal(data.profileSubsessionCounter, expectedSubsessions);
  Assert.equal(data.sessionId, expectedSessionUUID);
  Assert.equal(data.subsessionId, expectedSubsessionUUID);
});

add_task(async function test_abortedSession() {
  if (gIsAndroid) {
    // We don't have the aborted session ping here.
    return;
  }

  const ABORTED_FILE = PathUtils.join(
    DATAREPORTING_PATH,
    ABORTED_PING_FILE_NAME
  );

  // Make sure the aborted sessions directory does not exist to test its creation.
  await IOUtils.remove(DATAREPORTING_PATH, {
    ignoreAbsent: true,
    recursive: true,
  });

  let schedulerTickCallback = null;
  let now = new Date(2040, 1, 1, 0, 0, 0);
  fakeNow(now);
  // Fake scheduler functions to control aborted-session flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  Assert.ok(
    await IOUtils.exists(DATAREPORTING_PATH),
    "Telemetry must create the aborted session directory when starting."
  );

  // Fake now again so that the scheduled aborted-session save takes place.
  now = futureDate(now, ABORTED_SESSION_UPDATE_INTERVAL_MS);
  fakeNow(now);
  // The first aborted session checkpoint must take place right after the initialisation.
  Assert.ok(!!schedulerTickCallback);
  // Execute one scheduler tick.
  await schedulerTickCallback();
  // Check that the aborted session is due at the correct time.
  Assert.ok(
    await IOUtils.exists(ABORTED_FILE),
    "There must be an aborted session ping."
  );

  // This ping is not yet in the pending pings folder, so we can't access it using
  // TelemetryStorage.popPendingPings().
  let abortedSessionPing = await IOUtils.readJSON(ABORTED_FILE);

  // Validate the ping.
  checkPingFormat(abortedSessionPing, PING_TYPE_MAIN, true, true);
  Assert.equal(abortedSessionPing.payload.info.reason, REASON_ABORTED_SESSION);

  // Trigger a another aborted-session ping and check that it overwrites the previous one.
  now = futureDate(now, ABORTED_SESSION_UPDATE_INTERVAL_MS);
  fakeNow(now);
  await schedulerTickCallback();

  let updatedAbortedSessionPing = await IOUtils.readJSON(ABORTED_FILE);
  checkPingFormat(updatedAbortedSessionPing, PING_TYPE_MAIN, true, true);
  Assert.equal(
    updatedAbortedSessionPing.payload.info.reason,
    REASON_ABORTED_SESSION
  );
  Assert.notEqual(abortedSessionPing.id, updatedAbortedSessionPing.id);
  Assert.notEqual(
    abortedSessionPing.creationDate,
    updatedAbortedSessionPing.creationDate
  );

  await TelemetryController.testShutdown();
  Assert.ok(
    !(await IOUtils.exists(ABORTED_FILE)),
    "No aborted session ping must be available after a shutdown."
  );
});

add_task(async function test_abortedSession_Shutdown() {
  if (gIsAndroid) {
    // We don't have the aborted session ping here.
    return;
  }

  const ABORTED_FILE = PathUtils.join(
    DATAREPORTING_PATH,
    ABORTED_PING_FILE_NAME
  );

  let schedulerTickCallback = null;
  let now = fakeNow(2040, 1, 1, 0, 0, 0);
  // Fake scheduler functions to control aborted-session flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  Assert.ok(
    await IOUtils.exists(DATAREPORTING_PATH),
    "Telemetry must create the aborted session directory when starting."
  );

  // Fake now again so that the scheduled aborted-session save takes place.
  fakeNow(futureDate(now, ABORTED_SESSION_UPDATE_INTERVAL_MS));
  // The first aborted session checkpoint must take place right after the initialisation.
  Assert.ok(!!schedulerTickCallback);
  // Execute one scheduler tick.
  await schedulerTickCallback();
  // Check that the aborted session is due at the correct time.
  Assert.ok(
    await IOUtils.exists(ABORTED_FILE),
    "There must be an aborted session ping."
  );

  // Remove the aborted session file and then shut down to make sure exceptions (e.g file
  // not found) do not compromise the shutdown.
  await IOUtils.remove(ABORTED_FILE);

  await TelemetryController.testShutdown();
});

add_task(async function test_abortedDailyCoalescing() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  const ABORTED_FILE = PathUtils.join(
    DATAREPORTING_PATH,
    ABORTED_PING_FILE_NAME
  );

  // Make sure the aborted sessions directory does not exist to test its creation.
  await IOUtils.remove(DATAREPORTING_PATH, {
    ignoreAbsent: true,
    recursive: true,
  });

  let schedulerTickCallback = null;
  PingServer.clearRequests();

  let nowDate = new Date(2009, 10, 18, 0, 0, 0);
  fakeNow(nowDate);

  // Fake scheduler functions to control aborted-session flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();
  await TelemetryController.testReset();

  Assert.ok(
    await IOUtils.exists(DATAREPORTING_PATH),
    "Telemetry must create the aborted session directory when starting."
  );

  // Delay the callback around midnight so that the aborted-session ping gets merged with the
  // daily ping.
  let dailyDueDate = futureDate(nowDate, MS_IN_ONE_DAY);
  fakeNow(dailyDueDate);
  // Trigger both the daily ping and the saved-session.
  Assert.ok(!!schedulerTickCallback);
  // Execute one scheduler tick.
  await schedulerTickCallback();

  // Wait for the daily ping.
  let dailyPing = await PingServer.promiseNextPing();
  Assert.equal(dailyPing.payload.info.reason, REASON_DAILY);

  // Check that an aborted session ping was also written to disk.
  Assert.ok(
    await IOUtils.exists(ABORTED_FILE),
    "There must be an aborted session ping."
  );

  // Read aborted session ping and check that the session/subsession ids equal the
  // ones in the daily ping.
  let abortedSessionPing = await IOUtils.readJSON(ABORTED_FILE);
  Assert.equal(
    abortedSessionPing.payload.info.sessionId,
    dailyPing.payload.info.sessionId
  );
  Assert.equal(
    abortedSessionPing.payload.info.subsessionId,
    dailyPing.payload.info.subsessionId
  );

  await TelemetryController.testShutdown();
});

add_task(async function test_schedulerComputerSleep() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  const ABORTED_FILE = PathUtils.join(
    DATAREPORTING_PATH,
    ABORTED_PING_FILE_NAME
  );

  await TelemetryController.testReset();
  await TelemetryController.testShutdown();
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  // Remove any aborted-session ping from the previous tests.
  await IOUtils.remove(DATAREPORTING_PATH, {
    ignoreAbsent: true,
    recursive: true,
  });

  // Set a fake current date and start Telemetry.
  let nowDate = fakeNow(2009, 10, 18, 0, 0, 0);
  let schedulerTickCallback = null;
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  // Set the current time 3 days in the future at midnight, before running the callback.
  nowDate = fakeNow(futureDate(nowDate, 3 * MS_IN_ONE_DAY));
  Assert.ok(!!schedulerTickCallback);
  // Execute one scheduler tick.
  await schedulerTickCallback();

  let dailyPing = await PingServer.promiseNextPing();
  Assert.equal(
    dailyPing.payload.info.reason,
    REASON_DAILY,
    "The wake notification should have triggered a daily ping."
  );
  Assert.equal(
    dailyPing.creationDate,
    nowDate.toISOString(),
    "The daily ping date should be correct."
  );

  Assert.ok(
    await IOUtils.exists(ABORTED_FILE),
    "There must be an aborted session ping."
  );

  // Now also test if we are sending a daily ping if we wake up on the next
  // day even when the timer doesn't trigger.
  // This can happen due to timeouts not running out during sleep times,
  // see bug 1262386, bug 1204823 et al.
  // Note that we don't get wake notifications on Linux due to bug 758848.
  nowDate = fakeNow(futureDate(nowDate, 1 * MS_IN_ONE_DAY));

  // We emulate the mentioned timeout behavior by sending the wake notification
  // instead of triggering the timeout callback.
  // This should trigger a daily ping, because we passed midnight.
  Services.obs.notifyObservers(null, "wake_notification");

  dailyPing = await PingServer.promiseNextPing();
  Assert.equal(
    dailyPing.payload.info.reason,
    REASON_DAILY,
    "The wake notification should have triggered a daily ping."
  );
  Assert.equal(
    dailyPing.creationDate,
    nowDate.toISOString(),
    "The daily ping date should be correct."
  );

  await TelemetryController.testShutdown();
});

add_task(async function test_schedulerEnvironmentReschedules() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  // Reset the test preference.
  const PREF_TEST = "toolkit.telemetry.test.pref1";
  Services.prefs.clearUserPref(PREF_TEST);
  const PREFS_TO_WATCH = new Map([
    [PREF_TEST, { what: TelemetryEnvironment.RECORD_PREF_VALUE }],
  ]);

  await TelemetryController.testReset();
  await TelemetryController.testShutdown();
  await TelemetryStorage.testClearPendingPings();
  // bug 1829855 - Sometimes the idle dispatch from a previous test interferes.
  TelemetryScheduler.testReset();
  PingServer.clearRequests();

  // Set a fake current date and start Telemetry.
  let nowDate = fakeNow(2060, 10, 18, 0, 0, 0);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  let schedulerTickCallback = null;
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();
  await TelemetryEnvironment.testWatchPreferences(PREFS_TO_WATCH);

  // Set the current time at midnight.
  fakeNow(futureDate(nowDate, MS_IN_ONE_DAY));
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );

  // Trigger the environment change.
  Services.prefs.setIntPref(PREF_TEST, 1);

  // Wait for the environment-changed ping.
  let ping = await PingServer.promiseNextPing();
  Assert.equal(ping.type, "main", `Expected 'main' ping on ${ping.id}`);
  Assert.equal(
    ping.payload.info.reason,
    "environment-change",
    `Expected 'environment-change' reason on ${ping.id}`
  );

  // We don't expect to receive any daily ping in this test, so assert if we do.
  PingServer.registerPingHandler(req => {
    const receivedPing = decodeRequestPayload(req);
    Assert.ok(
      false,
      `No ping should be received in this test (got ${receivedPing.id} type: ${receivedPing.type} reason: ${receivedPing.payload.info.reason}).`
    );
  });

  // Execute one scheduler tick. It should not trigger a daily ping.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();
  await TelemetryController.testShutdown();
});

add_task(async function test_schedulerNothingDue() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  const ABORTED_FILE = PathUtils.join(
    DATAREPORTING_PATH,
    ABORTED_PING_FILE_NAME
  );

  // Remove any aborted-session ping from the previous tests.
  await IOUtils.remove(DATAREPORTING_PATH, {
    ignoreAbsent: true,
    recursive: true,
  });
  await TelemetryStorage.testClearPendingPings();
  await TelemetryController.testReset();

  // We don't expect to receive any ping in this test, so assert if we do.
  PingServer.registerPingHandler(req => {
    const receivedPing = decodeRequestPayload(req);
    Assert.ok(
      false,
      `No ping should be received in this test (got ${receivedPing.id}).`
    );
  });

  // Set a current date/time away from midnight, so that the daily ping doesn't get
  // sent.
  let nowDate = new Date(2009, 10, 18, 11, 0, 0);
  fakeNow(nowDate);
  let schedulerTickCallback = null;
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  // Delay the callback execution to a time when no ping should be due.
  let nothingDueDate = futureDate(
    nowDate,
    ABORTED_SESSION_UPDATE_INTERVAL_MS / 2
  );
  fakeNow(nothingDueDate);
  Assert.ok(!!schedulerTickCallback);
  // Execute one scheduler tick.
  await schedulerTickCallback();

  // Check that no aborted session ping was written to disk.
  Assert.ok(!(await IOUtils.exists(ABORTED_FILE)));

  await TelemetryController.testShutdown();
  PingServer.resetPingHandler();
});

add_task(async function test_pingExtendedStats() {
  const EXTENDED_PAYLOAD_FIELDS = [
    "log",
    "slowSQL",
    "fileIOReports",
    "lateWrites",
    "addonDetails",
  ];

  // Reset telemetry and disable sending extended statistics.
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();
  await TelemetryController.testReset();
  Telemetry.canRecordExtended = false;

  await sendPing();

  let ping = await PingServer.promiseNextPing();
  checkPingFormat(ping, PING_TYPE_MAIN, true, true);

  // Check that the payload does not contain extended statistics fields.
  for (let f in EXTENDED_PAYLOAD_FIELDS) {
    Assert.ok(
      !(EXTENDED_PAYLOAD_FIELDS[f] in ping.payload),
      EXTENDED_PAYLOAD_FIELDS[f] +
        " must not be in the payload if the extended set is off."
    );
  }

  // We check this one separately so that we can reuse EXTENDED_PAYLOAD_FIELDS below, since
  // slowSQLStartup might not be there.
  Assert.ok(
    !("slowSQLStartup" in ping.payload),
    "slowSQLStartup must not be sent if the extended set is off"
  );

  Assert.ok(
    !("addonManager" in ping.payload.simpleMeasurements),
    "addonManager must not be sent if the extended set is off."
  );
  Assert.ok(
    !("UITelemetry" in ping.payload.simpleMeasurements),
    "UITelemetry must not be sent."
  );

  // Restore the preference.
  Telemetry.canRecordExtended = true;

  // Send a new ping that should contain the extended data.
  await sendPing();
  ping = await PingServer.promiseNextPing();
  checkPingFormat(ping, PING_TYPE_MAIN, true, true);

  // Check that the payload now contains extended statistics fields.
  for (let f in EXTENDED_PAYLOAD_FIELDS) {
    Assert.ok(
      EXTENDED_PAYLOAD_FIELDS[f] in ping.payload,
      EXTENDED_PAYLOAD_FIELDS[f] +
        " must be in the payload if the extended set is on."
    );
  }

  Assert.ok(
    "addonManager" in ping.payload.simpleMeasurements,
    "addonManager must be sent if the extended set is on."
  );
  Assert.ok(
    !("UITelemetry" in ping.payload.simpleMeasurements),
    "UITelemetry must not be sent."
  );

  await TelemetryController.testShutdown();
});

add_task(async function test_schedulerUserIdle() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  const SCHEDULER_TICK_INTERVAL_MS = 5 * 60 * 1000;
  const SCHEDULER_TICK_IDLE_INTERVAL_MS = 60 * 60 * 1000;

  let now = new Date(2010, 1, 1, 11, 0, 0);
  fakeNow(now);

  let schedulerTimeout = 0;
  fakeSchedulerTimer(
    (callback, timeout) => {
      schedulerTimeout = timeout;
    },
    () => {}
  );
  await TelemetryController.testReset();
  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  // When not idle, the scheduler should have a 5 minutes tick interval.
  Assert.equal(schedulerTimeout, SCHEDULER_TICK_INTERVAL_MS);

  // Send an "idle" notification to the scheduler.
  fakeIdleNotification("idle");

  // When idle, the scheduler should have a 1hr tick interval.
  Assert.equal(schedulerTimeout, SCHEDULER_TICK_IDLE_INTERVAL_MS);

  // Send an "active" notification to the scheduler.
  await fakeIdleNotification("active");

  // When user is back active, the scheduler tick should be 5 minutes again.
  Assert.equal(schedulerTimeout, SCHEDULER_TICK_INTERVAL_MS);

  // We should not miss midnight when going to idle.
  now.setHours(23);
  now.setMinutes(50);
  fakeNow(now);
  fakeIdleNotification("idle");
  Assert.equal(schedulerTimeout, 10 * 60 * 1000);

  await TelemetryController.testShutdown();
});

add_task(async function test_DailyDueAndIdle() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  await TelemetryStorage.testClearPendingPings();
  PingServer.clearRequests();

  let receivedPingRequest = null;
  // Register a ping handler that will assert when receiving multiple daily pings.
  PingServer.registerPingHandler(req => {
    Assert.ok(!receivedPingRequest, "Telemetry must only send one daily ping.");
    receivedPingRequest = req;
  });

  // Faking scheduler timer has to happen before resetting TelemetryController
  // to be effective.
  let schedulerTickCallback = null;
  let now = new Date(2030, 1, 1, 0, 0, 0);
  fakeNow(now);
  // Fake scheduler functions to control daily collection flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryController.testReset();

  // Trigger the daily ping.
  let firstDailyDue = new Date(2030, 1, 2, 0, 0, 0);
  fakeNow(firstDailyDue);

  // Run a scheduler tick: it should trigger the daily ping.
  Assert.ok(!!schedulerTickCallback);
  let tickPromise = schedulerTickCallback();

  // Send an idle and then an active user notification.
  fakeIdleNotification("idle");
  fakeIdleNotification("active");

  // Wait on the tick promise.
  await tickPromise;

  await TelemetrySend.testWaitOnOutgoingPings();

  // Decode the ping contained in the request and check that's a daily ping.
  Assert.ok(receivedPingRequest, "Telemetry must send one daily ping.");
  const receivedPing = decodeRequestPayload(receivedPingRequest);
  checkPingFormat(receivedPing, PING_TYPE_MAIN, true, true);
  Assert.equal(receivedPing.payload.info.reason, REASON_DAILY);

  await TelemetryController.testShutdown();
});

add_task(async function test_userIdleAndSchedlerTick() {
  if (gIsAndroid) {
    // We don't have the aborted session or the daily ping here.
    return;
  }

  let receivedPingRequest = null;
  // Register a ping handler that will assert when receiving multiple daily pings.
  PingServer.registerPingHandler(req => {
    Assert.ok(!receivedPingRequest, "Telemetry must only send one daily ping.");
    receivedPingRequest = req;
  });

  let schedulerTickCallback = null;
  let now = new Date(2030, 1, 1, 0, 0, 0);
  fakeNow(now);
  // Fake scheduler functions to control daily collection flow in tests.
  fakeSchedulerTimer(
    callback => (schedulerTickCallback = callback),
    () => {}
  );
  await TelemetryStorage.testClearPendingPings();
  await TelemetryController.testReset();
  PingServer.clearRequests();

  // Move the current date/time to midnight.
  let firstDailyDue = new Date(2030, 1, 2, 0, 0, 0);
  fakeNow(firstDailyDue);

  // The active notification should trigger a scheduler tick. The latter will send the
  // due daily ping.
  fakeIdleNotification("active");

  // Immediately running another tick should not send a daily ping again.
  Assert.ok(!!schedulerTickCallback);
  await schedulerTickCallback();

  // A new "idle" notification should not send a new daily ping.
  fakeIdleNotification("idle");

  await TelemetrySend.testWaitOnOutgoingPings();

  // Decode the ping contained in the request and check that's a daily ping.
  Assert.ok(receivedPingRequest, "Telemetry must send one daily ping.");
  const receivedPing = decodeRequestPayload(receivedPingRequest);
  checkPingFormat(receivedPing, PING_TYPE_MAIN, true, true);
  Assert.equal(receivedPing.payload.info.reason, REASON_DAILY);

  PingServer.resetPingHandler();
  await TelemetryController.testShutdown();
});

add_task(async function test_changeThrottling() {
  if (gIsAndroid) {
    // We don't support subsessions yet on Android.
    return;
  }

  let getSubsessionCount = () => {
    return TelemetrySession.getPayload().info.subsessionCounter;
  };

  let now = fakeNow(2050, 1, 2, 0, 0, 0);
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 10 * MILLISECONDS_PER_MINUTE
  );
  await TelemetryController.testReset();
  Assert.equal(getSubsessionCount(), 1);

  // The first pref change should not trigger a notification.
  TelemetrySession.testOnEnvironmentChange("test", {});
  Assert.equal(getSubsessionCount(), 1);

  // We should get a change notification after the 5min throttling interval.
  fakeNow(futureDate(now, 5 * MILLISECONDS_PER_MINUTE + 1));
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 5 * MILLISECONDS_PER_MINUTE + 1
  );
  TelemetrySession.testOnEnvironmentChange("test", {});
  Assert.equal(getSubsessionCount(), 2);

  // After that, changes should be throttled again.
  now = fakeNow(futureDate(now, 1 * MILLISECONDS_PER_MINUTE));
  gMonotonicNow = fakeMonotonicNow(gMonotonicNow + 1 * MILLISECONDS_PER_MINUTE);
  TelemetrySession.testOnEnvironmentChange("test", {});
  Assert.equal(getSubsessionCount(), 2);

  // ... for 5min.
  now = fakeNow(futureDate(now, 4 * MILLISECONDS_PER_MINUTE + 1));
  gMonotonicNow = fakeMonotonicNow(
    gMonotonicNow + 4 * MILLISECONDS_PER_MINUTE + 1
  );
  TelemetrySession.testOnEnvironmentChange("test", {});
  Assert.equal(getSubsessionCount(), 3);
});

add_task(async function stopServer() {
  // It is important to shut down the TelemetryController first as, due to test
  // environment changes, failure to upload pings here during shutdown results
  // in an infinite loop of send failures and retries.
  await TelemetryController.testShutdown();
  await PingServer.stop();
});
