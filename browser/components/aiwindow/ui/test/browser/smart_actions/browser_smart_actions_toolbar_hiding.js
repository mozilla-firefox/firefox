/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SmartActionsToolbar } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartActionsToolbar.sys.mjs"
);

let win;
let tab;
let mockEngine;

add_setup(async function () {
  ({ win, tab, mockEngine } = await openToolbarWindow());
});

describe("Smart actions toolbar hiding", () => {
  let sandbox;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    await loadInTab(tab, newPageUrl());
    await respondWithStarter(win, mockEngine);
  });

  afterEach(async () => {
    sandbox.restore();
    await mockEngine.rejectAllRequests();
  });

  it("hides in a Classic window", async () => {
    AIWindow.toggleAIWindow(win, false);
    Assert.ok(!isToolbarShown(win), "Hidden after switching to Classic");
    const update = sandbox.spy(SmartActionsToolbar, "update");
    await loadInTab(tab, OTHER_URL);
    Assert.ok(!isToolbarShown(win), "Stays hidden on another page");
    Assert.ok(update.notCalled, "Classic windows don't follow navigation");

    AIWindow.toggleAIWindow(win, true);
    await respondWithStarter(win, mockEngine);
  });

  it("hides on the full-page assistant", async () => {
    await loadInTab(tab, AIWINDOW_URL);
    Assert.ok(!isToolbarShown(win), "Hidden on the full-page assistant");
  });

  it("hides without Terms of Use consent", async () => {
    await SpecialPowers.pushPrefEnv({ clear: [[CONSENT_PREF]] });
    await loadInTab(tab, OTHER_URL);
    Assert.ok(!isToolbarShown(win), "Hidden without consent");
    await SpecialPowers.popPrefEnv();
  });

  it("hides while its pref is off", async () => {
    await SpecialPowers.pushPrefEnv({ set: [[TOOLBAR_PREF, false]] });
    Assert.ok(!isToolbarShown(win), "Hidden when the pref is turned off");
    const update = sandbox.spy(SmartActionsToolbar, "update");
    await loadInTab(tab, OTHER_URL);
    Assert.ok(update.notCalled, "Doesn't follow navigation while off");
    await SpecialPowers.popPrefEnv();
    await respondWithStarter(win, mockEngine);
  });

  it("hides while Smart Window is disabled", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", false]],
    });
    Assert.ok(!isToolbarShown(win), "Hidden when Smart Window is disabled");
    await SpecialPowers.popPrefEnv();
    // Disabling switched the window to Classic.
    AIWindow.toggleAIWindow(win, true);
    await respondWithStarter(win, mockEngine);
  });
});
