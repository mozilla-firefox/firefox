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

    it("leaves the Labs pref alone when blocked and made available", async () => {
      const doc = paneDoc;
      await enableInstalledWidgets();
      await enrolInLabsAndWait(doc);
      await blockAllAi(doc);
      Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "blocked");
      Assert.ok(isEnrolled(), "Labs enrolment remains active");
      Assert.ok(Services.prefs.getBoolPref(LABS_PREF), "Labs remains enabled");
      Assert.ok(
        Services.prefs.getBoolPref(PROVIDER_PREF),
        "Installed widgets remain enabled after global block"
      );
      await unblockAllAi(doc);
      Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
      Assert.ok(isEnrolled(), "Labs enrolment remains active");
    });

    it("does not enrol in Labs on a global unblock", async () => {
      const doc = paneDoc;
      await blockAllAi(doc);
      await unblockAllAi(doc);
      Assert.ok(!isEnrolled(), "Global unblock does not enrol in Labs");
      Assert.ok(!Services.prefs.getBoolPref(LABS_PREF), "Labs stays disabled");
    });

    it("is available again after a global unblock when the user re-enrolled while blocked", async () => {
      const doc = paneDoc;
      await blockAllAi(doc);
      await enrolInLabs();
      Assert.ok(Services.prefs.getBoolPref(LABS_PREF), "Labs enables the pref");
      Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "blocked");
      Assert.ok(!GeneratedWidgetsFeature.isEnabled, "Still blocked globally");
      await unblockAllAi(doc);
      Assert.equal(Services.prefs.getStringPref(CONTROL_PREF), "available");
      Assert.ok(isEnrolled(), "Labs enrolment remains active");
      Assert.ok(Services.prefs.getBoolPref(LABS_PREF), "Labs remains enabled");
      Assert.ok(!GeneratedWidgetsFeature.isBlocked, "The control is available");
    });

    it("leaves a policy-locked control alone on a global block", async () => {
      const doc = paneDoc;
      await enrolInLabsAndWait(doc);
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
