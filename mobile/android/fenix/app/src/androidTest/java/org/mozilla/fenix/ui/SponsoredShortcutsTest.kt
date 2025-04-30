/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.Constants.defaultTopSitesList
import org.mozilla.fenix.helpers.DataGenerationHelper.getSponsoredShortcutTitle
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen

/**
 * Tests Sponsored shortcuts functionality
 */

class SponsoredShortcutsTest : TestSetup() {
    private lateinit var sponsoredShortcutTitle: String
    private lateinit var sponsoredShortcutTitle2: String

    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides(skipOnboarding = true)

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729331
    // Expected for en-us defaults
    @SmokeTest
    @Test
    fun verifySponsoredShortcutsListTest() {
        homeScreen {
            defaultTopSitesList.values.forEach { value ->
                verifyExistingTopSitesTabs(value)
            }
        }.openThreeDotMenu {
        }.openCustomizeHome {
            verifySponsoredShortcutsCheckBox(true)
            clickSponsoredShortcuts()
            verifySponsoredShortcutsCheckBox(false)
        }.goBackToHomeScreen {
            verifyNotExistingSponsoredTopSitesList()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729338
    @Test
    fun openSponsoredShortcutTest() {
        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
        }.openSponsoredShortcut(sponsoredShortcutTitle) {
            verifyUrl(sponsoredShortcutTitle)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729334
    @Test
    fun openSponsoredShortcutInPrivateTabTest() {
        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
        }.openContextMenuOnSponsoredShortcut(sponsoredShortcutTitle) {
        }.openTopSiteInPrivateTab {
            verifyUrl(sponsoredShortcutTitle)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729335
    @Test
    fun openSponsorsAndYourPrivacyOptionTest() {
        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
        }.openContextMenuOnSponsoredShortcut(sponsoredShortcutTitle) {
        }.clickSponsorsAndPrivacyButton {
            verifySponsoredShortcutsLearnMoreURL()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729336
    @Test
    fun openSponsoredShortcutsSettingsOptionTest() {
        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
        }.openContextMenuOnSponsoredShortcut(sponsoredShortcutTitle) {
        }.clickSponsoredShortcutsSettingsButton {
            verifyHomePageView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729337
    @Test
    fun verifySponsoredShortcutsDetailsTest() {
        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(3)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 3)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729328
    // 1 sponsored shortcut should be displayed if there are 7 pinned top sites
    @Test
    fun verifySponsoredShortcutsListWithSevenPinnedSitesTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)
        val thirdWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 3)
        val fourthWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 4)

        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(3)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 3)
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
            verifyPageContent(firstWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
        }.addToFirefoxHome {
        }.goToHomescreen {
            verifyExistingTopSitesTabs(firstWebPage.title)
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(secondWebPage.url) {
            verifyPageContent(secondWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
        }.addToFirefoxHome {
        }.goToHomescreen {
            verifyExistingTopSitesTabs(secondWebPage.title)
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(thirdWebPage.url) {
            verifyPageContent(thirdWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
        }.addToFirefoxHome {
        }.goToHomescreen {
            verifyExistingTopSitesTabs(thirdWebPage.title)
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(fourthWebPage.url) {
            verifyPageContent(fourthWebPage.content)
        }.openThreeDotMenu {
            expandMenuFully()
        }.addToFirefoxHome {
        }.goToHomescreen {
            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle2, 3)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729329
    // No sponsored shortcuts should be displayed if there are 8 pinned top sites
    @Test
    fun verifySponsoredShortcutsListWithEightPinnedSitesTest() {
        val pagesList = listOf(
            TestAssetHelper.getGenericAsset(mockWebServer, 1),
            TestAssetHelper.getGenericAsset(mockWebServer, 2),
            TestAssetHelper.getGenericAsset(mockWebServer, 3),
            TestAssetHelper.getGenericAsset(mockWebServer, 4),
            TestAssetHelper.getLoremIpsumAsset(mockWebServer),
        )

        homeScreen {
            sponsoredShortcutTitle = getSponsoredShortcutTitle(2)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(3)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 3)

            MockBrowserDataHelper.addPinnedSite(
                Pair(pagesList[0].title, pagesList[0].url.toString()),
                Pair(pagesList[1].title, pagesList[1].url.toString()),
                Pair(pagesList[2].title, pagesList[2].url.toString()),
                Pair(pagesList[3].title, pagesList[3].url.toString()),
                Pair(pagesList[4].title, pagesList[4].url.toString()),
                activityTestRule = activityIntentTestRule,
            )

            verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle2, 3)
        }
    }
}
