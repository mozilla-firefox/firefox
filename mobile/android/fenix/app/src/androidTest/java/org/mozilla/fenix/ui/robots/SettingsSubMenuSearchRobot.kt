/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.preference.R as preferenceR
import androidx.recyclerview.widget.RecyclerView
import androidx.test.espresso.Espresso.closeSoftKeyboard
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.ViewInteraction
import androidx.test.espresso.action.ViewActions.clearText
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.action.ViewActions.typeText
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.contrib.RecyclerViewActions
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isDescendantOfA
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withChild
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withTagValue
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.By.desc
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import org.hamcrest.CoreMatchers
import org.hamcrest.Matchers.allOf
import org.hamcrest.Matchers.endsWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getAvailableSearchEngines
import org.mozilla.fenix.helpers.DataGenerationHelper.getRegionSearchEnginesList
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.isChecked
import org.mozilla.fenix.helpers.isEnabled
import org.mozilla.fenix.settings.search.SearchEngineShortcutsTestTag

/** Implementation of Robot Pattern for the settings search sub menu. */
class SettingsSubMenuSearchRobot {
    fun verifyToolbarText(title: String) {
        Log.i(TAG, "verifyToolbarText: Trying to verify that the $title toolbar title is visible")
        onView(
                allOf(
                    withId(R.id.navigationToolbar),
                    hasDescendant(withContentDescription(R.string.action_bar_up_description)),
                    hasDescendant(withText(title)),
                )
            )
            .check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyToolbarText: Verified that the $title toolbar title is visible")
    }

    fun verifySearchEnginesSectionHeader() {
        Log.i(
            TAG,
            "verifySearchEnginesSectionHeader: Trying to verify that the \"Search engines\" heading is displayed",
        )
        onView(withText("Search engines")).check(matches(isDisplayed()))
        Log.i(TAG, "verifySearchEnginesSectionHeader: Verified that the \"Search engines\" heading is displayed")
    }

    fun verifyDefaultSearchEngineHeader() {
        Log.i(
            TAG,
            "verifyDefaultSearchEngineHeader: Trying to verify that the \"Default search engine\" option is displayed",
        )
        defaultSearchEngineHeader.check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyDefaultSearchEngineHeader: Verified that the \"Default search engine\" option is displayed")
    }

    fun verifyDefaultSearchEngineSummary(engineName: String) {
        Log.i(
            TAG,
            "verifyDefaultSearchEngineSummary: Trying to verify that the \"Default search engine\" option has $engineName as summary",
        )
        defaultSearchEngineHeader.check(matches(hasSibling(withText(engineName))))
        Log.i(
            TAG,
            "verifyDefaultSearchEngineSummary: Verified that the \"Default search engine\" option has $engineName as summary",
        )
    }

    fun verifyManageSearchShortcutsHeader() {
        Log.i(
            TAG,
            "verifyManageSearchShortcutsHeader: Trying to verify that the \"Manage alternative search engines\" option is displayed",
        )
        manageSearchShortcutsHeader.check(matches(isDisplayed()))
        Log.i(
            TAG,
            "verifyManageSearchShortcutsHeader: Verified that the \"Manage alternative search engines\" option is displayed",
        )
    }

    fun verifyManageShortcutsSummary() {
        Log.i(
            TAG,
            "verifyManageShortcutsSummary: Trying to verify that the \"Manage alternative search engines\" option has \"Edit engines visible in the search menu\" as summary",
        )
        manageSearchShortcutsHeader.check(matches(hasSibling(withText("Edit engines visible in the search menu"))))
        Log.i(
            TAG,
            "verifyManageShortcutsSummary: Verified that the \"Manage alternative search engines\" option has \"Edit engines visible in the search menu\" as summary",
        )
    }

    fun verifyEnginesShortcutsListHeader() = assertUIObjectExists(itemWithText("Engines visible on the search menu"))

    fun verifyAddressBarSectionHeader() {
        Log.i(
            TAG,
            "verifyAddressBarSectionHeader: Trying to verify that the \"Address bar - Firefox Suggest\" heading is displayed",
        )
        onView(withText("Address bar - Firefox Suggest")).check(matches(isDisplayed()))
        Log.i(
            TAG,
            "verifyAddressBarSectionHeader: Verified that the \"Address bar - Firefox Suggest\" heading is displayed",
        )
    }

    fun verifyDefaultSearchEngineList() {
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"Google\" search engine option has a favicon",
        )
        defaultSearchEngineOption("Google").check(matches(hasSibling(withId(R.id.engine_icon))))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"Google\" search engine option has a favicon")
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"Google\" search engine option is displayed",
        )
        defaultSearchEngineOption("Google").check(matches(isDisplayed()))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"Google\" search engine option is displayed")
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"Bing\" search engine option has a favicon",
        )
        defaultSearchEngineOption("Bing").check(matches(hasSibling(withId(R.id.engine_icon))))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"Bing\" search engine option has a favicon")
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"Bing\" search engine option is displayed",
        )
        defaultSearchEngineOption("Bing").check(matches(isDisplayed()))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"Bing\" search engine option is displayed")
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"DuckDuckGo\" search engine option has a favicon",
        )
        defaultSearchEngineOption("DuckDuckGo").check(matches(hasSibling(withId(R.id.engine_icon))))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"DuckDuckGo\" search engine option has a favicon")
        Log.i(
            TAG,
            "verifyDefaultSearchEngineList: Trying to verify that the \"DuckDuckGo\" search engine option is displayed",
        )
        defaultSearchEngineOption("DuckDuckGo").check(matches(isDisplayed()))
        Log.i(TAG, "verifyDefaultSearchEngineList: Verified that the \"DuckDuckGo\" search engine option is displayed")
        assertUIObjectExists(addSearchEngineButton())
    }

    fun verifyManageShortcutsList(testRule: ComposeTestRule) {
        val defaultEngineId = appContext.components.core.store.state.search.selectedOrDefaultSearchEngine?.id
        val availableShortcutsEngines =
            (getRegionSearchEnginesList() + getAvailableSearchEngines()).filter { it.id != defaultEngineId }

        availableShortcutsEngines.forEach {
            Log.i(
                TAG,
                "verifyManageShortcutsList: Trying to verify that the ${it.name} alternative search engine is displayed",
            )
            testRule
                .onNodeWithText(it.name)
                .assert(hasAnySibling(hasContentDescription("Search engine: ${it.name}")))
                .assertIsDisplayed()
            Log.i(TAG, "verifyManageShortcutsList: Verify that the ${it.name} alternative search engine is displayed")
        }

        assertUIObjectExists(addSearchEngineButton())
    }

    /** Method that verifies the selected engines inside the Manage search shortcuts list. */
    fun verifySearchShortcutChecked(testRule: ComposeTestRule, vararg engineShortcut: EngineShortcut) {
        engineShortcut.forEach {
            Log.i(
                TAG,
                "verifySearchShortcutChecked: Trying to verify ${it.name}'s check box is checked: ${it.isChecked}",
            )
            val checkbox = testRule.onNodeWithTag(SearchEngineShortcutsTestTag.checkbox(it.name))
            if (it.isChecked) {
                checkbox.assertIsOn()
            } else {
                checkbox.assertIsOff()
            }
            Log.i(TAG, "verifySearchShortcutChecked: Verified ${it.name}'s check box is checked: ${it.isChecked}")
        }
    }

    fun verifyAutocompleteURlsIsEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifyAutocompleteURlsIsEnabled: Trying to verify that the \"Autocomplete URLs\" toggle is checked: $enabled",
        )
        autocompleteSwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(
            TAG,
            "verifyAutocompleteURlsIsEnabled: Verified that the \"Autocomplete URLs\" toggle is checked: $enabled",
        )
    }

    fun verifyShowSearchSuggestionsEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifyShowSearchSuggestionsEnabled: Trying to verify that the \"Show search suggestions\" toggle is checked: $enabled",
        )
        showSearchSuggestionSwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(
            TAG,
            "verifyShowSearchSuggestionsEnabled: Verified that the \"Show search suggestions\" toggle is checked: $enabled",
        )
    }

    fun verifyShowSearchSuggestionsInPrivateEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifyShowSearchSuggestionsInPrivateEnabled: Trying to verify that the \"Show in private sessions\" check box is checked: $enabled",
        )
        showSuggestionsInPrivateModeSwitch()
            .check(
                matches(
                    hasSibling(
                        withChild(
                            allOf(
                                withClassName(endsWith("CheckBox")),
                                isChecked(enabled),
                            )
                        )
                    )
                )
            )
        Log.i(
            TAG,
            "verifyShowSearchSuggestionsInPrivateEnabled: Verified that the \"Show in private sessions\" check box is checked: $enabled",
        )
    }

    fun verifyShowClipboardSuggestionsEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifyShowClipboardSuggestionsEnabled: Trying to verify that the \"Show clipboard suggestions\" option is visible",
        )
        showClipboardSuggestionSwitch().check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyShowClipboardSuggestionsEnabled: Verified that the \"Show clipboard suggestions\" option is visible",
        )
        Log.i(
            TAG,
            "verifyShowClipboardSuggestionsEnabled: Trying to verify that the \"Show clipboard suggestions\" toggle is checked: $enabled",
        )
        showClipboardSuggestionSwitch()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(
            TAG,
            "verifyShowClipboardSuggestionsEnabled: Verified that the \"Show clipboard suggestions\" toggle is checked: $enabled",
        )
    }

    fun verifySearchBrowsingHistoryEnabled(enabled: Boolean) {
        Log.i(
            TAG,
            "verifySearchBrowsingHistoryEnabled: Trying to verify that the \"Search browsing history\" option is visible",
        )
        searchHistorySwitchButton().check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifySearchBrowsingHistoryEnabled: Verified that the \"Search browsing history\" option is visible",
        )
        Log.i(
            TAG,
            "verifySearchBrowsingHistoryEnabled: Trying to verify that the \"Search browsing history\" toggle is checked: $enabled",
        )
        searchHistorySwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(
            TAG,
            "verifySearchBrowsingHistoryEnabled: Verified that the \"Search browsing history\" toggle is checked: $enabled",
        )
    }

    fun verifySearchBookmarksEnabled(enabled: Boolean) {
        Log.i(TAG, "verifySearchBookmarksEnabled: Trying to verify that the \"Search bookmarks\" option is visible")
        searchBookmarksSwitchButton().check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifySearchBookmarksEnabled: Verified that the \"Search bookmarks\" option is visible")
        Log.i(
            TAG,
            "verifySearchBookmarksEnabled: Trying to verify that the \"Search bookmarks\" toggle is checked: $enabled",
        )
        searchBookmarksSwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(TAG, "verifySearchBookmarksEnabled: Verified that the \"Search bookmarks\" toggle is checked: $enabled")
    }

    fun verifySearchSyncedTabsEnabled(enabled: Boolean) {
        Log.i(TAG, "verifySearchSyncedTabsEnabled: Trying to verify that the \"Search synced tabs\" option is visible")
        searchSyncedTabsSwitchButton().check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifySearchSyncedTabsEnabled: Verified that the \"Search synced tabs\" option is visible")
        Log.i(
            TAG,
            "verifySearchSyncedTabsEnabled: Trying to verify that the \"Search synced tabs\" toggle is checked: $enabled",
        )
        searchSyncedTabsSwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(
            TAG,
            "verifySearchSyncedTabsEnabled: Verified that the \"Search synced tabs\" toggle is checked: $enabled",
        )
    }

    fun verifyVoiceSearchEnabled(enabled: Boolean) {
        Log.i(TAG, "verifyVoiceSearchEnabled: Trying to verify that the \"Show voice search\" option is visible")
        voiceSearchSwitchButton().check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyVoiceSearchEnabled: Verified that the \"Show voice search\" option is visible")
        Log.i(
            TAG,
            "verifyVoiceSearchEnabled: Trying to verify that the \"Show voice search\" toggle is checked: $enabled",
        )
        voiceSearchSwitchButton()
            .check(matches(hasCousin(allOf(withClassName(endsWith("Switch")), isChecked(enabled)))))
        Log.i(TAG, "verifyVoiceSearchEnabled: Verified that the \"Show voice search\" toggle is checked: $enabled")
    }

    fun openDefaultSearchEngineMenu() {
        Log.i(TAG, "openDefaultSearchEngineMenu: Trying to click the \"Default search engine\" button")
        defaultSearchEngineHeader.click()
        Log.i(TAG, "openDefaultSearchEngineMenu: Clicked the \"Default search engine\" button")
    }

    fun openManageShortcutsMenu() {
        Log.i(TAG, "openManageShortcutsMenu: Trying to click the \"Manage alternative search engines\" button")
        manageSearchShortcutsHeader.click()
        Log.i(TAG, "openManageShortcutsMenu: Clicked the \"Manage alternative search engines\" button")
    }

    fun changeDefaultSearchEngine(searchEngineName: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "changeDefaultSearchEngine: Started try #$i")
            try {
                assertUIObjectExists(itemWithResIdAndText("$packageName:id/engine_text", searchEngineName))
                Log.i(TAG, "changeDefaultSearchEngine: Trying to click the $searchEngineName option")
                itemWithResIdAndText("$packageName:id/engine_text", searchEngineName).click()
                Log.i(TAG, "changeDefaultSearchEngine: Clicked the $searchEngineName option")
                verifyDefaultSearchEngineSelected(searchEngineName)

                break
            } catch (e: Exception) {
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(TAG, "changeDefaultSearchEngine: Exception caught, executing fallback methods")
                    settingsSubMenuSearch {}
                        .goBackToSearchSettings {
                            openDefaultSearchEngineMenu()
                        }
                }
            }
        }
    }

    fun selectSearchShortcut(testRule: ComposeTestRule, shortcut: EngineShortcut) {
        Log.i(TAG, "selectSearchShortcut: Trying to click ${shortcut.name}'s alternative search engine check box")
        testRule.onNodeWithTag(SearchEngineShortcutsTestTag.checkbox(shortcut.name)).performClick()
        Log.i(TAG, "selectSearchShortcut: Clicked ${shortcut.name}'s alternative search engine check box")
    }

    fun toggleAutocomplete() {
        Log.i(TAG, "toggleAutocomplete: Trying to click the \"Autocomplete URLs\" toggle")
        autocompleteSwitchButton().click()
        Log.i(TAG, "toggleAutocomplete: Clicked the \"Autocomplete URLs\" toggle")
    }

    fun toggleShowSearchSuggestions() {
        Log.i(TAG, "toggleShowSearchSuggestions: Trying to click the \"Show search suggestions\" toggle")
        showSearchSuggestionSwitchButton().click()
        Log.i(TAG, "toggleShowSearchSuggestions: Clicked the \"Show search suggestions\" toggle")
    }

    fun toggleVoiceSearch() {
        Log.i(TAG, "toggleVoiceSearch: Trying to click the \"Show voice search\" toggle")
        voiceSearchSwitchButton().perform(click())
        Log.i(TAG, "toggleVoiceSearch: Clicked the \"Show voice search\" toggle")
    }

    fun toggleClipboardSuggestion() {
        Log.i(TAG, "toggleClipboardSuggestion: Trying to click the \"Show clipboard suggestions\" toggle")
        showClipboardSuggestionSwitch().click()
        Log.i(TAG, "toggleClipboardSuggestion: Clicked the \"Show clipboard suggestions\" toggle")
    }

    fun switchSearchHistoryToggle() {
        Log.i(TAG, "switchSearchHistoryToggle: Trying to click the \"Search browsing history\" toggle")
        searchHistorySwitchButton().click()
        Log.i(TAG, "switchSearchHistoryToggle: Clicked the \"Search browsing history\" toggle")
    }

    fun switchSearchBookmarksToggle() {
        Log.i(TAG, "switchSearchBookmarksToggle: Trying to click the \"Search bookmarks\" toggle")
        searchBookmarksSwitchButton().click()
        Log.i(TAG, "switchSearchBookmarksToggle: Clicked the \"Search bookmarks\" toggle")
    }

    fun switchShowSuggestionsInPrivateSessionsToggle() {
        Log.i(
            TAG,
            "switchShowSuggestionsInPrivateSessionsToggle: Trying to click the \"Show in private sessions\" check box",
        )
        showSuggestionsInPrivateModeSwitch().click()
        Log.i(TAG, "switchShowSuggestionsInPrivateSessionsToggle: Clicked the \"Show in private sessions\" check box")
    }

    fun openAddSearchEngineMenu() {
        Log.i(TAG, "openAddSearchEngineMenu: Trying to click the \"Add search engine\" button")
        addSearchEngineButton().click()
        Log.i(TAG, "openAddSearchEngineMenu: Clicked the \"Add search engine\" button")
    }

    fun verifyEngineListContains(searchEngineName: String, shouldExist: Boolean) =
        assertUIObjectExists(itemWithText(searchEngineName), exists = shouldExist)

    fun verifyDefaultSearchEngineSelected(searchEngineName: String) {
        Log.i(
            TAG,
            "verifyDefaultSearchEngineSelected: Trying to verify that $searchEngineName's radio button is checked",
        )
        defaultSearchEngineOption(searchEngineName).check(matches(isChecked(true)))
        Log.i(TAG, "verifyDefaultSearchEngineSelected: Verified that $searchEngineName's radio button is checked")
    }

    fun verifySaveSearchEngineButtonEnabled(enabled: Boolean) {
        Log.i(TAG, "verifySaveSearchEngineButtonEnabled: Trying to verify that the \"Save\" button is enabled")
        addSearchEngineSaveButton().check(matches(isEnabled(enabled)))
        Log.i(TAG, "verifySaveSearchEngineButtonEnabled: Verified that the \"Save\" button is enabled")
    }

    fun saveNewSearchEngine() {
        Log.i(TAG, "saveNewSearchEngine: Trying to perform \"Close soft keyboard\" action")
        closeSoftKeyboard()
        Log.i(TAG, "saveNewSearchEngine: Performed \"Close soft keyboard\" action")
        Log.i(TAG, "saveNewSearchEngine: Trying to click the \"Save\" button")
        addSearchEngineSaveButton().click()
        Log.i(TAG, "saveNewSearchEngine: Clicked the \"Save\" button")
    }

    fun typeCustomEngineDetails(engineName: String, engineURL: String) {
        try {
            Log.i(TAG, "typeCustomEngineDetails: Trying to clear the \"Search engine name\" text field")
            mDevice.findObject(By.res("$packageName:id/edit_engine_name")).clear()
            Log.i(TAG, "typeCustomEngineDetails: Cleared the \"Search engine name\" text field")
            Log.i(TAG, "typeCustomEngineDetails: Trying to set the \"Search engine name\" text field to: $engineName")
            mDevice.findObject(By.res("$packageName:id/edit_engine_name")).text = engineName
            Log.i(TAG, "typeCustomEngineDetails: The \"Search engine name\" text field text was set to: $engineName")
            assertUIObjectExists(itemWithResIdAndText("$packageName:id/edit_engine_name", engineName))
            Log.i(TAG, "typeCustomEngineDetails: Trying to clear the \"URL to use for search\" text field")
            mDevice.findObject(By.res("$packageName:id/edit_search_string")).clear()
            Log.i(TAG, "typeCustomEngineDetails: Cleared the \"URL to use for search\" text field")
            Log.i(TAG, "typeCustomEngineDetails: Trying to set the \"URL to use for search\" text field to: $engineURL")
            mDevice.findObject(By.res("$packageName:id/edit_search_string")).text = engineURL
            Log.i(TAG, "typeCustomEngineDetails: The \"URL to use for search\" text field text was set to: $engineURL")
            assertUIObjectExists(itemWithResIdAndText("$packageName:id/edit_search_string", engineURL))
        } catch (e: AssertionError) {
            Log.i(TAG, "typeCustomEngineDetails: AssertionError caught, executing fallback methods")
            Log.i(TAG, "typeCustomEngineDetails: Trying to clear the \"Search engine name\" text field")
            mDevice.findObject(By.res("$packageName:id/edit_engine_name")).clear()
            Log.i(TAG, "typeCustomEngineDetails: Cleared the \"Search engine name\" text field")
            Log.i(TAG, "typeCustomEngineDetails: Trying to set the \"Search engine name\" text field to: $engineName")
            mDevice.findObject(By.res("$packageName:id/edit_engine_name")).setText(engineName)
            Log.i(TAG, "typeCustomEngineDetails: The \"Search engine name\" text field text was set to: $engineName")
            assertUIObjectExists(itemWithResIdAndText("$packageName:id/edit_engine_name", engineName))
            Log.i(TAG, "typeCustomEngineDetails: Trying to clear the \"URL to use for search\" text field")
            mDevice.findObject(By.res("$packageName:id/edit_search_string")).clear()
            Log.i(TAG, "typeCustomEngineDetails: Cleared the \"URL to use for search\" text field")
            Log.i(TAG, "typeCustomEngineDetails: Trying to set the \"URL to use for search\" text field to: $engineURL")
            mDevice.findObject(By.res("$packageName:id/edit_search_string")).setText(engineURL)
            Log.i(TAG, "typeCustomEngineDetails: The \"URL to use for search\" text field text was set to: $engineURL")
            assertUIObjectExists(itemWithResIdAndText("$packageName:id/edit_search_string", engineURL))
        }
    }

    fun typeSearchEngineSuggestionString(searchSuggestionString: String) {
        Log.i(TAG, "typeSearchEngineSuggestionString: Trying to click the \"Search suggestion API URL\" text field")
        onView(withId(R.id.edit_suggest_string)).click()
        Log.i(TAG, "typeSearchEngineSuggestionString: Clicked the \"Search suggestion API URL\" text field")
        Log.i(TAG, "typeSearchEngineSuggestionString: Trying to clear the \"Search suggestion API URL\" text field")
        onView(withId(R.id.edit_suggest_string)).perform(clearText())
        Log.i(TAG, "typeSearchEngineSuggestionString: Cleared the \"Search suggestion API URL\" text field")
        Log.i(
            TAG,
            "typeSearchEngineSuggestionString: Trying to type $searchSuggestionString in the \"Search suggestion API URL\" text field",
        )
        onView(withId(R.id.edit_suggest_string)).perform(typeText(searchSuggestionString))
        Log.i(
            TAG,
            "typeSearchEngineSuggestionString: Typed $searchSuggestionString in the \"Search suggestion API URL\" text field",
        )
    }

    // Used in the non-Compose Default search engines menu
    fun openEngineOverflowMenu(searchEngineName: String) {
        Log.i(
            TAG,
            "openEngineOverflowMenu: Waiting for $waitingTimeShort ms for $searchEngineName's three dot button to exist",
        )
        threeDotMenu(searchEngineName).waitForExists(waitingTimeShort)
        Log.i(
            TAG,
            "openEngineOverflowMenu: Waited for $waitingTimeShort ms for $searchEngineName's three dot button to exist",
        )
        Log.i(TAG, "openEngineOverflowMenu: Trying to click $searchEngineName's three dot button")
        threeDotMenu(searchEngineName).click()
        Log.i(TAG, "openEngineOverflowMenu: Clicked $searchEngineName's three dot button")
    }

    // Used in the composable Manage shortcuts menu, otherwise the overflow menu is not visible
    fun openCustomShortcutOverflowMenu(testRule: ComposeTestRule, searchEngineName: String) {
        Log.i(TAG, "openCustomShortcutOverflowMenu: Trying to click $searchEngineName's three dot button")
        testRule.onNode(overflowMenuWithSiblingText(searchEngineName)).performClick()
        Log.i(TAG, "openCustomShortcutOverflowMenu: Clicked $searchEngineName's three dot button")
    }

    fun clickEdit() {
        Log.i(TAG, "clickEdit: Trying to click the \"Edit\" button")
        onView(withText("Edit")).click()
        Log.i(TAG, "clickEdit: Clicked the \"Edit\" button")
    }

    // Used in the Default search engine menu
    fun clickDeleteSearchEngine() {
        Log.i(TAG, "clickDeleteSearchEngine: Trying to click the \"Delete\" button")
        mDevice.findObject(UiSelector().textContains(getStringResource(R.string.search_engine_delete))).click()
        Log.i(TAG, "clickDeleteSearchEngine: Clicked the \"Delete\" button")
    }

    // Used in the composable Manage search shortcuts menu, otherwise the overflow menu is not visible
    fun clickDeleteSearchEngine(testRule: ComposeTestRule) {
        Log.i(TAG, "clickDeleteSearchEngine: Trying to click the \"Delete\" button")
        testRule.onNodeWithText("Delete").performClick()
        Log.i(TAG, "clickDeleteSearchEngine: Clicked the \"Delete\" button")
    }

    fun saveEditSearchEngine() {
        Log.i(TAG, "saveEditSearchEngine: Trying to click the \"Save\" button")
        onView(withId(R.id.save_button)).click()
        Log.i(TAG, "saveEditSearchEngine: Clicked the \"Save\" button")
    }

    fun verifyInvalidTemplateSearchStringFormatError() {
        Log.i(TAG, "verifyInvalidTemplateSearchStringFormatError: Trying to perform \"Close soft keyboard\" action")
        closeSoftKeyboard()
        Log.i(TAG, "verifyInvalidTemplateSearchStringFormatError: Performed \"Close soft keyboard\" action")
        Log.i(
            TAG,
            "verifyInvalidTemplateSearchStringFormatError: Trying to verify that the \"Check that search string matches Example format\" error message is displayed",
        )
        onView(withText(getStringResource(R.string.search_add_custom_engine_error_missing_template)))
            .check(matches(isDisplayed()))
        Log.i(
            TAG,
            "verifyInvalidTemplateSearchStringFormatError: Verified that the \"Check that search string matches Example format\" error message is displayed",
        )
    }

    fun verifyErrorConnectingToSearchString(searchEngineName: String) {
        Log.i(TAG, "verifyErrorConnectingToSearchString: Trying to perform \"Close soft keyboard\" action")
        closeSoftKeyboard()
        Log.i(TAG, "verifyErrorConnectingToSearchString: Performed \"Close soft keyboard\" action")
        Log.i(
            TAG,
            "verifyErrorConnectingToSearchString: Trying to verify that the \"Error connecting to $searchEngineName\" error message is displayed",
        )
        assertUIObjectExists(
            itemWithResIdContainingText("$packageName:id/textinput_error", "Error connecting to “$searchEngineName”")
        )
        Log.i(
            TAG,
            "verifyErrorConnectingToSearchString: Verified that the \"Error connecting to $searchEngineName\" error message is displayed",
        )
    }

    fun verifyTheHomeScreenWidgetOption() {
        Log.i(
            TAG,
            "verifyTheHomeScreenWidgetOption: Trying to verify that the \"Add search widget to Home screen\" option is visible",
        )
        homeScreenWidgetOption().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyTheHomeScreenWidgetOption: Verified that the \"Add search widget to Home screen\" option is visible",
        )
    }

    fun clickAddSearchWidgetToHomeScreenMenuItem() {
        Log.i(
            TAG,
            "clickAddSearchWidgetToHomeScreenMenuItem: Trying to click the \"Add search widget to Home screen\" menu item",
        )
        homeScreenWidgetOption().click()
        Log.i(
            TAG,
            "clickAddSearchWidgetToHomeScreenMenuItem: Clicked the \"Add search widget to Home screen\" menu item",
        )
        mDevice.waitForIdle()
    }

    fun verifySearchWidgetExistsOnHomeScreen(): Boolean {
        Log.i(TAG, "verifySearchWidgetExistsOnHomeScreen: Pressing device home button")
        mDevice.pressHome()

        Log.i(TAG, "verifySearchWidgetExistsOnHomeScreen: Waiting for $waitingTime ms for search widget to exist")
        mDevice.wait(Until.findObject(desc(getStringResource(R.string.widget_picket_description))), waitingTime)
        Log.i(TAG, "verifySearchWidgetExistsOnHomeScreen: Waited for $waitingTime ms for search widget to exist")

        Log.i(TAG, "verifySearchWidgetExistsOnHomeScreen: Trying to verify search widget exists")
        val exists = mDevice.hasObject(desc(getStringResource(R.string.widget_picket_description)))
        Log.i(TAG, "verifySearchWidgetExistsOnHomeScreen: Verified search widget exists: $exists")

        return exists
    }

    class Transition {
        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "goBack: Waited for device to be idle")
            Log.i(TAG, "goBack: Trying to click the navigate up button")
            goBackButton().perform(click())
            Log.i(TAG, "goBack: Clicked the navigate up button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun goBackToSearchSettings(
            interact: SettingsSubMenuSearchRobot.() -> Unit
        ): SettingsSubMenuSearchRobot.Transition {
            Log.i(TAG, "goBackToSearchSettings: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "goBackToSearchSettings: Waited for device to be idle")
            Log.i(TAG, "goBackToSearchSettings: Trying to click the navigate up button")
            goBackButton().perform(click())
            Log.i(TAG, "goBackToSearchSettings: Clicked the navigate up button")

            SettingsSubMenuSearchRobot().interact()
            return SettingsSubMenuSearchRobot.Transition()
        }

        fun clickCustomSearchStringLearnMoreLink(
            composeTestRule: ComposeTestRule,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickCustomSearchStringLearnMoreLink: Trying to click the \"Search string URL\" learn more link",
            )
            onView(withId(R.id.custom_search_engines_learn_more)).click()
            Log.i(TAG, "clickCustomSearchStringLearnMoreLink: Clicked the \"Search string URL\" learn more link")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickCustomSearchSuggestionsLearnMoreLink(
            composeTestRule: ComposeTestRule,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(
                TAG,
                "clickCustomSearchSuggestionsLearnMoreLink: Trying to click the \"Search suggestions API\" learn more link",
            )
            onView(withId(R.id.custom_search_suggestions_learn_more)).click()
            Log.i(
                TAG,
                "clickCustomSearchSuggestionsLearnMoreLink: Clicked the \"Search suggestions API\" learn more link",
            )

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickSearchWidgetOnHomeScreen(
            composeTestRule: ComposeTestRule,
            interact: SearchRobot.() -> Unit,
        ): SearchRobot.Transition {
            Log.i(TAG, "clickSearchWidgetOnHomeScreen: Trying to click voice search on the search widget")
            mDevice
                .findObject(UiSelector().description(getStringResource(R.string.search_widget_voice)))
                .clickAndWaitForNewWindow(waitingTime)
            Log.i(TAG, "clickSearchWidgetOnHomeScreen: Clicked the voice search on the search widget")
            mDevice.waitForIdle()

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }
    }
}

fun settingsSubMenuSearch(interact: SettingsSubMenuSearchRobot.() -> Unit): SettingsSubMenuSearchRobot.Transition {
    SettingsSubMenuSearchRobot().interact()
    return SettingsSubMenuSearchRobot.Transition()
}

/**
 * Matches search shortcut items inside the 'Manage search shortcuts' menu
 *
 * @property name, of type String, should be the name of the search engine.
 * @property isChecked, of type Boolean, should show if the checkbox is expected to be checked.
 */
class EngineShortcut(
    val name: String,
    val isChecked: Boolean = true,
)

private val defaultSearchEngineHeader = onView(withText("Default search engine"))

private val manageSearchShortcutsHeader = onView(withText("Manage alternative search engines"))

private fun searchHistorySwitchButton(): ViewInteraction {
    Log.i(TAG, "searchHistorySwitchButton: Trying to perform scroll action to the \"Search browsing history\" option")
    onView(withId(preferenceR.id.recycler_view))
        .perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(hasDescendant(withText("Search browsing history")))
        )
    Log.i(TAG, "searchHistorySwitchButton: Performed scroll action to the \"Search browsing history\" option")
    return onView(withText("Search browsing history"))
}

private fun searchBookmarksSwitchButton(): ViewInteraction {
    Log.i(TAG, "searchBookmarksSwitchButton: Trying to perform scroll action to the \"Search bookmarks\" option")
    onView(withId(preferenceR.id.recycler_view))
        .perform(RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(hasDescendant(withText("Search bookmarks"))))
    Log.i(TAG, "searchBookmarksSwitchButton: Performed scroll action to the \"Search bookmarks\" option")
    return onView(withText("Search bookmarks"))
}

private fun searchSyncedTabsSwitchButton(): ViewInteraction {
    Log.i(TAG, "searchSyncedTabsSwitchButton: Trying to perform scroll action to the \"Search synced tabs\" option")
    onView(withId(preferenceR.id.recycler_view))
        .perform(RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(hasDescendant(withText("Search synced tabs"))))
    Log.i(TAG, "searchSyncedTabsSwitchButton: Performed scroll action to the \"Search synced tabs\" option")
    return onView(withText("Search synced tabs"))
}

private fun voiceSearchSwitchButton(): ViewInteraction {
    Log.i(TAG, "voiceSearchSwitchButton: Trying to perform scroll action to the \"Show voice search\" option")
    onView(withId(preferenceR.id.recycler_view))
        .perform(RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(hasDescendant(withText("Show voice search"))))
    Log.i(TAG, "voiceSearchSwitchButton: Performed scroll action to the \"Show voice search\" option")
    return onView(withText("Show voice search"))
}

private fun autocompleteSwitchButton(): ViewInteraction {
    Log.i(TAG, "autocompleteSwitchButton: Trying to perform scroll action to the \"Autocomplete URLs\" option")
    onView(withId(preferenceR.id.recycler_view))
        .perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                hasDescendant(withText(getStringResource(R.string.preferences_enable_autocomplete_urls)))
            )
        )
    Log.i(TAG, "autocompleteSwitchButton: Performed scroll action to the \"Autocomplete URLs\" option")
    return onView(withText(getStringResource(R.string.preferences_enable_autocomplete_urls)))
}

private fun showSearchSuggestionSwitchButton(): ViewInteraction {
    Log.i(
        TAG,
        "showSearchSuggestionSwitchButton: Trying to perform scroll action to the \"Show search suggestions\" option",
    )
    onView(withId(preferenceR.id.recycler_view))
        .perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(hasDescendant(withText("Show search suggestions")))
        )
    Log.i(TAG, "showSearchSuggestionSwitchButton: Performed scroll action to the \"Show search suggestions\" option")
    return onView(withText("Show search suggestions"))
}

private fun showClipboardSuggestionSwitch(): ViewInteraction {
    Log.i(
        TAG,
        "showClipboardSuggestionSwitch: Trying to perform scroll action to the \"Show clipboard suggestions\" option",
    )
    onView(withId(preferenceR.id.recycler_view))
        .perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                hasDescendant(withText(getStringResource(R.string.preferences_show_clipboard_suggestions)))
            )
        )
    Log.i(TAG, "showClipboardSuggestionSwitch: Performed scroll action to the \"Show clipboard suggestions\" option")
    return onView(withText(getStringResource(R.string.preferences_show_clipboard_suggestions)))
}

private fun showSuggestionsInPrivateModeSwitch(): ViewInteraction {
    Log.i(
        TAG,
        "showSuggestionsInPrivateModeSwitch: Trying to perform scroll action to the \"Show in private sessions\" option",
    )
    onView(withId(preferenceR.id.recycler_view))
        .perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                hasDescendant(withText(getStringResource(R.string.preferences_show_search_suggestions_in_private)))
            )
        )
    Log.i(TAG, "showSuggestionsInPrivateModeSwitch: Performed scroll action to the \"Show in private sessions\" option")
    return onView(withText(getStringResource(R.string.preferences_show_search_suggestions_in_private)))
}

private fun goBackButton() = onView(CoreMatchers.allOf(withContentDescription("Navigate up")))

private fun addSearchEngineButton() = mDevice.findObject(UiSelector().text("Add search engine"))

private fun addSearchEngineSaveButton() = onView(withId(R.id.save_button))

private fun threeDotMenu(searchEngineName: String) =
    mDevice.findObject(UiSelector().text(searchEngineName)).getFromParent(UiSelector().description("More options"))

private fun defaultSearchEngineOption(searchEngineName: String) =
    onView(
        allOf(
            withId(R.id.radio_button),
            hasSibling(withText(searchEngineName)),
            isDescendantOfA(
                withTagValue(CoreMatchers.equalTo(getStringResource(R.string.pref_key_search_engine_list)))
            ),
        )
    )

private fun overflowMenuWithSiblingText(text: String): SemanticsMatcher =
    hasAnySibling(hasText(text)) and hasContentDescription("More options")

private fun homeScreenWidgetOption() =
    onView(CoreMatchers.allOf(withText(R.string.preferences_search_widget_in_home_screen)))
