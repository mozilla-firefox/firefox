/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await pushStarterPrefs();
  win = await openSharedAIWindow();
});

describe("Full page starter prompts", () => {
  let sandbox;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    await SpecialPowers.pushPrefEnv({ set: MEMORIES_ON_PREFS });
  });

  afterEach(async () => {
    sandbox.restore();
    await SpecialPowers.popPrefEnv();
  });

  it("render while the resume cards are pending", async () => {
    // The resume gate refreshes memories first. Leave that pending so the
    // cards can't load; the starters must not wait on it.
    const getAllMemories = sandbox
      .stub(MemoriesManager, "getAllMemories")
      .returns(new Promise(() => {}));
    const { aiWindow, buttons } = await openStartersPage(win);

    await TestUtils.waitForCondition(
      () => getAllMemories.called,
      "Wait for the resume gate to start refreshing memories"
    );
    Assert.equal(
      buttons.length,
      3,
      "Starters should render while the resume cards are pending"
    );
    Assert.deepEqual(aiWindow.resumeCards, [], "No resume cards have loaded");
  });
});
