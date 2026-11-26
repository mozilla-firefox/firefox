/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.semantics.ScrollAxisRange
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performTouchInput

/**
 * Hold the item at [from] and move it in steps to [distanceFromBottom] above the bottom of the screen, still held, with
 * the clock advancing by frames from then on.
 */
internal fun ComposeTestRule.pickUpAndMoveToBottom(from: Offset, distanceFromBottom: Float) {
    val bottom = onRoot().fetchSemanticsNode().boundsInRoot.bottom
    onRoot().performTouchInput {
        down(from)
        advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
        // Send the delay with a stationary move, an empty batch would drop it.
        move()
    }
    waitForIdle()
    mainClock.autoAdvance = false

    val step = (Offset(from.x, bottom - distanceFromBottom) - from) / 20f
    repeat(20) {
        onRoot().performTouchInput { moveBy(step) }
        mainClock.advanceTimeByFrame()
    }
}

/**
 * Keep holding the item with [moves] tiny moves, each followed by a frame. Each move shifts the finger down by 0.5px,
 * so it drifts past the bottom of the list, use [holdStill] to stay in place.
 */
internal fun ComposeTestRule.holdFor(moves: Int) {
    repeat(moves) {
        onRoot().performTouchInput { moveBy(Offset(0f, 0.5f)) }
        mainClock.advanceTimeByFrame()
    }
}

/** Keep holding the item at one point with [moves] tiny moves back and forth, each followed by a frame. */
internal fun ComposeTestRule.holdStill(moves: Int) {
    repeat(moves) { index ->
        onRoot().performTouchInput { moveBy(Offset(0f, if (index % 2 == 0) 0.5f else -0.5f)) }
        mainClock.advanceTimeByFrame()
    }
}

internal fun ComposeTestRule.scrollRange(): ScrollAxisRange =
    onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.VerticalScrollAxisRange))
        .fetchSemanticsNode()
        .config[SemanticsProperties.VerticalScrollAxisRange]
