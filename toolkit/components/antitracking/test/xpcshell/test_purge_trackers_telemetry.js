/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TRACKING_PAGE = "https://tracking.example.org";
const BENIGN_PAGE = "https://example.com";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);
const { SiteDataTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SiteDataTestUtils.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);

// Get expected Glean histogram bucket value for timing distribution
// Use round=true for getting the bucket for a value returned from Glean and
// round=false to get a bucket for a specific time
function getExpBucketValue(timeNs, round) {
  const bucket = 8 * Math.log2(Number(timeNs) + 1);
  return round ? Math.round(bucket) : Math.floor(bucket);
}

function assertHistogram(histogram, index, expected, timeDist) {
  let found = false;
  if (timeDist) {
    index = getExpBucketValue(index);
  }
  for (let [i, val] of Object.entries(histogram?.values || [])) {
    if (timeDist) {
      i = getExpBucketValue(i, true);
    }
    if (i == index) {
      found = true;
      Assert.equal(
        val,
        expected,
        `expected counts should match for ${JSON.stringify(histogram)} at index ${i}`
      );
    } else {
      Assert.equal(
        val,
        0,
        `unexpected counts should be zero for ${JSON.stringify(histogram)} at index ${i}  -- expected ${expected} at ${index}`
      );
    }
  }
  Assert.ok(
    found,
    `Should have found an entry for ${JSON.stringify(histogram)} at index ${index} with expected ${expected}`
  );
}

XPCOMUtils.defineLazyServiceGetter(
  this,
  "PurgeTrackerService",
  "@mozilla.org/purge-tracker-service;1",
  Ci.nsIPurgeTrackerService
);

add_task(async function setup() {
  Services.prefs.setIntPref(
    "network.cookie.cookieBehavior",
    Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER
  );
  Services.prefs.setBoolPref("privacy.purge_trackers.enabled", true);
  Services.prefs.setStringPref(
    "urlclassifier.trackingAnnotationTable.testEntries",
    "tracking.example.org"
  );
  Services.prefs.setBoolPref(
    "toolkit.telemetry.testing.overrideProductsCheck",
    true
  );

  // Enables us to test localStorage in xpcshell.
  Services.prefs.setBoolPref("dom.storage.client_validation", false);

  // Needed for BounceTrackingProtection.
  do_get_profile(true);

  Services.fog.initializeFOG();
});

/**
 * Test telemetry for cookie purging.
 */
add_task(async function () {
  await UrlClassifierTestUtils.addTestTrackers();

  let FIVE_DAYS = 5 * 24 * 60 * 60 * 1000;

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    Services.perms.EXPIRE_TIME,
    Date.now() + FIVE_DAYS
  );

  SiteDataTestUtils.addToLocalStorage(TRACKING_PAGE);
  SiteDataTestUtils.addToCookies({ origin: BENIGN_PAGE });
  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE });
  await SiteDataTestUtils.addToIndexedDB(TRACKING_PAGE);

  Services.fog.testResetFOG();

  // Purge while storage access permission exists.
  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(TRACKING_PAGE),
    "cookie remains while storage access permission exists."
  );
  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "localStorage should not have been removed while storage access permission exists."
  );
  Assert.greater(
    await SiteDataTestUtils.getQuotaUsage(TRACKING_PAGE),
    0,
    `We have data for ${TRACKING_PAGE}`
  );

  await Services.fog.testFlushAllChildren();

  let purgedHistogram = Glean.cookiePurging.originsPurged.testGetValue();
  let notPurgedHistogram =
    Glean.cookiePurging.trackersWithUserInteraction.testGetValue();
  let remainingDaysHistogram =
    Glean.cookiePurging.trackersUserInteractionRemainingDays.testGetValue();
  let intervalHistogram = Glean.cookiePurging.intervalHours.testGetValue();

  const nsPerHour = 60 * 60 * 1e9;
  const nsPerDay = 24 * nsPerHour;

  assertHistogram(purgedHistogram, 0, 1);
  assertHistogram(notPurgedHistogram, 1, 1);
  assertHistogram(remainingDaysHistogram, 4 * nsPerDay, 2, true);
  // The interval between purges here is 0 hours, but Glean's timing
  // distribution clamps a sample of 0 to 1 (in the metric's time_unit) before
  // converting to nanoseconds, so 0 hours becomes 1 hour.
  assertHistogram(intervalHistogram, nsPerHour, 1, true);

  Services.fog.testResetFOG();

  // Run purge after storage access permission has been removed.
  PermissionTestUtils.remove(TRACKING_PAGE, "storageAccessAPI");
  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(BENIGN_PAGE),
    "A non-tracking page should retain cookies after purging"
  );

  // Cookie should have been removed.
  ok(
    !SiteDataTestUtils.hasCookies(TRACKING_PAGE),
    "cookie is removed after purge with no storage access permission."
  );
  ok(
    !SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "localStorage should not have been removed while storage access permission exists."
  );
  Assert.equal(
    await SiteDataTestUtils.getQuotaUsage(TRACKING_PAGE),
    0,
    "quota storage was deleted"
  );

  await Services.fog.testFlushAllChildren();

  purgedHistogram = Glean.cookiePurging.originsPurged.testGetValue();
  notPurgedHistogram =
    Glean.cookiePurging.trackersWithUserInteraction.testGetValue();
  intervalHistogram = Glean.cookiePurging.intervalHours.testGetValue();

  assertHistogram(purgedHistogram, 1, 1);
  Assert.equal(notPurgedHistogram.sum, 0, "no origins with user interaction");
  assertHistogram(intervalHistogram, nsPerHour, 1, true);

  await UrlClassifierTestUtils.cleanupTestTrackers();
});

/**
 * Test counting correctly across cookies batches
 */
add_task(async function () {
  await UrlClassifierTestUtils.addTestTrackers();

  // Enforce deleting the same origin twice by adding two cookies and setting
  // the max number of cookies per batch to 1.
  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE, name: "cookie1" });
  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE, name: "cookie2" });
  Services.prefs.setIntPref("privacy.purge_trackers.max_purge_count", 1);

  Services.fog.testResetFOG();

  await PurgeTrackerService.purgeTrackingCookieJars();

  // Cookie should have been removed.
  await TestUtils.waitForCondition(
    () => !SiteDataTestUtils.hasCookies(TRACKING_PAGE),
    "cookie is removed after purge."
  );

  await Services.fog.testFlushAllChildren();
  let purgedHistogram = Glean.cookiePurging.originsPurged.testGetValue();

  assertHistogram(purgedHistogram, 1, 1);

  Services.prefs.clearUserPref("privacy.purge_trackers.max_purge_count");
  await UrlClassifierTestUtils.cleanupTestTrackers();
});
