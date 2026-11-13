/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import android.content.Context
import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class PictureInPictureWindowFeatureTest {
    private val testDispatcher = StandardTestDispatcher()
    private val context: Context = spy(testContext).also { doNothing().`when`(it).startActivity(any()) }
    private val store =
        BrowserStore(
            initialState =
                BrowserState(
                    tabs =
                        listOf(
                            createTab("https://www.mozilla.org", id = "tab"),
                            createTab("https://www.firefox.com", id = "other"),
                        )
                )
        )
    private val feature =
        PictureInPictureWindowFeature(
            context = context,
            store = store,
            activityClass = TestPictureInPictureActivity::class.java,
            mainDispatcher = testDispatcher,
        )

    @Test
    fun `GIVEN a video is requested WHEN its session arrives THEN the window is started for it`() {
        startAndRequest("tab")

        enter("tab")

        val intent = argumentCaptor<Intent>()
        verify(context).startActivity(intent.capture())
        assertEquals(TestPictureInPictureActivity::class.java.name, intent.value.component?.className)
        assertTrue(intent.value.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
    }

    @Test
    fun `GIVEN the window is up WHEN its session is replaced THEN the window is not started again`() {
        startAndRequest("tab")
        enter("tab")

        request("other")
        enter("other")

        verify(context, times(1)).startActivity(any())
    }

    @Test
    fun `GIVEN a video is requested WHEN no session arrives THEN the window is not started`() {
        startAndRequest("tab")

        store.dispatch(PictureInPictureAction.ConsumeRequestAction("tab"))
        testDispatcher.scheduler.advanceUntilIdle()

        verify(context, never()).startActivity(any())
    }

    @Test
    fun `GIVEN the feature is stopped WHEN a session arrives THEN the window is not started`() {
        startAndRequest("tab")
        feature.stop()

        enter("tab")

        verify(context, never()).startActivity(any())
    }

    private fun startAndRequest(tabId: String) {
        feature.start()
        request(tabId)
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private fun request(tabId: String) {
        store.dispatch(PictureInPictureAction.RequestAction(tabId))
    }

    private fun enter(tabId: String) {
        store.dispatch(PictureInPictureAction.EnterAction(tabId, mock<EngineSession>()))
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private class TestPictureInPictureActivity : AbstractPictureInPictureActivity() {
        override val store: BrowserStore = mock()
        override val engine: Engine = mock()
        override val pictureInPictureUseCases = PictureInPictureUseCases(store)

        override fun onReturnToTab(tabId: String) = Unit
    }
}
