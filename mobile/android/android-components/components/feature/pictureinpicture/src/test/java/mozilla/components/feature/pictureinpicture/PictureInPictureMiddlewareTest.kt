/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.browser.state.state.PictureInPictureWindowState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

class PictureInPictureMiddlewareTest {
    private val dispatcher = StandardTestDispatcher()
    private val scope = TestScope(dispatcher)
    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
    private val tabEngineSession: EngineSession = mock()
    private val otherTabEngineSession: EngineSession = mock()

    @Test
    fun `GIVEN a window waits on the tab WHEN the engine asks for a window THEN the window gets the session`() {
        val newEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession)),
                window = PictureInPictureWindowState(requestedTabId = "tab"),
            )

        store.dispatch(PictureInPictureAction.EnterAction("tab", newEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(tabEngineSession, never()).exitPictureInPicture()
        assertEquals(PictureInPictureSession("tab", newEngineSession), store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN the window has a session of another tab WHEN the engine asks for a window THEN the replaced session is released`() {
        val replacedEngineSession: EngineSession = mock()
        val newEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession), tab("other", otherTabEngineSession)),
                window =
                    PictureInPictureWindowState(
                        session = PictureInPictureSession("other", replacedEngineSession),
                        requestedTabId = "tab",
                    ),
            )

        store.dispatch(PictureInPictureAction.EnterAction("tab", newEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(replacedEngineSession).close()
        verify(otherTabEngineSession).exitPictureInPicture()
        verify(tabEngineSession, never()).exitPictureInPicture()
        assertEquals(PictureInPictureSession("tab", newEngineSession), store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN the window has a session of the same tab WHEN the engine asks for a window THEN the tab stays in picture-in-picture`() {
        val replacedEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession)),
                window =
                    PictureInPictureWindowState(
                        session = PictureInPictureSession("tab", replacedEngineSession),
                        requestedTabId = "tab",
                    ),
            )

        store.dispatch(PictureInPictureAction.EnterAction("tab", mock()))
        dispatcher.scheduler.advanceUntilIdle()

        verify(replacedEngineSession).close()
        verify(tabEngineSession, never()).exitPictureInPicture()
    }

    @Test
    fun `GIVEN no window and no session WHEN the engine asks for a window THEN the video is not shown`() {
        val newEngineSession: EngineSession = mock()
        val store = createStore(tabs = listOf(tab("tab", tabEngineSession)))

        store.dispatch(PictureInPictureAction.EnterAction("tab", newEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(newEngineSession).close()
        verify(tabEngineSession).exitPictureInPicture()
        captureActionsMiddleware.assertNotDispatched(PictureInPictureAction.EnterAction::class)
        assertNull(store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN a window waits on another tab WHEN the engine asks for a window THEN the video is not shown`() {
        val shownEngineSession: EngineSession = mock()
        val newEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession), tab("other", otherTabEngineSession)),
                window =
                    PictureInPictureWindowState(
                        session = PictureInPictureSession("other", shownEngineSession),
                        requestedTabId = "other",
                    ),
            )

        store.dispatch(PictureInPictureAction.EnterAction("tab", newEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(newEngineSession).close()
        verify(tabEngineSession).exitPictureInPicture()
        verify(shownEngineSession, never()).close()
        captureActionsMiddleware.assertNotDispatched(PictureInPictureAction.EnterAction::class)
        assertEquals(PictureInPictureSession("other", shownEngineSession), store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN a window waits on a tab that is gone WHEN the engine asks for a window for it THEN the session is closed`() {
        val newEngineSession: EngineSession = mock()
        val store = createStore(tabs = emptyList(), window = PictureInPictureWindowState(requestedTabId = "tab"))

        store.dispatch(PictureInPictureAction.EnterAction("tab", newEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(newEngineSession).close()
        captureActionsMiddleware.assertNotDispatched(PictureInPictureAction.EnterAction::class)
        assertNull(store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN a window renders the session of the tab WHEN the engine takes the video out THEN the session is closed and dropped`() {
        val shownEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession)),
                window = PictureInPictureWindowState(session = PictureInPictureSession("tab", shownEngineSession)),
            )

        store.dispatch(PictureInPictureAction.ExitAction("tab"))
        dispatcher.scheduler.advanceUntilIdle()

        verify(shownEngineSession).close()
        verify(tabEngineSession, never()).exitPictureInPicture()
        assertEquals(PictureInPictureWindowState(), store.state.pictureInPictureWindow)
    }

    @Test
    fun `WHEN the window that is up closes THEN its session is released`() {
        val shownEngineSession: EngineSession = mock()
        val store =
            createStore(
                tabs = listOf(tab("tab", tabEngineSession)),
                window = PictureInPictureWindowState(session = PictureInPictureSession("tab", shownEngineSession)),
            )

        store.dispatch(PictureInPictureAction.CloseWindowAction)
        dispatcher.scheduler.advanceUntilIdle()

        verify(shownEngineSession).close()
        verify(tabEngineSession).exitPictureInPicture()
        assertEquals(PictureInPictureWindowState(), store.state.pictureInPictureWindow)
    }

    private fun createStore(
        tabs: List<TabSessionState>,
        window: PictureInPictureWindowState = PictureInPictureWindowState(),
    ) =
        BrowserStore(
            initialState = BrowserState(tabs = tabs, pictureInPictureWindow = window),
            middleware = listOf(PictureInPictureMiddleware(scope), captureActionsMiddleware),
        )

    private fun tab(id: String, engineSession: EngineSession) =
        TabSessionState(
            id = id,
            content = ContentState(url = "https://www.mozilla.org"),
            engineState = EngineState(engineSession = engineSession),
        )
}
