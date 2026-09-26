/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.util.Log
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.isDefaultPinnedShortcutsOnHomepage
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.Constants.defaultPinnedShortcutTitles
import org.mozilla.fenix.helpers.Constants.sponsoredShortcutTitles
import org.mozilla.fenix.helpers.DataGenerationHelper.getSponsoredShortcutTitle
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.loremIpsumAsset
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/** Tests Sponsored shortcuts functionality */
class SponsoredShortcutsTest {
    private lateinit var sponsoredShortcutTitle: String
    private lateinit var sponsoredShortcutTitle2: String

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
                    .goBack(composeTestRule) {
                        verifyExistingTopSitesList()
                    }

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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729331
    // Expected for en-us defaults
    @SmokeTest
    @Test
    fun verifySponsoredShortcutsListTest() {
        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                sponsoredShortcutTitles.forEach { title -> verifyExistingTopSitesTabs(title) }
                if (isDefaultPinnedShortcutsOnHomepage()) {
                    defaultPinnedShortcutTitles.forEach { title -> verifyExistingTopSitesTabs(title) }
                }
                verifyAddShortcutButtonExists()
            }
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openHomepageSubMenu {
                verifySponsoredShortcutsCheckBox(true)
                clickSponsoredShortcuts()
                verifySponsoredShortcutsCheckBox(false)
            }
            .goBack {}
            .goBack(composeTestRule) {
                verifyNotExistingSponsoredTopSitesList()
                if (isDefaultPinnedShortcutsOnHomepage()) {
                    verifyAddShortcutButtonExists()
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729338
    @Test
    fun openSponsoredShortcutTest() {
        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            }
            .openTopSiteTabWithTitle(sponsoredShortcutTitle) {
                verifyUrl(sponsoredShortcutTitle)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729334
    @Test
    fun openSponsoredShortcutInPrivateTabTest() {
        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            }
            .openContextMenuOnTopSitesWithTitle(sponsoredShortcutTitle) {}
            .openTopSiteInPrivateTab {
                verifyUrl(sponsoredShortcutTitle)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729335
    @Test
    fun openSponsorsAndYourPrivacyOptionTest() {
        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            }
            .openContextMenuOnTopSitesWithTitle(sponsoredShortcutTitle) {}
            .clickSponsorsAndPrivacyButton {
                verifySponsoredShortcutsLearnMoreURL()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729336
    @Test
    fun openSponsoredShortcutsSettingsOptionTest() {
        homeScreen(composeTestRule) {
                verifyExistingTopSitesList()
                sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            }
            .openContextMenuOnTopSitesWithTitle(sponsoredShortcutTitle) {}
            .clickSponsoredShortcutsSettingsButton {
                verifyHomePageView()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729337
    @Test
    fun verifySponsoredShortcutsDetailsTest() {
        homeScreen(composeTestRule) {
            verifyExistingTopSitesList()
            sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(2)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 1)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 2)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729328
    // 1 sponsored shortcut should be displayed if there are 7 pinned top sites
    @Test
    fun verifySponsoredShortcutsListWithSevenPinnedSitesTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)
        val thirdWebPage = mockWebServer.getGenericAsset(3)
        val fourthWebPage = mockWebServer.getGenericAsset(4)

        homeScreen(composeTestRule) {
            verifyExistingTopSitesList()
            sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(2)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 1)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 2)
        }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(firstWebPage.url) {
                verifyPageContent(firstWebPage.content)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToShortcutsButton {}
            .goToHomescreen {
                verifyExistingTopSitesTabs(firstWebPage.title)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(secondWebPage.url) {
                verifyPageContent(secondWebPage.content)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToShortcutsButton {}
            .goToHomescreen {
                verifyExistingTopSitesTabs(secondWebPage.title)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(thirdWebPage.url) {
                verifyPageContent(thirdWebPage.content)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToShortcutsButton {}
            .goToHomescreen {
                verifyExistingTopSitesTabs(thirdWebPage.title)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(fourthWebPage.url) {
                verifyPageContent(fourthWebPage.content)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToShortcutsButton {}
            .goToHomescreen {
                verifySponsoredShortcutDetails(sponsoredShortcutTitle, 1)
                verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle2, 3)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1729329
    // No sponsored shortcuts should be displayed if there are 8 pinned top sites
    @Test
    fun verifySponsoredShortcutsListWithEightPinnedSitesTest() {
        val pagesList =
            listOf(
                mockWebServer.getGenericAsset(1),
                mockWebServer.getGenericAsset(2),
                mockWebServer.getGenericAsset(3),
                mockWebServer.getGenericAsset(4),
                mockWebServer.loremIpsumAsset,
            )

        homeScreen(composeTestRule) {
            verifyExistingTopSitesList()

            sponsoredShortcutTitle = getSponsoredShortcutTitle(1)
            sponsoredShortcutTitle2 = getSponsoredShortcutTitle(2)

            verifySponsoredShortcutDetails(sponsoredShortcutTitle, 1)
            verifySponsoredShortcutDetails(sponsoredShortcutTitle2, 2)

            MockBrowserDataHelper.addPinnedSite(
                Pair(pagesList[0].title, pagesList[0].url.toString()),
                Pair(pagesList[1].title, pagesList[1].url.toString()),
                Pair(pagesList[2].title, pagesList[2].url.toString()),
                Pair(pagesList[3].title, pagesList[3].url.toString()),
                Pair(pagesList[4].title, pagesList[4].url.toString()),
                activityTestRule = composeTestRule.activityRule,
            )

            verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle, 2)
            verifySponsoredShortcutDoesNotExist(sponsoredShortcutTitle2, 3)
        }
    }
}
