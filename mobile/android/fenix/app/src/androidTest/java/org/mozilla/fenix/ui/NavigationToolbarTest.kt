/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("DEPRECATION")

package org.mozilla.fenix.ui

import android.content.Context
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.hardware.camera2.CameraManager
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.Espresso
import androidx.test.filters.SdkSuppress
import androidx.test.rule.ActivityTestRule
import org.junit.Assume
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.setScreenOrientation
import org.mozilla.fenix.helpers.AppAndSystemHelper.verifyKeyboardVisibility
import org.mozilla.fenix.helpers.DataGenerationHelper.createCustomTabIntent
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createHistoryItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.MockBrowserDataHelper.setCustomSearchEngine
import org.mozilla.fenix.helpers.SearchMockServerRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.htmlControlsFormAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.verifyDarkThemeApplied
import org.mozilla.fenix.helpers.TestHelper.verifyLightThemeApplied
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickContextMenuItem
import org.mozilla.fenix.ui.robots.customTabScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.longClickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.searchScreen

/**
 *  Tests for verifying basic functionality of browser navigation and page related interactions
 *
 *  Including:
 *  - Visiting a URL
 *  - Back and Forward navigation
 *  - Refresh
 *  - Find in page
 */

class NavigationToolbarTest {
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    private val customTabActionButton = "CustomActionButton"

    private val bookmarkFolderName = "My Folder"

    private val queryString: String = "firefox"

    private val defaultSearchEngineList =
        listOf(
            "Bing",
            "DuckDuckGo",
            "Google",
        )

    private val generalEnginesList = listOf("DuckDuckGo", "Google", "Bing")
    private val topicEnginesList = listOf("Wikipedia (en)")

    private val firefoxSuggestHeader = getStringResource(R.string.firefox_suggest_header)

    private fun getUiTheme(): Boolean {
        val mode =
            composeTestRule.activity.resources?.configuration?.uiMode?.and(Configuration.UI_MODE_NIGHT_MASK)

        return when (mode) {
            Configuration.UI_MODE_NIGHT_YES -> true // dark theme is set
            Configuration.UI_MODE_NIGHT_NO -> false // dark theme is not set, using light theme
            else -> false // default option is light theme
        }
    }

    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule(
                isPWAsPromptEnabled = false,
                isWallpaperOnboardingEnabled = false,
                isOpenInAppBannerEnabled = false,
                isMicrosurveyEnabled = false,
                isTermsOfServiceAccepted = true,
            ),
        ) { it.activity }

    @get:Rule
    val intentReceiverActivityTestRule = ActivityTestRule(
        IntentReceiverActivity::class.java,
        true,
        false,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @get:Rule
    val searchMockServerRule = SearchMockServerRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135074
    @SmokeTest
    @Test
    fun verifySecurePageSecuritySubMenuTest() {
        val defaultWebPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val defaultWebPageTitle = "Login_form"

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(
            defaultWebPage.toUri(),
        ) {
            verifyPageContent("Login Form")
        }.openSiteSecuritySheet {
            verifyQuickActionSheet(defaultWebPage, true)
            openSecureConnectionSubMenu(true)
            verifySecureConnectionSubMenu(defaultWebPageTitle, defaultWebPage, true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135075
    @SmokeTest
    @Test
    fun verifyInsecurePageSecuritySubMenuTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }.openSiteSecuritySheet {
            verifyQuickActionSheet(defaultWebPage.url.toString(), false)
            openSecureConnectionSubMenu(false)
            verifySecureConnectionSubMenu(
                defaultWebPage.title,
                defaultWebPage.url.toString(),
                false,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135076
    @SmokeTest
    @Test
    @SkipLeaks
    fun verifyClearCookiesFromQuickSettingsTest() {
        val loginPage = "https://mozilla-mobile.github.io/testapp/loginForm"
        val originWebsite = "mozilla-mobile.github.io"

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(loginPage.toUri()) {
            waitForPageToLoad(waitingTimeLong)
        }.openSiteSecuritySheet {
            clickQuickActionSheetClearSiteData()
            verifyClearSiteDataPrompt(originWebsite)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135034
    @SmokeTest
    @Test
    fun verifySearchForBookmarkedItemsTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.htmlControlsFormAsset

        val newFolder = generateBookmarkFolder(title = bookmarkFolderName, position = null)
        createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
        createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickBookmarksButton {
        }.clickSearchButton {
            // Search for a valid term
            typeSearch(firstWebPage.title)
            verifySearchSuggestionsAreDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
            // Search for invalid term
            typeSearch("Android")
            verifySuggestionsAreNotDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135037
    @SmokeTest
    @Test
    fun verifyTheCustomTabsMainMenuItemsTest() {
        val customMenuItem = "TestMenuItem"
        val customTabPage = mockWebServer.getGenericAsset(1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                customTabPage.url.toString(),
                customMenuItem,
            ),
        )

        customTabScreen(composeTestRule) {
            verifyCustomTabCloseButton()
        }.openMainMenu {
            verifyCustomTabsMainMenuItems(customMenuItem, true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135106
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=2021581")
    @SmokeTest
    @Test
    fun verifyShowSearchSuggestionsToggleTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            // The Google related suggestions aren't always displayed on cold run
            // Bugzilla ticket: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            typeSearch("mozilla ")
            verifySearchSuggestionsAreDisplayed("mozilla firefox")
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
            verifySuggestionsAreNotDisplayed()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135025
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
                verifySearchEngineIcon(name = it)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135009
    @SmokeTest
    @Test
    fun scanQRCodeToOpenAWebpageTest() {
        val cameraManager =
            TestHelper.appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        homeScreen(composeTestRule) {
        }.openSearch {
            clickScanButton()
            grantSystemPermission()
            verifyScannerOpen()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135051
    @SmokeTest
    @Test
    fun verifyHistorySearchWithBrowsingHistoryTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1)
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2)

        createHistoryItem(firstPageUrl.url.toString())
        createHistoryItem(secondPageUrl.url.toString())

        homeScreen(composeTestRule) {
        }.openSearch {
            clickSearchSelectorButton()
            selectTemporarySearchMethod(searchEngineName = "History")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            typeSearch(searchTerm = "generic")
            // verifyTypedToolbarText("generic", exists = true)
            verifySearchSuggestionsAreDisplayed(
                searchSuggestions = arrayOf(
                    firstPageUrl.url.toString(),
                    secondPageUrl.url.toString(),
                ),
            )
        }.clickSearchSuggestion(firstPageUrl.url.toString()) {
            verifyUrl(firstPageUrl.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135109
    // Verifies a temporary change of search engine from the Search shortcut menu
    @SmokeTest
    @Test
    fun searchEnginesCanBeChangedTemporarilyFromSearchSelectorMenuTest() {
        (generalEnginesList + topicEnginesList).forEach {
            homeScreen(composeTestRule) {
            }.openSearch {
                clickSearchSelectorButton()
                verifySearchShortcutList(it, isSearchEngineDisplayed = true)
                selectTemporarySearchMethod(it)
                verifySearchEngineIcon(it)
            }.submitQuery("mozilla ") {
                verifyUrl("mozilla")
            }.goToHomescreen {
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135027
    @SmokeTest
    @Test
    fun searchHistoryNotRememberedInPrivateBrowsingTest() {
        TestHelper.appContext.settings().shouldShowSearchSuggestionsInPrivate = true

        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1)
        val searchEngineName = "TestSearchEngine"

        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)
        createBookmarkItem(firstPageUrl.url.toString(), firstPageUrl.title, 1u)

        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery("test page 1") {
        }.goToHomescreen {
            togglePrivateBrowsingModeOnOff()
        }.openSearch {
        }.submitQuery("test page 2") {
        }.openSearch {
            typeSearch(searchTerm = "test page")
            verifyTheSuggestionsHeader(firefoxSuggestHeader)
            verifyTheSuggestionsHeader("TestSearchEngine search")
            verifySearchSuggestionsAreDisplayed(
                searchSuggestions = arrayOf(
                    "test page 1",
                    firstPageUrl.url.toString(),
                ),
            )
            // 2 search engine suggestions and 2 browser suggestions (1 history, 1 bookmark)
            verifySearchSuggestionsCount(
                numberOfSuggestions = 4,
                searchTerm = "test page",
            )
            verifySuggestionsAreNotDisplayed(
                searchSuggestions = arrayOf(
                    "test page 2",
                ),
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135012
    @Ignore("Failing: https://bugzilla.mozilla.org/show_bug.cgi?id=2014561")
    @SmokeTest
    @Test
    fun searchResultsOpenedInNewTabsGenerateSearchGroupsTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url
        val searchEngineName = "TestSearchEngine"
        // setting our custom mockWebServer search URL
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            Espresso.pressBack()
            longClickPageObject(composeTestRule, itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
        }.goToHomescreen {
            verifyRecentlyVisitedSearchGroupDisplayed(
                shouldBeDisplayed = true,
                searchTerm = queryString,
                groupSize = 3,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135013
    @SmokeTest
    @Test
    fun searchGroupIsNotGeneratedForLinksOpenedInPrivateTabsTest() {
        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, itemWithText("Link 1"))
            clickContextMenuItem("Open link in private tab")
            longClickPageObject(composeTestRule, itemWithText("Link 2"))
            clickContextMenuItem("Open link in private tab")
        }.openTabDrawer(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openPrivateTab(0) {
        }.openTabDrawer(composeTestRule) {
        }.openPrivateTab(1) {
        }.goToHomescreen(isPrivateModeEnabled = true) {
            togglePrivateBrowsingModeOnOff()
            verifyRecentlyVisitedSearchGroupDisplayed(
                shouldBeDisplayed = false,
                searchTerm = queryString,
                groupSize = 3,
            )
        }.openThreeDotMenu {
        }.clickHistoryButton {
            verifyHistoryItemExists(shouldExist = false, item = "3 sites")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135071
    // Swipes the nav bar left/right to switch between tabs
    @SmokeTest
    @Test
    @SkipLeaks
    fun swipeToSwitchTabTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.getGenericAsset(2)

        // Disable the back gesture from the edge of the screen on the device.
        enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }.openTabDrawer(composeTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebPage.url.toString()) {
            swipeNavBarRight(secondWebPage.url.toString())
            verifyUrl(firstWebPage.url.toString())
            swipeNavBarLeft(firstWebPage.url.toString())
            verifyUrl(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135000
    @Test
    fun changeThemeOfTheAppTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyThemes()
            selectDarkMode()
            verifyDarkThemeApplied(getUiTheme())
            selectLightMode()
            verifyLightThemeApplied(getUiTheme())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135001
    @Test
    fun setToolbarPositionTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            verifyAddressBarPositionPreference("Top")
            clickBottomToolbarToggle()
            verifyAddressBarPositionPreference("Bottom")
        }.goBack {
        }.goBack(composeTestRule) {
            verifyToolbarPosition(bottomPosition = true)
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickTopToolbarToggle()
            verifyAddressBarPositionPreference("Top")
            exitMenu()
        }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135099
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
            createBookmarkItem(
                "https://github.com/mozilla-mobile/focus-android",
                "focus-android",
                1u,
            )

            homeScreen(composeTestRule) {
            }.openSearch {
                typeSearch("moz")
                // "Top domain" suggestions from the address bar's autocomplete are disabled, "moz" shouldn't autocomplete to mozilla.org
                verifyTypedToolbarText("mozilla.org", exists = false)
                // The address bar's autocomplete should take use of the browsing history
                // Autocomplete with the history items url
                typeSearch("github.com/mozilla-mobile/f")
                verifyTypedToolbarText(
                    "github.com/mozilla-mobile/fenix",
                    exists = true,
                )
                // The address bar's autocomplete should also take use of the saved bookmarks
                // Autocomplete with the bookmarked items url
                typeSearch("github.com/mozilla-mobile/fo")
                verifyTypedToolbarText(
                    "github.com/mozilla-mobile/focus-android",
                    exists = true,
                )
                // It should not autocomplete with links that are not part of browsing history or bookmarks
                typeSearch("github.com/mozilla-mobile/fi")
                verifyTypedToolbarText(
                    "github.com/mozilla-mobile/firefox-android",
                    exists = false,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135102
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135103
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
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135105
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135107
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135006
    @Test
    fun verifyClearSearchButtonTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            typeSearch(queryString)
            clickClearButton()
            verifySearchBarPlaceholder("Search or enter address")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135044
    @Test
    fun verifySearchForHistoryItemsTest() {
        val firstWebPage = mockWebServer.getGenericAsset(1)
        val secondWebPage = mockWebServer.htmlControlsFormAsset

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(secondWebPage.url) {
        }.openThreeDotMenu {
        }.clickHistoryButton {
        }.clickSearchButton {
            // Search for a valid term
            typeSearch(firstWebPage.title)
            verifySearchSuggestionsAreDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
            clickClearButton()
            // Search for invalid term
            typeSearch("Android")
            verifySuggestionsAreNotDisplayed(firstWebPage.url.toString())
            verifySuggestionsAreNotDisplayed(secondWebPage.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135049
    @Test
    fun verifyHistorySearchWithoutBrowsingHistoryTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("History")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            verifySearchBarPlaceholder("Search history")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135015
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifySearchBarItemsTest() {
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
        }.clickURLBar {
            verifyKeyboardVisibility(isExpectedToBeVisible = true)
            verifyScanButton(isDisplayed = true)
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder("Search or enter address")
            typeSearch("mozilla ")
            verifyScanButton(isDisplayed = false)
            verifyVoiceSearchButton(isDisplayed = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135016
    @Test
    fun verifySearchSelectorMenuItemsTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            clickSearchSelectorButton()
            verifySearchShortcutList(
                *generalEnginesList.toTypedArray(),
                *topicEnginesList.toTypedArray(),
                "Bookmarks",
                "Tabs",
                "History",
                "Search settings",
                isSearchEngineDisplayed = true,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135021
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyTabsSearchItemsTest() {
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("Tabs")
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder("Search tabs")
            verifyScanButton(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135048
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyHistorySearchItemsTest() {
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("History")
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder("Search history")
            verifyScanButton(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135036
    @Test
    fun verifyCustomTabViewItemsTest() {
        val customTabPage = mockWebServer.getGenericAsset(1)

        intentReceiverActivityTestRule.launchActivity(
            createCustomTabIntent(
                pageUrl = customTabPage.url.toString(),
                customActionButtonDescription = customTabActionButton,
            ),
        )

        customTabScreen(composeTestRule) {
            verifyCustomTabCloseButton()
            verifyCustomTabsSiteInfoButton()
            verifyCustomTabToolbarTitle(customTabPage.title)
            verifyCustomTabUrl(customTabPage.url.toString())
            verifyCustomTabActionButton(customTabActionButton)
            verifyMainMenuButton()
            clickCustomTabCloseButton()
        }
        homeScreen(composeTestRule) {
            verifyHomeScreenAppBarItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135066
    @Test
    fun verifyTheToolbarItemsTest() {
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
        homeScreen(composeTestRule) {
        }.togglePrivateBrowsingMode()
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
            verifyTheTabCounter("0", isPrivateBrowsingEnabled = true)
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135067
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            verifyTabCounter("1")
        }
        navigationToolbar(composeTestRule) {
            verifyTheNewTabButton()
        }.clickTheNewTabButton {
        }.submitQuery(secondPage.url.toString()) {
            verifyTabCounter("2")
        }.goToHomescreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            verifyTabCounter("1", isPrivateBrowsingEnabled = true)
        }
        navigationToolbar(composeTestRule) {
            verifyTheNewTabButton(isPrivateModeEnabled = true)
        }.clickTheNewTabButton(isPrivateModeEnabled = true) {
        }.submitQuery(secondPage.url.toString()) {
            verifyTabCounter("2", isPrivateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333206
    @SmokeTest
    @Test
    fun verifyHomepageItemsWithTabStripTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }

        navigationToolbar(composeTestRule) {
            verifyNavBarPositionWithTabStripEnabled(true)
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton()
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333193
    @SmokeTest
    @Test
    fun verifyTheTabStripUITest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
        }

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
            verifyUrl(defaultWebPage.url.toString())
            verifyETPShieldIconIsDisplayed(composeTestRule)
        }

        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }

        navigationToolbar(composeTestRule) {
            verifyTheTabStripOpenTab("Test_Page_1")
            verifyTheTabStripCloseTabButton("Test_Page_1")
            verifyNavBarPositionWithTabStripEnabled(true)
            verifyTheNavigationBarAddBookmarkButton()
            verifyTheNavigationBarShareButton()
            verifyTheNewTabButton(false)
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333195
    @SmokeTest
    @Test
    fun verifyTheNewTabButtonWithTabStripEnabledTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
        }

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyTabCounter("1")
        }
        navigationToolbar(composeTestRule) {
            verifyTheNewTabButton(false)
        }.clickTheNewTabButton(false) {
            verifySearchBarPlaceholder("Search or enter address")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333195
    @SmokeTest
    @Test
    fun verifyTabsTrayWithTabStripEnabledTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
            navigationToolbar(composeTestRule) {
            }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            }.openTabDrawer(composeTestRule) {
                verifyExistingOpenTabs(defaultWebPage.title)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333173
    @Test
    fun verifyHomepageItemsWithTabStripLandscapeTest() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }

        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)

        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
            verifyTheTabCounter("0")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3333201
    @Test
    fun verifyTheTabStripUILandscapeTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openCustomizeSubMenu {
            clickShowTabBarToggle()
            scrollToExpandedToolbarOption()
            selectExpandedToolbarLayout()
        }.goBack {
        }.goBack(composeTestRule) {
        }

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }

        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE)
        browserScreen(composeTestRule) {
            verifyUrl(defaultWebPage.url.toString())
            verifyETPShieldIconIsDisplayed(composeTestRule)
        }
        homeScreen(composeTestRule) {
            verifyToolbarPosition(bottomPosition = false)
        }
        navigationToolbar(composeTestRule) {
            verifyTheTabStripOpenTab("Test_Page_1")
            verifyTheTabStripCloseTabButton("Test_Page_1")
            verifyTheBackButton()
            verifyTheForwardButton()
            verifyTheRefreshButton()
            verifyTheNewTabButton(false)
            verifyTheTabCounter("1")
            verifyTheMainMenuButton()
        }
        setScreenOrientation(composeTestRule, ActivityInfo.SCREEN_ORIENTATION_PORTRAIT)
    }
}
