/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  ContentRelevancyManager:
    "resource://gre/modules/ContentRelevancyManager.sys.mjs",
  exposedForTesting: "resource://gre/modules/ContentRelevancyManager.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
});

const PREF_CONTENT_RELEVANCY_ENABLED = "toolkit.contentRelevancy.enabled";
const PREF_TIMER_INTERVAL = "toolkit.contentRelevancy.timerInterval";

// These consts are copied from the update timer manager test. See
// `initUpdateTimerManager()`.
const PREF_APP_UPDATE_TIMERMINIMUMDELAY = "app.update.timerMinimumDelay";
const PREF_APP_UPDATE_TIMERFIRSTINTERVAL = "app.update.timerFirstInterval";
const MAIN_TIMER_INTERVAL = 1000; // milliseconds
const CATEGORY_UPDATE_TIMER = "update-timer";

let gSandbox;

add_setup(() => {
  gSandbox = sinon.createSandbox();
  initUpdateTimerManager();
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  ContentRelevancyManager.init();

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
    gSandbox.restore();
  });
});

add_task(function test_init() {
  Assert.ok(ContentRelevancyManager.initialized, "Init should succeed");
  Assert.ok(ContentRelevancyManager.enabled, "Enabled flag should be set");
});

add_task(function test_uninit() {
  ContentRelevancyManager.uninit();

  Assert.ok(!ContentRelevancyManager.initialized, "Uninit should succeed");
});

add_task(function test_store_manager() {
  const RustRelevancyStoreManager = exposedForTesting.RustRelevancyStoreManager;
  const fakeStore = {
    close: gSandbox.fake(),
  };
  const fakeRustRelevancyStore = {
    init: gSandbox.fake.returns(fakeStore),
  };
  const storeManager = new RustRelevancyStoreManager(
    "test-path",
    fakeRustRelevancyStore
  );
  Assert.equal(fakeRustRelevancyStore.init.callCount, 1);
  Assert.deepEqual(fakeRustRelevancyStore.init.firstCall.args[0], "test-path");
  // TODO (1956519): test that the second arg is the app-wide RemoteSettingsService
  // store should throw before the manager is enabled
  Assert.throws(() => storeManager.store, /StoreDisabledError/);
  // Once the manager is enabled, store should return the RustRelevancyStore
  storeManager.enable();
  Assert.equal(storeManager.store, fakeStore);
  // Disabling the store should call `close` and make store throw again
  Assert.equal(fakeStore.close.callCount, 0);
  storeManager.disable();
  Assert.equal(fakeStore.close.callCount, 1);
  Assert.throws(() => storeManager.store, /StoreDisabledError/);
  // Test calling enable() one more time
  storeManager.enable();
  Assert.equal(storeManager.store, fakeStore);
});

add_task(async function test_timer() {
  // Set the timer interval to 0 will trigger the timer right away.
  Services.prefs.setIntPref(PREF_TIMER_INTERVAL, 0);
  gSandbox.spy(ContentRelevancyManager, "notify");

  ContentRelevancyManager.init();

  await TestUtils.waitForCondition(
    () => ContentRelevancyManager.notify.called,
    "The timer callback should be called"
  );

  Services.prefs.clearUserPref(PREF_TIMER_INTERVAL);
  gSandbox.restore();
});

add_task(async function test_feature_toggling() {
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, false);
  // Set the timer interval to 0 will trigger the timer right away.
  Services.prefs.setIntPref(PREF_TIMER_INTERVAL, 0);
  gSandbox.spy(ContentRelevancyManager, "notify");

  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1100));
  Assert.ok(
    ContentRelevancyManager.notify.notCalled,
    "Timer should not be registered if disabled"
  );

  // Toggle the pref again should re-enable the feature.
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  await TestUtils.waitForTick();

  await TestUtils.waitForCondition(
    () => ContentRelevancyManager.notify.called,
    "The timer callback should be called"
  );

  Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
  Services.prefs.clearUserPref(PREF_TIMER_INTERVAL);
  gSandbox.restore();
});

add_task(async function test_call_disable_twice() {
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, false);
  await TestUtils.waitForTick();

  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, false);
  await TestUtils.waitForTick();

  Assert.ok(true, "`#disable` should be safe to call multiple times");

  Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
});

add_task(async function test_shutdown_blocker() {
  Services.prefs.setBoolPref(PREF_CONTENT_RELEVANCY_ENABLED, true);
  await TestUtils.waitForTick();

  gSandbox.spy(ContentRelevancyManager, "interrupt");

  // Simulate shutdown.
  Services.prefs.setBoolPref("toolkit.asyncshutdown.testing", true);
  AsyncShutdown.profileChangeTeardown._trigger();

  await TestUtils.waitForCondition(
    () => ContentRelevancyManager.interrupt.calledOnce,
    "The interrupt shutdown blocker should be called"
  );

  Services.prefs.clearUserPref("toolkit.asyncshutdown.testing");
  Services.prefs.clearUserPref(PREF_CONTENT_RELEVANCY_ENABLED);
  gSandbox.restore();
});

/**
 * Sets up the update timer manager for testing: makes it fire more often,
 * removes all existing timers, and initializes it for testing. The body of this
 * function is copied from:
 * https://searchfox.org/mozilla-central/source/toolkit/components/timermanager/tests/unit/consumerNotifications.js
 */
function initUpdateTimerManager() {
  // Set the timer to fire every second
  Services.prefs.setIntPref(
    PREF_APP_UPDATE_TIMERMINIMUMDELAY,
    MAIN_TIMER_INTERVAL / 1000
  );
  Services.prefs.setIntPref(
    PREF_APP_UPDATE_TIMERFIRSTINTERVAL,
    MAIN_TIMER_INTERVAL
  );

  // Remove existing update timers to prevent them from being notified
  for (let { data: entry } of Services.catMan.enumerateCategory(
    CATEGORY_UPDATE_TIMER
  )) {
    Services.catMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, entry, false);
  }

  Cc["@mozilla.org/updates/timer-manager;1"]
    .getService(Ci.nsIUpdateTimerManager)
    .QueryInterface(Ci.nsIObserver)
    .observe(null, "utm-test-init", "");
}
