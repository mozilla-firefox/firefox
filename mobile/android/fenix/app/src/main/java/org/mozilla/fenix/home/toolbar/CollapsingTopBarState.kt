/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.animation.core.animate
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.layout.layout
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.Velocity
import kotlin.math.roundToInt
import kotlinx.coroutines.Job
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch

/**
 * State of a top bar that scrolls away when the content below it is scrolled down and comes back when it is scrolled
 * up, similar to the dynamic toolbar of the browser.
 *
 * Collapsing the top bar makes the content's viewport taller, which reduces how far the content can scroll by the same
 * amount. The top bar therefore only collapses when the content has enough scroll left for it to fully collapse, so
 * that the content's scroll position doesn't get clamped and the content doesn't jump. When it doesn't, the top bar
 * stays where it is rather than partially collapsing and snapping back open.
 *
 * @param distanceBeforeCollapse Lambda that returns how far, in pixels, the content scrolls down before the top bar
 *   starts collapsing.
 * @param remainingScroll Lambda that returns how far, in pixels, the content can still scroll down, as of its last
 *   layout.
 */
@Stable
internal class CollapsingTopBarState(
    private val distanceBeforeCollapse: () -> Float = { 0f },
    private val remainingScroll: () -> Float = { Float.POSITIVE_INFINITY },
) {
    /** Whether scrolling the content collapses and expands the top bar. */
    var isEnabled by mutableStateOf(false)

    /** Full height of the top bar, in pixels. */
    var height by mutableIntStateOf(0)
        internal set

    /** Vertical offset of the top bar, in pixels, from 0 when fully expanded to -[height] when fully collapsed. */
    var offset by mutableFloatStateOf(0f)
        private set

    /** [offset] of the top bar at its last layout, which the content's last layout is based on. */
    internal var layoutOffset: Float = 0f

    /** Fraction of the top bar that is visible, from 0 when fully collapsed to 1 when fully expanded. */
    val visibleFraction: Float
        get() = if (height == 0) 1f else 1f + offset / height

    /** [NestedScrollConnection] collapsing or expanding the top bar before the content consumes the scroll. */
    val nestedScrollConnection =
        object : NestedScrollConnection {
            override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
                if (!isEnabled || available.y == 0f) return Offset.Zero

                settleJob?.cancel()

                val delta =
                    if (available.y < 0f) {
                        val scroll = -available.y
                        val contentScroll = distanceBeforeCollapse().coerceIn(0f, scroll)
                        val remainingCollapse = height + offset
                        if (collapseRoom() - contentScroll >= remainingCollapse) {
                            -(scroll - contentScroll)
                        } else {
                            0f
                        }
                    } else {
                        available.y
                    }
                val newOffset = (offset + delta).coerceIn(-height.toFloat(), 0f)
                val consumed = newOffset - offset
                offset = newOffset
                return Offset(0f, consumed)
            }

            override suspend fun onPostFling(consumed: Velocity, available: Velocity): Velocity {
                settle()
                return Velocity.Zero
            }
        }

    private var settleJob: Job? = null

    /** Fully expands the top bar, stopping any [settle] animation in progress. */
    fun expand() {
        settleJob?.cancel()
        offset = 0f
    }

    /**
     * Animates a partially visible top bar to whichever of fully expanded or collapsed is closest. The animation stops
     * when the content is scrolled again or the top bar is [expand]ed.
     */
    suspend fun settle() {
        if (!isEnabled || offset == 0f || offset == -height.toFloat()) return

        val canFullyCollapse = height + offset <= collapseRoom()
        val target = if (visibleFraction < SETTLE_THRESHOLD && canFullyCollapse) -height.toFloat() else 0f
        coroutineScope {
            settleJob?.cancel()
            val job = launch { animate(initialValue = offset, targetValue = target) { value, _ -> offset = value } }
            settleJob = job
            job.join()
        }
    }

    // How far the top bar can still collapse without the content running out of scroll. The content's scroll range is
    // only updated on its next layout, so the collapse since the last layout is subtracted from it.
    private fun collapseRoom(): Float = remainingScroll() - (layoutOffset - offset)

    private companion object {
        const val SETTLE_THRESHOLD = 0.5f
    }
}

/**
 * Lays out the top bar so that only its visible part, per [state], takes up space, sliding the rest out of view at the
 * top.
 */
internal fun Modifier.collapsingTopBar(state: CollapsingTopBarState): Modifier =
    clipToBounds()
        .layout { measurable, constraints ->
            val placeable = measurable.measure(constraints)
            state.layoutOffset = state.offset
            val visibleHeight = (placeable.height + state.offset).roundToInt().coerceIn(0, placeable.height)
            layout(placeable.width, visibleHeight) { placeable.place(0, visibleHeight - placeable.height) }
        }
        .onSizeChanged { state.height = it.height }
