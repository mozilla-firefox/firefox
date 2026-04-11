/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.os.Build
import android.util.Log
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.ViewInteraction
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.Matchers.endsWith
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.LISTS_MAXSWIPES
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.scrollToElementByText
import org.mozilla.fenix.helpers.click

/**
 * Implementation of Robot Pattern for the settings Theme sub menu.
 */
class SettingsSubMenuCustomizeRobot {

    fun verifyThemes() {
        Log.i(TAG, "verifyThemes: Trying to verify that the \"Light\" mode option is visible")
        lightModeToggle()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyThemes: Verified that the \"Light\" mode option is visible")
        Log.i(TAG, "verifyThemes: Trying to verify that the \"Dark\" mode option is visible")
        darkModeToggle()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyThemes: Verified that the \"Dark\" mode option is visible")
        Log.i(TAG, "verifyThemes: Trying to verify that the \"Follow device theme\" option is visible")
        deviceModeToggle()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyThemes: Verified that the \"Follow device theme\" option is visible")
    }

    fun selectDarkMode() {
        Log.i(TAG, "selectDarkMode: Trying to click the \"Dark\" mode option")
        darkModeToggle().click()
        Log.i(TAG, "selectDarkMode: Clicked the \"Dark\" mode option")
    }

    fun selectLightMode() {
        Log.i(TAG, "selectLightMode: Trying to click the \"Light\" mode option")
        lightModeToggle().click()
        Log.i(TAG, "selectLightMode: Clicked the \"Light\" mode option")
    }

    fun clickTopToolbarToggle() {
        Log.i(TAG, "clickTopToolbarToggle: Trying to click the \"Top\" toolbar option")
        topToolbarToggle().click()
        Log.i(TAG, "clickTopToolbarToggle: Clicked the \"Top\" toolbar option")
    }

    fun clickBottomToolbarToggle() {
        Log.i(TAG, "clickBottomToolbarToggle: Trying to click the \"Bottom\" toolbar option")
        bottomToolbarToggle().click()
        Log.i(TAG, "clickBottomToolbarToggle: Clicked the \"Bottom\" toolbar option")
    }

    fun verifyAddressBarPositionPreference(selectedPosition: String) {
        Log.i(TAG, "verifyAddressBarPositionPreference: Trying to verify that the $selectedPosition toolbar option is checked")
        onView(withText(selectedPosition))
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyAddressBarPositionPreference: Verified that the $selectedPosition toolbar option is checked")
    }

    fun clickSwipeToolbarToSwitchTabToggle() {
        Log.i(TAG, "clickSwipeToolbarToSwitchTabToggle: Trying to click the \"Swipe toolbar sideways to switch tabs\" toggle")
        swipeToolbarToggle().click()
        Log.i(TAG, "clickSwipeToolbarToSwitchTabToggle: Clicked the \"Swipe toolbar sideways to switch tabs\" toggle")
    }

    fun clickPullToRefreshToggle() {
        Log.i(TAG, "clickPullToRefreshToggle: Trying to click the \"Pull to refresh\" toggle")
        pullToRefreshToggle().click()
        Log.i(TAG, "clickPullToRefreshToggle: Clicked the \"Pull to refresh\" toggle")
    }

    fun verifySwipeToolbarGesturePrefState(isEnabled: Boolean) {
        scrollToElementByText("Swipe toolbar sideways to switch tabs")
        Log.i(TAG, "verifySwipeToolbarGesturePrefState: Trying to scroll to the end of the \"Customize\" sub menu")
        customizeSettingsList().scrollToEnd(LISTS_MAXSWIPES)
        Log.i(TAG, "verifySwipeToolbarGesturePrefState: Scrolled to the end of the \"Customize\" sub menu")
        assertUIObjectExists(itemContainingText(getStringResource(R.string.preference_gestures_swipe_toolbar_switch_tabs_2)))
        Log.i(TAG, "verifySwipeToolbarGesturePrefState: Trying to verify that the \"Swipe toolbar sideways to switch tabs\" toggle is checked: $isEnabled")
        swipeToolbarToggle()
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (isEnabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        ),
                    ),
                ),
            )
        Log.i(TAG, "verifySwipeToolbarGesturePrefState: Verified that the \"Swipe toolbar sideways to switch tabs\" toggle is checked: $isEnabled")
    }

    fun verifyPullToRefreshGesturePrefState(isEnabled: Boolean) {
        Log.i(TAG, "verifyPullToRefreshGesturePrefState: Trying to verify that the \"Pull to refresh\" toggle is checked: $isEnabled")

        scrollToElementByText("Pull to refresh")
        pullToRefreshToggle()
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (isEnabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        ),
                    ),
                ),
            )
        Log.i(TAG, "verifyPullToRefreshGesturePrefState: Verified that the \"Pull to refresh\" toggle is checked: $isEnabled")
    }

    fun verifyAppIconOption(composeTestRule: ComposeTestRule, appIconOptionName: String) {
        Log.i(TAG, "verifyAppIconOption: Trying to verify that the \"App icon\" option is set to: $appIconOptionName")
        composeTestRule.onNodeWithText(getStringResource(R.string.preference_select_app_icon_title), useUnmergedTree = true)
            .assert(hasAnySibling(hasText(appIconOptionName)))
        Log.i(TAG, "verifyAppIconOption: Verified that the \"App icon\" option is set to: $appIconOptionName")
    }

    fun clickTheAppIconOption(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickTheAppIconOption: Trying to click that the \"App icon\" option")
        composeTestRule.onNodeWithText(getStringResource(R.string.preference_select_app_icon_title), useUnmergedTree = true).performClick()
        Log.i(TAG, "clickTheAppIconOption: Clicked that the \"App icon\" option")
    }

    fun verifyAppIconOptionIsDisplayed(composeTestRule: ComposeTestRule, vararg stringResIds: Int) {
        for (stringResId in stringResIds) {
            Log.i(TAG, "verifyAppIconOptionIsDisplayed: Trying to verify that the: ${getStringResource(stringResId)} app icon option is displayed")
            composeTestRule.onNodeWithText(getStringResource(stringResId), useUnmergedTree = true).assertIsDisplayed()
            Log.i(TAG, "verifyAppIconSettingItems: Verified that the: ${getStringResource(stringResId)} app icon option is displayed")
        }
    }

    fun verifyAppIconSettingItems(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyAppIconSettingItems: Trying to verify that the \"App icon\" setting items are displayed")
        verifyAppIconOptionIsDisplayed(
            composeTestRule,
            // Other section
            R.string.alternative_app_icon_group_featured,
            R.string.alternative_app_icon_option_retro_2004,
            R.string.alternative_app_icon_option_pixelated,
            R.string.alternative_app_icon_option_cuddling,
            R.string.alternative_app_icon_option_pride,
            R.string.alternative_app_icon_option_flaming,
            R.string.alternative_app_icon_option_minimal,
            R.string.alternative_app_icon_option_momo,
            R.string.alternative_app_icon_option_momo_subtitle,
            R.string.alternative_app_icon_option_cool,
            // Solid colors section
            R.string.alternative_app_icon_group_solid_colors,
            R.string.alternative_app_icon_option_default,
            R.string.alternative_app_icon_option_light,
            R.string.alternative_app_icon_option_dark,
        )
        scrollToElementByText(getStringResource(R.string.alternative_app_icon_option_gradient_northern_lights))
        verifyAppIconOptionIsDisplayed(
            composeTestRule,
            R.string.alternative_app_icon_option_red,
            R.string.alternative_app_icon_option_green,
            R.string.alternative_app_icon_option_blue,
            R.string.alternative_app_icon_option_purple,
            R.string.alternative_app_icon_option_purple_dark,
            // Gradients section
            R.string.alternative_app_icon_option_gradient_sunrise,
            R.string.alternative_app_icon_option_gradient_golden_hour,
            R.string.alternative_app_icon_option_gradient_sunset,
            R.string.alternative_app_icon_option_gradient_blue_hour,
            R.string.alternative_app_icon_option_gradient_twilight,
            R.string.alternative_app_icon_group_gradients,
            R.string.alternative_app_icon_option_gradient_midnight,
            R.string.alternative_app_icon_option_gradient_northern_lights,
        )
        Log.i(TAG, "verifyAppIconSettingItems: Verified that the \"App icon\" setting items are displayed")
    }

    fun clickAppIconOption(composeTestRule: ComposeTestRule, appIconOptionName: String) {
        Log.i(TAG, "clickAppIconOption: Trying to click that the: $appIconOptionName \"App icon\" option")
        composeTestRule.onNodeWithText(appIconOptionName, useUnmergedTree = true)
            .performClick()
        Log.i(TAG, "clickAppIconOption: Clicked that the: $appIconOptionName \"App icon\" option")
    }

    fun verifyChangeAppIconDialog(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyChangeAppIconDialog: Trying to verify that the dialog title is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.restart_warning_dialog_title), useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyChangeAppIconDialog: Verified that the  dialog title is displayed")
        Log.i(TAG, "verifyChangeAppIconDialog: Trying to verify that the dialog message is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.restart_warning_dialog_body_2, argument = appName), useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyChangeAppIconDialog: Verified that the dialog message is displayed")
        Log.i(TAG, "verifyChangeAppIconDialog: Trying to verify that the \"Cancel\" dialog button is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.restart_warning_dialog_button_negative), useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyChangeAppIconDialog: Verified that the \"Cancel\" dialog button is displayed")
        Log.i(TAG, "verifyChangeAppIconDialog: Trying to verify that the \"Change icon\" dialog button is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.restart_warning_dialog_button_positive_2), useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyChangeAppIconDialog: Verified that the \"Change icon\" dialog button is displayed")
    }

    fun clickTheChangeIconDialogButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickTheChangeIconDialogButton: Trying to click the \"Change icon\" dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.restart_warning_dialog_button_positive_2), useUnmergedTree = true).performClick()
        Log.i(TAG, "clickTheChangeIconDialogButton: Clicked the \"Change icon\" dialog button")
    }

    fun verifyToolbarLayout() {
        Log.i(TAG, "verifyToolbarLayout: Trying to verify that the \"Simple\" toolbar layout option is visible")
        simpleToolbarLayoutToggle()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyToolbarLayout: Verified that the \"Simple\" toolbar layout option is visible")
        Log.i(TAG, "verifyToolbarLayout: Trying to verify that the \"Expanded\" toolbar layout option is visible")
        expandedToolbarLayoutToggle()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyToolbarLayout: Verified that the \"Expanded\" toolbar layout option is visible")
    }

    fun selectExpandedToolbarLayout() {
        Log.i(TAG, "selectExpandedToolbarLayout: Trying to click the \"Expanded\" toolbar layout option")
        expandedToolbarLayoutToggle().click()
        Log.i(TAG, "selectExpandedToolbarLayout: Clicked the \"Expanded\" toolbar layout option")
    }

    fun verifyToolbarLayoutPreference(selectedToolbarLayout: String) {
        Log.i(TAG, "verifyToolbarLayoutPreference: Trying to verify that the $selectedToolbarLayout toolbar layout option is checked")
        onView(withText(selectedToolbarLayout))
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyToolbarLayoutPreference: Verified that the $selectedToolbarLayout toolbar layout option is checked")
    }

    fun clickShowTabBarToggle() {
        Log.i(TAG, "clickShowTabBarToggle: Trying to click the \"Show tab bar\" toggle")
        showTabBarToggle().click()
        Log.i(TAG, "clickShowTabBarToggle: Clicked the \"Show tab bar\" toggle")
    }

    fun scrollToExpandedToolbarOption() {
        scrollToElementByText("Expanded")
    }

    fun scrollToTheScrollToHideToolbarOption() {
        scrollToElementByText("Scroll to hide toolbar")
    }

    fun scrollToAddressBarLocation() {
        scrollToElementByText("Address bar location")
    }

    fun verifyTheSimpleToolbarShortcutOptions() {
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Open a new tab\" simple toolbar shortcut option is visible")
        openANewTabToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Open a new tab\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Share\" simple toolbar shortcut option is visible")
        shareToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Share\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Add bookmark\" simple toolbar shortcut option is visible")
        addBookmarkToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Add bookmark\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Translate\" simple toolbar shortcut option is visible")
        translateToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Translate\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Homepage\" simple toolbar shortcut option is visible")
        homepageToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Homepage\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Back\" simple toolbar shortcut option is visible")
        backToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Back\" simple toolbar shortcut option is visible")
    }

    fun verifyTheExpandedToolbarShortcutOptions() {
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Add bookmark\" simple toolbar shortcut option is visible")
        addBookmarkToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Add bookmark\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Translate\" simple toolbar shortcut option is visible")
        translateToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Translate\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Homepage\" simple toolbar shortcut option is visible")
        homepageToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Homepage\" simple toolbar shortcut option is visible")
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Trying to verify that the \"Back\" simple toolbar shortcut option is visible")
        backToolbarShortcutOption()
            .check(matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheSimpleToolbarShortcutOptions: Verified that the \"Back\" simple toolbar shortcut option is visible")
    }

    fun verifyTheOpenANewTabToolbarShortcutIsSelected() {
        Log.i(TAG, "verifyTheOpenANewTabToolbarShortcutIsSelected: Trying to verify that the \"Open a new tab\" simple toolbar shortcut option is checked")
        openANewTabToolbarShortcutOption()
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyTheOpenANewTabToolbarShortcutIsSelected: Verified that the \"Open a new tab\" simple toolbar shortcut option is checked")
    }

    fun verifyTheShareToolbarShortcutIsSelected() {
        Log.i(TAG, "verifyTheShareToolbarShortcutIsSelected: Trying to verify that the \"Share\" simple toolbar shortcut option is checked")
        shareToolbarShortcutOption()
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyTheShareToolbarShortcutIsSelected: Verified that the \"Share\" simple toolbar shortcut option is checked")
    }

    fun clickTheShareToolbarShortcut() {
        Log.i(TAG, "clickTheShareToolbarShortcut: Trying to click the \"Share\" simple toolbar shortcut option")
        shareToolbarShortcutOption().click()
        Log.i(TAG, "clickTheShareToolbarShortcut: Clicked the \"Share\" simple toolbar shortcut option")
    }

    fun verifyTheAddBookmarkToolbarShortcutIsSelected() {
        Log.i(TAG, "verifyTheAddBookmarkToolbarShortcutIsSelected: Trying to verify that the \"Add bookmark\" simple toolbar shortcut option is checked")
        addBookmarkToolbarShortcutOption()
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyTheAddBookmarkToolbarShortcutIsSelected: Verified that the \"Add bookmark\" simple toolbar shortcut option is checked")
    }

    fun clickTheAddBookmarkToolbarShortcut() {
        Log.i(TAG, "clickTheAddBookmarkToolbarShortcut: Trying to click the \"Add bookmark\" simple toolbar shortcut option")
        addBookmarkToolbarShortcutOption().click()
        Log.i(TAG, "clickTheAddBookmarkToolbarShortcut: Clicked the \"Add bookmark\" simple toolbar shortcut option")
    }

    fun verifyTheTranslateToolbarShortcutIsSelected() {
        Log.i(TAG, "verifyTheTranslateToolbarShortcutIsSelected: Trying to verify that the \"Translate\" simple toolbar shortcut option is checked")
        translateToolbarShortcutOption()
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyTheTranslateToolbarShortcutIsSelected: Verified that the \"Translate\" simple toolbar shortcut option is checked")
    }

    fun clickTheTranslateToolbarShortcut() {
        Log.i(TAG, "clickTheTranslateToolbarShortcut: Trying to click the \"Translate\" simple toolbar shortcut option")
        translateToolbarShortcutOption().click()
        Log.i(TAG, "clickTheTranslateToolbarShortcut: Clicked the \"Translate\" simple toolbar shortcut option")
    }

    fun verifyTheHomepageToolbarShortcutIsSelected() {
        Log.i(TAG, "verifyTheHomepageToolbarShortcutIsSelected: Trying to verify that the \"Homepage\" simple toolbar shortcut option is checked")
        homepageToolbarShortcutOption()
            .check(matches(hasSibling(allOf(withId(R.id.radio_button), isChecked()))))
        Log.i(TAG, "verifyTheHomepageToolbarShortcutIsSelected: Verified that the \"Homepage\" simple toolbar shortcut option is checked")
    }

    fun clickTheHomepageToolbarShortcut() {
        Log.i(TAG, "clickTheHomepageToolbarShortcut: Trying to click the \"Homepage\" simple toolbar shortcut option")
        homepageToolbarShortcutOption().click()
        Log.i(TAG, "clickTheHomepageToolbarShortcut: Clicked the \"Homepage\" simple toolbar shortcut option")
    }

    class Transition {
        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "goBack: Waited for device to be idle")
            Log.i(TAG, "goBack: Trying to click the navigate up toolbar button")
            goBackButton().perform(click())
            Log.i(TAG, "goBack: Clicked the navigate up toolbar button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }
    }
}

private fun darkModeToggle() = onView(withText("Dark"))

private fun lightModeToggle() = onView(withText("Light"))

private fun topToolbarToggle() = onView(withText("Top"))

private fun bottomToolbarToggle() = onView(withText("Bottom"))

private fun deviceModeToggle(): ViewInteraction {
    val followDeviceThemeText =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) "Follow device theme" else "Set by Battery Saver"
    return onView(withText(followDeviceThemeText))
}

private fun swipeToolbarToggle() =
    onView(withText(getStringResource(R.string.preference_gestures_swipe_toolbar_switch_tabs_2)))

private fun pullToRefreshToggle() =
    onView(withText(getStringResource(R.string.preference_gestures_website_pull_to_refresh)))

private fun goBackButton() =
    onView(allOf(ViewMatchers.withContentDescription("Navigate up")))

private fun simpleToolbarLayoutToggle() = onView(withText("Simple"))

private fun expandedToolbarLayoutToggle() = onView(withText("Expanded"))

private fun customizeSettingsList() =
    UiScrollable(UiSelector().resourceId("$packageName:id/recycler_view"))

private fun showTabBarToggle() =
    onView(withText(getStringResource(R.string.preference_tab_strip_show)))

private fun openANewTabToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_new_tab)))

private fun shareToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_share)))

private fun addBookmarkToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_add_bookmark)))

private fun translateToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_translate)))

private fun homepageToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_homepage)))

private fun backToolbarShortcutOption() = onView(withText(getStringResource(R.string.toolbar_customize_shortcut_back)))
