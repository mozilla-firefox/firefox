/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_smart_window.js", gTestPath).href,
  this
);

requestLongerTimeout(3);

describe("Smart Window custom model selection", () => {
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

  it("shows custom fields when custom radio is selected", async () => {
    ({ doc, win } = await openSmartWindowPanel());

    await selectCustomModel(doc);

    Assert.equal(
      Services.prefs.getStringPref(
        "browser.smartwindow.firstrun.modelChoice",
        ""
      ),
      "",
      "Custom radio click does not prematurely write firstrun.modelChoice"
    );

    const customModelName = doc.getElementById("customModelName");
    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    const customModelAuthToken = doc.getElementById("customModelAuthToken");
    const customModelSaveButton = doc.getElementById("customModelSaveButton");

    Assert.ok(
      BrowserTestUtils.isVisible(customModelName),
      "Custom model name input is visible"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(customModelEndpoint),
      "Custom model endpoint input is visible"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(customModelAuthToken),
      "Custom model auth token input is visible"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(customModelSaveButton),
      "Custom model save button is visible"
    );

    // Reset to preset for keyboard test
    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    fastRadio.click();

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () => !BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
    );

    fastRadio.focus();
    // Move from Fast through Flexible to Custom.
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);
    EventUtils.synthesizeKey("KEY_ArrowDown", {}, win);

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () => BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
    );

    Assert.ok(
      BrowserTestUtils.isVisible(doc.getElementById("customModelName")),
      "Custom model name input is visible via keyboard"
    );
  });

  it("enables the save button only for valid endpoint URLs", async () => {
    ({ doc, win } = await openSmartWindowPanel());
    const endpoints = [
      {
        url: "example.com",
        disabled: true,
        description: "non-HTTPS, non-localhost URL",
      },
      {
        url: "https://example.com",
        disabled: false,
        description: "HTTPS URL",
      },
      {
        url: "http://localhost:8080",
        disabled: false,
        description: "localhost URL",
      },
    ];

    await selectCustomModel(doc);

    const customModelEndpoint = doc.getElementById("customModelEndpoint");
    const customModelSaveButton = doc.getElementById("customModelSaveButton");

    Assert.ok(
      customModelSaveButton.disabled,
      "Save button is disabled when endpoint is empty"
    );

    for (const { url, disabled, description } of endpoints) {
      customModelEndpoint.value = url;
      customModelEndpoint.dispatchEvent(new Event("change", { bubbles: true }));

      await BrowserTestUtils.waitForMutationCondition(
        customModelSaveButton,
        { attributes: true, attributeFilter: ["disabled"] },
        () => customModelSaveButton.disabled === disabled
      );

      Assert.equal(
        customModelSaveButton.disabled,
        disabled,
        `Save button is ${disabled ? "disabled" : "enabled"}: ${description}`
      );
    }
  });

  it("restores custom endpoint when switching back to custom", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.customEndpoint", "https://example.com"]],
    });

    ({ doc, win } = await openSmartWindowPanel());

    await selectCustomModel(doc);

    const customModelEndpoint = doc.getElementById("customModelEndpoint");

    await TestUtils.waitForCondition(
      () => customModelEndpoint.value === "https://example.com",
      "Waiting for endpoint to be restored in input"
    );

    Assert.equal(
      customModelEndpoint.value,
      "https://example.com",
      "Custom endpoint is restored in input"
    );
  });

  it("hides custom fields when preset model is selected", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "0"]],
    });

    ({ doc, win } = await openSmartWindowPanel());

    const fastRadio = doc.querySelector(
      'moz-radio[data-l10n-id="smart-window-model-fast-v2"]'
    );
    fastRadio.click();

    await BrowserTestUtils.waitForMutationCondition(
      doc.body,
      { attributes: true, childList: true, subtree: true },
      () => !BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
    );

    const customModelName = doc.getElementById("customModelName");
    Assert.ok(
      !BrowserTestUtils.isVisible(customModelName),
      "Custom fields are hidden when preset is selected"
    );
  });

  it("shows custom as selected when user has custom endpoint", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.customEndpoint", "https://example.com"],
        ["browser.smartwindow.firstrun.modelChoice", "0"],
      ],
    });

    ({ doc, win } = await openSmartWindowPanel());

    const modelSelection = doc.getElementById("modelSelection");
    Assert.equal(
      modelSelection.value,
      "0",
      "Custom radio is selected when user has custom endpoint"
    );
  });
});
