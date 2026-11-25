/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.readerview

import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ReaderAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ReaderState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.readerview.ReaderViewFeature.Companion.READER_VIEW_EXTENSION_ID
import mozilla.components.support.test.mock
import mozilla.components.support.webextensions.BuiltInWebExtensionController
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class ReaderViewMiddlewareTest {

    @Test
    fun `state is updated to connect content script port when tab is added and engine session linked`() {
        val store =
            BrowserStore(
                initialState = BrowserState(),
                middleware = listOf(ReaderViewMiddleware()),
            )

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        store.dispatch(TabListAction.AddTabAction(tab))
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, mock()))

        val readerState = store.state.findTab(tab.id)!!.readerState
        assertTrue(readerState.connectRequired)
    }

    @Test
    fun `state is updated to disconnect content script port when tab is removed`() {
        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val engineSession: EngineSession = mock()
        val controller: BuiltInWebExtensionController = mock()
        val middleware =
            ReaderViewMiddleware().apply {
                extensionController = controller
            }
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(middleware),
            )

        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, engineSession))
        assertSame(engineSession, store.state.findTab(tab.id)?.engineState?.engineSession)
        verify(controller, never()).disconnectPort(engineSession, READER_VIEW_EXTENSION_ID)

        store.dispatch(EngineAction.UnlinkEngineSessionAction(tab.id))
        assertNull(store.state.findTab(tab.id)?.engineState?.engineSession)
        verify(controller).disconnectPort(engineSession, READER_VIEW_EXTENSION_ID)
    }

    @Test
    fun `state is updated to reconnect and trigger readerable check when new tab is selected`() {
        val tab1 = createTab("https://www.mozilla.org", id = "test-tab1")
        val tab2 =
            createTab(
                "https://www.firefox.com",
                id = "test-tab2",
                readerState = ReaderState(readerable = true),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab1, tab2)),
                middleware = listOf(ReaderViewMiddleware()),
            )
        assertFalse(store.state.findTab(tab1.id)!!.readerState.connectRequired)
        assertFalse(store.state.findTab(tab1.id)!!.readerState.checkRequired)
        assertFalse(store.state.findTab(tab1.id)!!.readerState.readerable)
        assertFalse(store.state.findTab(tab2.id)!!.readerState.connectRequired)
        assertFalse(store.state.findTab(tab2.id)!!.readerState.checkRequired)
        assertTrue(store.state.findTab(tab2.id)!!.readerState.readerable)

        store.dispatch(TabListAction.SelectTabAction(tab2.id))
        assertFalse(store.state.findTab(tab1.id)!!.readerState.readerable)
        assertFalse(store.state.findTab(tab1.id)!!.readerState.checkRequired)
        assertFalse(store.state.findTab(tab1.id)!!.readerState.connectRequired)
        assertFalse(store.state.findTab(tab2.id)!!.readerState.readerable)
        assertTrue(store.state.findTab(tab2.id)!!.readerState.checkRequired)
        assertTrue(store.state.findTab(tab2.id)!!.readerState.connectRequired)
    }

    @Test
    fun `state is updated to trigger readerable check when URL changes`() {
        val tab = createTab("https://www.mozilla.org", id = "test-tab1")
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )
        assertFalse(store.state.findTab(tab.id)!!.readerState.checkRequired)

        store.dispatch(ContentAction.UpdateUrlAction(tab.id, "https://www.firefox.com"))
        assertTrue(store.state.findTab(tab.id)!!.readerState.checkRequired)
    }

    @Test
    fun `state is updated to enter and leave reader view when URL changes`() {
        val tab =
            createTab(
                "https://www.mozilla.org",
                id = "test-tab1",
                readerState = ReaderState(active = false, baseUrl = "moz-extension://123"),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )
        assertFalse(store.state.findTab(tab.id)!!.readerState.active)

        store.dispatch(ContentAction.UpdateUrlAction(tab.id, "moz-extension://123?url=articleLink"))
        assertTrue(store.state.findTab(tab.id)!!.readerState.active)

        store.dispatch(ContentAction.UpdateUrlAction(tab.id, "https://www.firefox.com"))
        assertFalse(store.state.findTab(tab.id)!!.readerState.active)
        assertFalse(store.state.findTab(tab.id)!!.readerState.readerable)
        assertTrue(store.state.findTab(tab.id)!!.readerState.checkRequired)
        assertNull(store.state.findTab(tab.id)!!.readerState.activeUrl)
    }

    @Test
    fun `state is updated to mask extension page URL when navigating to reader view page`() {
        val tab =
            createTab(
                "https://www.mozilla.org",
                id = "test-tab1",
                readerState =
                    ReaderState(
                        active = true,
                        baseUrl = "moz-extension://123",
                        activeUrl = "https://mozilla.org/article1",
                    ),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )

        store.dispatch(
            ContentAction.UpdateUrlAction(
                tab.id,
                "moz-extension://123?url=https%3A%2F%2Fmozilla.org%2Farticle1",
            )
        )

        assertTrue(store.state.findTab(tab.id)!!.readerState.active)
        assertEquals("https://mozilla.org/article1", store.state.findTab(tab.id)!!.content.url)
    }

    @Test
    fun `state is updated to mask extension page URL when reader view connects`() {
        val tab =
            createTab(
                "moz-extension://123?url=https%3A%2F%2Fmozilla.org%2Farticle1",
                id = "test-tab1",
                readerState =
                    ReaderState(
                        active = true,
                        baseUrl = "moz-extension://123",
                    ),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )

        store.dispatch(
            ReaderAction.UpdateReaderActiveUrlAction(
                tab.id,
                activeUrl = "https://mozilla.org/article1",
            )
        )

        assertEquals("https://mozilla.org/article1", store.state.findTab(tab.id)!!.content.url)
    }

    @Test
    fun `GIVEN a reader page for an article WHEN a non-http active URL is reported THEN the article URL is kept`() {
        val articleUrl = "https://www.mozilla.org/article1"
        val tab =
            createTab(
                articleUrl,
                id = "test-tab1",
                readerState =
                    ReaderState(
                        active = true,
                        baseUrl = "moz-extension://123",
                        activeUrl = articleUrl,
                    ),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )

        listOf("javascript:alert(1)", "jav\tascript:alert(1)", "file:///sdcard/a.html", "data:text/html,a").forEach {
            store.dispatch(ReaderAction.UpdateReaderActiveUrlAction(tab.id, activeUrl = it))
        }

        assertEquals(articleUrl, store.state.findTab(tab.id)!!.readerState.activeUrl)
        assertEquals(articleUrl, store.state.findTab(tab.id)!!.content.url)
    }

    @Test
    fun `GIVEN navigating to a new URL WHEN that is not a reader mode URL THEN clear reader mode status for the tab`() {
        val tab =
            createTab(
                "https://www.mozilla.org",
                id = "test-tab1",
                readerState =
                    ReaderState(
                        active = true,
                        baseUrl = "moz-extension://123",
                        activeUrl = "https://mozilla.org/article1",
                    ),
            )
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(tab)),
                middleware = listOf(ReaderViewMiddleware()),
            )

        store.dispatch(
            ContentAction.UpdateUrlAction(
                tab.id,
                "https://mozilla.org/article1",
            )
        )

        assertFalse(store.state.findTab(tab.id)!!.readerState.active)
    }
}
