/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.UiSelector
import org.hamcrest.CoreMatchers
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.Matchers
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.isChecked
import org.mozilla.fenix.wallpapers.Wallpaper

/** Implementation of Robot Pattern for the settings Homepage sub menu. */
class SettingsSubMenuHomepageRobot {

    fun verifyHomePageView(
        shortcutsSwitchEnabled: Boolean = true,
        sponsoredShortcutsCheckBox: Boolean = true,
        continueSwitchEnabled: Boolean = true,
        recentBookmarksSwitchEnabled: Boolean = true,
        recentlyVisitedSwitchEnabled: Boolean = true,
        pocketSwitchEnabled: Boolean = true,
        sponsoredStoriesCheckBox: Boolean = true,
    ) {
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Shortcuts\" option is visible")
        shortcutsButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Shortcuts\" option is visible")
        if (shortcutsSwitchEnabled) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Shortcuts\" toggle is checked")
            shortcutsButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Shortcuts\" toggle is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Shortcuts\" toggle is not checked")
            shortcutsButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isNotChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Shortcuts\" toggle is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored shortcuts\" option is visible")
        sponsoredShortcutsButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored shortcuts\" option is visible")
        if (sponsoredShortcutsCheckBox) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored shortcuts\" check box is checked")
            sponsoredShortcutsButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored shortcuts\" check box is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored shortcuts\" check box is not checked")
            sponsoredShortcutsButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isNotChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored shortcuts\" check box is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Continue\" option is visible")
        continueButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Continue\" option is visible")
        if (continueSwitchEnabled) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Continue\" toggle is checked")
            continueButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Continue\" toggle is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Continue\" toggle is not checked")
            continueButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isNotChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Continue\" toggle is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recent bookmarks\" option is visible")
        recentBookmarksButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Recent bookmarks\" option is visible")
        if (recentBookmarksSwitchEnabled) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recent bookmarks\" toggle is checked")
            recentBookmarksButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Recent bookmarks\" toggle is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recent bookmarks\" toggle is not checked")
            recentBookmarksButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isNotChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Recent bookmarks\" toggle is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recently visited\" option is visible")
        recentlyVisitedButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Recently visited\" option is visible")
        if (recentlyVisitedSwitchEnabled) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recently visited\" toggle is checked")
            recentlyVisitedButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Recently visited\" toggle is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Recently visited\" toggle is not checked")
            recentlyVisitedButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isNotChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Recently visited\" toggle is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Stories\" option is visible")
        pocketButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Stories\" option is visible")
        if (pocketSwitchEnabled) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Stories\" toggle is checked")
            pocketButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Stories\" toggle is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Stories\" toggle is not checked")
            pocketButton()
                .check(
                    matches(
                        TestHelper.hasCousin(
                            Matchers.allOf(
                                withClassName(Matchers.endsWith("Switch")),
                                isNotChecked(),
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Stories\" toggle is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored stories\" option is visible")
        sponsoredStoriesButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored stories\" option is visible")
        if (sponsoredStoriesCheckBox) {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored stories\" check box is checked")
            sponsoredStoriesButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored stories\" check box is checked")
        } else {
            Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Sponsored stories\" check box is not checked")
            sponsoredStoriesButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isNotChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(TAG, "verifyHomePageView: Verified that the \"Sponsored stories\" check box is not checked")
        }
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Opening screen\" heading is visible")
        openingScreenHeading().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Opening screen\" heading is visible")
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Homepage\" option is visible")
        homepageButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Homepage\" option is visible")
        Log.i(TAG, "verifyHomePageView: Trying to verify that the \"Last tab\" option is visible")
        lastTabButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomePageView: Verified that the \"Last tab\" option is visible")
        Log.i(
            TAG,
            "verifyHomePageView: Trying to verify that the \"Homepage after four hours of inactivity\" option is visible",
        )
        homepageAfterFourHoursButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyHomePageView: Verified that the \"Homepage after four hours of inactivity\" option is visible",
        )
    }

    fun verifySelectedOpeningScreenOption(openingScreenOption: String) {
        Log.i(
            TAG,
            "verifySelectedOpeningScreenOption: Trying to verify that the \"Opening screen\" option $openingScreenOption is checked",
        )
        onView(
                allOf(
                    withId(R.id.radio_button),
                    hasSibling(withText(openingScreenOption)),
                )
            )
            .check(matches(isChecked(true)))
        Log.i(
            TAG,
            "verifySelectedOpeningScreenOption: Verified that the \"Opening screen\" option $openingScreenOption is checked",
        )
    }

    fun clickShortcutsButton() {
        Log.i(TAG, "clickShortcutsButton: Trying to click the \"Shortcuts\" option")
        shortcutsButton().click()
        Log.i(TAG, "clickShortcutsButton: Clicked the \"Shortcuts\" option")
    }

    fun clickSponsoredShortcuts() {
        Log.i(TAG, "clickSponsoredShortcuts: Trying to click the \"Sponsored shortcuts\" option")
        sponsoredShortcutsButton().click()
        Log.i(TAG, "clickSponsoredShortcuts: Clicked the \"Sponsored shortcuts\" option")
    }

    fun clickContinueButton() {
        Log.i(TAG, "clickContinueButton: Trying to click the \"Continue\" option")
        continueButton().click()
        Log.i(TAG, "clickContinueButton: Clicked the \"Continue\" option")
    }

    fun clickRecentlyVisited() {
        Log.i(TAG, "clickRecentlyVisited: Trying to click the \"Recently visited\" option")
        recentlyVisitedButton().click()
        Log.i(TAG, "clickRecentlyVisited: Clicked the \"Recently visited\" option")
    }

    fun clickRecentBookmarksButton() {
        Log.i(TAG, "clickRecentBookmarksButton: Trying to click the \"Recent bookmarks\" option")
        recentBookmarksButton().click()
        Log.i(TAG, "clickRecentBookmarksButton: Clicked the \"Recent bookmarks\" option")
    }

    fun clickRecentSearchesButton() {
        Log.i(TAG, "clickRecentSearchesButton: Trying to click the \"Recently visited\" option")
        recentlyVisitedButton().click()
        Log.i(TAG, "clickRecentSearchesButton: Clicked the \"Recently visited\" option")
    }

    fun clickOpeningScreenOption(openingScreenOption: String) {
        Log.i(TAG, "clickOpeningScreenOption: Trying to click \"Opening screen\" option: $openingScreenOption")
        when (openingScreenOption) {
            "Homepage" -> homepageButton().click()
            "Last tab" -> lastTabButton().click()
            "Homepage after four hours of inactivity" -> homepageAfterFourHoursButton().click()
        }
        Log.i(TAG, "clickOpeningScreenOption: Clicked \"Opening screen\" option: $openingScreenOption")
    }

    fun verifySponsoredShortcutsCheckBox(checked: Boolean) {
        if (checked) {
            Log.i(
                TAG,
                "verifySponsoredShortcutsCheckBox: Trying to verify that the \"Sponsored shortcuts\" check box is checked",
            )
            sponsoredShortcutsButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(
                TAG,
                "verifySponsoredShortcutsCheckBox: Verified that the \"Sponsored shortcuts\" check box is checked",
            )
        } else {
            Log.i(
                TAG,
                "verifySponsoredShortcutsCheckBox: Trying to verify that the \"Sponsored shortcuts\" check box is not checked",
            )
            sponsoredShortcutsButton()
                .check(
                    matches(
                        hasSibling(
                            ViewMatchers.withChild(
                                allOf(
                                    withClassName(CoreMatchers.endsWith("CheckBox")),
                                    isNotChecked(),
                                )
                            )
                        )
                    )
                )
            Log.i(
                TAG,
                "verifySponsoredShortcutsCheckBox: Verified that the \"Sponsored shortcuts\" check box is not checked",
            )
        }
    }

    fun clickTheSolidWallpaper(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickTheSolidWallpaper: Trying to click solid wallpaper")
        composeTestRule.solidWallpaper().performClick()
        Log.i(TAG, "clickTheSolidWallpaper: Clicked solid wallpaper")
        // Wait for the wallpaper state to propagate and background to be applied
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
    }

    fun verifyTheWallpapersSettingsPageHeader() {
        Log.i(TAG, "verifyTheWallpapersSettingsPageHeader: Trying to verify that the \"Wallpapers\" header is visible")
        wallpapersPageHeader().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheWallpapersSettingsPageHeader: Verified that the \"Wallpapers\" header is visible")
        Log.i(TAG, "verifyTheWallpapersSettingsPageHeader: Trying to verify that the back button is visible")
        wallpapersPageBackButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyTheWallpapersSettingsPageHeader: Verified that the back button is visible")
    }

    fun verifyClassicFirefoxSection(composeTestRule: ComposeTestRule) {
        Log.i(
            TAG,
            "verifyClassicFirefoxSection: Trying to verify that the \"Classic Firefox\" wallpaper collection is visible",
        )
        composeTestRule.classicFirefoxWallpaperSectionHeader().assertIsDisplayed()
        Log.i(TAG, "verifyClassicFirefoxSection: Verified that the \"Classic Firefox\" wallpaper collection is visible")
    }

    fun verifyGradientWallpaperIsDisplayed(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyGradientWallpaperIsDisplayed: Trying to verify that gradient wallpaper is visible")
        composeTestRule.gradientWallpaper().assertIsDisplayed()
        Log.i(TAG, "verifyGradientWallpaperIsDisplayed: Verified that gradient wallpaper is visible")
    }

    fun verifySolidWallpaperIsDisplayed(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifySolidWallpaperIsDisplayed: Trying to verify that solid wallpaper is visible")
        composeTestRule.solidWallpaper().assertIsDisplayed()
        Log.i(TAG, "verifySolidWallpaperIsDisplayed: Verified that solid wallpaper is visible")
    }

    fun verifyGradientWallpaperIsSelected(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyGradientWallpaperIsSelected: Trying to verify that gradient wallpaper is selected")
        composeTestRule.gradientWallpaperSelected().assertExists()
        Log.i(TAG, "verifyGradientWallpaperIsSelected: Verified that gradient wallpaper is selected")
    }

    fun verifySolidWallpaperIsSelected(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifySolidWallpaperIsSelected: Trying to verify that solid wallpaper is selected")
        composeTestRule.solidWallpaperSelected().assertExists()
        Log.i(TAG, "verifySolidWallpaperIsSelected: Verified that solid wallpaper is selected")
    }

    fun verifyGradientWallpaperIsNotSelected(composeTestRule: ComposeTestRule) {
        Log.i(
            TAG,
            "verifyGradientWallpaperIsNotSelected: Trying to verify that gradient wallpaper is not selected",
        )
        composeTestRule.gradientWallpaperSelected().assertDoesNotExist()
        Log.i(TAG, "verifyGradientWallpaperIsNotSelected: Verified that gradient wallpaper is not selected")
    }

    fun verifySolidWallpaperIsNotSelected(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifySolidWallpaperIsNotSelected: Trying to verify that solid wallpaper is not selected")
        composeTestRule.solidWallpaperSelected().assertDoesNotExist()
        Log.i(TAG, "verifySolidWallpaperIsNotSelected: Verified that solid wallpaper is not selected")
    }

    class Transition {

        fun goBackToHomeScreen(
            composeTestRule: ComposeTestRule,
            interact: HomeScreenRobot.() -> Unit,
        ): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click the navigate up toolbar button")
            goBackButton().click()
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up toolbar button")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the navigate up toolbar button")
            goBackButton().click()
            Log.i(TAG, "goBack: Clicked the navigate up toolbar button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun clickSnackBarViewButton(
            composeTestRule: ComposeTestRule,
            interact: HomeScreenRobot.() -> Unit,
        ): HomeScreenRobot.Transition {
            Log.i(
                TAG,
                "clickSnackBarViewButton: Waiting for $waitingTimeShort ms for \"VIEW\" snackbar button to exist",
            )
            mDevice.findObject(UiSelector().text("VIEW")).waitForExists(waitingTimeShort)
            Log.i(TAG, "clickSnackBarViewButton: Waited for $waitingTimeShort ms for \"VIEW\" snackbar button to exist")
            Log.i(TAG, "clickSnackBarViewButton: Trying to click the \"VIEW\" snackbar button")
            mDevice.findObject(UiSelector().text("VIEW")).click()
            Log.i(TAG, "clickSnackBarViewButton: Clicked the \"VIEW\" snackbar button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun clickWallpapersMenuOption(interact: SettingsSubMenuHomepageRobot.() -> Unit): Transition {
            Log.i(
                TAG,
                "clickWallpapersMenuOption: Trying to click the \"Wallpapers\" option in the homepage settings menu",
            )
            wallpapersOptionMenu().click()
            Log.i(TAG, "clickWallpapersMenuOption: Clicked the \"Wallpapers\" option in the homepage settings menu")

            SettingsSubMenuHomepageRobot().interact()
            return Transition()
        }
    }
}

private fun shortcutsButton() = onView(allOf(withText(R.string.top_sites_toggle_top_recent_sites_4)))

private fun sponsoredShortcutsButton() = onView(allOf(withText(R.string.customize_toggle_contile)))

private fun continueButton() = onView(allOf(withText(R.string.customize_toggle_continue)))

private fun recentBookmarksButton() = onView(allOf(withText(R.string.customize_toggle_bookmarks)))

private fun recentlyVisitedButton() = onView(allOf(withText(R.string.customize_toggle_recently_visited)))

private fun pocketButton() = onView(allOf(withText(R.string.customize_toggle_pocket_3)))

private fun sponsoredStoriesButton() = onView(allOf(withText(R.string.customize_toggle_pocket_sponsored)))

private fun openingScreenHeading() = onView(withText(R.string.preferences_opening_screen))

private fun homepageButton() =
    onView(
        allOf(
            withId(R.id.title),
            withText(R.string.opening_screen_homepage),
            hasSibling(withId(R.id.radio_button)),
        )
    )

private fun lastTabButton() =
    onView(
        allOf(
            withId(R.id.title),
            withText(R.string.opening_screen_last_tab),
            hasSibling(withId(R.id.radio_button)),
        )
    )

private fun homepageAfterFourHoursButton() =
    onView(
        allOf(
            withId(R.id.title),
            withText(R.string.opening_screen_after_four_hours_of_inactivity),
            hasSibling(withId(R.id.radio_button)),
        )
    )

private fun goBackButton() = onView(allOf(withContentDescription(R.string.action_bar_up_description)))

private fun wallpapersOptionMenu() = onView(withText(R.string.customize_wallpapers))

private fun wallpapersPageHeader() = onView(withText(R.string.customize_wallpapers))

private fun wallpapersPageBackButton() = onView(withContentDescription(R.string.action_bar_up_description))

private fun ComposeTestRule.gradientWallpaper() =
    onNodeWithContentDescription(
        getStringResource(R.string.wallpapers_item_name_content_description, Wallpaper.GRADIENT),
        useUnmergedTree = true,
    )

private fun ComposeTestRule.solidWallpaper() =
    onNodeWithContentDescription(
        getStringResource(R.string.wallpapers_item_name_content_description, Wallpaper.SOLID),
        useUnmergedTree = true,
    )

private fun ComposeTestRule.gradientWallpaperSelected() =
    onNodeWithTag(
        "wallpaper.thumbnail.selected.${Wallpaper.GRADIENT}",
        useUnmergedTree = true,
    )

private fun ComposeTestRule.solidWallpaperSelected() =
    onNodeWithTag(
        "wallpaper.thumbnail.selected.${Wallpaper.SOLID}",
        useUnmergedTree = true,
    )

private fun ComposeTestRule.classicFirefoxWallpaperSectionHeader() =
    onNodeWithText(
        text = "Classic",
        substring = true,
        useUnmergedTree = true,
    )
