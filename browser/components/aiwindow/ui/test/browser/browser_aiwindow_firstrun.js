/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineLazyGetter(this, "SidebarTestUtils", () => {
  const { SidebarTestUtils: utils } = ChromeUtils.importESModule(
    "resource://testing-common/SidebarTestUtils.sys.mjs"
  );
  utils.init(this);
  return utils;
});

async function openFirstrunPage() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    FIRSTRUN_URL
  );
  return tab;
}

function withHomeRegion(region) {
  const originalRegion = Region.home;
  Region._setHomeRegion(region, false);
  registerCleanupFunction(() => {
    if (originalRegion) {
      Region._setHomeRegion(originalRegion, false);
    } else {
      Services.prefs.clearUserPref("browser.search.region");
    }
  });
}

add_task(async function test_firstrun_europe_promotes_mistral() {
  withHomeRegion("DE");

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
    ],
  });

  const tab = await openFirstrunPage();

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const firstItem = content.document.querySelectorAll(".select-item")[0];
    Assert.ok(firstItem, "First model card exists");

    await ContentTaskUtils.waitForCondition(
      () => firstItem.querySelector(".flair"),
      "Recommended badge appears on the first (Mistral) card"
    );
    Assert.ok(
      firstItem.querySelector(".flair"),
      "First card has the Recommended flair badge"
    );

    const radio = firstItem.querySelector(".input");
    await ContentTaskUtils.waitForCondition(
      () => radio.checked,
      "First (Mistral) card is preselected by default"
    );
    Assert.ok(radio.checked, "First card is the default selection");
  });

  Assert.equal(
    Services.prefs.getStringPref(
      "browser.smartwindow.firstrun.modelChoice",
      ""
    ),
    "3",
    "Promoted Mistral model is committed as the default choice"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_non_europe_no_promotion() {
  // "US" is outside the EEA, so there should be no promoted treatment.
  withHomeRegion("US");

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
    ],
  });

  const tab = await openFirstrunPage();

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const items = [...content.document.querySelectorAll(".select-item")];
    Assert.ok(items.length, "Model cards exist");
    Assert.ok(
      !items.some(item => item.querySelector(".flair")),
      "No Recommended badge is shown outside Europe"
    );
    Assert.ok(
      !items.some(item => item.querySelector(".input").checked),
      "No model is preselected outside Europe"
    );
  });

  Assert.equal(
    Services.prefs.getStringPref(
      "browser.smartwindow.firstrun.modelChoice",
      ""
    ),
    "",
    "No default model is committed outside Europe"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_welcome_screen_renders() {
  const tab = await openFirstrunPage();

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true },
      () => content.document.querySelector(".screen.AI_WINDOW_INTRO")
    );

    const introScreen = content.document.querySelector(
      ".screen.AI_WINDOW_INTRO"
    );
    Assert.ok(
      introScreen,
      "The intro screen with class 'screen AI_WINDOW_INTRO' should be present"
    );

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_launchWindow_shows_firstrun_when_not_completed() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", false],
    ],
  });

  const restoreSignIn = skipSignIn();

  // Ensure we start in classic mode
  document.documentElement.removeAttribute("ai-window");

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await AIWindow.launchWindow(gBrowser.selectedBrowser);

  await TestUtils.waitForCondition(
    () => gBrowser.selectedBrowser.currentURI.spec === FIRSTRUN_URL,
    "Should navigate to firstrun.html"
  );

  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    FIRSTRUN_URL,
    "launchWindow should load firstrun.html when firstrun not completed"
  );

  // Cleanup
  AIWindow.toggleAIWindow(window, false);
  restoreSignIn();
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_switcher_shows_firstrun_when_not_completed() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", false],
    ],
  });

  const restoreSignIn = skipSignIn();

  // Ensure we start in classic mode
  document.documentElement.removeAttribute("ai-window");

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  // Open the switcher panel and click "Switch to AI Window"
  let button = document.getElementById("ai-window-toggle");
  let view = PanelMultiView.getViewNode(document, "ai-window-toggle-view");

  let viewShownPromise = BrowserTestUtils.waitForEvent(view, "ViewShown");
  button.click();
  await viewShownPromise;

  let aiButton = view.querySelector("#ai-window-switch-ai");
  aiButton.click();

  await TestUtils.waitForCondition(
    () => document.documentElement.hasAttribute("ai-window"),
    "Window should have ai-window attribute after switching"
  );

  await TestUtils.waitForCondition(
    () => gBrowser.selectedBrowser.currentURI.spec === FIRSTRUN_URL,
    "Should navigate to firstrun.html"
  );

  Assert.equal(
    gBrowser.selectedBrowser.currentURI.spec,
    FIRSTRUN_URL,
    "Switcher should load firstrun.html when firstrun not completed"
  );

  await TestUtils.waitForCondition(
    () => PanelUI.panel.state === "closed",
    "Panel should close after switching"
  );

  // Cleanup
  AIWindow.toggleAIWindow(window, false);
  restoreSignIn();
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(
  async function test_launchWindow_opens_new_window_with_firstrun_when_not_completed() {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.enabled", true],
        ["browser.smartwindow.firstrun.hasCompleted", false],
      ],
    });

    const restoreSignIn = skipSignIn();

    document.documentElement.removeAttribute("ai-window");

    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );

    const newWindowPromise = BrowserTestUtils.waitForNewWindow({
      url: FIRSTRUN_URL,
    });
    await AIWindow.launchWindow(gBrowser.selectedBrowser, true);
    const newWindow = await newWindowPromise;

    Assert.equal(
      newWindow.gBrowser.selectedBrowser.currentURI.spec,
      FIRSTRUN_URL,
      "launchWindow with openNewWindow=true should load firstrun.html when firstrun not completed"
    );

    await TestUtils.waitForCondition(
      () => newWindow.document.documentElement.hasAttribute("ai-window"),
      "New window should have ai-window attribute after authorization"
    );

    Assert.ok(
      newWindow.document.documentElement.hasAttribute("ai-window"),
      "New window should be in AI Window mode"
    );

    await BrowserTestUtils.closeWindow(newWindow);
    AIWindow.toggleAIWindow(window, false);
    restoreSignIn();
    BrowserTestUtils.removeTab(tab);
    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_firstrun_explainer_page_opens() {
  const explainerPref = "browser.smartwindow.firstrun.explainerURL";
  const exampleURL = "https://example.com/";

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", false],
      ["browser.smartwindow.firstrun.modelChoice", ""],
      [explainerPref, exampleURL],
    ],
  });

  const restoreSignIn = skipSignIn();

  const explainerUrlPref = Services.prefs.getStringPref(
    explainerPref,
    exampleURL
  );

  const win = Services.wm.getMostRecentWindow("navigator:browser");
  let calls = [];
  const originalOpenLinkIn = win.openLinkIn;

  win.openLinkIn = function (url, where, params) {
    calls.push({ url, where, params });
    return null;
  };

  const aiWindowTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  await AIWindow.launchWindow(gBrowser.selectedBrowser);

  await TestUtils.waitForCondition(
    () => gBrowser.selectedBrowser.currentURI.spec === FIRSTRUN_URL,
    "Should navigate to firstrun.html"
  );

  const browser = aiWindowTab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const model1Box = content.document.querySelectorAll(".select-item")[0];
    const nextButton = content.document.querySelector(
      ".action-buttons > button"
    );

    Assert.ok(model1Box, "Model 1 box exists");
    Assert.ok(nextButton, "Next button exists");

    EventUtils.synthesizeMouseAtCenter(model1Box, {}, content);
    EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_MEMORIES")
    );

    const memoriesNextButton =
      content.document.getElementById("additional_button");
    Assert.ok(memoriesNextButton, "Next button exists on memories screen");

    EventUtils.synthesizeMouseAtCenter(memoriesNextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_SET_DEFAULT")
    );

    const letsGoButton = content.document.getElementById("additional_button");
    Assert.ok(letsGoButton, "Let's go button exists on set default screen");

    EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
  });

  await TestUtils.waitForCondition(
    () => calls.length,
    "openLinkIn function was called"
  );

  const call = calls[0];

  Assert.ok(
    call.url.includes(explainerUrlPref),
    "openLinkIn function was called with the explainer URL"
  );

  Assert.equal(
    call.where,
    "tab",
    "openLinkIn function opened in a background tab"
  );

  // Clean up
  AIWindow.toggleAIWindow(window, false);
  restoreSignIn();
  BrowserTestUtils.removeTab(aiWindowTab);
  win.openLinkIn = originalOpenLinkIn;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_telemetry() {
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
    ],
  });

  const win = Services.wm.getMostRecentWindow("navigator:browser");
  let calls = [];
  const originalOpenLinkIn = win.openLinkIn;
  win.openLinkIn = function (url, where, params) {
    calls.push({ url, where, params });
    return null;
  };

  const tab = await openFirstrunPage();
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const model2Box = content.document.querySelector(
      ".select-item:has(input[value='model_2'])"
    );
    const nextButton = content.document.querySelector(
      ".action-buttons > button"
    );

    Assert.ok(model2Box, "Model 2 box exists");
    Assert.ok(nextButton, "Next button exists");

    EventUtils.synthesizeMouseAtCenter(model2Box, {}, content);
    EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_MEMORIES")
    );

    const memoriesNextButton =
      content.document.getElementById("additional_button");
    Assert.ok(memoriesNextButton, "Next button exists on memories screen");

    EventUtils.synthesizeMouseAtCenter(memoriesNextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_SET_DEFAULT")
    );

    const letsGoButton = content.document.getElementById("additional_button");
    Assert.ok(letsGoButton, "Let's go button exists on set default screen");

    EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
  });

  await TestUtils.waitForCondition(
    () => calls.length,
    "openLinkIn was called after onboarding completion"
  );

  const impressionEvents =
    Glean.smartWindow.onboardingScreenImpression.testGetValue();
  Assert.equal(
    impressionEvents?.length,
    4,
    "Four screen impression events were recorded"
  );
  Assert.ok(
    impressionEvents[0].extra.message_id.includes("AI_WINDOW_INTRO"),
    "First impression is for AI_WINDOW_INTRO"
  );
  Assert.ok(
    impressionEvents[1].extra.message_id.includes("AI_WINDOW_CHOOSE_MODEL"),
    "Second impression is for AI_WINDOW_CHOOSE_MODEL"
  );
  Assert.ok(
    impressionEvents[2].extra.message_id.includes("AI_WINDOW_MEMORIES"),
    "Third impression is for AI_WINDOW_MEMORIES"
  );
  Assert.ok(
    impressionEvents[3].extra.message_id.includes("AI_WINDOW_SET_DEFAULT"),
    "Fourth impression is for AI_WINDOW_SET_DEFAULT"
  );

  const modelSelectedEvents =
    Glean.smartWindow.onboardingModelSelected.testGetValue();
  Assert.equal(
    modelSelectedEvents?.length,
    1,
    "One model selected event was recorded"
  );
  Assert.equal(
    modelSelectedEvents[0].extra.model,
    "2",
    "Model selected event records 2 for model 2"
  );

  const modelNavigateEvents =
    Glean.smartWindow.onboardingModelNavigate.testGetValue();
  Assert.equal(
    modelNavigateEvents?.length,
    1,
    "One model navigate event was recorded"
  );
  Assert.equal(
    modelNavigateEvents[0].extra.model,
    "2",
    "Model navigate event records 2 for model 2"
  );

  const memoriesNavigateEvents =
    Glean.smartWindow.onboardingMemoriesNavigate.testGetValue();
  Assert.equal(
    memoriesNavigateEvents?.length,
    1,
    "One memories navigate event was recorded"
  );
  Assert.equal(
    memoriesNavigateEvents[0].extra.source,
    "memories-chats,memories-browsing",
    "Memories navigate event records both default-checked checkbox ids"
  );

  const memoriesSettingsEvents =
    Glean.smartWindow.onboardingMemoriesSettings.testGetValue();
  Assert.equal(
    memoriesSettingsEvents?.length,
    1,
    "One memories settings event was recorded"
  );
  Assert.equal(
    memoriesSettingsEvents[0].extra.source,
    "memories-chats,memories-browsing",
    "Memories settings event records both default-checked checkbox ids"
  );

  const setdefaultNavigateEvents =
    Glean.smartWindow.onboardingSetdefaultNavigate.testGetValue();
  Assert.equal(
    setdefaultNavigateEvents?.length,
    1,
    "One set default navigate event was recorded"
  );
  Assert.equal(
    setdefaultNavigateEvents[0].extra.source,
    "set-default-window",
    "Set default navigate event records the default-checked checkbox id"
  );

  const setdefaultSettingsEvents =
    Glean.smartWindow.onboardingSetdefaultSettings.testGetValue();
  Assert.equal(
    setdefaultSettingsEvents?.length,
    1,
    "One set default settings event was recorded"
  );
  Assert.equal(
    setdefaultSettingsEvents[0].extra.source,
    "set-default-window",
    "Set default settings event records the default-checked checkbox id"
  );

  const completeEvents = Glean.smartWindow.onboardingComplete.testGetValue();
  Assert.equal(
    completeEvents?.length,
    1,
    "One onboarding complete event was recorded"
  );
  Assert.equal(
    completeEvents[0].extra.model,
    "2",
    "Onboarding complete event records the final selected model"
  );
  Assert.equal(
    completeEvents[0].extra.memory_source,
    "memories-chats,memories-browsing",
    "Onboarding complete event records the final memories selection"
  );
  Assert.equal(
    completeEvents[0].extra.setdefault_source,
    "set-default-window",
    "Onboarding complete event records the final set default selection"
  );

  BrowserTestUtils.removeTab(tab);
  win.openLinkIn = originalOpenLinkIn;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_hidden_screen_removed() {
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
      ["browser.smartwindow.firstrun.hasCompleted", false],
      [
        "browser.smartwindow.firstrun.hiddenOnboardingScreenIds",
        "AI_WINDOW_SET_DEFAULT",
      ],
    ],
  });

  // The finish action commits this pref; clear it explicitly so it doesn't leak.
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("browser.smartwindow.firstrun.hasCompleted")
  );

  const tab = await openFirstrunPage();

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const modelBox = content.document.querySelectorAll(".select-item")[0];
    const nextButton = content.document.querySelector(
      ".action-buttons > button"
    );
    EventUtils.synthesizeMouseAtCenter(modelBox, {}, content);
    EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_MEMORIES")
    );

    // Memories is now the terminal screen, so its advance button finishes
    // onboarding instead of navigating to the hidden set default screen.
    const finishButton = content.document.getElementById("additional_button");
    Assert.ok(finishButton, "Advance button exists on the memories screen");
    EventUtils.synthesizeMouseAtCenter(finishButton, {}, content);
  });

  await TestUtils.waitForCondition(
    () =>
      Services.prefs.getBoolPref("browser.smartwindow.firstrun.hasCompleted"),
    "First run is marked complete by the relabeled memories button"
  );

  const impressionEvents =
    Glean.smartWindow.onboardingScreenImpression.testGetValue();
  Assert.equal(
    impressionEvents?.length,
    3,
    "Only three screens are shown when the set default screen is hidden"
  );
  Assert.ok(
    !impressionEvents.some(event =>
      event.extra.message_id.includes("AI_WINDOW_SET_DEFAULT")
    ),
    "The hidden set default screen is never shown"
  );

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_non_removable_screens_not_hidden() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
      [
        "browser.smartwindow.firstrun.hiddenOnboardingScreenIds",
        "AI_WINDOW_CHOOSE_MODEL,AI_WINDOW_MEMORIES",
      ],
    ],
  });

  const tab = await openFirstrunPage();

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const root = content.document.documentElement;

    // The model choice screen is non-removable, so it renders despite being
    // listed in hiddenOnboardingScreenIds.
    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );
    Assert.ok(
      content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL"),
      "The non-removable model choice screen is shown"
    );

    const modelBox = content.document.querySelectorAll(".select-item")[0];
    const nextButton = content.document.querySelector(
      ".action-buttons > button"
    );
    EventUtils.synthesizeMouseAtCenter(modelBox, {}, content);
    EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

    // The memories screen is non-removable too, so navigation reaches it.
    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_MEMORIES")
    );
    Assert.ok(
      content.document.querySelector(".screen.AI_WINDOW_MEMORIES"),
      "The non-removable memories screen is shown"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_firstrun_telemetry_unchecked() {
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
      ["browser.smartwindow.firstrun.modelChoice", ""],
      ["browser.smartwindow.memories.generateFromHistory", true],
      ["browser.smartwindow.memories.generateFromConversation", true],
    ],
  });

  // The firstrun flow runs TelemetryManager, which writes a user value to this
  // pref. popPrefEnv won't revert a pref it never set, so clear it explicitly to
  // avoid a "changed preference" leak.
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("browser.smartwindow.lastLLMTelemetryRunTime")
  );

  const win = Services.wm.getMostRecentWindow("navigator:browser");
  let calls = [];
  const originalOpenLinkIn = win.openLinkIn;
  win.openLinkIn = function (url, where, params) {
    calls.push({ url, where, params });
    return null;
  };

  const tab = await openFirstrunPage();
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const root = content.document.documentElement;

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL")
    );

    const model2Box = content.document.querySelector(
      ".select-item:has(input[value='model_2'])"
    );
    const nextButton = content.document.querySelector(
      ".action-buttons > button"
    );

    Assert.ok(model2Box, "Model 2 box exists");
    Assert.ok(nextButton, "Next button exists");

    EventUtils.synthesizeMouseAtCenter(model2Box, {}, content);
    EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_MEMORIES")
    );

    // Uncheck both default-checked memories checkboxes.
    const memoriesChats = content.document.getElementById("memories-chats");
    const memoriesBrowsing =
      content.document.getElementById("memories-browsing");
    Assert.ok(memoriesChats, "Memories chats checkbox exists");
    Assert.ok(memoriesBrowsing, "Memories browsing checkbox exists");

    // The MultiSelect component applies its default-checked state in a mount
    // effect, so wait for both checkboxes to be checked before clicking to
    // uncheck them, otherwise the click can be undone by the initial state.
    await ContentTaskUtils.waitForCondition(
      () => memoriesChats.checked && memoriesBrowsing.checked,
      "Memories checkboxes are checked by default"
    );

    memoriesChats.click();
    await ContentTaskUtils.waitForCondition(
      () => !memoriesChats.checked,
      "Memories chats checkbox is unchecked"
    );
    memoriesBrowsing.click();
    await ContentTaskUtils.waitForCondition(
      () => !memoriesBrowsing.checked,
      "Memories browsing checkbox is unchecked"
    );

    const memoriesNextButton =
      content.document.getElementById("additional_button");
    Assert.ok(memoriesNextButton, "Next button exists on memories screen");
    EventUtils.synthesizeMouseAtCenter(memoriesNextButton, {}, content);

    await ContentTaskUtils.waitForMutationCondition(
      root,
      { childList: true, subtree: true, attributes: true },
      () => content.document.querySelector(".screen.AI_WINDOW_SET_DEFAULT")
    );

    // Uncheck the default-checked set default checkbox.
    const setDefault = content.document.getElementById("set-default-window");
    Assert.ok(setDefault, "Set default checkbox exists");
    await ContentTaskUtils.waitForCondition(
      () => setDefault.checked,
      "Set default checkbox is checked by default"
    );
    setDefault.click();
    await ContentTaskUtils.waitForCondition(
      () => !setDefault.checked,
      "Set default checkbox is unchecked"
    );

    const letsGoButton = content.document.getElementById("additional_button");
    Assert.ok(letsGoButton, "Let's go button exists on set default screen");
    EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
  });

  await TestUtils.waitForCondition(
    () => calls.length,
    "openLinkIn was called after onboarding completion"
  );

  const memoriesNavigateEvents =
    Glean.smartWindow.onboardingMemoriesNavigate.testGetValue();
  Assert.equal(
    memoriesNavigateEvents?.length,
    1,
    "One memories navigate event was recorded"
  );
  Assert.equal(
    memoriesNavigateEvents[0].extra.source,
    "",
    "Memories navigate event records an empty source when all are deselected"
  );

  const memoriesSettingsEvents =
    Glean.smartWindow.onboardingMemoriesSettings.testGetValue();
  Assert.equal(
    memoriesSettingsEvents[0].extra.source,
    "",
    "Memories settings event records an empty source when all are deselected"
  );

  const setdefaultNavigateEvents =
    Glean.smartWindow.onboardingSetdefaultNavigate.testGetValue();
  Assert.equal(
    setdefaultNavigateEvents?.length,
    1,
    "One set default navigate event was recorded"
  );
  Assert.equal(
    setdefaultNavigateEvents[0].extra.source,
    "",
    "Set default navigate event records an empty source when deselected"
  );

  const setdefaultSettingsEvents =
    Glean.smartWindow.onboardingSetdefaultSettings.testGetValue();
  Assert.equal(
    setdefaultSettingsEvents[0].extra.source,
    "",
    "Set default settings event records an empty source when deselected"
  );

  const completeEvents = Glean.smartWindow.onboardingComplete.testGetValue();
  Assert.equal(
    completeEvents?.length,
    1,
    "One onboarding complete event was recorded"
  );
  Assert.equal(
    completeEvents[0].extra.memory_source,
    "",
    "Onboarding complete event records an empty memories selection"
  );
  Assert.equal(
    completeEvents[0].extra.setdefault_source,
    "",
    "Onboarding complete event records an empty set default selection"
  );

  BrowserTestUtils.removeTab(tab);
  win.openLinkIn = originalOpenLinkIn;
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_toggle_interactive_during_firstrun() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.firstrun.hasCompleted", false]],
  });

  const win = await openAIWindow({ waitForTabURL: FIRSTRUN_URL });

  await TestUtils.waitForCondition(
    () => win.document.documentElement.hasAttribute("aiwindow-first-run"),
    "Root should enter first-run immersive state"
  );

  const toggle = win.document.getElementById("ai-window-toggle");
  Assert.ok(
    toggle.closest("#TabsToolbar-customization-target"),
    "Toggle should live in the tabstrip in horizontal tabs mode"
  );

  const toggleStyle = win.getComputedStyle(toggle);
  Assert.notEqual(
    toggleStyle.pointerEvents,
    "none",
    "Toggle must stay clickable during first run (regression guard)"
  );
  Assert.equal(
    toggleStyle.opacity,
    "1",
    "Toggle must not be dimmed during first run"
  );

  const tabs = win.document.getElementById("tabbrowser-tabs");
  const tabsStyle = win.getComputedStyle(tabs);
  Assert.equal(
    tabsStyle.pointerEvents,
    "none",
    "Other tabstrip items remain dimmed during first run"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

/**
 * The core (non-AI) sidebar's launcher, panel, and their splitters.
 *
 * @param {Window} win
 * @returns {Record<string, Element>}
 */
function getCoreSidebarEls(win) {
  return {
    container: win.document.getElementById("sidebar-container"),
    launcherSplitter: win.document.getElementById("sidebar-launcher-splitter"),
    box: win.document.getElementById("sidebar-box"),
    splitter: win.document.getElementById("sidebar-splitter"),
  };
}

function assertCoreSidebarHidden(els, context) {
  for (const [name, el] of Object.entries(els)) {
    Assert.ok(
      BrowserTestUtils.isHidden(el),
      `#${el.id} (${name}) is hidden ${context}`
    );
  }
}

// In horizontal tabs mode the whole core sidebar - launcher, an open panel, and
// both splitters - is hidden during smart window first run, and comes back once
// first run finishes.
add_task(async function test_core_sidebar_hidden_during_firstrun_horizontal() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", false],
      ["sidebar.verticalTabs", false],
    ],
  });

  const win = await openAIWindow({ waitForTabURL: FIRSTRUN_URL });
  const root = win.document.documentElement;
  const browser = win.gBrowser.selectedBrowser;
  const els = getCoreSidebarEls(win);

  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => root.hasAttribute("aiwindow-first-run")
  );

  // A sidebar panel may already be open when the user is in the smart window.
  // Open one outside of first run so it loads normally, then confirm it is
  // visible before re-entering first run.
  await promiseNavigateAndLoad(browser, "https://example.com/");
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => !root.hasAttribute("aiwindow-first-run")
  );

  await SidebarTestUtils.showPanel(win, "viewBookmarksSidebar");
  Assert.ok(
    BrowserTestUtils.isVisible(els.box),
    "Sidebar panel is visible before first run"
  );

  await promiseNavigateAndLoad(browser, FIRSTRUN_URL);
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => root.hasAttribute("aiwindow-first-run")
  );

  assertCoreSidebarHidden(els, "during first run");

  // Completing first run navigates to the AI Window new tab, which keeps the
  // window in immersive view but clears the first-run state. The core sidebar
  // is gated on first run specifically, so the open panel comes back even
  // though the window is still immersive.
  await promiseNavigateAndLoad(browser, AIWINDOW_URL);
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => !root.hasAttribute("aiwindow-first-run")
  );
  Assert.ok(
    root.hasAttribute("aiwindow-immersive-view"),
    "Window is still in immersive view after first run completes"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(els.box),
    "Sidebar panel is visible again after first run completes"
  );

  SidebarTestUtils.closePanel(win);
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Aborting first run by switching back to a classic window also restores the
// core sidebar.
add_task(async function test_core_sidebar_restored_when_firstrun_aborted() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      ["browser.smartwindow.firstrun.hasCompleted", false],
      ["sidebar.verticalTabs", false],
    ],
  });

  const win = await openAIWindow({ waitForTabURL: FIRSTRUN_URL });
  const root = win.document.documentElement;
  const browser = win.gBrowser.selectedBrowser;
  const els = getCoreSidebarEls(win);

  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => root.hasAttribute("aiwindow-first-run")
  );

  // Open the panel outside of first run so it loads normally.
  await promiseNavigateAndLoad(browser, "https://example.com/");
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => !root.hasAttribute("aiwindow-first-run")
  );
  await SidebarTestUtils.showPanel(win, "viewBookmarksSidebar");

  await promiseNavigateAndLoad(browser, FIRSTRUN_URL);
  await BrowserTestUtils.waitForMutationCondition(
    root,
    { attributes: true },
    () => root.hasAttribute("aiwindow-first-run")
  );
  Assert.ok(
    BrowserTestUtils.isHidden(els.box),
    "Sidebar panel is hidden during first run"
  );

  AIWindow.toggleAIWindow(win, false);
  Assert.ok(
    !root.hasAttribute("ai-window"),
    "Window is back in classic mode after aborting first run"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(els.box),
    "Sidebar panel is visible again after aborting first run"
  );

  SidebarTestUtils.closePanel(win);
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});
