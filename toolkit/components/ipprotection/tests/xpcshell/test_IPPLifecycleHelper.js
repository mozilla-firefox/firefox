/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { IPPLifecycleHelper } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPLifecycleHelper.sys.mjs"
);

const SLEEP_TOPIC = "sleep_notification";
const WAKE_TOPIC = "wake_notification";

add_setup(async function () {
  await putServerInRemoteSettings();
});

/**
 * Brings the proxy to the ACTIVE state, at which point the lifecycle helper is
 * observing sleep/wake.
 */
async function startActiveProxy() {
  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.init();
  await readyEvent;

  const activeEvent = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );
  await IPPProxyManager.start();
  await activeEvent;
}

/**
 * On sleep, the channel filter is suspended (proxyInfo cleared).
 */
add_task(async function test_sleep_suspends_connection() {
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  const channelFilter = IPPProxyManager.channelFilter();
  Assert.ok(channelFilter.proxyInfo, "Connection has proxyInfo while active");
  Assert.ok(IPPProxyManager.isolationKey, "Connection has an isolation key");

  Services.obs.notifyObservers(null, SLEEP_TOPIC);

  Assert.equal(
    channelFilter.proxyInfo,
    null,
    "Channel filter is suspended after sleep"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
});

/**
 * On wake with a still-good pass, the saved isolation key is re-injected
 * (same key, no rotation).
 */
add_task(async function test_wake_with_valid_pass_resumes_same_key() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  const channelFilter = IPPProxyManager.channelFilter();
  const savedKey = IPPProxyManager.isolationKey;

  const rotateSpy = sandbox.spy(IPPProxyManager, "rotateProxyPass");

  Services.obs.notifyObservers(null, SLEEP_TOPIC);
  Assert.equal(channelFilter.proxyInfo, null, "Suspended after sleep");

  Services.obs.notifyObservers(null, WAKE_TOPIC);

  Assert.ok(channelFilter.proxyInfo, "Connection resumed after wake");
  Assert.equal(
    IPPProxyManager.isolationKey,
    savedKey,
    "Resuming reuses the isolation key captured before sleep"
  );
  Assert.ok(rotateSpy.notCalled, "A valid pass should not trigger a rotation");

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * On wake with an expired pass, the helper triggers a rotation, which yields a
 * fresh isolation key.
 */
add_task(async function test_wake_with_expired_pass_rotates() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  // Rotate to an expired pass while staying active so canResume is false on the
  // next wake.
  setupStubs({ validProxyPass: false });
  await IPPProxyManager.rotateProxyPass();
  Assert.ok(
    !IPPProxyManager.hasValidProxyPass,
    "Pass is expired after rotating to an expired pass"
  );

  const savedKey = IPPProxyManager.isolationKey;
  const rotateSpy = sandbox.spy(IPPProxyManager, "rotateProxyPass");

  Services.obs.notifyObservers(null, SLEEP_TOPIC);

  // The wake rotation should fetch a fresh, valid pass.
  setupStubs({ validProxyPass: true });
  Services.obs.notifyObservers(null, WAKE_TOPIC);

  Assert.ok(rotateSpy.called, "An expired pass should trigger a rotation");
  await rotateSpy.returnValues[0];

  Assert.ok(channelFilterProxyInfoPresent(), "Connection has proxyInfo again");
  Assert.notEqual(
    IPPProxyManager.isolationKey,
    savedKey,
    "Rotation produces a new isolation key"
  );
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "Proxy stays active across a wake rotation"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * On wake with a pass that is valid but already inside its rotation window, the
 * helper rotates instead of resuming (validates the canResume threshold).
 */
add_task(async function test_wake_within_rotation_window_rotates() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  // Rotate to a pass that is still valid but due for rotation (expires in 30s,
  // well within the 2 minute rotation window).
  const now = Temporal.Now.instant();
  const soonToExpire = new ProxyPass(
    createProxyPassToken(now, now.add({ seconds: 30 }))
  );
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: soonToExpire,
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "3026-02-01T00:00:00.000Z"
    ),
  });
  await IPPProxyManager.rotateProxyPass();

  Assert.ok(
    IPPProxyManager.hasValidProxyPass,
    "Pass is still valid before its expiry"
  );
  Assert.ok(
    !IPPProxyManager.channelFilter().canResume,
    "Pass within the rotation window cannot be resumed"
  );

  const savedKey = IPPProxyManager.isolationKey;
  const rotateSpy = sandbox.spy(IPPProxyManager, "rotateProxyPass");

  Services.obs.notifyObservers(null, SLEEP_TOPIC);

  setupStubs({ validProxyPass: true });
  Services.obs.notifyObservers(null, WAKE_TOPIC);

  Assert.ok(
    rotateSpy.called,
    "A pass within the rotation window should trigger a rotation"
  );
  await rotateSpy.returnValues[0];

  Assert.notEqual(
    IPPProxyManager.isolationKey,
    savedKey,
    "Rotation produces a new isolation key"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * The wake handler rotates in network-error-tolerant mode so a post-wake outage
 * keeps retrying instead of erroring out (the recovery itself is covered in
 * test_IPProxyManager.js).
 */
add_task(async function test_wake_rotates_tolerant_of_network_errors() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  // Rotate to an expired pass while active so the next wake must rotate.
  setupStubs({ validProxyPass: false });
  await IPPProxyManager.rotateProxyPass();

  const rotateSpy = sandbox.spy(IPPProxyManager, "rotateProxyPass");
  setupStubs({ validProxyPass: true });

  Services.obs.notifyObservers(null, SLEEP_TOPIC);
  Services.obs.notifyObservers(null, WAKE_TOPIC);

  Assert.deepEqual(
    rotateSpy.firstCall?.args,
    [{ abortOnNetworkError: false }],
    "Wake rotates in network-error-tolerant mode"
  );
  await rotateSpy.returnValues[0];

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * Sleep/wake observers are only registered while the proxy is ACTIVE.
 */
add_task(async function test_observers_only_active_while_active() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  await IPPProxyManager.stop();
  Assert.notEqual(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "Proxy is no longer active after stop"
  );

  const observeSpy = sandbox.spy(IPPLifecycleHelper, "observe");
  Services.obs.notifyObservers(null, SLEEP_TOPIC);
  Services.obs.notifyObservers(null, WAKE_TOPIC);

  Assert.ok(
    observeSpy.notCalled,
    "The helper stops observing power events once the proxy is inactive"
  );

  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * On Android the helper reacts to the app moving to the background/foreground
 * the same way it reacts to sleep/wake on desktop: suspend on background, resume
 * on foreground.
 */
add_task(async function test_android_background_foreground_cycle() {
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  const channelFilter = IPPProxyManager.channelFilter();
  const savedKey = IPPProxyManager.isolationKey;

  Services.obs.notifyObservers(null, "application-background");
  Assert.equal(
    channelFilter.proxyInfo,
    null,
    "Channel filter is suspended when the app is backgrounded"
  );

  Services.obs.notifyObservers(null, "application-foreground");
  Assert.ok(
    channelFilter.proxyInfo,
    "Connection resumed when the app is foregrounded"
  );
  Assert.equal(
    IPPProxyManager.isolationKey,
    savedKey,
    "Resuming reuses the isolation key captured before backgrounding"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
});

/**
 * isSuspended reflects the sleep/background state, tracking both the desktop
 * sleep/wake and the Android background/foreground notifications.
 */
add_task(async function test_isSuspended_tracks_sleep_wake() {
  setupStubs({ validProxyPass: true });
  await startActiveProxy();

  Assert.ok(
    !IPPLifecycleHelper.isSuspended,
    "Not suspended while awake and active"
  );

  Services.obs.notifyObservers(null, SLEEP_TOPIC);
  Assert.ok(IPPLifecycleHelper.isSuspended, "Suspended after sleep");

  Services.obs.notifyObservers(null, WAKE_TOPIC);
  Assert.ok(!IPPLifecycleHelper.isSuspended, "Not suspended after wake");

  Services.obs.notifyObservers(null, "application-background");
  Assert.ok(
    IPPLifecycleHelper.isSuspended,
    "Suspended when the app is backgrounded"
  );

  Services.obs.notifyObservers(null, "application-foreground");
  Assert.ok(
    !IPPLifecycleHelper.isSuspended,
    "Not suspended when the app is foregrounded"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
});

/**
 * A successful rotation reschedules the next one for the freshly obtained pass,
 * cancelling any earlier schedule. This keeps rotation going even when the
 * rotation was triggered outside the scheduled timer (e.g. by the wake handler).
 */
add_task(async function test_successful_rotation_reschedules_next() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });

  await startActiveProxy();

  // The pass returned by the manual rotation is due for rotation almost
  // immediately, so the reschedule it triggers fires shortly after.
  const now = Temporal.Now.instant();
  const soonDuePass = new ProxyPass(
    createProxyPassToken(now, now.add({ minutes: 2, seconds: 1 }))
  );
  const longLivedPass = new ProxyPass(createProxyPassToken());

  // Resolves when the rescheduled rotation fires on its own and fetches again.
  const rescheduledRotationRan = Promise.withResolvers();
  const fetchStub = sandbox.stub(IPPDummyAuthProvider, "fetchProxyPass");
  fetchStub.onCall(0).resolves({ status: 200, pass: soonDuePass });
  fetchStub.callsFake(() => {
    rescheduledRotationRan.resolve();
    return Promise.resolve({ status: 200, pass: longLivedPass });
  });

  const rotated = await IPPProxyManager.rotateProxyPass();
  Assert.equal(
    rotated.token,
    soonDuePass.token,
    "Manual rotation returned the soon-due pass"
  );

  // If the rotation had not rescheduled, this would never resolve.
  await rescheduledRotationRan.promise;

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "Proxy stays ACTIVE after the rescheduled rotation"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * A pass that is always fetched already due for rotation must not spin the
 * scheduled rotation into a tight loop while awake: the reschedule delay is
 * floored by browser.ipProtection.guardian.minRotationInterval.
 */
add_task(async function test_scheduled_rotation_does_not_livelock() {
  const MIN_ROTATION_INTERVAL_PREF =
    "browser.ipProtection.guardian.minRotationInterval";
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  Services.prefs.setIntPref(MIN_ROTATION_INTERVAL_PREF, 100);

  // Every fetch returns a pass that is already past its rotation point, so each
  // scheduled rotation immediately schedules another one.
  const now = Temporal.Now.instant();
  const alwaysDue = new ProxyPass(
    createProxyPassToken(now.subtract({ minutes: 2 }), now.add({ seconds: 1 }))
  );
  const fetchSpy = sandbox.spy(IPPDummyAuthProvider, "fetchProxyPass");
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: alwaysDue,
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "3026-02-01T00:00:00.000Z"
    ),
  });

  await startActiveProxy();

  const fetchesAfterStart = fetchSpy.callCount;

  // Stay awake and let scheduled rotations run for several floor intervals.
  await new Promise(resolve => do_timeout(500, resolve));

  const scheduledRotations = fetchSpy.callCount - fetchesAfterStart;
  Assert.lessOrEqual(
    scheduledRotations,
    20,
    "Scheduled rotations are bounded by the minimum interval, not a busy loop"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
  Services.prefs.clearUserPref(MIN_ROTATION_INTERVAL_PREF);
});

function channelFilterProxyInfoPresent() {
  return !!IPPProxyManager.channelFilter()?.proxyInfo;
}
