/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

describe("Multi-stage onboarding", () => {
  let sandbox;
  let openLinkIn;
  let tab;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    Services.fog.testResetFOG();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.firstrun.singlePageWithAutoModel", false],
        ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
        ["browser.smartwindow.firstrun.modelChoice", ""],
        ["browser.smartwindow.firstrun.hasCompleted", false],
        ["browser.smartwindow.isDefaultWindow", false],
      ],
    });
    openLinkIn = sandbox.stub(window, "openLinkIn").returns(null);
  });

  afterEach(async () => {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
      tab = null;
    }
    sandbox.restore();
    await SpecialPowers.flushPrefEnv();
  });

  it("promotes and preselects Mistral in Europe", async () => {
    withHomeRegion("DE");

    tab = await openFirstrunPage();

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
  });

  it("doesn't promote or preselect a model outside Europe", async () => {
    // "US" is outside the EEA, so there should be no promoted treatment.
    withHomeRegion("US");

    tab = await openFirstrunPage();

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
  });

  it("starts on the intro screen", async () => {
    // The default auto-advance keeps the intro up long enough to be seen. The
    // other tests reach the model choice screen through the intro advancing.
    await SpecialPowers.pushPrefEnv({
      clear: [["browser.smartwindow.firstrun.autoAdvanceMS"]],
    });

    tab = await openFirstrunPage();

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await ContentTaskUtils.waitForMutationCondition(
        content.document.documentElement,
        { childList: true, subtree: true },
        () => content.document.querySelector(".screen.AI_WINDOW_INTRO")
      );

      Assert.ok(
        content.document.querySelector(".screen.AI_WINDOW_INTRO"),
        "The intro screen with class 'screen AI_WINDOW_INTRO' should be present"
      );
    });
  });

  it("records each screen's choices in telemetry", async () => {
    tab = await openFirstrunPage();

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const root = content.document.documentElement;

      await ContentTaskUtils.waitForMutationCondition(
        root,
        { childList: true, subtree: true, attributes: true },
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_CHOOSE_MODEL .select-item:has(input[value='model_2'])"
          )
      );
      const model2Box = content.document.querySelector(
        ".screen.AI_WINDOW_CHOOSE_MODEL .select-item:has(input[value='model_2'])"
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
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_MEMORIES #additional_button"
          )
      );
      const memoriesNextButton = content.document.querySelector(
        ".screen.AI_WINDOW_MEMORIES #additional_button"
      );
      Assert.ok(memoriesNextButton, "Next button exists on memories screen");

      EventUtils.synthesizeMouseAtCenter(memoriesNextButton, {}, content);

      await ContentTaskUtils.waitForMutationCondition(
        root,
        { childList: true, subtree: true, attributes: true },
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_SET_DEFAULT #additional_button"
          )
      );
      const letsGoButton = content.document.querySelector(
        ".screen.AI_WINDOW_SET_DEFAULT #additional_button"
      );
      Assert.ok(letsGoButton, "Let's go button exists on set default screen");

      EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
    });

    await TestUtils.waitForCondition(
      () => openLinkIn.called,
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
  });

  it("records deselected choices as empty sources", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.memories.generateFromHistory", true],
        ["browser.smartwindow.memories.generateFromConversation", true],
      ],
    });

    tab = await openFirstrunPage();

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const root = content.document.documentElement;

      await ContentTaskUtils.waitForMutationCondition(
        root,
        { childList: true, subtree: true, attributes: true },
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_CHOOSE_MODEL .select-item:has(input[value='model_2'])"
          )
      );
      const model2Box = content.document.querySelector(
        ".screen.AI_WINDOW_CHOOSE_MODEL .select-item:has(input[value='model_2'])"
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
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_MEMORIES #memories-browsing"
          )
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

      // Uncheck the default-checked set default checkbox.
      await ContentTaskUtils.waitForMutationCondition(
        root,
        { childList: true, subtree: true, attributes: true },
        () =>
          content.document.querySelector(
            ".screen.AI_WINDOW_SET_DEFAULT #set-default-window"
          )
      );
      const setDefault = content.document.querySelector(
        ".screen.AI_WINDOW_SET_DEFAULT #set-default-window"
      );
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

      const letsGoButton = content.document.querySelector(
        ".screen.AI_WINDOW_SET_DEFAULT #additional_button"
      );
      Assert.ok(letsGoButton, "Let's go button exists on set default screen");
      EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
    });

    await TestUtils.waitForCondition(
      () => openLinkIn.called,
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
  });

  describe("with hidden screens", () => {
    it("finishes on the memories screen when set default is hidden", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          [
            "browser.smartwindow.firstrun.hiddenOnboardingScreenIds",
            "AI_WINDOW_SET_DEFAULT",
          ],
        ],
      });

      tab = await openFirstrunPage();

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        const root = content.document.documentElement;

        await ContentTaskUtils.waitForMutationCondition(
          root,
          { childList: true, subtree: true, attributes: true },
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
            )
        );
        const modelBox = content.document.querySelector(
          ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
        );
        const nextButton = content.document.querySelector(
          ".action-buttons > button"
        );
        EventUtils.synthesizeMouseAtCenter(modelBox, {}, content);
        EventUtils.synthesizeMouseAtCenter(nextButton, {}, content);

        // Memories is now the terminal screen, so its advance button finishes
        // onboarding instead of navigating to the hidden set default screen.
        await ContentTaskUtils.waitForMutationCondition(
          root,
          { childList: true, subtree: true, attributes: true },
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_MEMORIES #additional_button"
            )
        );
        const finishButton = content.document.querySelector(
          ".screen.AI_WINDOW_MEMORIES #additional_button"
        );
        Assert.ok(finishButton, "Advance button exists on the memories screen");
        EventUtils.synthesizeMouseAtCenter(finishButton, {}, content);
      });

      await TestUtils.waitForCondition(
        () =>
          Services.prefs.getBoolPref(
            "browser.smartwindow.firstrun.hasCompleted"
          ),
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
    });

    it("still shows the non-removable screens", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          [
            "browser.smartwindow.firstrun.hiddenOnboardingScreenIds",
            "AI_WINDOW_CHOOSE_MODEL,AI_WINDOW_MEMORIES",
          ],
        ],
      });

      tab = await openFirstrunPage();

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        const root = content.document.documentElement;

        // The model choice screen is non-removable, so it renders despite being
        // listed in hiddenOnboardingScreenIds.
        await ContentTaskUtils.waitForMutationCondition(
          root,
          { childList: true, subtree: true, attributes: true },
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
            )
        );
        const modelBox = content.document.querySelector(
          ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
        );
        Assert.ok(
          content.document.querySelector(".screen.AI_WINDOW_CHOOSE_MODEL"),
          "The non-removable model choice screen is shown"
        );

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
    });
  });

  describe("when launched in a Smart Window", () => {
    let restoreSignIn;

    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.enabled", true]],
      });
      restoreSignIn = skipSignIn();
    });

    afterEach(() => {
      AIWindow.toggleAIWindow(window, false);
      restoreSignIn();
    });

    it("opens the explainer page in a tab when finished", async () => {
      const explainerURL = "https://example.com/";
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.explainerURL", explainerURL]],
      });
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );

      await AIWindow.launchWindow(tab.linkedBrowser);
      await TestUtils.waitForCondition(
        () => tab.linkedBrowser.currentURI.spec === FIRSTRUN_URL,
        "Should navigate to firstrun.html"
      );

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        const root = content.document.documentElement;

        await ContentTaskUtils.waitForMutationCondition(
          root,
          { childList: true, subtree: true, attributes: true },
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
            )
        );
        const model1Box = content.document.querySelector(
          ".screen.AI_WINDOW_CHOOSE_MODEL .select-item"
        );
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
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_MEMORIES #additional_button"
            )
        );
        const memoriesNextButton = content.document.querySelector(
          ".screen.AI_WINDOW_MEMORIES #additional_button"
        );
        Assert.ok(memoriesNextButton, "Next button exists on memories screen");

        EventUtils.synthesizeMouseAtCenter(memoriesNextButton, {}, content);

        await ContentTaskUtils.waitForMutationCondition(
          root,
          { childList: true, subtree: true, attributes: true },
          () =>
            content.document.querySelector(
              ".screen.AI_WINDOW_SET_DEFAULT #additional_button"
            )
        );
        const letsGoButton = content.document.querySelector(
          ".screen.AI_WINDOW_SET_DEFAULT #additional_button"
        );
        Assert.ok(letsGoButton, "Let's go button exists on set default screen");

        EventUtils.synthesizeMouseAtCenter(letsGoButton, {}, content);
      });

      await TestUtils.waitForCondition(
        () => openLinkIn.called,
        "openLinkIn function was called"
      );
      const [url, where] = openLinkIn.firstCall.args;
      Assert.ok(
        url.includes(explainerURL),
        "openLinkIn function was called with the explainer URL"
      );
      Assert.equal(where, "tab", "openLinkIn function opened in a tab");
    });
  });
});
