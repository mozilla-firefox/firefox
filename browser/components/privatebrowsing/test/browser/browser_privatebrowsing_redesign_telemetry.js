/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const REDESIGN_PREF = "browser.privateWindowRedesign.enabled";
const SHOWN_PREF = "browser.privatebrowsing.introAnimationShown";
const NOVA_PREF = "browser.nova.enabled";

add_task(async function test_intro_animation_recorded_on_first_run() {
  // Flush first: events buffered in the content process by earlier tests would
  // otherwise arrive after the reset and be counted here.
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, false],
      ["ui.prefersReducedMotion", 0],
    ],
  });

  Assert.equal(
    Glean.aboutprivatebrowsing.introAnimationPlayed.testGetValue(),
    null,
    "No intro animation event recorded yet"
  );

  let { win } = await openTabAndWaitForRender();
  await Services.fog.testFlushAllChildren();

  const events = Glean.aboutprivatebrowsing.introAnimationPlayed.testGetValue();
  Assert.equal(events?.length, 1, "The intro animation was recorded once");
  Assert.equal(events[0].category, "aboutprivatebrowsing", "Correct category");

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// The animation only plays once per profile, so nothing is recorded after it.
add_task(async function test_nothing_recorded_on_later_runs() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, true],
      ["ui.prefersReducedMotion", 0],
    ],
  });

  let { win } = await openTabAndWaitForRender();
  await Services.fog.testFlushAllChildren();

  Assert.equal(
    Glean.aboutprivatebrowsing.introAnimationPlayed.testGetValue(),
    null,
    "The intro animation is not recorded once already seen"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Reduced motion suppresses the animation, so it must not be counted as seen.
add_task(async function test_nothing_recorded_under_reduced_motion() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, false],
      ["ui.prefersReducedMotion", 1],
    ],
  });

  let { win } = await openTabAndWaitForRender();
  await Services.fog.testFlushAllChildren();

  Assert.equal(
    Glean.aboutprivatebrowsing.introAnimationPlayed.testGetValue(),
    null,
    "The intro animation is not recorded under reduced motion"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_basics_link_click_recorded() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, true],
    ],
  });

  let { win, tab } = await openTabAndWaitForRender();
  await SpecialPowers.spawn(tab, [], async function () {
    content.document.getElementById("private-window-basics").click();
  });
  await Services.fog.testFlushAllChildren();

  const events =
    Glean.aboutprivatebrowsing.clickPrivateWindowBasicsLink.testGetValue();
  Assert.equal(events?.length, 1, "The basics link click was recorded once");

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Stubbed: showSpotlightDialog resolves when the dialog closes, so a real one
// would leave a modal open for the rest of the run.
add_task(async function test_basics_modal_shown_recorded() {
  const { Spotlight } = ChromeUtils.importESModule(
    "resource:///modules/asrouter/Spotlight.sys.mjs"
  );
  const sandbox = sinon.createSandbox();
  sandbox.stub(Spotlight, "showSpotlightDialog").resolves(true);

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, true],
    ],
  });

  let { win, tab } = await openTabAndWaitForRender();
  await SpecialPowers.spawn(tab, [], async function () {
    content.document.getElementById("private-window-basics").click();
  });

  await TestUtils.waitForCondition(
    () => Glean.aboutprivatebrowsing.basicsModalShown.testGetValue(),
    "The spotlight impression is recorded"
  );
  const events = Glean.aboutprivatebrowsing.basicsModalShown.testGetValue();
  Assert.equal(events.length, 1, "The impression was recorded once");
  ok(
    Spotlight.showSpotlightDialog.called,
    "The impression is recorded for a spotlight that was actually opened"
  );

  sandbox.restore();
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Enrollment says who was assigned a branch; exposure says who saw it.
add_task(async function test_redesign_exposure_recorded() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  const doExperimentCleanup = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: "privateWindowRedesign",
    value: { pbwRedesignGate: true },
  });

  await SpecialPowers.pushPrefEnv({
    set: [
      [NOVA_PREF, true],
      [REDESIGN_PREF, true],
      [SHOWN_PREF, true],
    ],
  });

  let { win, tab } = await openTabAndWaitForRender();

  // Flush on each poll: the exposure is recorded in the content process, so
  // the parent cannot see it until the child data is flushed.
  await TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    return Glean.nimbusEvents.exposure
      .testGetValue("events")
      ?.some(e => e.extra.feature_id === "privateWindowRedesign");
  }, "An exposure event is recorded for the redesign feature");

  // The exposure is recorded once per content process, which outlives the
  // test, so a later run in the same browser would see nothing recorded.
  await SpecialPowers.spawn(tab, [], () => {
    const { NimbusFeatures } = ChromeUtils.importESModule(
      "resource://nimbus/ExperimentAPI.sys.mjs"
    );
    NimbusFeatures.privateWindowRedesign._didSendExposureEvent = false;
  });

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
  await doExperimentCleanup();
});
