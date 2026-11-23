/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures companion activation records one event in the panel's
 * flow, while cancellation and reopening in Split View record none.
 */
add_task(async function test_companion_button_telemetry() {
  await FullPageTranslationsTestUtils.assertCompanionButtonTelemetry({
    openFromAppMenu: false,
  });
});

/**
 * This test case ensures clicking the companion after opening from the app menu
 * records one event without events from cancellation or reopening.
 */
add_task(async function test_companion_button_telemetry_app_menu() {
  await FullPageTranslationsTestUtils.assertCompanionButtonTelemetry({
    openFromAppMenu: true,
  });
});
