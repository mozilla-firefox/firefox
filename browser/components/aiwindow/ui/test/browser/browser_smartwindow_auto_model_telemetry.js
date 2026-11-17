/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { _clearModelsDataCacheForTesting, FALLBACK_MODELS } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
  );
const { SmartWindowTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs"
);

const ROUTED_MODEL = MOCK_AUTO_RS_RECORD.inference_model;
const PRESET_MODEL = FALLBACK_MODELS[3].model;

// Events about the user's model choice.
const CHOICE_EVENTS = ["chatSubmit", "modelResponse", "clientError"];
// Events about a model request.
const REQUEST_EVENTS = [
  "toolCall",
  "timeToFirstActionLog",
  "timeToFirstCharacter",
  "timeToTurnComplete",
];

/**
 * Runs one chat turn with a tool call, raises a client error in the Smart Window,
 * and returns the `model` extra of the first event of each kind.
 *
 * @param {string} modelChoice - Default model choice
 * @param {string} [tabModelChoice] - Model choice picked for the tab in the
 *   smartbar
 * @returns {Promise<{[event: string]: string}>}
 */
async function runTurnAndCollectModels(modelChoice, tabModelChoice) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.modelChoice", modelChoice],
      ["browser.smartwindow.firstrun.singlePageWithAutoModel", true],
    ],
  });
  const engineManager = new MockEngineManager();
  let win;
  try {
    _setRemoteClientForTesting({
      get: async () => [...MOCK_RS_RECORDS, MOCK_AUTO_RS_RECORD],
    });
    _clearModelsDataCacheForTesting();
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    if (tabModelChoice) {
      await switchSmartbarModel(browser, tabModelChoice);
    }
    Services.fog.testResetFOG();
    SmartWindowTelemetry._resetClientErrorDedupForTests();

    await typeInSmartbar(browser, "testing auto model telemetry");
    await submitSmartbar(browser);
    await engineManager.respondToWithToolCall({
      toolName: "get_open_tabs",
      toolParams: {},
    });
    await engineManager.respondTo({ purpose: "chat", response: "Done." });

    const view = browser.contentDocument.defaultView;
    view.dispatchEvent(
      new view.ErrorEvent("error", {
        error: new view.TypeError("auto model test"),
        message: "auto model test",
      })
    );
    const events = [...CHOICE_EVENTS, ...REQUEST_EVENTS];
    await TestUtils.waitForCondition(
      () =>
        events.every(name => Glean.smartWindow[name].testGetValue()?.length),
      "Wait for every event to be recorded"
    );

    return Object.fromEntries(
      events.map(name => [
        name,
        Glean.smartWindow[name].testGetValue()[0].extra.model,
      ])
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
    await engineManager.rejectAllRequests();
    engineManager.cleanupMocks();
    _setRemoteClientForTesting({ get: async () => MOCK_RS_RECORDS });
    _clearModelsDataCacheForTesting();
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async function test_auto_model_reports_choice_and_routed_model() {
  const models = await runTurnAndCollectModels("4");
  Assert.deepEqual(
    models,
    Object.fromEntries([
      ...CHOICE_EVENTS.map(name => [name, "auto"]),
      ...REQUEST_EVENTS.map(name => [name, ROUTED_MODEL]),
    ]),
    "Choice events report Auto, request events the model Auto picked"
  );
});

add_task(async function test_preset_model_reported_everywhere() {
  const models = await runTurnAndCollectModels("3");
  Assert.deepEqual(
    models,
    Object.fromEntries(
      [...CHOICE_EVENTS, ...REQUEST_EVENTS].map(name => [name, PRESET_MODEL])
    ),
    "Every event reports the preset model"
  );
});

add_task(async function test_tab_auto_model_reported_over_default() {
  const models = await runTurnAndCollectModels("3", "4");
  Assert.deepEqual(
    models,
    Object.fromEntries([
      ...CHOICE_EVENTS.map(name => [name, "auto"]),
      ...REQUEST_EVENTS.map(name => [name, ROUTED_MODEL]),
    ]),
    "A tab's Auto choice is reported instead of the default model"
  );
});
