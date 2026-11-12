/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is the directory where gcov is emitting the gcda files.
const gcovPrefixPath = Services.env.get("GCOV_PREFIX");
// This is the directory where codecoverage.py is expecting to see the gcda files.
const gcovResultsPath = Services.env.get("GCOV_RESULTS_DIR");
// This is the directory where the JS engine is emitting the lcov files.
const jsvmPrefixPath = Services.env.get("JS_CODE_COVERAGE_OUTPUT_DIR");
// This is the directory where codecoverage.py is expecting to see the lcov files.
const jsvmResultsPath = Services.env.get("JSVM_RESULTS_DIR");

function awaitPromise(promise) {
  let ret;
  let complete = false;
  let error = null;
  promise
    .catch(e => (error = e))
    .then(v => {
      ret = v;
      complete = true;
    });
  Services.tm.spinEventLoopUntil(
    "PerTestCoverageUtils.sys.mjs:awaitPromise",
    () => complete
  );
  if (error) {
    throw new Error(error);
  }
  return ret;
}

export var PerTestCoverageUtils = class PerTestCoverageUtilsClass {
  // Resets the counters to 0.
  static async beforeTest() {
    if (!PerTestCoverageUtils.enabled) {
      return;
    }

    // Flush the counters.
    let codeCoverageService = Cc[
      "@mozilla.org/tools/code-coverage;1"
    ].getService(Ci.nsICodeCoverage);
    await codeCoverageService.flushCounters();

    // Remove coverage files created by the flush, and those that might have been created between the end of a previous test and the beginning of the next one (e.g. some tests can create a new content process for every sub-test).
    await IOUtils.remove(gcovPrefixPath, {
      recursive: true,
      ignoreAbsent: true,
    });
    await IOUtils.remove(jsvmPrefixPath, {
      recursive: true,
      ignoreAbsent: true,
    });

    // Move coverage files from the GCOV_RESULTS_DIR and JSVM_RESULTS_DIR directories, so we can accumulate the counters.
    await IOUtils.move(gcovResultsPath, gcovPrefixPath);
    await IOUtils.move(jsvmResultsPath, jsvmPrefixPath);
  }

  static beforeTestSync() {
    awaitPromise(this.beforeTest());
  }

  // Dumps counters and moves the gcda files in the directory expected by codecoverage.py.
  static async afterTest() {
    if (!PerTestCoverageUtils.enabled) {
      return;
    }

    // Flush the counters.
    let codeCoverageService = Cc[
      "@mozilla.org/tools/code-coverage;1"
    ].getService(Ci.nsICodeCoverage);
    await codeCoverageService.flushCounters();

    // Move the coverage files in GCOV_RESULTS_DIR and JSVM_RESULTS_DIR, so that the execution from now to shutdown (or next test) is not counted.
    await IOUtils.move(gcovPrefixPath, gcovResultsPath);
    await IOUtils.move(jsvmPrefixPath, jsvmResultsPath);
  }

  static afterTestSync() {
    awaitPromise(this.afterTest());
  }
};

PerTestCoverageUtils.enabled = !!gcovResultsPath;
