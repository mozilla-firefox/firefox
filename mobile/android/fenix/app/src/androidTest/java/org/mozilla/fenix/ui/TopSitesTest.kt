/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.Constants.defaultTopSitesList
import org.mozilla.fenix.helpers.DataGenerationHelper.generateRandomString
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.TestSetup
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

class TopSitesTest : TestSetup() {
    @get:Rule
    val activityIntentTestRule = AndroidComposeTestRule(
        HomeActivityIntentTestRule.withDefaultSettingsOverrides(skipOnboarding = true),
    ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532598
    @SmokeTest
    @Test
    fun addAWebsiteAsATopSiteTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        homeScreen {
            verifyExistingTopSitesList()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
            verifyAddToShortcutsButton(shouldExist = true)
        }.addToFirefoxHome {
            verifySnackBarText(getStringResource(R.string.snackbar_added_to_shortcuts))
        }.goToHomescreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532599
    @Test
    fun openTopSiteInANewTabTest() {
        val webPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openTopSiteTabWithTitle(title = webPage.title) {
            verifyUrl(webPage.url.toString().replace("http://", ""))
        }.goToHomescreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
            // Dismiss context menu popup
            mDevice.pressBack()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532600
    @Test
    fun openTopSiteInANewPrivateTabTest() {
        val webPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
        }.openTopSiteInPrivateTab {
            verifyCurrentPrivateSession(activityIntentTestRule.activity.applicationContext)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1110321
    @Test
    fun editTopSiteTest() {
        val webPage = getGenericAsset(mockWebServer, 1)
        val newWebPageURL = getGenericAsset(mockWebServer, 2)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
        }.editTopSite(newPageTitle, newWebPageURL.url.toString()) {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(newPageTitle)
        }.openTopSiteTabWithTitle(title = newPageTitle) {
            verifyUrl(newWebPageURL.url.toString())
        }
    }

    @Test
    fun editTopSiteTestWithInvalidURL() {
        val webPage = getGenericAsset(mockWebServer, 1)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
        }.editTopSite(newPageTitle, "gl") {
            verifyTopSiteContextMenuUrlErrorMessage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532601
    @Test
    fun removeTopSiteUsingMenuButtonTest() {
        val webPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
        }.removeTopSite {
            clickSnackbarButton(activityIntentTestRule, "UNDO")
            verifyExistingTopSitesTabs(webPage.title)
        }.openContextMenuOnTopSitesWithTitle(webPage.title) {
            verifyTopSiteContextMenuItems()
        }.removeTopSite {
            verifyNotExistingTopSitesList(webPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2323641
    @Test
    fun removeTopSiteFromMainMenuTest() {
        val webPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(webPage.title, webPage.url.toString()),
            activityTestRule = activityIntentTestRule.activityRule,
        )

        homeScreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(webPage.title)
        }.openTopSiteTabWithTitle(webPage.title) {
        }.openThreeDotMenu {
            verifyRemoveFromShortcutsButton()
        }.clickRemoveFromShortcuts {
        }.goToHomescreen {
            verifyNotExistingTopSitesList(webPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/561582
    // Expected for en-us defaults
    @Test
    fun verifyENLocalesDefaultTopSitesListTest() {
        homeScreen {
            verifyExistingTopSitesList()
            defaultTopSitesList.values.forEach { value ->
                verifyExistingTopSitesTabs(value)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1050642
    @SmokeTest
    @Test
    fun addAndRemoveMostViewedTopSiteTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        for (i in 0..1) {
            navigationToolbar {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
                waitForPageToLoad()
            }
        }

        browserScreen {
        }.goToHomescreen {
            verifyExistingTopSitesList()
            verifyExistingTopSitesTabs(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
        }.deleteTopSiteFromHistory {
            verifySnackBarText(getStringResource(R.string.snackbar_top_site_removed))
            waitUntilSnackbarGone()
        }.openThreeDotMenu {
        }.openHistory {
            verifyEmptyHistoryView()
        }
    }
}
