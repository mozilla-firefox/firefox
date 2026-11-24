/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.animation.AnimatedVisibilityScope
import androidx.compose.animation.EnterExitState
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.clearAndSetSemantics

/**
 * Makes a player layout ignore touches and hides it from accessibility services while it fades in or out.
 *
 * @param scope The scope that shows and hides the layout.
 */
internal fun Modifier.inertWhileTransitioning(scope: AnimatedVisibilityScope): Modifier {
    if (scope.isSettled) {
        return this
    }
    return this.clearAndSetSemantics {}
        .pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) {
                    awaitPointerEvent(PointerEventPass.Initial).changes.forEach { it.consume() }
                }
            }
        }
}

/** Whether the layout has finished fading in and is shown as is. */
internal val AnimatedVisibilityScope.isSettled: Boolean
    get() = transition.currentState == EnterExitState.Visible && transition.targetState == EnterExitState.Visible
