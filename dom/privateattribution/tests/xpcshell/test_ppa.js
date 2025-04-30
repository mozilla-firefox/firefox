/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

const { PrivateAttributionService } = ChromeUtils.importESModule(
  "resource://gre/modules/PrivateAttributionService.sys.mjs"
);

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);

const PREF_LEADER = "toolkit.telemetry.dap.leader.url";
const PREF_HELPER = "toolkit.telemetry.dap.helper.url";
const TASK_ID = "DSZGMFh26hBYXNaKvhL_N4AHA3P5lDn19on1vFPBxJM";
const MAX_CONVERSIONS = 2;
const DAY_IN_MILLI = 1000 * 60 * 60 * 24;
const LOOKBACK_DAYS = 1;
const HISTOGRAM_SIZE = 5;

class MockDateProvider {
  constructor() {
    this._now = Date.now();
  }

  now() {
    return this._now;
  }

  add(interval_ms) {
    this._now += interval_ms;
  }
}

class MockDAPTelemetrySender {
  constructor() {
    this.receivedMeasurements = [];
  }

  async sendDAPMeasurement(task, measurement, { timeout }) {
    this.receivedMeasurements.push({
      task,
      measurement,
      timeout,
    });
  }
}

class MockServer {
  constructor() {
    this.receivedReports = [];

    const server = new HttpServer();

    server.registerPrefixHandler(
      "/leader_endpoint/tasks/",
      this.uploadHandler.bind(this)
    );

    this._server = server;
  }

  start() {
    this._server.start(-1);

    this.orig_leader = Services.prefs.getStringPref(PREF_LEADER);
    this.orig_helper = Services.prefs.getStringPref(PREF_HELPER);

    const i = this._server.identity;
    const serverAddr =
      i.primaryScheme + "://" + i.primaryHost + ":" + i.primaryPort;
    Services.prefs.setStringPref(PREF_LEADER, serverAddr + "/leader_endpoint");
    Services.prefs.setStringPref(PREF_HELPER, serverAddr + "/helper_endpoint");
  }

  async stop() {
    Services.prefs.setStringPref(PREF_LEADER, this.orig_leader);
    Services.prefs.setStringPref(PREF_HELPER, this.orig_helper);

    await this._server.stop();
  }

  uploadHandler(request, response) {
    let body = new BinaryInputStream(request.bodyInputStream);

    this.receivedReports.push({
      contentType: request.getHeader("Content-Type"),
      size: body.available(),
    });

    response.setStatusLine(request.httpVersion, 200);
  }
}

add_setup(async function () {
  do_get_profile();
  Services.fog.initializeFOG();
});

add_task(async function testIsEnabled() {
  Services.fog.testResetFOG();

  // isEnabled should match the telemetry preference without the test flag
  const ppa1 = new PrivateAttributionService();
  Assert.equal(
    ppa1.isEnabled(),
    AppConstants.MOZ_TELEMETRY_REPORTING &&
      Services.prefs.getBoolPref("datareporting.healthreport.uploadEnabled") &&
      Services.prefs.getBoolPref("dom.private-attribution.submission.enabled")
  );

  // Should always be enabled with the test flag
  const ppa2 = new PrivateAttributionService({ testForceEnabled: true });
  Assert.ok(ppa2.isEnabled());
});

add_task(async function testSuccessfulConversion() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source.test";
  const targetHost = "target.test";
  const adIdentifier = "ad_identifier";
  const adIndex = 1;

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "click",
    adIndex,
    adIdentifier,
    targetHost
  );

  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  const expectedMeasurement = {
    task: {
      id: TASK_ID,
      vdaf: "sumvec",
      bits: 8,
      length: HISTOGRAM_SIZE,
      time_precision: 60,
    },
    measurement: [0, 1, 0, 0, 0],
    timeout: 30000,
  };

  const receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement, expectedMeasurement);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testImpressionsOnMultipleSites() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const mockDateProvider = new MockDateProvider();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    dateProvider: mockDateProvider,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost1 = "source-multiple-1.test";
  const adIndex1 = 1;

  const sourceHost2 = "source-multiple-2.test";
  const adIndex2 = 3;

  const targetHost = "target-multiple.test";
  const adIdentifier = "ad_identifier_multiple";

  // View at adIndex1 on sourceHost1 at t=0
  await privateAttribution.onAttributionEvent(
    sourceHost1,
    "view",
    adIndex1,
    adIdentifier,
    targetHost
  );

  // Click at adIndex2 on sourceHost2 at t=1
  mockDateProvider.add(1);
  await privateAttribution.onAttributionEvent(
    sourceHost2,
    "click",
    adIndex2,
    adIdentifier,
    targetHost
  );

  // Click at adIndex1 on sourceHost1 at t=2
  mockDateProvider.add(1);
  await privateAttribution.onAttributionEvent(
    sourceHost1,
    "click",
    adIndex1,
    adIdentifier,
    targetHost
  );

  // View at adIndex2 on sourceHost2 at t=3
  mockDateProvider.add(1);
  await privateAttribution.onAttributionEvent(
    sourceHost2,
    "view",
    adIndex2,
    adIdentifier,
    targetHost
  );

  // Conversion for "click" matches most recent click on sourceHost1
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "click",
    [adIdentifier],
    [sourceHost1, sourceHost2]
  );

  let receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 1, 0, 0, 0]);

  // Conversion for "view" matches most recent view on sourceHost2
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost1, sourceHost2]
  );

  receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 1, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testConversionWithoutImpression() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-no-impression.test";
  const targetHost = "target-no-impression.test";
  const adIdentifier = "ad_identifier_no_impression";

  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  const receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 0, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testSelectionByInteractionType() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-by-type.test";
  const targetHost = "target-by-type.test";
  const adIdentifier = "ad_identifier_by_type";

  const viewAdIndex = 2;
  const clickAdIndex = 3;

  // View at viewAdIndex
  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    viewAdIndex,
    adIdentifier,
    targetHost
  );

  // Click at clickAdIndex clobbers previous view at viewAdIndex
  await privateAttribution.onAttributionEvent(
    sourceHost,
    "click",
    clickAdIndex,
    adIdentifier,
    targetHost
  );

  // Conversion filtering for "view" matches the interaction at clickAdIndex
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  let receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 1, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testSelectionBySourceSite() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost1 = "source-by-site-1.test";
  const adIndex1 = 1;

  const sourceHost2 = "source-by-site-2.test";
  const adIndex2 = 4;

  const targetHost = "target-by-site.test";
  const adIdentifier = "ad_identifier_by_site";

  // Impression on sourceHost1 at adIndex1
  await privateAttribution.onAttributionEvent(
    sourceHost1,
    "view",
    adIndex1,
    adIdentifier,
    targetHost
  );

  // Impression for the same ad ID on sourceHost2 at adIndex2
  await privateAttribution.onAttributionEvent(
    sourceHost2,
    "view",
    adIndex2,
    adIdentifier,
    targetHost
  );

  // Conversion filtering for sourceHost1 matches impression at adIndex1
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost1]
  );

  let receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 1, 0, 0, 0]);

  // Conversion filtering for sourceHost2 matches impression at adIndex2
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost2]
  );

  receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 0, 1]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testSelectionByAdIdentifier() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-by-ad-id.test";
  const targetHost = "target-by-ad-id.test";

  const adIdentifier1 = "ad_identifier_1";
  const adIndex1 = 1;

  const adIdentifier2 = "ad_identifier_2";
  const adIndex2 = 2;

  // Impression for adIdentifier1 at adIndex1
  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex1,
    adIdentifier1,
    targetHost
  );

  // Impression for adIdentifier2 at adIndex2
  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex2,
    adIdentifier2,
    targetHost
  );

  // Conversion filtering for adIdentifier1 matches impression at adIndex1
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier1],
    [sourceHost]
  );

  let receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 1, 0, 0, 0]);

  // Conversion filtering for adIdentifier2 matches impression at adIndex2
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier2],
    [sourceHost]
  );

  receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 1, 0, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testExpiredImpressions() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const mockDateProvider = new MockDateProvider();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    dateProvider: mockDateProvider,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-expired.test";
  const targetHost = "target-expired.test";
  const adIdentifier = "ad_identifier_expired";
  const adIndex = 2;

  // Register impression
  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  // Fast-forward time by LOOKBACK_DAYS days + 1 ms
  mockDateProvider.add(LOOKBACK_DAYS * DAY_IN_MILLI + 1);

  // Conversion doesn't match expired impression
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  const receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 0, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testConversionBudget() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-budget.test";
  const targetHost = "target-budget.test";
  const adIdentifier = "ad_identifier_budget";
  const adIndex = 3;

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  // Measurements uploaded for conversions up to MAX_CONVERSIONS
  for (let i = 0; i < MAX_CONVERSIONS; i++) {
    await privateAttribution.onAttributionConversion(
      targetHost,
      TASK_ID,
      HISTOGRAM_SIZE,
      LOOKBACK_DAYS,
      "view",
      [adIdentifier],
      [sourceHost]
    );

    const receivedMeasurement = mockSender.receivedMeasurements.pop();
    Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 1, 0]);
  }

  // Empty report uploaded on subsequent conversions
  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  const receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 0, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testHistogramSize() {
  Services.fog.testResetFOG();

  const mockSender = new MockDAPTelemetrySender();
  const privateAttribution = new PrivateAttributionService({
    dapTelemetrySender: mockSender,
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-histogram.test";
  const targetHost = "target-histogram.test";
  const adIdentifier = "ad_identifier_histogram";

  // Zero-based ad index is equal to histogram size, pushing the measurement out of the histogram's bounds
  const adIndex = HISTOGRAM_SIZE;

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  const receivedMeasurement = mockSender.receivedMeasurements.pop();
  Assert.deepEqual(receivedMeasurement.measurement, [0, 0, 0, 0, 0]);

  Assert.equal(mockSender.receivedMeasurements.length, 0);
});

add_task(async function testWithRealDAPSender() {
  Services.fog.testResetFOG();

  // Omit mocking DAP telemetry sender in this test to defend against mock
  // sender getting out of sync
  const mockServer = new MockServer();
  mockServer.start();

  const privateAttribution = new PrivateAttributionService({
    testForceEnabled: true,
    testDapOptions: { ohttp_relay: null },
  });

  const sourceHost = "source-telemetry.test";
  const targetHost = "target-telemetry.test";
  const adIdentifier = "ad_identifier_telemetry";
  const adIndex = 4;

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  await mockServer.stop();

  Assert.equal(mockServer.receivedReports.length, 1);

  const expectedReport = {
    contentType: "application/dap-report",
    size: 1318,
  };

  const receivedReport = mockServer.receivedReports.pop();
  Assert.deepEqual(receivedReport, expectedReport);
});

function BinaryHttpResponse(status, headerNames, headerValues, content) {
  this.status = status;
  this.headerNames = headerNames;
  this.headerValues = headerValues;
  this.content = content;
}

BinaryHttpResponse.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIBinaryHttpResponse"]),
};

class MockOHTTPBackend {
  HOST;
  #ohttpServer;

  constructor() {
    let ohttp = Cc["@mozilla.org/network/oblivious-http;1"].getService(
      Ci.nsIObliviousHttp
    );
    this.#ohttpServer = ohttp.server();

    let httpServer = new HttpServer(); // This is the OHTTP relay endpoint.
    httpServer.registerPathHandler(
      new URL(this.getRelayAddress()).pathname,
      this.handle_relay_request.bind(this)
    );
    httpServer.start(-1);
    this.HOST = `localhost:${httpServer.identity.primaryPort}`;
    registerCleanupFunction(() => {
      return new Promise(resolve => {
        httpServer.stop(resolve);
      });
    });
  }

  getRelayAddress() {
    return `http://${this.HOST}/relay/`;
  }

  getOHTTPConfig() {
    return this.#ohttpServer.encodedConfig;
  }

  async handle_relay_request(request, response) {
    let inputStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );
    inputStream.setInputStream(request.bodyInputStream);
    let requestBody = inputStream.readByteArray(inputStream.available());
    let ohttpRequest = this.#ohttpServer.decapsulate(requestBody);
    let bhttp = Cc["@mozilla.org/network/binary-http;1"].getService(
      Ci.nsIBinaryHttp
    );
    let decodedRequest = bhttp.decodeRequest(ohttpRequest.request);
    response.processAsync();
    let real_destination =
      decodedRequest.scheme +
      "://" +
      decodedRequest.authority +
      decodedRequest.path;
    let innerBody = new Uint8Array(decodedRequest.content);
    let innerRequestHeaders = Object.fromEntries(
      decodedRequest.headerNames.map((name, index) => {
        return [name, decodedRequest.headerValues[index]];
      })
    );
    let innerResponse = await fetch(real_destination, {
      method: decodedRequest.method,
      headers: innerRequestHeaders,
      body: innerBody,
    });
    let bytes = new Uint8Array(await innerResponse.arrayBuffer());
    let binaryResponse = new BinaryHttpResponse(
      innerResponse.status,
      ["Content-Type"],
      ["application/octet-stream"],
      bytes
    );
    let encResponse = ohttpRequest.encapsulate(
      bhttp.encodeResponse(binaryResponse)
    );
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "message/ohttp-res", false);

    let bstream = Cc["@mozilla.org/binaryoutputstream;1"].createInstance(
      Ci.nsIBinaryOutputStream
    );
    bstream.setOutputStream(response.bodyOutputStream);
    bstream.writeByteArray(encResponse);
    response.finish();
  }
}

add_task(async function testWithRealDAPSenderAndOHTTP() {
  Services.fog.testResetFOG();

  const mockServer = new MockServer();
  mockServer.start();

  let ohttpBackend = new MockOHTTPBackend();

  let testDapOptions = {
    ohttp_relay: ohttpBackend.getRelayAddress(),
    ohttp_hpke: ohttpBackend.getOHTTPConfig(),
  };

  const privateAttribution = new PrivateAttributionService({
    testForceEnabled: true,
    testDapOptions,
  });

  const sourceHost = "source-telemetry.test";
  const targetHost = "target-telemetry.test";
  const adIdentifier = "ad_identifier_telemetry";
  const adIndex = 4;

  await privateAttribution.onAttributionEvent(
    sourceHost,
    "view",
    adIndex,
    adIdentifier,
    targetHost
  );

  await privateAttribution.onAttributionConversion(
    targetHost,
    TASK_ID,
    HISTOGRAM_SIZE,
    LOOKBACK_DAYS,
    "view",
    [adIdentifier],
    [sourceHost]
  );

  await mockServer.stop();

  Assert.equal(mockServer.receivedReports.length, 1);

  const expectedReport = {
    contentType: "application/dap-report",
    size: 1318,
  };

  const receivedReport = mockServer.receivedReports.pop();
  Assert.deepEqual(receivedReport, expectedReport);
});
