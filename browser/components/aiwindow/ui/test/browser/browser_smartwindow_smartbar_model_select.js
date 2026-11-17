/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the input-model-select component in the Smartbar.
 */

"use strict";

const {
  _clearModelsDataCacheForTesting,
  AUTO_MODEL_CHOICE_ID,
  FALLBACK_MODELS,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
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
 * Waits for the expected model in the smartbar model select.
 *
 * @param {MozBrowser} browser - The browser element
 * @param {string} expectedModel - The expected model name
 */
async function waitForSelectedModel(browser, expectedModel) {
  await SpecialPowers.spawn(browser, [expectedModel], async expected => {
    const aiWindowElement = content.document.querySelector("ai-window");
    await ContentTaskUtils.waitForMutationCondition(
      aiWindowElement,
      { attributes: true },
      () => aiWindowElement.selectedModelId === expected
    );
  });
}

/**
 * Opens the model select panel, returns the choice IDs of the items showing the
 * selected-model checkmark, then closes the panel again.
 *
 * @param {MozBrowser} browser - The browser element
 * @returns {Promise<string[]>} Choice IDs of items with a checkmark
 */
async function getCheckedModelChoiceIds(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const modelSelect = smartbar.querySelector("input-model-select");
    const triggerButton = modelSelect.shadowRoot.querySelector("moz-button");
    const panelList = modelSelect.shadowRoot.querySelector("panel-list");

    // Selecting a model leaves the panel open, so only toggle it open when it
    // is currently closed.
    if (!panelList.hasAttribute("open")) {
      triggerButton.click();
      await ContentTaskUtils.waitForMutationCondition(
        panelList,
        { attributes: true },
        () => panelList.hasAttribute("open")
      );
    }
    await modelSelect.updateComplete;

    const checkedIds = [];
    for (const item of panelList.querySelectorAll("button.model-item")) {
      if (item.querySelector(".model-item-check")) {
        const iconSrc = item
          .querySelector(".model-item-icon")
          .getAttribute("src");
        const iconName = iconSrc.match(/model-choice-(.+)\.svg/)?.[1];
        checkedIds.push(
          { 0: "0", gemini: "1", qwen: "2", mistral: "3" }[iconName]
        );
      }
    }

    triggerButton.click();
    await ContentTaskUtils.waitForMutationCondition(
      panelList,
      { attributes: true },
      () => !panelList.hasAttribute("open")
    );

    return checkedIds;
  });
}

add_task(async function test_smartbar_model_select_shows_default_model() {
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });

  try {
    const expectedDefaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const { selectedModelId } = await getSmartbarModelSelectData(browser);
    Assert.equal(
      selectedModelId,
      expectedDefaultModel,
      "Should show the default model based on modelChoice pref"
    );

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await restore();
  }
});

add_task(async function test_smartbar_model_select_panel_shows_default_badge() {
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const defaultItemHasBadge = await SpecialPowers.spawn(
      browser,
      [FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].brandName.toLowerCase()],
      async iconName => {
        const aiWindowElement = content.document.querySelector("ai-window");
        const smartbar = aiWindowElement.shadowRoot.querySelector(
          "#ai-window-smartbar"
        );
        const modelSelect = smartbar.querySelector("input-model-select");
        const triggerButton =
          modelSelect.shadowRoot.querySelector("moz-button");
        triggerButton.click();

        const panelList = modelSelect.shadowRoot.querySelector("panel-list");
        await ContentTaskUtils.waitForMutationCondition(
          panelList,
          { attributes: true },
          () => panelList.hasAttribute("open")
        );

        const defaultItem = panelList.querySelector(
          `button.model-item:has(.model-item-icon[src*="model-choice-${iconName}.svg"])`
        );
        return !!defaultItem.querySelector("moz-badge");
      }
    );

    Assert.ok(defaultItemHasBadge, "Default model item should have the badge");

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await restore();
  }
});

add_task(
  async function test_smartbar_model_select_check_marks_selected_model() {
    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: null,
    });

    try {
      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      // A fresh tab starts on the user's currently selected model (the saved
      // modelChoice pref), so the check sits on that selection.
      Assert.deepEqual(
        await getCheckedModelChoiceIds(browser),
        [DEFAULT_MODEL_CHOICE_ID],
        "The checkmark should be on the currently selected model"
      );

      // Switching models should move the check to the new selection, proving
      // the check tracks the selected model rather than any fixed default.
      await switchSmartbarModel(browser, "1");
      Assert.deepEqual(
        await getCheckedModelChoiceIds(browser),
        ["1"],
        "The checkmark should follow the newly selected model"
      );

      await BrowserTestUtils.closeWindow(win);
    } finally {
      await restore();
    }
  }
);

add_task(
  async function test_smartbar_model_select_shows_custom_model_when_configured() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.customEndpoint", "https://custom.endpoint/v1"],
      ],
    });

    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: null,
    });

    try {
      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      const { availableModels } = await getSmartbarModelSelectData(browser);

      const hasCustomModel = Object.values(availableModels).some(
        m => m.model === "custom-model"
      );
      Assert.ok(hasCustomModel, "Custom model should show when configured");

      await BrowserTestUtils.closeWindow(win);
    } finally {
      await restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_smartbar_model_select_shows_custom_model_name() {
  const fullName = "hf.co/bartowski/Llama-3.2-1B-Instruct-GGUF:Q4_K_M";
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.customEndpoint", "https://custom.endpoint/v1"],
      ["browser.smartwindow.model", fullName],
      ["browser.smartwindow.firstrun.modelChoice", "0"],
    ],
  });

  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const getCustomModelLabels = () =>
      SpecialPowers.spawn(browser, [], async () => {
        const aiWindowElement = content.document.querySelector("ai-window");
        const smartbar = aiWindowElement.shadowRoot.querySelector(
          "#ai-window-smartbar"
        );
        const modelSelect = smartbar.querySelector("input-model-select");
        await ContentTaskUtils.waitForCondition(
          () => modelSelect.shadowRoot.querySelector("moz-button"),
          "Model select button should render"
        );
        await modelSelect.updateComplete;
        const button = modelSelect.shadowRoot.querySelector("moz-button");
        const customItemLabel = modelSelect.shadowRoot.querySelector(
          "button.model-item:has(.model-item-icon--custom) .model-item-label"
        );
        return {
          buttonLabel: button.getAttribute("label"),
          buttonTitle: button.shadowRoot
            .querySelector("button")
            .getAttribute("title"),
          buttonL10nId: button.getAttribute("data-l10n-id"),
          itemLabel: customItemLabel.textContent.trim(),
          itemL10nId: customItemLabel.getAttribute("data-l10n-id"),
        };
      });

    let labels = await getCustomModelLabels();
    Assert.equal(
      labels.buttonLabel,
      "Llama",
      "Button should show the first word of the custom model name"
    );
    Assert.equal(
      labels.buttonTitle,
      fullName,
      "Button tooltip should show the full custom model name"
    );
    Assert.equal(
      labels.itemLabel,
      fullName,
      "Menu item should show the full custom model name"
    );

    for (const [name, expected] of [
      ["/models/Qwen2.5-7B-Instruct-Q4_K_M.gguf", "Qwen2.5"],
      ["meta-llama/Llama-3.1-8B-Instruct", "Llama"],
      ["llama3.2:3b", "llama3.2"],
      ["gpt-oss-120b", "gpt"],
      ["mistral", "mistral"],
    ]) {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.model", name]],
      });
      labels = await getCustomModelLabels();
      Assert.equal(
        labels.buttonLabel,
        expected,
        `Button should shorten "${name}" to "${expected}"`
      );
      await SpecialPowers.popPrefEnv();
    }

    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.model", ""]],
    });
    labels = await getCustomModelLabels();
    Assert.equal(
      labels.buttonL10nId,
      "aiwindow-input-model-select-button-label-custom",
      "Button should fall back to the Custom label without a model name"
    );
    Assert.equal(
      labels.buttonTitle,
      null,
      "Button should have no tooltip without a model name"
    );
    Assert.equal(
      labels.itemL10nId,
      "aiwindow-input-model-select-button-label-custom",
      "Menu item should fall back to the Custom label without a model name"
    );
    await SpecialPowers.popPrefEnv();

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_smartbar_model_select_hides_custom_model_without_custom_endpoint() {
    await SpecialPowers.pushPrefEnv({
      clear: [["browser.smartwindow.customEndpoint"]],
    });

    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: null,
    });

    try {
      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      const { availableModels } = await getSmartbarModelSelectData(browser);

      const hasCustomModel = Object.values(availableModels).some(
        m => m.model === "custom-model"
      );
      Assert.ok(
        !hasCustomModel,
        "Custom model should NOT be available without a custom endpoint"
      );

      await BrowserTestUtils.closeWindow(win);
    } finally {
      await restore();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_model_switch_uses_correct_model_for_requests() {
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const { selectedModelId: initialModelId } =
      await getSmartbarModelSelectData(browser);

    await switchSmartbarModel(browser, "1");
    const { selectedModelId: updatedModelId } =
      await getSmartbarModelSelectData(browser);

    Assert.notEqual(
      initialModelId,
      updatedModelId,
      "Model should have changed"
    );

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await restore();
  }
});

add_task(
  async function test_model_switch_updates_system_prompt_mid_conversation() {
    let { promise: requestPromise, resolve: resolveRequest } =
      Promise.withResolvers();

    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: {
        streamChunks: ["Hello", "from mock server."],
        onRequest(body) {
          resolveRequest(body);
          ({ promise: requestPromise, resolve: resolveRequest } =
            Promise.withResolvers());
        },
      },
    });

    try {
      const win = await openAIWindow();
      const browser = win.gBrowser.selectedBrowser;

      await typeInSmartbar(browser, "First message");
      await submitSmartbar(browser);
      const firstRequest = await requestPromise;

      await switchSmartbarModel(browser, "1");

      await typeInSmartbar(browser, "Second message");
      await submitSmartbar(browser);
      const secondRequest = await requestPromise;

      const userMessages = secondRequest.messages
        .filter(message => message.role === "user")
        .map(message => message.content);

      Assert.notEqual(
        firstRequest.model,
        secondRequest.model,
        "Model should change after model switch"
      );
      Assert.ok(
        userMessages.some(message => message.includes("First message")),
        "Should include first message"
      );
      Assert.ok(
        userMessages.some(message => message.includes("Second message")),
        "Should include second message"
      );

      await BrowserTestUtils.closeWindow(win);
    } finally {
      await restore();
    }
  }
);

add_task(async function test_model_switch_routes_to_correct_endpoint() {
  let customServer;
  let stubRestore;
  const customRequests = [];
  const presetRequests = [];

  try {
    const { server, port: customPort } = startMockOpenAI({
      streamChunks: ["Custom reply"],
      onRequest(body) {
        customRequests.push(body);
      },
    });
    customServer = server;
    await SpecialPowers.pushPrefEnv({
      set: [
        [
          "browser.smartwindow.customEndpoint",
          `http://localhost:${customPort}/v1`,
        ],
        ["browser.smartwindow.model", "custom-model"],
        ["browser.smartwindow.apiKey", "custom-key"],
      ],
    });

    const { restore } = await stubEngineNetworkBoundaries({
      serverOptions: {
        streamChunks: ["Preset reply"],
        onRequest(body) {
          presetRequests.push(body);
        },
      },
    });
    stubRestore = restore;

    // Waits for the smartbar to return to idle after a turn completes.
    const waitForIdle = browser =>
      SpecialPowers.spawn(browser, [], async () => {
        const inputCta = ContentTaskUtils.querySelectorDeep(
          content.document,
          "input-cta"
        );
        await ContentTaskUtils.waitForCondition(
          () => inputCta.getAttribute("action") !== "stop",
          "Smartbar should return to idle after generating"
        );
      });

    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    // The custom model choice option "0" is available when configured
    const { availableModels } = await getSmartbarModelSelectData(browser);
    Assert.ok(availableModels["0"], "Custom model is available");

    // Test initial preset turn
    await typeInSmartbar(browser, "Preset message");
    await submitSmartbar(browser);
    await TestUtils.waitForCondition(
      () => presetRequests.length,
      "MLPA endpoint should receive the preset request"
    );
    Assert.equal(
      customRequests.length,
      0,
      "Custom endpoint should not receive the preset request"
    );
    await waitForIdle(browser);

    // Test switch to custom model
    await switchSmartbarModel(browser, "0");
    await typeInSmartbar(browser, "Custom message");
    await submitSmartbar(browser);
    await TestUtils.waitForCondition(
      () => customRequests.length,
      "Custom endpoint should receive the custom-model request"
    );
    Assert.equal(
      presetRequests.length,
      1,
      "Mozilla endpoint must not receive the custom-model request"
    );

    // Custom model choice stays available
    const { availableModels: afterSwitch } =
      await getSmartbarModelSelectData(browser);
    Assert.ok(
      Object.values(afterSwitch).some(m => m.model === "custom-model"),
      "Custom option stays available"
    );

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await stubRestore();
    await SpecialPowers.popPrefEnv();
    await stopMockOpenAI(customServer);
  }
});

add_task(async function test_model_choice_pref_change_updates_selected_model() {
  const { restore } = await stubEngineNetworkBoundaries({
    serverOptions: null,
  });

  try {
    const win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;

    const { selectedModelId: initialModelId } =
      await getSmartbarModelSelectData(browser);
    Assert.equal(
      initialModelId,
      FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model,
      "Should be the initial default model"
    );

    const newModelChoiceId = "1";
    const expectedNewModel = FALLBACK_MODELS[newModelChoiceId].model;
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", newModelChoiceId]],
    });

    await SpecialPowers.spawn(
      browser,
      [expectedNewModel],
      async expectedModel => {
        const aiWindowElement = content.document.querySelector("ai-window");
        await ContentTaskUtils.waitForMutationCondition(
          aiWindowElement,
          { attributes: true },
          () => aiWindowElement.selectedModelId === expectedModel
        );
      }
    );

    const { selectedModelId: updatedModelId } =
      await getSmartbarModelSelectData(browser);
    Assert.equal(
      updatedModelId,
      expectedNewModel,
      "Selected model should update when pref changes"
    );

    await BrowserTestUtils.closeWindow(win);
  } finally {
    await restore();
    await SpecialPowers.popPrefEnv();
  }
});

add_task(
  async function test_model_choice_pref_change_updates_fullpage_and_sidebar_on_default() {
    const mockEngineManager = new MockEngineManager();

    try {
      const win = await openAIWindow();
      const browserFullpage = win.gBrowser.selectedBrowser;

      // Open second tab with sidebar
      await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        "https://example.com/"
      );
      Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
      const browserSidebar = await waitForSidebarReady(win);

      // Both tabs match the default
      const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;
      await waitForSelectedModel(browserFullpage, defaultModel);
      await waitForSelectedModel(browserSidebar, defaultModel);

      // Change the default model
      const newModel = FALLBACK_MODELS["1"].model;
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.modelChoice", "1"]],
      });
      await waitForSelectedModel(browserFullpage, newModel);
      await waitForSelectedModel(browserSidebar, newModel);

      await BrowserTestUtils.closeWindow(win);
    } finally {
      mockEngineManager.cleanupMocks();
      await SpecialPowers.popPrefEnv();
    }
  }
);

add_task(async function test_sidebar_model_switch_persists_across_tabs() {
  const mockEngineManager = new MockEngineManager();

  try {
    const win = await openAIWindow();

    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    const tabB = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    const browserSidebar = await waitForSidebarReady(win);

    const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;
    const fastModel = FALLBACK_MODELS["1"].model;

    // Tab B starts with the default and then overrides
    await waitForSelectedModel(browserSidebar, defaultModel);
    await switchSmartbarModel(browserSidebar, "1");

    // Tab A stays on the default
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, defaultModel);

    // Tab B restores its override choice
    await BrowserTestUtils.switchTab(win.gBrowser, tabB);
    await waitForSelectedModel(browserSidebar, fastModel);

    await BrowserTestUtils.closeWindow(win);
  } finally {
    mockEngineManager.cleanupMocks();
  }
});

// Change default while override tab is focused: Override tab keeps model choice
// and the background tab mirrors default when switching back to tab.
add_task(async function test_default_change_with_override_tab_focused() {
  const mockEngineManager = new MockEngineManager();

  try {
    const win = await openAIWindow();

    // Tab A mirrors the default and tab B overrides model choice
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    // Tab B stays focused as the override tab.
    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    const browserSidebar = await waitForSidebarReady(win);

    await switchSmartbarModel(browserSidebar, "1");

    // Change the default model
    const newDefaultModel = FALLBACK_MODELS["3"].model;
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });

    // Focused override tab B does not change
    Assert.equal(
      (await getSmartbarModelSelectData(browserSidebar)).selectedModelId,
      FALLBACK_MODELS["1"].model,
      "Override tab B should keep its picked model"
    );

    // Tab A updates to the new default on switching back
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, newDefaultModel);

    await BrowserTestUtils.closeWindow(win);
  } finally {
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

// Change default while tab without override is focused: Default tab choice
// changes and the override tab restores its pick when switching back to the tab.
add_task(async function test_default_change_with_default_tab_focused() {
  const mockEngineManager = new MockEngineManager();

  try {
    const win = await openAIWindow();

    // Tab A overrides the default model
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    const browserSidebar = await waitForSidebarReady(win);
    await switchSmartbarModel(browserSidebar, "1");

    // Tab B mirrors the default and stays focused while the default changes
    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );

    // Change the default model
    const newDefaultModel = FALLBACK_MODELS["3"].model;
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });

    // Focused tab B changes to the new default
    await waitForSelectedModel(browserSidebar, newDefaultModel);

    // Background override tab A keeps its model when switching back
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, FALLBACK_MODELS["1"].model);

    await BrowserTestUtils.closeWindow(win);
  } finally {
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
});

// Model override persists even after the global default changes to match.
add_task(async function test_override_persists_when_default_matches() {
  const mockEngineManager = new MockEngineManager();

  try {
    const win = await openAIWindow();

    // Tab A overrides the default model
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    const browserSidebar = await waitForSidebarReady(win);
    await switchSmartbarModel(browserSidebar, "1");

    // Tab B mirrors the default
    await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );

    // Move the default to match the override of tab A
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "1"]],
    });

    // Tab A keeps its override even though it now matches the default
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, FALLBACK_MODELS["1"].model);

    // Change the default model away again
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });
    // Tab A keeps the overridden model instead of following the default
    await waitForSelectedModel(browserSidebar, FALLBACK_MODELS["1"].model);

    await BrowserTestUtils.closeWindow(win);
  } finally {
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.popPrefEnv();
  }
});

// Typing in the smartbar should not persist a model override.
add_task(async function test_smartbar_input_does_not_persist_model_choice() {
  const mockEngineManager = new MockEngineManager();

  try {
    const win = await openAIWindow();

    // Tab A mirrors the default
    const tabA = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.com/"
    );
    Assert.ok(AIWindowUI.isSidebarOpen(win), "Sidebar should be open");
    const browserSidebar = await waitForSidebarReady(win);

    // Type into the smartbar without picking a model from the select
    const defaultModel = FALLBACK_MODELS[DEFAULT_MODEL_CHOICE_ID].model;
    await waitForSelectedModel(browserSidebar, defaultModel);
    await typeInSmartbar(browserSidebar, "Hello there");

    // Open tab B and change the global default
    const tabB = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      "https://example.org/"
    );
    const newDefaultModel = FALLBACK_MODELS["3"].model;
    await SpecialPowers.pushPrefEnv({
      set: [["browser.smartwindow.firstrun.modelChoice", "3"]],
    });
    await waitForSelectedModel(browserSidebar, newDefaultModel);

    // Tab A should mirror the new default
    await BrowserTestUtils.switchTab(win.gBrowser, tabA);
    await waitForSelectedModel(browserSidebar, newDefaultModel);

    await BrowserTestUtils.switchTab(win.gBrowser, tabB);
    await waitForSelectedModel(browserSidebar, newDefaultModel);

    await BrowserTestUtils.closeWindow(win);
  } finally {
    mockEngineManager.cleanupMocks();
    await SpecialPowers.popPrefEnv();
  }
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
