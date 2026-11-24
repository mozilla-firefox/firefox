/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

describe("Single-page onboarding", () => {
  let sandbox;
  let tab;

  beforeEach(async () => {
    sandbox = sinon.createSandbox();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.smartwindow.firstrun.singlePageWithAutoModel", true],
        ["browser.smartwindow.firstrun.autoAdvanceMS", 0],
        ["browser.smartwindow.firstrun.modelChoice", ""],
        ["browser.smartwindow.firstrun.hasCompleted", false],
      ],
    });
  });

  afterEach(async () => {
    if (tab) {
      BrowserTestUtils.removeTab(tab);
      tab = null;
    }
    sandbox.restore();
    await SpecialPowers.flushPrefEnv();
  });

  it("is selected by the pref", async () => {
    tab = await openFirstrunPage();

    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      await ContentTaskUtils.waitForCondition(
        () =>
          content.document
            .querySelector("smartwindow-onboarding")
            ?.shadowRoot?.getElementById("save"),
        "The pref selects the single-page onboarding"
      );
      Assert.ok(
        !content.document.getElementById("multi-stage-message-root"),
        "The old onboarding root is not created"
      );
      Assert.equal(
        content.document.querySelector("smartwindow-onboarding").parentElement
          .id,
        "onboarding-root",
        "The new flow mounts inside the shared container"
      );
      Assert.ok(
        !Array.from(content.document.scripts).some(script =>
          script.src.endsWith("aboutwelcome.bundle.js")
        ),
        "The about:welcome bundle is not loaded"
      );
    });
    Assert.equal(
      Services.prefs.getStringPref("browser.smartwindow.firstrun.modelChoice"),
      "",
      "Showing the UI does not save a model choice"
    );
    Assert.ok(
      !Services.prefs.getBoolPref("browser.smartwindow.firstrun.hasCompleted"),
      "Showing the UI does not complete onboarding"
    );
  });

  describe("defaults", () => {
    beforeEach(async () => {
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.memories.generateFromConversation", true],
          ["browser.smartwindow.memories.generateFromHistory", false],
        ],
      });
    });

    it("to Auto outside the EEA", async () => {
      withHomeRegion("US");

      tab = await openFirstrunPage();

      Assert.deepEqual(
        await getSinglePageOnboardingDefaults(tab),
        {
          modelChoiceId: "4",
          recommendedModelChoiceId: "4",
          memoriesChoice: "chats",
          isDefaultWindow: true,
        },
        "Outside the EEA defaults to Auto, current memories prefs and default window"
      );
    });

    it("to the promoted Mistral model in the EEA", async () => {
      withHomeRegion("DE");

      tab = await openFirstrunPage();

      Assert.deepEqual(
        await getSinglePageOnboardingDefaults(tab),
        {
          modelChoiceId: "3",
          recommendedModelChoiceId: "3",
          memoriesChoice: "chats",
          isDefaultWindow: true,
        },
        "In the EEA the promoted Mistral model is the default"
      );
    });
  });

  describe("when launched in a Smart Window", () => {
    const explainerURL = "https://example.com/";
    let openLinkIn;
    let restoreSignIn;

    beforeEach(async () => {
      Services.fog.testResetFOG();
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.smartwindow.enabled", true],
          ["browser.smartwindow.firstrun.explainerURL", explainerURL],
          ["browser.smartwindow.isDefaultWindow", true],
          ["browser.smartwindow.memories.generateFromConversation", true],
          ["browser.smartwindow.memories.generateFromHistory", true],
        ],
      });
      withHomeRegion("US");
      openLinkIn = sandbox.stub(window, "openLinkIn").returns(null);
      restoreSignIn = skipSignIn();
    });

    afterEach(() => {
      AIWindow.toggleAIWindow(window, false);
      restoreSignIn();
    });

    it("saves the choices and opens the explainer", async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
      await launchFirstrun(tab);
      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        await ContentTaskUtils.waitForCondition(
          () =>
            content.document
              .querySelector("smartwindow-onboarding")
              ?.shadowRoot?.getElementById("save"),
          "Setup is rendered"
        );
        const onboarding = content.document.querySelector(
          "smartwindow-onboarding"
        );
        onboarding.modelChoiceId = "2";
        onboarding.memoriesChoice = "browsing";
        onboarding.isDefaultWindow = false;
        await onboarding.updateComplete;
        onboarding.shadowRoot.getElementById("save").click();
      });

      await TestUtils.waitForCondition(
        () => openLinkIn.called,
        "Explainer opened"
      );
      Assert.deepEqual(
        openLinkIn.args.map(([url, where]) => ({ url, where })),
        [{ url: explainerURL, where: "tab" }],
        "The explainer page opens in a tab"
      );
      Assert.equal(
        Services.prefs.getStringPref(
          "browser.smartwindow.firstrun.modelChoice"
        ),
        "2",
        "Model choice is saved"
      );
      Assert.ok(
        !Services.prefs.getBoolPref(
          "browser.smartwindow.memories.generateFromConversation"
        ),
        "Chats memories are off for browsing only"
      );
      Assert.ok(
        Services.prefs.getBoolPref(
          "browser.smartwindow.memories.generateFromHistory"
        ),
        "Browsing memories are on for browsing only"
      );
      Assert.ok(
        !Services.prefs.getBoolPref("browser.smartwindow.isDefaultWindow"),
        "Default window choice is saved"
      );
      Assert.ok(
        Services.prefs.getBoolPref("browser.smartwindow.firstrun.hasCompleted"),
        "Onboarding is marked completed"
      );
      await TestUtils.waitForCondition(
        () => tab.linkedBrowser.currentURI.spec === AIWindow.newTabURL,
        "Navigates to the Smart Window new tab page"
      );
    });

    it("records changed choices in telemetry", async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.firstrun.autoAdvanceMS", 100]],
      });
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
      await launchFirstrun(tab);

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        await ContentTaskUtils.waitForCondition(
          () =>
            content.document
              .querySelector("smartwindow-onboarding")
              ?.shadowRoot?.getElementById("save"),
          "Setup is rendered"
        );
        const { shadowRoot } = content.document.querySelector(
          "smartwindow-onboarding"
        );
        const model = shadowRoot.getElementById("model");
        const memories = shadowRoot.getElementById("memories");
        const toggle = shadowRoot.getElementById("default-window");
        await Promise.all([
          model.updateComplete,
          memories.updateComplete,
          toggle.updateComplete,
        ]);
        const selectOption = async (select, value) => {
          EventUtils.synthesizeMouseAtCenter(select.button, {}, content);
          await ContentTaskUtils.waitForCondition(
            () => select.panelOpen,
            `${select.id} menu is shown`
          );
          EventUtils.synthesizeMouseAtCenter(
            select.shadowRoot.querySelector(`[data-value="${value}"]`),
            {},
            content
          );
          await ContentTaskUtils.waitForCondition(
            () => !select.panelOpen,
            `${select.id} menu is hidden`
          );
        };
        await selectOption(model, "2");
        await selectOption(memories, "browsing");
        EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, content);
        await toggle.updateComplete;
        EventUtils.synthesizeMouseAtCenter(
          shadowRoot.getElementById("save"),
          {},
          content
        );
      });

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.onboardingComplete.testGetValue()?.length,
        "Completion is recorded"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingScreenImpression
          .testGetValue()
          .map(e => e.extra.message_id),
        ["AI_WINDOW_SINGLE_PAGE_0_WELCOME", "AI_WINDOW_SINGLE_PAGE_1_SETUP"],
        "Welcome and setup impressions are recorded"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingModelSelected
          .testGetValue()
          .map(e => e.extra),
        [{ model: "2" }],
        "Model change is recorded"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingMemoriesSettings
          .testGetValue()
          .map(e => e.extra),
        [{ source: "memories-browsing" }],
        "Memories change is recorded as checkbox ids"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingSetdefaultSettings
          .testGetValue()
          .map(e => e.extra),
        [{ source: "" }],
        "Default window toggle is recorded"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingComplete.testGetValue().map(e => e.extra),
        [
          {
            model: "2",
            memory_source: "memories-browsing",
            setdefault_source: "",
          },
        ],
        "Completion records the final choices"
      );
      for (const navigate of [
        "onboardingModelNavigate",
        "onboardingMemoriesNavigate",
        "onboardingSetdefaultNavigate",
        "onboardingBackNavigate",
      ]) {
        Assert.equal(
          Glean.smartWindow[navigate].testGetValue(),
          null,
          `${navigate} is not recorded`
        );
      }
      await TestUtils.waitForCondition(
        () => tab.linkedBrowser.currentURI.spec === AIWindow.newTabURL,
        "Navigates to the Smart Window new tab page"
      );
    });

    it("records the defaults when the welcome is skipped", async () => {
      tab = await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        "about:blank"
      );
      await launchFirstrun(tab);

      await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
        await ContentTaskUtils.waitForCondition(
          () =>
            content.document
              .querySelector("smartwindow-onboarding")
              ?.shadowRoot?.getElementById("save"),
          "Setup is rendered"
        );
        EventUtils.synthesizeMouseAtCenter(
          content.document
            .querySelector("smartwindow-onboarding")
            .shadowRoot.getElementById("save"),
          {},
          content
        );
      });

      await TestUtils.waitForCondition(
        () => Glean.smartWindow.onboardingComplete.testGetValue()?.length,
        "Completion is recorded"
      );
      Assert.deepEqual(
        Glean.smartWindow.onboardingScreenImpression
          .testGetValue()
          .map(e => e.extra.message_id),
        ["AI_WINDOW_SINGLE_PAGE_1_SETUP"],
        "Only the setup impression is recorded when the welcome is skipped"
      );
      for (const change of [
        "onboardingModelSelected",
        "onboardingMemoriesSettings",
        "onboardingSetdefaultSettings",
      ]) {
        Assert.equal(
          Glean.smartWindow[change].testGetValue(),
          null,
          `${change} is not recorded without a change`
        );
      }
      Assert.deepEqual(
        Glean.smartWindow.onboardingComplete.testGetValue().map(e => e.extra),
        [
          {
            model: "4",
            memory_source: "memories-chats,memories-browsing",
            setdefault_source: "set-default-window",
          },
        ],
        "Completion records both memories sources and the default window"
      );
    });
  });
});

/**
 * Reads the choices the single-page onboarding starts with.
 *
 * @param {MozTabbrowserTab} tab - A tab showing firstrun.html.
 * @returns {Promise<object>}
 */
function getSinglePageOnboardingDefaults(tab) {
  return SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document
          .querySelector("smartwindow-onboarding")
          ?.shadowRoot?.getElementById("save"),
      "Setup is rendered"
    );
    const onboarding = content.document.querySelector("smartwindow-onboarding");
    return {
      recommendedModelChoiceId: onboarding.shadowRoot
        .getElementById("model")
        .options.find(option => option.secondaryLabelL10nId)?.value,
      modelChoiceId: onboarding.modelChoiceId,
      memoriesChoice: onboarding.memoriesChoice,
      isDefaultWindow: onboarding.isDefaultWindow,
    };
  });
}

/**
 * Turns the test window into a Smart Window from the tab, which then shows
 * first run because it hasn't completed.
 *
 * @param {MozTabbrowserTab} tab
 */
async function launchFirstrun(tab) {
  await AIWindow.launchWindow(tab.linkedBrowser);
  await TestUtils.waitForCondition(
    () => tab.linkedBrowser.currentURI.spec === FIRSTRUN_URL,
    "Navigated to firstrun.html"
  );
}
