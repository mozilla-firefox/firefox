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

NimbusTestUtils.init(this);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  DAPVisitCounter: "resource://gre/modules/DAPVisitCounter.sys.mjs",
});

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

const PREF_LEADER = "toolkit.telemetry.dap.leader.url";
const PREF_HELPER = "toolkit.telemetry.dap.helper.url";

const TRANSITION_TYPED = PlacesUtils.history.TRANSITION_TYPED;
let server;
let server_addr;

// The dummy test server will record report sizes in this list.
let server_requests = [];

const task = {
  id: "o-91EcR2kfxfAmkKPPHifXKqiH7Upm0Ilw5joB3L_pE",
  vdaf: "histogram",
  length: 30,
  time_precision: 300,
};
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

function openDatabase() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open("SubmissionCap", 1);

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

async function getReportCount(db) {
  const tx = db.transaction("reports", "readonly");
  const store = tx.objectStore("reports");
  const count = await countRecords(store);
  await tx.done;
  return count;
}

async function getFreqCapCount(db) {
  const tx = db.transaction("freq_caps", "readonly");
  const store = tx.objectStore("freq_caps");
  const count = await countRecords(store);
  await tx.done;
  return count;
}

add_task(
  {
    // Requires Nimbus.
    skip_if: () => AppConstants.MOZ_BUILD_APP !== "browser",
  },
  async function testVisitCounterNimbus() {
    const { cleanup } = await NimbusTestUtils.setupTest();
    await lazy.DAPVisitCounter.startup();

    Assert.strictEqual(
      lazy.DAPVisitCounter.dapReportController,
      null,
      "dapReportController should not exist before enrollment"
    );

    // Enroll experiment
    let doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
      featureId: "dapTelemetry",
      value: {
        enabled: true,
        visitCountingEnabled: true,
        visitCountingExperimentList: [
          {
            task_id: task.id,
            urls: ["*://*.mozilla.org/", "*://*.example.com/"],
            task_veclen: 30,
            bucket: 0,
          },
        ],
      },
    });

    Assert.notStrictEqual(
      lazy.DAPVisitCounter.dapReportController,
      null,
      "dapReportController should be active"
    );

    // Verify there are no pending reports to submit
    const db = await openDatabase();
    let numRecords = await getReportCount(db);
    Assert.equal(numRecords, 0, "Should be no pending reports");

    // Visit a URL
    let uri = NetUtil.newURI("http://www.mozilla.org/");
    let timestamp = Date.now() * 1000;
    await PlacesTestUtils.addVisits({
      uri,
      transition: TRANSITION_TYPED,
      visitDate: timestamp,
    });

    //Verify there is 1 pending report to send
    numRecords = await getReportCount(db);
    Assert.equal(numRecords, 1, "Should be 1 pending report");

    // Trigger submission of the report
    await lazy.DAPVisitCounter.dapReportController.submit(1000, "unit-test");

    // Verify there are 0 pending reports
    numRecords = await getReportCount(db);
    Assert.equal(numRecords, 0, "Should be 0 pending reports");

    // Verify submission capping is active
    numRecords = await getFreqCapCount(db);
    Assert.equal(numRecords, 1, "Should be 1 cap entry");

    // Unenroll experiment
    await doExperimentCleanup();

    Services.tm.spinEventLoopUntil(
      "Wait for DAPVisitCounter to flush",
      () => lazy.DAPVisitCounter.counters === null
    );

    //Verify IndexDB Cleanup after unenrollment
    numRecords = await getFreqCapCount(db);
    Assert.equal(numRecords, 0, "Should be 0 cap entries");

    //Report
    numRecords = await getReportCount(db);
    Assert.equal(numRecords, 0, "Should be 0 pending reports");

    // The 2 server requests are:
    // 1.  The first request is submitted on enrollment
    // 2.  The second request is force submitted
    // 2.  The third is submitted on unenrollment
    Assert.deepEqual(
      server_requests,
      [1126, 1126, 1126],
      "Should have one report on enrollment, second for triggered submission, third on unenrollment"
    );
    Assert.strictEqual(
      lazy.DAPVisitCounter.dapReportController,
      null,
      "dapReportController should not exist after unenrollment"
    );

    await cleanup();
  }
);
