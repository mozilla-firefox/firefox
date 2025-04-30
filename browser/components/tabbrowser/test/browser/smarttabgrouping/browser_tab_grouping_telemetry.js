/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

async function openCreatePanel(tabgroupPanel, tab) {
  let panelShown = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "shown");
  gBrowser.addTabGroup([tab], {
    color: "cyan",
    isUserTriggered: true,
  });
  await panelShown;
}

async function setup({
  enableSmartTab = true,
  optIn = true,
  labelReason = "DEFAULT",
} = {}) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.groups.enabled", true],
      ["browser.tabs.groups.smart.enabled", enableSmartTab],
      ["browser.tabs.groups.smart.optin", optIn],
    ],
  });
  sinon
    .stub(SmartTabGroupingManager.prototype, "generateGroupLabels")
    .returns("");
  sinon
    .stub(SmartTabGroupingManager.prototype, "smartTabGroupingForGroup")
    .resolves([]);
  sinon.stub(SmartTabGroupingManager.prototype, "getEngineConfigs").resolves({
    "text2text-generation": { modelRevision: "v0.3.4" },
    "feature-extraction": { modelRevision: "v0.1.0" },
  });
  sinon
    .stub(SmartTabGroupingManager.prototype, "preloadAllModels")
    .resolves([]);

  sinon
    .stub(SmartTabGroupingManager.prototype, "getLabelReason")
    .returns(labelReason);

  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const cleanup = () => {
    sinon.restore();
    BrowserTestUtils.removeTab(tab);
    Services.fog.testResetFOG();
  };
  return { tab, cleanup };
}

add_task(async function test_saving_ml_suggested_label_telemetry() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const events = Glean.tabgroup.smartTabTopic.testGetValue();
  Assert.equal(events.length, 1, "Should create a label event");
  Assert.equal(events[0].extra.action, "save", "Save button was clicked");
  Assert.equal(events[0].extra.tabs_in_group, "1", "Number of tabs in group");
  Assert.equal(events[0].extra.ml_label_length, "25", "Suggested ML Label");
  Assert.equal(
    events[0].extra.user_label_length,
    "25",
    "User input was the same as ml suggested label"
  );
  Assert.equal(
    events[0].extra.model_revision,
    "v0.3.4",
    "Model revision should be present"
  );
  Assert.equal(
    events[0].extra.label_reason,
    "DEFAULT",
    "If suggested label had more than zero length, reason should be default"
  );
  cleanup();
});

add_task(async function test_cancel_ml_suggested_label_telemetry() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  tabgroupPanel.querySelector("#tab-group-editor-button-cancel").click(); // cancel
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const events = Glean.tabgroup.smartTabTopic.testGetValue();
  Assert.equal(events.length, 1, "Should create a label event");
  Assert.equal(
    events[0].extra.action,
    "cancel",
    "Should save label even if cancel button was clicked"
  );
  Assert.equal(events[0].extra.tabs_in_group, "1", "Number of tabs in group");
  Assert.equal(events[0].extra.ml_label_length, "25", "Suggested ML Label");
  Assert.equal(
    events[0].extra.user_label_length,
    "25",
    "User input was the same as ml suggested label"
  );
  Assert.equal(
    events[0].extra.model_revision,
    "v0.3.4",
    "Model revision should be present"
  );
  cleanup();
});

add_task(async function test_saving_ml_suggested_tabs() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  // add the name only
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  // click on the suggest button
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  tabgroupEditor.hasSuggestedMlTabs = true;
  tabgroupPanel.querySelector("#tab-group-create-suggestions-button").click();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;
  const events = Glean.tabgroup.smartTabSuggest.testGetValue();
  Assert.equal(events.length, 1, "Should create a sugggest event");
  Assert.equal(events[0].extra.action, "save", "Save button was clicked");
  Assert.equal(events[0].extra.tabs_in_window, "2", "Number of tabs in window");
  Assert.equal(
    events[0].extra.tabs_in_group,
    "1",
    "Number of tabs in the group"
  );
  Assert.equal(events[0].extra.tabs_suggested, "0", "No tabs were suggested");
  Assert.equal(events[0].extra.tabs_approved, "0", "No tabs were approved");
  Assert.equal(events[0].extra.tabs_removed, "0", "No tabs were removed");
  Assert.equal(
    events[0].extra.model_revision,
    "v0.1.0",
    "Model revision should be present"
  );
  cleanup();
});

add_task(async function test_saving_ml_suggested_tabs_with_ml_label() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  // add the label
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  // click on the suggest button
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  tabgroupEditor.hasSuggestedMlTabs = true;
  tabgroupPanel.querySelector("#tab-group-create-suggestions-button").click();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const labelEvent = Glean.tabgroup.smartTabTopic.testGetValue();
  const suggestEvent = Glean.tabgroup.smartTabSuggest.testGetValue();
  Assert.equal(labelEvent.length, 1, "Should create label event");
  Assert.equal(suggestEvent.length, 1, "Should create suggest event");
  Assert.equal(labelEvent[0].extra.action, "save", "Save button was clicked");
  Assert.equal(suggestEvent[0].extra.action, "save", "Save button was clicked");
  Assert.ok(suggestEvent[0].extra.id, "Id of group should be present");
  cleanup();
});

add_task(async function test_canceling_ml_suggested_tabs_with_ml_label() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  // add the label
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  // click on the suggest button
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  tabgroupEditor.hasSuggestedMlTabs = true;
  tabgroupPanel.querySelector("#tab-group-cancel-suggestions-button").click(); // cancel
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const labelEvent = Glean.tabgroup.smartTabTopic.testGetValue();
  const suggestEvent = Glean.tabgroup.smartTabSuggest.testGetValue();
  Assert.equal(labelEvent.length, 1, "Should create label event");
  Assert.equal(suggestEvent.length, 1, "Should create suggest event");
  Assert.equal(
    labelEvent[0].extra.action,
    "cancel",
    "Cancel button was clicked"
  );
  Assert.equal(
    suggestEvent[0].extra.action,
    "cancel",
    "cancel button was clicked"
  );
  Assert.ok(suggestEvent[0].extra.id, "Id of group should be present");
  cleanup();
});

add_task(async function test_pref_off_should_not_create_events() {
  let { tab, cleanup } = await setup({ enableSmartTab: false });
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  tabgroupPanel.querySelector("#tab-group-editor-button-create").click(); // save
  await panelHidden;

  Assert.equal(
    Glean.tabgroup.smartTabTopic.testGetValue() ?? "none",
    "none",
    "No event if the feature is off"
  );
  cleanup();
});

add_task(async function test_saving_ml_label_popup_hidden_no_button_click() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  // add the label
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  // click on the suggest button
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  tabgroupEditor.hasSuggestedMlTabs = true;
  tabgroupPanel.hidePopup();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const labelEvent = Glean.tabgroup.smartTabTopic.testGetValue();
  const suggestEvent = Glean.tabgroup.smartTabSuggest.testGetValue();
  Assert.equal(labelEvent.length, 1, "Should create label event");
  Assert.equal(suggestEvent.length, 1, "Should create suggest event");
  Assert.equal(
    labelEvent[0].extra.action,
    "save-popup-hidden",
    "Popup was hidden by clicking away"
  );
  Assert.equal(
    suggestEvent[0].extra.action,
    "save-popup-hidden",
    "Popup was hidden by clicking away"
  );
  Assert.ok(suggestEvent[0].extra.id, "Id of group should be present");
  cleanup();
});

async function waitForUpdateComplete(element) {
  if (element && typeof element.updateComplete === "object") {
    await element.updateComplete;
  }
}

add_task(async function test_optin_telemetry() {
  let { tab, cleanup } = await setup({ enableSmartTab: true, optIn: false });
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;

  await openCreatePanel(tabgroupPanel, tab);
  // click on suggestions flow
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  let optInEvent = Glean.tabgroup.smartTabOptin.testGetValue();
  Assert.equal(
    optInEvent.length,
    1,
    "Should create optin event for onboarding"
  );
  Assert.equal(
    optInEvent[0].extra.step,
    "step0-optin-shown",
    "Should show proper step and description"
  );

  let mo = document.querySelector("model-optin");
  Assert.ok(mo, "Found the ModelOptin element in the DOM.");
  await waitForUpdateComplete(mo);

  // first cancel the flow
  Services.fog.testResetFOG();
  const denyBtn = mo.shadowRoot.querySelector("#optin-deny-button");
  denyBtn.click();
  optInEvent = Glean.tabgroup.smartTabOptin.testGetValue();
  Assert.equal(optInEvent.length, 1, "Should create optin cancel event");
  Assert.equal(
    optInEvent[0].extra.step,
    "step1-optin-denied",
    "Should show proper step and description"
  );
  cleanup();
});

add_task(async function test_saving_ml_suggested_empty_label_telemetry() {
  let { tab, cleanup } = await setup();
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = ""; // suggested label
  tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  const events = Glean.tabgroup.smartTabTopic.testGetValue();
  Assert.equal(events.length, 1, "Should create a label event");
  Assert.equal(events[0].extra.action, "save", "Save button was clicked");
  Assert.equal(events[0].extra.tabs_in_group, "1", "Number of tabs in group");
  Assert.equal(events[0].extra.ml_label_length, "0", "Suggested ML Label");
  Assert.equal(
    events[0].extra.user_label_length,
    "25",
    "User input was the same as ml suggested label"
  );
  Assert.equal(
    events[0].extra.model_revision,
    "v0.3.4",
    "Model revision should be present"
  );
  cleanup();
});

add_task(async function test_user_not_opted_in_should_not_send_ml_events() {
  let { tab, cleanup } = await setup({ enableSmartTab: true, optIn: false });
  let tabgroupEditor = document.getElementById("tab-group-editor");
  let tabgroupPanel = tabgroupEditor.panel;
  let nameField = tabgroupPanel.querySelector("#tab-group-name");

  await openCreatePanel(tabgroupPanel, tab);
  // add the label
  nameField.focus();
  nameField.value = "Random ML Suggested Label"; // user label matching suggested label
  tabgroupEditor.mlLabel = "Random ML Suggested Label"; // suggested label
  // click on the suggest button
  tabgroupPanel.querySelector("#tab-group-suggestion-button").click();
  tabgroupEditor.hasSuggestedMlTabs = true;
  tabgroupPanel.hidePopup();
  let panelHidden = BrowserTestUtils.waitForPopupEvent(tabgroupPanel, "hidden");
  await panelHidden;

  Assert.equal(
    Glean.tabgroup.smartTabTopic.testGetValue() ?? "none",
    "none",
    "No topic event should be sent"
  );
  Assert.equal(
    Glean.tabgroup.smartTabSuggest.testGetValue() ?? "none",
    "none",
    "No suggest event should be sent"
  );
  cleanup();
});

add_task(
  async function test_saving_ml_zero_length_low_confidence_label_telemetry() {
    let { tab, cleanup } = await setup({ labelReason: "LOW_CONFIDENCE" });
    let tabgroupEditor = document.getElementById("tab-group-editor");
    let tabgroupPanel = tabgroupEditor.panel;
    let nameField = tabgroupPanel.querySelector("#tab-group-name");

    await openCreatePanel(tabgroupPanel, tab);
    nameField.focus();
    nameField.value = "Random Non-ML Label"; // user label matching suggested label
    tabgroupEditor.mlLabel = ""; // empty label
    tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
    let panelHidden = BrowserTestUtils.waitForPopupEvent(
      tabgroupPanel,
      "hidden"
    );
    await panelHidden;

    const events = Glean.tabgroup.smartTabTopic.testGetValue();
    Assert.equal(
      events[0].extra.label_reason,
      "LOW_CONFIDENCE",
      "If suggested label zero length with low confidence, proper reason should be given"
    );
    cleanup();
  }
);

add_task(
  async function test_saving_ml_zero_length_dissimilar_label_telemetry() {
    let { tab, cleanup } = await setup({ labelReason: "EXCLUDE" });
    let tabgroupEditor = document.getElementById("tab-group-editor");
    let tabgroupPanel = tabgroupEditor.panel;
    let nameField = tabgroupPanel.querySelector("#tab-group-name");

    await openCreatePanel(tabgroupPanel, tab);
    nameField.focus();
    nameField.value = "Random Non-ML Label"; // user label matching suggested label
    tabgroupEditor.mlLabel = ""; // empty label
    tabgroupPanel.querySelector("#tab-group-editor-button-create").click();
    let panelHidden = BrowserTestUtils.waitForPopupEvent(
      tabgroupPanel,
      "hidden"
    );
    await panelHidden;

    const events = Glean.tabgroup.smartTabTopic.testGetValue();
    Assert.equal(
      events[0].extra.label_reason,
      "EXCLUDE",
      "If suggested label zero length with dissimilar tabs, proper reason should be given"
    );
    cleanup();
  }
);
