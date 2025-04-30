/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
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
import org.mozilla.fenix.ui.robots.homeScreenWithComposeTopSites
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests Top Sites functionality
 *
 * - Verifies 'Add to Firefox Home' UI functionality
 * - Verifies 'Top Sites' context menu UI functionality
 * - Verifies 'Top Site' usage UI functionality
 * - Verifies existence of default top sites available on the home-screen
 */

class TopSitesTestCompose : TestSetup() {
    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(
                composeTopSitesEnabled = true,
            ),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532598
    @SmokeTest
    @Test
    fun addAWebsiteAsATopSiteTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
            verifyAddToShortcutsButton(true)
        }.addToFirefoxHome {
            verifySnackBarText(getStringResource(R.string.snackbar_added_to_shortcuts))
        }.goToHomescreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532599
    @Test
    fun openTopSiteInANewTabTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openTopSiteTabWithTitle(title = defaultWebPage.title) {
            verifyUrl(defaultWebPage.url.toString().replace("http://", ""))
        }.goToHomescreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }

        // Dismiss context menu popup
        mDevice.pressBack()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532600
    @Test
    fun openTopSiteInANewPrivateTabTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }.openTopSiteInPrivate {
            verifyCurrentPrivateSession(composeTestRule.activity.applicationContext)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1110321
    @Test
    fun editTopSiteTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)
        val genericWebPage = getGenericAsset(mockWebServer, 2)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }.editTopSite(newPageTitle, genericWebPage.url.toString()) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(newPageTitle)
        }.openTopSiteTabWithTitle(newPageTitle) {
            verifyUrl(genericWebPage.url.toString())
        }
    }

    @Test
    fun editTopSiteTestWithInvalidURL() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)
        val newPageTitle = generateRandomString(5)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }.editTopSite(newPageTitle, "gl") {
            verifyTopSiteContextMenuUrlErrorMessage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/532601
    @Test
    fun removeTopSiteUsingMenuButtonTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }.removeTopSite {
            clickSnackbarButton(composeTestRule, "UNDO")
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
            verifyTopSiteContextMenuItems()
        }.removeTopSite {
            verifyNotExistingTopSiteItem(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2323641
    @Test
    fun removeTopSiteFromMainMenuTest() {
        val defaultWebPage = getGenericAsset(mockWebServer, 1)

        MockBrowserDataHelper.addPinnedSite(
            Pair(defaultWebPage.title, defaultWebPage.url.toString()),
            activityTestRule = composeTestRule.activityRule,
        )

        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openTopSiteTabWithTitle(defaultWebPage.title) {
        }.openThreeDotMenu {
            verifyRemoveFromShortcutsButton()
        }.clickRemoveFromShortcuts {
        }.goToHomescreenWithComposeTopSites(composeTestRule) {
            verifyNotExistingTopSiteItem(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/561582
    // Expected for en-us defaults
    @Test
    fun verifyENLocalesDefaultTopSitesListTest() {
        homeScreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            val topSitesTitles = arrayListOf("Google", "Top Articles", "Wikipedia")
            topSitesTitles.forEach { value ->
                verifyExistingTopSiteItem(value)
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
        }.goToHomescreenWithComposeTopSites(composeTestRule) {
            verifyExistingTopSitesList()
            verifyExistingTopSiteItem(defaultWebPage.title)
        }.openContextMenuOnTopSitesWithTitle(defaultWebPage.title) {
        }.removeTopSite {
            verifySnackBarText(getStringResource(R.string.snackbar_top_site_removed))
            waitUntilSnackbarGone()
        }
        homeScreen {
        }.openThreeDotMenu {
        }.openHistory {
            verifyEmptyHistoryView()
        }
    }
}
