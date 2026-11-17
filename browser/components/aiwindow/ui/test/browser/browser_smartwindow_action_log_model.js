/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChatStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);
const { AUTO_MODEL_CHOICE_ID } = ChromeUtils.importESModule(
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

const MODEL_ONLY_ACTION_LOG = [
  {
    label: { id: "action-log-completed-steps", args: { count: 1 } },
    steps: [{ id: "action-log-picked-model", args: { model: "Routed Model" } }],
  },
];

/**
 * Asserts which pending indicator shows before the model answers.
 *
 * @param {MozBrowser} browser
 * @param {boolean} isDecidingModel - Whether the "Deciding best model" action
 *   log shows instead of the loader
 */
async function assertPendingIndicator(browser, isDecidingModel) {
  await SpecialPowers.spawn(
    await getAIChatBrowser(browser),
    [isDecidingModel],
    async expectDeciding => {
      const { shadowRoot } = content.document.querySelector("ai-chat-content");
      await ContentTaskUtils.waitForCondition(
        () =>
          !!shadowRoot.querySelector("chat-assistant-loader") !==
          expectDeciding,
        "The pending indicator shows"
      );
      const actionLog = [...shadowRoot.querySelectorAll("ai-action-result")].at(
        -1
      );
      Assert.equal(
        actionLog?.wrappedJSObject.labelL10nId === "action-log-deciding-model",
        expectDeciding,
        "The action log shows only while Auto picks the model"
      );
    }
  );
}

add_task(async function test_auto_reply_without_tools_lists_model() {
  let convId;
  try {
    await withModelChoiceWindow(
      { modelChoice: AUTO_MODEL_CHOICE_ID, autoRecord: ROUTED_AUTO_RS_RECORD },
      async ({ win, browser, engineManager }) => {
        Services.fog.testResetFOG();
        await typeInSmartbar(browser, "hello");
        await submitSmartbar(browser);
        const { respond } = await engineManager.captureRequest({
          purpose: "chat",
        });
        await assertPendingIndicator(browser, true);
        respond("Hello.");

        Assert.deepEqual(
          await getActionLogs(browser, "Hello."),
          MODEL_ONLY_ACTION_LOG,
          "The action log lists the model Auto picked"
        );
        Assert.equal(
          Glean.smartWindow.timeToFirstActionLog.testGetValue(),
          undefined,
          "The model step alone does not record action-log latency"
        );

        convId = await getConversationId(browser);
        let saved;
        await TestUtils.waitForCondition(async () => {
          saved = await ChatStore.findConversationById(convId);
          return saved?.messages.some(m => m.content.body === "Hello.");
        }, "The reply is saved");

        // The stored reply, not the current choice, decides the action log.
        Services.prefs.setStringPref(
          "browser.smartwindow.firstrun.modelChoice",
          "3"
        );
        const tab = await BrowserTestUtils.openNewForegroundTab(
          win.gBrowser,
          AIWINDOW_URL
        );
        const chatBrowser = await getAIChatBrowser(tab.linkedBrowser);
        await TestUtils.waitForCondition(
          () => chatBrowser.currentURI.spec === "about:aichatcontent",
          "The new tab’s chat content loads"
        );
        tab.linkedBrowser.contentDocument
          .querySelector("ai-window")
          .openConversation(saved);
        Assert.deepEqual(
          await getActionLogs(tab.linkedBrowser, "Hello."),
          MODEL_ONLY_ACTION_LOG,
          "A reopened chat still lists the model Auto picked"
        );
      }
    );
  } finally {
    if (convId) {
      await ChatStore.deleteConversationById(convId);
    }
  }
});

add_task(async function test_auto_deciding_ends_with_hidden_tool_call() {
  await withModelChoiceWindow(
    { modelChoice: AUTO_MODEL_CHOICE_ID, autoRecord: ROUTED_AUTO_RS_RECORD },
    async ({ browser, engineManager }) => {
      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      await engineManager.respondToWithToolCall({
        toolName: "get_skill",
        toolParams: { name: "missing" },
      });
      const { respond } = await engineManager.captureRequest({
        purpose: "chat",
      });
      await assertPendingIndicator(browser, false);
      respond("Hello.");
      Assert.deepEqual(
        await getActionLogs(browser, "Hello."),
        MODEL_ONLY_ACTION_LOG,
        "The reply still lists the model Auto picked"
      );
    }
  );
});

add_task(async function test_auto_deciding_survives_dismissed_undo_card() {
  await withModelChoiceWindow(
    { modelChoice: AUTO_MODEL_CHOICE_ID, autoRecord: ROUTED_AUTO_RS_RECORD },
    async ({ win, browser, engineManager }) => {
      await typeInSmartbar(browser, "close my tabs");
      await submitSmartbar(browser);
      await engineManager.respondTo({ purpose: "chat", response: "Closed." });
      await getActionLogs(browser, "Closed.");

      // The next prompt dismisses the Undo card and updates the message.
      const reply = AIWindow.getActiveConversation(win).messages.findLast(
        m => m.content.body === "Closed."
      );
      reply.toolUIData = {
        uiType: "ai-action-result",
        properties: { confirmedData: { operationIds: ["close-1"] } },
      };

      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      const { respond } = await engineManager.captureRequest({
        purpose: "chat",
      });
      await assertPendingIndicator(browser, true);
      respond("Hello.");
    }
  );
});

add_task(async function test_retry_lists_model_of_retried_reply() {
  let convId;
  try {
    await withModelChoiceWindow(
      { modelChoice: "3", autoRecord: ROUTED_AUTO_RS_RECORD },
      async ({ browser, engineManager }) => {
        await typeInSmartbar(browser, "fail this request");
        await submitSmartbar(browser);
        await engineManager.captureRequest({ purpose: "chat" });
        engineManager.engines.get("chat").rejectAllRequests();
        const aiWindow = browser.contentDocument.querySelector("ai-window");
        await TestUtils.waitForCondition(
          () => !aiWindow.isGenerating,
          "The request fails"
        );

        await switchSmartbarModel(browser, AUTO_MODEL_CHOICE_ID);
        aiWindow.handleFooterAction({ action: "retry-after-error" });
        await engineManager.respondTo({
          purpose: "chat",
          response: "Recovered.",
        });
        Assert.deepEqual(
          await getActionLogs(browser, "Recovered."),
          MODEL_ONLY_ACTION_LOG,
          "The retried reply lists the model Auto picked"
        );

        convId = await getConversationId(browser);
        await TestUtils.waitForCondition(async () => {
          const saved = await ChatStore.findConversationById(convId);
          const reply = saved?.messages.find(
            m => m.content.body === "Recovered."
          );
          return reply?.params?.modelChoiceId === AUTO_MODEL_CHOICE_ID;
        }, "The retried reply is saved with the model choice that answered");
      }
    );
  } finally {
    if (convId) {
      await ChatStore.deleteConversationById(convId);
    }
  }
});

add_task(async function test_preset_reply_without_tools_has_no_action_log() {
  await withModelChoiceWindow(
    { modelChoice: "3" },
    async ({ browser, engineManager }) => {
      await typeInSmartbar(browser, "hello");
      await submitSmartbar(browser);
      const { respond } = await engineManager.captureRequest({
        purpose: "chat",
      });
      await assertPendingIndicator(browser, false);
      respond("Hello.");
      Assert.deepEqual(
        await getActionLogs(browser, "Hello."),
        [],
        "A preset reply has no action log"
      );
    }
  );
});
