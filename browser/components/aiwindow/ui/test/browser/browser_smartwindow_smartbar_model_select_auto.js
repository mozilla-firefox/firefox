/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the Auto model in the Smartbar model select.
 */

"use strict";

const {
  _clearModelsDataCacheForTesting,
  AUTO_MODEL_CHOICE_ID,
  FALLBACK_MODELS,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);

const DEFAULT_MODEL_CHOICE_ID = "2";

add_setup(async function () {
  _clearModelsDataCacheForTesting();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.modelChoice", DEFAULT_MODEL_CHOICE_ID],
      ["places.semanticHistory.smartwindow.featureGate", false],
    ],
  });
});

/**
 * Reads the smartbar model select button and panel items.
 *
 * @param {MozBrowser} browser - The browser element
 */
async function getModelSelectContents(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const modelSelect = await ContentTaskUtils.waitForCondition(
      () =>
        aiWindowElement.shadowRoot
          .querySelector("#ai-window-smartbar")
          ?.querySelector("input-model-select"),
      "Wait for the model select"
    );
    await ContentTaskUtils.waitForCondition(
      () => modelSelect.shadowRoot.querySelector("button.model-item"),
      "Wait for the model items"
    );
    const button = modelSelect.shadowRoot.querySelector("moz-button");
    const items = [
      ...modelSelect.shadowRoot.querySelectorAll("button.model-item"),
    ].map(item => ({
      icon: item
        .querySelector(".model-item-icon")
        .getAttribute("src")
        .match(/model-choice-(.+)\.svg/)[1],
      labelL10nId: item
        .querySelector(".model-item-label")
        .getAttribute("data-l10n-id"),
      detailsL10nId: item
        .querySelector(".model-item-details")
        .getAttribute("data-l10n-id"),
      badges: [...item.querySelectorAll("moz-badge")].map(badge =>
        badge.getAttribute("data-l10n-id")
      ),
    }));
    return {
      buttonL10nId: button.getAttribute("data-l10n-id"),
      buttonLabel: button.getAttribute("label"),
      items,
    };
  });
}

add_task(async function test_auto_model_hidden_without_pref() {
  await withModelChoiceWindow({ enabled: false }, async ({ browser }) => {
    const { items } = await getModelSelectContents(browser);
    Assert.ok(
      !items.some(item => item.icon === "auto"),
      "Auto is not offered while the pref is off"
    );
    Assert.deepEqual(
      items.map(({ icon, detailsL10nId }) => [icon, detailsL10nId]),
      [
        ["mistral", "aiwindow-input-model-select-button-label-personal"],
        ["gemini", "aiwindow-input-model-select-button-label-fast"],
        ["qwen", "aiwindow-input-model-select-button-label-allpurpose"],
      ],
      "The presets keep their order and labels while the pref is off"
    );
  });
});

add_task(async function test_auto_model_listed_first_as_recommended() {
  await withModelChoiceWindow({ enabled: true }, async ({ browser }) => {
    const { items } = await getModelSelectContents(browser);
    const [auto, ...presets] = items;

    Assert.equal(auto.icon, "auto", "Auto leads the preset models");
    Assert.equal(
      auto.labelL10nId,
      "aiwindow-input-model-select-button-label-auto",
      "Auto uses its own label instead of the routed model's name"
    );
    Assert.equal(
      auto.detailsL10nId,
      "aiwindow-input-model-select-auto-description",
      "Auto shows its description"
    );
    Assert.deepEqual(
      auto.badges,
      ["aiwindow-input-model-select-recommended-badge"],
      "Auto is marked as recommended"
    );
    Assert.ok(
      presets.every(
        item =>
          !item.badges.includes("aiwindow-input-model-select-recommended-badge")
      ),
      "Only Auto is marked as recommended"
    );
    Assert.deepEqual(
      presets.map(({ icon, detailsL10nId }) => [icon, detailsL10nId]),
      [
        ["qwen", "aiwindow-input-model-select-flexible-description"],
        ["mistral", "aiwindow-input-model-select-personal-description"],
        ["gemini", "aiwindow-input-model-select-fast-description"],
      ],
      "The presets follow the Auto-Model order and descriptions"
    );
  });
});

add_task(async function test_auto_model_as_default() {
  await withModelChoiceWindow(
    { enabled: true, modelChoice: "4" },
    async ({ browser }) => {
      await waitForSelectedModel(browser, "auto");
      const { buttonL10nId, buttonLabel, items } =
        await getModelSelectContents(browser);
      Assert.equal(
        buttonL10nId,
        "aiwindow-input-model-select-button-label-auto",
        "The button reads Auto"
      );
      Assert.equal(buttonLabel, null, "The routed brand name is not shown");
      Assert.deepEqual(
        items.find(item => item.icon === "auto").badges,
        ["aiwindow-input-model-select-default-badge"],
        "Auto as the default is marked Default instead of Recommended"
      );
    }
  );
});

add_task(async function test_auto_model_default_kept_when_disabled() {
  await withModelChoiceWindow(
    { enabled: false, modelChoice: AUTO_MODEL_CHOICE_ID },
    async ({ browser }) => {
      await waitForSelectedModel(browser, "auto");
      const { buttonL10nId, items } = await getModelSelectContents(browser);
      Assert.equal(
        buttonL10nId,
        "aiwindow-input-model-select-button-label-auto",
        "The picker shows the saved Auto default"
      );
      Assert.deepEqual(
        items.map(item => item.icon),
        ["mistral", "gemini", "qwen", "auto"],
        "Auto stays listed after the presets"
      );
    }
  );
});

add_task(async function test_auto_model_override_falls_back_when_disabled() {
  await withModelChoiceWindow({ enabled: true }, async ({ browser }) => {
    const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;
    await waitForSelectedModel(browser, defaultModel);

    await switchSmartbarModel(browser, "4");
    await waitForSelectedModel(browser, "auto");

    // The helper's popPrefEnv restores the pref after the window closed.
    Services.prefs.setBoolPref(AUTO_MODEL_PREF, false);
    await waitForSelectedModel(browser, defaultModel);
    const { items } = await getModelSelectContents(browser);
    Assert.deepEqual(
      items.map(item => item.icon),
      ["mistral", "gemini", "qwen"],
      "Auto is removed from the list"
    );
  });
});

add_task(async function test_auto_model_offered_when_enabled() {
  await withModelChoiceWindow({ enabled: false }, async ({ browser }) => {
    Services.prefs.setBoolPref(AUTO_MODEL_PREF, true);
    await TestUtils.waitForCondition(
      () =>
        AUTO_MODEL_CHOICE_ID in
        browser.contentDocument.querySelector("ai-window").availableModels,
      "Wait for Auto to be offered"
    );
    const { items } = await getModelSelectContents(browser);
    Assert.deepEqual(
      items.map(item => item.icon),
      ["auto", "qwen", "mistral", "gemini"],
      "Auto is offered without reopening the window"
    );
  });
});

add_task(async function test_auto_model_not_offered_without_record() {
  await withModelChoiceWindow(
    { enabled: true, autoRecord: null },
    async ({ browser }) => {
      const { items } = await getModelSelectContents(browser);
      Assert.deepEqual(
        items.map(item => item.icon),
        ["qwen", "mistral", "gemini"],
        "Auto is not offered without its routing record"
      );
    }
  );
});

add_task(async function test_sidebar_auto_override_cleared_when_disabled() {
  await withModelChoiceWindow({ enabled: true }, async ({ win }) => {
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    const tabB = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );
    const browserSidebar = await waitForSidebarReady(win);
    const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;

    await waitForSelectedModel(browserSidebar, defaultModel);
    await switchSmartbarModel(browserSidebar, "4");
    await waitForSelectedModel(browserSidebar, "auto");

    Services.prefs.setBoolPref(AUTO_MODEL_PREF, false);
    await waitForSelectedModel(browserSidebar, defaultModel);

    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, defaultModel);
    await BrowserTestUtils.switchTab(win.gBrowser, tabB);
    await waitForSelectedModel(browserSidebar, defaultModel);

    // Tab B dropped its override, so it follows a new default.
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });
    try {
      await waitForSelectedModel(browserSidebar, FALLBACK_MODELS["3"].model);
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });
});

add_task(async function test_sidebar_background_auto_override_ignored() {
  await withModelChoiceWindow({ enabled: true }, async ({ win }) => {
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    const tabB = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );
    const browserSidebar = await waitForSidebarReady(win);
    const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;

    await waitForSelectedModel(browserSidebar, defaultModel);
    await switchSmartbarModel(browserSidebar, "4");
    await waitForSelectedModel(browserSidebar, "auto");

    // Turn Auto off while tab B, which keeps the Auto override, is in the
    // background.
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, defaultModel);
    Services.prefs.setBoolPref(AUTO_MODEL_PREF, false);
    await TestUtils.waitForCondition(
      () =>
        !(
          AUTO_MODEL_CHOICE_ID in
          browserSidebar.contentDocument.querySelector("ai-window")
            .availableModels
        ),
      "Wait for Auto to leave the available models"
    );

    await BrowserTestUtils.switchTab(win.gBrowser, tabB);
    await waitForSelectedModel(browserSidebar, defaultModel);

    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });
    try {
      await waitForSelectedModel(browserSidebar, FALLBACK_MODELS["3"].model);
    } finally {
      await SpecialPowers.popPrefEnv();
    }
  });
});

add_task(async function test_switch_to_auto_loads_routed_model_prompt() {
  const sandbox = sinon.createSandbox();
  const loadSystemPrompt = sandbox.spy(
    ChatConversation.prototype,
    "loadSystemPrompt"
  );
  try {
    await withModelChoiceWindow(
      { enabled: true },
      async ({ browser, engineManager }) => {
        Services.fog.testResetFOG();
        await typeInSmartbar(browser, "First message");
        await submitSmartbar(browser);
        await engineManager.respondTo({ purpose: "chat", response: "Hi." });
        await TestUtils.waitForCondition(
          () => Glean.smartWindow.modelResponse.testGetValue()?.length,
          "Wait for the first turn to complete"
        );
        loadSystemPrompt.resetHistory();

        await switchSmartbarModel(browser, "4");
        await TestUtils.waitForCondition(
          () => loadSystemPrompt.called,
          "Wait for the system prompt to reload"
        );
        Assert.deepEqual(
          loadSystemPrompt.lastCall.args[0],
          { model: MOCK_AUTO_RS_RECORD.inference_model },
          "Switching to Auto loads the routed model's system prompt"
        );
      }
    );
  } finally {
    sandbox.restore();
  }
});
