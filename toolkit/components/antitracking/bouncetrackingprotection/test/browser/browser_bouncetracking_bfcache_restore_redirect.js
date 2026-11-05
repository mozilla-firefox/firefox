/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A bfcache history restore must end the extended navigation like any other
// history navigation. Otherwise, for A -> B -> Back (bfcached) -> A, followed by
// A redirecting or reloading without user activation, B is neither the initial
// nor the final host of the still open record and gets classified, even though
// the user navigated to it and it never redirected.

let bounceTrackingProtection = Cc[
  "@mozilla.org/bounce-tracking-protection;1"
].getService(Ci.nsIBounceTrackingProtection);

// The history traversal and the redirect have to land inside the finalization
// window armed by B's response. The test asserts that they did, so a slow run
// fails rather than passing without exercising the restore. The timer is long
// enough to not fire under chaos mode, so closing the tab finalizes the record.
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
    ],
  });
  registerCleanupFunction(() => {
    bounceTrackingProtection.clearAll();
  });
});

function getStartURL(origin) {
  return getBaseUrl(origin) + "file_start.html";
}

/**
 * Navigate A -> B by link click, go Back to A and let A navigate without user
 * activation, then check which sites the finalized record classified.
 *
 * @param {object} options
 * @param {boolean} options.expectBfcache - Whether A is restored from bfcache.
 * @param {boolean} [options.reload=false] - Whether A reloads itself instead of
 * redirecting to C. A is then reached by a link click, since a reload reuses the
 * triggering principal of A's history entry, and a system principal there would
 * count as user activation.
 */
async function runBackThenNavigate({ expectBfcache, reload = false }) {
  bounceTrackingProtection.clearAll();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionhistory.max_total_viewers", expectBfcache ? 10 : 0]],
  });

  let urlA = getStartURL(ORIGIN_A);
  let urlB = getStartURL(ORIGIN_B);
  let urlTarget = reload ? urlA : getStartURL(ORIGIN_C);
  let finalized;

  await BrowserTestUtils.withNewTab(
    reload ? getStartURL(ORIGIN_C) : urlA,
    async browser => {
      if (reload) {
        let loadedA = BrowserTestUtils.browserLoaded(browser, false, urlA);
        await navigateLinkClick(browser, new URL(urlA));
        await loadedA;
      }

      await SpecialPowers.spawn(browser, [], () => {
        content.window.wrappedJSObject.__btpBfcacheMarker = "present";
      });

      let loadedB = BrowserTestUtils.browserLoaded(browser, false, urlB);
      let timerArmedBefore = ChromeUtils.now();
      await navigateLinkClick(browser, new URL(urlB));
      await loadedB;

      // Chrome driven, so B gets no user activation.
      let restored = BrowserTestUtils.waitForLocationChange(gBrowser, urlA);
      browser.goBack();
      await restored;

      let fromBfcache = await SpecialPowers.spawn(browser, [], () => {
        return content.window.wrappedJSObject.__btpBfcacheMarker == "present";
      });
      Assert.equal(
        fromBfcache,
        expectBfcache,
        `A should ${expectBfcache ? "" : "not "}have been restored from bfcache.`
      );

      // A navigation ending the extended navigation finalizes the record before
      // this point, so only the finalization after A's navigation is observed.
      finalized = waitForRecordBounces(browser, 0);

      let loadedTarget = BrowserTestUtils.browserLoaded(
        browser,
        false,
        urlTarget
      );
      await SpecialPowers.spawn(
        browser,
        [urlTarget, reload],
        async (urlTarget, reload) => {
          // A's activation from the link click can still be transient.
          content.document.clearUserGestureActivation();
          Assert.ok(
            !content.document.hasValidTransientUserGestureActivation,
            "A has no transient user activation for the navigation."
          );
          // Page script, so A is the triggering principal.
          let script = content.document.createElement("script");
          script.textContent = reload
            ? "window.location.reload();"
            : `window.location.href = ${JSON.stringify(urlTarget)};`;
          content.document.body.appendChild(script);
        }
      );
      await loadedTarget;
      Assert.less(
        ChromeUtils.now() - timerArmedBefore,
        TIMER_PERIOD_MS,
        "A should have navigated before the timer armed by B's response fired, " +
          "otherwise this test is not exercising the restore."
      );
    }
  );
  await finalized;

  let candidates = bounceTrackingProtection
    .testGetBounceTrackerCandidateHosts({})
    .map(entry => entry.siteHost);

  Assert.ok(
    !candidates.includes(SITE_B),
    `${SITE_B} was navigated to by the user and never redirected, so it should ` +
      `not be classified. Got: ${JSON.stringify(candidates)}`
  );
  Assert.ok(
    !candidates.includes(SITE_A),
    `${SITE_A} has user activation from the link click, so it should not be ` +
      `classified. Got: ${JSON.stringify(candidates)}`
  );

  await SpecialPowers.popPrefEnv();
}

add_task(async function test_bfcache_restore_then_redirect() {
  await runBackThenNavigate({ expectBfcache: true });
});

add_task(async function test_bfcache_restore_then_reload() {
  await runBackThenNavigate({ expectBfcache: true, reload: true });
});

// Control: the same flow with bfcache disabled. Back is then a regular
// navigation which goes through OnStartNavigation and ends the extended
// navigation, with B as its final host.
add_task(async function test_back_without_bfcache_then_redirect() {
  await runBackThenNavigate({ expectBfcache: false });
});

/**
 * Navigate A -> B by link click, then let B go back to A with history.back()
 * and no user activation. B bounced the user, so it must be classified whether
 * or not A is restored from bfcache. A is reached by a link click, since a
 * system triggering principal on A's history entry would count as user
 * activation for the traversal.
 *
 * @param {object} options
 * @param {boolean} options.expectBfcache - Whether A is restored from bfcache.
 */
async function runContentBackWithoutActivation({ expectBfcache }) {
  bounceTrackingProtection.clearAll();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionhistory.max_total_viewers", expectBfcache ? 10 : 0]],
  });

  let urlA = getStartURL(ORIGIN_A);
  let urlB = getStartURL(ORIGIN_B);
  let finalized;

  await BrowserTestUtils.withNewTab(getStartURL(ORIGIN_C), async browser => {
    let loadedA = BrowserTestUtils.browserLoaded(browser, false, urlA);
    await navigateLinkClick(browser, new URL(urlA));
    await loadedA;

    await SpecialPowers.spawn(browser, [], () => {
      content.window.wrappedJSObject.__btpBfcacheMarker = "present";
    });

    let loadedB = BrowserTestUtils.browserLoaded(browser, false, urlB);
    await navigateLinkClick(browser, new URL(urlB));
    await loadedB;

    finalized = waitForRecordBounces(browser, 0);

    let restored = BrowserTestUtils.waitForLocationChange(gBrowser, urlA);
    await SpecialPowers.spawn(browser, [], () => {
      Assert.ok(
        !content.document.hasValidTransientUserGestureActivation,
        "B has no transient user activation for the traversal."
      );
      content.history.back();
    });
    await restored;

    let fromBfcache = await SpecialPowers.spawn(browser, [], () => {
      return content.window.wrappedJSObject.__btpBfcacheMarker == "present";
    });
    Assert.equal(
      fromBfcache,
      expectBfcache,
      `A should ${expectBfcache ? "" : "not "}have been restored from bfcache.`
    );
  });
  await finalized;

  let candidates = bounceTrackingProtection
    .testGetBounceTrackerCandidateHosts({})
    .map(entry => entry.siteHost);

  Assert.ok(
    candidates.includes(SITE_B),
    `${SITE_B} sent the user back without user activation, so it should be ` +
      `classified. Got: ${JSON.stringify(candidates)}`
  );

  await SpecialPowers.popPrefEnv();
}

add_task(async function test_bfcache_restore_by_content_without_activation() {
  await runContentBackWithoutActivation({ expectBfcache: true });
});

add_task(async function test_content_back_without_bfcache_without_activation() {
  await runContentBackWithoutActivation({ expectBfcache: false });
});
