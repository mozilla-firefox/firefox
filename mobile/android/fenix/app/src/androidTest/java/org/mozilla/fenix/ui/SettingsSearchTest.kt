/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.test.espresso.Espresso.pressBack
import androidx.test.filters.SdkSuppress
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithAppLocaleChanged
import org.mozilla.fenix.helpers.DataGenerationHelper.setTextToClipBoard
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper.addCustomSearchEngine
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createHistoryItem
import org.mozilla.fenix.helpers.SearchMockServerRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.ui.robots.EngineShortcut
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.searchScreen
import java.util.Locale
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

class SettingsSearchTest {
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    private val defaultSearchEngineList =
        listOf(
            "Bing",
            "DuckDuckGo",
            "Google",
        )

    @get:Rule(order = 1)
    val composeTestRule = AndroidComposeTestRuleV2(
        HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
    ) { it.activity }

    @get:Rule(order = 2)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    @get:Rule
    val searchMockServerRule = SearchMockServerRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203333
    @Test
    fun verifySearchSettingsMenuItemsTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            verifyToolbarText("Search")
            verifySearchEnginesSectionHeader()
            verifyDefaultSearchEngineHeader()
            verifyDefaultSearchEngineSummary("Google")
            verifyManageSearchShortcutsHeader()
            verifyManageShortcutsSummary()
            verifyAddressBarSectionHeader()
            verifyAutocompleteURlsIsEnabled(true)
            verifyShowClipboardSuggestionsEnabled(true)
            verifySearchBrowsingHistoryEnabled(true)
            verifySearchBookmarksEnabled(true)
            verifySearchSyncedTabsEnabled(true)
            verifyVoiceSearchEnabled(true)
            verifyShowSearchSuggestionsEnabled(true)
            verifyShowSearchSuggestionsInPrivateEnabled(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203307
    @Test
    fun verifyDefaultSearchEnginesSettingsItemsTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            verifyDefaultSearchEngineHeader()
            openDefaultSearchEngineMenu()
            verifyToolbarText("Default search engine")
            verifyDefaultSearchEngineList()
            verifyDefaultSearchEngineSelected("Google")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203308
    @SmokeTest
    @Test
    fun verifyTheDefaultSearchEngineCanBeChangedTest() {
        // Goes through the settings and changes the default search engine, then verifies it has changed.
        defaultSearchEngineList.forEach {
            homeScreen(composeTestRule) {
            }.openThreeDotMenu {
            }.clickSettingsButton {
            }.openSearchSubMenu {
                openDefaultSearchEngineMenu()
                changeDefaultSearchEngine(it)
                exitMenu()
            }
            searchScreen(composeTestRule) {
                verifySearchEngineIcon(it)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/233586
    @Test
    fun verifyEnabledUrlAutocompleteToggleTest() {
        // Currently part of an experiment https://bugzilla.mozilla.org/show_bug.cgi?id=1842106
        // Check if "Top domain" suggestions for the address bar's autocomplete are enabled
        if (FxNimbus.features.suggestShippedDomains.value().enabled) {
            // If true it will use the hardcoded list of "top domain" suggestions for the address bar's autocomplete suggestions
            homeScreen(composeTestRule) {
            }.openSearch {
                typeSearch("mo")
                verifyTypedToolbarText("monster.com", exists = true)
                typeSearch("moz")
                verifyTypedToolbarText("mozilla.org", exists = true)
            }
        } else {
            // The suggestions for the address bar's autocomplete will take use of the user's local browsing history and bookmarks
            createHistoryItem("https://github.com/mozilla-mobile/fenix")
            createBookmarkItem("https://github.com/mozilla-mobile/focus-android", "focus-android", 1u)

            homeScreen(composeTestRule) {
            }.openSearch {
                typeSearch("moz")
                // "Top domain" suggestions from the address bar's autocomplete are disabled, "moz" shouldn't autocomplete to mozilla.org
                verifyTypedToolbarText("mozilla.org", exists = false)
                // The address bar's autocomplete should take use of the browsing history
                // Autocomplete with the history items url
                typeSearch("github.com/mozilla-mobile/f")
                verifyTypedToolbarText("github.com/mozilla-mobile/fenix", exists = true)
                // The address bar's autocomplete should also take use of the saved bookmarks
                // Autocomplete with the bookmarked items url
                typeSearch("github.com/mozilla-mobile/fo")
                verifyTypedToolbarText("github.com/mozilla-mobile/focus-android", exists = true)
                // It should not autocomplete with links that are not part of browsing history or bookmarks
                typeSearch("github.com/mozilla-mobile/fi")
                verifyTypedToolbarText("github.com/mozilla-mobile/firefox-android", exists = false)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2660692
    @Test
    fun verifyDisabledUrlAutocompleteToggleTest() {
        // Currently part of an experiment https://bugzilla.mozilla.org/show_bug.cgi?id=1842106
        // Check if "Top domain" suggestions for the address bar's autocomplete are enabled
        if (FxNimbus.features.suggestShippedDomains.value().enabled) {
            // If true it will use the hardcoded list of "top domain" suggestions for the address bar's autocomplete suggestions
            homeScreen(composeTestRule) {
            }.openThreeDotMenu {
            }.clickSettingsButton {
            }.openSearchSubMenu {
                toggleAutocomplete()
            }.goBack {
            }.goBack(composeTestRule) {
            }.openSearch {
                typeSearch("moz")
                verifyTypedToolbarText("moz", exists = true)
                verifyTypedToolbarText("mozilla.org", exists = false)
            }
        } else {
            // The suggestions for the address bar's autocomplete will take use of the user's local browsing history and bookmarks
            createHistoryItem("https://github.com/mozilla-mobile/fenix")
            createBookmarkItem("https://github.com/mozilla-mobile/focus-android", "focus-android", 1u)

            homeScreen(composeTestRule) {
            }.openThreeDotMenu {
            }.clickSettingsButton {
            }.openSearchSubMenu {
                toggleAutocomplete()
            }.goBack {
            }.goBack(composeTestRule) {
            }.openSearch {
                // Having the setting disabled, it should not autocomplete anymore with the history items url
                typeSearch("github.com/mozilla-mobile/f")
                verifyTypedToolbarText("github.com/mozilla-mobile/fenix", exists = false)
                // Having the setting disabled, it should not autocomplete anymore with the bookmarked items url
                typeSearch("github.com/mozilla-mobile/fo")
                verifyTypedToolbarText("github.com/mozilla-mobile/focus-android", exists = false)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/361817
    @Test
    fun disableSearchBrowsingHistorySuggestionsToggleTest() {
        val websiteURL = mockWebServer.getGenericAsset(1).url.toString()

        createHistoryItem(websiteURL)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            switchSearchHistoryToggle()
            exitMenu()
        }

        homeScreen(composeTestRule) {
        }.openSearch {
            typeSearch("test")
            verifySuggestionsAreNotDisplayed(
                "Firefox Suggest",
                websiteURL,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/412926
    @Test
    fun disableSearchBookmarksToggleTest() {
        val website = mockWebServer.getGenericAsset(1)

        createBookmarkItem(website.url.toString(), website.title, 1u)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            switchSearchBookmarksToggle()
            // We want to avoid confusion between history and bookmarks searches,
            // so we'll disable this too.
            switchSearchHistoryToggle()
            exitMenu()
        }

        homeScreen(composeTestRule) {
        }.openSearch {
            typeSearch("test")
            verifySuggestionsAreNotDisplayed(
                "Firefox Suggest",
                website.title,
            )
        }.dismissSearchBar {
            verifyHomeComponent()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203309
    // Verifies setting as default a customized search engine name and URL
    @SmokeTest
    @Test
    fun verifyCustomSearchEngineCanBeAddedFromSearchEngineMenuTest() {
        val customSearchEngine = object {
            val title = "TestSearchEngine"
            val url = "http://localhost:${searchMockServerRule.server.port}/searchResults.html?search=%s"
        }

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            openAddSearchEngineMenu()
            verifySaveSearchEngineButtonEnabled(false)
            typeCustomEngineDetails(customSearchEngine.title, customSearchEngine.url)
            verifySaveSearchEngineButtonEnabled(true)
            saveNewSearchEngine()
            verifyEngineListContains(customSearchEngine.title, shouldExist = true)
            openEngineOverflowMenu(customSearchEngine.title)
            pressBack()
            changeDefaultSearchEngine(customSearchEngine.title)
            pressBack()
            openManageShortcutsMenu()
            verifyEngineListContains(customSearchEngine.title, shouldExist = false)
            pressBack()
        }.goBack {
            verifySettingsOptionSummary("Search", customSearchEngine.title)
        }.goBack(composeTestRule) {
        }.openSearch {
            verifySearchEngineIcon(customSearchEngine.title)
            clickSearchSelectorButton()
            verifySearchShortcutList(customSearchEngine.title, isSearchEngineDisplayed = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203335
    @Test
    fun addCustomSearchEngineToManageShortcutsListTest() {
        val customSearchEngine = object {
            val title = "TestSearchEngine"
            val url = "http://localhost:${searchMockServerRule.server.port}/searchResults.html?search=%s"
        }

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openManageShortcutsMenu()
            openAddSearchEngineMenu()
            typeCustomEngineDetails(customSearchEngine.title, customSearchEngine.url)
            saveNewSearchEngine()
            verifyEngineListContains(customSearchEngine.title, shouldExist = true)
            pressBack()
            openDefaultSearchEngineMenu()
            verifyEngineListContains(customSearchEngine.title, shouldExist = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203343
    @Test
    fun verifyLearnMoreLinksFromAddSearchEngineSectionTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            openAddSearchEngineMenu()
        }.clickCustomSearchStringLearnMoreLink(composeTestRule) {
            verifyCustomSearchEngineLearnMoreURL()
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            openAddSearchEngineMenu()
        }.clickCustomSearchSuggestionsLearnMoreLink(composeTestRule) {
            verifyCustomSearchEngineLearnMoreURL()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203310
    @Test
    fun editCustomSearchEngineTest() {
        val customSearchEngine = object {
            val title = "TestSearchEngine"
            val url = "http://localhost:${searchMockServerRule.server.port}/searchResults.html?search=%s"
            val newTitle = "NewEngineTitle"
        }

        addCustomSearchEngine(searchMockServerRule.server, customSearchEngine.title)
        restartApp(composeTestRule.activityRule)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            verifyEngineListContains(customSearchEngine.title, shouldExist = true)
            openEngineOverflowMenu(customSearchEngine.title)
            clickEdit()
            typeCustomEngineDetails(customSearchEngine.newTitle, customSearchEngine.url)
            saveEditSearchEngine()
            verifyEngineListContains(customSearchEngine.newTitle, shouldExist = true)
            pressBack()
            openManageShortcutsMenu()
            verifyEngineListContains(customSearchEngine.newTitle, shouldExist = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203312
    @Test
    fun verifyErrorMessagesForInvalidSearchEngineUrlsTest() {
        val customSearchEngine = object {
            val title = "TestSearchEngine"
            val badTemplateUrl = "http://localhost:${searchMockServerRule.server.port}/searchResults.html?search="
            val typoUrl = "http://local:${searchMockServerRule.server.port}/searchResults.html?search=%s"
            val goodUrl = "http://localhost:${searchMockServerRule.server.port}/searchResults.html?search=%s"
        }

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            openAddSearchEngineMenu()
            typeCustomEngineDetails(customSearchEngine.title, customSearchEngine.badTemplateUrl)
            saveNewSearchEngine()
            verifyInvalidTemplateSearchStringFormatError()
            typeCustomEngineDetails(customSearchEngine.title, customSearchEngine.goodUrl)
            typeSearchEngineSuggestionString(customSearchEngine.badTemplateUrl)
            saveNewSearchEngine()
            verifyInvalidTemplateSearchStringFormatError()
            typeCustomEngineDetails(customSearchEngine.title, customSearchEngine.typoUrl)
            saveNewSearchEngine()
            verifyEngineListContains(customSearchEngine.title, shouldExist = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203313
    @Test
    fun deleteCustomSearchEngineTest() {
        val customSearchEngineTitle = "TestSearchEngine"

        addCustomSearchEngine(mockWebServer, searchEngineName = customSearchEngineTitle)
        restartApp(composeTestRule.activityRule)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            verifyEngineListContains(customSearchEngineTitle, shouldExist = true)
            openEngineOverflowMenu(customSearchEngineTitle)
            clickDeleteSearchEngine()
            verifyEngineListContains(customSearchEngineTitle, shouldExist = false)
            verifyDefaultSearchEngineSelected("Google")
            pressBack()
            openManageShortcutsMenu()
            verifyEngineListContains(customSearchEngineTitle, shouldExist = false)
            exitMenu()
        }
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
            verifySearchShortcutList(customSearchEngineTitle, isSearchEngineDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203339
    @Test
    fun deleteCustomSearchShortcutTest() {
        val customSearchEngineTitle = "TestSearchEngine"

        addCustomSearchEngine(mockWebServer, searchEngineName = customSearchEngineTitle)
        restartApp(composeTestRule.activityRule)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openManageShortcutsMenu()
            verifyEngineListContains(customSearchEngineTitle, shouldExist = true)
            openCustomShortcutOverflowMenu(composeTestRule, customSearchEngineTitle)
            clickDeleteSearchEngine(composeTestRule)
            verifyEngineListContains(customSearchEngineTitle, shouldExist = false)
            pressBack()
            openDefaultSearchEngineMenu()
            verifyEngineListContains(customSearchEngineTitle, shouldExist = false)
            exitMenu()
        }
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
            verifySearchShortcutList(customSearchEngineTitle, isSearchEngineDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/233588
    // Test running on beta/release builds in CI:
    // caution when making changes to it, so they don't block the builds
    // Goes through the settings and changes the search suggestion toggle, then verifies it changes.
    @SmokeTest
    @Test
    fun verifyShowSearchSuggestionsToggleTest() {
        searchScreen(composeTestRule) {
            // The Google related suggestions aren't always displayed on cold run
            // Bugzilla ticket: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            typeSearch("mozilla ")
            verifySearchSuggestionsAreDisplayed("mozilla")
        }.dismissSearchBar {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            toggleShowSearchSuggestions()
        }.goBack {
        }.goBack(composeTestRule) {
        }.openSearch {
            // The Google related suggestions aren't always displayed on cold run
            // Bugzilla ticket: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            typeSearch("mozilla")
            verifySuggestionsAreNotDisplayed("mozilla")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/464420
    // Tests the "Don't allow" option from private mode search suggestions onboarding dialog
    @Test
    fun doNotAllowSearchSuggestionsInPrivateBrowsingTest() {
        homeScreen(composeTestRule) {
            togglePrivateBrowsingModeOnOff()
        }.openSearch {
            typeSearch("mozilla")
            verifyAllowSuggestionsInPrivateModeDialog()
            denySuggestionsInPrivateMode()
            verifySuggestionsAreNotDisplayed("mozilla firefox")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1957063
    // Tests the "Allow" option from private mode search suggestions onboarding dialog
    @Test
    fun allowSearchSuggestionsInPrivateBrowsingTest() {
        homeScreen(composeTestRule) {
            togglePrivateBrowsingModeOnOff()
        }.openSearch {
            typeSearch("mozilla")
            verifyAllowSuggestionsInPrivateModeDialog()
            allowSuggestionsInPrivateMode()
            verifySearchSuggestionsAreDisplayed("mozilla")
        }.dismissSearchBar {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            switchShowSuggestionsInPrivateSessionsToggle()
        }.goBack {
        }.goBack(composeTestRule) {
        }.openSearch {
            typeSearch("mozilla")
            verifySuggestionsAreNotDisplayed("mozilla")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/888673
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyShowVoiceSearchToggleTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            verifyVoiceSearchButton(isDisplayed = true)
            startVoiceSearch()
            closeVoiceSearchDialog()
        }.dismissSearchBar {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            toggleVoiceSearch()
            exitMenu()
        }
        homeScreen(composeTestRule) {
        }.openSearch {
            verifyVoiceSearchButton(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/412927
    @Test
    fun verifyShowClipboardSuggestionsToggleTest() {
        val link = "https://www.mozilla.org/en-US/"
        setTextToClipBoard(appContext, link)

        homeScreen(composeTestRule) {
        }.openSearch {
        }
        navigationToolbar(composeTestRule) {
            verifyClipboardSuggestionsAreDisplayed(true)
        }.visitLinkFromClipboard {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
        }.openTabDrawer(composeTestRule) {
        }.openNewTab {
        }
        navigationToolbar(composeTestRule) {
            verifyClipboardSuggestionsAreDisplayed(shouldBeDisplayed = true)
        }
        searchScreen(composeTestRule) {
        }.dismissSearchBar {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            verifyShowClipboardSuggestionsEnabled(true)
            toggleClipboardSuggestion()
            verifyShowClipboardSuggestionsEnabled(false)
            exitMenu()
        }
        homeScreen(composeTestRule) {
        }.openTabDrawer {
        }.openNewTab {
        }
        navigationToolbar(composeTestRule) {
            verifyClipboardSuggestionsAreDisplayed(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2233337
    @Test
    fun verifyTheSearchEnginesListsRespectTheLocaleTest() {
        runWithAppLocaleChanged(Locale.CHINA, composeTestRule.activityRule) {
            navigationToolbar(composeTestRule) {
            }.clickSearchSelectorButton {
                verifySearchShortcutList(
                    "Google",
                    "百度",
                    "Bing",
                    "DuckDuckGo",
                    isSearchEngineDisplayed = true,
                )
            }.dismissSearchBar {}
        }

        runWithAppLocaleChanged(Locale.FRENCH, composeTestRule.activityRule) {
            navigationToolbar(composeTestRule) {
            }.clickSearchSelectorButton {
                clickSearchSelectorButton()
                verifySearchShortcutList(
                    "Google",
                    "Bing",
                    "DuckDuckGo",
                    "Qwant",
                    "Wikipédia (fr)",
                    isSearchEngineDisplayed = true,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203334
    @Test
    fun verifyManageSearchShortcutsSettingsItemsTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openManageShortcutsMenu()
            verifyToolbarText("Manage alternative search engines")
            verifyEnginesShortcutsListHeader()
            verifyManageShortcutsList(composeTestRule)
            verifySearchShortcutChecked(
                EngineShortcut(name = "Bing", checkboxIndex = 1, isChecked = true),
                EngineShortcut(name = "DuckDuckGo", checkboxIndex = 4, isChecked = true),
                EngineShortcut(name = "Wikipedia (en)", checkboxIndex = 7, isChecked = true),
                EngineShortcut(name = "Reddit", checkboxIndex = 10, isChecked = false),
                EngineShortcut(name = "YouTube", checkboxIndex = 13, isChecked = false),
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2203340
    @SmokeTest
    @Test
    fun verifySearchShortcutChangesAreReflectedInSearchSelectorMenuTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openManageShortcutsMenu()
            selectSearchShortcut(EngineShortcut(name = "Reddit", checkboxIndex = 10))
            selectSearchShortcut(EngineShortcut(name = "YouTube", checkboxIndex = 13))
            exitMenu()
        }
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
            verifySearchShortcutList("YouTube", isSearchEngineDisplayed = true)
            verifySearchShortcutList("Reddit", isSearchEngineDisplayed = true)
        }
    }
}
