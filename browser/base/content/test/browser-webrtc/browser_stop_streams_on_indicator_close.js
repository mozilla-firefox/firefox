/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content/",
  "https://example.com/"
);
const TEST_PAGE = TEST_ROOT + "get_user_media.html";

add_setup(async function () {
  let prefs = [
    [PREF_PERMISSION_FAKE, true],
    [PREF_AUDIO_LOOPBACK, ""],
    [PREF_VIDEO_LOOPBACK, ""],
    [PREF_FAKE_STREAMS, true],
    [PREF_FOCUS_SOURCE, false],
  ];
  await SpecialPowers.pushPrefEnv({ set: prefs });
});

/**
 * Tests that if the indicator is closed somehow by the user when streams
 * still ongoing, that all of those streams it represents are stopped, and
 * the most recent tab that a stream was shared with is selected.
 *
 * This test makes sure the global mute toggles for camera and microphone
 * are disabled, so the indicator only represents display streams, and only
 * those streams should be stopped on close.
 */
add_task(async function test_close_indicator_no_global_toggles() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.webrtc.globalMuteToggles", false]],
  });

  let indicatorPromise = promiseIndicatorWindow();

  info("Opening first tab");
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing camera, microphone and screen");
  await shareDevices(tab1.linkedBrowser, true, true, SHARE_SCREEN, false);

  info("Opening second tab");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing camera, microphone and screen");
  await shareDevices(tab2.linkedBrowser, true, true, SHARE_SCREEN, true);

  info("Opening third tab");
  let tab3 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing screen");
  await shareDevices(tab3.linkedBrowser, false, false, SHARE_SCREEN, false);

  info("Opening fourth tab");
  let tab4 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  Assert.equal(
    gBrowser.selectedTab,
    tab4,
    "Most recently opened tab is selected"
  );

  let indicator = await indicatorPromise;

  indicator.close();

  // Wait a tick of the event loop to give the unload handler in the indicator
  // a chance to run.
  await new Promise(resolve => executeSoon(resolve));

  // Make sure the media capture state has a chance to flush up to the parent.
  await getMediaCaptureState();

  // The camera and microphone streams should still be active.
  let camStreams = webrtcUI.getActiveStreams(true, false);
  Assert.equal(camStreams.length, 2, "Should have found two camera streams");
  let micStreams = webrtcUI.getActiveStreams(false, true);
  Assert.equal(
    micStreams.length,
    2,
    "Should have found two microphone streams"
  );

  // The camera and microphone permission were remembered for tab2, so check to
  // make sure that the permissions remain.
  let { state: camState, scope: camScope } = SitePermissions.getForPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "camera",
    tab2.linkedBrowser
  );
  Assert.equal(camState, SitePermissions.ALLOW);
  Assert.equal(camScope, SitePermissions.SCOPE_PERSISTENT);

  let { state: micState, scope: micScope } = SitePermissions.getForPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "microphone",
    tab2.linkedBrowser
  );
  Assert.equal(micState, SitePermissions.ALLOW);
  Assert.equal(micScope, SitePermissions.SCOPE_PERSISTENT);

  Assert.equal(
    gBrowser.selectedTab,
    tab3,
    "Most recently tab that streams were shared with is selected"
  );

  SitePermissions.removeFromPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "camera",
    tab2.linkedBrowser
  );

  SitePermissions.removeFromPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "microphone",
    tab2.linkedBrowser
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab3);
  BrowserTestUtils.removeTab(tab4);
});

/**
 * Tests that if the indicator is closed somehow by the user when streams
 * still ongoing, that all of those streams is represents are stopped, and
 * the most recent tab that a stream was shared with is selected.
 *
 * This test makes sure the global mute toggles are enabled. This means that
 * when the user manages to close the indicator, we should revoke camera
 * and microphone permissions too.
 */
add_task(async function test_close_indicator_with_global_toggles() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.webrtc.globalMuteToggles", true]],
  });

  let indicatorPromise = promiseIndicatorWindow();

  info("Opening first tab");
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing camera, microphone and screen");
  await shareDevices(tab1.linkedBrowser, true, true, SHARE_SCREEN, false);

  info("Opening second tab");
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing camera, microphone and screen");
  await shareDevices(tab2.linkedBrowser, true, true, SHARE_SCREEN, true);

  info("Opening third tab");
  let tab3 = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
  info("Sharing screen");
  await shareDevices(tab3.linkedBrowser, false, false, SHARE_SCREEN, false);

  info("Opening fourth tab");
  let tab4 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  Assert.equal(
    gBrowser.selectedTab,
    tab4,
    "Most recently opened tab is selected"
  );

  let indicator = await indicatorPromise;

  indicator.close();

  // Wait a tick of the event loop to give the unload handler in the indicator
  // a chance to run.
  await new Promise(resolve => executeSoon(resolve));

  Assert.deepEqual(
    await getMediaCaptureState(),
    {},
    "expected nothing to be shared"
  );

  // getMediaCaptureState only round-trips with the selected tab. webrtcUI
  // hears about the other stopped streams from their own content processes,
  // and nothing notifies once the last of them has arrived.
  await TestUtils.waitForCondition(
    () => !webrtcUI.getActiveStreams(true, true, true, true).length,
    "Should have found no active streams"
  );

  // The camera and microphone permissions should have been cleared.
  let { state: camState } = SitePermissions.getForPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "camera",
    tab2.linkedBrowser
  );
  Assert.equal(camState, SitePermissions.UNKNOWN);

  let { state: micState } = SitePermissions.getForPrincipal(
    tab2.linkedBrowser.contentPrincipal,
    "microphone",
    tab2.linkedBrowser
  );
  Assert.equal(micState, SitePermissions.UNKNOWN);

  Assert.equal(
    gBrowser.selectedTab,
    tab3,
    "Most recently tab that streams were shared with is selected"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  BrowserTestUtils.removeTab(tab3);
  BrowserTestUtils.removeTab(tab4);
});
