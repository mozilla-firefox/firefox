/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SmartWindowTelemetry } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/SmartWindowTelemetry.sys.mjs"
);

describe("SmartWindowMetricsTelemetry", () => {
  let win;

  beforeEach(async () => {
    Services.fog.testResetFOG();
    SmartWindowTelemetry._initialized = false;
  });

  it("records model metric when opening smart window", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "0"]],
    });
    win = await openAIWindow();

    const modelMetric = Glean.smartWindow.model.testGetValue();
    Assert.equal(
      modelMetric,
      "custom-model",
      "The model metric should be custom-model"
    );
    await SpecialPowers.popPrefEnv();
    await BrowserTestUtils.closeWindow(win);
  });

  it("records memoriesOptin.generateFromConversation as true", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.memories.generateFromConversation", true]],
    });
    SmartWindowTelemetry.updateMemoriesFromConversationMetric();

    Assert.strictEqual(
      Glean.smartWindow.memoriesOptin.generate_from_conversation.testGetValue(),
      true,
      "memoriesOptin.generate_from_conversation should be true"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records memoriesOptin.generateFromConversation as false", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.memories.generateFromConversation", false]],
    });
    SmartWindowTelemetry.updateMemoriesFromConversationMetric();

    Assert.strictEqual(
      Glean.smartWindow.memoriesOptin.generate_from_conversation.testGetValue(),
      false,
      "memoriesOptin.generate_from_conversation should be false"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records memoriesOptin.generateFromHistory as true", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.memories.generateFromHistory", true]],
    });
    SmartWindowTelemetry.updateMemoriesFromHistoryMetric();

    Assert.strictEqual(
      Glean.smartWindow.memoriesOptin.generate_from_history.testGetValue(),
      true,
      "memoriesOptin.generate_from_history should be true"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records memoriesOptin.generateFromHistory as false", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.memories.generateFromHistory", false]],
    });
    SmartWindowTelemetry.updateMemoriesFromHistoryMetric();

    Assert.strictEqual(
      Glean.smartWindow.memoriesOptin.generate_from_history.testGetValue(),
      false,
      "memoriesOptin.generate_from_history should be false"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records setDefaultOptin as true", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.isDefaultWindow", true]],
    });
    SmartWindowTelemetry.updateSetDefaultOptinMetric();

    Assert.strictEqual(
      Glean.smartWindow.setDefaultOptin.testGetValue(),
      true,
      "setDefaultOptin should be true"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records setDefaultOptin as false", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.isDefaultWindow", false]],
    });
    SmartWindowTelemetry.updateSetDefaultOptinMetric();

    Assert.strictEqual(
      Glean.smartWindow.setDefaultOptin.testGetValue(),
      false,
      "setDefaultOptin should be false"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records enabled as true", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", true]],
    });
    SmartWindowTelemetry.updateEnabledMetric();

    Assert.strictEqual(
      Glean.smartWindow.enabled.testGetValue(),
      true,
      "enabled should be true"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records enabled as false", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", false]],
    });
    SmartWindowTelemetry.updateEnabledMetric();

    Assert.strictEqual(
      Glean.smartWindow.enabled.testGetValue(),
      false,
      "enabled should be false"
    );
    await SpecialPowers.popPrefEnv();
  });

  it("records enabled when the pref flips to true", async () => {
    SmartWindowTelemetry.init();
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.enabled", true]],
    });

    Assert.strictEqual(
      Glean.smartWindow.enabled.testGetValue(),
      true,
      "enabled should be updated by the pref observer"
    );
    await SpecialPowers.popPrefEnv();
  });
});
