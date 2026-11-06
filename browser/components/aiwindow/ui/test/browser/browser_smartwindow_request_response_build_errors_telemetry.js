/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartwindow_request_response_telemetry.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) +
    "head_smartwindow_request_response_telemetry.js",
  this
);

describe("SmartWindowRequestResponseTelemetry", () => {
  setupRequestResponseTelemetry();

  it("createEngine error path receives flowId matching chat_id", async () => {
    const createError = new Error("engine process unavailable");
    createError.error = 1;
    const createEngineSpy = sb
      .stub(openAIEngine, "_createEngine")
      .rejects(createError);

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await typeInSmartbar(browser, "trigger engine error");
    await submitSmartbar(browser);

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
      "Wait for model_response event with error"
    );

    const responseEvents = Glean.smartWindow.modelResponse.testGetValue();
    const chatId = responseEvents[0].extra.chat_id;
    Assert.ok(chatId, "Error response has a non-empty chat_id");

    const chatCreateCalls = createEngineSpy
      .getCalls()
      .filter(call => call.args[0]?.featureId === "chat");
    Assert.greaterOrEqual(
      chatCreateCalls.length,
      1,
      "At least one chat createEngine call"
    );
    Assert.equal(
      chatCreateCalls[0].args[0].flowId,
      chatId,
      "createEngine received flowId matching chat_id"
    );
  });

  it("records model_response with error when build fails", async () => {
    const error = new Error("test error");
    error.error = 1;
    sb.stub(openAIEngine, "build").rejects(error);

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await typeInSmartbar(browser, "trigger error");
    await submitSmartbar(browser);

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
      "Wait for model_response event with error"
    );

    const requestEvents = Glean.smartWindow.modelRequest.testGetValue();
    Assert.equal(
      requestEvents,
      undefined,
      "No model_request event when build fails"
    );

    const responseEvents = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(
      responseEvents?.length,
      1,
      "One model_response event was recorded"
    );
    Assert.equal(
      responseEvents[0].extra.location,
      "home",
      "model_response: location is home"
    );
    Assert.equal(
      responseEvents[0].extra.model,
      "custom-model",
      "model_response: model is custom-model"
    );
    Assert.equal(
      responseEvents[0].extra.message_seq,
      0,
      "model_response: message_seq is 0"
    );
    Assert.equal(
      responseEvents[0].extra.memories,
      0,
      "model_response: memories is 0"
    );
    Assert.equal(
      responseEvents[0].extra.intent,
      "chat",
      "model_response: intent is chat"
    );
    Assert.ok(
      "tokens" in responseEvents[0].extra,
      "model_response: tokens exists"
    );
    Assert.ok(
      "duration" in responseEvents[0].extra,
      "model_response: duration exists"
    );
    Assert.ok(
      "latency" in responseEvents[0].extra,
      "model_response: latency exists"
    );
    Assert.ok(
      "chat_id" in responseEvents[0].extra,
      "model_response: chat_id exists"
    );
    Assert.equal(
      responseEvents[0].extra.error,
      "budgetExceeded",
      "model_response: error is budgetExceeded"
    );
    Assert.equal(
      Number(responseEvents[0].extra.http_status),
      0,
      "model_response: http_status is 0"
    );
  });

  const ERROR_TELEMETRY_CASES = [
    {
      errorProps: { error: 5, status: 429 },
      expectedName: "upstreamRateLimit",
      expectedHttpStatus: 429,
    },
    {
      errorProps: { error: 6, status: 429 },
      expectedName: "fastlyWafRateLimit",
      expectedHttpStatus: 429,
    },
    {
      errorProps: { error: 4, status: 403 },
      expectedName: "maxUsersReached",
      expectedHttpStatus: 403,
    },
    {
      errorProps: { clientReason: "fxaTokenUnavailable" },
      expectedName: "fxaTokenUnavailable",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "remoteSettingsUnavailable" },
      expectedName: "remoteSettingsUnavailable",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "modelConfigUnavailable" },
      expectedName: "modelConfigUnavailable",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "promptLoadFailure" },
      expectedName: "promptLoadFailure",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "missingBrowsingContext" },
      expectedName: "missingBrowsingContext",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "offline" },
      expectedName: "offline",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { clientReason: "connectionFailure" },
      expectedName: "connectionFailure",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { status: 401 },
      expectedName: "serverError",
      expectedHttpStatus: 401,
    },
    {
      errorProps: { error: 99, status: 500 },
      expectedName: "serverError",
      expectedHttpStatus: 500,
    },
    {
      errorProps: { status: 502 },
      expectedName: "serverError",
      expectedHttpStatus: 502,
    },
    {
      errorProps: {},
      expectedName: "Error",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { name: "TypeError" },
      expectedName: "TypeError",
      expectedHttpStatus: 0,
    },
    {
      errorProps: { name: "APIConnectionError" },
      expectedName: "APIConnectionError",
      expectedHttpStatus: 0,
    },
  ];

  // Share one AI window across all error cases — a fresh window per case
  // pushed the file past the per-task timeout in test-verify chaos mode.
  it("records expected error name and http_status for build errors", async () => {
    // Stub build *before* opening the window. Background flows scheduled
    // during window startup (e.g. memory-related work) would otherwise reach
    // the real openAIEngine.build and surface as an uncaught Connection
    // error from the ML pipeline. Re-arm the stub per iteration via
    // `.rejects(error)` instead of restoring between cases.
    const buildStub = sb
      .stub(openAIEngine, "build")
      .rejects(new Error("build errors test setup"));

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    for (const {
      errorProps,
      expectedName,
      expectedHttpStatus,
    } of ERROR_TELEMETRY_CASES) {
      info(`Error case: ${JSON.stringify(errorProps)} -> ${expectedName}`);
      Services.fog.testResetFOG();

      const error = new Error("test error");
      Object.assign(error, errorProps);
      buildStub.rejects(error);

      await typeInSmartbar(browser, "trigger error");
      await submitSmartbar(browser);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
        `Wait for model_response event for ${expectedName}`
      );

      const events = Glean.smartWindow.modelResponse.testGetValue();
      Assert.equal(
        events.length,
        1,
        `${expectedName}: one model_response event was recorded`
      );
      Assert.equal(
        events[0].extra.error,
        expectedName,
        `${expectedName}: model_response.error matches`
      );
      Assert.equal(
        Number(events[0].extra.http_status),
        expectedHttpStatus,
        `${expectedName}: model_response.http_status is ${expectedHttpStatus}`
      );
    }
  });
});
