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
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests for verifying the presence of home screen and first-run homescreen elements
 *
 * Note: For private browsing, navigation bar and tabs see separate test class
 */
class HomeScreenTest {
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/235396
    @Test
    fun homeScreenItemsTest() {
        homeScreen(composeTestRule) {
            verifyHomeWordmark()
            verifyHomePrivateBrowsingButton()
            if (isDefaultPinnedShortcutsOnHomepage()) {
                verifyExistingTopSitesTabs("Wikipedia")
                verifyExistingTopSitesTabs("Google")
            }
            verifyNavigationToolbar()
            verifyHomeMenuButton()
            verifyTabCounter("0")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/244199
    @Test
    fun privateBrowsingHomeScreenItemsTest() {
        homeScreen(composeTestRule) {}.togglePrivateBrowsingMode()

        homeScreen(composeTestRule) {
                verifyPrivateBrowsingHomeScreenItems()
            }
            .openPrivateBrowsingModeLearnMoreLink {
                verifyUrl("common-myths-about-private-browsing")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1364362
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.HomeTest#verifyContinueSectionTest"],
        bug = 2039207,
        since = "2026-05",
    )
    @SmokeTest
    @Test
    fun verifyContinueSectionTest() {
        composeTestRule.activityRule.applySettingsExceptions {
            it.isRecentlyVisitedFeatureEnabled = false
            it.isPocketEnabled = false
        }

        val firstWebPage = mockWebServer.getGenericAsset(4)
        val secondWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(firstWebPage.url) {
                verifyPageContent(firstWebPage.content)
                verifyUrl(firstWebPage.url.toString())
            }
            .goToHomescreen {
                verifyContinueSectionIsDisplayed()
                verifyContinueItemTitle(composeTestRule, firstWebPage.title)
                verifyContinueItemWithUrl(composeTestRule, firstWebPage.url.toString())
            }

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(secondWebPage.url) {
                verifyPageContent(secondWebPage.content)
                verifyUrl(secondWebPage.url.toString())
            }
            .goToHomescreen {
                verifyContinueSectionIsDisplayed()
                verifyContinueItemTitle(composeTestRule, secondWebPage.title)
                verifyContinueItemWithUrl(composeTestRule, secondWebPage.url.toString())
            }
            .openTabDrawer {
                closeTabWithTitle(secondWebPage.title)
                waitUntilSnackbarGone()
                verifyExistingOpenTabs(firstWebPage.title)
            }
            .closeTabDrawer {}

        homeScreen(composeTestRule) {
                verifyContinueSectionIsDisplayed()
                verifyContinueItemTitle(composeTestRule, firstWebPage.title)
                verifyContinueItemWithUrl(composeTestRule, firstWebPage.url.toString())
            }
            .openTabDrawer {
                closeTab()
            }

        homeScreen(composeTestRule) {
            verifyContinueSectionIsNotDisplayed()
        }
    }
}
