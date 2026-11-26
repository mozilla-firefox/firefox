/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that the REGEXP_ constants UrlbarShared duplicates from UrlUtils stay
// the same as the originals.

"use strict";

const { UrlUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/UrlUtils.sys.mjs"
);

add_task(function test_regexps_match_UrlUtils() {
  // UrlbarShared also has its own REGEXP_ constants, so compare the ones that
  // exist in both.
  let names = Object.keys(UrlbarShared).filter(
    name => name.startsWith("REGEXP_") && name in UrlUtils
  );
  for (let name of ["REGEXP_SPACES", "REGEXP_PREFIX", "REGEXP_COMMON_EMAIL"]) {
    Assert.ok(names.includes(name), `${name} is in both modules`);
  }

  for (let name of names) {
    Assert.equal(
      UrlbarShared[name].source,
      UrlUtils[name].source,
      `${name} has the same pattern as UrlUtils`
    );
    Assert.equal(
      UrlbarShared[name].flags,
      UrlUtils[name].flags,
      `${name} has the same flags as UrlUtils`
    );
  }
});
