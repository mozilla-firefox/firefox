/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.browser.compose.interactable

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.zIndex
import kotlin.math.abs
import kotlin.math.pow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.mozilla.fenix.tabstray.browser.compose.ItemInteractionState
import org.mozilla.fenix.tabstray.controller.ItemInteractionHandler
import org.mozilla.fenix.tabstray.ui.tabitems.Elevation
import org.mozilla.fenix.tabstray.ui.tabitems.defaultListItemAnimation

/**
 * Remember the reordering state for reordering list items.
 *
 * @param listState State of the list.
 * @param ignoredItems Set of keys for non-draggable items.
 * @param liveReorderEnabled Whether reordering should happen 'live' while a drag is active.
 * @param onLongPress Callback to be invoked when long pressing an item.
 * @param itemInteractionHandler Handler for item interactions.
 * @param dragAndDropEnabled Whether the drag and drop feature is enabled for this list.
 */
@Composable
fun createListInteractionState(
    listState: LazyListState,
    ignoredItems: Set<Any>,
    liveReorderEnabled: Boolean,
    onLongPress: (LazyListItemInfo) -> Unit = {},
    itemInteractionHandler: ItemInteractionHandler,
    dragAndDropEnabled: Boolean = true,
): ListInteractionState {
    val scope = rememberCoroutineScope()
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val hapticFeedback = LocalHapticFeedback.current
    val state =
        remember(listState) {
            ListInteractionStateImpl(
                listState = listState,
                scope = scope,
                touchSlop = touchSlop,
                hapticFeedback = hapticFeedback,
                ignoredItems = ignoredItems,
                onLongPress = onLongPress,
                itemInteractionHandler = itemInteractionHandler,
                dragAndDropEnabled = dragAndDropEnabled,
                liveReorderEnabled = liveReorderEnabled,
            )
        }
    return state
}

/** Stable snapshot interface for a list's interaction state. */
@Stable
interface ListInteractionState {
    /** The currently dragged item. Can be [InteractionState.List.None] */
    val draggedItem: InteractionState.List

    /** The currently hovered item. Can be [InteractionState.List.None] */
    val hoveredItem: InteractionState.List

    /** The [Rect] used to display a reorder placement indicator */
    val highlightedRect: Rect?

    /** The current [InteractionMode], e.g. reordering, scrolling, drag and drop */
    val interactionMode: InteractionMode.List

    /** The previously dragged item's key */
    val previousKeyOfDraggedItem: Any?

    /** The list's orientation */
    val orientation: Orientation

    /** Cached offset used to animate the item from a cancelled drag back into place */
    val previousItemAnimatableOffset: Animatable<Float, AnimationVector1D>

    /** An item's size */
    val itemSize: Int?

    /**
     * Called when a slop threshold has been exceeded to start a drag event.
     *
     * @param offset The offset for the drag event
     * @param shouldLongPress Whether long press is needed to initiate a drag event
     */
    fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean)

    /**
     * Called when a drag event is updated.
     *
     * @param offset the latest offset for the drag event
     * @param preserveSelectMode whether select mode should be preserved
     */
    fun onDrag(offset: Float, preserveSelectMode: Boolean)

    /** Called when a drag event ends. */
    fun onDragEnd()

    /** Called when a drag is cancelled, for example, when a user lets go without performing an action. */
    fun onDragCancelled()

    /**
     * Computes the offset of an item at a given index.
     *
     * @param index the item's index
     */
    fun computeItemOffset(index: Int): Float

    /** Called to indicate to the list that the drop handling has been completed and the state can be reset. */
    fun reset()
}

/**
 * Class containing details about the current state of dragging in list.
 *
 * @param listState State of the list.
 * @param scope [CoroutineScope] used for scrolling to the target item.
 * @param hapticFeedback [HapticFeedback] used for performing haptic feedback on item long press.
 * @param touchSlop Distance in pixels the user can wander until we consider they started dragging.
 * @param ignoredItems List of keys for non-draggable items.
 * @param itemInteractionHandler Handler for item interactions.
 * @param dragAndDropEnabled Whether the drag and drop feature is enabled for this list.
 * @param liveReorderEnabled Whether reordering should happen 'live' while a drag is active.
 * @param onLongPress Optional callback to be invoked when long pressing an item.
 */
class ListInteractionStateImpl
internal constructor(
    private val listState: LazyListState,
    private val scope: CoroutineScope,
    private val hapticFeedback: HapticFeedback,
    private val touchSlop: Float,
    private val ignoredItems: Set<Any>,
    private val itemInteractionHandler: ItemInteractionHandler,
    private val dragAndDropEnabled: Boolean,
    private val liveReorderEnabled: Boolean,
    private val onLongPress: (LazyListItemInfo) -> Unit = {},
) : ListInteractionState {
    override var draggedItem by mutableStateOf<InteractionState.List>(InteractionState.List.None)
        private set

    override var hoveredItem by mutableStateOf<InteractionState.List>(InteractionState.List.None)
        private set

    override var highlightedRect by mutableStateOf<Rect?>(null)
        private set

    override var interactionMode by mutableStateOf<InteractionMode.List>(InteractionMode.List.None)
        private set

    private var scrollJob: Job? = null

    internal var moved by mutableStateOf(false)
        private set

    override var previousKeyOfDraggedItem by mutableStateOf<Any?>(null)
        private set

    override val previousItemAnimatableOffset = Animatable(0f)

    override val orientation: Orientation
        get() = listState.layoutInfo.orientation

    override val itemSize: Int?
        get() = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key !in ignoredItems }?.size

    override fun computeItemOffset(index: Int): Float {
        val itemAtIndex = listState.layoutInfo.visibleItemsInfo.firstOrNull { info -> info.index == index }
        if (itemAtIndex != null) {
            return (draggedItem.initialOffset + draggedItem.cumulatedOffset - itemAtIndex.offset)
        }
        return draggedItem.initialOffset + draggedItem.cumulatedOffset
    }

    override fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean) {
        listState.findItem(offset)?.also { item ->
            val key = item.key as? String
            if (shouldLongPress) {
                hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                onLongPress(item)
            }
            key?.let {
                draggedItem =
                    InteractionState.List.Active(
                        index = item.index,
                        key = it,
                        initialOffset = item.offset.toFloat(),
                    )
            }
        }
    }

    override fun reset() {
        resetState()
    }

    override fun onDragEnd() {
        if (draggedItem is InteractionState.List.Active) {
            handleDragEnd(interactionMode)
        }
    }

    private fun doReorder(mode: InteractionMode.List.Reordering) {
        if (draggedItem.index == listState.firstVisibleItemIndex) {
            itemSize?.let { height ->
                autoScroll(height.toFloat())
            }
        }
        itemInteractionHandler.onMove(
            sourceKey = mode.source.key,
            targetKey = mode.target.key,
            placeAfter = mode.placeAfter,
        )
    }

    private fun handleDragEnd(mode: InteractionMode.List) {
        when (mode) {
            is InteractionMode.List.DragAndDrop -> {
                itemInteractionHandler.onDrop(
                    sourceKey = mode.source.key,
                    targetKey = mode.target.key,
                )
            }

            is InteractionMode.List.Reordering -> {
                if (!liveReorderEnabled) {
                    doReorder(mode)
                }
                itemInteractionHandler.onDragCancel()
                resetState()
            }

            is InteractionMode.List.Scroll,
            is InteractionMode.List.None -> {
                // No action is taken
                if (moved) {
                    itemInteractionHandler.onDragCancel()
                }
                resetState()
            }
        }
    }

    override fun onDragCancelled() {
        if (moved) {
            itemInteractionHandler.onDragCancel()
        }
        resetState()
    }

    private fun resetState() {
        if (draggedItem is InteractionState.List.Active) {
            val draggingItem = draggedItem as InteractionState.List.Active
            previousKeyOfDraggedItem = draggedItem.key
            val startOffset = computeItemOffset(draggingItem.index)
            scope.launch {
                if (previousItemAnimatableOffset.isRunning) {
                    previousItemAnimatableOffset.stop()
                }
                previousItemAnimatableOffset.snapTo(startOffset)
                previousItemAnimatableOffset.animateTo(
                    0f,
                    spring(
                        stiffness = Spring.StiffnessMediumLow,
                        visibilityThreshold = 1f,
                    ),
                )
                previousKeyOfDraggedItem = null
            }
        }
        draggedItem = InteractionState.List.None
        hoveredItem = InteractionState.List.None
        highlightedRect = null
        interactionMode = InteractionMode.List.None
        moved = false
        scrollJob?.cancel()
        scrollJob = null
    }

    /**
     * Restricts all scroll actions to run in a single job. If a scroll job is currently executing when a new one is
     * asked for, it is cancelled.
     */
    fun autoScroll(amount: Float) {
        scrollJob?.cancel()
        scrollJob = scope.launch {
            listState.scrollBy(amount)
        }
    }

    private fun handleReorderingModeOnDrag(mode: InteractionMode.List.Reordering) {
        hoveredItem = InteractionState.List.None
        if (liveReorderEnabled) {
            doReorder(mode)
            // Update the dragged item's index as reorders happen
            val newIndex = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == draggedItem.key }?.index ?: -1
            if (newIndex != -1) {
                draggedItem = draggedItem.copyWithNewIndex(newIndex)
            }
        } else {
            highlightedRect = mode.rect
        }
    }

    private fun handleDragAndDropModeOnDrag(mode: InteractionMode.List.DragAndDrop) {
        highlightedRect = null
        if (hoveredItem != mode.target) {
            hoveredItem = mode.target
        }
    }

    private fun handleNoInteractionModeOnDrag(itemOffset: ListItemOffset) {
        highlightedRect = null
        hoveredItem = InteractionState.List.None
        val overscroll =
            findOverscroll(
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                listState = listState,
            )
        if (overscroll != 0f) {
            autoScroll(overscroll)
        }
    }

    override fun onDrag(offset: Float, preserveSelectMode: Boolean) {
        draggedItem = draggedItem.incrementCumulatedOffset(offset)
        if (!moved && abs(draggedItem.cumulatedOffset) > touchSlop) {
            draggedItem = draggedItem.markAsMoved()
            (draggedItem as? InteractionState.List.Active)?.let { active ->
                itemInteractionHandler.onDragStart(
                    sourceKey = active.key,
                    preserveSelectMode = preserveSelectMode,
                )
            }
            moved = true
        }
        val draggingItem = draggedItem as? InteractionState.List.Active ?: return
        val itemOffset =
            ListItemOffset(
                draggedItem = draggingItem,
                draggingItemOffset = computeItemOffset(draggingItem.index),
                itemSize = itemSize ?: 0,
            )
        val mode =
            determineInteractionMode(
                listState = listState,
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                ignoredItems = ignoredItems,
                dragAndDropEnabled = dragAndDropEnabled,
            )
        // Debounce duplicate reorder events.
        // Note that multiple duplicate scroll events are valid
        if (mode == interactionMode && interactionMode is InteractionMode.List.Reordering) {
            return
        }
        interactionMode = mode
        when (mode) {
            is InteractionMode.List.DragAndDrop -> {
                handleDragAndDropModeOnDrag(mode = mode)
            }

            is InteractionMode.List.Reordering -> {
                handleReorderingModeOnDrag(mode = mode)
            }

            is InteractionMode.List.None,
            is InteractionMode.List.Scroll -> {
                handleNoInteractionModeOnDrag(itemOffset = itemOffset)
            }
        }
    }
}

private fun determineInteractionMode(
    listState: LazyListState,
    draggedItem: InteractionState.List,
    itemOffset: ListItemOffset,
    ignoredItems: Set<Any>,
    dragAndDropEnabled: Boolean,
): InteractionMode.List {
    if (listState.isScrollInProgress) return InteractionMode.List.None
    if (draggedItem is InteractionState.List.None) return InteractionMode.List.None

    val topCandidate =
        gatherCandidates(
                listState = listState,
                draggedItemOffset = itemOffset,
                draggedItem = draggedItem,
                ignoredItems = ignoredItems,
            )
            .filter { item ->
                // Filter out the drag and drop interaction type if it is disabled, as in private tabs
                dragAndDropEnabled || item.type !is InteractionType.Overlap
            }
            .minByOrNull { it.score }

    val key = topCandidate?.anchorItem?.key as? String
    return when {
        topCandidate == null || key == null -> {
            InteractionMode.List.None
        }

        topCandidate.type is InteractionType.Overlap -> {
            InteractionMode.List.DragAndDrop(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                    ),
            )
        }

        topCandidate.type is InteractionType.TopGutter -> {
            InteractionMode.List.Reordering(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                    ),
                placeAfter = false,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.BottomGutter -> {
            InteractionMode.List.Reordering(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                    ),
                placeAfter = true,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.Scroll -> {
            InteractionMode.List.Scroll(topCandidate.type.scroll)
        }

        else -> InteractionMode.List.None
    }
}

/**
 * Calculates the distance from the closest point on a [Rect] object to a given point in space represented as an
 * [Offset]. Uses getDistanceSquared() for performance reasons, which is appropriate for comparisons to other distances
 * calculated with the same method. Returns a float value representing the distance.
 *
 * @param offset: [Offset] representing a comparison point in spce.
 */
@androidx.annotation.VisibleForTesting
internal fun Rect.closestDistanceTo(offset: Float): Float {
    return (this.closestPointTo(offset) - offset).pow(2f)
}

internal fun Rect.closestPointTo(offset: Float): Float {
    return offset.coerceIn(this.top, this.bottom)
}

private fun getScrollCandidates(
    listState: LazyListState,
    draggedItemOffset: ListItemOffset,
): List<ListInteractionCandidate> {
    val firstVisible = listState.layoutInfo.visibleItemsInfo.firstOrNull() ?: return emptyList()
    val candidates = mutableListOf<ListInteractionCandidate>()
    val scrollRectSize =
        Size(
            width = listState.layoutInfo.viewportSize.width.toFloat(),
            height = firstVisible.size / 3.0f,
        )
    // Scroll up can only be a candidate if we are not at the top of the view
    if (listState.firstVisibleItemIndex > 0) {
        val scrollUpRect =
            Rect(
                offset = Offset(0f, 0f),
                size = scrollRectSize,
            )
        if (scrollUpRect.bottom > draggedItemOffset.start) {
            candidates.add(
                ListInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.start.minus(scrollUpRect.bottom)),
                    anchorItem = firstVisible,
                    score = scrollUpRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    // Scroll down can only be a candidate if we are not at the bottom of the view
    val lastVisible = listState.layoutInfo.visibleItemsInfo.lastOrNull() ?: return candidates
    if (lastVisibleItemIndex(listState) < listState.layoutInfo.totalItemsCount - 1) {
        val scrollDownRect =
            Rect(
                offset = Offset(0f, listState.layoutInfo.viewportSize.height - scrollRectSize.height),
                size = scrollRectSize,
            )
        if (scrollDownRect.top < draggedItemOffset.end) {
            candidates.add(
                ListInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.end.minus(scrollDownRect.top)),
                    anchorItem = lastVisible,
                    score = scrollDownRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    return candidates
}

private fun lastVisibleItemIndex(listState: LazyListState): Int {
    return listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0
}

internal fun gatherCandidates(
    listState: LazyListState,
    draggedItemOffset: ListItemOffset,
    draggedItem: InteractionState,
    ignoredItems: Set<Any>,
): List<ListInteractionCandidate> {
    val candidates = mutableListOf<ListInteractionCandidate>()
    candidates.addAll(
        elements =
            getScrollCandidates(
                listState = listState,
                draggedItemOffset = draggedItemOffset,
            )
    )
    for (itemCandidate in listState.layoutInfo.visibleItemsInfo) {
        if (itemCandidate.key in ignoredItems || itemCandidate.key == draggedItem.key) continue
        val gutterSpacing = listState.layoutInfo.mainAxisItemSpacing
        val horizontalGutterSize =
            Size(
                width = listState.layoutInfo.viewportSize.width.toFloat(),
                height = gutterSpacing.toFloat(),
            )

        // body candidate
        val body =
            Rect(
                offset = Offset(x = 0f, y = itemCandidate.offset.toFloat()),
                size =
                    Size(
                        width = listState.layoutInfo.viewportSize.width.toFloat(),
                        height = itemCandidate.size.toFloat(),
                    ),
            )
        val distanceToCenter = (body.center.y - draggedItemOffset.center).pow(2)
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.Overlap,
                anchorItem = itemCandidate,
                score = distanceToCenter,
            )
        )

        // top gutter candidate
        val topGutter =
            Rect(
                offset =
                    Offset(
                        x = 0f,
                        y = itemCandidate.offset.toFloat(),
                    ),
                size = horizontalGutterSize,
            )
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.TopGutter(rect = topGutter),
                anchorItem = itemCandidate,
                score = topGutter.closestDistanceTo(draggedItemOffset.center),
            )
        )

        // bottom gutter candidate
        val bottomGutter =
            Rect(
                offset =
                    Offset(
                        x = 0f,
                        y = itemCandidate.endOffset.toFloat(),
                    ),
                size = horizontalGutterSize,
            )
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.BottomGutter(rect = bottomGutter),
                anchorItem = itemCandidate,
                score = bottomGutter.closestDistanceTo(draggedItemOffset.center),
            )
        )
    }
    return candidates
}

private fun findOverscroll(
    draggedItem: InteractionState.List,
    itemOffset: ListItemOffset,
    listState: LazyListState,
): Float {
    return when {
        draggedItem.cumulatedOffset > 0 -> (itemOffset.end - listState.layoutInfo.viewportEndOffset).coerceAtLeast(0f)

        draggedItem.cumulatedOffset < 0 ->
            (itemOffset.start - listState.layoutInfo.viewportStartOffset).coerceAtMost(0f)

        else -> 0f
    }
}

/**
 * Container for draggable list item.
 *
 * @param state List reordering state.
 * @param key Key of the item to be displayed.
 * @param position Position in the list of the item to be displayed.
 * @param enteringItemKey The key of the item entering composition, if any. Can be null.
 * @param content Content of the item to be displayed.
 */
@Composable
fun LazyItemScope.InteractableDragItemContainer(
    state: ListInteractionState,
    key: Any,
    position: Int,
    enteringItemKey: String? = null,
    content: @Composable (itemInteractionState: ItemInteractionState) -> Unit,
) {
    val modifier =
        when (key) {
            state.draggedItem.key -> {
                Modifier.zIndex(Elevation.DRAGGED_ITEM).graphicsLayer {
                    when (state.orientation) {
                        Orientation.Vertical -> translationY = state.computeItemOffset(position)
                        Orientation.Horizontal -> translationX = state.computeItemOffset(position)
                    }
                }
            }

            state.previousKeyOfDraggedItem -> {
                Modifier.zIndex(Elevation.DRAGGED_ITEM).graphicsLayer {
                    when (state.orientation) {
                        Orientation.Vertical -> translationY = state.previousItemAnimatableOffset.value

                        Orientation.Horizontal -> translationX = state.previousItemAnimatableOffset.value
                    }
                }
            }

            else -> {
                Modifier.zIndex(Elevation.NO_INTERACTION)
            }
        }.defaultListItemAnimation(
            lazyListItemScope = this,
            // suppress item transitions when group entrance animation is playing
            suppressTransitions = enteringItemKey != null,
        )
    Box(modifier = modifier, propagateMinConstraints = true) {
        content(
            ItemInteractionState(
                isHoveredByItem = key == state.hoveredItem.key,
                isDragged = key == state.draggedItem.key,
                isEntering = key == enteringItemKey,
            )
        )
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
 * @param interactionState List interaction state used for dragging callbacks.
 * @param shouldLongPressToDrag Whether or not an item should be long pressed to start the dragging gesture.
 */
fun Modifier.detectListPressAndDrag(
    listState: LazyListState,
    interactionState: ListInteractionState,
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
                        interactionState.onTouchSlopPassed(offsetInOrientation, true)
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        val dragOffset =
                            when (listState.layoutInfo.orientation) {
                                Orientation.Vertical -> dragAmount.y
                                Orientation.Horizontal -> dragAmount.x
                            }
                        interactionState.onDrag(dragOffset, false)
                    },
                    onDragEnd = interactionState::onDragEnd,
                    onDragCancel = interactionState::onDragCancelled,
                )
            }
        }

/**
 * Data class that simplifies calculations of the center, start, and end of a list item.
 *
 * @property draggedItem The current item being dragged
 * @property draggingItemOffset The offset of the dragged item
 * @property itemSize The item's size, in Int
 */
data class ListItemOffset(
    val draggedItem: InteractionState.List.Active,
    val draggingItemOffset: Float,
    val itemSize: Int,
) {
    val start by lazy {
        draggedItem.initialOffset + draggedItem.cumulatedOffset
    }

    val end by lazy {
        start + itemSize
    }

    val center by lazy {
        start + (end - start) / 2f
    }
}
