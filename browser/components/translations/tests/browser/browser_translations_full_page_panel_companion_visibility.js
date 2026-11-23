/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures an existing Split View hides the companion action
 * until its tabs separate.
 */
add_task(
  async function test_companion_button_visibility_with_regular_partner() {
    await FullPageTranslationsTestUtils.assertCompanionButtonVisibilityWithRegularPartner(
      {
        openFromAppMenu: false,
      }
    );
  }
);

/**
 * This test case ensures an existing Split View hides the companion action
 * until its tabs separate when the panel opens from the app menu.
 */
add_task(
  async function test_companion_button_visibility_with_regular_partner_app_menu() {
    await FullPageTranslationsTestUtils.assertCompanionButtonVisibilityWithRegularPartner(
      {
        openFromAppMenu: true,
      }
    );
  }
);
