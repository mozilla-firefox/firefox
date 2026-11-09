/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

add_task(async function test_imei_not_implemented() {
  await Assert.rejects(
    getImeiProvider().imei,
    e => e.result == Cr.NS_ERROR_NOT_IMPLEMENTED,
    "Provider is not implemented"
  );
});
