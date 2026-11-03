/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.isDefaultPinnedShortcutsOnHomepage
import org.mozilla.fenix.helpers.AppAndSystemHelper.openAppFromExternalLink
import org.mozilla.fenix.helpers.Constants.defaultPinnedShortcutTitles
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/** Tests for verifying the Homepage settings menu */
class SettingsHomepageTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1) val retryTestRule = RetryTestRule(3)

    @get:Rule(order = 2)
    val retryableComposeTestRule = RetryableComposeTestRule {
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }
    }

    private val composeTestRule
        get() = retryableComposeTestRule.current

    @get:Rule(order = 3) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1564843
    @Test
    fun verifyHomepageSettingsTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                verifyHomePageView()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1564859
    @Test
    fun verifyShortcutOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
                if (isDefaultPinnedShortcutsOnHomepage()) {
                    defaultPinnedShortcutTitles.forEach { item ->
                        verifyExistingTopSitesTabs(item)
                    }
                }
            }
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                clickShortcutsButton()
            }
            .goBack {}
            .goBack(composeTestRule) {
                defaultPinnedShortcutTitles.forEach { item ->
                    verifyNotExistingTopSiteItem(item)
                }
            }
        // Disabling the "Shortcuts" homepage setting option should remove the "Add to shortcuts" from main menu option
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .openThreeDotMenu {
                clickTheMoreButton()
                verifyAddToShortcutsButton(isDisplayed = false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1565003
    @Test
    fun verifyRecentlyVisitedOptionTest() {
        composeTestRule.activityRule.applySettingsExceptions {
            it.isRecentTabsFeatureEnabled = false
        }
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .goToHomescreen {
                verifyRecentlyVisitedSectionIsDisplayed(true)
            }
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                clickRecentlyVisited()
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyRecentlyVisitedSectionIsDisplayed(false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1564999
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsHomepageTest#continueOptionTest"],
        bug = 2042363,
        since = "2026-05",
    )
    @SmokeTest
    @Test
    fun continueOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .goToHomescreen {
                verifyContinueSectionIsDisplayed()
            }
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                clickContinueButton()
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyContinueSectionIsNotDisplayed()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1565000
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsHomepageTest#recentBookmarksOptionTest"],
        bug = 2042363,
        since = "2026-05",
    )
    @SmokeTest
    @Test
    fun recentBookmarksOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .openThreeDotMenu {}
            .clickBookmarkThisPageButton {}
            .goToHomescreen {
                verifyBookmarksSectionIsDisplayed(exists = true)
            }
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                clickRecentBookmarksButton()
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyBookmarksSectionIsDisplayed(exists = false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1569831
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsHomepageTest#verifyOpeningScreenOptionsTest"],
        bug = 2058818,
        since = "2026-07",
    )
    @SmokeTest
    @Test
    fun verifyOpeningScreenOptionsTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifySettingsOptionSummary("Homepage", "Open on homepage after four hours")
            }
            .openHomepageSubMenu {
                verifySelectedOpeningScreenOption("Homepage after four hours of inactivity")
                clickOpeningScreenOption("Homepage")
                verifySelectedOpeningScreenOption("Homepage")
            }

        restartApp(composeTestRule.activityRule)

        homeScreen(composeTestRule) {
                verifyHomeScreen()
            }
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifySettingsOptionSummary("Homepage", "Open on homepage")
            }
            .openHomepageSubMenu {
                clickOpeningScreenOption("Last tab")
                verifySelectedOpeningScreenOption("Last tab")
            }
            .goBack {
                verifySettingsOptionSummary("Homepage", "Open on last tab")
            }

        restartApp(composeTestRule.activityRule)

        browserScreen(composeTestRule) {
            verifyUrl(genericURL.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1569843
    @Test
    fun verifyOpeningScreenAfterLaunchingExternalLinkTest() {
        val genericPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                clickOpeningScreenOption("Homepage")
            }
            .goBackToHomeScreen(composeTestRule) {}

        composeTestRule.activityRule.applySettingsExceptions {
            it.isTermsOfServiceAccepted = true

            with(composeTestRule.activityRule) {
                finishActivity()
                mDevice.waitForIdle()
                openAppFromExternalLink(composeTestRule, genericPage.url.toString())
            }
        }

        browserScreen(composeTestRule) {
            verifyPageContent(genericPage.content)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1676355
    @Test
    fun verifyTheWallpapersMenuUI() {
        homeScreen(retryableComposeTestRule.current) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {}
            .clickWallpapersMenuOption {
                verifyTheWallpapersSettingsPageHeader()
                verifyClassicFirefoxSection(retryableComposeTestRule.current)
                verifyEdgeToEdgeWallpaperIsDisplayed(retryableComposeTestRule.current)
                verifyDefaultWallpaperIsDisplayed(retryableComposeTestRule.current)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1676359
    @Test
    fun verifyWallpaperChangeFunctionalityTest() {
        homeScreen(retryableComposeTestRule.current) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {}
            .clickWallpapersMenuOption {
                verifyEdgeToEdgeWallpaperIsSelected(retryableComposeTestRule.current)
                verifyDefaultWallpaperIsNotSelected(retryableComposeTestRule.current)
                clickTheDefaultWallpaper(retryableComposeTestRule.current)
                verifyDefaultWallpaperIsSelected(retryableComposeTestRule.current)
                verifyEdgeToEdgeWallpaperIsNotSelected(retryableComposeTestRule.current)
            }
            .goBack {}
            .goBack(retryableComposeTestRule.current) {
                exitMenu()
                verifyDefaultWallpaperApplied(retryableComposeTestRule.current)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3375726
    @Test
    fun verifyEdgeToEdgeWallpaperBackgroundAdaptation() {
        homeScreen(retryableComposeTestRule.current) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {}
            .clickWallpapersMenuOption {
                verifyEdgeToEdgeWallpaperIsSelected(retryableComposeTestRule.current)
            }
            .goBack {}
            .goBack(retryableComposeTestRule.current) {
                exitMenu()
                verifyEdgeToEdgeWallpaperApplied(retryableComposeTestRule.current)
            }

        homeScreen(retryableComposeTestRule.current) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openCustomizeSubMenu {
                selectDarkMode()
            }
            .goBack {}
            .goBack(retryableComposeTestRule.current) {
                verifyEdgeToEdgeWallpaperApplied(retryableComposeTestRule.current)
            }
            .togglePrivateBrowsingMode()

        homeScreen(retryableComposeTestRule.current) {
            verifyPrivateModeBackgroundApplied(retryableComposeTestRule.current)
        }
    }
}
