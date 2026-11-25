/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let win;

add_setup(async function () {
  await pushStarterPrefs();
  win = await openSharedAIWindow();
});

describe("Clicking a starter prompt", () => {
  let sandbox;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
  });

  afterEach(() => {
    sandbox.restore();
  });

  describe("on a new Smart Window page", () => {
    let fetchWithHistory;

    beforeEach(() => {
      fetchWithHistory = sandbox.stub(Chat, "fetchWithHistory").resolves();
      sandbox.stub(openAIEngine, "build").resolves({});
    });

    it("submits the prompt's text as the user message", async () => {
      const { aiWindow, buttons } = await openStartersPage(win);
      const promptText = buttons[0].ariaLabel;

      buttons[0].click();

      await TestUtils.waitForCondition(
        () => pageTurns(fetchWithHistory, aiWindow).length === 1,
        "fetchWithHistory should be called after clicking prompt"
      );
      const userMessage = pageTurns(fetchWithHistory, aiWindow)[0]
        .args[0].conversation.getMessagesInChatCompletionsFormat()
        .findLast(message => message.role === "user");
      Assert.equal(
        userMessage.content,
        promptText,
        "Should submit starter prompt text as user message on New Tab"
      );
    });

    it("hides the starter prompts", async () => {
      const { aiWindow, buttons } = await openStartersPage(win);

      buttons[0].click();

      await BrowserTestUtils.waitForMutationCondition(
        aiWindow.shadowRoot,
        { childList: true, subtree: true },
        () => !aiWindow.shadowRoot.querySelector("smartwindow-prompts")
      );
    });

    it("skips memories while they are off", async () => {
      const injectMemories = sandbox
        .stub(ChatConversation.prototype, "injectMemoriesContext")
        .resolves(null);
      const { aiWindow, buttons } = await openStartersPage(win);

      buttons[0].click();

      await TestUtils.waitForCondition(
        () => pageTurns(fetchWithHistory, aiWindow).length === 1,
        "fetchWithHistory should be called after clicking prompt"
      );
      Assert.ok(
        injectMemories.notCalled,
        "injectMemoriesContext should not be called when memories are disabled"
      );
    });

    describe("with memories on", () => {
      beforeEach(async () => {
        await SpecialPowers.pushPrefEnv({ set: MEMORIES_ON_PREFS });
      });

      afterEach(async () => {
        await SpecialPowers.popPrefEnv();
      });

      it("fetches memories once", async () => {
        const injectMemories = sandbox
          .stub(ChatConversation.prototype, "injectMemoriesContext")
          .resolves(null);
        const { buttons } = await openStartersPage(win);

        buttons[0].click();

        await TestUtils.waitForCondition(
          () => injectMemories.called,
          "injectMemoriesContext should be called with memories enabled"
        );
        Assert.ok(
          injectMemories.calledOnce,
          "injectMemoriesContext should be called once"
        );
      });
    });
  });
});

/**
 * The previous test's page can still start its chat turn after the test ends,
 * so only count the turns started from the page under test.
 *
 * @param {sinon.SinonStub} fetchWithHistory
 * @param {Element} aiWindow
 * @returns {sinon.SinonSpyCall[]}
 */
function pageTurns(fetchWithHistory, aiWindow) {
  return fetchWithHistory
    .getCalls()
    .filter(call => call.args[0].conversation.id === aiWindow.conversationId);
}
