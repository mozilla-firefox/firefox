/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for the following data of engagement telemetry.
// - interaction

// Allow more time for Mac machines so they don't time out in verify mode.
if (AppConstants.platform == "macosx") {
  requestLongerTimeout(3);
}

add_setup(async function () {
  await initInteractionTest();
});

add_task(async function topsites() {
  await doTopsitesTest({
    trigger: () => doEnter(),
    assert: () => assertEngagementTelemetry([{ interaction: "topsites" }]),
  });
});

add_task(async function typed() {
  await doTypedTest({
    trigger: () => doEnter(),
    assert: () => assertEngagementTelemetry([{ interaction: "typed" }]),
  });

  await doTypedWithResultsPopupTest({
    trigger: () => doEnter(),
    assert: () => assertEngagementTelemetry([{ interaction: "typed" }]),
  });
});

add_task(async function dropped() {
  await doTest(async () => {
    await doDropAndGo("example.com");

    await assertEngagementTelemetry([{ interaction: "dropped" }]);
  });

  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.com/");
    await showResultByArrowDown();
    await doDropAndGo("example.com");

    await assertEngagementTelemetry([{ interaction: "dropped" }]);

    await cleanupTopSites();
  });
});

add_task(async function pasted() {
  await doPastedTest({
    trigger: () => doEnter(),
    assert: () => assertEngagementTelemetry([{ interaction: "pasted" }]),
  });

  await doPastedWithResultsPopupTest({
    trigger: () => doEnter(),
    assert: () => assertEngagementTelemetry([{ interaction: "pasted" }]),
  });

  await doTest(async () => {
    await doPasteAndGo("www.example.com");

    await assertEngagementTelemetry([{ interaction: "pasted" }]);
  });

  await doTest(async () => {
    let cleanupTopSites = await addTopSites("https://example.com/");
    await showResultByArrowDown();
    await doPasteAndGo("www.example.com");

    await assertEngagementTelemetry([{ interaction: "pasted" }]);

    await cleanupTopSites();
  });

  await doTest(async () => {
    // "paste" event will be fired if the string includes control char.
    // Otherwise, "input" event will be fired.
    await doPaste("a\nb");
    await doEnter();

    await assertEngagementTelemetry([{ interaction: "pasted" }]);
  });
});

add_task(async function topsite_search() {
  await doTopsitesSearchTest({
    trigger: () => doEnter(),
    assert: () =>
      assertEngagementTelemetry([{ interaction: "topsite_search" }]),
  });
});

add_task(async function returned_restarted_refined() {
  await doReturnedRestartedRefinedTest({
    trigger: () => doEnter(),
    assert: expected => assertEngagementTelemetry([{ interaction: expected }]),
  });
});
