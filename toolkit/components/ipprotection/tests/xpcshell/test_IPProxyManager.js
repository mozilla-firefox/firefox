/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { scheduleCallback } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
);
const { IPPStartupCache } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPStartupCache.sys.mjs"
);
const { IPProtectionServerlist } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs"
);
const { IPPChannelFilter } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs"
);

const TIMEOUT_PREF = "browser.ipProtection.guardian.timeout";
const RETRY_AFTER_PREF = "browser.ipProtection.guardian.retryAfter";
const ATTEMPT_TIMEOUT_PREF = "browser.ipProtection.guardian.attemptTimeout";
const MODE_PREF = "browser.ipProtection.mode";

/**
 * Returns a promise that never resolves and rejects with the signal's reason
 * once it aborts, modelling a fetch that hangs until it is cancelled.
 *
 * @param {AbortSignal} signal
 */
function hangUntilAbort(signal) {
  return new Promise((_resolve, reject) => {
    if (signal?.aborted) {
      reject(signal.reason);
      return;
    }
    signal?.addEventListener("abort", () => reject(signal.reason), {
      once: true,
    });
  });
}

add_setup(async function () {
  await putServerInRemoteSettings();
});

/**
 * Tests that starting the service gets a state changed event.
 */
add_task(async function test_IPPProxyManager_start() {
  setupStubs();

  let readyEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();

  await readyEventPromise;

  Assert.ok(
    !IPPProxyManager.activatedAt,
    "IP Protection service should not be active initially"
  );

  let startedEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );

  IPPProxyManager.start();

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVATING,
    "Proxy activation"
  );
  await startedEventPromise;

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "IP Protection service should be active after starting"
  );
  Assert.ok(
    !!IPPProxyManager.activatedAt,
    "IP Protection service should have an activation timestamp"
  );
  Assert.ok(
    IPPProxyManager.active,
    "IP Protection service should have an active connection"
  );

  Assert.notEqual(
    IPPProxyManager.usageInfo,
    null,
    "IP Protection service should have usage info after starting"
  );
  Assert.ok(
    IPPProxyManager.usageInfo instanceof ProxyUsage,
    "usageInfo should be an instance of ProxyUsage"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that stopping the service gets stop events.
 */
add_task(async function test_IPPProxyManager_stop() {
  setupStubs();

  const waitForReady = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();
  await waitForReady;

  await IPPProxyManager.start();

  let stoppedEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.READY
  );
  await IPPProxyManager.stop();

  await stoppedEventPromise;
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IP Protection service should not be active after stopping"
  );
  Assert.ok(
    !IPPProxyManager.activatedAt,
    "IP Protection service should not have an activation timestamp after stopping"
  );
  Assert.ok(
    !IPProtectionService.connection,
    "IP Protection service should not have an active connection"
  );
  Assert.notEqual(
    IPPProxyManager.usageInfo,
    null,
    "IP Protection service should still have usage info after stopping"
  );
  Assert.ok(
    IPPProxyManager.usageInfo instanceof ProxyUsage,
    "usageInfo should be an instance of ProxyUsage"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that the proxy manager gets proxy pass and connection on starting
 * and removes the connection after after stop.
 */
add_task(async function test_IPPProxyManager_start_stop_reset() {
  setupStubs();

  let readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();
  await readyEvent;

  await IPPProxyManager.start();

  Assert.ok(IPPProxyManager.active, "Should be active after starting");

  Assert.ok(
    IPPProxyManager.isolationKey,
    "Should have an isolationKey after starting"
  );

  Assert.ok(
    IPPProxyManager.hasValidProxyPass,
    "Should have a valid proxy pass after starting"
  );

  await IPPProxyManager.stop();

  Assert.ok(!IPPProxyManager.active, "Should not be active after starting");

  Assert.ok(
    !IPPProxyManager.isolationKey,
    "Should not have an isolationKey after stopping"
  );
});

/**
 * Tests that the proxy manager gets proxy pass and connection on starting
 * and removes them after stop / reset.
 */
add_task(async function test_IPPProxyManager_reset() {
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "3026-02-01T00:00:00.000Z"
    ),
  });

  await IPPProxyManager.start();

  Assert.ok(IPPProxyManager.active, "Should be active after starting");

  Assert.ok(
    IPPProxyManager.isolationKey,
    "Should have an isolationKey after starting"
  );

  Assert.ok(
    IPPProxyManager.hasValidProxyPass,
    "Should have a valid proxy pass after starting"
  );

  await IPPProxyManager.reset();

  Assert.ok(!IPPProxyManager.active, "Should not be active after reset");

  Assert.ok(
    !IPPProxyManager.isolationKey,
    "Should not have an isolationKey after reset"
  );

  Assert.ok(
    !IPPProxyManager.hasValidProxyPass,
    "Should not have a proxy pass after reset"
  );
  IPProtectionService.uninit();
});

/**
 * Tests that usage info is refreshed after reset, so stale data from a
 * previous account doesn't carry over when using the same profile.
 */
add_task(async function test_IPPProxyManager_reset_clears_usage() {
  const oldUsage = new ProxyUsage(
    "5368709120",
    "4294967296",
    "3026-02-01T00:00:00.000Z"
  );
  const newUsage = new ProxyUsage(
    "1073741824",
    "1073741824",
    "3026-03-01T00:00:00.000Z"
  );
  setupStubs({ proxyUsage: oldUsage });

  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.init();
  await readyEvent;

  await IPPProxyManager.start();

  Assert.equal(
    IPPProxyManager.usageInfo,
    oldUsage,
    "Should have old account's usage info after starting"
  );

  // Simulate an account switch: the next fetchProxyUsage call returns new
  // data. UsageChanged fires after the refresh completes, so we use it to
  // know when the refresh has been applied.
  let usageRefreshed = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:UsageChanged"
  );
  IPPDummyAuthProvider.setProxyUsage(newUsage);

  await IPPProxyManager.reset();
  await usageRefreshed;

  Assert.equal(
    IPPProxyManager.usageInfo,
    newUsage,
    "Usage info should be refreshed after reset, not carry over old account's data"
  );

  IPProtectionService.uninit();
});

/**
 * Tests the error state.
 * - When the proxy is active, the ERROR state is set on errors.
 * - Stopping the proxy clears the ERROR state and returns the proxy to READY.
 */
add_task(async function test_IPPProxyStates_error() {
  setupStubs({ validProxyPass: true });

  const readyPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.init();
  await readyPromise;

  const activeEvent = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );
  IPPProxyManager.start();
  await activeEvent;

  IPPDummyAuthProvider.setProxyPass({
    status: 403,
    error: undefined,
    pass: undefined,
    usage: undefined,
  });

  const errorPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ERROR
  );
  await IPPProxyManager.rotateProxyPass();
  await errorPromise;
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "Proxy should be in ERROR state after rotation failure while ACTIVE"
  );

  const resetPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.READY
  );
  await IPPProxyManager.stop();
  await resetPromise;

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "Proxy should return to READY state after stop() from ERROR"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that a non-string error reaching the error state is normalized to a
 * string errorType, so it survives serialization to GeckoView (where it is read
 * back with GeckoBundle.getString and would otherwise throw ClassCastException).
 */
add_task(async function test_IPPProxyManager_non_string_error_normalized() {
  setupStubs({ validProxyPass: true });

  const readyPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );
  IPProtectionService.init();
  await readyPromise;

  const activeEvent = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );
  IPPProxyManager.start();
  await activeEvent;

  // Simulate a provider surfacing a non-string error
  IPPDummyAuthProvider.setProxyPass({
    status: 403,
    error: new Error("boom"),
    pass: undefined,
    usage: undefined,
  });

  const errorPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ERROR
  );
  await IPPProxyManager.rotateProxyPass();
  await errorPromise;

  Assert.equal(
    typeof IPPProxyManager.errorType,
    "string",
    "errorType must be a string so it serializes to GeckoView"
  );
  Assert.equal(
    IPPProxyManager.errorType,
    ERRORS.GENERIC,
    "A non-string error is normalized to ERRORS.GENERIC"
  );

  const resetPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.READY
  );
  await IPPProxyManager.stop();
  await resetPromise;

  IPProtectionService.uninit();
});

/**
 * Tests the source reported with an error state. FOG is reset between the
 * cases because each one asserts on the single event it just recorded.
 */
add_task(async function test_IPPProxyManager_error_source() {
  const source = () =>
    Glean.ipprotection.error.testGetValue()?.at(-1).extra.source;

  Services.fog.testResetFOG();
  const error = new Error("boom");
  error.stack = [
    "activate@moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs:1:2",
    "onClick@https://example.com/page.js:3:4",
    "run@resource://gre/modules/Foo.sys.mjs:5:6",
    "start@chrome://browser/content/ipprotection/ipprotection-content.mjs:7:8",
  ].join("\n");
  IPPProxyManager.setErrorState(error);
  Assert.equal(
    source(),
    [
      "activate@IPPProxyManager.sys.mjs:1:2",
      "run@Foo.sys.mjs:5:6",
      "start@ipprotection-content.mjs:7:8",
    ].join("\n"),
    "A thrown error reports its privileged frames, without paths"
  );

  Services.fog.testResetFOG();
  IPPProxyManager.setErrorState({ code: 500 });
  Assert.stringContains(
    source() ?? "",
    "@IPPProxyManager.sys.mjs:",
    "An error that was never thrown reports where we handled it"
  );

  Services.fog.testResetFOG();
  IPPProxyManager.setErrorState(ERRORS.SERVER_NOT_FOUND);
  Assert.equal(
    source(),
    "ProxyManager",
    "One of our error codes reports the module"
  );

  Services.fog.testResetFOG();
});

/**
 * Tests that a non-string provider error does not reach the activation result,
 * where it would not survive serialization to GeckoView.
 */
add_task(async function test_IPPProxyManager_non_string_error_on_activation() {
  setupStubs();
  IPPDummyAuthProvider.setProxyPass({
    status: 403,
    error: new Error("boom"),
    pass: undefined,
    usage: undefined,
  });

  await IPProtectionService.init();
  const result = await IPPProxyManager.start(false);

  Assert.equal(
    result.error,
    ERRORS.PASS_UNAVAILABLE,
    "A non-string provider error is reported as ERRORS.PASS_UNAVAILABLE"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that a 500 from Guardian during activation surfaces as CATASTROPHIC.
 */
add_task(async function test_IPPProxyManager_catastrophic_on_500() {
  setupStubs();
  IPPDummyAuthProvider.setProxyPass({
    status: 500,
    error: AUTH_ERRORS.SERVER_ERROR,
    pass: undefined,
    usage: undefined,
  });

  await IPProtectionService.init();
  const result = await IPPProxyManager.start(false);

  Assert.equal(
    result.error,
    ERRORS.CATASTROPHIC,
    "Status 500 from Guardian should surface CATASTROPHIC"
  );

  IPProtectionService.uninit();
});

/**
 * Starts the service with a proxy pass request that fails.
 */
async function initServiceWithFailingPass() {
  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });
  IPPDummyAuthProvider.setProxyPass({
    status: 500,
    error: "test_error",
    usage: null,
  });

  await IPProtectionService.init();

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be ready"
  );
}

/**
 * Tests that a user activation that fails returns to the previous state.
 */
add_task(async function test_IPPProxyManager_user_activation_failure() {
  await initServiceWithFailingPass();

  await IPPProxyManager.start(true);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "Proxy should return to READY state after activation failure"
  );
  Assert.ok(
    !IPPProxyManager.channelFilter(),
    "The channel filter should be gone"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that a system activation that fails stays in ERROR, holding traffic,
 * until it is stopped.
 */
add_task(async function test_IPPProxyManager_system_activation_failure() {
  await initServiceWithFailingPass();

  await IPPProxyManager.start(false);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "Proxy should be in ERROR after a system activation failure"
  );
  Assert.ok(
    IPPProxyManager.channelFilter()?.active,
    "The channel filter should still hold traffic"
  );

  IPPProxyManager.updateState();
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "Recomputing the state should not leave ERROR"
  );

  await IPPProxyManager.stop(false);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "Stopping should leave ERROR"
  );
  Assert.ok(
    !IPPProxyManager.channelFilter(),
    "Stopping should remove the channel filter"
  );

  IPProtectionService.uninit();
});

/**
 * Tests that a user start retries out of ERROR, going straight to ACTIVATING
 * without announcing READY in between.
 */
add_task(async function test_IPPProxyManager_user_start_retries_error() {
  await initServiceWithFailingPass();
  await IPPProxyManager.start(false, false, undefined, IPPMode.MODE_INCLUSION);
  Assert.equal(IPPProxyManager.state, IPPProxyStates.ERROR);

  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
  });
  const states = [];
  const record = () => states.push(IPPProxyManager.state);
  IPPProxyManager.addEventListener("IPPProxyManager:StateChanged", record);

  const { started } = await IPPProxyManager.start(true);

  IPPProxyManager.removeEventListener("IPPProxyManager:StateChanged", record);
  Assert.ok(started, "The user start should succeed");
  Assert.deepEqual(
    states,
    [IPPProxyStates.ACTIVATING, IPPProxyStates.ACTIVE],
    "The retry should not pass through READY"
  );
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.getDefault(),
    "The retry should use the default mode"
  );

  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
});

/**
 * Tests that a system start does not retry out of ERROR.
 */
add_task(async function test_IPPProxyManager_system_start_keeps_error() {
  await initServiceWithFailingPass();
  await IPPProxyManager.start(false);

  const { started, error } = await IPPProxyManager.start(false);

  Assert.ok(!started, "The system start should be refused");
  Assert.ok(error, "The refusal should carry the error");
  Assert.equal(IPPProxyManager.state, IPPProxyStates.ERROR);

  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
});

/**
 * Tests that a user retry out of ERROR that fails stays in ERROR, holding
 * traffic, rather than releasing it.
 */
add_task(async function test_IPPProxyManager_failed_retry_keeps_error() {
  await initServiceWithFailingPass();
  await IPPProxyManager.start(false);
  Assert.equal(IPPProxyManager.state, IPPProxyStates.ERROR);

  const { started } = await IPPProxyManager.start(true);

  Assert.ok(!started, "The retry should fail");
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "A failed retry should stay in ERROR"
  );
  const later = makeDocumentChannel("https://example.com/");
  applyFilter(IPPProxyManager.channelFilter(), later);
  Assert.equal(
    later.cancelStatus,
    Cr.NS_ERROR_PROXY_CONNECTION_REFUSED,
    "A new load should still be cancelled with a proxy error"
  );

  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
});

/**
 * Tests that a load held by a user activation that fails is cancelled with a
 * proxy error rather than aborted.
 */
add_task(
  async function test_IPPProxyManager_user_activation_failure_fails_held_load() {
    await initServiceWithFailingPass();

    const started = IPPProxyManager.start(true);
    const channel = makeDocumentChannel("https://example.com/");
    applyFilter(IPPProxyManager.channelFilter(), channel);
    await started;

    Assert.equal(
      channel.cancelStatus,
      Cr.NS_ERROR_PROXY_CONNECTION_REFUSED,
      "The held load should be cancelled with a proxy error"
    );

    IPProtectionService.uninit();
  }
);

/**
 * Tests that in ERROR, new loads are cancelled with a proxy error.
 */
add_task(async function test_IPPProxyManager_error_fails_proxied_loads() {
  await initServiceWithFailingPass();
  await IPPProxyManager.start(false);

  const later = makeDocumentChannel("https://example.com/");
  Assert.strictEqual(
    applyFilter(IPPProxyManager.channelFilter(), later),
    null,
    "A new load should not get a connection"
  );
  Assert.equal(
    later.cancelStatus,
    Cr.NS_ERROR_PROXY_CONNECTION_REFUSED,
    "A new load should be cancelled with a proxy error"
  );

  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
});

/**
 * Tests that usage data is preserved when quota is exceeded.
 */
add_task(async function test_IPPProxyManager_quota_exceeded() {
  Services.fog.testResetFOG();

  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });
  await putServerInRemoteSettings();

  IPPDummyAuthProvider.setProxyPass({
    status: 429,
    error: "quota_exceeded",
    pass: undefined,
    usage: new ProxyUsage("5368709120", "0", "3026-02-02T00:00:00.000Z"),
  });

  // Initialize service and wait for READY state
  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();
  await readyEvent;

  // Setup event listener to capture usage change
  let usageChanged = false;
  let capturedUsage = null;
  const usageListener = event => {
    usageChanged = true;
    capturedUsage = event.detail.usage;
  };
  IPPProxyManager.addEventListener(
    "IPPProxyManager:UsageChanged",
    usageListener
  );

  // Try to start - should pause due to quota exhaustion
  const pausedEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.PAUSED
  );

  IPPProxyManager.start();
  await pausedEventPromise;

  // Verify usage was set before pausing
  Assert.ok(usageChanged, "UsageChanged event should have fired");
  Assert.notEqual(capturedUsage, null, "Usage should be captured");
  Assert.equal(
    capturedUsage.remaining,
    BigInt("0"),
    "Usage remaining should be 0"
  );
  Assert.equal(
    capturedUsage.max,
    BigInt("5368709120"),
    "Usage max should be set"
  );

  // Verify the proxy is in PAUSED state because quota is exhausted
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "Should be in PAUSED state"
  );

  let pausedEvent = Glean.ipprotection.paused.testGetValue();
  Assert.equal(pausedEvent.length, 1, "Should have recorded the paused event");
  Assert.equal(
    pausedEvent[0].extra.wasActive,
    "false",
    "Previous state was not active"
  );

  // Verify usage is still accessible in manager
  Assert.notEqual(IPPProxyManager.usageInfo, null, "Usage should be stored");
  Assert.equal(
    IPPProxyManager.usageInfo.remaining,
    BigInt("0"),
    "Stored usage remaining should be 0"
  );
  Assert.equal(
    IPPProxyManager.usageInfo.max,
    BigInt("5368709120"),
    "Stored usage max should be set"
  );

  // Cleanup
  IPPProxyManager.removeEventListener(
    "IPPProxyManager:UsageChanged",
    usageListener
  );
  IPProtectionService.uninit();
  Services.fog.testResetFOG();
});

/**
 * Tests that an unlimited usage from a pass fetch is recorded and dispatched,
 * and that the proxy activates instead of pausing despite a null remaining.
 */
add_task(async function test_IPPProxyManager_unlimited_usage() {
  setupStubs({
    proxyUsage: new ProxyUsage(null, null, null, true),
  });
  Services.prefs.clearUserPref("browser.ipProtection.usageCache");

  let capturedUsage = null;
  const usageListener = event => {
    capturedUsage = event.detail.usage;
  };
  IPPProxyManager.addEventListener(
    "IPPProxyManager:UsageChanged",
    usageListener
  );

  const waitForReady = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();
  await waitForReady;

  await IPPProxyManager.start(false);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "Proxy should activate for unlimited usage instead of pausing"
  );
  Assert.ok(
    IPPProxyManager.usageInfo?.unlimited,
    "Manager should record the unlimited usage from the pass fetch"
  );
  Assert.notEqual(
    capturedUsage,
    null,
    "UsageChanged event should fire for unlimited usage"
  );
  Assert.ok(capturedUsage.unlimited, "Dispatched usage should be unlimited");

  IPPProxyManager.removeEventListener(
    "IPPProxyManager:UsageChanged",
    usageListener
  );
  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.usageCache");
});

/**
 * Tests the active state.
 */
add_task(async function test_IPPProxytates_active() {
  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "3026-02-01T00:00:00.000Z"
    ),
  });

  const waitForReady = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();

  await waitForReady;

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be ready"
  );

  const startPromise = IPPProxyManager.start(false);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVATING,
    "Proxy activation"
  );

  await startPromise;

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be in ready state"
  );

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "IP Protection service should be active"
  );

  await IPPProxyManager.stop(false);

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be ready again"
  );

  IPProtectionService.uninit();
});

/**
 * Tests the quick start/stop calls.
 */
add_task(async function test_IPPProxytates_start_stop() {
  IPPDummyAuthProvider.simulateSignIn(true);
  IPPDummyAuthProvider.setGetEntitlementResponse({
    entitlement: createTestEntitlement(),
  });
  IPPDummyAuthProvider.setProxyPass({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
    usage: new ProxyUsage(
      "5368709120",
      "123456789",
      "3026-02-01T00:00:00.000Z"
    ),
  });

  const waitForReady = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();

  await waitForReady;

  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "IP Protection service should be ready"
  );

  IPPProxyManager.start(false);
  IPPProxyManager.start(false);
  IPPProxyManager.start(false);

  IPPProxyManager.stop(false);
  IPPProxyManager.stop(false);
  IPPProxyManager.stop(false);
  IPPProxyManager.stop(false);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVATING,
    "Proxy activation"
  );

  // We should expect the Stop to cancel the activation
  using fail = {
    listener: () => {
      if (IPPProxyManager.state === IPPProxyStates.ACTIVE) {
        Assert.ok(false, "We must abort the activation when calling stop.");
      }
    },
    [Symbol.dispose]: () => {
      IPPProxyManager.removeEventListener(
        "IPPProxyManager:StateChanged",
        fail.listener
      );
    },
  };
  IPPProxyManager.addEventListener(
    "IPPProxyManager:StateChanged",
    fail.listener
  );

  await waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.READY
  );

  IPProtectionService.uninit();
  IPPProxyManager.uninit();
});

add_task(
  async function test_IPPProxyManager_restart_after_pause_during_activation() {
    // setupStubs first so the implicit refreshUsage inside reset() picks up
    // the default (non-zero) usage instead of stale state from a previous
    // task — otherwise the proxy briefly transitions to PAUSED during init
    // and the listener below misses the expected event.
    setupStubs();
    await IPPProxyManager.reset();

    const readyEvent = waitForEvent(
      IPProtectionService,
      "IPProtectionService:StateChanged",
      () => IPProtectionService.state === IPProtectionStates.READY
    );

    IPProtectionService.init();
    await readyEvent;

    // Starting with no bandwidth remaining will pause the proxy during activation.
    const pausedEventPromise = waitForEvent(
      IPPProxyManager,
      "IPPProxyManager:StateChanged",
      () => IPPProxyManager.state === IPPProxyStates.PAUSED
    );

    setupStubs({
      validProxyPass: false,
      proxyUsage: new ProxyUsage("1000000", "0", "3026-02-05T00:00:00.000Z"),
    });

    IPPProxyManager.start();
    await pausedEventPromise;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.PAUSED,
      "Should be PAUSED after activation with no bandwidth remaining"
    );

    // Refresh usage with available bandwidth to unpause the proxy.
    setupStubs({
      validProxyPass: true,
      proxyUsage: new ProxyUsage(
        "1000000",
        "500000",
        "3026-02-06T00:00:00.000Z"
      ),
    });

    const readyAfterRefresh = waitForEvent(
      IPPProxyManager,
      "IPPProxyManager:StateChanged",
      () => IPPProxyManager.state === IPPProxyStates.READY
    );

    await IPPProxyManager.refreshUsage();
    await readyAfterRefresh;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.READY,
      "Should be READY after refreshing usage with available bandwidth"
    );

    // Start again, should activate successfully
    const activeEventPromise = waitForEvent(
      IPPProxyManager,
      "IPPProxyManager:StateChanged",
      () => IPPProxyManager.state === IPPProxyStates.ACTIVE
    );

    await IPPProxyManager.start();
    await activeEventPromise;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVE,
      "Should be ACTIVE after restarting with available bandwidth"
    );
    Assert.ok(IPPProxyManager.active, "Should have an active connection");

    IPProtectionService.uninit();
  }
);

add_task(
  async function test_IPPProxyManager_paused_on_rotation_with_zero_quota() {
    Services.fog.testResetFOG();
    IPPProxyManager.uninit();
    setupStubs({
      validProxyPass: true,
      proxyUsage: new ProxyUsage(
        "1000000",
        "500000",
        "3026-02-05T00:00:00.000Z"
      ),
    });

    const readyEvent = waitForEvent(
      IPProtectionService,
      "IPProtectionService:StateChanged",
      () => IPProtectionService.state === IPProtectionStates.READY
    );

    IPProtectionService.init();
    await readyEvent;

    const activeEventPromise = waitForEvent(
      IPPProxyManager,
      "IPPProxyManager:StateChanged",
      () => IPPProxyManager.state === IPPProxyStates.ACTIVE
    );

    IPPProxyManager.start();
    await activeEventPromise;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVE,
      "Proxy should be active after initial start"
    );

    // Re-configure the dummy to return a zero quota and simulate quota
    // exhaustion on rotation.
    setupStubs({
      validProxyPass: false,
      proxyUsage: new ProxyUsage("1000000", "0", "3026-02-05T00:00:00.000Z"),
    });

    const pausedEventPromise = waitForEvent(
      IPPProxyManager,
      "IPPProxyManager:StateChanged",
      () => IPPProxyManager.state === IPPProxyStates.PAUSED
    );
    IPPProxyManager.rotateProxyPass();
    await pausedEventPromise;

    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.PAUSED,
      "Proxy should be in PAUSED state when quota exhausted during rotation"
    );
    Assert.equal(
      IPPProxyManager.isolationKey,
      null,
      "Should not have an isolationKey when paused, as the connection is paused"
    );

    let pausedEvent = Glean.ipprotection.paused.testGetValue();
    Assert.equal(
      pausedEvent.length,
      1,
      "Should have recorded the paused event"
    );
    Assert.equal(
      pausedEvent[0].extra.wasActive,
      "true",
      "Previous state was active"
    );

    await IPPProxyManager.stop();
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.PAUSED,
      "Proxy should remain in the PAUSED state when stopping from PAUSED state"
    );

    IPProtectionService.uninit();
    Services.fog.testResetFOG();
  }
);

add_task(async function test_IPPProxyManager_rotateProxyPass_changes_pass() {
  setupStubs({ validProxyPass: true });

  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();
  await readyEvent;

  const activeEventPromise = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    () => IPPProxyManager.state === IPPProxyStates.ACTIVE
  );

  IPPProxyManager.start();
  await activeEventPromise;

  setupStubs({ validProxyPass: false });

  const firstPass = await IPPProxyManager.rotateProxyPass();
  Assert.ok(firstPass, "First rotation should return a pass");
  Assert.ok(!firstPass.isValid(), "First pass should be invalid/expired");

  setupStubs({ validProxyPass: true });

  const secondPass = await IPPProxyManager.rotateProxyPass();
  Assert.ok(secondPass, "Second rotation should return a pass");
  Assert.ok(secondPass.isValid(), "Second pass should be valid");

  Assert.notEqual(
    firstPass.token,
    secondPass.token,
    "Pass tokens should be different after rotation"
  );
  Assert.ok(
    !firstPass.isValid() && secondPass.isValid(),
    "Pass validity should change from invalid to valid"
  );

  IPProtectionService.uninit();
});

/**
 * fromResponse anchors a proxy pass to the local clock at receipt: the pass is
 * valid from now for the token's lifetime (exp - nbf), regardless of how far
 * Guardian's clock has drifted from ours. This preserves rotation scheduling
 * even for a token that looks long-expired to a skewed client, and is what
 * prevents the 0ms rotation-reschedule loop.
 */
add_task(async function test_ProxyPass_fromResponse_reanchors_to_now() {
  const now = Temporal.Now.instant();
  const cases = [
    {
      desc: "large drift (client clock ~2h ahead of a 1h token)",
      from: now.subtract({ hours: 2 }),
      until: now.subtract({ hours: 1 }),
    },
    {
      desc: "accurate clock (freshly issued 24h token)",
      from: now.subtract({ seconds: 30 }),
      until: now.add({ hours: 24 }),
    },
  ];

  for (const { desc, from, until } of cases) {
    const token = createProxyPassToken(from, until);
    const response = { ok: true, json: async () => ({ token }) };

    const pass = await ProxyPass.fromResponse(response);
    Assert.ok(pass, `${desc}: fromResponse returns a pass`);

    const lifetimeMs = until.epochMilliseconds - from.epochMilliseconds;
    const expectedFromMs = Temporal.Now.instant().epochMilliseconds;

    Assert.less(
      Math.abs(pass.from.epochMilliseconds - expectedFromMs),
      1000,
      `${desc}: from is re-anchored to now`
    );
    Assert.less(
      Math.abs(pass.until.epochMilliseconds - (expectedFromMs + lifetimeMs)),
      1000,
      `${desc}: the token lifetime is preserved`
    );
    Assert.ok(pass.isValid(), `${desc}: pass is valid`);
    Assert.ok(
      !pass.shouldRotate(),
      `${desc}: a just-received pass does not want immediate rotation`
    );
  }
});

add_task(async function test_IPPProxyManager_stop_during_rotation() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });

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

  let resolveFetch;
  IPPDummyAuthProvider.setProxyPass(
    new Promise(resolve => {
      resolveFetch = resolve;
    })
  );

  const resumeSpy = sandbox.spy(
    IPPChannelFilter.prototype,
    "replaceAuthTokenAndResume"
  );

  const rotationPromise = IPPProxyManager.rotateProxyPass();

  await IPPProxyManager.stop();

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "State should be READY after stop while a rotation is in flight"
  );

  resolveFetch({
    status: 200,
    error: undefined,
    pass: new ProxyPass(createProxyPassToken()),
    usage: new ProxyUsage(
      "5368709120",
      "4294967296",
      "3026-02-01T00:00:00.000Z"
    ),
  });

  await rotationPromise;

  Assert.ok(
    !resumeSpy.called,
    "replaceAuthTokenAndResume should not be called when rotation resolves after stop"
  );
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "State should remain READY after the late rotation result is discarded"
  );
  Assert.ok(
    !IPPProxyManager.active,
    "Connection should remain inactive after the late rotation result is discarded"
  );

  IPProtectionService.uninit();
  sandbox.restore();
});

add_task(
  async function test_IPPProxyManager_rotation_retries_transient_error() {
    let sandbox = sinon.createSandbox();
    setupStubs({ validProxyPass: true });
    try {
      await initServiceToReady();
      await IPPProxyManager.start();

      Services.prefs.setIntPref(RETRY_AFTER_PREF, 1);
      Services.prefs.setIntPref(TIMEOUT_PREF, 10000);

      const validPass = new ProxyPass(createProxyPassToken());
      const transient = { status: 503 };
      const fetchStub = sandbox.stub(IPPDummyAuthProvider, "fetchProxyPass");
      fetchStub.onCall(0).resolves(transient);
      fetchStub.onCall(1).resolves(transient);
      fetchStub.resolves({ status: 200, pass: validPass });

      const newPass = await IPPProxyManager.rotateProxyPass();

      Assert.ok(newPass, "Rotation should return a pass once the 5xx clears");
      Assert.equal(
        newPass.token,
        validPass.token,
        "Rotation should return the pass fetched after the retries"
      );
      Assert.greaterOrEqual(
        fetchStub.callCount,
        3,
        "A 5xx response should be retried until it succeeds"
      );
      Assert.equal(
        IPPProxyManager.state,
        IPPProxyStates.ACTIVE,
        "Proxy should stay ACTIVE after a successful retried rotation"
      );
    } finally {
      sandbox.restore();
      await IPPProxyManager.stop(false).catch(() => {});
      IPProtectionService.uninit();
      Services.prefs.clearUserPref(RETRY_AFTER_PREF);
      Services.prefs.clearUserPref(TIMEOUT_PREF);
    }
  }
);

/**
 * Wake recovery retries through network errors and recovers to ACTIVE once
 * the request reaches Guardian. retryAfter is 0 so the retry loop is driven by
 * promise resolution rather than wall-clock timers.
 */
add_task(async function test_wake_rotation_retries_network_errors() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  try {
    await initServiceToReady();
    await IPPProxyManager.start();

    Services.prefs.setIntPref(RETRY_AFTER_PREF, 0);

    const validPass = new ProxyPass(createProxyPassToken());
    const fetchStub = sandbox.stub(IPPDummyAuthProvider, "fetchProxyPass");
    const networkError = { error: AUTH_ERRORS.NETWORK_ERROR, usage: null };
    fetchStub.onCall(0).resolves(networkError);
    fetchStub.onCall(1).resolves(networkError);
    fetchStub.resolves({ status: 200, pass: validPass });

    const newPass = await IPPProxyManager.rotateProxyPass({
      abortOnNetworkError: false,
    });

    Assert.equal(
      newPass?.token,
      validPass.token,
      "Recovers with the pass fetched after retrying past the failures"
    );
    Assert.equal(IPPProxyManager.state, IPPProxyStates.ACTIVE, "Stays ACTIVE");
  } finally {
    sandbox.restore();
    await IPPProxyManager.stop(false).catch(() => {});
    IPProtectionService.uninit();
    Services.prefs.clearUserPref(RETRY_AFTER_PREF);
  }
});

/**
 * In-session (default) rotation treats a network error as a real disconnect:
 * it fails fast with a network error instead of retrying.
 */
add_task(async function test_insession_rotation_network_error_is_fatal() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  try {
    await initServiceToReady();
    await IPPProxyManager.start();

    const fetchSpy = sandbox
      .stub(IPPDummyAuthProvider, "fetchProxyPass")
      .resolves({ error: AUTH_ERRORS.NETWORK_ERROR, usage: null });

    const errorEvent = waitForProxyState(IPPProxyStates.ERROR);
    await IPPProxyManager.rotateProxyPass();
    await errorEvent;

    Assert.equal(fetchSpy.callCount, 1, "Fatal in-session, not retried");
    Assert.equal(
      IPPProxyManager.errorType,
      ERRORS.NETWORK,
      "Surfaces a network error"
    );
  } finally {
    sandbox.restore();
    await IPPProxyManager.stop(false).catch(() => {});
    IPProtectionService.uninit();
  }
});

/**
 * A thrown provider error is an auth failure, not a network error, so wake
 * recovery fails fast with it instead of retrying.
 */
add_task(async function test_wake_rotation_thrown_error_is_fatal() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  try {
    await initServiceToReady();
    await IPPProxyManager.start();

    const fetchStub = sandbox
      .stub(IPPDummyAuthProvider, "fetchProxyPass")
      .callsFake(() => Promise.reject(AUTH_ERRORS.LOGIN_NEEDED));

    const errorEvent = waitForProxyState(IPPProxyStates.ERROR);
    await IPPProxyManager.rotateProxyPass({ abortOnNetworkError: false });
    await errorEvent;

    Assert.equal(fetchStub.callCount, 1, "Fatal on wake, not retried");
    Assert.equal(
      IPPProxyManager.errorType,
      AUTH_ERRORS.LOGIN_NEEDED,
      "Surfaces the thrown error"
    );
  } finally {
    sandbox.restore();
    await IPPProxyManager.stop(false).catch(() => {});
    IPProtectionService.uninit();
  }
});

/**
 * A single fetch that hangs (flaky transport) is bounded by the per-attempt
 * timeout and retried, rather than consuming the whole rotation budget. Once the
 * transport recovers the rotation succeeds.
 */
add_task(async function test_rotation_recovers_from_slow_fetch() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  try {
    await initServiceToReady();
    await IPPProxyManager.start();

    Services.prefs.setIntPref(ATTEMPT_TIMEOUT_PREF, 20);
    Services.prefs.setIntPref(RETRY_AFTER_PREF, 1);
    Services.prefs.setIntPref(TIMEOUT_PREF, 10000);

    const validPass = new ProxyPass(createProxyPassToken());
    const fetchStub = sandbox.stub(IPPDummyAuthProvider, "fetchProxyPass");
    fetchStub.onCall(0).callsFake(signal => hangUntilAbort(signal));
    fetchStub.onCall(1).callsFake(signal => hangUntilAbort(signal));
    fetchStub.resolves({ status: 200, pass: validPass });

    const newPass = await IPPProxyManager.rotateProxyPass();

    Assert.ok(newPass, "Rotation should succeed once the transport recovers");
    Assert.equal(
      newPass.token,
      validPass.token,
      "Rotation should return the pass fetched after the slow attempts"
    );
    Assert.greaterOrEqual(
      fetchStub.callCount,
      3,
      "A hanging fetch should be aborted per-attempt and retried"
    );
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVE,
      "Proxy should stay ACTIVE after recovering from slow fetches"
    );
  } finally {
    sandbox.restore();
    await IPPProxyManager.stop(false).catch(() => {});
    IPProtectionService.uninit();
    Services.prefs.clearUserPref(ATTEMPT_TIMEOUT_PREF);
    Services.prefs.clearUserPref(RETRY_AFTER_PREF);
    Services.prefs.clearUserPref(TIMEOUT_PREF);
  }
});

/**
 * A fetch that hangs indefinitely is retried multiple times within the overall
 * budget thanks to the per-attempt timeout, then moves to ERROR once the budget
 * is exhausted. Without the per-attempt timeout a single hang would consume the
 * whole budget in one attempt.
 */
add_task(async function test_rotation_retries_slow_fetch_until_budget() {
  let sandbox = sinon.createSandbox();
  setupStubs({ validProxyPass: true });
  try {
    await initServiceToReady();
    await IPPProxyManager.start();

    Services.prefs.setIntPref(ATTEMPT_TIMEOUT_PREF, 20);
    Services.prefs.setIntPref(RETRY_AFTER_PREF, 1);
    Services.prefs.setIntPref(TIMEOUT_PREF, 200);

    const fetchSpy = sandbox.spy(IPPDummyAuthProvider, "fetchProxyPass");
    IPPDummyAuthProvider.setProxyPassHang(true);

    const errorEvent = waitForProxyState(IPPProxyStates.ERROR);
    const result = await IPPProxyManager.rotateProxyPass();
    await errorEvent;

    Assert.equal(
      result,
      null,
      "A perpetually hanging fetch fails the rotation"
    );
    Assert.greater(
      fetchSpy.callCount,
      1,
      "A hanging fetch should be retried across the budget, not consume it once"
    );
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ERROR,
      "Proxy should move to ERROR when the overall budget is exhausted"
    );
  } finally {
    IPPDummyAuthProvider.setProxyPassHang(false);
    sandbox.restore();
    await IPPProxyManager.stop(false).catch(() => {});
    IPProtectionService.uninit();
    Services.prefs.clearUserPref(ATTEMPT_TIMEOUT_PREF);
    Services.prefs.clearUserPref(RETRY_AFTER_PREF);
    Services.prefs.clearUserPref(TIMEOUT_PREF);
  }
});

add_task(async function test_IPPProxyManager_restores_cached_usage() {
  Services.prefs.setBoolPref("browser.ipProtection.cacheDisabled", false);

  const { ProxyUsage } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ipprotection/GuardianTypes.sys.mjs"
  );

  const cachedUsage = new ProxyUsage(
    "5000000000",
    "2500000000",
    "3026-03-01T00:00:00Z"
  );
  IPPStartupCache.storeUsageInfo(cachedUsage);

  const { IPPProxyManager } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
  );
  IPPProxyManager.init();

  const loadedUsage = IPPProxyManager.usageInfo;
  Assert.notEqual(loadedUsage, null, "Manager loaded usage from cache");
  Assert.equal(
    loadedUsage.max.toString(),
    cachedUsage.max.toString(),
    "Cached max loaded correctly"
  );
  Assert.equal(
    loadedUsage.remaining.toString(),
    cachedUsage.remaining.toString(),
    "Cached remaining loaded correctly"
  );
  Assert.equal(
    loadedUsage.reset.toString(),
    cachedUsage.reset.toString(),
    "Cached reset loaded correctly"
  );

  Services.prefs.setBoolPref("browser.ipProtection.cacheDisabled", true);
  Services.prefs.clearUserPref("browser.ipProtection.usageCache");
  Services.prefs.clearUserPref("browser.ipProtection.stateCache");
  IPPProxyManager.uninit();
});

const refreshUsageTestCases = [
  {
    name: "paused -> paused",
    initialState: IPPProxyStates.PAUSED,
    initialUsage: new ProxyUsage("1000000", "0", "3026-02-05T00:00:00.000Z"),
    refreshedUsage: new ProxyUsage("1000000", "0", "3026-02-06T00:00:00.000Z"),
    expectedState: IPPProxyStates.PAUSED,
    expectedRemaining: BigInt("0"),
  },
  {
    name: "paused -> ready",
    initialState: IPPProxyStates.PAUSED,
    initialUsage: new ProxyUsage("1000000", "0", "3026-02-05T00:00:00.000Z"),
    refreshedUsage: new ProxyUsage(
      "1000000",
      "500000",
      "3026-02-06T00:00:00.000Z"
    ),
    expectedState: IPPProxyStates.READY,
    expectedRemaining: BigInt("500000"),
  },
  {
    name: "Active -> paused",
    initialState: IPPProxyStates.ACTIVE,
    initialUsage: new ProxyUsage(
      "1000000",
      "500000",
      "3026-02-05T00:00:00.000Z"
    ),
    refreshedUsage: new ProxyUsage("1000000", "0", "3026-02-06T00:00:00.000Z"),
    expectedState: IPPProxyStates.PAUSED,
    expectedRemaining: BigInt("0"),
  },
  {
    name: "Active -> active (connection still active)",
    initialState: IPPProxyStates.ACTIVE,
    initialUsage: new ProxyUsage(
      "1000000",
      "500000",
      "3026-02-05T00:00:00.000Z"
    ),
    refreshedUsage: new ProxyUsage(
      "1000000",
      "400000",
      "3026-02-06T00:00:00.000Z"
    ),
    expectedState: IPPProxyStates.ACTIVE,
    expectedRemaining: BigInt("400000"),
  },
];

refreshUsageTestCases.forEach(testCase => {
  add_task(
    async function test_IPPProxyManager_refreshUsage_state_transitions() {
      info(`Running test: ${testCase.name}`);
      IPPStartupCache.storeUsageInfo(testCase.initialUsage);
      setupStubs({
        validProxyPass: testCase.initialState === IPPProxyStates.READY,
        proxyUsage: testCase.initialUsage,
      });

      const readyEvent = waitForEvent(
        IPProtectionService,
        "IPProtectionService:StateChanged",
        () => IPProtectionService.state === IPProtectionStates.READY
      );

      IPProtectionService.init();
      await readyEvent;

      if (testCase.initialState === IPPProxyStates.ACTIVE) {
        const pausedEventPromise = waitForEvent(
          IPPProxyManager,
          "IPPProxyManager:StateChanged",
          () => IPPProxyManager.state === testCase.initialState
        );
        IPPProxyManager.start();
        await pausedEventPromise;
      }

      Assert.equal(
        IPPProxyManager.state,
        testCase.initialState,
        `Initial state should be ${testCase.initialState}`
      );

      setupStubs({
        proxyUsage: testCase.refreshedUsage,
      });

      const stateChangePromise =
        testCase.initialState !== testCase.expectedState
          ? waitForEvent(
              IPPProxyManager,
              "IPPProxyManager:StateChanged",
              () => IPPProxyManager.state === testCase.expectedState
            )
          : Promise.resolve();

      await IPPProxyManager.refreshUsage();
      await stateChangePromise;

      Assert.equal(
        IPPProxyManager.state,
        testCase.expectedState,
        `${testCase.name}: Final state should be ${testCase.expectedState}`
      );
      Assert.equal(
        IPPProxyManager.usageInfo.remaining,
        testCase.expectedRemaining,
        `${testCase.name}: Usage remaining should be ${testCase.expectedRemaining}`
      );

      IPProtectionService.uninit();
    }
  );
});

/**
 * When Firefox launches with a cached usage metric updated in the past,
 * IPPProxyManager should refresh usage info.
 */
add_task(
  async function test_IPPProxyManager_refreshes_stale_startup_cache_on_init() {
    Services.prefs.setBoolPref("browser.ipProtection.cacheDisabled", false);

    const pastReset = Temporal.Now.instant().subtract({ hours: 1 });

    const staleCached = new ProxyUsage(
      "53687091200",
      "48318382080", // 45 GB remaining from last month
      pastReset.toString()
    );

    const freshUsage = new ProxyUsage(
      "53687091200",
      "53687091200", // full quota for the new month
      Temporal.Now.instant()
        .add({ hours: 24 * 30 })
        .toString()
    );

    setupStubs({ validProxyPass: true, proxyUsage: freshUsage });
    IPPStartupCache.storeUsageInfo(staleCached);

    const usageRefreshed = new Promise(resolve => {
      IPPProxyManager.addEventListener(
        "IPPProxyManager:UsageChanged",
        function listener(event) {
          if (event.detail.usage.remaining === BigInt("53687091200")) {
            IPPProxyManager.removeEventListener(
              "IPPProxyManager:UsageChanged",
              listener
            );
            resolve();
          }
        }
      );
    });

    IPPProxyManager.init();

    await usageRefreshed;

    Assert.equal(
      IPPProxyManager.usageInfo.remaining,
      BigInt("53687091200"),
      "Stale cached usage metric should be replaced by a newly fetched value"
    );

    await IPPProxyManager.reset();
    IPPProxyManager.uninit();

    Services.prefs.clearUserPref("browser.ipProtection.cacheDisabled");
    Services.prefs.clearUserPref("browser.ipProtection.usageCache");
    Services.prefs.clearUserPref("browser.ipProtection.stateCache");
  }
);

add_task(async function test_scheduleCallback_basic() {
  const now = Temporal.Now.instant();
  const triggerTime = now.add({ milliseconds: 100 });
  const abortController = new AbortController();

  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal
  );

  await schedulePromise;

  Assert.ok(
    callbackTriggered,
    "Callback should be triggered after the timepoint"
  );
});

add_task(async function test_scheduleCallback_abort_before_trigger() {
  const now = Temporal.Now.instant();
  const triggerTime = now.add({ milliseconds: 200 });
  const abortController = new AbortController();

  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal
  );

  abortController.abort();

  await schedulePromise;

  Assert.ok(
    !callbackTriggered,
    "Callback should not be triggered if aborted before timepoint"
  );
});

add_task(async function test_scheduleCallback_abort_during_wait() {
  const now = Temporal.Now.instant();
  const triggerTime = now.add({ milliseconds: 500 });
  const abortController = new AbortController();

  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal
  );

  await new Promise(resolve => {
    do_timeout(100, resolve);
  });

  abortController.abort();

  await schedulePromise;

  Assert.ok(
    !callbackTriggered,
    "Callback should not be triggered if aborted during wait"
  );
});

add_task(async function test_scheduleCallback_timepoint_in_past() {
  const now = Temporal.Now.instant();
  const triggerTime = now.subtract({ milliseconds: 100 });
  const abortController = new AbortController();

  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal
  );

  await schedulePromise;

  Assert.ok(
    callbackTriggered,
    "Callback should be triggered immediately if timepoint is in the past"
  );
});

add_task(async function test_scheduleCallback_long_delay_clamping() {
  const sandbox = sinon.createSandbox();
  const maxSetTimeoutMs = 2147483647;

  const startTime = Temporal.Instant.fromEpochMilliseconds(0);
  const triggerTime = startTime.add({ milliseconds: maxSetTimeoutMs + 5000 });

  let currentTime = startTime;

  const setTimeoutStub = sandbox.stub();
  let timeoutResolvers = [];
  setTimeoutStub.callsFake((callback, ms) => {
    timeoutResolvers.push({ callback, ms });
    return timeoutResolvers.length - 1;
  });

  const clearTimeoutStub = sandbox.stub();

  const mockImports = {
    setTimeout: setTimeoutStub,
    clearTimeout: clearTimeoutStub,
    getNow: () => currentTime,
  };

  const abortController = new AbortController();
  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal,
    mockImports
  );

  await new Promise(resolve => do_timeout(0, resolve));

  Assert.ok(
    setTimeoutStub.calledOnce,
    "setTimeout should be called once initially"
  );
  Assert.equal(
    setTimeoutStub.firstCall.args[1],
    maxSetTimeoutMs,
    "First setTimeout should be clamped to max value"
  );

  currentTime = startTime.add({ milliseconds: maxSetTimeoutMs });
  timeoutResolvers[0].callback();

  // Yield again to allow scheduleCallback to process the timeout and loop back
  await new Promise(resolve => do_timeout(0, resolve));

  Assert.ok(
    setTimeoutStub.calledTwice,
    "setTimeout should be called again after first timeout"
  );
  Assert.equal(
    setTimeoutStub.secondCall.args[1],
    5000,
    "Second setTimeout should use remaining time"
  );

  currentTime = triggerTime;
  timeoutResolvers[1].callback();

  await schedulePromise;

  Assert.ok(callbackTriggered, "Callback should be triggered after all waits");

  sandbox.restore();
});

add_task(async function test_scheduleCallback_abort_stops_loop_promptly() {
  const sandbox = sinon.createSandbox();
  const maxSetTimeoutMs = 2147483647;

  const startTime = Temporal.Instant.fromEpochMilliseconds(0);
  const triggerTime = startTime.add({ milliseconds: maxSetTimeoutMs + 5000 });

  let currentTime = startTime;

  const setTimeoutStub = sandbox.stub();
  let timeoutResolvers = [];
  setTimeoutStub.callsFake((callback, ms) => {
    timeoutResolvers.push({ callback, ms });
    return timeoutResolvers.length - 1;
  });

  const clearTimeoutStub = sandbox.stub();

  const mockImports = {
    setTimeout: setTimeoutStub,
    clearTimeout: clearTimeoutStub,
    getNow: () => currentTime,
  };

  const abortController = new AbortController();
  let callbackTriggered = false;
  const callback = () => {
    callbackTriggered = true;
  };

  const schedulePromise = scheduleCallback(
    callback,
    triggerTime,
    abortController.signal,
    mockImports
  );

  await new Promise(resolve => do_timeout(0, resolve));

  Assert.ok(
    setTimeoutStub.calledOnce,
    "setTimeout should be called once initially"
  );

  // Abort the signal while the first timeout is still pending.
  abortController.abort();

  // The abort handler clears the first timeout and resolves the promise.
  // The loop should now exit promptly without scheduling another setTimeout.
  await new Promise(resolve => do_timeout(0, resolve));

  await schedulePromise;

  Assert.ok(!callbackTriggered, "Callback should not be triggered after abort");
  Assert.ok(
    clearTimeoutStub.calledOnce,
    "clearTimeout should be called once when the abort handler fires"
  );
  Assert.ok(
    setTimeoutStub.calledOnce,
    "setTimeout should only be called once - the loop should exit after abort " +
      "instead of scheduling another timeout"
  );

  sandbox.restore();
});

[401, 403, 407].forEach(httpStatus => {
  add_task(async function test_handleProxyErrorEvent_triggers_rotation() {
    info(`Running test for HTTP ${httpStatus} proxy error`);
    setupStubs({ validProxyPass: true });

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

    const isolationKey = IPPProxyManager.isolationKey;

    setupStubs({ validProxyPass: true });

    const oldIsolationKey = IPPProxyManager.isolationKey;
    await IPPProxyManager.handleProxyErrorEvent(
      new CustomEvent("proxy-http-error", {
        detail: { level: "error", isolationKey, httpStatus },
      })
    );

    Assert.notEqual(
      IPPProxyManager.isolationKey,
      oldIsolationKey,
      `Isolation key should change after ${httpStatus} triggers rotation`
    );
    Assert.equal(
      IPPProxyManager.state,
      IPPProxyStates.ACTIVE,
      `Should remain active after ${httpStatus} rotation`
    );

    IPProtectionService.uninit();
  });
});

add_task(async function test_IPPProxyManager_start_forwards_country() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();

  let sandbox = sinon.createSandbox();
  setupStubs();

  const getLocationSpy = sandbox.spy(IPProtectionServerlist, "getLocation");
  const getRecommendedSpy = sandbox.spy(
    IPProtectionServerlist,
    "getRecommendedLocation"
  );

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
  await IPPProxyManager.start(true, false, "US");
  await activeEvent;

  Assert.ok(
    getLocationSpy.calledWith("US"),
    "getLocation should be called with the requested country code"
  );
  Assert.ok(
    getRecommendedSpy.notCalled,
    "getRecommendedLocation should not be called when a country is provided"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

add_task(
  async function test_IPPProxyManager_start_without_country_uses_recommended() {
    await IPPProxyManager.reset();
    await putServerInRemoteSettings();

    let sandbox = sinon.createSandbox();
    setupStubs();

    const getRecommendedSpy = sandbox.spy(
      IPProtectionServerlist,
      "getRecommendedLocation"
    );

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
    await IPPProxyManager.start(true, false);
    await activeEvent;

    Assert.ok(
      getRecommendedSpy.calledOnce,
      "Omitting the country should fall back to getRecommendedLocation"
    );

    await IPPProxyManager.stop();
    IPProtectionService.uninit();
    sandbox.restore();
  }
);

add_task(async function test_IPPProxyManager_switch_noop_when_not_active() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();

  let sandbox = sinon.createSandbox();
  setupStubs();

  const getLocationSpy = sandbox.spy(IPProtectionServerlist, "getLocation");

  const readyEvent = waitForEvent(
    IPProtectionService,
    "IPProtectionService:StateChanged",
    () => IPProtectionService.state === IPProtectionStates.READY
  );

  IPProtectionService.init();

  await readyEvent;

  const result = IPPProxyManager.switch("US");

  Assert.deepEqual(
    result,
    { switched: false, error: ERRORS.NOT_READY },
    "switch() should return {switched: false} with a reason when not ACTIVE"
  );

  Assert.ok(
    getLocationSpy.notCalled,
    "switch() should not touch the serverlist when not ACTIVE"
  );

  IPProtectionService.uninit();
  sandbox.restore();
});

add_task(async function test_IPPProxyManager_switch_from_active() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();

  let sandbox = sinon.createSandbox();
  setupStubs();

  const getLocationSpy = sandbox.spy(IPProtectionServerlist, "getLocation");
  const suspendSpy = sandbox.spy(IPPChannelFilter.prototype, "suspend");
  const initSpy = sandbox.spy(IPPChannelFilter.prototype, "initialize");

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

  await IPPProxyManager.start(true, false);
  await activeEvent;

  const result = IPPProxyManager.switch("US");

  Assert.deepEqual(
    result,
    { switched: true },
    "switch() should report success from ACTIVE"
  );

  Assert.ok(
    getLocationSpy.calledWith("US"),
    "getLocation should be called with the requested country code"
  );

  Assert.ok(
    suspendSpy.calledBefore(initSpy.lastCall),
    "suspend must be called before the re-initialize"
  );

  Assert.deepEqual(
    IPPProxyManager.location,
    IPProtectionServerlist.getLocation("US"),
    "location should follow the switch"
  );

  await IPPProxyManager.stop();

  Assert.equal(
    IPPProxyManager.location,
    null,
    "location should be cleared when the proxy stops"
  );

  IPProtectionService.uninit();
  sandbox.restore();
});

add_task(async function test_IPPProxyManager_switch_recommended() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();

  let sandbox = sinon.createSandbox();
  setupStubs();

  const getRecommendedSpy = sandbox.spy(
    IPProtectionServerlist,
    "getRecommendedLocation"
  );

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

  await IPPProxyManager.start(true, false);
  await activeEvent;

  const result = IPPProxyManager.switch();

  Assert.deepEqual(
    result,
    { switched: true },
    "switch() with no country should report success from ACTIVE"
  );

  Assert.ok(
    getRecommendedSpy.called,
    "getRecommendedLocation should be called when no country is provided"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * Tests that IPPProxyManager refreshes usage info automatically when
 * IPProtectionService transitions into READY, and does not re-trigger the
 * refresh when the state remains READY across subsequent updateState() calls.
 */
add_task(
  async function test_IPPProxyManager_refreshes_usage_on_transition_into_ready() {
    await IPPProxyManager.reset();
    let sandbox = sinon.createSandbox();
    setupStubs(sandbox);

    // Stub refreshUsage before the state transitions to READY so we can
    // observe the listener-driven call.
    const refreshUsageStub = sandbox.stub(IPPProxyManager, "refreshUsage");

    const readyEvent = waitForEvent(
      IPProtectionService,
      "IPProtectionService:StateChanged",
      () => IPProtectionService.state === IPProtectionStates.READY
    );

    IPProtectionService.init();
    await readyEvent;

    Assert.ok(
      refreshUsageStub.calledOnce,
      "refreshUsage should be called when IPProtectionService transitions into READY"
    );

    IPProtectionService.uninit();
    sandbox.restore();
  }
);

add_task(async function test_IPPProxyManager_switch_no_server_found() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();

  let sandbox = sinon.createSandbox();
  setupStubs();

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

  await IPPProxyManager.start(true, false);
  await activeEvent;

  sandbox.stub(IPProtectionServerlist, "selectServer").returns(null);

  const result = IPPProxyManager.switch("ZZ");

  Assert.equal(
    result.switched,
    false,
    "switch() should report failure when no server is found"
  );

  Assert.ok(
    result.error,
    "switch() should return an error string when no server is found"
  );

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "State should move to ERROR when no server is found"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * A start() issued while an activation is running must join it rather than
 * open a second one racing for the same channel filter.
 */
add_task(
  async function test_IPPProxyManager_start_joins_activation_in_flight() {
    await IPPProxyManager.reset();
    await putServerInRemoteSettings();
    setupStubs();
    await initServiceToReady();
    await IPProtectionServerlist.maybeFetchList();

    const sandbox = sinon.createSandbox();
    const fetching = Promise.withResolvers();
    const fetchingList = Promise.withResolvers();
    const fetchList = sandbox
      .stub(IPProtectionServerlist, "maybeFetchList")
      .callsFake(() => {
        fetching.resolve();
        return fetchingList.promise;
      });

    const first = IPPProxyManager.start(true, false);
    await fetching.promise;

    const second = IPPProxyManager.start(false, false);
    Assert.equal(
      fetchList.callCount,
      1,
      "the second start() joins the activation instead of opening another"
    );

    const active = waitForProxyState(IPPProxyStates.ACTIVE);
    fetchingList.resolve();
    Assert.deepEqual(
      await Promise.all([first, second]),
      [{ started: true }, { started: true }],
      "both callers get the result of the one activation"
    );
    await active;

    await IPPProxyManager.stop();
    IPProtectionService.uninit();
    sandbox.restore();
  }
);

add_task(async function test_IPPProxyManager_modes() {
  await IPPProxyManager.reset();
  await putServerInRemoteSettings();
  setupStubs();
  await initServiceToReady();
  registerCleanupFunction(() => Services.prefs.clearUserPref(MODE_PREF));

  const connect = async mode => {
    const activeEvent = waitForProxyState(IPPProxyStates.ACTIVE);
    await IPPProxyManager.start(true, false, undefined, mode);
    await activeEvent;
  };

  Assert.deepEqual(
    IPPProxyManager.setMode(IPPMode.MODE_TRACKER),
    { changed: false, error: ERRORS.NOT_READY },
    "setMode should refuse while the proxy is not active"
  );

  Services.prefs.setIntPref(MODE_PREF, IPPMode.MODE_INCLUSION);
  await connect();

  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "the mode pref should seed a connection"
  );
  Assert.equal(
    IPPProxyManager.channelFilter().mode,
    IPPMode.MODE_INCLUSION,
    "the channel filter should be routing in the seeded mode"
  );

  await IPPProxyManager.stop();

  Assert.equal(
    IPPProxyManager.mode,
    null,
    "a stopped proxy should have no mode"
  );

  await connect(IPPMode.MODE_TRACKER);

  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_TRACKER,
    "a mode passed to start() should beat the pref"
  );

  const sandbox = sinon.createSandbox();
  const suspendSpy = sandbox.spy(IPPChannelFilter.prototype, "suspend");
  const initSpy = sandbox.spy(IPPChannelFilter.prototype, "initialize");
  const modeEvent = waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:ModeChanged"
  );

  Assert.deepEqual(
    IPPProxyManager.setMode(42),
    { changed: true },
    "setMode should report success"
  );
  const { detail } = await modeEvent;

  Assert.equal(
    detail.mode,
    IPPMode.MODE_FULL,
    "an unknown mode should fall back to MODE_FULL and be announced as such"
  );
  Assert.equal(
    IPPProxyManager.channelFilter().mode,
    IPPMode.MODE_FULL,
    "the live channel filter should have the new mode"
  );
  Assert.ok(
    suspendSpy.notCalled && initSpy.notCalled,
    "a mode change should not re-point the connection"
  );

  await IPPProxyManager.stop();
  IPProtectionService.uninit();
  sandbox.restore();
});

/**
 * Tests that start() before the service is READY holds traffic in the asked
 * mode and consults nothing until the service arrives, then completes the
 * activation on its own. It starts before the service init, as a helper does,
 * so the cached non-READY state the init publishes lands on the wait.
 */
add_task(async function test_IPPProxyManager_start_waits_for_the_service() {
  await IPPProxyManager.reset();
  setupStubs({ signedIn: false });
  // Reopens the startup cache window the manifest closes for every test. Both
  // getters are shadowed: the real `state` throws once startup has completed.
  Object.defineProperty(IPPStartupCache, "isStartupCompleted", {
    get: () => false,
    configurable: true,
  });
  Object.defineProperty(IPPStartupCache, "state", {
    get: () => IPProtectionStates.UNAUTHENTICATED,
    configurable: true,
  });
  const sandbox = sinon.createSandbox();
  const fetchList = sandbox.spy(IPProtectionServerlist, "maybeFetchList");
  const aboutToStart = sandbox.spy(IPPDummyAuthProvider, "aboutToStart");

  const states = [];
  const record = () =>
    states.push({
      state: IPPProxyManager.state,
      connected: !!IPPProxyManager.channelFilter()?.proxyInfo,
    });
  IPPProxyManager.addEventListener("IPPProxyManager:StateChanged", record);

  const startPromise = IPPProxyManager.start(
    false,
    false,
    undefined,
    IPPMode.MODE_INCLUSION
  );
  IPProtectionService.init();

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVATING,
    "The wait should read as activating"
  );
  Assert.ok(
    IPPProxyManager.channelFilter()?.active,
    "The filter should hold traffic while the service is not ready"
  );
  Assert.equal(
    IPPProxyManager.channelFilter().mode,
    IPPMode.MODE_INCLUSION,
    "The filter should hold traffic for the mode this activation asked for"
  );
  Assert.ok(
    !fetchList.called && !aboutToStart.called,
    "Neither the server list nor the auth provider should be consulted yet"
  );

  setupStubs({ signedIn: true });
  delete IPPStartupCache.isStartupCompleted;
  delete IPPStartupCache.state;
  IPProtectionService.updateState();
  await IPProtectionService.initOnStartupCompleted();
  const result = await startPromise;

  Assert.ok(result.started, "The activation should complete on its own");
  Assert.ok(
    fetchList.called && aboutToStart.called,
    "The server list and the auth provider should be consulted once ready"
  );
  Assert.ok(IPPProxyManager.hasValidProxyPass, "A pass should be fetched");
  IPPProxyManager.removeEventListener("IPPProxyManager:StateChanged", record);
  Assert.deepEqual(
    states,
    [
      { state: IPPProxyStates.ACTIVATING, connected: false },
      { state: IPPProxyStates.ACTIVE, connected: true },
    ],
    "The activation should own the state from the ask to the connection"
  );

  await IPPProxyManager.stop(false);
  IPProtectionService.uninit();
  sandbox.restore();
});
