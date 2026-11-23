/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This test case ensures an LTR companion opens before its source page, and
 * closing it restores the companion button without closing the source page.
 */
add_task(async function test_companion_button_opens_split_view_ltr() {
  await FullPageTranslationsTestUtils.assertCompanionButtonOpensSplitView({
    rtl: false,
    openFromAppMenu: false,
  });
});

/**
 * This test case ensures an RTL companion stays at the logical start of Split
 * View and the companion button returns after it closes.
 */
add_task(async function test_companion_button_opens_split_view_rtl() {
  await FullPageTranslationsTestUtils.assertCompanionButtonOpensSplitView({
    rtl: true,
    openFromAppMenu: false,
  });
});

/**
 * This test case ensures the companion's LTR placement and button visibility
 * also hold when the panel opens from the app menu.
 */
add_task(async function test_companion_button_opens_split_view_app_menu_ltr() {
  await FullPageTranslationsTestUtils.assertCompanionButtonOpensSplitView({
    rtl: false,
    openFromAppMenu: true,
  });
});

/**
 * This test case ensures the companion's RTL placement and button visibility
 * also hold when the panel opens from the app menu.
 */
add_task(async function test_companion_button_opens_split_view_app_menu_rtl() {
  await FullPageTranslationsTestUtils.assertCompanionButtonOpensSplitView({
    rtl: true,
    openFromAppMenu: true,
  });
});
