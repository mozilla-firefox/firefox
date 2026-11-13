/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.speech.RecognizerIntent
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.SearchAction.ApplicationSearchEnginesLoaded
import mozilla.components.browser.state.action.TabListAction.AddTabAction
import mozilla.components.browser.state.action.TabListAction.RemoveTabAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.search.SearchEngine.Type.APPLICATION
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TranslationsBrowserState
import mozilla.components.browser.state.state.content.HistoryState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.LoadFromClipboard
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.PasteFromClipboard
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
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.concept.engine.history.HistoryItem
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.ClipboardHandler
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.SimpleBrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEngineSelected
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.OrientationMode.Landscape
import org.mozilla.fenix.components.appstate.OrientationMode.Portrait
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequested
import org.mozilla.fenix.components.appstate.search.SearchState
import org.mozilla.fenix.components.appstate.search.SelectedSearchEngine
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.toolbar.BrowserToolbarMiddleware.HomeToolbarAction
import org.mozilla.fenix.home.toolbar.DisplayActions.FakeClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.PrivateModeClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.StoriesClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.VoiceSearchClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateBackClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateBackLongClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateForwardClicked
import org.mozilla.fenix.home.toolbar.NavigationInteractions.NavigateForwardLongClicked
import org.mozilla.fenix.home.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.search.fixtures.assertSearchSelectorEquals
import org.mozilla.fenix.search.fixtures.buildExpectedSearchSelector
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.utils.Settings
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class BrowserToolbarMiddlewareTest {
    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)

    @get:Rule val gleanRule = FenixGleanTestRule(testContext)

    private val browserStore =
        BrowserStore(BrowserState(translationEngine = TranslationsBrowserState(isEngineSupported = true)))
    private val browsingModeManager = SimpleBrowsingModeManager(Normal)

    // Translations are available by default: the engine is supported and the feature is enabled.
    private val translationsFeatureSettings = TranslationsEnabledSettings.inMemory(isEnabledInitial = true)
    private lateinit var appStore: AppStore

    private val aboutHomeTabId = "homeTabId"

    @Before
    fun setup() = runTest {
        appStore = spyk(AppStore())
        every { testContext.components.settings.shouldUseExpandedToolbar } returns false
        every { testContext.components.settings.isTabStripEnabled } returns false
        every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.BOOKMARK.value
        every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns false
        every { testContext.components.settings.shouldShowVoiceSearch } returns true
    }

    @Test
    fun `WHEN initializing the toolbar THEN add browser end actions`() = runTest {
        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `WHEN initializing the toolbar AND should use expanded toolbar THEN don't add browser end actions`() = runTest {
        every { testContext.components.settings.shouldUseExpandedToolbar } returns true

        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)
    }

    @Test
    fun `WHEN initializing the navigation bar AND should use expanded toolbar THEN add navigation bar actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            val navigationActions = toolbarStore.state.displayState.navigationActions
            assertEquals(5, navigationActions.size)
            val bookmarkButton = navigationActions[0] as ActionButtonRes
            val shareButton = navigationActions[1] as ActionButtonRes
            val newTabButton = navigationActions[2] as ActionButtonRes
            val tabCounterButton = navigationActions[3] as TabCounterAction
            val menuButton = navigationActions[4] as ActionButtonRes
            assertEquals(expectedBookmarkButton, bookmarkButton)
            assertEquals(expectedShareButton, shareButton)
            assertEquals(expectedNewTabButton(Source.NavigationBar), newTabButton)
            assertEqualsToolbarButton(
                expectedToolbarButton(source = Source.NavigationBar),
                tabCounterButton,
            )
            assertEquals(expectedMenuButton(source = Source.NavigationBar), menuButton)
        }

    @Test
    fun `WHEN initializing the navigation bar AND should use expanded toolbar AND window is short THEN add no navigation bar actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore(isWideScreen = { true })

            val navigationActions = toolbarStore.state.displayState.navigationActions
            assertEquals(0, navigationActions.size)

            val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
        }

    @Test
    fun `WHEN should use expanded toolbar AND window is changing to short window THEN add no navigation bar actions`() =
        runTest {
            val appStore = AppStore(initialState = AppState(orientation = Portrait))
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true

            var isWideScreen = false
            var isTallScreen = true
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    appStore = appStore,
                    isWideScreen = { isWideScreen },
                    isTallScreen = { isTallScreen },
                )

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
            assertEquals(2, toolbarBrowserActions.size)
        }

    @Test
    fun `GIVEN normal browsing mode WHEN initializing the toolbar THEN show the number of normal tabs in the tabs counter button`() =
        runTest {
            val browsingModeManager = SimpleBrowsingModeManager(Normal)
            val browserStore =
                BrowserStore(initialState = BrowserState(tabs = listOf(createTab("test.com", private = false))))

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    browsingModeManager = browsingModeManager,
                )

            val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
        }

    @Test
    fun `GIVEN private browsing mode WHEN initializing the toolbar THEN show the number of private tabs in the tabs counter button`() =
        runTest {
            val browsingModeManager = SimpleBrowsingModeManager(Private)
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs =
                                listOf(
                                    createTab("test.com", private = true),
                                    createTab("firefox.com", private = true),
                                )
                        )
                )
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    browsingModeManager = browsingModeManager,
                )

            val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(2, true), tabCounterButton)
        }

    @Test
    fun `WHEN initializing the toolbar THEN setup showing the website origin`() {
        val expectedConfiguration =
            PageOrigin(
                hint = R.string.search_hint,
                title = null,
                url = null,
                contextualMenuOptions = listOf(PasteFromClipboard, LoadFromClipboard),
                onClick = OriginClicked,
            )
        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        val originConfiguration = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedConfiguration, originConfiguration)
    }

    @Test
    fun `WHEN clicking on the URL THEN record telemetry`() {
        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        assertEquals("HOME", Events.searchBarTapped.testGetValue()?.last()?.extra?.get("source"))
    }

    @Test
    fun `GIVEN voice search in display toolbar is disabled WHEN initializing the toolbar THEN no end page actions are shown and the long search hint is used`() {
        every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns false

        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
        assertEquals(R.string.search_hint, toolbarStore.state.displayState.pageOrigin.hint)
    }

    @Test
    fun `GIVEN voice search is enabled AND speech recognition is available WHEN initializing the toolbar THEN a voice search button is shown and the short search hint is used`() {
        every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns true
        every { testContext.components.settings.shouldShowVoiceSearch } returns true
        registerSpeechRecognizer()

        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        val pageActionsEnd = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageActionsEnd.size)
        val voiceSearchButton = pageActionsEnd[0] as ActionButtonRes
        assertEquals(iconsR.drawable.mozac_ic_microphone_24, voiceSearchButton.drawableResId)
        assertEquals(R.string.voice_search_content_description, voiceSearchButton.contentDescription)
        assertEquals(VoiceSearchClicked, voiceSearchButton.onClick)
        assertEquals(R.string.search_hint_short, toolbarStore.state.displayState.pageOrigin.hint)
    }

    @Test
    fun `GIVEN voice search is enabled AND speech recognition is unavailable WHEN initializing the toolbar THEN no voice search button is shown`() {
        every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns true
        every { testContext.components.settings.shouldShowVoiceSearch } returns true

        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
        assertEquals(R.string.search_hint_short, toolbarStore.state.displayState.pageOrigin.hint)
    }

    @Test
    fun `GIVEN the show-voice-search setting is off AND the display-toolbar experiment is enabled AND speech recognition is available WHEN initializing the toolbar THEN no voice search button is shown and the long search hint is used`() {
        every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns true
        every { testContext.components.settings.shouldShowVoiceSearch } returns false
        registerSpeechRecognizer()

        val (_, toolbarStore) = buildMiddlewareAndAddToStore()

        assertTrue(toolbarStore.state.displayState.pageActionsEnd.isEmpty())
        assertEquals(R.string.search_hint, toolbarStore.state.displayState.pageOrigin.hint)
    }

    @Test
    fun `GIVEN voice search button is shown WHEN it is clicked THEN VoiceInputRequested is dispatched and SearchStarted is dispatched after the toolbar edit delay`() =
        runTest {
            every { testContext.components.settings.showVoiceSearchInDisplayToolbar } returns true
            registerSpeechRecognizer()
            val (_, toolbarStore) = buildMiddlewareAndAddToStore()
            val voiceSearchButton = toolbarStore.state.displayState.pageActionsEnd[0] as ActionButtonRes

            toolbarStore.dispatch(voiceSearchButton.onClick as BrowserToolbarEvent)
            testDispatcher.scheduler.runCurrent()

            verify { appStore.dispatch(VoiceInputRequested) }
            verify(exactly = 0) { appStore.dispatch(SearchStarted()) }

            testDispatcher.scheduler.advanceUntilIdle()

            verify { appStore.dispatch(SearchStarted()) }
        }

    // Testing updated configuration

    @Test
    fun `GIVEN tall window WHEN changing to short window THEN show browser end actions`() = runTest {
        var isWideScreen = false
        var isTallScreen = true
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                appStore = appStore,
                isWideScreen = { isWideScreen },
                isTallScreen = { isTallScreen },
            )
        testDispatcher.scheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)

        isWideScreen = true
        isTallScreen = false
        appStore.dispatch(AppAction.OrientationChange(Landscape))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN short window WHEN changing to tall window THEN show all browser end actions`() = runTest {
        var isWideScreen = true
        var isTallScreen = false
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                appStore = appStore,
                isWideScreen = { isWideScreen },
                isTallScreen = { isTallScreen },
            )
        testDispatcher.scheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)

        isWideScreen = false
        isTallScreen = true
        appStore.dispatch(AppAction.OrientationChange(Portrait))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN expanded toolbar with tabstrip and tall window WHEN changing to short window THEN show tab counter and menu`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.isTabStripEnabled } returns true
            var isWideScreen = false
            var isTallScreen = true
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    appStore = appStore,
                    isWideScreen = { isWideScreen },
                    isTallScreen = { isTallScreen },
                )
            testDispatcher.scheduler.advanceUntilIdle()
            var navigationActions = toolbarStore.state.displayState.navigationActions
            assertEquals(5, navigationActions.size)
            var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(0, toolbarBrowserActions.size)

            isWideScreen = true
            isTallScreen = false
            appStore.dispatch(AppAction.OrientationChange(Portrait))
            testDispatcher.scheduler.advanceUntilIdle()

            navigationActions = toolbarStore.state.displayState.navigationActions
            assertEquals(0, navigationActions.size)
            toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
            val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            val menuButton = toolbarBrowserActions[1] as ActionButtonRes
            assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
            assertEquals(expectedMenuButton(), menuButton)
        }

    @Test
    fun `GIVEN in normal browsing WHEN the number of normal opened tabs is modified THEN update the tab counter`() =
        runTest {
            val browsingModeManager = SimpleBrowsingModeManager(Normal)
            val browserStore = BrowserStore()
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    browsingModeManager = browsingModeManager,
                )
            testDispatcher.scheduler.advanceUntilIdle()
            var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
            var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(0), tabCounterButton)

            val newNormalTab = createTab("test.com", private = false)
            val newPrivateTab = createTab("test.com", private = true)
            browserStore.dispatch(AddTabAction(newNormalTab))
            browserStore.dispatch(AddTabAction(newPrivateTab))
            testDispatcher.scheduler.advanceUntilIdle()

            toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
            tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
        }

    @Test
    fun `GIVEN in private browsing WHEN the number of private opened tabs is modified THEN update the tab counter`() =
        runTest {
            val browsingModeManager = SimpleBrowsingModeManager(Private)
            val initialNormalTab = createTab("test.com", private = false)
            val initialPrivateTab = createTab("test.com", private = true)
            val browserStore = BrowserStore(BrowserState(tabs = listOf(initialNormalTab, initialPrivateTab)))
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    browsingModeManager = browsingModeManager,
                )
            testDispatcher.scheduler.advanceUntilIdle()
            var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
            var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(1, true), tabCounterButton)

            browserStore.dispatch(RemoveTabAction(initialPrivateTab.id))
            testDispatcher.scheduler.advanceUntilIdle()

            toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(2, toolbarBrowserActions.size)
            tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
            assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
        }

    // Testing actions

    @Test
    fun `GIVEN menu customization is enabled WHEN clicking the menu button THEN open the customizable menu`() {
        every { testContext.components.settings.isMenuCustomizationEnabled } returns true
        val navController = navControllerShowingHome()
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(navController = navController)
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes

        toolbarStore.dispatch(menuButton.onClick as BrowserToolbarEvent)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionHomeFragmentToMenuFragment(accessPoint = MenuAccessPoint.Home),
                null,
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking the tab counter button THEN open the tabs tray in normal mode`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabManagementFragment(page = Page.NormalTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking the tab counter button THEN open the tabs tray in private mode`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabManagementFragment(page = Page.PrivateTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking on the long click menu option THEN open a new private tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        assertEquals("", toolbarStore.state.editState.query.current)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking on the long click menu option THEN open a new normal tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Normal, browsingModeManager.mode)
        assertEquals("", toolbarStore.state.editState.query.current)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled and expanded toolbar WHEN clicking the new tab button THEN start a new search`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.enableHomepageAsNewTab } returns false
            val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(fenixBrowserUseCases = fenixBrowserUseCases)
            val newTabButton = toolbarStore.state.displayState.navigationActions[2] as ActionButtonRes

            toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

            verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(any(), any(), any()) }
            verify { appStore.dispatch(SearchStarted()) }
        }

    @Test
    fun `GIVEN homepage as a new tab is enabled and expanded toolbar WHEN clicking the new tab button THEN open a new homepage tab`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val browsingModeManager = SimpleBrowsingModeManager(Normal)
            val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    fenixBrowserUseCases = fenixBrowserUseCases,
                    browsingModeManager = browsingModeManager,
                )
            val newTabButton = toolbarStore.state.displayState.navigationActions[2] as ActionButtonRes

            toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

            verify { fenixBrowserUseCases.addNewHomepageTab(private = false) }
            verify(exactly = 0) { appStore.dispatch(SearchStarted()) }
            assertEquals(Normal, browsingModeManager.mode)
        }

    @Test
    fun `GIVEN homepage as a new tab is enabled and expanded toolbar in private browsing mode WHEN clicking the new tab button THEN open a new private homepage tab`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val browsingModeManager = SimpleBrowsingModeManager(Private)
            val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    fenixBrowserUseCases = fenixBrowserUseCases,
                    browsingModeManager = browsingModeManager,
                )
            val newTabButton = toolbarStore.state.displayState.navigationActions[2] as ActionButtonRes

            toolbarStore.dispatch(newTabButton.onClick as BrowserToolbarEvent)

            verify { fenixBrowserUseCases.addNewHomepageTab(private = true) }
            verify(exactly = 0) { appStore.dispatch(SearchStarted()) }
            assertEquals(Private, browsingModeManager.mode)
        }

    @Test
    fun `GIVEN homepage as a new tab is enabled and private browsing mode WHEN clicking on the long click menu option THEN open a new normal homepage tab`() {
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        verify { fenixBrowserUseCases.addNewHomepageTab(private = false) }
        verify(exactly = 0) { appStore.dispatch(SearchStarted()) }
        assertEquals(Normal, browsingModeManager.mode)
    }

    @Test
    fun `GIVEN in normal browsing mode WHEN the page origin is clicked THEN start the search UX for normal browsing`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN in private browsing mode WHEN the page origin is clicked THEN start the search UX for private browsing`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                navController = navController,
                browsingModeManager = browsingModeManager,
            )

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `WHEN choosing to paste from clipboard THEN start a new search with the current clipboard text`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val clipboard =
            ClipboardHandler(testContext).also {
                it.text = "test"
            }
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                clipboard = clipboard,
                browsingModeManager = browsingModeManager,
            )

        toolbarStore.dispatch(PasteFromClipboardClicked)

        assertEquals(clipboard.text, toolbarStore.state.editState.query.current)
        assertTrue(toolbarStore.state.editState.isQueryPrefilled)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN homepage as new tab is disabled WHEN choosing to load URL from clipboard THEN start load the URL from clipboard in a new tab`() {
        every { testContext.components.settings.enableHomepageAsNewTab } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val clipboardUrl = "https://www.mozilla.com"
        val clipboard =
            ClipboardHandler(testContext).also {
                it.text = clipboardUrl
            }
        val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val selectedSearchEngine = appStore.state.searchState.selectedSearchEngine?.searchEngine
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                clipboard = clipboard,
                fenixBrowserUseCases = fenixBrowserUseCases,
                navController = navController,
                browsingModeManager = browsingModeManager,
            )

        toolbarStore.dispatch(LoadFromClipboardClicked)

        verify {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = clipboardUrl,
                newTab = true,
                private = false,
                searchEngine = selectedSearchEngine,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN homepage as new tab is enabled WHEN choosing to load URL from clipboard THEN start load the URL from clipboard in the current tab`() {
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val clipboardUrl = "https://www.mozilla.com"
        val clipboard =
            ClipboardHandler(testContext).also {
                it.text = clipboardUrl
            }
        val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val selectedSearchEngine = appStore.state.searchState.selectedSearchEngine?.searchEngine
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                clipboard = clipboard,
                fenixBrowserUseCases = fenixBrowserUseCases,
                navController = navController,
                browsingModeManager = browsingModeManager,
            )

        toolbarStore.dispatch(LoadFromClipboardClicked)

        verify {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = clipboardUrl,
                newTab = false,
                private = false,
                searchEngine = selectedSearchEngine,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `WHEN the selected search engine changes THEN update the search selector`() {
        val appStore = AppStore()

        val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)
        val newSearchEngine = SearchEngine("test", "Test", mockk(relaxed = true), type = APPLICATION)

        appStore.dispatch(SearchEngineSelected(newSearchEngine, true))
        testDispatcher.scheduler.advanceUntilIdle()

        assertSearchSelectorEquals(
            expectedSearchSelector(newSearchEngine),
            toolbarStore.state.displayState.pageActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `GIVEN a search engine is already selected WHEN the search engine configuration changes THEN don't change the selected search engine`() {
        val selectedSearchEngine = SearchEngine("test", "Test", mockk(relaxed = true), type = APPLICATION)
        val otherSearchEngine = SearchEngine("other", "Other", mockk(relaxed = true), type = APPLICATION)
        val appStore =
            AppStore(
                initialState =
                    AppState(
                        searchState =
                            SearchState.EMPTY.copy(
                                selectedSearchEngine = SelectedSearchEngine(selectedSearchEngine, true)
                            )
                    )
            )
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

        browserStore.dispatch(ApplicationSearchEnginesLoaded(listOf(otherSearchEngine)))
        testDispatcher.scheduler.advanceUntilIdle()

        assertNotEquals(
            appStore.state.searchState.selectedSearchEngine?.searchEngine,
            browserStore.state.search.selectedOrDefaultSearchEngine,
        )
        assertSearchSelectorEquals(
            expectedSearchSelector(selectedSearchEngine, listOf(otherSearchEngine)),
            toolbarStore.state.displayState.pageActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `WHEN building TabCounter action THEN returns TabCounterAction with correct count and menu`() {
        val browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(id = "a", url = "https://www.mozilla.org"),
                                createTab(id = "b", url = "https://www.firefox.com"),
                                createTab(id = "c", url = "https://getpocket.com"),
                            )
                    )
            )

        val (middleware, _) = buildMiddlewareAndAddToStore(browserStore = browserStore)

        val action = middleware.buildHomeAction(action = HomeToolbarAction.TabCounter) as TabCounterAction

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
    fun `WHEN building Menu action THEN returns Menu ActionButton`() {
        val (middleware, _) = buildMiddlewareAndAddToStore()

        val action = middleware.buildHomeAction(action = HomeToolbarAction.Menu) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_more_vertical_round_24, action.drawableResId)
        assertEquals(R.string.content_description_menu, action.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, action.state)
        assertEquals(MenuClicked(source = Source.Unknown), action.onClick)
        assertNull(action.onLongClick)
    }

    @Test
    fun `WHEN building Stories action THEN returns Stories ActionButton`() {
        val (middleware, _) = buildMiddlewareAndAddToStore()

        val action = middleware.buildHomeAction(action = HomeToolbarAction.Stories) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_newsfeed_24, action.drawableResId)
        assertEquals(R.string.homepage_all_stories, action.contentDescription)
        assertEquals(StoriesClicked(source = Source.Unknown), action.onClick)
        assertNull(action.onLongClick)
    }

    @Test
    fun `WHEN building PrivateMode action THEN returns PrivateMode ActionButton`() {
        val (middleware, _) = buildMiddlewareAndAddToStore()

        val action = middleware.buildHomeAction(action = HomeToolbarAction.PrivateMode) as ActionButtonRes

        assertEquals(iconsR.drawable.mozac_ic_private_mode_24, action.drawableResId)
        assertEquals(R.string.content_description_private_browsing, action.contentDescription)
        assertEquals(PrivateModeClicked(source = Source.Unknown), action.onClick)
        assertNull(action.onLongClick)
    }

    @Test
    fun `GIVEN homepage middle search is enabled WHEN initializing the toolbar THEN add the stories browser start action and private mode end action`() =
        runTest {
            every { testContext.components.settings.isHomepageMiddleSearchEnabled } returns true
            every { testContext.components.settings.showPocketRecommendationsFeature } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            val startActions = toolbarStore.state.displayState.browserActionsStart
            assertEquals(1, startActions.size)
            assertEquals(iconsR.drawable.mozac_ic_newsfeed_24, (startActions[0] as ActionButtonRes).drawableResId)

            val endActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(3, endActions.size)
            assertEquals(iconsR.drawable.mozac_ic_private_mode_24, (endActions[0] as ActionButtonRes).drawableResId)
            assertIs<TabCounterAction>(endActions[1])
        }

    @Test
    fun `GIVEN homepage middle search is enabled AND browsing in private mode WHEN initializing the toolbar THEN add only the private mode end action`() =
        runTest {
            every { testContext.components.settings.isHomepageMiddleSearchEnabled } returns true
            every { testContext.components.settings.showPocketRecommendationsFeature } returns true

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(browsingModeManager = SimpleBrowsingModeManager(Private))

            assertEquals(0, toolbarStore.state.displayState.browserActionsStart.size)

            val endActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(3, endActions.size)
            val privateModeButton = endActions[0] as ActionButtonRes
            assertEquals(iconsR.drawable.mozac_ic_private_mode_24, privateModeButton.drawableResId)
            assertEquals(ActionButton.State.ACTIVE, privateModeButton.state)
        }

    @Test
    fun `GIVEN homepage middle search is disabled WHEN initializing the toolbar THEN don't add the stories browser start action and private mode end action`() =
        runTest {
            every { testContext.components.settings.showPocketRecommendationsFeature } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            assertEquals(0, toolbarStore.state.displayState.browserActionsStart.size)
            assertEquals(2, toolbarStore.state.displayState.browserActionsEnd.size)
        }

    @Test
    fun `GIVEN homepage middle search is enabled WHEN clicking the stories button THEN open the stories screen`() {
        every { testContext.components.settings.isHomepageMiddleSearchEnabled } returns true
        every { testContext.components.settings.showPocketRecommendationsFeature } returns true
        val navController = navControllerShowingHome()
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(navController = navController)
        val storiesButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes

        toolbarStore.dispatch(storiesButton.onClick as BrowserToolbarEvent)

        verify { navController.navigate(HomeFragmentDirections.actionHomeFragmentToStoriesFragment(), null) }
    }

    @Test
    fun `GIVEN homepage middle search is enabled WHEN clicking the private mode button THEN switch to private browsing`() {
        every { testContext.components.settings.isHomepageMiddleSearchEnabled } returns true
        val privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true)
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(privateBrowsingController = privateBrowsingController)
        val privateModeButton = toolbarStore.state.displayState.browserActionsEnd[0] as ActionButtonRes

        toolbarStore.dispatch(privateModeButton.onClick as BrowserToolbarEvent)

        verify { privateBrowsingController.handlePrivateModeButtonClicked(Private) }
    }

    @Test
    fun `GIVEN the menu button is not highlighted WHEN a menu item is highlighted THEN highlight menu button`() =
        runTest {
            val appStore = AppStore()
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

            testDispatcher.scheduler.advanceUntilIdle()
            val initialMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(), initialMenuButton)

            appStore.dispatch(AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.Downloads))
            testDispatcher.scheduler.advanceUntilIdle()
            val updatedMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(true), updatedMenuButton)
        }

    @Test
    fun `GIVEN the menu button is highlighted WHEN no menu item is highlighted THEN remove highlight from menu button`() =
        runTest {
            val appStore =
                AppStore(
                    initialState = AppState(supportedMenuNotifications = setOf(SupportedMenuNotifications.Downloads))
                )
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

            testDispatcher.scheduler.advanceUntilIdle()
            val initialMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(true), initialMenuButton)

            appStore.dispatch(AppAction.MenuNotification.RemoveMenuNotification(SupportedMenuNotifications.Downloads))
            testDispatcher.scheduler.advanceUntilIdle()
            val updatedMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(), updatedMenuButton)
        }

    @Test
    fun `GIVEN the open in app is highlighted THEN menu button is not highlighted`() = runTest {
        val appStore =
            AppStore(initialState = AppState(supportedMenuNotifications = setOf(SupportedMenuNotifications.OpenInApp)))
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

        testDispatcher.scheduler.advanceUntilIdle()
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
        assertEquals(expectedMenuButton(false), menuButton)
    }

    @Test
    fun `GIVEN SupportedMenuNotifications contains NotDefaultBrowser THEN menu button is highlighted`() = runTest {
        val appStore =
            AppStore(
                initialState =
                    AppState(supportedMenuNotifications = setOf(SupportedMenuNotifications.NotDefaultBrowser))
            )
        val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

        testDispatcher.scheduler.advanceUntilIdle()
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
        assertEquals(expectedMenuButton(true), menuButton)
    }

    @Test
    fun `GIVEN SupportedMenuNotifications doesn't contains NotDefaultBrowser but other notification THEN menu button is not highlighted`() =
        runTest {
            val appStore = AppStore(initialState = AppState(supportedMenuNotifications = emptySet()))
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

            testDispatcher.scheduler.advanceUntilIdle()
            val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(false), menuButton)
        }

    @Test
    fun `GIVEN menu is highlighted from browser not being set as default during onboarding WHEN clicking the menu button THEN remove highlight`() =
        runTest {
            val appStore =
                AppStore(
                    initialState =
                        AppState(supportedMenuNotifications = setOf(SupportedMenuNotifications.NotDefaultBrowser))
                )
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)

            testDispatcher.scheduler.advanceUntilIdle()
            val initialMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(true), initialMenuButton)

            appStore.dispatch(
                AppAction.MenuNotification.RemoveMenuNotification(SupportedMenuNotifications.NotDefaultBrowser)
            )

            testDispatcher.scheduler.advanceUntilIdle()
            val updatedMenuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes
            assertEquals(expectedMenuButton(), updatedMenuButton)
        }

    @Test
    fun `GIVEN expanded toolbar use translate shortcut WHEN initializing toolbar THEN show DISABLED Translate in navigation actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.TRANSLATE.value

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            val translateButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(expectedTranslateButton, translateButton)
        }

    @Test
    fun `GIVEN expanded toolbar use homepage shortcut WHEN initializing toolbar THEN show DISABLED Homepage in navigation actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.HOMEPAGE.value

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            val homepageButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(expectedHomepageButton, homepageButton)
        }

    @Test
    fun `GIVEN expanded toolbar use back shortcut WHEN initializing toolbar THEN show DISABLED Back in navigation actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.BACK.value

            val (_, toolbarStore) = buildMiddlewareAndAddToStore()

            val backButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(expectedBackButton, backButton)
        }

    @Test
    fun `GIVEN homepage as a new tab and the back shortcut WHEN the selected tab can go back THEN show an enabled back navigation button`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.BACK.value
            every { testContext.components.settings.enableHomepageAsNewTab } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore(browserStore = createBrowserStoreWithAboutHome())

            val backButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(ActionButton.State.DEFAULT, backButton.state)
            assertEquals(NavigateBackClicked(Source.NavigationBar), backButton.onClick)
            assertEquals(NavigateBackLongClicked(Source.NavigationBar), backButton.onLongClick)
        }

    @Test
    fun `GIVEN homepage as a new tab and the back shortcut WHEN the selected tab cannot go back THEN show a disabled back navigation button`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.BACK.value
            every { testContext.components.settings.enableHomepageAsNewTab } returns true

            val (_, toolbarStore) = buildMiddlewareAndAddToStore(browserStore = createBrowserStoreWithAboutHome(false))

            val backButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(ActionButton.State.DISABLED, backButton.state)
            assertEquals(NavigateBackClicked(Source.NavigationBar), backButton.onClick)
            assertEquals(NavigateBackLongClicked(Source.NavigationBar), backButton.onLongClick)
        }

    @Test
    fun `GIVEN homepage as a new tab WHEN the selected tab starts being able to go back THEN update the Back button`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.BACK.value
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val browserStore = createBrowserStoreWithAboutHome(canGoBack = false)

            val (_, toolbarStore) = buildMiddlewareAndAddToStore(browserStore = browserStore)
            assertEquals(
                ActionButton.State.DISABLED,
                (toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes).state,
            )

            browserStore.dispatch(ContentAction.UpdateBackNavigationStateAction(aboutHomeTabId, true))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                ActionButton.State.DEFAULT,
                (toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes).state,
            )
        }

    @Test
    fun `GIVEN homepage as a new tab WHEN initializing a wide toolbar THEN show the Back and Forward buttons in the start browser actions`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = createBrowserStoreWithAboutHome(),
                    isWideScreen = { true },
                )

            assertEquals(2, toolbarStore.state.displayState.browserActionsStart.size)

            val backButton = toolbarStore.state.displayState.browserActionsStart[0] as ActionButtonRes
            assertEquals(ActionButton.State.DEFAULT, backButton.state)
            assertEquals(NavigateBackClicked(Source.AddressBar.BrowserStart), backButton.onClick)
            assertEquals(NavigateBackLongClicked(Source.AddressBar.BrowserStart), backButton.onLongClick)

            val forwardButton = toolbarStore.state.displayState.browserActionsStart[1] as ActionButtonRes
            assertEquals(ActionButton.State.DEFAULT, forwardButton.state)
            assertEquals(NavigateForwardClicked(Source.AddressBar.BrowserStart), forwardButton.onClick)
            assertEquals(NavigateForwardLongClicked(Source.AddressBar.BrowserStart), forwardButton.onLongClick)
        }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN initializing a wide toolbar THEN show no start browser actions`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns false

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = createBrowserStoreWithAboutHome(),
                    isWideScreen = { true },
                )

            assertTrue(toolbarStore.state.displayState.browserActionsStart.isEmpty())
        }

    @Test
    fun `GIVEN homepage as new tab and the selected home tab can go back WHEN the Back button is clicked THEN go back in the tab history`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val navController: NavController = mockk(relaxed = true)
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs =
                                listOf(
                                    createTab(
                                        id = "tab",
                                        canGoBack = true,
                                        history =
                                            HistoryState(
                                                items =
                                                    listOf(
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                        HistoryItem(title = "Mozilla", uri = "mozilla.org"),
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                    ),
                                                currentIndex = 2,
                                            ),
                                    )
                                ),
                            selectedTabId = "tab",
                        ),
                    middleware = listOf(captureMiddleware) + EngineMiddleware.create(mockk(relaxed = true)),
                )

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateBackClicked(Source.AddressBar.BrowserStart))

            captureMiddleware.assertLastAction(EngineAction.GoBackAction::class) {
                assertEquals("tab", it.tabId)
            }
            verify(exactly = 0) {
                navController.navigate(
                    NavGraphDirections.actionGlobalTabHistoryDialogFragment(null),
                    null,
                )
            }
        }

    @Test
    fun `GIVEN the homepage is shown with homepage as new tab enabled and there is a page to go back to WHEN the Back button is clicked THEN leave the homepage`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val navController: NavController =
                mockk(relaxed = true) {
                    every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
                }
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs =
                                listOf(
                                    createTab(
                                        id = "tab",
                                        canGoBack = true,
                                        history =
                                            HistoryState(
                                                items =
                                                    listOf(
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                        HistoryItem(title = "Mozilla", uri = "mozilla.org"),
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                    ),
                                                currentIndex = 2,
                                            ),
                                    )
                                ),
                            selectedTabId = "tab",
                        ),
                    middleware = EngineMiddleware.create(mockk(relaxed = true)),
                )

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateBackClicked(Source.AddressBar.BrowserStart))

            verify { navController.navigate(NavGraphDirections.actionGlobalBrowser()) }
        }

    @Test
    fun `GIVEN homepage as new tab and the selected home tab cannot go back WHEN the Back button is clicked THEN do nothing`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val navController: NavController =
                mockk(relaxed = true) {
                    every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
                }
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore =
                        createBrowserStoreWithAboutHome(canGoBack = false, captureMiddleware = captureMiddleware),
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateBackClicked(Source.AddressBar.BrowserStart))

            captureMiddleware.assertNotDispatched(EngineAction.GoBackAction::class)
            verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        }

    @Test
    fun `GIVEN homepage as new tab WHEN the Back button is long clicked THEN show the tab history`() = runTest {
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val navController: NavController =
            mockk(relaxed = true) {
                every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
            }
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                browserStore =
                    createBrowserStoreWithAboutHome(canGoBack = false, captureMiddleware = captureMiddleware),
                navController = navController,
            )

        toolbarStore.dispatch(NavigateBackLongClicked(Source.AddressBar.BrowserStart))

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalTabHistoryDialogFragment(null),
                null,
            )
        }
        captureMiddleware.assertNotDispatched(EngineAction.GoBackAction::class)
    }

    @Test
    fun `GIVEN homepage as a new tab WHEN mapping the back shortcut THEN it maps to the real back action`() = runTest {
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        val (middleware, _) = buildMiddlewareAndAddToStore()

        val back = with(middleware) { ShortcutType.BACK.toHomeToolbarAction() }

        assertEquals(HomeToolbarAction.Back, back)
    }

    @Test
    fun `GIVEN homepage as new tab and the selected home tab can go forward WHEN the Forward button is clicked THEN go forward in the tab history`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val navController: NavController = mockk(relaxed = true)
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs =
                                listOf(
                                    createTab(
                                        id = "tab",
                                        canGoForward = true,
                                        history =
                                            HistoryState(
                                                items =
                                                    listOf(
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                        HistoryItem(title = "Mozilla", uri = "mozilla.org"),
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                    ),
                                                currentIndex = 2,
                                            ),
                                    )
                                ),
                            selectedTabId = "tab",
                        ),
                    middleware = listOf(captureMiddleware) + EngineMiddleware.create(mockk(relaxed = true)),
                )

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateForwardClicked(Source.AddressBar.BrowserStart))

            captureMiddleware.assertLastAction(EngineAction.GoForwardAction::class) {
                assertEquals("tab", it.tabId)
            }
            verify(exactly = 0) {
                navController.navigate(
                    NavGraphDirections.actionGlobalTabHistoryDialogFragment(null),
                    null,
                )
            }
        }

    @Test
    fun `GIVEN the homepage is shown with homepage as new tab enabled and there is a page to go forward to WHEN the Forward button is clicked THEN leave the homepage`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val navController: NavController =
                mockk(relaxed = true) {
                    every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
                }
            val browserStore =
                BrowserStore(
                    initialState =
                        BrowserState(
                            tabs =
                                listOf(
                                    createTab(
                                        id = "tab",
                                        canGoForward = true,
                                        history =
                                            HistoryState(
                                                items =
                                                    listOf(
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                        HistoryItem(title = "Homepage", uri = ABOUT_HOME_URL),
                                                        HistoryItem(title = "Mozilla", uri = "mozilla.org"),
                                                    ),
                                                currentIndex = 1,
                                            ),
                                    )
                                ),
                            selectedTabId = "tab",
                        ),
                    middleware = EngineMiddleware.create(mockk(relaxed = true)),
                )

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore = browserStore,
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateForwardClicked(Source.AddressBar.BrowserStart))

            verify { navController.navigate(NavGraphDirections.actionGlobalBrowser()) }
        }

    @Test
    fun `GIVEN homepage as new tab and the selected home tab cannot go forward WHEN the Forward button is clicked THEN do nothing`() =
        runTest {
            every { testContext.components.settings.enableHomepageAsNewTab } returns true
            val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val navController: NavController =
                mockk(relaxed = true) {
                    every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
                }
            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    browserStore =
                        createBrowserStoreWithAboutHome(canGoForward = false, captureMiddleware = captureMiddleware),
                    navController = navController,
                )

            toolbarStore.dispatch(NavigateForwardClicked(Source.AddressBar.BrowserStart))

            captureMiddleware.assertNotDispatched(EngineAction.GoForwardAction::class)
            verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        }

    @Test
    fun `GIVEN homepage as new tab WHEN the Forward button is long clicked THEN show the tab history`() = runTest {
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        val captureMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val navController: NavController =
            mockk(relaxed = true) {
                every { currentDestination } returns mockk { every { id } returns R.id.homeFragment }
            }
        val (_, toolbarStore) =
            buildMiddlewareAndAddToStore(
                browserStore =
                    createBrowserStoreWithAboutHome(canGoForward = false, captureMiddleware = captureMiddleware),
                navController = navController,
            )

        toolbarStore.dispatch(NavigateForwardLongClicked(Source.AddressBar.BrowserStart))

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalTabHistoryDialogFragment(null),
                null,
            )
        }
        captureMiddleware.assertNotDispatched(EngineAction.GoForwardAction::class)
    }

    @Test
    fun `toHomeToolbarAction maps ShortcutType to HomeToolbarAction`() = runTest {
        val (middleware, _) = buildMiddlewareAndAddToStore()

        with(middleware) {
            assertEquals(
                HomeToolbarAction.NewTab,
                ShortcutType.NEW_TAB.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeShare,
                ShortcutType.SHARE.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeBookmark,
                ShortcutType.BOOKMARK.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeTranslate,
                ShortcutType.TRANSLATE.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeHomepage,
                ShortcutType.HOMEPAGE.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeBack,
                ShortcutType.BACK.toHomeToolbarAction(),
            )
            assertEquals(
                HomeToolbarAction.FakeSummarize,
                ShortcutType.SUMMARIZE.toHomeToolbarAction(),
            )
        }
    }

    @Test
    fun `GIVEN translations is unavailable WHEN mapping the translate shortcut THEN it falls back to the bookmark action`() =
        runTest {
            val (middleware, _) =
                buildMiddlewareAndAddToStore(
                    translationsFeatureSettings = TranslationsEnabledSettings.inMemory(isEnabledInitial = false)
                )

            val translate = with(middleware) { ShortcutType.TRANSLATE.toHomeToolbarAction() }

            assertEquals(HomeToolbarAction.FakeBookmark, translate)
        }

    @Test
    fun `GIVEN expanded toolbar uses the translate shortcut but translations is disabled WHEN initializing toolbar THEN show the bookmark button in navigation actions`() =
        runTest {
            every { testContext.components.settings.shouldUseExpandedToolbar } returns true
            every { testContext.components.settings.toolbarExpandedShortcutKey } returns ShortcutType.TRANSLATE.value

            val (_, toolbarStore) =
                buildMiddlewareAndAddToStore(
                    translationsFeatureSettings = TranslationsEnabledSettings.inMemory(isEnabledInitial = false)
                )

            val primaryButton = toolbarStore.state.displayState.navigationActions.first() as ActionButtonRes
            assertEquals(expectedBookmarkButton, primaryButton)
        }

    @Test
    fun `GIVEN a tab groups notification WHEN a downloads notification is added THEN the menu button is highlighted`() =
        runTest {
            val appStore =
                AppStore(
                    initialState = AppState(supportedMenuNotifications = setOf(SupportedMenuNotifications.TabGroups))
                )
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(
                expected = expectedMenuButton(false),
                actual = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes,
            )

            appStore.dispatch(AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.Downloads))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                expected = expectedMenuButton(true),
                actual = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes,
            )
        }

    @Test
    fun `GIVEN tab groups and downloads notifications WHEN the downloads notification is removed THEN the menu button is not highlighted`() =
        runTest {
            val appStore =
                AppStore(
                    initialState =
                        AppState(
                            supportedMenuNotifications =
                                setOf(SupportedMenuNotifications.TabGroups, SupportedMenuNotifications.Downloads)
                        )
                )
            val (_, toolbarStore) = buildMiddlewareAndAddToStore(appStore = appStore)
            testDispatcher.scheduler.advanceUntilIdle()
            assertEquals(
                expected = expectedMenuButton(true),
                actual = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes,
            )

            appStore.dispatch(AppAction.MenuNotification.RemoveMenuNotification(SupportedMenuNotifications.Downloads))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                expected = expectedMenuButton(false),
                actual = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes,
            )
        }

    private fun navControllerShowingHome(): NavController =
        mockk(relaxed = true) { every { currentDestination } returns mockk { every { id } returns R.id.homeFragment } }

    private fun buildMiddlewareAndAddToStore(
        uiContext: Context = testContext,
        appStore: AppStore = this.appStore,
        browserStore: BrowserStore = this.browserStore,
        clipboard: ClipboardHandler = mockk(),
        fenixBrowserUseCases: FenixBrowserUseCases = mockk(),
        navController: NavController = mockk(),
        browsingModeManager: BrowsingModeManager = this.browsingModeManager,
        privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true),
        settings: Settings = testContext.components.settings,
        translationsFeatureSettings: TranslationsEnabledSettings = this.translationsFeatureSettings,
        isWideScreen: () -> Boolean = { false },
        isTallScreen: () -> Boolean = { true },
    ): Pair<BrowserToolbarMiddleware, BrowserToolbarStore> {
        val middleware =
            buildMiddleware(
                uiContext = uiContext,
                appStore = appStore,
                browserStore = browserStore,
                clipboard = clipboard,
                fenixBrowserUseCases = fenixBrowserUseCases,
                navController = navController,
                browsingModeManager = browsingModeManager,
                privateBrowsingController = privateBrowsingController,
                settings = settings,
                translationsFeatureSettings = translationsFeatureSettings,
                isWideScreen = isWideScreen,
                isTallScreen = isTallScreen,
            )
        val store = buildStore(middleware = middleware)

        return middleware to store
    }

    private fun buildMiddleware(
        uiContext: Context = testContext,
        appStore: AppStore = this.appStore,
        browserStore: BrowserStore = this.browserStore,
        clipboard: ClipboardHandler = mockk(),
        fenixBrowserUseCases: FenixBrowserUseCases = mockk(),
        navController: NavController = mockk(),
        browsingModeManager: BrowsingModeManager = this.browsingModeManager,
        privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true),
        settings: Settings = testContext.components.settings,
        translationsFeatureSettings: TranslationsEnabledSettings = this.translationsFeatureSettings,
        isWideScreen: () -> Boolean = { false },
        isTallScreen: () -> Boolean = { true },
    ) =
        BrowserToolbarMiddleware(
            uiContext = uiContext,
            appStore = appStore,
            browserStore = browserStore,
            clipboard = clipboard,
            fenixBrowserUseCases = fenixBrowserUseCases,
            navController = navController,
            browsingModeManager = browsingModeManager,
            privateBrowsingController = privateBrowsingController,
            settings = settings,
            translationsFeatureSettings = translationsFeatureSettings,
            isWideScreen = isWideScreen,
            isTallScreen = isTallScreen,
            scope = testScope,
        )

    private fun buildStore(middleware: BrowserToolbarMiddleware) =
        BrowserToolbarStore(middleware = listOf(middleware)).also {
            testDispatcher.scheduler.advanceUntilIdle() // to complete the initial setup happening in coroutines
        }

    private fun registerSpeechRecognizer() {
        val component = ComponentName("com.test", "VoiceTest")
        shadowOf(testContext.packageManager).apply {
            addActivityIfNotPresent(component)
            addIntentFilterForActivity(
                component,
                IntentFilter(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
                    addCategory(Intent.CATEGORY_DEFAULT)
                },
            )
        }
    }

    private fun expectedSearchSelector(
        defaultOrSelectedSearchEngine: SearchEngine,
        searchEngineShortcuts: List<SearchEngine> = emptyList(),
    ) =
        buildExpectedSearchSelector(
            defaultOrSelectedSearchEngine,
            searchEngineShortcuts,
            testContext.resources,
        )

    private fun assertEqualsToolbarButton(expected: TabCounterAction, actual: TabCounterAction) {
        assertEquals(expected.count, actual.count)
        assertEquals(expected.contentDescription, actual.contentDescription)
        assertEquals(expected.showPrivacyMask, actual.showPrivacyMask)
        assertEquals(expected.onClick, actual.onClick)
        when (expected.onLongClick) {
            null -> assertNull(actual.onLongClick)
            is BrowserToolbarEvent -> assertEquals(expected.onLongClick, actual.onLongClick)
            is BrowserToolbarMenu ->
                assertEquals(
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

    private fun expectedToolbarButton(
        tabCount: Int = 0,
        isPrivate: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) =
        TabCounterAction(
            count = tabCount,
            contentDescription =
                if (isPrivate) {
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
            onLongClick =
                CombinedEventAndMenu(TabCounterLongClicked(source)) {
                    when (isPrivate) {
                        true ->
                            listOf(
                                BrowserToolbarMenuButton(
                                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_tab),
                                    contentDescription =
                                        StringResContentDescription(tabcounterR.string.mozac_browser_menu_new_tab),
                                    onClick = AddNewTab(source),
                                )
                            )

                        false ->
                            listOf(
                                BrowserToolbarMenuButton(
                                    icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_fill_24),
                                    text = StringResText(tabcounterR.string.mozac_browser_menu_new_private_tab),
                                    contentDescription =
                                        StringResContentDescription(
                                            tabcounterR.string.mozac_browser_menu_new_private_tab
                                        ),
                                    onClick = AddNewPrivateTab(source),
                                )
                            )
                    }
                },
        )

    private fun expectedMenuButton(
        highlighted: Boolean = false,
        source: Source = Source.AddressBar.BrowserEnd,
    ) =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_more_vertical_round_24,
            contentDescription = R.string.content_description_menu,
            highlighted = highlighted,
            onClick = MenuClicked(source),
        )

    private val expectedBookmarkButton =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_bookmark_24,
            contentDescription = R.string.browser_menu_bookmark_this_page_2,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private val expectedShareButton =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_share_android_24,
            contentDescription = R.string.browser_menu_share,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private fun expectedNewTabButton(source: Source = Source.AddressBar.BrowserEnd) =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_plus_24,
            contentDescription = R.string.home_screen_shortcut_open_new_tab_2,
            onClick = AddNewTab(source),
        )

    private val expectedTranslateButton =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_translate_24,
            contentDescription = R.string.browser_toolbar_translate,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private val expectedHomepageButton =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_home_24,
            contentDescription = R.string.browser_menu_homepage,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private val expectedBackButton =
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_back_24,
            contentDescription = R.string.browser_menu_back,
            state = ActionButton.State.DISABLED,
            onClick = FakeClicked,
        )

    private fun createTab(
        id: String,
        url: String = ABOUT_HOME_URL,
        canGoBack: Boolean = false,
        canGoForward: Boolean = false,
        history: HistoryState = HistoryState(),
    ) =
        mozilla.components.browser.state.state.createTab(url = url, id = id).let {
            it.copy(content = it.content.copy(canGoBack = canGoBack, canGoForward = canGoForward, history = history))
        }

    private fun createBrowserStoreWithAboutHome(
        canGoBack: Boolean = true,
        canGoForward: Boolean = true,
        captureMiddleware: CaptureActionsMiddleware<BrowserState, BrowserAction>? = null,
    ) =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                url = ABOUT_HOME_URL,
                                id = aboutHomeTabId,
                                canGoBack = canGoBack,
                                canGoForward = canGoForward,
                            )
                        ),
                    selectedTabId = aboutHomeTabId,
                ),
            middleware = listOfNotNull(captureMiddleware) + EngineMiddleware.create(mockk(relaxed = true)),
        )
}
