/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.preferences.aiControls", true],
      ["browser.ai.control.default", "available"],
      ["browser.ai.control.smartWindow", "default"],
      ["browser.smartwindow.enabled", true],
    ],
  });
});

describe("settings AI Controls - Smart Window", () => {
  it("reacts to smartwindow.enabled and ai.control prefs", async () => {
    await withPrefsPane("ai", async doc => {
      const smartWindowSelect = doc.getElementById(
        "aiControlSmartWindowSelect"
      );

      // Visible by default when enabled
      Assert.ok(
        BrowserTestUtils.isVisible(smartWindowSelect),
        "aiControlSmartWindowSelect is visible when Smart Window is enabled"
      );

      const fieldset = doc.getElementById("smartWindowFieldset");
      Assert.ok(fieldset, "smartWindowFieldset exists");

      const badge = fieldset.shadowRoot.querySelector("moz-badge");
      Assert.ok(
        BrowserTestUtils.isVisible(badge),
        "moz-badge is visible on Smart Window fieldset"
      );
      Assert.equal(badge.getAttribute("type"), "beta", "badge has type beta");

      // Hidden when browser.smartwindow.enabled is false
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.enabled", false]],
      });
      Assert.ok(
        !BrowserTestUtils.isVisible(smartWindowSelect),
        "Smart Window control is hidden when browser.smartwindow.enabled is false"
      );
      await SpecialPowers.popPrefEnv();

      // Visible when blocked while browser.smartwindow.enabled is true
      await SpecialPowers.pushPrefEnv({
        set: [["browser.ai.control.smartWindow", "blocked"]],
      });
      Assert.ok(
        BrowserTestUtils.isVisible(smartWindowSelect),
        "Smart Window control is visible when blocked"
      );

      const control = doc.getElementById("aiControlSmartWindowSelect");
      await control.updateComplete;

      Assert.equal(
        smartWindowSelect.value,
        "blocked",
        "control shows blocked state"
      );
      await SpecialPowers.popPrefEnv();
    });
  });

  it("updates control when browser.ai.control.smartWindow is set externally to available", async () => {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.ai.control.smartWindow", "blocked"]],
    });
    await withPrefsPane("ai", async doc => {
      const control = doc.getElementById("aiControlSmartWindowSelect");
      await control.updateComplete;
      Assert.equal(control.value, "blocked", "control shows blocked");

      await SpecialPowers.pushPrefEnv({
        set: [["browser.ai.control.smartWindow", "available"]],
      });
      await control.updateComplete;
      Assert.equal(
        control.value,
        "available",
        "control updates to available after external pref change"
      );
      await SpecialPowers.popPrefEnv();
    });
    await SpecialPowers.popPrefEnv();
  });

  it("shows activate link and personalize button based on consent", async () => {
    await withPrefsPane("ai", async doc => {
      const smartWindowActivateLink = doc.getElementById(
        "activateSmartWindowLink"
      );
      const smartWindowPersonalizeButton = doc.getElementById(
        "personalizeSmartWindowButton"
      );

      // Without consent, activate link is visible
      Assert.ok(
        BrowserTestUtils.isVisible(smartWindowActivateLink),
        "smartWindowActivateLink is visible without consent"
      );

      // With consent, activate link is hidden and personalize button is visible
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.tos.consentTime", 1770830464]],
      });
      Assert.ok(
        !BrowserTestUtils.isVisible(smartWindowActivateLink),
        "smartWindowActivateLink is hidden with consent"
      );
      Assert.ok(
        BrowserTestUtils.isVisible(smartWindowPersonalizeButton),
        "smartWindowPersonalizeButton is visible with consent"
      );
      await SpecialPowers.popPrefEnv();
    });
  });

  it("launches Smart Window with the settings trigger from the activate link", async () => {
    const launchStub = sinon.stub(AIWindow, "launchWindow").resolves(true);

    try {
      await withPrefsPane("ai", async doc => {
        const activateLink = doc.getElementById("activateSmartWindowLink");
        synthesizeClick(activateLink);

        await TestUtils.waitForCondition(
          () => launchStub.callCount > 0,
          "Wait for launchWindow to be called"
        );

        Assert.deepEqual(
          launchStub.firstCall.args.slice(1),
          [true, "settings"],
          "launchWindow opens a new window with the settings trigger"
        );
      });
    } finally {
      launchStub.restore();
    }
  });

  it("shows correct options based on consent state", async () => {
    await withPrefsPane("ai", async doc => {
      const control = doc.getElementById("aiControlSmartWindowSelect");
      await control.updateComplete;

      // Without consent, available shown and no enabled option in moz-select
      Assert.equal(
        control.value,
        "available",
        "control shows available when no consent given"
      );
      let enabledOpt = control.querySelector('moz-option[value="enabled"]');
      Assert.ok(!enabledOpt, "enabled option does not exist without consent");

      // With consent, enabled option appears in select
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.tos.consentTime", 1770830464]],
      });
      await control.updateComplete;
      enabledOpt = control.querySelector('moz-option[value="enabled"]');
      Assert.ok(enabledOpt, "enabled option exists with consent");
      Assert.equal(
        control.value,
        "enabled",
        "control shows enabled when consented"
      );
      await SpecialPowers.popPrefEnv();
    });
  });
});

describe("settings Smart Window sub-panes", () => {
  for (let pane of ["personalizeSmartWindow", "manageMemories"]) {
    it(`does not open #${pane} when Smart Window is disabled`, async () => {
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.enabled", false]],
      });
      await withPrefsPane(pane, async doc => {
        Assert.notEqual(
          doc.documentGlobal.gLastCategory.category,
          `pane${pane[0].toUpperCase()}${pane.slice(1)}`,
          `#${pane} falls back to another pane`
        );
      });
      await SpecialPowers.popPrefEnv();
    });
  }

  it("does not open sub-panes after Smart Window is disabled", async () => {
    await withPrefsPane("ai", async doc => {
      const win = doc.documentGlobal;
      await SpecialPowers.pushPrefEnv({
        set: [["browser.smartwindow.enabled", false]],
      });
      for (let pane of ["personalizeSmartWindow", "manageMemories"]) {
        await win.gotoPref(pane);
        Assert.notEqual(
          win.gLastCategory.category,
          `pane${pane[0].toUpperCase()}${pane.slice(1)}`,
          `${pane} falls back to another pane`
        );
      }
      await SpecialPowers.popPrefEnv();
    });
  });
});
