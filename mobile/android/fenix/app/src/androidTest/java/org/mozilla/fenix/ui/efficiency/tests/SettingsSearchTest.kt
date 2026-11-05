/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchAddSearchEngineSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchDefaultSearchEngineSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchDefaultSearchEngineSelectors.DEFAULT_SEARCH_ENGINE_OPTION
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchManageShortcutsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors.SEARCH_ENGINE_SELECTOR_ICON

class SettingsSearchTest : BaseTest() {
    private val defaultSearchEngineList =
        listOf(
            "Bing",
            "DuckDuckGo",
            "Google",
        )

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203333
    @Critical
    @Test
    fun verifySearchSettingsMenuItemsTest() {
        on.settingsSearch
            .navigateToPage()
            // Search engines section
            .mozVerify(SettingsSearchSelectors.SETTINGS_SEARCH_TITLE)
            .mozVerify(SettingsSelectors.GO_BACK_BUTTON)
            .mozVerify(SettingsSearchSelectors.SEARCH_ENGINES_SECTION_HEADER)
            .mozVerify(SettingsSearchSelectors.DEFAULT_SEARCH_ENGINE_SETTING_OPTION)
            .mozVerifyElementHasSiblingWithText(
                SettingsSearchSelectors.DEFAULT_SEARCH_ENGINE_SETTING_OPTION,
                siblingText = "Google",
            )
            .mozVerify(SettingsSearchSelectors.MANAGE_SHORTCUTS_SETTING_OPTION)
            .mozVerifyElementHasSiblingWithText(
                SettingsSearchSelectors.MANAGE_SHORTCUTS_SETTING_OPTION,
                siblingText = SettingsSearchSelectors.MANAGE_SHORTCUTS_SUMMARY,
            )
            .mozSwipeTo(SettingsSearchSelectors.GOOGLE_LENS_OPTION, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.GOOGLE_LENS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.GOOGLE_LENS_OPTION)
            .mozSwipeTo(SettingsSearchSelectors.HOME_SCREEN_WIDGET_OPTION, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.HOME_SCREEN_WIDGET_OPTION)
            // Suggestions from search engines section: search + trending + recent on by default,
            // private-session suggestions off by default
            .mozSwipeTo(SettingsSearchSelectors.SEARCH_SUGGESTIONS_SECTION_HEADER, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.SEARCH_SUGGESTIONS_SECTION_HEADER)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SHOW_SEARCH_SUGGESTIONS_TOGGLE)
            .mozVerifyOptionCheckBoxIsNotChecked(SettingsSearchSelectors.SHOW_SUGGESTIONS_IN_PRIVATE_OPTION)
            .mozVerifyOptionCheckBoxIsChecked(SettingsSearchSelectors.SHOW_TRENDING_SUGGESTIONS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SHOW_RECENT_SEARCHES_OPTION)
            // Address bar - Firefox Suggest section: all sources on by default
            .mozSwipeTo(SettingsSearchSelectors.ADDRESS_BAR_FX_SUGGEST_HEADER, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.ADDRESS_BAR_FX_SUGGEST_HEADER)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SEARCH_BROWSING_HISTORY_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SEARCH_BOOKMARKS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SEARCH_SYNCED_TABS_OPTION)
            // Firefox Suggest sources (shown when FxSuggest is enabled): on by default, plus the Learn more link
            .mozSwipeTo(SettingsSearchSelectors.NONSPONSORED_SUGGESTIONS_OPTION, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.NONSPONSORED_SUGGESTIONS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.NONSPONSORED_SUGGESTIONS_OPTION)
            .mozSwipeTo(SettingsSearchSelectors.SPONSORED_SUGGESTIONS_OPTION, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.SPONSORED_SUGGESTIONS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SPONSORED_SUGGESTIONS_OPTION)
            .mozSwipeTo(SettingsSearchSelectors.LEARN_ABOUT_FX_SUGGEST_LINK, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.LEARN_ABOUT_FX_SUGGEST_LINK)
            // Address bar preferences section: all on by default
            .mozSwipeTo(SettingsSearchSelectors.ADDRESS_BAR_PREFERENCES_SECTION_HEADER, direction = SwipeDirection.UP)
            .mozVerify(SettingsSearchSelectors.ADDRESS_BAR_PREFERENCES_SECTION_HEADER)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.SHOW_CLIPBOARD_SUGGESTIONS_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.VOICE_SEARCH_OPTION)
            .mozVerifyOptionSwitchIsChecked(SettingsSearchSelectors.AUTOCOMPLETE_URLS_OPTION)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203308
    @SmokeTest
    @Test
    fun verifyTheDefaultSearchEngineCanBeChangedTest() {
        defaultSearchEngineList.forEach {
            on.settingsSearchDefaultSearchEngine
                .navigateToPage()
                .mozClick(DEFAULT_SEARCH_ENGINE_OPTION(engineName = it))
            on.home.navigateToPage().mozVerify(SEARCH_ENGINE_SELECTOR_ICON(searchEngineName = it))
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203340
    @SmokeTest
    @Test
    fun verifySearchShortcutChangesAreReflectedInSearchSelectorMenuTest() {
        on.settingsSearchManageShortcuts
            .navigateToPage()
            .selectSearchShortcut(engineName = "Reddit", checkboxIndex = 10)
            .selectSearchShortcut(engineName = "YouTube", checkboxIndex = 13)

        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozVerify(SearchBarSelectors.SEARCH_SELECTOR_MENU_ENGINE(engineName = "YouTube"))
            .mozVerify(SearchBarSelectors.SEARCH_SELECTOR_MENU_ENGINE(engineName = "Reddit"))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203309
    @SmokeTest
    @Test
    fun verifyCustomSearchEngineCanBeAddedFromSearchEngineMenuTest() {
        val engineTitle = "TestSearchEngine"
        // Any %s template is accepted; the engine is added without a reachability check (only the icon
        // fetch hits the network, and that is best-effort). localhost keeps it offline and fast.
        val engineUrl = "https://localhost/?q=%s"

        // Add the custom engine from the Default search engine menu. A local binding keeps the page
        // type across page-specific helpers, since the moz* verbs return BasePage.
        val addEnginePage = on.settingsSearchAddSearchEngine.navigateToPage()
        addEnginePage.mozVerifyElementIsNotEnabled(SettingsSearchAddSearchEngineSelectors.SAVE_BUTTON)
        addEnginePage.typeCustomEngineDetails(engineName = engineTitle, engineUrl = engineUrl)
        addEnginePage.mozVerifyElementIsEnabled(SettingsSearchAddSearchEngineSelectors.SAVE_BUTTON)
        addEnginePage.saveNewSearchEngine()

        // Back on the Default search engine list: the new engine is present, exposes an overflow menu,
        // and can be set as the default.
        on.settingsSearchDefaultSearchEngine
            .navigateToPage()
            .mozVerify(DEFAULT_SEARCH_ENGINE_OPTION(engineName = engineTitle))
            .mozClick(SettingsSearchDefaultSearchEngineSelectors.ENGINE_OVERFLOW_MENU)
            .mozVerify(SettingsSearchDefaultSearchEngineSelectors.OVERFLOW_EDIT_ITEM)
            .mozPressBackUntilGone(SettingsSearchDefaultSearchEngineSelectors.OVERFLOW_EDIT_ITEM)
            .mozClick(DEFAULT_SEARCH_ENGINE_OPTION(engineName = engineTitle))

        // A custom engine set as default is not offered as an alternative search shortcut.
        on.settingsSearchManageShortcuts
            .navigateToPage()
            .mozVerifyElementAbsent(
                SettingsSearchManageShortcutsSelectors.SEARCH_ENGINE_SHORTCUT(engineName = engineTitle)
            )

        // The Settings > Search summary reflects the new default engine.
        on.settings
            .navigateToPage()
            .mozVerifyElementHasSiblingWithText(SettingsSelectors.SEARCH_SETTING_ROW, siblingText = engineTitle)

        // The search selector shows the custom engine as the default and lists it.
        on.searchBar
            .navigateToPage()
            .mozVerify(SEARCH_ENGINE_SELECTOR_ICON(searchEngineName = engineTitle))
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozVerify(SearchBarSelectors.SEARCH_SELECTOR_MENU_ENGINE(engineName = engineTitle))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/233588
    // Depends on live network search suggestions (DuckDuckGo), so it carries the same network flakiness
    // and CI caution as the legacy test. DuckDuckGo is used because Google suggestions are not reliably
    // returned on a cold run (https://bugzilla.mozilla.org/show_bug.cgi?id=1813587).
    @SmokeTest
    @Test
    fun verifyShowSearchSuggestionsToggleTest() {
        // Suggestions are on by default: typing a term surfaces DuckDuckGo suggestions.
        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SELECTOR_MENU_ENGINE(engineName = "DuckDuckGo"))
            .mozClearAndEnterText("mozilla ", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyAnyContainsText(SearchBarSelectors.AWESOMEBAR_SUGGESTION, text = "mozilla")

        // Dismiss the search bar back to Home before routing on: navigating to Settings straight from
        // the search bar would let the planner pick the equal-cost "type a URL + Enter" browser route, which
        // submits the leftover "mozilla " text and opens a tab. Home -> Settings avoids that.
        on.home.navigateToPage()

        // Turn the Show search suggestions setting off.
        on.settingsSearch.navigateToPage().mozClick(SettingsSearchSelectors.SHOW_SEARCH_SUGGESTIONS_TOGGLE)

        // Back to Home, then reopen search: with the setting off the same query surfaces no suggestion.
        on.home.navigateToPage()
        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SELECTOR_MENU_ENGINE(engineName = "DuckDuckGo"))
            .mozClearAndEnterText("mozilla", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyNoneContainText(SearchBarSelectors.AWESOMEBAR_SUGGESTION, text = "mozilla")
    }
}
