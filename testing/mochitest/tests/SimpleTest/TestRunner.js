/*
 * e10s event dispatcher from content->chrome
 *
 * type = eventName (QuitApplication)
 * data = json object {"filename":filename} <- for LoggerInit
 */

// This file expects the following files to be loaded.
/* import-globals-from LogController.js */
/* import-globals-from MemoryStats.js */
/* import-globals-from MozillaLogger.js */

/* eslint-disable no-unsanitized/property */

"use strict";

const { StructuredLogger, StructuredFormatter } =
  SpecialPowers.ChromeUtils.importESModule(
    "resource://testing-common/StructuredLog.sys.mjs"
  );

function getElement(id) {
  return typeof id == "string" ? document.getElementById(id) : id;
}

this.$ = this.getElement;

function contentDispatchEvent(type, data, sync) {
  if (typeof data == "undefined") {
    data = {};
  }

  var event = new CustomEvent("contentEvent", {
    bubbles: true,
    detail: {
      sync,
      type,
      data: JSON.stringify(data),
    },
  });
  document.dispatchEvent(event);
}

function contentAsyncEvent(type, data) {
  contentDispatchEvent(type, data, 0);
}

/* Helper Function */
function extend(obj, /* optional */ skip) {
  // Extend an array with an array-like object starting
  // from the skip index
  if (!skip) {
    skip = 0;
  }
  if (obj) {
    var l = obj.length;
    var ret = [];
    for (var i = skip; i < l; i++) {
      ret.push(obj[i]);
    }
  }
  return ret;
}

function flattenArguments(/* ...*/) {
  var res = [];
  var args = extend(arguments);
  while (args.length) {
    var o = args.shift();
    if (o && typeof o == "object" && typeof o.length == "number") {
      for (var i = o.length - 1; i >= 0; i--) {
        args.unshift(o[i]);
      }
    } else {
      res.push(o);
    }
  }
  return res;
}

function testInXOriginFrame() {
  // Check if the test running in an iframe is a cross origin test.
  try {
    $("testframe").contentWindow.origin;
    return false;
  } catch (e) {
    return true;
  }
}

function testInDifferentProcess() {
  // Check if the test running in an iframe that is loaded in a different process.
  return SpecialPowers.Cu.isRemoteProxy($("testframe").contentWindow);
}

/**
 * TestRunner: A test runner for SimpleTest
 * TODO:
 *
 *  * Avoid moving iframes: That causes reloads on mozilla and opera.
 *
 *
 */
var TestRunner = {};
TestRunner.logEnabled = false;
TestRunner._currentTest = 0;
TestRunner._lastTestFinished = -1;
TestRunner._loopIsRestarting = false;
TestRunner.currentTestURL = "";
TestRunner.originalTestURL = "";
TestRunner._urls = [];
TestRunner._lastAssertionCount = 0;
TestRunner._expectedMinAsserts = 0;
TestRunner._expectedMaxAsserts = 0;

TestRunner.timeout = 300 * 1000; // 5 minutes.
TestRunner.maxTimeouts = 4; // halt testing after too many timeouts
TestRunner.runSlower = false;
TestRunner.dumpOutputDirectory = "";
TestRunner.dumpAboutMemoryAfterTest = false;
TestRunner.dumpDMDAfterTest = false;
TestRunner.slowestTestTime = 0;
TestRunner.slowestTestURL = "";
TestRunner.interactiveDebugger = false;
TestRunner.cleanupCrashes = false;
TestRunner.timeoutAsPass = false;
TestRunner.conditionedProfile = false;
TestRunner.comparePrefs = false;

TestRunner._expectingProcessCrash = false;
TestRunner._structuredFormatter = new StructuredFormatter();

/**
 * Make sure the tests don't hang indefinitely.
 */
TestRunner._numTimeouts = 0;
TestRunner._currentTestStartTime = new Date().valueOf();
TestRunner._timeoutFactor = 1;
TestRunner._currentTestTimedOut = false;

function record(succeeded, expectedFail, msg) {
  let successInfo;
  let failureInfo;
  if (expectedFail) {
    successInfo = {
      status: "PASS",
      expected: "FAIL",
      message: "TEST-UNEXPECTED-PASS",
    };
    failureInfo = {
      status: "FAIL",
      expected: "FAIL",
      message: "TEST-KNOWN-FAIL",
    };
  } else {
    successInfo = {
      status: "PASS",
      expected: "PASS",
      message: "TEST-PASS",
    };
    failureInfo = {
      status: "FAIL",
      expected: "PASS",
      message: "TEST-UNEXPECTED-FAIL",
    };
  }

  let result = succeeded ? successInfo : failureInfo;

  TestRunner.structuredLogger.testStatus(
    TestRunner.currentTestURL,
    msg,
    result.status,
    result.expected,
    "",
    ""
  );
}

TestRunner._checkForHangs = function () {
  function reportError(win, msg) {
    if (testInXOriginFrame() || "SimpleTest" in win) {
      record(false, TestRunner.timeoutAsPass, msg);
    } else if ("W3CTest" in win) {
      win.W3CTest.logFailure(msg);
    }
  }

  async function killTest(win) {
    if (testInXOriginFrame()) {
      win.postMessage("SimpleTest:timeout", "*");
    } else if ("SimpleTest" in win) {
      await win.SimpleTest.timeout();
      win.SimpleTest.finish();
    } else if ("W3CTest" in win) {
      await win.W3CTest.timeout();
    }
  }

  if (TestRunner._currentTest < TestRunner._urls.length) {
    var runtime = new Date().valueOf() - TestRunner._currentTestStartTime;
    if (
      !TestRunner.interactiveDebugger &&
      runtime >= TestRunner.timeout * TestRunner._timeoutFactor
    ) {
      let testIframe = $("testframe");
      var frameWindow =
        (!testInXOriginFrame() && testIframe.contentWindow.wrappedJSObject) ||
        testIframe.contentWindow;
      TestRunner._currentTestTimedOut = true;
      reportError(frameWindow, "Test timed out.");
      TestRunner.updateUI([{ result: false }]);

      // If we have too many timeouts, give up. We don't want to wait hours
      // for results if some bug causes lots of tests to time out.
      if (
        ++TestRunner._numTimeouts >= TestRunner.maxTimeouts ||
        TestRunner.runUntilFailure
      ) {
        TestRunner._haltTests = true;

        // Reported against the test that ran out of time rather than a synthetic
        // path: currentTestURL is what every later testStatus, assertionCount and
        // testEnd is logged with, so overwriting it here mis-attributes all of them.
        reportError(
          frameWindow,
          TestRunner.maxTimeouts + " test timeouts, giving up."
        );
        var skippedTests = TestRunner._urls.length - TestRunner._currentTest;
        reportError(
          frameWindow,
          "Skipping " + skippedTests + " remaining tests."
        );
      }

      // Add a little (1 second) delay to ensure automation.py has time to notice
      // "Test timed out" log and process it (= take a screenshot).
      setTimeout(async function delayedKillTest() {
        try {
          await killTest(frameWindow);
        } catch (e) {
          reportError(frameWindow, "Test error: " + e);
          TestRunner.updateUI([{ result: false }]);
        }
      }, 1000);

      if (TestRunner._haltTests) {
        return;
      }
    }

    setTimeout(TestRunner._checkForHangs, 30000);
  }
};

TestRunner.requestLongerTimeout = function (factor) {
  TestRunner._timeoutFactor = factor;
};

/**
 * This is used to loop tests
 */
TestRunner.repeat = 0;
TestRunner._currentLoop = 1;

TestRunner.expectAssertions = function (min, max) {
  if (typeof max == "undefined") {
    max = min;
  }
  if (
    typeof min != "number" ||
    typeof max != "number" ||
    min < 0 ||
    max < min
  ) {
    throw new Error("bad parameter to expectAssertions");
  }
  TestRunner._expectedMinAsserts = min;
  TestRunner._expectedMaxAsserts = max;
};

/**
 * This function is called after generating the summary.
 */
TestRunner.onComplete = null;

/**
 * Adds a failed test case to a list so we can rerun only the failed tests
 */
TestRunner._failedTests = {};
TestRunner._failureFile = "";

TestRunner.addFailedTest = function (testName) {
  if (TestRunner._failedTests[testName] == undefined) {
    TestRunner._failedTests[testName] = "";
  }
};

TestRunner.setFailureFile = function (fileName) {
  TestRunner._failureFile = fileName;
};

TestRunner.generateFailureList = function () {
  if (TestRunner._failureFile) {
    var failures = new MozillaFileLogger(TestRunner._failureFile);
    failures.log(JSON.stringify(TestRunner._failedTests));
    failures.close();
  }
};

/**
 * If logEnabled is true, this is the logger that will be used.
 */

// This delimiter is used to avoid interleaving Mochitest/Gecko logs.
var LOG_DELIMITER = "\ue175\uee31\u2c32\uacbf";

// A log callback for StructuredLog.sys.mjs
TestRunner._dumpMessage = function (message) {
  var str;

  // This is a directive to python to format these messages
  // for compatibility with mozharness. This can be removed
  // with the MochitestFormatter (see bug 1045525).
  message.js_source = "TestRunner.js";
  if (
    TestRunner.interactiveDebugger &&
    message.action in TestRunner._structuredFormatter
  ) {
    str = TestRunner._structuredFormatter[message.action](message);
  } else {
    str = LOG_DELIMITER + JSON.stringify(message) + LOG_DELIMITER;
  }
  // BUGFIX: browser-chrome tests don't use LogController
  if (Object.keys(LogController.listeners).length !== 0) {
    LogController.log(str);
  } else {
    dump("\n" + str + "\n");
  }
  // Checking for error messages
  if (message.expected || message.level === "ERROR") {
    TestRunner.failureHandler();
  }
};

// From https://searchfox.org/firefox-main/source/testing/modules/StructuredLog.sys.mjs
TestRunner.structuredLogger = new StructuredLogger(
  "mochitest",
  TestRunner._dumpMessage,
  [],
  TestRunner
);
TestRunner.structuredLogger.deactivateBuffering = function () {
  TestRunner.structuredLogger.logData("buffering_off");
};
TestRunner.structuredLogger.activateBuffering = function () {
  TestRunner.structuredLogger.logData("buffering_on");
};

TestRunner.log = function (msg) {
  if (TestRunner.logEnabled) {
    TestRunner.structuredLogger.info(msg);
  } else {
    dump(msg + "\n");
  }
};

TestRunner.error = function (msg) {
  if (TestRunner.logEnabled) {
    TestRunner.structuredLogger.error(msg);
  } else {
    dump(msg + "\n");
    TestRunner.failureHandler();
  }
};

TestRunner.failureHandler = function () {
  if (TestRunner.runUntilFailure) {
    TestRunner._haltTests = true;
  }

  if (TestRunner.debugOnFailure) {
    // You've hit this line because you requested to break into the
    // debugger upon a testcase failure on your test run.
    // eslint-disable-next-line no-debugger
    debugger;
  }
};

/**
 * Toggle element visibility
 */
TestRunner._toggle = function (el) {
  if (el.className == "noshow") {
    el.className = "";
    el.style.cssText = "";
  } else {
    el.className = "noshow";
    el.style.cssText = "width:0px; height:0px; border:0px;";
  }
};

/**
 * Creates the iframe that contains a test
 */
TestRunner._makeIframe = function (url, retry) {
  var iframe = $("testframe");
  if (
    url != "about:blank" &&
    (("hasFocus" in document && !document.hasFocus()) ||
      ("activeElement" in document && document.activeElement != iframe))
  ) {
    contentAsyncEvent("Focus");
    window.focus();
    SpecialPowers.focus();
    iframe.focus();
    if (retry < 3) {
      window.setTimeout(function () {
        TestRunner._makeIframe(url, retry + 1);
      });
      return;
    }

    TestRunner.structuredLogger.info(
      "Error: Unable to restore focus, expect failures and timeouts."
    );
  }
  window.scrollTo(0, $("indicator").offsetTop);
  try {
    let urlObj = new URL(url);
    if (TestRunner.xOriginTests) {
      // The test will run in a xorigin iframe, so we pass in additional test params in the
      // URL since the content process won't be able to access them from the parentRunner
      // directly.
      let params = TestRunner.getParameterInfo();
      urlObj.searchParams.append(
        "currentTestURL",
        urlObj.pathname.replace("/tests/", "")
      );
      urlObj.searchParams.append("closeWhenDone", params.closeWhenDone);
      urlObj.searchParams.append("showTestReport", TestRunner.showTestReport);
      urlObj.searchParams.append("expected", TestRunner.expected);
      iframe.src = urlObj.href;
    } else {
      iframe.src = url;
    }
  } catch {
    // If the provided `url` is not a valid URL (i.e. doesn't include a protocol)
    // then the new URL() constructor will raise a TypeError. This is expected in the
    // usual case (i.e. non-xorigin iFrame tests) so set the URL in the usual way.
    iframe.src = url;
  }
  iframe.name = url;
  iframe.width = "500";
};

/**
 * Returns the current test URL.
 * We use this to tell whether the test has navigated to another test without
 * being finished first.
 */
TestRunner.getLoadedTestURL = function () {
  if (!testInXOriginFrame()) {
    var prefix = "";
    // handle mochitest-chrome URIs
    if ($("testframe").contentWindow.location.protocol == "chrome:") {
      prefix = "chrome://mochitests";
    }
    return prefix + $("testframe").contentWindow.location.pathname;
  }
  return TestRunner.currentTestURL;
};

TestRunner.setParameterInfo = function (params) {
  this._params = params;
};

TestRunner.getParameterInfo = function () {
  return this._params;
};

/**
 * Print information about which prefs are set.
 * This is used to help validate that the tests are actually
 * running in the expected context.
 */
TestRunner.dumpPrefContext = function () {
  let prefs = ["fission.autostart"];

  let message = ["Dumping test context:"];
  prefs.forEach(function formatPref(pref) {
    let val = SpecialPowers.getBoolPref(pref);
    message.push(pref + "=" + val);
  });
  TestRunner.structuredLogger.info(message.join("\n  "));
};

/**
 * TestRunner entry point.
 *
 * The arguments are the URLs of the test to be ran.
 *
 */
TestRunner.runTests = function (/*url...*/) {
  TestRunner.structuredLogger.info("SimpleTest START");
  TestRunner.dumpPrefContext();
  TestRunner.originalTestURL = $("current-test").innerHTML;

  SpecialPowers.registerProcessCrashObservers();

  SpecialPowers.requestResetCoverageCounters().then(() => {
    TestRunner._urls = flattenArguments(arguments);

    var singleTestRun = this._urls.length <= 1 && TestRunner.repeat <= 1;
    TestRunner.showTestReport = singleTestRun;
    var frame = $("testframe");
    frame.src = "";
    if (singleTestRun) {
      // Can't use document.body because this runs in a XUL doc as well...
      var body = document.getElementsByTagName("body")[0];
      body.setAttribute("singletest", "true");
      frame.removeAttribute("scrolling");
    }
    TestRunner._checkForHangs();
    TestRunner.runNextTest();
  });
};

/**
 * Used for running a set of tests in a loop for debugging purposes
 * Takes an array of URLs
 */
TestRunner.resetTests = function (listURLs) {
  TestRunner._currentTest = 0;
  // Reset our "Current-test" line - functionality depends on it
  $("current-test").innerHTML = TestRunner.originalTestURL;
  if (TestRunner.logEnabled) {
    TestRunner.structuredLogger.info(
      "SimpleTest START Loop " + TestRunner._currentLoop
    );
  }

  TestRunner._urls = listURLs;
  $("testframe").src = "";
  TestRunner._checkForHangs();
  TestRunner.runNextTest();
};

TestRunner.getNextUrl = function () {
  var url = "";
  // sometimes we have a subtest/harness which doesn't use a manifest
  if (
    TestRunner._urls[TestRunner._currentTest] instanceof Object &&
    "test" in TestRunner._urls[TestRunner._currentTest]
  ) {
    url = TestRunner._urls[TestRunner._currentTest].test.url;
    TestRunner.expected =
      TestRunner._urls[TestRunner._currentTest].test.expected;
  } else {
    url = TestRunner._urls[TestRunner._currentTest];
    TestRunner.expected = "pass";
  }
  return url;
};

/**
 * Run the next test. If no test remains, calls onComplete().
 */
TestRunner._haltTests = false;
async function _runNextTest() {
  if (
    TestRunner._currentTest < TestRunner._urls.length &&
    !TestRunner._haltTests
  ) {
    var url = TestRunner.getNextUrl();
    TestRunner.currentTestURL = url;

    $("current-test-path").innerHTML = url;

    TestRunner._currentTestStartTimestamp = SpecialPowers.ChromeUtils.now();
    TestRunner._currentTestStartTime = new Date().valueOf();
    TestRunner._timeoutFactor = 1;
    TestRunner._currentTestTimedOut = false;
    TestRunner._expectedMinAsserts = 0;
    TestRunner._expectedMaxAsserts = 0;

    TestRunner.structuredLogger.testStart(url);

    if (TestRunner._urls[TestRunner._currentTest].test.allow_xul_xbl) {
      await SpecialPowers.pushPermissions([
        { type: "allowXULXBL", allow: true, context: "http://mochi.test:8888" },
        { type: "allowXULXBL", allow: true, context: "http://example.org" },
      ]);
    }
    if (TestRunner._urls[TestRunner._currentTest].test.https_first_disabled) {
      await SpecialPowers.pushPrefEnv({
        set: [["dom.security.https_first", false]],
      });
    }
    TestRunner._makeIframe(url, 0);
  } else {
    $("current-test").innerHTML = "<b>Finished</b>";
    // Only unload the last test to run if we're running more than one test.
    if (TestRunner._urls.length > 1) {
      TestRunner._makeIframe("about:blank", 0);
    }

    var passCount = parseInt($("pass-count").innerHTML, 10);
    var failCount = parseInt($("fail-count").innerHTML, 10);
    var todoCount = parseInt($("todo-count").innerHTML, 10);

    if (passCount === 0 && failCount === 0 && todoCount === 0) {
      // No |$('testframe').contentWindow|, so manually update: ...
      // ... the log,
      TestRunner.structuredLogger.error(
        "TEST-UNEXPECTED-FAIL | SimpleTest/TestRunner.js | No checks actually run"
      );
      // ... the count,
      $("fail-count").innerHTML = 1;
      // ... the indicator.
      var indicator = $("indicator");
      indicator.innerHTML = "Status: Fail (No checks actually run)";
      indicator.style.backgroundColor = "red";
    }

    let e10sMode = SpecialPowers.isMainProcess() ? "non-e10s" : "e10s";

    TestRunner.structuredLogger.info("TEST-START | Shutdown");
    TestRunner.structuredLogger.info("Passed:  " + passCount);
    TestRunner.structuredLogger.info("Failed:  " + failCount);
    TestRunner.structuredLogger.info("Todo:    " + todoCount);
    TestRunner.structuredLogger.info("Mode:    " + e10sMode);
    TestRunner.structuredLogger.info(
      "Slowest: " +
        TestRunner.slowestTestTime +
        "ms - " +
        TestRunner.slowestTestURL
    );

    // If we are looping, don't send this cause it closes the log file,
    // also don't unregister the crash observers until we're done.
    if (TestRunner.repeat === 0) {
      SpecialPowers.unregisterProcessCrashObservers();
      TestRunner.structuredLogger.info("SimpleTest FINISHED");
    }

    if (TestRunner.repeat === 0 && TestRunner.onComplete) {
      TestRunner.onComplete();
    }

    if (
      TestRunner._currentLoop <= TestRunner.repeat &&
      !TestRunner._haltTests
    ) {
      TestRunner._currentLoop++;
      TestRunner.resetTests(TestRunner._urls);
      TestRunner._loopIsRestarting = true;
    } else {
      // Loops are finished
      if (TestRunner.logEnabled) {
        TestRunner.structuredLogger.info(
          "TEST-INFO | Ran " + TestRunner._currentLoop + " Loops"
        );
        TestRunner.structuredLogger.info("SimpleTest FINISHED");
      }

      if (TestRunner.onComplete) {
        TestRunner.onComplete();
      }
    }
    TestRunner.generateFailureList();
  }
}
TestRunner.runNextTest = _runNextTest;

TestRunner.expectChildProcessCrash = function () {
  TestRunner._expectingProcessCrash = true;
};

/**
 * This stub is called by SimpleTest when a test is finished.
 */
TestRunner.testFinished = function (tests) {
  // Need to track subtests recorded here separately or else they'll
  // trigger the `result after SimpleTest.finish()` error.
  var extraTests = [];
  var result = "PASS";

  // Prevent a test from calling finish() multiple times before we
  // have a chance to unload it.
  if (
    TestRunner._currentTest == TestRunner._lastTestFinished &&
    !TestRunner._loopIsRestarting
  ) {
    record(false, false, "called finish() multiple times");
    TestRunner.structuredLogger.testEnd(
      TestRunner.currentTestURL,
      "FAIL",
      "PASS",
      "called finish() multiple times"
    );
    TestRunner.updateUI([{ result: false }]);
    return;
  }

  SpecialPowers.requestDumpCoverageCounters().then(() => {
    TestRunner._lastTestFinished = TestRunner._currentTest;
    TestRunner._loopIsRestarting = false;

    // TODO : replace this by a function that returns the mem data as an object
    // that's dumped later with the test_end message
    MemoryStats.dump(
      TestRunner._currentTest,
      TestRunner.currentTestURL,
      TestRunner.dumpOutputDirectory,
      TestRunner.dumpAboutMemoryAfterTest,
      TestRunner.dumpDMDAfterTest
    );

    async function cleanUpCrashDumpFiles() {
      if (
        !(await SpecialPowers.removeExpectedCrashDumpFiles(
          TestRunner._expectingProcessCrash
        ))
      ) {
        let subtest = "expected-crash-dump-missing";
        TestRunner.structuredLogger.testStatus(
          TestRunner.currentTestURL,
          subtest,
          "ERROR",
          "PASS",
          "This test did not leave any crash dumps behind, but we were expecting some!"
        );
        extraTests.push({ name: subtest, result: false });
        result = "ERROR";
      }

      var unexpectedCrashDumpFiles = SpecialPowers.unwrap(
        await SpecialPowers.findUnexpectedCrashDumpFiles()
      );
      TestRunner._expectingProcessCrash = false;
      if (unexpectedCrashDumpFiles.length) {
        let subtest = "unexpected-crash-dump-found";
        TestRunner.structuredLogger.testStatus(
          TestRunner.currentTestURL,
          subtest,
          "ERROR",
          "PASS",
          "This test left crash dumps behind, but we " +
            "weren't expecting it to!",
          null,
          { unexpected_crashdump_files: unexpectedCrashDumpFiles }
        );
        extraTests.push({ name: subtest, result: false });
        result = "CRASH";
        unexpectedCrashDumpFiles.sort().forEach(function (aFilename) {
          TestRunner.structuredLogger.info(
            "Found unexpected crash dump file " + aFilename + "."
          );
        });
      }

      if (TestRunner.cleanupCrashes) {
        if (await SpecialPowers.removePendingCrashDumpFiles()) {
          TestRunner.structuredLogger.info(
            "This test left pending crash dumps"
          );
        }
      }
    }

    function runNextTest() {
      if (TestRunner.currentTestURL != TestRunner.getLoadedTestURL()) {
        TestRunner.structuredLogger.testStatus(
          TestRunner.currentTestURL,
          TestRunner.getLoadedTestURL(),
          "FAIL",
          "PASS",
          "finished in a non-clean fashion, probably" +
            " because it didn't call SimpleTest.finish()",
          { loaded_test_url: TestRunner.getLoadedTestURL() }
        );
        extraTests.push({ name: "clean-finish", result: false });
        result = result != "CRASH" ? "ERROR" : result;
      }

      var runtime = new Date().valueOf() - TestRunner._currentTestStartTime;

      if (
        TestRunner.slowestTestTime < runtime &&
        TestRunner._timeoutFactor >= 1
      ) {
        TestRunner.slowestTestTime = runtime;
        TestRunner.slowestTestURL = TestRunner.currentTestURL;
      }

      TestRunner.updateUI(tests.concat(extraTests));

      // Check if any tests failed
      var results = TestRunner.countResults(tests.concat(extraTests));
      if (results.notOK > 0 && result === "PASS") {
        result = "FAIL";
      }

      // Don't show the interstitial if we just run one test with no repeats:
      if (TestRunner._urls.length == 1 && TestRunner.repeat <= 1) {
        TestRunner.testUnloaded(result, runtime);
        return;
      }

      var interstitialURL;
      if (
        !testInXOriginFrame() &&
        $("testframe").contentWindow.location.protocol == "chrome:"
      ) {
        interstitialURL =
          "tests/SimpleTest/iframe-between-tests.html?result=" +
          result +
          "&runtime=" +
          runtime;
      } else {
        interstitialURL =
          "/tests/SimpleTest/iframe-between-tests.html?result=" +
          result +
          "&runtime=" +
          runtime;
      }
      // check if there were test run after SimpleTest.finish, which should never happen
      if (!testInXOriginFrame()) {
        $("testframe").contentWindow.addEventListener("unload", function () {
          var testwin = $("testframe").contentWindow;
          if (testwin.SimpleTest) {
            if (typeof testwin.SimpleTest.testsLength === "undefined") {
              record(
                false,
                false,
                "fired an unload callback with missing test data," +
                  " possibly due to the test navigating or reloading"
              );
              TestRunner.updateUI([{ result: false }]);
            } else if (
              testwin.SimpleTest._tests.length != testwin.SimpleTest.testsLength
            ) {
              var didReportError = false;
              var wrongtestlength =
                testwin.SimpleTest._tests.length -
                testwin.SimpleTest.testsLength;
              var wrongtestname = "";
              for (var i = 0; i < wrongtestlength; i++) {
                wrongtestname =
                  testwin.SimpleTest._tests[testwin.SimpleTest.testsLength + i]
                    .name;
                record(
                  false,
                  false,
                  "logged result after SimpleTest.finish(): " + wrongtestname
                );
                didReportError = true;
              }
              if (!didReportError) {
                // This clause shouldn't be reachable, but if we somehow get
                // here (e.g. if wrongtestlength is somehow negative), it's
                // important that we log *something* for the { result: false }
                // test-failure that we're about to post.
                record(
                  false,
                  false,
                  "hit an unexpected condition when checking for" +
                    " logged results after SimpleTest.finish()"
                );
              }
              TestRunner.updateUI([{ result: false }]);
            }
          }
        });
      }
      TestRunner._makeIframe(interstitialURL, 0);
    }

    SpecialPowers.executeAfterFlushingMessageQueue(async function () {
      await SpecialPowers.waitForCrashes(TestRunner._expectingProcessCrash);
      await cleanUpCrashDumpFiles();
      await SpecialPowers.flushPermissions();
      await SpecialPowers.flushPrefEnv();
      SpecialPowers.cleanupAllClipboard(window);
      runNextTest();
    });
  });
};

/**
 * This stub is called by XOrigin Tests to report assertion count.
 */
TestRunner._xoriginAssertionCount = 0;
TestRunner.addAssertionCount = function (count) {
  if (!testInXOriginFrame()) {
    TestRunner.error(
      `addAssertionCount should only be called by a cross origin test`
    );
    return;
  }

  if (testInDifferentProcess()) {
    TestRunner._xoriginAssertionCount += count;
  }
};

// On failure, save a profile of the test as a CI artifact (the plain-mochitest
// counterpart of browser-test.js's notifyProfilerOfTestEnd). The profiler runs
// in the parent process, so gather and write the profile there via
// SpecialPowers, then return the message for the content-side structured logger
// to report.
TestRunner._saveFailureProfile = function (testURL) {
  return SpecialPowers.spawnChrome([testURL], async profileName => {
    const { shouldSaveFailureProfile, saveProfileToUploadDir } =
      ChromeUtils.importESModule(
        "resource://testing-common/TestProfilerArtifact.sys.mjs"
      );
    return shouldSaveFailureProfile()
      ? await saveProfileToUploadDir(profileName)
      : null;
  });
};

TestRunner.testUnloaded = function (result, runtime) {
  // If we're in a debug build, check assertion counts.  This code is
  // similar to the code in Tester_nextTest in browser-test.js used
  // for browser-chrome mochitests.
  if (SpecialPowers.isDebugBuild) {
    var newAssertionCount =
      SpecialPowers.assertionCount() + TestRunner._xoriginAssertionCount;
    var numAsserts = newAssertionCount - TestRunner._lastAssertionCount;
    TestRunner._lastAssertionCount = newAssertionCount;

    var max = TestRunner._expectedMaxAsserts;
    var min = TestRunner._expectedMinAsserts;
    if (Array.isArray(TestRunner.expected)) {
      // Accumulate all assertion counts recorded in the failure pattern file.
      let additionalAsserts = TestRunner.expected.reduce(
        (acc, [pat, count]) => {
          return pat == "ASSERTION" ? acc + count : acc;
        },
        0
      );
      min += additionalAsserts;
      max += additionalAsserts;
    }

    TestRunner.structuredLogger.assertionCount(
      TestRunner.currentTestURL,
      numAsserts,
      min,
      max
    );

    if (numAsserts < min || numAsserts > max) {
      result = "FAIL";

      var direction = "more";
      var target = max;
      if (numAsserts < min) {
        direction = "less";
        target = min;
      }
      TestRunner.structuredLogger.testStatus(
        TestRunner.currentTestURL,
        "Assertion Count",
        "ERROR",
        "PASS",
        numAsserts +
          " is " +
          direction +
          " than expected " +
          target +
          " assertions"
      );
    }
  }

  if (TestRunner._currentTestTimedOut && result == "PASS") {
    result = "TIMEOUT";
  }

  // Compare preferences before testEnd so that any changed-pref failures
  // are attributed to the test that changed them. This also resets
  // preferences between tests.
  // Note: this is for mochitest-plain only; browser tests do not
  // unconditionally reset between tests, see
  // checkPreferencesAfterTest in testing/mochitest/browser-test.js
  SpecialPowers.comparePrefsToBaseline(TestRunner.ignorePrefs, function (p) {
    if (TestRunner.comparePrefs) {
      let prefs = Array.from(SpecialPowers.Cu.waiveXrays(p), x =>
        SpecialPowers.unwrapIfWrapped(SpecialPowers.Cu.unwaiveXrays(x))
      );
      for (let pr of prefs) {
        record(false, false, "changed preference: " + pr);
      }
      if (prefs.length && result == "PASS") {
        result = "FAIL";
      }
    }

    // Save the failing test's profile (and log where it went) before testEnd so
    // the dashboards associate the artifact with this test. Guard the whole step
    // so a problem saving the profile can never stop the run from proceeding.
    (async function () {
      try {
        if (result != "PASS") {
          let message = await TestRunner._saveFailureProfile(
            TestRunner.currentTestURL
          );
          if (message) {
            TestRunner.structuredLogger.testStatus(
              TestRunner.currentTestURL,
              null,
              "FAIL",
              "PASS",
              message
            );
          }
        }
      } catch (e) {
        TestRunner.structuredLogger.info(
          "Failed to save failure profile: " + e
        );
      }

      TestRunner.structuredLogger.testEnd(
        TestRunner.currentTestURL,
        result,
        "PASS",
        TestRunner._currentTestTimedOut
          ? "Test timed out"
          : "Finished in " + runtime + "ms",
        { runtime }
      );

      TestRunner.doNextTest();
    })();
  });
};

TestRunner.doNextTest = function () {
  TestRunner._currentTest++;
  if (TestRunner.runSlower) {
    setTimeout(TestRunner.runNextTest, 1000);
  } else {
    TestRunner.runNextTest();
  }
};

/**
 * Get the results.
 */
TestRunner.countResults = function (tests) {
  var nOK = 0;
  var nNotOK = 0;
  var nTodo = 0;
  for (var i = 0; i < tests.length; ++i) {
    var test = tests[i];
    if (test.todo && !test.result) {
      nTodo++;
    } else if (test.result && !test.todo) {
      nOK++;
    } else {
      nNotOK++;
    }
  }
  return { OK: nOK, notOK: nNotOK, todo: nTodo };
};

/**
 * Print out table of any error messages found during looped run
 */
TestRunner.displayLoopErrors = function (tableName, tests) {
  if (TestRunner.countResults(tests).notOK > 0) {
    var table = $(tableName);
    var curtest;
    if (!table.rows.length) {
      //if table headers are not yet generated, make them
      var row = table.insertRow(table.rows.length);
      var cell = row.insertCell(0);
      var textNode = document.createTextNode("Test File Name:");
      cell.appendChild(textNode);
      cell = row.insertCell(1);
      textNode = document.createTextNode("Test:");
      cell.appendChild(textNode);
      cell = row.insertCell(2);
      textNode = document.createTextNode("Error message:");
      cell.appendChild(textNode);
    }

    //find the broken test
    for (var testnum in tests) {
      curtest = tests[testnum];
      if (
        !(
          (curtest.todo && !curtest.result) ||
          (curtest.result && !curtest.todo)
        )
      ) {
        //this is a failed test or the result of todo test. Display the related message
        row = table.insertRow(table.rows.length);
        cell = row.insertCell(0);
        textNode = document.createTextNode(TestRunner.currentTestURL);
        cell.appendChild(textNode);
        cell = row.insertCell(1);
        textNode = document.createTextNode(curtest.name);
        cell.appendChild(textNode);
        cell = row.insertCell(2);
        textNode = document.createTextNode(curtest.diag ? curtest.diag : "");
        cell.appendChild(textNode);
      }
    }
  }
};

TestRunner.updateUI = function (tests) {
  var results = TestRunner.countResults(tests);
  var passCount = parseInt($("pass-count").innerHTML) + results.OK;
  var failCount = parseInt($("fail-count").innerHTML) + results.notOK;
  var todoCount = parseInt($("todo-count").innerHTML) + results.todo;
  $("pass-count").innerHTML = passCount;
  $("fail-count").innerHTML = failCount;
  $("todo-count").innerHTML = todoCount;

  // Set the top Green/Red bar
  var indicator = $("indicator");
  if (failCount > 0) {
    indicator.innerHTML = "Status: Fail";
    indicator.style.backgroundColor = "red";
  } else if (passCount > 0) {
    indicator.innerHTML = "Status: Pass";
    indicator.style.backgroundColor = "#0d0";
  } else {
    indicator.innerHTML = "Status: ToDo";
    indicator.style.backgroundColor = "orange";
  }

  // Set the table values
  var trID = "tr-" + $("current-test-path").innerHTML;
  var row = $(trID);

  // Only update the row if it actually exists (autoUI)
  if (row != null) {
    var tds = row.getElementsByTagName("td");
    tds[0].style.backgroundColor = "#0d0";
    tds[0].innerHTML = parseInt(tds[0].innerHTML) + parseInt(results.OK);
    tds[1].style.backgroundColor = results.notOK > 0 ? "red" : "#0d0";
    tds[1].innerHTML = parseInt(tds[1].innerHTML) + parseInt(results.notOK);
    tds[2].style.backgroundColor = results.todo > 0 ? "orange" : "#0d0";
    tds[2].innerHTML = parseInt(tds[2].innerHTML) + parseInt(results.todo);
  }

  //if we ran in a loop, display any found errors
  if (TestRunner.repeat > 0) {
    TestRunner.displayLoopErrors("fail-table", tests);
  }
};

// XOrigin Tests
// If "--enable-xorigin-tests" is set, mochitests are run in a cross origin iframe.
// The parent process will run at http://mochi.xorigin-test:8888", and individual
// mochitests will be launched in a cross-origin iframe at http://mochi.test:8888.

var xOriginDispatchMap = {
  runner: TestRunner,
  logger: TestRunner.structuredLogger,
  addFailedTest: TestRunner.addFailedTest,
  expectAssertions: TestRunner.expectAssertions,
  expectChildProcessCrash: TestRunner.expectChildProcessCrash,
  requestLongerTimeout: TestRunner.requestLongerTimeout,
  "structuredLogger.deactivateBuffering":
    TestRunner.structuredLogger.deactivateBuffering,
  "structuredLogger.activateBuffering":
    TestRunner.structuredLogger.activateBuffering,
  "structuredLogger.testStatus": TestRunner.structuredLogger.testStatus,
  "structuredLogger.info": TestRunner.structuredLogger.info,
  "structuredLogger.warning": TestRunner.structuredLogger.warning,
  "structuredLogger.error": TestRunner.structuredLogger.error,
  testFinished: TestRunner.testFinished,
  addAssertionCount: TestRunner.addAssertionCount,
};

function xOriginTestRunnerHandler(event) {
  if (event.data.harnessType != "SimpleTest") {
    return;
  }
  // Handles messages from xOriginRunner in SimpleTest.js.
  if (event.data.command in xOriginDispatchMap) {
    xOriginDispatchMap[event.data.command].apply(
      xOriginDispatchMap[event.data.applyOn],
      event.data.params
    );
  } else {
    TestRunner.error(`Command ${event.data.command} not found
      in xOriginDispatchMap`);
  }
}

TestRunner.setXOriginEventHandler = function () {
  window.addEventListener("message", xOriginTestRunnerHandler);
};
