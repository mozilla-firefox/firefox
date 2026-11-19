/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SiteDataTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SiteDataTestUtils.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);

let btp;
let bounceTrackingGracePeriodSec;
let bounceTrackingActivationLifetimeSec;

/**
 * Adds brackets to a host if it's an IPv6 address.
 *
 * @param {string} host - Host which may be an IPv6.
 * @returns {string} bracketed IPv6 or host if host is not an IPv6.
 */
function maybeFixupIpv6(host) {
  if (!host.includes(":")) {
    return host;
  }
  // Already bracketed.
  if (host.startsWith("[") && host.endsWith("]")) {
    return host;
  }
  return `[${host}]`;
}

/**
 * Adds cookies and indexedDB test data for the given host.
 *
 * @param {string} host
 */
async function addStateForHost(host) {
  info(`Populating cookies and indexedDB for host ${host}`);
  SiteDataTestUtils.addToCookies({ host });
  await SiteDataTestUtils.addToIndexedDB(`https://${maybeFixupIpv6(host)}`);
}

/**
 * Checks if the given host as cookies or indexedDB data.
 *
 * @param {string} host
 * @returns {boolean}
 */
async function hasStateForHost(host) {
  let origin = `https://${maybeFixupIpv6(host)}`;
  if (SiteDataTestUtils.hasCookies(origin)) {
    return true;
  }
  return SiteDataTestUtils.hasIndexedDB(origin);
}

/**
 * Assert that there are no bounce tracker candidates or user activations
 * recorded.
 */
function assertEmpty() {
  Assert.equal(
    btp.testGetBounceTrackerCandidateHosts({}).length,
    0,
    "No tracker candidates."
  );
  Assert.equal(
    btp.testGetUserActivationHosts({}).length,
    0,
    "No user activation hosts."
  );
}

add_setup(function () {
  Services.prefs.setIntPref(
    "privacy.bounceTrackingProtection.mode",
    Ci.nsIBounceTrackingProtection.MODE_ENABLED
  );

  btp = Cc["@mozilla.org/bounce-tracking-protection;1"].getService(
    Ci.nsIBounceTrackingProtection
  );

  // Reset global bounce tracking state.
  btp.clearAll();

  bounceTrackingGracePeriodSec = Services.prefs.getIntPref(
    "privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec"
  );
  bounceTrackingActivationLifetimeSec = Services.prefs.getIntPref(
    "privacy.bounceTrackingProtection.bounceTrackingActivationLifetimeSec"
  );
});

/**
 * When both maps are empty running PurgeBounceTrackers should be a no-op.
 */
add_task(async function test_empty() {
  assertEmpty();

  info("Run PurgeBounceTrackers");
  await btp.testRunPurgeBounceTrackers();

  assertEmpty();
});

/**
 * Tests that the PurgeBounceTrackers behaves as expected by adding site state
 * and adding simulated bounce state and user activations.
 */
add_task(async function test_purge() {
  let now = Date.now();

  // Epoch in MS.
  let timestampWithinGracePeriod =
    now - (bounceTrackingGracePeriodSec * 1000) / 2;
  let timestampWithinGracePeriod2 =
    now - (bounceTrackingGracePeriodSec * 1000) / 4;
  let timestampOutsideGracePeriodFiveSeconds =
    now - (bounceTrackingGracePeriodSec + 5) * 1000;
  let timestampOutsideGracePeriodThreeDays =
    now - (bounceTrackingGracePeriodSec + 60 * 60 * 24 * 3) * 1000;
  let timestampFuture = now + bounceTrackingGracePeriodSec * 1000 * 2;

  let timestampValidUserActivation =
    now - (bounceTrackingActivationLifetimeSec * 1000) / 2;
  let timestampExpiredUserActivationFourSeconds =
    now - (bounceTrackingActivationLifetimeSec + 4) * 1000;
  let timestampExpiredUserActivationTenDays =
    now - (bounceTrackingActivationLifetimeSec + 60 * 60 * 24 * 10) * 1000;

  const TEST_TRACKERS = {
    "example.com": {
      bounceTime: timestampWithinGracePeriod,
      userActivationTime: null,
      message: "Should not purge within grace period.",
      shouldPurge: bounceTrackingGracePeriodSec == 0,
    },
    "example2.com": {
      bounceTime: timestampWithinGracePeriod2,
      userActivationTime: null,
      message: "Should not purge within grace period (2).",
      shouldPurge: bounceTrackingGracePeriodSec == 0,
    },
    "example.net": {
      bounceTime: timestampOutsideGracePeriodFiveSeconds,
      userActivationTime: null,
      message: "Should purge after grace period.",
      shouldPurge: true,
    },
    // Don't purge if the site is allowlisted.
    // Allowlist via "trackingprotection" permission (ETP toggle).
    "example2.net": {
      bounceTime: timestampOutsideGracePeriodFiveSeconds,
      userActivationTime: null,
      allowListedPerm: "trackingprotection",
      message:
        "Should not purge after grace period if allowlisted via 'trackingprotection' permission.",
      shouldPurge: false,
    },
    // Allowlist via "cookie" permission
    "example3.net": {
      bounceTime: timestampOutsideGracePeriodFiveSeconds,
      userActivationTime: null,
      allowListedPerm: "cookie",
      permissionState: Services.perms.ALLOW_ACTION,
      message:
        "Should not purge after grace period if allowlisted via 'cookie' permission.",
      shouldPurge: false,
    },
    // "cookie" permission but not allowlisted (deny action).
    "example4.net": {
      bounceTime: timestampOutsideGracePeriodFiveSeconds,
      userActivationTime: null,
      allowListedPerm: "cookie",
      permissionState: Services.perms.DENY_ACTION,
      message:
        "Should get purged outside of grace period with a 'cookie' DENY permission.",
      shouldPurge: true,
    },
    // Also ensure that clear data calls with IP sites succeed.
    "1.2.3.4": {
      bounceTime: timestampOutsideGracePeriodThreeDays,
      userActivationTime: null,
      message: "Should purge after grace period (2).",
      shouldPurge: true,
    },
    "[2606:4700:4700::1111]": {
      bounceTime: timestampOutsideGracePeriodThreeDays,
      userActivationTime: null,
      message: "Should purge after grace period (3).",
      shouldPurge: true,
    },
    "example.org": {
      bounceTime: timestampWithinGracePeriod,
      userActivationTime: null,
      message: "Should not purge within grace period.",
      shouldPurge: false,
    },
    "example2.org": {
      bounceTime: timestampFuture,
      userActivationTime: null,
      message: "Should not purge for future bounce time (within grace period).",
      shouldPurge: false,
    },
    "1.1.1.1": {
      bounceTime: null,
      userActivationTime: timestampValidUserActivation,
      message: "Should not purge without bounce (valid user activation).",
      shouldPurge: false,
    },
    // Also testing domains with trailing ".".
    "mozilla.org.": {
      bounceTime: null,
      userActivationTime: timestampExpiredUserActivationFourSeconds,
      message: "Should not purge without bounce (expired user activation).",
      shouldPurge: false,
    },
    "firefox.com": {
      bounceTime: null,
      userActivationTime: timestampExpiredUserActivationTenDays,
      message: "Should not purge without bounce (expired user activation) (2).",
      shouldPurge: false,
    },
  };

  info("Assert empty initially.");
  assertEmpty();

  info("Populate bounce and user activation sets.");

  let expectedBounceTrackerHosts = [];
  let expectedUserActivationHosts = [];
  let allowListedHosts = [];

  let expiredUserActivationHosts = [];
  let expectedPurgedHosts = [];

  // This would normally happen over time while browsing.
  let initPromises = Object.entries(TEST_TRACKERS).map(
    async ([
      siteHost,
      {
        message,
        bounceTime,
        userActivationTime,
        allowListedPerm,
        permissionState = Services.perms.ALLOW_ACTION,
        shouldPurge,
      },
    ]) => {
      info(`Initializing state for test ${siteHost}: ${message}`);

      // Add site state so we can later assert it has been purged.
      await addStateForHost(siteHost);

      // Add allowlist entry if needed.
      if (allowListedPerm == "trackingprotection") {
        PermissionTestUtils.add(
          `https://${siteHost}`,
          "trackingprotection",
          permissionState
        );
        allowListedHosts.push(siteHost);
      } else if (allowListedPerm == "cookie") {
        PermissionTestUtils.add(
          `https://${siteHost}`,
          "cookie",
          permissionState
        );
        allowListedHosts.push(siteHost);
      }

      if (bounceTime != null) {
        if (userActivationTime != null) {
          throw new Error(
            "Attempting to construct invalid map state. testGetBounceTrackerCandidateHosts({}) and testGetUserActivationHosts({}) must be disjoint."
          );
        }

        expectedBounceTrackerHosts.push(siteHost);

        // Convert bounceTime timestamp to microseconds (PRTime).
        info(
          `Adding bounce. siteHost: ${siteHost}, bounceTime: ${bounceTime} ms`
        );
        btp.testAddBounceTrackerCandidate({}, siteHost, bounceTime * 1000);
      }

      if (userActivationTime != null) {
        if (bounceTime != null) {
          throw new Error(
            "Attempting to construct invalid map state. testGetBounceTrackerCandidateHosts({}) and testGetUserActivationHosts({}) must be disjoint."
          );
        }

        expectedUserActivationHosts.push(siteHost);
        if (
          userActivationTime + bounceTrackingActivationLifetimeSec * 1000 >
          now
        ) {
          expiredUserActivationHosts.push(siteHost);
        }

        // Convert userActivationTime timestamp to microseconds (PRTime).
        info(
          `Adding user interaction. siteHost: ${siteHost}, userActivationTime: ${userActivationTime} ms`
        );
        btp.testAddUserActivation({}, siteHost, userActivationTime * 1000);
      }

      if (shouldPurge) {
        expectedPurgedHosts.push(siteHost);
      }
    }
  );
  await Promise.all(initPromises);

  info(
    "Check that bounce and user activation data has been correctly recorded."
  );
  Assert.deepEqual(
    btp
      .testGetBounceTrackerCandidateHosts({})
      .map(entry => entry.siteHost)
      .sort(),
    expectedBounceTrackerHosts.sort(),
    "Has added bounce tracker hosts."
  );
  Assert.deepEqual(
    btp
      .testGetUserActivationHosts({})
      .map(entry => entry.siteHost)
      .sort(),
    expectedUserActivationHosts.sort(),
    "Has added user activation hosts."
  );

  info("Run PurgeBounceTrackers");
  let actualPurgedHosts = await btp.testRunPurgeBounceTrackers();

  Assert.deepEqual(
    actualPurgedHosts.sort(),
    expectedPurgedHosts.sort(),
    "Should have purged all expected hosts."
  );

  // After the purge only the bounce trackers that have not been purged should
  // remain in the candidate map. Additionally the allowlisted hosts should be
  // removed from the candidate map.
  let expectedBounceTrackerHostsAfterPurge = expectedBounceTrackerHosts
    .filter(
      host =>
        !expectedPurgedHosts.includes(host) && !allowListedHosts.includes(host)
    )
    .sort();
  Assert.deepEqual(
    btp
      .testGetBounceTrackerCandidateHosts({})
      .map(entry => entry.siteHost)
      .sort(),
    expectedBounceTrackerHostsAfterPurge.sort(),
    "After purge the bounce tracker candidate host set should be updated correctly."
  );

  Assert.deepEqual(
    btp
      .testGetUserActivationHosts({})
      .map(entry => entry.siteHost)
      .sort(),
    expiredUserActivationHosts.sort(),
    "After purge any expired user activation records should have been removed"
  );

  info("Test that we actually purged the correct sites.");
  for (let siteHost of expectedPurgedHosts) {
    Assert.ok(
      !(await hasStateForHost(siteHost)),
      `Site ${siteHost} should no longer have state.`
    );
  }
  for (let siteHost of expectedBounceTrackerHostsAfterPurge) {
    Assert.ok(
      await hasStateForHost(siteHost),
      `Site ${siteHost} should still have state.`
    );
  }

  info("Reset bounce tracking state.");
  btp.clearAll();
  assertEmpty();

  info("Clean up site data and permissions.");
  await SiteDataTestUtils.clear();
  Services.perms.removeAll();
});

/**
 * Tests that default "bounce-tracking-protection-exception" permissions, as
 * set by remote permissions, exempt the whole site from purging in normal
 * browsing, containers and private browsing.
 */
add_task(async function test_purge_remote_exception_permission() {
  const PERM_TYPE = "bounce-tracking-protection-exception";
  const OAS = [{}, { userContextId: 2 }, { privateBrowsingId: 1 }];
  let bounceTime =
    (Date.now() - (bounceTrackingGracePeriodSec + 5) * 1000) * 1000;

  let allowPrincipal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI("https://www.example.com"),
    {}
  );
  let denyPrincipal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI("https://example.net"),
    {}
  );
  Services.perms.addDefaultFromPrincipal(
    allowPrincipal,
    PERM_TYPE,
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addDefaultFromPrincipal(
    denyPrincipal,
    PERM_TYPE,
    Services.perms.DENY_ACTION
  );

  for (let oa of OAS) {
    btp.testAddBounceTrackerCandidate(oa, "example.com", bounceTime);
    btp.testAddBounceTrackerCandidate(oa, "example.net", bounceTime);
    btp.testAddBounceTrackerCandidate(oa, "example.org", bounceTime);
  }

  let purgedHosts = await btp.testRunPurgeBounceTrackers();
  Assert.deepEqual(
    purgedHosts.sort(),
    OAS.flatMap(() => ["example.net", "example.org"]).sort(),
    `Should only purge sites without a ${PERM_TYPE} ALLOW permission.`
  );
  for (let oa of OAS) {
    Assert.deepEqual(
      btp.testGetBounceTrackerCandidateHosts(oa).map(entry => entry.siteHost),
      [],
      `Allowlisted and purged hosts are removed from the candidate map for ${JSON.stringify(oa)}.`
    );
  }

  info(`Removing the ${PERM_TYPE} permission makes the site purgeable again.`);
  Services.perms.addDefaultFromPrincipal(
    allowPrincipal,
    PERM_TYPE,
    Services.perms.UNKNOWN_ACTION
  );
  btp.testAddBounceTrackerCandidate({}, "example.com", bounceTime);
  Assert.deepEqual(
    await btp.testRunPurgeBounceTrackers(),
    ["example.com"],
    `Should purge the site after its ${PERM_TYPE} permission was removed.`
  );

  Services.perms.addDefaultFromPrincipal(
    denyPrincipal,
    PERM_TYPE,
    Services.perms.UNKNOWN_ACTION
  );
  btp.clearAll();
  assertEmpty();
  Services.perms.removeAll();
});
