/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

let btp;
let bounceTrackingGracePeriodSec;

/**
 * Wait for purge telemetry to be recorded for a list of site hosts.
 *
 * @param {Array} siteHosts - List of site hosts to be purged.
 */
function waitForPurgeTelemetry(siteHosts) {
  let expectedSiteHosts = new Set(siteHosts);

  return TestUtils.topicObserved(
    "bounce-tracking-protection-recorded-purge-telemetry",
    (_, siteHost) => {
      console.debug("observed purge telemetry", siteHost);
      expectedSiteHosts.delete(siteHost);
      // Resolve once all expected sites have been observed.
      return expectedSiteHosts.size == 0;
    }
  );
}

add_setup(async function () {
  Services.prefs.setIntPref(
    "privacy.bounceTrackingProtection.mode",
    Ci.nsIBounceTrackingProtection.MODE_ENABLED
  );

  btp = Cc["@mozilla.org/bounce-tracking-protection;1"].getService(
    Ci.nsIBounceTrackingProtection
  );

  // Reset global bounce tracking state.
  btp.clearAll();

  // Clear telemetry state.
  Services.fog.testResetFOG();

  bounceTrackingGracePeriodSec = Services.prefs.getIntPref(
    "privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec"
  );

  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

add_task(async function test_purging_increments_classified_tracker_counter() {
  let now = Date.now();
  let timestampOutsideGracePeriodThreeDays =
    now - (bounceTrackingGracePeriodSec + 60 * 60 * 24 * 3) * 1000;
  let timestampWithinGracePeriod =
    now - (bounceTrackingGracePeriodSec * 1000) / 2;

  // Bounce tracker that doesn't get purged.
  btp.testAddBounceTrackerCandidate(
    {},
    "example.com",
    timestampWithinGracePeriod * 1000
  );
  // Bounce tracker that is not classified as a tracker by list.
  btp.testAddBounceTrackerCandidate(
    {},
    "example.org",
    timestampOutsideGracePeriodThreeDays * 1000
  );
  // Bounce trackers that are known trackers.
  btp.testAddBounceTrackerCandidate(
    {},
    "itisatracker.org",
    timestampOutsideGracePeriodThreeDays * 1000
  );
  btp.testAddBounceTrackerCandidate(
    {},
    "trackertest.org",
    timestampOutsideGracePeriodThreeDays * 1000
  );

  Assert.equal(
    Glean.bounceTrackingProtection.purgeCountClassifiedTracker.testGetValue(),
    null,
    "No classified trackers purged yet"
  );

  info("Run PurgeBounceTrackers");
  let expectedPurgedHosts = [
    "example.org",
    "itisatracker.org",
    "trackertest.org",
  ];
  let telemetryPromise = waitForPurgeTelemetry(expectedPurgedHosts);

  let purgedHosts = await btp.testRunPurgeBounceTrackers();
  await telemetryPromise;

  Assert.deepEqual(
    purgedHosts.sort(),
    expectedPurgedHosts.sort(),
    "Purged the correct sites"
  );
  Assert.equal(
    Glean.bounceTrackingProtection.purgeCountClassifiedTracker.testGetValue(),
    2,
    "Purge counter for classified trackers is correct"
  );

  info("Re-run PurgeBounceTrackers with a known tracker.");
  btp.testAddBounceTrackerCandidate(
    {},
    "trackertest.org",
    timestampOutsideGracePeriodThreeDays * 1000
  );

  telemetryPromise = waitForPurgeTelemetry(["trackertest.org"]);
  purgedHosts = await btp.testRunPurgeBounceTrackers();
  await telemetryPromise;

  Assert.deepEqual(
    purgedHosts,
    ["trackertest.org"],
    "Purged the correct sites"
  );
  Assert.equal(
    Glean.bounceTrackingProtection.purgeCountClassifiedTracker.testGetValue(),
    3,
    "Purge counter for classified trackers has been incremented"
  );

  // Reset global bounce tracking state.
  btp.clearAll();

  // Clear telemetry state.
  Services.fog.testResetFOG();
});

add_task(async function test_purge_ping_submitted_on_idle_daily() {
  let submitted = false;
  GleanPings.bounceTrackingProtection.testBeforeNextSubmit(() => {
    submitted = true;
  });

  // The idle-daily category calls into the observer directly, it does not go
  // through the observer service.
  btp.QueryInterface(Ci.nsIObserver).observe(null, "idle-daily", null);

  Assert.ok(submitted, "idle-daily submitted the purge telemetry ping");
});
