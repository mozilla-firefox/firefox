/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.node.RootForTest
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getAllSemanticsNodes
import androidx.compose.ui.semantics.getOrNull

/**
 * Reads and moves the accessibility focus between the player's controls, which Compose has no API for.
 *
 * Controls are found by their [PlayerControl.testTag], which needs `testTagsAsResourceId` set on an ancestor.
 *
 * @param view The Compose view that hosts the player.
 */
internal class AccessibilityFocusMover(private val view: View) {

    /** Returns the player control that has the accessibility focus, or `null` if none has. */
    internal fun focusedControl(): PlayerControl? =
        view.accessibilityNodeProvider
            ?.findFocus(AccessibilityNodeInfo.FOCUS_ACCESSIBILITY)
            ?.viewIdResourceName
            .let(PlayerControl::fromTestTag)

    /** Gives the accessibility focus to [control]. Returns whether it was moved. */
    internal fun moveFocusTo(control: PlayerControl): Boolean {
        if (focusedControl() != null) return false
        val id =
            (view as? RootForTest)
                ?.semanticsOwner
                ?.getAllSemanticsNodes(mergingEnabled = true)
                ?.firstOrNull { it.config.getOrNull(SemanticsProperties.TestTag) == control.testTag }
                ?.id ?: return false
        return view.accessibilityNodeProvider?.performAction(
            id,
            AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS,
            null,
        ) ?: false
    }
}

@Composable
internal fun AccessibilityFocusMover.MoveFocusWhenSettled(isSettled: Boolean, control: PlayerControl?) {
    LaunchedEffect(isSettled, control) {
        if (isSettled && control != null) {
            moveFocusTo(control)
        }
    }
}
