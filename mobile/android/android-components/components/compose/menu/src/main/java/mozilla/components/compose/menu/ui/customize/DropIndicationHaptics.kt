/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import kotlinx.coroutines.flow.filterNotNull
import mozilla.components.compose.draggable.ListInteractionState

/**
 * Lets the user feel it whenever the line, or the highlighted area, showing where the group or item dragged with
 * [interactionState] is dropped appears or moves to another place.
 *
 * @param interactionState State of dragging the items of the list showing the groups or items.
 * @param items The groups or items as last saved.
 * @param dropTarget Where the dragged one is moved to if the user lets go of it, if anywhere.
 */
@Composable
internal fun DropIndicationHaptics(
    interactionState: ListInteractionState,
    items: HideableListItems,
    dropTarget: State<CustomizeMenuDropTarget?>,
) {
    val hapticFeedback = LocalHapticFeedback.current
    LaunchedEffect(interactionState, items, dropTarget) {
        // Different drop targets can show the line at the same place, which is felt only once.
        snapshotFlow {
            val draggedKey = interactionState.draggedItem.key ?: return@snapshotFlow null
            dropTarget.value?.let { items.drop(draggedKey, it) }
        }
            .filterNotNull()
            .collect { hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress) }
    }
}
