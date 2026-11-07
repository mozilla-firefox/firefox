/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head.js */

const { PromiseTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromiseTestUtils.sys.mjs"
);

// Tests using this helper deliberately reject openAIEngine.build for several
// error-path subtests. Background ML work scheduled during AI window setup
// (e.g. memories) can race with stub install/restore and surface its own
// Connection error from the OpenAI pipeline — orthogonal to what the
// assertions exercise.
PromiseTestUtils.allowMatchingRejectionsGlobally(/Connection error/);

// Test cases "separate conversations have isolated IDs" and
// "createEngine error path receives flowId matching chat_id" might stop
// inflight requests and lead to 400 errors.
PromiseTestUtils.allowMatchingRejectionsGlobally(/400 Bad request/);

const requestResponseLazy = {};
ChromeUtils.defineESModuleGetters(requestResponseLazy, {
  IntentClassifier:
    "moz-src:///browser/components/aiwindow/models/IntentClassifier.sys.mjs",
});

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);

let win;
let sb;
let mockEngineManager;

function setupRequestResponseTelemetry() {
  beforeEach(async () => {
    sb = sinon.createSandbox();
    sb.stub(requestResponseLazy.IntentClassifier, "getPromptIntent").resolves(
      "chat"
    );
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.firstrun.modelChoice", "0"],
        ["browser.smartwindow.customEndpoint", "http://localhost:0/v1"],
      ],
    });
    Services.fog.testResetFOG();
  });

  afterEach(async () => {
    try {
      if (win) {
        await BrowserTestUtils.closeWindow(win);
      }
    } finally {
      try {
        await mockEngineManager?.rejectAllRequests();
      } finally {
        mockEngineManager?.cleanupMocks();
        mockEngineManager = null;
        win = null;
        sb.restore();
        await SpecialPowers.popPrefEnv();
      }
    }
  });
}
