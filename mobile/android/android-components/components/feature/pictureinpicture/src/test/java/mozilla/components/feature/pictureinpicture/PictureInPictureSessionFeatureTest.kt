/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineView
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class PictureInPictureSessionFeatureTest {
    private val testDispatcher = StandardTestDispatcher()
    private val engineView: EngineView = mock()
    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
    private val store =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs =
                        listOf(
                            createTab("https://www.mozilla.org", id = "tab"),
                            createTab("https://www.firefox.com", id = "other"),
                        )
                ),
            middleware = listOf(captureActionsMiddleware),
        )
    private var videoGoneCount = 0
    private val returnedTabIds = mutableListOf<String>()
    private val feature =
        PictureInPictureSessionFeature(
            store = store,
            engineView = engineView,
            mainDispatcher = testDispatcher,
            onVideoGone = { videoGoneCount++ },
            onReturnToTab = { returnedTabIds.add(it) },
            pictureInPictureUseCases = PictureInPictureUseCases(store),
        )

    @Test
    fun `GIVEN a video is requested WHEN the window gets a session THEN it is rendered`() {
        val engineSession: EngineSession = mock()
        request("tab")

        enter("tab", engineSession)

        verify(engineView).render(engineSession)
        verify(engineSession).onPipModeChanged(false)
    }

    @Test
    fun `GIVEN a session is rendered WHEN the window state changes otherwise THEN it is not rendered again`() {
        val engineSession: EngineSession = mock()
        request("tab")
        enter("tab", engineSession)

        request("other")

        verify(engineView, times(1)).render(engineSession)
    }

    @Test
    fun `GIVEN a session is rendered WHEN another one replaces it THEN the new one is rendered`() {
        val second: EngineSession = mock()
        request("tab")
        enter("tab", mock())

        enter("other", second)

        verify(engineView).render(second)
    }

    @Test
    fun `GIVEN the window got a session elsewhere WHEN the feature starts THEN it is rendered without a request`() {
        val engineSession: EngineSession = mock()
        store.dispatch(PictureInPictureAction.RequestAction("tab"))
        enter("tab", engineSession)
        captureActionsMiddleware.reset()

        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        verify(engineView).render(engineSession)
        captureActionsMiddleware.assertNotDispatched(PictureInPictureAction.RequestAction::class)
    }

    @Test
    fun `GIVEN the window is up WHEN the feature stops THEN the view is released and the window closes`() {
        request("tab")
        enter("tab", mock())

        feature.stop()

        verify(engineView).release()
        captureActionsMiddleware.assertLastAction(PictureInPictureAction.CloseWindowAction::class) {}
        assertNull(store.state.pictureInPictureWindow.session)
    }

    @Test
    fun `GIVEN no video was requested WHEN the feature stops THEN nothing is dispatched`() {
        feature.stop()

        verify(engineView, never()).release()
        captureActionsMiddleware.assertNotDispatched(PictureInPictureAction.CloseWindowAction::class)
    }

    @Test
    fun `GIVEN a session is rendered WHEN its video leaves picture-in-picture THEN the video is gone`() {
        request("tab")
        enter("tab", mock())
        store.dispatch(PictureInPictureAction.ConsumeRequestAction("tab"))

        store.dispatch(PictureInPictureAction.ExitAction("tab"))
        testDispatcher.scheduler.advanceUntilIdle()
        request("other")

        assertEquals(1, videoGoneCount)
    }

    @Test
    fun `GIVEN a session was replaced WHEN its video leaves picture-in-picture THEN the video is not gone`() {
        request("tab")
        enter("tab", mock())
        enter("other", mock())

        store.dispatch(PictureInPictureAction.ExitAction("tab"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(0, videoGoneCount)
    }

    @Test
    fun `GIVEN a video is requested WHEN the request finishes without a video THEN the video is gone`() {
        request("tab")
        assertEquals(0, videoGoneCount)

        store.dispatch(PictureInPictureAction.ConsumeRequestAction("tab"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(1, videoGoneCount)
    }

    @Test
    fun `GIVEN a video is requested WHEN the request finishes with a video THEN the video is shown`() {
        request("tab")
        enter("tab", mock())

        store.dispatch(PictureInPictureAction.ConsumeRequestAction("tab"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(0, videoGoneCount)
    }

    @Test
    fun `GIVEN a session is rendered WHEN a request for another video finishes without a video THEN the video is kept`() {
        request("tab")
        enter("tab", mock())
        request("other")

        store.dispatch(PictureInPictureAction.ConsumeRequestAction("other"))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(0, videoGoneCount)
        assertEquals("tab", store.state.pictureInPictureWindow.session?.tabId)
    }

    @Test
    fun `GIVEN a session is rendered WHEN the activity changes picture-in-picture mode THEN the session is told`() {
        val engineSession: EngineSession = mock()
        request("tab")
        enter("tab", engineSession)

        feature.onPictureInPictureModeChanged(true)

        verify(engineSession).onPipModeChanged(true)
        assertEquals(emptyList<String>(), returnedTabIds)
    }

    @Test
    fun `GIVEN a session is rendered WHEN the activity leaves picture-in-picture mode THEN the window is expanded`() {
        request("tab")
        enter("tab", mock())
        feature.onPictureInPictureModeChanged(true)

        feature.onPictureInPictureModeChanged(false)

        assertEquals(listOf("tab"), returnedTabIds)
    }

    @Test
    fun `GIVEN the feature stopped WHEN the activity leaves picture-in-picture mode THEN the window is not expanded`() {
        request("tab")
        enter("tab", mock())
        feature.onPictureInPictureModeChanged(true)

        feature.stop()
        feature.onPictureInPictureModeChanged(false)

        assertEquals(emptyList<String>(), returnedTabIds)
    }

    private fun request(tabId: String) {
        store.dispatch(PictureInPictureAction.RequestAction(tabId))
        feature.start()
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private fun enter(tabId: String, engineSession: EngineSession) {
        store.dispatch(PictureInPictureAction.EnterAction(tabId, engineSession))
        testDispatcher.scheduler.advanceUntilIdle()
    }
}
