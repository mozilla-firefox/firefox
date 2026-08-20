/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_setup(async function setup() {
  Services.prefs.setIntPref("app.update.timerMinimumDelay", 1);
  Services.prefs.setIntPref("app.update.timerFirstInterval", 1000);

  Cc["@mozilla.org/updates/timer-manager;1"]
    .getService(Ci.nsIUpdateTimerManager)
    .QueryInterface(Ci.nsIObserver)
    .observe(null, "utm-test-init", "");
});

add_task(async function testTimerTriggersUpdate() {
  Services.prefs.setIntPref("app.normandy.run_interval_seconds", 1);
  const { sandbox, loader, cleanup } = await NimbusTestUtils.setupTest({
    init: false,
  });

  sandbox.stub(loader, "updateRecipes");

  await ExperimentAPI.init();

  Assert.ok(
    loader.updateRecipes.calledWithExactly("enabled", { forceSync: false }),
    "should call updateRecipes() with enabled trigger"
  );

  await TestUtils.waitForCondition(
    () => loader.updateRecipes.calledWith("timer"),
    "should call updateRecipes() with timer trigger"
  );

  Services.prefs.setIntPref("app.normandy.run_interval_seconds", 0);

  await cleanup();
});
