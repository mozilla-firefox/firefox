/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { Normandy } = ChromeUtils.importESModule(
  "resource://normandy/Normandy.sys.mjs"
);
const { NormandyTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NormandyTestUtils.sys.mjs"
);
ChromeUtils.defineESModuleGetters(this, {
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

NormandyTestUtils.init({ add_task });
const { decorate_task } = NormandyTestUtils;

// Normandy's initialization function should register the observer for UI
// startup before it's first await.
decorate_task(
  NormandyTestUtils.withStub(Normandy, "finishInit"),
  async function test_normandy_init_applies_startup_prefs_synchronously() {
    let originalDeferred = Normandy.uiAvailableNotificationObserved;
    let mockUiAvailableDeferred = Promise.withResolvers();
    Normandy.uiAvailableNotificationObserved = mockUiAvailableDeferred;

    let promiseResolvedCount = 0;
    mockUiAvailableDeferred.promise.then(() => promiseResolvedCount++);

    let initPromise = Normandy.init();

    Assert.equal(promiseResolvedCount, 0);
    Normandy.observe(null, "sessionstore-windows-restored");
    await TestUtils.waitForCondition(() => promiseResolvedCount === 1);

    await initPromise;
    await Normandy.uninit();
    Normandy.uiAvailableNotificationObserved = originalDeferred;
  }
);
