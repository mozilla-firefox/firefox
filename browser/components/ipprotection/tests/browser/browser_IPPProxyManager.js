/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { IPProtectionServerlist } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs"
);

add_task(async function test_IPPProxyManager_handleProxyErrorEvent() {
  setupService({
    isReady: true,
  });

  IPProtectionService.updateState();

  await IPProtectionServerlist.maybeFetchList();

  await IPPProxyManager.start();

  const cases = [
    {
      name: "Non-401 HTTP status - should not rotate",
      httpStatus: 500,
      level: "error",
      shouldRotate: false,
    },
    {
      name: "Different isolation key - should not rotate",
      httpStatus: 401,
      level: "error",
      isolationKey: "different-key",
      shouldRotate: false,
    },
    {
      name: "401 with warning level - accepts whatever shouldRotate returns",
      httpStatus: 401,
      level: "warning",
      shouldRotate: false, // This will depend on the actual shouldRotate implementation
    },
    {
      name: "401 with error level - should rotate",
      httpStatus: 401,
      level: "error",
      shouldRotate: true,
    },
  ];

  for (const testCase of cases) {
    const originalIsolationKey = IPPProxyManager.isolationKey;
    // Create the error event
    const errorEvent = new CustomEvent("proxy-http-error", {
      detail: {
        isolationKey: testCase.isolationKey || originalIsolationKey,
        level: testCase.level,
        httpStatus: testCase.httpStatus,
      },
    });

    console.log(`Testing: ${testCase.name}`);

    const result = IPPProxyManager.handleProxyErrorEvent(errorEvent);

    if (testCase.shouldRotate) {
      Assert.ok(
        result,
        `${testCase.name}: Should return a promise when rotation is triggered`
      );

      await result;

      const newIsolationKey = IPPProxyManager.isolationKey;
      Assert.notEqual(
        originalIsolationKey,
        newIsolationKey,
        `${testCase.name}: Isolation key should change after token rotation`
      );
    } else {
      Assert.equal(
        result,
        undefined,
        `${testCase.name}: Should not return a promise when rotation is not triggered`
      );

      const unchangedIsolationKey = IPPProxyManager.isolationKey;
      Assert.equal(
        originalIsolationKey,
        unchangedIsolationKey,
        `${testCase.name}: Isolation key should not change when rotation is not triggered`
      );
    }
  }

  // Test inactive connection
  const isolationKeyBeforeStop = IPPProxyManager.isolationKey;
  await IPPProxyManager.stop();

  const inactiveErrorEvent = new CustomEvent("proxy-http-error", {
    detail: {
      isolationKey: isolationKeyBeforeStop,
      level: "error",
      httpStatus: 401,
    },
  });

  const inactiveResult =
    IPPProxyManager.handleProxyErrorEvent(inactiveErrorEvent);
  Assert.equal(
    inactiveResult,
    undefined,
    "Should not return a promise when connection is inactive"
  );

  cleanupService();
});

/**
 * Test for Bug 1999946 - When having an issue in IPPProxyManager.start
 * we must make sure we don't have an invalid connection left running.
 */
add_task(async function test_IPPProxyManager_bug_1999946() {
  const { IPPChannelFilter } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs"
  );

  Services.prefs.clearUserPref("browser.ipProtection.enabled");

  // Hook the Call to create to capture the created channel filter
  let channelFilterRef = null;
  const sandbox = sinon.createSandbox();
  const originalCreate = IPPChannelFilter.create.bind(IPPChannelFilter);
  sandbox.stub(IPPChannelFilter, "create").callsFake(function () {
    channelFilterRef = originalCreate();
    sandbox.spy(channelFilterRef, "stop");
    return channelFilterRef;
  });

  IPPDummyAuthProvider.setProxyPassError(new Error("Simulate a Fail"));

  setupService({
    isReady: true,
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  await IPProtectionServerlist.maybeFetchList();

  await IPPProxyManager.start();

  Assert.ok(channelFilterRef, "Channel filter should have been created");
  Assert.ok(
    channelFilterRef.stop.calledOnce,
    "Channel filter stop should be called when fetchProxyPass fails"
  );

  await IPPProxyManager.stop();

  sandbox.restore();
  cleanupService();
});

/**
 * Tests that opening the panel when the IPPProxyManager state is PAUSED shows the paused view.
 */
add_task(async function test_IPPProxyManager_paused_shown() {
  IPPProxyManager.reset();

  const usage = makeUsage("5368709120", "0", "2027-01-01T00:00:00.000Z");
  setupService({
    isReady: true,
    usageInfo: usage,
  });
  IPProtectionService.updateState();

  let content = await openPanel();

  IPPProxyManager.refreshUsage();
  await waitForProxyState(IPPProxyStates.PAUSED);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "IPPProxyManager state should be paused when bandwidth quota is exceeded"
  );

  Assert.ok(content.upgradeEl, "Paused upgrade content should be shown");

  let pausedIcon = content.statusBoxEl?.querySelector('img[slot="image"]');
  Assert.ok(pausedIcon, "Paused icon should show in the panel");

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    button.classList.contains("ipprotection-paused"),
    "Toolbar icon should show paused status"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that setting usage with remaining > 0 unpauses the IPPProxyManager and shows the main view.
 */
add_task(async function test_IPPProxyManager_unpause_on_available() {
  IPPProxyManager.reset();
  setupService({
    isReady: true,
  });

  IPProtectionService.updateState();
  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "Should be in READY state"
  );

  // Pause the service
  const quotaExceededUsage = makeUsage(
    "5368709120",
    "0",
    "2027-01-01T00:00:00.000Z"
  );

  setupService({
    usageInfo: quotaExceededUsage,
  });

  let content = await openPanel();

  IPPProxyManager.refreshUsage();
  await waitForProxyState(IPPProxyStates.PAUSED);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "IPPProxyManager state should be paused when bandwidth quota is exceeded"
  );

  Assert.ok(content.upgradeEl, "Paused upgrade content should be shown");

  // Reset usage to unpause
  const restoredUsage = makeUsage(
    "5368709120",
    "4294967296",
    "2027-01-01T00:00:00.000Z"
  );
  setupService({
    usageInfo: restoredUsage,
  });

  IPPProxyManager.refreshUsage();
  await waitForProxyState(IPPProxyStates.READY);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager state should be READY after bandwidth is restored"
  );

  let statusCard = content.statusCardEl;
  let turnOnButton = statusCard.actionButtonEl;

  Assert.ok(turnOnButton, "Turn on button should be shown when un-paused");

  await closePanel();
  cleanupService();
});

/**
 * Tests that turning off the VPN refreshes usage and transitions to PAUSED
 * when bandwidth quota is exceeded.
 */
add_task(async function test_IPPProxyManager_update_usage_on_stop() {
  IPPProxyManager.reset();
  setupService({
    isReady: true,
  });

  IPProtectionService.updateState();
  Assert.equal(
    IPProtectionService.state,
    IPProtectionStates.READY,
    "Should be in READY state"
  );

  // Open panel and click the button to turn VPN on
  let content = await openPanel();
  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  Assert.ok(actionButton, "Turn on button should be shown");

  actionButton.click();
  await waitForProxyState(IPPProxyStates.ACTIVE);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "IPPProxyManager state should be ACTIVE after clicking turn on"
  );

  // Set usage to half
  const used = BigInt(2684354560).toString(); // 2.5 GB
  const quotaHalfUsage = makeUsage("5368709120", used);
  setupService({
    usageInfo: quotaHalfUsage,
  });

  // Turn off VPN via the panel button
  const usageChangePromise = BrowserTestUtils.waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:UsageChanged"
  );
  actionButton.click();
  await waitForProxyState(IPPProxyStates.READY);
  await usageChangePromise;

  const bandwidthEl = content.bandwidthUsageEl;
  await bandwidthEl.updateComplete;

  Assert.equal(
    bandwidthEl.remaining,
    used,
    "Bandwidth element should show the updated remaining bandwidth"
  );

  // Start the VPN again
  actionButton.click();
  await waitForProxyState(IPPProxyStates.ACTIVE);

  // Set usage to exceed the bandwidth quota while active
  const quotaExceededUsage = makeUsage("5368709120", "0");
  setupService({
    usageInfo: quotaExceededUsage,
  });

  // Turn off VPN via the panel button
  actionButton.click();
  await waitForProxyState(IPPProxyStates.PAUSED);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "IPPProxyManager state should be PAUSED after stopping with exceeded quota"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that re-opening the panel when the IPPProxyManager state is ACTIVE does not reset the state (Bug 2021236).
 */
add_task(async function test_IPPProxyManager_active_shown() {
  IPPProxyManager.reset();

  const usage = makeUsage();
  setupService({
    isReady: true,
    usageInfo: usage,
  });
  IPProtectionService.updateState();

  let content = await openPanel();

  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  Assert.ok(actionButton, "Turn on button should be shown");

  actionButton.click();

  await waitForProxyState(IPPProxyStates.ACTIVE);

  await closePanel(window, false);

  content = await openPanel();
  statusCard = content.statusCardEl;
  actionButton = statusCard.actionButtonEl;

  await waitForProxyState(IPPProxyStates.ACTIVE);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "IPPProxyManager state should be active when re-opened"
  );

  actionButton.click();

  await waitForProxyState(IPPProxyStates.READY);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager state should be ready after turning off"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that the paused modal isn't shown when quota is exceeded during activation.
 */
add_task(async function test_IPPProxyManager_paused_on_activation() {
  IPPProxyManager.reset();

  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();

  let content = await openPanel();

  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  Assert.ok(actionButton, "Turn on button should be shown");

  // Pause the service
  const quotaExceededUsage = makeUsage("5368709120", "0");
  setupService({
    proxyPass: {
      status: 429,
      error: "quota_exceeded",
      pass: null,
      usage: quotaExceededUsage,
    },
  });

  actionButton.click();

  await waitForProxyState(IPPProxyStates.PAUSED);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "IPPProxyManager state should be paused after quota is exceeded"
  );

  Assert.ok(!window.gDialogBox?.isOpen, "Paused dialog is not shown");

  await closePanel();
  cleanupService();
});

/**
 * Tests that calling rotateProxyPass when quota is exceeded moves to PAUSED state,
 * not ERROR state (Bug 2022865).
 */
add_task(async function test_IPPProxyManager_rotateProxyPass_when_paused() {
  IPPProxyManager.reset();

  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();

  let content = await openPanel();

  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  Assert.ok(actionButton, "Turn on button should be shown");

  actionButton.click();

  await waitForProxyState(IPPProxyStates.ACTIVE);

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ACTIVE,
    "IPPProxyManager state should be active after clicking turn on"
  );

  // Simulate quota exceeded response from fetchProxyPass
  const quotaExceededUsage = makeUsage("5368709120", "0");
  setupService({
    proxyPass: {
      status: 429,
      error: "quota_exceeded",
      pass: null,
      usage: quotaExceededUsage,
    },
  });

  await IPPProxyManager.rotateProxyPass();

  await waitForProxyState(IPPProxyStates.PAUSED);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.PAUSED,
    "rotateProxyPass with exceeded quota should move to PAUSED, not ERROR"
  );

  // Check that the paused dialog is shown
  await TestUtils.waitForCondition(
    () => window.gDialogBox.isOpen,
    "Wait for the dialog to exist"
  );
  Assert.ok(window.gDialogBox.isOpen, "Dialog exists and is open");

  // Close the dialog
  await TestUtils.waitForCondition(
    () =>
      window.gDialogBox.dialog._frame.contentDocument.getElementById(
        "titleContainer"
      ),
    "Wait for the dialog to load"
  );
  let dialogDoc = window.gDialogBox.dialog._frame.contentDocument;
  dialogDoc
    .getElementById("commonDialog")
    .shadowRoot.querySelector("button[dlgtype='accept']")
    .click();
  await TestUtils.waitForCondition(
    () => !window.gDialogBox.isOpen,
    "Wait for the dialog to not exist"
  );

  await closePanel();
  cleanupService();
});
