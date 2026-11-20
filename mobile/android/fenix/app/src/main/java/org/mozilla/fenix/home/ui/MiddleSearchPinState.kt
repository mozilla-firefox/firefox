/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

/**
 * State of the middle search bar pinning to the top of the homepage as the homepage content scrolls.
 *
 * Most of the derived values change on every scroll frame. Only read them in the layout or draw phase, or through
 * `derivedStateOf`, so that scrolling doesn't recompose the homepage on every frame.
 *
 * @property scrollState [ScrollState] of the scrollable homepage content.
 */
@Stable
internal class MiddleSearchPinState(val scrollState: ScrollState) {
    /** Offset of the search bar within the scrollable content, in pixels, or null if it isn't laid out. */
    var offset by mutableStateOf<Int?>(null)
        private set

    /** Height of the search bar, in pixels. */
    var height by mutableIntStateOf(0)
        private set

    /** Distance from the top of the homepage to the search bar, in pixels, negative once it has scrolled past. */
    val distance: Int?
        get() = offset?.let { it - scrollState.value }

    /** Fraction of the search bar that has scrolled into its pinned position, from 0 to 1. */
    val pinProgress: Float
        get() {
            val distance = distance ?: return 0f
            if (height == 0) return 0f
            return ((height - distance).toFloat() / height).coerceIn(0f, 1f)
        }

    /** Offset at which the overlaid search bar is drawn: following the content, then stopping at the top. */
    val overlayOffset: Int
        get() = (distance ?: 0).coerceAtLeast(0)

    /** Distance from the top of the homepage where the top fade starts, i.e. the pinned search bar's bottom edge. */
    val topFadeStart: Float
        get() = height * pinProgress

    /**
     * Returns whether the search bar is close enough to the top fade, of [fadeLengthPx] pixels, that it has to be drawn
     * over the scrollable content to not be faded.
     */
    fun isOverlaid(fadeLengthPx: Float): Boolean = distance?.let { it < height + fadeLengthPx } ?: false

    /** Updates the [offset] and [height] of the search bar laid out in the scrollable content. */
    fun onPlaced(offset: Int, height: Int) {
        this.offset = offset
        this.height = height
    }

    /** Forgets the search bar position, for when it is no longer shown. */
    fun reset() {
        offset = null
    }
}

/** Creates and remembers a [MiddleSearchPinState] for [scrollState]. */
@Composable
internal fun rememberMiddleSearchPinState(scrollState: ScrollState = rememberScrollState()): MiddleSearchPinState =
    remember(scrollState) { MiddleSearchPinState(scrollState) }
