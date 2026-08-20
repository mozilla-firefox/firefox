/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine.middleware

import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.Mockito.anyString
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

@OptIn(ExperimentalCoroutinesApi::class)
class LinkingMiddlewareTest {
    private val testDispatcher = StandardTestDispatcher()
    private val scope = CoroutineScope(testDispatcher)

    @Test
    fun `loads URL after linking`() {
        val middleware = LinkingMiddleware(scope)

        val tab = createTab("https://www.mozilla.org", id = "1")
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession).loadUrl(tab.content.url)
    }

    @Test
    fun `loads URL with load URL flags and additional headers after linking`() {
        val middleware = LinkingMiddleware(scope)

        val loadFlags = EngineSession.LoadUrlFlags.external()
        val additionalHeaders = mapOf("X-Extra-Header" to "true")
        val tab =
            createTab(
                url = "https://www.mozilla.org",
                id = "1",
                initialLoadFlags = loadFlags,
                initialAdditionalHeaders = additionalHeaders,
                initialTextDirectiveUserActivation = true,
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession)
            .loadUrl(
                url = tab.content.url,
                flags = loadFlags,
                additionalHeaders = additionalHeaders,
                textDirectiveUserActivation = true,
            )
    }

    @Test
    fun `GIVEN ABOUT_HOME_URL tab WHEN linking the tab THEN load the URL bypassing the load URI delegate`() {
        val middleware = LinkingMiddleware(scope)

        val tab = createTab(ABOUT_HOME_URL, id = "1")
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession)
            .loadUrl(
                url = ABOUT_HOME_URL,
                flags =
                    EngineSession.LoadUrlFlags.select(EngineSession.LoadUrlFlags.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE),
            )
    }

    @Test
    fun `GIVEN tab is already bypassing the load URI delegate WHEN linking the tab THEN the load flags are unchanged`() {
        val middleware = LinkingMiddleware(scope)

        val loadFlags =
            EngineSession.LoadUrlFlags.select(EngineSession.LoadUrlFlags.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE)
        val tab = createTab(ABOUT_HOME_URL, id = "1", initialLoadFlags = loadFlags)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession)
            .loadUrl(
                url = ABOUT_HOME_URL,
                flags = loadFlags,
            )
    }

    @Test
    fun `loads URL with parent after linking`() {
        val middleware = LinkingMiddleware(scope)

        val parent = createTab("https://www.mozilla.org", id = "1")
        val child = createTab("https://www.firefox.com", id = "2", parent = parent)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(parent, child)),
                middleware = listOf(middleware),
            )

        val parentEngineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(parent.id, parentEngineSession))

        val childEngineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(child.id, childEngineSession, includeParent = true))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(childEngineSession).loadUrl(child.content.url, parentEngineSession)
    }

    @Test
    fun `loads URL without parent for extension URLs`() {
        val middleware = LinkingMiddleware(scope)

        val parent = createTab("https://www.mozilla.org", id = "1")
        val child = createTab("moz-extension://1234", id = "2", parent = parent)
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(parent, child)),
                middleware = listOf(middleware),
            )

        val parentEngineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(parent.id, parentEngineSession))

        val childEngineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(child.id, childEngineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(childEngineSession).loadUrl(child.content.url)
    }

    @Test
    fun `skips loading URL if specified in action`() {
        val middleware = LinkingMiddleware(scope)

        val tab = createTab("https://www.mozilla.org", id = "1")
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession, skipLoading = true))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession, never()).loadUrl(tab.content.url)
    }

    @Test
    fun `does nothing if linked tab does not exist`() {
        val middleware = LinkingMiddleware(scope)

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf()),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction("invalid", engineSession))

        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineSession, never()).loadUrl(anyString(), any(), any(), any(), any(), anyBoolean())
    }

    @Test
    fun `registers engine observer after linking`() = runTest {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val tab2 = createTab("https://www.mozilla.org", id = "2")

        val middleware = LinkingMiddleware(this)

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab1, tab2)),
                middleware = listOf(middleware),
            )

        val engineSession1: EngineSession = mock()
        val engineSession2: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab1.id, engineSession1))
        store.dispatch(EngineAction.LinkEngineSessionAction(tab2.id, engineSession2))

        // We only have a session for tab2 so we should only register an observer for tab2
        val engineObserver = store.state.findTab(tab2.id)?.engineState?.engineObserver
        assertNotNull(engineObserver)

        verify(engineSession2).register(engineObserver)
        engineObserver.onTitleChange("test")
        advanceUntilIdle()

        assertEquals("test", store.state.tabs[1].content.title)
    }

    @Test
    fun `unregisters engine observer before unlinking`() = runTest {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val tab2 = createTab("https://www.mozilla.org", id = "2")

        val middleware = LinkingMiddleware(scope)

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab1, tab2)),
                middleware = listOf(middleware),
            )

        val engineSession: EngineSession = mock()
        store.dispatch(EngineAction.LinkEngineSessionAction(tab1.id, engineSession))
        assertNotNull(store.state.findTab(tab1.id)?.engineState?.engineObserver)
        assertNull(store.state.findTab(tab2.id)?.engineState?.engineObserver)

        store.dispatch(EngineAction.UnlinkEngineSessionAction(tab1.id))
        store.dispatch(EngineAction.UnlinkEngineSessionAction(tab2.id))
        assertNull(store.state.findTab(tab1.id)?.engineState?.engineObserver)
        assertNull(store.state.findTab(tab2.id)?.engineState?.engineObserver)
    }

    @Test
    fun `registers engine observer when tab is added with engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val tab2 = createTab("https://www.mozilla.org", id = "2", engineSession = engineSession)

        val middleware = LinkingMiddleware(this)

        val store =
            BrowserStore(
                initialState = BrowserState(),
                middleware = listOf(middleware),
            )

        store.dispatch(TabListAction.AddTabAction(tab1))
        store.dispatch(TabListAction.AddTabAction(tab2))

        // We only have a session for tab2 so we should only register an observer for tab2
        val engineObserver = store.state.findTab(tab2.id)?.engineState?.engineObserver
        assertNotNull(engineObserver)
        verify(engineSession).register(engineObserver)
        engineObserver.onTitleChange("test")
        advanceUntilIdle()

        assertEquals("test", store.state.tabs[1].content.title)
    }
}
