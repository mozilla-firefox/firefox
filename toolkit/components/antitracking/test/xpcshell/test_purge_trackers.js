/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TRACKING_PAGE = "https://tracking.example.org";
const HTTP_TRACKING_PAGE = "http://tracking.example.org";
const TRACKING_PAGE2 =
  "https://tracking.example.org^partitionKey=(https,example.com)";
const HTTP_TRACKING_PAGE2 =
  "http://tracking.example.org^partitionKey=(https,example.com)";
const BENIGN_PAGE = "https://example.com";
const FOREIGN_PAGE = "https://example.net";
const FOREIGN_PAGE2 = "https://example.net^partitionKey=(https,example.com)";
const FOREIGN_PAGE3 = "https://example.net^partitionKey=(https,example.org)";

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);
const { SiteDataTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SiteDataTestUtils.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "PurgeTrackerService",
  "@mozilla.org/purge-tracker-service;1",
  Ci.nsIPurgeTrackerService
);

async function assertQuotaUsage(urls, assertFn) {
  const usages = await Promise.all(
    urls.map(url => SiteDataTestUtils.getQuotaUsage(url))
  );
  for (let i = 0; i < urls.length; i++) {
    assertFn(usages[i], urls[i]);
  }
}

async function setupTest(aCookieBehavior) {
  Services.prefs.setIntPref("network.cookie.cookieBehavior", aCookieBehavior);
  Services.prefs.setBoolPref("privacy.purge_trackers.enabled", true);
  Services.prefs.setCharPref("privacy.purge_trackers.logging.level", "Debug");
  Services.prefs.setStringPref(
    "urlclassifier.trackingAnnotationTable.testEntries",
    "tracking.example.org"
  );

  // Enables us to test localStorage in xpcshell.
  Services.prefs.setBoolPref("dom.storage.client_validation", false);
}

/**
 * Test that purging doesn't happen when it shouldn't happen.
 */
add_task(async function testNotPurging() {
  await UrlClassifierTestUtils.addTestTrackers();
  setupTest(Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN);
  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE });

  Services.prefs.setIntPref(
    "network.cookie.cookieBehavior",
    Ci.nsICookieService.BEHAVIOR_ACCEPT
  );
  await PurgeTrackerService.purgeTrackingCookieJars();
  ok(SiteDataTestUtils.hasCookies(TRACKING_PAGE), "cookie remains.");
  Services.prefs.setIntPref(
    "network.cookie.cookieBehavior",
    Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN
  );

  Services.prefs.setBoolPref("privacy.purge_trackers.enabled", false);
  await PurgeTrackerService.purgeTrackingCookieJars();
  ok(SiteDataTestUtils.hasCookies(TRACKING_PAGE), "cookie remains.");
  Services.prefs.setBoolPref("privacy.purge_trackers.enabled", true);

  Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.history", true);
  await PurgeTrackerService.purgeTrackingCookieJars();
  ok(SiteDataTestUtils.hasCookies(TRACKING_PAGE), "cookie remains.");
  Services.prefs.clearUserPref("privacy.sanitize.sanitizeOnShutdown");
  Services.prefs.clearUserPref("privacy.clearOnShutdown.history");

  await PurgeTrackerService.purgeTrackingCookieJars();
  ok(!SiteDataTestUtils.hasCookies(TRACKING_PAGE), "cookie cleared.");

  await UrlClassifierTestUtils.cleanupTestTrackers();
});

/**
 * Test that cookies indexedDB and localStorage are purged if the cookie is found
 * on the tracking list and does not have an Interaction Permission.
 */
async function testIndexedDBAndLocalStorage() {
  await UrlClassifierTestUtils.addTestTrackers();

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION
  );

  SiteDataTestUtils.addToCookies({ origin: BENIGN_PAGE });
  const urls = [
    TRACKING_PAGE,
    TRACKING_PAGE2,
    FOREIGN_PAGE,
    FOREIGN_PAGE2,
    FOREIGN_PAGE3,
  ];
  for (let url of urls) {
    SiteDataTestUtils.addToLocalStorage(url);
    SiteDataTestUtils.addToCookies({ origin: url });
  }
  await Promise.all(urls.map(url => SiteDataTestUtils.addToIndexedDB(url)));

  // Purge while storage access permission exists.
  await PurgeTrackerService.purgeTrackingCookieJars();

  for (let url of [TRACKING_PAGE, TRACKING_PAGE2]) {
    ok(
      SiteDataTestUtils.hasCookies(url),
      "cookie remains while storage access permission exists."
    );
    ok(
      SiteDataTestUtils.hasLocalStorage(url),
      "localStorage should not have been removed while storage access permission exists."
    );
  }
  await assertQuotaUsage([TRACKING_PAGE, TRACKING_PAGE2], (usage, url) => {
    Assert.greater(usage, 0, `We have data for ${url}`);
  });

  // Run purge after storage access permission has been removed.
  PermissionTestUtils.remove(TRACKING_PAGE, "storageAccessAPI");
  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(BENIGN_PAGE),
    "A non-tracking page should retain cookies after purging"
  );

  for (let url of [FOREIGN_PAGE, FOREIGN_PAGE2, FOREIGN_PAGE3]) {
    ok(
      SiteDataTestUtils.hasCookies(url),
      `A non-tracking foreign page should retain cookies after purging`
    );
    ok(
      SiteDataTestUtils.hasLocalStorage(url),
      `localStorage for ${url} should not have been removed.`
    );
  }
  await assertQuotaUsage(
    [FOREIGN_PAGE, FOREIGN_PAGE2, FOREIGN_PAGE3],
    (usage, url) => {
      Assert.greater(usage, 0, `We have data for ${url}`);
    }
  );

  // Cookie should have been removed.

  for (let url of [TRACKING_PAGE, TRACKING_PAGE2]) {
    ok(
      !SiteDataTestUtils.hasCookies(url),
      "cookie is removed after purge with no storage access permission."
    );
    ok(
      !SiteDataTestUtils.hasLocalStorage(url),
      "localStorage should have been removed"
    );
  }
  await assertQuotaUsage([TRACKING_PAGE, TRACKING_PAGE2], usage => {
    Assert.equal(usage, 0, "quota storage was deleted");
  });

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/**
 * Test that trackers are treated based on their base domain, not origin.
 */
async function testBaseDomain() {
  await UrlClassifierTestUtils.addTestTrackers();

  let associatedOrigins = [
    "https://itisatracker.org",
    "https://sub.itisatracker.org",
    "https://www.itisatracker.org",
    "https://sub.sub.sub.itisatracker.org",
    "http://itisatracker.org",
    "http://sub.itisatracker.org",
  ];

  for (let permissionOrigin of associatedOrigins) {
    // Only one of the associated origins gets permission, but
    // all should be exempt from purging.
    PermissionTestUtils.add(
      permissionOrigin,
      "storageAccessAPI",
      Services.perms.ALLOW_ACTION
    );

    for (let origin of associatedOrigins) {
      SiteDataTestUtils.addToCookies({ origin });
    }

    // Add another tracker to verify we're actually purging.
    SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE });

    await PurgeTrackerService.purgeTrackingCookieJars();

    for (let origin of associatedOrigins) {
      ok(
        SiteDataTestUtils.hasCookies(origin),
        `${origin} should have retained its cookies when permission is set for ${permissionOrigin}.`
      );
    }

    ok(
      !SiteDataTestUtils.hasCookies(TRACKING_PAGE),
      "cookie is removed after purge with no storage access permission."
    );

    PermissionTestUtils.remove(permissionOrigin, "storageAccessAPI");
    await SiteDataTestUtils.clear();
  }

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/**
 * Test that trackers are not cleared if they are associated
 * with an entry on the entity list that has user interaction.
 */
async function testUserInteraction(ownerPage) {
  Services.prefs.setBoolPref(
    "privacy.purge_trackers.consider_entity_list",
    true
  );
  // The test URL for the entity list for annotation is
  // itisatrap.org/?resource=example.org, so we need to
  // add example.org as a tracker.
  Services.prefs.setCharPref(
    "urlclassifier.trackingAnnotationTable.testEntries",
    "example.org"
  );
  await UrlClassifierTestUtils.addTestTrackers();

  // example.org and itisatrap.org are hard coded test values on the entity list.
  const RESOURCE_PAGE = "https://example.org";

  PermissionTestUtils.add(
    ownerPage,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION
  );

  SiteDataTestUtils.addToCookies({ origin: RESOURCE_PAGE });

  // Add another tracker to verify we're actually purging.
  SiteDataTestUtils.addToCookies({
    origin: "https://another-tracking.example.net",
  });

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(RESOURCE_PAGE),
    `${RESOURCE_PAGE} should have retained its cookies when permission is set for ${ownerPage}.`
  );

  ok(
    !SiteDataTestUtils.hasCookies("https://another-tracking.example.net"),
    "cookie is removed after purge with no storage access permission."
  );

  Services.prefs.setBoolPref(
    "privacy.purge_trackers.consider_entity_list",
    false
  );

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    !SiteDataTestUtils.hasCookies(RESOURCE_PAGE),
    `${RESOURCE_PAGE} should not have retained its cookies when permission is set for ${ownerPage} and the entity list pref is off.`
  );

  PermissionTestUtils.remove(ownerPage, "storageAccessAPI");
  await SiteDataTestUtils.clear();

  Services.prefs.clearUserPref("privacy.purge_trackers.consider_entity_list");
  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/**
 * Test that quota storage (even without cookies) is considered when purging trackers.
 */
async function testQuotaStorage() {
  await UrlClassifierTestUtils.addTestTrackers();

  let testCases = [
    { localStorage: true, indexedDB: true },
    { localStorage: false, indexedDB: true },
    { localStorage: true, indexedDB: false },
  ];

  for (let { localStorage, indexedDB } of testCases) {
    PermissionTestUtils.add(
      TRACKING_PAGE,
      "storageAccessAPI",
      Services.perms.ALLOW_ACTION
    );

    if (localStorage) {
      for (let url of [
        TRACKING_PAGE,
        TRACKING_PAGE2,
        BENIGN_PAGE,
        FOREIGN_PAGE,
        FOREIGN_PAGE2,
        FOREIGN_PAGE3,
      ]) {
        SiteDataTestUtils.addToLocalStorage(url);
      }
    }

    if (indexedDB) {
      await Promise.all(
        [
          TRACKING_PAGE,
          TRACKING_PAGE2,
          BENIGN_PAGE,
          FOREIGN_PAGE,
          FOREIGN_PAGE2,
          FOREIGN_PAGE3,
        ].map(url => SiteDataTestUtils.addToIndexedDB(url))
      );
    }

    // Purge while storage access permission exists.
    await PurgeTrackerService.purgeTrackingCookieJars();

    if (localStorage) {
      ok(
        SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
        "localStorage should not have been removed while storage access permission exists."
      );
    }

    if (indexedDB) {
      await assertQuotaUsage(
        [
          TRACKING_PAGE,
          TRACKING_PAGE2,
          FOREIGN_PAGE,
          FOREIGN_PAGE2,
          FOREIGN_PAGE3,
        ],
        (usage, url) => Assert.greater(usage, 0, `We have data for ${url}`)
      );
    }

    // Run purge after storage access permission has been removed.
    PermissionTestUtils.remove(TRACKING_PAGE, "storageAccessAPI");
    await PurgeTrackerService.purgeTrackingCookieJars();

    if (localStorage) {
      for (let url of [
        BENIGN_PAGE,
        FOREIGN_PAGE,
        FOREIGN_PAGE2,
        FOREIGN_PAGE3,
      ]) {
        ok(
          SiteDataTestUtils.hasLocalStorage(url),
          "localStorage should not have been removed for non-tracking page."
        );
      }
      for (let url of [TRACKING_PAGE, TRACKING_PAGE2]) {
        ok(
          !SiteDataTestUtils.hasLocalStorage(url),
          "localStorage should have been removed."
        );
      }
    }

    if (indexedDB) {
      await assertQuotaUsage(
        [BENIGN_PAGE, FOREIGN_PAGE, FOREIGN_PAGE2, FOREIGN_PAGE3],
        usage =>
          Assert.greater(
            usage,
            0,
            "quota storage for non-tracking page was not deleted"
          )
      );
      await assertQuotaUsage([TRACKING_PAGE, TRACKING_PAGE2], usage =>
        Assert.equal(usage, 0, "quota storage was deleted")
      );
    }

    await SiteDataTestUtils.clear();
  }

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/**
 * Test that we correctly delete cookies and storage for sites
 * with an expired interaction permission.
 */
async function testExpiredInteractionPermission() {
  await UrlClassifierTestUtils.addTestTrackers();

  for (let url of [
    TRACKING_PAGE,
    TRACKING_PAGE2,
    FOREIGN_PAGE,
    FOREIGN_PAGE2,
    FOREIGN_PAGE3,
  ]) {
    SiteDataTestUtils.addToLocalStorage(url);
    SiteDataTestUtils.addToCookies({ origin: url });
  }
  await Promise.all(
    [
      TRACKING_PAGE,
      TRACKING_PAGE2,
      FOREIGN_PAGE,
      FOREIGN_PAGE2,
      FOREIGN_PAGE3,
    ].map(url => SiteDataTestUtils.addToIndexedDB(url))
  );

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    Services.perms.EXPIRE_TIME,
    Date.now() + 500
  );

  // Purge while storage access permission exists.
  await PurgeTrackerService.purgeTrackingCookieJars();

  for (let url of [
    TRACKING_PAGE,
    TRACKING_PAGE2,
    FOREIGN_PAGE,
    FOREIGN_PAGE2,
    FOREIGN_PAGE3,
  ]) {
    ok(
      SiteDataTestUtils.hasCookies(url),
      "cookie remains while storage access permission exists."
    );
    ok(
      SiteDataTestUtils.hasLocalStorage(url),
      "localStorage should not have been removed while storage access permission exists."
    );
  }
  await assertQuotaUsage(
    [TRACKING_PAGE, TRACKING_PAGE2, FOREIGN_PAGE, FOREIGN_PAGE2, FOREIGN_PAGE3],
    (usage, url) => Assert.greater(usage, 0, `We have data for ${url}`)
  );

  // Run purge after storage access permission has been removed.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(c => setTimeout(c, 500));
  await PurgeTrackerService.purgeTrackingCookieJars();

  // Cookie should have been removed.
  for (let url of [TRACKING_PAGE, TRACKING_PAGE2]) {
    ok(
      !SiteDataTestUtils.hasCookies(url),
      "cookie is removed after purge with no storage access permission."
    );
    ok(
      !SiteDataTestUtils.hasLocalStorage(url),
      "localStorage should not have been removed while storage access permission exists."
    );
  }
  await assertQuotaUsage([TRACKING_PAGE, TRACKING_PAGE2], usage =>
    Assert.equal(usage, 0, "quota storage was deleted")
  );

  // Cookie should not have been removed.
  for (let url of [FOREIGN_PAGE, FOREIGN_PAGE2, FOREIGN_PAGE3]) {
    ok(
      SiteDataTestUtils.hasCookies(url),
      "cookie remains while storage access permission exists."
    );
    ok(
      SiteDataTestUtils.hasLocalStorage(url),
      "localStorage should not have been removed while storage access permission exists."
    );
  }

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/*
 * Test that we correctly do or do not purges cookies
 * from sites given thier cookie permissions.
 */
async function testNotPurgingFromAllowedWebsites() {
  await UrlClassifierTestUtils.addTestTrackers();

  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE });
  SiteDataTestUtils.addToCookies({ origin: TRACKING_PAGE2 });

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "cookie",
    Ci.nsICookiePermission.ACCESS_ALLOW
  );

  PermissionTestUtils.add(
    TRACKING_PAGE2,
    "cookie",
    Ci.nsICookiePermission.ACCESS_SESSION
  );

  ok(
    SiteDataTestUtils.hasCookies(TRACKING_PAGE),
    "Cookie is set to the initial state for Tracking Page 1"
  );
  ok(
    SiteDataTestUtils.hasCookies(TRACKING_PAGE2),
    "Cookie is set to the initial state for Tracking Page 2"
  );

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(TRACKING_PAGE),
    "Cookie was not purged for Tracking Page 1"
  );
  ok(
    !SiteDataTestUtils.hasCookies(TRACKING_PAGE2),
    "Cookie was purged for Tracking Page 2"
  );

  PermissionTestUtils.remove(TRACKING_PAGE, "cookie");

  PermissionTestUtils.remove(TRACKING_PAGE2, "cookie");

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/*
 * Testing that Local Storage is not purged
 * from sites based thier cookie permissions.
 */
async function testNotPurgingLocalStorage() {
  await UrlClassifierTestUtils.addTestTrackers();

  SiteDataTestUtils.addToLocalStorage(TRACKING_PAGE);
  SiteDataTestUtils.addToLocalStorage(TRACKING_PAGE2);

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "cookie",
    Ci.nsICookiePermission.ACCESS_ALLOW
  );

  PermissionTestUtils.add(
    TRACKING_PAGE2,
    "cookie",
    Ci.nsICookiePermission.ACCESS_SESSION
  );

  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "Local Storage  is set to the initial state for Tracking Page 1"
  );
  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE2),
    "Local Storage  is set to the initial state for Tracking Page 2"
  );

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "Local Storage was not purged for Tracking Page 1"
  );
  ok(
    !SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE2),
    "Local Storage  was not purged for Tracking Page 2"
  );

  PermissionTestUtils.remove(TRACKING_PAGE, "cookie");

  PermissionTestUtils.remove(TRACKING_PAGE2, "cookie");

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/*
 * Test that we correctly do or do not purges cookies
 * from http sites given thier cookie permissions.
 */
async function testNotPurgingFromHTTP() {
  await UrlClassifierTestUtils.addTestTrackers();

  SiteDataTestUtils.addToCookies({ origin: HTTP_TRACKING_PAGE });
  SiteDataTestUtils.addToCookies({
    origin: HTTP_TRACKING_PAGE2,
  });

  PermissionTestUtils.add(
    HTTP_TRACKING_PAGE,
    "cookie",
    Ci.nsICookiePermission.ACCESS_ALLOW
  );

  PermissionTestUtils.add(
    HTTP_TRACKING_PAGE2,
    "cookie",
    Ci.nsICookiePermission.ACCESS_SESSION
  );

  ok(
    SiteDataTestUtils.hasCookies(HTTP_TRACKING_PAGE),
    "Cookie is set to the initial state for HTTP Tracking Page 1"
  );
  ok(
    SiteDataTestUtils.hasCookies(HTTP_TRACKING_PAGE2),
    "Cookie is set to the initial state for HTTP Tracking Page 2"
  );

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasCookies(HTTP_TRACKING_PAGE),
    "Cookie was not purged for HTTP Tracking Page 1"
  );
  ok(
    !SiteDataTestUtils.hasCookies(HTTP_TRACKING_PAGE2),
    "Cookie was purged for HTTP Tracking Page 2"
  );

  PermissionTestUtils.remove(HTTP_TRACKING_PAGE, "cookie");

  PermissionTestUtils.remove(HTTP_TRACKING_PAGE2, "cookie");

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

/*
 * Test that we correctly do or do not purges local storage
 * from http sites if https site has preserve cookies permission
 */
async function testNotPurgingFromDifferentScheme() {
  await UrlClassifierTestUtils.addTestTrackers();

  SiteDataTestUtils.addToLocalStorage(TRACKING_PAGE);
  SiteDataTestUtils.addToLocalStorage(HTTP_TRACKING_PAGE);

  PermissionTestUtils.add(
    TRACKING_PAGE,
    "cookie",
    Ci.nsICookiePermission.ACCESS_ALLOW
  );

  PermissionTestUtils.add(
    HTTP_TRACKING_PAGE,
    "cookie",
    Ci.nsICookiePermission.ACCESS_SESSION
  );

  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "Local Storage is set to the initial state for HTTPS Tracking Page "
  );
  ok(
    SiteDataTestUtils.hasLocalStorage(HTTP_TRACKING_PAGE),
    "Local Storage is set to the initial state for HTTP Tracking Page"
  );

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(
    SiteDataTestUtils.hasLocalStorage(TRACKING_PAGE),
    "Local Storage was not purged for HTTPS Tracking Page "
  );
  ok(
    SiteDataTestUtils.hasLocalStorage(HTTP_TRACKING_PAGE),
    "Local Storage  was not purged for HTTP Tracking Page"
  );

  PermissionTestUtils.remove(TRACKING_PAGE, "cookie");

  PermissionTestUtils.remove(HTTP_TRACKING_PAGE, "cookie");

  await UrlClassifierTestUtils.cleanupTestTrackers();
}

add_task(async function () {
  const cookieBehaviors = [
    Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN,
    Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN,
    Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
    Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
  ];

  for (let cookieBehavior of cookieBehaviors) {
    await setupTest(cookieBehavior);
    await testIndexedDBAndLocalStorage();
    await testBaseDomain();
    // example.org and itisatrap.org are hard coded test values on the entity list.
    await testUserInteraction("https://itisatrap.org");
    await testUserInteraction(
      "https://itisatrap.org^firstPartyDomain=example.net"
    );
    await testQuotaStorage();
    await testExpiredInteractionPermission();
    await testNotPurgingFromAllowedWebsites();
    await testNotPurgingLocalStorage();
    await testNotPurgingFromHTTP();
    await testNotPurgingFromDifferentScheme();
  }
});

/**
 * Test that purging trackers does not clear Bounce Tracking Protection state
 * for the purged sites.
 */
add_task(async function testNotPurgingBounceTrackingProtectionState() {
  await UrlClassifierTestUtils.addTestTrackers();
  await setupTest(Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN);

  Services.prefs.setIntPref(
    "privacy.bounceTrackingProtection.mode",
    Ci.nsIBounceTrackingProtection.MODE_ENABLED
  );
  let btp = Cc["@mozilla.org/bounce-tracking-protection;1"].getService(
    Ci.nsIBounceTrackingProtection
  );
  btp.clearAll();

  const ACTIVATION_TRACKER = "https://tracking.example.org";
  const CANDIDATE_TRACKER = "https://itisatracker.org";
  let now = Date.now() * 1000;
  btp.testAddUserActivation({}, "example.org", now);
  btp.testAddBounceTrackerCandidate({}, "itisatracker.org", now);
  SiteDataTestUtils.addToCookies({ origin: ACTIVATION_TRACKER });
  SiteDataTestUtils.addToCookies({ origin: CANDIDATE_TRACKER });

  await PurgeTrackerService.purgeTrackingCookieJars();

  ok(!SiteDataTestUtils.hasCookies(ACTIVATION_TRACKER), "cookie cleared.");
  ok(!SiteDataTestUtils.hasCookies(CANDIDATE_TRACKER), "cookie cleared.");
  Assert.deepEqual(
    btp.testGetUserActivationHosts({}).map(entry => entry.siteHost),
    ["example.org"],
    "BTP user activation for the purged site is retained."
  );
  Assert.deepEqual(
    btp.testGetBounceTrackerCandidateHosts({}).map(entry => entry.siteHost),
    ["itisatracker.org"],
    "BTP bounce tracker candidate for the purged site is retained."
  );

  btp.clearAll();
  await SiteDataTestUtils.clear();
  await UrlClassifierTestUtils.cleanupTestTrackers();
  Services.prefs.clearUserPref("privacy.bounceTrackingProtection.mode");
});
