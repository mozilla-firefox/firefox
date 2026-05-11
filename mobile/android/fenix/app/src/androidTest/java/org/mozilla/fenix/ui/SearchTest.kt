/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.content.Context
import android.hardware.camera2.CameraManager
import androidx.core.net.toUri
import androidx.test.espresso.Espresso
import androidx.test.espresso.Espresso.openActionBarOverflowOrOptionsMenu
import androidx.test.filters.SdkSuppress
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import org.junit.Assume
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.AppAndSystemHelper
import org.mozilla.fenix.helpers.AppAndSystemHelper.denyPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.denyPermissionAndDontAskAgainButton
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.verifyKeyboardVisibility
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createTabItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.setCustomSearchEngine
import org.mozilla.fenix.helpers.SearchMockServerRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.longTapSelectItem
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.TestHelper.waitUntilSnackbarGone
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickContextMenuItem
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.longClickPageObject
import org.mozilla.fenix.ui.robots.multipleSelectionToolbar
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.searchScreen
import org.mozilla.fenix.ui.robots.settingsTurnOnSyncScreen
import java.util.Locale
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

/**
 *  Tests for verifying the search fragment
 *
 *  Including:
 * - Verify the toolbar, awesomebar, and shortcut bar are displayed
 * - Select shortcut button
 * - Select scan button
 *
 */

class SearchTest {
    private val queryString: String = "firefox"
    private val generalEnginesList = listOf("DuckDuckGo", "Google", "Bing")
    private val scanButtonEnginesList = listOf("DuckDuckGo", "Bing")
    private val topicEnginesList = listOf("Wikipedia (en)")
    private val firefoxSuggestHeader = getStringResource(R.string.firefox_suggest_header)

    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    @get:Rule(order = 1)
    val composeTestRule = AndroidComposeTestRuleV2(
        HomeActivityTestRule(
            skipOnboarding = true,
            isPocketEnabled = false,
            isRecentTabsFeatureEnabled = false,
            isWallpaperOnboardingEnabled = false,
            isLocationPermissionEnabled = SitePermissionsRules.Action.BLOCKED,
            // workaround for toolbar at top position by default
            // remove with https://bugzilla.mozilla.org/show_bug.cgi?id=1917640
            shouldUseBottomToolbar = true,
        ),
    ) { it.activity }

    @get:Rule(order = 2)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    @get:Rule
    val searchMockServerRule = SearchMockServerRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154189
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifySearchBarItemsTest() {
        navigationToolbar(composeTestRule) {
            verifyDefaultSearchEngine("Google")
            verifySearchBarPlaceholder("Search or enter address")
        }.clickURLBar {
            verifyKeyboardVisibility(isExpectedToBeVisible = true)
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder("Search or enter address")
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            verifyScanButton(isDisplayed = true)
            typeSearch("mozilla ")
            waitForAppWindowToBeUpdated()
            verifyScanButton(isDisplayed = false)
            verifyVoiceSearchButton(isDisplayed = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154190
    @Test
    fun verifySearchSelectorMenuItemsTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            verifySearchToolbar(isDisplayed = true)
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154194
    @Test
    fun verifySearchPlaceholderForGeneralDefaultSearchEnginesTest() {
        generalEnginesList.forEach {
            searchScreen(composeTestRule) {
                clickSearchSelectorButton()
            }.clickSearchEngineSettings {
                openDefaultSearchEngineMenu()
                changeDefaultSearchEngine(it)
                exitMenu()
            }
            navigationToolbar(composeTestRule) {
                verifySearchBarPlaceholder("Search or enter address")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154195
    @Test
    fun verifySearchPlaceholderForNotDefaultGeneralSearchEnginesTest() {
        val generalEnginesList = listOf("DuckDuckGo", "Bing")

        generalEnginesList.forEach {
            searchScreen(composeTestRule) {
                clickSearchSelectorButton()
                selectTemporarySearchMethod(it)
                verifySearchBarPlaceholder("Search the web")
            }.dismissSearchBar {}
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154196
    @Test
    fun verifySearchPlaceholderForTopicSpecificSearchEnginesTest() {
        topicEnginesList.forEach {
            homeScreen(composeTestRule) {
            }.openSearch {
                clickSearchSelectorButton()
                selectTemporarySearchMethod(it)
                verifySearchBarPlaceholder("Enter search terms")
            }.dismissSearchBar {}
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1059459
    @SmokeTest
    @Test
    fun verifyQRScanningCameraAccessDialogTest() {
        val cameraManager = TestHelper.appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        homeScreen(composeTestRule) {
        }.openSearch {
            waitForAppWindowToBeUpdated()
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            clickScanButton()
            denyPermission()
            clickScanButton()
            denyPermissionAndDontAskAgainButton()
        }.dismissSearchBar {
        }.openThreeDotMenu {
        }.clickSignInToSyncButton {
        }
        settingsTurnOnSyncScreen(composeTestRule) {
            clickReadyToScanButton()
            clickDismissPermissionRequiredDialog()
            clickReadyToScanButton()
        }.clickGoToPermissionsSettings {
            openAppSystemPermissionsSettings()
            switchAppPermissionSystemSetting("Camera", "Allow")
        }.goBackToSignInToSync(composeTestRule) {
            clickReadyToScanButton()
            verifyQRScannerIsOpen()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/235397
    @SmokeTest
    @Test
    fun scanQRCodeToOpenAWebpageTest() {
        val cameraManager = TestHelper.appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        homeScreen(composeTestRule) {
        }.openSearch {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("DuckDuckGo")
            clickScanButton()
            grantSystemPermission()
            verifyScannerOpen()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154191
    @Test
    fun verifyScanButtonAvailableOnlyForGeneralSearchEnginesTest() {
        scanButtonEnginesList.forEach {
            searchScreen(composeTestRule) {
                clickSearchSelectorButton()
                selectTemporarySearchMethod(it)
                verifyScanButton(isDisplayed = true)
            }.dismissSearchBar {}
        }

        topicEnginesList.forEach {
            searchScreen(composeTestRule) {
                clickSearchSelectorButton()
                selectTemporarySearchMethod(it)
                verifyScanButton(isDisplayed = false)
            }.dismissSearchBar {}
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/235395
    // Verifies a temporary change of search engine from the Search shortcut menu
    @SmokeTest
    @Test
    fun searchEnginesCanBeChangedTemporarilyFromSearchSelectorMenuTest() {
        (generalEnginesList + topicEnginesList).forEach {
            searchScreen(composeTestRule) {
                clickSearchSelectorButton()
                verifySearchShortcutList(it, isSearchEngineDisplayed = true)
                selectTemporarySearchMethod(it)
                verifySearchEngineIcon(it)
            }.submitQuery("mozilla ") {
                verifyUrl("mozilla")
            }.goToHomescreen {}
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/233589
    @Test
    fun defaultSearchEnginesCanBeSetFromSearchSelectorMenuTest() {
        searchScreen(composeTestRule) {
            clickSearchSelectorButton()
        }.clickSearchEngineSettings {
            verifyToolbarText("Search")
            openDefaultSearchEngineMenu()
            changeDefaultSearchEngine("DuckDuckGo")
            exitMenu()
        }
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            verifyUrl(queryString)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/522918
    @Test
    fun verifyClearSearchButtonTest() {
        homeScreen(composeTestRule) {
        }.openSearch {
            typeSearch(queryString)
            clickClearButton()
            verifySearchBarPlaceholder("Search or enter address")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1623441
    @Ignore("Failing: https://bugzilla.mozilla.org/show_bug.cgi?id=1930244")
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
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            Espresso.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592229
    @Test
    fun verifyAPageIsAddedToASearchGroupOnlyOnceTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url
        val originPageUrl =
            "http://localhost:${searchMockServerRule.server.port}/pages/searchResults.html?search=firefox".toUri()
        val searchEngineName = "TestSearchEngine"
        // setting our custom mockWebServer search URL
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            Espresso.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            Espresso.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
            Espresso.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            verifyTestPageUrl(firstPageUrl)
            verifyTestPageUrl(secondPageUrl)
            verifyTestPageUrl(originPageUrl)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1591782
    @Ignore("Failing due to known bug, see https://bugzilla.mozilla.org/show_bug.cgi?id=1807294")
    @Test
    fun searchGroupIsGeneratedWhenNavigatingInTheSameTabTest() {
        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            clickPageObject(composeTestRule, MatcherHelper.itemContainingText("Link 1"))
            waitForPageToLoad()
            Espresso.pressBack()
            clickPageObject(composeTestRule, MatcherHelper.itemContainingText("Link 2"))
            waitForPageToLoad()
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1591781
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
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in private tab")
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in private tab")
        }.openTabDrawer(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openPrivateTab(0) {
        }.openTabDrawer(composeTestRule) {
        }.openPrivateTab(1) {
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            togglePrivateBrowsingModeOnOff()
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = false, searchTerm = queryString, groupSize = 3)
        }.openThreeDotMenu {
        }.clickHistoryButton {
            verifyHistoryItemExists(shouldExist = false, item = "3 sites")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592269
    @SmokeTest
    @Test
    fun deleteIndividualHistoryItemsFromSearchGroupTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url
        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            TestHelper.mDevice.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            clickDeleteHistoryButton(firstPageUrl.toString())
            longTapSelectItem(secondPageUrl)
            multipleSelectionToolbar(composeTestRule) {
                openActionBarOverflowOrOptionsMenu(composeTestRule.activity)
                clickMultiSelectionDelete()
                waitUntilSnackbarGone()
            }
            exitMenu()
        }
        homeScreen(composeTestRule) {
            waitForAppWindowToBeUpdated()
            // checking that the group is removed when only 1 item is left
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = false, searchTerm = queryString, groupSize = 1)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592242
    @Test
    fun deleteSearchGroupFromHomeScreenTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url
        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            TestHelper.mDevice.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            clickDeleteAllHistoryButton()
            confirmDeleteAllHistory()
            verifySnackBarText(expectedText = "Group deleted")
            verifyHistoryItemExists(shouldExist = false, firstPageUrl.toString())
        }.goBack {}
        homeScreen(composeTestRule) {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = false, queryString, groupSize = 3)
        }.openThreeDotMenu {
        }.clickHistoryButton {
            verifySearchGroupDisplayed(shouldBeDisplayed = false, queryString, groupSize = 3)
            verifyEmptyHistoryView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592235
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=2011676"])
    @Test
    fun openAPageFromHomeScreenSearchGroupTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url

        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            TestHelper.mDevice.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
            waitForPageToLoad()
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(
                shouldBeDisplayed = true,
                searchTerm = queryString,
                groupSize = 3,
            )
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
        }.openWebsiteFromSearchGroup(firstPageUrl) {
            verifyUrl(firstPageUrl.toString())
        }.goToHomescreen {
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            longTapSelectItem(firstPageUrl)
            longTapSelectItem(secondPageUrl)
            openActionBarOverflowOrOptionsMenu(composeTestRule.activity)
        }
        multipleSelectionToolbar(composeTestRule) {
        }.clickOpenNewTab {
            verifyNormalBrowsingButtonIsSelected()
        }.openThreeDotMenu {
        }.closeAllTabs {
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            longTapSelectItem(firstPageUrl)
            longTapSelectItem(secondPageUrl)
            openActionBarOverflowOrOptionsMenu(composeTestRule.activity)
            multipleSelectionToolbar(composeTestRule) {
            }.clickOpenPrivateTab {
                verifyPrivateBrowsingButtonIsSelected()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592238
    @Test
    fun shareAPageFromHomeScreenSearchGroupTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url
        // setting our custom mockWebServer search URL
        val searchEngineName = "TestSearchEngine"
        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)

        // Performs a search and opens 2 dummy search results links to create a search group
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 1"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(firstPageUrl.toString())
            TestHelper.mDevice.pressBack()
            longClickPageObject(composeTestRule, MatcherHelper.itemWithText("Link 2"))
            clickContextMenuItem("Open link in new tab")
            clickSnackbarButton(composeTestRule, "SWITCH")
            verifyUrl(secondPageUrl.toString())
        }.openTabDrawer(composeTestRule) {
        }.openThreeDotMenu {
        }.closeAllTabs {
            verifyRecentlyVisitedSearchGroupDisplayed(shouldBeDisplayed = true, searchTerm = queryString, groupSize = 3)
        }.openRecentlyVisitedSearchGroupHistoryList(queryString) {
            TestHelper.longTapSelectItem(firstPageUrl)
        }

        multipleSelectionToolbar(composeTestRule) {
            clickShareHistoryButton()
            verifyShareOverlay()
            verifyShareTabFavicon()
            verifyShareTabTitle()
            verifyShareTabUrl()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1232633
    // Default search code for Google-US
    @Test
    fun defaultSearchCodeGoogleUS() {
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            verifyPageContent("google")
        }.openThreeDotMenu {
        }.clickHistoryButton {
            // Full URL no longer visible in the nav bar, so we'll check the history record
            // A search group is sometimes created when searching with Google (probably redirects)
            try {
                verifyHistoryItemExists(shouldExist = true, Constants.searchEngineCodes["Google"]!!)
            } catch (e: AssertionError) {
                openSearchGroup(queryString)
                verifyHistoryItemExists(shouldExist = true, Constants.searchEngineCodes["Google"]!!)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1232637
    // Default search code for Bing-US
    @Test
    fun defaultSearchCodeBingUS() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            changeDefaultSearchEngine("Bing")
            exitMenu()
        }
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
           waitForPageToLoad()
        }.openThreeDotMenu {
        }.clickHistoryButton {
            // Full URL no longer visible in the nav bar, so we'll check the history record
            // A search group is sometimes created when searching with Bing (probably redirects)
            try {
                verifyHistoryItemExists(shouldExist = true, Constants.searchEngineCodes["Bing"]!!)
            } catch (e: AssertionError) {
                openSearchGroup(queryString)
                verifyHistoryItemExists(shouldExist = true, Constants.searchEngineCodes["Bing"]!!)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1232638
    // Default search code for DuckDuckGo-US
    @Test
    fun defaultSearchCodeDuckDuckGoUS() {
        homeScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickSettingsButton {
        }.openSearchSubMenu {
            openDefaultSearchEngineMenu()
            changeDefaultSearchEngine("DuckDuckGo")
            exitMenu()
        }
        homeScreen(composeTestRule) {
        }.openSearch {
        }.submitQuery(queryString) {
            verifyPageContent("duckduckgo")
        }.openThreeDotMenu {
        }.clickHistoryButton {
            // Full URL no longer visible in the nav bar, so we'll check the history record
            // A search group is sometimes created when searching with DuckDuckGo
            try {
                verifyHistoryItemExists(shouldExist = true, item = Constants.searchEngineCodes["DuckDuckGo"]!!)
            } catch (e: AssertionError) {
                openSearchGroup(queryString)
                verifyHistoryItemExists(shouldExist = true, item = Constants.searchEngineCodes["DuckDuckGo"]!!)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1850517
    // Test that verifies the Firefox Suggest results in a general search context
    @Test
    fun verifyFirefoxSuggestHeaderForBrowsingDataSuggestionsTest() {
        val firstPage = searchMockServerRule.server.getGenericAsset(1)
        val secondPage = searchMockServerRule.server.getGenericAsset(2)

        createBookmarkItem(secondPage.url.toString(), secondPage.title, 1u)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstPage.url) {
            verifyTabCounter("1")
        }.openThreeDotMenu {
        }.clickBookmarksButton {
            verifyBookmarkTitle(secondPage.title)
        }.goBackToHomeScreen {
        }.openSearch {
            typeSearch("generic")
            verifyTheSuggestionsHeader(firefoxSuggestHeader)
            verifySearchSuggestionsAreDisplayed(
                searchSuggestions = arrayOf(
                    firstPage.url.toString(),
                    secondPage.url.toString(),
                ),
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154215
    @SmokeTest
    @Test
    fun verifyHistorySearchWithBrowsingHistoryTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1)
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2)

        MockBrowserDataHelper.createHistoryItem(firstPageUrl.url.toString())
        MockBrowserDataHelper.createHistoryItem(secondPageUrl.url.toString())

        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod(searchEngineName = "History")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            typeSearch(searchTerm = "generic")
            verifyTypedToolbarText("generic", exists = true)
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154197
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyTabsSearchItemsTest() {
        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("Tabs")
            verifyScanButton(isDisplayed = false)
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder(searchHint = "Search tabs")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154198
    @Test
    fun verifyTabsSearchWithoutOpenTabsTest() {
        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod(searchEngineName = "Tabs")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            verifySearchBarPlaceholder("Search tabs")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154199
    @SmokeTest
    @Test
    fun verifyTabsSearchWithOpenTabsTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1)
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2)

        createTabItem(firstPageUrl.url.toString())
        createTabItem(secondPageUrl.url.toString())

        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod(searchEngineName = "Tabs")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            typeSearch(searchTerm = "generic")
            verifyTypedToolbarText("generic", exists = true)
            verifyTheSuggestionsHeader(firefoxSuggestHeader)
            verifySearchSuggestionsAreDisplayed(
                searchSuggestions = arrayOf(
                    firstPageUrl.url.toString(),
                    secondPageUrl.url.toString(),
                ),
            )
        }.clickSearchSuggestion(firstPageUrl.url.toString()) {
            verifyTabCounter("2")
        }.openTabDrawer(composeTestRule) {
            verifyOpenTabsOrder(position = 1, title = firstPageUrl.url.toString())
            verifyOpenTabsOrder(position = 2, title = secondPageUrl.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154203
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyBookmarksSearchItemsTest() {
        navigationToolbar(composeTestRule) {
        }.clickSearchSelectorButton {
            selectTemporarySearchMethod("Bookmarks")
            verifySearchBarPlaceholder("Search bookmarks")
            verifyKeyboardVisibility(isExpectedToBeVisible = true)
            verifyScanButton(isDisplayed = false)
            verifyVoiceSearchButton(isDisplayed = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154204
    @Test
    fun verifyBookmarkSearchWithNoBookmarksTest() {
        navigationToolbar(composeTestRule) {
        }.clickSearchSelectorButton {
            selectTemporarySearchMethod("Bookmarks")
            typeSearch("test")
            verifySuggestionsAreNotDisplayed("test")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154206
    @Test
    fun verifyBookmarksSearchForBookmarkedItemsTest() {
        createBookmarkItem(url = "https://bookmarktest1.com", title = "Test1", position = 1u)
        createBookmarkItem(url = "https://bookmarktest2.com", title = "Test2", position = 2u)

        navigationToolbar(composeTestRule) {
        }.clickSearchSelectorButton {
            selectTemporarySearchMethod("Bookmarks")
            typeSearch("test")
            verifyTheSuggestionsHeader(firefoxSuggestHeader)
            verifySearchSuggestionsAreDisplayed(
                searchSuggestions = arrayOf(
                    "Test1",
                    "https://bookmarktest1.com/",
                    "Test2",
                    "https://bookmarktest2.com/",
                ),
            )
        }.dismissSearchBar {
        }.openSearch {
            typeSearch("mozilla ")
            verifySuggestionsAreNotDisplayed("Test1", "Test2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154212
    @SdkSuppress(minSdkVersion = 34)
    @Test
    fun verifyHistorySearchItemsTest() {
        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod("History")
            verifyScanButton(isDisplayed = false)
            verifyVoiceSearchButton(isDisplayed = true)
            verifySearchBarPlaceholder(searchHint = "Search history")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154213
    @Test
    fun verifyHistorySearchWithoutBrowsingHistoryTest() {
        navigationToolbar(composeTestRule) {
        }.clickURLBar {
            clickSearchSelectorButton()
            selectTemporarySearchMethod(searchEngineName = "History")
            typeSearch(searchTerm = "Mozilla")
            verifySuggestionsAreNotDisplayed("Mozilla")
            clickClearButton()
            verifySearchBarPlaceholder("Search history")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2230212
    @SmokeTest
    @Test
    fun searchHistoryNotRememberedInPrivateBrowsingTest() {
        TestHelper.appContext.settings().shouldShowSearchSuggestionsInPrivate = true

        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1)
        val searchEngineName = "TestSearchEngine"

        setCustomSearchEngine(searchMockServerRule.server, searchEngineName)
        createBookmarkItem(firstPageUrl.url.toString(), firstPageUrl.title, 1u)

        navigationToolbar(composeTestRule) {
        }.clickURLBar {
        }.submitQuery("test page 1") {
        }.goToHomescreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar(composeTestRule) {
        }.clickURLBar {
        }.submitQuery("test page 2") {
        }.openNavigationToolbar {
        }.clickURLBar {
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
            verifySearchSuggestionsCount(numberOfSuggestions = 4, searchTerm = "test page")
            verifySuggestionsAreNotDisplayed(
                searchSuggestions = arrayOf(
                    "test page 2",
                ),
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1232631
    // Expected for app language set to Arabic
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=2004855"])
    fun verifySearchEnginesFunctionalityUsingRTLLocaleTest() {
        val arabicLocale = Locale.Builder().setLanguage("ar").setRegion("AR").build()

        AppAndSystemHelper.runWithAppLocaleChanged(arabicLocale, composeTestRule.activityRule) {
            homeScreen(composeTestRule) {
            }.openSearch {
                verifyTranslatedNavigationToolbarHint("ابحث أو أدخِل عنوانا")
                clickSearchSelectorButton()
                verifySearchShortcutList(
                    "Google",
                    "Bing",
                    "DuckDuckGo",
                    "ويكيبيديا (ar)",
                    isSearchEngineDisplayed = true,
                )
                selectTemporarySearchMethod("ويكيبيديا (ar)")
            }.submitQuery("firefox") {
                verifyUrl("firefox")
            }
        }
    }
}
