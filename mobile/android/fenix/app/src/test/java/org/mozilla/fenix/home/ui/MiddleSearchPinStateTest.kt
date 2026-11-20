/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.ScrollState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MiddleSearchPinStateTest {

    @Test
    fun `GIVEN the search bar is not laid out THEN it has no distance and is not pinned or overlaid`() {
        val state = pinState(scroll = 100)

        assertNull(state.distance)
        assertEquals(0f, state.pinProgress)
        assertEquals(0, state.overlayOffset)
        assertEquals(0f, state.topFadeStart)
        assertFalse(state.isOverlaid(fadeLengthPx = 24f))
    }

    @Test
    fun `GIVEN the search bar is below the top WHEN not scrolled to it THEN it is not pinned and follows the content`() {
        val state = pinState(scroll = 100, offset = 300, height = 50)

        assertEquals(200, state.distance)
        assertEquals(0f, state.pinProgress)
        assertEquals(200, state.overlayOffset)
        assertEquals(0f, state.topFadeStart)
    }

    @Test
    fun `GIVEN the search bar is scrolling into its pinned position THEN the pin progress is partial`() {
        val state = pinState(scroll = 275, offset = 300, height = 50)

        assertEquals(25, state.distance)
        assertEquals(0.5f, state.pinProgress)
        assertEquals(25, state.overlayOffset)
        assertEquals(25f, state.topFadeStart)
    }

    @Test
    fun `GIVEN the search bar has scrolled past the top THEN it is pinned at the top`() {
        val state = pinState(scroll = 400, offset = 300, height = 50)

        assertEquals(-100, state.distance)
        assertEquals(1f, state.pinProgress)
        assertEquals(0, state.overlayOffset)
        assertEquals(50f, state.topFadeStart)
    }

    @Test
    fun `GIVEN a top fade WHEN the search bar gets within its height and the fade of the top THEN it is overlaid`() {
        val state = pinState(scroll = 100, offset = 173, height = 50)

        assertTrue(state.isOverlaid(fadeLengthPx = 24f))
        assertFalse(state.isOverlaid(fadeLengthPx = 0f))
    }

    @Test
    fun `GIVEN the search bar is laid out WHEN resetting THEN its position is forgotten`() {
        val state = pinState(scroll = 400, offset = 300, height = 50)

        state.reset()

        assertNull(state.offset)
        assertEquals(0f, state.pinProgress)
    }

    private fun pinState(scroll: Int, offset: Int? = null, height: Int = 0) =
        MiddleSearchPinState(ScrollState(initial = scroll)).apply {
            if (offset != null) {
                onPlaced(offset = offset, height = height)
            }
        }
}
