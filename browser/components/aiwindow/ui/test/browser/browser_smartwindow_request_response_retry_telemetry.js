/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartwindow_request_response_telemetry.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) +
    "head_smartwindow_request_response_telemetry.js",
  this
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  MESSAGE_ROLE:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatEnums.sys.mjs",
});

describe("SmartWindowRequestResponseTelemetry", () => {
  setupRequestResponseTelemetry();

  it("records is_retry true on a retried turn and false on the initial turn", async () => {
    mockEngineManager = new MockEngineManager();
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "first attempt");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Reply from mock.",
    });
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 1,
      "Wait for initial model_response"
    );

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const lastAssistant = aiWindow.conversation.messages.findLast(
      m => m.role === lazy.MESSAGE_ROLE.ASSISTANT
    );
    Assert.ok(lastAssistant, "Have an assistant message to retry from");

    aiWindow.handleFooterAction({
      action: "retry",
      messageId: lastAssistant.id,
    });

    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Reply from mock.",
    });
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 2,
      "Wait for retry model_response"
    );

    const responses = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(responses.length, 2, "Two model_response events recorded");
    Assert.equal(
      responses[0].extra.is_retry,
      "false",
      "Initial turn: is_retry is false"
    );
    Assert.equal(responses[0].extra.error, "", "Initial turn: no error");
    Assert.equal(
      responses[1].extra.is_retry,
      "true",
      "Retried turn: is_retry is true"
    );
    Assert.equal(responses[1].extra.error, "", "Retried turn: no error");
  });

  it("records retry orchestration failure in model_response with is_retry true", async () => {
    mockEngineManager = new MockEngineManager();
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "first attempt");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Reply from mock.",
    });
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 1,
      "Wait for initial model_response"
    );

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const lastAssistant = aiWindow.conversation.messages.findLast(
      m => m.role === lazy.MESSAGE_ROLE.ASSISTANT
    );

    // Force the retry orchestration to throw before #fetchAIResponse
    // takes over, so the catch in #retryFromAssistantMessageId is the
    // only path that can record telemetry for this attempt.
    const orchestrationError = new Error("boom");
    sb.stub(aiWindow.conversation, "retryMessage").rejects(orchestrationError);

    aiWindow.handleFooterAction({
      action: "retry",
      messageId: lastAssistant.id,
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 2,
      "Wait for retry-failure model_response"
    );

    const responses = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(responses.length, 2, "Two model_response events recorded");
    Assert.equal(
      responses[1].extra.is_retry,
      "true",
      "Orchestration failure: is_retry is true"
    );
    Assert.equal(
      responses[1].extra.error,
      "retryOrchestrationFailure",
      "Orchestration failure: error is retryOrchestrationFailure"
    );
  });

  it("preserves retryInvalidMessage clientReason when retryMessage rejects with one", async () => {
    mockEngineManager = new MockEngineManager();
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    await typeInSmartbar(browser, "first attempt");
    await submitSmartbar(browser);
    await mockEngineManager.respondTo({
      purpose: "chat",
      response: "Reply from mock.",
    });
    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 1,
      "Wait for initial model_response"
    );

    const aiWindow = browser.contentDocument.querySelector("ai-window");
    const lastAssistant = aiWindow.conversation.messages.findLast(
      m => m.role === lazy.MESSAGE_ROLE.ASSISTANT
    );

    const taggedError = new Error("unrelated");
    taggedError.clientReason = "retryInvalidMessage";
    sb.stub(aiWindow.conversation, "retryMessage").rejects(taggedError);

    aiWindow.handleFooterAction({
      action: "retry",
      messageId: lastAssistant.id,
    });

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length >= 2,
      "Wait for retry-invalid model_response"
    );

    const responses = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(
      responses[1].extra.error,
      "retryInvalidMessage",
      "Existing clientReason on the rejection is preserved"
    );
    Assert.equal(
      responses[1].extra.is_retry,
      "true",
      "Existing clientReason path still marks is_retry=true"
    );
  });
});
