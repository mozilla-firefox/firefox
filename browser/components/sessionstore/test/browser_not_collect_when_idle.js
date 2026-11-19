/** Test for Bug 1305950 */

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

// The mock idle service.
var idleService = {
  _observers: new Set(),
  _activity: {
    addCalls: [],
    removeCalls: [],
    observerFires: [],
  },

  _reset() {
    this._observers.clear();
    this._activity.addCalls = [];
    this._activity.removeCalls = [];
    this._activity.observerFires = [];
  },

  _fireObservers(state) {
    for (let observer of this._observers.values()) {
      observer.observe(observer, state, null);
      this._activity.observerFires.push(state);
    }
  },

  QueryInterface: ChromeUtils.generateQI(["nsIUserIdleService"]),
  idleTime: 19999,

  addIdleObserver(observer, time) {
    this._observers.add(observer);
    this._activity.addCalls.push(time);
  },

  removeIdleObserver(observer, time) {
    this._observers.delete(observer);
    this._activity.removeCalls.push(time);
  },
};

add_task(async function testIntervalChanges() {
  const PREF_SS_INTERVAL = 2000;

  // We speed up the interval between session saves to ensure that the test
  // runs quickly, and raise `idleDelay` to 1 day so the observer that was
  // registered against the "real" idle service does not interfere, especially
  // in the CI server environment. Pushing the prefs rather than setting them
  // lets the harness restore their prior effective values instead of resetting
  // them to the defaults.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.sessionstore.interval", PREF_SS_INTERVAL],
      ["browser.sessionstore.idleDelay", 86400],
    ],
  });

  // Mock an idle service.
  let fakeIdleService = MockRegistrar.register(
    "@mozilla.org/widget/useridleservice;1",
    idleService
  );
  idleService._reset();

  registerCleanupFunction(async function () {
    // Pop the inner env while the mock is still registered: the pref observer
    // removes the idle observer from whichever service is current, and after
    // the mock is gone the real service does not know the delay this test
    // registered. The outer env is deliberately left on the stack for the
    // harness to pop at the end of the test, when the real service is current
    // again and still holds the delay that env set.
    await SpecialPowers.popPrefEnv();
    MockRegistrar.unregister(fakeIdleService);
  });

  // Hook idle/active observer to mock idle service by changing pref `idleDelay`
  // to a whatever value, which will not be used.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.sessionstore.idleDelay", 5000]],
  });

  // Wait a `sessionstore-state-write-complete` event from any previous
  // scheduled state write. This is needed since the `_lastSaveTime` in
  // runDelayed() should be set at least once, or the `_isIdle` flag will not
  // become effective.
  info("Waiting for sessionstore-state-write-complete notification");
  await TestUtils.topicObserved("sessionstore-state-write-complete");

  info(
    "Got the sessionstore-state-write-complete notification, now testing idle mode"
  );

  // Enter the "idle mode" (raise the `_isIdle` flag) by firing idle
  // observer of mock idle service.
  idleService._fireObservers("idle");

  // Cancel any possible state save, which is not related with this test to
  // avoid interference.
  SessionSaver.cancel();

  let p1 = promiseSaveState();

  // Schedule a state write, which is expeced to be postponed after about
  // `browser.sessionstore.interval.idle` ms, since the idle flag was just set.
  SessionSaver.runDelayed(0);

  // We expect `p1` hits the timeout.
  await Assert.rejects(
    p1,
    /Save state timeout/,
    "[Test 1A] No state write during idle."
  );

  // Test again for better reliability. Same, we expect following promise hits
  // the timeout.
  await Assert.rejects(
    promiseSaveState(),
    /Save state timeout/,
    "[Test 1B] Again: No state write during idle."
  );

  // Back to the active mode.
  info("Start to test active mode...");
  idleService._fireObservers("active");

  info("[Test 2] Waiting for sessionstore-state-write-complete during active");
  await TestUtils.topicObserved("sessionstore-state-write-complete");
});
