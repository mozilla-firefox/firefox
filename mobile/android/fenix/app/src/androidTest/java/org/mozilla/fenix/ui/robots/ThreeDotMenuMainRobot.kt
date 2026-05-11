/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiSelector
import org.hamcrest.Matchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_OFF
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_ON
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS_OPTION_CHEVRON
import org.mozilla.fenix.components.menu.MenuDialogTestTag.MORE_OPTION_CHEVRON
import org.mozilla.fenix.helpers.Constants.LONG_CLICK_DURATION
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndDescription
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.click
import mozilla.components.browser.menu.R as menuR

/**
 * Implementation of Robot Pattern for the three dot (main) menu.
 */
@Suppress("ForbiddenComment")
class ThreeDotMenuMainRobot(private val composeTestRule: ComposeTestRule) {
    fun verifyShareAllTabsButton() {
        Log.i(TAG, "verifyShareAllTabsButton: Trying to verify that the \"Share all tabs\" menu button is displayed")
        shareAllTabsButton().check(matches(isDisplayed()))
        Log.i(TAG, "verifyShareAllTabsButton: Verified that the \"Share all tabs\" menu button is displayed")
    }

    fun verifyThreeDotMenuExists() {
        Log.i(TAG, "verifyThreeDotMenuExists: Trying to verify that the three dot menu is displayed")
        threeDotMenuRecyclerView().check(matches(isDisplayed()))
        Log.i(TAG, "verifyThreeDotMenuExists: Verified that the three dot menu is displayed")
    }
    fun verifyBookmarkThisPageButton() {
        Log.i(TAG, "verifyEditBookmarkButton: Trying to verify that the \"Bookmark this page\" button is visible")
        composeTestRule.bookmarkPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyEditBookmarkButton: Verified that the \"Bookmark this page\" button is visible")
    }
    fun verifyEditBookmarkButton() {
        Log.i(TAG, "clickEditBookmarkButton: Trying to verify the \"Edit bookmark\" button is displayed.")
        composeTestRule.editBookmarkButton().assertIsDisplayed()
        Log.i(TAG, "clickEditBookmarkButton: Verified the \"Edit bookmark\" button is displayed.")
    }
    fun verifyCloseAllTabsButton() {
        Log.i(TAG, "verifyCloseAllTabsButton: Trying to verify that the \"Close all tabs\" button is visible")
        closeAllTabsButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyCloseAllTabsButton: Verified that the \"Close all tabs\" button is visible")
    }
    fun verifyCustomizeReaderViewButton(isDisplayed: Boolean) {
        waitForAppWindowToBeUpdated()
        if (isDisplayed) {
            Log.i(TAG, "verifyCloseAllTabsButton: Trying to verify that the \"Customize Reader view\" button is displayed")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_customize_reader_view_2), useUnmergedTree = true).assertIsDisplayed()
            Log.i(TAG, "verifyCloseAllTabsButton: Verified that the \"Customize Reader view\" button is displayed")
        } else {
            Log.i(TAG, "verifyCloseAllTabsButton: Trying to verify that the \"Customize Reader view\" button is not displayed")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_customize_reader_view_2), useUnmergedTree = true).assertIsNotDisplayed()
            Log.i(TAG, "verifyCloseAllTabsButton: Verified that the \"Customize Reader view\" button is not displayed")
        }
    }

    fun verifyQuitButtonExists() {
        Log.i(TAG, "verifyShareTabButton: Trying to verify that the \"Quit $appName\" button is visible")
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_delete_browsing_data_on_quit, appName)).assertIsDisplayed()
        Log.i(TAG, "verifyShareTabButton: Verified that the \"Quit $appName\" button is visible")
    }

    fun verifyShareTabButton() {
        Log.i(TAG, "verifyShareTabButton: Trying to verify that the \"Share all tabs\" button is visible")
        shareTabButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyShareTabButton: Verified that the \"Share all tabs\" button is visible")
    }
    fun verifySelectTabs() {
        Log.i(TAG, "verifySelectTabs: Trying to verify that the \"Select tabs\" button is visible")
        selectTabsButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySelectTabs: Verified that the \"Select tabs\" button is visible")
    }

    fun verifyRemoveFromShortcutsButton() {
        Log.i(TAG, "clickRemoveFromShortcutsButton: Trying to verify that the \"Remove from shortcuts\" button is displayed.")
        composeTestRule.removeFromShortcutsButton().assertIsDisplayed()
        Log.i(TAG, "clickRemoveFromShortcutsButton: Clicked the \"Remove from shortcuts\" button is displayed.")
    }

    fun verifyShareTabsOverlay() {
        Log.i(TAG, "verifyShareTabsOverlay: Trying to verify that the share overlay site list is displayed")
        onView(withId(R.id.shared_site_list)).check(matches(isDisplayed()))
        Log.i(TAG, "verifyShareTabsOverlay: Verified that the share overlay site list is displayed")
        Log.i(TAG, "verifyShareTabsOverlay: Trying to verify that the shared tab title is displayed")
        onView(withId(R.id.share_tab_title)).check(matches(isDisplayed()))
        Log.i(TAG, "verifyShareTabsOverlay: Verified that the shared tab title is displayed")
        Log.i(TAG, "verifyShareTabsOverlay: Trying to verify that the shared tab favicon is displayed")
        onView(withId(R.id.share_tab_favicon)).check(matches(isDisplayed()))
        Log.i(TAG, "verifyShareTabsOverlay: Verified that the shared tab favicon is displayed")
        Log.i(TAG, "verifyShareTabsOverlay: Trying to verify that the shared tab url is displayed")
        onView(withId(R.id.share_tab_url)).check(matches(isDisplayed()))
        Log.i(TAG, "verifyShareTabsOverlay: Verified that the shared tab url is displayed")
    }

    fun verifySwitchToDesktopSiteButton() {
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Trying to verify that the \"Switch to desktop site\" button is displayed.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Verified that the \"Switch to desktop site\" button is displayed.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifySwitchToDesktopSiteButtonIsEnabled(isEnabled: Boolean) {
        Log.i(TAG, "verifySuggestedUserName: Waiting for the \"Desktop site\" button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.browser_menu_desktop_site), substring = true))
        Log.i(TAG, "verifySuggestedUserName: Waited for the \"Desktop site\" button to exist")
        Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Trying to verify the Switch to Desktop Site button from the new main menu design is enabled.")
        if (isEnabled) {
            composeTestRule.desktopSiteButton().assertIsEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is enabled.")
        } else {
            composeTestRule.desktopSiteButton().assertIsNotEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is disabled.")
        }
    }

    fun verifyDesktopSiteButtonState(isEnabled: Boolean) {
        if (isEnabled) {
            Log.i(TAG, "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"On\".")
            composeTestRule.enabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"On\".")
        } else {
            Log.i(TAG, "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"Off\".")
            composeTestRule.disabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"Off\".")
        }
    }

    fun verifyPageMainMenuItems() {
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify the main menu items on the web page.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Back\" button exists.")
        composeTestRule.backButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Back\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Forward\" button exists.")
        composeTestRule.forwardButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Forward\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Refresh\" button exists.")
        composeTestRule.refreshButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Refresh\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Share\" button exists.")
        composeTestRule.shareButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Share\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Bookmark page\" button exists.")
        assertUIObjectExists(itemWithDescription(getStringResource(R.string.browser_menu_bookmark_this_page_2)))
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Bookmark page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Find in page\" button exists.")
        composeTestRule.findInPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Find in page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Desktop site\" button exists.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Desktop site\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.extensionsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Extensions\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"More\" button exists.")
        composeTestRule.moreButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"More\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"History\" button exists.")
        composeTestRule.historyButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"History\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Bookmarks\" button exists.")
        composeTestRule.bookmarksButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Bookmarks\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Downloads\" button exists.")
        composeTestRule.downloadsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Downloads\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Passwords\" button exists.")
        composeTestRule.passwordsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Passwords\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Sign in\" button exists.")
        composeTestRule.signInButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Sign in\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Settings\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Verified the main menu items on the web page.")
    }

    fun verifyPageMainMenuItemsInLandscapeMode() {
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify the main menu items on the web page in landscape mode.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Back\" button exists.")
        composeTestRule.backButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Back\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Forward\" button exists.")
        composeTestRule.forwardButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Forward\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Refresh\" button exists.")
        composeTestRule.refreshButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Refresh\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Share\" button exists.")
        composeTestRule.shareButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Share\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Bookmark page\" button exists.")
        assertUIObjectExists(itemWithDescription(getStringResource(R.string.browser_menu_bookmark_this_page_2)))
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Bookmark page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Find in page\" button exists.")
        composeTestRule.findInPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Find in page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Desktop site\" button exists.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Desktop site\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.extensionsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Extensions\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to verify that the \"More\" button exists.")
        composeTestRule.moreButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"More\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"History\" button exists.")
        composeTestRule.historyButton().performScrollTo().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"History\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"Bookmarks\" button exists.")
        composeTestRule.bookmarksButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Bookmarks\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"Downloads\" button exists.")
        composeTestRule.downloadsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Downloads\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"Passwords\" button exists.")
        composeTestRule.passwordsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Passwords\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"Sign in\" button exists.")
        composeTestRule.signInButton().performScrollTo().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Sign in\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Trying to scroll to and verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().performScrollTo().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified that the \"Settings\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItemsInLandscapeMode: Verified the main menu items on the web page in landscape mode.")
    }

    fun verifyHomeMainMenuItems() {
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Make Firefox your default\" button exists.")
        verifyMakeFirefoxYourDefaultBrowserPromotionBanner()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Make Firefox your default\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.extensionsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Extensions\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"History\" button exists.")
        composeTestRule.historyButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"History\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Bookmarks\" button exists.")
        composeTestRule.bookmarksButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Bookmarks\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Downloads\" button exists.")
        composeTestRule.downloadsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Downloads\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Passwords\" button exists.")
        composeTestRule.passwordsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Passwords\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Sign in\" button exists.")
        composeTestRule.signInButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Sign in\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Settings\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Verified the main menu items on the home page.")
    }

    fun verifyMainMenuCFR() {
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR title is displayed.")
        composeTestRule.mainMenuCFRTitle().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR title is displayed.")
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR message is displayed.")
        composeTestRule.mainMenuCFRMessage().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR message is displayed.")
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR dismiss button is displayed.")
        composeTestRule.closeMainMenuCFRButton().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR dismiss button is displayed.")
    }

    fun clickTheQuitFirefoxButton() {
        Log.i(TAG, "clickTheQuitFirefoxButton: Trying to click the \"Quit $appName\" button from the new main menu design.")
        composeTestRule.quitFirefoxButton().performClick()
        Log.i(TAG, "clickTheQuitFirefoxButton: Clicked the \"Quit $appName\" button from the new main menu design.")
    }

    fun verifyTranslatePageButton() {
        Log.i(TAG, "verifyTranslatePageButton: Trying to verify that the \"Translate page\" button exists.")
        composeTestRule.translatePageButton().assertIsDisplayed()
        Log.i(TAG, "verifyTranslatePageButton: Verified that the \"Translate page\" button exists.")
    }

    fun verifyMakeFirefoxYourDefaultBrowserPromotionBanner() {
        composeTestRule.onNodeWithText(getStringResource(R.string.browser_menu_default_banner_title, appName)).assertIsDisplayed()
        composeTestRule.onNodeWithText(getStringResource(R.string.browser_menu_default_banner_subtitle_2)).assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_default_banner_dismiss_promotion)).assertIsDisplayed()
    }

    fun verifyMoreMainMenuItems() {
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify the more main menu items on the web page.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Translate page\" button exists.")
        composeTestRule.translatePageButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Translate page\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Report broken site\" button exists.")
        composeTestRule.reportBrokenSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Report broken site\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Add to shortcuts\" button exists.")
        composeTestRule.addToShortcutsButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Add to shortcuts\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Add app to Home screen\" button exists.")
        composeTestRule.addToHomeScreenButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Add app to Home screen\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Save to collection\" button exists.")
        composeTestRule.saveToCollectionButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Save to collection\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Open in app\" button exists.")
        composeTestRule.defaultOpenInAppButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Open in app\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Save as PDF\" button exists.")
        composeTestRule.saveAsPDFButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Save as PDF\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Print\" button exists.")
        composeTestRule.printContentButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Print\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Verified the more main menu items on the web page.")
    }

    fun clickTheMoreButton() {
        Log.i(TAG, "clickTheMoreButton: Trying to click the \"More\" button from the new main menu design.")
        composeTestRule.moreButton().performClick()
        Log.i(TAG, "clickTheMoreButton: Clicked the \"More\" button from the new main menu design.")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        waitForAppWindowToBeUpdated()
    }

    fun clickMoreOptionChevron() {
        Log.i(TAG, "clickMoreOptionChevron: Trying to click the \"More option chevron\" button from the new main menu design.")
        composeTestRule.moreChevronButton().performClick()
        Log.i(TAG, "clickMoreOptionChevron: Clicked the \"More option chevron\" button from the new main menu design.")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
        waitForAppWindowToBeUpdated()
    }

    fun clickSaveButton() {
        Log.i(TAG, "clickSaveButton: Trying to click the \"Save\" button from the new main menu design.")
        composeTestRule.saveMenuButton().performClick()
        Log.i(TAG, "clickSaveButton: Clicked the \"Save\" button from the new main menu design.")
    }

    fun verifyOpenInAppButtonIsEnabled(appName: String = "", isEnabled: Boolean) {
        Log.i(TAG, "verifyOpenInAppButtonIsEnabled: Trying to verify the Open in App button from the new main menu design is enabled.")
        when (appName) {
            "" -> composeTestRule.defaultOpenInAppButton().apply {
                if (isEnabled) assertIsEnabled() else assertIsNotEnabled()
                Log.i(
                    TAG,
                    "verifyOpenInAppButtonIsEnabled: Open in App button from the new main menu design is enabled = $isEnabled.",
                )
            }
            else -> composeTestRule.openInAppNameButton(appName).apply {
                if (isEnabled) assertIsEnabled() else assertIsNotEnabled()
                Log.i(TAG, "verifyOpenInAppButtonIsEnabled: Open in App button from the new main menu design is enabled = $isEnabled.")
            }
        }
    }

    fun clickOpenInAppButton(appName: String = "") {
        Log.i(TAG, "clickOpenInAppButton: Trying to click the Open in App button from the new main menu design.")
        when (appName) {
            "" -> composeTestRule.defaultOpenInAppButton().performClick()
            else -> composeTestRule.openInAppNameButton(appName).performClick()
        }
        Log.i(TAG, "clickOpenInAppButton: Clicked the Open in App button from the new main menu design.")
        mDevice.waitForIdle()
    }

    fun clickSwitchToDesktopSiteButton() {
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Trying to click the \"Switch to desktop site\" button.")
        composeTestRule.desktopSiteButton().performClick()
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Clicked the \"Switch to desktop site\" button.")
    }

    fun clickPrintContentButton() {
        Log.i(TAG, "clickPrintContentButton: Trying to click the \"Print…\" button.")
        composeTestRule.printContentButton().performClick()
        Log.i(TAG, "clickPrintContentButton: Clicked the \"Print…\" button.")
    }

    fun clickTheToolsButton() {
        Log.i(TAG, "clickTheToolsButton: Trying to click the Tools menu button from the new main menu design.")
        composeTestRule.toolsMenuButton().performClick()
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickTheToolsButton: Clicked the Tools menu button from the new main menu design.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyExtensionsButtonWithInstalledExtension(extensionTitle: String) {
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Trying to verify the extensions button shows: $extensionTitle")
        composeTestRule.waitUntil(waitingTimeLong) {
            itemWithResIdAndDescription(EXTENSIONS, extensionTitle).exists()
        }
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Verified the extensions button shows: $extensionTitle")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTryRecommendedExtensionButton() {
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Trying to verify that the \"Extensions - Try a recommended extension\" button exists.")
        composeTestRule.waitUntilAtLeastOneExists(
            hasContentDescription("Extensions Try a recommended extension", substring = true),
            waitingTimeLong,
        )
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Verified that the \"Extensions - Try a recommended extension\" button exists.")
    }

    fun verifyNoExtensionsEnabledButton() {
        Log.i(TAG, "verifyNoExtensionsEnabledButton: Trying to verify that the \"Extensions - Try a recommended extension\" button exists.")
        composeTestRule.noExtensionsEnabledButton().assertExists()
        Log.i(TAG, "verifyNoExtensionsEnabledButton: Verified that the \"Extensions - Try a recommended extension\" button exists.")
    }

    fun verifySettingsButton() {
        Log.i(TAG, "verifySettingsButton: Trying to verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(TAG, "verifySettingsButton: Verified that the \"Settings\" button exists.")
    }
    fun verifyHistoryButton() {
        Log.i(TAG, "verifyHistoryButton: Trying to verify that the \"History\" button exists.")
        composeTestRule.historyButton().assertIsDisplayed()
        Log.i(TAG, "verifyHistoryButton: Verified that the \"History\" button exists.")
    }

    fun verifyFindInPageButton() {
        Log.i(TAG, "verifyFindInPageButton: Trying to verify that the \"Find in page\" button exists.")
        composeTestRule.findInPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyFindInPageButton: Verified that the \"Find in page\" button exists.")
    }

    fun verifyAddToShortcutsButton(isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(TAG, "verifyAddToShortcutsButton: Trying to verify that the \"Add to shortcuts\" button is displayed.")
            composeTestRule.addToShortcutsButton().assertIsDisplayed()
            Log.i(TAG, "verifyAddToShortcutsButton: Verified that the \"Add to shortcuts\" button is displayed.")
        } else {
            Log.i(TAG, "verifyAddToShortcutsButton: Trying to verify that the \"Add to shortcuts\" button is not displayed.")
            composeTestRule.addToShortcutsButton().assertIsNotDisplayed()
            Log.i(TAG, "verifyAddToShortcutsButton: Verified that the \"Add to shortcuts\" button is not displayed.")
        }
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun clickSettingsButton(localizedText: String = getStringResource(R.string.browser_menu_settings), interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "clickSettingsButton: Trying to click the Settings button from the new main menu design.")
            itemWithDescription(localizedText).click()
            Log.i(TAG, "clickSettingsButton: Clicked the Settings button from the new main menu design.")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()
            waitForAppWindowToBeUpdated()

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun clickDownloadsButton(interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
            Log.i(TAG, "clickDownloadsButton: Trying to click the \"Download\" button from the new main menu design.")
            composeTestRule.downloadsButton().performClick()
            Log.i(TAG, "clickDownloadsButton: Clicked the \"Download\" button from the new main menu design.")

            DownloadRobot(composeTestRule).interact()
            return DownloadRobot.Transition(composeTestRule)
        }

        fun clickPasswordsButton(interact: SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.() -> Unit): SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition {
            Log.i(TAG, "clickPasswordsButton: Trying to click the \"Passwords\" button from the new main menu design.")
            composeTestRule.passwordsButton().performClick()
            Log.i(TAG, "clickPasswordsButton: Clicked the \"Passwords\" button from the new main menu design.")

            SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot(composeTestRule).interact()
            return SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition(composeTestRule)
        }

        fun clickSignInToSyncButton(interact: SettingsSignInToSyncRobot.() -> Unit): SettingsSignInToSyncRobot.Transition {
            Log.i(TAG, "clickSignInToSyncButton: Trying to click \"Sign in\" main menu button")
            composeTestRule.onNodeWithContentDescription("Sign in Sync passwords, bookmarks, and more").performClick()
            Log.i(TAG, "clickSignInToSyncButton: Clicked \"Sign in\" main menu button")

            SettingsSignInToSyncRobot().interact()
            return SettingsSignInToSyncRobot.Transition(composeTestRule)
        }

        fun clickBookmarksButton(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickBookmarksButton: Trying to click the Bookmarks button from the new main menu design.")
            itemWithDescription(getStringResource(R.string.library_bookmarks)).click()
            Log.i(TAG, "clickBookmarksButton: Clicked the Bookmarks button from the new main menu design.")
            composeTestRule.waitForIdle()

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickHistoryButton(interact: HistoryRobot.() -> Unit): HistoryRobot.Transition {
            Log.i(TAG, "clickHistoryButton: Trying to click the History button from the new main menu design.")
            composeTestRule.historyButton().performClick()
            Log.i(TAG, "clickHistoryButton: Clicked the History button from the new main menu design.")

            HistoryRobot().interact()
            return HistoryRobot.Transition(composeTestRule)
        }

        fun clickBookmarkThisPageButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickBookmarkThisPageButton: Trying to click the \"Bookmark this page\" button from the new main menu design.")
            composeTestRule.bookmarkPageButton().performClick()
            Log.i(TAG, "clickBookmarkThisPageButton: Clicked the \"Bookmark this page\" button from the new main menu design.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickEditBookmarkButton(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickEditBookmarkButton: Trying to click the \"Edit bookmark\" button from the new main menu design.")
            composeTestRule.editBookmarkButton().performClick()
            Log.i(TAG, "clickEditBookmarkButton: Clicked the \"Edit bookmark\" button from the new main menu design.")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickForwardButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickForwardButton: Trying to click the \"Forward\" button")
            composeTestRule.forwardButton().performClick()
            Log.i(TAG, "clickForwardButton: Clicked the \"Forward\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickPreviousPageButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickPreviousPageButton: Trying to click the \"Back\" button")
            composeTestRule.backButton().performClick()
            Log.i(TAG, "clickPreviousPageButton: Clicked the \"Back\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareButton: Trying to click the Share button from the new main menu design.")
            composeTestRule.shareButton().performClick()
            Log.i(TAG, "clickShareButton: Clicked the Share button from the new main menu design.")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun closeMainMenuAndExitToBrowserView(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "closeMainMenuAndExitToBrowserView: Trying to click device back button")
            mDevice.pressBack()
            Log.i(TAG, "closeMainMenuAndExitToBrowserView: Clicked the device back button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickRefreshButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            for (i in 1..RETRY_COUNT) {
                Log.i(TAG, "clickRefreshButton: Started try #$i")
                try {
                    Log.i(TAG, "clickRefreshButton: Waiting for $waitingTimeLong until the \"Stop\" button does not exist")
                    composeTestRule.waitUntilDoesNotExist(hasText("Stop"), waitingTimeLong)
                    Log.i(TAG, "clickRefreshButton: Waited for $waitingTimeLong until the \"Stop\" button does not exist")
                    Log.i(TAG, "clickRefreshButton: Waiting for $waitingTime until the \"Refresh\" button exists")
                    composeTestRule.waitUntilAtLeastOneExists(hasText("Refresh"), waitingTime)
                    Log.i(TAG, "clickRefreshButton: Waited for $waitingTime until the \"Refresh\" button exists")
                } catch (e: ComposeTimeoutException) {
                    Log.i(TAG, "clickRefreshButton: ComposeTimeoutException caught, executing fallback methods")
                    if (i == RETRY_COUNT) {
                        throw e
                    } else {
                        closeMainMenuAndExitToBrowserView {
                        }.openThreeDotMenu {
                        }
                    }
                }
            }
            Log.i(TAG, "clickRefreshButton: Trying to click the \"Refresh\" button")
            composeTestRule.refreshButton().performClick()
            Log.i(TAG, "clickRefreshButton: Clicked the \"Refresh\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun forceRefreshPage(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "forceRefreshPage: Trying to long click the \"Refresh\" button")
            mDevice.findObject(By.desc(getStringResource(R.string.browser_menu_refresh)))
                .click(LONG_CLICK_DURATION)
            Log.i(TAG, "forceRefreshPage: Long clicked the \"Refresh\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun closeAllTabs(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "closeAllTabs: Trying to click the \"Close all tabs\" button")
            closeAllTabsButton().click()
            Log.i(TAG, "closeAllTabs: Clicked the \"Close all tabs\" button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun clickReportBrokenSiteButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openReportSiteIssue: Trying to click the \"Report Site Issue\" button")
            composeTestRule.reportBrokenSiteButton().performClick()
            Log.i(TAG, "openReportSiteIssue: Clicked the \"Report Site Issue\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickFindInPageButton(interact: FindInPageRobot.() -> Unit): FindInPageRobot.Transition {
            Log.i(TAG, "clickFindInPageButton: Trying to click the FindInPage button from the new main menu design.")
            composeTestRule.findInPageButton().performClick()
            Log.i(TAG, "clickFindInPageButton: Clicked the FindInPage button from the new main menu design.")

            FindInPageRobot().interact()
            return FindInPageRobot.Transition()
        }

        fun clickCustomizeReaderViewButton(interact: ReaderViewRobot.() -> Unit): ReaderViewRobot.Transition {
            Log.i(TAG, "clickCustomizeReaderViewButton: Trying to click the \"Customize reader view\" button")
            composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_customize_reader_view_2), useUnmergedTree = true).performClick()
            Log.i(TAG, "clickCustomizeReaderViewButton: Clicked the \"Customize reader view\" button")

            ReaderViewRobot().interact()
            return ReaderViewRobot.Transition()
        }

        fun clickAddToShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickAddToShortcutsButton: Trying to click the \"Add to shortcuts\" button from the new main menu design.")
            composeTestRule.addToShortcutsButton().performClick()
            Log.i(TAG, "clickAddToShortcutsButton: Clicked the \"Add to shortcuts\" button from the new main menu design.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickRemoveFromShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickRemoveFromShortcutsButton: Trying to click the \"Remove from shortcuts\" button from the new main menu design.")
            composeTestRule.removeFromShortcutsButton().performClick()
            Log.i(TAG, "clickRemoveFromShortcutsButton: Clicked the \"Remove from shortcuts\" button from the new main menu design.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickAddToHomeScreenButton(interact: AddToHomeScreenRobot.() -> Unit): AddToHomeScreenRobot.Transition {
            Log.i(TAG, "clickAddToHomeScreenButton: Trying to click the \"Add to Home screen…\" button from the new main menu design.")
            composeTestRule.addToHomeScreenButton().performClick()
            Log.i(TAG, "clickAddToHomeScreenButton: Clicked the \"Add to Home screen…\" button from the new main menu design.")

            AddToHomeScreenRobot(composeTestRule).interact()
            return AddToHomeScreenRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickAddAppToHomeScreenButton(interact: AddToHomeScreenRobot.() -> Unit): AddToHomeScreenRobot.Transition {
            Log.i(TAG, "clickAddToHomeScreenButton: Trying to click the \"Add app to Home screen…\" button from the new main menu design.")
            composeTestRule.addAppToHomeScreenButton().performClick()
            Log.i(TAG, "clickAddToHomeScreenButton: Clicked the \"Add app to Home screen…\" button from the new main menu design.")
            Log.i(TAG, "clickAddToHomeScreenButton: Waiting for $waitingTime until the \"Add app to Home screen…\" button does not exist.")
            composeTestRule.waitUntilDoesNotExist(hasContentDescription(getStringResource(R.string.browser_menu_add_app_to_homescreen)), waitingTime)
            Log.i(TAG, "clickAddToHomeScreenButton: Waited for $waitingTime until the \"Add app to Home screen…\" button does not exist.")

            AddToHomeScreenRobot(composeTestRule).interact()
            return AddToHomeScreenRobot.Transition(composeTestRule)
        }

        fun clickSaveToCollectionButton(interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
            Log.i(TAG, "clickSaveToCollectionButton: Trying to click the \"Save to collection…\" button from the new main menu design.")
            composeTestRule.saveToCollectionButton().performClick()
            Log.i(TAG, "clickSaveToCollectionButton: Clicked the \"Save to collection…\" button from the new main menu design.")

            CollectionRobot(composeTestRule).interact()
            return CollectionRobot.Transition(composeTestRule)
        }

        fun clickExtensionsButton(interact: SettingsSubMenuAddonsManagerRobot.() -> Unit): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "clickExtensionsButton: Trying to click the \"Extensions\" button")
            assertUIObjectExists(itemWithResId("mainMenu.extensions"))
            itemWithResId("mainMenu.extensions").clickAndWaitForNewWindow(waitingTimeShort)
            Log.i(TAG, "clickExtensionsButton: Clicked the \"Extensions\" button")
            composeTestRule.waitForIdle()
            waitForAppWindowToBeUpdated()

            SettingsSubMenuAddonsManagerRobot(composeTestRule).interact()
            return SettingsSubMenuAddonsManagerRobot.Transition(composeTestRule)
        }

        fun clickShareAllTabsButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareAllTabsButton: Trying to click the \"Share all tabs\" button")
            shareAllTabsButton().click()
            Log.i(TAG, "clickShareAllTabsButton: Clicked the \"Share all tabs\" button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickTranslateButton(interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(TAG, "clickTranslateButton: Trying to click the Translate button from the new main menu design.")
            composeTestRule.translatePageButton().performClick()
            Log.i(TAG, "clickTranslateButton: Clicked the Translate button from the new main menu design.")
            Log.i(TAG, "clickTranslateButton: Waiting for compose test rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "clickTranslateButton: Waited for compose test rule to be idle")

            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickTranslatedButton(interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(TAG, "clickTranslateButton: Trying to click the Translate button from the new main menu design.")
            composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.browser_menu_translated)), waitingTime)
            composeTestRule.translatedButton().assertIsDisplayed()
            composeTestRule.translatedButton().performClick()
            Log.i(TAG, "clickTranslateButton: Clicked the Translate button from the new main menu design.")
            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        fun clickOutsideTheMainMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOutsideTheMainMenu: Trying to click outside the main menu.")
            itemWithResId("$packageName:id/touch_outside").clickTopLeft()
            Log.i(TAG, "clickOutsideTheMainMenu: Clicked click outside the main menu.")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSaveAsPDFButton(interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
            Log.i(TAG, "clickSaveAsPDFButton: Trying to click the \"Save as PDF…\" button from the new main menu design.")
            composeTestRule.saveAsPDFButton().performClick()
            Log.i(TAG, "clickSaveAsPDFButton: Clicked the \"Save as PDF…\" button from the new main menu design.")

            DownloadRobot(composeTestRule).interact()
            return DownloadRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickExtensionsChevronFromMainMenu(interact: SettingsSubMenuAddonsManagerRobot.() -> Unit): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "clickExtensionsChevronFromMainMenu: Trying to click the \"Extensions chevron\" button from the new main menu design.")
            composeTestRule.waitUntil(waitingTimeLong) {
                composeTestRule.onAllNodes(hasTestTag(EXTENSIONS_OPTION_CHEVRON), useUnmergedTree = true)
                    .fetchSemanticsNodes(atLeastOneRootRequired = false).isNotEmpty()
            }
            composeTestRule.extensionsChevronButton().performClick()
            Log.i(TAG, "clickExtensionsChevronFromMainMenu: Clicked the \"Extensions chevron\" button from the new main menu design.")

            SettingsSubMenuAddonsManagerRobot(composeTestRule).interact()
            return SettingsSubMenuAddonsManagerRobot.Transition(composeTestRule)
        }
    }
}

fun mainMenuScreen(composeTestRule: ComposeTestRule, interact: ThreeDotMenuMainRobot.() -> Unit): ThreeDotMenuMainRobot.Transition {
    ThreeDotMenuMainRobot(composeTestRule).interact()
    return ThreeDotMenuMainRobot.Transition(composeTestRule)
}

private fun threeDotMenuRecyclerView() =
    onView(withId(menuR.id.mozac_browser_menu_recyclerView))

private fun editBookmarkButton() = onView(withText("Edit"))

private fun closeAllTabsButton() = onView(allOf(withText("Close all tabs"))).inRoot(RootMatchers.isPlatformPopup())

private fun shareTabButton() = onView(allOf(withText("Share all tabs"))).inRoot(RootMatchers.isPlatformPopup())

private fun selectTabsButton() = onView(allOf(withText("Select tabs"))).inRoot(RootMatchers.isPlatformPopup())

private fun readerViewAppearanceToggle() =
    mDevice.findObject(UiSelector().text("Customize reader view"))

private fun removeFromShortcutsButton() =
    onView(allOf(withText(R.string.browser_menu_remove_from_shortcuts)))

private fun addAppToHomeScreenButton() =
    itemContainingText(getStringResource(R.string.browser_menu_add_app_to_homescreen))

private fun openInAppButton() =
    onView(
        allOf(
            withText("Open in app"),
            withEffectiveVisibility(Visibility.VISIBLE),
        ),
    )

private fun shareAllTabsButton() =
    onView(allOf(withText("Share all tabs"))).inRoot(RootMatchers.isPlatformPopup())

// ComposeMainMenu

private fun ComposeTestRule.mainMenuCFRTitle() = onNodeWithText(getStringResource(R.string.menu_cfr_title))

private fun ComposeTestRule.mainMenuCFRMessage() = onNodeWithText(getStringResource(R.string.menu_cfr_body))

private fun ComposeTestRule.closeMainMenuCFRButton() = onNodeWithTag("cfr.dismiss")

private fun ComposeTestRule.backButton() = onNodeWithText("Back")

private fun ComposeTestRule.forwardButton() = onNodeWithText("Forward")

private fun ComposeTestRule.refreshButton() = onNodeWithText("Refresh")

private fun ComposeTestRule.stopButton() = onNodeWithText("Stop")

private fun ComposeTestRule.shareButton() = onNodeWithText("Share")

private fun ComposeTestRule.signInButton() = onNodeWithContentDescription("Sign in Sync passwords, bookmarks, and more")

private fun ComposeTestRule.settingsButton(localizedText: String = getStringResource(R.string.browser_menu_settings)) =
    onNodeWithContentDescription(localizedText)

private fun ComposeTestRule.extensionsButton() = onNodeWithTag(EXTENSIONS)

private fun ComposeTestRule.tryRecommendedExtensionButton() = onNodeWithContentDescription("Extensions Try a recommended extension", substring = true)

private fun ComposeTestRule.noExtensionsEnabledButton() = onNodeWithContentDescription("Extensions No extensions enabled", substring = true)

private fun ComposeTestRule.moreButton() = onNodeWithContentDescription("More Collapsed")

private fun ComposeTestRule.moreChevronButton() = onNodeWithTag(MORE_OPTION_CHEVRON, useUnmergedTree = true)

private fun ComposeTestRule.bookmarksButton() = onNodeWithContentDescription(getStringResource(R.string.library_bookmarks), substring = true)

private fun ComposeTestRule.historyButton() = onNodeWithContentDescription(getStringResource(R.string.library_history), substring = true)

private fun ComposeTestRule.downloadsButton() = onNodeWithContentDescription(getStringResource(R.string.library_downloads), substring = true)

private fun ComposeTestRule.passwordsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_passwords), substring = true)

private fun ComposeTestRule.quitFirefoxButton() = onNodeWithContentDescription("Quit $appName")

// Page main menu items

private fun ComposeTestRule.findInPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_find_in_page), substring = true)

private fun ComposeTestRule.toolsMenuButton() = onNodeWithTag("mainMenu.tools")

private fun ComposeTestRule.saveMenuButton() = onNodeWithTag("mainMenu.save")

private fun ComposeTestRule.bookmarkPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_bookmark_this_page_2))

private fun ComposeTestRule.desktopSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_desktop_site), substring = true)

private fun ComposeTestRule.enabledDesktopSiteButton() = onNodeWithTag(DESKTOP_SITE_ON)

private fun ComposeTestRule.disabledDesktopSiteButton() = onNodeWithTag(DESKTOP_SITE_OFF)

// Save sub menu items

private fun ComposeTestRule.editBookmarkButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_edit_bookmark))

private fun ComposeTestRule.addToShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_shortcuts))

private fun ComposeTestRule.removeFromShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_remove_from_shortcuts))

private fun ComposeTestRule.addToHomeScreenButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_homescreen))

private fun ComposeTestRule.addAppToHomeScreenButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_app_to_homescreen))

private fun ComposeTestRule.saveToCollectionButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_to_collection_2))

private fun ComposeTestRule.saveAsPDFButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_as_pdf_2))

private fun ComposeTestRule.translatePageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translations))

private fun ComposeTestRule.translatedButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translated))

private fun ComposeTestRule.reportBrokenSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_webcompat_reporter_2))

private fun ComposeTestRule.printContentButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_print_2))

private fun ComposeTestRule.defaultOpenInAppButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_app_link))

private fun ComposeTestRule.openInAppNameButton(appName: String) = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_in_fenix, appName))

private fun ComposeTestRule.extensionsChevronButton() = onNodeWithTag(EXTENSIONS_OPTION_CHEVRON, useUnmergedTree = true)
