/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

var gTestTab;
var gContentAPI;

add_task(setup_UITourTest);

const IS_ESR = AppConstants.IS_ESR;

add_UITour_task(async function test_showFirefoxAccountsForAIWindow() {
  if (IS_ESR) {
    Assert.ok(true, "Smart Window is not available on ESR, skipping.");
    return;
  }

  // Block Smart Window via AI control
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ai.control.smartWindow", "blocked"]],
  });

  // Stub launchWindow to prevent actually opening a window
  let launchStub = sinon.stub(AIWindow, "launchWindow");
  launchStub.resolves(true);

  gContentAPI.showFirefoxAccountsForAIWindow();

  await TestUtils.waitForCondition(
    () => launchStub.callCount > 0,
    "Waiting for launchWindow to be called"
  );

  // Verify the pref was overridden to "available"
  Assert.equal(
    Services.prefs.getStringPref("browser.ai.control.smartWindow", ""),
    "available",
    "AI control pref should be set to available when launching from AI control blocked state"
  );

  Assert.ok(launchStub.calledOnce, "launchWindow should be called");

  Assert.deepEqual(
    launchStub.firstCall.args.slice(1),
    [false, "bedrock"],
    "launchWindow should be called with the bedrock trigger"
  );

  launchStub.restore();
  await SpecialPowers.popPrefEnv();
});

add_UITour_task(
  async function test_showFirefoxAccountsForAIWindow_entrypoint() {
    if (IS_ESR) {
      Assert.ok(true, "Smart Window is not available on ESR, skipping.");
      return;
    }

    let launchStub = sinon.stub(AIWindow, "launchWindow");
    launchStub.resolves(true);

    gContentAPI.showFirefoxAccountsForAIWindow("custom_entrypoint");

    await TestUtils.waitForCondition(
      () => launchStub.callCount > 0,
      "Waiting for launchWindow to be called"
    );

    Assert.deepEqual(
      launchStub.firstCall.args.slice(1),
      [false, "bedrock-custom_entrypoint"],
      "launchWindow should be called with the bedrock-prefixed entrypoint as trigger"
    );

    launchStub.restore();
  }
);

add_UITour_task(async function test_showFirefoxAccountsForAIWindow_esr() {
  if (!IS_ESR) {
    Assert.ok(true, "Not an ESR build, skipping.");
    return;
  }

  let launchStub = sinon.stub(AIWindow, "launchWindow");
  launchStub.resolves(true);

  gContentAPI.showFirefoxAccountsForAIWindow();

  // Round-trip another UITour message to make sure the one above was handled.
  await new Promise(resolve =>
    gContentAPI.getConfiguration("appinfo", resolve)
  );

  Assert.ok(launchStub.notCalled, "launchWindow should not be called on ESR");

  launchStub.restore();
});
