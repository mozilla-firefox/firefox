/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.tabs

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import mozilla.components.browser.state.action.CustomTabListAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabConfig
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean

@RunWith(AndroidJUnit4::class)
class CustomTabsUseCasesTest {

    private lateinit var store: BrowserStore
    private lateinit var tabsUseCases: CustomTabsUseCases
    private lateinit var engine: Engine
    private lateinit var engineSession: EngineSession

    @Before
    fun setup() {
        engineSession = mock()
        engine = mock()

        whenever(engine.createSession(anyBoolean(), any())).thenReturn(engineSession)
        store = BrowserStore(middleware = EngineMiddleware.create(engine = engine))
        tabsUseCases = CustomTabsUseCases(store, mock())
    }

    @Test
    fun `MigrateCustomTabUseCase - turns custom tab into regular tab and selects it`() {
        val customTab = createCustomTab("https://mozilla.org")
        store.dispatch(CustomTabListAction.AddCustomTabAction(customTab))
        assertEquals(0, store.state.tabs.size)
        assertEquals(1, store.state.customTabs.size)

        tabsUseCases.migrate(customTab.id, select = false)

        assertEquals(1, store.state.tabs.size)
        assertEquals(0, store.state.customTabs.size)
        assertNull(store.state.selectedTabId)

        val otherCustomTab = createCustomTab("https://firefox.com")
        store.dispatch(CustomTabListAction.AddCustomTabAction(otherCustomTab))
        assertEquals(1, store.state.tabs.size)
        assertEquals(1, store.state.customTabs.size)

        tabsUseCases.migrate(otherCustomTab.id, select = true)

        assertEquals(2, store.state.tabs.size)
        assertEquals(0, store.state.customTabs.size)
        assertEquals(otherCustomTab.id, store.state.selectedTabId)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN adding a custom tab without desktopMode THEN the tab is in desktop mode`() {
        store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = engine),
            )
        tabsUseCases = CustomTabsUseCases(store, mock())

        val tabId =
            tabsUseCases.add(
                url = "https://mozilla.org",
                customTabConfig = CustomTabConfig(),
                source = SessionState.Source.Internal.None,
            )
        val tab = store.state.findCustomTab(tabId)
        assertNotNull(tab)
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN adding a custom tab with desktopMode true THEN the tab is in desktop mode`() {
        store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = engine),
            )
        tabsUseCases = CustomTabsUseCases(store, mock())

        val tabId =
            tabsUseCases.add(
                url = "https://mozilla.org",
                customTabConfig = CustomTabConfig(),
                source = SessionState.Source.Internal.None,
                desktopMode = true,
            )
        val tab = store.state.findCustomTab(tabId)
        assertNotNull(tab)
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is enabled WHEN adding a custom tab with desktopMode false THEN the tab is not in desktop mode`() {
        store =
            BrowserStore(
                initialState = BrowserState(desktopMode = true),
                middleware = EngineMiddleware.create(engine = engine),
            )
        tabsUseCases = CustomTabsUseCases(store, mock())

        val tabId =
            tabsUseCases.add(
                url = "https://mozilla.org",
                customTabConfig = CustomTabConfig(),
                source = SessionState.Source.Internal.None,
                desktopMode = false,
            )
        val tab = store.state.findCustomTab(tabId)
        assertNotNull(tab)
        assertFalse(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is disabled WHEN adding a custom tab with desktopMode true THEN the tab is in desktop mode`() {
        store =
            BrowserStore(
                initialState = BrowserState(desktopMode = false),
                middleware = EngineMiddleware.create(engine = engine),
            )
        tabsUseCases = CustomTabsUseCases(store, mock())

        val tabId =
            tabsUseCases.add(
                url = "https://mozilla.org",
                customTabConfig = CustomTabConfig(),
                source = SessionState.Source.Internal.None,
                desktopMode = true,
            )
        val tab = store.state.findCustomTab(tabId)
        assertNotNull(tab)
        assertTrue(tab.content.desktopMode)
    }

    @Test
    fun `GIVEN desktop mode is disabled WHEN adding a custom tab with desktopMode false THEN the tab is not in desktop mode`() {
        store =
            BrowserStore(
                initialState = BrowserState(desktopMode = false),
                middleware = EngineMiddleware.create(engine = engine),
            )
        tabsUseCases = CustomTabsUseCases(store, mock())

        val tabId =
            tabsUseCases.add(
                url = "https://mozilla.org",
                customTabConfig = CustomTabConfig(),
                source = SessionState.Source.Internal.None,
                desktopMode = false,
            )
        val tab = store.state.findCustomTab(tabId)
        assertNotNull(tab)
        assertFalse(tab.content.desktopMode)
    }
}
