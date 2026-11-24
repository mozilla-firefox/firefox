/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_generated_widgets.js", gTestPath).href,
  this
);

describe("settings ai features", () => {
  describe("generated widgets AI control", () => {
    beforeEach(setUpGeneratedWidgetsTest);
    afterEach(tearDownGeneratedWidgetsTest);

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
  });
});
