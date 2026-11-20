/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

const { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const { NimbusTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NimbusTestUtils.sys.mjs"
);

const { DAPIncrementality } = ChromeUtils.importESModule(
  "resource://gre/modules/DAPIncrementality.sys.mjs"
);

NimbusTestUtils.init(this);

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

const PREF_LEADER = "toolkit.telemetry.dap.leader.url";
const PREF_HELPER = "toolkit.telemetry.dap.helper.url";

const TRANSITION_TYPED = PlacesUtils.history.TRANSITION_TYPED;

const TASK_ID = "o-91EcR2kfxfAmkKPPHifXKqiH7Upm0Ilw5joB3L_pE";

let server;
let server_addr;

// The dummy test server will record report sizes in this list.
let server_requests = [];

function uploadHandler(request, response) {
  Assert.equal(
    request.getHeader("Content-Type"),
    "application/dap-report",
    "Wrong Content-Type header."
  );

  let body = new BinaryInputStream(request.bodyInputStream);
  server_requests.push(body.available());
  response.setStatusLine(request.httpVersion, 200);
}

function resetServerRequests() {
  server_requests.length = 0;
}

add_setup(async function () {
  do_get_profile();
  // Set up a mock server to represent the DAP endpoints.
  server = new HttpServer();
  server.registerPrefixHandler("/leader_endpoint/tasks/", uploadHandler);

  server.start(-1);
  const i = server.identity;
  server_addr = i.primaryScheme + "://" + i.primaryHost + ":" + i.primaryPort;

  Services.prefs.setStringPref(PREF_LEADER, server_addr + "/leader_endpoint");
  Services.prefs.setStringPref(PREF_HELPER, server_addr + "/helper_endpoint");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(PREF_LEADER);
    Services.prefs.clearUserPref(PREF_HELPER);

    return new Promise(resolve => {
      server.stop(resolve);
    });
  });
});

function openSubCapDatabase() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open("SubmissionCap", 1);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

function openIncrDatabase() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open("DAPIncrementality", 1);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

function countRecords(store) {
  return new Promise((resolve, reject) => {
    const request = store.count();
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function getFreqCapCount(db) {
  const tx = db.transaction("freq_caps", "readonly");
  const store = tx.objectStore("freq_caps");
  const count = await countRecords(store);
  await tx.done;
  return count;
}

async function getReportCount(db) {
  const tx = db.transaction("reports", "readonly");
  const store = tx.objectStore("reports");
  const count = await countRecords(store);
  await tx.done;
  return count;
}

async function getReport(db, taskId) {
  const tx = db.transaction("reports", "readonly");
  const store = await tx.objectStore("reports");
  return new Promise((resolve, reject) => {
    const request = store.get(taskId);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function getReferrerState(db, taskId) {
  const tx = db.transaction("referrer", "readonly");
  const store = await tx.objectStore("referrer");
  return new Promise((resolve, reject) => {
    const request = store.get(taskId);

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testVisitMeasurementNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    // Enroll in experiment to count 1 url
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "visitMeasurement",
        taskId: TASK_ID,
        length: 2,
        timePrecision: 3600,
        visitCountUrls: [
          {
            url: "*://*.example.com/",
            bucket: 0,
          },
          {
            url: "*://*.mozilla.org/",
            bucket: 1,
          },
        ],
      },
    });

    Assert.notStrictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should be active"
    );

    // Verify there are no pending reports to submit
    const subCapDb = await openSubCapDatabase();
    let numRecords = await getReportCount(subCapDb);
    Assert.equal(numRecords, 0, "Should be no pending reports");

    // Visit a url that does not match a pattern.
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      uri: NetUtil.newURI("http://www.mozilla.org/path"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify there are 0 pending reports to send
    numRecords = await getReportCount(subCapDb);
    Assert.equal(numRecords, 0, "Should be no pending reports");

    // Visit a url by typing in the url bar
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      uri: NetUtil.newURI("http://www.mozilla.org/"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify there is 1 pending report to send and measurement matches bucket value.
    numRecords = await getReportCount(subCapDb);
    Assert.equal(numRecords, 1, "Should be 1 pending report");

    // Trigger submission of the report
    await DAPIncrementality.dapReportContoller.submit(1000, "unit-test");

    // Verify there are 0 pending reports
    numRecords = await getReportCount(subCapDb);
    Assert.equal(numRecords, 0, "Should be no pending reports");

    // Verify submission capping is active
    numRecords = await getFreqCapCount(subCapDb);
    Assert.equal(numRecords, 1, "Should be 1 cap entry");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify freq cap cleanup after unenrollment
    numRecords = await getFreqCapCount(subCapDb);
    Assert.equal(numRecords, 0, "Should be 0 cap entries");

    // Verify pending report cleanup after unenrollment
    numRecords = await getReportCount(subCapDb);
    Assert.equal(numRecords, 0, "Should be no pending reports");

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [390, 390, 390],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist after unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testParsingMultiUrlVisitMeasurementNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    const expectedVisitCountUrls = [
      {
        url: "*://*.mozilla.org/",
        bucket: 0,
      },
      {
        url: "*://*.example.com/",
        bucket: 1,
      },
    ];

    // Enroll in experiment to count 2 urls
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "visitMeasurement",
        taskId: TASK_ID,
        length: 2,
        timePrecision: 3600,
        visitCountUrls: expectedVisitCountUrls,
      },
    });

    const toKey = ({ url, bucket }) => `${url}#${bucket}`;
    const expectedKeySorted = expectedVisitCountUrls.map(toKey).sort();
    const parsedKeySorted = DAPIncrementality.config.visitUrlPatterns
      .map(({ pattern, bucket }) => `${pattern.pattern}#${bucket}`)
      .sort();
    Assert.deepEqual(
      parsedKeySorted,
      expectedKeySorted,
      "patterns and buckets match"
    );

    // Trigger submission of the report
    await DAPIncrementality.dapReportContoller.submit(1000, "unit-test");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [390, 390, 390],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testReferrerMeasurementNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    // Enroll experiment with 1 referrer url and 1 target url
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "referrerMeasurement",
        taskId: TASK_ID,
        length: 3,
        timePrecision: 3600,
        referrerUrls: [
          {
            url: "*://*.mozilla.org/ref",
            bucket: 1,
          },
        ],
        targetUrls: "*://*.mozilla.org/target",
        unknownReferrerBucket: 2,
      },
    });

    // Visit neither referrer nor target Urls
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify no referrer state stored
    const incrDb = await openIncrDatabase();
    let state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(
      state,
      undefined,
      "No referrer state should be recorded"
    );

    // Visit the referrer url
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/ref"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify referrer state matches the bucket for the referred url
    state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(state.bucket, 1, "Referrer state should be 1");

    // Visit target url
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/target"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify the pending report matches the bucket for the referred url
    const subCapDb = await openSubCapDatabase();
    let pendingReport = await getReport(subCapDb, TASK_ID);
    Assert.equal(
      pendingReport.measurement,
      1,
      "Pending measurement value should be 1"
    );

    // Verify that the referrer state has been cleared
    state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(state, undefined, "Referrer state should be cleared");

    // Trigger submission of the report
    await DAPIncrementality.dapReportContoller.submit(1000, "unit-test");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [438, 438, 438],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testReferrerMeasurementParsingMultiTargetNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    // Enroll experiment with 1 referrer url and 2 target urls
    const targetUrls = "*://*.mozilla.org/target1,*://*.mozilla.org/target2";
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "referrerMeasurement",
        taskId: TASK_ID,
        length: 3,
        timePrecision: 3600,
        referrerUrls: [
          {
            url: "*://*.mozilla.org/ref",
            bucket: 1,
          },
        ],
        targetUrls,
        unknownReferrerBucket: 2,
      },
    });

    Assert.strictEqual(
      DAPIncrementality.config.targetUrlPatterns.length,
      2,
      "List should contain 2 urls patterns"
    );

    const missingUrls = DAPIncrementality.config.targetUrlPatterns.filter(
      s => !targetUrls.includes(s.pattern)
    );
    Assert.deepEqual(
      missingUrls,
      [],
      "All urls in the list should be found in the targetUrlPatterns"
    );

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [438, 438],
      "Should have one report on enrollment, second on unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testReferrerMeasurementUnknownBucketNotSetNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );
    // Enroll experiment with 1 referrer url and 1 target url without unknown bucket defined.
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "referrerMeasurement",
        taskId: TASK_ID,
        length: 3,
        timePrecision: 3600,
        referrerUrls: [
          {
            url: "*://*.mozilla.org/ref",
            bucket: 1,
          },
        ],
        targetUrls: "*://*.mozilla.org/target",
      },
    });

    // Visit target url without prior referrer visit
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/target"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify no referrer state is recorded
    const incrDb = await openIncrDatabase();
    let state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(
      state,
      undefined,
      "No referrer state should be recorded"
    );

    // Verify there is no pending report since there is no unknown bucket defined.
    const subCapDb = await openSubCapDatabase();
    let pendingReport = await getReport(subCapDb, TASK_ID);
    Assert.equal(
      pendingReport,
      undefined,
      "No pending report should be recorded"
    );

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [438, 438],
      "Should have one report on enrollment, second on unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testReferrerMeasurementUnknownBucketSet() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    // Enroll experiment with 1 referrer url and 1 target url
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "referrerMeasurement",
        taskId: TASK_ID,
        length: 3,
        timePrecision: 3600,
        referrerUrls: [
          {
            url: "*://*.mozilla.org/ref",
            bucket: 1,
          },
        ],
        targetUrls: "*://*.mozilla.org/target",
        unknownReferrerBucket: 2,
      },
    });

    // Visit neither referrer nor target Urls
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify no referrer state stored
    const incrDb = await openIncrDatabase();
    let state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(
      state,
      undefined,
      "No referrer state should be recorded"
    );

    // Visit target url without prior referrer visit
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/target"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify no referrer state stored
    state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(
      state,
      undefined,
      "No referrer state should be recorded"
    );

    // Verify there is a pending report to submit with the value for unknownReferrerBucket
    const subCapDb = await openSubCapDatabase();
    let pendingReport = await getReport(subCapDb, TASK_ID);
    Assert.equal(
      pendingReport.measurement,
      2,
      "Pending measurement value should be 2"
    );

    // Trigger submission of the report
    await DAPIncrementality.dapReportContoller.submit(1000, "unit-test");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [438, 438, 438],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );

    await cleanup();
  }
);

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testMultiReferrerMeasurementUnknownBucketNotSetNimbus() {
    resetServerRequests();
    const { cleanup } = await NimbusTestUtils.setupTest();
    await DAPIncrementality.startup();

    Assert.strictEqual(
      DAPIncrementality.dapReportContoller,
      null,
      "dapReportContoller should not exist before enrollment"
    );

    // Enroll experiment with 2 referrer urls and 1 target url without unknown bucket defined.
    const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapIncrementality",
      value: {
        measurementType: "referrerMeasurement",
        taskId: TASK_ID,
        length: 3,
        timePrecision: 3600,
        referrerUrls: [
          {
            url: "*://*.mozilla.org/ref",
            bucket: 1,
          },
          {
            url: "*://*.mozilla.org/ref2",
            bucket: 2,
          },
        ],
        targetUrls: "*://*.mozilla.org/target",
      },
    });

    // Visit first referrer url
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/ref"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify that referrer state matches the specifed bucket for the first url
    const incrDb = await openIncrDatabase();
    let state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(state.bucket, 1, "Referrer state should be 1");

    // Visit second referrer url
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/ref2"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify that referrer state matches the specifed bucket for the second url
    state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(state.bucket, 2, "Referrer state should be 2");

    // Visit target url
    timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      url: NetUtil.newURI("http://www.mozilla.org/target"),
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    // Verify the pending report measurement is 2
    const subCapDb = await openSubCapDatabase();
    let pendingReport = await getReport(subCapDb, TASK_ID);
    Assert.equal(
      pendingReport.measurement,
      2,
      "Pending measurement value should be 2"
    );

    // Verify referrer state is cleard
    state = await getReferrerState(incrDb, TASK_ID);
    Assert.strictEqual(state, undefined, "Referrer state should be cleared");

    // Trigger submission of the report
    await DAPIncrementality.dapReportContoller.submit(1000, "unit-test");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPIncrementality to flush",
      () => DAPIncrementality.config === null
    );

    // Verify server requests
    Assert.deepEqual(
      server_requests,
      [438, 438, 438],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );

    await cleanup();
  }
);
