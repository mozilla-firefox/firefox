/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import androidx.navigation.NavController
import androidx.navigation.NavDestination
import androidx.navigation.NavDirections
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.Runs
import io.mockk.coEvery
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.ContentAction.UpdateLoadingStateAction
import mozilla.components.browser.state.action.ContentAction.UpdateProgressAction
import mozilla.components.browser.state.action.ContentAction.UpdateSecurityInfoAction
import mozilla.components.browser.state.action.ContentAction.UpdateUrlAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.action.TabListAction.AddTabAction
import mozilla.components.browser.state.action.TabListAction.RemoveTabAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.TrackingProtectionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.AnimatedPillActionRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.CopyToClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.LoadFromClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.PasteFromClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent.Source
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionAction
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.lib.state.Middleware
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.ClipboardHandler
import mozilla.components.support.utils.INTENT_TYPE_PDF
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.PageTranslationStatus
import org.mozilla.fenix.browser.ReaderModeStatus
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.SimpleBrowsingModeManager
import org.mozilla.fenix.browser.readermode.ReaderModeController
import org.mozilla.fenix.browser.store.BrowserScreenAction
import org.mozilla.fenix.browser.store.BrowserScreenAction.ClosingLastPrivateTab
import org.mozilla.fenix.browser.store.BrowserScreenAction.PageTranslationStatusUpdated
import org.mozilla.fenix.browser.store.BrowserScreenAction.ReaderModeStatusUpdated
import org.mozilla.fenix.browser.store.BrowserScreenState
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.NimbusComponents
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.CurrentTabClosed
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction.SnackbarDismissed
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.OrientationMode.Landscape
import org.mozilla.fenix.components.appstate.OrientationMode.Portrait
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.TABS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.toolbar.BrowserToolbarMiddleware.ToolbarAction
import org.mozilla.fenix.components.toolbar.DisplayActions.AddBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.EditBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.HomepageClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.RefreshClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.ShareClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.StopRefreshClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.TranslateClicked
import org.mozilla.fenix.components.toolbar.PageEndActionsInteractions.ReaderModeClicked
import org.mozilla.fenix.components.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.CloseCurrentTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.directionsEq
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.ui.AccessPoint
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Stories.markAsOpenedFromHomeScreen
import org.mozilla.fenix.utils.Stories.markAsOpenedFromStoriesScreen
import org.robolectric.annotation.Config
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR

@RunWith(AndroidJUnit4::class)
class BrowserToolbarMiddlewareTest {

    @get:Rule
    val gleanRule = FenixGleanTestRule(testContext)

    private val testDispatcher = StandardTestDispatcher()
    private val testScope = CoroutineScope(testDispatcher)

    private val searchEngine: SearchEngine = fakeSearchState().customSearchEngines.first()
    private val browserScreenState: BrowserScreenState = mockk(relaxed = true)
    private val browserScreenStore: BrowserScreenStore = mockk(relaxed = true) {
        every { state } returns browserScreenState
    }
    private val browserStore = BrowserStore()
    private val clipboard: ClipboardHandler = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val browsingModeManager = SimpleBrowsingModeManager(Normal)
    private val thumbnailsFeature: BrowserThumbnails = mockk(relaxed = true)
    private val readerModeController: ReaderModeController = mockk(relaxed = true)
    private val useCases: UseCases = mockk(relaxed = true)
    val nimbusEventsStore: NimbusEventStore = mockk {
        every { recordEvent(any()) } just Runs
    }
    private val nimbusComponents: NimbusComponents = mockk {
        every { events } returns nimbusEventsStore
    }
    private val settings = Settings(testContext)
    private val tabId = "test"
    private val tab: TabSessionState = mockk(relaxed = true) {
        every { id } returns tabId
    }
    private val permissionsStorage: SitePermissionsStorage = mockk()
    private val cookieBannersStorage: CookieBannersStorage = mockk()
    private val trackingProtectionUseCases: TrackingProtectionUseCases = mockk()
    private val publicSuffixList = PublicSuffixList(testContext)
    private val bookmarksStorage: BookmarksStorage = mockk()
    private val ipProtectionStore = IPProtectionStore()
    private lateinit var appStore: AppStore

    @Before
    fun setup() {
        appStore = spyk(AppStore())
        coEvery { bookmarksStorage.getBookmarksWithUrl(any()) } returns Result.success(listOf(mockk()))
        settings.shouldUseBottomToolbar = false
        settings.shouldUseExpandedToolbar = false
        settings.isTabStripEnabled = false
    }

    @Test
    fun `WHEN initializing the toolbar THEN add browser start actions`() = runTest(testDispatcher) {
        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertEquals(emptyList<Action>(), toolbarBrowserActions)
    }

    @Test
    fun `WHEN initializing the toolbar THEN add browser end actions`() = runTest(testDispatcher) {
        val toolbarStore = buildStore()
        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        val newTabButton = toolbarBrowserActions[0]
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        val menuButton = toolbarBrowserActions[2]
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `WHEN initializing the toolbar on bottom THEN add browser end actions`() = runTest(testDispatcher) {
        settings.shouldUseBottomToolbar = true
        val toolbarStore = buildStore()
        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        val newTabButton = toolbarBrowserActions[0]
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        val menuButton = toolbarBrowserActions[2]
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedBottomTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN normal browsing mode WHEN initializing the toolbar THEN show the number of normal tabs in the tabs counter button`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(createTab("test.com", private = false)),
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore, browsingModeManager = browsingModeManager)

        val toolbarStore = buildStore(middleware)

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN private browsing mode WHEN initializing the toolbar THEN show the number of private tabs in the tabs counter button`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    createTab("test.com", private = true),
                    createTab("firefox.com", private = true),
                ),
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore, browsingModeManager = browsingModeManager)

        val toolbarStore = buildStore(middleware)

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(2, true), tabCounterButton)
    }

    @Test
    fun `WHEN initializing the toolbar THEN setup showing the website origin`() {
        val initialTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(initialTab),
                selectedTabId = initialTab.id,
            ),
        )
        val expectedConfiguration = PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = initialTab.getUrl(),
            contextualMenuOptions = ContextualMenuOption.entries,
            onClick = OriginClicked,
        )
        val middleware = buildMiddleware(browserStore = browserStore)

        val toolbarStore = buildStore(middleware)

        val originConfiguration = toolbarStore.state.displayState.pageOrigin
        assertEqualsOrigin(expectedConfiguration, originConfiguration)
    }

    @Test
    fun `GIVEN ABOUT_HOME URL WHEN the page origin is modified THEN update the page origin`() = runTest(testDispatcher) {
        val tab = createTab("https://mozilla.com/")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware)

        val pageOrigin = PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = URLStringUtils.toDisplayUrl(tab.getUrl()!!).toString(),
            contextualMenuOptions = ContextualMenuOption.entries,
            onClick = OriginClicked,
        )
        assertEqualsOrigin(pageOrigin, toolbarStore.state.displayState.pageOrigin)

        browserStore.dispatch(UpdateUrlAction(sessionId = tab.id, url = ABOUT_HOME_URL))
        testDispatcher.scheduler.advanceUntilIdle()
        testDispatcher.scheduler.advanceUntilIdle()

        assertEqualsOrigin(
            pageOrigin.copy(
                url = "",
            ),
            toolbarStore.state.displayState.pageOrigin,
        )
    }

    @Test
    fun `GIVEN narrow window WHEN changing to wide window THEN keep browser end actions`() = runTest(testDispatcher) {
        val appStore = AppStore(
            initialState = AppState(
                orientation = Portrait,
            ),
        )
        var isWideScreen = false
        var isTallScreen = false
        val middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { isWideScreen },
            isTallScreen = { isTallScreen },
        )
        val toolbarStore = buildStore(middleware)

        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)

        isWideScreen = true

        appStore.dispatch(AppAction.OrientationChange(Landscape))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        val newTabButton = toolbarBrowserActions[0]
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        val menuButton = toolbarBrowserActions[2]
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN wide window WHEN changing to narrow window THEN keep all browser end actions`() = runTest(testDispatcher) {
        val appStore = AppStore(
            initialState = AppState(
                orientation = Landscape,
            ),
        )
        var isWideScreen = false
        var isTallScreen = false
        val middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { isWideScreen },
            isTallScreen = { isTallScreen },
        )

        val toolbarStore = buildStore(middleware)

        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        val newTabButton = toolbarBrowserActions[0] as ActionButtonRes
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        val menuButton = toolbarBrowserActions[2] as ActionButtonRes
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)

        isWideScreen = true

        appStore.dispatch(AppAction.OrientationChange(Portrait))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
    }

    @Test
    fun `GIVEN in normal browsing WHEN the number of normal opened tabs is modified THEN update the tab counter`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore()
        val middleware = buildMiddleware(browserStore = browserStore, browsingModeManager = browsingModeManager)

        val toolbarStore = buildStore(middleware)

        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(0), tabCounterButton)

        val newNormalTab = createTab("test.com", private = false)
        val newPrivateTab = createTab("test.com", private = true)
        browserStore.dispatch(AddTabAction(newNormalTab))
        browserStore.dispatch(AddTabAction(newPrivateTab))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN in private browsing WHEN the number of private opened tabs is modified THEN update the tab counter`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val initialNormalTab = createTab("test.com", private = false)
        val initialPrivateTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(initialNormalTab, initialPrivateTab),
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore, browsingModeManager = browsingModeManager)

        val toolbarStore = buildStore(middleware)

        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1, true), tabCounterButton)

        browserStore.dispatch(RemoveTabAction(initialPrivateTab.id))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(0, true), tabCounterButton)
    }

    @Test
    fun `WHEN clicking the new tab button THEN navigate to application's home screen`() {
        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val newTabButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

        verify { navController.navigate(BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true)) }
    }

    @Test
    fun `GIVEN homepage as new tab is enabled WHEN clicking the new tab button THEN navigate to home screen without focus`() {
        settings.enableHomepageAsNewTab = true
        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val newTabButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

        verify { useCases.fenixBrowserUseCases.addNewHomepageTab(false) }
    }

    @Test
    fun `WHEN clicking the new tab button with homepage search bar enabled THEN navigate to home screen without focus`() {
        settings.enableHomepageSearchBar = true
        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val newTabButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

        verify { navController.navigate(BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = false)) }
    }

    @Test
    fun `WHEN clicking the menu button THEN open the menu`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment

        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes

        toolbarStore.dispatch(menuButton.onClick as BrowserToolbarEvent)

        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                    accesspoint = MenuAccessPoint.Browser,
                ),
                null,
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking the tab counter button THEN open the tabs tray in normal mode`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val middleware = buildMiddleware(browserStore = browserStore, browsingModeManager = browsingModeManager)
        val toolbarStore = buildStore(middleware)
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalTabManagementFragment(page = Page.NormalTabs),
                null,
            )
        }
        verify {
            thumbnailsFeature.requestScreenshot()
        }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking the tab counter button THEN open the tabs tray in private mode`() {
        val navController: NavController = mockk(relaxed = true) {
            every { currentDestination?.id } returns R.id.browserFragment
        }
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val thumbnailsFeature: BrowserThumbnails = mockk(relaxed = true)
        val middleware = buildMiddleware(
            navController = navController,
            browsingModeManager = browsingModeManager,
            thumbnailsFeature = { thumbnailsFeature },
        )
        val toolbarStore = buildStore(middleware)
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalTabManagementFragment(
                    enterMultiselect = false,
                    page = Page.PrivateTabs,
                    accessPoint = AccessPoint.None,
                ),
                null,
            )
        }
        verify {
            thumbnailsFeature.requestScreenshot()
        }
    }

    @Test
    fun `WHEN clicking on the first option in the toolbar long click menu THEN open a new normal tab`() {
        val navController: NavController = mockk(relaxed = true)
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val middleware = buildMiddleware(navController = navController, browsingModeManager = browsingModeManager)
        val toolbarStore = buildStore(middleware)
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(0, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Normal, browsingModeManager.mode)
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
        }
    }

    @Test
    fun `GIVEN no search terms for the current tab WHEN the page origin is clicked THEN start search in the home screen`() {
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    sessionToStartSearchFor = browserStore.state.selectedTabId,
                ),
            )
        }
    }

    @Test
    fun `GIVEN the current tab has search terms WHEN the page origin is clicked THEN start search in the browser screen`() {
        val currentTab = createTab("test.com", searchTerms = "test")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify { appStore.dispatch(SearchStarted(currentTab.id)) }
        assertEquals(currentTab.content.searchTerms, toolbarStore.state.editState.query.current)
        assertTrue(toolbarStore.state.editState.isQueryPrefilled)
    }

    @Test
    fun `WHEN clicking on the URL THEN record telemetry`() {
        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        assertEquals("BROWSER", Events.searchBarTapped.testGetValue()?.last()?.extra?.get("source"))
    }

    @Test
    @Config(sdk = [30])
    fun `GIVEN on Android 11 WHEN choosing to copy the current URL to clipboard THEN copy to clipboard and show a snackbar`() {
        val clipboard = ClipboardHandler(testContext)
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            clipboard = clipboard,
        )
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(CopyToClipboardClicked)

        assertEquals(currentTab.getUrl(), clipboard.text)
        verify { appStore.dispatch(URLCopiedToClipboard) }
        assertNotNull(Events.copyUrlTapped.testGetValue())
    }

    @Test
    @Config(sdk = [31])
    fun `GIVEN on Android 12 WHEN choosing to copy the current URL to clipboard THEN copy to clipboard and show a snackbar`() {
        val clipboard = ClipboardHandler(testContext)
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            clipboard = clipboard,
        )
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(CopyToClipboardClicked)

        assertEquals(currentTab.getUrl(), clipboard.text)
        verify { appStore.dispatch(URLCopiedToClipboard) }
        assertNotNull(Events.copyUrlTapped.testGetValue())
    }

    @Test
    @Config(sdk = [33])
    fun `GIVEN on Android 13 WHEN choosing to copy the current URL to clipboard THEN copy to clipboard and don't show a snackbar`() {
        val clipboard = ClipboardHandler(testContext)
        val currentTab = createTab("firefox.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            clipboard = clipboard,
        )
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(CopyToClipboardClicked)

        assertEquals(currentTab.getUrl(), clipboard.text)
        verify(exactly = 0) { appStore.dispatch(URLCopiedToClipboard) }
        assertNotNull(Events.copyUrlTapped.testGetValue())
    }

    @Test
    fun `WHEN choosing to paste from clipboard THEN start a new search with the current clipboard text`() {
        val queryText = "test"
        val clipboard = ClipboardHandler(testContext).also {
            it.text = queryText
        }
        val currentTab = createTab("firefox.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            clipboard = clipboard,
        )
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(PasteFromClipboardClicked)

        verify {
            appStore.dispatch(SearchStarted(currentTab.id))
            assertEquals(queryText, toolbarStore.state.editState.query.current)
            assertTrue(toolbarStore.state.editState.isQueryPrefilled)
        }
    }

    @Test
    fun `WHEN choosing to load URL from clipboard THEN start load the URL from clipboard in a new tab`() {
        val clipboardUrl = "https://www.mozilla.com"
        val clipboard = ClipboardHandler(testContext).also {
            it.text = clipboardUrl
        }
        val currentTab = createTab("wikipedia.org", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com", private = true)),
                selectedTabId = currentTab.id,
            ),
        )
        val browserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val useCases: UseCases = mockk {
            every { fenixBrowserUseCases } returns browserUseCases
        }
        val middleware = buildMiddleware(
            browserStore = browserStore,
            useCases = useCases,
            clipboard = clipboard,
        )
        val toolbarStore = buildStore(middleware)

        every { appStore.state.searchState.selectedSearchEngine?.searchEngine } returns searchEngine

        toolbarStore.dispatch(LoadFromClipboardClicked)

        verify {
            browserUseCases.loadUrlOrSearch(
                searchTermOrURL = clipboardUrl,
                newTab = false,
                searchEngine = searchEngine,
                private = false,
            )
        }
        assertEquals(
            "false",
            Events.enteredUrl.testGetValue()?.last()?.extra?.get("autocomplete"),
        )
    }

    @Test
    fun `WHEN clicking on the second option in the toolbar long click menu THEN open a new private tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val middleware = buildMiddleware(browsingModeManager = browsingModeManager)
        val toolbarStore = buildStore(middleware)
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(0, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[1] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
        }
    }

    @Test
    fun `GIVEN multiple tabs opened WHEN clicking on the close tab item in the tab counter long click menu THEN close the current tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com", private = true)),
                selectedTabId = currentTab.id,
            ),
        )
        val tabsUseCases: TabsUseCases = mockk(relaxed = true)
        every { useCases.tabsUseCases } returns tabsUseCases
        val middleware = buildMiddleware(
            browserStore = browserStore,
            browsingModeManager = browsingModeManager,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(2, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[3] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        verify {
            tabsUseCases.removeTab(currentTab.id, true)
            appStore.dispatch(CurrentTabClosed(true))
        }
        verify(exactly = 0) {
            navController.navigate(any<NavDirections>())
        }
    }

    @Test
    fun `GIVEN on the last open normal tab WHEN clicking on the close tab item in the tab counter long click menu THEN navigate to home before closing the tab`() {
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val tabsUseCases: TabsUseCases = mockk(relaxed = true)
        every { useCases.tabsUseCases } returns tabsUseCases
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[3] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Normal, browsingModeManager.mode)
        verify(exactly = 0) {
            tabsUseCases.removeTab(any(), any())
            appStore.dispatch(CurrentTabClosed(true))
            appStore.dispatch(CurrentTabClosed(false))
        }
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(
                    sessionToDelete = currentTab.id,
                ),
            )
        }
    }

    @Test
    fun `GIVEN on the last open private tab and no private downloads WHEN clicking on the close tab item THEN navigate to home before closing the tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com")),
                selectedTabId = currentTab.id,
            ),
        )
        val tabsUseCases: TabsUseCases = mockk(relaxed = true)
        every { useCases.tabsUseCases } returns tabsUseCases
        val middleware = buildMiddleware(
            browserStore = browserStore,
            browsingModeManager = browsingModeManager,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[3] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        verify(exactly = 0) {
            tabsUseCases.removeTab(any(), any())
            appStore.dispatch(CurrentTabClosed(true))
            appStore.dispatch(CurrentTabClosed(false))
        }
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(
                    sessionToDelete = currentTab.id,
                ),
            )
        }
    }

    @Test
    fun `GIVEN on the last open private tab with private downloads in progress WHEN clicking on the close tab item THEN navigate to home before closing the tab`() {
        every { browserScreenStore.state } returns BrowserScreenState(
            cancelPrivateDownloadsAccepted = false,
        )
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com")),
                selectedTabId = currentTab.id,
                downloads = mapOf("test" to DownloadState("download", private = true)),
            ),
        )
        val tabsUseCases: TabsUseCases = mockk(relaxed = true)
        every { useCases.tabsUseCases } returns tabsUseCases
        val middleware = buildMiddleware(
            appStore = appStore,
            browserStore = browserStore,
            useCases = useCases,
            browsingModeManager = browsingModeManager,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[3] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        verify(exactly = 0) {
            tabsUseCases.removeTab(any(), any())
            appStore.dispatch(CurrentTabClosed(true))
            appStore.dispatch(CurrentTabClosed(false))
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(
                    sessionToDelete = currentTab.id,
                ),
            )
        }
        verify {
            browserScreenStore.dispatch(
                ClosingLastPrivateTab(
                    tabId = currentTab.id,
                    inProgressPrivateDownloads = 1,
                ),
            )
        }
    }

    @Test
    fun `GIVEN on the last open private tab and accepted cancelling private downloads WHEN clicking on the close tab item THEN inform about closing the last private tab`() {
        every { browserScreenStore.state } returns BrowserScreenState(
            cancelPrivateDownloadsAccepted = true,
        )
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com")),
                selectedTabId = currentTab.id,
                downloads = mapOf("test" to DownloadState("download", private = true)),
            ),
        )
        val tabsUseCases: TabsUseCases = mockk(relaxed = true)
        every { useCases.tabsUseCases } returns tabsUseCases
        val middleware = buildMiddleware(
            appStore = appStore,
            browserStore = browserStore,
            useCases = useCases,
            browsingModeManager = browsingModeManager,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        assertEqualsTabCounterButton(expectedTabCounterButton(1, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[3] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        verify(exactly = 0) {
            tabsUseCases.removeTab(any(), any())
            appStore.dispatch(CurrentTabClosed(true))
            appStore.dispatch(CurrentTabClosed(false))
            browserScreenStore.dispatch(any())
        }
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(
                    sessionToDelete = currentTab.id,
                ),
            )
        }
    }

    @Test
    fun `GIVEN a bottom toolbar WHEN the loading progress of the current tab changes THEN update the progress bar`() = runTest(testDispatcher) {
        settings.shouldUseBottomToolbar = true
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }

        browserStore.dispatch(UpdateProgressAction(currentTab.id, 50))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            ProgressBarConfig(
                progress = 50,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    @Test
    fun `GIVEN a top toolbar WHEN the loading progress of the current tab changes THEN update the progress bar`() = runTest(testDispatcher) {
        settings.shouldUseBottomToolbar = false
        val currentTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }

        browserStore.dispatch(UpdateProgressAction(currentTab.id, 71))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            ProgressBarConfig(
                progress = 71,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    @Test
    fun `GIVEN the current page can be viewed in reader mode WHEN tapping on the reader mode button THEN show the reader mode UX`() {
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com")),
                selectedTabId = currentTab.id,
            ),
        )
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware)

        browserScreenStore.dispatch(
            ReaderModeStatusUpdated(
                ReaderModeStatus(
                    isAvailable = true,
                    isActive = false,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        val readerModeButton = toolbarStore.state.displayState.pageActionsEnd[0] as ActionButtonRes
        assertEquals(expectedReaderModeButton(false), readerModeButton)

        toolbarStore.dispatch(readerModeButton.onClick as BrowserToolbarEvent)
        verify { readerModeController.showReaderView() }
        assertNotNull(ReaderMode.opened.testGetValue())
    }

    @Test
    fun `GIVEN the current page is already viewed in reader mode WHEN tapping on the reader mode button THEN close the reader mode UX`() {
        val currentTab = createTab("test.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab, createTab("firefox.com")),
                selectedTabId = currentTab.id,
            ),
        )
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware)

        browserScreenStore.dispatch(
            ReaderModeStatusUpdated(
                ReaderModeStatus(
                    isAvailable = true,
                    isActive = true,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        val readerModeButton = toolbarStore.state.displayState.pageActionsEnd[0] as ActionButtonRes
        assertEquals(expectedReaderModeButton(true), readerModeButton)

        toolbarStore.dispatch(readerModeButton.onClick as BrowserToolbarEvent)
        verify { readerModeController.hideReaderView() }
        assertNotNull(ReaderMode.closed.testGetValue())
    }

    @Test
    fun `GIVEN on a wide window WHEN translation is possible THEN show a translate button`() {
        settings.shouldUseExpandedToolbar = false
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { true },
            isTallScreen = { false },
        )
        val toolbarStore = buildStore(middleware)

        browserScreenStore.dispatch(
            PageTranslationStatusUpdated(
                PageTranslationStatus(
                    isTranslationPossible = true,
                    isTranslated = false,
                    isTranslateProcessing = false,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        val translateButton = toolbarStore.state.displayState.pageActionsEnd[0]
        assertEquals(expectedTranslateButton(source = Source.AddressBar.PageEnd), translateButton)
    }

    @Test
    fun `GIVEN the current page is translated AND a wide window WHEN knowing of this state THEN update the translate button to show this`() {
        settings.shouldUseExpandedToolbar = true
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { true },
            isTallScreen = { false },
        )
        val toolbarStore = buildStore(middleware)

        browserScreenStore.dispatch(
            PageTranslationStatusUpdated(
                PageTranslationStatus(
                    isTranslationPossible = true,
                    isTranslated = false,
                    isTranslateProcessing = false,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()
        var translateButton = toolbarStore.state.displayState.pageActionsEnd[0]
        assertEquals(expectedTranslateButton(source = Source.AddressBar.PageEnd), translateButton)

        browserScreenStore.dispatch(
            PageTranslationStatusUpdated(
                PageTranslationStatus(
                    isTranslationPossible = true,
                    isTranslated = true,
                    isTranslateProcessing = false,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()
        translateButton = toolbarStore.state.displayState.pageActionsEnd[0]
        assertEquals(
            expectedTranslateButton(isActive = true, source = Source.AddressBar.PageEnd),
            translateButton,
        )
    }

    @Test
    fun `GIVEN translation is possible WHEN tapping on the translate button THEN allow user to choose how to translate`() {
        settings.shouldUseExpandedToolbar = true
        val currentNavDestination: NavDestination = mockk {
            every { id } returns R.id.browserFragment
        }
        val navController: NavController = mockk(relaxed = true) {
            every { currentDestination } returns currentNavDestination
        }

        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            navController = navController,
            isWideScreen = { true },
            isTallScreen = { false },
        )
        val toolbarStore = buildStore(middleware)
        browserScreenStore.dispatch(
            PageTranslationStatusUpdated(
                PageTranslationStatus(
                    isTranslationPossible = true,
                    isTranslated = false,
                    isTranslateProcessing = false,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        val translateButton =
            toolbarStore.state.displayState.pageActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(translateButton.onClick as BrowserToolbarEvent)

        verify { appStore.dispatch(SnackbarDismissed) }
        verify { navController.navigate(BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment()) }
        assertEquals(
            "main_flow_toolbar",
            Translations.action.testGetValue()?.last()?.extra?.get("item"),
        )
    }

    @Test
    fun `GIVEN on a small screen with tabstrip is disabled and not using the extended layout THEN don't show a share button as page end action`() {
        settings.isTabStripEnabled = false
        settings.shouldUseExpandedToolbar = false

        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(appStore, browserScreenStore)
        val toolbarStore = buildStore(middleware)

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
    }

    @Test
    fun `GIVEN on a wide screen with tabstrip is disabled THEN show a share button as page end action`() {
        settings.isTabStripEnabled = false
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val shareButton = toolbarStore.state.displayState.pageActionsEnd[0]
        assertEquals(expectedShareButton(source = Source.AddressBar.PageEnd), shareButton)
    }

    @Test
    fun `GIVEN on a large screen with tabstrip is enabled THEN don't show a share button as page end action`() {
        settings.isTabStripEnabled = true
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(appStore, browserScreenStore)
        val toolbarStore = buildStore(middleware)

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
    }

    @Test
    fun `GIVEN the current tab shows a content page WHEN the share shortcut is clicked THEN record telemetry and start sharing the local resource`() = runTest(testDispatcher) {
        settings.isTabStripEnabled = true
        settings.shouldUseExpandedToolbar = false
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.SHARE.value
        val browserScreenStore = buildBrowserScreenStore()
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val currentTab = createTab("content://test", private = false)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
            middleware = listOf(captureMiddleware),
        )
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val shareButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedShareButton(), shareButton)

        toolbarStore.dispatch(shareButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        captureMiddleware.assertLastAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(currentTab.id, it.tabId)
            assertEquals(ShareResourceState.LocalResource(currentTab.content.url, INTENT_TYPE_PDF), it.resource)
        }
    }

    @Test
    fun `GIVEN the current tab shows a remote PDF WHEN the share shortcut is clicked THEN record telemetry and start sharing the remote resource`() {
        settings.isTabStripEnabled = true
        settings.shouldUseExpandedToolbar = false
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.SHARE.value
        val browserScreenStore = buildBrowserScreenStore()
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val currentTab = createTab("https://mozilla.org/document.pdf", private = false).let {
            it.copy(content = it.content.copy(isPdf = true))
        }
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
            middleware = listOf(captureMiddleware),
        )
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val shareButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedShareButton(), shareButton)

        toolbarStore.dispatch(shareButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        captureMiddleware.assertLastAction(ShareResourceAction.AddShareAction::class) {
            assertEquals(currentTab.id, it.tabId)
            assertEquals(
                ShareResourceState.InternetResource(
                    url = currentTab.content.url,
                    contentType = INTENT_TYPE_PDF,
                    private = false,
                    referrerUrl = currentTab.content.url,
                ),
                it.resource,
            )
        }
    }

    @Test
    fun `GIVEN the current tab shows a normal webpage WHEN the share shortcut is clicked THEN record telemetry and open the share dialog`() {
        settings.isTabStripEnabled = true
        settings.shouldUseExpandedToolbar = false
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.SHARE.value
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.navigate(any<NavDirections>(), null) } just Runs
        val browserScreenStore = buildBrowserScreenStore()
        val currentTab = createTab("test.com", private = false)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val shareButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedShareButton(), shareButton)

        toolbarStore.dispatch(shareButton.onClick as BrowserToolbarEvent)
        verify {
            navController.navigate(
                directions = directionsEq(
                    BrowserFragmentDirections.actionGlobalShareFragment(
                        sessionId = currentTab.id,
                        data = arrayOf(
                            ShareData(
                                url = currentTab.content.url,
                                title = currentTab.content.title,
                            ),
                        ),
                        showPage = true,
                    ),
                ),
                navOptions = null,
            )
        }
    }

    @Test
    fun `GIVEN on a small width with tabstrip is enabled and not using the extended layout THEN don't show a share button as browser end action`() {
        settings.shouldUseExpandedToolbar = false
        settings.isTabStripEnabled = true
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { false },
            isTallScreen = { false },
        )
        val toolbarStore = buildStore(middleware)

        assertEquals(3, toolbarStore.state.displayState.browserActionsEnd.size)
        val newTabButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN expanded toolbar with tabstrip and tall window WHEN changing to short window THEN show new tab, tab counter and menu`() = runTest(testDispatcher) {
        settings.isTabStripEnabled = true
        settings.shouldUseExpandedToolbar = true
        val browserScreenStore = buildBrowserScreenStore()
        var isWideScreen = false
        var isTallScreen = true
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { isWideScreen },
            isTallScreen = { isTallScreen },
        )
        val toolbarStore = buildStore(middleware)

        var navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(5, navigationActions.size)
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)

        isTallScreen = false
        appStore.dispatch(AppAction.OrientationChange(Portrait))
        testDispatcher.scheduler.advanceUntilIdle()

        navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(0, navigationActions.size)
        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
        val newTabButton = toolbarBrowserActions[0] as ActionButtonRes
        val tabCounterButton = toolbarBrowserActions[1] as TabCounterAction
        val menuButton = toolbarBrowserActions[2] as ActionButtonRes
        assertEquals(expectedNewTabButton(), newTabButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN on a wide window with tabstrip and extended layout enabled THEN don't show a share button as browser end action`() {
        settings.isTabStripEnabled = true
        settings.shouldUseExpandedToolbar = true

        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(appStore, browserScreenStore)
        val toolbarStore = buildStore(middleware)

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
    }

    @Test
    fun `WHEN cycling through tall window and wide window THEN update what end page actions should be shown`() {
        val appStore = AppStore()
        settings.isTabStripEnabled = false
        settings.shouldUseExpandedToolbar = false
        val readerModeStatus: ReaderModeStatus = mockk(relaxed = true) {
            every { isAvailable } returns true
        }
        every { browserScreenState.readerModeStatus } returns readerModeStatus
        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
        }
        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus

        var middleware = buildMiddleware(appStore)
        var toolbarStore = buildStore(middleware)

        assertEquals(
            listOf(expectedReaderModeButton(false)),
            toolbarStore.state.displayState.pageActionsEnd,
        )

        middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { true },
            isTallScreen = { false },
        )
        toolbarStore = buildStore(middleware)

        assertEquals(
            listOf(
                expectedReaderModeButton(false),
                expectedTranslateButton(source = Source.AddressBar.PageEnd),
                expectedShareButton(source = Source.AddressBar.PageEnd),
            ),
            toolbarStore.state.displayState.pageActionsEnd,
        )

        middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { false },
            isTallScreen = { true },
        )
        toolbarStore = buildStore(middleware)

        assertEquals(
            listOf(expectedReaderModeButton(false)),
            toolbarStore.state.displayState.pageActionsEnd,
        )
    }

    @Test
    fun `GIVEN device has wide window WHEN a website is loaded THEN show navigation buttons`() = runTest(testDispatcher) {
        settings.shouldUseBottomToolbar = false
        val middleware = buildMiddleware(
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val displayGoBackButton = toolbarStore.state.displayState.browserActionsStart[0]
        assertEquals(displayGoBackButton, expectedGoBackButton(isActive = false, source = Source.AddressBar.BrowserStart))
        val displayGoForwardButton = toolbarStore.state.displayState.browserActionsStart[1]
        assertEquals(displayGoForwardButton, expectedGoForwardButton.copy(state = ActionButton.State.DISABLED))
    }

    @Test
    fun `GIVEN the back button is shown WHEN interacted with THEN go back or show history`() = runTest(testDispatcher) {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        settings.shouldUseBottomToolbar = false
        val currentTab = createTab("test.com", private = false)
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val engine = mockk<Engine>(relaxed = true)
        val session = mockk<EngineSession>(relaxed = true)
        every { engine.createSession() } returns session
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
            middleware = listOf(captureMiddleware) + EngineMiddleware.create(engine),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
        toolbarStore.dispatch(backButton.onClick as BrowserToolbarEvent)
        captureMiddleware.assertLastAction(EngineAction.GoBackAction::class) {
            assertEquals(currentTab.id, it.tabId)
        }

        toolbarStore.dispatch(backButton.onLongClick as BrowserToolbarEvent)

        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalTabHistoryDialogFragment(null),
                null,
            )
        }
    }

    @Test
    fun `GIVEN a tab with a home screen story URL WHEN the back button is clicked THEN navigate to home`() = runTest(testDispatcher) {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        val currentTab = createTab(
            url = "https://story.test".markAsOpenedFromHomeScreen(),
            private = false,
        )
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
        toolbarStore.dispatch(backButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(),
                null,
            )
        }
    }

    @Test
    fun `GIVEN a tab with a stories screen story URL WHEN the back button is clicked THEN navigate to stories fragment`() = runTest(testDispatcher) {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        val currentTab = createTab(
            url = "https://story.test".markAsOpenedFromStoriesScreen(),
            private = false,
        )
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
        toolbarStore.dispatch(backButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()

        verify {
            navController.navigate(
                directionsEq(BrowserFragmentDirections.actionBrowserFragmentToStoriesFragment()),
                null,
            )
        }
    }

    @Test
    fun `GIVEN a tab with a home screen story URL WHEN the back button is clicked AND home is on the back stack THEN pop back to home`() = runTest(testDispatcher) {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns true
        val currentTab = createTab(
            url = "https://story.test".markAsOpenedFromHomeScreen(),
            private = false,
        )
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
        toolbarStore.dispatch(backButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()

        verify { navController.popBackStack(R.id.homeFragment, false) }
        verify(exactly = 0) {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(),
                null,
            )
        }
    }

    @Test
    fun `GIVEN a tab with a stories screen story URL WHEN the back button is clicked AND stories is on the back stack THEN pop back to stories`() = runTest(testDispatcher) {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.storiesFragment, false) } returns true
        val currentTab = createTab(
            url = "https://story.test".markAsOpenedFromStoriesScreen(),
            private = false,
        )
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
        toolbarStore.dispatch(backButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()

        verify { navController.popBackStack(R.id.storiesFragment, false) }
        verify(exactly = 0) {
            navController.navigate(
                directionsEq(BrowserFragmentDirections.actionBrowserFragmentToStoriesFragment()),
                null,
            )
        }
    }

    @Test
    fun `GIVEN the forward button is shown WHEN interacted with THEN go forward or show history`() = runTest(testDispatcher) {
        settings.shouldUseBottomToolbar = false
        val currentTab = createTab("test.com", private = false)
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
            middleware = listOf(captureMiddleware) + EngineMiddleware.create(mockk()),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val forwardButton = toolbarStore.state.displayState.browserActionsStart[1] as ActionButtonRes
        toolbarStore.dispatch(forwardButton.onClick as BrowserToolbarEvent)
        captureMiddleware.assertLastAction(EngineAction.GoForwardAction::class) {
            assertEquals(currentTab.id, it.tabId)
        }

        toolbarStore.dispatch(forwardButton.onLongClick as BrowserToolbarEvent)
        navController.navigate(BrowserFragmentDirections.actionGlobalTabHistoryDialogFragment(null))
    }

    @Test
    fun `GIVEN device has wide window WHEN a website is loaded THEN show refresh button`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentNavDestination: NavDestination = mockk {
            every { id } returns R.id.browserFragment
        }
        val navController: NavController = mockk(relaxed = true) {
            every { currentDestination } returns currentNavDestination
        }

        val currentTab = createTab("test.com", private = false)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val reloadUseCases: SessionUseCases.ReloadUrlUseCase = mockk(relaxed = true)
        val stopUseCases: SessionUseCases.StopLoadingUseCase = mockk(relaxed = true)
        val sessionUseCases: SessionUseCases = mockk {
            every { reload } returns reloadUseCases
            every { stopLoading } returns stopUseCases
        }
        val browserScreenStore = buildBrowserScreenStore()
        val browserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val useCases: UseCases = mockk {
            every { fenixBrowserUseCases } returns browserUseCases
        }

        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            useCases = useCases,
            sessionUseCases = sessionUseCases,
            navController = navController,
            browsingModeManager = browsingModeManager,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val loadUrlFlagsUsed = mutableListOf<LoadUrlFlags>()

        val pageLoadButton = toolbarStore.state.displayState.browserActionsStart.last() as ActionButtonRes
        assertEquals(expectedRefreshButton, pageLoadButton)
        toolbarStore.dispatch(pageLoadButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.none().value, loadUrlFlagsUsed.first().value)
        toolbarStore.dispatch(pageLoadButton.onLongClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.BYPASS_CACHE, loadUrlFlagsUsed.last().value)
    }

    @Test
    fun `GIVEN device have a wide window WHEN a website is loaded THEN show refresh button`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentNavDestination: NavDestination = mockk {
            every { id } returns R.id.browserFragment
        }
        val navController: NavController = mockk(relaxed = true) {
            every { currentDestination } returns currentNavDestination
        }

        val currentTab = createTab("test.com", private = false)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val reloadUseCases: SessionUseCases.ReloadUrlUseCase = mockk(relaxed = true)
        val stopUseCases: SessionUseCases.StopLoadingUseCase = mockk(relaxed = true)
        val sessionUseCases: SessionUseCases = mockk {
            every { reload } returns reloadUseCases
            every { stopLoading } returns stopUseCases
        }
        val browserScreenStore = buildBrowserScreenStore()
        val browserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val useCases: UseCases = mockk {
            every { fenixBrowserUseCases } returns browserUseCases
        }
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            useCases = useCases,
            sessionUseCases = sessionUseCases,
            navController = navController,
            browsingModeManager = browsingModeManager,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val loadUrlFlagsUsed = mutableListOf<LoadUrlFlags>()

        val pageLoadButton =
            toolbarStore.state.displayState.browserActionsStart.last() as ActionButtonRes
        assertEquals(expectedRefreshButton, pageLoadButton)
        toolbarStore.dispatch(pageLoadButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.none().value, loadUrlFlagsUsed.first().value)
        toolbarStore.dispatch(pageLoadButton.onLongClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.BYPASS_CACHE, loadUrlFlagsUsed.last().value)
    }

    @Test
    fun `GIVEN a loaded tab WHEN the refresh button is pressed THEN show stop refresh button`() = runTest(testDispatcher) {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val currentNavDestination: NavDestination = mockk {
            every { id } returns R.id.browserFragment
        }
        val navController: NavController = mockk(relaxed = true) {
            every { currentDestination } returns currentNavDestination
        }
        settings.shouldUseBottomToolbar = false
        val currentTab = createTab("test.com", private = false)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            ),
        )
        val reloadUseCases: SessionUseCases.ReloadUrlUseCase = mockk(relaxed = true)
        val stopUseCases: SessionUseCases.StopLoadingUseCase = mockk(relaxed = true)
        val sessionUseCases: SessionUseCases = mockk {
            every { reload } returns reloadUseCases
            every { stopLoading } returns stopUseCases
        }
        val browserScreenStore = buildBrowserScreenStore()
        val browserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val useCases: UseCases = mockk {
            every { fenixBrowserUseCases } returns browserUseCases
        }
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            browserStore = browserStore,
            useCases = useCases,
            sessionUseCases = sessionUseCases,
            navController = navController,
            browsingModeManager = browsingModeManager,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val loadUrlFlagsUsed = mutableListOf<LoadUrlFlags>()

        var pageLoadButton = toolbarStore.state.displayState.browserActionsStart.last() as ActionButtonRes
        assertEquals(expectedRefreshButton, pageLoadButton)
        toolbarStore.dispatch(pageLoadButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.none().value, loadUrlFlagsUsed.first().value)
        toolbarStore.dispatch(pageLoadButton.onLongClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { reloadUseCases(currentTab.id, capture(loadUrlFlagsUsed)) }
        assertEquals(LoadUrlFlags.BYPASS_CACHE, loadUrlFlagsUsed.last().value)

        browserStore.dispatch(UpdateLoadingStateAction(currentTab.id, true))
        testDispatcher.scheduler.advanceUntilIdle()
        pageLoadButton = toolbarStore.state.displayState.browserActionsStart.last() as ActionButtonRes
        assertEquals(expectedStopButton, pageLoadButton)
        toolbarStore.dispatch(pageLoadButton.onClick as BrowserToolbarEvent)
        testDispatcher.scheduler.advanceUntilIdle()
        verify { stopUseCases(currentTab.id) }

        browserStore.dispatch(UpdateLoadingStateAction(currentTab.id, false))
        testDispatcher.scheduler.advanceUntilIdle()
        pageLoadButton = toolbarStore.state.displayState.browserActionsStart.last() as ActionButtonRes
        assertEquals(expectedRefreshButton, pageLoadButton)
    }

    @Test
    fun `GIVEN the url if of a local file WHEN initializing the toolbar THEN add an appropriate security indicator`() = runTest(testDispatcher) {
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            useCases = useCases,
        )
        every { tab.content.url } returns "content://test"
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_page_portrait_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = StartPageActions.SiteInfoClicked,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0] as ActionButtonRes
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is secure WHEN initializing the toolbar THEN add an appropriate security indicator`() = runTest(testDispatcher) {
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            useCases = useCases,
        )
        every { tab.content.securityInfo } returns SecurityInfo.Secure()
        every { tab.trackingProtection.enabled } returns true
        every { tab.trackingProtection.ignoredOnTrackingProtection } returns false
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = StartPageActions.SiteInfoClicked,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0] as ActionButtonRes
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is unknown WHEN initializing the toolbar THEN add an appropriate security indicator`() = runTest(testDispatcher) {
        val middleware = buildMiddleware(
            browserStore = browserStore,
            useCases = useCases,
        )
        every { tab.content.securityInfo } returns SecurityInfo.Unknown
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_globe_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            state = ActionButton.State.DEFAULT,
            highlighted = false,
            onClick = object : BrowserToolbarEvent {},
        )

        val toolbarStore = buildStore(
            middleware = middleware,
        )

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0] as ActionButtonRes
        assertEquals(expectedSecurityIndicator.drawableResId, securityIndicator.drawableResId)
        assertEquals(expectedSecurityIndicator.contentDescription, securityIndicator.contentDescription)
        assertEquals(expectedSecurityIndicator.state, securityIndicator.state)
        assertEquals(expectedSecurityIndicator.highlighted, securityIndicator.highlighted)
        assertFalse(securityIndicator.onClick is StartPageActions.SiteInfoClicked)
        assertNull(securityIndicator.onLongClick)
    }

    @Test
    fun `GIVEN the website is insecure WHEN the connection becomes secure THEN update appropriate security indicator`() =
        runTest {
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = true,
                    ignoredOnTrackingProtection = false,
                ),
                securityInfo = SecurityInfo.Insecure(),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                useCases = useCases,
            )
            val expectedSecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val expectedInsecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val toolbarStore = buildStore(middleware).also {
                it.dispatch(BrowserToolbarAction.Init())
            }

            var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            var securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedInsecureIndicator, securityIndicator)

            browserStore.dispatch(UpdateSecurityInfoAction(tab.id, SecurityInfo.Secure()))
            testDispatcher.scheduler.advanceUntilIdle()
            toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedSecureIndicator, securityIndicator)
        }

    @Test
    fun `GIVEN the tabSessionState has tracking protection disabled THEN show appropriate security indicator`() =
        runTest {
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = false,
                    ignoredOnTrackingProtection = false,
                ),
                securityInfo = SecurityInfo.Insecure(),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                useCases = useCases,
            )
            val expectedInsecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val toolbarStore = buildStore(middleware).also {
                it.dispatch(BrowserToolbarAction.Init())
            }

            val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            val securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedInsecureIndicator, securityIndicator)
        }

    @Test
    fun `GIVEN the tabSessionState has tracking protection enabled WHEN tracking protection is disabled THEN show appropriate security indicator`() =
        runTest {
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = true,
                    ignoredOnTrackingProtection = false,
                ),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                useCases = useCases,
            )
            val expectedSecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val expectedInsecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val toolbarStore = buildStore(middleware).also {
                it.dispatch(BrowserToolbarAction.Init())
            }
            browserStore.dispatch(UpdateSecurityInfoAction(tab.id, SecurityInfo.Secure()))
            testDispatcher.scheduler.advanceUntilIdle()
            val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            val securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedSecureIndicator, securityIndicator)
            browserStore.dispatch(TrackingProtectionAction.ToggleAction(tabId = tabId, enabled = false))
            testDispatcher.scheduler.advanceUntilIdle()
            val toolbarPageActions2 = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions2.size)
            val securityIndicator2 = toolbarPageActions2[0] as ActionButtonRes
            assertEquals(expectedInsecureIndicator, securityIndicator2)
        }

    @Test
    fun `GIVEN the tabSessionState has tracking protection disabled WHEN tracking protection is enabled THEN show appropriate security indicator`() =
        runTest {
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = false,
                    ignoredOnTrackingProtection = false,
                ),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                useCases = useCases,
            )
            val expectedSecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val expectedInsecureIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )
            val toolbarStore = buildStore(middleware).also {
                it.dispatch(BrowserToolbarAction.Init())
            }
            browserStore.dispatch(UpdateSecurityInfoAction(tab.id, SecurityInfo.Secure()))

            testDispatcher.scheduler.advanceUntilIdle()
            val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            val securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedInsecureIndicator, securityIndicator)
            browserStore.dispatch(TrackingProtectionAction.ToggleAction(tabId = tabId, enabled = true))

            testDispatcher.scheduler.advanceUntilIdle()
            val toolbarPageActions2 = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions2.size)
            val securityIndicator2 = toolbarPageActions2[0] as ActionButtonRes
            assertEquals(expectedSecureIndicator, securityIndicator2)
        }

    @Test
    fun `GIVEN ip protection is active WHEN initializing the toolbar THEN show animated pill with an overlay icon`() = runTest(testDispatcher) {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        every { tab.content.securityInfo } returns SecurityInfo.Secure()
        every { tab.trackingProtection.enabled } returns true
        every { tab.trackingProtection.ignoredOnTrackingProtection } returns false
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
            useCases = useCases,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_checkmark_24, siteInfo.iconResId)
        assertEquals(iconsR.drawable.mozac_ic_globe_24, siteInfo.overlayResId)
        assertEquals(R.string.ip_protection_toolbar_pill_label, siteInfo.textResId)
    }

    @Test
    fun `GIVEN ip protection is active and security is unknown WHEN initializing THEN show animated pill with globe icon`() = runTest(testDispatcher) {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        every { tab.content.securityInfo } returns SecurityInfo.Unknown
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
            useCases = useCases,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_globe_24, siteInfo.iconResId)
    }

    @Test
    fun `GIVEN ip protection is active and site is insecure WHEN initializing THEN show animated pill with shield slash icon`() = runTest(testDispatcher) {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        every { tab.content.securityInfo } returns SecurityInfo.Insecure()
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
            useCases = useCases,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_slash_24, siteInfo.iconResId)
    }

    @Test
    fun `GIVEN ip protection is active and url is content WHEN initializing THEN show regular button`() = runTest(testDispatcher) {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        every { tab.content.url } returns "content://test"
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
            useCases = useCases,
        )

        val toolbarStore = buildStore(middleware)

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is ActionButtonRes)
    }

    @Test
    fun `GIVEN ip protection is active WHEN security changes from insecure to secure THEN pill remains with updated icon`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val tab = createTab(
            url = "URL",
            id = tabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Insecure(),
        )
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
            useCases = useCases,
        )
        val toolbarStore = buildStore(middleware)

        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_slash_24, siteInfo.iconResId)

        browserStore.dispatch(UpdateSecurityInfoAction(tab.id, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_checkmark_24, siteInfo.iconResId)
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN ip protection is not active WHEN it becomes active THEN update site info to animated pill`() = runTest {
            val ipProtectionStore = IPProtectionStore()
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = true,
                    ignoredOnTrackingProtection = false,
                ),
                securityInfo = SecurityInfo.Secure(),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                ipProtectionStore = ipProtectionStore,
                useCases = useCases,
            )
            val toolbarStore = buildStore(middleware)

            var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            assertTrue(toolbarPageActions[0] is ActionButtonRes)

            ipProtectionStore.dispatch(
                IPProtectionAction.EngineStateChanged(
                    StateInfo(
                        serviceState = StateInfo.SERVICE_STATE_READY,
                        proxyState = StateInfo.PROXY_STATE_ACTIVE,
                    ),
                ),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            assertTrue(toolbarPageActions[0] is AnimatedPillActionRes)
        }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN ip protection is active WHEN it becomes inactive THEN update site info to regular button`() = runTest {
            val ipProtectionStore = IPProtectionStore(
                initialState = IPProtectionState(proxyStatus = Authorized.Active),
            )
            val tab = createTab(
                url = "URL",
                id = tabId,
                trackingProtection = TrackingProtectionState(
                    enabled = true,
                    ignoredOnTrackingProtection = false,
                ),
                securityInfo = SecurityInfo.Secure(),
            )
            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )
            val middleware = buildMiddleware(
                browserStore = browserStore,
                ipProtectionStore = ipProtectionStore,
                useCases = useCases,
            )
            val toolbarStore = buildStore(middleware)

            var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            assertTrue(toolbarPageActions[0] is AnimatedPillActionRes)

            ipProtectionStore.dispatch(
                IPProtectionAction.EngineStateChanged(
                    StateInfo(
                        serviceState = StateInfo.SERVICE_STATE_READY,
                        proxyState = StateInfo.PROXY_STATE_READY,
                    ),
                ),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            assertTrue(toolbarPageActions[0] is ActionButtonRes)
        }

    @Test
    fun `GIVEN reader mode is available WHEN reader mode status updates THEN update appropriate security indicator`() =
        runTest {
            val readerModeStatus: ReaderModeStatus = mockk(relaxed = true) {
                every { isAvailable } returns true
                every { isActive } returns false
            }

            every { browserScreenState.readerModeStatus } returns readerModeStatus

            val tab = createTab(
                url = "URL",
                id = tabId,
                securityInfo = SecurityInfo.Insecure(),
            )

            val browserScreenStore = buildBrowserScreenStore()

            val browserStore = BrowserStore(
                BrowserState(
                    tabs = listOf(tab),
                    selectedTabId = tab.id,
                ),
            )

            val middleware = buildMiddleware(
                browserScreenStore = browserScreenStore,
                browserStore = browserStore,
            )
            val toolbarStore = buildStore(middleware).also {
                it.dispatch(BrowserToolbarAction.Init())
            }

            val expectedSecurityIndicator = ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                onClick = StartPageActions.SiteInfoClicked,
            )

            var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(1, toolbarPageActions.size)
            val securityIndicator = toolbarPageActions[0] as ActionButtonRes
            assertEquals(expectedSecurityIndicator, securityIndicator)

            browserScreenStore.dispatch(
                ReaderModeStatusUpdated(
                    ReaderModeStatus(
                        isAvailable = true,
                        isActive = true,
                    ),
                ),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
            assertEquals(0, toolbarPageActions.size)
        }

    @Test
    fun `GIVEN default state WHEN building NewTab action THEN returns NewTab ActionButton with DEFAULT state and no long-click`() {
        val middleware = buildMiddleware()
        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.NewTab,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_plus_24, result.drawableResId)
        assertEquals(R.string.home_screen_shortcut_open_new_tab_2, result.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, result.state)
        assertEquals(AddNewTab(Source.Unknown), result.onClick)
        assertNull(result.onLongClick)
    }

    @Test
    fun `GIVEN no history WHEN building Back action THEN returns DISABLED Back ActionButton with long-click`() {
        val middleware = buildMiddleware()
        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Back,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_back_24, result.drawableResId)
        assertEquals(R.string.browser_menu_back, result.contentDescription)
        assertEquals(ActionButton.State.DISABLED, result.state)
        assertEquals(NavigateBackClicked(Source.Unknown), result.onClick)
        assertEquals(NavigateBackLongClicked(Source.Unknown), result.onLongClick)
    }

    @Test
    fun `GIVEN can go back WHEN building Back action THEN returns DEFAULT Back ActionButton`() {
        val contentState: ContentState = mockk(relaxed = true) {
            every { canGoBack } returns true
        }

        val tabSessionState: TabSessionState = mockk(relaxed = true) {
            every { content } returns contentState
        }

        val browserState = BrowserState(
            tabs = listOf(tabSessionState),
            selectedTabId = tabSessionState.id,
        )

        val browserStore = BrowserStore(browserState)
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Back,
        ) as ActionButtonRes

        assertEquals(ActionButton.State.DEFAULT, result.state)
    }

    @Test
    fun `GIVEN not other websitge in tab history current URL is of an internally opened story WHEN configuring the back action THEN enable it`() {
        val currentTab = createTab("https://story.test".markAsOpenedFromHomeScreen())
        val browserState = BrowserState(
            tabs = listOf(currentTab),
            selectedTabId = currentTab.id,
        )
        val browserStore = BrowserStore(browserState)
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )

        val result = middleware.buildAction(toolbarAction = ToolbarAction.Back) as ActionButtonRes

        assertEquals(ActionButton.State.DEFAULT, result.state)
    }

    @Test
    fun `GIVEN no history WHEN building Forward action THEN returns DISABLED Forward ActionButton with long-click`() {
        val middleware = buildMiddleware()
        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Forward,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_forward_24, result.drawableResId)
        assertEquals(R.string.browser_menu_forward, result.contentDescription)
        assertEquals(ActionButton.State.DISABLED, result.state)
        assertEquals(NavigateForwardClicked, result.onClick)
        assertEquals(NavigateForwardLongClicked, result.onLongClick)
    }

    @Test
    fun `GIVEN can go forward WHEN building Forward action THEN returns DEFAULT Forward ActionButton`() {
        val contentState: ContentState = mockk(relaxed = true) {
            every { canGoForward } returns true
        }

        val tabSessionState: TabSessionState = mockk(relaxed = true) {
            every { content } returns contentState
        }

        val browserState = BrowserState(
            tabs = listOf(tabSessionState),
            selectedTabId = tabSessionState.id,
        )

        val browserStore = BrowserStore(browserState)
        val middleware = buildMiddleware(browserStore = browserStore)

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Forward,
        ) as ActionButtonRes

        assertEquals(ActionButton.State.DEFAULT, result.state)
    }

    @Test
    fun `GIVEN not loading WHEN building RefreshOrStop action THEN returns Refresh ActionButton with both clicks`() {
        val contentState: ContentState = mockk(relaxed = true) {
            every { loading } returns false
        }

        val tabSessionState: TabSessionState = mockk(relaxed = true) {
            every { content } returns contentState
        }

        val browserState = BrowserState(
            tabs = listOf(tabSessionState),
            selectedTabId = tabSessionState.id,
        )

        val browserStore = BrowserStore(browserState)
        val middleware = buildMiddleware(browserStore = browserStore)

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.RefreshOrStop,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_arrow_clockwise_24, result.drawableResId)
        assertEquals(R.string.browser_menu_refresh, result.contentDescription)
        assertEquals(RefreshClicked(bypassCache = false), result.onClick)
        assertEquals(RefreshClicked(bypassCache = true), result.onLongClick)
    }

    @Test
    fun `GIVEN loading WHEN building RefreshOrStop action THEN returns Stop ActionButton`() {
        val contentState: ContentState = mockk(relaxed = true) {
            every { loading } returns true
        }

        val tabSessionState: TabSessionState = mockk(relaxed = true) {
            every { content } returns contentState
        }

        val browserState = BrowserState(
            tabs = listOf(tabSessionState),
            selectedTabId = tabSessionState.id,
        )

        val browserStore = BrowserStore(browserState)
        val middleware = buildMiddleware(browserStore = browserStore)

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.RefreshOrStop,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_cross_24, result.drawableResId)
        assertEquals(R.string.browser_menu_stop, result.contentDescription)
        assertEquals(StopRefreshClicked, result.onClick)
        assertNull(result.onLongClick)
    }

    @Test
    fun `GIVEN default state WHEN building Menu action THEN returns Menu ActionButton without long-click`() {
        val middleware = buildMiddleware()
        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Menu,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_ellipsis_vertical_24, result.drawableResId)
        assertEquals(R.string.content_description_menu, result.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, result.state)
        assertEquals(MenuClicked(Source.Unknown), result.onClick)
        assertNull(result.onLongClick)
    }

    @Test
    fun `GIVEN reader mode inactive WHEN building ReaderMode action THEN returns DEFAULT ReaderMode ActionButton`() {
        val readerModeStatus: ReaderModeStatus = mockk(relaxed = true) {
            every { isAvailable } returns false
            every { isActive } returns false
        }

        every { browserScreenState.readerModeStatus } returns readerModeStatus
        val middleware = buildMiddleware()

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.ReaderMode,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_reader_view_24, result.drawableResId)
        assertEquals(R.string.browser_menu_read, result.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, result.state)
        assertEquals(ReaderModeClicked(false), result.onClick)
    }

    @Test
    fun `GIVEN reader mode active WHEN building ReaderMode action THEN returns ACTIVE ReaderMode ActionButton`() {
        val readerModeStatus: ReaderModeStatus = mockk(relaxed = true) {
            every { isAvailable } returns true
            every { isActive } returns true
        }

        every { browserScreenState.readerModeStatus } returns readerModeStatus
        val middleware = buildMiddleware()

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.ReaderMode,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_reader_view_fill_24, result.drawableResId)
        assertEquals(R.string.browser_menu_read_close, result.contentDescription)
        assertEquals(ActionButton.State.ACTIVE, result.state)
        assertEquals(ReaderModeClicked(true), result.onClick)
    }

    @Test
    fun `GIVEN translation not done WHEN building Translate action THEN returns DEFAULT Translate ActionButton`() {
        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns false
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus
        val middleware = buildMiddleware()

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Translate,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_translate_24, result.drawableResId)
        assertEquals(R.string.browser_toolbar_translate, result.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, result.state)
        assertEquals(TranslateClicked(Source.Unknown), result.onClick)
    }

    @Test
    fun `GIVEN already translated WHEN building Translate action THEN returns ACTIVE Translate ActionButton`() {
        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns true
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus
        val middleware = buildMiddleware()

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Translate,
        ) as ActionButtonRes

        assertEquals(ActionButton.State.ACTIVE, result.state)
    }

    @Test
    fun `GIVEN tabsCount set WHEN building TabCounter action THEN returns TabCounterAction with correct count`() {
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://www.mozilla.org"),
                    createTab(id = "b", url = "https://www.firefox.com"),
                    createTab(id = "c", url = "https://getpocket.com"),
                ),
            ),
        )

        val middleware = buildMiddleware(browserStore = browserStore)
        buildStore(middleware)

        val action = middleware.buildAction(
            toolbarAction = ToolbarAction.TabCounter,
        ) as TabCounterAction

        assertEquals(3, action.count)
        assertEquals(
            testContext.getString(tabcounterR.string.mozac_tab_counter_open_tab_tray, 3),
            action.contentDescription,
        )
        assertFalse(action.showPrivacyMask)
        assertEquals(TabCounterClicked(Source.Unknown), action.onClick)
        assertNotNull(action.onLongClick)
    }

    @Test
    fun `GIVEN in expanded mode WHEN THEN no browser end actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true

        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)
    }

    @Test
    fun `WHEN building EditBookmark action THEN returns Bookmark ActionButton with correct icon`() {
        val middleware = buildMiddleware()
        buildStore(middleware)

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.EditBookmark,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_bookmark_fill_24, result.drawableResId)
        assertEquals(R.string.browser_menu_edit_bookmark, result.contentDescription)
        assertEquals(EditBookmarkClicked(Source.Unknown), result.onClick)
    }

    @Test
    fun `WHEN building Bookmark action THEN returns Bookmark ActionButton with correct icon`() {
        val middleware = buildMiddleware()
        buildStore(middleware)

        val result = middleware.buildAction(
            toolbarAction = ToolbarAction.Bookmark,
        ) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_bookmark_24, result.drawableResId)
        assertEquals(R.string.browser_menu_bookmark_this_page_2, result.contentDescription)
        assertEquals(AddBookmarkClicked(Source.Unknown), result.onClick)
    }

    @Test
    fun `WHEN initializing the navigation bar AND should not use simple toolbar THEN add navigation bar actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true

        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)

        val navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(5, navigationActions.size)
        val bookmarkButton = navigationActions[0] as ActionButtonRes
        val shareButton = navigationActions[1] as ActionButtonRes
        val newTabButton = navigationActions[2] as ActionButtonRes
        val tabCounterButton = navigationActions[3] as TabCounterAction
        val menuButton = navigationActions[4] as ActionButtonRes
        assertEquals(expectedBookmarkButton(Source.NavigationBar), bookmarkButton)
        assertEquals(expectedShareButton(Source.NavigationBar), shareButton)
        assertEquals(expectedNewTabButton(Source.NavigationBar), newTabButton)
        assertEqualsTabCounterButton(
            expectedTabCounterButton(source = Source.NavigationBar),
            tabCounterButton,
        )
        assertEquals(expectedMenuButton(source = Source.NavigationBar), menuButton)
    }

    @Test
    fun `WHEN initializing the navigation bar AND should not use simple toolbar AND in short window THEN add no navigation bar actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true

        val middleware = buildMiddleware(
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(0, navigationActions.size)
    }

    @Test
    fun `WHEN should use expanded toolbar AND window is changing to short window THEN add no navigation bar actions`() = runTest(testDispatcher) {
        val appStore = AppStore(
            initialState = AppState(
                orientation = Portrait,
            ),
        )
        settings.shouldUseExpandedToolbar = true
        var isWideScreen = false
        var isTallScreen = true
        val middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { isWideScreen },
            isTallScreen = { isTallScreen },
        )
        val toolbarStore = buildStore(middleware)

        var navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(5, navigationActions.size)

        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)

        isWideScreen = true
        isTallScreen = false

        appStore.dispatch(AppAction.OrientationChange(Landscape))
        testDispatcher.scheduler.advanceUntilIdle()

        navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(0, navigationActions.size)

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(3, toolbarBrowserActions.size)
    }

    @Test
    fun `GIVEN current page is bookmarked WHEN initializing navigation bar THEN show ACTIVE EditBookmark button`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true

        val tab = createTab("https://example.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )

        coEvery { bookmarksStorage.getBookmarksWithUrl(tab.content.url) } returns Result.success(
            listOf(mockk(relaxed = true)),
        )

        val middleware = buildMiddleware(
            browserStore = browserStore,
            bookmarksStorage = bookmarksStorage,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        testDispatcher.scheduler.advanceUntilIdle()

        val navigationActions = toolbarStore.state.displayState.navigationActions
        val editButton = navigationActions.first() as ActionButtonRes

        assertEquals(expectedEditBookmarkButton(Source.NavigationBar), editButton)
    }

    @Test
    fun `GIVEN the menu button is not highlighted WHEN a menu item is highlighted THEN highlight menu button`() = runTest(testDispatcher) {
        val appStore = AppStore()
        val middleware = buildMiddleware(appStore = appStore)
        val toolbarStore = buildStore(middleware)

        val initialMenuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedMenuButton(), initialMenuButton)

        appStore.dispatch(
            AppAction.MenuNotification.AddMenuNotification(
                SupportedMenuNotifications.Downloads,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()
        val updatedMenuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedMenuButton(true), updatedMenuButton)
    }

    @Test
    fun `GIVEN the menu button is highlighted WHEN no menu item is highlighted THEN remove highlight from menu button`() = runTest(testDispatcher) {
        val appStore = AppStore(
            initialState = AppState(
                supportedMenuNotifications = setOf(SupportedMenuNotifications.Downloads),
            ),
        )
        val middleware = buildMiddleware(appStore = appStore)
        val toolbarStore = buildStore(middleware)

        val initialMenuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedMenuButton(true), initialMenuButton)

        appStore.dispatch(
            AppAction.MenuNotification.RemoveMenuNotification(
                SupportedMenuNotifications.Downloads,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()
        val updatedMenuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedMenuButton(), updatedMenuButton)
    }

    @Test
    fun `GIVEN site permissions different than default WHEN observing THEN SiteInfo button is highlighted`() = runTest(testDispatcher) {
        val currentTab = createTab(
            url = "example.com",
            private = false,
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(tabs = listOf(currentTab), selectedTabId = currentTab.id),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }
        testDispatcher.scheduler.advanceUntilIdle()

        var siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(!siteInfo.highlighted)

        browserStore.dispatch(
            ContentAction.UpdatePermissionHighlightsStateAction.NotificationChangedAction(
                tabId = currentTab.id,
                value = true,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(siteInfo.highlighted)
    }

    @Test
    fun `GIVEN no custom site permissions WHEN observing THEN SiteInfo button is NOT highlighted`() = runTest(testDispatcher) {
        val currentTab = createTab("example.com", private = false)
        val browserStore = BrowserStore(
            BrowserState(tabs = listOf(currentTab), selectedTabId = currentTab.id),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }
        testDispatcher.scheduler.advanceUntilIdle()

        val siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(!siteInfo.highlighted)
    }

    @Test
    fun `GIVEN tracking protection ignored WHEN observing THEN SiteInfo button is highlighted`() = runTest(testDispatcher) {
        val currentTab = createTab(
            url = "https://example.com",
            private = false,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = true,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(tabs = listOf(currentTab), selectedTabId = currentTab.id),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }
        testDispatcher.scheduler.advanceUntilIdle()

        val siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(siteInfo.highlighted)
    }

    @Test
    fun `GIVEN tracking protection not ignored WHEN it becomes ignored THEN SiteInfo button becomes highlighted`() = runTest(testDispatcher) {
        val currentTab = createTab(
            url = "https://example.com",
            private = false,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(tabs = listOf(currentTab), selectedTabId = currentTab.id),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }
        testDispatcher.scheduler.advanceUntilIdle()

        var siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(!siteInfo.highlighted)

        browserStore.dispatch(
            TrackingProtectionAction.ToggleExclusionListAction(
                tabId = currentTab.id,
                excluded = true,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(siteInfo.highlighted)
    }

    @Test
    fun `GIVEN tracking protection ignored WHEN it is no longer ignored THEN SiteInfo button is NOT highlighted`() = runTest(testDispatcher) {
        val currentTab = createTab(
            url = "https://example.com",
            private = false,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = true,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(tabs = listOf(currentTab), selectedTabId = currentTab.id),
        )
        val middleware = buildMiddleware(browserStore = browserStore)
        val toolbarStore = buildStore(middleware).also {
            it.dispatch(BrowserToolbarAction.Init())
        }
        testDispatcher.scheduler.advanceUntilIdle()

        var siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(siteInfo.highlighted)

        browserStore.dispatch(
            TrackingProtectionAction.ToggleExclusionListAction(
                tabId = currentTab.id,
                excluded = false,
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        siteInfo = toolbarStore.state.displayState.pageActionsStart.first() as ActionButtonRes
        assertTrue(!siteInfo.highlighted)
    }

    @Test
    fun `GIVEN share shortcut is selected THEN update end page actions without share action`() = runTest(testDispatcher) {
        settings.isTabStripEnabled = false
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.SHARE.value
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val endPageActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(emptyList<Action>(), endPageActions)
    }

    @Test
    fun `GIVEN translate shortcut is selected THEN update end page actions without translate action`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.TRANSLATE.value
        val browserScreenStore = buildBrowserScreenStore()
        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        browserScreenStore.dispatch(
            PageTranslationStatusUpdated(
                PageTranslationStatus(
                    isTranslationPossible = true,
                    isTranslated = false,
                    isTranslateProcessing = false,
                ),
            ),
        )

        val translateButton = toolbarStore.state.displayState.pageActionsEnd[0]
        assertNotEquals(expectedTranslateButton(source = Source.AddressBar.PageEnd), translateButton)
        assertEquals(expectedShareButton(source = Source.AddressBar.PageEnd), translateButton)
    }

    @Test
    fun `WHEN clicking the homepage button THEN navigate to application's home screen`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.HOMEPAGE.value

        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val homepageButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(homepageButton.onClick as BrowserToolbarEvent)

        verify { navController.navigate(BrowserFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN homepage as new tab is enabled WHEN clicking the homepage button THEN navigate to homepage`() = runTest(testDispatcher) {
        settings.enableHomepageAsNewTab = true
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.HOMEPAGE.value

        val middleware = buildMiddleware()
        val toolbarStore = buildStore(middleware)
        val newTabButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

        verify { useCases.fenixBrowserUseCases.navigateToHomepage() }
    }

    @Test
    fun `GIVEN expanded toolbar is used and navbar is hidden WHEN building end browser actions THEN use simple toolbar shortcuts`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.shouldUseExpandedToolbar = true
        settings.toolbarSimpleShortcutKey = ShortcutType.HOMEPAGE.value

        val middleware = buildMiddleware(
            browserScreenStore = browserScreenStore,
            isTallScreen = { false },
            isWideScreen = { true },
        )
        val toolbarStore = buildStore(middleware)

        val homepageButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedHomepageButton(), homepageButton)
    }

    @Test
    fun `GIVEN simple toolbar use add bookmark shortcut AND the current page is not bookmarked WHEN initializing toolbar THEN show Bookmark in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.BOOKMARK.value
        val toolbarStore = buildStore()

        val bookmarkButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedBookmarkButton(), bookmarkButton)
    }

    @Test
    fun `GIVEN simple toolbar use add bookmark shortcut AND the current page is bookmarked WHEN initializing toolbar THEN show ACTIVE EditBookmark in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.BOOKMARK.value

        val tab = createTab("https://example.com")
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )

        coEvery { bookmarksStorage.getBookmarksWithUrl(tab.content.url) } returns Result.success(
            listOf(mockk(relaxed = true)),
        )

        val middleware = buildMiddleware(
            browserStore = browserStore,
            bookmarksStorage = bookmarksStorage,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val editButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedEditBookmarkButton(), editButton)
    }

    @Test
    fun `GIVEN simple toolbar use translate shortcut AND current page is not translated WHEN initializing toolbar THEN show Translate in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.TRANSLATE.value

        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns false
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus

        val toolbarStore = buildStore()

        val translateButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedTranslateButton(), translateButton)
    }

    @Test
    fun `GIVEN simple toolbar use translate shortcut AND current page is translated WHEN initializing toolbar THEN show ACTIVE Translate in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.TRANSLATE.value

        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns true
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus

        val toolbarStore = buildStore()

        val translateButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedTranslateButton(isActive = true), translateButton)
    }

    @Test
    fun `GIVEN simple toolbar use homepage shortcut WHEN initializing toolbar THEN show Homepage in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.HOMEPAGE.value

        val toolbarStore = buildStore()

        val homepageButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedHomepageButton(), homepageButton)
    }

    @Test
    fun `GIVEN simple toolbar use back shortcut AND current page has no history WHEN initializing toolbar THEN show DISABLED Back in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.BACK.value

        val toolbarStore = buildStore()

        val backButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedGoBackButton(isActive = false), backButton)
    }

    @Test
    fun `GIVEN simple toolbar use back shortcut AND current page has history WHEN initializing toolbar THEN show ACTIVE Back in end browser actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.BACK.value

        val tab = createTab(url = "https://example.com").let {
            it.copy(content = it.content.copy(canGoBack = true))
        }
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val backButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        assertEquals(expectedGoBackButton(), backButton)
    }

    @Test
    fun `GIVEN simple toolbar use share shortcut AND wide window with tabstrip enabled WHEN initializing toolbar THEN only show one Share in end browser actions`() {
        settings.shouldShowToolbarCustomization = true
        settings.isTabStripEnabled = true
        settings.toolbarSimpleShortcutKey = ShortcutType.SHARE.value

        val middleware = buildMiddleware(
            appStore = appStore,
            isWideScreen = { true },
            isTallScreen = { false },
        )
        val toolbarStore = buildStore(middleware)

        assertEquals(3, toolbarStore.state.displayState.browserActionsEnd.size)
        val shareButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[1] as TabCounterAction
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[2] as ActionButtonRes
        assertEquals(expectedShareButton(), shareButton)
        assertEqualsTabCounterButton(expectedTabCounterButton(), tabCounterButton)
        assertNotEquals(expectedShareButton(), menuButton)
    }

    @Test
    fun `GIVEN simple toolbar set to use no custom shortcut WHEN initializing toolbar THEN don't show any custom shortcut in end browser actions`() = runTest {
        settings.shouldUseBottomToolbar = true
        settings.shouldShowToolbarCustomization = true
        settings.toolbarSimpleShortcutKey = ShortcutType.NONE.value

        val toolbarStore = buildStore()

        val browserEndActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, browserEndActions.size)
        assertEqualsTabCounterButton(expectedBottomTabCounterButton(), browserEndActions.first() as TabCounterAction)
        assertEquals(expectedMenuButton(), browserEndActions.last())
    }

    @Test
    fun `GIVEN expanded toolbar use translate shortcut AND current page is not translated WHEN initializing toolbar THEN show Translate in navigation actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.shouldUseExpandedToolbar = true
        settings.toolbarExpandedShortcutKey = ShortcutType.TRANSLATE.value

        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns false
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus

        val toolbarStore = buildStore()

        val translateButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
        assertEquals(expectedTranslateButton(source = Source.NavigationBar), translateButton)
    }

    @Test
    fun `GIVEN expanded toolbar use translate shortcut AND current page is translated WHEN initializing toolbar THEN show ACTIVE Translate in navigation actions`() = runTest(testDispatcher) {
        settings.shouldShowToolbarCustomization = true
        settings.shouldUseExpandedToolbar = true
        settings.toolbarExpandedShortcutKey = ShortcutType.TRANSLATE.value

        val pageTranslationStatus: PageTranslationStatus = mockk(relaxed = true) {
            every { isTranslationPossible } returns true
            every { isTranslated } returns true
            every { isTranslateProcessing } returns false
        }

        every { browserScreenState.pageTranslationStatus } returns pageTranslationStatus

        val toolbarStore = buildStore()

        val translateButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
        assertEquals(expectedTranslateButton(isActive = true, source = Source.NavigationBar), translateButton)
    }

    @Test
    fun `GIVEN expanded toolbar use homepage shortcut WHEN initializing toolbar THEN show Homepage in navigation actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true
        settings.shouldShowToolbarCustomization = true
        settings.toolbarExpandedShortcutKey = ShortcutType.HOMEPAGE.value

        val toolbarStore = buildStore()

        val homepageButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
        assertEquals(expectedHomepageButton(source = Source.NavigationBar), homepageButton)
    }

    @Test
    fun `GIVEN expanded toolbar use back shortcut AND current page has no history WHEN initializing toolbar THEN show DISABLED Back in navigation actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true
        settings.shouldShowToolbarCustomization = true
        settings.toolbarExpandedShortcutKey = ShortcutType.BACK.value

        val toolbarStore = buildStore()

        val backButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
        assertEquals(expectedGoBackButton(isActive = false, source = Source.NavigationBar), backButton)
    }

    @Test
    fun `GIVEN expanded toolbar use back shortcut AND current page has history WHEN initializing toolbar THEN show ACTIVE Back in navigation actions`() = runTest(testDispatcher) {
        settings.shouldUseExpandedToolbar = true
        settings.shouldShowToolbarCustomization = true
        settings.toolbarExpandedShortcutKey = ShortcutType.BACK.value

        val tab = createTab(url = "https://example.com").let {
            it.copy(content = it.content.copy(canGoBack = true))
        }
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
                selectedTabId = tab.id,
            ),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val backButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
        assertEquals(expectedGoBackButton(source = Source.NavigationBar), backButton)
    }

    @Test
    fun `ShortcutType toToolbarAction maps shortcuts`() = runTest(testDispatcher) {
        val middleware = buildMiddleware()

        val newTab = with(middleware) { ShortcutType.NEW_TAB.toToolbarAction() }
        val share = with(middleware) { ShortcutType.SHARE.toToolbarAction() }
        val translate = with(middleware) { ShortcutType.TRANSLATE.toToolbarAction() }
        val homepage = with(middleware) { ShortcutType.HOMEPAGE.toToolbarAction() }
        val back = with(middleware) { ShortcutType.BACK.toToolbarAction() }

        assertEquals(ToolbarAction.NewTab, newTab)
        assertEquals(ToolbarAction.Share, share)
        assertEquals(ToolbarAction.Translate, translate)
        assertEquals(ToolbarAction.Homepage, homepage)
        assertEquals(ToolbarAction.Back, back)
    }

    private fun assertEqualsTabCounterButton(expected: TabCounterAction, actual: TabCounterAction) {
        assertEquals(expected.count, actual.count)
        assertEquals(expected.contentDescription, actual.contentDescription)
        assertEquals(expected.showPrivacyMask, actual.showPrivacyMask)
        assertEquals(expected.onClick, actual.onClick)
        when (expected.onLongClick) {
            null -> assertNull(actual.onLongClick)
            is BrowserToolbarEvent -> assertEquals(expected.onLongClick, actual.onLongClick)
            is BrowserToolbarMenu -> assertEquals(
                (expected.onLongClick as BrowserToolbarMenu).items(),
                (actual.onLongClick as BrowserToolbarMenu).items(),
            )
            is CombinedEventAndMenu -> {
                assertEquals(
                    (expected.onLongClick as CombinedEventAndMenu).event,
                    (actual.onLongClick as CombinedEventAndMenu).event,
                )
                assertEquals(
                    (expected.onLongClick as CombinedEventAndMenu).menu.items(),
                    (actual.onLongClick as CombinedEventAndMenu).menu.items(),
                )
            }
        }
    }

    private fun assertEqualsOrigin(expected: PageOrigin, actual: PageOrigin) {
        assertEquals(expected.hint, actual.hint)
        assertEquals(expected.url, actual.url.toString())
        assertEquals(expected.title, actual.title)
        assertEquals(expected.contextualMenuOptions, actual.contextualMenuOptions)
        assertEquals(expected.onClick, actual.onClick)
        assertEquals(expected.textGravity, actual.textGravity)
        assertEquals(expected.onLongClick, actual.onLongClick)
    }

    private val expectedRefreshButton = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_arrow_clockwise_24,
        contentDescription = R.string.browser_menu_refresh,
        state = ActionButton.State.DEFAULT,
        onClick = RefreshClicked(bypassCache = false),
        onLongClick = RefreshClicked(bypassCache = true),
    )

    private val expectedStopButton = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_cross_24,
        contentDescription = R.string.browser_menu_stop,
        state = ActionButton.State.DEFAULT,
        onClick = StopRefreshClicked,
    )

    private fun expectedReaderModeButton(isActive: Boolean = false) = ActionButtonRes(
        drawableResId = when (isActive) {
            true -> iconsR.drawable.mozac_ic_reader_view_fill_24
            false -> iconsR.drawable.mozac_ic_reader_view_24
        },
        contentDescription = when (isActive) {
            true -> R.string.browser_menu_read_close
            false -> R.string.browser_menu_read
        },
        state = when (isActive) {
            true -> ActionButton.State.ACTIVE
            false -> ActionButton.State.DEFAULT
        },
        onClick = ReaderModeClicked(isActive),
    )

    private val expectedGoForwardButton = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_forward_24,
        contentDescription = R.string.browser_menu_forward,
        state = ActionButton.State.ACTIVE,
        onClick = NavigateForwardClicked,
        onLongClick = NavigateForwardLongClicked,
    )

    private fun expectedGoBackButton(
        isActive: Boolean = true,
        source: Source = Source.AddressBar.BrowserEnd,
    ) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_back_24,
        contentDescription = R.string.browser_menu_back,
        state = when (isActive) {
            true -> ActionButton.State.DEFAULT
            false -> ActionButton.State.DISABLED
        },
        onClick = NavigateBackClicked(source),
        onLongClick = NavigateBackLongClicked(source),
    )

    private fun expectedTranslateButton(
        isActive: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_translate_24,
        contentDescription = R.string.browser_toolbar_translate,
        state = when (isActive) {
            true -> ActionButton.State.ACTIVE
            false -> ActionButton.State.DEFAULT
        },
        onClick = TranslateClicked(source),
    )

    private fun expectedTabCounterButton(
        tabCount: Int = 0,
        isPrivate: Boolean = false,
        shouldUseBottomToolbar: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) = TabCounterAction(
        count = tabCount,
        contentDescription = if (isPrivate) {
            testContext.getString(
                tabcounterR.string.mozac_tab_counter_private,
                tabCount.toString(),
            )
        } else {
            testContext.getString(
                tabcounterR.string.mozac_tab_counter_open_tab_tray,
                tabCount.toString(),
            )
        },
        showPrivacyMask = isPrivate,
        onClick = TabCounterClicked(source),
        onLongClick = CombinedEventAndMenu(TabCounterLongClicked(source)) {
            listOf(
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_tab),
                    contentDescription = StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_tab),
                    onClick = AddNewTab(source),
                ),
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_fill_24),
                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_private_tab),
                    contentDescription = StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_private_tab),
                    onClick = AddNewPrivateTab(source),
                ),
                BrowserToolbarMenuDivider,
                BrowserToolbarMenuButton(
                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_cross_24),
                    text = StringResText(tabcounterR.string.mozac_close_tab),
                    contentDescription = StringResContentDescription(tabcounterR.string.mozac_close_tab),
                    onClick = CloseCurrentTab,
                ),
            ).apply {
                if (shouldUseBottomToolbar) {
                    asReversed()
                }
            }
        },
    )

    fun expectedBottomTabCounterButton(
        tabCount: Int = 0,
        isPrivate: Boolean = false,
        shouldUseBottomToolbar: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) = expectedTabCounterButton(
        tabCount = tabCount,
        isPrivate = isPrivate,
        shouldUseBottomToolbar = shouldUseBottomToolbar,
        source = source,
    ).run {
        copy(
            onLongClick = (onLongClick as CombinedEventAndMenu).copy(
                menu = BrowserToolbarMenu { (onLongClick as CombinedEventAndMenu).menu.items().reversed() },
            ),
        )
    }

    private fun expectedNewTabButton(source: Source = Source.AddressBar.BrowserEnd) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_plus_24,
        contentDescription = R.string.home_screen_shortcut_open_new_tab_2,
        onClick = AddNewTab(source),
    )

    private fun expectedMenuButton(
        highlighted: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
        contentDescription = R.string.content_description_menu,
        highlighted = highlighted,
        onClick = MenuClicked(source),
    )

    private fun expectedBookmarkButton(source: Source = Source.AddressBar.BrowserEnd) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_bookmark_24,
        contentDescription = R.string.browser_menu_bookmark_this_page_2,
        onClick = AddBookmarkClicked(source),
    )

    private fun expectedEditBookmarkButton(source: Source = Source.AddressBar.BrowserEnd) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_bookmark_fill_24,
        contentDescription = R.string.browser_menu_edit_bookmark,
        onClick = EditBookmarkClicked(source),
        state = ActionButton.State.ACTIVE,
    )

    private fun expectedShareButton(source: Source = Source.AddressBar.BrowserEnd) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_share_android_24,
        contentDescription = R.string.browser_menu_share,
        onClick = ShareClicked(source),
    )

    private fun expectedHomepageButton(source: Source = Source.AddressBar.BrowserEnd) = ActionButtonRes(
        drawableResId = iconsR.drawable.mozac_ic_home_24,
        contentDescription = R.string.browser_menu_homepage,
        onClick = HomepageClicked(source),
    )

    private fun buildMiddleware(
        appStore: AppStore = this.appStore,
        browserScreenStore: BrowserScreenStore = this.browserScreenStore,
        browserStore: BrowserStore = this.browserStore,
        ipProtectionStore: IPProtectionStore = this.ipProtectionStore,
        permissionsStorage: SitePermissionsStorage = this.permissionsStorage,
        cookieBannersStorage: CookieBannersStorage = this.cookieBannersStorage,
        trackingProtectionUseCases: TrackingProtectionUseCases = this.trackingProtectionUseCases,
        bookmarksStorage: BookmarksStorage = this.bookmarksStorage,
        useCases: UseCases = this.useCases,
        sessionUseCases: SessionUseCases = SessionUseCases(browserStore),
        nimbusComponents: NimbusComponents = this.nimbusComponents,
        clipboard: ClipboardHandler = this.clipboard,
        publicSuffixList: PublicSuffixList = this.publicSuffixList,
        settings: Settings = this.settings,
        navController: NavController = this.navController,
        browsingModeManager: BrowsingModeManager = this.browsingModeManager,
        readerModeController: ReaderModeController = this.readerModeController,
        thumbnailsFeature: () -> BrowserThumbnails = { this.thumbnailsFeature },
        isWideScreen: () -> Boolean = { false },
        isTallScreen: () -> Boolean = { true },
        scope: CoroutineScope = testScope,
    ) = BrowserToolbarMiddleware(
        uiContext = testContext,
        appStore = appStore,
        browserScreenStore = browserScreenStore,
        browserStore = browserStore,
        ipProtectionStore = ipProtectionStore,
        permissionsStorage = permissionsStorage,
        cookieBannersStorage = cookieBannersStorage,
        bookmarksStorage = bookmarksStorage,
        trackingProtectionUseCases = trackingProtectionUseCases,
        useCases = useCases,
        nimbusComponents = nimbusComponents,
        clipboard = clipboard,
        publicSuffixList = publicSuffixList,
        settings = settings,
        navController = navController,
        browsingModeManager = browsingModeManager,
        readerModeController = readerModeController,
        thumbnailsFeature = thumbnailsFeature,
        isWideScreen = isWideScreen,
        isTallScreen = isTallScreen,
        sessionUseCases = sessionUseCases,
        scope = scope,
        ioDispatcher = testDispatcher,
    )

    private fun buildStore(
        middleware: BrowserToolbarMiddleware = buildMiddleware(),
    ) = BrowserToolbarStore(
        middleware = listOf(middleware),
    ).also {
        testDispatcher.scheduler.advanceUntilIdle() // to complete the initial setup happening in coroutines
    }

    private fun buildBrowserScreenStore(
        initialState: BrowserScreenState = BrowserScreenState(),
        middlewares: List<Middleware<BrowserScreenState, BrowserScreenAction>> = emptyList(),
    ) = BrowserScreenStore(
        initialState = initialState,
        middleware = middlewares,
    )

    private fun fakeSearchState() = SearchState(
        region = RegionState("US", "US"),
        regionSearchEngines = listOf(
            SearchEngine("engine-a", "Engine A", mockk(), type = SearchEngine.Type.BUNDLED),
            SearchEngine("engine-b", "Engine B", mockk(), type = SearchEngine.Type.BUNDLED),
        ),
        customSearchEngines = listOf(
            SearchEngine("engine-c", "Engine C", mockk(), type = SearchEngine.Type.CUSTOM),
        ),
        applicationSearchEngines = listOf(
            SearchEngine(TABS_SEARCH_ENGINE_ID, "Tabs", mockk(), type = SearchEngine.Type.APPLICATION),
            SearchEngine(BOOKMARKS_SEARCH_ENGINE_ID, "Bookmarks", mockk(), type = SearchEngine.Type.APPLICATION),
            SearchEngine(HISTORY_SEARCH_ENGINE_ID, "History", mockk(), type = SearchEngine.Type.APPLICATION),
        ),
        additionalSearchEngines = listOf(
            SearchEngine("engine-e", "Engine E", mockk(), type = SearchEngine.Type.BUNDLED_ADDITIONAL),
        ),
        additionalAvailableSearchEngines = listOf(
            SearchEngine("engine-f", "Engine F", mockk(), type = SearchEngine.Type.BUNDLED_ADDITIONAL),
        ),
        hiddenSearchEngines = listOf(
            SearchEngine("engine-g", "Engine G", mockk(), type = SearchEngine.Type.BUNDLED),
        ),
        regionDefaultSearchEngineId = null,
        userSelectedSearchEngineId = null,
        userSelectedSearchEngineName = null,
    )
}
