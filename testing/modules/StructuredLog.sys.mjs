/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let gSchemaRegistered = false;

function ensureSchemaRegistered() {
  if (gSchemaRegistered) {
    return;
  }
  gSchemaRegistered = true;

  ChromeUtils.registerMarkerSchema({
    name: "TestStatus",
    tableLabel: "{marker.data.message}",
    display: ["marker-chart", "marker-table"],
    colorField: "color",
    data: [
      {
        key: "message",
        label: "Message",
        format: "string",
      },
      {
        key: "test",
        label: "Test Name",
        format: "string",
      },
      {
        key: "subtest",
        label: "Subtest",
        format: "string",
      },
      {
        key: "status",
        label: "Status",
        format: "string",
      },
      {
        key: "expected",
        label: "Expected",
        format: "string",
      },
      {
        key: "color",
        hidden: true,
      },
    ],
  });

  ChromeUtils.registerMarkerSchema({
    name: "Test",
    tooltipLabel: "{marker.data.name}",
    tableLabel: "{marker.data.status} — {marker.data.test}",
    chartLabel: "{marker.data.name}",
    display: ["marker-chart", "marker-table"],
    colorField: "color",
    data: [
      {
        key: "test",
        label: "Test Name",
        format: "string",
      },
      {
        key: "name",
        label: "Short Name",
        format: "string",
        hidden: true,
      },
      {
        key: "status",
        label: "Status",
        format: "string",
      },
      {
        key: "expected",
        label: "Expected",
        format: "string",
      },
      {
        key: "message",
        label: "Message",
        format: "string",
      },
      {
        key: "color",
        hidden: true,
      },
    ],
  });

  ChromeUtils.registerMarkerSchema({
    name: "TestLog",
    tableLabel: "{marker.data.message}",
    display: ["marker-chart", "marker-table"],
    colorField: "color",
    data: [
      {
        key: "message",
        label: "Message",
        format: "string",
      },
      {
        key: "level",
        label: "Level",
        format: "string",
      },
      {
        key: "test",
        label: "Test Name",
        format: "string",
      },
      {
        key: "subtest",
        label: "Subtest",
        format: "string",
      },
      {
        key: "color",
        hidden: true,
      },
    ],
  });
}

/**
 * TestLogger: Logger class generating messages compliant with the
 * structured logging protocol for tests exposed by mozlog
 *
 * @param {string} name
 *        The name of the logger to instantiate.
 * @param {function} [dumpFun]
 *        An underlying function to be used to log raw messages. This function
 *        will receive the complete serialized json string to log.
 * @param {object} [scope]
 *        The scope that the dumpFun is loaded in, so that messages are cloned
 *        into that scope before passing them.
 */
export class StructuredLogger {
  name = null;
  #dumpFun = null;
  #dumpScope = null;
  #testStartTimes = new Map();

  // Regexes that normalize test paths, matching MessageLogger.TEST_PATH_PREFIXES
  static #TEST_PATH_PREFIXES = [
    /^\/tests\//,
    /^\w+:\/\/[\w.]+(:\d+)?(\/\w+)?\/(tests?|a11y|chrome)\//,
    /^\w+:\/\/[\w.]+(:\d+)?(\/\w+)?\/(tests?|browser)\//,
  ];

  constructor(name, dumpFun = dump, scope = null) {
    this.name = name;
    this.#dumpFun = dumpFun;
    this.#dumpScope = scope;
  }

  /**
   * Normalize a test path to match the relative path from the sourcedir.
   * Matches the behavior of MessageLogger._fix_test_name in runtests.py.
   */
  #normalizeTestPath(testPath) {
    for (const pattern of StructuredLogger.#TEST_PATH_PREFIXES) {
      const normalized = testPath.replace(pattern, "");
      if (normalized !== testPath) {
        return normalized;
      }
    }
    return testPath;
  }

  testStart(test) {
    var data = { test: this.#testId(test) };
    this.logData("test_start", data);

    const testId = this.#testId(test);
    this.#testStartTimes.set(testId, ChromeUtils.now());
  }

  testStatus(
    test,
    subtest,
    status,
    expected = "PASS",
    message = null,
    stack = null,
    extra = null,
    time = null
  ) {
    var data = {
      test: this.#testId(test),
      subtest,
      status,
    };

    if (expected != status && status != "SKIP") {
      data.expected = expected;
    }
    if (message !== null) {
      data.message = String(message);
    }
    if (stack !== null) {
      data.stack = stack;
    }
    if (extra !== null) {
      data.extra = extra;
    }
    if (time !== null) {
      data.time = time;
    }

    this.logData("test_status", data);

    ensureSchemaRegistered();

    // Determine marker name following mochitest conventions
    let markerName;
    if (status === expected) {
      // Expected result
      if (status === "FAIL") {
        markerName = "TEST-KNOWN-FAIL";
      } else {
        markerName = "TEST-" + status;
      }
    } else {
      // Unexpected result
      markerName = "TEST-UNEXPECTED-" + status;
    }

    // Prepare marker data with normalized test path
    const markerData = {
      type: "TestStatus",
      test: this.#normalizeTestPath(data.test),
      status,
      expected,
    };

    if (subtest) {
      markerData.subtest = subtest;
    }

    if (message !== null) {
      markerData.message = String(message);
    }

    // Determine color
    if (status === "ERROR" && status !== expected) {
      markerData.color = "red";
    } else if (status === expected) {
      markerData.color = "green";
    } else {
      markerData.color = "orange";
    }

    const options = { category: "Test" };

    // Capture stack for failures and errors
    if (status === "FAIL" || status === "ERROR") {
      options.captureStack = true;
    }

    ChromeUtils.addProfilerMarker(markerName, options, markerData);
  }

  testEnd(
    test,
    status,
    expected = "PASS",
    message = null,
    stack = null,
    extra = null
  ) {
    const testId = this.#testId(test);
    var data = { test: testId, status };

    if (expected != status && status != "SKIP") {
      data.expected = expected;
    }
    if (message !== null) {
      data.message = String(message);
    }
    if (stack !== null) {
      data.stack = stack;
    }
    if (extra !== null) {
      data.extra = extra;
    }

    this.logData("test_end", data);

    const startTime = this.#testStartTimes.get(testId);
    if (!startTime) {
      return;
    }
    this.#testStartTimes.delete(testId);

    ensureSchemaRegistered();

    // Normalize test path
    const testPath = this.#normalizeTestPath(testId);

    const markerData = {
      type: "Test",
      test: testPath,
      name: testPath.split("/").pop(),
      status,
    };

    if (data.expected) {
      markerData.expected = data.expected;
    }

    if (data.message) {
      markerData.message = data.message;
    }

    if (status) {
      // Determine color based on status and expectations
      if (status === "SKIP" || status === "TIMEOUT") {
        markerData.color = "yellow";
      } else if (status === "CRASH" || status === "ERROR") {
        markerData.color = "red";
      } else if (expected === status) {
        markerData.color = "green";
      } else {
        markerData.color = "orange";
      }
    }

    ChromeUtils.addProfilerMarker(
      "test",
      {
        category: "Test",
        startTime,
      },
      markerData
    );
  }

  assertionCount(test, count, minExpected = 0, maxExpected = 0) {
    var data = {
      test: this.#testId(test),
      min_expected: minExpected,
      max_expected: maxExpected,
      count,
    };

    this.logData("assertion_count", data);
  }

  suiteStart(
    ids,
    name = null,
    runinfo = null,
    versioninfo = null,
    deviceinfo = null,
    extra = null
  ) {
    Object.keys(ids).map(function (manifest) {
      ids[manifest] = ids[manifest].map(x => this.#testId(x));
    }, this);
    var data = { tests: ids };

    if (name !== null) {
      data.name = name;
    }

    if (runinfo !== null) {
      data.runinfo = runinfo;
    }

    if (versioninfo !== null) {
      data.versioninfo = versioninfo;
    }

    if (deviceinfo !== null) {
      data.deviceinfo = deviceinfo;
    }

    if (extra !== null) {
      data.extra = extra;
    }

    this.logData("suite_start", data);
  }

  suiteEnd(extra = null) {
    var data = {};

    if (extra !== null) {
      data.extra = extra;
    }

    this.logData("suite_end", data);
  }

  /**
   * Unstructured logging functions. The "extra" parameter can always by used to
   * log suite specific data. If a "stack" field is provided it is logged at the
   * top level of the data object for the benefit of mozlog's formatters.
   */
  log(level, message, extra = null) {
    var data = {
      level,
      message: String(message),
    };

    if (extra !== null) {
      data.extra = extra;
      if ("stack" in extra) {
        data.stack = extra.stack;
      }
    }

    this.logData("log", data);

    ensureSchemaRegistered();

    // Add marker type
    data.type = "TestLog";

    // Copy test/subtest from extra if present, normalizing test path
    if (extra) {
      if (extra.test) {
        data.test = this.#normalizeTestPath(extra.test);
      }
      if (extra.subtest) {
        data.subtest = extra.subtest;
      }
    }

    // Determine color based on log level
    if (level === "CRITICAL" || level === "ERROR") {
      data.color = "red";
    } else if (level === "WARNING") {
      data.color = "orange";
    } else if (level === "DEBUG") {
      data.color = "grey";
    }

    // Remove fields we don't want in the marker
    delete data.extra;

    const options = { category: "Test" };

    // Capture stack for errors and critical
    if (level === "CRITICAL" || level === "ERROR") {
      options.captureStack = true;
    }

    ChromeUtils.addProfilerMarker(level, options, data);
  }

  debug(message, extra = null) {
    this.log("DEBUG", message, extra);
  }

  info(message, extra = null) {
    this.log("INFO", message, extra);
  }

  warning(message, extra = null) {
    this.log("WARNING", message, extra);
  }

  error(message, extra = null) {
    this.log("ERROR", message, extra);
  }

  critical(message, extra = null) {
    this.log("CRITICAL", message, extra);
  }

  processOutput(thread, message) {
    this.logData("process_output", {
      message,
      thread,
    });
  }

  logData(action, data = {}) {
    var allData = {
      action,
      time: Date.now(),
      thread: null,
      pid: null,
      source: this.name,
    };

    for (var field in data) {
      allData[field] = data[field];
    }

    if (this.#dumpScope) {
      try {
        allData = Cu.cloneInto(allData, this.#dumpScope);
      } catch (e) {
        try {
          this.error(`Failed to cloneInto: ${e}`);
          this.warning(`Tried to clone: ${uneval(allData)}`);
        } catch (e2) {
          console.error("Failed to handle clone error", e, e2);
        }
      }
    }
    this.#dumpFun(allData);
  }

  #testId(test) {
    if (Array.isArray(test)) {
      return test.join(" ");
    }
    return test;
  }
}

/**
 * StructuredFormatter: Formatter class turning structured messages
 * into human-readable messages.
 */
export class StructuredFormatter {
  // The time at which the whole suite of tests started.
  #suiteStartTime = null;

  #testStartTimes = new Map();

  log(message) {
    return message.message;
  }

  suite_start(message) {
    this.#suiteStartTime = message.time;
    return "SUITE-START | Running " + message.tests.length + " tests";
  }

  test_start(message) {
    this.#testStartTimes.set(message.test, new Date().getTime());
    return "TEST-START | " + message.test;
  }

  test_status(message) {
    var statusInfo =
      message.test +
      " | " +
      message.subtest +
      (message.message ? " | " + message.message : "");
    if (message.expected) {
      return (
        "TEST-UNEXPECTED-" +
        message.status +
        " | " +
        statusInfo +
        " - expected: " +
        message.expected
      );
    }
    return "TEST-" + message.status + " | " + statusInfo;
  }

  test_end(message) {
    var startTime = this.#testStartTimes.get(message.test);
    this.#testStartTimes.delete(message.test);
    var statusInfo =
      message.test + (message.message ? " | " + String(message.message) : "");
    var result;
    if (message.expected) {
      result =
        "TEST-UNEXPECTED-" +
        message.status +
        " | " +
        statusInfo +
        " - expected: " +
        message.expected;
    } else {
      return "TEST-" + message.status + " | " + statusInfo;
    }
    result = result + " | took " + message.time - startTime + "ms";
    return result;
  }

  suite_end(message) {
    return "SUITE-END | took " + message.time - this.#suiteStartTime + "ms";
  }
}
