/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.articleSummaryAsset
import org.mozilla.fenix.helpers.TestAssetHelper.loremIpsumAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class SettingsPageSummariesTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                hasSeenShakeToSummarizeToolbarCfr = false,
            )
        ) {
            it.activity
        }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036042
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsPageSummariesTest#verifyPageSummariesUITest"],
        bug = 2062914,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyPageSummariesUITest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifyPageSummariesButton()
            }
            .openPageSummariesSubMenu(composeTestRule) {
                verifyPageSummariesView()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036045
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SettingsPageSummariesTest#verifyTheSummarizePagesToggleBehaviourTest"
            ],
        bug = 2079744,
        since = "2026-10",
    )
    @Critical
    @Test
    fun verifyTheSummarizePagesToggleBehaviourTest() {
        composeTestRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = true
        }

        val articlePage = mockWebServer.articleSummaryAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openPageSummariesSubMenu(composeTestRule) {
                verifySummarizePagesToggle(true)
                clickSummarizePagesToggle()
                verifySummarizePagesToggle(false)
            }

        exitMenu()

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(articlePage.url) {}
            .openThreeDotMenu {
                clickTheMoreButton()
                verifySummarizePageButton(isDisplayed = false)
            }
            .dismissMainMenu {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openPageSummariesSubMenu(composeTestRule) {
                verifySummarizePagesToggle(false)
                clickSummarizePagesToggle()
                verifySummarizePagesToggle(true)
            }

        exitMenu()

        browserScreen(composeTestRule) {}
            .openThreeDotMenu {
                clickTheMoreButton()
                verifySummarizePageButton()
            }
            .clickSummarizePageButton {
                verifyTheSignInToSummarizeBottomSheet()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4035976
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.SettingsPageSummariesTest#verifyTheShakeToSummarizeDismissCFRTest"],
        bug = 2080832,
        since = "2026-10",
    )
    @Critical
    @Test
    fun verifyTheShakeToSummarizeDismissCFRTest() {
        val articlePage = mockWebServer.articleSummaryAsset
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(articlePage.url) {
                waitForPageToLoad()
                verifyTheSummarizeCFR(true)
                clickTheDismissButtonOnSummarizeCFR()
                verifyTheSummarizeCFR(false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4035977
    @Test
    fun verifyTheShakeToSummarizeCFRIsOnlyDisplayedOnceTest() {
        val firstWebsite = mockWebServer.articleSummaryAsset
        val secondWebsite = mockWebServer.loremIpsumAsset
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(firstWebsite.url) {
                waitForPageToLoad()
                verifyTheSummarizeCFR(true)
                clickTheDismissButtonOnSummarizeCFR()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(secondWebsite.url) {
                waitForPageToLoad()
                verifyTheSummarizeCFR(false)
            }
    }
}
