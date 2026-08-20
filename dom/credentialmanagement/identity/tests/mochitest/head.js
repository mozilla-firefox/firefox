/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let test_path = "/tests/dom/credentialmanagement/identity/tests/mochitest";

async function setupTest(testName, idp_origin = "https://example.net") {
  let idp_api = idp_origin + test_path;
  ok(
    window.location.pathname.includes(testName),
    `Must set the right test name when setting up. Test name "${testName}" must be in URL path "${window.location.pathname}"`
  );
  let fetchPromise = fetch(
    `${idp_api}/server_manifest.sjs?set_test=${testName}`
  );
  let focusPromise = SimpleTest.promiseFocus();
  // The IdP cookie is set by the popup's response headers. Most accounts
  // endpoints reject requests without it, so wait for the popup to report back
  // rather than racing it.
  let cookiePromise = new Promise(resolve => {
    window.addEventListener("message", function listener(event) {
      if (event.data == "cookie-set") {
        window.removeEventListener("message", listener);
        resolve();
      }
    });
  });
  window.open(`${idp_api}/helper_set_cookie.html`, "_blank");
  await Promise.all([focusPromise, cookiePromise]);
  return fetchPromise;
}

async function clearIdentityCredentialStorage() {
  // Clear the storage before running test
  return SpecialPowers.spawnChrome([], () => {
    let icStorageService = Cc[
      "@mozilla.org/browser/identity-credential-storage-service;1"
    ].getService(Ci.nsIIdentityCredentialStorageService);
    icStorageService.clear();
  });
}
