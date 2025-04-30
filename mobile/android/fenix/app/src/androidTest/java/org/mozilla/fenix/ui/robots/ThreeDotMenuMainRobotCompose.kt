/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.test.espresso.action.ViewActions.swipeUp
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName

class ThreeDotMenuMainRobotCompose(private val composeTestRule: ComposeTestRule) {

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

    fun expandMainMenu() {
        Log.i(TAG, "expandMainMenu: Trying to perform swipe up action on the main menu.")
        composeTestRule
            .onNode(hasAnyChild(hasContentDescription("New private tab")))
            .performTouchInput { swipeUp() }
        composeTestRule.waitForIdle()
        Log.i(TAG, "expandMainMenu: Performed swipe up action on the main menu.")
    }

    fun verifyHomeMainMenuItems() {
        Log.i(
            TAG,
            "verifyHomeMainMenuItems: Trying to verify the main menu items on the home page.",
        )
        composeTestRule.signInButton().assertIsDisplayed()
        composeTestRule.signInButtonDescription().assertIsDisplayed()
        composeTestRule.helpButton().assertIsDisplayed()
        composeTestRule.settingsButton().assertIsDisplayed()
        composeTestRule.newTabButton().assertIsNotEnabled()
        composeTestRule.newPrivateTabButton().assertIsDisplayed()
        composeTestRule.extensionsButton().assertIsDisplayed()
        composeTestRule.bookmarksButton().assertIsDisplayed()
        composeTestRule.historyButton().assertIsDisplayed()
        composeTestRule.downloadsButton().assertIsDisplayed()
        composeTestRule.passwordsButton().assertIsDisplayed()
        composeTestRule.whatsNewButton().assertIsDisplayed()
        composeTestRule.customizeHomeButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyHomeMainMenuItems: Verified the main menu items on the home page.",
        )
    }

    fun verifyPageMainMenuItems() {
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify the main menu items on the web page.")
        composeTestRule.signInButton().assertIsDisplayed()
        composeTestRule.signInButtonDescription().assertIsDisplayed()
        composeTestRule.helpButton().assertIsDisplayed()
        composeTestRule.settingsButton().assertIsDisplayed()
        composeTestRule.newTabButton().assertIsEnabled()
        composeTestRule.newPrivateTabButton().assertIsDisplayed()
        composeTestRule.extensionsButton().assertIsDisplayed()
        composeTestRule.bookmarksButton().assertIsDisplayed()
        composeTestRule.historyButton().assertIsDisplayed()
        composeTestRule.downloadsButton().assertIsDisplayed()
        composeTestRule.passwordsButton().assertIsDisplayed()
        composeTestRule.findInPageButton().assertIsDisplayed()
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        composeTestRule.toolsMenuButton().assertIsDisplayed()
        composeTestRule.saveMenuButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified the main menu items on the web page.")
    }

    fun verifySaveSubMenuItems() {
        Log.i(TAG, "verifySaveSubMenuItems: Trying to verify the \"Save\" sub menu items.")
        composeTestRule.backToMainMenuButton().assertIsDisplayed()
        composeTestRule.saveSubMenuTitle().assertIsDisplayed()
        composeTestRule.bookmarkThisPageButton().assertIsDisplayed()
        composeTestRule.addToShortcutsButton().assertIsDisplayed()
        composeTestRule.addToHomeScreenButton().assertIsDisplayed()
        composeTestRule.saveToCollectionButton().assertIsDisplayed()
        composeTestRule.saveAsPDFButton().assertIsDisplayed()
        Log.i(TAG, "verifySaveSubMenuItems: Verified the \"Save\" sub menu items.")
    }

    fun openToolsMenu() {
        Log.i(
            TAG,
            "openToolsMenu: Trying to click the Tools menu button from the new main menu design.",
        )
        composeTestRule.toolsMenuButton().performClick()
        composeTestRule.waitForIdle()
        Log.i(TAG, "openToolsMenu: Clicked the Tools menu button from the new main menu design.")
    }

    fun verifyTheDefaultToolsMenuItems() {
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Trying to verify the default tools menu items from the new main menu design.",
        )
        composeTestRule.toolsMenuHeader().assertIsDisplayed()
        Log.i(TAG, "verifyTheDefaultToolsMenuItems: Verified the tools menu header is displayed.")
        composeTestRule.backToMainMenuButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Verified the back to main menu button is displayed.",
        )
        composeTestRule.turnOnReaderViewButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Verified the Turn on Reader View button is displayed.",
        )
        composeTestRule.translatePageButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Verified the Translate Page button is displayed.",
        )
        composeTestRule.reportBrokenSiteButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Verified the Report Broken Site button is displayed.",
        )
        composeTestRule.printContentButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheDefaultToolsMenuItems: Verified the Print Content button is displayed.",
        )
        composeTestRule.shareButton().assertIsDisplayed()
        Log.i(TAG, "verifyTheDefaultToolsMenuItems: Verified the Share button is displayed.")
        composeTestRule.defaultOpenInAppButton().assertIsDisplayed()
        Log.i(TAG, "verifyTheDefaultToolsMenuItems: Verified the Open in App button is displayed.")
    }

    fun clickPrintContentButton() {
        Log.i(TAG, "clickPrintContentButton: Trying to click the \"Print…\" button.")
        composeTestRule.printContentButton().performClick()
        Log.i(TAG, "clickPrintContentButton: Clicked the \"Print…\" button.")
    }

    fun verifySettingsButton() {
        Log.i(
            TAG,
            "verifySettingsButton: Trying to verify the Settings button from the new main menu design is displayed.",
        )
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(
            TAG,
            "verifySettingsButton: Verified the Settings button from the new main menu design is displayed.",
        )
    }

    fun verifySwitchToDesktopSiteButtonIsEnabled(
        isEnabled: Boolean,
    ) {
        Log.i(
            TAG,
            "verifySwitchToDesktopSiteButtonIsEnabled: Trying to verify the Switch to Desktop Site button from the new main menu design is enabled.",
        )
        if (isEnabled) {
            composeTestRule.desktopSiteButton().assertIsEnabled()
            Log.i(
                TAG,
                "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is enabled.",
            )
        } else {
            composeTestRule.desktopSiteButton().assertIsNotEnabled()
            Log.i(
                TAG,
                "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is disabled.",
            )
        }
    }

    fun verifySwitchToDesktopSiteButton() {
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Trying to verify that the \"Switch to desktop site\" button is displayed.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Verified that the \"Switch to desktop site\" button is displayed.")
    }

    fun verifySwitchToMobileSiteButton() {
        Log.i(TAG, "verifySwitchToMobileSiteButton: Trying to verify that the \"Switch to mobile site\" button is displayed.")
        composeTestRule.mobileSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToMobileSiteButton: Verified that the \"Switch to mobile site\" button is displayed.")
    }

    fun clickSwitchToDesktopSiteButton() {
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Trying to click the \"Switch to desktop site\" button.")
        composeTestRule.desktopSiteButton().performClick()
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Clicked the \"Switch to desktop site\" button.")
    }

    fun clickSwitchToMobileSiteButton() {
        Log.i(TAG, "clickSwitchToMobileSiteButton: Trying to click the \"Switch to mobile site\" button.")
        composeTestRule.mobileSiteButton().performClick()
        Log.i(TAG, "clickSwitchToMobileSiteButton: Clicked the \"Switch to mobile site\" button.")
    }

    fun verifyReaderViewButtonIsEnabled(isEnabled: Boolean) {
        Log.i(
            TAG,
            "verifyReaderViewButtonIsEnabled: Trying to verify the Reader View button from the new main menu design is enabled.",
        )
        composeTestRule.turnOnReaderViewButton().apply {
            if (isEnabled) assertIsEnabled() else assertIsNotEnabled()
            Log.i(
                TAG,
                "verifyReaderViewButtonIsEnabled: Reader View button from the new main menu design is enabled = $isEnabled.",
            )
        }
    }

    fun verifyOpenInAppButtonIsEnabled(appName: String = "", isEnabled: Boolean) {
        Log.i(
            TAG,
            "verifyOpenInAppButtonIsEnabled: Trying to verify the Open in App button from the new main menu design is enabled.",
        )
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
                Log.i(
                    TAG,
                    "verifyOpenInAppButtonIsEnabled: Open in App button from the new main menu design is enabled = $isEnabled.",
                )
            }
        }
    }

    fun verifyCustomizeReaderViewButtonIsDisplayed(isDisplayed: Boolean) {
        Log.i(
            TAG,
            "verifyCustomizeReaderViewButton: Trying to verify the Customize Reader View button from the new main menu design is displayed $isDisplayed.",
        )
        composeTestRule.customizeReaderViewButton().apply {
            if (isDisplayed) assertIsDisplayed() else assertIsNotDisplayed()
            Log.i(
                TAG,
                "verifyCustomizeReaderViewButton: Verified the Customize Reader View button from the new main menu design is displayed = $isDisplayed.",
            )
        }
    }

    fun clickOpenInAppButton(appName: String = "") {
        Log.i(
            TAG,
            "clickOpenInAppButton: Trying to click the Open in App button from the new main menu design.",
        )
        when (appName) {
            "" -> composeTestRule.defaultOpenInAppButton().performClick()
            else -> composeTestRule.openInAppNameButton(appName).performClick()
        }
        Log.i(
            TAG,
            "clickOpenInAppButton: Clicked the Open in App button from the new main menu design.",
        )
        mDevice.waitForIdle()
    }

    fun verifyNoExtensionsButton() {
        Log.i(TAG, "verifyNoExtensionsButton: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.noExtensionsButton().assertExists()
        Log.i(TAG, "verifyNoExtensionsButton: Verified that the \"Extensions\" button exists.")
    }

    fun clickSaveButton() {
        Log.i(TAG, "clickSaveButton: Trying to click the \"Save\" button from the new main menu design.")
        composeTestRule.saveMenuButton().performClick()
        Log.i(TAG, "clickSaveButton: Clicked the \"Save\" button from the new main menu design.")
    }

    fun verifyBookmarkThisPageButton() {
        composeTestRule.bookmarkThisPageButton().assertIsDisplayed()
    }

    fun clickQuitFirefoxButton() {
        Log.i(TAG, "clickQuitFirefoxButton: Trying to click the \"Quit $appName\" button from the new main menu design.")
        composeTestRule.quitFirefoxButton().performClick()
        Log.i(TAG, "clickQuitFirefoxButton: Clicked the \"Quit $appName\" button from the new main menu design.")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun openSettings(
            interact: SettingsRobot.() -> Unit,
        ): SettingsRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Settings button from the new main menu design.",
            )
            composeTestRule.settingsButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Settings button from the new main menu design.")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun openHelp(
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Help button from the new main menu design.",
            )
            composeTestRule.helpButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Help button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickNewTabButton(
            interact: SearchRobot.() -> Unit,
        ): SearchRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the New  tab button from the new main menu design.",
            )
            composeTestRule.newTabButton().performClick()
            Log.i(TAG, "openSettings: Clicked the New tab button from the new main menu design.")

            SearchRobot().interact()
            return SearchRobot.Transition()
        }

        fun clickNewPrivateTabButton(
            interact: SearchRobot.() -> Unit,
        ): SearchRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the New private tab button from the new main menu design.",
            )
            composeTestRule.newPrivateTabButton().performClick()
            Log.i(
                TAG,
                "openSettings: Clicked the New private tab button from the new main menu design.",
            )

            SearchRobot().interact()
            return SearchRobot.Transition()
        }

        fun clickFindInPageButton(
            interact: FindInPageRobot.() -> Unit,
        ): FindInPageRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the FindInPage button from the new main menu design.",
            )
            composeTestRule.findInPageButton().performClick()
            Log.i(TAG, "openSettings: Clicked the FindInPage button from the new main menu design.")

            FindInPageRobot().interact()
            return FindInPageRobot.Transition()
        }

        fun openBookmarks(
            interact: BookmarksRobot.() -> Unit,
        ): BookmarksRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Bookmarks button from the new main menu design.",
            )
            composeTestRule.bookmarksButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Bookmarks button from the new main menu design.")

            BookmarksRobot().interact()
            return BookmarksRobot.Transition()
        }

        fun openHistory(
            interact: HistoryRobot.() -> Unit,
        ): HistoryRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the History button from the new main menu design.",
            )
            composeTestRule.historyButton().performClick()
            Log.i(TAG, "openSettings: Clicked the History button from the new main menu design.")

            HistoryRobot().interact()
            return HistoryRobot.Transition()
        }

        fun openDownloads(
            interact: DownloadRobot.() -> Unit,
        ): DownloadRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Download button from the new main menu design.",
            )
            composeTestRule.downloadsButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Download button from the new main menu design.")

            DownloadRobot().interact()
            return DownloadRobot.Transition()
        }

        fun openPasswords(
            interact: SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.() -> Unit,
        ): SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Download button from the new main menu design.",
            )
            composeTestRule.passwordsButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Download button from the new main menu design.")

            SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot().interact()
            return SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition()
        }

        @OptIn(ExperimentalTestApi::class)
        fun openExtensionsFromMainMenu(interact: SettingsSubMenuAddonsManagerRobot.() -> Unit): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "openExtensionsFromMainMenu: Trying to click the \"Extensions\" button")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(EXTENSIONS))
            composeTestRule.extensionsButton().performClick()
            Log.i(TAG, "openExtensionsFromMainMenu: Clicked the \"Extensions\" button")
            composeTestRule.waitForIdle()

            SettingsSubMenuAddonsManagerRobot().interact()
            return SettingsSubMenuAddonsManagerRobot.Transition()
        }

        fun clickBookmarkThisPageButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickBookmarkThisPageButton: Trying to click the \"Bookmark this page\" button from the new main menu design.")
            composeTestRule.bookmarkThisPageButton().performClick()
            Log.i(TAG, "clickBookmarkThisPageButton: Clicked the \"Bookmark this page\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickAddToShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickAddToShortcutsButton: Trying to click the \"Add to shortcuts\" button from the new main menu design.")
            composeTestRule.addToShortcutsButton().performClick()
            Log.i(TAG, "clickAddToShortcutsButton: Clicked the \"Add to shortcuts\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickRemoveFromShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickRemoveFromShortcutsButton: Trying to click the \"Remove from shortcuts\" button from the new main menu design.")
            composeTestRule.removeFromShortcutsButton().performClick()
            Log.i(TAG, "clickRemoveFromShortcutsButton: Clicked the \"Remove from shortcuts\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickEditBookmarkButton(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickEditBookmarkButton: Trying to click the \"Edit bookmark\" button from the new main menu design.")
            composeTestRule.editBookmarkButton().performClick()
            Log.i(TAG, "clickEditBookmarkButton: Clicked the \"Edit bookmark\" button from the new main menu design.")

            BookmarksRobot().interact()
            return BookmarksRobot.Transition()
        }

        fun clickAddToHomeScreenButton(interact: AddToHomeScreenRobot.() -> Unit): AddToHomeScreenRobot.Transition {
            Log.i(TAG, "clickAddToHomeScreenButton: Trying to click the \"Add to Home screen…\" button from the new main menu design.")
            composeTestRule.addToHomeScreenButton().performClick()
            Log.i(TAG, "clickAddToHomeScreenButton: Clicked the \"Add to Home screen…\" button from the new main menu design.")

            AddToHomeScreenRobot().interact()
            return AddToHomeScreenRobot.Transition()
        }

        fun clickSaveToCollectionButton(interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
            Log.i(TAG, "clickSaveToCollectionButton: Trying to click the \"Save to collection…\" button from the new main menu design.")
            composeTestRule.saveToCollectionButton().performClick()
            Log.i(TAG, "clickSaveToCollectionButton: Clicked the \"Save to collection…\" button from the new main menu design.")

            CollectionRobot().interact()
            return CollectionRobot.Transition()
        }

        fun clickSaveAsPDFButton(interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
            Log.i(TAG, "clickSaveAsPDFButton: Trying to click the \"Save as PDF…\" button from the new main menu design.")
            composeTestRule.saveAsPDFButton().performClick()
            Log.i(TAG, "clickSaveAsPDFButton: Clicked the \"Save as PDF…\" button from the new main menu design.")

            DownloadRobot().interact()
            return DownloadRobot.Transition()
        }

        fun clickTheReaderViewModeButton(
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickTheReaderViewModeButton: Trying to click the Reader View button from the new main menu design.",
            )
            composeTestRule.turnOnReaderViewButton().performClick()
            Log.i(
                TAG,
                "clickTheReaderViewModeButton: Clicked the Reader View button from the new main menu design.",
            )

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickCustomizeReaderViewButton(
            interact: ReaderViewRobot.() -> Unit,
        ): ReaderViewRobot.Transition {
            Log.i(
                TAG,
                "clickCustomizeReaderViewButton: Trying to click the Customize Reader View button from the new main menu design.",
            )
            composeTestRule.customizeReaderViewButton().performClick()
            Log.i(
                TAG,
                "clickCustomizeReaderViewButton: Clicked the Customize Reader View button from the new main menu design.",
            )

            ReaderViewRobot().interact()
            return ReaderViewRobot.Transition()
        }

        fun clickTurnOffReaderViewButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickTurnOffReaderViewButton: Trying to click the Turn off Reader View button from the new main menu design.",
            )
            composeTestRule.turnOffReaderViewButton().performClick()
            Log.i(
                TAG,
                "clickTurnOffReaderViewButton: Clicked the Turn off Reader View button from the new main menu design.",
            )
            BrowserRobot().interact()
            return BrowserRobot.Transition()
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

        fun clickTranslatedToButton(language: String, interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(
                TAG,
                "clickTranslateButton: Trying to click the Translate button from the new main menu design.",
            )
            composeTestRule.translatedToButton(language).assertIsDisplayed()
            composeTestRule.translatedToButton(language).performClick()
            Log.i(
                TAG,
                "clickTranslateButton: Clicked the Translate button from the new main menu design.",
            )
            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        fun clickShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(
                TAG,
                "clickShareButton: Trying to click the Share button from the new main menu design.",
            )
            composeTestRule.shareButton().performClick()
            Log.i(
                TAG,
                "clickShareButton: Clicked the Share button from the new main menu design.",
            )
            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickCustomizeHomepageButton(interact: SettingsSubMenuHomepageRobot.() -> Unit): SettingsSubMenuHomepageRobot.Transition {
            Log.i(TAG, "clickCustomizeHomepageButton: Trying to click the \"Customize homepage\" button")
            composeTestRule.customizeHomeButton().performClick()
            Log.i(TAG, "clickCustomizeHomepageButton: Clicked the \"Customize homepage\" button")

            SettingsSubMenuHomepageRobot().interact()
            return SettingsSubMenuHomepageRobot.Transition()
        }

        fun clickGoBackToMainMenuButton(interact: ThreeDotMenuMainRobotCompose.() -> Unit): Transition {
            Log.i(TAG, "clickGoBackToMainMenuButton: Trying to click the \"Navigate back\" to main menu button.")
            composeTestRule.backToMainMenuButton().performClick()
            Log.i(TAG, "clickGoBackToMainMenuButton: Clicked the \"Navigate back\" to main menu button.")

            ThreeDotMenuMainRobotCompose(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun clickOutsideTheMainMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOutsideTheMainMenu: Trying to click outside the main menu.")
            itemWithResId("$packageName:id/touch_outside").clickTopLeft()
            Log.i(TAG, "clickOutsideTheMainMenu: Clicked click outside the main menu.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickReportBrokenSiteButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openReportSiteIssue: Trying to click the \"Report Site Issue\" button")
            composeTestRule.reportBrokenSiteButton().performClick()
            Log.i(TAG, "openReportSiteIssue: Clicked the \"Report Site Issue\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun openReportBrokenSite(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openReportBrokenSite: Trying to click the \"Report broken site\" button")
            composeTestRule.reportBrokenSiteButton().performClick()
            Log.i(TAG, "openReportBrokenSite: Clicked the \"Report broken site\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }
    }
}

fun mainMenuScreen(composeTestRule: ComposeTestRule, interact: ThreeDotMenuMainRobotCompose.() -> Unit): ThreeDotMenuMainRobotCompose.Transition {
    ThreeDotMenuMainRobotCompose(composeTestRule).interact()
    return ThreeDotMenuMainRobotCompose.Transition(composeTestRule)
}

private fun ComposeTestRule.mainMenuCFRTitle() = onNodeWithText(getStringResource(R.string.menu_cfr_title))

private fun ComposeTestRule.mainMenuCFRMessage() = onNodeWithText(getStringResource(R.string.menu_cfr_body))

private fun ComposeTestRule.closeMainMenuCFRButton() = onNodeWithTag("cfr.dismiss")

private fun ComposeTestRule.signInButton() = onNodeWithText(getStringResource(R.string.browser_menu_sign_in))

private fun ComposeTestRule.signInButtonDescription() = onNodeWithText(getStringResource(R.string.browser_menu_sign_in_caption))

private fun ComposeTestRule.customizeHomeButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_customize_home_1))

private fun ComposeTestRule.newPrivateTabButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_new_private_tab))

private fun ComposeTestRule.newTabButton() = onNodeWithContentDescription(getStringResource(R.string.library_new_tab))

private fun ComposeTestRule.helpButton() = onNodeWithContentDescription("Help")

private fun ComposeTestRule.settingsButton() = onNodeWithContentDescription("Settings")

private fun ComposeTestRule.extensionsButton() = onNodeWithTag(EXTENSIONS)

private fun ComposeTestRule.noExtensionsButton() = onNodeWithContentDescription("ExtensionsNo extensions enabled")

private fun ComposeTestRule.bookmarksButton() = onNodeWithContentDescription(getStringResource(R.string.library_bookmarks))

private fun ComposeTestRule.historyButton() = onNodeWithContentDescription(getStringResource(R.string.library_history))

private fun ComposeTestRule.downloadsButton() = onNodeWithContentDescription(getStringResource(R.string.library_downloads))

private fun ComposeTestRule.passwordsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_passwords))

private fun ComposeTestRule.whatsNewButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_new_in_firefox))

private fun ComposeTestRule.backToMainMenuButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_back_button_content_description))

private fun ComposeTestRule.quitFirefoxButton() = onNodeWithContentDescription("Quit $appName")

// Page main menu items

private fun ComposeTestRule.findInPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_find_in_page_2))

private fun ComposeTestRule.toolsMenuButton() = onNodeWithTag("mainMenu.tools")

private fun ComposeTestRule.saveMenuButton() = onNodeWithTag("mainMenu.save")

private fun ComposeTestRule.desktopSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_switch_to_desktop_site))

private fun ComposeTestRule.mobileSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_switch_to_mobile_site))

// Save sub menu items

private fun ComposeTestRule.saveSubMenuTitle() = onNodeWithText(getStringResource(R.string.browser_menu_save))

private fun ComposeTestRule.bookmarkThisPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_bookmark_this_page))

private fun ComposeTestRule.editBookmarkButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_edit_bookmark))

private fun ComposeTestRule.addToShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_shortcuts))

private fun ComposeTestRule.removeFromShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_remove_from_shortcuts))

private fun ComposeTestRule.addToHomeScreenButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_homescreen_2))

private fun ComposeTestRule.saveToCollectionButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_to_collection))

private fun ComposeTestRule.saveAsPDFButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_as_pdf))

// Tools menu items

private fun ComposeTestRule.toolsMenuHeader() = onNodeWithText("Tools")

private fun ComposeTestRule.translatePageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translate_page))

private fun ComposeTestRule.translatedToButton(language: String) = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translated_to, language))

private fun ComposeTestRule.reportBrokenSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_webcompat_reporter))

private fun ComposeTestRule.printContentButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_print))

private fun ComposeTestRule.turnOnReaderViewButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_turn_on_reader_view))

private fun ComposeTestRule.turnOffReaderViewButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_turn_off_reader_view))

private fun ComposeTestRule.customizeReaderViewButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_customize_reader_view_2))

private fun ComposeTestRule.shareButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_share_2))

private fun ComposeTestRule.defaultOpenInAppButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_app_link))

private fun ComposeTestRule.openInAppNameButton(appName: String) = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_in_fenix, appName))
