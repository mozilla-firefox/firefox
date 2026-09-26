/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.browser.compose

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.zIndex
import kotlin.math.abs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/**
 * Remember the reordering state for reordering list items.
 *
 * @param listState State of the list.
 * @param onMove Callback to be invoked when switching between two items.
 * @param ignoredItems List of keys for non-draggable items.
 * @param onLongPress Callback to be invoked when long pressing an item.
 * @param onExitLongPress Callback to be invoked when the item is dragged after long press.
 */
@Composable
fun createListReorderState(
    listState: LazyListState,
    onMove: (LazyListItemInfo, LazyListItemInfo) -> Unit,
    ignoredItems: List<Any>,
    onLongPress: (LazyListItemInfo) -> Unit = {},
    onExitLongPress: (sourceKey: Any) -> Unit = {},
): ListReorderState {
    val scope = rememberCoroutineScope()
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val hapticFeedback = LocalHapticFeedback.current
    val state =
        remember(listState) {
            ListReorderState(
                listState = listState,
                onMove = onMove,
                scope = scope,
                touchSlop = touchSlop,
                hapticFeedback = hapticFeedback,
                ignoredItems = ignoredItems,
                onLongPress = onLongPress,
                onExitLongPress = onExitLongPress,
            )
        }
    return state
}

/**
 * Class containing details about the current state of dragging in list.
 *
 * @param listState State of the list.
 * @param scope [CoroutineScope] used for scrolling to the target item.
 * @param hapticFeedback [HapticFeedback] used for performing haptic feedback on item long press.
 * @param touchSlop Distance in pixels the user can wander until we consider they started dragging.
 * @param onMove Callback to be invoked when switching between two items.
 * @param ignoredItems List of keys for non-draggable items.
 * @param onLongPress Optional callback to be invoked when long pressing an item.
 * @param onExitLongPress Optional callback to be invoked when the item is dragged after long press.
 */
class ListReorderState
internal constructor(
    private val listState: LazyListState,
    private val scope: CoroutineScope,
    private val hapticFeedback: HapticFeedback,
    private val touchSlop: Float,
    private val onMove: (LazyListItemInfo, LazyListItemInfo) -> Unit,
    private val ignoredItems: List<Any>,
    private val onLongPress: (LazyListItemInfo) -> Unit,
    private val onExitLongPress: (sourceKey: Any) -> Unit,
) {
    var draggingItemKey by mutableStateOf<Any?>(null)
        private set

    private var draggingItemCumulatedOffset by mutableFloatStateOf(0f)
    private var draggingItemInitialOffset by mutableFloatStateOf(0f)
    internal var moved by mutableStateOf(false)
    private val draggingItemOffset: Float
        get() =
            draggingItemLayoutInfo?.let { item ->
                draggingItemInitialOffset + draggingItemCumulatedOffset - item.offset
            } ?: 0f

    internal fun computeItemOffset(index: Int): Float {
        val itemAtIndex =
            listState.layoutInfo.visibleItemsInfo.firstOrNull { info -> info.index == index }
                ?: return draggingItemOffset
        return draggingItemInitialOffset + draggingItemCumulatedOffset - itemAtIndex.offset
    }

    private val draggingItemLayoutInfo: LazyListItemInfo?
        get() = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == draggingItemKey }

    internal var previousKeyOfDraggedItem by mutableStateOf<Any?>(null)
        private set

    internal var previousItemOffset = Animatable(0f)
        private set

    internal val orientation: Orientation
        get() = listState.layoutInfo.orientation

    internal fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean) {
        listState.findItem(offset)?.also {
            draggingItemKey = it.key
            if (shouldLongPress) {
                hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                onLongPress(it)
            }
            draggingItemInitialOffset = it.offset.toFloat()
            moved = !shouldLongPress
        }
    }

    internal fun onDragInterrupted() {
        if (draggingItemKey != null) {
            previousKeyOfDraggedItem = draggingItemKey
            val startOffset = draggingItemOffset
            scope.launch {
                previousItemOffset.snapTo(startOffset)
                previousItemOffset.animateTo(
                    0f,
                    spring(
                        stiffness = Spring.StiffnessMediumLow,
                        visibilityThreshold = 1f,
                    ),
                )
                previousKeyOfDraggedItem = null
            }
        }
        draggingItemCumulatedOffset = 0f
        draggingItemKey = null
        draggingItemInitialOffset = 0f
    }

    @Suppress("CognitiveComplexMethod")
    internal fun onDrag(offset: Float) {
        draggingItemCumulatedOffset += offset

        if (draggingItemLayoutInfo == null) {
            moved = false
        }
        val draggingItem = draggingItemLayoutInfo ?: return

        if (!moved && abs(draggingItemCumulatedOffset) > touchSlop) {
            draggingItemKey?.let { onExitLongPress(it) }
        }
        val startOffset = draggingItem.offset + draggingItemOffset
        val endOffset = startOffset + draggingItem.size
        val middleOffset = startOffset + (endOffset - startOffset) / 2f

        val targetItem =
            listState.layoutInfo.visibleItemsInfo.find { item ->
                middleOffset.toInt() in item.offset..item.endOffset && draggingItemKey != item.key
            }

        if (targetItem != null && targetItem.key !in ignoredItems) {
            if (
                draggingItem.index == listState.firstVisibleItemIndex ||
                    targetItem.index == listState.firstVisibleItemIndex
            ) {
                scope.launch {
                    onMove.invoke(draggingItem, targetItem)
                    listState.scrollBy(draggingItem.size.toFloat())
                }
            } else {
                onMove.invoke(draggingItem, targetItem)
            }
        } else {
            val overscroll =
                when {
                    draggingItemCumulatedOffset > 0 ->
                        (endOffset - listState.layoutInfo.viewportEndOffset).coerceAtLeast(0f)

                    draggingItemCumulatedOffset < 0 ->
                        (startOffset - listState.layoutInfo.viewportStartOffset).coerceAtMost(0f)

                    else -> 0f
                }
            if (overscroll != 0f) {
                scope.launch {
                    listState.scrollBy(overscroll)
                }
            }
        }
    }
}

/**
 * Container for draggable list item.
 *
 * @param state List reordering state.
 * @param key Key of the item to be displayed.
 * @param position Position in the list of the item to be displayed.
 * @param content Content of the item to be displayed.
 */
@Composable
fun LazyItemScope.ReorderableDragItemContainer(
    state: ListReorderState,
    key: Any,
    position: Int,
    content: @Composable () -> Unit,
) {
    val modifier =
        when (key) {
            state.draggingItemKey -> {
                Modifier.zIndex(1f).graphicsLayer {
                    when (state.orientation) {
                        Orientation.Vertical -> translationY = state.computeItemOffset(position)
                        Orientation.Horizontal -> translationX = state.computeItemOffset(position)
                    }
                }
            }

            state.previousKeyOfDraggedItem -> {
                Modifier.zIndex(1f).graphicsLayer {
                    when (state.orientation) {
                        Orientation.Vertical -> translationY = state.previousItemOffset.value
                        Orientation.Horizontal -> translationX = state.previousItemOffset.value
                    }
                }
            }

            else -> {
                Modifier.zIndex(0f).animateItem(tween())
            }
        }
    Box(modifier = modifier, propagateMinConstraints = true) {
        content()
    }
}

/** Calculates the offset of an item taking its height into account. */
private val LazyListItemInfo.endOffset: Int
    get() = offset + size

/**
 * Find item based on position on screen.
 *
 * @param offset Position on screen used to find the item.
 */
private fun LazyListState.findItem(offset: Float) =
    layoutInfo.visibleItemsInfo.firstOrNull { item ->
        offset.toInt() in item.offset..item.endOffset
    }

/**
 * Detects press, long press and drag gestures.
 *
 * @param listState State of the list.
 * @param reorderState List reordering state used for dragging callbacks.
 * @param shouldLongPressToDrag Whether or not an item should be long pressed to start the dragging gesture.
 */
fun Modifier.detectListPressAndDrag(
    listState: LazyListState,
    reorderState: ListReorderState,
    shouldLongPressToDrag: Boolean,
): Modifier =
    this then
        Modifier.pointerInput(listState, shouldLongPressToDrag) {
            if (shouldLongPressToDrag) {
                detectDragGesturesAfterLongPress(
                    onDragStart = { offset ->
                        val offsetInOrientation =
                            when (listState.layoutInfo.orientation) {
                                Orientation.Vertical -> offset.y
                                Orientation.Horizontal -> offset.x
                            }
                        reorderState.onTouchSlopPassed(offsetInOrientation, true)
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        val dragOffset =
                            when (listState.layoutInfo.orientation) {
                                Orientation.Vertical -> dragAmount.y
                                Orientation.Horizontal -> dragAmount.x
                            }
                        reorderState.onDrag(dragOffset)
                    },
                    onDragEnd = reorderState::onDragInterrupted,
                    onDragCancel = reorderState::onDragInterrupted,
                )
            }
        }
