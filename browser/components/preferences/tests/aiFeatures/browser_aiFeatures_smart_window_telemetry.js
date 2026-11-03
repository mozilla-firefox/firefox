/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_smart_window.js", gTestPath).href,
  this
);

const { SmartWindowTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs"
);

describe("Smart Window telemetry", () => {
  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.preferences.aiControls", true],
        ["browser.smartwindow.enabled", true],
        ["browser.smartwindow.tos.consentTime", 1770830464],
      ],
    });
    Services.fog.testResetFOG();
    SmartWindowTelemetry.init();
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
    Services.prefs.clearUserPref("browser.smartwindow.firstrun.modelChoice");
    Services.prefs.clearUserPref("browser.smartwindow.model");
    Services.prefs.clearUserPref("browser.smartwindow.customEndpoint");
    Services.prefs.clearUserPref("browser.smartwindow.endpoint");
    Services.prefs.clearUserPref("browser.smartwindow.apiKey");
    Services.fog.testResetFOG();
    await SpecialPowers.popPrefEnv();
  });

  it("sends telemetry when no model was chosen during onboarding but is chosen via settings", async () => {
    const expectedModel = await modelFor("1");
    let { doc } = await openSmartWindowPanel();

    const modelSelection = doc.getElementById("modelSelection");
    Assert.equal(
      modelSelection.value,
      null,
      "There's no model from the onboarding selection"
    );

    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    const prefChanged = TestUtils.waitForPrefChange(
      "browser.smartwindow.firstrun.modelChoice"
    );
    fastRadio.click();
    await prefChanged;

    const settingsModelEvent = Glean.smartWindow.settingsModel.testGetValue();

    Assert.equal(
      settingsModelEvent.length,
      1,
      "One settingsModel event was recorded"
    );

    Assert.equal(
      settingsModelEvent[0].extra.previous_model,
      "No model",
      "No previous model was set"
    );

    Assert.equal(
      settingsModelEvent[0].extra.new_model,
      expectedModel,
      "New model was set properly"
    );

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.model.testGetValue() === expectedModel,
      "Model metric should be updated asynchronously"
    );
    const modelMetric = Glean.smartWindow.model.testGetValue();
    Assert.equal(modelMetric, expectedModel, "Model metric was set properly");
  });

  it("sends telemetry when new model is chosen via settings", async () => {
    const previousModel = await modelFor("2");
    const newModel = await modelFor("1");
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "2"]],
    });

    let { doc } = await openSmartWindowPanel();

    const modelSelection = doc.getElementById("modelSelection");
    Assert.equal(modelSelection.value, 2, "Model 2 was previously selected");

    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    const prefChanged = TestUtils.waitForPrefChange(
      "browser.smartwindow.firstrun.modelChoice"
    );
    fastRadio.click();
    await prefChanged;

    const settingsModelEvent = Glean.smartWindow.settingsModel.testGetValue();

    Assert.equal(
      settingsModelEvent.length,
      1,
      "One settingsModel event was recorded"
    );

    Assert.equal(
      settingsModelEvent[0].extra.previous_model,
      previousModel,
      "Model 2 is previous model"
    );

    Assert.equal(
      settingsModelEvent[0].extra.new_model,
      newModel,
      "Model 1 is new model"
    );

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.model.testGetValue() === newModel,
      "Model metric should be updated asynchronously"
    );
    const modelMetric = Glean.smartWindow.model.testGetValue();
    Assert.equal(modelMetric, newModel, "Model metric was set properly");
  });

  it("sends telemetry when custom model is chosen via settings", async () => {
    const previousModel = await modelFor("2");
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "2"]],
    });

    let { doc } = await openSmartWindowPanel();

    const customRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-custom"]'
    );
    customRadio.click();

    await TestUtils.waitForCondition(
      () => BrowserTestUtils.isVisible(doc.getElementById("customModelName")),
      "Waiting for custom fields to be visible"
    );

    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    customModelEndpoint.value = "https://example.com";
    customModelEndpoint.dispatchEvent(new Event("change", { bubbles: true }));

    const customModelSaveButton = doc.getElementById("customModelSaveButton");
    await TestUtils.waitForCondition(
      () => !customModelSaveButton.disabled,
      "Waiting for save button to be enabled"
    );

    doc.getElementById("customModelName").value = "my-custom-model";
    doc.getElementById("customModelAuthToken").value = "my-token";

    const prefChanged = TestUtils.waitForPrefChange(
      "browser.smartwindow.model"
    );
    customModelSaveButton.scrollIntoView();
    customModelSaveButton.click();
    await prefChanged;

    const settingsModelEvent = Glean.smartWindow.settingsModel.testGetValue();

    Assert.equal(
      settingsModelEvent.length,
      1,
      "One settingsModel event was recorded"
    );

    Assert.equal(
      settingsModelEvent[0].extra.previous_model,
      previousModel,
      "Model 2 is previous model"
    );

    Assert.equal(
      settingsModelEvent[0].extra.new_model,
      "custom-model",
      "Custom model is new model"
    );

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.model.testGetValue() === "custom-model",
      "Model metric should be updated asynchronously"
    );
    const modelMetric = Glean.smartWindow.model.testGetValue();
    Assert.equal(modelMetric, "custom-model", "Model metric was set properly");
  });

  it("sends telemetry when custom model is updated", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "0"]],
    });

    let { doc } = await openSmartWindowPanel();

    await TestUtils.waitForCondition(
      () => BrowserTestUtils.isVisible(doc.getElementById("customModelName")),
      "Waiting for custom fields to be visible"
    );

    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    customModelEndpoint.value = "https://example.com";
    customModelEndpoint.dispatchEvent(new Event("change", { bubbles: true }));

    const customModelSaveButton = doc.getElementById("customModelSaveButton");
    await TestUtils.waitForCondition(
      () => !customModelSaveButton.disabled,
      "Waiting for save button to be enabled"
    );

    doc.getElementById("customModelName").value = "my-custom-model";
    doc.getElementById("customModelAuthToken").value = "my-token";

    const prefChanged = TestUtils.waitForPrefChange(
      "browser.smartwindow.model"
    );
    customModelSaveButton.scrollIntoView();
    customModelSaveButton.click();
    await prefChanged;

    const settingsModelEvent = Glean.smartWindow.settingsModel.testGetValue();

    Assert.equal(
      settingsModelEvent.length,
      1,
      "One settingsModel event was recorded"
    );

    Assert.equal(
      settingsModelEvent[0].extra.previous_model,
      "No model",
      "No previous model was set when panel opened with custom already selected"
    );

    Assert.equal(
      settingsModelEvent[0].extra.new_model,
      "custom-model",
      "Custom model is new model"
    );

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.model.testGetValue() === "custom-model",
      "Model metric should be updated asynchronously"
    );
    const modelMetric = Glean.smartWindow.model.testGetValue();
    Assert.equal(modelMetric, "custom-model", "Model metric was set properly");
  });

  it("sends telemetry when memories chat checkbox is updated", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", false],
        ["browser.smartwindow.memories.generateFromHistory", false],
      ],
    });

    let { doc, win } = await openSmartWindowPanel();

    const chatCheckbox = doc.getElementById("learnFromChatActivity");
    Assert.ok(!chatCheckbox.checked, "Chat checkbox is unchecked initially");

    chatCheckbox.scrollIntoView();
    EventUtils.synthesizeMouseAtCenter(chatCheckbox.labelEl, {}, win);
    await chatCheckbox.updateComplete;

    Assert.ok(
      Services.prefs.getBoolPref(
        "browser.smartwindow.memories.generateFromConversation"
      ),
      "Chat preference is now true"
    );
    Assert.ok(chatCheckbox.checked, "Chat checkbox is now checked");

    chatCheckbox.scrollIntoView();
    chatCheckbox.labelEl.click();
    await chatCheckbox.updateComplete;

    Assert.ok(
      !Services.prefs.getBoolPref(
        "browser.smartwindow.memories.generateFromConversation"
      ),
      "Chat preference is now false"
    );
    Assert.ok(!chatCheckbox.checked, "Chat checkbox is NOT checked");

    const settingsMemoriesEvents =
      Glean.smartWindow.settingsMemories.testGetValue();

    Assert.equal(
      settingsMemoriesEvents.length,
      2,
      "Two settingsMemories event were recorded"
    );
    Assert.equal(
      settingsMemoriesEvents[0].extra.type,
      "chat",
      "Settings memory telemetry type is chat for first event"
    );
    Assert.equal(
      settingsMemoriesEvents[0].extra.enabled,
      "true",
      "Settings memory telemetry for chat was updated to true for first event"
    );
    Assert.equal(
      settingsMemoriesEvents[1].extra.type,
      "chat",
      "Settings memory telemetry type is chat for second event"
    );
    Assert.equal(
      settingsMemoriesEvents[1].extra.enabled,
      "false",
      "Settings memory telemetry for chat was updated to false for second event"
    );
  });

  it("sends telemetry when memories browsing checkbox is updated", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", false],
        ["browser.smartwindow.memories.generateFromHistory", false],
      ],
    });

    let { doc, win } = await openSmartWindowPanel();

    const browsingCheckbox = doc.getElementById("learnFromBrowsingActivity");
    Assert.ok(
      !browsingCheckbox.checked,
      "Browsing checkbox is unchecked initially"
    );

    browsingCheckbox.scrollIntoView();
    EventUtils.synthesizeMouseAtCenter(browsingCheckbox.labelEl, {}, win);
    await browsingCheckbox.updateComplete;

    Assert.ok(
      Services.prefs.getBoolPref(
        "browser.smartwindow.memories.generateFromHistory"
      ),
      "Browsing preference is now true"
    );
    Assert.ok(browsingCheckbox.checked, "Browsing checkbox is now checked");

    browsingCheckbox.scrollIntoView();
    browsingCheckbox.labelEl.click();
    await browsingCheckbox.updateComplete;

    Assert.ok(
      !Services.prefs.getBoolPref(
        "browser.smartwindow.memories.generateFromHistory"
      ),
      "Browsing preference is now false"
    );
    Assert.ok(!browsingCheckbox.checked, "Browsing checkbox is NOT checked");

    const settingsMemoriesEvents =
      Glean.smartWindow.settingsMemories.testGetValue();

    Assert.equal(
      settingsMemoriesEvents.length,
      2,
      "Two settingsMemories event were recorded"
    );
    Assert.equal(
      settingsMemoriesEvents[0].extra.type,
      "browsing",
      "Settings memory telemetry type is browsing for first event"
    );
    Assert.equal(
      settingsMemoriesEvents[0].extra.enabled,
      "true",
      "Settings memory telemetry for browsing was updated to true for first event"
    );
    Assert.equal(
      settingsMemoriesEvents[1].extra.type,
      "browsing",
      "Settings memory telemetry type is browsing for second event"
    );
    Assert.equal(
      settingsMemoriesEvents[1].extra.enabled,
      "false",
      "Settings memory telemetry for browsing was updated to false for second event"
    );
  });

  it("sends telemetry when open sidebar by default checkbox is updated", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.sidebar.openByDefault", true]],
    });
    Services.fog.testResetFOG();

    let { doc, win } = await openSmartWindowPanel();

    const checkbox = doc.getElementById("openSidebarByDefault");
    Assert.ok(checkbox.checked, "Checkbox is checked initially");

    checkbox.scrollIntoView();
    EventUtils.synthesizeMouseAtCenter(checkbox.labelEl, {}, win);
    await checkbox.updateComplete;

    Assert.ok(!checkbox.checked, "Checkbox is now unchecked");
    Assert.equal(
      Glean.smartWindow.sidebarOpenByDefault.testGetValue(),
      false,
      "sidebarOpenByDefault metric is false after unchecking"
    );

    checkbox.labelEl.click();
    await checkbox.updateComplete;

    Assert.ok(checkbox.checked, "Checkbox is checked again");
    Assert.equal(
      Glean.smartWindow.sidebarOpenByDefault.testGetValue(),
      true,
      "sidebarOpenByDefault metric is true after checking"
    );

    const changeEvents =
      Glean.smartWindow.sidebarOpenByDefaultChange.testGetValue();
    Assert.equal(
      changeEvents.length,
      2,
      "Two sidebarOpenByDefaultChange events were recorded"
    );
    Assert.equal(
      changeEvents[0].extra.enabled,
      "false",
      "First event records the sidebar no longer opening by default"
    );
    Assert.equal(
      changeEvents[1].extra.enabled,
      "true",
      "Second event records the sidebar opening by default again"
    );
  });

  it("sends telemetry when memories panel is visible", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    const { memories } = await populateMemories();
    await openManageMemoriesPanel();

    const settingsMemoriesPanelEvent =
      Glean.smartWindow.memoriesPanelDisplayed.testGetValue();

    Assert.equal(
      settingsMemoriesPanelEvent.length,
      1,
      "One memoriesPanelDisplayed event was recorded"
    );
    Assert.equal(
      settingsMemoriesPanelEvent[0].extra.source,
      "settings",
      "memoriesPanelDisplayed source is settings"
    );
    Assert.equal(
      settingsMemoriesPanelEvent[0].extra.memories,
      memories.length,
      `memoriesPanelDisplayed memories quantity is ${memories.length}`
    );
  });

  it("sends telemetry when delete all memories button is clicked", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromConversation", true],
        ["browser.smartwindow.memories.generateFromHistory", true],
      ],
    });

    await populateMemories();
    let { doc } = await openManageMemoriesPanel();

    const memoriesList = doc.getElementById("memoriesList");
    await memoriesList.updateComplete;

    const deleteAllMemoriesButton = memoriesList.querySelector(
      "#deleteAllMemoriesButton"
    );
    Assert.ok(deleteAllMemoriesButton, "Delete all memories button exists");

    const dialogPromise = BrowserTestUtils.promiseAlertDialogOpen("accept");
    deleteAllMemoriesButton.click();
    await dialogPromise;

    await TestUtils.waitForCondition(
      () => Glean.smartWindow.memoriesNuke.testGetValue(),
      "Waiting for memoriesNuke telemetry to be recorded"
    );

    const settingsMemoriesNukeEvent =
      Glean.smartWindow.memoriesNuke.testGetValue();

    Assert.equal(
      settingsMemoriesNukeEvent.length,
      1,
      "One memoriesNuke event was recorded"
    );
  });
});
