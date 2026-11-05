/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A bfcache history restore does not go through OnStartNavigation, so it must
// not overwrite the live record's finalHost while the armed client bounce
// detection timer keeps running. Otherwise the site the user just left is still
// in the bounce set but no longer the final host, and gets classified.

let bounceTrackingProtection = Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection);

// The history traversal has to land inside the pending finalization window.
// The test asserts that it did, so a slow run fails rather than passing without
// exercising the restore. The timer is long enough to not fire under chaos
// mode, so closing the tab finalizes the record.
const TIMER_PERIOD_MS = 60000;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
      ["privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec", 0],
      [
        "privacy.bounceTrackingProtection.clientBounceDetectionTimerPeriodMS",
        TIMER_PERIOD_MS,
      ],
      // The defect requires the page being left to be bfcached.
      ["browser.sessionhistory.max_total_viewers", 10],
    ],
  });
  registerCleanupFunction(() => {
    bounceTrackingProtection.clearAll();
  });
});

function getStartURL(origin) {
  return getBaseUrl(origin) + "file_start.html";
}

add_task(async function test_bfcache_restore_overwrites_finalhost() {
  bounceTrackingProtection.clearAll();

  let urlA = getStartURL(ORIGIN_A);
  let urlB = getStartURL(ORIGIN_B);
  let recordBounces;

  await BrowserTestUtils.withNewTab(urlA, async browser => {
    // Marker used to prove the traversal below was served from bfcache rather
    // than being a fresh load, which would go through OnStartNavigation and not
    // exhibit the defect.
    await SpecialPowers.spawn(browser, [], () => {
      content.window.wrappedJSObject.__btpBfcacheMarker = "present";
    });

    // A -> B with user activation. This ends A's extended navigation and starts a
    // new one with initialHost A. B's response puts B in the bounce set and arms
    // the finalization timer, and B's load sets finalHost to B, which is what
    // would exempt B. finalHost is set on the parent's STATE_STOP, which can
    // arrive after the content load event.
    let loadedB = BrowserTestUtils.browserStopped(browser, urlB);
    let timerArmedBefore = ChromeUtils.now();
    await navigateLinkClick(browser, new URL(urlB));
    await loadedB;

    // Listen before going back, in case the restore finalizes the record. The
    // A -> B finalization already ran synchronously in OnStartNavigation.
    recordBounces = waitForRecordBounces(browser, 0);

    // Go back to A. Deliberately chrome driven: clicking in B's content area
    // would record user activation for B and exempt it, masking the defect.
    let restored = BrowserTestUtils.waitForLocationChange(gBrowser, urlA);
    browser.goBack();
    await restored;
    Assert.less(
      ChromeUtils.now() - timerArmedBefore,
      TIMER_PERIOD_MS,
      "A should have been restored before the finalization timer fired, " +
        "otherwise this test is not exercising the restore path."
    );

    let fromBfcache = await SpecialPowers.spawn(browser, [], () => {
      return content.window.wrappedJSObject.__btpBfcacheMarker;
    });
    Assert.equal(
      fromBfcache,
      "present",
      "A should have been restored from bfcache, otherwise this test is not " +
        "exercising the restore path."
    );
  });
  await recordBounces;

  let candidates = bounceTrackingProtection
    .testGetBounceTrackerCandidateHosts({})
    .map(entry => entry.siteHost);

  Assert.ok(
    !candidates.includes(SITE_B),
    `${SITE_B} was navigated to by the user and was the final host, so it should ` +
      `not be classified after a Back to a bfcached page. ` +
      `Got: ${JSON.stringify(candidates)}`
  );
});

// Control for the task above: identical up to the history traversal, which is
// omitted. finalHost stays B, so B is exempt and nothing is classified. This is
// what isolates the restore as the cause rather than the navigation to B.
add_task(async function test_no_traversal_does_not_classify() {
  bounceTrackingProtection.clearAll();

  let urlB = getStartURL(ORIGIN_B);
  let recordBounces;

  await BrowserTestUtils.withNewTab(getStartURL(ORIGIN_A), async browser => {
    let loadedB = BrowserTestUtils.browserStopped(browser, urlB);
    await navigateLinkClick(browser, new URL(urlB));
    await loadedB;

    // No Back here. The same tab close finalizes the record.
    recordBounces = waitForRecordBounces(browser, 0);
  });
  await recordBounces;

  let candidates = bounceTrackingProtection
    .testGetBounceTrackerCandidateHosts({})
    .map(entry => entry.siteHost);

  Assert.ok(
    !candidates.includes(SITE_B),
    `${SITE_B} should not be classified without the history traversal. ` +
      `Got: ${JSON.stringify(candidates)}`
  );
});
