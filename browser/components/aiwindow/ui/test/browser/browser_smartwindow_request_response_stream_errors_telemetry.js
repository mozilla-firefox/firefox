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

  function makeFakeEngine({ runWithGenerator } = {}) {
    return {
      feature: "chat",
      model: "custom-model",
      runWithGenerator: runWithGenerator ?? async function* () {},
    };
  }

  it("records fxaTokenUnavailable when FxA token is missing", async () => {
    sb.stub(openAIEngine, "build").resolves(makeFakeEngine());
    sb.stub(AuthProvider, "getToken").resolves(null);

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await typeInSmartbar(browser, "trigger fxa error");
    await submitSmartbar(browser);

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
      "Wait for model_response event with FxA error"
    );

    const events = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(events.length, 1, "One model_response event was recorded");
    Assert.equal(
      events[0].extra.error,
      "fxaTokenUnavailable",
      "model_response: error is fxaTokenUnavailable"
    );
    Assert.equal(
      Number(events[0].extra.http_status),
      0,
      "model_response: http_status is 0"
    );
  });

  it("records connectionFailure when streaming fails with no status or error code", async () => {
    sb.stub(openAIEngine, "build").resolves(
      makeFakeEngine({
        // eslint-disable-next-line require-yield
        async *runWithGenerator() {
          throw new Error("network request failed");
        },
      })
    );
    sb.stub(AuthProvider, "getToken").resolves("mock-fxa-token");

    const originalOffline = Services.io.offline;
    Services.io.offline = false;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await typeInSmartbar(browser, "trigger connection failure");
      await submitSmartbar(browser);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
        "Wait for model_response event with connectionFailure"
      );

      const events = Glean.smartWindow.modelResponse.testGetValue();
      Assert.equal(events.length, 1, "One model_response event was recorded");
      Assert.equal(
        events[0].extra.error,
        "connectionFailure",
        "model_response: error is connectionFailure"
      );
      Assert.equal(
        Number(events[0].extra.http_status),
        0,
        "model_response: http_status is 0"
      );
    } finally {
      Services.io.offline = originalOffline;
    }
  });

  it("records offline when streaming fails while the browser is offline", async () => {
    sb.stub(openAIEngine, "build").resolves(
      makeFakeEngine({
        // eslint-disable-next-line require-yield
        async *runWithGenerator() {
          throw new Error("network request failed");
        },
      })
    );
    sb.stub(AuthProvider, "getToken").resolves("mock-fxa-token");

    const originalOffline = Services.io.offline;
    Services.io.offline = true;
    try {
      win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;
      await typeInSmartbar(browser, "trigger offline error");
      await submitSmartbar(browser);

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
        "Wait for model_response event with offline"
      );

      const events = Glean.smartWindow.modelResponse.testGetValue();
      Assert.equal(events.length, 1, "One model_response event was recorded");
      Assert.equal(
        events[0].extra.error,
        "offline",
        "model_response: error is offline"
      );
      Assert.equal(
        Number(events[0].extra.http_status),
        0,
        "model_response: http_status is 0"
      );
    } finally {
      Services.io.offline = originalOffline;
    }
  });

  it("records fastlyBlocked and http_status 406 on streaming 406", async () => {
    sb.stub(openAIEngine, "build").resolves(
      makeFakeEngine({
        // eslint-disable-next-line require-yield
        async *runWithGenerator() {
          const err = new Error("test 406 from upstream");
          err.status = 406;
          throw err;
        },
      })
    );
    sb.stub(AuthProvider, "getToken").resolves("mock-fxa-token");

    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    await typeInSmartbar(browser, "trigger 406");
    await submitSmartbar(browser);

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.modelResponse.testGetValue()?.length > 0,
      "Wait for model_response event with 406 error"
    );

    const events = Glean.smartWindow.modelResponse.testGetValue();
    Assert.equal(events.length, 1, "One model_response event was recorded");
    Assert.equal(
      events[0].extra.error,
      "fastlyBlocked",
      "model_response: error is fastlyBlocked"
    );
    Assert.equal(
      Number(events[0].extra.http_status),
      406,
      "model_response: http_status is 406"
    );
  });
});
