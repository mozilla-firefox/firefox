/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test the telemetry event for opening the about:translations page.
 */
add_task(async function test_about_translations_telemetry_open() {
  const { cleanup } = await openAboutTranslations();

  await TestTranslationsTelemetry.assertEvent(
    Glean.translationsAboutTranslationsPage.open,
    {
      expectedEventCount: 1,
      expectNewFlowId: true,
      assertForAllEvents: { open_location: "full" },
    }
  );

  await TestTranslationsTelemetry.assertTranslationsEnginePerformance({
    expectedEventCount: 0,
  });

  await cleanup();
});
