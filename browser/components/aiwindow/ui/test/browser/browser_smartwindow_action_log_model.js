/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AUTO_MODEL_CHOICE_ID, FALLBACK_MODELS } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

/**
 * Waits for the reply and returns the label and steps of each action log.
 *
 * @param {MozBrowser} browser
 * @param {string} reply - Text of the assistant reply
 * @returns {Promise<Array<{label: object, steps: Array<object>}>>}
 */
async function getActionLogs(browser, reply) {
  return SpecialPowers.spawn(
    await getAIChatBrowser(browser),
    [reply],
    async expectedReply => {
      const chatContent = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForCondition(
        () =>
          Array.from(
            chatContent.shadowRoot.querySelectorAll("ai-chat-message")
          ).some(el => el.wrappedJSObject.message === expectedReply),
        "The reply is rendered"
      );
      const actionLogs = Array.from(
        chatContent.shadowRoot.querySelectorAll("ai-action-result")
      );
      await ContentTaskUtils.waitForCondition(
        () => actionLogs.every(log => !log.hasAttribute("shimmering")),
        "The action logs are complete"
      );

      const toL10n = el => ({
        id: el.getAttribute("data-l10n-id"),
        args: JSON.parse(el.getAttribute("data-l10n-args")),
      });
      const result = [];
      for (const log of actionLogs) {
        log.shadowRoot.querySelector(".action-result-header").click();
        await ContentTaskUtils.waitForCondition(() =>
          log.shadowRoot.querySelector(".action-result-expanded")
        );
        result.push({
          label: toL10n(log.shadowRoot.querySelector(".action-result-label")),
          steps: Array.from(
            log.shadowRoot.querySelectorAll(
              ".action-result-expanded-row-label"
            ),
            toL10n
          ),
        });
      }
      return result;
    }
  );
}

// Routes the auto model to a model no other choice uses, so its name can only
// come from the auto model’s record.
const ROUTED_AUTO_RS_RECORD = {
  ...MOCK_AUTO_RS_RECORD,
  inference_model: "routed-model",
  model_details: {
    ...MOCK_AUTO_RS_RECORD.model_details,
    model: "routed-model",
    shortName: "Routed Model",
  },
};

/**
 * Runs a chat turn with a tool call and returns the steps of its action log.
 *
 * @param {string} modelChoice - Default model choice
 * @param {string} [tabModelChoice] - Model choice picked in the smartbar
 * @returns {Promise<Array<{id: string, args: ?object}>>}
 */
async function getToolTurnSteps(modelChoice, tabModelChoice) {
  let steps;
  await withModelChoiceWindow(
    { modelChoice, autoRecord: ROUTED_AUTO_RS_RECORD },
    async ({ browser, engineManager }) => {
      if (tabModelChoice) {
        await switchSmartbarModel(browser, tabModelChoice);
      }
      Services.fog.testResetFOG();
      await typeInSmartbar(browser, "Which tabs are open?");
      await submitSmartbar(browser);
      await engineManager.respondToWithToolCall({
        toolName: "get_open_tabs",
        toolParams: {},
      });
      await engineManager.respondTo({ purpose: "chat", response: "Done." });
      [{ steps }] = await getActionLogs(browser, "Done.");
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.timeToFirstActionLog.testGetValue()?.length,
        "The action log records its latency"
      );
      Assert.equal(
        Glean.smartWindow.timeToFirstActionLog.testGetValue().length,
        1,
        "The turn records its action log latency once"
      );
    }
  );
  return steps;
}

add_task(async function test_action_log_names_model_auto_picked() {
  for (const [label, modelChoice, tabModelChoice] of [
    ["Auto by default", AUTO_MODEL_CHOICE_ID],
    ["Auto in the smartbar", "3", AUTO_MODEL_CHOICE_ID],
  ]) {
    const steps = await getToolTurnSteps(modelChoice, tabModelChoice);
    Assert.deepEqual(
      steps.map(({ id }) => id),
      ["action-log-picked-model", "action-log-searched-open-tabs"],
      `${label}: the model is the first step`
    );
    Assert.deepEqual(
      steps[0].args,
      { model: "Routed Model" },
      `${label}: the step names the model Auto picked`
    );
  }
});

add_task(async function test_action_log_hides_model_without_auto() {
  for (const [label, modelChoice, tabModelChoice] of [
    ["Preset by default", "3"],
    ["Preset in the smartbar", AUTO_MODEL_CHOICE_ID, "3"],
  ]) {
    Assert.deepEqual(
      (await getToolTurnSteps(modelChoice, tabModelChoice)).map(({ id }) => id),
      ["action-log-searched-open-tabs"],
      `${label}: the model is not a step`
    );
  }
});
