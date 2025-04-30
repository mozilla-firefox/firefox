/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is a non-production file that needs to log.
/* eslint-disable no-console */
/* eslint-env node */

import { Transform, finished } from "node:stream";
import path, { dirname } from "node:path";
import { fileURLToPath } from "node:url";

const PROJECT_ROOT = path.resolve(
  dirname(fileURLToPath(import.meta.url)),
  "../../../../../"
);

// These constants help filter the events we care about.
const ENQUEUE_EVENT_DEPTH = 0;
const START_EVENT_DEPTH = 0;
const PASS_EVENT_DEPTH = 4;
const FAILURE_CODE = "testCodeFailure";

/**
 * Transforms Node test runner data into the format treeherder expects.
 */
class MozillaFormatter extends Transform {
  constructor() {
    super({ writableObjectMode: true });
    this.passes = 0;
    this.failures = [];
    this.ruleNameToFilePath = new Map();
    this.currentFilePath = "";
    this.currentRuleName = "";
  }
  _transform(event, _, callback) {
    let { type, data } = event;
    switch (type) {
      // Node test runner sends `test:enqueue` events for all files before
      // running any tests. We use this to build a map of rule name → test file
      // path so that we can lookup the file path later. This is pretty hack-y
      // since it relies on the test file name matching the rule name, but the
      // combination of Node test runner + stylelint-test-rule-node makes it
      // frustratingly hard to determine which file a test originates from.
      case "test:enqueue":
        if (
          data.nesting === ENQUEUE_EVENT_DEPTH &&
          !data.file.includes("stylelint-test-rule-node")
        ) {
          let relativePath = path.relative(PROJECT_ROOT, data.file);
          let ruleName = path.basename(
            relativePath,
            path.extname(relativePath)
          );
          this.ruleNameToFilePath.set(
            `stylelint-plugin-mozilla/${ruleName}`,
            relativePath
          );
          callback();
        } else {
          callback();
        }
        break;
      case "test:start":
        if (data.nesting === START_EVENT_DEPTH) {
          this.currentRuleName = data.name;
          this.currentFilePath = this.ruleNameToFilePath.get(data.name);
          callback(null, `SUITE-START | ${this.currentFilePath}\n`);
        } else {
          callback();
        }
        break;
      case "test:pass":
        if (data.nesting === PASS_EVENT_DEPTH) {
          this.passes++;
          callback(
            null,
            `TEST-PASS | ${this.currentFilePath} | ${data.name}\n`
          );
        } else {
          callback();
        }
        break;
      case "test:fail":
        if (data.details.error.failureType === FAILURE_CODE) {
          this.failures.push({
            testPath: this.currentFilePath,
            testName: data.name,
            message: data.details.error.message,
          });
          callback(
            null,
            `TEST-UNEXPECTED-FAIL | ${this.currentFilePath} | ${data.name} | ${data.details.error.message}\n`
          );
        } else {
          callback();
        }
        break;
      default:
        callback();
    }
  }
}

let reporter = new MozillaFormatter();

finished(reporter, err => {
  if (err) {
    console.error(`Error processing test data: ${err}`);
    process.exit(1);
  }

  // Space the results out visually with an additional blank line.
  console.log("");
  console.log("INFO | Result summary:");
  console.log(`INFO | Passed: ${reporter.passes}`);
  console.log(`INFO | Failed: ${reporter.failures.length}`);
  console.log("SUITE-END");
  // Space the failures out visually with an additional blank line.
  if (reporter.failures.length) {
    console.log("");
    console.log("Failure summary:");
    reporter.failures.map(({ testPath, testName, message }) => {
      console.log(`TEST-FAIL | ${testPath} | ${testName} | ${message}`);
    });
  }

  process.exit(reporter.failures.length);
});

export default reporter;
