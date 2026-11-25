/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { StructuredLogger } = ChromeUtils.importESModule(
  "resource://testing-common/StructuredLog.sys.mjs"
);

const { ProfilerTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ProfilerTestUtils.sys.mjs"
);

add_task(async function test_testStatus_adds_profiler_markers() {
  if (!Services.profiler) {
    Assert.ok(true, "Profiler not available, skipping test");
    return;
  }

  await ProfilerTestUtils.startProfilerForMarkerTests();

  let buffer = [];
  let appendBuffer = function (msg) {
    buffer.push(JSON.stringify(msg));
  };

  let logger = new StructuredLogger("test_markers", appendBuffer);

  // Add some test status events
  logger.testStatus("test1.html", null, "PASS", "PASS", "test passed");
  logger.testStatus("test2.html", "subtest1", "FAIL", "PASS", "test failed");
  logger.testStatus("test3.html", null, "ERROR", "PASS", "test error");
  logger.testStatus("test4.html", null, "FAIL", "FAIL", "known fail");
  logger.testStatus("test5.html", null, "PASS", "FAIL", "unexpected pass");

  // Stop profiler and get profile
  const profile = await ProfilerTestUtils.stopNowAndGetProfile();

  // Find markers
  const mainThread = profile.threads.find(({ name }) => name === "GeckoMain");
  Assert.ok(mainThread, "Found main thread");

  const markers = ProfilerTestUtils.getInflatedMarkerData(mainThread);
  const testMarkers = markers.filter(m => m.name.startsWith("TEST-"));

  // Find specific markers and verify them
  const passMarker = testMarkers.find(m => m.name === "TEST-PASS");
  const failMarker = testMarkers.find(m => m.name === "TEST-UNEXPECTED-FAIL");
  const errorMarker = testMarkers.find(m => m.name === "TEST-UNEXPECTED-ERROR");
  const knownFailMarker = testMarkers.find(m => m.name === "TEST-KNOWN-FAIL");
  const unexpectedPassMarker = testMarkers.find(
    m => m.name === "TEST-UNEXPECTED-PASS"
  );

  Assert.ok(passMarker, "Found TEST-PASS marker");
  Assert.equal(
    passMarker.data.type,
    "TestStatus",
    "TEST-PASS has TestStatus type"
  );
  Assert.equal(passMarker.data.test, "test1.html", "TEST-PASS has test field");
  Assert.ok(
    !passMarker.data.hasOwnProperty("subtest"),
    "TEST-PASS has no subtest field"
  );
  Assert.equal(passMarker.data.status, "PASS", "TEST-PASS has status PASS");
  Assert.equal(passMarker.data.expected, "PASS", "TEST-PASS has expected PASS");
  Assert.equal(passMarker.data.color, "green", "TEST-PASS is green");

  Assert.ok(failMarker, "Found TEST-UNEXPECTED-FAIL marker");
  Assert.equal(
    failMarker.data.type,
    "TestStatus",
    "TEST-UNEXPECTED-FAIL has TestStatus type"
  );
  Assert.equal(
    failMarker.data.test,
    "test2.html",
    "TEST-UNEXPECTED-FAIL has test field"
  );
  Assert.equal(
    failMarker.data.subtest,
    "subtest1",
    "TEST-UNEXPECTED-FAIL has subtest field"
  );
  Assert.equal(
    failMarker.data.status,
    "FAIL",
    "TEST-UNEXPECTED-FAIL has status FAIL"
  );
  Assert.equal(
    failMarker.data.expected,
    "PASS",
    "TEST-UNEXPECTED-FAIL has expected PASS"
  );
  Assert.equal(
    failMarker.data.color,
    "orange",
    "TEST-UNEXPECTED-FAIL is orange"
  );
  Assert.ok(failMarker.data.stack, "TEST-UNEXPECTED-FAIL has captured stack");

  Assert.ok(errorMarker, "Found TEST-UNEXPECTED-ERROR marker");
  Assert.equal(
    errorMarker.data.type,
    "TestStatus",
    "TEST-UNEXPECTED-ERROR has TestStatus type"
  );
  Assert.equal(
    errorMarker.data.test,
    "test3.html",
    "TEST-UNEXPECTED-ERROR has test field"
  );
  Assert.ok(
    !errorMarker.data.hasOwnProperty("subtest"),
    "TEST-UNEXPECTED-ERROR has no subtest field"
  );
  Assert.equal(
    errorMarker.data.status,
    "ERROR",
    "TEST-UNEXPECTED-ERROR has status ERROR"
  );
  Assert.equal(
    errorMarker.data.expected,
    "PASS",
    "TEST-UNEXPECTED-ERROR has expected PASS"
  );
  Assert.equal(errorMarker.data.color, "red", "TEST-UNEXPECTED-ERROR is red");
  Assert.ok(errorMarker.data.stack, "TEST-UNEXPECTED-ERROR has captured stack");

  Assert.ok(knownFailMarker, "Found TEST-KNOWN-FAIL marker");
  Assert.equal(
    knownFailMarker.data.type,
    "TestStatus",
    "TEST-KNOWN-FAIL has TestStatus type"
  );
  Assert.equal(
    knownFailMarker.data.test,
    "test4.html",
    "TEST-KNOWN-FAIL has test field"
  );
  Assert.ok(
    !knownFailMarker.data.hasOwnProperty("subtest"),
    "TEST-KNOWN-FAIL has no subtest field"
  );
  Assert.equal(
    knownFailMarker.data.status,
    "FAIL",
    "TEST-KNOWN-FAIL has status FAIL"
  );
  Assert.equal(
    knownFailMarker.data.expected,
    "FAIL",
    "TEST-KNOWN-FAIL has expected FAIL"
  );
  Assert.equal(knownFailMarker.data.color, "green", "TEST-KNOWN-FAIL is green");
  Assert.ok(knownFailMarker.data.stack, "TEST-KNOWN-FAIL has captured stack");

  Assert.ok(unexpectedPassMarker, "Found TEST-UNEXPECTED-PASS marker");
  Assert.equal(
    unexpectedPassMarker.data.type,
    "TestStatus",
    "TEST-UNEXPECTED-PASS has TestStatus type"
  );
  Assert.equal(
    unexpectedPassMarker.data.test,
    "test5.html",
    "TEST-UNEXPECTED-PASS has test field"
  );
  Assert.ok(
    !unexpectedPassMarker.data.hasOwnProperty("subtest"),
    "TEST-UNEXPECTED-PASS has no subtest field"
  );
  Assert.equal(
    unexpectedPassMarker.data.status,
    "PASS",
    "TEST-UNEXPECTED-PASS has status PASS"
  );
  Assert.equal(
    unexpectedPassMarker.data.expected,
    "FAIL",
    "TEST-UNEXPECTED-PASS has expected FAIL"
  );
  Assert.equal(
    unexpectedPassMarker.data.color,
    "orange",
    "TEST-UNEXPECTED-PASS is orange"
  );
});

add_task(async function test_testEnd_adds_profiler_markers() {
  if (!Services.profiler) {
    Assert.ok(true, "Profiler not available, skipping test");
    return;
  }

  await ProfilerTestUtils.startProfilerForMarkerTests();

  let buffer = [];
  let appendBuffer = function (msg) {
    buffer.push(JSON.stringify(msg));
  };

  let logger = new StructuredLogger("test_markers", appendBuffer);

  // Add test start/end pairs
  logger.testStart("browser/base/content/test1.html");
  logger.testEnd("browser/base/content/test1.html", "PASS", "PASS");

  logger.testStart("browser/base/content/test2.html");
  logger.testEnd(
    "browser/base/content/test2.html",
    "FAIL",
    "PASS",
    "test failed"
  );

  logger.testStart("browser/base/content/test3.html");
  logger.testEnd(
    "browser/base/content/test3.html",
    "ERROR",
    "PASS",
    "test error"
  );

  logger.testStart("browser/base/content/test4.html");
  logger.testEnd(
    "browser/base/content/test4.html",
    "FAIL",
    "FAIL",
    "expected fail"
  );

  // Stop profiler and get profile
  const profile = await ProfilerTestUtils.stopNowAndGetProfile();

  // Find markers
  const mainThread = profile.threads.find(({ name }) => name === "GeckoMain");
  Assert.ok(mainThread, "Found main thread");

  const markers = ProfilerTestUtils.getInflatedMarkerData(mainThread);
  const testMarkers = markers.filter(m => m.name === "test");

  Assert.equal(testMarkers.length, 4, "Found 4 test duration markers");

  // Find specific test markers
  const passTestMarker = testMarkers.find(
    m => m.data.test === "browser/base/content/test1.html"
  );
  const failTestMarker = testMarkers.find(
    m => m.data.test === "browser/base/content/test2.html"
  );
  const errorTestMarker = testMarkers.find(
    m => m.data.test === "browser/base/content/test3.html"
  );
  const expectedFailTestMarker = testMarkers.find(
    m => m.data.test === "browser/base/content/test4.html"
  );

  Assert.ok(passTestMarker, "Found test1.html marker");
  Assert.equal(passTestMarker.data.type, "Test", "PASS test has Test type");
  Assert.equal(
    passTestMarker.data.test,
    "browser/base/content/test1.html",
    "PASS test has correct test"
  );
  Assert.equal(
    passTestMarker.data.name,
    "test1.html",
    "PASS test has correct name"
  );
  Assert.equal(passTestMarker.data.color, "green", "PASS test is green");
  Assert.equal(
    passTestMarker.data.status,
    "PASS",
    "PASS test has correct status"
  );
  Assert.ok(
    !passTestMarker.data.hasOwnProperty("expected"),
    "PASS test has no expected field"
  );
  Assert.greater(
    passTestMarker.endTime,
    passTestMarker.startTime,
    "PASS test has duration"
  );

  Assert.ok(failTestMarker, "Found test2.html marker");
  Assert.equal(failTestMarker.data.type, "Test", "FAIL test has Test type");
  Assert.equal(
    failTestMarker.data.test,
    "browser/base/content/test2.html",
    "FAIL test has correct test"
  );
  Assert.equal(
    failTestMarker.data.name,
    "test2.html",
    "FAIL test has correct name"
  );
  Assert.equal(failTestMarker.data.color, "orange", "FAIL test is orange");
  Assert.equal(
    failTestMarker.data.status,
    "FAIL",
    "FAIL test has correct status"
  );
  Assert.equal(
    failTestMarker.data.expected,
    "PASS",
    "FAIL test has expected field"
  );
  Assert.greater(
    failTestMarker.endTime,
    failTestMarker.startTime,
    "FAIL test has duration"
  );

  Assert.ok(errorTestMarker, "Found test3.html marker");
  Assert.equal(errorTestMarker.data.type, "Test", "ERROR test has Test type");
  Assert.equal(
    errorTestMarker.data.test,
    "browser/base/content/test3.html",
    "ERROR test has correct test"
  );
  Assert.equal(
    errorTestMarker.data.name,
    "test3.html",
    "ERROR test has correct name"
  );
  Assert.equal(errorTestMarker.data.color, "red", "ERROR test is red");
  Assert.equal(
    errorTestMarker.data.status,
    "ERROR",
    "ERROR test has correct status"
  );
  Assert.equal(
    errorTestMarker.data.expected,
    "PASS",
    "ERROR test has expected field"
  );
  Assert.greater(
    errorTestMarker.endTime,
    errorTestMarker.startTime,
    "ERROR test has duration"
  );

  Assert.ok(expectedFailTestMarker, "Found test4.html marker");
  Assert.equal(
    expectedFailTestMarker.data.type,
    "Test",
    "Expected FAIL test has Test type"
  );
  Assert.equal(
    expectedFailTestMarker.data.test,
    "browser/base/content/test4.html",
    "Expected FAIL test has correct test"
  );
  Assert.equal(
    expectedFailTestMarker.data.name,
    "test4.html",
    "Expected FAIL test has correct name"
  );
  Assert.equal(
    expectedFailTestMarker.data.color,
    "green",
    "Expected FAIL test is green"
  );
  Assert.equal(
    expectedFailTestMarker.data.status,
    "FAIL",
    "Expected FAIL test has correct status"
  );
  Assert.ok(
    !expectedFailTestMarker.data.hasOwnProperty("expected"),
    "Expected FAIL test has no expected field"
  );
  Assert.greater(
    expectedFailTestMarker.endTime,
    expectedFailTestMarker.startTime,
    "Expected FAIL test has duration"
  );
});

add_task(async function test_log_adds_profiler_markers() {
  if (!Services.profiler) {
    Assert.ok(true, "Profiler not available, skipping test");
    return;
  }

  await ProfilerTestUtils.startProfilerForMarkerTests();

  let buffer = [];
  let appendBuffer = function (msg) {
    buffer.push(JSON.stringify(msg));
  };

  let logger = new StructuredLogger("test_markers", appendBuffer);

  // Add log messages at different levels
  logger.info("info message", { test: "test1.html", subtest: "subtest1" });
  logger.warning("warning message");
  logger.error("error message");
  logger.debug("debug message");

  // Stop profiler and get profile
  const profile = await ProfilerTestUtils.stopNowAndGetProfile();

  // Find markers
  const mainThread = profile.threads.find(({ name }) => name === "GeckoMain");
  Assert.ok(mainThread, "Found main thread");

  const markers = ProfilerTestUtils.getInflatedMarkerData(mainThread);
  const logMarkers = markers.filter(m =>
    ["INFO", "WARNING", "ERROR", "DEBUG"].includes(m.name)
  );

  Assert.equal(logMarkers.length, 4, "Found 4 log markers");

  // Find specific markers
  const infoMarker = logMarkers.find(m => m.name === "INFO");
  const warningMarker = logMarkers.find(m => m.name === "WARNING");
  const errorMarker = logMarkers.find(m => m.name === "ERROR");
  const debugMarker = logMarkers.find(m => m.name === "DEBUG");

  Assert.ok(infoMarker, "Found INFO marker");
  Assert.ok(warningMarker, "Found WARNING marker");
  Assert.ok(errorMarker, "Found ERROR marker");
  Assert.ok(debugMarker, "Found DEBUG marker");

  // Verify marker data
  Assert.equal(infoMarker.data.type, "TestLog", "INFO marker has TestLog type");
  Assert.equal(infoMarker.data.test, "test1.html", "INFO marker has test name");
  Assert.equal(
    infoMarker.data.subtest,
    "subtest1",
    "INFO marker has subtest name"
  );
  Assert.ok(!infoMarker.data.color, "INFO marker has no color (default blue)");

  Assert.equal(
    warningMarker.data.type,
    "TestLog",
    "WARNING marker has TestLog type"
  );
  Assert.equal(warningMarker.data.color, "orange", "WARNING marker is orange");

  Assert.equal(
    errorMarker.data.type,
    "TestLog",
    "ERROR marker has TestLog type"
  );
  Assert.equal(errorMarker.data.color, "red", "ERROR marker is red");

  Assert.equal(
    debugMarker.data.type,
    "TestLog",
    "DEBUG marker has TestLog type"
  );
  Assert.equal(debugMarker.data.color, "grey", "DEBUG marker is grey");
});

add_task(async function test_test_path_normalization() {
  if (!Services.profiler) {
    Assert.ok(true, "Profiler not available, skipping test");
    return;
  }

  await ProfilerTestUtils.startProfilerForMarkerTests();

  let buffer = [];
  let appendBuffer = function (msg) {
    buffer.push(JSON.stringify(msg));
  };

  let logger = new StructuredLogger("test_markers", appendBuffer);

  // Test various path formats that should be normalized
  // These match the test cases in testing/mochitest/tests/python/test_message_logger.py
  logger.testStatus(
    "chrome://mochitests/content/a11y/accessible/tests/browser/test_foo.html",
    null,
    "PASS",
    "PASS"
  );
  logger.testStatus(
    "chrome://mochitests/content/browser/browser/base/content/test_foo.html",
    null,
    "PASS",
    "PASS"
  );
  logger.testStatus(
    "http://mochi.test:8888/tests/dom/test_foo.html",
    null,
    "PASS",
    "PASS"
  );
  logger.testStatus(
    "https://example.org:443/tests/netwerk/test/test_foo.html",
    null,
    "PASS",
    "PASS"
  );

  // Test with testEnd
  logger.testStart("chrome://mochitests/content/browser/toolkit/test_bar.html");
  logger.testEnd(
    "chrome://mochitests/content/browser/toolkit/test_bar.html",
    "PASS",
    "PASS"
  );

  // Test with log markers
  logger.info("info message", {
    test: "chrome://mochitests/content/chrome/toolkit/test_baz.html",
  });

  const profile = await ProfilerTestUtils.stopNowAndGetProfile();

  const mainThread = profile.threads.find(({ name }) => name === "GeckoMain");
  Assert.ok(mainThread, "Found main thread");

  const markers = ProfilerTestUtils.getInflatedMarkerData(mainThread);
  const testStatusMarkers = markers.filter(m => m.name === "TEST-PASS");
  const testEndMarkers = markers.filter(m => m.name === "test");
  const infoMarkers = markers.filter(m => m.name === "INFO");

  // Verify testStatus markers have normalized paths
  const a11yMarker = testStatusMarkers.find(
    m => m.data.test === "accessible/tests/browser/test_foo.html"
  );
  Assert.ok(a11yMarker, "Found a11y marker with normalized path");

  const browserMarker = testStatusMarkers.find(
    m => m.data.test === "browser/base/content/test_foo.html"
  );
  Assert.ok(browserMarker, "Found browser marker with normalized path");

  const domMarker = testStatusMarkers.find(
    m => m.data.test === "dom/test_foo.html"
  );
  Assert.ok(domMarker, "Found dom marker with normalized path");

  const netwerkMarker = testStatusMarkers.find(
    m => m.data.test === "netwerk/test/test_foo.html"
  );
  Assert.ok(netwerkMarker, "Found netwerk marker with normalized path");

  // Verify testEnd marker has normalized path
  const testEndMarker = testEndMarkers.find(
    m => m.data.test === "toolkit/test_bar.html"
  );
  Assert.ok(testEndMarker, "Found testEnd marker with normalized path");
  Assert.equal(
    testEndMarker.data.name,
    "test_bar.html",
    "testEnd marker has correct name"
  );

  // Verify info marker has normalized path
  const infoMarker = infoMarkers.find(
    m => m.data.test === "toolkit/test_baz.html"
  );
  Assert.ok(infoMarker, "Found info marker with normalized path");
});
