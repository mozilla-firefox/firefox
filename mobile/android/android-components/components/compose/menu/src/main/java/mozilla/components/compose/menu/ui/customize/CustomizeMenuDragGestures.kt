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
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ListInteractionState

/**
 * Drag the item with [groupId] once the user long presses where this is applied.
 *
 * @param dragState State of dragging the items, or `null` to not drag them.
 * @param groupId Id of the item to drag.
 */
internal fun Modifier.dragGroupAfterLongPress(dragState: CustomizeMenuDragState<*>?, groupId: Any): Modifier =
    dragState?.let { dragAfterLongPress(it.interactionState, it.listState, groupId) } ?: this

/**
 * Drag the item with [groupId] as soon as the user moves vertically where this is applied, without a long press, or
 * call [onLongPress] once the user held it for a long press without moving, after which this touch no longer drags it.
 *
 * @param dragState State of dragging the items, or `null` to not drag them.
 * @param groupId Id of the item to drag.
 * @param onLongPress Called when the user long presses without moving, while the finger is still down.
 */
internal fun Modifier.dragGroup(
    dragState: CustomizeMenuDragState<*>?,
    groupId: Any,
    onLongPress: () -> Unit,
): Modifier = dragState?.let { dragOnVerticalMove(it.interactionState, it.listState, groupId, onLongPress) } ?: this

/**
 * Drag the list item with [key] once the user long presses where this is applied.
 *
 * @param interactionState State of dragging the items of the list.
 * @param listState State of the list showing the item.
 * @param key Key of the list item to drag.
 */
internal fun Modifier.dragAfterLongPress(
    interactionState: ListInteractionState,
    listState: LazyListState,
    key: Any,
): Modifier =
    this.pointerInput(interactionState, listState, key) {
        val gesture = ListItemDragGesture(interactionState, listState, key)
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

/**
 * Drag the list item with [key] as soon as the user moves vertically where this is applied, without a long press, or
 * call [onLongPress] once the user held it for a long press without moving, after which this touch no longer drags it.
 *
 * @param interactionState State of dragging the items of the list.
 * @param listState State of the list showing the item.
 * @param key Key of the list item to drag.
 * @param onLongPress Called when the user long presses without moving, while the finger is still down. If `null`, a
 *   long press without moving does nothing and the item is still dragged once the user moves vertically.
 */
internal fun Modifier.dragOnVerticalMove(
    interactionState: ListInteractionState,
    listState: LazyListState,
    key: Any,
    onLongPress: (() -> Unit)?,
): Modifier =
    this.pointerInput(interactionState, listState, key) {
        val gesture = ListItemDragGesture(interactionState, listState, key)
        awaitEachGesture {
            val down = awaitFirstDown()
            val slopChange =
                awaitVerticalSlopOrLongPress(down, gesture, interactionState, onLongPress) ?: return@awaitEachGesture
            try {
                // Everything moved since the finger went down, for the item not to stay behind by the touch slop.
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

/** Wait for the pointer of [down] to move vertically by the touch slop, picking the item up then if it can be. */
private suspend fun AwaitPointerEventScope.awaitVerticalSlop(
    down: PointerInputChange,
    gesture: ListItemDragGesture,
): PointerInputChange? =
    // The touch is left to the list unless the item is dragged.
    awaitVerticalTouchSlopOrCancellation(down.id) { change, _ ->
        if (gesture.start(shouldLongPress = false)) change.consume()
    }

/**
 * Wait for the pointer of [down] to move vertically by the touch slop, or call [onLongPress], if not `null`, if it is
 * held still for a long press, in which case this consumes the rest of the touch and returns `null`.
 */
private suspend fun AwaitPointerEventScope.awaitVerticalSlopOrLongPress(
    down: PointerInputChange,
    gesture: ListItemDragGesture,
    interactionState: ListInteractionState,
    onLongPress: (() -> Unit)?,
): PointerInputChange? {
    if (onLongPress == null) return awaitVerticalSlop(down, gesture)
    var slop: PointerInputChange? = null
    val isBeforeLongPress =
        withTimeoutOrNull(viewConfiguration.longPressTimeoutMillis) {
            slop = awaitVerticalSlop(down, gesture)
            true
        }
    if (isBeforeLongPress != null) return slop
    if (isStill(down) && interactionState.draggedItem == InteractionState.List.None) {
        onLongPress()
        consumeUntilUp(down.id)
        return null
    }
    return awaitVerticalSlop(down, gesture)
}

/** Whether the pointer of [down] is still down within the touch slop of where it went down. */
private fun AwaitPointerEventScope.isStill(down: PointerInputChange): Boolean {
    val change = currentEvent.changes.firstOrNull { it.id == down.id } ?: return false
    return change.pressed && (change.position - down.position).getDistance() <= viewConfiguration.touchSlop
}

/** Consume the changes of the pointer with [id] until it is lifted or the gesture is cancelled. */
private suspend fun AwaitPointerEventScope.consumeUntilUp(id: PointerId) {
    do {
        val change = awaitPointerEvent().changes.firstOrNull { it.id == id }
        change?.consume()
    } while (change != null && change.pressed)
}

/**
 * Drags the list item with [key] for one gesture at a time. A gesture drags only if no other item is dragged when it
 * starts, and leaves the drag of another gesture alone, as the state it drives is shared by all items of the list.
 */
private class ListItemDragGesture(
    private val interactionState: ListInteractionState,
    private val listState: LazyListState,
    private val key: Any,
) {
    private var isDragging = false

    /** Pick the item up, if no other item is dragged. Returns whether the item is dragged by this gesture. */
    fun start(shouldLongPress: Boolean): Boolean {
        val center = listState.centerOf(key)
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
