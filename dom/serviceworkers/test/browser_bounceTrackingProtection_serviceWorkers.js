"use strict";

/* import-globals-from browser_head.js */
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/dom/serviceworkers/test/browser_head.js",
  this
);

// This test verifies that a Bounce Tracking Protection (BTP) purge removes the
// service worker registration and Cache API storage of a classified bounce
// tracker, while leaving a non-tracker origin's service worker intact. BTP
// purges site data via nsIClearDataService with CLEAR_STATE_FOR_TRACKER_PURGING
// (which includes CLEAR_DOM_QUOTA); the BTP xpcshell tests (test_bouncetracking_purge.js)
// cover cookies and IndexedDB but not service workers.

const btp = Cc["@mozilla.org/bounce-tracking-protection;1"].getService(
  Ci.nsIBounceTrackingProtection
);

// The tracker and the benign origin live on distinct base domains so that the
// site-scoped BTP purge of the tracker cannot reach the benign origin.
const TRACKER_ORIGIN = "https://example.org";
const TRACKER_SITE = "example.org";
const BENIGN_ORIGIN = "https://example.com";
const BENIGN_SITE = "example.com";
const SW_SCRIPT = "empty.js";

add_setup(async function () {
  // Registered first so it runs on every exit path, including a setup that
  // throws partway through.
  registerCleanupFunction(async function () {
    await new Promise(resolve => {
      Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, resolve);
    });
    btp.clearAll();
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.serviceWorkers.enabled", true],
      ["dom.serviceWorkers.testing.enabled", true],
      ["dom.serviceWorkers.exemptFromPerDomainMax", true],
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
    ],
  });
});

add_task(async function test_btp_purge_removes_tracker_service_worker() {
  // Start from a clean BTP state so only the candidates added below matter.
  btp.clearAll();

  let trackerReg = await install_sw({
    origin: TRACKER_ORIGIN,
    script: SW_SCRIPT,
    scope: "empty.html?btp_tracker",
  });
  ok(trackerReg, "Service worker registered on the bounce-tracker origin");

  const trackerScriptCache = trackerReg.activeWorker.cacheName;
  Assert.ok(
    (await get_sw_script_cache_names(TRACKER_ORIGIN)).includes(
      trackerScriptCache
    ),
    "Bounce-tracker origin has a service worker script cache before purging"
  );

  let benignReg = await install_sw({
    origin: BENIGN_ORIGIN,
    script: SW_SCRIPT,
    scope: "empty.html?btp_benign",
  });
  ok(benignReg, "Service worker registered on the benign origin");
  const benignScriptCache = benignReg.activeWorker.cacheName;

  // Give both origins Cache API storage so they have quota usage to purge.
  await consume_storage(TRACKER_ORIGIN, {
    cacheBytes: 1024 * 1024,
    idbBytes: 0,
  });
  await consume_storage(BENIGN_ORIGIN, {
    cacheBytes: 1024 * 1024,
    idbBytes: 0,
  });

  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(TRACKER_ORIGIN)),
    "Tracker origin has storage before purging"
  );
  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(BENIGN_ORIGIN)),
    "Benign origin has storage before purging"
  );

  // Mark the tracker site as a bounce-tracker candidate with a bounce time far
  // in the past, outside the grace period, so the purge removes its data.
  btp.testAddBounceTrackerCandidate({}, TRACKER_SITE, 1);

  let purged = await btp.testRunPurgeBounceTrackers();
  Assert.ok(
    purged.includes(TRACKER_SITE),
    `BTP purged the bounce tracker (${JSON.stringify(purged)})`
  );

  Assert.equal(
    countRegistrationsForOrigin(TRACKER_ORIGIN),
    0,
    "Bounce-tracker service worker removed by BTP purge"
  );
  Assert.ok(
    is_minimum_origin_usage(await get_qm_origin_usage(TRACKER_ORIGIN)),
    "Bounce-tracker Cache API storage cleared by BTP purge"
  );
  Assert.ok(
    !(await get_sw_script_cache_names(TRACKER_ORIGIN)).includes(
      trackerScriptCache
    ),
    "Bounce-tracker service worker script cache removed by BTP purge"
  );

  Assert.greaterOrEqual(
    countRegistrationsForOrigin(BENIGN_ORIGIN),
    1,
    "Non-tracker service worker preserved after BTP purge"
  );
  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(BENIGN_ORIGIN)),
    "Non-tracker Cache API storage preserved after BTP purge"
  );
  Assert.ok(
    (await get_sw_script_cache_names(BENIGN_ORIGIN)).includes(
      benignScriptCache
    ),
    "Non-tracker service worker script cache preserved after BTP purge"
  );

  // The benign site was not a candidate, so the purge left its data in place.
  await clear_qm_origin_group_via_clearData(BENIGN_ORIGIN);
});

add_task(async function test_btp_purge_spares_origin_with_user_activation() {
  // Start from a clean BTP state so only the candidates added below matter.
  btp.clearAll();

  let trackerReg = await install_sw({
    origin: TRACKER_ORIGIN,
    script: SW_SCRIPT,
    scope: "empty.html?btp_activation_tracker",
  });
  ok(trackerReg, "Service worker registered on the bounce-tracker origin");

  let sparedReg = await install_sw({
    origin: BENIGN_ORIGIN,
    script: SW_SCRIPT,
    scope: "empty.html?btp_activation_spared",
  });
  ok(sparedReg, "Service worker registered on the user-activated origin");

  const trackerScriptCache = trackerReg.activeWorker.cacheName;
  const sparedScriptCache = sparedReg.activeWorker.cacheName;
  Assert.ok(
    (await get_sw_script_cache_names(TRACKER_ORIGIN)).includes(
      trackerScriptCache
    ),
    "Bounce-tracker origin has a service worker script cache before purging"
  );

  await consume_storage(TRACKER_ORIGIN, {
    cacheBytes: 1024 * 1024,
    idbBytes: 0,
  });
  await consume_storage(BENIGN_ORIGIN, {
    cacheBytes: 1024 * 1024,
    idbBytes: 0,
  });

  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(TRACKER_ORIGIN)),
    "Tracker origin has storage before purging"
  );
  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(BENIGN_ORIGIN)),
    "User-activated origin has storage before purging"
  );

  // Both sites bounced, so both are bounce-tracker candidates...
  btp.testAddBounceTrackerCandidate({}, TRACKER_SITE, 1);
  btp.testAddBounceTrackerCandidate({}, BENIGN_SITE, 1);

  // ...but the user has since interacted with the benign site. Recording a
  // user activation removes the site from the candidate list, so the purge
  // must leave it alone.
  btp.testAddUserActivation({}, BENIGN_SITE, Date.now() * 1000);

  // Check that the user activation removed the benign site from the candidate
  // list and kept the tracker in it, so we know the benign site is spared
  // because of the user activation.
  let candidates = btp
    .testGetBounceTrackerCandidateHosts({})
    .map(e => e.siteHost);
  Assert.ok(
    candidates.includes(TRACKER_SITE),
    `Tracker is still a bounce-tracker candidate (${JSON.stringify(candidates)})`
  );
  Assert.ok(
    !candidates.includes(BENIGN_SITE),
    "User activation removed the benign site from the candidate list"
  );

  let purged = await btp.testRunPurgeBounceTrackers();
  Assert.ok(
    purged.includes(TRACKER_SITE),
    `BTP purged the bounce tracker (${JSON.stringify(purged)})`
  );
  Assert.ok(
    !purged.includes(BENIGN_SITE),
    "BTP did not purge the user-activated site"
  );

  Assert.equal(
    countRegistrationsForOrigin(TRACKER_ORIGIN),
    0,
    "Bounce-tracker service worker removed by BTP purge"
  );
  Assert.ok(
    is_minimum_origin_usage(await get_qm_origin_usage(TRACKER_ORIGIN)),
    "Bounce-tracker Cache API storage cleared by BTP purge"
  );
  Assert.ok(
    !(await get_sw_script_cache_names(TRACKER_ORIGIN)).includes(
      trackerScriptCache
    ),
    "Bounce-tracker service worker script cache removed by BTP purge"
  );

  Assert.greaterOrEqual(
    countRegistrationsForOrigin(BENIGN_ORIGIN),
    1,
    "User-activated origin's service worker preserved"
  );
  Assert.ok(
    !is_minimum_origin_usage(await get_qm_origin_usage(BENIGN_ORIGIN)),
    "User-activated origin's Cache API storage preserved"
  );
  Assert.ok(
    (await get_sw_script_cache_names(BENIGN_ORIGIN)).includes(
      sparedScriptCache
    ),
    "User-activated origin's service worker script cache preserved"
  );

  // Clean up.
  await clear_qm_origin_group_via_clearData(BENIGN_ORIGIN);
});
