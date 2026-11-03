/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_smart_window.js", gTestPath).href,
  this
);

requestLongerTimeout(3);

describe("Smart Window custom model settings", () => {
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

    await SpecialPowers.popPrefEnv();
  });

  it("saves custom model when save button is clicked", async () => {
    ({ doc, win } = await openSmartWindowPanel());

    await selectCustomModel(doc);

    const customModelName = doc.getElementById("customModelName");
    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    const customModelAuthToken = doc.getElementById("customModelAuthToken");

    customModelEndpoint.value = "https://example.com";
    customModelEndpoint.dispatchEvent(new Event("change", { bubbles: true }));

    // Wait for button to be enabled
    const customModelSaveButton = doc.getElementById("customModelSaveButton");
    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => !customModelSaveButton.disabled
    );

    customModelName.value = "my-custom-model";
    customModelName.dispatchEvent(new Event("input", { bubbles: true }));
    customModelAuthToken.value = "my-token";
    customModelAuthToken.dispatchEvent(new Event("input", { bubbles: true }));

    customModelSaveButton.scrollIntoView({});
    EventUtils.synthesizeMouseAtCenter(customModelSaveButton, {}, win);

    await TestUtils.waitForCondition(
      () =>
        Services.prefs.getStringPref("browser.smartwindow.model", "") ===
        "my-custom-model",
      "Waiting for model to be saved via mouse"
    );

    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.firstrun.modelChoice"),
      "0",
      "firstrun.modelChoice is written to '0' when save button is clicked"
    );
    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.model"),
      "my-custom-model",
      "Model pref is saved via mouse"
    );

    // Reset for keyboard test
    Services.prefs.clearUserPref("browser.smartwindow.model");
    Services.prefs.clearUserPref("browser.smartwindow.customEndpoint");
    Services.prefs.clearUserPref("browser.smartwindow.apiKey");

    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => !customModelSaveButton.disabled
    );

    // Set values after Lit has flushed re-renders so they aren't overwritten.
    customModelName.value = "keyboard-model";
    customModelEndpoint.value = "https://example.com";
    customModelAuthToken.value = "keyboard-token";

    // Keyboard test to focus and space bar
    customModelSaveButton.focus();
    EventUtils.synthesizeKey(" ", {}, win);

    await TestUtils.waitForCondition(
      () =>
        Services.prefs.getStringPref("browser.smartwindow.model", "") ===
        "keyboard-model",
      "Waiting for model to be saved via keyboard"
    );

    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.model"),
      "keyboard-model",
      "Model pref is saved via keyboard"
    );
    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.customEndpoint"),
      "https://example.com",
      "Endpoint pref is saved via keyboard"
    );
  });

  it("save button disables after save and re-enables when the user edits a field", async () => {
    ({ doc, win } = await openSmartWindowPanel());

    await selectCustomModel(doc);

    const customModelName = doc.getElementById("customModelName");
    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    const customModelSaveButton = doc.getElementById("customModelSaveButton");
    let confirmation = doc.getElementById("customModelSaveConfirmation");

    Assert.ok(
      !BrowserTestUtils.isVisible(confirmation),
      "Save confirmation is hidden before the user saves a custom model"
    );

    customModelName.value = "my-model";
    customModelName.dispatchEvent(new Event("change", { bubbles: true }));
    customModelEndpoint.value = "https://example.com";
    customModelEndpoint.dispatchEvent(new Event("change", { bubbles: true }));
    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => !customModelSaveButton.disabled
    );

    customModelSaveButton.scrollIntoView({});
    EventUtils.synthesizeMouseAtCenter(customModelSaveButton, {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () =>
        customModelSaveButton.disabled &&
        BrowserTestUtils.isVisible(
          doc.getElementById("customModelSaveConfirmation")
        )
    );

    confirmation = doc.getElementById("customModelSaveConfirmation");
    Assert.ok(
      customModelSaveButton.disabled,
      "Save button is disabled immediately after a successful save"
    );
    Assert.equal(
      confirmation.getAttribute("role"),
      "status",
      "Confirmation has role=status so assistive technologies announce the message"
    );
    Assert.equal(
      confirmation.querySelector("[data-l10n-id]").getAttribute("data-l10n-id"),
      "smart-window-model-custom-save-confirmation",
      "Confirmation renders the smart-window-model-custom-save-confirmation Fluent string"
    );

    customModelName.value = "my-other-model";
    customModelName.dispatchEvent(
      new Event("input", { bubbles: true, composed: true })
    );

    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => !customModelSaveButton.disabled
    );

    Assert.ok(
      !customModelSaveButton.disabled,
      "Save button is enabled again once the form differs from the saved values"
    );
    Assert.ok(
      !BrowserTestUtils.isVisible(
        doc.getElementById("customModelSaveConfirmation")
      ),
      "Save confirmation is hidden once the form differs from the saved values"
    );

    customModelName.value = "my-model";
    customModelName.dispatchEvent(
      new Event("input", { bubbles: true, composed: true })
    );

    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => customModelSaveButton.disabled
    );

    Assert.ok(
      customModelSaveButton.disabled,
      "Save button is disabled when the user reverts edits back to the saved values"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(
        doc.getElementById("customModelSaveConfirmation")
      ),
      "Save confirmation is visible again when the form matches the saved values"
    );
  });

  it("save button stays disabled after switching to a preset model and back to custom", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.model", "saved-model"],
        ["browser.smartwindow.customEndpoint", "https://example.com"],
        ["browser.smartwindow.apiKey", "saved-token"],
        ["browser.smartwindow.firstrun.modelChoice", "0"],
      ],
    });

    ({ doc, win } = await openSmartWindowPanel());

    const customModelSaveButton = doc.getElementById("customModelSaveButton");

    await BrowserTestUtils.waitForMutationCondition(
      customModelSaveButton,
      { attributes: true, attributeFilter: ["disabled"] },
      () => customModelSaveButton.disabled
    );

    Assert.ok(
      customModelSaveButton.disabled,
      "Save button is disabled when a previously saved custom model is loaded"
    );

    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    fastRadio.click();

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () => !BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
    );

    await selectCustomModel(doc);

    const restoredSaveButton = doc.getElementById("customModelSaveButton");

    Assert.ok(
      restoredSaveButton.disabled,
      "Save button stays disabled after switching to a preset and back to custom without edits"
    );
    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.firstrun.modelChoice"),
      "0",
      "firstrun.modelChoice is restored to '0' so the saved custom model is the active selection again"
    );
    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.customEndpoint"),
      "https://example.com",
      "smartwindow.customEndpoint persists the previously saved value"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(
        doc.getElementById("customModelSaveConfirmation")
      ),
      "Save confirmation is visible since the form matches the saved values"
    );
  });

  it("populates custom fields with saved values", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.model", "saved-model"],
        ["browser.smartwindow.customEndpoint", "https://example.com"],
        ["browser.smartwindow.apiKey", "saved-token"],
        ["browser.smartwindow.firstrun.modelChoice", "0"],
      ],
    });

    ({ doc, win } = await openSmartWindowPanel());

    const customModelName = doc.getElementById("customModelName");
    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    const customModelAuthToken = doc.getElementById("customModelAuthToken");
    const customModelSaveButton = doc.getElementById("customModelSaveButton");
    const confirmation = doc.getElementById("customModelSaveConfirmation");

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () => BrowserTestUtils.isVisible(confirmation)
    );

    Assert.equal(
      customModelName.value,
      "saved-model",
      "Model name will be populated"
    );
    Assert.equal(
      customModelEndpoint.value,
      "https://example.com",
      "Endpoint is populated"
    );
    Assert.equal(
      customModelAuthToken.value,
      "saved-token",
      "Auth token is populated"
    );
    Assert.ok(
      customModelSaveButton.disabled,
      "Save button is disabled when saved custom values have not changed"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(confirmation),
      "Save confirmation is visible when saved custom values have not changed"
    );
  });
});
