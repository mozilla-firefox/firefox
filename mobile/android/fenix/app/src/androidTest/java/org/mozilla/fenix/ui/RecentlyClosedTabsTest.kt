/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.test.espresso.Espresso.openActionBarOverflowOrOptionsMenu
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.registerAndCleanupIdlingResources
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RecyclerViewIdlingResource
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.longTapSelectItem
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying basic functionality of recently closed tabs history
 *
 */
class RecentlyClosedTabsTest : TestSetup() {
    @get:Rule
    val activityTestRule = AndroidComposeTestRule(
        HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
    ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1065414
    // Verifies that a recently closed item is properly opened
    @SmokeTest
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1956220"])
    fun openRecentlyClosedItemTest() {
        val website = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(website.url) {
            mDevice.waitForIdle()
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        homeScreen {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.recently_closed_list), 1),
            ) {
                verifyRecentlyClosedTabsMenuView()
                verifyRecentlyClosedTabsPageTitle("Test_Page_1")
                verifyRecentlyClosedTabsUrl(website.url)
            }
        }.clickRecentlyClosedItem("Test_Page_1") {
            verifyUrl(website.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2195812
    // Verifies that tapping the "x" button removes a recently closed item from the list
    @SmokeTest
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1956220"])
    fun deleteRecentlyClosedTabsItemTest() {
        val website = getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(website.url) {
            mDevice.waitForIdle()
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        homeScreen {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
            registerAndCleanupIdlingResources(
                RecyclerViewIdlingResource(activityTestRule.activity.findViewById(R.id.recently_closed_list), 1),
            ) {
                verifyRecentlyClosedTabsMenuView()
            }
            clickDeleteRecentlyClosedTabs()
            verifyEmptyRecentlyClosedTabsList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1605515
    @Test
    fun openMultipleRecentlyClosedTabsTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondPage.url.toString()) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
            longTapSelectItem(firstPage.url)
            longTapSelectItem(secondPage.url)
            openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
        }.clickOpenInNewTab(activityTestRule) {
            // URL verification to be removed once https://bugzilla.mozilla.org/show_bug.cgi?id=1839179 is fixed.
            browserScreen {
                verifyPageContent(secondPage.content)
                verifyUrl(secondPage.url.toString())
            }.openTabDrawer(activityTestRule) {
                verifyNormalBrowsingButtonIsSelected(true)
                verifyExistingOpenTabs(firstPage.title, secondPage.title)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2198690
    @Test
    fun openRecentlyClosedTabsInPrivateBrowsingTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondPage.url.toString()) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
            longTapSelectItem(firstPage.url)
            longTapSelectItem(secondPage.url)
            openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
        }.clickOpenInPrivateTab(activityTestRule) {
            // URL verification to be removed once https://bugzilla.mozilla.org/show_bug.cgi?id=1839179 is fixed.
            browserScreen {
                verifyPageContent(secondPage.content)
                verifyUrl(secondPage.url.toString())
            }.openTabDrawer(activityTestRule) {
                verifyPrivateBrowsingButtonIsSelected(true)
                verifyExistingOpenTabs(firstPage.title, secondPage.title)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1605514
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1956220"])
    fun shareMultipleRecentlyClosedTabsTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = getGenericAsset(mockWebServer, 2)
        val sharingApp = "Gmail"
        val urlString = "${firstPage.url}\n\n${secondPage.url}"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondPage.url.toString()) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
            longTapSelectItem(firstPage.url)
            longTapSelectItem(secondPage.url)
        }.clickShare {
            verifyShareTabsOverlay(firstPage.title, secondPage.title)
            verifySharingWithSelectedApp(sharingApp, urlString, "${firstPage.title}, ${secondPage.title}")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1065438
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1956220"])
    fun closedPrivateTabsAreNotSavedInRecentlyClosedTabsTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = getGenericAsset(mockWebServer, 2)

        homeScreen {}.togglePrivateBrowsingMode()
        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondPage.url.toString()) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            verifyEmptyRecentlyClosedTabsList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1065439
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1956220"])
    fun deletingBrowserHistoryClearsRecentlyClosedTabsListTest() {
        val firstPage = getGenericAsset(mockWebServer, 1)
        val secondPage = getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openNewTab {
        }.submitQuery(secondPage.url.toString()) {
            waitForPageToLoad()
        }.openTabDrawer(activityTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openThreeDotMenu {
        }.openHistory {
        }.openRecentlyClosedTabs {
            waitForListToExist()
        }.goBackToHistoryMenu {
            clickDeleteAllHistoryButton()
            selectEverythingOption()
            confirmDeleteAllHistory()
            verifyEmptyHistoryView()
        }.openRecentlyClosedTabs {
            verifyEmptyRecentlyClosedTabsList()
        }
    }
}
