/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitVerticalTouchSlopOrCancellation
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.verticalDrag
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ListInteractionState

/**
 * Drag the group with [groupId] once the user long presses where this is applied.
 *
 * @param dragState State of dragging the groups, or `null` to not drag them.
 * @param groupId Id of the group to drag.
 */
internal fun Modifier.dragGroupAfterLongPress(dragState: CustomizeMenuDragState?, groupId: Any): Modifier =
    dragState?.let { dragGroupAfterLongPress(it.interactionState, it.listState, groupId) } ?: this

/**
 * Drag the group with [groupId] as soon as the user moves vertically where this is applied, without a long press.
 *
 * @param dragState State of dragging the groups, or `null` to not drag them.
 * @param groupId Id of the group to drag.
 */
internal fun Modifier.dragGroup(dragState: CustomizeMenuDragState?, groupId: Any): Modifier =
    dragState?.let { dragGroup(it.interactionState, it.listState, groupId) } ?: this

private fun Modifier.dragGroupAfterLongPress(
    interactionState: ListInteractionState,
    listState: LazyListState,
    groupId: Any,
): Modifier =
    this.pointerInput(interactionState, listState, groupId) {
        val gesture = GroupDragGesture(interactionState, listState, groupId)
        detectDragGesturesAfterLongPress(
            onDragStart = { gesture.start(shouldLongPress = true) },
            onDrag = { change, dragAmount ->
                change.consume()
                gesture.drag(dragAmount.y)
            },
            onDragEnd = gesture::end,
            onDragCancel = gesture::cancel,
        )
    }

private fun Modifier.dragGroup(
    interactionState: ListInteractionState,
    listState: LazyListState,
    groupId: Any,
): Modifier =
    this.pointerInput(interactionState, listState, groupId) {
        val gesture = GroupDragGesture(interactionState, listState, groupId)
        awaitEachGesture {
            val down = awaitFirstDown()
            // The touch is left to the list unless the group is dragged.
            val slopChange =
                awaitVerticalTouchSlopOrCancellation(down.id) { change, _ ->
                    if (gesture.start(shouldLongPress = false)) change.consume()
                } ?: return@awaitEachGesture
            try {
                // Everything moved since the finger went down, for the group not to stay behind by the touch slop.
                gesture.drag(slopChange.position.y - down.position.y)
                val isCompleted =
                    verticalDrag(slopChange.id) { change ->
                        gesture.drag(change.positionChange().y)
                        change.consume()
                    }
                if (isCompleted) gesture.end()
            } finally {
                gesture.cancel()
            }
        }
    }

/**
 * Drags the group with [groupId] for one gesture at a time. A gesture drags only if no other group is dragged when it
 * starts, and leaves the drag of another gesture alone, as the state it drives is shared by all groups.
 */
private class GroupDragGesture(
    private val interactionState: ListInteractionState,
    private val listState: LazyListState,
    private val groupId: Any,
) {
    private var isDragging = false

    /** Pick the group up, if no other group is dragged. Returns whether the group is dragged by this gesture. */
    fun start(shouldLongPress: Boolean): Boolean {
        val center = listState.centerOf(groupId)
        if (center != null && interactionState.draggedItem == InteractionState.List.None) {
            interactionState.onTouchSlopPassed(center, shouldLongPress)
            isDragging = interactionState.draggedItem != InteractionState.List.None
        }
        return isDragging
    }

    fun drag(offset: Float) {
        if (isDragging) interactionState.onDrag(offset, false)
    }

    fun end() {
        if (isDragging) interactionState.onDragEnd()
        isDragging = false
    }

    fun cancel() {
        if (isDragging) interactionState.onDragCancelled()
        isDragging = false
    }
}

/** Position in the list of the center of the item with [key], if it is shown. */
private fun LazyListState.centerOf(key: Any): Float? =
    layoutInfo.visibleItemsInfo.firstOrNull { it.key == key }?.let { it.offset + it.size / 2f }
