/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { browserVersionMatches } = ChromeUtils.importESModule(
  "chrome://remote/content/shared/webdriver/BrowserVersion.sys.mjs"
);

add_task(function test_browserVersionMatches() {
  for (const [browserVersion, requested, matches] of [
    ["50.0", "=50", true],
    ["50.1", "=50", true],
    ["50.1", "=50.1", true],
    ["50.1.1", "=50.1", true],
    ["50.0.0", "=50.0.0", true],
    ["50.0", "*", true],
    ["50.0", "x", true],
    ["50.0", "*,50", false],
    ["51.0.0", ">50", true],
    ["49.0", "<50", true],
    ["50.0", "<50.1", true],
    ["50.0.0", "<50.0.1", true],
    ["50.1.0", ">50", false],
    ["50.1.0", "<50", false],
    ["50.1.0", ">=50,<51", true],
    ["50.0a1", ">49.0", true],
    ["50.0a2", "=50", true],
    ["78.1.0esr", ">=78", true],
    ["78.1.0esr", "<79", true],
    ["78.1.11esr", "<79", true],
    ["50.0a2", ">50.0", false],
    ["1.3.0", "1.2", true],
    ["2.0.0", "1.2", false],
    ["1.2.4", "~1.2.3", true],
    ["1.3.0", "~1.2.3", false],
    ["1.2.3", "1.2.*", true],
    ["1.3.0", "1.2.*", false],
    ["1.2.3", "1.*.*", true],
    ["1.2.3", "=1.*.3", false],
    ["123.0", "=123.0+local", false],
    ["123.0", "=123.0.0+local", true],
    ["1.2.3", "=1.2.3-alpha", false],
    ["1.2.3", "=1.2.3-01", false],
    ["1.2.3", ">=1.2.3-0a", true],
    ["1.2.3", ">=1.2.3-alpha", true],
    ["1.2.2", ">=1.2.3-alpha", false],
    ["1.2.3", ">1.2.3-alpha", true],
    ["1.2.2", ">1.2.3-alpha", false],
    ["1.2.3", "^1.2.3-beta.1", true],
    ["50.0a2", "=50.0.0-alpha", false],
    ["50.0a2", ">=50.0.0-alpha", true],
  ]) {
    Assert.equal(
      browserVersionMatches(browserVersion, requested),
      matches,
      `Got expected match for browserVersion="${browserVersion}" and ` +
        `requested="${requested}"`
    );
  }
});

add_task(function test_browserVersionMatches_invalid() {
  for (const [browserVersion, requested] of [
    ["", "50"],
    ["50", ""],
    ["invalid", "50"],
    ["50", "invalid"],
    ["50.0", "=1.2.3.4"],
    ["50.0", "=50.*.1"],
    ["50.0", "=50.0.0-01"],
    ["", "*"],
    ["50", "x"],
  ]) {
    Assert.equal(
      browserVersionMatches(browserVersion, requested),
      false,
      `Got invalid match for browserVersion="${browserVersion}" and ` +
        `requested="${requested}"`
    );
  }
});
