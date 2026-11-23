/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures the page's close button records exactly one event
 * even though clicking it closes the tab and destroys its actor.
 */
add_task(
  async function test_about_translations_telemetry_split_view_close_button() {
    await AboutTranslationsTestUtils.assertSplitViewCloseButtonTelemetry({
      closeWithButton: true,
    });
  }
);

/**
 * This test case ensures separating Split View and closing the tab through
 * the browser controls record no page close-button event.
 */
add_task(
  async function test_about_translations_telemetry_separate_and_close_tab() {
    await AboutTranslationsTestUtils.assertSplitViewCloseButtonTelemetry({
      closeWithButton: false,
    });
  }
);
