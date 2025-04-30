/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.navigation.NavController
import androidx.navigation.NavOptions
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkStatic
import io.mockk.unmockkStatic
import io.mockk.verify
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.setMain
import mozilla.components.browser.state.action.TabListAction.AddTabAction
import mozilla.components.browser.state.action.TabListAction.RemoveTabAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.SimpleBrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.OrientationMode.Landscape
import org.mozilla.fenix.components.appstate.OrientationMode.Portrait
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.components.toolbar.navbar.shouldAddNavigationBar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.toolbar.BrowserToolbarMiddleware.LifecycleDependencies
import org.mozilla.fenix.home.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.tabstray.Page
import mozilla.components.ui.icons.R as iconsR

@RunWith(AndroidJUnit4::class)
class BrowserToolbarMiddlewareTest {
    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    private val appStore = AppStore()
    private val browserStore = BrowserStore()
    private val lifecycleOwner = FakeLifecycleOwner(Lifecycle.State.RESUMED)
    private val browsingModeManager = SimpleBrowsingModeManager(Normal)

    @Before
    fun setup() {
        mockkStatic(Context::shouldAddNavigationBar)
    }

    @After
    fun teardown() {
        unmockkStatic(Context::shouldAddNavigationBar)
    }

    // Testing initial configuration

    @Test
    fun `GIVEN navbar should not be shown WHEN initializing the toolbar THEN add browser end actions`() = runTestOnMain {
        every { testContext.shouldAddNavigationBar() } returns false
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }

        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButton
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN navbar should be shown WHEN initializing the toolbar THEN don't add browser end actions`() = runTestOnMain {
        every { testContext.shouldAddNavigationBar() } returns true
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }

        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)
    }

    @Test
    fun `GIVEN normal browsing mode WHEN initializing the toolbar THEN show the number of normal tabs in the tabs counter button`() = runTestOnMain {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(createTab("test.com", private = false)),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }

        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN private browsing mode WHEN initializing the toolbar THEN show the number of private tabs in the tabs counter button`() = runTestOnMain {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    createTab("test.com", private = true),
                    createTab("firefox.com", private = true),
                ),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }

        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(2, true), tabCounterButton)
    }

    // Testing updated configuration

    @Test
    fun `GIVEN in portrait WHEN changing to landscape THEN show browser end actions`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        // In portrait the navigation bar is displayed
        every { testContext.shouldAddNavigationBar() } returns true
        val appStore = AppStore(
            initialState = AppState(
                orientation = Portrait,
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)

        // In landscape the navigation bar is not displayed
        every { testContext.shouldAddNavigationBar() } returns false
        appStore.dispatch(AppAction.OrientationChange(Landscape)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButton
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN in landscape WHEN changing to portrait THEN remove all browser end actions`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        // In landscape the navigation bar is not displayed
        every { testContext.shouldAddNavigationBar() } returns false
        val appStore = AppStore(
            initialState = AppState(
                orientation = Landscape,
            ),
        )

        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButton
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton, menuButton)

        // In portrait the navigation bar is displayed
        every { testContext.shouldAddNavigationBar() } returns true
        appStore.dispatch(AppAction.OrientationChange(Portrait)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(0, toolbarBrowserActions.size)
    }

    @Test
    fun `GIVEN in normal browsing WHEN the number of normal opened tabs is modified THEN update the tab counter`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore()
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0), tabCounterButton)

        val newNormalTab = createTab("test.com", private = false)
        val newPrivateTab = createTab("test.com", private = true)
        browserStore.dispatch(AddTabAction(newNormalTab)).joinBlocking()
        browserStore.dispatch(AddTabAction(newPrivateTab)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN in private browsing WHEN the number of private opened tabs is modified THEN update the tab counter`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val initialNormalTab = createTab("test.com", private = false)
        val initialPrivateTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(initialNormalTab, initialPrivateTab),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, lifecycleOwner, mockk(), browsingModeManager))
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1, true), tabCounterButton)

        browserStore.dispatch(RemoveTabAction(initialPrivateTab.id)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
    }

    // Testing actions

    @Test
    fun `WHEN clicking the menu button THEN open the menu`() {
        every { testContext.shouldAddNavigationBar() } returns false
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(
                LifecycleDependencies(testContext, lifecycleOwner, navController, browsingModeManager),
            )
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButton

        toolbarStore.dispatch(menuButton.onClick as BrowserToolbarEvent)

        verify {
            navController.nav(
                R.id.homeFragment,
                BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                    accesspoint = MenuAccessPoint.Browser,
                ),
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking the tab counter button THEN open the tabs tray in normal mode`() {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(
                LifecycleDependencies(testContext, lifecycleOwner, navController, browsingModeManager),
            )
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabsTrayFragment(page = Page.NormalTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking the tab counter button THEN open the tabs tray in private mode`() {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(
                LifecycleDependencies(testContext, lifecycleOwner, navController, browsingModeManager),
            )
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabsTrayFragment(page = Page.PrivateTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking on the long click menu option THEN open a new private tab`() {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(
                LifecycleDependencies(testContext, lifecycleOwner, navController, browsingModeManager),
            )
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as BrowserToolbarMenu).items()

        mockkStatic(Context::settings) {
            mockkStatic(NavController::nav) {
                every { testContext.settings().toolbarPosition } returns ToolbarPosition.TOP

                toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

                assertEquals(Private, browsingModeManager.mode)
                verify {
                    navController.nav(
                        R.id.homeFragment,
                        NavGraphDirections.actionGlobalSearchDialog(sessionId = null),
                        NavOptions.Builder()
                            .setEnterAnim(R.anim.fade_in)
                            .setExitAnim(R.anim.fade_out)
                            .build(),
                    )
                }
            }
        }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking on the long click menu option THEN open a new normal tab`() {
        every { testContext.shouldAddNavigationBar() } returns false
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore).apply {
            updateLifecycleDependencies(
                LifecycleDependencies(testContext, lifecycleOwner, navController, browsingModeManager),
            )
        }
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as BrowserToolbarMenu).items()

        mockkStatic(Context::settings) {
            mockkStatic(NavController::nav) {
                every { testContext.settings().toolbarPosition } returns ToolbarPosition.TOP

                toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

                assertEquals(Normal, browsingModeManager.mode)
                verify {
                    navController.nav(
                        R.id.homeFragment,
                        NavGraphDirections.actionGlobalSearchDialog(sessionId = null),
                        NavOptions.Builder()
                            .setEnterAnim(R.anim.fade_in)
                            .setExitAnim(R.anim.fade_out)
                            .build(),
                    )
                }
            }
        }
    }

    private fun assertEqualsToolbarButton(expected: TabCounterAction, actual: TabCounterAction) {
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

    private fun expectedToolbarButton(
        tabCount: Int = 0,
        isPrivate: Boolean = false,
    ) = TabCounterAction(
        count = tabCount,
        contentDescription = testContext.getString(R.string.mozac_tab_counter_open_tab_tray, tabCount),
        showPrivacyMask = isPrivate,
        onClick = TabCounterClicked,
        onLongClick = BrowserToolbarMenu {
            when (isPrivate) {
                true -> listOf(
                    BrowserToolbarMenuButton(
                        iconResource = iconsR.drawable.mozac_ic_plus_24,
                        text = R.string.mozac_browser_menu_new_tab,
                        contentDescription = R.string.mozac_browser_menu_new_tab,
                        onClick = AddNewTab,
                    ),
                )

                false -> listOf(
                    BrowserToolbarMenuButton(
                        iconResource = iconsR.drawable.mozac_ic_private_mode_24,
                        text = R.string.mozac_browser_menu_new_private_tab,
                        contentDescription = R.string.mozac_browser_menu_new_private_tab,
                        onClick = AddNewPrivateTab,
                    ),
                )
            }
        },
    )
    private val expectedMenuButton = ActionButton(
        icon = R.drawable.mozac_ic_ellipsis_vertical_24,
        contentDescription = R.string.content_description_menu,
        tint = R.attr.actionPrimary,
        onClick = MenuClicked,
    )

    private class FakeLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner {
        override val lifecycle: Lifecycle = LifecycleRegistry(this).apply {
            currentState = initialState
        }
    }
}
