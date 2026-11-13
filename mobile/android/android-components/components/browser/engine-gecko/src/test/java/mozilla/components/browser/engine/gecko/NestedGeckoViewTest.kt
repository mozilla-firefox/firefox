/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.app.Activity
import android.content.Context
import android.os.Looper.getMainLooper
import android.view.MotionEvent
import android.view.MotionEvent.ACTION_CANCEL
import android.view.MotionEvent.ACTION_DOWN
import android.view.MotionEvent.ACTION_MOVE
import android.view.MotionEvent.ACTION_UP
import android.widget.FrameLayout
import androidx.core.view.NestedScrollingChildHelper
import androidx.core.view.ViewCompat.SCROLL_AXIS_VERTICAL
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.concept.engine.INPUT_HANDLING_UNKNOWN
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.components.support.test.mockMotionEvent
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyInt
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.PanZoomController.INPUT_RESULT_HANDLED
import org.mozilla.geckoview.PanZoomController.INPUT_RESULT_HANDLED_CONTENT
import org.mozilla.geckoview.PanZoomController.InputResultDetail
import org.mozilla.geckoview.PanZoomController.OVERSCROLL_FLAG_HORIZONTAL
import org.mozilla.geckoview.PanZoomController.OVERSCROLL_FLAG_NONE
import org.mozilla.geckoview.PanZoomController.OVERSCROLL_FLAG_VERTICAL
import org.mozilla.geckoview.PanZoomController.SCROLLABLE_FLAG_BOTTOM
import org.robolectric.Robolectric.buildActivity
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class NestedGeckoViewTest {

    private val context: Context
        get() = buildActivity(Activity::class.java).get()

    @Test
    fun `NestedGeckoView must delegate NestedScrollingChild implementation to childHelper`() {
        val nestedWebView = NestedGeckoView(context)
        val mockChildHelper: NestedScrollingChildHelper = mock()
        nestedWebView.childHelper = mockChildHelper

        doReturn(true).`when`(mockChildHelper).isNestedScrollingEnabled
        doReturn(true).`when`(mockChildHelper).hasNestedScrollingParent()

        nestedWebView.isNestedScrollingEnabled = true
        verify(mockChildHelper).isNestedScrollingEnabled = true

        assertTrue(nestedWebView.isNestedScrollingEnabled)
        verify(mockChildHelper).isNestedScrollingEnabled

        nestedWebView.startNestedScroll(1)
        verify(mockChildHelper).startNestedScroll(1)

        nestedWebView.stopNestedScroll()
        verify(mockChildHelper).stopNestedScroll()

        assertTrue(nestedWebView.hasNestedScrollingParent())
        verify(mockChildHelper).hasNestedScrollingParent()

        nestedWebView.dispatchNestedScroll(0, 0, 0, 0, null)
        verify(mockChildHelper).dispatchNestedScroll(0, 0, 0, 0, null)

        nestedWebView.dispatchNestedPreScroll(0, 0, null, null)
        verify(mockChildHelper).dispatchNestedPreScroll(0, 0, null, null)

        nestedWebView.dispatchNestedFling(0f, 0f, true)
        verify(mockChildHelper).dispatchNestedFling(0f, 0f, true)

        nestedWebView.dispatchNestedPreFling(0f, 0f)
        verify(mockChildHelper).dispatchNestedPreFling(0f, 0f)
    }

    @Test
    fun `verify onTouchEvent when ACTION_DOWN`() {
        val nestedWebView = spy(NestedGeckoView(context))
        val mockChildHelper: NestedScrollingChildHelper = mock()
        val downEvent = mockMotionEvent(ACTION_DOWN)
        val eventCaptor = argumentCaptor<MotionEvent>()
        nestedWebView.childHelper = mockChildHelper

        nestedWebView.onTouchEvent(downEvent)
        shadowOf(getMainLooper()).idle()

        // We pass a deep copy to `updateInputResult`.
        // Can't easily check for equality, `eventTime` should be good enough.
        verify(nestedWebView).updateInputResult(eventCaptor.capture())
        assertEquals(downEvent.eventTime, eventCaptor.value.eventTime)
        verify(mockChildHelper).startNestedScroll(SCROLL_AXIS_VERTICAL)
        verify(nestedWebView, times(0)).callSuperOnTouchEvent(any())
    }

    @Test
    fun `verify onTouchEvent when ACTION_MOVE`() {
        val nestedWebView = spy(NestedGeckoView(context))
        val mockChildHelper: NestedScrollingChildHelper = mock()
        nestedWebView.childHelper = mockChildHelper
        nestedWebView.inputResultDetail = nestedWebView.inputResultDetail.copy(INPUT_RESULT_HANDLED)
        doReturn(true).`when`(nestedWebView).callSuperOnTouchEvent(any())

        doReturn(true)
            .`when`(mockChildHelper)
            .dispatchNestedPreScroll(
                anyInt(),
                anyInt(),
                any(),
                any(),
            )

        nestedWebView.scrollOffset[0] = 1
        nestedWebView.scrollOffset[1] = 2

        nestedWebView.onTouchEvent(mockMotionEvent(ACTION_DOWN, y = 0f))
        nestedWebView.onTouchEvent(mockMotionEvent(ACTION_MOVE, y = 5f))
        assertEquals(2, nestedWebView.nestedOffsetY)
        assertEquals(3, nestedWebView.lastY)

        doReturn(true)
            .`when`(mockChildHelper)
            .dispatchNestedScroll(
                anyInt(),
                anyInt(),
                anyInt(),
                anyInt(),
                any(),
            )

        nestedWebView.onTouchEvent(mockMotionEvent(ACTION_MOVE, y = 10f))
        assertEquals(6, nestedWebView.nestedOffsetY)
        assertEquals(6, nestedWebView.lastY)

        // onTouchEventForResult should be also called for ACTION_MOVE
        verify(nestedWebView, times(1)).updateInputResult(any())
    }

    @Test
    fun `verify onTouchEvent when ACTION_UP or ACTION_CANCEL`() {
        val nestedWebView = spy(NestedGeckoView(context))
        val initialInputResultDetail = nestedWebView.inputResultDetail.copy(INPUT_RESULT_HANDLED)
        nestedWebView.inputResultDetail = initialInputResultDetail
        val mockChildHelper: NestedScrollingChildHelper = mock()
        nestedWebView.childHelper = mockChildHelper
        doReturn(true).`when`(nestedWebView).callSuperOnTouchEvent(any())

        assertEquals(INPUT_RESULT_HANDLED, nestedWebView.inputResultDetail.inputResult)
        nestedWebView.onTouchEvent(mockMotionEvent(ACTION_UP))
        verify(mockChildHelper).stopNestedScroll()
        // ACTION_UP should reset nestedWebView.inputResultDetail.
        assertNotEquals(initialInputResultDetail, nestedWebView.inputResultDetail)
        assertEquals(INPUT_HANDLING_UNKNOWN, nestedWebView.inputResultDetail.inputResult)

        nestedWebView.inputResultDetail = initialInputResultDetail
        assertEquals(INPUT_RESULT_HANDLED, nestedWebView.inputResultDetail.inputResult)
        nestedWebView.onTouchEvent(mockMotionEvent(ACTION_CANCEL))
        verify(mockChildHelper, times(2)).stopNestedScroll()
        // ACTION_CANCEL should reset nestedWebView.inputResultDetail.
        assertNotEquals(initialInputResultDetail, nestedWebView.inputResultDetail)
        assertEquals(INPUT_HANDLING_UNKNOWN, nestedWebView.inputResultDetail.inputResult)

        // onTouchEventForResult should never be called for ACTION_UP or ACTION_CANCEL
        verify(nestedWebView, times(0)).updateInputResult(any())
    }

    @Test
    fun `requestDisallowInterceptTouchEvent doesn't pass touch events to parents until engineView responds`() {
        var viewParentInterceptCounter = 0
        val result: GeckoResult<InputResultDetail> = GeckoResult()
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent) = result
            }
        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Down action enables requestDisallowInterceptTouchEvent (and starts a gesture).
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 0f))

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)

        // Move action assert that onInterceptTouchEvent calls continue to be ignored.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 1f))

        assertEquals(1, viewParentInterceptCounter)

        // Simulate a `handled` response from the APZ GeckoEngineView API.
        val inputResultMock =
            mock<InputResultDetail>().apply {
                whenever(handledResult()).thenReturn(INPUT_RESULT_HANDLED)
                whenever(scrollableDirections()).thenReturn(SCROLLABLE_FLAG_BOTTOM)
                whenever(overscrollDirections()).thenReturn(OVERSCROLL_FLAG_VERTICAL)
            }
        result.complete(inputResultMock)
        shadowOf(getMainLooper()).idle()

        // Move action no longer ignores onInterceptTouchEvent calls.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 2f))

        assertEquals(2, viewParentInterceptCounter)

        // Complete the gesture by finishing with an up action.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_UP))

        assertEquals(3, viewParentInterceptCounter)
    }

    @Test
    fun `touch events are never intercepted once after scrolled down`() {
        var viewParentInterceptCounter = 0
        val result: GeckoResult<InputResultDetail> = GeckoResult()
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent): GeckoResult<InputResultDetail> =
                    result
            }

        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Down action enables requestDisallowInterceptTouchEvent (and starts a gesture).
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 4f))

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)

        // Move action to scroll down assert that onInterceptTouchEvent calls continue to be ignored.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 3f))

        assertEquals(1, viewParentInterceptCounter)

        // Simulate a `handled` response from the APZ GeckoEngineView API.
        val inputResultMock =
            mock<InputResultDetail>().apply {
                whenever(handledResult()).thenReturn(INPUT_RESULT_HANDLED)
                whenever(scrollableDirections()).thenReturn(SCROLLABLE_FLAG_BOTTOM)
                whenever(overscrollDirections()).thenReturn(OVERSCROLL_FLAG_VERTICAL or OVERSCROLL_FLAG_HORIZONTAL)
            }
        result.complete(inputResultMock)
        shadowOf(getMainLooper()).idle()

        // Move action to scroll down further that onInterceptTouchEvent calls continue to be ignored.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 2f))

        assertEquals(1, viewParentInterceptCounter)

        // Complete the gesture by finishing with an up action.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_UP))

        assertEquals(1, viewParentInterceptCounter)
    }

    @Suppress("UNUSED_CHANGED_VALUE")
    @Test
    fun `GIVEN page is not at its top touch events WHEN user pulls page up THEN parent doesn't intercept the gesture`() {
        var viewParentInterceptCounter = 0
        val geckoResults = mutableListOf<GeckoResult<InputResultDetail>>()
        var resultCurrentIndex = 0
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent): GeckoResult<InputResultDetail> {
                    return GeckoResult<InputResultDetail>().also(geckoResults::add)
                }
            }

        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Down action enables requestDisallowInterceptTouchEvent (and starts a gesture).
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 1f))

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)

        // Move action to scroll down assert that onInterceptTouchEvent calls continue to be ignored.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 2f))

        // Simulate a `handled` response from the APZ GeckoEngineView API.
        val inputResultMock =
            mock<InputResultDetail>().apply {
                whenever(handledResult()).thenReturn(INPUT_RESULT_HANDLED)
                whenever(scrollableDirections()).thenReturn(SCROLLABLE_FLAG_BOTTOM)
                whenever(overscrollDirections()).thenReturn(OVERSCROLL_FLAG_NONE)
            }
        geckoResults[resultCurrentIndex++].complete(inputResultMock)
        shadowOf(getMainLooper()).idle()

        assertEquals(1, viewParentInterceptCounter)

        // Move action to scroll down further that onInterceptTouchEvent calls continue to be ignored.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 3f))

        assertEquals(1, viewParentInterceptCounter)

        // Complete the gesture by finishing with an up action.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_UP))

        assertEquals(1, viewParentInterceptCounter)
    }

    @Suppress("UNUSED_CHANGED_VALUE")
    @Test
    fun `verify parent don't intercept touch if the delayed result is INPUT_RESULT_HANDLED_CONTENT`() {
        var viewParentInterceptCounter = 0
        val result: GeckoResult<InputResultDetail> = GeckoResult()
        var disallowInterceptTouchEventValue = false
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent): GeckoResult<InputResultDetail> =
                    result
            }

        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }

                    override fun requestDisallowInterceptTouchEvent(disallowIntercept: Boolean) {
                        disallowInterceptTouchEventValue = disallowIntercept
                        super.requestDisallowInterceptTouchEvent(disallowIntercept)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Down action enables requestDisallowInterceptTouchEvent (and starts a gesture).
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 2f))

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 2f))

        // Make sure the parent couldn't intercept the touch event
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 3f))
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 4f))

        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls keep being ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 4f))

        // Now `INPUT_RESULT_HANDLED_CONTENT` got received.
        result.complete(generateOverscrollInputResultMock(INPUT_RESULT_HANDLED_CONTENT))
        shadowOf(getMainLooper()).idle()

        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll downwards, assert that onInterceptTouchEvent calls keep being ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 1f))

        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls keep being ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 4f))

        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll downwards, assert that onInterceptTouchEvent calls keep being ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 5f))

        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Complete the gesture by finishing with an up action.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_UP))
        assertEquals(1, viewParentInterceptCounter)
    }

    @Suppress("UNUSED_CHANGED_VALUE")
    @Test
    fun `verify parent don't intercept touch when gesture started with an downward scroll on a page2`() {
        var viewParentInterceptCounter = 0
        val geckoResults = mutableListOf<GeckoResult<InputResultDetail>>()
        var resultCurrentIndex = 0
        var disallowInterceptTouchEventValue = false
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent): GeckoResult<InputResultDetail> {
                    return GeckoResult<InputResultDetail>().also(geckoResults::add)
                }
            }

        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }

                    override fun requestDisallowInterceptTouchEvent(disallowIntercept: Boolean) {
                        disallowInterceptTouchEventValue = disallowIntercept
                        super.requestDisallowInterceptTouchEvent(disallowIntercept)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Simulate a `handled` response from the APZ GeckoEngineView API.
        val inputResultMock =
            mock<InputResultDetail>().apply {
                whenever(handledResult()).thenReturn(INPUT_RESULT_HANDLED)
                whenever(scrollableDirections()).thenReturn(SCROLLABLE_FLAG_BOTTOM)
                whenever(overscrollDirections()).thenReturn(OVERSCROLL_FLAG_VERTICAL or OVERSCROLL_FLAG_HORIZONTAL)
            }

        // Down action enables requestDisallowInterceptTouchEvent (and starts a gesture).
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 2f))

        geckoResults[resultCurrentIndex++].complete(inputResultMock)

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 2f))

        // Make sure the size of results hasn't increased, meaning we don't pass the event to GeckoView to process
        assertEquals(1, geckoResults.size)

        // Make sure the parent couldn't intercept the touch event
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 3f))
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 4f))
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll downwards, assert that onInterceptTouchEvent calls don't reach the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 1f))
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 4f))

        assertEquals(1, resultCurrentIndex)
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Move action to scroll upwards, assert that onInterceptTouchEvent calls are still ignored by the parent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_MOVE, y = 5f))

        assertEquals(1, resultCurrentIndex)
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Complete the gesture by finishing with an up action.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_UP))
        assertEquals(1, viewParentInterceptCounter)
    }

    @Suppress("UNUSED_CHANGED_VALUE")
    @Test
    fun `verify parent does intercept touch if the result for ACTION_DOWN is INPUT_RESULT_HANDLED`() {
        var viewParentInterceptCounter = 0
        val result: GeckoResult<InputResultDetail> = GeckoResult()
        var disallowInterceptTouchEventValue = false
        val nestedWebView =
            object : NestedGeckoView(context) {
                init {
                    // We need to make the view a non-zero size so that the touch events hit it.
                    left = 0
                    top = 0
                    right = 5
                    bottom = 5
                }

                override fun superOnTouchEventForDetailResult(event: MotionEvent): GeckoResult<InputResultDetail> =
                    result
            }

        val viewParent =
            object : FrameLayout(context) {
                    override fun onInterceptTouchEvent(ev: MotionEvent?): Boolean {
                        viewParentInterceptCounter++
                        return super.onInterceptTouchEvent(ev)
                    }

                    override fun requestDisallowInterceptTouchEvent(disallowIntercept: Boolean) {
                        disallowInterceptTouchEventValue = disallowIntercept
                        super.requestDisallowInterceptTouchEvent(disallowIntercept)
                    }
                }
                .apply {
                    addView(nestedWebView)
                }

        // Down action enables requestDisallowInterceptTouchEvent.
        viewParent.dispatchTouchEvent(mockMotionEvent(ACTION_DOWN, y = 0f))

        // `onInterceptTouchEvent` will be triggered the first time because it's the first pass.
        assertEquals(1, viewParentInterceptCounter)
        assertTrue(disallowInterceptTouchEventValue)

        // Now `INPUT_RESULT_HANDLED_CONTENT` got received.
        result.complete(generateOverscrollInputResultMock(INPUT_RESULT_HANDLED))
        shadowOf(getMainLooper()).idle()

        assertEquals(1, viewParentInterceptCounter)
        assertFalse(disallowInterceptTouchEventValue)
    }

    private fun generateOverscrollInputResultMock(inputResult: Int) =
        mock<InputResultDetail>().apply {
            whenever(handledResult()).thenReturn(inputResult)
            whenever(scrollableDirections()).thenReturn(SCROLLABLE_FLAG_BOTTOM)
            whenever(overscrollDirections()).thenReturn(OVERSCROLL_FLAG_VERTICAL or OVERSCROLL_FLAG_HORIZONTAL)
        }
}
