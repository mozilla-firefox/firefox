/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.helpers.TestAssetHelper.articleSummaryAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsAiControlsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsPageSummariesSelectors

class SettingsAIControlsTest : BaseTest() {

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036059
    @Critical
    @Test
    fun verifyAIControlsSettingsUITest() {
        // The AI-controls Page summaries row stays absent until the summarize opt-in is set (fresh installs
        // seed it on first run; the test profile doesn't). Toggling off then on seeds it, left enabled.
        on.settingsPageSummaries.navigateToPage()
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)

        on.settingsAiControls.navigateToPage()
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.CHOICE_BANNER_SUPPORTING_TEXT)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCK_AI_DESCRIPTION)
        on.settingsAiControls.mozVerifyElementIsNotSelected(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerifyElementsByGroup(SettingsAiControlsSelectors.Group.AI_CONTROLS_VIEW)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.AI_POWERED_FEATURES_HEADER, SwipeDirection.UP)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.AI_POWERED_FEATURES_HEADER)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.TRANSLATIONS_DESCRIPTION)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.MORE_TRANSLATIONS_SETTINGS_LINK)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.PAGE_SUMMARIES_DESCRIPTION)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.MORE_PAGE_SUMMARY_SETTINGS_LINK)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.VOICE_SEARCH_DESCRIPTION)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036090
    @Critical
    @Test
    fun verifyEnabledFeatureStaysEnabledAfterUnblockingTest() {
        on.settingsAiControls.navigateToPage()

        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_TITLE)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_BLOCK_BUTTON)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)

        // Unblock: the enabled feature stays enabled and every other feature is available (toggle on).
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE, SwipeDirection.DOWN)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerifyElementIsNotSelected(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036096
    // Translations feature state is checked via its AI-controls toggle: the standalone Translations settings
    // entry is gated on a translation engine the test devices lack, so that screen is unreachable here.
    @Critical
    @Test
    fun verifyTranslationsBlockedStateViaBlockAIEnhancementsToggleTest() {
        // Seed the summarize opt-in so the Page summaries row renders (see verifyAIControlsSettingsUITest).
        on.settingsPageSummaries.navigateToPage()
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)

        on.settingsAiControls.navigateToPage()

        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_TITLE)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_BLOCK_BUTTON)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCKED_INFO_BANNER)

        // All three AI features are disabled.
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsNotSelected(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsNotSelected(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE)
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsNotSelected(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE)

        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.TRANSLATIONS_TOGGLE)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036068
    @Critical
    @Test
    fun verifyVoiceSearchBlockedViaBlockAIEnhancementsToggleTest() {
        on.settingsAiControls.navigateToPage()

        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_TITLE)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.BLOCK_AI_DIALOG_BLOCK_BUTTON)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.BLOCK_AI_TOGGLE)
        on.settingsAiControls.mozVerify(SettingsAiControlsSelectors.BLOCKED_INFO_BANNER)

        // Voice search is unavailable in the search bar while blocked.
        on.searchBar.navigateToPage()
        on.searchBar.mozVerifyElementAbsent(SearchBarSelectors.VOICE_SEARCH_BUTTON)

        on.settingsAiControls.navigateToPage()
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozClick(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.VOICE_SEARCH_TOGGLE)

        // Voice search is now available in the search bar.
        on.searchBar.navigateToPage()
        on.searchBar.mozVerify(SearchBarSelectors.VOICE_SEARCH_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4036073
    @Critical
    @Test
    fun verifyShakeToSummarizeEnabledStateTest() {
        // Skip the shake-to-summarize CFR.
        composeRule.activityRule.applySettingsExceptions {
            it.hasSeenShakeToSummarizeToolbarCfr = true
        }

        // Toggling off then on seeds the opt-in (so the AI-controls row renders) and leaves it enabled.
        // This toggle carries a ToggleableState (checked), not selected.
        on.settingsPageSummaries.navigateToPage()
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
        on.settingsPageSummaries.mozClick(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
        on.settingsPageSummaries.mozVerifyElementIsChecked(SettingsPageSummariesSelectors.SUMMARIZE_PAGES_TOGGLE)
        on.settingsPageSummaries.mozVerify(SettingsPageSummariesSelectors.SHAKE_TO_SUMMARIZE_OPTION)

        on.settingsAiControls.navigateToPage()
        on.settingsAiControls.mozSwipeTo(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE, SwipeDirection.UP)
        on.settingsAiControls.mozVerifyElementIsSelected(SettingsAiControlsSelectors.PAGE_SUMMARIES_TOGGLE)

        // The sheet opens regardless of the backend outcome (summary, error, or sign-in), so assert the sheet itself.
        val article = mockWebServer.articleSummaryAsset
        on.browserPage.navigateToPage(article.url.toString()).verifyPageContent(article.content)
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.SUMMARIZE_PAGE_BUTTON)
        on.browserPage.mozVerify(BrowserPageSelectors.SUMMARIZE_BOTTOM_SHEET)
    }
}
