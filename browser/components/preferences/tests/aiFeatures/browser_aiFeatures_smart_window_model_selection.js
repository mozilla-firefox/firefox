/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_smart_window.js", gTestPath).href,
  this
);

requestLongerTimeout(3);

const { AUTO_MODEL_CHOICE_ID } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

function getModelRadio(doc, value) {
  return [
    ...doc.getElementById("modelSelection").querySelectorAll("moz-radio"),
  ].find(radio => radio.value === value);
}

describe("Smart Window model selection", () => {
  let doc, win;

  beforeEach(async function setup() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.preferences.aiControls", true],
        ["browser.smartwindow.enabled", true],
        ["browser.smartwindow.tos.consentTime", 1770830464],
      ],
    });
  });

  afterEach(async () => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);

    // Clean up prefs that changed by tests
    Services.prefs.clearUserPref("browser.smartwindow.apiKey");
    Services.prefs.clearUserPref("browser.smartwindow.endpoint");
    Services.prefs.clearUserPref("browser.smartwindow.firstrun.modelChoice");
    Services.prefs.clearUserPref("browser.smartwindow.model");
    Services.prefs.clearUserPref("browser.smartwindow.customEndpoint");
    Services.prefs.clearUserPref(
      "browser.smartwindow.firstrun.singlePageWithAutoModel"
    );

    await SpecialPowers.popPrefEnv();
  });

  it("selects model from onboarding choice", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "2"]],
    });

    ({ doc, win } = await openSmartWindowPanel());

    const modelSelection = doc.getElementById("modelSelection");
    Assert.equal(
      modelSelection.value,
      "2",
      "Model from onboarding choice is selected"
    );
  });

  it("shows model selection with no model selected, then selects a model", async () => {
    ({ doc, win } = await openSmartWindowPanel());

    const modelSelection = doc.getElementById("modelSelection");
    Assert.ok(modelSelection, "Model selection exists");
    Assert.ok(
      BrowserTestUtils.isVisible(modelSelection),
      "Model selection is visible"
    );
    Assert.equal(
      modelSelection.value,
      null,
      "No radio is selected if user didn't select model choices"
    );

    await assertRadioGroupAccessibleName(modelSelection);

    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    fastRadio.click();

    await TestUtils.waitForCondition(
      () =>
        Services.prefs.getStringPref(
          "browser.smartwindow.firstrun.modelChoice",
          ""
        ) === "1",
      "Waiting for model pref to be saved"
    );

    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.firstrun.modelChoice"),
      "1",
      "Model pref is saved"
    );

    fastRadio.focus();
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);

    await TestUtils.waitForCondition(
      () =>
        Services.prefs.getStringPref(
          "browser.smartwindow.firstrun.modelChoice",
          ""
        ) === "2",
      "Waiting for model pref to be saved via keyboard"
    );

    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.firstrun.modelChoice"),
      "2",
      "Model pref is saved via keyboard"
    );
  });

  it("hides the Auto model option when the pref is off, and shows it once the pref is flipped", async () => {
    ({ doc, win } = await openSmartWindowPanel());

    Assert.ok(
      !getModelRadio(doc, AUTO_MODEL_CHOICE_ID),
      "Auto model option is hidden with the pref off"
    );

    Services.prefs.setBoolPref(
      "browser.smartwindow.firstrun.singlePageWithAutoModel",
      true
    );

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { childList: true, subtree: true },
      () => getModelRadio(doc, AUTO_MODEL_CHOICE_ID)
    );
    Assert.ok(true, "Auto model option appears when the pref is turned on");
  });

  describe("with the Auto model enabled", () => {
    beforeEach(async () => {
      Services.fog.testResetFOG();
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.singlePageWithAutoModel", true]],
      });
    });

    afterEach(async () => {
      await SpecialPowers.popPrefEnv();
    });

    it("shows Auto first and saves it", async () => {
      ({ doc, win } = await openSmartWindowPanel());

      const modelSelection = doc.getElementById("modelSelection");
      await modelSelection.updateComplete;
      Assert.deepEqual(
        [...modelSelection.querySelectorAll("moz-radio")].map(r => r.value),
        [AUTO_MODEL_CHOICE_ID, "2", "3", "1", "0"],
        "Models are shown as Auto, Qwen, Mistral, Gemini, then Custom"
      );

      const firstRadio = modelSelection.querySelector("moz-radio");

      firstRadio.inputEl.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(firstRadio.inputEl, {}, win);

      await TestUtils.waitForCondition(
        () =>
          Services.prefs.getStringPref(
            "browser.smartwindow.firstrun.modelChoice",
            ""
          ) === AUTO_MODEL_CHOICE_ID,
        "Waiting for Auto model choice to be saved"
      );
      Assert.equal(
        modelSelection.value,
        AUTO_MODEL_CHOICE_ID,
        "Auto model is selected"
      );

      const events = Glean.smartWindow.settingsModel.testGetValue();
      Assert.equal(events.length, 1, "One settingsModel event was recorded");
      Assert.equal(
        events[0].extra.new_model,
        "auto",
        "Auto model is reported as auto"
      );
    });

    it("switches between Auto and Custom", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.firstrun.modelChoice", AUTO_MODEL_CHOICE_ID],
        ],
      });

      ({ doc, win } = await openSmartWindowPanel());

      const modelSelection = doc.getElementById("modelSelection");
      Assert.equal(
        modelSelection.value,
        AUTO_MODEL_CHOICE_ID,
        "Auto model is selected"
      );

      const customModelName = await selectCustomModel(doc);
      Assert.equal(modelSelection.value, "0", "Custom is selected");
      Assert.ok(
        BrowserTestUtils.isVisible(customModelName),
        "Custom model fields are shown"
      );
      Assert.equal(
        Services.prefs.getStringPref(
          "browser.smartwindow.firstrun.modelChoice"
        ),
        AUTO_MODEL_CHOICE_ID,
        "Auto model choice is kept until Custom is saved"
      );

      const autoRadio = getModelRadio(doc, AUTO_MODEL_CHOICE_ID);
      autoRadio.inputEl.scrollIntoView();
      EventUtils.synthesizeMouseAtCenter(autoRadio.inputEl, {}, win);

      await BrowserTestUtils.waitForMutationCondition(
        doc.body,
        { attributes: true, childList: true, subtree: true },
        () => !BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
      );
      Assert.equal(
        modelSelection.value,
        AUTO_MODEL_CHOICE_ID,
        "Auto model is selected again"
      );

      const events = Glean.smartWindow.settingsModel.testGetValue();
      Assert.equal(
        events.at(-1).extra.previous_model,
        "auto",
        "Previous Auto model is reported as auto"
      );
    });
  });
});
