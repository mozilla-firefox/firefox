/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_PAGE =
  "chrome://mochitests/content/browser/browser/components/aiwindow/ui/test/browser/test_smartwindow_onboarding_page.html";

describe("smartwindow-onboarding", () => {
  let tab;
  let browser;

  beforeEach(async () => {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_PAGE);
    browser = tab.linkedBrowser;
    await SpecialPowers.spawn(browser, [], async () => {
      await content.customElements.whenDefined("smartwindow-onboarding");
    });
  });

  afterEach(() => BrowserTestUtils.removeTab(tab));

  it("automatically advances from welcome and focuses the setup heading", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const onboarding = content.document.createElement(
        "smartwindow-onboarding"
      );
      onboarding.autoAdvanceMS = 100;
      content.document.body.append(onboarding);
      await onboarding.updateComplete;
      Assert.ok(
        onboarding.shadowRoot.getElementById("welcome-title"),
        "Starts on welcome"
      );
      await ContentTaskUtils.waitForCondition(
        () => onboarding.shadowRoot.activeElement?.id === "setup-title",
        "Setup heading receives focus after advancing"
      );
      Assert.ok(
        !onboarding.shadowRoot.getElementById("welcome-title"),
        "Welcome is replaced"
      );
    });
  });

  it("shows supplied choices and emits the edited choices only on Save", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const onboarding = content.document.createElement(
        "smartwindow-onboarding"
      );
      Object.assign(onboarding, {
        autoAdvanceMS: 0,
        modelChoiceId: "3",
        memoriesChoice: "chats",
        isDefaultWindow: false,
      });
      let saved = [];
      content.document.addEventListener("smartwindow-onboarding:save", event =>
        saved.push(event.detail)
      );
      content.document.body.append(onboarding);
      await ContentTaskUtils.waitForCondition(
        () => onboarding.shadowRoot?.getElementById("save"),
        "Setup is rendered"
      );
      const model = onboarding.shadowRoot.getElementById("model");
      const memories = onboarding.shadowRoot.getElementById("memories");
      const toggle = onboarding.shadowRoot.getElementById("default-window");
      await Promise.all([
        model.updateComplete,
        memories.updateComplete,
        toggle.updateComplete,
      ]);
      Assert.equal(model.value, "3", "Supplied model is selected");
      Assert.equal(
        memories.value,
        "chats",
        "Supplied memories choice is selected"
      );
      Assert.ok(!toggle.pressed, "Supplied default-window choice is shown");
      model.button.click();
      await ContentTaskUtils.waitForCondition(
        () => model.panelList.open,
        "Model menu is open"
      );
      model.shadowRoot.querySelector('[data-value="2"]').click();
      memories.button.click();
      await ContentTaskUtils.waitForCondition(
        () => memories.panelList.open,
        "Memories menu is open"
      );
      memories.shadowRoot.querySelector('[data-value="none"]').click();
      toggle.click();
      await toggle.updateComplete;
      await onboarding.updateComplete;
      Assert.equal(saved.length, 0, "Editing choices does not save");
      onboarding.shadowRoot.getElementById("save").click();
      Assert.deepEqual(
        saved,
        [
          {
            modelChoiceId: "2",
            memoriesChoice: "none",
            isDefaultWindow: true,
          },
        ],
        "Save bubbles with all edited choices"
      );
    });
  });

  it("saves the defaults when no choices are supplied", async () => {
    await SpecialPowers.spawn(browser, [], async () => {
      const onboarding = content.document.createElement(
        "smartwindow-onboarding"
      );
      onboarding.autoAdvanceMS = 0;
      content.document.body.append(onboarding);
      await ContentTaskUtils.waitForCondition(
        () => onboarding.shadowRoot?.getElementById("save"),
        "Setup is rendered"
      );
      const saved = new Promise(resolve =>
        onboarding.addEventListener("smartwindow-onboarding:save", resolve, {
          once: true,
        })
      );
      onboarding.shadowRoot.getElementById("save").click();
      Assert.deepEqual(
        (await saved).detail,
        {
          modelChoiceId: "4",
          memoriesChoice: "both",
          isDefaultWindow: true,
        },
        "Save includes the default choices"
      );
    });
  });
});
