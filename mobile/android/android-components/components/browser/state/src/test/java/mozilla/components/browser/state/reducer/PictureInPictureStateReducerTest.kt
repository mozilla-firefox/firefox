/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.browser.state.state.PictureInPictureWindowState
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class PictureInPictureStateReducerTest {
    private val engineSession: EngineSession = mock()
    private val session = PictureInPictureSession(tabId = "tab", engineSession = engineSession)

    @Test
    fun `WHEN RequestAction is dispatched THEN the window waits on the tab`() {
        val result =
            PictureInPictureStateReducer.reduce(
                BrowserState(tabs = listOf(tab())),
                PictureInPictureAction.RequestAction("tab"),
            )

        assertEquals(PictureInPictureWindowState(requestedTabId = "tab"), result.pictureInPictureWindow)
    }

    @Test
    fun `GIVEN the window has a session WHEN RequestAction is dispatched THEN the window keeps its session and waits on the tab`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow = PictureInPictureWindowState(session = session),
            )

        val result = PictureInPictureStateReducer.reduce(state, PictureInPictureAction.RequestAction("tab"))

        assertEquals(
            PictureInPictureWindowState(session = session, requestedTabId = "tab"),
            result.pictureInPictureWindow,
        )
    }

    @Test
    fun `GIVEN the window waits on the tab WHEN ConsumeRequestAction is dispatched THEN the window stops waiting`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow = PictureInPictureWindowState(requestedTabId = "tab"),
            )

        val result = PictureInPictureStateReducer.reduce(state, PictureInPictureAction.ConsumeRequestAction("tab"))

        assertEquals(PictureInPictureWindowState(), result.pictureInPictureWindow)
    }

    @Test
    fun `GIVEN the window waits on another tab WHEN ConsumeRequestAction is dispatched THEN the window keeps waiting`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow = PictureInPictureWindowState(requestedTabId = "other"),
            )

        val result = PictureInPictureStateReducer.reduce(state, PictureInPictureAction.ConsumeRequestAction("tab"))

        assertEquals("other", result.pictureInPictureWindow.requestedTabId)
    }

    @Test
    fun `GIVEN the window waits on the tab WHEN EnterAction and then ConsumeRequestAction are dispatched THEN the window has the session and stops waiting`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow = PictureInPictureWindowState(requestedTabId = "tab"),
            )

        val entered =
            PictureInPictureStateReducer.reduce(
                state,
                PictureInPictureAction.EnterAction(tabId = "tab", engineSession = engineSession),
            )
        val result = PictureInPictureStateReducer.reduce(entered, PictureInPictureAction.ConsumeRequestAction("tab"))

        assertEquals(
            PictureInPictureWindowState(session = session, requestedTabId = "tab"),
            entered.pictureInPictureWindow,
        )
        assertEquals(PictureInPictureWindowState(session = session), result.pictureInPictureWindow)
    }

    @Test
    fun `WHEN EnterAction is dispatched THEN the tab is in a window rendering its session and legacy picture-in-picture mode is untouched`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow =
                    PictureInPictureWindowState(
                        session = PictureInPictureSession(tabId = "other", engineSession = mock())
                    ),
            )

        val result =
            PictureInPictureStateReducer.reduce(
                state,
                PictureInPictureAction.EnterAction(tabId = "tab", engineSession = engineSession),
            )

        assertFalse(result.findTab("tab")!!.content.pictureInPictureEnabled)
        assertEquals(PictureInPictureWindowState(session = session), result.pictureInPictureWindow)
    }

    @Test
    fun `GIVEN a window renders the session of the tab WHEN ExitAction is dispatched THEN the session is dropped`() {
        val state =
            BrowserState(
                tabs = listOf(tab()),
                pictureInPictureWindow = PictureInPictureWindowState(session = session),
            )

        val result = PictureInPictureStateReducer.reduce(state, PictureInPictureAction.ExitAction("tab"))

        assertEquals(PictureInPictureWindowState(), result.pictureInPictureWindow)
    }

    @Test
    fun `GIVEN the window is up WHEN CloseWindowAction is dispatched THEN the window state is cleared`() {
        val state = BrowserState(pictureInPictureWindow = PictureInPictureWindowState(session = session))

        val result = PictureInPictureStateReducer.reduce(state, PictureInPictureAction.CloseWindowAction)

        assertEquals(PictureInPictureWindowState(), result.pictureInPictureWindow)
    }

    private fun tab() = createTab(url = "https://www.mozilla.org", id = "tab")
}
