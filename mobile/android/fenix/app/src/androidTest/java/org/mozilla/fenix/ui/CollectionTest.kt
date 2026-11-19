/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.collectionRobot
import org.mozilla.fenix.ui.robots.composeTabDrawer
import org.mozilla.fenix.ui.robots.homeScreen

/** Tests for verifying basic functionality of tab collections */
class CollectionTest {
    private val collectionName = "First Collection"

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                isRecentTabsFeatureEnabled = false,
                isRecentlyVisitedFeatureEnabled = false,
                isPocketEnabled = false,
                isWallpaperOnboardingEnabled = false,
                // workaround for toolbar at top position by default
                // remove with https://bugzilla.mozilla.org/show_bug.cgi?id=1917640
                shouldUseBottomToolbar = true,
            )
        ) {
            it.activity
        }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/343422
    @SmokeTest
    @Test
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.CollectionsTest#verifyExpandedCollectionItemsTest"],
        bug = 2054023,
        since = "2026-07",
    )
    fun verifyExpandedCollectionItemsTest() {
        val webPage = mockWebServer.getGenericAsset(1)
        val webPage2 = mockWebServer.getGenericAsset(2)
        val webPageUrl = webPage.url.host.toString()

        MockBrowserDataHelper.createCollection(
            Pair(webPage.url.toString(), webPage.title),
            Pair(webPage2.url.toString(), webPage2.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(webPage.title)
                verifyTabSavedInCollection(webPage2.title)
                verifyShareCollectionButtonIsVisible(true)
                verifyCollectionMenuIsVisible(true)
                verifyCollectionItemRemoveButtonIsVisible(webPage.title, true)
            }
            .collapseCollection(collectionName) {}

        collectionRobot(composeTestRule) {
            verifyTabSavedInCollection(webPage.title, false)
            verifyShareCollectionButtonIsVisible(false)
            verifyCollectionMenuIsVisible(false)
            verifyCollectionTabUrl(false, webPageUrl)
            verifyCollectionItemRemoveButtonIsVisible(webPage.title, false)
        }

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(webPage.title)
                verifyCollectionTabUrl(true, webPageUrl)
                verifyShareCollectionButtonIsVisible(true)
                verifyCollectionMenuIsVisible(true)
                verifyCollectionItemRemoveButtonIsVisible(webPage.title, true)
            }
            .collapseCollection(collectionName) {}

        collectionRobot(composeTestRule) {
            verifyTabSavedInCollection(webPage.title, false)
            verifyShareCollectionButtonIsVisible(false)
            verifyCollectionMenuIsVisible(false)
            verifyCollectionTabUrl(false, webPageUrl)
            verifyCollectionItemRemoveButtonIsVisible(webPage.title, false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/343425
    @SmokeTest
    @Test
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.CollectionsTest#openAllTabsFromACollectionTest"],
        bug = 2054023,
        since = "2026-07",
    )
    fun openAllTabsFromACollectionTest() {
        val firstTestPage = mockWebServer.getGenericAsset(1)
        val secondTestPage = mockWebServer.getGenericAsset(2)

        MockBrowserDataHelper.createCollection(
            Pair(firstTestPage.url.toString(), firstTestPage.title),
            Pair(secondTestPage.url.toString(), secondTestPage.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                clickCollectionThreeDotButton()
                selectOpenTabs()
            }
        composeTabDrawer(composeTestRule) {
            verifyExistingOpenTabs(firstTestPage.title, secondTestPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/343426
    @SmokeTest
    @Test
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.CollectionsTest#shareAllTabsFromACollectionTest"],
        bug = 2054023,
        since = "2026-07",
    )
    fun shareAllTabsFromACollectionTest() {
        val firstWebsite = mockWebServer.getGenericAsset(1)
        val secondWebsite = mockWebServer.getGenericAsset(2)
        val sharingApp = "Gmail"
        val urlString = "${secondWebsite.url}\n\n${firstWebsite.url}"

        MockBrowserDataHelper.createCollection(
            Pair(firstWebsite.url.toString(), firstWebsite.title),
            Pair(secondWebsite.url.toString(), secondWebsite.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {}
            .clickShareCollectionButton {
                verifyShareTabsOverlay(firstWebsite.title, secondWebsite.title)
                verifySharingWithSelectedApp(sharingApp, urlString, collectionName)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/343428
    // Test running on beta/release builds in CI:
    // caution when making changes to it, so they don't block the builds
    @SmokeTest
    @Test
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.CollectionsTest#deleteCollectionTest"],
        bug = 2054023,
        since = "2026-07",
    )
    fun deleteCollectionTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.createCollection(
            Pair(webPage.url.toString(), webPage.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                clickCollectionThreeDotButton()
                selectDeleteCollection()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2319455
    @Test
    fun removeTabFromCollectionUsingTheCloseButtonTest() {
        val webPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.createCollection(
            Pair(webPage.url.toString(), webPage.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(webPage.title, true)
                removeTabFromCollection(webPage.title)
            }
        homeScreen(composeTestRule) {
            verifyCollectionIsDisplayed(collectionName, false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/343427
    @Test
    fun removeTabFromCollectionUsingSwipeLeftActionTest() {
        val testPage1 = mockWebServer.getGenericAsset(1)
        val testPage2 = mockWebServer.getGenericAsset(2)

        MockBrowserDataHelper.createCollection(
            Pair(testPage1.url.toString(), testPage1.title),
            Pair(testPage2.url.toString(), testPage2.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(testPage1.title, true)
                verifyTabSavedInCollection(testPage2.title, true)
                swipeTabLeft(testPage2.title)
                verifyTabSavedInCollection(testPage2.title, false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/991278
    @Test
    fun removeTabFromCollectionUsingSwipeRightActionTest() {
        val testPage1 = mockWebServer.getGenericAsset(1)
        val testPage2 = mockWebServer.getGenericAsset(2)

        MockBrowserDataHelper.createCollection(
            Pair(testPage1.url.toString(), testPage1.title),
            Pair(testPage2.url.toString(), testPage2.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(testPage1.title, true)
                verifyTabSavedInCollection(testPage2.title, true)
                swipeTabRight(testPage2.title)
                verifyTabSavedInCollection(testPage2.title, false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080080
    @Test
    fun removeLastTabFromCollectionUsingSwipeActionTest() {
        val testPage = mockWebServer.getGenericAsset(1)

        MockBrowserDataHelper.createCollection(
            Pair(testPage.url.toString(), testPage.title),
            title = collectionName,
        )

        homeScreen(composeTestRule) {
                verifyCollectionIsDisplayed(collectionName)
            }
            .expandCollection(collectionName) {
                verifyTabSavedInCollection(testPage.title, true)
                swipeTabLeft(testPage.title)
                verifyTabSavedInCollection(testPage.title, false)
            }
        homeScreen(composeTestRule) {
            verifyCollectionIsDisplayed(collectionName, false)
        }
    }
}
