/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the animation inspector can display an animation with a playback rate of 0.

add_task(async function () {
  await addTab(URL_ROOT + "doc_zero_playback_rate.html");
  const { animationInspector, inspector, panel } =
    await openAnimationInspector();

  const animationItemEl = await findAnimationItemByTargetSelector(
    panel,
    ".zero-playback-rate"
  );
  ok(animationItemEl, "The animation item is displayed");

  const summaryGraphEl = animationItemEl.querySelector(
    ".animation-summary-graph"
  );
  ok(
    summaryGraphEl.getAttribute("title").includes("Playback rate: 0"),
    "The tooltip shows the playback rate"
  );

  const svgEl = summaryGraphEl.querySelector(".animation-summary-graph-path");
  is(
    svgEl.viewBox.baseVal.width,
    100000,
    "The graph is drawn with the layout of a playback rate of 1"
  );

  checkTickLabels(panel);

  info("Click in the middle of the scrubber area");
  clickOnCurrentTimeScrubberController(animationInspector, panel, 0.5);
  await waitFor(async () => {
    const { currentTime, playState } = await getContentAnimationState();
    return playState === "paused" && currentTime > 0;
  }, "Wait until the page animation is paused at the scrubber position");

  info("Select the animated node to create the actors for a paused animation");
  const [previousAnimation] = animationInspector.state.animations;
  await selectNode(".zero-playback-rate", inspector);
  await waitFor(
    () =>
      animationInspector.state.animations.length === 1 &&
      animationInspector.state.animations[0] !== previousAnimation
  );

  const { state } = animationInspector.state.animations[0];
  ok(Number.isFinite(state.createdTime), "The created time is finite");
  checkTickLabels(panel);
});

function checkTickLabels(panel) {
  const labels = [...panel.querySelectorAll(".tick-label")].map(
    el => el.textContent
  );
  Assert.greater(labels.length, 0, "Tick labels are displayed");
  ok(
    labels.every(label => !label.includes("NaN")),
    `No tick label is NaN: ${labels.join(", ")}`
  );
}

function getContentAnimationState() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    const { currentTime, playState } = content.document.getAnimations()[0];
    return { currentTime, playState };
  });
}
