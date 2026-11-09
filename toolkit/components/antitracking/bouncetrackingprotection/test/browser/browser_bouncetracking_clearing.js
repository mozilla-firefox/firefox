/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let bounceTrackingProtection = Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
      ["privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec", 0],
    ],
  });
});

// Clearing a site must drop the purge log's bounce chain records which name it,
// not only the entries keyed by it: Report Broken Site sends the records.
add_task(async function test_clearing_a_site_drops_its_chain_records() {
  await BrowserTestUtils.withNewTab(
    getBaseUrl(ORIGIN_A) + "file_start.html",
    async browser => {
      let promiseRecordBounces = waitForRecordBounces(browser);
      let targetURL = new URL(getBaseUrl(ORIGIN_B) + "file_start.html");
      let targetLoaded = BrowserTestUtils.browserLoaded(
        browser,
        false,
        targetURL
      );
      await navigateLinkClick(
        browser,
        getBounceURL({ bounceType: "server", targetURL })
      );
      await targetLoaded;
      await navigateLinkClick(
        browser,
        new URL(getBaseUrl(ORIGIN_C) + "file_start.html")
      );
      await promiseRecordBounces;

      Assert.deepEqual(
        await bounceTrackingProtection.testRunPurgeBounceTrackers(),
        [SITE_TRACKER],
        "The bounce tracker is purged."
      );
      Assert.equal(
        bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A)
          .length,
        1,
        "The purge log entry names the initial site of its chain."
      );

      bounceTrackingProtection.clearBySiteHostAndOriginAttributesPattern(
        SITE_A,
        { userContextId: 5 }
      );
      Assert.equal(
        bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A)
          .length,
        1,
        "Clearing the site in another container keeps the record."
      );

      bounceTrackingProtection.clearBySiteHostAndOriginAttributesPattern(
        SITE_A,
        {}
      );
      Assert.deepEqual(
        bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_A),
        [],
        "No purge log entry names the cleared site."
      );
      Assert.deepEqual(
        bounceTrackingProtection.getRecentPurgedChainEntriesForSite(SITE_B),
        [],
        "The whole record is dropped, not just the cleared site."
      );
      let [entry, ...rest] =
        bounceTrackingProtection.testGetRecentlyPurgedTrackers({});
      Assert.equal(rest.length, 0, "The purge log keeps its single entry");
      Assert.equal(entry.siteHost, SITE_TRACKER, "for the bounce tracker,");
      Assert.equal(
        entry.bounceTrackingRecord,
        null,
        "but no longer its chain record."
      );

      bounceTrackingProtection.clearAll();
    }
  );
});

// Bounce tracking state is only kept for top level sites. Clearing a site's
// partitioned principal, as the sanitizer does for third-party data, must leave
// its first-party state alone.
add_task(async function test_clearing_a_partitioned_principal_keeps_state() {
  bounceTrackingProtection.testAddUserActivation({}, SITE_A, Date.now() * 1000);

  let partitionedPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI(ORIGIN_A),
      { partitionKey: `(https,${SITE_B})` }
    );
  await new Promise(resolve =>
    Services.clearData.deleteDataFromPrincipal(
      partitionedPrincipal,
      false,
      Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE,
      resolve
    )
  );
  Assert.deepEqual(
    bounceTrackingProtection
      .testGetUserActivationHosts({})
      .map(entry => entry.siteHost),
    [SITE_A],
    "Clearing the partitioned principal keeps the user activation."
  );

  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(ORIGIN_A),
    {}
  );
  await new Promise(resolve =>
    Services.clearData.deleteDataFromPrincipal(
      principal,
      false,
      Ci.nsIClearDataService.CLEAR_BOUNCE_TRACKING_PROTECTION_STATE,
      resolve
    )
  );
  Assert.deepEqual(
    bounceTrackingProtection.testGetUserActivationHosts({}),
    [],
    "Clearing the first-party principal clears the user activation."
  );

  bounceTrackingProtection.clearAll();
});
