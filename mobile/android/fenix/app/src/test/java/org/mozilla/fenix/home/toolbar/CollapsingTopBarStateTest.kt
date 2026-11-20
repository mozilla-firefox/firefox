/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.runtime.MonotonicFrameClock
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

class CollapsingTopBarStateTest {
    private lateinit var state: CollapsingTopBarState

    private val frameClock =
        object : MonotonicFrameClock {
            private var frameTimeNanos = 0L

            override suspend fun <R> withFrameNanos(onFrame: (frameTimeNanos: Long) -> R): R {
                delay(FRAME_DURATION_MS)
                frameTimeNanos += FRAME_DURATION_MS * 1_000_000
                return onFrame(frameTimeNanos)
            }
        }

    @Before
    fun setup() {
        state = collapsingTopBarState()
    }

    @Test
    fun `GIVEN the top bar is expanded WHEN scrolling down THEN the top bar collapses and consumes the scroll`() {
        val consumed = preScroll(-40f)

        assertEquals(Offset(0f, -40f), consumed)
        assertEquals(-40f, state.offset)
        assertEquals(0.6f, state.visibleFraction)
    }

    @Test
    fun `GIVEN the top bar is almost collapsed WHEN scrolling down past its height THEN only the remaining height is consumed`() {
        preScroll(-80f)

        val consumed = preScroll(-50f)

        assertEquals(Offset(0f, -20f), consumed)
        assertEquals(-100f, state.offset)
        assertEquals(0f, state.visibleFraction)
    }

    @Test
    fun `GIVEN the top bar is collapsed WHEN scrolling up THEN the top bar expands before the content scrolls`() {
        preScroll(-100f)

        val consumed = preScroll(150f)

        assertEquals(Offset(0f, 100f), consumed)
        assertEquals(0f, state.offset)
        assertEquals(1f, state.visibleFraction)
    }

    @Test
    fun `GIVEN the content has not scrolled to the collapse point WHEN scrolling down short of it THEN the top bar stays expanded`() {
        state = collapsingTopBarState(distanceBeforeCollapse = 60f)

        val consumed = preScroll(-40f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the content has not scrolled to the collapse point WHEN scrolling down past it THEN the top bar only collapses by the excess`() {
        state = collapsingTopBarState(distanceBeforeCollapse = 30f)

        val consumed = preScroll(-50f)

        assertEquals(Offset(0f, -20f), consumed)
        assertEquals(-20f, state.offset)
    }

    @Test
    fun `GIVEN the content has not scrolled to the collapse point WHEN scrolling up THEN the collapsed top bar expands`() {
        var distanceBeforeCollapse = 0f
        state = collapsingTopBarState(distanceBeforeCollapse = { distanceBeforeCollapse })
        preScroll(-100f)
        distanceBeforeCollapse = 60f

        val consumed = preScroll(40f)

        assertEquals(Offset(0f, 40f), consumed)
        assertEquals(-60f, state.offset)
    }

    @Test
    fun `GIVEN the top bar is not collapsible WHEN scrolling down THEN nothing is consumed`() {
        state.isEnabled = false

        val consumed = preScroll(-40f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the top bar is collapsed WHEN expanding it THEN it is fully visible`() {
        preScroll(-100f)

        state.expand()

        assertEquals(0f, state.offset)
        assertEquals(1f, state.visibleFraction)
    }

    @Test
    fun `GIVEN the top bar is mostly collapsed WHEN settling THEN it fully collapses`() =
        runTest(frameClock) {
            preScroll(-60f)

            state.settle()

            assertEquals(-100f, state.offset)
        }

    @Test
    fun `GIVEN the top bar is mostly expanded WHEN settling THEN it fully expands`() =
        runTest(frameClock) {
            preScroll(-40f)

            state.settle()

            assertEquals(0f, state.offset)
        }

    @Test
    fun `GIVEN the top bar is not collapsible WHEN settling THEN it doesn't move`() =
        runTest(frameClock) {
            preScroll(-60f)
            state.isEnabled = false

            state.settle()

            assertEquals(-60f, state.offset)
        }

    @Test
    fun `GIVEN the top bar is settling WHEN expanding it THEN it stays fully expanded`() =
        runTest(frameClock) {
            preScroll(-60f)
            val settle = launch { state.settle() }
            delay(FRAME_DURATION_MS * 3)

            state.expand()
            settle.join()

            assertEquals(0f, state.offset)
        }

    @Test
    fun `GIVEN the top bar is settling WHEN scrolling horizontally THEN it keeps settling`() =
        runTest(frameClock) {
            preScroll(-60f)
            val settle = launch { state.settle() }
            delay(FRAME_DURATION_MS * 3)

            val consumed = state.nestedScrollConnection.onPreScroll(Offset(30f, 0f), NestedScrollSource.UserInput)
            settle.join()

            assertEquals(Offset.Zero, consumed)
            assertEquals(-100f, state.offset)
        }

    @Test
    fun `GIVEN the top bar is settling WHEN the content is scrolled THEN the settling stops`() =
        runTest(frameClock) {
            preScroll(-60f)
            val settle = launch { state.settle() }
            delay(FRAME_DURATION_MS * 3)

            preScroll(10f)
            val offsetAfterScroll = state.offset
            settle.join()

            assertEquals(offsetAfterScroll, state.offset)
        }

    @Test
    fun `GIVEN there is no search bar position WHEN scrolling down THEN the top bar stays expanded`() {
        state = collapsingTopBarState(distanceBeforeCollapse = Float.POSITIVE_INFINITY)

        val consumed = preScroll(-40f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the content cannot scroll WHEN scrolling down THEN the top bar stays expanded`() {
        state = collapsingTopBarState(remainingScroll = 0f)

        val consumed = preScroll(-40f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the content can scroll less than the top bar height WHEN scrolling down THEN the top bar stays expanded`() {
        state = collapsingTopBarState(remainingScroll = 99f)

        val consumed = preScroll(-50f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the content can scroll the top bar height WHEN scrolling down THEN the top bar collapses`() {
        state = collapsingTopBarState(remainingScroll = 100f)

        val consumed = preScroll(-50f)

        assertEquals(Offset(0f, -50f), consumed)
        assertEquals(-50f, state.offset)
    }

    @Test
    fun `GIVEN the content scrolls before the top bar collapses WHEN that leaves too little scroll THEN the top bar stays expanded`() {
        state = collapsingTopBarState(distanceBeforeCollapse = 20f, remainingScroll = 110f)

        val consumed = preScroll(-60f)

        assertEquals(Offset.Zero, consumed)
        assertEquals(0f, state.offset)
    }

    @Test
    fun `GIVEN the content scrolls before the top bar collapses WHEN that leaves enough scroll THEN the top bar collapses by the rest`() {
        state = collapsingTopBarState(distanceBeforeCollapse = 20f, remainingScroll = 120f)

        val consumed = preScroll(-60f)

        assertEquals(Offset(0f, -40f), consumed)
        assertEquals(-40f, state.offset)
    }

    @Test
    fun `GIVEN the top bar is mostly collapsed and cannot fully collapse WHEN settling THEN it fully expands`() =
        runTest(frameClock) {
            var remainingScroll = 100f
            state = collapsingTopBarState(distanceBeforeCollapse = { 0f }, remainingScroll = { remainingScroll })
            preScroll(-60f)
            remainingScroll = 50f

            state.settle()

            assertEquals(0f, state.offset)
        }

    private fun collapsingTopBarState(
        distanceBeforeCollapse: Float = 0f,
        remainingScroll: Float = Float.POSITIVE_INFINITY,
    ) =
        collapsingTopBarState(
            distanceBeforeCollapse = { distanceBeforeCollapse },
            remainingScroll = { remainingScroll },
        )

    private fun collapsingTopBarState(
        distanceBeforeCollapse: () -> Float,
        remainingScroll: () -> Float = { Float.POSITIVE_INFINITY },
    ) =
        CollapsingTopBarState(distanceBeforeCollapse, remainingScroll).apply {
            height = 100
            isEnabled = true
        }

    private fun preScroll(deltaY: Float) =
        state.nestedScrollConnection.onPreScroll(Offset(0f, deltaY), NestedScrollSource.UserInput)

    private companion object {
        const val FRAME_DURATION_MS = 16L
    }
}
