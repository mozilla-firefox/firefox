/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.util.Log
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.isDefaultPinnedShortcutsOnHomepage
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.Constants.defaultPinnedShortcutTitles
import org.mozilla.fenix.helpers.Constants.sponsoredShortcutTitles
import org.mozilla.fenix.helpers.DataGenerationHelper.generateRandomString
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests Top Sites functionality
 *
 * - Verifies 'Add to Firefox Home' UI functionality
 * - Verifies 'Top Sites' context menu UI functionality
 * - Verifies 'Top Site' usage UI functionality
 * - Verifies existence of default top sites available on the home-screen
 */
class TopSitesTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    @Before
    fun setUp() {
        // Workaround to make sure the Top sites list displayed before starting the tests.
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "setUp: Started try #$i")
            try {
                homeScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .goBack(composeTestRule) { verifyExistingTopSitesList() }

                break
            } catch (e: Throwable) {
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    waitForAppWindowToBeUpdated()
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532598
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.ShortcutsTest#addAWebsiteAsATopSiteTest"],
        bug = 2048243,
        since = "2026-06",
    )
    @SmokeTest
    @Test
    fun addAWebsiteAsATopSiteTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
            verifyExistingTopSitesList()
        }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(defaultWebPage.url) {
                verifyPageContent(defaultWebPage.content)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
                verifyAddToShortcutsButton(isDisplayed = true)
            }
            .clickAddToShortcutsButton {
                verifySnackBarText(getStringResource(R.string.snackbar_added_to_shortcuts))
            }
            .goToHomescreen {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(defaultWebPage.title)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532599
    @Test
    fun openTopSiteInANewTabTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openTopSiteTabWithTitle(title = webPage.title) {
                verifyUrl(webPage.url.toString().replace("http://", ""))
            }
            .goToHomescreen {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(webPage.title) {
                verifyTopSiteContextMenuItems()
                // Dismiss context menu popup
                mDevice.pressBack()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532600
    @Test
    fun openTopSiteInANewPrivateTabTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(webPage.title) {
                verifyTopSiteContextMenuItems()
            }
            .openTopSiteInPrivateTab {
                verifyCurrentPrivateSession(composeTestRule.activity.applicationContext)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1110321
    @Test
    fun editTopSiteTest() {
        val webPage = mockWebServer.getGenericAsset(1)
        val newWebPageURL = mockWebServer.getGenericAsset(2)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(webPage.title) {
                verifyTopSiteContextMenuItems()
            }
            .editTopSite(newPageTitle, newWebPageURL.url.toString()) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(newPageTitle)
            }
            .openTopSiteTabWithTitle(title = newPageTitle) {
                verifyUrl(newWebPageURL.url.toString())
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2673886
    @Test
    fun editTopSiteTestWithInvalidURL() {
        val webPage = mockWebServer.getGenericAsset(1)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(webPage.title) {
                verifyTopSiteContextMenuItems()
            }
            .editTopSite(newPageTitle, "gl") {
                verifyTopSiteContextMenuUrlErrorMessage()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532601
    @Test
    fun removeTopSiteUsingMenuButtonTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(webPage.title) {
                verifyTopSiteContextMenuItems()
            }
            .removeTopSite {
                verifyNotExistingTopSiteItem(webPage.title)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2323641
    @Test
    fun removeTopSiteFromMainMenuTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(webPage.title)
            }
            .openTopSiteTabWithTitle(webPage.title) {}
            .openThreeDotMenu {
                clickTheMoreButton()
                verifyRemoveFromShortcutsButton()
            }
            .clickRemoveFromShortcutsButton {}
            .goToHomescreen {
                verifyNotExistingTopSiteItem(webPage.title)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/561582
    // Expected for en-us defaults
    @Test
    fun verifyENLocalesDefaultTopSitesListTest() {
        homeScreen(composeTestRule) {
            verifyExistingTopSitesList()
            sponsoredShortcutTitles.forEach { title -> verifyExistingTopSitesTabs(title) }
            if (isDefaultPinnedShortcutsOnHomepage()) {
                defaultPinnedShortcutTitles.forEach { title -> verifyExistingTopSitesTabs(title) }
            }
            verifyAddShortcutButtonExists()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1050642
    @SmokeTest
    @Test
    fun addAndRemoveMostViewedTopSiteTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        for (i in 0..1) {
            navigationToolbar(composeTestRule) {}
                .enterURLAndEnterToBrowser(defaultWebPage.url) {
                    waitForPageToLoad()
                }
        }

        browserScreen(composeTestRule) {}
            .goToHomescreen {
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(defaultWebPage.title)
            }
            .openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {}
            .removeTopSite {}
            .openThreeDotMenu {}
            .clickHistoryButton {
                verifyEmptyHistoryView()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4227423
    @Test
    fun addAShortcutFromHomepageTest() {
        val websiteData =
            object {
                val title = "Mozilla"
                val goodUrl = "https://www.mozilla.org/en-US/"
                val badUrl = "incorrectURL"
                val popularSite = "Facebook"
            }

        homeScreen(composeTestRule) {}
            .clickAddShortcutButton {
                verifyAddToHomepageBottomSheet()
                clickOnPopularWebsite(websiteData.popularSite)
                verifySnackBarText(getStringResource(R.string.snackbar_added_to_shortcuts))
                waitUntilSnackbarGone()
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(websiteData.popularSite)
            }

        homeScreen(composeTestRule) {}
            .clickAddShortcutButton {}
            .clickAddWebsiteButton {
                verifyEnterAWebsiteUrlDialog()
                enterWebsiteUrl(websiteData.goodUrl)
                enterShortcutName(websiteData.title)
                clickCancelInAddWebsiteDialog()
            }

        homeScreen(composeTestRule) {}
            .clickAddShortcutButton {}
            .clickAddWebsiteButton {
                verifyEnterAWebsiteUrlDialog()
                enterWebsiteUrl(websiteData.badUrl)
                enterShortcutName(websiteData.title)
                clickSaveInAddWebsiteDialog()
                verifyInvalidUrlError()
                enterWebsiteUrl(websiteData.goodUrl)
                enterShortcutName(websiteData.title)
                clickSaveInAddWebsiteDialog()
                verifyExistingTopSitesList()
                verifyExistingTopSitesTabs(websiteData.title)
            }
    }
}
