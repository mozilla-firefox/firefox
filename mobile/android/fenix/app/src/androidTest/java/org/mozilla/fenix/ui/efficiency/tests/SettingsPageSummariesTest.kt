/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.articleSummaryAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsPageSummariesSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors

class SettingsPageSummariesTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036042
    @SmokeTest
    @Test
    fun verifyPageSummariesUITest() {
        // When: we open the Settings screen
        // Then: the "Page summaries" entry is present
        on.settings
            .navigateToPage()
            .mozSwipeTo(SettingsSelectors.PAGE_SUMMARIES_BUTTON)
            .mozVerify(SettingsSelectors.PAGE_SUMMARIES_BUTTON)

        // When: we open the Page summaries sub-menu
        // Then: the Page summaries view and all its options are displayed
        on.settingsPageSummaries
            .navigateToPage()
            .mozVerify(SettingsPageSummariesSelectors.PAGE_SUMMARIES_TOOLBAR_TITLE)
            .mozVerify(SettingsSelectors.GO_BACK_BUTTON)
            .mozVerify(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_OPTION)
            .mozVerify(SettingsPageSummariesSelectors.LEARN_MORE_LINK)
            .mozVerify(SettingsPageSummariesSelectors.GESTURES_SUB_HEADER)
            .mozVerify(SettingsPageSummariesSelectors.SHAKE_TO_SUMMARIZE_OPTION)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036045
    @Critical
    @Test
    fun verifyTheSummarizePagesToggleBehaviourTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = true
        }

        val defaultWebPage = mockWebServer.articleSummaryAsset

        on.settingsPageSummaries
            .navigateToPage()
            .mozVerifyElementIsChecked(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
            .mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
            .mozVerifyElementIsNotChecked(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)

        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerifyElementAbsent(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)

        on.settingsPageSummaries
            .navigateToPage()
            .mozVerifyElementIsNotChecked(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
            .mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
            .mozVerifyElementIsChecked(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)

        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerify(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
            .mozClick(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
        on.browserPage.mozVerifyElementsByGroup(BrowserPageSelectors.Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4035976
    @Critical
    @Test
    fun verifyTheShakeToSummarizeDismissCFRTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = false
        }

        val defaultWebPage = mockWebServer.articleSummaryAsset

        on.browserPage
            .navigateToPage(defaultWebPage.url.toString())
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.SHAKE_TO_SUMMARIZE_CFR)
            .mozClick(BrowserPageSelectors.SHAKE_TO_SUMMARIZE_CFR_DISMISS_BUTTON)
            .mozVerifyElementsByGroupAbsent(BrowserPageSelectors.Group.SHAKE_TO_SUMMARIZE_CFR)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4035988
    @Critical
    @Test
    fun verifyTheDismissalOfTheShakeToSummarizePromptTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = true
        }

        val defaultWebPage = mockWebServer.articleSummaryAsset

        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozVerify(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
            .mozClick(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
        on.browserPage
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.SIGN_IN_TO_SUMMARIZE_BOTTOM_SHEET)
            .mozSwipeElementUntilAbsent(
                BrowserPageSelectors.SIGN_IN_TO_SUMMARIZE_HANDLE,
                SwipeDirection.DOWN,
                maxSwipes = 3,
            )
    }
}
