/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import android.Manifest
import android.content.Context
import android.hardware.camera2.CameraManager
import mozilla.components.feature.contextmenu.R as contextMenuR
import org.junit.Assume
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.SearchMockServerRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.core.WaitPolicy
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.RequiresDeniedRuntimePermission
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.pageObjects.HistorySearchGroupPage
import org.mozilla.fenix.ui.efficiency.pageObjects.SystemSettingsPage
import org.mozilla.fenix.ui.efficiency.selectors.BookmarksSelectors
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HistorySelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSearchDefaultSearchEngineSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsTurnOnSyncSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SystemSettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

// Pocket is disabled class-wide to match the legacy SearchTest rule, which runs every test in this class
// with isPocketEnabled = false. It matters for the search-group tests: with Pocket on, "Recently visited"
// can sit below the fold, and mozVerify does not scroll — so a "group is displayed" assertion fails and, far
// worse, a "group is gone" assertion passes for the wrong reason.
class SearchTest : BaseTest(LaunchConfig(isPocketEnabled = false)) {

    private val queryString = "firefox"
    private val generalEnginesList = listOf("DuckDuckGo", "Google", "Bing")
    private val topicEnginesList = listOf("Wikipedia (en)")
    private val openLinkInNewTab = getStringResource(contextMenuR.string.mozac_feature_contextmenu_open_link_in_new_tab)
    private val openLinkInPrivateTab =
        getStringResource(contextMenuR.string.mozac_feature_contextmenu_open_link_in_private_tab)

    // get() so it binds to the composeRule of the current test invocation. Not on PageContext by design —
    // see HistorySearchGroupPage.
    private val searchGroup
        get() = HistorySearchGroupPage(composeRule)

    // SystemSettingsPage is not on PageContext (nothing referenced it until now) and this flow reaches the
    // Android Settings app through Fenix's own "Go to settings" intent rather than a registered edge, so it is
    // instantiated locally. get() binds it to the current test invocation's composeRule.
    private val systemSettings
        get() = SystemSettingsPage(composeRule)

    // Legacy SearchTest drives these URLs off SearchMockServerRule, whose dispatcher 404s everything
    // except searchResults.html. That is load-bearing for verifyTabsSearchWithOpenTabsTest: the tabs
    // never load, so they have no title and the awesomebar row shows the URL, which is what the
    // suggestion assertions match on. BaseTest's mockWebServer serves the asset for real, the tab
    // gets the title "Test_Page_1", and the same assertions cannot match. Keep this rule to preserve
    // the legacy environment rather than re-pointing the assertions at titles.
    @get:Rule val searchMockServerRule = SearchMockServerRule()

    @Ignore("Covered by verifyNavigationReachability[0: SearchBarPage (TBD)")
    @Test
    fun homeSearchBarItemsTest() {
        // Given: App is loaded with default settings
        // on = AndroidComposeTestRule<HomeActivityIntentTestRule, *> with app defaults

        // When: We navigate to the Homepage page with the SearchBar component visible
        on.home.navigateToPage()
        on.searchBar.navigateToPage()

        // Then: the toolbar elements should load
        on.searchBar.mozVerifyReadiness()
    }

    // TestRail link:
    @Ignore("Covered by verifyNavigationReachability[0: SearchBarPage (TBD)")
    @Test
    fun browserSearchBarItemsTest() {
        // Given: App is loaded with default settings
        // on = AndroidComposeTestRule<HomeActivityIntentTestRule, *> with app defaults

        // When: We navigate to the Browser page with the SearchBar component visible
        on.home.navigateToPage()
        on.browserPage.navigateToPage("mozilla.com")
        on.searchBar.navigateToPage()

        // Then: the search bar elements should load
        on.searchBar.mozVerifyReadiness()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/522918
    @Critical
    @Test
    fun verifyClearSearchButtonTest() {
        val firstPage = mockWebServer.getGenericAsset(1)
        val secondPage = mockWebServer.getGenericAsset(2)

        // From the homepage: type a term, clear it, then type again. The search bar stays in edit mode
        // across the clear, which is what the manual case asserts.
        on.searchBar.navigateToPage().mozEnterText(queryString, SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.searchBar
            .verifyTypedToolbarText(queryString)
            .clickClearButton()
            .verifySearchBarPlaceholder()
            .mozVerify(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozEnterText("mozilla", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
        on.searchBar.verifyTypedToolbarText("mozilla")

        // Load a page, then tap the toolbar: its URL appears in the edit-mode search bar.
        on.home.navigateToPage()
        on.browserPage.navigateToPage(firstPage.url.toString()).verifyUrl(firstPage.url.toString())

        // Clear the URL (the search bar stays open), then type a new URL and load it.
        on.searchBar
            .navigateToPage()
            .verifyTypedToolbarText(firstPage.url.toString())
            .clickClearButton()
            .verifySearchBarPlaceholder()
            .mozVerify(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        on.browserPage.navigateToPage().verifyUrl(secondPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/235397
    @SmokeTest
    @Test
    fun scanQRCodeToOpenAWebpageTest() {
        // Same guard as the legacy test: with no camera the scanner cannot be exercised at all, so the
        // test is skipped rather than failed.
        val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SHORTCUT("DuckDuckGo"))
            .mozClick(SearchBarSelectors.SCAN_BUTTON)

        AppAndSystemHelper.grantSystemPermission()

        on.searchBar.verifyScannerOpen()
    }

    // Verifies a temporary change of search engine from the Search shortcut menu
    @SmokeTest
    @Test
    fun searchEnginesCanBeChangedTemporarilyFromSearchSelectorMenuTest() {
        (generalEnginesList + topicEnginesList).forEach { searchEngine ->
            on.searchBar
                .navigateToPage()
                .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
                .mozVerify(SearchBarSelectors.SEARCH_SHORTCUT(searchEngine))
                .mozClick(SearchBarSelectors.SEARCH_SHORTCUT(searchEngine))
                .mozVerify(ToolbarSelectors.SEARCH_ENGINE_SELECTOR_ICON(searchEngine))
                .mozEnterText("mozilla ", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
                .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

            on.browserPage.navigateToPage().verifyUrl("mozilla")

            on.home.navigateToPage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1232638
    @Critical
    @Test
    fun defaultSearchCodeDuckDuckGoUSTest() {
        on.settingsSearchDefaultSearchEngine
            .navigateToPage()
            .mozClick(SettingsSearchDefaultSearchEngineSelectors.DEFAULT_SEARCH_ENGINE_OPTION("DuckDuckGo"))

        on.home.navigateToPage()

        on.searchBar
            .navigateToPage()
            .mozEnterText(queryString, SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        on.browserPage.navigateToPage().verifyUrl(queryString)

        on.mainMenu.navigateToPage().mozClick(MainMenuSelectors.BOOKMARK_THIS_PAGE_BUTTON)

        on.browserPage.navigateToPage()

        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.BOOKMARKS_BUTTON)
            .mozVerify(BookmarksSelectors.BOOKMARK_ITEM_URL("https://duckduckgo.com/?t=fpas&q=firefox"))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154215
    @SmokeTest
    @Test
    fun verifyHistorySearchWithBrowsingHistoryTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url.toString()
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url.toString()

        MockBrowserDataHelper.createHistoryItem(firstPageUrl)
        MockBrowserDataHelper.createHistoryItem(secondPageUrl)

        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SHORTCUT("History"))
            .mozEnterText("Mozilla", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyNoneContainText(SearchBarSelectors.SEARCH_SUGGESTION, "Mozilla")
            .mozClick(SearchBarSelectors.CLEAR_BUTTON)
            .mozEnterText("generic", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyAnyContainsText(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE, "generic")
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, firstPageUrl)
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, secondPageUrl)
            .mozClick(SearchBarSelectors.SEARCH_SUGGESTION_WITH_TEXT(firstPageUrl))

        // The suggestion click already navigated to the browser, so verify the URL directly;
        // navigateToPage()'s non-polling check can race the load and fail against the search bar.
        on.browserPage.verifyUrl(firstPageUrl)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154199
    @SmokeTest
    @Test
    fun verifyTabsSearchWithOpenTabsTest() {
        // Carried over from the legacy test: without this the tab-groups CFR can cover the toolbar
        // partway through and the search selector becomes unclickable.
        appContext.components.settings.tabGroupsOnboardingEnabled = false

        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url.toString()
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url.toString()

        MockBrowserDataHelper.createTabItem(firstPageUrl)
        MockBrowserDataHelper.createTabItem(secondPageUrl)

        on.searchBar
            .navigateToPage()
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SHORTCUT("Tabs"))
            .mozEnterText("Mozilla", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyNoneContainText(SearchBarSelectors.SEARCH_SUGGESTION, "Mozilla")
            .mozClick(SearchBarSelectors.CLEAR_BUTTON)
            .mozEnterText("generic", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerifyAnyContainsText(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE, "generic")
            .mozVerify(SearchBarSelectors.FIREFOX_SUGGEST_HEADER)
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, firstPageUrl)
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, secondPageUrl)
            .mozClick(SearchBarSelectors.SEARCH_SUGGESTION_WITH_TEXT(firstPageUrl))

        // Settle on the browser page before asserting: clicking a suggestion leaves the page tracker on
        // SearchBarComponent, so the later tab-drawer hop would otherwise be routed through the edit-mode
        // toolbar, which no longer exists once the tab is showing.
        on.browserPage.navigateToPage().mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("2"))

        on.tabDrawer
            .navigateToPage()
            .verifyOpenTabsOrder(position = 1, tabTitle = firstPageUrl)
            .verifyOpenTabsOrder(position = 2, tabTitle = secondPageUrl)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2230212
    @SmokeTest
    @Test
    fun searchHistoryNotRememberedInPrivateBrowsingTest() {
        appContext.components.settings.shouldShowSearchSuggestionsInPrivate = true

        val firstPageAsset = searchMockServerRule.server.getGenericAsset(1)
        val firstPageUrl = firstPageAsset.url.toString()
        val searchEngineName = "TestSearchEngine"

        MockBrowserDataHelper.setCustomSearchEngine(searchMockServerRule.server, searchEngineName)
        MockBrowserDataHelper.createBookmarkItem(firstPageUrl, firstPageAsset.title, 1u)

        // forceNavigation throughout: SearchBarComponent's arrival check is SEARCH_ENGINE_SELECTOR, which
        // also renders on the home toolbar once a custom search engine is registered. The default
        // already-visible check therefore false-positives and skips the click into edit mode, leaving
        // mozEnterText with no search box to type into.
        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozEnterText("test page 1", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            // Edit mode must be gone before the next hop. BrowserPage's arrival check resolves the engine
            // view underneath the edit-mode overlay, so without this the run reports arrival on a page it
            // never reached and then fails several steps later somewhere unrelated.
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        // Back out rather than taking the registered BrowserPage -> HomePage edge: that edge clicks
        // "New tab", which is not reliably present after a search and opens a tab the legacy flow never
        // opens.
        on.browserPage.mozPressBackUntilGone(BrowserPageSelectors.ENGINE_VIEW)

        on.home.navigateToPage().mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)

        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozEnterText("test page 2", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        on.browserPage.navigateToPage().mozClick(ToolbarSelectors.TOOLBAR_URL_BOX_UIAUTOMATOR2)

        on.searchBar
            // mozEnterText's own locate reports success on a Compose node that does not exist, so assert
            // edit mode is really up first: this is a polled assertExists/assertIsDisplayed check.
            .mozVerify(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozEnterText("test page", SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozVerify(SearchBarSelectors.FIREFOX_SUGGEST_HEADER)
            .mozVerify(SearchBarSelectors.SUGGESTIONS_HEADER("$searchEngineName search"))
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, "test page 1")
            .mozVerifyAnyContainsText(SearchBarSelectors.SEARCH_SUGGESTION, firstPageUrl)
            // 2 search engine suggestions and 2 browser suggestions (1 history, 1 bookmark)
            .mozVerifyElementCount(SearchBarSelectors.SEARCH_SUGGESTION, 4)
            .mozVerifyNoneContainText(SearchBarSelectors.SEARCH_SUGGESTION, "test page 2")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1623441
    @SmokeTest
    @Test
    fun searchResultsOpenedInNewTabsGenerateSearchGroupsTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url.toString()
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url.toString()

        MockBrowserDataHelper.setCustomSearchEngine(searchMockServerRule.server, "TestSearchEngine")

        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozEnterText(queryString, SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        // The two links open tabs that never load: SearchDispatcher serves only searchResults.html and 404s
        // the link targets. Only the URL is assertable, which is what the legacy test checks too.
        on.browserPage.navigateToPage()
        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 1", openLinkInNewTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInNewTab))
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.browserPage
            .verifyUrl(firstPageUrl)
            // Back out of the 404 tab to the results page. One press, as legacy does.
            .mozPressBack()

        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 2", openLinkInNewTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInNewTab))
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.browserPage.verifyUrl(secondPageUrl)

        on.tabDrawer.navigateToPage().closeAllTabs()

        // The group holds 3 URLs: the search-results origin plus the two links opened from it.
        on.home
            .navigateToPage()
            .mozVerify(HomeSelectors.RECENTLY_VISITED_HEADER)
            .mozVerifyElementHasSiblingWithText(
                HomeSelectors.RECENTLY_VISITED_SEARCH_GROUP(queryString),
                getStringResource(R.string.history_search_group_sites_1, 3),
            )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1591781
    @SmokeTest
    @Test
    fun searchGroupIsNotGeneratedForLinksOpenedInPrivateTabsTest() {
        MockBrowserDataHelper.setCustomSearchEngine(searchMockServerRule.server, "TestSearchEngine")

        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozEnterText(queryString, SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        // Both long presses happen on the same results page: unlike "Open link in new tab", opening in a
        // private tab does not switch to it, so there is no snackbar and nothing to back out of.
        on.browserPage.navigateToPage()
        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 1", openLinkInPrivateTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInPrivateTab))
        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 2", openLinkInPrivateTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInPrivateTab))

        // Each private tab has to be opened, not merely created: the pages are lazily loaded until selected,
        // and it is loading them that would leak into history if private browsing did not isolate it. Every
        // return to the tab manager is re-anchored through BrowserPage, since a TabDrawerPage self-loop
        // resolves to no steps and would silently do nothing.
        on.tabDrawer.navigateToPage().mozClick(TabDrawerSelectors.PRIVATE_TABS_PAGE_BUTTON)
        on.tabDrawer.openPrivateTab(position = 1)

        on.browserPage.navigateToPage()
        on.tabDrawer.navigateToPage().openPrivateTab(position = 2)

        on.browserPage.navigateToPage()
        on.tabDrawer.navigateToPage().closeAllTabs()

        // closeAllTabs from the private tray lands on the private homepage; toggle back to normal browsing
        // before asserting, because the search group would only ever render there.
        on.home
            .navigateToPage()
            .mozClick(HomeSelectors.PRIVATE_BROWSING_BUTTON)
            .mozVerifyElementAbsent(HomeSelectors.RECENTLY_VISITED_SEARCH_GROUP(queryString))

        // Deviation from legacy, deliberate: it asserts the absence of "3 sites", but the History screen
        // renders a group's size with history_search_group_sites_1 ("%d pages") and no "%d sites" string
        // exists anywhere in the tree — so the legacy assertion cannot fail and is dead. This asserts the
        // caption the app actually renders. The caption and not the group title, because the title is the
        // search term, which also appears inside the visited search-results URL.
        on.history
            .navigateToPage()
            .mozVerifyElementAbsent(
                HistorySelectors.HISTORY_ITEM_WITH_TEXT(getStringResource(R.string.history_search_group_sites_1, 3))
            )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1592269
    @SmokeTest
    @Test
    fun deleteIndividualHistoryItemsFromSearchGroupTest() {
        val firstPageUrl = searchMockServerRule.server.getGenericAsset(1).url.toString()
        val secondPageUrl = searchMockServerRule.server.getGenericAsset(2).url.toString()
        val threePagesCaption = getStringResource(R.string.history_search_group_sites_1, 3)

        MockBrowserDataHelper.setCustomSearchEngine(searchMockServerRule.server, "TestSearchEngine")

        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozEnterText(queryString, SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozWaitUntilAbsent(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        // Same group-building steps as searchResultsOpenedInNewTabsGenerateSearchGroupsTest, duplicated
        // rather than shared because the legacy tests duplicate them too and are reviewed side by side.
        on.browserPage.navigateToPage()
        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 1", openLinkInNewTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInNewTab))
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.browserPage.verifyUrl(firstPageUrl).mozPressBack()

        on.browserPage
            .longClickPageObjectUntilContextMenu("Link 2", openLinkInNewTab)
            .mozClick(BrowserPageSelectors.CONTEXT_MENU_ITEM(openLinkInNewTab))
            .mozClick(BrowserPageSelectors.SNACKBAR_ACTION_BUTTON)
        on.browserPage.verifyUrl(secondPageUrl)

        on.tabDrawer.navigateToPage().closeAllTabs()

        on.home
            .navigateToPage()
            .mozVerify(HomeSelectors.RECENTLY_VISITED_HEADER)
            .mozVerifyElementHasSiblingWithText(
                HomeSelectors.RECENTLY_VISITED_SEARCH_GROUP(queryString),
                threePagesCaption,
            )
            .mozClick(HomeSelectors.RECENTLY_VISITED_SEARCH_GROUP(queryString))

        // Two different delete paths, as legacy does: the per-row cross for the first item and multi-select
        // for the second.
        searchGroup
            .verifyGroupIsOpen(queryString)
            .deleteItemWithRowButton(firstPageUrl)
            .deleteItemWithMultiSelectOverflow(secondPageUrl)
            .exitToHomepage()

        // Only the search-results origin is left, which is under the minimum group size, so the whole group
        // disappears. mozWaitUntilAbsent and not mozVerifyElementAbsent: the homepage rebuilds asynchronously
        // after the deletions, and legacy covers that with a 1s window wait before a single-shot check.
        on.home.navigateToPage().mozWaitUntilAbsent(HomeSelectors.RECENTLY_VISITED_SEARCH_GROUP(queryString))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1059459
    @SmokeTest
    @RequiresDeniedRuntimePermission(Manifest.permission.CAMERA)
    @Test
    fun verifyQRScanningCameraAccessDialogTest() {
        // Same guard as legacy: with no camera the flow cannot be exercised, so skip rather than fail.
        val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        appContext.components.settings.setCameraPermissionNeededState = true

        on.searchBar
            .navigateToPage(forceNavigation = true)
            .mozClick(SearchBarSelectors.SEARCH_ENGINE_SELECTOR)
            .mozClick(SearchBarSelectors.SEARCH_SHORTCUT("DuckDuckGo"))
            .mozClick(SearchBarSelectors.SCAN_BUTTON)
            // Poll rather than mozClick's default single look: the deny buttons belong to
            // GrantPermissionsActivity in another process, and SCAN_BUTTON returns as soon as the request
            // intent is sent, roughly half a second before that activity is displayed.
            .mozClick(SystemSettingsSelectors.PERMISSION_DENY_BUTTON, WaitPolicy.Poll(waitingTime))
            .mozClick(SearchBarSelectors.SCAN_BUTTON)
            .mozClick(
                SystemSettingsSelectors.PERMISSION_DENY_AND_DONT_ASK_AGAIN_BUTTON,
                WaitPolicy.Poll(waitingTime),
            )
            // Back out until the homepage TOOLBAR is showing, not just HOMEPAGE_VIEW: the homepage view
            // resolves behind the search overlay, so anchoring on it returns immediately while the toolbar is
            // still covered — and HomePage's arrival check needs the menu button, so the following
            // navigateToPage would then route through the "New tab" edge and fail.
            .mozPressBackUntilPresent(HomeSelectors.MAIN_MENU_BUTTON)

        // Re-anchor the page tracker too — it is still on SearchBarComponent, and findPath would otherwise
        // route SearchBar -> BrowserPage by TYPING A URL to reach anything else.
        on.home.navigateToPage()

        // Routed through Settings rather than the main menu's own Sign in row, which is the registered edge;
        // same destination, and it keeps the nav graph as the single source of truth for how to get there.
        on.settingsTurnOnSync
            .navigateToPage()
            .clickReadyToScanUntilPermissionDialog()
            .mozClick(SettingsTurnOnSyncSelectors.PERMISSION_DIALOG_DISMISS_BUTTON)
        on.settingsTurnOnSync
            .clickReadyToScanUntilPermissionDialog()
            .mozClick(SettingsTurnOnSyncSelectors.PERMISSION_DIALOG_GO_TO_SETTINGS_BUTTON)

        // Leaves Fenix for the Android Settings app.
        systemSettings.openAppPermissions().allowAppPermission("Camera")

        on.settingsTurnOnSync.returnFromSystemSettings().mozClick(SettingsTurnOnSyncSelectors.READY_TO_SCAN_BUTTON)

        on.searchBar.verifyScannerOpen()
    }
}
