/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Every result the provider rejects with when no usable modem is found.
const EXPECTED_FAILURES = [Cr.NS_ERROR_FAILURE, Cr.NS_ERROR_NOT_IMPLEMENTED];

// CI machines have no modem, but developer machines may have one.
add_task(async function test_imei_settles() {
  try {
    const imei = await getImeiProvider().imei;
    info(`Got IMEI ${imei.slice(0, 3)}...${imei.slice(-3)}`);
    Assert.ok(isValidImei(imei), "Resolved value is a valid IMEI");
  } catch (e) {
    info(`IMEI rejected with ${e}`);
    Assert.ok(
      EXPECTED_FAILURES.includes(e.result),
      "Rejected with an expected error"
    );
  }
});
