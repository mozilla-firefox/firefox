/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

const { GeneratedWidgetsFeature } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/GeneratedWidgetsFeature.sys.mjs"
);
const { _ExperimentFeature: ExperimentFeature } = ChromeUtils.importESModule(
  "resource://nimbus/ExperimentAPI.sys.mjs"
);

const CONTROL_PREF = "browser.ai.control.generatedWidgets";
const DEFAULT_PREF = "browser.ai.control.default";
const LABS_PREF = "browser.widgets.generated.enabled";
const PROVIDER_PREF = "extensions.widgets.generated.enabled";

describe("settings ai features", () => {
  describe("generated widgets AI control", () => {
    let cleanupEnrollment, removeTestFeature, prefEnvCount;

    beforeEach(async () => {
      cleanupEnrollment = null;
      removeTestFeature = null;
      prefEnvCount = 0;
      Services.prefs.clearUserPref(LABS_PREF);
      Services.prefs.clearUserPref(PROVIDER_PREF);
      await SpecialPowers.pushPrefEnv({
        set: [
          ["browser.preferences.aiControls", true],
          ["extensions.ml.enabled", true],
          [DEFAULT_PREF, "available"],
          ["browser.ai.control.translations", "default"],
          ["browser.ai.control.pdfjsAltText", "default"],
          ["browser.ai.control.smartTabGroups", "default"],
          ["browser.ai.control.linkPreviewKeyPoints", "default"],
          ["browser.ai.control.sidebarChatbot", "default"],
          ["browser.ai.control.smartWindow", "default"],
          ["browser.ai.control.speechRecognition", "default"],
          [CONTROL_PREF, "default"],
        ],
      });
      prefEnvCount++;
      removeTestFeature = NimbusTestUtils.addTestFeatures(
        new ExperimentFeature("test-generated-widgets", {
          variables: {
            enabled: {
              type: "boolean",
              setPref: { branch: "user", pref: LABS_PREF },
            },
          },
        })
      );
    });

    afterEach(async () => {
      try {
        if (isEnrolled()) {
          await cleanupEnrollment?.();
        }
        await NimbusTestUtils.assert.storeIsEmpty();
      } finally {
        removeTestFeature?.();
        while (prefEnvCount > 0) {
          await SpecialPowers.popPrefEnv();
          prefEnvCount--;
        }
        Services.prefs.clearUserPref(LABS_PREF);
        Services.prefs.clearUserPref(PROVIDER_PREF);
      }
    });

    async function enrolInLabs() {
      cleanupEnrollment = await NimbusTestUtils.enrollWithFeatureConfig(
        { featureId: "test-generated-widgets", value: { enabled: true } },
        { slug: "custom-widgets-labs" }
      );
    }

    function isEnrolled() {
      return !!ExperimentAPI.manager.store.get("custom-widgets-labs")?.active;
    }

    function assertFeatureState(message, [allowed, enabled, state]) {
      Assert.equal(
        GeneratedWidgetsFeature.isAllowed,
        allowed,
        `${message}: isAllowed`
      );
      Assert.equal(
        GeneratedWidgetsFeature.isEnabled,
        enabled,
        `${message}: isEnabled`
      );
      Assert.equal(
        GeneratedWidgetsFeature.aiControlState,
        state,
        `${message}: aiControlState`
      );
    }

    async function enableInstalledWidgets() {
      await SpecialPowers.pushPrefEnv({ set: [[PROVIDER_PREF, true]] });
      prefEnvCount++;
    }

    async function blockAllAi(doc) {
      const win = doc.documentGlobal;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
      await dialogEl.updateComplete;
      toggle.buttonEl.scrollIntoView();
      await new Promise(resolve => win.requestAnimationFrame(resolve));
      const dialogShown = BrowserTestUtils.waitForEvent(
        dialogEl.dialog,
        "toggle"
      );
      EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
      await dialogShown;
      Assert.ok(dialogEl.dialog.open, "Block confirmation is open");
      await waitForSettingChange(
        win.Preferences.getSetting("aiControlDefaultToggle"),
        () =>
          EventUtils.synthesizeMouseAtCenter(dialogEl.confirmButton, {}, win)
      );
      Assert.equal(
        Services.prefs.getStringPref(DEFAULT_PREF),
        "blocked",
        "The global switch blocks AI"
      );
    }

    async function unblockAllAi(doc) {
      const win = doc.documentGlobal;
      const toggle = doc.getElementById("aiControlDefaultToggle");
      toggle.buttonEl.scrollIntoView();
      await new Promise(resolve => win.requestAnimationFrame(resolve));
      await waitForSettingChange(
        win.Preferences.getSetting("aiControlDefaultToggle"),
        () => EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win)
      );
      Assert.equal(
        Services.prefs.getStringPref(DEFAULT_PREF),
        "available",
        "The global switch makes AI available"
      );
    }

    describe("custom widgets row", () => {
      function assertVisibility(doc, visible) {
        for (const id of [
          "customWidgetsFieldset",
          "aiControlGeneratedWidgetsSelect",
        ]) {
          const element = doc.getElementById(id);
          Assert.ok(element, `${id} exists`);
          Assert.equal(
            BrowserTestUtils.isVisible(element),
            visible,
            `${id} is ${visible ? "visible" : "hidden"}`
          );
        }
      }

      it("shows the group only while the Labs pref is on", async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.preferences.aiControls.showUnavailable", true]],
        });
        prefEnvCount++;
        Assert.ok(!Services.prefs.getBoolPref(LABS_PREF), "Labs is disabled");

        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const setting = win.Preferences.getSetting(
            "aiControlGeneratedWidgetsSelect"
          );
          assertVisibility(doc, false);

          let settingChanged = waitForSettingChange(setting);
          await enrolInLabs();
          await settingChanged;
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          assertVisibility(doc, true);
          Assert.equal(
            doc.getElementById("aiControlGeneratedWidgetsSelect").value,
            "enabled"
          );

          settingChanged = waitForSettingChange(setting);
          await cleanupEnrollment();
          await settingChanged;
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          assertVisibility(doc, false);
        });
      });

      it("keeps the group and reads Blocked when AI is blocked", async () => {
        await enrolInLabs();
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const setting = win.Preferences.getSetting(
            "aiControlGeneratedWidgetsSelect"
          );
          const select = doc.getElementById("aiControlGeneratedWidgetsSelect");
          assertVisibility(doc, true);
          try {
            const settingChanged = waitForSettingChange(setting);
            await blockAllAi(doc);
            await settingChanged;
            await new Promise(resolve => win.requestAnimationFrame(resolve));
            assertVisibility(doc, true);
            Assert.equal(select.value, "blocked");
            Assert.ok(isEnrolled(), "Labs enrolment remains active");
          } finally {
            await unblockAllAi(doc);
          }
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          assertVisibility(doc, true);
          Assert.equal(select.value, "available");
        });
      });

      it("offers all three options and keeps the one chosen", async () => {
        Services.prefs.setStringPref(CONTROL_PREF, "blocked");
        await enrolInLabs();
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const setting = win.Preferences.getSetting(
            "aiControlGeneratedWidgetsSelect"
          );
          const select = doc.getElementById("aiControlGeneratedWidgetsSelect");
          const offered = () =>
            [...select.querySelectorAll("moz-option")].map(o => o.value);
          const expected = ["available", "enabled", "blocked"];
          Assert.deepEqual(offered(), expected, "Blocked user is offered all");
          for (const choice of ["available", "enabled", "available"]) {
            const settingChanged = waitForSettingChange(setting);
            await changeMozSelectValue(select, choice);
            await settingChanged;
            await new Promise(resolve => win.requestAnimationFrame(resolve));
            Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), choice);
            Assert.equal(select.value, choice, `${choice} stays selected`);
            Assert.deepEqual(offered(), expected, "All options remain");
          }
          Assert.ok(isEnrolled(), "Labs enrolment remains active");
        });
      });

      it("keeps the group and enrolment when blocked from the row", async () => {
        await enrolInLabs();
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const select = doc.getElementById("aiControlGeneratedWidgetsSelect");
          const settingChanged = waitForSettingChange(
            win.Preferences.getSetting("aiControlGeneratedWidgetsSelect")
          );
          await changeMozSelectValue(select, "blocked");
          await settingChanged;
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "blocked");
          Assert.ok(isEnrolled(), "Labs enrolment remains active");
          assertVisibility(doc, true);
          Assert.equal(select.value, "blocked");
        });
      });

      it("reads Blocked for a user who rejoined Labs while AI is blocked", async () => {
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          try {
            await blockAllAi(doc);
            const settingChanged = waitForSettingChange(
              win.Preferences.getSetting("aiControlGeneratedWidgetsSelect")
            );
            await enrolInLabs();
            await settingChanged;
            await new Promise(resolve => win.requestAnimationFrame(resolve));
            assertVisibility(doc, true);
            Assert.equal(
              doc.getElementById("aiControlGeneratedWidgetsSelect").value,
              "blocked"
            );
          } finally {
            await unblockAllAi(doc);
          }
        });
      });

      it("is disabled when the control is locked", async () => {
        await enrolInLabs();
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const setting = win.Preferences.getSetting(
            "aiControlGeneratedWidgetsSelect"
          );
          try {
            const settingChanged = waitForSettingChange(setting);
            Services.prefs.lockPref(CONTROL_PREF);
            setting.emit("change");
            await settingChanged;
            await new Promise(resolve => win.requestAnimationFrame(resolve));
            assertVisibility(doc, true);
            Assert.ok(
              doc.getElementById("aiControlGeneratedWidgetsSelect").disabled,
              "The control is disabled when locked"
            );
          } finally {
            Services.prefs.unlockPref(CONTROL_PREF);
          }
        });
      });

      it("lists custom widgets in the block dialog only for Labs users", async () => {
        await withPrefsPane("ai", async doc => {
          const win = doc.documentGlobal;
          const toggle = doc.getElementById("aiControlDefaultToggle");
          const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
          await dialogEl.updateComplete;
          for (const enrolled of [false, true]) {
            if (enrolled) {
              await enrolInLabs();
            }
            toggle.buttonEl.scrollIntoView();
            await new Promise(resolve => win.requestAnimationFrame(resolve));
            const dialogShown = BrowserTestUtils.waitForEvent(
              dialogEl.dialog,
              "toggle"
            );
            EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
            await dialogShown;
            Assert.ok(dialogEl.dialog.open, "Block confirmation is open");
            Assert.equal(
              !!dialogEl.renderRoot.querySelector(
                '[data-l10n-id="preferences-ai-controls-block-confirmation-custom-widgets"]'
              ),
              enrolled,
              "Custom widgets are listed only for Labs users"
            );
            const dialogClosed = BrowserTestUtils.waitForEvent(
              dialogEl.dialog,
              "toggle"
            );
            EventUtils.synthesizeMouseAtCenter(dialogEl.cancelButton, {}, win);
            await dialogClosed;
            Assert.ok(!dialogEl.dialog.open, "Block confirmation is closed");
          }
        });
      });

      it("has its strings", async () => {
        await enrolInLabs();
        await withPrefsPane("ai", async doc => {
          Assert.equal(
            await doc.l10n.formatValue(
              "preferences-ai-controls-block-confirmation-custom-widgets"
            ),
            "Custom widgets"
          );
          const fieldset = doc.getElementById("customWidgetsFieldset");
          await doc.l10n.translateFragment(fieldset);
          Assert.equal(fieldset.getAttribute("label"), "Custom widgets");
        });
      });
    });

    it("follows the per-feature control, then the global default", () => {
      for (const [control, global, expected] of [
        ["default", "available", false],
        ["default", "blocked", true],
        ["available", "blocked", false],
        ["blocked", "available", true],
      ]) {
        Services.prefs.setStringPref(CONTROL_PREF, control);
        Services.prefs.setStringPref(DEFAULT_PREF, global);
        Assert.equal(
          GeneratedWidgetsFeature.isBlocked,
          expected,
          `Control ${control} and global ${global} resolve to blocked=${expected}`
        );
      }
    });

    it("is enabled in Labs unless blocked or Available is chosen", async () => {
      assertFeatureState("Before Labs", [false, false, "disallowed"]);
      await enrolInLabs();
      assertFeatureState("After enrolment", [true, true, "enabled"]);
      Services.prefs.setStringPref(CONTROL_PREF, "available");
      assertFeatureState("Available", [true, false, "available"]);
      Services.prefs.setStringPref(CONTROL_PREF, "enabled");
      assertFeatureState("Enabled once chosen", [true, true, "enabled"]);
      Services.prefs.setStringPref(CONTROL_PREF, "blocked");
      assertFeatureState("Blocked", [true, false, "blocked"]);
    });

    it("keeps the Labs enrolment when made available", async () => {
      await enableInstalledWidgets();
      await enrolInLabs();
      Services.prefs.setStringPref(CONTROL_PREF, "blocked");
      await GeneratedWidgetsFeature.makeAvailable();
      Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
      Assert.ok(isEnrolled(), "Labs enrolment remains active");
      Assert.ok(Services.prefs.getBoolPref(LABS_PREF), "Labs remains enabled");
      Assert.ok(
        Services.prefs.getBoolPref(PROVIDER_PREF),
        "Installed widgets remain enabled"
      );
    });

    it("leaves the Labs pref alone when blocked and made available", async () => {
      await enableInstalledWidgets();
      await enrolInLabs();
      await withPrefsPane("general", async doc => {
        await openAiFeaturePanel(doc, doc.documentGlobal);
        await blockAllAi(doc);
        Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "blocked");
        Assert.ok(isEnrolled(), "Labs enrolment remains active");
        Assert.ok(
          Services.prefs.getBoolPref(LABS_PREF),
          "Labs remains enabled"
        );
        Assert.ok(
          Services.prefs.getBoolPref(PROVIDER_PREF),
          "Installed widgets remain enabled after global block"
        );
        await unblockAllAi(doc);
        Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
        Assert.ok(isEnrolled(), "Labs enrolment remains active");
      });
    });

    it("does not enrol in Labs on a global unblock", async () => {
      await withPrefsPane("general", async doc => {
        await openAiFeaturePanel(doc, doc.documentGlobal);
        await blockAllAi(doc);
        await unblockAllAi(doc);
        Assert.ok(!isEnrolled(), "Global unblock does not enrol in Labs");
        Assert.ok(
          !Services.prefs.getBoolPref(LABS_PREF),
          "Labs stays disabled"
        );
      });
    });

    it("is available again after a global unblock when the user re-enrolled while blocked", async () => {
      await withPrefsPane("general", async doc => {
        await openAiFeaturePanel(doc, doc.documentGlobal);
        await blockAllAi(doc);
        await enrolInLabs();
        Assert.ok(
          Services.prefs.getBoolPref(LABS_PREF),
          "Labs enables the pref"
        );
        Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "blocked");
        Assert.ok(!GeneratedWidgetsFeature.isEnabled, "Still blocked globally");
        await unblockAllAi(doc);
        Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
        Assert.ok(isEnrolled(), "Labs enrolment remains active");
        Assert.ok(
          Services.prefs.getBoolPref(LABS_PREF),
          "Labs remains enabled"
        );
        Assert.ok(
          !GeneratedWidgetsFeature.isBlocked,
          "The control is available"
        );
      });
    });

    it("leaves a policy-locked control alone on a global block", async () => {
      await enrolInLabs();
      await withPrefsPane("general", async doc => {
        await openAiFeaturePanel(doc, doc.documentGlobal);
        const defaults = Services.prefs.getDefaultBranch("");
        const originalDefault = defaults.getStringPref(CONTROL_PREF);
        defaults.setStringPref(CONTROL_PREF, "available");
        Services.prefs.setStringPref(CONTROL_PREF, "available");
        Services.prefs.lockPref(CONTROL_PREF);
        try {
          await blockAllAi(doc);
          Assert.ok(isEnrolled(), "Policy-locked enrolment remains active");
          Assert.ok(
            Services.prefs.getBoolPref(LABS_PREF),
            "Labs remains enabled"
          );
          Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
          Assert.ok(GeneratedWidgetsFeature.isManagedByPolicy);
        } finally {
          Services.prefs.unlockPref(CONTROL_PREF);
          defaults.setStringPref(CONTROL_PREF, originalDefault);
          if (Services.prefs.getStringPref(DEFAULT_PREF) === "blocked") {
            await unblockAllAi(doc);
          }
        }
      });
    });
  });
});
