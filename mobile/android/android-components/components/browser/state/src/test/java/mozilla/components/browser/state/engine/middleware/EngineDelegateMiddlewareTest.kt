/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine.middleware

import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mockito.ArgumentMatchers
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

class EngineDelegateMiddlewareTest {

    @Test
    fun `LoadUrlAction for tab without engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for private tab without engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession(private = true)

        val tab = createTab("https://www.mozilla.org", id = "test-tab", private = true)
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = true, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for container tab without engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession(contextId = "test-container")

        val tab = createTab("https://www.mozilla.org", id = "test-tab", contextId = "test-container")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = "test-container")
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for tab with engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "test-tab")
                                    .copy(engineState = EngineState(engineSession))
                            )
                    ),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine, never()).createSession(ArgumentMatchers.anyBoolean(), ArgumentMatchers.anyString())
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for private tab with engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "test-tab", private = true)
                                    .copy(engineState = EngineState(engineSession))
                            )
                    ),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine, never()).createSession(ArgumentMatchers.anyBoolean(), ArgumentMatchers.anyString())
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for container tab with engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "test-tab", contextId = "test-container")
                                    .copy(engineState = EngineState(engineSession))
                            )
                    ),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine, never())
            .createSession(
                ArgumentMatchers.anyBoolean(),
                ArgumentMatchers.anyString(),
            )
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction omits parent tab when includeParent is false`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val parentEngineSession: EngineSession = mock()

        val parent =
            createTab("https://getpocket.com", id = "parent-tab").copy(engineState = EngineState(parentEngineSession))
        val tab = createTab("https://www.mozilla.org", id = "test-tab", parent = parent)

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(parent, tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
                includeParent = false,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com", null)
        assertEquals(parentEngineSession, store.state.tabs[0].engineState.engineSession)
        assertEquals(engineSession, store.state.tabs[1].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction includes parent tab when includeParent is true`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val parentEngineSession: EngineSession = mock()

        val parent =
            createTab("https://getpocket.com", id = "parent-tab").copy(engineState = EngineState(parentEngineSession))
        val tab = createTab("https://www.mozilla.org", id = "test-tab", parent = parent)

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(parent, tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
                includeParent = true,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com", parentEngineSession)
        assertEquals(parentEngineSession, store.state.tabs[0].engineState.engineSession)
        assertEquals(engineSession, store.state.tabs[1].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for tab with parent tab without engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val parent = createTab("https://getpocket.com", id = "parent-tab")
        val tab = createTab("https://www.mozilla.org", id = "test-tab", parent = parent)

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(parent, tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine, times(1)).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.firefox.com")
        assertEquals(engineSession, store.state.tabs[1].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction with flags and additional headers`() = runTest {
        val engineSession: EngineSession = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = mock(),
                        scope = this,
                    ),
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "test-tab")
                                    .copy(engineState = EngineState(engineSession))
                            )
                    ),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.firefox.com",
                EngineSession.LoadUrlFlags.external(),
                mapOf(
                    "X-Coffee" to "Large",
                    "X-Sugar" to "None",
                ),
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engineSession, times(1))
            .loadUrl(
                "https://www.firefox.com",
                flags = EngineSession.LoadUrlFlags.external(),
                additionalHeaders =
                    mapOf(
                        "X-Coffee" to "Large",
                        "X-Sugar" to "None",
                    ),
            )
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for tab with same url and without engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "test-tab",
                "https://www.mozilla.org",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).loadUrl("https://www.mozilla.org")

        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadUrlAction for not existing tab`() = runTest {
        val engine: Engine = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab"))),
            )

        store.dispatch(
            EngineAction.LoadUrlAction(
                "unknown-tab",
                "https://www.mozilla.org",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine, never()).createSession(ArgumentMatchers.anyBoolean(), ArgumentMatchers.anyString())
        assertNull(store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `LoadDataAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.LoadDataAction(
                "test-tab",
                data = "foobar data",
                mimeType = "something/important",
                encoding = "UTF-16",
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1))
            .loadData(
                data = "foobar data",
                mimeType = "something/important",
                encoding = "UTF-16",
            )
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `ReloadAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.ReloadAction(
                "test-tab",
                flags = EngineSession.LoadUrlFlags.select(EngineSession.LoadUrlFlags.BYPASS_CACHE),
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1))
            .reload(EngineSession.LoadUrlFlags.select(EngineSession.LoadUrlFlags.BYPASS_CACHE))
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `GoForwardAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(EngineAction.GoForwardAction("test-tab"))

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).goForward()
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `GoBackAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(EngineAction.GoBackAction("test-tab"))

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).goBack()
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `GoToHistoryIndexAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.GoToHistoryIndexAction(
                "test-tab",
                index = 42,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).goToHistoryIndex(42)
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `ToggleDesktopModeAction - Enable desktop mode`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.ToggleDesktopModeAction(
                "test-tab",
                enable = true,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).toggleDesktopMode(enable = true, reload = true)
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `ToggleDesktopModeAction - Disable desktop mode`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.ToggleDesktopModeAction(
                "test-tab",
                enable = false,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).toggleDesktopMode(enable = false, reload = true)
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `ExitFullscreenModeAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(EngineAction.ExitFullScreenModeAction("test-tab"))

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).exitFullScreenMode()
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `ClearDataAction for tab without EngineSession`() = runTest {
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()

        val tab = createTab("https://www.mozilla.org", id = "test-tab")
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(
            EngineAction.ClearDataAction(
                "test-tab",
                data = Engine.BrowsingData.allCaches(),
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engine).createSession(private = false, contextId = null)
        verify(engineSession, times(1)).clearData(Engine.BrowsingData.allCaches())
        assertEquals(engineSession, store.state.tabs[0].engineState.engineSession)
    }

    @Test
    fun `PurgeHistoryAction - calls purgeHistory on engine session instances`() = runTest {
        val engineSession1: EngineSession = mock()
        val engineSession2: EngineSession = mock()

        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = mock(),
                        scope = this,
                    ),
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org")
                                    .copy(engineState = EngineState(engineSession = null, engineSessionState = mock())),
                                createTab("https://www.firefox.com")
                                    .copy(
                                        engineState =
                                            EngineState(engineSession = engineSession1, engineSessionState = mock())
                                    ),
                            ),
                        customTabs =
                            listOf(
                                createCustomTab("http://www.theverge.com")
                                    .copy(engineState = EngineState(engineSession = null, engineSessionState = mock())),
                                createCustomTab("https://www.google.com")
                                    .copy(
                                        engineState =
                                            EngineState(engineSession = engineSession2, engineSessionState = mock())
                                    ),
                            ),
                    ),
            )

        store.dispatch(EngineAction.PurgeHistoryAction)

        testScheduler.advanceUntilIdle()

        verify(engineSession1).purgeHistory()
        verify(engineSession2).purgeHistory()
    }

    @Test
    fun `TranslateAction correctly sets progress state AND begins a translation`() = runTest {
        val tab = createTab("https://www.mozilla.org")
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        assertFalse(store.state.findTab(tab.id)?.translationsState?.isTranslateProcessing!!)

        store.dispatch(
            TranslationsAction.TranslateAction(
                tabId = tab.id,
                fromLanguage = "es",
                toLanguage = "en",
                options = null,
            )
        )

        testScheduler.advanceUntilIdle()

        verify(engineSession).requestTranslate(any(), any(), any())
        assertTrue(store.state.findTab(tab.id)?.translationsState?.isTranslateProcessing!!)
    }

    @Test
    fun `TranslateRestoreAction correctly sets progress state AND begins a restore`() = runTest {
        val tab = createTab("https://www.mozilla.org")
        val engineSession: EngineSession = mock()
        val engine: Engine = mock()
        doReturn(engineSession).`when`(engine).createSession()
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = engine,
                        scope = this,
                    ),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        assertFalse(store.state.findTab(tab.id)?.translationsState?.isRestoreProcessing!!)

        store.dispatch(TranslationsAction.TranslateRestoreAction(tabId = tab.id))

        testScheduler.advanceUntilIdle()

        verify(engineSession).requestTranslationRestore()
        assertTrue(store.state.findTab(tab.id)?.translationsState?.isRestoreProcessing!!)
    }

    @Test
    fun `RequestAction for picture-in-picture asks the engine session`() = runTest {
        val engineSession: EngineSession = mock()
        val tab = createTab("https://www.mozilla.org", engineSession = engineSession)
        val store =
            BrowserStore(
                middleware = EngineMiddleware.create(engine = mock(), scope = this),
                initialState = BrowserState(tabs = listOf(tab)),
            )

        store.dispatch(PictureInPictureAction.RequestAction(tabId = tab.id))
        testScheduler.advanceUntilIdle()

        verify(engineSession).requestPictureInPicture()
    }

    @Test
    fun `FlushEngineSessionStateAction correctly triggers the engine session state flush on the selected tab`() =
        runTest {
            val tab = createTab("https://www.mozilla.org")
            val engineSession: EngineSession = mock()
            val engine: Engine = mock()
            doReturn(engineSession).`when`(engine).createSession()
            val store =
                BrowserStore(
                    middleware =
                        EngineMiddleware.create(
                            engine = engine,
                            scope = this,
                        ),
                    initialState = BrowserState(tabs = listOf(tab)),
                )

            store.dispatch(EngineAction.FlushEngineSessionStateAction(tabId = tab.id))

            testScheduler.advanceUntilIdle()

            verify(engineSession).flushSessionState()
        }

    @Test
    fun `FlushEngineSessionStateAction correctly triggers the engine session state flush on the custom tab`() =
        runTest {
            val tab = createCustomTab("https://www.mozilla.org")
            val engineSession: EngineSession = mock()
            val engine: Engine = mock()
            doReturn(engineSession).`when`(engine).createSession()
            val store =
                BrowserStore(
                    middleware =
                        EngineMiddleware.create(
                            engine = engine,
                            scope = this,
                        ),
                    initialState = BrowserState(customTabs = listOf(tab)),
                )

            store.dispatch(EngineAction.FlushEngineSessionStateAction(tabId = tab.id))

            testScheduler.advanceUntilIdle()

            verify(engineSession).flushSessionState()
        }
}
