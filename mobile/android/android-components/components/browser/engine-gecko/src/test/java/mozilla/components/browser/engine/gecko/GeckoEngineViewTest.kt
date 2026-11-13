/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Looper.getMainLooper
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import mozilla.components.browser.engine.gecko.GeckoEngineView.Companion.DARK_COVER
import mozilla.components.browser.engine.gecko.facts.GeckoEngineViewFacts
import mozilla.components.browser.engine.gecko.selection.GeckoSelectionActionDelegate
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.concept.engine.selection.SelectionActionDelegate
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import mozilla.components.test.ReflectionUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.robolectric.Robolectric.buildActivity
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class GeckoEngineViewTest {

    private val context: Context
        get() = buildActivity(Activity::class.java).get()

    @Test
    fun render() {
        val engineView = GeckoEngineView(context)
        val engineSession = mock<GeckoEngineSession>()
        val geckoSession = mock<GeckoSession>()
        val geckoView = mock<NestedGeckoView>()

        whenever(engineSession.geckoSession).thenReturn(geckoSession)
        engineView.geckoView = geckoView

        engineView.render(engineSession)
        verify(geckoView, times(1)).setSession(geckoSession)

        whenever(geckoView.session).thenReturn(geckoSession)
        engineView.render(engineSession)
        verify(geckoView, times(1)).setSession(geckoSession)
    }

    @Test
    fun captureThumbnail() =
        assertCaptureForwardsGeckoResult(
            geckoViewCapture = { capturePixels() },
            engineViewCapture = { captureThumbnail(it) },
        )

    @Test
    fun captureFullPage() =
        assertCaptureForwardsGeckoResult(
            geckoViewCapture = { captureFullPage() },
            engineViewCapture = { captureFullPage(it) },
        )

    private fun assertCaptureForwardsGeckoResult(
        geckoViewCapture: NestedGeckoView.() -> GeckoResult<Bitmap>?,
        engineViewCapture: GeckoEngineView.(onFinish: (Bitmap?) -> Unit) -> Unit,
    ) {
        val engineView = GeckoEngineView(context)
        val mockGeckoView = mock<NestedGeckoView>()
        var captured: Bitmap? = null

        var geckoResult = GeckoResult<Bitmap>()
        whenever(mockGeckoView.geckoViewCapture()).thenReturn(geckoResult)
        engineView.geckoView = mockGeckoView

        // Test GeckoResult resolves successfuly
        engineView.engineViewCapture { captured = it }
        geckoResult.complete(mock())
        shadowOf(getMainLooper()).idle()

        assertNotNull(captured)

        geckoResult = GeckoResult()
        whenever(mockGeckoView.geckoViewCapture()).thenReturn(geckoResult)

        // Test GeckoResult resolves in error
        engineView.engineViewCapture { captured = it }
        geckoResult.completeExceptionally(mock())
        shadowOf(getMainLooper()).idle()

        assertNull(captured)
    }

    @Test
    fun `captureFullPage emits attempted and success facts`() {
        val engineView = GeckoEngineView(context)
        val mockGeckoView = mock<NestedGeckoView>()
        val geckoResult = GeckoResult<Bitmap>()
        whenever(mockGeckoView.captureFullPage()).thenReturn(geckoResult)
        engineView.geckoView = mockGeckoView

        CollectionProcessor.withFactCollection { facts ->
            engineView.captureFullPage {}
            geckoResult.complete(mock())
            shadowOf(getMainLooper()).idle()

            assertEquals(2, facts.size)
            assertEquals(Component.BROWSER_ENGINE_GECKO, facts[0].component)
            assertEquals(GeckoEngineViewFacts.Items.CAPTURE_FULL_PAGE_ATTEMPTED, facts[0].item)
            assertEquals(GeckoEngineViewFacts.Items.CAPTURE_FULL_PAGE_RESULT, facts[1].item)
            assertEquals(GeckoEngineViewFacts.CaptureFullPageResults.SUCCEEDED, facts[1].value)
        }
    }

    @Test
    fun `captureFullPage emits failure fact when the capture is rejected`() {
        val engineView = GeckoEngineView(context)
        val mockGeckoView = mock<NestedGeckoView>()
        val geckoResult = GeckoResult<Bitmap>()
        whenever(mockGeckoView.captureFullPage()).thenReturn(geckoResult)
        engineView.geckoView = mockGeckoView

        CollectionProcessor.withFactCollection { facts ->
            engineView.captureFullPage {}
            geckoResult.completeExceptionally(mock())
            shadowOf(getMainLooper()).idle()

            assertEquals(2, facts.size)
            assertEquals(GeckoEngineViewFacts.Items.CAPTURE_FULL_PAGE_ATTEMPTED, facts[0].item)
            assertEquals(GeckoEngineViewFacts.Items.CAPTURE_FULL_PAGE_RESULT, facts[1].item)
            assertEquals(GeckoEngineViewFacts.CaptureFullPageResults.FAILED, facts[1].value)
        }
    }

    @Test
    fun `clearSelection is forwarded to BasicSelectionAction instance`() {
        val engineView = GeckoEngineView(context)
        engineView.geckoView = mock()
        engineView.currentSelection = mock()

        engineView.clearSelection()

        verify(engineView.currentSelection)?.clearSelection()
    }

    @Test
    fun `setColorScheme uses preferred color scheme to set correct cover color`() {
        val engineView = GeckoEngineView(context)

        engineView.geckoView = mock()

        var preferredColorScheme: PreferredColorScheme = PreferredColorScheme.Light

        engineView.setColorScheme(preferredColorScheme)

        verify(engineView.geckoView)?.coverUntilFirstPaint(Color.WHITE)

        preferredColorScheme = PreferredColorScheme.Dark
        engineView.setColorScheme(preferredColorScheme)
        verify(engineView.geckoView)?.coverUntilFirstPaint(DARK_COVER)
    }

    @Test
    fun `setVerticalClipping is forwarded to GeckoView instance`() {
        val engineView = GeckoEngineView(context)
        engineView.geckoView = mock()

        engineView.setVerticalClipping(-42)

        verify(engineView.geckoView).setVerticalClipping(-42)
    }

    @Test
    fun `setDynamicToolbarMaxHeight is forwarded to GeckoView instance`() {
        val engineView = GeckoEngineView(context)
        engineView.geckoView = mock()

        engineView.setDynamicToolbarMaxHeight(42)

        verify(engineView.geckoView).setDynamicToolbarMaxHeight(42)
    }

    @Test
    fun `release method releases session from GeckoView`() {
        val engineView = GeckoEngineView(context)
        val engineSession = mock<GeckoEngineSession>()
        val geckoSession = mock<GeckoSession>()
        val geckoView = mock<NestedGeckoView>()

        whenever(engineSession.geckoSession).thenReturn(geckoSession)
        engineView.geckoView = geckoView

        engineView.render(engineSession)

        verify(geckoView, never()).releaseSession()

        engineView.release()

        verify(geckoView).releaseSession()
    }

    @Test
    fun `after rendering currentSelection should be a GeckoSelectionActionDelegate`() {
        val engineView =
            GeckoEngineView(context).apply {
                selectionActionDelegate = mock()
            }
        val engineSession = mock<GeckoEngineSession>()
        val geckoSession = mock<GeckoSession>()
        val geckoView = mock<NestedGeckoView>()

        whenever(engineSession.geckoSession).thenReturn(geckoSession)
        engineView.geckoView = geckoView

        engineView.render(engineSession)

        assertIs<GeckoSelectionActionDelegate>(engineView.currentSelection)
    }

    @Test
    fun `will attach and detach selection action delegate when rendering and releasing`() {
        val delegate: SelectionActionDelegate = mock()

        val engineView =
            GeckoEngineView(context).apply {
                selectionActionDelegate = delegate
            }
        val engineSession = mock<GeckoEngineSession>()
        val geckoSession = mock<GeckoSession>()
        val geckoView = mock<NestedGeckoView>()

        whenever(engineSession.geckoSession).thenReturn(geckoSession)
        engineView.geckoView = geckoView

        engineView.render(engineSession)

        val captor = argumentCaptor<GeckoSession.SelectionActionDelegate>()
        verify(geckoSession).selectionActionDelegate = captor.capture()

        val capturedDelegate = captor.value
        assertIs<GeckoSelectionActionDelegate>(capturedDelegate)

        assertEquals(delegate, capturedDelegate.customDelegate)

        verify(geckoSession, never()).selectionActionDelegate = null

        engineView.release()

        verify(geckoSession).selectionActionDelegate = null
    }

    @Test
    fun `will attach and detach selection action delegate when rendering new session`() {
        val delegate: SelectionActionDelegate = mock()

        val engineView =
            GeckoEngineView(context).apply {
                selectionActionDelegate = delegate
            }
        val engineSession = mock<GeckoEngineSession>()
        val geckoSession = mock<GeckoSession>()
        val geckoView = mock<NestedGeckoView>()

        whenever(engineSession.geckoSession).thenReturn(geckoSession)
        engineView.geckoView = geckoView

        engineView.render(engineSession)

        val captor = argumentCaptor<GeckoSession.SelectionActionDelegate>()
        verify(geckoSession).selectionActionDelegate = captor.capture()

        val capturedDelegate = captor.value
        assertIs<GeckoSelectionActionDelegate>(capturedDelegate)

        assertEquals(delegate, capturedDelegate.customDelegate)

        verify(geckoSession, never()).selectionActionDelegate = null

        whenever(geckoView.session).thenReturn(geckoSession)

        engineView.render(
            mock<GeckoEngineSession>().apply {
                whenever(this.geckoSession).thenReturn(mock())
            }
        )

        verify(geckoSession).selectionActionDelegate = null
    }

    fun `WHEN rendering a new session THEN start observing scroll events`() {
        val listener: GeckoVerticalScrollListener = mock()
        val engineView =
            GeckoEngineView(context).apply {
                verticalScrollListener = listener
            }
        val geckoSession: GeckoSession = mock()
        val engineSession = mock<GeckoEngineSession>()
        doReturn(geckoSession).`when`(engineSession).geckoSession

        engineView.render(engineSession)

        verify(listener).observe(geckoSession)
    }

    fun `WHEN releasing a session THEN stop observing scroll events`() {
        val listener: GeckoVerticalScrollListener = mock()
        val engineView =
            GeckoEngineView(context).apply {
                verticalScrollListener = listener
            }

        engineView.release()

        verify(listener).release()
    }

    @Test
    fun `setVisibility is propagated to gecko view`() {
        val engineView = GeckoEngineView(context)
        engineView.geckoView = mock()

        engineView.visibility = View.GONE
        verify(engineView.geckoView)?.visibility = View.GONE
    }

    @Test
    fun `canClearSelection should return false for null selection, null and empty selection text`() {
        val engineView = GeckoEngineView(context)
        engineView.geckoView = mock()
        engineView.currentSelection = mock()

        // null selection returns false
        whenever(engineView.currentSelection?.selection).thenReturn(null)
        assertFalse(engineView.canClearSelection())

        // selection with null text returns false
        val selectionWthNullText: GeckoSession.SelectionActionDelegate.Selection = mock()
        whenever(engineView.currentSelection?.selection).thenReturn(selectionWthNullText)
        assertFalse(engineView.canClearSelection())

        // selection with empty text returns false
        val selectionWthEmptyText: GeckoSession.SelectionActionDelegate.Selection = mockSelection("")
        whenever(engineView.currentSelection?.selection).thenReturn(selectionWthEmptyText)
        assertFalse(engineView.canClearSelection())
    }

    @Test
    fun `GIVEN a GeckoView WHEN EngineView returns the InputResultDetail THEN the value from the GeckoView is used`() {
        val engineView = GeckoEngineView(context)
        val geckoview = engineView.geckoView

        assertSame(geckoview.inputResultDetail, engineView.getInputResultDetail())
    }

    private fun mockSelection(text: String): GeckoSession.SelectionActionDelegate.Selection {
        val selection: GeckoSession.SelectionActionDelegate.Selection = mock()
        ReflectionUtils.setField(selection, "text", text)
        return selection
    }
}
