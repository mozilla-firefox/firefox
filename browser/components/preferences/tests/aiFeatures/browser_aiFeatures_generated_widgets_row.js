/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_generated_widgets.js", gTestPath).href,
  this
);

requestLongerTimeout(3);

describe("settings ai features", () => {
  describe("generated widgets AI control", () => {
    useSharedAiPane();
    beforeEach(setUpGeneratedWidgetsTest);
    afterEach(tearDownGeneratedWidgetsTest);

    describe("custom widgets row", () => {
      it("shows the group only while the Labs pref is on", async () => {
        await SpecialPowers.pushPrefEnv({
          set: [["browser.preferences.aiControls.showUnavailable", true]],
        });
        prefEnvCount++;
        Assert.ok(!Services.prefs.getBoolPref(LABS_PREF), "Labs is disabled");

        const doc = paneDoc;
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

      it("offers all three options and keeps the one chosen", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        Services.prefs.setStringPref(CONTROL_PREF, "blocked");
        await enrolInLabsAndWait(doc);
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

      it("keeps the group and enrolment when blocked from the row", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        await enrolInLabsAndWait(doc);
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

      it("is disabled when the control is locked", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        await enrolInLabsAndWait(doc);
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

      it("has its strings", async () => {
        const doc = paneDoc;
        await enrolInLabsAndWait(doc);
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

      it("links to the custom widgets support page", async () => {
        const doc = paneDoc;
        await enrolInLabsAndWait(doc);
        Assert.equal(
          doc.getElementById("customWidgetsFieldset").supportPage,
          "firefox-custom-widgets"
        );
      });
    });
  });
});
