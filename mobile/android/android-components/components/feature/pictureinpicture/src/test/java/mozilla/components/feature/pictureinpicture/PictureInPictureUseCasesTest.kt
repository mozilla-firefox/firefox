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
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

class PictureInPictureUseCasesTest {
    private val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    private val dispatcher = StandardTestDispatcher()
    private val scope = TestScope(dispatcher)
    private val tabEngineSession: EngineSession = mock()
    private val pictureInPictureEngineSession: EngineSession = mock()
    private val releaseStore = BrowserStore(BrowserState(tabs = listOf(tabWithEngineSession("tab", tabEngineSession))))
    private val release = PictureInPictureUseCases(releaseStore, scope).release

    @Test
    fun `WHEN requesting without a tab ID THEN the selected tab is requested`() {
        val useCases = PictureInPictureUseCases(createStore(selectedTabId = "selected"))

        useCases.request()

        middleware.assertLastAction(PictureInPictureAction.RequestAction::class) { action ->
            assertEquals("selected", action.tabId)
        }
    }

    @Test
    fun `WHEN requesting with a tab ID THEN that tab is requested`() {
        val useCases = PictureInPictureUseCases(createStore(selectedTabId = "selected"))

        useCases.request("other")

        middleware.assertLastAction(PictureInPictureAction.RequestAction::class) { action ->
            assertEquals("other", action.tabId)
        }
    }

    @Test
    fun `GIVEN no tab is selected WHEN requesting without a tab ID THEN nothing is requested`() {
        val useCases = PictureInPictureUseCases(createStore(selectedTabId = null))

        useCases.request()

        middleware.assertNotDispatched(PictureInPictureAction.RequestAction::class)
    }

    @Test
    fun `WHEN closing the window THEN the close action is dispatched`() {
        val useCases = PictureInPictureUseCases(createStore(selectedTabId = "selected"))

        useCases.closeWindow()

        middleware.assertLastAction(PictureInPictureAction.CloseWindowAction::class) {}
    }

    @Test
    fun `WHEN releasing a session THEN it is closed and the tab exits picture-in-picture`() {
        release(PictureInPictureSession("tab", pictureInPictureEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(pictureInPictureEngineSession).close()
        verify(tabEngineSession).exitPictureInPicture()
    }

    @Test
    fun `WHEN releasing a session of the kept tab THEN it is closed and the tab stays in picture-in-picture`() {
        release(PictureInPictureSession("tab", pictureInPictureEngineSession), keepTabId = "tab")
        dispatcher.scheduler.advanceUntilIdle()

        verify(pictureInPictureEngineSession).close()
        verify(tabEngineSession, never()).exitPictureInPicture()
    }

    @Test
    fun `WHEN releasing a session while keeping another tab THEN the released tab exits picture-in-picture`() {
        release(PictureInPictureSession("tab", pictureInPictureEngineSession), keepTabId = "other")
        dispatcher.scheduler.advanceUntilIdle()

        verify(pictureInPictureEngineSession).close()
        verify(tabEngineSession).exitPictureInPicture()
    }

    @Test
    fun `GIVEN the tab no longer exists WHEN releasing its session THEN the session is still closed`() {
        release(PictureInPictureSession("missing", pictureInPictureEngineSession))
        dispatcher.scheduler.advanceUntilIdle()

        verify(pictureInPictureEngineSession).close()
        verify(tabEngineSession, never()).exitPictureInPicture()
    }

    private fun createStore(selectedTabId: String?) =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs =
                        listOf(
                            createTab("https://www.mozilla.org", id = "selected"),
                            createTab("https://example.com", id = "other"),
                        ),
                    selectedTabId = selectedTabId,
                ),
            middleware = listOf(middleware),
        )

    private fun tabWithEngineSession(id: String, engineSession: EngineSession) =
        TabSessionState(
            id = id,
            content = ContentState(url = "https://www.mozilla.org"),
            engineState = EngineState(engineSession = engineSession),
        )
}
