/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* exported getImeiProvider, isValidImei, TEST_IMEI */

const IMEI_PROVIDER_CONTRACTID = "@mozilla.org/imei/provider;1";
const TEST_IMEI = "490154203237518";

function getImeiProvider() {
  return Cc[IMEI_PROVIDER_CONTRACTID].getService(Ci.nsIImeiProvider);
}

// An IMEI is 15 digits, the last one being a Luhn check digit.
function isValidImei(aImei) {
  if (!/^\d{15}$/.test(aImei)) {
    return false;
  }
  let sum = 0;
  for (let i = 0; i < aImei.length; i++) {
    let digit = Number(aImei[aImei.length - 1 - i]);
    if (i % 2) {
      digit *= 2;
      if (digit > 9) {
        digit -= 9;
      }
    }
    sum += digit;
  }
  return sum % 10 == 0;
}

add_task(function test_provider_registered() {
  Assert.ok(IMEI_PROVIDER_CONTRACTID in Cc, "IMEI provider is registered");
  const provider = getImeiProvider();
  Assert.ok(provider instanceof Ci.nsIImeiProvider, "Implements interface");
  Assert.equal(provider, getImeiProvider(), "Provider is a singleton");
});

add_task(async function test_provider_parent_only() {
  const inChild = await new Promise(resolve =>
    sendCommand(
      `${JSON.stringify(IMEI_PROVIDER_CONTRACTID)} in Components.classes`,
      resolve
    )
  );
  Assert.equal(inChild, "false", "IMEI provider is not registered in a child");
});
