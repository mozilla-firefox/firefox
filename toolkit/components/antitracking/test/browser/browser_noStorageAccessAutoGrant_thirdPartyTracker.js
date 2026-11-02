/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/modules/test/browser/head.js",
  this
);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/antitracking/test/browser/storage_access_head.js",
  this
);

const TEST_ENTITY_LIST_DOMAIN = "https://www.itisatrap.org/";
const TEST_ENTITY_LIST_PAGE = TEST_ENTITY_LIST_DOMAIN + TEST_PATH + "page.html";

add_setup(async function () {
  await setPreferences();

  await SpecialPowers.pushPrefEnv({
    set: [
      // The entity list is url-classifier only, so keep it as the channel
      // classifier where the content classifier is the default engine.
      ["privacy.trackingprotection.content.mirror.mode", 0],
      [
        "privacy.trackingprotection.urlclassifier.disable_for_channel_classifier",
        false,
      ],
      ["dom.storage_access.max_concurrent_auto_grants", 2],
      ["dom.storage_access.auto_grants.exclude_third_party_trackers", true],
    ],
  });

  await UrlClassifierTestUtils.addTestTrackers();

  registerCleanupFunction(async _ => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

add_task(async function test_noAutoGrant_thirdPartyTracker() {
  // Grant the storageAccessAPI permission to the third-party tracker. This
  // indicates that the tracker has been interacted with. This is necessary to
  // trigger the auto-grant logic.
  const uri = Services.io.newURI(TEST_3RD_PARTY_DOMAIN);
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );
  Services.perms.addFromPrincipal(
    principal,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION
  );

  // Ensure that the third-party tracker does not get autoGranted storage
  // access.
  await openPageAndRunCode(
    TEST_TOP_PAGE,
    getExpectPopupAndClick("reject"),
    TEST_3RD_PARTY_PAGE,
    requestStorageAccessAndExpectFailure
  );

  await clearSiteTestData();
});

add_task(async function test_autoGrant_entityList() {
  // Grant the storageAccessAPI permission to the third-party tracker. This
  // indicates that the tracker has been interacted with. This is necessary to
  // trigger the auto-grant logic.
  const uri = Services.io.newURI(TEST_3RD_PARTY_DOMAIN);
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );
  Services.perms.addFromPrincipal(
    principal,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION
  );

  // Ensure that third-party trackers in the entity list can still be
  // auto-granted.
  await openPageAndRunCode(
    TEST_ENTITY_LIST_PAGE,
    expectNoPopup,
    TEST_3RD_PARTY_PAGE,
    requestStorageAccessAndExpectSuccess
  );

  await clearSiteTestData();
});
