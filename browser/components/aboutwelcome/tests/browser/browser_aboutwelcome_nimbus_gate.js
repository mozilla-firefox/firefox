/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { ExperimentAPI } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);
const { RemoteSettingsExperimentLoader } = ChromeUtils.importESModule(
  "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs"
);
const { TelemetryReportingPolicy } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryReportingPolicy.sys.mjs"
);
const { resetNimbusReadyPromiseForTesting } = ChromeUtils.importESModule(
  "resource:///actors/AboutWelcomeParent.sys.mjs"
);

async function openAboutWelcome() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:welcome",
    true
  );
  const browser = tab.linkedBrowser;

  return { tab, browser };
}

function resetNimbusState() {
  resetNimbusReadyPromiseForTesting();
  try {
    ExperimentAPI._resetForTests();
  } catch {}
}

add_task(
  async function test_aboutwelcome_calls_nimbus_init_when_gate_enabled() {
    resetNimbusState();

    const sandbox = sinon.createSandbox();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.aboutwelcome.enabled", true],
        ["browser.aboutwelcome.experimentsGate.enabled", true],
        ["trailhead.firstrun.didSeeAboutWelcome", false],
      ],
    });

    let initCalled = false;
    sandbox.stub(ExperimentAPI, "init").callsFake(async () => {
      initCalled = true;
    });

    const { tab } = await openAboutWelcome();

    await TestUtils.waitForCondition(
      () => initCalled,
      "ExperimentAPI.init should be called when experiments gate pref is enabled"
    );

    Assert.ok(
      initCalled,
      "ExperimentAPI.init was called for about:welcome with gate enabled"
    );

    BrowserTestUtils.removeTab(tab);
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_aboutwelcome_does_not_call_init_when_gate_disabled() {
    resetNimbusState();

    const sandbox = sinon.createSandbox();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.aboutwelcome.enabled", true],
        ["browser.aboutwelcome.experimentsGate.enabled", false],
        ["trailhead.firstrun.didSeeAboutWelcome", false],
      ],
    });

    const initSpy = sandbox.spy(ExperimentAPI, "init");

    const { tab } = await openAboutWelcome();

    await TestUtils.waitForCondition(
      () => tab.linkedBrowser.currentURI.spec === "about:welcome",
      "about:welcome tab did load"
    );
    await TestUtils.waitForTick();

    Assert.ok(
      !initSpy.called,
      "ExperimentAPI.init should not be called by about:welcome when gate pref is disabled"
    );

    BrowserTestUtils.removeTab(tab);
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_aboutwelcome_loads_after_timeout_even_if_nimbus_hangs() {
    resetNimbusState();
    const sandbox = sinon.createSandbox();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.aboutwelcome.enabled", true],
        ["browser.aboutwelcome.experimentsGate.enabled", true],
        ["browser.aboutwelcome.experimentsGate.maxDisplayMs", 2000],
        ["trailhead.firstrun.didSeeAboutWelcome", false],
      ],
    });

    sandbox.stub(ExperimentAPI, "init").returns(new Promise(() => {}));
    sandbox.stub(ExperimentAPI, "_rsLoader").value({
      finishedUpdating: () => new Promise(() => {}),
    });

    const startTime = Date.now();
    const { tab } = await openAboutWelcome();

    await TestUtils.waitForCondition(
      () => tab.linkedBrowser.currentURI.spec === "about:welcome",
      "about:welcome should load after timeout"
    );

    const loadTime = Date.now() - startTime;
    Assert.less(loadTime, 15000, "AW should be loaded within 15s");

    BrowserTestUtils.removeTab(tab);
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_aboutwelcome_renders_after_nimbus_gating() {
  resetNimbusState();
  const sandbox = sinon.createSandbox();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.aboutwelcome.enabled", true],
      ["browser.aboutwelcome.experimentsGate.enabled", true],
      ["trailhead.firstrun.didSeeAboutWelcome", false],
    ],
  });

  sandbox.stub(ExperimentAPI, "init").resolves();
  sandbox.stub(ExperimentAPI, "_rsLoader").value({
    _hasUpdatedOnce: false,
    finishedUpdating: async () => {
      ExperimentAPI._rsLoader._hasUpdatedOnce = true;
    },
  });

  const { tab, browser } = await openAboutWelcome();

  await TestUtils.waitForCondition(
    () => tab.linkedBrowser.currentURI.spec === "about:welcome",
    "about:welcome should load"
  );

  const contentLoaded = await SpecialPowers.spawn(browser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () => content.document.readyState === "complete",
      "Document loaded"
    );

    return !!content.document.querySelector("#multi-stage-message-root");
  });

  Assert.ok(contentLoaded, "about:welcome content should be rendered");

  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_aboutwelcome_loads_after_nimbus_error() {
  resetNimbusState();
  const sandbox = sinon.createSandbox();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.aboutwelcome.enabled", true],
      ["browser.aboutwelcome.experimentsGate.enabled", true],
      ["trailhead.firstrun.didSeeAboutWelcome", false],
    ],
  });

  sandbox.stub(ExperimentAPI, "init").rejects(new Error("Network error"));
  sandbox.stub(ExperimentAPI, "_rsLoader").value({
    finishedUpdating: () => Promise.reject(new Error("RS error")),
  });

  const { tab } = await openAboutWelcome();

  await TestUtils.waitForCondition(
    () => tab.linkedBrowser.currentURI.spec === "about:welcome",
    "about:welcome should load after Nimbus error"
  );

  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

// When `ExperimentAPI.init()` is already in flight (e.g., kicked off
// early in startup) and about:welcome opens, the gate should
// await the actual completion of that in-flight init
// See Bug 2042553
add_task(async function test_gate_awaits_in_flight_init() {
  const sandbox = sinon.createSandbox();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.aboutwelcome.enabled", true],
      ["browser.aboutwelcome.experimentsGate.enabled", true],
      ["trailhead.firstrun.didSeeAboutWelcome", false],
    ],
  });

  // ExperimentAPI has already been initialized by Firefox startup,
  // so reset for a fresh state
  resetNimbusReadyPromiseForTesting();
  ExperimentAPI._resetForTests();

  // Hold _rsLoader.enable() so the first init() stays in flight while
  // the about:welcome gate runs.
  const enableGate = Promise.withResolvers();
  sandbox.stub(ExperimentAPI._rsLoader, "enable").callsFake(async (...args) => {
    await enableGate.promise;
    return RemoteSettingsExperimentLoader.prototype.enable.call(
      ExperimentAPI._rsLoader,
      ...args
    );
  });

  // Capture _hasUpdatedOnce at the first finishedUpdating() call.
  // init() awaits real completion first, so by the time anything calls
  // finishedUpdating() rsLoader has already finished updating
  let hasUpdatedOnceAtFirstCall = null;
  sandbox.stub(ExperimentAPI._rsLoader, "finishedUpdating").callsFake(() => {
    if (hasUpdatedOnceAtFirstCall === null) {
      hasUpdatedOnceAtFirstCall = ExperimentAPI._rsLoader._hasUpdatedOnce;
    }
    return RemoteSettingsExperimentLoader.prototype.finishedUpdating.call(
      ExperimentAPI._rsLoader
    );
  });

  // First caller (BrowserGlue stand-in)
  const firstInit = ExperimentAPI.init();

  // Open about:welcome so its gate calls init() as the second caller.
  const { tab } = await openAboutWelcome();

  enableGate.resolve();
  await firstInit;
  await TestUtils.waitForCondition(
    () => hasUpdatedOnceAtFirstCall !== null,
    "finishedUpdating() was called"
  );

  Assert.strictEqual(
    hasUpdatedOnceAtFirstCall,
    true,
    "Gate did not call finishedUpdating() until init genuinely finished"
  );

  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});
